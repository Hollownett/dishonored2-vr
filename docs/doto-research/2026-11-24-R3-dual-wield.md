# R3 - Dual wield (D2 two hands + DOTO forearm Voltaic guns): architecture, gaps, fixes

Worktree ~/github/dishonored2-vr-doto @ 7ef5612 (feat/doto-vr). Read-only research; nothing was built or run. All line numbers are
`src/dxgi_probe.cpp` (D) unless another file is named. "Confidence" marks what was proven by reading vs what needs a lab run.

## 1. Architecture

### 1.1 Who owns what
* Engine: ONE slot 0 (sword/knife) and ONE slot 1 (gun, gadget, power). The mod's arm plan (`src/arm_state.hpp`, packed in `arm_pack`,
  D:5940) says per arm: sword arm (`sword_arm_now()` D:5949), slot-1 arm (`slot1_arm_now()` D:5950), cross bits, and each arm's mod-owned
  copy (`arm_copy_now(h)` D:5952). Default: sword right (mh=1), slot 1 left (rh=0).
* Sword is held only while its grip is squeezed (`SwordGrip`, src/vr_controls.hpp:548-640; `held_`, `owns = held_||pending_||zone||copy_reach`).
  The control thread computes it at D:16427 (`sword_owns` plain bool, control thread only; `sword_held_now` atomic, D:6241, written D:16427).
* D2: slot-1 gun on one hand (native), second gun = ArmSet copy (`Home::copy`, kinds pistol/crossbow) in the other hand, fired by
  `NativeRangedFire` from `held_copy_pose(h)`'s barrel (D:17184-17200). ArmSet makes sword and copy exclusive per arm
  (`Fx::sword_left`, `extra_owns` kills SwordGrip D:16415-16416).
* DOTO: Billie has no hand-held gun. The engine's `arkCpntWeaponGaussGun` hangs on `left_lower_armhelper2_jnt` (native, left forearm,
  pinned: `voltaic_pinned()` D:5959, `voltaic_native_hand=0`, `ArmSet::pin_slot1` D:16398). A mod-spawned copy of the gun entity
  (`game_traits::voltaic_copy_def`, game_profile.hpp:60) rides either forearm. The forearm gun is NOT a hand item: the hand stays free
  (grab, doors, sword); fire = grip>=.55 AND trigger>=.35 of that hand (D:17155-17165 native, D:17203-17226 copy).

### 1.2 Which forearms carry the gun: `VoltaicArms` (src/voltaic_arms.hpp, v145744/45)
Pure state machine, fed each control frame in D:16688-16730: `commit(h)` (wheel closed on an item), `same(h)` (hooked equip saw the same
item), `update(native, now, rev)`. Output `arms` bit0 = left (engine's gun), bit1 = right (copy). Mirrors:
`voltaic_arms_now`, `voltaic_copy_at[2]` (D:5963; left copy only when engine slot holds another item, right whenever carried),
`NativeWeaponHands::forearm_native_hide(...)` (hides the engine gun when only the right carries it, D:16721).
A gun chosen by the sword's hand sheathes that sword (D:16715-16720, only on the gain edge).

### 1.3 Copy entity: spawn / attach / draw
* Spawn: `NativeWeaponHands::pool_acquire` + `spawn_def` with owner = player (native_weapon_hands.hpp:812); service loop at
  :1815-1860 (`forearm_mode(side)` true when `forearm_copy(side,&offset)` was set by D:16727-16731).
* Anchor: each copy is bound (0x875e80 -> DOTO 0x8e36f0) to a hidden anchor entity of its own (`service_forearm_anchors`
  native_weapon_hands.hpp:1111-1147) which the game-thread `teleport()` moves (v145748).
* Pose: render thread, eye-0 Begin only, gated by `image_skin_enabled && full_body_requested && tracked_arms_requested` (D:7983-7986):
  `publish_voltaic_copy()` (D:6113-6141) reads the DRAWN arm's `*_lower_armhelper2_jnt` image frame (`forearm_helper_index` D:4574) ->
  `NativeWeaponHands::forearm_pose` (game-thread placement feed, 250 ms staleness) and writes the copy's model frame (+0x1860) =
  forearm * offset (left: identity = engine's own bind tag; right: `power_hand::mirror_correction(weapon_tag_rot)`, a y180 rotation).
  No fresh feed -> copy hidden (`held_hide`, :1832). The feed is published only when `game_logic_mask()==2`.
* Draw-time (per eye / per pass) placement: D2 copies call `note_copy_draw` (:1861) so `publish_weapon_draw`/`place_weapons` re-place them
  at every draw call. The DOTO forearm copy calls `clear_copy_draw` instead (:1857-1859): it is placed ONLY by the teleport and the one
  eye-0 model-frame write per render.
* Visibility: copy exists while `voltaic_copy_at[side]` (needs `logic==2`, `dual_wield_enabled`, copy knob); no other hand-state toggles
  (not hidden for sword/grab/hold/carry).

### 1.4 Fire routing per hand
* Native left (pinned) gun: D:17155-17165: `hand_busy_now = grab || native_grab.holding(gh) || weapon_owns(gh) || !(varms&1)`;
  `voltaic_fire = grip>=.55 && trigger>=.35 ? trigger : 0` routed to the engine's LT. `weapon_owns(h)` (D:17144) =
  `(h==rh ? gun_owns||hip_draw_reserved : sword_owns) || extra_owns[h]`; in DOTO `gun_owns` is forced false (D:16573), so for the sword
  hand it is `sword_owns` => a hand squeezing the sword never fires its forearm gun (OK).
* Copies: D:17203-17226 `copy_busy = grab || holding || weapon_owns(ch) || fist_take`; `pull = !copy_busy && grip && trigger` ->
  `VoltaicCharge::step` (voltaic_arms.hpp:78-95; 2 s charged, 4 s auto overcharged) -> `NativeRangedFire::fire(Shot::crossbow)` with
  origin/axis = the hand's own aim ray `voltaic_copy_aim[ch]` (D:10663-10676, <=100 ms fresh), `shot.entity` = copy entity (sound).
* Native trigger kill: D:17169-17170 `if(voltaic_copy_at[s] && raw_grip>=.55 && raw_trigger>=.35) native trigger of hand s = 0` (copy's
  chord never reaches the hand's native trigger).
* D2 copies: D:17184-17200 per hand `ex=arm_set.copy_at(h)`, trigger >= .55/.35, muzzle = `held_copy_pose(h)` + .2 m forward.
* First-shot fix 7ef5612: `gun_chord_hand` exempts the forearm chord from the 400 ms GripTriggerGuard (D:~17800).

### 1.5 Ammo / bolt per hand
* Engine's gun: bolt = the engine's component ammo, chosen by the game's wheel equip. `NativeManualEquipRuntime::wheel_pick` ->
  `NativeRangedFire::wheel_pick` (native_ranged_fire.hpp:311-340) hooks the wheel's equip (0x7fb350). DOTO branch: left wheel
  (`side==0`) always returns 0 (game's own choice, the engine's gun); right wheel stores `copy_ammo_[1][crossbow]`, `hand_choice_`,
  and returns the engine gun's current ammo (`keep`) so the engine gun keeps its bolt. `copy_ammo_` is applied only in `handle()`
  (:903-906) by swapping `comp+0x98` for the shot (`AmmoSwap`, :930-937) and only when `!l->own`.
* Wheel hand identity: `note_wheel_open(h)`/`note_wheel` (wheel_hand_, :211-212). `voltaic_native_hands_` is now unused (:326).
* D2 per-copy ammo: `note_copies(kind(0),kind(1),slot1_holder)` D:17185 -> `track_ammo` (:788-846) keeps each copy's component on its
  own bolt via `select_ammo` (0x63a490) and `bolts_` status.

### 1.6 Crosshairs
* ONE game HUD crosshair (arkHUDElementCrosshair) positioned by `native_crosshair.publish(nx,ny)` from the shared aim ray, per eye
  (D:12285-12328). Ray hand = `aim_hand` (D:10581): `voltaic_native_hand` (0, constant) when pinned in DOTO, else `slot1_arm_now()`.
  Visibility = `crosshair_visible_now` (settings only, D:12291) AND `crosshair_composed` (aim current). Never looks at which hand holds
  what. Shown even when the engine's gun is hidden (right-only arms) and when the left hand holds the sword (see G-c1).
* Second crosshair "ring" per hand (D:12329-12365, v1387 + v145750): D2: `copy_gun=dual_wield_enabled && crosshair_visible_now &&
  is_gun(arm_copy_now(h))`, pose = `held_copy_pose(h)` + .2 m; DOTO: `voltaic_copy_at[h]` and a <=250 ms-fresh `voltaic_copy_aim[h]`;
  distance from a weapon-segment cast slot `copy_crosshair_slot`; published per eye via `game_xr->copy_reticle(h,eye,ring)` and drawn
  in game_xr.hpp:4476-4500 (`copy_reticle_draws`). Status: `crosshair.copy_ring.{published[2],no_pose[2],last_ndc,draws}` (D:16156).
* `ranged_aim_gate` (src/ranged_aim_gate.hpp): reticle = valid pose & not authored view; shots additionally need no sword swing in the last
  500 ms (`swing_clear`), holster_clear, focused. The DOTO copy rays use `copy_gate.shots` (D:10663-10672).

## 2. Gaps and fixes

### (c) Block the crosshair gun when the sword is in that hand (the user's third ask) - highest value
Fire is already blocked by `weapon_owns` (see 1.4); the crosshair is not, and one side effect on the sword is a bug.

* **G-c1 [high confidence] Right-hand ring shows while the right hand holds the sword.** Default DOTO plan has the sword right and the
  copy right; the ring condition (D:12338-12343) is only `voltaic_copy_at[h]` + fresh aim tick + `crosshair_visible_now`. The fresh tick
  is written whenever `copy_gate.shots` passes (D:10663), and SwordGrip state is not an input. Evidence: v145750 (2075edf) added the ring;
  nothing in 2075edf..7ef5612 reads `sword_held_now`/`sword_owns` for it. Fix: add `std::atomic<unsigned> hand_sword_bits` published at
  D:16427 (`(sword_grip.held() || pending) ? 1u<<mh : 0`; exclude `zone`, which flickers at the shoulder) and require
  `!(hand_sword_bits & (1u<<h))` in the ring condition for both D2 (`copy_gun`) and DOTO (`voltaic_ring`); add ~150 ms release hysteresis
  to avoid a ring flash while the grip opens. File: dxgi_probe.cpp at 12335-12343 and 16427.
* **G-c2 [high] Main HUD crosshair stays on the left forearm ray while the left hand holds the sword** (user mirrored/left-handed sword
  via wheel or `pin_slot1` refusal: `aim_hand` is the constant 0, D:10581; comment D:17172 "the left arm holds the sword, so pin_slot1 could
  not move it"). Fix: compute `aim_blocked = voltaic_pinned() && (hand_sword_bits & (1u<<aim_hand))` and fold into
  `crosshair_visible_now`/`crosshair_composed` (D:12291-12294; also stop the 30 m cast post at D:12296 when blocked, and
  `reticle_left`/`gate.reticle` at D:10648). The shots keep working as `hand_busy_now` already blocks them.
* **G-c3 [high] Main crosshair drawn when the engine's gun is hidden (arms==right only, `!(varms&1)`)**: the left forearm shows nothing but
  the HUD reticle sits on the left hand's ray while the right ring also shows. Fix: same fold, `aim_blocked |= voltaic_pinned() &&
  !(voltaic_arms_now & 1)` (only when `voltaic_copy_at[1]` has a ring). Needs a render-thread-visible flag (already atomic).
* **G-c4 [high, likely regression] Sword block is dead with a right copy.** D:17169-17170 zeroes the hand's native trigger on grip+trigger
  whenever `voltaic_copy_at[s]`, with no `weapon_owns(s)` test, but in dual wield the sword hand's trigger IS the block (D:16689; D:10636
  comment). Right copy + right sword (default) => RT is cleared, so block/parry never reaches the game, and the copy does not fire either
  (`copy_busy`). Fix: `... && !weapon_owns(s)` (move the lambda definition above, or test `sword_owns && s==mh`). Same for the native
  chord when the sword is in the left hand (the `voltaic_fire`/`right_trigger=0` at D:17172-17173 already keeps it released only while
  firing, fine). Needs a headset/sim check that RT block works with `voltaic both`.
* **G-c5 [medium] Ring flicker from the shots gate.** The ring needs `voltaic_copy_aim_tick` <=250 ms, which stops being written during
  500 ms after every sword swing (`swing_clear`), while the sword hand's ring is the thing to hide anyway; for the OTHER hand (left
  copy while the right swings) the ring blinks out for 0.5 s. Fix: give the ring its own pose read with the `reticle` gate
  (`copy_gate.reticle`) and keep the shots gate only for firing.
* **G-c6 [medium] Ring for a hand that cannot fire for another reason (grabbing/holding a prop, body carry, fist_take).** Same
  predicate as `copy_busy` (D:17210) should hide the ring. Publish `std::atomic<unsigned> copy_armed_bits` from the control thread at the
  copy loop (D:17208-17211: `!copy_busy`) and use it in the ring (covers sword too, a single flag instead of the sword bits for DOTO
  rings; the main crosshair still needs the sword bits).
* **Does it fire while the sword is in this hand?** No: `copy_busy` (D:17210) and `hand_busy_now` (D:17161) include `weapon_owns` ->
  `sword_owns`. One caveat: `sword_owns` includes `in.zone` (hand at the shoulder holster) so a squeeze at the holster is also blocked
  (acceptable). No code change for firing beyond G-c4.

### (b) Bolt type per hand
* **G-b1 [medium-high, needs one lab read] Copy's bolt choice may never reach the shot or the model in DOTO.**
  Chain: right wheel -> `wheel_pick` stores `copy_ammo_[1]` (:330) but never calls `select_ammo` on the copy component (D2 does,
  :358). In `handle()` (:892-906) the copy component is accepted whenever `typed(cc, voltaic_comp_vtable)` and `cc+0x10==entity`
  (`held_copy::kind_of(gaussgun)` = crossbow, held_copy.hpp:104-108; `own_weapon` finds it, `started_weapon` required) -> `l->own=true`
  -> `copy_ammo_` is skipped (`l->own?0:copy_ammo_`, :903) and the shot uses `cc+0x98`, whose default/first ammo is never changed. If the
  copy comp is NOT found (own_shots stays 0), the AmmoSwap path (:930) does apply `copy_ammo_` for the shot, but the copy's component
  (visual: the user's "copy shows the wrong bolt", v145749) still holds the default bolt. Either way the copy's model/component is never
  synced. `track_ammo`'s copy loop (:812-846) and the `ammo` test command do nothing in DOTO: `note_copies(kind(0),kind(1))` (D:17185)
  feeds `arm_set.copy_at(h)`, which is `none` for the Voltaic copy, so `copy_kind_ = -1`. Status shows it: `fire.v1094[4]` (own_shots) and
  `fire.bolts` (always -1 for copies).
  Fix (native_ranged_fire.hpp): (1) DOTO `wheel_pick`: after `copy_ammo_[side]=entry`, `if(const auto cc=copy_comp_[side]; cc &&
  rig::value(cc+0x98)!=entry && select_ammo(base,cc,ammo)) ++copy_ammo_sets_;` and `set_own(cc,entry)`; (2) D:17185: call `note_copies`
  with `1` (crossbow kind) for each `voltaic_copy_at[h]` in DOTO so `track_ammo` keeps the copy's component on its own bolt and fills
  `bolts_[1+side]` and the fresh-copy logic; (3) make `copy_ammo_[0]` meaningful for a left copy (see G-b2).
* **G-b2 [medium] Left copy (engine slot holds another item) can't have its own bolt.** `wheel_pick` returns 0 for `side==0` (7ef5612)
  so a left choice always goes to the engine's gun; if the left forearm carries a copy at that moment the same commit makes VoltaicArms
  hand it the native (arrival) and the left copy vanishes - consistent. But two guns with different bolts left+right works only for
  native-left + copy-right. If the user wants copy-left/copy-right with different bolts, branch on `voltaic_copy_at[0]` (publish it into
  `voltaic_native_hands_` replacement atomics) rather than the constant `side==0` rule.
* **G-b3 [medium] Hand that owns the bolt after an arm change.** `copy_ammo_[side]` is cleared only when the copy component pointer
  changes (`set_copy_comp` :219-220); a right copy that is dropped and re-gained keeps the previous choice (fine), but
  `hand_choice_`/`pick_` are D2-oriented (`pick_ms` 4000, `carry_ms`). Check `fresh_copy_take` semantics for the Voltaic gun after
  `voltaic drop right` -> `voltaic both` (scenario below).
* **G-b4 [low] HUD ammo display** follows the engine's gun, not the copy; no HUD fix recommended, but status should expose the copy bolt
  index (G-b1 (2) does).

### (a) Gun render polish
* **G-a1 [medium, needs a lab shot] Copy skips draw-time per-eye/per-pass placement.** `clear_copy_draw` for forearm copies
  (native_weapon_hands.hpp:1857-1859) means the copy is not in `copy_draw_` => `publish_weapon_draw`/`place_weapons` never re-place it per
  draw (D2's `note_copy_draw` path with `draw_write_`). DOTO-VR.md §5 row play21 documents exactly this class (black sword = untagged
  render passes + TAA). The forearm copy is a plain entity drawn in world passes, written once at eye-0 Begin (D:7983-7986). Risk: the
  second eye / alternate-eye render (and motion vectors for TAA) draw it at the game-thread teleport pose from an older tick: lag or
  ghosting between eyes. Also the write requires `image_skin_enabled && full_body_requested && tracked_arms_requested`; if any is off the
  copy is hidden (feed absent). Proof: per-eye mirror screenshots (`play*-stereo-mirror` style) with the right copy + fast arm swings;
  compare copy position per eye; check `voltaic.stats[6]` (model frame writes) ~ frames, `copy_entity[2]` (hidden count) ~ 0.
  Fix options: (i) register the forearm copy with `note_copy_draw(kind 3)` using the forearm pose as `read` and the applied offset (needs
  `copy_draw_grip` to accept a non-grip source), or (ii) call `publish_voltaic_copy()` again at the eye-1 Begin (D:7983 is `eye==0`
  only).
* **G-a2 [medium] Right copy uses a y180 *rotation* (`mirror_correction`) of the left gun**, not a mirror; fine for a symmetric body but
  any handed detail (cables, emitter side, glow) will face the wrong way on the right forearm. Needs a look at the render; fix would be
  a model-frame negative scale on one axis (frame_from_pose supports scale; `want[12..14]` are scales and currently copied from `cur`).
* **G-a3 [medium] Native gun hide kills its VFX** (`forearm_native_hide` just hides the entity): with right-only arms the engine's charge
  glow/muzzle for the copy shots plays on the hidden native entity (shot.entity = copy for sound only). Check that the copy shows
  charge/fire VFX (user-visible); fix would route the VFX/anim events to the copy entity as D2 does (`copy_events`, `equip_anim`
  keyed to `own_weapon` in native_weapon_hands.hpp:2516-2520 - `copy_events` gets `kind=crossbow` for the Gauss copy, so verify
  `copy_events_`/`gate_copy_fire` fire on it).
* **G-a4 [low] No hide for a hand that holds something** (sword, grabbed prop, carried body): copy stays drawn on the forearm. Wanted?
  Probably yes for body carry and mantle (authored arm), no for sword (forearm gun + sword hand is the design). Use the same
  `copy_armed` flag as G-c6 for body carry only.
* **G-a5 [low] Stale model frame check**: `origin_gap > max_move_m (1.5 m)` skips the write ("not ours") (D:6133); after a teleport/respawn
  the copy shows stale for a tick. Fine.

## 3. D2 (Dishonored 2) check
* Same crosshair rule: **satisfied by construction in D2**: sword and a copy gun are exclusive per arm (ArmSet, `extra_owns` zeroes
  the sword grip D:16415-16416; `Fx::sword_left`), and the main crosshair follows `slot1_arm_now()`, which can never be the sword arm.
  So no D2 rule change is needed for the sword. The ring for a D2 copy only shows if `held_copy_pose(h)` is fresh, so it already
  disappears when the copy is hidden (a hand that holds a bottle: `hand_route::copy_gun` D2 `!holds(s)`).
* Shared fixes: (1) G-c6's `copy_armed` flag also covers the D2 ring when the copy hand grabs something while its copy stays visible
  (low risk to add; the same code block D:12329-12365 serves both games); (2) the G-c5 flicker applies to D2 only through `ranged_aim_gate`
  for the main crosshair (not the ring: D2's ring pose is `held_copy_pose`, not the shots gate) -> no change; (3) G-c4 (trigger kill)
  is DOTO-only (`voltaic_copy_at`).
* D2 bolt choice per copy is already complete (track_ammo + bolt_check tests); G-b1/b2 are DOTO-only.

## 4. How a simulated session proves each fix
Harness: `tools/simulated_gameplay_script.py` segments; `_lib.console("d2vr <cmd>")` test commands (D:7305+): `voltaic left|right|both|none|
drop left|right|charge on|off`, `equip`, `extra left|right pistol|crossbow|none` (D2 copies), `ammo left|right <i>` (D2 copies only),
`melee`, `console`. Sword draw/hold: scenario `sword_throw.py` pattern (hand at ZONE [-0.03,0.25,0.50], hand_pitch 60, grip 1.0 draws; keep
grip held = sword held). Status: `xr-live-status.json`.

| Fix | Scenario (new file `tools/linux/scenarios/voltaic_sword_hand.py`, modeled on voltaic_keep.py + sword_throw.py) | Fields to read |
|---|---|---|
| G-c1/c6 ring hidden with sword in hand | `voltaic both`; wait 4 s; ring baseline (grip open); then draw sword with right grip at ZONE and hold 3 s; release at the zone to sheathe; hold the hand idle 2 s | `crosshair.copy_ring.published[1]` must stop rising while held (and `draws`), rise again after; new `crosshair.copy_ring.suppressed[h]` counter + `sword_hand_bits` (add to status); `sword_grip` block (held) |
| G-c2/c3 main crosshair | (a) `voltaic right`: only right carries the gun; (b) mirror: draw sword with LEFT hand (left zone) then `voltaic both` | `crosshair.visible` + new `crosshair.blocked` reason; `crosshair.cast_posts` stops rising; `crosshair.game_art_draws` flat; `voltaic.arms`, `voltaic.native_hide` |
| G-c4 block works with copy | `voltaic both`; sword held right; press right trigger (grip held) 0.5 s | `attack_intent`/`dual_wield` trigger counters (D:16755 `sword_block`), input block state; `voltaic.stats[3]` (native chord frames) unchanged, `fire.fired` unchanged (no shot) |
| fire blocked with sword | same, with grip+trigger on the sword hand | `voltaic.stats[0..2]` (copy asks/queued/refused) must stay 0; `fire.fired` flat |
| G-b1 bolt | `voltaic both`; add test command `voltaic bolt right <i>` (add: calls `wheel_pick` path); fire right chord twice with different `i` | `fire.v1094` [ammo_swaps, ammo_splits, spreads, bursts, own_shots, copy_ammo_sets, wheel_picks, same_item_keeps]; `fire.bolts` [native, left, right] (must be non-negative for copies after the fix); projectile def of shots in `fire.shots[]` |
| G-a1 render | `voltaic both`; head still, right hand swings; save per-eye mirror images | `voltaic.stats[6]` (frame writes), `voltaic.copy_entity` [spawned,placed,hidden,presented,failed], `anchors` [anchors,rebinds,failed] (failed 0), pixel diff between eyes |

Existing evidence to extend: `fire.fired`, `fire.projectiles_created`, `voltaic.arms`, `voltaic.copy` mask, `voltaic.arm_events`,
`voltaic.charge`. Known status limits to fix while there: `copy_ring.last_ndc` is shared by both hands (D:12360 writes index 0/1 as x/y;
make it `[[x,y],[x,y]]`); no status states why the main crosshair is hidden.

## 5. Prioritised tasks
| # | Task | Size | Risk |
|---|---|---|---|
| 1 | G-c1+c6: `copy_armed_bits` / `hand_sword_bits` atomics; ring hidden when that hand holds the sword (or is busy); status counters | S (~40 lines D + status) | low; only the ring |
| 2 | G-c4: do not zero the native trigger when `weapon_owns(s)` (restore sword block with right copy) | XS (1 line) | medium: verify RT block in headset/sim; touches input routing |
| 3 | G-c2/c3: hide main crosshair when the aim hand holds the sword or the engine's gun is hidden; stop the cast post | S | low-medium: crosshair composed/hidden flags are shared with the HUD draw hook |
| 4 | G-b1: select_ammo on the copy comp in DOTO `wheel_pick`; `note_copies` for Voltaic copies; copy bolt status; add test command | M | medium: game-thread call into 0x63a490 on a copy comp (D2 does it; DOTO address is the ported one: check `offsets.json` rule) - lab proof first with a read-only status run |
| 5 | G-c5: own reticle gate for the copy ring (no flicker after swings) | S | low |
| 6 | G-a1: stereo/per-eye check; if bad, eye-1 `publish_voltaic_copy` or `note_copy_draw` for forearm copies | M-L | medium-high: render pass tagging, TAA; do after a baseline capture |
| 7 | G-a2/a3: mirrored model scale / VFX on the copy | M | medium: visual only, but VFX needs RE |
| 8 | G-b2: left copy ammo ownership | S | low; only if copy+copy bolts are wanted |

Suggested order: 2, 1, 3, 5 (one build), then 4 with a status-only probing run first, then 6/7 after a screenshot baseline.
Note the lab saves hold few Voltaic charges (voltaic_keep.py docstring): order shots by importance.

## 6. Open questions (not answerable from code)
* Whether the copy's `copy_comp_` is set in DOTO (decides G-b1 branch): read `fire.v1094[4]` own_shots vs `fire.fired` after copy shots.
* Whether the engine's HUD crosshair element draws when only the hidden native gun is equipped (G-c3 assumes it does; v145750's journal said
  so implicitly: "the left forearm shows it whenever it carries one").
* Whether the DOTO user wants the sword hand's forearm gun fully disabled (as implemented) or only visually hidden when sword is held.
