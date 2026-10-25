"""G2 cutscene capture (camera plan Task 4 spike): load a save that plays an in-engine cinematic (game_logic_mask 32) and keep
every input off while it plays, so nothing skips it. The probe (knob build/cinematic-probe, optional text "stereo" / "offset", see
src/dxgi_probe.cpp) logs artifacts/cinematic-probe.csv; tools/linux/cinematic_probe_report.py summarises it.

Used by `linux/d2vr-linux sim --candidate-dll ... --scenario cutscene --knob cinematic-probe[=stereo offset] --lead 45 --seconds 120`
(the rig caps a session at 120 s). `--test-save` does not work for this: Steam Cloud puts the user's slots back at launch and Continue
loads the newest one. So the scenario loads the cutscene slot itself with the engine console (`loadgame <slot>`, gameplay script v8)
once Continue has reached gameplay. Loading never writes a save; anything the game autosaves is undone by the rig's save restore.
The slot is chosen in .superpowers/sdd/2026-10-25-camera-recenter-vr-cutscenes/g2-spike.md.

1. Lead-in: neutral, with menu-only A pulses for the first 24 s only (title -> Campaign -> Continue; the DLL itself confirms the
   post-load prompt, mask 1<<15). No A after that: a cinematic must not be skipped by our presses.
2. At the lead's end: `loadgame SLOT`, then 69 s of standing still, head straight, no buttons or sticks (the load, the cinematic,
   then whatever follows).
"""
import importlib.util
from pathlib import Path

_spec = importlib.util.spec_from_file_location("d2vr_scenario_lib", Path(__file__).resolve().parents[1] / "scenario_lib.py")
L = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(L)

PULSE_UNTIL_MS = 24000  # menu A pulses stop here (the menus are passed by ~20 s after the session starts)
WATCH_MS = 69000        # lead 45 s + 69 s + the rig's 6 s = the 120 s session cap
SLOT = 22               # the user's SLOT22: campaign/void/void_01/void_01_p, a mission-start save (g2-spike.md)


def build(lead_ms):
    segments = [s for s in L.lead_in(min(lead_ms, PULSE_UNTIL_MS))]
    cursor = max(s["end_ms"] for s in segments) if segments else 0
    if cursor < lead_ms:
        segments.append({"start_ms": cursor, "end_ms": lead_ms, "head_pitch_degrees": 0, "input": dict(L.NEUTRAL)})
    segments.append(L.console(lead_ms, "loadgame %d" % SLOT))
    segments.append({"start_ms": lead_ms + L.FRAME_MS, "end_ms": lead_ms + WATCH_MS, "head_pitch_degrees": 0, "input": dict(L.NEUTRAL)})
    return segments
