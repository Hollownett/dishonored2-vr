"""Post key presses to the Death of the Outsider window only (PostMessage, never global input), then capture it.

    python tools/doto/keys.py shot artifacts/doto-runs/x/now.png
    python tools/doto/keys.py press ENTER DOWN DOWN ENTER --gap 0.6 --shot artifacts/doto-runs/x/after.png
Keys: ENTER ESC SPACE UP DOWN LEFT RIGHT TAB BACK, single letters/digits.
"""
import argparse
import ctypes
import ctypes.wintypes as W
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import doto_sim as d  # noqa: E402  (DPI awareness, find_window, capture)

VK = {"ENTER": 0x0D, "ESC": 0x1B, "SPACE": 0x20, "UP": 0x26, "DOWN": 0x28, "LEFT": 0x25, "RIGHT": 0x27, "TAB": 0x09, "BACK": 0x08}
WM_KEYDOWN, WM_KEYUP = 0x0100, 0x0101
user32 = ctypes.WinDLL("user32")
user32.MapVirtualKeyW.restype = W.UINT


def post(h, name):
    vk = VK.get(name.upper()) or ord(name.upper()[0])
    scan = user32.MapVirtualKeyW(vk, 0)
    ext = 1 << 24 if vk in (0x25, 0x26, 0x27, 0x28) else 0
    user32.PostMessageW(h, WM_KEYDOWN, vk, 1 | (scan << 16) | ext)
    time.sleep(0.08)
    user32.PostMessageW(h, WM_KEYUP, vk, 1 | (scan << 16) | ext | (3 << 30))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd", choices=("press", "shot"))
    ap.add_argument("args", nargs="*")
    ap.add_argument("--gap", type=float, default=0.8)
    ap.add_argument("--shot")
    a = ap.parse_args()
    h = d.find_window()
    if not h:
        sys.exit("no Death of the Outsider window")
    if a.cmd == "shot":
        print(d.capture(a.args[0]))
        return
    for k in a.args:
        post(h, k)
        time.sleep(a.gap)
    if a.shot:
        time.sleep(0.5)
        print(d.capture(a.shot))


if __name__ == "__main__":
    main()
