# Arm and weapon collisions, body holsters, putting gadgets and the Heart back (design, 2026-11-26)

Branch `feat/doto-vr` (on top of df81353, v1483 / v145785). Both games: everything lives in the shared `src/` and reaches Death of the
Outsider through `tools/doto/port.py`. Research behind this design (same folder as this spec's research):
`docs/research/2026-11-26-holsters-collisions/` — the reference mod's notes (kept outside the repository) (the MIT-licensed reference VR mod,
its public repository, read as data, nothing copied) and `spacewarp-improvements.md`.

## 1. Intent

The user (2026-11-26): "make arms collisions and weapon holsters similar to the reference VR mod (and mines and grenades and heart can be
put to the inventory back again) ... add guns and swords collisions as well, so it is physically correct". Bar: good, bug free, VR native,
smooth, physically correct; proven in simulated sessions on the Linux lab (no headset available to the builder); helpers Sonnet 5.5 and
Haiku 5.5. The user opted out of design questions ("Proceed"): every choice below marked **(call)** is the builder's, stated so the user
can correct it.

## 2. What exists today (found 2026-11-26, file:line in the research notes)

- **Hands**: the drawn hand stops at world surfaces (`hand_clamp.hpp`: palm, leading fingertip, wall-tip and exclusion sphere sweeps,
  half-space planes held 0.1 s, budget `CastBudget`), pushes loose props (`native_hand_contact.hpp`), fingers stop on surfaces. Default on.
- **Weapons**: the drawn blade / barrel segment is swept every frame from the drawn grip (`weapon_collision.hpp` v1301: planes, critically
  damped follower, |turn| <= 35 deg, |push| <= 10 cm; hits classified world / npc / dynamic / self / held; an NPC or a loose body is a
  surface for one answer). The weapon's bead body pushes props (`weapon_body.hpp`). Shots and the crosshair leave from the drawn barrel and
  are clipped at the clamp's planes (`hand_clamp_feed.hpp` I2/N5). Hand vs other hand's hand/weapon (`hand_weapon_contact.hpp`, 2 capsules a
  side). All default on, all working in DOTO (sim 20261125T213934: hand_clamp contact frames, 3363 weapon casts, clamp casts ~2 us mean).
- **Arms**: nothing. The arm IK (`body_ik.hpp` solve_arm, Boneworks pole `arm_body.hpp`) solves the elbow from the drawn wrist with no
  world check: forearms and upper arms pass through walls, door frames and table edges.
- **Holsters** (`vr_controls.hpp`): sword over either shoulder (`TempleReach` sphere r 0.16 +0.03, HEAD-relative: right .17, up -.10,
  back .10; squeeze there draws, release there sheathes, release elsewhere drops/throws); pistol/crossbow at either hip (pelvis-relative
  from the body solve, r .17; `GunGrip`: release at the hip holsters, elsewhere drops in dual wield); a visible gun copy at the hip
  (`NativeWeaponHands::hip`); journal / VR menu at the temples; health on B (dual wield).
- **Gadgets** (`throw_gesture.hpp` `GadgetGrip` dual wield, `GadgetFist` single): a grenade or mine is thrown, placed, or dropped as a world
  pickup (`native_gadget_drop.hpp`, RemoveItem + a pickup the hand can catch). **It can never be put back.**
- **The Heart** (D2 only; `HeartGrip`): letting go of it AT THE HIP puts it away (`Op::slot1_unequip`, engine instant unequip 0x20 state 64),
  elsewhere drops it. Dual wield only; the hip is not where anyone looks for the Heart.

## 3. Goals, non-goals, anti-goals

Goals
1. **Arm collisions**: forearm and upper arm never pass through world geometry while the hand is tracked; the elbow bends around what is
   there, as a real arm does (the real elbow is the one free joint between a tracked shoulder and a tracked hand).
2. **Weapons physically consistent with the arms**: a weapon never passes through its owner's other arm; the existing weapon/world
   collisions keep working with arm collisions on (regression A/B), in both games; what the world casts see of living guards is measured
   and documented (experiment E1) and fixed only if the measurement shows a gap (see 6.3).
3. **Body holsters like the reference mod's**: places on the body that follow the body (not the head), nearest place wins, guarded against
   false triggers, a short pull to commit a draw from a pouch, consistent haptic ticks.
4. **Put back**: a grenade, mine (all kinds, both games) or the Heart goes back to the inventory by letting go of it at its place on the
   body (belt pouch; the Heart's chest pocket; any holster place is forgiven), and comes out again by a squeeze-and-pull there.
5. Every new feature has a default-on launcher row and a knob to turn it off; every claim is backed by a simulated A/B run.

Non-goals (this iteration)
- A full engine physics body for the arms (Havok bodies per segment; the reference mod's PhysX hands). The query-and-solve route below gives the
  same visible result with the proven cast ABI (the hand clamp's), at ~2 us per cast.
- Visible holstered sword on the back, visible grenades on the belt (would need non-physical attached model copies; the mod's sword copy is
  a physical world entity). The hip gun copy stays as it is.
- Arms colliding with living NPC bodies or loose props (the reference mod's forearms also meet the world only); seated / left-handed layouts.
- Implementing the spacewarp recommendations (research delivered; R1/R2 are the suggested next step, see 8).

Anti-goals (failure even if it "works")
- Arm jitter: an elbow flipping between two solutions frame to frame, or a visible pop when contact starts or ends.
- The hand leaving the controller because of the arm (this iteration moves only the elbow; the hand stays where the hand clamp draws it).
- A gadget or the Heart equipped by accident while reaching for a body, a bottle or the hip gun; a throw swallowed as a "put back".
- Any regression of accepted behaviour: shoulder draw/sheathe, hip draw/holster, gadget throw/place/drop, Hook Mine mode hold, Heart hip
  put-away, chokes, takedowns, carries, door peeks.

## 4. Approaches considered

**Arms.** (a) **Cast the natural arm and swivel the elbow (chosen)**: each frame sweep the forearm and upper arm of the arm the IK would
draw (two sphere casts per arm through the hand clamp's transport), keep what they hit as half-space planes, and let the IK pick the elbow
angle about the shoulder-wrist axis nearest the natural one that clears every plane. Cheap (4 casts/frame), deterministic, testable
offline, no new engine ABI, the hand is untouched. (b) Real Havok capsule bodies on the forearms driven by velocity (the reference mod's PhysX
route): most faithful, but needs body creation, filters, step hooks and teardown for 4 more bodies; the mod's single hand body (knob
`hand-body`) already shows how costly that path is to make safe. (c) Hand-frame forearm probes added to the hand clamp's sphere set: a plane
found by the palm would wrongly constrain the forearm at table edges (half-spaces are local), and it can only move the hand, never the
elbow. (a) wins.

**Sweeping the natural arm, not the corrected one (call)**: a corrected forearm that just clears a wall stops hitting it, the plane
expires, the elbow relaxes back into the wall, hits again: a limit cycle. Sweeping the arm as the IK would draw it WITHOUT the swivel keeps
hitting for exactly as long as the user's pose would penetrate, so contact starts and ends with the pose, not with our own output
(the reference mod lesson "never collide with geometry placed by the same solve").

**Holsters.** (a) **Keep our release-to-stow rule, add body places, a gadget pouch and a Heart pocket (chosen)**: the user already plays
with "release at the place puts it away" (sword, guns, Heart); the reference mod's press-to-stow would contradict it. (b) the reference mod's full
immersive holsters (press to stow, visible copies of everything, per-weapon place picker): bigger change to accepted controls and needs
model copies we do not have. (c) Wheel-only put-back (an empty wheel sector unequips): not body-native, and the user asked for the
The reference mod feel. (a) wins; from the reference mod we take body-relative places, nearest-wins arbitration, the false-trigger guards, the pull commit
for pouches, edge-only enter ticks, and "an armed throwable cannot be put back".

## 5. Arm collision (feature A)

### 5.1 Geometry and casts (render thread posts, game thread casts, one frame late, like the hand clamp)
Per tracked arm whose hand is gameplay-driven (not natively owned, not holding a physical object or a door, no shoulder carry, no
choke/takedown/mantle/cutscene, no authored view), using LAST frame's solve (world space, published by the full-body driver):
`S` the shoulder after the clavicle shrug, `W` the solved wrist, `E0` the NATURAL elbow (the Boneworks pole with no swivel).
- forearm cast: sphere `r_f` from `W + 0.05 m toward E0` to `E0`;
- upper-arm cast: sphere `r_u` from `S + 0.04 m toward E0` to `E0`.
Radii **(call)**: `r_f` 0.045 m, `r_u` 0.055 m (Corvo's coat sleeve; the reference mod legacy capsules 0.046 / 0.07). Two new slots per side in
`NativeInteractionAim` (17 -> 19), the clamp's layers (`kWorldOccluderLayers`) and its strict request-id answers; no exclusion.

### 5.2 Planes (pure, `src/arm_collision.hpp`, `ArmCollision`)
A hit makes or refreshes a plane (contact point P, normal n toward free space, the radius of the cast that found it). Accepted only when it
is the world or a static/keyframed entity (`BodyClass` world / entity_static; doors count), not an initial overlap, normal valid, and the
cast's free end (W for forearm planes, S for upper-arm planes) lies on the free side by at least half the radius (else it is an edge the
half-space cannot describe: counted as `edge_discards`, ignored). Same plane = normal within 15 deg and offset within 3 cm: refreshed. Up to
4 planes a side, each expires 0.15 s after its last hit **(call: the clamp holds 0.1 s; 0.15 covers one missed answer at 45 Hz)**. Any epoch
change, teleport (> 0.35 m step), snap turn, gap > 0.25 s, load or native ownership clears the side.

### 5.3 Swivel (in the solver, every substep)
`body_ik.hpp` gets per arm the active planes (in solve space) and a swivel state. With the shoulder `S`, the IK wrist target `W'` and the
natural pole, the elbow can lie anywhere on a circle about the axis `a = unit(W' - S)` (centre C, radius rho from the two bone lengths).
`E(theta)` = the natural elbow turned by theta about `a`. The solver picks `theta*` = the smallest |theta| on a 5-degree grid (refined to
1 degree around the best) such that for every plane `dot(E(theta) - P, n) >= r + skin` (skin 5 mm), within the anatomical limits **(call)**:
|theta| <= 110 deg and the elbow never more than 0.10 m INWARD (toward the body midline) of the natural elbow (no elbow through the chest).
None feasible: the theta with the smallest worst violation (ties: smaller |theta|), and that residual is reported (`infeasible_frames`,
`residual_mm`); the hand is not moved (non-goal), so the residual stays visible in rare reach-through-an-opening poses.
Follower (smoothness): engaging (|theta*| grows, or the current theta violates a plane by > 1 cm) goes at up to 12 rad/s (never draws the
elbow inside a wall for more than a frame or two); releasing (planes gone or theta* nearer zero) is critically damped (tau 80 ms, at most
4 rad/s), so the elbow eases back as the reference mod's 60 ms release residual does. The natural pole's own continuity state is kept untouched
(the swivel is applied on top, never stored into it). Substeps advance the follower by their own dt.
Output per arm (world, for the next frame's casts and the status): S, W, E0, E (drawn), theta, residual, plane count.

### 5.4 Which arms
Full-body mode (the launcher default) only **(call)**: the arm-palette path without full body keeps today's behaviour. Both hands, empty
or armed (sword, gun, power, gadget, Heart, Voltaic forearm gun: the swivel moves only the elbow; a Voltaic gun bound to the forearm rides
the forearm the solver draws).

## 6. Weapons (feature B)

### 6.1 Self-collision with the other arm (pure, extends `hand_weapon_contact.hpp`)
Each side gets a third capsule: its forearm (last frame's drawn elbow to wrist, r 0.045). Pairs: this side's hand and weapon against the
other side's forearm (both directions). The forearm cannot be pushed (it follows its own hand), so the whole correction goes to the hand
and weapon that met it (weight 1/0), capped at 5 cm as today; no forearm-forearm pair (crossing arms stays free, as in the reference mod). Knob
`no-forearm-contact`. The existing hand/weapon pairs are unchanged.

### 6.2 Weapons with arm collisions on (regression)
No change to `weapon_collision.hpp`. Proven by A/B sims: `sword_sweep`, `sword_draw_sweep`, `holster_draw`, `afw_ab_pistol` (gun) and a
new `weapon_wall` scenario (blade and barrel pressed into the lab wall): weapon contact counters, max jump/turn and plane-change rates no
worse than v145785 / v1483.

### 6.3 Living guards (experiment E1, then decide)
Scenario `npc_contact_probe`: `d2vr guard behind 0.7`, `god`, then press the open right hand, then the drawn sword's blade, slowly into the
guard's back and hold; record the hand clamp's contact class and the blade's hit kinds (`weapon_collision.contact.hits`: world / npc /
dynamic / self / held). If the casts see living guards (kind npc): nothing to add (the blade and barrel already stop on them, the hand
clamp holds them as entity_static); document it. If they do not: write the finding into the research notes and propose (not build) the
The reference mod route (analytic capsules on the guard's bones for hands and weapons) as the next item. Rationale: building NPC capsules without
the measurement would be a hypothesis fix (user rule since v613).

## 7. Holsters and putting things back (feature C)

### 7.1 Body places (pure, `src/holster_rig.hpp`, `HolsterRig`)
Frames from the full-body solve when it is fresh (< 250 ms, as the hip is today): the pelvis (position, level left/forward) and the chest
(`center_spine03_jnt`, level left/forward); fallback: today's head-relative points. Places (metres, the hand's own side; **calls**, tunable
through a knob file `build/holster-places` with `name x y z r` lines):

| place | frame | offset (fwd, side, up) | radius (+exit) | takes | gives (draw) |
|---|---|---|---|---|---|
| shoulder L/R | chest | (-0.02, 0.17, 0.17) | 0.16 (+0.03) | sword (sheathe) | sword (draw), as today |
| hip L/R | pelvis | (0, 0.21, -0.05) | 0.17 | pistol / crossbow (holster) | the hip's gun, as today |
| belt pouch L/R (new) | pelvis | (0.14, 0.09, 0.00) | 0.13 (+0.03) | any grenade or mine | the last gadget put away |
| Heart pocket (new, D2) | chest | (0.12, 0.07 left side for both hands, -0.02) | 0.13 (+0.03) | the Heart | the Heart |

The shoulder zone is the UNION of today's head-relative sphere and the chest-relative one **(call: no accepted draw is lost; the chest one
keeps working while looking down or with the head turned)**. Nearest place wins; a hand holding item X only "sees" places that take X for a
release, plus every place for a forgiving put-away of a gadget or the Heart (any body place puts those away).

### 7.2 Put back (release)
- **Grenade / mine (both games, dual wield: the launcher default)**: the hand holding it opens inside a place, UNARMED (the trigger was not
  pressed since the grab) and the hand slower than 1.0 m/s relative to the player (a throw that starts near the belt is a throw) ->
  `Op::slot1_unequip` (the engine's own instant unequip, as the Heart's; nothing removed from the inventory). The pouch remembers the item
  (equip name) for the next draw. Outside a place: today's behaviour (drop, throw-place, throw, mode hold). An armed grenade (trigger held)
  is never put back (the reference mod's pin rule). Single wield keeps today's behaviour **(call)**: there the fist IS the game's throw button, so
  opening it is the game's own throw; vetoing that release is not worth the risk while dual wield is the default.
- **The Heart** (dual wield, as today): release in the Heart pocket OR at the hip (today's) OR any other body place -> put away. Elsewhere:
  today's drop.
- Sword and guns: unchanged (shoulder / hip), plus the chest-relative shoulder sphere.

### 7.3 Take out (squeeze and pull)
An EMPTY hand (no item, no physical hold, no grab candidate within the grip-alone palm reach, no native Use target, no wheel, not natively
owned, no power aim) squeezes inside a pouch or the Heart pocket: the draw is armed with a light tick; it commits when the hand, still
squeezing, has moved 0.07 m away from the place's centre within 1.2 s **(call: the reference mod's pull commit, shortened from 0.13-0.22 m because
nothing is sheathed)**; letting go first cancels with nothing equipped. Commit: the engine equips the remembered gadget (inventory name,
the same native equip as `gadget_restore_item`) or the Heart into slot 1, and that hand becomes the slot-1 arm (the wheel's own arm
assignment, `ArmSet::wheel_commit`), so the item lands in the pulling hand. Nothing remembered, or none left: an "empty" double tick, no
equip. Cooldown 0.6 s per hand after any draw or put-away (the reference mod).

### 7.4 Arbitration and guards (from the reference mod's list)
Suspended for a hand while: the wheel is open, a menu / note / journal is up, the arm is natively owned, it holds a body / prop / door,
a shoulder carry is on, a power aim is held, a gadget is armed, within the cooldown. A holster place nearer than a pouch wins (and the
reverse). Only one draw pending per hand. The draw never starts on a grip that was already closed when the hand entered (edge only).

### 7.5 Haptics (one vocabulary, minimum 12 ms segments)
Enter a place that has something for this hand: 0.25 x 14 ms (edge only); draw armed: 0.2 x 12 ms; draw committed: 0.55 x 25 ms; put away:
0.35 x 18 ms; empty: 2 x (0.15 x 12 ms, 40 ms apart). Existing shoulder/hip ticks keep their values.

## 8. Spacewarp (research only, delivered)

`docs/research/2026-11-26-holsters-collisions/spacewarp-improvements.md`. Ranked: R1 hand-region gating while walking (the measured weak spot:
both warps are worse than the held frame around the hands while walking; own code, ~0 ms), R2 nearest-depth dilation of mover vectors (the
FSR3 technique, MIT) plus a same-surface 2x2 splat footprint (speckles on moving sleeves, +0.1-0.2 ms), R3 pyramid inpainting for the star
pattern, R4 submitting `XR_KHR_composition_layer_depth` (headset A/B only), R5 a sharpen pass after AFW; plus the reference mod's snap-turn
cut hint. Not recommended: interpolating frame generation (FSR3 FG / DLSS-G: a frame of latency, D3D12-only SDK), optical flow as the motion
source, neural extrapolation (no public code), XR_FB_space_warp on PC (no runtime evidence), copying GPL code.

## 9. Knobs, status, tests, sims

Knobs (all with a default-on row in `tools/default_features.json`): `no-arm-collision` (feature A), `arm-collision-shadow` (computes and
reports, applies nothing: the A/B lever), `no-forearm-contact` (6.1), `no-gadget-stow` (7.2 gadgets, dual wield), `no-holster-draw` (7.3),
`legacy-shoulder-zone` (head-relative sphere only), `no-heart-pocket`; `build/holster-places` (tuning file).
Status (`xr-live-status.json`): `arm_collision` {enabled, shadow, casts, hits by class, planes, edge_discards, engaged_frames, max_swivel_deg,
infeasible_frames, max_residual_mm, release_frames, max_step_deg}, `holsters` {places_valid, enters, draws_armed, draws, draws_cancelled,
stows by kind, empty, suspended_by}, `hand_weapon_contact.forearm` {contacts, max_mm}.
Host checks (`tests/`, run by `tools/build-msvc.cmd` and the Linux harness): `arm_collision_check` (side wall, table under the elbow,
corner of two planes, infeasible residual, edge discard, release easing, no flicker under alternating hits, inward limit, degenerate reach),
`holster_rig_check` (frames, nearest wins, union shoulder, pull commit/cancel, cooldown, armed never stows, fast hand never stows,
suspensions), `hand_weapon_contact_check` additions (forearm pairs).
Sims (`tools/linux/scenarios/`, DOTO v145786+ and D2 v1484+, each A/B against v145785 / v1483 or the shadow knob): `arm_wall` (arm along
and into the lab wall, elbow-low reach), `arm_table` (reach down onto a surface), `weapon_wall`, `gadget_stow` (DOTO Hook Mine, D2 grenade:
put back at the pouch, take out, control drops/throws), `heart_pocket` (D2), `npc_contact_probe` (E1), plus the regression set
`holster_draw`, `sword_sweep`, `sword_draw_sweep`, `mine_throw_stick`, `hook_mine_mode_vr`, `power_grip_cancel`. Eye captures
(`d2vr spacewarp capture N`) for the arm-vs-wall pictures.

## 10. Risks

- The wall/table spots in the lab saves must be found first (a short exploratory sim with `d2vr trace` and setviewpos).
- Half-space planes at thin edges: the edge-discard rule may leave some forearm-through-edge frames; reported as residual, not hidden.
- The engine's instant unequip for gadgets is the Heart's proven op; for grenades and mines it is new: the sim must show the count
  unchanged and the item equippable again.
- Single wield (dual wield off) keeps today's gadget and Heart behaviour: no put-back and no pouch draw there (documented; dual wield is the
  launcher default).
- No headset: comfort and feel of the swivel, the pouch positions and the pull distance need the user's session; the numbers are knobs.

## 11. Left to the builder

Exact plane matching, grid refinement, how the driver maps planes to solve space, status field names, scenario timelines, and the tuning
file parser. Anything that changes accepted controls goes back to the user.
