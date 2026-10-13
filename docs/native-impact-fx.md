# Native surface impact FX (sword vs world): the ContactSystem

Pinned PE: Steam build 17576867, image base `0x140000000`, all RVAs relative. Static analysis only (capstone/pefile scans of the exe, Ghidra 12.1.4 decompiles, decl extraction from `base/game*_patch.index`). Nothing is hooked or called; no live run. Raw notes, scripts, decompiles and extracted decls: `artifacts/claude/impact/` (`notes.md` is the index). VERIFIED = bytes/decompile seen; INFERRED = deduced, needs a live check.

## Verdict

The engine has one shared spawner for every surface contact effect (sword on wall, arrows, bolts, footsteps, props): the **ContactSystem** manager. Melee, projectiles and footsteps all build a small 0x50-byte *event* and call `Manager::Play(event)` (`0x60a8b0`). The manager picks an FX decl from a database keyed by **(material A, material B)** (here `contactsystem/wpn.sword` x the surface's physics material), and starts that FX (sparks/dust particles + the impact sound). There is no separate decal for sword hits (the sword FX decls only carry `m_particleSystems` + `m_sounds`).

The mod can call it directly from the Stage0 hook with data it already has: hit position and normal from the `0xc89bc0` collector, the surface material from the collector's u16 at `+0x7c` (already in the struct as `body_key`, dropped by `convert_world_hit`), the sword material from the equipped melee component's decl. Confidence that this recipe works end to end: about 80 percent (details under Risks).

## Engine path (VERIFIED unless marked)

| Step | RVA | Evidence |
|---|---|---|
| Physics contact on the weapon body, `arkCpntWeaponMelee` vtable slot 34 (`+0x110`): base `635c90`, Player override `62fec0` (falls back to base) | `635c90`, `62fec0` | decompile; vtable qwords `0x1b9df28` / `0x1b9d618` (and NPC `0x1b9d058`) |
| Contact record read by it: `+0x10` matA ptr, `+0x20` other object (`+0x10` = entity), `+0x28` matB ptr, `+0x30` pos, `+0x3c` normal, `+0x48` velocity | `635c90` | decompile |
| Build event `ev_ctor(evt, owner, matA, matB, &pos, speed=\|vel\|, &normal, &dir, 1.0, flag)` | `60a480` | decompile + 130 bytes disasm |
| Get manager: `game->vtable[+0x390]()` = `lea rax,[rcx+0x1f8370]; ret`, i.e. **manager = game + 0x1f8370** | `8a8f60`, vtable `0x1c94438` | bytes; manager ctor `60a540` called with `game+0x1f8370` at `8a3ae3` |
| `Play(mgr, evt)`: skip if `*(owner+0xd2) & 0x80`; DB lookup; allocate FX instance; fill; spam-limit; queue | `60a8b0` | full decompile |
| Then (not needed by the mod): engine posts message `0x1aa` to the *other* entity (AI hearing / gameplay), only in `635c90`/`62fec0` | `635c90` | decompile; we skip this on purpose |
| FX instances are started by the FX manager tick: `1a3390` -> `1a16d0` drains the queue -> `222f80` starts particle/sound/light/decal groups | `1a16d0`, `222f80` | decompile |

`62f610` is a second melee-component hit handler (hit record: pos `+0x10`, normal `+0x1c`, velocity `+0x28`; same Play call with flag 1, f40 0). The engine passes flag 1 when the holder is the player (`635c90` checks the holder's type). The mod's virtual sword has no engine physics contact, so we replay the tail of this path ourselves.

## Surface type from a physics hit (VERIFIED for ray casts, INFERRED for the shape cast)

`materialPtr = *( *( *( *(game+0x60410) + 0x988 ) + 0x30 ) + 0x60 + materialId*0x70 )` (`92c10`, 31 bytes, `rcx = game+0x60280`; `[rcx+0x190]` is the same `World*` the mod already uses). `materialId` = u16 at **collector `+0x7c`** (`NativeClosestCollector::body_key`). Evidence: the engine's own projectile impact code (`6b9ab0` -> `6ba540`) fills a closest-hit collector (vtable `0x1ad5010`, ray cast `c89b00` = `"TtWorldCastRay"`) and does exactly `FUN_140092c10(game+0x60280, *(ushort*)(collector+0x7c))` to get matB. `c89bc0` is `"TtWorldCastShape"`, and both collector slots (`ca53a0`, `ca5460`) copy hit-record `+0x4c` verbatim to `+0x7c`, so the sphere cast fills the same field. INFERRED only in that nobody has read it live after a sphere cast.

Result is an `arkPhysicsMaterial*` (vtable `0x1ad3848`, RTTI `.?AVarkPhysicsMaterial@@`) = the decl `contactsystem/<name>` (e.g. `env.stone`, `env.metal`, `env.wood`, props `po.*`). Material ids with no material give 0/garbage: check the vtable before use. Body handle + shape key are **not** needed; the collector fields are sufficient. A null matB makes `Play` a no-op (`0x2c60` returns 0), so a bad lookup cannot crash it, but a wrong non-null pointer can.

## ABI

All functions are plain MS x64, no hooks needed, no `this` adjustors.

**Event** (`NativeContactEvent`, size 0x50, align 8; the engine allocates 0x70-0x80 on the stack; `998b0` is a `ret` so there is no destructor):

| Off | Type | Field | Source / use |
|---|---|---|---|
| 0x00 | `idEntity*` | owner | Play reads `*(owner+0xd2)&0x80` (suppress all) and `*(u32*)(owner+0xa8)` (entity id stored in the FX instance). Must be a valid entity. Engine passes `*(meleeComponent+0x10)` (the sword entity). |
| 0x08 | `arkPhysicsMaterial*` | matA | weapon material = `*(*(meleeComponent+8)+0x138)` (decl `m_weaponMaterial`, `contactsystem/wpn.sword`) |
| 0x10 | `arkPhysicsMaterial*` | matB | surface material (above) |
| 0x18 | `float[3]` | pos | world contact position |
| 0x24 | `float` | speed | impact speed; stored at FX ctx `+0xb8` (FX instance `+0xf0`). `<0` disables the owner callback. Real melee passes `\|velocity\|` (m/s, INFERRED) |
| 0x28 | `float[3]` | normal | surface normal (FX orientation axis; unit, outward INFERRED) |
| 0x34 | `float[3]` | dir | unit travel direction; only used for yaw when `\|normal.z\| >= 0.99` (floor/ceiling); zero vector is fine (many engine callers pass `0,0,0`) |
| 0x40 | `float` | f40 | stored at FX ctx `+0xbc`. Engine: 1.0 in the contact handlers `635c90`/`62fec0`/`635f30`, 0 in `62f610` and projectile/footstep callers |
| 0x44 | `u32` | mask | always `0x1ffff` (ctor writes it; `Play` forwards it to the FX queue) |
| 0x48 | `u8` | flag | 1 = also install the owner-complete callback `0x439480` -> `6dd740` (queues a 0x10-byte message `0xc6`, probably AI noise); 0 = no callback. **Use 0.** (INFERRED purpose) |

`Event* __fastcall ev_ctor(Event* RCX, void* owner RDX, void* matA R8, void* matB R9, const float* pos [rsp+28h], float speed [rsp+30h], const float* normal [rsp+38h], const float* dir [rsp+40h], float f40 [rsp+48h], uint8_t flag [rsp+50h])` at `0x60a480` (leaf function, copies fields, returns RCX; the mod may also just fill the struct itself).

`void __fastcall Play(Manager* RCX, const Event* RDX)` at `0x60a8b0`. Returns nothing useful.

Inside `Play` (decompile): `DB = *(mgr+0x10)` must be non-null (set by `60a860` on level load; null in the main menu); `fx = Lookup(DB, matA, matB)` (`002c60`: binary search over a table keyed by material pointer, walking **matA's parent chain `+0x48` outermost and matB's parent chain innermost**, so an unlisted `env.stone.marble` still finds `wpn.sword_stone`); needs `game[0x3d12b]` (`game+0x1e8958`, an entity-like object with id at `+0xa8`) non-null; FX manager = `game->vtable[+0x388]()`; builds the FX instance (`19d100`, pos at `+0x38`, 3x3 orientation from `normal` at `+0x44`, speed `+0xf0`, f40 `+0xf4`, owner id `+0x110`); per-FX concurrent-instance cap via `60af50` (oldest is stopped; prints `"too many unique contact"` if the map is full); `1a2ff0` appends it to the FX start queue. Failure modes are silent (no FX).

## What a sword-on-wall hit plays (VERIFIED from extracted decls)

`wpn.sword` physics decl `inherit contactsystem/sword`; FX decl name = `<matA>_<matB>.fx` (the `default_db.decl` fxTable has 7407 such entries, about 125 of them `wpn.sword_*`). Examples (`artifacts/claude/impact/sword_env_fx_table.txt` has all 58 `env.*` surfaces; it is a reconstruction of the lookup order, INFERRED for rows where the parent chain decides):

| Surface (matB) | FX picked | Particles | Sound event (`sound_events/dishonored/physmat/impact/imp_sword/`) |
|---|---|---|---|
| `env.metal*` | `wpn.sword_env.metal` | `sword_metal.particledt` (flash + spark clusters + trail sparks) | `imp_sword_on_metal` |
| `env.stone*`, concrete, marble | `wpn.sword_env.stone` / `wpn.sword_stone` | `sword_stone.particledt` (dust/chips) | `imp_sword_on_stone` |
| `env.wood*` | `wpn.sword_env.wood` | `sword_wood.particledt` | `imp_sword_on_wood` |
| `env.dirt/gravel/mud/sand/grass/tile/carpet/glass` | own FX each | `sword_<name>.particledt` | `imp_sword_on_<name>` |
| `environment` (generic) | `wpn.sword_environment` | `sword_dirt.particledt` (dust) | `imp_sword_on_dirt` |
| `genericcontact` | `wpn.sword_genericcontact` | none | `bsp/.../bsp_imp_sword_on_dirt` (sound only) |
| props `po.*` | `wpn.sword_po.<x>` (about 90 variants) | per prop | per prop |

Each FX has fixed particle bursts (no speed-driven emitter fields seen in `sword_metal.particledt`). The only verified use of speed/f40 is the **sound**: the sound start routine (`22fb2c` chunk, `0x22fbdc`) computes `speed*speed*f40` from the FX ctx and feeds it into a sound parameter (RTPC-like call `0xc5c60`); what the Wwise bank does with it is not visible statically. So: no verified "particle intensity" knob.

## Intensity and "lighter" options

| Option | How | Status |
|---|---|---|
| Quieter impact sound | lower `speed` and/or `f40` (energy = speed^2 * f40) | VERIFIED formula, effect on loudness INFERRED |
| Sound only, no particles | matB = `contactsystem/genericcontact` (FX `wpn.sword_genericcontact`) | VERIFIED decl; resolve by name (below) |
| Dust only, no sparks on metal | matB = `contactsystem/environment` or `env.dirt` | VERIFIED decl |
| Fewer events | mod-side rate limit + speed threshold; engine already caps concurrent instances per FX decl | VERIFIED cap (`60af50`) |
| Different weapon feel | matA = `contactsystem/sword` (generic, uses bolt-metal particles) or `wpn.pipe` (blunt) | decls VERIFIED, resolve by name |

There is no dedicated scrape/graze table for swords. `slide`/`slideimpact` are the player slide footstep materials. Sparks cannot be thinned except by rate and by choosing a different FX pair.

## Thread and safety

| Aspect | Finding |
|---|---|
| Callers | the 19 engine callers of `Play` are weapon-component, player-physics, projectile and throwable update functions (game-thread logic, INFERRED from context) |
| Locks | FX instance allocation (`19d100`), start queue (`1a2ff0`) and the per-FX contact map (`60af50`) are guarded by atomic spin locks (`1581c0` = `lock cmpxchg`, `1581e0` = `xchg`) VERIFIED |
| Unguarded | reads of the owner entity, `game` singleton tables, `mgr+0x10` DB (replaced on map load), decl lookup |
| Recommendation | call only in the Stage0 game-thread scope with the same re-check discipline as `service_obstruction`; never from render/XR threads. The FX actually starts later at the FX manager tick, so latency is one frame |

## Pinned code bytes (first bytes, little-endian hex)

| Function | RVA | Len | Bytes |
|---|---|---|---|
| `ev_ctor` | `60a480` | 48 | `f30f10442430488911488b5424284c8bd14c8941084c8949108b028941188b420489411c8b4208894120f30f11412448` |
| `Play` | `60a8b0` | 48 | `4c8bdc55574155498d6ba84881ec40010000488b02488bfa4c8be9f680d2000000800f855c040000f30f105230f30f10` |
| `material_of_id` | `092c10` | 31 | `488b81900100000fb7ca488b8088090000486bc970488b4030488b440860c3` |
| `get_manager` | `8a8f60` | 8 | `488d8170831f00c3` (game vtable qword `0x1c94438 == base+0x8a8f60`) |
| `FindDecl` (optional) | `01e030` | 48 | `44884424185556574154415541564157488d6c24d94881ec0001000048c745cffeffffff48899c2440010000410fb6f9` |
| `sphere cast` | `c89bc0` | 48 | already pinned in `native_world_trace.hpp` |

Object type checks: manager `*(u64*)mgr == base+0x1b98d90`; `arkPhysicsMaterial` `*(u64*)mat == base+0x1ad3848`; melee component `0x1b9d508`, its decl `0x1b9d3c8` (both already checked by `native_equipment_ownership.hpp`).

## Recommended recipe

1. Extend `WorldTraceHit` with `material_id = collector.body_key` (u16, `0xffff` = none) in `convert_world_hit`. Keep `point` and the flipped (free-space-facing) `normal`.
2. Once per session in the Stage0 scope: `verify`: code bytes above, game vtable `+0x390` slot, `mgr = *(base+0x39a5290) + 0x1f8370`, `*(u64*)mgr == base+0x1b98d90`, `*(u64*)(mgr+0x10) != 0`.
3. Per accepted hit (new contact, or scrape every >= 150-250 ms while speed > threshold; blade tip speed from the tracked pose delta; skip when the hit entity is an NPC/flesh so blood/damage stay engine-driven):
   ```
   matB  = *( *( *( *(game+0x60410)+0x988 )+0x30 ) + 0x60 + id*0x70 )   // validate vtable 0x1ad3848
   matA  = *( *(equipment+8) + 0x138 )                                     // validate vtable, or FindDecl
   owner = *(equipment+0x10)                                               // sword entity, check *(owner+0xd2)&0x80 == 0
   ev_ctor(&e, owner, matA, matB, pos, speed*scale, normal, dir_or_zero, f40, 0);
   Play(mgr, &e);
   ```
4. Start with `scale` about 0.35 and `f40` 0.5-1.0, flag 0. Tune by ear; log `*(owner+0xd2)`, `materialId`, `matB` once.
5. Optional name-based materials (for the lighter options): `FindDecl(base+0x2435520, "contactsystem/genericcontact", 0, 0)` (rcx = decl-type object of `physicsMaterial`, rdx = name, r8b = r9b = 0), same call shape the engine uses at `8a7a90`. Game thread only. INFERRED that the returned pointer is directly usable as matA/matB (the decl field `m_weaponMaterial` holds the same decl pointer).

## Alternatives if the direct call is too risky

| Alternative | Detail | Verdict |
|---|---|---|
| Console `TestFx <fxDeclName>` | handler `781d00`, `"tests a fx impact"`; spawns an `idEntityFx` (`func/fx`, entity name `testFxEntity`) at a ray hit from the player view (INFERRED from the call sequence `88e140`, `95130`, `93000`), pushed along the normal by cvar `g_testFxNormalOffset` (default `0.5`). The fx decl is looked up by name (`1e030` with the fx decl-type object `0x32cefc0`), names like `contactsystem/wpn.sword_env.metal.fx` (INFERRED). | Only works at the head/crosshair ray, not at the blade; use `buffer_command` from `native_console.hpp`. Poor fit, usable as a smoke test of the FX decls and sound |
| Direct entity spawn | `8ae9b0` (spawn with args) -> `8f2f90` -> `87eb80(fxEntity, fxDecl, 0)` as in the tail of `781d00` | Entangled (spawn-args dict, class name `idEntityFx`), not investigated further |
| No `spawn`, `fx`, `playSound` console commands exist | enumerated all 113 registered commands: `artifacts/claude/impact/commands.txt` | confirmed absent |

## Risks and unverified

1. Collector `+0x7c` after a **sphere** cast is assumed to carry the same material id as after a ray cast (shared copy code, not read live). Also whether the id is per-triangle for compound static meshes.
2. `*(owner+0xd2) & 0x80` silently suppresses everything; its meaning is unknown (the sword entity in VR may differ from the engine's weapon).
3. `speed` units (m/s) and the exact loudness response are INFERRED; particle intensity is not tunable.
4. Normal orientation expected outward (engine passes contact normals); the mod's flipped normal faces free space, which matches. Not seen live.
5. The AI side channel (flag 1, message `0x1aa`/`0xc6`) is deliberately skipped, so scraping will not alert guards; decide if that is wanted before enabling flag 1.
6. Audio/particle appearance in the headset, comfort and gameplay balance are unaddressed.
7. If `Play` is ever reached with a freed/foreign `owner` or `matB`, it dereferences them; keep the Stage0 re-checks and vtable checks.

Reproduce: `artifacts/claude/impact/scripts/` (`d2re.py` helpers, `declx.py` decl extractor, `ghidra-decomp.cmd` + `D2ImpDecomp.java`, `cmds.py`, `fxtable.py`); Ghidra outputs in `artifacts/claude/impact/ghidra/` (`core.c` = Play/ctor, `callers.c` = 19 callers + DB lookup, `mat.c`/`spawn.c` = material lookup and FX instance allocation).
