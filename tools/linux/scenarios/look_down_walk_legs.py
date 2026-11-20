"""look_down_walk with the same-frame A/B of the v1438 leg rule (`spacewarp altlegs`: generated-alt is the frame without the drawn legs'
own steps, the v1437 rigid body), for the user's headset v1437 report "fix legs warping ... minor ghosting around player legs"."""
import importlib.util
from pathlib import Path

_spec = importlib.util.spec_from_file_location("d2vr_scenario_look_down_walk", Path(__file__).with_name("look_down_walk.py"))
_base = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_base)

SEGMENTS = tuple((ms, pitch, values, "d2vr spacewarp altlegs" if console == "d2vr spacewarp altbody" else console)
                 for ms, pitch, values, console in _base.SEGMENTS)


def build(lead_ms):
    _base.SEGMENTS = SEGMENTS
    return _base.build(lead_ms)
