"""Gameplay-script scenario: put the Heart back into the left-chest pocket and into the hip holster, and take it out again
(plan 2026-11-26 Task 8, the Heart in the inventory). D2 only.

Run: `D2VR_SPOT=d2 linux/d2vr-linux sim --scenario heart_pocket --knob sim-invulnerable --lead 54 --seconds 120`.

Timeline after the lead (left hand only; right hand at its default; head pitch 0):
 1  `d2vr equip heart` (console) and 1.5 s; the left hand squeezes in front (LFRONT) to hold it, 1 s.
 2  glide 0.8 s to the Heart pocket (0.13, 0.05, 0.36), hold 0.5 s, open there (put away), hold 1.5 s.
 3  squeeze at the pocket (empty hand), pull out (0.10 m up, 0.05 m forward in 0.4 s), hold 1.5 s (taken out).
 4  glide 0.8 s to hip_left (-0.01, -0.38, 0.40), hold 0.5 s, open there (the old hip put-away), hold 1.5 s.
 5  glide 0.8 s back to the pocket, squeeze, pull out again, hold 1.5 s (taken out).
Pass: holsters.stows_heart >= 2 (pocket and hip) and holsters.draws >= 2.
"""
import importlib.util
from pathlib import Path

_spec = importlib.util.spec_from_file_location("d2vr_scenario_lib", Path(__file__).resolve().parents[1] / "scenario_lib.py")
L = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(L)

ZERO = [0.0, 0.0, 0.0]
NEU = {"move_x": 0.0}
LGRIP = {"left_grip": 1.0}
LFRONT = [0.05, -0.05, -0.15]
POCKET = [0.13, 0.05, 0.36]     # the Heart pocket, left chest
PULL = [0.13, 0.15, 0.31]       # pocket + 0.10 m up, 0.05 m forward
HIP = [-0.01, -0.38, 0.40]      # hip_left


def pose(t0, t1, left=ZERO, inp=NEU):
    return {"start_ms": t0, "end_ms": t1, "head_pitch_degrees": 0, "input": dict(inp),
            "right_hand_offset": list(ZERO), "left_hand_offset": list(left), "hand_pitch_degrees": 0}


def build(lead_ms):
    segments = L.lead_in(lead_ms)
    t = lead_ms

    def add(ms, cons=None, **kw):
        """A hold of `ms`; with `cons`, its first 11 ms are a console frame carrying the same pose."""
        nonlocal t
        if cons:
            segments.append(dict(pose(t, t + L.FRAME_MS, **kw), console=cons))
            segments.append(pose(t + L.FRAME_MS, t + ms, **kw))
        else:
            segments.append(pose(t, t + ms, **kw))
        t += ms

    def glide(ms, steps, a, b, inp=NEU):
        nonlocal t
        for i in range(steps):
            k = (i + 1) / steps
            segments.append(pose(t + ms * i // steps, t + ms * (i + 1) // steps, left=L.lerp(a, b, k), inp=inp))
        t += ms

    # 1
    add(1500, cons="d2vr equip heart", left=LFRONT)
    add(1000, left=LFRONT, inp=LGRIP)
    # 2: put away in the pocket
    glide(800, 8, LFRONT, POCKET, inp=LGRIP)
    add(500, left=POCKET, inp=LGRIP)
    add(1500, left=POCKET)
    # 3: take out of the pocket
    add(500, left=POCKET, inp=LGRIP)
    glide(400, 4, POCKET, PULL, inp=LGRIP)
    add(1500, left=PULL, inp=LGRIP)
    # 4: put away on the hip
    glide(800, 8, PULL, HIP, inp=LGRIP)
    add(500, left=HIP, inp=LGRIP)
    add(1500, left=HIP)
    # 5: back to the pocket, take out again
    glide(800, 8, HIP, POCKET)
    add(300, left=POCKET, inp=LGRIP)
    glide(400, 4, POCKET, PULL, inp=LGRIP)
    add(1500, left=PULL, inp=LGRIP)
    return segments
