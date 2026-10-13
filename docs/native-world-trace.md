# Native world trace (sphere sweep against static world and bodies)

Pinned PE: Steam build 17576867, SHA256 `b671f8249422862bf557d00aab2f4bb671ed4515cc10ad8a8a9decad84516180`, image base `0x140000000`.
Code: `src/native_world_trace.hpp`, checks: `tests/native_world_trace_check.cpp` (layout asserts, request building, hit conversion, fake native caller). Nothing is hooked, installed or called by the DLL. Evidence is static (Ghidra decompile and disassembly) only. No live call has been made.

## Verdict on the known candidates

| RVA | What it really is | Usable as world trace? |
|---|---|---|
| `6c6760` | Selects from three sensor candidate lists (manager +0x20/+0x198/+0x310, each `+0x3f0`) via `fd7e80`; returns entity node, distance, normal from those lists. Not a physics query. | No (interaction targets only) |
| `95280` / `95550` / `95370` | Asynchronous job enqueue (kind 4, variant 2 / 1 / 2+overlap). Return a token, results are consumed later with `92630`. | No (async, one frame late) |
| `3b7b90` | Synchronous obstruction test: builds a sphere request + multi-hit collector and calls `c89bc0`. Returns only a bool. | No (binary), but it is the template for the filter and the player-origin usage |
| **`c89bc0`** | Synchronous physics-world shape cast wrapper: `world->+0x478 vtable+0x98(desc, rot, world+0x28, world+0x5c0, collector, 0)`. | **Yes** |
| `cc99c0` | Complete sphere sweep with closest-hit collector (used by particle/debris physics `cca020`, `ccb1f0`). Uses world default filter at `world+0x470` and queryId 0. | Alternative, see below |

`c89bc0` has 26 callers (AI line of sight `14061e500/e6c0/ede0`, `1403b2000`, `1403b3450`, `1403a2730`, `14044bd90`, `14083a510`, `14083c860`, `14086e410`, `140638800`, `140627f60`, hand volume job `96ac0`, interaction `3b7b90`, `6c1760`, `629590`, ...), so it is the engine-wide cast entry.

## ABI

`void __fastcall c89bc0(World* RCX, const NativeCastRequest* RDX, const float rot[12] R8, Collector* R9, uint64 stack0 = 0)`

* `World` = `*(*(base+0x39a5290) + 0x60410)` (same pointer `native_rigid_body.hpp`, `native_grab.hpp` and `native_interaction_aim.hpp:482` use). Validate the game object (RTTI `0x1c940a8`) as those do. Fields used: `+0x9d8` broadphase (request +0), `+0x28` body pool (stride `0xb0`), `+0x478` physics interface, `+0x5c0`.
* `rot` = three identity rows `(1,0,0,0),(0,1,0,0),(0,0,1,0)` (the constant at `.data 0x23ccdd0`, used by `cc99c0`).
* `NativeCastRequest` (0x70, align 16; same bytes the queued path copies in `8ffb0` to slot+0x90): `+0x00 *(world+0x9d8)`, `+0x08` filter pointer, `+0x10 u16 0xffff`, `+0x14 u32 queryId (0)`, `+0x20 u32 2`, `+0x28` shape pointer, `+0x30` origin vec4, `+0x40` delta vec4 (w = 1.0), `+0x50` inverse delta xyz (FLT_MAX sentinel for 0), `+0x5c u32 0x3f000000 | bit i if delta[i] >= 0`, `+0x60 float 0.001`. The +0x30..+0x5f block is exactly `InteractionRay`.
* Shape: `0xca6550(storage96, centre vec4*, radius)` builds an `hknpSphereShape` (vtable `0x1ec1e78`, radius at +0x28, centre at +0x50). `cc99c0` then sets the word at +0x10 to 0 (stack-owned); the adapter does the same. Use a zero centre and put the translation in the request origin only.
* Filter (stack object, vtable `0x1ad4b68`, size 0x28): `+0x10 u32 0x1ffff`, `+0x18 u8 4`, `+0x20 u32 layer_mask`, `+0x24 u32 required_bits`. Slot 4 (`0x996d0`) is the predicate: hit body with filter dword `info` collides iff `(layer_mask >> (info & 31)) & 1` and `((info >> 5) & required) == required`.
* Closest-hit collector (vtable `0x1ad5010`, 0xa0 bytes, align 16), initial state exactly as `cc99c0` builds it (see header comments for every field). Result: `+0x0c` hit count (nonzero = hit), `+0x30` position (world), `+0x40` normal, `+0x50` float fraction along delta (init FLT_MAX), `+0x78` hit body handle (low 24 bits index, `0xffffff` none), `+0x80` shape key. `cc99c0` converts to `out.distance = |delta| * fraction` at out+0x1c, confirming fraction in [0,1] of delta. Slot 3 (`ca53a0`) stores a sweep hit (closer fraction wins); slot 9 (`ca5460`) is the initial-overlap callback (stores, sets limit 0).
* Body to entity: `*( *( *(world+0x28) + idx*0xb0 + 0x90 ) + 8 ) + 0x10` (`18cd00` plus `3b7b90`), idx = `body & 0xffffff`.
* Multi-hit alternative (vtable `0x1ad2640`, 0x70-byte records inline, `0x8000000a` capacity word, sorted by `c85470`; record: +0x00 position, +0x10 normal, +0x20 fraction, +0x48 body, +0x50 shape key). It can grow through the engine allocator and needs a matching free, so the adapter deliberately uses the closest-hit collector with no allocation.

## Filter and masks

* `0xc43e` / required `0`: used by `3b7b90` (interaction obstruction from the camera/player origin toward a target). It must exclude the player's layer or every check from inside the player would be blocked (the loop only skips the target entity and its parent), so this is the best available "world and props but not the player" mask. INFERRED, not observed live.
* Other masks seen (AI/entity probes, meanings unknown, not used): `0x2f`/`0x22f` (`638800`), `0x2e`/`0x800` (`83c860`), `0x200`/`0x40` (`83a510`), `0x22b`/`0x40` (`44bd90`), `0x2b`/`0x40` (`3a2730`), per-entity `*(entity+0x1d8)`/`0x40` (`61e500`).
* Held weapon and own arms: not physics bodies of the world as far as known; there is no per-entity ignore in the closest-hit path.

## Where to call it

Same scope as `NativeInteractionAim::service_obstruction` (`native_interaction_aim.hpp` ~251-276), inside the Stage0 hook after `self->target(*scope)`: game thread, `active_`, `logic_()==2`, sensor typed `0x1b403b8` with identity unchanged, camera identity unchanged and not authored view, re-checked after the call. That proves a synchronous `3b7b90 -> c89bc0` is safe there (it is the same engine call). Publish per-hand `(from,to,radius,token)` requests from the render side, drain them in that scope, and write back `WorldTraceHit` with token and tick (one-frame latency, see `weapon-collision-integration.md`). Never call from the render or XR thread.

```
const auto world=*(*(base+0x39a5290)+0x60410); // validated as in native_interaction_aim.hpp
if(NativeWorldTrace::verify(base,read)) // once per session
auto hit=NativeWorldTrace::sphere_cast(base,read,
    reinterpret_cast<NativeWorldTrace::Cast>(base+0xc89bc0),
    reinterpret_cast<NativeWorldTrace::Construct>(base+0xca6550),world,from,to,radius);
if(hit) policy_hit=to_collision_hit(*hit); // WeaponCollision::update query
```
Pass radius 0.01-0.03 with `WeaponCollision` palm_radius reduced accordingly (otherwise the margin is applied twice).

## Alternative: `cc99c0` (not wrapped)

`bool-ish cc99c0(uint* RCX status (0 = hit, 1 = clear), World* RDX, const float4* R8 start, float XMM3 radius, const float4* [stack] end, Out* [stack] out, uint [stack] queryId)`, `Out`: +0x00 position, +0x10 normal, +0x1c distance, +0x20 shape key, +0x24 body. Simplest ABI, but uses the world default filter (`world+0x470`), so whether it ignores the player is unknown. Prefer the filtered `c89bc0` path.

## Pinned code bytes (checked by `NativeWorldTrace::verify`)

`c89bc0` (48 bytes), `996d0` (55), `ca53a0` (32), `ca5460` (32), `ca6550` (113, from `NativeHandVolume`), and the vtable qwords: filter `+0x20 == base+0x996d0`, collector `+0x18 == base+0xca53a0`, `+0x48 == base+0xca5460`.

## Not verified, must be checked live (read-only first)

1. That `c89bc0` with this hand-built request returns hits at all from the stage-0 scope (all layouts here come from decompilation; only the register and stack ABI of `cc99c0` was read from disassembly).
2. That `0xc43e` hits walls, floors, doors, props, and does not hit the player capsule or held weapon. If it does, find the player layer bit from a live body filter dword and clear it.
3. Units and scale (assumed metres, matches the rest of the mod), normal orientation (assumed toward the cast shape, the helper flips it against the sweep), and the meaning of `position` (assumed contact point on the hit surface).
4. Initial-overlap behaviour (fraction 0, normal may be junk; helper falls back to the reversed sweep and sets `initial_overlap`).
5. Whether skipping the filter and collector destructors (`a9e0d0`, `a9e0b0`) is harmless: they only reset vtables and notify the optional registry `DAT_143e97d60`; native does call them. Add the calls if a live run shows registry growth.
6. Cost and frequency budget (two sweeps per hand per frame), shape-cast of radius below about 5 mm is rejected by the adapter.
7. Thread safety against the physics worker threads outside the stage-0 window.
8. Headset behaviour, comfort and visual quality are unaddressed.

Evidence exports: re-run with `artifacts/claude/fullbody/ghidra-decomp.cmd <out> 140c89bc0 140cc99c0 1403b7b90 14061e500 14061e6c0 140ca53a0 140ca5460 1400996d0` (create the output directory first).
