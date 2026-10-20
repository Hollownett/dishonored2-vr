# Physical hands, next phase: "Bonelab feel" (design, 2026-10-19)

Builds on: `docs/superpowers/specs/2026-10-18-physical-hands-design.md` (phase 1, "hands that stop at the world"; cited as
"phase 1 Gn/Cn"), `docs/superpowers/specs/2026-10-18-physical-hands-research.md` ("research §…") and the phase-1 ledger
`.superpowers/sdd/2026-10-18-physical-hands-phase1/progress.md` ("ledger").
Engine/mod research for this phase (outside the repo):
- `~/.local/share/d2vr-re/research/corpse-grab-prep-2026-10-18.md` ("corpse prep §…")
- `~/.local/share/d2vr-re/research/physical-hands-2026-10-18.md`
- `~/.local/share/d2vr-re/bonelab/bonelab-research-2026-10-18.md` ("Bonelab §…", techniques and values only)

No Bonelab code or assets go into the mod. Every technique and number below is restated in our own words and is used only as
a feel target. Every implementation brief for this phase must cite the Bonelab technique or numbers it follows (user
requirement), and reviews check the work against them.

Marking: **(builder's call)** means the user did not decide it explicitly. The user can overrule any of these. Evidence tags
follow the research: [V] verified, [I] inferred, [U] unknown and needing a runtime check.

This document is a draft for the user's review. It is not committed.

## Where we are

- Phase 1 was approved in the headset on 2026-10-19: "arms collisions with objects work good, next phase approved".
  The reverse-grip sword is fixed. Grabbing props and physical objects "works well" (user).
- Known issues running in parallel, not part of this phase unless they block it:
  - SteamVR `xrEndFrame` rejections (kicks): fix in progress (full-extent quad layers).
  - Silent assassination unreliable: root-cause analysis in progress.
  - Blink aim to follow the hand: in progress.
  - A Linux simulated-VR test rig is planned, so offline scripted sessions can reproduce grabs and drops without the
    headset. This design builds its verification around that rig (see "Verification model").
- Known defect this phase fixes: a dropped whale-oil tank falls through the floor. The held tank is on the DEBRIS layer and
  the hand clamp ignores the held body, so the keyframed tank can be driven into the floor and is then restored to rigid
  inside it (ledger, 2026-10-19 23:10).

## Intent and why

The user wants "a great mod with full immersion similar to bonelab", with "physical hands with grabbing mechanics (with all
physical objects in game), and a full physics based doors". They want to grab both props and guards, and said that "corpses
should be movable, this is the one of the core vr mode mechanics".

Phase 1 made the hands stop at the world. This phase makes what the hands *hold* behave and feel physical. The order the
user agreed, which this design keeps:

1. Grab and drag/lift corpses (dead or unconscious guards) by limb or collar.
2. Throw history and spin.
3. Touch-sensor finger curl.
4. Palm-facing grab scoring.
5. Phase-2 Havok hand body, so held objects physically collide with the world.

The user's annoyances from phase 1 still apply: objects held by an invisible point instead of the hand, fingers that do not
react, hand/finger misalignment, and render breaks while carrying. Parts 1, 3 and 5 address the first, second and fourth
directly. The third is mostly closed by phase 1's calibration (G5), plus one routing decision in part 4.

Full physics doors are on the user's goal list but are not in this phase (see non-goals).

## Goals

G1. **A corpse can be moved by hand.** A full-fist grab on a dead or unconscious guard's limb or collar takes hold. Dragging
along the floor, lifting off the floor and putting back down work reliably, repeatably, for both hands, and without any
crash.

G2. **A body has weight.** A held body lags behind the hand according to how heavy it is, and its pull is limited by a force
cap, the way Bonelab limits the hand. A light limb follows closely. A whole body hanging from one hand is slow to lift. The
user never sees a body teleport into the hand.

G3. **The hand is on the body.** While holding a corpse the drawn hand sits at the grasped point on the limb or collar. It
is never at an invisible point away from the body, and it never shows a gap to the body.

G4. **Throws match the flick.** A release sends the held object away in the direction and at the speed of the final flick
of the wrist, with the spin the wrist put on it. Walking while throwing does not double-count the walking speed.

G5. **Fingers react to the controller.** Resting a finger on the trigger, lifting the thumb, and closing the other fingers
show on the visible hand, using the controller's touch sensors. A held object shows at least a firm closure.

G6. **The hand reaches for the right thing.** When several things are in reach, the one the palm faces and the hand is
moving toward is chosen, and the choice does not flicker.

G7. **Held objects collide with the world.** A carried object stops at walls and floors instead of passing through them.
Releasing it never leaves it inside geometry, so the dropped oil tank rests on the floor. Whether the *hand itself* becomes
a Havok body is decided by a time-boxed spike (part 5B), not assumed.

## Non-goals

- Physics doors. Still wanted by the user, still a later design. Part 5B's hand body is what they would build on.
- Awake (live) guards as a new grab target. The existing "yank a living guard" path stays as it is (kill-switch kept,
  not hardened or extended here). *(builder's call; see open question 3)*
- Two-hand merged hold of the same limb, force pull from range, and synthesized cylinder-grip alignment (Bonelab §6.1 rows
  6 and 7 of the shortlist). Two hands on *different* limbs of one body already work independently and stay that way.
  *(builder's call)*
- Recoil, stab/slash classification and rendering items from the Bonelab study. Not in this phase's order.
- Any change to locomotion, turning, aim, or things the user already accepted (props held well, reverse grip, phase-1 clamp).
- Player body reaction forces. Hands never push the player (phase 1 anti-goal stays).
- Damage from thrown bodies. A thrown corpse is inert in this phase. *(builder's call; see open question 4)*

## Anti-goals (failures even if it technically works)

- **Any crash from ragdoll writes.** The v435/v436 headset crashes listed the ragdoll event posts (`0xb9`, `0x149`, with
  400 ms re-posts) as suspects (corpse prep §3). A feature that grabs corpses and crashes the game is worse than no feature.
- **A corpse that is launched, spins wildly, stretches, or "explodes".** Pulling a body must never produce a speed spike on
  any of its 17 bodies.
- **A body that teleports to the hand** or snaps into place on grab or release.
- **Held objects that jitter** against the hand, against walls, or against the floor, in or out of contact.
- **A held object visibly held by a point away from the hand**, or a hand that shows a gap to what it holds.
- **Finger flicker.** Curl values that chatter between frames, or fingers that fight the contact wrap.
- **Grab selection that flips between two objects** when the hand is still.
- **A dropped object inside geometry.** Anything released ends up outside the world.
- **Frame time regressions.** Budget below. Measured through the status JSON.
- **Regressing accepted behaviour:** prop grabbing, weapon grips, aim and crosshair routing, the phase-1 clamp.
- **The hands moving the player** or the camera, via any new body or constraint.

## Constraints

- **Branch** *(builder's call)*: new branch `feat/bonelab-feel` from the phase-1 head (`feat/physical-hands` /
  `test/phase1-candidate` once the fixes there are ported), in the worktree `~/github/dishonored2-vr-hands`. Not committed
  by this document.
- **Pinned exe** `Dishonored2.exe` SHA-256 `b671f824…6180`. All RVAs are fixed to it. Any hook byte check stays.
- **Engine write safety.** Every engine write keeps the SEH guard, finite checks and fault counters from the 2026-10-17
  review fixes. Handle generation (`(gen+1)<<24 | index`) and world identity are re-validated on every write. Writes run only
  in the existing 0x89fb0 dispatcher callback (velocity setter 0xc85dc0). Event posts go through 0x8112c0 and are budgeted
  (see part 1). Nothing new calls 0xc90de0, 0xc8e5b0 or 0xc85860 outside the part-5B spike.
- **Frame budget** *(builder's call)*: the new work in parts 1–4 costs ≤ 0.2 ms game-thread time for both hands combined, on
  top of phase 1's ≤ 0.5 ms cast budget (C14 fallbacks still apply). Part 5A's extra casts count against the phase-1 cast
  budget. Reported as `cost_us` / `cost_max_us` in status.
- **C9 routing (phase 1) stays the law:**
  - The **raw** controller pose drives intent: grab desire and selection (including part 4 scoring), throw velocity (part 2),
    aim, attack/block and interaction rays.
  - The **drawn** pose (clamped or, in this phase, anchored to the held body) drives what is rendered: the hand, the arm IK
    target, the drawn weapon.
  - Native authored animations (takedowns, climbing) keep ownership and the clamp yields.
  - Finger curl (part 3) is visual only. It never feeds any intent consumer.
- **Platforms.** Windows MSVC (`tools\build-msvc.cmd`, `tools\build.ps1`) is authoritative. The Linux harness
  (`python3 tools/linux/run_build_cmd.py`; 483 passed / 0 failed / 41 skipped at phase-1 end) must stay green, with the
  `-Wshadow` family now an error. Every new check runs on Linux. Wine runs the hook tests since the trampoline-unwind fix.
  Nothing here may depend on a Linux-only or Windows-only API in the DLL.
- **Testing honesty (AGENTS.md).** Offline checks and simulated sessions pin logic and crash-safety. Only the headset proves
  feel, comfort and visuals.
- **Do not make known unknowns look known.** Mass (`motion+0x26`) and the time factor are still unverified ([U]). The
  status flags `mass_verified` and `time_factor_verified` stay false until a runtime probe proves them. Features degrade
  to conservative class-based behaviour, never to a guess presented as measured.
- **Interplay with in-flight work.** The `xrEndFrame` fix and the Blink-aim work touch `game_xr.hpp` and the hand
  publication in `dxgi_probe.cpp`. Part 3 (new OpenXR actions) must rebase on them. No part may change swapchain or layer
  handling.

## Verification model (applies to every part)

Each part is verified in three tiers. A part is not done until all three are recorded.

- **Tier A, offline labs.** Pure-logic checks on the Linux harness, written RED-first, in the style of phase 1's push lab:
  a small deterministic simulation with judged scenarios (pass/fail limits) and unjudged info lines, plus mutation
  checks for the key thresholds. New labs: a corpse-drag lab (part 1), a throw lab (part 2), a curl check (part 3), a
  scoring lab (part 4), a held-clamp lab (part 5A). Smooth per-frame input is the judged variant (phase 1 learned that
  30 ms stepped input misleads).
- **Tier B, simulated-VR scripted runs** on the planned Linux rig. Scenarios are scripted controller poses plus input
  (grip, trigger, touch) against the real game process with the real DLL, reading the status JSON and the opt-in CSV
  trace. They prove the engine side: that a corpse can be woken and held, that 200 grab/release cycles cause no fault, that a
  dropped tank ends above the floor. What the rig must provide (assumption, *builder's call*): scripted poses for head and
  both hands, scripted button, grip, trigger and touch values, a repeatable way to place a corpse and a tank near the
  player, and a way to read the status JSON. If the rig is not ready when a part lands, the part ships on Tier A plus the
  headset checklist, and Tier B scenarios are listed as outstanding in the ledger.
- **Tier C, a short headset checklist**, per part, in the style of `docs/VR-physical-hands-TEST-CHECKLIST.md`: few
  items, ordered by risk, with the status fields to save. The user runs it. Only this tier can declare a feel target met.

The scenario lists below are descriptions, not scripts.

## Shared foundations (built with part 1, used by all parts)

These come first because parts 1, 2 and 5A all need them.

### F1. Runtime mass probe

The velocity cap needs a body mass. The mod reads an inverse mass at `motion+0x26` (u16) with an undocumented scale
(research Q4; corpse prep §5 open RE question 1). [U]

- Add a read-only probe that logs, for props and each of the 17 ragdoll bones, the raw `+0x26` value next to the Havok body
  record, in a simulated run and in the headset. Compare against objects of known size (bottle, crate, guard torso).
- Until the probe settles the scale, mass comes from a **class table** *(builder's call)*: per ragdoll bone kind (hand,
  forearm, calf, foot, chest, neck, pelvis) and per prop class, seeded from phase 1's ragdoll limb handling
  (`heavy_kg_ragdoll` = 40 kg, props 12 kg). Status reports `mass_source` as `class` or `measured`, and `mass_verified`.
- **Safety gate:** an implausible read (zero, non-finite, above the table by a wide margin) falls back to the class value for
  that body and counts `mass_read_rejected`.

### F2. Force-capped follow (the Bonelab force law, in velocity form)

Bonelab limits how hard the hand can pull. The hand is driven by forces clamped per axis, with separate forearm-direction
and lateral strengths, and an effective mass that includes the held object (Bonelab §1.4, §2.1). Our engine only takes
velocity writes (research Q4), so we express the same law as a cap on the *change* of speed per motor tick:

- `Δv_max = F_max · dt / (m_object + m_hand)`, and `v_new = v_object + clamp(v_motor − v_object, Δv_max)`.
- Starting caps from Bonelab: about **300 N** along the arm axis and about **90 N** laterally. (Bonelab's calibrated hand
  limits are 320–450 N forward and 89–112 N sideways.) Angular: a torque cap of about **25–30 N·m** divided by an inertia
  estimate (Bonelab maxTorque 30 N·m).
- A mass **floor** keeps very light limbs from getting infinite acceleration, and a mass **ceiling** keeps heavy bodies
  movable. Both are lab-tuned.
- Two hands on one body: each hand's mass share is half (Bonelab: hand + 0.5 × object when two-handed). Because our two
  hands hold *different* limbs and drive independently, the cap is applied per hand and the lab checks the combined effect on
  the body (no double pull beyond the sum of the two caps). *(builder's call)*

This law is already planned for the phase-1 push model (ledger carry M6). In this phase it is applied to *held* things. The
hand mass is the small fixed value phase 1 already uses; adopting the full law with a physical hand mass belongs to part 5B.

### F3. Write-safety envelope for ragdolls *(builder's call)*

All of the following apply to anything that writes to a corpse:

- Per-tick: validate world, body handle generation, component generation, finite inputs; speed and spin caps; SEH guard.
- **Event posts are budgeted:** `START_RAGDOLL 0xb9` at most once per wake of a given entity, never re-posted while that
  entity's primary body already reads dynamic (`flags & 7 == 2`), and never on the 400 ms re-post timer for corpses.
  `VELOCITY_IMPULSE 0x149` is not used by anything new. A global ceiling (starting value 2 posts per second) protects against
  loops. A kill-switch file `build/no-ragdoll-posts` disables all posts and a second `build/no-corpse-grab` disables the
  whole physical corpse path. Both are read at start and logged.
- **Runaway detector:** if any of the 17 bodies reads above a speed limit (starting value 12 m/s) or non-finite while held,
  the hold is released, no further writes go to that entity for 1 s, and `runaway` is counted. The mod never "corrects" a
  runaway by writing more.
- **Session fault latch:** any SEH fault on a ragdoll write disables ragdoll writes for the rest of the session and sets a
  visible status flag.
- **Lifecycle:** holds drop on world change, load, death reload (logic 0 / bit 15, phase-1 task 5), entity gone, tracking
  loss, and focus loss. A held body never survives a streaming change.

### F4. Diagnostics conventions

New status block per part, camelCase-free JSON like the existing `hand_clamp` block. Counters are per side where it makes
sense. An opt-in CSV trace (`build/bonelab-trace`, one row per motor tick while held) is bounded and written off the render
thread, like the carry trace. Field names are the implementer's choice, but the fields named in each part must exist.

## Part 1. Grab and drag/lift corpses (limb or collar)

### What the player feels

Squeeze a full fist (grip plus trigger, the existing gate) on a dead or unconscious guard's forearm, wrist, calf or ankle,
or on the collar. The hand takes hold at that spot. Pulling drags the body along the floor with limbs trailing. Lifting
raises the limb and the body hangs from it, heavily. Moving fast makes the body lag and swing, not teleport. Opening the
hand drops it. A body never jumps, spins up or flies off.

Pressing grip alone still does the game's native action (the shoulder carry prompt). *(builder's call; see open question 1)*

### Bonelab technique followed

- Grips keep the object **dynamic**; nothing is parented or made kinematic (Bonelab §1.2). We keep the ragdoll dynamic and
  drive it.
- The hand has **limited strength** and holding a heavy object lowers its acceleration; the held mass is part of the hand's
  effective mass (Bonelab §1.4, §2.1). Our F2 cap implements this.
- A **snatch** grab never knocks the object away: the grab attaches without a fling (Bonelab §1.3). Our bind stores the
  hand-to-limb offset at the contact point and starts the motor with zero commanded jump. The drawn hand blend onto the
  grasp point takes 0.25 s (Bonelab's pose blend). *(builder's call: 0.25 s taken from Bonelab's pose-blend time)*
- A loose grip lets an object swing; a held limb is free to hang from its joints. We do not add stiffness.

### Behaviour decisions

- **Targets.** Limb bones stay as today: forearms and hands (bones 6, 7, 9, 10), calves and feet (12, 13, 15, 16).
  Collar adds the **chest and neck bones (2 and 3)** under the same full-fist gate, with the same motor and the same
  caps. The head, spine and pelvis are not grab targets. *(builder's call)*
- **Which bodies.** Require the entity flags `+0xd1` dead (0x08) or unconscious (0x20). Today the reader only checks motion
  flags, so a live active ragdoll also qualifies. Live guards keep their existing yank path, unchanged. *(builder's call)*
- **Lift.** The effective mass seen by the cap includes the bone and an estimate of the rest of the body that hangs from it
  once the pelvis is off the floor (a fraction of the total, starting value 0.5, lab-tuned). So one hand can drag a body
  easily, and lifting a whole body with one hand is slow and limited by the cap. How heavy that should be is a taste call
  (open question 2). *(builder's call: the fraction and the default)*
- **Pelvis tow.** The existing tow (a second velocity write on the pelvis, up to 4 m/s horizontal) is a bridge that bypasses
  the ragdoll joints and contradicts the "never substitute pelvis towing" note in `docs/GENERAL-PHYSICAL-GRIP.md`. In this
  phase: cap it by mass, keep it horizontal (z = 0), disable it when the limb motor is already saturated, and put it behind a
  flag. The lab compares limb-only against limb-plus-tow before the default is chosen. *(builder's call)*
- **Drawn hand.** While a corpse is held, the drawn hand and arm IK target use the **grasp anchor pose** (the limb or
  collar pose times the stored offset), bounded to 0.35 m from the raw controller (phase 1 C3 distance). That removes the
  "invisible point" feel for bodies. Raw controller pose stays the intent. This replaces phase 1 C9's carry exemption **for
  corpses only**. Prop holds keep today's behaviour because the user accepted it. *(builder's call; see open question 6)*
- **Release.** The hold drops (a) when the player opens the fist, (b) when the grasp anchor lags the raw controller by more
  than 0.5 m for 0.25 s (heavy body or stuck on geometry; replaces the old 1.5 m error release), (c) on any F3 lifecycle
  event, or (d) on a runaway. Release writes no velocity beyond what part 2 specifies. A short regrab cooldown of 0.3 s
  follows a forced release (Bonelab §1.7). *(builder's call: thresholds)*
- **Awake guards.** Contact and yank behaviour unchanged. The phase-1 rule stays: an awake guard stops the hand and nothing
  else happens.

### Engine levers

- Velocity setter 0xc85dc0 under the 0x89fb0 dispatcher: the only motor writer, already used on limb and pelvis [V]
  (corpse prep §2).
- 17-body ragdoll map, `native_ragdoll_body`, `nearest_ragdoll_body`, `ragdoll_grab_bone` (gate to widen for bones 2 and 3).
  Ownership checks and exclusion of carried bodies already exist from phase 1 task 7.
- `START_RAGDOLL 0xb9` via 0x8112c0 wakes a settled corpse. [V used, crash-suspect]
- Velocity readback from the motion (+0x40) for the held bone each tick. Used for the lag, the runaway detector and part 2.
- `PhysicalGrab::motor` (error × 18 plus hand feed, clamp 10 m/s and 5 rad/s) gets the F2 cap.
- **Unverified / needs RE or a runtime probe:**
  - Mass scale (F1). [U]
  - Whether `0xc85dc0` on one limb keeps all 16 joints stable in fast lifts, or stretches them. Measured in the drag lab
    and the Tier B runs. [U]
  - Collision layers and pairs of a held corpse against the player capsule, hands, doors and walls (corpse prep open
    question 4). Not needed for the first version because the player is excluded by the existing body layers, but a held body
    pressed into the player capsule must be watched (diagnostic `body_near_player`). [U]
  - Which press wins when the game shows a carry prompt and the player makes a full fist (corpse prep open question 6).
    Observed in Tier B and the headset. [U]
  - Ragdoll ownership across wake, settle, reload and streaming (corpse prep open question 5). [U]
- Constraint route 0xc90de0 stays an optional spike. It is entered only if the velocity motor fails the lab or the headset
  twice, and it is not part of this design's done criteria. *(builder's call)*

### Safety gates

F3 in full, plus: no write when the entity is not dead/unconscious; no write while the native carry or an NPC drag owns the
body (`+0xc8` parent, `+0xd2` lease, the phase-1 carry gate); per-tick Δv cap from F2; position error beyond 0.5 m is a
release, not a bigger write; collar grabs use the same caps, with the neck/chest bone mass from the class table.

### Diagnostics (status block `corpse`, plus opt-in CSV)

Per side: `holds`, `binds`, `releases` split by reason (open fist, lag, lifecycle, runaway, fault), `motor_samples`,
`capped_writes`, `cap_fraction_mean` (how often the cap bound), `mass_source`, `mass_read_rejected`, `lag_m_max`,
`peak_body_speed`, `peak_bone_speed_all17`, `runaway`, `tow_writes`, `tow_disabled_saturated`, `event_posts_b9`,
`event_posts_suppressed`, `fault_latch`, `wake_attempts`, `body_near_player`, bone histogram of what was grabbed
(limb versus collar), `drawn_anchor_gap_m`.

### Verification

- **Tier A, corpse-drag lab.** A simulated 17-body chain with joint limits and a floor, driven by the real motor code
  with smooth hand input. Judged (starting limits, tuned during the first task):
  - drag 3 m along the floor: the body follows within 0.5 s of the hand stopping, with no body above 6 m/s;
  - lift a limb 0.5 m: the force implied by the commanded Δv never exceeds the F2 cap by more than 1%;
  - a 4 m/s flick: peak speed on all 17 bodies stays under the runaway limit;
  - release at rest: no velocity written;
  - runaway detector fires on a seeded spike and the hold ends without a counter-write;
  - tow on/off comparison is recorded as an info line.
  Plus mutation checks on the cap, the speed limit and the post budget.
- **Tier B scripted runs.**
  - S1 wake a settled corpse, grab the wrist, drag 3 m, release.
  - S2 lift by the ankle to 0.5 m and put down.
  - S3 collar grab and lift.
  - S4 two hands on two limbs of one body.
  - S5 stress: 200 grab/release cycles across 5 bodies, with 0 faults, `event_posts_b9` within budget, no body above the
    speed limit.
  - S6 grab then trigger a load / death reload mid-hold: the hold drops, no fault.
  - S7 grab with the native carry prompt on screen: record which action wins.
  - Collect the F1 mass-probe table in one of these runs.
- **Tier C headset (6 items).**
  1. Drag a dead guard by the wrist, then by the ankle, 3 m.
  2. Lift by an ankle and by the collar: heavy but possible, nothing flies off.
  3. Put the body down on stairs / against a wall: no jitter.
  4. Same with an unconscious guard, who may wake up while you hold him.
  5. Two hands on two limbs.
  6. Grip alone near a body: the game's native carry still works.
  Save the `corpse` block after each. The old v435/v436 crash habit (walk the body around for two minutes) is part of item 2.

## Part 2. Throw history and spin

### What the player feels

A flick throws the object in the direction the wrist was going, at the speed of the flick, and with the spin the wrist put
on it. A slow lowering does not throw. Walking forward while releasing adds your walking speed once. A light bottle can be
thrown fast. A heavy body or crate cannot be, because it never got up to speed in the hand.

### Bonelab technique followed

(Bonelab §1.5)

- Sample the **held body's velocity relative to the player's own body**, plus its angular velocity, every physics step at
  100 Hz, into a ring of **50 entries** (0.5 s).
- At release **skip the newest ~2 steps** (about 20 ms), because the hand slows as the grip opens, and **average the next
  ~7 steps** (about 70 ms).
- **Direction** = normalised sum of the window. **Magnitude** = the **maximum** speed inside the window, so a flick keeps its
  peak.
- Final velocity = player (whole-body) velocity + **1.1** × history result. Angular velocity comes from the same window
  without the player term.
- Throws are body-relative, so walking adds momentum once.

### Behaviour decisions

- **Sample the held body, not the hand.** Read the body's own velocity back each motor tick (the sampler in
  `PhysicalGrab::sample` currently low-passes hand position with a 35 ms time constant, clamped to 12 m/s, world-relative,
  and returns no angular velocity). Because the body's speed is already limited by part 1's force cap, a heavy object
  produces a slow throw automatically. This is the Bonelab behaviour and removes the need for a separate mass rule for
  throws. If the readback fails, fall back to the hand velocity limited by the same cap. *(builder's call)*
- **Time-based window, not step-based** *(builder's call)*: the mod's motor tick is the 0x89fb0 callback (not a fixed 100 Hz),
  so samples are stamped with time. The window is "skip 20 ms, then average 70 ms" with the Bonelab constants, and the ring
  keeps the last 0.5 s.
- **Player velocity** is the mod's player root velocity (from the same root tracking the phase-1 clamp uses). If it is
  unavailable or non-finite, subtract nothing and add nothing (throw is world-relative for that release).
- **Caps.** Keep the existing 12 m/s linear ceiling for props. Angular ceiling 20 rad/s for props. For corpses the
  ceiling is lower (6 m/s linear, 8 rad/s angular). *(builder's call: all four numbers; Bonelab only has the engine's
  60 rad/s global angular limit)*
- **Corpses are thrown inert.** No damage, no hit registration (today only props are registered as thrown, and corpse
  release already throws the limb with hand velocity but no tracking). The thrown body's tracking gap (corpse prep §5.5) is
  deliberately left open. *(builder's call; open question 4)*
- **Slow motion.** Raw velocity writes ignore Bend Time's per-motion time factor (research Q2.3). Throws keep today's
  behaviour until `time_factor_verified` flips. Gate: if the time factor is readable and below 1, no extra scaling is
  guessed. Status records the release time-factor for the headset test. *(builder's call)*

### Engine levers

0xc85dc0 already takes both linear and angular vectors [V], so release can write spin. Readback of the body's velocity from
the motion. The ang velocity from consecutive hand basis rotations is the fallback source. Unverified: scale of the
angular readback and whether the motion stores the angular velocity in the frame the setter expects. [U] The first task
checks that a write-then-read of an angular velocity round-trips.

### Safety gates

Finite checks on every sample and on the final result; speed caps above; a throw is never larger than the cap-limited body
speed times 1.1 plus player speed; a release with fewer than 3 valid samples falls back to zero throw velocity (drop), not a
guess; SEH-guarded write as for all velocity writes.

### Diagnostics (status block `throw`)

`releases`, `samples_valid`, `window_samples_used`, `v_hist` and `v_final` (last), `omega_final`, `player_v_added`,
`readback_failed`, `fallback_used`, `cap_hit` split by linear/angular, `peak_v_final`, last-release time factor.

### Verification

- **Tier A, throw lab.** Synthetic flick trajectories (smooth, with a decelerating tail like an opening hand, and with
  walking). Judged: the result direction is within 5 degrees of the pre-deceleration flick direction; speed is within 5% of
  the peak in the window times 1.1; the player's speed is added exactly once; spin follows the wrist; a sampler with noise
  injected still stays under the caps; the old sampler is kept as a comparison info line. Mutation checks on skip and window
  lengths.
- **Tier B.** S8 scripted flicks at three speeds with a bottle, a crate and a corpse, reading `v_final` and the body's actual
  speed one frame later; S9 throw while walking at 2 m/s.
- **Tier C (4 items).** Throw a bottle at a far wall; flick a book to spin; throw while walking; try to throw a body and a
  heavy crate (should barely move).

## Part 3. Touch-sensor finger curl

### What the player feels

Fingers follow the real fingers. Resting a finger on the trigger bends the index a little before you pull. Lifting the index
off the trigger while the other fingers are closed points. Taking the thumb off the stick, buttons and rest opens the thumb
(thumbs-up). The hand does not look dead. A held object always shows a firm closure, and the contact wrap from phase 1's
`finger_ik` still stops fingers at surfaces.

### Bonelab technique followed

(Bonelab §3.1–§3.2)

- **Index** curl = trigger + a "touch simulation" value that slews toward **0.3** while the trigger is touched and back to 0
  otherwise, at **10 per second**. A resting finger is half bent.
- **Middle, ring, pinky** curl = grip axis × **1.3** − **0.1**, limited to 0..1.
- **Thumb** = fully closed if any thumb sensor is touched (stick, A/B/X/Y, thumb rest), else open, slewed at **25 per
  second**.
- **Smoothing** on every finger: step toward the input by `clamp01(20 · dt)` each frame (about 50 ms first-order).
- **While holding**, the finger curl is floored at **0.6**. On hover, fingers pre-curl toward the grip in proportion to the
  squeeze (this half lands with part 4's candidate focus).
- Grab/release thresholds are not copied. Bonelab uses 0.99 / 0.5 on a solved grip, but Dishonored shares the squeeze with
  native Use, so the mod's own thresholds stay (0.45 / 0.35).

### Behaviour decisions

- **Scope** *(builder's call)*: the touch-driven curl applies to **empty hands**. A hand holding a weapon keeps the captured
  native sword/gun fist, and a hand holding a prop or corpse uses the 0.6 floor and the existing contact wrap.
- **Controllers.** Touch and Index profiles expose the needed touch inputs (`trigger/touch`, `thumbstick/touch`,
  `a|b|x|y/touch` or `a|b/touch`, `thumbrest/touch` on Touch; trackpad touch on Index). Controllers without touch sensors
  (Vive wand, WMR) keep the current mapping. The mod chooses the mapping by the interaction profile it already queries for
  phase 1's calibration. *(builder's call)*
- **Final clamp.** `finger_ik` contact wrap stays the last stage, as in the Bonelab study's recommendation: the procedural
  wrap is more general than Bonelab's authored poses, so it is kept.
- **Tuning knobs** for the 0.3 touch value, slew rates and the 0.6 floor, so the user can adjust in the headset.
  *(builder's call)*

### Engine levers

`game_xr.hpp` currently binds only trigger/value, squeeze/value and click actions, no `/touch` actions. New optional
boolean actions, with suggested bindings per profile, carried in the publication; `arm_tracking.hpp` builds the curls
(thumb `max(squeeze, .5·index)` today). **Unverified:** whether the runtime (SteamVR, Virtual Desktop/OpenXR layer)
reports touch for every profile. Treat as optional: a failed action creation or binding suggestion must leave the current
mapping live, never throw, as with phase 1's optional profile calls.

### Safety gates

Actions are optional and fail soft; values finite and clamped; curl is visual only (C9); rate-limited by the slew; no new
swapchain or layer work; no change to the action set that the game's native input depends on.

### Diagnostics (status block `fingers`)

Profile name, `touch_available` per side, raw touch booleans, the five curl outputs per side, `mapping` (`touch` or
`legacy`), `floor_active`, `action_create_failed`.

### Verification

- **Tier A, curl check.** Judged: every input combination yields the Bonelab mapping within 1e-4 (index at rest 0.3 after
  settling, middle/ring/pinky at 0 for grip 0.08 and 1 for grip 0.85), slew times to 63% at the stated rates, floor 0.6
  when held, legacy mapping unchanged when `touch_available` is false, no value outside 0..1. Mutations on the rates and
  constants.
- **Tier B.** S10 scripted touch and trigger values over time; assert the published curls.
- **Tier C (4 items).** Rest the finger on the trigger; point; thumbs-up; hold a bottle and a body (curl at least firm).
  Report any chatter. Check the resting index half-bend feels right (tunable).

## Part 4. Palm-facing grab scoring

### What the player feels

Reaching toward something with the palm toward it picks that thing, even when something else is slightly nearer behind the
knuckles or under the wrist. The pick holds steady when the hand shakes. Reaching *through* a nearer object toward a farther
one while moving the hand that way prefers what you are swinging toward.

### Bonelab technique followed

(Bonelab §1.1)

- A hover sphere sits **in front of the palm**: its centre is pushed 0.015 m along the palm axis and 0.8 × the radius along
  the palm direction.
- Cost, lower is better, with distance `d_n` normalised by **a quarter of the avatar's arm length**:
  - **Facing term** `((0.6 − dot(palmDir, toTargetDir))² + 1)`. Facing the target is cheap, the back of the hand costs about
    three times more.
  - **Distance term** `(d_n² + 1)`.
  - **Approach term**: the closing speed between hand and target lowers the cost.
  - **Priority** multiplier, default 1.
  - **Hysteresis**: the currently hovered candidate's cost is multiplied by **0.86**.
  - **Cut-off**: very high cost rejects the candidate.
- Far hover (force grab) is not part of this phase.

### Behaviour decisions

- **Replace the "nearest contact" pick** in `grab_focus_candidate` with the cost above, keeping the existing 0.45 m gate and
  0.55 m retain distance (the gates are the mod's, Bonelab's 6000 cut-off is effectively unreachable at our ranges).
  *(builder's call)*
- **Candidates** are props and, after part 1, the grab bones of corpses (limbs and collar), scored by the same formula.
  Priority is 1.0 for all at first, with per-kind priority knobs. *(builder's call)*
- **Arm length** for normalisation comes from the user's calibrated arm span in the existing calibration; default 0.65 m if
  uncalibrated. *(builder's call)*
- **Approach term** shape is not fully reduced in the Bonelab decompile (Bonelab §1.1 item 4, "inferred"). We use a simple
  form: a bounded multiplier between 0.8 and 1.2 from the closing speed projected on the hand-to-target direction, tuned in
  the lab. *(builder's call)*
- **Palm direction** is the calibrated palm axis derived from the **raw** controller pose plus the phase-1 palm fit
  (translation and tilt). It does not come from the clamped or anchored drawn hand. This follows C9 (selection reads raw)
  and resolves the parked "raw path gets translation for grab reach" item in favour of using the fit for selection geometry
  only. *(builder's call; verify in the headset that reach feels right)*
- **Hover pre-curl** (part 3's second half): when a candidate is focused, the hand pre-curls toward `0.5 × squeeze`
  proportional to closeness, using the candidate's size. Visual only.

### Engine levers

All mod-side: `grab_focus_candidate` in `native_grab.hpp`, `selection_policy.hpp` for constants, `hand_bind.hpp` for the
palm frame, bounds already read for focus. No new engine calls, nothing unverified in the engine.

### Safety gates

Only reorders existing, already-validated candidates; the 0.45 m gate keeps it from ever choosing something unreachable;
non-finite palm direction falls back to the old nearest pick and counts `scoring_fallback`.

### Diagnostics (status block `grab_focus`)

Per side: chosen candidate id/kind, its cost and the runner-up's, `switches` per 10 s (chatter indicator), `hysteresis_held`
count, `scoring_fallback`, palm-direction source.

### Verification

- **Tier A, scoring lab.** Fixed clutter scenes with expected winners (palm toward a far book beats a near but behind
  object; back of hand loses; approaching object wins ties), hysteresis (a ±1 cm jitter never flips the choice; a clear
  change does), cost monotonic in distance, mutation checks on 0.6, 0.86 and the gates.
- **Tier B.** S11 scripted palm sweeps over a table with three props; assert `switches` and the final pick.
- **Tier C (3 items).** Reach into a cluttered shelf; hold still and watch for flicker; reach for a body's wrist next to
  its torso. Accepted when it picks what the player means in most reaches.

## Part 5. Phase-2 Havok hand body: held objects collide with the world

The user's goal for this item is that held objects physically collide with the world, and that the dropped-tank bug goes
away. Whether that needs a physical hand body is not known. This part therefore has two pieces, both listed in the agreed
order slot (5). *(builder's call to split; open question 5)*

### Part 5A. Held-body clamp and safe release (ships in this phase)

**What the player feels.** Carrying a tank or crate into a wall or floor stops the object at the surface and the hand with
it, with no sinking, no jitter, and no flying out. Dropping a tank leaves it on the floor, not inside it.

**Bonelab technique followed.** Bonelab does not let a held object pass through the world: the held object is a dynamic
body on the hand's joint, and the hand is force-limited, so the hand lags when the object is blocked (Bonelab §1.2, §2.1;
also "the hand simply lags when blocked", §2.2). We have no joint, so we reproduce the *outcome*: the drive target for the
held body is clamped to a position where the body is clear of the world, and the drawn hand stays with the held body.

**Behaviour decisions.**
- **Clamp the target of the held body**, not only the hand. Sweep the held object's proxy (a sphere or a few spheres from its
  bounds) from its last position to the new target with the engine cast (0xc89bc0), excluding the held body itself (the cast
  has a source-body exclusion, research Q1). Hits become planes in the phase-1 clamp machinery (world outranks entity
  static outranks dynamic; same solver). The drive goes to the clamped target.
- **Drawn hand follows the held body.** While the object is blocked, the drawn hand and arm IK target stay at the grasp point
  on the stopped object, bounded to 0.35 m from the raw controller. Beyond that the hand shows the clamp's snap-back (phase 1
  G3 behaviour) and the hold is released after 0.25 s. *(builder's call)* This reverses phase 1 C9's "does not apply to a
  hand carrying a prop" for blocked holds only. Free carrying is unchanged.
- **Safe release.** On release, check the object for overlap with the world (a zero-length or short cast from the held pose).
  If it overlaps, keep the DEBRIS layer and keep driving a small outward velocity along the contact normal (capped at
  1.5 m/s, at most 0.5 s) until it is free, and only then restore RIGID_BODY. If it never frees, restore anyway and count
  `release_overlap_unresolved` (so the failure is visible, not silent). *(builder's call)* Teleporting out with
  `setBodyTransform` 0xc85860 is not used: it is unproven in this mod.
- **Layer swap upkeep.** Phase 1 task 10 already re-applies the DEBRIS swap after a transient clear. Keep it. The release
  path must not restore the layer while an overlap check is pending.
- **Bounds source.** Props and bodies expose a bounding sphere or per-bone radius; large irregular objects (planks, the
  tank) use a few spheres along their long axis. If bounds are unreadable, 5A does not clamp that object and reports
  `held_clamp_no_bounds`. [U]

**Engine levers.** Cast 0xc89bc0 on the game-thread Stage0 scope with body exclusion [V]; the cast budget is shared with the
phase-1 cast budget and C14 fallbacks; the hand is exempt from its own palm sweeps while carrying, so those slots are free
for the held-body sweeps (budget stays ≤ 4 casts per side per frame). Layer words at `body+0x6c`; the engine layer wrapper
0x97600 defers when physics is busy [V]. Unverified: how the HoldObject keyframed bind reacts when its target is clamped
rather than driven (the engine keeps keyframing toward whatever the mod substitutes; expected to be fine) [U]; whether the
query filter reports walls through the held-body exclusion exactly as in phase 1 [U, the phase-1 `exclusion` counters apply].

**Safety gates.** Same SEH/finite/generation checks; a held-body clamp never moves the body away from the player faster than
the existing keyframe clamps (25 m/s, 40 rad/s are already high; the new clamp is stricter); no new write type.

**Diagnostics (status block `held_clamp`).** `sweeps`, `hits`, `clamped_frames`, `cast_us`, `no_bounds`, `overlap_on_release`,
`release_depen_frames`, `release_overlap_unresolved`, `drawn_hand_gap_m`, `layer_at_release`.

**Verification.**
- Tier A, held-clamp lab: extend the push lab's wall/floor scenes with a held body: carried into a wall at 0.1 to 3 m/s the
  body ends ≤ 1 cm past the plane (judged, same style as `wall_cast`), a release inside the floor resolves within 0.5 s, a
  lab release into free space is untouched, smooth input only.
- Tier B: S12 scripted carry of the tank into a wall and onto the floor; S13 scripted drop from 1.5 m onto the floor and
  from a hold with the tank pressed into the floor; assert the tank's final height is above the floor plane after 5 s and
  `release_overlap_unresolved` stays 0; S14 100 drop cycles.
- Tier C (4 items): carry the tank into a wall and the floor; drop it from chest height; drop it while pressed to the floor;
  carry a plank through a doorway. Save `held_clamp`.

### Part 5B. Physical hand body: time-boxed spike, then a decision

This is the research's Phase 2 (research, Recommendation). It is **a spike with a go/no-go**, not a feature, in this phase.
*(builder's call)*

**Goal of the spike.** Prove four things on a single dynamic sphere hand body driven by the force-capped servo (the F2 law
with the hand mass in the effective mass, separate forward and lateral caps, and the chest reaction dropped): no crash for
10 minutes near props, guards and doors; the player is never pushed; save/load is clean; frame-time delta is under 0.2 ms.

**Bonelab technique followed.** The hand is a physical body pulled by clamped forces with braking, and it collides with the
world and holds objects through joints (Bonelab §2.1, §1.2–1.4). The full force law (muscle-space clamp, braking blend,
jerk filter) is adopted only here. This is the phase-1 ledger's M6 carry.

**Engine levers (from research, [V] unless marked).** `createBody` 0xc8e5b0 (22 engine callers), cinfo ctor 0xca65f0,
`destroyBodies` 0xc8f6b0, `setBodyMotionType` 0xc86800, `applyHardKeyFrame` 0xc86420, `setBodyTransform` 0xc85860 (for
snap-back), the engine command queue drained by 0x89fb0 with records of a known header, layer plus collide-with word at
`body+0x6c` with a mutual test (0x992c0), so a mask omitting layers 6, 7, 8 and 18 never collides with the player [V
mechanism]. **Blocking unknown:** the body userdata contract at `body+0x90` — the pair filter 0x93a40 dereferences it, and
the contact-impulse handler 0x8527e0 can call through it — so a mod-created body needs engine-shaped userdata or another
route to have the engine create the body. Also unknown: a box/capsule shape constructor, motion-properties ids, the drain
thread and timing, the disabled-pair add routine (hand versus held object), AI reactions to hand contact with live guards,
and the real filter words of the player bodies (research open items 1–12). [U]

**Spike steps.** (a) Read-only runtime reads of the player bodies' `+0x6c` words and a prop's userdata. (b) Build one body
with engine-shaped userdata in an isolated harness build. (c) Drive it with command-9 records. (d) Run the four proofs.
(e) Write up findings with a go/no-go and a size estimate for making the drawn hand follow the body (research Phase 3) and
for doors. The user decides what happens next.

**Safety gates.** Behind a build knob (`build/hand-body-spike`), off by default, never part of a normal run. The spike
creates and destroys bodies only while the engine is not busy or through the engine queue, destroys on world change, load
and exit, and latches off after any fault. Nothing in the normal path depends on it.

**Verification.** Tier A: the research's offline tests (`hand_command_encoding_check`, `hand_filter_mask_check`,
`hand_body_lifecycle_check`, servo-law check) are written before the spike touches the engine. Tier B: the long-run and
save/load proofs on the sim rig. Tier C: one short run with the knob on if the spike passes; otherwise none.

## Dependencies and order

| Part | Needs | Reason |
|---|---|---|
| F1–F4 | nothing | Mass probe, force cap, write-safety envelope and diagnostics conventions, built with part 1 |
| 1 corpses | F1–F4 | First, see below |
| 2 throw | 1 (F2 cap, held-body velocity readback, release path) | Throw samples the held body, which only moves at cap-limited speed |
| 3 finger curl | nothing technical; rebases on the `game_xr.hpp` changes in flight | Touch inputs are independent; the hover half needs part 4 |
| 4 scoring | 1 (corpse bones as candidates), 3 (hover pre-curl, small) | Scoring must rank bones, not only props |
| 5A held clamp | 1 (held-body readback, lifecycle gates), 2 (release path) | It extends the release path and reuses F1–F3 |
| 5B hand body | 5A, F2 | The full force law and anything door-related needs the held-body and cap infrastructure first |

**Why corpses come first.**
1. The user called corpse moving "one of the core vr mode mechanics". It is the feature they care about most.
2. It is the largest risk. The existing path has never been accepted in the headset and its history includes crashes
   (ragdoll event posts). Learning the real failure modes early protects every later part.
3. It builds the shared pieces the later parts consume: the mass probe, the force-capped motor, held-body readback and the
   F3 write-safety envelope. Throw, scoring and the held clamp are thin additions once these exist.
4. Parts 3 and 4 are mod-side and low risk. They fit well after the risky engine work has settled.

Parts 3 and 4 may run in parallel with part 1 or 2 if the helpers allow, since they do not share write paths with them, as
long as `game_xr.hpp` merges are sequenced with the `xrEndFrame` fix. *(builder's call)*

## Decisions

Made by the user (quoted above, restated): the goal and the five-item order; props and guards both; corpses movable; phase 1
approved; every task brief cites the Bonelab technique or numbers it uses (in our own words, no code or assets).

Made by the builder, all marked above and listed here for review:
- C1. Collar = chest and neck bones, full fist, same motor; grip alone stays the game's native carry.
- C2. Physical corpse grab only for dead or unconscious bodies; live guards keep today's yank path unchanged.
- C3. Drawn hand shows the grasp point on the corpse (bounded to 0.35 m from the controller); prop holds unchanged.
- C4. Release at 0.5 m lag for 0.25 s, 0.3 s regrab cooldown, 0.25 s visual blend.
- C5. Pelvis tow capped by mass, horizontal only, off when the limb motor is saturated, behind a flag.
- C6. Ragdoll write-safety envelope (event-post budget, runaway detector, session fault latch, two kill-switch files).
- C7. Mass comes from a class table until the runtime probe verifies the encoding. Caps start at 300/90 N and 25–30 N·m.
- C8. Throw samples the held body's velocity with a time-based window; corpses are inert; caps as in part 2.
- C9. Touch curl applies to empty hands only; controllers without touch keep the current mapping; tunable constants.
- C10. Scoring keeps the 0.45 m gate; palm direction comes from the raw pose plus the palm fit; arm length from the
  calibration.
- C11. Part 5 split into 5A (held-body clamp and safe release, shipped) and 5B (hand body spike with go/no-go).
- C12. Frame budget for the new work ≤ 0.2 ms, on top of phase 1's cast budget.
- C13. Branch `feat/bonelab-feel` and a Tier-B rig assumption list.

## Left to the implementers

Exact file split and names; how the lab simulates the ragdoll chain; field names inside each status block (the listed ones
must exist); test names and fixtures; the numerical tuning of the starting values flagged above, as long as each change is
recorded with its lab result; whether the lift fraction and the tow default come out of the lab runs.

## Done means

1. Linux harness green (483/0/41 baseline plus the new labs), with the `-Wshadow` family still an error. The Windows build
   passes (user).
2. Tier A labs pass for parts 1–4 and 5A, with mutation checks killing the key thresholds.
3. Tier B scenarios S1–S14 recorded for the parts they cover, or listed as outstanding if the rig is not ready.
4. The part-5B spike report exists with a go/no-go. Part 5B is not part of "done" for shipping.
5. Tier C headset checklists pass for parts 1, 2, 3, 4 and 5A, as judged by the user, with status blocks saved.
6. No new fault counters rise in any run, and `cost_us` stays inside the budget.

## Resolved questions (2026-10-20, answered by the user)

The user answered the open questions on 2026-10-20 (these override the builder's earlier defaults C2, C3, C7 caps feel and C11):
1. **Collar:** full fist on the collar = physical hold (same motor, weaker leverage); grip alone stays the native shoulder carry. (user: "Physical hold")
2. **Body weight: forgiving.** One hand can lift and swing a whole body freely. Keep the Bonelab force-capped follow as the
   *technique* (no teleporting, smooth catch-up), but scale the caps/strength so a one-hand lift of a full body works; the caps
   live in a tuning file (`build/grab-tuning.cfg`, builder's call on the name); the first headset run tunes them. (user: "Forgiving")
3. **Awake guards: grabbable now — pull + stagger.** Grabbing a living guard pulls him toward the hand; he staggers / is pulled off
   balance and becomes alerted; living guards are NOT ragdolled. This joins Part 1 (new sub-part 1L). (user: "Yes, now" + "Pull + stagger")
4. **Thrown corpses:** inert. (user)
5. **Part 5: full physical hand body now** (no staged 5A/5B split): held objects truly collide with the world through a physics
   hand body (Bonelab-style physical hand). The held-body clamp + safe release may still exist as the safety fallback when the
   hand body is unavailable, but the deliverable is the hand body. (user: "Full hand body now")
6. **Props: hand on the grasp point for every prop**, Bonelab style (the visible hand sits exactly on the object's grasp point
   for bottles, crates, tanks — changes previously accepted behaviour; keep a knob to restore today's prop drawing). (user: "Hand on grasp point")

Builder-check answers (builder's calls):
- **Which holds the caps apply to:** only bodies the mod drives with velocity writes (the PhysicalGrab motor: corpses, limbs,
  physical-grab props). Native HoldObject items (bottle, glass, whale-oil tank, severed arm) keep the engine's keyframed bind
  and its existing clamps; part 2's throw sampler reads the actual body velocity in both cases. The Tier C item "a heavy crate
  barely moves" applies only to motor-driven props.
- **Unconscious guard wakes mid-hold:** treat a change of the `+0xd1` state away from dead/unconscious as a release reason
  (`release_reason=state_changed`, counter), open the hand with the 0.25 s visual blend and let the game's wake animation and AI
  take over; add a Tier B scenario if the rig can reach a sleeping guard, otherwise a Tier C item.
- **0xb9 START_RAGDOLL does not wake a settled corpse:** one retry after 1 s, then refuse the grab with a short haptic pulse and
  a counter; the 0xc90de0 constraint route becomes a separate spike, not part of part 1.
- **Linux simulated-VR rig:** exists (`linux/d2vr-linux sim`, scripted sessions in the real game, save backup/restore, menu
  presses via script v6); it is a prerequisite for Tier B. A test save next to the objects (oil tank, a corpse) is needed; the
  first plan task makes those saves or asks the user for them.
- **Anchoring mass and caps before the headset:** F1's probe run records the engine's mass field and the motor response for the
  class table in the rig; the lab is anchored to those recordings; the first headset run is a tuning session (see 2).
- **Collar bone mapping:** verified in the F1 probe run (bone index → body → name), before part 1 uses it.

## Original open questions (answered by default above)

1. **Collar.** You said "by limb or collar". Today a full fist on a limb is a physical grab, and the collar goes to the game's
   native shoulder carry. I chose to make a full fist on the collar a *physical* hold too (a body hanging by the neck, weaker
   leverage), and to keep grip-only as the native shoulder carry. Is that what you want, or should the collar always be the
   native carry?
2. **How heavy should a body feel?** With Bonelab-like caps one hand drags a body easily but lifts it slowly, and you need
   two hands to lift a whole body quickly. Do you want that (realistic), or a more forgiving feel where one hand can lift
   and swing a body freely?
3. **Awake guards.** This phase only hardens grabbing for dead or unconscious guards. The existing yank of a living guard
   stays as it is (best-effort, never headset-accepted). Is that acceptable, or do you want living guards physically
   grabbable now?
4. **Thrown bodies.** Should a thrown corpse be inert (my choice), or hurt/knock down guards it hits, or make noise that
   alerts AI? Each needs separate engine work and AI testing.
5. **Part 5 split.** I split item (5) into a shippable held-body clamp plus safe release (fixes the tank through the floor,
   no new engine objects), and a time-boxed spike for the real Havok hand body, with a go/no-go for you. Do you agree with
   that, or do you want the hand body built straight away at higher risk?
6. **Prop holds.** You said props work well, so I left prop holds drawing as they do today and only put the drawn hand on
   the grasp point for corpses and for blocked holds. If you want the hand on the grasp point for every prop too, that changes
   behaviour you already accepted. Say so if you want it.
