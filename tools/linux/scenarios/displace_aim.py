"""Gameplay-script scenario (DOTO, plan Tasks 11 and 12): Displace aims along the left hand, then swaps; the swap fades the view.

Timeline after the lead-in (absolute poses, script v5: the head level at 1.65 m, the left hand held out 0.4 m ahead, yawed ~30 deg to the
right of the head, the right hand at rest):
  0 s       `d2vr equip displace`, 3 s idle (the equip animation)
  3-4.5 s   LT held 1.5 s (aim: the Swapper target should follow the HAND ray, < 5 deg from hand forward, > 20 deg from head forward)
  4.5-6.5 s LT released, 2 s (the aim is cancelled: no swap on release without a second press in this power's flat control)
  6.5-6.8 s LT tapped 0.3 s (the swap: player state 93 rising edge)
  6.8-9.8 s idle
Read (xr-live-status.json): powers.swapper.aiming true during the hold with has_target + target along the hand ray (compare with the head
position from the status pose block); powers.state_entries.swapper (state 93) +1; teleport_fade.fades +1 (120 ms), alpha_now back to 0 after
the window, teleport_fade.last_ms set; frames still paired, 0 faults. A/B: with build/no-teleport-fade teleport_fade.enabled false, fades 0.
"""
import importlib.util
from pathlib import Path

_spec = importlib.util.spec_from_file_location("d2vr_scenario_lib", Path(__file__).resolve().parents[1] / "scenario_lib.py")
_lib = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_lib)
_idle_spec = importlib.util.spec_from_file_location("d2vr_scenario_idle", Path(__file__).with_name("idle.py"))
_idle = importlib.util.module_from_spec(_idle_spec)
_idle_spec.loader.exec_module(_idle)

TOTAL_MS = 60000
POWER = "displace"
HEAD_Y = 1.65
HAND_YAW_DEG = -30.0                 # about +y: negative = to the right of the head
LEFT_HAND = (-0.15, 1.35, -0.4)      # held out ahead of the left shoulder
RIGHT_REST = (0.2, 1.3, -0.4)
IDENTITY = (0.0, 0.0, 0.0, 1.0)


def build(lead_ms, power=POWER):
    out = _lib.lead_in(lead_ms)
    t = lead_ms
    head = [0.0, HEAD_Y, 0.0, *[round(v, 6) for v in _idle.head_quaternion(0.0, 0.0)]]
    left = [*LEFT_HAND, *[round(v, 6) for v in _idle.head_quaternion(HAND_YAW_DEG, 0.0)]]

    def seg(inp, ms):
        nonlocal t
        out.append({"start_ms": t, "end_ms": t + ms, "input": dict(inp), "head_pitch_degrees": 0, "head_pose": list(head),
                    "left_hand_pose": list(left), "right_hand_pose": [*RIGHT_REST, *IDENTITY]})
        t += ms

    out.append(_lib.console(t, "d2vr equip " + power))
    t += _lib.FRAME_MS
    seg({"move_x": 0.0}, 3000)
    seg({"left_trigger": 1.0}, 1500)      # aim along the hand
    seg({"move_x": 0.0}, 2000)            # release
    seg({"left_trigger": 1.0}, 300)       # tap: the swap
    seg({"move_x": 0.0}, 3000)
    out.append({"start_ms": t, "end_ms": lead_ms + TOTAL_MS, "input": {"move_x": 0.0}, "head_pitch_degrees": 0, "head_pose": list(head),
                "left_hand_pose": list(left), "right_hand_pose": [*RIGHT_REST, *IDENTITY]})
    return out
