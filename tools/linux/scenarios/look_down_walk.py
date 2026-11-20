"""Gameplay-script scenario: look down at the player's own body (legs, feet) while standing, walking, running and turning.

Made for spacewarp's body rule (user, headset v1431 2026-11-20: "player body (especially legs) still has a lot of ghosting"): looking down
the legs and feet are in view, so `spacewarp capture N` triples show how generated frames move them. The captures are taken by the
script itself (console lines "d2vr <test command>", v1432) at fixed points of each phase: the runner's own --command times drift by
several seconds against the script from run to run. build(lead_ms) returns the segment list for tools/simulated_gameplay_script.py: the
neutral lead-in shared with oil_tank_drop, then (head pitch in brackets) stand 2 s [-45] (spacewarp always, the A/B frame without the
body rule), walk (move_y .5) 4 s [-45], run (move_y 1) 3 s [-45], walk 3 s [-80], turn right while walking 2.5 s [-60], stand 2 s [-60],
two triples captured 1.5 s into each moving phase; then stand level until 90 s of gameplay have passed (the user's save leaves gameplay
~15 s in: the authored camera).
"""
import importlib.util
from pathlib import Path

_spec = importlib.util.spec_from_file_location("d2vr_scenario_oil_tank_drop", Path(__file__).with_name("oil_tank_drop.py"))
_oil = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_oil)

CAPTURE = "d2vr spacewarp capture 2"
# (ms, head pitch, input, console line at the segment's start or None)
SEGMENTS = (
    (1000, -45, {"move_y": 0.0}, "d2vr spacewarp always"),
    (1000, -45, {"move_y": 0.0}, "d2vr spacewarp altbody"),
    (1500, -45, {"move_y": 0.5}, None), (2500, -45, {"move_y": 0.5}, CAPTURE),
    (1500, -45, {"move_y": 1.0}, None), (1500, -45, {"move_y": 1.0}, CAPTURE),
    (1500, -80, {"move_y": 0.5}, None), (1500, -80, {"move_y": 0.5}, CAPTURE),
    (1000, -60, {"move_y": 0.5, "turn_x": 0.6}, None), (1500, -60, {"move_y": 0.5, "turn_x": 0.6}, CAPTURE),
    (2000, -60, {"move_y": 0.0}, None),
)
TOTAL_MS = 90000


def build(lead_ms):
    out = _oil._lead_in(lead_ms)
    t = lead_ms
    for ms, pitch, values, console in SEGMENTS:
        segment = {"start_ms": t, "end_ms": t + ms, "head_pitch_degrees": pitch, "input": dict(values)}
        if console:
            segment["console"] = console
        out.append(segment)
        t += ms
    out.append({"start_ms": t, "end_ms": lead_ms + TOTAL_MS, "head_pitch_degrees": 0, "input": {"move_y": 0.0}})
    return out
