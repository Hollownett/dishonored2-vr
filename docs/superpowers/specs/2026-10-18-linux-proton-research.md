# Dishonored 2 VR mod on Linux (Proton + SteamVR + Steam Link) — research

Date: 2026-10-17. Read-only research. Repo paths are relative to /home/hollownet/github/dishonored2-vr-fixes.
Target: Fedora 44, ROG G513QR (5900HX iGPU + RTX 3070 Laptop 8 GB), native Steam, D2 in /mnt/steamssd/SteamLibrary,
SteamVR (Linux) + Quest over Steam Link.

## 1. How the mod gets into the game today

**Injection method: classic remote-thread `LoadLibraryW` into an already-running game.**
- launcher/Probe.cs:59-108 `Launch`: verifies the candidate manifest (DLL name regex `d2vr_render_probe_arm_vNNN.dll`, manifest
  `sha256` == DLL hash, manifest `game_sha256` == pin) (Probe.cs:63-70). If no `Dishonored2` process exists it runs
  `GameKeys.PrepareConfig` and `Process.Start("steam://rungameid/403640")` (Probe.cs:74-77), waits up to 3 min for the process,
  then a fixed **45 s** for the title screen (Probe.cs:82-87). Exactly one `Dishonored2` process is required (Probe.cs:90).
- Probe.cs:146-205 `Attach`: reads `<root>\build\<dll>`, requires x64 PE with export `StartProbe` (Probe.cs:151-155); enumerates
  the target's modules (Toolhelp32, Probe.cs:521-523), requires exactly one `dishonored2.exe` with SizeOfImage 68988928
  (Probe.cs:18,158) and SHA-256 == `GameHash` (Probe.cs:17,160). Resolves `LoadLibraryW` from **kernelbase.dll** (not kernel32,
  because Steam hooks kernel32's thunk) and maps it into the target by module identity (Probe.cs:191-198). `OpenProcess`
  + `QueryFullProcessImageNameW` identity re-check (Probe.cs:197-201).
- Probe.cs:217-292 `Session`: refuses if any loaded `d2vr_render_probe*.dll` reports `ProbeStatus != 0` (Probe.cs:226-233);
  checks the remote LoadLibraryW bytes equal the local ones (Probe.cs:234-235); checks game code at RVA 0x5bf00 and that
  `[base+0x2be6910] == base+0x1aca0b8` ("native renderer in single-camera mode", Probe.cs:236-240; same constants in
  src/game_stereo.hpp:7-8). Requires export `ProbeCapabilities`. Writes **`<root>\build\dxgi-probe.cfg`** (Probe.cs:242-244),
  clears/archives `artifacts\` outputs (Probe.cs:245-247), `VirtualAllocEx`+`WriteProcessMemory` the UTF-16 DLL path,
  `CreateRemoteThread(LoadLibraryW)` (Probe.cs:250-258, RunThread at Probe.cs:490). Verifies the loaded module identity and
  the first 16 bytes of `StartProbe` (Probe.cs:259-264), calls `ProbeCapabilities` remotely and requires bits
  `RequiredCapabilities` (Probe.cs:118, 266-267), writes `artifacts\loaded-render-probe<variant>.json` {pid,base,sha256}
  (Probe.cs:268), then **`CreateRemoteThread(StartProbe, param=0)` and waits with no timeout** (Probe.cs:271) — StartProbe
  blocks for the whole VR session and returns the exit code (0 = clean). Afterwards writes `artifacts\probe-launch-config.json`
  and archives a run folder (Probe.cs:280-288, 296-345, 387+).
- Stop: Probe.cs:208-213 `CreateRemoteThread(StopProbe)` on the loaded module (StopProbe → `probe_session.request_stop()`,
  src/dxgi_probe.cpp:7112-7116; returns 2 if not capturing).
- tools/inject_probe.py is the original Python implementation of the same thing (args at :190-234, CreateRemoteThread :385,
  exe hash check :425, kernelbase LoadLibraryW :459-464, cfg write :579, identity file :619). Probe.cs is its C# port with the
  fixed "headset" flag set (Probe.cs:110-116).

**Exports in the DLL** (src/dxgi_probe.cpp): `ProbeStatus` :7104, `ProbeCapabilities` :7108 (returns 524287), `StopProbe` :7112,
`StartProbe` :7118. All are `DWORD WINAPI f(void*)` — i.e. thread-start signatures; the argument is ignored. `DllMain` only stores
the module handle (:8438-8441) — nothing runs on load.

**How arguments are passed: entirely via files, not via the thread parameter.**
- `<root>` = parent of the DLL's folder: `root = module_path(module).parent_path().parent_path()` (dxgi_probe.cpp:7154).
  **`<root>\.git` must exist or StartProbe returns 3** (dxgi_probe.cpp:7155). The launcher fakes it with an empty directory
  (launcher/ModHome.cs:23).
- `<root>\build\dxgi-probe.cfg`: one ASCII line of positional integers. The first token **must equal the game's own PID**
  (`expected_pid != GetCurrentProcessId()` → throw, dxgi_probe.cpp:7167-7170). Headset profile line (Probe.cs:122):
  `"{pid} 0 6 1 0 1 1 1 1 1 1 0 1 0 0 1 1 1.0 1 0 1 45 90 1 2 0 1 1 1 0 0 0\n"` (pid, camera 0, mode 6 = XR+head tracking,
  paired, ... see the doc comment Probe.cs:119-121; parse continues dxgi_probe.cpp:7170-7240+).
- `<root>\build\vr-settings.cfg` saved preferences (dxgi_probe.cpp:5669, 7327; format in launcher/Core.cs:10-60).
- Optional knob files under `<root>\build\` read at start: `hud-scale`, `fist-mirror-axis`, `sim-eye-size`, `sim-fov`, `full-body`,
  `fsr`, `fsr-mv-full`, `mip-bias`, `fsr4.off`, `fsr4`, `fsr4-compute-timing`, `aa-mode`, `upscale`, `held-offset`, `aim-offset`,
  `trace-calls`, `gun-shape`, `slash-speed`, `taa-jitter-scale`, `read-self-profile`, `ui-dump`, `no-swarm-contact-kill`,
  `simulated-gameplay-script.cfg` (dxgi_probe.cpp:7383, 7683-7824).
- Outputs: `<root>\artifacts\` (created by the DLL, dxgi_probe.cpp:7156-7160); `xr-live-status.json` is written ~1/s and is how
  the launcher detects gameplay (Core.cs:178-193).

**What must be on disk** (mod home layout, ModHome.cs:8-11, csproj EmbeddedResources launcher/DishonoredVRLauncher.csproj:28-41,91-93):
```
<root>/.git/                                   (empty dir; required)
<root>/build/d2vr_render_probe_arm_vNNN.dll    (+ .source.json provenance, optional)
<root>/build/dxgi-probe.cfg, vr-settings.cfg, knobs
<root>/artifacts/repair-vNNN-test-candidate.json  (manifest; launcher-only check, the DLL doesn't read it)
<root>/third_party/openxr/bin/openxr_loader.dll   (Khronos 1.1.63 x64, third_party/openxr/bin/source.json)
<root>/third_party/fsr4/bin/amd_fidelityfx_{upscaler,loader}_dx12.dll  (optional; absent in this checkout)
```
The DLL is built `/MD` (tools/build-msvc.cmd:847) → needs msvcp140/vcruntime140/vcruntime140_1 (Core.cs:144-146 checks
msvcp140_2 too), and links d3d11, dxgi, bcrypt, user32, **d3dcompiler(_47)**, gdi32, FidelityFX FSR3 DX11 static libs
(build-msvc.cmd:847) plus `#pragma comment` d3d12, wintrust, crypt32 (src/fsr4_eye_upscaler.hpp:21-23) — so d3d12.dll is a hard
import even when FSR4 is unused.

**Exe SHA-256 pin**: `b671f8249422862bf557d00aab2f4bb671ed4515cc10ad8a8a9decad84516180` (Probe.cs:17; src/game_camera.hpp:12),
checked by the launcher (Probe.cs:160) and again **inside StartProbe** (`sha256(game) != d2vr::game_sha256` → throw,
dxgi_probe.cpp:7162-7163), plus SizeOfImage 68988928. **Local check: the Dishonored2.exe already present in
/mnt/steamssd/SteamLibrary/steamapps/downloading/403640/ hashes to exactly the pin** (41,134,632 bytes, SizeOfImage 68988928,
TargetBuildID 17576867). Re-check after Steam moves it into common/Dishonored2 (state_*.patch files are pending).

**Post-launch Enter automation**: only when the launcher itself started the game. 10 s after StartProbe's "started" callback it
posts VK_RETURN up to 3× (1.5 s apart) via `PostMessageW(WM_KEYDOWN/WM_KEYUP)` to the visible window owned by the pid whose
title contains "Dishonored", stopping once `xr-live-status.json` says gameplay (Probe.cs:93-106, GameKeys.Post Probe.cs:559-577).
Before launch `GameKeys.PrepareConfig` appends `win_pauseOnFocusLoss "0"` and `in_controlInactiveWindow "1"` to
`%USERPROFILE%\Saved Games\Arkane Studios\Dishonored2\base\dishonored2Config.cfg` (Probe.cs:540-557). Under Proton that file is
`compatdata/403640/pfx/drive_c/users/steamuser/Saved Games/Arkane Studios/Dishonored2/base/dishonored2Config.cfg`.

## 2. OpenXR loading and Windows-isms in the DLL

- Loader path (dxgi_probe.cpp:2415-2423 `openxr_loader_path`): 1) `<root>\third_party\openxr\bin\openxr_loader.dll`;
  2) `C:\Program Files (x86)\Steam\steamapps\common\SteamVR\bin\win64\openxr_loader.dll`; 3) bare `openxr_loader.dll` (search path).
  Loaded with `LoadLibraryExW(path, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS)` and
  `GetProcAddress("xrGetInstanceProcAddr")` (src/game_xr.hpp:961-965). Under Proton the bundled Khronos loader is the one to use.
- Extensions: **`XR_KHR_D3D11_enable` (required)** + `XR_KHR_composition_layer_cylinder` (only if enumerated) (game_xr.hpp:967-982).
  No D3D12 OpenXR binding. Binding = `XrGraphicsBindingD3D11KHR{device = the game's own ID3D11Device}` (game_xr.hpp:1016-1018).
- **Adapter check**: `xrGetD3D11GraphicsRequirementsKHR` LUID must equal the game device's DXGI adapter LUID and feature level
  ≥ min, else throw "Game device differs from XR device requirements" (game_xr.hpp:1009-1015). Critical on hybrid GPU (see §5).
- Interaction profile: only `/interaction_profiles/oculus/touch_controller` (game_xr.hpp:666) — fine for Quest via Steam Link.
- Input sampled only while FOCUSED (VD crash workaround, game_xr.hpp:681-683). Harmless on SteamVR.
- Registry: the **DLL reads no registry**. The launcher does: Steam path (Core.cs:116-117), `HKLM\SOFTWARE\Khronos\OpenXR\1\
  ActiveRuntime` only for a UI check / VDXR recommendation (Core.cs:134-159). The Khronos loader reads that same key itself.
- Process names: DLL checks its host module filename `Dishonored2.exe` + hash (dxgi_probe.cpp:7161-7163); launcher uses
  `GetProcessesByName("Dishonored2")`.
- Virtual-Desktop-specific behaviour: only the FOCUSED-input guard and the launcher's "VDXR recommended" hint. Nothing hard-wired.
- D3D11 runtime identity check: `verified_d3d11_target` requires `GetModuleFileName(d3d11.dll) == GetSystemDirectory()\d3d11.dll`
  and hook targets inside that image (dxgi_probe.cpp:1468-1488). Under Proton DXVK's d3d11.dll lives at C:\windows\system32\
  d3d11.dll in the prefix → passes. A game-dir d3d11.dll (e.g. ReShade/another proxy) would **fail** this check.
- Present hook discovery: creates a hidden window + `D3D11CreateDeviceAndSwapChain` and takes vtable[8]/[13] (dxgi_probe.cpp:
  7079-7101), MinHook on that (7494-7518). Works with DXVK (one swapchain class), see §5.
- XInput: the controller bridge requires `GetModuleHandle("XINPUT9_1_0.dll")` and patches the game's IAT slot, checking the slot
  still holds the real export (src/controller_input.hpp:165-180). ⇒ **never proxy XINPUT9_1_0.dll**.
- Windows APIs with Wine caveats:
  - `D3DCompile` (d3dcompiler_47) at runtime for CAS/SMAA/mirror/FSR-TAA shaders (src/eye_sharpen.hpp:38 etc.). Wine's builtin
    d3dcompiler_47 (vkd3d-shader) can miscompile/refuse some HLSL → install native d3dcompiler_47 in the prefix (same fix used for
    Skyrim/Nolvus and Lost Alpha on this machine).
  - VC++ runtime /MD: Wine builtin msvcp140 is incomplete → `protontricks 403640 vcrun2022` (or ship the DLLs next to the mod and
    load with LOAD_WITH_ALTERED_SEARCH_PATH).
  - `WinVerifyTrust` Authenticode check of the AMD FSR4 provider (fsr4_eye_upscaler.hpp:88-104) — Wine has no real Authenticode
    chain → FSR4 always fails (fine: it must be off on NVIDIA anyway).
  - `ReadProcessMemory(GetCurrentProcess())` + SEH as a safe-read (dxgi_probe.cpp:1531-1533, 1568-1578): supported by Wine.
  - BCrypt SHA-256 (dxgi_probe.cpp:5432+): supported (gnutls backend).
  - `GetAsyncKeyState`/foreground checks only in non-paired modes (dxgi_probe.cpp:5415-5420, 7486-7490) — not used by headset profile.

## 3. Proton side (wineopenxr)

Source: ValveSoftware/Proton `wineopenxr/` (https://github.com/ValveSoftware/Proton/tree/proton_10.0/wineopenxr), files
`openxr_loader.c`, `openxr.c`, `dxvk-interop.h`; VR bootstrap in `vrclient_x64/vrclient_main.c` + `unixlib.cpp`, and
`steam_helper/steam.c`.

- **D3D11 is supported, DXVK only.** `xrCreateSession` with `XR_TYPE_GRAPHICS_BINDING_D3D11_KHR` QIs the device for
  `IID_IDXGIVkInteropDevice2`; failure → "Only DXVK is supported" `XR_ERROR_VALIDATION_FAILURE`; it then gets the VkInstance/
  VkPhysicalDevice/VkDevice/queue from DXVK and creates a host Vulkan session (wineopenxr/openxr_loader.c ~L586-615). D3D12 is
  likewise vkd3d-proton-only (~L616-665). ⇒ PROTON_USE_WINED3D must NOT be used.
- `xrGetD3D11GraphicsRequirementsKHR` (openxr_loader.c ~L1677-1740): enumerates DXGI adapters and picks the one whose
  VendorId/DeviceId match the `openxr_vulkan_device_vid/pid` that SteamVR requested; if none matches it **falls back to adapter 0**
  and returns its LUID; minFeatureLevel = 10_0.
- Local binaries confirm it: `wineopenxr.so` in Proton Experimental (11.0-20261014), Proton 11.0-2e, Proton 10.0-4b, Proton 9.0-4f
  and all GE builds contains `XR_KHR_D3D11_enable`, `XR_KHR_D3D12_enable`, `XR_KHR_vulkan_enable(2)`,
  `XR_KHR_composition_layer_cylinder`. wineopenxr.dll present in every install (Exp: files/lib/wine/x86_64-windows/wineopenxr.dll).
- **Runtime registration inside the prefix**: default_pfx `system.reg` has `[Software\Khronos\OpenXR\1] "ActiveRuntime"=
  "C:\\openxr\\wineopenxr64.json"` (and Wow6432Node); the `proton` script copies `share/openxr/wineopenxr64.json` to
  `drive_c/openxr/` (Proton-Experimental `proton` L1158-1160). JSON: `library_path = C:\windows\system32\wineopenxr.dll`.
  ⇒ **Yes: a Khronos openxr_loader.dll shipped next to the mod finds wineopenxr via HKLM\SOFTWARE\Khronos\OpenXR\1\ActiveRuntime.**
- **Bootstrap / ordering (the important gotcha)**: Proton's `steam.exe` helper calls `vrclient_init_registry` for game processes
  (steam_helper/steam.c L184-204, L876-879). That creates the *volatile* key `HKCU\Software\Wine\VR`, and a thread does
  `IVRClientCore::Init(VRApplication_Background)`; if no SteamVR server is running → `state = -1` (vrclient_main.c ~L310-340,
  unixlib.cpp ~L170-230). Only on success does it call `wineopenxr_init_registry`, which writes `openxr_vulkan_instance_extensions`,
  `openxr_vulkan_device_extensions`, `openxr_vulkan_device_vid/pid`, then `state = 1`. wineopenxr's xrCreateInstance waits while
  `state == 0` and fails if the extension values are missing (openxr_loader.c ~L100-200). The key is created once per wineserver
  session ("Already initialized, returning"). ⇒ **SteamVR (with the Quest connected via Steam Link) must be running before the
  game is launched**, and the game must be relaunched (wineserver gone) if it was started first.
- **Host side**: wineopenxr.so links Proton's bundled `libopenxr_loader.so.1` (1.1.63), which needs a host active runtime:
  `~/.config/openxr/1/active_runtime.json` → `~/.local/share/Steam/steamapps/common/SteamVR/steamxr_linux64.json`.
  **Locally this file does not exist** (`~/.config/openxr` missing; /etc/xdg/openxr absent). Fix: SteamVR Settings → OpenXR →
  "Set SteamVR as OpenXR runtime", or symlink it manually. SteamVR installed: buildid 25330290; openvrpaths.vrpath OK.
- Env / launch options: none required for OpenXR itself. `PROTON_ENABLE_NVAPI` is **not** needed (game imports amd_ags_x64, not
  nvapi; NVAPI only matters for DLSS/Reflex). Do not set PROTON_USE_WINED3D / PROTON_NO_D3D11.
- Steam Link VR on Linux: works since SteamVR beta 2.13.2 (experimental; flatpak Steam unsupported):
  https://gamingonlinux.com/2025/09/steam-link-vr-for-meta-quest-on-linux-now-appears-to-work-in-the-latest-updates ;
  later Linux Steam Link fixes in 2.17.8 (crash on invalid game textures) and 2.18.1:
  https://gamingonlinux.com/2026/09/steamvr-beta-2-17-8-released-to-ready-up-for-the-steam-frame-added-support-for-usb-streaming ,
  https://www.gamingonlinux.com/2026/09/steamvr-beta-v2-18-1-brings-more-linux-fixes-and-general-improvements/
  ⇒ keep SteamVR on the beta branch.
- **Best Proton bet: Proton Experimental** (newest wineopenxr/vrclient, OpenXR loader 1.1.63, same interop) with **Proton 10.0**
  as fallback. Proton 9.0 also has D3D11 wineopenxr but older vrclient (fewer Steam Link fixes). GE-Proton builds carry the same
  wineopenxr but the user's memory notes GE-Proton11 broke SKSE-style injection for Nolvus — avoid GE for the first bring-up.

## 4. Injection approach under Proton (ranked)

Game PE imports (parsed read-only from the downloaded exe, SHA matches pin): bink2w64, WINHTTP, IPHLPAPI, WINMM(3),
SETUPAPI, d3d11(D3D11CreateDevice), **DINPUT8 (1: DirectInput8Create)**, dxgi(CreateDXGIFactory1), WSOCK32, XINPUT9_1_0,
dbghelp, amd_ags_x64, KERNEL32, USER32, GDI32, ADVAPI32, SHELL32, ole32, OLEAUT32, steam_api64, GFSDK_SSAO_D3D11.win64,
**d3d9 (1: D3DPERF_SetOptions)**, AnselSDK64(4), WS2_32. No version.dll, no delay imports.

**(a) Proxy DLL — recommended: `dinput8.dll` in the game folder + `WINEDLLOVERRIDES="dinput8=n,b"`.**
- Only one export to forward (`DirectInput8Create` → `LoadLibrary("C:\\windows\\system32\\dinput8.dll")`). Standard for
  Proton mods (SKSE/ASI loaders). Not touched by the mod's own checks (unlike d3d11/dxgi/XINPUT9_1_0).
- Runner-up: `d3d9.dll` (1 export, D3DPERF_SetOptions) — but d3d9 is DXVK-native in Proton and override juggling is riskier.
  `winmm.dll` is used by Wine internals; `dxgi`/`d3d11` break `verified_d3d11_target`; `XINPUT9_1_0` breaks the controller
  bridge; replacing game-shipped DLLs (AnselSDK64, GFSDK_SSAO) needs no override but is undone by Steam "verify files".
- Proxy behaviour (must replicate Probe.cs, minus the cross-process parts):
  1. `DllMain(PROCESS_ATTACH)`: forward setup only, `CreateThread(worker)`; never call StartProbe under loader lock.
  2. worker: wait for the title screen — poll until `[base+0x2be6910] == base+0x1aca0b8` (single-camera mode, game_stereo.hpp:7-8),
     a visible "Dishonored" window exists, plus a settle delay (launcher uses 45 s); or wait for a trigger file
     `<root>\build\start-request` written by a Linux helper.
  3. Verify exe SHA-256 (the DLL does it anyway), write `<root>\build\dxgi-probe.cfg` with `GetCurrentProcessId()` and the
     Probe.cs:122 line; ensure `<root>\.git` and `<root>\artifacts` exist.
  4. `LoadLibraryExW(<root>\build\d2vr_render_probe_arm_vNNN.dll, LOAD_WITH_ALTERED_SEARCH_PATH)`; `ProbeStatus()==0`;
     `ProbeCapabilities() & 0xEE59 (=61017 = RequiredCapabilities 1|8|16|64|512|1024|2048|8192|16384|32768)` all set; then call
     `StartProbe(nullptr)` **on this worker thread** (it blocks for the session); log the return code to `artifacts\`.
  5. Optional: hotkey/file-watch → `StopProbe`; optional Enter automation by posting WM_KEYDOWN to its own window (same as
     GameKeys.Post) until `xr-live-status.json` reports gameplay.
- `<root>` can be any Wine-visible path, e.g. `Z:\home\hollownet\.local\share\DishonoredVR\` or
  `compatdata/403640/pfx/drive_c/users/steamuser/AppData/Local/DishonoredVR` (= ModHome.Dir inside the prefix).
- Steam launch options: `WINEDLLOVERRIDES="dinput8=n,b" DXVK_FILTER_DEVICE_NAME="RTX 3070" %command%` (+ PRIME vars, §5).

**(b) Run the existing launcher exe in the same prefix** (`protontricks-launch --appid 403640 DishonoredVRLauncher.exe`, or
`STEAM_COMPAT_DATA_PATH=… STEAM_COMPAT_CLIENT_INSTALL_PATH=… "<Proton>/proton" run …`). It is a self-contained single-file
.NET 8 WinForms exe (csproj:3-16), so no .NET install needed, and Wine does implement VirtualAllocEx/WriteProcessMemory/
CreateRemoteThread/Toolhelp across processes **in the same wineserver**. Problems: must share the game's wineserver (same Proton
build + same prefix; the game runs inside the pressure-vessel/sniper container, protontricks-launch may not), the launcher's
`steam://rungameid` start path and Steam/registry lookups (Core.cs:113-132) don't map cleanly, the kernelbase identity checks
are fragile, and WinForms-under-Wine UX. Usable as a quick feasibility test (start game from Steam first, then "Launch VR"
attaches), not as the product.

**(c) Other**: Proton's `PROTON_REMOTE_DEBUG_CMD`/a companion exe launched via `%command%` wrapper inside the same container that
does the CreateRemoteThread itself (a tiny native-Windows C injector = inject_probe.py minus Python) — keeps the
"inject at title screen" model, no game-folder files; or Wine `AppInit_DLLs` (unreliable). Ranked below (a).

## 5. Risks

- **DXVK vs native D3D11 vtable hooks.** Present/ResizeBuffers found via a throwaway swapchain (dxgi_probe.cpp:7079-7101): DXVK
  uses one `DxgiSwapChain` class → same vtable as the game's, OK. DXVK methods are mingw-compiled with COM adjustor thunks; MinHook
  generally handles them, but the 16-byte prologue verification (`disabled_target_bytes`, dxgi_probe.cpp:7498-7509) is per-run
  only, OK. Context draw hooks (`gpu_bindings`, vtable slots; dxgi_probe.cpp:1520-1555) — DXVK's immediate and deferred contexts
  are different template instantiations (`D3D11CommonContext<…>`), so `gpu_deferred_methods_shared` will be false; the mod already
  records that. `CreateTexture2D` hook (dxgi_probe.cpp:300-315) hooks DXVK's D3D11Device — note wineopenxr creates its swapchain
  images through DXVK interop, not CreateTexture2D, so no recursion expected. DXVK presents asynchronously (`dxgi.numBackBuffers`,
  frame latency) — frame-timing CSVs will differ from Windows. Expect to verify `native-context-dispatch-change.jsonl` on first run.
- **Hybrid GPU.** Both the game (DXVK) and SteamVR's vrcompositor must be on the RTX 3070, and the DXGI adapter LUID returned by
  wineopenxr must equal the game's adapter (game_xr.hpp:1009-1015). DXVK hides NVIDIA as AMD by default when NVAPI is off, so
  wineopenxr's vid/pid match can miss and it falls back to **adapter 0** → make adapter 0 the NVIDIA GPU:
  `DXVK_FILTER_DEVICE_NAME="NVIDIA GeForce RTX 3070"` (or `VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/nvidia_icd.x86_64.json`,
  which also dodges the mesa 26.1.3 heap crash seen with Nolvus), plus `__NV_PRIME_RENDER_OFFLOAD=1
  __VK_LAYER_NV_optimus=NVIDIA_only __GLX_VENDOR_LIBRARY_NAME=nvidia`. Launch Steam/SteamVR so vrcompositor also uses NVIDIA (same
  env in `~/.local/share/Steam/steamapps/common/SteamVR/bin/vrmonitor.sh` launch options or Steam itself). VRAM: 8 GB shared by
  game + compositor + mod eye buffers — stop bonsai.service/llama-server first (memory note: it wedges NVIDIA).
- **FSR4** (D3D12 via vkd3d-proton + AMD Authenticode + RDNA4-only provider): will fail on this machine; the mod falls back, but set
  AA mode ≤4 in vr-settings.cfg (`TemporalAa` 2 = FSR3 native) and create `<root>\build\fsr4.off` (dxgi_probe.cpp:7766-7767);
  don't ship the FSR4 DLLs in the Linux bundle. d3d12.dll must still resolve (vkd3d-proton ships it).
- **Exe hash pin**: currently matches (downloaded exe = pin). Any Steam update breaks both the launcher check and StartProbe's
  internal check (dxgi_probe.cpp:7162). Consider disabling auto-update for 403640 ("Only update when I launch" isn't enough —
  note the risk). Proxy should log a clear message instead of silently doing nothing.
- **Ordering**: SteamVR + Steam Link connected **before** the game starts (Wine\VR state), host active_runtime.json present.
- **d3dcompiler_47 / vcrun2022**: install native via protontricks, or StartProbe may throw on shader compile / crash on load.
- **Focus**: the launcher's PrepareConfig keeps the game running unfocused; do the same edit in the prefix's config file.
- **Overlay / anti-cheat**: none on D2; Steam overlay hooks kernel32 thunks (Probe.cs:191-192) — irrelevant for an in-process proxy.

## Recommended architecture (≤15 lines)

1. Proton Experimental (fallback 10.0), DXVK default, no NVAPI; prefix prepped once with `protontricks 403640 vcrun2022 d3dcompiler_47`.
2. Host prereqs script: `~/.config/openxr/1/active_runtime.json` → SteamVR `steamxr_linux64.json`; SteamVR beta ≥2.13 running with Quest linked first.
3. Tiny `dinput8.dll` proxy (MinGW/MSVC, forwards DirectInput8Create to system32) dropped next to Dishonored2.exe.
4. Proxy worker thread: wait for single-camera mode + window + settle (or trigger file) → write build/dxgi-probe.cfg with own PID.
5. Then LoadLibraryEx(mod, ALTERED_SEARCH_PATH) → ProbeStatus==0 → ProbeCapabilities mask → StartProbe() blocking on that thread.
6. Mod home at `pfx/drive_c/users/steamuser/AppData/Local/DishonoredVR` with `.git/`, `build/`, `artifacts/`, `third_party/openxr/bin/openxr_loader.dll`, `build/fsr4.off`.
7. Khronos loader → HKLM Khronos ActiveRuntime → C:\openxr\wineopenxr64.json → wineopenxr (D3D11 via DXVK interop) → host SteamVR.
8. Steam launch options: `WINEDLLOVERRIDES="dinput8=n,b" DXVK_FILTER_DEVICE_NAME="RTX 3070" __NV_PRIME_RENDER_OFFLOAD=1 __VK_LAYER_NV_optimus=NVIDIA_only %command%`.
9. Linux helper (`tools/linux/…`): install/verify files + exe hash, patch dishonored2Config.cfg focus lines, write start/stop request files, tail artifacts.
10. Stop via file-watch/hotkey → StopProbe; Enter automation optional (proxy posts to own window until xr-live-status gameplay).
11. Fallback for quick feasibility: run the existing launcher exe in the same prefix/wineserver and use its attach path.
12. First-run checks: native-context-dispatch-change.jsonl, dxgi-probe-failure.txt, LUID mismatch message, Proton log (PROTON_LOG=1, WINEDEBUG=+openxr).
