"""Gameplay-script scenario: stand, walk, run, sprint, stand, with the head still and both hands at their simulated defaults.

Used by `linux/d2vr-linux sim --scenario run_hands --knob locomotion-trace` (add `--knob no-hand-body` for the legacy hand source). It measures
the "arms lag, shake and drift while running" problem: the DLL writes artifacts/locomotion-hands.csv (one row per displayed frame) and
`tools/linux/bonelab_run_check.py RUN --scenario run_hands` bins it by anchor-root speed (standing < 0.3 m/s, running >= 2.5 m/s).

build(lead_ms) returns the segment list for tools/simulated_gameplay_script.py: the neutral lead-in (A pulses on menus only, shared with
oil_tank_drop), then stand 8 s, walk (move_y .5) 12 s, run (move_y 1) 12 s, sprint (move_y 1 plus left_click held) 12 s, stand 8 s.
"""
import importlib.util
from pathlib import Path

_spec = importlib.util.spec_from_file_location("d2vr_scenario_oil_tank_drop", Path(__file__).with_name("oil_tank_drop.py"))
_oil = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_oil)

PHASES = (("stand", 8000, {"move_y": 0.0}), ("walk", 12000, {"move_y": 0.5}), ("run", 12000, {"move_y": 1.0}),
          ("sprint", 12000, {"move_y": 1.0, "left_click": True}), ("stand", 8000, {"move_y": 0.0}))


def timeline(t, extra=None):
    """The locomotion phases from t on; extra is merged into every segment's input (e.g. the held grip)."""
    out = []
    for _, ms, values in PHASES:
        out.append({"start_ms": t, "end_ms": t + ms, "head_pitch_degrees": 0, "input": {**values, **(extra or {})}})
        t += ms
    return out


def build(lead_ms):
    return _oil._lead_in(lead_ms) + timeline(lead_ms)
