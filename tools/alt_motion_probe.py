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

R16G16_FLOAT, R32_FLOAT, R16G16B16A16_FLOAT = 34, 41, 10
OTHER_STILL_PX = 3.0  # src/eye_motion_pass.hpp eye_motion_other_still_px (v1158; v1154 0.25)
ARM_REACH = 0.5  # metres; src/eye_motion_pass.hpp eye_motion_arm_reach: nearer pixels carry w = 0 (first-person arms)


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


def load_motion(path):  # v1151 motion pass output: RGBA16F (xyz world metres, w object motion px)
    w, h, fmt, row, data = read_raw(path)
    if fmt != R16G16B16A16_FLOAT: raise ValueError(f'{path}: format {fmt}, expected R16G16B16A16_FLOAT')
    return np.frombuffer(data, np.float16).reshape(h, row // 8, 4)[:, :w].astype(np.float64)


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
        if os.path.exists(stem + '-motion.rgba16f'): d['motion'] = load_motion(stem + '-motion.rgba16f')
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
    # camera_relative stays False (Phase 0: the receipts are world space; on a still head True tied and broke the H1 test)
    return list(itertools.product((False, True), (True, False), (1, -1), (0, 1, -1), (False,), ('vp', 'pv'), mv_scales(d)))


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
        log(f'burst {b}: {len(burst)} dispatches, eyes {[d["meta"]["eye"] for d in burst]}, presents {[d["meta"]["present"] for d in burst]}')
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
            if other and other['eye'] != meta['eye'] and other['present'] + 1 == meta['present']:
                h1 = residual_px(meta['current'], other['current'], conv, uv, dep, mv, size)
                if movers.any():
                    better = (h1[movers] < own[movers]) & (h1[movers] <= threshold)
                    log(f'      H1 on movers: {pct(h1[movers])}  better explained by H1: {better.mean() * 100:.1f}%  (H1 on all: {pct(h1)})')
                    r['h1_better'] = float(better.mean())
                    if b1: log('      b1 step vs other-eye step %s' % np.round(jitter_step(meta['current'], other['current']), 6).tolist())
            elif i:
                log('      H1: previous dispatch is not the other eye one tick earlier; skipped')
            # v1151: pixels the motion pass covers (not sky, not within arm reach) and, when dumped, the pass's own w against this residual
            proj = meta['current']['projection']
            ahead = proj[11] / (dep + proj[10])
            covered = ahead >= ARM_REACH
            r['movers_covered'] = float((own[covered] > threshold).mean()) if covered.any() else 0.0
            r['movers_covered_3px'] = float((own[covered] > 3.0).mean()) if covered.any() else 0.0  # v1157: the warp's mover_px
            msg = (f'      covered (beyond {ARM_REACH} m) {covered.mean() * 100:.1f}%: movers >{threshold} px {r["movers_covered"] * 100:.3f}%,'
                   f' >3.0 px {r["movers_covered_3px"] * 100:.3f}%')
            if 'motion' in d:
                m = d['motion'][ys, xs]
                pw = m[:, 3]
                both = covered & np.isfinite(own)
                diff = np.abs(pw[both] - own[both])
                r['pass_w_diff_p99'] = float(np.percentile(diff, 99)) if len(diff) else None
                r['pass_movers'] = float((pw[covered] >= threshold).mean()) if covered.any() else 0.0
                r['pass_movers_3px'] = float((pw[covered] >= 3.0).mean()) if covered.any() else 0.0
                r['pass_netted'] = float((covered & (own > threshold) & (pw == 0)).mean())  # v1154: own movers the pass wrote as still
                r['pass_w_zero_outside'] = bool(np.all(pw[~covered] == 0))
                agree = ((pw[both] >= threshold) == (own[both] > threshold)).mean() if len(diff) else 1.0
                msg += (f'; pass w vs script: |diff| {pct(diff)}, mover agreement {agree * 100:.2f}%, pass movers {r["pass_movers"] * 100:.3f}% (>=3 px {r["pass_movers_3px"] * 100:.3f}%),'
                        f' w=0 outside cover: {r["pass_w_zero_outside"]}, own movers written still {r["pass_netted"] * 100:.3f}%')
            log(msg)
            # v1166: the rig pixels (the dispatch's -rig.r8 mask, 255 = rig): their residual against this eye's own previous render
            rig_path = d['stem'] + '-rig.r8'
            if os.path.exists(rig_path):
                rw, rh, _, rrow, rdata = read_raw(rig_path)
                mask = np.frombuffer(rdata, np.uint8)[:rh * rrow].reshape(rh, rrow)[:, :rw]
                inside = (ys < rh) & (xs < rw)
                rig = np.zeros(len(own), bool); rig[inside] = mask[ys[inside], xs[inside]] == 255
                r['rig_px'] = int(rig.sum())
                if rig.any():
                    r['rig_own_p50'] = float(np.percentile(own[rig], 50)); r['rig_own_p90'] = float(np.percentile(own[rig], 90))
                    log(f'      rig pixels {rig.sum()}: own {pct(own[rig])}')
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
        meta = dict(burst=1, index=i, eye=t % 2, display=100 + t, present=100 + t, current=strip(cur), previous=strip(own_prev),
                    render=[W, H], motion_texture=[W, H, R16G16_FLOAT], mv_jittered=True)
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


# ---------- v1151 motion pass check (tests/eye_motion_pass_check.cpp on WARP) ----------
MEASURED = (False, True, 1, -1, False, 'vp', 1.0)  # Phase 0 winner (dumps before v1178: the MV had no jitter step)
MEASURED_JITTERED = (False, True, 1, 0, False, 'vp', 1.0)  # v1178+: +0x7858 = +0x7850 at upload, the MV carries the jitter step


def measured(meta):  # the convention src/eye_motion_pass.hpp implements for this dump
    return MEASURED_JITTERED if meta.get('mv_jittered') else MEASURED


def reference_motion(cur, prev, uv, depth, mv, size, conv=MEASURED):
    """numpy reference of the pass: (xyz world displacement, w px) per sample, arms/sky not masked."""
    mc, mp = matrix(cur, False, 'vp'), matrix(prev, False, 'vp')
    ndc = np.stack([uv[:, 0] * 2 - 1, 1 - uv[:, 1] * 2, depth, np.ones(len(uv))], 1)
    now = ndc @ np.linalg.inv(mc).T; now = now[:, :3] / now[:, 3:4]
    clip = np.concatenate([now, np.ones((len(uv), 1))], 1) @ mp.T
    puv = uv - mv + conv[3] * jitter_step(cur, prev)
    pn = np.stack([puv[:, 0] * 2 - 1, 1 - puv[:, 1] * 2, clip[:, 2] / clip[:, 3], np.ones(len(uv))], 1)
    then = pn @ np.linalg.inv(mp).T; then = then[:, :3] / then[:, 3:4]
    return now - then, residual_px(cur, prev, conv, uv, depth, mv, size)


def check_pass(stem, exe, work):
    """Runs the pass on WARP on one dump (stem without suffix) and asserts it matches the reference."""
    import subprocess
    with open(stem + '.json') as f: meta = json.load(f)
    os.makedirs(work, exist_ok=True)
    cams = os.path.join(work, 'cameras.txt')
    # v1154: the other eye's render one Present earlier (the previous dispatch of the burst), when the dump has it
    m = re.search(r'alt-motion-(\d+)-(\d+)$', stem)
    other = None
    if m and int(m.group(2)) > 0:
        o = f'{stem[:m.start()]}alt-motion-{m.group(1)}-{int(m.group(2)) - 1}.json'
        if os.path.exists(o):
            with open(o) as f: om = json.load(f)
            if om['eye'] != meta['eye'] and om['present'] + 1 == meta['present']: other = om['current']
    with open(cams, 'w') as f:
        for snap in (meta['current'], meta['previous']) + ((other,) if other else ()):
            f.write(' '.join(repr(float(v)) for v in snap['projection'] + snap['view_projection'] + snap['position'] + snap['jitter']) + '\n')
    out = os.path.join(work, 'motion.rgba16f')
    conv = measured(meta)
    subprocess.run([exe, cams, stem + '-depth.r32', stem + '-mv.rg16f', out] + ([] if meta.get('mv_jittered') else ['legacy']), check=True)
    d = dict(meta=meta, depth=load_depth(stem + '-depth.r32'), mv=load_mv(stem + '-mv.rg16f'))
    got = load_motion(out)
    ys, xs, uv, dep, mv, size = samples(d)
    proj = meta['current']['projection']
    ahead = proj[11] / (dep + proj[10])
    covered = ahead >= ARM_REACH + 1e-3  # float32 vs float64 at the boundary itself: a texel either way
    outside = ahead < ARM_REACH - 1e-3
    xyz, w = reference_motion(meta['current'], meta['previous'], uv, dep, mv, size, conv)
    g = got[ys, xs]
    if other:  # the net: movers the other eye's render explains come out 0; boundary cases (within 0.01 px) are not compared
        h1 = residual_px(meta['current'], other, conv, uv, dep, mv, size)
        still = covered & (w >= 0.75) & (h1 < OTHER_STILL_PX)
        edge = (np.abs(w - 0.75) < 0.01) | (np.abs(h1 - OTHER_STILL_PX) < 0.01)
        assert np.all(g[still & ~edge] == 0), 'other-eye still pixels must be 0'
        covered = covered & ~still & ~edge
        print(f'check-pass: other-eye net on, {still.mean() * 100:.3f}% of samples still by it (0 in the pass)')
    else: print('check-pass: no other-eye snapshot one Present earlier: net off')
    zero = got[d['depth'] <= 0]
    dw = np.abs(g[covered, 3] - w[covered])
    dxyz = np.linalg.norm(g[covered, :3] - xyz[covered], axis=1)
    rel = dw / np.maximum(1.0, w[covered])
    print(f'check-pass {os.path.basename(stem)}: {covered.sum()} covered px; |w - ref| {pct(dw)}; relative p99.9 {np.percentile(rel, 99.9):.4f};'
          f' |xyz - ref| m {pct(dxyz)}; sky/arm zeros {np.all(zero == 0) and np.all(g[outside] == 0)}')
    assert np.all(zero == 0) and np.all(g[outside] == 0), 'sky and arm-reach pixels must be 0'
    small = w[covered] < 8  # half precision: absolute agreement where w is small, relative where it is large (a fast mover, a sway)
    assert (not small.any() or np.percentile(dw[small], 99) < 0.05) and np.percentile(rel, 99.9) < 0.02, 'pass w differs from the reference'
    movers = w[covered] > 0.75
    assert ((g[covered, 3] >= 0.75) == movers).mean() > 0.999, 'pass and reference disagree on movers'
    print(f'check-pass: movers after the net {(g[:, 3] >= 0.75).mean() * 100:.3f}% of samples')
    assert np.percentile(dxyz, 99) < 0.01 + 0.0, 'pass xyz differs from the reference'
    print('check-pass: ok')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('directory', nargs='?')
    ap.add_argument('--out', help='PNG directory (default: <directory>/alt-motion-analysis)')
    ap.add_argument('--threshold', type=float, default=.75, help='mover threshold in render px (design: mover_px 0.75)')
    ap.add_argument('--selftest', action='store_true')
    ap.add_argument('--check-pass', metavar='STEM', help='run tests/eye_motion_pass_check on this dump (or "synthetic") and compare')
    ap.add_argument('--exe', default=os.path.join(os.path.dirname(__file__), '..', 'build', 'eye_motion_pass_check.exe'))
    ap.add_argument('--work', default=os.path.join(tempfile.gettempdir(), 'eye_motion_pass_check'))
    a = ap.parse_args()
    if a.selftest: return selftest()
    if a.check_pass:
        stem = a.check_pass
        if stem == 'synthetic':
            os.makedirs(a.work, exist_ok=True); _synth(a.work, 'own_mover'); stem = os.path.join(a.work, 'alt-motion-1-3')
        return check_pass(stem, a.exe, a.work)
    if not a.directory: ap.error('directory required')
    bursts = load_dumps(a.directory)
    if not bursts: sys.exit('no alt-motion-*-*.json dumps with -mv.rg16f and -depth.r32 in ' + a.directory)
    analyse(bursts, a.threshold, a.out or os.path.join(a.directory, 'alt-motion-analysis'))


if __name__ == '__main__':
    main()
