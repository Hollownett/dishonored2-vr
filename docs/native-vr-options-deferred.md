# Native VR options page: running the page work deferred, like the engine does

This is a static analysis of Steam 17576867 (`Dishonored2.exe`). The game was not launched or attached. RVAs are
relative to 0x140000000. The Ghidra dumps are in `artifacts/claude/iggydefer/idf1..16.txt`, and older dumps are in
`artifacts/claude/vroptions/`.
**VERIFIED** means read from disassembly or decompilation. **INFERRED** means reasoned but not proven statically.

This document follows on from `docs/native-vr-options-page.md` and corrects its risk 1.

## Why the live write failed (VERIFIED)

The design doc said the value setters have no re-entry guard. That is wrong. The guard lives in one shared helper:

- `0x100ce90` (Iggy enter) runs `CAS(Iggy+0x3584, 0 -> 1)` unless the creation-time flag `Iggy+0x93` is set.
  On failure it reports 0x192 through the string "Attempted to re-enter Iggy...".
- `0x100cf10` is the matching leave (`CAS 1 -> 0`).
- Callers of `0x100ce90`:
  - `0x100bc30`: the core of every `IggyValueSet*RS`;
  - `0x100ba70`: the core of the `Get*RS` functions (`GetS32RS` 0x1009bf0 goes through it);
  - `0x1009960`: `IggyPlayerCreateFastNameUTF8`;
  - `0x1009f00`: `RefCreateArray`;
  - `0x100a070`: `RefCreateEmptyObject`;
  - `0x100a210`: `RefFromPath`.
- `IggyPlayerTickRS` (0x100ccd0, which names itself "IggyPlayerTickRS") and `IggyPlayerCallMethodRS` (0x100c4c0)
  hold the same lock while AS3 runs.

So inside `ExternalInterface.call` (the hub), these calls all fail: `CreateFastNameUTF8` returns 0, and every Set,
Get and CallMethod fails. Only path building is unguarded: `MakeNameRef` 0x1009de0 and `MakeArrayRef` 0x1009da0.
**All Iggy access, including reading values back for `VR_SETTING_CHANGED`, must move to a deferred handler.**

## Q1. 0x2cd20 = arkIggyFile::RegisterExternalInterfaceHandler (VERIFIED)

`int __fastcall Register(arkIggyFile* file, const char* name, DeferredFn fn, void* owner)` is a thin wrapper:

1. It calls `0x351b0(main = *(arkIggyMain**)0x142274270, name, fn, owner, file)`.
2. It appends the returned id to `file+0x1790` (`idList<int>`) under the `0x1548e0` lock. Duplicates are not
   appended.
3. It returns the id.

The name and the key:
- `name` is a NUL-terminated `const char*`. Only its hash is used, and the pointer is **not stored**.
- The hash is computed at 0x1431a0: `h = 0; for each byte b (unsigned): h = h*31 + b`.
- The key is `id = h ^ (uint32)*(void**)(file+0x1608)`, where `file+0x1608` holds the `Iggy*`. With `file == NULL`
  the key is `h` alone; the engine uses that form for its two global handlers (0x34ba0).

The table:
- There is one global hash table inside arkIggyMain: lock at `+0x16cbc0` (taken with 0x158290 and released with
  0x1582e0), buckets at `+0x16cbc8`, count at `+0x16cbd4`, mask at `+0x16cbd8`.
- Entries are 0x28 bytes: `{int key; arkIggyFile* file; void* owner; fn; next}`.
- The table is global but keyed per player (Iggy) and name.

Behaviour:
- **Same name, same file:** the entry is replaced in place (file, owner and fn are overwritten) and no duplicate id
  is added.
- **Collisions:** a hash collision, or reusing an engine name, **replaces the engine's handler**. Never register
  `OnBackFromSettingsList`, `OnRestoreSettings` or any other engine-owned name on a game movie.
- **Unregister:**
  - `0x2cf50(file, id)` removes the id from the table (`0x35400(main, id)`) and from `file+0x1790`.
  - `0x35400(main, key)` removes the table entry only; it is a no-op on a miss.
  - Engine owners unregister their own ids this way in their destructors (e.g. 0x517540).
- **Teardown:** file unload `0x2cf80`, on the owned-player path (`file+0x1610 == -1`), calls `0x2d0b0`. That
  unregisters **every** id in `file+0x1790`, then destroys the player. On the other branch (`file+0x1610 != -1`)
  nothing is unregistered and the entries remain. That is only safe if each use refreshes them (see below).
- **Calling it from the hub hook is safe:**
  - It makes no Iggy API call. It only reads `file+0x1608` as an integer.
  - It takes the table lock and the `0x1548e0` lock. Neither is held when the hook runs. The hub takes the table
    lock only after the hook returns, and the hub's own 0x2b7e0 takes the `0x1548e0` lock in this same callback
    context.
  - So a hook that registers and then calls the original hub has the hub find the new key and queue this very
    call.
- **Lifetime of our pointers:** `fn` and `owner` live in our DLL, which stays loaded until exit, so they never
  dangle. Re-registering on every forwarded call also keeps `entry.file` current.

## Q2. Objects (VERIFIED)

- **Hub arguments:** the hub is `0x34800(arkIggyMain* self, Iggy* player, IggyExternalFunctionCallUTF8* call)`.
  It is installed with `0x102c430(hub, main)`.
- **Hub lookup:**
  1. It computes `key = (uint32)player ^ hash(name)`.
  2. On a hit it calls `0x2b7e0(entry.file, &{entry.file, entry.owner, entry.fn}, call)`.
  3. On a miss it tries the global key `hash(name)` (0x349e0). On a hit it uses `file = *(player+0x78)` (0xc19b0),
     and if that is null it logs and drops the call.
  4. It always returns 1.
- **Player and file are linked both ways.** Loader 0x2c620 stores `Iggy*` at `file+0x1608` (0x2bfb0 reads it). It
  sets `Iggy+0x78 = file` via 0x556540 (IggyPlayerSetUserdata), and caches `IggyPlayerRootPath(Iggy)` at
  `file+0x1780` (0x2bfe0).
- **Handler arguments:** handlers get the same `arkIggyFile*`. The drain passes `*(file+0)`, a back-pointer set to
  the file itself by 0x2ac90.
- **Getting what the Iggy API needs from a handler:**
  - `Iggy* iggy = *(void**)(file+0x1608)`.
  - The root path is `IggyPlayerRootPath(iggy)` (0x1009a90, returns `iggy+0x2950`, or null with no AS3 context).

## Q3. The drain (VERIFIED, except the thread identity)

**Where it runs.** `0x35170` (arkIggyMain update, called from 0x48ad0) calls `0x30da0(main+0x109f40)`:

1. It processes load and unload requests.
2. Then, **for each file:**
   - `0x2cc90(file)` calls `IggyPlayerTickRS` (0x100ccd0), which takes and releases the lock;
   - **then** `0x2b610(file)` drains the queue.

The load-wait loop 0x31c80 does the same. The drain runs at top level after Tick has returned, with the lock free.
It pops records until the ring is empty and calls `fn(owner, file, &rec->args)`. After the call it frees copied
strings and refs.

**Proof that writes and method calls work there.** The engine's own `OnOptionSectionSelected` handler (0x5171f0)
does exactly this from the drain:
1. section vtable slot `+0x20` = 0x501bb0 writes `optionsData.currentSection`;
2. slot `+0x30` = 0x503920 calls `_C_ShowOptionSection`.

**Calls made while our handler runs.** AS3 that our handler triggers through CallMethodRS may call the hub again.
Those calls are queued and run in the same drain loop. They are not re-entrant.

**INFERRED:**
- The handler thread is whichever thread runs 0x48ad0. 0x48ad0 is reached only through a vtable, so the thread is
  not resolved statically.
- Latency is at most one frame.
- If another thread is inside Draw or Dispatch on the same player at that moment, the calls fail with 0x192
  rather than deadlocking. The engine has the same exposure. Check the return codes.

## Q4. IggyPlayerCallMethodRS 0x100c4c0 (VERIFIED)

`int CallMethodRS(Iggy* iggy, IggyDataValue* result, IggyValuePath* target, IggyName method, S32 nargs, IggyDataValue* args)`

Return values:
- 0: success;
- 0x192: re-entry (lock held);
- 0x1f5: no movie.

How the engine calls it (wrapper 0x28a90):
- The method object is `{Iggy*, IggyName, IggyDataValue result[0x20]}`. Its name is bound once by 0x28b70 through
  `CreateFastNameUTF8(iggy, name, -1)`.
- The target is the top of a path stack seeded by 0x28ee0 with a copy of `*(file+0x1780)`. That makes it **the
  root path**.
- Arguments come from 0x28980 with capacity 1 and **count 0**: a zeroed 0x20 buffer is passed, with `nargs = 0`.

Engine call sites:
- `_C_ShowOptionSection`: 0x503920 (section vtable +0x30), method object at `this+0x10`.
- `_C_BackFromOptionSection`: 0x5018b0 (vtable +0x60), method object at `this+0x40`, reached from Back through
  0x503650.

Both are bound in 0x5034a0 (vtable +0x18).

## Q5. Argument layouts (VERIFIED unless marked)

- **Hub `call`** (IggyExternalFunctionCallUTF8):
  - `+0 const char* name`;
  - `+0x10 S32 num_args`;
  - `+0x18 IggyDataValue args[]` **inline** (`lea r14,[r8+0x18]` at 0x2b866).
  - **Bug in the mod:** `NativeOptionsPage::number_argument` dereferences `call+0x18` as a pointer. The number is
    actually the F64 at **`call+0x28`**, with its type at `call+0x18`.
- **Handler `args`** (`idList<IggyDataValue>`):
  - `+0 IggyDataValue* data`;
  - `+8 u32 count & 0xffffff` (the top byte is flags);
  - `+0xC u32 (capacity<<8 | flags)`.
  - Engine read in 0x517110: `count==1`, then `cvttsd2si [data+0x10]`.
- **IggyDataValue** (0x20 bytes):
  - `+0 S32 type`, `+8 temp_ref`, `+0x10` union.
  - Number: F64 at `+0x10`. Type 4 is INFERRED from SDK enum order; the engine reads the F64 without checking the
    type.
  - Type 5, UTF-8 string: pointer at `+0x10`, length at `+0x18`. The queue makes a heap copy including the NUL and
    frees it after the handler.
  - Type 6, UTF-16: the queue logs an error and **drops the call, leaking one record**.
  - Types 10 and 11, array and object: converted to a persistent `IggyValueRef` at `+0x10` (0x100a2f0) and freed
    after the handler with 0x100a170.
  - Type 9: valueref.
- **Queue capacity:**
  - The pending ring holds 32 entries: `file+0x40`, read index `file+0x140`, write index `file+0x180`.
  - The free pool holds 16 records of 0x128 bytes: `file+0x1c0`, indices `+0x2c0` and `+0x300`.
  - 0x2b7e0 does not check for an empty pool. Never enqueue in bulk.

## Q6. Returning 1 for section 100 (VERIFIED)

The engine handlers 0x517110 (Back) and 0x5172d0 (Restore) only look at the idx-th non-null entry among their four
section slots, and return otherwise. Both forwarding index 100 and swallowing it are therefore harmless. The engine
options state (`owner[0]`, set only by 0x5171f0 OnOptionSectionSelected) is never touched by the VR page.

The real hazards are:
1. Registering those names, which replaces the engine handlers (Q1).
2. The `number_argument` offset bug. Because of it the index-100 branch never matches, so `closed()` and
   `restore()` never run.

Note what the engine's Restore does: it refills (+0x20), saves, and calls `_C_ShowOptionSection` **again** (+0x30).
The VR page can do the same instead of the `locked` toggle trick.

## Recipe

Recommended approach: **enqueue directly with 0x2b7e0**. The hub does exactly this (Q2). It needs no table entry,
nothing to unregister, carries no collision risk, and also works for the engine-owned names at index 100. The
record does not keep the name, so pass an **event tag as `owner`**.

```cpp
struct IggyDataValue {std::int32_t type,pad;void* temp_ref;union {double number;struct {const char* str;std::int32_t len;} s;void* ref;};};
static_assert(sizeof(IggyDataValue)==0x20);
struct IggyArgs {IggyDataValue* data;std::uint32_t count_flags,capacity_flags;};      // count = count_flags & 0xffffff
using DeferredFn=void (__fastcall*)(void* owner,void* file,IggyArgs* args);
struct CallTarget {void* file;void* owner;DeferredFn fn;};                            // copied into the record
using RegisterCall=void (__fastcall*)(void* file,const CallTarget* t,const void* call); // 0x2b7e0 {4c 89 44 24 18 48 89 4c 24 08 55 56}
using CallMethodRS=int (__fastcall*)(void* iggy,IggyDataValue* result,void* path,std::uintptr_t method,int nargs,IggyDataValue* args); // 0x100c4c0 {48 89 5c 24 10 48 89 6c 24 18 48 89}
// optional variant A: using RegisterHandler=int(__fastcall*)(void* file,const char* name,DeferredFn,void* owner); // 0x2cd20 {40 57 48 83 ec 40 48 c7 44 24 30 fe}
//                     using UnregisterHandler=void(__fastcall*)(void* file,int id);                             // 0x2cf50 {89 54 24 10 53 48 83 ec 20 48 8b d9}
enum Event:std::uintptr_t {Open=1,Changed,Back,Restore,Action=0x100}; // Action+n for VR_SETTING_ACTION_<n>

// Hub hook (0x34800): classify by name, never touch Iggy here.
//   number argument: n>=1 at *(int*)(call+0x10); value = *(double*)(call+0x28)
if(event) {                                    // VR_SETTINGS_EVT/OPEN_REQUEST, VR_SETTING_CHANGED, ACTION_n,
    void* file=*(void**)((char*)player+0x78);  // or OnBack/OnRestore with number == 100
    if(file && *(void**)((char*)file+0x1608)==player) {CallTarget t{file,(void*)event,&on_deferred};register_call(file,&t,call);}
    return 1;                                  // the original hub would only drop unknown names
}
return hub_original_(self,player,call);

// Deferred handler (drain, outside AS3): all Iggy work happens here.
static void __fastcall on_deferred(void* owner,void* file,IggyArgs*) {
    void* iggy=*(void**)((char*)file+0x1608);if(!iggy) return;
    switch((std::uintptr_t)owner) {
    case Open: if(NativeOptionsPage::open(base,iggy) && show(iggy)) ++native_pages;else requested=true;break;
    case Changed: NativeOptionsPage::changed(base,iggy);break;            // GetS32RS is guarded too
    case Restore: if(NativeOptionsPage::restore(base,iggy)) show(iggy);break; // the engine re-shows after restore
    case Back: NativeOptionsPage::closed();break; // the shipped lib_options _BPressed edit already navigates back
    default: NativeOptionsPage::action(int((std::uintptr_t)owner-Action));
    }
}
static bool show(void* iggy) {   // same as engine 0x503920: root path, 0 args, zeroed arg buffer
    void* root=RootPath(iggy);std::uintptr_t m=root?FastName(iggy,"_C_ShowOptionSection",-1):0;
    IggyDataValue result{},none{};return m && call_method(iggy,&result,root,m,0,&none)==0;
}
```

Notes:
- **Back navigation:**
  - The shipped `lib_options` edit (`controller_glyph_atlas.hpp`, offset 489418) makes AS3 call
    `_C_BackFromOptionSection` itself for index 100 or above, so Back must **not** also call it natively.
  - If that edit is ever dropped, call `_C_BackFromOptionSection` the same way `show` calls
    `_C_ShowOptionSection` (engine 0x5018b0).
- **Opening:** the shipped click edits (`menu_patches.hpp` m0_n2, m1_n2, m2_n0) only send `VR_SETTINGS_EVT` and do
  not call `_C_ShowOptionSection`, which is what deferral needs. The deferred handler now shows the page. The
  comments in `native_menu_entry.hpp` and `native_options_page.hpp` that say AS3 shows it are stale.
- **Fixed-size queue:** keep `requested=true` as a fallback when show fails, and do not enqueue more than a few
  calls per frame (16-record pool).
- **Variant A, registered handlers:**
  1. In the hub hook, call `RegisterHandler(file, name, on_deferred, (void*)event)` with the mod's own name, on
     **every** call.
  2. Then return `hub_original_(...)`. The hub queues the call.
  - Re-registering refreshes stale entries, and the engine removes them at file teardown.
  - It cannot be used for the engine-owned names: use direct enqueue or forward for those.

## Still needs a live check

- The first live open from the deferred handler: `open()` writes, then `_C_ShowOptionSection`. Look at the
  CallMethodRS return code.
- Whether the handler thread races the render thread's `IggyPlayerDraw`, which would show up as intermittent
  0x192.
- Whether a second `_C_ShowOptionSection` after Restore refreshes the page in place.
- Headset readability and comfort cannot be judged statically.
