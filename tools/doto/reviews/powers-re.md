# DOTO powers RE: Displace, Voltaic Gun, Foresight, Semblance

Static RE only (capstone over `Dishonored_DO.exe`, image base 0x140000000, every address below is an RVA).
Nothing was launched, attached to, or written. Tags: **PROVEN** = read in the disassembly / vtable / ctor;
**INFERRED** = deduced; **OPEN** = needs a read-only live check (`tools/entities.py`-style ReadProcessMemory).

Conventions: `power` = the `arkCpntPower*` component; `cam` = `arkCpntPlayerCamera` (decl info `0x3ab82b0`, proven by the
static initializer `0x149f420`, which registers the string `arkDeclCpntPlayerCamera` next to it); `machine` =
`arkCpntPlayerStateMachine`. DOTO camera aim block = D2 +0x30: origin `cam+0x252ac` (x,y,z), axis rows `cam+0x252b8`
(forward) / `+0x252c4` (left) / `+0x252d0` (up); camera pitch `cam+0x25294`, mode `cam+0x25230`, target mode `cam+0x25234`,
mode queue `cam+0x25238` (count `+0x25240`).

---------------------------------------------------------------------------------------------------

## 0. Shared facts

| Fact | Evidence |
|---|---|
| Vtable RVAs | `arkCpntPowerSwapper` 0x1c44a18 (60 slots), `arkCpntPowerTheEye` 0x1c451a8 (47), `arkCpntPowerMimic` 0x1c44518 (47), `arkCpntWeaponGaussGun` 0x1c45768 (48), `arkCpntWeaponPlaceableAfar` 0x1c45a88 (51), `arkCpntSwapper` 0x1c52b28, `arkCpntMimic` 0x1c49448, `arkCpntPlayerSensorSwapper` 0x1c4fed8, `...SwapperCross` 0x1c50138, `...SwapperOcclusion` 0x1c503b8, `...SwapperToMantle` 0x1c50628, `arkCpntPlayerSensorBlinkSwapper` 0x1d70668, `arkCpntPlayerSensorBlinkCross` 0x1c4aa08 |
| Power vtable roles (PROVEN, base `arkCpntPower` 0x1cab888) | slot 34 (+0x110) `0x6719f0` "stop if `+0xa4`"; slot 35 (+0x118) `0x671950` **StartAim** (writes `power+0xa4=1`); slot 36 (+0x120) `0x671b50` **Cast**; slot 37 (+0x128) `0x671b00` **StopAim** (clears `+0xa4`, `+0xa6`); slot 44 (+0x160) `0x671ce0` SetActive(bool). Factory vtable index `2+stage` holds the stage job (1 QUERY_PHYSICS, 2 DEFAULT, 7 PRE_RENDER). Base init `0x671da0` registers the power with the input layer: `0x7e8f70(0x97d090(player), decl+0x1a0, power)` (decl +0x1a0 = the power's slot id) |
| Per-frame jobs | Swapper: stage 2 `0x9c8930` -> `0x3b9280` (call 0x9c895e), stage 7 `0x9cb950` -> `0x3ba6e0` (-> `0x3b93d0`, `0x3ba4b0`). TheEye: stage 2 `0x9c8980` -> `0x3bfbb0` (call 0x9c89ae), stage 7 `0x9cb9a0` -> `0x3c10e0`. Mimic: stage 2 `0x9c87f0` -> `0x3b5f80` (call 0x9c881e). Blink (D2 0x954d00 maps exactly to `0x9cd420`) -> `0x674200` |
| Input action names in the exe (name tables only, no code refs; ids = table order) | `ACTION_POWER_WEAPON`, `ACTION_POWERWHEEL`, `ACTION_QUICK_0..3`, `ACTION_ZOOM`, `ACTION_USE`, `ACTION_INTERACT`, `ACTION_STATE_THE_EYE`, `ACTION_EYE_MARK`, `ACTION_MIMIC_CHOKE`, `ACTION_RAT_WHISPER`, `ACTION_USE_HOLLOW`, `ACTION_QUICK_USE_HEALTH_POTION`, `ACTION_QUICK_USE_MANA_POTION`. There is **no** displace/swap/cast action: place and swap are not separate inputs (see 1.5) |
| Player state ids (decl ctor `[this+0x60]`, PROVEN) | Swapper **93 (0x5d)**, DespawnSwapper 101, Mimicking 96, UseMimic 102, CancelMimicking 100, UsePower 63, Powerwheel 83, BriskEye 36, RatWhispers 89, Blinking 12, Idle 22, Block 44 (unchanged from D2). 0x5d is the literal in the Swapper cast (`0x3b7040`, `mov edx,0x5d`) and in `IsInState(0x5d)` at `0x3b946e` |
| Decl infos | `0x8e5050(entity, declInfo, flag)` = GetComponent by decl info; names resolved from static initializers (`0x3a8e370` PlayerSensor, `0x3a9a560` PlayerStateMachine, `0x3ab82b0` PlayerCamera, ...) |

---------------------------------------------------------------------------------------------------

## 1. Displace (arkCpntPowerSwapper + arkCpntSwapper + sensor family)

### 1.1 Architecture (PROVEN)

* The power owns a primary **sensor**, not a thrown weapon. `0x48b520` (PowerClone slot 3, shared) ends with
  `[power+0xc8] = 0x7db6d0(sensors, vslot(+0x198))`; Swapper's +0x198 slot (index 51) is `0x9bd980` = `mov eax,0x26`.
  Sensor id **0x26** is the decl ctor `0x405730` (`mov [rbx+0x68],0x26`, vtable 0x1c4fd88 = `arkDeclCpntPlayerSensorSwapper`), so
  **`power+0xc8` is an `arkCpntPlayerSensorSwapper`**.
* That sensor aggregates (init slot 3 = `0x4058a0`; names via static-init strings 0x3ab83d0/0x3aa03c0/0x3a94250):
  `sensor+0x130` = `arkCpntPlayerSensorBlinkSwapper`, `+0x138` = `...SwapperToMantle`, `+0x140` = `...SwapperCross`.
* Stage-2 sensor job `0x9c8250` -> `0x4059c0`: copies the BlinkSwapper sub-sensor result into the Swapper sensor:
  `sensor+0x148..0x150` (target point, 3 floats), `+0x154..0x174` (orientation/normal), mirrored to `+0x94..0x9c` and `+0x60..0x68`.
  `0x3b6ad0` (Swapper vslot 59) = "has target" (`sensor+0x148..0x150` != sentinel). **These are the read-only observables for the marker.**
* `power+0x100` is a component handle to `arkCpntWeaponPlaceableAfar` (`0x425050(handle)`, virtual +0xd0), used by `0x3b7040`, `0x3b93d0`.
  `arkCpntSwapper` (`arkCpntSwapper::UpdateDoPhysicsQueries`) is the placed Billie-double entity; cvars `g_swapperClone`, `g_swapperSnapToGround`,
  `g_swapperNpcExplosion`. Player states Swapper (93) and DespawnSwapper (101) perform swap/despawn.

### 1.2 Where the aim ray / target point comes from (PROVEN, one OPEN)

Both readers take the **player camera component**, not an aim-output object:

1. **Destination solver = BlinkSwapper sensor** (stage-1 job thunk `0x9cccf0`, call at `0x9ccd1e` -> `0x3e21b0` -> solver `0x3e0c60`):
   * start X/Y = `cam+0x252ac/+0x252b0` (insns `3e0dea addss xmm2,[rdx+0x252b0]`, `3e0dfa addss xmm3,[rdx+0x252ac]`, `rdx=[[sensor+0x20]+0x368]`, machine+0x368 = camera);
     start Z = entity Z (`[entity+0x318]`) + stance height. Same recipe as D2 Blink ("origin Z is not read from the camera").
   * direction = virtual +0xe0 of `container[[sensor+0x18c]]` (`3e0cdc..3e0cf6`). `[sensor+0x18c]` = **0x22** (shared ctor `0x3dfd60`, insn `3dfef2 mov [rbx+0x18c],0x22`),
     and 0x22 is `arkDeclCpntPlayerSensorBlinkCross` (ctor `0x3e2920`). BlinkCross vslot 28 = **`0x3e6520`**, which is: `rcx=[[this+0x20]+0x368]` (camera), then copies
     **`cam+0x252b8 .. cam+0x252d8` (the 3x3 aim axis) into the out buffer**.
   * So the destination solver direction is `cam+0x252b8` and its XY origin `cam+0x252ac`: exactly the block `native_gameplay_aim` writes (D2 +0x2527c/+0x25288) and the D2 Blink hook overwrote.
2. **Generic ray (Swapper sensor, stage-1 job `0x9cd060`, call `0x9cd08e` -> `0x3fecb0`)**, derived from `arkCpntPlayerSensorBase` (shared with Navmesh/Catfall/DevouringSwarm):
   main-ray block `3fef99..3ff0a8` uses `rbx = 0x86d8e0(cam)` = **`cam+0xc8`** (origin) and `0x86d860(cam)` = **`cam+0xec`** (first row of the camera's 3x3 *view* basis; `0x859760` snapshots both),
   length `decl+0x70`, submitted via `0x970b0` (handle at `sensor+0x50`). `sensor+0x120` = camera (init `0x3fe0e0`).
   **OPEN:** what consumes the `+0x50/+0x54` ray results for Displace (stage-2 `0x4059c0` uses only the BlinkSwapper sub-sensor): treat it as validation/occlusion. Note these are the *view* eye/basis, not the aim pose.

Other readers of `cam+0x252b8/0x252ac`: marker orientation `0x3ba150` (Swapper vslot 56, insns `3ba29c`, `3ba2fe..3ba30f`), weapon aim sensor `0x402498`, weapon aim helpers `0x68e800/0x68ec19/0x68eca4`, Blink fallback `0x674353`.

### 1.3 Marker placement per-frame functions (PROVEN)

* `0x3ba590` (called from `0x3b9280` at 0x3b9303 when `power+0x259==0`, and from `0x3ba130`): copies the sensor's validated target `[sensor+0x148..0x150]` (virtual +0xd0 = valid) into two marker FX entities (`0x1a3b20(entityMgr,id)`, ids `power+0x23c`, writes `+0x38..+0x64`); hides them via `0x3b9120` at the sentinel.
* `0x3b93d0` (stage 7 via `0x3ba6e0`): big validation pass: camera eye `0x86d8e0`, averages hit positions of the results list `[sensor+0x58]`, `IsInState(0x5d)`, swapper entity id tracked at `power+0x240`.
* There is **no stage-1 power job** for Swapper, so the D2 trick (write the camera aim from inside the power job before the sensors) has no direct twin. The sensors read the aim made at stage 7 of the previous frame by `0x85d170` (where `native_gameplay_aim` already hooks; caller `0x85d96a`).

### 1.4 How the D2 Blink hook maps

D2 `native_blink_aim.hpp`: MinHook on the Blink power per-frame fn `0x6205e0` (job thunk `0x954d00`, call `0x954d2e`), gate `power+0xa4!=0 && +0xa5==0`, hand published by the left-aim gates, writes **only the 36 direction bytes** `cam+0x25288` once before the original, no restore (stage 7 regenerates), origin stays native. Pins: 32 B prologues of fn and thunk, the `e8` call site.
DOTO: `0x6205e0 -> 0x674200` is **Blink only** (camera at `power+0xc0`, aim read only in a fallback branch; do not reuse for Displace). Displace has no power-level aim fn; the injection point is the sensor job that runs the solver, direction target `cam+0x252b8` (36 B), origin untouched.

### 1.5 Place vs swap (PROVEN structure, OPEN `+0x259` semantics)

* One Cast virtual (`0x3b7040`, vslot 36). First test `cmp byte [power+0x259],0 / je 3b73a2` falls into the generic clone-cast `0x48b220`. With `+0x259!=0` it runs the swap path: marker position either `[power+0xc8]+0x94` or the swapper entity (`[r15+0x58]` -> `entity+0x310`), LOS test `0x5366e0`+`0x6143c0` (result `power+0x25b`), then requests **player state 0x5d** (`0x869760(machine,0x5d,req)` at `3b739b`, or `0x86d0a0` for the fast-swap attribute).
* StartAim `0x3b6970` (vslot 35): base `+0xa4=1`, **enables the primary sensor** (`[power+0xc8]+0x38==0 -> vf +0xd8(sensor,1)`, `3b6985..3b6990`), stores entity id `power+0x130` and time `+0x138`, writes dword `+0x259=1`.
  StopAim `0x3b6ed0` (vslot 37): base stop, **disables the sensor**, hides the marker `0x3b9120`, writes word `[+0x259]=1`, `[+0x25b]=0`.
  **OPEN:** both write `+0x259=1`, so "+0x259 = marker placed" is contradicted; read `+0x259/+0x25a/+0x25b/+0x25c` live while aiming/placing/swapping to name them.
* Per-frame `0x3b9280`: `power+0x250` marker expiry (double, sentinel 0x7fefffffffffffff) cleared unless `IsInState(2|5|3)`; `power+0xfc &= (now-power+0x130 > 0)`.
* Conclusion: Displace is driven by the generic power events (StartAim/Cast/StopAim) the mod already bridges. "Place" = Cast with no marker, "swap" = Cast with a marker, "cancel" = DespawnSwapper (101). The VR swap gesture must call the same Cast a second time, not a different action.

### 1.6 Concrete hook proposal

Step A (no new native code, verify first): while the Swapper power has `power+0xa4!=0` (or `sensor+0x38!=0`), make the existing power-hand **camera-aim route** (`native_gameplay_aim`, writes `cam+0x252ac/+0x252b8`) publish, because `0x3e6520` and `0x3e0c60` read exactly those bytes in stage 1. Check read-only: `cam+0x252b8` vs hand forward, and `sensor+0x148..0x150` following the hand.

Step B (if the route does not steer it, same situation as D2 Blink): `NativeDisplaceAim` mirroring `NativeBlinkAim`:

| Field | Value |
|---|---|
| Target | **`0x3e21b0`** (BlinkSwapper stage-1 job, `void(sensor*)`, only caller thunk `0x9cccf0` at **`0x9ccd1e`** = `e8 8d 54 a1 ff`) |
| Optional 2nd target | **`0x3fecb0`** (Swapper ray job; thunk `0x9cd060`, call **`0x9cd08e`** = `e8 1d 1c a3 ff`): swap `cam+0xc8` (12 B) and `cam+0xec` (12 B) for the call and restore (gadget_aim FieldSwap pattern) |
| Argument | `rcx = sensor` (`+0x20` machine -> `+0x368` camera, `+0x38` enable byte, `+0x18c` Cross id 0x22) |
| Gate | `sensor+0x38!=0` AND Swapper power `+0xa4!=0` AND hand fresh (<=250 ms) AND not authored view; else call the original untouched |
| Replace | before the original write the hand 3x3 (forward/left/up, pitch clamped like `blink_aim_axis_from_hand`) to `cam+0x252b8` (36 B); do **not** touch `cam+0x252ac` (XY native, Z from the entity); no restore |
| Pins (32 B, from the exe) | `0x3e21b0`: `48 8b c4 53 48 81 ec 00 01 00 00 80 79 38 00 48 8b d9 0f 84 16 07 00 00 c6 81 88 01 00 00 01 f3`; thunk `0x9cccf0`: `48 89 5c 24 08 48 89 74 24 10 57 48 83 ec 20 48 8b 1a 48 8b fa 48 8b f1 48 3b 5a 18 73 1f 66 90`; `0x3fecb0`: `48 8b c4 55 57 48 8d 68 a8 48 81 ec 48 01 00 00 80 79 38 00 48 8b f9 0f 84 8b 05 00 00 f3 0f 10` |
| Observation | read `sensor+0x148..0x150` after the original for the status JSON (`last_dest`) |

Ordering (INFERRED): the solver and its direction reader `0x3e6520` run in sequence inside `0x3e0c60`, so a write right before `0x3e21b0` is seen; the camera regenerates the block at stage 7.

---------------------------------------------------------------------------------------------------

## 2. Voltaic Gun (arkCpntWeaponGaussGun)

### 2.1 Answer: yes, the fire path goes through the launch function the hand aim already steers (PROVEN)

* D2 `0x639970` -> DOTO **`0x68e970`** (translate.py: exact). DOTO prologue (32 B): `48 89 5c 24 10 48 89 6c 24 18 56 57 41 57 48 83 ec 30 48 8b 41 10 4c 8b f9 49 8b e9 8b 88 b4 00`;
  **one byte differs from the D2 pin**: byte 30 is `b4` (owner id +0xb4) instead of `ac`. Args unchanged: `rcx` weapon, `rdx` projectile entity, `r8` float3 origin, `r9` 3x3 rows, 5th float at `[rsp+0x20]`, more stack slots.
* GaussGun fire = event **206** (D2 `ARK_MDNT_MSG_WEAPON_FIRE` 200 + 6; every weapon event is +6, D2 242 -> DOTO 248). Registered in GaussGun slot 5 `0x3c26b0` (`0x877bf0`/`0x8781c0`, `r8d=0xce`): handler **`0x3c2d20`** (`jmp 0x3c1d70`); event 0xcf -> `0x49f020` (`jmp [vt+0x160]`).
  Same walk for the others: Gun `0x49c130`: 206->`0x49cfa0` (`jmp 0x49bf20` = D2 `0x44e0c0`, exact), 207->`0x49cfb0`, 208->`0x49cf40`, 209->`0x49cfe0`, 210->`0x49d020`, 211->`0x49d030`, **248**->`0x49cf20`; Crossbow `0x6803c0`: 206->`0x680740` (`jmp 0x67fcc0` = D2 `0x62b560`, exact), 207->`0x49f020`, 210->`0x49d020`.
* Fire fn **`0x3c1d70`**: first `call [vt+0x110]` (CanFire = Gauss slot 34 `0x3c1b90` -> `0x68d070`), then the same shot assembly as the other guns and the launch at **`0x3c219d`** (`call 0x14068e970`): `r8=&muzzle (rbp-0x68)`, `r9=&axis (rbp+0x10)`, `xmm8` 5th arg, stack (`rbp+0xc8`, `rbp+0xc0`, -1, 0).
  `0x68e970` callers (rel32 scan of the whole `.text`): `0x3c219d` Gauss, `0x49cac2` Gun (D2 `0x44ebfc`, fn `0x49c860`), `0x49efb8` Placeable, `0x680035` Crossbow (D2 `0x62b8cc`), `0x691751` Ranged base. D2's single-target call `0x633419` (fn `0x632f70`) has no exact DOTO twin.
* Direction source (PROVEN): `0x3c1d70` calls `0x68e710(weapon,&t)` = `0x402980(sensors[9], &t, decl+0x1f8)`; sensor 9's job `0x402498` copies the **camera aim block `cam+0x252ac..0x252dc` into `sensor+0x48..0x78`** (`4024d8..40250b`). Then `0x68e1c0` refines, `0x68db40` computes the muzzle from the weapon-local offset `weapon+0x104..0x10c` (from a model joint via `0x58d2a0` at `0x3c1ffe`) transformed by the owner entity (`[owner+0x310]`, rows `+0x31c`), direction = normalize(target - muzzle) (`3c2104..3c214f`), axis by `0x115360(dir,out)` (call `0x3c215e`).
  So the hand already steers it two ways: the camera-aim route (target from `cam+0x252ac/+0x252b8`) and the launch swap at `0x68e970` (origin + frame).

### 2.2 vtable differences (Gauss 0x1c45768 vs Gun / Crossbow 0x1cad088 / Ranged 0x1caea88)

Identical to Ranged/Crossbow: slots 10-13, 21, 27-31, 35, 36, 38, 40-42, 44, 45. Different:

| slot | off | Gun | Crossbow | **Gauss** | role |
|---|---|---|---|---|---|
| 1 | 0x08 | 0x49bc30 | 0x67f8f0 | 0x3c18e0 | dtor |
| 3 | 0x18 | 0x49c040 | 0x680180 | 0x3c2530 | init (all call `0x68eee0`) |
| 4 | 0x20 | 0x49c0d0 | 0x680360 | 0x3c2600 | deinit |
| 5 | 0x28 | 0x49c130 | 0x6803c0 | **0x3c26b0** | event registration |
| 6 | 0x30 | 0x49c2d0 | 0x680490 | 0x3c2770 | unregistration |
| 23 | 0xb8 | 0x68f400 | 0x68f400 | 0x3c2900 | ammo/UI (`0x3c1410`, `0x5a8c50`) |
| 32 | 0x100 | 0x49bc60 | 0x67f9a0 | 0x3c1980 | per-frame/anim sync |
| 33 | 0x108 | 0x49bfe0 | 0x680100 | 0x3c2460 | |
| 34 | 0x110 | 0x68d070 | 0x67fba0 | 0x3c1b90 | **CanFire** (first call of the fire fn) |
| 37 | 0x128 | 0x49ce60 | 0x690dc0 | 0x690dc0 | |
| 43 | 0x158 | 0x68e400 | 0x68e400 | **0x3c1bd0** | called at the start of the fire fn (`call [rdx+0x158]`, `0x3c1e06`) |
| 46 | 0x170 | 0x68e900 | 0x68e900 | **0x3c1d20** | projectile/ammo getter (`call [rax+0x170]`, `0x3c1e4f`) |
| 47 | 0x178 | 0x49c790 | 0x680580 | 0x3c28c0 | |

Fire-fn layout: `weapon+0xb0` fire byte (from `+0xe0`), `+0xc8` flags, `+0x104..+0x10c` muzzle offset, `+0x178` ammo-type index (`[weapon+0x30]+0x48+idx*40+0x24`), `+0x180` second component, `+0x8` decl, `+0x10` owner, `+0x40/+0x48` sensors / camera holder, `+0x20` handle for the joint query. These are new versus the D2 pistol component; the weapons.json "unverified" generator rewrites stay unverified for GaussGun.

### 2.3 Implementation plan

1. `NativeShotAim`: set pin byte 30 to `b4`; add call site `0x3c219d` (rel32 -> `0x68e970`) to `shot_aim_calls`; keep Crossbow `0x680035` and Gun `0x49cac2`. Publish the **forearm/wrist** frame as the hand pose for this weapon (origin = wrist muzzle, forward = arm direction).
2. Keep `native_gameplay_aim` publishing the same pose while the Gauss gun is the active weapon (the aim sensor `0x402498` feeds the target; otherwise target and launch frame disagree by the muzzle offset).
3. Do not port the dual-wield event hooks (`0x44f430` family); equivalents are `0x3c2d20` / `0x49f020`, only needed for a second gun (none exists).
4. Live read-only checks first: `weapon+0x104..0x10c` vs wrist, call counter of `0x68e970` from `0x3c219d`, the 5th arg.

---------------------------------------------------------------------------------------------------

## 3. Foresight (arkCpntPowerTheEye)

### 3.1 Camera: not a separate camera (PROVEN entry/exit, OPEN steady state)

Init `0x3bd8e0` resolves: `power+0xb0` Mana, `+0xb8` HealthEvolved, **`+0xc0` PlayerController**, **`+0xc8` PlayerStateMachine**, `+0xd0` AnimEvolved, **`+0xd8` PlayerPhysics**, `+0xe0` PlayerSensor, **`+0xe8` PlayerCamera**, `+0xf0` EngineAudio.

* Enter (state-3 enter `0x3bb7d0` -> `0x3bf180(power, player)`): saves the **player's own body pose** (`power+0x358..0x360` origin from `entity+0x310..0x318`, `+0x364` yaw, **`+0x368` = `cam+0x25294` (camera pitch; D2 0x25264)**, `+0x36c` physics value, `+0x378..0x380` = camera eye `0x86d8e0(cam)`), then **moves the player entity itself** with `0x8eab40(entity,&power+0x378,entity+0x31c,1)` (insn `3bf2f9`; the same SetOriginAndAxis `0x68e9d1` uses for projectiles). Also `0x85ee00(controller)`, `controller[+0x179]=0`, `0x86cff0(machine,0xd)`, `physics->body+0x20a=0`, `0x1bb240/0x1ba2b0`. `0x3bf060` fills the camera screen-effect record at **`cam+0x888`** (flag dword `+0x40` = 1 or 2, mirrored in `power+0x410`; values from decl +0x28c/+0x2a8/+0x2ac/+0x2b0/+0x2c8/+0x2cc).
* Exit (`0x3bc680` -> `0x3be0b0`): restores origin/yaw (`0x8eab40`), `0x85a9b0(cam, saved pitch)`, clears the physics flags and **`0x85ab40(cam, 0, 0.0)` = PushCameraMode(0)** (insn `3be254`, `xor edx,edx`). `0x85ab40` is the only camera-mode setter (`cmp edx,0x13` = none; queue `cam+0x25238`/`+0x25240`; consumers `0x85c950` -> `0x85b0f0` set `+0x25234`/`+0x25230`).
* Interpretation (INFERRED, high confidence): the player entity detaches from the body and flies under the **normal first-person player camera component**; no authored camera, no camera entity. The mode-0 push is cleanup; the only `0x85ab40` call in the TheEye cluster `0x3b9000..0x3c1500` is `0x3be254`.
  **OPEN live check:** `cam+0x25230/+0x25234` during Foresight. If both are 0, `NativePlayerCamera::authored_view()` is false and hand aim/head tracking apply unchanged. (CORRECTION, section 6: `0x6e5df0` -> `0x6e5f28` is `arkPlayerStateNpcFatality` vtable slot 11, not Semblance; the Semblance states push no camera mode.)

### 3.2 Time (PROVEN mechanism, no global timescale)

* No global timescale write in the power. The freeze is **per entity**: `0x3c0830(power, factor)` (called at enter `0x3bb9b1` with `decl+0x2c0`, and at `0x3bc9c4`, `0x3be089`) walks the entity-id array **`power+0x3f0` (count `+0x3f8`)**, resolves ids via the registry `game+0x327770` (`0x81c320`) and writes the factor into the entity model time-scale layer `entity[+0xf8]+0x1950` (`0x23c3b0 -> 0x349cd0`), or removes it (`0x3486a0`) for a negative factor. The list is fed by the Eye's Havok trigger volume (`arkCpntPowerTheEye::OnTriggerEvent`, body built in `0x3bc3b0`).
* Separate global machinery exists (`idGameTimeManager`: `m_isTimeBent`, `m_isTimeStopped`, `m_timeInfo[5]`; messages `arkMsgTimeScaleWorld/Unbent`) but Foresight does not reach it. Do not key anything on a global timescale; the spirit runs at normal speed.

### 3.3 State to read (PROVEN layout, OPEN numeric meaning)

The power embeds a small state machine: `power+0x190` owner, `+0x198` target, **`+0x19c` current state (0..5)**, callbacks at `+0x1a0 + state*0x18` {enter, exit, update}; job `0x3bfbb0` runs `update[cur]`. Filled by ctor `0x3baea0`:

| state | enter | exit | update | role (INFERRED from the code) |
|---|---|---|---|---|
| 0 | - | - | - | idle |
| 1 | `0x3bb750` | `0x3bc8f0` | - | armed (StartAim sets `+0x19c=1`; sets `entity flags +0x2c |= 4`) |
| 2 | nop | `0x3bca00` | `0x3c0d70` | cast pending (Cast sets `+0x19c=2` only from 1) |
| 3 | **`0x3bb7d0`** | `0x3bca60` | `0x3c0fb0` | **enter spirit**: body saved, entity moved, sensor 0x29 enabled (`0x7db700(sensors,0x29,1)`), `0x3c0830`, `0x3bf060` |
| 4 | `0x3bb690` | **`0x3bc680`** (restore `0x3be0b0`) | `0x3bfbe0` | **spirit active** (update: `+0x418==0` runs `0x3befd0`, `0x3bde40`) |
| 5 | `0x3bb790` | `0x3bca50` | `0x3c0ea0` | leaving |

Mod-side: type-check `vtable RVA 0x1c451a8`, read `u32 power+0x19c` (spirit out of body = 3 or 4), `u8 power+0xa4` (aiming), `power+0x428/0x429/0x42a/0x42d` flags, `power+0x410` (FX mode) and `cam+0x888+0x40` (screen FX flag). Body anchor: `power+0x358` (origin), `+0x364` (yaw). **OPEN:** confirm 0->1->2->3->4 live. Mana drain per frame was not traced.

### 3.4 Applying the VR head pose

* The spirit is the player entity with the normal camera component, so the mod's renderView write (view Setup hook) applies the head pose on top of the camera's eye position with no spirit-specific code (INFERRED; nothing in Foresight bypasses `arkCpntPlayerCamera`, it only edits `cam+0x888` post-FX and the pitch field).
* Risks to check live: (a) `cam+0x25230/+0x25234` during Foresight (authored-view gate); (b) `cam+0x25294` is restored from `power+0x368` on exit, so any VR pitch recentering must not fight it; (c) body anchor/tether at `power+0x358`.
* Locomotion is the standard controller/physics with flags changed, so the accepted VR locomotion/turning should keep working; speed cap not traced.

### 3.5 Implementation plan

1. Read-only `NativeForesightState`: read `power+0x19c`, `cam+0x25230/+0x25234`, `cam+0x888+0x40`; log transitions to `xr-live-status.json`.
2. If `authored_view()` is false in states 3/4: nothing needed for head tracking; add the comfort vignette while the left stick moves in state 4; draw the tether from `power+0x358` to the camera.
3. Tagging = `ACTION_EYE_MARK` (INFERRED to be an interaction-sensor action: it sits at the end of the interaction action name table: ... `ACTION_BUSY_TALK`, `ACTION_MIMIC_CHOKE`, `ACTION_USE_HOLLOW`, `ACTION_RAT_WHISPER`, `ACTION_EYE_MARK`, `ACTION_MAX`); reuse `native_interaction_aim` (hand sphere). Note the `kind 0xe -> 0xf` shift from weapons.json. `ACTION_STATE_THE_EYE` is the player action-state value that filters offers while in Foresight.
4. Displace from Foresight uses the section-1 path unchanged (the player entity is the spirit).

---------------------------------------------------------------------------------------------------

## 4. Semblance (arkCpntPowerMimic)

### 4.1 What selects the target

* The power has **no aim sensor and no ray**. Init `0x3b5650` resolves only PlayerStateMachine (`+0xb0`), `arkCpntMimic` (`+0xb8`, decl info 0x3a93e60), Mana. It is a *mode power*: Cast = enter the mimic state; per-frame `0x3b5f80` runs its own 3-state table (`+0xd8..+0xf8`) plus `0x3b61b0` (face/FX) and `0x3b6240` (mana drain, `IsInState(0x1d)`, `[mimicCpnt+0x4c]==2` = currently mimicking).
* The target is the **NPC being choked/held**: StartAim `0x3b5240` calls `0x86a2c0(machine,0x5f)` (state id 95, not named; neighbours 94 CancelVoidStrike / 96 Mimicking) and tests the current state's sub-object `[[machine+0x98]+0x340]+0x88 == 3` before calling Cast; `0x3b5fd0` tests `[[machine+0x98]+0x308]+0xc0 == 2` with `IsInState(0x5f)`. `ACTION_MIMIC_CHOKE` and message `ARK_MDNT_MSG_MIMIC_CHOKE_WIN` belong to the choke interaction. Player states: UseMimic 102 (take the face), Mimicking 96, CancelMimicking 100.
* So a hand-to-face gesture maps onto the **same hand-sphere/interaction path as the choke** (`native_interaction_aim`, `native_offered_choke`, `native_choke_distance_scope`): the NPC comes from the interaction query, not a ray; the power only needs arming at the moment the choke context reports phase 3.
* **OPEN:** identity of state 95 and of the `+0x308/+0x340` objects (likely the choke state's target/phase records). A live dump of `machine+0x98` (RTTI name) during a normal choke settles it.

### 4.2 Implementation plan

1. Reuse hand-choke selection (face proximity of the casting hand); gate on "Semblance equipped" and call StartAim (vslot 35, +0x118) on the Mimic power when the game's own phase test (`[[machine+0x98]+0x340]+0x88==3`, copied read-only) passes.
2. "Pull away like lifting a mask" = Cast (vslot 36, +0x120) or the game's own UseMimic request; never synthesize states directly.
3. While mimicking (state 96) no Mimic state pushes a camera mode (section 6; the earlier "mode 2" attribution was wrong). Expect mode 0 during free roam; confirm with the live read in 6.6.
4. Wrist HUD face: `arkCpntMimic` fields (`mimicFace`, `m_mimicName`, ...); not traced.

---------------------------------------------------------------------------------------------------

## 5. Claim status

| Item | Status |
|---|---|
| Displace sensor chain (0x26 -> BlinkSwapper / ToMantle / Cross), target at `sensor+0x148` | PROVEN |
| Solver reads `cam+0x252ac/b0` and `cam+0x252b8` (via BlinkCross vslot `0x3e6520`) | PROVEN |
| Existing camera-aim route already steers Displace | INFERRED (live read: `cam+0x252b8` vs `sensor+0x148` while aiming) |
| Place vs swap = same Cast virtual | PROVEN structure; `+0x259` meaning OPEN |
| Gauss fire via `0x68e970` (call `0x3c219d`), event 206, pin byte 30 = `b4` | PROVEN |
| Gauss target from `cam+0x252ac/b8` via aim sensor 9 (`0x402498`) | PROVEN |
| Foresight = player entity under normal camera, per-entity time layer | PROVEN (entry/exit); no non-zero mode/target-mode push anywhere in the power (6.3); steady-state value 0 INFERRED, live read advised |
| Foresight states 3/4 = spirit | INFERRED, OPEN live |
| Semblance gated by choke state, no ray | PROVEN gate code; state 95 identity OPEN |
| Semblance pushes camera mode 2 (`0x6e5df0`) | REFUTED: `0x6e5df0` is NpcFatality (38) slot 11; UseMimic 102 / Mimicking 96 / CancelMimicking 100 push no camera mode (6.2) |
| Mimicking (96) = passive overlay, free locomotion continues | PROVEN structure (per-state active flag, no per-frame body); mode 0 during free roam INFERRED (6.2) |
| Mode 2 = generic animation-driven camera | INFERRED (6.4): Choke, CureChoke, NpcFatality, Versus, Mantle, CharacterChoice |


---------------------------------------------------------------------------------------------------

## 6. Camera modes during Semblance and Foresight

Static only (capstone over the exe; nothing launched or attached). Tags: **PROVEN** = read in the disassembly, **INFERRED** = deduced, **OPEN** = needs data not in the exe (decl files) or a live read.

### 6.1 Machinery (PROVEN)

* `0x85ab40(cam, mode, blend)` is not a stack push but a **FIFO of pending transitions**: `cmp edx,0x13` (13 = none) at `85ab40`; it dedupes against the queue tail / `cam+0x25234` and appends `{mode, blend}` to the queue at `cam+0x25238` (count `+0x25240`). The per-frame consumer `0x85c950` pops the head into `0x85b0f0`, which writes **target mode `cam+0x25234`** immediately (insn `85b109`) and lets **mode `cam+0x25230`** follow after the blend (blend `-1` = camera default `+0x10c` of the camera decl, insn `85b13d`). So `authored_view()` stays true for the blend time after a push of mode 0.
* Callers of `0x85ab40`: 40 `call` sites plus 4 tail `jmp`s (`0x6d6590`, `0x6e5f28`, `0x892103`, `0x8cd616`), found by a byte scan of the whole .text. Nothing else writes `+0x25230/34` except the consumer chain and midnight cutscene track code (`0x880610`).
* Player state machine (`machine`; `0x869570` request-enter, `0x869c90` request-exit; both enqueue 12-byte requests at `machine+0x260`): **each state has its own active flag**. `IsInState(id)` = `0x86a290` = `[[machine+0x98]+id*8+0x10]` byte `+0x20` (id < 0x67); the flag is set by the base start `0x8bcdc0` (insn `8bcde4`) and cleared by the base stop `0x8bd220` (insn `8bd236`). States are therefore not mutually exclusive: Mimicking (96) can be "on" while locomotion states (0 Running, 1 Sprinting, 6 Sneaking, ...) start and stop under it.
* State ids (decl ctor `mov [this+0x60], id`, read for every `arkDeclPlayerState*` ctor): NpcFatality 38 (`0x6e5fa0`), Choke 41 (`0x89268f`), Mimicking 96 (`0x6e48fb`), CancelMimicking 100 (`0x6d96cb`), UseMimic 102 (`0x6e514b`). State 95 (requested at `0x6e4ebc`, tested at `0x3b5240`/`0x3b5fd0`) has no decl ctor with a literal id: still unnamed.
* The `arkPlayerStateNavigation` decl family (ctor `0x8d2480`; base of Running, Sprinting, Sneaking, Falling, Jumping, Swimming, Flying, Stun, Choke, CureChoke, Cinematic, ...) uses start `0x8d26a0`, which pushes **the decl's own `m_cameraMode` (`decl+0xc8`), blend `decl+0xcc`** (call `8d26ee`). Decl ctor defaults (every `arkDeclPlayerState*` ctor scanned for stores to `+0xc8`): Navigation base `0x8d2556` = **0**, blend `0x8d255c` = -1.0; **Choke `0x8926c8` = 2**, **CureChoke `0x6e24da` = 2**, Cinematic 9, BlackMarketGoto 9, BlackMarketInteract 10, EnterPossessedHost 0x13 (none). Decl files in the game archives may override these (OPEN, not readable from the exe).

### 6.2 Semblance (Q1)

* **The earlier claim "Semblance pushes mode 2 at `0x6e5df0` -> `0x6e5f28`" is wrong (PROVEN).** `0x6e5df0` is slot 11 of the `arkPlayerStateNpcFatality` vtable (`0x1cbffc8`, data ref `0x1cc0020`; state id 38). Its tail `jmp 0x85ab40` at `0x6e5f28` pushes mode **2** (`mov edx,2` at `6e5ef5`, blend `[rip+0x14d37c2]`). It is not a Mimic state.
* Mimic state vtables (PROVEN: slots read, direct call graph followed 4-6 levels; indirect `call [reg+x]` not followed):
  * `arkPlayerStateMimicking` (96, vtable `0x1cbf7c8`): slot 10 `0x6e47e0` = base start `0x8bcdc0` + anim/timeline setup; slot 12 `0x6e4a90` = base stop `0x8bd220` + cleanup; slots 4/5 save/load; **no per-frame body (slots 13/16 = `0x93eb60` no-op)**. It is not in the Navigation family (decl base `0x8bcd10`), so it never reaches `0x8d26a0`. **No `0x85ab40`/`0x85b0f0` reference on any path.**
  * `arkPlayerStateUseMimic` (102, `0x1cbfc08`; decl base `0x8bf1c0`): slot 10 `0x6e4dd0` is a 5-case dispatcher on `[this+0x88]`: requests state 95 (`0x6e4ebc`), sets flags with `0x8bd0d0`, finishes through `0x8bd210`; slot 12 `0x6e54c0` cancels. **No camera-mode push.**
  * `arkPlayerStateCancelMimicking` (100, `0x1cbbc28`): slots `0x8bcdc0`, `0x6d97e0`, `0x8bd220`, `0x6d9cf0`, `0x6d9d90`, `0x6da9b0`, `0x6ec460`: **no camera-mode push.**
  * The base start/stop do touch the camera, but not the mode: decl flag `[decl+0x69] & 0x40` -> `0x856c40(cam,+1)` at `8bce5d` and `(cam,-1)` at `8bd2b6`, which increments/decrements the dword **`cam+0x251ec`** (an input/rotation-lock counter, not a mode; not part of `authored_view()` as written). Whether the Mimic decls set that flag is OPEN (decl data).
* Power side: `arkCpntMimic` (`0x3d9d10` request-enter 96 at `3d9d22`; `0x3d9de0` request-exit 96 at `3d9df6`; update `0x3dab20` requests 96 at `3dac72`; `0x3dadd0` requests CancelMimicking at `3dae11`) and `arkCpntPowerMimic` (`0x3b5000..0x3b6500`) contain **no** `0x85ab40` call. PROVEN by the whole-.text caller scan (none in `0x3b5xxx`, `0x3d9xxx..0x3dbxxx`, `0x6e47e0..0x6e5b00`).
* Release of mode 2: not in the Mimic states. Mode 2 can only be **inherited** from the Choke (41, push `0x8d26ee` from its decl default 2) the cast starts from, and it is replaced the first time a Navigation-family state starts, because each pushes its own decl mode (default 0); **Running (0) is the base state and pushes mode 0 on every start**. INFERRED (not read): that the machine falls back into Running/Sprinting/Sneaking after UseMimic; Idle (22) has no push, so if the fallback were Idle the inherited mode would linger. OPEN: a decl file could set a non-zero `m_cameraMode` on a locomotion decl.
* **Answer: Semblance never requests mode 2.** It does not hold a camera mode for the whole Mimicking state and does not push one for the UseMimic animation. During the face-taking animation the camera sits in whatever mode the preceding Choke left (INFERRED 2); during free movement while disguised it is the locomotion state's mode (INFERRED 0).

### 6.3 Foresight (Q2)

* PROVEN: in the whole TheEye cluster (`0x3b9000..0x3c1500`, plus a depth-6 call-graph walk of every state enter/exit/update: `0x3bb7d0 0x3bf180 0x3bb690 0x3bfbe0 0x3c0fb0 0x3bc680 0x3be0b0 0x3bb750 0x3bca60 0x3bb790 0x3bca50 0x3bc8f0 0x3befd0 0x3bde40 0x3c10e0 0x3bf060 0x3bd8e0 0x3bc3b0 0x3c0d70 0x3c0ea0 0x3bfbb0`) there is **exactly one** `0x85ab40` call, the mode-0 push at exit `0x3be254` (`xor edx,edx`). Nothing pushes a non-zero camera mode or target mode during states 3/4. The power also requests no player state (`0x869570/0x869760/0x869c90` absent; only `IsInState(2)` / `IsInState(4)` at `0x3bb803` / `0x3bb81b` and the machine-layer call `0x86cff0(machine,0xd)` at `0x3bf371`). The other machine calls (`0x86d140` at `0x3bf360`, `0x3be1c2`) toggle sensor/physics, not the camera.
* Consequence (INFERRED, high): during the spirit the camera mode is whatever the active locomotion (Navigation-family) state pushed last, decl default 0, as in normal play; the exit push of 0 is cleanup. Foresight needs no `authored_view()` exception. Residual risk: a decl-file override of a locomotion `m_cameraMode` (OPEN).

### 6.4 Ordinary choke / takedown (Q3)

Mode 2 is a generic "animation-driven" first-person camera mode, not choke-specific (PROVEN pushes; meaning INFERRED):

| State (id) | push | where |
|---|---|---|
| Choke (41), CureChoke (43) | decl `m_cameraMode`, ctor default **2** | start `0x8d26a0` (slots 10/11 `0x892a80`, `0x892ac0`), push `8d26ee`; defaults `0x8926c8`, `0x6e24da` |
| NpcFatality (38) | explicit **2** | slot 11 `0x6e5df0`, tail jmp `0x6e5f28` |
| Choke kill sub-phase / Assassination (13) | explicit **3** (+ vector `decl+0x90` to `cam+0x251f0`) | `0x8b1810` call `8b184c`; called from Choke's update (`0x8938a3`) and Assassination slot 13 (`0x8cd4a1`) |
| Versus (31) / VersusGrab (32) | 2 / 3, exit pushes 0 | `0x6f1930`, `0x6f2e91` / `0x6f1e96`, `0x6f3422` |
| Mantle (8), CharacterChoice (37) | 2 | `0x8c8ba9`, `0x6da4e6` |
| Peeking (24) 3, Pathing 3, ShadowWalk 7, CatFall (4) 0xe, EnterPossessedHost 0xc | fixed | `0x8ab6a3`, `0x8cd616`, `0x8b2087`, `0x8c4ae1`, `0x89c555` |
| Locomotion (Running 0, Sprinting, Sneaking, Falling, Jumping, Swimming, Slipping, AutoCrouch, WallCrouch, InAir, Flying) | decl default **0** via `0x8d26a0` | every start re-asserts mode 0 |

Mode 2 is released only implicitly: Choke's stop (`0x8d2d40` -> base `0x8bd220`) does not push; the next Navigation-family start pushes 0. Explicit exit pushes of 0 exist in Versus, VersusGrab, CarryCorpse, Grapple (`0x8a0e05`).

### 6.5 Consequence for the VR mod

* **No Semblance-specific or Foresight-specific `authored_view()` exception is justified by the code.** The Mimic states push nothing and Mimicking (96) is a passive overlay (own active flag, no per-frame body), so free-roam disguise runs in the normal locomotion states under camera mode 0 and `authored_view()` is false; Foresight is the same.
* What remains authored: the face-taking animation (UseMimic 102 plus the Choke it starts from, inherited mode 2) and the `target_mode`/mode blend tail after any push (target mode changes at once, mode only after the blend).
* Fallback only if a live read shows a persistent mode 2 while Mimicking and walking (decl override, or Idle fallback). Exception condition, mirroring `tracked_shadow_walk()`: `mimicking && !use_mimic && !cancel_mimicking && mode==2 && target_mode==2 && !blend_body_yaw && !code_lock`, with `mimicking = active(96)`, `use_mimic = active(102)`, `cancel_mimicking = active(100)` and `active(id)` = byte `+0x20` of `[[machine+0x98]+id*8+0x10]` (null-checked, id < 0x67).

### 6.6 Remaining OPEN (one read-only live check closes all)

Read `cam+0x25230/+0x25234` and the active bytes of states 96/102 (a) during UseMimic, (b) while Mimicking and walking, (c) during Foresight states 3/4 while moving. Expected: (a) 2 (inherited), (b) 0, (c) 0. Also unnamed: state 95; decl-file values of `m_cameraMode`.
