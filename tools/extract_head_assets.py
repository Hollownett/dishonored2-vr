"""Extract the protagonists' masked NPC heads (face, mask, hair, eyes) for the calibration-pose renders.

The first-person player body has no head. The main menu (maps/campaign/menu/menu_p.entities) poses
models/characters/posing/story/{corvo_attano,emily_kaldwin}.md6, which inherit the plain NPC heads on the full NPC
bodies (215/219-joint skeletons, not poseable by the 20-joint player solver frames). So the posed player body keeps its
own outfit and gets the masked NPC head variants:
  corvo: models/characters/tall/str/corvo/t_str_corvo_head_mask.md6 (lod 0 head01/t_str_corvo01_head01.md6mesh:
         skull mask t_str_corvo_mask.material, hair, face; skeleton head01/t_str_corvo01_head01.md6skl)
  emily: models/characters/female/emily/head01/emily_head01.md6 (lod 0 emily_head01_head.md6mesh: face, hair and
         her cloth face mask emily_scarf01.material; skeleton head01/emily_head01.md6skl)
The decl gives the mesh, the mesh names its skeleton and per-surface materials, each material decl names its
diffuse map, whose full-size mip is generated/image/<map>.bimage7_mip0 (20-byte mip header + BC7/BC3 blocks).

Writes artifacts/fullbody-assets/raw/<source path> (raw resources, like tools/extract_player_raw.py),
pack/<char>/head.npz (body.npz layout from build_player_asset_pack.merge_surfaces, plus per-surface 'texture'
and 'alpha_test', and the NPC skeleton bind 'skeleton_joint_names' / 'skeleton_model_bind_3x4') and
textures/<char>_head_d.png plus the mask/hair/eye diffuse maps under their game names. Teeth, eyelashes and the
additive mask lens glass are dropped (hidden or sub-pixel at calibration-render size). Read-only on the game folder.

    python tools/extract_head_assets.py [--game PATH]
"""
import argparse
import re
import struct
from pathlib import Path

import numpy as np

from build_player_asset_pack import merge_surfaces, parse_mesh, parse_skeleton
from extract_game_art import Containers, bc_decode, find_game, norm

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / 'artifacts/fullbody-assets'
HEADS = {'corvo': 'models/characters/tall/str/corvo/t_str_corvo_head_mask.md6',
         'emily': 'models/characters/female/emily/head01/emily_head01.md6'}
SKIP = ('teeth', 'lashes', 'glass')


def fetch(game, names):
    """Read resources by destination name, also saving each under raw/<source path>."""
    c = Containers(game, set(names))
    out = {}
    for name in names:
        data = c.read(name)
        source = norm(next(iter(c.entries[name].values()))['source'])
        target = ASSETS / 'raw' / (name if source.endswith('.tga') else source)   # image headers keep .bimage7
        target.parent.mkdir(parents=True, exist_ok=True)
        if not target.exists() or target.read_bytes() != data:
            target.write_bytes(data)
        out[name] = (data, target)
    return out


def decl_name(path):
    return 'generated/decls/%s/%s.decl' % ({'.md6': 'md6def', '.material': 'material'}[Path(path).suffix], path)


def mip0_name(tga):
    return 'generated/image/%s.bimage7_mip0' % tga.removesuffix('.tga')


def decode_mip0(header, mip):
    fmt = struct.unpack_from('<I', header, 0x0c)[0]
    level, _, w, h, size = struct.unpack_from('<5I', mip, 0)
    codec = {16: 'bc3', 20: 'bc7'}.get(fmt)
    if header[:4] != b'BIM\x18' or not codec or level or len(mip) != 20 + size:
        raise ValueError('unsupported bimage7 mip (format %d)' % fmt)
    return bc_decode(mip[20:], w, h, codec)


def extract(game, char):
    decl = HEADS[char]
    text = fetch(game, [decl_name(decl)])[decl_name(decl)][0].decode()
    mesh_path = re.search(r'lod 0\s*\{\s*mesh\s+"([^"]+)"', text).group(1)
    mesh_name = 'generated/basemodel/%s.bmd6model' % mesh_path[:-len('.md6mesh')]
    mesh = parse_mesh(fetch(game, [mesh_name])[mesh_name][1])
    skl_name = 'generated/skeleton/%s.edgeskl' % mesh['skeleton'][:-len('.md6skl')]
    skeleton = parse_skeleton(fetch(game, [skl_name])[skl_name][1])
    surfaces = [s for s in mesh['surfaces'] if not any(k in s['material'] for k in SKIP)]
    materials = fetch(game, sorted({decl_name(s['material']) for s in surfaces}))
    maps, textures, alpha = {}, [], []
    for s in surfaces:
        mtext = materials[decl_name(s['material'])][0].decode()
        tga = re.search(r'^\s*diffusemap\s+(\S+)', mtext, re.M | re.I).group(1).lower()
        skin = re.search(r'^\s*mainProgram\s+arksssrender', mtext, re.M | re.I)
        maps.setdefault(tga, '%s_head_d.png' % char if skin else Path(tga).stem + '.png')
        textures.append(maps[tga])
        alpha.append(bool(re.search(r'^\s*alphatest\s+1', mtext, re.M | re.I)))
    images = fetch(game, [n for tga in maps for n in (mip0_name(tga), mip0_name(tga)[:-len('_mip0')])])
    (ASSETS / 'textures').mkdir(parents=True, exist_ok=True)
    for tga, png in maps.items():
        image = decode_mip0(images[mip0_name(tga)[:-len('_mip0')]][0], images[mip0_name(tga)][0])
        image.save(ASSETS / 'textures' / png)
        print('  texture %-28s %dx%d <- %s' % (png, image.width, image.height, tga))
    merged = merge_surfaces(dict(surfaces=surfaces))
    if merged['joints'].max() >= len(skeleton['names']):
        raise ValueError('head joint index outside its skeleton')
    merged.update(texture=np.array(textures), alpha_test=np.array(alpha),
                  skeleton_joint_names=np.array(skeleton['names']),
                  skeleton_model_bind_3x4=skeleton['model'][:, :3].astype(np.float32),
                  source_mesh=np.array(mesh_path), source_skeleton=np.array(mesh['skeleton']))
    (ASSETS / 'pack' / char).mkdir(parents=True, exist_ok=True)
    np.savez_compressed(ASSETS / 'pack' / char / 'head.npz', **merged)
    print('%s: %s, %d verts, %d tris, %d joints in %s' % (char, mesh_path, len(merged['positions']),
          len(merged['index']), len(skeleton['names']), mesh['skeleton']))
    for s, t, a in zip(surfaces, textures, alpha):
        print('  surface %-70s %5d verts -> %s%s' % (s['material'], s['vertices'], t, ' (alpha test)' if a else ''))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--game', type=Path)
    game = find_game(ap.parse_args().game).resolve()
    if ASSETS.resolve().is_relative_to(game):
        raise SystemExit('Refusing to write inside the game folder')
    for char in HEADS:
        extract(game, char)


if __name__ == '__main__':
    main()
