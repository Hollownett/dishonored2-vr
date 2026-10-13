"""Extract Dishonored 2 UI art from the user's own install into the git-ignored artifacts/game-art cache.

Read-only on the game folder. The output is the user's local copy of the game's art: never commit or
redistribute it. See docs/game-art.md.

Formats (all verified against the pinned Steam build):
- Containers: base/gameN_patch.index + *.resources, read with tools/extract_arm_assets.py
  (reference: https://github.com/chipolux/disrev/blob/main/resources.py).
- Iggy movies (ui/<movie>.iggy): subfile id 8 is the external texture table for ui/<movie>.raw.iggytex:
  u16 tag, u16 version, u32 total bytes, 8 reserved, then 12-byte records
  (u32 offset, u16 format, u16 width, u16 height, u16 texture id). Format 0x10d is a single BC7 mip with
  premultiplied alpha; textures are packed back to back.
- bimage7: 'BIM\\x18' header, u32 format at 0x0c (16 = BC3, 20 = BC7), u32 width/height at 0x18,
  mip count at 0x2c, then at 0x3e a 20-byte mip record (u32 level, u32 0, u32 w, u32 h, u32 bytes) + blocks.
"""
import argparse
import hashlib
import io
import json
import re
import struct
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

from extract_arm_assets import exact, index_entries, resource_path, unpack_asset

DEFAULT_GAME = Path(r'C:\Program Files (x86)\Steam\steamapps\common\Dishonored2')
CONTAINERS = ('game1', 'game2', 'game3', 'game4')
MAX_SIDE = 2048
GUI = 'generated/image/models/guis/'

# key: (resource, Iggy texture id or None for bimage7, expected (width, height), note)
PICKS = {
    'key_art': (GUI + 'loading_screens/loading_mainmenu_is.bimage7', None, (1920, 1080),
                'Main-menu loading screen: split Corvo mask / Emily key art'),
    'logo': ('ui/main_menu.iggy', 54, (1472, 240), 'DISHONORED 2 wordmark from the title screen'),
    'portrait_corvo': ('ui/corvo_pause_menu.iggy', 101, (572, 940), 'Pause-menu portrait (Corvo mask)'),
    'portrait_emily': ('ui/emily_pause_menu.iggy', 110, (764, 844), 'Pause-menu portrait (Emily profile)'),
    'menu_background': ('ui/lib_common.iggy', 139, (552, 700), 'Dark ink panel behind menu lists'),
    'highlight_brush': ('ui/lib_widgets.iggy', 99, (164, 44),
                        'White brush-stroke list highlight. The pause-menu blade highlight was not found as a '
                        'bitmap (likely vector art in the movie)'),
    'arrow_right': ('ui/lib_widgets.iggy', 122, (24, 36), 'Selector arrow'),
    'arrow_left': ('ui/lib_widgets.iggy', 122, (24, 36), 'Horizontal mirror of arrow_right; no separate left bitmap'),
    'slider_track': ('ui/lib_widgets.iggy', 54, (364, 16), 'Rounded track bar; picked by inspection'),
    'slider_knob': ('ui/lib_widgets.iggy', 61, (60, 48), 'Lens-shaped handle; picked by inspection'),
    'divider': ('ui/global.iggy', 60, (592, 32), 'Ornamental divider with centre diamond'),
    'frame': ('ui/lib_widgets.iggy', 85, (380, 44), 'Outlined option-row frame'),
    'portrait_corvo_red': ('ui/corvo_pause_menu.iggy', 45, (572, 944), 'Red variant (game-over state)'),
    'portrait_emily_red': ('ui/emily_pause_menu.iggy', 52, (764, 844), 'Red variant (game-over state)'),
    'character_corvo': ('ui/lib_journal_charms.iggy', 122, (468, 636), 'Painted unmasked Corvo'),
    'character_emily': ('ui/lib_journal_charms.iggy', 124, (468, 636), 'Painted masked Emily'),
    'select_corvo': ('ui/main_menu.iggy', 88, (288, 596), 'Main-menu character art (Corvo mask)'),
    'select_emily': ('ui/main_menu.iggy', 91, (520, 568), 'Main-menu character art (Emily profile)'),
    'key_art_banner': ('ui/global.iggy', 57, (860, 352), 'Emily and Corvo banner'),
    'story_art': ('ui/main_menu.iggy', 160, (528, 216), 'Painted fight scene'),
    'menu_vignette': ('ui/main_menu.iggy', 19, (1920, 1080), 'Full-screen dark vignette frame'),
    'highlight_glow_bar': ('ui/lib_common.iggy', 22, (384, 180), 'Glowing white selection bar'),
    'outsider_mark': ('ui/lib_journal_powers.iggy', 159, (616, 584), "The Outsider's mark"),
    'parchment': ('ui/lib_diary.iggy', 21, (456, 684), 'Folded parchment page'),
    'sketch_corvo': ('ui/lib_diary.iggy', 24, (616, 824), 'Parchment with Corvo sketch'),
    'sketch_emily': ('ui/lib_diary.iggy', 25, (616, 824), 'Parchment with Emily sketch'),
    'dark_page': ('ui/lib_diary.iggy', 8, (612, 844), 'Dark leather book page'),
    'ink_flourish': ('ui/lib_common.iggy', 16, (1000, 408), 'Ink flourish ornament'),
    'ink_splatter': ('ui/lib_particles.iggy', 54, (800, 800), 'Blood-ink splatter'),
    'paper_texture': ('ui/lib_particles.iggy', 5, (512, 512), 'Dark paper grain overlay'),
    'glass_shard': ('ui/lib_particles.iggy', 100, (160, 160), 'Pause-menu glass shard'),
    'bonecharm': ('ui/lib_journal_charms.iggy', 144, (148, 148), 'Bonecharm bone fragments'),
    'power_glyph': ('ui/lib_journal_charms.iggy', 42, (136, 136), 'Blue power/rune glyph'),
}
MIRRORED = {'arrow_left'}
NOT_FOUND = {}  # key -> reason, for requested keys with no suitable bitmap


def norm(name):
    return name.replace('\\', '/').lower()


def find_game(arg):
    if arg: return arg
    if (DEFAULT_GAME / 'base').is_dir(): return DEFAULT_GAME
    roots = []
    try:
        import winreg
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r'Software\Valve\Steam') as key:
            roots.append(Path(winreg.QueryValueEx(key, 'SteamPath')[0]))
    except OSError: pass
    roots.append(DEFAULT_GAME.parents[2])
    for root in roots:
        vdf = root / 'steamapps' / 'libraryfolders.vdf'
        if not vdf.is_file(): continue
        for lib in re.findall(r'"path"\s+"([^"]+)"', vdf.read_text(encoding='utf-8', errors='replace')):
            game = Path(lib.replace('\\\\', '\\')) / 'steamapps' / 'common' / 'Dishonored2'
            if (game / 'base').is_dir(): return game
    raise SystemExit('Dishonored 2 install not found; pass --game PATH')


class Containers:
    """Final patch index entries for the wanted destinations, read through extract_arm_assets."""
    def __init__(self, game, wanted):
        self.base = game / 'base'; self.entries = {}
        for container in CONTAINERS:
            index = self.base / (container + '_patch.index')
            if not index.is_file(): raise SystemExit(f'Missing final patch index {index}')
            with index.open('rb') as stream:
                for e in index_entries(stream):
                    name = norm(e['destination'])
                    if name in wanted and e['size']:
                        e['container'] = container
                        self.entries.setdefault(name, {})[(container, e['resource_id'], e['offset'], e['packed'], e['size'])] = e

    def read(self, name):
        if name not in self.entries: raise ValueError(f'{name} is not in the final patch indexes')
        blobs = set()
        for e in self.entries[name].values():
            resource = resource_path(self.base, e['container'], e['resource_id'])
            if e['offset'] < 4 or e['offset'] + e['packed'] > resource.stat().st_size:
                raise ValueError(f'{name} lies outside its container')
            with resource.open('rb') as stream:
                if exact(stream, 4) != b'\x04SER': raise ValueError('Unexpected resource magic')
                stream.seek(e['offset']); blobs.add(unpack_asset(exact(stream, e['packed']), e['size']))
        if len(blobs) != 1: raise ValueError(f'Conflicting copies of {name} across containers')
        return blobs.pop()


def bc_decode(blocks, width, height, codec):
    dxgi = {'bc3': 77, 'bc7': 98}[codec]
    if width % 4 or height % 4 or len(blocks) != width * height: raise ValueError('BC block size mismatch')
    header = (b'DDS ' + struct.pack('<7I', 124, 0x1007, height, width, len(blocks), 0, 1) + b'\0' * 44 +
              struct.pack('<II4s5I', 32, 4, b'DX10', 0, 0, 0, 0, 0) + struct.pack('<5I', 0x1000, 0, 0, 0, 0) +
              struct.pack('<5I', dxgi, 3, 0, 1, 0))
    image = Image.open(io.BytesIO(header + blocks)); image.load()
    return image.convert('RGBA')


def iggy_texture(movie, raw, texture_id):
    if movie[:4] != b'Ig\n\xed': raise ValueError('Not an Iggy movie')
    count = struct.unpack_from('<I', movie, 0x1c)[0]
    if count > 64 or 0x20 + 16 * count > len(movie): raise ValueError('Bad Iggy subfile table')
    table = {}
    for i in range(count):
        ident, size, _, offset = struct.unpack_from('<4I', movie, 0x20 + 16 * i)
        table[ident] = (offset, size)
    if 8 not in table: raise ValueError('Movie has no external texture table')
    offset, size = table[8]
    if offset + size > len(movie) or size < 16 or (size - 16) % 12: raise ValueError('Bad Iggy texture table')
    total = struct.unpack_from('<I', movie, offset + 4)[0]
    if total != len(raw): raise ValueError('Iggy texture table does not match raw.iggytex size')
    records = sorted(struct.unpack_from('<IHHHH', movie, p) for p in range(offset + 16, offset + size, 12))
    bpp = {0x10d: 1, 0x100: 4}
    for (o, fmt, w, h, _), nxt in zip(records, [r[0] for r in records[1:]] + [total]):
        if fmt not in bpp or o + w * h * bpp[fmt] != nxt: raise ValueError('Iggy texture records are not contiguous')
    match = [r for r in records if r[4] == texture_id]
    if len(match) != 1: raise ValueError(f'Iggy texture {texture_id} not found')
    o, fmt, w, h, _ = match[0]
    if fmt == 0x10d: image = bc_decode(raw[o:o + w * h], w, h, 'bc7')
    else: image = Image.frombytes('RGBA', (w, h), raw[o:o + w * h * 4])
    return Image.frombytes('RGBa', image.size, image.tobytes()).convert('RGBA')  # Iggy alpha is premultiplied


def bimage(data):
    if data[:4] != b'BIM\x18' or len(data) < 0x52: raise ValueError('Not a bimage7')
    fmt = struct.unpack_from('<I', data, 0x0c)[0]
    width, height = struct.unpack_from('<II', data, 0x18)
    mips = struct.unpack_from('<I', data, 0x2c)[0]
    level, _, mw, mh, size = struct.unpack_from('<5I', data, 0x3e)
    codec = {16: 'bc3', 20: 'bc7'}.get(fmt)
    if not codec or mips != 1 or level or mw < width or mh < height or len(data) != 0x52 + size:
        raise ValueError(f'Unsupported bimage7 layout (format {fmt}, mips {mips})')
    return bc_decode(data[0x52:], mw, mh, codec).crop((0, 0, width, height))


def write_if_changed(path, data):
    if not path.exists() or path.read_bytes() != data: path.write_bytes(data)


def contact_sheet(items, path):
    font = ImageFont.load_default(); cell, label = 256, 30
    cols = 6; rows = (len(items) + cols - 1) // cols
    sheet = Image.new('RGBA', (cols * cell, rows * (cell + label)), (24, 22, 28, 255))
    draw = ImageDraw.Draw(sheet)
    for k, (key, source, image) in enumerate(items):
        x, y = (k % cols) * cell, (k // cols) * (cell + label)
        for cy in range(0, cell, 16):
            for cx in range(0, cell, 16):
                shade = 70 if (cx + cy) // 16 % 2 else 50
                draw.rectangle((x + cx, y + cy, x + cx + 15, y + cy + 15), fill=(shade, shade, shade + 10, 255))
        thumb = image.copy(); thumb.thumbnail((cell - 8, cell - 8), Image.LANCZOS)
        sheet.alpha_composite(thumb, (x + (cell - thumb.width) // 2, y + (cell - thumb.height) // 2))
        draw.text((x + 4, y + cell + 2), key, fill=(255, 220, 120, 255), font=font)
        draw.text((x + 4, y + cell + 15), source.rsplit('/', 1)[-1], fill=(190, 190, 200, 255), font=font)
    buffer = io.BytesIO(); sheet.convert('RGB').save(buffer, 'PNG'); write_if_changed(path, buffer.getvalue())


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--game', type=Path, help=f'Dishonored 2 folder (default {DEFAULT_GAME}, then Steam libraries)')
    parser.add_argument('--out', type=Path, default=Path(__file__).resolve().parents[1] / 'artifacts' / 'game-art')
    args = parser.parse_args()
    game = find_game(args.game).resolve(); out = args.out.resolve()
    if out.is_relative_to(game): raise SystemExit('Refusing to write inside the game folder')
    wanted = set()
    for resource, texture_id, _, _ in PICKS.values():
        wanted.add(resource)
        if texture_id is not None: wanted.add(resource[:-len('.iggy')] + '.raw.iggytex')
    containers = Containers(game, wanted); cache = {}

    def load(name):
        if name not in cache: cache[name] = containers.read(name)
        return cache[name]

    out.mkdir(parents=True, exist_ok=True)
    manifest = {'game': str(game), 'notice': 'Extracted from the local game install. Do not commit or redistribute.',
                'keys': {}, 'missing': dict(NOT_FOUND)}
    sheet = []
    for key, (resource, texture_id, size, note) in PICKS.items():
        try:
            if texture_id is None:
                image, source = bimage(load(resource)), resource
            else:
                raw_name = resource[:-len('.iggy')] + '.raw.iggytex'
                image = iggy_texture(load(resource), load(raw_name), texture_id)
                source = f'{raw_name}#{texture_id}'
            if image.size != size: raise ValueError(f'expected {size}, decoded {image.size}')
            if image.getextrema()[3][1] == 0: raise ValueError('fully transparent')
        except (ValueError, OSError) as error:
            manifest['missing'][key] = f'{resource}: {error}'; continue
        if key in MIRRORED: image = image.transpose(Image.FLIP_LEFT_RIGHT)
        if max(image.size) > MAX_SIDE:
            scale = MAX_SIDE / max(image.size)
            image = image.resize((max(1, round(image.width * scale)), max(1, round(image.height * scale))), Image.LANCZOS)
        buffer = io.BytesIO(); image.save(buffer, 'PNG'); png = buffer.getvalue()
        raw = struct.pack('<II', *image.size) + image.tobytes()
        write_if_changed(out / f'{key}.png', png); write_if_changed(out / f'{key}.rgba', raw)
        manifest['keys'][key] = dict(file=f'{key}.png', raw=f'{key}.rgba', source=source, width=image.width,
                                     height=image.height, sha256=hashlib.sha256(png).hexdigest(),
                                     raw_sha256=hashlib.sha256(raw).hexdigest(), note=note)
        sheet.append((key, source, image))
    contact_sheet(sheet, out / 'contact.png')
    write_if_changed(out / 'manifest.json', (json.dumps(manifest, indent=2) + '\n').encode())
    for key, entry in manifest['keys'].items():
        print(f"{key:20} {entry['width']:5}x{entry['height']:<5} {entry['source']}")
    for key, reason in manifest['missing'].items(): print(f'{key:20} MISSING {reason}')
    print(f"{len(manifest['keys'])} keys written to {out} (contact.png, manifest.json); do not commit or redistribute.")


if __name__ == '__main__':
    main()
