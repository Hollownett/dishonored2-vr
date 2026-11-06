# VR v1020 (2026-11-05) — merged build

One build of the worktree after both sessions: the v1019 corpse-on-shoulder carry (parallel
session) and the v1018/v1019-era held-weapon work (this session). Nothing else changed.

## Contents

- **Corpse on the shoulder (v1019, unchanged).** A body released at either shoulder is handed to the
  game's own `arkPlayerStateCarryCorpse` (FSM 45) through the engine's held Use, so the game owns the
  carry pose, Drop/Throw, prompt and mission scripting; the physical shoulder (v636) remains the
  fallback. New: `src/shoulder_carry.hpp`, `src/grip_decision.hpp` take, `src/dxgi_probe.cpp` wiring;
  knobs `no-shoulder-carry`, `no-shoulder-native`, `legacy-corpse-carry`; status `shoulder_carry`.
  See `docs/VR-v1019-RELEASE.md` and `docs/VR-v1019-TEST-CHECKLIST.md`.
- **Held weapon is a normal engine object (this session, new default).** `ObjectMotion::weapon_mode`
  now defaults to 0: the engine draws the held weapon at its own tick transform, both eyes identical,
  with the engine's own depth, motion vectors and colour. The reason and evidence are in
  `docs/WEAPON-NORMAL-OBJECT.md`: the engine's item advance already followed the drawn hand within
  1.03 mm (native pistol) and 22.05 mm / 4.9 deg (crossed sword) in the v1017 session, while the
  per-draw per-eye rewrite authored the only near object's temporal inputs and is what FSR 4 / TAA
  turned into a black left-eye weapon and right-eye flicker. `build/weapon-draw-place` opts back into
  the round-6 binder for A/B; `build/no-weapon-draw-place` still forces it off.
- **v1018 publisher fixes (kept for the opt-in path).** The grip offset is learned only from a writer
  note whose read is the hand this render draws (3 cm / 8 deg; `weapon_draw.stale_note` counts the
  rest), and a copy entity shared by both hands is published only for the hand it was placed from
  (the dual-wield `ambiguous` 9,697-draw case). With the default placement off, `arm_flicker.weapon_draw`
  is all zeros by design.

## Build

- `build/d2vr_render_probe_arm_v1020.dll`, 14,902,784 bytes, SHA-256
  `3492c698fdf92c03af67c46f12171cf6de73f615edb84a82f25c46f18932377e`, compiled by
  `build/compile-v1020.cmd` (MSVC 2022 `/W4`, `dll_exit=0`).
- `artifacts/repair-v1020-test-candidate.json` is the candidate the launcher picks (newest unblocked,
  DLL present, sha256 checked by the launcher build task).
- Launcher: `%USERPROFILE%\Downloads\DishonoredVRLauncher-v1020.exe`, 83,183,121 bytes, SHA-256
  `11dcf1014b84afcc3d36653f5acff46c6a2495d145402a3847fa843254ce9af8` (same bytes as
  `launcher/dist/DishonoredVRLauncher.exe`). Its publish printed
  `Embedding d2vr_render_probe_arm_v1020.dll from repair-v1020-test-candidate.json`.
- A `--page play --screenshot artifacts/v1020-launcher-play.png --exit` run shows every setup check
  green and `Mod d2vr_render_probe_arm_v1020.dll (verified)`; that run installed v1020 into
  `%LOCALAPPDATA%\DishonoredVR\build` (the only probe DLL there now) and removed v1019. The
  `no-weapon-draw-place` knob was removed, so the installed v1020 runs its new default.

## Checks run on this build

- `object_motion_check` OK, `weapon_draw_place_check` OK (the latter is the header-only lab; it does
  not cover `publish_weapon_draw`).
- `shoulder_carry_check` PASS, `grip_decision_check` PASS, `held_release_check` PASS,
  `shoulder_carry_routing_check ok` (the v1019 checks, re-run against the merged source).
- `tools/build-msvc.cmd` still stops on pre-existing `/WX` warnings, so the per-version compile
  script and the named checks above were used. The known pre-existing failures named in the v1019
  note (`corpse_routing_check`, `physical_grip_routing_check`, `held_object_routing_check`) were not
  re-run.

## Not verified

- No headset or game run of v1020. Neither the shoulder handoff (does the engine accept the body at
  the shoulder; the carry pose; Sokolov) nor the normal-object weapon (does the black left eye and
  right flicker disappear; is the 1-22 mm residual visible; does a dual-wield copy trail) has been
  seen in game.
- A mod-spawned held copy (dual wield) is still teleported from the last drawn hand at game-tick
  rate, so it can trail a fast hand by up to one frame without the placement. Native weapons use the
  engine's item advance and do not.
