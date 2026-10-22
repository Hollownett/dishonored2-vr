"""S7: with the fist closed on a body, press A: records whether the carry/pickup prompt or the grab wins (informational: binds vs carries).

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
    segments.append(L.hold(t + 1500, t + 2300, CLOSE, ANKLE))
    segments.append(L.hold(t + 2300, t + 3000, GRIP, ANKLE))
    for i in range(4):
        base = t + 3000 + i * 1500
        segments.append(L.hold(base, base + 200, dict(L.GRIP, a=True), ANKLE))
        segments.append(L.hold(base + 200, base + 1500, GRIP, ANKLE))
    segments.append(L.hold(t + 9000, t + 10000, NEUTRAL, ANKLE))
    segments.append(L.hold(t + 10000, t + 12000, NEUTRAL, ANKLE))
    return segments
