"""afw_ab with a held weapon: the lab D2 save has no sword to draw, so the draw phase equips the pistol with the test command
`d2vr equip pistol` (the wheel's own equip) and both hands sweep (the left mirrored), so whichever hand holds it moves through the view.
Same phases, captures and scoring as afw_ab.py (afw_ab_score.py reads phases from afw_ab.PHASES).
"""
import importlib.util
from pathlib import Path

_spec = importlib.util.spec_from_file_location("d2vr_scenario_afw_ab", Path(__file__).with_name("afw_ab.py"))
_ab = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_ab)


def build(lead_ms):
    segments = _ab.build(lead_ms)
    for seg in segments:
        t = seg["start_ms"] - lead_ms
        if t == 19500:
            seg["console"] = "d2vr equip pistol"
            seg["input"].pop("b", None)
        if 19500 < t < 19750:
            seg["input"].pop("b", None)
        hand = seg.get("right_hand_offset")
        if hand is not None and t >= 19000:
            seg["left_hand_offset"] = [-hand[0], hand[1], hand[2]]
            seg["input"]["left_grip"] = seg["input"].get("right_grip", 0.0)
    return segments
