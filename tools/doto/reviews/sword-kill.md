# Thrown-sword kill and missed swings (DOTO v145734): causes and fixes

Static review 2026-11-22. D2 `Dishonored2.exe`, DOTO `Dishonored_DO.exe`; tree regenerated from HEAD 9e10a9a rules into the scratchpad `swordkill/out/src`
(scripts `swordkill/d.py` disasm, `sc.py` range grep, `cl.py` call-site finder, `vt.py` vtable scan). Nothing in `src/`, `offsets.json` or the game was changed.
Rule format as in sword-combat.md (`text` rules: `contains` in the D2 line, `old` -> `new`).

## 1. Root cause (certain): the weapon decl's damage-decl pointer moved +0x120 -> +0x128, the port kept 0x120

`NativeMelee::sword()` (native_melee.hpp:89, generated line 89 unchanged) reads `*(weapon_decl+0x120)` as the sword's damage decl (the decl the engine's TakeDamage params are
built from: `ctor 0x7e41b0(params, decl)` copies decl+0x5c.. into params+8..). D2 proof: weapon-component vslot +0x100 `0x62f160` (damage-decl select) calls `0x635090`
= `mov rax,[rcx+8]; mov rax,[rax+0x120]; ret` for the default (non-focused/blink/bloodthirsty) case.
DOTO twin: same vslot +0x100 = `0x683be0` calls `0x689a70` = `mov rax,[rcx+8]; mov rax,[rax+0x128]; ret`. The other decl-select fields shifted the same way
(D2 0x1d0/0x1e0/0x1e8/0x1f0 -> DOTO 0x1d8/0x1e8/0x1f0/0x1f8-0x200; so the decl grew a field before 0x128). `ctor_code` (0x7e41b0 reads decl +0x5c/0x60/0x68/0x6c) and
TakeDamage `take 0x8e6bb0` are byte-identical in the damage decl itself, so once the right decl is passed the params layout holds.

The same stale literal is in two places (both D2-only, no rule exists; grep of offsets.json finds none):
* `native_melee.hpp:89`  `value<std::uintptr_t>(read,*wdecl+0x120)` -> `NativeMelee::sword()` returns a wrong pointer, or `{}` if +0x120 is null / not a pointer.
* `native_sword_contact.hpp:77` `value<std::uintptr_t>(read,*weapon_decl+0x120)!=sword->decl` (contact equipment validation; equal to the bad value, so it would not catch it).

Consequences, one cause for both reports:
* Throw: `service(Op::drop)` (native_weapon_hands.hpp:209-213) takes `sword_=NativeMelee::sword(...)`. If it is empty, `fly()` never calls `strike` (`speed>=3 && melee_ok && sword_`),
  so the copy flies and sticks, `thrown_hits` stays 0, `unaware_asked` stays 0. If it is a wrong decl, the params carry garbage/other damage (no `unaware_amount=1000` effect on type or
  reaction fields; wrong max damage) and the guard does not die. Flight itself (what the user sees working) needs none of this.
* Swings: `native_contact_equipment` (native_sword_contact.hpp:58) calls `NativeMelee::sword`; with it empty `capture_sword_contact` returns `{}` for every physical contact
  (`++sword_capture_rejected_`, dxgi_probe.cpp:11126), so only strikes where the swing also pressed the native attack and the game's own Slash (state 39) landed register: "sometimes misses".
  The KillCam id fix (83->84) only unblocks the step before this one.

Fix (two rules, `source` review/sword-kill):
```json
{"file":"src/native_melee.hpp","contains":"value<std::uintptr_t>(read,*wdecl+0x120)","old":"*wdecl+0x120","new":"*wdecl+0x128",
 "why":"weapon decl damage-decl pointer: D2 0x635090 mov rax,[rax+0x120] -> DOTO 0x689a70 mov rax,[rax+0x128] (vslot +0x100 damage select 0x62f160 -> 0x683be0)","source":"review/sword-kill"},
{"file":"src/native_sword_contact.hpp","contains":"*weapon_decl+0x120","old":"*weapon_decl+0x120","new":"*weapon_decl+0x128",
 "why":"same field as native_melee sword(): equipment identity check","source":"review/sword-kill"}
```
Alternative if a rule is preferred in src: `game_traits::weapon_decl_damage_offset` (D2 0x120, DOTO 0x128) used at both sites; also fix the doc comment at native_melee.hpp:68.

## 2. Other D2 constants beside it (verify; not proven wrong)
* `native_melee.hpp:311` `*wdecl+0x138` = weapon impact material (matA of the ContactSystem Play). No D2 code in the melee component range reads +0x138 and DOTO's has none either, so
  I could not map it; the material would be wrong only for the impact fx/blood, not for damage. Check live: `*(wdecl+0x138)` should be a pointer typed `material_vtable_rva` (0x1bd9b58).
* DOTO TakeDamage (0x779800 vs D2 0x717950) is the same flow plus an `isUnaware` (0x1406143c0) bit stored in the result flags (`[rsi+0x18] bit 4`) and a new vcall `[rax+0xd8]` before the dead
  check. The 1000 amount is still clamped to hp by the health component, so a lethal amount kills; nothing in the diff gates it. `isUnaware` pin `0x6143c0` byte-matches
  (`unaware_code` verified against the exe) and the body reads `0xd9` (dead 8, 0x20) consistently: the unaware test is fine.
* Thrown strike path checks out: registry `game+0x327770` (translated), NPC model `+0xf8` (translated in the regenerated tree; the old build/doto-src still had 0xf0 until re-port),
  model position `+0x1860` is unchanged (DOTO present-model `0x8eb680` writes `[model+0x1860/64/68]`), vtable `arkNPC 0x1ce2f28` exact test, `damageable` 0xdf/0xd9, victim id `+0xb0`, Teleport
  `0x8eab40` is the same wrapper (`0x8eab60` body) as D2 `0x87d1e0`/`0x87d200`.
* Design note (same as D2, not a bug): the 1000 amount applies only when `isUnaware` is true (`apply`, native_melee.hpp:132); an alerted guard takes the knife's normal m_maxDamage
  (D2 sword 10). Billie's knife may have a different damage/hp ratio; check the knife damage decl `m_maxDamage` if aware guards survive.

## 3. Swing misses: remaining (lower confidence) contributors
1. Blade length: `game_traits::player_blade_length` is already .62 for DOTO in the working tree (game_profile.hpp:33, used at dxgi_probe.cpp:6132); the blade segment from the drawn hand is
   fine for Billie's knife (x -0.209..0.62). Not a miss source after that change; confirm the headset build contains it.
2. Contact path gates (dxgi_probe.cpp:11103-11141): `why` = not drawn / no hand / epoch / holding / too_slow (legacy only) / `sword_hit_gate.open`. The default path has no speed gate, so the
   only systematic rejecter is `capture_sword_contact` (cause 1). After the fix expect `contact_gates[7]` to track `contact_gates[0]`.
3. Slash timing (state 39 and the scene rate fix a4c2638) already reviewed in sword-combat.md B2.
4. NPC origin/row reads used for rear/side (`+0x300/+0x30c` D2 -> `+0x310/+0x31c`) are translated in the regenerated tree.

## 4. Headset-free verification (tools\doto\doto_sim.py, scenarios tools\linux\scenarios\sword_sweep.py, sword_regrab.py; run only when the PC is idle)
1. `python tools/doto/port.py`; `grep -n "\*wdecl+0x128" build/doto-src/src/native_melee.hpp`, `grep -n "\*weapon_decl+0x128" build/doto-src/src/native_sword_contact.hpp` must hit.
2. `sword_sweep` in a save with a guard: before the fix `sword_contact.capture_rejected` == contact count and `enqueued/invoked/native_returned` stay 0; after, `contact_gates[7]` > 0,
   `capture_rejected` 0, `invoked` and `native_returned` rise, `stealth_reject` as before.
3. `sword_regrab` (throw): `sword_grip.thrown` = [hits, flight_frames, peak, ...] expects hits >= 1 when a guard is in the path; `sword_grip.stealth` = [unaware_asked, unaware_yes] must show asked >= 1
   (it is 0 today because `sword_` is empty or the strike never runs), yes >= 1 for an unaware guard; `sword_grip.native` = [ops, failures, spawned, removed] failures 0.
4. Live read (allowed while the game runs): weapon component `comp+8` (decl, typed 0x1cad8d8) -> `+0x128` should be a pointer to a `damage/*.damage` decl, `+0x120` something else; compare with
   `NativeMelee::sword` returning non-empty (add `sword_` null count to the status JSON if needed).
