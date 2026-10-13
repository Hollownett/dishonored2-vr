# VR combat integration (src/vr_combat.hpp)

`d2vr::VrCombat` is a pure per-frame policy for the sword hand. It replaces `MotionSwordSwing` (play_controls.hpp), the `takedown_strike_until` / `takedown_rearm_until` repress logic and the `ThroatSlash` pulse in `src/dxgi_probe.cpp`. It keeps `SwordBlockPose` (vr_gestures.hpp) for the block pose. It never talks to the game: it returns `rt`, `rb` and a haptic, and the caller applies them.

## What it does
1. **Swing classification.** The tip is the grip plus `aim * 0.65 m`. Its velocity is a finite difference smoothed 60/40 (samples under 8 ms apart are skipped, because `GetTickCount64` steps about 15 ms). Head velocity is subtracted, so walking is neutral. The segment logic is:
   - Arm when the tip is under 0.8 m/s.
   - Open a segment above 1.2 m/s, with hand speed at least 0.55 m/s (rejects wrist-only jitter).
   - Fire on the first of these inside 350 ms, once peak acceleration is at least 9 m/s²:
     - Thrust: speed at least 1.7 m/s, velocity within about 37 degrees of the blade and of head-forward, path at least 0.12 m.
     - Otherwise, speed at least 2.2 m/s with path at least 0.18 m. It is classed overhead if the velocity y component is -0.55 or lower, else slash.
   - A segment that outlives 350 ms is spent until the tip slows (slow drifting arcs never fire).
   - Guards: head speed above 2.5 m/s, head turn above 150 deg/s, a tip jump above 0.6 m per sample (tracking glitch, resets), and a 300 ms cooldown. Losing `allowed`, `tracked` or `equipped` resets everything, so nothing fires late.
2. **Stealth kill.** A thrust while the native `kill` sensor is qualified sets `stealth_kill` and drops the thrust threshold to 0.8x. The sensor already checks distance, angle and awareness ("from behind"). RT is held 200 ms. Any swing also attacks, as the game's animation picks the move.
3. **Choke.** The hand reaches 0.2-0.7 m ahead of the head, between 0.4 m below and 0.1 m above head height, slower than 1.5 m/s, with grip at least 0.55 and the native `choke` sensor qualified. RB latches until grip falls below 0.3 or the sensor has been absent for 500 ms. The latch rides over sensor flicker and gives the grip no meaning when no target exists.
4. **Block and parry.** `SwordBlockPose` (horizontal blade in front of the chest, 80 ms in, 150 ms out) sets `rb`. If hand speed peaked at 1.5 m/s or more in the 300 ms before block onset, `parry` is true for 250 ms. That flag only boosts haptics and diagnostics. RB is already held, which is the native parry window.
5. **Drop assist.** If an attack was pressed (swing fired or raw RT edge) within 600 ms before the `drop` sensor rises, RT drops for 40 ms and then holds 200 ms. The game sees a fresh press while the target exists. `drop_assist` is set on that frame.
6. **Haptics.** `Haptic{amp, sec}` goes to `game_xr->pulse(1, amp, sec)`.

   | Event | Amplitude | Duration |
   |---|---|---|
   | Swing | 0.25-0.75 by speed | 0.04 s |
   | Block onset | 0.2 | 0.03 s |
   | `ev_hit` | 0.6 | 0.06 s |
   | `ev_block` | 0.8 (parry 1.0) | 0.08 s (parry 0.1 s) |
   | Kill | 1.0 | 0.18 s |
   | Choke latch | 0.15 | 0.03 s |

   The strongest haptic of the frame wins.

## Integration points in `src/dxgi_probe.cpp`
Line numbers are from the current tree.

- **Statics (~line 305-321).** Add `static d2vr::VrCombat vr_combat;`. After migration `sword_swing`, `swing_until_tick`, `takedown_strike_until`, `takedown_rearm_until`, `last_swing_start_tick` and `takedown_kill_was` can be dropped. Keep the `swings_with_kill`, `takedown_repress` and `takedown_gestures` counters, bumped from the decision.
- **Frame build (~4473-4507).** The gesture frame already has everything needed. After `takedown` is polled (line 4491), fill a `CombatFrame c`:
  - `head_*` from `f`.
  - `pos`, `aim`, `vel` from `f.pos[1]`, `f.aim[1]`, `f.vel[1]`.
  - `grip`, `trigger` from `raw.right_grip`, `raw.right_trigger`.
  - `tracked` from `f.tracked[1]`.
  - `allowed` from `f.allowed && !input.lean_suspended`.
  - `equipped` from `f.sword_equipped && !grip_guard.swing_blocked(1, now)`.
  - `kill`, `choke`, `drop` from `takedown.kill`, `takedown.choke`, `takedown.drop`.
  - `now_ms` from `GetTickCount64()`.
  - Events left false until hooked up: `ev_hit` and `ev_block` need new native signals, and `ev_kill` can be set on the rising edge of `!takedown.any()` after a kill.
- **Replace ~4513.** `if(right_free && gesture.block_held) input.right_grip=1;` becomes `if(right_free && cd.rb) input.right_grip=1;`. Keep the existing free-trigger and grab-guard behaviour. `right_free` is unchanged.
- **Replace ~4514.** Remove the `gesture.takedown_pulse` branch. The `ThroatSlash` pulse is superseded by thrust with kill. Optionally keep it as a fallback by OR-ing it into `cd.rt`.
- **Replace ~4526-4545 (the swing block).**
  - Drop `sword_swing.update` and the repress logic.
  - Keep the grab pulse loop.
  - Use `if(logic==2 && cd.rt) input.right_trigger=1;` and `else` leave the current `input.right_trigger=0` from line 4520.
  - On `cd.swing != none`: `++physical_swings;`, and `++swings_with_kill` if `takedown_kill_now`.
  - Call `game_xr->pulse(1, cd.haptic.amp, cd.haptic.sec)` once per frame when `amp > 0`. This replaces the fixed `.7f,.05f` swing pulse.
  - Keep `grab_guard[1]` zeroing RT after this block.
- **Menu gating.** The existing `input.menu_modifier` block (4555) zeroes everything afterwards, so no extra work.
- **`VrCombat::reset()`** on `pose_epoch` change, level change, or when `logic != 2`. `update` with `allowed=false` already resets internally.
- **Telemetry (line ~3096).** Add counters for thrust, slash, overhead, `stealth_kill`, `drop_assist`, parry and choke frames.

## Tunables
`CombatTuning` fields, defaults in the header. Expose via vr_settings / prefs only if tuning requires it. Most likely knobs:
- `slash_speed` and `thrust_speed`: swing sensitivity (the current motion sword uses 2.0 m/s hand speed).
- `min_accel`: raise to cut false positives from smooth arm waving.
- `kill_thrust_scale`: how generous the assist is when a kill target exists.
- `drop_lead_ms`: drop pre-press window.
- `choke_*` boxes: depend on player height and arm length.

## Test plan
1. `tests/vr_combat_check.cpp`, which passes standalone, covers idle, thrust (with and without kill), slash, overhead, walking (hand moves with head), wrist jitter, slow drift, cooldown and no retract double-fire, lost tracking, disallowed-then-allowed, block, parry, choke (reach, grace, too-low hand), drop assist (fresh and stale press) and haptic events. It is built via `tools/build.ps1` once added to its test list.
2. Simulated-VR injection loop (d2vr-test-loop): scripts for a thrust at a patrolling guard from behind and for a forward swing in an empty room. Check the new counters, then confirm `kill_frames`/`swings_with_kill` rise only with a target.
3. Headset checks (not established by automation):
   - Normal walking with sword drawn: no accidental attacks.
   - Quick head turns and snap-turn while holding the controller still.
   - Blocking while a guard strikes, with RB held for the full swing.
   - Stab from behind: the kill animation plays.
   - Choke: raise the hand, squeeze grip, release.
   - Drop kill: press attack while falling, release lands on the target.
   - Haptic strength comfort.
   - Tune thresholds from the near-miss counters.
