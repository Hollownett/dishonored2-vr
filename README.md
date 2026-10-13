# Dishonored 2 VR development

## Current test build (v205, 10 October 2026)
How to play: [docs/PLAYING.md](docs/PLAYING.md). One click: run `launcher\dist\DishonoredVRLauncher.exe` (a single self-contained
file: it installs the mod, the Khronos OpenXR loader and art from your own Dishonored 2 install into `%LOCALAPPDATA%\DishonoredVR`,
then LAUNCH VR starts the game and the mod; see [docs/LAUNCHER.md](docs/LAUNCHER.md)). Developers can still use `.\tools\run_demo.ps1 -Launch`.

- **Combat hands:** controller tracking remains active during ordinary attacks and blocks. Verified authored full-body actions retain native ownership. Arms and forearms are hidden; calibration is deferred.
- **Interaction:** either hand can grab with a weapon equipped. Per-hand native selection has fresh-query and grip ownership gates and stable native range-slot routing. Custom ink lines and circles are removed. Reusing native interaction prompts beside objects in the game scene remains pending.
- **Settings:** choose Pause > VR Settings. Settings are composed into the native menu image and use its pointer, without a separate settings XR panel.
- **World UI:** the Quick Access Wheel and journal/notes use isolated native art on panels placed in front of the head. X opens the wheel; a Menu hold opens the journal. Objective markers still need individual native-art rendering at their game-world targets.
- **Rendering:** separate-eye FSR Native/Quality/Balanced, deferred execution and restoration checks pass. Native scale-cache ownership now permits reduced-resolution FSR input; a prior v201 native-game run verified Balanced upscaling from 853x507 to 1273x756 per eye. Saved user settings are preserved. `tools/run_demo.ps1 -Fsr4` requests the new experimental AMD FSR 4.1.1 DX12 bridge, with genuine provider queries, stereo GPU checks and FSR 3.1 fallback. FSR 3.1 remains the default.

The v205 full build and GPU checks pass, with a verified source record from an
isolated compiler tree. It fixes the observed focus reset from a 2496x2688 native
viewport to the desktop's 841x485 cache: live focus-return testing recorded one
automatic recovery, matching full-size dimensions and no deferred corrections.
Headset visual acceptance remains pending. Live v204 verified genuine FSR 4 in
both eyes without fallback, but its Quality cadence was about 14 ms per frame;
90 Hz performance and stable grabbing remain unfinished.
See [the current build report](artifacts/feedback-v205-build-validation.json)
and [FSR 4 compatibility assessment](docs/fsr4-compatibility.md).
v204 is retained as the previous test candidate; v203, v200, v199 and v193 are also retained.

The history below is the earlier development record.

Target: the Steam edition of Dishonored 2, Quest 3 and Quest 2 through Virtual
Desktop/VDXR, with Steam Link/SteamVR as a second runtime. The intended result is
a full VR conversion with stereo, tracked weapons, connected arms, and a visible body.

Current status, 9 October: the diagnostic candidate is v98
(`d2vr_render_probe_arm_v98.dll`, SHA-256
`d20dc74b66179a5353c9494b3e4e802a587a41016623b79d5bf4d58d8f91c615`). Its
archived build-source manifest and ZIP match the loaded DLL identity, and the
build records that source stayed unchanged during compilation; it does not
claim compilation from a frozen snapshot. The candidate and normal launcher
remain blocked; full-demo/headset acceptance are false and tracked arms remain
off by default. Accepted locomotion and turning are preserved.

A separate v103 run records one actual, non-simulated Quest 3/VirtualDesktopXR 1.0.10 session with 3,090 native eye pairs/gameplay layers at the recommended 2496x2688 eye size. The fit was accepted and active at stop; native camera, snap-turn, and head-anchor observers recorded 1,411, 2,298 verified, and 1,034 samples respectively. Left arm reach clamps were 118 versus zero right, with cause unresolved; the arm-upload trace contains no calibrated/matched rows after fit acceptance. The physical-grab request produced no physical-grip presses or grab samples, and weapon tracing was disabled. Diagnostics ended with zero faults, no callback failure, and all 23 restoration checks true. This is a bounded runtime session, not headset visual/comfort acceptance; the v98 candidate remains blocked and tracked arms remain off by default. Evidence: `artifacts/native-quest3-preview-v103-20261009.md`.

A v103 active-fit options-return trial records 1920x1080 -> 1600x900 -> 1920x1080 and operator-reported Resume. XR timing shows 2,725 subsequent 1920x1080 gameplay rows, 2,723 submitted; native camera traces also continue in gameplay and the fit gate remains active. The saved config backup hash matches the reported restored hash. Arm-upload CSV ends at present 97, before options; it has no calibrated/matched post-Resume records, so this is not per-frame arm proof. Weapon tracing was disabled. v103 is not promoted; v98 remains the blocked candidate. Evidence: `artifacts/native-calibrated-options-return-audit-v103-20261009.json`.

The v98 tracked-arm diagnostic archive records 1,606 complete typed-equipment
dry leases (6,424 rows), one fit acceptance/reuse, no output changes, and no
native weapon attachment writes. All leases join source receipts; bounded
model observations cover 1,154, including 1,153 site-5 GPU joins, while 452
accepted receipts have no bounded model row. A separate ownership capture
matches 328 leases. This is diagnostic evidence, not native weapon-writing or
weapon-gameplay proof. A separate v98 options trial records 7,184 timing rows
through one 1920x1080 → 1600x900 → 1920x1080 round trip with restoration checks
passing; its submitted-by-dimension counts total 7,171. It ran with tracked
arms disabled and does not establish headset image quality.

A v102 run now records a calibrated startup fit, a Pause/manual-load owner
replacement, then death-state logic, loading suspension, a new gameplay owner,
and same-epoch fit reuse with a same-tick pose-gate update after Restart Latest
Checkpoint. This is one bounded same-session calibrated death/restart result;
the menu clicks/save identity are operator-reported, and four nearby native
position reads are not frame-synchronised. The separate v97 Pause > Load trial
did not include death; the older v96 death interval began after death without
an accepted fit. v102 remains diagnostic evidence, not candidate promotion.
v99 captured 38 complete ordinary-Lean
worker chains and 17 join pairs, but dropped 5,190 lifecycle rows and observed
no load/shutdown. v100 exercised an operator-reported actual Pause > Load and
resumed gameplay with 1,587 eye pairs, no faults and no callback error. Its
passive lifecycle observer captured 224 rows but dropped 5,188, reported one
lifetime rejection, and recorded no shutdown/fence-complete row; it does not
prove a complete load fence and is not promoted to candidate.

The separate v96 ordinary-contact grab remains the accepted baseline: two
nearby-prop pickups, 135 motor samples, two drops, zero ownership loss/clamps,
and 0.276198 native-unit initial gap using simulated controllers. Natural
fists/fingers, tracked weapons and powers, native weapon writes, physical head
translation, roomscale collision, whole-gameplay acceptance, Quest comfort and
visual acceptance, and native world metric scale remain unfinished. Evidence:
`artifacts/development-validation-v102.json`,
`artifacts/native-calibrated-death-restart-audit-v102-20261009.json`,
`artifacts/development-validation-v100.json` and
`artifacts/development-checkpoint-2026-10-09.md`.

Historical v96 baseline detail (retained for its trial-specific evidence): v96 added bounded native diagnostics while keeping the normal launcher blocked.
The clean run used simulated VR with tracked arms and palette copying disabled;
it exited cleanly with zero fault records, 1,921 native eye pairs/gameplay
layers, and restored hooks and values. A separate calibrated Sword trial
accepted one fit and recorded 328 completed dry leases with no output changes
or native weapon writes. Typed reads found both carry states inactive in 299
focused empty-hand samples; this was not a carry action. A separate Y-first
lean capture has 464 stable-window reads, including 169 active-lean reads with
fixed feet, and an inactive final window. Its command archive does not record a
mouse Resume click. The v95 calibrated ordinary-grab result remains prior
baseline evidence; v96 now also has its own calibrated ordinary-contact grab
trial. That trial acquired and released one nearby native dynamic prop twice,
recording 135 physics-motor updates, zero ownership losses or reach clamps, and
no ranged frames. The ordinary contact gap was 0.276198 native units. Analysis
measured 0.23099 vertical travel and 0.01588 maximum target error; world metric
scale remains unverified. It used simulated controllers and does not establish
natural fist appearance or weapon grabbing. Natural fists, tracked weapons/powers,
physical head translation, complete gameplay, and headset comfort/visual
acceptance remain unfinished. Evidence:
`artifacts/development-validation-v96.json`.

v91 passed actual native Game Over -> Restart From Last Checkpoint -> stereo
gameplay with VR active on the death screen. In a separate trial, required calibration gates
tracked arms from startup and opens its setup page on the first Pause; native
measurement and same-session checkpoint fit reuse passed. Hold-to-lean release
also passed: 600 read-consistent samples, fixed feet, steady heading during lean,
and an inactive final state with the original camera position restored.
The active Sword pose producer now has exact frame-qualified attachment/upload
matches. Weapon retargeting and physical head translation remain disabled;
natural fists and complete gameplay still need work. Evidence:
`artifacts/development-validation-v91.json`.

v82 fixed a reproduced D3D11 context-dispatch transition: the same immediate
context can change method entries after Map or context-state operations. The
observer now accepts verified new entries using retained immutable trampolines.
Actual native mantle rendering survived two dispatch changes and submitted
41 tracked authored-camera frames. This addresses the observed observer failure;
the calibrated pre-death transition and headset runtime return need further verification.

v83 verified the long Menu press opens the native Journal without a Pause event,
after ordinary gameplay stereo capture. The first attempt opened the Journal
before the required capture and failed that diagnostic gate; the delayed attempt
passed with matching native menu pixels and clean restoration. v84 then verified
opening the actual office escape window and native climbing to the exterior ledge.
Its default-off passive weapon-attachment trace restores correctly but has no
equipped-weapon sample yet. Uncalibrated scripted arms show some reach clamps;
these runs are not hand alignment or headset acceptance. Evidence:
`artifacts/development-validation-v84.json`.

v85 captured the legitimately obtained Sword's native right-hand grasp and
577 qualified passive socket samples. The source is occupied Sword, not a
certified Empty fist. v86's first directional lean trial changed native look
instead of leaning and was rejected. v88 maps Quest Y plus right stick into
the native Y plus left stick; the actual game then shifted the viewpoint with
constant heading/pitch. Combat outcomes and headset comfort remain unverified.

v89 completed the paused calibration wizard, loaded the latest legitimate Sword
autosave, and returned to tracked stereo in the same XR session. Its event log
records loading suspension, replaced player/arm owners, exact matching resident
avatar resources, measured-fit reuse and pose-gate restoration in that order.
There were no dropped events, callback failures or recorded faults; clean stop
restored hooks/settings. Resource-template checks have a 500ms bound. This is
saved-checkpoint evidence; death-menu reload remains open. Private run:
`artifacts/runs/20261009T015057.025773Z-pid44016-arm-v89`.

v66 passed an actual native loading-to-gameplay simulation. v67 survived a
saved-checkpoint load, returned to gameplay in a subsequent diagnostic, and
completed a separate resolution change/revert with three successful native
resize calls and resumed stereo gameplay. Those runs recorded zero first-chance
faults and restored hooks/settings. They do not yet cover death-menu reload,
real runtime/device loss, or headset comfort. Evidence is recorded in
`artifacts/development-validation-v67.json`.

v69 passed the explicit paused VR calibration wizard in the actual game and
completed two surface-contact prop pickups, 131 native motor samples and two
releases, including focus-loss release. It recorded no ownership loss or faults.
The maximum target error was 0.0113 native units; native world metric scale and
physical headset reach remain unverified. See `artifacts/development-validation-v69.json`.

v71/v72 passed native stereo rendering with graphics ownership and eye-source
epoch guards. Setup frame N is consumed by Begin frame N+1, as verified from
native trace data. The guards retain the actual swapchain and verify device,
immediate context, window, thread and resize dispatch before accepting eye data.

The explicit paused VR calibration wizard has visible recenter/rig
invalidation and cancellation. It fits measured arm reach and shoulder offset;
absolute floor height and native world metric scale remain unverified. Tracked
arms are experimental. Fingers now preserve authored native poses unless a
matching native empty-hand reference and occupancy evidence are available;
there are currently no installed accepted fist references. Captured native sprint
poses were rendered and measured; both hands remain open, so they were rejected
as fist endpoints. Native left weapon/power aiming remains a separate default-off
experiment. v73 made zero aim writes and failed its success gate because its
test configuration omitted native eye capture; it restored hooks without faults.
v74 corrected stale left-aim intent cancellation and passed 1,149 actual native
aim writes/readbacks with zero overlap, write failure or recorded fault. Owned
camera reads after cleanup confirm normal native aim regeneration. No projectile
or power outcome is implied; see `artifacts/development-validation-v74.json`.

v76 passed the revised graphics guards and unmodified native block-pose capture.
Block left both equipment slots Empty and produced no closed-hand reference.
v77 deliberately threw a handled worker exception after four XR gameplay
frames. Render-thread resource retirement, all hook restoration and a second
session in the same game process passed. That session picked up/released one
prop with zero ownership loss or recorded faults. The development fixture placed
the right controller 2.68 native units from its shoulder, causing expected arm
fallback; it does not validate player reach or arm alignment. Death-menu reload
and complete gameplay remain open. See `artifacts/development-validation-v77.json`.

v78 verified finite controller-script movement and native crouch in the actual
game. v79 then completed two pickups/carries/releases using the ordinary raw
controller pose: zero ranged hand replacements, 134 motor samples, no ownership
loss or recorded fault, and 437 tracked poses per arm with zero reach clamps.
Exact first-pick wrist extension was 0.891 times the native limb length; archived
early carry stayed 0.876–0.891. The second pick lacks an exact archived arm
source, and physical headset alignment remains unaccepted. Native block input
arbitration and authored grip shape work are still active. Evidence:
`artifacts/development-validation-v79.json`.

v80 passed ordinary grip ownership in the actual game: two physical-owned
squeezes, zero native-owned squeezes, two pickups/releases, 132 motor samples,
no ownership loss or recorded fault, and zero arm reach clamps. Native armed
block and takedown outcomes remain pending. A separate scripted mantle produced
46 authored-camera XR frames with submitted tracked stereo pairs. v81 captured
distinct native eye images during a mantle transition, then stopped when a
graphics observer incorrectly treated a legitimate context-dispatch change as
an owner change. The game survived and hooks/settings restored. v81 is not an
accepted candidate. This was reproduced with independent WARP and hardware
Direct3D contexts; the observer lifetime fix and checkpoint calibration reuse
are under validation. See `artifacts/development-validation-v80.json` and the
v81 run `artifacts/runs/20261009T004133.050967Z-pid44016-arm-v81`.

The following v56 account is historical development evidence:

The v56 demo candidate added a simulated OpenXR/D3D11 development lane,
native controller-pointer Resume tests, and experimental right-grip physical
grab/drop/throw. Run `tools/run_demo.ps1` with the game open and Quest connected;
Ctrl+C stops and restores the diagnostic. `tools/run_demo.ps1 -Simulated` runs a
12-second automated test with real game rendering and real native prop physics.
It clicks Resume through the controller ray/trigger, lifts and throws a nearby
eligible prop, repeats pickup, loses/reacquires XR focus, clicks Resume again,
and restores pause if initially paused. It needs an eligible dynamic prop near
the player. Simulation is explicitly marked in the result and timing report.

v56 passed the native 12-second simulation: two candlestick pickups/releases,
139 motor samples, about 23 cm of real object lift, less than 1.9 cm maximum
target error, and four native mouse-button events for Resume before/after focus
return. There were no callback errors, recorded faults or ownership losses;
hooks and settings restored, and the game returned to pause. The combined v55
arms/grab test also restored cleanly; its floor-level simulated hand reached the
arm solver's clamp, so that run does not establish physical hand alignment.
Evidence: `artifacts/development-validation-v56.json` and the referenced runs.

Right grip acquires a standalone `idDynamicEntity` rigid body with a verified
native static-mesh render model whose centre is
within 30 cm of the tracked hand. A bounded velocity motor retains native
dynamic physics and collisions; releasing transfers filtered hand velocity.
Focus/tracking/ownership loss cancels the motor. Static/keyframed/attached or
compound bodies, pickups/characters and native-held bodies are excluded.
This first integration does not cover every interactable object or weapon.
Controller interaction-ray routing remains a separate experimental option.

`-ExperimentalArms` additionally enables native tracked-arm palettes. Tracked arms remain opt-in because deformation and
physical grip alignment have not passed headset acceptance. The new pipeline
predicts the pose for the next submitted frame, matching the one-frame native
render pipeline; automated checks cover normal and missed-frame schedules.
Simulation also covers reference changes, hidden/focus returns, authored-camera
panels, resolution controls and a 1,000-frame D3D11 soak. It does not establish
headset comfort, compositor pacing or image quality. Full demo acceptance is
still incomplete: image stability/AA, tracked arms/body visibility, broader
interaction and immersive authored cameras remain open.

Headset result from 8 October: v38 improved camera behavior but regressed
walking/menu input and failed image-quality acceptance. v39 corrects the native
pad-only window gate before joystick serialization. The user confirmed walking,
jumping, crouching and menu-button operation with the PC game window inactive.
Pointer/trigger Resume after returning to VR still fails, and climbing/keyhole
camera changes briefly show black. Both tests restored their hooks and settings.
The actual v38 scene geometry and submitted eyes were 3102x3208, with SMAA
modifying the submitted pixels; high output size and a running AA pass do not
establish adequate image quality. Its first camera trace averaged 54.8 rendered
pairs/s, with identical stereo anchors; headset refresh timing remains unmeasured.

v40 is built with recognition of the native Iggy wrapper's exact cursor-reset
pair (`0x7149f2ca`, initialized by native `0x34200`). Unknown/nonfinite cached
values and invalid owners are still rejected. Owned game-controlled camera modes
now have a stationary VR-panel path using the current native image, returning to
tracked stereo on ordinary gameplay. These two changes still need headset
acceptance. They do not fix image quality or implement tracked hands/grabbing.

v41 retains those fixes and adds a bounded `xr-frame-timing.csv` trace (20,000
frames), archived with each diagnostic. It records CPU time outside Present,
the preceding desktop Present, XR wait/begin, locate/input, transfer and submit,
plus each rendered eye's prediction time and pose-locate CPU timestamp.
Run `python tools/analyze_frame_timing.py artifacts/runs/<run>` after clean
shutdown. The report separates tracked gameplay, native menus, settings and
authored cameras; cadence comparisons exclude focus/epoch/size/AA transitions
and diagnostic readbacks. CPU spans include driver blocking and are not GPU
execution times. OpenXR's predicted period can differ from hardware refresh,
so the report does not claim headset scanout or compositor-drop measurements
([Khronos frame timing specification](https://github.com/KhronosGroup/OpenXR-Docs/blob/main/specification/sources/chapters/rendering.adoc)).
The build/check suite passes. v41 has not been injected or tested in the headset;
the latest read-only VDXR check still reports `HEADSET_UNAVAILABLE`. Timing and
image-quality improvements remain unproven. v39 and v40 binaries are preserved.

The private offline body replay now combines Emily's 20-joint torso/legs and both
69-joint arm meshes using the captured native model frames and eight-weight
skinning. Replayed vertices agree with the native matrices within 1.6e-7 metres;
both arm roots match torso spine 3. Controller alignment, body visibility and
tracked deformation are unverified. Run `tools/render_body_rig.py --pose-report
artifacts/native-fullbody-layout-23072-readonly.json` for the private diagnostic.
Extracted game assets are ignored artifacts and must not be redistributed.

**Historical rendering state (v22, before the later tracked-arm work): stereo, head rotation and controller locomotion had user
confirmation. The symmetric-frustum rendering fallback greatly reduced the
reported doubled edges. Native/OpenXR captures match at 3102x3208 per eye;
full-size colour/depth geometry has been observed; not every scene pass is verified.
The `arm_v14` controller-driven native palette substitution produced distorted,
unusable arms and was rejected by the user. That game process subsequently
exited with an access violation; its cause is unknown. The tracked-arm mode
is disabled for the next comparison. An unchanged palette-copy test had
previously restored cleanly; offline IK checks do not prove correct skinning.
The original graphics settings were Very Low textures/models and FXAA Low.
They are now backed up and a High textures/models/shadows, FXAA High and
zero walking-bob profile has been applied. Startup reads confirm textures,
models, FXAA and bob; the requested skip-shakes value was not accepted.
The session-only `--stable-camera` cache override now activates bob=0 and
skip-shakes=1, with live reads confirming both values and clean restoration
in the `arm_v17` and `arm_v18` runs. `arm_v17` stopped before rendering because
OpenXR changed the D3D11 context dispatch. The next build binds observers to
the game's context after graphics setup; it ran but its obsolete render-target
startup gate prevented stereo, producing the user's black-screen report.
`arm_v19` removes that gate specifically for native eye-boundary capture.
It submitted 46,443 XR layers, including 8,208 gameplay layers. Matched tracked
eye captures and observed colour/depth geometry use 3102x3208; this does not
verify every scene pass. The user confirmed a much sharper image and good head
movement, while reporting turn, jump and crouch shake. The run restored cleanly
and is the sharper rendering baseline. `arm_v20` changes level-heading extraction
to remain stable across vertical native pitch and adds the command-line
`--resolution-multiplier` backend. The 1.2 comparison delivered 3722x3850 native/XR frames and full-size scene geometry, then restored cleanly. The user still reported jagged edges. `arm_v21` adds independent SMAA 1x Ultra processing, with no sharpening or temporal history. The GPU tests and actual stereo transfers pass; visual acceptance is pending. Its game process ended without a final cleanup report. `arm_v22` adds paused-menu Y graphics settings: resolution plus/minus, SMAA toggle and right-controller pointing. Tests pass; its live panel and resizing still need validation. Connected body,
usable independent hands/weapons, interaction aim and prompt alignment,
menu pointing and physical grabbing remain unfinished. The full prototype
goal was active at that point.**

## Playable demo plan — 8 October 2026

The target is a repeatable scene from the installed Steam Dishonored 2 game,
using the existing manual save. A standalone room may exercise the same VR core
as a test, but the delivered demo must run the actual game. Completion means
the player can enter VR, recenter, look and move naturally, snap or smoothly
turn, use readable menus, control both hands, and pick up/carry/release a native
object. Connected arms and a visible body remain part of the requested demo.
The current renderer is a baseline, not evidence that these features are done.

Research is pinned under `artifacts/research/playable-demo-2026-10-08/`.
Each donor has a commit, original license and source-file hashes. GitHub search
snippets are not sufficient evidence for either implementation or permission.

| Reference | Reuse decision | Dishonored 2 boundary |
|---|---|---|
| [Dishonored VR](https://github.com/VR-Stereo-Hub/Dishonored-VR/tree/b15e4d32a720bd4cfdb5ce4d600f3f8fba8d2cec), zlib/libpng | Adapt its snap-turn edge/rearm/expiry policy; directly reuse its pure parent-relative finger screw-blending mathematics. Follow its controller/menu ownership, measured palm calibration and animation handoff practices. | UE3/D3D9 addresses, camera influences and mesh formats do not apply to Void/D3D11. Its floating-hand mesh cut is not the requested connected-arm/body solution. |
| [BioShock trilogy VR](https://github.com/VR-Stereo-Hub/bioshock-trilogy-vr/tree/41f004bc6a1e4fa8be0d0e0749f9ac5a0453e75e), MIT | Use its D3D11/OpenXR frame, rendered-pose tagging, focus recovery and replay structure as the runtime reference. Port small applicable pieces into the existing bridge rather than replacing the verified renderer wholesale. | Engine camera/weapon bindings must be measured in Void. The reference documents renderer-dependent pose-generation lag; its constant is not a Dishonored 2 setting. |
| [FEAR2VR TurnController](https://github.com/praydog/FEAR2VR/blob/d060da9d3c2c67b0ca85d489e47f304b873dcc73/src/mods/TurnController.cpp), MIT | Follow heading feedback, input-settle checks, liveness cancellation and a native look API. Verify a requested turn is actually retained by the engine. | Its LithTech API, sensitivity and correction gains are not portable. A synthetic stick pulse cannot establish a fixed angle in Void. |
| [Khronos OpenXR frame guide](https://github.com/KhronosGroup/OpenXR-Guide/blob/main/chapters/frame_submission.md) and [hello_xr](https://github.com/KhronosGroup/OpenXR-SDK-Source/blob/main/src/tests/hello_xr/openxr_program.cpp) | Follow the standard frame loop, same-time eye/head/controller location, action profiles and swapchain ownership. | The application must submit the pose actually used for the game image, even when engine rendering spans frames. |
| UEVR `4ee5c6b6162dee2291fc75f9dfc57667f6d45a2d` | Research reference only in this plan: the checked root LICENSE reserves rights; no source-copy permission is assumed. | Its Unreal injection is not a Void adapter. |

Implementation order and acceptance gates:

1. **Reproducible setup and baseline.** Keep the accepted `arm_v19` rendering
   build and the backed-up game settings. Launch through Steam, select the
   manual save, record the executable hash/runtime/eye recommendation, and
   use a single bounded diagnostic owner. Deliver a launcher/profile that
   exposes resolution, recentering and bindings. Preserve preferences across
   restart; distinguish requested and effective render size.
2. **Camera ownership and frame timing.** Record one generation identifier for
   native gameplay heading, collision-safe eye origin, head/controllers and
   each rendered eye. Compare the source player frame with the final animated
   camera during walking, right-stick turning, jumping and crouching. Attribute
   roll/landing/height smoothing individually before suppressing it. Keep
   physical jump/crouch/collision behaviour. Gate: no mod-added roll or repeated
   head offsets, correctly tagged eyes and stable desktop/VR return.
3. **Proper VR turning and movement.** Reuse the reference snap detector
   (threshold 0.6, rearm 0.3, one step per push, stale-step expiry). Offer 30/45
   degree snap and time-based smooth turning. Bind the target to a verified
   native gameplay-heading writer, once per fresh game update. Body, collision,
   aim and rendering share that heading. Gate: four 90-degree turns return to
   the initial heading; a held stick/menu transition cannot add unintended
   steps; aiming and forward movement follow the new heading.
4. **VR menus and quality.** Extend the built `arm_v22` panel with persisted
   settings and recenter/turning choices. Right-hand aim ray and trigger operate
   the panel; A/B and sticks remain usable for native menus. Consume menu input
   before gameplay routing, release held buttons on focus changes and display
   the effective per-eye resolution. Gate: live quality changes, pause/resume
   and two desktop/VR switches without black images or leaked combat input.
5. **Tracked hands, connected arms and body.** Reuse the upstream finger
   interpolation rather than inventing another curl algorithm. Measure the
   native inverse binds, actual blend indices/weights, palm basis, unit scale
   and shoulder/elbow helper roles. Use one world-space tracking origin for
   both head and hands. Retain native animations for climbing/takedowns.
   Gate: neutral-pose skinning, independent wrist rotations, grip/fist closure,
   stable elbow bend and shoulder attachment in the actual mesh. The rejected
   `arm_v14` path stays disabled until these mesh checks pass.
6. **Aim and native interaction.** Use one calibrated ray for controller aim,
   the reticle, the interact prompt and the native action. Bind a native movable
   object to the hand, with bounded reach, collision, release and measured
   throw velocity. Gate: visibly grab, carry, drop and throw the same object;
   an X-button pickup alone is not physical grabbing.
7. **Playable demo acceptance.** Repeat the same saved room: recenter; inspect
   both hands; turn; walk; crouch/jump; open/close settings; change resolution;
   interact and carry/throw; return from desktop; save/quit normally. Record
   real headset feedback and frame timing. Code tests or synthetic inputs do
   not establish visual comfort, mesh quality or physical interaction.

The source-reused snap policy (`src/vr_turning.hpp`) and finger blend adapter
(`src/finger_pose.hpp`) now pass replay/geometry checks. The new read-only
`--player-camera` sampler decodes the owned camera/controller state with type,
owner and generation guards; its synthetic guard checks pass. Read-only live
samples now confirm radians, the owned physics delegate, and agreement between
physics heading, entity orientation and native aim. The `arm_v25` passive run
observed 3,623 native camera and 3,607 physics updates without changing heading;
all observers restored, with no recorded faults. Updates move across worker
threads, so the adapter does not assume one fixed simulation thread.

`arm_v26` connects the reused snap policy and time-based smooth turning to
the native physics update. `--vr-turning snap --snap-degrees 45` or
`--vr-turning smooth --smooth-degrees-per-second 90` requires `--controllers`.
The default remains the previously tested native gamepad path. The adapter
preserves all six observed physics-step arguments (including float timestep
bits in EDX), applies a delta through the engine's yaw function, and checks
that both the heading and entity orientation retain it. It cancels stale,
menu/wheel and recenter input; disagreement stops further writes. Tests cover
four 90-degree turns, held-stick/replay rejection, and equal smooth speed at
20/45/90 native updates per second. These tests do not prove headset comfort.

The first `arm_v26` headset trial stayed in OpenXR SYNCHRONIZED and
submitted no frames. Its later continuous run submitted 781 stereo layers,
including 780 gameplay layers, at 3102x3208. One native 45-degree turn changed
the physics heading exactly, but the immediate entity-transform check still
saw the previous heading and stopped the diagnostic. All hooks/settings
restored and no first-chance faults were recorded. The native callback
`0x800f70 -> 0x754ea0` copies a staged transform through `0x18e050`, then
schedules publication through `0x7521f0`. A post-step entity read is therefore
not a same-phase acknowledgement.

`arm_v27` waits up to 500 ms for that publication without replaying the turn.
It retains overwritten-heading, ownership, mode and timeout checks, and maps
Quest right-stick direction to Void's forward/left/up yaw convention. Portable
feedback checks pass; actual turn retention and headset comfort remain to be
verified. The continuous comparison waits for native gameplay before launch.
Evidence: `artifacts/runs/20261008T142138.687452Z-pid23072-arm-v26/`,
`artifacts/re-toolchain/direct-ghidra/demo-physics-transform-callback/`,
`artifacts/re-toolchain/direct-ghidra/demo-physics-transform-queue/` and
`artifacts/re-toolchain/direct-ghidra/demo-physics-transform-dirty/`.

`arm_v28` extends the existing paused Y panel with snap 30/45/90, smooth
45/90/120 degrees per second, native turning, and a recenter action. Recenter
invalidates the rendered-pose epoch before the next gameplay image. With
`--saved-settings`, preferences in `build/vr-settings.cfg` override initial
command-line quality/turning values; UI changes save through atomic file
replacement. Parser, invalid-save preservation, button-release, pointer and
recenter-epoch checks pass. The menu has submitted actual XR layers and matched
GPU readback. Fourteen live snaps were acknowledged by both physics and entity orientation
within 0-16 ms; all hooks/settings restored, with no faults. Visual turn comfort
and live smooth-rate acceptance remain pending. The user reported intermittent
native-menu input and requested ray/trigger selection and reliable Resume. The
mod-owned settings pointer does not yet control the Iggy native menus. Evidence:
`artifacts/runs/20261008T145515.257855Z-pid23072-arm-v28/`. The tracked-arm path
remains disabled.

The native-menu pointer candidate uses the existing controller-to-quad ray
and the verified Iggy cursor callback (`0x33530`), not hard-coded Resume
coordinates. It routes cursor movement and trigger press/release through the
same native Iggy methods as the game mouse. The `--native-menu-observer`
comparison observed 89 owned callbacks without rejected owners, writes, clicks
or faults; all hooks/settings restored. Its rendering diagnostic ended on a
context-dispatch change, so it was not a successful stereo run. Ownership and
trigger-release checks pass. `--native-menu-pointer` still needs real menu
hover/click, Resume, focus-return and held-trigger acceptance in the headset.
`arm_v31` adds a visible cyan aim marker on that same menu ray; the marker turns
amber while the trigger is held. Its separate transparent OpenXR quad follows
the menu's world anchor and has no effect on gameplay eye images. Menu triggers
are suppressed until physical release after Resume, preventing click-through
into weapon use. Ray/marker alignment, translated/rotated panels, focus loss and
held-trigger release checks pass. Live pointer, button and layer counters are in
`artifacts/xr-live-status.json`. The v30 run restored all hooks/settings without
faults, but VDXR remained hidden and submitted no headset frames; this is not
native menu interaction proof. v31 ended as visibility changed because the native
cached display dimensions no longer matched the prior request; all hooks and
settings restored, with no observed faults. `arm_v32` accepts a reset to the
captured native dimensions or an acknowledgement of the new dimensions during
the bounded update, while still rejecting unrelated values. Ownership/update
checks pass. v32 submitted 17,174 layers and the user confirmed pointing and
triggering Resume. Reopening the menu failed, and camera instability caused
dizziness. The comparison was stopped; its hooks and settings restored without
observed faults. Tracked arms remain disabled.

The read-only MD6 inspection now verifies the owned surface list and the native
shader element records: planar position/UV/normal/tangent streams plus eight
skin weights and eight joint indices per vertex, 44 bytes in total. The installed
Emily arm assets are exported only into ignored `artifacts/research/native-arm-assets/`
using an independent bounded reader of the container format documented by
[disrev](https://github.com/chipolux/disrev/blob/main/resources.py). All 13,003
vertices match native surface counts/bounds and normalised eight-weight records;
the mesh material/footer metadata is not fully decoded. The offline replay uses
the same `pose_arm` implementation as the mod and the live inverse binds. A neutral
wrist target preserves the mesh within 1.2e-6 game units. Other tested wrist poses
remain finite, but stretch and headset-to-rig placement are not accepted visually.
This does not re-enable tracked arms or establish hand grabbing.

`arm_v33` replaces first-controller-pose wrist calibration with an explicit
bind-space palm/wrist transform. It resolves named `02` knuckles through their
`01` metacarpal parents, checks the native inverse binds twice, and maps the
OpenXR grip axes in the same forward/left/up order as the controller publication.
Grip positions now use the calibrated room origin rather than subtracting the
current head centre. This follows the [OpenXR grip-pose definition](https://github.com/KhronosGroup/OpenXR-Docs/blob/main/specification/sources/chapters/semantic_paths.adoc#standard-pose-identifiers).
The palm position is a skeletal estimate and needs physical controller alignment.
Both named bindings pass a live read-only check using the same C++ decoder as the
mod. Mirroring, rigid inverse, independent first held pose, recenter and concurrent
publication checks pass. The 18-case mesh replay preserves both neutral wrist and
neutral palm targets within 1.2e-6 units. Tested deformations remain finite but can
stretch some triangle edges by up to 5.7 times, so this is not visual acceptance.
v33 is built, and tracked arms remain disabled in headset comparisons.
It does not implement finger closure, weapon aiming, a visible body or grabbing.

`arm_v34` adds native forearm helper roll, separating roll from swing as described
in the [VRM roll-constraint specification](https://github.com/vrm-c/vrm-specification/blob/master/specification/VRMC_node_constraint-1.0/README.md).
The helper joints and their elbow parents are checked against the owned skeleton.
The 27-case mesh replay passes neutral-pose preservation, but the largest triangle
stretch remains about 5.7 times; this does not fix or validate tracked arms.

`arm_v36` includes v35's shared stereo body anchor: both eyes use one owned player
entity heading snapshot per render frame, excluding animation camera rotation.
Native interpolated translation is retained. It also adapts the two verified
joystick polling/serialization call sites to accept a fresh focused OpenXR input
lease. The adapter wakes the owned joystick worker when that lease becomes active.
It leaves the native focus field, other callers, keyboard/mouse and Windows focus
unchanged. Owner, stale-input, stereo consistency, restoration and build checks
pass. A live menu-reopening and comfort comparison is pending; these changes are
not yet a user-accepted comfort fix.

`arm_v37` adds Dishonored-VR's source-adapted 150 ms pause pulse after a released
menu tap, with focus-loss cancellation and a Resume guard. Controller mapping
checks and an isolated executable fixture verify both native joystick callers,
unrelated/foreign-owner passthrough, worker wake and original code restoration.
The v36 session restored all hooks/settings, but VDXR remained hidden throughout;
it did not validate headset input or camera comfort. v37 also remained hidden and
restored all hooks/settings when stopped. Its paused-menu Y panel saves resolution,
SMAA and turning preferences. `--native-camera-trace` also records a bounded
`rendered-head-anchor.csv` with each eye's native source, shared anchor and final
rendered pose; `native_pause_button_reads` distinguishes native polling from raw
OpenXR action state. Reopening the native pause/save/settings menus and comfort
still require live acceptance.

`arm_v38` preserves room calibration across native pause/transient logic and XR
visibility or suppressed-frame transitions. Pose freshness still invalidates stale
eye transfers and input, but has a separate epoch from room-reference calibration.
An actual reference change, explicit recenter or replacement player/camera resets
calibration. The initial origin uses both eye poses, with antipodal quaternion
handling. Pause/resume, visibility return, delayed reference changes, ownership
replacement and invalid-pose checks pass. v38 subsequently ran in Quest 3 and
restored cleanly. The user reported improved camera behavior but rejected its
image stability and input regression. v39's upstream serialization gate fixes
the latter; pointer return and authored camera transitions remain unresolved.

The build script now rejects every nonzero Windows status, including negative
native crash codes. A new calibration check exposed the old positive-only gate;
the invalid quaternion average was corrected before loading v38. The full C++
check/build pass and a controlled negative-exit gate check succeed.

`tools/inspect_interaction.py --pid <PID>` inspects the pinned process with read-only
permissions. It verifies the player component list, interaction camera/state-machine
links and back-reference, sensor parent/camera ownership and target generation.
These links pass on PID 23072; the paused game has no selected interaction target.
The native interaction sensor reads aim position/axis in `0x3b8430`, with related
query stages at `0x3b8d60` and `0x3b9650`. This identifies a route for controller
aiming but does not implement grabbing. Its decoder checks reject recycled targets,
wrong owners, invalid bounds and concurrent owner changes. Runtime pose routing
needs job-ordering/overlap evidence before changing the shared camera aim fields.

```powershell
# Read-only binding check after building; use the PID of the pinned Steam build.
.\build\arm_binding_probe.exe $gameProcess.Id
```

Evidence: `artifacts/native-hand-bind-23072-readonly.json` and
`artifacts/research/native-arm-assets/mesh-deformation-bind-grip.json`.

```powershell
python tests\md6_surface_check.py
python tests\arm_asset_check.py
python tests\arm_mesh_decode_check.py
python tools\extract_arm_assets.py
python tools\sample_camera.py --report artifacts\game-probe-equipment.json --pid $gameProcess.Id --player-components --pose-state 3 --skeleton-layout --samples 1 --output artifacts\native-arm-surface-readonly.json
python tools\analyse_arm_mesh.py --pose-report artifacts\native-arm-surface-readonly.json
# Compile tools/arm_mesh_simulation.cpp with the same MSVC C++17 flags as the checks.
build\arm_mesh_simulation.exe artifacts\research\native-arm-assets\mesh-fixture.bin
```

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
* `src/tracked_pose.hpp`: OpenXR head/eye/grip pose conversion into the game's
  forward/left/up frame, with yaw-only recentering, explicit world scale and
  independent wrist orientation. It rejects tilted character frames and invalid
  tracking data. The experimental render-view hook now uses this conversion for
  head rotation and physical eye separation; aiming and bones remain unconnected.
* `src/game_xr.hpp`: submits paired native game images through OpenXR using the
  game's existing D3D11 device. Recorded per-eye render poses/FOV accompany the
  tracked frames. The candidate reads Quest actions, recreates eye swapchains
  when native output changes, and displays menus on a stationary OpenXR quad.
  It does not animate the game's body or implement controller-ray menu pointing.
* `src/controller_input.hpp`: maps Quest actions into the main executable's
  verified XInput 9.1.0 import. It releases input on focus/tracking loss or a
  stalled sample, suppresses combat in menus, and restores the import afterward.
  Its real Windows import-routing check runs in a separate test executable.
  Live native reads after device notification and the later `head_v19` headset
  comparison confirm visible movement. The earlier `head_v12` black-image
  report remains a failed scene test, not evidence for visual movement.
* `src/camera_comfort.hpp`: session-only overrides of the verified native
  bob/shake caches, using atomic compare/exchange and preserving later native
  or user choices on restoration. The startup config does not apply
  `view_skipShakes`; the session override has been verified through live reads.
* `src/render_size.hpp`: validates the enclosing per-eye recommendation and
  restricts the size override to the game's renderer window/HDC. The candidate
  changes native scene/output dimensions and DXGI buffers together, preserving
  the real client dimensions used for desktop input. Its WARP check covers
  actual GPU allocation, retained-reference failure and output restoration.
  `--optical-density` increases the enclosing symmetric render dimensions to
  preserve the runtime's recommended tangent-space pixel density. Quest 3
  currently requests 3102x3208; this adds about 48% more pixels than 2496x2688.
  `--resolution-multiplier` scales both eye dimensions, reports the effective
  scale, and respects runtime and current 4096-pixel diagnostic limits. The
  backend is built; the VR-facing control and live adjustment remain unfinished.
* `src/game_rig.hpp`: validates both owned player arm rigs and their 69-joint
  renderer palettes. `--arm-upload-observer` samples the native upload phase
  without modifying animation. The live observer accepted 100 palettes per
  side with zero rejected samples. Controller-driven palette replacement is
  implemented experimentally, but its actual arm mesh was distorted and rejected.
  Its hierarchy reader implements the pinned native `0x24f240` relative-data
  lookup, bounds the declaration records and rejects cycles/replaced owners.
* `src/rig_transform.hpp` and `src/arm_palette.hpp`: native joint packing,
  model/world conversion and arm IK with independent wrist orientation. The
  verified hierarchy selects wrist/finger/weapon attachment, forearm helper and
  upper-arm subtrees. `src/arm_tracking.hpp` passes owned CPU palette copies
  into the native consumer. The current posing is visually unusable and remains
  disabled for renderer comparisons. `tools/export_arm_fixture.py` connects
  actual upload records to their named rig report for offline checks.
* `tools/game_state.py`: reads the exact-build native game-logic manager and
  distinguishes gameplay, pause, overlays and loading. Unknown states suppress
  the test; foreground-key heuristics are not used for the paired/XR launch gate.
* `tools/sample_camera.py`: samples a unique historical camera candidate in a
  running copy using read-only process rights, after checking the executable
  hash, module identity, and loaded instruction signature. Its output remains
  explicitly unverified; camera movement alone does not prove a usable VR hook.
  It can also find typed camera objects and correlate fields with focused input.
  `--find-rig` performs a bounded read-only rig scan, checks that an animation
  component belongs to the current typed player/model, and samples named joint
  transforms through the native pose-state map. It does not invoke engine methods.
  `--player-components` uses the player's native component list instead of a heap
  scan and resolves both separate arm rigs. `--pose-state 3` reads the palette
  state passed to the renderer; the reads are not frame-synchronised.
* `build/d2vr_render_probe.dll` and `tools/inject_probe.py`: temporary exact-build DXGI
  diagnostic. It observes the live D3D11 Present path and reads the identified
  camera for ten seconds, then disables its hook. It patches Present temporarily;
  it does not write camera values or render VR. The DLL and its inert trampoline
  remain allocated until normal game exit to protect in-flight calls.
  `--stereo` temporarily selects the engine's two-view side-by-side metadata,
  sets a small separation in game units, records both prepared views, captures
  the backbuffer, and restores the prior values. This is a desktop diagnostic;
  it has no headset rendering or tracking integration.

Executable files, Steam settings and system runtime selections are unchanged.
The explicitly approved graphics comparison edits the game's saved settings
through `tools/vr_graphics_profile.py`, retaining a verbatim baseline and a
conditional restore record. A separate pre-test save was created through the
game's normal menu. Build outputs and diagnostic reports stay in this workspace.

## Verified locally on October 7–8, 2026

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
| Full-viewport paired gameplay | The corrected capture learns four backbuffer bindings per eye, then captures the first completed eye before the second replaces it. Gameplay run recorded 2,638 Presents and 2,628 native two-view frames; clean distinct full-screen images show depth-dependent horizontal parallax. All hooks/settings restored. Eye order, metric scale and broad scene quality remain uncalibrated. Named evidence: `paired-v3-gameplay-*`. |
| In-game Quest 3 submission | The 30-second image bridge submitted 2,645 game layers, all with native gameplay state, and reached focused XR state. All hooks/settings restored, with no callback errors. The user confirmed clear stereo but a game-window presentation because head rotation was not applied in that test. Named evidence: `xr-game-images-v2-*`. |
| First head-rotation test | The user reported an immersive 3D game world, camera rotation with head movement, 2D menus and the game's untracked hand models. Grid/edge artifacts were reported; a saved pre-streaming eye image also contains edge defects. After approximately 30 seconds, teardown stalled before final counters/restoration could be written. A local thread dump shows worker-thread XR cleanup waiting on D3D11 while the game renderer waits in the AMD driver. |
| Head rotation and render-thread shutdown | Fresh-process `head_v2` test passed: 2,663 Presents, 2,651 focused gameplay XR layers, and 5,298 render-pose writes with 5,298 source restores. XR shutdown ran on Present, all hooks/native settings restored, and the game remained responsive. The user confirmed camera tracking; artifacts remained. Preserved evidence: `head-v2-dxgi-probe-result.json` and `head-v2-*` images/logs. Roomscale, controller gameplay and playable quality remain unverified. |
| Temporal-AA isolation | `head_v3 --skip-temporal-aa` passed: 2,648 gameplay XR layers, 5,292 pose writes/restores and clean render-thread shutdown. The exact native `r_skipTemporalAA` integer cache was temporarily enabled and restored; an independent post-test read confirmed zero. Visible image defects remain in the saved eye image. The user changed the quality preset to High and resized the window between tests (1920x1080 to 1920x1103), so this is not a matched quality comparison and does not isolate the full cause. Named evidence: `head-v3-skip-taa-*`. |
| Resolution measurement | Last native eye source was 1920x1103; the large scene viewport was 1632x936, about 85% per dimension. VDXR recommended 2496x2688 per eye. Direct decompilation of the scale selector at RVA `0x377540` confirms forced X/Y fractions accept only values through 1.0. Setting 100% cannot create a headset-sized scene above the desktop output. |
| Full-scale test failure | `head_v4 --full-render-scale` stalled in XR submission and did not finish restoration. Preserved thread stacks show Present waiting in VDXR while a VDXR thread waits on this diagnostic's observer mutex. This supports a lock cycle in the observer; it does not prove that resolution caused the hang or the visible artifacts. Evidence: `head-v4-full-scale-stalled-*`. |
| Observer and D3D11 isolation candidates | `head_v5` limits observers to the game's exact context/Present thread and releases the observer lock before XR calls. `head_v6/v7` use native D3D11.1 context-state swapping around XR to restore the game pipeline. The real WARP-device check passes for normal and exception exits. These candidates have no live-game stability result. |
| Symmetric-frustum comparison | `head_v7 --symmetric-frustum` uses an enclosing symmetric projection with matching scalar FOV and submitted XR FOV. Optical checks pass, including scalar-to-matrix consistency. This tests the native frustum-consumer hypothesis from EternalVR's light-tile investigation; no headset comparison or artifact fix is claimed. |
| Controller input delivery, headset failure | `head_v12 --controllers` notifies the game's native device-change handler after the virtual pad becomes active. The game read VR pad values 7,402 times, including 5,556 nonzero reads. The user's latest correction reports a black headset image with a few light dots and no opportunity to test movement; this supersedes the earlier positive answer. Input delivery is verified; visible gameplay controls are not. The v12 frame pixels were not preserved, so that image's cause cannot be concluded from submission counters. All hooks/settings restored; evidence: `artifacts/head-v12-live-10276/`. Tracked hand/weapon aiming and grabbing remain unimplemented. |
| Recommended output live result | `head_v11 --headset-size` overrides renderer helper `0x346320`, updates the native cached size read by getters `0x346760/0x346750`, and resizes DXGI and XR eyes. A live native scene viewport was `[0,0,2495,2687]`; both eye textures were 2496x2688. The test submitted 572 gameplay layers, applied/restored 2,654 head poses and restored all hooks, scale, cached dimensions and desktop output. User confirmed clearer resolution. Evidence: `artifacts/head-v11-live-10276/`. |
| Eye timing defect | The same test had 771 empty XR layers out of 1,343 Presents. Backbuffer binding counts varied between 0 and 9; the current capture accepted only two bindings. This makes binding-count-based eye completion unreliable. User reported flickering; saved eye images also retain shading/edge artifacts. Resolution alone does not fix these defects. |
| Native eye boundary trace | A desktop-only `eye_v13 --eye-boundary` observer matched 10,890 native begin-view calls over 5,445 stereo frames. All 279 fully recorded eye sequences were left then right on the game renderer thread. Source view identity is established before head-pose substitution. All hooks/settings restored. The new optional `--native-eye-capture` copies the completed left eye at the second eye's begin call; its headset timing result is pending. Evidence: `artifacts/eye-v13-live-10276/`. |
| Native eye capture desktop result | `eye_v14 --native-eye-capture` recovered 3,563 completed pairs over 3,564 stereo frames, including a captured frame with five backbuffer bindings. Partial, repeated and out-of-order eye calls are rejected, with state reset at each Present. All hooks/settings restored and the saved eye image shows the game world. This is desktop timing/image evidence; headset flicker and shading quality are still unverified. Evidence: `artifacts/eye-v14-live-10276/`. |
| Menu candidate | `head_v9` submits menu images on a native OpenXR quad anchored when the menu opens. The placement check verifies that head movement does not move the panel and reopening places it ahead of the new head pose. Native controller navigation is implemented; headset presentation and menu interaction are unverified. No controller-ray pointer is implemented. |
| Direct static analysis | REA 5.0.0's Ghidra doctor passes, but its native binary snapshot cannot be opened by Ghidra because of file sharing. Direct Ghidra 12.1.4 imports a SHA-verified workspace copy and decompiles selected functions. Whole-image analysis hit its 240-second limit; the generated class-registration function exceeds the decompiler flow limit, so bounded instruction windows are exported instead. The installed game executable hash remains unchanged. |
| Native body metadata | Generated records identify `arkCpntEquipment::m_playerBody` at `+0x38`, `arkCpntPlayerBody` size `0xf40`, left/right arm parts at `+0x370/+0x680`, and inherited `arkBodyBase::m_animation` at `+0x08`. These are static layouts, not verified live handles, bone palettes or safe write sites. Exact evidence is in `artifacts/research/d2-body-field-metadata.json`. |
| Animation and rig path | Static generated records identify `arkCpntAnimBase` size `0x410`, skinned model at `+0x38`, transform set at `+0x40`, and derived `arkCpntAnimEvolved` size `0x2d40`, IK bridge/solver pointers at `+0x468/+0x470`, and head-joint index at `+0x2c7c`. `tools/export_body_fields.py` reconstructs direct fields from pinned Ghidra exports and skips generated accessor properties. Live instance ownership, palette layout, animation timing and writes remain unverified. |
| Rig reader live result | The read-only sampler found one animation belonging to the verified player's model and read all 20 named body/leg/head joints consistently in ten samples. It rejected 57 other-model animations and six unstarted components. This skeleton contains no arm/hand joints; the separate hand rigs remain unresolved. State zero is not verified as the final blended pose, and reads are not frame-synchronised. Evidence: `artifacts/player-rig-live-10276-c.json`. No bone writes occurred. |
| Separate arm rigs resolved | Native component getter `0x877720` traverses the player list at `+0x268`. A direct read found the owned `arkCpntPlayerBody`, verified its main animation/model, and resolved left/right arm animations through `body+0x378/+0x688`. Each has 69 named joints, including shoulder, elbow, wrist, fingers and hand attachment. Ten read-only snapshots were consistent. Evidence: `artifacts/player-component-rig-live-10276.json` and `player-arm-pose-live-10276.json`. These are live identities, not controller-driven arms. |
| Renderer arm pose source | Native `0x541270` passes pose-state three's transform slice to model update `0x25efe0`. The reader now selects that map explicitly and read all 69 joints of each arm consistently across ten samples; both model scales were `[1,1,1]`. It also records model origin/axis for later world/local conversion. Read consistency is not an animation-job synchronization guarantee. Evidence: `artifacts/player-arm-render-pose-live-10276.json` and static `arm-pose-finalisation/` exports. No bone or model writes occurred. |
| Native camera setup hook | The public headtracking mod's PE fingerprint matches all three fields of the installed executable. A read-only observer at `idRenderView::Setup` (`0x374e40`) recorded 5,051 calls and 84 sampled input/render pose pairs; all 84 matched. Present and Setup restored, no callback errors, game responsive. Camera was stationary, so this verifies the ABI/copy layout, not tracked gameplay. |
| Tracked coordinate conversion | Native checks cover XR axis signs, yaw recentering, independent wrist aiming, nonzero character heading, scale, 360 rotations and rejected invalid poses. No in-game tracking writes or interaction claims follow from these checks. |
| Diagnostic mode reuse | `head_v19` dispatches the current desktop/head mode through one stable Setup hook. A desktop-to-VR regression in the same loaded DLL captured matching actual XR pixels, applied/restored 2,728 head poses and restored all hooks/settings. This fixes the flat-window result from the preceding reused `head_v18` session. Evidence: `artifacts/runs/20261008T072523.568983Z-pid12600-head-v19/`. |
| User-confirmed rendering fallback | `arm_v1 --native-eye-capture --symmetric-frustum --headset-size --controllers` produced a playable surrounding view in Quest 3. User reports artifacts greatly reduced and visible movement usable; clarity, interaction reticle alignment and prompt size remain unresolved. All hooks, cached/native output, render scale and input import restored; no callback failure. Evidence: `artifacts/runs/20261008T075525.816049Z-pid12600-arm-v1/`. This does not establish tracked hands or physical interaction. |
| Passive arm upload | `arm_v1 --arm-upload-observer` accepted 100 left and 100 right 69-joint palettes at native `0x25efe0`, with zero rejected samples. Multiple animation worker threads call this phase. Original arguments were passed through, no animation was written, and the observer restored its original instruction bytes. Evidence: `artifacts/runs/20261008T073647.310406Z-pid12600-arm-v1/`. |
| Optical-density candidate | `arm_v3 --optical-density` captured a settled tracked gameplay pair after 180 eye transfers at 3102x3208. Both actual XR destination textures equal their native sources. Image inspection shows the room and some curtain-edge trails. User reports a black image after switching out of VR and returning, so clarity/smoothness improvement is unverified. All hooks/settings and the two-stage resize restored cleanly. Evidence: `artifacts/runs/20261008T080625.335211Z-pid12600-arm-v3/`. |
| VR return candidate | `arm_v4` invalidates old pose pairs and resets menu placement/head calibration after visibility loss or a local reference-space change. It honors OpenXR's reference-change timestamp and captures fresh actual gameplay/menu XR pixels per pose epoch. Native simulated lifecycle checks pass; live headset recovery is pending. |
| Input after menu recovery | `arm_v5` reads buttons/sticks independently of grip pose availability. User confirmed entering gameplay and walking; active actions from both controllers and nonzero gamepad reads were observed. Its shutdown timed out, so the process was restarted. OpenXR state 5 means FOCUSED; an earlier commentary interpretation of that numeric value was corrected. Evidence: `artifacts/runs/20261008T082946.175329Z-pid12600-arm-v5/user-feedback.json`. |
| Arm hierarchy | Both owned live skeletons resolve the native hierarchy method at `0x24f240`. Read-only decoding verifies 69-joint trees: fingers and hand attachment descend from wrist, lower-arm helpers from elbow, and the upper-arm helper from the upper arm. Identity and hierarchy guards pass synthetic checks. Evidence: `artifacts/player-arm-hierarchy-live-1988.json` and `arm-hierarchy/` static exports. No engine methods were invoked. |
| Native palette IK simulation | Offline checks passed 720 wrist targets on both actual palettes captured at the native upload callback, using compatible named rigs and the verified hierarchy. Limb lengths, wrist orientation, finger/attachment offsets and model/world conversion were checked. Evidence: `artifacts/native-arm-ik-check-2026-10-08.txt` and `native-arm-ik-fixture.json`. This does not verify visible tracked arms, gameplay aiming or physical interaction. |
| Settled scene-size diagnostic | `arm_v7` and later builds trace one additional gameplay frame after output resizing and 180 tracked transfers. Both native render-target binding paths and depth targets are observed. `settled-render-state.json` and `dxgi-settled-views.json` accompany phase 3. The current headset is unavailable, so no settled scene-target result is claimed. The earlier trace only observed backbuffers; it cannot establish internal scene resolution. |

Reports are under `artifacts/`, which is ignored by Git. The symmetric-frustum
rendering fallback and visible movement have user evidence. Independent tracked
arms/weapons, a connected body, physical interaction, broader load transitions
and polished image quality remain unfinished. Clean shutdowns do not establish
stability across every game scene or runtime.

## Run the checks

Visual Studio 2022 with x64 C++ tools, Windows SDK, and Python 3.11+ are required.
The machine used for initial development already has them. Included dependencies
are pinned official OpenXR headers and MinHook source; see `THIRD_PARTY_NOTICES.md`.

From this repository in PowerShell:

```powershell
.\tools\build.ps1
python .\tests\probe_check.py
python .\tests\game_state_check.py
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
minutes for verified native gameplay state before invoking the DLL.
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

The paired candidate uses full viewports for both native views. It learns the
normal frame's backbuffer-binding count and copies the first eye at that boundary
before the second eye replaces it. A pair requires exactly twice the learned
count before the same Present, distinct prepared poses and clean restoration:

```powershell
.\tools\build.ps1 -Paired # Only while this DLL is not loaded.
python .\tools\inject_probe.py --pid $gameProcess.Id --paired --wait-for-game
python .\tools\view_captures.py
```

`dxgi-left.png` and `dxgi-stereo.png` are the candidate pair in this mode.
Their image quality and association with the prepared eye poses must be checked
before any image-quality claim. Without the XR options, this path performs no
headset submission or tracking writes.

The experimental head-rotation test requires a fresh game process after a stalled
test. Build to a new filename whenever an earlier DLL is still loaded:

```powershell
.\tools\build.ps1 -Paired -OutputName d2vr_render_probe_head_v2.dll
$gameProcess = Get-Process -Name Dishonored2
python .\tools\inject_probe.py --pid $gameProcess.Id --paired --dll-name d2vr_render_probe_head_v2.dll --vr-preview --head-tracking --wait-for-game
```

This runs for 30 seconds. The Setup hook temporarily overrides the render-view
pose and asymmetric headset projection, then restores the source inputs after
each engine call. Rotation and physical pupil separation are enabled; collision
bounded roomscale movement, controller gameplay, tracked arms and body are not.
Keyboard/mouse still drive native movement and aiming. Menus suppress the pose
override and use the native image presentation. The result records writes,
matching source restores, submitted layers and render-thread shutdown. A timeout
requires a normal game restart before another test; never inject into the stalled
process or overwrite its loaded DLL.

For an artifact-isolation experiment, build another unused output name and add
`--skip-temporal-aa`. This validates the exact native flag-read instruction and
cvar name pointer, requires a zero baseline, temporarily sets its integer cache
and restores it with compare/exchange. It does not rewrite the configuration file
or select a different quality preset. Native menu/renderer use of the flag is not
a complete stereo temporal-history implementation; visible defects remain.

The projection comparison candidate is built as `d2vr_render_probe_head_v7.dll`.
It includes observer filtering and graphics-state isolation. To compare the
projection hypothesis in a fresh game process, use the same candidate and scene
with and without `--symmetric-frustum`:

```powershell
python .\tools\inject_probe.py --pid $gameProcess.Id --paired --dll-name d2vr_render_probe_head_v7.dll --vr-preview --head-tracking --symmetric-frustum --wait-for-game
```

This option keeps tracked eye separation and head rotation. Both the game and
OpenXR receive the symmetric FOV actually rendered. It is an isolation test,
not the final asymmetric optics implementation. Keep `--full-render-scale` and
AA changes separate so a comparison changes only one factor. A failed shutdown
still requires a game restart. No `head_v7` live test has been performed.

The preserved rendering fallback is `d2vr_render_probe_arm_v1.dll`, SHA-256
`dcef90bca74608b86baeb43cfce515d79a2cdd568e14447a0ecffbf16092bdc0`.
In a windowed game with a loaded save and a visible headset session:

```powershell
python .\tools\inject_probe.py --pid $gameProcess.Id --paired --native-eye-capture --dll-name d2vr_render_probe_arm_v1.dll --vr-preview --head-tracking --controllers --headset-size --symmetric-frustum --until-stopped --wait-for-game
```

`--headset-size` implies `--full-render-scale`; no fixed 2K size or upscaling is
substituted for the runtime recommendation. It requires a windowed swapchain.
The continuous fallback runs until `--stop` is requested. Stop it with the same
PID and DLL name, then wait for the original injector to finish restoration and
archive the run before starting another diagnostic. Result
JSON separately records requested dimensions, actual application, native resize
HRESULT, size-helper calls, output restoration, controller reads and menu layers.
Failure to reach the requested output or restore the desktop output is a failed
test. Do not rerun after an incomplete shutdown; restart the game normally.

Current candidates support `--wait-for-headset` with `--vr-preview`. This waits
up to five minutes using the standalone OpenXR system/device probe before
starting the injected session. Unavailability retries; runtime errors/timeouts
abort. Four mocked protocol checks pass. The readiness probe starts no XR
session and cannot establish that the game is visible or controllers work.

Default candidate controls use the game's existing gamepad bindings: left stick
movement, right stick turning, right A jump, right B crouch, left X interact,
left Y lean, left/right stick clicks sprint/zoom, right trigger attack, left
trigger power, right grip block, left grip power wheel, and left menu pause.
In menus the sticks and A/B navigate/confirm/back; triggers are suppressed.
Hand pose tracking is sampled, but native wrist/weapon/rig binding is pending.
These bindings do not turn interact into physical grabbing or add swing damage.

The latest VDXR `1.0.10` check found Meta Quest 3, and the `head_v10/v11`
live sessions submitted game images. Earlier isolated Oculus/SteamVR checks
returned `HEADSET_UNAVAILABLE`; they do not describe this later connected session.
No global runtime registration was changed. The current candidate writes
`xr-live-status.json` once per second, including while OpenXR suppresses frames.
Its PID and timestamp must match the current test. Check session visibility,
`runtime_should_render`, tracked gameplay transfers, applied dimensions and
actual source/destination pixel equality before requesting headset feedback.
The `headset_scene_visually_verified` field remains false until user evidence
is recorded separately; submission counters cannot supply that evidence.

The prepared read-only rig inspection needs a loaded save in a running game,
but does not need a connected headset or injection:

```powershell
python .\tools\probe_game.py --game 'C:\Program Files (x86)\Steam\steamapps\common\Dishonored2' --output artifacts\game-probe-rig.json
python .\tools\sample_camera.py --report artifacts\game-probe-rig.json --pid $gameProcess.Id --find-rig --samples 10 --output artifacts\player-rig-live.json
python .\tests\rig_fields_check.py
```

Each scan reads at most 512 MiB for at most ten seconds. `next_scan_address`
allows another bounded scan with `--scan-start` if the player's component was
outside that segment. Candidate counts are bounded per type. Initial results
retain rejected animations with a reason; repeated pose snapshots are restricted
to those whose model matched the live player. Name IDs and first-state matrices
are research evidence, not proof of correctly timed VR arm writes. The reader
does not lock the game animation frame and explicitly marks that limitation.

## GitHub findings used for the next implementation

Source trees and selected text files were read at pinned commits on October 7,
2026. No downloaded mod was installed or executed. Source snapshots and metadata
are under `artifacts/research/vr-comparison-2026-10-07/`.

| Reference | Useful finding and applicability |
| --- | --- |
| [Dishonored VR](https://github.com/VR-Stereo-Hub/Dishonored-VR/tree/7c1cb8a323e889220d33e62dbb7ed2b64ec765a5) | Separate stereo, OpenXR actions, gamepad composition, hand rig, weapon attachment and interaction hooks. Its UE3/x86/D3D9 engine offsets cannot be reused for Dishonored 2. The inspected license is zlib/libpng. |
| [EternalVR](https://github.com/ArcadaLabs-Jason/EternalVR/tree/2c6b6389459617dae52b41399172281880bdf824) | MIT source for DOOM Eternal: native engine-size hooks, input routing, menus and per-eye histories. Its Vulkan/id Tech 7 addresses and layouts do not transfer to Void/D3D11. Full independent arm rigging is still listed as planned; it is not proof of completed hands. |
| [KHARVOX](https://github.com/CactusVRStudios/KHARVOX/tree/ff348bbd62204067fe6e16abc99be2526cd08dc0) | MIT source for DOOM 2016. It changes the engine's size accessors as well as graphics output and checks engine image-allocation capacity. Its documented static controller hands are a different target from connected native arms. |
| [Dishonored 2 headtracking](https://github.com/itsloopyo/dishonored-2-headtracking/tree/0dc9036a9b5e9ce3161c38d7f89ffeac34a20e47) | MIT, exact-build Void camera/game-state/collision research. Existing camera adaptation is credited in `THIRD_PARTY_NOTICES.md`; this project provides no stereo or controller rig. |
| [disrev](https://github.com/chipolux/disrev/tree/e9cf30eec5fa3d35eeef9c448f12f8c711a5914c) | Dishonored 2 resource and entity-format research. Useful for identifying assets, not a live rig or interaction API. No root license was found in the inspected tree; no code was copied. |
| [UEVR](https://github.com/praydog/UEVR/tree/4ee5c6b6162dee2291fc75f9dfc57667f6d45a2d) | Readable examples of engine render-target resizing, D3D state preservation and input hooks. Engine hooks require UE4/5. The inspected license says all rights reserved; no code was incorporated. |
| [CircuitLord Titanfall 2 installer](https://github.com/CircuitLord/CircuitLordVRModInstaller/blob/main/README.md) | Installer source is MIT; the full-body VR mod itself is closed source. Its implementation cannot be learned from the installer code. |
| [TinyBlkDog Titanfall 2 repository](https://github.com/TinyBlkDog/titanfall2vr) | Indexed documentation describes another implementation. The live repository API returned 404; no current source or pinned revision could be inspected. |

The strongest artifact lead is EternalVR's
[light-tile investigation](https://github.com/ArcadaLabs-Jason/EternalVR/blob/2c6b6389459617dae52b41399172281880bdf824/docs/rig-findings/stereo-bin-tiles.md):
an asymmetric rendered frustum disagreed with a symmetric light/decal tile grid.
Their controlled scene comparisons and projection-derived tile fix address
rectangular lighting patches. Our hook also writes an asymmetric explicit
projection, so the hypothesis is applicable; Dishonored 2's corresponding pass
and constants still need to be identified. Disabling its final temporal-AA pass
did not remove the reported defects. Resolution and the tile hypothesis must be
tested separately.

For resolution, [EternalVR's engine-size analysis](https://github.com/ArcadaLabs-Jason/EternalVR/blob/2c6b6389459617dae52b41399172281880bdf824/docs/rig-findings/render-size.md)
supports changing native output sizing before allocating eye targets. Direct
Dishonored 2 analysis found a rendering size helper at RVA `0x346320`:
`WindowFromDC` then `GetClientRect`, used by renderer callers `0x368b80`,
`0x36a740` and `0x36aa40`. Other client-rect calls centre and clip the mouse;
those must continue to use the real desktop window. The next size implementation
must align native scene targets, output, XR swapchains and resize handling with
the recommendation queried from the active runtime. `head_v9` implements that
path, pending live verification. Corrected Ghidra flow at the non-fatal logging
helper reveals that `0x36aa40` calls DXGI `ResizeBuffers` with width/height zero;
the candidate must override that call as well. [DXGI's documented lifecycle](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-resizebuffers)
requires releasing backbuffer references before resize, confirmed by the WARP
failure/recovery check. Enlarging only the XR copy would upscale the original image.

For gameplay, the installed executable imports `XInputGetState/SetState` from
`XINPUT9_1_0.dll`. Route OpenXR actions into that verified native input path for
movement, turning, jump, crouch, powers, attack and pause, releasing held actions
when XR focus/tracking is lost. The menu needs a stable VR panel, controller-ray
hit coordinates and native confirm/back input; menu state must suppress combat.
The action-to-gamepad path and anchored menu panel are implemented in the
candidate; the controller-ray pointer and native hit-coordinate path remain pending.

For grabbing, [Dishonored VR's interaction hook](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/7c1cb8a323e889220d33e62dbb7ed2b64ec765a5/src/game/dishonored/interact_aim.cpp)
uses a controller ray anchored to a stable gameplay camera, preserving native
selection and use rules. A rendered eye is an unsuitable gameplay anchor because
it changes with eye/render passes. Dishonored 2 needs its own trace and carry
physics bindings so a tracked hand can select, hold, release and throw a supported
object. Mapping grip to the interact button alone does not meet that target.

The REA video was reviewed together with its
[Windows/Ghidra guide](https://github.com/morluto/rea/blob/main/docs/windows-ghidra-p0.md).
The local snapshot-sharing error is preserved in
`artifacts/re-toolchain/camera-and-scale-temurin.json`. The working direct fallback
is `tools/D2StaticResearch.java`, with the local project and exports under
`artifacts/re-toolchain/direct-ghidra/`. Selected decompilation success does not
mean the complete game has been reconstructed.

If the signature does not match the installed build, the sampler refuses to run.
This tool needs a real running game; no live sampling was established by the
offline synthetic-file checks.

The native camera observer reads the input view before Setup and the render copy
after it, without changing either camera, projection or stereo settings:

```powershell
.\tools\build.ps1 -ViewSetup # Only while this separate DLL is not loaded.
python .\tools\inject_probe.py --pid $gameProcess.Id --view-setup
```

Its entry instructions must match the exact executable. `native-view-setup.csv`
records sampled source/render pairs; result JSON records their match count and
hook restoration. The first live evidence is preserved in
`artifacts/native-view-setup-first.csv` and
`artifacts/native-view-setup-first-result.json`. No headset submission occurs.

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
can be produced or validated in one session. Major remaining unknowns include
stereo image defects, stable lifecycle, safe rig binding and gameplay interaction.

## Interaction target

The requested quality target includes connected arms and a visible body, with
hands driving gameplay rather than merely following mouse aim. Planned checks:

* Left-hand Blink/Far Reach and other aimed powers; right-hand sword, pistol and
  crossbow aim independent of the headset.
* Sword swings and blocks from tracked motion, with native damage/target rules;
  physical reload and item-use actions where each weapon supports them.
* Reach, grab, carry and throw supported objects and bodies with controller
  motion, including native collision and release behavior.
* Hand-driven stealth grabs/chokes and takedowns, with animation takeover and
  return that does not leave hands or body detached.
* Physical crouching/leaning and supported climbing/mantling, bounded by level
  collision; comfortable turning and seated/standing calibration.
* Wrist/world menus, usable journal and weapon/power selection, and controller
  haptics for confirmed contacts and actions.

These are acceptance targets, not implemented features. The original game has
limited object/animation rules; extending those rules needs additional gameplay
hooks. Rendering stereo and binding the player skeleton remain prerequisites.

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
* [itsloopyo's Dishonored 2 headtracking source](https://github.com/itsloopyo/dishonored-2-headtracking/tree/0dc9036a9b5e9ce3161c38d7f89ffeac34a20e47)
  is MIT licensed and targets the installed PE fingerprint. Its native camera
  setup, game-state gating, camera collision, reticle and marker work are useful
  integration references. It is flat headtracking, without VR stereo or hands.
  The ABI/copy layout is now independently verified in our running game; its
  reported metric world scale still needs a local measurement.
* [Void Explorer](https://www.nexusmods.com/dishonored2/mods/30?tab=description)
  supports D2/Death of the Outsider resources, model/skeleton previews and FBX
  import/export. It is a useful rig-inspection tool, not a VR runtime. Its author
  restricts modification/redistribution; no tool code or assets are incorporated.
* [Helix Mod's Dishonored 2 stereo fix](https://helixmod.blogspot.com/2018/03/dishonored-2-dx11.html)
  supplies geo-11/3Dmigoto stereo rendering and shader fixes. It provides a
  comparison for graphics defects, without tracked hand/body interactions.
  The downloaded personal-use archive has a restrictive license; no fix code is
  adapted or installed. Downloads remain in ignored research artifacts.
* [Luma Framework's D2 renderer source](https://github.com/Filoppi/Luma-Framework/blob/5d7ee5470d8f09e27dfe529a386af46d8c6f8e33/Source/Games/Dishonored%202/main.cpp)
  documents per-view shader constants, separate first-person weapon projection,
  temporal history and graphics hooks. Its [maintainer wiki](https://github.com/Filoppi/Luma-Framework/wiki)
  lists the D2/Death of the Outsider mod as work in progress. This is a graphics
  mod, not a motion-control conversion. Source inspected only; its custom MIT
  license includes attribution and commercial-use conditions.
* [WolfSDK](https://github.com/LopeKinz/wolfenstein-modloader)
  covers Wolfenstein's id Tech 5/6 archive/decl and asset workflows. That related
  engine research does not establish compatibility with Void or provide D2 VR.
* [The supplied video, at 4:12](https://www.youtube.com/watch?v=rRQzrQLZRWM&t=252s)
  describes compile-and-compare matching decompilation. Its transcript is a
  workflow reference, not a D2 SDK or a demonstration that arbitrary engine
  source and polished interactions can be recovered automatically.

The arm test covers 20,000 deterministic random poses plus reach boundaries,
degenerate pole/target cases, mirrored elbow selection, and invalid calibration.
The binary-probe test uses synthetic files to verify bounds, imports, multiple
matches, executable-section filtering, and RIP-relative target decoding. These
checks validate development components, not Dishonored 2 gameplay.
