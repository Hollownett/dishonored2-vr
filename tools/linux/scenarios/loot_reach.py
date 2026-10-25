"""G1 loot measurement (hands plan Task 1): sweep each palm over a coin on a table, a coin on the floor and loot in a commode, so the
read-only native Use trace (knob build/loot-reach-trace -> artifacts/native-loot-reach.csv) shows whether the coin is in the native
overlap result set, its rank after order_native_use_hits, whether 3b7b90 LOS was asked / blocked and what was selected and prompted.
Judged per case by `tools/linux/bonelab_run_check.py RUN --scenario loot_reach --coin X Y Z --case table|floor|drawer`.

Used by `linux/d2vr-linux sim --scenario loot_reach --knob loot-reach-trace --knob sim-invulnerable --lead 45 --seconds 120`
(build/sim-invulnerable, src/native_sim_invulnerable.hpp: the spots are beside bloodfly nests; the engine's god and buddha toggles are not
honoured, the player died three times in this scenario's first runs with them). Spots are in the user's latest save
(campaign/institute/institute_p_highchaos), found read-only with tools/entities.py over /proc/PID/mem and teleport probes (stand points
where the player lands on the floor, not on furniture); ledger .superpowers/sdd/2026-10-25-small-objects-fingers/g1-measure.md.

Per case: teleport (gameplay script v8 console `setviewpos`, eye = feet + 1.70 m, game yaw) facing
the coin, head pitched down 35 deg (physically crouched 0.7 m for the low cases, so the hand stays within the 1.5 m hand-ray / range-origin
reach of the eye); then for the right hand and then the left hand: the palm comes down over the coin palm-down from 0.35 m to 0.05 m above it, then
toward it from the side palm-first (0.35 m to 0.05 m out), then +-6 cm laterally above it. Then the right hand and then the left hand
each hold still above it for 2 s, squeeze the grip for 0.6 s and release (both sweeps come first: a squeeze that takes the coin ends
the case, and the sweeps of both hands are the measurement; the checker drops the frames after a take). The hands are
absolute tracking-space poses (script v5): the coin's offset from the eye turned into the body frame (x right, y up, -z forward), with
tracking y = height above the feet - 0.05 (the eye is the tracking head, 1.65 m, at feet + 1.70 m). The other hand rests at its default.
"""
import importlib.util
import math
from pathlib import Path

_spec = importlib.util.spec_from_file_location("d2vr_scenario_lib", Path(__file__).resolve().parents[1] / "scenario_lib.py")
L = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(L)
_idle_spec = importlib.util.spec_from_file_location("d2vr_scenario_idle", Path(__file__).with_name("idle.py"))
_idle = importlib.util.module_from_spec(_idle_spec)
_idle_spec.loader.exec_module(_idle)

HEAD_Y, EYE_ABOVE_FEET, HEAD_PITCH_DEG = 1.65, 1.70, -35.0
PALM_AHEAD_M = 0.05      # measured (run 20261025T083537): with the head pitched -35 deg the logged palm sits 5 cm ahead of the authored point
STEP_MS = 200            # pose keyframes; the runner lerps between back-to-back v5 segments
SETTLE_MS = 1000         # after a teleport
TOP, BOTTOM, LATERAL = 0.35, 0.05, 0.06  # metres above / beside the coin
HOLD_MS, SQUEEZE_MS, RELEASE_MS = 2000, 600, 400
DEFAULT = {"left": (-0.2, 1.3, -0.4), "right": (0.2, 1.3, -0.4)}  # src/dxgi_probe.cpp's scripted default grips
# Case: name, coin (world, the entity origin from entities.py), stand (feet x, y, z), crouch (m the head goes down), lift (m the game eye
# stays above feet + 1.70 - crouch once the physical crouch has crouched the player: measured from the logged palm, see g1-measure.md)
CROUCH_M, CROUCH_LIFT_M = 0.7, 0.22  # measured: runs 20261025T083537 (crouch 0.55) and T084335 (crouch 0.7) both leave the eye 0.22 m up
CASES = (
    # pickups_coin1_lowvalue_33 on a writing desk (typewriter, books); its -x side is the open floor (probe: feet stay at 34.00)
    ("table", (-2.09, 45.28, 34.85), (-2.69, 45.28, 34.00), 0.0, 0.0),
    # pickups_coin3_highvalue_14 on the lab floor (z 34.00); +x 0.8 m is open floor (0.6 m lands on furniture)
    ("floor", (-25.66, 46.08, 34.00), (-24.86, 46.08, 34.00), CROUCH_M, CROUCH_LIFT_M),
    # pickups_pratchette_jellied_eels_01 inside a rich commode (doors) 0.28 m above the floor: no open drawer with loot in this save
    ("drawer", (-13.80, 59.80, 27.28), (-13.23, 60.37, 27.00), CROUCH_M, CROUCH_LIFT_M),
)
PALM_DOWN = {"right": (0.0, 0.0, math.sqrt(0.5), math.sqrt(0.5)), "left": (0.0, 0.0, -math.sqrt(0.5), math.sqrt(0.5))}  # OpenXR grip: the right palm faces -x, the left +x
PALM_SIDE = {"right": (0.0, 0.0, 0.0, 1.0), "left": (0.0, 0.0, 0.0, 1.0)}  # palms face inward: the right hand beside the coin's right faces it


def yaw_to(stand, coin):
    return math.degrees(math.atan2(coin[1] - stand[1], coin[0] - stand[0]))


def to_tracking(point, stand, yaw_deg, lift=0.0):
    """World point -> tracking-space position for a body standing at `stand` (feet) with game yaw `yaw_deg` (0 = +x, CCW): the offset
    from the eye in the body frame (x right, y up, -z forward); the eye is the tracking head, so tracking y = height above the feet - 0.05
    - lift. PALM_AHEAD_M comes off the forward offset."""
    yaw = math.radians(yaw_deg)
    f, r = (math.cos(yaw), math.sin(yaw)), (math.sin(yaw), -math.cos(yaw))
    dx, dy = point[0] - stand[0], point[1] - stand[1]
    return [round(dx * r[0] + dy * r[1], 4), round(point[2] - stand[2] - (EYE_ABOVE_FEET - HEAD_Y) - lift, 4),
            round(-(dx * f[0] + dy * f[1] - PALM_AHEAD_M), 4)]


def case_segments(t, coin, stand, crouch, lift=0.0):
    """One case from script time t: teleport, settle, then right and left sweeps. -> (segments, end time)."""
    yaw = yaw_to(stand, coin)
    head = [0.0, round(HEAD_Y - crouch, 4), 0.0, *[round(v, 6) for v in _idle.head_quaternion(0.0, HEAD_PITCH_DEG)]]
    yr = math.radians(yaw)
    right_axis = (math.sin(yr), -math.cos(yr), 0.0)
    rest = {side: list(DEFAULT[side]) + [0.0, 0.0, 0.0, 1.0] for side in DEFAULT}
    out = [L.console(t, L.setviewpos(stand[0], stand[1], stand[2] + EYE_ABOVE_FEET, yaw))]
    t += L.FRAME_MS

    def seg(t0, t1, side=None, pose=None, inp=None):
        s = {"start_ms": t0, "end_ms": t1, "input": dict(inp or L.NEUTRAL), "head_pitch_degrees": HEAD_PITCH_DEG, "head_pose": list(head),
             "left_hand_pose": list(rest["left"]), "right_hand_pose": list(rest["right"])}
        if side:
            s[side + "_hand_pose"] = list(pose)
        return s

    def at(side, offset, quat):
        p = (coin[0] + offset[0], coin[1] + offset[1], coin[2] + offset[2])
        return to_tracking(p, stand, yaw, lift) + list(quat)

    def move(t0, side, a, b, ms, inp=None):
        """Keyframes a -> b (offsets from the coin, world metres) over ms, the quaternion of b throughout after the first key."""
        steps, segs = max(1, ms // STEP_MS), []
        for i in range(steps):
            k = i / (steps - 1) if steps > 1 else 1.0
            off = tuple(a[0][j] + (b[0][j] - a[0][j]) * k for j in range(3))
            segs.append(seg(t0 + i * ms // steps, t0 + (i + 1) * ms // steps, side, at(side, off, b[1] if i else a[1]), inp))
        return segs, t0 + ms

    standing = [0.0, HEAD_Y] + head[2:]
    out.append(dict(seg(t, t + SETTLE_MS - 400), head_pose=list(standing)))  # land standing (a crouched landing put the player on the commode)
    out.append(dict(seg(t + SETTLE_MS - 400, t + SETTLE_MS), head_pose=list(standing)))  # then crouch: the runner lerps this pose to the next
    t += SETTLE_MS
    for side in ("right", "left"):
        out_side = 1.0 if side == "right" else -1.0
        beside = tuple(v * out_side for v in right_axis)
        down = PALM_DOWN[side]
        for a, b, ms in (
                (((0, 0, TOP), down), ((0, 0, TOP), down), 400),                                    # arrive above the coin
                (((0, 0, TOP), down), ((0, 0, BOTTOM), down), 2000),                              # palm down, 0.35 -> 0.05 m above
                (((0, 0, BOTTOM), down), ((beside[0] * TOP, beside[1] * TOP, 0.02), PALM_SIDE[side]), 400),
                (((beside[0] * TOP, beside[1] * TOP, 0.02), PALM_SIDE[side]),
                 ((beside[0] * BOTTOM, beside[1] * BOTTOM, 0.02), PALM_SIDE[side]), 2000),           # palm toward it from the side
                (((beside[0] * BOTTOM, beside[1] * BOTTOM, 0.02), PALM_SIDE[side]), ((0, 0, BOTTOM), down), 400)):
            segs, t = move(t, side, a, b, ms)
            out += segs
        for k in (1, -1, 1, -1):                                                                   # +-6 cm laterally, palm down
            o = (right_axis[0] * LATERAL * k, right_axis[1] * LATERAL * k, BOTTOM)
            out.append(seg(t, t + 400, side, at(side, o, down)))
            t += 400
        segs, t = move(t, side, ((0, 0, BOTTOM), down), ((0, 0, TOP), down), 400)                  # lift off before the other hand
        out += segs
    for side in ("right", "left"):
        down, grip = PALM_DOWN[side], {side + "_grip": 1.0}
        segs, t = move(t, side, ((0, 0, TOP), down), ((0, 0, BOTTOM), down), 400)
        out += segs
        out.append(seg(t, t + HOLD_MS, side, at(side, (0, 0, BOTTOM), down)))                      # still for 2 s
        t += HOLD_MS
        out.append(seg(t, t + SQUEEZE_MS, side, at(side, (0, 0, BOTTOM), down), grip))           # squeeze 0.6 s
        t += SQUEEZE_MS
        out.append(seg(t, t + RELEASE_MS, side, at(side, (0, 0, BOTTOM), down)))                 # release
        t += RELEASE_MS
    return out, t


def build(lead_ms):
    segments = L.lead_in(lead_ms)
    t = lead_ms
    for _, coin, stand, crouch, lift in CASES:
        segs, t = case_segments(t, coin, stand, crouch, lift)
        segments += segs
    return segments
