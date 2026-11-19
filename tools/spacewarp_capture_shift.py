"""Spacewarp captures: how far the generated frame moved, relative to the real frames around it.

    python tools/spacewarp_capture_shift.py <dir>

Per triple (sw-capture-NN-{real,generated,next}), per 256 px tile, the shift R->X and R->G by phase correlation (luminance, integer
pixels). For tiles where R->X moved at least 3 px: the median of (R->G) . (R->X) / |R->X|^2, the share of the real step the
generated frame took along it (0.5 = halfway, as spacewarp at half rate should), and the median of the perpendicular part relative to
|R->X| (0 = along the real motion). Valid only where the real frames differ by motion the generated frame models: walking, running and
turning; on the simulated runtime the head's own change is not predicted (its poses ignore the requested time), so use straight
walking there.
"""
import json, os, sys
import numpy as np


def load(stem):
    m = json.load(open(stem + '.json'))
    img = np.fromfile(stem + '.rgba', np.uint8).reshape(m['height'], m['width'], 4)[..., :3].astype(np.float32)
    return img @ np.array([0.2126, 0.7152, 0.0722], np.float32)


def shift(a, b):
    """Integer (dy, dx) that b is shifted by relative to a, by phase correlation."""
    w = np.outer(np.hanning(a.shape[0]), np.hanning(a.shape[1])).astype(np.float32)
    A, B = np.fft.fft2((a - a.mean()) * w), np.fft.fft2((b - b.mean()) * w)
    r = B * np.conj(A); r /= np.abs(r) + 1e-6
    c = np.abs(np.fft.ifft2(r)); y, x = np.unravel_index(np.argmax(c), c.shape)
    if y > a.shape[0] // 2: y -= a.shape[0]
    if x > a.shape[1] // 2: x -= a.shape[1]
    return np.array([y, x], float), c.max() / (c.mean() + 1e-9)


def main():
    d = sys.argv[1]; n = 0
    print('triple tiles  along(median)  across(median)   |R->X| px median')
    while os.path.exists(f'{d}/sw-capture-{n:02d}-next.json'):
        s = f'{d}/sw-capture-{n:02d}-'; R, G, X = load(s + 'real'), load(s + 'generated'), load(s + 'next')
        T = 256; along, across, mags = [], [], []
        for y in range(T // 2, R.shape[0] - T - T // 2, T):
            for x in range(T // 2, R.shape[1] - T - T // 2, T):
                a = R[y:y + T, x:x + T]
                if a.std() < 4: continue
                rx, q1 = shift(a, X[y:y + T, x:x + T]); rg, q2 = shift(a, G[y:y + T, x:x + T])
                m = np.hypot(*rx)
                if m < 3 or q1 < 8: continue
                u = rx / m
                along.append(float(rg @ u) / m); across.append(abs(float(rg[0] * u[1] - rg[1] * u[0])) / m); mags.append(m)
        if along:
            print(f'{n:6d} {len(along):5d}  {np.median(along):12.2f}  {np.median(across):14.2f}   {np.median(mags):8.1f}')
        else:
            print(f'{n:6d}     0  (no tile moved 3 px or more between the real frames)')
        n += 1


if __name__ == '__main__':
    main()
