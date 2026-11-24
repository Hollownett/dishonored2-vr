# Death of the Outsider VR: Billie's powers, dual Voltaic Guns, grip fixes, mines (design, 2026-11-24)

Builds on `docs/DOTO-VR.md` (state at v145751, branch `feat/doto-vr` @ 7ef5612). Research behind every decision below
(static RE of both executables, code reading, web sources) is in `docs/doto-research/2026-11-24-R{1..4}-*.md`.
Tags: PROVEN = read in code/exe bytes; UNVERIFIED = needs a simulated or headset read (the plan measures it first).

## 0. Constraints

- No headset and no Windows. Verification = host checks (`tests/*_check.cpp` under the Linux clang-cl + Wine harness)
  plus simulated sessions of the real game under Proton on the laptop (mod's scripted XR backend). Per AGENTS.md these do not
  establish headset feel; every new feature ships as a launcher feature row (default on) and has a headset checklist line.
- A/B against the branch HEAD build before claiming a fix (user rule after v613): a baseline sim of 7ef5612 is recorded first.
- One source tree: D2 behaviour must not change unless the change is the fix (issue #6 is a D2 bug). DOTO-only engine
  facts go behind `game_traits` in `src/game_profile.hpp`; DOTO-only RVAs need keep-as-is rules in `tools/doto/offsets.json`
  (port.py translates every hex literal in `src/` as a D2 address).
- Hypothesis fixes default-off; fixes proven in sim default-on.

## 1. GitHub issue #6: the grip casts a held power (D2 Far Reach; every hold-to-aim power, DOTO Displace too)

Cause (PROVEN by code): `src/dxgi_probe.cpp:17682` ("a fist never fires") zeroes LT when the power hand's grip passes .55.
The pad composer forwards that as an LT release; the engine's UsePower (state 63) release handler (D2 `0x8566f0`) calls
the power's Cast (vt+0x120). The grip never sends a native cancel (`:16999` grips belong to the hands). There is no cancel
action in the game's action table; the state's exit path calls StopAim (vt+0x128) without casting.

Design: **grip squeezed while a power aim is held = cancel**. (1) A held power aim (trigger-first, power in the hand's slot)
is exempt from the fist rule, so LT stays held. (2) A grip rising edge during the aim latches "cancel"; a MinHook detour
on the UsePower release handler calls the power's StopAim before the original when the latch is set and `power+0xa4`
is non-zero, so the original skips Cast. Class-independent (Blink, Far Reach, Domino, Doppelganger, Mesmerize, Windblast,
Possession, Displace). Haptic tick on cancel. The hand's grip Use/grab is suppressed while the aim is held so the grip is
free to be the cancel. Fallback if the hook cannot install: keep LT held while gripped (no accidental cast) and release
silently only after the grip opens (the user lets go of the trigger to finish).

## 2. Hook Mine lethal / non-lethal (DOTO)

Engine (PROVEN): lethality lives in `arkCpntWeaponStates`; the switch is player state ChangeWeaponState (id 73) with a
decl timer. Flat game (R1, guide sources): **hold the mine's use button (LT) for a moment** switches mode (red lethal /
blue non-lethal); a tap throws. In VR the mod turns gadget triggers into a 150 ms tap (`GadgetGrip::tap_until_`) or a
grip-held LT, so a long hold either never reaches the game or ends in a throw.

Design: measure first (scenario: LT holds of 0.3/0.8/1.5 s with a hook mine equipped, read state 73 and the
weapon-states index). If the long LT hold is the switch (expected): **mine equipped, hold the trigger ~0.8 s = switch
mode** (the native long hold passes through; release after a switch never throws), tap = throw/place as today. Haptic
double tick on a switch. If the measurement shows another input, pulse that one instead, same gesture. Trait
`game_traits::gadget_mode_switch` (DOTO true, D2 false).

## 3. A thrown mine attaches like the flat game

Cause (PROVEN): in dual wield, opening the grip with a mine calls `NativeGadgetDrop::drop`, which spawns the mine's
loot pickup and flies it with the mod's own physics; the engine's attach (`arkCpntHookMine` THROWN -> STUCK_*) never runs.

Design: **grip opened with the hand moving (> 1.5 m/s over the last 100 ms) = engine placement along the throw**: emit a
native placement tap and publish a 250 ms throw-aim override to `NativeGadgetAim` (origin = hand, direction = hand
velocity, pitch clamped +-85 deg). The engine raycasts, spawns its own throwable and sticks it. A slow open stays the
current drop. If the engine refuses (no surface / no item consumed within 400 ms) the toss fallback runs.

## 4. Dual Voltaic Guns polish (DOTO; ring rule shared with D2)

Current (R3): native gun on the left forearm, a mod-spawned copy on the right; each fires on its own grip + trigger;
one HUD crosshair on the native gun's ray plus a per-copy ring.

Decisions:
- **Sword hand**: a hand holding the sword shows no crosshair/ring for its forearm gun and never fires it (fire is
  already blocked). Fix the side effect found in research: `:17169-17170` zeroes that hand's native trigger on
  grip + trigger whenever a copy rides the forearm, which kills sword block in the default plan (sword right + copy right).
- Main HUD crosshair hidden when its hand holds the sword or when the engine gun is hidden (right-only arms); ring uses
  its own reticle gate (no 0.5 s blink after sword swings); ring also hidden while that hand grabs/holds/carries.
- **Bolt type per hand**: the right wheel's choice is applied to the copy's own component (`select_ammo`) and tracked like
  D2 copies (`note_copies`), so the copy shows and fires its own bolt; status exposes each hand's bolt.
- **Render**: the copy is placed once per render at eye-0 Begin only; place it again at eye-1 Begin (per-eye
  placement), after a stereo capture baseline shows the gap. Right copy mirrored by scale if the capture shows handed
  details; copy VFX only if missing in capture.

## 5. Billie's powers

Shared: the power hand's trigger is LT (hold = aim, release = cast); the hand ray is published into the camera aim block
while the trigger is held. A read-only observer (new) reports power components, aim flags, state actives and decl values
to `xr-live-status.json` so every power change is measured.

| Power | Engine (PROVEN) | VR design |
|---|---|---|
| Displace | `arkCpntPowerSwapper`; destination solver reads the camera aim block; one Cast places (no marker) or swaps (marker); swap = player state 93 | Hold trigger: hand-aimed marker (engine FX at the target); release: place. With a marker placed, a trigger tap swaps (the game's own rule) behind a 100 ms dark fade at the state-93 edge; grip while aiming cancels (§1). Publish gate also holds while the Swapper aims. If the sim shows head steering: hook the solver job (`0x3e21b0`) like the D2 Blink hook. |
| Foresight | `arkCpntPowerTheEye` moves the player entity as the spirit under the normal camera; exit restores origin + yaw | Game's own trigger cast; spirit moves with the user's locomotion and comfort vignette. While spirit (states 3/4): Billie's full body hidden, physical hand contact/grab/carry off, tracked hands stay. Exit: 200 ms fade, anchor/smoother reset, turning treats the restored yaw as a snap. |
| Semblance | `arkCpntPowerMimic`, armed from inside a choke; LT during the choke takes the face | LT must pass during a choke even when that hand is a fist (fist-rule exception while state 41 is active). |
| Void Strike | variant of the Slash state: held attack with the attribute owned, commits when the hold passes `decl+0xa0`, costs `decl+0xf0` mana, aims from the camera aim block | The existing sword charge release becomes a Void Strike when owned: press length = `decl+0xa0` + 60 ms (per-press cap raised); thrust direction published as the aim during the release. |
| Original Game+ | Blink/Dark Vision/Domino return; the D2 Blink hook pin fails in DOTO | Out of scope for this iteration (noted in DOTO-VR.md). |

## 6. Lab and simulation rig (prerequisite)

- `tools/linux/build_doto.py <ver>`: port.py + the `build-doto.cmd` compile under clang-cl/xwin (same flags).
- `linux/d2vr-linux --game doto ...`: game profile (app 614570, `Dishonored_DO.exe` sha be73d9..., saves
  `Dishonored_DO/base`, config `dishonored_DOConfig.cfg`, mod home `DishonoredVR-DOTO`) for install/doctor/sim.
- Lab saves: chapter-2 Karnaca saves on the laptop (powers owned); save slots are backed up/restored by the sim.
