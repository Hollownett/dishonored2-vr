"""Gameplay-script scenario: pick up a whale-oil tank, lift it, then throw it down at the floor and let it settle.

Same segment format and lead-in as oil_tank_drop.py (see its docstring for the save it needs). The hand sweeps from the chest
to the floor in 0.3 s (about 3 m/s, above the 1.2 m/s that makes HeldRelease throw instead of drop) with the grip held, and
the grip opens on the last step, so the release is a throw along the hand's motion. The engine keeps its own throw speed
(held_throw_velocity); build/legacy-throw restores the old hand-velocity x1.5 replacement for a before/after comparison.
"""
import importlib.util
from pathlib import Path

_spec = importlib.util.spec_from_file_location("d2vr_scenario_oil_tank_drop", Path(__file__).with_name("oil_tank_drop.py"))
_oil = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_oil)
CHEST, CLOSE, FLOOR, GRIP, NEUTRAL, REACH = _oil.CHEST, _oil.CLOSE, _oil.FLOOR, _oil.GRIP, _oil.NEUTRAL, _oil.REACH
_glide, _hold, _lead_in = _oil._glide, _oil._hold, _oil._lead_in


def build(lead_ms):
    t = lead_ms
    segments = _lead_in(lead_ms)
    segments.append(_hold(t, t + 1500, NEUTRAL, REACH))           # settle the hand at the object
    segments.append(_hold(t + 1500, t + 2700, CLOSE, REACH))      # grab
    segments += _glide(t + 2700, t + 4200, REACH, CHEST, 15, GRIP)  # lift
    segments.append(_hold(t + 4200, t + 5500, GRIP, CHEST))       # carry at the chest
    mid = [round(a + (b - a) * 5 / 6, 4) for a, b in zip(CHEST, FLOOR)]
    segments += _glide(t + 5500, t + 5750, CHEST, mid, 5, GRIP)     # swing down at the floor
    segments += _glide(t + 5750, t + 5800, mid, FLOOR, 1, NEUTRAL)  # open the hand while it is still moving: the throw
    segments.append(_hold(t + 5800, t + 5900, NEUTRAL, FLOOR))
    segments.append(_hold(t + 5900, t + 12000, NEUTRAL, FLOOR))   # the tank bounces and settles
    return segments
