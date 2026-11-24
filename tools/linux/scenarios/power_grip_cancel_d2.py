"""D2 variant of power_grip_cancel.py (GitHub issue #6, Emily's Far Reach): the same cycles with `d2vr equip far_reach` first.
Corvo saves have no Far Reach: the equip is refused and powers.present lists what the save owns (pick Blink then)."""
import importlib.util
from pathlib import Path

_spec = importlib.util.spec_from_file_location("d2vr_power_grip_cancel", Path(__file__).with_name("power_grip_cancel.py"))
_base = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_base)


def build(lead_ms):
    return _base.build(lead_ms, power="far_reach")
