"""G1 scripted moments (camera plan Task 3): enter a mantle and a takedown with the head turned away from the action, so the
scripted-moment recenter (src/scripted_recenter.hpp) has a real entry turn to ease; judged by
`tools/linux/bonelab_run_check.py RUN --scenario authored_turn` on artifacts/scripted-view.csv (knob build/scripted-view-trace).

Used by `linux/d2vr-linux sim --scenario authored_turn --knob scripted-view-trace --lead 45 --seconds 120`. Spots are in the user's
latest save (campaign/institute/institute_p_highchaos, save start eye -7.16 60.08 35.70), found read-only with tools/entities.py over
/proc/PID/mem during sim runs (ledger: .superpowers/sdd/2026-10-25-camera-recenter-vr-cutscenes/g1-sim.md). The teleports are
gameplay-script v8 console lines (scenario_lib.console / setviewpos; setviewpos takes the EYE position and the body yaw in degrees,
game yaw: 0 = +x, counter-clockwise). The game walks where the head looks, so each teleport yaw is the wanted gaze minus the head yaw.

1. Lead-in as head_turn_walk (oil_tank_drop's: neutral, menu-only A pulses), then `ark_god_player 1` (sets the cvar, not a toggle).
2. Mantle: teleport in front of a ~1 m ledge in the big hall (the probe walk from (0, 60.08) toward -y mantled at y 57.0), head held
   at +40 deg yaw (absolute head_pose, script v5), walk at the ledge with A pulses (jump, then the game's own mantle).
3. Takedown: teleport 0.6 m behind a lone, unaware guard, head at -35 deg so the gaze is on him, then the grip alone (right_grip 0.7,
   no trigger: the offered choke) pulsed for 3 s (the choke needs a fresh press while it is offered), then held 4 s. "Behind" is
   MINUS his arkCpntReferenceFrame row 0 here: +row0 (vr_selftest --near npc) gave kill offers only, -row0 gave the choke offer.
4. Idle with the head straight.
In the sim the takedown was offered but never started (the guard turns and the player dies, ark_god_player 1 notwithstanding;
g1-sim.md), so the mantle alone is the gate (the check needs one judged entry).
"""
import importlib.util
from pathlib import Path

_spec = importlib.util.spec_from_file_location("d2vr_scenario_lib", Path(__file__).resolve().parents[1] / "scenario_lib.py")
L = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(L)
_idle_spec = importlib.util.spec_from_file_location("d2vr_scenario_idle", Path(__file__).with_name("idle.py"))
_idle = importlib.util.module_from_spec(_idle_spec)
_idle_spec.loader.exec_module(_idle)

EYE = (0.0, 1.65, 0.0)  # src/simulated_xr.hpp default head position
MANTLE_HEAD_DEG, TAKEDOWN_HEAD_DEG = 40.0, -35.0
MANTLE_EYE, MANTLE_GAZE_DEG = (0.0, 59.0, 35.70), -90.0  # big hall floor z 34.0; the ledge is ~2 m toward -y
# guard_male_small_01.def_9276 stands still at (11.69, 46.90, 20.01), reference-frame row 0 (0.635, 0.772): 0.6 m against it, facing him.
GUARD_EYE, GUARD_GAZE_DEG = (11.31, 46.44, 21.71), 50.6
STEP_MS = 100


def _pose(t0, t1, inp, yaw):
    return {"start_ms": t0, "end_ms": t1, "input": dict(inp), "head_pitch_degrees": 0,
            "head_pose": [*EYE, *[round(v, 6) for v in _idle.head_quaternion(yaw, 0)]]}


def build(lead_ms):
    segments = L.lead_in(lead_ms)
    t = lead_ms
    segments.append(L.console(t, "ark_god_player 1"))
    t += L.FRAME_MS
    segments.append(_pose(t, lead_ms + 1000, L.NEUTRAL, 0))
    # 2. mantle, head at +40: the teleport body yaw puts the gaze (and the walk) on the ledge
    t = lead_ms + 1000
    segments.append(L.console(t, L.setviewpos(*MANTLE_EYE, MANTLE_GAZE_DEG - MANTLE_HEAD_DEG)))
    t += L.FRAME_MS
    segments.append(_pose(t, t + 1500, L.NEUTRAL, MANTLE_HEAD_DEG))
    t += 1500
    for _ in range(6):  # walk at the ledge, A pressed for 0.2 s every 0.7 s: a jump at the ledge becomes the native mantle
        segments.append(_pose(t, t + 200, {"move_y": 1.0, "a": True}, MANTLE_HEAD_DEG))
        segments.append(_pose(t + 200, t + 700, {"move_y": 1.0}, MANTLE_HEAD_DEG))
        t += 700
    segments.append(_pose(t, t + 2000, L.NEUTRAL, MANTLE_HEAD_DEG))
    t += 2000
    # 3. takedown, head at -35: teleport behind the guard, the grip alone 0.6 s after landing, held 6 s
    segments.append(L.console(t, L.setviewpos(*GUARD_EYE, GUARD_GAZE_DEG - TAKEDOWN_HEAD_DEG)))
    t += L.FRAME_MS
    for _ in range(12):  # the choke needs a fresh grip press while the game offers it (vr_controls.hpp OfferedChokeGrip): pulse it
        segments.append(_pose(t, t + 150, {"right_grip": 0.7}, TAKEDOWN_HEAD_DEG))
        segments.append(_pose(t + 150, t + 250, L.NEUTRAL, TAKEDOWN_HEAD_DEG))
        t += 250
    segments.append(_pose(t, t + 4000, {"right_grip": 0.7}, TAKEDOWN_HEAD_DEG))  # then keep holding to finish
    t += 4000
    segments.append(_pose(t, t + 3000, L.NEUTRAL, TAKEDOWN_HEAD_DEG))
    t += 3000
    # 4. idle, head straight
    segments.append(_pose(t, t + 8000, L.NEUTRAL, 0))
    return segments
