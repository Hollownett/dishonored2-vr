#!/usr/bin/env python3
"""v1178 offline check: the engine's motion-vector arithmetic in alternate-eye mode, per draw path (pure numpy, no game).

Engine (ShaderCache_PC.rpd psMotionVector, 14 copies; CB filler 0x3586a0):
  mv_ndc = cur.xy/cur.w - prev.xy/prev.w - jrel,  jrel = 2*(view+0x7858 - view+0x7850),  mv_uv = (0.5*mv_ndc.x, -0.5*mv_ndc.y)
  static/instanced world: cur = cb_viewprojectionmatrix*P, prev = cb_previousviewprojectionmatrix*P (view +0x7510 / +0x7590)
  per-instance entities:  cur/prev = cb_mvpmatrix / cb_previousmvpmatrix rows (the mod installs the eye's own n-2 rows when chained)
Projection jitter: P[2] += 2jx, P[6] += 2jy (w = -z), i.e. NDC offset -2j, UV offset s(j) = (-jx, +jy).
Consumers: FSR/DLSS (MV jittered) and the native TAA dejitter subtract s(J_cur) - s(J_own_prev); the motion pass subtracts the receipt step
only when the MVs are not jittered (eye_motion_pass.hpp). Checks:
  1. v1177 (+0x7858 seeded with the eye's previous jitter): static MV has no step, so the consumers are off by one step on every pixel;
     still props on the other eye's rows carry s(J_n-1) - s(J_n-2) against the other-eye fit (the measured 0.39-0.91 px).
  2. v1178 (+0x7858 = +0x7850 after the receipt): every path gives exactly zero after the consumers (static, own rows, camera-only rows
     from the receipts) and the other-eye fit is exact.
  3. The kept 16-phase table (design doc) equals s(Jn) - s(Jn-2) - s(Jn-3) + s(Jn-5) exactly, and that is the residual of v1155's snapped
     rows when the snap's camera matrix carried the other eye's (n-5, n-3) jitter pair.
"""
import numpy as np

W, H = 1922, 2070
PAT = [(1, -3), (-1, 3), (5, 1), (-3, -5), (-5, 5), (-7, 1), (3, 7), (7, -7)]
TABLE = {4: (-.375, -.625), 5: (.375, -1.25), 6: (1, -.125), 7: (.625, 1.125), 8: (.375, 1.25), 9: (-.25, .125), 10: (-.75, -.375),
         11: (-.5, -.5), 12: (.375, -.75), 13: (.875, -.25)}


def J(n):  # UV jitter of the render at engine frame n (one pattern sample per eye pair, as measured)
    p = PAT[(n // 2 + 5) % 8]
    return np.array([p[0] / 16 / W, p[1] / 16 / H])


def s(j): return np.array([-j[0], j[1]])


def proj(j):
    P = np.array([[.97, 0, 0, 0], [0, .97, 0, 0], [0, 0, 1.4e-6, .01], [0, 0, -1, 0]])
    P[0, 2] += 2 * j[0]; P[1, 2] += 2 * j[1]
    return P


def view(x):  # camera at (x, 1.6, 0) looking down -z
    V = np.eye(4); V[:3, 3] = -np.array([x, 1.6, 0]); return V


def ndc(M, p):
    c = M @ np.append(p, 1); return c[:2] / c[3]


def mv_uv(cur, prev, j50, j58):
    m = cur - prev - 2 * (j58 - j50)
    return np.array([.5 * m[0], -.5 * m[1]])


def run(legacy, n=1001):
    eye_x = {0: -.032, 1: .032}
    e = n % 2
    VP = lambda k: proj(J(k)) @ view(eye_x[k % 2])
    P = np.array([.3, 1.2, -6.0])                       # a still point (static world or a still prop)
    j50, j58 = J(n), (J(n - 2) if legacy else J(n))     # +0x7858 at the upload
    cur = ndc(VP(n), P)
    out = {}
    out['static'] = mv_uv(cur, ndc(VP(n - 2), P), j50, j58)          # cb_previousviewprojectionmatrix = seeded own n-2
    out['own_rows'] = mv_uv(cur, ndc(VP(n - 2), P), j50, j58)        # chained rows = this eye's n-2 current rows
    out['camera_only'] = mv_uv(cur, ndc(VP(n - 2) @ np.linalg.inv(VP(n)) @ VP(n), P), j50, j58)
    consumer = s(J(n)) - s(J(n - 2))                                  # what FSR/DLSS/native TAA remove (jittered MVs)
    res = {k: (v - consumer) * [W, H] for k, v in out.items()}      # true displacement is 0: what is left is the error in px
    # motion pass: puv = uv - mv - step(legacy only); residual against the jittered own-previous reprojection
    step = consumer if legacy else 0
    for k, v in out.items():
        res['pass_' + k] = ((s(J(n)) - s(J(n - 2))) - v - step) * [W, H]
    # other-eye net: a still prop on the other eye's rows (n-1); the pass compares against the other eye's jittered reprojection
    v = mv_uv(cur, ndc(VP(n - 1), P), j50, j58)
    other_step = (s(J(n)) - s(J(n - 1))) if legacy else 0
    pred = ndc(VP(n - 1), P); pred_uv = np.array([.5 * pred[0], -.5 * pred[1]])
    cur_uv = np.array([.5 * cur[0], -.5 * cur[1]])
    res['other_fit'] = ((cur_uv - v - other_step) - pred_uv) * [W, H]
    return res


def main():
    bad = 0
    for n in range(1000, 1016):
        new, old = run(False, n), run(True, n)
        for k, r in new.items():
            if np.abs(r).max() > 1e-6: print(f'FAIL v1178 n={n} {k} {r}'); bad += 1
        if np.abs(old['static']).max() < 1e-6 and np.abs(s(J(n)) - s(J(n - 2))).max() > 0:
            print(f'FAIL v1177 model n={n}: consumers should be off by a step'); bad += 1
        want = (s(J(n - 1)) - s(J(n - 2))) * [W, H]
        if np.abs(old['other_fit'] - want).max() > 1e-6: print(f'FAIL v1177 other fit n={n} {old["other_fit"]} vs {want}'); bad += 1
        for k in ('pass_static', 'pass_own_rows', 'pass_camera_only'):
            if np.abs(old[k]).max() > 1e-6: print(f'FAIL v1177 pass {k} n={n}'); bad += 1
    worst = max(np.abs(run(True, n)['static']).max() for n in range(1000, 1016))
    print(f'v1177: consumers misregister static pixels by up to {worst:.3f} px per axis; v1178: all paths 0')
    # 3. the kept table
    for n, t in TABLE.items():
        r = (s(J(n)) - s(J(n - 2)) - s(J(n - 3)) + s(J(n - 5))) * [W, H]
        snap = (s(J(n - 5)) - s(J(n - 3)) - s(J(n - 2)) + s(J(n))) * [W, H]  # snap rows VP(n-5)VP(n-3)^-1 cur, jrel own
        if np.abs(r - t).max() > 1e-9 or np.abs(snap - t).max() > 1e-9: print(f'FAIL table n={n} {r} {t}'); bad += 1
    print('table: 10/10 phases reproduced' if not bad else f'{bad} failures')
    return 1 if bad else 0


if __name__ == '__main__':
    raise SystemExit(main())
