> **Back since PR #7 (merged 2026-11-25), as an open rebuild.** v1443 removed PureDark's closed-plugin AFW. Indurhra's PR #7 brings AFW
> back as VR Stereo Hub's open-source held-eye rebuild, compiled into the mod (no plugin). Spacewarp still comes first: with Spacewarp on
> (the launcher default) every session renders native stereo. Performance mode (launcher Settings, or the VR menu's Renderer row) offers
> **Off / On (built-in warp) / On (AFW)**. The PureDark-era sections below are kept for history.

# AFW renderer, PSVR2 triggers and the ImGui VR menu

Ported from the Indurhra DLSS/AFW fork (`dishonored-2-dlss-afw`, docs `DLSS-AFW-IMPLEMENTATION.md`, `PSVR2-TRIGGERS.md`) onto this
codebase. Tuning follows `AFW_RENDERING_GUIDE.md` (the user's AFW guide; concepts only, nothing copied from engines it describes).

## Renderer row (VR menu)

Open the VR menu with the **right grip at your right temple** in gameplay: page 1 is VR Settings, the right stick (left / right) pages on to the Body, Controllers and Gestures guide, the same gesture closes it. Holding left Y opens the game's pause menu (upstream behaviour). Pause > VR Settings is the game's own options page.
Row 13 of page 1, **Renderer**, has three choices (saved in `build\vr-settings.cfg`, file version 12):

| Choice | What renders |
|---|---|
| Native stereo | both eyes every frame |
| Alternate eye (built-in warp) | one eye per frame; the other eye's last image is re-projected (`stale_eye_warp.hpp`) |
| AFW (alternate frame warping) | one eye per frame; the other eye is **rebuilt** from the fresh eye and its own last image (VR Stereo Hub's open AFW, compiled in) |

Choose with the stick, then press **A** (or point at the green **A: APPLY** chip): every switch is live, no restart. Sessions start
alternate-capable, and Native simply renders both eyes every frame (as menus already did). If alternate eye could not start (its
prerequisites failed) the row shows **unavailable** for Alternate/AFW. `build\native-classic` restores upstream's paired-only start for
Native. Any AFW refusal falls back to the built-in warp for that frame; a hard failure (plugin missing, ABI mismatch, eye-size change)
falls back for the session, and the description plate shows the reason.

Requirements:
- **Spacewarp off** (launcher Settings). Spacewarp is the default and wins: one frame generation at a time, so with it on, sessions stay
  in native stereo and the Renderer help says so.
- An **FSR or DLSS anti-aliasing mode**. AFW reads the fresh eye's depth copy that these modes keep. Without one, every rebuild is
  refused and the built-in warp draws the held eye.

AFW is VR Stereo Hub's open-source held-eye rebuild from their Dishonored 1 VR mod
(`third_party/vsh_afw`, zlib license, modified; credited in `THIRD_PARTY_NOTICES.md`), compiled into the mod DLL. The closed PureDark
plugin is no longer used.

## Integration fixes (2026-11-25, on top of PR #7)

- **Each eye's own FOV.** VSH projected every image with one symmetric pair, tan(right) and tan(up). The shader now takes each image's
  own tangents (left, right, up, down): `tnH` for the held eye's last image and its target, `tnF` for the fresh eye (`afw_warp.cpp`
  `rayA`/`toA`; `warp_held(..., tanHeld, tanFresh)`). With the launcher's settings this changes nothing: every eye renders a symmetric
  frustum that encloses the headset's own FOV. The sim confirmed this with the Quest 3 FOV via `build\sim-fov`: rendered L/R +-0.942,
  U/D +-0.960, AFW 7,314 rebuilds, 0 refused. It matters only in sessions without that symmetric frustum (asymmetric renders).
- **No stale held image after a pause.** VSH's records had no age limit and were only reset once, at start. A capture gap of more than 4
  alternate frames (AFW switched off and on, a menu, a load, paired frames) now drops them (`set_enabled` epoch). The next rebuild uses
  the fresh eye alone until the held eye renders again. Counter: status `alternate_eye.afw.resets`.
- **GPU timing.** The rebuild has its own span `afw_rebuild` and the fresh-eye capture `afw_capture`; the PR timed the rebuild as
  `aa_sharpen`, the sharpen pass's span (`native-gpu-timing` knob, status `gpu_passes.mod_gpu_ms_p50`).
- **Spacewarp first.** The PR's "AFW wins over Spacewarp" commit (b63b9ca) is undone, as above.
- **No pause on the first switch.** AFW compiled its shaders on the render thread at its first rebuild: a ~2 s pause (Wine).
  The built-in warp's AFW2 pass compiled 13 shaders the first time a session used it: a 7 s pause when switching from AFW. Both
  now load from the mod's shader cache (`shader-cache\`), filled on the menu-time warm thread (`afw::warm_cache`,
  `Afw2Pass::warm_cache`). Sims with live switching, starting on AFW: D2 8,383 and DOTO 10,171 one-eye frames, no pause, 0 refused.
- **Smaller fixes.** The status text reads "off" once AFW is switched off. `afw tune` lists this backend's keys. The compile-census
  module name buffer was too small. The NGX log uses `_wfopen_s` (clang `-Werror`). The sampler skips forwarded exports again.

## Measured (2026-11-25, Linux simulator; no headset)

Same-frame A/B: on every capture the review build ran both warps on the same inputs and scored each against that eye's next real frame.
Five runs of 140 frame sets each, 640x640 eyes, FSR 3 Native. `tools/linux/scenarios/afw_ab.py` and `afw_ab_pistol.py` were the
scenarios. Error in the parts of the image that moved (luma 0-255, lower is better):

| Motion | AFW | Built-in warp | No warp |
|---|---|---|---|
| Head sway | 14.5 | 15.1 | 21.3 |
| Both arms sweeping, pistol held, slow | 22.0 | 25.3 | 32.0 |
| Both arms sweeping, pistol held, fast | 24.7 | 27.8 | 34.5 |
| Head flick | 8.8 | 8.7 | 11.2 |
| Walking | 14.4 | 15.0 | 15.4 |

- **Moving arms.** The built-in warp breaks a moving sleeve into see-through dots, about 5x more stray dots than AFW. AFW keeps the arm
  solid but leaves blocky light wedges at the trailing edge.
- **Head flicks:** a tie.
- **Walking:** both warps put the hands where the game does not draw them during the walk bob. Around the hands this is worse than
  showing the old frame unchanged.
- **Image detail.** AFW is about 10% softer than the real frame in head motion. The built-in warp adds about 5-10% speckle.
- **GPU** (laptop RTX, sim size): AFW 0.55 ms rebuild + 0.08 ms capture, built-in warp 1.14 ms. Either alternate-eye mode took about
  7.7 ms per frame, native stereo 9.5 ms.
- **Depth.** With `world-hand-depth` on (the default) the arms and a held pistol render at world depth: no pixel was nearer than 0.12 m.
  So `rigdepth` auto is 1 and the x5/x9 player-model path is off. That path is only for sessions where world depth is not active.

## How AFW is wired

- `third_party/vsh_afw/core/gfx/afw_warp.cpp`: the rebuild (D3D11). Per held-eye pixel it takes the hands and weapon from the fresh eye
  (reprojected by depth), the world from the held eye's own last image carried by the camera change, and the fresh eye's world where
  the held one moved or was uncovered (the stale test), with a disocclusion fill. Small adapters stand in for VSH's logging, depth
  probe and clarity headers.
- `src/afw_vsh.hpp` (the Dishonored 2 adapter): per captured fresh eye, one compute pass turns the R32 reverse-Z device depth into
  linear metres and writes the foreground mask; the eye pose and grip poses come from the stale warp's `WarpCamera`. No engine matrices.
- `src/game_xr.hpp`: at the held-eye seam (`v792`), AFW is tried first; on refusal the built-in warp draws that frame.

### Player-model depth (the part that made it work in D2)

Dishonored 2 compresses the depth of the player's models (`r_playerDepthHack`) so flat-screen arms never clip walls. The mod's cvar
override only changes the cache; loading a player restores the models' value, so the arms keep compressed depth. Measured from the
stereo disparity of still hands in the simulator (`afw capture` with depth dumps): world pixels need a factor 1.00, the free hand and
arms 5.04 (IQR 5.00-5.07), the held sword and **the hand that holds it** 9.0 (IQR 8.96-9.10). Device depth alone cannot tell the two
classes apart, so per pixel the adapter takes the class whose point lands nearer one of this render's grips or palms. Held items are
not in the rig mask; device depth above 0.04 (a world surface reaches that only within 25 cm) marks them. When the native
player-depth path (`build\world-hand-depth`) has verifiably set the models to world depth, the factors drop to 1 automatically.

## Tuning

Console (`artifacts\vr-command.txt`): `afw status`, `afw on|off`, `afw capture N`, `afw tune <key> <value>` (also one `key value` per
line in `build\afw-tune`). `renderer 0|1|2` switches the live renderer the way the VR menu's Apply does (native, built-in warp, AFW;
not saved), for sims (`tools/linux/scenarios/renderer_switch.py`). `afw status` and the status file (`alternate_eye.afw`) report
rebuilt / refused / fallbacks / resets, the depth factors in use (`rigdepth`, `near`) and the last problem:

| Key | Default | Meaning |
|---|---|---|
| `mask` | 1 | the foreground (arms, body, held items) from the rig mask + near range; 0 = nearer than `fg` metres |
| `rigdepth` | auto (5) | arms' depth compression; setting it pins it |
| `helddepth` | 9 | held items' (and the holding hand's) compression |
| `near` | 0.04 | device depth above which a non-rig pixel is a held item (0 = rig mask only) |
| `stillmm`, `stilldeg` | 3, 1 | hands moving less than this (head-relative, per two frames) count as still: the held eye keeps its own shading |
| `stale` | 0.015 | the stale test's relative depth tolerance |
| `nearmiss` | 6 | texels: a near-miss candidate beats the fill |
| `fg`, `body` | 0.75, 0.45 | metres, used only with `mask 0` |
| `heldhands`, `edgehands`, `stillshade`, `ownhands` | VSH defaults | VSH switches (-1 keeps the default) |
| `debug` | 0 | tints the held eye by source: green fresh hands, orange held own hands, blue fresh world, red last resort, magenta fill |

Knobs (files in `build\`): `afw` (start with AFW, for development runs without saved settings), `alternate-eye`, `menu-gdi` (old
GDI panel), `no-psvr2-triggers`.

## PSVR2 adaptive triggers

`src/psvr2_triggers.hpp` publishes the wanted left-trigger profile (pistol wall, crossbow wall, power vibration while pulled) into a
250 ms shared-memory lease; `DVR_psvr2\dvr_psvr2_bridge.exe` (source `third_party/psvr2/Bridge.cpp`, PSVR2Toolkit, MIT) applies it.
Only when the OpenXR runtime is SteamVR **and** the headset is a PlayStation VR2: other headsets never start the helper. The item comes
from the left hand's native equip name. `psvr2 status` prints runtime/headset, helper state and the current profile.

## ImGui VR menu

`src/vr_menu_imgui.hpp` draws the main settings page with Dear ImGui 1.92.8 (`third_party/imgui`, MIT) into the existing 640x560 panel
texture, at the same row rectangles, so pointer and stick navigation are unchanged. Calibration pages keep the GDI image. Preview
without the game: `tools\build-vr-menu-preview.cmd` → `artifacts\vr-menu-preview-*.png`.

## Build

`tools\build-dll-fast.cmd <name>.dll` builds MinHook, ImGui and the mod DLL only (no check suite, warnings not fatal: this toolset
reports 9 pre-existing upstream warnings under `/WX`). The full `tools\build.ps1` also needs the gitignored SDK binaries
(`third_party\dlss\lib\nvsdk_ngx_d.lib` 310.9.16, `third_party\fidelityfx\libs\*.lib` from Luma Framework) and currently stops at the
upstream `tests\note_tablet_check.cpp:45` assertion.

## AFW capture and the tuning record (2026-11-20, simulator)

`afw capture N` saves, per rebuilt held eye: `-rebuilt`, `-native` (the same eye's next native frame, one alternation later: the
reference, as on the Dishonored 1 AFW sheet), `-held` (what the held eye shows without AFW), `-fresh`, and the fresh eye's
`-freshdepth` (R32) and `-freshrig` (R8). `python tools/afw_capture_sheet.py <dir> <out.png> [--crop hand]` scores hand-region error
(MAE 0-255) and ghost fraction (pixels off by more than 48) for AFW and for the held frame. Runs: `vr_selftest.py ... --every
"afw capture 12"` (about 200 pairs per scenario). Pairs whose held-frame hand error is 15 or more are "motion".

Ratios are AFW / held frame (lower is better; 1.00 = as good as holding the old frame):

| Scenario | still hands | hands in motion | ghosting in motion | whole image |
|---|---|---|---|---|
| head sway | 0.84 | 0.65 | 0.55 | **0.77** |
| run (sword held) | 1.00 | 0.86 | 0.81 | **0.92** |
| walk | 0.97 | - | - | **0.95** |
| sword swing (two runs) | 1.00 / 1.00 | 0.93 / 0.91 | 0.90 / 0.87 | **0.98 / 0.97** |
| flick | 1.01 | - | - | **1.00** |

How it got there (swing, whole image): depth threshold only 1.30 -> rig mask 1.13 -> still-hands report 1.11 -> arms x5 ~1.15 (motion
0.65 in its best run) -> held sword x9 and per-pixel class by grip distance 0.97-0.98 with still hands exactly 1.00. Fast swings have a
floor: the reference is one tick later than the fresh eye, and the rebuild shows the fresh eye's pose (no extrapolation). A simulator is
not a headset: comfort, latency and the real compositor are untested.

## History in Hollow's line (PureDark bridge era, kept for reference)

## Merged into this codebase (2026-11-19, branch `feat/indurhra-afw-psvr2-imgui`, build v1411; not installed)

Cherry-picked on top of v1410 (the v1407 line plus the opt-in experiments) with the author kept: `c57a06f`, `ae5932f`, `cb6265e`,
plus merge fixes in `8230061`. Conflicts were small: teardown lines, the lab build list (ours kept, their ImGui link line taken) and a
status line (both field sets kept). Two merge-level fixes: `vr_menu_imgui.hpp`'s row helper takes `std::wstring_view` (our
`temporal_aa_text()` returns `std::wstring`), and three `vr_settings_check` assertions expect preferences version 12 (the fork's
own test still expected 11). Build: `build/compile-night2.cmd <ver>` (links Dear ImGui and d3d12.lib); PSVR2 helper:
`build/night/psvr2-bridge.cmd` -> `build/night/dvr_psvr2_bridge.exe`.

Sim (2026-11-19 12:10-12:30, release config, 2621x2822, DLSS Quality): no exceptions and a clean session end, but **v1411 is
~3.7 ms slower than v1407** in native stereo (p50 13.2-13.7 vs 9.50 ms; the same machine and save, v1407 re-measured right after).
`build/native-classic` does not change it (13.4 ms). The extra time is the engine's own render work (the render job graph 46 -> 64 %
of the main thread); GPU passes that grew: UPDATE_NPC_DECALS 0.10 -> 1.61 ms, IGGY_FULLSCREEN 0.28 -> 1.11 ms, DEPTHPASS 1.50 -> 2.07 ms,
SHADOWS_WORLD 0.17 -> 0.65 ms. **Resolved in v1412:** the cause was the merged "sessions start alternate-capable": Native then renders
both eyes through the mono-view path, without the paired shadow share or second-view skips. The comparison was also confounded:
the launcher's performance mode (`build/alternate-eye`) had been switched on at 10:28, so the v1407 "baseline" ran in performance mode.
v1412 starts Native paired again; a session goes alternate-capable only with `build/alternate-eye`, a saved Alternate/AFW renderer, or
`build/renderer-live`. Same scene, same hour: paired v1407 11.86 / v1412 11.92 ms p50; performance mode v1407 9.50 / v1412 9.55 ms.
(The scene is the user's 10:36 save, heavier than the morning's, hence the higher paired numbers.) Live switching between Native and
Alternate/AFW in one session now needs `build/renderer-live` or a start in performance mode.

Not testable here: `PDAFWPlugin.dll` is not on this PC (the AFW row reports it missing and falls back to the built-in warp), and PSVR2
triggers need SteamVR with a PlayStation VR2 plus `Dishonored2\DVR_psvr2\dvr_psvr2_bridge.exe` and PSVR2Toolkit's
`psvr2_toolkit_capi_loader.dll` next to it (the launcher does not install them).

## Built-in warp vs PureDark AFW (2026-11-19, simulator)

Setup: v1414 (the built-in warp now feeds the same `afw capture` triples), the user's current save, performance mode, DLSS Quality,
scenario `head_turn_walk` (walking with the head yawing +-40 degrees), 10 triples per warp captured 32-56 s into gameplay; the
plugin is PDAFWPlugin.dll SHA256 48D5849F...E5D5 from Indurhra's v0.7.0 release (the user's own test copy, `build/afw-plugin/`, not
committed). Scored with `tools/afw_compare.py` (rebuilt eye vs the eye's next native frame; "held" = no warp):

| | rebuilt / held, whole eye (median) | hand box | hand ghosting | frame time p50 / p90 |
|---|---|---|---|---|
| built-in warp | 0.81 (0.13-1.00) | 0.82 | 0.74 | 9.50 / 11.41 ms |
| PureDark AFW | 0.83 (0.24-1.00) | 0.82 | 0.71 | 9.60 / 11.78 ms |

A tie within the noise: both remove about a fifth of the no-warp error; ours is a little cheaper. Sheets: `docs/afw-compare/`. The
scenario moves only the head and the walk; fast hand and weapon motion, NPC motion and disocclusions (where the designs differ) are
not exercised, and three of ten captures per warp were up to two alternations apart (both warps ~1.00 there). Judge in the headset
with the VR menu's Renderer row (both are live in a performance-mode session).

