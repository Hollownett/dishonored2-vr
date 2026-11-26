"""The right arm into an inside corner (2026-11-26, arm collisions): the hand slides along the wall in front into the corner, so the elbow
(which points down and out) meets the wall on the right. The run-time trace of arm_wall showed why a plain side wall never touches the arm:
the player's capsule keeps the shoulder >= 0.27 m from it and the natural elbow sits >= 0.11 m off (no penetration to resolve).

Spots (lab-facts.md walls; corner = where the two planes meet; the eye 0.55 m from the wall in front and 0.45 m from the wall on the right):
 DOTO: walls 1 (n (0.774,-0.633), n.p 11.06) and 2 (n (0.633,0.774), n.p -59.70) meet at (-29.23, -53.21); face wall 2 (yaw -129.3), wall 1 right.
 D2:   wall A (n (-0.98,0.20), n.p -67.03) and B (n (-0.20,-0.98), n.p -9.517) meet at (67.59, -4.08); face B (yaw 78.5), wall A right.
Timeline (right hand, grip open, head level): press on the wall in front at chest height, slide right into the corner (0.1 m/s), hold 2 s,
raise the hand up the corner, hold, lower it to the hip, hold; capture (`d2vr spacewarp capture 2`) in the corner, high and low.
Pass (with the knob arm-collision-shadow as the baseline): arm_collision.hits_solid > 0, natural_pen_frames > 0 in both runs; with the
collision on, max_elbow_pen_mm near 0 while max_natural_pen_mm > 0; max_step_deg bounded; arm-collision.csv shows the drawn elbow
outside the corner's side wall. `D2VR_SPOT=doto|d2`.
"""
import importlib.util
import os
from pathlib import Path

_spec = importlib.util.spec_from_file_location("d2vr_scenario_lib", Path(__file__).resolve().parents[1] / "scenario_lib.py")
L = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(L)

CORNER = {"doto": (-28.534, -53.069, 42.25, -129.3), "d2": (67.039, -4.529, 34.60, 78.5)}
CAP = "d2vr spacewarp capture 2"
FRONT = [-0.05, 0.15, -0.25]     # 0.15 m into the wall in front (it is 0.55 m ahead), chest height
INTO = [0.40, 0.15, -0.25]       # slid 0.45 m right: into the corner (the wall on the right is 0.45 m from the eye)
HIGH = [0.40, 0.55, -0.20]
LOW = [0.40, -0.35, -0.10]


def build(lead_ms):
    x, y, z, yaw = CORNER[os.environ.get("D2VR_SPOT", "doto")]
    segments = L.lead_in(lead_ms)
    t = lead_ms

    def hold(ms, off, cons=None):
        nonlocal t
        seg = {"start_ms": t, "end_ms": t + ms, "head_pitch_degrees": 0, "input": dict(L.NEUTRAL), "right_hand_offset": list(off)}
        if cons:
            segments.append(dict(seg, end_ms=t + L.FRAME_MS, console=cons))
            seg["start_ms"] = t + L.FRAME_MS
        segments.append(seg)
        t += ms

    def glide(ms, a, b, steps):
        nonlocal t
        for i in range(steps):
            segments.append({"start_ms": t + ms * i // steps, "end_ms": t + ms * (i + 1) // steps, "head_pitch_degrees": 0,
                             "input": dict(L.NEUTRAL), "right_hand_offset": L.lerp(a, b, (i + 1) / steps)})
        t += ms

    hold(1200, [0.0, 0.0, 0.0], cons=L.setviewpos(x, y, z, yaw))
    glide(800, [0.0, 0.0, 0.0], FRONT, 8)
    hold(1000, FRONT)
    glide(4500, FRONT, INTO, 30)            # slide into the corner at 0.1 m/s
    hold(1000, INTO)
    hold(1000, INTO, cons=CAP)
    glide(1500, INTO, HIGH, 12)             # up the corner
    hold(1000, HIGH)
    hold(1000, HIGH, cons=CAP)
    glide(2000, HIGH, LOW, 15)              # down to the hip
    hold(1000, LOW)
    hold(1000, LOW, cons=CAP)
    glide(4500, LOW, FRONT, 30)             # back out along the wall
    hold(1500, FRONT)
    return segments
