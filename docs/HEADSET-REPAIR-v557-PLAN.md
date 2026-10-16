# v557: physical sword impact and weapon-held left hand

## Current authority and preserved behavior

The human finished the v556 test in PID51156 and requested the next build. The reviewed v556 DLL is SHA25643d9243e59da5f13df470f0c1bd4d6b9eaf6a079933bdcd66713f5126dca37ed. Root stopped VR through its owned stop export; the launcher returned diagnostic_exit0 and archived the run at artifacts/runs/20261015T164626.470310Z-pid51156-arm-v556. The game was then closed normally. No save restoration, Steam backend change or cloud-isolation claim is authorized by this closeout.

Preserve accepted locomotion/turning, exact-native-hint nonlethal grip choking, both-hand object grabbing, Y-to-Talk, wheel equip protocol, hip/wheel-only manual weapon draw authority, existing saves and prior numbered builds. The current run remains a rejected visual/combat candidate, not a release baseline.

## Requirements and implementation stages

1. A sword physically held by the player must damage an NPC on a genuine blade impact, independently of physical trigger/button state. Internal native attack pulses may remain for native animation/AI events; they must not be mistaken for requiring the user's trigger. Preserve verified equipped sword ownership, real contact and motion qualification, native thread scope, target generation and active nonlethal scene safety. Do not make resting a blade or tracking jumps an unlimited damage stream.
2. The isolated sword patch removes the generic400ms grab delay from a genuinely held/equipped sword only and prevents a raw-trigger consume latch from erasing the scheduler's internally generated attack pulse. Body carry and ordinary prop guards remain independent. The production-routing regression must preserve carried-body throws after their existing guard expires and existing native drop assist.
3. The physical contact route still has a separate global remembered-takedown veto and unowned/stale queued requests. Replace offer-based suppression with actual, stable native authored-action ownership checked on Stage0. Capture and revalidate exact player lease, sword equipment/component, full victim spawn, tracking epoch and bounded contact age. An offered or remembered hint alone must not disable sword impact. Keep actively claimed nonlethal revision cancellation. Do not invent an assassination victim offset or clear native hit lists as a shortcut.
4. Existing melee_applied means native invocation returned without exception, not proven damage. Independently verify the native DamageResult layout before exposing any positive-damage metric. Keep zero/ignored damage distinct from a failed request and preserve engine hit-list semantics unless evidence proves a necessary change.
5. The user confirms the left-hand offset while holding Gun or Crossbow. Ordinary wrist math did not change between v555 and v556; a guessed global offset is not justified. The new socket lease can cause the entire arm palette to fall back to native. Existing pose counters increment before final revocation and cannot prove what was submitted. Add bounded final-consumer diagnostics for actually tracked/native submission, frozen token/epochs/age, wrist/palm/joint59 transforms and retained-reader outcome. Fix a demonstrated ownership or basis defect only from that terminal evidence.

## Offline gates before one combined candidate

- Merge isolated patches only after baseline hashes and non-overlapping source hunks are reviewed. Root owns the combined build/export; helpers do not build competing DLLs.
- Run actual production-input regression for fresh grip with physical RT0 and held RT, trigger-only/no motion, pulse/block separation, contact ownership/generation/epoch/age, active scripted/nonlethal exclusion, unrelated hint acceptance, body throw and both-hand prop isolation.
- Run terminal observer serialization/rate/lock-order fixtures and existing socket/attachment owner checks. Static source guards supplement, not replace, composed tests.
- Run strict DXGI compilation and the complete required build in a frozen source copy, proving all compiler inputs and external fixtures unchanged before export. Preserve failed attempts and do not relabel an untested source patch as a built repair.
- Export the next available single numbered candidate with exact DLL/PDB/source provenance only after all gates pass. Do not automatically launch while offline build work is underway.

## Recorded evidence and performance boundary

The completed v556 session is retained under artifacts/vr-v556-test-20261015-restart2. Its low-resolution silent desktop mirror is not a headset compositor or audio recording. Capture intervals contaminate clean maximum-performance comparisons. Native command-list GPU timing was genuinely enabled and advancing with no query failures in observed samples; FSR timestamps use a separate queue clock and cannot simply be summed with native elapsed time. Saved output stayed1.2/2534x2765. No clean100-percent benchmark was performed and no graphics quality setting was lowered.

The retained native combat CSV records52 blade-contact events and23 producer contact-hit logs, but the final native invocation counter is2 with zero recorded native rejections/faults. The CSV has no embedded PID and is attributed through the owned run/ticks; it does not identify every producer/queue suppression reason or prove HP damage. The existing full-palette diagnostic cap stopped at tick68034359, before the confirmed Crossbow status at68121953. The next diagnostic worker therefore keeps the64MiB full-palette budget and continues compact left-terminal/reader observations in a separate64MiB budget, with exact physical-byte accounting and explicit lossy/cap/failure status. Compact observations never extend full-palette coverage or prove visual alignment.

The performance chat has a separate direct human request for exclusive HUD extraction versus FSR synchronization measurements at100-percent resolution without image-quality reduction. It may prepare isolated instrumentation and read-only research, not edit shared source or control the fresh flat-game process. Root reviews any ready contribution before the combined freeze; do not hold the critical sword repair for an unfinished instrumentation project. The next clean baseline excludes all compiler/video/readback windows and changes no live flat-game setting.

## Real-headset acceptance still required

Repeated actual guard damage with physical RT0, RT held for Block and other buttons; immediate swing after drawing/gripping; native active choke remains nonlethal; repeated same-target impacts after native attack cancellation; no delayed hit after target replacement/reload/menu; accepted body throws, grabs, doors and movement remain intact. Compare left gun/crossbow hand against tracked controller at rest and during motion, with ordinary use and right-hand corpse/prop carry. Correlate the terminal diagnostic token to the same recorded failure instead of accepting upload counters.

Full native physical hand actors/constraints, Shadow Walk attack/turning acceptance, large tank hand alignment/drop, audio, comfort, subtle weapon smoothing and sustained clean headset performance remain explicit unfinished scope. Query-driven palm/finger collision is not full native physical hand tracking. A narrow sword repair does not complete the full VR goal.

## Built candidate

The reviewed sword-contact and terminal-hand diagnostic changes are now merged. The complete required v557 build PASSED, including the existing stop/restoration fixture, new button-independent contact/ownership/control-loss checks, observer/budget/parser checks and existing standalone GPU correctness fixtures. All769 compiler inputs, the reviewed root checkout and500 pinned external fixtures remained unchanged. Evidence: artifacts/combined-v557-build-20261015/combined-build-result.json and full-build.log; independent review and corrected findings: integration-review.json.

Exported build/d2vr_render_probe_arm_v557.dll with matching PDB/source pointer, SHA256625cb969883bdc9417d8647bfacdf049e57724a3d0306e3447547263116f30b6. The human-test manifest is artifacts/repair-v557-test-candidate.json. It is NOT launched or headset-accepted. The fresh flat-game process45072 was not injected, restarted, closed or reconfigured by this build. Root's full build ran approximately17:08:49..17:15:35UTC; performance comparisons must exclude that validation interval and the separately coordinated follow-up instrumentation checks.

The left-hand offset is not declared repaired: the build adds the terminal evidence necessary to distinguish a native fallback from a tracked pose/socket mismatch. Physical sword impact routing is repaired in source and covered by offline regressions, but repeated actual native guard damage with varied buttons still requires the headset acceptance test.
