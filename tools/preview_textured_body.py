"""Bind-pose preview of the player body coloured from a decoded diffuse texture (per-face UV centroid).

Usage: python tools/preview_textured_body.py <emily|corvo> <diffuse.png> <out.png>
Checks whether the body mesh UVs fit the shipped visible body material.
"""
import sys
from pathlib import Path
import numpy as np
from PIL import Image
from build_player_asset_pack import write_preview

PACK = Path(__file__).resolve().parents[1] / 'artifacts/fullbody-assets/pack'


def main(char, texture, out):
    z = np.load(PACK / char / 'body.npz')
    tex = np.asarray(Image.open(texture).convert('RGB'), np.float32) / 255.
    tris = z['index'].astype(int)
    uv = z['uv0'][tris].mean(axis=1) % 1.0
    h, w = tex.shape[:2]
    rgb = tex[(uv[:, 1] * (h - 1)).astype(int), (uv[:, 0] * (w - 1)).astype(int)]
    rgb = np.clip(rgb * 1.8 + .05, 0, 1)  # dark albedo; lift for readability
    write_preview(Path(out), [(z['positions'], tris, z['normals'][:, :3], rgb)], char + ' textured')


if __name__ == '__main__':
    main(*sys.argv[1:4])
