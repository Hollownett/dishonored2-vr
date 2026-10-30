"""Render the guide's controller pictures from the real Meta Quest 3 Touch Plus models that SteamVR installs
(resources/rendermodels/oculus_quest_plus_controller_{left,right}: OBJ, metres, +Y faceplate, +Z toward the
handle's end, -Y trigger side). Writes launcher/assets/touch-plus-{left,right}.png (transparent, 600 px tall) and
touch-plus.anchors.json: the pixel centre of each button component in those images, which the launcher figures
and the in-game Controllers page use for rings and leaders. Read-only on SteamVR; rerun when the view changes.

    python tools/render_touch_plus.py [--steamvr PATH]
"""
import argparse
import json
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'launcher/assets'
H, SS = 600, 3            # output height, supersampling
YAW, PITCH = 55, 25  # degrees: turn toward the grip side, tilt the faceplate toward the viewer
BUTTONS = {'left': ['button_x', 'button_y', 'button_menu', 'thumbstick', 'grip', 'trigger'],
           'right': ['button_a', 'button_b', 'thumbstick', 'grip', 'trigger']}


def load_obj(path):
    v, vt, vn, faces = [], [], [], []
    for line in open(path):
        k, *r = line.split() or [""]
        if k == 'v': v.append([float(x) for x in r[:3]])
        elif k == 'vt': vt.append([float(x) for x in r[:2]])
        elif k == 'vn': vn.append([float(x) for x in r[:3]])
        elif k == 'f':
            idx = [[int(i) - 1 for i in c.split('/')] for c in r]
            for a in range(1, len(idx) - 1): faces.append([idx[0], idx[a], idx[a + 1]])
    return np.array(v), np.array(vt), np.array(vn), np.array(faces)  # faces: (n, 3 corners, v/vt/vn)


def view(side):
    # Camera looks along -forward; the left controller turns its inner (+X) grip side toward us, the right one mirrors.
    s = -1 if side == 'left' else 1
    y, p = np.radians(YAW) * s, np.radians(PITCH)
    ry = np.array([[np.cos(y), 0, np.sin(y)], [0, 1, 0], [-np.sin(y), 0, np.cos(y)]])
    rx = np.array([[1, 0, 0], [0, np.cos(p), -np.sin(p)], [0, np.sin(p), np.cos(p)]])
    # Model to camera: x right, y up (image), z toward the viewer. Handle (+Z model) points down the image, the
    # faceplate (+Y model) faces the viewer; then turn about the handle and tip the faceplate's front edge down.
    base = np.array([[1, 0, 0], [0, 0, -1], [0, 1, 0]])
    return rx @ ry @ base


def render(folder, side):
    name = 'oculus_quest_plus_controller_' + side
    v, vt, vn, f = load_obj(folder / f'{name}.obj')
    tex = np.asarray(Image.open(folder / f'{name}_color.png').convert('RGB'), np.float32) / 255
    ao = np.asarray(Image.open(folder / f'{name}_ao.png').convert('L'), np.float32) / 255
    R = view(side)
    p, n = v @ R.T, vn @ R.T
    lo, hi = p[:, :2].min(0), p[:, :2].max(0)
    scale = (H * SS * .94) / (hi[1] - lo[1])
    W = int((hi[0] - lo[0]) * scale / .94) + 1
    Hs = H * SS
    def to_px(q): return np.stack([(q[..., 0] - lo[0]) * scale + W * .03, (hi[1] - q[..., 1]) * scale + Hs * .03], -1)
    xy = to_px(p)
    depth = np.full((Hs, W), -np.inf, np.float32)
    rgb = np.zeros((Hs, W, 3), np.float32)
    key, fill = np.array([-.45, .7, .55]), np.array([.7, -.1, .7])
    key, fill = key / np.linalg.norm(key), fill / np.linalg.norm(fill)
    for tri in f:
        a, b, c = xy[tri[:, 0]]
        x0, x1 = int(max(min(a[0], b[0], c[0]), 0)), int(min(max(a[0], b[0], c[0]) + 1, W))
        y0, y1 = int(max(min(a[1], b[1], c[1]), 0)), int(min(max(a[1], b[1], c[1]) + 1, Hs))
        den = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1])
        if x1 <= x0 or y1 <= y0 or abs(den) < 1e-6: continue
        gx, gy = np.meshgrid(np.arange(x0, x1) + .5, np.arange(y0, y1) + .5)
        l0 = ((b[1] - c[1]) * (gx - c[0]) + (c[0] - b[0]) * (gy - c[1])) / den
        l1 = ((c[1] - a[1]) * (gx - c[0]) + (a[0] - c[0]) * (gy - c[1])) / den
        l2 = 1 - l0 - l1
        inside = (l0 >= 0) & (l1 >= 0) & (l2 >= 0)
        if not inside.any(): continue
        z = l0 * p[tri[0, 0], 2] + l1 * p[tri[1, 0], 2] + l2 * p[tri[2, 0], 2]
        win = depth[y0:y1, x0:x1]
        m = inside & (z > win)
        if not m.any(): continue
        win[m] = z[m]
        uv = (l0[..., None] * vt[tri[0, 1]] + l1[..., None] * vt[tri[1, 1]] + l2[..., None] * vt[tri[2, 1]])[m]
        tu = np.clip((uv[:, 0] % 1) * tex.shape[1], 0, tex.shape[1] - 1).astype(int)
        tv = np.clip((1 - uv[:, 1] % 1) * tex.shape[0], 0, tex.shape[0] - 1).astype(int)
        nn = l0[..., None] * n[tri[0, 2]] + l1[..., None] * n[tri[1, 2]] + l2[..., None] * n[tri[2, 2]]
        nn = nn[m] / np.maximum(np.linalg.norm(nn[m], axis=1, keepdims=True), 1e-9)
        nn[nn[:, 2] < 0] *= -1
        half = (key + [0, 0, 1]) / np.linalg.norm(key + [0, 0, 1])
        light = .45 + .85 * np.clip(nn @ key, 0, 1) + .25 * np.clip(nn @ fill, 0, 1)
        spec = .18 * np.clip(nn @ half, 0, 1) ** 40
        col = tex[tv, tu] ** 2.2 * (ao[tv, tu] ** 1.5)[:, None] * light[:, None] + spec[:, None]
        rgb[y0:y1, x0:x1][m] = np.clip(col, 0, 1) ** (1 / 2.2)
    alpha = np.isfinite(depth).astype(np.float32)
    img = Image.fromarray(np.dstack([rgb * 255, alpha * 255]).astype(np.uint8), 'RGBA')
    out = img.resize((round(W / SS), H), Image.LANCZOS)
    anchors = {}
    for comp in BUTTONS[side]:
        cv, *_ = load_obj(folder / f'{name}_{comp}.obj')
        q = cv @ R.T
        px = to_px(q).astype(int).clip(0, [W - 1, Hs - 1])
        seen = q[q[:, 2] >= depth[px[:, 1], px[:, 0]] - .0015]    # vertices on the visible surface of the part
        c = to_px((seen if len(seen) else q).mean(0)) / SS
        anchors[comp.replace('button_', '')] = [round(float(c[0]), 1), round(float(c[1]), 1)]
    return out, anchors


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--steamvr', default=r'C:\Program Files (x86)\Steam\steamapps\common\SteamVR')
    a = ap.parse_args()
    models = Path(a.steamvr) / 'resources/rendermodels'
    result = {}
    for side in ('left', 'right'):
        img, anchors = render(models / f'oculus_quest_plus_controller_{side}', side)
        img.save(OUT / f'touch-plus-{side}.png', optimize=True)
        result[side] = {'size': list(img.size), 'anchors': anchors}
        print(side, img.size, anchors)
    (OUT / 'touch-plus.anchors.json').write_text(json.dumps(result, indent=2) + '\n')


if __name__ == '__main__':
    main()
