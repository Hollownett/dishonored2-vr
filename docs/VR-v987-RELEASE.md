# VR v987 (test candidate, 2026-11-04)

Paired mode only. Canting fixed and kept on, plus an engine-cvar preset mechanism for the CPU knobs the profile points at.

## 1. Canting: startup fixed, quality kept (v986)

Two XR inits 4 s apart at startup (`frame=1` at 09:50:50 and 09:50:55) were the reason VR "started twice". Canting forces the
optical-density eye size, and that size was requested as soon as views existed - on the title screen, while SteamVR was still
bringing the session up. SteamVR then rebuilt the eye swapchains under the live session, the state dropped VISIBLE -> READY ->
VISIBLE, and because `optical_density_applied` stayed set, the second session kept SteamVR's smaller recommended size: the canted
density was lost as well.

- v986 requests that one resize on the first gameplay frame (`gameplay && FOCUSED`) instead.
- Planar comparison: the same build with `build/no-cant` had exactly one init; with canting on it had two.
- Canting itself is unchanged; `build/no-cant` still returns to the enclosing frustum.

## 2. Engine CPU knobs (v987)

`Dishonored2.exe` exposes the cvars the 2026-11-03 profile points at (shadow passes were 32% of a core):

`r_skipShadowModelSort`, `r_shadowCullDistance`, `r_shadowParallelSliceCnt`, `r_shadowAtlasSliceCnt`,
`r_maxDynamicDecals`, `r_useGPUCulling`, `r_shadowQualityPreset`, `r_skipDecalStencil`, `r_skipDynamicDecals`.

- `build/engine-cvars` holds `name value` lines. v987 applies them once, on the first gameplay frame, through the same static
  idCVar write the `cvar` test command uses (`find_cvar` + `WriteProcessMemory` at +0x28/+0x2c).
- Runtime only: a new game start begins from the engine's own defaults, so a bad line cannot persist. Status: `engine_cvars`.
- No defaults are shipped yet; the values are chosen from a live A/B (`build/cvar-sweep.ps1`), because the engine's own defaults
  are not readable through the mod and a value in the wrong direction is worse than none.

## 3. The A/B that picks the values

`build/cvar-sweep.ps1` waits for a live VR gameplay session (status file fresh within 3 s, session FOCUSED, gameplay), samples a
30 s baseline, then applies one cvar at a time through `artifacts/vr-command.txt`, 30 s each, cumulative. It writes
`build/cvar-sweep.csv` with `pace50`, `pace95`, `missed`, `eye0`, `eye1`, `hook`, `mod_cpu`, `pacer locks` per step, so each cvar's
marginal effect is the difference from the step before it. The first run at 10:27 sampled a stale status file from the crashed
10:03 session (headset not connected); the script now refuses a status file older than 3 s.

## 4. Mod-side CPU already removed (v984-v985, in this build)

| change | measured effect |
|---|---|
| `GpuDispatchBindings` re-verification window (native HUD validation called it per HUD child draw) | `hud_validate` 1.51 ms/frame -> ~0.1 ms (bind() 32.8 us full path vs 0.01 us unchanged, ~46 calls/frame) |
| FSR eye GPU timer made opt-in (`build/fsr-gpu-timing`) | hook 5.4 -> ~1.5-2.9 ms, mod CPU 3.42 -> 1.55 ms |
| `FramePacer` decides the cadence from median frame work, not a miss score with a 10-120 s probe backoff | a 45 fps hold ends in ~0.4 s instead of 20-60 s; menus no longer count as scene work |
| diagnostics knobs (`render-perf`, `native-gpu-timing`, `alt-pose-trace`, `legacy-pacing`) turned off | ~1 MB/s of CSV I/O and ~78 GPU query spans/frame removed from play |

## 5. What the numbers still say

Engine per-view CPU is the limit: ~10.5 ms for two views against an 11.1 ms budget at 90 Hz, GPU ~55% busy, resolution
independent. The DOOM Eternal VR work (EternalVR, id Tech 7) is the same architecture and reports the same profile: stereo is 2.5x
mono CPU, command recording is the serial critical path, sharing culling between eyes measured *slower* (152.8 -> 143.1 ticks/s),
and their Quest-3-size target on a 4080 is 72 Hz.

## Checks

- DLL builds (MSVC 2022, 14,093,824 bytes). `frame_pacer_check` and `gpu_dispatch_bindings_check` pass, including the new
  recovery, menu and revalidation cases.
- Not run: the headset session that picks the cvar values.
