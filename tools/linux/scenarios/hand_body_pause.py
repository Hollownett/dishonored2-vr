"""Plan B task B13: the production hand body through a pause (`linux/d2vr-linux sim --scenario hand_body_pause --knob hand-body`).

The hand body itself runs inside the DLL (src/hand_body_runtime.hpp): after 8 s of gameplay (in-run G1) it creates one 5 cm sphere per tracked
hand at the calibrated palm (route R1) and wakes it once; the bodies fall and rest (B13 does not drive them yet). At lead + 25 s this script
presses the menu button (200 ms, when=always): the release gate must destroy both bodies on a dispatcher tick BEFORE the game pauses (the
0x89fb0 dispatcher does not run while paused, B11 run 3) and Start must reach the game only after that. At lead + 28 s A resumes (B11 run 3:
the pause menu's first entry is Resume); the bodies are re-created after the release settles, and destroyed again at the session end.
Results: artifacts/hand-body-events.csv (create / wake / destroy_release / destroy / abandon with the logic mask), the "hand_body" object in
xr-live-status.json, hand-body.txt (the knobs as read). Same segment format and lead-in as oil_tank_drop.py. build(lead_ms) -> segments.
"""
import importlib.util
from pathlib import Path

_spec = importlib.util.spec_from_file_location("d2vr_scenario_lib", Path(__file__).resolve().parents[1] / "scenario_lib.py")
L = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(L)
MENU_AT_MS, RESUME_AT_MS, END_MS = 25000, 28000, 54000


def build(lead_ms):
    t = lead_ms
    segments = L.lead_in(lead_ms)
    segments.append({"start_ms": t, "end_ms": t + MENU_AT_MS, "head_pitch_degrees": 0, "input": dict(L.NEUTRAL)})
    segments.append({"start_ms": t + MENU_AT_MS, "end_ms": t + MENU_AT_MS + 200, "input": {"menu": True}, "when": "always"})
    segments.append({"start_ms": t + MENU_AT_MS + 200, "end_ms": t + RESUME_AT_MS, "input": dict(L.NEUTRAL), "when": "always"})
    segments.append({"start_ms": t + RESUME_AT_MS, "end_ms": t + RESUME_AT_MS + 200, "input": {"a": True}, "when": "always"})
    segments.append({"start_ms": t + RESUME_AT_MS + 200, "end_ms": t + END_MS, "head_pitch_degrees": 0, "input": dict(L.NEUTRAL), "when": "always"})
    return segments
