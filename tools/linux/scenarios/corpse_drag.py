"""S1: grab a dead guard's ankle, walk backwards 4 s dragging him, open the fist.

Tier B scenario (needs test save: a dead guard lying on the floor within reach of the standing, facing player (a limb at the hand's REACH position, ~0.5 m ahead); save to ~/d2vr-test-saves/corpse-dead).
Same segment format and lead-in as oil_tank_drop.py. build(lead_ms) -> segments.
"""
import importlib.util
from pathlib import Path

_spec = importlib.util.spec_from_file_location("d2vr_scenario_lib", Path(__file__).resolve().parents[1] / "scenario_lib.py")
L = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(L)
NEUTRAL, GRIP, CLOSE, REACH, CHEST, FLOOR = L.NEUTRAL, L.GRIP, L.CLOSE, L.REACH, L.CHEST, L.FLOOR
ANKLE = REACH                       # the hand at a dead guard's ankle on the floor
RAISED = [REACH[0], -0.3, REACH[2]]  # about 0.5 m above ANKLE
LEFT_REACH = L.mirror(REACH)
SAVE = "a dead guard lying on the floor within reach of the standing, facing player (a limb at the hand's REACH position, ~0.5 m ahead); save to ~/d2vr-test-saves/corpse-dead"


def build(lead_ms):
    t = lead_ms
    segments = L.lead_in(lead_ms)
    segments.append(L.hold(t, t + 1500, NEUTRAL, ANKLE))
    segments.append(L.hold(t + 1500, t + 2300, CLOSE, ANKLE))                         # grab
    segments.append(L.hold(t + 2300, t + 6800, dict(L.GRIP, move_y=-1.0), ANKLE))     # walk back with the body
    segments.append(L.hold(t + 6800, t + 7800, NEUTRAL, ANKLE))                       # open fist: release at rest
    segments.append(L.hold(t + 7800, t + 11000, NEUTRAL, ANKLE))
    return segments
