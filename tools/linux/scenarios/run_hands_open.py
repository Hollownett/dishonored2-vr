"""Gameplay-script scenario: run_hands in open floor. The save's spawn is a small room (the player stops after ~5 m), so each locomotion phase
starts with an engine teleport (gameplay script v8 console `setviewpos`) to the long lab floor and faces down it: stand 4 s, walk 6 s,
run 5 s, sprint (move_y 1 plus left_click held) 5 s, then stand 3 s, the head still and both hands at their simulated defaults.

Used by `linux/d2vr-linux sim --scenario run_hands_open --knob locomotion-trace --lead 45 --seconds 120` and judged by
`tools/linux/bonelab_run_check.py RUN --scenario run_hands` (speed bins) plus its sprint and oscillation lines.
"""
import importlib.util
from pathlib import Path

_spec = importlib.util.spec_from_file_location("d2vr_scenario_lib", Path(__file__).resolve().parents[1] / "scenario_lib.py")
L = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(L)

START = (-24.86, 46.08, 34.00 + 1.70)   # eye; the lab floor the loot_reach "floor" case stands on
YAW = 0.0                                # +x: down the lab
TURN = 0.5   # a continuous gentle turn keeps the player circling inside the room instead of stopping at a wall
PHASES = (("stand", 4000, {"move_y": 0.0}), ("walk", 6000, {"move_y": 0.5, "turn_x": TURN}), ("run", 8000, {"move_y": 1.0, "turn_x": TURN}),
          ("sprint", 10000, {"move_y": 1.0, "turn_x": TURN, "left_click": True}), ("stand", 3000, {"move_y": 0.0}))


def build(lead_ms, extra=None):
    t = lead_ms
    segments = L.lead_in(lead_ms)
    for _, ms, values in PHASES:
        segments.append(L.console(t, L.setviewpos(START[0], START[1], START[2], YAW)))
        segments.append({"start_ms": t + L.FRAME_MS, "end_ms": t + 1500, "head_pitch_degrees": 0, "input": dict(L.NEUTRAL)})
        t += 1500   # settle after the teleport
        segments.append({"start_ms": t, "end_ms": t + ms, "head_pitch_degrees": 0, "input": {**L.NEUTRAL, **values, **(extra or {})}})
        t += ms
    return segments
