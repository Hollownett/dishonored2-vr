"""Gameplay-script scenario: the wrist watch held in view (dev command `watch on`, v1442) while walking with spacewarp always on.

User, SteamVR on v1440: "the interface on the wrist blinks during frame generation"; that session rejected 3,196 cluster blits. With the
watch forced visible the mod composes the game's HUD cluster every frame, so the sim's status shows cluster updates, rejects and the
compositions v1442 drops (cluster_partial) on whichever spacewarp submit path the knobs choose.
"""
import importlib.util
from pathlib import Path

_spec = importlib.util.spec_from_file_location("d2vr_scenario_oil_tank_drop", Path(__file__).with_name("oil_tank_drop.py"))
_oil = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_oil)

WALK = {"move_y": 0.5, "turn_x": 0.2}
SEGMENTS = (
    (1000, -10, {"move_y": 0.0}, "d2vr spacewarp always"),
    (1000, -10, {"move_y": 0.0}, "d2vr watch on"),
    (20000, -10, WALK, None),
    (2000, -10, {"move_y": 0.0}, None),
)
TOTAL_MS = 90000


def build(lead_ms):
    out = _oil._lead_in(lead_ms)
    t = lead_ms
    for ms, pitch, values, console in SEGMENTS:
        segment = {"start_ms": t, "end_ms": t + ms, "head_pitch_degrees": pitch, "input": dict(values)}
        if console:
            segment["console"] = console
        out.append(segment)
        t += ms
    out.append({"start_ms": t, "end_ms": lead_ms + TOTAL_MS, "head_pitch_degrees": 0, "input": {"move_y": 0.0}})
    return out
