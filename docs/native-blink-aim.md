# Native Blink aim: where the target ray comes from

Steam build 17576867 (`Dishonored2.exe`, SHA-256 `b671f824...6180`, base `0x140000000`, offsets are RVAs). Static RE only (Ghidra 12.1.4 headless, fresh
auto-analysis of a copy of the exe; helper scripts were scratch files, nothing in the repo changed except this doc). **VERIFIED** = seen in the decompile / disassembly
/ vtable bytes. **INFERRED** = deduced, needs a live check. The game was not inspected live.

## 0. Answer in five lines

* The Blink aim is `arkCpntPowerBlink` (vtable `0x141b9bb78`, size `0x298`). Its per-frame aim function is **`0x1406205e0`** (`void __fastcall (arkCpntPowerBlink* power)`), which calls the
  target solver **`0x14061ede0`** (`void __fastcall (power)`, only caller `0x1406205e0`).
* Both read the ray **only** from the player camera component's aim pose: `cam = *(power+0x160)`, origin `cam+0x2527c` (x,y,z), direction `cam+0x25288` (first row of the 3x3 axis). That is exactly the
  `0x7f4b30` field the existing hook rewrites. No head bone, no view/render matrix, no `cam+0x30` view matrix.
* The same 12 floats (`cam+0x2527c .. +0x252ac`) feed the Blink cross-block sensor (`0x1403a4a20`, `0x1403a4620`), the shared power-snap sensor (`0x1403c3870`), the ledge-hysteresis test (`0x14061fd20`) and
  an FX placer (`0x14061ec20`). One consistent rewrite of the camera aim therefore moves all of them together.
* Scheduling: Blink reads the pose at component stage `QUERY_PHYSICS` (1). `0x7f4b30` writes it at stage `PRE_RENDER` (7), the only per-frame writer. So Blink sees the **previous frame's** rewrite
  (one frame of latency); the existing hook's write is visible to it, nothing between stage 7 and stage 1 overwrites it.
* Therefore the static evidence says the existing rewrite *should* already steer the Blink marker. Since it does not in the headset, the failure is a gate or a data issue on the hook side (see 5), not a missing
  reader. Next step is a 20-line observer hook on `0x1406205e0` (section 6), which also doubles as a deterministic override.

## 1. Classes and where the per-frame work lives

| Thing | Address | Evidence |
|---|---|---|
| `arkCpntPowerBlink` vtable | `0x141b9bb78` | ctor `0x14061d390` writes it (`LEA RAX,[0x141b9bb78]`), class registration `0x1413d7800` ("arkCpntPowerBlink", base "arkCpntPower", size `0x298`). VERIFIED |
| `arkCpntPowerBlinkFactory` vtable | `0x141d0b658` | per-stage job slots below. VERIFIED |
| Power aim job (thunk) | `0x140954d00` | factory slot `+0x18` (`0x141d0b670`); loops component ids, `FUN_1404900b0(mgr,id)`, calls `0x1406205e0(power)`. Unique to Blink (2 data refs: unwind + this vtable). VERIFIED |
| Power aim update | **`0x1406205e0`** | `if(power[+0xa4]==0) {...cooldown/ready check...} else { 0x14061ede0(power); ...}` VERIFIED. `+0xa4` is set to 1 by `0x14061bc20` (start aim) and cleared by `0x14061be50` (stop); `+0xa5` is set by the cast function `0x14061bea0`. |
| Target solver | **`0x14061ede0`** | the ray cast, see 2. Called only at `0x140620639` (`E8 A2 E7 FF FF`). VERIFIED |
| Max-distance function | `0x14061e230` | see 4 |
| Ledge hysteresis | `0x14061fd20` | compares the saved aim (`+0x1c8` dir, `+0x1d4` origin) with the live camera aim |
| Power setup | `0x14061ff90` (vtable `+0x18`) | resolves components from the player: `+0x150` body (`DAT_1439d7510`), `+0x158` attributes (`DAT_1439e8640`), **`+0x160` camera (`DAT_1439d7480`, = `arkCpntPlayerCamera`, same class-info that `0x1413d0930` registers with the string "arkCpntPlayerCamera")**, `+0x168` state, `+0x170` sensor container (`DAT_1439e86d0`) |
| Power sensors | `0x140620200` (vtable `+0x28`) | `+0x178 = sensor(id 2)` = `arkCpntPlayerSensorBlinkToMantle`, `+0x180 = sensor(id 0)` = `arkCpntPlayerSensorBlinkSnap`, sensor id `0x22` = `arkCpntPlayerSensorBlink`. Ids from the decl ctors `0x1403a52a0` (ToMantle, `[+0x68]=2`), `0x1403a4fb0` (Snap, `=0`), `0x1403a3b80` (Blink, `=0x22`). VERIFIED |
| Player state | `arkPlayerStateBlinking` (RTTI/vtable `0x141c815d8`, decl `arkDeclPlayerStateBlinking`) | the *cast* (the teleport tween) state, pool index 12 in `docs/native-attack-gating.md`. It does not aim; it consumes the destination the power resolved. Strings: "Blink cancelled: ..." `0x141c80b28..`. (INFERRED consumer relation) |
| `ark_player_new_blink` cvar | reg `0x141386b00`, default string `"1"` at `0x141abefb8`, "If true, use the new blink sensor." | value read at `0x14061e230` via `DAT_143987e48`. VERIFIED read, default value INFERRED on |

There are no camera-independent code paths: strings `arkPowerBlink`/`arkPlayerStateBlink` do not exist; the real names are above.

### Component update stages (so "when" can be reasoned about)
`arkComponentUpdateStage_t` initializer `0x1419f1a20`: PRE_INIT_FRAME 0, QUERY_PHYSICS 1, DEFAULT 2, POST_THINK 3, ANIMATION 4, PRE_PHYSICS 5, POST_PHYSICS 6, PRE_RENDER 7, MAX 8. VERIFIED.
A component factory's vtable slot `+0x10 + 8*stage` holds that stage's job thunk. Anchors: `arkCpntPlayerSensorBlinkFactory` slot `+0x18` is `0x954620`, whose job contains the string
"arkCpntPlayerSensorBlink::UpdateQueryPhysics" (so `+0x18` = stage 1, VERIFIED); `arkCpntPlayerCameraFactory` (`0x141d04698`) slot `+0x48` (stage 7) is `0x9533c0`, whose body is `0x7f6a80(camera)`, the
hooked function.

| Stage | Camera job | Blink power | Blink sensors |
|---|---|---|---|
| 1 QUERY_PHYSICS | `0x954490` -> `0x7f72c0` (does not write the aim) | **`0x954d00` -> `0x1406205e0` -> `0x14061ede0`** | Blink `0x954620` -> `0x1403a4a20`; PowerSnap `0x954a30` -> `0x1403c3870` |
| 2 DEFAULT | `0x94f9a0` -> `0x7f6140` | | Blink `0x94fc20` -> `0x1403a4620` (computes `m_blinkMaxDistance`); Snap `0x1403c2b90` |
| 7 PRE_RENDER | **`0x9533c0` -> `0x7f6a80` -> `0x7f4b30`** (the only per-frame aim writer) | | |

Writers of `cam+0x2527c..0x252a8` in the whole exe (operand scan of 0x25270..0x252b0): `0x7f4b30` (per frame), `0x7ef5a0` (camera ctor, callers `0x90bad0/0x910ce0/0x944750`), `0x7f2aa0` (reset, caller `0x7f1d80`).
Every other hit is a reader. VERIFIED. So the value Blink reads at stage 1 is whatever `0x7f4b30` (plus the hook) left at stage 7 of the previous frame.

## 2. What `0x14061ede0` does with the ray (VERIFIED, abridged from the decompile)

```c
cam   = *(power+0x160);                      // arkCpntPlayerCamera
dir   = (float3*)(cam+0x25288);              // aim axis row 0 (forward)
maxd  = 0x14061e230(power, dir);             // reach along this direction, see 4
h     = shape[stance].topY - probeShape.topY;           // power+0xb0.. / +0xd0 shapes (body capsules), stance = body+0x50
start.x = cam.origin.x(+0x2527c) + h*0;      // DAT_14228a940..948 = (0,0,1)
start.y = cam.origin.y(+0x25280) + h*0;
start.z = playerEntity(+0x308) + h;          // Z does NOT come from the camera origin (+0x25284)
0x14061e500(power, start, dir, maxd, shapeIdx=4, allHitsCollector);   // swept shape cast, first non-ignored hit wins
// BlinkSnap sensor (power+0x180): if valid and within maxd, its point (sensor+0x80..0x88) replaces the hit and sets flag +0x188
power[+0x1e8..+0x1f0] = raw aim point  (= start + hitFrac*dir)
power[+0x1f4..+0x1fc] = resolved blink destination (backed off by power+0x260 body radius; then a <=10 step (0.4 each, DAT_141ac97a0)
                        "walk back along -dir" loop with two vertical probes (0x14061e6c0) to find standable ground)
```
Then `0x1406205e0` ledges and writes the marker:
`0x14061f9e0` classifies the stance that fits at the destination (`+0x204` 0/1/2, `+0x200` = 0/6/10), the ToMantle sensor (`power+0x178`, destination written to `+0x290..0x298`, orientation `+0x29c..`) decides ledge/mantle
(`sensor+0x2fc` = ledge height; accepted when the height above the destination is within decl window `decl+0x1f4..0x1f8` and a ratio test `< decl+0x1fc/0x200`), `0x14061fd20` keeps the previous snapped result
while the live camera forward stays within `acos(power+0x1e0)` (`decl+0x19c` degrees) of the saved one, and the marker FX are placed with `0x14087dc10(entity, fxId, power+0x214/+0x220/+0x22c, ...)`.

Consequences for a hand ray:
* **Direction** (`cam+0x25288`) is used everywhere; **origin X/Y** come from `cam+0x2527c/80`; **origin Z is NOT read from the camera** at all here (entity Z `+0x308` plus a stance offset). A hand origin below/above
  the head therefore does not move the cast vertically; it only shifts the start in XY. (INFERRED consequence: if the hand is held 0.5 m in front of the torso, the cast starts 0.5 m forward in XY at body-top height; with the arm
  pushed into a wall that start can be inside geometry.) Recommended: substitute the **direction only**, keep the native origin (see 6).
* The sensors (`0x1403a4a20`, `0x1403c3870`) take origin Z from the camera (`+0x25284`) too, so a hand origin would make power and sensors disagree by the hand-head height difference. Another reason to keep the native origin.

## 3. Other readers of the same pose (all VERIFIED reads)

| Function | Stage | Reads | Use |
|---|---|---|---|
| `0x1403a4a20` (Blink sensor UpdateQueryPhysics) | 1 | origin `+0x2527c`, rows `+0x25288/+0x25294/+0x252a0` | two fans of physics ray lines (vertical/horizontal cross-blocks, `decl+0x70/0x74/0x7c/0x80`), length from `0x1403a3f20` (same pitch ellipse as 4) |
| `0x1403a4620` | 2 | dir `+0x25288` | folds the line results into `sensor+0x88 m_blinkMaxDistance` |
| `0x1403c3870` (`arkCpntPlayerSensorPowerSnap::UpdateQueryPhysics,aabbCast`) | 1 | all 12 floats via `sensor+0x20 -> +0x310` (camera) | aabb cast cone of length `sensor+0x44`, spread `sensor+0x40` degrees; results consumed by Snap `0x1403c2b90` |
| `0x14061fd20` | within `0x1406205e0` | origin and dir | ledge hysteresis |
| `0x14061ec20` (vtable `+0x120`) | | origin XY | FX offset |

## 4. Vertical reach, ledge, max distance (VERIFIED structure, trig INFERRED)

`0x14061e230(power, dir)` returns the reach along `dir`:
* If `DAT_143987e48` (value of `ark_player_new_blink`, default "1") is non-zero: `reach = shape[2].radius_field(**(power+0xc0 +8)) + sensor(0x22)+0x88` (the BlinkSensor's `m_blinkMaxDistance`, which `0x1403a4620`
  already clamps by the cross-block ray results). **This is the default path** (INFERRED that the cvar is non-zero at runtime; the default string is `"1"`).
* Otherwise (legacy): pitch `a` of `dir` (`0x1401121b0` * 57.2958, wrapped to +-180), `h = attribute[power+0x278]` (`ARK_ATTRIBUTE_BLINK_HORIZONTAL_DISTANCE`), `v = decl(+8)+0x198`. Near level or +-90 deg it returns `v`
  (constants `DAT_14228a750 = pi/2`); otherwise the radius of the ellipse with semi-axes `h` (horizontal) and `v`:
  `r = h*v / sqrt(v^2 cos^2 a + h^2 sin^2 a)`. A tilted hand ray simply shortens/lengthens along that ellipse, no discontinuity. The sensor's own line length (`0x1403a3f20`) uses the identical formula.

So a tilted hand ray behaves sanely **without any clamp of our own**: reach is a smooth function of pitch, ledge height is measured at the resolved point, the stance probe (`0x14061f9e0`) lowers the
destination to a stance that fits, and the snap sensor only accepts points inside `reach`. Practical guard for the mod: clamp the substituted forward to the same +-85 deg pitch the head can produce so the
`a ~ +-90` branch (returns `v` only) is not hit by a hand pointing straight up/down. (INFERRED nicety, not required for correctness.)

## 5. Why the marker may still follow the head today (hook-side, not RE-side)

The reader chain above reads the rewritten bytes, so check, in `native-left-aim.csv` (one row per applied/rejected candidate) *while a Blink is being aimed*:
1. `applied` stays 0 / the file has no rows during Blink aim -> a gate of `NativeGameplayAim::plan` rejected it: `native_mode!=1` (i.e. `0x7f6a80` passed `mode 0`, which happens when the camera mode `cam+0x25200/04` is `0xd`
   corpse/`0x10`), `logic_mask!=2`, `tracked_` older than 250 ms (published only while `left_trigger>=.1`, +400 ms), hand more than 1.5 m from the native origin, or `state->authored_view()`.
2. Rows are applied but the marker still follows the head -> something between the write (stage 7) and the read (stage 1) restores the bytes. Static scan says nothing does (section 1), so this would be a surprise
   and the observer hook in 6 would show it immediately (compare `*(power+0x160)+0x25288` at `0x1406205e0` entry with the published hand axis).
3. The camera object differs: the hook keys on the camera that `native_player_camera()` resolved; the power uses `*(power+0x160)` (class `arkCpntPlayerCamera`). If the two pointers differ (second camera component),
   the hook writes the wrong one. The observer in 6 logs both.

## 6. Recommended hook design

**Step 1 (observe, 0 risk): hook `0x1406205e0`.**
* Signature `void __fastcall(void* power)`, RCX = power. Entry bytes (32, for install-time verification):
  `48 8b c4 55 57 48 8d 68 a1 48 81 ec f8 00 00 00 80 b9 a4 00 00 00 00 48 8b f9 0f 84 16 0b 00 00`.
* Sole code call site: `0x954d2e` in the job thunk `0x954d00` (`E8 AD B8 CC FF`, target `0x1406205e0`, verified rel32). Thunk entry bytes:
  `48 89 5c 24 08 48 89 74 24 10 57 48 83 ec 20 48 8b 1a 48 8b fa 48 8b f1 48 3b 5a 18 73 1f 66 90`. (Hooking the thunk instead is possible but it iterates a list of components; the inner is simpler.)
* Aim-active predicate: `*(uint8_t*)(power+0xa4) != 0 && *(uint8_t*)(power+0xa5)==0`.
* Log: `cam=*(void**)(power+0x160)`, 48 bytes at `cam+0x2527c`, `power+0x1e8..0x1fc` after the original returns (raw aim point and resolved destination). Compare with `native_player_camera().camera`.

**Step 2 (override, minimal): same hook, direction-only substitution.**
```
on entry, if aim-active and left-aim pose fresh (same freshness/owner gates as NativeGameplayAim):
    cam = *(void**)(power+0x160)   // must equal the owned camera, verify vtable/identity like native_player_camera
    read 48 bytes B at cam+0x2527c
    write axis (cam+0x25288, 36 bytes) = hand basis (rows: forward, left, up; same convention as controller_gameplay_aim),
          keep origin (cam+0x2527c, 12 bytes) native
    original(power)
    (do not restore: the next 0x7f4b30 regenerates all 48 bytes at stage 7, "build always regenerates the inherited pose first")
```
Why no restore: sensors `0x1403a4a20` / `0x1403c3870` read the same bytes in the same stage and may run concurrently with the power job (INFERRED: job scheduling inside a stage not verified), so a
bracketed temporary override would be seen by some readers and not others. A persistent write until the next stage-7 rebuild is consistent for all of them and is what the existing hook already does at stage 7;
this only adds a guaranteed write immediately before the Blink read, independent of the stage-7 gates (`caller==0x7f727a`, `native_mode`, `logic_mask`).
Keep the native origin (section 2: the solver ignores camera Z, sensors use it; a hand origin makes them disagree).

**Step 3: marker.** The marker is placed from `power+0x1e8/0x1f4` and the FX ids `power+0x280/+0x284/+0x288` via `0x14087dc10`; no further hook is needed once the solver sees the hand direction.

Also valid but worse alternatives: hooking `0x14061ede0` (misses `0x14061fd20` and the sensors' same-frame view of the aim, and the saved aim copy at `power+0x1c8/0x1d4` written in `0x1406205e0` would hold the head),
or `0x14061e500`/`0x14061e6c0` (low level shape casts, take explicit start/dir: arg1 power, RDX = start float4*, R8 = dir float3*, XMM3 = length; usable for a pure ray override but they are called for many other purposes).

## 7. Open items

* Live confirm (cheap, Step 1): that `*(power+0x160)` is the same object as the hooked camera, and that `+0x25288` at the Blink read equals the published hand axis.
* `ark_player_new_blink` runtime value (`DAT_143987e48`) not read; only changes which reach formula applies (4).
* Decl field names for `arkDeclCpntPowerBlink` (`decl+0x198/0x19c/0x1b0..0x1b8/0x1f4..0x200`) were not resolved; their meaning above is from use, INFERRED.
* Stage-scheduling concurrency inside one stage is not verified (affects only whether a bracketed override would be safe).
