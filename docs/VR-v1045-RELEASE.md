# VR v1045 (test candidate, 2026-11-06)

The user's v1040 headset report: a weapon picked into the free hand while the other hand holds a power was not shown until a
re-grip; bodies could no longer be put on the shoulder; changing the resolution slider sometimes closed the game; wanted an
"Upscaler scale" slider shown only with FSR/DLSS; the scope should use the game's own lens art. Built by five helpers (patches in the
scratchpad, integrated here) and verified in simulated sessions; v1041-v1044 were intermediate test builds.

## 1. Resolution slider closed the game (FSR upscale modes)

Root cause (proven): `Fsr4EyeUpscaler` kept a reference to the caller's output in each of its 4 GPU slots per eye until the slot was
reused (`slot.caller[4]`). In the scaled modes (FSR 4 Quality, the user's mode) that output is the game's swapchain back buffer, so
every engine `ResizeBuffers` failed with `DXGI_ERROR_INVALID_CALL`, which the game turns into its own fatal error ("ResizeBuffers",
Dishonored2.exe+0x36ae22) and an `int3`: the process ends with 0x80000003, no Windows crash report. The mod's output-size restore at a
session stop hit it too. Earlier resize tests passed only because they ran FSR 4 Native (full scale, no upscale output).

- Fix: the output is not retained (D3D11 keeps it alive for the copy already queued).
- Hardening (resolution helper): the whole apply step is caught and retried instead of restarting XR; a failed XR eye resize keeps the
  old swapchains and retries; a video-memory guard caps an upward step that would not fit; a faulting read forgets its cached range.
- Evidence that survives a crash: `native-exception-context-last.txt` (memory-mapped ring of the last faults, a FATAL line from the
  unhandled-exception filter plus `-fatal.dmp`, an EXIT line), `.previous` copies of the crash/recovery/status files at the next start,
  and `artifacts/resolution-apply.log` (each slider step, the engine's ResizeBuffers result with the back buffer's reference count,
  the XR resize, each FSR/DLSS rebuild).
- Sim (FSR 4 Quality, 2496x2688 eyes): 12 steps from 90% to 145% and back, every ResizeBuffers OK with only our probe reference, no
  XR restart; the game now survives the session stop.

## 2. Shoulder carry

Root cause (disassembly + sim): the game's carry (CarryCorpse, state 45) waits at stage 1 until both hand slots are empty and aborts
with "Both hands have not been unequipped" otherwise. v1039 kept the left item in the hand (to keep powers usable), so every carry
started and dropped at once (the user's session: started 5, takeoffs 5).

- The left unequip stays, only its lock is removed (the hand is not locked during the carry).
- Once the carry holds (stage 3 of [state+0x110]; sim: 1 -> 2 after ~0.3 s, 3 after the ~1 s lift, held until the drop), the left item
  is equipped again the way the wheel does (its inventory record, code 0x30), up to 3 tries. The first try (EquipChange "draw the
  selected item") was refused by the game during the carry; the wheel-style equip works.
- Sim (pistol in the left hand): carry held to the drop, pistol back in the left hand 250 ms after stage 3. Journal rows
  `carry_left_state` / `carry_left_redraw`; status `shoulder_carry.left_rewrites/left_unequips/left_redraws`. Knob
  `build/carry-locks-hands` restores the game's own lock-and-empty.

## 3. Wheel pick into a hand while the other holds a power

From the user's journal: the hand already held a copy; the wheel's choice became a new copy (`copy_restore` route), and the old copy's
queued drop (`NativeWeaponHands::gun_drop`) also cleared the hand's requested copy, so the new one stayed hidden until a re-grip.
Fix: the drop releases only the copy it drops and never the hand's request; a copy that fails to spawn is retried (250 ms, 12 tries)
instead of being latched until the next grip. Not reproduced in the sim (verified from the journal and code). Unchanged by design: a
hip squeeze with a power in the other hand draws nothing (the hip is the engine's slot-1 holster).

Also: the `grip_choice` journal row was written every frame (the 4000-row journal cap filled in about 2 minutes, so later events were
lost); it is now written on change, and the journal flushes every row.

## 4. Upscaler scale (new VR Settings row)

The page keeps its 10 rows: row 2 is the "Upscaler scale" slider (50-100%) while the anti-aliasing mode is an FSR or DLSS mode, and the
"Edge anti-aliasing (SMAA)" toggle otherwise (it has no effect in FSR/DLSS modes). Picking a preset (Native 100, Quality 77, Balanced
67, DLSS Quality = NVIDIA's optimal size) sets the slider; moving it sets a custom scale and the AA entry reads "FSR 4 Custom (NN%)".
The value is saved (settings file version 11; older files load as presets). A custom DLSS scale is kept inside NVIDIA's range.
Changing the AA mode between an upscaler and a non-upscaler re-shows the page (the row changes kind); after that no row is
highlighted until the next up/down. Knobs `build/no-live-row-switch`, `build/no-live-row-values` keep the page as opened.
Sim: all of the above seen in the game's own page.

## 5. Scope: the game's lens art

During the first zoom of a session the mod finds the game's own lens draw (the right view keeps the engine's lens quad for two short
blocks and drops it for one, and the draw that follows the quad is taken) and from then on draws that texture over the scope: the game's
1920x1080 lens mask (BC4, 0 = the opening, 255 = masked, painted rim), as black with the mask as coverage, black outside. The
probe writes `spyglass-lens-art.json` and a one-shot texture dump. While it searches (about 0.2 s, once per session) the right eye
shows the game's full-screen lens. Fallback: the v1040 ring. Sim: art found on the first zoom; the scene shows inside the lens.

## Evidence

- DLL: `build\compile-v1045.cmd`, dll_exit=0, 9 compiler warnings (as v1040); 15,025,664 bytes,
  sha256 `13944b66ab9d483f6ef2693ed850a44c895c42fd341141ed9353ca2aaa3eb0f5`.
- Offline checks: see `artifacts/repair-v1045-test-candidate.json` (`fsr4_eye_upscaler_check` runs the real FSR 4.1.2 dispatch).
  `native_weapon_visibility_check` and `corpse_hold_check` fail to compile on C4459 in files unchanged this round (pre-existing).
- Sims: saves byte-identical after the batch, game config and proxy restored.

## Not verified

No headset run of v1041-v1045 (comfort, the lens art's size in the real FOV, the carry feel, the upscaler row with a real controller,
DLSS custom scales on NVIDIA). The wheel/power case is fixed from evidence but not reproduced.
