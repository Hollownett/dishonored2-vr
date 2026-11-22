"""Short clip of the game window (the VR mirror) for sim runs: grabs the window's screen rectangle at --fps for
--seconds and writes an animated GIF or WebP (by --out extension; and the frames, with --frames). Read-only: it only
copies screen pixels.

    python tools/doto/burst.py --out artifacts/doto-runs/play15/walk.webp --seconds 8 --fps 8 --width 800

As a doto_sim.py hook: --during "30 tools/doto/burst.py --out artifacts/doto-runs/play15/walk.webp"
(the hook blocks the runner's loop while it records and encodes, so window shots pause meanwhile; its log line
is written when it ends).
"""
import argparse
import ctypes
import ctypes.wintypes as W
import time
from pathlib import Path

from PIL import Image, ImageGrab

user32 = ctypes.windll.user32
try:
    ctypes.windll.shcore.SetProcessDpiAwareness(2)  # physical pixels, as doto_sim.py captures
except (AttributeError, OSError):
    user32.SetProcessDPIAware()

TITLES = ("Death of the Outsider", "Dishonored")


def find_window():
    found = []
    proto = ctypes.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)

    def cb(h, _):
        if not user32.IsWindowVisible(h):
            return True
        n = user32.GetWindowTextLengthW(h)
        if n <= 0:
            return True
        buf = ctypes.create_unicode_buffer(n + 1)
        user32.GetWindowTextW(h, buf, n + 1)
        title = buf.value
        if any(t in title for t in TITLES) and "VR Launcher" not in title:
            found.append((0 if TITLES[0] in title else 1, h))
        return True

    user32.EnumWindows(proto(cb), 0)
    return sorted(found)[0][1] if found else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--seconds", type=float, default=8)
    ap.add_argument("--fps", type=float, default=8)
    ap.add_argument("--width", type=int, default=800, help="GIF width in pixels (height keeps the aspect)")
    ap.add_argument("--frames", action="store_true", help="also keep the full-size PNG frames")
    a = ap.parse_args()
    h = find_window()
    if not h:
        print("no game window")
        return 1
    out = Path(a.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    # Grab only while recording (resizing/encoding per frame halved the rate to ~5 fps); encode afterwards.
    raw, step = [], 1.0 / a.fps
    t0 = time.perf_counter()
    for i in range(int(a.seconds * a.fps)):
        rect = W.RECT()
        user32.GetWindowRect(h, ctypes.byref(rect))
        if rect.right <= rect.left or rect.bottom <= rect.top:
            break
        img = ImageGrab.grab(bbox=(rect.left, rect.top, rect.right, rect.bottom), all_screens=True)
        factor = max(1, img.width // a.width)   # cheap box reduction keeps memory bounded (180 full frames ~1 GB)
        raw.append(img.reduce(factor) if factor > 1 else img)
        wait = t0 + (i + 1) * step - time.perf_counter()
        if wait > 0:
            time.sleep(wait)
    elapsed = time.perf_counter() - t0
    if not raw:
        print("no frames")
        return 1
    real_fps = len(raw) / max(1e-3, elapsed)
    frames = []
    for i, img in enumerate(raw):
        if a.frames:
            img.save(out.with_name(f"{out.stem}-{i:03d}.png"))
        w = a.width
        f = img.resize((w, max(1, round(img.height * w / img.width))), Image.LANCZOS)
        frames.append(f.convert("P", palette=Image.ADAPTIVE, colors=192) if out.suffix.lower() == ".gif" else f.convert("RGB"))
    extra = dict(optimize=True) if out.suffix.lower() == ".gif" else dict(quality=55, method=4)
    frames[0].save(out, save_all=True, append_images=frames[1:], duration=round(1000 / real_fps), loop=0, **extra)
    print(f"{out}: {len(frames)} frames at {real_fps:.1f} fps, {out.stat().st_size // 1024} KiB")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
