"""Gameplay-script scenario: walk with spacewarp always on, change the resolution twice mid-walk (v1438 repro).

User, headset v1437 2026-11-20: after a resolution change "it breaks the warping and frame generation ... I see double image and
ghosting". The script captures spacewarp triples (`spacewarp capture 2`: real, generated, next real) while walking before the change,
then asks the VR menu's resolution path for 0.8 and later 1.2 (dev command `resolution <m>`) and captures again 4 s after each.
"""
import importlib.util
from pathlib import Path

_spec = importlib.util.spec_from_file_location("d2vr_scenario_oil_tank_drop", Path(__file__).with_name("oil_tank_drop.py"))
_oil = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_oil)

CAPTURE = "d2vr spacewarp capture 2"
WALK = {"move_y": 0.5, "turn_x": 0.25}
SEGMENTS = (
    (1000, -10, {"move_y": 0.0}, "d2vr spacewarp always"),
    (1500, -10, WALK, None), (1500, -10, WALK, CAPTURE),
    (4000, -10, WALK, "d2vr resolution 0.8"), (1500, -10, WALK, CAPTURE),
    (4000, -10, WALK, "d2vr resolution 1.2"), (1500, -10, WALK, CAPTURE),
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
