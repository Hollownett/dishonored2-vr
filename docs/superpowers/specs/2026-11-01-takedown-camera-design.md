# Takedown and Finisher Camera in VR: Research and Design

Date: 2026-11-01. Branch `feat/bonelab-feel`. Status: design for review; nothing is built yet.
Evidence used:
- the v636 headset session logs (`artifacts/headset-v636-20261027T1331`);
- an offline census of the game's 269 takedown and fatality clips (`artifacts/takedown-camera-census/`);
- a read of the current camera code;
- web research on VR comfort and on how other VR games and mods do this.

Figures: `artifacts/takedown-camera-research/v636-gaze-vs-game-pitch.png`, `artifacts/takedown-camera-census/plot_*.png`.

## Intent and why

The user wants this about kills and finishers in combat: "the camera inside, for the player in VR, during kills and finishers should be more pleasant: the player should see what is happening and not get motion sick. It should be visible what the character does, where, and how this mini-cutscene works, and it should be done well". They suggested "maybe increase the field of view a little in these cutscenes". Today "in some cutscenes I can't see what is happening at all, and it looks strange; sometimes the camera is placed strangely".

A takedown should read like a short, well-framed moment: your hands, the victim, the strike. It should be in front of you, in full VR, with no lurch that makes you sick.

## What happens today (measured)

### The pipeline

Takedowns, chokes, finishers, NPC fatalities and the kill cam are "authored camera" states (FSM 13, 41, 42, 38, 55, 83, 33; mantle 8 and vault 9 share the path). They run as follows:

- **Position.** The eye position is the game's animated camera (the native eye midpoint, `dxgi_probe.cpp` ~5739) plus the player's live physical head offset (up to 0.45 m sideways, +0.3 m up, `dxgi_probe.cpp` ~5939). Height goes through the gameplay smoother (tau 0.12 s, lag cap 0.20 m, `anchor_smoother.hpp`). Nothing keeps the eye out of the victim or walls.
- **Rotation.** Rotation is the headset's. The game's pitch and roll are dropped, and its yaw is latched at entry; it re-latches only on a 30° jump (`authored_view.hpp`).
- **Recenter.** The G1 recenter eases the world in yaw so the animation's heading lands on the entry gaze (0.4 s smoothstep, 10° dead zone, `scripted_recenter.hpp`).
- **Comfort and arms.** The comfort vignette is off in these states except during that ease. Both arms play the game's animation (`takes_hands`).

### What the player actually saw (v636, 23 min of real play)

The figure `v636-gaze-vs-game-pitch.png` shows the game camera's pitch and the headset's pitch over three authored scenes.

- **2.4 s takedown.** The game pitched to −33…−41° to show the strike. You were looking +22° up at entry, so for about 1 s the strike sat 24–55° below your gaze. For the first ~0.3 s it was at the very edge of the ±55° view (render FOV ±54 × ±55°), then low in the periphery, far from the sharp centre.
- **Camera mode 14 dives** (five times; 1.1 m down in 0.25–0.34 s to −84°). These are **hard landings, not takedowns**:
  - The camera fell 6–14 m in the 1.5 s before each dive (`dxgi-camera-probe.csv`, e.g. z 57.1 → 42.9).
  - The dive matches the big-landing clip (`baseline_jumpland_big`: −82…−84°, 1.2 m dip).
  - You kept looking ahead, on average 54° above the camera. That is locomotion, outside this design (see Non-goals).
- **1.8 s takedown.** It was fine: your gaze stayed within 8° of the action.
- **The G1 recenter never turned.** All 29 entries fell inside its 10° dead zone: the animation's heading was already where you looked, so yaw was not the problem.
- **Height smoothing (simulated on the logged path).** It hit its 0.20 m lag cap in 21 of 29 scenes. Seven scenes ended about 0.2 m behind, so the exit reset makes the eye jump up to 20 cm in one frame.

### What the game's clips do (census of 269 clips, 0 decode failures)

The camera is rigid on the head pivot. All of its motion comes from the body (lunge, crouch, fall) and from head rotation. Assassinations, chokes and NPC fatalities have no separate camera layer. Values are median / max.

| Scene | Duration | Look-down | Peak turn rate | Peak camera speed |
|---|---|---|---|---|
| Standing assassination | 2.1 / 2.4 s | 28 / 44° | 294 / 454 °/s | 4.6 / 9.1 m/s, lunge and return |
| Drop assassination / takedown | 2.0 / 2.7 s | 79 / 89° | 411 / 750 °/s | 9.2 / 14 m/s, 2.1 m fall |
| Choke entry, then loop | 1.2 s, then 1.4 s per loop | 13° (loop ≤9°) | 241 °/s (loop ≤67) | 4.9 m/s (loop still) |
| Sword fatality (combat finisher) | 1.7 / 2.3 s | 21 / 69° | 291 / 671 °/s | 5.3 / 10 m/s |
| NPC kills the player | 4.7 / 5.2 s | 36 / 58° | 692 / 1098 °/s | ends on its side, on the floor |
| For scale: walking | — | 0° | 6 °/s | 0.3 m/s |

**The decisive geometry** (victim clips decoded and paired by their shared anchor, `plot_7_victim_head.png`):

- **Back assassination.** During the stab the victim's head is **0.24–0.40 m from the camera, at eye level**. The game's camera looks about 35° (31–37° median by victim type) away from it. In Corvo's clip that is mostly to the side, past the head; other variants pitch down by up to 44°.
- **Choke loop.** The victim's head is **0.23–0.25 m away**, 5–20° below eye level and 45–55° off the camera's look direction.

On a flat screen the head is a blur at the frame edge. In VR it is a large object 25–40 cm from your eyes, and it sits exactly where you look as soon as you turn toward the victim. At 0.3 m a head covers about 35° of view, and your hands and the blade are next to it or behind it. Combined with the gaze misses above, that is "I can't see what happens" and "the camera is placed strangely" at once.

The census is clip data in model space. The live camera can differ: the v636 2.4 s scene pitched to −41°, more than Corvo's back clip does. It is probably another variant or a finisher; the state was not logged, which D1 fixes.

### The kill cam is a different thing

From the executable's strings (not yet observed live):

- State 83 is a **projectile follow camera** (`arkCpntKillCamProjectile`, states SMOOTH_IN / FOLLOW / SMOOTH_OUT) with bent time.
- The game has its own setting for it: `ARK_GPSETTING_KILLCAM`, values None / Normal / Frequent, cvar `ark_killcam_chance_setting`.
- In VR the view would leave the player and fly with the bolt in slow motion.
- It is not in the v636 logs.

## Root causes

1. **Point-blank staging (lab: authored, not a cause).** The animation parks the eye 0.24–0.40 m from the victim's head. The takedown lab shows the flat game frames the same close-up: Row A has the victim's face filling the frame in the back stab and the choke. A level VR view from the animated eye looks almost the same. The close-up is the intended shot, not the problem.
2. **The gaze is not on the action.** The flat game aims your eyes; in VR nobody does, and nothing hints where to look. That covers the 24–55° misses in the 2.4 s takedown.
3. **The raw position path.** It has lunges at 5–9 m/s and returns, 1.1 m dives and 2 m falls, a 0.2 m smoother lag, and a pop at exit.
4. **No comfort layer in these scenes.** There is no vignette, and the live lean can push the eye into the victim.
   - **Your own empty collar** (lab): the eye sits at the game's head pivot over the headless body, so looking 25° down in the choke shows the inside of your own hood at the bottom of the view in 86% of frames. This needs an in-game eye capture to confirm (Phase 0).
5. **Special scenes have no policy:** the kill cam (projectile) and the player's own death (5 s, ends on the floor).

## What others do (summary; sources in the research notes)

- **Rotation is the player's.** Every source agrees: Oculus/Meta guidance, Doom VFR (glory kills dropped for exactly this), Fallout 4 VR (VATS camera removed), and both Dishonored VR mods. Pitch and roll are never applied. Keshavarz & Hecht 2011 found that pitch plus roll is much worse than pitch alone.
- **The rival D2 mod** (cosmictangerine, GPL, ideas only):
  - Rotation comes from the head.
  - Position follows the animated camera at **at most 4 m/s**. Their 2.5 m/s left the eye 0.7 m behind a lunge, and 4 m/s turned a 0.66 m entry pop into about 0.17 s.
  - Exit waits until the camera is within 1.5° and 2 cm, then glides 0.3 s.
  - It has no vignette and no kill-cam handling.
- **The D1 VR mod** (zlib):
  - It keeps the authored yaw and position, and strips pitch and roll.
  - It re-seats the game arm on the player's shoulder.
  - It turns the game's Kill Cam off by default.
  - It times drop takedowns for the player.
- **Comfort tools with evidence.**
  - A soft vignette driven by motion the head did not make: Fernandes & Feiner 2016; Teixeira & Palmisano 2021.
  - Blinks or snaps rather than long ramps for reorientation (Oculus).
  - Attention directed by sound, light and haptics, never by turning the view.
- **Long, camera-heavy shots** go to a window or screen in shipped games: Hellblade VR, RE4 VR, Village's 2D option, Sniper Elite VR's kill cam options.

## The field-of-view question

Widening the FOV in the headset does not work. The rendered FOV must equal the lens FOV; a wider one is **minification**:

- the world shrinks;
- head turns feel wrong;
- the vestibulo-ocular reflex can adapt and persist after the session.

Draper et al. 2001 measured more sickness at both 0.5× and 2× than at 1×. A wider FOV also cannot show what a 40° pitch-down hid.

What the user wants from it is to "see more of the action". The obvious substitute, moving the viewpoint back or up, was tested in the takedown lab and did not pay off (D3). What puts the strike in view is **the gaze**: looking about 25° down. D4 and D5 bring the gaze there. The FOV stays 1:1.

## Goals

- **G1 Readable.** In every takedown, choke and finisher the victim and your hands are in view. You see the strike without hunting for it. Target: the strike point within 35° of a 25°-down gaze in most action frames, and none of your own body (collar, neck, shoulder caps) in view.
- **G2 Comfortable.** No forced pitch, roll or FOV change. No position pops: entry and exit glide. Fast animated motion is eased and vignetted.
- **G3 Every kind of scene has a policy:**
  - standing, side, front and window assassinations;
  - chokes;
  - drop and slide takedowns;
  - sword finishers;
  - Bloodthirsty finishers;
  - the player's death;
  - the kill cam.
- **G4 Evidence first.** Telemetry and an offline lab show what the player sees before and after, as pictures, before any sim or headset test.

## Non-goals

- Mantle, vault, slide and landing cameras. These are accepted locomotion and stay as they are, including their gameplay z smoother.
  - Noted for later: the hard-landing camera (mode 14) dips the eye 1.2 m in 0.3 s on every big fall. It is a candidate for its own comfort change, if the user wants one.
- Cutscenes (mask 32). Their own design is `2026-10-25-camera-recenter-vr-cutscenes-design.md`.
- Replacing the game's takedown animations with player-driven physical kills. That is a possible later option, not this iteration.
- Turning the user's hands back on during takedowns. The arms keep the game's animation.

## Anti-goals (failures even if it technically works)

- The view tilting, rolling or zooming on its own.
- The eye inside the victim's head, a wall or the player's own body.
- A pop at entry or exit, or the view swinging back after the scene.
- A staged viewpoint that shows the headless neck or the inside of the arm models.
- Losing the strike in a fade. Fades are only for the player's own death and as an opt-in.
- A frame-rate cost in normal gameplay. Everything new runs only while an authored camera is active.

## Design

### D1 Evidence first (no behaviour change)

**Telemetry.** Add these per row to `native-authored-camera.csv` (authored frames only, every frame, with a higher cap):
- the FSM state index;
- the HMD orientation;
- the rendered eye position and forward;
- the victim's spawn id, head position, distance and angle from the gaze;
- the physical offset;
- the raw and smoothed eye;
- the vignette opacity and the time scale.

Also add a per-scene summary to the status JSON: state, duration, time the victim's head was nearer than 0.4 m, and time the victim was outside the view.

**Eye images.** Write 3 composited eye images per takedown (entry +0.3 s, middle, end) with the existing `build/final-eye-capture` path.

**Takedown camera lab** (offline, seconds, no game):
- Inputs:
  - the decoded player body and arm clips with root motion;
  - the paired victim clip (placed by the shared anchor);
  - a gaze model: level, −15°, or a recorded headset trace.
- It drives the same pure view helpers the DLL uses: today's policy and each candidate.
- It renders first-person frames: the player rig from the asset pack, the victim as its jointed skeleton with head and body capsules, and a side diagram.
- Metrics:
  - the victim head's distance and the angle it covers;
  - the angle of the strike point (hands, blade, victim neck) from the gaze;
  - eye speed, acceleration and jerk;
  - pops at entry and exit;
  - clearance.
- Families: back, front and side assassination (Corvo and Emily), choke in/loop/exit, drop assassination, sword fatality, NPC fatality.
- It replays the v636 headset trace, so the lab is checked against live data.
- Output: a contact sheet per family (Game framing / VR today / Proposed) to show the user.

**Victim spike (with a stop point).** The victim-aimed blink, the lean clearance and the look-down cue need the victim's head (and the strike point) every frame. Nothing reads it today:
- `takedown_target` gives only the entity origin at the feet;
- `living_bone` refuses dead or unconscious NPCs;
- the victim id exists only as the sensor offer (`takedown_spawn_id`, held 600 ms).

The spike settles two things.

*Identity*, latched once at scene entry, in this order:
1. the takedown sensor's offered victim;
2. otherwise the NPC the sword's contact hit in the last 300 ms (finishers);
3. otherwise the nearest NPC within 1.5 m in front of the eye.

*Head position per frame*, tried in this order:
1. the victim's animated model joints (the same palette path the arm upload reads for the player);
2. a keyframed or ragdoll body bone;
3. the entity origin plus the body type's head height.

The spike also records whether a synced animation's root motion moves the entity origin.

**Stop point.** If no per-frame source works through the stab:
- the victim-aimed blink uses the latched entry direction only;
- the look-down cue uses the right hand's attach joint (the native arm pose the mod already uploads) as the strike point;
- the lean clearance uses the entity origin plus head height.

When a scene has no victim at all, it runs D2 plus the vignette only, with the status reason `no_victim`.

**Identify the open cases in one sim run.**
- Which state plays the sword fatality (33?) and the Bloodthirsty kill.
- The kill cam: confirm state 83 and the projectile camera, and measure its time scale (bent time) and duration.

### D2 Stable eye (comfort)

**Lab result (2026-11-01).** A follow that lags behind the animated eye is harmful. With a 4 m/s critically damped follow, the eye lagged up to 0.12–0.30 m and cut the corners of the animated path. In the choke it ended up 14 cm from the victim's head, inside the hat (row B at contact). So the eye **stays on the animated camera**; there is no lagging smoother. Comfort for the fast lunges comes from the vignette (D5), not from smoothing.

- **On the path.** The eye is the animated camera plus the player's physical offset. Today's gameplay z smoother (0.20 m lag) is turned off in takedown-class states, for the same corner-cutting reason.
- **Entry and exit pops only.**
  - A real discontinuity is glided over at most 0.2 s from the last view; afterwards the eye is exactly on the path. Examples: the native entry pop (0.66 m in a choke) and today's 20 cm exit reset.
  - At exit, the scene view hands back once the gameplay eye is within 2 cm (at most 0.5 s).
- **Clearance against the lean only.** The live physical lean must not push the eye into the victim's head. The lean component toward the head is reduced (eased, the `carry_push` pattern) once the head is nearer than the animated eye's own distance minus 2 cm. The animation's own close-ups are kept: they are authored, see root cause 1.
- **Falls.** Drop takedowns keep the path; the vignette carries the comfort load.

### D3 "See more": no viewpoint shift; hide your own collar instead

The takedown lab (`tools/takedown_lab.py`, `artifacts/takedown-lab/2026-11-01/`) rendered five scenes with the real player meshes and the game's NPC mesh:
- Corvo back stab;
- Emily back stab;
- Corvo choke in + loop;
- Corvo front stab;
- a sword finisher.

Each scene was rendered from the flat game camera, from today's VR eye, and from four shifted viewpoints.

| Viewpoint | What the lab showed |
|---|---|
| Today's eye, looking 25° down | The strike point is within 35° of the gaze in 95% of the choke's action frames, 62% for the front stab, 24–43% for the back stabs and the finisher. Looking level gives 39%, 42%, 9–29% and 5%. **Where you look matters far more than where the eye sits.** |
| Up 12 cm | Often worse. The victim's hat fills the view at the choke contact, and the strike moves further down. |
| Back 15 cm | Best distance (head 0.28–0.69 m), but you see the inside of your own empty hood in 44–97% of frames and arm shoulder caps up to 33%. Rejected. |
| Over the shoulder (12 cm + 8 cm up), auto clearance | Small gains in distance; the strike lower or unchanged. |

Decision (lead, pending the user's look at the pictures): **no viewpoint shift.** Instead:
- **Hide your own body's collar and neck in takedown-class states.** The arms stay. Looking down 25° in the choke today shows your own empty hood in 86% of frames. This matches the user's cutscene choice ("hide the body, keep only the camera"). Phase 0 confirms the collar in an in-game eye capture before this is built.
- **Get the gaze to the action**: D4 (the yaw blink toward the victim) and D5 (a look-down cue).

### D4 Getting the action in front (yaw)

**User decision, 2026-11-01** (it supersedes the 2026-10-25 "gentle turn, not a fade" choice for takedown-class scenes; mantles keep G1): use the research's recommendation. **A re-aim is an instant snap hidden by a short blink:** out 60–80 ms, hold ≤ 50 ms, in 100–150 ms. It is not an eased world turn. Two further changes:

- **The target is the victim's head direction** (D1 victim spike), not the animation's heading. The entry alignment keeps G1's timing.
- **Re-aims are driven by the animation, never by the player's gaze.**
  - A re-aim fires only when the animation has moved the victim's bearing by 35° or more, measured in the scene frame (against the latched heading), and that bearing has settled (under 30°/s for 0.2 s).
  - Looking away on purpose, for example at a second guard during a choke loop, never turns the world. Rotation belongs to the player.
  - This replaces the native heading's 30° jump trigger, which made spinning clips (clockwork, window, Emily's 42–61° excursions) re-aim on the animation's schedule.
  - There are no re-aims inside a choke loop.

The entry alignment and each re-aim snap at the dark point of the blink. The world never visibly turns, which replaces today's 0.4 s ease with its 110–340°/s peaks. The 10° dead zone stays: a victim already in front gets no blink at all. At most one blink per 0.5 s.

### D5 Attention and comfort layer

- **Vignette in takedown-class scenes**, driven by the eye's motion that the head did not make: the animated eye's speed (the lab measured lunge peaks of 4.8–9.1 m/s). The existing ring is used, at the existing strength setting (default 0.6).
- Start values: zero at 1.4 m/s or less, full at 4 m/s, rise 0.1 s, fall 0.4 s after a 0.2 s hold.
- The cutscene path's `SceneMotion` / `cutscene_vignette_speed` are reused.
- **Entry cue:** a short light haptic on both controllers when the game takes the arms, so the player knows a takedown started and looks at their hands.
- **Look-down cue.** For the first ~0.6 s of a takedown, while the strike point is more than 35° from the gaze, the vignette's clear area is biased toward the strike point, so the eye is drawn there. In the lab, looking 25° down is what puts the strike in view. This is tried in the lab and then in-game, and it is dropped if it reads as a moving tunnel.

### D6 Special scenes

- **Kill cam (user decision, 2026-11-01: keep it).**
  - The game's kill cam stays on, at the game's own setting.
  - Its state-83 policy is **"stay in your body"**: the eye ignores the projectile camera's position and keeps the gameplay eye, head-owned, while the game's slow motion plays. You watch the bolt fly from where you stand. This is Fallout 4 VR's answer to VATS.
  - The game's own Kill Cam setting stays the way to turn it off. There is no new launcher row.
  - Following the bolt, or a window, are later options only if wanted.
- **Player's death (NPC fatality, 4–5 s).** Stable eye and vignette. Fade to black over 0.3 s once the strike lands (start: 1.2 s in), with audio continuing. The game's death screen follows.
- **Drop takedowns.** Stable eye, vignette during the fall. The player is already looking down at the target to get the drop prompt.
- **Theater for takedowns** (optional, later). The game's own framing on a world-locked window for the scene. It reuses the cutscene theater path, for players who want the exact cinematic or are sensitive.

### Per-scene policy

| Scene | Eye | Own collar (D3) | Yaw (D4) | Vignette | Special |
|---|---|---|---|---|---|
| Assassinations (13; standing, fast, side, window, slide, far reach, shadow walk, clockwork, wolfhound) | D2 | hidden | victim | yes | — |
| Choke (41, 42; in, loop, exit) | D2 | hidden | victim | yes | — |
| Drop assassination / takedown | D2 | hidden | head only | yes | — |
| Sword finisher, Bloodthirsty (33?, 55) | D2 | hidden | victim | yes | state confirmed in D1 |
| Player's death (38) | D2 | no | head only | yes | fade after strike |
| Kill cam (83) | gameplay eye (ignores the projectile camera) | no | head only | no | kept on (game setting); slow motion seen from your eyes |
| Mantle, vault, slide, hard landing (8, 9; modes 2, 3, 6, 14) | unchanged | no | unchanged (G1) | unchanged | non-goal |

## Plan

Each phase ends with its checks:
- **A:** unit checks in the build;
- **L:** the lab, with images sent to the user;
- **B:** the sim on the user's save, following the sim safety rules (Steam ActiveUser is the user's own account, save backup and diff, never over the user's own game);
- **C:** the user's headset.

| Phase | Work | Done when |
|---|---|---|
| 0 Evidence | D1 telemetry, eye captures, takedown lab, victim spike, identify the fatality state and the kill cam | A: telemetry columns. L: contact sheets of "Game / VR today" per family; v636 replay matches the logged view. B: one takedown from behind (guard 9258 recipe) with eye images and a victim head track; one crossbow kill with the kill-cam setting. |
| 1 Stable eye | D2 eye on the path, pop glides at entry and exit, lean clearance; D5 vignette and haptic | A: the eye equals the animated path outside the glides; no step over 2 cm per frame at entry and exit; lean clearance held; gameplay path unchanged. L: zero corner cutting (eye never nearer the victim's head than the animated eye), zero exit pops. B: same takedown, no pop, telemetry clean. |
| 2 Framing | D3 own collar hidden, D4 blink re-aim toward the victim, D5 look-down cue | L: no own-body intrusion in any family at level and 25°-down gazes; the look-down cue's renders approved by the user. A: blink timing, re-aim trigger (animation-driven only), at most one blink per 0.5 s. B: the collar is gone in an eye capture. |
| 3 Special scenes | D6 "stay in your body" kill cam policy, death fade | A: state 83 keeps the gameplay eye. B: a crossbow kill plays the game's slow motion with the view at the player; death fades. |
| 4 Headset | The user plays takedowns of each family | C: the checklist below. |

Headset checklist (Tier C):
1. Back, front and side kills, standing and crouched.
2. A choke, held through a loop. Look away at something else during the loop: the world must not turn.
3. A drop assassination.
4. A sword finisher in a fight.
5. Getting killed.
6. A crossbow kill at range that triggers the kill cam: slow motion, and the view stays with you.

For each, the user answers: did you see your hands and the victim without searching? Any lurch, pop or sickness? Does it look like a deliberate shot?

Knobs keep the old behaviour reachable for A/B: `build/legacy-takedown-eye`, `build/takedown-collar-visible`, and the existing `legacy-authored-camera`.

## Builder's calls made here

- These numbers are research-based starting values, to be tuned in the lab:
  - pop glide 0.2 s, exit hand-back within 2 cm or 0.5 s;
  - vignette 1.4–4 m/s;
  - victim cone ±35° for 0.15 s;
  - re-aim blink 60–80 ms out, ≤ 50 ms hold, 100–150 ms in, at most one per 0.5 s.
- No viewpoint shift (lab, D3). Hiding the own collar is one rule for all takedown-class states, not per-animation offsets. This follows the "no special-case patches" rule.
- Mantles, vaults, slides and hard landings are left exactly as they are.
- The takedown re-aim is a blink-snap (the user's 2026-11-01 choice); G1's gentle turn stays for mantles and vaults.
- Answers to the builder check:
  - Victim identity and head source: D1 victim spike, with an order of sources and a stop point.
  - Re-aims are animation-driven, measured in the scene frame, and never chase the gaze (D4).
  - Camera mode 14 is the hard landing, so it is out of scope; there is no body pick-up dive.
  - State 83 always follows "stay in your body"; the kill cam itself stays on (D6).

## User answers (2026-11-01)

1. **Instead of a wider FOV:** "show the lab". The lab (`artifacts/takedown-lab/2026-11-01/`) showed that shifting the viewpoint does not pay off. The lead's recommendation is no shift, the own collar hidden, and the gaze guided (D3). Awaiting the user's look at the pictures.
2. **Kill cam:** keep it, as slow motion seen from your own eyes (D6).
3. **Re-aim:** the recommended quick blink (D4).

## References

Research notes with sources:
- `artifacts/takedown-camera-research/web-research.md`: Oculus Best Practices; Meta locomotion and comfort pages; Draper et al. 2001; Keshavarz & Hecht 2011; Fernandes & Feiner 2016; Teixeira & Palmisano 2021; Serrano et al. 2017; the Doom VFR, Fallout 4 VR, Hellblade VR, RE4 VR, Sniper Elite VR and Arkham Shadow coverage; the cosmictangerine D2 VR and VR-Stereo-Hub Dishonored VR repositories.
- `artifacts/takedown-camera-research/code-map.md`: the code map with file:line references.
- `artifacts/takedown-camera-census/summary.md`: the clip census.
