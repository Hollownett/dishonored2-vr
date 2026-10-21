"""Gameplay-script scenario: walk forward for 10 s, crouch and stand three times while walking, then stand still. A camera
height (landing / crouch / stairs wobble) comparison for `comfort.anchor_jerk_events` against `comfort.view_jerk_events`.

Used by `linux/d2vr-linux sim --scenario crouch_walk`. build(lead_ms) returns the segment list for tools/simulated_gameplay_script.py:
the neutral lead-in with A pulses on menus only (copied from oil_tank_drop so title -> Continue advance by themselves), then
10 s of forward walking (move_y), then three crouch toggles. A crouch is the physical one the mod already understands: the
head drops 0.5 m below the standing eye (0, 1.65, 0) for 2 s (the mod's crouch threshold is 0.35 m) and rises again, so the
engine's own crouch height change drives the camera. Finally the player stands still until 90 s of gameplay have passed.
Head poses are absolute tracking-space [x, y, z, qx, qy, qz, qw] (script version 5).
"""
import importlib.util
from pathlib import Path

_spec = importlib.util.spec_from_file_location("d2vr_scenario_oil_tank_drop", Path(__file__).with_name("oil_tank_drop.py"))
_oil = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_oil)

TOTAL_MS = 90000
WALK_MS = 10000
CROUCH_MS, STAND_MS = 2000, 3000
CROUCHES = 3
STAND_EYE = (0.0, 1.65, 0.0)
CROUCH_EYE = (0.0, 1.15, 0.0)
LEVEL = (0.0, 0.0, 0.0, 1.0)  # no head rotation


def _segment(t0, t1, move_y, eye):
    return {"start_ms": t0, "end_ms": t1, "input": {"move_y": move_y}, "head_pitch_degrees": 0,
            "head_pose": [*eye, *LEVEL]}


def build(lead_ms):
    segments = _oil._lead_in(lead_ms)
    t = lead_ms
    segments.append(_segment(t, t + WALK_MS, 1.0, STAND_EYE))
    t += WALK_MS
    for _ in range(CROUCHES):
        segments.append(_segment(t, t + CROUCH_MS, 1.0, CROUCH_EYE))
        segments.append(_segment(t + CROUCH_MS, t + CROUCH_MS + STAND_MS, 1.0, STAND_EYE))
        t += CROUCH_MS + STAND_MS
    segments.append(_segment(t, lead_ms + TOTAL_MS, 0.0, STAND_EYE))
    return segments
