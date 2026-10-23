"""S13 (held clamp): drop a tank from about 1.5 m, then drop another pressed into the floor; wait over 5 s after the last release (measured from the open hand).

Same save as oil_tank_drop.py. Pass: release_overlap_unresolved == 0, layer_at_release recorded, and the tank's rest height from
release_watch keeps only the LAST release (the floor-pressed one): its z_last must be within 0.15 m of z_at_release (3 s watch).
The 1.5 m drop itself is judged only by the headset Tier C item.
"""
import importlib.util
from pathlib import Path

_spec = importlib.util.spec_from_file_location("d2vr_scenario_lib", Path(__file__).resolve().parents[1] / "scenario_lib.py")
L = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(L)


HIGH = [-0.1, 0.15, -0.2]   # hand about 1.5 m above the floor (head height, arm raised)


def build(lead_ms):
    t = lead_ms
    segments = L.lead_in(lead_ms)
    segments.append(L.hold(t, t + 1500, L.NEUTRAL, L.REACH))
    segments.append(L.hold(t + 1500, t + 2700, L.CLOSE, L.REACH))                    # grab
    segments += L.glide(t + 2700, t + 4200, L.REACH, HIGH, 15, L.GRIP)               # lift to 1.5 m
    segments.append(L.hold(t + 4200, t + 5000, L.GRIP, HIGH))
    segments.append(L.hold(t + 5000, t + 6000, L.NEUTRAL, HIGH))                     # release: drop from 1.5 m
    segments.append(L.hold(t + 6000, t + 9000, L.NEUTRAL, L.REACH))                  # it lands
    segments.append(L.hold(t + 9000, t + 10200, L.CLOSE, L.REACH))                   # grab again
    segments += L.glide(t + 10200, t + 11200, L.REACH, L.FLOOR, 10, L.GRIP)          # press it into the floor
    segments.append(L.hold(t + 11200, t + 12200, L.GRIP, L.FLOOR))
    segments.append(L.hold(t + 12200, t + 13200, L.NEUTRAL, L.FLOOR))                # release pressed into the floor
    segments.append(L.hold(t + 13200, t + 18500, L.NEUTRAL, L.FLOOR))               # 5 s wait after the last release
    return segments
