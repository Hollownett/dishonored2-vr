# VR v1030 (test candidate, 2026-11-05) — the FPS fix, plus the gun's body

This build carries **two** changes made in the same worktree at the same time:

1. **Canted eyes render at the eye size again** (this session) — the answer to the second FPS report,
   *"fps still is worser then in v1020. I suspect physics upgrades."*
2. **The gun's physical body actually collides now, and a still weapon's contact scan costs nothing** (parallel session) — recorded
   in `artifacts\repair-v1030-test-candidate.json`; it is summarised at the end of this note.

Both are in `build\d2vr_render_probe_arm_v1030.dll` (14,925,824 bytes, SHA-256
`3dcbd0ea2cd95444342c94caa6239534646944e5d0078f8dfb533623d4d0bfd6`), which is what
`artifacts\repair-v1030-test-candidate.json` and `%USERPROFILE%\Downloads\DishonoredVRLauncher-v1030.exe` carry.

## 1. The FPS: the optical density, not physics

The physics is not it. `perf.mod_cpu_ms_mean` is **0.98 ms**: every CPU instruction the mod runs in the Present hook, the grip
ball, the weapon-body lane, the contact solve and all. The frame is spent rendering, and 7.59 ms of it is the mod's own hook.

What was left after the v1028 fix is the **optical density**. Since v986, a canted eye has quietly rendered 6.9% larger per axis
(16.5% more pixels) than the XR eye; because the XR eye chain stays at the runtime's recommended size, the mod pays an `EyeResolve`
pass on **both eyes every frame** and then submits the resolved image.

The v1029 session (2026-11-05 20:09-20:18, pid 30708, 38983 frames):

```
frame_split.interval_ms_p50   15.94  (p95 23.03)  -> 63 FPS on a 90 Hz runtime
frame_split.eye0_ms_p50        3.87   (the game rendering the left eye)
frame_split.eye1_ms_p50        4.24   (the game rendering the right eye)
frame_split.hook_ms_p50        7.59   (the mod's Present hook: FSR 4, the resolve, the copies)
perf.mod_cpu_ms_mean           0.98   (ALL mod CPU in the hook)
eye_resolves                   72448  (~1.86 per frame: a full-screen resolve, both eyes, every frame)
game_render_size               2668x2930
xr_eye_size                    2496x2688     <- the game's image is 16.5% larger than the eye it is submitted to
mod_gpu_ms_p50                 fsr 1.95+1.91, sharpen 0.11, copy 0.10, overlays 0.04, sum 4.21
```

The physics counters that session: `weapon_body` published 66960 times with 92 touches and 44 pushes, and the hands' contact lane
is sub-millisecond. Nothing in v1024-v1026 touches the GPU or the eye chain.

The comparison the user is making, from v1020's own transfer artifact (`xr-transfer-left.json`, 18:53:20):

```
"width":2496,"height":2688,"pixels_equal":true,"gameplay":true,
"submitted_fov":[-0.942477763,0.69813168,0.767944872,-0.959931076]
```

One 2496x2688 image, identical to what the game rendered, **canted**. No resolve, no density. That is the render size this build
restores.

### The change (`src/dxgi_probe.cpp`)

```cpp
// before: canting always forced the density
if(cant_requested) {optical_density_requested=true;optical_density_applied=false;}
// now: only on request
if(cant_requested && fs::exists(root/L"build"/L"optical-density")) {optical_density_requested=true;optical_density_applied=false;}
```

- Canting itself is unchanged (`build/no-cant` still turns the canted frusta off); only the automatic 1.069x density goes away.
- `build/optical-density` (or the launcher's optical-density option, through `dxgi-probe.cfg`) still applies it once in gameplay and
  still honours it through a resolution change.
- With the game and the eye chain both at 2496x2688, `eye_texture_matches` passes, `EyeResolve` does not run, FSR upscales to the
  submission size instead of above it, and the image keeps its full density instead of being resampled down.

Expected: the 8.11 ms of eye rendering falls ~14% to ~7.0 ms, FSR ~14% to ~3.3 ms, and the resolve (~1 ms plus its copies)
disappears — about 2.5-3 ms, i.e. ~13 ms per frame (~77 FPS) instead of 15.9 ms (63 FPS) on the same scene.

## 2. The gun's body (parallel session)

The v1026-v1029 weapon body gave the barrel and blade sphere beads and pushed props with them; the sword shoved fine, the gun never
touched anything (20:18 session: 44 weapon pushes, all on the blade). Cause: the world sweep stops a weapon's segment its own
radius off the surface (a pistol .035 m), so beads of exactly that radius rest on the surface with no overlap, and the contact model
pushes only on an overlap; the blade is swept as a line (radius 0), which is why only it worked. The body now reaches
`weapon_body_skin` (.015 m) past what the sweep holds off, the registry scan is gated on motion (a still weapon scans every 8th
frame, a moved one every frame), and the status reports gun and blade contacts separately
(`weapon_body.gun` / `weapon_body.blade`). Details in `docs\WEAPON-BODY.md`.

## Build and checks

- `build\d2vr_render_probe_arm_v1030.dll`, MSVC 2022 x64 `/W4`, `dll_exit=0`; only the pre-existing C4456/C4459/C4244/C4267 and
  FidelityFX LNK4099 warnings.
- `tests\optical_density_routing_check.py` (new, registered in `tools\build-msvc.cmd`): canting may no longer request the density,
  the knob is the only opt-in, the cfg flag still requests it, and the application/resolution paths are unchanged. PASS.
  `tests\eye_capture_routing_check.py` PASS (the v1028 latch is untouched). `tests\weapon_body_check.cpp` PASS (parallel session).
- `artifacts\repair-v1030-test-candidate.json` is the candidate the launcher picks; its SHA-256 matches the DLL above.

## Not verified

- **No headset run of v1030.** The saving is estimated from the v1029 session's own `frame_split`, not measured.
- The density was a sharpness feature (a 6.9% supersample resolved down). It is off by default; the image is the size the user's
  v1020 build submitted. `build/optical-density` restores the supersampled size.
- There is no v1020 session record with `frame_split` numbers, so "matches v1020" means the same render configuration
  (2496x2688 canted, no resolve), not a measured equal frame time.
- The gun-body fix is likewise unobserved: watch `weapon_body.gun` in `xr-live-status.json` while a pistol or crossbow is driven
  into a prop.

## What to check

1. `xr-live-status.json`: `eye_resolves` must stay **0**, `game_render_size` and `xr_eye_size` both 2496x2688, and
   `frame_split.interval_ms_p50` should fall from 15.94 ms to ~13 ms with `hook_ms_p50` from 7.59 to ~5 ms.
2. `perf.mod_cpu_ms_mean` stays ~1 ms — that is the whole mod's CPU, and it is the answer to the physics suspicion.
3. If it is still short of v1020: `feedback_render.eyes[].render` (2054x2256 here) is the FSR input; that is the launcher's
   AA/upscaler setting, not physics.
