"""Gameplay-script scenario: the right arm against walls (plan 2026-11-26 Task 8, arm collisions).

Run per game: `D2VR_SPOT=doto|d2 linux/d2vr-linux sim --scenario arm_wall --knob arm-collision --lead 54 --seconds 120`, and again with
`--knob arm-collision-shadow` for the baseline. Spots: WALL_RIGHT (wall 0.38 m to the right of the eye) and WALL_FRONT (wall 0.60 m ahead).

Timeline after the lead (right hand only, grip open, head pitch 0, hand pitch 0, left hand at its default):
 1  setviewpos WALL_RIGHT, settle 1 s.
 a  reach forward-right at shoulder height (0.10, 0.25, -0.10); sweep 0.30 m toward the wall (+x) at 0.2 m/s (1.5 s), hold 0.4 s at the
    far end (capture at its middle), sweep back 1.5 s.
 b  reach high on the wall side (0.15, 0.55, 0.05), hold 1.5 s (capture at the middle).
 c  hand down by the hip on the wall side (0.15, -0.40, 0.35), hold 1.5 s (capture at the middle).
 d  elbow out: hand in front of the chest (-0.15, 0.10, 0.15), hold 1.5 s.
 2  setviewpos WALL_FRONT, settle 1 s; hand to (-0.05, 0.10, -0.35) (0.15 m into the wall at chest height), press 1 s, slide 0.25 m left
    and 0.25 m right at 0.1 m/s (the control: the hand clamp holds it at the wall and the elbow stays behind), back to the centre.
Pass: xr-live-status.json arm_collision.engaged_frames[1] > 0; elbow_pen_mm max near 0 with arm collision on (> 0 with the shadow knob);
arm_collision.max_step_deg <= 8; the three captures show the forearm and elbow outside the wall.
"""
import importlib.util
import os
from pathlib import Path

_spec = importlib.util.spec_from_file_location("d2vr_scenario_lib", Path(__file__).resolve().parents[1] / "scenario_lib.py")
L = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(L)

# eye x, y, z (metres) and yaw (degrees) of the two wall spots, from docs/research/2026-11-26-holsters-collisions/lab-facts.md
SPOTS = {
    "doto": {"WALL_RIGHT": (-27.894, -53.824, 42.25, -39.3), "WALL_FRONT": (-27.755, -53.654, 42.25, -129.3)},
    "d2": {"WALL_RIGHT": (66.666, -6.606, 34.60, 78.5), "WALL_FRONT": (66.440, -6.560, 34.60, -11.5)},
}
ZERO = [0.0, 0.0, 0.0]
NEU = {"move_x": 0.0}
CAP = "d2vr spacewarp capture 2"
REST, FAR = [0.10, 0.25, -0.10], [0.40, 0.25, -0.10]
HIGH, LOW, ELBOW = [0.15, 0.55, 0.05], [0.15, -0.40, 0.35], [-0.15, 0.10, 0.15]
PRESS, LEFT_PT, RIGHT_PT = [-0.05, 0.10, -0.35], [-0.30, 0.10, -0.35], [0.20, 0.10, -0.35]


def pose(t0, t1, right=ZERO, left=ZERO, inp=NEU):
    return {"start_ms": t0, "end_ms": t1, "head_pitch_degrees": 0, "input": dict(inp),
            "right_hand_offset": list(right), "left_hand_offset": list(left), "hand_pitch_degrees": 0}


def build(lead_ms):
    spot = SPOTS[os.environ.get("D2VR_SPOT", "doto")]
    segments = L.lead_in(lead_ms)
    t = lead_ms

    def add(ms, cons=None, **kw):
        """A hold of `ms`; with `cons`, its first 11 ms are a console frame carrying the same pose."""
        nonlocal t
        if cons:
            segments.append(dict(pose(t, t + L.FRAME_MS, **kw), console=cons))
            segments.append(pose(t + L.FRAME_MS, t + ms, **kw))
        else:
            segments.append(pose(t, t + ms, **kw))
        t += ms

    def glide(ms, steps, right=(ZERO, ZERO), left=(ZERO, ZERO), **kw):
        nonlocal t
        for i in range(steps):
            k = (i + 1) / steps
            segments.append(pose(t + ms * i // steps, t + ms * (i + 1) // steps,
                                 right=L.lerp(*right, k), left=L.lerp(*left, k), **kw))
        t += ms

    def view(name):
        x, y, z, yaw = spot[name]
        return L.setviewpos(x, y, z, yaw)

    def hold_capture(ms, pose_at):
        add(ms // 2, right=pose_at)
        add(ms - ms // 2, cons=CAP, right=pose_at)

    # 1 and a: out to the wall and back
    add(1000, cons=view("WALL_RIGHT"), right=REST)
    glide(1500, 10, right=(REST, FAR))
    hold_capture(400, FAR)
    glide(1500, 10, right=(FAR, REST))
    # b, c, d
    glide(800, 6, right=(REST, HIGH))
    hold_capture(1500, HIGH)
    glide(800, 6, right=(HIGH, LOW))
    hold_capture(1500, LOW)
    glide(800, 6, right=(LOW, ELBOW))
    add(1500, right=ELBOW)
    # 2: the wall in front, press and slide
    add(1000, cons=view("WALL_FRONT"), right=ELBOW)
    glide(800, 6, right=(ELBOW, PRESS))
    add(1000, right=PRESS)
    glide(2500, 10, right=(PRESS, LEFT_PT))
    glide(5000, 20, right=(LEFT_PT, RIGHT_PT))
    glide(2500, 10, right=(RIGHT_PT, PRESS))
    add(1000, right=PRESS)
    return segments
