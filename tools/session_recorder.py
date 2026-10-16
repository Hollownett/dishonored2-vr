"""Record a headset play session for debugging: the game window (the VR mirror) as MP4 plus the mod's live status.

    python tools/session_recorder.py [--fps 10] [--minutes 30] [--out artifacts/recordings]

Captures with PrintWindow (works while the window is covered, never takes focus). Every frame carries the
GetTickCount64 clock, the same clock as artifacts/native-combat.csv and xr-live-status.json, so a frame can be matched
to a swing, hit or prompt. Stops when the game exits or the time is up; writes <stamp>.mp4 and <stamp>-status.jsonl."""
import argparse
import ctypes
import json
import os
import time
from ctypes import wintypes

import cv2
import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
user32, gdi32, kernel32 = ctypes.windll.user32, ctypes.windll.gdi32, ctypes.windll.kernel32
kernel32.GetTickCount64.restype = ctypes.c_uint64
KEEP = ('logic_mask', 'session_state', 'combat', 'contact_hits', 'npc_blade_contacts', 'impact_fx', 'tip_speed', 'slash_speed',
        'world_prompt_frames', 'prompt_shows_hides', 'takedown_flips', 'takedown_hints', 'overlay_hidden', 'left_equip',
        'hand_weapon_bits', 'weapon_collision', 'published_move', 'wheel_zone',
        'grip_tracked_hands', 'native_arm_pose_fallbacks', 'full_body', 'arm_hidden', 'arm_shows', 'arm_hides_skipped',
        'held_object', 'sword_grip', 'interact', 'look_uses', 'hip_frames', 'ragdoll_tows', 'block', 'gesture_blocks')  # arm drop: tracking loss vs native ownership vs solver


def game_window():
    found = []

    @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def visit(hwnd, _):
        t = ctypes.create_unicode_buffer(256)
        user32.GetWindowTextW(hwnd, t, 256)
        if t.value.startswith('Dishonored 2') and user32.IsWindowVisible(hwnd):
            found.append(hwnd)
        return True
    user32.EnumWindows(visit, 0)
    return found[0] if found else None


class BIH(ctypes.Structure):
    _fields_ = [('biSize', ctypes.c_uint32), ('biWidth', ctypes.c_int32), ('biHeight', ctypes.c_int32), ('biPlanes', ctypes.c_uint16),
                ('biBitCount', ctypes.c_uint16), ('biCompression', ctypes.c_uint32), ('biSizeImage', ctypes.c_uint32), ('a', ctypes.c_int32),
                ('b', ctypes.c_int32), ('c', ctypes.c_uint32), ('d', ctypes.c_uint32)]


def grab(hwnd):
    r = wintypes.RECT()
    user32.GetClientRect(hwnd, ctypes.byref(r))
    w, h = r.right, r.bottom
    if w < 16 or h < 16:
        return None
    hdc = user32.GetDC(hwnd)
    mdc = gdi32.CreateCompatibleDC(hdc)
    bmp = gdi32.CreateCompatibleBitmap(hdc, w, h)
    old = gdi32.SelectObject(mdc, bmp)
    user32.PrintWindow(hwnd, mdc, 3)  # PW_CLIENTONLY | PW_RENDERFULLCONTENT
    bi = BIH(40, w, -h, 1, 32, 0, 0, 0, 0, 0, 0)
    buf = ctypes.create_string_buffer(w * h * 4)
    gdi32.GetDIBits(mdc, bmp, 0, h, buf, ctypes.byref(bi), 0)
    gdi32.SelectObject(mdc, old)
    gdi32.DeleteObject(bmp)
    gdi32.DeleteDC(mdc)
    user32.ReleaseDC(hwnd, hdc)
    return np.frombuffer(buf, np.uint8).reshape(h, w, 4)[:, :, :3].copy()


def main():
    # Per-monitor DPI awareness: without it Windows reports the 150%-scaled window size and PrintWindow captured only its
    # top-left 594x422 (headset session 2026-10-13).
    try:
        user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))
    except (AttributeError, OSError):
        pass
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--fps', type=float, default=10)
    ap.add_argument('--minutes', type=float, default=30)
    ap.add_argument('--segment', type=float, default=60, help='seconds per MP4 file (a file is readable once closed)')
    ap.add_argument('--out', default=os.path.join(ROOT, 'artifacts', 'recordings'))
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    stamp = time.strftime('%Y%m%dT%H%M%S')
    hwnd = game_window()
    if not hwnd:
        raise SystemExit('Dishonored 2 window not found')
    writer, size = None, None
    status_path = os.path.join(ART := os.path.join(ROOT, 'artifacts'), 'xr-live-status.json')
    log = open(os.path.join(a.out, stamp + '-status.jsonl'), 'w')
    end, period, next_status, frames = time.time() + a.minutes * 60, 1 / a.fps, 0, 0
    segment_end = 0
    while time.time() < end:
        t0 = time.time()
        if not user32.IsWindow(hwnd):
            break
        img = grab(hwnd)
        tick = kernel32.GetTickCount64()
        if img is not None:
            if writer is None or img.shape[:2] != size or t0 >= segment_end:  # resized window or segment done: new file
                if writer:
                    writer.release()
                size = img.shape[:2]
                segment_end = t0 + a.segment
                writer = cv2.VideoWriter(os.path.join(a.out, f'{stamp}-{tick}.mp4'), cv2.VideoWriter_fourcc(*'mp4v'), a.fps, (size[1], size[0]))
            cv2.putText(img, str(tick), (6, 18), cv2.FONT_HERSHEY_SIMPLEX, .5, (0, 255, 255), 1, cv2.LINE_AA)
            writer.write(img)
            frames += 1
        if t0 >= next_status:
            next_status = t0 + .5
            try:
                d = json.load(open(status_path))
                log.write(json.dumps({'tick': tick, **{k: d.get(k) for k in KEEP}}) + '\n')
                log.flush()
            except (OSError, ValueError):
                pass
        time.sleep(max(0, period - (time.time() - t0)))
    if writer:
        writer.release()
    log.close()
    print(f'{frames} frames -> {a.out}/{stamp}-*.mp4')


if __name__ == '__main__':
    main()
