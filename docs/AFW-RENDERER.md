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
| AFW (PureDark reconstruction) | one eye per frame; the other eye is **rebuilt** from the fresh eye with `PDAFWPlugin.dll` |

Choose with the stick, then press **A** (or point at the green **A: APPLY** chip): every switch is live, no restart. Sessions start
alternate-capable, and Native simply renders both eyes every frame (as menus already did). If alternate eye could not start (its
prerequisites failed) the row shows **unavailable** for Alternate/AFW. `build
ative-classic` restores upstream's paired-only start for
Native. Any AFW refusal falls back to the built-in warp for that frame; a hard failure (plugin missing, ABI mismatch, eye-size change)
falls back for the session, and the description plate shows the reason.

Requirements: `PDAFWPlugin.dll` (the user's own D3D12 build, SHA256 `48D5849F…E5D5` verified) next to `Dishonored2.exe`, and an
**FSR or DLSS anti-aliasing mode** (AFW reads the per-eye depth and motion copies those modes make). Nothing is redistributed.

## How AFW is wired

- `src/afw_puredark.hpp`: ABI subset of PureDark's plugin (size-checked), a same-adapter D3D12 device/queue, shared D3D11/D3D12
  textures and fences, three command slots (no CPU waits), a compute adapter that crops the render rectangle of depth (R32) and motion
  (RG16F) to the output size, and the camera matrices (view from the stale-warp `WarpCamera` basis, projection from the OpenXR FOV
  tangents with the engine's reverse-Z `m10/m11`).
- `src/fsr_taa.hpp`: `keep_motion` keeps a per-eye copy of the motion vectors the FSR/DLSS dispatch consumed (the engine reuses its
  target for the next eye), plus the raster jitter.
- `src/game_xr.hpp`: at the held-eye seam (`v792` stale-eye warp), AFW is tried first: fresh colour + depth + motion + fresh camera
  (src) and the held eye's camera at this Setup (dest). The held eye's layer carries the dest view, as the built-in warp's does.

## Tuning

Console (`artifacts\vr-command.txt`): `afw status`, `afw on|off`, `afw tune <key> <value>`:

| Key | Default | Meaning |
|---|---|---|
| `mv` | **1.0** | motion-vector multiplier. The guide predicts 2.0 under alternation; the simulator A/B showed 2.0 cracks the hands and arm (background through them), 1.0 keeps them solid. |
| `halo` | **2** | depth/motion dilation (source texels) toward the nearest surface, so hand/weapon silhouettes warp with them. 0 left background-coloured cracks along the sleeve and fingers. |
| `threshold` | 2.5 | per-object motion (px) the plugin ignores (10 made no visible difference) |
| `jitter` | 0 | fraction of the raster jitter removed from depth/motion sampling (1 was no better) |
| `gap` | 1 | fresh frames further apart than this reset the plugin history |
| `reset` | 1 | clear history on a gap |

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

## AFW capture and the tuning record (2026-11-11, simulator)

`afw capture N` saves N triples to `artifacts\`: `afw-capture-NN-rebuilt` (the rebuilt held eye), `-native` (the same eye's next native
frame, one alternation later: the reference, as on the Dishonored 1 AFW sheet) and `-held` (the eye's previous native frame: what
the held eye shows without AFW). `python tools/afw_capture_sheet.py <dir> <out.png> [--crop hand]` draws rebuilt | next native |
difference and writes hand-region error (MAE 0-255) and ghost fraction (pixels off by more than 48) for AFW and for the held frame.
Run with `vr_selftest.py ... --every "afw capture 2"` (about 50 triples per walk run).

Scene: Edge of the World balcony save, walk scenario, FSR 3 Native, simulated headset. Ratios are AFW / held (lower is better):

| Setting | static hand error | motion hand error | motion ghost |
|---|---|---|---|
| mv 2, halo 0 | 1.16 | 0.78 | 0.74 |
| mv 1, halo 0 | 1.14 | 0.87 | 0.87 |
| mv 1, halo 1 | 1.12 | 0.94 | 0.99 |
| **mv 1, halo 2** | 1.13 | **0.77** | **0.70** |

mv 2 scored similarly in ratio but its absolute hand error in motion was higher (24.0 vs 19.8) and the arm cracked visibly; halo 2
removed the edge cracks. Static pairs are about one point worse than the held frame: that is frame-to-frame FSR jitter noise (no colour
bias, no pixel offset, equal detail), not reconstruction error. Remaining known artefact: the first frames after a history reset (load
fade-in) can show a doubled weapon until the plugin's history fills. A simulator is not a headset: comfort, latency and the real
compositor are untested.
