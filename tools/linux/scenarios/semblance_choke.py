"""Gameplay-script scenario (DOTO, 2026-11-25; plan Task 11 "Semblance").

How the flat game does it (docs/doto-research/2026-11-25-R9-semblance-trigger.md): Semblance is NOT used from inside the ordinary choke (41,
locked body parts: a power press there is lost). With Semblance equipped, a fresh power press (ACTION_POWER_WEAPON = the native LT) behind
an unaware NPC starts UseMimic 102, which requests MIMIC_CHOKE 95 (the face-taking choke, held by LT), then Mimicking 96. On an NPC that is
already unconscious, the same press takes the face from the body (UseMimic result 1).

  `god` (engine console: an alerted guard must not end the session on a death screen), `d2vr guard list`, `d2vr equip semblance`
  attempt 1 (mimic choke): `d2vr guard behind 0.6 0`, 0.4 s, the power trigger (left, open hand) pressed 1.2 s, captures, a short walk
  attempt 2 (face from a body): `d2vr guard behind 0.6 1`, the right grip chokes him out (41, 4 s), head down at the body, the power trigger
Read back: artifacts/player-state-transitions.csv (102 -> 95 -> 96 for attempt 1; 41, then 102 -> 96 for attempt 2), powers.state_entries
(use_mimic, mimicking), power_grip.offer_lt [trigger frames while the choke is offered, LT sent, gates that released it], guard_target.state.
"""
import importlib.util
from pathlib import Path

_spec = importlib.util.spec_from_file_location("d2vr_scenario_lib", Path(__file__).resolve().parents[1] / "scenario_lib.py")
_lib = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_lib)

TOTAL_MS = 52000
NECK = [0.0, 0.25, -0.15]     # the right hand raised to his neck, 0.6 m ahead (offset from the simulated default hand)
REST = [0.0, 0.0, 0.0]
LOW = [0.0, -0.3, -0.2]       # both hands toward the body on the floor
MANA_LINE = "gp_manaEnable 0"   # set to "echo mana on" to keep the save's own mana rules


def build(lead_ms):
    out = _lib.lead_in(lead_ms)
    t = lead_ms

    def seg(inp, ms, right=REST, pitch=0, left=REST):
        nonlocal t
        out.append({"start_ms": t, "end_ms": t + ms, "head_pitch_degrees": pitch, "input": dict(inp),
                    "right_hand_offset": list(right), "left_hand_offset": list(left)})
        t += ms

    def console(line):
        nonlocal t
        out.append(_lib.console(t, line))
        t += _lib.FRAME_MS

    console("god")
    console(MANA_LINE)              # run 20261125T144443: the disguise went on (mimic +0x4c = 2) at the end of the face take and off 66 ms
                                    # later; a mana check (gp_manaEnable 0 for this run) tells whether the save had the energy to keep it
    console("d2vr voltaic none")   # the mod remembers the forearm guns across sessions: a pinned left gun owns the left trigger (its chord), as
                                   # in play until that hand's wheel picks a power (the test equip below bypasses the wheel)
    console("d2vr guard list")
    console("d2vr equip semblance")
    seg({"move_x": 0.0}, 2500)

    # attempt 1: the mimic choke, from a fresh power press behind an unaware guard
    console("d2vr guard behind 0.6 0")
    seg({"move_x": 0.0}, 400, REST, -10)
    console("d2vr spacewarp capture 2")
    seg({"left_trigger": 1.0}, 4000, REST, -10)        # Semblance, HELD (95 is held by LT like a choke button: run 20261125T143332 let go at
                                                         # 1.2 s, inside the grab-in, and the face was not taken; 95 runs ~3.8 s)
    seg({"move_x": 0.0}, 500, REST, -10)
    console("d2vr spacewarp capture 2")
    seg({"move_x": 0.0}, 2500)
    console("d2vr spacewarp capture 2")                  # disguised (Mimicking 96)
    seg({"move_y": 0.6}, 1500)                           # a short walk as the face's owner
    seg({"move_x": 0.0}, 1500)
    console("d2vr guard list")

    # attempt 2: choke the next guard out (41), then take the face from the body
    console("d2vr guard behind 0.6 1")
    seg({"move_x": 0.0}, 400, NECK, -10)
    seg({"right_grip": 1.0}, 4200, NECK, -10)           # the ordinary choke: 3 s held -> unconscious
    seg({"move_x": 0.0}, 2000, LOW, -50, LOW)            # look down at the body
    console("d2vr spacewarp capture 2")
    seg({"left_trigger": 1.0}, 600, LOW, -50, LOW)      # Semblance on the sleeping body
    seg({"move_x": 0.0}, 3000, LOW, -50, LOW)
    console("d2vr spacewarp capture 2")
    seg({"move_x": 0.0}, 1500)
    console("d2vr guard list")
    out.append({"start_ms": t, "end_ms": lead_ms + TOTAL_MS, "head_pitch_degrees": 0, "input": {"move_x": 0.0}})
    return out
