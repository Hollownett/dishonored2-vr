"""Per-path scores and before/after crops for stale_warp_replay outputs (v1152 dump replays).

python tools/stale_warp_crops.py <dump dir> <replay dir> <N> [--before NAME] [--crop x0 y0 x1 y1] [--png OUT]

Scores (inside a 48 px margin) the replay's -replay-on.rgba and -replay-off.rgba, and an optional earlier "on" output
(<replay dir>/stale-warp-N-<NAME>.rgba), with F = (|R-K|+|R-X|-|X-K|)/2 and B4 = max over K, X of the best match of R within +-4 px,
per path of the replay's path map (-paths.png from REPLAY_PATHS). --png writes kept | before | on | next of the crop, enlarged 3x.
"""
import sys, zlib, argparse
import numpy as np


def read_rgba(path, w, h):
    return np.fromfile(path, np.uint8).reshape(h, w, 4)[..., :3].astype(np.float32)


def read_png(path):  # the replay tool's stored-deflate RGB PNG
    b = open(path, 'rb').read(); i = 8; idat = b''; w = h = 0
    while i < len(b):
        n = int.from_bytes(b[i:i + 4], 'big'); t = b[i + 4:i + 8]
        if t == b'IHDR': w = int.from_bytes(b[i + 8:i + 12], 'big'); h = int.from_bytes(b[i + 12:i + 16], 'big')
        if t == b'IDAT': idat += b[i + 8:i + 8 + n]
        i += 12 + n
    return np.frombuffer(zlib.decompress(idat), np.uint8).reshape(h, w * 3 + 1)[:, 1:].reshape(h, w, 3).astype(int)


def write_png(path, rgb):
    h, w, _ = rgb.shape
    raw = b''.join(b'\0' + rgb[y].astype(np.uint8).tobytes() for y in range(h))
    def chunk(t, d):
        import struct
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    import struct
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))


def nearest_match(r, t, rad=4):
    best = np.full(r.shape[:2], 1e9, np.float32)
    for dy in range(-rad, rad + 1):
        for dx in range(-rad, rad + 1):
            best = np.minimum(best, np.abs(r - np.roll(t, (dy, dx), (0, 1))).mean(-1))
    return best


def main():
    a = argparse.ArgumentParser()
    a.add_argument('dumps'); a.add_argument('replays'); a.add_argument('n', type=int)
    a.add_argument('--before'); a.add_argument('--crop', type=int, nargs=4); a.add_argument('--png')
    o = a.parse_args()
    d = f'{o.dumps}/stale-warp-{o.n}-'; r = f'{o.replays}/stale-warp-{o.n}-'
    import json
    meta = json.load(open(d + 'meta.json')); w, h = meta['box'][2], meta['box'][3]
    k, x = read_rgba(d + 'kept.rgba', w, h), read_rgba(d + 'next.rgba', w, h)
    variants = {'off': read_rgba(r + 'replay-off.rgba', w, h), 'on': read_rgba(r + 'replay-on.rgba', w, h)}
    if o.before: variants['before'] = read_rgba(r + o.before + '.rgba', w, h)
    path = read_png(r + 'paths.png'); R, G, B = path[..., 0], path[..., 1], path[..., 2]
    cls = np.full((h, w), 'rest', object)
    cls[(G > 200) & (R <= 200)] = 'M1 own'; cls[(G > 200) & (B > 200) & (R <= 200)] = 'M1 edge'; cls[(R > 200) & (G > 200)] = 'M1 fresh'; cls[(B > 200) & (G <= 200)] = 'M3'; cls[(R > 200) & (G <= 200) & (B < 200)] = 'M2'
    km = np.fromfile(d + 'kept-motion.rgba16f', np.float16).reshape(h, w, 4)[..., 3].astype(float) >= 0.75
    from scipy.ndimage import maximum_filter
    mover = maximum_filter(km, 33)
    s = (slice(48, -48), slice(48, -48))
    xk = np.abs(x - k).mean(-1)
    scores = {}
    for name, img in variants.items():
        f = (np.abs(img - k).mean(-1) + np.abs(img - x).mean(-1) - xk) / 2
        b4 = np.maximum(nearest_match(img, k), nearest_match(img, x))
        scores[name] = (f, b4)
    regions = [('whole', np.ones((h, w), bool)), ('movers', mover), ('rest', ~mover)] + [(c, cls == c) for c in ('M1 own', 'M1 edge', 'M1 fresh', 'M3', 'M2')]
    print('%-9s %8s ' % ('region', 'pixels') + ' '.join('%14s' % f'{n} F/B4' for n in variants))
    for label, m in regions:
        m = m[s]
        if not m.any(): continue
        print('%-9s %8d ' % (label, m.sum()) + ' '.join('%6.3f/%6.3f ' % (scores[n][0][s][m].mean(), scores[n][1][s][m].mean()) for n in variants))
    if o.png and o.crop:
        x0, y0, x1, y1 = o.crop
        panels = [k] + ([variants['before']] if o.before else []) + [variants['on'], x]
        row = np.concatenate([p[y0:y1, x0:x1] for p in panels], 1)
        write_png(o.png, np.repeat(np.repeat(row, 3, 0), 3, 1))
        print('wrote', o.png)


if __name__ == '__main__':
    main()
