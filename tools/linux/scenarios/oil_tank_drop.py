"""Gameplay-script scenario: pick up a whale-oil tank with the right hand, lift it, lower the hand to the floor, let go.

Used by `linux/d2vr-linux sim --scenario oil_tank_drop` (UNTESTED against a real tank: it needs a save that starts next to
one, see "Simulated test runs (developer)" in linux/README.md; without one it grabs whatever physical object is nearest the
right hand and drops it, which still exercises the carry/drop path).

build(lead_ms) returns the segment list for tools/simulated_gameplay_script.py (script clock = session start, so the first
segment is a neutral lead-in while the game is driven into gameplay). Hand offsets are metres relative to the simulated
default right hand (0.4 m ahead, 0.2 m right, 0.35 m below the eye); x right, y up, z backward. The head looks down at the
object. Pose numbers follow tools/vr_selftest.py's 'hold' scenario (a bottle on the floor in front of the player).
"""
import importlib.util
from pathlib import Path

_spec = importlib.util.spec_from_file_location("d2vr_scenario_lib", Path(__file__).resolve().parents[1] / "scenario_lib.py")
_lib = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_lib)
NEUTRAL, GRIP, CLOSE, REACH, CHEST, FLOOR, HEAD_PITCH = _lib.NEUTRAL, _lib.GRIP, _lib.CLOSE, _lib.REACH, _lib.CHEST, _lib.FLOOR, _lib.HEAD_PITCH
PRESS_FIRST_MS, PRESS_EVERY_MS, PRESS_MS = _lib.PRESS_FIRST_MS, _lib.PRESS_EVERY_MS, _lib.PRESS_MS
_glide, _hold, _lead_in = _lib.glide, _lib.hold, _lib.lead_in


def build(lead_ms):
    t = lead_ms
    segments = _lead_in(lead_ms)
    segments.append(_hold(t, t + 1500, NEUTRAL, REACH))           # settle the hand at the object
    segments.append(_hold(t + 1500, t + 2700, CLOSE, REACH))      # grab
    segments += _glide(t + 2700, t + 4200, REACH, CHEST, 15, GRIP)  # lift
    segments.append(_hold(t + 4200, t + 5500, GRIP, CHEST))       # carry at the chest
    segments += _glide(t + 5500, t + 7500, CHEST, FLOOR, 20, GRIP)  # lower the hand to the floor
    segments.append(_hold(t + 7500, t + 8500, GRIP, FLOOR))       # hold at the floor
    segments.append(_hold(t + 8500, t + 9500, NEUTRAL, FLOOR))    # release
    segments.append(_hold(t + 9500, t + 14000, NEUTRAL, FLOOR))   # wait: the object must have dropped, nothing else must happen
    return segments
