"""Gameplay-script scenario: put a gadget back into the pouch and take it out again (plan 2026-11-26 Task 8, gadget stow).

Run per game: `D2VR_SPOT=doto|d2 linux/d2vr-linux sim --scenario gadget_stow --knob sim-invulnerable --lead 54 --seconds 120`.
DOTO holds the Hook Mine (`d2vr equip hook`, slot 1 = the left hand); D2 holds the regular grenade (`d2vr equip regular_grenade`).

Timeline after the lead (left hand only; right hand at its default; head pitch 0):
 1  `god`; equip the gadget (console) and 1.5 s; the left hand squeezes in front (LFRONT) to hold it, 1 s.
 2  glide 0.8 s to pouch_left, hold 0.5 s, open there (put back), hold 1.5 s.
 3  squeeze at pouch_left (empty hand), pull out (0.10 m up and 0.05 m forward in 0.4 s), hold 1.5 s (taken out).
 4  glide 0.8 s back to pouch_left, hold 0.5 s, open (put back again), hold 1.5 s.
 5  control: squeeze at the pouch, take it out again, glide in front to (-0.10, 0.10, -0.10) and open the grip slowly over 1 s (today's drop).
Pass: holsters.stows_gadget >= 2 and holsters.draws >= 2; dual-wield-events.jsonl has "gadget_put_back" and "pouch_draw" events; the
mine count (NativeWeaponHands slot1 count in the status) is unchanged across a put-back.
"""
import importlib.util
import os
from pathlib import Path

_spec = importlib.util.spec_from_file_location("d2vr_scenario_lib", Path(__file__).resolve().parents[1] / "scenario_lib.py")
L = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(L)

EQUIP = {"doto": "d2vr equip hook", "d2": "d2vr equip regular_grenade"}
ZERO = [0.0, 0.0, 0.0]
NEU = {"move_x": 0.0}
LGRIP = {"left_grip": 1.0}
LFRONT = [0.05, -0.05, -0.15]   # the left hand in front, mirrored from holster_draw's FRONT
POUCH = [0.11, -0.33, 0.34]     # pouch_left
PULL = [0.11, -0.23, 0.29]      # pouch_left + 0.10 m up, 0.05 m forward
FAR = [-0.10, 0.10, -0.10]      # the control's drop spot, far from the body


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
    add(500, cons="god", left=LFRONT)
    add(1000, cons=EQUIP[os.environ.get("D2VR_SPOT", "doto")], left=LFRONT)
    add(1000, left=LFRONT, inp=LGRIP)
    # 2: put back
    glide(800, 8, LFRONT, POUCH, inp=LGRIP)
    add(500, left=POUCH, inp=LGRIP)
    add(1500, left=POUCH)
    # 3: take out
    add(500, left=POUCH, inp=LGRIP)
    glide(400, 4, POUCH, PULL, inp=LGRIP)
    add(1500, left=PULL, inp=LGRIP)
    # 4: put back again
    glide(800, 8, PULL, POUCH, inp=LGRIP)
    add(500, left=POUCH, inp=LGRIP)
    add(1500, left=POUCH)
    # 5: control, the drop in front of the body
    add(500, left=POUCH, inp=LGRIP)
    glide(400, 4, POUCH, PULL, inp=LGRIP)
    glide(800, 8, PULL, FAR, inp=LGRIP)
    add(300, left=FAR, inp={"left_grip": 0.66})
    add(300, left=FAR, inp={"left_grip": 0.33})
    add(400, left=FAR)
    add(1500, left=FAR)
    return segments
