# Two active hands: plan (2026-10-31)

Goal (user):
- Both hands are fully active.
- Any weapon can be held in either hand and used with that hand's trigger.
- A pistol, crossbow or unarmed grenade can be dropped or thrown and picked up with either hand.
- A power's animation and effects show on the hand that casts it.
- The user also wants two pistols, crossbow + pistol, and guard weapons.

Everything stays behind the launcher feature "Dual wield" (knob `build/dual-wield`). With the knob off, the game behaves exactly as without the feature. Background: docs/research-dual-wield.md and artifacts/claude/dual-wield/A-E.

## The rule for two weapons

The engine has one sword slot (0) and one slot (1) for a gun, crossbow, grenade, mine or power. One record per item decl.

**Native first.** The sword plus anything else in slot 1 is native in both hands. This is the hand mirror, already in v754.

**Second of a kind = mod-owned.** When both hands want slot-1 items, or both want swords, one hand holds the native item. The other holds a mod-owned copy:
- the copy model is drawn at that hand;
- its effect is a native call:
  - a ranged weapon fires a real projectile from its barrel;
  - a sword deals contact damage;
- ammunition comes from the native inventory.

**How the second weapon comes into a hand:**
- **Wheel.** The wheel gives the item to the hand that opened it. The other hand keeps what it held: if that was a gun or crossbow, it becomes mod-owned. Choosing the item the other hand already holds gives both hands one (two pistols).
- **Pickup.** Picking up a dropped gun, or a guard's weapon, with a hand gives it to that hand. It is native if its slot is free, otherwise mod-owned.

## Workstreams (agents) and file ownership

Only the owner edits its files. The lead does all of the following:
- `src/dxgi_probe.cpp` wiring;
- `src/hand_loadout.hpp` (v2: one item per hand);
- `src/game_xr.hpp`, builds (DLL numbers v755-v769) and launcher packaging.

| Agent | Owns | Delivers |
|---|---|---|
| A gun-copies | `src/native_weapon_hands.hpp`, `src/vr_controls.hpp` (GunGrip only), `src/held_copy.hpp` | Gun/crossbow drop and throw as a flown copy, picked up by either hand. A held copy of any item def at a given hand (the mod-owned second weapon and guard weapons), with a grip offset. |
| B ranged-fire | new `src/native_ranged_fire.hpp` + test | Fire a pistol or crossbow shot from a given muzzle pose on the game thread. It replays the native sequence `63b5f0 -> 639970 -> 639360 -> 63b3c0`, uses native ammo, and has its own cooldown and reload. It also has probe counters: projectile created, components started, moved, hit. |
| C power-hand | `src/native_attachment_pose.hpp` (+ new header if needed) | Power effects and hand effects of slot 1 drawn at the hand that holds slot 1 (the right hand when mirrored). Done by cross-arm attachment substitution, or whatever the RE shows is right. |
| D grenade-drop | new `src/native_gadget_drop.hpp` + test | An unarmed grenade, stun mine or spring razor put down or thrown as a world pickup, and taken back by either hand, without arming it. Research first; implement only what the engine supports. |
| E guard-weapons | new `src/npc_weapon_pickup.hpp` + test | The game's dropped NPC weapons (msg 0xb6, the DEAD context) found near a hand. A take hides the native entity and returns its kind and def for A's held copy. Guard sword hits use the player's sword damage by name. |
| F sim tester (later) | none (runs the game) | Probes and scenarios on the integrated DLL. Runs only after the lead's build, and only when the desktop is free. |

## Acceptance (sim first, then the headset)

1. Sword in the left hand, pistol in the right: unchanged from v754.
2. Drop the pistol with the right hand, pick it up with the left; it fires from the left.
3. Two pistols: both triggers fire real bullets from their own barrels, from the shared ammo.
4. Crossbow in the right hand and pistol in the left: both fire.
5. Put a grenade down unarmed and pick it up with the other hand; throwing it still arms it.
6. Blink and other power effects drawn on the right hand when the power is there.
7. Take a dead guard's sword and hit with it; take an elite's pistol and fire it.
8. Knob off: identical to v754 (review).

Rules for agents:
- No DLL builds and no game.
- Compile your own unit tests standalone, with unique output names.
- No git state changes. Build on other sessions' uncommitted work.
- Each agent writes its report to `artifacts/claude/dual-wield/fleet/<agent>.md`.

## Status: v757 (2026-10-31)

All five agents delivered. Their reports are in `artifacts/claude/dual-wield/fleet/`. The lead integrated their work, and a sonnet review found nothing that changes knob-off behaviour.

The 12 findings that only matter with the knob on are fixed:
- A gun copy no longer counts as a blade.
- A hand with a mod-owned weapon draws nothing else.
- A guard weapon is released when a swap clears it.
- The JSON is valid.
- Only grenades use the unarmed-drop fist.
- Gun pickup is limited to 0.6 m, native or mod-owned by `r.decl`.
- A lying gun is taken only into an empty slot 1.
- There is no extra in the gun hand while slot 1 is drawn.
- Powers cross independently of power-native-fingers.
- NpcWeaponPickup tears down after the knob goes off.
- NativeRangedFire resets when gameplay ends.

Build: `artifacts/claude/dual-wield/build-dw.cmd` runs 13 tests, then the DLL. Not yet sim or headset tested; the game was in use.

Headset checklist for v757 (launcher feature "Dual wield" on):
1. Pistol in the right hand: let go away from the hip, and it falls (or flies with a throw). Squeeze at it with the left hand: it is back and fires from the left.
2. Left pistol, then open the right wheel (hold A) and choose the crossbow. The crossbow goes to the right hand and the pistol copy stays in the left. Each trigger fires its own weapon. Check the ammo counts and the reload pause.
3. Two pistols: with a pistol in one hand, open the other hand's wheel and choose the pistol again. After about 1.2 s that hand gets a pistol too. Status: `sword_grip.dual_wield.fire`.
4. Grenade: close the fist and open it slowly. It is put down unarmed; pick it up (the grip's Use). Close the fist and throw: it cooks and flies armed.
5. Grenade, mine or power in the right hand: its model and effects are at the right hand. Check this on Blink and Far Reach.
6. Knock out or kill a guard and squeeze at his dropped sword or pistol with a free hand. It is in that hand; the pistol fires.
7. With the knob off, everything is as in v754.

## Status: v758-v761 (2026-10-31, after the v757 headset test)

User, headset v757: two pistols never appeared; a gun chosen in the right wheel showed in the left hand (or only when a pistol was already
in the left); no power right + pistol left; the right hand could not pick up a thrown pistol; the left could not throw the crossbow; the
left hand's sword showed its hilt open. "The weapon fires, not the hand; a power aims like a weapon; the mod adapts the hands to the weapon."

v758 (previous session): the game's own sword and gun ride the other hands through the arm-joint cross (`set_cross_mask(3)`) instead of
model copies, so each keeps its animation, barrel and muzzle; either hand picks up either lying gun; a gun chosen while the other hand holds
a power or grenade becomes this hand's copy and the power is equipped again (`Op::slot1_restore`); events journal `artifacts/dual-wield-events.jsonl`.

v759-v761 review fixes (launcher `DishonoredVRLauncher-v761.exe`):
- Root cause of the missing second weapon: the pending wheel route was cancelled whenever `gun_grip.held()`, and hold-to-hold keeps the
  other hand's gun gripped. Now only the wheel's own hand taking a gun from the hip after the commit cancels it (`HandLoadout::wheel_route_lost`, tested).
- The same-item route (two pistols) needed a sector seen by the hand dial; a thumbstick choice now counts too.
- A sword left drawn in a hand that takes a mod-owned weapon is put away, never dropped (SwordGrip saw that hand as busy and never sheathed it);
  the grip stays the copy's until let go.
- Journal `artifacts/dual-wield-events.jsonl`: `wheel_lost` (why a choice never reached its hand) and `gun_holster` (a let-go that holstered: hip, busy, claim, ready).

Review, no change needed: aim, crosshair and shots follow the slot-1 hand (`aim_hand`) and the drawn gun's barrel (`barrel_aim`, crossed
too); Blink and the other powers aim from the hand that holds them and their arm FX ride it (cross bit 0); VrCombat swings from the sword's
hand; the mod-owned gun fires from its copy's barrel; the sword hand's trigger never reaches native RT (combat owns it).

Not verified live (the sim's test save no longer loads, and the desktop was in use): the crossed weapon's roll in the hand (joint-59 y180,
inferred), the mod-fired bullet, the left-hand crossbow throw. Headset check for v761: the v757 checklist above, plus the items in the first paragraph.

## Status: v762 (2026-10-31, after the v761 headset test)

User, headset v761: two pistols came, but the left pistol could not always be thrown; crossed items (pistol, crossbow, grenade,
mine in the right hand, the sword in the left) sat wrong, muzzle up; only the native gun fired (two pistols, pistol + crossbow);
powers worked well (cast and aimed from the right hand); held A also swims up; the ring's bottom (powers, mines) was hard to choose;
the wheel's dark backdrop should go. Muzzle shots of the native pistol and crossbow: good.

Causes found in the v761 logs (`dual-wield-events.jsonl`, `xr-live-status.json`):
- `gun_item_frames` 0 for the whole session: the weapon entity's decl name (`+0x2e8`, `+0x48`) never contains gun/pistol/crossbow for the
  player's weapons (it does for world props). So `mirror_lookup(slot 1)` never knew the gun: all 20 native drops were refused
  (`guns.failed` 20, `native_drops` 0), and copies were never measured (identity grip = muzzle up along the fist).
- `fire.refused.no_weapon` 29 of 29: the inventory record's `+0x10/+0x18` pair never typed as the weapon component/decl.
- y180 on the crossed joint 59 is a bone-axis guess and was wrong.

v762:
- The weapon kind comes from the weapon entity's own components (`entity+0x268`, RTTI `arkCpntWeaponGun 0x1b53f58`,
  `arkCpntWeaponCrossbow 0x1b9cbf8`); the name is a fallback.
- A crossed item's joint 59 is the owning arm's own grip relative to its drawn hand, mirrored across the hand's left axis onto the other
  drawn hand (`power_hand::mirrored_grip`, tested on a mirrored pose); y180 only when the hands are not fresh.
- Copies keep the last measured grip per kind (`kind_offset_`).
- NativeRangedFire finds the component through the record's weapon entity when the record pair does not type (`entity_finds`).
  Expected limit: a copy whose kind is not the equipped one (pistol copy beside the native crossbow or a power) has an unstarted
  component and is refused `not_ready`; two pistols use the equipped pistol's live component.
- The right hand's wheel is hold B (A stays native); B's holster hold is off with dual wield.
- The hand ring is read along its own plane (WheelCursor `plane_up`) and its own hand is the cursor at once.
- The hand wheel is keyed like the dialogue choices (its dark backdrop transparent).

## Status: v763 (2026-10-31, after the v762 headset test)

User, headset v762: the hold of crossed items still very wrong; the left hand could not let go of its gun; only the last active
weapon fired; the keyed wheel looked pale and its choice was still sometimes lost. Wanted: the right hand holds every weapon exactly as
the left, the left the sword exactly as the right; throw and catch (juggle) every held item; Boneworks / Bonelab / Blade & Sorcery feel.

Found in the v762 logs:
- `left-weapon-bind-receipts.jsonl`: the left item's world frame equals joint 59 to the millimetre (the left tag is identity).
- In the head frame, crossed items swung from correct to 90 degrees rolled to reversed: the v762 formula read the left arm's joint 59,
  which (mirrored) was the player's fist or the native fire clip, not the item's hold.
- `fire`: no_weapon 63 of 63, entity_finds 0: the inventory is not among the player's components (the player state machine +0x328 has it).
- No gun_drop/holster events for the left hand: GunGrip never held its gun (busy).
- The wheel layer: premultiplied colour (.046,.050,.056) at alpha .502, vignette to 1 in the corners (world-ui-1 dump).

v763:
- `Tracking::mirror_native` (FullBodyDriver): a hand holding the other arm's item takes that arm's native clip mirrored onto its skeleton
  (power_hand::mirror_joint); with that, y180 is the exact mirror (proved in native_power_hand_check, plumbing in full_body_driver_check).
  The right hand also gets the native fire/reload clips. A hand holding a gun copy takes the native gun's hold as well.
- NativeRangedFire: inventory from the state machine; unstarted components fire (counted `unstarted_shots`).
- GunGrip: lane readiness no longer blocks holding a drawn gun with dual wield; journal `gun_kept`.
- Juggling: guns caught in flight; gently tossed grenades caught by either hand (`gadget_catch`, slot 1 follows, the last one re-equipped).
  Mines and razors are not tossed unarmed (live mine component in their pickup).
- Wheel: `PSUnback` un-composites the layer (lab render artifacts/claude/dual-wield/wheel-lab-v763.png); the last sector holds when the
  hand drifts back to the centre.

v764-v765:
- Live memory probe on the user's chapter-4 save: the inventory has 816 records; `NativeRangedFire::locate`/`pool_count` stopped at 512,
  so a weapon past record 512 (both of the user's) was never found (`no_weapon`). The caps are gone (native_ranged_fire_check: 600 records).

v766 (sim tooling, 2026-10-31):
- The native wheel ignores the sim stick on that save (reason 6 `no scope` only; equip never changes), so sim scenarios equip directly:
  test commands `equip pistol|crossbow` (the wheel's own 835520/8024d0 equip with the weapon's current ammo, comp+0x98),
  `extra left|right pistol|crossbow|none` (a hand's mod-owned copy, as the wheel's second-weapon route) and
  `gadget spawn stun_mine|spring_razor|grenade` (a free pickup under the right hand; status `gadget_drop.probe.mine_started` says whether
  the pickup's own mine component started: the gate for mines joining toss/catch).
- `vr_selftest.py --script`: a segment's `"command"` (split by `;`) is sent once when the script reaches that segment.

Sim v766 (user's save, boat interior; test commands instead of the wheel):
- Left native pistol let go -> falls -> right hand catches it in flight (melee swaps to 0) -> native shots from the right ->
  left crossbow copy fires real bolts (2/2 created, moving) -> juggled back to the left. Shot aim: 6 launches (2 copy, 4 native).
- The right hand's native pistol hold is the mirror image of the left's (mirror_native), also measured: kinds[pistol] on the right
  = the left's offset with y negated.
- A copy with no measured native twin (a crossbow never equipped natively) used the identity grip: barrel along the fist tube, muzzle up.

v767-v769:
- held_copy::grip_constant: pistol and crossbow carry the native left-hand holds measured live (barrel ~52 degrees below the hand's
  forward), with their side; sim v768: both copies (right mirrored, left as measured) sit like the native pistol.
- body_ik: the mirrored left sword arm gets the sneak-stance forward flip with the offset negated (mirror_joint keeps a local turn,
  negates a local offset); full_body_driver_check covers it.
- NativeGadgetDrop: mines and razors on (mines_enabled default true; their pickup's mine component starts as on the level's loot mines,
  state comp+0x108 read into stun_probe/razor_probe every second while one lies); with dual wield a gentle opening puts a mine down
  unarmed like a grenade, a throw motion places it natively.
- A put-down item's own physics body (started on runtime spawns since this save) met every landing sweep (893 self hits) and the item
  fell through the deck: its body is excluded from its sweeps, mapped from the self-hit's handle (native_cast_exclusion takes props only).
- `equip` takes regular_grenade|sticky_grenade|stun_mine|springrazor too (code 0x30).

## Status: v771 (2026-10-31, after the v770 headset test)

User, headset v770: juggling, two-hand fire and catches work. Problems: the left sword through the palm and no surface reaction; a
right-hand gun's hold broke (barrel turned) when the left took a grenade; a weapon vanished when its hand took the Heart; grenades and
mines should be held by the grip and activated by the trigger; the Heart stuck to the hand; hands and weapon jerk while running; the
hand wheel sometimes ignores the hand.

Found (logs, code; no sim: the user was in another game):
- Journal: the right hand held a pistol copy and took sector 6 (the Heart) from its own wheel; HandLoadout::update cleared the copy.
- Copies are teleported on the game thread to the hand pose drawn one render frame earlier, never carried by the player's motion
  (the arms are, since v237's note_root): at running speed several cm per frame.
- A gun copy only had a gun clip to play while slot 1 held a native gun (left_gun_model); with a grenade or the Heart there it fell back
  to the procedural fist with the measured offset.
- The right hand's sword draw is flagged incoming and starts from its remembered grip; the mirrored left had neither, so it kept the draw
  clip's open fingers until the hold was steady.
- The wheel let a resting right thumb (left wheel) or the locomotion stick (right wheel) override the hand, and the game's wheel closed on
  the same frame the stick went neutral.

v771:
- NativeWeaponHands: the hand feed carries the player's model origin read with it (render_player_root); held copies add the player's
  motion since then (status guns.held.carried).
- FullBodyDriver: Tracking::slot1_gun keeps the left arm's last calm gun clip (attachment under 20 deg/s for 0.25 s);
  Tracking::gun_clip poses a copy hand with it (mirrored on the right). Lab: full_body_driver_check.
- body_ik: a hand's first hold of an item starts from the other arm's memo for the same key, mirrored (q kept, offsets negated);
  the melee hand's draw is flagged incoming on either side. Lab: full_body_driver_check (fails without the sharing).
- GadgetGrip (throw_gesture.hpp): dual wield, the grip holds a grenade or mine; the trigger pressed while holding arms a grenade (the
  game's trigger held, released when the grip opens: the throw, NativeGrenadeThrow fed by gadget_throw_hold) or taps a mine placement;
  a grip let go unarmed drops it (NativeGadgetDrop). Test: throw_gesture_check.
- The Heart: grip held, then let go -> Op::slot1_unequip (835590 no item, code 0x20, state 64: the documented instant unequip).
- A pistol/crossbow copy displaced by a wheel choice or a native item falls from that hand (gun_drop, journal copy_displaced).
- Wheel: wheel_select (right wheel: the hand wins once it moved; left wheel: the right stick only past .6), dial dead zone 2.5 cm, a
  60 ms close tail holding LB and the last direction; journal wheel_commit adds dial/stick frames and the equipped item.
- Status: weapon_collision.events_side / blade_side per hand (the left sword's contact in the next headset log).

## Status: v773-v774 (2026-10-31, after the v772 headset test)

User, headset v772: two pistols/crossbows broke; the right hand could not take a pistol; the left pistol pointed the wrong way; a mine
could not be placed by the right hand; the left sword seen through the palm and its box hit not registered; the hand wheel stopped
answering once the hand passed through it (wanted: depth-independent, drawn in front of the hand at its distance); the Heart vanished
instead of falling; low frame rate.

Found:
- Journal: a left pistol copy was displaced 1.3 s after the wheel gave it: HandLoadout::update cleared the new slot-1 hand's copy even
  though slot 1 only held a power; the extras pickup refused a slot-1 hand holding a power (slot1_ex counted powers).
- held.kinds[pistol] (measured live) had the barrel 45 degrees up (95 degrees off the game's hold): a hold read mid-clip.
- Status: blade casts and contacts on the left (85 events vs 95 right): the left blade does collide.
- The game's wheel selects by its pointer (mouse hover); in dual wield the pointer came from the hand's aim ray on the quad, so a hand
  behind the quad selected nothing (also why the sim never selected by stick).
- Perf: gameplay frames ~17 ms (59 fps) in both of today's sessions, desktop Present waiting ~7.4 ms on the GPU (GPU-bound) at
  2709x2898 per eye with FSR 4 AA at 100%; the mod's XR hook ~1.2 ms; no spikes around drops/catches.
- INCIDENT: a sim launch under the other Steam account let Steam Cloud empty the save folder; restored byte-identical from the backup.
  vr_selftest now refuses --launch unless Steam's ActiveUser owns the save folder (steam_autocloud.vdf).

v773-v774:
- HandLoadout::In::slot1_item (slot1_shows_item: gun, gadget, the Heart): a copy is kept when slot 1 shows nothing; a power does not
  block pickups; the slot-1 hand's copy fires and its power does not cast (native LT zeroed).
- held_copy::plausible_grip: a measured gun hold within 45 degrees of grip_constant (mirrored per side), else measured again.
- GameXr::wheel_dial/hand_wheel_mode: the game's pointer at the ring point in the hand's direction (dial), never the aim ray (also
  through the 60 ms close tail); wheel_hand_back_m -0.06 (6 cm in front of the hand).
- NativeGadgetDrop Kind::heart (keep: never leaves the inventory): heart_pickup.def flown like a grenade, taken by either hand
  (slot1_restore re-equips it); the native Use on it is blocked; a Heart re-equipped by the wheel removes the lying one.
- Journal: mine_place/grenade_armed, heart_drop, sword_hold (hilt-to-fist mm, socket, finger mode).

## Status: v775 (2026-10-31, the sword hilt seen through the fist)

User: "I see the sword's hilt through the left fist; a clenched fist must hide it."

Found (offline, on the installed meshes: models/weapons/sword_player/sword_player.bmd6model, Emily's arm meshes from the pack,
ply_rightarm_sword_idle; Python renders of the skinned hand around the grip from 8 directions plus cross-sections):
- The left sword is an exact mirror of the right: the sword mesh is symmetric across its flat (Y), so the y180 attachment cross is exact,
  and the left hand plays the right clip mirrored. Not a mirroring bug.
- The game's own sword grip is loose: the grip (the ivory scales, mid line 11 mm along the right attachment's -Z, 13.5 x 16 mm half-sizes)
  sits ~1 cm off the centre of the finger ring (every finger's middle/tip bones 8-10 mm inside it, the palm side empty) and the fingers are
  splayed. From the flat-screen camera this never shows; in a headset the grip shows between and through the fingers, in either hand.

v775 (body_ik close_on_hilt, both hands, every sword hold including reverse and sneak):
- The sword is seated in the ring: the attachment moves 9 mm in the hold's frame so the grip's mid line passes the ring centre
  (right attachment frame (0,-.0075,-.0055), scaled with the hand; the left negated).
- The fingers close on it: index, ring and pinky turn sideways toward their inner neighbour until the middle segments touch, then each
  finger curls (02-04) until its 8.5 mm bone tube touches the grip, or opens until it clears it. Only a curled grip is changed.
- Lab: full_body_driver_check (the real clip through the driver) fails without it (bones 6+ mm inside the grip) and passes with it
  (every finger touching, never inside; neighbours touching; the left the mirror of the right). reverse_grip_check now asserts the grip
  keeps passing through the same point of the fist while the reverse blends. Render: artifacts/claude/dual-wield/v775-sword-fist.png.
- Not checked: Corvo's arms, attack/parry clips, the headset.

## Status: v776 (2026-10-31, after the v775 headset test)

User: the wheel must sit near the hand, keep the hands visible, and be chosen by pointing; Emily's weapons must be Emily's; crossbow bolts
left from the camera; no mine from the right hand; the Heart upside down in the right hand; revert the v775 sword fist (the grip was right,
the sword shows through the left fist only); weapons trail the hands when running; the left sword made no sparks on metal.

- Wheel: the ring sits on the opening hand's aim ray (wheel_hand_ahead_m .22, times the HUD size factor), placed once per opening, and
  that hand points at it (WheelPointer::update_plane, continuous direction: the native ring holds one item per owned weapon or power).
  WheelCursor and wheel_hand_back_m are gone from the game path. The hands stay tracked in logic 256 (wheel_avatar.hpp wheel_hides_avatar /
  wheel_tracks_arms) and the ring is drawn into the eyes behind the hands like a note page (NoteEyeArt ring mode; build/wheel-quad keeps the quad).
- Copies and the hip clone take the avatar's own guns (avatar_emily: the body model's resource name).
- Shots: a dual-wield gun held by its squeezed grip keeps its hand aim (ranged_aim_gate grip_holds_weapon); native-shot-aim.csv has a reason column.
- Mines: the placement ray leaves the palm of the hand that holds the mine (gadget_palm_ray).
- Heart: the cross correction is the mirror by the item's own left bind tag (power_hand::mirror_correction: z180 for heart_attach_tag).
- Running: crossed items use the arms' published frames; copies add held_copy::root_step (sim switches copystep, crossframes).
- Sparks: ImpactFxRequest carries what the hand holds (gun), not the hand.
- Sword: v775's close_on_hilt is reverted. The left fist's see-through (lab on the user's v775 poses: geometry fine) was found in the sim with
  dual_wield.depth_class: Block sets the sword's own (right) arm entity's first-person render scale (entity+0xc8) to 4 and the sword inherits it
  (render model +0x18b8), so a sword riding the left hand is drawn as a view-model over the world-drawn left fist. v777: a tracked arm with a
  non-zero scale is reset to 0 on the game thread (NativeHeldObject::world_render via NativeInteractionAim::request_arm_world), as held objects are.

## Sim v776/v777 (2026-11-01, the user's save; artifacts/runs/*v776*, *v777*)

- Wheel: wheel_ring ahead .22 m, off-ray 0 mm; pointing right/left/up/down gave wedges 0/4/2/6; the commit equipped the pointed item; 0 hidden
  uploads; 5170 ring eye draws, 0 failures; 11 of 11 captures show the hand over the ring. The game's own hover label ignores sim input.
  wheel_hands: half the frames had no fresh occluder (bunched outside the captures, likely the elixir click at the end).
- Shots: both native bolts with a squeezed grip routed (reason 0); the copy's shot is its own launch (reason 7). Mine: PlaceWeapon from the right
  palm, its launch routed. Emily: avatar_emily 1, hip_owner 1. Running: crossed reads with live frames apart by up to 75 mm (the old lag).
- Left fist: v776 Block -> right arm scale 4, sword eff 4, left arm 0 (grip drawn over the fingers); v777 the same pose stays 0/0/0 and the
  fingers cover the grip (one world_render reset per Block).
- Not shown in the sim: the Heart's orientation by eye (the correction axis 2 was applied), the left sword's sparks (the cut was too slow).
- Selftest gotchas: inject_probe takes 10..120 s sessions, so with --inject-at-title (75 s lead) a script is at most ~33 s (re-inject: 25 s lead);
  run vr_selftest with plain python (python -I hides the user-site Pillow); a stale artifacts/xr-live-status.json hides a failed injection.


## v778 (2026-11-01): the hand wheel's hands at 6 Hz

- Found from the v776 sim logs (wheel_hands: 1327 of 2587 frames refused as "tracked palettes not submitted"): while the wheel is open the arm
  uploads (0x25efe0) came exactly every 166.7 ms (102 in 17 s; the synchronized observer's records), while the game drew ~150 frames a second.
  So the tracked hands moved 6 times a second and the ring's hand occluder was refused about half the time (the ring over the hand). The 1 s
  wheel captures are a multiple of 166.7 ms, so all 11 landed on fresh frames.
- Cause (static RE, checked in the disassembly): open (logic state vtable slot 11, 0x4f5ae0) -> StartBendTime 0x4f7490 reads its decl's
  world +0xe8 / unbent +0xec / ease +0xf0 (0.05 / 0.05 / 0, compiled-in, decl ctor 0x4f4c00) -> AddBendTimeRequest 0x80eb40. AdvanceFrame
  0x59990 steps the sim n = floor(pending x max(WORLD, UNBENT) x GAME x 120 + .5) times (1/120 s quantum), so 0.05 gives 6 steps a second;
  animation (arkCpntAnimEvolved render update 0x541270 -> 0x25efe0) runs only on sim steps.
- Fix: the wheel decl's unbent scale is written 1.0 (only over the vanilla 0.05/0.05 pair, the state still listed by the logic manager,
  StartBendTime's reads pinned); WORLD stays 0.05, the player is not on the unbent list (only Bend Time's AddUnbent 0x80d8c0 puts it there),
  so the world and the player stay slowed and the sim steps every frame. Put back on VR stop. Status eye_art.wheel_step {writes, unbent,
  clocks [WORLD, UNBENT scaleA at gtm game+0x2e7428 +0x110/+0x160]}. Knob build/wheel-slow-steps: the game's 6 Hz.
- Not checked: the sim and the headset. Timers on the UNBENT clock (not identified) run at real time while the wheel is open.
- Fallback, not used: re-skin the arms every frame from prepare_head_view (0x25f200 with the packed palette; the GPU upload 0x25f930 runs only
  while model+0x1fb1 > 0, which 0x25efe0 sets to 2). It would leave weapons on the slow tick; more sim steps (v778) move arms and weapons alike.

## v779 (2026-11-01, after the v778 headset test)

User: the wheel works as wanted. Left: the weapon chosen second (the copy) jitters when running, in either hand; a mine from the wheel places
badly until tossed to the other hand (both hands; make placing a bit more forgiving); the Heart tossed to the other hand vanishes for a second;
jump on the right stick up, hold A for the right hand's wheel (mirrored with X); the elixirs on the wheel never worked by pointing.

- Copy jitter: copies are teleported at Stage0, before the step's physics, carried ahead by the last step. The session ran 70 frames/s with the
  frame time changing 1.3 ms (p50) / 5.7 ms (p99) frame to frame (xr-frame-timing.csv), and arm uploads were ~1 per frame (82k vs 85k frames):
  one step per frame of varying dt, so the last step missed by v*|dt_j - dt_j-1| (1-3 cm at a run, every frame). held_copy::root_step now
  scales it by this step's GAME clock dt over the last one's (gtm game+0x2e7428 +0xc8, written by AdvanceFrame before the stages):
  exact at a steady speed for any pacing (lab weapon_follow_lab_check: 36.6 mm p90 -> 0).
- Mine: the journal showed ~25 left-trigger presses with the grip open after a wheel mine and no placement (GadgetGrip needed the grip; a
  caught mine is gripped). A mine now places on the trigger with or without the grip, never in the frame the grip closes (a catch) or for a
  trigger already down when it arrives. Palm ray 50 cm in front of the palm (was 25).
- Controls (dual wield only): the right stick pushed up holds the jump (JumpStick: past .7, more up than sideways, let go under .3; held =
  swim up / climb); physical A opens the right hand's wheel at once, as X the left's (WheelHands right owner without the 350 ms hold);
  B is free. Launcher feature help updated.
- Elixirs: wheel_zone.clicks was 0 for the whole session. Journal wheel_press (stage point, zone, plane) on every trigger press in the
  wheel, for the next headset session; no blind tuning.
- Heart toss (helper RE on the v778 journal): drop -> catch flight is the same as a mine's (median 399 vs 344 ms); after the catch the Heart
  was requested only 250 ms later (gadget_restore_at, the grenades' delay) in one try gated on slot 1 being empty at that tick, and a catch
  within a frame of the unequip once lost it. Now a kept item (the Heart) is equipped again by name (Op::test_equip "heart", code 0x30: no
  draw scene) as soon as slot 1 reads empty, polling 0.6 s; take() no longer syncs ammo on a kept item. Journal heart_on (catch -> back).
- Not checked: sim and headset (v779 is lab-only). dxgi_probe.cpp and native_weapon_hands.hpp are LF in the working copy (core.autocrlf
  normalizes; Python cannot open dxgi_probe.cpp for writing, Errno 22): patches for those two files need LF context.

## v780 (2026-11-01, after the v779 headset test)

User: everything works as planned. Left: the elixirs are still hard to reach (put the health elixir on B); a sword held in one hand folds
away when a weapon is chosen for the OTHER hand (either side); a weapon taken into the hand that shows a power should send the power to the
other hand (any weapon, the sword too), as before.

- Sword: the session's status had 65 vetoed game sword draws against 1 sheathe of the mod's own. A left draw (the wheel's new slot-1 item)
  asks the right hand to draw too (code 8); v694's veto emptied that request, and the emptied EquipChange put away the sword the hand was
  holding. A sword its hand holds (SwordGrip held, the sword drawn: NativeManualEquipRuntime::sword_kept) now keeps the game's request;
  the veto still stops a sword drawn into an empty hand. Status game_sword_draws.kept.
- Power hand-over: a power is no thing in the hand, so the second weapon in the other hand is a copy; tossed to the power's hand it was
  caught as that hand's copy and the power stayed there, hidden (v773). HandLoadout::power_hand_over: the slot-1 hand taking a weapon (a
  copy, a lying native gun then taken as a copy, a guard's weapon) while slot 1 holds a power gives slot 1 to the other hand when that one is
  free (no weapon of its own, no sword drawn or held). Journal slot1_handover. The sword take already does this (HandLoadout::update).
- B = health elixir (dual wield; helper RE, checked in the disassembly): the keyboard R (action 35: 0x4de150) and the wheel's B
  ("ConsumeHealthPotion" callback 0x4f5fb0) both end in arkCpntHealthEvolved::ConsumeHealthPotion 0x7194a0 (it re-checks, removes one,
  heals, posts ARK_MSG_PLAYER_CONSUME_HEALTH_POTION 0x15c for the HUD and plays the sound). NativeQuickHealth: GetComponent 0x877720(player,
  base+0x39ae8e0, 2), count 0x719890 > 0, CanConsume 0x718ce0, then the call; all four prologues pinned. Requested on a B press in logic 2
  (not in the wheel, where B stays the wheel's own), run at Stage0; pulse strong = drank, weak = refused. Status dual_wield.quick_health
  [drank, cannot, empty, no_component, code_differs]; journal quick_health. Mana (action 36) is 0x745b20, not bound.
- v780 not checked in the sim or the headset.

## v781-v782 (2026-11-01, after the v780 headset session 12:20-12:45)

User: v780 works (status: 53 held-sword redraws passed, 19 elixirs on B). Left: a gun dropped after a sword/gun swap vanishes when picked up;
items take a moment to appear in the hand (wheel, pickups, a caught mine); performance drops; Emily's hit reaction takes the hands; a bottle
taken with one hand puts the sword away; a pistol next to a held bottle sits muzzle up.

- Instant items: the left EquipChange (64) gets code bit 0x10 on every draw (no equip midnight: 836120 picks no scene, as the Heart's 0x30);
  a caught gadget is equipped again by its own name (gadget_drop::equip_key) as soon as slot 1 is empty. Knob build/equip-midnight.
- Hit reactions: native_ownership("Stumble"/"Stun") keeps both hands and the torso tracked (legs native).
- Bottle: HoldObject entry's 0x21 on 53 (requester 49) becomes lock + draw-remembered when the sword is held (sword_kept): locked, nothing
  drawn, the sword not emptied. Knob build/hold-sheathes-sword. Status game_sword_draws.hold_kept. Not run in the game.
- Pickup after a swap: 15 of the v780 session's lying guns went back to the hip unpicked; every failure was a right-hand drop with melee 0.
  Journal pickup_blocked (squeeze near a lying gun: distance and the gate that refused) and guns.last_return (height over the player: fallen
  through the floor?). A likely contributor: the swap's wheel equip still running its midnight when the gun was dropped (now gone, 0x10).
- Performance: GPU/runtime-bound (submit 52 ms in the worst frames; FSR4 Quality at render fraction 1 vs 0.77 in v779): the performance-mode
  session owns it.
- Not done: the pistol muzzle up next to a held bottle (needs the sim: the arm clip during HoldObject).
