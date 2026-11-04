# VR v984 (test candidate, 2026-11-04)

Why lowering resolution and graphics settings stopped helping, and the two fixes that came out of it. No headset run yet: the
DLL builds and the two lab checks below pass.

## 1. The measurement: the frame is engine CPU, not pixels

2026-11-03 headset logs (`%LOCALAPPDATA%\DishonoredVR\artifacts`, frames 28,469-35,573 and the last session's live status):

| | fast frames (11.1 ms bucket) | slow frames (22.2 ms bucket) |
|---|---|---|
| engine CPU between Presents, p50 | **10.49 ms** | **15.13 ms** |
| mod Present hook, p50 | 0.32 ms | 6.71 ms, of which 6.26 ms is `xrWaitFrame` |
| GPU, native list sum, p50 | 5.74 ms | 10.70 ms |

- The budget at 90 Hz is 11.111 ms. A fast frame already spends 10.49 ms of it inside the engine, with the GPU only 5.7 ms
  busy. That is why resolution, AA and graphics settings do not raise fps any more: they move the GPU column, and the GPU is
  not the limit.
- The 45 fps spots are 23 runs of 20-69 consecutive frames at 22.25 ms (44.9-45.2 fps), all inside one 47 s stretch.
- The second eye costs more than the first: `frame_split` eye0 3.45 ms, eye1 6.50 ms.
- Draw submission is cheap in absolute terms: `object-motion.json` self-times the whole per-draw bookkeeping at 84.6 us per
  Present over 266 draws per Present, so per-draw work was not the lever.

## 2. Fix: the HUD validation no longer re-verifies every hook per HUD child

The mod's own CPU was 2.60 ms/frame, and **1.511 ms of it was `hud_validate`** (58% of the mod's CPU, 13.6% of the 90 Hz
budget, near-constant p50 1.515 / p90 1.749).

`begin_native_hud_art` (`src/dxgi_probe.cpp`) runs once per native HUD child draw, dozens of times per frame, and each call went
through `GpuDispatchBindings::bind()`. `bind()` re-verified every retained hook with one `ReadProcessMemory` each (up to 7 methods
x 4 slots = 28 reads) plus a code-page check per target. The same seven context methods cannot change between two HUD children of
one frame.

- v984: an unchanged, already-enabled target set is re-verified at most every 250 ms, and up to four target sets are remembered
  (the immediate context and the native HUD's deferred context stay enabled together, so two sets alternate). Any change still
  runs the full check immediately, `disable_all` still drains and restores, and the retained-byte check still catches a hook that a
  third party overwrote (the lab check proves it after the 250 ms window).
- Measured with the real reader and a loader-mediated target check (`build/bind_bench.cpp`): **32.8 us per bind on the full path,
  0.01 us on the unchanged path**. At the ~46 HUD child draws per frame that 1.51 ms/frame implies, the whole fixed cost goes away.
- Status: `gpu_dispatch_fast_binds`, `gpu_dispatch_verifications`. Expect verifications in the low hundreds and fast binds in the
  tens of thousands; if `hud_validate` still shows ~1.5 ms in `render-perf-cpu.csv`, this did not take effect.

## 3. Fix: the pacer stops holding 45 fps after a heavy stretch

The frame pacer counted missed slots and retried the full rate only every 10 s, doubling to 2 minutes while the retries failed. One
heavy stretch therefore held the app at 22.2 ms for a minute or more after the scene had recovered - the user's "fps drops to 45
and stays". The 2026-11-03 run measured those holds at 20-60 s.

- v984 decides the cadence from the frame's own work: the median work of the last 32 frames (present-to-present minus
  `xrWaitFrame` and the pacing sleep). Half rate is entered only above 1.4 periods - below ~2/3 of the refresh rate, where such
  frames miss every slot anyway - and left as soon as it falls under 1.1 periods.
- Recovery is ~0.4 s (lab: `build/frame_pacer_check`, "18 -> 10.5 ms: k=1 after 0.40 s"; v844-v983 held it for 10-120 s).
- A scene whose work fits the period never locks (lab: steady 10.5 ms, `paced 0.00`, `locks 0`).
- `build/pacer-miss-lock` keeps the old miss-rate lock for anyone who wants it.
- Status: `pacer {k, locks, unlocks, paced_frames, frames, work_ms_p50, base_ms}`.

## 4. Measurement knobs turned off for play

These 0-byte knobs in `%LOCALAPPDATA%\DishonoredVR\build` were left on from earlier sessions and cost frame time and disk:

- `render-perf` - wrote `render-perf-gpu.csv` 104 MB + `render-perf-cpu.csv` 5 MB per session (362 KB/s, ~5.4 KB/frame).
- `native-gpu-timing` - GPU timestamp queries around every native command list (~78 spans/frame).
- `alt-pose-trace` - 12 MB CSV per session (42 KB/s).
- `legacy-pacing` - the old free-running cadence, now unnecessary.

They were moved to `%LOCALAPPDATA%\DishonoredVR\knobs-off-20261104\` (move them back to re-enable). `no-diagnostics` stays: the
user's launcher feature "Record frame timing and controller diagnostics" is off.

## 5. What the numbers say to do about the heavy spots

The engine's ~10.5 ms per-frame CPU at 90 Hz (11.111 ms) leaves almost no margin, and a heavy scene raises it to 15+ ms. Nothing
inside the mod can halve that; only fewer engine views or a lower refresh can. In order of expected effect:

1. **72 Hz** (period 13.89 ms): fast-scene work 10.5 ms fits with 3.4 ms spare, so the pacer should never lock. 80 Hz (12.5 ms)
   is a middle step. This is a SteamVR refresh setting, not a mod setting.
2. **Performance mode** (launcher: "performance mode: one eye per frame"): one engine view per frame halves the engine CPU. It
   still needs a headset verdict on motion doubling.
3. Paired at 90 Hz stays possible in light scenes, and v984 no longer pins the app to 45 when a heavy stretch ends.

## Checks

- `build/frame_pacer_check` passes, including the new recovery and steady-10.5 ms cases.
- `build/gpu_dispatch_bindings_check` passes, including the 250 ms revalidation window and the retained-byte detection after it.
- The DLL builds (MSVC 2022, 14,089,216 bytes, `build/compile-v984.cmd`).
- Not run: the game and the headset. The expected result is `hud_validate` gone from the profile and no 22.2 ms holds longer
  than ~0.5 s after a heavy spot ends.
