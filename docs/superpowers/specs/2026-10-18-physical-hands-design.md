# Physical hands, phase 1: hands that stop at the world (design, 2026-10-18)

Research behind this design: `docs/superpowers/specs/2026-10-18-physical-hands-research.md` (cited as "research §…").
Bonelab technique study: `~/.local/share/d2vr-re/bonelab/bonelab-research-2026-10-18.md` (outside the repo; techniques and
values only. No Bonelab code or assets go into the mod. All mod code is written from these descriptions and from
Dishonored's own engine).

## Intent and why

The user wants Dishonored 2 VR to feel like BONELAB. The visible game hand should *be* the physical hand. It collides with
the world, holds what it touches, lines up with the controller, and its fingers wrap whatever it holds. Props and ragdoll
guards are grabbable, and doors are physical. The user named the problems with the mod today (2026-10-18):

1. The arms have no physical presence. They pass through walls, bodies and furniture.
2. Props and guards are held by an invisible point, not the visible hand.
3. Hands, and especially fingers, are misaligned with the controllers.
4. Fingers do not react to held objects, weapon grips or surfaces.
5. The image sometimes breaks while carrying an object.

The user chose **hands colliding with the world** as the first thing to get right, because every later feature (grabbing,
fingers, doors) builds on that hand. This document covers that first phase. It also folds in the two problems that block
judging it in the headset: misalignment (3) and carry rendering (5).

## Goals (phase 1)

G1. **The visible hand stops at surfaces.** It never renders inside walls, floors, tables, doors or guards. Pressing a palm
onto a table or wall leaves the hand resting on the surface while the real hand keeps going. Sliding along a wall makes the
hand slide along the surface. A fast swat at a wall does not go through.

G2. **The arm follows the stopped hand.** The rendered arm (IK) reaches the stopped hand, not the controller, with no gap
between the wrist and the hand.

G3. **Snap-back.** When the real hand gets far from the stopped hand (reaching through a wall, or teleporting the body), the
visible hand returns to the controller cleanly, never through the surface it was stopped on, and without flicker
(hysteresis).

G4. **Pushes have weight.** A light prop the hand presses into moves with the hand. A heavy prop barely moves and holds the
hand back visually. Fixed objects do not move. Pushes respect the game's slow-motion time scale.

G5. **The hand sits in the controller.** A per-side calibration of the grip-to-palm offset (translation plus tilt) puts the
game palm where the user's palm is around the controller. The calibration is saved per side and per controller profile.

G6. **Carrying does not break the image.** Instrument the carry path so the candidate causes (research Q6) can be told
apart from a headset run. Fix the statically proven one: the DEBRIS layer swap lost after a transient `clear()`.

## Non-goals (later phases, separate designs)

- Real Havok hand bodies (research Phase 2/3). Phase 1 is query-based (research approach A, tightened). The physics-body
  spike comes after phase 1 has passed in the headset.
- Grabbing with the visible hand, finger IK wrapping, ragdoll guard grabbing, physics doors (research Phase 4 and later
  designs).
- Pushing **awake** guards. Contact with a live guard stops the hand and does nothing else. *(Controller's call. The user
  delegated it. It avoids unpredictable AI reactions in the first version. Unconscious and dead bodies are pushable like
  props.)*
- Changing locomotion, turning, or anything the user already accepted.

## Anti-goals (a failure even if it technically works)

- The player's view or body moves because of the hands. Hands never push the player.
- The hand visibly pops, jitters or flickers between the stopped pose and the controller.
- Frame time regresses noticeably. The budget is ≤ 0.5 ms game-thread cost for both hands combined, measured through the
  status JSON.
- Anything that can crash the game on bad data. Every engine write keeps the SEH guard, finite checks and the existing
  fault counters from the 2026-10-17 review fixes.
- Combat breaks. Weapons and powers still work when the hand is stopped, including aim, the attack/block ownership rules and
  native animations.

## Constraints

- Branch `feat/physical-hands` from `fix/review-2026-10-17`, in worktree `~/github/dishonored2-vr-hands`.
- Engine: Void + Havok hknp via hooks at fixed RVAs. The exe is pinned (`b671f824…`). Engine facts come from the research
  doc and `rea-evidence.md` and are marked [V]erified / [I]nferred there. Anything marked [U] must be verified at runtime
  before relying on it.
- Existing seams to build on (research Q1/Q5):
  - `src/weapon_collision.hpp`, the chest-anchored sphere-sweep clamp;
  - `src/dxgi_probe.cpp`, hand publication around `:4035-4102` with separate `raw_grips`;
  - `src/full_body_driver.hpp`, `in.hand[i]` vs `raw_hand` (`:504-546`);
  - `src/body_ik.hpp`, wrist target and soft reach;
  - `src/hand_contact.hpp` / `src/native_hand_contact.hpp`, solid-hand pushes through 0xc85dc0;
  - `src/native_world_trace.hpp`, sphere cast 0xc89bc0, results one frame late;
  - `src/hand_bind.hpp`, the palm frame;
  - `src/native_held_object.hpp`, the carry path.
- Casts stay on the game-thread Stage0 scope where they run today.
- Builds: Windows MSVC (`tools\build-msvc.cmd`, `tools\build.ps1`) is authoritative. The Linux harness
  (`python3 tools/linux/run_build_cmd.py`, currently 468 passed / 0 failed / 41 skipped) must stay green, and new checks
  must run on it.
- Testing honesty: offline checks pin logic. Only the headset proves feel, comfort and visuals (AGENTS.md). Headset steps
  go to the user as a checklist.

## Decisions

Made by the user:
- Bonelab is the reference for feel. Start with hands colliding with the world. Later phases: grab props *and* guards,
  fingers wrapping, physics doors.

Made by the controller (the user delegated them; review them):
- C1. Phase 1 is query-based (approach A tightened). Real hand bodies come in a later phase.
- C2. Awake guards stop the hand only. Ragdolls are pushable.
- C3. Snap-back thresholds start from the research values: gap > 0.35 m for > 0.25 s, or any non-finite value. Re-acquire
  by a sweep from the chest. Keep the existing 1.5 m/s ease-out.
- C4. Sweep from the **previous drawn hand** to the current (reach-clamped) controller palm. Use the palm sphere (4–5 cm)
  plus a fingertip sphere. Clamp the controller target to arm reach before the sweep, so the clamp and the IK agree.
- C5. Push strength is mass-aware (`Δv ≤ F·dt/m`) and multiplied by the engine's time factor. The research reads the mass
  from motion +0x26 with an undocumented scale. That must be checked at runtime (research open item 8), and until then the
  push stays capped as it is today.
- C6. The grip-to-palm calibration is an explicit step in the existing calibration flow: hold the controller naturally,
  then confirm. It is stored per side and per OpenXR interaction profile, has sane bounds, and defaults to a fixed offset
  derived from the bind-palm audit (`docs/native-palm-controller-alignment.md`) when not calibrated.
- C7. Carry: add per-frame carry diagnostics (layer swapped/restored, residual age, render scale, quaternion norm) to the
  status/trace output, and re-apply the DEBRIS swap after any transient `clear()` while the hold continues. Other candidates
  wait for the diagnostic run.

Settled after the builder check (controller's calls unless marked as a project fact):
- C8. **Fix the `wall_cast` failure by finding its cause first.** The first plan task reproduces the push-lab failure
  (22.4 cm past the wall) and attributes it. Candidate causes:
  - the chest-anchored origin;
  - one-frame-late results;
  - the `!meeting.prop` exemption;
  - the 45 cm clamp.

  The fix follows the cause. If lag is a contributor, the sweep is extended predictively by the hand's velocity over one
  frame, which is the agreed fallback.
- C9. **Which pose each consumer reads.**
  - The **clamped** hand drives what is *drawn*: the rendered hand, the arm IK target and the drawn weapon pose.
  - The **raw** controller pose keeps driving *intent*:
    - aim rays and the crosshair;
    - attack/block detection and swing velocity;
    - throw velocity;
    - grab desire and selection;
    - interaction-ray routing;
    - the native attack/block ownership rules.
  - Native authored animations (takedowns, climbing) keep native ownership, and the clamp yields to them.
  - The clamp applies to empty hands and weapon-holding hands.
  - It does **not** apply to a hand carrying a prop in phase 1. The existing `!meeting.prop` exemption stays, because the
    carried body is moved by physics. That case belongs to the grabbing phase.
- C10. **Tracked arms are on in practice (project fact).** The launcher always passes `--tracked-arms`
  (`launcher/Probe.cs:108-130`). G2 therefore targets the tracked-arm IK path, and the IK-feed check is meaningful. No
  second feed path is needed.
- C11. **Reference frame of the stored hand.** The previous drawn hand is stored relative to the player root/body frame.
  Walking and turning carry it along, and each frame's sweep re-validates it against the world. The player's own motion
  therefore never drags the hand into snap-back, and a wall still stops it.
- C12. **Calibration UX.** A hold-and-confirm step is added to the existing calibration page (`docs/native-hand-calibration.md`
  flow, opened from Pause): "hold the controllers naturally, press trigger to confirm".
  - Storage key: side + the OpenXR interaction profile path.
  - Bounds: translation ≤ 8 cm per axis, tilt ≤ 30°.
  - Default when uncalibrated: 3 cm along the palm normal plus 15° pitch, a starting value from the research's 3–5 cm and
    10–20° estimate. It is tuned in the headset.
  - The builder chooses the exact page wiring within the existing flow.
- C13. **Time factor.** Pushes use the engine's time factor only once its source is verified at runtime (research open
  item 8). Until then, pushes stay capped as today, without time scaling. Phase 1 does not depend on it.
- C14. **Over the frame budget.** If the measured cast cost exceeds 0.5 ms for both hands, drop the fingertip sphere first.
  If it still exceeds the budget, sweep the two hands on alternate frames and reuse the previous result for the other hand.
  Report `cast_us` in the status either way.

## Left to the builder (planner/implementers)

- Exact file split, names, and how the clamp state machine is structured.
- Test names and fixtures, extending the existing `weapon_collision_check`, `grip_ik_pipeline_check` and push-lab tests
  where possible. The push-lab `wall_cast` check (drawn hand ≤ 1 cm past the wall plane) must pass.
- Status JSON field names for the new counters (clamp active, snap-backs, cast µs, calibration source).
- Whether the fingertip sphere is a second sweep or a capsule approximation.

## Done means

1. All offline checks pass on the Linux harness, including a passing push-lab `wall_cast` and new checks for:
   - the clamp policy (sweep from the previous pose, corners and grazing, a fast approach with one-frame-late results, a
     deep start overlap);
   - snap-back hysteresis;
   - the reach clamp;
   - the IK feed (drawn palm within 1 cm of the commanded hand inside reach);
   - mass and time-scaled pushes;
   - calibration bounds and persistence;
   - the carry layer re-apply.
2. The Windows build passes (user).
3. Headset checklist (user):
   - press a palm onto a table and a wall;
   - slide along a wall;
   - swat a bottle and a wall;
   - push a crate (it resists) and a bottle (it moves);
   - touch an awake guard (the hand stops, nothing else happens) and push a knocked-out guard;
   - reach through a wall and pull back (clean snap-back);
   - calibrate, then confirm the hand sits in the controller;
   - carry an oil tank for 60 s standing and walking (no image breaks; diagnostics captured);
   - frame time within budget.
