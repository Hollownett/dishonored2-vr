"""The right arm while leaning toward a wall (2026-11-26, arm collisions). Physical head translation is not applied to the camera
(docs/physical-head-translation.md), but a sideways head offset is the mod's physical lean: the game's own lean moves the camera, and the
body and arms follow the camera, so the shoulder comes much nearer the wall than the player's capsule allows standing (arm_wall's trace:
the shoulder stayed >= 0.27 m off, the natural elbow >= 0.11 m: nothing to resolve).

At WALL_RIGHT (lab-facts.md; the wall 0.38 m to the right of the eye): lean right (head offset +0.25 m x), then with the right hand: down by
the side, forward at shoulder height, high on the wall side, the hand at the chest (elbow out); each held 1.5 s with a capture in the
middle; un-lean. `D2VR_SPOT=doto|d2`. Pass (with --knob arm-collision-shadow as the baseline): arm_collision.hits_solid > 0 and
natural_pen_frames > 0 in both runs; with the collision on, max_elbow_pen_mm near 0 while max_natural_pen_mm > 0.
"""
import importlib.util
import os
from pathlib import Path

_spec = importlib.util.spec_from_file_location("d2vr_scenario_lib", Path(__file__).resolve().parents[1] / "scenario_lib.py")
L = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(L)

SPOTS = {"doto": (-27.894, -53.824, 42.25, -39.3), "d2": (66.666, -6.606, 34.60, 78.5)}
LEAN = [0.25, 0.0, 0.0]
CAP = "d2vr spacewarp capture 2"
POSES = ([0.20, -0.30, 0.30], [0.15, 0.20, -0.10], [0.20, 0.50, 0.05], [-0.10, 0.05, 0.20])   # side, forward, high, chest (elbow out)


def build(lead_ms):
    x, y, z, yaw = SPOTS[os.environ.get("D2VR_SPOT", "doto")]
    segments = L.lead_in(lead_ms)
    t = lead_ms

    def hold(ms, head, hand, cons=None):
        nonlocal t
        seg = {"start_ms": t, "end_ms": t + ms, "head_pitch_degrees": 0, "head_offset": list(head), "input": dict(L.NEUTRAL),
               "right_hand_offset": list(hand)}
        if cons:
            segments.append(dict(seg, end_ms=t + L.FRAME_MS, console=cons))
            seg["start_ms"] = t + L.FRAME_MS
        segments.append(seg)
        t += ms

    hold(1200, [0.0, 0.0, 0.0], [0.0, 0.0, 0.0], cons=L.setviewpos(x, y, z, yaw))
    for k in range(10):                      # lean in over 1 s
        hold(100, L.lerp([0.0, 0.0, 0.0], LEAN, (k + 1) / 10), [0.0, 0.0, 0.0])
    hold(800, LEAN, [0.0, 0.0, 0.0])
    prev = [0.0, 0.0, 0.0]
    for pose in POSES:
        for k in range(8):
            hold(100, LEAN, L.lerp(prev, pose, (k + 1) / 8))
        hold(700, LEAN, pose)
        hold(800, LEAN, pose, cons=CAP)
        prev = pose
    for k in range(10):                      # lean out
        hold(100, L.lerp(LEAN, [0.0, 0.0, 0.0], (k + 1) / 10), prev)
    hold(1500, [0.0, 0.0, 0.0], prev)
    return segments
