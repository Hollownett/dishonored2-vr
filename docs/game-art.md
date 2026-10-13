# Game art cache

`tools/extract_game_art.py` copies real Dishonored 2 UI art out of the user's own game install into
`artifacts/game-art/`, so the launcher and the in-game VR settings page can use the game's art instead of
hand-drawn imitations.

**The art belongs to Arkane/Bethesda.** The tool only reads the user's local install at runtime. Never commit,
package, upload or redistribute anything in `artifacts/game-art/`. `artifacts/` is git-ignored. Builds and
releases must ship the tool, not its output.

## Usage

```
python tools/extract_game_art.py                 # default Steam path, then Steam libraryfolders.vdf
python tools/extract_game_art.py --game "D:\SteamLibrary\steamapps\common\Dishonored2"
python tools/extract_game_art.py --out some\other\cache
```

Requires Python 3.9+ and Pillow. The tool only reads the game files. It does not need the game running,
and it refuses to write inside the game folder. The output is deterministic, and files are rewritten only
when their bytes change. Any asset that does not decode cleanly is refused and listed under `missing` in
the manifest. Such failures include a wrong size, non-contiguous texture tables, an unknown format or a
fully transparent result.

Output for each key:

- `<key>.png`: straight (non-premultiplied) RGBA.
- `<key>.rgba`: little-endian `u32 width`, `u32 height`, then `width*height*4` bytes of straight RGBA8, top
  row first. Use it from the DLL, which has no image codec.
- `manifest.json`: `keys.<key>` = `file`, `raw`, `source`, `width`, `height`, `sha256` (of the PNG),
  `raw_sha256`, `note`. `missing` maps keys to reasons.
- `contact.png`: a labelled thumbnail sheet for reviewing the picks.

Images are capped at 2048 px on the longest side, using a Lanczos downscale. No current pick is that large.

## Keys

`source` is either `ui/<movie>.raw.iggytex#<texture id>` or a bimage7 resource path.

| Key | Source | Size | Notes |
|---|---|---|---|
| key_art | models/guis/loading_screens/loading_mainmenu_is.bimage7 | 1920x1080 | Split Corvo mask / Emily key art |
| logo | ui/main_menu #54 | 1472x240 | DISHONORED 2 wordmark |
| portrait_corvo | ui/corvo_pause_menu #101 | 572x940 | Pause-menu portrait |
| portrait_emily | ui/emily_pause_menu #110 | 764x844 | Pause-menu portrait |
| menu_background | ui/lib_common #139 | 552x700 | Dark ink panel behind menu lists |
| highlight_brush | ui/lib_widgets #99 | 164x44 | White brush-stroke highlight (see caveats) |
| arrow_right | ui/lib_widgets #122 | 24x36 | Selector arrow |
| arrow_left | ui/lib_widgets #122, mirrored | 24x36 | No separate left bitmap exists |
| slider_track | ui/lib_widgets #54 | 364x16 | Picked by inspection |
| slider_knob | ui/lib_widgets #61 | 60x48 | Picked by inspection |
| divider | ui/global #60 | 592x32 | Ornamental divider |
| frame | ui/lib_widgets #85 | 380x44 | Outlined row frame |
| portrait_corvo_red, portrait_emily_red | pause menus #45, #52 | 572x944, 764x844 | Game-over variants |
| character_corvo, character_emily | ui/lib_journal_charms #122, #124 | 468x636 | Painted character portraits |
| select_corvo, select_emily | ui/main_menu #88, #91 | 288x596, 520x568 | Main-menu character art |
| key_art_banner | ui/global #57 | 860x352 | Emily and Corvo banner |
| story_art | ui/main_menu #160 | 528x216 | Painted fight scene |
| menu_vignette | ui/main_menu #19 | 1920x1080 | Full-screen dark vignette |
| highlight_glow_bar | ui/lib_common #22 | 384x180 | Glowing selection bar |
| outsider_mark | ui/lib_journal_powers #159 | 616x584 | The Outsider's mark |
| parchment, dark_page | ui/lib_diary #21, #8 | 456x684, 612x844 | Paper and dark book page |
| sketch_corvo, sketch_emily | ui/lib_diary #24, #25 | 616x824 | Parchment sketches |
| ink_flourish, ink_splatter | ui/lib_common #16, ui/lib_particles #54 | 1000x408, 800x800 | Ink ornaments |
| paper_texture, glass_shard | ui/lib_particles #5, #100 | 512x512, 160x160 | Overlay grain and pause-menu shard |
| bonecharm, power_glyph | ui/lib_journal_charms #144, #42 | 148x148, 136x136 | Icons |

Caveats:

- The blade-shaped selection highlight in the pause menu was not found as a bitmap. It is probably vector
  art inside the movie.
- The slider parts were chosen by eye from `lib_widgets`, not traced through the movie's symbols.

## Formats

These were verified on the pinned Steam build.

- **Containers.** The tool reads the final `base/gameN_patch.index` files with the readers in
  `tools/extract_arm_assets.py` (based on [disrev](https://github.com/chipolux/disrev/blob/main/resources.py)).
- **Iggy movies (`ui/<movie>.iggy`).**
  - The header is `Ig\n\xed`, with the subfile count at 0x1c and 16-byte records `(id, size, size, offset)`.
  - Subfile 8 is the texture table for `ui/<movie>.raw.iggytex`. It holds `u16 tag, u16 version, u32 total`
    and 8 reserved bytes, then 12-byte records `(u32 offset, u16 format, u16 w, u16 h, u16 id)`.
  - Format 0x10d is a single BC7 mip and 0x100 is raw RGBA8. Records are packed back to back.
  - Alpha is premultiplied, and the tool un-premultiplies it.
- **bimage7.**
  - The header is `BIM\x18`: format at 0x0c (16 = BC3, 20 = BC7), width and height at 0x18, mip count at 0x2c.
  - A 20-byte mip record `(level, 0, w, h, bytes)` at 0x3e is followed by the blocks. Rows may be padded
    to 4, so the tool crops to the header size.
  - Large textures keep their top mips in separate `.bimage7_mipN` entries. The tool reads only
    single-file GUI images.

To add a key, add an entry to `PICKS` with its resource, texture ID and expected size. Use a scratch dump
of the movie's texture table to find candidates. The expected size guards against a different game build
remapping texture IDs.
