"""Gameplay-script scenario: pick up a whale-oil tank with the right hand, lift it, lower the hand to the floor, let go.

Used by `linux/d2vr-linux sim --scenario oil_tank_drop` (UNTESTED against a real tank: it needs a save that starts next to
one, see "Simulated test runs (developer)" in linux/README.md; without one it grabs whatever physical object is nearest the
right hand and drops it, which still exercises the carry/drop path).

build(lead_ms) returns the segment list for tools/simulated_gameplay_script.py (script clock = session start, so the first
segment is a neutral lead-in while the game is driven into gameplay). Hand offsets are metres relative to the simulated
default right hand (0.4 m ahead, 0.2 m right, 0.35 m below the eye); x right, y up, z backward. The head looks down at the
object. Pose numbers follow tools/vr_selftest.py's 'hold' scenario (a bottle on the floor in front of the player).
"""
NEUTRAL = {"move_x": 0.0}
GRIP = {"right_grip": 1.0}
CLOSE = {"right_grip": 1.0, "right_trigger": 1.0}  # a full fist for the grab; the grip alone then keeps the object
REACH = [-0.2, -0.8, -0.2]   # hand in front of the knees, within reach of an object on the floor
CHEST = [-0.1, -0.2, -0.2]   # lifted to the chest
FLOOR = [-0.2, -1.15, -0.3]  # hand about 0.1 m above the floor
HEAD_PITCH = -50


def _glide(t0, t1, a, b, steps, inp):
    out = []
    for i in range(steps):
        k = (i + 1) / steps
        out.append({"start_ms": round(t0 + (t1 - t0) * i / steps), "end_ms": round(t0 + (t1 - t0) * (i + 1) / steps),
                    "head_pitch_degrees": HEAD_PITCH, "input": dict(inp),
                    "right_hand_offset": [round(x + (y - x) * k, 4) for x, y in zip(a, b)]})
    return out


def _hold(t0, t1, inp, offset):
    return {"start_ms": t0, "end_ms": t1, "head_pitch_degrees": HEAD_PITCH, "input": dict(inp), "right_hand_offset": list(offset)}


PRESS_FIRST_MS, PRESS_EVERY_MS, PRESS_MS = 8000, 3000, 200


def _lead_in(lead_ms):
    """Neutral gameplay-time lead plus A pulses (menu-only segments) every 3 s so title -> Campaign -> Continue (and a
    'press A to continue' after loading) advance by themselves; each pulse is released before the next, so it is a fresh edge.
    Menu segments are inert once the game is in gameplay."""
    out, cursor = [], 0
    for start in range(PRESS_FIRST_MS, lead_ms - PRESS_MS + 1, PRESS_EVERY_MS):
        if start > cursor:
            out.append({"start_ms": cursor, "end_ms": start, "head_pitch_degrees": 0, "input": dict(NEUTRAL)})
        out.append({"start_ms": start, "end_ms": start + PRESS_MS, "input": {"a": True}, "when": "menu"})
        cursor = start + PRESS_MS
    if cursor < lead_ms:
        out.append({"start_ms": cursor, "end_ms": lead_ms, "head_pitch_degrees": 0, "input": dict(NEUTRAL)})
    return out


def build(lead_ms):
    t = lead_ms
    segments = _lead_in(lead_ms)
    segments.append(_hold(t, t + 1500, NEUTRAL, REACH))           # settle the hand at the object
    segments.append(_hold(t + 1500, t + 2700, CLOSE, REACH))      # grab
    segments += _glide(t + 2700, t + 4200, REACH, CHEST, 15, GRIP)  # lift
    segments.append(_hold(t + 4200, t + 5500, GRIP, CHEST))       # carry at the chest
    segments += _glide(t + 5500, t + 7500, CHEST, FLOOR, 20, GRIP)  # lower the hand to the floor
    segments.append(_hold(t + 7500, t + 8500, GRIP, FLOOR))       # hold at the floor
    segments.append(_hold(t + 8500, t + 9500, NEUTRAL, FLOOR))    # release
    segments.append(_hold(t + 9500, t + 14000, NEUTRAL, FLOOR))   # wait: the object must have dropped, nothing else must happen
    return segments
