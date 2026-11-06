"""Send keys to the Dishonored 2 window without taking focus or touching the user's input.

Keys go to the game window only (PostMessage WM_KEYDOWN/WM_KEYUP). The game must not pause on focus loss:
`python tools/game_keys.py --prepare-config` (game closed) forces win_pauseOnFocusLoss "0" and
in_controlInactiveWindow "1" in dishonored2Config.cfg. The engine rewrites the cfg from its archived cvars, so
those non-archived lines can disappear again on start/exit; a live game only reads the cfg at process start.
The selftest therefore also sends `cvar in_controlInactiveWindow 1` and `cvar win_pauseOnFocusLoss 0` through
the mod's command channel after injection (tools/vr_selftest.py).

    python tools/game_keys.py 0x0D 0x1B      # Enter, then Esc
"""
import ctypes
import os
import sys
import time
from ctypes import wintypes

user32 = ctypes.windll.user32
CONFIG = os.path.expandvars(r'%USERPROFILE%\Saved Games\Arkane Studios\Dishonored2\base\dishonored2Config.cfg')
BACKGROUND_CVARS = {'win_pauseOnFocusLoss': '"0"', 'in_controlInactiveWindow': '"1"'}


def prepare_config():
    """Force the two focus cvars, replacing a stale default value instead of only appending a missing line.

    The old append-only version skipped the cvar when the engine had already written it with its default
    (`win_pauseOnFocusLoss "1"`), which is exactly the state a running game leaves behind. Rewriting the value
    is still best effort: the engine may drop the non-archived lines again when it next writes the file.
    """
    with open(CONFIG, encoding='utf-8', errors='ignore') as f:
        lines = f.read().splitlines()
    out, seen = [], set()
    for line in lines:
        name = line.split()[0] if line.strip() else ''
        if name in BACKGROUND_CVARS:
            if name not in seen:
                out.append(f'{name} {BACKGROUND_CVARS[name]}')
                seen.add(name)
        else:
            out.append(line)
    for name, value in BACKGROUND_CVARS.items():
        if name not in seen:
            out.append(f'{name} {value}')
    with open(CONFIG, 'w', encoding='utf-8') as f:
        f.write('\n'.join(out) + '\n')


def game_window():
    found = []

    @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def visit(hwnd, _):
        title = ctypes.create_unicode_buffer(256)
        user32.GetWindowTextW(hwnd, title, 256)
        if 'Dishonored' in title.value and user32.IsWindowVisible(hwnd):
            found.append(hwnd)
        return True

    user32.EnumWindows(visit, 0)
    return found[0] if found else None


def post(hwnd, vk):
    scan = user32.MapVirtualKeyW(vk, 0)
    user32.PostMessageW(hwnd, 0x0100, vk, 1 | (scan << 16))                       # WM_KEYDOWN
    time.sleep(.08)
    user32.PostMessageW(hwnd, 0x0101, vk, 1 | (scan << 16) | (3 << 30))           # WM_KEYUP


def main():
    if '--prepare-config' in sys.argv:
        prepare_config()
        return
    hwnd = game_window()
    if not hwnd:
        sys.exit('Dishonored 2 window not found')
    for code in sys.argv[1:]:
        post(hwnd, int(code, 0))
        time.sleep(1.5)


if __name__ == '__main__':
    main()
