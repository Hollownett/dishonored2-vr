# Combat port review: ranged fire, slash rate, choke offer, receipts, grenade, equip, hand body, forearm Voltaic Gun

Static review of 2026-11-22 (D2 `Dishonored2.exe` vs DOTO `Dishonored_DO.exe`, generated tree `build/doto-src/src`). Nothing in `src/`, `offsets.json` or the game was changed.
Scratch scripts: `scratchpad/combat/*.py` (pefile+capstone, `tools/doto/translate.py` maps). Rule format: "source": "review/combat-port"; `text` rules match `contains`
in the D2 source line and replace `old` in the generated line; `lines` rules map every listed offset literal on a line containing `contains`.

## 0. Findings that change the plan

1. **Billie has no crossbow.** Her player weapons are the **Voltaic Gun** (`components/characters/player/billie/weapons/gaussgun.cpntweapongaussgun`, entity def
   `models/weapons/dlc01/player/billie/gaussgun01/ply_billie_gaussgun01.def`, class `arkCpntWeaponGaussGun`, comp vtable 0x1c45768, decl vtable 0x1c45618), the
   assassin sword (`assassinsword01/ply_billie_assassinsword01.def`) and a dagger/knife. `arkCpntWeaponCrossbow` (0x1cad088) exists only for NPC wristbows
   (`wristbow_howler`), `arkCpntWeaponGun` (0x1c63c58) for NPC guns. **What the user calls "the crossbow" on the forearm is the Voltaic Gun.** Everything in D2's
   "crossbow" slot maps to the Gauss component; the D2 "pistol" kind has no player weapon in DOTO.
2. **The Voltaic Gun fires through exactly the D2 three-call sequence.** Gauss fire fn `0x3c1d70` calls `0x6906e0` create (at 0x3c1dc6), `0x6904b0` sound (0x3c1e11),
   `0x68e970` launch (0x3c219d) and vcall `[vt+0x158]` (spend). So NativeRangedFire's create/launch/sound/spend works for it with the offset fixes below.
3. **DOTO has no `models/campaign/dunwall/midnightassets/*_midnight.def`** (grep of every `game*_patch.index`: 0 hits for `midnightassets` and `sword_player_midnight`).
   `native_weapon_hands.hpp:149,955-957,1055-1057` (`sword_copy_decl`, `mirror_def_names`) name D2-only defs, so every weapon copy spawn (`spawn_def`, line 1224) fails in DOTO
   (the weapon "stays native": counted `mirror_failed`). Replacement defs that do exist: `models/weapons/dlc01/player/billie/assassinsword01/ply_billie_assassinsword01.def`,
   `.../gaussgun01/ply_billie_gaussgun01.def`. Needs live checking that they spawn as an inert prop (they carry AnimEvolved + MidnightProxy + AIAttentionSource components).
4. Billie's left arm model overrides the weapon bind tag: `player_leftarm_billie.md6` -> `prop "...ply_billie_gaussgun01.md6" { tag "weapon_attach_tag" { trans(0 0 0) rot(0 0 0 1) parent "left_lower_armhelper2_jnt" } }`.
   The right arm has an empty props block (inherits Emily's `weapon_attach_tag` on `right_hand_attachment_jnt`). See section 8.

## 1. Weapon component layout (D2 -> DOTO), proven by aligned instructions

Fn pairs (fn_diff, `how` exact/shape): spend `0x639360->0x68e400`, reload `0x63a2b0->0x68f2c0`, sound `0x63b3c0->0x6904b0`, create `0x63b5f0->0x6906e0`,
ammo switch `0x63a490->0x68f4a0`, modifier rebuild `0x638190->0x68cf80`, upgrades `0x61b400->0x6710d0` (identical bar a global), launch `0x639970->0x68e970`.

| field (weapon component) | D2 | DOTO | evidence |
|---|---|---|---|
| attributes / bag / camera / upgrades list (+count 0x58) / decl / owner | 0x30 / 0x40 / 0x48 / 0x50 / 8 / 0x10 | same | create, upgrades fn |
| inventory | 0x68 | **0x70** | spend `0x68e437 mov rcx,[rbx+0x70]`, reload `0x68f2e7` |
| current ammo entry (item at +0, projectile def at +0x10) | 0x98 | **0xa0** | `0x68e900: mov rax,[rcx+0xa0]; mov rax,[rax+0x10]` (new vslot 0x170 getter) |
| loaded magazine | 0xa0 | **0xa8** | spend, CanFire `0x68d070 cmp [rcx+0xa8],0; setg` |
| magazine capacity | 0xa4 | **0xac** | reload `0x68f2fc`, handler 248 `0x49cf20 cmp [rcx+0xac],4` |
| NEW byte "configure projectile" | - | **0xb0** | set to 1 by ranged base init `0x68c324`; `0x6906e0` and `0x6904b0` skip their config when 0; **Gauss sets `[comp+0xb0]=[comp+0xe0]` before every create (`0x3c1dc0`)** |
| modifier list ptr / count | 0xa8 / 0xb0 | **0xb8 / 0xc0** | create `0x69078f`, rebuild `0x68cf8e and [rcx+0xc0],0xff000000` |
| flags byte (bit 4, 0x20, 0x40, 0x80) | 0xb8 | **0xc8** | create `0x6909ea test [rsi+0xc8],4` |
| byte "suppress" | 0xd0 | 0xe0 | create `0x6909f8` |
| decl ammo-type array / count (stride 0x70) | decl+0x188 / 0x190 | decl+**0x1e8 / 0x1f0** | `0x68f4b3 mov ecx,[rbx+0x1f0]`, `0x68f4c1 mov rbx,[rbx+0x1e8]` |
| upgrade decl condition name | decl+0x250 | same | `0x6710d0` identical |
| vslot ConsumeAmmo / Reload | 0x140 / 0x148 | **0x158 / 0x160** | `0x67fcdb`..; Gauss slot 43 `0x3c1bd0` (jmp 0x68e400); slots 44,45 identical to Ranged |
| Gauss ammo-type attribute index | - | comp+0x178 | `0x3c1e7f movsxd rax,[rdi+0x178]` (`[weapon+0x30]+0x48+idx*0x28+0x24`) |
| Crossbow speed / flag attr index | 0x158 / 0x15c | 0x178 / 0x17c | `0x67ffca`, `0x68003a` (NPC only; Gauss has no speed attr, passes constant 1.0 in xmm8) |
| Crossbow bolt entity ptr | 0x160 | **0x180** (NPC only) | `0x67f87e mov [rbx+0x180],0`; D2 `0x62b193` |
| projectile info component: mark | `[c+0xa5] \|= 2` | `[c+0x18d] \|= 4` | `0x680070 or byte [rax+0x18d],4` vs D2 `0x62b907 or byte [rax+0xa5],2`; launch `0x68ea8a or [rbx+0x18c],1` vs `0x639a8a [rbx+0xa4]` |
| entity id / owner id | +0xa8 / +0xac | +0xb0 / +0xb4 | already rewritten |
| inventory: items +0x38, count +0x40, stride 0x38, record +0x30/+0x2c counters | | identical | `0x614f30->0x66aa10` (hash exact), `0x615680->0x66b1e0` |

The generator rewrote the magazine (0xa0->0xa8), flags (0xb8->0xc8) and vslots (0x140->0x158, 0x148->0x160) correctly (weapons.json called them unverified; they are now proven).
It **missed** the inventory 0x68, ammo 0x98, capacity 0xa4, list 0xa8, attribute 0x158/0x15c, decl 0x188/0x190, `*c+0x328` (player state machine inventory -> 0x380), and the `+0xa5` mark.

## 2. native_ranged_fire.hpp (audit row 10)

Class: layout + one real change (new +0xb0 gate and the ammo getter vcall); ABI of every called function is unchanged
(create `rcx=comp -> rax=projectile entity`; sound `rcx=comp, edx=0`; launch `rcx comp, rdx proj, r8 origin, r9 axis3x3, xmm... [rsp+0x20] float, [rsp+0x28/0x30] qword, [rsp+0x38/0x40] int` same as D2).

`verify()` fails today only because two pins hold D2 prologues: `0x6906e0` (DOTO `48 89 4c 24 08 53 56 57 41 55 48 81 ec a8 00 00`: extra `push rdi`, `sub rsp,0xa8`, then
`call [rdx+0x170]` = ammo getter) and `0x6904b0` (DOTO `40 53 57 41 56 48 81 ec a0 00 00 00 48 8b 81 a0`). All other 7 pins (and the 0x68e970 launch pin) already match DOTO.

### Rules (text, generated line replaced)

```json
[
 {"file":"src/native_ranged_fire.hpp","contains":"{0x63b5f0,{0x48,0x89,0x4c,0x24,0x08,0x53,0x56,0x41,0x55,0x48,0x81,0xec,0xb0,0x00,0x00,0x00}}",
  "old":"0x48,0x89,0x4c,0x24,0x08,0x53,0x56,0x41,0x55,0x48,0x81,0xec,0xb0,0x00,0x00,0x00","new":"0x48,0x89,0x4c,0x24,0x08,0x53,0x56,0x57,0x41,0x55,0x48,0x81,0xec,0xa8,0x00,0x00",
  "why":"create 0x63b5f0->0x6906e0: push rdi added, sub rsp,0xa8 (bytes 48894c240853565741554881eca80000)","source":"review/combat-port"},
 {"file":"src/native_ranged_fire.hpp","contains":"{0x63b3c0,{0x40,0x53,0x57,0x41,0x56,0x48,0x81,0xec,0xa0,0x00,0x00,0x00,0x48,0x8b,0x81,0x98}}",
  "old":"0x48,0x8b,0x81,0x98}","new":"0x48,0x8b,0x81,0xa0}",
  "why":"sound 0x6904b0: mov rax,[rcx+0xa0] (ammo entry)","source":"review/combat-port"},
 {"file":"src/native_ranged_fire.hpp","contains":"inline static void real_mark","old":"+0xa5)|=2","new":"+0x18d)|=4",
  "why":"projectile info component mark flag","source":"review/combat-port"},
 {"file":"src/native_ranged_fire.hpp","contains":"projectile_component+0xa5)|=2","old":"+0xa5)|=2","new":"+0x18d)|=4","why":"real_mark body (the line above may not match; keep the one that does)","source":"review/combat-port"}
]
```
(The `real_mark` line is `static bool real_mark(...) {__try {*reinterpret_cast<volatile unsigned char*>(projectile_component+0xa5)|=2;...` so only the second rule matches; drop the first.)

Offsets (`lines`, one per class of literal; line rules win over `sites`):
```json
[
 {"file":"src/native_ranged_fire.hpp","contains":"l.comp+0x68","map":{"0x68":"0x70"},"why":"inventory 0x68e437","source":"review/combat-port"},
 {"file":"src/native_ranged_fire.hpp","contains":"+0x98)","map":{"0x98":"0xa0"},"why":"ammo entry (lines 331,529,689,743,785,842)","source":"review/combat-port"},
 {"file":"src/native_ranged_fire.hpp","contains":"+0xa4)","map":{"0xa4":"0xac"},"why":"capacity 0x68f2fc (lines 395,535,847)","source":"review/combat-port"},
 {"file":"src/native_ranged_fire.hpp","contains":"l.comp+0xa8,64","map":{"0xa8":"0xb8"},"why":"modifier list 0x6907b1","source":"review/combat-port"},
 {"file":"src/native_ranged_fire.hpp","contains":"+0x188","map":{"0x188":"0x1e8","0x190":"0x1f0"},"why":"decl ammo array (lines 349,676,734,761)","source":"review/combat-port"},
 {"file":"src/native_ranged_fire.hpp","contains":"attribute(0x158)","map":{"0x158":"0x178"},"why":"crossbow speed idx; for Gauss see src change","source":"review/combat-port"},
 {"file":"src/native_ranged_fire.hpp","contains":"attribute(0x15c)","map":{"0x15c":"0x17c"},"why":"crossbow flag idx","source":"review/combat-port"},
 {"file":"src/native_ranged_fire.hpp","contains":"*c+0x328","map":{"0x328":"0x380"},"why":"player state machine inventory (structs rule only knows bases machine/machine_)","source":"review/combat-port"}
]
```
(`rig::value<...>(read,cc+0xa4)` lines also carry `cc+0xa0`, already mapped to 0xa8 by the existing site rule; the `+0xa4)` line rule maps only 0xa4.)
Also `native_weapon_hands.hpp` lines 1923, 2106, 2777 (`comp+0x98`, `comp+0xa0`, `*comp+0x98`), 2704-2707 (diagnostic) need the same map; crossbow `comp+0x160` at 1915, 2107, 2405, 2728 -> `0x180` (2516 was already rewritten; the others were missed) but these are NPC-crossbow only: for Billie the bolt does not exist.

Event hooks (`event_hooks` table, lines 251-255) and gate: DOTO events are D2+6; observed DOTO handlers: gun 206/207/208/209/210/211/248 = 0x49cfa0/0x49cfb0/0x49cf40/0x49cfe0/0x49d020/0x49d030/0x49cf20,
crossbow 206->0x680740, 207->0x49f020 (`jmp [vt+0x160]`, shared), **Gauss 206 -> thunk `0x3c2d20` (`e9 4b f0 ff ff cc cc cc`, `jmp 0x3c1d70`), registered by `lea rax,[rip+0x647]` at 0x3c26d2 (r8d=0xce)**, 207 -> 0x49f020.
Bytes verified against DOTO. Generated table errors: gun 201 pin `...0x58,0x01` must be `0x78,0x01` (0x49cfb0 = `48 8b c1 48 8b 89 78 01`); crossbow 201 is still D2 `0x62bfe0` and must be `{0x49f020,1,{0x48,0x8b,0x01,0x48,0xff,0xa0,0x60,0x01}}`.
```json
[
 {"file":"src/native_ranged_fire.hpp","contains":"{0x44f440,1,{0x48,0x8b,0xc1","old":"0x58,0x01}}","new":"0x78,0x01}}","why":"0x49cfb0: mov rcx,[rcx+0x178]","source":"review/combat-port"},
 {"file":"src/native_ranged_fire.hpp","contains":"{0x62bfe0,1,{0x48,0x8b,0x01,0x48,0xff,0xa0,0x48,0x01}}","old":"{0x62bfe0,1,{0x48,0x8b,0x01,0x48,0xff,0xa0,0x48,0x01}}","new":"{0x49f020,1,{0x48,0x8b,0x01,0x48,0xff,0xa0,0x60,0x01}}","why":"201 -> shared jmp [vt+0x160] stub","source":"review/combat-port"}
]
```

### Src change (behind a trait) for the Voltaic Gun as the hand-fired weapon
Add to `game_profile.hpp` (DOTO: true, D2: false) `inline constexpr bool ranged_player_weapon_is_voltaic`. Then in `native_ranged_fire.hpp`:
- `kind_info(Shot::crossbow)` (line ~464, generated `{0x1cad088,0x1cacf38}`) -> `{0x1c45768,0x1c45618}` (Gauss comp/decl vtables); `kind_info(pistol)` -> `{0,0}` so `locate()` returns `no_weapon` and the pistol path skips cleanly (`typed(...,0)` must be refused: guard `if(!info.comp_vtable) {why=Why::no_weapon;return {};}`). `dxgi_probe.cpp:16886` `kind()` already yields 1 for a crossbow copy; the Voltaic copy is E::crossbow in `arm_set` (copy def replaced per finding 3).
- `execute()`: before `n.create` write `*(uint8_t*)(comp+0xb0)=*(uint8_t*)(comp+0xe0)` for Gauss (save/restore; D2 path unchanged: base init leaves 1 for Gun/Crossbow); skip `spread()` and `n.flash` (pistol-only, 0x49cb10 is the Gun's muzzle flash, Gauss has none); `l.speed=1` (no speed attribute): replace `attribute(0x158)` by "none" for Gauss; `l.mark` from `attribute(0x178)` (`> 0`), `real_mark` sets `+0x18d |= 4`; also copy the Gauss fire's `proj_info[0x18d] bit1 = (comp[0xc9]>>5)&1` (0x3c1e30) if overcharge visuals matter.
- `gate_copy_fire()`: replace the two-entry loop with `{thunk 0x3c2d20, target 0x3c1d70, reg 0x3c26d2}` + the crossbow row only if NPC copies are wanted; prologue pin of `0x3c1d70` = `40 55 57 48 8d ac 24 68 ff ff ff 48`. `i<2` hooks required -> `i<1` (gate) for DOTO.
- The Gauss gun is a **charged** weapon (`gaussgun_charge/overcharge/fire_overcharged` scenes). A hand-fired copy fires the uncharged shot only; charge release needs the native event path (event 206 with the charge flag): out of scope for the first iteration.
- Billie's native Voltaic fire is already hand-aimed by `NativeShotAim` (`shot_aim_calls {0x3c219d,0x68e970}`); NativeRangedFire only adds copies (a second hand). With one wrist gun the single native gun needs no NativeRangedFire; it matters only for dual copies or a hand-switch implemented as a copy.

Risk: medium-high (new +0xb0 gate, magazine/ammo fields proven but the DOTO inventory-record fields `+0x10 comp, +0x18 equipment` were not independently re-proven: `Find/Count/Remove` are hash-identical so +0x30/+0x2c counters and stride hold).
Verify: add the rules, run `python tools/doto/port.py`, expect `status.fire.verified` true (`refused[unverified]==0`); sim: `python tools/doto/doto_sim.py --name fire1 --dll build/doto/<dll> --scenario hands_fingers` then a scripted trigger (new scenario `voltaic_fire.py` modelled on `tools/linux/scenarios/hands_fingers.py`: grip+trigger on the gun hand); read `fire.fired/created/launched/pool_after` and `hooks[0]==0`. Headset needed for aiming comfort.

## 3. native_slash_scene_rate.hpp (row 9): revivable, layout + one prologue + trampoline size

D2 `0x86cbf0` (perform) -> DOTO `0x8d7ed0`. Class: layout + moderate real change (new `[rsi+0xda]/[rsi+0xdb]` branches that only gate whether the scene is replayed; the two scene-play wrappers and the rate setter have exact twins).
Twin map (translate.py, all hash/shape exact): `0x86c6a0->0x8d7930`, `0x854ab0->0x8bdb70`, `0x854be0->0x8bdca0`, `0x8549c0->0x8bda50` (+ a tail `call 0x83f2e0` that only clears a scene flag, ABI same, returns the same output pointer), `0x7d91c0->0x83f590`, setter `0x7d8fe0->0x83f3b0`.
State layout (slash state id 39 in both: decl ctor `mov [rbx+0x60],0x27` at D2 0x86a7dc / DOTO 0x8d51b0): primary scene id `+0x100 -> +0x110`, secondary id `+0xc8 -> +0xd0`, attribute index `+0x108 -> +0x118`, bool flags `+0xcc/+0xcd -> +0xd4/+0xd5`, machine pool `+0x90 -> +0x98`, machine attributes `+0x330 -> +0x388` (generated ok), scene manager `game+0x1f5338 -> +0x1f5a68` (generated ok).
Perform tail (DOTO `0x8d8364..0x8d83ce`): `movsxd rax,[rsi+0x118]; attrs=[machine+0x388]; rate=[[attrs+0x48]+idx*0x28+0x24]; setter(game+0x1f5a68,ebx,rate); if [rsi+0xd0] setter(...,[rsi+0xd0],rate)` same as D2.
The call from the secondary slot (`0x8d78b0`, vtable+0xc8) writes the id with `lea rcx,[rbx+0xd0]` (D2 `0x86c7da lea rcx,[rbx+0xc8]`).

Problems in the generated file: (a) `prologues[0]` still D2 bytes (DOTO: `48 8b c4 56 48 81 ec 90 00 00 00 80 b9 d4 00 00 00 00 48 8b f1 0f 85 16 05 00 00 80 b9 d5 00 00 00 00 0f 85 09 05 00 00 48 83 b9 a8 00 00 00 00`);
(b) three caller pins poisoned `0xfff00030/40/50`; (c) `{0x8d790e,{0xe9,0x66,5,0,0}}` must be `e9 bd 05 00 00` (jmp to 0x8d7ed0); (d) literals `state+0x100`, `apply(...,0x100)`, `apply(...,0xc8)`, `state+0x108` untouched;
(e) **the prologue is 11 bytes to steal, not 8**: `mov rax,rsp; push rsi; sub rsp,0x90` = 3+1+7. MinHook's trampoline copies 11 bytes, so `perform_trampoline_size=22`, `expected_perform_tail` (`base+rvas[0]+8`), `owned_perform_tail`, `restore_perform_protection(+8,14)`, the 8-byte `memcmp(...,prologues[0],8)` and the unwind blob `{1,8,2,0,8,0xb2,4,0x60}` must change.
```json
{
 "text":[
  {"file":"src/native_slash_scene_rate.hpp","contains":"{0x48,0x8b,0xc4,0x56,0x48,0x83,0xec,0x60,0x80,0xb9,0xcc,0,0,0,0,0x48,0x8b,0xf1","old":"0x48,0x8b,0xc4,0x56,0x48,0x83,0xec,0x60,0x80,0xb9,0xcc,0x00,0x00,0x00,0x00,0x48,0x8b,0xf1,0x0f,0x85,0xfa,0x02,0x00,0x00,0x80,0xb9,0xcd,0x00,0x00,0x00,0x00,0x0f,0x85,0xed,0x02,0x00,0x00,0x48,0x8b,0x89,0xa0,0x00,0x00,0x00,0x48,0x85,0xc9,0x0f",
   "new":"0x48,0x8b,0xc4,0x56,0x48,0x81,0xec,0x90,0x00,0x00,0x00,0x80,0xb9,0xd4,0x00,0x00,0x00,0x00,0x48,0x8b,0xf1,0x0f,0x85,0x16,0x05,0x00,0x00,0x80,0xb9,0xd5,0x00,0x00,0x00,0x00,0x0f,0x85,0x09,0x05,0x00,0x00,0x48,0x83,0xb9,0xa8,0x00,0x00,0x00,0x00","why":"perform prologue at 0x8d7ed0 (48 B). Adjust 'old' to whatever the generator wrote for that array.","source":"review/combat-port"},
  {"file":"src/native_slash_scene_rate.hpp","contains":"{0x86cd7d,{0xe8,0x5e,0x7e,0xfe,0xff}}","old":"{0xfff00030,{0xe8,0x5e,0x7e,0xfe,0xff}}","new":"{0x8d81b0,{0xe8,0xeb,0x5a,0xfe,0xff}}","why":"call 0x8bdca0 (twin of 0x854be0, r9d=7)","source":"review/combat-port"},
  {"file":"src/native_slash_scene_rate.hpp","contains":"{0x86cd7d,{0xe8,0x5e,0x7e,0xfe,0xff}}","old":"{0xfff00040,{0xe8,0xf2,0x7c,0xfe,0xff}}","new":"{0x8d815c,{0xe8,0x0f,0x5a,0xfe,0xff}}","why":"call 0x8bdb70 (twin of 0x854ab0, r9d=0x27)","source":"review/combat-port"},
  {"file":"src/native_slash_scene_rate.hpp","contains":"{0x86c7d5,{0xe8,0xe6,0xc9,0xf6,0xff}},{0x86cdf9","old":"{0xfff00050,{0xe8,0xa2,0xf8,0xff,0xff}}","new":"{0x8d81e1,{0xe8,0x4a,0xf7,0xff,0xff}}","why":"call 0x8d7930 (twin of 0x86c6a0)","source":"review/combat-port"},
  {"file":"src/native_slash_scene_rate.hpp","contains":"{0x86bf1f,{0xe8,0xcc,0x0c,0,0}},{0x86c685,{0xe9,0x66,5,0,0}}","old":"{0x8d790e,{0xe9,0x66,5,0,0}}","new":"{0x8d790e,{0xe9,0xbd,5,0,0}}","why":"jmp rel32 to 0x8d7ed0","source":"review/combat-port"},
  {"file":"src/native_slash_scene_rate.hpp","contains":"rig::value<std::uint32_t>(reader,state+0x100)!=frame.primary","old":"state+0x100)","new":"state+0x110)","why":"primary id","source":"review/combat-port"},
  {"file":"src/native_slash_scene_rate.hpp","contains":"apply(frame,frame.primary,0x100)","old":"0x100)","new":"0x110)","why":"primary id","source":"review/combat-port"},
  {"file":"src/native_slash_scene_rate.hpp","contains":"apply(frame,frame.secondary,0xc8)","old":"0xc8)","new":"0xd0)","why":"secondary id (0x8d83ae cmp [rsi+0xd0],0)","source":"review/combat-port"},
  {"file":"src/native_slash_scene_rate.hpp","contains":"rig::value<std::int32_t>(reader,state+0x108)","old":"state+0x108","new":"state+0x118","why":"attribute index (0x8d8364)","source":"review/combat-port"}
 ]
}
```
Src change (the trampoline): in `native_slash_scene_rate.hpp` introduce `perform_steal` = `game_traits::slash_perform_steal` (D2 8, DOTO 11) and derive `perform_trampoline_size=perform_steal+14`, tails at `+perform_steal`, `memcmp(...,perform_steal)`, and the unwind blob `D2 {1,8,2,0,8,0xb2,4,0x60}` / **DOTO `{1,11,3,0, 11,0x01, 0x12,0x00, 4,0x60}`** (ALLOC_LARGE 0x90 = 0x12*8 at offset 11, PUSH rsi at offset 4; `PerformMetadata::unwind` array 8 -> 10; `RUNTIME_FUNCTION` `EndAddress` = size).
Candidate frames: `primary_body` return-address checks 0x8bdc3d / 0x8bdd68 and `secondary_body` 0x8d7a6a are already translated (verified: calls at 0x8bdc38 / 0x8bdd63 -> 0x8bda50, 0x8d7a65 -> 0x83f590).
Risk: medium (hooked entry with an 11-byte steal + unwind registration; a wrong unwind blob crashes only on an exception through the trampoline, which the SEH `__try` frames make likely when anything faults).
Verify: `slash_speed` knob on; status `accelerations > 0`, `setter_failures==0`, `installed()` true, `perform_unwind_registered`; sim `sword_sweep` (phase 2 at 3.5 m/s presses attack): `python tools/doto/doto_sim.py --name slash1 --dll ... --scenario sword_sweep`. Headset for feel.

## 4. native_offered_choke.hpp (row 8): both entries revivable by hash

idx0 `0x391280 -> 0x3c7dd0` (366 B): 93 vs 93 insns, only `[rdi+0x338]->0x390`, `0x3130b0->0x327770`, `[rax+0xcc]->0xdc`: new FNV `0xd272ee322e4c7501` (already in pin-audit C).
idx1 `0x391550 -> 0x3c86f0`: **same ABI and flow** (`rcx hud, edx action, r8b normal, r9b alternate`; builds a record in locals at `[rsp+0x30..0x6c]` with `normal=[rsp+0x68]`, `alternate=[rsp+0x69]`, flags `[rsp+0x6a]`, id `0x3fff0010` at 0x6c, then `call 0x3c6120` = D2 `0x38f5b0`); the 63->61 insn change is register allocation (`ebx/ebp/r14` swapped) and the string-id table (`action 16 -> id 0x14/0x16` mapping), not behaviour. Prologue 32 B identical, so `prologues[1]` and `unwinds[1]` (`1,5,1,0,5,0xe0`) stay. Size 252 -> **271 B**, FNV **0x1a668a7f6ff86989** (my FNV reproduces the D2 value 0xde5a0e9ec71e378f for idx0 as a check).
Caller `0x3913e3 -> 0x3c7f33` is the return address after `call 0x3c86f0` at 0x3c7f2e (`lea edx,[r9+9]` = action 9, `r8b=1` or 0): verified.
```json
[
 {"file":"src/native_offered_choke.hpp","contains":"inline static constexpr std::array<unsigned,2> sizes{366,252}","old":"{366,252}","new":"{366,271}","why":"hint fn 0x3c86f0 is 271 B","source":"review/combat-port"},
 {"file":"src/native_offered_choke.hpp","contains":"0xde5a0e9ec71e378full,0x6fffc255cf8e88b5ull","old":"0xde5a0e9ec71e378full,0x6fffc255cf8e88b5ull","new":"0xd272ee322e4c7501ull,0x1a668a7f6ff86989ull","why":"FNV-1a of the DOTO bytes","source":"review/combat-port"}
]
```
(If pin-audit C's idx0 text rule is applied first the `old` above no longer matches; use `old":"0x6fffc255cf8e88b5ull","new":"0x1a668a7f6ff86989ull"` then.) Remaining unverified: sensor field reads (`sensor+0x10/0x20/0x30/0x38/0x50`, container `+0xd8`, hud `+0x40/0x48/0x60/0x68/0x80`, choke state `+0x20/+0xb8`: the choke state ctor `0x82a5a0 -> 0x8926e0` keeps `+0xb0/+0xb8` identical) - do a live `rtti`/`fields` read of the hud once. State id 0x29 = choke is the same (DOTO `0x89268f`).
Risk low-medium. Verify: status `offered_choke` `published>0`, `rejected[record]==0` in `tools/linux/scenarios/guard_probe.py` / `corpse_*` scenarios (choke offer needs an NPC).

## 5. native_assassination_receipt_hooks.hpp (row 14, diagnostic, off by default)

Prologues: idx0,1,2,4,5 equal D2's first 32 B; idx3 (`0x832d20 -> 0x89b2d0`) differs in the `call rel32` bytes only. pdata sizes: idx1 83 (FNV `0x4498e0c131255f0f`), idx3 261 (`0x56f341dff2a69bcd`), idx5 86 (hash unchanged `0xf44a324d119e367a`).
idx2 (`0x860eb0 -> 0x8caa20`, D2 2100 B) and idx4 (`0x86b050 -> 0x8d5970`, D2 2210 -> **2261 B**, FNV `0x5a1bc3b50bdcfc2b`) have real code changes (the 78 differing aligned insns in idx4 show new branches); their `unwinds` (`prefixes` 6/5) were not re-derived. Recommendation: leave the receipt observer off in DOTO (it is only a jsonl diagnostic when a folder is passed); apply pin-audit B/C for idx0,1,3 only if someone wants it. idx2's DOTO extent was not measured (pdata gives only the cold chunk, 108 B).

## 6. native_grenade_throw.hpp, native_manual_equip*.hpp, hand_body_native.hpp: pins already correct

The `text` rules that print "matched nothing" are **redundant, not broken**: port.py's own byte-group pass had already produced the DOTO bytes before the text rule ran, so `old` is gone. Checked the generated pins against the DOTO exe:
- `native_grenade_throw.hpp:53` want[8] `8b 02 89 81 4c 03 00 00` = DOTO `0x8e9c20` (`mov [rcx+0x34c],eax`). Call site `0x691668 -> 0x8e9c20`, return `0x69166d`: OK. **Real difference (not covered by any pin): the velocity and launch-point locals.** D2 Throw passes `rdx=[rsp+0x50]` (velocity) and launches from `[rsp+0x70]` = velocity + 0x20 B (`write()` uses `velocity[8+i]`). DOTO passes `rdx=[rsp+0x78]` (x at +0x78, y +0x7c, z `[rbp-0x80]`) and launches (`0x68e970` at 0x691751) from `r8=&[rsp+0x68]` = velocity - 0x10 B. So `velocity[8+i]` would corrupt a local. Src change: `game_traits::grenade_spawn_float_delta` (D2 +8, DOTO -4) in `write()`; first confirm in a disassembly with the real function start that `rbp-0x80 == rsp+0x80` (the fn start was not recoverable from pdata chunks). Risk medium (stack write). Do not enable the throw rewrite in DOTO until this is checked; the Hyperbaric Grenade is thrown by `arkCpntWeaponThrowable` (`fire_scene_rva 0x690ff0`).
- `native_manual_equip.hpp:48` and `native_manual_equip_hooks.hpp:33`: DOTO wheel fn `0x861b20` starts `48 8b c4 56 41 56 48 83 ec 68 80 b9 79 01 00 00 00 4d 8b f0 48 8b f1 0f 84 23 01 00 00 48 89 58`: equals the generated arrays. Call pins `0x546761 -> 0x861b20`, `0x861c06 -> 0x869760`, `0x89f87d`/`0x89dccc -> 0x89ea20`, slots `0x1cff558+0x58/0x68/0x70/0x90 -> 0x89dca0/0x89f850/0x89e2d0/0x89f550`, processor prologue at `0x89ea20`: all verified in DOTO.
- `hand_body_native.hpp:57,58`: `{0x1903c0,{0f 57 c0 33 d2 48 8d 05 ac 5b a7 01 48 89 01 48}}` and `{0x9161e0,{80 b9 e1 b7 34 00 00 75 1c 80 b9 e2 b7 34 00 00}}` both equal the DOTO bytes; line 63 `userdata_rva=0x1903c0` is rewritten. The two rules can be deleted from offsets.json (or kept; they only warn).

## 7. Summary of what can be revived

| feature | state | how |
|---|---|---|
| hand-fired Voltaic copy (NativeRangedFire) | revivable | sections 1-2 (2 pins, 8 line rules, 2 hook rules, trait for kind_info/gate/+0xb0) |
| slash-speed | revivable | section 3 (prologue, 3 pins, 1 jmp, 4 offsets, 11-byte trampoline/unwind) |
| choke offer observer | revivable | section 4 (size + 2 hashes) |
| receipt observer idx2/4 | leave off | real changes, diagnostic only |
| grenade throw | pins fine, needs stack-slot trait | section 6 |
| manual equip, hand body | already fine | section 6 |

## 8. Forearm crossbow (user requirements) -- the Voltaic Gun on Billie's left forearm

User feedback (DOTO headset): the gun "kind of moves with my hand" instead of being rigid on the forearm; it must follow the other forearm when the hand choice switches; its hand must keep the grip for grabs and only fire on grip+trigger.

### 8.1 What DOTO does and why it floats

- The Voltaic Gun is a **separate weapon entity** (`arkWeapon`, 0x1d30f78, md6 `ply_billie_gaussgun01.md6`), NOT part of the arm mesh: Billie's arm meshes have only the glove and upper surfaces (3255+568 / 3281+395 vertices).
- It hangs on the arm model through an `arkCpntReferenceFrame` bind component. `player_leftarm_billie.md6` declares `prop "...ply_billie_gaussgun01.md6" { tag "weapon_attach_tag" { trans (0 0 0) rot (0 0 0 1) parent "left_lower_armhelper2_jnt" } }`; `gaussgun.cpntweapongaussgun` has `m_equippedBindTag="weapon_attach_tag"`, `m_unequippedBindTag="gun_attach_tag"`. **So the engine binds the gun to the forearm twist helper 2, not to joint 59 (hand attachment).** The right arm (`player_rightarm_billie.md6`, props empty; void variant only `_info`) has no gaussgun override: a gun equipped on the right arm would use Emily's default `weapon_attach_tag` = `right_hand_attachment_jnt`. D2 defaults put the pistol on `left_hand_attachment_jnt` (joint 59 after the leading nameless joint).
- Joint indices (skeleton `player_leftarm_emily.md6skl`, 69 joints = 68 named + one leading root; byte-identical in D2 and DOTO): hand_attachment **59** (= `power_hand::attachment_joint`, valid for Billie too), `lower_armhelper1` 64, **`lower_armhelper2` 65**, `lower_armhelper3` 66, `wrist_rotation` 67, `arm_02` 68 (+1 over the name-list order, because 59 is hand_attachment).
- The mod: `weapon_draw_place.hpp` binds every native weapon `B = H_image * G` with `H_image = full_body.image_world(side, 59, now)` (`dxgi_probe.cpp:4492,4771,4933`), `G` learned from the engine's compose (`G = inverse(H_read)*F`) or from the exact bind (`engine_bind_of`). With the gun on helper 2 and H = joint 59, any wrist flexion/roll of the tracked hand (joint 59 = `wrist_target.q`, `body_ik.hpp:1086-1090`) moves the gun relative to the forearm; the learned G is also measured against a pose where the engine's helper2 and the mod's joint 59 disagree (sanity gap 18-56 mm).
- `body_ik.hpp:1073-1091`: the helpers parented to the elbow (`lower_armhelper1..3`, `arm_02`) are posed as `swing(qb) * twist(roll*along)`, i.e. they follow forearm swing and a fraction of the roll, never the wrist flexion. `image_world(side, joint)` accepts any joint index, so joint 65 is available in the same world space.

### 8.2 Exact bind (engine_bind_of, `dxgi_probe.cpp:4563`): offsets verified

`arkCpntReferenceFrame`: D2 vtable 0x1c573e8 -> DOTO 0x1cdbc98 (RTTI name pair). Its ctor `0x74f5e0 -> 0x7b2030` is **hash-identical** (fn_diff shows every field store at the same offset): master `+0x3c8` (qword), joint `+0x3d0` (**u16**, `mov word [rbx+0x3d0],si`), offset float[3] `+0x3d4`, quaternion float[4] `+0x3e0`, `+0x400` animation, `+0x408` flags, `+0x410`. Copy/assign slot 23 `0x752670 -> 0x7b50f0` copies the same fields with the same displacements.
The owner entity's pointer to its frame is **`+0x358 -> +0x368`** (D2 slots 2 and 4 `mov [rdi+0x358],0` at 0x751ad9/0x751c53 vs DOTO `mov [rdi+0x368],0` at 0x7b4559/0x7b46d3; slot 22 reads `[rax+0x358]`/`[rax+0x368]`). `master+0xf0 -> +0xf8` (entity +8 below 0x2b8). Therefore only `weapon+0x358` and `master+0xf0` needed shifting (the coordinator is doing that); **`comp+0x3c8/0x3d0/0x3d4/0x3e0` stay as written**. Same for `native_weapon_attachment_candidate.hpp` (`binding+0x3c8/0x3d0/0x400`, `*entity+0x358 -> +0x368`, typed vtables 0x1c573e8/0x1c574c0 -> 0x1cdbc98/0x1cdbd70; `*entity+0x358` there is not rewritten by the generator either).
Joint check: `engine_bind_of` refuses when `joint != 59` (`engine_bind_refused[2]`, line 4571). With the forearm tag the DOTO value is **65** for the left arm, so after the offset fix the refusal moves to counter [2] unless the check is generalised.

### 8.3 Proposal 1: bind to the forearm joint, not 59 (file:line)

1. `game_profile.hpp`: `inline constexpr bool forearm_weapon` (DOTO true) and `inline constexpr unsigned forearm_weapon_joint = 65` (left; `left_lower_armhelper2_jnt`; resolve by name via `body_ik`'s `a.find(s+"_lower_armhelper2_jnt")` instead of the literal; `FullBodyDriver::twist_[i][1]` is already that joint).
2. `dxgi_probe.cpp:4571` `engine_bind_of`: accept `joint==attachment_joint || (forearm_weapon && joint==twist helper 2 of that arm)` and return G relative to **that** joint. `weapon_draw::engine_bind_g(q,o,s,cross,g)` composes `q,o` (tag rotation/offset, here identity/0) from the bound joint, so G is exact and the same constant every frame (D2 measured 0.0 mm).
3. `dxgi_probe.cpp:4492` (`place_weapons`), `4771` (native/crossed `image`), `4933` and `5075`: replace `full_body.image_world(side,59,now)` by `image_world(side, weapon_joint(side), now)` where `weapon_joint` = helper2 for a forearm weapon and 59 otherwise. `weapon_draw_place.hpp` `HandSpace` (line 296) gets a third value `space_forearm` next to `space_joint59/space_grip`; G is stored/validated per space (`|G.t|<0.3 m` holds, the gun is within 0.3 m of the helper).
4. The grip pose of the hand must not drive the gun: `grip_pose_blend*.hpp`, `weapon_grip_clips.hpp` and `copy_draw_grip.hpp` (`copy_joint_grip(...)`, line 4752ff) correct the weapon against the grip; for `forearm_weapon` skip these (G is joint-space only). `held_copy::place` offsets for the copy case: use G (identity) relative to the helper.
5. Fallback when the engine bind is absent (copy entity or the other arm): G = identity relative to the helper frame (the tag is `trans 0, rot identity`), i.e. no learning at all.
Verify: status `engine_bind` `used[0]>0, refused==[0,0,0,0], compared mm_max < 1` (existing counters, `engine_bind_json`, line 4588); sim: `tools/linux/scenarios/hands_fingers.py` + a new `forearm_roll.py` that rolls/flexes the left wrist while the arm is still: the gun frame must stay within 1 mm of helper2's world frame (`weapon_draw` `rel` census near line 2039: `rel = inverse(E*J_helper)*B` deviation must be ~0). Headset needed for comfort.

### 8.4 Proposal 2: hand choice moves the gun to the other forearm

- The engine only ever builds the gun's frame on the **left** arm (`player_leftarm_billie.md6`); an equip with the slot on the other hand (`slot1_arm_now()`, `dxgi_probe.cpp:5901`, `arm::slot1_arm_of(arm_pack)`) would hang it on the right hand attachment (Emily default tag). For the right forearm the mod must place a **mirror**: `B = image_world(right, helper2) * M` with M = mirror of the left tag frame (tag identity, so just the right helper2 frame, with the gun mesh mirrored in X by the existing mirror-copy path `held_copy` `write_mirror`).
- Preferred implementation (no second weapon entity for the real gun): keep the real gun entity as is (left arm bind drives gameplay: muzzle, fire scenes), and draw it at the chosen forearm: `weapon_draw` already places any weapon at `B` for every eye pass (`cur' = VP*B*inverse(F)*M`): choose `B` from the right helper2 when the user picked the right arm. Because aim/muzzle use the hand-aim hooks (`NativeShotAim`, `gameplay_aim.hpp`) fed by the chosen forearm frame, firing is independent of where the engine believes the arm is.
- If a separate mirrored entity is wanted (to also keep the left arm free of the gun): copy def = `models/weapons/dlc01/player/billie/gaussgun01/ply_billie_gaussgun01.def` (entityDef, class idEntity, components `cpntweapongaussgun`, `cpntanimevolved`, `cpntmidnightproxy`, `aiattentionsource_gaussgun_fireshot`, `audio_default`) spawned through `spawn_def` and moved by `teleport`, replacing `mirror_def_names[2/4]` (finding 3); its Gauss component must be started or only the visual is used (`started_weapon`). The copy's mesh is left-handed: mirror via the existing `write_mirror` kind. This is the same mechanism D2 used for the dual-wield copies and is the higher-risk route.
- Hand choice input: D2 chooses the gun hand by drawing from the hip (`arm_set.hip_draw(h,...)`, `dxgi_probe.cpp:16188`). For a forearm weapon there is no draw. Suggested gesture: while holding grip on the **opposite** hand, touch the mounted forearm (hand_contact distance < 8 cm for 0.4 s, haptic pulse) -> `arms_apply(arm_set.hip_draw(h, now, rev),"forearm_swap")`; plus a menu setting "Gun arm: left / right" (`vr-settings.cfg`), default left (the engine's native arm). `plan.slot1_arm` then carries the choice through `slot1_arm_now()`, `gun_hand`, `physical_left_occupied` etc.

### 8.5 Proposal 3: the forearm gun must not consume the grip

Today (D2 model) the slot-1 hand owns the grip for the gun: `gun_grip.update(in)` (`dxgi_probe.cpp:16408`) draws/holds the gun with the squeeze, `gun_owns` blocks the hand from grabs (`16492-16509`: `slot1_block=h==rh && (gun_owns || hip_draw_reserved || slot1_ex)`), `left_gun_model` (set at 15112 from slot 0) makes `gate_in.grip_holds_weapon=true` (10466), curls the fingers closed (10551 `hands.curls[1]=1`), routes `fast_native` (16293), `in.draw_authorized`/`busy` (16397-16401) and `physical_left_occupied` (16044). For a forearm weapon none of that may apply. Proposed change (all behind `game_traits::forearm_weapon`):

1. `dxgi_probe.cpp:15112`: do not set `left_gun_model` (or add a separate `left_forearm_gun`) when `forearm_weapon`; the gun is not "held". Then 10466 (`grip_holds_weapon`), 10551 (finger curl), 16293 (`fast_native`), 16374 (`hip_loaded`) and 11859 (`r.left_gun`) stay false.
2. `16407-16448`: skip `gun_grip.update` entirely (`gun_owns=false; hip_draw_reserved=false`); `grip_intent[rh].update` at 16340 then gets `free=true` (the `!gun_owns` term) and the hand behaves like an empty hand: grip alone = Use/**grab** (doors, objects, carry), via `native_grab`, `grip_decision`. Remove the `rh` special-casing in `physical_left_occupied` (16044: `!gun_owns && (...)` becomes the empty-hand rule).
3. Firing: `GripIntent` already classifies **grip + trigger as `Kind::fist`** (`vr_controls.hpp:418-470`: `fist` when both are pressed in the decision window, or a trigger joining within 400 ms of an idle Use). Today a fist "never casts, fires or blocks": `dxgi_probe.cpp:16876-16879` zeroes `input.left_trigger/right_trigger` unless `pass_trigger`. For a forearm gun: after those lines add
   `if(forearm_weapon && grip_intent_out[rh].fist) input.<gun trigger> = raw trigger;` (the gun hand's native trigger is the fire/charge input of slot 1, mapped via the `rh` swap on 16881). `fist` stays true only while grip AND trigger are held, so releasing the trigger ends the shot and releasing the grip cancels charge (`Kind::fist -> none/drain`). Grip alone (Use/grab) and trigger alone keep their present meanings (trigger alone with an empty hand: `pass_trigger` for `Kind::trigger`/`none` casts the slot power; for the forearm hand set `input.trigger=0` when no grip so a stray trigger never fires: the user wants grip+trigger only).
   `16883`: `if(dual_wield_enabled && ... is_gun(copy_at(rh))) input.left_trigger=0` must be skipped for a forearm gun (the trigger is the gun's, not a power's) and the power slot cannot be cast from that hand at the same time (Billie's powers are the other slot anyway).
4. NativeRangedFire route: the real gun fires through the native trigger -> event 206 -> `0x3c1d70` -> `NativeShotAim` hook (`native_shot_aim.hpp` calls `{0x3c219d,0x68e970}`), already steered by the hand-aim frame (publish the **forearm** frame as the gun pose: origin = helper2 frame + muzzle offset `weapon+0x104..0x10c`, forward = forearm axis; `gameplay_aim.hpp` must read the chosen forearm). A mirrored copy on the right forearm needs NativeRangedFire (section 2) because the native gun stays on the engine's arm; its trigger is `dxgi_probe.cpp:16889-16904` (`raw_trigger(h)`, `shot_down`): add `&& grip_intent_out[h].fist` for `forearm_weapon` before `NativeRangedFire::fire(shot)`, and use `pose = forearm frame` instead of `held_copy_pose(h)`.
5. Hip/holster: with `forearm_weapon`, `hip_valid`/`NativeWeaponHands::hip(...)` (10580), `gun_take`/`near_hip` (16179) and the shoulder dock should not offer or take the gun.

Verification: `status` `gun_grip` counters stay 0, `fire.refused` has no `latched`; sim: new scenario `forearm_grab_fire.py` (variant of `tools/linux/scenarios/loot_reach.py` + `hands_fingers.py`): left grip only near a drawer/door -> grab counters (`grab_hands`) rise; left grip+trigger -> one `fired`; trigger only -> nothing. Headset required for the real feel.

### 8.6 Unverified / next live reads (read-only, allowed)
- `engine_bind.last_joint` (`engine_bind_last_joint`) while the Voltaic Gun is equipped: expect 65 after the `+0x368` fix; if it reads 59 the engine's tag fell back to the hand attachment and 8.3 only needs the 59 path.
- `tools/entities.py` read of the gun entity's component at `entity+0x368`: vtable 0x1cdbc98, `+0x3d0` u16, `+0x3d4` floats `0,0,0`, `+0x3e0` quaternion `0,0,0,1`.
- Joint index 65 for helper2: derive at run time (`twist_[i][1]` in `body_ik.hpp:276`), not from the literal.
