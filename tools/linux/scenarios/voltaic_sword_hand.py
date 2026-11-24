"""Gameplay-script scenario (DOTO, dual wield, Task 8): the sword hand blocks its gun crosshair; the sword block works next to a right copy.

Steps (status read from xr-live-status.json; A/B with build/no-sword-hand-reticle):
  1. `d2vr voltaic both`, 4 s: both forearms carry a gun; crosshair.copy_ring.published[1] rises (baseline), hand_sword_bits 0.
  2. The right grip draws the sword at the shoulder zone (sword_throw.py's zone/pitch), held 3 s: with the fix hand_sword_bits == 2,
     crosshair.copy_ring.published[1] stays FLAT and suppressed[1] rises; published[0] (left ring) keeps rising; crosshair.visible stays true
     (the aim hand is the left); fire.fired flat.
  3. Right trigger (grip held) 0.5 s with the sword held = block: the trigger reaches the game (fixed: no native-trigger kill; baseline: the
     block counter stays flat); voltaic.stats[0..2] (copy asks/queued/refused) stay 0 and fire.fired stays flat.
  4. Sheathe (the hand back at the zone, grip released), idle 2 s: hand_sword_bits 0, published[1] rises again >= 150 ms after the release.
  5. Left grip + trigger fires the left gun (fire.fired rises, stats[3] rises).
build(lead_ms) returns the segment list for tools/simulated_gameplay_script.py.
"""
import importlib.util
from pathlib import Path

_spec = importlib.util.spec_from_file_location("d2vr_scenario_lib", Path(__file__).resolve().parents[1] / "scenario_lib.py")
_lib = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_lib)

ZONE = [-0.03, 0.25, 0.50]     # the right shoulder holster (sword_throw.py)
IDLE = [0.0, 0.0, 0.0]
PITCH = -25
TOTAL_MS = 40000


def build(lead_ms):
    out = _lib.lead_in(lead_ms)
    t = lead_ms

    def seg(ms, inp=None, offset=None, hand_pitch=0):
        nonlocal t
        s = {"start_ms": t, "end_ms": t + ms, "head_pitch_degrees": PITCH, "input": {"move_x": 0.0, **(inp or {})}}
        if offset is not None:
            s["right_hand_offset"] = list(offset)
            s["hand_pitch_degrees"] = hand_pitch
        out.append(s)
        t += ms

    def console(line):
        nonlocal t
        out.append(_lib.console(t, line))
        t += _lib.FRAME_MS

    seg(2000)
    console("d2vr voltaic both")
    seg(4000)                                              # baseline: both rings published
    seg(1000, None, ZONE, 60)                              # settle the empty right hand at the holster
    seg(3000, {"right_grip": 1.0}, ZONE, 60)               # draw and hold the sword 3 s
    seg(500, {"right_grip": 1.0, "right_trigger": 1.0}, ZONE, 60)   # right trigger with the sword: the block
    seg(1500, {"right_grip": 1.0}, ZONE, 60)
    seg(1500, None, ZONE, 60)                              # release at the zone: sheathe
    seg(2000, None, IDLE)                                  # idle: the right ring returns
    seg(1400, {"left_grip": 1.0})
    seg(400, {"left_grip": 1.0, "left_trigger": 1.0})      # the left gun fires
    seg(300, {"left_grip": 1.0})
    seg(2000)
    out.append({"start_ms": t, "end_ms": lead_ms + TOTAL_MS, "head_pitch_degrees": 0, "input": {"move_x": 0.0}})
    return out
