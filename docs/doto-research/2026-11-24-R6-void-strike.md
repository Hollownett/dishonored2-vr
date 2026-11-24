# R6: Void Strike commit (DOTO, static RE, capstone, 2026-11-24)

All addresses are DOTO RVAs (image base 0x140000000). Follows R2 sections 2.4 and 3.4.

| Question | Answer | Evidence |
|---|---|---|
| How is `decl` reached from the slash object? | `decl = [slash+0x18]` (the state's declaration, same field every state class has; base-class code reads `[state+0x18]` too). | `0x8d6ac0 mov rax,[rcx+0x18]; movss xmm0,[rax+0xa0]`; perform `0x8d7fb0 mov r12,[rsi+0x18]` then `[r12+0xf0]` (`0x8d806d`) |
| Units of `slash+0x38` | float, **seconds**: the base state update `0x8be6a0` ends with `0x8be831..0x8be83a: movss xmm0,[rax]; addss xmm0,[rbx+0x38]; movss [rbx+0x38],xmm0`, where `[rax]` is the frame delta from `0x8e57e0` (the same time source `0x8d86c0` uses as a double). It accumulates per update while the state runs. | `0x8be6a0..0x8be83a`; the same field is compared with `[rsi+0x7c]` (a decl float) at `0x8be7b6` |
| Units of `decl+0xa0` | float, **seconds**, compared directly with `slash+0x38` as floats: `comiss xmm0,[rsp+0x30]` (xmm0=decl+0xa0, stack = copy of slash+0x38); `jbe` (decl <= hold) takes the commit branch. So commit when hold >= decl+0xa0. | `0x8d6ab6..0x8d6ae3` |
| Mana | `decl+0xf0` is an **int**: `0x8d8065 mov rcx,rax(=[slash+0x88]); call 0x7a81d0; 0x8d806d cmp eax,[r12+0xf0]; jge ok; else mov [rsi+0xdb],0`. Failing the check clears the owned byte, so the strike silently falls back to a heavy slash. | `0x8d7ed0` |
| Owned byte | `slash+0xdb` set at slash start (`0x8d56f8`); it is **only refreshed while a slash starts**, so before the first slash of a session the inactive object may still read 0. The mod treats 0 as "not owned" (keeps 340 ms) until a slash has run once. | `0x8d56b6..0x8d56f8`; R2 2.4 |
| Where is the Slash object | player state pool: `[[machine+0x98]+0x10+39*8]` (id 39, `+8` = id, `+0x10` = machine, `+0x20` = active byte), vtable `base+0x1d0b978`. The mod's `native_player_state_object()` (src/native_takedown.hpp, `machine+0x90` D2 literal rewritten to `0x98`) returns it. The object exists while inactive too (pool is built at machine start). | R2 section 2.4; DOTO-VR.md play21 |
| Hold ends how | the commit function `0x8d6ab0` runs on the attack release; `+0xda` (committed) is set from `+0xd9` (charging flag set by `0x8d8533`) only when the hold time reached `decl+0xa0`. | `0x8d6ab0` |

## Mod consequences

* `PowerSnapshot` gains `voidstrike_found/owned/commit_s/mana`, `slash_hold_s`; status `powers.void_strike`.
* Press length (`void_strike_press_ms`): owned and `commit_s` finite and > 0 -> `commit*1000+60` ms (<= 1500), else 340. The
  press hold is `AttackPress::start(..., hold_ms)`; its cap is `max(500, 45 + hold + 100)` for that press only.
* The press hold is counted from when RT goes high in the mod; the engine's `slash+0x38` accumulates from the slash state start
  (a few frames later), hence the +60 ms margin.
* Not verified live (no game run in this task): the real `decl+0xa0` and `decl+0xf0` values. Read `powers.void_strike` in the next sim.
