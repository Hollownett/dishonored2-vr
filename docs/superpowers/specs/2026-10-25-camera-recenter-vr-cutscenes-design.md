# Scripted-Moment Recenter and VR Cutscenes

Date: 2026-10-25. Source: the user's headset play of v609 (commit `c668be9`, branch `feat/bonelab-feel`), a brainstorm the
same night, and a read-only code recon. This is its own fix, built and shipped **before** the small-objects / fingers design
(`2026-10-25-small-objects-fingers-design.md`).

## Intent and why

The user's words: "when I have a camera off by some degree of the center and climbing something, it [is] still in the
place where it was. I expect that [the] camera will be gently recentered at this point to align with [the] body position,
so [the] player see[s] a proper animation (same for takedowns and cutscenes and dialogs)". And: "I expect that all first
person cutscenes and dialogs will be in VR (not a flat screen)", "VR cutscenes should be a comfort option, for some people
it can be very intense". Earlier, v603: "camera off-place sometimes when climbing/killing".

A scripted moment should play in front of the player, the way the animators framed it, in full VR, without making anyone
sick.

## Goals

- **G1 Scripted moments play in front of you.** At the start of a climb, a mantle, a ledge grab, a takedown, a choke, a
  finisher, or a dialog that uses the game's authored camera, the world turns gently around the player so the action is in
  front of where they are already looking. When the moment ends, the new direction is kept.
- **G2 First-person cutscenes are in VR.** Cutscenes that the game renders in-engine (the "Cinematic" state) render in
  stereo 3D in the headset, not on a flat screen.
- **G3 Cutscenes have a comfort setting** in the VR menu, with three modes:
  - **VR comfort** (default): full 3D. The camera position follows the scene, but the player owns the rotation. At the
    start and on every camera cut, the view gently re-aims so the action is in front of them.
  - **VR full**: the scene's camera rotation also moves the view, with the player's head on top. This is the most
    cinematic mode and the most intense.
  - **Theater**: today's flat screen in front of the player.

## Non-goals

- Pre-rendered video cutscenes in 3D. Those are 2D videos and stay on the theater screen in every mode.
- Changes to normal gameplay view, snap or smooth turning, or the manual "Recenter view" menu row.
- Changes to the hands, fingers or grabbing (the other design).

## Anti-goals (failures even if it technically works)

- **A sudden snap or a camera swing** at the start or end of a scripted moment. Every turn is eased.
- **A view that swings back after the moment ends.** The new direction is kept.
- **Nausea from tiny corrections.** Small offsets are left alone.
- **Broken image in a cutscene**: one eye black or stale, a mono image shown as stereo, the menu pointer appearing in a
  cutscene, or a crash on cutscene entry or exit. An unknown or incomplete cutscene frame falls back to the theater screen
  for that cutscene instead.
- **Losing the keyhole and peeking views** that already work (logic 2, mode 3).
- **Frame-rate loss in gameplay.** Nothing new runs outside scripted moments and cutscenes.

## Constraints

- Worktree `~/github/dishonored2-vr-bonelab`, branch `feat/bonelab-feel`. Never `git stash`.
- Pinned exe `Dishonored2.exe` SHA-256 `b671f8249422862bf557d00aab2f4bb671ed4515cc10ad8a8a9decad84516180`.
- No new engine writes for G1. G2 changes only how the mod classifies and presents the cinematic state.
- Harness gate: no new failures against the v609 baseline (laptop harness 632 passed / 1 failed `fsr_taa_gpu_check
  --fsr4`, environment-only; launcher 127 passed; `linux/tests` 230 passed).
- Sim safety as in the finger-curl plan. The GNOME screen stays unlocked during sim runs.
- Testing honesty: the sim proves logic and images. Only the headset proves comfort. Every goal ends with a Tier C line.
- Release as v610 by the usual process (memory `d2vr-release-process`).

## Findings from recon (static reading of `c668be9`; unverified until measured)

- **Normal gameplay**: `prepare_head_view` (`dxgi_probe.cpp` ~4425–5730) builds the view from `BodyAnchor` (the native
  camera position plus the levelled body yaw). The HMD pose maps in through `head_origin`, a yaw-only room reference from
  `HeadCalibration` (`head_calibration.hpp` ~20–30). It is reset only on a recenter epoch or a new player, camera or
  generation.
- **Authored moments** (climb, takedown and similar): `authored_camera` is decided at ~4436–4449 from
  `native_action_camera.poll(...).owns_camera(...)` (`native_action_camera.hpp`: nine non-combat FSM states 8, 9, 13, 33,
  38, 41, 42, 55, 83 are `scripted`, and 39, 44 are `combat`; the indices are not named). The game's camera rotation is
  ignored on purpose. Only its centre position is used (`authored_view.hpp` 1–3).
  - `AuthoredHeading::update` (`authored_view.hpp` ~40–49) latches the native level yaw at entry, and re-aims only on a
    jump of 30° or more.
  - `authored_head_view(..., head_origin.orientation, authored_physical_offset)` (call sites ~5728 and ~5822) applies the
    HMD orientation relative to `origin`.
- **Cause of the symptom** (inferred): the view yaw is HMD yaw minus origin yaw plus the latched body yaw. If the head is
  turned 40° from the last calibration, the whole animation plays 40° off to the side. Nothing aligns the HMD to the body
  at entry.
- **Cutscenes** (`game_logic_mask` 32 = Cinematic): `sample_stereo` restores native stereo, and `GameXr::present(!gameplay)`
  picks a quad, so they are flat. `docs/native-authored-scene-presentation.md`: "the current flat square is caused by
  classification" and "Generic Cinematic32 presentation remains a separate unresolved case". It already lists the safety
  requirements for an immersive authored scene:
  - a complete tracked eye pair, never a duplicate or mono texture as stereo;
  - no native menu pointer;
  - a clean return to pause and menus.

  The keyhole (logic 2, mode 3) is accepted.
- **Dialogs**: no dialog-camera detection exists. Most dialogs are ordinary gameplay. Those that use the authored camera or
  the cinematic state are covered by G1 or G2.
- **Reusable**:
  - the comfort vignette (`comfort_vignette.hpp`; it is inhibited while `authored`);
  - `tracked_yaw(centre_pose, head_origin)` for the HMD-vs-origin yaw;
  - the `comfort_meter` telemetry;
  - the VR settings rows (`vr_settings.hpp`, `vr_settings_native.hpp`).
- **Tests**: `authored_view_check.cpp`, `action_camera_alignment_check.cpp`, `native_action_camera_check.cpp`,
  `head_calibration_check.cpp`, `comfort_vignette_check.cpp`. There is no authored, cutscene or mantle sim scenario.

## Decisions (the user's)

- Recenter by turning the view to the action (the world rotates gently around the player), not by a fade, and not by
  turning the body.
- After the moment, the new direction is kept.
- First-person cutscenes and dialogs are in VR. A comfort setting has three modes (VR comfort, VR full, Theater), and VR
  comfort is the default.
- This ships first, before the hands work.

## Behaviour decisions (builder's calls, made here)

### G1 recenter

- **One pure helper** (a new header, for example `src/scripted_recenter.hpp`) holds an extra yaw offset composed into the
  origin: `origin' = yaw(offset) · head_origin`. It is used by both `authored_head_view` call sites, so the eyes, the
  listener and `authored_physical_offset` all follow.
- **Entry**: when `authored_camera` goes from false to true, `delta = wrap(HMD yaw relative to origin − latched heading
  offset)`. That is the angle between where the player looks and where the animation faces. If `|delta| ≥ 10°`, the offset
  eases to `delta` over **0.4 s** (smoothstep). Under 10°, nothing turns.
- **During**: when `AuthoredHeading` re-aims on a 30° native jump, a new delta is computed against the new heading and
  eased the same way.
- **Exit**: the offset is **folded into the head origin** (a yaw-only update of the stored `head_origin`, not a recenter
  epoch: no calibration, avatar or menu reset). Normal gameplay continues with "forward" = where the player now faces.
  There is no swing back.
- **Head movement during the ease** is respected. The offset is a target, not a lock, so the player can still look around
  while it eases.
- **Covered states**: all authored-camera states, both `scripted` and `combat` (takedowns and finishers are the user's
  explicit cases). A sim scenario maps which index is which. If a state's animation turns out to drive a big intentional
  camera rotation, that state gets the 30° re-aim path, not exclusion.
- **Comfort**: during the 0.4 s ease, a light vignette (the existing vignette, inhibit lifted for the ease only; strength
  from the existing comfort setting). VR menu toggle "Recenter on scripted moments", default on.
- **Reset**: the offset is dropped on a recenter epoch, or on a player, camera or generation change.

### G2 / G3 VR cutscenes

- **Classification**: exact mask 32 with a valid paired native source (both eyes rendered with distinct poses and FOVs,
  the same checks as gameplay) becomes an immersive scene. `GameXr::present` gets the `authored_scene_render` argument
  described in `docs/native-authored-scene-presentation.md` §5. It uses projection layers, disables the native menu
  pointer and keeps raw native buttons (skip still works). Mixed masks (2|32), loading and pause stay on today's path.
- **Fallback**: if a cinematic frame lacks a complete distinct eye pair (a pre-rendered video, an unknown renderer path,
  a fault), that cutscene shows on the theater screen until it ends, and a status counter records why. No per-frame
  flicker between modes: once a cutscene falls back, it stays on the theater screen until it ends.
- **VR comfort mode**: the view is built like G1's authored view. The camera centre is the scene's camera position, the
  rotation is the player's head, aligned at entry with G1's helper. On a **camera cut** (the native camera position jumps
  more than 0.5 m, or its yaw jumps 30° or more, in one frame), it re-aims instantly on the cut frame (see below). Position-only camera
  moves (dolly, pans) move the player. Vignette while the scene camera moves faster than the existing comfort-meter
  threshold.
- **VR full mode**: the scene camera's full rotation is applied, with the HMD rotation on top. Cuts are instant, as the
  director intended.
- **Theater mode**: today's behaviour, unchanged.
- **Exit**: as G1, the direction is kept.
- **Setting**: VR menu row "Cutscenes: VR comfort / VR full / Theater", saved like the other VR settings. The launcher
  settings page mirrors it (the in-game menu wins, as with AA in v609).

### Settled after the builder check (builder's calls)

- **G2 starts with a read-only capture spike and a go/no-go gate.** Before any presentation change, the spike records
  under mask 32 in a real cutscene:
  - whether the mod's eye setup and begin hooks fire twice per frame with distinct poses;
  - which camera entity is active (the player camera or another one);
  - whether the identity and N→N+1 eye matching hold.

  **Go**: G2/G3 are built as described. **No-go**: v610 ships G1 with Theater unchanged. The spike's findings and the
  missing lever are written up in `docs/native-authored-scene-presentation.md`, and the user is told what VR cutscenes
  would need. G1 never waits on G2.
- **Engine writes.** "No new engine writes" means no new *kinds* of writes. The cinematic view may reuse the gameplay
  path's existing head-view writes and restores (`prepare_head_view`, the matched head writes and source restores the
  keyhole uses). That is how VR comfort applies the head rotation and how VR full adds the head to the scene rotation. If
  the spike shows the cutscene camera is not reachable by those writes, that is the no-go case.
- **Exit continuity.** The world-space view direction is continuous at exit. The fold into `head_origin` also absorbs
  any body-yaw change the game makes at exit (for example a mantle ending facing the ledge). If a residual remains after
  one frame, because the game's turn spans several frames, it is eased out over 0.4 s. Zero-jump is the target, and an
  ease is the fallback, never a snap.
- **Hard cuts re-aim instantly.** A cut is already a discontinuity, so on the cut frame the re-aim is applied in full,
  with no 0.4 s sweep across the new shot. The 0.4 s ease is only for scene entry, G1 entries and 30° re-aims inside a
  continuous shot. The 10° dead zone still applies. No minimum interval is needed, because instant re-aims don't stack
  sweeps.
- **Ordinary-gameplay dialogs** are already rendered in full VR (they are gameplay, logic 2) and need nothing. Only
  dialogs that use the authored camera (G1) or the cinematic state (G2) change.
- **Scripted states with big intentional camera rotation** use the 30° re-aim path. That is an acceptable result, not an
  escalation. The sim records which states do it.
- **Pre-rendered video**: the complete distinct eye pair is the criterion. If the spike also finds a reliable video-player
  signal (for example a Bink playback flag), it is used as an extra "theater" reason.
- **Launcher mirror** of the cutscene setting is included in v610, following the v609 AA precedent (the in-game menu
  wins).

### Diagnostics

- Status block `scripted_view`:
  - the state index at entry;
  - `delta` at entry;
  - the ease time;
  - re-aims;
  - exits with the offset folded;
  - cutscene mode;
  - cinematic frames immersive vs theater, with fallback reasons;
  - camera cuts detected.

## Verification

- **Tier A**:
  - the recenter helper: the ease ends exactly aligned, under 10° does nothing, a 30° re-aim eases again, the fold on exit
    makes the gameplay view continuous (no frame-to-frame yaw jump above the ease rate), and an epoch drop resets it;
  - the cutscene classification policy: mask 32 plus a complete pair is immersive, anything else is not; mixed masks, pause
    and loading are rejected; the fallback sticks for the cutscene;
  - cut detection thresholds;
  - mutation checks on 10°, 0.4 s, 30° and 0.5 m.
- **Tier B (sim)**:
  - an `authored_turn` scenario: turn the simulated head 40°, trigger a mantle and a takedown (find spots with
    `tools/entities.py` / `vr_selftest.py`), and record the yaw residual between the HMD forward and the animation heading
    at the end of the ease (target under 3°), with no jump at exit;
  - a `cutscene` scenario: load a save just before an in-engine cutscene, and capture both eyes in each mode. VR comfort
    and VR full submit distinct stereo projection layers; Theater submits the quad; skip works; the return to gameplay is
    clean. If no save near a cutscene exists, ask the user for one.
  - The existing keyhole/peek and `head_turn_walk` stay green.
- **Tier C (`docs/VR-v610-TEST-CHECKLIST.md`)**:
  - with your head turned well away from your body, climb a ledge, do a takedown and a choke: the action is in front of
    you, the turn is gentle, and nothing swings back afterwards;
  - watch a cutscene in each of the three modes and a dialog: 3D, comfortable in VR comfort, no broken eye, skip works.

## Left to the builder

- The exact sim saves and timelines.
- Where the cut detector reads the scene camera (the native view the mod already samples).
- The vignette strength curve during the ease.
- Task order, except that the recenter (G1) is built and verified before the cutscene work (G2/G3).
