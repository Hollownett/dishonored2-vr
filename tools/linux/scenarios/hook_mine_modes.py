"""Gameplay-script scenario (DOTO, plan Task 5): which input switches the Hook Mine between lethal and non-lethal.

Run with dual wield OFF (`d2vr-linux --game doto feature off dual-wield`, back on afterwards): without dual wield a closed fist holds
the game's LT natively for a gadget (src/dxgi_probe.cpp, GadgetFist) and opening it lets LT go, so this measures the engine itself.
The flat game (guides, docs/doto-research/2026-11-24-R1-mechanics.md) switches the mode by holding the use button for a moment.
Timeline after the lead-in (head level):
  0 s        `d2vr equip hook`, 4 s idle (the equip)
  holds      left fist (grip + trigger) for 1.6 s, 0.9 s, 0.3 s (long first: the save has one mine), each followed by 5 s idle
  pulses     150 ms each of: x, y, b, right_click (R3), right_grip (RB), each followed by 2.5 s idle
Read: powers.state_entries.change_weapon (player state 73 entries), powers.hook_mines (the count drops by one per thrown mine),
powers.events ([tick, 1 = state entered / 2 = mine count, value]) against simulated-gameplay-commands.csv,
dual-wield-events.jsonl, controller-trace.csv (what LT the game got). Expected (flat game): the long holds enter state 73 without a
throw; the 0.3 s hold throws one mine.
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
        out.append({"start_ms": t, "end_ms": t + ms, "head_pitch_degrees": -10, "input": dict(inp)})
        t += ms

    out.append(_lib.console(t, "d2vr equip hook"))
    t += _lib.FRAME_MS
    seg({"move_x": 0.0}, 4000)
    for hold in (1600, 900, 300):   # the long holds first: the lab save holds one mine, and a throw uses it
        seg({"left_grip": 1.0, "left_trigger": 1.0}, hold)
        seg({"move_x": 0.0}, 5000)
    for button in ("x", "y", "b", "right_click"):
        seg({button: True}, 150)
        seg({"move_x": 0.0}, 2500)
    seg({"right_grip": 1.0}, 150)
    seg({"move_x": 0.0}, 2500)
    out.append({"start_ms": t, "end_ms": lead_ms + TOTAL_MS, "head_pitch_degrees": 0, "input": {"move_x": 0.0}})
    return out
