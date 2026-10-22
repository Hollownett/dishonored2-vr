"""Task 14 Tier B scenario: the one-shot living-guard lever probe (needs test save: player standing within reach of an unaware guard).

Write the experiment into the mod home's build/guard-probe first (via sim --knob is not enough, the file carries the text): "shove",
"shove_decl <name>" or "teleport_step <metres> <count>"; see docs/BONELAB-FEEL-GUARD-PULL-RE.md. The probe fires on the pull path (a full fist
squeezed at the guard's upper body, then the hand moving faster than 2.2 m/s: the yank request), runs ONE experiment instead of the pull and writes
artifacts/guard-probe.csv for 6 s. The scenario squeezes at chest height ~0.4 m ahead, snatches the hand back 0.6 m in 0.2 s (3 m/s) and then waits
10 s with the hand open. Same segment format and lead-in as oil_tank_drop.py.
"""
import importlib.util
from pathlib import Path

_spec = importlib.util.spec_from_file_location("d2vr_scenario_lib", Path(__file__).resolve().parents[1] / "scenario_lib.py")
L = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(L)
CHEST_AHEAD = [0.0, 0.0, 0.0]    # the simulated default hand: 0.4 m ahead, 0.35 m below the eye (a standing guard's chest)
SNATCH = [0.0, 0.0, 0.6]         # 0.6 m back toward the body
SAVE = "player standing within reach of an unaware guard (about 0.4 m ahead, facing him, no other guard awake nearby)"


def build(lead_ms):
    t = lead_ms
    segments = L.lead_in(lead_ms)
    segments.append(L.hold(t, t + 1500, L.NEUTRAL, CHEST_AHEAD, pitch=0))
    segments.append(L.hold(t + 1500, t + 2500, L.CLOSE, CHEST_AHEAD, pitch=0))          # full-fist squeeze
    segments += L.glide(t + 2500, t + 2700, CHEST_AHEAD, SNATCH, 2, L.CLOSE, pitch=0)  # 0.6 m in 0.2 s: the pull
    segments.append(L.hold(t + 2700, t + 3200, L.NEUTRAL, SNATCH, pitch=0))
    segments.append(L.hold(t + 3200, t + 13200, L.NEUTRAL, SNATCH, pitch=0))           # the probe samples for 6 s; wait past it
    return segments
