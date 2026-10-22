"""S4: both hands grab the body (one at each limb), lift together, put it down.

Tier B scenario (needs test save: a dead guard lying on the floor within reach of the standing, facing player (a limb at the hand's REACH position, ~0.5 m ahead); save to ~/d2vr-test-saves/corpse-dead; two limbs reachable (ankles or wrists apart)).
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
SAVE = "a dead guard lying on the floor within reach of the standing, facing player (a limb at the hand's REACH position, ~0.5 m ahead); save to ~/d2vr-test-saves/corpse-dead; two limbs reachable (ankles or wrists apart)"


def build(lead_ms):
    t = lead_ms
    segments = L.lead_in(lead_ms)
    both = L.two(CLOSE)
    segments.append(L.hold(t, t + 1500, NEUTRAL, ANKLE, left=LEFT_REACH))
    segments.append(L.hold(t + 1500, t + 2300, both, ANKLE, left=LEFT_REACH))
    segments += L.glide(t + 2300, t + 4300, ANKLE, RAISED, 20, L.two(GRIP), left=(LEFT_REACH, L.mirror(RAISED)))
    segments.append(L.hold(t + 4300, t + 7300, L.two(GRIP), RAISED, left=L.mirror(RAISED)))
    segments += L.glide(t + 7300, t + 8300, RAISED, ANKLE, 10, L.two(GRIP), left=(L.mirror(RAISED), LEFT_REACH))
    segments.append(L.hold(t + 8300, t + 9300, NEUTRAL, ANKLE, left=LEFT_REACH))
    segments.append(L.hold(t + 9300, t + 12000, NEUTRAL, ANKLE, left=LEFT_REACH))
    return segments
