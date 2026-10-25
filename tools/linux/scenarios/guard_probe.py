"""Task 14 Tier B scenario: the one-shot living-guard lever probe; hands plan Task 13 adds the teleport to an unaware guard (below).

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
_idle_spec = importlib.util.spec_from_file_location("d2vr_scenario_idle", Path(__file__).with_name("idle.py"))
_idle = importlib.util.module_from_spec(_idle_spec)
_idle_spec.loader.exec_module(_idle)
CHEST_AHEAD = [0.0, 0.0, 0.0]    # the simulated default hand: 0.4 m ahead, 0.35 m below the eye (a standing guard's chest)
SNATCH = [0.0, 0.0, 0.6]         # 0.6 m back toward the body
SAVE = "the user's latest save (Continue): campaign/institute/institute_p_highchaos"
# Plan Task 13 (G6 measure first): the spot, found read-only with tools/entities.py over /proc/PID/mem (ledger g6-probe.md).
# guard_male_small_01.def_9276 stands still and alone at (11.69, 46.90, 20.01), reference-frame row 0 (0.635, 0.772); no other living guard
# within 10 m. The teleport puts the eye 0.6 m BEHIND him (against row 0), facing him: camera Task 3 got the game's choke offer there (he is
# unaware), while in front of him he sees the player at once and the alert lever could not be measured. The yank / probe (guard_at) takes a
# living guard's upper body at the hand from any side: the fist lands 0.15 m behind his spine, ~1.55 m above his model origin (1.30 read 1.88: over guard_at's 1.85 window).
GUARD_FEET = (11.69, 46.90, 20.01)
GUARD_FORWARD = (0.635, 0.772)
BEHIND_M, EYE_ABOVE_FEET = 0.6, 1.70
SETTLE_MS = 150   # land, then squeeze at once: he holds still ~0.3 s after the teleport, then steps 1-1.6 m away (personal-space reaction, g6-probe.md)
SQUEEZE_MS = 150


def stand_eye():
    fx, fy = GUARD_FORWARD
    n = (fx * fx + fy * fy) ** 0.5
    return (GUARD_FEET[0] - fx / n * BEHIND_M, GUARD_FEET[1] - fy / n * BEHIND_M, GUARD_FEET[2] + EYE_ABOVE_FEET)


def gaze_deg():
    import math
    x, y, _ = stand_eye()
    return math.degrees(math.atan2(GUARD_FEET[1] - y, GUARD_FEET[0] - x))


# Absolute tracking-space poses (script v5, as loot_reach): x right, y up (height above the feet - 0.05), -z forward; the head stands at 1.65 m.
# The fist lands at his back, 0.45 m ahead of the eye at 1.05 m up (0.15 m behind his spine), then snatches 0.45 m back in 0.15 s (3 m/s).
HEAD_Y = 1.65
GRAB = (0.0, 1.00, -0.45)
BACK = (0.0, 1.00, 0.0)
LEFT_REST = (-0.2, 1.3, -0.4)
IDENTITY = (0.0, 0.0, 0.0, 1.0)


def _seg(t0, t1, inp, right):
    head = [0.0, HEAD_Y, 0.0, *[round(v, 6) for v in _idle.head_quaternion(0.0, 0.0)]]
    return {"start_ms": t0, "end_ms": t1, "input": dict(inp), "head_pitch_degrees": 0, "head_pose": head,
            "left_hand_pose": [*LEFT_REST, *IDENTITY], "right_hand_pose": [round(v, 4) for v in right] + list(IDENTITY)}


def build(lead_ms):
    t = lead_ms
    segments = L.lead_in(lead_ms)
    segments.append(L.console(t, L.setviewpos(*stand_eye(), gaze_deg())))
    t += L.FRAME_MS
    segments.append(_seg(t, t + SETTLE_MS, L.NEUTRAL, GRAB))                                   # land, the open hand at his back
    segments.append(_seg(t + SETTLE_MS, t + SETTLE_MS + SQUEEZE_MS, L.CLOSE, GRAB))            # full-fist squeeze
    t += SETTLE_MS + SQUEEZE_MS
    for i in range(3):                                                                           # the snatch: 0.45 m in 0.15 s
        k = (i + 1) / 3
        at = tuple(GRAB[j] + (BACK[j] - GRAB[j]) * k for j in range(3))
        segments.append(_seg(t + 50 * i, t + 50 * i + 50, L.CLOSE, at))
    segments.append(_seg(t + 150, t + 11150, L.NEUTRAL, BACK))                                  # the probe samples for 6 s; wait past it
    return segments
