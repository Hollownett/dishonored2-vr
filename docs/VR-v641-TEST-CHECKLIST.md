# v641 test build: no game carry from the torso, hands never stay stuck, stealth kills and chokes from behind, sphere grip

Headset status: not run. Built on v638. User feedback on v638 (headset, 2026-10-27): limbs, head and legs pick up correctly; four problems remain.

## 1. A torso grab still went onto the game's shoulder (fixed)

The game's held Use on a dead or unconscious body is its shoulder carry. A grip whose decision fell back to Use (an offered takedown nearby, a late native target) or the gaze look-up could still send that Use. The v638 session logged about 690 frames of a native carry.

Now no Use reaches the game while any of its selections (either hand's or the head's) is a dead or unconscious body (`NativeInteractionAim::use_owners`, `body_carry_prompt`). A native carry already running keeps its Use (the drop). Status: `sword_grip.body_use_blocked`. Knob: `build/legacy-corpse-carry`.

## 2. A hand stopped following the controller after a table or a corpse (fixed)

- The visible hand's clamp waited for a palm sweep to validate the controller target with no time limit (strict mode). A sweep that never answered, or one that started inside a loose body (a corpse limb), froze the drawn hand in mid-air. Now the hold ends after 0.25 s and the hand follows the controller, still kept out of known walls. Reproduced and fixed offline in `hand_clamp_check` (`strict_hold_is_bounded`).
- The physical hand body could stay wedged away from a blocked target forever (the snap needs a free palm; the v638 log has 1500 blocked snaps and 2 snaps). A body wedged for 1 s is now released and re-created when the palm is free. Status: `hand_body.sides[].drive.stuck_releases`, event `stuck_release` in `hand-body-events.csv`.
- Knob for both: `build/legacy-hand-stuck`.

## 3. Stealth kills and chokes (fixed)

- **Kills.** v638 only pressed the game's attack and let the engine decide: in the v638 session 5 of 19 presses became the Assassination. Now a blade contact (stab or slash, from 0.6 m/s) from behind or the side on an NPC the engine itself calls unaware (`isUnaware` 0x5bf560) is the stealth kill. When the engine's kill sensor offers that NPC, its own Assassination gets 700 ms (the native press); otherwise the kill lands on the same game-thread pass, before a swing's ordinary native slash can alert him. A noticed guard still gets the ordinary hit; a slow touch on a noticed guard does nothing. A strong pulse on the sword hand confirms the kill. Status: `sword_contact.stealth` (held, native took it, kill, noticed, dropped), `sword_grip.stealth_kill_pulses`. Knob: `build/legacy-sword-contact` (the old contact rules).
- **Chokes.** Only the right grip could choke, and with the sword drawn the right fist holds the sword (its grip never makes a new press). Now the choke is the free hand's grip: the left hand while the sword is drawn (unless it holds a gun). The sword-in-front Block holds RB, which swallowed the choke's press: the choke now lets RB go for 45 ms first, so the game sees a fresh press.

## 4. Sphere grip for physical props and body parts (new)

The fingers wrap one invisible ball fixed in the palm, decided when the hold starts, instead of the held object's own lagging shape:

- Props (books, folders, candlesticks, spoons, forks): radius = geometric mean of the two smaller half sides, 1.5 to 6 cm (spoon/fork 1.5, candlestick 2.2, book 4.0, folder 3.3, skull 6). The prop's centre is pulled into the ball (at most 0.25 m), keeping its orientation.
- Dragged corpse limbs: 0.6 x the ragdoll capsule radius (a wrist 3 cm); the body is never moved into the palm.
- Severed limbs and body halves (the game's HoldObject): the same ball.
- Bottles, glasses and the other native carryables keep their existing grip.
- Knob: `build/legacy-finger-grab`.

Lab (real Corvo rig, real solver): after settling, the largest per-frame finger joint change is 0 deg on the ball; the old path moved 25 to 73 deg per frame with the motor lag recorded in the v638 session. Renders: `artifacts/hand-lab/sphere-v2/*_sphere-views.png`, `snap866-old` / `snap866-sphere`.

## Headset checks

1. Grab a body by the chest, collar and belly with grip alone, many times, also next to an awake guard: the game's shoulder carry never starts. Limbs and head drag as in v638.
2. Push a hand into a table top and pull it out quickly; grab a corpse limb, push the other hand into the body, let go: the hand always comes back to your controller within a fraction of a second.
3. Sneak behind a calm guard. Stab him in the back, and on another guard slash from the side: each one dies at once (strong pulse). A guard who has seen you takes an ordinary hit.
4. Sword drawn and held in front, sneak behind a calm guard, grab his neck with the LEFT hand: the choke starts. Also with no sword, with the right hand.
5. Pick up a book, a folder, a candlestick and a spoon; drag a corpse by the wrist; pick up a severed head: the fingers close round the object and stay still while you move.
