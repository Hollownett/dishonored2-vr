# Independent per-arm weapon state (dual wield) — design

2026-11-06. Requested by the user after the v1037/v1039 headset tests; design mapped from the v1039 worktree.

## Intent

The user: decouple weapon selection between the arms. Each arm has its own state of what it holds now, not bound to the
other arm, except one thing: the **power swap** (when a weapon goes into the arm that holds a power, the power moves to the
other arm — today's behaviour). Powers may be in either hand. The coupled approach keeps producing wrong-arm selection,
items in the wrong hand, two weapons in one hand and weapons vanishing from the other hand; patching it never ends.
Hard constraint: CPU-cheap, strict stable 90 fps (11.1 ms frames).

Goals: every action (wheel pick, holster draw/put-away, pickup, catch, drop/throw, sword take) changes only the acting arm;
the other arm keeps exactly its item, visually and functionally; the power swap is the single cross-arm rule.
Non-goals: how wheels open (X left, A right), the power-swap rule itself, the shoulder carry, the copy/fire machinery.
Anti-goals: any per-frame cost beyond a few integer compares; a second source of truth for "what a hand holds".

## Why the current code couples the arms

`HandLoadout::melee` (one bit, `ranged()==melee^1`) decides at once where the sword is, where the slot-1 item is and which
side is mirrored. Taking the sword, drawing from the hip, a wheel commit, a native pickup or a gadget catch on arm h flips
it, which re-homes the other arm's item, resets both grip machines (puts the other arm's sword away / holsters its gun),
erases copies, and makes `wheel_second`/same-item code guess what the other arm held. The engine does not need this:
the arm-joint cross has two independent bits (`native_attachment_pose.hpp` ~451, `weapon_draw_place.hpp` ~290):
bit 0 = the slot-1 item rides the right arm, bit 1 = the sword rides the left arm.

## Model (new pure header `src/arm_state.hpp`)

- `Held {none, sword, pistol, crossbow, grenade, mine, razor, heart, power, guard_sword, guard_pistol}`,
  `Home {none, slot0, slot1, copy}`, `Arm{what, home}` per physical arm, `Pending` per arm (an outstanding native request
  with a ticket, deadline, revision), sticky `rest_slot1` / `rest_sword` arms.
- Invariants (one `place()`): at most one arm on slot0 and one on slot1; slot1 = pistol, crossbow, gadgets, heart, power;
  copies only for pistol, crossbow, guard weapons (a power, the Heart or a gadget is never a copy); one thing per arm,
  newest placement wins and the older item leaves only that arm.
- `derive()` each frame: `slot1_arm` (pending claimant, else the slot-1 holder, else `rest_slot1`), `sword_arm` (slot0
  holder, else `rest_sword`, kept off `slot1_arm`), `cross = (slot1_arm==1) | (sword_arm==0)<<1`, `copy_of[h]`.
  Replaces `melee`, `ranged()`, `mirrored()`, `hand_bits()`; published to the render thread as one atomic `arm_pack`
  (replacing `melee_hand` / `extra_kinds`). Knob `build/dual-wield` off = the game's own hands (cross 0).
- Events touch only their arm: `wheel_commit(h, chosen)`, `hip_draw(h)` / `hip_holster(h)`, `sword_take(h)` /
  `sword_away(h)`, `pickup(h, kind)` (native only when slot 1 is unowned, else a copy; a gun taken while the other arm
  holds a power/gadget is a copy — no longer overwriting it), `catch_gadget(h, kind)`, `release(h)`, and
  `native_changed(slot1 kind, sword drawn, revision)` as the only way engine truth enters (async equips land
  78-2172 ms after a request; each mod request carries a ticket naming its arm; engine-initiated changes go to the
  holder or the sticky arm; a displaced native gun in the other arm becomes its copy using that arm's recorded kind).
- **Same item from the wheel (decided):** exact, not guessed. The mod already hooks the wheel's equip function 0x7fb350
  (`NativeManualEquipRuntime::wheel_before`, args controller, chosen item, ammo); the game skips the equip when the chosen
  item is already in the left hand. Record the chosen item per commit and compare it with the current slot-1 item; same
  item -> the opening arm gets a copy at once (two pistols); different -> wait for the ticketed arrival. The
  160 ms guess, `SameItemGuess`, `wheel_second`, `wheel_prev_*` go away. Fallback only if the hook data is missing.
- **Power swap (the one cross-arm rule):** in `place(h, X)` when arm h holds a slot-1-only item P (power, Heart, gadget)
  and X is a weapon: if the other arm is empty P moves there; if it is occupied P is put away (today's `power_other_free`).
- Grip machines bind to `sword_arm` / `slot1_arm` and reset only on their own arm's events (no reset from the other arm).
- Cutscenes/authored clips: unchanged render rule (cross 0, copies hidden); `ArmSet` untouched.
- Cost: one `native_changed()`, one `derive()`, one atomic store per frame; no allocation, no new engine reads (slot data
  already sampled); cache the copy def per arm instead of interning names every frame.

## Migration (each step builds and has an offline check)

1. `src/arm_state.hpp` + `tests/arm_state_check.cpp`: invariants, power swap from wheel/pickup/sword take, async arrival
   matrix, exact same-item, engine ammo-out/unequip, property test (random events never change the other arm's `what`
   except under the power swap); add to `tools/build-msvc.cmd`.
2. (Optional) shadow mode journalling disagreements with the old state.
3. Publish `arm_pack`; switch the render readers of `melee_hand` / `extra_kinds` / `hand_loadout_bits`.
4. Move the events: wheel, pickup/catch, sword/hip takes, release; `settle()`/`copy_vs_native` become the fall branch.
5. Bind the grip machines to the plan; replace blanket `sword_grip={}; gun_grip={}` with per-arm resets.
6. Role readers (`rh`/`mh`: aim/trigger routing, grip intents, choke hand, NPC hold) read the arm of the thing they need.
7. Delete `HandLoadout::melee/extra/update/wheel_second/pickup/drop/power_hand_over/settle/hand_bits`, `SameItemGuess`,
   `melee_hand`, `extra_kinds`, `extra_prev_shown`, `native_pickup_until`; port `tests/hand_loadout_check.cpp` cases.

Journal events keep their names (`wheel_commit`, `wheel_route`, `wheel_lost`, `pickup`, `gun_drop`, ...). Existing knobs stay.

## Risks / what only the headset can confirm

Copy appearance on the frame a native item leaves (pre-warm the copy def), native items riding each arm independently
when the two cross bits differ (`mirror_native` / clip choice per arm), engine sword redraws with the sticky arm occupied,
pending-request expiry, the power swap from all three triggers, and frame cost (expected negligible).
