"""Gameplay-script scenario: turn the head continuously while walking, then while standing. The pose-versus-image check for the
paired (two-view) render path: a layer must carry the pose its image was rendered with, which only shows when the head moves
every frame (`tools/alt_pose_trace_check.py` on `alt-pose-trace.csv`, knob `alt-pose-trace`).

Used by `linux/d2vr-linux sim --scenario head_turn_walk`. build(lead_ms) returns the segment list for tools/simulated_gameplay_script.py:
the neutral lead-in with A pulses on menus only (copied from oil_tank_drop so title -> Continue advance by themselves), then 45 s of
forward walking (move_y) and 45 s of standing still, with the head yawing +-40 degrees over a 5 s cycle (peak 50 deg/s, a slow
real head turn) and pitching +-8 degrees over a 7 s cycle throughout. Head poses are absolute tracking-space
[x, y, z, qx, qy, qz, qw] (script version 5) in 100 ms back-to-back segments; the script runner lerps between them, so the
pose changes every frame.
"""
import importlib.util
import math
from pathlib import Path

_spec = importlib.util.spec_from_file_location("d2vr_scenario_oil_tank_drop", Path(__file__).with_name("oil_tank_drop.py"))
_oil = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_oil)
_idle_spec = importlib.util.spec_from_file_location("d2vr_scenario_idle", Path(__file__).with_name("idle.py"))
_idle = importlib.util.module_from_spec(_idle_spec)
_idle_spec.loader.exec_module(_idle)

TOTAL_MS = 90000
WALK_MS = 45000
STEP_MS = 100
EYE = (0.0, 1.65, 0.0)  # src/simulated_xr.hpp default head position
YAW_DEG, YAW_PERIOD_MS = 40.0, 5000
PITCH_DEG, PITCH_PERIOD_MS = 8.0, 7000


def build(lead_ms):
    segments = _oil._lead_in(lead_ms)
    for t in range(lead_ms, lead_ms + TOTAL_MS, STEP_MS):
        mid = t + STEP_MS / 2 - lead_ms
        yaw = YAW_DEG * math.sin(2 * math.pi * mid / YAW_PERIOD_MS)
        pitch = PITCH_DEG * math.sin(2 * math.pi * mid / PITCH_PERIOD_MS)
        segments.append({
            "start_ms": t, "end_ms": t + STEP_MS, "input": {"move_y": 1.0 if t - lead_ms < WALK_MS else 0.0},
            "head_pitch_degrees": round(pitch, 3),
            "head_pose": [*EYE, *[round(v, 6) for v in _idle.head_quaternion(yaw, pitch)]]})
    return segments
