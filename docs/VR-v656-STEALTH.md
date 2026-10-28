# v656 stealth contact candidate

Any deliberate sword contact on a guard the engine currently confirms is unaware now applies lethal damage immediately. Contact with the back, torso, arms or legs uses the same path as contact near the neck. The default path no longer requires the native neck offer, a second facing calculation, or the legacy 0.6 m/s threshold. The existing 0.3 m/s admission threshold still rejects a resting blade.

Candidate launcher: `artifacts/launcher-stealth-v656/DishonoredVRLauncher.exe`. It embeds `d2vr_render_probe_arm_v656.dll`, SHA256 `9461944ff7c1b5be6bd53ad596a8370137729a4f8d734786d2c48195b4476005`. No game was launched for this change.

## Latest logs and diagnosis

The October 28 v655 session, PID 17312, was read from `%LOCALAPPDATA%/DishonoredVR/artifacts`. Its status, combat CSV and proxy log are preserved under `artifacts/stealth-contact-20261028/baseline-v655`.

- Twelve contacts reached the queue: four leg, five torso, three head. Eleven damage calls ran; the last returned ordinary 10 damage.
- Thirteen native attack presses produced two observed native assassinations and ten unhonoured presses.
- No contact passed the old combined damageable/unaware branch. That branch did not distinguish a guard being aware from the awareness check failing.
- Six contacts also failed the extra rear/side test. The previous report incorrectly made that the primary diagnosis without accounting for the missing awareness evidence.

These logs establish body contact and ordinary damage, but do not establish the guards' awareness before the swing. The patch removes the verified contact-policy dependencies; it does not pretend this session proves why every guard failed the native awareness predicate.

## Implementation

The game-thread consumer checks the existing exact player, equipment, pose epoch, victim registry and scene leases. For a damageable guard, a successful current native unaware result selects 1000 damage through the existing sword damage constructor, TakeDamage and already-hit-list ABI. This happens in the contact's own pass, avoiding the former 700 ms neck-offer wait and single pending-victim slot. A kill counter increments only after a successful call and observation of the victim's dead flag.

Aware guards retain ordinary combat; calm contacts on them remain non-damaging. Unknown awareness skips the contact and records the failure. Choke reservations and active native takedowns retain their existing ownership. Locomotion and turning are untouched. Legacy contact mode remains available; its held payload is now copied before servicing can destroy the optional.

`sword_contact.contact_policy` identifies the route. `sword_contact.awareness` is `[unavailable, aware, unaware]`; `stealth_reject[2]` now records aware results on the default route. `stealth[0]` counts qualified contacts, including immediate default strikes; `stealth[2]` records confirmed kills. The report distinguishes missing old awareness evidence from a new unavailable native result.

The installed Steam executable and the archived pinned PE both match the awareness function's prologue at RVA `0x5bf560`. Existing local decompilation and independent disassembly confirm its one-NPC-argument, boolean-result ABI. Public gameplay context: [Bethesda's Dishonored 2 launch guidance](https://bethesda.net/en-US/news/dishonored-2-available-now) explains enemy facing and field of view at close range; it does not document the private native ABI.

## Verification and acceptance

Focused MSVC checks passed for the extracted production default decision, contact admission and ownership, sword combat routing, contact policy, native ownership and awareness failures, attack intent, VR combat and stealth reports. The existing attack-intent source assertions were updated for the current left-hand-use guard and new contact policy.

The DLL-only `/W4` build passed and its source archive was unchanged during compilation. The full strict build was attempted and stopped at the existing `/WX` C4702 in `src/dlss_mode.hpp:163`. The separate build-exit-status check also fails an existing missing signed-failure guard at `tools/build-msvc.cmd:10`, present in the v655 source archive. Proxy verification passed. The actual packaged launcher installed into an isolated empty folder, verified the embedded v656 hash, and reported `game_launched: false`.

Evidence is under `artifacts/stealth-contact-20261028`; source identity is in `build/d2vr_render_probe_arm_v656.dll.source.json`. Later edits in other working-tree lanes are not part of that archived DLL.

Headset acceptance is pending. At the same checkpoint, crouch behind a fresh unaware guard and deliberately strike the back, lower torso and leg without looking at the neck. Each first contact should kill. Repeat with a slow deliberate stab and a slash; then check ordinary combat against an already alerted guard and the existing nonlethal choke. A resting blade must not kill. If a miss remains, the new awareness counters determine whether the engine answered aware or the query failed; an old awareness result is never reused to kill a guard who has since noticed the player.
