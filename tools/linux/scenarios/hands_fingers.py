"""Gameplay-script scenario: look down at both hands and cycle the controller curls, for the procedural finger pose
(full body: an empty hand's fingers follow grip and trigger; body_ik finger mode 1/2).

Head pitched -40 deg so the tracked hands are in view; per hand: open, grip (fist), light trigger (index curl, kept
at 0.35 so the right trigger never attacks and the left never casts), both, open again. build(lead_ms) returns the
segment list for tools/simulated_gameplay_script.py.
"""
import importlib.util
from pathlib import Path

_spec = importlib.util.spec_from_file_location("d2vr_scenario_lib", Path(__file__).resolve().parents[1] / "scenario_lib.py")
_lib = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_lib)

PITCH = -40
STEPS = (
    ({"move_x": 0.0}, 1500),
    ({"left_grip": 1.0, "right_grip": 1.0}, 1800),
    ({"move_x": 0.0}, 1200),
    ({"left_trigger": 0.35, "right_trigger": 0.35}, 1800),
    ({"move_x": 0.0}, 1200),
    ({"left_grip": 1.0, "right_grip": 1.0, "left_trigger": 0.35, "right_trigger": 0.35}, 1800),
    ({"left_grip": 0.5, "right_grip": 0.5}, 1500),
    ({"move_x": 0.0}, 1500),
)
TOTAL_MS = 60000


def build(lead_ms):
    out = _lib.lead_in(lead_ms)
    t = lead_ms
    for _ in range(2):
        for inp, ms in STEPS:
            out.append({"start_ms": t, "end_ms": t + ms, "head_pitch_degrees": PITCH, "input": dict(inp)})
            t += ms
    out.append({"start_ms": t, "end_ms": lead_ms + TOTAL_MS, "head_pitch_degrees": 0, "input": {"move_x": 0.0}})
    return out
