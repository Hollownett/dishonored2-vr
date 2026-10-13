# Native NPC sword attacks and the player's block (basis for VR directional blocking)

Pinned PE: Steam build 17576867, image base `0x140000000`; all addresses are RVAs unless written `0x14...`. Static evidence only (Ghidra decompile, capstone, `.rdata`/`.data` scans, decl and midnight text from `base/game*_patch.index`). Nothing was called or hooked and the running game was not touched. **VERIFIED** = decompile/bytes/decl text seen; **INFERRED** = deduced, needs one live check. Raw outputs: `artifacts/claude/block/out/*.txt` (decompiles `d1..d13`, `timing-table.txt`, `attack-table2.txt`, `rtti-vt.txt`, `vtables.txt`), `data/` (decls, scenes), `scripts/py` (helpers), `mg.cmd`/`gd.ps1` (read-only Ghidra wrapper). Builds on `docs/native-melee-damage.md` (player side); sections numbered here are self-contained.

## 1. Result in six lines

* The native block is **not** blade-aware. At the instant a guard's weapon touches the player, `0x6357d0` asks `0x60d750`: "is the Block state active, and is the guard's *body position* inside a 40 degree cone ahead of the *head camera*?" Nothing about the sword. That is why any frontal attack is blocked while RB is held. (VERIFIED)
* Outcome (normal block / parry / blow-back) is decided in that same call by `arkPlayerStateBlock` `0x859980`, from a parry window (first 0.30 s after the Block state starts, `PARRY_PHASE`) and a few state bytes. (VERIFIED)
* Per NPC, "is it attacking me and how far into the attack" is readable from the component `arkCpntAICombatAttack` (vtable `0x1b93a88`): state at `+0x138` (2 = melee attack), target through the AI blackboard, attack-tag handle at `+0x168`, scene id at `+0x40`; the exact contact window is the weapon flag `weapon+0x3428 bit0`. (state/offsets VERIFIED, enum numbering INFERRED)
* The attack tag `AI_ATTACK_SLASH_LEFT/RIGHT` exists but is **not a geometric side**: guards' "right" scenes include chops and strokes in both directions (offline animation decode, section 5). Do not drive the mod from the tag alone. Use the guard weapon entity's live position and swing direction (section 6). (VERIFIED tag identity; "tag is not geometric" INFERRED from decoded animations)
* Recommended: keep native block as the effect, but gate it at the hit frame with the VR blade. Single inline hook on `0x6357d0` (pinned bytes in section 8): if the VR blade does not cover the incoming blade, call the weapon's own ApplyHit (`vtbl[0x138]`, exactly what the engine does when unblocked) instead of the block path. Fallback without hooks: RB policy from the per-frame recipe (section 7).
* Confidence: reading attack state/timing high (80%), hook mechanics high (85%), feel/tuning unknown until a headset test. Kicks and punches (`arkCpntCloseCombat`) never consult the block component and probably cannot be blocked at all (INFERRED).

## 2. Pipeline from guard animation to damage or block (VERIFIED)

```
midnight scene (sword_attack_*)  timed event MELEE_WEAPON_ACTIVE_FRAMES(type 0)  -> handler 0x84e0c0 -> 0x634890 SetActive(weapon,1,fwd,heavy,dmgFactor)
                                   flags byte weapon+0x3428: b0 active, b1 forward, b2 triggerHeavyBlock; float +0x3424 damage factor
per frame (game thread)  0x951e30 -> 0x62d7a0 (arkCpntWeaponMeleeNPC update)
   if (+0x3428 & 1): target = AI blackboard combat target of the owner (weapon+0x3430 weak ref -> comp; target id at comp+0xcdc, valid byte comp+0x101b)
                     prune candidate hit records (weapon+0x2298, 0x88 B each) to reach 0x62c9a0 (decl+0x190 vs player, +0x194 vs NPCs)
                     0x62cb80: for each candidate whose entity == target:
                          class id in [DAT_1439ce350..54] (player) -> 0x6357d0   ;  in [DAT_1439ce2c0..c4] (NPC) -> 0x6355f0
                          on result 1 post entity event 0xe7 {victimId,0} to the attacker
0x6357d0(weapon, hitRec, victim):
   blockCpnt = victim.GetComponent(arkCpntBlock)  (classinfo base+0x39edc20, flags 2)
   if !blockCpnt                                  -> ApplyHit
   gate = 0x60d750(blockCpnt, attackerPos)        (attackerPos = 0x878230(owner NPC) = AABB centre)
   if !gate || !weaponDecl[0x180] (m_isBlockable) -> ApplyHit   = (*(void***)w)[0x138](w, rec, 1, 1, *(float*)(w+0x3424), 0)   [damage]
   else  st = *(*(fsm+0x90)+0x170)                (arkPlayerStateBlock, pool index 44)
         res = 0x859980(st, attackerEntity, heavy = (w+0x3428>>2)&1)   -> 0 SIMPLE, 1 PARRY_LOSE, 2 BLOW_BACK
         res==2 -> 0x636140 (damage the attacker with weaponDecl+0x128 m_blowbackDamage, impulse +0x130, FX +0x158/+0x168)
         else   -> entity event 0xe6 {victimId, res} to the attacker
         0x635f30 sparks, 0x634890(w,0,..) deactivate the weapon, return 0 (no damage)
```

`0x60d750(blockCpnt, pos)`: requires `GetComponent(victim, FSM)` and `0x803180(fsm, 0x2c)` (Block state active byte) and the player camera component (class info `DAT_1439d7480`, pos `+0xd8`, forward `+0xfc`; role INFERRED from argument order, matches the head camera the mod already drives); then `0x60d8b0` tests `pos` against a cone: apex camera, axis camera forward, height `decl+0x6c` (m_blockConeHeight, ctor default 5.0), derived radius `decl+0x70`, angle `decl+0x68` = `m_blockAngle` (ctor 60, `block.cpntblock` sets **40**, clamped 1..89). The in-cone maths mixes a `/height` factor so the exact shape is a flared cone (INFERRED), but it depends on head forward and the NPC centre only. Class `arkCpntBlock` vtable `0x1b99248`, decl vtable `0x1b99108`. Projectile blocking uses the sibling `0x623f80` (callers `0x625600`, `0x808bf0`), not this path.

## 3. Player block state (`arkPlayerStateBlock`, vtable `0x1c81a68`) (VERIFIED unless noted)

Find it: player entity `game+0x3130b0`; component list `+0x268`; FSM comp vtable `0x1c73d58` (`tools/player_states.py`); `pool = *(fsm+0x90)`; **Block = pool index 44 (0x2c)**, pointer `*(pool + 0x10 + 44*8) = *(pool+0x170)`; active byte `st+0x20`.

| off | type | meaning |
|---|---|---|
| `+0x08/+0x10/+0x18/+0x20` | u32/ptr/ptr/u8 | index (44) / FSM owner / decl (`block.playerstateblock`) / active |
| `+0x24` | i32 | current block scene instance id |
| `+0x38` | float | time in state (INFERRED) |
| `+0x50` | i32 | phase: 1 raise (`sword_parry_in`), 2 hold (`sword_parry_idle`), 3 lower (`_out`), **4 hit reaction** (set at end of every resolved block) |
| `+0x58` | u8 | `m_parryPhase`: 1 while timed event `PARRY_PHASE` (type 5) runs, toggled by `0x85a100` (handler `0x84e3a0`) |
| `+0x59` | u8 | `m_canParry`: set at state enter unless the previous exit was within `+0x70` seconds (anti-spam) |
| `+0x5c` | u32 | entity id of the last attacker that was **parried** (written only in the parry branch) |
| `+0x60` | u32 | entity id of the last attacker blocked (`0x3fff0010` = none); the update (`0x85a160`) turns the player toward it |
| `+0x68` | double | time of last exit; `+0x70` float anti-spam cooldown in seconds (computed in `0x8590b0`; source INFERRED, the decl's only time value is `m_freeLeftHandDuration 0.5`) |
| `+0x74` | float | extra parry time left; loaded from attribute index `+0x88` when `PARRY_PHASE` **ends**, decays by dt |
| `+0x78/+0x7c/+0x80/+0x84/+0x88` | i32 | attribute indices (chance of blow-back = adept parry, refund, ...), `+0x88` = parry-phase extra duration |

Parry rule in `0x859980` (VERIFIED): parry iff `(st+0x58 != 0 || st+0x74 > 0) && st+0x59 != 0 && attacker has the AI-skills comp (class `DAT_1439b9bc0`) with parry-lose midnights for **both** slash types` (`0x8598c0`). Then `res = 1` and, if `0x14060c180(attrs, st+0x78)` rolls true (attribute value = percent chance), `res = 2` (blow-back). Otherwise ordinary block, `res = 0`, plays `sword_parry_hit_1/2` (`_clockwork` for heavy). **Window length:** `PARRY_PHASE` clip in `sword_parry_in.midnightscene` is `t=0 dur=300 ms` (`_in_sneak` 333 ms), so a fresh RB press must land **0..0.30 s (+attribute extra) before contact**. A held RB (the current mod behaviour, doc `vr-combat-integration.md` line 16 "RB is already held, which is the native parry window") gives ordinary blocks only.

Success signals to read after the hit: `st+0x50 == 4` (rising) = a block was resolved; `st+0x60` = attacker id; `st+0x5c` changed = parry (`res` 1 or 2). Entity events/messages also exist (`0xe6` to the attacker, a `blocked` message `DAT_1439abb20` with `{attacker, 0|1}` sent from the state).

## 4. Per-NPC attack state

### 4.1 `arkCpntAICombatAttack` (RTTI vtable `0x1b93a88`, component on every arkNPC; factory `0x233efe0`)

Methods: init `0x5cc9e0` (caches sibling comps), start `0x5cd8b0` (subscribes entity events `0xbf 0xe6 0xe7 0xe8 0x47 0x48 0x20 0x3e 0x49 0x5a`), stop `0x5cdb50`, per-frame update `0x5d0e00`, serialise `0x5d0250` (lists the persistent fields below).

| off | type | meaning | status |
|---|---|---|---|
| `+0x10` | ptr | owner NPC entity | VERIFIED |
| `+0x20..` | obj | embedded midnight-play helper (`0x1407c7d30` plays a scene) | VERIFIED |
| **`+0x40`** | i32 | **scene instance id** of the attack midnight now playing (lookup `0x7d5860(game+0x1f5338, id)`: instance `+0x158` id, `+0x16c` rate, `+0x27c` state, 10 = ended) | VERIFIED |
| `+0x80` | ptr | midnight decl being played (set with the id; name string probably `*(char**)(decl+8)`) | VERIFIED ptr / INFERRED name |
| `+0x90 / +0x98` | ptr | sibling comps: AI skills (`DAT_1439b9bc0`, decl `+0x418` = per-slash-type parry-lose midnights) / AI knowledge blackboard (`DAT_1439b3e50`, likely `arkCpntAIKnowledge`) | VERIFIED ptr, class name INFERRED |
| **`+0x138`** | i32 | `state_t`: 0 NONE, 1 IDLE, **2 ATTACK_MELEE**, 3 FLURRY_IN, 4 FLURRY, 5 FLURRY_OUT, 6 RANGED, 7 PARRY_LOSE, 8 COUNTER_ATTACK (strings `arkCpntAICombatAttack::STATE_*` in this order; 2 and 8 confirmed by `0x5cc280`/`0x5d2350`) | order INFERRED |
| `+0x13c` / `+0x140` | i32 | 1 / family(0 melee,1 flurry,2 ranged) set at attack start (`0x5ccf90`); stop writes 0 / -1 | VERIFIED |
| **`+0x144`** | i32 | hit phase: 0 wind-up, **2 after `ATTACK_HIT_CAN_START`** (entity event `0xe8`), 1 = blocked before that, 3/4 after `0x5d0780` | VERIFIED values, meaning INFERRED |
| `+0x150` double, `+0x158` | time | cooldown end | VERIFIED |
| `+0x160` | struct | `arkAIAttackOutcomeInfo`: **`+0x168` attack-tag handle**, `+0x170` midnight descriptor ptr, `+0x178/+0x17c` cooldown min/max, `+0x188` attribute idx | VERIFIED |
| `+0x198` | ptr | `m_attackHitMidnight` decl (non-null for real slashes) | INFERRED |
| `+0x1bc` | u8 | flags: `0x20` blocked, `0x10` scene playing, `0x04`, `0x02` | VERIFIED bits |

Target: `kn = *(void**)(atk+0x98)`; valid `*(u8*)(kn+0x101b)`; target entity id `*(u32*)(kn+0xcdc)` (from `0x62d7a0`: `kn+0x20` is the blackboard, `+0xffb` valid, `+0xcb0` target record, `+0xc` id). Attacking the player = `*(u32*)(player+0xa8)`.

Attack tag identity: the handle at `atk+0x168` is a qword that equals `*(u64*)(base+0x397d9d8)` for `AI_ATTACK_SLASH_LEFT` and `*(u64*)(base+0x397d9e0)` for `AI_ATTACK_SLASH_RIGHT` (`0x3dad50` returns 2 / 1). `arkSlashType_t`: SLASH_NONE 0, **SLASH_RIGHT 1, SLASH_LEFT 2** (strings `0x1dfe7a0..`; same enum the player's slash uses). (VERIFIED)

Entity event ids posted by the attack code through `0x8112c0(bus=0x810240()+0xc8e8, id, size, entityId)` (subscribe `0x810810`, unsubscribe `0x811160`): `0x43` attack started (payload 0x18), `0x44` stopped (1 B), `0xe6` blocked `{u32 victim, i32 res}`, `0xe7` hit `{victim, 0}`, `0xe8` hit-can-start, `0x42` DEFENSELESS_PHASE began (handler `0x84e360`), `0xc4` NPC_VULNERABLE (`0x84e530`). Polling is simpler than subscribing.

### 4.2 Timed event types (enum registered at `0x1a0cd50`, handler table RVA `0x23263c0`, 8 B per type) (VERIFIED)

0 MELEE_WEAPON_ACTIVE_FRAMES `0x84e0c0`, 1 CLOSE_COMBAT_ACTIVE_FRAMES `0x84e300`, 2/3 MELEE_LEFT/RIGHT_WEAPONS `0x84e0c0`, 4 DEFENSELESS_PHASE `0x84e360`, **5 PARRY_PHASE `0x84e3a0`**, 6 DODGE_PHASE (none), 9 NPC_VULNERABLE `0x84e530`, 25 MELEE_HEAD_WEAPONS `0x84e0c0`.

## 5. Attack scenes: names, side, timing (decls under `base/*.index`)

Rule files `components/characters/npc/base/rules/attack_regular.rulehandler` (and `attack_flurry`, `attack_elite`, `worker/nestkeeper/grimalex` variants) hold `arkRuleAction_AIMeleeAttackOutcome` items: `m_attackTag` (`AI_ATTACK_SLASH_LEFT|RIGHT`), `m_midnight` (`midnight/gameplay/human/common/combat/sword_attack_*`), `m_attackHitMidnight` (`*_hit`, played when it connects). Selection uses `attack.anticipation.angle` (+ = target to the NPC's left), `attack.anticipation.distance.xy`, `player.blockCount >= 3` (then kick/punch, **unblockable**), etc.:

| distance / angle | scenes (tag) |
|---|---|
| 3..5 m, angle 70..120 / -120..-70 / 120..180 / -180..-120 | `left_90` (L) / `right_90` (R) / `left_180` (L) / `right_180` (R) |
| 0..1.7 m | `stepback` (R); 1.7..3 m `shorta..e` (R,L,R,R,L) + `fasta` (R) `fastb` (L), random |
| 3..5 m, within 50 deg ahead | `mediuma` (R) / `mediumb` (L); 5..6 m `longa` (R) / `longb` (L); 8..9 m `charge` (R, jump) |

Every scene's timeline carries `MELEE_WEAPON_ACTIVE_FRAMES(slot RIGHT)`, `DEFENSELESS_PHASE`, `ARK_MDNT_MSG_ATTACK_HIT_CAN_START`, `ARK_MDNT_MSG_SLASH_CAN_BE_INTERRUPTED`; `short*` also `AI_DODGE/AI_PARRY_MIDNIGHT_START`. Times in ms from scene start for the **Small** rig. Female is identical except `left_90 760`, `right_90 740`, `right_180 760`, `left_180 780`, `stepback 566`, `charge 827`; Tall starts 20..300 ms earlier (`longa 827`, `shortc 466`, ...) and for `medium*` plays a 500 ms `_in` item first (`mediuma` Tall main item active at 133, i.e. about 633 from attack start). Full per-rig list: `artifacts/claude/block/out/timing-table.txt`.

| scene | tag | active frames | hit-can-start | derived stroke, **player's view** (offline, INFERRED) |
|---|---|---|---|---|
| fasta | R | 360-532 | 399 | chop down |
| fastb | L | 360-560 | 430 | right to left |
| shorta | R | 757-935 | 831 | left to right, rising |
| shortb | L | 758-944 | 830 | chop down |
| shortc | R | 754-946 | 831 | right to left |
| shortd | R | 758-918 | 832 | chop down |
| shorte | L | 760-921 | 831 | rising |
| mediuma | R | 830-1060 | 900 | mixed (down, slightly right to left) |
| mediumb | L | 760-938 | 830 | left to right |
| longa | R | 1130-1350 | 1198 | chop down |
| longb | L | 1060-1360 | 1160 | right to left |
| left_90 / left_180 | L | 667-792 / 753-973 | 700 / 866 | rising / mixed |
| right_90 / right_180 | R | 612-872 / 520-760 | 667 / 633 | mixed / right to left |
| charge | R | 1299-1499 | 1400 | chop down (jump) |
| stepback | R | 533-635 | - | right to left |
| flurry_* | L/R | 66-533 | none | left* strokes right to left, right* strokes left to right |

Derivation: `tools/decode_md6anim.py` + `tools/build_player_asset_pack.parse_skeleton` on the female/low NPC anims (`base_female.md6skl`, 212 joints, `right_hand_attachment_jnt`), hand in a shoulder-midpoint frame, sweep over the ACTIVE window (`artifacts/claude/block/out/attack-table2.txt`). Same tag, opposite strokes (shorta vs shortc), so the tag cannot be the swing side. It looks like the tag selects defence/recoil side tables only (`m_defenderParrySide`, `PLAYER_RECOIL_NPC_PARRY_LEFT/RIGHT`, NPC parry-lose midnight `sword_parry_lose_left/right`).

Timing facts that matter: wind-up to contact 0.55..1.35 s (typical 0.76-0.83 s), contact window 100-330 ms; `DEFENSELESS_PHASE` starts 140-320 ms in. Timings scale with the scene play rate (`instance+0x16c`) and Bend Time.

## 6. NPC weapon (`arkCpntWeaponMeleeNPC`, vtable `0x1b9cf48`, base `arkCpntWeaponMelee` `0x1b9de18`) (VERIFIED unless noted)

Lives on the sword entity (`weapon+0x10` = that entity, `entity+0xac` = owner NPC id, `weapon+0x8` = decl `weaponmeleenpc_sword`). No direct owner-to-weapon link was found; scan entities once (like the player-weapon recipe) and cache by NPC id. Decl (`components/characters/base/weaponmeleenpc_sword.cpntweaponmeleenpc`): `m_damage npc_sword.damage`, `m_blowbackDamage`, `m_weaponMaterial wpn.sword`, `m_parryMaterial wpn.parry`, `m_damageShapeCone.m_height 2`. Fields read by the code: decl `+0x180` u8 m_isBlockable (name INFERRED from adjacent reflection strings), `+0x198` u8 (anim-joint hit mode), `+0x190/+0x194` reach, `+0x128/+0x130/+0x158/+0x168` blow-back damage/impulse/FX.

| off | meaning |
|---|---|
| `+0x3428` u8 | **b0 active frames**, b1 forward attack, b2 heavy block, b3 stop requested |
| `+0x3424` float | damage factor |
| `+0x3430` / `+0x3440` / `+0x3450` | weak refs to owner comps (blackboard / `DAT_1439dc9d0`) / player comp |
| `+0x2298` list | candidate hit records (0x88 B), count `+0x22a0 & 0xffffff` |
| `+0x33e0` vec3 | previous sample position (written by the collector `0x636b40`); `+0x33ec` vec3 previous axis; `+0x33c8` float |
| `+0x345c` i32 | forced target entity id |

Blade geometry (not decoded): the sword entity transform is `entity+0x300` pos, rows at `+0x30c / +0x318 / +0x324`. Swing direction = `normalize(pos - weapon[+0x33e0])` (the engine computes exactly this in `0x634990`). The model's long axis was **not** located statically (swords use `decl+0x198 == 0`, aim vector `0x62c630`: owner-to-player-point `0x62ca70`, where the player point is `player+0x420` vec3). Calibrate live: the entity-row with the largest dot against `(pos - ownerChest)`.

## 7. Recommended per-frame read recipe (game thread, Stage0)

```
// cached once: fsm = player comp with vtable base+0x1c73d58;  camera comp (already used by the mod)
st      = *(void**)(*(char**)(fsm+0x90) + 0x170);     blockActive = *(u8*)(st+0x20);
parryOn = *(u8*)(st+0x58) || *(float*)(st+0x74) > 0;  phase = *(i32*)(st+0x50);
for each alive arkNPC within ~6 m (cache comp pointers by entity id; find atk by vtable base+0x1b93a88):
    state = *(i32*)(atk+0x138);                      // 2 melee, 3..5 flurry
    kn = *(void**)(atk+0x98);
    if (!kn || !*(u8*)(kn+0x101b) || *(u32*)(kn+0xcdc) != playerId) continue;   // not targeting me
    if (state not in {2,3,4,5}) continue;
    sceneId = *(i32*)(atk+0x40);  midnight = *(void**)(atk+0x80);  tag = *(u64*)(atk+0x168);
    side_hint = tag==*(u64*)(base+0x397d9d8) ? LEFT : tag==*(u64*)(base+0x397d9e0) ? RIGHT : NONE;   // hint only
    hitCanStart = *(i32*)(atk+0x144) >= 2;
    w = weapon of this NPC (cached);  contact = *(u8*)(w+0x3428) & 1;          // ground truth for "blade can hurt now"
    P = *(vec3*)(weaponEntity+0x300);  P0 = *(vec3*)(w+0x33e0);  v = normalize(P - P0);   // guard blade path
    t_attack += dt on first sight of a new sceneId (game-clock, not wall clock); expected contact from the table in section 5 (or live: contact flag)
```

Policy, no hooks (fallback): keep RB released unless an attacker satisfies state==2 and t_to_contact < 0.45 s; press RB so contact lands 0.05..0.25 s after the press (perfect-parry window, section 3); hold it only while the VR blade has covered the guard blade path (segment-to-segment distance of the VR blade to `[P0, P + v*0.5]` plus blade axis, < ~0.2 m) and release (>= 0.3 s before contact if it no longer covers; block exit is 266 ms). Limitation: the blade can only be judged from the wind-up, release latency makes late corrections impossible.

## 8. Recommended: hit-frame gate (single inline hook)

| fn | RVA | signature | first bytes (pinned) |
|---|---|---|---|
| NPC weapon hits player | `0x6357d0` | `u64 __fastcall(weapon rcx, hitRec rdx, victim r8)`; returns 1 only if damage applied | `48895c241848896c2420565741564883ec40498bd8488bf2488bf9488d152e84` (18 relocatable bytes before the first rip-relative `lea`) |
| Block cone gate | `0x60d750` | `bool __fastcall(arkCpntBlock* rcx, const float pos[3] rdx)` | `48895c2408488974241048897c241841564883ec50488b59104c8bf2488bf148` (first 15 bytes relocatable) |
| Block resolve | `0x859980` | `i32 __fastcall(st rcx, attacker rdx, u8 heavy r8)` -> 0/1/2 | `488bc4555657415641574881ec9000000048c7442430feffffff488958180f29` |
| Parry window toggle | `0x85a100` | `void(st rcx)` | `807958000f94c088415884c075434863818800000083f8ff7e27488b51104c8b` |
| NPC weapon update | `0x62d7a0` | `void(weapon rcx)` | `40564883ec40f6812834000001488bf10f843201000048895c245048896c2458` |
| NPC per-target loop | `0x62cb80` | `void(weapon rcx, target rdx)` | `48895c2420555657415441554883ec208bb9a02200008b815c340000488be948` |
| SetActive | `0x634890` | `(weapon, on, fwd, heavy, factor)` | `40564883ec300fb68128340000f30f104424604180e1014502c924f04180e001` |
| Attack-comp update | `0x5d0e00` | `void(atk rcx)` | `488bc45556574154415541564157488da8e8feffff4881ec` |
| Hit-can-start handler | `0x5d23f0` | `void(atk rcx)` | `40534883ec208b8144010000488bd985c07510c78144010000020000004883c4` |

Hook `0x6357d0`:
```
using ApplyHitFn = uint64_t(__fastcall*)(void* w, void* rec, char activeCheck, char retarget, float factor, uint64_t extraFlags);
if (victim == player && *(uint8_t*)(st + 0x20) /*Block state active*/ && !bladeCovers(weapon))
    return (*(ApplyHitFn*)(*(char**)weapon + 0x138))(weapon, rec, 1, 1, *(float*)((char*)weapon + 0x3424), 0);   // = vtbl[39], 0x634320, the engine's own unblocked call
return orig(weapon, rec, victim);   // native block, parry and blow-back as usual
// optional second hook on 0x60d750: return true when bladeCovers even if the guard is outside the 40 degree head cone
```
`bladeCovers`: VR blade segment [grip, grip + 0.65*aim] (existing `vr_combat.hpp` model) within ~0.2 m of the guard blade segment or its swept path `[P0,P]` at any frame since the contact flag rose (latch, because the first contact frame may precede the blade crossing). Optional parry: when the VR blade met the guard blade with relative speed above the existing `parry_prior_speed`, write `st+0x58=1, st+0x59=1` just before calling `orig` (the same bytes the engine's `PARRY_PHASE` handler sets; INFERRED safe). After `orig`, read `st+0x50==4`, `st+0x5c`, `st+0x60` for haptics/diagnostics.

## 9. Unknown / to verify live (one session each)

1. `atk+0x138 == 2` for the whole windup (enum order INFERRED) and `atk+0x168` equals the two tag globals during a left/right scene.
2. `*(u8*)(kn+0x101b)` / `*(u32*)(kn+0xcdc)` really are target valid/id (derived from `0x62d7a0`; class of `kn` assumed `arkCpntAIKnowledge`).
3. Weapon entity: `+0x300` follows the hand, and which row of `+0x30c/+0x318/+0x324` is the blade axis; confirm `+0x33e0` is the previous position.
4. Midnight decl name (`*(char**)(decl+8)`) for scene id -> table lookup; otherwise match by pointer after resolving decls with `0x1e030`.
5. Parry cooldown `st+0x70` value and the attribute extra duration; `res==2` roll attribute.
6. Tall rigs: first 500 ms `_in` item for `medium*` (timings are per scene item).
7. Flurry states (3..5) have no `hit-can-start`. Kicks/punches: `CLOSE_COMBAT_ACTIVE_FRAMES` (type 1, handler `0x84e300`) toggles a byte (`+0x48`) on the separate `arkCpntCloseCombat` (vtable `0x1b3b7c8`, decl `m_hitDetectionBox`, `npc_closecombat.damage`), and `arkCpntBlock`'s class info is referenced only by `0x6357d0`/`0x623f80`, so they should ignore block; confirm with one live kick.
