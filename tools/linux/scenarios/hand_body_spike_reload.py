"""Plan B task B11: the spike with a pause/load in the middle (`--scenario hand_body_spike_reload --knob hand-body-spike=R1`).

Same as hand_body_spike.py, then at lead + 14 s it opens the pause menu (menu, when=always) and presses A every 2 s. Opening the menu ends
gameplay: the spike's lifecycle must destroy the body at once (same world) or, if the dispatcher does not run while paused and a load happens,
abandon it on the world change without any engine call. UNVERIFIED: whether A on the pause menu's first entry starts a load (corpse_reload.py
has the same caveat); if no world change shows in hand-body-spike.json, the load-mid-run case is Tier C.
"""
import importlib.util
from pathlib import Path

_spec = importlib.util.spec_from_file_location("d2vr_scenario_lib", Path(__file__).resolve().parents[1] / "scenario_lib.py")
L = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(L)


def build(lead_ms):
    t = lead_ms
    segments = L.lead_in(lead_ms)
    segments.append({"start_ms": t, "end_ms": t + 14000, "head_pitch_degrees": 0, "input": dict(L.NEUTRAL)})
    segments.append({"start_ms": t + 14000, "end_ms": t + 14200, "input": {"menu": True}, "when": "always"})
    cursor = t + 14200
    for i in range(4):
        start = t + 16000 + i * 2000
        segments.append({"start_ms": cursor, "end_ms": start, "input": dict(L.NEUTRAL), "when": "always"})
        segments.append({"start_ms": start, "end_ms": start + 200, "input": {"a": True}, "when": "always"})
        cursor = start + 200
    segments.append({"start_ms": cursor, "end_ms": cursor + 3000, "input": dict(L.NEUTRAL), "when": "always"})
    return segments
