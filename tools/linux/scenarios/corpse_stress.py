"""S5: 50 grab/open cycles on one body (the rig cannot move between bodies; with five bodies present run it per body). Each cycle is 1.0 s: grab 0.4 s, hold 0.2 s, open 0.4 s, so every regrab is 0.4 s after an open fist, past the 0.3 s cooldown. Fits a 120 s session with lead <= 60 s; run four sessions of 50 and sum.

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
    cycles = 50
    segments.append(L.hold(t, t + 1000, NEUTRAL, ANKLE))
    t += 1000
    for i in range(cycles):
        c = t + i * 1000
        segments.append(L.hold(c, c + 400, CLOSE, ANKLE))      # grab
        segments.append(L.hold(c + 400, c + 600, GRIP, ANKLE))  # hold
        segments.append(L.hold(c + 600, c + 1000, NEUTRAL, ANKLE))  # open fist
    return segments
