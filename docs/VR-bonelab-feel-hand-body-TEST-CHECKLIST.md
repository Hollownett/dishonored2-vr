# Bonelab feel Part 5B: physical hand bodies, headset checklist (Tier C)

Status: not run. The sim proved logic and crash safety only (no crash in about 818 s of driven sessions, the player never
moved, drive cost within budget). Feel, comfort, and anything that needs a level load or a save/load can only be judged here.
The hand body stays off by default until this list passes.

## Turn it on

- Launcher: Features, switch on "Physical hand bodies (experimental, headset test)". Or create an empty file `build/hand-body`.
- Hard off switch: an empty file `build/no-hand-body` always wins and turns it off. Use it for item 12.
- Leave the other hand features at their defaults: hand reach clamp and weapon collision must stay on (the body is never
  created without them). Leave the carried-object clamp (`held-clamp`) on too.
- Note the DLL hash. Dev knobs `hand-body-spike` and `hand-body-probe` must NOT exist in `build/`.
- Stand where you can test: a table, a wall, a crate, a whale-oil tank, a long plank, a corpse (a knocked-out or dead
  guard), and later one awake guard.

## What to save after each item

Copy `xr-live-status.json` and keep these blocks:

- `hand_body`: `available`, `faults`, `abandons`, `alive_outside_gameplay`, `snaps`, `yields`, `cost_us`, `cost_max_us`,
  `filter_readback_mismatches`.
- `hand_visual`: how often the body or the clamp pose was drawn, blends.
- `hold_coupling`: coupled holds, lag releases, rebases.
- `held_clamp` and `hand_clamp`: sweeps, hits, `clamped_frames`, `release_overlap_unresolved`.
- `carry`.

Each item below says which of these to watch. A crash, a freeze, or the player being moved by your hand is an automatic fail.

## Items, highest risk first

### A. World changes with the hands alive (watch `abandons`, `faults`, crashes)

1. **Save, then load.** With the hands out in the air, quicksave, then quickload. Repeat once via the pause menu: Load.
   Pass: no crash, hands work after the load, `faults` 0. Save `hand_body` (`abandons`, `alive_outside_gameplay`).
2. **Level load.** Walk through a loading transition (door or area change) with your hands up. Pass: no crash, hands
   work afterwards, `faults` 0. Save `hand_body`.
3. **Die and reload.** Get killed (jump from a height, or a guard) with your hands near a prop or wall, then reload.
   Pass: no crash, `faults` 0. Also note whether every death plays the death camera: a death with none is a known
   path that counts an `abandon`; one abandon is acceptable if the game then behaves, a crash is not. Save `hand_body`.
4. **Keyboard Esc, mission end, menus the game opens itself.** Press Esc (keyboard) with the hands in contact with a
   table. Finish or fail a mission to the end screen if you can. Open the settings panel and the hold-Y menu. Pass: no
   crash and `faults` 0. `abandons` / `alive_outside_gameplay` may count these; the number is what we want back. Also
   note Start feels a little slower to open the menu (up to 0.3 s by design): is that annoying?

### B. First real carries (watch `filter_readback_mismatches`)

5. **First corpse carry.** Pick up and carry a corpse (hold it, walk, drop it). This is the first real filter write on a
   hand body in the engine. Pass: `filter_readback_mismatches` stays 0, `faults` 0, and your hand is not stuck
   without collision afterwards (touch a wall: it still stops). If the count rises, the body falls back to the old
   hand for the session: record it. Save `hand_body` and `hold_coupling`.
6. **Pull a corpse into a wall.** Pull the carried corpse into a wall. Pass: it stops, your hand lags a little, nothing flies.
   If you jam it for a moment it lets go cleanly instead of fighting you. While carrying, your hand passes through other
   ragdolls (by design): note if that feels wrong. Save `hold_coupling`, `carry`.
7. **Oil tank: wall, floor, drop.** Carry the tank into a wall and into the floor, drop it from chest height, and drop it
   while pressed to the floor. Pass: it stops at the surface, ends on the floor, no ejection, no jitter; the drawn hand
   stays within about 0.35 m of your real hand. Fast arm swings in free air must not make the tank let go on its own.
   `release_overlap_unresolved` 0, `filter_readback_mismatches` 0. Save `held_clamp`, `hold_coupling`, `hand_body`.
   (This also covers the held-clamp checklist items 1 to 3.)
8. **Carry a plank through a doorway.** Carry the plank through a door frame, turning your wrist. Pass: it scrapes or
   stops, never passes through, never throws you. Carrying a crate or rigid prop, your hand passes through other crates
   (by design): note if that feels odd. Save `held_clamp`, `hold_coupling`.

### C. Bare hands against the world

9. **Press into a table, wall and crate.** Push your hands slowly into each, then sweep along them. Pass: the visible
   hand stops at the surface and the arm follows, no jitter or shaking; it does not tunnel. Then wave both hands hard in
   free air (a fast sweep, about a 6 m/s swing): the visible hand must stay close to your real hand, not trail behind.
   Save `hand_visual` (check it is drawing the clamp pose in free air), `hand_body`, `hand_clamp`.
10. **Push your own body.** Press a hand hard against a wall, the floor and a heavy crate while standing still, then
    while leaning. Pass: you never move and never get pushed or lifted. Save `hand_body`.
11. **Awake guard.** Touch an awake guard with a fast hand, a slow hand and a held prop. Pass: nothing worse than it was
    without the body (it may notice you; it must not be launched, glitch or crash the game). Watch vision, melee, alert and
    health. Save `hand_body`.

### D. Feel and fallback

12. **Odd cases.** Blink while holding something, and Blink or teleport with the hands pressed into a wall (the hand may
    snap back, it must not fire off). Wear the hand into an authored animation (a ledge climb, a takedown) and a hold.
    Bend Time, then swing hands: nothing drifts or floats. Pass: no jump, no spinning, no crash. Save `hand_body`
    (`snaps`, `yields`).
13. **Ten minutes of ordinary play.** Fight, carry, open doors, loot. Pass: no crash, no nausea, no sluggish feel, hands feel
    physical but not floaty; `faults` 0, `cost_us` stays about 0.2 ms or less at the headset frame rate (note `cost_max_us`;
    a rare spike near 0.27 ms is already known). Note any hand lag you can feel and any case where the hand and a held
    object disagree. Save all blocks.
14. **Fallback still works.** Create `build/no-hand-body` (leave the hand-body row on) and restart. Pass: `hand_body` is
    off, nothing physical happens to the hands, the tank still stops at walls and floors through the carried-object clamp.
    Delete the file afterwards.

## Pass

Items 1 to 5 are the gate for turning the default on: all must pass with `faults` 0, no crash, and
`filter_readback_mismatches` 0. Items 6 to 14 are feel findings. Anything off is a finding, not a skip. Keep the saved
blocks and send them back with a note for each item: pass, fail or unsure.
