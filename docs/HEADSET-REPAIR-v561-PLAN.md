# v561 combined repair candidate

Status: integrated; the complete required build and unchanged-source gate pass. Candidate is `artifacts/repair-v561-test-candidate.json`, DLL SHA256 `6bb3bff1b348ce8ce96a3d6d87389de3dd4c9115b6bb7a2a9624d956ebd7ff7b`. No v561 native gameplay, headset acceptance or performance improvement is claimed.

## Scope

One root-owned candidate combines the separately reviewed carry and diagnostic repairs. Preserve v559's accepted pistol stow/draw, locomotion, turning, perspective and stereo audio. Do not overwrite v559 or v560, change native stealth eligibility, lower image quality, or enable unqualified native hand actors/constraints.

- Pair each exported palm with its actual solved grip, root mapping, source clock, hand callback and epochs. Transfer that reach residual to the current grip instead of subtracting a previous palm from a new grip. Preserve the existing arm-solve/native-frame latch and weapon socket state machines.
- Serialize mod-held target publication and raw placement commit with a distinct target revision, so an older mod worker cannot overwrite a newer mod target. Preserve same-hold completion serials, typed49/50 ownership, gun/crossbow admission, fresh throw behavior and original native calls. This does not serialize independent native writers or establish the oil tank's terminal render cause.
- Correct the passive native13 receipt requester's32-bit copy width; unrelated upper padding must not produce a false accepted-copy rejection. This is not a stealth eligibility or damage change.
- Expose bounded versioned metadata for the existing GPU query-readiness checks, including the first pending/error stage, exact HRESULT and frame/HUD identities. Export final cached records and handle counter resets. No extra GPU query, flush, wait, ring growth, query reuse or changed retirement policy is permitted. The patch identifies the profiler blocker; it does not yet repair an unknown driver/query cause.

## Required gates

Keep failed attempts and their logs. Require strict MSVC regression checks on actual production implementations, including per-arm root/scale/publication pairing, native-frame latch, raw placement interleavings and SEH unlock, typed held ownership, left sockets and hip visibility. Compare deterministic accepted arm replay output before/after. Register the new palm and extracted readiness fixtures in the complete build.

Root integrates the tested artifact packages, freezes all compiler/tool/external-fixture inputs, then runs the complete required build under unused `d2vr_render_probe_arm_v561.dll`. Candidate metadata is published only after final success, matching DLL/source hashes and unchanged inputs. Compile and simulated fixtures are not headset acceptance.

## Real-game checks

The previous profiler game PID48316 has stopped core VR hooks, but its final native report retains deferred body restoration. Do not inject another candidate into that process. A subsequent test requires a fresh Steam process, the reviewed candidate and direct `-SkipPreflightWait` launch; do not add a headset preflight. Preserve required OpenXR recovery. Never swap retail saves or describe a read-only backup as Steam/cloud isolation.

1. Confirm exactly one correctly placed pistol/crossbow through wheel selection, hip stow/draw, ordinary object carry and corpse carry, with no acquisition by unrelated object grip.
2. Hold the oil tank while standing still, then move/turn and move the hand. Check stable placement, release/drop/throw, both-hand acquisition rules and left-hand visibility. Capture actual typed object/fullspawn, target revision and final render identity where supported; empty-hand screenshots are not tank proof.
3. Recheck trigger-independent sword damage and native-hint-only nonlethal grip Choke, including after menu/focus transitions. Native behind-guard assassination completion remains separately unaccepted.
4. Keep functional screenshots/recording separate from detailed timing. At unchanged Native100/2688x2880, start the profiler from activation and capture a short bounded readiness-diagnostic window first. Require actual duration rows before claiming HUD-versus-FSR synchronization attribution. Preserve quota/loss/counter metadata and query latency.

The carry changes make this a new workload; its timing is not automatically a matched A/B against v560. Headset refresh/ASW, compositor delivery, sustained90FPS, full native hand physics, Shadow Walk and overall comfort remain separate acceptance requirements. A default-off gate for diagnostic-only HUD proof dumps is deferred, not silently included as a claimed steady-state performance fix.
