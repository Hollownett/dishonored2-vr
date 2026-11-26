"""The holstered gun while moving (2026-11-26, user: "the weapon in the holster steady in movement"): Dishonored 2.

`d2vr equip pistol` (slot 1: the left hand), the left grip holds it once, the left hand goes to the left hip and lets go there: the pistol is
holstered and its copy shows at the hip. Then, hands at rest: walk forward 4 s, sprint 3 s (stick click), strafe right 2 s, turn right 2 s
(right stick), walk back 2 s, stand 2 s. Pass: xr-live-status.json hip_frames grows while moving and hip_jitter_mm [max, mean, n] stays small
(the copy keeps its place on the body: max <= 10 mm, mean <= 3 mm); a lag behind the body shows as a mean near the body's per-frame step.
`D2VR_SPOT=d2 tools/linux/sim_serial.sh LOG sim --scenario holster_walk --lead 54 --knob diagnostics --candidate-dll ...`
"""
import importlib.util
from pathlib import Path

_spec = importlib.util.spec_from_file_location("d2vr_scenario_lib", Path(__file__).resolve().parents[1] / "scenario_lib.py")
L = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(L)

HIP_LEFT = [-0.01, -0.38, 0.40]   # the left hip in left-hand offset coordinates (docs/research/2026-11-26-holsters-collisions/lab-facts.md)
FRONT = [0.05, 0.05, 0.10]


def build(lead_ms):
    segments = L.lead_in(lead_ms)
    t = lead_ms

    def seg(ms, inp, left=FRONT, right=(0.0, 0.0, 0.0)):
        nonlocal t
        segments.append({"start_ms": t, "end_ms": t + ms, "head_pitch_degrees": 0, "input": dict(inp),
                         "left_hand_offset": list(left), "right_hand_offset": list(right)})
        t += ms

    segments.append(L.console(t, "d2vr equip pistol"))
    t += 1500
    seg(600, {"left_grip": 1.0})                                # the grip holds it once
    for k in range(8):                                          # to the hip, still holding
        a = [FRONT[i] + (HIP_LEFT[i] - FRONT[i]) * k / 8 for i in range(3)]
        seg(100, {"left_grip": 1.0}, left=a)
    seg(500, {"left_grip": 1.0}, left=HIP_LEFT)
    seg(1500, dict(L.NEUTRAL), left=HIP_LEFT)                   # let go at the hip: holstered
    seg(800, dict(L.NEUTRAL), left=FRONT)
    seg(4000, {"move_y": 1.0}, left=FRONT)                      # walk
    seg(3000, {"move_y": 1.0, "left_click": True}, left=FRONT)  # sprint
    seg(2000, {"move_x": 1.0}, left=FRONT)                      # strafe
    seg(2000, {"turn_x": 1.0}, left=FRONT)                      # turn
    seg(2000, {"move_y": -1.0}, left=FRONT)                     # back
    seg(2000, dict(L.NEUTRAL), left=FRONT)
    return segments
