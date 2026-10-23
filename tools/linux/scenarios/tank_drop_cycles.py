"""S14 (held clamp): 25 grab / lift / drop cycles in one session (run 4 sessions for 100 releases).

3.2 s a cycle, 4 segments each, so 25 cycles fit the 120 s cap with the default 28 s lead and the 128-segment script limit. Pass
(summed over the sessions): 100 releases, release_overlap_unresolved == 0, corpse.fault_latch == false, no carry fault counters.
"""
import importlib.util
from pathlib import Path

_spec = importlib.util.spec_from_file_location("d2vr_scenario_lib", Path(__file__).resolve().parents[1] / "scenario_lib.py")
L = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(L)

CYCLES = 25
CYCLE_MS = 3200


def build(lead_ms):
    segments = L.lead_in(lead_ms)
    for i in range(CYCLES):
        t = lead_ms + i * CYCLE_MS
        segments.append(L.hold(t, t + 500, L.NEUTRAL, L.REACH))
        segments.append(L.hold(t + 500, t + 1200, L.CLOSE, L.REACH))     # grab
        segments.append(L.hold(t + 1200, t + 2100, L.GRIP, L.CHEST))     # lift
        segments.append(L.hold(t + 2100, t + CYCLE_MS, L.NEUTRAL, L.CHEST))  # drop
    return segments
