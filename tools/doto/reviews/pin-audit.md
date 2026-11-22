# Pin audit of the generated DOTO tree (build/doto-src/src)

Audited 2026-11-22 against Dishonored_DO.exe (sha256 be73d979...) with D2 Dishonored2.exe for comparison. No source, rule or build was changed.

Method: every byte list (`{0xNN,...}` including decimal wildcards such as `8` and `0`), every `{rva,[size,]{bytes}}` pin table, hex-string
signature, `{rva,size,0xHASHull}` FNV pin AND the parallel `rvas/sizes/hashes` tables (which port.py does not know) was parsed from the
generated sources and resolved in the DOTO image. A pin is OK when its bytes are in DOTO at the RVA written next to it (tables) or, for
named arrays, when the D2 location translates (translate.py) to a DOTO location holding those bytes. Scratch scripts:
`scratchpad/pins/{scan2,cons,hashes,hcls,dcls,rules}.py` of the audit session.

Class key: (a) only rip-relative / call rel32 differ; (b) struct displacement, frame or vtable-slot shifts with the same ABI; (c) real code
change; (d) generated RVA wrong or untranslated (poison 0xfff000xx, or still a D2 RVA) with a DOTO address found.

Generator gaps that explain most rows: (1) byte groups containing decimal tokens (`8`, `0`) are not recognised; (2) tables with parallel
`sizes[]/hashes[]` arrays are never re-hashed (only the brace form `{rva,size,hash}`); (3) leaf functions without pdata are poisoned even when
a unique byte match exists; (4) hash pins with status `semantic` (a displacement moved by an unvoted amount) stay at the D2 hash.

## Ranked findings

| # | file:line | guarded feature (what the user loses) | default-on? | class | evidence | rule | conf. |
|---|---|---|---|---|---|---|---|
| 1 | native_weapon_hands.hpp:2323-2339 (`anim_pinned`: 0xfff00070/80/90 and `b[10]==0x60`) | Copy-weapon animation resume/suspend when a hand takes or equips a weapon; `anim_pinned` is false so every call returns early | yes (only build/no-copy-anim disables it) | d + b | equip block unique at DOTO 0x670c88; calls at 0x670c9b / 0x670ca8 decode to 0x425050 / 0x84e0d0; thunk field 0x1e8960 -> 0x1e8968 | A: 3 rvas + 1 text | high |
| 2 | bodyless_motion_guard.hpp:27-30 (prologue 64 B at 0x7cff90) | Crash guard against a motion switch on a bodyless physics system (crash 0xcaf005): `install()` returns false silently, so the engine crash stays reachable | yes (installed unconditionally, dxgi_probe.cpp:18655) | a | only `cmp r8w,[rip+0x1bb0bd3]` -> `[rip+0x1c11683]` | A: 1 text | high |
| 3 | alternate_native_taa.hpp:15-21 (`verified_abi`, 7 FNV regions, 5 differ) | Per-eye alternate native TAA + projection jitter: `alternate_native_taa_abi` is false, so the whole path (`alternate_taa_requested`) is off. Rendering | yes (`no-alternate-native-taa` absent) | b | view struct moved -0x10 (0x3a88->0x3a78, 0x15c->0x14c, 0x140->0x130); pinned regions keep an identical instruction stream. The function around region 0x375447 also gained flag-bit writes to +0x3a37 outside the pinned ranges | C: 5 hashes (spot-check first) | medium |
| 4 | native_note_kind.hpp:93 (ShowMap 0x7f5da0 / ShowNote 0x7f6000 FNV regions) | Note/map kind detection for the held notes tablet (`configure()` false, kind never known) | yes (`note-tablet` default true) | b | same instruction stream, fields +0x10 (0xe0->0xf0, 0xd4->0xe4) | C: 2 hashes | medium |
| 5 | native_choke_distance_scope.hpp:28-30 (`caller_code` at 0x3e4739) | "Closer takedown trigger" choke scope; consumer skipped (`optional_native_consumers`), choke offers stay at the game's reach | yes (`takedown-range`) | a | two call rel32 only | A: 1 text | high |
| 6 | npc_weapon_pickup.hpp:207,336 (`manager_code`; hide/show 0xfff000c0/d0) | Taking a guard's sword or pistol with a hand (`verified()` false, so nothing is picked up or released) | yes (gameplay path, no knob) | a/b + d | field 0x1e8968 -> 0x1e8970 (same as weapon_hands:2256); hide/show are unique byte matches at 0x8e5de0 / 0x8e9f40 | A: 1 text + 2 rvas | high |
| 7 | native_door_event_admission.hpp:41-47 and native_door_animation.hpp:43-52 (hash/prologue tables) | VR-hand door opening (`NativeDoorInteraction` needs both owners; on failure the game's own door interaction stays, `optional_native_consumers`) | yes with controller aim | mixed: 11 relocated/layout (B), 6 disp-only (C), real changes in door_event 3, 13, 14 and door_anim 1, 2, one poison (door_event idx 8, D2 0x74ba10, no DOTO twin by pattern) | see sections B, C | B + C | low for the feature as a whole: it cannot be revived without reviewing the real changes |
| 8 | native_offered_choke.hpp:44-45 | Choke-offer observer (consumer skipped) | yes | idx0 b (0x338->0x390 ...), idx1 c (63 -> 61 insns, 252 -> 271 B) | | C: idx0 only | low |
| 9 | native_slash_scene_rate.hpp:33,38-41 | Slash-speed scene rate (`slash-speed`, consumer skipped) | yes | c | D2 0x86cbf0 -> DOTO 0x8d7ed0: sub rsp,0x60 -> 0x90, [rcx+0xcc] -> [rcx+0xd4], mov rcx,[rcx+0xa0] -> cmp qword [rcx+0xa8],0 (10 vs 9 insns); callers 0x86cd7d / 0x86cdb9 / 0x86cdf9 (fn 0x86cd06) have no DOTO twin (poison); jmp 0x8d790e rel32 differs | none (needs RE) | high that it is a real change |
| 10 | native_ranged_fire.hpp:252,255,577,578 | Hand-fired pistol/crossbow copies (`verify()` false, NativeRangedFire stays off) | n/a | c | known: Voltaic Gun (weapons.json). 0x6906e0: extra push and vcall [rdx+0x170]; 0x6904b0: [rcx+0x98]->[rcx+0xa0], 0x1e8->0x248 and a new `cmp byte [rcx+0xb0],0` branch; 0x49cfb0 [rcx+0x158]->[rcx+0x178]; 0x49cf20 [rcx+0xa4]->[rcx+0xac]; the crossbow 201 hook 0x62bfe0 (line 252) is an untranslated D2 RVA (the 200 hook of the same row translated to 0x680740) | none | high |
| 11 | secondary_view_guard.hpp:18-20 | Timepiece-lens secondary view guard (render prologue [rcx+0x88] -> [rcx+0x78]; `set_rva` 0xfff000e0 poison) | D2-only power | b/d | probably not applicable to DOTO | none | low |
| 12 | native_door_skeleton_content.hpp:9,100 (pin 0 and the vtable-slot check) | Door owner clip/skeleton lease (part of #7) | with #7 | d | D2 0x25df10 == DOTO 0x18e6f0 (vtable slot +0x20 of the skinned model, bytes identical) | A: 1 rva | high for this pin; the other pins and the door_clip_content data blocks still need work |
| 13 | native_weapon_source_receipts.hpp:233-235 (0xfff000a0/b0 and two byte arrays) | Passive weapon-attachment trace (diagnostic, needs a timed `attachment_flag` run; `install()` throws today) | no | d | idGameLocal vtable slots 0xc0/0xb8 -> 0x9173d0 / 0x917440 | A: 2 rvas + 2 text | high |
| 14 | native_assassination_receipt_hooks.hpp:25-39 | Assassination receipt jsonl observer (diagnostic, only when a folder is passed) | no | idx1 relocated; idx0, 3 disp-only; idx2, 4 real (459 -> 480 insns; 2210 -> 2261 B) | | B idx1, C idx0 and 3 | medium |
| 15 | native_fire_seam_trace.hpp:62,65 | Fire-seam diagnostic (needs build/fire-probe) | no | d / c | 0x632f70 poison; 0x867330 instruction boundaries differ | none | n/a |
| 16 | native_lean_lifecycle.hpp:17-25 | Lean lifecycle observer (default off) | no | 5 of 9 hex signatures (indices 1,2,3,5,7) match only D2 | | none | n/a |
| 17 | native_gameplay_aim_trace.hpp:21 (0x7f727a poison); authored_hand_bank.hpp:30 and native_hand_reference.hpp:30 (D2 exe sha256 literal) | diagnostics / data-file provenance, not referenced by dxgi_probe.cpp | no | n/a | | none | n/a |

Not game-code pins, ignore: the `unwinds{}` arrays (they match neither exe's .pdata, so they describe the mod's relay), native_choke_distance_scope.hpp:23,178,180,
native_takedown_range.hpp:19, sun_sprite_aspect.hpp patch bytes, native_call_trace.hpp:29, controller_input.hpp:235 (GUID), dxgi_probe.cpp:14275 and :14311.

## A. Mechanical rules for offsets.json

"text" list:

```json
[
 {
  "file": "src/bodyless_motion_guard.hpp",
  "contains": "0x05,0xd3,0x0b,0xbb,0x01,0x4d,0x8b,0xe1",
  "old": "0x05,0xd3,0x0b,0xbb,0x01,0x4d,0x8b,0xe1",
  "new": "0x05,0x83,0x16,0xc1,0x01,0x4d,0x8b,0xe1",
  "why": "bodyless-motion guard entry D2 0x76ce50 -> DOTO 0x7cff90 (64 B): only cmp r8w,[rip+disp] differs (0x1bb0bd3 -> 0x1c11683 = sentinel 0x23e1630 already translated); both jcc rel8 keep their displacement (0x47, 0x30). port.py said 'semantic: byte 27 outside any operand field' (the 66 prefix confuses its field map).",
  "source": "review/pin-audit"
 },
 {
  "file": "src/native_choke_distance_scope.hpp",
  "contains": "0xe8,0x3d,0xfc,0xff,0xff,0x8b,0x4b,0x50",
  "old": "0xe8,0x3d,0xfc,0xff,0xff,0x8b,0x4b,0x50,0x0f,0x28,0xf8,0xe8,0xa2,0x5a,0x33,0,",
  "new": "0xe8,0x11,0xfc,0xff,0xff,0x8b,0x4b,0x50,0x0f,0x28,0xf8,0xe8,0xc6,0x94,0x35,0,",
  "why": "caller_code D2 0x3a6a9d -> DOTO 0x3e4739 (38 B): identical except the two call rel32 (0x3a66f0 -> 0x3e4360 = rvas[1]; 0x6dc560 -> 0x73dc20). The generator skipped it (decimal tokens 8 and 0 split the byte group; 'site-unmapped').",
  "source": "review/pin-audit"
 },
 {
  "file": "src/npc_weapon_pickup.hpp",
  "contains": "manager_code{0x48,0x8b,0x05,0x49,0x50,0x19,0x03",
  "old": "0x48,0x8b,0x05,0x49,0x50,0x19,0x03,0x48,0x05,0x68,0x89,0x1e,0x00,0xc3",
  "new": "0x48,0x8b,0x05,0x89,0xa6,0x1f,0x03,0x48,0x05,0x70,0x89,0x1e,0x00,0xc3",
  "why": "game-manager accessor D2 0x810240 -> DOTO 0x877bf0: mov rax,[rip+g]; add rax,0x1e8968 -> 0x1e8970. native_weapon_hands.hpp:2256 carries the same bytes and was rewritten; npc_weapon_pickup.hpp:336 is a second copy that was not.",
  "source": "review/pin-audit"
 },
 {
  "file": "src/native_weapon_hands.hpp",
  "contains": "b[10]==0x60 && b[11]==0x89 && b[12]==0x1e",
  "old": "b[10]==0x60 && b[11]==0x89 && b[12]==0x1e",
  "new": "b[10]==0x68 && b[11]==0x89 && b[12]==0x1e",
  "why": "anim_pinned Resume/Suspend thunks 0x84e0d0 / 0x84df10 (DOTO): mov rdx,[rdx+0x1e8960] is [rdx+0x1e8968] (bytes 48 8b 92 68 89 1e 00); the global ref 0x3a72280 and the jmp targets (+0x20) already translate correctly.",
  "source": "review/pin-audit"
 },
 {
  "file": "src/native_weapon_source_receipts.hpp",
  "contains": "counter{0x48,0x81,0xc1,0x28,0x74,0x2e,0,0xe9,0x14,0x78,0x7c,0}",
  "old": "0x48,0x81,0xc1,0x28,0x74,0x2e,0,0xe9,0x14,0x78,0x7c,0",
  "new": "0x48,0x81,0xc1,0x48,0x7c,0x2f,0,0xe9,0x74,0xcd,0x7d,0",
  "why": "DOTO 0x9173d0 = 48 81 c1 48 7c 2f 00 e9 74 cd 7d 00 (counter offset 0x2e7428 -> 0x2f7c48; jmp rel32 -> 0x10f4150, the value_code target already pinned at line 241)",
  "source": "review/pin-audit"
 },
 {
  "file": "src/native_weapon_source_receipts.hpp",
  "contains": "address{0x48,0x8d,0x81,0x28,0x74,0x2e,0,0xc3}",
  "old": "0x48,0x8d,0x81,0x28,0x74,0x2e,0,0xc3",
  "new": "0x48,0x8d,0x81,0x48,0x7c,0x2f,0,0xc3",
  "why": "DOTO 0x917440 = 48 8d 81 48 7c 2f 00 c3",
  "source": "review/pin-audit"
 }
]
```

"rvas" list (no "file": global, like the existing entries; each poison value is unique):

```json
[
 {
  "from": "0x8784b0",
  "to": "0x8e5de0",
  "why": "idEntity hide (or byte [rcx+0xd0],1; jmp): D2 0x8784b0 -> DOTO 0x8e5de0, unique byte match 0f b6 81 d8 00 00 00 a8 01 75 0d 0c 01 88 81 d8 00 00 00 e9 (flags +0xd0 -> +0xd8 as in weapons.json); jmp target is translate(0x87e040)=0x8eb990 on both sides. A leaf without pdata, so port.py poisoned it.",
  "source": "review/pin-audit"
 },
 {
  "from": "0x87c590",
  "to": "0x8e9f40",
  "why": "idEntity show (and byte [rcx+0xd0],0xfe; jmp): D2 0x87c590 -> DOTO 0x8e9f40, unique match, same jmp target 0x8eb990.",
  "source": "review/pin-audit"
 },
 {
  "from": "0x61af85",
  "to": "0x670c88",
  "why": "anim_pinned equip block (19 B 8b 43 1c c1 e8 1e a8 01 75 1b 48 8d 4b 20 ba 01 00 00 00) inside the weapon-equip fn D2 0x61aed0 (lengths 0x139 vs 0x13c, so the generator only found 'same shape'); unique in DOTO at 0x670c88, verified by disassembly.",
  "source": "review/pin-audit"
 },
 {
  "from": "0x61af98",
  "to": "0x670c9b",
  "why": "call 0x425050 (= translated 0x3da910) at DOTO 0x670c9b, rel32 decoded",
  "source": "review/pin-audit"
 },
 {
  "from": "0x61afa5",
  "to": "0x670ca8",
  "why": "call 0x84e0d0 (= translated 0x7e79a0 Resume thunk) at DOTO 0x670ca8, rel32 decoded",
  "source": "review/pin-audit"
 },
 {
  "from": "0x8a89a0",
  "to": "0x9173d0",
  "why": "idGameLocal vtable +0xc0 getter: DOTO slot value 0x9173d0 = add rcx,0x2f7c48; jmp 0x10f4150 (D2: add rcx,0x2e7428; jmp 0x10701c0). Read from the DOTO vtable 0x1d1a678+0xc0.",
  "source": "review/pin-audit"
 },
 {
  "from": "0x8a8a20",
  "to": "0x917440",
  "why": "idGameLocal vtable +0xb8 getter: DOTO slot value 0x917440 = lea rax,[rcx+0x2f7c48]; ret (D2 lea rax,[rcx+0x2e7428]; ret).",
  "source": "review/pin-audit"
 },
 {
  "from": "0x25df10",
  "to": "0x18e6f0",
  "why": "skinned-render-model vtable slot +0x20 stub (cmp edx,2; sete al; ret = 83 fa 02 0f 94 c0 c3): D2 0x25df10 (vtable 0x1b0d628+0x20) <-> DOTO 0x18e6f0 (vtable 0x1c146e8+0x20), bytes IDENTICAL. body-rig.json says the equivalent is slot +0x80 (0x263e30), but that function is mov rax,[rcx+0x1fd8]; add rax,0xe0 and cannot satisfy the pin. Fixes pins[0] (line 9) and reader.equals(+0x20, ...) (line 100).",
  "source": "review/pin-audit"
 }
]
```

## B. FNV pins that differ only by relocation or a voted layout move (safe, "text" list)

```json
[
 {
  "file": "src/native_door_event_admission.hpp",
  "contains": "0xc134621a70d6547dull",
  "old": "0xc134621a70d6547dull",
  "new": "0x3464e5c24b3dc868ull",
  "why": "FNV-1a pin 0x810430->0x877de0 size 278: DOTO bytes hashed (layout disp 0xc94c->0xd060); regenerated hash",
  "source": "review/pin-audit"
 },
 {
  "file": "src/native_door_event_admission.hpp",
  "contains": "0xe008b89df8694e5ull",
  "old": "0xe008b89df8694e5ull",
  "new": "0xaea79e64facf7269ull",
  "why": "FNV-1a pin 0x811320->0x878d00 size 25: DOTO bytes hashed (relocated ); regenerated hash",
  "source": "review/pin-audit"
 },
 {
  "file": "src/native_door_event_admission.hpp",
  "contains": "0x333dd8e999eeadf4ull",
  "old": "0x333dd8e999eeadf4ull",
  "new": "0x6146b64a5f6e1e65ull",
  "why": "FNV-1a pin 0x74d7a0->0x7b0200 size 367: DOTO bytes hashed (relocated ); regenerated hash",
  "source": "review/pin-audit"
 },
 {
  "file": "src/native_door_event_admission.hpp",
  "contains": "0xe074b24d83db4c61ull",
  "old": "0xe074b24d83db4c61ull",
  "new": "0x53c048ab27821272ull",
  "why": "FNV-1a pin 0x53eef0->0x58ea90 size 162: DOTO bytes hashed (relocated ); regenerated hash",
  "source": "review/pin-audit"
 },
 {
  "file": "src/native_door_event_admission.hpp",
  "contains": "0x5143bed2cc687e33ull",
  "old": "0x5143bed2cc687e33ull",
  "new": "0xec100195a57bd78eull",
  "why": "FNV-1a pin 0x74c560->0x7aefc0 size 93: DOTO bytes hashed (relocated ); regenerated hash",
  "source": "review/pin-audit"
 },
 {
  "file": "src/native_door_event_admission.hpp",
  "contains": "0x13f891989dc5e160ull",
  "old": "0x13f891989dc5e160ull",
  "new": "0x49906ea8943855f1ull",
  "why": "FNV-1a pin 0x74a800->0x7ad220 size 188: DOTO bytes hashed (relocated ); regenerated hash",
  "source": "review/pin-audit"
 },
 {
  "file": "src/native_door_event_admission.hpp",
  "contains": "0x894d758fbe8eceb1ull",
  "old": "0x894d758fbe8eceb1ull",
  "new": "0x9178c9254deae68bull",
  "why": "FNV-1a pin 0x8af583->0x91e423 size 12: DOTO bytes hashed (layout disp 0x1e8968->0x1e8970); regenerated hash",
  "source": "review/pin-audit"
 },
 {
  "file": "src/native_door_event_admission.hpp",
  "contains": "0x8df1059b0c6eeb28ull",
  "old": "0x8df1059b0c6eeb28ull",
  "new": "0x1e5ca2d148187ffull",
  "why": "FNV-1a pin 0x8101f0->0x877ba0 size 12: DOTO bytes hashed (layout disp 0xc8e8->0xcff0); regenerated hash",
  "source": "review/pin-audit"
 },
 {
  "file": "src/native_door_event_admission.hpp",
  "contains": "0x5b5f1d96e5165acaull",
  "old": "0x5b5f1d96e5165acaull",
  "new": "0x87571c85fba8eeeaull",
  "why": "FNV-1a pin 0x74bdd6->0x7ae826 size 11: DOTO bytes hashed (relocated ); regenerated hash",
  "source": "review/pin-audit"
 },
 {
  "file": "src/native_door_event_admission.hpp",
  "contains": "0x492dc3d4dfe23e65ull",
  "old": "0x492dc3d4dfe23e65ull",
  "new": "0xd294e51a83a9dbf7ull",
  "why": "FNV-1a pin 0x74af00->0x7ad930 size 131: DOTO bytes hashed (relocated ); regenerated hash",
  "source": "review/pin-audit"
 },
 {
  "file": "src/native_door_event_admission.hpp",
  "contains": "0x5fd62a1ca579833full",
  "old": "0x5fd62a1ca579833full",
  "new": "0xab4d10c06a9587dcull",
  "why": "FNV-1a pin 0x8112c0->0x878ca0 size 94: DOTO bytes hashed (relocated ); regenerated hash",
  "source": "review/pin-audit"
 },
 {
  "file": "src/native_door_animation.hpp",
  "contains": "0x6c8c6360081cb7c7ull",
  "old": "0x6c8c6360081cb7c7ull",
  "new": "0x5680492e15ec45e8ull",
  "why": "FNV-1a pin 0x6e3fa0->0x745510 size 448: DOTO bytes hashed (relocated ); regenerated hash",
  "source": "review/pin-audit"
 },
 {
  "file": "src/native_door_animation.hpp",
  "contains": "0x017ed628b7d5ba7aull",
  "old": "0x017ed628b7d5ba7aull",
  "new": "0x681e9da600e07142ull",
  "why": "FNV-1a pin 0x6e4850->0x745dc0 size 469: DOTO bytes hashed (relocated ); regenerated hash",
  "source": "review/pin-audit"
 },
 {
  "file": "src/native_assassination_receipt_hooks.hpp",
  "contains": "0x9312bc958039dafeull",
  "old": "0x9312bc958039dafeull",
  "new": "0x4498e0c131255f0full",
  "why": "FNV-1a pin 0x8636a0->0x8cd170 size 83: DOTO bytes hashed (relocated ); regenerated hash",
  "source": "review/pin-audit"
 }
]
```

## C. FNV pins that differ by displacement shifts only (apply after a spot check, "text" list)

New hash = FNV-1a of the DOTO bytes at the translated RVA, same size. Mnemonics and operand shapes are identical; displacements moved with the
struct shifts documented in weapons.json and body-rig.json (+8 / +0x10 entity fields, view -0x10). Caveat: an indirect `call [reg+disp]` vtable-slot
shift is not separable in a text compare (door_animation idx 3, fn 0x746060, differs only in one such slot, 0x90 -> 0x98). For rows 3 and 7 also
confirm the mod's own offsets in the same files were translated.

```json
[
 {
  "file": "src/native_door_event_admission.hpp",
  "contains": "0x10c43863d84872b9ull",
  "old": "0x10c43863d84872b9ull",
  "new": "0xe2a05f7706dfd0fbull",
  "why": "FNV-1a pin 0x810000->0x8779b0 size 489: DOTO bytes hashed (semantic disp 0x1d30->0x2000 not the voted move (mixed 3/5 0x2000:3,0x1d30:2)); regenerated hash",
  "source": "review/pin-audit"
 },
 {
  "file": "src/native_door_event_admission.hpp",
  "contains": "0xae40fe40d660cfd6ull",
  "old": "0xae40fe40d660cfd6ull",
  "new": "0x5cdb3d025720223bull",
  "why": "FNV-1a pin 0x74c5c0->0x7af020 size 1092: DOTO bytes hashed (semantic disp 0xd5->0xdd not the voted move (mixed 8/21 0xd5:8,0xdd:7,0xdf:5,0x); regenerated hash",
  "source": "review/pin-audit"
 },
 {
  "file": "src/native_door_event_admission.hpp",
  "contains": "0x9af0b80ec9778783ull",
  "old": "0x9af0b80ec9778783ull",
  "new": "0x656e95a52e68b538ull",
  "why": "FNV-1a pin 0x74dbb0->0x7b0610 size 402: DOTO bytes hashed (semantic disp 0xd4->0xdc not the voted move (mixed 485/745 0xd4:485,0xdc:202,0x); regenerated hash",
  "source": "review/pin-audit"
 },
 {
  "file": "src/native_door_event_admission.hpp",
  "contains": "0x1d51e3c464ab3ec3ull",
  "old": "0x1d51e3c464ab3ec3ull",
  "new": "0x238428ee20579f8aull",
  "why": "FNV-1a pin 0x74e080->0x7b0ae0 size 380: DOTO bytes hashed (semantic disp 0x20->0x28 not the voted move (same 104666/105259 0x20:104666,0x2); regenerated hash",
  "source": "review/pin-audit"
 },
 {
  "file": "src/native_door_event_admission.hpp",
  "contains": "0xe784f56538d7900eull",
  "old": "0xe784f56538d7900eull",
  "new": "0x19e8c02338df9086ull",
  "why": "FNV-1a pin 0x74c1f0->0x7aec40 size 734: DOTO bytes hashed (semantic instruction boundaries differ); regenerated hash",
  "source": "review/pin-audit"
 },
 {
  "file": "src/native_door_animation.hpp",
  "contains": "0xf11c59744626d909ull",
  "old": "0xf11c59744626d909ull",
  "new": "0x92bd36116cd539d0ull",
  "why": "FNV-1a pin 0x6e4af0->0x746060 size 1760: DOTO bytes hashed (semantic disp 0x90->0x98 not the voted move (same 10032/10593 0x90:10032,0x98:3); regenerated hash",
  "source": "review/pin-audit"
 },
 {
  "file": "src/native_assassination_receipt_hooks.hpp",
  "contains": "0x3efd3df6f7fb48b0ull",
  "old": "0x3efd3df6f7fb48b0ull",
  "new": "0x981f354f14f2f1a0ull",
  "why": "FNV-1a pin 0x3b1700->0x3f0090 size 487: DOTO bytes hashed (semantic disp 0x1d0->0x1d8 not the voted move (same 1038/1130 0x1d0:1038,0x1d8:); regenerated hash",
  "source": "review/pin-audit"
 },
 {
  "file": "src/native_assassination_receipt_hooks.hpp",
  "contains": "0x39e856bec7ae4992ull",
  "old": "0x39e856bec7ae4992ull",
  "new": "0x56f341dff2a69bcdull",
  "why": "FNV-1a pin 0x832d20->0x89b2d0 size 261: DOTO bytes hashed (semantic disp 0x1d0->0x1d8 not the voted move (same 1038/1130 0x1d0:1038,0x1d8:); regenerated hash",
  "source": "review/pin-audit"
 },
 {
  "file": "src/native_offered_choke.hpp",
  "contains": "0xde5a0e9ec71e378full",
  "old": "0xde5a0e9ec71e378full",
  "new": "0xd272ee322e4c7501ull",
  "why": "FNV-1a pin 0x391280->0x3c7dd0 size 366: DOTO bytes hashed (semantic disp 0x338->0x390 not the voted move (mixed 364/472 0x338:364,0x390:81); regenerated hash",
  "source": "review/pin-audit"
 },
 {
  "file": "src/alternate_native_taa.hpp",
  "contains": "0x620885b5b4c9ad51ull",
  "old": "0x620885b5b4c9ad51ull",
  "new": "0xce2aeeff96d5f397ull",
  "why": "FNV-1a pin 0x374e61->0x37bd71 size 111: DOTO bytes hashed (semantic disp 0x3a88->0x3a78 not the voted move (mixed 6/11 0x3a88:6,0x3a78:5)); regenerated hash",
  "source": "review/pin-audit"
 },
 {
  "file": "src/alternate_native_taa.hpp",
  "contains": "0x368ca4de159f0d57ull",
  "old": "0x368ca4de159f0d57ull",
  "new": "0x623fd51d52816281ull",
  "why": "FNV-1a pin 0x375447->0x37c36f size 662: DOTO bytes hashed (semantic disp 0x3a88->0x3a78 not the voted move (mixed 6/11 0x3a88:6,0x3a78:5)); regenerated hash",
  "source": "review/pin-audit"
 },
 {
  "file": "src/alternate_native_taa.hpp",
  "contains": "0xc082ab5a71f586b6ull",
  "old": "0xc082ab5a71f586b6ull",
  "new": "0x6c2a184685edf7c3ull",
  "why": "FNV-1a pin 0x375b87->0x37caaf size 149: DOTO bytes hashed (semantic disp 0x15c->0x14c not the voted move (mixed 227/471 0x15c:227,0x164:21); regenerated hash",
  "source": "review/pin-audit"
 },
 {
  "file": "src/alternate_native_taa.hpp",
  "contains": "0xbf3ec9f7f048d570ull",
  "old": "0xbf3ec9f7f048d570ull",
  "new": "0xa654dc8194747f90ull",
  "why": "FNV-1a pin 0x3760a6->0x37cfce size 428: DOTO bytes hashed (semantic disp 0x3a88->0x3a78 not the voted move (mixed 6/11 0x3a88:6,0x3a78:5)); regenerated hash",
  "source": "review/pin-audit"
 },
 {
  "file": "src/alternate_native_taa.hpp",
  "contains": "0x5548a7f8d7a0cdcfull",
  "old": "0x5548a7f8d7a0cdcfull",
  "new": "0x1046de22ce509396ull",
  "why": "FNV-1a pin 0x3742b0->0x37b1a0 size 120: DOTO bytes hashed (semantic disp 0x140->0x130 not the voted move (same 2464/2669 0x140:2464,0x150:); regenerated hash",
  "source": "review/pin-audit"
 },
 {
  "file": "src/native_note_kind.hpp",
  "contains": "0xc43e4cabde100658ull",
  "old": "0xc43e4cabde100658ull",
  "new": "0x3b6ef1c27924e8fdull",
  "why": "FNV-1a pin 0x790380->0x7f5da0 size 594: DOTO bytes hashed (semantic disp 0xe0->0xf0 not the voted move (same 4711/5005 0xe0:4711,0xe8:106,); regenerated hash",
  "source": "review/pin-audit"
 },
 {
  "file": "src/native_note_kind.hpp",
  "contains": "0xb2ffbdff554301c2ull",
  "old": "0xb2ffbdff554301c2ull",
  "new": "0x898a2e20158b516full",
  "why": "FNV-1a pin 0x7905e0->0x7f6000 size 471: DOTO bytes hashed (semantic disp 0xd4->0xe4 not the voted move (mixed 485/745 0xd4:485,0xdc:202,0x); regenerated hash",
  "source": "review/pin-audit"
 }
]
```

Not mechanical (real change or no DOTO twin): door_event_admission profile 3 (add rcx,0x410 -> 0x428), 13 and 14 (imm 0xa1 -> 0xa6), 8 (poison);
door_animation 1 (sub rsp,0x20 -> 0x30) and 2 (imm 0x1b0 -> 0x1af); assassination 2 and 4; offered_choke 1.

## D. Pins that match DOTO but were reported "not-located" (ignore)

port-report `bytes` has 518 not-located entries: 458 are data tables (controller_glyph_atlas*.hpp, menu_patches*.hpp: Iggy movie patch data, not engine
code), 19 match DOTO, 4 are inline compares with no byte list, 37 belong to the mismatching groups above. The `{rva,size,hash}` pins of
native_prompt_entry.hpp (6 regions) already carry DOTO hashes and match. Overall 270 of 310 parsed byte lists are present in DOTO (the other 40 are
the rows above plus data/patch lists); every `{rva,[n,]{bytes}}` table entry was also compared at its RVA.

## E. Checked, nothing found

- Exe identity: dxgi_probe.cpp:18090 and game_camera.hpp:12 carry the DOTO sha256 as intended.
- native_interaction_aim.hpp:2825 scan hash, native_menu.hpp hex strings, native_pad_focus, native_world_trace, native_console, native_melee, native_held_object,
  native_gadget_*, native_manual_equip*, native_taa_history, native_options_page, dxgi_probe.cpp:1833-1835 / 2629-2679 / 18726-18731: bytes present where the
  generated RVA points.
- dxgi_probe.cpp:18915-18917 (`held_frame_globals`) uses DOTO addresses and the code there matches.
