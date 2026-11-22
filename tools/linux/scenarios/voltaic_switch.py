"""Gameplay-script scenario (DOTO, dual wield): the Voltaic Gun moved between the forearms (v145744; user: "when I select it on my right arm,
it just updates it on my left arm").

The wheel's choice is stood in for by the mod's test command (`d2vr voltaic left|right|both`, what a wheel commit by that hand sets).
The save holds only a few charges, so each arm fires few shots:
  right only: the right chord fires the copy (twice), the left chord fires nothing (its gun hidden: status voltaic.native_hide);
  left only:  the left chord fires the engine's gun once, the right chord nothing (no copy);
  both:       the copy on the right again.
Head pitched down so the screenshots show both forearms. build(lead_ms) returns the segment list for tools/simulated_gameplay_script.py.
"""
import importlib.util
from pathlib import Path

_spec = importlib.util.spec_from_file_location("d2vr_scenario_lib", Path(__file__).resolve().parents[1] / "scenario_lib.py")
_lib = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_lib)

PITCH = -25
TOTAL_MS = 70000


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

    def chord(side, n):
        for _ in range(n):
            seg({side + "_grip": 1.0}, 1400)
            seg({side + "_grip": 1.0, side + "_trigger": 1.0}, 400)

    seg({"move_x": 0.0}, 2000)
    console("d2vr voltaic right")
    seg({"move_x": 0.0}, 6000)          # the copy spawns on the right, the left gun hides (screenshots)
    chord("right", 2)
    seg({"move_x": 0.0}, 1000)
    chord("left", 1)                     # must not fire
    seg({"move_x": 0.0}, 1500)
    console("d2vr voltaic left")
    seg({"move_x": 0.0}, 6000)          # the left gun shows again, the copy goes
    chord("left", 1)
    seg({"move_x": 0.0}, 1000)
    chord("right", 1)                    # must not fire
    seg({"move_x": 0.0}, 1500)
    console("d2vr voltaic both")
    seg({"move_x": 0.0}, 6000)
    out.append({"start_ms": t, "end_ms": lead_ms + TOTAL_MS, "head_pitch_degrees": 0, "input": {"move_x": 0.0}})
    return out
