"""Gameplay-script scenario (DOTO): the forearm gun's grip + trigger pressed in the SAME frame, as quick taps (v145751; user: "the first
shot does not register" - the grab guard swallowed a trigger within 400 ms of its grip). Three left taps of 300 ms, 2.5 s apart (the
one-shot magazine reloads in ~1.1 s): each must fire (the Gauss magazine empties and reloads: scratchpad gauss_sample.py).
"""
import importlib.util
from pathlib import Path

_spec = importlib.util.spec_from_file_location("d2vr_scenario_lib", Path(__file__).resolve().parents[1] / "scenario_lib.py")
_lib = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_lib)

TOTAL_MS = 40000


def build(lead_ms):
    out = _lib.lead_in(lead_ms)
    t = lead_ms

    def seg(inp, ms):
        nonlocal t
        out.append({"start_ms": t, "end_ms": t + ms, "head_pitch_degrees": -10, "input": dict(inp)})
        t += ms

    seg({"move_x": 0.0}, 3000)
    for _ in range(3):
        seg({"left_grip": 1.0, "left_trigger": 1.0}, 300)
        seg({"move_x": 0.0}, 2500)
    out.append({"start_ms": t, "end_ms": lead_ms + TOTAL_MS, "head_pitch_degrees": 0, "input": {"move_x": 0.0}})
    return out
