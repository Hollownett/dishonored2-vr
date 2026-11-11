#!/usr/bin/env python3
"""Perf-mode motion Phase 0 analysis (docs/superpowers/specs/2026-11-11-perf-mode-motion-design.md).

Reads the alt-motion-<burst>-<index>-* dumps the v1150 probe writes (knob build/alt-motion-probe) and, per burst:
  * static check: residual between the engine MV's previous UV and the reprojection of the pixel's surface through this eye's
    previous receipt snapshot (own-previous hypothesis). The matrix convention is searched, not assumed; the winner is printed.
  * H1 check: for pixels above --threshold, the residual when the previous camera is the OTHER eye one tick earlier (the
    previous dispatch of the burst, its current snapshot), and the fraction H1 explains better.
  * H2 aid: PNGs of both residuals and of the MV magnitude.

  python tools/alt_motion_probe.py <artifacts dir> [--out DIR] [--threshold 0.75]
  python tools/alt_motion_probe.py --selftest
"""
import argparse, glob, itertools, json, os, re, struct, sys, tempfile, zlib
import numpy as np

R16G16_FLOAT, R32_FLOAT = 34, 41


# ---------- files ----------
def read_raw(path):
    with open(path, 'rb') as f:
        w, h, fmt, row = struct.unpack('<4I', f.read(16))
        data = f.read()
    return w, h, fmt, row, data


def load_mv(path):
    w, h, fmt, row, data = read_raw(path)
    if fmt != R16G16_FLOAT: raise ValueError(f'{path}: format {fmt}, expected R16G16_FLOAT')
    return np.frombuffer(data, np.float16).reshape(h, row // 4, 2)[:, :w].astype(np.float64)


def load_depth(path):
    w, h, fmt, row, data = read_raw(path)
    if fmt != R32_FLOAT: raise ValueError(f'{path}: format {fmt}, expected R32_FLOAT')
    return np.frombuffer(data, np.float32).reshape(h, row // 4)[:, :w].astype(np.float64)


def write_png(path, img):
    """img: HxW (gray) or HxWx3 uint8."""
    img = np.ascontiguousarray(img, np.uint8)
    h, w = img.shape[:2]
    colour = 2 if img.ndim == 3 else 0
    raw = b''.join(b'\x00' + img[y].tobytes() for y in range(h))
    chunk = lambda t, d: struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, colour, 0, 0, 0))
                + chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))


def load_dumps(directory):
    bursts = {}
    for js in glob.glob(os.path.join(directory, 'alt-motion-*-*.json')):
        m = re.search(r'alt-motion-(\d+)-(\d+)\.json$', js)
        if not m: continue
        stem = js[:-5]
        if not (os.path.exists(stem + '-mv.rg16f') and os.path.exists(stem + '-depth.r32')): continue
        with open(js) as f: meta = json.load(f)
        d = dict(meta=meta, stem=stem, mv=load_mv(stem + '-mv.rg16f'), depth=load_depth(stem + '-depth.r32'))
        bursts.setdefault(int(m.group(1)), []).append(d)
    for b in bursts.values(): b.sort(key=lambda d: d['meta']['index'])
    return dict(sorted(bursts.items()))


# ---------- geometry ----------
# A convention: (transpose, y_up, mv_sign, jitter_sign, camera_relative, matrix source, mv scale to the render rectangle)
CONV_NAMES = ('transpose', 'ndc_y_up', 'mv_sign', 'jitter_step_sign', 'camera_relative', 'matrix', 'mv_uv_of')


def matrix(snap, transpose, source):
    m = lambda k: (np.array(snap[k], np.float64).reshape(4, 4).T if transpose else np.array(snap[k], np.float64).reshape(4, 4))
    return m('view_projection') if source == 'vp' else m('projection') @ m('view')


def jitter_step(cur, prev):  # taa_mv_dejitter.hpp TaaMvDejitter::step: (prev.x-cur.x, cur.y-prev.y) in UV
    return np.array([prev['jitter'][0] - cur['jitter'][0], cur['jitter'][1] - prev['jitter'][1]])


def residual_px(cur, prev, conv, uv, depth, mv, size):
    """|previous UV from the MV - reprojection through `prev`| in render pixels, one per sample."""
    transpose, y_up, mv_sign, js, rel, source, mv_k = conv
    mc, mp = matrix(cur, transpose, source), matrix(prev, transpose, source)
    try: inv = np.linalg.inv(mc)
    except np.linalg.LinAlgError: return np.full(len(uv), np.inf)
    ndc = np.stack([uv[:, 0] * 2 - 1, (1 - uv[:, 1] * 2) if y_up else (uv[:, 1] * 2 - 1), depth, np.ones(len(uv))], 1)
    world = ndc @ inv.T
    world = world[:, :3] / world[:, 3:4]
    if rel: world = world + np.array(cur['position']) - np.array(prev['position'])
    clip = np.concatenate([world, np.ones((len(uv), 1))], 1) @ mp.T
    with np.errstate(divide='ignore', invalid='ignore'):
        pn = clip[:, :2] / clip[:, 3:4]
    pred = np.stack([(pn[:, 0] + 1) * .5, (1 - pn[:, 1]) * .5 if y_up else (pn[:, 1] + 1) * .5], 1)
    prev_uv = uv - mv_sign * mv * mv_k + js * jitter_step(cur, prev)
    r = (prev_uv - pred) * np.array(size, np.float64)
    out = np.hypot(r[:, 0], r[:, 1])
    out[~np.isfinite(out)] = np.inf
    return out


def samples(d, every=1):
    depth, mv = d['depth'], d['mv']
    h, w = depth.shape
    ys, xs = np.nonzero(depth[::every, ::every] > 0)
    ys, xs = ys * every, xs * every
    uv = np.stack([(xs + .5) / w, (ys + .5) / h], 1)
    return ys, xs, uv, depth[ys, xs], mv[ys, xs], (w, h)


def mv_scales(d):
    t = d['meta'].get('motion_texture')
    h, w = d['depth'].shape
    ks = [1.0]
    if t and t[0] != w: ks.append(t[0] / w)  # the MV may be UV of the full target, not of the render rectangle
    return ks


def conventions(d):
    return list(itertools.product((False, True), (True, False), (1, -1), (0, 1, -1), (False, True), ('vp', 'pv'), mv_scales(d)))


def pick_convention(burst, max_samples=20000):
    scores = {}
    rng = np.random.default_rng(1)
    pools = []
    for d in burst:
        ys, xs, uv, dep, mv, size = samples(d)
        if len(uv) > max_samples:
            k = rng.choice(len(uv), max_samples, replace=False); uv, dep, mv = uv[k], dep[k], mv[k]
        pools.append((d, uv, dep, mv, size))
    for conv in conventions(burst[0]):
        meds = [np.median(residual_px(d['meta']['current'], d['meta']['previous'], conv, uv, dep, mv, size)) for d, uv, dep, mv, size in pools]
        scores[conv] = float(np.median(meds))
    ranked = sorted(scores.items(), key=lambda kv: kv[1])
    return ranked


def pct(a):
    if not len(a): return 'n/a'
    p = np.percentile(a, [50, 90, 99, 99.9])
    return 'p50 %.3f p90 %.3f p99 %.3f p99.9 %.3f' % tuple(p)


def gray(img, top):
    return (np.clip(img / top, 0, 1) * 255).astype(np.uint8)


def analyse(bursts, threshold=0.75, out=None, log=print):
    results = {}
    for b, burst in bursts.items():
        ranked = pick_convention(burst)
        conv, med = ranked[0]
        log(f'burst {b}: {len(burst)} dispatches, eyes {[d["meta"]["eye"] for d in burst]}, displays {[d["meta"]["display"] for d in burst]}')
        log('  convention: ' + ', '.join(f'{n}={v}' for n, v in zip(CONV_NAMES, conv)) + f'  -> median {med:.4f} px')
        for c, m in ranked[1:4]: log('    runner-up: ' + ', '.join(f'{n}={v}' for n, v in zip(CONV_NAMES, c)) + f' -> {m:.4f} px')
        if med > 0.25: log('  WARNING: no convention reaches a 0.25 px median; the matrix/receipt assumptions do not hold for this burst')
        per = []
        for i, d in enumerate(burst):
            meta = d['meta']
            ys, xs, uv, dep, mv, size = samples(d)
            own = residual_px(meta['current'], meta['previous'], conv, uv, dep, mv, size)
            movers = own > threshold
            line = f'  #{meta["index"]} eye {meta["eye"]} display {meta["display"]} px {len(own)}: own {pct(own)}  >{threshold}px {movers.mean() * 100:.3f}%'
            b1 = meta.get('b1_step_uv')
            if b1: line += '  b1 step %s vs own-prev step %s' % (np.round(b1, 6).tolist(), np.round(jitter_step(meta['current'], meta['previous']), 6).tolist())
            log(line)
            r = dict(index=meta['index'], eye=meta['eye'], own_median=float(np.median(own)) if len(own) else None, movers=float(movers.mean()) if len(own) else 0.0)
            h1 = None
            other = burst[i - 1]['meta'] if i else None
            if other and other['eye'] != meta['eye'] and other['display'] + 1 == meta['display']:
                h1 = residual_px(meta['current'], other['current'], conv, uv, dep, mv, size)
                if movers.any():
                    better = (h1[movers] < own[movers]) & (h1[movers] <= threshold)
                    log(f'      H1 on movers: {pct(h1[movers])}  better explained by H1: {better.mean() * 100:.1f}%  (H1 on all: {pct(h1)})')
                    r['h1_better'] = float(better.mean())
                    if b1: log('      b1 step vs other-eye step %s' % np.round(jitter_step(meta['current'], other['current']), 6).tolist())
            elif i:
                log('      H1: previous dispatch is not the other eye one tick earlier; skipped')
            per.append(r)
            if out:
                os.makedirs(out, exist_ok=True)
                h, w = d['depth'].shape
                stem = os.path.join(out, os.path.basename(d['stem']))
                img = np.zeros((h, w)); img[ys, xs] = own
                write_png(stem + '-own.png', gray(img, 4 * threshold))
                if h1 is not None:
                    img = np.zeros((h, w)); img[ys, xs] = h1
                    write_png(stem + '-h1.png', gray(img, 4 * threshold))
                mvpx = np.hypot(d['mv'][..., 0] * w, d['mv'][..., 1] * h)
                write_png(stem + '-mv.png', gray(mvpx, max(np.percentile(mvpx, 99), 1e-3)))
        results[b] = dict(convention=conv, median=med, dispatches=per)
    return results


# ---------- self-test ----------
def _camera(pos, yaw, jitter, near=.1, f=1.0, W=320, H=240):
    """Row-major, column vectors; view space x right, y up, z ahead; reverse-Z infinite (depth = near/z); engine jitter sign."""
    c, s = np.cos(yaw), np.sin(yaw)
    R = np.array([[c, 0, -s], [0, 1, 0], [s, 0, c]])  # world -> view rotation
    V = np.eye(4); V[:3, :3] = R; V[:3, 3] = -R @ np.asarray(pos, float)
    a = W / H
    P = np.array([[f / a, 0, -2 * jitter[0], 0], [0, f, -2 * jitter[1], 0], [0, 0, 0, near], [0, 0, 1, 0]])
    return dict(projection=P.ravel().tolist(), view=V.ravel().tolist(), view_projection=(P @ V).ravel().tolist(),
                position=list(map(float, pos)), jitter=list(map(float, jitter)), _V=V, _P=P)


def _render(cam, box_c, W=320, H=240):
    """Depth and hit points of a wall at z=12 and a 0.6 m box at box_c, seen through cam."""
    P, V = cam['_P'], cam['_V']
    M = P @ V
    inv = np.linalg.inv(M)
    ys, xs = np.mgrid[0:H, 0:W]
    u, v = (xs + .5) / W, (ys + .5) / H
    def unproj(d):
        c = np.stack([u * 2 - 1, 1 - v * 2, np.full_like(u, d), np.ones_like(u)], -1) @ inv.T
        return c[..., :3] / c[..., 3:4]
    o = np.asarray(cam['position'], float)
    far = unproj(1e-3)
    dirs = far - o
    t_wall = (12 - o[2]) / dirs[..., 2]
    t = np.where(t_wall > 0, t_wall, np.inf)
    lo, hi = np.asarray(box_c) - .3, np.asarray(box_c) + .3
    with np.errstate(divide='ignore', invalid='ignore'):
        t1, t2 = (lo - o) / dirs, (hi - o) / dirs
    tn, tf = np.minimum(t1, t2).max(-1), np.maximum(t1, t2).min(-1)
    box = (tn <= tf) & (tn > 0) & (tn < t)
    t = np.where(box, tn, t)
    pts = o + dirs * t[..., None]
    clip = np.concatenate([pts, np.ones(pts.shape[:-1] + (1,))], -1) @ M.T
    return clip[..., 2] / clip[..., 3], pts, box


def _project(cam, pts):
    M = cam['_P'] @ cam['_V']
    clip = np.concatenate([pts, np.ones(pts.shape[:-1] + (1,))], -1) @ M.T
    n = clip[..., :2] / clip[..., 3:4]
    return np.stack([(n[..., 0] + 1) * .5, (1 - n[..., 1]) * .5], -1)


def _synth(directory, scenario, W=320, H=240):
    """8 consecutive alternate-eye ticks. scenario: 'own_mover' (box rises, MVs span this eye's last render),
    'h1_still' (box still, entity MVs from the other eye one tick ago), 'own_still'."""
    pattern = [(.125, -.25), (-.375, .125), (.375, -.125), (-.125, .375)]
    cams = []
    for t in range(10):
        eye = t % 2
        head = np.array([.3 + .01 * t, 1.6, .5 + .02 * t])
        pos = head + np.array([(eye - .5) * .064, 0, 0])
        cams.append(_camera(pos, .02 * t, np.array(pattern[t % 4]) / np.array([W, H]), W=W, H=H))
    vel = np.array([0, .05, 0]) if scenario == 'own_mover' else np.zeros(3)
    for i, t in enumerate(range(2, 10)):
        cur, own_prev, other = cams[t], cams[t - 2], cams[t - 1]
        depth, pts, box = _render(cur, np.array([.4, 1.5, 2.0]) + vel * t, W, H)
        prev_uv = _project(own_prev, pts - vel * 2 * box[..., None])
        if scenario == 'h1_still':
            prev_uv = np.where(box[..., None], _project(other, pts), prev_uv)
        ys, xs = np.mgrid[0:H, 0:W]
        uv = np.stack([(xs + .5) / W, (ys + .5) / H], -1)
        mv = (uv - prev_uv).astype(np.float16)
        stem = os.path.join(directory, f'alt-motion-1-{i}')
        strip = lambda c: {k: v for k, v in c.items() if not k.startswith('_')}
        meta = dict(burst=1, index=i, eye=t % 2, display=100 + t, current=strip(cur), previous=strip(own_prev),
                    render=[W, H], motion_texture=[W, H, R16G16_FLOAT])
        with open(stem + '.json', 'w') as f: json.dump(meta, f)
        for suffix, arr, fmt in (('-mv.rg16f', mv, R16G16_FLOAT), ('-depth.r32', depth.astype(np.float32), R32_FLOAT)):
            with open(stem + suffix, 'wb') as f:
                f.write(struct.pack('<4I', W, H, fmt, W * 4)); f.write(arr.tobytes())
    return box.mean()


def selftest():
    expected = (False, True, 1, 0, False, 'vp', 1.0)
    for scenario in ('own_still', 'own_mover', 'h1_still'):
        with tempfile.TemporaryDirectory() as tmp:
            box_frac = _synth(tmp, scenario)
            lines = []
            res = analyse(load_dumps(tmp), out=tmp, log=lines.append)[1]
            assert res['convention'][:4] == expected[:4] and res['convention'][5] == 'vp', (scenario, res['convention'])
            assert res['median'] < .05, (scenario, res['median'])
            movers = np.mean([r['movers'] for r in res['dispatches']])
            h1 = [r['h1_better'] for r in res['dispatches'] if 'h1_better' in r]
            if scenario == 'own_still':
                assert movers < .002, (scenario, movers)
            elif scenario == 'own_mover':
                assert movers > .5 * box_frac, (scenario, movers, box_frac)
                assert h1 and max(h1) < .2, (scenario, h1)
            else:
                assert movers > .5 * box_frac, (scenario, movers, box_frac)
                assert h1 and min(h1) > .9, (scenario, h1)
            assert glob.glob(os.path.join(tmp, '*-own.png'))
            print(f'selftest {scenario}: ok (median {res["median"]:.4f} px, movers {movers * 100:.2f}% of samples, box {box_frac * 100:.2f}% of image'
                  + (f', H1 better on movers {np.mean(h1) * 100:.1f}%' if h1 else '') + ')')
    print('selftest: all passed')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('directory', nargs='?')
    ap.add_argument('--out', help='PNG directory (default: <directory>/alt-motion-analysis)')
    ap.add_argument('--threshold', type=float, default=.75, help='mover threshold in render px (design: mover_px 0.75)')
    ap.add_argument('--selftest', action='store_true')
    a = ap.parse_args()
    if a.selftest: return selftest()
    if not a.directory: ap.error('directory required')
    bursts = load_dumps(a.directory)
    if not bursts: sys.exit('no alt-motion-*-*.json dumps with -mv.rg16f and -depth.r32 in ' + a.directory)
    analyse(bursts, a.threshold, a.out or os.path.join(a.directory, 'alt-motion-analysis'))


if __name__ == '__main__':
    main()
