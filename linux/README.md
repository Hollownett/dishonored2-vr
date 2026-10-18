# Dishonored 2 VR on Linux (Proton + SteamVR + Steam Link)

## 1. What this is

Two pieces:

- **The proxy** (`dinput8.dll`, built by `linux/proxy/build.py`). It sits in the game folder, loads the mod DLL inside the game process, and starts the VR probe once the game is ready. It logs to `<mod home>/artifacts/linux-proxy.log`. If anything fails it logs and idles, and the vanilla game keeps running.
- **The CLI** (`linux/d2vr-linux`, Python 3, stdlib only). It installs and removes the proxy and the mod home, checks the setup (`doctor`), prints the Steam launch options, and sends start/stop requests to the running game.

Windows is unaffected: nothing under `src/`, `launcher/` or `tools/` changed.

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

   This copies the mod into the prefix (the mod home), the proxy and a one-line `d2vr-linux.ini` into the game folder, and downloads the OpenXR loader, verifying its hash. Offline, pass `--openxr-zip <OpenXR SDK zip>` instead. `--proxy <dir>` overrides the proxy location (default `build/linux-proxy`).

   `install` refuses, and changes nothing, if the game's `Dishonored2.exe` is not the pinned build (17576867), if the release zip fails its hash check, or if the game folder already holds another mod's `dinput8.dll`.
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

Alt-tabbing is safe. At launch the proxy appends `win_pauseOnFocusLoss "0"` and `in_controlInactiveWindow "1"` to `dishonored2Config.cfg` if they are missing, so the game does not pause when it loses focus. If the lines were only just added (the log shows `focus cvars appended`), the game may only apply them from the next launch.

`start`, `stop` and `doctor` take `--mod-home <dir>` if the mod home is not the default.

## 5. Troubleshooting

Logs and artifacts live in the mod home, `<prefix>/drive_c/users/steamuser/AppData/Local/DishonoredVR/artifacts/`:

- `linux-proxy.log` (the previous run is kept as `linux-proxy.previous.log`). Lines to look for:
  - `game window found; waiting for start (trigger or 45 s)`
  - `start trigger` or `settle timeout`
  - `StartProbe begin ...`
  - `StartProbe returned 0` means VR started.
  - `StartProbe returned 3 (...\.git missing)` means the mod home is not installed. Run `install`.
  - `StartProbe returned 1 (see artifacts\dxgi-probe-failure.txt)` means the probe failed. Read `artifacts/dxgi-probe-failure.txt`.
  - `StopProbe returned 0` after `d2vr-linux stop`.
  - `expected exactly one d2vr_render_probe_arm_v*.dll`: the mod home holds zero or two mod DLLs. Re-run `install`.
- `artifacts/dxgi-probe-failure.txt`: why the probe failed.
- `artifacts/xr-live-status.json`: updates about once a second while VR runs.

Common problems:

- **"Game device differs from XR device requirements" (LUID mismatch).** The game and the VR compositor are on different GPUs. Check that `DXVK_FILTER_DEVICE_NAME` in the launch options matches the NVIDIA GPU name (re-run `launch-options`) and that the SteamVR compositor runs on the NVIDIA GPU.
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

The second one runs under Wine in the harness prefix. Never run it at the same time as `tools/linux/run_build_cmd.py`.
