"""S2: grab the ankle, lift the body 0.5 m with one hand, swing it side to side, put it down.

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
    segments += L.glide(t + 2300, t + 4300, ANKLE, RAISED, 20, GRIP)                   # lift 0.5 m
    for i in range(3):                                                                  # swing
        base = t + 4300 + i * 1600
        segments += L.glide(base, base + 800, RAISED, [RAISED[0] + 0.4, RAISED[1], RAISED[2]], 8, GRIP)
        segments += L.glide(base + 800, base + 1600, [RAISED[0] + 0.4, RAISED[1], RAISED[2]], RAISED, 8, GRIP)
    segments += L.glide(t + 9100, t + 10100, RAISED, ANKLE, 10, GRIP)
    segments.append(L.hold(t + 10100, t + 11100, NEUTRAL, ANKLE))
    segments.append(L.hold(t + 11100, t + 14000, NEUTRAL, ANKLE))
    return segments
