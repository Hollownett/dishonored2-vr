"""Close-up renders of both hands for solver frames (finger posing review).

Usage: python tools/render_hand_closeup.py <frames.json> <out.png> [--frames 0,1]
Each row: per hand, views from the thumb side, the back of the hand and the palm.
"""
import argparse
import json
from pathlib import Path
import numpy as np
from PIL import Image
import render_body_ik as r


def hand_row(frame, assets):
    posed = r.pose_frame(frame, assets)
    tiles = []
    for side, part in enumerate(('left_arm', 'right_arm')):
        names = assets[part]['joint_names']
        verts = posed[part]['verts']
        # Hand vertices: everything near the hand_attachment marker.
        center = np.asarray(frame['markers'][side]['position'], np.float32)
        joints = [np.asarray(j, np.float32).reshape(4, 4).T for j in frame[part]]
        frame_m = np.asarray(frame[part + '_frame'], np.float32).reshape(4, 4).T
        wrist = (frame_m @ joints[names.index(('left' if side == 0 else 'right') + '_wrist_jnt')])
        axes = wrist[:3, :3] / np.linalg.norm(wrist[:3, :3], axis=0)
        along = axes[:, 0]  # wrist X runs along the forearm/hand
        palm = axes[:, 1]
        for name, fwd in (('thumb side', np.cross(along, palm)), ('back', palm), ('palm', -palm)):
            view = r.View(name, 420, 420, center, fwd, *_basis(fwd), False, span=.32)
            color = np.full((view.h, view.w, 3), r.BACKGROUND, np.float32)
            depth = np.full((view.h, view.w), np.inf, np.float32)
            r.draw_mesh(color, depth, view, posed[part])
            tiles.append(r.label_tile(np.clip(color * 255, 0, 255).astype(np.uint8),
                                      '%s %s' % (frame['label'], part.split('_')[0]), name))
    gap = np.full((420, r.GAP, 3), 90, np.uint8)
    row = tiles[0]
    for t in tiles[1:]:
        row = np.hstack((row, gap, t))
    return row


def _basis(fwd):
    fwd = fwd / np.linalg.norm(fwd)
    ref = np.array([0, 0, 1.], np.float32) if abs(fwd[2]) < .9 else np.array([1, 0, 0.], np.float32)
    right = np.cross(fwd, ref); right /= np.linalg.norm(right)
    return right, np.cross(right, fwd)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('frames_json', type=Path)
    ap.add_argument('out', type=Path)
    ap.add_argument('--frames')
    a = ap.parse_args()
    data = json.loads(a.frames_json.read_text())
    assets = r.load_character(data.get('character', 'corvo'))
    frames = data['frames']
    pick = [int(x) for x in a.frames.split(',')] if a.frames else range(len(frames))
    rows = [hand_row(frames[i], assets) for i in pick]
    Image.fromarray(np.vstack(rows)).save(a.out)
    print('wrote', a.out)


if __name__ == '__main__':
    main()
