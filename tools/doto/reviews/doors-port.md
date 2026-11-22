# Door feature (NativeDoorInteraction) port review

Static review, 2026-11-22. D2 `Dishonored2.exe` vs DOTO `Dishonored_DO.exe` (sha256 be73d979...). Nothing in `src\`, `tools\doto\offsets.json` or the game was changed; the rules below are ready to paste.
Scratch tools and the simulation live in this session's scratchpad folder `scratchpad\doors\`: `cmp.py` (instruction diff of two functions), `cls.py` (diff of every paired function in an address range), `off.py` / `vtscan.py` (a displacement across all aligned pairs / across a vtable), `ret.py` (call targets behind return-address constants), `mkrules*.py` (rule generator), `verify_sim.py`, and `sim\root` (a private copy of the repo in which `port.py` was run with these rules).

## Verdict

**Yes: the door feature can be revived in DOTO with data-only rules (42 `text` rules + 1 `rvas` rule); no `src\` change and no new game trait are needed.**

Every one of the 22 hooked functions (16 NativeDoorEventAdmission + 6 NativeDoorAnimation) has a DOTO twin with the same arguments and the same control flow. The only behavioural differences are constants the mod embeds (the door-use message id 0x96 -> 0x9b) and one extra entity-validity gate in the windblast handler that the hook does not depend on.

With the rules applied, `port.py` (run on a private copy of the repo with the current `offsets.json` plus these rules) produced a tree in which **every pin of `native_door_event_admission.hpp`, `native_door_animation.hpp` and `native_door_skeleton_content.hpp` verifies against the DOTO image**: 16 prologues (32 B) and 16 profile FNV hashes, 6 caller hashes, 6 animation prologues and hashes, 6 skeleton pins, 4 vtable-slot checks; every hook prefix ends on an instruction boundary with no rip-relative or branch instruction inside it (`verify_sim.py`: `ALL OK`). Besides the door files, the only generated files that changed are poison-id renumberings (hook 8 no longer consumes a poison id, so later `0xfff000N0` values shift; harmless).

Not provable statically (headset or live only): the items under "Risks", and gameplay or comfort of the hand-driven door. A wrong layout fails closed: the owner refuses to capture, `install` returns false, and the game's own door interaction stays (`optional_native_consumers`).

## How the door feature installs (what has to hold)

`native_interaction_aim.hpp:2761 install_door_interaction()` -> `NativeDoorInteraction::install` -> `NativeDoorAnimation::install` (6 MinHook detours; `verify()` = 32 B prologue + FNV of 6 regions) -> `NativeDoorEventAdmission::install` (16 detours; `verify()` = 16 prologues + FNV, plus 6 caller FNV). Per frame the owners read the door entity, component, animation graph, skeleton and clip content (`native_door_owner.hpp`, `native_door_clip_selection.hpp`, `native_door_clip_content.hpp`, `native_door_skeleton_content.hpp`, `native_door_ingress.hpp`). `physical_door_geometry/control`, `native_door_contact/progress/request/empty_gate` read no engine code beyond the offsets listed in section C (`request.hpp` uses `game+0x1e8970`, already translated).

## A. NativeDoorEventAdmission hooks (16)

Class key: same / relocated (only rel32 or rip displacement) / layout (struct displacement shift, same ABI and control flow) / imm only / real. "prefix" = bytes copied into the trampoline. All 16 DOTO functions were diffed instruction by instruction against D2.

| # | role | D2 rva | DOTO rva | prefix | D2 first 12 B | DOTO first 12 B | class | evidence |
|---|---|---|---|---|---|---|---|---|
| 0 | dispatch | `0x810000` | `0x8779b0` | 11 | `41 54 41 57 48 81 ec 88 00 00 00 4c` | `41 54 41 57 48 81 ec 88 00 00 00 4c` | layout | manager fields: dispatching flag +0xc948->+0xd050 (queue base 0xc8e8->0xcff0, +0x60), listener hash [+0x1d20/+0x1d30]->[+0x1ff0/+0x2000]; envelope {id u32 @0, payload @8, target u32 @0x10}, listener loops, calls to flush/post identical. 0x74 disp lines only |
| 1 | flush | `0x810430` | `0x877de0` | 6 | `40 55 56 57 41 56 41 57 48 83 ec 30` | `40 55 56 57 41 56 41 57 48 83 ec 30` | layout | spin lock lea rbx,[rcx+0xc94c]->[rcx+0xd060]; body (event drain loop, 32 B records) identical |
| 2 | reset | `0x811320` | `0x878d00` | 6 | `40 53 48 83 ec 20 33 d2 48 8b d9 e8` | `40 53 48 83 ec 20 33 d2 48 8b d9 e8` | relocated | instruction stream identical (rel32 only) |
| 3 | use | `0x74b6e0` | `0x7ae110` | 5 | `48 89 54 24 10 56 48 83 ec 50 48 8b` | `48 89 54 24 10 56 48 83 ec 50 48 8b` | layout | 13 displacement lines (see note 3): spawn id, entity origin/axes, entity flags, player state-machine/controller accessors; same control flow |
| 4 | retarget | `0x74d7a0` | `0x7b0200` | 5 | `48 89 5c 24 08 48 89 6c 24 18 56 57` | `48 89 5c 24 08 48 89 6c 24 18 56 57` | relocated | identical |
| 5 | lookup | `0x6df6d0` | `0x7409b0` | 5 | `48 89 5c 24 08 48 89 6c 24 10 48 89` | `48 89 5c 24 08 48 89 6c 24 10 48 89` | same | byte-identical (hash unchanged): lookup(machine,state) |
| 6 | activate | `0x74c5c0` | `0x7af020` | 5 | `48 8b c4 55 56 57 48 8d a8 28 ff ff` | `48 8b c4 55 56 57 48 8d a8 28 ff ff` | layout | only test byte [rax+0xd5],4 -> [rax+0xdd] (entity flags +8) |
| 7 | play | `0x53eef0` | `0x58ea90` | 8 | `48 83 ec 68 41 80 f8 ff 0f 8e 8d 00` | `48 83 ec 68 41 80 f8 ff 0f 8e 8d 00` | relocated | identical except rel32/rip; callers identical |
| 8 | windblast guard | `0x74ba10` | `0x7ae440` | 5 | `48 89 5c 24 18 48 89 54 24 10 55 56` | `48 89 5c 24 18 48 89 6c 24 20 56 48` | real (new twin) | D2 0x74ba10 (471 B) = DOTO 0x7ae440 (500 B); same arguments (rcx component, rdx envelope), different prologue after byte 5, one added entity-lookup gate (note 2). Hook only cancels and calls the original |
| 9 | script | `0x74dbb0` | `0x7b0610` | 5 | `44 88 44 24 18 53 57 48 83 ec 28 83` | `44 88 44 24 18 53 57 48 83 ec 28 83` | layout | or byte [rax+0xd4],4 -> [rax+0xdc] (x2) |
| 10 | serialize | `0x74e080` | `0x7b0ae0` | 6 | `40 57 48 83 ec 30 48 c7 44 24 20 fe` | `40 57 48 83 ec 30 48 c7 44 24 20 fe` | layout | lea rcx,[rax+0x20]->[rax+0x28] (entity member) |
| 11 | attach | `0x74c1f0` | `0x7aec40` | 6 | `48 8b c4 57 41 56 48 81 ec 88 00 00` | `48 8b c4 57 41 56 48 81 ec 88 00 00` | layout | entity +0xa8/+0x2e0/+0x2e8 and neighbours +8/+0x10; no ABI change |
| 12 | detach | `0x74c560` | `0x7aefc0` | 6 | `40 53 48 83 ec 40 48 8b d9 8b 89 dc` | `40 53 48 83 ec 40 48 8b d9 8b 89 dc` | relocated | identical |
| 13 | start | `0x74cd90` | `0x7af7f0` | 6 | `40 55 56 57 41 54 41 55 41 56 41 57` | `40 55 56 57 41 54 41 55 41 56 41 57` | imm only | message ids 0xa1/0x96/0x138/0x97/0xa2 -> 0xa6/0x9b/0x13e/0x9c/0xa7 (note 1), flag imm 0x10 -> 0x20 (note 4), model +0x1fe8 -> +0x1fd8, entity flags +0xd5 -> +0xdd; 658 aligned instructions |
| 14 | stop | `0x74d910` | `0x7b0370` | 5 | `48 89 5c 24 18 4c 89 64 24 20 55 41` | `48 89 5c 24 18 4c 89 64 24 20 55 41` | imm only | same message-id immediates (+5 / +6); the hook is a thin guard (cancel, call original) |
| 15 | destroy | `0x74a800` | `0x7ad220` | 5 | `48 89 4c 24 08 57 48 83 ec 30 48 c7` | `48 89 4c 24 08 57 48 83 ec 30 48 c7` | relocated | identical |

Notes on the semantic items:

1. **Message id (the one real source constant).** The mod posts `post(queue, 0x96, 4, door_spawn)` and later requires envelope message `0x96` and record header `0x96`. In D2 `start` (0x74cd90) registers the use wrapper thunk (r15 -> 0x74bd80, which calls the use hook at 0x74bddc) with `mov r8d,0x96; call 0x810810`; in DOTO the same code at 0x7afe36 registers thunk 0x7b1810 -> **0x7ae7d0** (the DOTO use wrapper, which calls 0x7ae110 at 0x7ae82c) with `mov r8d,0x9b`. Message ids before 0x138 moved by +5, from 0x138 on by +6 (0x138 -> 0x13e). Payload size 4 (player spawn id) and the record header layout (`post` 0x878ca0: u16 id at [rec], u16 size at [rec+2], u32 target at [rec+4], data at +8) are identical. Fixed by the three `0x96` text rules.
2. **Hook 8 (windblast guard) twin.** `translate.py` found none ("0 masked matches"): D2 0x74ba10 -> **DOTO 0x7ae440**, found by control-flow alignment (its caller `0x7ae5d7: call 0x7ae440` mirrors D2 `0x74bb8d: call 0x74ba10`; the neighbouring functions 0x74bbf0 / 0x74bd80 map to 0x7ae640 / 0x7ae7d0). First 5 bytes `48 89 5c 24 18` are identical, so `prefixes[8]=5` and `unwinds[8]={1,5,2,0,5,0x34,3,0}` stay valid; after that DOTO spills rbp (`48 89 6c 24 20 56`) instead of D2's `48 89 54 24 10 55 56 57`. Extra gate: after `mov [rsi+0x114],ecx` DOTO calls the entity lookup 0x73dc20 and skips the work (`je 0x7ae5f7`) when the sender entity is gone. The mod hook only runs `cancel_component(component)` and the original, so nothing else changes. Pinned size becomes the whole function, 500 B (3 pdata chunks), D2 was 471 B.
3. **idx 3 (use)** is the handler whose results the mod verifies (`+0x48==0`, `+0x4c==1`, `+0x114==player_spawn`, params at +0x90): no instruction was added, removed or reordered. The 13 differing lines are spawn id +0xa8 -> +0xb0, entity origin/axes +0x300..+0x314 -> +0x310..+0x324, entity flags +0xd4/+0xd5 -> +0xdc/+0xdd, and the player accessors `[playerStateMachine+0x2f8] -> +0x350` (the reviewed `arkCpntPlayerStateMachine` map in offsets.json `structs` has the same 0x2f8 -> 0x350), `[controller+0x5d0] -> +0x5e8` (still compared with 0xe) and `add rcx,0x410 -> 0x428`. The pin-audit labelled the `0x410 -> 0x428` line and idx 13/14 as real changes; they are layout and id immediates and need no change in the mod logic.
4. **idx 13/14** immediates: the message-id table (note 1). In `start` also `or rdx,0x10 -> or rdx,0x20` on a flag word of a sound-volume component (unrelated to the door interaction).
5. **Return-address constants** the hooks compare (`caller==base_+X`, `relative==X`) were each decoded in both images: the instruction before X is a `call` to the corresponding hooked function and sits at the same offset inside the same function. dispatch 0x91e42f (`call 0x877ba0` in the game tick, D2 0x8af58f), flush 0x8779cd (inside dispatch), use 0x7ae831, lookup callers 0x7ae191 / 0x7ae34e / 0x7ae361 (use), 0x7b027b (retarget), 0x7ace48 / 0x7ace58 (0x7ace20), 0x740a40 / 0x740a4e (0x740a20), 0x7ad951 / 0x7ad96e (0x7ad930), play callers 0x7af24b (activate) and 0x7b0355 (retarget); animation callers 0x74548c, 0x7465bb, 0x746280. The generator already translates all of them.

### Caller pins (6)

| pin | D2 rva (size) | DOTO rva | class | note |
|---|---|---|---|---|
| 0 | 0x8af583 (12) | 0x91e423 | layout | `lea rcx,[rsi+0x1e8968] -> +0x1e8970` (event manager in idGameLocal); call target 0x8101f0 -> 0x877ba0 |
| 1 | 0x8101f0 (12) | 0x877ba0 | layout | `lea rdx,[rcx+0xc8e8] -> [rcx+0xcff0]`, the queue the mod's `queue==manager+0xcff0` test expects; then `jmp` dispatch |
| 2 | 0x74bdd6 (11) | 0x7ae826 | relocated | `mov rdx,rdi; mov rcx,rbx; call use` |
| 3 | 0x6df740 (106) | 0x740a20 | same | hash unchanged |
| 4 | 0x74af00 (131) | 0x7ad930 | relocated | |
| 5 | 0x8112c0 (94) | 0x878ca0 | relocated | the `post()` the mod calls directly: same ABI (rcx queue, dx message id, r8d size, r9d target; returns rec+8) |

## B. NativeDoorAnimation hooks (6)

| # | role | D2 rva | DOTO rva | prefix | class | evidence |
|---|---|---|---|---|---|---|
| 0 | input | 0x6e1fe0 (118) | 0x743530 | 7 | same | hash unchanged |
| 1 | evaluate | 0x6eb070 (131 B, pin 160) | 0x74c6d0 | 5 | relocated | function identical; the 160 B pin runs into the next function (`sub rsp,0x20 -> 0x30` there), so only the pin hash changes (pin-audit called this "real") |
| 2 | retire | 0x6eafc0 (135 B, pin 176) | 0x74c620 | 5 | imm only | `mov r8d,0x1b0 -> 0x1af`, a source line number passed to the allocator free call; nothing else |
| 3 | parent | 0x6e4af0 (pin 1760) | 0x746060 | 7 | relocated | |
| 4 | insert | 0x6e3fa0 (448) | 0x745510 | 5 | relocated | |
| 5 | update | 0x6e4850 (469) | 0x745dc0 | 5 | relocated | |

Arguments and return values are unchanged for all six (instruction-by-instruction identical). The graph machinery the mod reads is unchanged: `arkAnimTreeNode_AnimBasic` vtable 0x1cd0088 (RTTI match), node +0x590 (mode) and +0x5b0 (input key), context +0x9b0 list header (data at +0x9c0, packed `>>24==5`, capacity 0x440), +0x9a0, +0x988, +0xa78 clip list, record +0x970 next, schema hash map +0x58 buckets / +0x68..+0x80 mask, `schema+0xc0` arena size. They appear at identical displacements in the aligned D2/DOTO functions (the context constructor 0x6e38f0 -> 0x744e40 and the input-binding lookup 0x6e2be0 -> 0x744130 are hash-identical; 153 anim-graph function pairs were diffed).

## C. Struct offsets, vtables, globals (everything the door code reads)

| field | D2 | DOTO | class | evidence |
|---|---|---|---|---|
| `idGameLocal*` global / vtable | base+0x39a5290 / 0x1c940a8 | 0x3a72280 / 0x1d1a678 | relocated | RTTI `idGameLocal`; used by all ported modules |
| world | game+0x60410 | game+0x60410 | same | 89 aligned accesses pair 0x60410 -> 0x60410 |
| entity registry | game+0x3130b0 | game+0x327770 | translated | generator (voted); other ported modules use it |
| event manager | game+0x1e8968 | game+0x1e8970 | layout | accessor 0x810240 -> 0x877bf0 (`add rax,0x1e8968 -> 0x1e8970`) |
| event queue | manager+0xc8e8 | manager+0xcff0 | layout | dispatch caller 0x8101f0 -> 0x877ba0 |
| entity spawn id | +0xa8 | +0xb0 | layout | entity map (weapons review); **missing for the identifiers `door` and `part`**, fixed by rules |
| entity components list | +0x268 | +0x270 | layout | entity map |
| entity model / flags / transform | +0xf0 / +0xd4 / +0x300 | +0xf8 / +0xdc / +0x310 | layout | entity map; `part+0xf0` and `part+0x300` need rules (identifier `part`) |
| `arkDoor` vtable | 0x1cb7c68 | 0x1ce2888 | RTTI | `.?AVarkDoor@@` |
| door -> `arkCpntOpenable*` | +0x370 | **+0x380** | layout (+0x10) | D2 783360 (`GetComponent` result stored at [door+0x370]) = DOTO 7e74c0 (`[door+0x380]`); the arkDoor constructor 0x8de2a0 -> 0x7e6570 initialises it right after the +0x10-shifted dword at +0x368 -> +0x378 |
| door locker id (`0x3fff0010` = free) | +0x378 | **+0x388** | layout (+0x10) | D2 7833a0 / 783430 = DOTO 7e7500 / 7e7590 (`add rcx,0x378 -> 0x388`, `[door+0x370] -> [door+0x380]`); the DOTO constructor stores 0x3fff0010 at +0x388 |
| `arkCpntOpenable` vtable / Opened state / decl | 0x1c569c8 / 0x1c56988 / 0x1c56af8 | 0x1cdb248 / 0x1cdb208 / 0x1cdb378 | RTTI | opened-state getter at slot +0x28 = 0x11701b0 (`b8 02 00 00 00 c3`, read from the DOTO vtable) |
| component fields: +0x8 decl, +0x10 door, +0x18 identity, +0x20 usable, +0x40 machine, +0x48/+0x4c state ids, +0x90 params, +0xb8/+0xc0/+0xc8/+0xd0 Opened state (vtable / machine / sense / owner), +0xdc part spawn, +0xf0 linked, +0xf8 animation, +0x114 instigator, +0x118 blocked, +0x1e0..+0x1e7 clips, +0x1e8 flags | | same | same | the whole component class (D2 0x74a800..0x74e3b2, 81 of 82 pdata chunks paired, `cls.py`) has no component-field displacement difference; its constructor 0x74a4a0 -> 0x7acec0 is instruction-identical |
| `arkCpntAnimBase` vtable; +0x10 owner, +0x338 container, +0x390 binding | 0x1b7f408 | 0x1c8ef48 | RTTI; offsets same | constructor 0x53b090 -> 0x58abc0 hash-identical; container constructor 0x53b020 -> 0x58ab50 identical (+0 holder, +8 schema, +0x20 events, +0x38 time) |
| `arkSkinnedRenderModel` vtable; model -> mesh -> skeleton | 0x1b0d628; +0x1fe8; mesh+0x50 | 0x1c146e8; **+0x1fd8**; mesh+0x50 | layout | body-rig review; skeleton pin 0x25da50 -> 0x263e00 (`mov rax,[rcx+0x1fd8]; mov rcx,[rax+0x50]`) matches the DOTO bytes |
| `arkEdgeSkel` vtable | 0x1b0bd08 | 0x1c12d98 | RTTI | resident data +0x158, size +0x160, joint-name list +0x70 (base constructor 0x255960 -> 0x25b100 identical): unchanged. **Vtable slots +0x80 / +0x88 (resident getters 0x24f1c0 / 0x24f240) are at +0x88 / +0x90 in DOTO** (0x254880 / 0x254900; DOTO inserted a slot at +0x70). The mod only compares the slot values (two text rules) |
| `arkSkinnedRenderModel` vtable +0x20 stub | 0x25df10 | **0x18e6f0** | rva | `83 fa 02 0f 94 c0 c3` identical; slot +0x20 of 0x1c146e8 holds 0x18e6f0 |
| `idAnimationLocal` name manager | global 0x22995c0 (object +0x10, vtable 0x1b093c8) | 0x235a570 (0x235a580, 0x1c10438) | translated | slot +0x38 (0x23cb00 -> 0x2424f0) byte-identical: stride 0x48, name pointer +0x10, count `[this+0x10]&0xffffff`, list data `[this+8]` |
| `arkEdgeAnim` vtable; +0x30 skeleton, +0x38 duration, +0x48 buffer, +0x50 length | 0x1b0ba38 | 0x1c12ac8 | RTTI; offsets same | 0x24adf0 -> 0x250440 aligned with the same displacements |
| `arkCpntPlayerSensorInteraction` vtable | 0x1b403b8 | 0x1c4dc48 | RTTI | its +0x10/+0x38/+0x40 are shared with the already-ported interaction code |

Asset data the door code compares byte for byte (`native_door_clip_content.hpp` 2 x 368 B, `native_door_skeleton_content.hpp` 448 B resident and 6 joint names): extracted read-only with the `tools\extract_arm_assets.py` readers from both installs. `generated/skeleton/models/interactive/doors/doors_structure/doors.edgeskl` is **byte-identical** (1056 B, sha1 d1e14000c4...) and all 8 player door clips (`player_open/close/windblast/changelevel_cw/ccw.edgeanim`, 31 frames, 432..528 B) are **byte-identical** in D2 and DOTO; the mod's `ccw_bytes` equals the first 368 B of `player_open_ccw.edgeanim` apart from 3 runtime-patched bytes ([84], [85], [88]). The door leaf model differs in DOTO (`doors.bmd6model` size changed; new `heavy_doors` skeleton and decls), so heavy doors fail the skeleton-content check and keep the game's interaction.

## D. Rules to paste into tools\doto\offsets.json

Merge into the existing `"text"` list (42 entries, all `"source": "review/doors-port"`: 25 FNV-1a hash rules, hook-8 prologue and size, three message-id rules, four event-admission entity offsets, three owner offsets, one ingress rule, four skeleton rules):

```json
[
 {
  "file": "src/native_door_event_admission.hpp",
  "contains": "0x10c43863d84872b9ull",
  "old": "0x10c43863d84872b9ull",
  "new": "0xe2a05f7706dfd0fbull",
  "why": "profile pin 0 (dispatch): D2 0x810000+489 -> DOTO 0x8779b0+489, layout; hash = FNV-1a of the DOTO bytes",
  "source": "review/doors-port"
 },
 {
  "file": "src/native_door_event_admission.hpp",
  "contains": "0xc134621a70d6547dull",
  "old": "0xc134621a70d6547dull",
  "new": "0x3464e5c24b3dc868ull",
  "why": "profile pin 1 (flush): D2 0x810430+278 -> DOTO 0x877de0+278, layout; hash = FNV-1a of the DOTO bytes",
  "source": "review/doors-port"
 },
 {
  "file": "src/native_door_event_admission.hpp",
  "contains": "0xe008b89df8694e5ull",
  "old": "0xe008b89df8694e5ull",
  "new": "0xaea79e64facf7269ull",
  "why": "profile pin 2 (reset): D2 0x811320+25 -> DOTO 0x878d00+25, relocated; hash = FNV-1a of the DOTO bytes",
  "source": "review/doors-port"
 },
 {
  "file": "src/native_door_event_admission.hpp",
  "contains": "0x21f65997fd2dfbf7ull",
  "old": "0x21f65997fd2dfbf7ull",
  "new": "0x9bb9c1eb0685e9fcull",
  "why": "profile pin 3 (use): D2 0x74b6e0+804 -> DOTO 0x7ae110+804, layout; hash = FNV-1a of the DOTO bytes",
  "source": "review/doors-port"
 },
 {
  "file": "src/native_door_event_admission.hpp",
  "contains": "0x333dd8e999eeadf4ull",
  "old": "0x333dd8e999eeadf4ull",
  "new": "0x6146b64a5f6e1e65ull",
  "why": "profile pin 4 (retarget): D2 0x74d7a0+367 -> DOTO 0x7b0200+367, relocated; hash = FNV-1a of the DOTO bytes",
  "source": "review/doors-port"
 },
 {
  "file": "src/native_door_event_admission.hpp",
  "contains": "0xae40fe40d660cfd6ull",
  "old": "0xae40fe40d660cfd6ull",
  "new": "0x5cdb3d025720223bull",
  "why": "profile pin 6 (activate): D2 0x74c5c0+1092 -> DOTO 0x7af020+1092, layout; hash = FNV-1a of the DOTO bytes",
  "source": "review/doors-port"
 },
 {
  "file": "src/native_door_event_admission.hpp",
  "contains": "0xe074b24d83db4c61ull",
  "old": "0xe074b24d83db4c61ull",
  "new": "0x53c048ab27821272ull",
  "why": "profile pin 7 (play): D2 0x53eef0+162 -> DOTO 0x58ea90+162, relocated; hash = FNV-1a of the DOTO bytes",
  "source": "review/doors-port"
 },
 {
  "file": "src/native_door_event_admission.hpp",
  "contains": "0x7b7ebc2f64722791ull",
  "old": "0x7b7ebc2f64722791ull",
  "new": "0xd98158be09bc3485ull",
  "why": "profile pin 8 (windblast guard): D2 0x74ba10+471 -> DOTO 0x7ae440+500, real (new twin); hash = FNV-1a of the DOTO bytes",
  "source": "review/doors-port"
 },
 {
  "file": "src/native_door_event_admission.hpp",
  "contains": "0x9af0b80ec9778783ull",
  "old": "0x9af0b80ec9778783ull",
  "new": "0x656e95a52e68b538ull",
  "why": "profile pin 9 (script): D2 0x74dbb0+402 -> DOTO 0x7b0610+402, layout; hash = FNV-1a of the DOTO bytes",
  "source": "review/doors-port"
 },
 {
  "file": "src/native_door_event_admission.hpp",
  "contains": "0x1d51e3c464ab3ec3ull",
  "old": "0x1d51e3c464ab3ec3ull",
  "new": "0x238428ee20579f8aull",
  "why": "profile pin 10 (serialize): D2 0x74e080+380 -> DOTO 0x7b0ae0+380, layout; hash = FNV-1a of the DOTO bytes",
  "source": "review/doors-port"
 },
 {
  "file": "src/native_door_event_admission.hpp",
  "contains": "0xe784f56538d7900eull",
  "old": "0xe784f56538d7900eull",
  "new": "0x19e8c02338df9086ull",
  "why": "profile pin 11 (attach): D2 0x74c1f0+734 -> DOTO 0x7aec40+734, layout; hash = FNV-1a of the DOTO bytes",
  "source": "review/doors-port"
 },
 {
  "file": "src/native_door_event_admission.hpp",
  "contains": "0x5143bed2cc687e33ull",
  "old": "0x5143bed2cc687e33ull",
  "new": "0xec100195a57bd78eull",
  "why": "profile pin 12 (detach): D2 0x74c560+93 -> DOTO 0x7aefc0+93, relocated; hash = FNV-1a of the DOTO bytes",
  "source": "review/doors-port"
 },
 {
  "file": "src/native_door_event_admission.hpp",
  "contains": "0xa0719f85e11e0d86ull",
  "old": "0xa0719f85e11e0d86ull",
  "new": "0xc59bc3b06114b5e3ull",
  "why": "profile pin 13 (start): D2 0x74cd90+2571 -> DOTO 0x7af7f0+2571, disp/imm; hash = FNV-1a of the DOTO bytes",
  "source": "review/doors-port"
 },
 {
  "file": "src/native_door_event_admission.hpp",
  "contains": "0x154a50193b7c7dddull",
  "old": "0x154a50193b7c7dddull",
  "new": "0xf6b3f446c16977c3ull",
  "why": "profile pin 14 (stop): D2 0x74d910+669 -> DOTO 0x7b0370+669, disp/imm; hash = FNV-1a of the DOTO bytes",
  "source": "review/doors-port"
 },
 {
  "file": "src/native_door_event_admission.hpp",
  "contains": "0x13f891989dc5e160ull",
  "old": "0x13f891989dc5e160ull",
  "new": "0x49906ea8943855f1ull",
  "why": "profile pin 15 (destroy): D2 0x74a800+188 -> DOTO 0x7ad220+188, relocated; hash = FNV-1a of the DOTO bytes",
  "source": "review/doors-port"
 },
 {
  "file": "src/native_door_event_admission.hpp",
  "contains": "0x894d758fbe8eceb1ull",
  "old": "0x894d758fbe8eceb1ull",
  "new": "0x9178c9254deae68bull",
  "why": "caller pin 0: D2 0x8af583+12 -> DOTO 0x91e423; FNV-1a of the DOTO bytes (relocated rel32 / disp)",
  "source": "review/doors-port"
 },
 {
  "file": "src/native_door_event_admission.hpp",
  "contains": "0x8df1059b0c6eeb28ull",
  "old": "0x8df1059b0c6eeb28ull",
  "new": "0x1e5ca2d148187ffull",
  "why": "caller pin 1: D2 0x8101f0+12 -> DOTO 0x877ba0; FNV-1a of the DOTO bytes (relocated rel32 / disp)",
  "source": "review/doors-port"
 },
 {
  "file": "src/native_door_event_admission.hpp",
  "contains": "0x5b5f1d96e5165acaull",
  "old": "0x5b5f1d96e5165acaull",
  "new": "0x87571c85fba8eeeaull",
  "why": "caller pin 2: D2 0x74bdd6+11 -> DOTO 0x7ae826; FNV-1a of the DOTO bytes (relocated rel32 / disp)",
  "source": "review/doors-port"
 },
 {
  "file": "src/native_door_event_admission.hpp",
  "contains": "0x492dc3d4dfe23e65ull",
  "old": "0x492dc3d4dfe23e65ull",
  "new": "0xd294e51a83a9dbf7ull",
  "why": "caller pin 4: D2 0x74af00+131 -> DOTO 0x7ad930; FNV-1a of the DOTO bytes (relocated rel32 / disp)",
  "source": "review/doors-port"
 },
 {
  "file": "src/native_door_event_admission.hpp",
  "contains": "0x5fd62a1ca579833full",
  "old": "0x5fd62a1ca579833full",
  "new": "0xab4d10c06a9587dcull",
  "why": "caller pin 5: D2 0x8112c0+94 -> DOTO 0x878ca0; FNV-1a of the DOTO bytes (relocated rel32 / disp)",
  "source": "review/doors-port"
 },
 {
  "file": "src/native_door_animation.hpp",
  "contains": "0x939f4de46b651986ull",
  "old": "0x939f4de46b651986ull",
  "new": "0xcd2006a999d37ecull",
  "why": "animation pin 1: D2 0x6eb070+160 -> DOTO 0x74c6d0; FNV-1a of the DOTO bytes",
  "source": "review/doors-port"
 },
 {
  "file": "src/native_door_animation.hpp",
  "contains": "0xf420ee6ca94e580cull",
  "old": "0xf420ee6ca94e580cull",
  "new": "0x4eafd6af00105cd2ull",
  "why": "animation pin 2: D2 0x6eafc0+176 -> DOTO 0x74c620; FNV-1a of the DOTO bytes",
  "source": "review/doors-port"
 },
 {
  "file": "src/native_door_animation.hpp",
  "contains": "0xf11c59744626d909ull",
  "old": "0xf11c59744626d909ull",
  "new": "0x92bd36116cd539d0ull",
  "why": "animation pin 3: D2 0x6e4af0+1760 -> DOTO 0x746060; FNV-1a of the DOTO bytes",
  "source": "review/doors-port"
 },
 {
  "file": "src/native_door_animation.hpp",
  "contains": "0x6c8c6360081cb7c7ull",
  "old": "0x6c8c6360081cb7c7ull",
  "new": "0x5680492e15ec45e8ull",
  "why": "animation pin 4: D2 0x6e3fa0+448 -> DOTO 0x745510; FNV-1a of the DOTO bytes",
  "source": "review/doors-port"
 },
 {
  "file": "src/native_door_animation.hpp",
  "contains": "0x017ed628b7d5ba7aull",
  "old": "0x017ed628b7d5ba7aull",
  "new": "0x681e9da600e07142ull",
  "why": "animation pin 5: D2 0x6e4850+469 -> DOTO 0x745dc0; FNV-1a of the DOTO bytes",
  "source": "review/doors-port"
 },
 {
  "file": "src/native_door_event_admission.hpp",
  "contains": "std::array<unsigned char,32>,16> prologues",
  "old": "{0x48,0x89,0x5c,0x24,0x18,0x48,0x89,0x54,0x24,0x10,0x55,0x56,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0x41,0x8,0x48,0x8b,0xfa,0x48,0x8b,0xf1,0xf,0xb6,0x98,0xf0,0x1}",
  "new": "{0x48,0x89,0x5c,0x24,0x18,0x48,0x89,0x6c,0x24,0x20,0x56,0x48,0x83,0xec,0x20,0x48,0x8b,0x41,0x8,0x48,0x8b,0xea,0x48,0x8b,0xf1,0xf,0xb6,0x98,0xf0,0x1,0x0,0x0}",
  "why": "hook 8 (windblast guard) prologue: DOTO fn 0x7ae440 (D2 0x74ba10) starts mov [rsp+0x18],rbx / mov [rsp+0x20],rbp / push rsi (D2: ... mov [rsp+0x10],rdx / push rbp,rsi,rdi). First 5 bytes (the hook prefix) are identical, so prefixes[8]=5 and unwinds[8] stay valid.",
  "source": "review/doors-port"
 },
 {
  "file": "src/native_door_event_admission.hpp",
  "contains": "std::array<unsigned,16> profile_sizes",
  "old": "162,471,402",
  "new": "162,500,402",
  "why": "profile 8 covers the whole DOTO function 0x7ae440..0x7ae634 (3 pdata chunks, 500 B; D2 was 471 B)",
  "source": "review/doors-port"
 },
 {
  "file": "src/native_door_event_admission.hpp",
  "contains": "post(frame->queue,0x96,4,",
  "old": "post(frame->queue,0x96,4,",
  "new": "post(frame->queue,0x9b,4,",
  "why": "ARK_MSG_USE message id: D2 0x96 -> DOTO 0x9b (start fn 0x74cd90->0x7af7f0 registers the use wrapper thunk, which calls the use hook, under r8d=0x96 / 0x9b; ids before 0x138 moved +5)",
  "source": "review/doors-port"
 },
 {
  "file": "src/native_door_event_admission.hpp",
  "contains": "message==0x96 && get(envelope+8,payload)",
  "old": "message==0x96",
  "new": "message==0x9b",
  "why": "envelope message id of the use event (see above)",
  "source": "review/doors-port"
 },
 {
  "file": "src/native_door_event_admission.hpp",
  "contains": "header_message==0x96",
  "old": "header_message==0x96",
  "new": "header_message==0x9b",
  "why": "queued record header message id (post() writes the u16 id at [rec])",
  "source": "review/doors-port"
 },
 {
  "file": "src/native_door_event_admission.hpp",
  "contains": "equals(request.door+0xa8,request.door_spawn)",
  "old": "request.door+0xa8,",
  "new": "request.door+0xb0,",
  "why": "idEntity spawn id +0xa8 -> +0xb0 (weapons review); the generator's entity rule does not know the identifier 'door'",
  "source": "review/doors-port"
 },
 {
  "file": "src/native_door_event_admission.hpp",
  "contains": "equals(request.door+0x370,request.component)",
  "old": "request.door+0x370,",
  "new": "request.door+0x380,",
  "why": "arkDoor component pointer: D2 783360 (GetComponent -> [door+0x370]) = DOTO 7e74c0 ([door+0x380]); ctor 8de2a0 -> 7e6570",
  "source": "review/doors-port"
 },
 {
  "file": "src/native_door_event_admission.hpp",
  "contains": "equals(operation.part+0xa8,request.part_spawn)",
  "old": "operation.part+0xa8,",
  "new": "operation.part+0xb0,",
  "why": "idEntity spawn id of the animated part",
  "source": "review/doors-port"
 },
 {
  "file": "src/native_door_event_admission.hpp",
  "contains": "equals(request.door+0x378,std::uint32_t{0x3fff0010})",
  "old": "request.door+0x378,",
  "new": "request.door+0x388,",
  "why": "arkDoor m_lockerId: D2 7833a0/783430 [door+0x378] = DOTO 7e7500/7e7590 [door+0x388]; ctor 7e6570 stores 0x3fff0010 at +0x388",
  "source": "review/doors-port"
 },
 {
  "file": "src/native_door_owner.hpp",
  "contains": "audit.equals(result.part+0xa8,result.part_spawn)",
  "old": "result.part+0xa8,",
  "new": "result.part+0xb0,",
  "why": "idEntity spawn id of the animated part",
  "source": "review/doors-port"
 },
 {
  "file": "src/native_door_owner.hpp",
  "contains": "audit.get(result.door+0x370,result.component)",
  "old": "result.door+0x370,",
  "new": "result.door+0x380,",
  "why": "arkDoor component pointer (see event admission)",
  "source": "review/doors-port"
 },
 {
  "file": "src/native_door_owner.hpp",
  "contains": "snapshot.door+0x378,result.locker",
  "old": "snapshot.door+0x378,",
  "new": "snapshot.door+0x388,",
  "why": "arkDoor m_lockerId (see event admission)",
  "source": "review/doors-port"
 },
 {
  "file": "src/native_door_ingress.hpp",
  "contains": "owner->part+0x300",
  "old": "owner->part+0x300",
  "new": "owner->part+0x310",
  "why": "idEntity world transform (origin +0x300, axes +0x30c) -> +0x310 / +0x31c: 12 floats read as one block",
  "source": "review/doors-port"
 },
 {
  "file": "src/native_door_skeleton_content.hpp",
  "contains": "reader.get(owner.part+0xf0,result.model)",
  "old": "owner.part+0xf0,",
  "new": "owner.part+0xf8,",
  "why": "idEntity render model +0xf0 -> +0xf8 (entity map)",
  "source": "review/doors-port"
 },
 {
  "file": "src/native_door_skeleton_content.hpp",
  "contains": "reader.equals(base+0x1b0bd08+0x80,base+0x24f1c0)",
  "old": "base+0x1c12d98+0x80,base+0x254880",
  "new": "base+0x1c12d98+0x88,base+0x254880",
  "why": "arkEdgeSkel vtable: DOTO inserted a slot at +0x70, so the resident-data getter (D2 slot +0x80, 0x24f1c0 -> DOTO 0x254880) is at +0x88",
  "source": "review/doors-port"
 },
 {
  "file": "src/native_door_skeleton_content.hpp",
  "contains": "reader.equals(base+0x1b0bd08+0x88,base+0x24f240)",
  "old": "base+0x1c12d98+0x88,base+0x254900",
  "new": "base+0x1c12d98+0x90,base+0x254900",
  "why": "same shift: D2 slot +0x88 (0x24f240 -> DOTO 0x254900) is at +0x90",
  "source": "review/doors-port"
 },
 {
  "file": "src/native_door_skeleton_content.hpp",
  "contains": "{0x25df10,{0x83,0xfa,0x02",
  "old": "{0x25df10,{0x83,0xfa,0x02",
  "new": "{0x18e6f0,{0x83,0xfa,0x02",
  "why": "skinned-render-model vtable +0x20 stub (cmp edx,2; sete al; ret): D2 0x25df10 == DOTO 0x18e6f0, bytes identical. The generator promotes the other five table rvas but leaves this one (its rva rule only fixed the equals() line)",
  "source": "review/doors-port"
 }
]
```

Merge into the existing `"rvas"` list (global rule, no `"file"`):

```json
[
 {
  "from": "0x74ba10",
  "to": "0x7ae440",
  "why": "hook 8 of NativeDoorEventAdmission (door windblast guard): D2 0x74ba10 (471 B) = DOTO 0x7ae440 (500 B, 3 pdata chunks); same (component, envelope) arguments, control flow aligned apart from one added entity-lookup gate. The generator found no twin (0 masked matches) because the prologue and the length differ.",
  "source": "review/doors-port"
 }
]
```

Already present in offsets.json, do not add again: `{"from":"0x25df10","to":"0x18e6f0"}` (pin-audit rule). It fixes the `equals()` line only; the `{0x25df10,{...},7}` table entry is fixed by the skeleton text rule above (the generator promotes the other five table rvas but not this one).

These rules **supersede** the door rows of pin-audit sections B and C (`native_door_event_admission.hpp`, `native_door_animation.hpp`; same hash values for profile idx 0,1,2,4,6,7,9,10,11,12,15, callers 0,1,2,4,5, animation idx 3,4,5). Applying both only prints "text rule matched nothing" warnings for the second copy.
After the merge run `python tools\doto\port.py`; expected: no `WARNING` for door files, and all pins match the DOTO image (the `verify_sim.py` check of the scratch folder, `ALL OK`).

## E. Does `src\` need a change?

No. Everything is applied by `text` / `rvas` rules; `NativeDoorInteraction::install` already returns false (and `install_door_interaction` already falls back) if any pin stops matching a future DOTO build, because `game_traits::optional_native_consumers` is true for DOTO. If the project prefers no literal rewriting for the message id, the clean alternative is a trait in `src\game_profile.hpp` (`d2vr::game_traits::door_use_message`: 0x96 in the D2 block, 0x9b in the `D2VR_GAME_DOTO` block) used at the three `0x96` sites of `native_door_event_admission.hpp` (lines 324, 332, 334); the rules route is equivalent and keeps D2 byte-identical.

## F. Risks and what was not proven

1. **Unproven field reads** (all fail closed): `arkDeclCpntAnimBase` +0x68 (schema) and +0x70 (key), read through `animation+8` (the code that fills them was not located in either image; the mod cross-checks `container+8 == decl+0x68`); `arkCpntPlayerSensorInteraction` +0x10/+0x38/+0x40 (shared with other ported code); the AnimBasic context size 0xb78 (only a lower bound is checked). A wrong value makes `NativeDoorOwner::capture` return nothing: no hand door contact, and the game's door stays in charge.
2. **Heavy doors** and other new DOTO door classes are not covered (the skeleton-content check fails by design).
3. **Locker semantics**: `m_lockerId == 0x3fff0010` as "free" was transferred by layout (+0x10) and by the DOTO constructor storing the same sentinel. A live read of one closed door (`tools\entities.py`, read-only) should show 0x3fff0010 at door+0x388.
4. **Hook density**: 22 detours including `play` (0x58ea90) and `lookup` (0x7409b0), both hot. They were harmless in D2 (in-flight guards), but headset performance and comfort in DOTO are unmeasured.
5. The poison-id renumbering touches the generated `native_fire_seam_trace.hpp`, `native_gameplay_aim_trace.hpp`, `native_slash_scene_rate.hpp`, `secondary_view_guard.hpp` (constants only; those features are already off).
6. Static analysis does not establish visual quality, comfort or complete door behaviour (peeking, locked, double doors, NPC contention). A headset session on a few doors (closed -> open -> close -> locked) is still required.
