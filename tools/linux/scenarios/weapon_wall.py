"""Gameplay-script scenario: weapons pushed into a wall (plan 2026-11-26 Task 8, weapon collisions).

Run per game: `D2VR_SPOT=doto|d2 linux/d2vr-linux sim --scenario weapon_wall --knob arm-collision --knob sim-invulnerable --lead 54 --seconds 120`.
Spot WALL_FRONT (eye 0.60 m from the wall). Judge against a baseline run without the weapon-collision knobs.

Timeline after the lead (head pitch 0, `god` first):
 1  setviewpos WALL_FRONT, settle 1 s; `god`.
 2  sword: squeeze at the shoulder ZONE (draw, hand pitch 60), bring it in front with the blade at the wall, hand 0.15 m ahead of the
    eye (-0.05, 0.0, 0.25), hand pitch 0.
 3  push slowly 0.30 m forward (z 0.25 -> -0.05) at 0.1 m/s (3 s) so the tip goes into the wall; hold 2 s; sweep 0.30 m sideways (-x)
    along the wall at 0.1 m/s and back; pull back to the ZONE and let go there (sheathe).
 4  D2 only (D2VR_SPOT=d2): `d2vr equip pistol`; the LEFT hand squeezes at hip_left (draws it), presses the barrel 0.30 m into the
    wall at 0.1 m/s, holds 2 s, sweeps 0.30 m sideways and back, returns to hip_left and lets go (holster). The right hand rests at ZONE.
Pass: weapon_collision.contact.frames > 0; contact.max_jump_mm and plane_changes_per_s not worse than the baseline; arm_collision active
without new jitter.
"""
import importlib.util
import os
from pathlib import Path

_spec = importlib.util.spec_from_file_location("d2vr_scenario_lib", Path(__file__).resolve().parents[1] / "scenario_lib.py")
L = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(L)

SPOTS = {"doto": {"x": -27.755, "y": -53.654, "z": 42.25, "yaw": -129.3},   # WALL_FRONT per game (lab-facts.md); the D2 one was used for both
         "d2": {"x": 66.440, "y": -6.560, "z": 34.60, "yaw": -11.5}}
ZERO = [0.0, 0.0, 0.0]
NEU = {"move_x": 0.0}
GRIP = {"right_grip": 1.0}
LGRIP = {"left_grip": 1.0}
ZONE, FRONT, PUSH, SIDE = [-0.03, 0.25, 0.50], [-0.05, 0.0, 0.25], [-0.05, 0.0, -0.05], [-0.35, 0.0, -0.05]
HIP_L = [-0.01, -0.38, 0.40]
LFRONT, LPUSH, LSIDE = [0.05, 0.0, 0.25], [0.05, 0.0, -0.05], [0.35, 0.0, -0.05]  # the left hand: x mirrored
SWORD_PITCH = 60


def pose(t0, t1, right=ZERO, left=ZERO, inp=NEU, pitch=0):
    return {"start_ms": t0, "end_ms": t1, "head_pitch_degrees": 0, "input": dict(inp),
            "right_hand_offset": list(right), "left_hand_offset": list(left), "hand_pitch_degrees": pitch}


def build(lead_ms):
    SPOT = SPOTS[os.environ.get("D2VR_SPOT", "doto")]
    segments = L.lead_in(lead_ms)
    t = lead_ms
    right_default = dict(right=ZONE, pitch=SWORD_PITCH)

    def add(ms, cons=None, **kw):
        """A hold of `ms`; with `cons`, its first 11 ms are a console frame carrying the same pose."""
        nonlocal t
        if cons:
            segments.append(dict(pose(t, t + L.FRAME_MS, **kw), console=cons))
            segments.append(pose(t + L.FRAME_MS, t + ms, **kw))
        else:
            segments.append(pose(t, t + ms, **kw))
        t += ms

    def glide(ms, steps, right=(ZERO, ZERO), left=(ZERO, ZERO), **kw):
        nonlocal t
        for i in range(steps):
            k = (i + 1) / steps
            segments.append(pose(t + ms * i // steps, t + ms * (i + 1) // steps,
                                 right=L.lerp(*right, k), left=L.lerp(*left, k), **kw))
        t += ms

    # 1
    add(1000, cons=L.setviewpos(SPOT["x"], SPOT["y"], SPOT["z"], SPOT["yaw"]), **right_default)
    add(500, cons="god", **right_default)
    # 2: draw at the zone, bring the blade to the wall
    add(800, **right_default)
    add(150, inp=GRIP, **right_default)
    add(1000, inp=GRIP, **right_default)
    glide(600, 6, right=(ZONE, FRONT), inp=GRIP)
    add(1000, right=FRONT, inp=GRIP)
    # 3: push, hold, sweep, back, sheathe
    glide(3000, 15, right=(FRONT, PUSH), inp=GRIP)
    add(2000, right=PUSH, inp=GRIP)
    glide(3000, 15, right=(PUSH, SIDE), inp=GRIP)
    glide(3000, 15, right=(SIDE, PUSH), inp=GRIP)
    glide(1500, 10, right=(PUSH, ZONE), inp=GRIP, pitch=SWORD_PITCH)
    add(1000, **right_default)
    # 4: D2 only, the pistol in the left hand
    if os.environ.get("D2VR_SPOT") == "d2":
        add(800, cons="d2vr equip pistol", **right_default)
        add(1000, left=HIP_L, **right_default)
        add(800, left=HIP_L, inp=LGRIP, **right_default)
        rest = dict(right=(ZONE, ZONE), pitch=SWORD_PITCH)  # glide takes (start, end) pairs
        glide(1000, 10, left=(HIP_L, LFRONT), inp=LGRIP, **rest)
        glide(3000, 15, left=(LFRONT, LPUSH), inp=LGRIP, **rest)
        add(2000, left=LPUSH, inp=LGRIP, **right_default)
        glide(3000, 15, left=(LPUSH, LSIDE), inp=LGRIP, **rest)
        glide(3000, 15, left=(LSIDE, LPUSH), inp=LGRIP, **rest)
        glide(1500, 10, left=(LPUSH, HIP_L), inp=LGRIP, **rest)
        add(1000, left=HIP_L, **right_default)
    return segments
