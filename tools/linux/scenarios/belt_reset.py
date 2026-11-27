"""The belt forgets on a load (2026-11-26, final-review follow-up: holster_forget_epoch). Both games.

A gadget is put back at the left belt pouch (as gadget_stow's steps 1-2), then the save is loaded again with the engine console
(`loadgame <slot>`: the lab saves' newest slots, DOTO 33 / D2 31; the loading screen is game logic 0, where the hand clamp forgets and the
holster state now does too). After the load, the empty left hand squeezes at the pouch and pulls out.
Pass (xr-live-status.json holsters + dual-wield-events.jsonl in this run's tick range): stows_gadget >= 1 before the load; after it the pull
gives "pouch_empty" (holsters.empty >= 1) and no "pouch_draw"; pouch_key is "" at the end. Without the reset the pouch would offer the
gadget it remembered from before the load. `D2VR_SPOT=doto|d2` (D2VR_GADGET as in gadget_stow), `--seconds 120`.
"""
import importlib.util
import os
from pathlib import Path

_spec = importlib.util.spec_from_file_location("d2vr_scenario_lib", Path(__file__).resolve().parents[1] / "scenario_lib.py")
L = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(L)

_gs = importlib.util.spec_from_file_location("d2vr_gadget_stow", Path(__file__).with_name("gadget_stow.py"))
G = importlib.util.module_from_spec(_gs)
_gs.loader.exec_module(G)

SLOT = {"doto": 33, "d2": 31}
LOAD_WAIT_MS = 36000


def build(lead_ms):
    spot = os.environ.get("D2VR_SPOT", "doto")
    segments = L.lead_in(lead_ms)
    t = lead_ms

    def add(ms, cons=None, **kw):
        nonlocal t
        if cons:
            segments.append(dict(G.pose(t, t + L.FRAME_MS, **kw), console=cons))
            segments.append(G.pose(t + L.FRAME_MS, t + ms, **kw))
        else:
            segments.append(G.pose(t, t + ms, **kw))
        t += ms

    def glide(ms, steps, a, b, inp=G.NEU):
        nonlocal t
        for i in range(steps):
            segments.append(G.pose(t + ms * i // steps, t + ms * (i + 1) // steps, left=L.lerp(a, b, (i + 1) / steps), inp=inp))
        t += ms

    add(500, cons="god", left=G.LFRONT)
    if spot == "d2":   # the D2 lab save holds no grenade: give it first (gadget_stow does the same)
        add(800, cons="d2vr give item %s 3" % G.EQUIP["d2"].split()[-1], left=G.LFRONT)
    add(1000, cons=G.EQUIP[spot], left=G.LFRONT)
    add(1000, left=G.LFRONT, inp=G.LGRIP)
    glide(800, 8, G.LFRONT, G.POUCH, inp=G.LGRIP)          # put back at the pouch
    add(500, left=G.POUCH, inp=G.LGRIP)
    add(1500, left=G.POUCH)
    add(600, left=G.LFRONT)
    add(LOAD_WAIT_MS, cons="loadgame %d" % SLOT[spot], left=G.LFRONT)   # load the save again
    add(1500, cons="god", left=G.LFRONT)
    glide(800, 8, G.LFRONT, G.POUCH)                         # the empty hand to the pouch
    add(500, left=G.POUCH, inp=G.LGRIP)                       # squeeze there and pull out: "empty" expected
    glide(400, 4, G.POUCH, G.PULL, inp=G.LGRIP)
    add(1500, left=G.PULL, inp=G.LGRIP)
    add(1000, left=G.PULL)
    return segments
