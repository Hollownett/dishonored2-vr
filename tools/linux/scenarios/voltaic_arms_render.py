"""Gameplay-script scenario (DOTO; user: "make sure that voltaic gun ... is rendered correctly"): which forearm shows a Voltaic Gun in
each arm state. For each of: the save's default (the engine's gun, pinned to the left forearm), `voltaic left`, `voltaic right`,
`voltaic both` — 2 s settle, then the LEFT forearm raised to the middle of the view (capture 1), then the RIGHT one (capture 1).
Captures are sw-capture-NN-real (eye 0) in that order: 00/01 default L/R, 02/03 left, 04/05 right, 06/07 both.
Expected: default and `left` = a gun on the left forearm only; `right` = on the right only; `both` = one on each forearm.
"""
import importlib.util
from pathlib import Path

_spec = importlib.util.spec_from_file_location("d2vr_scenario_lib", Path(__file__).resolve().parents[1] / "scenario_lib.py")
_lib = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_lib)

TOTAL_MS = 60000
UP = {"left": [0.22, 0.30, 0.05], "right": [-0.22, 0.30, 0.05]}
DOWN = [0.0, -0.25, 0.1]


def build(lead_ms):
    out = _lib.lead_in(lead_ms)
    t = lead_ms

    def console(line):
        nonlocal t
        out.append(_lib.console(t, line))
        t += _lib.FRAME_MS

    def pose(side, ms):
        nonlocal t
        out.append({"start_ms": t, "end_ms": t + ms, "head_pitch_degrees": -25, "input": {"move_x": 0.0},
                    "left_hand_offset": list(UP["left"] if side == "left" else DOWN),
                    "right_hand_offset": list(UP["right"] if side == "right" else DOWN)})
        t += ms

    for state in (None, "left", "right", "both"):
        if state:
            console("d2vr voltaic " + state)
        pose(None, 2500)
        for side in ("left", "right"):
            pose(side, 1400)
            console("d2vr spacewarp capture 1")
            pose(side, 400)
    out.append({"start_ms": t, "end_ms": lead_ms + TOTAL_MS, "head_pitch_degrees": 0, "input": {"move_x": 0.0}})
    return out
