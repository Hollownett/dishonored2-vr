"""S3: grab the collar (chest/neck) and lift. Needs the knob: sim --knob collar-grab.

Tier B scenario (needs test save: a dead guard lying on the floor within reach of the standing, facing player (a limb at the hand's REACH position, ~0.5 m ahead); save to ~/d2vr-test-saves/corpse-dead; run with --knob collar-grab).
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
SAVE = "a dead guard lying on the floor within reach of the standing, facing player (a limb at the hand's REACH position, ~0.5 m ahead); save to ~/d2vr-test-saves/corpse-dead; run with --knob collar-grab"


def build(lead_ms):
    t = lead_ms
    segments = L.lead_in(lead_ms)
    collar = [REACH[0], -0.6, REACH[2] - 0.1]
    segments.append(L.hold(t, t + 1500, NEUTRAL, collar))
    segments.append(L.hold(t + 1500, t + 2300, CLOSE, collar))
    segments += L.glide(t + 2300, t + 4300, collar, [collar[0], -0.25, collar[2]], 20, GRIP)
    segments.append(L.hold(t + 4300, t + 7300, GRIP, [collar[0], -0.25, collar[2]]))
    segments.append(L.hold(t + 7300, t + 8300, NEUTRAL, [collar[0], -0.25, collar[2]]))
    segments.append(L.hold(t + 8300, t + 11000, NEUTRAL, ANKLE))
    return segments
