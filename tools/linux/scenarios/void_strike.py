"""Gameplay-script scenario (DOTO): the Void Strike from the VR sword charge (plan 2026-11-24-doto-billie-vr Task 13; R2 3.4, R6).

Draw the sword as sword_draw_sweep does (head up, grip closed, B), then hold the right trigger still for 0.8 s with the sword held
and release; repeat once. Any save: `d2vr voidstrike own` sets the Void Strike attribute first (VoidStrikeTest; the slash reads it at its start), mana >= decl+0xf0.
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
ZONE = [-0.03, 0.25, 0.50]     # the right shoulder holster (sword_throw.py, voltaic_sword_hand.py)
GRIP = {"right_grip": 1.0}
UP = 25          # head pitch while pressing B: nothing to interact with


def build(lead_ms):
    segments = _lib.lead_in(lead_ms)
    t = lead_ms

    def seg(ms, inp, head=0, offset=FRONT, hand_pitch=None):
        nonlocal t
        s = {"start_ms": t, "end_ms": t + ms, "head_pitch_degrees": head, "input": dict(inp), "right_hand_offset": list(offset)}
        if hand_pitch is not None:
            s["hand_pitch_degrees"] = hand_pitch
        segments.append(s)
        t += ms

    segments.append(_lib.console(t, "d2vr voidstrike own"))   # 2026-11-25: the attribute set for saves without Void Strike (VoidStrikeTest)
    t += _lib.FRAME_MS
    # 2026-11-25: the sword is drawn at the right shoulder holster (as voltaic_sword_hand, sim-proven in DOTO); the B draw of the first
    # version never drew it in the lab save (hand_weapon_bits 0 in runs 20261124T230135 and 20261125T133517)
    seg(1000, dict(_lib.NEUTRAL), -25, ZONE, 60)
    seg(2000, GRIP, -25, ZONE, 60)                     # the grip at the holster draws the sword (head pitch as voltaic_sword_hand)
    seg(1500, GRIP)                                     # the sword brought in front, held
    for _ in range(2):
        seg(800, {**GRIP, "right_trigger": 1.0})   # charge: trigger held, hand still
        seg(150, GRIP)                              # release: the mod presses the attack for commit+60 ms
        seg(3500, GRIP)                             # the strike plays out
    seg(3000, GRIP)
    segments.append(_lib.console(t, "d2vr voidstrike off"))
    t += _lib.FRAME_MS
    seg(500, GRIP)
    return segments
