# Small Objects, Palm Aim and Bonelab-Style Fingers

Date: 2026-10-25. Source: the user's headset play of v609 (commit `c668be9`, branch `feat/bonelab-feel`), a brainstorm the
same night, and three read-only code recons. This covers the "small-object pickup, floor reach and palm-facing scoring"
design that the v608 spec deferred, plus finger collision and grab reliability. Bonelab references are to
`~/.local/share/d2vr-re/bonelab/bonelab-research-2026-10-18.md` ("study") and to Parts 3–4 of
`docs/superpowers/specs/2026-10-19-bonelab-feel-design.md` ("bonelab-feel spec").

## Intent and why

The user wants a polished mod with "Bonelab / Half-Life: Alyx style" reactivity: the hand you see is the hand that
touches, chooses and holds things. In v609, three things break that.

1. **Coins and loot are hard to pick up.** These are items the game takes straight into the inventory. Bottles and other
   physical props are fine. The user's words: pickup "works only after in game pickup prompt shows up, but it [is] hard to
   aim to make this [prompt] appear".
2. **Fingers go through the world.** The user named paper stacks and other props, gliding the hands over a table, and
   reaching onto a shelf or a table.
3. **Grabs are unreliable.** Physical props like a stack of paper, and corpses, "quite often miss or fail to handle
   quickly, so it [is] not very stable". When a grab does catch (bottles), "some fast grip change" happens: the hand and
   fingers snap into the grip instead of easing onto it.

## Goals

- **G1 Palm picks loot.** Reach toward a coin, a food item, a potion, ammo, a note, a rune or a bonecharm. The item in reach
  that the palm faces and is closest to is chosen for that hand. The game's normal pickup prompt appears on it. Squeezing
  grip takes it straight into the inventory, as native Use does today. This works with no aiming ritual on a table, on a
  shelf, on the floor and in an open drawer.
- **G2 The choice is steady.** With the hand held still, a trembling hand (±1 cm, a few degrees) never flips the choice
  between two nearby items. A clear move toward another item does flip it. This holds for loot (G1) and for physical props
  and corpse bones (G3).
- **G3 Physical grabs catch first time.** With the hand on a paper stack, a crate, a bottle or a corpse, the grab catches
  on the first squeeze and without a noticeable wait. Squeezing a split second before the hand arrives still catches it.
  In clutter, the thing the palm faces wins, not whichever item the engine listed first.
- **G4 The hand eases into the grip.** At grab start the fingers blend into their wrap over about a quarter of a second
  (Bonelab `SetHandPose` 0.25 s, study lines 75/135/351). No single-frame jump in curl. No second jump when the
  contact shape arrives. When something is chosen but not yet grabbed, the fingers pre-curl toward it a little.
- **G5 Fingers stop on the world.** With an empty hand, a fingertip that meets a table, a shelf, a wall, a door or a
  prop stops on it and the finger bends instead of passing through. A hand pressed flat on a table lies flat on it.
  Sliding the hand over a tabletop keeps the fingertips on the surface. Thin props (paper stacks, books, plates) stop the
  fingers too. This is visual only.

## Non-goals

- **Holding loot in the fingers.** No coin held between the fingers and no putting loot away by hand. The user's words:
  "just grab and then straight to inventory. Let's go the simple way first, and once the mod is stable we can add more
  interaction".
- Far or force grab (Bonelab's far hover).
- Changes to bottles and other props that already grab well, beyond G2–G4 applying to them.
- Finger physics that pushes props. Fingers stop on things. Only the hand moves or knocks things, as it does now.
- New buttons. Grip alone now also grabs (see Decisions). Nothing else about the controls changes.
- Changes to the throw, the sword, guns, powers, doors or the corpse weight from v609.

## Anti-goals (failures even if it technically works)

- **Loot taken through a wall, a closed drawer or a window.** The game's own line-of-sight check stays in force.
- **Loot taken that you didn't reach for.** For example, a coin behind the knuckles while the palm faces a bottle, or a
  pickup from the other hand's item.
- **A prompt that lies.** The prompt shows one item and the squeeze takes another, or the prompt shows nothing and the
  squeeze takes something anyway.
- **Selection that chatters.** The prompt jumping between two coins while the hand is held still.
- **Fingers that jitter or vibrate on surfaces.** A finger resting on a table must sit still, not flicker between
  touching and not touching. The probe cadence must not show up as finger pops.
- **Fingers that feel stuck.** Bending fingers back so far they look broken, or a hand that stays open after you lift it
  off the table.
- **A grab that catches the wrong thing because of the new buffer.** The late-squeeze buffer only catches what the hand
  actually reaches within the window. It never reaches out to something farther away.
- **Frame-rate loss.** The user's hard requirement is ≥72 fps stable on 8 GB-class GPUs. The game is CPU and engine bound
  on the laptop, so every new cast is counted against the existing budget.
- **Mushy hands.** The ease-in is for the grip pose only. The hand position stays on the controller with no added lag.

## Constraints

- Worktree `~/github/dishonored2-vr-bonelab`, branch `feat/bonelab-feel`. Never `git stash`.
- Pinned exe `Dishonored2.exe` SHA-256 `b671f8249422862bf557d00aab2f4bb671ed4515cc10ad8a8a9decad84516180`. All RVAs are
  fixed to it.
- Engine write safety and C9 routing as in the bonelab-feel spec (Constraints): the raw controller pose drives intent and
  selection, and the drawn pose drives rendering. Selection geometry reads the raw pose plus the phase-1 palm fit (C10).
- No swapchain, layer or render-path changes.
- Harness gate: no new failures against the v609 baseline (laptop harness 632 passed / 1 failed `fsr_taa_gpu_check
  --fsr4`, environment-only; launcher 127 passed; `linux/tests` 230 passed).
- Sim safety as in the finger-curl plan: `PAUSE-SIM` absent; no `Dishonored2.exe` or `vrserver` running before a run;
  never kill by pattern; the installed DLL hash is restored after each run; the GNOME screen stays unlocked.
- Testing honesty: the sim and offline checks prove logic and measurable numbers. Only the headset proves feel and
  visuals. Every goal ends with a Tier C checklist line. The user wants to be told when a headset session is needed.
- The release follows the usual process (memory `d2vr-release-process`). The result ships as v610.

## Findings from recon (static reading of `c668be9`; unverified until measured)

### R1 Loot pickup (G1)

- The mod does not choose the native Use target. The engine's stage-0 sensor job does. The mod re-aims its inputs per
  hand:
  - the direct ray starts at the hand `volume_centre` and runs along `hand.basis[0]` (`native_interaction_aim.hpp`
    ~950–957);
  - a 0.45 m native sphere overlap is queued (`native_interaction_aim.hpp` ~1350–1370) and its hits are sorted by
    `3a37f0` (~1093–1125);
  - the mod then re-sorts those hits in `order_native_use_hits` (`selection_policy.hpp` ~59–84).
- That re-sort is a **forward-axis beam**: `across <= 0.06 m` (`grab_beam_radius`), cost `across + 0.25·along`,
  otherwise cost `distance + 1`, a 2 cm bonus for the previous owner, and a 6 cm loot bonus (`use_loot_bonus`). It is not
  palm-facing. A ~5 cm coin is easy to miss with a 6 cm beam if the hand is off by a few degrees at 30 cm. It then drops
  below the table or floor shell.
- Native eligibility is untouched: the `3b80a0` facing, height and range checks, and the `3b7b90` LOS. The range-origin
  hook swaps the eye for the hand `volume_centre` only when the hand is within 1.5 m of the eye. **Floor and drawer coins
  most likely fail facing/height** while the hand is low or beside them (inferred).
- It is **unproven whether a coin is always in the overlap result set at all**. `docs/native-use-ranking-20261010.md`:
  "if loot never enters that result set, changing rank cannot fix it".
- Loot is identified already as `takeable()`: arkPickup RTTI `0x1c5e258` or an `arkCpntPickable_Simple` component
  `0x1c5a3b8` (~1022–1030). Model AABBs are readable (`*(model+0x1840)`), and `native_use_volume_focus`
  (`selection_policy.hpp` ~28–47) is an unused bounds-based policy written for this.
- No hook sets the focus entity directly. The levers are the hit ordering and the hand origin, ray and range-origin
  hooks. The prompt follows the native selected interactable (`interaction+0x318`), so it should follow a re-ordered
  pick (inferred, needs a sim check).
- Grip routing (`dxgi_probe.cpp` ~8225–8290, `vr_controls.hpp` `GripIntent`): grip alone commits to Use after 40–100 ms
  and drives native Use when the hand has a native target. A native target beats the physical grab.

### R2 Physical grab reliability (G2, G3)

Ranked suspects:

1. **The grab is decided on one frame and never retried** (verified). `filter_native_grip` (`native_grab.hpp`
   ~1010–1090) picks a candidate at the single frame the grip crosses 0.35 with the fist formed. If nothing is within
   0.45 m at that frame, the role latches native for the whole squeeze, and the press goes to the game's carry. There is
   no buffer. Squeezing while still arriving (typical for corpses and papers) loses the grab.
2. **The fist window** (verified). A physical grab is grip + trigger within 100 ms (`GripIntent`, `vr_controls.hpp`
   ~301–324), with a late fist up to 400 ms only when the hand has no native target. Grip first with the trigger under
   0.05 commits to Use after 40 ms. A grip that lands a little before the trigger, next to something the game considers
   usable, becomes a Use or a native carry instead of a grab.
3. **Nearest-AABB pick with a list-order tie-break** (verified). `grab_focus_candidate` (`native_grab.hpp` ~89–102)
   takes the nearest `contact_distance` within 0.45 m. A hand overlapping several boxes has a gap of 0 to each, and the
   first one in registry order wins. Rotated props have inflated world AABBs. This is the likely cause of "wrong object"
   in clutter such as paper on a desk.
4. **Thin-prop bounds** (partly verified). Bounds are accepted only within size and centre limits
   (`native_rigid_body.hpp` ~72–82). Otherwise the body falls back to its origin point, which gives the wrong distance for
   off-centre papers. It is not verified that a settled paper stack passes `(flags&7)==2`. Pickables
   (`arkCpntPickable_Simple`) are not physical-grab bodies at all and go through native Use. Which path a paper stack is
   on is not verified.
5. **Corpse candidates are only 8 limb bones** (verified): forearms, hands, calves and feet, plus chest and neck when
   collar grab is on and squeeze ≥ 0.35. Each is a ±5–8 cm cube. The torso, pelvis and thighs are never candidates, so
   reaching for the body's middle misses.
6. **Discovery lag** (verified). The registry is scanned 1024 slots per tick, about 0.25 s for a full pass, so an object
   just reached may not be cached yet.
7. **The obstruction defer** (`obstruction_required_`) waits about 250 ms for a native LOS query when it is on. It is not
   verified whether it is on in the shipped config.
8. The 0.3 s regrab cooldown and the 1 s runaway lockout, after a forced release only.

### R3 Grab-start snap (G4)

- `FingerTouchCurl` applies the held floor of 0.6 **after** its 20/s smoothing (`finger_touch_curl.hpp` ~75), so an open
  hand jumps to ≥ 0.6 in one frame. `dxgi_probe.cpp` (~5452) then clamps the fingers to `held_curl` 0.8.
- The contact shape (`seat_held` cylinder, 9-frame `GripFitter`) arrives one or more frames later. Until then the fingers
  go to the held curl unwrapped. Then the wrap switches on, which is a possible second jump. The `reach` argument also
  flips from 1 to 0.8 at the same moment, which invalidates the solver memo.
- Only the hand-to-object seat is eased (0.12 s). The finger pose closes with tau 0.06 s, short against Bonelab's 0.25 s
  grab blend.
- There is no hover pre-curl (bonelab-feel spec Part 3, second half, deferred to Part 4).

### R4 Free-finger world contact (G5)

- A free-finger solver already exists (`finger_contact`, `finger_ik.hpp`). It gets up to 8 shapes per hand: the held
  object, an empty-fist palm slab, and world hits from fingertip sphere casts (`dxgi_probe.cpp` ~5385–5500).
- It fails on tables, shelves and thin props because:
  - **it can only close fingers** toward their wants. A flat hand on a table starts with its tips inside the surface, and
    `march` treats "already inside" as unblocked (`finger_ik.hpp` ~445–470). There is no "blocked, so bend or lift"
    logic;
  - **the probe is a knuckle-to-tip segment at the curl-0.5 pose** (`finger_ik.hpp` ~1086–1093, radius 0.012). For a flat
    hand it doesn't point at the table;
  - **the probes are sparse**: one finger per frame (`probe_frame%5`), so each finger is cast about every 5 frames, and a
    hit lives 100 ms. The solve budget is 1 finger per hand per frame;
  - **world hits become a 12 × 12 cm, 2 cm-thick slab** at the hit point and normal (`finger_collider`, ~3158–3180), and
    props under 1 m go through `classify_held`, which may fit a flat sheet as a cylinder. There is no thin path for
    probe hits;
  - weapon and native hands skip the touch curl entirely (~5448).
- The query budget: ≤ 2 sphere casts per hand per frame (one palm sweep on alternate frames and one fingertip), on weapon
  slots 3–8 of 16, game thread, nearest hit only. `FullHandQueryTransaction` (24 proxies) exists, but it is opt-in
  (`build/full-hand-query`), so the default needs its own casts.
- Bonelab itself has no per-finger collision (study 353–355). Its wrap comes from authored poses. The mod's contact
  solver already goes further. G5 follows the Alyx look (fingers rest on surfaces), and Bonelab numbers are used where
  they exist: 50 ms finger smoothing (study 323), the 0.6–1.0 held clamp (study 327) and the 0.25 s grab blend.

## Decisions (the user's)

- Loot goes straight to the inventory on squeeze. Nothing is held in the fingers in this phase.
- The game's pickup prompt still shows, on the item the palm has chosen.
- Selection is by the palm ("aim with palm"): the item the palm faces and is closest to.
- Bottles and props that already grab well stay as they are, apart from the shared improvements.
- Fingers stop on the world in Bonelab / Alyx style: tables, shelves, thin props, gliding over surfaces.
- A grab should catch first time and quickly, and ease into the grip instead of snapping.
- **Grip alone grabs physical props and corpses** (user, 2026-10-25, the Bonelab / Alyx way). Grip on loot or a usable
  thing (a door, a lever) still does the game's Use. The palm decides which one you meant. The fist (grip + trigger) still
  grabs too.

## Behaviour decisions (builder's calls, made here)

### G1 / G2: one palm cost for loot, props and corpse bones

- **One scoring function** in `selection_policy.hpp`, used by both `order_native_use_hits` (loot and other native Use
  targets) and `grab_focus_candidate` (physical props and corpse bones). It is the bonelab-feel spec Part 4 formula:
  - the hover centre is in front of the palm: 0.015 m along the palm axis plus 0.8 × hover radius along the palm
    direction;
  - cost = `((0.6 − dot(palmDir, toTarget))² + 1) · (d_n² + 1) · approach · priority`, with `d_n` = distance ÷ (arm
    length ÷ 4); the arm length comes from the calibration, 0.65 m if uncalibrated;
  - the approach multiplier is bounded to 0.8–1.2 from the closing speed along hand-to-target;
  - the current pick's cost × 0.86 (hysteresis);
  - the palm direction is the calibrated palm axis from the raw controller pose plus the palm fit (C10). If it is
    non-finite, the old pick is used and `scoring_fallback` is counted.
- The **distance for loot is to the item's bounds**, not its origin, read with the existing model AABB reader. A coin's
  bounds are padded to at least 3 cm radius for scoring only, so a 5 cm coin is as easy to pick as a fist-sized apple.
- **The forward-axis beam is replaced** for hand mode. Gaze or `reach` mode (the game's own Use distance) is unchanged.
- **Loot priority**: loot multiplies its cost by 0.8 against non-loot native targets in the same hit list, replacing the
  6 cm distance bonus. A per-kind priority knob is in `build/grab-tuning.cfg`.
- **Both hands**: an item chosen by one hand is not offered to the other while chosen (existing two-hand exclusion,
  extended to the new scoring).
- **The 0.45 m acquire and 0.55 m retain gates stay.**

### G1: getting floor and drawer loot into the game's hands

The order is: measure, then fix the cause the numbers show.

1. **Measure first.** Add a sim scenario `loot_reach` that teleports beside a coin on a table, on the floor and (if a save
   provides it) in an open drawer. It sweeps the simulated palm over each. Per frame it records:
   - whether the coin is in the overlap result set;
   - its rank after `order_native_use_hits`;
   - whether `3b80a0` or `3b7b90` rejected it, and which check;
   - whether the prompt showed and on which entity.
2. If the coin is **in the set but rejected by `3b80a0` facing/height**, the fix is to feed the native check the hand's
   viewpoint in a way that passes for items within hand reach. That means extending the range-origin swap to the facing
   and height inputs, within the 0.45 m gate, keeping `3b7b90` LOS. This needs a small RE pass on `3b80a0` with Ghidra
   (project `~/.local/share/d2vr-re/snapshots/dishonored2-ghidra-2026-10-19`). It is documented in `docs/` as usual.
3. If the coin is **not in the set at all**, the fallback is the mod-side bounds policy (`native_use_volume_focus`). The
   palm-chosen loot is aimed at by pointing the stage-0 direct ray at the chosen item's bounds centre from the hand. The
   game then selects it through its own direct-ray path, with its own LOS and prompt. Forging hit records is **not**
   allowed (unknown fields at +0x48..0x6f).
4. If neither works for a case (for example drawers), report it with the trace before going further.

- The **prompt must match the take.** The press consumes the target the prompt showed at that moment. The existing lock
  (`native_interaction_aim.lock(side)`) is reused. A sim check asserts that the prompt entity equals the taken entity.
- **Occupied hand**: a hand holding a prop, a body, a weapon or the sword never offers loot (existing occupancy gate,
  asserted by a check).

### G3: grab reliability

- **Grab-intent buffer.** A fist squeezed with nothing in reach stays a *pending physical grab* for **150 ms** instead of
  latching native on that frame. If, inside the window, a candidate enters the 0.45 m gate, it is grabbed then. If not,
  the squeeze goes to the native role as today. Bonelab gives no number for this. 150 ms is about two reach-arrival frames
  at 72 Hz with margin, and short enough that a block or choke on the right fist still feels immediate (the existing RB
  feed for a fist only applies when something is held, from v609 G3). It is a knob in `build/grab-tuning.cfg`, 0–300 ms.
- **Grip alone grabs** (user's decision). `GripIntent` gets a third outcome for a grip without the trigger. At the grip
  edge, the G2 palm cost ranks the hand's native Use target and its physical candidates **in one list**, and the winner
  decides:
  - loot or a usable wins → native Use, as today;
  - a prop or a corpse bone wins → a physical grab, with the 150 ms buffer;
  - nothing in reach → the native role, as today (the game's carry; the right hand's RB block and choke).
- **Block stays usable** (my call). A grip-alone physical grab needs the candidate within **0.12 m of the palm** (the hand
  is on it). The fist keeps the 0.45 m gate. So a right-hand block next to a table still blocks unless the palm is
  actually on a prop. The 0.12 m is a knob in `build/grab-tuning.cfg`. The Tier C list has a block-next-to-furniture
  line.
- **Native carry** (the game's own shoulder carry of pickables) stays reachable. The Use path still runs it when a
  pickable wins the palm cost.
- The 40 ms "quick Use" commit is replaced by the one-list decision at the grip edge. The decision may wait up to the
  existing 100 ms window for the native target to be fresh (`has_target`), never longer.
- The fist window stays 100 ms, and the late fist stays as it is.
- **Selection** uses the G2 palm cost, so clutter ties go to what the palm faces.
- **Corpse candidates** become every ragdoll bone with a known radius: the torso, pelvis and thighs as well as the limbs.
  Each bone has a per-bone priority (limbs 1.0, torso and pelvis 1.1, so a palm on a forearm still prefers the forearm).
  Collar bones keep their squeeze rule. The hold on a torso or pelvis bone uses the same physical hold as the collar
  (user's 2026-10-20 decision: collar = physical hold).
- **Thin props**:
  - log a paper stack's path (pickable vs rigid) and bounds acceptance in the new `grab_focus` status block;
  - when bounds are rejected only by the centre-offset limit, use the AABB anyway if the pose lies inside it, instead of
    falling back to the origin point;
  - a settled dynamic body that fails `(flags&7)==2` is reported, not forced.
- **Discovery near the hands first**: on each tick, re-validate the cached bodies and scan the registry slots nearest
  to the last hits first. This is enough if the sim shows the 0.25 s full pass is not the cause. Otherwise, a second pass
  every tick over cached bodies within 1 m of either hand.
- **Obstruction defer**: report whether it is on in the shipped config. If it is on, start the LOS query at hover (when a
  candidate is chosen) instead of at press, so the press-to-held wait is about one physics tick.

### G4: grab ease-in and hover pre-curl

- The held floor of 0.6 is applied **before** the 20/s smoothing, not after, so it rises through the 50 ms filter.
- On a grab, the finger pose blends from the current pose to the wrapped grip pose over **0.25 s** (Bonelab
  `SetHandPose`). This is a pose blend, not a slower solver: the solver target is the full wrap from frame one, and the
  output lerps from the start pose. The hand-to-object seat ease stays 0.12 s.
- **No unwrapped interval**: at hover, the candidate's contact shape is pre-computed from its bounds (cylinder, slab or
  box via `classify_held`). On the grab frame that shape is the solver's first shape. The fitter refines it later and
  the change is blended (tau 60 ms), never stepped.
- The `reach` argument does not flip at grab start. It moves with the same 0.25 s blend, and the solver memo keys on the
  blended value at a coarse step so it does not churn.
- **Hover pre-curl** (bonelab-feel spec Part 3, second half): while a candidate is chosen, the fingers pre-curl toward
  `0.5 × squeeze`, scaled by closeness (0 at 0.45 m, 1 at contact). This applies to loot as well. A small blend toward a
  pinch is used for items with a bounds radius under 3 cm. Visual only.
- **Loot take**: on the squeeze that takes loot, the fingers close to the held floor over 0.1 s (Bonelab `LerpPose`) and
  reopen over 0.25 s after the item disappears, so the take reads as a grab.

### G5: free fingers stop on the world

- **The solver can stop an extended finger.** When a fingertip or a finger segment is already inside or on a shape at the
  wanted curl, the solver searches for the nearest non-penetrating pose:
  - first more closed (the finger bends on the surface);
  - then slightly more open, limited to **15° of hyperextension at the middle and tip joints** (a hand pressed flat
    stays flat).
  - A finger that would need more than that stops at the limit. The hand body already stops the palm.
- **Probes that cover the fingers.** Each frame, per free hand:
  - one sphere cast along the palm normal from the palm, about 8 cm, which finds the table or shelf the hand is over;
  - one fingertip cast, round-robin across the five fingers, but along each finger's **current** pose and reaching 2 cm
    past the tip, instead of the fixed curl-0.5 pose.
  - The total stays within the current ≤ 2 casts per hand per frame. The alternate-frame palm sweep is replaced, not
    added to, when the hand is free. Probe hits live 150 ms (up from 100) and are re-validated by the next probe of that
    finger.
- **Surface shapes**: a world probe hit becomes a plane slab large enough for the hand (20 × 20 cm, from 12 × 12) at the
  hit point and normal. A prop hit under 1 m whose smallest axis is under 2 cm (paper, books, plates) becomes a thin slab
  from its oriented bounds, not a `classify_held` cylinder.
- **Solve budget**: free-hand solving goes from 1 finger per hand per frame to all five, with the solver memo keeping
  untouched fingers cheap. If the measured `finger_ik` `avg_us` rises by more than 50 µs per hand on the laptop sim, fall
  back to 2 fingers per frame plus all fingers that have a shape in reach.
- **No jitter**: a contact keeps the finger on the surface for at least 2 probe cycles after the last hit (stickiness),
  and opening is eased (tau 60 ms) when free-hand contact is involved. Today opening is immediate.
- **Weapon and native hands** stay as they are (the gun and the sword own their poses).

### Settled after the builder check (builder's calls)

- **G5 covers static world geometry** (walls, door leaves, table tops, shelves), not only props, through the same world
  probe hits.
- **Cast budget against surface tolerance.** The frame rate wins. The free hand may use up to **3 casts per frame** (the
  palm-normal cast plus two fingertips) when the laptop sim shows no frame-time loss against v609. Otherwise it uses 2.
  The surface tolerance is **5 mm on flat static surfaces when the hand is still or slow (under 0.3 m/s)**, and **15 mm
  while sliding fast**. If the measured cost allows it, `FullHandQueryTransaction` may become the free-finger source by
  default. That is a plan-time measurement, not a requirement.
- **Finger priority** (highest first):
  1. surface contact (G5, final clamp, as in bonelab-feel spec Part 3: the contact wrap is always the last stage);
  2. a held-object wrap;
  3. the take close (loot);
  4. the hover pre-curl and pinch;
  5. the touch curl.

  A hand reaching a coin on a table keeps its fingers on the table and pre-curls only as far as the table allows.
- **Floor and drawer loot in v610.** Plan the fix branches of G1 in this release. If a case is still unsolved after the
  measured fix (for example drawers), v610 ships with it listed as a known gap in the release notes and the checklist. It
  is not hidden, and it is not a blocker for the rest.
- **Existing checks.** Updating checks that encode the old behaviour (the forward beam order, the held-floor ordering,
  immediate finger opening, the nearest-AABB pick) is expected. Each update names the goal it serves in the commit. A
  changed check that is not tied to a goal here is a red flag and is reverted.
- **`build/grab-tuning.cfg`** exists in the mod home (the loader is in `grab_force_cap.hpp`, ≤ 4096 bytes). New keys go
  into that loader with bounds, and the status block reports `tuning: file` as it does now.

### Diagnostics

- New status block `grab_focus`, per side:
  - the chosen id and kind (loot, prop, bone);
  - its cost and the runner-up's;
  - `switches` per 10 s;
  - `hysteresis_held`;
  - `scoring_fallback`;
  - `buffered_grabs` (caught inside the 150 ms window) and `buffer_expired`;
  - the press-to-held time in ms;
  - the paper path and bounds result for the last thin prop.
- `finger_ik` block additions:
  - per-finger `blocked`, `hyperextended` and `stuck_frames`;
  - `probe_hits_world`, `probe_hits_thin`;
  - `ease_active`.
- A loot trace (`native-loot-reach.csv`, bounded rows) with the R1 per-frame fields, written only when the scenario or a
  knob asks for it.

## Verification

- **Tier A (offline checks, `tests/`):**
  - a scoring lab: fixed clutter scenes with expected winners (the palm toward a farther book beats a nearer object
    behind the knuckles; the back of the hand loses; an approaching object wins a tie; a coin beats the table shell);
  - hysteresis: ±1 cm and ±3° jitter never flips; a clear move does;
  - cost is monotonic in distance;
  - mutation checks on 0.6, 0.86, the gates and the loot priority;
  - the grab buffer: catches inside 150 ms, expires after it, never catches beyond the gate;
  - the ease-in: no frame-to-frame curl step above the 0.25 s blend rate at grab start, including when the shape arrives
    late;
  - the finger solver: a flat hand on a plane stays flat, within the hyperextension limit; a finger sliding over an
    edge stays outside the surface; a thin slab stops the fingers; stickiness prevents flicker on a probe cadence of 5;
  - the existing `native_use_order_check`, finger, grab and corpse checks stay green or are updated with a stated reason.
- **Tier B (sim):**
  - `loot_reach` (table, floor, drawer if available): the coin is picked, the prompt entity equals the taken entity, and
    there are no takes through LOS blockers;
  - `paper_grab`: a paper stack on a desk among props; press before contact (buffer), press on contact; first-try
    catch, no wrong pick;
  - `corpse_middle_grab`: reach to the torso; the grab catches;
  - `hand_on_table`: a flat hand pressed on and slid over a table; no fingertip below the surface by more than 5 mm in the
    drawn pose, no `stuck_frames`, `finger_ik` cost within budget;
  - the existing corpse, `finger_touch`, `run_hands` and `sword_regrab` scenarios stay green;
  - a frame-time comparison against v609 in the same scenario.
  - Saves near a coin, a paper stack and a table are needed. Use `tools/entities.py` and `vr_selftest.py --near item` to
    teleport. If no usable save exists, ask the user for one.
- **Tier C (headset checklist `docs/VR-v610-TEST-CHECKLIST.md`):**
  - pick up coins on a table, on the floor and in a drawer with each hand, with no aiming; the prompt is on the item you
    reach for;
  - hold the hand still between two coins: the prompt doesn't flicker;
  - grab a paper stack, a bottle and a corpse (limb and middle) ten times each: count the misses; squeeze just before
    arriving;
  - grab with grip alone and with a fist; block with the right grip next to a table and a crate (it must block, not grab);
    open a door and pull a lever with grip alone (Use still wins);
  - watch a bottle grab: the fingers ease in, no snap;
  - press a flat hand on a table, slide it, reach onto a shelf, touch a paper stack: the fingers stay on the surfaces,
    with no jitter;
  - the fps overlay in a busy room is the same as v609.

## Left to the builder

- The exact sim timelines, saves and thresholds beyond the acceptance targets above.
- The hover sphere radius (Bonelab's is in the study; scale it to the mod's 0.45 m gate).
- The RE depth on `3b80a0`, as long as the result is documented and pinned to the exe hash.
- How the solver searches for the non-penetrating pose (bisection or the existing `march`), within the cost budget.
- The order of tasks, except that G1 measurement and the scoring function come first, because G2–G4 depend on them.
