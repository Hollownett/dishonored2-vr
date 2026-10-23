"""S12 (held clamp): grab a whale-oil tank, carry it forward into a wall with move_y, then lower it onto the floor.

Runs on the current save next to a tank (like oil_tank_drop.py; without a tank it grabs the nearest prop). Pass: the held
clamp swept, hit and clamped (held_clamp.sweeps/hits/clamped_frames > 0). Segment format: tools/simulated_gameplay_script.py.
"""
import importlib.util
from pathlib import Path

_spec = importlib.util.spec_from_file_location("d2vr_scenario_lib", Path(__file__).resolve().parents[1] / "scenario_lib.py")
L = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(L)


def build(lead_ms):
    t = lead_ms
    segments = L.lead_in(lead_ms)
    segments.append(L.hold(t, t + 1500, L.NEUTRAL, L.REACH))
    segments.append(L.hold(t + 1500, t + 2700, L.CLOSE, L.REACH))                    # grab
    segments += L.glide(t + 2700, t + 4200, L.REACH, L.CHEST, 15, L.GRIP)            # lift
    segments.append(L.hold(t + 4200, t + 10200, dict(L.GRIP, move_y=1.0), L.CHEST))  # walk the tank into the wall
    segments.append(L.hold(t + 10200, t + 11200, L.GRIP, L.CHEST))                   # stand against it
    segments += L.glide(t + 11200, t + 13200, L.CHEST, L.FLOOR, 20, L.GRIP)         # press it down onto the floor
    segments.append(L.hold(t + 13200, t + 14200, L.GRIP, L.FLOOR))
    segments.append(L.hold(t + 14200, t + 20000, L.NEUTRAL, L.FLOOR))               # release and watch it settle
    return segments
