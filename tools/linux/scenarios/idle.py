"""Gameplay-script scenario: stand still for 90 s with slow head sway, no stick input. A comfort/performance baseline.

Used by `linux/d2vr-linux sim --scenario idle`. build(lead_ms) returns the segment list for tools/simulated_gameplay_script.py:
the neutral lead-in with A pulses on menus only (same as oil_tank_drop, so title -> Continue advance by themselves), then
90 s of standing still. The head yaws +-20 degrees over a 10 s cycle and pitches +-10 degrees (15 s cycle); there is no stick
input and the hands stay at their default offsets.

Yaw has no degrees field in the script format, so it is authored as an absolute tracking-space head_pose (script version 5):
[x, y, z, qx, qy, qz, qw] with the simulated rig's standing eye position (0, 1.65, 0) and the yaw*pitch quaternion. head_pitch_degrees
is kept in the same segments so the pitch also holds if a runner ignores head_pose.
"""
import importlib.util
import math
from pathlib import Path

_spec = importlib.util.spec_from_file_location("d2vr_scenario_oil_tank_drop", Path(__file__).with_name("oil_tank_drop.py"))
_oil = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_oil)

IDLE_MS = 90000
STEP_MS = 200
EYE = (0.0, 1.65, 0.0)  # src/simulated_xr.hpp default head position
YAW_DEG, YAW_PERIOD_MS = 20.0, 10000
PITCH_DEG, PITCH_PERIOD_MS = 10.0, 15000


def head_quaternion(yaw_deg, pitch_deg):
    """yaw about +y times pitch about +x, as [qx, qy, qz, qw]."""
    ya, pa = math.radians(yaw_deg) / 2, math.radians(pitch_deg) / 2
    yx, yy, yz, yw = 0.0, math.sin(ya), 0.0, math.cos(ya)
    px, py, pz, pw = math.sin(pa), 0.0, 0.0, math.cos(pa)
    return [yw * px + yx * pw + yy * pz - yz * py,
            yw * py - yx * pz + yy * pw + yz * px,
            yw * pz + yx * py - yy * px + yz * pw,
            yw * pw - yx * px - yy * py - yz * pz]


def build(lead_ms):
    segments = _oil._lead_in(lead_ms)
    for t in range(lead_ms, lead_ms + IDLE_MS, STEP_MS):
        mid = t + STEP_MS / 2 - lead_ms
        yaw = YAW_DEG * math.sin(2 * math.pi * mid / YAW_PERIOD_MS)
        pitch = PITCH_DEG * math.sin(2 * math.pi * mid / PITCH_PERIOD_MS)
        segments.append({
            "start_ms": t, "end_ms": t + STEP_MS, "input": {"move_x": 0.0},
            "head_pitch_degrees": round(pitch, 3),
            "head_pose": [*EYE, *[round(v, 6) for v in head_quaternion(yaw, pitch)]]})
    return segments
