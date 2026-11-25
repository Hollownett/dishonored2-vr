"""Gameplay-script scenario: switch the live renderer between AFW, the built-in warp and native stereo in one session, as the VR menu's
Renderer row does (test command `d2vr renderer 0|1|2`), with the head swaying so every mode has motion to rebuild.

Used by `linux/d2vr-linux sim --scenario renderer_switch --knob diagnostics --knob aa-mode=2` with the Spacewarp feature OFF (Spacewarp
holds sessions in native stereo, by design). Each phase ends with `d2vr afw status` (logged in simulated-gameplay-console.txt), so
the check reads, per phase: AFW rebuilds only grow in the AFW phases, `resets` grows when AFW starts again after a gap, and the status
text reads "off" outside AFW. Phases after the lead (script ms): settle to 19 s (D2 sims stall ~7 s some 20 s after the lead), then
AFW 19-27 s, built-in 27-35 s, AFW 35-43 s, native 43-49 s, AFW 49-55 s.
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

STEP_MS = 50
EYE = (0.0, 1.65, 0.0)
TOTAL_MS = 56000
PHASES = [("settle", 0, 19000, None), ("afw", 19000, 27000, 2), ("builtin", 27000, 35000, 1), ("afw2", 35000, 43000, 2),
          ("native", 43000, 49000, 0), ("afw3", 49000, 55000, 2)]


def build(lead_ms):
    segments = _oil._lead_in(lead_ms)
    lines = {2000: "god"}
    for name, a, b, r in PHASES:
        if r is not None:
            lines[a] = "d2vr renderer %d" % r
            lines[b - 500] = "d2vr afw status"
    for t in range(lead_ms, lead_ms + TOTAL_MS, STEP_MS):
        s = t - lead_ms + STEP_MS / 2
        yaw = 25.0 * math.sin(2 * math.pi * s / 4000) if s >= 19000 else 0.0
        seg = {"start_ms": t, "end_ms": t + STEP_MS, "input": {"move_x": 0.0}, "head_pitch_degrees": 0,
               "head_pose": [*EYE, *[round(v, 6) for v in _idle.head_quaternion(yaw, 0.0)]]}
        if t - lead_ms in lines:
            seg["console"] = lines[t - lead_ms]
        segments.append(seg)
    return segments
