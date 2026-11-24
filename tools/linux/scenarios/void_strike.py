"""Gameplay-script scenario (DOTO): the Void Strike from the VR sword charge (plan 2026-11-24-doto-billie-vr Task 13; R2 3.4, R6).

Draw the sword as sword_draw_sweep does (head up, grip closed, B), then hold the right trigger still for 0.8 s with the sword held
and release; repeat once. Needs a save with the Void Strike attribute owned and mana >= decl+0xf0, a guard in front is optional.
Expected deltas in xr-live-status.json: `powers.void_strike.presses` +2, `powers.void_strike.last_press_ms` = commit*1000+60,
`powers.state_entries.cancel_void_strike` unchanged, mana drop; `sword_charge_starts` +2, `sword_power_attacks` +2.
Hand offsets: metres relative to the simulated default right hand (x right, y up, z backward).
"""
import importlib.util
from pathlib import Path


def _load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


_lib = _load("d2vr_scenario_lib", Path(__file__).resolve().parents[1] / "scenario_lib.py")
FRONT = [-0.05, -0.05, -0.15]
GRIP = {"right_grip": 1.0}
UP = 25          # head pitch while pressing B: nothing to interact with


def build(lead_ms):
    segments = _lib.lead_in(lead_ms)
    t = lead_ms

    def seg(ms, inp, head=0):
        nonlocal t
        segments.append({"start_ms": t, "end_ms": t + ms, "head_pitch_degrees": head, "input": dict(inp), "right_hand_offset": list(FRONT)})
        t += ms

    seg(1500, dict(_lib.NEUTRAL), UP)
    seg(800, GRIP, UP)
    seg(250, {**GRIP, "b": True}, UP)
    seg(2500, GRIP)
    for _ in range(2):
        seg(800, {**GRIP, "right_trigger": 1.0})   # charge: trigger held, hand still
        seg(150, GRIP)                              # release: the mod presses the attack for commit+60 ms
        seg(3500, GRIP)                             # the strike plays out
    seg(3000, GRIP)
    return segments
