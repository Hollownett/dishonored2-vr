"""Headset-free self test of the VR mod against the running game.

Runs a simulated-VR scenario through inject_probe.py, then checks what the player would see and do:
HUD screen present and nothing clipped at its edges, wheel layer never dropped, stick reaching the game.
UI screens are saved as PNGs in the run folder (build/ui-dump knob).

    python tools/vr_selftest.py --dll d2vr_render_probe_arm_v258.dll --scenario all
    python tools/vr_selftest.py --dll ... --scenario wheel --launch      # start the game in the background and load the last save

Never takes focus or sends global input: keys are posted to the game window, and the game is configured not to
pause without focus (tools/game_keys.py --prepare-config)."""
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
    SCENARIOS['grab'] += [[t, t + 1000, NEUTRAL, hand], [t + 1000, t + 1800, {"right_grip": 1.0}, hand], [t + 1800, t + 2500, NEUTRAL, hand]]
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
    SCENARIOS['pickup'] += [[t, t + 1100, NEUTRAL, hand], [t + 1100, t + 1900, {"right_grip": 1.0}, hand], [t + 1900, t + 2600, NEUTRAL, hand]]
# Damage: idle while the engine hurts the player (--damage DECL): the damage vignette must blend into the view.
SCENARIOS['damage'] = [[0, 12000, NEUTRAL, None]]
# Aim: both hands pitched 20 degrees down (hand_pitch_degrees); the crosshair must follow the left hand in the eye.
SCENARIOS['aim'] = [[0, 8000, NEUTRAL, None]]
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
SCENARIOS['reach'] = [[0, 1500, NEUTRAL, [0.25, -0.3, -0.25]]] + glide(1500, 3000, [0.25, -0.3, -0.25], [0.0, -0.85, -0.35], 30) +     [[3000, 3800, NEUTRAL, [0.0, -0.85, -0.35]], [3800, 4800, {"right_grip": 1.0}, [0.0, -0.85, -0.35]], [4800, 6000, NEUTRAL, [0.0, -0.85, -0.35]]]
# Hold and throw a native carryable (bottle; use with --near bottle): squeeze at the bottle (native Use -> HoldObject), keep
# squeezing while lifting to the chest (the bottle must follow the hand), then swing forward fast and let go (throw).
_HA, _HC, _HD = [-0.2, -0.8, -0.2], [-0.1, -0.2, -0.2], [-0.1, 0.1, -0.75]
_GRIP = {"right_grip": 1.0}
SCENARIOS['hold'] = [[0, 1500, NEUTRAL, _HA], [1500, 2700, _GRIP, _HA]] + glide(2700, 3700, _HA, _HC, 20, _GRIP) +     [[3700, 5500, _GRIP, _HC]] + glide(5500, 5750, _HC, _HD, 8, _GRIP) + [[5750, 9000, NEUTRAL, _HD]]
# Sword over the right shoulder: reach back (sheathes), return, reach back and squeeze grip (draws). Offsets are relative to
# the default right hand (0.2,-0.3,-0.4 from the head); the scabbard point is right .17, down .10, back .10.
BACK = [-0.03, 0.20, 0.50]
SCENARIOS['sheathe'] = [[0, 2000, NEUTRAL, [0.0, 0.0, 0.0]]] + glide(2000, 3000, [0.0, 0.0, 0.0], BACK, 20) +     [[3000, 4000, NEUTRAL, BACK]] + glide(4000, 5000, BACK, [0.0, 0.0, 0.0], 20) + [[5000, 7000, NEUTRAL, [0.0, 0.0, 0.0]]] +     glide(7000, 8000, [0.0, 0.0, 0.0], BACK, 20) + [[8000, 9000, {"right_grip": 1.0}, BACK]] + glide(9000, 10000, BACK, [0.0, 0.0, 0.0], 20) +     [[10000, 12000, NEUTRAL, [0.0, 0.0, 0.0]]]
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
SCENARIOS['all'] = [[0, 3000, NEUTRAL, None], [3000, 7000, {"move_y": 1.0}, None],
                    [7000, 10000, {"x": True}, [0.05, 0.2, -0.2]], [10000, 13000, {"x": True}, [-0.15, 0.25, -0.2]],
                    [13000, 16000, NEUTRAL, None]]
# Player-FSM states (tools/player_states.py) a scenario must reach: the mechanic really ran in the engine.
FORBIDDEN_STATES = {'flick': ['Slash'], 'reach': ['Slash']}  # accidental attacks
EXPECTED_STATES = {'chop': ['Slash'], 'pickup': ['LootObject', 'Interact', 'HoldObject'], 'reach': ['LootObject', 'Interact', 'HoldObject'], 'swing': ['Slash', 'Assassination'], 'duel': ['Slash'], 'grab': ['LootObject', 'HoldObject', 'Interact', 'CarryCorpse', 'ReadNote'], 'powers': ['UsePower', 'Blinking', 'UseWindblast', 'UsePhantasmalLink', 'InBendTime', 'StoneShroud', 'DoppelgangerSwap', 'UseHeart'], 'jump': ['Jumping', 'Falling'],  # Falling always follows a jump; the short Jumping state can fall between samples
                   'lean': ['Leaning'], 'power': ['UsePower', 'Blinking'], 'wheel': ['Powerwheel'],
                   'walk': ['Running', 'Sneaking', 'Sprinting'], 'all': ['Powerwheel'], 'potion': ['Powerwheel'], 'hide': ['Powerwheel'], 'sheathe': ['Undraw']}
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


def command(line):
    with open(os.path.join(ART, 'vr-command.txt'), 'a') as f:
        f.write(line + chr(10))


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
    """With the mod injected: dismiss the load screen / pause menu through the mod's virtual pad (no focus)."""
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
            command('pad a 200')  # loading done: "press a button"
        elif mask in (4, 1024):
            command('pad b 200')  # pause menu / black market: back to the game
        elif mask == 8192:
            command('pad a 200')  # Game Over: "Restart From Last Checkpoint" is the default choice
        time.sleep(2)
    return False


def npc_stand(pid, behind, civilian=False, beside=False, skip=()):
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
    feet = listing['player']['pos']
    before = {e['addr']: e['pos'] for e in first}
    # Patrolling guards walked past the player before the first swing: prefer NPCs standing still, guards first.
    still = [e for e in npcs if e['addr'] in before and math.dist(before[e['addr']], e['pos']) < .15]
    npcs = sorted(still, key=lambda e: (bool(e.get('guard')) == civilian, e['dist'])) or npcs
    if not npcs:
        return None, None
    npc = npcs[0]
    base, read = attach(pid)
    fwd = None
    for comp, name in World(read, base).components(int(npc['addr'], 16)):
        if name.startswith('arkCpntReferenceFrame'):
            fwd = struct.unpack('<3f', read(comp + 0x3c, 12))
            break
    if not fwd or math.hypot(fwd[0], fwd[1]) < .1:
        fwd = (feet[0] - npc['pos'][0], feet[1] - npc['pos'][1], 0)  # unknown heading: approach from the player's side
        behind = False
    d = math.hypot(fwd[0], fwd[1])
    fx, fy = fwd[0] / d, fwd[1] / d
    if beside:  # at the NPC's right/left: neither a takedown from behind nor face to face
        fx, fy = -fy, fx
    side = 1 if behind else -1  # measured: standing at +row0 put the player BEHIND (the swing became an Assassination)
    x, y = npc['pos'][0] + side * fx * .9, npc['pos'][1] + side * fy * .9
    yaw = math.degrees(math.atan2(npc['pos'][1] - y, npc['pos'][0] - x))
    return (x, y, npc['pos'][2] + 1.65, yaw), npc


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


def write_scenario(name):
    segments = []
    for start, end, inp, hand, *more in SCENARIOS[name]:
        seg = {"start_ms": start + LEAD_MS, "end_ms": end + LEAD_MS, "head_pitch_degrees": -40 if name == 'pickup' else 0, "input": inp}
        if hand:
            seg["left_hand_offset" if name in ('potion', 'hide', 'floorleft', 'waist') else "right_hand_offset"] = hand  # the wheel cursor belongs to the X (left) hand
        if name in ('pickup', 'reach', 'hold'):
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
    json.dump({"segments": segments}, open(path, 'w'))
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
    ap.add_argument('--near', choices=['item', 'bottle', 'npc', 'npc-front', 'npc-side', 'civ-front'],
                    help='teleport next to the nearest pickable item / 0.9 m behind (npc) or in front of (npc-front) the nearest guard, facing it')
    ap.add_argument('--teleport', nargs=3, type=float, metavar=('X', 'Y', 'Z'), help='engine setviewpos (eye position) before the scenario')
    ap.add_argument('--damage', metavar='DECL', help='hurt the player every 3 s with this damage decl (engine `damage player1 DECL`, god off)')
    ap.add_argument('--inject-at-title', action='store_true',
                    help='with --launch: inject at the title screen, then load the save (UI movies loaded with the level get the owned-load patches, as the headset launcher does)')
    ap.add_argument('--blind-ai', action='store_true', help='NPC vision budget ark_vision_max_checks 0 during the scenario (restored to 150): guards do not spot or kill the test player (unverified live)')
    ap.add_argument('--every', metavar='CMD', help='send this mod test command every 3 s during the scenario (e.g. "pad x 200")')
    a = ap.parse_args()
    if a.launch and not game_pid():
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
        LEAD_MS = 75000  # the save loads inside the session: the scenario starts after the load
    elif a.load_save:  # title -> Campaign -> Continue (posted to the window; the load screen is dismissed by the mod's pad)
        keys(0x0D, 0x0D, 0x0D)
        time.sleep(20)
    script, seconds = write_scenario(a.scenario)
    knob = os.path.join(ROOT, 'build', 'ui-dump')
    open(knob, 'w').close()
    # Combat scenarios: the player ignores damage (engine god/buddha are not honoured) so a fight can run to the end.
    invulnerable = os.path.join(ROOT, 'build', 'sim-invulnerable')
    if a.scenario in ('duel', 'swarm'):
        open(invulnerable, 'w').close()
    dump_dir = os.path.join(ART, 'ui-dump')
    for f in glob.glob(os.path.join(dump_dir, '*')):
        os.remove(f)
    for stale in ('npc-watch.csv', 'native-combat.csv'):
        if os.path.exists(os.path.join(ART, stale)):
            os.remove(os.path.join(ART, stale))
    try:
        proc = subprocess.Popen([sys.executable, os.path.join(ROOT, 'tools', 'inject_probe.py'), '--pid', str(pid), '--dll-name', a.dll,
                                 *SIM_FLAGS, '--simulated-gameplay-script', script, '--seconds', str(int(seconds + 12))],
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        proc_start = time.time()
        time.sleep(4)
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
        scenario_end = time.time() + seconds - 1
        if reached and a.near == 'bottle':  # a native carryable (HoldObject), on the floor or a low table
            import math, re
            listing = json.loads(subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'entities.py'), '--pid', str(pid), '--kind', 'all', '--json', '--limit', '400'],
                                                capture_output=True, text=True).stdout)
            player = listing['player']['pos']
            items = [e for e in listing['entities'] if e.get('cls') == 'arkMovable' and e.get('pickable') and re.search('bottle|glass|cup|mug', e['name'], re.I)
                     and -0.2 < e['pos'][2] - player[2] < 1.0 and e['dist'] < 8]
            items.sort(key=lambda e: e['dist'])
            if items:
                item = items[0]['pos']
                dx, dy = item[0] - player[0], item[1] - player[1]
                d = max(1e-6, math.hypot(dx, dy))
                stand = (item[0] - dx / d * .6, item[1] - dy / d * .6, player[2] + 1.3)
                yaw = math.degrees(math.atan2(dy, dx))
                print('near bottle', items[0]['name'], item, 'stand', [round(v, 2) for v in stand])
                set_god(pid, True)
                command('console setviewpos %.2f %.2f %.2f %.1f' % (*stand, yaw))
                time.sleep(2)
        if reached and a.near == 'item':
            import math
            listing = json.loads(subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'entities.py'), '--pid', str(pid), '--kind', 'item', '--json', '--limit', '40'],
                                                capture_output=True, text=True).stdout)
            player = listing['player']['pos']
            # Same floor as the player (feet z within 0.5 m): a teleport beside a raised item can drop the player off a ledge.
            items = [e for e in listing['entities'] if e.get('cls') == 'arkPickup' and e.get('pickable') and abs(e['pos'][2] - player[2]) < .5]
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
        if reached and a.near in ('npc', 'npc-front', 'npc-side', 'civ-front'):
            # Arrive just before the scenario (script t ~ LEAD): a guard given 15 s kills the player even in god mode.
            time.sleep(max(0, proc_start + LEAD_MS / 1000 - 4 - time.time()))
            if a.blind_ai:
                command('cvar ark_vision_max_checks 0')
            skip = set()
            for attempt in range(3):  # a spot on a ledge or table left the blade swinging over the guard (sim v382: 1 m above)
                stand, npc = npc_stand(pid, a.near == 'npc', a.near == 'civ-front', a.near == 'npc-side', skip)
                if not stand:
                    break
                print('near npc', npc['name'], npc['pos'], 'stand', [round(v, 2) for v in stand])
                if attempt == 0:
                    set_god(pid, True)
                    set_god(pid, True, buddha=True)
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
        if reached and a.teleport:
            if a.scenario == 'swarm':  # bloodflies killed the god-mode player within 20 s: arrive 2 s before the first sweep
                time.sleep(max(0, proc_start + LEAD_MS / 1000 + 2 - 2 - time.time()))
            set_god(pid, True)  # engine god (the bare cvar write is not honoured): water, fish and guards must not end the test
            command('console setviewpos %.2f %.2f %.2f' % tuple(a.teleport))
            time.sleep(2)
        samples = []  # (logic, |published stick|) while the scenario runs
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
                    seen_states |= {st['name'].replace('arkPlayerState', '') for st in player_states.states(read, base, machine) if st['active'] == 1}
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
                if time.time() <= scenario_end:
                    arm_samples.append(st.get('arm_hidden', 0))
                if a.scenario in ('potion', 'hide') and st.get('wheel_zone'):
                    wz = json.dumps(st['wheel_zone'])
                    if not wheel_trace or wheel_trace[-1] != wz:
                        wheel_trace.append(wz)
            except Exception:
                pass
            time.sleep(.5)
        out, _ = proc.communicate()
        run = subprocess.CompletedProcess(proc.args, proc.returncode, out or '', '')
    finally:
        os.remove(knob)
        if os.path.exists(invulnerable):
            os.remove(invulnerable)
    runs = sorted(glob.glob(os.path.join(ART, 'runs', '*')), key=os.path.getmtime)
    run_dir = runs[-1] if runs else ART
    status = json.load(open(os.path.join(ART, 'xr-live-status.json')))
    frames = to_png(dump_dir, os.path.join(run_dir, 'ui-dump-png'))
    hud = [f for f in frames if f[0].startswith('hud')]
    wheel = [f for f in frames if f[0].startswith('wheel')]
    checks = []

    def check(name, ok, detail):
        checks.append((name, bool(ok), detail))

    check('reached gameplay', reached, 'logic mask 2 before the scenario began')
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
    if a.near in ('npc', 'npc-front', 'npc-side', 'civ-front'):
        combat_latency(check, target_hp(pid, npc) if npc else None)
    if a.scenario in ('potion', 'hide'):
        print('wheel zone trace:', wheel_trace[-8:])
    print(f'run: {run_dir}')
    for name, ok, detail in checks:
        print(('PASS ' if ok else 'FAIL ') + name + ' - ' + str(detail))
    sys.exit(0 if all(ok for _, ok, _ in checks) else 1)


if __name__ == '__main__':
    main()
