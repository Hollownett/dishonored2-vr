# Held-weapon draw placement under temporal AA (v1101)

Scope: the held weapons' draw-time placement (`src/weapon_draw_place.hpp`, the weapon branch of `ObjectMotion::prepare` in
`src/object_motion.hpp`, the publisher `publish_weapon_draw` at the eye-0 begin in `src/dxgi_probe.cpp`) and the black flash the
user sees with FSR 4 Native (temporal AA), paired stereo, 90 Hz. Line numbers below are the **pre-v1101** (base) source, so they
identify the code that caused the report; the new code is named at the end of each section.

## 1. How a weapon draw is recognised and placed before v1101

`Place::place` (`src/weapon_draw_place.hpp:133`) runs once per layout draw, from `ObjectMotion::prepare`
(`src/object_motion.hpp:533`), with the draw's per-instance rows read from the shadowed VS b2 constant buffer:

- cheap rejection: the clip w of the draw must be in `(0.01, near_w=3)` (`weapon_draw_place.hpp:135`);
- the render's published slot for the engine frame (`:136`), the two eyes' view-projections (`:138`; `weapon_views`,
  `object_motion.hpp:623`);
- a side is chosen by orientation only: `same_orientation(inverse(VP)·MVP, F_side)` over both eyes, the **last** matching side wins
  (`weapon_draw_place.hpp:144-147`);
- the eye is **certified from the engine's cached previous MVP** (the instance's MVP in another view/frame), because the context's
  per-view eye tag is wrong for ~90 % of weapon draws (`:152-170`). The same-frame certificate needs the other eye's MVP of this
  frame; the previous-render certificate needs this draw's `cb_previousmvpmatrix` rows and the previous render's eye views;
- a draw the cache cannot certify takes the context's eye tag when the orientation fits it (`:172-176`, v960's `tagged`);
- reach around F (`:177-178`), then `cur' = VP·D·M` and `prev' = VP_prev·P·M` (`:180-189`), and a slot-reuse check (`:190`).

The engine renders in named passes (`src/render_pass_table.hpp:31-83`: `DEPTHPASS` 17, `SHADOWS_WORLD/DYNAMIC` 20/21,
`FORWARD_WORLD/DYNAMIC` 27/28, `ALPHA_LIT` 38, ...). The pass a draw is recorded in was already known through
`RenderPassMap` (`src/render_pass_map.hpp:44`, `:60-109`), but the weapon placement ignored it.

## 2. Root cause of the black flash

The placement's verdict was made from **per-draw and per-pass state that is not the same in every pass of the same weapon part**:

1. **The frame's first weapon draw has no same-frame certificate.** `view 0` renders first, so its depth prepass is the first draw
   of the weapon in the frame. The cached previous MVP of that instance is then from the previous render (other eye / other pass),
   not from this frame's other view. It can only be certified through the previous-render branch
   (`weapon_draw_place.hpp:163-170`), which requires (a) the draw's own `cb_previousmvpmatrix` rows and (b) the previous render's
   views. A vertex shader without previous rows (a depth-only or velocity-only layout) never even reaches that branch: `gather`
   (`object_motion.hpp:116-124`) returns false without all four previous float4s, `prepare` counts `no_rows` and returns
   (`object_motion.hpp:540`). The forward pass, whose material shader declares them, is placed. **Result: the depth prepass writes
   the weapon at the tick frame while the forward pass draws it in the hand.**
2. **The context eye tag is not a certificate.** `ObjectMotion::current_view` (`object_motion.hpp:475-489`) falls back to the newest
   view written anywhere when the recording context's own per-view write is not of the newest frame; for a weapon draw that can be
   the other eye (the v930 probe: origins off by the eye separation). The v960 tagged fallback (`weapon_draw_place.hpp:172-176`)
   then places with the wrong eye's VP for 0.3 % of the certified draws and unknown rates of tagged ones.
3. **Passes can bind different per-view constants.** `prepare` requires its context's last classified per-view write to match an
   eye VP (`current_view`, `object_motion.hpp:537-538`) and requires the draw's current rows to be affine under that eye's inverse
   (`:542`, `guard_fail`). A pass that binds depth/light constants, or simply has not written the classified buffer on that
   context yet, is `untagged` and keeps the engine's tick-frame rows even though its geometry is the same weapon part.
4. **The previous rows of an uncertified draw were an approximation.** With no previous render's view the code used the current VP
   for the previous world position (`weapon_draw_place.hpp:181-188`): the hand's own motion but not the camera's. Under head
   rotation that is the wrong reprojection.
5. **The publisher never had the opposite hand's copy.** `weapon_models` (`dxgi_probe.cpp:3086`) only reads the two inventory
   slots; the mod's mirror/held copy is a separate spawned entity. The last headset session shows `published [4877, 0]`: the copy
   was never published, so its draws could never be placed (the second user report).

Temporal AA consumes the depth prepass, the motion vectors and the colour together. When the depth/motion rows of a weapon part
are at the tick-frame position and the colour rows at the hand, TAA/DLAA/FSR/DLSS reproject history from the wrong pixel: the black,
untextured-looking flash. v970 (`object_motion.hpp:228, 306-311, 545`; `dxgi_probe.cpp:13005, 14142`) removed the symptom by
disabling the whole placement under every temporal mode; the last session then reports `weapon_temporal_skip=1` and zero
placements, which is consistent but leaves the weapon lagging (user report 1).

## 3. The fix (v1101)

### 3.1 The pass names the eye; the publisher names the weapon

`RenderPassMap` now exposes, on the recording thread, the current pass id and the pass job's view (`item+0x70`, the same view
pointer the engine's pass callbacks read: `src/render_pass_map.hpp`). `dxgi_probe` installs that pass map when the weapon placement
is enabled (in addition to `build/native-gpu-timing` / `build/shadow-share`) and gives `ObjectMotion` a `DrawPass` source and the
pass-name table. `ObjectMotion::prepare` asks for the pass before placing; a draw whose pass view equals one of the two recorded
eye views is a **view pass of that eye**. `Place::place` takes that `pass_eye` and uses it as the certificate:

- no engine cached previous MVP is needed for the eye;
- no `cb_previousmvpmatrix` rows are needed (`gather_current` reads the current rows only; `gather_previous` is separate);
- no classified per-view constants are needed;
- the previous rows are always the publisher's `P` under the previous render's view of that eye, whether or not this draw's
  shader declares them.

All passes of one part therefore compute their rows from the same inputs: the engine's current MVP rows (the engine writes the
frame's per-instance constants once, before the eye-0 begin, v901 probe) and the same published side and eye. The rows are
identical by construction. The current rows are `VP·D·M`; the previous rows are `VP_prev·placed(previous render)·inverse(F)·M`,
i.e. the true previous screen position of that part as it was drawn last render (scratch the current tick frame from both frames;
parts sit at fixed local offsets from F).

Without the pass map (install failed or the draw is outside a pass job) the v931 certificate and v960 tag fallback run unchanged;
with no previous rows that path returns `no_certificate` rather than guessing an eye.

### 3.2 Shadow passes: decided, not placed

A pass job whose view is not one of the eye views is a light/shadow pass (`SHADOWS_WORLD/DYNAMIC` 20/21, ...). The held weapon's
shadow is drawn with the light's view-projection, which the mod does not track, and is not part of the eye's TAA history. Those
draws stay at the engine's tick frame and are counted (`weapon_draw.place.passes`, id 20/21, `matched = 0`, `placed = 0`). Placing
them with an eye VP would move a shadow into a projection that does not belong to it. `SCREENSHADOWS` (25), `FORWARD_*` (27/28),
`ALPHA_LIT` (38), `DEPTHPASS` (17) and any other pass whose view is an eye view are placed like the forward pass.

### 3.3 The opposite hand's copy

`NativeWeaponHands` publishes a `CopyDraw` per hand (`src/native_weapon_hands.hpp`): the copy's entity/id (its render model is read
live at `entity+0xf0 -> model+0x1860`), the hand pose it was last placed from (15-float model-frame layout) and a serial that counts
those placements. Both copy paths feed it: the mirror service (`service_mirror`, after the copy is shown/teleported from
`feed(other_side)`) and the mod-owned held copy (`service_held`, after `held_copy::place` from the carried hand). It is cleared when
the copy is hidden, released, replaced, or a level/session changes.

`publish_weapon_draw` (`dxgi_probe.cpp`) now prefers, per hand, the shown copy over the native inventory slot, and applies the
same pair-validity rules: a new placement serial starts a new pair; the same serial with a changed model frame (something else
moved the copy) invalidates it; a pair not renewed for 3 publishes is stale; the read hand must be within 0.5 m of the image hand
and the weapon frame within 0.6 m of the read hand, with less than 60° of rotation. The delta is `image_hand · inverse(read hand)`,
i.e. the copy is moved to the hand this render draws exactly like the native weapon. `weapon_draw.published` now counts both hands;
a copy hand is no longer 0. (v1102 replaces this serial/frame pair with the known-writer log in §6; the side and gate rules stay.)

### 3.4 Two identical weapons (two pistols)

Both sides can have the same model and the same orientation (a native pistol in one hand, a copy in the other). Orientation alone
cannot tell them apart, so the side is now chosen by the **model-origin distance** to each published frame, smallest first, within
`reach` (`weapon_draw_place.hpp`, `origin_distance`/`origin_tie`). Each weapon's parts sit at fixed local offsets from their own F,
so the nearest F is that weapon's. Two sides whose distances are within `origin_tie` (1 cm) are **ambiguous** and the draw is left
to the engine (counted) instead of guessed. The two hands are at least a hand apart in normal play, so the intended weapon is the
unique nearest; the residual risk is documented below.

### 3.5 Knob

`build/no-weapon-draw-place` remains the only off switch (`ObjectMotion::weapon_mode`, read every 2 s): it makes the publisher
publish no hand, so every placement is rejected (lab-checked). `build/weapon-draw-place-temporal` and
`ObjectMotion::set_weapon_temporal` are removed; the v970 skip no longer exists.

## 4. Status to read in one headset session

`xr-live-status.json -> arm_flicker.weapon_draw`, and `object-motion.json -> weapon_status`:

```json
"weapon_draw": {
  "published": [left, right],              // render publishes per hand (copy included)
  "skipped":  [left, right],               // publisher skips per hand
  "skip_reasons": [[...6...],[...6...]],   // not an image hand, no image, no read/stale, frame unreadable, not held/implausible, pair invalid
  "engine_dev_mm": [...], "placed_dev_mm": [...],
  "place": {
    "published": [l, r],                   // copied from Place
    "placed":   [l, r],                    // draw-time placements per hand
    "skip":     [{"no_certificate":n,...},{"far_away":n,...}],   // per hand, by rejection stage
    "passes": [
      {"id":17,"name":"ARK_RENDERPASS_DEPTHPASS","near":n,"matched":n,"placed":n,"left":n,"right":n},
      {"id":27,"name":"ARK_RENDERPASS_FORWARD_WORLD","near":n,"matched":n,"placed":n,"left":n,"right":n},
      {"id":20,"name":"ARK_RENDERPASS_SHADOWS_WORLD","near":n,"matched":0,"placed":0,"left":0,"right":0}
    ]
  }
}
```

`near` counts every candidate draw (anything within `near_w` that reached the weapon branch, so the hands and other held items are
included); `matched` counts the draws that fitted a published weapon side (orientation + reach); `placed` counts those that were
actually re-placed. What a correct session looks like: `published` and `placed` > 0 on both hands (the copy included), every
eye-view pass that saw the weapon has `matched == placed` (every weapon-shaped draw in that pass was placed), and `matched > 0`
for `DEPTHPASS`/`FORWARD_*`. `SHADOWS_*` is expected to show `matched = 0, placed = 0` by design (its light view does not fit the
eye's orientation test). `weapon_near_draws` should equal the sum of the passes' `near`; `weapon_no_rows`/`weapon_untagged`
should not grow for the weapon's passes. A pass with `matched > placed` is the case this change exists to eliminate; its skip
stage is in `place.skip`.

## 5. Residual risks (not claimed as fixed)

- **A pass with a different projection** for the same part (a depth prepass using a depth-only VP instead of the eye's jittered
  VP) is rejected by the orientation/reach test under the eye VP rather than misplaced. The status per pass shows it
  (`matched > 0, placed = 0`). The fix assumes the engine builds all view passes' per-instance MVP from the eye's VP, which is what
  the v930/v931 probes saw.
- **Indirect draws**: the draw hooks cover `DrawIndexed/DrawIndexedInstanced/Draw/DrawInstanced`; `DrawIndexedInstancedIndirect`
  and `DrawAuto` are not hooked. The recorded weapon draw count (~5 per eye per frame) came from hooked draws, so the weapon does
  not use them; a future pass that does would not be placed (visible as a missing per-pass `matched`).
- **Copy pair latency**: the copy's model frame is read at the eye-0 begin while the copy service runs on the game thread. A pair
  whose model frame moves without a new serial is rejected; a teleport applied one frame late can therefore leave the copy
  unplaced for that frame (counted as `pair invalid`), not at the wrong place.
- **Two weapons whose frames are within 1 cm** (overlapping hands) are ambiguous and not placed; two weapons ~0.5–0.8 m apart are
  told apart by origin distance.
- **One published weapon per hand.** If a hand really draws two weapons at once (e.g. under mirroring, a natively drawn grenade on
  the same arm as the other slot's copy), the copy source wins and the other weapon's draws stay at the engine's tick frame. The
  reported dual-wield case (one weapon per hand) is covered.
- The status/placement path relies on the official pass map's `item+0x70` view. A pass whose item has no view is not placed
  (counted as a pass with `near` and no `placed`).
- No headset, sim or in-game run was made for this change. Only the DLL build and the two lab checks named in the final
  report ran; nothing about in-game behaviour is claimed.

## 6. Round 2: who moves the weapon (v1102)

The v1007 headset status (`publish_weapon_draw` [5, 0], skip reason 5 "pair invalid" for 12100/11749 frames) showed that the
v1101 publisher trusted only `advance_attachment`'s hand read. With dual wield the weapon is moved by the mod, not by that
read: the engine attachment cross moves a native slot's item to the **other physical hand**, the mirror/held copy services
teleport a copy from the **drawn** hand, and a carried copy is stepped/turned by the player's motion. None of those wrote a
pair, so almost every frame was rejected as "frame moved without a new read", and the frames that reached the gate used the
wrong hand for a crossed weapon (the engine arm's hand instead of the hand the item rides), which is exactly what the
motion/origin gate rejected.

Every writer now reports, on the thread that moves the frame, `(physical hand, engine slot side if native, exact world hand
pose it placed from, a serial)` to `weapon_draw::WriteLog` (`src/weapon_draw_place.hpp`). The publisher observes the live
model frame of that hand and binds the first frame of a weapon to the first read; a later frame is accepted only when it is
the rigid hand motion of the last accepted frame, `frame' = read' * inverse(read) * frame`. The grip is fixed in the hand, so
the hand moves the frame on its **left** in world space. A frame no writer's read produces is still rejected (skip reason 5):
that is the only remaining meaning of "frame moved without a new read". The publisher then publishes that hand's live frame
and `D = image_hand(physical) * inverse(read)`; the draw-time side choice (origin distance) is therefore in physical hands.

| Path | Code | What writes the weapon model frame | Hand pose it placed from, when | Pair published |
|---|---|---|---|---|
| Engine attachment (native slot, not crossed) | `NativeAttachmentPose::around_frame` (`src/native_attachment_pose.hpp`), `advance_attachment` (`src/dxgi_probe.cpp`) | the engine composes `model+0x1860` from joint 59 of the arm palette after the read; `advance_attachment` moves a pre-upload read onto this tick's image hand | `full_body.image_world(engine_side,59,frames)` (or the live arm frame without an image solve), on every attachment read of joint 59 | `WriteLog` kind `engine`, physical = engine side; the live frame is bound to the read by the publisher |
| `NativeAttachmentPose` cross | `around_frame` → `cross_joint` (`native_power_hand.hpp`) | the destination arm's item is composed from the **origin** controller's joint 59, re-expressed in the destination model (y180 for weapons) | destination live arm model frame × the crossed joint, read after the cross; the physical hand is `engine_side ^ 1` | `WriteLog` kind `cross`, physical = origin hand, engine side = destination slot |
| Mirror copy (no attachment-pose install) | `NativeWeaponHands::service_mirror` → `mirror_slot` (`src/native_weapon_hands.hpp`) | native hidden; copy entity teleported to `target(feed(other_side), mirror(offset))` | the DRAWN hand of the copy's side from the render thread's `hand_pose` (≤250 ms), each game-thread service | `CopyDraw` + `WriteLog` kind `mirror`, physical = `other_side` |
| Mod-held copy | `service_held` | copy entity teleported to `held_copy::place(hand, offset, mirrored)` | `feed(side)` (drawn hand) then `held_copy::carry` by the player's last root step/turn (v776/v816), each service | `CopyDraw` + `WriteLog` kind `held`, physical = `side` |
| Hold-to-hold / sword grip | `SwordGrip`, `GunGrip`, `HandLoadout` (`src/hand_loadout.hpp`, `src/dxgi_probe.cpp` routing) | nothing itself: it decides which hand holds what (`hand_empty`, `mirror_native`, `gun_clip`, `weapon_grip`), i.e. which native clip or procedural grip poses the fingers; the item is moved by the rows above | controller squeeze/hold state at the control tick | covered by the native/copy pair of the item it routes |
| Tick placement, opt-in | `place_weapons` (`src/dxgi_probe.cpp`, `build/weapon-image-place`, off in the report) | writes `model+0x1860` at the eye-0 begin | `weapon_hand_read` from `advance_attachment` | none: it is a second writer at draw time. It is off by default; if enabled it would move the frame after the publisher and the pair would reject it, which is the safe direction |
| Any other engine movement (regrip, equip clip, hide/show) | the game | moves the frame without a mod read | none | rejected (skip 5) until a known path writes again |

The publisher works per **physical hand** (0 left, 1 right). The inventory slot is only used to find the live native model:
`weapon_models()` returns the model of the engine arm's slot, and the write's `engine_side` says which one; a crossed native
weapon therefore publishes the left hand's frame from the right slot's model without confusing the two hands. Copies are read
live from their own entity, exactly like the native weapon. Two weapons in two hands publish two independent sides; the
draw-time `Place` still chooses the side by model-origin distance, so two identical pistols remain separate.

Status (`arm_flicker.weapon_draw`): `source` adds, per physical hand, the accepted count by writer kind
(`engine`, `cross`, `copy`, `held`); `published`/`skipped`/`skip_reasons` are now per physical hand. Skip reason 5 is the
unrecorded-writer rejection (no known write produced the live frame), not the old serial/frame mismatch. `weapon_draw.place`
is unchanged and still reports per-side draw-time placements by pass.

### 6.1 Grip-pose fallback (left-hand gun)

The finger pose is chosen in `FullBodyDriver::solve_locked` / `BodySolver::pose_arm` (`src/body_ik.hpp`). A hand that the game
says is not empty (`hand_empty=false`) keeps the game's own native clip (`finger_mode 3`). For a mod-held gun there is
sometimes no gun clip to copy: the code path in `dxgi_probe.cpp` sets `gun_clip[s]=true` when the native slot-1 gun model is
absent (`left_gun_model` false: a displaced gun, a guard pistol, slot 1 holding a power/grenade), but the kept gun clip
(`have_gun_clip_`) only exists once the left arm actually played a calm gun clip. The hand then followed the arm's
empty/idle clip and the fingers stayed straight around the gun. The routing now sets `Tracking::weapon_grip[s]` when a
`gun_clip` is wanted but `FullBodyDriver::gun_clip_ready()` is false; `BodySolver` poses that hand procedurally
(`finger_mode 2`), closes every finger to at least 0.8 of the grip (`finger_local`), ignores contact turns for it, and clears
the native socket token. A held gun therefore always has a grip pose while held; a real authored gun clip is still used when
it exists.

### 6.2 Open risk found in the v1007 status (not fixed here)

`object-motion.json` in the v1007 session counted all near weapon candidates (`weapon_near_draws` 384298) and all 41
placements in `ARK_RENDERPASS_DEPTHPASS`; no other pass appears in `weapon_draw.place.passes`. The draw inventory only
contains vertex-shader layouts for shaders that declare `cb_previousmvpmatrix` (`linux/proxy/vs_layouts.hpp` reflects only
those; the private copy has no proxy tree). If the weapon's forward/colour shader does not declare that buffer, or the draw
uses an indirect/other path that is not hooked, `ObjectMotion` never sees the colour draw and no publisher fix can place it.
That would leave visible colour lag even with every pair valid. This round's changes fix the pair and side mapping for every
draw the placement does see; they do not add a new draw hook or a proxy layout for shaders without previous rows, and no
in-game run was made to test the colour pass. The per-pass status is the field that will show it: a view pass with near draws
and `matched == placed` is placed; a pass that never appears in `place.passes` was never seen by the placement.

## 7. Round 3: colour pass shaders (v1103)

The round-2 open risk above is confirmed in the shader cache: the held weapon's **colour/forward** vertex shader declares the
current MVP rows only. The old proxy `reflect()` returned 0 unless the bytecode named `cb_previousmvpmatrix`
(`linux/proxy/vs_layouts.hpp`, pre-v1103), so `ObjectMotion` never saw the colour draw and the per-pass list only had
`ARK_RENDERPASS_DEPTHPASS`. The depth/Z-prepass variant of the same shader declares both row sets and writes the previous clip
position; that is why depth was placed and colour was not. This section records the evidence and the new encoding.

### 7.1 Cache format (correcting the earlier note)

`ShaderCache_PC.ucd` does **not** contain HLSL source. It contains compiled DXBC shaders, indexed by `ShaderCache_PC.uci`:

- `.uci`: a `u32` version word (`14`) and `(u32 ucd_offset, u32 hash)` pairs; the first pair is a header, every later
  offset points at a record. 14,711 records.
- each record: `u32 uncompressed_size` followed by a **raw deflate** stream (`zlib` wbits `-15`). The stream is shorter than
  the advertised size; the first record advertises 3,948 bytes, its stream consumes 1,457 bytes and expands to 3,948. The
  next record is found from the `.uci` offset, not by walking compressed bytes.
- the decompressed record is a DXBC container (`RDEF`, `ISGN`, `OSGN`, `SHEX`, `STAT`).
- the HLSL source is in `ShaderCache_PC.rpd`, indexed by `ShaderCache_PC.rpi` (name -> `(rpd_offset, size)`); the two blocks
  quoted below are `vertex` and `arkdefault`.

Record census: 8,323 pixel shaders, 6,184 vertex shaders, 48 compute shaders, 156 records with no RDEF (the last are not
model shaders). Of the 6,184 vertex shaders, **2,023** name the current MVP rows:
**1,931 current-only** and **92 with both current and previous rows**. The v1007 round-2 status recorded
`proxy_shaders:[6251,93,0,0]` (`created,with_previous,unstored,unhooked_devices`), i.e. the cache's 92 previous-row VS match
the 93 the runtime actually recorded, and the other 1,931 were invisible to the proxy. That match is why the cache is usable
evidence for the runtime variants.

### 7.2 Cbuffer name, slot and row layout

Every model vertex shader's per-instance constants are in `PerInstanceCB`; the current MVP is four separate `float4`
variables `cb_mvpmatrixx`, `cb_mvpmatrixy`, `cb_mvpmatrixz`, `cb_mvpmatrixw`, and the previous set is
`cb_previousmvpmatrixx/y/z/w`. The variables are not an array: the engine packs other per-instance constants around them, so
the row index differs per variant. The proxy's packed word stores the actual row index of each of the eight variables
(`byte k = float4 index`, `0xff = absent`).

The **offline cache** reports `PerInstanceCB` at `b0`. The runtime bytecode that `CreateVertexShader` sees has it at `b2`:
the existing proxy only accepts `BindPoint==2` and the v1007 status recorded 93 layouts, so the runtime shader differs from
the cache in this one bind point. The bind remap does not change the variable names or their float4 offsets inside the
buffer; the proxy keeps using `b2`.

Representative records (ucd record index, `.ucd` offset, uncompressed size; rows are `x,y,z,w` then `previous x,y,z,w`):

| Class | rec | ucd off | bytes | `PerInstanceCB` | current rows | previous rows | inputs |
|---|---:|---:|---:|---|---|---|---|
| rigid colour | 144 | 3,150,513 | 14,496 | 320 B, 26 vars | 7,6,5,8 | absent (all `0xff`) | POSITION, NORMAL, TANGENT, TEXCOORD |
| rigid colour + vertex colour | 174 | 3,427,531 | 4,020 | 352 B, 31 vars | 6,5,4,7 | absent | + COLOR |
| rigid Z prepass / velocity | 3,175 | 106,602,819 | 1,904 | 160 B, 11 vars | 7,6,5,8 | 3,2,1,4 | POSITION, TEXCOORD |
| rigid Z prepass / velocity | 2,804 | 86,223,031 | 1,676 | 144 B, 9 vars | 7,6,5,0 | 3,2,1,4 | POSITION |
| skinned Z prepass / velocity | 1,274 | 28,466,650 | 4,276 | 160 B, 11 vars | 7,6,5,8 | 3,2,1,4 | POSITION, SKININDICES, SKINWEIGHTS, TEXCOORD |

DXBC SHA-256 prefixes (first 16 bytes): rigid colour `0f880f160ecd4e9a6e012def3adceeeb`, rigid colour+COLOR
`f6847373ae96c00b8032e481c053cf0d`, rigid Z(POS,UV) `96152674ec4c4f14892a7d9b1c0e1bea`, rigid Z(POS)
`f61aca86392271991c2108fac5ac6236`, skinned Z `b8c941c54d0c4ce22c0f66a9bb027d4a`.

The variant sets, grouped by input signature (vertex shaders naming `cb_mvpmatrix`):

| Input signature | current-only | with previous |
|---|---:|---:|
| POSITION, NORMAL, TANGENT, TEXCOORD | 284 | 0 |
| POSITION, NORMAL, TANGENT, TEXCOORD, COLOR | 208 | 0 |
| POSITION, TEXCOORD | 93 | 31 |
| POSITION | 25 | 5 |
| POSITION, TEXCOORD, SKININDICES, SKINWEIGHTS | 130 | 42 |
| others (skinned/normal/tangent/colour combinations) | 1,191 | 14 |
| **total** | **1,931** | **92** |

The rigid colour rows are the ones that matter here: no `POSITION, NORMAL, TANGENT, TEXCOORD` colour variant declares
`cb_previousmvpmatrix` (and none of the 208 vertex-colour colour variants does). The rigid Z-prepass class does: 31 of its
`POSITION, TEXCOORD` variants and 5 of its `POSITION` variants declare both row sets. The Z-prepass class without previous
rows (93 / 25) is consistent with the alpha/`ISZPREPASSALPHA` branch that does not write a previous position.

### 7.3 Source: the colour and Z-prepass branches

`ShaderCache_PC.rpd`, block `vertex` (rpi entry 179, rpd offset 1,150,133, size 19,149), `TransformVertex`:

```hlsl
VertexOutContext TransformVertex( in VertexInContext ctx, bool _previousFrameTransform = false ) {
    ...
    #else
        ...
        if( _previousFrameTransform )
        {
            modelMatrixX = cb_previousmodelmatrixx; ...
        }
        else
        {
            modelMatrixX = cb_modelmatrixx; ...
        }
    #endif
    ...
    #else
        if( _previousFrameTransform )
        {
            result._viewPosition = mul( cb_previousmodelviewmatrix, localPosition ).xyz;
            result._projPosition.x = dot( localPosition, cb_previousmvpmatrixx );
            result._projPosition.y = dot( localPosition, cb_previousmvpmatrixy );
            result._projPosition.z = dot( localPosition, cb_previousmvpmatrixz ) * cb_modeldepthhack.x;
            result._projPosition.w = dot( localPosition, cb_previousmvpmatrixw );
        }
        else
        {
            result._viewPosition = mul( cb_modelviewmatrix, localPosition ).xyz;
            result._projPosition.x = dot( localPosition, cb_mvpmatrixx );
            result._projPosition.y = dot( localPosition, cb_mvpmatrixy );
            result._projPosition.z = dot( localPosition, cb_mvpmatrixz ) * cb_modeldepthhack.x;
            result._projPosition.w = dot( localPosition, cb_mvpmatrixw );
        }
    #endif
```

`ShaderCache_PC.rpd`, block `arkdefault` (rpi entry 183, rpd offset 1,182,153, size 18,802), `MainVp`:

```hlsl
VertexOutContext vout = TransformVertex( victx );          // colour/forward: current rows only
rw_sv_position = vout._projPosition;
...
#if !ISSHADOW && !ISZPREPASS && !ISMASKING && !ISOUTLINE && !TOOL_UNLIT
    rw_wstangent = vout.m_wsTangent.xyz;
    ...
    rw_wsnormal = vout.m_wsNormal;
#elif ISZPREPASS
    #if !ISZPREPASSALPHA && !ISALPHA
        //The motion vector generation for motion blur & temporalAA is done here
        VertexOutContext previousFrameVout = TransformVertex( victx, true );
        rw_psposition = vout._projPosition;
        rw_pspreviousposition = previousFrameVout._projPosition;
    #endif
    ...
#endif
```

So a held weapon, a default-model (rigid) draw, uses the `arkDefault` model shader, and its pass selects the permutation:

- **colour/forward** (`FORWARD_WORLD` 27, `FORWARD_DYNAMIC` 28, and any other forward colour pass): `ISZPREPASS=0`;
  `TransformVertex(victx)` uses `cb_mvpmatrixx/y/z/w` only. The compiler strips the previous variables, which is the
  current-only RDEF layout the proxy now records. This is the draw that was never placed before v1103.
- **Z prepass** (`DEPTHPASS` 17): `ISZPREPASS=1` and `!ISZPREPASSALPHA && !ISALPHA`;
  `TransformVertex(victx, true)` uses both sets and writes `rw_pspreviousposition`. This is the variant that declares both
  row sets and was the only one visible to the old proxy.

The simple `depthonly` / `coloronly` source blocks also call `TransformVertex( victx )`, but they are standalone passes that
do not export the model attributes; they are not the held weapon's material draw. The evidence for the mapping is the
source branch plus the compiled variant sets: the rigid colour signatures (normal/tangent/texcoord, 8 interpolants) have no
previous rows, and the rigid Z signatures (position + texcoord, 2 interpolants) have both when they write the previous
position. The exact material permutation (which of the 284 rigid colour variants a particular weapon uses) is not identified
without a live shader hash; the pass-independent fact the placement relies on is that its class declares current rows only.

### 7.4 Where FSR4 gets the weapon's motion

The colour VS does not write a previous position. The Z-prepass VS does (`rw_pspreviousposition`, source comment: "The
motion vector generation for motion blur & temporalAA is done here"), and the engine consumes that buffer for TAA/FSR/DLSS.
The weapon's motion vectors therefore come from `DEPTHPASS` 17, which was already placed in v1101 (current and previous rows).
The colour pass only needs its **current** rows placed; with v1103 it is placed from the same `VP * D * M` as every other view
pass, so the colour and depth geometry of the part now agree. There is no previous row in the colour shader to rewrite, and
`ObjectMotion::scatter` leaves those bytes untouched.

### 7.5 v1103 code and status

- `linux/proxy/vs_layouts.hpp`: `reflect()` now accepts a b2 `PerInstanceCB` with the current `x`, `y`, `w` rows even when
  every previous byte is absent. The previous bytes stay `0xff`, so a current-only layout is a non-zero packed word
  (e.g. `0xffffffff03020100`). `D2vrVertexShaderStats` gained a fifth value, `current_only`, after
  `created,with_previous,unstored,unhooked_devices`. The cheap gate is still one bytecode scan (`cb_mvpmatrix`) before
  `D3DReflect`; the table is 1<<16 slots. The shipped cache has 2,023 MVP vertex shaders (3.1% of the table) and the v1007
  session created 6,251 vertex shaders, so the table is not grown; `stats[2]` remains the table-full canary.
- `src/object_motion.hpp`: `prepare` already gathered the current rows separately; the new layouts now reach it. A placed
  draw with no previous rows counts `weapon_placed_current_only`; `bind_pool`/`scatter` treat a null/absent previous half as
  "nothing to rewrite". `object-motion.json` now publishes `proxy_shaders` as five values and
  `proxy_fields:"created,with_previous,unstored,unhooked_devices,current_only"`.
- `src/weapon_draw_place.hpp`: the pass-eye path accepts `prev == nullptr` and places the current rows from the publisher's
  `D`; the previous rows it computes for the pool are ignored by a current-only layout. No new hook or draw path.

Expected status after a headset session (no in-game run was made for this change):

- `object-motion.json -> proxy_shaders`: `[created, ~93, 0, 0, ~1930]`; the fifth value is the new current-only count. The
  exact first/fifth values depend on the session; the important shape is `with_previous > 0`, `current_only` in the
  thousands, and `unstored == 0`.
- `arm_flicker.weapon_draw.place.passes` (and `object-motion.json -> weapon_status`): a colour pass
  (`ARK_RENDERPASS_FORWARD_WORLD` 27 or `ARK_RENDERPASS_FORWARD_DYNAMIC` 28) must now appear with `near > 0`,
  `matched == placed > 0`; `DEPTHPASS` 17 must still show `matched == placed > 0`; the per-hand `left`/`right` counts of the
  colour pass must match the hand that draws the weapon.
- `object-motion.json` counter `weapon_placed_current_only`: non-zero and close to the colour pass's `placed` count for
  the weapon's parts (it counts placed draws whose shader had no previous rows).
- `proxy_shaders[2] == 0` over a session means the table was not the limit; if `proxy_shaders[2]` grows, the table is full
  and needs to grow (none of the 6,184 cache vertex shaders require it).

Residual risk: the cache's RDEF binds `PerInstanceCB` at `b0` while the runtime hook sees `b2`; the conclusion that the
runtime layout has `b2` rests on the existing proxy contract and the v1007 `proxy_shaders:[6251,93,0,0]` record, not on a
live reflection dump of a colour shader. No game was run for this change; the lab checks below prove the reflection encoding
and the placement arithmetic, not in-game behaviour.

## 8. Round 4: grip per weapon type, writer notes, and the gate's frame (v1104)

The v1010 status (`artifacts/claude/headset-v1010-status.json`, user dual wield + FSR4) had three symptoms: the colour pass was
placed (`FORWARD_DYNAMIC` 2566 = `DEPTHPASS` 2507) but only for the **left** physical hand (`place.passes.left` 2566, `right` 0);
the publisher counted `published [472, 0]`, `source [{engine 472, cross 0, copy 0, held 0}, {0,0,0,0}]`; and the motion/origin gate
(reason 4) rejected 14,861 + 9,071 frames while reason 5 ("no known writer made the frame") rejected 6,061 + 9,847. The user also
reported a pistol grip on a crossbow and a crossbow grip on a pistol. The game was not running for this round, so the evidence is
the session's read-only artifacts plus the game files/executable on disk and the labs; **no in-game behaviour is claimed**.

### 8.1 Finding: the kept gun clip had no weapon type

`FullBodyDriver::solve_locked` kept **one** `gun_clip_` member (`src/full_body_driver.hpp`) whenever `Tracking::slot1_gun`
was true and the left arm's attachment was calm. `Tracking::gun_clip[i]` then posed **every** gun-copy hand with that one clip
(mirrored for side 1). Nothing keyed it to the item: a pistol captured first served a crossbow copy later, and
`gun_clip_ready()` was a single global boolean. That is exactly the cross-assignment the user saw.

The game's own animation assets carry one authored left-arm idle per type (read-only list of the final patch indexes):
`models/characters/player/_animations_/{emily,corvo}/leftarm/ply_leftarm_gun_idle.md6anim` and
`.../ply_leftarm_crossbow_idle.md6anim` (also `grenade_*`, `stunmine_*`, ...). The decoded frame-0 model-space attachment positions
differ by type: Emily pistol `(0.2854, 0.0850, 0.3548)`, Emily crossbow `(0.3273, 0.0728, 0.3164)` (Corvo: `(0.2803, 0.0996,
0.3596)` / `(0.3137, 0.0743, 0.3056)`). The left arm skeleton's `left_hand_attachment_jnt` is joint 59 of 69 (the same index the
engine's `0x53d3f0`/`0x53d6a0` readers use, and the index `power_hand::attachment_joint`). The item's component type is read from
the item entity's component list (`entity+0x268`): `arkCpntWeaponGun` (`0x1b53f58`) = pistol, `arkCpntWeaponCrossbow` (`0x1b9cbf8`)
= crossbow; the decl names do not reliably carry the kind.

### 8.2 Fix: a clip per type, captured live, sampled, or the procedural last resort

- `Tracking` now carries `slot1_type` (the slot-1 item's component type) and `weapon_type[2]` (the type in each physical hand).
  `dxgi_probe` fills them from the item entity's components (`NativeWeaponHands::weapon_type`/`slot1_type`, `src/native_weapon_hands.hpp`),
  and from `HandLoadout::Extra` for mod-owned copies (pistol/guard pistol = 1, crossbow = 2).
- `FullBodyDriver` keeps `gun_clip_[3]` (calm captured, per type) and `gun_clip_first_[3]` (the first live frame of that type: a
  sampled clip of the right type, used until the calm one exists). A type change resets only the calm timer (`gun_clip_type_`).
- `clip_for_type_locked(type)` picks, in order: the calm captured clip, the first live frame, the engine's baked idle clip
  (`src/weapon_grip_clips.hpp`, generated by `tools/gen_weapon_grip_clips.py` from `ply_leftarm_{gun,crossbow}_idle.md6anim` frame 0,
  model space, unscaled). Only if none exists does the hand take the procedural grip (`weapon_grip`, `finger_mode 2`, native socket
  token cleared), and that is counted. A hand never takes another type's clip.
- `dxgi_probe`'s routing is type-aware: a copy hand uses the native arm's clip (mirrored) only when the native slot-1 gun is the
  **same component type**; a mismatched copy sets `gun_clip[s]` instead. The old `gun_clip_ready()` pre-routing (which could only
  see "some clip exists") is removed; the driver decides at solve time.
- Status per hand: `full_body.weapon_type` (`0 none, 1 pistol, 2 crossbow, 3 other`), `full_body.clip_source`
  (`0 none, 1 captured, 2 sampled, 3 fallback`), `full_body.clip_source_names`, and `full_body.clip_fallbacks`.
- Off switch: `build/legacy-single-gun-clip` restores the round-2 behaviour (one last calm clip for every type); it is read once at
  startup by `dxgi_probe` (`full_body.legacy_single_gun_clip`). The off switch is what an A/B of the per-type fix uses.

### 8.3 Finding: the writer log did arrive, but the status could not show it — and its ring/serial had defects

`source` counts a writer only **after** `solve()` and the gate accept its frame (`++weapon_draw_source[hand][out.kind]` after the gate
in `publish_weapon_draw`). The v1010 right hand had 9,071 **reason-4** frames: `solve()` had returned a `(frame, read)` pair for them,
so a note (or the `CopyDraw` fallback) was reaching the publisher. The zero `copy`/`held` entries were therefore not proof that
`WriteLog::note` never fired; they were downstream of the frame-mismatch gate (§8.4). That said, the round-2 log had real defects:

- `WriteLog::ring` was 4 per physical hand. A physical hand can get a native joint-59 note from both hooked readers (query/reader)
  and a copy note between two renders; a copy note could be overwritten before the publisher's once-per-render `writes()`.
- The publisher's reconstruction from `CopyDraw` used `copy.serial`, a **different counter** from `WriteLog::serial`. After a native
  note had been accepted, a fallback copy serial could be `<= state.serial`, so `solve()` ignored it and only the "frame did not move"
  branch could accept the copy. That is a silent one-frame-or-more stall.
- `note_copy_draw` called the sink outside `feed_m_`, so the published `CopyDraw` (read/serial) and the ring note could be torn.
- `publish_weapon_draw` checked `image_skin_slots[hand].kind`, but those slots are indexed by **engine arm side**, while the loop is
  per **physical hand**. With both arms crossed (mirrored dual wield) physical hand 1 is drawn by engine arm 0; the wrong slot was
  checked (the v1010 skip0 is small, 22/2, because both arms were image-drawn, but the mapping was wrong).

Fixes: ring 16, a bounded retry on the note try-lock plus a `write_dropped` count, `note_copy_draw` publishing the `CopyDraw` and the
ring note under `feed_m_`, the ring serial returned by the sink and stored in `CopyDraw.write_serial` (a dropped note uses
`WriteLog::reserve()`, same serial space), and `image_skin_slots[physical hand ^ 1]` when both arms are crossed. The physical side is
otherwise already explicit: `service_mirror` notes `other_side`, `service_held` notes `side`, and a crossed native read notes
`engine_side^1` (`src/native_attachment_pose.hpp`).

### 8.4 Finding: the gate compared the image's hand attachment with a differently framed read

The gate is in `publish_weapon_draw` (`src/dxgi_probe.cpp`, pre-round-4 lines 3249-3259). It compared `image_world(hand, 59)` (the
solver's hand **attachment** joint) with `out.read` (whatever the writer reported), then required ≤ 0.5 m travel, `cos(angle) >=
cos(0.5)` (0.5 rad = 28.6°, not the 60° the old note said; the gate was not loosened), and ≤ 0.6 m from the weapon's model origin.
The writers reported two different frames:

- A **native** read reported the joint the engine composes the item from. For a crossed read, `NativeAttachmentPose::around_frame`
  applies the item's mirror correction to the joint axes (`cross_attachment_`: y180 for weapons, z180 for the Heart) so the item is
  mirrored correctly. The image is the *origin hand's* attachment, with no correction. The gate therefore saw a **180°** rotation
  difference on every crossed native item (the mirrored dual-wield session's normal case) and rejected it. The right arm's sword
  also carries a y180 bind tag, but the engine applies that tag *after* the reader, so an un-crossed right-arm read was actually the
  hand joint; only the crossed reads carried the correction.
- A **copy** read reported the controller **grip** frame (`hand_pose`/`feed`, the frame `held_copy::place` composes the grip offset
  with). The image was joint 59 = grip × `grip_to_attachment` (whose rotation is one rad in the lab fixture), so every copy was
  rejected on rotation as well. This explains a copy hand's reason-4 dominance and why no copy/held source was ever counted.

The numbers fit the frame error, not the thresholds: the hand's **origin** was within the 0.5 m / 0.6 m bounds (the 26.1 mm
`engine_dev_mm` on the accepted left frames shows a correctly framed native gun), while the rotation was 180° (native crossed/tagged)
or the fixed grip→attachment rotation (copy). A world-space mismatch of that size cannot be fixed by tuning.

Fix: every write now says which frame its `read` is in (`ReadFrame {read_attachment, read_grip}`, `src/weapon_draw_place.hpp`).
`NativeAttachmentPose` reports the uncorrected origin hand joint for the read (a second `cross_joint` without the correction; the
final item joint is still passed for diagnostics). Copy writes report `read_grip`. `publish_weapon_draw` picks the matching image
(`image_world(hand, 59)` for attachment reads, `FullBodyDriver::image_grip(hand, frame)` = the image publication's solved controller
grip for copy reads) and applies the same `Place::gate_of(image, read, live)` used by the lab. The delta is still
`image · inverse(read)`, so both cases now place the weapon from the same hand frame the image is in. The gate's thresholds and the
`origin_tie`/`reach` rules are unchanged.

### 8.5 Status to read after a headset session

`xr-live-status.json -> full_body`:
`weapon_type` and `clip_source` (plus `clip_source_names`, `clip_fallbacks`) per physical hand. A crossbow copy held beside a native
pistol must show `weapon_type 2` and `clip_source` 1 or 2, never a pistol clip; a copy of a type never seen must show `clip_source 2`
(the baked idle clip) and a fallback only when `weapon_type 0/3` has no authored clip.

`arm_flicker.weapon_draw` (and `object-motion.json -> weapon_status`): `source` must now count `cross` and `copy`/`held` as well as
`engine`; `skip_reasons[hand][4]` (the gate) should fall to the level of genuinely implausible frames; `skip_reasons[hand][5]` is
the unrecorded-writer rejection (not a dropped note); `write_dropped` is the new note-contention count. `place.passes` should show
`matched == placed > 0` on **both** hands for `DEPTHPASS` and `FORWARD_DYNAMIC`, and `place.published` must be non-zero on both
hands once the copy's read frame matches its image.

### 8.6 Labs and build

- `tests/weapon_draw_place_check.cpp`: `Place::gate_of` (same-frame attachment passes; a 180° tagged read and a grip-vs-attachment
  read fail; a far hand/weapon fails) and the writer log (cross/mirror/held notes reach the per-hand ring with their read frame;
  `solve` preserves the kind; a copy note survives 8 native notes with ring 16). Output lines:
  `v1104 gate: same frame ok=1 tagged_item cos=-1.000 ok=0 copy ok=1 wrong_frame cos=0.540` and
  `v1104 writers: cross=1 mirror=1 held=2 ring=9 copy_note_after_8_native=1`; `weapon_draw_place_check OK`.
- `tests/full_body_driver_check.cpp`: captures a pistol clip (bent index) and a crossbow clip (bent ring) from the left arm, then a
  right-hand copy of each type gets its own clip (local angles 0.8999 rad apart), a never-captured type gets the baked clip
  (`clip_source 2`), and a type with no authored clip falls back to `finger_mode 2` / `clip_source 3`. Output:
  `v1104 per-type index angle 0.899879 ring angle 0.899909`; `full_body_driver_check` exits 0.
- `tests/body_ik_check.cpp` still passes the v1102 procedural-grip case and still has only the known pre-existing
  `FAIL reverse grip keeps the hilt through the fist` (since v612).
- `tests/object_motion_check.cpp` (`OK`) and `tests/native_attachment_pose_check.cpp` (`PASS: ...`) still pass with the new sink
  signature. `build\compile-v1102.cmd` gives `dll_exit=0`; no new warning from the touched files (the existing `dxgi_probe`/
  `game_xr` C445x warnings remain).

### 8.7 Open risks (not claimed fixed)

- No game or headset run was made. The gate fix is proved in the lab's arithmetic; in-game the `cross`/`copy`/`held` source counts
  and the reason-4 drop are the fields that decide whether it worked.
- The baked clip is Emily's arm idle frame 0, model space, unscaled. A Corvo avatar or a different arm scale may differ slightly; the
  live captured/sampled clip always wins when it exists. The baked clip is only the never-captured fallback.
- The per-type key is the **component type**, not the exact decl/model. All pistols (player, guard, Corvo/Emily) share one pistol
  clip; a weapon whose authored grip differs inside a component class would still take the class clip. The task allowed this key;
  the decl/model key is a possible refinement.
- A copy's `image_grip` is the controller grip the copy was placed from, but a hand posed by a kept/sampled authored socket may point
  its fingers differently; the copy still follows the controller by construction (the round-2 placement), not the authored socket.
- The `solve()` prediction still assumes the item's offset from the read frame is fixed for the life of a pair. A cross correction
  change (weapon ↔ Heart) on the same model identity can therefore reject a few frames until the identity/generation changes; the
  fallback and the next matched read recover it (counted as reason 5, not misplaced).
- The `origin_gap <= 0.6 m` test is a weapon-model-origin test. A future item whose model origin is farther than 0.6 m from its
  attachment (a long sword at some offsets) would be rejected; no threshold was changed.
- The ring's 16 entries and the 8-try note lock are bounded, not proved sufficient under every scheduler. `write_dropped` is the
  canary; a dropped copy note is reconstructed from `CopyDraw` with a reserved serial, so it is ordered but one frame of copy motion
  may be lost.

## 9. Round 5: the crossed weapon's arm and the weapon type (v1105)

The v1012 headset status (`artifacts/claude/headset-v1012-status.json`, dual wield + FSR4, `melee_hand 0`) had two open symptoms:
`arm_flicker.weapon_draw.source` counted only `engine` on both hands (`cross` 0), every skip was the gate (`skip_reasons` reason 4 on
8336 + 4936 frames, no reason 5), `published [7829, 954]`, `dual_wield.cross [13598, 600, 6862, 277.243, 1, 1]` with no copies at all
(both weapons native and crossed), and `full_body.weapon_type`/`clip_source` were `[0,0]`. The game was not running for this round; the
evidence below is the session's read-only status, the source, the executable on disk and the labs. **No in-game behaviour is claimed.**

### 9.1 Which notes a crossed physical hand gets, and which one won

With the arm-joint cross both arms are crossed (`set_cross_mask(mirrored?3u:0u)` at `src/dxgi_probe.cpp` when `melee_hand==0`,
`src/dxgi_probe.cpp:12117`): bit 0 = the left arm's slot-1 item (the gun) rides the RIGHT physical hand, bit 1 = the right arm's item
(the sword) rides the LEFT one. A physical hand's writer ring therefore holds notes of two different arms:

| Note | Writer | `physical` | `engine_side` | `kind` | read |
|---|---|---|---|---|---|
| cross note of the arm the hand carries | the crossed read in `NativeAttachmentPose::around_frame` (`src/native_attachment_pose.hpp:312-321`) → `weapon_native_write` (`src/dxgi_probe.cpp:3195-3206`) | the hand the item rides (`engine_side^1`) | the slot's arm | `cross` | the ORIGIN hand joint (the frame the cross composed the item from, tag undone) |
| that arm's own engine note | the same sink, for a read the cross did not apply to (cross declined/failed, or the arm not crossed) | `engine_side` | the slot's arm | `engine` | that arm's OWN joint, i.e. the other physical hand |
| the arm the hand does not carry, its engine note | as above | that arm's own hand | its arm | `engine` | — |

The two notes of one hand share (nearly) the same `read` — a joint-59 hand pose — but name different `engine_side`: for physical left
hand 0 they are the sword (arm 1, position exact, it rides hand 0) and the gun (arm 0, whose frame is at hand 1). Round 4 chose the
model with `WriteLog::native_engine_side()` — the `engine_side` of the **newest** note in the ring — and only then read that model's
live frame and filtered the candidates by that `engine_side` (`src/dxgi_probe.cpp`, pre-round-5 lines 3218-3242). Note recency is not
the cross state: when the newest note is an `engine` note, the publisher read and paired the OTHER arm's slot model, whose live frame
is at the other hand. `WriteLog::solve` accepts a first binding whenever the read is within 0.8 m of the live frame
(`src/weapon_draw_place.hpp:192-193`) and the two hands are usually closer than that, and `accept` copies the kind of that write
(`:262-266`) — so `source` counted `engine`, and the cross note was filtered out of the very set `solve` was given. The same wrong model
is what the gate then measured: its origin is at the other hand, so `origin_gap` is the hand separation — the reason-4 rejection, and
below 0.6 m the wrong weapon was published as this hand's.

This is the code-level answer to "why the cross note never becomes the source": it is a candidate only when
`native_engine_side()` happens to return the cross arm, i.e. only when the engine note is older; and even then the pair is the right
one by luck, not by construction. Two status readings fit this and neither contradicts it: `placed_dev_mm` 0.0005 says the accepted
pairs were rigid (the weapon really held by its read hand), so the publisher alternated with note recency — the frames it got right are
the published ones, the frames it got wrong are the reason-4 rejects; and the `source` kind names the kind of the accepted write, not the
arm, so it cannot separate the two notes. The next run's `source.cross`, `no_writer` and `gate_rejected` (§9.4) are the fields that
decide which of the two happened on a rejected frame; nothing about the v1012 frames is claimed beyond the code path.

### 9.2 Fix: the cross mask names the arm; only its own kind can have placed the item

`WriteLog::native_route(physical, cross_mask)` (`src/weapon_draw_place.hpp:234`) derives, from the cross mask, the arm whose slot item
a physical hand carries and the only writer kind that can have placed it: bit `physical^1` set → that arm, `kind=cross`; bit `physical`
clear → its own arm, `kind=engine`; otherwise the hand carries no native item (`arm = -1`, a hand whose item crosses away and nothing
crosses in). `WriteLog::native_candidates()` (`:245`) returns only the writes with that `engine_side` **and** that `kind` (age-filtered),
in chronological order. `publish_weapon_draw` (`src/dxgi_probe.cpp:3245-3272`) uses that route for the live model, the identity and the
candidates, so:

- a crossed hand is paired with the cross note's origin-hand read, the frame the cross composed the item from, and its image is
  `image_world(physical, 59)` — one frame for the gate;
- an arm's engine note is never a candidate for either physical hand;
- a crossed hand with no cross note publishes nothing (`no_writer`, §9.4) instead of the other hand's weapon;
- the native fallback (`weapon_hand_read`, the v935 read) is only used for `kind=engine` (an un-crossed arm), and now takes a
  `WriteLog::reserve()` serial: it used `weapon_hand_read_serial`, a read counter in a different number space, so it could never be
  newer than the accepted state and only the "frame did not move" branch ran (`src/dxgi_probe.cpp:3284-3290`).

`solve()` and `Place`/`gate_of` are unchanged: with the right candidate the read is the hand that holds the item, `delta = image ·
inverse(read)` is the physical hand's correction, and the gate's tolerances (0.5 m travel, 0.5 rad, 0.6 m origin gap) still apply.

### 9.3 Weapon type: the item record's component+declaration pair

The type key `full_body.weapon_type` (0 none, 1 pistol, 2 crossbow, 3 other) is used for the per-type grip clip and the status. Round 4
derived it from `NativeWeaponHands::weapon_kind()`, a scan of the weapon entity's component list (`entity+0x268`) that compared only the
component vtable, and it read it through `NativeWeaponHands::slot1_type()`, a **second** full player-graph walk (`player_arms`) called
from the Tracking build, whose 0 was indistinguishable from "no weapon". The v1012 artifact was captured with `gameplay:false`,
`logic_mask:12`, so its `[0,0]` also depends on the staleness path: `t.weapon_type` is gated on `fresh` (`hand_state_tick` within
300 ms) and the driver overwrote `diag_.weapon_type` with 0 on every stale frame.

The RTTI addresses the mod uses are the exe's: `artifacts/round5/prove_weapon_rtti.py` walks the PE on disk, finds the RTTI
TypeDescriptor for `.?AVarkCpntWeaponGun@@` at RVA 0x2304c50 (and Crossbow at 0x23164d0), the CompleteObjectLocator that names it
(0x1f9ac38 / 0x1fbfe28) and the vtable whose slot [-1] points at that COL:

```
arkCpntWeaponGun:      COL rva=0x1f9ac38  -> vtable rva=0x1b53f58   mod rva=0x1b53f58 -> MATCH
arkCpntWeaponCrossbow: COL rva=0x1fbfe28  -> vtable rva=0x1b9cbf8   mod rva=0x1b9cbf8 -> MATCH
```

(Full output in `artifacts/round5/weapon-rtti.txt`.) Since the addresses are right, the defect is in the read path: the component and
its declaration are one pair in the game's own equip path (`arkCpntWeaponGun` + decl 0x1b53e18, `arkCpntWeaponCrossbow` + decl
0x1b9cab8; the path that produced 1670 left-weapon bind receipts in the same v1012 session), and the type must come from the equipped
inventory **item record** (`*item+0x10` component, `*item+0x18` declaration), not from a list scan that can match a component of
another class. Fix:

- `NativeWeaponHands::item_kind` / `item_type` (`src/native_weapon_hands.hpp:1003`, `:1205`) read the pair and require the component
  vtable **and** its class's declaration vtable (the `item_kind` classifier is private; `item_type` is the public 1/2/0 form).
- `dxgi_probe` computes the slot-1 type once per control tick from the verified `native_hand_state` snapshot (`state->slots[0]`, the
  left/gun slot) into `slot1_component_type` (`src/dxgi_probe.cpp:10915`) and the Tracking build reads that latch
  (`:8268`) instead of walking the graph again. `Tracking::hand_state_fresh` (`src/full_body_driver.hpp:130`) makes the driver keep
  the last in-gameplay type/clip value instead of reporting 0 for a menu/load frame (`:845-855`).
- `full_body.weapon_type` is therefore the type of the item each PHYSICAL hand holds: a pistol in the crossed right hand is
  `[3,1]`, a crossbow `[3,2]`, the native arrangement `[3,1]`/`[1,3]`; `clip_source` stays 0 with no copies (it names the clip a
  mod-owned copy uses, and the v1012 session had none).

### 9.4 Status to read after a headset session

`arm_flicker.weapon_draw` gains, per physical hand:

- `gate_rejected`: `{n, window, hand_moved_m:{p50,p90,max}, angle_deg:{...}, origin_gap_m:{...}, kinds:{engine,cross,copy,held},
  read_frames:{attachment,grip,cross_origin}}` — the gate inputs of the rejected frames in a rolling window of the last 256 rejects
  (`GateRejectLog`, `src/weapon_draw_place.hpp:484`). A `cross`/`copy` kind with a large `origin_gap_m` is a pairing error; a large
  `hand_moved_m` is a genuinely implausible frame; a 180° `angle_deg` is a mixed read/image frame (the round-4 failure).
- `no_writer`: native frames where no note of the arm/kind the cross state requires existed. With the fix, a crossed hand reports
  `source.cross > 0`; if `no_writer` grows instead, the cross note is not reaching the ring for joint 59 (the open risk in §9.6).

What a correct crossed session looks like: `source` counts `cross` on both hands, `gate_rejected.n` small and `hand_moved_m` near the
hand's real motion, `no_writer` near 0, and `place.passes` keeps `matched == placed > 0` on both hands for `DEPTHPASS` and
`FORWARD_DYNAMIC`. `full_body.weapon_type` is `[3,1]`/`[3,2]` for a pistol/crossbow in the crossed right hand.

### 9.5 Labs and build

- `tests/weapon_draw_place_check.cpp`: `native_route` for masks 0/3/1/2; one mirrored dual-wield frame with a sword crossed into the
  left hand and a gun into the right, **both** kinds of note for each hand (cross + the arm's own engine note, the engine note newest);
  asserts the old `native_engine_side` picks the other arm (the v1012 failure), the new selection picks the cross note, the solved read
  is the origin hand joint, the gate accepts, the engine note alone yields no candidate, and the two draws are placed on their own
  physical sides with their own `delta`; plus the `GateRejectLog` p50/p90/max/kind/read-frame window. Output:
  `v1105 crossed native: sword side=0 gun side=1 cross_pairs=[1,1] engine_note_alone=0 gate_ok=[1,1]` and
  `v1105 gate window: n=5 travel_p50/p90/max=0.0500/0.0900/0.0900 m angle=180.0 deg gap=0.0346 m kinds[cross]=5 read[cross_origin]=5`;
  `weapon_draw_place_check OK`.
- `tests/full_body_driver_check.cpp`: `item_type` classifies a pistol item record as 1, a crossbow record as 2, and refuses a component
  whose declaration belongs to the other class (0). Output: `v1105 item component type: pistol=1 crossbow=2 mismatched_decl=0`;
  `PASS: ...` (the v1104 per-type clip lines unchanged).
- `tests/native_attachment_pose_check.cpp` `PASS`, `tests/object_motion_check.cpp` `OK`, `tests/native_left_weapon_socket_check.cpp`
  `PASS` (the component RTTI path it shares), `tests/hand_loadout_check.cpp` `ok`.
- `tests/body_ik_check.cpp` still has only the known pre-existing `FAIL reverse grip keeps the hilt through the fist` (1 failure).
- `build\compile-v1103.cmd` gives `dll_exit=0`. No new warning from the touched files (the existing `dxgi_probe`/`game_xr` C445x,
  `native_weapon_hands` C4127 and `hold_coupling` C4267 warnings remain). One pre-existing `/W4` C4267 in `src/native_hand_contact.hpp`
  (`std::min<unsigned>` against a `size_t`) is fixed with a cast because the lab that now includes `native_weapon_hands.hpp` is built
  with `/WX`; it is behaviour-preserving.

### 9.6 Open risks (not claimed fixed)

- No game or headset run was made. The fix is proved in the lab's arithmetic and the exe's RTTI; the next status fields (§9.4) decide
  whether it worked in game.
- If the cross note never arrives for joint 59 at all (the cross declined for every weapon read), a crossed hand now publishes
  nothing and the weapon keeps the engine's tick frame: the lag remains, but the wrong hand is never paired. `no_writer` vs
  `source.cross` is the field that decides.
- The cross note's read is the origin arm's joint re-expressed through the two arms' PUBLISHED model frames. When the live frames
  differ (`dual_wield.cross` `cross_skewed`/peak, 277 mm in v1012) the world read is off by that difference; it is exactly the frame
  the engine composed the item from only when the frames agree. Not changed this round.
- The gate tolerances (0.5 m, 0.5 rad, 0.6 m) are unchanged; a genuinely fast hand can still be rejected (counted, never misplaced).
- `Tracking::hand_state_fresh` keeps the last type/clip through a menu; a real weapon-type change is reported on the first fresh frame.
- The item-record pair test relies on the declaration vtables 0x1b53e18/0x1b9cab8, which the left-weapon socket receipts exercise live
  (1670 in v1012), not on a live type dump of the equipped item.



## 10. Round 6: rigid binding (v1106)

The v1014 status (`artifacts/claude/headset-v1014-status.json`, user dual wield + FSR4) still showed the two visual symptoms:
`arm_flicker.weapon_draw.skipped [33109, 28229]` of about 47k frames per hand, split between the gate (reason 4:
14939 + 7270) and "no known writer made the frame" (reason 5: 16960 + 19891), and `full_body.weapon_type [0,0]` /
`clip_source [0,0]` all session. The game was **not running** for this round. All evidence below is the read-only session
artifact, the executable's structures already proved in earlier rounds, and the labs; **no in-game behaviour is claimed**.

### 10.1 The design failure, restated

The old placement was a per-frame correction of the engine's tick frame F:

```
D = image_hand * inverse(writer_read)       # only on frames with a valid pair + gate
cur' = VP * D * F * M
```

Every frame without a valid pair/gate fell back to the engine's tick rows. That is not a slower placement; it is a *mix*
of two different weapon transforms across passes and frames. Depth/motion came from one subset of passes and colour from
another, which is exactly what FSR4/TAA reject as a black flicker, and a sequence of placed/unplaced frames reads as
skipped frames. The pair/gate is a **validation of F against a hand read**, not a binding: it has to succeed again every
frame, so its failure rate becomes the weapon's visible frame rate.

The round-6 contract is instead: the weapon is rigidly bound to the drawn hand by one offset G, learned once per
(weapon, hand, grip), and then placed in **every** eye pass of **every** render from the hand that render actually draws.

### 10.2 Spaces: every quantity, and why the units agree

All matrices are row-major with column vectors; `image_world`, `image_grip` and the engine's model frame all use the
game's world frame (metres, Z up). `weapon_draw::weapon_mat` turns a `fullbody::Xf` into the column-vector `Mat` used
here (columns = rotated forward/left/up, last column = position); `model_of` turns `model+0x1860` (origin, three axis
rows, three scales) into the same layout.

| Quantity | Producer / storage | Space | Frame |
|---|---|---|---|
| `FullBodyDriver::image_world(side,59,frame)` | solver output `image_out_.arm[side][59]`, read at `begin_image` | world, metres | the physical hand ATTACHMENT joint 59 this render draws |
| `FullBodyDriver::image_grip(side,frame)` | `solved_track_.grip[side]`, the same publication's solved controller grip | world, metres | the controller GRIP the copy service places from |
| native writer read | `weapon_native_write` (`src/dxgi_probe.cpp`): `to_world(frame_of(arm_model+0x1860), to_xf(hand))`, `hand` is the palette joint after `advance_attachment` but BEFORE the item tag | world, metres | the exact joint the engine composed the item from |
| crossed writer read | `NativeAttachmentPose::cross_pair`: the origin arm's joint re-expressed in the destination arm's PUBLISHED model frame, with **no** y180 correction | world, metres | the physical (origin) hand joint the crossed item rides |
| crossed item joint | same one composition, WITH `power_hand::mirror_correction` (y180 for a weapon tag) | world, metres | the joint written into the destination palette; the engine then composes the item from it |
| copy feed / `hand_pose` | `NativeWeaponHands::hand_pose`: the drawn `WorldTrackedPose` in game world; `held_copy::to_world(hand,offset)` for the entity | world, metres | controller grip the copy was placed from |
| copy applied offset | `note_copy_draw` (v1106): the exact local `held_copy::Pose` passed to `place`, already mirrored for the other hand | local to the grip; axes forward/left/up | the copy's grip offset |
| engine F | model `+0x1860` (15 floats), `model_of` | world, metres | weapon model frame the engine draws |
| `VP(eye)` | `Views` from the eye records (view+0x7510 z row storage) | clip = VP * world | eye view-projection of the render |

`G` is always in world axes. Only the hand frame it is relative to differs: `joint 59` for a native/crossed item,
the controller `grip` for a teleported copy. That is not an ambiguity in the world space; it is the frame the writer
actually composed from. Both are converted to the same `Mat` by `weapon_mat`.

The binding is then:

```
G = inverse(H_read) * F                                  (native and crossed; read is the joint above)
G = the exact applied offset (mirror(offset) for a cross-hand copy)
B = H_image * G                                          (H_image = image_world(59) or image_grip, matching G's space)
cur'  = VP(eye) * B * inverse(F) * M
prev' = VP_prev(eye) * B_prev * inverse(F) * M
```

`M` is the draw's own model matrix in F's frame (`M = inverse(F) * VP^-1 * cur`); it is the same for every pass of
that part. `B_prev` is the previous render's `B` (the previous drawn hand * the same G), kept per hand and per eye.

### 10.3 The two v1014 rejection numbers, explained

**The 16.3 m copy read (hand 0).** In v1014 hand 0's rejects were `kinds.held 14932`, `read_frames.grip 14932`,
`hand_moved_m p50 16.305840, p90 17.481312, max 17.514313`, while `origin_gap_m` (engine F origin vs the writer's read)
was only **0.030757 m**. The read and the copy's F agreed with each other, so the copy had been placed consistently — but
both were in the previous world, 16 m from the current image hand. The copy service reads `feed_[side]`, the last drawn
hand published on the render thread, and in `NativeWeaponHands::service_held` (pre-v1106) it composed that stale hand
with the copy offset around the current `now_root`; when the feed still described the previous world after a level
load/teleport, the guard `length(moved) < 1 m` skipped the translation but the stale feed was still reported and used as
the placement's read. The current world's coordinates in the same artifact are around `[-6.38,-2.62,-15.08]`
(|.| = 16.56 m), which brackets the 16.3-17.5 m reject distribution. The gate compared that old-world copy/hand with the
current image hand, which is the "different space" the user saw. Fix: `service_held` now drops a feed whose recorded
root is >= 1 m from the current root (`held_rejected`, copy hidden for that frame), and the binding learns a copy's G
from the exact applied offset, never from `inverse(read) * F`.

**The ~180 deg crossed read (hand 1).** In v1014 hand 1's rejects were `kinds.cross 7018`,
`read_frames.cross_origin 7018`, `hand_moved_m p50 0.029617`, `angle_deg p50 175.184082, p90 179.865326,
max 180.000000`, `origin_gap_m p50 0.000000`. The origins agreed; the orientations differed by exactly the item's y180
cross correction. `NativeAttachmentPose::around_frame` built the read by calling `cross_joint` a second time with no
correction; that second call checks the same admission/source conditions as the first and can fail (or race) even
though the first succeeded. When it did, `hand` stayed `frame.tracked`, i.e. the *corrected* joint, and only that side
of the comparison carried y180. Fix: `power_hand::cross_joint_both` computes both the corrected and the uncorrected
joint from one composition and one validity check; `around_frame` stores the uncorrected result in the same call and
the sink uses it. The lab's crossed case shows the correction is inside G (its G-vs-image angle is >2.5 rad), so the
placed weapon is the mirrored hold without the read ever being compared to a differently framed image.

### 10.4 Binding implementation

`src/weapon_draw_place.hpp` now has `Binder` (per physical hand) and the draw-time `Place` consumes a bound frame:

- `candidate_from_read(read, F)` -> `G = inverse(read) * F`; `candidate_from_offset(offset)` -> `G = offset`.
- `valid_grip` requires finite entries, `|G.translation| < 0.3 m`, column norms in `[0.98,1.02]`, pairwise column dots
  `<= 0.02`, determinant `> 0.98`. An invalid candidate is counted (`rejects`) and never becomes G.
- Learning: a new identity/source/space (equip, weapon swap, cross-state change) drops the old G. A native/crossed G
  needs **two consistent** compositions (a writer note can be newer than the F the engine composed); a copy's exact
  offset binds on the **first** placement. Until then the publisher passes no bound frame: the engine's rows stay and
  the frame is counted (`fallbacks`, skip reason 3/4).
- Keeping/reuse: the same identity/source/space keeps G. A different composition (a regrip, or a changed copy offset)
  must agree for **three** consecutive observations before G is replaced (`refreshes`); read skew on one frame never
  moves the weapon. G age is in renders since learn/refresh.
- Placement: `B = H_image * G`; `Place::publish_bound` stores F and B; `Place::place` matches the draw to F as before
  and emits `VP * B * inverse(F) * M`. `Side.prev_eye[eye]` is the previous render's B per eye; previous rows are
  `VP_prev(eye) * B_prev_eye * inverse(F) * M`. Colour/current-only layouts take the same current rows; depth and
  colour therefore agree bit for bit for the same part.
- Eye/pass: the existing render-pass map still names the eye exactly, so a bound weapon is placed in every eye pass of
  every render with no cache certificate and no per-frame writer read. Shadow/light passes whose view is not an eye
  view are still left at the engine's rows (they are not part of the eye's temporal history).
- Diagnostics only: `Binder::note_sanity(F, B)` records the engine-vs-bound origin distance and angle; it never gates.

`WriteLog` still records the writers' exact hand poses and is the *learning source* for native/crossed G; it is not
consulted for a frame that already has a valid G. `Solved`/`solve`/`gate_of`/`GateRejectLog` are removed from the
placement path (and the old header's gate window is replaced by the status fields in §10.6).

### 10.5 Weapon type: why v1014 was [0,0], and the fix

The v1014 artifact was captured with `gameplay:false` and `logic_mask:12` (a load/menu snapshot). The
`full_body.weapon_type`/`clip_source` diagnostic is latched by `FullBodyDriver::solve_locked` only on a frame whose
`tracking.hand_state_fresh` is true. Two code paths can produce `[0,0]`: (a) no fresh hand state, so the latch had
nothing to store; (b) a fresh state whose item record did not type. The artifact's capture context makes (a) sufficient;
it does **not** prove (b), and the item-record offsets are the right ones: the inventory item pointer's `+0x10` component
and `+0x18` declaration are exactly the pair `native_left_weapon_socket.hpp:104-110` validates live against
`arkCpntWeaponGun`/`arkCpntWeaponCrossbow` and their declaration vtables (RTTI proof in
`artifacts/round5/weapon-rtti.txt`). The remaining risk in game is an equip/load frame where the record pair is
transiently unreadable; v1106 therefore removes the ambiguity and adds:

- `NativeWeaponHands::item_type`: the record pair first, then a fallback to the weapon entity's component list
  (`weapon_kind`, the round-4 path the barrel aim uses live) only when the record pair does not type; a record that
  types is never overridden.
- `weapon_live_types()` in `dxgi_probe`: the equipped item record's type read directly per physical hand, independent
  of the control-tick latch, exposed as `weapon_draw.weapon_type_live`.
- The copy-hand type is taken from the loadout (`extra_type`) even when the native hand snapshot is stale, and the
  gun-hand type uses the control-tick latch or the live read. `Tracking::weapon_type_fresh` lets the driver update the
  status from either source, while still keeping the last in-gameplay value through a menu.
- Status to read after a headset session: `full_body.weapon_type` and `weapon_draw.weapon_type_live` must both name the
  equipped item (1 pistol, 2 crossbow) in gameplay; if `weapon_type_live` is non-zero while `full_body.weapon_type`
  stays 0, the driver's freshness/latch is the remaining fault, not the item-record offsets.

### 10.6 Status fields (v1106)

`arm_flicker.weapon_draw` keeps `published`, `skipped`, `frames`, `skip_reasons` (0 not an image hand, 1 no image,
2 engine frame unreadable, 3 no valid G yet, 4 invalid G candidate, 5 copy identity changed), `source` (placed frames
by G source), `no_writer`, `write_dropped`, `engine_dev_mm`, `placed_dev_mm`, and adds:

```json
"binding":[
  {"bound":1,"source":"engine|cross|mirror|held","space":"joint59|grip","age":12,"g_m":0.034,"pending":0,
   "learns":1,"refreshes":0,"rejects":0,"fallbacks":0,
   "sanity":{"n":900,"gap_mm":17.2,"angle_deg":1.9,"max_mm":31.0}},
  {"bound":0,...}
],
"weapon_type_live":[1,2]
```

`bound=1` with `fallbacks` near zero is the target: every render with an image hand and a bound weapon is placed.
`sanity.gap_mm` is report-only; a large value is the engine's tick lag, not a placement rejection. `place.passes`
still gives `matched/placed/left/right` per render pass, so "placed per pass" is visible per hand.

### 10.7 Labs and build

- `tests/weapon_draw_place_check.cpp` rewritten for the binding. Output:
  `v1106 native: frames=40 eye_passes=80 learns=1 refreshes=0 worst=4.32e-06 bound=1`;
  `v1106 previous rows: checked=7 worst=3.76e-06 motion=1.73e-02 (the previous drawn hand, not the engine tick)`;
  `v1106 crossed: source=cross space=joint59 G-y180-angle=160.4 deg bound=1`;
  `v1106 copy: offset-grip bound=1 kept_frames=10 learns=1 space=grip`;
  `v1106 two weapons: left=own right=own separation=0.566 m`;
  `v1106 refresh: regrip_refreshes=1 swap_learns=2`;
  `v1106 invalid G: rejected=1 fallback_frames=1 bound_after_valid=1`;
  `v1106 depth/colour identical=1` (the printed placement cost is ~0.13-0.15 us); `weapon_draw_place_check OK`.
- `tests/object_motion_check.cpp`: `current-only layout 0xffffffff03020100 previous=0 b3=0`, `object_motion_check OK`.
- `tests/native_attachment_pose_check.cpp`: PASS (the cross path now consumes one composition; no new failure).
- `tests/native_power_hand_check.cpp`: `native power hand check passed` (the unchanged `cross_joint` wrapper plus the
  new `cross_joint_both` produce the same corrected transform).
- `tests/full_body_driver_check.cpp`: `v1104 per-type index angle 0.899879 ring angle 0.899909`,
  `v1105 item component type: pistol=1 crossbow=2 mismatched_decl=0`, PASS.
- `tests/body_ik_check.cpp`: still only the known pre-existing `FAIL reverse grip keeps the hilt through the fist`
  (1 failure, since v612).
- `build\compile-v1104.cmd`: `dll_exit=0`. No new warning from the touched files. The warnings in the log remain the
  existing `dxgi_probe` C445x, `native_weapon_hands` C4127 and `hold_coupling` C4267.

### 10.8 Open risks (not claimed fixed)

- No game or headset run was made. The binding is proved in the lab's arithmetic and the session artifact; the status
  fields above are what the next run must show. `binding[hand].fallbacks` (not the sanity gap) is the field that says
  whether a weapon was ever unplaced.
- A copy's G is the exact offset the service applied. If a copy def's drawn model has a constant local offset from the
  entity frame (the old `mirror_err_mm` diagnostic), `H_grip * offset` places the entity frame; the model part may need
  that constant added. The copy defs used in v1014 report `mirror_err_mm -1` (no still sample), so this is unproven.
- A native/crossed G is learned from the newest applicable writer note. If that note is consistently newer than the
  engine's composition, the two-composition confirmation prevents a one-frame skew from binding, but a persistently
  mis-ordered writer could still bind a wrong G; the sanity gap would be large and `placed_dev_mm` would grow.
- A regrip is detected by G disagreement, not by an explicit engine event. A regrip that changes G by less than the
  `same_grip` thresholds (2 cm / 5 deg) is not refreshed; a genuinely different grip below that is visually small.
- The hand frame for a copy is the solver's controller grip (`image_grip`), while a native/crossed item uses joint 59.
  A future copy whose placement should follow the authored arm socket instead of the controller would need a
  grip-to-attachment conversion; the current copy service explicitly places from the controller.
- `cross_joint_both` assumes the same admission/source conditions as `cross_joint`; the cross path is still skipped when
  the receipt protocols say the arm is not published. That leaves the engine's rows for those frames (counted as skip 3).
- `weapon_type_live` reads the item record on the status thread. A stale record during an equip event may show the
  previous type for one status sample; the per-frame driver latch and the next sample correct it.

### Round 6 coordinator review (v1016)

- The worker's "stale previous world" reading of the v1014 16 m copy rejections does not hold: `feed()` only returns a hand younger
  than 0.25 s, yet ~15k frames were rejected. The `gate_rejected` window is the last 256 rejections and the status was written at
  `gameplay:false` (menu/load, `menu_position` near the origin). `origin_gap` was a constant 30.8 mm (= |offset|), so the copy read and
  its live frame were both world; `image_grip` (the raw tracked grip, `solved_track_.grip`) was the side 16 m away. The root-jump
  feed drop the worker added is kept as a harmless teleport guard.
- Copies are now bound to the DRAWN hand, like native weapons: H = image_world(hand, 59) * inverse(grip_to_attachment), the grip the
  drawn joint 59 stands for (`attachment_target(side, identity)` = grip_to_attachment). This keeps a copy in the drawn hand when the
  arm's reach clamps and never mixes in the raw grip's space. Native and copy hands now use the same H.
