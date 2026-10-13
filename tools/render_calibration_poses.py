"""Render the calibration poses (fists at the shoulders, arms straight out) on the player's own textured body for the
in-game VR settings page, so the avatar shows exactly the pose the player should take.

    python tools/render_calibration_poses.py [--frames-dir build/tmpcheck]

Needs the read-only player asset pack (artifacts/fullbody-assets, tools/extract_body_assets.py), the masked NPC head
pack (tools/extract_head_assets.py; the first-person body has no head) and solver frames from
`body_ik_check.exe <frames.json> <corvo|emily>` (scenarios "shoulders" and "tpose"). Lit like the main-menu
character scenes: warm key from upper front-left, cool rim from behind, soft fill, crushed blacks, a light
painterly (Kuwahara) finish and grain. Writes artifacts/game-art/pose_<shoulders|tpose>_<corvo|emily>.png and .rgba
(u32 width, u32 height, straight RGBA8) - the user's own game assets, never committed (artifacts/ is git-ignored)."""
import argparse
import json
import math
import struct
from pathlib import Path

import numpy as np
from PIL import Image
from scipy import ndimage

import render_body_ik as r

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'artifacts' / 'game-art'
POSES = {'shoulders': 'shoulders_1.4s', 'tpose': 'tpose_1.4s'}
HEAD_JOINT = 'center_head_jnt'
GREY = (0.35, 0.35, 0.37)
# Lights: direction in the camera basis (right, up, towards camera) and linear RGB intensity.
KEY = ((-0.65, 0.65, 0.45), (2.5, 2.38, 2.2))
FILL = ((0.85, -0.05, 0.5), (0.30, 0.34, 0.42))
RIM = ((0.55, 0.30, -0.8), (0.55, 0.95, 1.4))
AMBIENT = (0.05, 0.055, 0.07)
EXPOSURE = 1.0
BLACK = 0.03              # crushed blacks
PAINT_RADIUS = 2          # Kuwahara radius at supersampled resolution
GRAIN = 0.012


def load_rgba(path, max_size=1024):
    im = Image.open(path).convert('RGBA')
    if max(im.size) > max_size:
        im = im.resize((max_size * im.width // max(im.size), max_size * im.height // max(im.size)),
                       Image.Resampling.BILINEAR)
    a = np.asarray(im, np.float32) / 255.0
    return a[..., :3], a[..., 3]


def load_parts(char):
    """Body/arm assets plus the head, each with surfaces (index start, count, (rgb, alpha) or None, alpha test)."""
    assets = r.load_character(char)
    tex = {}
    for part in r.PARTS:
        a = assets[part]
        tex[part] = []
        for (s0, n, _), material in zip(a['surfaces'], a['materials']):
            path = r._texture_path(char, material, part)
            tex[part].append((s0, n, load_rgba(path) if path else None, False))
    z = np.load(r.PACK / char / 'head.npz')
    names = [str(n) for n in z['skeleton_joint_names']]
    head = dict(positions=z['positions'], normals=z['normals'][:, :3], uv0=z['uv0'], index=z['index'].astype(np.intp),
                surfaces=[(int(s0), int(n), load_rgba(r.TEXTURES / str(t)), bool(a)) for s0, n, t, a in
                          zip(z['index_start'], z['index_count'], z['texture'], z['alpha_test'])],
                bind=z['skeleton_model_bind_3x4'][names.index(HEAD_JOINT)])
    return assets, tex, head


def pose_head(frame, assets, head):
    """Rigidly carry the NPC head with the player's posed head joint. The two rigs use different joint axes, so match
    model space instead: shift the NPC head joint onto the player's bind head joint, then apply the player's
    bind-to-posed head motion (which includes the solver's body scale)."""
    body = assets['body']
    i = body['joint_names'].index(HEAD_JOINT)
    bind = r.promote(body['model_bind'][i][None])[0]
    motion = r.mat4(np.asarray(frame['body'], np.float32).reshape(-1, 16)[i]) @ np.linalg.inv(bind)
    verts = (head['positions'] + bind[:3, 3] - head['bind'][:, 3]) @ motion[:3, :3].T + motion[:3, 3]
    return dict(head, verts=verts.astype(np.float32), normals=head['normals'] @ motion[:3, :3].T)


def raster_triangle(gbuf, view, tex, alpha_test, p, uvs, nrms):
    """z-buffered triangle into the albedo/normal/depth G-buffer (orthographic views only)."""
    albedo, normal, depth = gbuf
    x, y, z = p[:, 0], p[:, 1], p[:, 2]
    xlo, xhi = int(max(float(x.min()), 0.0)), int(min(float(x.max()) + 1.0, view.w))
    ylo, yhi = int(max(float(y.min()), 0.0)), int(min(float(y.max()) + 1.0, view.h))
    den = (y[1] - y[2]) * (x[0] - x[2]) + (x[2] - x[1]) * (y[0] - y[2])
    if xhi <= xlo or yhi <= ylo or abs(den) < 0.2:
        return
    sx, sy = view.gx[xlo:xhi], view.gy[ylo:yhi, None]
    l0 = ((y[1] - y[2]) * (sx - x[2]) + (x[2] - x[1]) * (sy - y[2])) / den
    l1 = ((y[2] - y[0]) * (sx - x[2]) + (x[0] - x[2]) * (sy - y[2])) / den
    l2 = 1.0 - l0 - l1
    sub = depth[ylo:yhi, xlo:xhi]
    zz = l0 * z[0] + l1 * z[1] + l2 * z[2]
    hit = (l0 >= 0.0) & (l1 >= 0.0) & (l2 >= 0.0) & (zz < sub)
    if tex is not None and hit.any():
        rgb, alpha = tex
        th, tw = alpha.shape
        tx = np.minimum((np.mod(l0 * uvs[0, 0] + l1 * uvs[1, 0] + l2 * uvs[2, 0], 1.0) * tw).astype(np.int32), tw - 1)
        ty = np.minimum((np.mod(l0 * uvs[0, 1] + l1 * uvs[1, 1] + l2 * uvs[2, 1], 1.0) * th).astype(np.int32), th - 1)
        if alpha_test:
            hit &= alpha[ty, tx] >= 0.5
    if not hit.any():
        return
    sub[hit] = zz[hit]
    albedo[ylo:yhi, xlo:xhi][hit] = GREY if tex is None else rgb[ty[hit], tx[hit]]
    normal[ylo:yhi, xlo:xhi][hit] = l0[hit][:, None] * nrms[0] + l1[hit][:, None] * nrms[1] + l2[hit][:, None] * nrms[2]


def gbuffer(view, parts):
    gbuf = (np.zeros((view.h, view.w, 3), np.float32), np.zeros((view.h, view.w, 3), np.float32),
            np.full((view.h, view.w), np.inf, np.float32))
    for part in parts:
        pt = np.stack(view.project(part['verts']), axis=1).astype(np.float32)
        for s0, count, tex, alpha_test in part['surfaces']:
            for t in part['index'][s0:s0 + count]:
                raster_triangle(gbuf, view, tex, alpha_test, pt[t], part['uv0'][t], part['normals'][t])
    return gbuf


def camera_dir(view, d):
    return r._unit(d[0] * view.right + d[1] * view.up - d[2] * view.fwd)


def key_shadow(view, depth, mask, parts, size=1400):
    """Shadow-map the key light: 1 where lit, 0 where another part (head, arms, collar) blocks it; softened."""
    key = camera_dir(view, KEY[0])
    right = r._unit(np.cross(-key, np.array([0.0, 0.0, 1.0], np.float32)))
    light = r.View('key', size, size, view.origin, -key, right, np.cross(right, -key), False, span=view.span * 1.2)
    light.origin = view.origin + view.fwd * 5 - light.fwd * 5          # look at the same centre
    occluder = gbuffer(light, parts)[2]
    ys, xs = np.nonzero(mask)
    world = (view.origin + np.outer((xs + 0.5 - view.cx) / view.scale, view.right)
             + np.outer((view.cy - ys - 0.5) / view.scale, view.up) + np.outer(depth[ys, xs], view.fwd))
    lx, ly, lz = light.project(world)
    lx, ly = np.clip(lx.astype(int), 0, size - 1), np.clip(ly.astype(int), 0, size - 1)
    lit = np.ones(mask.shape, np.float32)
    lit[ys, xs] = lz <= occluder[ly, lx] + 0.02
    return ndimage.gaussian_filter(lit, 2.5)


def aces(x):
    return np.clip(x * (2.51 * x + 0.03) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0)


def shade(albedo, normal, depth, mask, view, shadow):
    toward = -view.fwd
    lit = {name: (camera_dir(view, d), np.array(rgb, np.float32)) for name, (d, rgb) in dict(key=KEY, fill=FILL, rim=RIM).items()}
    n = normal / np.maximum(np.linalg.norm(normal, axis=-1, keepdims=True), 1e-9)
    facing = n @ toward
    n *= np.where(facing < 0, -1.0, 1.0)[..., None]          # two-sided: face the camera
    facing = np.abs(facing)
    # Cheap ambient occlusion: darken pixels lying behind their surroundings (crevices, under collars and arms).
    filled = depth[tuple(ndimage.distance_transform_edt(~mask, return_distances=False, return_indices=True))]
    occlusion = 1.0 - np.clip((filled - ndimage.gaussian_filter(filled, 14)) / 0.05, 0.0, 0.55)
    key_dir, key_rgb = lit['key']
    nk = n @ key_dir
    diffuse = (key_rgb * (np.clip(nk, 0, None) * shadow)[..., None] + lit['fill'][1] * np.clip((n @ lit['fill'][0] + 0.3) / 1.3, 0, None)[..., None]
               + np.array(AMBIENT, np.float32)) * occlusion[..., None]
    half = r._unit(key_dir + toward)
    spec = 0.06 * np.clip(n @ half, 0, None) ** 32 * (nk > 0) * occlusion * shadow
    rim = lit['rim'][1] * ((1.0 - facing) ** 2.5 * np.clip(n @ lit['rim'][0] + 0.2, 0, None))[..., None]
    hdr = EXPOSURE * (albedo ** 2.2) * diffuse + spec[..., None] + rim
    out = aces(hdr) ** (1 / 2.2)
    return np.clip((out - BLACK) / (1.0 - BLACK), 0.0, 1.0) ** 1.08


def kuwahara(img, radius):
    """Edge-preserving painterly smoothing: each pixel takes the mean of its least-varied quadrant."""
    size, s = radius + 1, radius // 2
    mean = ndimage.uniform_filter(img, (size, size, 1))
    var = (ndimage.uniform_filter(img * img, (size, size, 1)) - mean * mean).sum(-1)
    shifts = [(dy, dx) for dy in (-s, s) for dx in (-s, s)]
    means = np.stack([np.roll(mean, sh, (0, 1)) for sh in shifts])
    best = np.argmin(np.stack([np.roll(var, sh, (0, 1)) for sh in shifts]), axis=0)
    return np.take_along_axis(means, best[None, ..., None], 0)[0]


def render(frame, assets, tex, head, size=900, supersample=2):
    posed = r.pose_frame(frame, assets)
    parts = [dict(p, surfaces=tex[k]) for k, p in posed.items()] + [pose_head(frame, assets, head)]
    w = h = size * supersample
    # Slightly turned (20 deg) and raised so the arms read in depth, like a character-select pose.
    az, el = math.radians(20.0), math.radians(6.0)
    fwd = -np.array([math.cos(el) * math.cos(az), math.cos(el) * math.sin(az), math.sin(el)], np.float32)
    right = r._unit(np.cross(fwd, np.array([0.0, 0.0, 1.0], np.float32)))
    up = np.cross(right, fwd)
    verts = np.concatenate([p['verts'] for p in parts])
    lo, hi = verts.min(0), verts.max(0)
    centre = (lo + hi) / 2
    view = r.View('calibration', w, h, centre - fwd * 5, fwd, right, up, False, span=float(max(hi - lo)) * 1.08)
    gbuf = gbuffer(view, parts)
    mask = np.isfinite(gbuf[2])
    # Keep only the figure: Emily's arm mesh has two shoulder-piece triangles skinned to the thumb that float free.
    labels = ndimage.label(ndimage.binary_dilation(mask, iterations=8))[0]
    mask &= labels == np.bincount(labels[mask]).argmax()
    color = shade(*gbuf, mask, view, key_shadow(view, gbuf[2], mask, parts))
    # Spread figure colours into the background so smoothing and resampling never pull in black at the silhouette.
    color = color[tuple(ndimage.distance_transform_edt(~mask, return_distances=False, return_indices=True))]
    color = kuwahara(color, PAINT_RADIUS)
    rgba = np.concatenate((color, mask[..., None].astype(np.float32)), axis=2)
    img = Image.fromarray((rgba * 255 + 0.5).astype(np.uint8), 'RGBA').resize((size, size), Image.Resampling.LANCZOS)
    a = np.asarray(img, np.float32) / 255.0
    a[..., :3] = np.clip(a[..., :3] + np.random.default_rng(7).normal(0.0, GRAIN, a.shape[:2])[..., None], 0, 1)
    img = Image.fromarray((a * 255 + 0.5).astype(np.uint8), 'RGBA')
    return img.crop(img.getchannel('A').point(lambda v: 255 if v > 8 else 0).getbbox())


def save(img, stem):
    OUT.mkdir(parents=True, exist_ok=True)
    img.save(OUT / (stem + '.png'))
    raw = img.convert('RGBA')
    (OUT / (stem + '.rgba')).write_bytes(struct.pack('<II', raw.width, raw.height) + raw.tobytes())
    print(stem, raw.size)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--frames-dir', type=Path, default=ROOT / 'build' / 'tmpcheck')
    a = ap.parse_args()
    for char in ('corvo', 'emily'):
        frames = {f['label']: f for f in json.loads((a.frames_dir / ('poses-%s.json' % char)).read_text())['frames']}
        assets, tex, head = load_parts(char)
        for pose, label in POSES.items():
            save(render(frames[label], assets, tex, head), 'pose_%s_%s' % (pose, char))


if __name__ == '__main__':
    main()
