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

- **Steam is waiting on a dialog.** At 03:01 a launch stopped at "SynchronizingCloud: syncfailed" (Steam's own log; its stats request
  failed at the same moment, so most likely a short network drop). Steam queues every later launch behind that prompt. Steam's
  cloud check at 03:16 lists every Dishonored 2 save as in sync, and the saves on disk match the last verified backup byte for
  byte. Choose Retry Sync in that prompt.
- **v1397 is installed** in `build/` (the dinput8 proxy loads it): v1396 plus the `canted_fov` memo (lab-tested; v1397 itself could
  not be sim-run because of the Steam prompt; v1396 ran ~10 sims). v1393 is kept in `build/night/installed/`, v1394-v1396 in
  `build/night/`. Switch: `sh build/night/use.sh 1396` or `sh build/night/use.sh 1393` (Git Bash); the proxy needs exactly one
  `d2vr_render_probe_arm_v*.dll` in `build/`. `artifacts/repair-v1397-test-candidate.json` names v1397 for the developer-mode
  launcher (`launcher/dist`); restart the launcher (or Re-check) so it picks it up. With v1393 swapped back, the launcher falls back to
  the v1393 manifest by itself (the v1397 manifest's DLL is then missing).
- Suggested settings: Anti-aliasing **DLSS Quality**; Save diagnostic logs **off**; Virtual Desktop **90 Hz** (try 80 Hz if it
  still drops); mod Resolution 1.0 with VD High or Ultra.
- Opt-in knobs (empty files in `build/`): `auto-refresh-rate` (picks 72/80/90 Hz itself), `dlss-preset` containing `K` (DLSS's best
  image, +~1.5 ms GPU), `dlss-fovea` (old foveated DLSS), `static-shadow-cache` (+ `static-shadow-cache-probe`, observe only).
- Things to look at: DLSS image (preset E ghosting vs K), Steam Link corners, the 2 s pacer hold, any new crash.

## 7. Not done / next

- Headset verification of all of the above.
- The governor (dynamic resolution) needs a GPU-time signal first: on this PC most missed frames are CPU-bound, and a miss-driven
  governor would lower resolution for nothing.
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
