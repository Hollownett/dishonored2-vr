# Performance mode, engine side: handover (v1150-v1177)

Branch `feat/perf-mode-motion`, worktree `VR-mods-perf-motion`. Design: `2026-11-11-perf-mode-motion-design.md`. The warp side
(`src/stale_eye_warp.hpp`, `tests/stale_eye_warp_check.cpp`, `tools/stale_warp_*`) belongs to the other implementer; this file covers
everything that feeds it. "Proven" below means code read plus a sim or live measurement; "inferred" means reasoned, not measured.

## a. What the branch added, and where

### Object motion in alternate mode (`src/object_motion.hpp`)
- `PrevMvpTable::fix(eye, frame, cur, prev, own_step)`: `own_step` 1 paired, 2 alternate. In alternate mode the engine's raw previous rows of
  a draw are the OTHER eye's render one tick ago (Phase 0 H1, measured). The chain: raw prev rows = current rows of a draw of eye^1 at
  frame-1 (`other_last`); that draw's own raw prev rows = this eye's rows at frame-2. Kinds: `same, own_last, other_now, other_last,
  unproven, miss`. Status `object_motion.alternate.kinds`.
- Unproven/miss draws in alternate mode take the camera-only previous (`View::camera` = VP_own(n-2) * inverse(VP_own(n)) * current): exact
  for static objects, counted `camera_only`.
- Alternate one-view snapshot: `note_alternate_view(view)` from the Setup hook (`dxgi_probe.cpp`, Setup block, ~line 6075); `classify()`
  snapshots the view once per engine frame under the logical eye from the alternate receipt (`set_alternate_eye`), and re-snapshots up to 4
  times a frame when a per-view write matches nothing (v1160 `resnapped`: the first write can predate the final canted/forced matrices).
- v1160 untagged fix: the v950 b1 retag (built for paired frames) is skipped in alternate mode; it looked for the other eye's view of the same
  frame, which never exists, and dropped draws (`b1_lost`).
- What still carries the other eye's rows: draws whose cb2 is over 4 KB (`oversize_kb`), unmatched shadows, layouts without previous rows,
  any draw whose table chain fails and whose camera-only fallback is impossible. That is why the motion pass has the other-eye net (below).
- Rig draws (`RigMask::draw_is_rig()`): with `stale-eye-rig-motion` absent (default since v1171) they keep the engine's raw rows and palette
  (v1165 behaviour), counted `rig_draws`; with it, see the rig-motion feature.

### Motion texture (`src/eye_motion_pass.hpp`, dispatched from `FsrTaa::dispatch`, `src/fsr_taa.hpp`)
- Inputs: t0 engine MV (R16G16F, render rectangle top-left of a full-size target, UV, current minus previous, includes the jitter step),
  t1 `Eye::depth` (R32F copy of the scene depth), t2 rig mask (R8_UINT view, v1162+). Constants from `eye_motion_constants(receipt.current,
  receipt.previous, w, h, &other_receipt.current)`: CPU doubles, camera-relative.
- Output `Eye::motion` RGBA16F: xyz world displacement (m) of the surface since this eye's previous render, w object motion in render px.
- Writes 0 when: the pass is off (`proj.w == 0`); sky (`d <= 1e-7`); arm rule (`proj.y/(d+proj.x) < 0.5 m`, or with a live rig mask
  `mvScale.z>0`: the pixel is rig; with valid rig motion both are disabled via `proj.z = 0`); within 1 cm of the previous camera plane; either
  UV more than an image outside; non-finite or |xyz| >= 100 m; the other-eye net (v1154): a mover by its own previous that lies within 3 px of
  where the other eye's render one Present earlier puts it (a still prop on the other eye's rows). w is clamped to 4096.
- No texture at all (`motion_valid=false`) without a receipt in phase >= 2, after a reset, or without the other eye's receipt one Present
  earlier (v1158). Status `motion_textures:[passes,refused,srv_misses,other_tests,no_other]`.

### Rig mask (`src/rig_mask.hpp`, copy in `FsrTaa::dispatch`)
- Recognition: a draw in a pass whose view (RenderPassMap item+0x70) is the alternate one-view, whose bound depth-stencil state writes depth,
  and that binds one of the rig's skin-palette buffers at VS t0-t2 (t0-t7 with rig motion on). Buffers: both wrappers (render model +0x2008,
  +0x2040) of the models in `image_skin_slots` (arm 0, arm 1, body avatar), published each Present (`publish_rig_buffers`, dxgi_probe).
- Replay: right after the engine's draw (`Scope::after`, called in all four draw hooks while every engine binding is still in place) the same
  draw runs again through the original draw function with: PS `return SV_Position.z`, RTV = rig-depth target (R32F, scene-depth size),
  the engine's DSV bound, depth EQUAL / no write / no stencil, opaque blend. RTVs (count and order), DSV, depth state + ref, blend state +
  factor + mask, PS + class instances are saved and restored; OMSetRenderTargets goes through the original function and the PS/wide census
  is told (barrier). MSAA depth is skipped (counted).
- Copy-time visibility (`rig_mask_copy_source`): `Eye::rig` = 255 where scene depth > 0 and |rig depth - scene depth| <= eps (D24: 2/2^24
  absolute; float: 4 ulp relative), else 0; pixel and near-miss counters read back 2-3 renders late. The rig target is cleared after each
  copy. Live only for a render of the one-view (`take_view_draws() > 0`). `Eye::rig` is R8_TYPELESS (warp reads R8_UNORM).
- Cost (sim): 12-16 replays per Present, one DSS read per one-view draw, one 3-slot VS SRV read per depth-writing draw. WARP check
  `tests/rig_mask_check.cpp`: D24 max |rig - scene| 0.5 step, D32 0.
- Not covered: held weapons and props, dual-wield copies, NPC bodies or parts the hands hold (decision: movers rules take them).

### Rig motion feature (opt-in `stale-eye-rig-motion`, v1166-v1170)
- Per eye and rig model two GPU slots (copies of the palette at that eye's renders), keys `epoch<<32 | frame+1`; read slot = this eye's
  frame-2, write slot the other (`rig_history_slots`, unit check `tests/rig_history_check.cpp`). Epoch advances when the one-view appears
  after paired frames (`begin_epoch`) and when the engine frame counter goes back.
- At a rig MV draw (`ObjectMotion` rig branch): rows own only for `own_last`/`other_last`; `RigMask::previous_palette` binds the own slot at the
  previous-palette slot (t0 measured; v1168 searches t0-t7; v1169 also replaces a "foreign" t0) or the current palette (zero), and the rows
  become own or current accordingly. t0 restored before the replay. Per-frame tally (`take_rig_motion(receipt.native_counter)`) decides
  `Eye::rig_motion`; the motion pass then keeps rig pixels' motion. Status `rig_mask.rig_motion`, `foreign_t0`, `unpaired*`, `depth_only*`.

### Plumbing
- Present block (dxgi_probe ~11830-11930): per eye `fsr_taa.eye_depth/eye_motion/eye_rig(eye,w,h,&live)` -> `GameXr::stale_eye_depth /
  stale_eye_motion / stale_eye_rig(..., live, rig_motion)`; `stale_fresh_part/kept_part` from `alt_motion_times`; RigMask enable/view/epoch;
  `fsr_taa.poll_rig_pixels(immediate)`.
- GameXr passes `StaleRig` for kept and fresh only when both are live masks of this Present (`stale_rig_mask`), else none; `StaleLayers`
  gets `rig_motion_rules` and `rig_motion[kept,fresh]` (fields defined by the warp side).

### Knobs (`%LOCALAPPDATA%\DishonoredVR\build\`)
`alternate-eye` (performance mode), `no-stale-eye-warp`, `no-stale-eye-parallax`, `no-stale-eye-motion` (no motion textures / mover rules /
alternate object-motion fix), `no-stale-eye-rig` (no rig recognition, replay, masks), `stale-eye-rig-motion` (opt-in rig MVs),
`no-stale-eye-rig-motion` (warp's rig motion rules off), `stale-near-range <m>`, `alt-shadow-slicing` (keep sun cascade slicing in
alternate mode), `no-object-motion`, `object-motion-mode <0|1>`, `no-object-motion-b1-eye`, `alt-motion-probe "<bursts> <dispatches> <s>"`
(default 6 8 10), `stale-warp-dump "<delay s> <interval s> <count> [full] [moving[=mm]] [camera[=mm]] [turn[=deg]]"`, `alt-pose-trace`,
`shadow-stability`. Diagnostics need diagnostics on.

### Status (`feedback` JSON, `object-motion.json`)
`stale_motion`, `motion_passes`, `motion_refusals`, `motion_textures`, `motion_other_still_px`, `rig_copies`, `rig_mask{enabled, rig_motion,
buffers, rig_draws_last_present, rig_draws, replays_last_present, replays, skipped, failures, unpaired..., foreign_t0...}`,
`rig_mask_pixels[2]`, `rig_mask_near_misses[2]`, `rig_mask_renders`, `rig_mask_motion`, `rig_motion_renders`, `rig_mask_warps[both, one-sided]`,
`object_motion.alternate{active, kinds, resnapped, camera_only, rig_draws, rig_own, untagged}`.

### Dumps
- `alt-motion-<burst>-<i>`: `-mv.rg16f`, `-depth.r32`, `-motion.rgba16f`, `-rig.r8`, `-b1.bin`, `.json` (receipt, sizes, b1 jitter); first two of
  a burst also `-colour.raw`, `-colour-edge.raw` (+64 texels), `-scene-edge.raw` (TAA input HDR, +64), `-upscaled.rgba`. Raw format: u32 width,
  height, DXGI format, row bytes, rows. Analysis `tools/alt_motion_probe.py <dir>` (prints own/H1 residual percentiles, rig-pixel line).
- `stale-warp-N`: kept/fresh/warped/next rgba, depth r32, motion rgba16f, rig r8 (0/255), `-meta.json` with boxes, sizes, fovea, parts,
  palms, hands_moved_m, hands_turned_deg, camera_step_m, camera_turn_deg, rig_mask[2], rig_motion[2], trigger settings.

## b. Engine facts established

- MV convention: UV, current minus previous, includes the jitter step; static world spans this eye's previous render in alternate mode because
  `alternate_native_taa` seeds the previous matrices (Phase 0 dumps, median 0.0004 px). Motion-vector PS (source read this week,
  `ShaderCache_PC.rpd`, block with `ISZPREPASS`): `mv = (pos.xy/pos.w - prev.xy/prev.w - cb_jitterrelativetopreviousframe) * 0.5; mv.y *= -1`.
  `cb_jitterrelativetopreviousframe = 2(prev - cur)` from view+0x7858/+0x7850 (`game_stereo.hpp` note, 0x3586a0): the VIEW's previous jitter.
- Skinned prepass VS (`TransformVertex`, `USESKINNING`): previous position = `ro_previousframeskinningmatrices` (VS t0) times
  `cb_previousmvpmatrix`; current = `ro_matrices` (t1); colour pass binds the current at t2 (v663 draw census 59/59; shader source).
- Palette ring (disassembly + live read): 0x25dfd0 swaps indices +0x1fb3 = +0x1fb2, +0x1fb2 ^= 1 while dirty (+0x1fb1 > 0); 0x25f930
  writes the CPU palette (+0x1ff8) into the new current wrapper, and into previous too when +0x1fb0 bit 4 is set; 0x25da80 hands out the
  current as "previous" when +0x1fb0 has 4/0x10/0x20. Live (paired, 90 Hz): every frame swap then copy, dirty restored to 2. Wrappers at
  +0x2008 + idx*0x38 hold the ID3D11Buffer* (deref = d3d11 vtable). Arms 69 joints (72 packed, 3456 B), body 20 (24 packed).
- v1170 sim: 12 distinct further palette-sized buffers (3456 B, 216 R32G32B32A32 elements, DEFAULT, SRV) appear at t0 in ~30 % of rig
  prepass draws: unexplained (see c-i).
- Player-model flag render model +0x18bc & 0x10 (arms, body, sword; live). Render model vtable 0x1b0d628; frame +0x1860; descriptor +0x1fe8,
  skeleton +0x50, joint count +0x78.
- Stencil (v1162 census, sim): engine states read 0xEF and write 0xBF, refs OR 0xFF; plane values 0, 4 (glass), 32 (marker). No free bit.
- Scene depth: R24G8_TYPELESS (D24S8), render rectangle top-left of a display-size target, reverse-Z, sky 0; learnt from clears
  (`FsrTaa::observe_depth`). In upscale mode the native TAA dispatch is replaced by a copy (t2 -> u1); t1 of that dispatch is the MV target.
- View object: frame counter +0x77b4, view +0x7450, view-projection +0x7510, jitter +0x7850 (prev +0x7858), forced projection +0x140.
- Bright right/bottom band: present in the HDR scene colour entering the TAA dispatch (`-scene-edge.raw`, v1170: last ~8 texels ramp to
  0.3-0.5 vs 0.01 inside), so before post and before our upscale; FSR 3 adds none (WARP lab `tests/fsr3_edge_lab.cpp`, 7 variants flat).

## c. Open problems

(i) Rig-motion validity never true, two wrong-palette renders. v1170: own 68182, zero 3174, unpaired 0, yet `rig_motion_renders` 0; 2 of 27
probes read 5200 px on rig pixels. Suspects: the per-frame tally is keyed by `view.frame` (classify's +0x77b4 read) but read with
`receipt.key.native_counter` (PostTonemapPass counter): verify they are the same number (log both); 3174 zero draws spread over renders also
invalidate them (each render needs zero == 0). The 12 foreign buffers look like extra palette copies (per LOD/sub-mesh or a longer ring); the
own slot is keyed per model, so a model with several palette rings gets one copy for all of them: wrong palette for some draws. Next: log
the foreign buffer's address against all wrappers of ALL render models of the player entity (read live: arms' child models, attachments),
and key history by the current buffer pointer instead of the model.
(ii) Entity jitter step (design doc "Open"): with own rows the PS still subtracts `cb_jitterrelativetopreviousframe` of the VIEW's previous
frame (the other eye's jitter in alternate mode), so own-row draws carry (J_view_prev - J_own_prev) error; two row-side corrections (v1155
snap, v1156 shift) made it worse. Next: replace the per-view constant for the one-view (patch view+0x7858 with this eye's own previous jitter
before Setup, or rewrite b1 at the prepass) and re-measure residuals; the shader is in `ShaderCache_PC.rpd` (search `psMotionVector`).
(iii) Held weapons/props are not in the mask: rigid models have no skin buffer. A mesh-identity link needs the rigid render model's surface
index/vertex buffers (decode model+0x1bb0 source -> surfaces; `native_note_geometry` shows the skinned path) plus instance identity (props share
meshes with world copies). Live RE session needed.
(iv) Band source pass: a scene pass before TAA. Guess: a reduced-resolution buffer (screen shadows, SSAO, fog) bilinearly sampled past the
render rectangle under the forced dynamic resolution. Next: dump pass outputs at the right edge per pass id (render_pass_map) or patch the
UV clamp constants the engine uses for dynamic resolution.
(v) Other cross-eye temporal state in alternate mode: shadow cascade slicing fixed (v1160 forces slicing off); native TAA bypassed in upscale
mode; exposure: FSR3 auto exposure off in upscale mode, engine eye adaptation not examined; fog/volumetric history, SSAO, SSR, motion blur and
the ocean (v731) not examined; any of them keeps the other eye's history.

## d. Build, checks, access

- DLL: `build\compile-vNNNN.cmd` (copy the latest, replace the version), run from `build\` as `.\compile-vNNNN.cmd > compile-vNNNN.log 2>&1`;
  success = `dll_exit=0` in the log. FidelityFX/DLSS libs come from `..\VR-mods-amd-ags\third_party`.
- Checks: `build\check-rig-mask.cmd`, `build\check-fsr3-edge.cmd`, `build\check-stale-warp.cmd`, `build\check-eye-motion-pass.cmd`
  (+ `tools\alt_motion_probe.py --check-pass synthetic`), `tests\rig_history_check.cpp` (pure), `python tests\alternate_eye_knob_check.py`
  (line 153 pin fails since before this branch: run with that line commented to reach the later pins).
- Any D3D/WARP run starts with `build\d3d-guard.cmd`: refuses while Dishonored2.exe runs or `..\VR-mods-amd-ags\artifacts\agent-locks\game`
  exists (sims hold it). Never launch, close, inject into or write to the game, never kill it, never touch saves or the game config or the
  user's `%LOCALAPPDATA%\DishonoredVR\build`; read-only memory reads (tools/scan_body_material.attach) are allowed.
- Packaging: `build\package-candidate.cmd <version>` (coordinator's step).
