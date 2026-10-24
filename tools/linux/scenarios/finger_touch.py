"""S10: scripted capacitive touch on the right hand: rest the index, point, thumbs-up, full fist, open hand (2.5 s each).

Tier B scenario, needs no props and no special save (the hand stays empty, held at eye height ahead of the player).
The DLL logs the `fingers` status at each segment's end (finger-touch-segments.jsonl); the five pose segments are the last five of the script,
so bonelab_run_check.py judges the last five samples. Touch columns need the version 7 gameplay script.
build(lead_ms) -> segments.
"""
import importlib.util
from pathlib import Path

_spec = importlib.util.spec_from_file_location("d2vr_scenario_lib", Path(__file__).resolve().parents[1] / "scenario_lib.py")
L = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(L)
HAND = [-0.2, 0.0, -0.1]  # right hand ahead at about eye height: nothing in reach
POSE_MS = 2500            # each pose holds well past the 1.5 s the curl needs to settle (slews 10/s and 25/s)
POSES = ("rest", "point", "thumbs_up", "fist", "open")
# (input, touch): trigger/grip values and the capacitive sensors the right hand reports
SPEC = {
    "rest": ({}, {"right_trigger": True}),                                                             # index resting on the trigger
    "point": ({"right_grip": 1.0}, {"right_thumbstick": True}),                                        # trigger untouched, grip 1
    "thumbs_up": ({"right_grip": 1.0}, {"right_trigger": True}),                                       # thumb sensors all clear
    "fist": ({"right_grip": 1.0, "right_trigger": 1.0}, {"right_trigger": True, "right_thumbstick": True, "right_first": True}),
    "open": ({}, {}),
}


def build(lead_ms):
    segments = L.lead_in(lead_ms)
    for i, name in enumerate(POSES):
        inp, touch = SPEC[name]
        seg = L.hold(lead_ms + i * POSE_MS, lead_ms + (i + 1) * POSE_MS, dict(L.NEUTRAL, **inp), HAND)
        seg["touch"] = dict(touch)
        segments.append(seg)
    return segments
