"""Gameplay-script scenario (plan Task 7, R4 S3): a thrown mine sticks (engine placement along the throw). Dual wield ON (default).

Stand with a wall a few metres ahead at head level (load a spot facing one; head pitch 0). Timeline after the lead-in:
  0 s        equip a mine (`d2vr equip hook` on DOTO, `equip stun_mine` for D2: pass game="d2" to build), 3 s idle
  S3         left grip 1.0 for 1 s (hold the mine); the left hand glides back 0.25 m then forward 0.6 m within 150 ms; the grip opens at the
             end of the glide while the hand still moves (~6 m/s); 3 s idle
  S3b        equip again, left grip 1.0 for 1 s, hand still, grip opens; 3 s idle
Expected (xr-live-status.json, dual-wield-events.jsonl):
  after S3   mine_throw.throw_place 1, mine_throw.fallback_drops 0, gadget_drop.flights 0 (the mod toss did not run), powers.hook_mines -1 (DOTO) /
             mine_throw.count -1 (any), gadget_aim.placement[1] and mine_throw.override_sensors[0] > 0 during the 250 ms window, event mine_throw_place.
  after S3b  mine_throw.slow_drops 1, throw_place still 1, gadget_drop.flights 1 (today's drop), event gadget_drop.
"""
import importlib.util
from pathlib import Path

_spec = importlib.util.spec_from_file_location("d2vr_scenario_lib", Path(__file__).resolve().parents[1] / "scenario_lib.py")
_lib = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_lib)

TOTAL_MS = 30000
HOLD = {"left_grip": 1.0}
REST = [0.0, 0.0, 0.0]
BACK = [0.0, 0.0, 0.25]      # z is backward: 0.25 m toward the body
FORWARD = [0.0, 0.0, -0.35]  # 0.6 m forward of BACK


def build(lead_ms, equip="d2vr equip hook"):
    out = _lib.lead_in(lead_ms)
    t = lead_ms

    def still(ms, inp):
        nonlocal t
        out.append(_lib.hold(t, t + ms, inp, REST, left=REST, pitch=0))
        t += ms

    out.append(_lib.console(t, equip))
    t += _lib.FRAME_MS
    still(3000, {"move_x": 0.0})
    # S3: hold, wind up, throw, open at the end of the glide
    still(1000, HOLD)
    out += _lib.glide(t, t + 60, REST, REST, 4, HOLD, left=(REST, BACK), pitch=0)
    t += 60
    out += _lib.glide(t, t + 90, REST, REST, 6, HOLD, left=(BACK, FORWARD), pitch=0)
    t += 90
    out += _lib.glide(t, t + 30, REST, REST, 3, {"move_x": 0.0}, left=(FORWARD, [0.0, 0.0, -0.40]), pitch=0)   # the grip opens while the hand is still moving out
    t += 30
    still(3000, {"move_x": 0.0})
    # S3b: hold again, open slowly with the hand still
    out.append(_lib.console(t, equip))
    t += _lib.FRAME_MS
    still(1500, {"move_x": 0.0})
    still(1000, HOLD)
    still(3000, {"move_x": 0.0})
    out.append({"start_ms": t, "end_ms": lead_ms + TOTAL_MS, "head_pitch_degrees": 0, "input": {"move_x": 0.0}})
    return out
