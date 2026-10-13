"""Read-only: poll NPC health (arkCpntHealthEvolved) and log a CSV row whenever an NPC's health/dead/unconscious state changes.

Usage: python tools/npc_watch.py --pid P [--near X Y Z --radius R] [--hz 60] [--seconds N] [--out file.csv]
       python tools/npc_watch.py --selftest
Timestamps: perf_s = time.perf_counter() (QueryPerformanceCounter, same clock as C++ steady_clock/QPC in the mod),
tick_ms = GetTickCount64, prev_poll_s = start of the previous poll (the true change time lies in (prev_poll_s, perf_s]).
NPC set is fixed at start (corpses skipped); NPCs freed by the engine are logged once as event=lost. Offsets: docs/entities.md.
"""
import argparse
import csv
import ctypes
import math
import struct
import sys
import time
from entities import scan, GAME_PTR, PLAYER, H_CUR, E_STATE, S_DEAD, S_UNCON, REF_POS
from scan_body_material import attach, try_read
from rig_fields import pointer

HEALTH_VT = 0x1c51588
COLS = 'event perf_s tick_ms prev_poll_s slot name hp max prev_hp dead unconscious dist npc_x npc_y npc_z player_x player_y player_z'.split()


def sample(read, n):
    """(vtable, owner, hp, state byte) of one tracked NPC; raises on unreadable memory."""
    vt, _, owner = struct.unpack('<QQQ', read(n['comp'], 24))
    return vt, owner, struct.unpack('<f', read(n['comp'] + H_CUR, 4))[0], read(n['ent'] + E_STATE, 1)[0]


def poll_once(read, base, track, emit):
    """One pass over tracked NPCs; calls emit(kind, npc, hp, dead, uncon, prev_hp) for changes/lost. Mutates npc state."""
    for n in list(track):
        try:
            vt, owner, hp, st = sample(read, n)
            if vt != base + HEALTH_VT or owner != n['ent']:
                raise ValueError('stale')
        except (OSError, ValueError, struct.error):
            track.remove(n)
            emit('lost', n, n['hp'], n['dead'], n['uncon'], n['hp'])
            continue
        dead, uncon = bool(st & S_DEAD), bool(st & S_UNCON)
        if (hp, dead, uncon) != (n['hp'], n['dead'], n['uncon']):
            prev, n['hp'], n['dead'], n['uncon'] = n['hp'], hp, dead, uncon
            emit('change', n, hp, dead, uncon, prev)
            if dead:
                track.remove(n)  # death is final; keep the poll loop cheap


def selftest():
    mem = {}

    def put(at, fmt, *v):
        for i, b in enumerate(struct.pack(fmt, *v)):
            mem[at + i] = b

    def read(at, size):
        return bytes(mem[at + i] for i in range(size))
    B, E, C = 0x140000000, 0x2000, 0x3000
    put(C, '<QQQ', B + HEALTH_VT, 0, E); put(C + H_CUR, '<f', 30.0); put(E + E_STATE, '<B', 0x80)
    n = dict(ent=E, comp=C, hp=30.0, dead=False, uncon=False)
    track, log = [n], []
    emit = lambda *a: log.append(a)
    poll_once(read, B, track, emit); assert not log
    put(C + H_CUR, '<f', 10.0); poll_once(read, B, track, emit); assert log[-1][0] == 'change' and log[-1][5] == 30.0
    put(C + H_CUR, '<f', 0.0); put(E + E_STATE, '<B', 0x88); poll_once(read, B, track, emit)
    assert log[-1][3] and not track and len(log) == 2
    track.append(dict(n, hp=0.0, dead=False)); put(C, '<Q', 0); poll_once(read, B, track, emit); assert log[-1][0] == 'lost'
    print('PASS npc_watch selftest')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--pid', type=int)
    ap.add_argument('--near', type=float, nargs=3, metavar=('X', 'Y', 'Z'))
    ap.add_argument('--radius', type=float)
    ap.add_argument('--hz', type=float, default=60)
    ap.add_argument('--seconds', type=float, default=0, help='0 = until Ctrl-C')
    ap.add_argument('--out')
    ap.add_argument('--selftest', action='store_true')
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    if not a.pid:
        ap.error('--pid required')
    base, read = attach(a.pid)
    ents, player = scan(read, base, 'npc', health=True)
    origin = a.near or player['pos']
    track = []
    for e in ents:
        h = e.get('health')
        if h and not h['dead'] and (a.radius is None or math.dist(e['pos'], origin) <= a.radius):
            track.append(dict(ent=int(e['addr'], 16), comp=int(h['comp'], 16), frame=int(e['frame'], 16), slot=e['slot'],
                              name=e['name'], hp=h['cur'], max=h['max'], dead=h['dead'], uncon=h['uncon']))
    pframe = int(player['frame'], 16)
    k = ctypes.WinDLL('kernel32'); k.GetTickCount64.restype = ctypes.c_uint64
    out = open(a.out, 'w', newline='') if a.out else sys.stdout
    w = csv.writer(out)
    w.writerow(COLS)
    clock = dict(t=0, tick=0, prev=0)

    def pos(frame):
        b = try_read(read, frame + REF_POS, 12)
        return ['%.2f' % v for v in struct.unpack('<3f', b)] if b else ['', '', '']

    def emit(kind, n, hp, dead, uncon, prev_hp):
        np_, pp = pos(n['frame']), pos(pframe)
        d = math.dist(map(float, np_), map(float, pp)) if np_[0] and pp[0] else ''
        w.writerow([kind, '%.6f' % (clock['t'] / 1e9), clock['tick'], '%.6f' % (clock['prev'] / 1e9), n['slot'], n['name'][-40:], hp,
                    n['max'], prev_hp, int(dead), int(uncon), '' if d == '' else '%.2f' % d, *np_, *pp])
        out.flush()
    clock.update(t=time.perf_counter_ns(), tick=k.GetTickCount64())
    for n in track:
        emit('init', n, n['hp'], n['dead'], n['uncon'], n['hp'])
    print('watching %d live NPCs (of %d), %.0f Hz' % (len(track), len(ents), a.hz), file=sys.stderr)
    period, t0 = 1.0 / a.hz, time.perf_counter()
    nxt, polls = t0, 0
    try:
        while track and (not a.seconds or time.perf_counter() - t0 < a.seconds):
            clock.update(prev=clock['t'], t=time.perf_counter_ns(), tick=k.GetTickCount64())
            poll_once(read, base, track, emit)
            polls += 1
            nxt += period
            if (wait := nxt - time.perf_counter()) > 0:
                time.sleep(wait)
            else:
                nxt = time.perf_counter()  # ponytail: fell behind, drop missed ticks rather than burst
    except KeyboardInterrupt:
        pass
    dt = time.perf_counter() - t0
    print('%d polls in %.1fs (%.1f Hz), %d NPCs still tracked' % (polls, dt, polls / dt, len(track)), file=sys.stderr)


if __name__ == '__main__':
    main()
