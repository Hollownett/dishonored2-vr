"""Read-only replica of find_native_player_camera() with the generated DOTO constants: prints every check and the first
that fails, so a live gameplay session shows which offset is still wrong.

    python tools/doto/check_camera.py
"""
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import live  # noqa: E402

GAME, GAME_VT, PLAYER_OFF, PLAYER_VT = 0x3a72280, 0x1d1a678, 0x327770, 0x1ce3258
CAM_VT, CAM_VT2 = 0x1cf78d8, 0x1cf79b8
BODY_VT, BODY_VT2, CTRL_VT, PHYS_VT, PHYS_VT2, PHYS_30 = 0x1cdf2c8, 0x1cdf3a0, 0x1cf7f58, 0x1cf88f8, 0x1cf89d0, 0x1cf8a08
DELEGATE_170 = 0x1c09768


def main():
    p = live.Proc()
    b = p.base
    ok = True

    def check(name, cond, extra=""):
        nonlocal ok
        print(("ok   " if cond else "FAIL ") + name + (" " + extra if extra else ""))
        if not cond and ok:
            ok = False
        return cond

    def vt(a):
        v = p.u64(a)
        return v - b if v else None

    def typed(a, primary, secondary=0):
        if not a or vt(a) != primary:
            return False
        return not secondary or (p.u64(a + 0x20) or 0) - b == secondary

    def started(a):
        raw = p.read(a + 0x18, 8)
        if not raw:
            return None
        gen, flags = struct.unpack('<iI', raw)
        state = ((((flags >> 2) & 3) | ((flags & 3) << 2)) << 2) | ((flags >> 4) & 3)
        return (gen, flags) if gen != -1 and (state & 40) == 40 else None

    g = p.u64(b + GAME)
    check("game typed", typed(g, GAME_VT))
    pl = p.u64(g + PLAYER_OFF)
    check("player typed", typed(pl, PLAYER_VT), f"{pl:#x}")
    lst = p.read(pl + 0x270, 16)
    data, cnt, cap = struct.unpack('<QII', lst)
    cnt &= 0xffffff
    print(f"     components {cnt}")
    cams = []
    for i in range(cnt):
        c = p.u64(data + 8 * i)
        name = p.rtti(c) or ''
        if 'Camera' in name:
            print(f"     [{i}] {c:#x} {name} vt=+{vt(c):#x} vt2=+{(p.u64(c + 0x20) or 0) - b:#x} owner_ok={p.u64(c + 0x10) == pl}")
        if typed(c, CAM_VT, CAM_VT2) and p.u64(c + 0x10) == pl:
            cams.append(c)
    if not check("exactly one camera component (vtables)", len(cams) == 1, f"found {len(cams)}"):
        return
    cam = cams[0]
    check("camera started", started(cam) is not None)
    w = p.read(cam + 0x25180, 0x200)

    def f(fmt, off):
        return struct.unpack_from(fmt, w, off - 0x25180)[0]
    ctrl, phys, body = f('<Q', 0x25188), f('<Q', 0x25190), f('<Q', 0x251a0)
    mode, tmode = f('<i', 0x25230), f('<i', 0x25234)
    print(f"     controller {ctrl:#x} {p.rtti(ctrl)}  physics {phys:#x} {p.rtti(phys)}  body {body:#x} {p.rtti(body)}  mode {mode}/{tmode}")
    print(f"     pitch {f('<f', 0x25294):.3f} yaw {f('<f', 0x252a4):.3f} roll {f('<f', 0x252a8):.3f} aim origin "
          f"{struct.unpack_from('<3f', w, 0x252ac - 0x25180)}")
    check("body typed", typed(body, BODY_VT, BODY_VT2), f"vt=+{vt(body) or 0:#x} vt2=+{(p.u64(body + 0x20) or 0) - b:#x}")
    check("controller typed", typed(ctrl, CTRL_VT), f"vt=+{vt(ctrl) or 0:#x}")
    check("physics typed", typed(phys, PHYS_VT, PHYS_VT2), f"vt=+{vt(phys) or 0:#x} vt2=+{(p.u64(phys + 0x20) or 0) - b:#x}")
    check("physics+0x30", (p.u64(phys + 0x30) or 0) - b == PHYS_30, f"+{(p.u64(phys + 0x30) or 0) - b:#x}")
    check("owners", p.u64(body + 0x10) == pl and p.u64(ctrl + 0x10) == pl and p.u64(phys + 0x10) == pl)
    check("physics started", started(phys) is not None)
    d = p.u64(phys + 0x48)
    check("delegate+0x170", (p.u64(d + 0x170) or 0) - b == DELEGATE_170, f"+{(p.u64(d + 0x170) or 0) - b:#x}")
    check("delegate+0x178 == physics", p.u64(d + 0x178) == phys, f"{p.u64(d + 0x178) or 0:#x}")
    check("delegate+0x130 == player+0x310", p.u64(d + 0x130) == pl + 0x310, f"{p.u64(d + 0x130) or 0:#x} vs {pl + 0x310:#x}")
    check("delegate+0x1f0 == delegate", p.u64(d + 0x1f0) == d)
    heading = p.f32(d + 0xd0)[0]
    check("heading in range", abs(heading) <= 3.141603, f"{heading:.3f}")
    look = p.f32(ctrl + 0xd0, 2)
    check("look finite", all(abs(x) < 1e7 for x in look), str(look))
    check("mode range", 0 <= mode <= 32 and 0 <= tmode <= 32)
    print("RESULT:", "camera chain OK" if ok else "first failure above")


if __name__ == "__main__":
    main()
