# Overnight performance work (2026-11-19)

Branch `docs/perf-plan-90hz` (local, not pushed). Plan: `docs/PERF-PLAN-90HZ-2026-11-19.md`. Every number below is from the
simulated-headset rig on this PC (RTX 5070, Ryzen 5 5600X 6C/12T, the user's last save = the heavy area of the 2026-11-19 Virtual
Desktop session), unless it says headset. The sim has no compositor and no Virtual Desktop encoder, so a headset frame costs a
little more. **Nothing here is headset-verified.**

## 1. Why the Virtual Desktop session ran at 45 fps

1. **DLSS never ran on the RTX 5070.** The foveated DLSS feature (v1108, on by default) writes its box into a sub-rectangle of the
   eye output, and NGX only allows that when the feature is created with `InEnableOutputSubrects`. The mod never set it, so the
   first evaluation failed with `0xBAD00005` (InvalidParameter) in every session, on Virtual Desktop and on Steam Link.
2. **The fallback for "DLSS Quality" was FSR 3 at 100 %.** After the failure the game rendered the full 2621x2822 per eye (VD at
   1.05x): 2.2x the pixels DLSS Quality renders.
3. Under that load the GPU needed ~14 ms per frame, the frame pacer locked to half rate (45 fps) and flipped 37 times in 6 minutes.

Sim of exactly that setup (2621x2822, "DLSS Quality" selected):

| | v1393 (installed before) | v1396 (tonight) |
|---|---|---|
| work per frame p50 / p90 | 15.06 / 15.82 ms | **9.96 / 11.87 ms** |
| engine GPU (native lists) | 13.9 ms | 7.0 ms |
| DLSS | failed, FSR 3 native | runs, 0.87 ms per eye |

90 Hz needs 11.1 ms per frame, 80 Hz 12.5 ms, 72 Hz 13.9 ms.

## 2. What limits the frame after that: the CPU

- Lowering resolution does not help any more: 0.5x and 1.0x land on the same ~9.5 ms (sim). Neither does performance mode
  (one eye per frame): 10.37 vs 10.42 ms, because one view still costs the main thread ~8.3 ms.
- The game's main/render thread is ~80 % busy and is the critical path; the five engine worker threads are ~50 % busy and wait on
  each other. No thread is saturated: the frame is a chain of dependent work.
- On that main thread: game code ~45 %, waits ~10-20 % (jobs and the D3D11 driver), D3D11/driver ~10 %, **the mod ~20 % before
  tonight** (about 2 ms per frame), mostly memory-validity checks (`VirtualQuery`, 13 % of the thread).
- A wait inside `VSSetShader` on every recording thread (8-10 %) is the engine/driver, not the mod: with the mod's hook removed
  (`build/no-object-motion`) the wait moves to engine frames and the frame is only 0.26 ms faster.

## 3. What changed (v1394-v1396)

| # | Change | Files | Measured |
|---|---|---|---|
| 1.1 | DLSS works on RTX: the foveated feature is created with output subrects; a failing box evaluation retries once as whole-eye DLSS before giving up | `dlss_eye_upscaler.hpp`, `fsr_taa.hpp`, `dlss_mode.hpp`, RTX lab 18 cases | 0 evaluate failures in every sim |
| 1.2 | A failed/unavailable "DLSS Quality" falls back to FSR 3 at the DLSS fraction (67 %), not 100 %; the VR menu row says so | `dlss_mode.hpp`, `vr_settings.hpp` | sim without a DLSS file: FSR 3 at 0.667 |
| 1.3 | No FSR 4 creation attempts on non-AMD GPUs (each failed attempt stalled an upscaler rebuild 360-430 ms) | `fsr4_create_gate.hpp`, `fsr_taa.hpp` | sim: 0 failures, 2 skips |
| 1.5 | Launcher: "Save diagnostic logs" help says it costs frame time (launcher not rebuilt tonight) | `launcher/` | - |
| 1.4 | Steam Link: built-in corner mask when the runtime returns no hidden-area mesh (only ~0.9 % of the image: a mask that cannot touch visible pixels; the next VD session logs the real Quest 3 mesh for a better one) | `visibility_mask.hpp`, `visibility_stamp.hpp`, `game_xr.hpp` | lab only |
| 1.6 | Pacer holds each 90/45 switch at least 2 s; opt-in automatic refresh rate (72/80/90 Hz from the measured work) via `XR_FB_display_refresh_rate` | `frame_pacer.hpp`, `refresh_rate_policy.hpp`, `game_xr.hpp` | labs only; refresh rate is opt-in |
| CPU | Memory-validity cache keeps big engine regions: VirtualQuery 11.4 % -> ~2 % of the main thread | `native_readable_range.hpp` | ~0.4 ms p50, ~0.65 ms p90 (section 4) |
| CPU | `canted_fov` memoised (was recomputed several times per eye per frame, 1.4 % of the main thread) | `canted_fov.hpp` | lab (v1397) |
| GPU | DLSS Quality defaults to render preset E (CNN) and whole-eye DLSS on NVIDIA; DLAA keeps NGX's default (K) | `dlss_eye_upscaler.hpp`, `fsr_taa.hpp` | 0.66 ms per eye vs 1.44 (K) and 0.99 (FSR 3 Quality) |
| 4.2 | First start picks DLSS Quality on RTX (was DLAA); launcher preselection and help text match | `aa_first_start.hpp`, `launcher/Prefs.cs`, `launcher/Program.cs` | labs (v1398) |
| 4.1 | Opt-in governor steps down only when GPU-bound (pre-XR stall), never on CPU-bound misses | `vr_perf_governor.hpp`, `dxgi_probe.cpp` | lab (v1398) |
| 2A.1 | Static shadow cache: design + observe-only prototype behind `build/static-shadow-cache` (counts only, skips nothing) | `docs/STATIC-SHADOW-CACHE.md`, `static_shadow_cache*.hpp` | needs live checks C1-C8 |

Upscaler GPU cost per eye at 2246x2419 output (sim, RTX 5070, NGX 310.9.1):

| mode | ms per eye |
|---|---|
| DLSS Quality, preset K (NGX default), whole eye or foveated | 1.44 |
| DLSS Quality, preset E, foveated (FSR 3 whole eye + DLSS box) | 1.13 |
| FSR 3 Quality | 0.99 |
| **DLSS Quality, preset E, whole eye (new default)** | **0.66** |

## 4. Measured on a quiet machine

Only runs taken after the helper agents had finished compiling (from ~02:05) are compared here. Work per frame p50 / p90:

| setup | v1393 | tonight |
|---|---|---|
| 0.5x, FSR 3 Quality (CPU-bound) | 9.93 / 11.37 | 9.51 / 10.71 (v1394: memory-check cache) |
| 0.5x, FSR 3 Quality, mod's motion hook off (test only) | - | 9.25 / 10.45 (v1395) |
| 2246x2419, FSR 3 Quality | (10.42 / 12.24, noisy run) | 9.49 / 10.67 (v1394) |
| 2246x2419, DLSS Quality | DLSS failed | 9.65 / 10.95 (v1396, preset E) |
| **2621x2822 (VD 1.05x), "DLSS Quality" selected** | **15.06 / 15.82** | **9.96 / 11.87** (v1396) |

So the memory-check cache is worth ~0.4 ms at p50 and ~0.65 ms at p90 on the CPU-bound frame (an earlier, noisier pair suggested
~1.9 ms; that pair overlapped the agents' compiles). The big step is the DLSS fix for the setup the user actually played.

The first runs of the night (R1, R2, R3) overlapped helper-agent compiles and are ~0.5-1.5 ms pessimistic. A planned interleaved
A/B (two rounds of v1393/v1396 at 0.5x and 1.0x) did not run: from 03:01 Steam stopped launching the game (section 6).

## 5. Other findings

- `UPDATE_NPC_DECALS` (0.5-1.6 ms, first eye only) varies with what the NPCs do; `r_skinDecalRes 256` did not change it.
- Shadows were only 0.3 ms at this spot (they were 49 % of the GPU at the 2026-11-04 heavy spot before sharing); static shadow
  caching matters where many lights cast.
- The mod's body/hand features cost ~0.1 ms (R3).
- The game itself creates thousands of vertex shaders while you move; the driver compiles them in the background.
- Diagnostics on (the user's current setting) writes 21 MB eye-image dumps and CSVs during play; keep it off when measuring
  frame rate (it is a launcher Feature).

## 6. Testing in the headset

- **Steam.** At 03:01 a launch stopped at "SynchronizingCloud: syncfailed" (a short network drop; Steam reconnected at 03:16 and lists
  every Dishonored 2 save as in sync). That prompt held every later launch, so I restarted the Steam client; it then stopped at its
  account picker. Steam's own auto-login account is the save owner (the account signed in all night), so at 04:08 I restarted it with
  `-login <that account>` (its remembered token, no password) and it signed in; nothing else was changed. The saves and
  `dishonored2Config.cfg` match the last verified backup byte for byte after every run.
- **v1403 is installed** in `build/` (v1401 plus dev commands and the opt-in NVIDIA VRS of v1403), with
  `artifacts/repair-v1403-test-candidate.json`; sim at 2621x2822 DLSS Quality 10.11 / 11.89 ms, 0 exceptions. v1401: v1401 sim: stereo 2621x2822 DLSS Quality 9.96 ms interval, performance-mode
  rebuild 1.82 ms GPU (v1399 1.92), 0 exceptions. v1399: (the dinput8 proxy loads it), with `artifacts/repair-v1399-test-candidate.json` for the
  developer-mode launcher (`launcher/dist`; restart it or press Re-check). Sim at your VD setup (2621x2822, DLSS Quality): work p50
  10.43 / p90 11.84 ms, 0 DLSS failures, 0 exceptions (v1398: 10.14 / 11.61, v1393: 15.06 / 15.82). Earlier builds: v1393 in
  `build/night/installed/`, v1394-v1398 in `build/night/`; switch with `sh build/night/use.sh <version>` (Git Bash); the proxy needs
  exactly one `d2vr_render_probe_arm_v*.dll` in `build/`, and the launcher picks the highest manifest whose DLL exists.
- Suggested settings: Anti-aliasing **DLSS Quality**; Save diagnostic logs **off**; Virtual Desktop **90 Hz** (try 80 Hz if it
  still drops); mod Resolution 1.0 with VD High or Ultra.
- Opt-in knobs (files in `build/`): `auto-refresh-rate` (picks 72/80/90 Hz itself), `dlss-preset` containing `K` (DLSS's best
  image, +~1.5 ms GPU), `dlss-fovea` (old foveated DLSS), `governor` (dynamic resolution, FSR modes, GPU-bound only),
  `render-pixel-cap` containing megapixels per eye (e.g. `5.0`: 2621x2822 -> 2155x2320, engine GPU 7.46 -> 6.59 ms in the sim),
  `pass-cpu-timing` (per-pass CPU table in the status), `nvidia-vrs` (NVIDIA foveated shading, v1403; optional text
  `0.35 0.55 [4x4]`), `afw2-fill-bound` (performance mode), `static-shadow-cache` (+ `-probe`, observe only).
- The status file now says why stereo misses its budget: `perf_governor.performance_mode_advice` = `gpu_bound` (performance mode,
  DLSS Quality or a lower resolution help) or `cpu_bound` (they do not; a lower refresh rate does); the log gets a line on each change.
- Things to look at: DLSS image (preset E ghosting vs K), Steam Link corners, the 2 s pacer hold, any new crash.

## 7. Not done / next

- Headset verification of all of the above.
- The governor (dynamic resolution, plan 4.1): v1398 gates its down steps on a GPU-bound signal (pre-XR stall > 1 ms; CPU-bound
  misses are counted in `perf_governor.cpu_bound_holds`). It stays opt-in (`build/governor`) and FSR-only until a headset run: DLSS
  needs its feature created at the maximum size with a per-frame render subrect, else every step rebuilds the feature.
- The CPU floor (~9.5 ms on the 5600X) is now mostly the game's own main thread: game code ~45 %, waits on its own jobs and the
  driver. **The second eye is not where the CPU goes** (`docs/ENGINE-CPU-SECOND-VIEW-RESEARCH.md`): one engine view vs two changed
  engine time by ~0.24 ms, executing eye 2's lists costs ~0.08 ms on NVIDIA, and per-draw matrices are written inside the recorded
  lists, so 2B.2 (replay eye 1's lists) is a no-go and 2B.1 is a no-go for the culling/gather/tile passes. The mod's own Present
  hook (~1.0 ms on that thread) is now a bigger CPU target than the second view. Levers left: the hook, the engine's job count
  (`jobs_numThreads`, `r_schedulerThreadingMode` exist; untested because Steam stopped launching), and a per-pass CPU timer to find
  the game's own hot passes (live check L2 in that doc).
- NVIDIA VRS (plan 2A.5) needs the NVAPI SDK (a download: ask first).
- Static shadow cache: run the live checks C1-C8 in `docs/STATIC-SHADOW-CACHE.md` before enabling anything.
- Performance mode (`docs/PERFORMANCE-MODE-AFW-COMPARISON.md`): its rebuild costs 2.04 ms GPU at 2246x2419, ~2.8x Dying Light
  VR's per output pixel, so the single view nets only ~0.34 ms GPU here. It pays only on GPU-bound PCs, and only after the warp is
  cut to ~1 ms. Top changes: per-stage GPU timing and cheaper warp stages; exact motion for held items (the root of the v1320-v1327
  crossbow doubling); an edge ring for hand/weapon outlines; a general stale-surface test; the lens-mask stamp in that mode (it
  never runs there today). The automatic switch (plan 3.1) should key on GPU load, not missed frames.

## 8. Plan status

Measured after Steam was back (04:08-04:55), all in the sim on this PC:

- **Engine CPU per pass** (v1399 `pass-cpu-timing`, ms per frame, main thread + workers): non-render game jobs 2.4, **shadows ~3.7**
  (SHADOWS_DYNAMIC 1.9, SHADOWS_WORLD 1.0, casters/prepare ~0.8; sun cascades ~2.0 of it), GATHER_DYN_MODELS 1.0, DEPTHPASS 0.7,
  FORWARD_WORLD 0.7, Iggy UI 0.55, GATHER_UMBRA 0.5, FORWARD_DYNAMIC 0.5, SORT 0.4. Job items total ~2.0 ms on the main thread and
  ~10 ms across the five workers (each ~50 % busy); the main thread's critical path is the engine's own serial work.
- `jobs_numThreads` 6/10/11 in the config: the busy thread count stays 7 and seven runs show no effect; v1402's `cvarget` shows why:
  it is not a cvar of this build ("cvar not found").
- Shadow cvars set LIVE through the mod's command channel (v1402 `cvar`/`cvarf`, within-run A/B against a control run that drifts
  -0.22 ms p50 / -0.73 ms p90 by itself between the first and the second half): `r_skipShadowModelSort 1` -0.11 / -0.38,
  `r_shadowSpotAndOmniPartialUpdateDynaSqrDistanceThreshold 144 -> 16` (time-slice local dynamic shadows from 4 m instead of 12 m)
  -0.11 / -0.24, `r_shadowCullDistance 25 -> 12` -0.22 / -0.46: all inside the control's drift, no effect. Defaults read live:
  cull distance 25, sun cascades 3, the engine's own shadow cache 12 slices, partial-update distance 12 m.
- `r_shadowParallelSliceCnt 2` and `r_shadowCullDistance 5`: shadow CPU 3.8 ms vs 3.6-3.9 baseline, frame time within the baseline
  spread (9.54-10.05 ms): no lever.
- Static shadow cache live checks (observe-only prototype): dynamic-shadow lists execute BEFORE the world lists in every frame (the
  capture point of the design does not exist), the key misses 984 world-draw changes, and only 27 % of frames would hit. No-go as
  designed; world-atlas recording is ~0.3 ms CPU per frame anyway.

| Phase | Status |
|---|---|
| 0 Measurement | Done (sim): runs R0-R6, quiet-machine A/Bs, thread and per-pass CPU profiles |
| 1.1-1.6 | Done: DLSS fix, fallback scale, no FSR 4 on NVIDIA, diagnostics warning, Steam Link mask, pacer hold + opt-in refresh rate |
| 2A.1 static shadow cache | Prototype + live checks: no-go as designed (above) |
| 2A.2 shadow cvars | Tested: no CPU or frame-time effect |
| 2A.3 more shared GPU passes | Not worth it here: the second view costs ~0.24 ms (CPU) and the GPU is not the limit after the DLSS fix |
| 2A.5 NVIDIA VRS | Done, opt-in `nvidia-vrs` (v1403): NVAPI SDK fetched from github.com/NVIDIA/nvapi (MIT) into `third_party/nvapi` (not committed; without it the feature compiles to a stub). Scene passes only, full rate within 35 % of the render height of each eye's optical centre, 2x2 beyond. Sim: FORWARD_WORLD 1.76 -> 1.55 ms GPU, 22816 applications, 0 failures; eye image centre unchanged, periphery slightly coarser. |
| 2A.4 stencil mask | Not done: ~9 % of FOG/SCREENSHADOWS/POSTPROC (~0.1 ms here) for forcing stencil state into engine passes; GPU-side and small |
| 2B.1 / 2B.2 second-view CPU | Researched: no-go |
| 2B.3 mod hooks on the main thread | Done: memory-check cache, `canted_fov` memo (~2 ms -> ~1 ms of mod work on that thread) |
| 2B.4 XR submit off the render thread | Not done: the hook is ~0.85 ms and the runtimes call into the app's D3D11 device in xrEndFrame; risky without the headset |
| 2C game thread / job count | Measured per pass; `jobs_numThreads` is not a cvar of this build; shadow cvars set live: no effect; no further settings lever |
| 3.1 automatic performance mode | Advisor done (v1399): the session reports `gpu_bound` / `cpu_bound`; the mode is chosen at session start |
| 3.2 performance-mode cost | 8 warp dumps captured in the sim (05:00); per-stage GPU timing on the RTX 5070 (replay `STALE_REPLAY_HW`): decide 0.38, resolve 0.29-0.34, splat 0.27, crack+copies 0.27, motion 0.19, scatter 0.16, repair 0.09-0.16, fill 0.07-0.11 ms; total ~1.9 ms. Fill bound (c): -0.02 ms, opt-in `afw2-fill-bound` (v1400). Motion tiles (b): Repair, Origins and ScatterWrite only on the 8x8 tiles with motion; byte-identical on all 8 dumps, -0.11 ms in the replay and 1.91 -> 1.82 ms in the game (v1401, kill switch `no-afw2-motion-tiles`). Next: split Decide (0.38 ms) and Repair's vote loop. Canted views (d) are not in the dumps. |
| 4.1 governor | GPU-bound gate done (v1398), opt-in |
| 4.2 defaults | Done: DLSS Quality on RTX (v1398) |
| 4.3 render-size cap | Done, opt-in `render-pixel-cap` (v1399), sim-verified |
| 4.4 depth layer | No-go for your runtimes: Virtual Desktop does not stream app depth (its SSW extrapolates from frames and poses on the headset) and SteamVR Motion Smoothing uses motion vectors, not depth. Meta's own runtime would use it. |

What remains open needs the headset: DLSS preset E's image, the Steam Link corners, the pacer hold, `auto-refresh-rate`, a sensible
default for `render-pixel-cap`, and whether performance mode's rebuild can be cut to ~1 ms (needs fresh warp dumps from a
performance-mode session).

Sources for 4.4: [Flight Simulator forum quoting Virtual Desktop's OpenXR developer](https://forums.flightsimulator.com/t/quest-3-new-feature-positional-time-warp/750770),
[UploadVR on Virtual Desktop SSW](https://uploadvr.com/virtual-desktop-synchronous-spacewarp/),
[Unity forum on SteamVR Motion Smoothing](https://discussions.unity.com/t/blurry-swimming-image-in-pcvr-quest-3-via-link-built-in-rp-textures-look-fine-nearby-but-look-blurry-and-float-at-5-6m/1682459/7).
