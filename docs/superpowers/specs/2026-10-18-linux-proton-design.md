# Linux / Proton support — design (draft, 2026-10-17)

Target: Fedora 44, native Steam, Dishonored 2 (build 17576867, SHA-256 b671f824… = mod pin) under Proton,
SteamVR for Linux, Quest via the Steam Link app. Research: linux-research.md (same folder).

## Hard requirement: Windows cannot break
- Lives on branch `linux/proton` (from `fix/review-2026-10-17`), in a new top-level `linux/` folder.
- No edits to `src/`, `launcher/`, `tools/build-msvc.cmd`. If a shared change is unavoidable it must be additive,
  behind a runtime Wine check, and called out for review.
- Gate: `python3 tools/linux/run_build_cmd.py` result identical before/after (same PASS/SKIP counts, 0 FAIL).

## Pieces
1. `linux/proxy/` — `dinput8.dll` proxy (C++17, built by clang-cl via the Linux harness; also buildable with MSVC).
   - Exports `DirectInput8Create` forwarding to the real `C:\windows\system32\dinput8.dll`.
   - Starts one worker thread on attach (no work under loader lock): waits until the game window exists and a
     start trigger is present (`<modhome>/build/linux-start` file or a settle timeout), writes
     `<modhome>/build/dxgi-probe.cfg` with its own PID (same format as launcher Probe.cs:122),
     `LoadLibraryExW(<modhome>/build/<dll>, LOAD_WITH_ALTERED_SEARCH_PATH)`, checks `ProbeStatus()==0`, then calls
     `StartProbe(0)` on that thread (blocking, as the launcher's remote thread does).
   - Mod home path from `<game dir>/d2vr-linux.ini` (one line), default
     `C:\users\steamuser\AppData\Local\DishonoredVR`.
   - Logs to `<modhome>/artifacts/linux-proxy.log`.
2. `linux/d2vr-linux` (Python 3, stdlib only) — CLI:
   - `install`: locate game via `libraryfolders.vdf` + `appmanifest_403640.acf` (Linux paths), verify exe SHA-256,
     find the Proton prefix (`compatdata/403640/pfx`), lay out mod home (fake `.git/`, `build/`, `artifacts/`,
     `third_party/openxr/bin/openxr_loader.dll`, `build/fsr4.off`), copy the mod DLL + manifest from a release zip
     (`d2vr-v*-dll.zip`, sha256-checked against its manifest), copy proxy `dinput8.dll` + ini next to the exe.
   - `doctor`: checks SteamVR installed/running, `~/.config/openxr/1/active_runtime.json` (offer to create →
     SteamVR `steamxr_linux64.json`), prefix `HKLM\Software\Khronos\OpenXR\1\ActiveRuntime`, vcrun/d3dcompiler_47
     present in prefix, NVIDIA Vulkan device present, exe hash; prints PASS/FAIL per line.
   - `launch-options`: prints the Steam launch options line
     `WINEDLLOVERRIDES="dinput8=n,b" DXVK_FILTER_DEVICE_NAME="<NVIDIA name>" __NV_PRIME_RENDER_OFFLOAD=1 __VK_LAYER_NV_optimus=NVIDIA_only %command%`.
   - `start` / `stop`: write the start trigger / the stop request file the DLL already watches.
   - `uninstall`: removes proxy + ini from the game dir (restores nothing else in game dir — we only add files).
3. `linux/README.md` — user guide: Steam Link VR setup, Proton Experimental, protontricks vcrun2022 + d3dcompiler_47,
   install/doctor/launch, troubleshooting (LUID mismatch, black screen, PROTON_LOG=1).

## Tests (run on Linux)
- `linux/tests/test_d2vr_linux.py` (unittest): vdf/acf parsing with Linux + Windows-style paths, prefix discovery,
  hash check, install layout into a temp dir, uninstall, idempotence.
- `linux/tests/proxy_check` — under Wine: a fake mod DLL exporting `ProbeStatus`/`StartProbe` that writes a marker;
  a host exe that loads the proxy as `dinput8.dll`, calls `DirectInput8Create` (forwarded, returns a valid HRESULT
  from Wine's dinput8), creates a window, writes the start trigger → marker appears, cfg has the host PID.
- Headset (user): SteamVR + Steam Link connected, launch from Steam, image in headset, head tracking, controllers.

## Out of scope (v1)
FSR 4 (D3D12/AMD-only, WinVerifyTrust under Wine), Virtual Desktop, WiVRn/ALVR, flatpak Steam, the WinForms launcher
under Wine (only as a manual fallback).
