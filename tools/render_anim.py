"""Render a dense solver scenario (30 fps frames JSON) to an MP4 for judging motion.

Usage: python tools/render_anim.py <scenario.json> <out.mp4> [--jobs N]
Each video frame: three-quarter and side views with a camera that smoothly tracks the pelvis,
plus the first-person eye view; labels show time and native ownership (legs/torso/arms).
"""
import argparse
import json
import math
import subprocess
import tempfile
from multiprocessing import Pool
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw
import render_body_ik as r

SPAN = 2.5
_assets = None


def _init(character):
    global _assets
    _assets = r.load_character(character)


def _view(name, fwd, center):
    return r.ortho_view(name, np.asarray(fwd, np.float32), center, SPAN)


def _frame(args):
    index, frame, center, out_dir, title = args
    posed = r.pose_frame(frame, _assets)
    az, el = math.radians(35.0), math.radians(12.0)
    three_quarter = -np.array([math.cos(el) * math.cos(az), math.cos(el) * math.sin(az), math.sin(el)], np.float32)
    views = [_view('three quarter', three_quarter, center), _view('side', [0, -1, 0], center),
             r.eye_view(frame['eye'])]
    tiles = []
    for view in views:
        color = np.full((view.h, view.w, 3), r.BACKGROUND, np.float32)
        color[..., 2] += np.linspace(0.0, 0.06, view.h, dtype=np.float32)[:, None]
        depth = np.full((view.h, view.w), np.inf, np.float32)
        for part in r.PARTS:
            r.draw_mesh(color, depth, view, posed[part])
        r.draw_ground(color, depth, view)
        r.draw_markers(color, view, frame.get('markers'))
        tiles.append(np.clip(color * 255.0, 0, 255).astype(np.uint8))
    gap = np.full((tiles[0].shape[0], r.GAP, 3), 90, np.uint8)
    row = np.hstack([tiles[0], gap, tiles[1], gap, tiles[2]])
    img = Image.fromarray(row[:row.shape[0] // 2 * 2, :row.shape[1] // 2 * 2])
    d = ImageDraw.Draw(img)
    owned = frame.get('owned', [0, 0, 0, 0])
    text = '%s   t=%.2fs   native: legs %.2f  torso %.2f  L arm %.2f  R arm %.2f' % (title, frame['t'], *owned)
    d.rectangle([0, 0, img.width, 30], fill=(20, 20, 26))
    d.text((10, 6), text, fill=(235, 235, 235), font=r._font(18))
    if max(owned) > 0:
        d.rectangle([img.width - 22, 6, img.width - 6, 22], fill=(220, 120, 40))  # native-owned indicator
    img.save(Path(out_dir) / ('%05d.png' % index))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('scenario', type=Path)
    ap.add_argument('out', type=Path)
    ap.add_argument('--jobs', type=int, default=14)
    a = ap.parse_args()
    data = json.loads(a.scenario.read_text())
    frames = data['frames']
    # Camera target: pelvis (body joint 1) averaged over +-0.25 s (centred, so no lag when walking).
    pelvis = np.array([f['body'][1][12:15] for f in frames], np.float32) - np.array([0, 0, .1], np.float32)
    centers = [pelvis[max(0, i - 7):i + 8].mean(axis=0) for i in range(len(frames))]
    title = data.get('scenario', a.scenario.stem)
    with tempfile.TemporaryDirectory(dir=a.out.parent) as tmp:
        jobs = [(i, f, centers[i], tmp, title) for i, f in enumerate(frames)]
        with Pool(a.jobs, initializer=_init, initargs=(data['character'],)) as pool:
            pool.map(_frame, jobs, chunksize=2)
        subprocess.run(['ffmpeg', '-y', '-loglevel', 'error', '-framerate', str(data.get('fps', 30)),
                        '-i', str(Path(tmp) / '%05d.png'), '-c:v', 'libx264', '-pix_fmt', 'yuv420p',
                        '-crf', '20', '-movflags', '+faststart', str(a.out)], check=True)
    print('wrote', a.out, len(frames), 'frames')


if __name__ == '__main__':
    main()
