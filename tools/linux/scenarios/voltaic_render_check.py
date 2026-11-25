"""Gameplay-script scenario (DOTO, plan Tasks 9 and 10; user: "make sure that voltaic gun and bolt type is rendered correctly").

Both forearms carry a Voltaic Gun (`d2vr voltaic both`: the engine's gun on the left, the mod's copy on the right). The hands are raised
in front of the face (head pitched down 15 deg) so both guns are in view, and eye frames are saved with `d2vr spacewarp capture N`
(sw-capture-NN-*.rgba + .json in the artifacts; tools: scratch rgba->png converter). Timeline after the lead-in:
  0 s     voltaic both; `d2vr voltaic bolts` (both hands' ammo names into simulated-gameplay-console.txt)
  4 s     capture 2 (both guns, default bolts); then the left forearm alone in the middle of the view (capture 2), then the right one
  6 s     `d2vr voltaic bolt right 1` (the right copy switches to the gun's second ammo entry), 2.5 s settle, `voltaic bolts`, capture 2
  11 s    right chord (grip 1.4 s, then grip + trigger 0.4 s): one copy shot with the new bolt; capture 3 at the trigger edge
  15 s    `d2vr voltaic bolt left 1` (the engine gun), 2.5 s, `voltaic bolts`, capture 2
  20 s    left chord: one native shot; capture 3 at the trigger edge
Read: the console replies (bolt names per hand), fire.bolts / voltaic_bolts in xr-live-status.json, fire.shots (projectile defs), and look at
the captures: each gun drawn on its own forearm in place (no lag/ghost between captures), the right copy showing its own (changed) bolt.
"""
import importlib.util
from pathlib import Path

_spec = importlib.util.spec_from_file_location("d2vr_scenario_lib", Path(__file__).resolve().parents[1] / "scenario_lib.py")
_lib = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_lib)

TOTAL_MS = 60000
PITCH = -15
RIGHT_UP = [-0.06, 0.24, -0.02]   # the right hand raised ~0.24 m and slightly inward: the forearm crosses the view
LEFT_UP = [0.06, 0.24, -0.02]


def build(lead_ms):
    out = _lib.lead_in(lead_ms)
    t = lead_ms

    def seg(inp, ms):
        nonlocal t
        out.append({"start_ms": t, "end_ms": t + ms, "head_pitch_degrees": PITCH, "input": dict(inp),
                    "right_hand_offset": list(RIGHT_UP), "left_hand_offset": list(LEFT_UP)})
        t += ms

    def console(line):
        nonlocal t
        out.append(_lib.console(t, line))
        t += _lib.FRAME_MS

    console("d2vr voltaic both")
    console("d2vr ammo infinite on")   # 2026-11-25: the inventory's own infinite-ammo field, so every copy shot is not refused no_ammo
    console("d2vr voltaic bolts")
    seg({"move_x": 0.0}, 4000)
    console("d2vr spacewarp capture 2")
    # each forearm brought to the middle of the view (the other hand down), so either gun fills the frame
    t0 = t
    for side, up in (("left", [0.22, 0.30, 0.05]), ("right", [-0.22, 0.30, 0.05])):
        down = [0.0, -0.25, 0.1]
        out.append({"start_ms": t, "end_ms": t + 1500, "head_pitch_degrees": -25, "input": {"move_x": 0.0},
                    "left_hand_offset": list(up if side == "left" else down), "right_hand_offset": list(up if side == "right" else down)})
        t += 1500
        console("d2vr spacewarp capture 2")
        out.append({"start_ms": t, "end_ms": t + 800, "head_pitch_degrees": -25, "input": {"move_x": 0.0},
                    "left_hand_offset": list(up if side == "left" else down), "right_hand_offset": list(up if side == "right" else down)})
        t += 800
    seg({"move_x": 0.0}, 2000)
    console("d2vr voltaic bolt right 1")
    seg({"move_x": 0.0}, 2500)
    console("d2vr voltaic bolts")
    console("d2vr spacewarp capture 2")
    seg({"move_x": 0.0}, 2500)
    seg({"right_grip": 1.0}, 1400)
    console("d2vr spacewarp capture 3")
    seg({"right_grip": 1.0, "right_trigger": 1.0}, 400)
    seg({"move_x": 0.0}, 1500)
    console("d2vr voltaic bolt left 1")
    seg({"move_x": 0.0}, 2500)
    console("d2vr voltaic bolts")
    console("d2vr spacewarp capture 2")
    seg({"move_x": 0.0}, 2000)
    seg({"left_grip": 1.0}, 1400)
    console("d2vr spacewarp capture 3")
    seg({"left_grip": 1.0, "left_trigger": 1.0}, 400)
    seg({"move_x": 0.0}, 2000)
    out.append({"start_ms": t, "end_ms": lead_ms + TOTAL_MS, "head_pitch_degrees": 0, "input": {"move_x": 0.0}})
    return out
