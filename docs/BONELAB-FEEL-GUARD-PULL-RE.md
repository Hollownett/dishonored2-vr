# Living-guard pull and stagger: levers and probe (plan A, Task 14)

Status: static evidence only. **No live experiment has been run** (the saves have no unaware guard within reach). Every live measurement below is marked `needs test save: player standing within reach of an unaware guard`. Nothing here changes production behaviour: the probe is a dev knob that is off by default.

Goal (user, 1L): grabbing a living guard pulls him toward the hand, staggers him and alerts him, without ragdolling him. Today `NativeWeaponHands::yank` (`src/native_weapon_hands.hpp`) does `shove` (damage decl `components/base/damage/shove_damage.damage`, amount 0 = the decl's own, no damage) plus `ragdoll` (`START_RAGDOLL 0xb9`, then a pull velocity). Sim v424: bodies go dynamic within 1 s, he is on the floor at 2 s and stands at 4-6 s.

## Levers that exist in the code today

| Lever | Entry point | What it is known to do | Unknown |
| --- | --- | --- | --- |
| shove | `NativeWeaponHands::shove` -> `NativeMelee::apply` (TakeDamage, `docs/native-melee-damage.md`), decl `shove_damage.damage`, the exe's `DAMAGETYPE_SHOVED` type, `m_shovedDamage` | no damage. Alert is **unproven**: `shove` builds `Sword{player,player,0,decl,pid,pid}`, i.e. the player is both inflictor and instigator, while `docs/native-melee-damage.md` only establishes the AI alert for an inflictor that is the sword entity with a `cpntAIAttentionSource`. Measure `unaware`. | whether it staggers without START_RAGDOLL; how far it moves him; whether it ragdolls him by itself |
| other damage decls | `damage_decl(base,read,name)` -> FindDecl `0x1e030` under the damage type | any named decl can be applied; `phys_damage.damage` is already used for thrown props | which names exist (see below) |
| teleport step | `NativeWeaponHands::teleport` -> `0x87d1e0(entity,pos,axis,0)` (REA Q4: no finite check, the wrapper adds it) | moves a runtime-spawned entity (sword copy, hip props); used per frame | what it does to an arkNPC with a live AI/physics (snap back? teleported capsule? animation pop?), and it takes an orientation (the probe passes identity) |
| START_RAGDOLL / VELOCITY_IMPULSE | `0xb9` / `0x149` via `8112c0` | ragdolls him; **excluded** from the new design | n/a |

## Static scan (Dishonored2.exe, SHA-256 `b671f824...6180`)

- The damage decl names are not stored in the exe (`strings` finds no `components/base/damage/` path; they live in packed resources). The only damage decl paths in the exe are `code_referenced/damage/{debug,generic_cure,generic_kill,generic_no_reaction,generic_sleep}_damage.damage` and `npc_remove_damage.damage`. So `shove_damage`, `phys_damage` and any "stagger" decl cannot be listed statically. Decl paths the mod already carries (the `shove` test command, `dxgi_probe.cpp`): `components/base/damage/shove_damage.damage`, `components/characters/player/base/damage/grapple_shove_npc.damage`, `components/characters/player/base/damage/windblast_npc.damage`, `components/base/damage/knockdown_damage.damage`. Of these only `shove_damage` and `knockdown_damage` are reachable through `shove_decl <name>` (which fixes the prefix `components/base/damage/`); the grapple and windblast decls need a knob that takes a full path (not built: add it if the first runs point there). Anything beyond these four needs the resource tooling or a Windows `D2StaticResearch.java` run. **Unresolved** beyond the four.
- The exe has the damage-type names `DAMAGETYPE_SHOVED`, `DAMAGETYPE_PHYSICS`, `DAMAGETYPE_PHYSICS_HARMLESS`, `DAMAGETYPE_PHYSICS_STUN_IN_HEAD`, `DAMAGETYPE_NO_REACTION`, `DAMAGETYPE_MELEE`, `DAMAGETYPE_BLOWBACK`, `DAMAGETYPE_WINDBLAST`, `DAMAGETYPE_SLEEP`, `DAMAGETYPE_PRE_SLEEP`. Damage types are per decl, so a decl of type `DAMAGETYPE_SHOVED` is the "shove" family. `DAMAGETYPE_NO_REACTION` is the type to avoid (the physics-reaction component logs "won't react at all"): it would give neither stagger nor alert.
- Hit-reaction machinery: `arkAIHitReact::PlayHitReact` ("No hit react found" when the midnight/animation is missing), `arkCpntAIPhysicsReaction::HandleDamaged` / `HandleIncapacitated`, `arkMidnight::PRIORITY_NPC_HITREACT`, the enum `arkNPCHitReaction_t` (strings `NPC_HITREACT_BIG`, `NPC_HITREACT_INVALID`), the rule action `arkRuleAction_AIHitReactOutcome`, and the AI sub-web state `AISUBWEB_PAIN_STAGGER`. These point at: a damage decl chooses a hit-react class, the AI plays a hit-react midnight (a stagger), and `arkCpntAIPhysicsReaction` can turn a hit into a physics reaction (ragdoll) depending on the decl's settings. Which setting separates stagger from ragdoll is **not determined statically**: it needs the decl contents or the `HandleDamaged` decompilation (`tools/D2StaticResearch.java`, `D2TakedownRefs.java` were not run; they need the Windows Ghidra project). `ACTIONSCRIPT_ENABLE_STAGGER_PAIN` and `arkHitReactSettings` belong to the player controller, not to NPCs.
- A cvar string says a knockback value can be overridden ("If -1, use knockback value from damage decl"), so knockback strength is a decl field: a candidate source of the "pull/stagger distance" but unverified.

## What `state_d1` (`npc+0xd1`) is known to mean

| Value / bit | Meaning | Source |
| --- | --- | --- |
| `0x08` | dead | `docs/entities.md` (VERIFIED live) |
| `0x20` | unconscious / downed | `docs/entities.md` (INFERRED; set by `0x879410`) |
| `0x80`, `0x90` | observed whole-byte values of living guards (`0x80`) and civilians (`0x90`); dead guards read `0x88`. The high bits are class/base bits shared with dead guards, so they are not a liveness test (`living()` is `!(state & 0x28)`) | `docs/entities.md` live read |
| `0x40` | the `WhenVulnerable` gate of the takedown range rule (an aware but vulnerable target). Not an awareness bit | `docs/native-takedown-range.md` |
| `0x20` | also documented as asleep (`docs/native-melee-damage.md`) and possibly despawning (`docs/entities.md`): treat it as "not an awake guard" | those docs |
| anything for stagger, alert or ragdoll | **not documented**. `living()` is `!(state & 0x28)`; the probe records the raw byte so the experiment can show which bits move. | this task |

Alert is not read from `state_d1`. The engine's own stealth test `NativeMelee::unaware` (`0x5bf560`, `docs/native-melee-damage.md`: not hostile to the player, or alive/conscious and his AI knowledge kind 0 level < 4) is the alert signal: `unaware` going false means alerted. Ragdoll is read from the physics body: bone 0 `flags & 7 == 2` (dynamic), the condition `native_ragdoll_body` already requires. `bodies_dynamic == 0` means "not a readable dynamic ragdoll": `native_ragdoll_body` returns nothing for a keyframed guard and for any failed physics read alike, so a `0` is only evidence of "no ragdoll" together with a moving, readable `state_d1`/position. `unaware_ok == 0` means the stealth test did not run (code pin mismatch); a fault inside the engine call is caught by the probe's own SEH and also gives `unaware_ok == 0`, so `unaware == 0` with `unaware_ok == 1` is a real answer from the engine.

## The probe

Dev knob `build/guard-probe` (absent by default, nothing is read or run). Its text is one of `shove`, `shove_decl <name>` (name under `components/base/damage/`, `[a-z0-9_]{1,64}`), `teleport_step <metres> <count>` (step 0 < m <= 0.5, count 1..20). It is read once at start. The next pull (`yank` request: a full fist at a living guard's upper body, then the hand faster than 2.2 m/s) runs that one experiment **instead of** the shove-plus-ragdoll, deletes the knob file, and writes `artifacts/guard-probe.csv` (`tick_ms,entity_id,x,y,z,state_d1,unaware,bodies_dynamic,read_ok,unaware_ok`) plus, for the shove experiments, `artifacts/guard-probe-result.txt` (decl, whether the shove applied, `last_shove`) so a silent no-op is visible once before and then every 50 ms for 6 s. The probe itself posts no `START_RAGDOLL` or velocity (a source check pins this). The shove, however, goes through `TakeDamage`, whose own reaction can still ragdoll or post events (`docs/native-melee-damage.md`): `bodies_dynamic` is the measurement, not an assumption. `teleport_step` moves `npc+0x300` by `step_m` toward the hand (horizontal only) every 100 ms with the **identity axis**, which is a world-aligned basis, not "no orientation": it can reset his yaw, a confound (the arkNPC basis offset is not documented in the repo, so the current axis cannot be preserved). Read the `teleport_step` result as a rotation-reset test as much as a displacement test. Each step first re-checks the guard's id and class, and stops if the slot was reused. Scenario: `tools/linux/scenarios/guard_probe.py`. Tests: `tests/guard_probe_check.cpp`, `tests/bonelab_scenarios_check.py`.

## Live experiments (all: needs test save: player standing within reach of an unaware guard)

For each, record: displacement toward/away from the hand, whether `unaware` went false and after how long, whether `bodies_dynamic` stayed false, the `state_d1` byte over time, whether he returned to his old position within 2 s, any crash.

| Experiment | Result |
| --- | --- |
| `shove` | needs test save |
| `shove_decl <each candidate name>` | needs test save, and a candidate list (unresolved above) |
| `teleport_step 0.1 5` | needs test save |

## Lever decision table (provisional)

| Need | Lever | Status |
| --- | --- | --- |
| stagger | "shove default" (`shove_damage`) is the only decl known to work without damage; whether it staggers without ragdolling is unmeasured | unresolved: needs test save |
| alert | unproven: the shove's inflictor is the player, not the sword entity the docs require for the AI attention source; confirm `unaware==0` (with `unaware_ok==1`) within N ms, else fall back to an explicit alert lever (not yet identified) | unresolved: needs test save |
| pull | `teleport_step` is the only candidate; behaviour on a live arkNPC unknown (snap-back risk, no facing) | unresolved: needs test save; if it snaps back or breaks the AI the decision is `none` and Task 15 ships stagger and alert only |

Task 15 can plan against these three levers with the table as it stands, but must not ship pull until `teleport_step` is measured.

## Second opinion (DeepSeek, 2026-10-22) and what was done

Findings folded in: the stealth test now reports `unaware_ok` (P2); the body flag caveat is documented (P1, not changed: distinguishing a failed physics read needs a new body reader); the alert claim and the identity-axis claim are corrected (P2); `state_d1` meanings corrected (P2); the existing decl candidates are listed (P2); the "never posts" claim is narrowed (P2); the teleport step re-validates the guard (P3); the CSV is opened before the one-shot is consumed, and the shove result is written (P3). Not done: a full-path knob for the grapple and windblast decls, and a test that fails when a shove ragdolls (only the live run can show that).
