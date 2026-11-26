"""E1 (plan 2026-11-26 Task 1): do the mod's world casts see a LIVING guard? (hand clamp and the blade cast)

Death of the Outsider lab save (chapter 2): `god`, then `d2vr guard behind 0.7` puts the eye 0.7 m behind the nearest unaware guard,
facing his back (his back is ~0.5 m ahead). A: the open right hand (default 0.4 m ahead) moves to his upper back and on 0.35 m into him at
0.1 m/s, holds 2 s, and comes back. B: the sword is drawn at the shoulder (as holster_draw.py), the hand comes in front low (blade
horizontal, its tip already past his back), holds 2 s, backs off 0.15 m and pushes in again slowly. Run with --knob no-stealth-contact
--knob no-contact-damage so the blade does not take him down. Read xr-live-status.json: weapon_collision.contact.hits (per side:
world, npc, dynamic, self, held), hand_clamp.contact_frames / state.
"""
import importlib.util
from pathlib import Path

_spec = importlib.util.spec_from_file_location("d2vr_scenario_lib", Path(__file__).resolve().parents[1] / "scenario_lib.py")
L = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(L)

ZONE = [-0.03, 0.25, 0.50]        # the shoulder scabbard (holster_draw.py)
BACK = [-0.15, 0.05, 0.0]         # the hand at his upper back height, centred on him
INTO = [-0.15, 0.05, -0.35]       # 0.35 m on into him
LOW_BACK = [-0.15, -0.05, 0.25]   # sword hand pulled back, 0.15 m ahead of the eye
LOW_IN = [-0.15, -0.05, 0.10]
GRIP = {"right_grip": 1.0}
OPEN = dict(L.NEUTRAL)


def build(lead_ms):
    segments = L.lead_in(lead_ms)
    t = lead_ms

    def hold(ms, inp, off, pitch=0):
        nonlocal t
        segments.append({"start_ms": t, "end_ms": t + ms, "head_pitch_degrees": 0, "input": dict(inp),
                         "right_hand_offset": list(off), "hand_pitch_degrees": pitch})
        t += ms

    def glide(ms, a, b, steps, inp, pitch=0):
        nonlocal t
        for s in L.glide(t, t + ms, a, b, steps, inp, pitch=0):
            s["hand_pitch_degrees"] = pitch
            segments.append(s)
        t += ms

    segments.append(L.console(t, "god"))
    t += 300
    segments.append(L.console(t, "d2vr guard behind 0.7"))
    t += 1200
    hold(800, OPEN, BACK)
    glide(3500, BACK, INTO, 35, OPEN)          # A: open hand into his back at 0.1 m/s
    hold(2000, OPEN, INTO)
    glide(1500, INTO, BACK, 15, OPEN)
    segments.append(L.console(t, "d2vr guard behind 0.7"))   # he may have stepped away: put us behind him again
    t += 1200
    glide(800, BACK, ZONE, 8, OPEN)            # B: draw the sword at the shoulder
    hold(600, OPEN, ZONE, 60)
    hold(150, GRIP, ZONE, 60)
    glide(400, ZONE, LOW_BACK, 6, GRIP)
    hold(2000, GRIP, LOW_BACK)
    glide(1500, LOW_BACK, [-0.15, -0.05, 0.40], 15, GRIP)   # back off
    glide(3000, [-0.15, -0.05, 0.40], LOW_IN, 30, GRIP)     # and in again slowly
    hold(2000, GRIP, LOW_IN)
    glide(800, LOW_IN, ZONE, 8, GRIP)
    hold(1000, GRIP, ZONE, 60)
    hold(2000, OPEN, ZONE, 60)                 # sheathe
    return segments
