# VR v1040 (test candidate, 2026-11-06)

Three requests from the user on 2026-11-06: independent arms, frame time, and the spyglass as a scope.
Design: `docs/superpowers/specs/2026-11-06-independent-arm-state-design.md`. Research: `reports/Dishonored VR 90 fps performance.md`.

## 1. Each arm has its own state (dual wield)

`HandLoadout::melee` (one bit: sword arm, slot-1 arm and mirroring at once) is gone with `src/hand_loadout.hpp`. New
`src/arm_state.hpp`: each physical arm records what it holds (sword, pistol, crossbow, gadget, Heart, power, guard weapon) and
where it lives (native sword slot, native slot 1, or a mod copy). One `place()` keeps the invariants (one thing per arm, at most
one arm per native slot, newest placement wins in its own arm); a pure `derive()` gives the sword arm, the slot-1 arm, the two
cross bits and the copies each frame, published to the render thread as one atomic `arm_pack`.

- Every action changes only its own arm: wheel pick, hip draw, sword take, pickup, catch, drop/throw. The only cross-arm rule is
  the **power swap**: a weapon placed in the arm holding a power/Heart/gadget moves it to the other arm if that arm is empty,
  else puts it away.
- Engine truth (equips land 78-2172 ms after a request, ammo out, engine unequips) enters only through `native_changed()`;
  mod requests carry a ticket naming their arm.
- **Same item from the wheel is exact:** the wheel's equip call (0x7fb350, already hooked) reports the chosen item; it is compared
  with the slot-1 item's declaration (inventory record word 0, as the gadget code reads it). The 160 ms guess, `SameItemGuess`,
  `wheel_second`, `settle`, `power_hand_over`, `native_pickup_until` are gone. Fallback when the hook gives nothing: 700 ms.
- Grip machines reset only when their own arm's role moves (no blanket `sword_grip={}; gun_grip={}`).
- A gun picked up while the other arm holds a power/gadget is a copy (it no longer overwrites the power).
- Journal names kept (`wheel_commit`, `wheel_route`, `wheel_lost`, `pickup`, `gadget_catch`, `loadout_swap`, `copy_vs_native`,
  `slot1_handover`); new `wheel_choice`, `arm_arrival`, `slot1_restore`, `slot1_put_away`.
- Check: `tests/arm_state_check.cpp` (invariants, power swap from wheel/pickup/sword take, async arrival and expiry, exact same
  item, ammo out, single occupancy, a seeded random-event property test: no event changes the other arm's item except the power
  swap). Replaces `hand_loadout_check` in `tools\build-msvc.cmd`.

## 2. Frame time

The research found the latest slowdown is mostly GPU (a 1.25 render scale and FSR 4: 5.5 ms at 3120x3360), plus measurement code
left on in play. This build removes the mod-side overhead (the image is unchanged):

- **Status write off the render thread:** `xr-live-status.json`, `xr-lifecycle.csv` and the 5-second summaries are built as text
  on the Present thread and written by one background thread (`src/latest_file_writer.hpp`, latest-wins per file, joined at
  stop). The old code opened and wrote 2-5 files on the Present thread every second. New status field
  `status_write_us {last, max, mean, writes}` measures what is left (the text building).
- **Measurement counters off in play:** the v990-v998 draw/state census hooks (draws by phase, render-target bind census, PS
  state census, PS constant-buffer hook, context barriers) are installed only with `build/draw-census` (or the state-elision /
  wide-census knobs that need them). Status keeps its keys with zeros and `"draw_census":false`.
- **Object motion early-out:** draws whose layout has no previous rows (82.6% of draws in the user's session) return before the
  view lookup, pass callback and row gather; weapon/arm motion vectors are unchanged.
- **Glyph swap / sampler LOD bias:** no work in their hooks when their features are off.
- **GPU timing:** the user's install had `build\native-gpu-timing` (about 224 GPU timestamp queries per frame); it was renamed to
  `native-gpu-timing.off` in `%LOCALAPPDATA%\DishonoredVR\build` (reversible).

## 3. Spyglass = a right-eye scope (user's choice)

R3 now works like Corvo's mask telescope: while zoomed, only the **right eye** renders with its frustum narrowed by the game's own
zoom factor (0.4375, 0.1875 with the upgrade, including the fades), so the zoomed image is a real render, not a magnified crop.
A darkening pass (`SpyglassScopeMask` in `game_xr.hpp`) leaves a circle (half the eye's smaller field, clamped 40-56 deg) with a
soft dark rim, drawn before the HUD and watch. The left eye is unchanged. The zoom's own mask quad and lens distortion are removed
from both views. TAA/FSR histories reset at entry and exit (`build/spyglass-fade-reset` also resets during the 0.2 s fades).
`src/spyglass_scope.hpp` + `tests/spyglass_scope_check.cpp` (projection maths, size clamps, rim, canted axis, state machine,
WARP mask pass). Old modes: `build/spyglass-lens` (v880 lens), `build/native-zoom` (the previous per-eye zoom).

## Evidence

- DLL: `build\compile-v1040.cmd` (MSVC 2022 x64 /W4), dll_exit=0, warnings unchanged (9, all in untouched code);
  14,959,104 bytes, sha256 `b446dbde25bf5acd2b8eedd0f97929aed6d00eddaf76ab7a09cb6acd913889a2`.
- Offline checks PASS: arm_state, latest_file_writer, object_motion, spyglass_scope, spyglass_lens, game_xr_wrist_recovery,
  resolution_debounce, native_world_ui, shoulder_carry, vr_settings_native, grip_decision, held_release, native_carry_snapshot,
  world_ui_pose, wheel_eye_art, wheel_avatar, render_size. Python routing checks: identical to v1039 except
  `combat_live_status_routing_check`, updated for the background status write (passes).
  `native_manual_equip_runtime_check` fails at line 519 on v1039 too (pre-existing).

## Not verified

- No headset or sim run of v1040. The arm rebuild changes many code paths at once; the property test covers the model, not the
  engine (copy appearance on the frame a native gun leaves, late equips, the power swap from all three triggers).
- The narrowed right-eye render has not been seen; possible smear during the zoom fades; shadows in the scope are the left eye's
  shared cascades (blockier at 6.7x); a hand raised in front of the face shows magnified in the scope.
- The frame-time gain is estimated (about 0.5-1 ms per frame plus the once-a-second hitch); read `status_write_us`,
  `mod_cpu_ms_mean/p95` and the pacer in `xr-live-status.json` after a session.

## What to check

1. Wheel picks with both hands: the item goes to the hand that picked it; the other hand keeps its item; two pistols still work
   at once; a power moves to the free hand when a weapon goes into its hand.
2. Pick up / throw / catch guns and gadgets with either hand: nothing changes in the other hand.
3. R3: a sharp zoomed circle in the right eye only; comfortable with both eyes open; no smear on zoom in/out.
4. Play a heavy spot: steadier frame time, no hitch every second.
