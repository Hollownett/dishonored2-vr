"""The back of the hand pressed on a wall (2026-11-26, Task 16: the reference VR mod's back-of-hand rule). Both games.

At palm_wall's spot (WALL_FRONT moved 0.15 m nearer: the wall ~0.45 m ahead of the eye) the right hand faces the player with its BACK on the
wall (absolute pose: palm_wall's turned about the fingers: the palm (-x of the OpenXR right grip) toward the player (+z)): 15 cm off,
pressed in (the controller ~5 cm past the surface, as palm_wall), held relaxed 2 s, then a grip of 0.8 for 2 s (the fingers want to close), slid +-10 cm, lifted off.
Captures (`d2vr spacewarp capture 2`) relaxed and gripping on the wall.
Pass (finger_ik in xr-live-status.json, right hand = index 1): round2.back_frames[1] > 0 (contact.back > 0.3: the rule engaged); tip_pen
over-limit frames few (the fingers straighten along the wall, never into it); with --knob legacy-finger-surface back_frames stays 0.
`D2VR_SPOT=doto|d2 tools/linux/sim_serial.sh LOG [--game doto] sim --scenario back_wall --lead 54 --knob diagnostics --candidate-dll ...`
"""
import importlib.util
import os
from pathlib import Path

_spec = importlib.util.spec_from_file_location("d2vr_scenario_lib", Path(__file__).resolve().parents[1] / "scenario_lib.py")
L = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(L)

SPOTS = {"doto": (-27.850, -53.770, 42.25, -129.3), "d2": (66.587, -6.590, 34.60, -11.5)}   # palm_wall.py's
# palm_wall's PALM_FWD turned 180 deg about the grip's own Y: the open fingers lie along the grip's -Y (OpenXR -Z runs along the handle,
# little finger to thumb), so they keep palm_wall's direction (sideways along the wall) and the palm now faces the player (+z), the back the
# wall. (2026-11-27: the first pose, [0.5, 0.5, -0.5, 0.5], took -Z for the fingers: they hung down and the back never met the wall.)
BACK_FWD = [-0.5, 0.5, 0.5, 0.5]
REST = [0.2, 1.3, -0.4, 0.0, 0.0, 0.0, 1.0]
OFF = [0.15, 1.50, -0.30]
PRESS = [0.15, 1.50, -0.50]            # the back of the hand ~6 cm past the wall (the hand clamp holds it on the surface)
SIDE = 0.10
CAP = "d2vr spacewarp capture 2"
STEP_MS = 100


def build(lead_ms):
    x, y, z, yaw = SPOTS[os.environ.get("D2VR_SPOT", "doto")]
    segments = L.lead_in(lead_ms)
    t = lead_ms

    def seg(ms, pos, inp=None, cons=None, quat=BACK_FWD):
        nonlocal t
        s = {"start_ms": t, "end_ms": t + ms, "head_pitch_degrees": 0, "input": dict(inp or L.NEUTRAL),
             "right_hand_pose": [round(v, 4) for v in pos] + list(quat)}
        if cons:
            segments.append(dict(s, end_ms=t + L.FRAME_MS, console=cons))
            s["start_ms"] = t + L.FRAME_MS
        segments.append(s)
        t += ms

    def glide(ms, a, b, inp=None):
        steps = max(1, ms // STEP_MS)   # each segment carries its START pose (the runner lerps toward the next segment's)
        for i in range(steps):
            seg(ms // steps, L.lerp(a, b, i / steps), inp)

    seg(1200, REST[:3], cons=L.setviewpos(x, y, z, yaw), quat=REST[3:])
    glide(800, REST[:3], OFF)
    glide(1000, OFF, PRESS)
    seg(1000, PRESS)
    seg(1000, PRESS, cons=CAP)                                # relaxed, the back on the wall
    seg(1000, PRESS, inp={"right_grip": 0.8})
    seg(1000, PRESS, inp={"right_grip": 0.8}, cons=CAP)       # the fingers want to close: they straighten instead
    seg(800, PRESS)
    right, left = [PRESS[0] + SIDE, PRESS[1], PRESS[2]], [PRESS[0] - SIDE, PRESS[1], PRESS[2]]
    glide(1000, PRESS, right)
    glide(2000, right, left)
    glide(1000, left, PRESS)
    seg(800, PRESS)
    glide(500, PRESS, OFF)                                    # lift off: the fingers curl back
    seg(1200, OFF)
    glide(800, OFF, REST[:3])
    seg(1500, REST[:3], quat=REST[3:])
    return segments
