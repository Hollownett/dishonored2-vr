"""Gameplay-script scenario: draw the sword, then the same stand / walk / run / sprint / stand timeline as run_hands with the grip held.

Used by `linux/d2vr-linux sim --scenario run_hands_sword --knob locomotion-trace`. The right hand first goes to the scabbard zone
(TempleReach(-.17,-.10,-.10,.16) relative to the default hand: offset [-0.03, 0.25, 0.50]), pitched +60 degrees, right_grip 1 for 1.5 s,
which draws the sword. The hand then returns to its default with the grip held through the whole timeline (a sword in a running hand).
"""
import importlib.util
from pathlib import Path

_spec = importlib.util.spec_from_file_location("d2vr_scenario_run_hands", Path(__file__).with_name("run_hands.py"))
_run = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_run)

SCABBARD = [-0.03, 0.25, 0.50]
SETTLE_MS, DRAW_MS, RETURN_MS = 1000, 1500, 1000
GRIP = {"right_grip": 1.0}


def build(lead_ms):
    t = lead_ms
    segments = _run._oil._lead_in(lead_ms)
    segments.append({"start_ms": t, "end_ms": t + SETTLE_MS, "head_pitch_degrees": 0, "input": {"move_y": 0.0}, "right_hand_offset": list(SCABBARD), "hand_pitch_degrees": 60})
    t += SETTLE_MS
    segments.append({"start_ms": t, "end_ms": t + DRAW_MS, "head_pitch_degrees": 0, "input": {"move_y": 0.0, **GRIP}, "right_hand_offset": list(SCABBARD), "hand_pitch_degrees": 60})
    t += DRAW_MS
    segments.append({"start_ms": t, "end_ms": t + RETURN_MS, "head_pitch_degrees": 0, "input": {"move_y": 0.0, **GRIP}})
    t += RETURN_MS
    return segments + _run.timeline(t, GRIP)
