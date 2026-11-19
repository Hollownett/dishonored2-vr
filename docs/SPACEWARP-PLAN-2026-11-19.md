# Own spacewarp: plan (2026-11-19)

Status: plan only. Nothing is built, launched or measured for it yet.

## 1. Goal

While the frame pacer runs at half rate (`FramePacer::k == 2`), the engine renders a real frame for every second display slot and
the mod fills each slot in between with a frame it generates itself, so the headset gets the full refresh rate without the runtime's
reprojection (Virtual Desktop SSW, SteamVR Motion Smoothing, Meta ASW).

The target is a CPU-bound PC. On the user's Ryzen 5 5600X the game's main thread needs 9.5-12 ms per frame
(`docs/PERF-NIGHT-2026-11-19.md`); performance mode (one eye per frame plus the AFW rebuild) saves GPU time only and leaves that frame
time unchanged (`docs/PERFORMANCE-MODE-AFW-COMPARISON.md` section 4). Generating every second frame halves the engine's CPU per
displayed frame. Performance mode stays the answer for GPU-bound PCs.

Method: **extrapolation** (the ASW / Application SpaceWarp family). The newest real frame is splatted forward to the generated slot's
predicted pose and time, using its depth and per-pixel 3D motion. That adds no latency. Interpolation (DLSS Frame Generation, FSR Frame
Generation, XeFG) needs the next real frame first and adds a full engine frame of lag (section 8).

## 2. What the plan rests on

| Fact | Where | Consequence |
|---|---|---|
| The engine's main loop runs the game side and then the render side on **one thread**, ending in Present. The mod's `present()`, `xrWaitFrame` and `xrEndFrame` run on that thread. | `src/async_present.hpp:4-8`, `docs/ENGINE-DIG-NOTES.md` section 3 | No frame can be generated inside the Present hook. At half rate the next Present comes ~22 ms later, after the in-between slot's deadline. Submission needs its own thread. |
| OpenXR needs one `xrWaitFrame` per submitted frame, and `XrFrameEndInfo::displayTime` must be that wait's predicted time. | OpenXR spec | Two frames for two slots cannot be submitted at once. Something must run once per slot. |
| The session is bound to the **game's** D3D11 device (`game_xr.hpp:2479`). Every XR swapchain (both eyes and about a dozen overlays) is drawn on the game's immediate context inside `present()` (`:3139`, `:3598`). Runtimes call into the session's device during `xrEndFrame`. | `src/game_xr.hpp`, `docs/PERF-NIGHT-2026-11-19.md` (2B.4) | A second thread cannot use that device. The async-Present experiment (v1408-v1410) crashed because background tasks touch the immediate context, and deadlocked on a wait inside a D3D11 call. |
| Per-eye depth copies, motion textures (`EyeMotionPass`: xyz = the surface's world displacement in metres since this eye's previous render; w = object motion in render pixels; arms zeroed) and rig masks exist, but reach `game_xr` **only in performance mode**. | `src/dxgi_probe.cpp:14102-14125`, `src/eye_motion_pass.hpp:2-16`, `src/fsr_taa.hpp:97` | Paired stereo must produce them too (step 0b). The xyz motion is what extrapolation needs: advance each mover by a share of it. |
| AFW v2 already forward-splats a frame into another camera: depth test by `InterlockedMax` on 1/z, jump-flood fill from the background side, Catmull-Rom resolve, sky as direction only, a camera-cut rule. Measured ~1.8-1.9 ms per eye (RTX 5070, 2246x2419). Per stage: decide 0.38, resolve 0.29-0.34, splat 0.27, crack and copies 0.27, motion 0.19, scatter 0.16, repair 0.09-0.16, fill 0.07-0.11 ms. | `src/afw2.hpp`, `docs/PERF-NIGHT-2026-11-19.md` section 8 | Extrapolation keeps the splat, crack, fill and resolve stages. The fresh-eye confirmation (chain, repair, votes) has nothing to confirm against and drops out. |
| A shared texture and shared fence bridge between two devices exists. | `src/afw_puredark.hpp:227-325` | Packets and overlays can use the same pattern. |
| The documented setup turns VD SSW off because walking with it looks steppy. | `docs/PLAYING.md` | The runtime's guess at locomotion is the quality bar to beat. The mod knows the locomotion and turn exactly. |

## 3. Architecture

```
main thread, per real frame (present())          XR thread, per display slot
  locate views for the packet's slot               xrWaitFrame -> slot time D
  input, menus, overlay drawing                    newest packet meant for D?
  eye processing (CAS, SMAA, alternate rebuild)       yes: copy it into the eye swapchains
  write packet: eyes, depth, motion, rig,             no:  generate from the newest packet for D
    cameras, play space, display time              copy dirty overlays, xrBeginFrame, layers, xrEndFrame
  signal the shared fence, publish to the mailbox
```

- **Mod-owned D3D11 device** on the game's adapter (the LUID from the D3D11 graphics requirements); the session binds it. The XR thread is
  its only user, apart from the runtime inside `xrEndFrame`. It is D3D11 rather than D3D12 so `StaleEyeWarp`, `Afw2Pass` and
  `EyeSharpen` are reused unchanged: each takes an `ID3D11Device`.
- **Packets**: three sets of shared textures, created on the mod device and opened on the game device with `OpenSharedResource1`:
  - per eye: colour (display size, before the overlays drawn into the eye), depth R32, motion RGBA16F and rig R8 (render size);
  - CPU data: both views and FOVs, the `WarpCamera`s, hand poses, the play-space transform, the display time each eye was rendered
    for, the frame number and the cut flag.

  Sync: a shared `ID3D11Fence` is signalled on the game context at the end of `present()` and waited on the GPU by the mod context
  (`ID3D11DeviceContext4::Wait`). Neither side waits on the CPU.
- **Overlays**: each overlay swapchain gets a shared texture on the game side. The main thread draws into it as today and marks it
  dirty, and the XR thread copies dirty ones into their swapchains. One wrapper type covers all of them.
- **Frame clock**: the XR thread publishes the last predicted display time and the period. The pacer's wake time and the locate horizon
  (`DisplayHorizon`, `locate_periods`) read them instead of the main thread's own `xrWaitFrame`. The pacer's work measure is unchanged:
  present-to-present time minus the pacing sleep.
- **Session ownership**: the XR thread owns `xrWaitFrame`, `xrBeginFrame`, `xrEndFrame` and every swapchain acquire, wait and release.
  Event polling, `xrBeginSession`/`xrEndSession` and recovery stay on the main thread, which stops (joins) and restarts the XR thread
  around any session change, so `XrSessionFault` -> `recover_xr_session` behaves as it does today. The XR thread publishes
  `shouldRender` and the session state for the main thread.
- **To check per runtime** (VDXR, SteamVR, Meta): `xrLocateViews`, `xrLocateSpace` and `xrSyncActions` on the main thread while the
  frame calls run on the XR thread. The spec allows it; a runtime may serialise or misbehave.

## 4. The generation pass (per eye)

Inputs: the packet (source camera S, the display time tS it was rendered for, the eye's previous render time tP) and the slot (time tD,
and the views located for tD, which give the destination camera D in the game world).

1. **Destination camera.** The located eye pose at tD, composed with the play-space transform extrapolated from the last two packets.
   That transform carries smooth locomotion and smooth turning, and its velocity is the difference between the last two packets'
   transforms over their time step. A snap turn or teleport between packets (the cut rule, `stale_eye_warp.hpp` `cut_step`) is not
   extrapolated.
2. **Splat.** For each source texel, find its world point from depth. Movers advance by a·xyz with a = (tD - tS) / (tS - tP): a = 0.5
   for a generated slot at half rate. Project into D and keep the nearest with `InterlockedMax` on 1/z. The still world gets exact
   parallax; the sky turns only.
3. **Rig** (arms, hands, held items; their motion texture is zero except for class 255). Each hand is moved rigidly by the controller's
   change between the packet's located grip and the grip located at tD, so the hands are as current as the head. The rest of the rig
   moves with the play space (locomotion, smooth turn), **not with the head's rotation**: tracked hands do not turn with the head.
   Step 0a (section 9): carried with the head lost on still dumps (rig 2.47 vs 0.69 levels), left in the world lost while walking
   (all 7.53 vs 5.51), moved by the player's step matched the hand oracle. `Afw2Rigid` and AFW v2's palm layer already do the per-hand
   mapping. The rig mask must be complete in every frame (arms and held items, classes 251/253 included): in a frame without it the
   player's sleeve counted as a world mover and the engine's motion vectors, which do not follow the tracked arm, tore it apart.
4. **Fill.** Jump flood from the farther neighbour, bounded by the largest displacement (as the `afw2-fill-bound` knob does). Later:
   disocclusions from the other eye of the same packet, with matched brightness (comparison doc item 6).
5. **Resolve.** Catmull-Rom from the source colour at the splatted origin. The overlays drawn into the eye (hand wheel, note tablet,
   reticle) are then drawn at the destination pose, as they are after the AFW rebuild today.

**Cost estimate** (not measured): ~1.0-1.2 ms per eye of AFW v2's ~1.9, so ~2-2.5 ms of GPU per generated frame for both eyes at
2246x2419. At half rate that is ~45 x 2.3 ms, about 10 % of the GPU; a CPU-bound PC has that to spare.

**GPU priority**: a generated frame must finish within its slot while the GPU renders the next real frame. Try these in order and measure
each under a GPU-bound load (DLAA, or resolution 1.3x):
1. the second device at normal priority (WDDM time-slices the two contexts);
2. `IDXGIDevice::SetGPUThreadPriority` on the mod device;
3. `D3D11On12` over a `D3D12_COMMAND_QUEUE_PRIORITY_HIGH` queue.

**Known limits**, as with every spacewarp:
- Transparent surfaces and particles (fire, smoke, water) carry the depth and motion of what is behind them.
- Motion that changes direction overshoots by one slot.
- Specular highlights move with the surface.
- After fast turns there are holes at the image edges.
- HUD quads are not affected.

## 5. Steps

Each step gets its own build knob, off by default until a headset check. Simulated checks do not establish headset quality or comfort.

| # | Step | Gate | Size |
|---|---|---|---|
| 0a | **Offline quality test.** An extrapolation mode in `tools/stale_warp_replay.cpp` over the existing performance-mode dumps: the kept eye at N-1 extrapolated to N+1 (two periods, harder than spacewarp's one) and compared with that eye's native N+1, scored like `tools/afw_capture_sheet.py` (hand and world error, ghost share). Baselines: the held frame, and rotation-only reprojection (what a runtime can do without motion vectors). | Clearly better than both baselines, in the hand region and while walking | S |
| 0b | **Paired-mode inputs.** Depth, motion textures and rig masks handed to `game_xr` in paired stereo too (the hand-off at `dxgi_probe.cpp:14102` runs only with `alternate_on()`). Check `EyeMotionPass`'s previous-render meaning in paired mode (one Present back) against live memory (AGENTS.md). | Lab and sim: still-world motion median ~0 px in paired mode; frame-time cost under ~0.3 ms | M |
| 0c | **Spacewarp preview** (`build/spacewarp-preview`, no new thread). In paired stereo at a rate the PC holds (72 Hz, or a lower render scale), every second Present submits the previous Present's eyes extrapolated to this Present's freshly located views instead of the rendered ones. No speed gain (the engine still renders every frame, plus ~2.3 ms of GPU every second frame). It shows in the headset what generated frames look like one period ahead, and the sim can score each generated frame against the real one it replaced. | User headset check: ghosting, hands, walking, turning, particles. **Stop here if it looks worse than VD SSW.** | M |
| 1 | **XR frame thread, no generation** (`build/xr-thread`): mod device, packets, shared overlays, frame clock, session handling. Every packet is submitted in its own slot. | Sim: same delivery and the same rendered-to-displayed time as today, no faults, clean teardown and recovery. Measure the main-thread time won by moving `xrEndFrame` off it (plan 2B.4, ~0.85 ms). Headset: VDXR and SteamVR behave as before. | L |
| 2 | **Spacewarp** (`build/spacewarp`, then a VR menu row "Spacewarp: Off / Auto"). At pacer k=2, real frames are rendered for every second slot and the slots between are generated. A real packet that misses its slot is extrapolated to that slot. Pacer thresholds and the 2 s hold are unchanged. | Sim on the user's save: the full rate delivered while real frames stay within two periods; generation GPU p95 under ~3 ms. Headset: comfort, judder, hands. | M |
| 3 | **Quality**: disocclusions from the other eye, particles and transparency, rig motion in paired mode, optionally k=3 (30 real frames to 90) | A headset check per item | M each |

## 6. How it fits with the rest

- **Performance mode (AFW)**: off while spacewarp runs, at first. They answer different limits: when `perf_mode_advisor` reports
  `cpu_bound`, suggest spacewarp; when it reports `gpu_bound`, performance mode. Combining them (alternate eyes at 45 Hz plus generation)
  is possible later, but each generated frame would then start from one stale eye.
- **Runtime reprojection**: keep VD SSW and SteamVR Motion Smoothing off. A runtime that sees a full-rate app should not engage, but rule
  out double generation in the headset.
- **Pacer**: the same rules. It now chooses between a real frame in every slot and a real frame in every second slot. Locking earlier
  once generation proves good is a headset decision.
- **Locomotion and turning**: unchanged. Only the frames between real ones are new.

## 7. Risks

- Step 1 is the big one: it touches every swapchain and the session lifecycle, and OpenXR initialisation and recovery must stay intact
  (AGENTS.md).
- Whether each runtime tolerates locate and sync calls concurrent with the frame calls.
- GPU scheduling between two devices on one GPU.
- VRAM: three packets at 2496x2688 (colour ~27 MB per eye; depth, motion and rig ~52 MB per eye at a 0.77 render scale) come to ~0.5 GB;
  two packets would cut that by a third.
- The spacewarp artefacts listed in section 4.

## 8. Not doing

- **Interpolation** (DLSS Frame Generation, FSR Frame Generation, XeFG): a full engine frame of added lag on hands and head movement, and
  D3D12-only APIs built around a desktop swapchain.
- **`XR_FB_space_warp`** (the runtime generates frames from the app's depth and motion): mainly a Quest-standalone feature. The v1105
  headset probe's noted VDXR extensions do not include it. Check a full headset extension log before ruling it out for Meta's PC runtime.
- **Any use of the game's immediate context from another thread** (the async-Present lesson).

## 9. Step 0a status (2026-11-19)

**Tool.** `tools/spacewarp_replay.cpp` (build: `cl /std:c++17 /EHsc /O2 /MD tools\spacewarp_replay.cpp`; usage in its header). From a
full performance-mode stale-warp dump it makes the rebuilt eye's image at N+1 from that eye's render at N-1 alone, and scores it against
the eye's real N+1 render (`-next.rgba`). That is two Presents of extrapolation, twice what spacewarp at half rate needs. Methods:

| Method | What it is |
|---|---|
| held | the kept image unchanged |
| rot | rotation-only reprojection: a runtime compositor without app depth |
| pos | every texel splatted as still world at its depth: a runtime that takes app depth |
| sw | own spacewarp: pos, with the rig (arms, hands, held items in the rig mask) moved by the camera's step but not its turn (`SW_RIG=play`, the default: tracked hands go along with locomotion, not with a head turn), and texels with their own motion advanced (only when the meta has the world-to-camera rotation, see below) |
| swo | sw with each hand and the rest of the rig fitted rigidly to the next frame: an oracle for the exact controller poses real spacewarp has |

The camera at N+1 is not in the dumps. The tool estimates it by direct photometric alignment of the still world (6 parameters,
coarse-to-fine Gauss-Newton, steps accepted only when they lower the cost). That brought the median kept-vs-next difference from
6-17 levels down to 1-1.8 on every dump except walking dump 1 (see below). The constant-velocity guess was off by up to a factor of two:
in these sim runs the camera does not step every Present. The scores therefore assume an exact destination camera. Real spacewarp has
the runtime's head pose plus extrapolated locomotion, so the error of that extrapolation is not in these numbers.
`tools/replay_io.hpp` now holds the JSON, PNG and half-float helpers both replay tools use.

Scores: mean |R - next| in 8-bit levels; ghost = share of changed pixels where R stays closer to the old frame. Sheets with the
worst tiles: `artifacts/spacewarp-0a/<run>/spacewarp-N.png` (columns held | rot | pos | sw | swo | next).

**Still dumps** (v1399 performance-mode sim, 2026-11-19 04:57; moved to `D:\d2vr-artifacts\stale-warp-v1399-still`). The eye moved
0-1.5 mm and turned 0-0.8 degrees over the two Presents, facing dark walls within 1.5 m:

| Method | border | rig | edge | near | far | all | ghost % |
|---|---|---|---|---|---|---|---|
| held | 1.57 | 2.73 | 11.47 | 1.11 | 12.77 | 1.43 | 100 |
| rot | 0.91 | 0.84 | 2.19 | 0.49 | 2.21 | 0.57 | 4.0 |
| pos (rig left in the world) | 0.90 | 0.69 | 1.94 | 0.44 | 2.12 | 0.52 | 2.2 |
| sw (rig moved with the player) | 0.91 | 0.84 | 1.94 | 0.44 | 2.12 | 0.53 | 2.6 |
| sw, rig carried with the head | 0.96 | 2.47 | 2.89 | 0.44 | 2.12 | 0.60 | 14.6 |
| swo | 0.90 | 0.46 | 1.94 | 0.44 | 2.12 | 0.51 | 2.1 |

Everything sits near the anti-aliasing jitter's frame-to-frame noise (~0.5 levels): these dumps only show that the tool works, and that
carrying the rig with the head is wrong when the head turns.

**Walking dumps** (v1413, run `sw0a-walk` 14:26-14:29: `head_turn_walk`, performance mode, `afw` moved aside, dump trigger
`camera=15 turn=1.5`; 3 dumps; moved to `D:\d2vr-artifacts\stale-warp-v1413-walk`). The eye moved 90-670 mm over the two Presents, depth
1.5-23 m. Default `SW_RIG=play`:

| Method | border | rig | edge | near | far | all | ghost % |
|---|---|---|---|---|---|---|---|
| held | 20.55 | 17.97 | 28.13 | 14.86 | 17.77 | 18.97 | 100 |
| rot | 18.88 | 11.41 | 22.81 | 18.41 | 15.29 | 16.05 | 52.6 |
| pos | 10.30 | 37.04 | 10.27 | 18.58 | 4.21 | 7.53 | 13.4 |
| **sw** | 8.00 | 11.30 | 9.94 | 10.96 | 4.11 | **5.51** | 13.7 |
| swo | 7.97 | 10.83 | 9.95 | 11.98 | 4.13 | 5.49 | 13.6 |

All three rig rules on the same dumps ("all"): play 5.51, carried 5.98, world 7.53 (= pos). Per dump, sw against rot: 4.46 vs 20.72 and
2.71 vs 13.19 levels; ghosting 8.0 vs 47.6 % and 4.0 vs 42.3 %. Dump 1 (a 670 mm step, possibly a vault or the gameplay start) did not
align (residual 12 levels) and scores ~9.4 for every depth method.

What this says:
- **While walking, own spacewarp beats the runtime's rotation-only reprojection by a wide margin**: the still world (far) 15.3 -> 4.1
  levels, all 16.1 -> 5.5, ghosting 53 -> 14 %. Virtual Desktop has no app depth and does not know the locomotion; its SSW estimates
  motion from the video on the headset, which this replay cannot reproduce, so `rot` is a lower bound for it, not its score.
- **The rig rule "moves with the player's step, not the head's turn" is the right one.** It wins on walking dumps (world loses: the
  sleeve is left behind) and is within 0.15 levels of the best on still ones (carried loses: the head turns). It matches the hand oracle
  (5.51 vs 5.49) because the sim's controllers did not move on their own.
- Two readings of this first walking run were corrected by the exact-camera run below: the "held gadget" near the camera is an arc
  pylon in the world (its electric arcs change at random every frame), and the "near" region is under 3 % of most images, so its mean
  is noise.

**Exact cameras (v1415, 2026-11-19 14:41-14:50).** v1415 writes `kept_camera` (`capture_stale_warp`) and `next_camera` plus
`next_play` (`capture_stale_next`) into the dump meta: WarpCamera world poses with the drawn hands, and the play space at the next
render (`write_world_pose` / `write_warp_camera` in `src/dxgi_probe.cpp`, `GameXr::kept_camera` in `src/game_xr.hpp`). It was built
into `build/night/` and installed for these runs only; `build/` is back on v1413 (sha256 `d1dd3d50...`, as before). With these
fields the tool takes the exact N+1 camera, moves each drawn hand rigidly from its kept to its next drawn pose (`SW_RIG=hands`, the
default with exact cameras), moves the rest of the rig by its own motion texture or else with the play space, and applies the motion
texture to world movers. The tool also gained a crack pass (AFW v2's Crack: background that slides into the seam of a magnified near
surface is replaced by that surface), per-region pixel shares and `SW_SHEET_REGION`.

Runs (each: performance mode, `afw` moved aside, saves verified unchanged, knobs cleaned): `sw0a-walk2` (`head_turn_walk`, trigger
`camera=10 turn=1`, 4 dumps, `D:\d2vr-artifacts\stale-warp-v1415-walk`), `sw0a-run` (`run_hands`: walk, run, sprint; `camera=20`;
4 dumps, `...-v1415-run`), `sw0a-sword` (`sword_sweep`; `moving=20`; 1 dump, `...-v1415-sword`). The triggers fire rarely: 4 of 8
requested.

| Set | rot | pos | **sw** | swo (hand oracle) | ghost % rot / sw |
|---|---|---|---|---|---|
| walk (4 dumps) | 18.63 | 7.69 | **6.68** | 6.65 | 49 / 14 |
| run (4) | 14.84 | 7.29 | **5.09** | 5.08 | 100 / 16 |
| sword (1) | 6.91 | 6.94 | 7.11 | 7.04 | 100 / 100 |

Rig region (5-8 % of the image when present), mean over the set: walk rot 13.2, pos 28.3, sw 13.4, swo 12.0; run rot 3.2, pos 33.7,
sw 3.3, swo 2.7. Rotation scores well on the rig only because nothing turns while walking straight: the rig stays in place in the view.

What the exact cameras add:
- **The camera estimate was right**: the alignment was off by 0.00-0.06 degrees and 0.0-5.6 mm (5.6 mm on a 677 mm step). The
  estimated-camera results above stand.
- **Spacewarp with exact hand poses equals the hand oracle** on walking and running (6.68 vs 6.65, 5.09 vs 5.08). While running straight,
  rotation-only reprojection does nothing at all (its ghost share is 100 %): sw 5.1 vs rot 14.8 levels.
- **Object motion from the engine's motion vectors is only safe where the rig mask exists.** Walk dump 4 had no rig mask; the player's
  red sleeve then counted as a world mover and the engine's vectors (which do not follow the tracked arm) tore it apart (movers 43.6 with
  the vectors vs 33.7 without). Run dump 4 had a mask that missed one bare forearm: the vectors placed it about right. Run dump 3's movers
  improved 22.9 -> 10.5. There were still no NPCs in these saves' paths, so NPC motion is untested.
- **Unpredictable content stays**: animated effects (the arc pylon's lightning), a global brightness change (sword dump), and what a
  fast hand uncovers that the old frame never saw. The sword dump (a fast slash, where the game's attack animation drives the hand, not
  the tracked pose) is the case none of the methods handle: everything sits at the held frame's 6.9-7.1.

**Verdict for 0a.** Extrapolation is clearly better than what a runtime can do without app depth, for the world (most of the image) and,
with the controllers' poses, for the hands; the hand rule is settled. Requirements found for the real pass: a complete rig mask in
every frame (arms and held items, 251/253 included) before any object motion is trusted; a crack stage; the other eye of the same
packet as a fill source for what a moving hand uncovers (section 4, step 4). Not covered offline: NPCs, real VD SSW, the error of
extrapolating the locomotion (the scores use the exact destination camera), and headset comfort. Next step: 0b.

Capture commands for more dumps (game closed; `alternate-eye` already sits in `build/`, so it is not passed; install a v1415+ DLL for
exact cameras with `build/night/use.sh <ver>` and restore `use.sh 1413` afterwards; new dumps restart at `stale-warp-1` in
`artifacts/`, so move earlier ones to D: first; each full dump is ~250 MB):

```
python tools/win_perf_sim.py --name sw0a-walk2 --scenario head_turn_walk --disable afw --knob diagnostics --knob "stale-warp-dump=0 2 8 full camera=10 turn=1"
python tools/win_perf_sim.py --name sw0a-run --scenario run_hands --disable afw --knob diagnostics --knob "stale-warp-dump=0 2 8 full camera=20"
python tools/win_perf_sim.py --name sw0a-sword --scenario sword_sweep --disable afw --knob diagnostics --knob "stale-warp-dump=0 1.5 8 full moving=20"
```
