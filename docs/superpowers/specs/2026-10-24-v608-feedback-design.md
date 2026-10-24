# v608 Headset Feedback: Running Arms, Sword, Crosshair, AA, Corpse Weight

Date: 2026-10-24. Source: the user's headset test of v608 (commit `e10ec71`, branch `feat/bonelab-feel`), plus a
read-only code recon the same day. One plan covers everything here. Small-object pickup (spec Part 4 "palm-facing
grab scoring" plus a floor-reach fix) is a separate, later design.

## Intent and why

The user played v608 and found that the arms "work ok but need more work". Running breaks the illusion in every fight.
The sword misbehaves on draw and on flip. The crosshair is in the wrong place. The anti-aliasing defaults and the launcher
do not behave as expected. Corpses are no fun to drag. The goal is a build where moving and fighting with weapons feels
solid, and the settings behave predictably.

The user's words, 2026-10-24:
- "arms and holded objects like sword and gun … while player is running" (they lag behind, shake/jitter and drift off
  the hand; empty hands too).
- "when I flip sword in hands it often missrenders and jitters" (the blade flickers or vanishes, snaps between two poses,
  and shakes after the flip).
- "anti aliasing should be always FSR3 native by default and DLSS should be autodetected on PC and available in options
  and enabled by default on rtx gpus" (default on RTX: DLAA, i.e. DLSS at native resolution).
- "AA options from launcher are not sync with in game VR settings" (the in-game VR menu wins; the launcher mirrors it).
- "crosshair … completely off place and do not follow guns/skills aim (guns and skills work just fine, crosshair should
  be optional)" (fix it to follow the real aim, on by default, with an off switch).
- "Sword should not auto equip when fist is handled. Sword should be equipped from the hold position only" (the existing
  reach-over-shoulder draw stays; a fist anywhere else never draws).
- "Make corpses 2.5 lighter to drag (this is provide more fun)".

## Goals

- **G1 Running.** While the player runs or walks, the drawn hands, arms and held weapons stay on the controllers. No
  visible lag, no jitter, no drift. This applies to empty hands, the sword, the gun and held props.
- **G2 Sword flip.** Flipping the sword in the hand gives one clean, stable grip. No flicker or vanishing blade, no snapping
  between two poses, no shake afterwards.
- **G3 Sword draw.** A fist never draws the sword. Reaching over the right shoulder and squeezing still does.
- **G4 Crosshair.** The crosshair sits where the gun or power actually aims, in both eyes. It is on by default, and it can
  be turned off in the in-game VR menu and in the launcher.
- **G5 Anti-aliasing.**
  - FSR 3 native is the default.
  - On an NVIDIA RTX GPU with the DLSS provider present, DLAA is the default.
  - DLSS modes stay offered only where supported.
  - The in-game VR menu is the source of truth, and the launcher always shows and keeps what the game saved.
- **G6 Corpses.** Dragging a corpse feels 2.5× lighter than v608.

## Non-goals

- Small-object pickup, floor reach and palm-facing scoring (next design).
- Throw history and spin (spec Part 2).
- New weapons, new gestures, or changes to the over-shoulder draw zone.
- Changing turning or locomotion. AGENTS.md says to preserve user-accepted locomotion and turning.

## Anti-goals (failures even if it technically works)

- **Lag swapped for floaty or rubbery hands.** A running fix that adds smoothing so the hands lag less but swim.
- **Hand bodies switched off to hide the problem.** The physical hand bodies are a feature the user just asked for. A fix
  may change how the drawn hand chooses between the body and the controller, but must not make bodies useless.
- **A fist that can no longer do native things it needs.** Blocking, choking and holding a drawn sword must keep working.
  Only drawing from nothing is removed.
- **Saved settings overwritten.** A launcher save, or an RTX default, must never overwrite a choice the user made in the
  game.
- **A crosshair that is right on the bench and wrong in the headset.** Alignment claims need headset evidence (Tier C), not
  only CPU fixtures.

## Constraints

- Worktree `~/github/dishonored2-vr-bonelab`, branch `feat/bonelab-feel`. Never `git stash`.
- Pinned exe `Dishonored2.exe` SHA-256 `b671f8249422862bf557d00aab2f4bb671ed4515cc10ad8a8a9decad84516180`. All RVAs are
  fixed to it.
- Engine write safety and C9 routing as in `docs/superpowers/specs/2026-10-19-bonelab-feel-design.md` (Constraints). The
  raw controller pose drives intent; the drawn pose drives rendering.
- No swapchain or layer changes.
- Harness gate: no new failures against the v608 baseline (628 passed / 1 failed `fsr_taa_gpu_check --fsr4`,
  environment-only / 28 skipped). Launcher tests and `linux/tests` stay green.
- Sim safety as in the finger-curl plan:
  - `PAUSE-SIM` absent;
  - no `Dishonored2.exe` or `vrserver` running before a run;
  - never kill by pattern;
  - the installed DLL hash is restored after each run.
- Testing honesty: the sim and offline checks prove logic and measurable residuals. Only the headset proves feel and
  visuals. Every item ends with a Tier C checklist line.
- The release follows the v608 process (memory `d2vr-release-process`). The result ships as v609.

## Findings from recon (static reading of `e10ec71`; unverified until measured)

### R1 Running lag, jitter and drift (G1). Ranked suspects

1. **The arm root-motion carry is offset from the hand anchor.**
   - The hands are placed relative to the camera anchor read on the render thread (`dxgi_probe.cpp` ~4430-4485,
     `to_game_pose` at ~4835).
   - The arm is carried by `root_now − root_at_publish_`. `root_at_publish_` comes from `root_last_`, the player root noted
     at the previous game-thread arm upload (`full_body_driver.hpp` ~297-305, 421-436, 500-506).
   - A one-tick offset gives an error of speed × offset. At 4-5 m/s and 11-16 ms that is about 5-8 cm, sawtooth in time.
     This reads as lag plus jitter, and it explains why empty hands are affected too.
2. **The hand-body visual source draws a lagging body.**
   - The hand servo (`hand_servo.hpp`: gain 18, max speed 10) drives the body toward a world-space target with no
     player-velocity feedforward. Its own comment says it lags about 0.11 m at 2 m/s and 0.65 m at 6 m/s.
   - `HandVisualSource` (`hand_visual_source.hpp`) elects the body on contact, or whenever it is within 2-3 cm of the clamp
     pose. A trailing body near that band flips the source, and each flip starts a 0.15 s blend.
   - Held weapons follow the drawn hand.
3. **Arm reach and shoulder lag.**
   - Chest yaw lags (tau 0.3 s), and `arm_reach` is one frame stale ("palm can fall 15-46 mm short while walking",
     comment near `dxgi_probe.cpp` ~4880).
   - The soft IK stops the wrist short when the target is near full reach.
4. Hand prediction (about two display periods ahead) under Virtual Desktop.
5. Clamp and weapon casts answer one frame late while the player moves past geometry, which feeds suspect 2.

Measurement already available:
- status fields `full_body` (`palm_residual`, `drawn_palm_residual`, `output_age_seconds`, `arm_gate`), `hand_visual`
  (`blends`, `snaps`, `gap_m`, `*_frames`), `arm_flicker`, `hand_body`;
- traces `arm-origin-trace.csv`, `native-arm-upload.csv`;
- the `head_turn_walk` sim scenario (45 s of walking);
- the knob `build/no-hand-body` for an A/B.

### R2 Sword flip (G2)

- `SwordGrip` (`vr_controls.hpp` ~389-437) latches `reversed_` only on the draw or pickup edge (`draw_dot < −0.2`), with
  no hysteresis and no in-hand re-evaluation.
- The reverse flag has several consumers that may disagree:
  - The collision blade axis is controller forward. `align_weapon_blade` rotates the whole drawn hand.
  - The rig reverse is a half-turn of the hold socket about the palm normal, pivoting at the smoothed wrap point
    (`body_ik.hpp` ~896-903).
  - If the attachment X axis is not the hand forward axis, the two blade axes differ (suspect for "snaps between two
    poses").
- The socket hold latches only after 0.25 s of calm, then eases at 8/s. The reverse is applied to an uncalibrated pose
  during the draw clip (suspect for "jumps, then shakes").
- A sneak-stance flip is detected on the raw native local with no hysteresis (`body_ik.hpp` ~870). It can add a second
  180° flip and a 4.5 cm offset during stance changes.
- If a reversed blade's weapon cast self-hits (unverified), `blade_dir` toggles every frame.
- Measurement: `sword_grip.{held,reversed}`, `full_body.grip[].{reverse,attach_in_fist,hilt_miss}`, `arm_flicker`,
  `native-weapon-attachment.csv`, and the `sword_sweep` scenario.

### R3 Sword draw on a fist (G3). Cause found

- The over-shoulder `SwordGrip` draw is correct and never fires on a fist.
- The bug is the native right-grip passthrough at `dxgi_probe.cpp` ~8203-8209:
  - `right_physical = grip_intent_out[1].fist || native_grab.holding(1)`;
  - with no grabbable body nearby, `RightGripOwner::filter` returns `Role::native` with the raw grip (gate
    `native_no_candidate`);
  - the game receives RB with an empty hand and draws the sword. The comment at ~8370 says so: "no sword in the fist: RB
    would draw one".
- Smallest fix: feed native RB from a fist only when something is held, the sword is held, or the sword is already drawn
  (`hand_weapon_bits & 2`).
- Must stay intact: block (~8370, ~8380, gated on `sword_grip.held()`) and choke (~8367, ~8522).
- `tests/physical_grip_routing_check.py` pins the current string and must change with it.

### R4 Crosshair (G4)

- The crosshair is the engine's own `arkHUDElementCrosshair`, moved by the mod (`native_crosshair.hpp`, `place()`
  ~154-220). It is drawn into the eye by `native_hud_child_draw.hpp`.
- The target is `shared_aim_ray->target(30.f)`. That is the same frozen ray the shot uses, so the ray itself is right.
  The target is projected with only the current eye (`dxgi_probe.cpp` ~5648-5668).
- Suspects, in order:
  1. **Stage mapping.** NDC is mapped to the movie stage as if the stage covered the render target. The HUD movie is a
     1920×1080 stage fitted by width and centred (`native_hud_child_draw.hpp` ~90-99). A near-square VR eye makes y, and
     any letterbox offset, wrong.
  2. **No HUD-scale compensation.** `ndc_limit` is never assigned.
  3. **One eye drives both.** With alternate-eye rendering (default since v602), the other eye is reprojected with the
     crosshair baked in.
  4. **Fixed 30 m depth.** There is no hit distance.
- Existing switches:
  - The launcher Features row `hand-crosshair` (file `no-hand-crosshair`, default on), read at install time
    (`dxgi_probe.cpp` ~9252).
  - There is no in-game VR menu toggle.

### R5 Anti-aliasing (G5)

- **Modes.** 0 off, 1 game TAA, 2 FSR 3 native, 3/4 FSR 3 quality/balanced, 5/6 FSR 4 native/quality, 7 DLAA, 8 DLSS
  Quality.
- **Default.** FSR 3 native is already the default in `vr_preferences.hpp:18`, `dxgi_probe.cpp:205` and `launcher/Core.cs`
  (14, 29). It is pinned by `tests/vr_settings_check.cpp` (398, 486).
- **DLSS detection already exists:**
  - C++: the DXGI vendor ID `0x10de` read at Present, plus the provider (`dlss_offered()`, `dxgi_probe.cpp` ~959, 7449).
  - Launcher: `nvapi64.dll` present, plus the provider (`Features.cs` ~361).
  - DLAA is never auto-selected.
- **One shared file.** Both sides read and write `build/vr-settings.cfg` (format v8). The in-game menu saves immediately
  (`game_xr.hpp` ~1901-1914).
- **Why they disagree:**
  - The game reads the file once at VR start.
  - The launcher rewrites the whole file from a form it loaded earlier (`Program.cs` ~686-718), so a stale form overwrites
    an in-game change.
  - Startup knob files override the file: `build/aa-mode`, `build/fsr`, `build/fsr4`, `build/force-taa`, `build/fsr4.off`
    (`dxgi_probe.cpp` ~9438-9488).
  - The launcher validator rejects HUD > 100 %, while the game offers 125/150/200 %, so a launcher save fails with "Not
    saved".

### R6 Corpse weight (G6)

- The corpse drag force cap is `kCorpseStrengthScale = 2*3 = 6` (`grab_force_cap.hpp:26`). It applies to the corpse motor
  and the tow, not to the hand servo (`hand_servo.hpp:70` forces 1).
- A cap ×2.5 is exactly "effective mass ÷ 2.5", because the cap law is Δv = F·dt/m.
- The value is pinned by `tests/grab_force_cap_check.cpp` (34, 37-50, 162), `tests/corpse_hold_check.cpp` (511, 523,
  535-538) and `tools/corpse_drag_lab.cpp` (44).
- `build/grab-tuning.cfg` can override it, within bounds .5..10.

## Decisions (the user's)

- One plan for G1–G6. Small objects come later, in their own design.
- Sword draw stays as now: reach over the right shoulder and squeeze. A fist elsewhere never draws.
- Crosshair: fix it to follow the real aim, on by default, optional.
- DLSS default on RTX: DLAA (native).
- AA sync: the in-game VR menu wins; the launcher mirrors it.

## Behaviour decisions (builder's calls, made here)

- **G1, measure first.**
  - Add a sim scenario that walks and runs in a straight line with both hands held still in front, plus an optional
    sword-held variant. Record `palm_residual` and `drawn_palm_residual` against player speed, and `hand_visual` blends.
  - Run it with and without `build/no-hand-body` to split suspect 1 (arm carry) from suspect 2 (body source).
  - The acceptance target is "running ≈ standing": median `drawn_palm_residual` while running at full speed is within
    1.5 cm of standing, and there are no source-blend bursts while running in free air.
  - Fix what the numbers show. The likely fixes:
    - carry the arm by the same player-root sample the hand anchor used (store the anchor's root with each publish, not
      the last upload's);
    - give the hand-body target player-velocity feedforward, so the body moves with the player;
    - make `HandVisualSource` prefer the controller pose in free air while moving.
  - No new smoothing on the hand pose.
- **G2.** The user's "flip" is a **regrab**: they let go of the sword and grab it again the other way round (user,
  2026-10-24). So the pickup edge is the main path: `pickup_dot < 0` at `vr_controls.hpp` ~407, with no dead band. A grab
  across the blade near perpendicular can land on either side. There is no in-hand flip gesture, and none is added.
  - One blade-axis convention, shared by collision and the rig.
  - The reverse state changes only at a draw or pickup edge.
  - The pickup decision gets a dead band. Inside it, keep the previous hold's orientation, or forward for a first pickup.
  - Once grabbed, the orientation is fixed for that hold.
  - The flip is applied to the calibrated hold and blended over about 0.1 s, never applied raw to an uncalibrated pose.
  - The sneak-stance flip gets hysteresis.
  - The weapon cast must never hit the player's own body.
  - Measure with a new sim scenario that draws, drops and regrabs the sword both ways and swings it: `hilt_miss`, `reverse` flips and
    `weapon_hidden_frames` must be zero after the blend.
- **G3.** The one-line passthrough gate from R3, plus routing and unit tests ("an empty-hand fist never presses native RB
  while the sword is undrawn"). Update `docs/PLAYING.md`.
- **G4.**
  - Fix the NDC→stage mapping for the fitted 1920×1080 stage, and compensate the HUD scale.
  - Project the target for the eye being rendered (each eye gets its own projection when both render; alternate-eye mode
    uses the rendered eye).
  - Distance: use a cast along the aim ray when a cast is available in the existing cast budget, otherwise 30 m.
  - Add an in-game VR menu row "Crosshair" (on/off), stored in `vr-settings.cfg` (format bump to v9, older files load with
    crosshair on). The launcher Settings page mirrors it. The existing `no-hand-crosshair` feature row becomes the same
    setting, and its file is migrated into the preference on first start.
- **G5.**
  - **One-time switch (the user's decision, 2026-10-24):** existing installs on RTX move to DLAA once, on the first v609
    start. After that, the user's choice always sticks.
  - Rule: when `vr-settings.cfg` has no "user chose AA" marker (a fresh install, or any file written before v609), the
    game picks DLAA if DLSS is offered (NVIDIA vendor and provider present), else FSR 3 native. It then sets the marker.
  - Any later change of AA in the in-game menu or the launcher also sets the marker.
  - Legacy AA knob files do not count as a choice in this one-time pass. They are removed during the same migration, so a
    stale knob cannot block the switch.
  - Saved choices always win. The DLAA default is decided at runtime in the DLL, because the vendor is known only at
    Present, and it is written back to the file so the launcher shows it.
  - The launcher re-reads the file before every save and on window activation, and writes only the fields the user changed
    on the form.
  - The launcher HUD range matches the game (50-200 %).
  - Legacy AA knob files (`aa-mode`, `fsr`, `fsr4`, `force-taa`) are removed once at the v609 migration, with a log line
    naming each file and its value. `fsr4.off` stays, because it is a provider switch, not a choice.
- **G6.** `kCorpseStrengthScale` = 15 (6 × 2.5), for corpses only (the corpse motor and the tow). Props and the hand servo
  keep their scales. The cfg bound rises to .5..25. Tests and the lab pins follow. `build/legacy-corpse-strength` still
  restores 3.
- **G1, if the sim can't reproduce it** (my call). Ship the measured fixes, each behind its own off-switch knob. Add the
  running trace fields to the status block and to the Tier C checklist, so the headset run gives ground truth. Don't block
  on a headset trace.
- **G4 on/off** (my call). Off hides the engine crosshair element; it isn't merely left unmoved. The in-game toggle applies
  live, with no restart, through the existing per-frame `place()` path. The launcher-side setting applies at the next start.

## Verification

- **Tier A:**
  - unit and routing checks for G3, G5 (preference migration, default selection, the launcher merge-on-save) and G6;
  - stage-mapping fixtures for G4 (a known NDC lands at the expected stage point for a square eye and a 16:9 target);
  - hysteresis tests for G2 and the G1 carry maths.
- **Tier B (sim):**
  - the running scenario, before and after (G1);
  - the sword draw/flip/swing scenario (G2);
  - `bonelab_run_check` assertions for both;
  - the existing `head_turn_walk` and `sword_sweep` stay green.
- **Tier C (headset checklist):**
  - run and sprint with empty hands, the sword and the gun, and watch for lag, shake and drift;
  - regrab the sword the other way round five times, including grabs across the blade;
  - fist with an empty hand: no draw; over-shoulder draw still works; block and choke still work;
  - crosshair on the gun and two powers at near and far targets; turn it off and on in-game;
  - fresh-install AA on the RTX laptop (expect DLAA); change AA in-game, then open the launcher (it shows the same);
  - drag a corpse (2.5× lighter, still controlled).

## Left to the builder

- The exact sim scenario timelines and thresholds beyond the acceptance targets above.
- The order of fixes inside G1 once the numbers are in.
- Where the "user chose AA" marker lives in the file format.
- Whether the crosshair cast reuses an existing cast path or adds one, within the existing budget.
