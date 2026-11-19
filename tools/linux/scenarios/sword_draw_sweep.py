"""Gameplay-script scenario: draw the sword with the game's holster toggle (as holster_draw part A), then sweep it left and right (as
sword_sweep) with the grip held.

Made for the spacewarp held-weapon check (docs/SPACEWARP-PLAN-2026-11-19.md section 12): sword_sweep alone sweeps whatever the save has
drawn (the user's latest save: empty hands), and the scabbard-zone draw of run_hands_sword drew nothing in that save. Timeline after the
lead-in: head up, grip closed, B (the sword comes into the closed hand), hold 2.5 s; hold left 1 s; slow sweeps 6.4 s (0.75 m/s); hold
left 1 s; fast sweeps 4.08 s (~3.5 m/s; longer than sword_sweep's so a capture command, polled every 2-3 s, lands in it); tail 4 s.
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
_sweep = _load("d2vr_scenario_sword_sweep", Path(__file__).with_name("sword_sweep.py"))
FRONT = [-0.05, -0.05, -0.15]
GRIP = {"right_grip": 1.0}
UP = 25          # head pitch while pressing B: nothing to interact with
FAST_HALVES = 24


def build(lead_ms):
    segments = _lib.lead_in(lead_ms)
    t = lead_ms

    def seg(ms, inp, offset, head=0):
        nonlocal t
        segments.append({"start_ms": t, "end_ms": t + ms, "head_pitch_degrees": head, "input": dict(inp), "right_hand_offset": list(offset)})
        t += ms

    seg(1500, dict(_lib.NEUTRAL), FRONT, UP)
    seg(800, GRIP, FRONT, UP)
    seg(250, {**GRIP, "b": True}, FRONT, UP)
    seg(2500, GRIP, FRONT)
    seg(1000, GRIP, _sweep.LEFT)

    def sweeps(half_ms, steps, halves):
        nonlocal t
        for s in _sweep._sweep(t, half_ms, steps, halves):
            s["input"] = {**s["input"], **GRIP}
            segments.append(s)
        t += half_ms * halves

    sweeps(_sweep.SLOW_HALF_MS, _sweep.SLOW_STEPS, _sweep.SLOW_HALVES)
    seg(1000, GRIP, _sweep.LEFT)
    sweeps(_sweep.FAST_HALF_MS, _sweep.FAST_STEPS, FAST_HALVES)
    seg(_sweep.TAIL_MS, GRIP, _sweep.LEFT)
    return segments
