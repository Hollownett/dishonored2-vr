# Dishonored 2 VR on Linux (Proton + SteamVR + Steam Link)

## 1. What this is

Two pieces:

- **The proxy** (`dinput8.dll`, built by `linux/proxy/build.py`). It sits in the game folder, loads the mod DLL inside the game process, and starts the VR probe once the game is ready. It logs to `<mod home>/artifacts/linux-proxy.log`. If anything fails it logs and idles, and the vanilla game keeps running.
- **The CLI** (`linux/d2vr-linux`, Python 3, stdlib only). It installs and removes the proxy and the mod home, checks the setup (`doctor`), prints the Steam launch options, and sends start/stop requests to the running game.

Windows is unaffected: nothing under `src/`, `launcher/` or `tools/` changed.

> **Status (2026-10-18): not yet tested in a headset.** The proxy and CLI pass their automated tests on Linux (unit tests, and a
> Wine-hosted check with a stand-in game and a fake mod). A real Dishonored 2 + Proton + SteamVR + Steam Link session has
> **not** been run yet. Use the checklist in section 8 for the first run and report what you see.

Out of scope for this version: FSR 4, Virtual Desktop, WiVRn/ALVR, flatpak Steam, and the WinForms launcher under Wine.

## 2. Requirements

- Fedora with native (non-flatpak) Steam.
- SteamVR on the beta branch, version 2.13 or later (Steam Link VR on Linux).
- A Quest with the Steam Link app.
- Proton Experimental. If it misbehaves, fall back to Proton 10.0. Do not use GE-Proton for the first run.
- Never set `PROTON_USE_WINED3D`.
- An NVIDIA GPU (hybrid laptops are fine). `protontricks` for the runtime DLLs.

## 3. One-time setup

1. Build the toolchain and the proxy, from the repo root:

   ```
   tools/linux/setup-toolchain.sh
   python3 linux/proxy/build.py
   ```

   The result is `build/linux-proxy/dinput8.dll`.
2. In Steam, set Dishonored 2 to use Proton Experimental and run it once, so the Proton prefix exists. Quit it.
3. Install the native runtime DLLs into the game's prefix:

   ```
   protontricks 403640 vcrun2022 d3dcompiler_47
   ```
4. Download `d2vr-vNNN-dll.zip` from the project's GitHub releases page. For a private repo with the GitHub CLI:

   ```
   gh release download vNNN -R Hollownett/dishonored2-vr -p 'd2vr-v*-dll.zip'
   ```
5. Install it:

   ```
   linux/d2vr-linux install --zip d2vr-vNNN-dll.zip
   ```

   This copies the mod into the prefix (the mod home), the proxy and a one-line `d2vr-linux.ini` into the game folder, and downloads the OpenXR loader, verifying its hash. Offline, pass `--openxr-zip <OpenXR SDK zip>` instead. `--proxy <path to dinput8.dll>` overrides the proxy file (default `build/linux-proxy/dinput8.dll`).

   `install` refuses, and changes nothing, if the game's `Dishonored2.exe` is not the pinned build (17576867), if the release zip fails its hash check, or if the game folder already holds another mod's `dinput8.dll`.

   The install also writes the default feature knobs into the mod home's `build/` (the list is `tools/default_features.json`; state in `build/launcher-features.json`). Switch features with:

   ```
   linux/d2vr-linux feature list          # id, on/off, label
   linux/d2vr-linux feature off full-body
   linux/d2vr-linux feature on full-body
   ```

   A switched-off feature stays off across reinstalls: the state's `off` list is authoritative and every install re-applies it (an `enable` knob file is removed; if it holds a value, such as `full-body`, it is set aside as `<file>.off-by-launcher`, then `.1`, `.2`, ... and the most recently modified is restored when the feature is switched back on; a `no-*` file is created). Editing `off` by hand works the same way at the next install; removing an id from `off` turns the feature back on then. Features marked "new, untested" are not enabled by default.
6. Let SteamVR be the host OpenXR runtime:

   ```
   linux/d2vr-linux doctor --fix-openxr
   ```
7. Print the launch options and paste the line into Steam > Dishonored 2 > Properties > Launch Options:

   ```
   linux/d2vr-linux launch-options
   ```

   It prints a line of this form (the GPU name is detected):

   ```
   WINEDLLOVERRIDES="dinput8=n,b" DXVK_FILTER_DEVICE_NAME="<NVIDIA name>" __NV_PRIME_RENDER_OFFLOAD=1 __VK_LAYER_NV_optimus=NVIDIA_only %command%
   ```
8. Recommended: Properties > Updates > "Only update this game when I launch it". A Steam update to the game changes the `Dishonored2.exe` hash, which breaks the pinned build: `doctor` then reports FAIL ("Steam updated the game") and the mod must not be used until a release supports the new build.
9. Before a VR session, stop anything else using VRAM (for example `systemctl --user stop bonsai.service`, or whichever LLM server or other GPU program you run). The game plus SteamVR need most of an 8 GB card.

If Steam is not in the default location, pass `--steam-root <dir>` before the command (for example `linux/d2vr-linux --steam-root ~/.steam/steam doctor`).

## 4. Every session

1. Start SteamVR and connect the Quest through Steam Link first. SteamVR must be running before the game starts.
2. Run `linux/d2vr-linux doctor`. All 12 checks should show PASS.
3. Launch Dishonored 2 from Steam.
4. At the title screen, either wait 45 seconds (the proxy logs `settle timeout`) or run:

   ```
   linux/d2vr-linux start
   ```

   VR does not start during the logos or loading screens. A `linux-start` file left over from a previous session is ignored.
5. Put the headset on and load your save.
6. Before quitting the game, run:

   ```
   linux/d2vr-linux stop
   ```

   The game keeps running flat.

Alt-tabbing is safe. At launch the proxy appends `win_pauseOnFocusLoss "0"`, `in_controlInactiveWindow "1"` and `r_fullscreen "0"` to `dishonored2Config.cfg` if they are missing, so the game does not pause when it loses focus and runs windowed (headset-sized output needs a windowed swapchain; in exclusive fullscreen `xr-recoveries.log` repeats "Recommended headset output requires a windowed game swapchain" and VR never starts). If the lines were only just added (the log shows `focus cvars appended`), the game may only apply them from the next launch. If the log shows `dishonored2Config.cfg not found`, run the game once without VR so it creates its config, then launch again.

`start`, `stop` and `doctor` take `--mod-home <dir>` if the mod home is not the default.

## 5. Troubleshooting

Logs and artifacts live in the mod home, `<prefix>/drive_c/users/steamuser/AppData/Local/DishonoredVR/artifacts/`:

- `linux-proxy.log` (the previous run is kept as `linux-proxy.previous.log`). **If this file does not exist at all, the proxy never loaded:** check that the launch options contain `WINEDLLOVERRIDES="dinput8=n,b"` and that `dinput8.dll` is in the game folder (`doctor`, check "Proxy"). Lines to look for:
  - `game window found; waiting for start (trigger or 45 s)`
  - `start trigger` or `settle timeout`
  - `StartProbe begin ...`
  - `StartProbe returned 0` means the VR session ran and ended normally.
  - `StartProbe returned 2` means a previous VR session is still shutting down or restoring the game. Wait a few seconds and run `d2vr-linux start` again.
  - `StartProbe returned 3 (...\.git missing)` means the mod home is not installed. Run `install`.
  - `StartProbe returned 1 (see artifacts\dxgi-probe-failure.txt)` means the probe failed. Read `artifacts/dxgi-probe-failure.txt`.
  - `StopProbe returned 0` after `d2vr-linux stop`.
  - `expected exactly one d2vr_render_probe_arm_v*.dll`: the mod home holds zero or two mod DLLs. Re-run `install`.
- `artifacts/dxgi-probe-failure.txt`: why the probe failed to start.
- `artifacts/xr-recoveries.log`: **why VR is not showing while the game runs.** If the proxy log stops at `StartProbe begin` and the headset stays dark, the mod is up but OpenXR session creation keeps failing and retrying; the reason (for example a LUID mismatch or the runtime not being available) is recorded here, not in `dxgi-probe-failure.txt`.
- `artifacts/xr-live-status.json`: updates about once a second while VR runs.

Common problems:

- **"Game device differs from XR device requirements" (LUID mismatch)**, found in `xr-recoveries.log` or `dxgi-probe-failure.txt`. The game and the VR compositor are on different GPUs. Check that `DXVK_FILTER_DEVICE_NAME` in the launch options matches the NVIDIA GPU name (re-run `launch-options`) and that the SteamVR compositor runs on the NVIDIA GPU.
- **Black screen or shader errors.** The prefix needs the native `d3dcompiler_47`: `protontricks 403640 d3dcompiler_47`. `doctor` checks this.
- **SteamVR was started after the game.** Quit the game, make sure SteamVR is running with the Quest connected, and launch the game again.
- **Doctor reports the host OpenXR runtime missing.** Run `linux/d2vr-linux doctor --fix-openxr`.
- **Need more detail.** Add `PROTON_LOG=1` to the launch options (before `%command%`). Proton writes `~/steam-403640.log`. Adding `WINEDEBUG=+openxr` as well gives OpenXR tracing. Remove both afterwards, the logs are large.
- **Game pauses when you alt-tab.** See the focus cvars note in section 4; restart the game once so they apply.

## 6. Uninstall

```
linux/d2vr-linux uninstall
```

This removes the proxy and `d2vr-linux.ini` from the game folder. A `dinput8.dll` that is not ours is left alone. The mod home stays in the prefix. Then remove `WINEDLLOVERRIDES="dinput8=n,b"` (the whole launch options line) from Steam.

## 7. Developer tests

```
python3 -m unittest discover -s linux/tests -v
python3 linux/tests/proxy_check/run_proxy_check.py -v
```

`tests/test_sim.py` covers the `sim` safety refusals and the save backup/restore logic against a fake prefix and fake `/proc`. For simulated game runs see section 9.

The second one runs under Wine in the harness prefix. Never run it at the same time as `tools/linux/run_build_cmd.py`.

## 8. First headset run checklist (manual)

Nobody has done this yet. It is the acceptance test for the Linux path. Record the result for each step.

1. `doctor` shows all 12 PASS with SteamVR running and the Quest connected through Steam Link.
2. Launch Dishonored 2 from Steam. `linux-proxy.log` shows `game window found`.
3. At the title screen, wait for `settle timeout` or run `d2vr-linux start`. The log shows `StartProbe begin`, and `artifacts/xr-live-status.json` updates about once a second.
4. The headset shows the game image in both eyes, with no LUID-mismatch error in `xr-recoveries.log` or `dxgi-probe-failure.txt`. If the headset stays dark, copy `xr-recoveries.log` and `linux-proxy.log` into your report.
5. Head tracking moves the view. Both Touch controllers track, and snap turn works.
6. Load a save. Gameplay stays responsive after alt-tabbing to a terminal and back. If the game pauses on focus loss in the very first session, note it: the focus settings are written at launch and may only take effect from the next launch.
7. `d2vr-linux stop`: the log shows `StopProbe returned 0` and then `StartProbe returned 0`. The game keeps running flat.
8. Quit the game, then run `d2vr-linux uninstall`. The game launches vanilla. Remove `dinput8=n,b` from the launch options.

## 9. Simulated test runs (developer)

`d2vr-linux sim` runs the real game under Proton with the mod's scripted XR backend (`src/simulated_xr.hpp`, no headset, no SteamVR) and plays a gameplay script, like the Windows `tools/vr_selftest.py`. It is for developers; a normal headset session never uses it.

**Safety.** The command refuses to start if `Dishonored2.exe` or SteamVR's `vrserver` is running (your own session), and it only ever signals a game it launched itself. Before touching anything it copies `savegame/` and `dishonored2Config.cfg` to `~/d2vr-test-builds/save-backups/<timestamp>-sim/` (backups are never deleted). While a run is unfinished that folder holds a `restore-pending` file and further runs are refused. At the end the saves are restored and checked against SHA-256 hashes taken at backup time. If the restore cannot be verified, or the game would not close, the backup is kept and the command tells you to run `d2vr-linux sim --restore`. The dev files it writes into the mod home (`build/linux-probe-override.cfg`, `linux-autocontinue`, `linux-settle-seconds`, `simulated-gameplay-script.cfg`, `artifacts/simulated-gameplay-script.json`, and the previous `dxgi-probe.cfg`) are put back or removed. The run overwrites the mod home's `artifacts/` (a copy goes to the snapshot).

**One-time prerequisites.** The installed proxy must be a build that carries the sim knobs (`python3 linux/proxy/build.py`, then `d2vr-linux install --zip ...`; `sim` says so if the installed one is older), and the mod home needs a mod DLL with the simulated-VR and gameplay-script capabilities (a dev build, not necessarily the release). `sim` installs nothing.

```
linux/d2vr-linux sim --dry-run                      # checks and plan; writes and launches nothing
linux/d2vr-linux sim --test-save ~/saves/next-to-a-tank --status-field carry --status-field sword_grip.carries
linux/d2vr-linux sim --restore                      # after a crash or an interrupted run
```

Useful options: `--scenario NAME` (a script under `tools/linux/scenarios/`, default `oil_tank_drop`) or `--script FILE.json` (the `{"segments": [...]}` format of `tools/simulated_gameplay_script.py`), `--seconds N` (10-120, default lead + scenario + 6), `--lead S` (neutral seconds before the scenario while the save loads, default 75), `--test-save DIR` (a `savegame` folder to play instead of your saves, swapped in like `tools/sim_saves.sh in`), `--settle S`, `--no-autocontinue`, `--status-field PATH` (repeatable, dotted path into `xr-live-status.json`; defaults `gameplay`, `logic_mask`, `game_layers_submitted`, `carry`), `--steam-cmd` (default `steam -applaunch 403640`, so your Steam launch options apply).

What a run does: check the safety rules, back up the saves, write the dev files, launch the game, wait for `StartProbe begin` and then `StartProbe returned` in `artifacts/linux-proxy.log`, close the game (SIGTERM, SIGKILL after `--close-timeout`, default 30 s), copy `artifacts/` and the dev config to `~/d2vr-test-builds/sim-runs/<timestamp>/`, restore everything, and print the chosen status fields from the snapshot. Exit code 0 only if `StartProbe returned 0`; 2 if the saves could not be restored.

**Measuring a candidate DLL.** `--candidate-dll PATH` runs a freshly built mod DLL without you installing it. PATH must be a PE DLL with exactly the name of the DLL in the mod home (`d2vr_render_probe_arm_v*.dll`; build the candidate under the installed name). The rig copies the installed DLL to `<backup-root>/<timestamp>-sim/installed-<name>` (fsynced, SHA-256 verified), writes `dll.json` there (resolved path, original and candidate hashes) and swaps the candidate in with an atomic rename. The restore at the end (and `sim --restore`, which tries every pending backup, oldest first) puts the DLL back first, independently of the saves, and only if the installed file still is the candidate; if it already is the original that counts as done, and if anything else is there (a newer install) nothing is written, the pending marker stays and both hashes are printed. A backup that fails its hash is never offered for copying. If the game will not close, the message says the candidate is still installed and gives a hash-guarded copy-then-rename command (it acts only while the installed file is still the candidate and the backup is the original; run it once the game has exited) plus `sim --restore`. One `flock` on `${XDG_STATE_HOME:-~/.local/state}/d2vr/sim.lock` covers each run and each `--restore`, whatever `--backup-root` is; the restore phase holds back Ctrl-C until it finishes. `--dry-run` prints the candidate and installed hashes. The `idle` scenario (`--scenario idle`) is 90 s of standing still with a slow head sway (yaw +-20 deg over 10 s as absolute `head_pose`, pitch +-10 deg); the session cap is 120 s, so use `--lead 28 --seconds 120` to fit all 90 s (the save may still be loading when the lead ends, and the idle segments only act in gameplay).

**Per-run knobs, probe modes, VR settings and environment.** All are developer options of `sim` and are undone after the run whatever its outcome.
- `--knob NAME` (repeatable, `[a-z0-9-]+`) writes an empty `build/NAME` through the same file guard as the other dev files, so a knob such as `native-gpu-timing` or `frame-split-trace` reaches only this run. A knob file you already had is restored with its bytes. The names it created are also listed in `build/linux-sim-knobs`, so `sim --restore` removes them after a hard kill. `--dry-run` lists them.
- `--probe-mode xr|paired|single` (default `xr`). `paired` and `single` use the probe's flat diagnostic stereo modes 3 and 2 instead of the simulated headset: no XR, no simulated input, no controllers (so the gameplay script does nothing; autocontinue still gets to the game). They are for the single-view measurement only. If mode 2 does not start under Proton the log says so; record "not measurable".
- `--vr-setting KEY=VALUE` (repeatable) gives the run VR settings without touching your `build/vr-settings.cfg` (a simulated run never reads it: the probe rejects saved settings together with the simulated backend). `temporal_aa` (0-6) is written as the probe's `build/aa-mode` knob; `resolution` (0.5-2.0, `--probe-mode xr` only) goes into the override config and turns on the headset sizing and full render scale that the probe requires for it. `mirror` is accepted only as 2 (the default): the probe takes the mirror mode only from saved settings, so another value is refused rather than ignored.
- `--env KEY=VALUE` (repeatable) adds `DXVK_HUD`, `DXVK_LOG_LEVEL` or `__GL_THREADED_OPTIMIZATIONS` to the environment of the Steam launch command. No other key is accepted.
- Each run folder gets `adapter.txt`: the `"adapter"` block of `xr-live-status.json` as one JSON line (`null` when absent). `python3 tools/perf_bound_report.py RUN [RUN ...] [--before PREVIOUS.json] [--json OUT.json]` prints one row per run (mode, adapter, interval p50/p95, eye0/eye1, eye1 share, hook, gpu_busy, render/game thread busy) and a verdict: GPU-BOUND, CPU-BOUND(render), CPU-BOUND(game) or MIXED. Runs without gameplay frames, or on an adapter that is not NVIDIA, are INVALID and left out.

How it works: the proxy (`linux/proxy`) honours two opt-in files in the mod home `build/` folder. `linux-probe-override.cfg` replaces the fixed headset `dxgi-probe.cfg` (`{pid}` is replaced by the game pid; the log shows `probe config override`). `linux-autocontinue` makes the proxy post Enter to the game window three times (title, Campaign, Continue, as `launcher/Probe.cs` does for a game it started) 10 s after the session starts, stopping once `xr-live-status.json` says `"gameplay":true`. The sim config is the `tools/vr_selftest.py` profile: simulated VR plus input (flag 2), timed session, no saved settings, gameplay-script flag 1. The script clock starts with the session, so the scenario begins after `--lead` seconds of neutral input while the save loads. To change a config field by hand, write your own `build/linux-probe-override.cfg` (field order: `tools/inject_probe.py probe_config`).

**Scenario `oil_tank_drop` (UNTESTED).** Holds the right grip next to a whale-oil tank, lifts to the chest, lowers the hand to the floor, releases and waits (`tools/linux/scenarios/oil_tank_drop.py`, hand poses from the `hold` scenario of `vr_selftest.py`). It needs a save that starts the player standing next to a tank, facing it, with the tank on the floor within reach in front of the knees. To make one: play to a tank in the headset or on the desktop, stand about half a metre from it looking at it, save (a manual save slot), quit the game, and copy `savegame/` (the `SLOT*` folders and `profile.bin`) to a folder of your own; pass that folder as `--test-save`. Without such a save the scenario grabs whatever physical object is nearest the right hand and drops it, which still exercises the carry/drop path but proves nothing about tanks. No real run has been done yet.

