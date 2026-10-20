# Dishonored 2 VR Launcher

One file, `DishonoredVRLauncher.exe`. Copy it anywhere and run it: no repo, Python, PowerShell or .NET install is needed. It installs the mod into a per-user folder, imports the game's UI art from your own install, checks your setup, edits the mod's saved settings, and **LAUNCH VR** does the rest (start the game, attach VR at the title screen, continue your last save). It never sends global input and never takes focus: keys go to the game window only.

## Build

Needs the .NET 8+ SDK (`C:\Program Files\dotnet`) and the repo with a validated mod build.

    cd launcher
    dotnet publish -c Release

Output: `launcher/dist/DishonoredVRLauncher.exe`, a single self-contained win-x64 file (about 80 MB: .NET runtime, WinForms, the mod DLL, the optional FSR 4 DLLs).

- **The mod is picked at build time** the way the launcher always picked it: the highest `artifacts\repair-vNNN-test-candidate.json` that is not `launch_blocked` and whose `build\<dll_name>` exists. As `run_demo.ps1` does, the build fails if the name is not `d2vr_render_probe_arm_vNNN.dll` or the DLL's sha256 differs from the manifest. The build log prints `Embedding <dll> from <manifest>`. Rebuild the launcher after validating a new candidate.
- Embedded (resources named `home/<path>`): the DLL, its candidate manifest, its `build\<dll>.source.json` provenance pointer when present, `third_party\fsr4\bin\amd_fidelityfx_{loader,upscaler}_dx12.dll` with the AMD license, the `third_party` license files and `THIRD_PARTY_NOTICES.md`. No game art is embedded.
- Nothing is unpacked to `%TEMP%`: the .NET runtime is linked into the single-file host, WPF's native libraries (unused by WinForms) are left out, and visual styles come from `app.manifest` (Common Controls 6) because `Application.EnableVisualStyles` writes a temp file in single-file apps.

Code: `Core.cs` (checks, settings file, mod/dev root), `ModHome.cs` (installs the embedded mod), `Probe.cs` (launch, inject, stop, game keys; PE reader), `ArtImport.cs` (game art, BC1/BC3/BC7 decoders), `PoseRender.cs` (calibration avatar poses), `Ui.cs` (art loading, drawing, owner-drawn controls), `Program.cs` (the window and its pages).

## Mod home

The first start also writes the default feature knobs (`tools/default_features.json`, embedded as `features.json`) into `build\` and records them in `build\launcher-features.json` (`off`, `seeded`). A feature you turn off stays off across launches and updates, and a knob file you made by hand is kept (a `no-*` file is adopted as off). Features marked `"default": false` (power gestures) are listed but not created. In developer mode (repo root) nothing is seeded unless `--seed-features` is passed.

On start the launcher writes every embedded file that is missing or different into `%LOCALAPPDATA%\DishonoredVR` (`--home <dir>` overrides), verifies the DLL's sha256 against its manifest, and deletes older `d2vr_render_probe_arm_v*.dll` files (a version still loaded in a running game is locked and stays). The layout is the repo layout the DLL expects (`src/dxgi_probe.cpp`: root = the DLL folder's parent, and `StartProbe` refuses to run without `<root>\.git`):

    DishonoredVR\
      .git\                          empty marker the DLL requires
      build\<dll>, <dll>.source.json
      build\dxgi-probe.cfg           probe configuration, written per launch
      build\vr-settings.cfg          saved settings (Settings page and --saved-settings)
      build\<knob files>             optional developer knobs (fsr4.off, hud-scale, ...)
      artifacts\repair-vNNN-test-candidate.json
      artifacts\xr-live-status.json, probe-launch-config.json, loaded-render-probe-*.json, vr-command*.txt, ...
      artifacts\runs\<UTC stamp>-<label>\   run archives
      artifacts\game-art\            <key>.png, <key>.rgba, manifest.json (your own install's art)
      third_party\fsr4\bin\          optional FSR 4 provider (Anti-aliasing 5-6)
      third_party\..., THIRD_PARTY_NOTICES.md

**Developer mode:** when the exe sits under the repo's `launcher\` folder (for example `launcher\dist`), or the repo path is passed as the first argument, the launcher works in the repo instead: newest candidate from `artifacts\` and `build\`, outputs in the repo's `artifacts\`, and the Tools page also offers **Run self-test** (`python tools\vr_selftest.py`). Launch, stop and art import are the same C# code in both modes.

## Look and game art

The launcher is styled after the game's own menus and draws the game's real art:

- The window is dark ink with paper grain (`paper_texture`) and the menu vignette (`menu_vignette`). The `DISHONORED 2` wordmark (`logo`) sits top left.
- The navigation on the left is PLAY / SETTINGS / TOOLS in Palatino Linotype (Georgia if Palatino is missing). The selected page has the game's white brush stroke (`highlight_brush`) behind it.
- Each page has a hero picture on the right that fades into the ink. Play shows the split Corvo mask / Emily title art (`key_art`), Settings shows Emily's pause-menu portrait (`portrait_emily`), and Tools shows Corvo's mask (`portrait_corvo`).
- Buttons: the primary action (LAUNCH VR, SAVE) is a brush stroke that gets the main-menu glow bar (`highlight_glow_bar`) on hover or keyboard focus. Secondary buttons use the option-row frame (`frame`). Section headings use the gold-tinted ornamental divider (`divider`).
- Settings rows work like the game's options menu. Each value sits between the selector arrows (`arrow_left`, `arrow_right`); an arrow fades out at the end of the range. Resolution uses the game's slider (`slider_track`, `slider_knob`). A description plate under the list shows the help text for the selected setting.

**The art is not shipped.** On first start (or whenever `artifacts\game-art\manifest.json` is missing) the launcher imports it from your Dishonored 2 install in the background (about 2 s); the Game art check shows the progress. `ArtImport.cs` is a C# port of `tools/extract_game_art.py`: it reads the final patch indexes and `.resources` containers (read-only, zlib), the Iggy movies' external texture tables (`ui/*.raw.iggytex`, BC7 with premultiplied alpha, converted to straight alpha as Pillow does) and the bimage7 key art (BC3), and writes the same `<key>.png`, `<key>.rgba` (u32 width, u32 height, straight RGBA8, used by the DLL) and `manifest.json` (`keys`, `missing`). The contact sheet is not written. Never commit or redistribute the output (see [game-art.md](game-art.md)). **Import game art** re-runs it and shows the output in the Launch log.

**Calibration avatar poses.** Right after the art import (and on any start where a `pose_*.rgba` is missing) the launcher renders the four images the in-game VR settings page shows for the player-size measurement: `pose_shoulders_corvo`, `pose_tpose_corvo`, `pose_shoulders_emily`, `pose_tpose_emily` (`.png` and `.rgba`, added to `manifest.json`). The Game art check shows the progress ("rendering poses n of 6"); it takes about 1.5 s. `PoseRender.cs` is a C# port of `tools/render_calibration_poses.py` and the parts of `tools/build_player_asset_pack.py`, `tools/extract_head_assets.py` and `tools/render_body_ik.py` it needs: it reads the player body and arm meshes and skeletons (MD6), the masked NPC head (Corvo `t_str_corvo_head_mask`, Emily `emily_head01` with her scarf; hair alpha-tested) and their diffuse maps (the bimage7 mip nearest 1024 px) from the containers, poses the arms, skins them and draws the figure with the Python's lighting (warm key with a shadow map, cyan rim, fill, cheap AO, filmic tone curve, Kuwahara paint pass, Lanczos downsample, grain, transparent crop). The poses are no longer solver frames: the body stays at bind and each arm gets `src/body_ik.hpp`'s arm step for the hand targets of the `body_ik_check` scenarios (clavicle shrug, Boneworks elbow pole, soft two-bone IK, forearm twist, fists from grip and trigger; arms span-fitted to 1.55 m at eye height 1.75 m). Nothing game-derived is embedded. If the render fails the Launch log says so and the DLL shows the painted portrait instead.

**Without the art** the launcher still works with a plain dark style (text logo, flat shapes in place of the bitmaps).

**Keyboard:** Tab and Shift+Tab move between controls, and Enter or Space presses a button. In the settings list, Up and Down select a row, Left and Right change its value, and PageUp and PageDown move the slider in large steps. The mouse can click the arrows or drag the slider. Text and layout scale with the monitor's DPI (per-monitor aware). The window opens at 1280x760 logical pixels, or smaller if the screen is smaller.

## Use

**Play page**
- Status line: Starting, Load your save, VR running, Stopped, from the game process plus `artifacts\xr-live-status.json` (`gameplay`, `logic_mask`; the file counts as live if written in the last 6 s).
- **Setup checks** (with a fix hint under each red or amber item): Dishonored 2 install (Steam `libraryfolders.vdf`), active OpenXR runtime (`HKLM\SOFTWARE\Khronos\OpenXR\1`; Virtual Desktop VDXR recommended), SteamVR's `openxr_loader.dll` at `C:\Program Files (x86)\Steam\steamapps\common\SteamVR\bin\win64` (the DLL loads OpenXR from that fixed path, even with Virtual Desktop), the mod (installed and hash-verified; in developer mode the newest unblocked candidate), the Visual C++ 2015-2022 x64 runtime (the DLL imports `msvcp140*.dll`/`vcruntime140*.dll`), and the game art. LAUNCH VR needs all but the art.
- **Launch VR** (`Probe.Launch`, the C# port of `tools\run_demo.ps1 -Launch -SkipPreflightWait` + `tools\inject_probe.py` + `tools\game_keys.py`):
  1. Checks the candidate as `run_demo.ps1` does: not `launch_blocked`, `d2vr_render_probe_arm_vNNN.dll` name, DLL sha256 equals the manifest, pinned game build.
  2. If the game is closed: adds `win_pauseOnFocusLoss "0"` and `in_controlInactiveWindow "1"` to `%USERPROFILE%\Saved Games\Arkane Studios\Dishonored2\base\dishonored2Config.cfg` when absent (`game_keys.py --prepare-config`), opens `steam://rungameid/403640`, waits up to 3 minutes for `Dishonored2.exe`, then 45 s for the title screen.
  3. Requires exactly one game process, then runs the `inject_probe.py` headset session for that PID with `run_demo.ps1`'s flags (`--paired --native-eye-capture --vr-preview --head-tracking --controllers --symmetric-frustum --eye-antialiasing --native-menu-pointer --stable-camera --vr-turning snap --physical-grab --native-left-aim-experiment --controller-aim --tracked-arms --headset-size --saved-settings --until-stopped`; no headset or gameplay pre-wait): game image size and exe sha256, loaded-DLL identity (refuses a different build of the same name and an unconfirmed older probe), refuses while any loaded probe reports `ProbeStatus` active (in any folder, so a repo build and the mod home cannot run side by side), `LoadLibraryW` resolved from KernelBase by owning-module identity with live-instruction comparison, stereo-construction code and single-camera checks, `ProbeCapabilities` (required bits 61017). Writes `build\dxgi-probe.cfg`, archives and clears the previous outputs (`runs\*-previous-artifacts`), loads the DLL with `CreateRemoteThread(LoadLibraryW)`, verifies the loaded image and its `StartProbe`/`StopProbe`/`ProbeCapabilities` entry bytes, writes `artifacts\loaded-render-probe-<variant>.json`, and runs `StartProbe` until the session ends.
  4. When the launcher started the game, 10 s after `StartProbe` it posts Enter three times (1.5 s apart) to the game's own window (title, Campaign, Continue), as `tools\vr_selftest.py --launch --inject-at-title` does; it stops early once `xr-live-status.json` reports gameplay. Keys use `PostMessage` `WM_KEYDOWN`/`WM_KEYUP` to the visible "Dishonored" window owned by the game process only.
  5. At the end: `artifacts\probe-launch-config.json` (schema `D2VR-probe-launch-config-v1`, same fields as `inject_probe.py`; `launcher_argv[0]` is the exe) and the run archive `artifacts\runs\<stamp>-pid<pid>-arm-vNNN[-failed]` (with the build-source provenance when the pointer, source manifest and zip all verify, which in standalone mode they do not, as the zip is not shipped).
- **Stop VR**: `inject_probe.py --stop` ported: same identity checks, then `StopProbe` in a remote thread of the exact loaded DLL.
- Closing the launcher during a session leaves VR running (reopen it to stop VR), but that session's launch record and run archive are not written.

**Settings page** edits `build\vr-settings.cfg` in the mod home (written as format version 7, atomically, the same file `--saved-settings` loads). Values are range-checked as in `src/vr_preferences.hpp`. Stored player measurements are kept, and an unreadable file is left untouched. Changes apply the next time VR starts. A loaded value that is not one of the listed choices is shown as an extra choice. Snap angle and Smooth speed are dimmed when the selected turning mode does not use them.
- Display: resolution 0.5-2.0. Anti-aliasing: 0 off, 1 game TAA, 2-4 FSR 3 native/quality/balanced, 5-6 FSR 4 native/quality. Sharpening off to max. HUD 55/65/75/85/100 %. Monitor view right/left/both eye. Controls hint on/off.
- Turning: native/snap/smooth (snap 30/45/90 deg, smooth 45/90/120 deg/s).

**Tools page**: writes `artifacts\vr-command.txt` (`unlock`, `cvar ark_god_player 1|0`, `uidump on|off`, or any custom line) and shows the tail of `artifacts\vr-command-result.txt`. It opens the artifacts, runs and mod folders. **Run self-test** (developer mode only) runs `python tools\vr_selftest.py --dll <dll> --scenario all` and counts PASS/FAIL lines.

## Review screenshots and checks

    DishonoredVRLauncher.exe --page settings --screenshot shots\settings.png --exit

- `--page play|settings|tools` opens that page; `--screenshot <png>` shows the window **without activating it**, waits for the install, checks, settings and a first-start art import, captures the client area with `PrintWindow` and saves a PNG; `--exit` closes afterwards; `--no-art` previews the plain style; `--home <dir>` uses another mod home.
- `--check-art <reference game-art folder> [--out <dir>]`: headless; extracts into a temporary folder and compares every `.rgba` with a `tools/extract_game_art.py` output (exit 0 when all match within 1 LSB).
- `--render-poses <dir> [--exit]`: headless; renders the four calibration poses into `<dir>` and writes `poses_sheet.png` (the four side by side, as `build\tmpcheck\poses_sheet.png`) for a visual comparison.
- `--check-probe <probe-launch-config.json> [--dll <dll>]`: headless, no game access; compares the probe configuration and launch record with one `inject_probe.py` wrote on the headset path and prints the DLL's export RVAs.

No input is injected; nothing is launched. From Git Bash, pipe through `| cat` (or use PowerShell `Start-Process -Wait`) so the shell waits for the GUI process.

## Troubleshooting

- Mod red with "access denied": the mod home is not writable (or a different file of the same name is locked by a running game). Close the game and Re-check, or use `--home`.
- SteamVR red: install SteamVR from Steam. A Steam library outside `C:\Program Files (x86)\Steam` is not supported by the current DLL (fixed loader path).
- VC++ runtime red: install the Microsoft Visual C++ 2015-2022 Redistributable (x64).
- Launch log "PID is not the supported Dishonored 2 image" or "Unsupported executable hash": the game build differs from the pinned Steam build.
- "A diagnostic is still active" or "Loaded diagnostic identity differs": stop the running session or restart the game.
- Stuck on "Starting": the game must reach a loaded save. If the Enter keys did not reach the title screen (window not found, or the game still loading), continue your save on the desktop; VR starts once gameplay begins.
- Plain look: the art import failed (see the Launch log) or Dishonored 2 was not found. Use **Import game art** after fixing it.

## Verification status (13 October 2026, v368)

- Verified: single exe (80.5 MB) copied alone into an empty folder outside the repo starts, installs v368 into `%LOCALAPPDATA%\DishonoredVR` (DLL sha256 equals the manifest; a previous v364 DLL was removed), imports 33 art keys on first start and shows all checks green; Settings and Tools pages render (self-test hidden). No files appear in the bundle extraction folder (`DOTNET_BUNDLE_EXTRACT_BASE_DIR` stayed empty).
- Verified: `--check-art` against the repo's Python output: all 33 `.rgba` files byte-identical (32 BC7 Iggy textures, the BC3 key art), extraction in about 1.4 s. `--check-probe` against a real v361 headset record: probe configuration, its sha256 and all 17 launch-record fields equal; v368 export RVAs equal `tools/probe_game.py`.
- Verified (v369 build): the calibration poses from the single exe run outside the repo: `--render-poses` 1.5 s for all four; sizes within 1 px of the Python renders (288x836, 707x836, 278x837, 672x788), mean colour within 1.5 %, silhouette overlap 0.95-0.97 (the solver's slight knee bend and chest tilt are not reproduced); compared visually against `build\tmpcheck\poses_sheet.png`. A fresh `--home` first start installs, imports 33 keys and renders the 4 poses (manifest 37 keys); deleting one pose file re-renders on the next start, and the output is byte-identical between runs. `--check-art` still byte-identical after the BC decoder went parallel.
- Not verified: the poses in the headset's settings page; an actual launch, injection, title-screen keys, Stop VR and the run archive against the live game (deliberately not run); headset visuals; BC1's 3-colour/transparent mode (no game texture uses it); hover/focus states and other DPI scales.
- In this tool environment, executables run from inside the repo are sandboxed (they cannot write `%LOCALAPPDATA%` or `%TEMP%` and see few processes). That is the environment, not the launcher; run review copies from outside the repo.
