# v656 stealth failure: evidence and next repair gate

The user reported that stealth takedowns still failed. v656 is **not accepted as a stealth fix**. The October 28 session loaded the installed v656 DLL, PID 12844. No new game session or candidate was launched during this investigation.

## What this run establishes

The status, combat CSV, proxy log and per-target analysis are archived at `artifacts/stealth-contact-20261028/rejected-v656-pid12844`. The user continued playing during investigation; the initial inspection and later archive represent different times in the same session. The counts below describe the initial inspection, not the full final session.

- Fifteen sword contacts reached the consumer without capture, epoch, queue, ownership or nonlethal rejection.
- Eleven contacts were on living NPCs. All eleven native awareness queries completed and answered false: `awareness = [0,11,0]`. None entered the lethal branch.
- The other four were contacts on an already-dead first NPC, as independently established by the combat CSV's dead flag. Their not-damageable classification does not identify another live stealth defect.
- Three calm contacts were dropped; twelve native invocations included a corpse path. The last returned 10 damage.
- Contacts covered legs, torso and head: `[4,5,6]`. The failure is upstream of lethal damage dispatch, not a neck-only collision restriction.
- Two native attack presses were observed, both unhonoured. The aggregate kill-sensor and global-offer counts cannot establish an accepted stealth offer for the contacted NPC on the relevant frame.

First contacts by victim:

| Victim | First tick | Live contacts | Dead contacts |
| --- | ---: | ---: | ---: |
| `0x20cf8c90ab0` | 102077828 | 4 | 4 |
| `0x20d0790aba0` | 102128281 | 4 | 0 |
| `0x20d079134e0` | 102161578 | 3 | 0 |

The prior change removed body-part/facing/native-offer dependencies, but still required the generic native unaware predicate to succeed. It therefore could not fix these failed contacts. Its earlier description as a fix was premature. Later in the same live session the counters reached 39 contacts, awareness `[0,18,4]`, and four confirmed kills from four qualified contacts. This confirms that the damage path worked for the four unaware results while the generic awareness gate continued rejecting other contacts.

## Verified native meaning and evidence gap

`0x5bf560(npc)` has the verified one-NPC-argument, boolean-result ABI and matched executable bytes. Its hostile, living, conscious branch checks an AI knowledge record: kind 0 with level greater than 3 returns false. This establishes a knowledge threshold; it does not by itself establish that the guard currently sees the player.

Read-only primary evidence is in `artifacts/claude/melee/ai-defense.txt` and the pinned PE. Disassembly identifies the knowledge component's embedded brain and the kind/level reads, but a production observer must retain exact NPC spawn/registry/component identities. Current-sight and last-seen ownership remain unverified. Reflection names alone are insufficient to use those fields.

The assassination sensor's target is also not an accepted stealth verdict: `docs/native-takedown-range.md` explains that it supplies an interaction target and LOS result. GlobalAssassination's aggregate offered count is neither victim-specific nor limited to ordinary stealth kills. Neither should override a failed awareness check without confirming the exact native outcome.

## Staged next work before another gameplay DLL

1. Establish the reported guard behavior: calm patrol, searching, or active combat. This determines whether the defect is premature alerting or a requested change to eligibility for a guard that remembers the player.
2. Capture the exact approached NPC before its first attack or contact. Record native unaware, read-only knowledge kind/level, player and guard position/facing, active native attack state, and victim-specific native offer/outcome on one game-thread timeline. Include a damage-dispatch marker so a later false result cannot be mistaken for the initial cause.
3. Reproduce the reported crouched approach with normal AI and no prior damage. Compare the first knowledge transition against native attack start, physical interaction and actual blade contact. Simulated checks can establish native state timing; they cannot establish headset acceptance.
4. Repair the observed cause at that boundary, preserve accepted locomotion/turning and nonlethal/native ownership, then verify unaware back/torso/leg hits plus alerted and resting-blade controls. Do not authorize kills from an old unaware sample after a guard has noticed the player.
5. Build one reviewed candidate only after the cause and expected eligibility are concrete. Headset acceptance must repeat the user's crouched approach and body strikes.

The user subsequently clarified the intended rule: approach a patrolling guard from behind, then a physical stab or slash must kill without an attack-speed requirement; a valid native animation trigger may take the animation, otherwise the back hit kills directly. The repair adopts physical rear contact as sufficient even when the generic AI memory predicate returns aware. This also makes rear hits lethal on alerted guards; the user was told this explicitly. Alerted frontal combat keeps ordinary damage.

The read-only external observer captured 89 samples over approximately 25 seconds, but its nearest NPCs were over 9.3 m away. It did not capture the cause of an earlier awareness transition and is not evidence for a proximity-alerting fix. No writes or engine calls were made by that observer.

Implementation, exact native-offer verification, controller identity preservation and focused checks are described in `VR-v658-STEALTH.md`. The current live game was left untouched.
