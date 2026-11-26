"""The open hand flat on a wall (2026-11-26, user: "fingers should react correctly to tables and walls ... steady in movement").

At WALL_FRONT moved 0.15 m nearer the wall (lab-facts.md; the wall ~0.45 m ahead of the eye), the right hand palm-first (absolute pose: the
palm faces the wall, the fingers point up) at shoulder height: 15 cm off, pressed in (the controller goes ~5 cm past the surface: the hand
clamp stops the drawn palm, the fingers must lie on the wall), held open 2 s, a half grip while pressed 2 s (G3: the fingers stay open on the
wall), released; slid +-12 cm slowly (0.1 m/s) and fast (0.6 m/s); then the player strafes along the wall 2 s each way with the hand on it
(steady in movement); lifted off (the fingers curl back), a grip off the wall (a fist forms: the open share ends). Captures
(`d2vr spacewarp capture 2`) while held open, while gripping and while strafing. `D2VR_SPOT=doto|d2`.

Pass (finger_ik in xr-live-status.json, right hand = index 1):
- tip_pen[1][0] (frames at the surface) > 0; tip_pen max slow <= 5 mm, max fast <= 15 mm (G5);
- steady[1] = [frames, max tip step mm, mean tip step mm, open frames, blocked flips]: open frames > 0 (G3), the mean tip step <= 2 mm and
  the blocked flips few (G7: no chatter) while the hand slides and the player strafes (the mean is of each frame's largest long-finger tip
  step and includes the first contact's closing);
- A/B: --knob no-finger-put-out (G1 off) shows deeper tips; --knob legacy-finger-slab the old 24 cm planes.
`D2VR_SPOT=doto tools/linux/sim_serial.sh LOG --game doto sim --scenario palm_wall --settle 45 --lead 54 --knob diagnostics --candidate-dll ...`
"""
import importlib.util
import os
from pathlib import Path

_spec = importlib.util.spec_from_file_location("d2vr_scenario_lib", Path(__file__).resolve().parents[1] / "scenario_lib.py")
L = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(L)

# eye (x, y, z) and game yaw: WALL_FRONT (lab-facts.md) moved 0.15 m along the wall's normal toward it
SPOTS = {"doto": (-27.850, -53.770, 42.25, -129.3), "d2": (66.587, -6.590, 34.60, -11.5)}
PALM_FWD = [0.5, -0.5, 0.5, 0.5]       # OpenXR right grip: the palm (-x) faces forward (-z), the fingers (-z) point up (+y)
REST = [0.2, 1.3, -0.4, 0.0, 0.0, 0.0, 1.0]
OFF = [0.15, 1.50, -0.30]               # 15 cm off the wall
PRESS = [0.15, 1.50, -0.50]             # ~5 cm past the surface
SIDE = 0.12
CAP = "d2vr spacewarp capture 2"
STEP_MS = 100


def build(lead_ms):
    x, y, z, yaw = SPOTS[os.environ.get("D2VR_SPOT", "doto")]
    segments = L.lead_in(lead_ms)
    t = lead_ms

    def seg(ms, pos, inp=None, cons=None, quat=PALM_FWD):
        nonlocal t
        s = {"start_ms": t, "end_ms": t + ms, "head_pitch_degrees": 0, "input": dict(inp or L.NEUTRAL),
             "right_hand_pose": [round(v, 4) for v in pos] + list(quat)}
        if cons:
            segments.append(dict(s, end_ms=t + L.FRAME_MS, console=cons))
            s["start_ms"] = t + L.FRAME_MS
        segments.append(s)
        t += ms

    def glide(ms, a, b, inp=None):
        """a -> b over ms: the runner lerps each absolute pose toward the next segment's, so a segment carries its START pose (the hold
        before a glide stays put; the glide's last segment runs into the following segment's pose, b)."""
        steps = max(1, ms // STEP_MS)
        for i in range(steps):
            seg(ms // steps, L.lerp(a, b, i / steps), inp)

    seg(1200, REST[:3], cons=L.setviewpos(x, y, z, yaw), quat=REST[3:])
    glide(800, REST[:3], OFF)
    glide(1000, OFF, PRESS)                                   # press in at 0.2 m/s
    seg(1000, PRESS)
    seg(1000, PRESS, cons=CAP)                                # held open on the wall
    seg(1000, PRESS, inp={"right_grip": 0.6})
    seg(1000, PRESS, inp={"right_grip": 0.6}, cons=CAP)       # a half grip while pressed: the fingers stay open (G3)
    seg(1000, PRESS)
    right, left = [PRESS[0] + SIDE, PRESS[1], PRESS[2]], [PRESS[0] - SIDE, PRESS[1], PRESS[2]]
    glide(1200, PRESS, right)                                 # slow slide, 0.1 m/s
    glide(2400, right, left)
    glide(1200, left, PRESS)
    for _ in range(3):                                        # fast slide, 0.6 m/s
        glide(200, PRESS, right)
        glide(400, right, left)
        glide(200, left, PRESS)
    seg(800, PRESS)
    seg(1000, PRESS, inp={"move_x": 0.6})                     # strafe along the wall, the hand on it
    seg(1000, PRESS, inp={"move_x": 0.6}, cons=CAP)
    seg(2000, PRESS, inp={"move_x": -0.6})
    seg(800, PRESS)
    glide(500, PRESS, OFF)                                    # lift off: the fingers curl back
    seg(1000, OFF)
    seg(1000, OFF, inp={"right_grip": 1.0})                   # a fist off the wall (the open share has ended)
    seg(800, OFF)
    glide(800, OFF, REST[:3])
    seg(1500, REST[:3], quat=REST[3:])
    return segments
