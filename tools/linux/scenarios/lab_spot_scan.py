"""Lab spot scan (plan 2026-11-26 Task 1): find walls and tables near the lab save's start for the arm-collision scenarios.

At the save's start position (env D2VR_SPOT=doto|d2 picks the known start feet position, see START) the player is put back there
facing 12 headings 30 degrees apart (setviewpos, eye 1.65 m above the feet) and, at each, `d2vr trace 3` casts along the view with
the head pitched 15 degrees down (a wall is hit about 1.4 m above the floor at 1 m). One `d2vr guard list` first confirms the
position. Results: the replies in artifacts/simulated-gameplay-console.txt / vr-command-result.txt.
`D2VR_SPOT=doto tools/linux/sim_serial.sh LOG --game doto sim --scenario lab_spot_scan --settle 45 --lead 54 --knob diagnostics --candidate-dll ...`
"""
import importlib.util
import os
from pathlib import Path

_spec = importlib.util.spec_from_file_location("d2vr_scenario_lib", Path(__file__).resolve().parents[1] / "scenario_lib.py")
L = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(L)

START = {"doto": (-26.42, -52.02, 40.60), "d2": (66.44, -6.56, 32.95)}   # feet, from `d2vr guard list` in runs 20261125T124848 / 131404
EYE = 1.65
PITCH = -15
STEP_MS = 900


def build(lead_ms):
    x, y, z = START[os.environ.get("D2VR_SPOT", "doto")]
    segments = L.lead_in(lead_ms)
    t = lead_ms
    segments.append(L.console(t, "d2vr guard list"))
    t += 600
    for k in range(12):
        yaw = k * 30
        segments.append(L.console(t, L.setviewpos(x, y, z + EYE, yaw)))
        segments.append({"start_ms": t + L.FRAME_MS, "end_ms": t + 400, "head_pitch_degrees": PITCH, "input": dict(L.NEUTRAL)})
        segments.append(dict(L.console(t + 400, "d2vr trace 3"), head_pitch_degrees=PITCH))
        segments.append({"start_ms": t + 400 + L.FRAME_MS, "end_ms": t + STEP_MS, "head_pitch_degrees": PITCH, "input": dict(L.NEUTRAL)})
        t += STEP_MS
    segments.append(L.console(t, "d2vr trace 3 down"))
    segments.append({"start_ms": t + L.FRAME_MS, "end_ms": t + 3000, "head_pitch_degrees": 0, "input": dict(L.NEUTRAL)})
    return segments
