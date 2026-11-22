"""Read-only walk of the native hand-state chain (src/native_hand_occupancy.hpp native_hand_state and
src/native_carry_snapshot.hpp native_carry_snapshot) in the running game, printing every link and the first one that does
not hold. Death of the Outsider offsets are the ones the DOTO build uses today; alternatives are probed where a shift is
suspected (state pool +0x90 vs +0x98). ReadProcessMemory only.

    python tools/doto/hand_state_probe.py [--d2]
"""
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import live  # noqa: E402

DOTO = dict(game=0x3a72280, player=0x327770, components=0x270, body_vt=(0x1cdf2c8, 0x1cdf3a0), machine_vt=0x1cf9428,
            state_vts=(0x1d00228, 0x1cfcef8), inventory_vt=0x1caadd8, graph_vt=0x1cd1848, node_vt=0x1cd1838, descriptor=0x1fd8)
D2 = dict(game=0x39a5290, player=0x3130b0, components=0x268, body_vt=(0x1c5a8d8, 0x1c5a9b0), machine_vt=0x1c73d58,
          state_vts=(0x1c7a998, 0x1c77748), inventory_vt=0x1b9a878, graph_vt=0x1c4d448, node_vt=0x1c4d438, descriptor=0x1fe8)


def lst(p, at):
    raw = p.read(at, 12)
    if not raw:
        return None, 0
    ptr, packed = struct.unpack_from('<QI', raw)
    return ptr, packed & 0xffffff


def cstr(p, at, n=64):
    raw = p.read(at, n) if at else None
    if not raw:
        return None
    s = raw.split(b'\0')[0]
    return s.decode('ascii', 'replace')


def main():
    c = D2 if '--d2' in sys.argv else DOTO
    p = live.Proc()
    base = p.base
    vt = lambda o: (p.u64(o) - base) if o and p.u64(o) else None  # noqa: E731
    ok = lambda b, what: print(('  ok   ' if b else '  FAIL ') + what) or b  # noqa: E731
    g = p.u64(base + c['game'])
    pl = p.u64(g + c['player']) if g else None
    print(f'game {g and hex(g)} player {pl and hex(pl)} {p.rtti(pl) if pl else None}')
    if not pl:
        return
    cptr, cnt = lst(p, pl + c['components'])
    comps = [p.u64(cptr + 8 * i) for i in range(min(cnt, 4096))] if cptr else []
    body = [e for e in comps if e and vt(e) in c['body_vt'] and p.u64(e + 0x10) == pl]
    mach = [e for e in comps if e and vt(e) == c['machine_vt'] and p.u64(e + 0x10) == pl]
    ok(len(body) == 1, f'body component: {[hex(b) for b in body]} {p.rtti(body[0]) if body else ""}')
    ok(len(mach) == 1, f'state machine: {[hex(m) for m in mach]} {p.rtti(mach[0]) if mach else ""}')
    if mach:
        m = mach[0]
        for off in (0x88, 0x90, 0x98, 0xa0):
            pool = p.u64(m + off)
            names = {}
            if pool and 0x10000 < pool < 0x7fff00000000:
                for i in (45, 49, 50):
                    s = p.u64(pool + 0x10 + 8 * i)
                    names[i] = (p.rtti(s) if s else None, (vt(s) in c['state_vts']) if s else False)
            print(f'  machine+{off:#x} pool {pool and hex(pool)} states 45/49/50: {names}')
        # arkCpntPlayerStateMachine fields (D2 -> DOTO per the constructor diff 0x801f80 -> 0x869040, offsets.json "structs").
        if c is DOTO:
            fields = {'requests list': 0x260, 'controller': 0x350, 'body': 0x358, 'interaction': 0x370, 'inventory': 0x380,
                      'attributes': 0x388, 'sensor container': 0x390}
            inv_now = p.u64(body[0] + 0x60) if body else None
            for name, off in fields.items():
                q = p.u64(m + off)
                extra = ''
                if name == 'body':
                    extra = f' == body component: {q == (body[0] if body else None)}'
                elif name == 'inventory':
                    extra = f' == body+0x60 inventory: {q == inv_now}'
                elif name == 'sensor container' and q:
                    extra = f' +0x10 == player: {p.u64(q + 0x10) == pl}'
                elif name == 'requests list':
                    ptr, n = lst(p, m + off)
                    extra = f' (list ptr {ptr and hex(ptr)} count {n})'
                print(f'  machine+{off:#x} {name}: {q and hex(q)} {p.rtti(q) if q and 0x10000 < q < 0x7fff00000000 else None}{extra}')
        # The whole player state table (id -> class), for the ids src/ uses (18, 39, 44, 45, 49, 50, 53, 54, 64, ...).
        pool = p.u64(m + (0x98 if c is DOTO else 0x90))
        if pool and 0x10000 < pool < 0x7fff00000000 and '--states' in sys.argv:
            for i in range(170):
                s = p.u64(pool + 0x10 + 8 * i)
                if s and 0x10000 < s < 0x7fff00000000:
                    sid = p.u32(s + 8)
                    print(f'    state {i:3d} id+8={sid} active={p.read(s + 0x20, 1).hex() if p.read(s + 0x20, 1) else None} {p.rtti(s)}')
        # Which ids hold the carry-family states (vtables state_vts) in each candidate pool.
        for off in (0x90, 0x98):
            pool = p.u64(m + off)
            if pool and 0x10000 < pool < 0x7fff00000000:
                hits = [i for i in range(160) if (lambda s: s and vt(s) in c['state_vts'])(p.u64(pool + 0x10 + 8 * i))]
                print(f'  machine+{off:#x}: ids whose state has a carry-table vtable: {hits}')
    if not body:
        return
    b = body[0]
    inv = p.u64(b + 0x60)
    ok(bool(inv) and vt(inv) == c['inventory_vt'], f'body+0x60 inventory {inv and hex(inv)} {p.rtti(inv) if inv else None} (vtable {vt(inv) and hex(vt(inv))}, want {c["inventory_vt"]:#x})')
    if inv:
        ok(p.u64(inv + 0x10) == pl, 'inventory+0x10 == player')
        s0, s1 = p.u64(inv + 0x68), p.u64(inv + 0x70)
        print(f'  inventory+0x68 slots {s0 and hex(s0)} {p.rtti(s0) if s0 else None} | {s1 and hex(s1)} {p.rtti(s1) if s1 else None}')
    for side in (0, 1):
        print(f'side {side}')
        part = b + (0x680 if side else 0x370)
        anim = p.u64(b + (0x688 if side else 0x378))
        model = p.u64(anim + 0x38) if anim else None
        desc = p.u64(model + c['descriptor']) if model else None
        graph = p.u64(desc + 0x138) if desc else None
        ok(bool(graph) and vt(graph) == c['graph_vt'], f'arm model {model and hex(model)} descriptor {desc and hex(desc)} +0x138 graph {graph and hex(graph)} {p.rtti(graph) if graph else None}')
        if desc and not (graph and vt(graph) == c['graph_vt']):
            for off in range(0x100, 0x180, 8):
                q = p.u64(desc + off)
                r = p.rtti(q) if q and 0x10000 < q < 0x7fff00000000 else None
                if r and ('raph' in r or 'Anim' in r):
                    print(f'    descriptor+{off:#x} -> {q:#x} {r}')
        node = p.u64(part + 0x18)
        want = graph + (0x1d0 if side else 0x190) if graph else None
        ok(node == want and vt(node) == c['node_vt'], f'part+0x18 node {node and hex(node)} {p.rtti(node) if node else None} want graph+{0x1d0 if side else 0x190:#x}')
        if graph and node and node != want:
            print(f'    node - graph = {node - graph:#x}')
        idx = p.read(part + 0x21, 1)
        idx = struct.unpack('<b', idx)[0] if idx else None
        nptr, ncnt = lst(p, graph + (0xb0 if side else 0xa0)) if graph else (None, 0)
        txt = cstr(p, p.u64(nptr + 8 * idx)) if nptr and idx is not None and 0 <= idx < ncnt else None
        ok(txt is not None, f'part+0x21 index {idx}, names graph+{0xb0 if side else 0xa0:#x} count {ncnt}, name {txt!r}')
        if nptr and ncnt:
            print('    names:', [cstr(p, p.u64(nptr + 8 * i), 32) for i in range(min(ncnt, 24))])


if __name__ == '__main__':
    main()
