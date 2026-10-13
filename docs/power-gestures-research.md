# Power gestures for Dishonored 2 VR

Nothing here was checked in the headset, and nothing is implemented. Today the left trigger
(`POWER_WEAPON` / LT) casts the equipped left-hand power and the left hand aims it; selection
stays on the hold-X wheel. This note proposes gesture casting as an optional layer on top of
that path, with LT kept as the fallback for every power.

## 1. What the references actually do

**Dishonored 1 VR ([VR-Stereo-Hub/Dishonored-VR](https://github.com/VR-Stereo-Hub/Dishonored-VR)).**
Powers, crossbow and pistol are aimed along the hand ray, and LT casts. The motion sword is a
hand-speed **edge detector** that pulses RT, with the speed threshold adjustable in the F10
panel and the log counting **near misses** so the threshold can be tuned to the player's arm.
The stealth kill and choke are gesture-only. The wheel is a world dial: hold the left grip,
move the hand to a wedge, release. This is the closest published reference — it proves
hand-aimed powers work, and it shows the tuning loop our recogniser should copy.

**Blade & Sorcery ([casting explained](https://steamcommunity.com/app/629730/discussions/0/2298472007787762898/)).**
Spells are equipped per hand, **charged by holding the trigger**, then delivered with a motion
plus release: a tossing motion for fire, a thrust for gravity, a point for lightning. So B&S
separates slow, unambiguous *charge* from fast, noisy *delivery* — the split that makes a
gesture reliable.

**The Wizards / Waltz of the Wizard.** Carbon Studio's stated rule is "gesture → magic → world
reaction"; *"Magic cannot be a button"* ([Carbon Studio](https://carbonstudio.pl/en/2026/02/04/how-the-wizards-was-created-carbon-studio-vr-game/)).
Gestures are learned in one move with no tutorial and lean on pop-culture shapes (point, push,
pull). Waltz of the Wizard added hand tracking without controllers
([UploadVR](https://www.uploadvr.com/waltz-of-the-wizard-psvr2-natural-magic/)). A
[CTU bachelor thesis](https://dspace.cvut.cz/entities/publication/a79a8470-092c-480a-aa22-4da8f9333e88/full)
builds three VR spells after analysing The Wizards, Waltz of the Wizard and War of Wizards — a
useful survey of shapes players already know.

**Half-Life: Alyx gravity gloves ([thread](https://steamcommunity.com/app/546560/discussions/0/3770110614214032466/), [overview](https://en.wikipedia.org/wiki/Half-Life:_Alyx)).**
Point to highlight, trigger to select, then **squeeze and flick the wrist toward yourself** to
pull the object in, then catch it. The thread shows the cost: the flick is "very hard to get
the hang of at first" and a small or slow motion fails silently. A flick suits a *committing,
reversible* action with visible feedback and generous tolerance, and needs a fallback.

**Skyrim VR.** VRIK and Spell Wheel VR show the two reliable answers in that ecosystem —
pose-driven hand/body states, and a hand-centred radial selector — rather than gesture
recognition. (Both Nexus pages block fetching, so I could not read their descriptions;
[Spell Wheel VR](https://www.nexusmods.com/skyrimspecialedition/mods/47630).)

**Locomotion, comfort, recognisers.** [Natural Locomotion](https://store.steampowered.com/app/798810/Natural_Locomotion/)
maps arm swing 1:1 onto stick output and reports reduced sickness because the arm motion
predicts the view motion. [Point & Teleport](https://dl.acm.org/doi/10.1145/2967934.2968105)
(Bozgeyikli et al., CHI PLAY 2016) measured teleport as the comfortable option; [Until You
Fall's GDC talk](https://gdcvault.com/play/1026868/Until-You-Fall-Building-Satisfying) is about
making VR combat feel good cheaply. $1 ([UIST 2007](https://dl.acm.org/doi/10.1145/1294211.1294238))
and $P ([ICMI 2012](https://dl.acm.org/doi/10.1145/2388676.2388732)) match a sampled path to
templates and return a similarity score: right for a *drawn shape*, wrong for our first pass,
because the score threshold is hard to justify, it needs per-user templates, and it cannot
express "hand at the temple for 300 ms". Hellsweeper VR and Boneworks/Bonelab are gesture-heavy
but I found no primary design documentation; treat claims about them as **unverified**.

## 2. Casting model

Only one power is equipped on the left hand at a time. Two ways to spend that:

1. **Gesture = cast (recommended first).** The gesture is a second trigger for the *equipped*
   power; the wheel still selects it. A misread can then only miss or cast the thing already
   equipped, never the wrong power.
2. **Gesture = select + cast.** The gesture must also equip its power, most plausibly by
   pulsing that power's D-pad shortcut (`QUICK_0..3`) and then LT. That path depends on native
   state-machine timing (wheel lag, which shortcuts are assigned) we have not measured.

Build for (1); treat (2) as an opt-in that reuses the same events.

## 3. Per-power gestures (left hand unless stated)

| Power | Gesture | Preconditions and motion signature | Fallback |
|---|---|---|---|
| Blink / Far Reach | Point, then an inward "yoink" flick | LT already held, marker valid ≥150 ms, no grab target, hand still (<0.35 m/s); then hand speed ≥0.9 m/s toward the chest over ≤250 ms, direction within ~45° of −aim. Commit on the flick edge, release LT. | Hold LT to aim, release to cast (native) |
| Dark Vision | Left palm to left temple | Palm within 12 cm of a temple anchor, palm normal inward ±35°, speed <0.3 m/s, dwell 300 ms; toggles | LT tap |
| Mesmerize | Palm-forward push, sustained | Open palm facing the target ±35°, aim-ray hit within 8 m; push ≥1.0 m/s over ≥15 cm; sustain while held | LT |
| Domino | Palm-forward push, then retract and push again | Same envelope as Mesmerize, but fires on a re-push edge so the two do not alias | LT |
| Bend Time | Clench both fists, both still | Both grips ≥0.7 within 120 ms of each other, both hands <0.3 m/s, sword sheathed, no grab target; toggles | LT |
| Possession | Point and close the left fist on the target | Aim-ray target within 3 m, no physical-grab target in reach, grip rises <0.35 → ≥0.7; hold to stay | LT |
| Devouring Swarm | Hand at the chest, palm down, push out | Hand within 20 cm of a sternum anchor, then forward push ≥1.1 m/s within 300 ms | LT |
| Shadow Walk | Palm-down drag from shoulder to hip | Palm down ±30°, hand travels ≥35 cm downward over ≤900 ms near the torso; sustain while held | LT |
| Doppelganger | Both palms forward, then apart | Both hands open and facing forward, then separation ≥0.8 m/s for ≥25 cm | LT |
| Windblast | Strong one-hand push | As Mesmerize with a higher threshold (≥1.6 m/s) | LT |
| Electric Charge | Both hands converge to a clap | Hands approach to ≤8 cm at ≥0.8 m/s; fire on closest approach. *(The name is the user's; I could not corroborate a D2 power called "Electric Charge".)* | LT |

Haptics: a 40–80 ms pulse on recognition, a ramped pulse during a charge gesture, and nothing
for a rejected near-miss.

## 4. Minimal recogniser (pure policy)

Mirror `src/play_controls.hpp`: header-only classes with `update(..., now) -> bool`. Inputs
already exist: per-side hand pose and velocity, `grip_speeds` (m/s from `XR_SPACE_VELOCITY`,
with the simulator's finite-difference fallback), grip/trigger curls, the aim ray, the head
pose, `target_valid`, and an `allowed` flag from the caller. Three primitives cover every row
above:

- **`SpeedFlick`** — scalar speed along an axis (aim axis, downward, or hand-to-anchor
  direction). Fire on a high threshold; stay spent until speed drops below a low re-arm
  threshold, copying `CrouchFlick`'s hysteresis and one-pulse-per-motion latch, so a motion
  that began while disallowed is spent and can never fire late.
- **`PoseHold`** — anchor proximity + orientation cone + stillness + dwell; latch on fire
  until the hand leaves the anchor.
- **`ConvergeDiverge`** — hand-to-hand distance and its derivative crossing a threshold.

Composition, one dispatcher keyed by the equipped power id: mutual exclusion with a live LT
hold, a per-gesture cooldown (400–1000 ms), a global cast cooldown (≥350 ms), cancel-on-focus-
loss like `menu_chord.hpp`, and one-shot latching so a held gesture cannot re-fire. Resolve
within ~120 ms of motion end, off the per-frame XR pose; no history buffers beyond the
250–900 ms windows above. $1/$P stays out of phases 1–2.

## 5. False positives, risks

- **Grip gestures collide with grab**, which the left grip already means. Gate every power
  gesture on "no grab target in reach", as `GripTriggerGuard` does, and never while holding
  an object.
- **Hand-to-face happens** while wiping a brow, adjusting the headset, or using the spyglass.
  Keep the temple gesture on the left hand and the existing spyglass gesture on the right.
- **Tracking loss manufactures velocity.** Use runtime velocity only when the linear-velocity
  bit is valid, clamp to a sane maximum, and suspend the recogniser briefly after a validity
  gap.
- **Melee swamps any threshold.** Require the sword sheathed or the right hand idle for
  two-hand gestures.
- **Comfort.** Blink is an instant translation, and a flick can cue the body to expect a
  lurch. Teleport itself measured comfortable; the real risk is a misfire mid-combat, so the
  Blink gesture must be committing and visible (marker + haptic) before the release.
- **Accessibility and seated play.** Per `docs/vr-conversion-best-practices.md`, every gesture
  keeps its button fallback; default all of this off until tested.
- **Ergonomics.** Keep amplitudes at 10–35 cm and never require a raised arm.

## 6. Phased plan

- **Phase 0 — instrument.** Log left-hand speed, anchor distances and near misses during
  normal play, as D1 VR does; no behaviour change. Add pure-policy unit tests
  (`tests/play_controls_check.cpp` style) for the three primitives.
- **Phase 1 — Blink + Dark Vision.** Blink: flick-to-commit on the existing LT aim. Dark
  Vision: left-temple dwell toggle. Both `allowed`-gated, cooldowned, haptic, with F10
  toggles; LT untouched. Headset test for misfires, then tune against the logged near misses.
- **Phase 2 — generic cast + Possession.** One palm-push that casts whatever is equipped, plus
  grab-to-possess. Measure whether gesture → shortcut → LT selection is stable enough for (2).
- **Phase 3 — the rest.** Mesmerize, Domino, Bend Time, Shadow Walk, Doppelganger, Devouring
  Swarm, Windblast, Electric Charge — each opt-in, each dispatched only when that power is
  equipped.
- **Phase 4 — optional shapes.** A $1/$P matcher for a drawn Domino sigil, behind a setting,
  never the only path.
