"""Gameplay-script scenario (DOTO baseline): 60 s of standing, head looks and one grip/trigger squeeze per hand.

Used by `linux/d2vr-linux --game doto sim --scenario doto_idle_look`. Timeline after the lead-in (build(lead_ms) returns the
segment list for tools/simulated_gameplay_script.py; the lead-in is the shared neutral lead with A pulses on menus only):
  0-5 s    stand still, head level
  5-20 s   head looks left, right, up, down (3.75 s each, +-35 deg yaw / +-30 deg pitch, eased there and back by 1 s ramps)
  20-28 s  look down (pitch -45) for 8 s
  28-30 s  neutral
  30-32 s  right hand: grip 1 s, then grip + trigger 1 s; left hand idle
  32-34 s  left hand: grip 1 s, then grip + trigger 1 s; right hand idle
  34-60 s  idle
Head poses are absolute tracking-space [x, y, z, qx, qy, qz, qw] (script version 5), like head_turn_walk.py; yaw/pitch ramp in
100 ms segments so the pose changes every frame.
"""
import importlib.util
from pathlib import Path

_here = Path(__file__).resolve()


def _load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


_lib = _load("d2vr_scenario_lib", _here.parents[1] / "scenario_lib.py")
_idle = _load("d2vr_scenario_idle", _here.with_name("idle.py"))

TOTAL_MS = 60000
STEP_MS = 100
EYE = (0.0, 1.65, 0.0)  # src/simulated_xr.hpp default head position
STILL_MS = 5000
LOOK_MS = 15000          # four looks of 3750 ms
LOOK_DOWN_MS = 8000
YAW_DEG, PITCH_UP_DEG, PITCH_DOWN_DEG = 35.0, 30.0, -45.0
LOOKS = [(-YAW_DEG, 0.0), (YAW_DEG, 0.0), (0.0, PITCH_UP_DEG), (0.0, PITCH_DOWN_DEG)]  # left, right, up, down (yaw, pitch)


def _head(yaw, pitch):
    return [*EYE, *[round(v, 6) for v in _idle.head_quaternion(yaw, pitch)]]


def _look_at(t_ms):
    """(yaw, pitch) at t_ms after the lead-in: only the head moves."""
    if t_ms < STILL_MS:
        return 0.0, 0.0
    if t_ms < STILL_MS + LOOK_MS:
        each = LOOK_MS // len(LOOKS)
        i, into = divmod(t_ms - STILL_MS, each)
        k = min(into, each - into, 1000) / 1000.0  # 1 s ease in and out of each look
        yaw, pitch = LOOKS[i]
        return yaw * k, pitch * k
    if t_ms < STILL_MS + LOOK_MS + LOOK_DOWN_MS:
        into = t_ms - STILL_MS - LOOK_MS
        k = min(into, STILL_MS + LOOK_MS + LOOK_DOWN_MS - t_ms, 1000) / 1000.0
        return 0.0, PITCH_DOWN_DEG * k
    return 0.0, 0.0


def build(lead_ms):
    out = _lib.lead_in(lead_ms)
    hands = {}  # ms after the lead-in -> input (each hand: grip 1 s, then grip + trigger 1 s, the other hand idle)
    base = STILL_MS + LOOK_MS + LOOK_DOWN_MS + 2000  # 30 s
    for n, side in enumerate(("right", "left")):
        t0 = base + n * 2000
        hands[t0] = {f"{side}_grip": 1.0}
        hands[t0 + 1000] = {f"{side}_grip": 1.0, f"{side}_trigger": 1.0}
    for rel in range(0, TOTAL_MS, STEP_MS):
        mid = rel + STEP_MS // 2
        yaw, pitch = _look_at(mid)
        inp = {"move_x": 0.0}
        for t0, h in hands.items():
            if t0 <= rel < t0 + 1000:
                inp = dict(h)
        out.append({"start_ms": lead_ms + rel, "end_ms": lead_ms + rel + STEP_MS, "input": inp,
                    "head_pitch_degrees": round(pitch, 3), "head_pose": _head(yaw, pitch)})
    return out
