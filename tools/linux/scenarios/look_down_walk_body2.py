"""look_down_walk with the same-frame A/B of the v1460 body rule (`spacewarp altbody2`: generated-alt is the frame with the other body2 rule,
v1445's upload legs and play-space torso while the image rule is on), for the user's 2026-11-21 request "fix players body and legs warp (do
same as for hands) in spacewarp mode"."""
import importlib.util
from pathlib import Path

_spec = importlib.util.spec_from_file_location("d2vr_scenario_look_down_walk", Path(__file__).with_name("look_down_walk.py"))
_base = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_base)

SEGMENTS = tuple((ms, pitch, values, "d2vr spacewarp altbody2" if console == "d2vr spacewarp altbody" else console)
                 for ms, pitch, values, console in _base.SEGMENTS)


def build(lead_ms):
    _base.SEGMENTS = SEGMENTS
    return _base.build(lead_ms)
