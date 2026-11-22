"""Gameplay-script scenario (DOTO, dual wield, v145745): the arm that did not choose keeps its Voltaic Gun, a left forearm copy, the charged copy shot.

Wheel choices are stood in for by the mod's test commands (`d2vr voltaic ...`, src/dxgi_probe.cpp run_test_command):
  both, then "drop left" (the left hand "chose another item": a commit by it, the engine's slot put away) -> the right keeps its copy (arms 2,
      status voltaic.arm_events[2] kept) and its chord fires with the engine's gun no longer equipped (fire.fired rises);
  both again (the gun not native) -> a copy on EACH forearm (voltaic.copy 3); the left chord fires the left copy;
  charge on, the right chord held 2.6 s then released -> one charged copy shot (voltaic.charge[3] charged creates);
  right only -> the left chord fires nothing.
The save holds only a few charges: the most important shot first. build(lead_ms) returns the segment list for tools/simulated_gameplay_script.py.
"""
import importlib.util
from pathlib import Path

_spec = importlib.util.spec_from_file_location("d2vr_scenario_lib", Path(__file__).resolve().parents[1] / "scenario_lib.py")
_lib = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_lib)

PITCH = -25
TOTAL_MS = 75000


def build(lead_ms):
    out = _lib.lead_in(lead_ms)
    t = lead_ms

    def seg(inp, ms):
        nonlocal t
        out.append({"start_ms": t, "end_ms": t + ms, "head_pitch_degrees": PITCH, "input": dict(inp)})
        t += ms

    def console(line):
        nonlocal t
        out.append(_lib.console(t, line))
        t += _lib.FRAME_MS

    def chord(side, hold_ms=400):
        seg({side + "_grip": 1.0}, 1400)
        seg({side + "_grip": 1.0, side + "_trigger": 1.0}, hold_ms)
        seg({side + "_grip": 1.0}, 300)

    seg({"move_x": 0.0}, 2000)
    console("d2vr voltaic both")
    seg({"move_x": 0.0}, 4000)
    console("d2vr voltaic drop left")
    seg({"move_x": 0.0}, 5000)          # the engine's gun put away; the right keeps its copy
    chord("right")                       # fires with the gun unequipped
    seg({"move_x": 0.0}, 1500)
    console("d2vr voltaic both")
    seg({"move_x": 0.0}, 5000)          # a copy on each forearm (screenshots)
    chord("left")                        # the left copy fires
    seg({"move_x": 0.0}, 1500)
    console("d2vr voltaic charge on")
    chord("right", 2600)                 # held past 2 s: the charged shot on release
    seg({"move_x": 0.0}, 1500)
    console("d2vr voltaic right")
    seg({"move_x": 0.0}, 3000)
    chord("left")                        # must not fire
    seg({"move_x": 0.0}, 2000)
    out.append({"start_ms": t, "end_ms": lead_ms + TOTAL_MS, "head_pitch_degrees": 0, "input": {"move_x": 0.0}})
    return out
