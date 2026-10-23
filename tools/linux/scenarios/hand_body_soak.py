"""Plan B task B14: the driven hand bodies soak (`linux/d2vr-linux sim --scenario hand_body_soak --lead 45 --seconds 120 --knob hand-body`).

The hand body runs inside the DLL (src/hand_body_runtime.hpp): after 8 s of gameplay (in-run G1) it creates one 5 cm sphere per hand at the
calibrated palm once the hand clamp's palm cast shows it free, and from then on drives it every game frame toward the palm with B9's capped
servo (one velocity write per side per tick, gravity compensated). Proof (a) is five of these sessions with hand_body.faults == 0 summed;
proof (b) is the pressed-into-capsule segment below (player_drift_um <= 1000); proof (d) is hand_body.cost_max_us <= 200 (the drive, both
hands summed per tick). Save/load (proof (c)) is not scriptable in this rig (the pause menu's first entry is Resume, B11 run 3): Tier C.

Script after the lead (both hands; offsets in metres from the simulated default hand, x right, y up, z backward):
  0-10 s   idle at the default hands (G1 observation, then the creates)
  10-22 s  reach down to the knees and back to the chest, twice
  22-32 s  press both hands into the floor (the clamp holds them; the bodies stop on the floor: contact frames)
  32-47 s  CAPSULE: both hands pulled into the player's own body (chest/belly, no locomotion): the bodies sit inside the capsule and the
           player must not move (player_drift_um)
  47-60 s  side-to-side and up-down sweeps (about 1-2 m/s)
  60-66 s  hands forward at chest height, a short walk forward and back (locomotion: drift not counted), the hands meet what is ahead
  66-74 s  idle, then the session ends (the bodies are destroyed at stop)
Results: the "hand_body" object in artifacts/xr-live-status.json and artifacts/hand-body-events.csv. build(lead_ms) -> segments.
"""
import importlib.util
from pathlib import Path

_spec = importlib.util.spec_from_file_location("d2vr_scenario_lib", Path(__file__).resolve().parents[1] / "scenario_lib.py")
L = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(L)
NEUTRAL = L.NEUTRAL
HOME = [0.0, 0.0, 0.0]
REACH, CHEST = L.REACH, L.CHEST
FLOOR_PRESS = [-0.2, -1.4, -0.3]   # 0.15 m below the floor under the hand: the clamp holds it on the floor
CAPSULE = [-0.18, -0.55, 0.4]      # the hand pulled back into the player's own chest/belly (about 0.0-0.1 m ahead of the eye line)
FORWARD = [0.0, -0.1, -0.3]        # chest height, 0.7 m ahead
SWEEP_L, SWEEP_R = [-0.45, -0.1, -0.1], [0.35, -0.1, -0.1]
SWEEP_HI, SWEEP_LO = [0.0, 0.25, -0.1], [0.0, -0.6, -0.1]
CAPSULE_AT_MS, CAPSULE_END_MS, END_MS = 32000, 47000, 74000


def _both(t0, t1, a, b, steps, inp=NEUTRAL, pitch=0):
    return L.glide(t0, t1, a, b, steps, inp, left=(L.mirror(a), L.mirror(b)), pitch=pitch)


def _hold(t0, t1, at, inp=NEUTRAL, pitch=0):
    return L.hold(t0, t1, inp, at, left=L.mirror(at), pitch=pitch)


def build(lead_ms):
    t = lead_ms
    segs = L.lead_in(lead_ms)
    segs.append(_hold(t, t + 10000, HOME))
    cursor = t + 10000
    for _ in range(2):   # 10-22 s: reach down and back up
        segs += _both(cursor, cursor + 1500, HOME, REACH, 3, pitch=-30)
        segs.append(_hold(cursor + 1500, cursor + 2500, REACH, pitch=-30))
        segs += _both(cursor + 2500, cursor + 4000, REACH, CHEST, 3, pitch=-30)
        segs.append(_hold(cursor + 4000, cursor + 6000, CHEST, pitch=-30))
        cursor += 6000
    segs += _both(t + 22000, t + 24000, CHEST, FLOOR_PRESS, 4, pitch=L.HEAD_PITCH)   # 22-32 s: into the floor
    segs.append(_hold(t + 24000, t + 31000, FLOOR_PRESS, pitch=L.HEAD_PITCH))
    segs += _both(t + 31000, t + CAPSULE_AT_MS, FLOOR_PRESS, CHEST, 2)
    segs += _both(t + CAPSULE_AT_MS, t + CAPSULE_AT_MS + 1000, CHEST, CAPSULE, 2)  # 32-47 s: pressed into the capsule, no locomotion
    segs.append(_hold(t + CAPSULE_AT_MS + 1000, t + CAPSULE_END_MS, CAPSULE))
    cursor = t + CAPSULE_END_MS
    for _ in range(3):   # 47-60 s: sweeps
        segs += _both(cursor, cursor + 700, SWEEP_L, SWEEP_R, 2)
        segs += _both(cursor + 700, cursor + 1400, SWEEP_R, SWEEP_L, 2)
        segs += _both(cursor + 1400, cursor + 2400, SWEEP_HI, SWEEP_LO, 2)
        segs += _both(cursor + 2400, cursor + 4300, SWEEP_LO, SWEEP_L, 2)
        cursor += 4300
    segs.append(_hold(cursor, t + 60000, FORWARD))
    segs.append(_hold(t + 60000, t + 62000, FORWARD, {"move_y": 0.6}))    # 60-66 s: a short walk forward and back
    segs.append(_hold(t + 62000, t + 64000, FORWARD))
    segs.append(_hold(t + 64000, t + 66000, FORWARD, {"move_y": -0.6}))
    segs.append(_hold(t + 66000, t + END_MS, HOME))
    return segs
