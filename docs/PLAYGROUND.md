# Offline VR development playground

From the repository root, run `python tools/playground.py`. This compiles and runs
the reviewed `fast` fixtures using installed MSVC and Python, with assertions
enabled and warnings treated as errors. It does not build/deploy the DLL, start
the game, open a real XR runtime, send desktop input, or change saves/settings.

```powershell
python tools/playground.py --suite fast
python tools/playground.py --suite gpu
python tools/playground.py --suite replay --render
python tools/playground.py --suite all
python tools/playground.py --test game_xr_simulation_check --isolated-desktop
python tests/test_playground.py
python tests/test_playground_capture.py
```

`--list` prints the reviewed manifest; repeat `--test NAME` for focused work.
`--timeout 90` bounds each compiler/test/render process. Timeout cleanup targets
only that newly created process tree. Workers run below normal priority, and
execution is sequential to bound CPU/GPU
contention while the PC remains usable. WARP software rasterization still uses
CPU time: start with the fast suite during other demanding work.

Every invocation writes a unique `artifacts/playground/runs/<timestamp-id>/`
directory containing `results.json`, compile/run logs and captures. Tests run
from their own directory, so fixture-relative output cannot overwrite existing
research captures. The runner never writes `build/dxgi_probe.obj`, a DLL, or a
shared build directory. Compiled test executables are reused in
`artifacts/playground/cache/<digest>/`. Cache keys include all local and vendor
headers, test source, build flags, compiler binary and installed include/library
search paths. Source changes during compilation prevent caching and produce a
`source_changed` result. Header invalidation is deliberately conservative. A
cached executable is still executed on every invocation; results are never cached.

`results.json` records stage timings, exit codes, cache hits, source digest and
capture hashes. It explicitly records that native game interaction and headset
acceptance remain unverified. Fixture stdout explains the precise assertions.
Do not interpret wall time as game FPS, render quality or comfort.

The replay suite exports both Corvo and Emily body poses from the production
solver with the extracted native rig fixtures. `--render` reuses
`tools/render_body_ik.py` and the read-only asset pack to render the first three
frames as PNGs without opening any UI; installed numpy/Pillow are required.
The fixed scenario order and serialized pose values make JSON capture hashes
useful for regression comparisons. More frames can be rendered explicitly:

```powershell
python tools/render_body_ik.py <run>/body_ik_check/corvo.json <run>/corvo-images --frames 0,3,5
```

The GPU suite exercises production GameXr through simulated dispatch on a WARP
device, native HUD child isolation, stereo compositing, mirror and render-size
transactions. Its JSON capture reports simulated frames and protocol cleanup.
These cover wheel/menu/focus transitions, art duplication boundaries and rig
symptoms reproducibly, but cannot reconstruct an unrecorded native game action.

Add a newly reviewed safe fixture to `tools/playground_manifest.json`, with
`source: tests/<name>.cpp` (or `.py`) and a `suites` array. No auto-discovery runs
unreviewed programs. Confirm that the fixture does not start the game/runtime,
inject desktop input, or write shared configuration before adding it. C++ uses
the common installed D3D11/Win32 libraries; specialized FidelityFX or MinHook
implementation builds require a reviewed build rule before admission. Python
fixtures must resolve read-only inputs from `__file__`, since cwd is isolated.

## Private desktop and actual game boundary

`--isolated-desktop` runs only the selected owned C++ fixtures on a unique Win32
desktop which is never made visible/active. The launcher accepts only a local
`playground-owned-*` executable copied by the runner, uses an owned job with
kill-on-close lifetime, and never calls SwitchDesktop or any input API. This
is an additional focus safeguard, not a security sandbox. Tests on that desktop
still consume shared hardware. The same-user desktop is not a save/config or
Steam isolation boundary.

Microsoft documents that [CreateDesktop](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-createdesktopw)
creates a desktop in the caller's window station and that
[STARTUPINFO.lpDesktop](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/ns-processthreadsapi-startupinfow)
selects the child's desktop. Only calling
[SwitchDesktop](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-switchdesktop)
makes it visible and activates user input. Therefore owned native offscreen
fixture processes can be isolated without taking the user's interactive desktop.

This does **not** establish that Dishonored 2 will keep rendering on a non-input
desktop: native focus checks, launcher/Steam behavior, swapchain occlusion and
game save paths must first be verified. Microsoft describes
[DXGI occlusion](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/dxgi-status)
as a presentation condition that applications may handle by skipping rendering.
The fixture command never launches a game. A separate guarded native startup
command prepares an owned candidate executable/dependencies and private save area:

```powershell
python tools/playground_native.py
# Explicit first-launch command, after reviewing native-results.json:
python tools/playground_native.py --launch --seconds 20
```

The default command only stages files and compiles the launcher. The explicit
launch starts a fresh copied game on the private desktop with a one-process
kill-on-close job. Fixed early arguments disable the platform backend, force
windowed 640x480 with the captured native `r_mode 0`, use installed assets read-only and redirect saves to the unique
`game/fs-save` directory. The native marker, pinned image SHA-256, staged
dependency hashes, module identity and exact owned PID are checked. Read-only
memory sampling verifies save/base paths, window/backend cvars and absent Steam
user/RemoteStorage interfaces. Save/backend/fullscreen/interface mismatches or
another game PID fail immediately. Actual dimension drift may settle for at most
three seconds. Mode-table initialization can wait within the overall deadline
while readiness remains false. Readiness requires three consecutive samples with
strict safety values, 640x480 dimensions and the captured flattened native mode
identity; a populated wrong mode fails immediately. The native getter maps mode
indices across successive monitor mode arrays. Its mode 0 is rechecked as 640x480
each run; the diagnostic selected-monitor value may be -1 in windowed mode.
An atomic `native-live.json` journal records nonce, timestamp, owned PID/path/base,
values, guard status and readiness, which is revoked when samples become invalid
or the process stops. A new minimal native config
in the private save area supplies configVersion10/bindingVersion44 and those
settings; user settings are never copied. The default duration is 20 seconds;
accepted range is 5..60 seconds.
No input, saved game loading, DLL injection or real XR session is part of this
startup command. An existing game prevents launch and is never terminated.

The report records checksums of installed files and the shared native/Steam
save baseline before and after. The private candidate can still consume shared
GPU/CPU resources; its startup has not established offscreen rendering, complete
native gameplay or visual quality. Root-coordinated native ABI/capture proof and
headset acceptance remain separate gates.

Current native-launch research (10 October 2026): `tools/run_demo.ps1` launches
through `steam://rungameid/403640` and discovers a process by name; that does not
preserve a private desktop/owned job. It also uses shared workspace flags.
The installed executable contains `SHGetKnownFolderPath`, `fs_savepath` and
`fs_basepath` strings. Read-only pinned-binary decompilation verifies that
`+set fs_savepath <absolute-path>` and `+fs_savepath <absolute-path>` are accepted
by the early startup parser before filesystem initialization. The filesystem
retains a nonempty override, and the native help explicitly describes overriding
the PC savegame path. This is static path-ownership evidence; the complete
native lane additionally checks the shared-save baseline at runtime. Changing USERPROFILE alone
does not redirect token-associated
[known-folder resolution](https://learn.microsoft.com/en-us/windows/win32/api/shlobj_core/nf-shlobj_core-shgetknownfolderpath).
Steam's [API overview](https://partner.steamgames.com/doc/sdk/api)
documents that `SteamAPI_RestartAppIfNecessary` may relaunch through Steam and
run a different installed executable. The guarded native lane rejects additional
processes and verifies the exact owned child. A future simulated gameplay lane
would require additional native input and capture ownership proof.
Never borrow `run_demo`'s process-by-name attachment or mutate shared Steam/cloud
settings to approximate isolation.

The session-only `+set net_usePlatformBackend 0` is a verified static native
startup gate. Pinned `FUN_1400e70c0` checks that cvar and returns with its platform-off
state set before the sole SteamAPI_Init import call at `0x1400e70fe`. The early
startup parser permits its flags `0x4001`. Native runtime verification is still
required: static evidence does not establish renderer startup or late
configuration lifetime. The guarded command checks those cvars repeatedly and
rejects nonzero Steam interfaces. Research exports live
under `artifacts/playground/native-cloud-gate/`, with the complete startup/save
decompilation in `artifacts/playground/native-save-console-final.txt`.
`tools/playgroundNativeSaveResearch.java` reproduces pinned-only read-only
Ghidra exports; any logger-flow repairs affect in-memory analysis metadata and
are discarded by `-readOnly`. Routine suites do not run Ghidra.

## Demonstrated offline runs

The v221 source completed all 43 reviewed checks on private desktops, including
rendered Corvo/Emily solver replays, in 75.4767 seconds at
`artifacts/playground/runs/20261011-001257-4310fe0f/results.json`.
The final run includes synchronized consumer JSON serialization, native movie
hash/edit/path isolation, bounded live-helper shutdown, material retry/restoration,
native weapon function-pointer compatibility, low-rate body/hold timing and
the half-size wrist/fault-recovery cases. This is offline protocol/math/resource
evidence; the report leaves native gameplay and headset acceptance false.

```powershell
python tools/playground.py --suite all --isolated-desktop --render
.\tools\build.ps1 -Paired -OutputName d2vr_render_probe_arm_v221.dll
```

The build command refuses to overwrite an existing numbered DLL. Use a new
version for later source changes; compile inputs are archived and verified
unchanged during compilation before a provenance pointer is published.

On 10 October 2026, the runner completed 20 fast fixtures in 18.34 seconds;
the original 7 GPU fixtures in 15.67 seconds; the newly integrated wrist fault
recovery plus GameXr simulated pair in 12.35 seconds; and both character replay
exports plus PNG rendering in 29.16 seconds. These are observed development
wall times, including MSVC environment setup and compilation, not game FPS.
Later range/attachment/ABI additions all passed in an 8.01-second focused run.

The same Corvo/Emily replay JSON hashes were reproduced across three runs.
A cached native UI layout invocation took 2.24 seconds total, with no compilation
and 0.029 seconds in the executable. Reports and PNG sheets are retained in
`artifacts/playground/runs/`; command lines and stage logs are recorded in each
report. The complete 36-check suite passed on private desktops with rendered replay
artifacts in `artifacts/playground/runs/20261010-223134-a77d4e8b/`.
Twelve runner unit checks cover fixture admission, cache invalidation, timeout,
guard journal, pending mode-table initialization and native flattened mode mapping.
Six capture-request unit checks use temporary files only, covering nonce/frame
ownership, partial files, pending-request exclusion and no-overwrite conversion.

The root-coordinated 20-second fresh native startup passed in
`artifacts/playground/native-runs/20261010-224259-f716e75d/native-results.json`.
Save/base paths, backend 0, absent Steam user/RemoteStorage interfaces, fullscreen
0, native mode 0 and 640x480 dimensions held; shared-save and installed-file
hashes were unchanged. Earlier dimension-reset experiments were stopped by the
guard and retained for comparison. HWND ownership/client size remains unverified
from the interactive reader, and no render quality, native save loading, gameplay,
VR simulation integration or headset acceptance is established by this startup.
