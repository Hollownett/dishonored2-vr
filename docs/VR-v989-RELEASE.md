# VR v989 (test candidate, 2026-11-04)

Paired mode. This build is about the things that were actually measurable today: the mod's own per-frame CPU, a canting
startup defect, and the frame pacer. No engine-side trick is claimed here, because the measurements did not support one.

## 1. Canting: startup fixed, quality kept (v986)

Two XR inits 4 s apart at startup (`frame=1` at 09:50:50 and 09:50:55) were the reason VR "started twice". Canting forces the
optical-density eye size, and that size was requested as soon as views existed - on the title screen, while SteamVR was still
bringing the session up. SteamVR rebuilt the eye swapchains under the live session, the state dropped VISIBLE -> READY ->
VISIBLE, and because `optical_density_applied` stayed set the second session kept SteamVR's smaller recommended size: the canted
density was lost as well.

- v986 requests that one resize on the first gameplay frame (`gameplay && FOCUSED`) instead.
- Same build, `build/no-cant` installed: exactly one init. With canting on: two. After v986: one, session FOCUSED, canted views on.
- Canting itself is unchanged; `build/no-cant` still returns to the enclosing frustum.

## 2. Mod CPU: 2.60 -> ~0.7 ms per frame

| change | measured |
|---|---|
| `GpuDispatchBindings` re-verification window | `hud_validate` 1.51 -> ~0.1 ms/frame; `bind()` 32.8 us full path vs 0.01 us unchanged (real reader, loader-mediated check), ~46 HUD-child calls per frame |
| FSR eye GPU timer made opt-in (`build/fsr-gpu-timing`) | hook 5.4 -> 1.5-2.9 ms, mod CPU 3.42 -> 1.55 ms (it had switched itself on when the profiler knobs were turned off) |
| per-bind trim in `observe_paired_target_change` (v989) | one `OMGetRenderTargets` + COM `GetResource` removed from every render-target bind; they now run only while the left-eye copy can still happen, once per frame |
| authored-camera CSV and draw-trace counters | honour `no-diagnostics` instead of running anyway |
| profiler knobs turned off (`render-perf`, `native-gpu-timing`, `alt-pose-trace`) | ~1 MB/s of CSV writes and ~78 GPU query spans/frame gone from play |

## 3. Frame pacer (v984)

The pacer counted missed slots and retried the full rate only every 10 s, doubling to 2 min while the retries failed, so one heavy
stretch held the app at half rate long after the scene recovered (2026-11-03: 22.2 ms holds of 20-60 s). v984 picks the cadence
from the median work of the last 32 frames: half rate only above 1.4 periods, back under 1.1, and menu/loading frames no longer
count as scene work. Recovery measured at 0.40 s in the lab; a scene whose work fits the period never locks. Status:
`pacer {k, locks, unlocks, paced_frames, frames, work_ms_p50, base_ms}`.

## 4. Engine CPU knobs (mechanism only, no defaults)

`build/engine-cvars` takes `name value` lines and v987+ applies them once, on the first gameplay frame, through the same static
idCVar write the `cvar` test command uses. Runtime only, so a bad line cannot persist. Comments are skipped since v988.

Measured and **not** shipped: `r_skipShadowModelSort` (neutral), `r_maxDynamicDecals 100` (neutral/negative),
`r_useGPUCulling 1` (its apparent win was a change of test spot, not the cvar). The shadow buy-out was measured directly with
`noShadow 1`, `r_shadowQualityPreset 0`, `r_shadowCullDistance 15`, `r_shadowParallelSliceCnt 2` all applied: engine two-view cost
14.35 -> 14.03 ms, i.e. unchanged. The menu settings' lack of effect is real and now measured.

## 5. What the frame actually is

At the heavy spot: engine ~14-20 ms for the two views, Present hook 1-4 ms, mod CPU ~0.7 ms. The budget at 90 Hz is 11.1 ms, at
72 Hz 13.9 ms. The engine cost is per-view draw submission and does not move with shadows, decals, culling, resolution or
quality. The DOOM Eternal VR project (EternalVR, id Tech 7) has the same architecture and the same profile: stereo 2.5x mono CPU,
command recording the serial critical path, shared culling between eyes measured slower (152.8 -> 143.1 ticks/s), and 72 Hz their
target at Quest-3 size. Treat 72 Hz as the realistic target and 90 Hz as a light-scene ceiling.

## Checks

- DLL builds (MSVC 2022, /W4): 14,094,336 bytes.
- `frame_pacer_check` and `gpu_dispatch_bindings_check` pass, including the new recovery, menu, revalidation and retained-byte
  cases.
- In the headset: canting on with a single init, 72 Hz confirmed (`period 13.8889 ms`), mod CPU 0.58-1.01 ms, hook 1-1.9 ms in
  normal scenes.
- Not done: engine-side sharing of the per-view work (command buffers, casters, per-view storage). This build deliberately makes
  no claim there.
