"""Gameplay-script scenario: open the pause menu, move to the VR Settings entry, open it, go back, close the menu.

Made for Death of the Outsider's in-game VR Settings entry (tools/doto/menu_patches.py, src/menu_patches_doto.hpp): the
pause movie's Stats entry is repurposed as "VR Settings" (list: resume, save, load, restart, options, VR Settings,
tutorials, quit), its click sends VR_SETTINGS_EVT and the mod opens the native options page; B goes back through the
patched lib_options back navigation. Works for Dishonored 2's pause menu as well (same list). The result file's
menu_entry_patched / menu_entry_clicks / native_options_page counters and window captures show what happened.
Every segment is 'always' (the pause menu is outside gameplay). build(lead_ms) returns the segment list for
tools/simulated_gameplay_script.py.
"""

import importlib.util
from pathlib import Path

_spec = importlib.util.spec_from_file_location("d2vr_scenario_lib", Path(__file__).resolve().parents[1] / "scenario_lib.py")
_lib = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_lib)

NEUTRAL = {"move_x": 0.0}
DOWN_TO_ENTRY = 5      # resume -> VR Settings
TOTAL_MS = 60000


def build(lead_ms):
    # The shared lead-in presses A on menus every 3 s (title -> Campaign -> Continue); without it a first session stays
    # on the title screen.
    out = _lib.lead_in(lead_ms)

    def seg(start, stop, inp):
        out.append({"start_ms": int(start), "end_ms": int(stop), "input": dict(inp), "when": "always"})
    seg(lead_ms, lead_ms + 2000, NEUTRAL)
    t = lead_ms + 2000
    seg(t, t + 200, {"menu": True})            # open the pause menu
    seg(t + 200, t + 2500, NEUTRAL)
    t += 2500
    for _ in range(DOWN_TO_ENTRY):            # 100 ms nudges: a 200 ms one sometimes auto-repeated (play18 overshot by one)
        seg(t, t + 100, {"move_y": -1.0})
        seg(t + 100, t + 800, NEUTRAL)
        t += 800
    seg(t, t + 200, {"a": True})               # VR Settings
    seg(t + 200, t + 6000, NEUTRAL)            # the native options page is up
    t += 6000
    seg(t, t + 200, {"b": True})               # back to the pause list (lib_options back navigation)
    seg(t + 200, t + 3000, NEUTRAL)
    t += 3000
    seg(t, t + 200, {"b": True})               # close the pause menu
    seg(t + 200, lead_ms + TOTAL_MS, NEUTRAL)
    return out
