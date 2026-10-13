# Native hold-object (bottles, glasses, arms, skulls): placement, throw, drop

Steam build 17576867, image base `0x140000000`; addresses below are RVAs unless written `0x14…`.
This is static analysis only: Ghidra ran read-only at low priority. The game was not running, so nothing was read live.
**VERIFIED** means I read it directly from a decompile, disassembly, PE bytes or decl text. **INFERRED** means I deduced it and it needs a live check.
Raw dumps are in `artifacts/claude/holdobject/out/r1..r14.txt` and `s1..s4.txt`. Decl text is in `artifacts/claude/holdobject/decls/`. To rerun a query, use
`artifacts/claude/holdobject/mg.cmd HoMulti <out> dc:0x14… vt:0x14… rf:0x14… sr:<ascii>`.

## Summary

* HoldObject has **no per-frame placement code**. On entry (`83d770`) it **binds** the object once to the player entity, using the
  engine's generic entity bind (the "reference frame" at `entity+0x358`). The bind stores a fixed local offset and rotation. (VERIFIED)
* Each frame, the **reference-frame pre-physics task** computes `target = master frame × local offset`. The held object's rigid body is keyframed while it is held, and the task drives it with
  `velocity = (target − current)/dt` (`7551c0 → 7545a0 → 7506d0 → 809ab0`). (VERIFIED chain; the body being keyframed is INFERRED)
* **Narrowest override:** post-hook **`0x7506d0`**. It is the single, side-effect-free function that returns the bound target pose and the drive velocities.
  When its frame belongs to the held object, replace the outputs with the VR hand pose and recompute the two velocities.
* **Throw** `83de90` and **drop** `83cfb0` are called only by `arkPlayerStateReleaseObject` (+0x50 = `8451e0`).
  The throw velocity is a ballistic solve toward a camera-pitch aim point, then ×0.7, plus the player's velocity, then ×`m_throwFactor`.
  It is posted as entity event **0x149** and applied by the rigid-body handler **`0x727100`**. That handler is the substitution point for a hand velocity.
* **Arms:** entry **hides the right-arm entity** (id at `body+0x680`; the sword is its child), and the left one too for two-handed objects.
  It also flags the object for the first-person render pass (`entity+0xc8`).

## 1. Where the held object is stored (Q1)

| Step | Pointer chain | Status |
|---|---|---|
| game, player | `game=*(base+0x39a5290)`, `player=*(game+0x3130b0)` (existing mod ABI) | VERIFIED (mod) |
| FSM | player component with vtable `1c73d58`; `FSM+0x10 == player`; camera/view `*(FSM+0x310)` (origin `+0xd8`, axis `+0xfc`); controller `*(FSM+0x2f8)`; body `*(FSM+0x300)` | VERIFIED |
| HoldObject state | `S=*(*(FSM+0x90)+0x10+49*8)`, vtable `1c7a998`, `S+0x20` active, `S+0x18` decl | VERIFIED (earlier doc) |
| **held entity id** | **`S+0x68`** u32 spawn id (`entity+0xa8`); `0x3fff0010` = none. Entry writes it (`83d770`: `*(S+0x68)=*(param+8)`); exit resets it | VERIFIED |
| other state fields | `S+0x6c` u32 copied from the transition params (saved and restored by `83ddc0`/`83dd60`); `S+0x70` = left arm locked (two-hand); `S+0x71` = "skip unbind on exit" (set by `Drop(S,1)`) | VERIFIED |
| entity | `1406dc560(id)` = registry lookup `game+0x3130b0` (same table the mod uses) | VERIFIED |
| **reference frame (bind)** | **`rf=*(entity+0x358)`**: `rf+0x10` owner entity, `rf+0x3c8` master (= player), `rf+0x3c0` attach-tag string, `rf+0x3d0` s16 joint (‑1 = none), **`rf+0x3d4` local offset vec3**, **`rf+0x3e0` local rotation quat**, `rf+0x400` master animator, `rf+0x408` flags (`0x20` physics-driven, `0x10` bind dirty), `rf+0x40a` target mode, `rf+0xd8` motion interface, `rf+0x88/+0x94` drive linear/angular velocity | VERIFIED offsets; INFERRED class name (only the task names `arkReferenceFrame{Pre,Post}PhysicsUpdateTask` exist in RTTI) |
| rigid body | `rb=GetComponent(entity,&DAT_1439e85b0,2)` (`877720`). For bottles this is `arkCpntRigidBodyThrowable` (vt `1b64b68`); `rb+0xe8` = motion interface (`rf+0xd8 == rb+0xe8`, set by `751050`) | VERIFIED |
| physics handle | `rb+0xf8`; record = `*(*(game+0x60410)+0x28) + (h&0xffffff)*0xb0` (same ABI as `native_rigid_body.hpp`) | VERIFIED |
| body motion byte | `rb+0xfe`: `0`/`1`/`2` set by vfuncs `+0x158`/`+0x160`/`+0x168`. Bind requests `1` (`80b2a0`: physics cmd `0x110200` arg 1); unbind requests `2` | VERIFIED; INFERRED meaning (1 = keyframed, 2 = dynamic) |
| other | `rb+0x8c..0x94` thrower position, `rb+0x438` throwable flags (`|0x48` thrown, `|0x4c` dropped; the mod rejects bit 8); entity `+0xd0` bit `0x80` = bound, bit `1` = hidden | VERIFIED |

The choke-sensor field "+0x48 hold object" is not used by any of this code. Ignore it.

## 2. How the object is positioned (Q2)

### 2a. Entry binds once: `83d770` (vtable +0x58 `83c460` → `9ffc00` + `83d770`; restore path `83dd60`)
```c
*(S+0x68) = id; e = Entity(id); e->d4 |= 4;                     // VERIFIED
pk = GetComponent(e, Pickable_Simple); pd = pk->decl;           // pickable decl
e->d7 &= ~1;
off = { sd->m_distanceInFrontOfUser(+0x70) + pd->centerOffset.x(+0x70), pd->centerOffset.y(+0x74), pd->centerOffset.z(+0x78) };
if (pd->m_collideWithSword(+0xbc)) 808310(GetComponent(e,&DAT_1439aee80), 0x100000);
if (c = GetComponent(e,&DAT_1439b4630)) off += angleOffset * localise(e->origin - 731470(c));   // centre-of-joints correction
875e80(player, e, &pd->m_attachTag(+0xb0), &off, &pd->m_angleOffset(+0x7c), 1);              // Bind
87b760(e, DAT_14398e32c);                                       // entity+0xc8 = first-person render scale
if (pd->m_needBothHands(+0xa8)) { hide left arm; FSM msg 0x40(0x21); S+0x70=1; }
FSM msg 0x35(0x21); hide right arm (774a10(body+0x680,0)); 883f80(e, "picked up" event)
```

### 2b. Bind: `875e80` → `752900(rf, master, tag, off, axis, 1)` (VERIFIED)
* If the tag string is non-empty and the master has a model, `879930` looks the tag up in the master model's tag table. The result is a joint with its own local position and quaternion, and `off`/`axis` are **added and multiplied onto** it.
  Otherwise the joint is `0xffff`, `rf+0x3d4 = off` and `rf+0x3e0 = quat(axis)` in the **master origin/axis** frame.
  The object is teleported immediately (`87d1e0`). The rigid body is then attached through `727450 → 727a00 → vfunc +0x150(mode 1)`.
* The default `m_attachTag` comes from the runtime global `0x14398e1f0` (ctor `76e0e0`), and no shipped decl overrides it, so its value is unknown statically (INFERRED).
  If it is empty, the object is bound to the player origin (0.80, 0, 0.10 in the player frame for `movable_small`). That is suspiciously low, so the default may be a tag on the camera or arms.
  **Live (2026-10-13, bottle):** joint 8, tag `camera_tag`, master = player, local `[0.55,-0.25,-0.29]`. The drawn model
  (`model+0x1860/+0x186c`) follows the bind, not the physics body, so overriding only `7506d0`'s outputs left the bottle by the
  eye. Writing `rf+0x3d0=-1` with `rf+0x3d4`/`rf+0x3e0` = the hand pose in the master's `+0x300/+0x30c` frame draws it in the
  hand (sim v376: render = entity, axis equal). `0x111410` is id `ToMat3` (quat x,y,z,w; row 0 = `1-(yy+zz), xy-wz, xz+wy`),
  composed `local x master` by `0xdce0`.
* **Sword (live v378):** during the hold the engine itself rebinds the sword to the player's joint 2 and hides it (`entity+0xd0`
  0x8b), and puts it back on the right arm's joint 59 at exit, so the re-shown right arm holds only the object. `0x87e040`
  derives bit 1 (effective hidden) = own bit 0 | bind master's bit 1; Show `87c590` is a no-op on a visible entity.
  Mesh centre: measure it only once the drawn model sits on the published pose (at pickup its bounds are stale).

### 2c. Per-frame placement (VERIFIED code; thread INFERRED)
* `arkReferenceFramePrePhysicsUpdateTask` (vtable `1c93fb0`, slot 2 = `7551c0`). This is a lock-free work-queue loop, so it is built to run on task workers.
  For each frame it calls `7545a0`. If `rf+0x408 & 0x20` (physics-driven, true when a rigid body is the motion object), it calls
  `7506d0(rf, rf+0x40a, pos, axis, lin, ang, dt)`, stores `lin/ang` at `rf+0x88/+0x94`, and calls motion vfunc +0x18 = `809ab0` (posts physics command `0x1e0200`, applies velocity).
* `arkReferenceFramePostPhysicsUpdateTask` (vtable `1c93fd8`, slot 2 = `755030`) → `753c10` copies the simulated pose to `entity+0x300/+0x30c` and the render model (`model+0x1860/+0x186c`).
  For non-physics bound entities, `753c10` itself calls `7506d0(mode 3)` and integrates.
* **`7506d0`**: `void(rf, int mode, float pos[3], float axis[3][3], float lin[3], float ang[3], float dt)` (7th argument on the stack).
  * No joint: `pos = master.origin + off·master.axis` (rows). With `mode==3` or a master without its own frame, it uses the master's `+0x300/+0x30c`. Otherwise it uses the master frame's predicted transform (`750fe0/750f70`).
  * Joint: `53d800`/`53de90`.
  * Then `lin = (pos − owner.origin(+0x300))/dt` and `ang = rotation(owner.axis(+0x30c) → axis)/dt`.
  * It **writes only the four output buffers**.
* While held, the throwable uses `m_collisionSettingsAttached`: every block flag is false (VERIFIED decl text). The object collides with nothing and can pass through walls.
  Release corrects for this with an eye→object shape-cast (`83c860`, §4).

### 2d. Decl data (VERIFIED: reflection `1417848c0`/`141784610`/`141737fa0`/`141738440` + decl text)
| Where | Offset | Field | Shipped value |
|---|---|---|---|
| state decl `sd=*(S+0x18)` | +0x70 | m_distanceInFrontOfUser | 0.55 (one- and two-hand) |
| | +0x78 / +0x80 / +0x84 | m_throwFx / UI label Drop / UI label Throw | |
| | +0x88 (+0,+4,+8,+0xc,+0x10) | m_settingsForThrowObject: speed, minPitch, distForMinPitch, maxPitch, distForMaxPitch | 16, ‑, 6, 80, 9 (two-hand 15, ‑, 4, 80, 8) |
| | +0x9c | m_settingsForGreaterThrow (same layout) | 22, ‑, 8, 80, 11 (two-hand 21, ‑, 4, 80, 10) |
| | +0xb0 | m_checkLineOfSightCollision | two-hand false |
| pickable decl `pd=pk->decl` | +0x70 | holdAndThrowProps.m_centerOffset | movable_small (0.25, 0, 0.10) |
| | +0x7c | m_angleOffset (mat3) | movable_small diag(−1, −1, 1) (180° yaw) |
| | +0xa0 / +0xa4 / +0xa8 | m_isHoldAndThrowObject / m_encumbrance / m_needBothHands | |
| | +0xb0 / +0xb8 / +0xbc / +0xc0 | m_attachTag / m_tutorialId / m_collideWithSword / m_throwFactor | ctor: global / 0 / 0 / 1.0 |

## 3. Recommended override (Q3)

**Post-hook `0x7506d0`** with MinHook. Prologue `48 8b c4 48 81 ec 68 01 00 00 66 83 b9 d0 03 00 00 ff`: 10 relocatable bytes, no RIP-relative operands (VERIFIED). Guard the build by these bytes, as `native_grab.hpp` does.
```cpp
using BindTarget = void(__fastcall*)(void* rf,int mode,float* pos,float* axis,float* lin,float* ang,float dt);
void __fastcall detour(void* rf,int mode,float* pos,float* axis,float* lin,float* ang,float dt){
    original(rf,mode,pos,axis,lin,ang,dt);                 // keep native bookkeeping; the function is pure
    const auto t = held_target_snapshot();                 // lock-free, published by the game thread
    if(!t || t->rf!=uintptr_t(rf) || rd<uintptr_t>(rf,0x10)!=t->entity || rd<uintptr_t>(rf,0x3c8)!=t->player || stale(t)) return;
    copy(pos,t->pos,3); copy(axis,t->axis,9);              // idMat3 rows, same basis as entity+0x30c
    if(dt>0){ lin = (t->pos - origin(entity+0x300))/dt; ang = grab_rotation_error(axis(entity+0x30c), t->axis)/dt; } // clamp both
}
```
* **Gate:** the game thread (Stage0) publishes `{rf, entity, player, pose, tick}` only while HoldObject (49) is active and `entity+0xa8 == S+0x68`.
  The detour compares the `rf` pointer first, so every other bound entity pays one comparison.
* **Thread:** this runs inside the task loops (worker threads, INFERRED). Never call engine functions here. Read only an atomic or seqlock snapshot.
* **Preserve:** always call the original. Leave `mode`/`dt` untouched. When `dt <= 0` the post task passes `dt = 1` with `lin` aliasing `ang`; writing both is harmless.
  Use `grab_rotation_error` (`physical_grab.hpp`), which already matches the physics angular-velocity convention used by the mod's motor (INFERRED to match `809ab0`).
* **Pose source:** hand pose × a grip offset captured at pickup (as `PhysicalGrab::bind` does), instead of `m_centerOffset`/`m_angleOffset`.
* **Data-only fallback (no hook):** write `rf+0x3d4` and `rf+0x3e0` from the game thread, with `local = inverse(master frame) × hand`. This is valid only when `rf+0x3d0 == -1`.
  It lags by about a frame and depends on which master frame `7506d0` picks (`rf+0x40a`, predicted versus current). Use it only for a quick experiment.

## 4. Throw and drop (Q4)

**Trigger (VERIFIED):** the `arkPlayerStateReleaseObject` (vtable `1c7cfe8`) activation slot +0x50 = `8451e0` reads controller `*(FSM+0x2f8)` action masks at `+0x80 & +0x88`:
* Drop (bit 22 = action 0x16, UI label Drop): calls `83cfb0(S,0)`.
* Throw (bit 19 = action 0x13, UI label Throw; only if `ctrl+0x5d0 == 0xe` and `ctrl+0x224 == 0`, otherwise drop): calls `83de90(S)`.
* It then exits both states. Each of these functions has this single input caller (`83cfb0` also has 7 non-input callers, such as forced drops).

**Throw `83de90(S)` (VERIFIED order):**
1. Set the rigid body's thrower position and id (`rb+0x8c`, `7278e0`). `throwFactor = pd+0xc0`.
2. `start = entity origin` (articulated bodies use the joint centre).
3. Aim `83d460`: pitch comes from the camera axis (`FSM+0x310 +0xfc`). The settings are greater or normal, chosen by an attribute in `FSM+0x330`.
   `dist = lerp(distForMinPitch, distForMaxPitch, clamp((pitch−minPitch)/(maxPitch−minPitch)))` (`83d300`).
   `aim = camOrigin + fwd·dist`, or toward the hit of sensor slot 9 (`FSM+0x338`), clipped to the hit distance.
4. `blocked = 83c860(S,&releasePos)`: shape-cast from the eye to the object centre +0.08 m. If it hits, the object is pulled back toward the eye.
5. `v = 6dd030(start, aim, speed)`: `t = |aim−start|/speed`, `v = ((aim−start) − g·t²)/t`, with g from the physics world.
6. `v = 0.7·(v + player velocity(player+0x33c))`.
7. Spin ω = 10 rad/s about `normalize(v̂ × −camUp)` (0 for articulated bodies).
8. `v −= rb->vfunc[0x190](ω)`, which compensates for centre-of-mass offset.
9. Teleport the object to `releasePos` (`87d1e0`).
10. If blocked, `v = v̂·0.1`.
11. `v *= throwFactor`.
12. Post event **0x149** `{v[3], ω[3], u16 flags = 0}` (`8112c0(eventMgr+0xc8e8, 0x149, 0x1c, id)`).
13. Unbind (`8762b0`), set thrown flags (`4c94e0`), spawn m_throwFx, send the thrown event, apply the AI-noise component.

**Drop `83cfb0(S,0)`:** sets the thrower, teleports to the safe position if `83c860` is blocked, sets the dropped flags (`4c7020`) and unbinds. **No velocity is posted.**
Whether the body keeps its last keyframe drive velocity after unbinding is INFERRED; check it live. `83cfb0(S,1)` only sets `S+0x71` (keep the object bound) before exiting.

**Velocity substitution:** event 0x149 is the generic "set velocity" event, posted by about 20 functions. Its rigid-body handler is
**`0x727100(rb, ev)`** (registered by rb OnStart `809d80` through thunk `80bc50`; prologue `4c 8b 42 08 c6 81 b2 00 00 00 01`).
The payload is `p=*(float**)(ev+8)`: `p[0..2]` linear, `p[3..5]` angular, byte `p+0x18` flags, byte `p+0x19` additive.
The handler stores the velocity as pending at `rb+0x98..0xb2` and calls vfunc +0x130 (activate).

Recommended combination:
* Pre-hook `83de90` to record a one-shot token: entity from `S+0x68`, the world hand linear and angular velocity, and a tick. Include locomotion velocity, which native code adds as `player+0x33c`.
* Hook `727100` to overwrite `p[0..5]` when `*(rb+0x10) == token.entity`, `p+0x19 == 0` and the token is fresh, then clear the token.
* Keep the native 0.1 m/s result when it was blocked (`|v_native| ≤ 0.1·throwFactor + ε`).

This preserves the native thrown semantics: shatter threshold 2 m/s for thrown objects versus 15 m/s for dropped ones, AI distraction, FX and the release wall-sweep.
Trigger it from VR by injecting action 0x13 (throw) or 0x16 (drop) for one frame, chosen by release speed. Do not call `83de90` directly.

## 5. Arms and rendering on entry (Q5)
* `774a10(part, show)` resolves the id at `*part` and calls entity vfunc +0x80 **Hide** (`8784b0`: sets `entity+0xd0 |= 1`, and `87e040`/`87e0a0` propagate it to the model and bound children) or +0x88 **Show** (`87c590`). (VERIFIED)
* Entry: **always hides `body+0x680`**. That is the right-arm entity id, the same id `native_equipment_ownership.hpp` checks as the sword's parent, so the sword is hidden with it.
  For `m_needBothHands` it also hides `body+0x370` (left). It sends FSM messages 0x35 (and 0x40) with argument 0x21; exit sends argument 0x16.
  The arguments' meaning is INFERRED: an equip-state request.
* No arm animation clip is started by HoldObject itself: `83d770` has no animation callees (VERIFIED).
* Exit `83ce70`: shows the arms again (left only if `S+0x70`) unless `FUN_140824050(ctrl+0x410)` is true. It unbinds unless `S+0x71`, then calls `87b760(e,0)`.
* `87b760(e, *0x398e32c)` sets `entity+0xc8`, which becomes `model+0x18b8`. Render `2fcff0` sends models with `+0x18b8 > 0` through the separate first-person pass (shader parameter `1/0x398e32c`).
  INFERRED: the held object is drawn as a view-model. Check it in the headset. If its depth or scale is wrong, call `87b760(e, 0.f)` on the game thread after entry.
* For VR, to show the hand that holds the object: after entry, call Show (vfunc +0x88) on the right-arm entity from the game thread. The mod's tracked arm pose then places it. Headset check required.

## 6. Live checks still open
* `rf+0x3d0` and the tag string while holding: is the object bound to a joint or to the player origin?
* `rb+0xfe` and the body record flags while held: keyframed or not?
* The value of `0x398e32c`.
* Whether a drop keeps the drive velocity.
* Which thread runs the task loops.
* Whether hiding `body+0x680` matches the visible right arm.
