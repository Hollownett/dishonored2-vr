"""Gameplay-script scenario (both games, GitHub issue #6): the grip cancels a held power aim; a plain release still casts.

The power hand is the slot-1 hand (left by default; DOTO pins the left forearm). Needs a hold-to-aim power in that slot (D2: Blink,
Far Reach, Domino...; DOTO: Displace). Timeline after the lead-in, head level, hands at the simulated default pose:
  0 s       `d2vr equip displace` (the power into the slot-1 hand through its inventory record)
  0-3 s     idle (the equip animation)
  3 cycles of 6 s: trigger 1.5 s (aim), trigger + grip 0.8 s (the grip closes during the aim: cancel), both released 3.7 s
  21-27 s   control: trigger 1.5 s then released (the game casts: a Displace marker / a Blink)
  27-60 s   idle
Read after the run (xr-live-status.json, diagnostics knob on): power_grip.cancels 3, power_cancel.applied 3 (StopAim before the
release handler), power_cancel.no_aim 0; powers.* shows the aim during each hold; the control cast leaves the power's cast state
(DOTO: powers.swapper_has_target). With build/no-power-grip-cancel (A/B baseline): power_grip absent/disabled, each grip casts.
"""
import importlib.util
from pathlib import Path

_spec = importlib.util.spec_from_file_location("d2vr_scenario_lib", Path(__file__).resolve().parents[1] / "scenario_lib.py")
_lib = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_lib)

TOTAL_MS = 60000
POWER = "displace"   # the power put in the slot-1 hand first (`d2vr equip <power>`); D2 runs: build(lead, power="far_reach")


def build(lead_ms, power=POWER):
    out = _lib.lead_in(lead_ms)
    t = lead_ms

    def seg(inp, ms):
        nonlocal t
        out.append({"start_ms": t, "end_ms": t + ms, "head_pitch_degrees": 0, "input": dict(inp)})
        t += ms

    out.append(_lib.console(t, "d2vr equip " + power))
    t += _lib.FRAME_MS
    seg({"move_x": 0.0}, 3000)
    for _ in range(3):
        seg({"left_trigger": 1.0}, 1500)
        seg({"left_trigger": 1.0, "left_grip": 1.0}, 800)
        seg({"move_x": 0.0}, 3700)
    seg({"left_trigger": 1.0}, 1500)
    seg({"move_x": 0.0}, 4500)
    out.append({"start_ms": t, "end_ms": lead_ms + TOTAL_MS, "head_pitch_degrees": 0, "input": {"move_x": 0.0}})
    return out
