# VR v1039 (test candidate, 2026-11-06)

Fixes for the five bugs reported on v1037. (v1038 was an intermediate build of the same work; v1039 supersedes it.)

## 1. A flat "default" wheel for a second after a wheel choice

After the commit the game keeps its wheel logic (256) while its wheel closes. Two paths could put the native wheel on screen then:

- the hand ring ends 60 ms after the button is released, and with no ring the layer code fell back to the head-locked native wheel quad
  (`game_xr.hpp` world UI layer);
- the wheel movie's own opening/closing frames drawn outside logic 256 were rejected by `begin_world_ui` and drawn flat into the eyes.

Fix: `world_ui_quad_allowed` (`src/world_ui_pose.hpp`) never shows the native wheel quad in hand-wheel mode once its hand is gone, and
`world_ui_capture_allowed` (`src/native_world_ui.hpp`) captures the wheel movie off-screen under any logic (it is still only *shown* under
logic 256; notes, journal, dialogue and the black market keep their old rule).

## 2 + 5. The previous weapon in the hand after a wheel choice; two weapons in one hand

The wheel route decided "the wheel chose what slot 1 already holds" when the engine's equip had not started 160 ms after the commit and gave
the opening hand a copy of the **previous** item. The user's journal: the engine's equip starts 78-2172 ms after a commit (slot-1 routes
median 234 ms), so a slow equip got the old item, and the real equip then landed with no route.

- `SameItemGuess` (`src/hand_loadout.hpp`): the guess stays (a real two-pistols pick is still instant) but is withdrawn when the slot-1
  equip changes within 2.5 s; the route then runs as usual and the new item goes to the hand that chose it. Journal: `wheel_guess_withdrawn`.
- `HandLoadout::settle`: a hand holds one thing. A native item that arrives in a hand holding a mod-owned copy displaces the copy (it falls
  where the hand is, as displaced copies already did). Journal: `copy_vs_native`.

## 3. Hands disabled with a body on the shoulder

The game's `CarryCorpse` (player state 45) locks and empties the LEFT hand's equipment on entry (EquipChange 64, code 0x21/0x81,
requester 45), though its decl only takes the right arm (body part 0x04, as Slash/Block); UsePower, FireWeapon and Blink are allowed by
the transition matrix. The mod also hid the left slot's item during any body carry.

- `carry_left_equip_code` (`src/shoulder_carry.hpp`) + `NativeManualEquipRuntime::keep_left_hand_in_carry`: the lock bit goes; a left
  slot that holds an item keeps it (0x24, draw the selected = the held item, instant); an empty hand stays empty (only the lock goes, so the
  carry's exit has no restore item to draw).
- The mod keeps the left slot's gun, gadget, power and aim tracking during a body carry; the carrying hand's fingers stay the player's.
- Knob `build/carry-locks-hands`: the game's lock as before. Status counter `carry_left_kept`.
- **Not changed:** the sword. The carry holds the right arm's body part, so the game refuses Slash/Block while a body is carried; take the
  body off (the existing gesture) to use the sword.

## 4. The resolution slider stops XR or crashes

Every slider step was applied at once: the game output resize, both XR eye swapchains and FSR/TAA resources. Any exception on that path
(`CachedOutputSize::update` racing the engine's two-store size write; the 5 s "did not reach the headset size" guard while the menu is
open) went to `recover_xr_session`: XR stopped and restarted.

- `ResolutionDebounce` (`src/resolution_debounce.hpp`): a drag applies once, its last value, 0.7 s after the slider stops.
- A failing size update is retried (250 ms, at most 8 times) instead of thrown into the session restart.
- After a slider change the 5 s guard asks the game again instead of restarting XR (it still guards the session start).

## Evidence

- Offline: `hand_loadout_check`, `native_world_ui_check`, `resolution_debounce_check` (new, in `tools\build-msvc.cmd`),
  `shoulder_carry_check`, `shoulder_carry_routing_check.py`, `world_ui_pose_check`, `wheel_eye_art_check`, `wheel_avatar_check`,
  `vr_settings_native_check`, `render_size_check`, `grip_decision_check`, `held_release_check`, `native_carry_snapshot_check`: PASS.
  `native_manual_equip_runtime_check` fails at line 519 on v1037 as well (pre-existing).
- DLL: `build\compile-v1039.cmd` (MSVC 2022 x64 /W4), dll_exit=0, warnings unchanged; 14,934,528 bytes, sha256
  `b3ca92691c0deb1497b616ba67caf50ee69b2f50715eb148c740caa7314777ff`.
- Simulated VR on the user's save (saves byte-identical afterwards):
  - Wheel, three hand-wheel opens and commits: ring at the hand every time; no native wheel quad; every wheel-movie draw captured (v1038,
    v1039 at headset size). v1037 showed no quad either: in the sim the logic leaves 256 ~150 ms after the commit, so the headset's
    one-second close is not reproduced there.
  - Resolution slider, v1037 at the sim's small size: the first step froze the mod's frame work ~37 s and restarted the XR session twice
    ("Invalid cached output-size update", a throw into `recover_xr_session`).
  - v1039 at headset size, 6 steps up and 6 down: 12 clean swapchain re-creations (2688 -> 3823x4096 capped -> 3226), no XR restart,
    no recoverable fault, every press handled on time. v1037 at headset size with the same slow steps also survived; a fast-drag A/B was
    inconclusive.

## Not verified

- No headset run. The wheel flash (the sim cannot reproduce it), the shoulder-carry left hand (no corpse sim), the slow-equip wheel route
  (the sim save's wheel never equips) and the debounce under a real drag need the headset.

## What to check

1. Pick weapons from both hand wheels: no flat wheel after the choice; the hand gets the item you picked, also when it comes out slowly.
2. Two pistols (pick the gun the other hand holds): still works, at once.
3. Put a body on the shoulder: cast a power, fire the pistol, pick from the wheel with the gun/power hand. Put it down: nothing doubles or
   disappears in that hand. The sword stays blocked while the body is carried.
4. Drag the Resolution slider up and down in VR Settings: XR stays up; the image settles at the new resolution ~1 s after you stop.
