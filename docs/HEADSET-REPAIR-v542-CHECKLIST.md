# v542 repair acceptance gates

This is an acceptance checklist, not evidence of passing. Do not bypass a blocked candidate manifest. All pre-existing numbered DLLs and uncommitted work remain available; rejected v533/v538 builds are not accepted rollback targets.

## Before a headset run

1. Verify the full required build and original source archive for the exact candidate hash.
2. Establish actual native gameplay and checkpoint-reload evidence. A running process, menu layers, idle load statuses, simulated poses or restored menu state do not count.
3. Preserve the owner save/configuration baseline. Use a reviewed contained runtime lane; do not swap shared saves or silently use another process.
4. Start a fresh game process. Confirm the loaded DLL/PID/hash archive, saved locomotion/turning settings and previously accepted perspective.
5. Keep the private lab-startup limitation separate from headset/game defects. The corrected lowercase startup reaches a requested SLOT01 but both DLL and native-only baselines still lack verified Gameplay. Earlier uppercase trials remained in Main Menu.

## Headset sequence

| Check | Required visible/gameplay result |
|---|---|
| Accepted baseline | Walking, turning, perspective, VR entry and return to desktop retain accepted behavior. |
| Talk versus choke | One squeeze on a street NPC offering Talk performs Talk only. Holding it after the prompt changes never begins choking. Release and a fresh deliberate neck squeeze can choke. Repeat with both hands and a shop/corpse interaction. |
| Hand contact | Open palms and individual fingers push small props at their visible surfaces, without a large invisible sphere. Test slow contact, resting contact, glancing contact, wall contact, open/half/fist and both hands. No giant correction, jitter or unexplained jump. Loose-prop proxies do not yet replace the world/door sphere cast; fist coverage and world contact remain release gates. |
| Severed limbs | A head and arm remain seated at the visible hand throughout pickup, rotation, moving and release. The actual mesh, not just a gold lab proxy, matches the grip. Fingers visibly wrap without the recorded thumb penetration/oversized-proxy defects. |
| Hip weapon | Looking up/down and turning the head independently do not move the holster away from the pelvis. Crouching/body turning keep both the rendered weapon and draw zone attached. Draw/stow pistol and crossbow reliably. |
| Blade block | Put the blade visibly in front of a real incoming attack: native damage/block result agrees with the pose. Repeat with changing kill/choke sensors and left-hand fire. Release ends blocking; attacks do not leak through or double-commit. |
| Thrown sword | Throw into aware/unaware NPCs, a wall and a floor at low/high speeds. The sword falls or stays on real support, never freezes in midair after contact or eight seconds. Check while an NPC dies and through camera changes. |
| Doors | Open gently in both swing directions; a slow push does not jitter toward the player. A fully opened door closes/slams with a physical push and no trigger. Locked/scripted doors remain native-owned. Partial-opening reversal is still an explicit unresolved feature, not a pass. |
| Death/reload | At least five same-process death/checkpoint reload cycles restore current body, hand, item, interaction and camera ownership without a crash or stale float writer. Include another save/map transition. |
| Stop/restart | Stop during gameplay, pause, death and map transition. Require actual restoration before claiming clean exit. The current fail-closed pending Stage0 path may require unpausing/a later valid callback; this is a release gate, not polished exit acceptance. Do not reinstall over retained hooks. |
| Sustained performance | Record sustained contiguous gameplay windows including contact, NPCs, reload and turning. Inspect worst stalls and headset/compositor timing; a median or predicted display period alone is insufficient. |

## Evidence to retain

- Exact candidate identity, source pointer and complete run archive.
- Issue-by-issue headset feedback and short capture of each failing case.
- Crash/exception records and current ownership/pose traces for any mismatch.
- Before/after save/configuration baseline and real restoration result.

Release readiness remains false while any reported defect, accepted-baseline regression or required evidence gate remains open.
