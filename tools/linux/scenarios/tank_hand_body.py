"""S15 (Plan B task B16): the whale-oil tank carried by the hand body. Grab the tank, walk it into a wall, lift and drop it, grab it again and
press it into the floor, release, and watch it settle for more than 5 s.

Run twice on a save next to a tank (like tank_wall.py / tank_drop.py; ~/d2vr-test-saves/tank-floor once the user provides it):
  linux/d2vr-linux sim --scenario tank_hand_body --lead 45 --seconds 120 --knob hand-body
  linux/d2vr-linux sim --scenario tank_hand_body --lead 45 --seconds 120 --knob no-hand-body
The first 12 s after the lead are idle so the hand bodies exist before the grab (in-run G1 8 s, then the creates). Pass
(tools/linux/bonelab_run_check.py RUN --scenario tank_hand_body): the last (floor-pressed) release rests near its release height,
release_overlap_unresolved == 0, hand_body.faults == 0, no layer left swapped (the held layer ledger and the hand's filter word), and with the
body on contact_frames > 0 and the native hold coupled to the body (hold_coupling native uses_body > 0). Segment format:
tools/simulated_gameplay_script.py. Without a tank in reach nothing is held: the checker then reports the coupling idle (uses_body 0).
"""
import importlib.util
from pathlib import Path

_spec = importlib.util.spec_from_file_location("d2vr_scenario_lib", Path(__file__).resolve().parents[1] / "scenario_lib.py")
L = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(L)

HOME = [0.0, 0.0, 0.0]
HIGH = [-0.1, 0.15, -0.2]   # about 1.5 m above the floor (tank_drop.py)
IDLE_MS = 12000             # the bodies are created before the grab


def build(lead_ms):
    t = lead_ms
    segments = L.lead_in(lead_ms)
    segments.append(L.hold(t, t + IDLE_MS, L.NEUTRAL, HOME))
    t += IDLE_MS
    segments.append(L.hold(t, t + 1500, L.NEUTRAL, L.REACH))
    segments.append(L.hold(t + 1500, t + 2700, L.CLOSE, L.REACH))                    # grab
    segments += L.glide(t + 2700, t + 4200, L.REACH, L.CHEST, 15, L.GRIP)            # lift
    segments.append(L.hold(t + 4200, t + 10200, dict(L.GRIP, move_y=1.0), L.CHEST))  # walk the tank into the wall: the clamp and the body stop it
    segments.append(L.hold(t + 10200, t + 11200, L.GRIP, L.CHEST))                   # stand pressed against it
    segments += L.glide(t + 11200, t + 12700, L.CHEST, HIGH, 15, L.GRIP)             # lift to 1.5 m
    segments.append(L.hold(t + 12700, t + 13500, L.GRIP, HIGH))
    segments.append(L.hold(t + 13500, t + 14500, L.NEUTRAL, HIGH))                   # drop it
    segments.append(L.hold(t + 14500, t + 17500, L.NEUTRAL, L.REACH))                # it lands
    segments.append(L.hold(t + 17500, t + 18700, L.CLOSE, L.REACH))                  # grab again
    segments += L.glide(t + 18700, t + 19700, L.REACH, L.FLOOR, 10, L.GRIP)          # press it into the floor
    segments.append(L.hold(t + 19700, t + 20700, L.GRIP, L.FLOOR))
    segments.append(L.hold(t + 20700, t + 21700, L.NEUTRAL, L.FLOOR))                # release pressed into the floor (the last release)
    segments.append(L.hold(t + 21700, t + 27500, L.NEUTRAL, L.FLOOR))                # more than 5 s after the last release
    return segments
