"""Plan 1.4 margin study for the built-in lens mask (src/visibility_mask.hpp lens_mask_fallback).

The fallback hides the FOV corners outside an ellipse centred on the FOV rectangle with semi-axes `scale` x its half extents. This
grid study asks, for lens outlines that hide as much as the VDXR Quest 3 hidden mesh (xr-recoveries.log 2026-11-19: 9.1 % of the FOV,
about 7.1 % inside it once the mesh's ~1 % overhang ring is removed), how large `scale` must be so the ellipse contains every visible
pixel. Pure Python, no game, no OpenXR. Usage: python tools/lens_mask_fallback_study.py
"""
import math

L, R, U, D = -0.933, 1.000, 1.111, -1.192  # VDXR Quest 3 left eye FOV tangents (the right eye is its mirror image)
CX, CY, HX, HY = (L + R) / 2, (U + D) / 2, (R - L) / 2, (U - D) / 2
N = 500

def grid():
    pts = []
    for j in range(N):
        y = D + (U - D) * (j + .5) / N
        for i in range(N):
            x = L + (R - L) * (i + .5) / N
            nx, ny = (x - CX) / HX, (y - CY) / HY
            pts.append((x, y, nx, ny, math.hypot(nx, ny)))
    return pts

OUTLINES = {  # visible region = {f(p) <= parameter}, the parameter solved for the hidden share
    'ellipse on the rectangle centre': lambda p: p[4],
    'round lens about the optical axis': lambda p: math.hypot(p[0], p[1]),
    'round lens decentred +.04,+.04': lambda p: math.hypot(p[0] - .04, p[1] - .04),
    'round lens decentred -.04,-.04': lambda p: math.hypot(p[0] + .04, p[1] + .04),
    'round lens decentred +.04,-.04': lambda p: math.hypot(p[0] - .04, p[1] + .04),
    'round lens decentred -.04,+.04': lambda p: math.hypot(p[0] + .04, p[1] - .04),
    'superellipse n=3': lambda p: (abs(p[2]) ** 3 + abs(p[3]) ** 3) ** (1 / 3),
    'superellipse n=4': lambda p: (abs(p[2]) ** 4 + abs(p[3]) ** 4) ** (1 / 4),
    'chamfered corners': lambda p: abs(p[2]) + abs(p[3]),
}

def main():
    pts = grid()
    m = len(pts)
    for scale in (1.25, 1.30, 1.32):
        print(f"fallback scale {scale}: hides {sum(1 for p in pts if p[4] > scale) / m * 100:.2f} % of the FOV")
    for share in (0.071, 0.091):
        print(f"outlines hiding {share * 100:.1f} % of the FOV: smallest safe scale")
        worst = 0
        for name, f in OUTLINES.items():
            vals = sorted((f(p), p[4]) for p in pts)
            cut = int(round((1 - share) * m))
            need = max(nr for _, nr in vals[:cut])
            worst = max(worst, need)
            print(f"  {name:36s} {need:.3f}")
        print(f"  worst {worst:.3f}")

if __name__ == '__main__':
    main()
