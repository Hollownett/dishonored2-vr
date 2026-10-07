# Dishonored 2 VR development

Target: the Steam edition of Dishonored 2, Quest 3 and Quest 2 through Virtual
Desktop/VDXR, with Steam Link/SteamVR as a second runtime. The intended result is
a full VR conversion with stereo, tracked weapons, connected arms, and a visible body.

**Current state: a tested in-game renderer diagnostic and an independent arm IK
solver. This repository does not yet contain a playable VR mod.**

## What works today

* `tools/probe_game.py`: read-only Steam installation check, executable hash,
  x64 PE inspection, imported graphics DLLs, and historical camera-signature scans.
  It defers scanning during Steam downloads/updates. A signature match is a
  candidate for investigation; it never authorizes a patch.
* `build/xr_probe.exe`: native x64 OpenXR runtime check. When a headset is
  available it reads the two-eye configuration and creates a D3D11 device on
  the runtime's requested GPU. Default mode does not start an XR session. With
  `--track`, it starts a short standalone session, submits solid-colour diagnostic
  eye textures, and samples head/eye/controller poses. It does not draw geometry
  or a virtual body and does not send input into the game.
* `src/arm_ik.hpp`: calibrated two-bone joint-position solver, with reach limits,
  selectable elbow side, and singularity handling. It has no game rig binding or
  bone rotations yet. It cannot make arms visible in Dishonored 2 by itself.
* `tools/sample_camera.py`: samples a unique historical camera candidate in a
  running copy using read-only process rights, after checking the executable
  hash, module identity, and loaded instruction signature. Its output remains
  explicitly unverified; camera movement alone does not prove a usable VR hook.
  It can also find typed camera objects and correlate fields with focused input.
* `build/d2vr_render_probe.dll` and `tools/inject_probe.py`: temporary exact-build DXGI
  diagnostic. It observes the live D3D11 Present path and reads the identified
  camera for ten seconds, then disables its hook. It patches Present temporarily;
  it does not write camera values or render VR. The DLL and its inert trampoline
  remain allocated until normal game exit to protect in-flight calls.
  `--stereo` temporarily selects the engine's two-view side-by-side metadata,
  sets a small separation in game units, records both prepared views, captures
  the backbuffer, and restores the prior values. This is a desktop diagnostic;
  it has no headset rendering or tracking integration.

No game files, saves, Steam settings, or system runtime selections are changed.
Build outputs and diagnostic reports stay in this workspace.

## Verified locally on October 7, 2026

| Check | Evidence and limit |
| --- | --- |
| Native x64 build | Own C++ code passes `/W4 /WX`; arm IK, camera codec, and hook restoration checks pass. Vendored MinHook C builds at `/W3`. |
| Binary-probe checks | Synthetic PE/manifest checks pass, including RTTI/vtable and unwind boundaries. |
| Installed game | Steam build `17576867`, file/product version `1.77.9.0`, 41,134,632-byte executable. |
| Exact executable | SHA-256 `b671f8249422862bf557d00aab2f4bb671ed4515cc10ad8a8a9decad84516180`. |
| Camera research | All four historical signatures match uniquely; the Ansel camera candidate is RVA `0x397bc60`. |
| Engine type information | One typed player camera and eight typed render views found in live memory; primary/secondary vtables agree. |
| Connected runtime | VirtualDesktopXR `1.0.10` detects Meta Quest 3; recommended eye size `2496x2688`; D3D11 device created on RX 9070 XT. |
| Initial live tracking | 46 frames returned valid head/eye poses. Session stayed synchronized/unfocused; no valid grip actions. This does not establish responsive head or hand tracking. |
| Focused Quest 3 tracking | Reconnected VDXR session reached focused state. Five-second test recorded 435 valid head/eye samples, 434 valid samples from each grip action, and 434 submitted stereo diagnostic layers. Head positions changed across logged samples. Eye RTV creation required an explicit typed format; the corrected build passed live. Only dark textures were submitted, with no game scene, arms or body. |
| Active gameplay camera | 100 focused samples, 90 with movement keys active: player-camera XYZ and orthonormal basis changed in all 100 samples. FoV at `0xcc/0xd0`, XYZ at `0xd8`, basis at `0xfc`. Game units, basis convention, and safe writes remain unverified. The Ansel global stayed zero. |
| In-game renderer diagnostic | 3,949 Present calls at 1920x1080, D3D11 feature level 11.1, 3,949 valid camera reads; hook disabled with no callback error and game remained responsive. Camera was stationary during that ten-second capture. This is a desktop frame observation, not stereo rendering. |
| Initial native stereo diagnostic | 4,142 frames had two prepared views 0.0640008 game units apart; mode and cvar values restored, hook disabled, game responsive. Captured pixels were covered by the pause menu. Distinct world images during gameplay have not been verified. |
| Gameplay stereo capture | External sample confirmed 65 focused frames and 48 movement-key frames. The in-game test observed 3,075 frames with two moving views, 0.0639975 game units apart. Both native settings and Present restored cleanly. The captured left half is corrupted; two usable eye images have **not** been established. |
| Backend-mode hypothesis | The gameplay test recorded 4,731 Present calls and 4,721 distinct two-view frames. The candidate query hook received zero calls and the captured world remained corrupted. Both hooks and native settings restored cleanly. The unused query hook has been removed from the candidate source. |
| Full-viewport single-eye diagnostic | The candidate recorded 5,424 valid camera samples and captured clean full-screen game images using left and right native offsets in successive phases. Both projection matrices were finite and both viewports covered 1920x1080. Present and native settings restored cleanly; game responsive. The user moved between phases, so this is not a synchronized stereo pair or a parallax/eye-order calibration. |
| Player visibility and model research | Read-only inspection resolved the native visibility command to live `idGameLocal`/`arkPlayer` objects. Player fields `0xf0` and `0x360` point to the same typed `arkSkinnedRenderModel`. The body component's primary/secondary vtables were identified in the executable; no direct pointer to that component was found in the player's first 8 KiB. No methods were invoked or player fields changed. Mesh visibility, bone binding and tracked arms/body remain unverified. |
| GPU pipeline comparison | Two traced frames produced 387 viewport/target events with no dropped events. Normal mode bound the backbuffer once; stereo bound it twice and selected the expected half-width viewports, but also ran full-width passes. A warmed gameplay capture remained corrupted. All three hooks/settings restored cleanly and game responsive. The exact defective pass is unverified. |
| Full-viewport paired candidate | A separately built candidate clones two native eye descriptors with full viewports and copies the first completed backbuffer before the second eye replaces it. Build checks pass. Its launcher timed out waiting for focused movement before loading the DLL; paired world images remain unverified. |

Reports are under `artifacts/`, which is ignored by Git. No playable conversion,
game stereo rendering, native body rig, or in-headset quality claim has been validated.

## Run the checks

Visual Studio 2022 with x64 C++ tools, Windows SDK, and Python 3.11+ are required.
The machine used for initial development already has them. Included dependencies
are pinned official OpenXR headers and MinHook source; see `THIRD_PARTY_NOTICES.md`.

From this repository in PowerShell:

```powershell
.\tools\build.ps1
python .\tests\probe_check.py
python .\tools\probe_game.py --game 'C:\Program Files (x86)\Steam\steamapps\common\Dishonored2' --output .\artifacts\game-probe.json
& .\build\xr_probe.exe 'C:\Program Files (x86)\Steam\steamapps\common\SteamVR\bin\win64\openxr_loader.dll'
```

Add `--track` to the runtime command for a five-second live tracking sample. This
briefly replaces the headset view with a dark diagnostic colour. A running
session needs to become focused before grip actions are active; an unfocused
session with zero submitted layers is insufficient for that check.

The OpenXR probe uses the registered runtime. Connect the headset through Virtual
Desktop with VDXR selected for the first test. For a Steam Link test, connect
through Steam Link and select SteamVR's OpenXR runtime through its own settings.
The executable does not change that selection. Keep the loader at its original
installed location; it is not copied into the game.

After installation completes, launch the game normally and use this optional
read-only research check from PowerShell while moving the character/view:

```powershell
$gameProcess = Get-Process -Name Dishonored2
python .\tools\sample_camera.py --report .\artifacts\game-probe.json --pid $gameProcess.Id --output .\artifacts\camera-sample.json
```

Add `--find-objects` for a read-only private-memory search capped at 512 MiB and
10 seconds. It requires both primary and secondary RTTI vtable pointers to agree
before reporting a camera/render-view object candidate. The scan is bounded and
can miss objects; matching an object's type does not establish its gameplay role.
Use `--scan-start` with the preceding scan's `next_scan_address` to continue the
search. Add `--object-report`, `--discover-fields`, `--diff-objects`, and
`--wait-for-input` to correlate candidate fields with focused gameplay movement.

After a successful movement capture from the same still-running process:

```powershell
python .\tools\inject_probe.py --pid $gameProcess.Id --camera-report .\artifacts\gameplay-focused-camera-diff.json
```

The injector supports only the executable hash above. It verifies the process,
typed camera, system loader implementation, and loaded diagnostic export before
starting the test. It uses the built DLL in this workspace; nothing is installed
in the game directory. Results are in `artifacts/dxgi-probe-result.json` and
`artifacts/dxgi-camera-probe.csv`. The current diagnostic can repeat after a clean
disable, provided its loaded DLL identity and restored Present bytes still match.
Restart the game before rebuilding its loaded DLL. A separately built candidate
can be loaded once with `--candidate` after the previous diagnostic confirms
clean restoration; its loaded identity is tracked separately. Rebuild neither
DLL while it is loaded. On a thread timeout the tool
retains any potentially referenced allocation rather than freeing memory in use.

For the temporary native stereo test, unpause the game and keep it focused while
walking/looking around. No prior camera-object scan is needed in this mode:

```powershell
python .\tools\inject_probe.py --pid $gameProcess.Id --stereo --wait-for-game
python .\tools\view_captures.py
```

The stereo diagnostic waits up to 60 seconds for foreground movement input,
then samples for ten seconds. It requires a changing camera before selecting
stereo. With `--wait-for-game`, the external launcher first waits up to five
minutes for focused movement before invoking the DLL.
The optional PNG converter uses Pillow and accepts only bounded RGBA captures
inside this workspace. It does not take screenshots of other apps.
The diagnostic separation is **0.032 game units per eye**, not a calibrated
metre-to-game scale or headset IPD. Native camera offsets alone do not establish
correct rendered eye order, projection, parallax, or comfort.
The original corrupted gameplay images are preserved in
`artifacts/dxgi-stereo-gameplay-first.png` and
`artifacts/dxgi-stereo-backend-query.png`.

To isolate the projection from the two-view path, the candidate supports a
single full-viewport eye descriptor cloned from validated native metadata:

```powershell
.\tools\build.ps1 -Candidate # Only while the candidate DLL is not loaded.
python .\tools\inject_probe.py --pid $gameProcess.Id --candidate --single-eye --wait-for-game
python .\tools\view_captures.py
```

This captures a baseline, the left offset after two seconds, then the right
offset after another two seconds. Without `--wait-for-game`, it starts immediately
and can capture a pause menu; keep the visible world unpaused for image validation.
`dxgi-left.png` is the left phase and `dxgi-stereo.png` is the right phase in this
mode. `dxgi-*-views.json` records viewports, positions and projection matrices.
The clean gameplay evidence is preserved as
`artifacts/dxgi-single-eye-gameplay-left.png` and
`artifacts/dxgi-single-eye-gameplay-right.png`. This narrows the corruption to
the two-view/viewport path; its exact cause remains unverified.

A successful diagnostic exit means the requested observations/captures completed
and changes restored; it does not evaluate image quality or certify a working
stereo conversion. These successive captures do not implement headset submission
or two eyes rendered from the same game update.

The pipeline diagnostic records one normal and one warmed stereo frame's D3D11
viewport and render-target changes (at most 2,000 events). It observes graphics
state without changing it; the existing native stereo override is still temporary.
Build this separate DLL only while it is not loaded:

```powershell
.\tools\build.ps1 -Pipeline
python .\tools\inject_probe.py --pid $gameProcess.Id --pipeline --stereo --wait-for-game
```

`artifacts/dxgi-gpu-trace.csv` identifies phase 1 as normal and phase 2 as stereo.
Result JSON records trace frames, dropped events, and restoration of all GPU
hooks in addition to Present/native settings. The DLL remains inert until normal
game exit after a clean test; a separately loaded diagnostic is checked against
the previous restoration report before starting.

The paired candidate tests a different compositor path: both native views use
the full viewport, and the first completed eye is copied on the GPU when its
backbuffer is unbound. Capturing requires two backbuffer passes before the same
Present, two distinct prepared poses, and restoration of all hooks/settings:

```powershell
.\tools\build.ps1 -Paired # Only while this DLL is not loaded.
python .\tools\inject_probe.py --pid $gameProcess.Id --paired --wait-for-game
python .\tools\view_captures.py
```

`dxgi-left.png` and `dxgi-stereo.png` are the candidate pair in this mode.
Their image quality and association with the prepared eye poses must be checked
before any stereo claim. This path has no headset submission or tracking writes.

If the signature does not match the installed build, the sampler refuses to run.
This tool needs a real running game; no live sampling was established by the
offline synthetic-file checks.

Game probe exit codes: `0` binary research available, `3` installation/executable
not ready, `1` invalid executable or failed probe. Runtime probe exit codes:
`0` device/configuration available, `3` headset unavailable, `4` required D3D11
extension missing, `2` usage error, `1` runtime/device failure.
Tracking mode returns `5` if live head/eye or either grip pose was unavailable.

## Research and implementation order

1. **Identify the exact executable.** Record the installed Steam build and
   SHA-256. Inspect candidate camera references and distinguish the photo-mode
   camera from the active gameplay view. Historical offsets are never assumed
   correct for another executable.
2. **Establish the rendering path.** Find the engine render-view construction,
   projection, frame scheduling, and D3D11 texture ownership. First prove two
   separate eye images with the correct per-eye position and asymmetric FOV.
   Then synchronize those images and their head poses with OpenXR. A desktop
   mirror, duplicated mono image, or depth reconstruction is insufficient.
3. **Head tracking and comfort.** Calibrate game units to metres; separate head
   rotation from character yaw; handle roomscale collision and recentering;
   suppress camera bob/forced roll. Check loading, death, menus, and cutscenes.
4. **Hands, weapons, and connected arms.** Find the player rig, bone names,
   skinning matrices, animation update timing, and weapon attachment transforms.
   Bind the tested IK solver after the game's normal animation. Keep each wrist
   orientation and aiming ray consistent with its actual weapon/power.
5. **Visible body and gameplay.** Preserve an appropriate in-game body mesh and
   leg animation, estimate torso/hips from head/controllers, and hide the head
   from the first-person view. Add calibrated shoulders/clavicle behavior.
   Head/controllers alone do not track the real hips or feet; full-body trackers
   would need an additional input path. Handle climbing, possession, takedowns,
   sword swings, blocking, and animation takeover/return.
6. **Usability and release testing.** World/wrist HUD, weapon wheel, journal,
   seated/standing settings, turn options, save/load, uninstall, and unknown-build
   rejection. Test both protagonists and both Quest models. Measure frame time
   in representative scenes and verify stereo, tracking, aiming, body continuity,
   and comfort in-headset before describing the result as playable or polished.

These are dependent engineering milestones, not a claim that a complete conversion
can be produced or validated in one session. The largest unknowns are the stereo
frame path and safe access to the player skeleton.

## Relevant existing work

* [CircuitLord's Titanfall 2 conversion](https://github.com/CircuitLord/CircuitLordVRModInstaller)
  documents full-body IK, stereo, manual reloads, and campaign controls. Its
  installer is a useful quality reference; it is not a Dishonored 2 engine adapter.
* [Dishonored VR](https://github.com/VR-Stereo-Hub/Dishonored-VR)
  targets the first game's 32-bit D3D9/UE3 path and documents tracked floating
  hands. Its VR design is relevant, but its game hooks do not transfer to the
  second game's engine.
* [Arkane's engine presentation](https://media.gdcvault.com/gdc2017/Presentations/Sadoulet_Xavier_Couvidou_Laurent_Taking_Back_What.pdf)
  identifies Dishonored 2's Void engine and its id Tech 5 starting point.
* [cosmic-tangerine's Dishonored 2 developer update, September 15, 2026](https://www.reddit.com/r/dishonored/comments/1wh88hg/got_dishonored_2_powers_working_in_vr_mod_update/)
  reports arm IK/visible body and hand-aimed powers, with stereo, detached sword
  control, and stealth takedowns still outstanding at that update. This is a
  creator report, not a release we have tested. No reusable public source was
  located in the initial search; this does not establish that none exists.
* [Frans Bouma's published camera research](https://github.com/FransBouma/InjectableGenericCameraSystem/tree/master/Cameras/Dishonored2)
  supplies Ansel/camera signature candidates under BSD terms. A photo-mode
  camera is only a starting point for gameplay tracking.
* [Historical Dishonored 2 cvar dump](https://gist.github.com/fakuivan/8910a208fc88f831f06b047d0955e4fe)
  includes `stereoRender_*`, player-camera, and render-view names. Strings may
  survive without a working stereo implementation; live rendering must be proven.

The arm test covers 20,000 deterministic random poses plus reach boundaries,
degenerate pole/target cases, mirrored elbow selection, and invalid calibration.
The binary-probe test uses synthetic files to verify bounds, imports, multiple
matches, executable-section filtering, and RIP-relative target decoding. These
checks validate development components, not Dishonored 2 gameplay.
