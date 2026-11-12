"""v1212: read the rig-dump-*.bin files RigMask writes for wild (and one clean) render(s) and say, per joint origin, whether the
previous position the draw computed (previous rows x slot palette) is what the eye's previous render actually drew (its current rows x
its current palette), and how far the rig moved between those two renders.

usage: python tools/rig_wild_dump.py <dir or files...> [--size W H]     (default render size 1922 2070)
       python tools/rig_wild_dump.py --selftest
"""
import glob, os, struct, sys

ABSENT = 0xFF


def read(path):
    with open(path, 'rb') as f:
        data = f.read()
    assert data[:8] == b'D2RIGD1\0', path
    n, = struct.unpack_from('<I', data, 8)
    at, recs = 12, []
    for _ in range(n):
        display, frame, tag, model, eye, layout = struct.unpack_from('<QIIIIQ', data, at); at += 32
        mask = struct.unpack_from('<3I', data, at); at += 12
        nb = struct.unpack_from('<3I', data, at); at += 12
        blobs = []
        for b in nb:
            blobs.append(struct.unpack_from('<%df' % (b // 4), data, at)); at += b
        recs.append(dict(display=display, frame=frame, tag=tag, model=model, eye=eye, layout=layout, mask=mask,
                         pal=blobs[0], cur=blobs[1], cb=blobs[2]))
    return recs


def rows(rec, prev):
    """the 4 MVP rows (x, y, z, w) of the record's b2 by its layout; a missing z row is None"""
    out = []
    for k in range(4):
        i = (rec['layout'] >> (8 * (k + (4 if prev else 0)))) & 0xFF
        out.append(None if i == ABSENT else rec['cb'][4 * i:4 * i + 4])
    return out


def skin(pal, j, p):
    m = pal[12 * j:12 * j + 12]
    return [sum(m[4 * r + c] * p[c] for c in range(4)) for r in range(3)] + [1.0]


def clip(r, q):
    return [None if row is None else sum(a * b for a, b in zip(row, q)) for row in r]


def screen(c, w, h):
    if not c[3] or c[3] <= 1e-6:
        return None  # behind the camera
    return (c[0] / c[3] * 0.5 * w, -c[1] / c[3] * 0.5 * h)


def dist(a, b):
    if a is None or b is None:
        return float('inf')
    return ((a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2) ** 0.5


def med(v):
    v = sorted(v)
    return v[len(v) // 2] if v else float('nan')


def mat34(pal, j):
    m = pal[12 * j:12 * j + 12]
    return [list(m[0:4]), list(m[4:8]), list(m[8:12])]


def rigid_delta(a, b):
    """b * inverse(a) for 3x4 affine rows: (translation length m, rotation angle deg)"""
    import math
    ra = [r[:3] for r in a]; ta = [r[3] for r in a]
    # inverse of a rotation(+scale) part by the adjugate
    det = (ra[0][0] * (ra[1][1] * ra[2][2] - ra[1][2] * ra[2][1]) - ra[0][1] * (ra[1][0] * ra[2][2] - ra[1][2] * ra[2][0])
           + ra[0][2] * (ra[1][0] * ra[2][1] - ra[1][1] * ra[2][0]))
    if abs(det) < 1e-12:
        return float('nan'), float('nan')
    inv = [[(ra[(c + 1) % 3][(r + 1) % 3] * ra[(c + 2) % 3][(r + 2) % 3] - ra[(c + 1) % 3][(r + 2) % 3] * ra[(c + 2) % 3][(r + 1) % 3]) / det
            for c in range(3)] for r in range(3)]
    rb = [r[:3] for r in b]; tb = [r[3] for r in b]
    rot = [[sum(rb[i][k] * inv[k][j] for k in range(3)) for j in range(3)] for i in range(3)]
    t = [tb[i] - sum(rot[i][k] * ta[k] for k in range(3)) for i in range(3)]
    cos = max(-1.0, min(1.0, (rot[0][0] + rot[1][1] + rot[2][2] - 1) / 2))
    return (sum(x * x for x in t) ** 0.5, math.degrees(math.acos(cos)))


def finite(v):
    return [x for x in v if x == x and x != float('inf')]


def stats(v):
    f = finite(v)
    return ('median %.1f max %.1f over %d' % (med(f), max(f), len(f))) if f else 'none in front of the camera'


def analyse(recs, w, h):
    r0 = recs[0]
    r2 = next((r for r in recs[1:] if r['display'] + 2 == r0['display']), None)
    joints = 69 if r0['model'] < 2 else 20
    joints = min(joints, len(r0['pal']) // 12, len(r0['cur']) // 12)
    origin = (0.0, 0.0, 0.0, 1.0)
    mv, pair, moved = [], [], []
    behind = {'now': 0, 'prev': 0, 'drawn_prev': 0}
    rc, rp = rows(r0, False), rows(r0, True)
    for j in range(joints):
        cur = screen(clip(rc, skin(r0['cur'], j, origin)), w, h)
        prev = screen(clip(rp, skin(r0['pal'], j, origin)), w, h)
        behind['now'] += cur is None; behind['prev'] += prev is None
        mv.append(dist(cur, prev))
        if r2:
            drawn = screen(clip(rows(r2, False), skin(r2['cur'], j, origin)), w, h)  # what the previous render of this eye drew
            behind['drawn_prev'] += drawn is None
            pair.append(dist(prev, drawn))
            moved.append(dist(cur, drawn))
    print('display %d frame %d eye %d model %d slot tag %d mask %s' % (r0['display'], r0['frame'], r0['eye'], r0['model'], r0['tag'], list(r0['mask'])))
    print('  joint origins behind the camera (w <= 0, skipped below): now %d, previous %d, display-2 drawn %d of %d' % (behind['now'], behind['prev'], behind['drawn_prev'], joints))
    print('  |mv| px: %s' % stats(mv))
    d = [rigid_delta(mat34(r0['pal'], j), mat34(r0['cur'], j)) for j in range(joints)]
    dt = finite([x[0] for x in d]); da = finite([x[1] for x in d])
    if dt:
        print('  palette slot -> current per joint as a rigid move (current * inverse(slot)): translation median %.3f max %.3f m, rotation median %.1f max %.1f deg'
              % (med(dt), max(dt), med(da), max(da)))
    if not r2:
        print('  no display-2 record in the file: pairing not checked')
        return
    rows_eq = rows(r0, True)[0:2] + rows(r0, True)[3:] == rows(r2, False)[0:2] + rows(r2, False)[3:]
    pal_eq = r0['pal'][:12 * joints] == r2['cur'][:12 * joints]
    print('  previous rows == display-2 current rows: %s; slot palette == display-2 current palette: %s' % (rows_eq, pal_eq))
    print('  previous position vs what display-2 drew (0 = the pair is exactly that draw): %s px' % stats(pair))
    print('  display-2 drawn -> now drawn (the rig really moved): %s px' % stats(moved))


def selftest():
    import tempfile
    ident = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0]
    pal = ident * 20
    cb = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0.1, 0, 0, 1, 2] * 2  # rows x, y, z, w twice (current at float4 0..3, previous 4..7)
    layout = sum(k << (8 * k) for k in range(8))

    def rec(display):
        blob = struct.pack('<%df' % len(pal), *pal)
        cbb = struct.pack('<%df' % len(cb), *cb)
        return (struct.pack('<QIIIIQ', display, display, display - 2, 2, display & 1, layout) + struct.pack('<3I', 1, 1, 0)
                + struct.pack('<3I', len(blob), len(blob), len(cbb)) + blob + blob + cbb)
    path = os.path.join(tempfile.mkdtemp(), 'rig-dump-selftest.bin')
    with open(path, 'wb') as f:
        f.write(b'D2RIGD1\0' + struct.pack('<I', 2) + rec(10) + rec(8))
    recs = read(path)
    assert len(recs) == 2 and recs[1]['display'] == 8 and rows(recs[0], True)[3] == (0, 0, 1, 2)
    analyse(recs, 100, 100)
    print('selftest ok')


def main(argv):
    if '--selftest' in argv:
        return selftest()
    w, h = 1922, 2070
    if '--size' in argv:
        i = argv.index('--size'); w, h = int(argv[i + 1]), int(argv[i + 2]); argv = argv[:i] + argv[i + 3:]
    files = []
    for a in argv:
        files += sorted(glob.glob(os.path.join(a, 'rig-dump-*.bin'))) if os.path.isdir(a) else [a]
    for p in files:
        print(os.path.basename(p))
        analyse(read(p), w, h)


if __name__ == '__main__':
    main(sys.argv[1:])
