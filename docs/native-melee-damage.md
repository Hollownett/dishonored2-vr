# Native player melee hit path and the direct-damage entry point

Pinned PE: Steam build 17576867, image base `0x140000000`, all addresses are RVAs unless written as `0x14...`. Evidence is static only (Ghidra decompile, capstone, string/xref scans of the PE, and the decl/midnight text read from `base/game*_patch.index` + `.resources`). Nothing here was called or hooked. Tags: **VERIFIED** = seen in decompile/bytes/decl text; **INFERRED** = deduced from control flow or naming, needs a live check.

Raw outputs: `artifacts/claude/melee/*.txt` (decompiles), `artifacts/claude/melee/data/` (decls, midnight scenes), `artifacts/claude/melee/scripts/` (Ghidra + python helpers; `mg.cmd <Script> <out> <args>` runs read-only headless).

## 1. Summary

* The sword hit is **not** a swing-time trace. Pressing attack starts the `slash` midnight scene; the scene's timeline contains a `MELEE_WEAPON_ACTIVE_FRAMES` timed event that switches the sword weapon component "active" for ~100-160 ms. Only while active does the weapon (every frame) take async physics query results, filter them with a **camera-forward cone/box** (`m_npcFocusShape`: cone 60 deg, 3 m, box 2x1.5x1.5), and send a `damage` message to each NPC in the cone. (VERIFIED)
* Latency from attack input to hit window: **200-270 ms** for the standard slash (table 4), plus 1-2 frames, independent of where the blade is. That is why hits feel late.
* The generic apply-damage entry is `idEntity::TakeDamage`, virtual slot 40 (`+0x140`): `result* = victim->vtbl[40](victim, DamageResult out[32], DamageParams* p)`. The console `damage` command and the weapon both reach it through the `damage` message (id `0x93`). (VERIFIED chain, section 3)
* **Recommended entry for the mod:** build an `arkDamageParams` (0x90 bytes) with the engine ctor `0x780260` from the sword's own damage decl, fill hit position/direction/inflictor(sword entity id)/instigator(player id), and call TakeDamage (`0x879270` for arkNPC). Confidence: ABI high, behaviour (reactions, alert, kill credit) medium-high, blood/impact FX not covered (section 6).
* Easy native speed-up exists: attribute `ARK_ATTRIBUTE_SLASH_SPEED_FACTOR` is applied as the midnight play rate when a slash starts (section 7).

## 2. Native player melee pipeline (VERIFIED unless marked)

```
RT press -> PLAYER_STATE_SLASH (arkPlayerStateSlash vt 0x1c85d28)
  0x86c630 (state vtable[25], INFERRED enter handler): 0x86b050 takedown arbitration (kill sensor target -> state 13 Assassination, no slash)
                    else 0x86d320 find target, 0x86cbf0 perform slash:
                      rule handler slash.rulehandler picks the outcome midnight (sword_slash_right/left/..._sneak/slide)
                      starts it (0x854be0/0x854ab0), then rate = player attr[state+0x108] ; if != 1.0 -> 0x7d8fe0(game+0x1f5338, sceneId, rate)
midnight timeline (scene time, scaled by play rate):
  t=200..240  MELEE_WEAPON_ACTIVE_FRAMES(slot RIGHT, forwardAttack, triggerHeavyBlock, damageFactor)
     -> handler 0x84e0c0 -> weapon 0x634890 SetActive(weapon, 1, fwd, heavyBlock, damageFactor)   (flags byte weapon+0x3428: b0 active, b1 fwd, b2 heavyBlock, b3 stop-requested; float weapon+0x3424 = damageFactor)
  t=+100..165 ms later: stop event -> 0x636610 (sets b3) -> 0x1406370f0 per frame -> SetActive(0,...) clears the already-hit list (weapon+0x33d0)
per frame while active (arkCpntWeaponMeleePlayer, vt 0x1b9d508):
  0x1406314f0 -> 0x637160: issue async hull/ray queries named MeleeWeaponWorld/Enemies/Entities/WorldRay (ids stored at weapon+0x3414..0x3420)
  0x630c90 -> 0x636b40 (`0x1406364c0` per list): collect the finished query results into lists weapon+0x78 (world), +0x1188 (chosen NPC hits), +0x2298 (NPC candidates); element = 0x88-byte hit record (section 5)
  0x631020: cone-filter candidates (0x62ee20 -> 0x62f900: dot(dir(cameraPos->hitPos), cameraFwd) > shape cos), choose, then 0x635140 applies:
     for each hit not in already-hit list: if target entity has health (entity+0xd7 bit1) -> vtbl[+0x138] ApplyHit(this, rec, 0, 0, 1.0f, 0) else vtbl[+0x110] OnHit (FX only); add target id to already-hit list (0x630540)
ApplyHit (vt+0x138 = 0x62e550 -> 0x634320):
  decl select vt+0x100 (0x62f160: weaponDecl+0x120 m_damage; +0x1d0 focused; +0x1e0 blink; +0x1e8 focused blink; +0x1f0 bloodthirsty; +0x1f8 generic fatality)
  already-hit check, ValidateHit vt+0x118 (0x630890 -> 0x636630 re-trace owner->hit, snaps/retargets to FIRST non-owner body on the line),
  damage params from decl, amount = attr[weapon+0x70](sword damage factor) * weapon+0x3424 * scale * decl.maxDamage, optional life-leech, 
  victim->vtbl[40] via the damage message, vt+0x110 OnHit (impact FX, msg 0x1aa), 0x630540 add to already-hit list
```

Camera/cone facts (VERIFIED from `sword.cpntweaponmeleeplayer.decl`): `m_npcFocusShape` box extents (2, 1.5, 1.5), cone angle 60 deg, height 3, `m_heightScale 0.95`; `m_worldFocusShape` cone 60 deg / 2.5 m; `m_phantomShapeDimensions` (3, 0.25, 0.25). Targeting uses the head camera (`weapon+0x3438` = camera component, `0x417820` pos / `0x8067c0` forward), never the hand.

Per-frame confirm delay `DAT_141b9d6f0 = 0.09 s` (float, `.rdata 0x1b9d6f0`) applies only to non-NPC "world" candidates in `0x631020` (skipped when forwardAttack b1 set). NPC hits go straight through `0x635140`.

## 3. Damage chain (VERIFIED)

| step | RVA | notes |
|---|---|---|
| console `damage <ent> <decl>` | `885990` (reg. `887250`) | entity = `game->vtbl[0x208](game, argv[1])`; decl = `0x1e030(&DAT_143990b60 /*rva 0x3990b60, decl type "damage"*/, name, 0, 0)`; params = ctor `780260`; sets only the direction (random cone) ; **no pos, inflictor/instigator stay 0x3fff0010**; sends message |
| send message | `884920` (`884a90` -> `884980`) | `SendMsg1(entity RCX, out24 RDX, msgType R8 = &DAT_1439a9d20 /*"damage", arg "arkDamageParams&"*/, variant* R9)`; variant built with `882e20(variant16, params)` (tag 0x39, heap copy of 0x90 bytes), freed with `882fc0` |
| message id | static-init order, `1413977e0` is entry #147 = **0x93** (`damageTaken` 0x94, `incapacitated` 0x95) | CRT table order (INFERRED mapping, matches the neighbouring registrations) |
| entity dispatch | `idEntity::vtbl[2]` (arkNPC `8f2260` -> `8cf620`, case 0x93) | -> `876630(entity, outVariant, params)` = `entity->vtbl[0x140](entity, tmp32, params)` |
| **TakeDamage** | `idEntity::vtbl[40]`: idEntity/arkNPC `879270`, arkPlayer `7846f0` | `879270`: `health = 0x877720(entity, &DAT_1439ba160, 2)`; if health state ok -> `717950(health, params, result)` |
| health core | `717950` arkCpntHealth::ProcessDamage | (a) expire temporary ignore mask (`health+0x48` mask / `+0x50` double end time), (b) `vtbl[0xd0]` compute (arkCpntHealthEvolved `718fe0`: per-type filters, modifiers `435200`, region table `719d20`, player attributes), (c) apply HP, (d) `vtbl[0xf0]` damaged / `vtbl[0xe8]` killed -> `incapacitated` msg + killer stats from instigator, (e) `vtbl[0xe0]` -> `damageTaken` msg (args params + result) which drives AI Touch sensor `1405ed870` ("HandleDamaged"), hit-reaction (`arkCpntAIPhysicsReaction::HandleDamaged`), HUD |
| dead target | `717950` else-branch | queues event 0x1a6 (damage after death) |

### 3.1 `arkDamageParams` (0x90 bytes, 16-aligned copy) layout (offsets VERIFIED from `780260`, `634320`, `717950`, `719d20`)

| off | type | meaning | filled by |
|---|---|---|---|
| 0x00 | decl* | damage decl (`arkDeclDamage`) | ctor arg |
| 0x08 | float | amount (decl+0x5c `m_maxDamage`; player_sword = 10) | ctor, then `780a80(p, v, 0)` |
| 0x10 | u64 | DAMAGETYPE bit mask (decl+0x60). bit2 INSTANTKILL (`780ae0` sets/clears), bit3 NO_REACTION, bit7 MELEE, bit36 AGAINST_MECH, bit44 CANT_INCAPACITATE... (bit order = enum string order, INFERRED) | ctor; weapon ORs extra flags |
| 0x18/0x1c | i32,i32 | NPC / player hit reaction enums (decl+0x68/+0x6c, INFERRED) | ctor |
| 0x20 | u32 | bit mask of body regions hit (`1<<region`) | `780ac0` |
| 0x24 | vec3 | hit position (world) | weapon |
| 0x30 | vec3 | unit vector hit -> attacker (weapon uses sword entity pos) | weapon |
| 0x3c | vec3 | unit damage/swing direction (hit rec +0x48 normalised) | weapon / console (random) |
| 0x4c | i32 | body region of the hit (BODY_REGION_*; keys `m_bodyRegions` of the health decl) | `780ac0` |
| 0x50 | u32 | **inflictor** entity id (the sword entity; its `cpntAIAttentionSource` is required for AI alert) | weapon |
| 0x54 | u32 | **instigator** entity id (player; kill credit/stats, "is player" checks). `14039eec0(p)` returns `&p->+0x54` | weapon |
| 0x58 | 12 dwords | extra block (init by `780500`; weapon `vtbl[0x130]` may fill; +0x84 = entity id sentinel) | |
| 0x88 | u8 | bit1 sever settings present, bit2 decl flag | ctor |

Defaults come from the ctor (`0x3fff0010` = invalid entity id; vec defaults are runtime constants `DAT_1431af620/668`, values live in .bss so not in the file). `DamageResult` (32 B) init `780470`: `+0 float damage dealt, +4 float absorbed, +8 u32 ignored/blocked marker, +0x18 u8 flags (bit0 =1 init)`. TakeDamage initialises `out` itself (`780470` is its first call), so the caller only supplies 32 bytes; it returns the pointer to the result. It skips components whose `+0x1c & 3 == 3`. Death test after the call: `*(u8*)(entity+0xd1) & 8` (VERIFIED use in `824250`); `0x20` = asleep.

Pinned code bytes (first 48 bytes, from the static PE):

| fn | RVA | bytes |
|---|---|---|
| idEntity::TakeDamage | `879270` | `48895c24084889742410574883ec20488bda498bf0488d15d40e140341b802000000e889e4ffff488bcb488bf8e8ce71` |
| arkPlayer::TakeDamage | `7846f0` | `48895c240848896c24104889742418574883ec60488bf1488bca498bf8488bdae85bbdffff8b8e00040000e8407ef5ff` |
| ProcessDamage | `717950` | `488bc4488958104889681848897020574881ecd0000000f20f104950488b59100f57c0660f2fc8498be8488bfa488bf1` |
| params ctor `(params RCX, decl RDX)` | `780260` | `48895c2408574883ec204889118b425c488bf9894108488b4260488bda488941108b42688941188b426c89411c33c989` |
| result init | `780470` | `33c0488901894108488941100fb6411824f10c01884118488bc1c3cccccccccc40534883ec20488bd9e812278aff488d` |
| set amount `(p, float xmm1, clamp r8b)` | `780a80` | `488b01f30f105058f30f10585c4584c074240f57c00f2fd8761c0f2fd177110f2fcb76090f28d3f30f115108c30f28d1` |
| set region `(p, int)` | `780ac0` | `4c8bc1b8010000008bcad3e04189504c41094020c3cccccccccccccccccccccc84d274064883491004c348836110fbc3` |
| set instant-kill `(p, bool)` | `780ae0` | `84d274064883491004c348836110fbc340534883ec20488bd9e8e2660600488d0523c94d0148890333c948894b208b05` |
| extra init | `780500` | `33d289118b0516f1a2028941048b0511f1a2028941088b050cf1a20289511c89410c8b05f8f0a2028941208b05f3f0a2` |
| SendMsg1 | `884920` | `4c894c24204889542410574883ec4048c7442438feffffff48895c2450498bf9488bdac7442430000000004c894c2420` |
| variant from params | `882e20` | `48895c24084889742410574883ec20488bf9c60139b990000000488bdae8aee586ff488bf04885c00f84b5000000488b` |
| variant free | `882fc0` | `40534883ec200fb601488bd93c397509488b49084885c9750d3c307516488b4b084885c9740de835e586ff48c7430800` |
| decl lookup `(typeObj, name, create, flag)` | `01e030` | `44884424185556574154415541564157488d6c24d94881ec0001000048c745cffeffffff48899c2440010000410fb6f9` |
| entity by id (`ecx`) | `6dc560` | `894c24084883ec28488b0d218d2c03488d5424304881c1b0303100e880980d004883c428c3cccccccccccccccccccccc` |
| component lookup `(entity, classinfo, flags)` | `877720` | `48895c2410448944241855565741544155415641574883ec20488be98b898802000041bc010000004c8b8d5802000041` |
| player entity `(game)` | `8a8ba0` | `4881c1b030310033d2e942d3f0ff` (14 bytes: `add rcx,0x3130b0; xor edx,edx; jmp 0x1407b5ef0`, then padding) |

Class infos for `877720`: arkCpntHealth family `0x1439ba160`, arkCpntWeaponMeleePlayer `0x1439c0260` (id range at `+0x60/+0x64`, e.g. `DAT_1439c02c0`), flags arg 2 is the "started" filter (INFERRED from `arkCheckedCpntPtr<..., CF_STARTED>` strings). The entity list is `game+0x3130b0`; entity `+0xa8` = id, `+0xac` = owner/spawner id, `+0x268/+0x270` component list, `+0x300` position, `+0xd1` flags, `+0xd7` bit1 = has health.

### 3.2 Which variant gives what

| variant | reactions / death | AI alert | kill credit | blood / impact FX | hit list / parry redirect |
|---|---|---|---|---|---|
| console path `885990` (decl, random dir, ids 0x3fff0010) | yes (decl reaction) | **no** (inflictor missing: sensor logs "has no attention source") | no | no | no |
| TakeDamage with full params (**recommended**) | yes | yes if inflictor=sword entity (has `cpntAIAttentionSource`, `ATTEV_DAMAGED`, yield HIGH) | yes if instigator=player | no | no |
| weapon ApplyHit vt+0x138 with synthetic hit record | yes | yes | yes | yes (OnHit, mat. `contactsystem/wpn.sword`) | yes |

`player_sword.damage` (VERIFIED): `m_maxDamage 10`, `DAMAGETYPE_MELEE`, `NPC_HITREACT_SMALL`, `PLAYER_HITREACT_SMALL`. Guard health 40, lower guard 30, civilians ~10 (`health_npc/guard` decls). `sword_focused.damage` = 10, HITREACT_BIG. Assassination = `DAMAGETYPE_INSTANTKILL|NO_REACTION|MELEE`.

## 4. Timing (VERIFIED from `midnight/gameplay/player/base/combat/sword_slash_*.midnightscene`, ms of scene time)

The scene file holds two variants (Emily/Corvo); both ranges are listed.

| scene | clip length | ACTIVE_FRAMES start..end | AI_PARRY_MIDNIGHT_START | ATTACK_HIT_CAN_START | SLASH_CAN_BE_INTERRUPTED |
|---|---|---|---|---|---|
| slash_right (first strike) | 1000 | 200..332 / 220..348 | 260 | 250 / 300 | 420 / 446 |
| slash_left | 1000 | 240..340 | 260 | 300 | 501 |
| slash_rightb | 1000 | 165..330 / 220..360 | 206 | 250 / 252 | 438 / 465 |
| slash_leftb | 1000 | 240..340 | 282 | 300 | 551 |
| sneak (crouched) | 933 | 240..374 | 260 | 260-267 | 367-369 |
| slide | 833 | 270..428 | 260 | - | 583 |
| focus_out (hold-release variant) | 666/833 | 33..99 | - | - | 600/684 |

* Input to scene start is immediate (no blend-in on slash scenes); the earliest damage is therefore about 0.2-0.27 s after the press, then 1-2 frames of async query + chosen-hit processing. If the mod's swing detection fires 100-250 ms into the physical swing, perceived delay reaches 0.4-0.6 s or more.
* The focus (hold) variant is gated by a player attribute at `slashState+0x10c` (`>0` enables `+0xd3`); the standard tap performs the slash on state entry (`0x86c630` -> `0x86cbf0`). INFERRED, not tested.
* AI is told about the incoming attack at `AI_PARRY_MIDNIGHT_START` (about 206-282 ms), i.e. inside the active window. A contact-time hit earlier than that cannot be parried by the AI (section 8).

## 5. Hit record (0x88 bytes, `arkMeleeHitInfo`; ctor `81a760`, layout from `636b40`, `636630`, `634320`, `635c90`)

| off | meaning |
|---|---|
| 0x00 u32 | id (0xffffff = none) |
| 0x08 ptr | source weapon component |
| 0x10 ptr | weapon physics material (`weaponDecl+0x138`, `contactsystem/wpn.sword`); non-null required by OnHit FX |
| 0x18 u32 | hit body handle (low 24 bits = body index; body table `*(world+0x28)+idx*0xb0`, world = `*(game+0x60410)`) |
| 0x20 ptr | hit-body owner component (`rec+0x60` of the query record, +8); `*(ptr+0x10)` = target entity |
| 0x28 ptr | hit shape object = `0x92c10(game+0x60280, u16 shapeKey)` |
| 0x30 / 0x3c / 0x48 | hit position / normal / velocity of the owner (swing direction, unnormalised) |
| 0x54 float | hit distance (scaled fraction) |
| 0x58..0x87 | misc, default via ctor |

## 6. Recommended recipe (Stage0 game-thread hook)

```
// once per hit, on the game thread, outside any loop over entities
weapon = scan player-owned entities for a component with vtable base+0x1b9d508   // entity+0xac == player id; or player+0x328 -> +0x68 slot (INFERRED)
wdecl  = *(void**)(weapon+8);  dmgDecl = *(void**)((char*)wdecl+0x120);          // or 0x1e030(base+0x3990b60,"components/characters/player/base/damage/player_sword.damage",0,0)
alignas(16) u8 p[0x90]; 0x780260(p, dmgDecl);                                    // rcx=p, rdx=decl
amount = attr(owner attr set = *(weapon+0x30), idx = *(int*)(weapon+0x70)) * (*(float*)(weapon+0x3424)) * *(float*)(dmgDecl+0x5c);
0x780a80(p, amount, 0);   0x780ac0(p, region);                                   // region from hit body (0x634f70) or head/torso default
*(vec3*)(p+0x24) = hitPos; *(vec3*)(p+0x30) = normalize(bladeOrHand - hitPos); *(vec3*)(p+0x3c) = normalize(tipVelocity);
*(u32*)(p+0x50) = *(u32*)(weaponEntity+0xa8); *(u32*)(p+0x54) = *(u32*)(player+0xa8);
alignas(16) u8 res[32];                                                           // initialised by TakeDamage itself
((Res*(__fastcall*)(Ent*,void*,void*))(*(void***)victim)[40])(victim, res, p);   // = 0x879270 for arkNPC
0x630540(weapon, *(u32*)(victim+0xa8));   // stop the native active frames from hitting the same NPC again in this swing
```

Rules:
1. Keep pressing RT at swing start so the scene, arm animation and AI parry/attack-hit events still run; the contact hit and `0x630540` make the later native active frames skip that NPC (list is cleared when the weapon deactivates via `0x634890(...,0,...)`; if the swing is cancelled before that, the NPC stays immune until the next deactivation, INFERRED).
2. Skip contact damage while the `kill`/takedown sensor qualifies (`0x86b050` turns the slash into Assassination state 13 with `assassination_damage`; do not duplicate).
3. Gate on `victim+0xd7 & 2` (has health) and not `(victim+0xd1 & 8)` (dead); reject the player and allies as native does.
4. Call at most once per NPC per swing. Never from the render/XR thread. First live test on one lone NPC; kills run FX, ragdoll, stats and network events synchronously in `717950`.

Full-fidelity alternative (adds impact/blood FX, ValidateHit retarget, hit list): fill a 0x88 hit record from the mod's own sphere cast (`c89bc0`, body handle at collector +0x78, shape key +0x80, see `docs/native-world-trace.md`) and call `weapon->vtbl[+0x138](weapon, rec, 0 /*skip active check*/, 0, 1.0f, 0)` = `62e550`. Risk: record fields +0x10/+0x20/+0x28 must be valid engine pointers; a bad pointer crashes in `634320`/`62fec0`. `0x630890` rewrites the record from its own owner->hit trace, so keep `rec+0x30` on the actual surface.

## 7. Speeding up the native attack (VERIFIED code, no write attempted)

* `0x86cbf0` (slash perform): after starting the outcome scene it reads `player attr[ slashState+0x108 ]` = `*(float*)( *(*(player+0x330)+0x48) + 0x24 + idx*0x28 )`; if that value is not `1.0` it calls `0x7d8fe0(game+0x1f5338, sceneId, value)` for the main scene (`state+0x100`) and the paired camera scene (`state+0xc8`). `0x7d8fe0` finds the scene instance and stores the rate (`0x7d1740`: `movss [rcx+0x16c], xmm1`). Attribute name `ARK_ATTRIBUTE_SLASH_SPEED_FACTOR` (string `1af9df0`, name holders `32cd960, 32cec78, 3996830, 39a6858, 3a27028`). The active-frame events are scene time, so factor 2.0 moves the hit window to about 100-175 ms and also speeds the arm animation.
* Two ways, both mod-written values: set the attribute's current value (entry `+0x24`, base at `+8`) before the press (it may be recomputed when modifiers change, INFERRED), or call `0x7d8fe0` right after the slash starts (slash state pool index via the player FSM component vtable `0x1c73d58`, state vtable `0x1c85d28`).
* No melee-specific gameplay cvar exists. Related cvar objects (name ptr `+0x30`, int/float value `+0x28`): `ark_weapon_melee_debug_lifetime` `0x39880a0`, `ark_close_combat_debug_lifetime` `0x397d620`, `g_maxPlayerMeleeWeapons` `0x39a3a10`, `g_damageScale` `0x39a0f10`, `g_aiIncomingDamageScale` `0x39a5b60`, `g_playerIncomingDamageScale` `0x39a5be0`, `g_damageSave` `0x39a0710`, `g_debugWeapon` `0x39a0910`, `g_timeEntities` `0x39a1710`, `g_stopTime` `0x39a0e10`, `g_freezeTime` `0x39a60e0`, `com_timeScaler` `0x25b1e00` (global time, not useful), `ark_bendTimeLastsForever` `0x3987d80`. The three damage-scale cvars and the debug cvars have no code reference other than their registration (byte scan of object and value field), so they look inert (VERIFIED absence, behaviour INFERRED). `ark_god_npc/player` are `0x398d580/0x398d500`.

## 8. Block, parry, unaware (what is known)

* The player damage call has no "blocked" return. An NPC parry is **geometric and animation driven**: `arkAIMeleeDefense` (vtable `1b88e58`, update `596a20`, state at `+200`: 2 parry, 4 dodge) plays a parry/dodge midnight on the NPC after the player's `AI_PARRY_MIDNIGHT_START` message; the sword body then lies on the line to the victim. `ValidateHit` (`0x636630`, called unconditionally from `0x630890`) re-traces owner -> hit point against bodies and retargets the hit to the first non-owner entity on the line; a sword/prop is not damageable (`entity+0xd7 & 2` clear) so it only gets FX and the NPC takes nothing. Recoil scenes `sword_parry_ll/lr/rl/rr` (`PLAYER_RECOIL_NPC_PARRY_*`) are the player-side reaction. (the retarget is VERIFIED; "the obstruction is the NPC's raised sword" is INFERRED)
* Health has a temporary ignore mask (`health+0x48`, expiry double `health+0x50`, setter `1407186a0`, default from decl `m_ignoredDamageTypes`); hits whose type mask intersects it are dropped in `717950`. Not shown to be used by AI parry (INFERRED unrelated).
* To respect blocks with the direct call: (a) cheap, do your own blade-origin -> victim trace with `c89bc0` and require the first non-player body to be the victim (same rule as `636630`); or (b) use the weapon ApplyHit path. Reading the AI parry state needs the `arkAIMeleeDefense` instance from the NPC AI components (not located statically, needs a live trace).
* Stealth: unaware = `0x5bf560(npc)` (hostility matrix `game+0x1f7bb8` < 2, or AI knowledge not alerted). In the health region table (`719d20`) unaware only scales types listed in `m_scaledDamageTypesIfUnaware` (guards: BOLT/BULLET only), so a plain slash on an unaware guard is still 10 damage (not a kill). One-hit kills come from the takedown/assassination states (`assassination_damage` INSTANTKILL, `generic_fatality` maxDamage 1000), chosen by `0x86b050` before any slash. Keep that path native.
* Attribute modifiers on the sword: `ARK_ATTRIBUTE_SWORD_DAMAGE_FACTOR`, `..._AGAINST_MAGICAL` (weapon `+0x3450`), `..._AGAINST_MECHANICAL` (`+0x3454`, also ORs DAMAGETYPE_AGAINST_MECH), `ARK_ATTRIBUTE_HEALTH_GAIN_ON_SWORD_HIT` / `PLAYER_MELEE_WEAPON_DAMAGE_TO_HEALTH_FACTOR` (leech: heal call `71a140` inside `634320`, only when the weapon runs the full ApplyHit). The direct TakeDamage recipe skips those unless you replicate them.

## 9. Risks and live checks before shipping

1. Message id 0x93 and TakeDamage slot 40 are derived statically (CRT order + case 0x93 + vtable); confirm once by calling `885990`-equivalent on a lone NPC and seeing health drop.
2. `p+0x30/0x3c` semantics and the region value (`p+0x4c`) are inferred from the weapon code; wrong values only change knockback/scaling.
3. Re-entrancy: TakeDamage on a killed NPC synchronously spawns ragdoll/FX and posts network events (`FUN_1408112c0(queue,...)`). Do it from Stage0 only after the hook's own work; avoid calling inside iteration of the entity list.
4. Double damage: rely on `0x630540`; verify the later native active frames do not hit again.
5. AI parry is not simulated at contact time (section 8); expect guards to parry less. Headset feel is not established by any of this.
6. `ValidateHit` uses the player position at sword height as the trace origin; a hand far from the body can be retargeted by the native path if you use ApplyHit.
7. Cone filtering is on the head camera; the direct path ignores it by design, so reach/feel is entirely your own volume test.
