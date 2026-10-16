"""Turn a headset session's controller-trace.csv (30 Hz, tracking space, OpenXR axes) into a simulated-VR gameplay script (version 5)
so the sim loop replays the user's real head, hand and button motion.

    python tools/trace_to_script.py artifacts/runs/<run>/controller-trace.csv --auto --lead-ms 25000
    python tools/trace_to_script.py <trace.csv> artifacts/claude/replay.json --from-ms 120000 --to-ms 150000

One segment per trace row (poses change every row, so nothing merges). The poses are re-centred: the slice's first head position
becomes the simulator default (0, 1.65, 0) and its first head yaw becomes 0 (everything rotates about +y), so the replay starts where
the sim player stands and faces. Trace pauses longer than GAP_MS (loads, menus without frames) are compressed to GAP_MS."""
import argparse
import bisect
import csv
import json
import math
import statistics
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUTTONS = (('a', 1), ('b', 2), ('x', 4), ('y', 8), ('left_click', 16), ('right_click', 32), ('menu', 64))
AXES = ('move_x', 'move_y', 'turn_x', 'turn_y', 'left_trigger', 'right_trigger', 'left_grip', 'right_grip')
HEAD0 = (0.0, 1.65, 0.0)  # simulated default head position (src/simulated_xr.hpp)
GAP_MS = 100
MAX_END_MS = 108000  # the sim session is at most 120 s and runs 12 s past the script
SIM_FLAGS = ['--paired', '--native-eye-capture', '--vr-preview', '--head-tracking', '--controllers', '--symmetric-frustum',
             '--stable-camera', '--tracked-arms', '--physical-grab', '--native-left-aim-experiment', '--controller-aim',
             '--vr-turning', 'snap', '--simulated-vr', '--simulated-input']  # as tools/vr_selftest.py


def qmul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by, aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw, aw * bw - ax * bx - ay * by - az * bz)


def yaw_of(q):
    """Rotation about +y that carries -z onto the quaternion's forward (flattened): 90 degrees = looking toward -x."""
    x, y, z, w = q
    return math.atan2(2 * (x * z + w * y), 1 - 2 * (x * x + y * y))


def recentre(pose, head0, yaw0):
    """pose = x,y,z,qx,qy,qz,qw -> R_y(-yaw0)(p - head0) + HEAD0, q' = R_y(-yaw0) * q."""
    c, s = math.cos(yaw0), math.sin(yaw0)
    x, y, z = (pose[i] - head0[i] for i in range(3))
    q = qmul((0.0, math.sin(-yaw0 / 2), 0.0, math.cos(-yaw0 / 2)), pose[3:])
    n = math.sqrt(sum(v * v for v in q))
    return [x * c - z * s + HEAD0[0], y + HEAD0[1], x * s + z * c + HEAD0[2]] + [v / n for v in q]


def load_rows(path):
    rows = []
    with open(path, newline='') as f:
        for r in csv.DictReader(f):
            row = {k: float(v) for k, v in r.items() if k is not None and v not in (None, '')}
            row['tick'] = int(row['tick_ms'])
            rows.append(row)
    if not rows:
        raise SystemExit('Empty trace')
    return rows


def active(r):
    return max(r['left_grip'], r['right_grip'], r['left_trigger'], r['right_trigger']) > .5 or int(r['buttons']) & ~64


def auto_window(rows, window_ms):
    """Start (ms from the first row) of the window with the most grip/trigger/button rows that is all gameplay and has no long gap."""
    t = [r['tick'] for r in rows]
    act, game, gap = [0], [0], [0]
    for i, r in enumerate(rows):
        act.append(act[-1] + bool(active(r)))
        game.append(game[-1] + (r['logic'] == 2))
        gap.append(gap[-1] + (i > 0 and t[i] - t[i - 1] > 1000))
    best, j = None, 0
    for i in range(len(rows)):
        if t[-1] - t[i] < window_ms:
            break
        j = bisect.bisect_left(t, t[i] + window_ms, max(j, i))
        if game[j] - game[i] < .95 * (j - i) or gap[j] - gap[i + 1]:
            continue
        if best is None or act[j] - act[i] > best[0]:
            best = (act[j] - act[i], t[i] - t[0])
    if best is None:
        raise SystemExit('No gameplay window of %d ms in the trace' % window_ms)
    return best[1], best[1] + window_ms, best[0]


def build_segments(rows, lead_ms=0):
    """Script segments (see tools/simulated_gameplay_script.py) for consecutive trace rows, re-centred on the first."""
    r0 = rows[0]
    head0 = [r0['head_' + c] for c in 'xyz']
    yaw0 = yaw_of([r0['head_q' + c] for c in 'xyzw'])
    dts = [b['tick'] - a['tick'] for a, b in zip(rows, rows[1:]) if b['tick'] > a['tick']]
    last_dt = min(int(statistics.median(dts)) if dts else 33, GAP_MS)
    segments, t, held = [], lead_ms, {}
    if lead_ms:
        segments.append({'start_ms': 0, 'end_ms': lead_ms, 'head_pitch_degrees': 0, 'input': {'move_x': 0}})
    for i, r in enumerate(rows):
        dt = min(rows[i + 1]['tick'] - r['tick'], GAP_MS) if i + 1 < len(rows) else last_dt
        if dt <= 0:
            continue  # duplicate tick
        seg = {'start_ms': t, 'end_ms': t + dt}
        inp = {k: round(max(-1.0 if k[:4] in ('move', 'turn') else 0.0, min(1.0, r[k])), 3) for k in AXES}
        inp = {k: v for k, v in inp.items() if v}
        inp.update({name: True for name, bit in BUTTONS if int(r['buttons']) & bit})
        seg['input'] = inp or {'move_x': 0}
        for name, prefix, bit in (('head_pose', 'head_', None), ('left_hand_pose', 'left_', 1), ('right_hand_pose', 'right_', 2)):
            if bit is None or int(r['tracked_hands']) & bit:  # an untracked hand holds its last pose
                held[name] = recentre([r[prefix + c] for c in ('x', 'y', 'z', 'qx', 'qy', 'qz', 'qw')], head0, yaw0)
            if name in held:
                seg[name] = [round(v, 4) for v in held[name][:3]] + [round(v, 5) for v in held[name][3:]]
        segments.append(seg)
        t += dt
    return segments, math.degrees(yaw0)


def selfcheck():
    r2 = math.sqrt(.5)
    # Head at (1,2,3) turned 90 degrees left (forward -x); a hand 0.5 m ahead of the head and 0.2 m up, rolled with the head.
    head = [1, 2, 3, 0, r2, 0, r2]
    assert abs(math.degrees(yaw_of(head[3:])) - 90) < 1e-9
    hand = [1 - .5, 2.2, 3, 0, r2, 0, r2]
    h, p = recentre(head, head[:3], yaw_of(head[3:])), recentre(hand, head[:3], yaw_of(head[3:]))
    assert all(abs(a - b) < 1e-9 for a, b in zip(h, [0, 1.65, 0, 0, 0, 0, 1])), h
    assert all(abs(a - b) < 1e-9 for a, b in zip(p, [0, 1.85, -.5, 0, 0, 0, 1])), p  # ahead = -z
    # Yaw 0 and a pitched head keep orientation; right of the head stays +x.
    pitched = [0, 1, 0, math.sin(-.2), 0, 0, math.cos(-.2)]
    side = recentre([.3, 1, 0, 0, 0, 0, 1], pitched[:3], yaw_of(pitched[3:]))
    assert abs(yaw_of(pitched[3:])) < 1e-12 and abs(side[0] - .3) < 1e-12 and abs(side[1] - 1.65) < 1e-12
    # Looking toward +x (yaw -90): the head's right is world +z and ends up at +x.
    right = [0, 0, 0, 0, -r2, 0, r2]
    rh = recentre([0, 0, 1, 0, 0, 0, 1], right[:3], yaw_of(right[3:]))
    assert abs(rh[0] - 1) < 1e-9 and abs(rh[2]) < 1e-9, rh
    n = math.sqrt(sum(v * v for v in recentre(hand, head[:3], 1.234)[3:]))
    assert abs(n - 1) < 1e-12


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('trace')
    ap.add_argument('out', nargs='?', default=str(ROOT / 'artifacts' / 'claude' / 'trace-replay.json'), help='workspace JSON (default artifacts/claude/trace-replay.json)')
    ap.add_argument('--from-ms', type=int, default=0, help='slice start, ms after the first trace row')
    ap.add_argument('--to-ms', type=int, help='slice end (exclusive), ms after the first trace row')
    ap.add_argument('--auto', action='store_true', help='pick the busiest all-gameplay window (most grip/trigger/button rows)')
    ap.add_argument('--window-s', type=float, default=60, help='--auto window length (default 60 s)')
    ap.add_argument('--lead-ms', type=int, default=0, help='prepend this much neutral time (vr_selftest uses 25000 to reach gameplay)')
    ap.add_argument('--selfcheck', action='store_true')
    a = ap.parse_args()
    selfcheck()
    if a.selfcheck:
        print('PASS: recentring')
        return
    rows = load_rows(a.trace)
    t0 = rows[0]['tick']
    if a.auto:
        a.from_ms, a.to_ms, score = auto_window(rows, int(a.window_s * 1000))
        print(f'auto window {a.from_ms}..{a.to_ms} ms ({score} active rows)')
    chosen = [r for r in rows if r['tick'] - t0 >= a.from_ms and (a.to_ms is None or r['tick'] - t0 < a.to_ms)]
    if len(chosen) < 2:
        sys.exit('Slice has fewer than two trace rows')
    segments, yaw = build_segments(chosen, a.lead_ms)
    end_ms = segments[-1]['end_ms']
    if end_ms > MAX_END_MS or len(segments) > 4096:
        sys.exit(f'Script is {end_ms} ms / {len(segments)} segments; the limit is {MAX_END_MS} ms (incl. lead) and 4096 segments: shorten the slice')
    out = Path(a.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps({'segments': segments}, separators=(',', ':')), encoding='utf-8')
    seconds = max(10, math.ceil(end_ms / 1000) + 12)
    print(f'{len(segments)} segments, {end_ms} ms, first head yaw {yaw:.1f} deg removed, wrote {out} ({out.stat().st_size} bytes)')
    print('python tools/inject_probe.py --pid <PID> --dll-name <DLL> ' + ' '.join(SIM_FLAGS) +
          f' --simulated-gameplay-script {out} --seconds {seconds}')


if __name__ == '__main__':
    main()
