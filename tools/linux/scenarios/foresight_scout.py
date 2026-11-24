"""Gameplay-script scenario (DOTO, plan Task 15 step 1): observe Foresight in VR before changing anything.

Timeline after the lead-in (head level): `d2vr equip foresight`, 3 s idle; LT held 0.6 s then released (enter the spirit: the game moves the
player entity itself, R2 §2.2); 2 s still; walk forward (move_y 0.6) 3 s; snap-turn right (turn_x 1.0 for 0.15 s), 1 s; LT tap 0.3 s (leave:
the engine restores the body's origin and yaw), 4 s idle. Read: powers.theeye.state over time (powers.events has none for TheEye: compare the
final state 0 and the teleport_fade.fades count, 1 = the exit edge seen), full_body.visible / body counters, the camera mode while the spirit
moves (native_player_camera status), frames paired, 0 faults.
"""
import importlib.util
from pathlib import Path

_spec = importlib.util.spec_from_file_location("d2vr_scenario_lib", Path(__file__).resolve().parents[1] / "scenario_lib.py")
_lib = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_lib)

TOTAL_MS = 60000


def build(lead_ms):
    out = _lib.lead_in(lead_ms)
    t = lead_ms

    def seg(inp, ms):
        nonlocal t
        out.append({"start_ms": t, "end_ms": t + ms, "head_pitch_degrees": 0, "input": dict(inp)})
        t += ms

    out.append(_lib.console(t, "d2vr equip foresight"))
    t += _lib.FRAME_MS
    seg({"move_x": 0.0}, 3000)
    seg({"left_trigger": 1.0}, 600)
    seg({"move_x": 0.0}, 2000)
    seg({"move_y": 0.6}, 3000)
    seg({"turn_x": 1.0}, 150)
    seg({"move_x": 0.0}, 1000)
    seg({"left_trigger": 1.0}, 300)
    seg({"move_x": 0.0}, 4000)
    out.append({"start_ms": t, "end_ms": lead_ms + TOTAL_MS, "head_pitch_degrees": 0, "input": {"move_x": 0.0}})
    return out
