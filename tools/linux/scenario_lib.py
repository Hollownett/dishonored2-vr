"""Segment helpers shared by the gameplay-script scenarios (moved from oil_tank_drop.py, which keeps its behaviour).

Hand offsets are metres relative to the simulated default hand (0.4 m ahead, 0.2 m right, 0.35 m below the eye); x right, y up,
z backward. Numbers follow tools/vr_selftest.py's 'hold' scenario. Segments are the format of tools/simulated_gameplay_script.py.
"""
NEUTRAL = {"move_x": 0.0}
GRIP = {"right_grip": 1.0}
CLOSE = {"right_grip": 1.0, "right_trigger": 1.0}  # a full fist for the grab; the grip alone then keeps the object
REACH = [-0.2, -0.8, -0.2]   # hand in front of the knees, within reach of an object on the floor
CHEST = [-0.1, -0.2, -0.2]   # lifted to the chest
FLOOR = [-0.2, -1.15, -0.3]  # hand about 0.1 m above the floor
HEAD_PITCH = -50
PRESS_FIRST_MS, PRESS_EVERY_MS, PRESS_MS = 8000, 3000, 200
FRAME_MS = 11  # one 90 Hz frame


def lerp(a, b, k):
    return [round(x + (y - x) * k, 4) for x, y in zip(a, b)]


def hold(t0, t1, inp, offset, left=None, pitch=HEAD_PITCH):
    seg = {"start_ms": t0, "end_ms": t1, "head_pitch_degrees": pitch, "input": dict(inp), "right_hand_offset": list(offset)}
    if left is not None:
        seg["left_hand_offset"] = list(left)
    return seg


def glide(t0, t1, a, b, steps, inp, left=None, pitch=HEAD_PITCH):
    """right hand a -> b in `steps` segments; `left`, if given, is an (a, b) pair gliding the left hand alongside."""
    out = []
    for i in range(steps):
        k = (i + 1) / steps
        seg = {"start_ms": round(t0 + (t1 - t0) * i / steps), "end_ms": round(t0 + (t1 - t0) * (i + 1) / steps),
               "head_pitch_degrees": pitch, "input": dict(inp), "right_hand_offset": lerp(a, b, k)}
        if left is not None:
            seg["left_hand_offset"] = lerp(left[0], left[1], k)
        out.append(seg)
    return out


def mirror(offset):
    """The left hand's offset for a right-hand one (x flips)."""
    return [-offset[0], offset[1], offset[2]]


def two(inp):
    """Right-hand input `inp` repeated for the left hand (right_* -> left_*), both hands together."""
    out = dict(inp)
    for key, value in inp.items():
        if key.startswith("right_"):
            out["left_" + key[6:]] = value
    return out


def lead_in(lead_ms):
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


def console(t_ms, line):
    """A 1-frame segment carrying an engine console line (gameplay script v8). The DLL fires it once, at the first script tick at or
    after t_ms while in gameplay (a teleport scheduled during a load waits for gameplay), through the 'console' test-command path."""
    return {"start_ms": t_ms, "end_ms": t_ms + FRAME_MS, "input": dict(NEUTRAL), "console": line}


def setviewpos(x, y, z, yaw_deg, pitch_deg=None):
    """The engine teleport (docs/native-console.md): x y z is the EYE position in game units (metres, z up), yaw in degrees."""
    line = "setviewpos %.3f %.3f %.3f %.1f" % (x, y, z, yaw_deg)
    return line if pitch_deg is None else line + " %.1f" % pitch_deg
