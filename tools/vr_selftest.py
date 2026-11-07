"""Headset-free self test of the VR mod against the running game.

Runs a simulated-VR scenario through inject_probe.py, then checks what the player would see and do:
HUD screen present and nothing clipped at its edges, wheel layer never dropped, stick reaching the game.
UI screens are saved as PNGs in the run folder (build/ui-dump knob).

    python tools/vr_selftest.py --dll d2vr_render_probe_arm_v258.dll --scenario all
    python tools/vr_selftest.py --dll ... --scenario wheel --launch      # start the game in the background and load the last save

Never takes focus or sends global input: keys are posted to the game window, and the game is configured not to
pause without focus (tools/game_keys.py --prepare-config; the live cvars are also sent through the mod's command
channel after injection and their answers checked, because the config is only read at process start)."""
_unused = """

Exit code 0 = all checks passed.
"""
import argparse
import glob
import json
import os
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
from scan_body_material import attach  # noqa: E402
from game_state import game_logic_mask  # noqa: E402

ART = os.path.join(ROOT, 'artifacts')
NEUTRAL = {"move_x": 0.0}
SCENARIOS = {
    # Each segment: [start_ms, end_ms, input, right_hand_offset]
    'hud': [[0, 6000, NEUTRAL, None]],
    'walk': [[0, 2000, NEUTRAL, None], [2000, 6000, {"move_y": 1.0}, None], [6000, 8000, NEUTRAL, None]],
    'wheel': [[0, 2000, NEUTRAL, None], [2000, 5000, {"x": True}, [0.05, 0.2, -0.2]],
              [5000, 8000, {"x": True}, [-0.15, 0.25, -0.2]], [8000, 10000, NEUTRAL, None]],
}
# Long neutral hold: menus and pages are driven meanwhile through the mod's "pad" test command.
SCENARIOS['idle'] = [[0, 30000, NEUTRAL, None]]  # sessions with a script are capped at 120 s
SCENARIOS['jump'] = [[0, 2000, NEUTRAL, None], [2000, 2300, {"a": True}, None], [2300, 5000, NEUTRAL, None]]
SCENARIOS['lean'] = [[0, 2000, NEUTRAL, None], [2000, 5000, {"y": True, "move_x": 1.0}, None], [5000, 7000, NEUTRAL, None]]
# Every wheel sector in turn: hold X with the stick at that sector, release (equips), press LT, rest.
SCENARIOS['powers'] = []
for k in range(8):
    import math as _m
    t = 2000 + k * 4500
    SCENARIOS['powers'] += [[t, t + 1500, {"x": True, "turn_x": round(_m.cos(k * _m.pi / 4), 3), "turn_y": round(_m.sin(k * _m.pi / 4), 3)}, None],
                            [t + 1500, t + 2200, NEUTRAL, None], [t + 2200, t + 3200, {"left_trigger": 1.0}, None], [t + 3200, t + 4500, NEUTRAL, None]]
SCENARIOS['powers'].insert(0, [0, 2000, NEUTRAL, None])
# Reach around with the right hand and squeeze: anything usable or grabbable nearby must engage the engine.
SCENARIOS['grab'] = [[0, 1500, NEUTRAL, None]]
for k, hand in enumerate([[0.1, -0.2, -0.3], [0.3, -0.3, -0.2], [-0.1, -0.4, -0.35], [0.2, 0.0, -0.45], [0.35, -0.5, 0.0], [0.0, -0.6, -0.3]]):
    t = 1500 + k * 2500
    SCENARIOS['grab'] += [[t, t + 1000, NEUTRAL, hand], [t + 1000, t + 1800, {"right_grip": 1.0}, hand], [t + 1800, t + 2500, NEUTRAL, hand]]  # Use = the grip alone (v449)
# Sword swings: the right hand sweeps 0.8 m across in 300 ms (about 2.7 m/s), three times.
SCENARIOS['swing'] = [[0, 2000, NEUTRAL, [-0.4, 0.0, -0.3]]]
for k in range(3):
    t = 2000 + k * 2500
    SCENARIOS['swing'] += [[t + i * 30, t + (i + 1) * 30, NEUTRAL, [-0.4 + i * 0.08, 0.0, -0.3]] for i in range(10)]
    SCENARIOS['swing'] += [[t + 300, t + 2500, NEUTRAL, [-0.4, 0.0, -0.3]]]
# Swim: stick forward in water (use with --teleport into water), then dive with crouch (right stick down = B).
SCENARIOS['swim'] = [[0, 2000, NEUTRAL, None], [2000, 6000, {"move_y": 1.0}, None], [6000, 6300, {"b": True}, None],
                     [6300, 9000, {"move_y": 1.0}, None], [9000, 10000, NEUTRAL, None]]
# Pick up the item in front (use with --near item): look down, reach to it and squeeze, a few hand placements.
SCENARIOS['pickup'] = [[0, 1500, NEUTRAL, None]]
for k, hand in enumerate([[0.0, -0.85, -0.35], [0.1, -0.95, -0.45], [-0.1, -0.8, -0.55], [0.0, -1.0, -0.3]]):
    t = 1500 + k * 2600
    SCENARIOS['pickup'] += [[t, t + 1100, NEUTRAL, hand], [t + 1100, t + 1900, {"right_grip": 1.0}, hand], [t + 1900, t + 2600, NEUTRAL, hand]]  # loot: the grip alone uses (v449)
# The same reach while looking at the item (a player looks at what he takes): separates a camera-facing rule from the hand query.
SCENARIOS['pickuplook'] = [[a, b, i, h, {"head_pitch_degrees": -62}] for a, b, i, h in SCENARIOS['pickup']]
# Damage: idle while the engine hurts the player (--damage DECL): the damage vignette must blend into the view.
SCENARIOS['damage'] = [[0, 12000, NEUTRAL, None]]
# Aim: both hands pitched 20 degrees down (hand_pitch_degrees); the crosshair must follow the left hand in the eye.
SCENARIOS['aim'] = [[0, 8000, NEUTRAL, None]]
# Drag a corpse by the right wrist (use with --near corpse): stand 0.55 m past the wrist, reach down to it (the sim right grip
# rests at 0.2 right, 0.35 below and 0.4 ahead of the eye), squeeze, then pull up and walk backwards, then let go.
_REACH = [-0.2, -1.25, -0.15]
_PULL = [-0.15, -0.95, 0.1]
SCENARIOS['drag'] = [[0, 1500, NEUTRAL, _REACH, {"hand_pitch_degrees": -60}],
                     [1500, 2500, {"right_grip": 1.0, "right_trigger": 1.0}, _REACH, {"hand_pitch_degrees": -60}],  # a full fist grabs (v449)
                     [2500, 6000, {"right_grip": 1.0, "move_y": -0.8}, _PULL, {"hand_pitch_degrees": -40}],
                     [6000, 8000, NEUTRAL, _PULL]]
# Pistol: equip it from the wheel (the test save's wheel holds only Pistol Bullet; any sector), then hold it forward without
# firing (LT fires and the hand returns to Empty) - for gun model / crosshair measurements.
# The simulated aim pose equals the grip pose (real Touch controllers tilt them apart), so a level sim grip holds the tracked gun
# ~59 deg nose-down (sim v398): pitch the hands up 59 deg once it is drawn, as a player points it forward.
_AIM_GUN = {"hand_pitch_degrees": 59}
SCENARIOS['pistol'] = [[0, 2000, NEUTRAL, None], [2000, 3500, {"x": True, "turn_x": 1.0}, None], [3500, 25000, NEUTRAL, None, _AIM_GUN]]
# Fires early: native-left-aim.csv / native-crosshair.csv keep only their first 4000 rows (~33 s of a session).
SCENARIOS['pistolfire'] = SCENARIOS['pistol'][:2] + [[3500, 5000, NEUTRAL, None, _AIM_GUN], [5000, 6000, {"left_trigger": 1.0}, None, _AIM_GUN], [6000, 9000, NEUTRAL, None, _AIM_GUN]]
# Potion: hold X (wheel), point the left (X) hand (default -0.2,-0.35 from the head) at the health elixir (top left of the wheel art), pull the trigger.
SCENARIOS['potion'] = [[0, 2000, NEUTRAL, None], [2000, 4000, {"x": True}, [-0.03, 0.535, -0.2]],
                       [4000, 4400, {"x": True, "left_trigger": 1.0}, [-0.03, 0.535, -0.2]], [4400, 6000, {"x": True}, [-0.03, 0.535, -0.2]],
                       [6000, 9000, NEUTRAL, None]]
# Hide/Show all (top right of the wheel art): trigger held on it = native Y held.
SCENARIOS['hide'] = [[a, b, i, [0.552, 0.535, -0.2] if h else None] for a, b, i, h in SCENARIOS['potion']]
SCENARIOS['hide'][2][1] = 5000
SCENARIOS['hide'][3][0] = 5000
def glide(t0, t1, a, b, steps, inp=None, extra=None):
    """Smooth hand motion: `steps` segments from offset a to b between t0 and t1 (ms)."""
    out = []
    for i in range(steps):
        k = (i + 1) / steps
        seg = [round(t0 + (t1 - t0) * i / steps), round(t0 + (t1 - t0) * (i + 1) / steps), inp or NEUTRAL,
               [round(x + (y - x) * k, 4) for x, y in zip(a, b)]]
        if extra:
            seg.append(dict(extra))
        out.append(seg)
    return out


# Wrist flick: the hand stays put, the wrist pitches 45 degrees down in 120 ms and back (blade tip ~0.5 m): NOT an attack.
REST = [0.0, 0.0, 0.0]  # offsets are relative to the simulated default hand: no jump at the start
SCENARIOS['flick'] = [[0, 2000, NEUTRAL, REST]]
for k in range(3):
    t = 2000 + k * 2000
    SCENARIOS['flick'] += [[t + i * 20, t + (i + 1) * 20, NEUTRAL, REST, {"hand_pitch_degrees": -7.5 * (i + 1)}] for i in range(6)]
    SCENARIOS['flick'] += [[t + 120, t + 2000, NEUTRAL, REST]]
# Slow, natural reach to the item in front (use with --near item): no attack may fire on the way; then grip.
SCENARIOS['reach'] = [[0, 1500, NEUTRAL, [0.25, -0.3, -0.25]]] + glide(1500, 3000, [0.25, -0.3, -0.25], [0.0, -0.85, -0.35], 30) +     [[3000, 3800, NEUTRAL, [0.0, -0.85, -0.35]], [3800, 4800, {"right_trigger": 1.0}, [0.0, -0.85, -0.35]], [4800, 6000, NEUTRAL, [0.0, -0.85, -0.35]]]
# Hold and throw a native carryable (bottle; use with --near bottle): squeeze at the bottle (native Use -> HoldObject), keep
# squeezing while lifting to the chest (the bottle must follow the hand), then swing forward fast and let go (throw).
_HA, _HC, _HD = [-0.2, -0.8, -0.2], [-0.1, -0.2, -0.2], [-0.1, 0.1, -0.75]
_GRIP = {"right_grip": 1.0}
# The fist grabs the bottle; the grip alone then keeps it (a full squeeze is not needed while carrying it).
SCENARIOS['hold'] = [[0, 1500, NEUTRAL, _HA], [1500, 2700, {"right_grip": 1.0, "right_trigger": 1.0}, _HA]] + glide(2700, 3700, _HA, _HC, 20, _GRIP) +     [[3700, 5500, _GRIP, _HC]] + glide(5500, 5750, _HC, _HD, 8, _GRIP) + [[5750, 9000, NEUTRAL, _HD]]
# v788: 'hold' by the grip alone (the trigger is the game's Use), with the hand at a crouched player's table height (the user's save sneaks: the eye is low, 'hold' reached the floor).
_HU = [0.1, 0.2, -0.4]
SCENARIOS['holdup'] = [[0, 1500, NEUTRAL, _HU], [1500, 2700, {"right_grip": 1.0}, _HU]] + glide(2700, 3700, _HU, _HC, 20, _GRIP) + [[3700, 5500, _GRIP, _HC]] + glide(5500, 5750, _HC, _HD, 8, _GRIP) + [[5750, 9000, NEUTRAL, _HD]]
# Showcase scenarios (screenshots of the grabbing fingers, 2026-10-14): the head looks AT the right hand (the simulated hand sits at a
# fixed spot in XR space, so the head pitch that points at it follows from the offsets) and eye pairs are requested at the key moments.
# PAIR_PLAN: scenario -> {script segment (scenario segment + 1): eye-pair label}; a pair is requested PAIR_DELAY s into the segment.
PAIR_PLAN, PAIR_DELAY = {}, {}


def glide_look(t0, t1, a, b, steps, inp, pitch0, pitch1, extra):
    """glide() with the head pitch moving from pitch0 to pitch1 as well (keeps the head on the hand)."""
    out = glide(t0, t1, a, b, steps, inp, extra)
    for i, seg in enumerate(out):
        seg[4]["head_pitch_degrees"] = round(pitch0 + (pitch1 - pitch0) * (i + 1) / steps, 1)
    return out


_SB = {"hand_pitch_degrees": -50, "head_offset": [0.0, 0.0, -0.3]}  # the hands point at the floor item; the head leans 30 cm forward over the hand
_SBL = lambda p: dict(_SB, head_pitch_degrees=p)  # noqa: E731
# (a) bottle: the head leans over the hand and looks 75 deg down at it (hand 1.15 m below the eye, 0.3 m ahead; upright at 0.6 m the avatar's
# chest hid the hand, sim v470), full fist, lift to the chest, throw.
SCENARIOS['showbottle'] = ([[0, 2000, NEUTRAL, _HA, _SBL(-75)], [2000, 3300, {"right_grip": 1.0, "right_trigger": 1.0}, _HA, _SBL(-75)],
                            [3300, 4600, {"right_grip": 1.0, "right_trigger": 1.0}, _HA, _SBL(-75)]] +
                           glide_look(4600, 5600, _HA, _HC, 20, _GRIP, -75, -61, _SB) + [[5600, 7600, _GRIP, _HC, _SBL(-61)]] +
                           glide(7600, 7850, _HC, _HD, 8, _GRIP, _SBL(-61)) + [[7850, 10500, NEUTRAL, _HD, _SBL(-61)]])
PAIR_PLAN['showbottle'] = {1: 'bottle-open-approach', 2: 'bottle-fist-closing', 3: 'bottle-fist-held-low', 24: 'bottle-held-chest'}  # scenario segments 0, 1, 2, 23
# v1072 (user: "a carried object is out of sync with the hand and flickers when running"): take the bottle like showbottle, hold it still at the
# chest for 2 s (the draw binding learns the hold), run forward 5 s with it, stop, let go. Pairs: still, running early/late, stopped.
SCENARIOS['holdrun'] = (SCENARIOS['showbottle'][:3] + glide_look(4600, 5600, _HA, _HC, 20, _GRIP, -75, -61, _SB) +
                        [[5600, 7600, _GRIP, _HC, _SBL(-61)], [7600, 10100, dict(_GRIP, move_y=1.0), _HC, _SBL(-61)],
                         [10100, 12600, dict(_GRIP, move_y=1.0), _HC, _SBL(-61)], [12600, 14100, _GRIP, _HC, _SBL(-61)], [14100, 16000, NEUTRAL, _HC, _SBL(-61)]])
PAIR_PLAN['holdrun'] = {24: 'held-still-chest', 25: 'held-running-early', 26: 'held-running-late', 27: 'held-stopped'}
# (b) corpse limb: the head 71 deg down at the wrist (hand 1.6 m below the eye, 0.55 m ahead), full fist, then pull and walk backwards. The pistol
# hand is raised out of the picture. (A 20 cm lean over the hand put the avatar's coat front between the eye and the hand, sim v470.)
_LEAN = {"left_hand_offset": [-0.2, 0.25, 0.0]}
_DL, _DP = dict(_LEAN, hand_pitch_degrees=-60, head_pitch_degrees=-71), dict(_LEAN, hand_pitch_degrees=-40, head_pitch_degrees=-76)
SCENARIOS['showdrag'] = [[0, 2000, NEUTRAL, _REACH, _DL], [2000, 3500, {"right_grip": 1.0, "right_trigger": 1.0}, _REACH, _DL],
                         [3500, 5200, {"right_grip": 1.0, "move_y": -0.8}, _PULL, _DP], [5200, 7000, {"right_grip": 1.0, "move_y": -0.8}, _PULL, _DP],
                         [7000, 9000, NEUTRAL, _PULL, dict(_LEAN, head_pitch_degrees=-76)]]
PAIR_PLAN['showdrag'] = {1: 'limb-open-approach', 2: 'limb-fist-closed', 3: 'limb-dragging-early', 4: 'limb-dragging-late'}
def glide_head(t0, t1, a, b, steps, inp, pitch0, pitch1, off0, off1, extra):
    """glide() with the head pitch and the head offset moving as well."""
    out = glide(t0, t1, a, b, steps, inp, extra)
    for i, seg in enumerate(out):
        k = (i + 1) / steps
        seg[4]["head_pitch_degrees"] = round(pitch0 + (pitch1 - pitch0) * k, 1)
        seg[4]["head_offset"] = [round(x + (y - x) * k, 3) for x, y in zip(off0, off1)]
    return out


# AFTER framing (lead, 2026-10-14). Bottle close-up: grab as showbottle, lift to the chest, then up to 36 cm in front of the eyes (hand
# 1.5 m high, 0.33 m ahead of the head) with the head looking straight at the fist. Legacy script, runs on any DLL.
_FACE = [-0.2, 0.2, 0.07]
_SF = {"hand_pitch_degrees": -30}
SCENARIOS['showbottleclose'] = ([[0, 2000, NEUTRAL, _HA, _SBL(-75)], [2000, 2600, {"right_grip": 1.0, "right_trigger": 1.0}, _HA, _SBL(-75)],
                                 [2600, 3400, {"right_grip": 1.0, "right_trigger": 1.0}, _HA, _SBL(-75)]] +
                                glide_look(3400, 4400, _HA, _HC, 20, _GRIP, -75, -61, _SB) +
                                glide_head(4400, 5400, _HC, _FACE, 20, _GRIP, -61, -24, [0.0, 0.0, -0.3], [0.0, 0.0, 0.0], _SB) +
                                [[5400, 6600, _GRIP, _FACE, dict(_SF, head_pitch_degrees=-24)], [6600, 7900, _GRIP, _FACE, dict(_SF, head_pitch_degrees=-24)],
                                 [7900, 9500, NEUTRAL, _FACE, dict(_SF, head_pitch_degrees=-24)]])
PAIR_PLAN['showbottleclose'] = {1: 'closeup-bottle-approach', 2: ('closeup-bottle-fist-0.1s', 0.0), 3: ('closeup-bottle-fist-0.7s', 0.0),
                                44: 'closeup-bottle-face-a', 45: 'closeup-bottle-face-b'}  # scenario segments 0, 1, 2, 43, 44


def look_pose(head, target):
    """Script v5 head pose [x, y, z, qx, qy, qz, qw] (tracking space, OpenXR -Z forward) at `head` looking at `target`: yaw about +Y, then pitch about +X."""
    import math
    dx, dy, dz = (t - h for t, h in zip(target, head))
    yaw, pitch = math.atan2(-dx, -dz), math.atan2(dy, math.hypot(dx, dz))
    sy, cy, sx, cx = math.sin(yaw / 2), math.cos(yaw / 2), math.sin(pitch / 2), math.cos(pitch / 2)
    return [round(v, 4) for v in (*head, cy * sx, sy * cx, -sy * sx, cy * cx)]


def right_hand(off):
    """Where the simulated right hand sits (tracking space) for an offset: (0.2, 1.3, -0.4) + off."""
    return [0.2 + off[0], 1.3 + off[1], -0.4 + off[2]]


# Corpse limb, AFTER framing: the head sits 30 cm right of and 10 cm ahead of the body line and turns toward the grabbing hand (script v5:
# DLL v480+): pairs at the squeeze (~0.1 s) and ~0.7 s later, then while dragging.
_LH = (0.3, 1.65, -0.1)
_LL = lambda off, pitch: {"head_pose": look_pose(_LH, right_hand(off)), "hand_pitch_degrees": pitch, "left_hand_offset": [-0.2, 0.25, 0.0]}  # noqa: E731
SCENARIOS['showlimb'] = [[0, 2000, NEUTRAL, _REACH, _LL(_REACH, -60)], [2000, 2600, {"right_grip": 1.0, "right_trigger": 1.0}, _REACH, _LL(_REACH, -60)],
                         [2600, 3400, {"right_grip": 1.0, "right_trigger": 1.0}, _REACH, _LL(_REACH, -60)],
                         [3400, 5000, {"right_grip": 1.0, "move_y": -0.8}, _PULL, _LL(_PULL, -40)], [5000, 7000, {"right_grip": 1.0, "move_y": -0.8}, _PULL, _LL(_PULL, -40)],
                         [7000, 9000, NEUTRAL, _PULL, _LL(_PULL, -40)]]
PAIR_PLAN['showlimb'] = {1: 'limb-approach', 2: ('limb-fist-0.1s', 0.0), 3: ('limb-fist-0.7s', 0.0), 4: 'limb-drag-early', 5: 'limb-drag-late'}
PAIR_TIMES = {}  # scenario -> [(scenario ms, label)]: eye pairs by the script clock (works inside runs of short segments)


def qmul(a, b):
    x1, y1, z1, w1 = a
    x2, y2, z2, w2 = b
    return (w1 * x2 + x1 * w2 + y1 * z2 - z1 * y2, w1 * y2 - x1 * z2 + y1 * w2 + z1 * x2, w1 * z2 + x1 * y2 - y1 * x2 + z1 * w2, w1 * w2 - x1 * x2 - y1 * y2 - z1 * z2)


def hand_quat(pitch=0.0, roll=0.0):
    """Right-hand grip orientation: roll about its own forward (-Z) axis first, then pitch about +X (negative = fingers down)."""
    import math
    p, r = math.radians(pitch) / 2, math.radians(roll) / 2
    return qmul((math.sin(p), 0, 0, math.cos(p)), (0, 0, math.sin(r), math.cos(r)))


def pose_glide(t0, t1, steps, inp, head0, head1, hand0, hand1, q0, q1, extra=None):
    """Script v5 segments: the right hand moves hand0 -> hand1 (tracking space, metres) turning q0 -> q1, the head moves head0 -> head1
    always looking at the hand (look_pose); the left hand stays raised out of the picture."""
    import math
    out = []
    for i in range(steps):
        k = (i + 1) / steps
        lerp = lambda a, b: [x + (y - x) * k for x, y in zip(a, b)]  # noqa: E731
        pos, head = lerp(hand0, hand1), lerp(head0, head1)
        q = lerp(q0, q1)
        n = math.sqrt(sum(v * v for v in q))
        more = {"head_pose": look_pose(head, pos), "right_hand_pose": [round(v, 4) for v in pos] + [round(v / n, 5) for v in q], "left_hand_offset": [-0.2, 0.25, 0.0]}
        more.update(extra or {})
        out.append([round(t0 + (t1 - t0) * i / steps), round(t0 + (t1 - t0) * (i + 1) / steps), inp, None, more])
    return out


# Free hands (script v5, DLL v480+; --near bottle: the table beside the bottle). The head sits at the default spot, hand 40 cm ahead of the
# eyes: open hand in the air, fist (full squeeze, nothing within reach), then the hand opens, goes palm-down over the table 25 cm right of
# the bottle and is lowered 0.2 m/s onto it (the fingers should stop flat). Table top ~0.9 m below the eye (test save), set TABLE_Y if it moves.
TABLE_Y = 0.80  # tracking-space height of the hand centre resting on the table (eye 1.65 here; world table top ~ eye_z - 0.85)
_FIST = {"right_grip": 1.0, "right_trigger": 1.0}
_AIR, _ABOVE, _ONTAB = [0.12, 1.50, -0.40], [0.28, TABLE_Y + 0.32, -0.45], [0.28, TABLE_Y - 0.03, -0.45]
_HEAD0, _HEAD1 = [0.0, 1.65, 0.0], [0.1, 1.65, -0.15]
_Q_AIR, _Q_DOWN = list(hand_quat(0, 0)), list(hand_quat(0, 90))  # roll +90 = palm down (-90 showed the palm up, sim v502)
SCENARIOS['showhand'] = (pose_glide(0, 300, 3, NEUTRAL, _HEAD0, _HEAD0, _AIR, _AIR, _Q_AIR, _Q_AIR) + [[300, 2200, NEUTRAL, None, pose_glide(0, 1, 1, NEUTRAL, _HEAD0, _HEAD0, _AIR, _AIR, _Q_AIR, _Q_AIR)[0][4]]] +
                         [[2200, 3600, _FIST, None, pose_glide(0, 1, 1, NEUTRAL, _HEAD0, _HEAD0, _AIR, _AIR, _Q_AIR, _Q_AIR)[0][4]]] +
                         pose_glide(3600, 4800, 12, NEUTRAL, _HEAD0, _HEAD1, _AIR, _ABOVE, _Q_AIR, _Q_DOWN) +
                         [[4800, 5400, NEUTRAL, None, pose_glide(0, 1, 1, NEUTRAL, _HEAD1, _HEAD1, _ABOVE, _ABOVE, _Q_DOWN, _Q_DOWN)[0][4]]] +
                         pose_glide(5400, 7000, 32, NEUTRAL, _HEAD1, _HEAD1, _ABOVE, _ONTAB, _Q_DOWN, _Q_DOWN) +
                         [[7000, 9500, NEUTRAL, None, pose_glide(0, 1, 1, NEUTRAL, _HEAD1, _HEAD1, _ONTAB, _ONTAB, _Q_DOWN, _Q_DOWN)[0][4]]])
PAIR_TIMES['showhand'] = [(1500, 'hand-open-air'), (3000, 'fist-air'), (5100, 'hand-over-table'), (6500, 'hand-lowering'), (7600, 'hand-on-table-a'), (8800, 'hand-on-table-b')]
# Corpse limb as a player does it: crouched (head 45 cm down, as 'carry'), the wrist 85 cm below the eye and 55 cm ahead (upright it is 1.7 m from the
# eye, past what the avatar's arm reaches: the fist stopped short of the limb, sim v502). Script v5 (DLL v480+).
_REACH_C, _PULL_C = [-0.2, -1.0, -0.15], [-0.15, -0.75, 0.1]
_LH2 = (0.3, 1.2, -0.1)
_LL2 = lambda off, pitch: {"head_pose": look_pose(_LH2, right_hand(off)), "hand_pitch_degrees": pitch, "left_hand_offset": [-0.2, 0.25, 0.0]}  # noqa: E731
SCENARIOS['showlimbcrouch'] = [[0, 2500, NEUTRAL, _REACH_C, _LL2(_REACH_C, -60)], [2500, 3100, {"right_grip": 1.0, "right_trigger": 1.0}, _REACH_C, _LL2(_REACH_C, -60)],
                               [3100, 3900, {"right_grip": 1.0, "right_trigger": 1.0}, _REACH_C, _LL2(_REACH_C, -60)],
                               [3900, 5500, {"right_grip": 1.0, "move_y": -0.8}, _PULL_C, _LL2(_PULL_C, -40)], [5500, 7500, {"right_grip": 1.0, "move_y": -0.8}, _PULL_C, _LL2(_PULL_C, -40)],
                               [7500, 9500, NEUTRAL, _PULL_C, _LL2(_PULL_C, -40)]]
PAIR_PLAN['showlimbcrouch'] = {1: 'crouch-limb-approach', 2: ('crouch-limb-fist-0p1s', 0.0), 3: ('crouch-limb-fist-0p7s', 0.0), 4: 'crouch-limb-drag-early', 5: 'crouch-limb-drag-late'}
# Table sweep (script v5, --near bottle): the open palm-down hand is lowered from 0.72 to 0.27 (tracking-space y; the eye is 14.53 here, so the hand
# world z runs 13.60 -> 13.15, across the table top whichever its exact height) at 0.1 m/s, then held. finger_ik hit timeline tells where contact began.
_T_HI, _T_LO = [0.28, 0.72, -0.45], [0.28, 0.27, -0.45]
SCENARIOS['showtable'] = (pose_glide(0, 1500, 6, NEUTRAL, _HEAD0, _HEAD1, [0.2, 1.3, -0.4], _T_HI, _Q_AIR, _Q_DOWN) +
                          pose_glide(1500, 6000, 45, NEUTRAL, _HEAD1, _HEAD1, _T_HI, _T_LO, _Q_DOWN, _Q_DOWN) +
                          [[6000, 8500, NEUTRAL, None, pose_glide(0, 1, 1, NEUTRAL, _HEAD1, _HEAD1, _T_LO, _T_LO, _Q_DOWN, _Q_DOWN)[0][4]]])
PAIR_TIMES['showtable'] = [(1700, 'table-hand-high'), (3200, 'table-lowering-a'), (4300, 'table-lowering-b'), (5200, 'table-lowering-c'), (6300, 'table-held-a'), (7800, 'table-held-b')]
# Table sweep, crouched like 'carry' (head 45 cm down): upright the table top is 1.2 m below the eye, past the avatar's arm (the hand stopped following
# the controller at the arm's reach and never got near the table, sim v502). The hand is lowered 0.95 -> 0.45 (tracking-space y) at 0.1 m/s, then held.
_C0, _C1 = [0.0, 1.2, 0.0], [0.1, 1.2, -0.15]
_U_HI, _U_LO = [0.28, 0.95, -0.45], [0.28, 0.45, -0.45]
SCENARIOS['showtable2'] = (pose_glide(0, 1500, 6, NEUTRAL, _C0, _C1, [0.2, 1.3, -0.4], _U_HI, _Q_AIR, _Q_DOWN) +
                           pose_glide(1500, 6500, 50, NEUTRAL, _C1, _C1, _U_HI, _U_LO, _Q_DOWN, _Q_DOWN) +
                           [[6500, 9000, NEUTRAL, None, pose_glide(0, 1, 1, NEUTRAL, _C1, _C1, _U_LO, _U_LO, _Q_DOWN, _Q_DOWN)[0][4]]])
PAIR_TIMES['showtable2'] = [(1700, 'table2-hand-high'), (3200, 'table2-lowering-a'), (4500, 'table2-lowering-b'), (5700, 'table2-lowering-c'), (6800, 'table2-held-a'), (8200, 'table2-held-b')]
# Orbit around the hand holding the bottle (script v5, DLL v480+; --near bottle): take the bottle as in 'hold' (full fist, then grip alone), carry it to a fixed
# showcase pose in front of the chest (palm left, thumb up, forearm forward), keep that pose, and put the head on a 0.40 m sphere around the hand, looking at it,
# 1.5 s per view (pair 1.0 s in). 'orbitfist' keeps the trigger pressed as well (full fist) during the views.
_OHAND = [0.10, 1.30, -0.35]
_ORBIT = [('palm-side', (-1, 0, 0)), ('back-of-hand', (1, 0, 0)), ('thumb-above', (0, 1, 0.15)), ('little-finger-below', (0, -1, 0.15)),
          ('forward', (0, 0, -1)), ('behind', (0, 0, 1)), ('front-thumb-45', (-1, 1, 0)), ('front-below-45', (-1, -1, 0))]


def orbit_scenario(fist, ha_off=None, tag=''):
    import math
    qa, qid = list(hand_quat(-50, 0)), list(hand_quat(0, 0))
    ha = right_hand(ha_off or _HA)
    inp = _FIST if fist else _GRIP
    segs = (pose_glide(0, 1500, 3, NEUTRAL, _HEAD0, _HEAD0, ha, ha, qa, qa) + pose_glide(1500, 2700, 4, _FIST, _HEAD0, _HEAD0, ha, ha, qa, qa) +
            pose_glide(2700, 3700, 10, _GRIP, _HEAD0, _HEAD0, ha, _OHAND, qa, qid) + pose_glide(3700, 4700, 2, inp, _HEAD0, _HEAD0, _OHAND, _OHAND, qid, qid))
    times = []
    for k, (label, d) in enumerate(_ORBIT):
        t = 4700 + 1500 * k
        n = math.sqrt(sum(c * c for c in d))
        head = [h + 0.40 * c / n for h, c in zip(_OHAND, d)]
        segs += pose_glide(t, t + 1500, 1, inp, head, head, _OHAND, _OHAND, qid, qid)
        times.append((t + 1000, ('orbitfist-' if fist else 'orbit-') + tag + label))
    for sg in segs:  # the left (pistol) hand down at the hip: raised, its sleeve filled the palm-side and forward views (sim v510)
        sg[4]['left_hand_offset'] = [0.1, -0.8, 0.6]
    return segs, times


SCENARIOS['orbit'], PAIR_TIMES['orbit'] = orbit_scenario(False)
SCENARIOS['orbitfist'], PAIR_TIMES['orbitfist'] = orbit_scenario(True)
# NECK grab variants (DLL 525): the press point climbs the bottle's axis, hand y -0.70 / -0.60 (-0.52: the 524 holdneck)
# NECK grab (DLL 524+): the same pickup, but the hand reaches the bottle's neck (28 cm higher than the body grab: about 0.33 m above its base).
_HAN = [-0.2, -0.52, -0.2]
SCENARIOS['orbitneck'], PAIR_TIMES['orbitneck'] = orbit_scenario(False, _HAN, 'neck-')
_NK = {"hand_pitch_degrees": -50, "head_pitch_degrees": -40}  # as 'hold' (write_scenario sets these by name)
for _tag, _y in (('2', -0.70), ('3', -0.60)):
    _h = [-0.2, _y, -0.2]
    SCENARIOS['holdneck' + _tag] = ([[0, 1500, NEUTRAL, _h, {"hand_pitch_degrees": -50, "head_pitch_degrees": -40}], [1500, 2700, {"right_grip": 1.0, "right_trigger": 1.0}, _h, {"hand_pitch_degrees": -50, "head_pitch_degrees": -40}]] +
                                    glide(2700, 3700, _h, _HC, 20, _GRIP, {"hand_pitch_degrees": -50, "head_pitch_degrees": -40}) + [[3700, 5500, _GRIP, _HC, {"hand_pitch_degrees": -50, "head_pitch_degrees": -40}]] +
                                    glide(5500, 5750, _HC, _HD, 8, _GRIP, {"hand_pitch_degrees": -50, "head_pitch_degrees": -40}) + [[5750, 9000, NEUTRAL, _HD, {"hand_pitch_degrees": -50, "head_pitch_degrees": -40}]])
SCENARIOS['holdneck'] = ([[0, 1500, NEUTRAL, _HAN, _NK], [1500, 2700, {"right_grip": 1.0, "right_trigger": 1.0}, _HAN, _NK]] + glide(2700, 3700, _HAN, _HC, 20, _GRIP, _NK) +
                         [[3700, 5500, _GRIP, _HC, _NK]] + glide(5500, 5750, _HC, _HD, 8, _GRIP, _NK) + [[5750, 9000, NEUTRAL, _HD, _NK]])
# Table press (DLL 530): showtable2's palm-down hand pressed 30 cm below the table top (tracking y 0.45 -> 0.18 at 0.1 m/s), held, then back up. The drawn hand must not sink.
_U_DEEP = [0.28, 0.12, -0.45]
SCENARIOS['tablepress'] = (pose_glide(0, 1500, 6, NEUTRAL, _C0, _C1, [0.2, 1.3, -0.4], _U_HI, _Q_AIR, _Q_DOWN) +
                           pose_glide(1500, 6000, 45, NEUTRAL, _C1, _C1, _U_HI, _U_LO, _Q_DOWN, _Q_DOWN) +
                           pose_glide(6000, 9500, 35, NEUTRAL, _C1, _C1, _U_LO, _U_DEEP, _Q_DOWN, _Q_DOWN) +
                           [[9500, 11500, NEUTRAL, None, pose_glide(0, 1, 1, NEUTRAL, _C1, _C1, _U_DEEP, _U_DEEP, _Q_DOWN, _Q_DOWN)[0][4]]] +
                           pose_glide(11500, 12500, 10, NEUTRAL, _C1, _C1, _U_DEEP, _U_HI, _Q_DOWN, _Q_DOWN))
PAIR_TIMES['tablepress'] = [(4800, 'tablepress-at-table'), (7000, 'tablepress-press-a'), (9000, 'tablepress-press-b'), (10500, 'tablepress-deep-held'), (12300, 'tablepress-released')]
# Death + "Restart From Last Checkpoint" (user crash 2026-10-14, v533, game rva 0x87bbfa): exercise what the player did (a bottle in the fist, a hand push,
# the sword drawn at the shoulder and thrown, the pistol the save starts with in the left hand), then die (god off, `damage player1 bullet_regular_npc` every
# second from script second DEATH_AT), pick "Restart From Last Checkpoint" with the virtual pad, wait for the load and watch the game for 40 s. Use --near bottle.
DEATH_AT, DEATH_SECONDS = 12.5, 70
BACK, _G = [-0.03, 0.20, 0.50], {"right_grip": 1.0}  # as defined again below (this block sits before them)
_DR_WIND = [0.0, 0.15, 0.1]
_DR_TAIL = (glide(0, 300, [0.0, 0.0, 0.0], BACK, 3) + [[300, 800, _G, BACK]] + glide(800, 1100, BACK, _DR_WIND, 2, _G) + [[1100, 1400, _G, _DR_WIND]] +
            glide(1400, 1480, _DR_WIND, [0.0, 0.15, -0.4], 4, _G) + glide(1480, 1500, [0.0, 0.15, -0.4], [0.0, 0.14, -0.5], 2) + [[1500, 2300, NEUTRAL, [0.0, 0.14, -0.5]]])


def _shift(segs, dt):
    return [[a + dt, b + dt, *rest] for a, b, *rest in segs]


# hold the bottle, throw it, draw the sword, throw it; then idle until the end
SCENARIOS['deathreload'] = ([[0, 1500, NEUTRAL, _HA], [1500, 2700, {"right_grip": 1.0, "right_trigger": 1.0}, _HA]] + glide(2700, 3700, _HA, _HC, 20, _GRIP) +
                            [[3700, 5500, _GRIP, _HC]] + glide(5500, 5750, _HC, _HD, 8, _GRIP) + [[5750, 7000, NEUTRAL, _HD]] +
                            _shift(_DR_TAIL, 7000) + [[9300, DEATH_SECONDS * 1000, NEUTRAL, [0.0, 0.14, -0.5]]])
# push the bottle with the open hand first (hand-push-short), close the fist behind it (hold), then as above
_PS = [-0.17, -0.8, 0.1]
SCENARIOS['deathreloadpush'] = ([[0, 1500, NEUTRAL, REST]] + glide(1500, 2700, REST, _PS, 12) + [[2700, 4200, NEUTRAL, _PS]] + glide(4200, 5700, _PS, [-0.17, -0.8, -0.1231], 25) +
                                [[5700, 6200, NEUTRAL, [-0.17, -0.8, -0.1231]]] + glide(6200, 6700, [-0.17, -0.8, -0.1231], [-0.17, -0.8, -0.2], 5) +
                                [[6700, 7700, {"right_grip": 1.0, "right_trigger": 1.0}, [-0.17, -0.8, -0.2]], [7700, 9000, _GRIP, [-0.17, -0.8, -0.2]]] +
                                glide(9000, 9250, [-0.17, -0.8, -0.2], _HD, 5, _GRIP) + [[9250, 10500, NEUTRAL, _HD]] + _shift(_DR_TAIL, 10500) +
                                [[12800, DEATH_SECONDS * 1000, NEUTRAL, [0.0, 0.14, -0.5]]])
# Sword over the right shoulder: reach back (sheathes), return, reach back and squeeze grip (draws). Offsets are relative to
# the default right hand (0.2,-0.3,-0.4 from the head); the scabbard point is right .17, down .10, back .10.
BACK = [-0.03, 0.20, 0.50]
SCENARIOS['sheathe'] = [[0, 2000, NEUTRAL, [0.0, 0.0, 0.0]]] + glide(2000, 3000, [0.0, 0.0, 0.0], BACK, 20) +     [[3000, 4000, NEUTRAL, BACK]] + glide(4000, 5000, BACK, [0.0, 0.0, 0.0], 20) + [[5000, 7000, NEUTRAL, [0.0, 0.0, 0.0]]] +     glide(7000, 8000, [0.0, 0.0, 0.0], BACK, 20) + [[8000, 9000, {"right_grip": 1.0}, BACK]] + glide(9000, 10000, BACK, [0.0, 0.0, 0.0], 20) +     [[10000, 12000, NEUTRAL, [0.0, 0.0, 0.0]]]
# Full-grip sword (SwordGrip): squeeze at the shoulder (draw), carry it down by the floor, let go (drop), squeeze there again
# (pick up), carry it back to the shoulder and let go (sheathe); then draw, let go in front (thrown), squeeze at the shoulder
# (the thrown sword comes back), let go there (sheathe).
_LOW, _FRONT, _G = [-0.1, -1.05, -0.1], [0.3, -0.2, 0.15], {"right_grip": 1.0}  # _FRONT: let go out to the right side (a forward throw hit the shopkeeper, who closed the shutter)
SCENARIOS['swordgrip'] = ([[0, 2000, NEUTRAL, [0.0, 0.0, 0.0]]] + glide(2000, 3000, [0.0, 0.0, 0.0], BACK, 10) +
                          [[3000, 3800, _G, BACK]] + glide(3800, 4800, BACK, _LOW, 10, _G) +
                          [[4800, 6500, _G, _LOW], [6500, 8500, NEUTRAL, _LOW], [8500, 10500, _G, _LOW]] +
                          glide(10500, 11500, _LOW, BACK, 10, _G) + [[11500, 12500, NEUTRAL, BACK]] +
                          glide(12500, 13500, BACK, [0.0, 0.0, 0.0], 10) + glide(13500, 14500, [0.0, 0.0, 0.0], BACK, 10) +
                          [[14500, 15300, _G, BACK]] + glide(15300, 16300, BACK, _FRONT, 5, _G) + [[16300, 18000, NEUTRAL, _FRONT]] +
                          glide(18000, 19000, _FRONT, BACK, 10) + [[19000, 21000, _G, BACK], [21000, 23000, NEUTRAL, BACK]])
# Throw the sword at a guard ~2.5 m ahead (use with --near npc-front): draw at the shoulder, wind up over the shoulder, then a
# fast forward throw let go mid-motion (10 ms steps: the hand speed estimate needs one step per frame).
_WIND, _MID, _OUT = [0.0, 0.15, 0.1], [0.0, 0.15, -0.4], [0.0, 0.14, -0.5]  # a flat dart throw from beside the head
SCENARIOS['swordthrow'] = ([[0, 300, NEUTRAL, [0.0, 0.0, 0.0]]] + glide(300, 700, [0.0, 0.0, 0.0], BACK, 4) +
                           [[700, 1300, _G, BACK]] + glide(1300, 1700, BACK, _WIND, 4, _G) + [[1700, 2000, _G, _WIND]] +
                           glide(2000, 2080, _WIND, _MID, 8, _G) + glide(2080, 2100, _MID, _OUT, 2) + [[2100, 7000, NEUTRAL, _OUT]])
# The same throw at a guard who has not noticed the player (use with --near npc --blind-ai): the sword is drawn 10 m behind him (its
# sound alerted guards 3 m away), then the player stands 2.5 m behind him just before the throw. The engine's own stealth test
# (isUnaware) must answer yes and the throw kill him.
SCENARIOS['stealththrow'] = SCENARIOS['swordthrow']
# v783 (user: "a pistol thrown into a guard stuns him, as a bottle does"; dual wield): the native pistol drawn into the LEFT hand by the test
# command, held by the left grip (hold-to-hold), thrown as 'swordthrow' throws the sword (the same dart path, mirrored: x negated). Use with
# --near npc-front. Expect guns.stuns[0] >= 1 (thrown gun struck a guard).
_LW, _LM, _LO = [-0.0, 0.15, 0.1], [-0.0, 0.15, -0.4], [-0.0, 0.14, -0.5]
SCENARIOS['gunthrow'] = ([[0, 300, NEUTRAL, None, {"left_hand_offset": REST, "command": "equip pistol"}], [300, 1300, NEUTRAL, None, {"left_hand_offset": REST}],
                          [1300, 1700, {"left_grip": 1.0}, None, {"left_hand_offset": REST}]] +
                         [s_[:3] + [None, {"left_hand_offset": s_[3]}] for s_ in glide(1700, 2000, REST, _LW, 4, {"left_grip": 1.0})] +
                         [[2000, 2300, {"left_grip": 1.0}, None, {"left_hand_offset": _LW}]] +
                         [s_[:3] + [None, {"left_hand_offset": s_[3]}] for s_ in glide(2300, 2380, _LW, _LM, 8, {"left_grip": 1.0})] +
                         [s_[:3] + [None, {"left_hand_offset": s_[3]}] for s_ in glide(2380, 2400, _LM, _LO, 2)] +
                         [[2400, 7000, NEUTRAL, None, {"left_hand_offset": _LO}]])
# v783 (user, headset v791: "with a bottle the pistol in the other hand is drawn wrong"): the native pistol drawn into the left hand, the right
# hand takes the bottle as 'hold' does (use with --near bottle). Eye pairs while the bottle is held show the pistol's hold.
# Crash soak (headset crashes 2026-10-13): six cycles of draw at the shoulder, wind up, throw, call back at the shoulder, sheathe.
_SOAK = []
for _k in range(6):
    _t = _k * 4200
    _SOAK += (glide(_t, _t + 300, [0.0, 0.0, 0.0], BACK, 3) + [[_t + 300, _t + 800, _G, BACK]] + glide(_t + 800, _t + 1100, BACK, _WIND, 2, _G) +
              [[_t + 1100, _t + 1400, _G, _WIND]] + glide(_t + 1400, _t + 1480, _WIND, _MID, 4, _G) + glide(_t + 1480, _t + 1500, _MID, _OUT, 2) +
              [[_t + 1500, _t + 2300, NEUTRAL, _OUT]] + glide(_t + 2300, _t + 2700, _OUT, BACK, 3) + [[_t + 2700, _t + 3400, _G, BACK], [_t + 3400, _t + 4200, NEUTRAL, BACK]])
SCENARIOS['soak'] = [[0, 1000, NEUTRAL, [0.0, 0.0, 0.0]]] + [[a + 1000, b + 1000, i, h, *m] for a, b, i, h, *m in _SOAK]
# Carry a body (use with --near corpse): crouch as a player does (head 45 cm down: the physical crouch), look at the body, the right
# hand at its chest, squeeze (the grip alone = the game's Use: the shoulder carry), hold, then stand and look ahead (right-eye captures).
# Upright, a hand at the floor is 1.6 m from the eye, past the hand query's 1.5 m reach (sim v462: Use never answered).
_TORSO = [-0.2, -0.95, -0.35]
_CROUCH = {"head_pitch_degrees": -50, "head_offset": [0.0, -0.45, 0.0]}
SCENARIOS['carry'] = ([[0, 1500, NEUTRAL, [0.0, 0.0, 0.0]]] + glide(1500, 2500, [0.0, 0.0, 0.0], _TORSO, 5, None, _CROUCH) +
                      [[2500, 3200, _G, _TORSO, _CROUCH], [3200, 6000, NEUTRAL, [0.0, 0.0, 0.0], _CROUCH],
                       [6000, 8000, NEUTRAL, [0.0, 0.0, 0.0], {"head_pitch_degrees": 0, "head_offset": [0.0, 0.0, 0.0]}]] +
                      # Leaning with the body on the shoulder (an eye pair in each): right, forward, down and forward, left.
                      [[8000 + 2000 * k, 10000 + 2000 * k, NEUTRAL, [0.0, 0.0, 0.0], {"head_pitch_degrees": 0, "head_offset": off}]
                       for k, off in enumerate([[0.15, 0.0, 0.0], [0.0, 0.0, -0.2], [0.05, -0.2, -0.2], [-0.15, 0.0, 0.0]])])
# Severed limb or body half (--near limb, after chopping a corpse): crouched like 'carry', grip ALONE on it (the game's Use: HoldObject), lift, let go.
SCENARIOS['limbgrip'] = ([[0, 1500, NEUTRAL, [0.0, 0.0, 0.0]]] + glide(1500, 2500, [0.0, 0.0, 0.0], _TORSO, 5, None, _CROUCH) +
                         [[2500, 3500, NEUTRAL, _TORSO, _CROUCH], [3500, 4300, _G, _TORSO, _CROUCH], [4300, 7000, _G, [0.0, -0.4, -0.3], _CROUCH],
                          [7000, 9000, NEUTRAL, [0.0, 0.0, 0.0], {"head_pitch_degrees": 0, "head_offset": [0.0, 0.0, 0.0]}]])
# The same reach without looking at the part (head level): the game's own gaze/hand-ray ranking misses it, only the hand's proximity can select it.
_CROUCH_LEVEL = {"head_pitch_degrees": 0, "head_offset": [0.0, -0.45, 0.0]}
SCENARIOS['limbgripblind'] = [seg[:4] + [dict(seg[4], **_CROUCH_LEVEL) if len(seg) > 4 and seg[4].get('head_offset') != [0.0, 0.0, 0.0] else (seg[4] if len(seg) > 4 else {})]
                              for seg in SCENARIOS['limbgrip']]
CARRY_PAIRS = range(len(SCENARIOS['carry']) - 4, len(SCENARIOS['carry']) + 1)  # script segments (scenario segment + 1) with an eye pair
PAIR_PLAN['carry'] = {k: f'carry{k}' for k in CARRY_PAIRS}
PAIR_DELAY['carry'] = 1.0
# (c) showcase: the same carry with longer holds and pairs: hand on the torso, fist squeezed, carried crouched, carried standing.
_UPRIGHT = {"head_pitch_degrees": 0, "head_offset": [0.0, 0.0, 0.0]}
SCENARIOS['showcarry'] = ([[0, 1500, NEUTRAL, [0.0, 0.0, 0.0]]] + glide(1500, 2500, [0.0, 0.0, 0.0], _TORSO, 5, None, _CROUCH) +
                          [[2500, 4300, NEUTRAL, _TORSO, _CROUCH], [4300, 5400, _G, _TORSO, _CROUCH], [5400, 8000, NEUTRAL, [0.0, 0.0, 0.0], _CROUCH],
                           [8000, 10500, NEUTRAL, [0.0, 0.0, 0.0], _UPRIGHT]])
PAIR_PLAN['showcarry'] = {7: 'carry-hand-on-torso', 8: 'carry-fist-squeezed', 9: 'carry-crouched', 10: 'carry-standing'}  # scenario segments 6-9
# Shoulder carry as the player does it (v629, use with --near corpse; the stand is 0.55 m beside the chest): the GRIP alone (never the
# trigger) on the torso, kept 0.5 s, hands off to the game's carry; the grip stays held through the pickup and while standing up.
# carrydrop lets go of a still hand (the body falls off the shoulder), carrythrow swings the hand 0.6 m forward in 150 ms and lets go (a throw).
_TORSO_HOLD = ([[0, 1500, NEUTRAL, _REACH, {"hand_pitch_degrees": -60}], [1500, 6500, _G, _REACH, {"hand_pitch_degrees": -60}],
                [6500, 8500, _G, [0.0, 0.0, 0.0], _UPRIGHT]])
SCENARIOS['carrydrop'] = _TORSO_HOLD + [[8500, 11500, NEUTRAL, [0.0, 0.0, 0.0], _UPRIGHT]]
SCENARIOS['carrythrow'] = (_TORSO_HOLD + glide(8500, 8650, [0.0, 0.0, 0.0], [0.0, 0.1, -0.6], 15, _G, _UPRIGHT) +
                           [[8650, 11500, NEUTRAL, [0.0, 0.1, -0.6], _UPRIGHT]])
# Shoulder dock by hand (v634, user headset v627 + the user's choice of a physical shoulder; use with --near corpse): the grip alone on the
# torso, a slow lift of the hand to the right shoulder zone (head + 0.18 right, 0.18 down, 0.03 back: grip_decision.hpp at_shoulder), the grip
# let go there (ShoulderDock: the body stays, held at that point), the hand lowered and a 2 s walk forward with the body on the shoulder; then a
# fresh squeeze at the shoulder takes it back into the hand, pulled down slowly and let go (shoulderdrop) or swung forward fast (shoulderthrow).
_SHOULDER = [-0.02, 0.17, 0.43]
_SHOULDER_LIFT = ([[0, 1500, NEUTRAL, _REACH, {"hand_pitch_degrees": -60}], [1500, 2500, _G, _REACH, {"hand_pitch_degrees": -60}]] +
                  glide(2500, 4500, _REACH, _SHOULDER, 30, _G, _UPRIGHT) + [[4500, 5500, _G, _SHOULDER, _UPRIGHT], [5500, 6000, NEUTRAL, _SHOULDER, _UPRIGHT]] +
                  glide(6000, 6500, _SHOULDER, [0.0, 0.0, 0.0], 8, None, _UPRIGHT) +
                  [[6500, 8500, {"move_y": 0.6}, [0.0, 0.0, 0.0], _UPRIGHT], [8500, 9000, NEUTRAL, [0.0, 0.0, 0.0], _UPRIGHT]] +
                  glide(9000, 9600, [0.0, 0.0, 0.0], _SHOULDER, 8, None, _UPRIGHT) + [[9600, 10100, _G, _SHOULDER, _UPRIGHT]])
SCENARIOS['shoulderdrop'] = _SHOULDER_LIFT + glide(10100, 10900, _SHOULDER, [0.0, 0.0, 0.0], 12, _G, _UPRIGHT) + [[10900, 13500, NEUTRAL, [0.0, 0.0, 0.0], _UPRIGHT]]
SCENARIOS['shoulderthrow'] = _SHOULDER_LIFT + glide(10100, 10250, _SHOULDER, [0.0, 0.1, -0.6], 15, _G, _UPRIGHT) + [[10250, 13500, NEUTRAL, [0.0, 0.1, -0.6], _UPRIGHT]]
_DOCK_SEG = next(i for i, sg in enumerate(_SHOULDER_LIFT) if sg[0] == 6500) + 1   # script segment (scenario segment + 1) of the docked walk
for _n in ('shoulderdrop', 'shoulderthrow'):
    PAIR_PLAN[_n] = {next(i for i, sg in enumerate(_SHOULDER_LIFT) if sg[0] == 5500) + 1: ('dock-let-go', .4), _DOCK_SEG: ('dock-walk', 1.2), _DOCK_SEG + 1: ('dock-stand', .3)}
# v1041: the same with a pistol drawn first (the game's carry empties both hands and aborts while one still holds an item; v1039's
# left-hand keep did exactly that). The pistol goes to the slot-1 arm 3 s before the reach.
for _n in ('shoulderdrop', 'shoulderthrow'):
    SCENARIOS[_n + '_armed'] = [[0, 3000, NEUTRAL, None, {"command": "equip pistol"}]] + [[sg[0] + 3000, sg[1] + 3000] + sg[2:] for sg in SCENARIOS[_n]]
TORSO_SCENARIOS = ('carrydrop', 'carrythrow', 'shoulderdrop', 'shoulderthrow', 'shoulderdrop_armed', 'shoulderthrow_armed')
# v1046 (user, headset v1045: the Heart "quite often just picking up randomly when I squeeze my hand ... should behave as a normal weapon,
# same as gun or crossbow"; dual wield). The Heart is equipped by name (slot-1 arm, the left), held by the left grip, let go in front: it
# falls as its pickup and the right hand, squeezing right next to it, takes it (into the RIGHT hand). Held by the right grip, carried to the
# left hip and let go there: put away, no pickup. Equipped again, both grips squeeze and let go in front (it drops); after it has landed both
# hands squeeze again at chest height where it was let go: nothing may come back (v1045 equipped it from a stale position).
# Journal: heart_on/heart_hold/heart_release/heart_take; nothing Heart after the second drop but heart_release.
_RHIP = [-0.4, -0.27, 0.38]   # the right hand at the left-hip holster (pistolhip's left-hand _HIP, moved across: the defaults are 0.4 m apart)
_RNEAR = [-0.3, 0.0, 0.0]     # the right hand 10 cm from the left
SCENARIOS['heart'] = ([[0, 1000, NEUTRAL, REST, {"command": "equip heart"}]] + glide(1000, 1500, REST, _RNEAR, 3) + [[1500, 3000, {"left_grip": 1.0}, _RNEAR],
                       [3000, 3050, NEUTRAL, _RNEAR], [3050, 5500, {"right_grip": 1.0}, _RNEAR]] +
                      glide(5500, 6500, _RNEAR, _RHIP, 5, {"right_grip": 1.0}) +
                      [[6500, 8500, NEUTRAL, _RHIP], [8500, 8600, NEUTRAL, _RHIP, {"command": "equip heart"}]] + glide(8600, 9000, _RHIP, REST, 3) +
                      [[9000, 10000, {"left_grip": 1.0, "right_grip": 1.0}, REST], [10000, 13000, NEUTRAL, REST],
                       [13000, 13600, {"left_grip": 1.0, "right_grip": 1.0}, REST], [13600, 16000, NEUTRAL, REST]])
# The Heart let go at the holster (the left hand at HIP, as 'pistolhip' draws there): put away, no pickup spawned (gadget_drop.dropped unchanged).
_LG, _LHIP = {"left_grip": 1.0}, [0.0, -0.27, 0.38]   # _LHIP = pistolhip's _HIP (the left hand at the holster; defined further down)
SCENARIOS['hearthip'] = ([[0, 1000, NEUTRAL, None, {"left_hand_offset": REST, "command": "equip heart"}], [1000, 2000, _LG, None, {"left_hand_offset": REST}]] +
                         [s_[:3] + [None, {"left_hand_offset": s_[3]}] for s_ in glide(2000, 3000, REST, _LHIP, 5, _LG)] +
                         [[3000, 3500, _LG, None, {"left_hand_offset": _LHIP}], [3500, 5000, NEUTRAL, None, {"left_hand_offset": _LHIP}]] +
                         [s_[:3] + [None, {"left_hand_offset": s_[3]}] for s_ in glide(5000, 6000, _LHIP, REST, 5)] +
                         [[6000, 8000, NEUTRAL, None, {"left_hand_offset": REST}]])
# The same with the pistol (GunGrip's own hip test): the journal's gun_holster row says whether the hand reached the holster.
SCENARIOS['pistolhipdw'] = [[sg[0], sg[1], sg[2], sg[3], dict(sg[4], command="equip pistol")] if "command" in sg[4] else sg for sg in SCENARIOS['hearthip']]
# Head sway for the eye pipeline (alt-pose-trace, alternate-eye): 48 s of side steps and nods (every Setup's pose differs from the one before),
# with a short walk and a few snap turns, so a pose-versus-image skew or a swapped eye image would show in alt_pose_trace_check.py.
_SWAY = []
for _k in range(12):
    _t = 1000 + _k * 4000
    _SWAY += glide_head(_t, _t + 2000, [0.0, 0.0, 0.0], [0.0, 0.0, 0.0], 5, NEUTRAL, -10, 10, [-0.12, 0.0, 0.0], [0.12, 0.03, -0.05], {"hand_pitch_degrees": 0})
    _SWAY += glide_head(_t + 2000, _t + 4000, [0.0, 0.0, 0.0], [0.0, 0.0, 0.0], 5, {"move_y": 0.5} if _k % 3 == 1 else ({"turn_x": 1.0} if _k % 5 == 2 else NEUTRAL), 10, -10, [0.12, 0.03, -0.05], [-0.12, 0.0, 0.0], {"hand_pitch_degrees": 0})
SCENARIOS['headsway'] = [[0, 1000, NEUTRAL, [0.0, 0.0, 0.0]]] + _SWAY
# Grip with nothing under the hand uses what the head looks at (test save: the Black Market counter ahead): the shop opens.
# The counter is below eye level. The left hand is raised in front: at its default pose it is in the hip holster zone, where the grip
# draws a holstered pistol (sim v465: a loaded hip drained the grip, the shop never opened).
_LOOK = {"head_pitch_degrees": -35, "left_hand_offset": [0.0, 0.2, -0.1]}
# The right hand presses: the test save starts with the pistol drawn in the left hand, whose grip then holds (and holsters) the gun.
SCENARIOS['lookuse'] = [[0, 3000, NEUTRAL, [0.0, 0.0, 0.0], _LOOK], [3000, 3600, {"right_grip": 1.0}, [0.0, 0.0, 0.0], _LOOK],  # use with --near shop
                        [3600, 8000, NEUTRAL, [0.0, 0.0, 0.0], _LOOK]]
# The shop held in the left hand like a note (use with --near shop; runs with the native menu pointer): open it as lookuse, then the right
# hand 20 cm in front of the default left hand's page, aiming ahead, must point at the page centre (the old 1 m panel ahead reads u .3, v .77).
SCENARIOS['shoparm'] = SCENARIOS['lookuse'][:2] + [[3600, 5000, NEUTRAL, [0.0, 0.0, 0.0], _LOOK], [5000, 12000, NEUTRAL, [-0.4, 0.16, 0.2], {"head_pitch_degrees": 0}]]
SCENARIOS['sworddrop'] = SCENARIOS['swordgrip'][:23] + [[6500, 26000, NEUTRAL, _LOW]]  # the dropped sword lies 20 s (probe it live)
# Full-grip gun (GunGrip): the wheel draws the pistol (kept until gripped), grip it, carry it to the left hip and let go
# (holster); an empty hand's trigger must not draw it; squeeze at the hip (draw), fire it, let go in front (holster).
_HIP, _LG = [0.0, -0.27, 0.38], {"left_grip": 1.0}
SCENARIOS['pistolhip'] = ([[0, 2000, NEUTRAL, [0.0, 0.0, 0.0]], [2000, 3500, {"x": True, "turn_x": 1.0}, [0.0, 0.0, 0.0]],
                           [3500, 5000, NEUTRAL, [0.0, 0.0, 0.0], _AIM_GUN], [5000, 5800, _LG, [0.0, 0.0, 0.0], _AIM_GUN]] +
                          glide(5800, 6800, [0.0, 0.0, 0.0], _HIP, 10, _LG, _AIM_GUN) + [[6800, 7800, NEUTRAL, _HIP]] +
                          glide(7800, 8800, _HIP, [0.0, 0.0, 0.0], 10) + [[8800, 9300, {"left_trigger": 1.0}, [0.0, 0.0, 0.0]], [9300, 9600, NEUTRAL, [0.0, 0.0, 0.0]]] +
                          glide(9600, 10300, [0.0, 0.0, 0.0], _HIP, 10) + [[10300, 11300, _LG, _HIP]] +
                          glide(11300, 12300, _HIP, [0.0, 0.0, 0.0], 10, _LG, _AIM_GUN) +
                          [[12300, 13000, _LG, [0.0, 0.0, 0.0], _AIM_GUN], [13000, 13600, {"left_grip": 1.0, "left_trigger": 1.0}, [0.0, 0.0, 0.0], _AIM_GUN],
                           [13600, 14300, _LG, [0.0, 0.0, 0.0], _AIM_GUN], [14300, 16500, NEUTRAL, [0.0, 0.0, 0.0], _AIM_GUN]])
SCENARIOS['power'] = [[0, 2000, NEUTRAL, None], [2000, 3200, {"left_trigger": 1.0}, None], [3200, 7000, NEUTRAL, None]]
# Overhead chops (use with --near npc-front/civ-front): the blade starts raised (pitch +70) outside the target and comes
# down and forward through it in 250 ms, three times: a contact hit must land on the first entry.
SCENARIOS['chop'] = [[i * 50, (i + 1) * 50, NEUTRAL, [0.0, round(-0.1 * (i + 1) / 24, 3), round(-0.4 * (i + 1) / 24, 3)], {"hand_pitch_degrees": round(-20 * (i + 1) / 24, 1)}]
                     for i in range(24)]  # glide in: a jump from the lead-in pose reads as a thrust
SCENARIOS['chop'] += [[1200, 1600, NEUTRAL, [0.0, -0.1, -0.4], {"hand_pitch_degrees": -20}], [1600, 2000, NEUTRAL, [0.0, 0.25, 0.1], {"hand_pitch_degrees": 70}]]
for k in range(3):
    t = 2000 + k * 3000
    SCENARIOS['chop'] += [[t + i * 25, t + (i + 1) * 25, NEUTRAL, [0.0, round(0.25 - 0.035 * (i + 1), 3), round(0.1 - 0.05 * (i + 1), 3)],
                           {"hand_pitch_degrees": 70 - 9 * (i + 1)}] for i in range(10)]
    SCENARIOS['chop'] += [[t + 250, t + 2600, NEUTRAL, [0.0, -0.1, -0.4], {"hand_pitch_degrees": -20}],  # a raised blade held long is a block
                          [t + 2600, t + 3000, NEUTRAL, [0.0, 0.25, 0.1], {"hand_pitch_degrees": 70}]]
# Blade flicked down into the floor three times (hand low, pitch 0 -> -80 in 250 ms): surface contact FX must play.
LOW = [0.0, -0.7, -0.3]
SCENARIOS['floor'] = [[0, 2000, NEUTRAL, LOW, {"hand_pitch_degrees": 0}]]
for k in range(3):
    t = 2000 + k * 3000
    SCENARIOS['floor'] += [[t + i * 25, t + (i + 1) * 25, NEUTRAL, LOW, {"hand_pitch_degrees": -8 * (i + 1)}] for i in range(10)]
    SCENARIOS['floor'] += [[t + 250, t + 1500, NEUTRAL, LOW, {"hand_pitch_degrees": -80}], [t + 1500, t + 3000, NEUTRAL, LOW, {"hand_pitch_degrees": 0}]]
SCENARIOS['floorleft'] = SCENARIOS['floor']  # same flicks with the left hand (pistol/crossbow)
# Face-to-face with an alerted guard (use with --near npc-front): stand 6 s so it notices and engages, then six swings
# (unaware targets turn any hit into an Assassination, sim v386).
SCENARIOS['duel'] = [[0, 6000, NEUTRAL, [-0.4, 0.0, -0.3]]]
for k in range(6):
    t = 6000 + k * 2000
    SCENARIOS['duel'] += [[t + i * 30, t + (i + 1) * 30, NEUTRAL, [-0.4 + i * 0.08, 0.0, -0.3]] for i in range(10)]
    SCENARIOS['duel'] += [[t + 300, t + 2000, NEUTRAL, [-0.4, 0.0, -0.3]]]
# Bloodflies (use with --teleport beside a nest): ten fast head-height sweeps over 25 s (headset v361: "attack is not working on wasps").
SCENARIOS['swarm'] = [[0, 2000, NEUTRAL, [-0.4, 0.15, -0.3]]]
for k in range(10):
    t, y = 2000 + k * 2500, (0.1, 0.3)[k % 2]
    SCENARIOS['swarm'] += [[t + i * 30, t + (i + 1) * 30, NEUTRAL, [-0.4 + i * 0.08, y, -0.3]] for i in range(10)]
    SCENARIOS['swarm'] += [[t + 300, t + 2500, NEUTRAL, [-0.4, y, -0.3]]]
# Right hand aimed down at a shop counter in front (the black-market save) and held; press Use meanwhile ("pad x").
SCENARIOS['shop'] = [[0, 25000, NEUTRAL, [-0.15, -0.15, -0.25], {"hand_pitch_degrees": -75}]]
# The same aim, opened by the hand's trigger (v404: the trigger uses what the hand points at). Nothing is bought.
_SHOP = ([-0.15, -0.15, -0.25], {"hand_pitch_degrees": -75})
SCENARIOS['shoptrigger'] = [[0, 1500, NEUTRAL, None]]
for k, pitch in enumerate([0, -20, -40, -60, -75]):  # sweep the right hand's aim down the counter, one trigger pull each
    t = 1500 + k * 1800
    SCENARIOS['shoptrigger'] += [[t, t + 1000, NEUTRAL, [-0.15, -0.15, -0.25], {"hand_pitch_degrees": pitch}],
                                 [t + 1000, t + 1400, {"right_trigger": 1.0}, [-0.15, -0.15, -0.25], {"hand_pitch_degrees": pitch}],
                                 [t + 1400, t + 1800, NEUTRAL, [-0.15, -0.15, -0.25], {"hand_pitch_degrees": pitch}]]
# The same sweep with the grip (v438: one button per hand, the grip uses what its hand points at).
SCENARIOS['shopgrip'] = [[a, b, {"right_grip": 1.0} if i.get("right_trigger") else i, *rest] for a, b, i, *rest in SCENARIOS['shoptrigger']]
# Left hand lowered beside the hip and held, looking down (headset v361: the left arm vanished near the waist). The engine must
# never hide it (status arm_hidden bit 0) and the mirror frames must show it.
HIP = [-0.05, -0.4, 0.35]
SCENARIOS['waist'] = [[0, 1500, NEUTRAL, [0.0, 0.0, 0.0], {"head_pitch_degrees": -50}]] +     [[s0, s1, NEUTRAL, h, {"head_pitch_degrees": -50, "hand_pitch_degrees": p}] for s0, s1, h, p in
     [(1500 + i * 50, 1550 + i * 50, [round(HIP[k] * (i + 1) / 30, 3) for k in range(3)], -2 * (i + 1)) for i in range(30)]] +     [[3000, 11000, NEUTRAL, HIP, {"head_pitch_degrees": -50, "hand_pitch_degrees": -60}],
     [11000, 13000, NEUTRAL, [0.0, 0.0, 0.0], {"head_pitch_degrees": 0}]]
# Engine damage path only (use with --near ...): the mod's "melee <npc>" test command requests a contact hit every 2.5 s.
SCENARIOS['contact'] = [[0, 8000, NEUTRAL, REST]]
# Behind an unaware guard (--near npc): stand still; the takedown sensor must qualify and its prompt must not blink.
SCENARIOS['takedown'] = [[0, 10000, NEUTRAL, REST]]
SCENARIOS['shove'] = [[0, 20000, NEUTRAL, REST]]
# Grab + yank a guard (use with --near npc-front): the left hand (the right grip is the choke when a takedown is offered).
# Squeeze and let go once to send the save's drawn pistol to the hip, then hand on his chest, squeeze, pull back hard
# (6 m/s), keep holding.
_CHEST, _BACK, _LG2 = [0.15, 0.05, -0.45], [-0.1, 0.0, 0.25], {"left_grip": 1.0}
SCENARIOS['yank'] = ([[0, 400, NEUTRAL, [0.0, 0.0, 0.0]], [400, 700, _LG2, [0.0, 0.0, 0.0]], [700, 1300, NEUTRAL, [0.0, 0.0, 0.0]]] +
                     glide(1300, 1900, [0.0, 0.0, 0.0], _CHEST, 6) + [[1900, 2050, {"left_grip": 1.0, "left_trigger": 1.0}, _CHEST]] +
                     glide(2050, 2150, _CHEST, _BACK, 10, {"left_grip": 1.0, "left_trigger": 1.0}) + [[2150, 5200, _LG2, _BACK], [5200, 9000, NEUTRAL, _BACK]])  # with --near npc: the "shove" test command tries each push decl on the guard
# Native hands phase 1 (v1050): a closed empty left fist punches a guard's chest three times at ~5 m/s (use with --near npc-front).
# Expect combat_log "fist_hit" (one per punch, 0.3 s gate) and the guard reacting/alerted. The opening squeeze sends the save's pistol to the hip.
_FIST = {"left_grip": 1.0}
SCENARIOS['punch'] = [[0, 400, NEUTRAL, [0.0, 0.0, 0.0]], [400, 700, _LG2, [0.0, 0.0, 0.0]], [700, 1300, NEUTRAL, [0.0, 0.0, 0.0]],
                      [1300, 2000, _FIST, _BACK]]
for _k in range(3):
    _t = 2000 + _k * 2000
    SCENARIOS['punch'] += glide(_t, _t + 150, _BACK, _CHEST, 10, _FIST) + [[_t + 150, _t + 450, _FIST, _CHEST]] + glide(_t + 450, _t + 1050, _CHEST, _BACK, 10, _FIST) + [[_t + 1050, _t + 2000, _FIST, _BACK]]
SCENARIOS['punch'] += [[8000, 10000, NEUTRAL, _BACK]]
# Push a closed door (use with --near door, which puts the right hand 18 cm before the middle of the leaf): slowly 0.4 m at
# 0.2 m/s (the leaf must follow the hand, ~20 deg), hold, take the hand back (the door must stay ajar: it never closes by
# itself), then a fast 0.7 m shove at 7 m/s (the native swing must open it fully: state Opened).
_DOOR_IN, _DOOR_SHOVE = [0.0, 0.0, -0.40], [0.0, 0.0, -0.70]
SCENARIOS['doorpush'] = ([[0, 1500, NEUTRAL, REST]] + glide(1500, 3500, REST, _DOOR_IN, 40) + [[3500, 6500, NEUTRAL, _DOOR_IN]] +
                         glide(6500, 7500, _DOOR_IN, REST, 10) + [[7500, 9500, NEUTRAL, REST]] +
                         glide(9500, 9600, REST, _DOOR_SHOVE, 10) + [[9600, 13000, NEUTRAL, _DOOR_SHOVE]])
DOOR_HELD_SEGMENTS = range(43, 54)  # script segments (scenario segment + 1): the hand going back and resting
SCENARIOS['all'] = [[0, 3000, NEUTRAL, None], [3000, 7000, {"move_y": 1.0}, None],
                    [7000, 10000, {"x": True}, [0.05, 0.2, -0.2]], [10000, 13000, {"x": True}, [-0.15, 0.25, -0.2]],
                    [13000, 16000, NEUTRAL, None]]
# Jump by gesture (HandsUpJump): both hands thrown up from the rest pose to 0.3 m above the eyes in 200 ms, held up 0.6 s
# (native A stays held: the mantle at a ledge), lowered, twice.
_UP = [0.0, 0.65, -0.1]
SCENARIOS['handsjump'] = [[0, 2000, NEUTRAL, REST, {"left_hand_offset": REST}]]
for k in range(2):
    t = 2000 + k * 3000
    SCENARIOS['handsjump'] += [s + [{"left_hand_offset": s[3]}] for s in glide(t, t + 200, REST, _UP, 10) + [[t + 200, t + 800, NEUTRAL, _UP]] +
                               glide(t + 800, t + 1100, _UP, REST, 10) + [[t + 1100, t + 3000, NEUTRAL, REST]]]
# Player-FSM states (tools/player_states.py) a scenario must reach: the mechanic really ran in the engine.


def held_sword(segments, draw_ms=2600):
    """Full grip (v412): a sword attacks only while its grip is squeezed. Draw it over the shoulder first, then hold the
    right grip through every segment of the original scenario (shifted by draw_ms)."""
    first = segments[0][3] or [0.0, 0.0, 0.0]
    pre = ([[0, 600, NEUTRAL, [0.0, 0.0, 0.0]]] + glide(600, 1200, [0.0, 0.0, 0.0], BACK, 6) + [[1200, 2000, _G, BACK]] +
           glide(2000, draw_ms, BACK, first, 6, _G))
    return pre + [[a + draw_ms, b + draw_ms, dict(inp, right_grip=1.0), hand or [0.0, 0.0, 0.0], *more] for a, b, inp, hand, *more in segments]


# Sprint, crouch into a slide, stand up, three times (headset v636: the native camera modes around sprints, slides and landings took both hands;
# arm_flicker publish_refused counts the frames the tracked arms were refused). Eye captures every 2 s.
_SPRINT = {"move_y": 1.0, "left_click": True}
SCENARIOS['slide'] = [[0, 2000, NEUTRAL, [0.0, 0.0, 0.0]]]
for _k in range(3):
    _t = 2000 + _k * 5000
    SCENARIOS['slide'] += [[_t, _t + 1200, _SPRINT, [0.0, 0.0, 0.0]], [_t + 1200, _t + 2600, _SPRINT, [0.0, 0.0, 0.0], {"head_offset": [0.0, -0.45, 0.0]}],
                           [_t + 2600, _t + 5000, NEUTRAL, [0.0, 0.0, 0.0], {"head_offset": [0.0, 0.0, 0.0]}]]
# Block against an alerted guard (use with --near npc-front; user, headset v636: "when I block an attack my right hand can just disappear"):
# the drawn sword held still in front of the chest (SwordBlockPose: RB held) for 24 s while the guard attacks; eye captures every 2 s.
SCENARIOS['blockduel'] = [[0, 24000, NEUTRAL, [-0.25, 0.05, -0.15]]]
PAIR_PLAN['blockduel'] = {}
# Choke with the LEFT hand while the sword is drawn and held in front (use with --near npc, 0.9 m behind the guard; user, headset v638:
# a grab from behind must register the choke). The sword in front holds the native Block (RB), so the choke's own RB press must be a
# fresh edge (v639). The left hand glides to the guard's neck, squeezes alone for 4.4 s and lets go.
_NECK_L = [0.2, 0.2, -0.35]
SCENARIOS['chokeleft'] = ([[0, 400, NEUTRAL, REST, {"left_hand_offset": REST}], [400, 700, {"left_grip": 1.0}, REST, {"left_hand_offset": REST}],   # holster the save's drawn pistol (as 'yank')
                           [700, 1500, NEUTRAL, REST, {"left_hand_offset": REST}]] +
                          [s + [{"left_hand_offset": s[3]}] for s in glide(1500, 2100, REST, _NECK_L, 6)] +
                          [[2100, 6500, {"left_grip": 1.0}, REST, {"left_hand_offset": _NECK_L}], [6500, 9000, NEUTRAL, REST, {"left_hand_offset": REST}]])
# glide() moved the LEFT hand above (its offsets went into left_hand_offset); the right hand (the sword) stays at rest
SCENARIOS['chokeleft'] = [[a, b, i, REST if len(s) > 4 and s[4].get('left_hand_offset') is not None else h, *s[4:]] for s in SCENARIOS['chokeleft'] for a, b, i, h in [s[:4]]]
# The same swings while looking down at the floor (v639): the game's kill sensor follows the camera and offers nothing, so a contact from
# behind or the side on the unaware guard must be the mod's own stealth kill (sword_contact.stealth[2], sword_grip.stealth_kill_pulses).
SCENARIOS['swinglow'] = [[a_, b_, i_, h_, {"head_pitch_degrees": -60}] for a_, b_, i_, h_, *_r in SCENARIOS['swing']]
# Left hand picks up a bottle/limb with the grip alone while the sword is drawn and held in the right hand (user, headset v642: "I cannot pickup body
# parts and bottles with my free hand when sword is in the right hand"). Use with --near bottle (or limb for 'leftlimb'); 'leftbottlenosword' = A/B.
_LB, _LB_UP, _LK = [0.2, -0.5, -0.2], [0.3, -0.1, -0.2], {"hand_pitch_degrees": -50, "head_pitch_degrees": -40}
SCENARIOS['leftbottlenosword'] = ([[0, 400, NEUTRAL, REST, {"left_hand_offset": REST}], [400, 700, {"left_grip": 1.0}, REST, {"left_hand_offset": REST}],
                                   [700, 1500, NEUTRAL, REST, {"left_hand_offset": REST}]] +
                                  [s + [dict(_LK, left_hand_offset=s[3])] for s in glide(1500, 2500, REST, _LB, 8)] +
                                  [[2500, 3500, NEUTRAL, REST, dict(_LK, left_hand_offset=_LB)], [3500, 4700, {"left_grip": 1.0}, REST, dict(_LK, left_hand_offset=_LB)]] +
                                  [s + [dict(_LK, left_hand_offset=s[3])] for s in glide(4700, 5500, _LB, _LB_UP, 8, {"left_grip": 1.0})] +
                                  [[5500, 7000, {"left_grip": 1.0}, REST, dict(_LK, left_hand_offset=_LB_UP)], [7000, 9000, NEUTRAL, REST, dict(_LK, left_hand_offset=_LB_UP)]])
SCENARIOS['leftbottlenosword'] = [[a, b, i, REST if len(s) > 4 and s[4].get('left_hand_offset') is not None else h, *s[4:]] for s in SCENARIOS['leftbottlenosword'] for a, b, i, h in [s[:4]]]
SCENARIOS['leftbottle'] = SCENARIOS['leftbottlenosword']
# The same with a severed part (--near limb): 'limbgrip' mirrored to the LEFT hand (user, headset v642: "I cannot pickup body parts and bottles with my
# free hand when sword is in the right hand"); leftlimb draws and holds the sword (in front: the Block), leftlimbnosword is the A/B.
_LT = [0.2, -0.95, -0.35]  # the left default is x -0.2: the same centre point as the right hand's _TORSO
SCENARIOS['leftlimbnosword'] = ([[0, 1500, NEUTRAL, REST, {"left_hand_offset": REST}]] +
                                [[a_, b_, i_, REST, dict(_CROUCH, left_hand_offset=h_)] for a_, b_, i_, h_ in glide(1500, 2500, REST, _LT, 5)] +
                                [[2500, 3500, NEUTRAL, REST, dict(_CROUCH, left_hand_offset=_LT)], [3500, 4300, {"left_grip": 1.0}, REST, dict(_CROUCH, left_hand_offset=_LT)],
                                 [4300, 7000, {"left_grip": 1.0}, REST, dict(_CROUCH, left_hand_offset=[0.2, -0.4, -0.3])],
                                 [7000, 9000, NEUTRAL, REST, {"head_pitch_degrees": 0, "head_offset": [0.0, 0.0, 0.0], "left_hand_offset": REST}]])
SCENARIOS['leftlimb'] = SCENARIOS['leftlimbnosword']
# The controls and gestures page (user, after v646): the right hand squeezes at the right temple (opens it), then again (closes it).
_TMP = [-0.05, 0.38, 0.35]  # grip at (0.15, 1.68, -0.05): 8 cm from the right temple point, outside the sword holster behind the shoulder
SCENARIOS['guidepage'] = ([[0, 1500, NEUTRAL, REST]] + glide(1500, 2500, REST, _TMP, 8) +
                          [[2500, 3000, NEUTRAL, _TMP], [3000, 3400, {"right_grip": 1.0}, _TMP], [3400, 3800, NEUTRAL, _TMP]] + glide(3800, 4600, _TMP, REST, 8) +
                          [[4600, 7500, NEUTRAL, REST]] + glide(7500, 8300, REST, _TMP, 8) +
                          [[8300, 8700, {"right_grip": 1.0}, _TMP], [8700, 9000, NEUTRAL, _TMP]] + glide(9000, 9800, _TMP, REST, 8) + [[9800, 12000, NEUTRAL, REST]])
PAIR_TIMES['guidepage'] = [(5500, 'guide-open'), (7000, 'guide-open-b'), (11000, 'guide-closed')]
# The paged guide (v652): open it, three right-stick flicks to the right (each 200 ms, then neutral), one back, then close.
# Pass: the hint overlay uploads rise with the pages and the snap-turn counters do not move while it is open.
_FLICK = {"turn_x": 1.0}
SCENARIOS['guidepages'] = (SCENARIOS['guidepage'][:20] + [[4600, 5200, NEUTRAL, REST], [5200, 5400, _FLICK, REST], [5400, 6000, NEUTRAL, REST],
                           [6000, 6200, _FLICK, REST], [6200, 6800, NEUTRAL, REST], [6800, 7000, _FLICK, REST], [7000, 7500, NEUTRAL, REST],
                           [7500, 7700, {"turn_x": -1.0}, REST], [7700, 8000, NEUTRAL, REST]] +
                           [[a_ + 500, b_ + 500, *r_] for a_, b_, *r_ in SCENARIOS['guidepage'][21:]])
# v643 (user, headset v642: cutscenes keep the avatar body and the tracked hands): load SLOT22 (void_01_p, a mission start that plays the
# Outsider cinematic, logic 32, ~4-14 s after the load) and stand still, the right hand forward where it shows; no buttons (a press skips it).
# Pass: dxgi-probe-result.json cutscene.hands_frames > 0 and the eye pairs show the hands (and the body when looking down).
# Both hands at eye level out to the sides, where no native animation puts them (A/B with build/legacy-cutscene-hands).
_CUT_R, _CUT_L = [0.25, 0.3, -0.15], {"left_hand_offset": [-0.25, 0.3, -0.15]}
SCENARIOS['cutscene'] = [[0, 500, NEUTRAL, _CUT_R, _CUT_L], [500, 600, NEUTRAL, _CUT_R, dict(_CUT_L, console="loadgame 22")], [600, 10000, NEUTRAL, _CUT_R, _CUT_L],
                         [10000, 14000, NEUTRAL, _CUT_R, dict(_CUT_L, head_pitch_degrees=-50)], [14000, 60000, NEUTRAL, _CUT_R, _CUT_L]]
PAIR_TIMES['cutscene'] = [(5000, 'cut-a'), (7500, 'cut-b'), (9500, 'cut-c'), (12000, 'cut-down-a'), (13500, 'cut-down-b'), (17000, 'cut-d')]
SCENARIOS['bottlepistol'] = [[0, 1500, NEUTRAL, SCENARIOS['hold'][0][3], {"command": "equip pistol"}]] + SCENARIOS['hold'][1:]
for _name in ('swing', 'chop', 'duel', 'blockduel', 'swarm', 'floor', 'chokeleft', 'swinglow', 'leftbottle', 'leftlimb'):
    SCENARIOS[_name] = held_sword(SCENARIOS[_name])


def stab_with_gun(trigger):
    """v920 (user, headset v891: "a choke (grab) instead of the stealth kill with the sword"): 'swing' with a pistol drawn in the LEFT hand, so the sword hand
    is the choke hand (choke_hand), and its hold-to-hold squeeze must not take the sensor's choke offer. Use with --near npc --blind-ai (0.9 m behind a guard).
    trigger: also squeeze the right trigger after the draw (the explicit Block, as the user holds the sword)."""
    base = [[0, 500, NEUTRAL, [-0.4, 0.0, -0.3]]]   # 'swing' with the first stroke 1.5 s earlier: a re-injected run (no --launch) ends its blind-AI window ~3 s into the script
    for k in range(3):
        t = 500 + k * 1500
        base += [[t + i * 30, t + (i + 1) * 30, NEUTRAL, [-0.4 + i * 0.08, 0.0, -0.3]] for i in range(10)] + [[t + 300, t + 1500, NEUTRAL, [-0.4, 0.0, -0.3]]]
    out = [[a, b, dict(i, right_trigger=1.0) if trigger and a >= 2600 else i, h, *more] for a, b, i, h, *more in held_sword(base)]
    out[0] = out[0][:4] + [{"command": "equip pistol"}]
    return out


SCENARIOS['stabgun'] = stab_with_gun(False)
SCENARIOS['stabguntrig'] = stab_with_gun(True)
FORBIDDEN_STATES = {'flick': ['Slash'], 'reach': ['Slash']}  # accidental attacks
EXPECTED_STATES = {'leftlimb': ['HoldObject'], 'leftlimbnosword': ['HoldObject'], 'pickuplook': ['LootObject', 'Interact', 'HoldObject'], 'carry': ['CarryCorpse', 'PickupCorpse', 'Interact'], 'carrydrop': ['CarryCorpse'], 'carrythrow': ['CarryCorpse'], 'chop': ['Slash'], 'pickup': ['LootObject', 'Interact', 'HoldObject'], 'reach': ['LootObject', 'Interact', 'HoldObject'], 'swing': ['Slash', 'Assassination'], 'duel': ['Slash'], 'grab': ['LootObject', 'HoldObject', 'Interact', 'CarryCorpse', 'ReadNote'], 'powers': ['UsePower', 'Blinking', 'UseWindblast', 'UsePhantasmalLink', 'InBendTime', 'StoneShroud', 'DoppelgangerSwap', 'UseHeart'], 'jump': ['Jumping', 'Falling'],  # Falling always follows a jump; the short Jumping state can fall between samples
                   'lean': ['Leaning'], 'power': ['UsePower', 'Blinking'], 'wheel': ['Powerwheel'],
                   'walk': ['Running', 'Sneaking', 'Sprinting'], 'all': ['Powerwheel'], 'potion': ['Powerwheel'], 'hide': ['Powerwheel'], 'sheathe': ['Undraw'], 'swordgrip': ['EquipChange'], 'pistolhip': ['EquipChange'], 'pistolfire': ['FireWeapon', 'ReloadWeapon']}
EXPECTED_STATES['limbgrip'] = ['HoldObject']
EXPECTED_STATES['leftbottle'] = EXPECTED_STATES['leftbottlenosword'] = ['HoldObject']
EXPECTED_STATES['limbgripblind'] = ['HoldObject']
EXPECTED_STATES['chokeleft'] = ['Choke']
EXPECTED_STATES['swinglow'] = ['Running']  # no Assassination expected: the kill is the mod's (counters)  # v639: the left hand's grip behind a guard, sword drawn and blocking
EXPECTED_STATES['handsjump'] = ['Jumping', 'Falling']  # gesture jump: Falling always follows a jump
SIM_FLAGS = ['--paired', '--native-eye-capture', '--vr-preview', '--head-tracking', '--controllers', '--symmetric-frustum',
             '--stable-camera', '--tracked-arms', '--physical-grab', '--native-left-aim-experiment', '--controller-aim',
             '--vr-turning', 'snap', '--simulated-vr', '--simulated-input']


def game_pid():
    out = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq Dishonored2.exe', '/FO', 'CSV', '/NH'], capture_output=True, text=True).stdout
    for line in out.splitlines():
        parts = [p.strip('"') for p in line.split(',')]
        if len(parts) > 1 and parts[0].lower() == 'dishonored2.exe':
            return int(parts[1])
    return None


def keys(*codes):
    subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'game_keys.py'), *[hex(c) for c in codes]], capture_output=True)


LEAD_MS = 25000  # scenarios start with this much neutral time while the game is driven into gameplay


def player_position(read, base):
    """Native camera origin (arkCpntPlayerCamera aim origin), for movement/power checks."""
    import struct
    from rig_fields import pointer, native_list
    from rtti_fields import rtti_name
    game = pointer(read, base + 0x39a5290)
    player = pointer(read, game + 0x3130b0)
    data, count = native_list(read, player + 0x268, 8, 4096)
    for at in struct.unpack('<' + 'Q' * count, read(data, count * 8)):
        if (rtti_name(read, base, at) or '').endswith('arkCpntPlayerCamera@@'):
            return struct.unpack('<3f', read(at + 0x2527c, 12))
    return None


def steam_account_ok():
    """True when Steam is signed into the account the save folder belongs to (steam_autocloud.vdf). 2026-10-31: a launch under the
    user's other account let Steam Cloud rewrite the save folder for it and empty every slot (restored from a backup)."""
    import re
    import winreg
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r'Software\Valve\Steam\ActiveProcess') as k:
            active = int(winreg.QueryValueEx(k, 'ActiveUser')[0])
        vdf = os.path.join(os.environ['USERPROFILE'], 'Saved Games', 'Arkane Studios', 'Dishonored2', 'base', 'savegame', 'steam_autocloud.vdf')
        owner = int(re.search(r'"accountid"\s+"(\d+)"', open(vdf).read()).group(1))
    except (OSError, AttributeError, ValueError):
        return False
    return active == owner


COMMAND_FILE = os.path.join(ART, 'vr-command.txt')
RESULT_FILE = os.path.join(ART, 'vr-command-result.txt')


def command(line):
    with open(COMMAND_FILE, 'a') as f:
        f.write(line + chr(10))


def command_result_offset():
    """Current end of the shared result file: only answers appended after this are ours."""
    try:
        return os.path.getsize(RESULT_FILE)
    except OSError:
        return 0


def command_result_tail(offset):
    try:
        with open(RESULT_FILE, 'r', encoding='utf-8', errors='ignore') as f:
            f.seek(offset)
            return f.read()
    except OSError:
        return ''


def wait_for_command_results(offset, expected, timeout=30):
    """Wait until every expected answer appears after `offset`. The mod appends one line per command after it
    consumes vr-command.txt; there is no other acknowledgement, so this is the only way to know a pre-load or
    post-injection command was really processed."""
    end = time.time() + timeout
    text = ''
    while time.time() < end:
        text = command_result_tail(offset)
        if all(item in text for item in expected):
            return True, text
        time.sleep(.25)
    return False, text


def apply_background_cvars(timeout=30):
    """Set the two focus cvars in the RUNNING game through the mod command channel.

    `--prepare-config` is only a next-launch hint: the engine rewrites dishonored2Config.cfg from archived
    cvars, and a game that was already running never re-reads it. Sending the cvars after injection is the
    only path that changes the current process. find_cvar() in src/dxgi_probe.cpp resolves both static
    idCVars by name, so this is the same static write the engine's own console command uses.
    """
    offset = command_result_offset()
    command('cvar in_controlInactiveWindow 1')
    command('cvar win_pauseOnFocusLoss 0')
    # 2026-11-04: the post-load "press any button" screen ignores the virtual pad in an unfocused sim window; the engine's own
    # com_loadScreenUserConfirmationDisabled skips that confirmation (runtime only, a game restart restores the default).
    command('cvar com_loadScreenUserConfirmationDisabled 1')
    return wait_for_command_results(offset, ('in_controlInactiveWindow=1 ok', 'win_pauseOnFocusLoss=0 ok',
                                             'com_loadScreenUserConfirmationDisabled=1 ok'), timeout)


def set_god(pid, on, buddha=False):
    """`god player` toggles ark_god_player (int at +0x28 of the cvar at 0x398d500): send it only when the state differs."""
    import struct
    base, read = attach(pid)
    at, cmd = (0x398d628, 'buddha player') if buddha else (0x398d528, 'god player')  # handlers 0x718e20 / 0x718f00
    for _ in range(3):  # verified: an unconfirmed toggle let a guard kill the player (Game Over, 2026-10-12)
        if bool(struct.unpack('<i', read(base + at, 4))[0]) == on:
            return True
        command('console ' + cmd)
        time.sleep(1.5)
    ok = bool(struct.unpack('<i', read(base + at, 4))[0]) == on
    if not ok:
        print('warning: ark_god_player did not change')
    return ok


def drive_to_gameplay(pid, timeout=LEAD_MS / 1000 - 3):
    """Drive main-menu, pause and game-over choices; only native InGame is success."""
    base, read = attach(pid)
    end = time.time() + timeout
    while time.time() < end:
        try:
            mask = game_logic_mask(read, base)
        except Exception:
            mask = None
        if mask == 2:
            return True
        if mask == 64:
            command('pad a 200')
        elif mask in (4, 1024):
            command('pad b 200')  # pause menu / black market: back to the game
        elif mask == 8192:
            command('pad a 200')  # Game Over: "Restart From Last Checkpoint" is the default choice
        time.sleep(2)
    return False


SKIP_NAMES, NPC_INDEX = [], 0  # --skip-npc / --npc-index


def npc_stand(pid, behind, civilian=False, beside=False, skip=(), dist=.9):
    """Eye position and yaw 0.9 m behind (or in front of) the nearest living guard (any floor), facing it.
    NPC facing = minus the first row of the arkCpntReferenceFrame 3x3 at +0x3c (live test 2026-10-12)."""
    import math
    import struct
    from entities import World
    def live():
        out = json.loads(subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'entities.py'), '--pid', str(pid), '--kind', 'npc', '--health', '--json'],
                                        capture_output=True, text=True).stdout)
        return out, [e for e in out['entities'] if (e.get('health') or {}).get('cur', 0) > 0 and not (e.get('health') or {}).get('dead')]
    listing, first = live()
    time.sleep(.8)
    listing, npcs = live()
    npcs = [e for e in npcs if e['addr'] not in skip and (e.get('health') or {}).get('max', 0) >= 10]  # 1-hp scripted extras take no hits (sim v382)
    npcs = [e for e in npcs if not any(n in (e.get('name') or '') for n in SKIP_NAMES)]
    # 1-hp guards with full max health are dormant pool entries nobody stands at (sim v639: an empty room behind 'def_50376'): real ones first
    npcs = [e for e in npcs if (e.get('health') or {}).get('cur', 0) >= 10] or npcs
    feet = listing['player']['pos']
    before = {e['addr']: e['pos'] for e in first}
    # Patrolling guards walked past the player before the first swing: prefer NPCs standing still, guards first.
    still = [e for e in npcs if e['addr'] in before and math.dist(before[e['addr']], e['pos']) < .15]
    npcs = sorted(still, key=lambda e: (bool(e.get('guard')) == civilian, e['dist'])) or npcs
    if not npcs:
        return None, None
    print('npc candidates (name, pos, hp, dist, guard, still):', [(e.get('name', '')[-34:], [round(v, 1) for v in e['pos']], (e.get('health') or {}).get('cur'),
                                                                    round(e['dist'], 1), bool(e.get('guard')), e in still) for e in npcs[:8]])
    npc = npcs[min(NPC_INDEX, len(npcs) - 1)]
    base, read = attach(pid)
    fwd = None
    for comp, name in World(read, base).components(int(npc['addr'], 16)):
        if name.startswith('arkCpntReferenceFrame'):
            fwd = struct.unpack('<3f', read(comp + 0x3c, 12))
            break
    try:  # Task 0 (v651): entity +0x300 position and +0x30c matrix row 0, the data sword_contact_policy.hpp attacker_rear_or_side reads
        ea = int(npc['addr'], 16)
        print('entity300', [round(v, 3) for v in struct.unpack('<3f', read(ea + 0x300, 12))], 'entity30c rows', [round(v, 3) for v in struct.unpack('<9f', read(ea + 0x30c, 36))], 'comp3c', [round(v, 3) for v in fwd] if fwd else None)
    except Exception as ex:
        print('entity read failed', ex)
    if not fwd or math.hypot(fwd[0], fwd[1]) < .1:
        fwd = (feet[0] - npc['pos'][0], feet[1] - npc['pos'][1], 0)  # unknown heading: approach from the player's side
        behind = False
    d = math.hypot(fwd[0], fwd[1])
    fx, fy = fwd[0] / d, fwd[1] / d
    if beside:  # at the NPC's right/left: neither a takedown from behind nor face to face
        fx, fy = -fy, fx
    side = -1 if behind else 1  # +row0 is his FORWARD (sim v644: a patrolling guard walks along +row0, median cos +1.00); behind = -row0
    x, y = npc['pos'][0] + side * fx * dist, npc['pos'][1] + side * fy * dist
    yaw = math.degrees(math.atan2(npc['pos'][1] - y, npc['pos'][0] - x))
    return (x, y, npc['pos'][2] + 1.65, yaw), npc


def door_frame(read, base, door):
    """An arkDoor (vtable 0x1cb7c68): its arkCpntOpenable (door+0x370) state (+0x48, next +0x4c: 0 closed, 1 animated, 2 opened),
    locked (+0x1e8), usable (+0x20), peeking (+0x1ea), broken (+0x1ed), NPC locker (door+0x378, 0x3fff0010 = free) and the
    hinge frame of its animated part (entity id cpnt+0xdc): origin +0x300 = hinge, axis rows +0x30c (normal), +0x318."""
    import struct
    q = lambda at: struct.unpack('<Q', read(at, 8))[0]
    u = lambda at: struct.unpack('<I', read(at, 4))[0]
    cpnt = q(door + 0x370)
    part_id = u(cpnt + 0xdc)
    part = q(q(base + 0x39a5290) + 0x3130b0 + (part_id >> 16) * 8)
    f = struct.unpack('<12f', read(part + 0x300, 48))
    return dict(cpnt=cpnt, state=u(cpnt + 0x48), next=u(cpnt + 0x4c), locked=read(cpnt + 0x1e8, 1)[0], usable=read(cpnt + 0x20, 1)[0],
                peek=read(cpnt + 0x1ea, 1)[0], broken=read(cpnt + 0x1ed, 1)[0], locker=u(door + 0x378), model=q(door + 0xf0),
                hinge=f[0:3], x=f[3:6], y=f[6:9])


def leaf_angle(read, door, fr, s):
    """Opening angle (deg) of the leaf in the swing sense s: the centre of its drawn mesh's world box (+0x1840) around the hinge."""
    import math
    import struct
    box = struct.unpack('<8f', read(fr['model'] + 0x1840, 32))
    c = [(box[i] + box[i + 4]) / 2 - fr['hinge'][i] for i in range(3)]
    dx, dy = sum(c[i] * fr['x'][i] for i in range(3)), sum(c[i] * fr['y'][i] for i in range(3))
    return math.degrees(math.atan2(s * dx, -dy))


def door_stand(pid):
    """Eye position and yaw in front of the nearest closed, free, unlocked door on the player's floor, on the player's side,
    so that the simulated right hand (0.4 m ahead, 0.2 m right, 0.35 m down) rests 18 cm before the middle of the leaf
    (doors.edgeskl: the closed leaf runs from the hinge along the part's -Y, 0.75 m to its middle; +X is the normal)."""
    import math
    import struct
    base, read = attach(pid)
    game = struct.unpack('<Q', read(base + 0x39a5290, 8))[0]
    registry = []
    for at in range(0, 0x3fff, 0x200):  # in chunks: one 128 KB read fails when a page of it is not readable
        n = min(0x200, 0x3fff - at)
        try:
            registry += struct.unpack('<%dQ' % n, read(game + 0x3130b0 + at * 8, n * 8))
        except (OSError, ValueError):
            registry += [0] * n
    eye = player_position(read, base)
    best = None
    seen = []  # every arkDoor: (distance + 100 if scripted, addr, state, next, locked, broken, peek, locker, usable, floor dz)
    for ent in registry[1:]:
        if not 0x10000 <= ent < 0x7fff00000000:
            continue
        try:
            if struct.unpack('<Q', read(ent, 8))[0] != base + 0x1cb7c68:
                continue
            fr = door_frame(read, base, ent)
        except (OSError, ValueError):  # the reader raises ValueError('bad read') on an unmapped field
            continue
        h = fr['hinge']
        d = math.hypot(h[0] - eye[0], h[1] - eye[1])
        try:  # scripted doors (latches, story locks) after ordinary ones: the Black Market building door's latch took the hand (sim v464)
            np_ = struct.unpack('<Q', read(ent + 0x20, 8))[0]
            if read(np_, 7) == b'script_':
                d += 100
        except (OSError, ValueError):
            pass
        seen.append((round(d, 1), hex(ent), fr['state'], fr['next'], fr['locked'], fr['broken'], fr['peek'], hex(fr['locker']), fr['usable'], round(h[2] - (eye[2] - 1.65), 2)))
        if (fr['state'], fr['next'], fr['locked'], fr['broken'], fr['peek'], fr['locker']) != (0, 0, 0, 0, 0, 0x3fff0010) or not fr['usable']                 or abs(h[2] - (eye[2] - 1.65)) > 1.0 or d > 130 or (best and d >= best[0]):
            continue
        best = (d, ent, fr)
    print('doors (d, addr, state, next, locked, broken, peek, locker, usable, dz):', sorted(seen)[:12])
    if not best:
        return None, None
    _, ent, fr = best
    h, x, y = fr['hinge'], fr['x'], fr['y']
    s = 1 if sum((h[i] - eye[i]) * x[i] for i in range(2)) >= 0 else -1  # the native Use's side rule: the player on -X opens CCW
    stand = [h[i] - s * .58 * x[i] + (-.75 + s * .2) * y[i] for i in range(2)] + [h[2] + 1.65]
    yaw = math.degrees(math.atan2(s * x[1], s * x[0]))
    return (*stand, yaw), dict(addr=ent, s=s, **fr)


def ragdoll(pid, entity):
    """The 17 hknp ragdoll bodies of an arkNPC: [(position, flags)], or None. Handles at arkCpntPhysicsSystemEvolved
    (vtable 0x1c54008) +0x100 -> idList (data +0x28, count +0x30 = 17; the data is often inline at +0x120); pool records
    (stride 0xb0): transform +0, flags +0x44 (sim v399, v429)."""
    import struct
    from entities import World
    base, read = attach(pid)
    q = lambda at: struct.unpack('<Q', read(at, 8))[0]
    physics = next((c for c, name in World(read, base).components(entity) if name.startswith('arkCpntPhysicsSystemEvolved')), None)
    if not physics:
        return None
    try:  # a living guard has no ragdoll yet: its handle list is garbage (sim v418)
        world = q(q(base + 0x39a5290) + 0x60410); pool = q(world + 0x28)
        lst = q(physics + 0x100)
        if struct.unpack('<I', read(lst + 0x30, 4))[0] != 17:
            return None
        handles = struct.unpack('<17I', read(q(lst + 0x28), 68))
        out = []
        for h in handles:
            rec = pool + (h & 0xffffff) * 0xb0
            out.append((struct.unpack('<3f', read(rec + 0x30, 12)), struct.unpack('<I', read(rec + 0x44, 4))[0]))
        return out
    except OSError:
        return None


def corpse_stand(pid, torso=False):
    """Eye position and yaw 0.55 m past the right wrist (ragdoll body 10) of the nearest corpse with dynamic ragdoll bodies,
    facing the wrist, plus (corpse entity, pelvis position). torso: 0.55 m beside the pelvis instead, across the body axis."""
    import math
    out = json.loads(subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'entities.py'), '--pid', str(pid), '--kind', 'npc', '--health', '--json'],
                                    capture_output=True, text=True).stdout)
    dead = sorted([e for e in out['entities'] if (e.get('health') or {}).get('dead')], key=lambda e: e['dist'])
    for e in dead[:12]:
        bodies = ragdoll(pid, int(e['addr'], 16))
        if not bodies or any(f & 7 != 2 for _, f in bodies):
            continue
        pelvis, wrist = bodies[0][0], bodies[10][0]
        dx, dy = wrist[0] - pelvis[0], wrist[1] - pelvis[1]
        if torso:  # beside the pelvis (sim v629: beside the chest the palm took the arm lying on it)
            chest, wrist = bodies[2][0], pelvis
            dx, dy = chest[1] - pelvis[1], -(chest[0] - pelvis[0])  # across the pelvis-chest axis
        d = math.hypot(dx, dy)
        if d < .2:
            continue
        x, y = wrist[0] + dx / d * .55, wrist[1] + dy / d * .55
        yaw = math.degrees(math.atan2(wrist[1] - y, wrist[0] - x))
        return (x, y, wrist[2] + 1.6, yaw), (int(e['addr'], 16), pelvis)
    return None, None


def old_corpse_stand(pid):
    """Eye position and yaw 0.6 m from the nearest dead body whose ragdoll is not live (settled corpses: static or not instanced),
    facing it, plus (corpse entity, reference-frame position). The hand's squeeze must wake it (NativeWeaponHands::wake)."""
    import math
    import struct
    from entities import World
    out = json.loads(subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'entities.py'), '--pid', str(pid), '--kind', 'npc', '--health', '--json'],
                                    capture_output=True, text=True).stdout)
    feet = out['player']['pos']
    dead = sorted([e for e in out['entities'] if (e.get('health') or {}).get('dead')], key=lambda e: e['dist'])
    base, read = attach(pid)
    for e in dead[:20]:
        bodies = ragdoll(pid, int(e['addr'], 16))
        if bodies and all(f & 7 == 2 for _, f in bodies):
            continue  # live ragdoll: the ordinary drag test covers it
        fr = next((c for c, nm in World(read, base).components(int(e['addr'], 16)) if nm.startswith('arkCpntReferenceFrame')), None)
        if not fr:
            continue
        p = struct.unpack('<3f', read(fr + 0x30, 12))
        dx, dy = feet[0] - p[0], feet[1] - p[1]
        d = math.hypot(dx, dy) or 1
        x, y = p[0] + dx / d * .6, p[1] + dy / d * .6
        return (x, y, p[2] + 1.65, math.degrees(math.atan2(p[1] - y, p[0] - x))), (int(e['addr'], 16), p)
    return None, None


def target_hp(pid, npc):
    """(hp when chosen, hp now) of the scenario's target NPC: a takedown or a kill between watcher polls still counts."""
    try:
        out = json.loads(subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'entities.py'), '--pid', str(pid), '--kind', 'npc', '--health', '--json'],
                                        capture_output=True, text=True).stdout)
        now = next(e for e in out['entities'] if e['addr'] == npc['addr'])
        return ((npc.get('health') or {}).get('cur'), (now.get('health') or {}).get('cur'))
    except Exception:
        return None


def combat_events():
    import csv
    try:
        return [(int(r[0]), r[1]) for r in csv.reader(open(os.path.join(ART, 'native-combat.csv'))) if r]
    except OSError:
        return []


def combat_latency(check, target=None):
    """Swing/contact timeline (mod: artifacts/native-combat.csv) against NPC health drops (npc_watch), same GetTickCount64 clock."""
    import csv
    try:
        events = [(int(r[0]), r[1]) for r in csv.reader(open(os.path.join(ART, 'native-combat.csv'))) if r]
    except OSError:
        events = []
    try:
        drops = [(int(r['tick_ms']), float(r['prev_hp'] or 0), float(r['hp'])) for r in csv.DictReader(open(os.path.join(ART, 'npc-watch.csv')))
                 if r['event'] == 'change' and r['prev_hp'] and float(r['hp']) < float(r['prev_hp'])]
    except OSError:
        drops = []
    for tick, what in events:
        after = [d for d in drops if d[0] >= tick]
        print('combat %s at %d -> %s' % (what, tick, ('hp %.0f->%.0f after %d ms' % (after[0][1], after[0][2], after[0][0] - tick)) if after else 'no damage'))
    swings = [t for t, w in events if w in ('slash', 'thrust', 'overhead', 'contact_hit')]
    lat = [min((d[0] - t for d in drops if 0 <= d[0] - t <= 1500), default=None) for t in swings]
    lat = [x for x in lat if x is not None]
    hurt = bool(target and target[0] is not None and target[1] is not None and target[1] < target[0])
    check('guard took damage', bool(drops) or hurt, f'{len(drops)} health drops, swings {len(swings)}, latencies ms {lat}, target hp {target}')


def script_segment():
    """The running gameplay script's segment (0 = the lead-in, k+1 = scenario segment k), from the mod's live command log;
    -1 when unknown. The process clock runs ~2 s ahead of the script (injection), so timed actions use this."""
    try:  # written to artifacts/ while the session runs (archived into the run folder at its end)
        with open(os.path.join(ART, 'simulated-gameplay-commands.csv')) as f:
            last = f.read().strip().splitlines()[-1].split(',')
        return int(last[1])
    except (OSError, ValueError, IndexError):
        return -1


_SE = [None, 0.0]  # (last logged script elapsed ms, wall clock when it changed)


def script_ms():
    """The running script's clock (ms, lead-in included): the last row of the mod's command log plus the wall time since it changed; None when unknown."""
    try:
        with open(os.path.join(ART, 'simulated-gameplay-commands.csv')) as f:
            ms = int(f.read().strip().splitlines()[-1].split(',')[0])
    except (OSError, ValueError, IndexError):
        return None
    if ms != _SE[0]:
        _SE[0], _SE[1] = ms, time.time()
    return ms + (time.time() - _SE[1]) * 1000


def bottle_sampler(pid, addr, out, stop, rot=None):
    """Thread: the entity's reference-frame position (+0x30) every 40 ms as (script ms - LEAD_MS, x, y, z)."""
    import struct
    from entities import World
    pb, pr = attach(pid)
    fr = next(c for c, nm in World(pr, pb).components(addr) if nm.startswith('arkCpntReferenceFrame'))
    while not stop.is_set():
        sm = script_ms()
        if sm is not None:
            try:
                raw = struct.unpack('<12f', pr(fr + 0x30, 48))  # position (3) + the 3x3 rotation at +0x3c
                out.append((round(sm - LEAD_MS), *raw[:3]))
                if rot is not None:
                    rot.append((round(sm - LEAD_MS), *raw[3:]))
            except Exception:
                pass
        stop.wait(0.04)


def write_scenario(name):
    segments = []
    for start, end, inp, hand, *more in SCENARIOS[name]:
        seg = {"start_ms": start + LEAD_MS, "end_ms": end + LEAD_MS, "head_pitch_degrees": -40 if name == 'pickup' else 0, "input": inp}
        if hand:
            seg["left_hand_offset" if name in ('potion', 'hide', 'floorleft', 'waist', 'pistolhip', 'yank', 'punch') else "right_hand_offset"] = hand  # the wheel cursor belongs to the X (left) hand
        if name in ('pickup', 'pickuplook', 'reach', 'hold', 'holdup', 'bottlepistol', 'deathreload', 'deathreloadpush'):
            seg["hand_pitch_degrees"] = -50  # point the hands at the floor item
            seg["head_pitch_degrees"] = -40
        if name == 'aim':
            seg["hand_pitch_degrees"] = -20
        for extra in more:
            seg.update(extra)
        segments.append(seg)
    segments.insert(0, {"start_ms": 0, "end_ms": LEAD_MS, "head_pitch_degrees": 0, "input": NEUTRAL})
    path = os.path.join(ART, 'claude', f'selftest-{name}.json')
    os.makedirs(os.path.dirname(path), exist_ok=True)
    write_scenario.segments = segments  # with "command" (mod test commands the selftest sends itself, as for --script)
    json.dump({"segments": [{k: v for k, v in sg.items() if k != 'command'} for sg in segments]}, open(path, 'w'))
    return path, (SCENARIOS[name][-1][1] + LEAD_MS) / 1000


def to_png(dump_dir, out_dir):
    from PIL import Image
    os.makedirs(out_dir, exist_ok=True)
    frames = []
    for j in sorted(glob.glob(os.path.join(dump_dir, '*.json'))):
        meta = json.load(open(j))
        raw = open(j[:-5] + '.rgba', 'rb').read()
        im = Image.frombytes('RGBA', (meta['width'], meta['height']), raw)
        bg = Image.new('RGBA', im.size, (40, 40, 40, 255))
        bg.alpha_composite(im)
        name = os.path.basename(j)[:-5]
        bg.convert('RGB').resize((meta['width'] // 3, meta['height'] // 3)).save(os.path.join(out_dir, name + '.png'))
        frames.append((name, meta, im))
    return frames


def edge_clipped(im, band=3):
    """Fraction of opaque pixels touching the left/right texture edge (clipped HUD content)."""
    a = im.getchannel('A')
    w, h = im.size
    edge = sum(1 for x in list(range(band)) + list(range(w - band, w)) for y in range(0, h, 4) if a.getpixel((x, y)) > 16)
    return edge / max(1, 2 * band * (h // 4))


def opaque_fraction(im):
    a = im.getchannel('A').resize((im.size[0] // 8, im.size[1] // 8))
    data = list(a.tobytes())
    return sum(1 for v in data if v > 16) / max(1, len(data))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--dll', required=True)
    ap.add_argument('--scenario', default='all', choices=sorted(SCENARIOS))
    ap.add_argument('--load-save', action='store_true', help='game is at the title screen: load the last save')
    ap.add_argument('--launch', action='store_true', help='start the game from Steam (background) and load the last save')
    ap.add_argument('--near', choices=['item', 'bottle', 'npc', 'npc-front', 'npc-side', 'civ-front', 'corpse', 'oldcorpse', 'shop', 'door', 'limb'],
                    help='teleport next to the nearest pickable item / 0.9 m behind (npc) or in front of (npc-front) the nearest guard, facing it')
    ap.add_argument('--teleport', nargs=3, type=float, metavar=('X', 'Y', 'Z'), help='engine setviewpos (eye position) before the scenario')
    ap.add_argument('--pre-teleport', nargs=3, type=float, metavar=('X', 'Y', 'Z'), help='setviewpos before --near picks its target (stream in an area: the test save start has only dormant guards near it since v639)')
    ap.add_argument('--damage', metavar='DECL', help='hurt the player every 3 s with this damage decl (engine `damage player1 DECL`, god off)')
    ap.add_argument('--inject-at-title', action='store_true',
                    help='with --launch: inject at the title screen, then load the save (UI movies loaded with the level get the owned-load patches, as the headset launcher does)')
    ap.add_argument('--blind-ai', action='store_true', help='NPC vision budget ark_vision_max_checks 0 during the scenario (restored to 150): guards do not spot or kill the test player (unverified live)')
    ap.add_argument('--item', metavar='NAME', help='with --near item: the nearest pickup whose entity name contains NAME (e.g. coin)')
    ap.add_argument('--script', metavar='FILE', help='play this gameplay script JSON as is (e.g. artifacts/claude/trace-replay-auto.json: a recorded human session) instead of a scenario; god mode + invulnerable')
    ap.add_argument('--skip-npc', metavar='SUBSTR[,SUBSTR]', help='--near npc*: never pick an NPC whose entity name contains one of these (e.g. def_10939)')
    ap.add_argument('--npc-index', type=int, default=0, help='--near npc*: take the Nth nearest candidate (default 0)')
    ap.add_argument('--script-shift', nargs=3, type=float, metavar=('X', 'Y', 'Z'), help='--script of a scenario-relative file: added to every right_hand_offset (metres)')
    ap.add_argument('--script-head-pitch', type=float, default=-53.0, help='--script of a scenario-relative file: head pitch where a segment sets none (default -53: looking at the bottle of --near bottle)')
    ap.add_argument('--script-hand-pitch', type=float, default=0.0, help='--script of a scenario-relative file: hand pitch where a segment sets none')
    ap.add_argument('--pairs-at', metavar='T,T,..', help='eye pairs at these scenario seconds (script clock)')
    ap.add_argument('--bottle-name', metavar='REGEX', help='--near bottle: take the nearest carryable whose entity name matches this (e.g. glass|cup|mug|bottle_01); --npc-index picks the Nth; the eye stands 1.2 m above the item as for bottle_02')
    ap.add_argument('--headset-size', action='store_true', help='render at the headset eye size (inject_probe --headset-size; build/sim-eye-size sets it, e.g. "2895 3095")')
    ap.add_argument('--every', metavar='CMD', help='send this mod test command every 3 s during the scenario (e.g. "pad x 200")')
    ap.add_argument('--lead-ms', type=int, help='with --inject-at-title: the lead-in before the scenario (default 75000; a slow save load needs more; lead + scenario + 12 s must stay under the injector 120 s)')
    a = ap.parse_args()
    SKIP_NAMES[:] = [n for n in (a.skip_npc or '').split(',') if n]
    global NPC_INDEX
    NPC_INDEX = a.npc_index
    if a.launch and not game_pid():
        if not steam_account_ok():
            sys.exit('Steam is signed into another account than the saves belong to: a launch would let Steam Cloud rewrite the save folder')
        # 2026-11-07: a sim launch ran over the user's No Man's Sky session. Steam tracking any other game (or SteamVR) = the user is playing.
        if subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'other_game_running.py')]).returncode != 0:
            sys.exit('Another game (or SteamVR) is running: the user is playing, no launch')
        subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'game_keys.py'), '--prepare-config'], check=True)
        os.startfile('steam://rungameid/403640')
        for _ in range(90):
            if game_pid():
                break
            time.sleep(2)
        time.sleep(45)  # title screen
        a.load_save = True
    pid = game_pid()
    if not pid:
        sys.exit('Dishonored 2 is not running (start it, or use --launch)')
    global LEAD_MS
    if a.inject_at_title:
        LEAD_MS = a.lead_ms or 75000  # the save loads inside the session: the scenario starts after the load
    elif a.load_save:  # title -> Campaign -> Continue (posted to the window; the load screen is dismissed by the mod's pad)
        keys(0x0D, 0x0D, 0x0D)
        time.sleep(20)
    if a.lead_ms and not a.inject_at_title:  # v1072: a slow save load after --launch needs a longer lead-in too
        LEAD_MS = a.lead_ms
    if a.script:  # the script's own lead-in (LEAD_MS) and length; the run is judged only by the generic checks (gameplay reached, no crash)
        a.scenario = 'idle'
        script = os.path.abspath(a.script)
        segs = json.load(open(script))['segments']
        if segs[0]['start_ms'] == 0 and segs[0]['end_ms'] < LEAD_MS:  # a scenario script without the lead-in: shift it and fill the pitches
            shift = a.script_shift or [0.0, 0.0, 0.0]
            fixed = [{"start_ms": 0, "end_ms": LEAD_MS, "head_pitch_degrees": 0, "input": NEUTRAL}]
            for sg in segs:
                sg = dict(sg, start_ms=sg['start_ms'] + LEAD_MS, end_ms=sg['end_ms'] + LEAD_MS)
                sg.setdefault('head_pitch_degrees', a.script_head_pitch)
                sg.setdefault('hand_pitch_degrees', a.script_hand_pitch)
                if 'right_hand_offset' in sg:
                    sg['right_hand_offset'] = [round(v + d, 4) for v, d in zip(sg['right_hand_offset'], shift)]
                fixed.append(sg)
            script = os.path.join(ART, 'claude', 'selftest-script.json')
            json.dump({"segments": fixed}, open(script, 'w'))
            segs = fixed
        if any('command' in sg for sg in segs):  # the mod's script parser takes no "command": the selftest sends those itself
            script = os.path.join(ART, 'claude', 'selftest-script.json')
            json.dump({"segments": [{k: v for k, v in sg.items() if k != 'command'} for sg in segs]}, open(script, 'w'))
        seconds = max(seg['end_ms'] for seg in segs) / 1000
    else:
        script, seconds = write_scenario(a.scenario)
        segs = write_scenario.segments
    knob = os.path.join(ROOT, 'build', 'ui-dump')
    open(knob, 'w').close()
    # Combat scenarios: the player ignores damage (engine god/buddha are not honoured) so a fight can run to the end.
    invulnerable = os.path.join(ROOT, 'build', 'sim-invulnerable')
    if a.script or a.scenario in ('duel', 'blockduel', 'swarm', 'swordthrow', 'stealththrow', 'shove', 'yank', 'soak') or a.near in ('item', 'corpse', 'oldcorpse', 'door'):  # guards near teleport spots killed the player (sim v404)
        open(invulnerable, 'w').close()
    dump_dir = os.path.join(ART, 'ui-dump')
    for f in glob.glob(os.path.join(dump_dir, '*')):
        os.remove(f)
    # A previous mod instance may have left commands in the shared file (the mod reads and deletes it, but a
    # crash/poll race can leave a line behind). Never replay them into this run: the mod has no command ids.
    try:
        os.remove(COMMAND_FILE)
    except FileNotFoundError:
        pass
    for stale in ('npc-watch.csv', 'native-combat.csv'):
        for _ in range(30):  # the previous run's npc_watch may still be finishing its last seconds
            try:
                if os.path.exists(os.path.join(ART, stale)):
                    os.remove(os.path.join(ART, stale))
                break
            except PermissionError:
                time.sleep(1)
    try:
        proc = subprocess.Popen([sys.executable, os.path.join(ROOT, 'tools', 'inject_probe.py'), '--pid', str(pid), '--dll-name', a.dll,
                                 *SIM_FLAGS, *(['--native-menu-pointer'] if a.scenario == 'shoparm' else []), *(['--headset-size'] if a.headset_size else []), '--simulated-gameplay-script', script, '--seconds', str(int(seconds + 12))],
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        proc_start = time.time()
        time.sleep(4)
        # The config file is only a next-launch hint and the engine can drop the non-archived lines. Set the live
        # cvars through the command channel before the title/load-screen input, and check the mod's answer.
        background_ok, background_text = apply_background_cvars()
        print(f"background cvars: {'confirmed' if background_ok else 'NOT confirmed'}")
        if not background_ok:
            print('warning: no live confirmation for in_controlInactiveWindow=1 / win_pauseOnFocusLoss=0;')
            print('warning: the load screen may stay paused if the game is unfocused. Last answers:')
            for line in background_text.splitlines()[-6:]:
                print('  ' + line)
        if a.inject_at_title:
            time.sleep(8)
            keys(0x0D, 0x0D, 0x0D)  # title -> Campaign -> Continue, with the mod already in
            time.sleep(10)
        reached = drive_to_gameplay(pid, timeout=LEAD_MS / 1000 - 3 - (time.time() - proc_start))
        home = None  # teleports are undone before the session ends: a player left in water drowns afterwards
        if reached and (a.teleport or a.near):
            try:
                b0, r0 = attach(pid)
                home = player_position(r0, b0)
            except Exception:
                home = None
        if reached and a.pre_teleport:
            set_god(pid, True)
            command('console setviewpos %.2f %.2f %.2f' % tuple(a.pre_teleport))
            time.sleep(4)  # the area's NPCs stream in and wake
        scenario_end = time.time() + seconds - 1
        bottle_rot = []
        bottle_addr, bottle_track, bottle_geom = None, [], {}  # the near-bottle item's entity, its world positions (script ms, x, y, z), stand geometry
        sampler_stop = None
        if reached and a.near == 'bottle':  # a native carryable (HoldObject), on the floor or a low table
            import math, re
            listing = json.loads(subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'entities.py'), '--pid', str(pid), '--kind', 'all', '--json', '--limit', '400'],
                                                capture_output=True, text=True).stdout)
            player = listing['player']['pos']
            items = [e for e in listing['entities'] if e.get('cls') == 'arkMovable' and e.get('pickable') and re.search(a.bottle_name or 'bottle|glass|cup|mug', e['name'], re.I)
                     and ((-6 < e['pos'][2] - player[2] < 12 and e['dist'] < 16) if a.bottle_name else (-0.2 < e['pos'][2] - player[2] < 1.0 and e['dist'] < 8))]
            items.sort(key=lambda e: e['dist'])
            if a.bottle_name:
                print('bottle candidates (name, pos, dist):', [(e['name'][-40:], [round(v, 2) for v in e['pos']], round(e['dist'], 1)) for e in items[:10]])
                items = items[NPC_INDEX:NPC_INDEX + 1]
            if items:
                item = items[0]['pos']
                dx, dy = item[0] - player[0], item[1] - player[1]
                d = max(1e-6, math.hypot(dx, dy))
                stand = (item[0] - dx / d * .6, item[1] - dy / d * .6, (item[2] + .8) if a.bottle_name else (player[2] + 1.3))
                yaw = math.degrees(math.atan2(dy, dx))
                print('near bottle', items[0]['name'], item, 'stand', [round(v, 2) for v in stand])
                set_god(pid, True)
                command('console setviewpos %.2f %.2f %.2f %.1f' % (*stand, yaw))
                time.sleep(2)
                bottle_addr = int(items[0]['addr'], 16)
                bottle_geom.update(yaw_deg=yaw, stand=list(stand), bottle0=list(items[0]['pos']))
                import threading
                sampler_stop = threading.Event()
                threading.Thread(target=bottle_sampler, args=(pid, bottle_addr, bottle_track, sampler_stop, bottle_rot), daemon=True).start()
                try:
                    pb, pr = attach(pid)
                    bottle_geom['eye'] = [float(v) for v in player_position(pr, pb)]
                    print('eye after the teleport (camera origin):', [round(v, 3) for v in player_position(pr, pb)], 'bottle', items[0]['pos'])
                except Exception as ex:
                    print('eye read failed', ex)
        if reached and a.near == 'item':
            import math
            listing = json.loads(subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'entities.py'), '--pid', str(pid), '--kind', 'all', '--json', '--limit', '600'],
                                                capture_output=True, text=True).stdout)
            player = listing['player']['pos']
            # Same floor as the player (feet z within 0.5 m): a teleport beside a raised item can drop the player off a ledge.
            # On the player's floor or a table on it (up to 1.2 m above the feet): the eye goes 1.3 m above the item.
            # Not the Black Market's display stock (its prefab's pickups are wares behind the counter: nothing to take, sim v465).
            items = [e for e in listing['entities'] if e.get('cls') == 'arkPickup' and e.get('pickable') and -.5 < e['pos'][2] - player[2] < 1.2 and e['dist'] < 15
                     and 'black_market' not in (e.get('model') or '') and not (e.get('name') or '').startswith('pickups_')]
            usables = [e['pos'] for e in listing['entities'] if e.get('cls') == 'arkUsable']
            # World loot, not an NPC's pocket (the shopkeeper's key moves), and alone: coins on a cash machine are ranked behind the
            # machine, which the hand then uses (sim v467).
            if a.item:
                items = [e for e in items if a.item in (e.get('name') or '')]
            placed = [e for e in items if (e.get('name') or '').startswith('env_') and (a.item or all(math.dist(e['pos'], u) > .6 for u in usables))]
            npcs = [e['pos'] for e in listing['entities'] if e.get('cls') == 'arkNPC']
            items = placed or [e for e in items if all(math.dist(e['pos'], u) > .6 for u in usables) and all(math.dist(e['pos'][:2], n[:2]) > 1.0 for n in npcs)]  # on the ground plane: a pocket hangs 1 m above the NPC's origin or items
            if items:
                item = items[0]['pos']
                dx, dy = item[0] - player[0], item[1] - player[1]
                d = max(1e-6, math.hypot(dx, dy))
                stand = (item[0] - dx / d * .7, item[1] - dy / d * .7, item[2] + 1.3)
                yaw = math.degrees(math.atan2(dy, dx))
                print('near item', items[0]['name'], item, 'stand', [round(v, 2) for v in stand])
                set_god(pid, True)
                command('console setviewpos %.2f %.2f %.2f %.1f' % (*stand, yaw))
                time.sleep(2)
        watcher = None
        npc = None
        next_melee = 0
        corpse = None
        door = None
        door_samples = []  # doorpush: (script segment, door state, next, leaf angle deg, status doors)
        door_errors = {}  # doorpush: the first failure of each sampling read
        eye_pairs = []  # carry: capture-eye-pair request ids
        pair_times = sorted(PAIR_TIMES.get(a.scenario, []) + [(int(float(t) * 1000), 'pair-%ss' % t) for t in (a.pairs_at or '').split(',') if t])
        pair_done = set()
        cmd_sent = set()  # --script: segments whose "command" was sent
        pair_actual = {}  # label -> scenario ms at the moment the request was posted
        eye_pair_segments, seg_first = set(), {}
        if reached and a.near == 'door':
            stand, door = door_stand(pid)
            if stand:
                print('near door', hex(door['addr']), 'hinge', [round(v, 2) for v in door['hinge']], 'side', door['s'], 'stand', [round(v, 2) for v in stand])
                set_god(pid, True)
                command('console setviewpos %.2f %.2f %.2f %.1f' % stand)
                time.sleep(2)
            else:
                print('no closed free door on this floor within 30 m')
        if reached and a.near in ('corpse', 'oldcorpse'):
            stand, corpse = corpse_stand(pid, a.scenario in TORSO_SCENARIOS) if a.near == 'corpse' else old_corpse_stand(pid)
            if stand:
                print('near corpse', hex(corpse[0]), 'pelvis', [round(v, 2) for v in corpse[1]], 'stand', [round(v, 2) for v in stand])
                set_god(pid, True)
                command('console setviewpos %.2f %.2f %.2f %.1f' % stand)
                time.sleep(2)
            else:
                print('no corpse with a dynamic ragdoll found')
        if reached and a.near == 'limb':  # the nearest severed limb / body half: its drawn mesh is the model box (+0x1840), not the entity origin
            import math
            import struct
            from entities import World, GAME_PTR, ENTITIES, ENTITIES_END
            from scan_body_material import attach as _attach, try_read
            lb, lr = _attach(pid);w = World(lr, lb);game = struct.unpack('<Q', lr(lb + GAME_PTR, 8))[0]
            eye0 = [float(v) for v in player_position(lr, lb)]

            def find_limbs():
                found = []
                for off in range(0, ENTITIES_END - ENTITIES, 8):
                    raw = try_read(lr, game + ENTITIES + off, 8);ent = struct.unpack('<Q', raw)[0] if raw else 0
                    if ent and w.cls(ent) == 'arkSeveredLimb':
                        model = struct.unpack('<Q', lr(ent + 0xf0, 8))[0]
                        box = struct.unpack('<8f', lr(model + 0x1840, 32)) if model else None
                        mvt = struct.unpack('<Q', lr(model, 8))[0] - lb if model else 0
                        if box:
                            c = [(box[i] + box[4 + i]) / 2 for i in range(3)] + [box[2]]
                            found.append((math.dist(c[:2], eye0[:2]), c, hex(ent), [round(box[4 + i] - box[i], 2) for i in range(3)], hex(mvt)))
                return sorted(found)
            limbs = find_limbs()
            if not limbs:  # make some: a grenade's damage on a living guard severs limbs (a loaded corpse can no longer be cut)
                out = json.loads(subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'entities.py'), '--pid', str(pid), '--kind', 'npc', '--health', '--json'],
                                                capture_output=True, text=True).stdout)
                for g in sorted([e for e in out['entities'] if e.get('guard') and not (e.get('health') or {}).get('dead')], key=lambda e: e['dist'])[:3]:
                    print('grenade damage on', g['name'], g['pos'])
                    command('console damage %s damage/explosion_grenade' % g['name'])
                    time.sleep(4)
                    limbs = find_limbs()
                    if limbs:
                        break
            print('limbs (dist, centre, entity, box size, model vtable):', [(round(d, 1), [round(v, 2) for v in c], e, sz, vt) for d, c, e, sz, vt in limbs[:6]])
            if limbs:
                c = limbs[0][1];dx, dy = c[0] - eye0[0], c[1] - eye0[1];d = max(1e-6, math.hypot(dx, dy))
                stand = (c[0] - dx / d * .75, c[1] - dy / d * .75, c[3] + 1.7, math.degrees(math.atan2(dy, dx)))  # the box bottom ~ the floor under the part
                print('near limb', limbs[0][2], 'stand', [round(v, 2) for v in stand])
                set_god(pid, True)
                command('console setviewpos %.2f %.2f %.2f %.1f' % stand)
                time.sleep(2)
            else:
                print('no severed limb found')
        if reached and a.near == 'shop':  # face the nearest living NPC (the Black Market keeper behind his window) from where the save stands
            import math
            out = json.loads(subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'entities.py'), '--pid', str(pid), '--kind', 'npc', '--health', '--json'],
                                            capture_output=True, text=True).stdout)
            eye = (37.88, 96.52, 14.52)  # the test save's spot in front of the Black Market (the live camera read drifted, sim v453)
            alive = [e for e in out['entities'] if not (e.get('health') or {}).get('dead')]
            if eye and alive:
                keeper = min(alive, key=lambda e: math.hypot(e['pos'][0] - eye[0], e['pos'][1] - eye[1]))
                yaw = math.degrees(math.atan2(keeper['pos'][1] - eye[1], keeper['pos'][0] - eye[0]))
                print('near shop', keeper.get('name'), [round(v, 2) for v in keeper['pos']], 'yaw %.1f' % yaw)
                command('console setviewpos %.2f %.2f %.2f %.1f' % (eye[0], eye[1], eye[2], yaw))
                time.sleep(1.5)
        if reached and a.near in ('npc', 'npc-front', 'npc-side', 'civ-front'):
            # Arrive just before the scenario (script t ~ LEAD): a guard given 15 s kills the player even in god mode.
            time.sleep(max(0, proc_start + LEAD_MS / 1000 - 4 - time.time()))
            if a.blind_ai:
                command('cvar ark_vision_max_checks 0')
            skip = set()
            for attempt in range(3):  # a spot on a ledge or table left the blade swinging over the guard (sim v382: 1 m above)
                stand, npc = npc_stand(pid, a.near == 'npc', a.near == 'civ-front', a.near == 'npc-side', skip, {'swordthrow': 3.0, 'gunthrow': 3.0, 'stealththrow': 10.0, 'chokeleft': 1.8}.get(a.scenario, .9))
                if not stand:
                    break
                print('near npc', npc['name'], npc['pos'], 'stand', [round(v, 2) for v in stand])
                if attempt == 0:
                    set_god(pid, True)
                    set_god(pid, True, buddha=True)
                if a.scenario == 'stealththrow':  # only the choice: the wind-up re-aim brings the player (below)
                    break
                command('console setviewpos %.2f %.2f %.2f %.1f' % stand)
                time.sleep(1.2)
                try:
                    pb, pr = attach(pid)
                    dz = player_position(pr, pb)[2] - (npc['pos'][2] + 1.65)  # eye vs the guard's eye height
                except Exception:
                    break
                if abs(dz) < .5:
                    break
                print('near npc: landed %.2f m off the guard floor, trying the next one' % dz)
                skip.add(npc['addr'])
            if stand:
                watcher = subprocess.Popen([sys.executable, os.path.join(ROOT, 'tools', 'npc_watch.py'), '--pid', str(pid), '--near', *map(str, npc['pos']),
                                            '--radius', '4', '--hz', '120', '--seconds', str(int(seconds - LEAD_MS / 1000 + 4)), '--out', os.path.join(ART, 'npc-watch.csv')],
                                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            else:
                print('near npc: no live guard')
        next_hit = time.time() + 3 if reached and (a.damage or a.every) else None
        if reached and a.script:
            set_god(pid, True)
        if reached and a.teleport:
            if a.scenario == 'swarm':  # bloodflies killed the god-mode player within 20 s: arrive 2 s before the first sweep
                time.sleep(max(0, proc_start + LEAD_MS / 1000 + 2 - 2 - time.time()))
            set_god(pid, True)  # engine god (the bare cvar write is not honoured): water, fish and guards must not end the test
            command('console setviewpos %.2f %.2f %.2f' % tuple(a.teleport))
            time.sleep(2)
        samples = []  # (logic, |published stick|) while the scenario runs
        dr = dict(god_off=None, dead=None, pads=0, last_pad=0.0, last_hit=0.0, hits=0, left_gameover=None, loading=None, reloaded=None, mask=None, timeline=[])
        fing_trace = []  # (scenario ms, finger_ik.route, bend, full_body.finger_modes, finger_inputs) whenever one changes (hold/orbit scenarios)
        fik_trace = []  # (scenario ms, (hits, props, slabs)) whenever the finger contact counters change
        interact_seen = []  # status "interact" [bits, presses] changes (shoptrigger)
        shop_uv = []  # native menu pointer u, v while the shop is open (shoparm)
        sword_trace = []  # status sword_grip + hand_weapon_bits changes (swordgrip)
        shoves = []  # shove scenario: (decl, result, npc state, ragdoll body flags)
        arm_samples = []  # status arm_hidden bits while the scenario runs
        positions = []
        seen_states = set()
        wheel_trace = []
        base, read = attach(pid)
        try:
            import player_states
            machine = player_states.machine_of(read, base)
        except Exception:
            machine = None
        while proc.poll() is None:
            if a.scenario in ('swordthrow', 'stealththrow', 'gunthrow', 'yank') and npc and not getattr(main, 'aimed', False) and script_segment() >= 7:  # the throw's wind-up begins: a console teleport lands 0.3-0.5 s later (at the hold it landed after the throw, sim v470)
                main.aimed = True  # the guard walked off since the arrival (sim v418, v431): face him again just before the throw / the grab
                reach = {'swordthrow': 2.0, 'gunthrow': 2.0, 'stealththrow': 2.5}.get(a.scenario, .9)  # 3 m: an alert guard ran out of the throw (sim v469)
                try:
                    import math
                    import struct
                    from entities import World
                    pb, pr = attach(pid)
                    fr = next(c for c, nm in World(pr, pb).components(int(npc['addr'], 16)) if nm.startswith('arkCpntReferenceFrame'))
                    gx, gy, gz = struct.unpack('<3f', pr(fr + 0x30, 12))
                    me = player_position(pr, pb)
                    dx, dy = me[0] - gx, me[1] - gy
                    if a.scenario == 'stealththrow':  # behind him: -row0 of his reference frame (+row0 is his forward, sim v644)
                        dx, dy = (-v for v in struct.unpack('<2f', pr(fr + 0x3c, 8)))
                    d = math.hypot(dx, dy) or 1
                    sx, sy = gx + dx / d * reach, gy + dy / d * reach
                    command('console setviewpos %.2f %.2f %.2f %.1f' % (sx, sy, gz + 1.65, math.degrees(math.atan2(gy - sy, gx - sx))))
                    print('re-aimed at the guard', [round(gx, 2), round(gy, 2)], 'from', [round(sx, 2), round(sy, 2)])
                except Exception as ex:
                    print('re-aim failed', ex)
            if a.scenario == 'yank' and npc and getattr(main, 'aimed', False) and script_segment() >= 11 and not shoves:  # the pull began
                import struct
                from entities import World
                t0 = None  # the pull: when the status first counts a yank (the script clock runs behind proc_start)
                for _ in range(30):
                    try:
                        if ((json.load(open(os.path.join(ART, 'xr-live-status.json'))).get('sword_grip') or {}).get('ragdolls') or [0] * 4)[3] >= 1:
                            t0 = time.time()
                            break
                    except Exception:
                        pass
                    time.sleep(.1)
                t0 = t0 or time.time()
                for wait in (0, .5, 1, 1.5, 2.5, 4, 6):
                    time.sleep(max(0, t0 + wait - time.time()))
                    try:
                        pb, pr = attach(pid)
                        fr = next(c for c, nm in World(pr, pb).components(int(npc['addr'], 16)) if nm.startswith('arkCpntReferenceFrame'))
                        pos = [round(v, 2) for v in struct.unpack('<3f', pr(fr + 0x30, 12))]
                    except Exception:
                        pos = None
                    bodies = ragdoll(pid, int(npc['addr'], 16))
                    shoves.append((wait, pos, None if not bodies else sorted({hex(f) for _, f in bodies})[:4], None if not bodies else [round(v, 2) for v in bodies[0][0]]))
                    print('yank', shoves[-1])
            if a.scenario == 'shove' and npc and time.time() > proc_start + LEAD_MS / 1000 + 1 and not shoves:
                command('ragdoll %s 3' % npc['addr'])  # the game's START_RAGDOLL + a 3 m/s push; watch the bodies and the recovery
                t0 = time.time()
                for wait in (.3, 1, 2, 4, 6, 9):
                    time.sleep(max(0, t0 + wait - time.time()))
                    bodies = ragdoll(pid, int(npc['addr'], 16))
                    state = None
                    try:
                        pb, pr = attach(pid)
                        state = pr(int(npc['addr'], 16) + 0xd1, 1)[0]
                    except Exception:
                        pass
                    st_ = json.load(open(os.path.join(ART, 'xr-live-status.json'))).get('sword_grip', {}) if os.path.exists(os.path.join(ART, 'xr-live-status.json')) else {}
                    shoves.append((wait, st_.get('ragdolls'), hex(state) if state is not None else None,
                                   None if not bodies else sorted({hex(f) for _, f in bodies}), None if not bodies else [round(v, 2) for v in bodies[0][0]]))
                    print('ragdoll', shoves[-1])
            ms_now = script_ms() if pair_times else None
            if ms_now is not None:
                for t_ms, label in pair_times:
                    if (t_ms, label) not in pair_done and ms_now >= LEAD_MS + t_ms:
                        pair_done.add((t_ms, label))
                        from pathlib import Path
                        sys.path.insert(0, os.path.join(ROOT, 'tools'))
                        from request_vr_capture import request_capture
                        safe = label.replace('.', 'p')
                        try:
                            eye_pairs.append((t_ms, safe, request_capture(Path(ART), safe)))
                            pair_actual[safe] = round(ms_now - LEAD_MS)
                        except OSError as e:
                            print('eye pair request failed', e)
            if a.scenario.startswith('deathreload') and reached:
                now_t = time.time()
                sm_dr = script_ms()
                mask_dr = dr['mask']
                tl_dr = round((sm_dr - LEAD_MS) / 1000, 1) if sm_dr else None
                if sm_dr is not None and (sm_dr - LEAD_MS) / 1000 >= DEATH_AT and not dr['god_off']:
                    dr['god_off'] = set_god(pid, False) or now_t
                    dr['timeline'].append(('god off', tl_dr))
                if dr['god_off'] and not dr['dead'] and now_t - dr['last_hit'] >= 1.0:
                    command('console damage player1 bullet_regular_npc')
                    dr['last_hit'] = now_t
                    dr['hits'] += 1
                if mask_dr == 8192 and not dr['dead']:
                    dr['dead'] = now_t
                    dr['timeline'].append(('game over screen after %d hits' % dr['hits'], tl_dr))
                if dr['dead'] and mask_dr == 8192 and now_t - dr['last_pad'] >= 2.5 and now_t - dr['dead'] >= 1.5:
                    command('pad a 200')  # first item: Restart From Last Checkpoint
                    dr['last_pad'] = now_t
                    dr['pads'] += 1
                    dr['timeline'].append(('pad a (restart)', dr['pads']))
                if dr['dead'] and mask_dr == 64:
                    if now_t - dr['last_pad'] >= 2.0:
                        command('pad a 200')
                        dr['last_pad'] = now_t
                    if not dr['loading']:
                        dr['loading'] = now_t
                        dr['timeline'].append(('main menu after game over', tl_dr))
                if dr['dead'] and mask_dr not in (8192, None) and not dr['left_gameover']:
                    dr['left_gameover'] = now_t
                    dr['timeline'].append(('left game over, mask %s' % mask_dr, tl_dr))
                if dr['left_gameover'] and mask_dr == 2 and not dr['reloaded']:
                    dr['reloaded'] = now_t
                    dr['timeline'].append(('gameplay again', tl_dr))
                if int(now_t * 2) % 4 == 0:
                    dr['alive_pid'] = game_pid()
                    if dr['alive_pid'] is None and not dr.get('crashed'):
                        dr['crashed'] = now_t
                        dr['timeline'].append(('GAME PROCESS GONE', tl_dr))
            if segs:  # a segment's "command" (mod test commands split by ';', e.g. "equip pistol") is sent once when the script reaches it
                seg_c = script_segment()
                if 0 <= seg_c < len(segs) and segs[seg_c].get('command') and seg_c not in cmd_sent:
                    cmd_sent.add(seg_c)
                    for c in segs[seg_c]['command'].split(';'):
                        command(c.strip())
            plan = PAIR_PLAN.get(a.scenario)
            seg_now = script_segment() if plan else -1
            if seg_now in (plan or {}) and seg_now not in seg_first:
                seg_first[seg_now] = time.time()
            if seg_now in (plan or {}) and seg_now not in eye_pair_segments and time.time() - seg_first[seg_now] > (plan[seg_now][1] if isinstance(plan[seg_now], tuple) else PAIR_DELAY.get(a.scenario, .6)):  # an eye pair per pose
                eye_pair_segments.add(seg_now)
                from pathlib import Path
                sys.path.insert(0, os.path.join(ROOT, 'tools'))
                from request_vr_capture import request_capture
                try:
                    label = plan[seg_now][0] if isinstance(plan[seg_now], tuple) else plan[seg_now]
                    eye_pairs.append((seg_now, label.replace('.', 'p'), request_capture(Path(ART), label.replace('.', 'p'))))
                except OSError as e:
                    print('eye pair request failed', e)
            if a.scenario == 'doorpush' and door:
                try:
                    pb, pr = attach(pid)
                    fr = door_frame(pr, pb, door['addr'])
                    try:
                        angle = round(leaf_angle(pr, door['addr'], fr, door['s']), 1)
                    except Exception as e:  # keep the state samples; v460 lost every sample to one failing read
                        angle = None
                        door_errors.setdefault('angle', repr(e))
                    try:
                        doors_now = json.load(open(os.path.join(ART, 'xr-live-status.json'))).get('sword_grip', {}).get('doors')
                    except (OSError, ValueError):  # the status file is rewritten while we read it
                        doors_now = None
                    door_samples.append((script_segment(), fr['state'], fr['next'], angle, doors_now))
                except Exception as e:
                    door_errors.setdefault('frame', repr(e))
            if a.scenario == 'contact' and npc and time.time() > next_melee and time.time() < scenario_end - 2:
                command('melee ' + npc['addr'])
                next_melee = time.time() + 2.5
            if next_hit and time.time() > next_hit and time.time() < scenario_end - 2:
                if a.damage:
                    set_god(pid, False)
                    command('console damage player1 ' + a.damage)
                if a.every:
                    command(a.every)
                next_hit = time.time() + 3
            if a.blind_ai and time.time() > scenario_end and not getattr(main, 'restored', False):
                command('cvar ark_vision_max_checks 150')
                main.restored = True
            if home and time.time() > scenario_end:
                command('console setviewpos %.2f %.2f %.2f' % home)  # eye position = camera origin
                home = None
            if machine:
                try:
                    now_states = {st['name'].replace('arkPlayerState', '') for st in player_states.states(read, base, machine) if st['active'] == 1}
                    if now_states != getattr(main, 'last_states', None):  # state timeline, seconds before the scenario's end
                        print(f'states {time.time() - scenario_end:+.2f}s {sorted(now_states)}')
                        main.last_states = now_states
                    seen_states |= now_states
                except Exception:
                    pass
            if time.time() <= scenario_end:  # not the return-home teleport
                try:
                    positions.append(player_position(read, base))
                except Exception:
                    pass
            try:
                st = json.load(open(os.path.join(ART, 'xr-live-status.json')))
                mv = st.get('published_move') or [0, 0]
                samples.append((st.get('logic_mask'), (mv[0] ** 2 + mv[1] ** 2) ** .5))
                dr['mask'] = st.get('logic_mask')
                dr['status'] = {k: st.get(k) for k in ('hand_weapon_bits', 'left_equip', 'held_object', 'hand_contact')}
                if a.scenario.startswith(('hold', 'orbit')) and len(fing_trace) < 300:
                    fk2, fb2 = st.get('finger_ik') or {}, st.get('full_body') or {}
                    cur = (fk2.get('route'), fk2.get('bend'), fb2.get('finger_modes'), fb2.get('finger_inputs'))
                    if cur != (None, None, None, None) and (not fing_trace or fing_trace[-1][1:] != cur):
                        sm2 = script_ms()
                        fing_trace.append((round(sm2 - LEAD_MS) if sm2 else None, *cur))
                fk = st.get('finger_ik')
                if fk and len(fik_trace) < 400:
                    key = (fk.get('hits'), fk.get('props'), fk.get('slabs'), tuple(fk.get('touch') or ()))
                    if not fik_trace or fik_trace[-1][1] != key:
                        sm = script_ms()
                        fik_trace.append((round(sm - LEAD_MS) if sm else None, key))
                if st.get('logic_mask') == 1024 and st.get('native_menu_pointer_active'):
                    shop_uv.append(st.get('native_menu_pointer_uv'))
                if a.scenario in ('shoptrigger', 'shopgrip') and (not interact_seen or interact_seen[-1][1] != st.get('interact')):
                    interact_seen.append((round(scenario_end - time.time(), 1), st.get('interact')))
                if a.scenario in ('swordgrip', 'pistolhip', 'swordthrow', 'stealththrow'):
                    sg = (st.get('sword_grip'), st.get('hand_weapon_bits'), st.get('left_equip'))
                    if not sword_trace or sword_trace[-1][1] != sg:
                        sword_trace.append((round(scenario_end - time.time(), 1), sg))
                if time.time() <= scenario_end:
                    arm_samples.append(st.get('arm_hidden', 0))
                if a.scenario in ('potion', 'hide') and st.get('wheel_zone'):
                    wz = json.dumps(st['wheel_zone'])
                    if not wheel_trace or wheel_trace[-1] != wz:
                        wheel_trace.append(wz)
            except Exception:
                pass
            time.sleep(.2 if a.scenario == 'doorpush' or a.scenario in PAIR_PLAN or pair_times else .5)
        out, _ = proc.communicate()
        run = subprocess.CompletedProcess(proc.args, proc.returncode, out or '', '')
    finally:
        os.remove(knob)
        if os.path.exists(invulnerable):
            os.remove(invulnerable)
    runs = sorted(glob.glob(os.path.join(ART, 'runs', '*')), key=os.path.getmtime)
    run_dir = runs[-1] if runs else ART
    if not os.path.exists(os.path.join(ART, 'xr-live-status.json')):
        sys.exit('no VR session started; injector output: ' + (run.stdout or '')[-3000:])
    status = json.load(open(os.path.join(ART, 'xr-live-status.json')))
    frames = to_png(dump_dir, os.path.join(run_dir, 'ui-dump-png'))
    hud = [f for f in frames if f[0].startswith('hud')]
    wheel = [f for f in frames if f[0].startswith('wheel')]
    checks = []

    def check(name, ok, detail):
        checks.append((name, bool(ok), detail))

    check('reached gameplay', reached, 'logic mask 2 before the scenario began')
    if a.scenario.startswith('deathreload'):
        print('deathreload timeline:', dr['timeline'])
        print('deathreload last status before the end:', json.dumps(dr.get('status'))[:600])
        end_alive = game_pid()
        since = (time.time() - dr['reloaded']) if dr['reloaded'] else 0
        check('player died (game over screen)', dr['dead'] is not None, f"{dr['hits']} damage commands")
        check('Restart From Last Checkpoint chosen and the level reloaded', dr['reloaded'] is not None, f"pads {dr['pads']}, left game over {dr['left_gameover'] is not None}")
        check('game process alive at the end', end_alive is not None and not dr.get('crashed'), f'pid {end_alive}')
        check('survived 40 s after the reload', dr['reloaded'] is not None and end_alive is not None and since >= 40, f'{since:.0f} s after the reload')
    check('injection', run.returncode == 0, (run.stdout + run.stderr).strip().splitlines()[-1:] or '')
    check('hud screen shown', status.get('hud_window_layers', 0) > 0 and status.get('hud_panel_children', 0) > 0,
          f"window_layers={status.get('hud_window_layers')} panel_children={status.get('hud_panel_children')}")
    check('hud captures saved', len(hud) > 0, f'{len(hud)} images')
    if hud:
        worst = max(edge_clipped(f[2]) for f in hud)
        check('nothing clipped at hud edges', worst < 0.02, f'max edge coverage {worst:.3f}')
        print('info: hud screen max opaque fraction %.4f (empty is valid: only prompts/notifications/tutorials go there)' % max(opaque_fraction(f[2]) for f in hud))
    if a.scenario in ('pickup', 'reach'):
        check('prompt shown beside a hand', status.get('world_prompt_frames', 0) > 0, f"world_prompt_frames={status.get('world_prompt_frames')}")
    if a.scenario == 'swarm':
        hits = status.get('contact_hits') or [0, 0, 0]
        check('blade kills bloodflies', hits[0] > 0, f'contact hits applied/rejected/faults {hits}, rejects {status.get("contact_rejects")}, '
              f'native re-kills skipped {(status.get("held_object") or {}).get("swarm_kill_skipped")}')
        check('swings reach the game', any(w == 'slash' for _, w in combat_events()), 'native slashes kill the flies along the sword aim')
    if a.scenario == 'waist':
        hidden = [h for h in arm_samples if h & 1]
        check('left arm never hidden at the waist', arm_samples and not hidden,
              f'{len(hidden)}/{len(arm_samples)} samples hidden; shows {status.get("arm_shows")}, engine hides skipped {status.get("arm_hides_skipped")}')
    if a.scenario in ('walk', 'all'):
        moving = [m for logic, m in samples if logic == 2 and m > .5]
        check('stick reaches the game', len(moving) > 2, f'{len(moving)} gameplay samples with |stick|>0.5')
    if a.scenario in EXPECTED_STATES:
        wanted = EXPECTED_STATES[a.scenario]
        check('engine state reached', any(w in seen_states for w in wanted), f'wanted any of {wanted}; seen {sorted(seen_states)}')
    if a.scenario in FORBIDDEN_STATES:
        bad = [w for w in FORBIDDEN_STATES[a.scenario] if w in seen_states]
        check('no accidental attack', not bad, f'forbidden {FORBIDDEN_STATES[a.scenario]}; seen {sorted(seen_states)}')
    if a.scenario == 'swim':
        pts = [q for q in positions if q]
        moved = max((sum((x - y) ** 2 for x, y in zip(pts[len(pts) // 3], q)) ** .5 for q in pts[len(pts) // 3:]), default=0) if pts else 0
        check('moved in water', moved > 1.5, f'displacement {moved:.2f} m after the teleport; z range {min(q[2] for q in pts):.2f}..{max(q[2] for q in pts):.2f}' if pts else 'no positions')
    if a.scenario == 'power':
        pts = [q for q in positions if q]
        moved = max((sum((x - y) ** 2 for x, y in zip(pts[0], q)) ** .5 for q in pts), default=0) if pts else 0
        check('left-hand power moved the player', moved > 2, f'max displacement {moved:.2f} m (Blink/Far Reach)')
    if a.scenario in ('wheel', 'all', 'potion', 'hide'):
        check('wheel captures saved', len(wheel) > 0, f'{len(wheel)} images')
        if wheel:
            check('wheel never empty', min(opaque_fraction(f[2]) for f in wheel) > 0.01,
                  'min opaque fraction %.4f' % min(opaque_fraction(f[2]) for f in wheel))
    if a.scenario == 'takedown':
        shows, hides = status.get('prompt_shows_hides') or [0, 0]
        check('takedown sensor qualified', status.get('takedown_flips', 0) > 0, f"takedown_flips={status.get('takedown_flips')}")
        check('guard prompt shown', shows > 0 and status.get('world_prompt_frames', 0) > 0, f"shows={shows} world_prompt_frames={status.get('world_prompt_frames')}")
        check('guard prompt steady', hides <= 1 and status.get('takedown_flips', 0) <= 2, f"hides={hides} sensor flips={status.get('takedown_flips')}")
    if a.scenario in ('lookuse', 'shoparm'):
        check('shop opened by the grip (gaze use)', any(logic == 1024 for logic, _ in samples), f'logic masks seen {sorted({l for l, _ in samples})}; look_uses {(status.get("sword_grip") or {}).get("look_uses")}')
    if a.scenario == 'shoparm':
        check('shop page held in the left hand (pointer at its centre)', any(abs(u - .5) < .15 and abs(v - .5) < .2 for u, v in shop_uv), f'pointer u, v samples {shop_uv[-4:]}')
    if a.scenario in ('shoptrigger', 'shopgrip'):
        print('interact [bits, presses] by seconds before the scenario end:', interact_seen)
        check('shop opened by the right trigger', any(logic == 1024 for logic, _ in samples), f'logic masks seen {sorted({l for l, _ in samples})}; interact {status.get("interact")}')
    if a.scenario == 'swordgrip':
        for t, sg in sword_trace:
            print('sword', t, json.dumps(sg))
        sg = status.get('sword_grip') or {}
        acts = sg.get('actions') or [0] * 5  # draw, sheathe, drop, pickup, recall
        nat = sg.get('native') or [0] * 4  # ops, failures, spawned, removed
        check('sword drawn at the shoulder', acts[0] >= 2 and any(sg_[1] and sg_[1] & 2 for _, sg_ in sword_trace), f'actions {acts}')
        check('sword dropped as a world sword', acts[2] >= 2 and nat[2] >= 1 and any((sg_[0] or {}).get('copy') for _, sg_ in sword_trace), f'actions {acts}, native {nat}')  # one copy per level, reused (v436)
        check('dropped sword picked up / called back', acts[3] >= 2 and nat[3] >= 2, f'actions {acts}, native {nat}')
        check('sheathed at the shoulder', acts[1] >= 2 and not (status.get('hand_weapon_bits', 0) & 2) and sg.get('copy') is None, f'actions {acts}, bits {status.get("hand_weapon_bits")}, copy {sg.get("copy")}')
        check('native equip calls succeeded', nat[1] == 0 and nat[0] > 0, f'native {nat}')
    if a.scenario == 'gunthrow':
        guns = status.get('guns') or {}
        stuns = guns.get('stuns') or [0, 0, 0]  # thrown gun, gun bash, thrown gadget
        check('pistol thrown (a native drop)', guns.get('native_drops', 0) >= 1, f'guns drops {guns.get("drops")} native {guns.get("native_drops")}')
        check('thrown pistol struck the guard', stuns[0] >= 1, f'stuns {stuns}, lying {guns.get("lying")}')
    if a.scenario in ('leftbottle', 'bottlepistol'):
        gd = status.get('game_sword_draws') or {}
        print('game_sword_draws:', json.dumps(gd), 'held_object:', json.dumps(status.get('held_object')))
        if a.scenario == 'leftbottle':
            check('the bottle kept the held sword (v782)', gd.get('hold_kept', 0) >= 1, f'game_sword_draws {gd}')
    if a.scenario in ('swordthrow', 'stealththrow'):
        sg = status.get('sword_grip') or {}
        print('sword trace:', [(t, (g[0] or {}).get('thrown'), (g[0] or {}).get('actions')) for t, g in sword_trace][:12])
        traces = []
        for _, g in sword_trace:
            tr = ((g[0] or {}).get('copy') or {}).get('trace')
            if tr and tr not in traces:
                traces.append(tr)
        print('flight traces:', traces)
        print('copy:', json.dumps(sg.get('copy')))
        thrown = sg.get('thrown') or [0, 0, 0]  # hits, flight frames, launch m/s, hand m/s at release, hand peak, services
        check('sword thrown (flew fast)', thrown[2] >= 4, f'thrown {thrown}, actions {sg.get("actions")}')
        check('thrown sword struck the guard', thrown[0] >= 1, f'thrown {thrown}')
        props = sg.get('props_thrown') or [0, 0, 0]  # native throwables marked, prop hits, phys_damage decl resolved
        check('thrown-prop damage decl resolves', props[2] == 1, f'props_thrown {props}')
        if a.scenario == 'stealththrow':
            st = sg.get('stealth') or [0, 0]  # isUnaware asked, answered yes
            hp = target_hp(pid, npc) if npc else None
            check('the engine judged the guard unaware', st[1] >= 1, f'stealth {st}')
            check('one throw killed him', hp is not None and hp[1] is not None and hp[1] <= 0, f'target hp {hp}')
    if a.scenario in ('stabgun', 'stabguntrig'):  # v920: the held sword grip is no choke press (with a gun in the other hand the sword hand is the choke hand)
        noc, sc = status.get('native_offer_controls') or {}, status.get('sword_contact') or {}
        hp = target_hp(pid, npc) if npc else None
        print('stab: states', sorted(seen_states), 'choke frames offered/input', noc.get('offered_choke_frames'), noc.get('choke_input_frames'), 'stealth', sc.get('stealth'), 'takedown', json.dumps(status.get('takedown')), 'target hp', hp)
        check('the held sword grip never chokes', 'Choke' not in seen_states and not noc.get('choke_input_frames'), f"states {sorted(seen_states)}, choke input frames {noc.get('choke_input_frames')}")
        check('the stab from behind killed him', hp is not None and hp[1] is not None and hp[1] <= 0, f'target hp {hp}, stealth {sc.get("stealth")}')
    if a.scenario == 'yank':
        rd = (status.get('sword_grip') or {}).get('ragdolls') or [0, 0, 0, 0]  # START_RAGDOLL posts, last result, yank requests, yanks applied
        check('yank requested by the pulling hand', rd[2] >= 1, f'ragdolls {rd}')
        moved = [_m.dist(shoves[0][1][:2], p[1][:2]) for p in shoves[1:] if shoves[0][1] and p[1]] if shoves else []
        dynamic = any(p[2] and any(int(f, 16) & 7 == 2 for f in p[2]) for p in shoves)
        check('guard yanked into ragdoll', rd[3] >= 1 and (dynamic or (moved and max(moved) > .8)), f'ragdolls {rd}, moved {moved}, samples {shoves}')
        print('info: hand kept hold of him', len(rd) > 8 and rd[8] >= 1, '(only when the game exposes his ragdoll bodies)')
        floor = shoves[-1][1][2] if shoves and shoves[-1][1] else None  # standing: the reference frame is back on the floor
        down = [p[1][2] - floor for p in shoves if p[1] and floor is not None]
        check('he goes down and gets up again', bool(down) and max(down) > .2 and abs(down[-1]) < .05, f'reference frame above the floor {[round(d, 2) for d in down]}')
    if a.scenario == 'pistolhip':
        for t, sg in sword_trace:
            print('gun', t, json.dumps(sg))
        gun = (status.get('sword_grip') or {}).get('gun') or [0, 0, 0, 0]  # draws, holsters, held, hip loaded
        equips = [sg_[2] for _, sg_ in sword_trace]
        # the empty-hand trigger (8.8-9.3 s) must not bring the gun back: between the first holster and the hip draw it stays away
        check('wheel gun kept until gripped, then holstered by letting go', gun[1] >= 2 and 'Gun' in equips, f'gun {gun}, left equips {equips}')
        check('gun drawn from the left hip', gun[0] >= 1, f'gun {gun}')
        check('gripped gun fires', bool({'FireWeapon', 'ReloadWeapon'} & seen_states), f'states {sorted(seen_states)}')
        check('gun back at the hip at the end', status.get('left_equip') != 'Gun' and not (status.get('hand_weapon_bits', 0) & 1), f"left {status.get('left_equip')}, bits {status.get('hand_weapon_bits')}")
    if eye_pairs:
        print('eye pairs (script segment or scenario ms, label, id):', eye_pairs)
        json.dump(eye_pairs, open(os.path.join(ART, 'claude', 'pairs-%s.json' % (os.path.splitext(os.path.basename(a.script))[0] if a.script else a.scenario)), 'w'))
    if fing_trace:
        json.dump(fing_trace, open(os.path.join(ART, 'claude', 'finger-trace-%s.json' % a.scenario), 'w'))
        print('finger trace entries:', len(fing_trace), '(artifacts/claude/finger-trace-%s.json: scenario ms, finger_ik.route, bend, full_body.finger_modes, finger_inputs)' % a.scenario)
    if fik_trace:
        print('finger_ik timeline (scenario ms, (hits, props, slabs, touch [along, roll_deg, measured, defaulted])), first 40:', fik_trace[:40])
    for key in status:
        if 'fault' in key.lower():
            print('status %s: %s' % (key, json.dumps(status[key])[:300]))
    print('status full_body finger_modes/finger_inputs:', json.dumps((status.get('full_body') or {}).get('finger_modes')), json.dumps((status.get('full_body') or {}).get('finger_inputs')))
    for key in ('finger_ik', 'hand_contact'):
        if key in status:
            print('status %s: %s' % (key, json.dumps(status[key])))
    if sampler_stop:
        sampler_stop.set()
    if bottle_track:
        key = os.path.splitext(os.path.basename(a.script))[0] if a.script else a.scenario
        json.dump(dict(bottle_geom, pairs=pair_actual, track=bottle_track, rot=bottle_rot, script=(script if a.script else None)), open(os.path.join(ART, 'claude', 'bottle-track-%s.json' % key), 'w'))
        bottle_track = [t for t in bottle_track if 3000 <= t[0] <= 14000] or bottle_track
        x0, y0, z0 = bottle_track[0][1:]
        print('bottle track (scenario ms, dx, dy, dz from the first sample, m), one per ~0.5 s:')
        last = -1e9
        for t_ms, x, y, z in bottle_track:
            if t_ms - last >= 250:
                last = t_ms
                print('  %6d  %+.3f %+.3f %+.3f  (horizontal %.3f)' % (t_ms, x - x0, y - y0, z - z0, ((x - x0) ** 2 + (y - y0) ** 2) ** .5))
        print('bottle track max horizontal %.3f m, z range %+.3f..%+.3f' % (max(((x - x0) ** 2 + (y - y0) ** 2) ** .5 for _, x, y, _z in bottle_track),
                                                                          min(z - z0 for *_, z in bottle_track), max(z - z0 for *_, z in bottle_track)))
    try:
        rows = open(os.path.join(run_dir, 'native-hand-contact.csv')).read().strip().splitlines()
        print('native-hand-contact.csv: %d rows' % (len(rows) - 1))
        for row in rows[:1] + rows[1:7]:
            print('  ' + row[:240])
    except OSError:
        pass
    if a.near in ('corpse', 'oldcorpse'):
        moved = None
        if corpse:
            try:
                import math
                bodies = ragdoll(pid, corpse[0])
                if bodies and any(abs(v) > 1e-3 for v in bodies[0][0]):  # an un-instanced ragdoll reads zeros
                    moved = math.dist(bodies[0][0], corpse[1])
                else:  # the ragdoll went unreadable after settling: the corpse's reference frame (it follows the pelvis)
                    import struct
                    from entities import World
                    pb, pr = attach(pid)
                    fr = next(c for c, nm in World(pr, pb).components(corpse[0]) if nm.startswith('arkCpntReferenceFrame'))
                    moved = math.dist(struct.unpack('<3f', pr(fr + 0x30, 12))[:2], corpse[1][:2])
            except Exception:
                moved = None
        check('corpse found', corpse is not None, hex(corpse[0]) if corpse else 'none')
        check('body dragged', moved is not None and moved > .3, f'pelvis moved {moved:.2f} m' if moved is not None else 'no reading')
    if a.scenario == 'holdrun':  # v1072: the held thing's draws placed in the hand while running (fields live under arm_flicker)
        af = status.get('arm_flicker') or {}
        print('held_bind [new, learning, off_moving, off_still, relearn, placed] per hand:', af.get('held_bind'), 'held_draw_placed', af.get('held_draw_placed'),
              'held_skip [kind, read, image]', af.get('held_skip'), 'prop_seat_frames [seated L,R, in hand L,R]', af.get('prop_seat_frames'))
        print('image_skin kinds:', (af.get('image_skin') or {}).get('kinds'), 'place:', json.dumps((af.get('weapon_draw') or {}).get('place'))[:1500])
        print('hand_visual:', json.dumps(status.get('hand_visual')), 'physical picked:', (status.get('physical_grab') or {}).get('picked') if isinstance(status.get('physical_grab'), dict) else None)
        placed = af.get('held_draw_placed') or [0, 0]
        check('held thing placed in the hand', max(placed) > 100, f'held_draw_placed {placed}')
    if a.scenario in ('hold', 'holdup', 'showbottle', 'holdneck', 'holdneck2', 'holdneck3'):  # the fist takes the bottle (physical grab, or the game's own HoldObject), the swing throws it
        try:
            result = json.load(open(os.path.join(run_dir, 'dxgi-probe-result.json')))
        except (OSError, ValueError):
            result = {}
        held = status.get('held_object') or {}
        props = (status.get('sword_grip') or {}).get('props_thrown') or [0, 0, 0]
        check('bottle taken by the fist', (result.get('physical_grab_picked') or 0) >= 1 or held.get('placed', 0) > 0,
              f"physical picked {result.get('physical_grab_picked')}, native hold placed {held.get('placed')}")
        check('bottle thrown by the swing', props[0] >= 1 or held.get('throws', 0) >= 1,
              f"props thrown {props}, native throws {held.get('throws')} drops {held.get('drops')}")
    if a.scenario == 'doorpush':
        print('door samples (segment, state, next, leaf deg, doors):', door_samples)
        if door_errors:
            print('door sampling errors:', door_errors)
        doors = ((status.get('sword_grip') or {}).get('doors') or []) + [0] * 10
        held = [smp for smp in door_samples if smp[0] in DOOR_HELD_SEGMENTS]
        angles = [smp[3] for smp in held if smp[3] is not None]
        pushing = [smp[3] for smp in door_samples if 2 <= smp[0] <= 42 and smp[3] is not None]
        check('door found', door is not None, hex(door['addr']) if door else 'none')
        check('slow push holds the door', doors[3] >= 1, f'doors [pushes, refusals, last refusal, holds, releases, last release, hold deg, leaf deg, hit deg, sync fails] = {doors[:10]}')
        check('leaf follows the hand slowly', pushing and max(pushing) < 60 and max(pushing) > 5, f'leaf angle while pushing {pushing}')
        check('door stays ajar, never closes by itself', held and angles and all(smp[1] == 1 for smp in held) and 8 <= min(angles) and max(angles) <= 75 and max(angles) - min(angles) <= 6,
              f'held samples {held}')
        check('fast push opens it fully', door_samples and door_samples[-1][1] == 2 and (door_samples[-1][3] or 0) >= 80, f'last sample {door_samples[-1:] }')
    if a.near in ('npc', 'npc-front', 'npc-side', 'civ-front') and a.scenario not in ('yank', 'shove'):  # those do no damage by design
        combat_latency(check, target_hp(pid, npc) if npc else None)
    if a.scenario in ('potion', 'hide'):
        print('wheel zone trace:', wheel_trace[-8:])
    print(f'run: {run_dir}')
    for name, ok, detail in checks:
        print(('PASS ' if ok else 'FAIL ') + name + ' - ' + str(detail))
    sys.exit(0 if all(ok for _, ok, _ in checks) else 1)


if __name__ == '__main__':
    main()
