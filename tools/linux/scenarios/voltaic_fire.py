"""Gameplay-script scenario (DOTO): the forearm Voltaic Gun's fire chord, both forearms.

Head pitched slightly down. The save holds only a few Voltaic charges and the right-forearm copy fires through the native gun's
component (its ammo), so the copy goes first: right grip squeezed and held, then full trigger presses (each 0.4 s, 1.8 s apart: the
one-shot magazine reloads in about 1.1 s); the copy must fire (status dual_wield fire.fired / projectiles_created, build/voltaic-copy
on). Then the same chord on the left hand: the native gun must fire (its magazine empties and reloads). Then the left trigger alone
(no grip): it must NOT fire. build(lead_ms) returns the segment list for tools/simulated_gameplay_script.py.
"""
import importlib.util
from pathlib import Path

_spec = importlib.util.spec_from_file_location("d2vr_scenario_lib", Path(__file__).resolve().parents[1] / "scenario_lib.py")
_lib = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_lib)

PITCH = -10
TOTAL_MS = 60000


def build(lead_ms):
    out = _lib.lead_in(lead_ms)
    t = lead_ms
    seg = lambda inp, ms: out.append({"start_ms": t, "end_ms": t + ms, "head_pitch_degrees": PITCH, "input": dict(inp)})
    seg({"move_x": 0.0}, 2000); t += 2000
    for _ in range(2):                       # right grip + trigger: the right-forearm copy (if on)
        seg({"right_grip": 1.0}, 1400); t += 1400
        seg({"right_grip": 1.0, "right_trigger": 1.0}, 400); t += 400
    seg({"move_x": 0.0}, 1500); t += 1500
    for _ in range(2):                       # left grip + trigger: the native gun fires
        seg({"left_grip": 1.0}, 1400); t += 1400
        seg({"left_grip": 1.0, "left_trigger": 1.0}, 400); t += 400
    seg({"move_x": 0.0}, 1500); t += 1500
    for _ in range(3):                       # left trigger alone: must not fire
        seg({"left_trigger": 1.0}, 400); t += 400
        seg({"move_x": 0.0}, 1400); t += 1400
    out.append({"start_ms": t, "end_ms": lead_ms + TOTAL_MS, "head_pitch_degrees": 0, "input": {"move_x": 0.0}})
    return out
