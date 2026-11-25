"""Gameplay-script scenario (DOTO, 2026-11-25; user: "Voltaic gun charged bolt works?").

Both forearms carry a Voltaic Gun (`d2vr voltaic both`). The Charged Shot upgrade (Black Market) is forced for the test with
`d2vr voltaic charge on`: the right copy's VoltaicCharge timing and the engine gun's charge bit (comp+0xc8 bit 2, probe_charge), so a save
without the upgrade charges as one with it would. `d2vr ammo infinite on` keeps the shots from being refused.
Each shot is followed by `d2vr voltaic chargestate` (the read-back goes into simulated-gameplay-console.txt):
  right plain   chord 0.4 s                    -> a copy shot, not charged
  right charged chord 2.6 s, release           -> copy_charged_shots +1, charged_creates +1 (the charged projectile def), capture at 2.2 s
  right over    chord 4.6 s                    -> fires by itself at 4 s: overcharged_creates +1
  left plain    chord 0.4 s                    -> the engine gun's release: native_e0 0
  left charged  chord 2.6 s, release           -> native_e0 1 (the engine's own charged release), capture at 2.2 s and after
  left over     chord 4.6 s                    -> native_e1 1 (the engine's 4 s auto fire, overcharged)
Captures: `d2vr spacewarp capture 2` (sw-capture-NN-*.rgba, tools/linux/rgba_to_png.py): the charge glow on each gun while held, the
charged projectile right after the release.
"""
import importlib.util
from pathlib import Path

_spec = importlib.util.spec_from_file_location("d2vr_scenario_lib", Path(__file__).resolve().parents[1] / "scenario_lib.py")
_lib = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_lib)

TOTAL_MS = 46000
PITCH = -15
RIGHT_UP = [-0.06, 0.24, -0.02]   # as voltaic_render_check: both forearms raised across the view
LEFT_UP = [0.06, 0.24, -0.02]


def build(lead_ms):
    out = _lib.lead_in(lead_ms)
    t = lead_ms

    def seg(inp, ms):
        nonlocal t
        out.append({"start_ms": t, "end_ms": t + ms, "head_pitch_degrees": PITCH, "input": dict(inp),
                    "right_hand_offset": list(RIGHT_UP), "left_hand_offset": list(LEFT_UP)})
        t += ms

    def console(line):
        nonlocal t
        out.append(_lib.console(t, line))
        t += _lib.FRAME_MS

    def shot(side, hold_ms, capture_at_ms=None):
        grip, trig = side + "_grip", side + "_trigger"
        seg({grip: 1.0}, 1400)                                   # the chord starts with the grip (as voltaic_render_check)
        if capture_at_ms is None:
            seg({grip: 1.0, trig: 1.0}, hold_ms)
        else:
            seg({grip: 1.0, trig: 1.0}, capture_at_ms)
            console("d2vr spacewarp capture 2")                  # the gun while it charges
            seg({grip: 1.0, trig: 1.0}, hold_ms - capture_at_ms)
        after = 120 if side == "right" else 300                  # the engine gun's shot leaves ~150 ms after the release (its fire animation)
        seg({"move_x": 0.0}, after)
        if capture_at_ms is not None:
            console("d2vr spacewarp capture 2")                  # the shot just after the release
        seg({"move_x": 0.0}, 1420 - after)
        console("d2vr voltaic chargestate")

    console("d2vr voltaic both")
    seg({"move_x": 0.0}, 3000)
    console("d2vr ammo infinite on")
    console("d2vr voltaic charge on")
    seg({"move_x": 0.0}, 2500)                                   # probe_charge runs once a second
    console("d2vr voltaic chargestate")
    shot("right", 400)
    shot("right", 2600, 2200)
    shot("right", 4600)
    shot("left", 400)
    shot("left", 2600, 2200)
    shot("left", 4600)
    console("d2vr voltaic charge off")
    seg({"move_x": 0.0}, 1500)
    console("d2vr voltaic chargestate")
    out.append({"start_ms": t, "end_ms": lead_ms + TOTAL_MS, "head_pitch_degrees": 0, "input": {"move_x": 0.0}})
    return out
