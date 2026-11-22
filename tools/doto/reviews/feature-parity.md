# D2 -> DOTO feature parity (static + counter review)

Read-only review, 2026-11-22. Nothing in src/, tools/, build/ was changed and no game was touched. Evidence:
D2 reference `artifacts/dxgi-probe-result.json` + `xr-live-status.json` (D2 sim, 3087 frames, spacewarp-safe-mid knob on),
DOTO `artifacts/doto-runs/play21` (v145721, hands_fingers), plus play13-20; generated tree `build/doto-src/src` (regenerated 11:50,
already contains the pin-audit rules and the Iggy rule); `artifacts/doto-lab/port-report.json`; pefile on both exes; the DOTO resource
index (`base/game1.index` is greppable: it lists every decl/def path). Scratch scripts: `scratchpad/parity/`.

**Caveat on evidence:** play21 was built at 09:27, before commit 2576249 (render pass callbacks), before the pin-audit/Iggy rules
reached the tree, and before v145724 (idEntity Hide/Show rvas). Counters in play21 that these fixes touch (HUD children published 0,
note kind, alternate TAA abi, bodyless guard, hide/show) must be re-read from a v145724 run. D2's own sim leaves most combat/interaction
counters at 0 too, so "not exercised" below means the sim cannot decide it; those verdicts are static.

---------------------------------------------------------------------------------------------------

## 0. TOP PRIORITY: grip does nothing on doors / props / bodies in a DOTO headset (physical grab path)

Headset report: "doors and all other objects do not react to grip action". Three independent generator misses, each of which kills the
grab path on its own. Two of them are NOT in pin-audit.md (they are RVAs in places the byte-pin audit does not look). Both are one-line rules.

### G1. Hand-aimed interaction ray is never routed (hover target = head ray, never the hand) [high confidence]
* `src/native_interaction_aim.hpp:1577` (identical in `build/doto-src/src/...:1577`):
  `callers{{{0x3b867b,0},{0x3b9037,0},{0x3b9962,0x3b9a49}}}` are D2 return addresses of the three sensor calls the ray detour accepts.
  port.py left them alone (port-report offset_review "no aligned access", class none). Every other use of 0x3b9037/0x3b9962/0x3b9a49
  in the same file WAS translated (lines 2048, 2092 -> 0x3f79b7, 0x3f82e2, 0x3f83c9). So `ray()` returns `native` at its first test
  (`rva!=callers[stage][0] && rva!=callers[stage][1]`) and never reaches `++rays_`.
* Counter proof: `native_interaction_rays` D2 18468, `native_interaction_routed` D2 18438; DOTO 0 / 0 in play13..21 (all runs), while
  `native_interaction_calls` is 16672 (the hook is alive, only the ray swap is skipped). Hand hover, grab-alone target, loot/talk/pickpocket
  by hand, door-by-hand and the prompt anchoring all depend on the routed hand ray, so a grip has no target.
* Verified with pefile: D2 0x3b867b/0x3b9037/0x3b9962/0x3b9a49 are each preceded by `e8 call` (to 0x6c6760 / 0x95280 / 0x95550 / 0x95550);
  DOTO 0x3f6f9e / 0x3f79b7 / 0x3f82e2 / 0x3f83c9 are each preceded by `e8 call` to 0x726be0 / 0x974c0 / 0x97790 / 0x97790, which are the
  `translate.py` exact twins of the D2 callees (0x6c6760->0x726be0, 0x95280->0x974c0, 0x95550->0x97790).
* Fix (offsets.json `sites`, four rules, "source": "review/feature-parity"):
```json
{"file":"src/native_interaction_aim.hpp","contains":"callers{{{0x3b867b,0}","from":"0x3b867b","to":"0x3f6f9e","why":"ray detour caller table stage0: return addr after call 0x726be0 (twin of D2 0x6c6760)","source":"review/feature-parity"},
{"file":"src/native_interaction_aim.hpp","contains":"{0x3b9037,0},{0x3b9962,0x3b9a49}","from":"0x3b9037","to":"0x3f79b7","why":"stage1 return addr after call 0x974c0","source":"review/feature-parity"},
{"file":"src/native_interaction_aim.hpp","contains":"{0x3b9037,0},{0x3b9962,0x3b9a49}","from":"0x3b9962","to":"0x3f82e2","why":"stage2 return addr after call 0x97790 (same as lines 2048/2092 already translated)","source":"review/feature-parity"},
{"file":"src/native_interaction_aim.hpp","contains":"{0x3b9037,0},{0x3b9962,0x3b9a49}","from":"0x3b9a49","to":"0x3f83c9","why":"stage2 second return addr after call 0x97790","source":"review/feature-parity"}
```
  (The sites rule takes one literal per rule; if two literals on one line need separate matches use `lines`/distinct `contains` as the existing rules do.)

### G2. Initial-contact collector refuses to initialise: every world cast with initial overlap / continuation fails [high confidence]
* `src/native_initial_contact.hpp:36` `native_slots{0xa95260,0x90e80,0x99d50,0xca53a0,0x998b0}` -> generated
  `{0xb183c0,0x92f40,0x99d50,0xd27e50,0x93eb60}`: slot 2 stayed at the D2 rva (port-report: "2 masked matches"). `verify()` compares the
  collector vtable (DOTO 0x1bdb3b0) slots with these. Read from the DOTO exe with pefile: slots are `0xb183c0, 0x92f40, 0x9bfe0, 0xd27e50, 0x93eb60`.
  Slot 2 is `0x9bfe0` (`0f 28 05 .. 33 d2 b8 ff ff 00 00 0f 29`, same shape as D2 0x99d50). Everything else in verify() already matches
  (getter bytes `48 8d 05 d9 a8 96 01 c3` at 0xb183c0, noop `c2 00 00` at 0x93eb60, metadata 0x2482ca0).
* `NativeInitialContactCollector::initialize()` (hence `NativeRegularContactCollector::initialize()`) returns false, so
  `native_world_trace.hpp:351-353` (`collect_initial` casts) and `:475-476` (exclusion continuation casts) return `{}`/`blocked`.
  These are the casts behind hand reach clamp, hand body contact, finger surface probes, hand sphere volume, grab LOS/obstruction
  (`grab_los.obstruction_required:true`), weapon collision answers.
* Counter proof (D2 -> DOTO): `hand_body.cast_continuations` 1897 -> 0, `hand_body.cast_excluded_hits` 3237 -> 0, `hand_body.contact_frames`
  449 -> 0, `native_hand_volume.requests` 99 -> 0, `finger_ik.hits/slabs/probe_hits_world` 4722 -> 0, `hand_clamp.unanswered` 5471 of 5475 frames,
  `native_hand_volume.direct_shortcuts_filtered` 3084 -> 0, `weapon_collision.contact_frames` 6096 -> 0 (play21; play15 had walk contacts
  through the non-initial path).
* Fix: `{"file":"src/native_initial_contact.hpp","contains":"native_slots{0xb183c0,0x92f40,0x99d50","from":"0x99d50","to":"0x9bfe0","why":"collector vtable 0x1bdb3b0 slot 2 read from the DOTO exe","source":"review/feature-parity"}`
  (`contains` is matched against the generated line; use the D2 spelling if the rule engine matches the source line).
* After G1+G2 the grab chain still needs a headset/sim test with a prop in reach; counters to watch: `native_interaction_rays/routed`,
  `physical_grab_queries/picked/moved`, `grab_los.hover_requests/hover_verdicts`, `hand_body.cast_continuations`.

### G3. Entity-typed offsets the generator did not recognise (held object / body / weapon paths) [high confidence per line, impact medium]
The entity map (`0xa8->0xb0 id`, `0xf0->0xf8 model`, `0xc8->0xd0`, `0xd1->0xd9`, `0x300->0x310 origin`, `0x30c->0x31c axes`, `0x358->0x368`, `0xb0->0xb8` children list;
fn_diff of the idEntity ctor 0x874c50->0x8e2390 confirms +8 from 0x20 and +0x10 from 0x2b8) is applied only when the base token is one of
`player, entity, ent, npc, victim, owner, e`. Left at D2 values (code lines only; comments excluded):

| file:line | read | fix | feature that goes wrong |
|---|---|---|---|
| dxgi_probe.cpp:4932 `thing[hand]+0xf0` | model of the held entity -> frame +0x1860 | 0xf8 | held prop render frame / finger-hold alignment: `held_skip[hand][1]++; continue` every frame, the held object is never matched to the hand |
| dxgi_probe.cpp:4625 `t+0xf0`, 4636 `child+0xf0` | held/child model frame | 0xf8 | object-motion held frames (AFW / motion vectors of a carried prop), child-of-entity scan |
| native_held_object.hpp:504, 528 `m+0x300`, `m+0x30c` ; dxgi_probe.cpp:6989 `*m+0x300/0x30c` | master entity origin / axes | 0x310 / 0x31c | held object bound to a master (a prop on a rail/crank, a limb on a corpse): wrong rebind origin |
| dxgi_probe.cpp:8421 `body+0x300` | entity origin for the world marker | 0x310 | carry prompt anchor above a body on the floor |
| dxgi_probe.cpp:10967 `cast_npc+0xd1` (and 11021 `contact->entity+0xd7`) | dead/unconscious flag | 0xd9 (0xd7->0xdf) | sword contact: `cast_dead` is wrong, so a swing at a corpse/live guard picks the wrong victim; combat log flags garbage |
| dxgi_probe.cpp:16019 `*pl+0x300` | player origin in OfferHold grace | 0x310 | takedown/choke offer hold distance (prompt flicker or never holds) |
| dxgi_probe.cpp:1810 `ents[k]+0xa8` | entity id for the part-batch queue | 0xb0 | part-batch retag (every entity fails the id test; sibling line 1851 was translated) |
| dxgi_probe.cpp:4566 `weapon+0x358` (+4570 `master+0xf0`; `comp+0x3c8/0x3d0/0x3d4` unverified, who_shifts shows a component with 0x3c8->0x3d8, 0x3d0->0x3e0, 0x3d4->0x3e4 in fn 0x72fcd0->0x791ad0) | weapon bind component, master model | 0x368 / 0xf8 (comp fields: verify first) | `arm_flicker.weapon_draw.engine_bind.last.joint` is -1 (D2 59): held-weapon engine bind placement dead |
| native_left_weapon_socket.hpp:87 `candidate->parent+0xa8` | parent spawn id | 0xb0 | left weapon socket ownership of a weapon on the hand |
| native_weapon_visibility.hpp:156 `parent+0xb0` (children list) and :161 `*child+0xa8` (id) | child hierarchy of the arm entity | 0xb8 and 0xb0 | `weapon_visibility.hierarchy_complete` False (D2 True): the snapshot that hides game-owned weapon models never completes; `items_complete` False too (inventory item slots `+0x70/+0x190/+0x1a0` and record stride 0x38 unverified) |
| dxgi_probe.cpp:5827 `read_self(player+0x270,&packed,8)` | packed count of the component list (data at 0x270 now, count at 0x278) | 0x278 | "Test: unlock all" rune loop reads the data pointer as the count (iterates 512 junk slots; works by luck) |

Rule shape (same as the existing `lines`/`sites` entries):
`{"file":"src/dxgi_probe.cpp","contains":"read_self(thing[hand]+0xf0,&model","from":"0xf0","to":"0xf8","why":"idEntity model +0xf8 (entity map); base token not in entity.bases","source":"review/feature-parity"}`
Simplest alternative: add `thing, t, child, master, weapon, m, ents[k], candidate->parent, parent, cast_npc, pl, body(only in dxgi_probe.cpp:8421 and the file bases of native_hand_occupancy are NOT entity), contact->entity` to `entity.bases` / `file_bases` for these files.

### G4. idEntity Hide/Show rvas (v145724): what this alone revives
`native_weapon_hands.hpp:751` `visible()` (28 call sites) and `npc_weapon_pickup.hpp:207` call Hide 0x8784b0->0x8e5de0 / Show 0x87c590->0x8e9f40.
That revives: showing/hiding the mod's weapon copies (dual-wield sword copy, hip gun/crossbow spawn_def copies, the Heart/gadget hip copies),
taking a guard's sword/gun with a hand (NpcWeaponPickup) and hiding the picked-up world weapon, the hide/show half of held-weapon visibility.
It does NOT revive ordinary prop/door/body grab (physical grab uses the physics motor, not Hide/Show) - G1/G2 are needed for that.
Caveat: `visible()` also checks idEntity vtable slots +0x80/+0x88; if DOTO's vtable shifted them the test still fails silently (`visibility_skips`). Read the DOTO slots once to confirm.

### G5. Input side: do the sword and the forearm crossbow block grip? (static reading, medium confidence)
* The grab decision is `decide_grip_alone` (dxgi_probe.cpp ~16262-16301) which is skipped when `sword_owns`, `gun_owns`, `hip_draw_reserved`,
  `extra_owns[h]` or `busy` hold the hand (lines 16492-16509, 16594). Equipped weapons do NOT reserve the grip by themselves
  (comment at 15094: "Equipped weapons do not reserve a controller's physical grip"; `hand_busy()` is only carry/hold).
  `sword_owns` is set only while `sword_grip.update(in).owns` (a drawn sword held by the grip at the hip zone); `gun_owns` only while
  `gun_grip` has drawn the slot-0 item. So with the sword holstered and nothing drawn from the hip, grip is free.
* In play21 `hand_bits.empty` = 3 (both equip names "Empty"), `hand_weapon_bits` 0, so the builder `native_hand_state()` (native_hand_occupancy.hpp) returns a state and
  reports both hands free: input is not what blocks grab in the sim. D2 shows `empty 2, item 1, weapon 1` (sword + Blink).
* Real risks in the equip/occupancy builder that sim cannot show: it still uses D2 constants `*inventory+0x68` (slot pair), `part+0x18`, graph nodes
  `*graph+0x190/0x1d0`, name lists `+0xa0/0xb0`, `part+0x21`, `descriptor+0x138` (hand_occupancy.hpp:33-64; votes for 0x138 are "mixed").
  If Billie's sword (right) or crossbow slot is misread as occupied, `slot1_block`/`sword_block` stop grips (16492). Weapon naming:
  `gun_equip_name.hpp:23` `is_barrel_equip_name` accepts only `"Gun"` or `*rossbow*`: Billie's Voltaic gun equip name is not "Gun" (name contains
  "Gauss"/"gun" - `is_gun_equip_name` catches it by substring, barrel routing does not), and hook mines hit `is_gadget_equip_name` ("mine") correctly.
* Recommendation: run `tools/doto/hand_state_probe.py --states` with the knife drawn and with the crossbow equipped (ONE session) and read
  `left_equip`, `hand_bits.*`, `sword_grip.actions`, `xr-live-status.json` `grab_focus`/`grab_los`. Add `consumers_ok_` (native_interaction_aim.hpp:271) and
  the equip names to the status JSON: today the install-time consumers (Block / Slash rate / Choke scope / Choke offer) report nothing when skipped.

Priority within section 0: G1 + G2 (rules above) first; G3 rows 4932/4625/4636 (held prop), 10967, 16019 next; then re-test grip on a bottle, a box, a body and a door.

---------------------------------------------------------------------------------------------------

## 1. Summary counts

Inventoried: 52 launcher features (`tools/default_features.json`) plus ~45 install-time sub-systems found in `StartProbe`.
(Counts are of rows in the tables below; sub-systems that share one cause are grouped.)

| verdict | features |
|---|---|
| alive in DOTO (counter nonzero, comparable to D2) | 24 |
| dead - cause known, fix proposed here or already staged | 21 |
| dead - needs RE (real code change), no cheap fix | 6 |
| not exercised by the DOTO runs (needs scenario) | 18 |
| not applicable to DOTO (D2-only; DOTO counterpart noted) | 8 |

## 2. Tables (feature | how measured | D2 | DOTO play21 | verdict | cause file:line | proposed fix | conf.)

### 2.1 Input / controls
| feature | measured | D2 | DOTO | verdict | cause | fix | conf |
|---|---|---|---|---|---|---|---|
| controller reads/actions routed | `native_controller_reads`, `controller_actions_routed` | 74579 / true | 93199 / true | alive | | | high |
| nonzero controller input to the game | `native_nonzero_vr_controller_reads` | 11459 | 0 (3126 in play13-16,20 walk) | alive when the scenario sends input; play21 sends none | | | high |
| controller input without window focus | `native_pad_window_gates`, `native_pad_window_overrides` | 5664 / 3408 | 5717 / 0 | not exercised (overrides need an unfocused window); gates alive | | run a sim with the window unfocused | med |
| smooth/snap turning | `native_turning_applied/verified` | 2303/2303 | 3525 in idle play12; 1 in walk runs | alive (scenario-dependent: only the head-turn scenarios turn) | | | high |
| head follow | `native_head_follow_applied` | 2303 | 3525 (play12) | alive | | | high |
| hold-Y menu / pause | `hold_y` | enabled, 0 fires | same | not exercised | | scripted Y hold | low |
| physical crouch | `crouch_*` | | not in result | not exercised | | scenario: lower the headset | low |
| power wheel dial | `wheel_zone`, `wheel_frames` | 0 | 0 | not exercised | dxgi_probe.cpp:16039 `wheel_controller+0x171` ("mixed 0x171/0x179") unverified | fn_diff wheel controller | low |
| left-hand aim | `native_left_aim.applied` | 3084 | 5251 | alive | | | high |
| hand-aimed shots | `shot_aim`, `hand-shots` | installed 1, 0 calls | installed 1, 0 calls | not exercised (needs firing); the crossbow/Voltaic fire pins fail (pin-audit #10) | native_ranged_fire.hpp | needs RE (Voltaic gun) | high |
| hand-aimed Blink -> DOTO Displace | `blink_aim.installed` | true | false | dead: D2 Blink power function 0x6205e0 maps to DOTO 0x674200 = Blink only; Displace is a different sensor power (`arkCpntPowerSwapper`) | native_blink_aim.hpp:211; recoveries log "Hand-aimed Blink disabled" | implement the hook from powers-re.md section 1.6 (sensor job -> cam+0x252b8 direction, 36 B) | med |
| steady power trigger / power gestures | `gesture_*`, `gadget_aim.left_power` | true | false | not exercised (left_equip Empty in the sim) | | equip a power then gesture | low |
| hand mine/grenade aim | `gadget_aim.enabled`, `grenade_throw` | enabled, 0 | enabled, 0 (throw_hit posts 162) | alive (hooks up), not exercised | | throw a grenade | med |

### 2.2 HUD / menus
| feature | measured | D2 | DOTO | verdict | cause | fix | conf |
|---|---|---|---|---|---|---|---|
| native HUD art on wrist/prompts/markers | `native_hud_children.published/marker_published/nonempty_images`, `wrist_hud.*` | 5971 / 5968 / 4999 | 0 / 0 / 0 | dead in v145721; Iggy getter rule now in tree (native_interaction_prompt.hpp:70 text rule), unverified | Iggy value-ref frame 0x30->0x40 | re-run; also check `file+0x1608` / `iggy+0x78` ownership (translate says same 42/42) | med |
| crosshair art | `crosshair.game_art_captures/direct_eye_draws/setup_owner_passes` | 6162 / 5440 / 3083 | 0 / 0 / 0 | dead (depends on the HUD child API above; `eye_owner_rejects[1]`=76650) | native_hud_child_draw.hpp:351 | same fix, re-read | med |
| crosshair aim apply | `native_crosshair_applied` | 3045 | 5473 | alive | | | high |
| hud rect scale / holder hide | `hud_holder_hidden`, `hud_rect_*` | 70874 | 2 | dead-ish: same HUD child path | | same | med |
| menu entry (VR Settings in pause/main) | `menu_entry_patched/mismatched/clicks` | | 2 / 0 / 1 (play19) | alive | menu-entry.md | | high |
| VR settings page, options page | `native_options_page`, `vr_settings_layers` | | [1,2,1,1,...] (play19) | alive (rendered), layers 0 in sim | | | med |
| menu quad / pointer | `xr_menu_quad_layers`, `native_menu_pointer_moves` | 1899 | 717 (play19), 0 play21 | alive in menu scenarios | | | high |
| controller glyph swap (Quest labels) | `glyph_swaps` | 0 (D2 glyph table not bound in that run) | 10958 | alive | | | med |
| held notes / maps tablet | `note_tablet.kind.verified` | true | false (sig failure rva 0x7f5da0) | dead in v145721; FNV pins re-hashed in current tree (pin-audit #4) | native_note_kind.hpp:93 | re-run | med |
| messages / dialogue art | `native_message_publishes`, `dialogue_*` | 0 | 0 | not exercised | | | low |
| wrist HUD values (health/mana/ammo) | `wrist_hud.health_valid` | false | false | not exercised + Iggy | native_wrist_hud.hpp:102 `0xf0u` field offsets (mixed) | re-verify after Iggy | low |
| world prompts / "prompt_world" | | false | false | not exercised | | | low |
| comfort vignette | `comfort.vignette.frames_shown` | 2014 | 0 in every DOTO play | not exercised or dead: D2 shows 2014 with `anchor_jerk_events` 71; DOTO `anchor_v_peak` 0 means the anchor never moves in the sim | | walk + snap turn scenario with vignette on | med |

### 2.3 Hands / avatar / body
| feature | measured | D2 | DOTO | verdict | cause | fix | conf |
|---|---|---|---|---|---|---|---|
| tracked arms / full body (Billie) | `full_body.arm_uploads/solves`, `native_body_upload.accepted` | 6166/3084 | 10934/5485 | alive | | | high |
| avatar fit / calibration | `avatar_fit_template_verified` | true | true | alive | | | high |
| finger curl / touch sensors | `fingers.mapping` | touch | touch (play15), legacy (play21) | alive | | | med |
| finger surface (stop on surfaces) | `finger_ik.hits/slabs` | 4722 | 0 | dead via G2 | native_initial_contact.hpp:36 | G2 | high |
| hand reach clamp | `hand_clamp.contact_frames`, `reach_clamped` | 131/2354 | 5065 (play15), 0 (play21) | partially alive; continuation/exclusion path dead via G2 | | G2 | high |
| physical hand bodies | `hand_body.contact_frames/root_moved_ticks` | 449/139 | 0 / 0 | dead via G2 (installed true, ticks 5492) | | G2 | med |
| torso fit (Billie) | `native_torso_fit` D2VR_GAME_DOTO branch | n/a | | alive per billie-avatar.md | | | med |
| held clamp / carried stops at walls | `held_clamp.*` | 0 | 0 | not exercised (needs carry) | | | low |
| left weapon socket / bind | `left_weapon_bind_receipt` | enabled | enabled | alive; id reads at native_left_weapon_socket.hpp:87 stale | G3 | | med |
| weapon visibility | `weapon_visibility.hierarchy_complete/items_complete` | true/true | false/false | dead | G3 rows | | high |
| arm hidden / shows | `arm_hidden`, `arm_shows` | | | alive | | | low |

### 2.4 Combat
| feature | measured | D2 | DOTO | verdict | cause | fix | conf |
|---|---|---|---|---|---|---|---|
| sword contact / weapon collision | `weapon_collision.contact_frames/events` | 6096/20 | 0/0 (play15: 10130/4) | alive for world contact (play15); NPC contact classification dead via G3 (cast_dead) | dxgi_probe.cpp:10967 | G3 | high |
| stealth kill by contact / contact damage | `sword_contact.*` | 0 | 0 | not exercised; `cast_dead` wrong (G3); `sword_contact.stealth` needs awareness read (native_assassination_receipt, skipped) | | G3 | med |
| block / unequip veto | `block` [0..], `block_unequip_vetoes` | 0 | 0 | not exercised; pin-audit lists no failing pin for NativeBlockEligibility | | scenario: block with a guard attacking | med |
| slash speed | `slash_scene_rate.enabled`, `slash_speed` | true / 1.7 | false / 1 | dead: real code change (pin-audit #9: 0x86cbf0->0x8d7ed0 frame 0x60->0x90, [rcx+0xcc]->[rcx+0xd4], 3 callers unmatched) | native_slash_scene_rate.hpp:33-41 | needs RE | high |
| closer takedown trigger (choke scope) | `takedown_range.enabled` | true | false | dead in v145721; text rule staged (pin-audit #5) | native_choke_distance_scope.hpp:28 | re-run | med |
| choke offer observer | `choke_observer_enabled` | true | false | dead: real change (pin-audit #8) | native_offered_choke.hpp:44 | needs RE | high |
| takedown prompt / gaze takedown | `gaze_takedown_frames` | 3087 | 5082 | alive | | | med |
| takedown anchor / sensors | `takedown_anchor`, `takedown_sensors` | | frames 0 | not exercised | | scenario: choke a guard | low |
| corpse chop / hit height | `contact_hits`, `corpse_chops` | | | not exercised | | | low |
| ranged fire (crossbow / Voltaic) | `native_ranged_fire` verify | | | dead (pin-audit #10) | native_ranged_fire.hpp:252 etc | needs RE | high |
| shoulder carry / body carry | `shoulder_carry`, `corpse` | 0 | 0 | not exercised; body flags `+0xd1` fixed in 6012/rigid_body by entity rule | | carry a body | low |

### 2.5 Interaction
| feature | measured | D2 | DOTO | verdict | cause | fix | conf |
|---|---|---|---|---|---|---|---|
| hand-aimed interaction rays | `native_interaction_rays/routed` | 18468/18438 | 0/0 | dead | G1 | G1 | high |
| physical grab (bottle, box, body) | `physical_grab_picked/moved`, `grab_los` | 0 in D2 sim too | 0 | dead/blocked by G1+G2 (headset report), not measurable in sim | G1, G2, G3 | G1, G2, G3 | high |
| hold/carry visuals (held render pose) | `held_render_pose.*` | 0 | 0 | not exercised; entity model reads stale (G3) | | G3 | med |
| doors | `sword_grip.door_controller.queries` | 2993 | 0 | dead: door owner skipped (pin-audit #7/#12), plus G1 | native_door_*.hpp | other reviewer | high |
| notes, loot, pickpocket, talk by hand | `loot_offers`, `talk_offers` | | | dead via G1; `native_talk_offer.hpp:54/83 sensor+0xa8` unverified | | G1 | med |
| take guard weapon | `NpcWeaponPickup::verified` | | | staged (pin-audit #6 + v145724 hide/show) | | re-run | med |
| gadget pickup / drop | `native_gadget_drop.hpp:41-48` D2 pickup defs | regular/sticky/stun/springrazor/heart | DOTO index has `ammo_grenade_regular.def`, `ammo_springrazor.def`, `ammo_hook_mine.def`; NOT `ammo_grenade_sticky.def`, `ammo_stun_mine.def`, `heart_pickup.def` | partly dead: sticky, stun mine, Heart defs do not exist; hook mine is a new gadget | native_gadget_drop.hpp:41-48 | replace stun_mine entry by `hook_mine_equipment` / `models/interactive/pickups_entities/ammo_hook_mine.def` / `hook_mine.inventoryitem`; drop `heart`; verify sticky with the index | med |
| Test: unlock all | `test_unlock_applied` | | | partly: cvars `ark_dbg_infinite_coins`, `ark_blackMarket_unlockAll` translated; rune wallet `rune_pickable.inventoryitem` exists in DOTO; count-offset bug G3 | dxgi_probe.cpp:5827 | 0x278 | low |

### 2.6 Powers
D2 powers (Blink, Far Reach, Domino, Devouring Swarm, Shadow Walk, Mesmerize, Possession, Doppelganger, Dark Vision, Bend Time) vs DOTO
(Displace, Foresight, Semblance, Void Strike / Billie's Void arm, bone charms). Reference: `powers-re.md`.
| feature | verdict | notes |
|---|---|---|
| Blink hand aim | replaced by Displace: dead (see 2.1) | powers-re.md 1.6 is the plan; D2 Blink hook is useless in DOTO |
| Voltaic Gun (replaces pistol) | dead: fire pins differ (pin-audit #10), barrel routing name check (`gun_equip_name.hpp:23`) only knows "Gun"/"rossbow" | add "Gauss" after reading the live equip name |
| Foresight (Dark-vision / Timepiece-like) | not applicable to the D2 timepiece lens guard (`timepiece.guard` false is expected, secondary_view_guard not installed) | camera: no authored-view exception needed (powers-re.md 6.3) |
| Semblance (Possession/Doppelganger analogue) | not implemented; authored face-taking animation only (powers-re.md 6.2) | |
| Devouring Swarm contact kill (`swarm_contact_kill`) | not applicable (no swarm); installed flag is D2-only | `native_melee.hpp:100` comment: agent record at entity+0x360 (now 0x370) not needed |
| D2 rune wallet / blackmarket | partly applicable (see unlock row) | |

### 2.7 Rendering / upscalers / AA / spacewarp / comfort
| feature | measured | D2 | DOTO | verdict | cause | fix | conf |
|---|---|---|---|---|---|---|---|
| paired stereo, eye capture, head tracking | `stereo_frames`, `head_pose_writes` | 3087/6174 | 5486/10972 | alive | | | high |
| FSR / TAA history / jitter | `fsr_replaced`, `taa_history_installed`, `jitter_*` | true | true | alive | | | high |
| DLSS | `feedback_render.dlss.provider_found/offered` | true (nvngx from Helldivers 2) | false | not applicable to the sim environment: provider search finds no DLL under `build/doto-home` | | point the provider search at the shared DishonoredVR ngx folder | med |
| alternate native TAA (performance mode) | `alternate_native_taa_abi` | true | false | dead in v145721; five FNV pins re-hashed in tree (pin-audit #3), view struct moved -0x10 | alternate_native_taa.hpp:15-21 | re-run; spot-check taa_history flags `0x3a37` (mixed) | med |
| shadow sharing between eyes | `shadow_share.installed`, `reason` | true | false, `prologue_mismatch` | dead in v145721 (`render_pass_map.install` false); pass callbacks re-derived in 2576249 | render_pass_map.hpp | re-run | med |
| alternate eye / AFW / rig mask | `alternate_eye.*` | all 0 (off) | all 0 (off) | not exercised (default off) | | | high |
| spacewarp safe-mid | `spacewarp_safe_mid.armed/windows` | 3007/60 | 0/0 | not exercised: D2 ran with `build/spacewarp-safe-mid`; DOTO knob absent; pacer in DOTO lock/unlock flaps (22/21 vs 1/0) | | rerun with the knob | med |
| Bink pre-rendered videos | `scripted_view.cutscene.bink.installed/frames` | true/1483 | false/5965 | alive (frames counted; installed flag read after disable) | native_bink_hooks.hpp | none | low |
| letterbox hide, cutscene hold-skip | `letterbox_hidden` | 0 | 1584 | alive | | | med |
| sun sprite aspect | `sun_sprite_aspect` | [1,1] | [1,1] | alive | | | high |
| comfort camera smoothing | `comfort.samples` | 3084 | 5473 | alive | | | high |
| bodyless motion guard | `bodyless_motion` | [1,...] | [0,...] | rule staged (pin-audit #2) | bodyless_motion_guard.hpp:27 | re-run | high |
| native cvar overrides | `native_cvar_overrides` | 6 | 5 | alive (focus cvars handled separately) | | | med |

## 3. Other D2-only data found (names / defs / paths that do not exist in DOTO)
Searched the DOTO resource index (`base/game1.index`): present: `models/campaign/dlc01/boat/midnight/assassin_sword.def`,
`models/weapons/dlc01/player/billie/{assassinsword01,knife01,gaussgun01}/ply_billie_*.def`, `hook_mine*.def`, `ammo_hook_mine.def`,
`concussion_grenade*.def`; absent: every `models/campaign/dunwall/midnightassets/*` def, `ammo_grenade_sticky.def`, `ammo_stun_mine.def`, `heart_pickup.def`, any `pistol`/`gun_corvo|emily` def.
| file:line | D2 name | consequence | proposed |
|---|---|---|---|
| native_weapon_hands.hpp:149, 729, 1055 | `models/campaign/dunwall/midnightassets/sword_player_midnight.def` (the cinematic sword copy: no physics, no audio) | dual-wield sword copy (`dual_wield`, hip sword, take-a-sword) cannot spawn: `dual_wield.guard.unverified` 5523, `copies` 0 | candidate `models/campaign/dlc01/boat/midnight/assassin_sword.def` (needs a live check that it has no physics/audio like the D2 asset) or a Billie def under `models/weapons/dlc01/player/billie/`. medium-low |
| native_weapon_hands.hpp:955-957, 1055-1057, 2547 | `gun_{corvo,emily}_midnight.def`, `crossbow_*_midnight.def` | gun/crossbow hip copies never spawn (also the D2 character split `emily_` reads the player decl name) | no D2 equivalents; Voltaic/crossbow copies need DOTO defs (`gauss_regular`? `ply_billie_gaussgun01.def`) |
| body_renderer.hpp, native_torso_fit.hpp, controls_guide_image.hpp | corvo/emily body resources | handled by `D2VR_GAME_DOTO` blocks (Billie) | none |
| native_gadget_drop.hpp:41-48 | stun mine, sticky grenade ammo def, heart | see 2.5 | see 2.5 |

## 4. Prioritized fix list
1. **G1** four-literal rule: native_interaction_aim.hpp:1577 callers (hand-aimed interaction/grab/hover).
2. **G2** one rule: native_initial_contact.hpp:36 slot 2 0x99d50 -> 0x9bfe0 (all initial-overlap and exclusion casts).
3. **G3** entity offsets: dxgi_probe.cpp 4932/4625/4636 (held model), 10967+11021 (dead flag), 16019 (player origin), 6989 + native_held_object 504/528 (master), 8421, 1810, native_left_weapon_socket 87, native_weapon_visibility 156/161 (entity children list 0xb0->0xb8, id 0xa8->0xb0).
4. Re-run a v145724 sim: record `native_interaction_rays/routed`, `hand_body.cast_continuations`, `finger_ik.hits`, `weapon_visibility.*`, `native_hud_children.published`, `note_tablet.kind.verified`, `alternate_native_taa_abi`, `shadow_share.installed`, `takedown_range.enabled`, `bodyless_motion`.
5. Add observability: export `consumers_ok_`, `NativeDoorInteraction` installed, hand-state build failures and equip names to xr-live-status.json so a skipped consumer is visible (today only 2 of ~30 silent fallbacks go to xr-recoveries.log).
6. Weapon copy decl for dual wield (sword_player_midnight -> a DOTO def), gadget defs (hook mine), barrel routing name for the Voltaic gun.
7. Displace aim hook (powers-re.md 1.6), Voltaic fire path (needs RE), slash scene rate, choke offer observer, doors (other reviewer).
8. Scenarios to add to the sim for the "not exercised" rows: grab a prop (place a bottle), choke a guard, throw a grenade, block, walk+snap turn with vignette, unfocused window, equip each power and gesture.
