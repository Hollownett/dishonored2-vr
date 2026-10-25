"""G5 acceptance and the cast-budget measurement (hands plan Task 12): a flat hand pressed on a table and slid over it.

At loot_reach.py's table spot (the writing desk in the user's latest save; same teleport, head pitch and pose mapping, crouched 0.7 m as its low
cases so the arm reaches the desk) each hand in turn, palm
down: arrives 0.20 m above the desk top beside the coin, comes down to 3 cm below the top (the controller pressed into the desk: the hand clamp
stops the drawn palm, the fingers must stop on the top), holds 2 s, slides +-12 cm across slowly (0.1 m/s) and then fast (0.6 m/s), and lifts off.
No squeeze: nothing is taken. The DLL's status finger_ik reports the drawn fingertips' penetration into the probe shapes (tip_pen), the solve
cost (avg_us, solve_us) and the G5 flags; xr-frame-timing.csv gives the frame times.
`linux/d2vr-linux sim --scenario hand_on_table --knob sim-invulnerable --lead 45 --seconds 90` (add --knob finger-probe-3 or
--knob no-finger-surface for the budget runs; ledger .superpowers/sdd/2026-10-25-small-objects-fingers/g5-budget.md).
"""
import importlib.util
from pathlib import Path

_spec = importlib.util.spec_from_file_location("d2vr_scenario_loot_reach", Path(__file__).with_name("loot_reach.py"))
_loot = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_loot)
L = _loot.L

ABOVE, PRESS, SIDE = 0.20, -0.03, 0.12   # metres above the desk top; the press below it; the slide half width
OFFSET, LEFT_NEARER = 0.10, 0.06         # the right hand works 10 cm right of the coin, the left 6 cm nearer the player than it (10 cm left: out of reach)
SLOW_MS, FAST_MS = 1200, 200             # one 12 cm leg: 0.1 m/s and 0.6 m/s


def hand_segments(t, side, coin, stand, yaw, right_axis, rest, head, lift):
    down = _loot.PALM_DOWN[side]
    # the right hand 10 cm right of the coin; the left over the coin, 6 cm nearer the player (sim 20261025T120204: over the coin it was out of reach)
    fwd = (-right_axis[1], right_axis[0])
    base = (right_axis[0] * OFFSET, right_axis[1] * OFFSET) if side == "right" else (-fwd[0] * LEFT_NEARER, -fwd[1] * LEFT_NEARER)
    out = []

    def pose(dx, dz):
        p = (coin[0] + base[0] + right_axis[0] * dx, coin[1] + base[1] + right_axis[1] * dx, coin[2] + dz)
        return _loot.to_tracking(p, stand, yaw, lift) + list(down)

    def seg(t0, t1, hand):
        s = {"start_ms": t0, "end_ms": t1, "input": dict(L.NEUTRAL), "head_pitch_degrees": _loot.HEAD_PITCH_DEG, "head_pose": list(head),
             "left_hand_pose": list(rest["left"]), "right_hand_pose": list(rest["right"])}
        s[side + "_hand_pose"] = hand
        return s

    def leg(t0, a, b, ms, step=100):
        segs, n = [], max(1, ms // step)
        for i in range(n):
            k = (i + 1) / n
            segs.append(seg(t0 + i * ms // n, t0 + (i + 1) * ms // n, pose(a[0] + (b[0] - a[0]) * k, a[1] + (b[1] - a[1]) * k)))
        return segs, t0 + ms

    out.append(seg(t, t + 400, pose(0, ABOVE)))
    t += 400
    for a, b, ms in (((0, ABOVE), (0, PRESS), 1500),):          # down onto the desk and 3 cm into it
        segs, t = leg(t, a, b, ms)
        out += segs
    out.append(seg(t, t + 2000, pose(0, PRESS)))                 # pressed, still
    t += 2000
    for ms in (SLOW_MS, FAST_MS):                                # slow, then fast slides across
        for a, b in (((0, PRESS), (SIDE, PRESS)), ((SIDE, PRESS), (-SIDE, PRESS)), ((-SIDE, PRESS), (0, PRESS))):
            legs = 2 if abs(a[0] - b[0]) > SIDE * 1.5 else 1
            segs, t = leg(t, a, b, ms * legs, step=50 if ms == FAST_MS else 100)
            out += segs
    segs, t = leg(t, (0, PRESS), (0, ABOVE), 600)               # lift off
    out += segs
    return out, t


def build(lead_ms):
    segments = L.lead_in(lead_ms)
    _, coin, stand, _crouch, _lift = next(c for c in _loot.CASES if c[0] == "table")
    # Crouched as loot_reach's low cases (0.7 m, the game eye stays 0.22 m up): standing, the arm cannot reach the desk top 0.6 m ahead (sim
    # 20261025T114758: reach_clamped on 2961 frames, no finger probe hit), crouched the shoulder is within an arm of it.
    crouch, lift = _loot.CROUCH_M, _loot.CROUCH_LIFT_M
    yaw = _loot.yaw_to(stand, coin)
    import math
    yr = math.radians(yaw)
    right_axis = (math.sin(yr), -math.cos(yr))
    head = [0.0, round(_loot.HEAD_Y - crouch, 4), 0.0, *[round(v, 6) for v in _loot._idle.head_quaternion(0.0, _loot.HEAD_PITCH_DEG)]]
    rest = {side: list(_loot.DEFAULT[side]) + [0.0, 0.0, 0.0, 1.0] for side in _loot.DEFAULT}
    t = lead_ms
    segments.append(L.console(t, L.setviewpos(stand[0], stand[1], stand[2] + _loot.EYE_ABOVE_FEET, yaw)))
    t += L.FRAME_MS
    standing = [0.0, _loot.HEAD_Y] + head[2:]
    for t0, t1 in ((t, t + _loot.SETTLE_MS - 400), (t + _loot.SETTLE_MS - 400, t + _loot.SETTLE_MS)):   # land standing, then crouch (the runner lerps)
        segments.append({"start_ms": t0, "end_ms": t1, "input": dict(L.NEUTRAL), "head_pitch_degrees": _loot.HEAD_PITCH_DEG,
                         "head_pose": list(standing), "left_hand_pose": list(rest["left"]), "right_hand_pose": list(rest["right"])})
    t += _loot.SETTLE_MS
    for side in ("right", "left"):
        segs, t = hand_segments(t, side, coin, stand, yaw, right_axis, rest, head, lift)
        segments += segs
    return segments
