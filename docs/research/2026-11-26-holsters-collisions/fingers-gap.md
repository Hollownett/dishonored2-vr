# Fingers on surfaces: our G5 path vs the reference VR mod

Read-only research, 2026-11-26. Nothing was built or run. Reference mod source (MIT, read only): a local checkout of the reference mod
(the reference sources). Ours: `src/finger_ik.hpp`, `src/finger_probe_cache.hpp`, `src/dxgi_probe.cpp`.

## 1. What our fingers do today

### 1.1 Pipeline (free hand, knob `build/no-finger-surface` absent)

1. **Probes** (`dxgi_probe.cpp:12249-12256`). Per hand per frame: the palm-normal cast every frame (slot 3, 8 cm, radius 3 cm, `free_palm_reach`
   `finger_ik.hpp:1517`) and `finger_probe_casts-1 = 1` fingertip cast (slots 4-8, `finger_probe_casts_default=2` at `dxgi_probe.cpp:607`).
   The finger is chosen round-robin `(probe_frame*(casts-1)+k)%5`, so each finger is re-cast once every 5 frames (about 70 ms at 72 Hz).
   The cast is `finger_sweep` along the CURRENT drawn angles, knuckle to 2 cm past the tip, radius 1.2 cm (`finger_ik.hpp:1506-1515`).
   `finger_drawn_angles` is zero until the first solve, so the first casts are straight.
2. **Cache** (`finger_probe_cache.hpp:20-37`). A slot's answer is a shape for 150 ms (`finger_probe_hit_life_ms`) and survives ONE miss
   (second miss in a row drops it; the palm slot is time-based, 140 ms). Answers are consumed once by serial (`dxgi_probe.cpp:12232-12243`).
3. **Collider** (`finger_collider`, `dxgi_probe.cpp:7108-7130`). A world hit becomes `slab_shape(point,normal)`: a **24 x 24 cm plane**, 2 cm
   half thick, centred on the hit (`finger_ik.hpp:1487-1492`). A prop under 1 m with a smallest axis under 2 cm (`thin_prop_axis`) becomes an oriented
   slab from its bounds (`held_slab`); other props go through `classify_held` (box/cylinder fit). Up to 8 shapes (`max_contact_shapes`), the
   cached shapes appended after de-dup by 1 micrometre (`:12244-12248`), so near-identical slabs are NOT merged.
4. **Solve** (`finger_contact`, `finger_ik.hpp:595-950`, args `ease_opening=true`, `stop_extended=true`, `budget=5`; `dxgi_probe.cpp:12337`).
   - Each finger closes along its own path in three stages: knuckle until the proximal phalanx touches, then PIP (DIP follows at `dip_per_pip=80/110`,
     `:27`), then DIP alone until the tip touches (`:446-452`, `:660-695`). Skin test = three HIGGS arcs (tip/outer/inner) per phalanx; contact
     tolerance 4 mm (`contact_tolerance`, `:457`).
   - Thumb starts open (`finger_open_degrees` CMC -45, MCP -5, IP -15, `:26`) and closes as one across the palm; it also sees the folded index and
     middle phalanges as shapes and a clear-of-the-hand search (`thumb_nearest_clear` 7x7x7 grid, `:414`).
   - `stop_extended` (`:702-748`), long fingers: if the straight pose is buried more than 1 mm, bisect (6 steps + 4 refine) for the least curl along the
     path that is out (to 0.5 mm), else open the PIP/DIP by up to 15 degrees of hyperextension (`finger_hyperextension_degrees`, `:95`), else stop
     blocked at 15 degrees (`stuck_frames`). A finger that lifted last frame prefers lifting again (`state.hyperextended`, `:731`): no flip-flop.
     If the straight pose is out but the contact pose is in, it opens back toward straight (5-step bisect).
   - Memo: a finger whose shapes (in palm frame, 2 mm / 0.01 basis, `memo_length_tolerance`) are unchanged keeps its answer; others solve in turn,
     `budget` per call; a hand faster than 0.3 m/s is capped at 3 solves a frame (`finger_free_budget`, `:478-479`); stale fingers keep their last pose.
5. **Wants** (`finger_wants`/`compose_finger_wants`, `finger_wants_stack.hpp`): controller touch curl; a free long finger draws at least
   `relaxed_share=.3` (`:507`) or the resting curl `.35` when the palm skin is within 2-3 cm of a shape and slower than 0.15-0.3 m/s (`:115-116`,
   `:917-923`); the contact caps it (`grip_curls`, `finger_drawn_pose :957-1004`). Thumb want is capped at `thumb_open_cap .5` unless the index/middle
   fold (`:519-533`).
6. **Smoothing, two layers.** (a) Contact pose (`:925-942`): rising (closing) follows tau 0.06 s; falling (finger must open) is immediate for a hold, but with
   `ease_opening` (always on for a free hand) it eases with tau 0.06 and lags the target by at most 5 mm of fingertip (15 mm if the hand is >= 0.3 m/s,
   `finger_open_lag :1047`, `dxgi_probe.cpp:12335`). `min_curl` rises at once, falls with tau. (b) `FingerPoseFilter` (`:971-985`): a critically damped spring,
   `finger_pose_smooth_time 0.07 s`, per joint, applied to the FINAL turns AFTER the contact clamp (`dxgi_probe.cpp:12350`).
7. **Drawn.** `finger_turn[side]` -> `body_ik.hpp` `Inputs::finger_turn/has_finger` (`:165-168`; range check `:407`) with the same hinge axes
   (`hinge_axes`, `finger_ik.hpp:265`, `Inputs::finger_axis :171`), so the pose solved is the pose drawn; used only when `full_body_requested &&
   tracked_arms_requested` (`dxgi_probe.cpp:12445`).
8. **Metrics** (`dxgi_probe.cpp:12356-12372`, status `finger_ik.tip_pen`): per hand, frames with a probe shape within 3 cm of a tip, frames deeper than
   5 mm (15 mm fast), deepest slow/fast (mm), thumb deepest, palm deepest, blocked/hyper frames; also `blocked`, `hyperextended`, `stuck`, `probe_casts`,
   `posts`, `hits`, `probe_hits_world/thin`, `solve_us`. Existing sim: `tools/linux/scenarios/hand_on_table.py` (press 3 cm into a desk, hold 2 s, slide
   +-12 cm at 0.1 then 0.6 m/s, lift). Host checks: `tests/finger_ik_check.cpp` (table/slab cases ~362-375, 661-681, 873-879), `tests/finger_probe_cache_check.cpp`.

### 1.2 Behaviour per situation

| Situation | Today |
|---|---|
| **Flat hand on a table** | Palm cast (every frame) gives a slab; fingertip casts add more slabs. Long finger straight pose buried -> bends the least that clears it, else hyperextends up to 15 deg, else blocked. Resting curl .35 lifts the finger's want, the surface caps it. Result: fingers lie on the plane, 0-4 mm skin gap. Works as designed in sim (tip_pen). |
| **Wall under the palm** | Same slab path (normal sideways). The palm slab is 12 cm half, so fingers 9 cm beyond the palm are still inside the plane. Fingers lie flat if the plane normal is accurate. No reference-mod-style "open the hand first": the want is the controller curl (>= .3), the contact only caps, so a curled controller hand wants a fist and relies on cap = first touch; the fingertips touch first (curl path knuckle-first) and the finger stops mostly STRAIGHT-ish at the tip contact. |
| **Table edge** | Slab is a 24 cm plane with no edge. A finger curling over a real edge stays held up by the phantom plane up to 12 cm past the hit point; once the cast of that finger misses twice (about 140 ms) its slab drops and the finger curls to its want (.3-.35) in tau .06 + spring .07 -> visible drop, with a short pop. It never "wraps" the edge by touching the side face. |
| **Thumb on top of an edge** | No rule. Thumb closes across the palm as one, stops on slabs, is kept clear of the palm and fingers (`thumb_clear_of_hand`). It can be buried by the slab (`:702-728` pushes it to the least-curl-that-is-out or lets it lie `thumb_shape_tolerance` 6 mm in). |
| **Thin prop (paper, plate)** | `finger_collider` -> `held_slab` from the prop's own bounds (`dxgi_probe.cpp:7123`), counted in `finger_collider_thin`. Real extents (not 24 cm), so the edge is right. Needs a probe hit on the entity and bounds < 1 m. |

### 1.3 Anatomical limits (ours)

`finger_limit_degrees` (`:22`): fingers MCP 90, PIP 110, DIP 80; thumb CMC 70, MCP 50, IP 70. Open to straight only, except the G5 hyperextension (PIP/DIP only,
15 deg, never MCP, never thumb). Thumb opens to -45/-5/-15. No separate back-bend for the knuckle.

### 1.4 Staleness and steadiness risks

- **Cadence.** A finger's slab is refreshed every 5th frame, usable up to 150 ms, one-miss sticky (about 140-210 ms total from last hit). The palm slab refreshes every frame, so the large plane is fresh; fingers mostly add redundant slabs.
- **Mailbox lag.** A cast posted in frame N is answered in N+1 (serial seen at `:12233`), so a slab is 1-2 frames older than the pose it constrains. At walking speed (1.5 m/s hand drift is up to 2 cm in 14 ms) this is within the 15 mm fast tolerance only barely.
- **Planes differ per cast.** Every cast returns a point and a normal on possibly different triangles; five fingertips + palm = up to 6 slightly tilted planes. Unnormalised mismatch (1-3 degrees) makes fingers disagree about the surface height by 1-3 mm over 9 cm. The memo sees a different shape each cycle (position moves with the finger, 2 mm tolerance) and re-solves; those re-solves are what the "refreshed < refresh_limit" cap throttles (`:898-900`).
- **Drawn pose can enter the surface.** The spring (0.07 s) filters the turns AFTER the contact clamp, so when a surface arrives (hand lowered at speed) the drawn finger is a critically damped lag behind a pose that already stopped: it goes deeper than the clamp by up to speed*0.07 s. `tip_pen` flags it only past 5/15 mm. The reference mod has the opposite order: the eased finger in something is put out immediately (the reference source).
- **Sliding.** On a table the surface is static in the world, so a refreshed slab is the same plane: steady. On a prop or body the slab is built from bounds at the hit instant and cached 150 ms: a moving prop under the hand leaves a stale slab.
- **Walking.** Slabs are in world space and the palm is a world pose each frame; no player-motion compensation is needed, but head bob/arm solve noise (1-2 mm) crosses the 1 mm `buried_below` threshold (`:703`, `:729`), toggling bend-vs-lift. Hysteresis exists only for lift-vs-bend (`state.hyperextended`), not for the buried test itself.

## 2. reference VR mod: exact rules

All in the reference source unless noted. Our digest (the reference mod's notes (kept outside the repository)) says "Jacobian-transpose"; the comment at `:3519` says so but the code that ships
(`SolveFinger :3571-3800`) is **bisection on curl**, `Segments` is always called with `push=nullptr` (grep: all calls pass nullptr). Treat the Jacobian text as stale.

- **Joint ranges** (the reference source, deg from straight, flexion +): fingers MCP -30..90, PIP -8..105 (little 110), DIP -20..85; thumb MCP -10..55, IP -20..80 (its metacarpal, chain slot 0, is range 0 and not turned). Plus a hard rule: knuckle and middle joint never more than 20 deg back past straight (`kBackMostDeg`, `:~420`). Ranges are re-based on the rig's current flexion (`c.lo/hi = k*deg - flex`).
- **Capsules** radius MCP/PIP/DIP 9.5/8.5/7.5 mm (little 9/8/7), thumb 10.5/9.5 (`kRadius`), grown by `skinMargin` 2 mm; capsules are of the player's own skin when known.
- **Hinge only** (`finger_hinge_only=1`): each joint turned only about its own hinge, swing-twist split, the knuckle keeps spread (`HingeOnly`).
- **One curl per finger, not joint by joint** (`SolveFinger :3571`): from the open hand (straight, laid in the palm plane `OpenAngles :3541-3566`) to the animation pose, with a back-bend limit `kBack = {.35,.10,.30}` rad (knuckle, PIP, DIP) times the least-that-clears factor (`pressedOut`, 5 bisections). Contact threshold `inMost = skinMargin + 1.5 mm`.
- **Wrap path** (palm on something): t 0..1 knuckle alone, t 1..2 middle and last joints; full wrap `kWrap = {1.1, 1.5, 0.9}` rad (63/86/52 deg). First touch stepped at `kStep=.03` from t=.06 to `kMost=2.0`, 4 bisections; if nothing is met the finger **droops** `kDrape=.35` of the wrap (a relaxed finger over an edge). Grip held adds `kSqueeze={-.2,.6,.35}` rad. A "last word" step turns the finger back toward the open hand by the least of the way that clears it (6 bisections).
- **The palm decides** (`PalmOn :3772`): palm box grown 1 cm in a shape, the way out against the palm's facing (dot < -0.4); with the body held by its fingertips, anything within 1.5/3/4.5 cm in front of the palm counts. Wrap is held 150 ms after it ends (`wrapUntil`), touch counts 100 ms: no flicker.
- **Open first**: when wrap is on the solve starts from `open` (straight), so fingers lie flat on a wall and curl on only where there is no surface. Thumb is solved with `wrap=false` (`SolveFinger(... wrap && f != 4)`, `:3987`): it is only pushed out and left as the animation poses it, so it stays on top of an edge.
- **Edge detection**: `edgeWrap = wrap && middleWrap > 1.1`: the middle finger passed the knuckle share (1.1 rad) before touching = over an edge (used elsewhere for the hand clamp).
- **Back of hand pressed** (`:3995-4005`): fingers straighten up to 35% of the way to the open hand, 4 mm to 4.4 cm held off.
- **Solve geometry**: the fingers are solved where the palm alone would be held (the body moved on toward tracking at most 5 cm until the palm box meets something, 8 steps + 4 bisections, `:3860-3890`), so fingers lie on a table the hand is pressed into.
- **Per-tick**: the worker runs `SolveFingers` once per physics step against up to 1536 near shapes (real geometry, `penetration` queries), query budget 1400 per hand per step (`calls < 1400`); the drawn side takes the turns once per batch (`newBatch = now - lastPassMs > VrikBatchGapMs`, the reference source) and rewrites them in each of the engine's 4-5 passes.
- **Smoothing**: shown angles are critically damped, tau .035 s into a bend (more bent) and .07 s out, max 6 rad/s (`Damped`, `:4060ff`); the drawn side adds `kTurnRate 9 rad/s`, `kTurnTau 0.03 s`, weight .35/batch. **Then a put-out step** (`:4080-4165`): the eased finger in something (`kInMost = skinMargin + 0.6 mm`) is set out at once, by the least of the way to the solve, else toward the open hand (6 bisections), else back-bent (5 bisections).
- **Hold freeze**: after 150 ms of a hold (`kFreezeMs`) the latched turns stop updating.
- **Which joints yield more**: the back-bend shares `{.35,.10,.30}` (PIP barely: volar plate) and the wrap shares `{1.1,1.5,.9}` (middle largest).

## 3. Gap list, ranked by visible impact

### G1. Drawn fingers can sink into a surface that just arrived (high)
Ours: `dxgi_probe.cpp:12350` filters AFTER the clamp; `finger_clamped_angles` (`finger_ik.hpp:1026`) is not the last word. Missing: a put-out after the spring.
Smallest change: in `dxgi_probe.cpp` after `finger_pose_filter[side].step(...)` for `free_hand`, compute the five tips and the proximal/medial capsule points from `finger_points` for the filtered turns, test them against `shapes[]` (`shape_distance`, same code as tip_pen `:12356-12366`); if any phalanx is deeper than 2 mm, blend the filter state (`turn`, zero `speed`) toward the contact target by bisection (6 steps) - a new `FingerPoseFilter::put_out(target, mask)` in `finger_ik.hpp`. Test `finger_pose_filter_put_out_check` (`tests/finger_ik_check.cpp`): hand 3 cm over a slab, step 0.07 s with a table appearing at 0 gap; assert after the call every tip `shape_distance - thickness >= -0.002`, and that without a surface the output equals the spring's. Sim: `hand_on_table` fast leg; `tip_pen.over` must be 0 and deepest fast < 4 mm.

### G2. Table edge: phantom 24 cm plane, no curl over the edge (high)
Ours: `slab_shape` `finger_ik.hpp:1487-1492` (half 0.12), palm slab persists 140 ms. Reference mod: real geometry, then the wrap/droop (`kDrape`).
Smallest change: size the slab by evidence instead of 12 cm: `slab_shape(point, normal, half)` with half = 0.04 for a fingertip slot and 0.06 for the palm slot (the finger is 9 cm long; the palm cast is 3 cm radius) and shrink further when a second probe at the same slot misses (the cache already knows a miss). Add a miss-fast path: on a fingertip miss, `FingerProbeCache::probed` currently keeps the shape for one cycle (70 ms); use 1 miss if the palm slab is still live and the finger tip is beyond the palm slab's extent (`edge_drop`). Test `finger_slab_edge_check`: slab half 0.05 under a hand, finger tips 8 cm past the slab centre; assert `finger_contact.finger[f] >= relaxed_share-1e-6` (free droop), and with half 0.12 assert it stays flat (the regression this removes). Sim: new scenario `hand_on_table_edge.py` (copy of `hand_on_table.py` with the desk edge under the palm heel); new status field `finger_ik.edge_drop[hand]` = frames where a finger went from blocked/touching to free within 5 frames.

### G3. Palm on a wall does not "open first" (medium-high)
Ours: wants are the controller's curl and the resting curl `.35` only below 0.15-0.3 m/s (`finger_ik.hpp:915-923`). With a closed controller hand pressed on a wall, fingers wants .8-1.0 and the first touch is the fingertips (knuckle-first path), so the hand lies as a claw. The reference mod opens from straight when the palm is on something (`PalmOn`, 150 ms hold).
Smallest change: in `finger_contact` (`:915-923`), when `resting` weight > 0.5 (palm on a shape within `resting_gap_near`), scale the four long fingers' wanted curl by `1 - resting_open_share*rest_w` (`resting_open_share=.7`, floor `resting_curl`) in `finger_drawn_pose`/`grip_curls` (`:957`, `:992`), with the same 60 ms tau hold on exit (150 ms stay, matches `wrapUntil`). Test `finger_palm_wall_open_check`: slab 1.5 cm under the palm, wants {0,.9,.9,.9,.9}; assert `grip_curls[f] <= resting_curl+0.05` for f 1-4 and each tip clearance >= -0.004; with the slab removed assert curls rise again within 0.3 s. Sim: `hand_on_table` fist variant (trigger and squeeze held), `tip_pen.over==0` and `touch` pose index flat (new status `finger_ik.flat[hand]` bits = every phalanx within 3 mm of the plane).

### G4. Thumb on top of an edge (medium)
Ours: thumb is solved as a closing finger and moved by the clear-of-hand search; nothing keeps it on the top face. Reference mod: thumb never wraps (`wrap && f != 4`).
Smallest change: in `finger_contact` when `stop_extended` and the palm rest weight is > 0.5, skip the thumb's closing march and take `thumb_nearest_clear` from the open pose (`finger_open_degrees`) only (`:858-876`, `fold && !stop_extended` branch already similar). Test `finger_thumb_on_edge_check`: slab under the palm with the thumb's closed path leading below it; assert thumb tip `shape_distance - thickness >= -thumb_shape_tolerance` and the thumb CMC angle stays <= its open pose + 10 deg. Sim: `thumb` field already in `tip_pen` (deepest, mm); target < 3 mm.

### G5. Fingertip probe cadence: 5-frame staleness, one finger per frame (medium)
Ours: `finger_probe_casts_default=2` (`dxgi_probe.cpp:607`); knob `build/finger-probe-3` (3 casts). Reference mod: no casts, real shapes every step.
Smallest change: adaptive cadence, not a bigger default: when `finger_ik.speed >= .3` (`FingerIkState::speed`) or any finger is `blocked/touching`, probe the touching/blocked fingers first (priority mask) instead of pure round-robin: `f = pick_finger(blocked|touching bits, probe_frame)` in `dxgi_probe.cpp:12251-12256`. Test `finger_probe_priority_check` (new, pure function in `finger_probe_cache.hpp`): bits 0b00110, frames 0..9, assert fingers 1 and 2 are chosen 8 of 10 times and every finger at least once in 10 frames. Sim: `hand_on_table` fast leg; `finger_ik.posts` must not grow (still 2 casts), `tip_pen_fast` deepest should drop.

### G6. Per-cast planes disagree by 1-3 degrees (medium-low)
Ours: five slabs with different normals; none of them merged (`dxgi_probe.cpp:12244-12248` de-dups at 1 um only).
Smallest change: merge slabs within 3 degrees and 5 mm of the palm slab's plane into the palm slab's plane at placement (`finger_collider` caller; compare `dot(n_a,n_b) > cos 3 deg` and plane offset < 5 mm, keep the palm one). Test `finger_slab_merge_check`: slabs at 1.5 and 2.5 degrees tilt over 8 cm; assert one shape in `shapes[]` and tip errors < 1 mm vs the palm plane. Status: new `finger_ik.merged` counter, `finger_ik.avg_shapes` should fall.

### G7. Buried threshold has no hysteresis (low-medium, the walking jitter)
Ours: `buried_below=-.001` / `clear_above=-.0005` (`finger_ik.hpp:703`, `:729`) give half a millimetre of deadband only. With 1-2 mm arm noise a finger toggles between "bend" and "lift" (the `state.hyperextended` stickiness only helps the lift side).
Smallest change: widen to `buried_below=-.002` while the state says blocked/bent last frame (carry `state.blocked`/`min_curl>0` as the sticky flag), keep -.001 when free. Test `finger_buried_hysteresis_check`: a slab oscillating +-1.2 mm about the finger's straight skin at 36 Hz for 2 s; assert `min_curl[f]` changes by < 0.05 frame to frame and the number of bend/lift flips is <= 2. Sim: `finger_ik.stuck`/`hyperextended` bit toggle count over the 2 s hold of `hand_on_table`.

### Already adequate (no change)
- **No tip inside a still surface**: `stop_extended` + `tip_pen` (tolerances 5/15 mm, `dxgi_probe.cpp:12356-12372`); G1 is the fast-motion remainder.
- **Thin props**: real bounds, `finger_collider_thin`.
- **Sliding along a static table**: world-space slab, no per-frame change; the 12-cm slab plus palm slot every frame keep it steady.

## 4. Summary ranking

1. G1 put-out after the spring (visible sinking on every landing).
2. G2 slab size / edge curl (the user's "curl over a table edge" case).
3. G3 palm-on-wall opens first (the user's "lie flat on a wall" case).
4. G4 thumb on top of an edge.
5. G5 priority probe cadence.
6. G6 merge slabs.
7. G7 buried hysteresis.
