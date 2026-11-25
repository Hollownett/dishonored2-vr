"""Gameplay-script scenario (D2 only, plan Task 15b): Possession moves the host where the head looks, not where the host faces.

2026-11-25: runs on any D2 save: `d2vr give power possession` (the game's own AddPower) and `d2vr guard behind 3` (setviewpos) set it up.
Timeline after the lead-in (absolute head poses via idle.head_quaternion(yaw, pitch); positive yaw = left):
  0 s       `d2vr equip possession`, 3 s idle
  3-4.5 s   LT held 1.5 s aiming at the host (TODO: a setviewpos line below places the player facing it), then released = cast
  4.5-8.5 s idle 4 s (fly-in; status possession.active true)
  8.5-9.5   head yaw +60 (left) held 1 s, then move_y 1.0 for 2 s with the head still at +60   (segment A)
  12.5-13.5 head yaw -60 (right) held 1 s, then move_y 1.0 for 2 s                           (segment B)
  16.5 s    head back to 0, LT then B to end the possession, 4 s idle
  then a control run outside possession: move_y 1.0 for 2 s with the head at +60 (normal head-follow, must be unchanged)
Read (xr-live-status.json plus tools/entities.py over /proc/PID/mem for the host position):
  possession.active true from the cast until the end, possession.entries 1, possession.frames > 0 during A and B,
  possession.stick_rot_deg ~ +60 during A and ~ -60 during B (0 outside), possession.head_follow_paused > 0, 0 when never possessed;
  native_turning_applied does not grow during possession; the host displacement direction over A is within 15 deg of the head forward
  (baseline with build/no-possession-view-move: along the host's own yaw), over B within 15 deg of -60 deg; the control run unchanged.
  If stick_rot_deg stays 0 or the displacement is off by exactly the head yaw, the view/host yaw source needs the sample R5 section 2 lists.
"""
import importlib.util
from pathlib import Path

_spec = importlib.util.spec_from_file_location("d2vr_scenario_lib", Path(__file__).resolve().parents[1] / "scenario_lib.py")
_lib = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_lib)
_idle_spec = importlib.util.spec_from_file_location("d2vr_scenario_idle", Path(__file__).with_name("idle.py"))
_idle = importlib.util.module_from_spec(_idle_spec)
_idle_spec.loader.exec_module(_idle)

TOTAL_MS = 56000
HEAD_Y = 1.65
YAW_DEG = 60.0


def build(lead_ms):
    out = _lib.lead_in(lead_ms)
    t = lead_ms

    def head(yaw):
        return [0.0, HEAD_Y, 0.0, *[round(v, 6) for v in _idle.head_quaternion(yaw, 0.0)]]

    def seg(inp, ms, yaw=0.0):
        nonlocal t
        out.append({"start_ms": t, "end_ms": t + ms, "input": dict(inp), "head_pitch_degrees": 0, "head_pose": head(yaw)})
        t += ms

    def console(line):
        nonlocal t
        out.append(_lib.console(t, line))
        t += _lib.FRAME_MS

    # 2026-11-25: no lab save owns Possession: the game's own purchase (AddPower, `d2vr give power possession`; the sim runner restores the
    # saves afterwards), then Corvo is put 3 m behind the nearest guard, facing his back (`d2vr guard behind 3`), god mode against alerts.
    console("god")
    console("d2vr give power possession")
    seg({"move_x": 0.0}, 1000)
    console("d2vr give status")
    console("d2vr equip possession")
    seg({"move_x": 0.0}, 3000)            # the equip (run 20261125T131404: an aim 3 s after the give never started UsePower)
    console("d2vr guard behind 2.5")
    seg({"move_x": 0.0}, 1500)
    console("d2vr spacewarp capture 2")
    seg({"left_trigger": 1.0}, 300)       # a tap at the host (runs 2026-11-25: a 0.3 s tap possessed, 0.8 / 1.5 s holds took no host)
    seg({"move_x": 0.0}, 4000)            # released = cast; fly-in and possession start
    console("d2vr spacewarp capture 2")
    console("d2vr guard list")            # the host's position before A (the possessed guard)
    seg({"move_x": 0.0}, 1000, YAW_DEG)   # turn the head 60 deg left
    seg({"move_y": 1.0}, 2000, YAW_DEG)   # A: walk the host
    console("d2vr guard list")            # after A
    console("d2vr spacewarp capture 2")
    seg({"move_x": 0.0}, 1000, -YAW_DEG)
    seg({"move_y": 1.0}, 2000, -YAW_DEG)  # B
    console("d2vr guard list")            # after B
    seg({"move_x": 0.0}, 1000, 0.0)
    seg({"move_x": 0.0}, 4300)            # the possession ends by itself (a re-cast here possessed a second host in run 20261125T131404)
    seg({"move_x": 0.0}, 1000, YAW_DEG)   # control run, outside possession
    seg({"move_y": 1.0}, 2000, YAW_DEG)
    out.append({"start_ms": t, "end_ms": lead_ms + TOTAL_MS, "input": {"move_x": 0.0}, "head_pitch_degrees": 0, "head_pose": head(0.0)})
    return out
