"""Gameplay-script scenario: holster and unholster the weapons, then toss the sword up and catch it (v612 feedback 7 and 3).

Used by `linux/d2vr-linux sim --scenario holster_draw --knob sword-grip-trace --knob sim-invulnerable --lead 45 --seconds 120`, judged
by `tools/linux/bonelab_run_check.py RUN --scenario holster_draw` (each hold of the game's sword: the game's hand closed on the hilt from its
first frame). Before v613 a draw opened the hand around the hilt for ~0.45 s, and a frame after each catch drew the sword at the native joint.

Timeline after the lead (head level unless noted; hand offsets as in sword_regrab, the scabbard zone [-0.03, 0.25, 0.50]):
 A  look up (no interact target), close the right grip away from the zone, B: the game's own holster toggle draws the sword into
    the closed hand; hold it 2.5 s; B again (holster) and 2.5 s; B again (unholster) and 2.5 s.
 B  carry the held sword to the zone and let go there (sheathe), 2 s; squeeze at the zone (draw) and bring the hand in front of the eyes
    within 0.3 s, hold 2 s; sheathe and draw again.
 C  twice: the hand in front, a fast 0.2 m upward flick and let go at the top (the sword flies up), squeeze again 0.45 s later
    while the sword falls past the hand (catch), hold 2.5 s.
 End: release at the zone (sheathe) and a quiet tail.
"""
import importlib.util
from pathlib import Path

_spec = importlib.util.spec_from_file_location("d2vr_scenario_lib", Path(__file__).resolve().parents[1] / "scenario_lib.py")
_lib = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_lib)

ZONE = [-0.03, 0.25, 0.50]
FRONT = [-0.05, -0.05, -0.15]
TOP = [-0.05, 0.15, -0.15]
GRIP = {"right_grip": 1.0}
NEUTRAL = dict(_lib.NEUTRAL)
UP = 25  # head pitch while pressing B: nothing to interact with


def build(lead_ms):
    segments = _lib.lead_in(lead_ms)
    t = lead_ms

    def seg(ms, inp, offset, pitch=0, head=0):
        nonlocal t
        segments.append({"start_ms": t, "end_ms": t + ms, "head_pitch_degrees": head, "input": dict(inp),
                         "right_hand_offset": list(offset), "hand_pitch_degrees": pitch})
        t += ms

    def glide(ms, a, b, steps, inp, head=0):
        nonlocal t
        segments.extend(_lib.glide(t, t + ms, a, b, steps, inp, pitch=head))
        t += ms

    # A: the game's B toggle with the grip closed
    seg(1500, NEUTRAL, FRONT, head=UP)
    seg(800, GRIP, FRONT, head=UP)
    for _ in range(3):
        seg(250, {**GRIP, "b": True}, FRONT, head=UP)
        seg(2500, GRIP, FRONT, head=UP)
    # B: sheathe and draw at the zone, twice
    glide(800, FRONT, ZONE, 8, GRIP)
    for _ in range(2):
        seg(1000, GRIP, ZONE, 60)
        seg(2000, NEUTRAL, ZONE, 60)   # let go in the zone: sheathe
        seg(150, GRIP, ZONE, 60)       # squeeze in the zone: draw
        glide(300, ZONE, FRONT, 6, GRIP)  # bring it into view while the draw clip runs
        seg(2050, GRIP, FRONT)
        if _ == 0:
            glide(600, FRONT, ZONE, 6, GRIP)
    # C: toss and catch, twice
    for _ in range(2):
        seg(1500, GRIP, FRONT)
        glide(120, FRONT, TOP, 10, GRIP)  # 0.2 m in 0.12 s: launched at ~2.5 m/s (the drop scales the hand speed by 1.5)
        seg(450, NEUTRAL, TOP)          # let go at the top: the sword flies ~0.3 m up and falls back past the hand
        seg(2500, GRIP, TOP)            # catch
        glide(600, TOP, FRONT, 6, GRIP)
    glide(800, FRONT, ZONE, 8, GRIP)
    seg(1000, GRIP, ZONE, 60)
    seg(3000, NEUTRAL, ZONE, 60)
    return segments
