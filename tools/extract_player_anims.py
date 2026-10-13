"""Extract raw player .md6anim files (read-only on game) to artifacts/fullbody-assets/anims/raw/<path>.
Usage: python tools/extract_player_anims.py [substring ...]   (no args: ALL player anims)"""
import sys
from pathlib import Path
from extract_arm_assets import index_entries, resource_path, exact, unpack_asset, MAX_ASSET

GAME = Path(r'C:\Program Files (x86)\Steam\steamapps\common\Dishonored2\base')
OUT = Path(__file__).resolve().parents[1] / 'artifacts/fullbody-assets/anims/raw'
PREFIX = 'models/characters/player/_animations_/'


def main(*subs):
    latest = {}
    for c in ('game1', 'game2', 'game3', 'game4'):
        with (GAME / (c + '_patch.index')).open('rb') as f:
            for e in index_entries(f):
                s = e['source'].replace(chr(92), '/').lower()
                if e['type'] == 'anim' and s.startswith(PREFIX) and e['size'] and e['packed']:
                    latest[s] = (c, e)
    n = 0
    for s, (c, e) in sorted(latest.items()):
        if subs and not any(x in s for x in subs):
            continue
        if max(e['size'], e['packed']) > MAX_ASSET:
            continue
        with resource_path(GAME, c, e['resource_id']).open('rb') as f:
            f.seek(e['offset']); data = unpack_asset(exact(f, e['packed']), e['size'])
        t = OUT / s; t.parent.mkdir(parents=True, exist_ok=True); t.write_bytes(data); n += 1
    print('extracted', n, 'of', len(latest))


if __name__ == '__main__':
    main(*sys.argv[1:])
