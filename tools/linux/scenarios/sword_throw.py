"""Gameplay-script scenario: draw the sword at the scabbard zone, wind up, throw it forward and let go mid-swing (DOTO: v145744's
"the thrown sword hangs in the air"). Twice: after the first throw the second draw at the zone takes a new sword from the scabbard
(or the one lying there).

What a working throw shows in the status JSON's sword_grip block: "thrown" [hits, flight_frames, peak, release speed, ...] with
flight_frames rising tens per throw, "copy".pos moving along the throw and dropping, "copy".trace with hit entries.
The scabbard zone and pitch are sword_regrab.py's (offset [-0.03, 0.25, 0.50], hand pitch +60 draws forward).
"""
import importlib.util
from pathlib import Path

_spec = importlib.util.spec_from_file_location("d2vr_scenario_lib", Path(__file__).resolve().parents[1] / "scenario_lib.py")
_lib = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_lib)

ZONE = [-0.03, 0.25, 0.50]
BACK = [0.15, 0.25, 0.30]      # wound up beside the head
FRONT = [0.10, 0.20, -0.65]    # arm stretched forward: 0.95 m in 0.15 s, ~6 m/s at the release
NEUTRAL = {"move_x": 0.0}
GRIP = {"right_grip": 1.0}
TOTAL_MS = 40000


def build(lead_ms):
    out = _lib.lead_in(lead_ms)
    t = lead_ms

    def seg(ms, grip, offset, pitch=0):
        nonlocal t
        out.append({"start_ms": t, "end_ms": t + ms, "head_pitch_degrees": 0, "input": {**NEUTRAL, **(GRIP if grip else {})},
                    "right_hand_offset": list(offset), "hand_pitch_degrees": pitch})
        t += ms

    def glide(ms, steps, a, b, grip):
        nonlocal t
        for i in range(steps):
            k = (i + 1) / steps
            seg(round(ms / steps), grip, _lib.lerp(a, b, k))

    for _ in range(2):
        seg(1000, False, ZONE, 60)          # settle the empty hand at the scabbard
        seg(2500, True, ZONE, 60)           # draw
        glide(800, 4, ZONE, BACK, True)     # wind up
        seg(400, True, BACK)
        glide(150, 5, BACK, FRONT, True)    # the throw
        seg(4000, False, FRONT)             # let go: the sword flies
        seg(1500, False, [0.0, 0.0, 0.0])
    out.append({"start_ms": t, "end_ms": lead_ms + TOTAL_MS, "head_pitch_degrees": 0, "input": dict(NEUTRAL)})
    return out
