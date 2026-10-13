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


def load_character(char):
    char = str(char).lower()
    if char not in ('emily', 'corvo'):
        raise ValueError('character must be emily or corvo, got %r' % char)
    assets = {}
    for part in PARTS:
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
    """Skin every part of one frame into world space."""
    posed = {}
    for part in PARTS:
        if part not in frame or frame[part] is None:
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
def _unit(v):
    v = np.asarray(v, np.float32)
    return v / np.linalg.norm(v)


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
def fill_triangle(color, depth, view, tex, p, uvs, nrms):
    """z-buffered, textured, Lambert-lit triangle; p is (3,3) screen x,y,depth."""
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
    sub[hit] = zz[hit]
    h0, h1, h2 = l0[hit][:, None], l1[hit][:, None], l2[hit][:, None]
    n = h0 * nrms[0] + h1 * nrms[1] + h2 * nrms[2]
    n /= np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-9)
    shade = (AMBIENT + (1.0 - AMBIENT) * np.abs(n @ LIGHT))[:, None]
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
    print('rendering %s: %d of %d frame(s) -> %s'
          % (doc.get('character', '?'), len(indices), len(frames), out_dir))
    render_frames(frames, indices, out_dir, doc['character'])
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
