# Camera, body, hands and weapons move as one (v861)

User, paired mode, after v819-v835: "the hands still jump off the body from camera to camera when I turn fast with the stick; the hands
lag behind the camera... it feels like the camera turns, then the body, then the hands... patches on patches".

## 1. The chain today, stage by stage

Indices: Present #N ends interval N-1; interval N runs until Present #N+1. S_N = view Setup in interval N, U_N = the tick's arm/body
skin uploads (0x25efe0, stage 7) in interval N, G = the render's GPU skin copy (0x25f930, UPDATE_NPC_DECALS pass).

| stage | where / thread | when | what it uses |
|---|---|---|---|
| runtime locate | Present hook, render thread | Present #N, for display at #N+2 | head + grips (sample s_N) |
| stick turn | physics step hook 0x1b4760, game job | tick N | add_yaw on the player heading |
| attach pass | 0x755030 -> 0x753c10, game job | tick N, after physics | writes arm frames (+0x1860), places held weapons from the arm palette of U_{N-1} (v834: 5712/5712 reads) |
| arm/body upload | 0x25efe0 hook, stage 7 job | tick N | solve of the last publication (Tracking_{N-1}) + v830 newer-sample refresh + v819/v832 root/heading carry |
| Setup | 0x374e40 -> prepare_head_view, render thread | interval N, after the eye begins of S_{N-1} | camera from anchor_N (BodyAnchor: camera position, smoothed z, entity heading minus head-follow) + s_N; publishes Tracking_N (clamp applied) |
| render of S_{N-1} | 0x3619d0 -> 0x361bc0 -> 0x361d60 (eye begin 0x351210, RenderSingleView), render thread + jobs | interval N | GPU skin copy G uploads whatever the CPU skin array holds then |
| hand clamp | prepare_head_view | Setup | sweeps posted at S_N, cast at Stage0 of tick N+1, read at S_{N+1}; a target off the answered path is HELD at its old world spot up to 0.25 s |

## 2. Lag sources, measured

| source | lab (tests/image_pose_check.cpp) | sim (frame-order trace) |
|---|---|---|
| solver memory in world space on rig turns (ChestYaw 40 deg dead zone + 0.3 s follow, feet, elbow poles) | hands off their targets 76 mm (stick 90 deg/s), 106 mm (45 deg snaps), 0 when still/running | TBD |
| skin drawn from a different publication than the camera (tick upload vs render order) | copy after the tick: 58-440 mm (one frame of run/turn); before: 0 | TBD |
| weapons placed from the previous upload | v834: every read one interval old | TBD |
| clamp hold on turns | 25-58 cm one-to-two-frame jumps of the pistol hand (v834 sim) | TBD |

### Timing: the pose sample's display time (owned by the pacing session)

Views and grips are located together at the Present for `predictedDisplayTime + period x 2` (the image shows two Presents later). In
the v835 headset log the runtime's display step flips between 11.1, 22.2 and 33.3 ms (15/71/13 %, a change on 45 of 100 frames), so
that horizon was off by up to 11-22 ms per frame. The compositor reprojects the head to the real display time, but not the hands, so a
moving hand shakes or trails by its own velocity times that error. paired-speed owns the fix: a locked app cadence (k x the runtime
base period) and `locate_display_time_ = predictedDisplayTime + paced_period() x 2` (v841+). This redesign takes whatever sample the
Setup used: head and both grips come from that one locate, and nothing re-predicts them afterwards (the v830 refresh is off with the
image skin).

## 3. Target design (what native VR games and the rival mod do)

- One sample per image: the Setup's runtime sample and camera anchor (the tracking space's world map) are the image's. Nothing else.
- A stick/snap turn rotates the tracking space (the rig): camera, body and hands turn in the same image. The body solver's memory
  turns with the rig (BodySolver::turn_rig); only physical head turns are followed with the dead zone.
- Placed right before the draws: the render's GPU skin copy (0x25f930) is the last moment before the image's draws. There the CPU skin
  array of the body and both arms is rewritten from the image's own solve (Driver::begin_image at the render's eye-0 begin;
  image_arm/image_body in the live model frame) through the engine's skin writer 0x25f200. The tick's upload stays for the engine's own
  readers. No carry, no newer-sample refresh, no prediction: the image's data is complete by then whatever order the engine ran in.
- Weapons and the clamp follow the same image (stage 2/3 below).

## 4. Stages

1. v861, body and arms:
   - `BodySolver::turn_rig(turn, pivot)` (body_ik.hpp) turns the solver's world-space memory (chest yaw, foot plants and yaws, head
     velocity, last eye, elbow poles, reach shoulder) with the rig. `Driver::solve_locked` applies the change of `Tracking::rig_yaw`
     (the world yaw of the calibrated forward as the Setup maps it) about `rig_pivot` (the camera anchor), and starts the substeps from
     the turned eye and hands. Physical head turns are not rig turns. Knob build/legacy-body-turn.
   - Image skin: `Driver::begin_image` at the eye-0 begin, `image_arm`/`image_body` in the GPU-copy hook (0x25f930), written through
     0x25f200. Only models whose last upload was the plain tracked palette (not hidden, contact or native). Knob build/legacy-tick-skin.
   - Lab tests/image_pose_check.cpp: still, run 5 m/s, stick 90/180 deg/s, 30/45 deg snaps, 45 and 90 Hz, copy before or after the
     tick: drawn joint on the image's solve 0.0000-0.0001 mm, hand on its target 0.0 mm (was 76-106 mm with turns), the old tick upload
     drawn after the tick 58-440 mm off.
2. Clamp (hand_clamp.hpp): a smooth turn's step carries the drawn hand about the root (known planes still push it out); a snap-sized
   rig turn (> 0.2 rad in one frame) re-acquires from the chest, because a hand carried into a wall no sweep met cannot see that wall
   from inside; on a frame that casts nothing the chest sweep stays owed. Lab tests/hand_clamp_check.cpp (all three cast modes): after
   a 45 deg snap the hand is at its turned target in the snap's own frame, an unmet wall is caught in 1-2 frames, it rests at the wall,
   no snap-back. Knob build/legacy-clamp-world-hold.
3. v862, weapons: the v830 newer-sample refresh is off with the image skin (it only served the tick's drawn upload); the v835 item
   advance starts from the image's own drawn joint (`Driver::image_world`) instead of the tick's upload.
   Literal same-frame wall casts on the render thread are not allowed (docs/native-world-trace.md: queries stay on the Stage0 scope).

## 5. Results

Engine order, measured (frame-order trace, v863 sim, ~11,400 gameplay intervals): `P X B0 K G1 G0 B1 U.. W.. S`, i.e. Present,
runtime sample, eye-0 begin, the render's GPU skin copies, eye-1 begin, then the tick's arm uploads, then the Setup. Every copy carried
the previous interval's upload (b - w = 1 in 21,129 of 21,131). So the old path drew a palette solved BEFORE its image's Setup: a
prediction (v830 refresh, v819/v832 carries). The image skin replaces that prediction with the image's own pose.

Sim A/B, v863, artifacts/claude/hand-sync-run30.json (still 3 s, walk 22 s, two head-yaw sweeps, 8 snap turns). A = build/legacy-tick-skin +
legacy-body-turn + legacy-clamp-world-hold (the v835 path), B = defaults. Error = drawn attachment joint vs the image's raw controller
target (the image's own camera). Traces: session scratchpad order863A/B.csv, analysis frame_order.py.

| | A old | B new |
|---|---|---|
| right hand, within 60 ms of a turn: p50 / p90 / max | 5.4 / 488 / 1116 mm | 10.7 / 240 / 489 mm |
| right hand, elsewhere: p90 / p99 | 0.9 / 111 mm | 0.4 / 137 mm |
| arm skins rewritten from the image (left / right / body) | - | 9,964 / 1,548 / 0 of 11,326 |
| left pistol: weapon origin to drawn hand, deviation p50 / p90 / p99 | (left hand empty in run A) | 0.25 / 8.3 / 31.7 mm |
| frame interval p50 (sim) | 8.4 ms | 5.6 ms (other sessions' pacing changes are in both) |

The left hand held the pistol in run B but not in A (game state), so its error is not compared.

Open, in order of size:
1. The right (empty) hand's upload is mostly the full-hand-contact "raw reach" palette (86% of images), which v863 does not rewrite: its
   near-turn error is mostly from those images (219 of 550 rewritten). Rewrite it from the image's raw solve (Output::raw_arm) unless a
   contact receipt rebased it.
2. The body model is never rewritten (0 writes, 2 misses): find how its GPU copy differs (is 0x25f930 called for it with the same pointer?).
3. Weapons stay tick-placed (advance from the image hand): move them at the image's begin or answer their reads from the image.
4. Snap re-acquire from the chest draws the turned target for 1-2 frames before an unmet wall is known (lab); a pre-turn look-ahead sweep
   would make it same-frame.

v864 (build/d2vr_render_probe_arm_v864.dll, build/v864src = v863src + only these hunks; lab only, not sim-run yet):
- The upload of each slot is classified (status arm_flicker.image_skin.kinds per slot: [kept, tracked, contact raw]); the contact raw
  palette (the right empty hand) is now rewritten from the image's raw solve too (Driver::image_arm(..., raw)). A contact receipt's rebased
  palette is still the upload's. The kinds counters will show why the body is never rewritten.
- Weapons (opt-in build/weapon-image-place until a sim shows the render reads the model frame then): at the eye-0 begin, each held
  weapon's model frame is moved by the change from the hand its attach read used (recorded in advance_attachment) to the image's hand
  (weapon_image_place.hpp; tests/weapon_image_place_check.cpp: exact grip offset, no compounding without a tick, a 1 m jump left alone).
- Sim (v864 B/C, same script): both arms rewritten in every image (15,460 of 15,512 copies; right arm uploads 7,353 contact raw vs
  418 tracked). The rewrite moves the right hand's joint against the tick's own raw palette by p50 0.0000, p90 1.5 mm, p99 25 mm (only
  while moving), so the contact system and the drawn hand agree. The body's copy is hooked every frame (7,755) but its slot reads
  "keep" at copy time (kinds: 7,799 tracked uploads, 1,882 kept: a second, non-tracked body upload lands after ours); not fixed. The
  weapon knob ran only 58 times (the hands held no weapon in most frames): not verified, stays opt-in.
- Launcher: C:\Users\hollo\Downloads\DishonoredVRLauncher-v864.exe (manifest artifacts/claude/pose-redesign/repair-v864-test-candidate.json).
- User verdict on v863 (headset): "much better overall; performance finally in place; the hands turn the way I expected; much smoother".
  Open from that test (weapons/timing, not this pose path): the muzzle flash comes from the original pistol, not the copy that fired;
  the pistol equips about 0.5 s late (the 0.25 m cap fired 5 times in 13,608 reads, so not that); after climbing the hand appears
  before the pistol.

Candidate: C:\Users\hollo\Downloads\DishonoredVRLauncher-v863.exe (manifest artifacts/claude/pose-redesign/repair-v863-test-candidate.json,
built from a staging RepoRoot holding only that manifest).
