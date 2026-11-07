"""Render full-body VR IK solver frames on the extracted player meshes.

Reads a solver JSON (animated model-space joint transforms per part plus an eye
camera) and skins the read-only asset pack in artifacts/fullbody-assets, then
draws one tile row per frame: front orthographic, side orthographic, three
quarter, and the first-person view from the eye.  Also draws the z=0 ground grid
and any solver markers so foot contact and grip placement can be judged.

Usage:
  python tools/render_body_ik.py <frames.json> <out_dir> [--frames 0,3,5]
  python tools/render_body_ik.py --selfcheck corvo [--out-dir DIR]

The game install is never touched.
"""
import argparse
import json
import math
import os
import time
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
PACK = ROOT / 'artifacts/fullbody-assets/pack'
TEXTURES = ROOT / 'artifacts/fullbody-assets/textures'
DEFAULT_OUT = ROOT / 'artifacts/fullbody-ik-render'

PARTS = ('body', 'left_arm', 'right_arm')
PART_JOINTS = {'body': 20, 'left_arm': 69, 'right_arm': 69}
NEAR = 0.03                      # camera-space near plane (m)
AMBIENT = 0.22
LIGHT = np.array([0.42, 0.36, 0.83], np.float32)
LIGHT /= np.linalg.norm(LIGHT)
GREY = np.array([0.62, 0.62, 0.65], np.float32)
BACKGROUND = 0.14
TILE_ORTHO = (400, 680)          # front / side / three-quarter: width, height
TILE_FP = (907, 680)             # first person 4:3 at the same height
GAP = 6
SHEET_MAX_W = 2400


# ------------------------------------------------------------------ transforms
def mat4(flat):
    """4x4 column-major affine (16 floats) -> row-major 4x4 (column vectors)."""
    a = np.asarray(flat, np.float32).reshape(-1)
    if a.size != 16 or not np.isfinite(a).all():
        raise ValueError('each transform must be 16 finite floats')
    if np.abs(a[[3, 7, 11, 15]] - np.array([0., 0., 0., 1.], np.float32)).max() > 1e-4:
        raise ValueError('transform is not affine (last row must be 0,0,0,1)')
    return np.ascontiguousarray(a.reshape(4, 4).T)


def promote(rows34):
    """(...,3,4) row-major -> (...,4,4) homogeneous with last row 0,0,0,1."""
    a = np.asarray(rows34, np.float32)
    out = np.zeros(a.shape[:-2] + (4, 4), np.float32)
    out[..., :3, :] = a
    out[..., 3, 3] = 1.0
    return out


def flatten44(m):
    """row-major 4x4 -> 16 floats in the solver's column-major order."""
    return np.asarray(m, np.float32).T.reshape(-1).tolist()


def quat_rotation(q):
    x, y, z, w = np.asarray(q, np.float64)
    n = x * x + y * y + z * z + w * w
    if n < 1e-12:
        raise ValueError('degenerate eye rotation quaternion')
    s = 2.0 / n
    return np.array([[1 - s * (y * y + z * z), s * (x * y - z * w), s * (x * z + y * w)],
                     [s * (x * y + z * w), 1 - s * (x * x + z * z), s * (y * z - x * w)],
                     [s * (x * z - y * w), s * (y * z + x * w), 1 - s * (x * x + y * y)]], np.float32)


# ------------------------------------------------------------------ asset pack
def _texture_path(char, material, part):
    if part == 'body':
        name = '%s_body_d.png' % char
    else:
        m = str(material).lower()
        if 'sleeves' in m:
            name = 'player_%s_sleeves_d.png' % char
        elif 'gloves' in m:
            name = 'player_%s_hands_d.png' % char
        elif 'hands' in m:
            name = 'player_%s_hands_tattoo_d.png' % char
        elif 'body' in m:                       # shoulder piece
            name = '%s_body_d.png' % char
        else:                                   # ring and anything unknown
            return None
    path = TEXTURES / name
    return path if path.is_file() else None


_TEX_CACHE = {}


def load_texture(path, max_size=1024):
    key = (str(path), max_size)
    if key not in _TEX_CACHE:
        if path is None:
            _TEX_CACHE[key] = None
        else:
            im = Image.open(path).convert('RGB')
            if max(im.size) > max_size:          # keep texel fetches cheap
                scale = max_size / max(im.size)
                im = im.resize((max(1, int(im.width * scale)), max(1, int(im.height * scale))),
                               Image.Resampling.BILINEAR)
            arr = np.asarray(im, np.float32) / 255.0
            _TEX_CACHE[key] = np.clip(arr ** 0.9 * 1.75 + 0.04, 0.0, 1.0).astype(np.float32)
    return _TEX_CACHE[key]


def load_character(char, parts=PARTS):
    char = str(char).lower()
    if char not in ('emily', 'corvo'):
        raise ValueError('character must be emily or corvo, got %r' % char)
    assets = {}
    for part in parts:
        sk = json.loads((PACK / ('skeleton_%s.json' % part)).read_text())
        z = np.load(PACK / char / ('%s.npz' % part))
        data = {k: z[k] for k in z.files}
        z.close()
        if len(sk['joint_names']) != PART_JOINTS[part]:
            raise ValueError('unexpected joint count in skeleton_%s.json' % part)
        materials = [str(m) for m in data['material']]
        surfaces = [(int(data['index_start'][i]), int(data['index_count'][i]),
                     load_texture(_texture_path(char, materials[i], part)))
                    for i in range(len(materials))]
        assets[part] = dict(
            char=char, joint_names=sk['joint_names'],
            model_bind=np.asarray(sk['model_bind_3x4'], np.float32).reshape(-1, 3, 4),
            inverse_bind=promote(np.asarray(sk['inverse_bind_3x4'], np.float32).reshape(-1, 3, 4)),
            positions=np.ascontiguousarray(data['positions'], np.float32),
            normals=np.ascontiguousarray(data['normals'][:, :3], np.float32),
            uv0=np.ascontiguousarray(data['uv0'], np.float32),
            weights=data['weights'], joints=data['joints'].astype(np.intp),
            index=data['index'].astype(np.intp), surfaces=surfaces, materials=materials)
    return assets


def skin(part_asset, joint_mats):
    """Linear blend skin: palette_j = joint_j @ inverse_bind_j (column vectors)."""
    pal = np.asarray(joint_mats, np.float32) @ part_asset['inverse_bind']
    w = part_asset['weights'].astype(np.float32) / np.float32(255.0)
    j = np.where(w > 0, part_asset['joints'], 0)
    p = pal[j]                                                    # (V,8,4,4)
    pos = np.concatenate((part_asset['positions'],
                          np.ones((len(w), 1), np.float32)), axis=1)
    posed = np.einsum('vkij,vj->vki', p, pos, optimize=True)
    verts = np.einsum('vki,vk->vi', posed, w, optimize=True)[:, :3]
    rot = np.einsum('vkij,vj->vki', p[:, :, :3, :3], part_asset['normals'], optimize=True)
    normals = np.einsum('vki,vk->vi', rot, w, optimize=True)
    normals /= np.maximum(np.linalg.norm(normals, axis=1, keepdims=True), 1e-9)
    if not (np.isfinite(verts).all() and np.isfinite(normals).all()):
        raise ValueError('skinning produced non-finite vertices')
    return verts.astype(np.float32), normals.astype(np.float32)


def pose_frame(frame, assets):
    """Skin every loaded part of one frame into world space."""
    posed = {}
    for part in PARTS:
        if part not in assets or part not in frame or frame[part] is None:
            continue
        mats = np.stack([mat4(f) for f in np.asarray(frame[part], np.float32).reshape(-1, 16)])
        if len(mats) != PART_JOINTS[part]:
            raise ValueError('frame %s: expected %d transforms, got %d'
                             % (part, PART_JOINTS[part], len(mats)))
        verts, normals = skin(assets[part], mats)
        if part != 'body':
            key = part + '_frame'
            if frame.get(key) is not None:
                f = mat4(frame[key])
                rot = f[:3, :3]
                verts = verts @ rot.T + f[:3, 3]
                normals = normals @ rot.T
                normals /= np.maximum(np.linalg.norm(normals, axis=1, keepdims=True), 1e-9)
        posed[part] = dict(verts=verts, normals=normals, uv0=assets[part]['uv0'],
                           index=assets[part]['index'], surfaces=assets[part]['surfaces'])
    return posed


# ---------------------------------------------------------------------- camera
def _unit(v, fallback=None):
    """v / |v|; a zero or non-finite v takes `fallback` (itself normalised) or is an error, never a NaN that reaches the rasteriser."""
    v = np.asarray(v, np.float32)
    n = float(np.linalg.norm(v))
    if np.isfinite(n) and n > 1e-6:
        return v / n
    if fallback is not None:
        return _unit(fallback)
    raise ValueError('cannot normalise a zero or non-finite vector %s' % (v,))


class View:
    """Orthographic or perspective camera; right/up span the image plane."""

    def __init__(self, name, w, h, origin, fwd, right, up, perspective,
                 span=2.0, hfov=math.radians(100.0)):
        self.name, self.w, self.h = name, int(w), int(h)
        self.origin = np.asarray(origin, np.float32)
        self.fwd, self.right, self.up = _unit(fwd), _unit(right), _unit(up)
        self.perspective = bool(perspective)
        self.cx, self.cy = self.w * 0.5, self.h * 0.5
        self.span = float(span)
        self.scale = self.h / self.span
        self.f = 0.5 * self.w / math.tan(0.5 * hfov)
        self.light = LIGHT
        self.gx = np.arange(self.w, dtype=np.float32) + 0.5
        self.gy = np.arange(self.h, dtype=np.float32) + 0.5

    def project(self, pts):
        rel = np.asarray(pts, np.float32) - self.origin
        if self.perspective:
            xc = rel @ self.fwd
            yc = rel @ self.right
            zc = rel @ self.up
            inv = 1.0 / np.where(np.abs(xc) < NEAR, NEAR, xc)
            return self.cx + self.f * yc * inv, self.cy - self.f * zc * inv, xc
        return (self.cx + (rel @ self.right) * self.scale,
                self.cy - (rel @ self.up) * self.scale, rel @ self.fwd)


def ortho_view(name, fwd, center, span):
    w, h = TILE_ORTHO
    fwd = _unit(fwd)
    right = _unit(np.cross(fwd, np.array([0.0, 0.0, 1.0], np.float32)))
    up = np.cross(right, fwd)
    return View(name, w, h, center, fwd, right, up, False, span=span)


def eye_view(eye, hfov=math.radians(100.0)):
    w, h = TILE_FP
    rot = quat_rotation(eye.get('rotation_xyzw', [0.0, 0.0, 0.0, 1.0]))
    fwd = rot @ np.array([1.0, 0.0, 0.0], np.float32)
    up = rot @ np.array([0.0, 0.0, 1.0], np.float32)
    right = rot @ np.array([0.0, -1.0, 0.0], np.float32)     # world +Y is left
    return View('first person', w, h, np.asarray(eye['position'], np.float32),
                fwd, right, up, True, hfov=hfov)


def build_views(center, span, eye):
    az, el = math.radians(35.0), math.radians(12.0)
    three_quarter = -np.array([math.cos(el) * math.cos(az),
                               math.cos(el) * math.sin(az), math.sin(el)], np.float32)
    return [ortho_view('front (+X)', np.array([-1.0, 0.0, 0.0], np.float32), center, span),
            ortho_view('side (+Y)', np.array([0.0, -1.0, 0.0], np.float32), center, span),
            ortho_view('three quarter', three_quarter, center, span),
            eye_view(eye)]


# ------------------------------------------------------------------ rasteriser
def fill_triangle(color, depth, view, tex, p, uvs, nrms, tint=None):
    """z-buffered, textured, Lambert-lit triangle; p is (3,3) screen x,y,depth.
    tint=(rgb, alpha): blend a flat colour over the pixels in front of the depth buffer, without writing depth."""
    if not np.isfinite(p).all():             # a vertex that skinned or projected to NaN/inf draws nothing
        return
    x, y, z = p[:, 0], p[:, 1], p[:, 2]
    xlo = int(max(float(x.min()), 0.0))
    xhi = int(min(float(x.max()) + 1.0, view.w))
    ylo = int(max(float(y.min()), 0.0))
    yhi = int(min(float(y.max()) + 1.0, view.h))
    if xhi <= xlo or yhi <= ylo:
        return
    den = (y[1] - y[2]) * (x[0] - x[2]) + (x[2] - x[1]) * (y[0] - y[2])
    if abs(den) < 0.2:                       # sub-pixel sliver
        return
    den = 1.0 / den
    sx = view.gx[xlo:xhi]
    sy = view.gy[ylo:yhi, None]
    l0 = ((y[1] - y[2]) * (sx - x[2]) + (x[2] - x[1]) * (sy - y[2])) * den
    l1 = ((y[2] - y[0]) * (sx - x[2]) + (x[0] - x[2]) * (sy - y[2])) * den
    l2 = 1.0 - l0 - l1
    inside = (l0 >= 0.0) & (l1 >= 0.0) & (l2 >= 0.0)
    if not inside.any():
        return
    sub = depth[ylo:yhi, xlo:xhi]
    zz = l0 * z[0] + l1 * z[1] + l2 * z[2]
    hit = inside & (zz < sub)
    if not hit.any():
        return
    if tint is None:
        sub[hit] = zz[hit]
    h0, h1, h2 = l0[hit][:, None], l1[hit][:, None], l2[hit][:, None]
    n = h0 * nrms[0] + h1 * nrms[1] + h2 * nrms[2]
    n /= np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-9)
    shade = (AMBIENT + (1.0 - AMBIENT) * np.abs(n @ view.light))[:, None]
    if tint is not None:
        old = color[ylo:yhi, xlo:xhi][hit]
        color[ylo:yhi, xlo:xhi][hit] = old * (1.0 - tint[1]) + np.asarray(tint[0], np.float32) * shade * tint[1]
        return
    if tex is None:
        rgb = np.broadcast_to(GREY * shade, (len(shade), 3))
    else:
        u = np.mod(l0[hit] * uvs[0, 0] + l1[hit] * uvs[1, 0] + l2[hit] * uvs[2, 0], 1.0)
        v = np.mod(l0[hit] * uvs[0, 1] + l1[hit] * uvs[1, 1] + l2[hit] * uvs[2, 1], 1.0)
        th, tw = tex.shape[:2]
        tx = np.minimum((u * tw).astype(np.int32), tw - 1)
        ty = np.minimum((v * th).astype(np.int32), th - 1)
        rgb = tex[ty, tx] * shade
    color[ylo:yhi, xlo:xhi][hit] = rgb


def clip_near(cam, attrs):
    """Clip one triangle against x >= NEAR; returns [(cam(3,3), attrs(3,K))]."""
    inside = cam[:, 0] >= NEAR
    if inside.all():
        return [(cam, attrs)]
    if not inside.any():
        return []
    pts, at = [], []
    for a in range(3):
        b = (a + 1) % 3
        if inside[a]:
            pts.append(cam[a])
            at.append(attrs[a])
        if inside[a] != inside[b]:
            t = (NEAR - cam[a, 0]) / (cam[b, 0] - cam[a, 0])
            pts.append(cam[a] + t * (cam[b] - cam[a]))
            at.append(attrs[a] + t * (attrs[b] - attrs[a]))
    p, a = np.array(pts, np.float32), np.array(at, np.float32)
    return [(p[[0, k, k + 1]], a[[0, k, k + 1]]) for k in range(1, len(pts) - 1)]


def draw_mesh(color, depth, view, part):
    pt = np.stack(view.project(part['verts']), axis=1).astype(np.float32)
    persp = view.perspective
    if persp:
        xc = (part['verts'] - view.origin) @ view.fwd
    for s0, count, tex in part['surfaces']:
        if count <= 0:
            continue
        idx = part['index'][s0:s0 + count]
        X, Y = pt[idx, 0], pt[idx, 1]
        keep = (X.max(1) > 0.0) & (X.min(1) < view.w) & \
               (Y.max(1) > 0.0) & (Y.min(1) < view.h)
        if persp:
            x0, x1, x2 = xc[idx[:, 0]], xc[idx[:, 1]], xc[idx[:, 2]]
            front = keep & (x0 > NEAR) & (x1 > NEAR) & (x2 > NEAR)
            straddle = keep & ~front & ((x0 > NEAR) | (x1 > NEAR) | (x2 > NEAR))
        else:
            front, straddle = keep, None
        P, UV, NR = pt[idx], part['uv0'][idx], part['normals'][idx]
        for t in np.nonzero(front)[0]:
            fill_triangle(color, depth, view, tex, P[t], UV[t], NR[t])
        if straddle is not None:
            for t in np.nonzero(straddle)[0]:
                i0, i1, i2 = idx[t]
                cam = np.array([[xc[i0], 0.0, 0.0], [xc[i1], 0.0, 0.0], [xc[i2], 0.0, 0.0]], np.float32)
                cam[:, 1] = (part['verts'][[i0, i1, i2]] - view.origin) @ view.right
                cam[:, 2] = (part['verts'][[i0, i1, i2]] - view.origin) @ view.up
                attrs = np.concatenate((UV[t], NR[t]), axis=1)
                for c, a in clip_near(cam, attrs):
                    inv = 1.0 / c[:, 0]
                    p = np.stack((view.cx + view.f * c[:, 1] * inv,
                                  view.cy - view.f * c[:, 2] * inv, c[:, 0]), axis=1)
                    fill_triangle(color, depth, view, tex, p, a[:, :2], a[:, 2:])


def draw_ground(color, depth, view):
    """Grey z=0 grid; analytic plane hit, depth-tested against the meshes."""
    h, w = view.h, view.w
    ex = (view.gx - view.cx) / (view.f if view.perspective else view.scale)
    ey = -(view.gy - view.cy) / (view.f if view.perspective else view.scale)
    base = view.origin
    if view.perspective:
        t = np.full((h, w), np.inf, np.float32)
        dirs = (view.fwd[None, None, :]
                + view.right[None, None, :] * ex[None, :, None]
                + view.up[None, None, :] * ey[:, None, None])
        dz = dirs[..., 2]
        ok = dz < -1e-6
        t = np.where(ok, -base[2] / np.where(ok, dz, -1.0), 0.0)
        gx = base[0] + t * dirs[..., 0]
        gy = base[1] + t * dirs[..., 1]
        valid = ok & (t > 0.0)
    else:
        p0 = (base[None, None, :] + view.right[None, None, :] * ex[None, :, None]
              + view.up[None, None, :] * ey[:, None, None])
        dz = float(view.fwd[2])
        if abs(dz) < 1e-6:
            # edge-on plane: it projects to the world z=0 line
            line = np.abs(p0[..., 2]) < 0.006
            gx, gy = p0[..., 0], p0[..., 1]
            hit = line & np.isinf(depth) & (gx * gx + gy * gy < 25.0)
            if hit.any():
                color[hit] = np.array([0.33, 0.33, 0.36], np.float32)
            return
        t = -p0[..., 2] / dz
        gx, gy = p0[..., 0] + t * view.fwd[0], p0[..., 1] + t * view.fwd[1]
        valid = t > 0.0
    r2 = gx * gx + gy * gy
    hit = valid & (r2 < 36.0) & (t < depth)
    if not hit.any():
        return
    gxs, gys, ts = gx[hit], gy[hit], t[hit]
    major = (np.abs(gxs - 2.0 * np.round(gxs / 2.0)) < 0.03) | \
            (np.abs(gys - 2.0 * np.round(gys / 2.0)) < 0.03)
    minor = (np.abs(gxs - 0.5 * np.round(gxs / 0.5)) < 0.018) | \
            (np.abs(gys - 0.5 * np.round(gys / 0.5)) < 0.018)
    lum = np.where(major, 0.30, np.where(minor, 0.46, 0.62)).astype(np.float32)
    fade = np.clip(1.0 - np.sqrt(gxs * gxs + gys * gys) / 6.0, 0.0, 1.0) ** 0.6
    lum = BACKGROUND + (lum - BACKGROUND) * (0.25 + 0.75 * fade)
    color[hit] = np.stack((lum, lum, lum * 1.02), axis=1)
    depth[hit] = ts


MARKER_COLOURS = np.array([[1.00, 0.28, 0.22], [0.30, 1.00, 0.40], [0.35, 0.65, 1.00],
                           [1.00, 0.90, 0.20], [1.00, 0.40, 0.90], [0.30, 1.00, 1.00]], np.float32)


def draw_markers(color, view, markers):
    if not markers:
        return
    h, w = color.shape[:2]
    for i, marker in enumerate(markers):
        pos = np.asarray(marker['position'], np.float32).reshape(1, 3)
        px, py, pz = view.project(pos)
        if view.perspective and float(pz[0]) <= NEAR:
            continue
        xi, yi = int(round(float(px[0]))), int(round(float(py[0])))
        if not (2 <= xi < w - 2 and 2 <= yi < h - 2):
            continue
        color[yi - 1:yi + 2, xi - 1:xi + 2] = 0.03
        c = MARKER_COLOURS[i % len(MARKER_COLOURS)]
        for dx, dy in ((0, 0), (1, 0), (-1, 0), (0, 1), (0, -1), (2, 0), (-2, 0), (0, 2), (0, -2)):
            color[yi + dy, xi + dx] = c


# ------------------------------------------------------------------------ tiles
def _font(size):
    try:
        return ImageFont.load_default(size=size)
    except TypeError:                                   # older Pillow
        return ImageFont.load_default()


def label_tile(tile, label, sub):
    im = Image.fromarray(tile)
    draw = ImageDraw.Draw(im)
    draw.text((8, 6), label, fill=(255, 255, 255), font=_font(20),
              stroke_width=2, stroke_fill=(0, 0, 0))
    draw.text((8, 30), sub, fill=(235, 225, 120), font=_font(15),
              stroke_width=2, stroke_fill=(0, 0, 0))
    return np.asarray(im)


def render_row(frame, assets, extra_label=''):
    posed = pose_frame(frame, assets)
    if not posed:
        raise ValueError('frame has no body/arm transforms')
    points = np.vstack([p['verts'] for p in posed.values()])
    lo, hi = points.min(0), points.max(0)
    lo[2] = min(float(lo[2]), 0.0)                      # always show the ground
    hi[2] = max(float(hi[2]), 0.10)
    center = (lo + hi) * 0.5
    span = float(np.max(hi - lo)) * 1.15
    eye = frame.get('eye') or {}
    if 'position' not in eye:
        eye = dict(position=assets['body']['model_bind'][
            assets['body']['joint_names'].index('camera_jnt'), :, 3].tolist())
    label = extra_label or str(frame.get('label', 'frame'))
    tiles = []
    for view in build_views(center, span, eye):
        color = np.full((view.h, view.w, 3), BACKGROUND, np.float32)
        color[..., 2] += np.linspace(0.0, 0.06, view.h, dtype=np.float32)[:, None]
        depth = np.full((view.h, view.w), np.inf, np.float32)
        for part in PARTS:
            if part in posed:
                draw_mesh(color, depth, view, posed[part])
        draw_ground(color, depth, view)
        draw_markers(color, view, frame.get('markers'))
        tiles.append(label_tile(np.clip(color * 255.0, 0, 255).astype(np.uint8),
                                label, view.name))
    filler = np.full((tiles[0].shape[0], GAP, 3), 90, np.uint8)
    row = tiles[0]
    for tile in tiles[1:]:
        row = np.hstack((row, filler, tile))
    return row


def save_sheet(rows, path):
    if len(rows) == 1:
        sheet = rows[0]
    else:
        filler = np.full((GAP, rows[0].shape[1], 3), 90, np.uint8)
        parts = [rows[0]]
        for row in rows[1:]:
            parts.extend((filler, row))
        sheet = np.vstack(parts)
    if sheet.shape[1] > SHEET_MAX_W:
        scale = SHEET_MAX_W / sheet.shape[1]
        im = Image.fromarray(sheet)
        im = im.resize((SHEET_MAX_W, max(1, int(round(sheet.shape[0] * scale)))),
                       Image.Resampling.LANCZOS)
        sheet = np.asarray(im)
    Image.fromarray(sheet).save(path)
    return sheet


def safe_name(text):
    return ''.join(c if c.isalnum() or c in '-_.' else '_' for c in str(text)) or 'frame'


def render_frames(frames, indices, out_dir, character):
    assets = load_character(character)
    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    rows, times = [], []
    for idx in indices:
        frame = frames[idx]
        start = time.perf_counter()
        row = render_row(frame, assets)
        elapsed = time.perf_counter() - start
        label = str(frame.get('label', 'frame'))
        Image.fromarray(row).save(out_dir / ('%d_%s.png' % (idx, safe_name(label))))
        rows.append(row)
        times.append(elapsed)
        print('frame %-3d %-18s %6.2f s   %.2f M tri-view' %
              (idx, label, elapsed, sum(len(assets[p]['index']) for p in PARTS) * 4 / 1e6))
    sheet = save_sheet(rows, out_dir / 'sheet.png')
    print('wrote %d frame PNG(s) + sheet.png %dx%d to %s'
          % (len(rows), sheet.shape[1], sheet.shape[0], out_dir))
    if times:
        print('timing: %s (mean %.2f s/frame, %.1f tris drawn per view)'
              % (', '.join('%.2f' % t for t in times), sum(times) / len(times),
                 sum(len(assets[p]['index']) for p in PARTS)))
    return times


# --------------------------------------------------------------------- bind pose
def bind_frame(character):
    assets = load_character(character)
    body = assets['body']
    spine = body['joint_names'].index('center_spine03_jnt')
    camera = body['joint_names'].index('camera_jnt')
    arm_frame = flatten44(promote(body['model_bind'][spine][None])[0])
    frame = {'label': 'bind', 't': 0.0}
    for part in PARTS:
        frame[part] = [flatten44(promote(m[None])[0]) for m in assets[part]['model_bind']]
        if part != 'body':
            frame[part + '_frame'] = arm_frame
    frame['eye'] = dict(position=body['model_bind'][camera, :, 3].tolist(),
                        rotation_xyzw=[0.0, 0.0, 0.0, 1.0])
    markers = []
    for name in ('origin', 'camera_jnt', 'center_head_jnt', 'left_ankle_jnt', 'right_ankle_jnt'):
        i = body['joint_names'].index(name)
        markers.append(dict(name=name, position=body['model_bind'][i, :, 3].tolist()))
    frame['markers'] = markers
    return frame, assets


def selfcheck(character, out_dir=None, limit=1e-5):
    frame, assets = bind_frame(character)
    errors = {}
    for part in PARTS:
        mats = np.stack([mat4(f) for f in frame[part]])
        verts, normals = skin(assets[part], mats)
        ref = assets[part]['positions']
        errors[part] = float(np.abs(verts - ref).max())
        if part == 'body':
            world = verts
        else:
            f = mat4(frame[part + '_frame'])
            world = verts @ f[:3, :3].T + f[:3, 3]
        errors[part + '_world_radius'] = float(np.linalg.norm(world, axis=1).max())
        if not np.isfinite(normals).all():
            raise ValueError('non-finite bind normals for %s' % part)
    worst = max(errors[p] for p in PARTS)
    print('selfcheck %s: max |skinned bind - npz bind positions| = %.3e m '
          '(body %.3e, left_arm %.3e, right_arm %.3e) limit %.1e -> %s'
          % (character, worst, errors['body'], errors['left_arm'], errors['right_arm'],
             limit, 'PASS' if worst < limit else 'FAIL'))
    print('  arm model->world bind radius: left %.3f m, right %.3f m (spine03 frame)'
          % (errors['left_arm_world_radius'], errors['right_arm_world_radius']))
    if worst >= limit:
        raise SystemExit('selfcheck failed: %.3e >= %.1e' % (worst, limit))
    out_dir = Path(out_dir) if out_dir else DEFAULT_OUT / 'selfcheck'
    render_frames([frame], [0], out_dir, character)
    return worst


# ------------------------------------------------------------- hand close-ups
# Eight lit cameras around the right hand (frame['hand'] from tools/hand_pose_lab.cpp), the solver's held shape drawn
# translucent over the depth buffer, and the finger joint chains as coloured overlay (dim where the skin hides them).
HAND_PARTS = ('right_arm',)
HAND_DIST, HAND_HFOV, HAND_TILE, HAND_CROP = 0.35, math.radians(50.0), 480, 0.42
FINGER_NAMES = ('thumb', 'index', 'medium', 'ring', 'pinky')
FINGER_COLOURS = np.array([[1.00, 0.30, 0.25], [0.30, 1.00, 0.40], [0.35, 0.65, 1.00],
                           [1.00, 0.90, 0.20], [1.00, 0.40, 0.90]], np.float32)


def hand_views(hand):
    """Cameras 0.35 m out along a side vector of the hand frame: fingers f, palm-in n, pinky->index t (thumb side)."""
    f, n, t = _unit(hand['fingers']), _unit(hand['normal']), _unit(hand['thumb'])
    z = np.array([0.0, 0.0, 1.0], np.float32)
    sides = [('fingers (front)', f), ('back of hand', -n), ('thumb side', t), ('little-finger side', -t),
             ('above', z), ('below', -z), ('front-thumb-45 (palm+thumb)', _unit(n + t, n)), ('front-below-45 (palm-thumb)', _unit(n - t, n))]
    center, views = np.asarray(hand['center'], np.float32), []
    for name, side in sides:
        side = _unit(side)
        look = -side
        # (a look along the world up, or along f - a hand pointing straight down seen from its fingers - takes the next reference that is not parallel)
        ref = next((c for c in (z, f, t, n) if abs(float(look @ _unit(c))) < 0.9), z)
        right = _unit(np.cross(look, ref), np.cross(look, np.array([1.0, 0.0, 0.0], np.float32)))
        up = np.cross(right, look)
        view = View(name, HAND_TILE, HAND_TILE, center + side * HAND_DIST, look, right, up, True, hfov=HAND_HFOV)
        view.light = _unit(look * 0.8 + up * 0.5 - right * 0.3)   # camera-side key light: every angle is lit
        views.append(view)
    return views


def crop_part(part, center, radius):
    """Keep the triangles with a vertex within radius of center (the rest never reach a close-up)."""
    near = np.linalg.norm(part['verts'] - center, axis=1) < radius
    kept, surfaces, start = [], [], 0
    for s0, count, tex in part['surfaces']:
        tri = part['index'][s0:s0 + count]
        tri = tri[near[tri].any(axis=1)]
        kept.append(tri)
        surfaces.append((start, len(tri), tex))
        start += len(tri)
    return dict(part, index=np.concatenate(kept) if kept else part['index'][:0], surfaces=surfaces)


def flat_mesh(pts, tris):
    """Flat-shaded triangles: every triangle its own three vertices and its face normal."""
    pts = np.asarray(pts, np.float64)
    tris = np.asarray(tris, np.intp).reshape(-1, 3)
    face = np.cross(pts[tris[:, 1]] - pts[tris[:, 0]], pts[tris[:, 2]] - pts[tris[:, 0]])
    face /= np.maximum(np.linalg.norm(face, axis=1, keepdims=True), 1e-12)
    verts = pts[tris].reshape(-1, 3).astype(np.float32)
    return verts, np.repeat(face, 3, axis=0).astype(np.float32), np.arange(len(verts), dtype=np.intp).reshape(-1, 3)


def shape_mesh(shape):
    """Capsule, box or hull from the lab JSON -> (verts, normals, triangles)."""
    if shape['kind'] in ('hull', 'mesh'):
        if shape['kind'] == 'mesh':
            return flat_mesh(shape['points'], shape['triangles'])
        from scipy.spatial import ConvexHull  # the real shape's hull (prop_hull.hpp): its corners pushed out by the convex radius
        pts = np.asarray(shape['points'], np.float64)
        hull = ConvexHull(pts)
        tris = [s if np.dot(np.cross(pts[s[1]] - pts[s[0]], pts[s[2]] - pts[s[0]]), e[:3]) > 0 else s[[0, 2, 1]]
                for s, e in zip(hull.simplices, hull.equations)]
        return flat_mesh(pts, tris)
    c = np.asarray(shape['center'], np.float32)
    if shape['kind'] == 'box':
        axes = [_unit(a) for a in shape['axes']]
        half = np.asarray(shape['half'], np.float32)
        verts, norms, tris = [], [], []
        for i in range(3):
            j, k = (i + 1) % 3, (i + 2) % 3
            for sign in (-1.0, 1.0):
                base = len(verts)
                for a, b in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
                    verts.append(c + axes[i] * sign * half[i] + axes[j] * a * half[j] + axes[k] * b * half[k])
                    norms.append(axes[i] * sign)
                tris += [(base, base + 1, base + 2), (base, base + 2, base + 3)]
        return np.array(verts, np.float32), np.array(norms, np.float32), np.array(tris, np.intp)
    if shape['kind'] == 'cylinder':  # flat-ended: a wall and two caps (the rim's rounding is a few millimetres: not drawn)
        a = _unit(shape['axis'])
        u = _unit(np.cross(a, [1.0, 0.0, 0.0] if abs(float(a[0])) < 0.9 else [0.0, 1.0, 0.0]))
        w = np.cross(a, u)
        half, rad, seg = float(shape['half']), float(shape['radius']), 40
        verts, norms, tris = [], [], []
        angles = np.linspace(0.0, 2 * math.pi, seg, endpoint=False)
        radial = [u * math.cos(p) + w * math.sin(p) for p in angles]
        for offset in (-half, half):            # the wall: two rings of radial normals
            for n in radial:
                verts.append(c + a * offset + n * rad)
                norms.append(n)
        for p in range(seg):
            q = (p + 1) % seg
            tris += [(p, seg + p, q), (q, seg + p, seg + q)]
        for sign in (-1.0, 1.0):                 # the caps: a fan from the centre
            base = len(verts)
            verts.append(c + a * sign * half)
            norms.append(a * sign)
            for n in radial:
                verts.append(c + a * sign * half + n * rad)
                norms.append(a * sign)
            for p in range(seg):
                tris.append((base, base + 1 + p, base + 1 + (p + 1) % seg))
        return np.array(verts, np.float32), np.array(norms, np.float32), np.array(tris, np.intp)
    if shape['kind'] != 'capsule':
        raise ValueError('unknown shape kind %r' % shape['kind'])
    a = _unit(shape['axis'])
    u = _unit(np.cross(a, [1.0, 0.0, 0.0] if abs(float(a[0])) < 0.9 else [0.0, 1.0, 0.0]))
    w = np.cross(a, u)
    half, rad, seg, ring = float(shape['half']), float(shape['radius']), 28, 10
    rad_end = float(shape.get('radius_end', rad))  # a round cone (a profiled shaft's piece): the radius at +axis; `radius` is the one at -axis
    rows = [(t, half, rad_end) for t in np.linspace(0.0, math.pi / 2, ring)] + \
           [(t, -half, rad) for t in np.linspace(math.pi / 2, math.pi, ring)]
    verts, norms = [], []
    for theta, offset, radius in rows:
        for phi in np.linspace(0.0, 2 * math.pi, seg, endpoint=False):
            n = (u * math.cos(phi) + w * math.sin(phi)) * math.sin(theta) + a * math.cos(theta)
            verts.append(c + a * offset + n * radius)
            norms.append(n)
    tris = []
    for r in range(len(rows) - 1):
        for p in range(seg):
            q = (p + 1) % seg
            a0, a1, b0, b1 = r * seg + p, r * seg + q, (r + 1) * seg + p, (r + 1) * seg + q
            tris += [(a0, b0, a1), (a1, b0, b1)]
    return np.array(verts, np.float32), np.array(norms, np.float32), np.array(tris, np.intp)


def draw_shapes(color, depth, view, shapes):
    """Translucent shapes over the opaque depth buffer; convex, so back faces then front faces blend correctly."""
    meshes = [dict(kind='mesh', points=s['mesh']['points'], triangles=s['mesh']['triangles'], color=[0.80, 0.62, 0.38], alpha=0.85)
              for s in shapes or [] if 'mesh' in s]  # the prop's render mesh (the thing the player sees), under its hull
    for shape in meshes + list(shapes or []):
        verts, norms, tris = shape_mesh(shape)
        x, y, z = view.project(verts)
        pt = np.stack((x, y, z), axis=1).astype(np.float32)
        away = np.einsum('tj,tj->t', norms[tris].mean(axis=1), verts[tris].mean(axis=1) - view.origin) > 0.0
        tint = (shape.get('color', [1.0, 1.0, 1.0]), shape.get('alpha', 0.30))
        for facing in (True, False):
            for t in np.nonzero(away == facing)[0]:
                if (pt[tris[t], 2] <= NEAR).any():
                    continue
                fill_triangle(color, depth, view, None, pt[tris[t]], None, norms[tris[t]], tint=tint)


def finger_points(frame, assets):
    """World joint positions per finger: metacarpal (fingers) .. last joint .. end."""
    names = assets['right_arm']['joint_names']
    mats = np.asarray(frame['right_arm'], np.float32).reshape(-1, 16)
    base = mat4(frame['right_arm_frame'])
    out = []
    for i, finger in enumerate(FINGER_NAMES):
        wanted = ['right_%s_0%d_jnt' % (finger, k) for k in ((1, 2, 3) if i == 0 else (1, 2, 3, 4))]
        wanted.append('right_%s_end_jnt' % finger)
        out.append(np.array([(base @ mat4(mats[names.index(n)]))[:3, 3] for n in wanted], np.float32))
    return out


def draw_overlay(color, depth, view, chains):
    h, w = color.shape[:2]
    for i, pts in enumerate(chains):
        samples = np.concatenate([a + (b - a) * np.linspace(0.0, 1.0, 16, dtype=np.float32)[:, None]
                                  for a, b in zip(pts[:-1], pts[1:])])
        for cloud, size in ((samples, 1), (pts, 3)):
            sx, sy, sz = view.project(cloud)
            for x, y, z in zip(sx, sy, sz):
                xi, yi = int(round(float(x))), int(round(float(y)))
                if not (size <= xi < w - size and size <= yi < h - size) or z <= NEAR:
                    continue
                shown = z <= depth[yi, xi] + 0.008
                patch = color[yi - size + 1:yi + size, xi - size + 1:xi + size]
                patch[...] = FINGER_COLOURS[i] if shown else patch * 0.6 + FINGER_COLOURS[i] * 0.4


def render_hand_tile(frame, posed, view, chains):
    color = np.full((view.h, view.w, 3), BACKGROUND, np.float32)
    color[..., 2] += np.linspace(0.0, 0.06, view.h, dtype=np.float32)[:, None]
    depth = np.full((view.h, view.w), np.inf, np.float32)
    for part in posed.values():
        draw_mesh(color, depth, view, part)
    seen = float(np.isfinite(depth).mean())
    draw_shapes(color, depth, view, frame.get('shapes'))
    draw_overlay(color, depth, view, chains)
    tile = label_tile(np.clip(color * 255.0, 0, 255).astype(np.uint8), str(frame.get('label', 'frame')), view.name)
    return tile, seen


def profile_image(frame, posed, chains, size=560):
    """Two flat diagrams of the posed finger chains (colour) against the solver's planned chains (white) and the held shape:
    looking along the hinge axis (fingers right, palm side up: every curl is a true angle) and onto the palm (sideways drift)."""
    hand = frame['hand']
    f, n, t = (_unit(hand[k]) for k in ('fingers', 'normal', 'thumb'))
    c = np.asarray(hand['center'], np.float32)
    verts = np.concatenate([p['verts'] for p in posed.values()])
    verts = verts[np.linalg.norm(verts - c, axis=1) < 0.30]
    planned = [np.asarray(p, np.float32) for p in frame.get('planned', [])]
    scale = size / 0.30
    panels = []
    for title, up in (('profile (along the hinge axis): fingers right, palm side up', n),
                      ('palm view (along the palm normal): fingers right, thumb side up', t)):
        im = Image.new('RGB', (size, size), (24, 24, 30))
        d = ImageDraw.Draw(im)

        def px(p, up=up):
            p = np.asarray(p, np.float32) - c
            return np.stack(((p @ f) * scale + size * 0.42, size * 0.5 - (p @ up) * scale), axis=-1)
        for x, y in px(verts):
            d.point((float(x), float(y)), fill=(120, 105, 98))
        for shape in frame.get('shapes') or []:
            colour = tuple(int(v * 255) for v in shape.get('color', [1, 1, 1]))
            if shape['kind'] in ('capsule', 'cylinder'):
                a = _unit(shape['axis'])
                radii = [float(shape['radius']) * scale, float(shape.get('radius_end', shape['radius'])) * scale]  # -axis end, +axis end
                ends = px(np.array([np.asarray(shape['center']) + a * s * float(shape['half']) for s in (-1, 1)]))
                side = ends[1] - ends[0]
                side = np.array([-side[1], side[0]]) / max(float(np.linalg.norm(side)), 1e-6)
                for e, r in zip(ends, radii):
                    d.ellipse((e[0] - r, e[1] - r, e[0] + r, e[1] + r), outline=colour, width=2)
                for s in (-1, 1):
                    d.line([tuple(ends[0] + s * side * radii[0]), tuple(ends[1] + s * side * radii[1])], fill=colour, width=2)
            elif shape['kind'] == 'hull':  # a real-shape piece: its outline in this view
                from scipy.spatial import ConvexHull
                q = px(np.asarray(shape['points'], np.float32))
                ring = [tuple(q[i]) for i in ConvexHull(q).vertices]
                d.line(ring + ring[:1], fill=colour, width=2)
            else:
                ax, hf = [_unit(v) for v in shape['axes']], np.asarray(shape['half'], np.float32)
                cs = np.array([np.asarray(shape['center']) + ax[0] * i * hf[0] + ax[1] * j * hf[1] + ax[2] * k * hf[2]
                               for i in (-1, 1) for j in (-1, 1) for k in (-1, 1)])
                q = px(cs)
                for a_, b_ in ((0, 1), (2, 3), (4, 5), (6, 7), (0, 2), (1, 3), (4, 6), (5, 7), (0, 4), (1, 5), (2, 6), (3, 7)):
                    d.line([tuple(q[a_]), tuple(q[b_])], fill=colour, width=2)
        for i, pts in enumerate(planned):
            q = px(pts)
            d.line([tuple(p) for p in q], fill=(235, 235, 235), width=1)
            for p in q:
                d.rectangle((p[0] - 3, p[1] - 3, p[0] + 3, p[1] + 3), outline=(235, 235, 235))
        for i, pts in enumerate(chains):
            colour = tuple(int(v * 255) for v in FINGER_COLOURS[i])
            q = px(pts)
            d.line([tuple(p) for p in q], fill=colour, width=3)
            for p in q:
                d.ellipse((p[0] - 4, p[1] - 4, p[0] + 4, p[1] + 4), fill=colour)
        d.text((8, 6), title, fill=(255, 255, 255), font=_font(16))
        d.text((8, size - 24), 'colour = posed joints (metacarpal .. tip), white squares = solver plan, outline = held shape',
               fill=(200, 200, 200), font=_font(13))
        panels.append(np.asarray(im))
    return np.hstack((panels[0], np.full((size, GAP, 3), 90, np.uint8), panels[1]))


_HAND = {}


def _hand_init(frames_path, character):
    doc = json.loads(Path(frames_path).read_text(encoding='utf-8-sig'))
    _HAND.update(frames=doc['frames'], assets=load_character(character, HAND_PARTS), cache={})


def _hand_tile(task):
    index, view_index = task
    cache = _HAND['cache']
    if index not in cache:
        frame = _HAND['frames'][index]
        center = np.asarray(frame['hand']['center'], np.float32)
        posed = {k: crop_part(p, center, HAND_CROP) for k, p in pose_frame(frame, _HAND['assets']).items()}
        cache[index] = (posed, hand_views(frame['hand']), finger_points(frame, _HAND['assets']))
    posed, views, chains = cache[index]
    if view_index == len(views):                       # one extra task per frame: the flat profile diagram
        return task, profile_image(_HAND['frames'][index], posed, chains)
    return task, render_hand_tile(_HAND['frames'][index], posed, views[view_index], chains)


def hand_montage(frame, tiles):
    tiles = [t[0] for t in tiles]
    gap = np.full((HAND_TILE, GAP, 3), 90, np.uint8)
    rows = []
    for r in (tiles[:4], tiles[4:]):
        row = r[0]
        for tile in r[1:]:
            row = np.hstack((row, gap, tile))
        rows.append(row)
    sheet = np.vstack((rows[0], np.full((GAP, rows[0].shape[1], 3), 90, np.uint8), rows[1]))
    head = Image.new('RGB', (sheet.shape[1], 34), (28, 28, 32))
    draw = ImageDraw.Draw(head)
    draw.text((8, 6), str(frame.get('title', frame.get('label', ''))), fill=(255, 255, 255), font=_font(18))
    x = sheet.shape[1] - 5 * 92 - 8
    for name, c in zip(FINGER_NAMES, FINGER_COLOURS):
        draw.rectangle((x, 11, x + 12, 23), fill=tuple(int(v * 255) for v in c))
        draw.text((x + 17, 8), name, fill=(220, 220, 220), font=_font(15))
        x += 92
    return np.vstack((np.asarray(head), sheet))


def write_index(path, tag, frames, indices):
    import html
    parts = ['<!doctype html><meta charset="utf-8"><title>hand lab %s</title>' % html.escape(tag),
             '<style>body{background:#161618;color:#ddd;font:14px/1.4 system-ui;margin:16px}'
             'img{max-width:100%;display:block;margin:6px 0}pre{background:#0e0e10;padding:8px;overflow:auto}</style>',
             '<h1>hand lab: %s</h1>' % html.escape(tag),
             '<p>Eight views per case, 0.35 m from the right hand. Top row: fingers front, back of hand, thumb side, '
             'little-finger side. Bottom row: above, below, front-thumb-45 (palm side and thumb side: the sim orbit view of that name), '
             'front-below-45 (palm side and the little-finger side). Joint chains: thumb red, index green, '
             "middle blue, ring yellow, pinky magenta (dim where the skin hides them). The held shape is the solver's capsule "
             '(or slab), translucent.</p>']
    for i in indices:
        f = frames[i]
        name = safe_name(f['label'])
        parts.append('<h2>%s</h2><a href="%s-views.png"><img src="%s-views.png"></a>'
                     '<a href="%s-profile.png"><img src="%s-profile.png"></a><pre>%s</pre>'
                     % (html.escape(str(f.get('title', f['label']))), name, name, name, name,
                        html.escape('\n'.join(f.get('report', [])))))
    Path(path).write_text('\n'.join(parts), encoding='utf-8')


def render_hand_views(frames_path, indices, out_dir, workers):
    from concurrent.futures import ProcessPoolExecutor
    doc = json.loads(Path(frames_path).read_text(encoding='utf-8-sig'))
    frames, out_dir = doc['frames'], Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    tasks = [(i, v) for i in indices for v in range(9)]     # 8 camera views + the profile diagram
    start = time.perf_counter()
    if workers > 1:
        with ProcessPoolExecutor(min(workers, len(tasks)), initializer=_hand_init,
                                 initargs=(str(frames_path), doc['character'])) as pool:
            done = dict(pool.map(_hand_tile, tasks))
    else:
        _hand_init(str(frames_path), doc['character'])
        done = dict(map(_hand_tile, tasks))
    for i in indices:
        tiles = [done[(i, v)] for v in range(8)]
        for v, (_, seen) in enumerate(tiles):
            if seen < 0.004:
                print('warning: %s view %d shows almost no hand (%.2f%% of pixels)' % (frames[i]['label'], v, seen * 100))
        Image.fromarray(hand_montage(frames[i], tiles)).save(out_dir / ('%s-views.png' % safe_name(frames[i]['label'])))
        Image.fromarray(done[(i, 8)]).save(out_dir / ('%s-profile.png' % safe_name(frames[i]['label'])))
    write_index(out_dir / 'index.html', out_dir.name, frames, indices)
    print('wrote %d montage(s) + index.html to %s in %.1f s' % (len(indices), out_dir, time.perf_counter() - start))


# -------------------------------------------------------------------------- main
def parse_indices(text, count):
    if not text:
        return list(range(count))
    try:
        idx = [int(x) for x in text.replace(' ', '').split(',') if x != '']
    except ValueError:
        raise SystemExit('--frames wants comma-separated integers, got %r' % text)
    for i in idx:
        if not 0 <= i < count:
            raise SystemExit('--frames index %d outside 0..%d' % (i, count - 1))
    return idx


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('frames_json', nargs='?', type=Path, help='solver output JSON')
    parser.add_argument('out_dir', nargs='?', type=Path, help='output directory')
    parser.add_argument('--frames', help='comma-separated frame indices, e.g. 0,3,5')
    parser.add_argument('--hand-views', action='store_true',
                        help='right-hand close-ups: 8 lit views per frame (needs the hand_pose_lab JSON), plus index.html')
    parser.add_argument('--workers', type=int, default=min(8, os.cpu_count() or 1), help='--hand-views processes')
    parser.add_argument('--selfcheck', metavar='CHARACTER',
                        help='verify bind-pose skinning for emily|corvo and render it')
    parser.add_argument('--out-dir', dest='out_dir_flag', type=Path,
                        help='output directory (selfcheck default: artifacts/fullbody-ik-render/selfcheck)')
    args = parser.parse_args(argv)
    if args.selfcheck:
        selfcheck(args.selfcheck, args.out_dir_flag or args.out_dir)
        return 0
    if args.frames_json is None or args.out_dir is None:
        parser.error('need <frames.json> <out_dir> (or --selfcheck CHARACTER)')
    source = args.frames_json
    if not source.is_file():
        raise SystemExit('no such frames JSON: %s' % source)
    doc = json.loads(source.read_text(encoding='utf-8-sig'))
    frames = doc['frames']
    if not frames:
        raise SystemExit('frames JSON has no frames')
    indices = parse_indices(args.frames, len(frames))
    out_dir = args.out_dir_flag or args.out_dir
    if args.hand_views:
        render_hand_views(source, indices, out_dir, args.workers)
        return 0
    print('rendering %s: %d of %d frame(s) -> %s'
          % (doc.get('character', '?'), len(indices), len(frames), out_dir))
    render_frames(frames, indices, out_dir, doc['character'])
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
