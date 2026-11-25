"""Gameplay-script scenario: AFW against the built-in alternate-eye warp on the SAME frames (PR 7 review, 2026-11-25).

Needs a DLL that captures both rebuilds of one held eye (`afw capture N` writes afw-capture-NN-{rebuilt,builtin,held,native,...}):
the review build of PR 7 with the same-frame patch. Run with `--knob afw --knob aa-mode=2` (AFW renderer, FSR 3: AFW needs the
per-eye depth a temporal mode keeps).

Phases after the lead-in: see PHASES (settle, sword draw, still, head sway, slow and fast sword sweeps, head flicks, walk forward and back),
requests are logged in simulated-gameplay-console.txt with the capture count, so afw_ab_score.py maps each capture to its phase.
"""
import importlib.util
import math
from pathlib import Path

_here = Path(__file__).resolve().parent


def _load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


_oil = _load("d2vr_scenario_oil_tank_drop", _here / "oil_tank_drop.py")
_idle = _load("d2vr_scenario_idle", _here / "idle.py")
_lib = _load("d2vr_scenario_lib", _here.parent / "scenario_lib.py")

STEP_MS = 50
EYE = (0.0, 1.65, 0.0)
FRONT = [-0.05, 0.10, -0.15]                      # the sword hand raised into view (the head looks down 20 deg during sweeps)
LEFT, RIGHT = [-0.3, 0.05, -0.2], [0.3, 0.05, -0.2]
GRIP = {"right_grip": 1.0}
# D2 sims stall up to ~8 s some time after the lead (2026-11-25 runs: no alternate frames 55-62 s and 66-73 s): settle 19 s, then draw the
# sword with the game's B toggle while the grip is closed and the head looks up (holster_draw.py A), and keep the grip closed throughout.
PHASES = [("settle", 0, 19000), ("draw", 19000, 22000), ("still", 22000, 25000), ("slow", 25000, 32000), ("fast", 32000, 39000),
          ("flick", 39000, 45000), ("sway", 45000, 53000), ("walk", 53000, 58000)]
CAPTURES = [23000, 26000, 28500, 30800, 33000, 35300, 37500, 40000, 42500, 46000, 48500, 51000, 54000, 56300]
CAPTURE_N = 12
TOTAL_MS = 58000


def phase_of(t):
    for name, a, b in PHASES:
        if a <= t < b:
            return name
    return "end"


def _sweep_offset(t, half_ms):
    """Triangle wave between LEFT and RIGHT, half period half_ms."""
    k = (t % (2 * half_ms)) / half_ms
    k = k if k <= 1 else 2 - k
    return [round(a + (b - a) * k, 4) for a, b in zip(LEFT, RIGHT)]


def _pose(t):
    """(yaw, pitch, input, right hand offset) at script ms t."""
    ph = phase_of(t)
    yaw, pitch, inp, hand = 0.0, 0.0, dict(GRIP), list(FRONT)
    if ph == "settle":
        inp = {"move_x": 0.0}
    elif ph == "draw":
        pitch = 25.0
        if 19500 <= t < 19750:
            inp["b"] = True
    elif ph == "sway":
        yaw = 30.0 * math.sin(2 * math.pi * (t - 45000) / 4000)
        pitch = 6.0 * math.sin(2 * math.pi * (t - 45000) / 6000)
    elif ph == "still":
        pitch = -20.0
    elif ph == "slow":
        pitch, hand = -20.0, _sweep_offset(t - 25000, 800)
    elif ph == "fast":
        pitch, hand = -20.0, _sweep_offset(t - 32000, 170)
    elif ph == "flick":
        cycle = (t - 39000) % 2000   # 0.15 s flick to +20 deg, hold, 0.15 s back, hold
        if cycle < 150:
            yaw = 20.0 * cycle / 150
        elif cycle < 1000:
            yaw = 20.0
        elif cycle < 1150:
            yaw = 20.0 * (1 - (cycle - 1000) / 150)
    elif ph == "walk":   # forward 4 s, back 4 s: stays in the lit area it started in
        inp["move_y"] = 1.0 if t < 55500 else -1.0
        yaw = 10.0 * math.sin(2 * math.pi * (t - 53000) / 4000)
    return yaw, pitch, inp, hand


def build(lead_ms):
    segments = _oil._lead_in(lead_ms)
    # Segments may not overlap: a console line rides on the pose segment that starts at its time (fired once, at the first tick).
    lines = {2000: "god", TOTAL_MS - 1000: "d2vr afw status"}
    for at in CAPTURES:
        assert at % STEP_MS == 0 and at not in lines
        lines[at] = "d2vr afw capture %d" % CAPTURE_N
    for t in range(lead_ms, lead_ms + TOTAL_MS, STEP_MS):
        yaw, pitch, inp, hand = _pose(t + STEP_MS / 2 - lead_ms)
        seg = {"start_ms": t, "end_ms": t + STEP_MS, "input": inp,
               "head_pitch_degrees": round(pitch, 3),
               "head_pose": [*EYE, *[round(v, 6) for v in _idle.head_quaternion(yaw, pitch)]],
               "right_hand_offset": hand}
        if t - lead_ms in lines:
            seg["console"] = lines[t - lead_ms]
        segments.append(seg)
    return segments
