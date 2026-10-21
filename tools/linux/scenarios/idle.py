"""Gameplay-script scenario: stand still for 90 s with slow head sway, no stick input. A comfort/performance baseline.

Used by `linux/d2vr-linux sim --scenario idle`. build(lead_ms) returns the segment list for tools/simulated_gameplay_script.py:
the neutral lead-in with A pulses on menus only (same as oil_tank_drop, so title -> Continue advance by themselves), then
90 s of standing still. The head pitches +-10 degrees slowly (10 s cycle); there is no stick input and the hands stay at
their default offsets. Yaw: the gameplay-script format has no yaw field (only head_pitch_degrees, or absolute head_pose in
tracking space), so the yaw sway comes from the simulated XR backend itself (src/simulated_xr.hpp wait_frame: sin(frame*0.02)
* 0.12 rad, about +-7 degrees, whenever a script is active), not the +-20 degrees over 10 s asked for.
"""
import importlib.util
import math
from pathlib import Path

_spec = importlib.util.spec_from_file_location("d2vr_scenario_oil_tank_drop", Path(__file__).with_name("oil_tank_drop.py"))
_oil = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_oil)

IDLE_MS = 90000
STEP_MS = 500
PITCH_DEG, PITCH_PERIOD_MS = 10.0, 10000


def build(lead_ms):
    segments = _oil._lead_in(lead_ms)
    for t in range(lead_ms, lead_ms + IDLE_MS, STEP_MS):
        mid = t + STEP_MS / 2 - lead_ms
        segments.append({
            "start_ms": t, "end_ms": t + STEP_MS, "input": {"move_x": 0.0},
            "head_pitch_degrees": round(PITCH_DEG * math.sin(2 * math.pi * mid / PITCH_PERIOD_MS), 3)})
    return segments
