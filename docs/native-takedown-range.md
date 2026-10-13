# Native takedown range / angle parameters (kill, choke, drop)

Steam build 17576867 (`Dishonored2.exe`, base `0x140000000`, offsets are RVAs). Static RE: Ghidra decompiles, decl ctors, `base/game*_patch.index`
decl text. **VERIFIED** = seen in decompile / ctor bytes / decl text. **INFERRED** = deduced, needs a live check.

**Live status.** The game was NOT running when this was written, so no live object was read. The "expected live" columns are
ctor default overlaid with the shipped decl text (VERIFIED statically). One command reads the real values and compares them:
`python artifacts/claude/takedown/live_sensors.py [--json out.json]` (read-only: OpenProcess VM_READ; walks the same player component list as
`src/native_takedown.hpp`). Run it before trusting any number below that is marked "expected".

## 1. What actually gates each takedown

| Takedown | Target comes from | Distance / angle / height gates | Awareness gate |
|---|---|---|---|
| **Choke** (BLOCK hold) | Interaction sensor target (`GetTarget(1)`), or hold target, or shadow-walk target (`0x1403a6460`) | **In the sensor decl**: 3D distance, vertical, signed yaw window vs player forward (`0x1403a6980`) | sensor flags + rule `isUnaware/isVulnerable/...`; no distance in choke rules |
| **Kill** (RT/melee) | Interaction sensor target (`0x1403a1750`); kill sensor only adds a LOS ray | **Not in the kill sensor.** In the assassination rule handler thresholds: `distance <= 3`, `heightDifference <= 1.7` (`assassination_base.rulehandler`) | rule `isUnaware`; directions FRONT/BACK/LEFT/RIGHT are all allowed |
| **Drop** (airborne) | Frustum overlap query below the player, then LOS ray per candidate | **In the drop sensor decl, baked into a physics shape at attach** | none beyond sensor readiness |

* No "behind only" check exists anywhere. `direction` is a 4-way sector of the target's facing (`0x1406dd170`, cos 0.7071 = 90 deg sectors) and choke/kill/drop rules
  have an outcome for all four sectors (decl text VERIFIED). The only angular gate is the choke sensor's window.
* Offered state is decided by `arkCpntPlayerSensorGlobalAssassination` (GA, vtable `0x1b3f998`, decl `0x1b3f838`): per frame it runs `RuleHandler::Evaluate`
  (`0x140895820`) per kind (0 shadow-walk, 1 slide, 2 base kill [all three use the kill sensor], 3 window, 4 drop sensor) and stores the first that passes at
  `GA+0x1d0` (handler ptr, 0 = nothing offered) and `GA+0x1d8` (kind) (`0x1403b1700`, VERIFIED). The mod's `kill` flag (kill sensor `+0x40/+0x48`) is only
  "interaction target exists and ray not blocked": it does not include the 3 m / 1.7 m / unaware rule. `GA+0x1d0 != 0` is the exact kill/drop-offered signal
  (INFERRED that this is safe to read from the poll thread; fields are plain stores).

## 2. Parameters and where they live

Sensor objects are found exactly as in `native_takedown.hpp`. Every sensor reads its decl through `comp+0x08` (decl ptr; `decl+0x68` = sensor-container slot id, VERIFIED
from `0x1403a36a0` / decl ctors). Decl floats start at `decl+0x70`.

### 2a. Choke: component vt `0x1b3e158`, decl `arkDeclCpntPlayerSensorChoke` vt `0x1b3e018` (ctor `0x1403a6350`, reflection `0x141771bc0`)
Decl file `components/characters/player/base/sensors/choke.cpntplayersensorchoke` overrides only `m_minimalVerticalDistance = 1.7`.
Update `0x1403a6980` (job thunk `0x14094fd10`) reads **all** of these from `*(comp+8)` **every frame** (VERIFIED), so a runtime write takes effect next frame.

| decl off | field | expected live | use in `0x1403a6980` |
|---|---|---|---|
| `+0x70` | m_minimalDistanceToTarget | **3.0** | reject if `dist >= value`; dist = 3D player origin to target (bounds-centre x,y; origin z), minus shadow-walk attr extra (`0x1403a66f0`) |
| `+0x74` | ...WhenGrappled | 5.0 | shadow-walk target only: `lerp(+0x78,+0x74,t)` replaces `+0x70` (`0x140001050`) |
| `+0x78` | ...WhenGrappledStopTime | 4.0 | same |
| `+0x7c` | m_minimalVerticalDistance | **1.7** (decl) | reject if `abs(target.z - player.z) >= value` (not for shadow-walk target) |
| `+0x80` / `+0x84` | min / max angle (deg) | **-40 / +40** | window for unaware/suspicious/other targets: signed yaw of (player->target) minus player forward `entity+0x30c`, wrapped to (-180,180] (`0x1403a65c0`; constants 360/180/57.2958 VERIFIED) |
| `+0x88` / `+0x8c` | min / max angle WhenVulnerable | **-50 / +50** | window for aware-but-vulnerable targets (needs `entity+0xd1 & 0x40` or shadow-walk target) |
| `+0x90` | m_anticipationTime | 0.2 | not a gate; leave |

Sensor object (extra to `native_takedown.hpp`): `+0x40` interaction sensor ptr, `+0x48` hold object ptr, `+0x50` target id, `+0x54` shadow-walk flag, `+0x58`/`+0x5c` attribute indices (INFERRED names: SHADOW_WALK_CHOKE_EXTRA_DISTANCE).

### 2b. Kill: component vt `0x1b3cc38`, decl vt `0x1b3caf8` (ctor `0x1403a1620`, reflection `0x141770fc0`); decl file `rassassination.cpntplayersensorassassination` overrides `m_ZOffset = 0.1`
No distance/angle gate in the sensor for a normal (non shadow-walk) kill: `0x1403a1750` returns the Interaction sensor's `GetTarget(0)` (dead targets dropped);
`0x1403a1c90` then casts a thin ray (radius 0.001) from `player.origin + (0,0,ZOffset)` to `target.origin + (0,0,ZOffset)` (read every query) and `0x1403a1b00` sets `obj+0x48 = 1`
if the nearest hit is not the target. Ready (`0x1403a19a0`) = target && `+0x48==0` && FSM state 0 (RUNNING) can be entered (`0x140803400`) && not (state 7 SLIDING active and target AI state in {0,2}, `0x1405bf440`).

| decl off | field | expected live | note |
|---|---|---|---|
| `+0x70` | m_ZOffset | 0.1 | ray height above entity origins. NPC origins are at the feet (`native_takedown.hpp`; player assumed the same, INFERRED), so the ray skims 10 cm above the floor: steps/clutter block it. Raising it (e.g. 0.6) reduces false "blocked"; read every query (VERIFIED) |
| `+0x74` | m_collisionMask | 0x2b | do not touch |
| `+0x78` `+0x7c` `+0x80` | grappledMaxDistance 5.0 / StopTime 4.0 / grappleMaxAngle 50 deg | defaults | shadow-walk (Emily power) target only (`0x1403a1750`) |

**The real kill range** (VERIFIED decl text, `components/characters/npc/base/rules/assassination_base.rulehandler`, 24 rules):

| rules | conditions | note |
|---|---|---|
| stop-time f/b/l/r, bend-time f/b | `player.isStopTimeActive`/`isBendTimeActive` and `distance <= 3` and `heightDifference <= 1.7` | |
| slow f/b/l/r | `isUnaware` and `numNpcsNearby == 1` and `distance <= 3` and `height <= 1.7` | |
| **fast f/b/l/r** | `isUnaware` and `distance <= 3` and `height <= 1.7` | the normal stealth kill |
| fatality (near death / vulnerable) | `distance <= 3`, `height <= 10` | other gameplay, leave |
| ground fatality f/b/l/r | `distance < 2`, `height < 0.5`, on ground | leave |
| grapple f/b/l/r | `distance <= 5` | leave |
| shadow-walk handler | `distance <= 3` (8 rules); slide handler `distance <= 4` | |

`assassination.target.distance` = 3D distance, player origin to (target bounds-centre x,y; origin z), minus shadow-walk attr extra (`0x1403b05f0`, VERIFIED);
`heightDifference` = `abs(player.z - slot.z)` (`0x1403b0730`, VERIFIED). Both are measured from the **player entity origin**, not the camera.

Interaction sensor (feeds kill and choke target): comp vt `0x1b403b8`, decl vt `0x1b40278`: decl `+0x70` m_maxDistance = **3.0** (ray length, read per frame, shared with doors/items:
do not scale), `+0x74..+0x84` m_shapeRadius[5] = 0.005, 0.05, 0.1, **0.3** (decl override index 3), 0.7. The sweep spheres are created once at attach (`0x1403b82c0`), so
changing these floats later does not change the sphere (BAKED). NPC bodies are found by categories 0..3 into `+0x88` (the id kill/choke use); category 4 (r 0.7) only fills `+0x8c`. The ray
starts at the camera (`view+0x2527c`, dir `+0x25288`): this is the aim the mod already steers.

### 2c. Drop: component vt `0x1b3f0a8`, decl vt `0x1b3eef8` (ctor `0x1403aa1c0`, reflection `0x1417724c0`); decl file `dropassassination.cpntplayersensordropassassination`
Attach (`0x1403aaa20` -> `0x1403aa590`) builds an 8-vertex convex hull **once** from decl `+0x70..+0x88` and stores the physics shape at `obj+0x40`
(local x = player forward, top face at player origin z, bottom face `shapeHeight` below). **Changing these decl floats later does nothing** (BAKED, VERIFIED). The shape is
re-queried every frame at `(player.xy, player.z - decl+0x8c)` with the player's axes (`0x1403ac4d0`).

| decl off | field | default | decl file | expected live | read |
|---|---|---|---|---|---|
| `+0x70` | m_shapeTopLength (forward) | 1.0 | 4 | **4** | baked |
| `+0x74` | m_shapeTopWidth (lateral) | 1.0 | - | **1.0** (only +/-0.5 m at the top) | baked |
| `+0x78` | m_shapeTopXOffset | 2.0 | - | 2.0 | baked |
| `+0x7c` | m_shapeBottomLength | 5.0 | 7 | 7 | baked |
| `+0x80` | m_shapeBottomWidth | 4.0 | 7 | 7 | baked |
| `+0x84` | m_shapeBottomXOffset | 0 | 1.5 | 1.5 | baked |
| `+0x88` | m_shapeHeight | 10 | 20 | 20 | baked |
| `+0x8c` | m_shapePositionZOffset | 2.0 | 0 | 0 | every frame |
| `+0x90` | m_jumpOriginMinimumHeight | 1.5 | 0.5 | 0.5 | every frame (`0x1403aa920`): player must be >= this above target origin (jump origin z, `state+0x78`) while JUMPING/FALLING |

Runtime: `+0x40` shape, `+0x48` shape-query id, `+0x50/+0x58` candidate ids/count, `+0x70/+0x78` visible (LOS-clear) list the mod reads (first id = target), `+0xa0/+0xa8` raw LOS queries.
Ready (`0x1403abf70`): visible list non-empty && enabled && FSM state 2 (JUMPING) or 3 (FALLING) active (state indices = `PLAYER_STATE_*` string order, matches `crouch-re`: 6 SNEAKING, 13 ASSASSINATION).

### 2d. Other float that can matter
Player slash state decl (`arkDeclPlayerStateSlash`, vt `0x1c85bf8`, ctor `0x14086a710`): `+0xe8` = **2.0** m = max distance for the *fallback* melee-to-assassination path used when GA offers nothing
(`0x14086b050`: `dist^2 >= +0xe8^2` rejects); `+0xec` = 10. Not on the normal path; optional.

## 3. Recipe (what to scale, how far, how to restore)

All writes are plain aligned 4-byte stores on the game heap, no engine call. Apply only when the current value equals the expected original (idempotent, and skips if another mod
or an earlier apply changed it); re-apply after every `NativeTakedowns::scan` (new player / level) because decls can be rebuilt; restore = write the original back (or when current == scaled).

| # | target | write | multiplier | why / clamp | status |
|---|---|---|---|---|---|
| 1 | choke decl `+0x70` | 3.0 -> 4.5 | x1.5 | when the body origin trails the HMD (INFERRED VR failure mode) the 3 m gate (from origin) trips before the 3 m interaction ray (from camera). Clamp <= 5 (choke scene is authored for <= 3 m; test the approach snap in the headset) | field VERIFIED, value effect needs headset test |
| 2 | choke decl `+0x7c` | 1.7 -> 2.55 | x1.5 | stairs/ledges; clamp <= 3 | VERIFIED |
| 3 | choke decl `+0x80,+0x84` | -40,40 -> -60,60 | x1.5 | window is vs the **entity** forward (`+0x30c`), the interaction target is chosen by the **view/hand ray**; if head and body headings differ by 40-50 deg the gate fails (INFERRED VR failure mode). Keep min<0<max, clamp \|x\| <= 120 (>=180 disables the gate) | VERIFIED |
| 4 | choke decl `+0x88,+0x8c` | -50,50 -> -75,75 | x1.5 | same, vulnerable path | VERIFIED |
| 5 | kill rule thresholds in the `assassination_base` handler | `<=3.0` -> 4.5 and `<=1.7` -> 2.55 | x1.5 | float condition instances (vt `base+0x1ca2140`): operator `+0x28`, **threshold `+0x2c`** (read every evaluation, `0x1408ba770`, VERIFIED). Reach them from GA: bucket array `GA+0x190` (count `GA+0x198`), node `+0x28` = handler (`+0x20` rule-instance array, count `&0xffffff` of `+0x28`), rule instance `+0` cond array / `+8` count. **Only touch rules whose float conditions are exactly {3.0, 1.7}** (kill rules; fatality has {3, 10}, ground {2, 0.5}, grapple {5}). Rule order may be re-sorted at instantiation, so match by values, not index | location INFERRED: confirm with `live_sensors.py` |
| 6 | kill sensor decl `+0x70` (ZOffset) | 0.1 -> 0.5 (optional) | - | cuts "blocked" flicker from floor clutter; ray goes chest-ish instead of ankle-ish. Changes what blocks a kill (a low wall no longer blocks) | VERIFIED field; gameplay judgement |
| 7 | drop decl `+0x74` topWidth 1.0 -> 2.0, bottom dims | needs shape rebuild | - | BAKED. Rebuild = write the decl floats, then on the game thread (Stage0) `old=*(obj+0x40); 0x1403aa590(obj); (*(*old+8))(old,1)`; same sequence as the native attach/detach (`0x1403aaa20`/`0x1403aaa50`). Not recommended for v1: raises cost/risk (physics shape lifetime, query job may hold the old shape) | INFERRED safety |
| - | Interaction decl `+0x70` / radii | **do not scale** | - | shared by every use prompt (doors, items); radii baked | VERIFIED |

Shared vs instance: choke/kill/drop decls are referenced only by the player's sensor components (instance reads `*(comp+8)`, holds no copy), so scaling the decl **is** scaling the instance
and affects nothing else. Rule thresholds are per-instance copies of the decl condition (`cond+0x18` -> `inst+0x2c`, ctor `0x1408b42f0`): scale the instance copies; the decl copy is only
re-read if the handler instances are rebuilt (GA attach).

## 4. Limits of the recipe (read before shipping)
* Kill reach is still capped by the interaction ray (3.0 m from the **camera**) and its aim spheres (r 0.3). Items 1-5 recover the VR-specific loss (origin vs camera offset, head vs body heading),
  they do not turn a 3 m kill into a true 4.5 m kill. A real extension needs the shared interaction ray, which is out of scope ("no other gameplay changes").
* Awareness is untouched on purpose: kill `isUnaware`, choke `isUnaware/isVulnerable/isAlwaysChokable/isSitting/isGrappled`, drop none.
* Visual comfort of longer choke/kill approach snaps is not established by any automated check.
* Evidence index: `artifacts/claude/takedown/NOTES.txt` (decompiles in `out/`, decl texts in `decls/`, scripts in `scripts/`).
