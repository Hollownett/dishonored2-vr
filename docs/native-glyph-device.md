# Native glyph / input-device decision (Dishonored 2 Steam 17576867)

Static reverse engineering only. Nothing was run live; no headset or comfort claim. VERIFIED = bytes, decompile or AS3 bytecode
seen. INFERRED = deduced from verified facts. RVAs are relative to image base 0x140000000. Raw notes, decompiles, AS3
dumps and the ready patch table: `artifacts/claude/glyphs/` (`notes.md` first).

## Verdict

1. The glyph set is held per movie in AS3, not in a native flag: `com.ark.inputs:PCControllerTypeHandler._controllerType`
   (0 XBOX, 1 KEYBOARD, 2 MOUSE, 3 PS4, 4 STEAM), changed only by `_C_UpdateControllerType(int)`. VERIFIED.
2. The mod's own Iggy injection flips it, inside the movie's AS3. The native "pad is current" byte is not touched by the
   injection, and no OS mouse input is involved. VERIFIED (AS3 and native paths), the Flash-event hop for the move is INFERRED.
3. `MOUSE` is also what switches the Iggy cursor on (`UIMouseCursor.active/visible`, `InputsManager.mouseEnabled`). Setting the
   type back to pad after injecting (AS3 or native) would hide that cursor. So do not fight the type; decouple the artwork:
   **make `ControllerButtonDefinition.getButtonImage` take its existing XBOX branch in every UI movie** (same-size 3-byte edit,
   the v221 wheel edit extended from 2 movies to 48). Confidence: high on mechanism, medium-high on completeness until headset-checked.

## 1. Where the device is decided

| Item | Location | Notes | Status |
|---|---|---|---|
| Input manager | `*(u64*)(base+0x2274340)` | plain struct, no vtable at +0; ctor 0x38640 | VERIFIED |
| Pad is current | `byte [mgr+0xa544]` | 1 pad, 0 keyboard/mouse. Writers: ctor 0x38640 (0), **0x3b580 only** (stores 0x3b61f, 0x3b636). Readers: getter 0x39d50 (14 callers), 0x3a240, 0x3b580 | VERIFIED |
| Active pad index | `int [mgr+0xa548]` | -1 none; set by 0xd9e70, reset 0xd9f10 | VERIFIED |
| Flag update rule (0x3b580) | per input tick (0x3a900, from 0x48ad0 every >=20 ms) | `padActive = idx>-1 && padEvents(idx).count>0`; if flag==0: flag=padActive; else if (kbEvents.count>0 or mouseEvents.count>0) and !padActive: flag=0. Event lists come from DirectInput device polling in 0x3a470 | VERIFIED |
| Type value | `0x34920()` -> int | `!flag`->1; else no pad->0; else pad-type query==1->3 (PS4); else 0. `0x34990()` = same without the keyboard branch | VERIFIED |
| Native -> movie push | AS3 `controllerTypeHandler._C_UpdateControllerType(int)` | strings 0x1abeda8 / 0x1abedc0. Pushed with `0x34920()` by: `0x35020` (Iggy callback "SetFlashFocus", registered in 0x34ba0, sent by AS3 `UIScene.focused=true`; also creates the file wrapper via 0x33b50), movie inits 0x479a0 / 0x3901e0 / 0x45fd30 / 0x4eee42 / 0x4f6053 / 0x78a3d0, refreshes 0x38f7d0 0x45fe40 0x4ef8e0 0x4f6d50 0x5067f0 0x78a7b0 0x78aaef, and a conditional per-tick push in 0x48ad0 | VERIFIED |
| Movie state | AS3 `PCControllerTypeHandler._controllerType`; `_C_UpdateControllerType` returns early if unchanged, else dispatches `Event_ControllerType.CHANGE`; `static isGamepad` = type is neither KEYBOARD nor MOUSE | per movie file (every UI movie embeds the library) | VERIFIED |
| Native -> movie input hooks | wrapper (stride 0xde8, init 0x34200): +0x8 `inputsManager.gamepadHandler`, +0x2b0 `IG_OnGamepadEvent`, +0x2e0 keyboardHandler, +0x588 `inputsManager.mouseHandler`, +0x830 `IG_OnMouseEvent`, +0x860 `mouseCursor`, +0xb10 `inputsManager`, +0xdb8 `_C_ClearInputs` | pad events: 0x33370 pushes (code,value,extra,padType=0x34990()); mouse buttons: 0x33450 | VERIFIED |

No strings named isGamepad / UsingController / OnInputDeviceChanged exist in the exe; the names above are the whole interface.

## 2. Who flips it (AS3 writers of the type)

| AS3 method (every movie) | Trigger | New type | Status |
|---|---|---|---|
| `MouseHandler.IG_OnMouseEvent(code,value[,flags])` | called natively by **0x33450** (the mod's button call) with 2 args (`code` int, `value` float); condition `value!=0 || flags!=0`, `flags` is undefined with the 2-arg call so button-up also qualifies | MOUSE | VERIFIED |
| `MouseHandler._onMouseMove` | stage `MouseEvent.MOUSE_MOVE` listener; fed by the Iggy move event the mod dispatches (type 7 via 0x102c3e0 + **0x2cb20**), same path the native 0x33530 uses for the real OS cursor | MOUSE | listener VERIFIED, event hop INFERRED |
| `MouseHandler._onMouseWheelEvent` | wheel | MOUSE | VERIFIED |
| `KeyboardHandler._onKeyDown` | Flash key down | KEYBOARD | VERIFIED |
| `GamepadHandlerPC.IG_OnGamepadEvent(code,value,flags,padType)` | native 0x33370 on any pad event with value/flags != 0 | padType (0 or 3) | VERIFIED |
| `QAWFilter._onInputUp` (wheel) | KEYBOARD -> MOUSE only | MOUSE | VERIFIED |

Answers to the three questions:

* Native flag vs injection: `0x3b580` sees only DirectInput device event lists; injecting into Iggy (0x2cb20 is a thin wrapper over the
  Iggy dispatch 0x14100c6e0 on `file+0x1608`, 0x2a8b0 an Iggy value setter, 0x33450 builds the event and calls the AS3 handle) never writes them, and `src/` has no SetCursorPos/SendInput/mouse_event. So the native byte stays at pad (1). Read-only live
  check to confirm: read `*(u8*)(*(u64*)(base+0x2274340)+0xa544)` while the pointer flips the glyphs; it should stay 1.
* A different Iggy event that moves without being "mouse": none. Hover needs the Iggy mouse event and the stage listener catches
  every move. `0x2a8b0` alone (cursor sprite x/y) does not raise the listener but gives no hover (INFERRED).
* Real hardware mouse or keyboard use (not the mod) does flip the native byte to 0 until the next pad event; the movies then get
  KEYBOARD from native pushes. Pad activity (the mod's emulated pad) restores it.

## 3. Why not "set the type back to pad"

`UIMouseCursor._onControllerChanged`: `active = (type==MOUSE)`, `visible = !isGamepad`; `static set active` also sets
`InputsManager.mouseEnabled`, which (un)registers MouseHandler -> InputsManager ControllerEvent forwarding.
`UIScene.focused`/`QuickAccessWheelScene._enableInputs` turn the cursor off when `isGamepad`. A post-injection reset (AS3 call or
the native sequence of 0x35020: handles 0x29a60/0x28b70, args 0x28980/0x28700, call 0x28a90) therefore also hides the cursor and
adds two `CHANGE` events per frame. Suppressing the three MouseHandler call sites (`4f xx xx 01` -> `29 29 02 02`, pop pop nop nop;
offsets in `scan_all.json`) has the same side effect. Neither is recommended. INFERRED for the exact UI behaviour, VERIFIED for the code.

## 4. Recommended change: force the pad artwork in getButtonImage

`ControllerButtonDefinition.getButtonImage(platform,large,useKeyboard)` starts with `pushnull; coerce_s; setlocal 4; jump <platform switch>`.
Retarget that `jump` to the existing `label` of the `getlocal0; getproperty image_XBOX ...` branch (`ID_XBOX` / `_s` suffix, null handling
untouched). Definitions are built as `(padID, keyboardEquivalent)` (`BindableActionDefinition.bindings`, `QAWFilterElement`), so `image_XBOX`
resolves to `gamepad_B_XBOX` style bitmaps, all present in `lib_controller_buttons.iggy`. VERIFIED statically.

| Variant | Movies | Bytes at the jump | Status |
|---|---|---|---|
| standard build | 43 | `10 79 00 00` -> `10 5c 00 00` | VERIFIED (my generator reproduces the v221 patched SHA256 of `quick_access_wheel` and `lib_wheel` exactly; original hashes match) |
| debug-compiled (journal, lib_journal, lib_journal_craft, lib_journal_mission, lib_mission_results) | 5 | `10 89 00 00` -> `10 64 00 00` | VERIFIED bytes, same logic |

Full list with file offsets, sizes, original and patched SHA256: `artifacts/claude/glyphs/glyph-getbuttonimage-patches.json`
(48 movies, v221 schema; 6 font/localisation movies have no ABC). Generic rule if a table is unwanted: in class `ControllerButtonDefinition`, method
`getButtonImage`, find `20 85 63 04 10` in the first 64 body bytes and rewrite the 3-byte `s24` to the displacement of the last `label` (0x09)
preceding the `image_XBOX` getproperty. Hook point already exists: `NativeMenuEntry` loader hook 0x2E4D0 calls `owned_movie_patch(path,data,size)` for every movie.

Integration hazards:

* `ui/hud.iggy` already has the v221 crosshair edit; add a second edit (`992262`) and use the combined patched hash recorded in the JSON
  (`also_applies_v221_edit`). The v221 struct holds one edit per path.
* `main_menu`, `corvo_pause_menu`, `emily_pause_menu` are also keyed by `src/menu_patches.hpp` on the full original SHA256, and
  `NativeMenuEntry::patch` runs `owned_movie_patch` first. A separate glyph edit would make the VR Settings patch fail its digest check.
  Fold the glyph edit into those three edit lists, or run it after the menu patch. Edits do not overlap (300592 / 300367 / 357545);
  combined hashes in `glyph-menu-combined-hashes.json` (corvo `9925801a...ce69`, emily `d8a9aeb8...77a8`, main `534ca606...451a`).
* Already-loaded movies are not changed (same limit as v221). Verify with `native_art_movie_patched` / `_rejected`.

Layout flips (not artwork) that follow `isGamepad` and are handled by separate same-size edits, all listed with before/after SHA256 chains in
`artifacts/claude/glyphs/layout-patches.json`:

| Movie | Site | Offset | Edit | Why |
|---|---|---|---|---|
| quick_access_wheel | `ShortcutsHandlerWheel._updateShortcutsType` | 329626 | `12 24 00 00` -> `29 02 02 02` | fall-through = D-pad shortcut bar (applied) |
| quick_access_wheel | `PotionsGamePad._onControllerChanged` | 327727 | `12 3a 00 00` -> `29 02 02 02` | fall-through = side-by-side pad layout (x -63, y 0); taken branch = stacked PC layout |
| hud | `QuickPotion._updateControllerType` | 1208270 | `60 5d 66 cb 07` -> `26 02 02 02 02` | PC branch is the fall-through here, so `isGamepad` is replaced by `pushtrue` instead of pop+nop |
| hud (optional) | `InteractionWindowElement` hold gauge / gauge visible / positions | 1218465 / 1218595 / 1218724 | `12 xx 00 00` -> `29 02 02 02` | fall-through = radial gauge + gamepad prompt layout |

Not AS3-patchable by design: the wheel 'Hide/Show all (hold)' keycap and the elixir R/T keycaps (both `ControllerButtonIcon` output, covered by the art row);
the HUD ShortcutsPCHUD dock bar (picked natively: `0x45e800` tests `0x39d50(*(base+0x2274340))`, nonzero -> AS3 `_C_Show`, zero -> `_C_ShowPCDockBar`);
cursor enables (`_enableInputs`, `UIScene.focused`, `UIMouseCursor`) which must keep reading the real type.
Edits leave dead code after the removed branch (same as the existing menu patches).

## 5. Pinned code bytes (first 32 bytes, for anything the mod would read, call or hook)

| RVA | Role | Bytes |
|---|---|---|
| 0x3b580 | update pad-is-current flag (sole runtime writer) | `48895c24084889742410574883ec208bb948a50000488bd983ffff7e28488b0d` |
| 0x39d50 | getter `movzx eax,byte [rcx+0xa544]; ret` (8 bytes meaningful) | `0fb68144a50000c3cccccccccccccccc40534883ec208bd985c9750d488d05d5` |
| 0x34920 | GetControllerType | `4883ec28488b0d15fa2302e82054000084c07450488b0505fa230248895c2420` |
| 0x34990 | GetPadType | `40534883ec20488b05a3f923028b9848a5000085db782a488b0d4a232502488b` |
| 0x3a900 | InputManager::Update | `40534883ec20488bd9e862fbffff488bcbe86a0c000083bb40a5000000488bcb` |
| 0x35020 | SetFlashFocus callback (pushes `0x34920()`) | `40574881ec6003000048c7442420feffffff48899c2470030000488bda488bf9` |
| 0x33370 | pad event -> IG_OnGamepadEvent | `48895c24084889742418574881ec00010000498bd8488bfa488bf14c8d8424b8` |
| 0x48ad0 | UI movie tick | `488bc45741544155415641574881ec9001000048c7442430feffffff48895808` |
| 0x33450 / 0x33530 / 0x2a8b0 / 0x2cb20 / 0x102c3e0 | already pinned in `src/native_menu.hpp` (still match the static exe) | see source |

Optional native pin (not needed for the recommendation): writing `1` to `byte [mgr+0xa544]` each game-thread tick would also keep native-formatted
text and native pushes on pad even after stray keyboard or mouse use. Risk: 0x3a240 and the 14 getter callers also read it. INFERRED, untested.

## 6. Open items needing live or headset confirmation

* That the Iggy move event really reaches `_onMouseMove` (static read says yes; the button path is certain).
* That the XBOX branch renders correctly for every movie's prompts (bitmap names exist; no live view).
* Whether any movie re-pushes the pad type every tick (0x48ad0 conditional path) and fights MOUSE; the user's reports suggest not for the wheel.
