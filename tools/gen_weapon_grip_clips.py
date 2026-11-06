"""Generate src/weapon_grip_clips.hpp from the shipped player arm idle clips (read-only on the game).

Why: the mod keeps a calm gun clip from the arm the game animates, but that only exists for a type the arm has actually
played. A mod-owned copy of a type that was never held calmly natively (or never held in this session) must still get the
engine's authored grip for ITS type, never the other type's. These two clips are the engine's own left-arm idle poses for
the pistol and the crossbow, baked once at build-authoring time.

Sources decoded (read-only from the game install):
  models/characters/player/_animations_/emily/leftarm/ply_leftarm_gun_idle.md6anim
  models/characters/player/_animations_/emily/leftarm/ply_leftarm_crossbow_idle.md6anim
  models/characters/player/emily/player_leftarm_emily.md6skl (bind pose / parents)
The output is model space, unscaled, frame 0. The live captured clip always wins at runtime; the baked clip is the
"sampled" source and the procedural grip is the last-resort fallback.

Usage: python tools/gen_weapon_grip_clips.py [--game PATH] [--out PATH]
"""
import argparse
import json
import math
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from extract_arm_assets import index_entries, resource_path, exact, unpack_asset  # noqa: E402
import decode_md6anim as md6  # noqa: E402
from build_player_asset_pack import parse_skeleton  # noqa: E402

DEFAULT_GAME = Path(r'C:\Program Files (x86)\Steam\steamapps\common\Dishonored2')
SKELETON = 'models/characters/player/emily/player_leftarm_emily.md6skl'
CLIPS = {
    'gun': 'models/characters/player/_animations_/emily/leftarm/ply_leftarm_gun_idle.md6anim',
    'crossbow': 'models/characters/player/_animations_/emily/leftarm/ply_leftarm_crossbow_idle.md6anim',
}
RAW = ROOT / 'artifacts/fullbody-assets/raw'
PACK = ROOT / 'artifacts/fullbody-assets/pack'


def extract(game):
    """Copy the skeleton and the two clips out of the final patch indexes (no game file is modified)."""
    wanted = {SKELETON: RAW / 'player_leftarm_emily.md6skl'}
    for kind, source in CLIPS.items():
        wanted[source] = RAW / 'anims' / ('ply_leftarm_' + kind + '_idle.md6anim')
    found = {}
    for container in ('game1', 'game2', 'game3', 'game4'):
        index = game / 'base' / (container + '_patch.index')
        if not index.is_file():
            raise SystemExit('missing final patch index ' + str(index))
        with index.open('rb') as stream:
            for entry in index_entries(stream):
                source = entry['source'].replace(chr(92), '/').lower()
                if source in wanted and entry['type'] in ('skeleton', 'anim') and entry['size'] and entry['packed']:
                    found[source] = (container, entry)
    for source, target in wanted.items():
        if source not in found:
            raise SystemExit('resource not found in the final indexes: ' + source)
        container, entry = found[source]
        with resource_path(game / 'base', container, entry['resource_id']).open('rb') as stream:
            if exact(stream, 4) != b'\x04SER':
                raise SystemExit('unexpected resource magic for ' + source)
            stream.seek(entry['offset'])
            data = unpack_asset(exact(stream, entry['packed']), entry['size'])
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
    skeleton = parse_skeleton(wanted[SKELETON])
    PACK.mkdir(parents=True, exist_ok=True)
    (PACK / 'skeleton_left_arm.json').write_text(json.dumps({
        'joint_names': skeleton['names'], 'parents': [int(p) for p in skeleton['parents']],
        'local_bind': [{'rot_xyzw': [float(x) for x in q], 'trans': [float(x) for x in t],
                        'scale': [float(s[0]), float(s[1]), float(s[2])]} for (t, q, s) in skeleton['local']]}))


def qmat(q):
    x, y, z, w = q
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                     [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                     [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])


def mat2quat(r):
    tr = r[0, 0] + r[1, 1] + r[2, 2]
    if tr > 0:
        s = math.sqrt(tr + 1.0) * 2; w = 0.25 * s; x = (r[2, 1] - r[1, 2]) / s; y = (r[0, 2] - r[2, 0]) / s; z = (r[1, 0] - r[0, 1]) / s
    elif r[0, 0] > r[1, 1] and r[0, 0] > r[2, 2]:
        s = math.sqrt(1.0 + r[0, 0] - r[1, 1] - r[2, 2]) * 2; w = (r[2, 1] - r[1, 2]) / s; x = 0.25 * s; y = (r[0, 1] + r[1, 0]) / s; z = (r[0, 2] + r[2, 0]) / s
    elif r[1, 1] > r[2, 2]:
        s = math.sqrt(1.0 + r[1, 1] - r[0, 0] - r[2, 2]) * 2; w = (r[0, 2] - r[2, 0]) / s; x = (r[0, 1] + r[1, 0]) / s; y = 0.25 * s; z = (r[1, 2] + r[2, 1]) / s
    else:
        s = math.sqrt(1.0 + r[2, 2] - r[0, 0] - r[1, 1]) * 2; w = (r[1, 0] - r[0, 1]) / s; x = (r[0, 2] + r[2, 0]) / s; y = (r[1, 2] + r[2, 1]) / s; z = 0.25 * s
    return np.array([x, y, z, w])


def fk(decoded, parents):
    q = np.array([decoded['Q'][0, j] for j in range(len(parents))])
    t = np.array([decoded['T'][0, j] for j in range(len(parents))])
    rotations = [None] * len(parents); positions = [None] * len(parents)
    for j in range(len(parents)):
        r = qmat(q[j])
        if parents[j] < 0:
            rotations[j], positions[j] = r, t[j].copy()
        else:
            rotations[j], positions[j] = rotations[parents[j]] @ r, positions[parents[j]] + rotations[parents[j]] @ t[j]
    return [mat2quat(rotations[j]) for j in range(len(parents))], positions


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', type=Path, default=DEFAULT_GAME)
    parser.add_argument('--out', type=Path, default=ROOT / 'src/weapon_grip_clips.hpp')
    parser.add_argument('--no-extract', action='store_true', help='use the assets already under artifacts/fullbody-assets')
    args = parser.parse_args()
    if not args.no_extract:
        extract(args.game)
    skeleton = json.loads((PACK / 'skeleton_left_arm.json').read_text())
    names, parents = skeleton['joint_names'], skeleton['parents']
    body = ['''#pragma once
// Baked authored arm poses for weapon types whose clip was never captured live (task 1, round 4).
// Source: the shipped player animation assets, decoded read-only from the game install:
//   models/characters/player/_animations_/emily/leftarm/ply_leftarm_gun_idle.md6anim
//   models/characters/player/_animations_/emily/leftarm/ply_leftarm_crossbow_idle.md6anim
// via tools/decode_md6anim.py + the left arm skeleton (player_leftarm_emily.md6skl), frame 0, model space, unscaled.
// These are the engine's own authored left-arm poses for the two slot-1 gun types, so a hand whose type was never held
// calmly natively still gets that type's grip, never the other type's. The right hand mirrors them (power_hand::mirror_joint).
// Regenerate with tools/gen_weapon_grip_clips.py (needs the extracted assets). The live captured clip always wins.
#include <array>
#include <cstddef>
namespace d2vr::fullbody::grip_clips {
struct BakedJoint {float q[4];float p[3];};
enum Type : unsigned {type_none=0,type_pistol=1,type_crossbow=2};
''']
    for kind, type_name in (('gun', 'pistol'), ('crossbow', 'crossbow')):
        decoded = md6.decode(RAW / 'anims' / ('ply_leftarm_' + kind + '_idle.md6anim'))
        rotations, positions = fk(decoded, parents)
        body.append('// %s: ply_leftarm_%s_idle.md6anim frame 0 (Emily), model space, unscaled\n' % (kind, kind))
        body.append('inline constexpr std::array<BakedJoint,69> %s{{\n' % kind)
        for j in range(len(names)):
            q, p = rotations[j], positions[j]
            body.append('    {{%+.9ff,%+.9ff,%+.9ff,%+.9ff},{%+.9ff,%+.9ff,%+.9ff}}, // %d %s\n'
                        % (q[0], q[1], q[2], q[3], p[0], p[1], p[2], j, names[j]))
        body.append('}};\n')
    body.append('''inline const std::array<BakedJoint,69>* sampled(unsigned type) noexcept {
    switch(type) {case type_pistol:return &gun;case type_crossbow:return &crossbow;default:return nullptr;}
}
} // namespace d2vr::fullbody::grip_clips
''')
    args.out.write_text(''.join(body))
    print('wrote', args.out, '(pistol + crossbow)')


if __name__ == '__main__':
    main()
