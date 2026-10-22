"""S6: hold a body, open the pause menu with menu and press A with when=always every 2 s (first entry). UNVERIFIED: whether the pause menu's first entry reloads depends on the menu; if it does not, S6 is Tier C item 7 (reload mid-hold, releases.lifecycle >= 1).

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
    segments.append(L.hold(t + 2300, t + 5000, GRIP, ANKLE))
    segments.append({"start_ms": t + 5000, "end_ms": t + 5200, "input": {"menu": True}, "when": "always"})
    cursor = t + 5200
    for i in range(6):
        start = t + 7000 + i * 2000
        segments.append({"start_ms": cursor, "end_ms": start, "input": dict(NEUTRAL), "when": "always"})
        segments.append({"start_ms": start, "end_ms": start + 200, "input": {"a": True}, "when": "always"})
        cursor = start + 200
    segments.append({"start_ms": cursor, "end_ms": cursor + 10000, "input": dict(NEUTRAL), "when": "always"})
    return segments
