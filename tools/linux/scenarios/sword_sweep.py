"""Gameplay-script scenario: the sword hand sweeps left and right in front of the player, slowly and then in fast slashes.

Used by `linux/d2vr-linux sim --scenario sword_sweep`. It exists to count how often the hand and the sword in it fall back to the
native arm for an upload (status `arm_flicker`: gate_flips, fallback_runs_max_frames, why[], publish{}). Nothing is grabbed: the
right grip stays open, the sword is whatever the saved game has drawn. Phase 1 sweeps at 0.75 m/s (below the mod's slash speed, so
no attack fires); phase 2 sweeps at ~3.5 m/s, the speed at which the mod presses attack, so the native attack animation and the
tracked hand take turns the way they do in a fight. The head stays level.

build(lead_ms) returns the segment list for tools/simulated_gameplay_script.py (script clock = session start): the neutral lead-in
with A pulses on menus only (shared with oil_tank_drop), then the sweeps, then a quiet tail. Hand offsets are metres relative to the
simulated default right hand (0.4 m ahead, 0.2 m right, 0.35 m below the eye); x right, y up, z backward.
"""
import importlib.util
from pathlib import Path

_spec = importlib.util.spec_from_file_location("d2vr_scenario_oil_tank_drop", Path(__file__).with_name("oil_tank_drop.py"))
_oil = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_oil)

SLOW_HALF_MS, SLOW_STEPS, SLOW_HALVES = 800, 6, 8     # 0.6 m in 0.8 s = 0.75 m/s
FAST_HALF_MS, FAST_STEPS, FAST_HALVES = 170, 3, 8     # 0.6 m in 0.17 s = 3.5 m/s
TAIL_MS = 4000
LEFT = [-0.3, -0.1, -0.2]
RIGHT = [0.3, -0.1, -0.2]
NEUTRAL = {"move_x": 0.0}


def _sweep(t0, half_ms, steps, halves):
    out, a, b = [], LEFT, RIGHT
    for h in range(halves):
        for i in range(steps):
            k = (i + 1) / steps
            out.append({"start_ms": round(t0 + h * half_ms + half_ms * i / steps),
                        "end_ms": round(t0 + h * half_ms + half_ms * (i + 1) / steps),
                        "head_pitch_degrees": 0, "input": dict(NEUTRAL),
                        "right_hand_offset": [round(x + (y - x) * k, 4) for x, y in zip(a, b)]})
        a, b = b, a
    return out


def build(lead_ms):
    segments = _oil._lead_in(lead_ms)
    t = lead_ms
    segments.append({"start_ms": t, "end_ms": t + 1000, "head_pitch_degrees": 0, "input": dict(NEUTRAL), "right_hand_offset": list(LEFT)})
    t += 1000
    segments += _sweep(t, SLOW_HALF_MS, SLOW_STEPS, SLOW_HALVES)
    t += SLOW_HALF_MS * SLOW_HALVES
    segments.append({"start_ms": t, "end_ms": t + 1000, "head_pitch_degrees": 0, "input": dict(NEUTRAL), "right_hand_offset": list(LEFT)})
    t += 1000
    segments += _sweep(t, FAST_HALF_MS, FAST_STEPS, FAST_HALVES)
    t += FAST_HALF_MS * FAST_HALVES
    segments.append({"start_ms": t, "end_ms": t + TAIL_MS, "head_pitch_degrees": 0, "input": dict(NEUTRAL), "right_hand_offset": list(LEFT)})
    return segments
