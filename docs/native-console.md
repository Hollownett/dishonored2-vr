# Native console / command system

Pinned PE: Steam build 17576867, image base `0x140000000`. Code: `src/native_console.hpp`, checks: `tests/native_console_check.cpp` (layout, code-byte verify, fake-vtable call path). Nothing is hooked, installed or called by the DLL. All evidence is static (Ghidra decompile via `tools/D2HudDecomp.java`, capstone disassembly, string/xref scan of the PE). No live call has been made.

## Singleton and vtable

* `*(base+0x228bae0)` is a pointer to a statically initialised object at RVA `0x228bb00` (the file already contains `0x14228bb00`; the object is `idCmdSystemLocal`-like, source string `idlib\csystems\CmdSystemLocal.cpp`). `object[0]` = vtable RVA `0x1aef458`. The prior note ("0x228bae0 is a global cmd system pointer") is CONFIRMED, but the `+0x40` slot is NOT "execute a command line" in the buffered sense, see below.
* Object fields used: `+0x08` head of singly linked command list (node 0x28: `next, name, handler, description, autocomplete`), `+0x10` int wait counter, `+0x18` idStr command buffer.
* Command handler ABI: `void handler(NativeCmdArgs*)` (RCX). `NativeCmdArgs` (built by `0x132630(args, text, 1)`, size `0x1408`): `+0x00 int argc` (includes the command name), `+0x08 char* argv[128]` (argv[0] = name), `+0x408 char text[0x1000]`.

| slot | RVA | meaning (evidence) |
|---|---|---|
| +0x08 | `ffeb0` | register builtins: listCmds, exec, vstr, echo, parse, wait, then walks the static registration list (head at `.data 0x32c7e00`, node ctor `164690`) |
| +0x10 | `feba0` | `AddCommand(name, handler, description, autocomplete)`; duplicates with a different handler print an error. 171 call sites, e.g. `887250` registers the game debug commands |
| +0x18 | `100340` | RemoveCommand(name) |
| +0x20 / 0x28 | `ffe50` / `ff2b0` | get description / command exists (exact or substring) |
| **+0x40** | `ffba0` | **ExecuteCommandText(const char*)**: `132630(&args, text, 1)` then slot +0x68. IMMEDIATE, on the calling thread. Used for engine-internal one-shots (e.g. `0x89dbb6`, `0x4f115`). |
| **+0x48** | `fec70` | **AppendCommandText(const char*)**: tail-jumps to the idStr append on `this+0x18`. Does NOT add a newline. Engine callers append the text and then `"\n"` (`140045460`: slot 0x48 with text, slot 0x48 with `"\n"`; `0x14003ffd1` appends a literal) |
| **+0x50** | `ff8f0` | **ExecuteCommandBuffer()**: if `wait` counter (this+0x10) != 0, decrement and return; else repeatedly cut one line from the buffer (terminators `\n`, `\r`, `;` outside double quotes; `//` and `/* */` comments stripped), tokenise it with `132630`, dispatch via slot +0x68, until the buffer is empty |
| +0x58 | `12da30` | set wait counter (`this+0x10 = edx`; the `wait` command) |
| +0x60 | `8832b0` | returns list head (`this+0x08`) |
| +0x68 | `ffbe0` | ExecuteCommandArgs(NativeCmdArgs*): skips empty argc and names starting with `_`; looks the command up in the list with the CASE-INSENSITIVE compare `143650` and tail-calls its handler; if not found tries the cvar system (`DAT_14228bae8`, vtable +0x78 = set/print cvar) and prints "unknown command" if that fails |
| **+0x70** | `ffff0` | **InsertCommandText(const char*)**: buffer = text + "\n" + buffer (prepend; used by `exec`, `vstr`) |
| +0x88 | `fed10` | autocomplete helper |

There is no execWhen enum in this build: "immediate" = slot 0x40, "append" = slot 0x48 (+ explicit `\n`), "insert" = slot 0x70.

## Thread and safety

* The per-frame flush is `140045460` (caller `1400450d0`): `while(true){ slot 0x50; input pump (DAT_142274708 +0xf8); event poll (DAT_142286cf8 +0xe8); ... }`. It also feeds typed console text into the buffer with slot 0x48 twice (text, then `"\n"`). So the buffer is drained once per iteration of the main loop of the thread running that function (the main engine/game thread; identity versus the mod's Stage0 hook thread is UNVERIFIED but they are expected to be the same).
* No lock, critical section or atomic exists in the command-system methods (`ffba0`, `fec70`, `ff8f0`, `ffff0`, `ffbe0` disassembled/decompiled). Only `AddCommand` takes the heap lock (`FUN_1401548e0()` vtable +0x48/+0x50) around its node allocation. The buffer is a plain idStr that `ff8f0` reallocates and cuts in place while running handlers (handlers such as `exec` re-enter slot 0x70), so a foreign-thread append can race a realloc/free and crash.
* Recommendation: call the helpers only from the game thread (same scope as the Stage0 work in `native_interaction_aim.hpp`), with a mod-side mutex-protected string queue filled from other threads and drained there. Prefer `buffer_command` (append; runs at the next pump, one frame later, safe against re-entrancy) over `execute_now` (runs inside your hook, i.e. possibly mid-frame while game state is half-updated). Calling slot 0x50 yourself is not needed.
* Only the three text slots are wrapped; `verify()` pins code bytes of every RVA used plus the vtable targets.

## Cheat gating

* Every debug-command handler checks `FUN_14011d080(game, player_or_0)`. That function is 3 bytes: `b0 01 c3` (`mov al,1; ret`), i.e. **always true in this retail build**. There is no `developer`/`cheats`/devmode check, so `setviewpos`, `teleport`, `god`, `buddha`, `killEntity`, `damage`, `trigger` work in a normal game. (The strings `developer` at `0x1ac4418` and `CVAR_CHEAT`/`CVAR_NOCHEAT` exist; CVAR_CHEAT only restricts `cvar_resetcheats` / possibly the generic set path via `DAT_14228bae8`; not needed for the commands below.)
* The handlers still need an active game: `FUN_1408a8830(game)` (player entity; null in the main menu) must be non-null for `setviewpos`, `getviewpos`, `teleport`, `trigger`. In the menu they silently return or print "You must be in-game for this command to work."
* `god` toggles cvar objects at `.bss 0x398d500` (`ark_god_player`, int at +0x28 = `0x398d528`) and `0x398d580` (`ark_god_npc`, `0x398d5a8`) via setter `102520(cvar, value, 1)`; names are known strings, the object RVAs are inferred from the handler. The mod can also set them by name through the cvar mechanism in `native_cvars.hpp` (name pointer +0x30, int +0x28), but the `god` command is simpler.

## Command list (verified registrations)

Name matching is case-insensitive. Quote args with spaces. Separators `;` / newline.

| command | handler | syntax / behaviour |
|---|---|---|
| `setviewpos` | `886d10` | `setviewpos <x> <y> <z> [yaw(DEG)] [pitch(DEG)]` or `setviewpos last` (argc must be >= 4 or exactly `last`; usage text at `0x1c8a3a0`). Does NOT move anything itself: it computes `z' = z - (DAT_1439a30bc - 0.25)` (a runtime view-height offset, value not in the file: it is .bss) and enqueues a 0x18-byte message type `0xc2` `{x, y, z', pitch, yaw, 0}` (note argv[4] -> yaw, argv[5] -> pitch) into the game's queue (`FUN_140810240()+0xc8e8`, player id `*(player+0xa8)`), applied by game logic later. `z` is therefore the EYE position, the same space as `getviewpos`. |
| `getviewpos` / `where` | `8861c0` | prints `xyz = ( %s ) yaw = %.1f pitch = %.1f` to the console (no return value), reading the player's camera block (`player+0x300`); falls back to a stored position when the game is not live. |
| `teleport` | `886f50` | `teleport <entity name>`: teleports the player to that entity. |
| `trigger`, `damage`, `killEntity`, `reloadEntity` | `887060`, `885990`, `1408863e0`, `886aa0` | `trigger <ent>`, `damage <ent> <damageDecl>`, `killEntity [all] <ent>` (refuses the player), `reloadEntity <ent>` |
| `god` | `718f00` | `god [player|npc]`, no arg = player; toggles `ark_god_player` / `ark_god_npc`, prints `God mode enabled/disabled on player` |
| `buddha` | `718e20` | toggle buddha mode (health floor) |
| `map` | `4a9f0` | `map <name> [reset] [spawnSpot <name>] [playlevel|playlevelfromhere]`; loads `maps/<name>` |
| `LoadGame` | `658d00` | static-list command, usage `loadgame <slotNumber>? <overrideMapName>?` (max 2 args; slot parsed with atoi). Name is `LoadGame`; matching is case-insensitive so `loadgame` works. Engine itself uses `wait 1\nLoadGame %d\n` (strings `0x1c91e98`/`0x1c91f28`). Also `SaveGame`, `RestartFromCheckpoint`, `RestartFromMemoryCheckpoint` (same static list). |
| `exec` | `ff3d0` | `exec <file> [-s <define>]`: appends `.cfg`, reads through the engine file system (`DAT_14228baf0` +0xc8), preprocesses and INSERTS the lines into the buffer (slot 0x70). Search path is the engine VFS, not verified. |
| `wait` | n/a | `wait [n]`: delays the remaining buffered commands n frames (slot 0x58) |
| `vstr`, `echo`, `parse`, `listCmds`, `toggle`, `reset`, `cvarAdd`, `cvar_resetcheats`, `listCvars`, `quit`, `restartmap`, `disconnect`, `leaveGame`, `vid_restart`, `screenshot`, `arkBind`/`arkListBindings`, `ark_settings_save`, ... | various | registered; not wrapped |

### Not present in this build
* **`noclip`**: no command is registered. The strings `RELIABLE_PLAYER_COMMAND_NOCLIP` (enum value 85, enum table at file `0x2387140`), `WALKSTATE_NOCLIP`, cvar `pm_noclipspeed` and `g_debugPlayer` ("Player for debug commands like noclip") exist, so the player noclip machinery exists but the console entry point was stripped. It cannot be reached from the text console; using it would need the reliable-player-command path (not investigated).
* **`notarget`, `give*`, `spawn`/`entity spawn`, items/powers**: no command strings. The only related hooks are the cvars `ark_dbg_infinite_coins`, `ark_god_player`, `ark_god_npc`, `ark_bloodthirsty_enableNoTarget`, and `damage`/`killEntity`/`trigger` above. For a playground use `setviewpos` plus `god`; giving items/powers needs a game-state call (not researched).

## What remains to verify live

1. That slot 0x48 + next frame pump actually runs a command appended from the Stage0 hook thread (and that thread == the thread running `140045460`).
2. Units (assumed metres, as the rest of the mod) and the exact `z` convention of `setviewpos` (runtime offset `DAT_1439a30bc - 0.25`): round-trip `getviewpos` output into `setviewpos` and compare camera position. Also that yaw/pitch are the same convention as the mod's yaw.
3. That `setviewpos` while the VR rig/body anchor is active does not fight the mod's locomotion (the rig must re-anchor after the teleport message is applied).
4. `exec` file location and `LoadGame` behaviour from a running map (async load, hook lifetime across the map change).
5. The god cvar object RVAs (`0x398d500`, `0x398d580`): read the name pointer at +0x30.
6. Whether a pre-existing VR hook observes a stale camera for one frame after teleport.
7. Reaching noclip another way if wanted.
