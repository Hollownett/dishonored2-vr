"""Gameplay-script scenario (DOTO, plan Task 6): the VR trigger hold that switches the Hook Mine between lethal and non-lethal.

Run with dual wield ON (the default; GadgetGrip, src/throw_gesture.hpp). The left grip is held for the whole run (the mine is held by the
grip) and the left hand is raised into view (head pitched down 25 deg) so the mine's colour (red lethal / blue non-lethal) shows in the captures.
Timeline after the lead-in:
  0 s     `d2vr equip hook`, 3 s idle
  3 s     `d2vr spacewarp capture 1` (the colour before), trigger held 1.2 s (mode hold), released, 1 s
          capture 1 (the colour after), trigger held 1.2 s again, 1 s, capture 1 (the colour back)
          a 0.2 s trigger tap (the throw), 3 s
Expected xr-live-status.json: gadget_mode.holds 2, switch_releases 2, taps 1; powers.hook_mines drops by one only after the tap; no
mine_throw.throw_place / gadget_drop.flights from the long holds (dual-wield-events.jsonl: mine_mode_hold / mine_mode_release x2).
"""
import importlib.util
from pathlib import Path

_spec = importlib.util.spec_from_file_location("d2vr_scenario_lib", Path(__file__).resolve().parents[1] / "scenario_lib.py")
_lib = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_lib)

TOTAL_MS = 30000
LEFT_UP = [0.2, 0.3, 0.05]


def build(lead_ms):
    out = _lib.lead_in(lead_ms)
    t = lead_ms

    def seg(inp, ms):
        nonlocal t
        i = {"left_grip": 1.0}
        i.update(inp)
        out.append({"start_ms": t, "end_ms": t + ms, "head_pitch_degrees": -25, "input": i, "left_hand_offset": list(LEFT_UP)})
        t += ms

    def console(line):
        nonlocal t
        out.append(_lib.console(t, line))
        t += _lib.FRAME_MS

    console("d2vr equip hook")
    seg({}, 3000)
    console("d2vr spacewarp capture 1")
    seg({"left_trigger": 1.0}, 1200)
    seg({}, 1000)
    console("d2vr spacewarp capture 1")
    seg({"left_trigger": 1.0}, 1200)
    seg({}, 1000)
    console("d2vr spacewarp capture 1")
    seg({"left_trigger": 1.0}, 200)
    seg({}, 3000)
    out.append({"start_ms": t, "end_ms": lead_ms + TOTAL_MS, "head_pitch_degrees": 0, "input": {"move_x": 0.0}})
    return out
