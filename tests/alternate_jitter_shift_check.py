#!/usr/bin/env python3
"""v1156: the alternate-mode jitter relation of src/object_motion.hpp (alternate_jitter_shift + shift_rows), offline, no D3D.

Measured in the v1155 sim (tools/alt_motion_probe.py, 16 still-head renders of bursts 2 and 3): still props carried one uniform
residual vector per render, a function of the engine frame n alone. This check
  1. reproduces every measured vector from the jitters with r = st(n) - st(n-3) (st(k) = s(J_k) - s(J_k-2), s(J) = (-x, y)),
  2. mirrors the C++ row shift and shows that, applied to the previous rows of a still model under the dump's real cameras, it moves
     the previous UV by exactly -r (so the entity vector equals the static world's: corrected residual 0),
  3. pins the C++ expressions this mirrors.
The second draw class (r = st(n) - st(n-2), 0.3 % of pixels in two renders) is listed as not corrected.
"""
import re
from pathlib import Path
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
PAT = np.array([[1, -3], [-1, 3], [5, 1], [-3, -5], [-5, 5], [-7, 1], [3, 7], [7, -7]]) / 16  # game_stereo.hpp engine_taa_pattern (px)
W, H = 2688, 2880
J = lambda n: PAT[(n // 2 + 5) % 8] / [W, H]   # v1155 sim: the view jitter (UV) of engine frame n, one sample per eye pair
# measured residual vectors (px) by engine frame (native_counter), bursts 2 (9156..9163) and 3 (10134..10141)
MEASURED = {9156: (-0.375, -0.625), 9157: (0.375, -1.25), 9158: (1.0, -0.125), 9159: (0.625, 1.125), 9160: (0.375, 1.25),
            9161: (-0.25, 0.125), 9162: (-0.75, -0.375), 9163: (-0.5, -0.5), 10134: (1.0, -0.125), 10135: (0.625, 1.125),
            10136: (0.375, 1.25), 10137: (-0.25, 0.125), 10138: (-0.75, -0.375), 10139: (-0.5, -0.5), 10140: (0.375, -0.75),
            10141: (0.875, -0.25)}
SECOND = {10136: (-0.25, 0.125), 10138: (-0.5, -0.5)}


def shift(jc, jp, o3, o5):  # mirror of ObjectMotion::alternate_jitter_shift (NDC)
    rx = (-jc[0] + jp[0]) - (-o3[0] + o5[0]); ry = (jc[1] - jp[1]) - (o3[1] - o5[1])
    return np.array([-2 * rx, 2 * ry]), np.array([rx, ry])


def shift_rows(m, sh):  # mirror of ObjectMotion::shift_rows on a row-major 4x4 (column vectors)
    m = m.copy(); m[0] += sh[0] * m[3]; m[1] += sh[1] * m[3]; return m


def uv(m, p):
    c = m @ np.append(p, 1.0); return np.array([(c[0] / c[3] + 1) * .5, (1 - c[1] / c[3]) * .5])


def main():
    rng = np.random.default_rng(3)
    P = np.array([[.97, 0, 0, 0], [0, .97, 0, 0], [0, 0, 1.4e-6, .01], [0, 0, -1, 0]])
    for n, r in MEASURED.items():
        sh, r_uv = shift(J(n), J(n - 2), J(n - 3), J(n - 5))
        assert np.abs(r_uv * [W, H] - r).max() < 0.02, (n, r_uv * [W, H], r)
        # a still model point seen by a camera; the installed previous rows (any VP) shifted by sh move its previous UV by -r
        prev = P @ np.eye(4); prev[:3, 3] = rng.normal(size=3)
        for _ in range(4):
            pt = np.array([rng.uniform(-1, 1), rng.uniform(-1, 1), -rng.uniform(1, 20)])
            d = (uv(shift_rows(prev, sh), pt) - uv(prev, pt)) * [W, H]
            assert np.abs(d + np.array(r)).max() < 1e-6, (n, d, r)
    for n, r in SECOND.items():
        _, r_uv = shift(J(n), J(n - 2), J(n - 3), J(n - 5))
        print(f'not corrected: second class at frame {n}: its residual {r} px, the shift assumes {np.round(r_uv * [W, H], 4)} px')
    src = (ROOT / 'src' / 'object_motion.hpp').read_text(encoding='utf-8')
    assert 'const double rx=(-double(jc[0])+jp[0])-(-double(o3[0])+o5[0]),ry=(double(jc[1])-jp[1])-(double(o3[1])-o5[1]);' in src
    assert 'shift[0]=static_cast<float>(-2*rx);shift[1]=static_cast<float>(2*ry);' in src
    assert 'for(int j=0;j<4;++j) {m[j]+=shift[0]*m[12+j];m[4+j]+=shift[1]*m[12+j];}' in src
    assert 'c=find(out.eye^1u,out.frame-3),*d=find(out.eye^1u,out.frame-5);' in src
    print('PASS: alternate jitter relation reproduces 16/16 measured renders; the row shift cancels it exactly for a still model')


if __name__ == '__main__':
    main()
