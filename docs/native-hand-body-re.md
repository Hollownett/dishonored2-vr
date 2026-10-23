# Hand-body static RE: userdata contract, shapes, cinfo, filters and queue (Plan B, task B6)

Plan B makes each VR hand a real Havok (hknp) body, the way Bonelab does it: the hand is a physical body pulled toward
the controller by clamped forces (Bonelab §2.1). Bonelab's hand also lags when blocked (§2.2). This document is the
static reverse engineering every later hand-body task depends on. No encoder, creation call, userdata layout or filter
word may be written from a guess. Anything not proven here is tagged [U] and has a runtime probe for B7 or B11.

Scope: research open items 1, 3, 7, 8, 9 and 11 from `docs/superpowers/specs/2026-10-18-physical-hands-research.md`.
Item 2 (the player bodies' live filter words) is touched only where it bears on Q1.

## Conventions

* **Build.** `Dishonored2.exe`, SHA-256 `b671f8249422862bf557d00aab2f4bb671ed4515cc10ad8a8a9decad84516180`, image
  base `0x140000000`. Every address is an RVA unless it says otherwise.
* **Number notation** (`tests/hand_body_re_doc_check.py` relies on it):
  * An RVA is written as a bare `0x` followed by 5 to 7 hex digits.
  * A structure offset always carries a leading `+` (`world+0x470`, `arkPhys+0x148288`).
  * A constant of 5 or more hex digits is zero-padded to 8 digits (`0x0001ffff`).
  * A global that only exists at run time (it has no file bytes, because it is in `.bss`) is written as an absolute
    VA with 9 digits (`0x1439e8610`).
* **Pins.** Every RVA cited in a Q section is pinned in that section, either by a `bytes:` line or by `known` plus
  the mod file that already pins it.
  * A `bytes:` line gives the first 16 file bytes at that RVA, taken from the PE file. For a vtable or a data table
    these are the stored values, which are preferred-base VAs.
  * The check compares every `bytes:` line with the exe when the exe is present.
* **Tags.**
  * **[V]** verified: this document re-derives it from a decompile, the disassembly or the file bytes.
  * **[M]** mod source or mod run evidence, with file:line.
  * **[I]** inferred from verified facts.
  * **[U]** unknown; a runtime probe is given.
  * A research claim is marked [V] here only when this pass re-derived it.
* **Excerpts.** Every excerpt comes from `tools/D2HandBodyQ.java`, run headless and read-only on the analysed
  project (no re-analysis):

  ```
  analyzeHeadless <proj> D2 -process Dishonored2.exe -noanalysis -readOnly -scriptPath tools \
      -postScript D2HandBodyQ.java <out.txt> <commands.txt>
  ```

  * Each excerpt is cited as `D2HandBodyQ <op>:<rva>`. For example, `D2HandBodyQ d:0x93a40` is the decompile of the
    function containing 0x93a40, and `a:` lists the instructions leading up to each call.
  * The raw outputs of this pass (33 batches) are kept in the git-excluded plan workspace,
    `.superpowers/sdd/2026-10-20-bonelab-feel-b-hand-body/b6-evidence/`.
  * No game or simulator was run.
* **Names used below.**
  * `game` = `*(base+0x39a5290)`.
  * `arkPhys` = `game+0x60280` (arkPhysicsWorldLocal).
  * `world` = `*(arkPhys+0x190)` = `*(game+0x60410)` (arkNpWorld, the hknpWorld).
  * `body` = `world+0x28` pool record (stride 0xb0).
  * `ud` = `*(body+0x90)` (the userdata).
  * `owner` = `*(ud+8)`.

## Q1 Userdata contract (open item 1)

Verdict: [U]

The fields and vtable slots that engine code reads through `body+0x90` are now fully enumerated for:

* the filter;
* the step;
* the contact events;
* the touch notifications.

All of that is [V]. What cannot be settled statically is the query consumers (AI, weapons, Use, projectiles). They
resolve a hit body to `ud->owner->entity` and then call into the entity. So the *safe minimum* is an engine-shaped
userdata plus a **real** owner component and a **real** entity, and which real owner B11 should use is [U]. A fully
fake owner or entity cannot be shown safe statically.

### 1.1 Object shapes [V]

* **Userdata.** It is an `arkHavokUserData`.
  * The base ctor is 0x18cb90 (`D2HandBodyQ d:0x18cb90`). It writes:

    | Offset | Value |
    |---|---|
    | +0x00 | vtable 0x1aff038 |
    | +0x08 .. +0x58 | zero (+0x18 is not written) |
    | +0x60 | pointer to the inline array at +0x70 |
    | +0x68 | count 0 |
    | +0x6c | `0x80000002` (two inline entries, not heap) |

    The inline storage ends at +0x80, so the object is at least 0x80 bytes.
  * The base dtor body 0x18cbd0 frees +0x60 only when the capacity word is not negative.
  * Every engine creator stores the owner with 0x694c20 `(ud, owner)`, a single store `ud+8 = owner`, and puts the
    userdata into the cinfo with 0x18cca0 `(ud, cinfo)` (`cinfo+0x20 = ud`). Body init copies `cinfo+0x20` to
    `body+0x90` (0xca6810: `*(body+0x90) = cinfo[4]`).
  * The player follows this pattern exactly (`D2HandBodyQ d:0x1b2290`):
    1. 0x18cb90 on `this+0x160`;
    2. it sets the vtable to `arkPlayerUserData` 0x1b02768;
    3. 0x694c20 `(this+0x160, <component>)`.
  * The rigid-body component does the same with `component+0x100` (ctor 0x806de0 sets its vtable 0x1c74578).
    Its create routine 0x808ea0 calls 0x694c20 `(component+0x100, component)`.
* **Base vtable 0x1aff038** (`D2HandBodyQ v:0x1aff038`):

  | Slot | Function | What it does |
  |---|---|---|
  | +0x00 | 0x801380 | dtor |
  | +0x08 | 0x998b0 | `RET` |
  | +0x10 | 0x11d080 | `MOV AL,1; RET` |
  | +0x18 | 0x998b0 | `RET` |
  | +0x20 | 0x6d040 | `XOR EAX,EAX; RET` |
  | +0x28 | 0x181a40 | `OR EAX,-1; RET` |
  | +0x30 | 0x6d040 | `XOR EAX,EAX; RET` |
  | +0x38 | 0x6d040 | `XOR EAX,EAX; RET` |

  Every slot that engine readers call is a harmless stub.
* **Owner.** For `arkCpntRigidBody` the owner is the component itself (0x808ea0) [V]. The player's owner is the second
  argument of its ctor 0x1b2290 (type not traced, [I] a component). Every reader in 1.2 and 1.3 treats `owner+0x10` as
  an entity
  (the mod already relies on `component+0x10 == entity`: `src/native_rigid_body.hpp:48`). The owner's vtable
  slot 0 is `GetType()`. Callers read `typeinfo+0x60` and compare it with a typenum range, which is idTech's
  `IsType`.

### 1.2 Every engine read through `body+0x90` outside the query consumers [V]

The `ud` accessors are 0x17d10 (`return *(body+0x90)`) and 0x18cd00 (`handle -> pool -> +0x90`). These are the readers
reached by the filter, the step and the contact events:

| Reader | Condition | What it touches |
|---|---|---|
| Pair filter 0x93a40, from 0x992c0 | every candidate pair that passes the mutual layer test | `ud[1]` of both bodies. Only if the owners are equal does it call `udA->vtbl+0x20` (base: returns 0, so no self-collision matrix) and `+0x28` |
| Query filter 0x99710 | every body whose layer passes the query's layer mask and required bits | `ud+8` compared with the query source's owner. Equal owners are skipped |
| The mod's cast filter 0x996a0 (`filter_body`, known, `src/native_world_trace.hpp:204`) | every body | **no userdata read**: `(queryLayerMask >> body.layer) & 1` and `(body.mask & required) == required` only. One-way, so the mod's own casts (`kWorldOccluderLayers` = `0x0000c43e` includes DEBRIS 4, `src/native_world_trace.hpp:106`) **will hit the hand** unless they exclude its handle with the existing source-body exclusion |
| Time dilation (Bend Time) in the step 0x97bc0 and 0x851a70, via 0x18d2b0(`ud+0x20`, …) | the body's motion-properties id is set in a dilation bitset at `arkPhys+0x148320` | reads and writes `ud+0x20..+0x5f` (cached velocities); `ud+0x4c` flag bits |
| Dilation reset 0x94280 and 0x99a00 (0x18cf30 / 0x18d420 on `ud+0x20`) | the same | the same region |
| Impulse commands 0x3c / 0x3d: 0x91270, 0x90fc0 | when an `applyBodyImpulse…` record names the body | `ud+0x40..+0x4c` |
| OnContactImpulse 0x8527e0, registered by 0x852400 (§2.5 of the research) | a contact-impulse event involving the body is raised. The started branch (event `+0x18 == 1`) also needs `arkPhys+0x1883e8 == 0` | see the next table |
| OnManifold 0x852e20 | the body has flag bit 6 | `owner->vtbl+0x90` |
| OnContactImpulseClipped 0x852cb0 | the body has flag bit 22 | `owner->vtbl+0xa0` |
| OnExitedBroadPhase 0x8527a0 | **unconditional**, for the body that left the broadphase | `owner->vtbl+0xa8` |
| Post-step touch 0x94700 | the body appears in another body's `ud+0x60` contact list. Only 0x95730 fills those lists, and its sole caller is the arkPhysicsPlayer (0x1b73a0) | `ud->vtbl+8(ud, otherBody, started)`. Base slot: `RET` |
| Contact info 0x81b1e0, reached from 0x8527e0 through 0x81b300 / 0x81b270 | every started impulse event (with `arkPhys+0x1883e8 == 0`) | copies `ud+8` (the owner) into the contact record; no dereference |
| Contact geometry 0x81aae0, called directly from 0x8527e0 | the same | `if (owner) entity = owner+0x10; read entity+0x300..+0x308`. This is the only reader seen that tolerates a NULL owner |

Inside 0x8527e0 (`D2HandBodyQ d:0x8527e0`):

| Path | Condition | Dereferences |
|---|---|---|
| bit7 | for each of the two bodies that has flag bit 7 | `owner->vtbl+0x98(owner, event, …)` |
| bit19 | exactly one of the two bodies has bit 19 | the owner of the body *without* bit 19: `owner->vtbl[0]()` gives a typeinfo; `typeinfo+0x60` is compared with `[VA 0x1439e8610, VA 0x1439e8614]` (runtime globals); if inside, 0x853220(owner) wakes it |
| impact FX | the faster body (squared speed compare) has bit 7 and both materials resolve | `owner+0x10 = entity`, `entity+0x294`; then FX 0x60a480 |
| group compare | 0x61c8f0() is true (`game` sub-object +0x378 +0xd0 byte; [U] meaning) | `owner+0x10 = entity`, `entity+0x2ac`, for **both** bodies, whatever their flags. Different values call 0x80e910(`game+0x3ea5d`) ([U] meaning; the research guessed AI noise). `entity+0x2ac` is also the group index 0x92c30 uses to pick Bend-Time motion properties ([I] dilation group) |

**Consequence [I].** A NULL owner crashes in 0x8527e0 on a started impulse event involving the hand (event `+0x18 == 1`,
`arkPhys+0x1883e8 == 0`) while 0x61c8f0 is true, and in the bit-19 path [V: the dereferences are unconditional on
those paths]. That such events reach the hand is [I]: bit 7 is tested inside the handler, and which body flag makes
hknp *raise* the event was not traced; props carry bit 7 (Q6), so it is the likely trigger. The userdata itself must be engine-shaped, because the step writes `ud+0x20..+0x5f`.

### 1.3 Query consumers (the part that stays [U])

0x17d10 has 55 callers and 0x18cd00 has 51, 103 functions in all. A triage of every one is kept in the evidence batch
r8 (`D2HandBodyQ d:` on every caller):

* At least 54 of them follow `ud -> owner(+8) -> entity(+0x10)`, most from a hit body. That is a lower bound from
  matching the dereference pattern. They then:
  * call `entity->vtbl[0]` (`GetType`);
  * read `entity+0xa8` (id), `+0xd1` (dead/unconscious) and `+0xf0` (model);
  * call `owner` vtable slots (examples: `+0xd0`, `+0xe8`, `+0x100`, `+0x148`).
* Examples: 0x3b7b90 (the native interaction sensor's cast), 0x3c1760, 0x3cdfc0, 0x4e10d0, 0x7b4ab0, 0x7ff230,
  0x83a510 and 0x8aff00.
* They are reached only when a query's filter accepts the hand. That depends on the hand's filter word, and also on
  queries that pass filter info 0, because 0x99610 returns 1 for every body when the query filter info is 0 [V].

Which queries exist with which filter info is not statically bounded. **So the hand body must carry a real owner
component whose `+0x10` is a real entity.** Then every consumer sees objects of the kind it already handles.

### 1.4 Minimum safe layout for R1 (statically derived; the owner choice is [U])

1. **Userdata.** Mod-owned, at least 0x80 bytes, 16-byte aligned, alive longer than the body. Call 0x18cb90(ud), the
   engine ctor (pure stores, no allocation). Keep the base vtable 0x1aff038: all of its slots are stubs. Never free it
   while the body exists. Call the dtor 0x18cbd0 only after `destroyBodies`.
2. **Owner.** Call 0x694c20(ud, owner) with a real component, for example:
   * **(a) the player's own userdata owner: the first candidate to *probe*, not to adopt.** Create a hand body under it
     only if gate G2 (Route table) shows that no reader reaches the player through it. Read it live as
     `*(*(body+0x90)+8)` of the player's second body.
     0x1b6ec0 stores that body's handle at `arkPhysicsPlayer+0x210` (`param_1[0x84]`), and its userdata is
     `arkPhysicsPlayer+0x160`.
     * The pair filter, the query consumers and Bend Time then see the player. 0x99710 then automatically skips the
       hand in queries whose source owner is the player [V].
     * Side effects: AI vision or melee that reaches the hand counts as reaching the player [U]; the bit-19 wake
       0x853220 on the player's component [U]. The player owner's type is itself [I] (traced only as the second
       argument of 0x1b2290). Any of these, observed, rejects (a).
   * **(b) the rigid-body component of a mod-spawned, inert prop** (see R2).
3. **Flags.** `cinfo+8` = 0: no bit 6, 7, 19 or 22. The owner's event slots +0x90/+0x98/+0xa0 are then never called
   for the hand. `+0xa8` (broadphase exit) and `vtbl[0]` (bit-19 mismatch) can still be called.
4. **Filter word.** See Q5 and Q7. The recommended `0x000887e4`:
   * is DEBRIS(4) with collides-with {0,1,2,3,4,5,10,14};
   * keeps every query whose filter info is non-zero and mutual (VISION 17, PROJECTILE 11, WEAPON_MELEE 20,
     TRIGGER 16) away from the hand;
   * excludes the player twice: PLAYER(6) does not list DEBRIS (layer table 0x1c764d0), and the hand does not
     list 6, 7, 8 or 18 [V arithmetic].

**Runtime probe (B11, before the first createBody).** Hook (count only) 0x853220, 0x8527a0 and the 0x3b7b90 / 0x99710
paths with the hand's handle. Read `typeinfo+0x60` of candidate owner (a) and the live range at `VA 0x1439e8610/14`.
Log every reader whose return address is outside the tables above for 10 minutes near props, guards and doors.

**Engine-path alternative (R2b, [U]).** `arkCpntRigidBody::CreatePhysics` 0x808ea0 creates a component-owned body
with exactly this contract:

* `ud = component+0x100`, `owner = component`;
* `cinfo+0x28 = 2` (dynamic), flag bit 7 set;
* motion properties registered through 0xca5600 and mapped through 0x92c30;
* `addMode` 0; the component's start 0x809d80 adds it later.

Its only caller is the derived override 0x4c7930. It raises the fatal error "[arkCpntRigidBody] No PhysicsShape on
'%s'" (0x166830) when the decl has no shape. Calling it by hand on a spawned, unstarted component is not proven safe.

Excerpts: `D2HandBodyQ d:0x93a40`, `d:0x8527e0`, `d:0x94700`, `d:0x852e20`, `d:0x852cb0`, `d:0x8527a0`, `d:0x18d2b0`,
`d:0x99710`, `d:0x81aae0`, `d:0x81b1e0`, `d:0x18cb90`, `d:0x18cbd0`, `d:0x694c20`, `d:0x18cca0`, `v:0x1aff038`,
`d:0x808ea0`, `d:0x1b2290`, `d:0x95730`.

<!-- PINS -->
**Pins.** The first 16 file bytes at every RVA cited in this section (`.text` unless marked).

```
bytes: 0x17d10 48 8b 81 90 00 00 00 c3 cc cc cc cc cc cc cc cc
bytes: 0x6d040 33 c0 c3 cc cc cc cc cc cc cc cc cc cc cc cc cc
bytes: 0x90fc0 48 89 6c 24 18 48 89 74 24 20 89 54 24 10 57 48
bytes: 0x91270 48 89 74 24 18 89 54 24 10 57 48 83 ec 70 49 8b
bytes: 0x92c30 66 44 89 44 24 18 48 89 54 24 10 56 41 54 48 81
bytes: 0x93a40 48 89 5c 24 10 57 48 83 ec 20 49 8b f8 45 8b 40
bytes: 0x94280 45 85 c9 0f 8e 3a 01 00 00 48 89 54 24 10 56 41
bytes: 0x94700 40 53 57 41 55 48 83 ec 30 8b 81 78 83 14 00 48
bytes: 0x95730 48 89 5c 24 08 44 89 44 24 18 89 54 24 10 57 48
bytes: 0x97bc0 48 8b c4 55 53 56 57 41 54 41 55 41 56 41 57 48
bytes: 0x992c0 48 89 5c 24 10 48 89 6c 24 18 57 41 56 41 57 48
bytes: 0x99610 41 8b 40 04 85 c0 75 03 b0 01 c3 41 8b 49 6c 44
bytes: 0x996a0 41 8b 41 6c 8b 51 20 44 8b c0 41 83 e0 1f 44 0f
bytes: 0x99710 40 53 48 83 ec 20 41 8b 41 6c 8b 51 20 49 8b d8
bytes: 0x998b0 c2 00 00 cc cc cc cc cc cc cc cc cc cc cc cc cc
bytes: 0x99a00 48 89 4c 24 08 55 56 57 41 54 41 55 41 56 41 57
bytes: 0x11d080 b0 01 c3 cc cc cc cc cc cc cc cc cc cc cc cc cc
bytes: 0x166830 48 89 54 24 10 4c 89 44 24 18 4c 89 4c 24 20 c3
bytes: 0x181a40 83 c8 ff c3 cc cc cc cc cc cc cc cc cc cc cc cc
bytes: 0x18cb90 0f 57 c0 33 d2 48 8d 05 9c 24 97 01 48 89 01 48
bytes: 0x18cbd0 48 89 4c 24 08 53 48 83 ec 30 48 c7 44 24 20 fe
bytes: 0x18cca0 48 89 4a 20 c3 cc cc cc cc cc cc cc cc cc cc cc
bytes: 0x18cd00 44 8b 01 48 8b 42 28 41 81 e0 ff ff ff 00 4d 69
bytes: 0x18cf30 0f 57 c0 0f 2e c8 7a 02 74 1d 0f 28 0d df fc 23
bytes: 0x18d2b0 48 8b c4 48 89 58 08 48 89 70 10 48 89 78 18 4c
bytes: 0x18d420 40 53 48 83 ec 50 0f 29 74 24 40 0f 57 c0 0f 28
bytes: 0x1b2290 48 8b c4 48 89 50 10 48 89 48 08 55 56 57 41 54
bytes: 0x1b6ec0 48 8b c4 57 41 56 41 57 48 81 ec 10 01 00 00 48
bytes: 0x1b73a0 48 83 ec 28 4c 8b 91 38 01 00 00 8b c2 4d 8b 82
bytes: 0x3b7b90 48 8b c4 55 41 54 41 55 41 56 41 57 48 8d a8 f8
bytes: 0x3c1760 48 8b c4 55 53 56 57 41 54 41 55 41 56 41 57 48
bytes: 0x3cdfc0 48 8b c4 48 89 48 08 53 55 56 57 41 54 41 55 41
bytes: 0x4c7930 40 53 48 81 ec 80 00 00 00 48 8b d9 e8 5f 15 34
bytes: 0x4e10d0 48 8b c4 48 89 48 08 55 53 56 57 41 54 41 55 41
bytes: 0x60a480 f3 0f 10 44 24 30 48 89 11 48 8b 54 24 28 4c 8b
bytes: 0x61c8f0 48 83 ec 28 48 8b 0d 95 89 38 03 e8 a0 c2 28 00
bytes: 0x694c20 48 89 51 08 c3 cc cc cc cc cc cc cc cc cc cc cc
bytes: 0x7b4ab0 48 8b c4 48 89 48 08 55 53 56 57 41 54 41 55 41
bytes: 0x7ff230 48 8b c4 55 56 57 41 54 41 55 41 56 41 57 48 8d
bytes: 0x801380 48 89 5c 24 08 57 48 83 ec 20 8b da 48 8b f9 e8
bytes: 0x806de0 48 89 4c 24 08 57 48 83 ec 30 48 c7 44 24 20 fe
bytes: 0x808ea0 48 8b c4 55 53 56 57 41 54 41 55 41 56 41 57 48
bytes: 0x809d80 48 8b c4 55 56 57 41 54 41 55 41 56 41 57 48 8d
bytes: 0x80e910 40 53 57 41 56 48 83 ec 40 4c 8b f1 48 8b 0d 6d
bytes: 0x81aae0 48 8b c4 48 89 58 08 48 89 70 10 48 89 78 20 4c
bytes: 0x81b1e0 44 89 44 24 18 89 54 24 10 53 56 57 48 83 ec 20
bytes: 0x81b270 48 89 5c 24 08 57 48 83 ec 20 83 7a 20 00 48 8b
bytes: 0x81b300 48 89 5c 24 08 57 48 83 ec 20 83 7a 20 00 48 8b
bytes: 0x83a510 48 8b c4 55 53 56 57 41 54 41 55 41 56 41 57 48
bytes: 0x851a70 48 89 5c 24 18 89 54 24 10 57 48 83 ec 50 48 8b
bytes: 0x852400 40 53 48 83 ec 30 48 8b d9 e8 c2 0e 84 ff 8b 05
bytes: 0x8527a0 48 89 5c 24 08 57 48 83 ec 20 48 8b fa 48 8b 12
bytes: 0x8527e0 48 8b c4 55 41 54 41 55 41 56 41 57 48 8d a8 48
bytes: 0x852cb0 48 89 5c 24 08 48 89 6c 24 10 48 89 74 24 18 57
bytes: 0x852e20 48 89 5c 24 08 48 89 6c 24 10 48 89 74 24 18 57
bytes: 0x853220 48 89 5c 24 08 48 89 74 24 10 57 48 83 ec 30 48
bytes: 0x8aff00 89 54 24 10 89 4c 24 08 53 48 83 ec 20 48 8b 05
bytes: 0xca5600 40 57 41 56 41 57 48 83 ec 20 41 83 38 00 49 8b
bytes: 0xca6810 48 89 5c 24 08 57 48 83 ec 60 48 c7 41 58 00 00
bytes: 0x1aff038 80 13 80 40 01 00 00 00 b0 98 09 40 01 00 00 00  (.rdata)
bytes: 0x1b02768 80 13 80 40 01 00 00 00 b0 98 09 40 01 00 00 00  (.rdata)
bytes: 0x1c74578 80 13 80 40 01 00 00 00 b0 98 09 40 01 00 00 00  (.rdata)
bytes: 0x1c764d0 78 47 2e 00 78 4f 3e 00 38 48 1e 00 7f 4d 5f 00  (.rdata)
```
<!-- /PINS -->

## Q2 Shape: sphere constructor, storage marker, refcount (open item 7)

Verdict: [V]

**Sphere constructor 0xca6550** (`D2HandBodyQ i:0xca6550,40`). It is already pinned by the mod over 113 bytes:
known, `src/native_hand_volume.hpp:15`.

* Signature: `hknpSphereShape* ctor(void* storage /*RCX*/, const float centre[4] /*RDX, 16-byte aligned, MOVAPS*/,
  float radius /*XMM2*/)`. It returns the storage.
* It writes:

  | Offset | Value |
  |---|---|
  | +0x00 | vtable 0x1ec1e78 (hknpSphereShape; first the base hknpConvexShape 0x1ec1728) |
  | +0x08 | 0 |
  | +0x10 | `0x0001ffff` |
  | +0x20 | u16 `0x0111` |
  | +0x24 | byte 2 |
  | +0x28 | radius |
  | +0x40 | `0x00100001` |
  | +0x50..+0x5f | one vertex at `+0x40 + *(u16*)(+0x42)` = the centre with w = 0.5 |

* Storage size **0x60**. The hknp debris emitter 0xcca020 allocates exactly 0x60 bytes for a heap sphere and then
  stores u16 `0x60` at +0x10 [V].

**Storage marker and refcount [V].** `+0x10` is the hkReferencedObject word: low 16 bits memSize, high 16 bits
refcount.

* `addReference` 0xa9e120 and `removeReference` 0xa9e180 do nothing at all when `(short)+0x10 == 0`.
* Otherwise they change the high half by `0x00010000`. When the count reaches 0, `removeReference` calls
  `vtbl+0x10`, which frees memSize bytes through the Havok allocator. For the sphere that slot is 0xa9e2e0.
* The constructor's `0x0001ffff` means memSize `0xffff`, refcount 1.

**Bodies do hold a reference [V] mechanism, [I] gate value.**

* Body shape set 0xca6910 does `addRef(new)` / `removeRef(old)` when the global byte at 0x23edfb1 is non-zero. That
  byte is `01` in the image, and it has only two readers and no static writer xref (`D2HandBodyQ x:0x23edfb1`). Its
  value at body-creation time is [I] (an indirect write cannot be excluded statically); B11 reads it once before
  `createBody`. The memSize-0 rule below is safe whatever its value.
* Body teardown 0xca68e0 (from 0xcaea70 in `destroyBodies`) does `removeReference` under the same gate.
* The engine creator 0x737300 drops its own box reference (0xa9e180) right after `createBody`, which is consistent.

**Rule for a persistent mod sphere.** Construct it with 0xca6550 in mod storage of at least 0x60 bytes, 16-byte
aligned. Then **store u16 0 at +0x10** (memSize 0). Every reference operation becomes a no-op, so the engine can
never free mod memory.

* The alternative is to keep the mod's own reference forever (refcount at least 1). That is also safe, because a
  memSize of `0xffff` would only be used at refcount 0. The memSize-0 form is simpler to prove.
* The storage must outlive the body: free it only after `destroyBodies` has run.

**Box and capsule [I] (not needed for phase 1).**

* `hknpBoxShape` vtable 0x1ec1ba8. The factory 0xca15b0 is `(const float aabb[8] /*min,max*/, float convexRadius,
  const BuildConfig*)`. It heap-allocates and sets memSize itself. 0x737300 calls it. The BuildConfig fields (+0x4,
  +0x10, +0x11, +0x18, +0x1c, +0x20, +0x2d) are not mapped.
* `hknpCapsuleShape` vtable 0x1ec1c98. The in-place ctor 0xcb4830 is `(storage, const float a[4], const float b[4],
  float radius /*XMM3*/)`. Its storage size is not established.
* Use the sphere.

Excerpts: `D2HandBodyQ i:0xca6550,40`, `d:0xa9e120`, `d:0xa9e180`, `d:0xca6910`, `d:0xca68e0`, `d:0xcca020`,
`d:0xca15b0`, `d:0xcb4830`, `b:0x23edfb1,1`.

<!-- PINS -->
**Pins.** The first 16 file bytes at every RVA cited in this section (`.text` unless marked).

```
bytes: 0x737300 48 8b c4 55 56 57 41 56 41 57 48 81 ec 90 01 00
bytes: 0xa9e120 66 83 79 10 00 4c 8b c1 74 47 41 8b 40 10 33 c9
bytes: 0xa9e180 66 83 79 10 00 48 8b d1 74 5a 8b 42 10 33 c9 44
bytes: 0xa9e2e0 48 85 c9 74 0c 48 8b 01 ba 01 00 00 00 48 ff 60
bytes: 0xca15b0 48 8b c4 55 53 48 8d 68 88 48 81 ec 68 01 00 00
bytes: 0xca6550 4c 8b c1 33 c9 48 8d 05 cc b1 21 01 49 89 48 08
bytes: 0xca68e0 83 61 44 f0 48 8b 49 60 48 85 c9 74 0d 80 3d bd
bytes: 0xca6910 48 89 5c 24 08 57 48 83 ec 20 80 3d 90 76 74 01
bytes: 0xcaea70 48 89 5c 24 18 48 89 74 24 20 89 54 24 10 57 48
bytes: 0xcb4830 48 8b c4 48 89 58 08 48 89 68 10 f3 0f 11 58 20
bytes: 0xcca020 48 8b c4 55 48 8d a8 e8 fe ff ff 48 81 ec 10 02
bytes: 0x1ec1728 d0 e3 c7 40 01 00 00 00 e0 0c c8 40 01 00 00 00  (.rdata)
bytes: 0x1ec1ba8 80 e5 c7 40 01 00 00 00 70 07 c8 40 01 00 00 00  (.rdata)
bytes: 0x1ec1c98 c0 e5 c7 40 01 00 00 00 f0 08 c8 40 01 00 00 00  (.rdata)
bytes: 0x1ec1e78 40 e6 c7 40 01 00 00 00 e0 0c c8 40 01 00 00 00  (.rdata)
bytes: 0x23edfb1 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 b8  (.data)
```
<!-- /PINS -->

## Q3 createBody call: arguments, add modes, cinfo (open item 9, first half)

Verdict: [V]

**Signature [V]** (`D2HandBodyQ d:0xc8e5b0`):

```
uint* createBody(hknpWorld* world, uint* outId, const hknpBodyCinfo* cinfo, u8 addMode /*R9*/, u8 activation /*stack*/)
```

What it does, in order:

1. If `world+0x958` (an API listener) is set, it copies the cinfo and calls `listener->vtbl+8`.
2. The id is `cinfo+0x84`, or it is allocated (0xcae7f0) when that is `0x00ffffff`.
3. The motion is `cinfo+0x88`. When that is `0x7fffffff`: if the byte at `cinfo+0x28` is 0, the motion is the
   static motion 0. Otherwise it builds a motion cinfo (0xcc2b10 defaults, then 0xcb9680 from the body cinfo) and
   creates it with 0xc908f0.
4. It calls 0xc92be0(world, id, &motion, cinfo, 2, addMode, activation). That inits the record (0xcaf730 → 0xca6810),
   attaches the motion and fires the body-created signals (`world+0x850`). When `addMode != 0` it then calls
   `addBodies` 0xc8e930(world, &id, 1, addMode, activation).

**There is no queued createBody [V].** The dispatcher 0x89fb0 has no case 0. The debug printer 0x8a730 names case 0
"createBody" but nothing dispatches it, and the same holds for 0x37 "createAttachedBody". So `createBody` must be
called directly, on the game thread, at a drain point, with 0x8a7820 false (see Q8). **Gated:** no direct
`createBody` until B11 passes gate G1 (Route table), which confirms the thread and the drain point at run time. `destroyBodies` can be queued (command 2).

**Add modes [V]** (`D2HandBodyQ d:0xc8e930`, `a:0xcafcb0,8`, `d:0xc8f000`):

| addMode | Effect |
|---|---|
| 0 | Created, not added. The caller adds it later with `addBodies`. `arkCpntRigidBody` does this: 0x808ea0 creates, then the start routine 0x809d80 calls 0xc8e930(world, &id, 1, 1, act) |
| 1 | Added now: broadphase insert ("StAddToBroadPhase"), then caches |
| 2 | **Deferred.** 0xcafcb0 puts the id on a scheduled list in the body manager: activation 1 → `world+0x108`, otherwise `world+0xf8`. `commitAddBodies` 0xc8f000 adds those lists later with activation 0 (`+0xf8`) or 1 (`+0x108`) |

* `commitAddBodies` is called from the game frame (0x8ab380, before the think window), from the dispatcher (command
  0x2c), from `shiftWorld` 0xcfadf0, and from 0xc9f920 and 0xc937f0.
* When the byte at `world+0x818` is 0, `activation` is forced to 0.

**Engine arguments [V]** (`D2HandBodyQ a:0xc8e5b0,14` and the decompiles):

| Caller | addMode | activation | `cinfo+0x28` (motion type) | Notes |
|---|---|---|---|---|
| 0x1b6ec0 arkPhysicsPlayer second body | 2 | 0 | (not used) | `cinfo+0x88` = the proxy body's motion (shared). ud = `this+0x160`. Filter `this[0x86]`. Material `this[0x87]` if not `0xffff`. Then `setBodyTransform` 0xc85860 directly |
| 0x737300 rigid-body helper (box) | 2 | 0 | 1 keyframed | filter = layer table entry for 16 `& 0xfff73cff`; flags `|= 0x10` |
| 0x808ea0 `arkCpntRigidBody::CreatePhysics` | 0 | 0 | 2 dynamic | flags `|= 0x80` (+`0x00040000`/`0x00020000` from the decl); added by 0x809d80 with addMode 1 |
| 0xcca020 hknp debris emitter | 2 | 0 | 2 dynamic | heap sphere, quality `0x0b`, flags `|= 0x80` when enabled, **no userdata**, filter 0 (never pairs) |
| 0x73b420 / 0x73b5b0 / 0x73b740 | 0 | 0 | 0 static | |
| 0x852560 ark world helper | 0 | 1 | | |

**cinfo layout (`hknpBodyCinfo`, at least 0x98 bytes) [V] unless tagged.** Default ctor 0xca65f0; consumers 0xca6810,
0xcaf730, 0xcb9680, 0xcaf5c0, 0xca6e30.

| Offset | Field | Default | Evidence |
|---|---|---|---|
| +0x00 | shape* | 0 | 0xca6910 |
| +0x08 | u32 body flags; low 4 bits ignored (`& 0xfffffff0`) | 0 | 0xca6810 |
| +0x0c | u16 | 0 | [U] not consumed in this pass |
| +0x10 | u32 collision filter info (`layer | mask<<5`) | 0 | `body+0x6c` |
| +0x14 | u16 material id | 0 | `body+0x6a` |
| +0x16 | u8 quality | `0xff` → 0 static / 3 otherwise | 0xcaf730 |
| +0x18 | hkStringPtr name (bit 0 = owned) | 0 | 0xcaf0a0 |
| +0x20 | userdata → `body+0x90` | 0 | 0xca6810, 0x18cca0 |
| +0x28 | **u8 motion type** (corrects research §2.1 "create-motion flag"): 0 = static [V] (0xc8e5b0 tests `== 0`), 1 = keyframed [V] (0xcb9680 tests `== 1`: inverse mass and inertia 0), 2 = dynamic [I] (no code tests 2; the dynamic engine creators 0xcca020 and 0x808ea0 pass 2) | 0 | 0xc8e5b0, 0xcb9680, 0xcca020, 0x808ea0 |
| +0x30 | position (vec4) | 0 | 0xca6810 |
| +0x40 | orientation (quat) | identity | 0xca6810 |
| +0x50 / +0x60 | initial linear / angular velocity → `motion+0x40/+0x50` | 0 | 0xcb9680 → 0xca6e30 [I name] |
| +0x70 | float **mass in kg**; negative means "from the shape" | -1.0 | 0xcaf5c0, 0xcaedc0 |
| +0x78 | mass-distribution ref (nullable, refcounted; data at +0x20) | 0 | 0xcaf5c0, 0x808ea0 |
| +0x80 | u16 motion-properties id; `0xffff` → 1 | `0xffff` | 0xcb9680, 0xca6e30, 0xcaeed0 |
| +0x84 | reserved body id | `0x00ffffff` | 0xc8e5b0 |
| +0x88 | motion id; `0x7fffffff` = create from this cinfo | `0x7fffffff` | 0xc8e5b0 |
| +0x8c | u32 → `body+0x3c` | 0 | [U] meaning |
| +0x90 | refcounted pointer, released by 0x1b6ec0 | 0 | [U] meaning |

**Hand body cinfo for B11 (all [V] fields):**

| Field | Value |
|---|---|
| shape | the Q2 sphere |
| flags | 0 |
| filter | per Q1 |
| material | 0, or a prop material |
| `+0x20` | the Q1 userdata |
| `+0x28` | 2 |
| position / orientation | the hand pose |
| `+0x70` | 2.0 (`HandContactConfig::strength_kg`) |
| `+0x80` | per Q4 |

After gate G1 has passed, call `createBody(world, &id, &cinfo, 1, 1)` directly from the confirmed drain point when not
busy (add now, active). `addMode` 2 also works, but
the body only appears at the next `commitAddBodies`.

Excerpts: `D2HandBodyQ d:0xc8e5b0`, `d:0xc92be0`, `d:0xc8e930`, `d:0xcafcb0`, `d:0xc8f000`, `d:0xca65f0`,
`d:0xca6810`, `d:0xcaf730`, `d:0xcb9680`, `a:0xc8e5b0,14`, `d:0x89fb0`, `d:0x8a730`.

<!-- PINS -->
**Pins.** The first 16 file bytes at every RVA cited in this section (`.text` unless marked).

```
bytes: 0x89fb0 48 83 ec 68 48 c7 44 24 30 fe ff ff ff 4c 8b d2
bytes: 0x8a730 41 56 48 83 ec 40 0f b7 42 04 4c 8b f2 83 f8 3e
bytes: 0x18cca0 48 89 4a 20 c3 cc cc cc cc cc cc cc cc cc cc cc
bytes: 0x1b6ec0 48 8b c4 57 41 56 41 57 48 81 ec 10 01 00 00 48
bytes: 0x737300 48 8b c4 55 56 57 41 56 41 57 48 81 ec 90 01 00
bytes: 0x73b420 48 8b c4 57 48 81 ec f0 00 00 00 48 c7 44 24 30
bytes: 0x73b5b0 48 8b c4 56 57 41 56 48 81 ec f0 00 00 00 48 c7
bytes: 0x73b740 48 8b c4 55 56 57 41 56 41 57 48 81 ec 90 01 00
bytes: 0x808ea0 48 8b c4 55 53 56 57 41 54 41 55 41 56 41 57 48
bytes: 0x809d80 48 8b c4 55 56 57 41 54 41 55 41 56 41 57 48 8d
bytes: 0x852560 40 55 56 57 41 54 41 55 41 56 41 57 48 83 ec 40
bytes: 0x8a7820 80 b9 21 71 33 00 00 75 1c 80 b9 22 71 33 00 00
bytes: 0x8ab380 48 89 4c 24 08 55 53 56 57 41 54 41 55 41 56 41
bytes: 0xc85860 48 89 5c 24 18 89 54 24 10 55 56 57 48 83 ec 40
bytes: 0xc8e5b0 48 89 5c 24 08 48 89 74 24 10 48 89 7c 24 18 4c
bytes: 0xc8e930 45 85 c0 0f 8e b3 06 00 00 48 8b c4 44 89 40 18
bytes: 0xc8f000 40 53 48 83 ec 30 44 8b 81 00 01 00 00 48 8b d9
bytes: 0xc908f0 48 89 5c 24 10 48 89 6c 24 18 56 57 41 56 48 83
bytes: 0xc92be0 48 8b c4 89 50 10 55 41 55 48 83 ec 58 48 89 58
bytes: 0xc937f0 48 89 5c 24 08 48 89 6c 24 10 48 89 74 24 18 57
bytes: 0xc9f920 48 83 ec 58 0f b7 42 04 83 f8 39 0f 87 f5 04 00
bytes: 0xca65f0 48 89 5c 24 08 57 48 83 ec 20 33 ff 0f 57 c9 48
bytes: 0xca6810 48 89 5c 24 08 57 48 83 ec 60 48 c7 41 58 00 00
bytes: 0xca6910 48 89 5c 24 08 57 48 83 ec 20 80 3d 90 76 74 01
bytes: 0xca6e30 48 89 5c 24 10 48 89 74 24 18 57 48 83 ec 30 0f
bytes: 0xcae7f0 48 89 5c 24 10 57 48 81 ec 40 02 00 00 4c 63 49
bytes: 0xcaedc0 89 54 24 10 57 48 83 ec 40 8b c2 0f 29 74 24 30
bytes: 0xcaeed0 66 44 89 44 24 18 89 54 24 10 55 57 48 83 ec 38
bytes: 0xcaf0a0 89 54 24 10 55 41 57 48 83 ec 48 4d 8b f8 48 8b
bytes: 0xcaf5c0 48 89 5c 24 08 57 48 83 ec 50 0f 29 74 24 40 0f
bytes: 0xcaf730 44 89 44 24 18 53 56 57 48 83 ec 30 41 8b c0 48
bytes: 0xcafcb0 66 44 89 4c 24 20 53 55 56 57 41 54 41 55 41 57
bytes: 0xcb9680 48 89 5c 24 08 57 48 81 ec 90 00 00 00 48 8b 41
bytes: 0xcc2b10 0f 57 c9 b8 ff ff 00 00 66 89 01 c6 41 02 01 c7
bytes: 0xcca020 48 8b c4 55 48 8d a8 e8 fe ff ff 48 81 ec 10 02
bytes: 0xcfadf0 48 89 5c 24 08 48 89 74 24 10 48 89 7c 24 18 4c
```
<!-- /PINS -->

## Q4 Motion: properties ids, motion from cinfo, setBodyMass, inverse-mass encoding (open item 8)

Verdict: [V]

**Motion from the cinfo [V].**

* 0xcc2b10 sets the motion-cinfo defaults: props `0xffff`, deactivation enabled, inverse mass 1.0, max speeds.
* 0xcb9680 fills the motion cinfo from the body cinfo:
  * mass properties from 0xcaf5c0(shape, `cinfo+0x78`, `cinfo+0x70`): with mass < 0 it keeps the shape's own mass
    properties; otherwise it scales the inertia to the given mass;
  * inverse mass 1/m;
  * for motion type 1 (keyframed), inverse mass and inertia are zero;
  * props = `cinfo+0x80`; velocities from `+0x50/+0x60`.
* 0xc908f0 allocates the motion (`world+0x118`). 0xca6e30 packs it:
  * `motion+0x38` u16 is the props id, with `0xffff` → 1;
  * `motion+0x20..+0x27` are four hkHalf (inverse inertia x, y, z and **inverse mass**).

**`motion+0x26` encoding [V].** Packing in 0xca6e30 multiplies by 1.00391 (rounding bias at 0x23ccf00), then `PSRAD 16`
on the float bits and `PACKSSDW`: the upper 16 bits of the float (Havok's hkHalf, not IEEE half). The impulse handler
0x90fc0 (command 0x3d) loads the four halves with
`MOVSD XMM0,[motion+0x20]` and widens each to a float with `PUNPCKLWD` against zero: float bits = `u16 << 16`.
Lane 3 (+0x26) multiplies the linear impulse to give a velocity change, so it is the inverse mass:
`mass = 1 / bitcast<float>(u16(+0x26) << 16)`. The mod's reader already assumes this (known,
`src/hand_contact.hpp:238`); that assumption is now verified.

**setBodyMass [V].** Command 0x13 calls 0xc87140(world, id, float mass /*rec+0xc*/, u8 mode /*rec+0x10*/).

* It stores the per-body mass with 0xcaedc0 (the same store `createBody` uses for `cinfo+0x70`).
* It rebuilds the motion's mass with 0xc90a90, unless the body is static or keyframed or mode == 2.
* Record: `{u16 size, u16 0x0200, u16 0x13, u32 id @+8, f32 mass @+0xc, u8 @+0x10}`.

**Motion properties [V]** (`D2HandBodyQ d:0xc9f050`, `d:0xca5550`, `d:0xca5600`):

* The library is `*(world+0x990)` (vtable `hknpMotionPropertiesLibrary`). Entries are 0x40 bytes at `+0x30`; the
  count is at `+0x38`.
* Entry fields:

  | Offset | Field |
  |---|---|
  | +0x08 | gravity factor |
  | +0x0c | time factor (what the velocity wrapper 0x18d020 reads) |
  | +0x10 | max linear speed |
  | +0x14 | max angular speed |
  | +0x18 | linear damping |
  | +0x1c | angular damping |

* Presets set by 0xc9f050:

  | Preset | Gravity | Max speeds (lin/ang) | Damping (lin/ang) |
  |---|---|---|---|
  | 0 static | 0 | | |
  | 1 dynamic | 1.0 | 200 / 100 | |
  | 2 keyframed | **0** | 200 / 100 | |
  | 3 frozen | 0 | | 30 / 30 |
  | 4 debris | 1.0 | 100 / 10 | 0.01 / 0.5 |

* The world ctor 0xc8c250 fills the default library with 0xca5550, which adds presets 0..4 in order. The ids being
  0..4 is [I]: the free-list order in 0xca5b90 was not decompiled.

**Gravity-compensated hand, three options:**

1. **[V], the engine's own pattern.** Used by 0x808ea0 and by debris 0xcca020:
   1. 0xc9f050(props, 1, value of the global at VA 0x141ad57c0);
   2. set `props+8 = 0.0f`;
   3. 0xca5600(library, &id, props) (finds an identical entry or adds one, and fires the library signal);
   4. then 0x92c30(arkPhys, &id, id, `playerEntity+0x2ac`), so the props follow the player's Bend-Time group;
   5. put the id in `cinfo+0x80`. Call this while not busy.
2. **[I]** Use props id 2 (keyframed preset, gravity 0) on a dynamic motion. Check it at run time by reading library
   entry 2's `+0x08`.
3. **No RE needed.** The servo adds `g·dt` each tick.

`setBodyMotionProperties` is command 0x17 → 0xc870c0(world, id, u16 props @rec+0xc).

Excerpts: `D2HandBodyQ d:0xcc2b10`, `d:0xcb9680`, `d:0xcaf5c0`, `d:0xca6e30`, `d:0xc908f0`, `i:0x90fc0,60`,
`d:0xc87140`, `d:0xcaedc0`, `d:0xc9f050`, `d:0xca5550`, `d:0xca5600`, `d:0xc870c0`, `d:0x808ea0`.

<!-- PINS -->
**Pins.** The first 16 file bytes at every RVA cited in this section (`.text` unless marked).

```
bytes: 0x90fc0 48 89 6c 24 18 48 89 74 24 20 89 54 24 10 57 48
bytes: 0x92c30 66 44 89 44 24 18 48 89 54 24 10 56 41 54 48 81
bytes: 0x18d020 48 89 5c 24 08 48 89 6c 24 10 48 89 74 24 18 48
bytes: 0x808ea0 48 8b c4 55 53 56 57 41 54 41 55 41 56 41 57 48
bytes: 0xc870c0 66 44 89 44 24 18 89 54 24 10 53 48 83 ec 20 48
bytes: 0xc87140 48 89 5c 24 08 48 89 74 24 18 89 54 24 10 57 48
bytes: 0xc8c250 48 89 5c 24 10 48 89 6c 24 18 48 89 74 24 20 57
bytes: 0xc908f0 48 89 5c 24 10 48 89 6c 24 18 56 57 41 56 48 83
bytes: 0xc90a90 4c 8b dc 55 57 41 56 41 57 49 8d ab 78 fd ff ff
bytes: 0xc9f050 48 89 5c 24 08 57 48 83 ec 30 8b fa 33 d2 48 8b
bytes: 0xca5550 48 89 5c 24 10 48 89 74 24 18 57 48 81 ec 80 00
bytes: 0xca5600 40 57 41 56 41 57 48 83 ec 20 41 83 38 00 49 8b
bytes: 0xca5b90 40 53 56 57 41 56 41 57 48 83 ec 40 44 8b 79 08
bytes: 0xca6e30 48 89 5c 24 10 48 89 74 24 18 57 48 83 ec 30 0f
bytes: 0xcaedc0 89 54 24 10 57 48 83 ec 40 8b c2 0f 29 74 24 30
bytes: 0xcaf5c0 48 89 5c 24 08 57 48 83 ec 50 0f 29 74 24 40 0f
bytes: 0xcb9680 48 89 5c 24 08 57 48 81 ec 90 00 00 00 48 8b 41
bytes: 0xcc2b10 0f 57 c9 b8 ff ff 00 00 66 89 01 c6 41 02 01 c7
bytes: 0xcca020 48 8b c4 55 48 8d a8 e8 fe ff ff 48 81 ec 10 02
bytes: 0x23ccf00 00 80 80 3f 00 80 80 3f 00 80 80 3f 00 80 80 3f  (.data)
```
<!-- /PINS -->

## Q5 Held-object pairing: disabled pairs, or the layer swap (open item 11)

Verdict: [V]

**The disabled-pair routines exist and are refcounted [V].**

* The world's `arkCollisionFilter` lives at `arkPhys+0x80` (`game+0x60300`; ctor 0x8f940). Its pair hash is:

  | Offset | Field |
  |---|---|
  | `filter+0x20` | 12-byte entries `{u32 lowId, u32 highId, s32 count}` |
  | `filter+0x28` | used count |
  | `filter+0x2c` | mask (capacity − 1; −1 = empty) |

  The hash is `(highId & 0x00ffffff) * 0x9e3779b1`, with linear probing. Keys are full handles, generation included.
* **Disable** 0x91b50(filter, world, idA, idB): it inserts the pair or adds one to its count. It then rebuilds the
  collision caches (0xc87dc0) of one dynamic body of the pair (chosen by `body+0x7e` when both are dynamic).
* **Enable** 0x91e20(filter, world, idA, idB): it subtracts one from the count. At 0 it removes the entry and, when
  both bodies are live and the layers agree, re-creates the collision cache (0xc881c0).
* **Readers:** 0x93b00 (from the pair filter 0x93a40) and slot +0x28 of the filter vtable (0x99650).

**Engine template [V]** (`D2HandBodyQ d:0x808180`), the `arkCpntRigidBody` method:

```
if (0x8a7820(game)) push(arkPhys, {u16 0x20, u16 0x0200, u16 0x3a, u32 ownId@+8, arkPhys*@+0x10, u32 otherId@+0x18, u8 enable=0@+0x1c})
else 0x91b50(game+0x60300, world, ownId, otherId);
```

* The dispatcher runs record 0x3a ("changeCollisionsBetweenBodies") as `+0x1c == 0 ? 0x91b50(*(rec+0x10)+0x80, world,
  rec+8, rec+0x18) : 0x91e20(...)`.
* The public wrapper is 0x851b80(arkPhysLocal, physObjA, physObjB, enable). A NULL object means the player's bodies.
* The engine's own callers are 0x808180 / 0x8083a0, 0x7fd780 / 0x7fd8c0 and 0x769b90 / 0x769f30.

**Use for the hand.**

* At grab, disable (hand, held) once. At release, enable it once, **before** either body is destroyed or its handle
  changes.
* Until gate G1 (Route table) has passed, **always push record 0x3a**: the dispatcher runs it on the drain thread, so no
  direct call is needed. After G1, the direct call 0x91b50 / 0x91e20 is allowed only from the confirmed drain point
  when not busy, exactly like 0x808180.
* A disable left behind is a stale hash entry that cannot match a recycled handle until the 8-bit generation wraps
  [I].

**Fallback: layer swap [V mechanism].**

* The mutual test 0x992c0 needs both `(B.mask >> A.layer) & 1` and `(A.mask >> B.layer) & 1`.
* While holding, rewrite the hand's word through 0x97600 (known, `src/native_held_object.hpp:189`) to a mask without
  the held object's layer. The held object is DEBRIS(4) while held by the mod.
* With the recommended hand word `0x000887e4` (mask `0x0000443f`) this means **`0x000885e4`**: mask bit 4 cleared,
  which is word bit 9 (`0x00000200`). That also stops the hand touching other debris during the hold.

Excerpts: `D2HandBodyQ d:0x91b50`, `d:0x91e20`, `d:0x93b00`, `d:0x808180`, `d:0x851b80`, `d:0x89fb0`, `d:0x8f940`,
`v:0x1ad4c08,12`.

<!-- PINS -->
**Pins.** The first 16 file bytes at every RVA cited in this section (`.text` unless marked).

```
bytes: 0x89fb0 48 83 ec 68 48 c7 44 24 30 fe ff ff ff 4c 8b d2
bytes: 0x8f940 48 89 4c 24 08 57 48 83 ec 50 48 c7 44 24 30 fe
bytes: 0x91b50 44 89 4c 24 20 44 89 44 24 18 55 41 56 48 83 ec
bytes: 0x91e20 44 89 4c 24 20 44 89 44 24 18 53 55 56 57 41 57
bytes: 0x93a40 48 89 5c 24 10 57 48 83 ec 20 49 8b f8 45 8b 40
bytes: 0x93b00 44 8b ca 8b c2 41 8b d0 81 e2 ff ff ff 00 41 81
bytes: 0x97600 48 83 ec 48 80 7c 24 70 00 41 0f b6 c1 74 3a 84
bytes: 0x992c0 48 89 5c 24 10 48 89 6c 24 18 57 41 56 41 57 48
bytes: 0x99650 48 83 ec 28 41 8b d0 45 8b c1 e8 a1 a4 ff ff 84
bytes: 0x769b90 48 89 5c 24 18 48 89 6c 24 20 89 54 24 10 56 57
bytes: 0x769f30 48 89 5c 24 18 48 89 6c 24 20 89 54 24 10 56 57
bytes: 0x7fd780 89 54 24 10 48 83 ec 48 48 8b 49 48 48 85 c9 0f
bytes: 0x7fd8c0 89 54 24 10 48 83 ec 48 48 8b 49 48 48 85 c9 0f
bytes: 0x808180 48 89 5c 24 08 89 54 24 10 57 48 83 ec 40 48 8b
bytes: 0x8083a0 48 89 5c 24 08 89 54 24 10 57 48 83 ec 40 48 8b
bytes: 0x851b80 44 88 4c 24 20 55 56 57 41 54 41 55 41 56 41 57
bytes: 0x8a7820 80 b9 21 71 33 00 00 75 1c 80 b9 22 71 33 00 00
bytes: 0xc87dc0 89 54 24 10 4c 8b dc 55 57 48 81 ec 68 08 00 00
bytes: 0xc881c0 4c 8b dc 49 89 5b 08 49 89 6b 10 49 89 73 20 57
bytes: 0x1ad4c08 a0 e0 c7 40 01 00 00 00 30 0b 09 40 01 00 00 00  (.rdata)
```
<!-- /PINS -->

## Q6 Body flags bit 7 and bit 19 (open item 9, second half)

Verdict: [V]

**Where the flags come from [V].**

* At creation, `body+0x44 = cinfo+8 & 0xfffffff0` (0xca6810). The low four bits are the motion bits: static,
  dynamic, keyframed, active.
* 0xca6910 adds bit 27 from shape flag `0x8000`.
* At run time, command 0x1b `updateBodyFlags` enables flags with 0xc878d0 and disables them with 0xc87970(world, id,
  flags, mode). The disable path clears the given bits (the low 4 are protected) and refreshes the broadphase and
  caches.

**Bit 7, `0x00000080` [V]: per-body "call my owner on contact impulses".**

* OnContactImpulse 0x8527e0 calls `owner->vtbl+0x98(owner, event, …)` for each body of the pair that has it.
* The impact-FX branch also requires it on the faster body.
* `arkCpntRigidBody::CreatePhysics` 0x808ea0 sets it on **every** rigid-body component. The debris emitter sets it
  when its `+0x88` is set.
* A hand body without it never has its owner's +0x98 called. Touching a prop still runs the prop's +0x98 (the
  normal impact path: FX, sound, AI noise), as any prop-on-prop touch does today.

**Bit 19, `0x00080000` [V] mechanism, [I] name.**

* It is toggled only by 0x853300(arkPhysLocal, id, on) (callers 0x852730, 0x76d280, 0x80b4b0):
  * **on:** it enables the flag (0xc878d0) and subscribes "arkPhysicsWorldLocal::OnBodyActivate" 0x852730 to that
    body's activation signal;
  * **off:** it calls 0xc87970(world, id, `0x00080000`, 1) and unsubscribes.
* 0x852730 turns the flag off once the owner's entity reports a positive value through 0x877eb0. It is a one-shot
  "watch this body's activation" flag.
* In 0x8527e0, when exactly one body of a started pair has it, the owner of the body **without** it is checked:
  `owner->vtbl[0]()` gives a typeinfo, and `typeinfo+0x60` is compared with `[VA 0x1439e8610, VA 0x1439e8614]`.
  If it is inside, 0x853220 wakes that owner:
  * when not busy: `owner->vtbl+0x1a0(owner, 1, 0)` if `(owner+0x1c & 3) > 1`, else bits in `owner+0xb4`;
  * when busy: entity event `0x1c0`.
* The flag value is also passed to the impact FX.

**Hand rule.** Keep bits 6, 7, 19 and 22 clear (`cinfo+8 = 0`). The hand's owner `vtbl[0]` can then still be called
by the bit-19 path. That is one of the reasons Q1 requires a real owner.

**Runtime probe (B11):** count 0x853220 calls whose owner is the hand's owner.

Excerpts: `D2HandBodyQ d:0x8527e0`, `d:0x853300`, `d:0x852730`, `d:0x853220`, `d:0xc87970`, `d:0xca6810`,
`d:0xca6910`, `d:0x808ea0`, `d:0xcca020`, `d:0x8a730` (record 0x1b "updateBodyFlagsCommand").

<!-- PINS -->
**Pins.** The first 16 file bytes at every RVA cited in this section (`.text` unless marked).

```
bytes: 0x8a730 41 56 48 83 ec 40 0f b7 42 04 4c 8b f2 83 f8 3e
bytes: 0x76d280 48 89 6c 24 10 48 89 74 24 18 48 89 7c 24 20 41
bytes: 0x808ea0 48 8b c4 55 53 56 57 41 54 41 55 41 56 41 57 48
bytes: 0x80b4b0 48 89 5c 24 08 48 89 74 24 10 57 48 83 ec 20 41
bytes: 0x852730 41 83 f9 01 75 63 44 89 44 24 18 53 48 83 ec 20
bytes: 0x8527e0 48 8b c4 55 41 54 41 55 41 56 41 57 48 8d a8 48
bytes: 0x853220 48 89 5c 24 08 48 89 74 24 10 57 48 83 ec 30 48
bytes: 0x853300 48 89 5c 24 20 89 54 24 10 57 48 83 ec 20 48 8b
bytes: 0x877eb0 40 53 48 83 ec 20 48 63 81 ac 02 00 00 48 8b 0d
bytes: 0xc878d0 48 89 5c 24 08 89 54 24 10 57 48 83 ec 20 48 8b
bytes: 0xc87970 48 89 5c 24 08 89 54 24 10 57 48 83 ec 20 8b c2
bytes: 0xca6810 48 89 5c 24 08 57 48 83 ec 60 48 c7 41 58 00 00
bytes: 0xca6910 48 89 5c 24 08 57 48 83 ec 20 80 3d 90 76 74 01
bytes: 0xcca020 48 8b c4 55 48 8d a8 e8 fe ff ff 48 81 ec 10 02
```
<!-- /PINS -->

## Q7 World filter identity: what sits at world+0x470 (open item 3)

Verdict: [V]

**At creation it is the plain `arkCollisionFilter` at `arkPhys+0x80` [V].**

* `arkPhysicsWorld` slot 0x932d0 (vtable 0x1ad4de0) builds the world cinfo on its stack and stores `arkPhys+0x80`
  (after `addReference`) at cinfo `+0xb8`. The hknpWorld ctor 0xc8c250 copies `cinfo+0xb8` to `world+0x470`
  (`MOV [RBX+0x470],RAX` at 0xc8d250, `RAX = [R14+0xb8]`).
* The `arkPhysicsWorld` ctor 0x8f940 embeds:
  * `hkfxCombinedFilter<arkCollisionFilter>` at `+0x58` (vtable 0x1ad4d98, inner filter at its `+0x28`, which is
    `arkPhys+0x80`);
  * `arkCollisionFilter` at `+0x80` (vtable 0x1ad4c08).

**arkCollisionFilter slots [V]** (`D2HandBodyQ v:0x1ad4c08,12`):

| Slot | Function | Role |
|---|---|---|
| +0x18 | 0x992c0 | pair list: mutual layer test, then 0x93a40 (pair hash and same-owner matrix) |
| +0x20 | 0x99670 | mutual layer test on two filter words |
| +0x28 | 0x99650 | pair hash only |
| +0x30 | 0x99610 | query: `queryInfo == 0` → accept; else mutual test against `body+0x6c` |
| +0x38 | 0x11d080 | returns 1 |

**Temporary replacement during the cloth step [V].** 0xfb1610 ("TtPhysicsClothWorld", called only by the cloth step
0x8534e0, "TtarkClothStep"):

1. it saves `world+0x470` (with `addReference`);
2. it installs `[cloth+0x98]` through 0xc8dc10 (the only setter: refcounts and stores `world+0x470`);
3. it runs the cloth work;
4. it restores the saved filter through 0xc8dc10.

Two details are [I]: that `[cloth+0x70]` is the main world, and that `[cloth+0x98]` is the combined filter at
`arkPhys+0x58`.

* The combined filter forwards +0x20/+0x30/+0x38 to the inner `arkCollisionFilter` only after its own checks: +0x20
  after 0xff3e40 (returns 1) [V]; +0x30 after 0xff3d60 and +0x38 after 0xff3d10, which do real work (material word
  `body+0x6a`, quality range) and were not decompiled [U]. Its pair-list slot +0x18 is 0xff3d00 (`MOV EAX,R9D; RET`,
  which keeps every pair).
* So while it is installed, pairs are filtered only by the inner per-pair slots [I]. The hand's layer test still
  applies through +0x20.

**Consequences.** The hand's word is judged by the `arkCollisionFilter` predicate, in both directions, through the
layer table 0x1c764d0. That table is read by the filter-word builder 0x81a5d0 (`word = table[layer] << 5 | layer`) and
is symmetric (verified for all 27 layers).

**Runtime probe (B7):** read `world+0x470` once per frame outside and inside the cloth step and compare it with
`arkPhys+0x80` / `+0x58`.

Excerpts: `D2HandBodyQ d:0x932d0`, `i:0xc8d230,12`, `d:0xc8dc10`, `a:0xc8dc10,12`, `d:0xfb1610`, `d:0x8534e0`,
`d:0x8f940`, `v:0x1ad4c08,12`, `v:0x1ad4d98,12`, `d:0x99610`, `d:0x99670`, `d:0x99650`, `d:0x81a5d0`,
`b:0x1c764d0,128`.

<!-- PINS -->
**Pins.** The first 16 file bytes at every RVA cited in this section (`.text` unless marked).

```
bytes: 0x8f940 48 89 4c 24 08 57 48 83 ec 50 48 c7 44 24 30 fe
bytes: 0x932d0 48 8b c4 55 57 41 54 41 56 41 57 48 8d a8 38 ff
bytes: 0x93a40 48 89 5c 24 10 57 48 83 ec 20 49 8b f8 45 8b 40
bytes: 0x992c0 48 89 5c 24 10 48 89 6c 24 18 57 41 56 41 57 48
bytes: 0x99610 41 8b 40 04 85 c0 75 03 b0 01 c3 41 8b 49 6c 44
bytes: 0x99650 48 83 ec 28 41 8b d0 45 8b c1 e8 a1 a4 ff ff 84
bytes: 0x99670 48 8b 4c 24 28 41 8b 41 04 8b 49 04 44 8b c0 8b
bytes: 0x11d080 b0 01 c3 cc cc cc cc cc cc cc cc cc cc cc cc cc
bytes: 0x81a5d0 48 63 c2 4c 8d 05 f6 be 45 01 83 e2 1f 45 8b 04
bytes: 0x8534e0 48 8b c4 4c 89 48 20 89 50 10 55 56 57 41 54 41
bytes: 0xc8c250 48 89 5c 24 10 48 89 6c 24 18 48 89 74 24 20 57
bytes: 0xc8d230 e8 eb 0e e1 ff 48 8b 8b 70 04 00 00 48 85 c9 74
bytes: 0xc8d250 48 89 83 70 04 00 00 49 8b 96 c0 00 00 00 e8 ed
bytes: 0xc8dc10 48 89 5c 24 08 57 48 83 ec 20 48 8b da 48 8b f9
bytes: 0xfb1610 48 89 5c 24 08 57 48 83 ec 20 48 8b d9 8b 0d e1
bytes: 0xff3d00 41 8b c1 c3 cc cc cc cc cc cc cc cc cc cc cc cc
bytes: 0xff3d10 41 8b 11 41 8d 40 fe 3c 02 0f 96 c1 45 84 c0 0f
bytes: 0xff3d60 48 83 ec 28 45 0f b7 51 6a 49 8b 50 08 b9 ff ff
bytes: 0xff3e40 b0 01 c3 cc cc cc cc cc cc cc cc cc cc cc cc cc
bytes: 0x1ad4c08 a0 e0 c7 40 01 00 00 00 30 0b 09 40 01 00 00 00  (.rdata)
bytes: 0x1ad4d98 40 d0 fe 40 01 00 00 00 30 0a 09 40 01 00 00 00  (.rdata)
bytes: 0x1ad4de0 00 0d 09 40 01 00 00 00 d0 32 09 40 01 00 00 00  (.rdata)
bytes: 0x1c764d0 78 47 2e 00 78 4f 3e 00 38 48 1e 00 7f 4d 5f 00  (.rdata)
```
<!-- /PINS -->

## Q8 Queue drain: thread, timing against the step, and per-frame ticks (open item 4)

Verdict: [I]

**Drain mechanism [V].**

* The queue at `arkPhys+0x148288` (`arkThreadSafeExpandableQueue`) is filled through 0x90fb0 → 0x89ea0 → 0xfa6a0.
* It is drained by 0x89eb0(queue, world): for each block and each record, 0xae1f40(`world+0x940`, rec). That calls
  `processor[rec+3]->vtbl+0x18`, which is `arkPhysicsCommandProcessor` (vtable 0x1ad4fb8) slot +0x18 = **0x89fb0**.
  The queue is then reset with 0xfa6d0.
* So 0x89fb0 runs once per queued record, on the thread that drains.
* 0x89eb0 has two callers: 0x945f0 and 0x91d80.

**When, in frame order [V]** (`D2HandBodyQ d:0x8ad260`, `d:0x8af460`, `d:0x8af8a0`, `d:0x7ec5b0`,
`step_97bc0.c` in the evidence folder). In the game frame 0x8ad260:

0. 0x7ec5b0 phase 0 (busy C window, then **drain**).
1. 0x8ab380, which includes `commitAddBodies` 0xc8f000.
2. **Busy window A:** `game+0x337121 = 1` around 0x7d9740 (game thinks). Then **drain** 0x91d80.
3. 0x8af8a0, which contains **busy window B**: `game+0x337122 = 1` around 0x7c10b0. Then **drain**, then 0x7ec5b0.
4. 0x8af460:
   * 0x7ec5b0 phase 5 (busy C window, then **drain**), before the step;
   * the physics step 0x97bc0:
     1. Bend-Time velocity rescale (0x18d2b0);
     2. 0x98890;
     3. pre-step 0xc9f5b0 / 0xc9f6c0;
     4. 0x945f0: a parallel batch, then **drain**, *before* the hknp step 0xc8e0a0 and the task graph 0xaab400;
     5. post-step notifications 0x94700;
   * 0x853af0;
   * **busy window C:** `*(game+0x1e8960)+0xc34 = 1` around 0x8af6a0. Then **drain** 0x91d80.
5. 0x7ec5b0(phase, n, …) is a generic phase runner: it sets `+0xc34`, runs the phase list, clears it and **drains**.
   It is called with phase codes 0..7 throughout the frame.

**What "busy" means [V].** 0x8a7820 is true only inside windows A, B and C (C includes every 0x7ec5b0 phase). Those
are the game-side update windows, which may fan out to job threads. **In the call order above, the step 0x97bc0 runs
with all three flags clear.** The scan also found one more writer of the `+0xc34` byte, 0x7e7e10 (`MOV [RBX+0xc34],AL`,
not traced, [U]); B7 should log 0x8a7820 at step entry to close it. So "not busy" means
"the game thread is not in an update window", not "no other thread is stepping".

**Rule for B11 [V] rule, [U] thread.** A direct hknp call must run on the game-frame thread at a drain point. The mod's
existing 0x89fb0 hook (known, `src/native_grab.hpp:197`) is the candidate point, but which thread it runs on is [I].
**No direct call (`createBody`, direct `destroyBodies`, 0x91b50 / 0x91e20, direct 0x97600) is allowed until gate G1
(Route table) passes.** Until then, use the queue (records 2, 6, 9, 0x11, 0x13, 0x1a, 0x3a) only, which also means no
hand body can be created, because `createBody` has no record. Never call from the render thread or an OpenXR thread.

**Thread identity [I].** The drains run on whatever thread runs 0x8ad260 (callers 0x8acdf0 → …), which is the
game-frame thread; the exact OS thread was not traced.

**Does 0x89fb0 run every frame without mod commands? [V] no guarantee.** It is called once per queued record, and
engine records exist only when engine code mutates while busy (examples: 0x97600, 0x18d020, 0x18d2b0, 0x98890).
Whether that happens every frame is [U].

* The servo must therefore queue its own record each frame it needs a tick: a command 9 velocity on the hand. That
  also guarantees the hook runs [V].
* **Runtime probe (B7):** per-frame count of 0x89fb0 calls without mod records, and `GetCurrentThreadId()` in the hook
  compared with the Stage0 scope.

Excerpts: `D2HandBodyQ d:0x89eb0`, `d:0xae1f40`, `v:0x1ad4fb8,6`, `d:0x945f0`, `d:0x91d80`, `d:0x8ad260`,
`d:0x8af460`, `d:0x8a7820`, `k:` scans for the offsets `+0x337121..+0x337122` and `+0xc34`, `d:0x7ec5b0`, `d:0x97bc0`.

<!-- PINS -->
**Pins.** The first 16 file bytes at every RVA cited in this section (`.text` unless marked).

```
bytes: 0x89ea0 44 0f b7 02 e9 f7 07 07 00 cc cc cc cc cc cc cc
bytes: 0x89eb0 40 53 57 41 54 41 55 48 83 ec 28 8b 41 08 48 8b
bytes: 0x89fb0 48 83 ec 68 48 c7 44 24 30 fe ff ff ff 4c 8b d2
bytes: 0x90fb0 48 81 c1 88 82 14 00 e9 e4 8e ff ff cc cc cc cc
bytes: 0x91b50 44 89 4c 24 20 44 89 44 24 18 55 41 56 48 83 ec
bytes: 0x91d80 48 8b 91 90 01 00 00 48 81 c1 88 82 14 00 e9 1d
bytes: 0x91e20 44 89 4c 24 20 44 89 44 24 18 53 55 56 57 41 57
bytes: 0x945f0 48 8b c4 57 41 56 41 57 48 83 ec 70 48 c7 40 98
bytes: 0x94700 40 53 57 41 55 48 83 ec 30 8b 81 78 83 14 00 48
bytes: 0x97600 48 83 ec 48 80 7c 24 70 00 41 0f b6 c1 74 3a 84
bytes: 0x97bc0 48 8b c4 55 53 56 57 41 54 41 55 41 56 41 57 48
bytes: 0x98890 40 53 48 83 ec 50 f6 42 26 04 f3 0f 10 62 40 48
bytes: 0xfa6a0 48 89 5c 24 08 57 48 83 ec 20 49 63 d8 48 8b fa
bytes: 0xfa6d0 41 56 48 83 ec 30 4c 8b f1 84 d2 74 7f 8b 41 08
bytes: 0x18d020 48 89 5c 24 08 48 89 6c 24 10 48 89 74 24 18 48
bytes: 0x18d2b0 48 8b c4 48 89 58 08 48 89 70 10 48 89 78 18 4c
bytes: 0x7c10b0 48 8b c4 48 89 70 20 55 48 8d 68 a1 48 81 ec 90
bytes: 0x7d9740 40 55 56 57 41 54 41 55 41 56 41 57 48 8d ac 24
bytes: 0x7e7e10 48 8b c4 48 89 48 08 56 57 41 55 41 56 41 57 48
bytes: 0x7ec5b0 48 89 5c 24 08 57 48 83 ec 20 c6 81 34 0c 00 00
bytes: 0x853af0 48 89 5c 24 08 48 89 74 24 10 57 48 83 ec 20 8b
bytes: 0x8a7820 80 b9 21 71 33 00 00 75 1c 80 b9 22 71 33 00 00
bytes: 0x8ab380 48 89 4c 24 08 55 53 56 57 41 54 41 55 41 56 41
bytes: 0x8acdf0 40 55 56 57 41 54 41 55 41 56 41 57 b8 30 19 00
bytes: 0x8ad260 40 57 48 83 ec 50 48 c7 44 24 20 fe ff ff ff 48
bytes: 0x8af460 40 53 55 56 57 41 56 48 83 ec 70 48 c7 44 24 30
bytes: 0x8af6a0 4c 8b dc 57 48 81 ec 40 02 00 00 48 c7 44 24 30
bytes: 0x8af8a0 48 8b c4 55 56 57 41 54 41 55 41 56 41 57 48 8d
bytes: 0xaab400 48 89 5c 24 08 57 48 83 ec 30 8b 44 24 60 48 8b
bytes: 0xae1f40 0f b6 42 03 48 8b 0c c1 48 8b 01 48 ff 60 18 cc
bytes: 0xc8e0a0 48 89 5c 24 08 48 89 6c 24 10 48 89 74 24 18 57
bytes: 0xc8f000 40 53 48 83 ec 30 44 8b 81 00 01 00 00 48 8b d9
bytes: 0xc9f5b0 f3 0f 11 54 24 18 48 83 ec 18 f3 0f 10 44 24 30
bytes: 0xc9f6c0 f3 0f 11 5c 24 20 f3 0f 11 54 24 18 48 83 ec 38
bytes: 0x1ad4fb8 80 52 a9 40 01 00 00 00 e0 0e 09 40 01 00 00 00  (.rdata)
```
<!-- /PINS -->

## Route table

Statically known blockers for the B11 routes. "Blocker" means a fact that stops the route until a runtime probe
settles it. Everything else is resolved above.

| Route | What it is | Static blockers | Resolved by this document |
|---|---|---|---|
| R1 | The mod calls `createBody` with engine-shaped userdata | **Q1 owner choice [U]:** the query consumers (at least 54 functions) need a real owner and entity. A pure fake owner or entity is not provable. Creation cannot be queued, so the call must be direct, on the game thread, at a drain point, not busy (Q3, Q8) | Userdata layout and ctor; sphere storage, marker and refcount (Q2); cinfo, add modes, mass, motion props (Q3, Q4); flags (Q6); filter word and pair API (Q5, Q7); drain timing (Q8) |
| R2 | Repurpose an engine-spawned prop: 0x8ae9b0 spawn (`NativeWeaponHands::spawn_def`, defined at `src/native_weapon_hands.hpp:670`, its 0x8ae9b0 call at `:677`; the brief's
`:524` is stale), then rewrite its layer through 0x97600 | Spawned components never start (sim v407/v413 [M], `src/native_weapon_hands.hpp:11-13`). Statically, 0x8ae9b0 creates the entity through `game->vtbl+0x1c0`, sets pose, owner and velocities, then registers it with 0x8aecc0 (entity table, name, 0x8ab180). Whether 0x8ab180 leads to a component start is [U]. The body is created only by the component's own `CreatePhysics` (0x808ea0 via 0x4c7930) and start (0x809d80). Calling those by hand on an unstarted component is [U], and fatal without a PhysicsShape. A prop body also carries bit 7 (FX/AI noise) and decl mass/gravity, and the entity may be saved [U] | If it ever has a body, its userdata contract is the engine's own (Q1); bit 7 can be cleared with command 0x1b (Q6); pairing (Q5) |
| R3 | A body attached to an engine motion with 0xc8e7c0 (`createAttachedBody`: `(world, outId, cinfo, targetId, rebuildMass, addMode, activation)`) | It shares the target body's motion, so it cannot have its own mass, gravity or velocity (Q4). There is no engine motion that follows the hand (the player's second body shares the proxy's motion; a hand on it would move with the capsule). It still needs userdata (Q1). It is not queueable (0x37 is printer-only, Q3) | The same cinfo and userdata findings as R1 |

### Gates for B11

Both gates must pass before the first production `createBody`. Each names what B7 (read-only, live) or B11 (sim
probes) must observe.

* **G1, thread and drain point (blocks every direct hknp call; Q3, Q5, Q8).**
  * B7: log `GetCurrentThreadId()` at 0x8ad260 entry and inside the 0x89fb0 hook; they must be equal on every call.
  * B7: log 0x8a7820 at 0x97bc0 entry (closes the 0x7e7e10 writer [U]) and count 0x89fb0 calls per frame without
    mod records.
  * B11: set a flag on entry of the hknp step 0xc8e0a0 and clear it after 0x94700 returns (count-only hooks, frame
    thread). The 0x89fb0 hook must never observe the flag set, and 0x8a7820 must be false at every call it would make.
  * Pass: zero mismatches over at least 5 sim sessions of 120 s, including a level load and a save/load.
* **G2, owner (blocks the owner choice; Q1, Q6).**
  * B7: read owner (a), `*(*(body+0x90)+8)` of the player's second body; check that `owner+0x10` is the player entity;
    read `owner->vtbl[0]()` → `typeinfo+0x60` and the live range at VA 0x1439e8610 / 0x1439e8614. In range rejects
    (a), because the bit-19 wake 0x853220 would reach the player component.
  * B11 (sim only, never in a headset build): a probe hand body under the candidate owner, with count-only hooks.
    Pass needs all of: zero 0x853220 calls with that owner; zero 0x8527a0 calls for the hand; every caller of
    0x17d10 / 0x18cd00 that sees the hand's handle is in the §1.2 table; and, with only the hand exposed to a guard
    (vision and melee), no change in player health and no alert raised.
  * (a) is adopted only if both parts pass. Otherwise (b) is probed the same way.

**Recommendation: B11 tries R1 first**, as a dynamic sphere, behind gates G1 and G2:

* userdata constructed with 0x18cb90 in mod memory;
* owner: (a), the player's own userdata owner, is the first candidate *to probe* under G2 and is used only if G2
  shows no reader reaching the player through it; candidate (b), a spawned inert prop's rigid-body component, is
  probed next;
* flags 0, filter `0x000887e4`, mass 2 kg, gravity-0 props through 0xca5600;
* created with addMode 1, activation 1, directly from the drain point that G1 confirmed, when not busy;
* driven by command 9 records; pair changes through record 0x3a until G1 passes;
* destroyed with command 2 before any world change.

G1 and G2 come first. R2 is only worth a spike if G2 rejects both owner candidates. R3 is dead for a force-driven hand.

<!-- PINS -->
**Pins.** The first 16 file bytes at every RVA cited in this section (`.text` unless marked).

```
bytes: 0x17d10 48 8b 81 90 00 00 00 c3 cc cc cc cc cc cc cc cc
bytes: 0x89fb0 48 83 ec 68 48 c7 44 24 30 fe ff ff ff 4c 8b d2
bytes: 0x94700 40 53 57 41 55 48 83 ec 30 8b 81 78 83 14 00 48
bytes: 0x97600 48 83 ec 48 80 7c 24 70 00 41 0f b6 c1 74 3a 84
bytes: 0x97bc0 48 8b c4 55 53 56 57 41 54 41 55 41 56 41 57 48
bytes: 0x18cb90 0f 57 c0 33 d2 48 8d 05 9c 24 97 01 48 89 01 48
bytes: 0x18cd00 44 8b 01 48 8b 42 28 41 81 e0 ff ff ff 00 4d 69
bytes: 0x4c7930 40 53 48 81 ec 80 00 00 00 48 8b d9 e8 5f 15 34
bytes: 0x7e7e10 48 8b c4 48 89 48 08 56 57 41 55 41 56 41 57 48
bytes: 0x808ea0 48 8b c4 55 53 56 57 41 54 41 55 41 56 41 57 48
bytes: 0x809d80 48 8b c4 55 56 57 41 54 41 55 41 56 41 57 48 8d
bytes: 0x8527a0 48 89 5c 24 08 57 48 83 ec 20 48 8b fa 48 8b 12
bytes: 0x853220 48 89 5c 24 08 48 89 74 24 10 57 48 83 ec 30 48
bytes: 0x8a7820 80 b9 21 71 33 00 00 75 1c 80 b9 22 71 33 00 00
bytes: 0x8ab180 40 57 48 83 ec 30 48 c7 44 24 20 fe ff ff ff 48
bytes: 0x8ad260 40 57 48 83 ec 50 48 c7 44 24 20 fe ff ff ff 48
bytes: 0x8ae9b0 40 57 41 56 41 57 48 83 ec 40 48 c7 44 24 20 fe
bytes: 0x8aecc0 40 55 56 57 48 83 ec 30 48 c7 44 24 20 fe ff ff
bytes: 0xc8e0a0 48 89 5c 24 08 48 89 6c 24 10 48 89 74 24 18 57
bytes: 0xc8e7c0 48 89 5c 24 08 48 89 6c 24 10 48 89 74 24 18 44
bytes: 0xca5600 40 57 41 56 41 57 48 83 ec 20 41 83 38 00 49 8b
```
<!-- /PINS -->

## Second opinion

DeepSeek (`dsh --profile headless`, read-only, run from the worktree) was asked to challenge each verdict against the
saved decompile excerpts. Its points and how each was resolved:

1. **Q5 fallback word was wrong (accepted, fixed).** `0x000807e4` clears the mask bit for layer 10, not DEBRIS. The
   correct word is `0x000885e4` (mask `0x0000442f`). This was a functional error.
2. **Q1, the 0x8527e0 gate and the "bit 7 fires the event" wording (accepted).** The started branch also needs
   `arkPhys+0x1883e8 == 0`, and bit 7 is tested inside the handler rather than raising the event. The NULL-owner
   consequence is now [I], with the dereference paths themselves [V].
3. **Q1, 0x81b1e0 vs 0x81aae0 (accepted).** The row is split: 0x81b1e0 only copies `ud+8`; the NULL test and the
   `entity+0x300` reads are in 0x81aae0, which 0x8527e0 calls directly.
4. **Q2, the gate byte at 0x23edfb1 (accepted).** Downgraded to [I] for its runtime value, with a B11 read. The
   memSize-0 rule does not depend on it.
5. **Q3, motion type 2 (accepted).** 0 and 1 are [V] by the tests in 0xc8e5b0 and 0xcb9680; 2 = dynamic is [I],
   taken from the dynamic engine creators.
6. **Q7, combined-filter forwarding (accepted after checking).** `D2HandBodyQ i:0xff3d60,6` and `i:0xff3d10,6` show
   that +0x30 and +0x38 run real checks, so only +0x20 is [V]; the other two are [U].
7. **Q4, "hkHalf" pairing (rejected with new evidence).** `D2HandBodyQ i:0xca6e30,40` shows the pack: `MULPS` by
   1.00391 (`f:0x23ccf00`), `PSRAD 16`, `PACKSSDW`. That is the upper-16-bit float truncation with rounding, the exact
   inverse of the `PUNPCKLWD` decode in 0x90fc0. DeepSeek was right that it is not IEEE half; the doc now says so. The
   encode/decode pair stays [V].
8. **Q8, drain list not exhaustive (accepted).** The phase-0 drain (before 0x8ab380) and the phase-5 drain (before the
   step) are added. The "step runs with the busy flags clear" claim was confirmed by the reviewer.

## Summary

| Question | Verdict | Key RVAs | blocks route R1 | blocks route R2 | blocks route R3 |
|---|---|---|---|---|---|
| Q1 userdata contract | [U] (inner [V] reader map, [I] player owner type) | 0x93a40, 0x8527e0, 0x94700, 0x18cb90, 0x694c20 | yes: gate G2 (owner) | no: the engine's own userdata | yes: same as R1 |
| Q2 shape | [V] + [I]: gate byte 0x23edfb1 runtime value; box and capsule ctors | 0xca6550, 0xa9e180, 0xca6910 | no (memSize-0 rule holds whatever the gate byte) | no | no |
| Q3 createBody | [V] + [I]: motion type 2 = dynamic, velocity field names; [U]: cinfo +0x0c, +0x8c, +0x90 | 0xc8e5b0, 0xc8e930, 0xc8f000, 0xc8e7c0 | yes until gate G1 (direct call only) | no | yes: not queueable, needs a target motion |
| Q4 motion | [V] + [I]: props id 2 = keyframed preset | 0xcb9680, 0xc87140, 0xca5600 | no (use the [V] 0xca5600 path) | no | yes: the motion is shared |
| Q5 pairing | [V] + [I]: stale-entry generation wrap | 0x91b50, 0x91e20, 0x808180 | no (record 0x3a until gate G1) | no | no |
| Q6 flags | [V] + [I]: the bit-19 name | 0x8527e0, 0x853300 | no: keep the bits clear | no: clear bit 7 with command 0x1b | no |
| Q7 world filter | [V] + [I]: cloth-step filter identity; [U]: combined filter +0x30/+0x38 checks | 0x932d0, 0xfb1610, 0x992c0 | no | no | no |
| Q8 queue drain | [I] + [U]: OS thread, engine records per frame, 0x7e7e10 writer | 0x89eb0, 0x945f0, 0x8ad260 | yes until gate G1 | no | no |

## Reproduce

* Verify the exe hash first.
* Copy the analysed project read-only (`~/.local/share/d2vr-re/snapshots/dishonored2-ghidra-2026-10-19/proj`) to a
  scratch directory.
* Run `tools/D2HandBodyQ.java` with one command file per batch, using the excerpt references above.
* The `bytes:` lines come straight from the PE file (section table, then RVA to file offset).
  `python3 tests/hand_body_re_doc_check.py` re-reads them from the exe and fails on any difference.
