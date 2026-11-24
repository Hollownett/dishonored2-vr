#!/usr/bin/env python3
"""Convert the mod's raw eye captures (NAME.rgba + NAME.json: width, height, format) to PNG for viewing.

    python3 tools/linux/rgba_to_png.py <artifacts dir or .rgba files...> [--out DIR] [--scale 0.5]

Format 28/29 (R8G8B8A8 UNORM / UNORM_SRGB) is copied as is; 87/91 (B8G8R8A8) is swizzled; 24 (R10G10B10A2) is unpacked. Files without a
.json next to them are skipped (the json names the size). Needs Pillow.
"""
import argparse
import json
import sys
from pathlib import Path

from PIL import Image


def convert(rgba: Path, out_dir: Path, scale: float) -> "Path | None":
    meta_path = rgba.with_suffix(".json")
    if not meta_path.is_file():
        return None
    meta = json.loads(meta_path.read_text())
    w, h, fmt = int(meta["width"]), int(meta["height"]), int(meta.get("format", 28))
    data = rgba.read_bytes()
    if len(data) < w * h * 4:
        return None
    data = data[: w * h * 4]
    if fmt in (87, 91):
        img = Image.frombytes("RGBA", (w, h), data, "raw", "BGRA")
    elif fmt == 24:
        import array
        px = array.array("I", data)
        buf = bytearray(w * h * 4)
        for i, v in enumerate(px):
            buf[i * 4] = (v & 0x3FF) >> 2
            buf[i * 4 + 1] = ((v >> 10) & 0x3FF) >> 2
            buf[i * 4 + 2] = ((v >> 20) & 0x3FF) >> 2
            buf[i * 4 + 3] = 255
        img = Image.frombytes("RGBA", (w, h), bytes(buf))
    else:
        img = Image.frombytes("RGBA", (w, h), data)
    img = img.convert("RGB")
    if scale != 1.0:
        img = img.resize((max(1, int(w * scale)), max(1, int(h * scale))))
    out = out_dir / (rgba.stem + ".png")
    img.save(out)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("inputs", nargs="+", type=Path)
    ap.add_argument("--out", type=Path, default=None)
    ap.add_argument("--scale", type=float, default=0.5)
    a = ap.parse_args()
    files = []
    for p in a.inputs:
        files += sorted(p.glob("*.rgba")) if p.is_dir() else [p]
    n = 0
    for f in files:
        out_dir = a.out or f.parent
        out_dir.mkdir(parents=True, exist_ok=True)
        r = convert(f, out_dir, a.scale)
        if r:
            print(r)
            n += 1
    return 0 if n else 1


if __name__ == "__main__":
    sys.exit(main())
