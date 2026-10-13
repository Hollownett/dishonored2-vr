"""Copy raw player body/arm resources (mesh, skeleton, decl) out of the game packages.

Read-only on the game install; writes artifacts/fullbody-assets/raw/<source path>.
"""
import sys
from pathlib import Path
from extract_arm_assets import index_entries, resource_path, exact, unpack_asset, MAX_ASSET

GAME = Path(r'C:\Program Files (x86)\Steam\steamapps\common\Dishonored2\base')
OUT = Path(__file__).resolve().parents[1] / 'artifacts/fullbody-assets/raw'
TYPES = {'image', 'material', 'baseModel', 'skeleton', 'md6Def', 'entityDef', 'animSet', 'playerLocomotion', 'animBasic'}


def main(prefix='models/characters/player/'):
    latest = {}  # later patch indexes override earlier ones
    for container in ('game1', 'game2', 'game3', 'game4'):
        index = GAME / (container + '_patch.index')
        with index.open('rb') as f:
            for e in index_entries(f):
                s = e['source'].replace('\\', '/').lower()
                if e['type'] in TYPES and s.startswith(prefix) and e['size'] and e['packed']:
                    latest[s] = (container, e)
    for s, (container, e) in sorted(latest.items()):
        if max(e['size'], e['packed']) > MAX_ASSET:
            print('skip (size)', s); continue
        with resource_path(GAME, container, e['resource_id']).open('rb') as f:
            f.seek(e['offset']); data = unpack_asset(exact(f, e['packed']), e['size'])
        target = OUT / s
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        print(e['type'], s, len(data))


if __name__ == '__main__':
    main(*sys.argv[1:])
