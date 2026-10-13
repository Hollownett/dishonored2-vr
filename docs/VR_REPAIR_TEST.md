# Experimental VR repair test

v220 failed headset acceptance. Select the new candidate explicitly with `artifacts/repair-v221-test-candidate.json`. Normal demo selection and older numbered builds are preserved. `docs/VR_REPAIR_v221.md` records changes, evidence and unresolved issues.

v221 repairs calibration geometry/palm-span fitting, gait wall time, stale solver output and fresh full-arm fallback outside authored ownership. Material refresh retries and physics discovery are bounded. Wrist health/ammo physical size is halved once, native gun reticle/wheel movie edits are exact-hash guarded, and the redundant blue pointer is removed while native cursor input remains. The opt-in observer records raw XR publication and the final palette passed to the owned consumer.

All 43 private-desktop checks passed with rendered Corvo/Emily replays at `artifacts/playground/runs/20261011-001257-4310fe0f/results.json`. The candidate manifest records full build/source provenance. Native-asset VR Settings, guard-carry right-eye visibility and sustained live performance remain unresolved. Reticle/glyph patch counters must demonstrate an owned movie load before claiming their live appearance changed.

PID38936 was revalidated with v220 stopped and body restoration pending. Exit that game normally before testing; do not force unload or bypass its retained restoration hook. A fresh game also isolates native movie lifetimes. The launcher still checks every loaded diagnostic for active/pending state. Never use F5/F9 or shared saves as an automated shortcut.

## Preserved v220 implementation history

Earlier repairs include optional wrist-HUD failure containment, cached typed wrist targets and nonblocking timeout handling; measured calibration into the active full-body solver; native palm/controller mapping; owned body visibility restoration; authored carry/arm attachment ownership; and captured native UI fallback tied to actual per-eye/wrist presentation. FSR4 caches unchanged per-slot constant inputs. These changes preserve the prepared body assets, accepted locomotion/turning and saved visual preferences.

Native guard/object eligibility now measures range from the selected tracked hand while retaining the game's eligibility and obstruction checks. Prompt captures carry target identity; markers use a matching anchor/frame/pose epoch. Full wrist panels fit both eye frusta. The verified MasterHUD alone receives compact layout, leaving the fullscreen damage mask outside that transform. The body Driver and arm tracker share the same native knuckle landmarks. Surface growth, XR recovery and shutdown retire app GPU work before destroying runtime images.

Offline checks establish selected math, resource/state ownership, alpha fallback and provider dispatch behaviour. They do not establish headset alignment, complete gameplay, comfort, guard carry visibility or repaired frame pacing. Sustained 10-FPS stalls and right-eye obstruction while carrying a guard need direct headset evaluation. No measured game speedup is claimed.

The required integrated build passed with warnings treated as errors and unchanged archived sources; its log is `artifacts/playground/build-v220-final.log`. The playground completed 36 private-desktop checks with rendered Corvo/Emily replay sheets, followed by four focused final rig/wrist checks after the landmark correction. Twelve runner checks and six capture-tool checks passed. A 20-second copied native startup held all 133 sampled guards at 640x480 with shared saves and installed files unchanged; that startup neither loaded a save nor tested native gameplay. Two further native rig fixtures are now in the fast manifest.

The surface lifetime repair follows the [OpenXR swapchain rules](https://raw.githubusercontent.com/KhronosGroup/OpenXR-Docs/main/specification/sources/chapters/rendering.adoc). Native engine ownership and offsets were checked against the pinned executable; Dishonored 1 VR supplied behavior and lifecycle references.

## Start and stop

Connect the headset and enter gameplay for initial startup. Use the existing save and familiar scene.

```powershell
.\tools\run_repair_test.ps1 -CandidateManifest artifacts/repair-v221-test-candidate.json -Launch -Direct -Seconds 120 -SynchronizedDiagnostics
# For an already-running game:
.\tools\run_repair_test.ps1 -CandidateManifest artifacts/repair-v221-test-candidate.json -GamePid 12345 -SynchronizedDiagnostics
# Skip the existing headset/game preflight waits only when already ready:
.\tools\run_repair_test.ps1 -CandidateManifest artifacts/repair-v221-test-candidate.json -Direct -GamePid 12345 -SynchronizedDiagnostics
# A bounded first pass:
.\tools\run_repair_test.ps1 -CandidateManifest artifacts/repair-v221-test-candidate.json -GamePid 12345 -Seconds 120 -SynchronizedDiagnostics
# From another terminal, request stop of the exact loaded candidate:
.\tools\run_repair_test.ps1 -CandidateManifest artifacts/repair-v221-test-candidate.json -Stop -GamePid 12345
```

Ctrl+C also requests stop through the ordinary launcher. Stop while paused should end VR projection/input without requiring a game restart. Body material restoration can remain deferred until native gameplay resumes and supplies its next owned body draw. Resume briefly, then verify restoration before another start. A stop request is not proof that every native state restored; inspect the archived result. Start remains blocked while restoration is pending.

## Staged headset checks

1. Start with stationary/walking performance under saved visual preferences. Separate captures, menus/loading and authored cameras. CPU spans and submission cadence are not measured headset FPS. Abort the wider pass on sustained severe slowdown or blackout and preserve that event.
2. Calibrate with both grips and natural sway. Record progress/reset reasons and acceptance tick. Check both hands, wrist rotations, overhead/cross-body reaches, chest/legs, walking/turning and crouching. Correlate controllers with actual submitted palms. Native consumer frame receipt and floor remain unverified. Abort on broken placement/disappearing geometry.
3. Test coins, container loot, movable objects and supported doors/usables with each hand. Compare intended cue with acquired identity, native eligibility/LOS and blocked targets. Complete prompts must stay at the correct target without persistent flat duplicates. Physics hover is not native Use permission.
4. Test guard pickup/carry/turn/drop/choke/kill. Distinguish awareness from action prompts. Check LEFT and RIGHT eyes independently during carry; abort immediately on complete right-eye obstruction. Preserve accepted authored combat.
5. Check half-size wrist health/mana/ammo inside both usable lens areas, gun reticle, world objectives, wheel VR glyphs and one effective cursor. Inspect native movie patch/rejection counts. Native VR Settings remains an implementation gap. Check heavy-damage borders, pause/load/focus and HUD blackout recovery.
6. Stop from pause, resume briefly to discharge owned body restoration and inspect the archived result before restarting. Extend to ordinary gameplay/checkpoint load only after earlier stages pass. Never overwrite a save for testing.

## Capture evidence

```powershell
python tools/request_vr_capture.py --request wrist-check --wait 30
```

This requests four source/destination projection images from a fresh tracked eye pair and converts completed raw RGBA files to PNG. These are **before compositor processing**: wrist and other separate XR quad layers are excluded. They cannot prove lens visibility or complete headset presentation. Runtime submission is explicitly unverified; source and destination pixels may differ because native object artwork is composited into the destination. Record the headset observation alongside the frame/pose identity and archived run.


## Explicit candidate and bounded diagnostic helpers

The default remains v220 for command compatibility. Select the new numbered candidate explicitly; a fresh build alone does not change selection. The wrapper verifies the candidate DLL and pinned game hashes. The helpers also verify the exact running PID, native loaded module path/base, loaded identity and candidate hash. They never launch, inject or control gameplay. The first game hash check reads the pinned executable; later module checks are bounded and run at most once per ten seconds.

```powershell
.\tools\run_repair_test.ps1 -CandidateManifest artifacts/repair-v221-test-candidate.json -Launch -Direct -SynchronizedDiagnostics
# Stop the selected candidate, using the actual PID reported by launch:
.\tools\run_repair_test.ps1 -CandidateManifest artifacts/repair-v221-test-candidate.json -Stop -GamePid 12345
```

`-SynchronizedDiagnostics` temporarily enables `build/synchronized-diagnostics` and restores its previous contents or absence when the launcher returns. The observer writes `artifacts/synchronized-diagnostics.jsonl`; the recorder archives it with selected candidate and loaded identity. Its ring holds 128 records, producers use try-lock publication and 10 Hz per owned body/arm kind, and worker-only serialization stops at 64 MiB. Inspect drops/limit flags. `publication_consistent` checks raw/solver publication; `consumer_frame_verified=false` leaves actual native same-frame receipt unproved. Floor, culling and lens visibility are not inferred. This is not complete scene replay or headset-compositor capture.

Run helpers from the workspace root after a fresh active selected session is ready. Use fresh output directories; they refuse to overwrite an existing recording. Both helpers set their own Windows process priority to Idle. The following hidden, bounded examples request no projection images:

```powershell
$taskStamp=Get-Date -Format yyyyMMdd-HHmmss
$taskRecording="artifacts/live-v221-review/recording-$taskStamp"
$taskPerformance="artifacts/live-v221-review/performance-$taskStamp"
Start-Process -FilePath python -WindowStyle Hidden -ArgumentList @('tools/record_live_session.py','--pid','12345','--candidate-manifest','artifacts/repair-v221-test-candidate.json','--seconds','300','--output',$taskRecording)
Start-Process -FilePath python -WindowStyle Hidden -ArgumentList @('tools/analyze_live_performance.py','--pid','12345','--candidate-manifest','artifacts/repair-v221-test-candidate.json','--seconds','300','--output',$taskPerformance)
# Owned stop requests; no game stop or process kill:
New-Item -ItemType File -Path "$taskRecording/stop.request"
New-Item -ItemType File -Path "$taskPerformance/stop.request"
```

An owned `stop.request` produces `stopped_by_owner`, a recorder final diagnostic snapshot and a final analyzer report. Ctrl+C also finalizes. Helpers stop on process exit, candidate/PID change, a matching completed stop result, XR stopping/exiting state, or 15 seconds of stale/non-progressing status (`--stale-seconds` accepts 2–120). They do not append repeated unchanged status as fresh samples. A paused game that stops publishing status can therefore stop a helper; start a new output directory after resuming.

Recorder limits remain 12 capture attempts, 128 MiB per source file, 512 MiB diagnostic copies, 1536 MiB raw projection budget and 64 MiB sampled status; each image is limited to 16 million pixels to bound PNG conversion memory. Projection capture is opt-in via `--projection-interval`; readbacks perturb timing. Analyzer reads at most the latest 1 MiB from each timing chunk, streams an at-most-8 MiB recovery log and stops at 64 MiB history. A long window can have less coverage than requested when the bounded tail is shorter; inspect `observed_window_seconds`.

Targeted CPU-only ownership checks:

```powershell
python tests/live_performance_check.py
python tests/live_session_tools_check.py
```

`sample_threads.py` now declares Win64 handles/pointers explicitly, defers Ctrl+C across suspension, resumes in `finally`, and closes thread/process/snapshot handles on failure. Its bounded duration is at most 30 seconds. It still actively suspends threads and can perturb the slowdown being investigated; it has not been run against the live game as part of these checks. Prefer the synchronized observer for initial diagnosis.
