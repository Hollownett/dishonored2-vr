"""Track T C2 variant: A held from 4.5 s to 13 s after the console line (the hold-A-0.6 s skip injects a held Interact for the rest of it).
See cutscene_skip.py for the original two shorter holds.

G2/G3 cutscene skip (camera plan Task 6): the `cutscene` scenario (SLOT22 via `loadgame 22`, the 10 s void_01 opening cinematic),
then a native skip attempt: A held 4 s (5 s to 9 s after the console line), then B held 3 s (9.5 s to 12.5 s), as menu-only segments
(outside gameplay: the load, its confirm prompt and the cinematic; never in gameplay, where A jumps). In the Task 6 runs the cinematic
ran from about 3.8 s to 14 s after the console line, so both holds fall inside it, and the time the exact-32 run ends names the button.
(Three 1.5 s A holds did not skip it: run 20261025T053625.)

Used by `linux/d2vr-linux sim --candidate-dll ... --scenario cutscene_skip --knob cutscene-mode=<mode> --lead 45 --seconds 120`.
Judged on the status (logic_mask, scripted_view.cutscene): the exact-32 run ends early and the return to logic 2 is clean.
"""
import importlib.util
from pathlib import Path

_spec = importlib.util.spec_from_file_location("d2vr_scenario_lib", Path(__file__).resolve().parents[1] / "scenario_lib.py")
L = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(L)
_cut_spec = importlib.util.spec_from_file_location("d2vr_scenario_cutscene", Path(__file__).with_name("cutscene.py"))
_cut = importlib.util.module_from_spec(_cut_spec)
_cut_spec.loader.exec_module(_cut)

HOLDS = [("a", 4500, 13000)]  # (button, from, until) after the console line


def build(lead_ms):
    """The cutscene scenario's segments with its watch window split around the holds (segments never overlap)."""
    segments = _cut.build(lead_ms)
    watch = segments.pop()  # the cutscene scenario ends with its single neutral watch segment
    assert watch["start_ms"] == lead_ms + L.FRAME_MS and watch["end_ms"] == lead_ms + _cut.WATCH_MS, watch
    neutral = lambda t0, t1: {"start_ms": t0, "end_ms": t1, "head_pitch_degrees": 0, "input": dict(L.NEUTRAL)}
    t = watch["start_ms"]
    for button, start, end in HOLDS:
        segments.append(neutral(t, lead_ms + start))
        segments.append({"start_ms": lead_ms + start, "end_ms": lead_ms + end, "input": {button: True}, "when": "menu"})
        t = lead_ms + end
    segments.append(neutral(t, watch["end_ms"]))
    return segments
