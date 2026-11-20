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

## 10. Real spacewarp, first version (v1416, 2026-11-19)

**Design change: no XR thread.** Section 3's XR thread was replaced by a smaller design that the code map (session, swapchains, overlays:
all on the game's Present thread and device) and the timing allow. At half rate the main thread already idles ~10 ms per game frame
in the pacer, and right after a real frame is submitted the GPU is free. So, inside `GameXr::present()`:

1. After the real frame's `xrEndFrame`, `generate_spacewarp_` builds the next slot's frame from it: both eyes' final images (kept by
   `keep_spacewarp_eye_` before their release, overlays included so nothing drawn into the eye flickers), the per-eye depth copies, the
   game cameras. It acquires each eye swapchain's next image, draws the generated eye into it (`SpacewarpPass`) and releases it, and keeps
   the layers: the generated projection plus the real frame's quads (OpenXR shows a quad's last released image).
2. At the next Present, before the pacing sleep, `submit_spacewarp_` waits for the in-between slot and submits that frame. If the
   runtime is already at the real frame's slot, or the main thread came back too late (less than half a period before the real frame's
   wake), the generated frame is dropped (counted `late`) and that wait serves the real frame.

No second device, thread or shared texture; every overlay path is unchanged. The cost of this simplicity: a generated frame is on time
only when the game's frame work leaves the main thread back in the hook before its slot (work up to about one period plus the runtime's
slack). The XR thread stays the fallback if headset tests show too many late frames.

**Pieces.**
- `src/spacewarp_pass.hpp`: the GPU pass (D3D11 compute plus one full-screen pixel shader). The rules are 0a's: each hand by its rigid
  motion, the rest of the rig by the play space's step, world movers by the motion texture, the still world by parallax, the sky by
  rotation; then Splat (depth test as `InterlockedMax` on 1/z), Crack, Fill (the farthest of the first surfaces in 8 directions), and
  Resolve (cell depth back into the source image, bilinear in linear light, sRGB encoding for a UNORM target). Without a rig mask (true
  stereo) the rig is what lies within a drawn palm's reach (0.22 m) or nearer than 0.5 m. Lab: `tests/spacewarp_pass_check.cpp` (WARP,
  13 checks).
- `src/spacewarp_math.hpp`: the cameras. Play space O = S V^-1 (game camera S, tracked view V), locomotion = the change of O over the last
  three real frames scaled to one slot, head and grips = their tracked change from the image's own predicted time to one slot later. In
  source camera coordinates: target = L (V^-1 dV V), hand = L (V^-1 dG V), rest = L. Lab: `tests/spacewarp_math_check.cpp`.
- `src/game_xr.hpp`: keep / generate / submit as above. Pacer thresholds while spacewarp is on: half rate above 1.0 period of work,
  full rate below 0.8 (`FramePacer::lock_tenths`/`unlock_tenths`, defaults unchanged otherwise); the knob text `always` holds half rate.
  Status under `pacer.spacewarp`: generated, submitted, late, unusable (+ `skip` reason), failures, cuts, `play_mismatch_mm` (the two
  eyes' play spaces apart: a check of the camera/view pairing), not_kept (+ why), presents, gameplay_presents, gated, captures.
- `src/dxgi_probe.cpp`: knob `build/spacewarp` (true stereo only; ignored in performance mode); Setup records both eyes' game cameras
  in true stereo; Present hands cameras and FSR/DLSS depth copies to GameXr; console `spacewarp capture N` (diagnostics) saves eye 0
  triples (real, generated, next real) for `tools/spacewarp_capture_sheet.py` and `tools/spacewarp_capture_shift.py`.
- Launcher: feature row `spacewarp` in `tools/default_features.json` (off by default, shown only when the DLL contains the knob).

**Sim results** (v1416, `build/spacewarp` = `always`, performance mode and `afw` moved aside, saves verified unchanged, v1413 put back
after every run):
- Every eligible gameplay frame got a generated frame: e.g. 223 generated of 282 gameplay Presents, the rest accounted for (47 without
  an eye pair yet, 11 missing input, 1 authored camera); 8-11 % of generated frames came back late; 0 failures, 0 runtime rejections.
- `play_mismatch_mm` 0.001-0.005: both eyes give the same play space, so the camera and view conventions hold.
- Walking and running straight (`run_hands`), phase correlation over 256 px tiles: the generated frame moves along the real motion
  (perpendicular part 0.01-0.03 of it) by 0.19-0.68 of the next real step (expected 0.5) with a three-frame average velocity. The
  scatter follows the game's own uneven steps and the run's acceleration (the step doubled within two frames). The last step alone gave
  0.33-0.91.
- The head's own change cannot be checked in the sim: the simulated runtime returns the current pose for any requested time. On a real
  runtime it comes from the runtime's prediction, and the compositor's reprojection corrects head rotation anyway.
- Found on the way: at half rate the real images' head poses are predicted about two slots past their display slot (the locate horizon,
  `locate_periods` x the locked period). The generated frame now predicts one slot after the image's own time, so it never turns back
  between two real frames.

**Not known yet (headset):** whether generated frames land in their slots on VDXR and SteamVR (`late` share), how they look and feel,
the pass's GPU time on the RTX 5070 (estimate ~1 ms per eye), and false "rig" near walls (anything within 0.5 m follows the player).
NPCs and other movers are not extrapolated in true stereo (no motion texture there yet), and the eye overlays (reticle, wheel) are warped
with the scene instead of being redrawn.

**To try it in the headset:** the launcher (launcher/dist, v1416 embedded) has a **Spacewarp** row on its Settings page, under Performance mode: Off / Auto / Always (half rate), written to `build/spacewarp` on Save. Spacewarp and Performance mode exclude each other: on Save the row changed last wins and the other is turned off. Use an FSR or DLSS anti-aliasing mode (Save warns otherwise) and turn off Virtual Desktop's SSW. Back: Spacewarp Off, or `build/night/use.sh 1413`.

**Launcher settings fix (same build).** The launcher read and wrote settings-file version 11 while the mod has written version 12 since the AFW merge (v1411: the VR menu's Renderer row). Reading a game-saved file failed, so the form showed defaults, and Save aborted ("Not saved") because it re-reads the file to merge. `launcher/Prefs.cs` now reads and writes version 12 with the renderer. The Performance mode row also writes that renderer and shows it: a saved renderer of 1 or 2 starts performance mode even without `build/alternate-eye` (dxgi_probe.cpp), so the launcher showed Off while the game ran AFW. Launcher tests: 199 pass (the 7 that failed before were stale: version 10/11 expectations, an old default-off list, a locale-dependent size string).

## 11. First headset test and v1417 (2026-11-19)

**Report.** Auto and Always (half rate): doubled images almost all the time; Auto seemed to switch on and off at the wrong places;
motion felt about as smooth as 90 fps, but Virtual Desktop's frame-rate overlay did not show 90.

**Cause 1: the generated frame used the wrong camera (constant doubling).** The headset eyes are rendered canted (v808: turned,
symmetric frustum) and resampled to the straight view before submission (`uncant_`). The image in the swapchain and its layer pose are
the straight view; depth, the game camera S and the tracked view V are the canted render's. v1416 read the swapchain image as if it were
the canted render and submitted the generated eye with the canted pose and FOV: every generated frame was turned and stretched by the
cant, so even standing still it differed from the real frame. Shown as real/generated/real alternating at 90 Hz, that is a double image.
The sim never saw it, because its symmetric default FOV is never canted.
- Fix (`game_xr.hpp`, `spacewarp_pass.hpp`): each kept eye also keeps its layer (pose Vimg, FOV). The output camera is Vimg moved by
  the head and the play space, `L V^-1 dV Vimg` in source coordinates, with the layer's FOV; the pass reads colour through Vimg's camera
  (`colour_camera`, `colour_m` = rotation of `V^-1 Vimg`, `colour_tan`) while depth stays the canted render's; the generated layer is
  submitted with `dV Vimg` and the layer FOV. Uncanted, Vimg == V and this is v1416.
- Hole cells: the back-projected point is used wherever the source sees it at that depth (`Seen`: within 2 %); for filled cells only
  when both the seed and the texel it lands on are still. Otherwise the cell's own source point.
- Lab: `tests/spacewarp_pass_check.cpp` 15 checks; the new canted case (straight output from a canted source with a colour camera) is
  exact, and 37 levels off without the colour camera.
- Sim with the headset's FOV (`build/sim-fov` = `-0.698 0.942 0.768 -0.960`, 1608 eyes uncanted, Always): standing still the generated
  frame equals the real one, 0.05-0.09 levels apart (|G-R|); 792 generated, 729 submitted, 63 late, 0 failures.

**Cause 2 (probable): the generated images were released early.** v1416 released each generated eye right after drawing it, before
the generated frame's own `xrEndFrame`. A projection layer names a swapchain, and the runtime shows its last released image. A runtime
that reads the image when it composes, not when `xrEndFrame` names it, could then show the generated image in the real frame's slot and
drop a slot, which would also explain a frame counter that does not reach 90. v1417 keeps the generated images acquired until just after
the generated frame's `xrBeginFrame` (`release_spacewarp_images_`), and releases them in every skip or failure path.

**Auto switching.** Auto now decides on the 80th percentile of the last 32 frames' work instead of the median (the frames that miss
their slot are the slow ones), and holds each cadence change at least 5 s while spacewarp is on (2 s otherwise). Thresholds unchanged:
half rate above 1.0 period, full rate below 0.8.

**Evidence from the next headset run.** `artifacts/xr-live-status.json` now always carries `"spacewarp"` (no diagnostics needed):
`k` (2 = half rate), `work_ms_p50`, `locks`/`unlocks`, `generated`, `submitted`, `late`, `unusable` + `skip`, `failures`,
`gameplay_presents`, `not_kept` + why, `play_mismatch_mm`. `submitted` close to `generated` and a low `late` share means the generated
frames reach their slots; if VD's overlay still shows ~45 then, its counter counts only frames whose content it considers new, or VD's
own SSW is on.

**To test:** launcher/dist (v1417 embedded), Spacewarp Always, then Auto; FSR or DLSS anti-aliasing; Virtual Desktop SSW off.

## 12. Held weapons (v1418, 2026-11-19)

**Report (v1417 headset):** "works quite well"; only weapons double: the tip of the sword, guns and crossbows. The part near the hand is
fine.

**Cause.** True stereo has no rig mask, so the pass gives a hand's motion only to what lies within its palm's reach (0.22 m). A held
thing is longer: in the sim dumps with a mask (`D:\d2vr-artifacts\stale-warp-v1415-*`, class 253 held) the held pixels lie 0.22-0.43 m
from the palm, all ahead of the grip (grip-local forward -0.08..+0.10 m), while the forearm (classes 255/252) lies 0.17-0.33 m behind
it. Past 0.22 m a blade or barrel was scenery (or, within 0.5 m of the eye, the body's motion), so it stayed behind the moving hand:
real and generated frames alternated between two places. Performance mode never had this, because its mask marks held draws and
moves them within 0.45 m.

**Why not the performance-mode mask.** That mask comes from replaying the rig and held draws of the one-view render into a target keyed
by the scene depth (`rig_mask.hpp`); true stereo renders two views per frame and the replay, the target and the takes are built for one.
Porting it is engine work. Tested first instead, offline on the dumps' real depth: the image-space walk from a blade texel to its palm
fails because the palms are often off-screen (the sim's hands are below the view: 0-18 % of the blade found).

**Rule (`spacewarp_pass.hpp` FarHand, `game_xr.hpp` keep_spacewarp_prev_).** A point past its nearest palm's reach and within 0.5 m of
it, at most 0.12 m behind that hand's grip (the forearm side), is the hand's when the previous real frame of the same eye confirms it:
its depth there shows a surface where the hand's last step puts the point (drawn grips, world: `S_prev^-1 G_prev G_now^-1 S_now`) and
not where the point would be had it stood still in the world (`S_prev^-1 S_now`). A test passes within 0.5 % + 3 mm of one of the 2 x 2
texels around the point (or within their range on one smooth surface). So a wall or a table near the hand is confirmed still and stays,
an NPC that does not move with the hand is not taken, and a still hand changes nothing. Each eye keeps a copy of its depth (FsrTaa's is
rewritten every frame: ~22 MB per eye, one GPU copy), its game camera, drawn grips and FOV. Off for the first generated frame after a gap
(no previous frame). Limits: a weapon that moves less than its own width in one real frame is "confirmed still" where old and new
overlap, so very slow motions keep up to half that width of doubling; parts hidden in the previous frame keep the old rule.
Status: `held_far_eyes` (generated eyes with the rule on).

Lab: `tests/spacewarp_pass_check.cpp` test 9 (blade follows, shelf beside the hand stays, a thing behind the grip stays, the palm-owned
hand unchanged, rule off = v1417, a still previous frame takes nothing); a mutant with the grip gate off fails the forearm check.

**Sim results (v1418, headset FOV, spacewarp Auto as the user left it).** The user's latest save has no sword drawn, and neither the
scabbard-zone draw nor the B toggle (vetoed by manual equip) drew one in the sim, so no weapon was seen there. The simulated runtime also
ignores locate times, so the grip step from the source to the target time is zero and hand motion is never extrapolated in the sim (v1417
too: generated hands sit on the real frame's place, along ratio 0.00); the sim cannot show the weapon fix itself. What it shows:
- no regression: sword_sweep and the new `tools/linux/scenarios/sword_draw_sweep.py` (hands sweeping 0.75 and ~3.5 m/s), 0 failures,
  2-2.4 % late, the rule on for 99.9 % of generated eyes (`held_far_eyes` 4696 of 4698);
- the rule on real game data: with the diagnostic knobs `spacewarp-palm-reach` = 0.05 (so the moving hand itself must be found by the
  rule) and `spacewarp-tint-held` (taken pixels magenta), the rule took exactly the sweeping right hand (glove and fingers) in every
  triple where it moved, and not the forearm sleeve (behind the grip), the floor (confirmed still) or the still left hand
  (`D:\d2vr-artifacts\sw-v1418-tint`). So the drawn grips, the previous depth copy and the cameras line up in the game.

**To test (headset):** Spacewarp Auto or Always; swing the sword, aim the pistol and crossbow while moving the hand. The tips should no
longer double. Still possible: very slow motions keep a little doubling (see Limits); a wall the weapon touches may move with it.

## 13. Weapon bodies and a faster Auto (v1419, 2026-11-19)

**Report (v1418 headset):** the weapon is much more stable, but there is still some ghosting and the sword's tip still doubles a lot;
"make the weapon interpolate the way the arms do". Auto engages a little late. Everything else good. The session's counters:
5067 generated, 4288 submitted, 779 late (15 %), the v1418 rule on for every generated eye, 5 locks / 5 unlocks.

**Cause of the tip.** The mod's own sword model (`dxgi_probe.cpp` `sword_blade_length`, the weapon collision line: "the sword's visible
tip is .63 m out") is 0.65 m from the grip along its forward axis (reversed in a reverse grip). v1418's rule ended 0.5 m from the palm,
so the outer part of the blade was never the hand's.

**Fix.**
- Weapon bodies: at Setup, next to the drawn grips the cameras record, `dxgi_probe.cpp` takes each armed hand's body from the weapon
  collision model (the sword: the grip along `sword_blade_axis` x 0.65 m, radius 2 cm; a pistol or crossbow, or a dual-wield gun copy:
  its drawn gun frame along the barrel, `gun_shape` length and radius) in the grip's own frame. GameXr places it on each eye's drawn grip
  (`spacewarp_weapon`), widened by 4 cm, and the pass gives a hand everything within its body unconditionally (`WeaponHand`), exactly as
  within its palm's reach: the whole weapon moves with the hand, no previous frame needed. A power (Blink...) counts as "armed" and
  `authored_gun_frame` falls back to a pistol frame for it: no body there (sim: caught before shipping).
- The previous-frame rule stays for what the bodies miss (a crossbow's bow, a prop): its reach is now 0.8 m, and the hand's own test
  allows 1.5 cm per metre from the palm.
- Auto: locks at 0.9 periods of 80th-percentile work (was 1.0) and only 0.5 s after an unlock (`FramePacer::lock_hold_ns`; leaving half
  rate still waits 5 s and needs < 0.8 periods).
- Status: `weapon_eyes` (generated eyes with a weapon body), `late_wait` (the game frame came back too near the real slot) and
  `late_slot` (the runtime's wait already gave a later slot), so the next headset run tells which kind the 15 % late are.

Lab: spacewarp_pass_check test 10 (a blade 0.7 m out follows the hand with no previous frame; a thing beside it stays; without the body
it stays); frame_pacer_check (lock 0.50 s after an unlock with the short lock hold; the unlock still waits the full hold).

**Late frames (not fixed here).** A generated frame is submitted at the next Present, so it is on time only when the game's frame work
leaves the main thread back before its slot. At the user's ~9.5 ms p50 / 11-12.6 ms p90 work, about every sixth game frame takes longer
than a period and its generated frame is dropped; the runtime then shows the real frame again for that slot, which reads as a stutter
of whatever moves (the sword tip most). The fix is the XR frame thread of section 3; it needs the D3D11 immediate context shared safely
with the game's render thread, and is the next step if `late_wait` dominates.

**Sim (v1419, headset FOV, Auto, sword_draw_sweep; the sim save holds no weapon, so the bodies themselves are not visible there):** 0
failures; tinted captures at the normal reach show no pixel taken by either rule (empty hands, floor, forearms); the first build gave
the Blink hand a body (`weapon_eyes` 4), the final one none (`weapon_eyes` 0); late 3.5 %, all `late_wait`.

**To test (headset):** swing the sword (also in a reverse grip), aim the pistol and the crossbow while moving the hand, and note whether
Auto now engages soon enough. Then `artifacts/xr-live-status.json` `spacewarp`: `weapon_eyes` should be most generated eyes while armed,
and `late_wait` vs `late_slot` says which kind the late frames are.

## 14. The generated frame's own thread, and an instant Auto (v1420, 2026-11-19)

**Report (v1419 headset):** weapons fine now. "Move the frame generation to its own thread so it does not fall behind": when a heavy
moment starts, low fps shows for some seconds; generation should start at once. The session's counters: 10851 generated, 5710
submitted, **5141 late (47 %)**, of them 4578 `late_wait` (the next Present came too late to submit) and 563 `late_slot`; 10 locks.
So in heavy moments spacewarp was on but half its frames never reached the headset: the user saw ~45 fps.

**Research (VDXR source, OpenXR spec; agent report 2026-11-19).** VirtualDesktop-OpenXR, default mode: xrAcquire/Wait/ReleaseSwapchainImage
make no D3D11 calls (release only records the index; the image is taken at xrEndFrame); xrWaitFrame makes none; xrBeginFrame and
xrEndFrame make single, state-free calls on the app's immediate context (GPU timer queries, `ID3D11DeviceContext4::Signal` of a shared
fence); copies, shaders and the OVR submit run on VDXR's own device and thread. The spec lets xrWaitFrame/BeginFrame/EndFrame run on
another thread if they are ordered. So a submit thread is safe on VDXR with D3D11 multithread protection (not as-is: a data race), and
not known to be safe on SteamVR (closed; its OpenVR base queues work on the app's device).

**Design.**
- The Present thread draws the generated frame right after the real xrEndFrame (as before), releases its images at once and posts the
  layers to the submit thread; that thread waits for the slot (xrWaitFrame), begins and ends the frame, however long the game then
  takes for its next frame. The next Present collects the result before its own frame calls, so the two threads' frame calls never
  overlap and keep their order; a wait that already came back at the real frame's slot is handed to the real frame (as v1419).
- Only on VDXR (runtime name "VirtualDesktop...") and the in-DLL simulator (no runtime name), on a device that is not single-threaded,
  with multithread protection switched on in the GameXr constructor (`d3d_protection_wanted`, set by the probe from the knobs). Not
  later: protection swaps the immediate context's methods, and the probe's context hooks patch those methods. The first build switched
  it on at the first generated frame; the hook registry (`gpu_dispatch_bindings.hpp`) met new targets, ran out of its four bindings per
  method and poisoned itself ("registry poisoned; restart process" on every frame, sim 2026-11-19). The probe installs its context
  hooks after the first GameXr construction ("may alter context dispatch"), so they bind the protected methods from the start.
  Otherwise, or with `build/spacewarp-no-thread`, the v1419 path. The thread's xrBeginFrame..xrEndFrame and the game's own DXGI Present share a gate (`GameXr::dxgi_gate`,
  `dxgi_probe.cpp gated_present`).
- Drained before resize, abandon_frame and close; stopped in close.
- Auto, instant: `FramePacer::fast_lock` (spacewarp) locks as soon as two of the last three frames missed their slot, and frames past two
  periods are kept (clamped) instead of restarting the work window (a heavy moment kept restarting it: no lock for seconds).
- Status: `thread`, `thread_why`, `mt_was`, `thread_frames`, `thread_lead_us` (how long before the next Present the thread's frame was
  ended), `cut_m`/`cut_deg` (the play-space step of the last locomotion cut).

Lab: frame_pacer_check (9 -> 14 ms frames lock after 2 frames; 25-30 ms frames lock after 2 frames instead of restarting; the plain
cadence still never locks on them). Sim, the user's latest save (a heavy scene, ~22 ms frames, pistol in the left hand), back to back:
without the thread 1865 generated, **80 submitted**, 1784 late (96 %); with it 2022 generated, **2021 submitted, 0 late**.

With protection at construction (final v1420), same save back to back: thread 1814 generated, 1813 submitted, 0 late, 0 failures, no
registry poison; without 2174 / 101 / 2072 late. The game's CPU time between Presents p50 11.2 ms (thread, protected) vs 12.6 ms
(without): no protection cost visible. `cuts` (locomotion not extrapolated: a play-space step of 0.5 m or 25 deg between two real
frames) varies by session in this save (0-1180, also without the thread); the last one was a 0.76 m step.

**To test (headset):** Spacewarp Auto; walk into heavy scenes and fights. Spacewarp should engage within a few frames and the frame
rate should stay at 90 (VD's overlay). Then `artifacts/xr-live-status.json` `spacewarp`: `thread` true, `thread_frames` close to
`generated`, `late` near 0. If the game misbehaves with the thread (crash, stalls), `build/spacewarp-no-thread` restores v1419.

## 15. v1420 headset: 84-90 fps; a crash; arm freezes while moving (v1421, 2026-11-19)

**Report (v1420 headset, Quest 2 over VDXR):** frame rate steady at 84-90 fps ("brilliant"); the session crashed after ~5 minutes; while
walking or running, an arm "freezes for a second" mid-swing (as if a controller lost tracking), then comes back.
Status at 21:19:00: 9488 generated, 9486 submitted, 1 late (`late_slot`), 0 failures, thread on, 4 locks / 3 unlocks.

**The crash: no evidence left.** No Application Error / WER report, no new local dump (LocalDumps works for this game: dumps from the
morning exist), no System event (no TDR), VDXR's own log stops at session start; the mod's crash observer only records with diagnostics.
The process ended abruptly within seconds of 21:19:00. v1420 is the first build that calls into VD's runtime from two threads: the
submit thread's xrWaitFrame/BeginFrame/EndFrame could run beside the Present thread's xrPollEvent / xrEndSession (session events are
handled before the old collect point), and beside controller vibrations, which dxgi_probe asks for in the middle of a frame (weapon and
hand contacts); VD's LibOVRRT has crashed on haptics before (GameXr::pulse comment). Not proven, but every such overlap is removed:
- the Present thread collects the thread's frame at the very start of present(), before any OpenXR call of that Present (events,
  xrEndSession, overlays, input, haptics, the real frame): the thread runs only while the game works outside the hook;
- `pulse` only queues; present() applies the vibrations right after the collect;
- the thread body cannot terminate the process (every path caught; a thread that stops on an error is replaced at the next post);
- no endless waits: the collect gives up after 1 s (that Present makes no OpenXR call and the game runs on; `stuck` counts them), and
  drain/stop after 2 s (stop then detaches the thread instead of hanging the game's exit);
- a wait the thread handed to the real frame survives an early return of present() (it must be begun before the next xrWaitFrame).

**The arm freeze: cause not known yet.** The mod draws a hand where its own rules put it: the wall clamp, the arm's reach clamp, the
physical hand body (only in contact, within 10 cm), the game's authored arm animation (a slash presses the game's attack), and nowhere
when the runtime reports no valid grip pose. The sim (always tracked, no wild swings) shows none of them while running. The always-written
status now carries what the next headset session needs: `hands` (per grip: samples, `invalid` = no valid pose, `invalid_longest_ms`,
`inferred` = valid but not optically tracked) and `arms` (gameplay frames, frames an authored clip owned each arm, attack presses,
wall-clamp contact frames, reach-clamped frames, snapbacks, weapon contact frames, and which pose each hand was drawn at).

Sim (v1421, run_hands, no diagnostics: the headset's status mode): 1393 of 1394 generated frames submitted, 0 late, 0 stuck; the
`hands`/`arms` blocks are written. **Next headset session:** reproduce the freeze (walk/run and swing), then read `hands.invalid` /
`invalid_longest_ms` (the runtime lost the grip), `arms.authored` and `attack_presses` (the game's attack animation took the arm),
`arms.reach_clamped` / `clamp_contact` (the mod held the hand at the arm's reach or a surface), `hand_visual` (the physics body drawn).

## 16. The v1421 hang, and a thread that only waits (v1422, 2026-11-19)

**Report (v1421 headset):** the game crashed again; the arm did not follow the controller ("like a controller connection interruption,
which never happened before the new thread"); held objects show many artifacts in generated frames; VD was left frozen for debugging.

**The hang, from the live process (pid 26536, user's go-ahead to debug; read-only stack scan, `D:\d2vr-artifacts\hang-v1421`: stacks,
status, a 204 MB minidump).** The status file had stopped updating (game layers 7667, spacewarp 5196 generated / 5193 submitted). Thread
stacks (heuristic scan of return addresses, the probe's own frames named from its linker map):
- the spacewarp thread: `GameXr::spacewarp_thread_main_` -> virtualdesktop-openxr.dll (xrBeginFrame/xrEndFrame internals) -> d3d11.dll
  -> ntdll critical-section wait: blocked on the device's multithread-protection lock;
- the game's render thread: inside the Steam overlay's Present hook (gameoverlayrenderer64, which runs before the probe's hook and so
  outside `gated_present`) -> d3d11.dll -> nvwgf2umx.dll waiting on an event;
- an NVIDIA driver worker thread spinning at 100 % CPU (`GetCurrentThreadId` in a loop: a driver spin lock).
A lock cycle between D3D11's multithread lock, the NVIDIA driver's threaded optimization and VDXR's immediate-context calls from the
second thread. Shorter versions of the same contention stall the render thread without deadlocking: the real frames stop for a moment
while VD keeps reprojecting the head, so only the hands look frozen. That was the "arm freezes for a second" of v1420/v1421.

**v1422 design: nothing touches the game's D3D11 device off the render thread.**
- The thread only waits: it calls xrWaitFrame (no D3D11 call in VDXR) for the generated frame's slot. When the slot opens the frame is
  *due* (`GameXr::spacewarp_due`).
- The game's render thread begins and ends the due frame at its next safe point: the probe's RSSetViewports / OMSetRenderTargets hooks
  call `spacewarp_safe_point` on the game's immediate context, on the render thread, outside `GameXr::present()` (which refuses: its own
  frame calls may be open), before the game's call goes on. VDXR's calls there are single and state-free (timer queries, a fence Signal).
  Failing a safe point, the next present() ends it before the real frame, as v1419 did.
- No D3D11 multithread protection (never switched on), no thread in xrBeginFrame/xrEndFrame; the Present gate stays but nobody else
  takes it.
- Status: `safe_frames` (ended at a safe point), `fallback_frames` (at present), `due_us_last` / `due_us_max` (slot open -> frame ended).

**Held objects.** A held thing (`publish_held_object`: its model box, its pose, the drawn grip) is now the hand's body like a weapon's:
the box as a capsule along its longest world half axis, radius covering the cross section, in the drawn grip's frame (the hand as drawn:
the requested grip plus the held clamp's shift). It replaces the weapon body while that hand holds something and is used for 250 ms after
its last publish. Things over 1.5 m long or 1 m across (a corpse) are not taken.

Sim (v1422, the user's latest save): run_hands 2304 generated, 2303 submitted, **all at a safe point** (0 at present), slot open ->
frame ended 0.18 ms last / 2.9 ms at most, 0 late, 0 failures, no registry poison; oil_tank_throw 2433 of 2434 at safe points, at most
5.7 ms. The save has no tank, so the held body was not exercised in the sim (same capsule rule as the weapon bodies, lab test 10).

## 17. Arms freezing while walking; carried things (v1423, 2026-11-19)

**Report (v1422 headset):** no hang any more; the game arm still "loses the controller and hangs in the air for a second" while walking
or running (fine standing still); corpses and weapons artifact strongly while walking.

**The arm (session 22:51-23:00, diagnostics on).** The controller is never lost: `controller-trace.csv` has 3960 rows (later 7170), all
`tracked_hands`=3, no controller pose held still for 140 ms or more, also while walking. What freezes is the full-body arm: 730 of 17006
arm uploads per side fell back to the native arm (`arm_flicker`: 556 `track_aged`, 173 `publish_refused`), in 26/22 episodes of up to
197 uploads. `track_aged` means the arm-track the Setup publishes was older than `tracked_pose_freshness_ms` (250 ms) by one of three
clocks when the arm palette was uploaded. Ruled out here: present() never ran longer than 19 ms in gameplay (no stall); sample_input()
runs every present(); full_body.publish() runs every Setup (solves = frames) and the per-frame Setup block never paused while walking
(controller trace); no C++ exception in the session's first-chance ring (only the guarded game reads); the early returns between the
trace and the hands block are inside a lambda. The same long fallback runs appear in the sims of v1416/v1417 (before the thread), so the
mechanism predates spacewarp. Which clock ages is not known yet, so v1423 records it (always, in both status files, block `arm_freeze`):
- `stale.hits` / `max_ms` / `last_ms` per clock: [0] the publication tick, [1] the controller sample tick, [2] the time since publish;
- `publish_gap_max_ms`, `publish_gaps_250`: gaps between two arm-track publications;
- `grips.sample_gap_max_ms`: the longest gap between two tracked controller samples;
- `episodes` (the last 12): side, start tick, length (ms), uploads, reason, the player's speed when it began.
The `hands`/`arms` blocks of v1421 were only in the status written without diagnostics; `arm_freeze` and `arm_flicker` are now in both.

**Carried things (`spacewarp_pass.hpp` carryTerms, `game_xr.hpp` prev.O).** What the player carries moves with the play space: a body on
the shoulder, a dragged corpse, the player's own body and legs. A point between rig_near (0.5 m) and 1.6 m from the eye whose previous
real frame shows it where the play space's step puts it (`prev_rest` = S_prev^-1 O_prev O_now^-1 S_now), and not where it would be had
it stood still in the world, takes the rest-of-rig motion (the locomotion). Static walls and floors are confirmed still and stay. Lab test
11 (a carried box follows, a still shelf beside it stays, rule off = v1422).

**Weapons while walking.** The drawn weapon trails the grip the camera recorded by the player's step times the render latency; the weapon
body is widened by three generated slots' locomotion (at most 10 cm more).

**The arm, found: the hand clamp's hold at half rate.** The v1423 sim (walking and running, `run_hands`) did not reproduce an aged arm
track in play: its 202 `track_aged` uploads per side are one run of 209 at the end of the run (the quit), and the clocks' 15 s maxima come
from one 15 s present at the first gameplay frame (the transfer stage) during which the game's arm uploads went on. What the sim did show:
the hand clamp in contact on 333 of 874 frames and the reach clamp on ~55 %. The drawn hand passes through the clamp (`hand_clamp.hpp`),
which draws a target only where an answered sweep proved the way free; a target off the last answered segment is *held* (last frame's
hand) for up to `hold_seconds` (0.25 s), then followed unvalidated. Offline (`tests/hand_clamp_check.cpp`
`walking_swing_at_half_rate_follows`, the clamp itself, strict answers, free air): at 90 Hz the hand is never held; at 45 Hz (spacewarp's
half rate) a swinging hand is held on 27-80 % of frames, 12-frame runs at every swing reversal, up to 0.75 m behind the controller while
walking at 4.5 m/s. Two causes:
- the sweep's look-ahead covered the root's motion for the measured answer round trip (plus the longest recent frame) but the hand's own
  motion for one frame only: answered two frames after its post, a 2.4 m/s swing at 45 Hz (5 cm a frame, the whole palm_radius) left
  every answered segment;
- the root over-prediction (two frames of walking for a one-frame round trip) bends the swept line away from where a reversing swing goes.

v1423: the controller-relative look-ahead covers the same time as the root (the round trip, at least the longest recent frame, of the last
step's velocity; cap scaled alike), and a hold draws the answered centre-line's point nearest the target (the sweep proved a palm sphere
centred there free; not a palm_radius past it: corner tests caught that reaching into unmet walls), never nearer the target than the eased
push. Offline, walking 4.5 m/s with a 0.4 m swing at 45 Hz: the hand trails the controller by at most 75 mm (mean 26 mm) instead of 748 mm
(mean 187 mm); standing with answers two frames late it is never held (was 27 %); a wall beside the walk is never entered (legacy: up to
53 mm). Knob `build/legacy-clamp-lead` restores the old rules. Status `hand_clamp` adds `holds`, `hold_runs`, `hold_longest_ms`,
`proven_draws` per side.

Sim (`run_hands`, final v1423, `D:\d2vr-artifacts\sw-v1423b`): 530/530 generated frames submitted at safe points, 0 late, 0 failures;
the clamp held 2/4 single frames (22 ms), none drawn on the answered line: the sim's controllers do not swing like a player's, so the
fix is shown offline only. Not headset-tested.

## 18. Soft edges: the silhouettes around moving things (v1424, 2026-11-20)

**Report (v1423 headset):** "everything is very good"; only ghosting remains: around the body, the hands, weapons, carried objects and
bodies, corpses and the player's own body, a silhouette-like frame realigned with the position, a lot of ghosting.

**Cause.** The colour image's edges are soft (anti-aliasing, DLSS/FSR upscaling from the render size, and the depth's own sub-texel jitter)
while the depth's edges are hard. Wherever two neighbours move differently (a hand, a weapon, a carried body or the player's legs over
the still world), (1) the background texels next to the moving thing carry its colour: they stay where they are and draw its old outline;
(2) the hole it leaves was filled with the first background texel at the hole's border, which is exactly that tinted rim: the old
silhouette filled with streaks of the thing's colour; (3) the thing's own edge texels carry the old background's colour along with it (a
halo); (4) a sky cell looked its direction up in the source, which showed the hand itself where it had been. Lab (`spacewarp_pass_check`
tests 12-13, a hand with soft edges moved 4 texels): old silhouette B 128, halo B 128, sky ghost B 255 (want 0, 255, 0).

**v1424.** Two compute passes before the splat: Classify stores each texel's class once (still, hand 0/1, the rest of the rig, its motion
texture, sky; the displacement follows from the class, so the per-texel rules no longer run 2-4 times per texel); Edge marks, only where
neighbours' displacements differ by more than half a texel, (a) every farther texel within 2 texels of a nearer differently-moving surface
as a rim: it is not drawn, its cells are filled from beyond it, and no filled cell or back-projection takes its colour; (b) a moving
surface's edge texel (one side only, not a thin thing seen from both): its colour is read 1.5 texels further in. A sky cell whose direction
the source shows covered by a moving thing or a rim shows its own sky texel. Nothing changes where nothing moves differently (lab: a still
scene with soft edges comes back exact). Lab: silhouette 0, halo gone (255), sky 0; all 22 earlier checks unchanged. Knob
`build/spacewarp-legacy-edges` (or the dev command `spacewarp edges off|on`) = v1423; status `spacewarp.soft_edges`.

**Uncovered areas.** The place a moving thing left was filled with one border texel's colour copied along each fill direction: shards and
streaks in the thing's old outline (sim crops). v1424 colours an uncovered cell with the background around it instead: in each of 8
directions the first drawn cell that is not nearer than 3/4 of the cell's depth and moves like its seed, blended by 1/distance (a Blend
compute pass once per uncovered cell at render size; per output pixel it cost 0.64 ms). Lab test 14: a striped wall hidden by the hand
comes back 27 levels off the truth (one seed's streak: 40).

**Measured.** Lab `spacewarp_pass_check`: all checks pass. GPU (RTX 5070, 1672x1800 render, 2496x2688 output, a large hand 0.6 m away
moving 5 cm): 0.83 ms per eye with the new rule, 0.67 ms without (+0.16 ms). Sim A/B on the same frames (`spacewarp capture` now also
saves `generated-alt`, the frame with the other rule; dev command `spacewarp always` holds half rate without touching the knob file):
20 triples walking/running in the user's save, 0.03-0.3 % of pixels differ; there the new rule is 24.0 levels from the next real frame,
the old 29.6; crops: the old rule's dark streak shards in uncovered areas are gone, a faint star pattern from the 8-direction blend
remains. Thin crossing crack lines on very close magnified surfaces appear with both rules (not this change). Not headset-tested.

**v1425: one surface, two motions; softer fills.** (1) Sim crops showed thin lines of the wall through a sleeve: an arm is one surface,
but the part within the palm's reach moved with the hand and the rest with the body, so the surface tore along the class boundary
whenever the two motions differed (lab test 16: 6 cells of wall through the arm). Edge now feathers the displacement within 4 texels of
such a boundary on one surface (depth within 5 %), only between the player's own parts (hands, the rest of the rig; a mover on a wall
or a carried body on the floor keeps its hard boundary): the arm stretches (lab: 0 cells). Feathered texels carry a flag and their
displacement in an RGBA32F texture; all others keep their class's. (2) The 8-direction blend left a star in each uncovered area: a
7-tap tent blur along x then y over uncovered cells filled from the same class of background softens it (lab 14 now 35 levels off the
hidden stripes: a blur trades their structure for smoothness; the streak was 40). Blend/blur are skipped with the rule off.
GPU per eye (bench): 1.07 ms on, 0.79 ms off. Sim same-frame A/B (15 triples): where the rules differ the new is 25.5 levels from the
next real frame, the old 28.2; crops: no shards or star seams; 392/392 submitted, 0 late, 0 failures. Not headset-tested.

## 19. Movers in true stereo (v1426, 2026-11-20 night)

NPCs, props and physics objects were not extrapolated in true stereo (no motion texture): at half rate they showed at the same place in a
real and a generated frame (45 Hz judder, a double image when the eyes follow them). Performance mode already had the motion texture
(`eye_motion_pass.hpp`: the engine's motion vectors less the camera's, world metres since the eye's previous render), built from
alternate-eye receipts only.

**Probe** (`paired_prev_probe` in the status, sim 2026-11-20): in paired stereo each eye keeps one view object and its previous
view-projection (+0x7590) is that eye's own last current one (+0x7510): 710 of 711 TAA dispatches per eye (the miss is the first). So the
engine's camera history is per eye in paired stereo and the motion pass's camera subtraction can use it directly.

**v1426.** At each eye's TAA dispatch in paired stereo, while spacewarp generated a frame within the last second
(`paired_pass_receipt`, `dxgi_probe.cpp`): a receipt with that view object's current and previous camera, phase = consecutive renders
(engine stamp +0x77b4); the motion pass runs on it without the alternate mode's other-eye test (`fsr_taa.hpp`, `Receipt::paired`). The
texture goes to the spacewarp frame (`game_xr.hpp`: `in.motion`, `world_to_source` = the source camera's world-to-camera rotation,
`motion_share` = (slot - display) / (display - the previous real display)). A mover's extrapolated step is capped at 3 cm
(`SpacewarpParams::mover_cap`): the first sim A/B without a cap carried a guard's raised sword arm on up while the next real frame swung it
down (14 % of the image changed, flicker 6.9 vs 3.9). With the cap, same-frame A/B (dev command `spacewarp altmovers`: the capture's
generated-alt is the frame without movers), 14 triples walking past guards: movers change 1-5 % of the image, there 40.1 vs 41.6 levels
from the next real frame (10 of 14 triples better), flicker about equal; without movers fast close NPC limbs left rectangular smear
blocks. Lab: test 4 (uncapped semantics) and the cap. Knob `build/no-spacewarp-movers` = v1425. Status: `spacewarp.mover_eyes`. Not
headset-tested; fast reversing NPC animation (attacks) still cannot be predicted.

## 20. Sharper generated frames, wider fills, no compile stall (v1427-v1429, 2026-11-20 night)

- **Sharp resample (v1429).** The generated frame looked its colour up bilinearly at fractional positions: fine detail was softer in every
  generated frame than in the real ones (a 45 Hz sharp/soft shimmer). Resolve now uses Catmull-Rom from 5 bilinear taps, clamped to the
  2x2 texels around the point (no ringing or halo at an edge). Lab test 17 (a 4-texel pattern, camera turned half a texel): contrast 151 of
  200 vs 123 bilinear. GPU +0.03 ms per eye. Knob `build/spacewarp-bilinear` = the old resample; capture A/B `spacewarp altsharp`.
  The sim's standing captures sit on whole texels, so they show no difference; the gain is for continuous head motion (headset).
- **Wider fills (v1429).** Three fill passes (144 texels) and a 96-cell blend search: a close hand moving fast (5 m/s at 0.4 m: ~125
  render texels a slot) left the middle of its hole unfilled, and an unfilled cell looks its direction up in the source (the hand at its
  old place). GPU per eye with everything on: 1.13 ms (bench: large hand, 1672x1800 render, 2496x2688 output; 0.85 ms rule off).
- **No compile stall (v1428).** Every session's first gameplay frame spent 11-15 s in D3DCompile on the main thread (StaleEyeWarp's
  shaders, which the canted-to-straight resample needs every frame; the spacewarp pass ~1 s more when it first engaged). Compiled
  bytecode is cached under the mod folder (`shader-cache/`, `src/shader_cache.hpp`, keyed by source, entry, profile and flags) and a
  background thread fills the cache during the menus. Sim: first gameplay Present 0.3 ms (was 15.2 s), no Present over 0.5 s.
- Spacewarp sanity (v1429 sim, Always): 412/412 generated frames submitted, 0 late, 0 failures, movers on 818 eyes.
- Engine-side experiments of the night: `docs/ENGINE-DIG-NOTES.md` section 7.
