"""Gameplay-script scenario: draw the sword, drop it, and regrab it the other way round, twice (G2: a regrab gives one clean grip).

Used by `linux/d2vr-linux sim --scenario sword_regrab --knob sword-grip-trace --lead 45 --seconds 120`; the DLL writes artifacts/sword-grip-trace.csv
(one row per displayed frame) and `tools/linux/bonelab_run_check.py RUN --scenario sword_regrab` judges each hold in it.

Timeline (each step >= 2 s; the scabbard zone and pitch are Task 8's: offset [-0.03, 0.25, 0.50], pitch +60 draws forward, -60 draws or picks up reversed):
 1 draw forward at the zone; 2 slow sweep (sword_sweep phase 1); 3 release away from the zone (drop); 4 regrab reversed at the zone (the copy is there);
 5 sweep; 6 drop; 7 regrab forward; 8 sweep; 9 drop; 10 regrab reversed; 11 sweep; 12 release in the zone (sheathe).
Steps 4, 7 and 10 first settle the empty hand at the zone for 1 s, then close the grip. Holds: F, R, F, R.
"""
import importlib.util
from pathlib import Path

_spec = importlib.util.spec_from_file_location("d2vr_scenario_sword_sweep", Path(__file__).with_name("sword_sweep.py"))
_sweep = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_sweep)

ZONE = [-0.03, 0.25, 0.50]
GRIP = {"right_grip": 1.0}
SETTLE_MS, DRAW_MS, DROP_MS, SWEEP_HALVES, TAIL_MS = 1000, 2500, 2500, 4, 2000


def _seg(t, ms, grip, offset, pitch=0):
    return {"start_ms": t, "end_ms": t + ms, "head_pitch_degrees": 0, "input": {**_sweep.NEUTRAL, **(GRIP if grip else {})},
            "right_hand_offset": list(offset), "hand_pitch_degrees": pitch}


def build(lead_ms):
    segments = _sweep._oil._lead_in(lead_ms)
    t = lead_ms

    def add(seg):
        nonlocal t
        segments.append(seg)
        t = seg["end_ms"]

    def grab(pitch):
        add(_seg(t, SETTLE_MS, False, ZONE, pitch))
        add(_seg(t, DRAW_MS, True, ZONE, pitch))

    def sweep():
        nonlocal t
        segments.extend({**s, "input": {**s["input"], **GRIP}} for s in _sweep._sweep(t, _sweep.SLOW_HALF_MS, _sweep.SLOW_STEPS, SWEEP_HALVES))
        t += _sweep.SLOW_HALF_MS * SWEEP_HALVES

    grab(60)                                   # 1 draw forward
    sweep()                                    # 2
    for pitch in (-60, 60, -60):               # 3..11: drop, regrab, sweep
        add(_seg(t, DROP_MS, False, _sweep.LEFT))
        grab(pitch)
        sweep()
    add(_seg(t, DRAW_MS, True, ZONE, 60))      # 12 back to the zone with the grip held, then let go there (sheathe)
    add(_seg(t, DRAW_MS, False, ZONE, 60))
    add(_seg(t, TAIL_MS, False, _sweep.LEFT))
    return segments
