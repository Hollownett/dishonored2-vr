"""Plan B task B11: the hand-body creation spike (`linux/d2vr-linux sim --scenario hand_body_spike --knob hand-body-spike=R1`).

The spike itself runs inside the DLL (src/hand_body_spike_engine.hpp): it observes G1 for 8 s of gameplay, reads owner (a), creates a 5 cm
dynamic sphere 0.45 m in front of the player and 0.30 m above the floor, lets it settle, pushes it through the player's capsule for 5 s, keeps it
until 60 s after its creation (or the session end) and destroys it. The script only keeps the player still: neutral input, both hands at the
simulated default pose (well above the floor, so the hand clamps never reach the sphere), head level. Any save with a floor under the player works.
Results: artifacts/hand-body-spike.json (final), hand-body-spike.csv (one row per second), hand-body-spike.txt (the knob as read).
Same segment format and lead-in as oil_tank_drop.py. build(lead_ms) -> segments.
"""
import importlib.util
from pathlib import Path

_spec = importlib.util.spec_from_file_location("d2vr_scenario_lib", Path(__file__).resolve().parents[1] / "scenario_lib.py")
L = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(L)
SPIKE_MS = 24000   # after the lead: G1 (8 s of gameplay from whenever gameplay began) plus settle and press fit well inside


def build(lead_ms):
    t = lead_ms
    segments = L.lead_in(lead_ms)
    segments.append({"start_ms": t, "end_ms": t + SPIKE_MS, "head_pitch_degrees": 0, "input": dict(L.NEUTRAL)})
    return segments
