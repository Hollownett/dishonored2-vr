# Native "VR Settings" page: using the game's own options UI

Static analysis only. The game was not launched or attached. Build: Steam 17576867 (`Dishonored2.exe`, same image
as `artifacts/re-toolchain/direct-ghidra`). RVAs are relative to 0x140000000.
Scratch scripts and dumps: `artifacts/claude/vroptions/` (`classes.py`, `q*.py`, `d.py`, `ms.py`, `mn.py`,
`*.txt` disassembly, `decls/` option decls).

Labels: **VERIFIED** means read directly from AS3 bytecode or x64 disassembly. **INFERRED** means a reasoned
conclusion that still needs a live check.

## Verdict: FEASIBLE

The options sub-page (`com.bsp.options.OptionSectionScreen`) is pure AS3 driven by data. The engine fills it by
writing AS3 properties through Iggy value paths and then calling an AS3 method. The mod can do the same thing
with the Iggy API functions that are statically linked in the exe (all located below). The design needs:

- the existing hub hook;
- four small same-size AS3 edits: one call added to each VR Settings click body, plus one guarded back-navigation
  edit in `lib_options`;
- no engine decls and no change to `Dishonored2.exe` code.

The rows (header, list items, sliders, steppers, Yes/No toggles, highlight, fonts, description plate, controls bar)
are the game's own widgets, drawn by Iggy.

---

## Q1. How a native options page gets its rows

### AS3 data model (lib_options.iggy; the pause and main movies embed the same classes)

- **VERIFIED** `com.bsp.options.OptionsData` has `options`/`sections`, a `GameDataProvider` of
  `OptionSectionDefinition`, and `currentSection`, a single `OptionSectionDefinition` created in `_init`. The pause
  movie builds it in `PauseMenuData._initData`. The scenes expose it as `root.optionsData`
  (`PauseMenuScene.optionsData`, `MainMenuScene.optionsData`).
- **VERIFIED** `OptionSectionDefinition extends Def_InteractiveElement` has these fields:
  - `label`, `instanceIndex`;
  - `categories:Array` (tab labels);
  - `settings`, a `GameDataProvider(ClassFactory(SettingDefinition))`;
  - `screenClassName`. Its setter maps null or "" to `OptionsSymbols.OPTION_SECTION_SCREEN_CLASS` =
    `"com.bsp.options.OptionSectionScreen"`.
  - `settingsTabs` and `settingsSorted`, which group the settings by `categoryIndex`.
- **VERIFIED** The `GameDataProvider.length` setter (m2732) builds a new `UIDataProvider` and creates N fresh
  definitions through the factory. Elements are reached as `data[i]`.
- **VERIFIED** `SettingDefinition` has `label`, `description`, `descriptionValues`, `categoryIndex`, `categoryName`
  and `locked`. Its widget getters create the widget definition lazily, on first access, from the current `label`:
  - `slider` creates `SliderDefinition` (className `w_slider`);
  - `optionStepper` creates `OptionStepperDefinition` (`w_optionStepper`);
  - `optionStepperBoolean` creates `OptionStepperBooleanDefinition(label, "ui_common_no", "ui_common_yes")`
    (`w_optionStepperBoolean`);
  - `dropBox` creates `w_dropDownBox`, `link` creates `w_link`, and `keyboardBinding` creates `w_keyboardBinding`.

  The default row class is `optionsList_item`.
- **VERIFIED** Widget definition fields:
  - `WidgetDefinition`: `updateGameCallback:String`, `refreshEnabled`, `locked`, `refresh()`.
  - `SliderDefinition`: `minimum`, `maximum`, `interval`, `value`, `displayValue`.
  - `OptionStepperDefinition`: `values:Array`, `selectedIndex`. The setter dispatches `UPDATE`.
  - `OptionStepperBooleanDefinition`: `selectedIndex` 0 = No, 1 = Yes. `values` cannot be set.
- **VERIFIED** Classes that build a row:
  - `OptionsListItem` (`optionsList_item`) reads `widgetDefinition.className` and instantiates the widget, from
    `OptionsElementsPool` or else `DynamicInstance.newInstance`. It sets `widget.definition` and places it at x=275.
  - The widgets are `com.ark.components.widgets.Slider` (track, lift and value label), `OptionStepper` (with
    `WidgetArrow` L/R) and `com.ark.core.Widget`.
- **VERIFIED** The description plate is `com.bsp.options.OptionDetails`. Its `definition` setter writes
  `definition.label` into `_optionLabel.text` and `definition.description` into `_optionDescriptionLabel.htmlText`.
  **No localisation lookup happens there.** The screen header is set as `header.text = currentSection.label`
  (`AbsOptionSectionScreen.data` setter). The text written is therefore already-translated plain text or HTML.
- **VERIFIED** `AbsOptionSectionScreen.data = optionsData` behaves as follows:
  - it takes `currentSection`;
  - it shows tabs only when there is more than one category;
  - it sets `optionsList.data = settingsSorted[tab]` and `header.text = label`.

### Engine side: rows come from decls and are pushed by value path

- **VERIFIED** The option pages are decls, extracted to `artifacts/claude/vroptions/decls/`:
  - `pausemenusectionoptions` and `mainmenusectionoptions` list four sections: CONTROLS, VISUALS, AUDIO and GAMEPLAY.
  - Each `menusectionoption*` decl has `m_settings = { enumItem[ARK_AUDIOSETTING_*] = { m_label, m_widget{m_type,
    m_maximum, m_interval, m_displayValue | m_stringValues}, m_categoryIndex, m_platform } }`.
  - Settings are keyed by C++ enums that have engine-side handlers. A new section cannot be added through decls
    without new engine code.
- **VERIFIED** The engine writes through these paths (strings in .rdata; xrefs at 0x502030, 0x502a60, 0x503100):
  - `optionsData.currentSection` with `label`, `screenClassName`, `categories`, `settings` (0x501bb0);
  - `optionsData.currentSection.settings.data[%d].{optionStepper|optionStepperBoolean|slider|dropBox|keyboardBinding|link}`.

  Member lists:
  - row (0x500e80): `label`, `description`, `categoryIndex`;
  - slider (0x5013c0): `value`, `minimum`, `maximum`, `interval`, `updateGameCallback`, `displayValue`,
    `refreshEnabled`, `locked`;
  - stepper (0x5012d0/0x500ff0): `selectedIndex`, `values`, `updateGameCallback`, `refreshEnabled`, `locked`.
- **VERIFIED** Provider sizing works by writing `length`. The wrapper at 0x27fb0/0x28030 binds `data` and `length`
  and calls `SetAS3Value<int>("length", N)` (0x2a120). Arrays are created with `IggyValueRefCreateArray` (or
  `CreateEmptyObject` when N=0) at 0x294ee and assigned with `IggyValueSetValueRefRS` (0x2a460).
- **VERIFIED** Afterwards the engine calls the root AS3 method `_C_ShowOptionSection` through `arkIggyAS3Method`
  (0x5034a0 binds `_C_ShowOptionSection`/`_C_BackFromOptionSection`; 0x28a90 calls `IggyPlayerCallMethodRS`).
  `PauseMenuScene._C_ShowOptionSection` and `MainMenuScene._C_ShowOptionSection` both run
  `NavigationManager.gotoScreenByLinkage(optionsData.currentSection.screenClassName, container, optionsData, true)`.
  The main-menu version also sends kiscule `GO_TO_OPTIONS_LIST`.
- **VERIFIED** The retail flow:
  1. `OptionsScreen._onOptionSelected` sets `currentSection.label` and `instanceIndex`.
  2. It sends `sendNotification(gameCallbackOnClick = "OnOptionSectionSelected", instanceIndex)`.
  3. The engine fills the section and calls `_C_ShowOptionSection`.

  `CompilationProfile.profile != DEBUG` selects this engine path. The DEBUG branch navigates from AS3 directly.
- Answer to Q1: the rows are neither static AS3 nor return values of external calls. **They are engine-driven Iggy
  variable writes (`IggyValueSet*RS` on value paths), followed by one AS3 method call.**

## Q2. How value changes reach the engine

- **VERIFIED** `Widget.update()` (m4172):
  1. dispatches `WidgetEvent.WIDGET_CHANGE`;
  2. if `definition.updateGameCallback` is set, calls `GameService.sendNotification(updateGameCallback, definition)`,
     which is `ExternalInterface.call(name, definitionObject)`.

  `Slider.updateValue` writes the clamped `definition.value` first. `OptionStepper.updateValue` writes
  `definition.selectedIndex` first (`setSelectedIndex`). So when the notification arrives, **the definition already
  holds the new value**.
- **VERIFIED** The visuals page sets its own callback names, for example `OnUpdateVSync`, `OnUpdateAA` and
  `OnTryResolution` (0x50b9a0).
- **VERIFIED** The section-level notifications are `OptionsCallback` constants:
  - B pressed: `OnBackFromSettingsList(instanceIndex)`.
  - X pressed then confirmed: `OnRestoreSettings(instanceIndex)`.

  In retail, AS3 does **not** navigate back itself. The engine handler calls `_C_BackFromOptionSection`, and the
  AS3 call in `_BPressed` is DEBUG-only.
- **VERIFIED** The engine handlers registered by 0x5173c0 through `0x2cd20(file, name, fn, owner)` are:
  - OnBackFromSettingsList → 0x517110
  - OnRestoreSettings → 0x5172d0

  The handler signature is `fn(owner, arkIggyFile*, idList<IggyDataValue>* args)`. The argument number is read as an
  F64 at `IggyDataValue+0x10`. These handlers only act on section indices 0..3; any other index returns without
  doing anything.
- **VERIFIED** The hub at 0x34800 never answers synchronously. It looks up the handler, queues the call through
  `arkIggyDeferredExternalInterfaceHandler::RegisterCall` (0x2b7e0; 0x20-byte `IggyDataValue` per argument, count at
  `call+0x10`, args at `call+0x18`) and always returns 1. Names with no registered handler are dropped silently.
  The engine therefore never returns typed data to AS3. It always pushes data.

## Q3. Design

### Iggy API in the exe

**VERIFIED** via the Iggy API name strings each function passes to its error reporter:

| RVA | Function | Calling convention seen at engine call sites |
|---|---|---|
| 0x1009960 | IggyPlayerCreateFastNameUTF8 | `(Iggy*, const char*, -1) -> IggyName` (0x28b70) |
| 0x1009bf0 | IggyValueGetS32RS | `(path, IggyName, const char* sub, S32* out) -> 0 ok`; accepts int atoms and integral Numbers |
| 0x1009b70 | IggyValueGetBooleanRS | same shape |
| 0x1009c70 | IggyValueGetStringUTF8RS | |
| 0x1009b00 | IggyValueGetArrayLength | |
| 0x100a3e0 | IggyValueSetS32RS | `(path, IggyName, NULL, S32)` (0x2a120) |
| 0x100a310 | IggyValueSetBooleanRS | `(path, IggyName, NULL, bool)` |
| 0x100a360 | IggyValueSetF32RS | `(path, IggyName, NULL, float)` |
| 0x100a460 | IggyValueSetStringUTF8RS | `(path, IggyName, NULL, const char*, S32 len)` (0x2a720) |
| 0x100a5c0 | IggyValueSetValueRefRS | `(path, IggyName, NULL, IggyValueRef)` (0x2a460) |
| 0x1009f00 | IggyValueRefCreateArray | `(Iggy*, S32 len) -> IggyValueRef` (0x294ff) |
| 0x100a070 | IggyValueRefCreateEmptyObject | `(Iggy*) -> IggyValueRef` |
| 0x100a210 | IggyValueRefFromPath | |
| 0x100c4c0 | IggyPlayerCallMethodRS | `(Iggy*, IggyDataValue* result, path, IggyName, S32 n, IggyDataValue* args)` |

Identified by usage pattern (**VERIFIED behaviour, name INFERRED**):
- 0x1009a90 IggyPlayerRootPath `(Iggy*)` returns `Iggy+0x2950`. The file loader 0x2c620 stores it as
  `arkIggyFile+0x1780`, and the engine uses it as the base of `optionsData...` paths.
- 0x1009de0 IggyValuePathMakeNameRef `(IggyValuePath* out, parent, const char*)` (asserts "Tried to
  IggyValuePathMakeNameRef with a NULL parent").
- 0x1009da0 IggyValuePathMakeArrayRef `(out, parent, S32)`. 0x29a60 calls it for each `[n]` path token.
- `sizeof(IggyValuePath)` is 0x28 (stride of the engine's path stacks).
- 0x100a170 is most likely IggyValueRefFree `(Iggy*, IggyValueRef)` (INFERRED).
- The hub's `player` argument is the `Iggy*`. `*(Iggy+0x78)` is its `arkIggyFile` (0xc19b0) (INFERRED).

**VERIFIED** Re-entry rule. The string "Attempted to re-enter Iggy from callback or second thread" is referenced
only by `CallMethodRS`, `TickRS`, `DispatchEventRS`, `Draw`, `GotoFrame`, `InitializeAndTick` and a few setup calls.
**None of the value Get/Set/Path/Ref functions reference it.** "Can't set field of IggyPlayerCallbackResultPath"
(0x100bc30) shows Iggy expects value writes inside callbacks.

Conclusion: inside the hub hook the mod **may read and write values** but **must not call AS3 methods** (INFERRED
from the guard placement). That is why AS3 itself must perform the navigation (edits below).

### Data flow (all paths start from IggyPlayerRootPath(player))

1. **Open.** The VR Settings click (patched) runs `sendNotification("VR_SETTINGS_EVT")`, then
   `root._C_ShowOptionSection()`. During the notification the hub hook writes the following synchronously, in this
   order, with the same calling convention as the engine (`IggyName` from `CreateFastNameUTF8`, sub-name NULL):
   - `optionsData.currentSection`:
     - `label` = "VR Settings";
     - `instanceIndex` = 100 (sentinel; engine sections are 0..3);
     - `screenClassName` = "". The setter maps this to OptionSectionScreen. It must always be written, because a
       previous visit may have left `GraphicOptionSectionScreen` there.
   - `currentSection.settings.length = N`. This creates N fresh `SettingDefinition`s and discards stale ones.
   - For each row i, on `settings.data[i]`:
     1. Write `label`, `description` (HTML-escaped, shown in the plate) and `categoryIndex` (0, or a tab index).
        `label` must be written **before** the widget getter is touched.
     2. Write the widget members:
        - Slider: `.slider` → `minimum`, `maximum`, `interval`, `displayValue`=true, `value` (integers),
          `refreshEnabled`=true, `updateGameCallback`="VR_SETTING_CHANGED".
        - Choice: `.optionStepper` → `values` = new array (`RefCreateArray(player,n)`, `SetValueRefRS`, then each
          `values[k]` = string by path), then `selectedIndex`, `refreshEnabled`, `updateGameCallback`.
        - Toggle: `.optionStepperBoolean` → `selectedIndex` (0/1), `refreshEnabled`, `updateGameCallback`.
        - Optional `locked`=true for unavailable rows.
   - Optional tabs: give rows several `categoryIndex` values and write `currentSection.categories` = array of tab
     labels.

   AS3 then navigates (VERIFIED code path). Iggy draws the native list, widgets, highlight, fonts and plate.
2. **Change.** `Widget.update` sends `VR_SETTING_CHANGED(def)`. The hook ignores the argument and reads the N values
   back by path (`GetS32RS` on `slider.value`/`selectedIndex`), applies them to `VrPreferences`, and returns 1
   without forwarding.
3. **Back (B).** `_BPressed` sends `OnBackFromSettingsList(100)`. The hook checks
   `GetS32RS(currentSection.instanceIndex) == 100`, commits and saves the values, and returns 1. The patched AS3 then
   calls `root._C_BackFromOptionSection()`: the param is optional (m4023 flags 0x8). For indices below 100 the hook
   forwards the call and behaviour is unchanged.
4. **Restore (X).** `OnRestoreSettings(100)`: the hook resets to defaults and rewrites the values. `selectedIndex`
   setters dispatch `UPDATE`, so steppers refresh. `SliderDefinition.value` does not dispatch an update, so the hook
   toggles `locked` to force `Widget.applyDefinition` (INFERRED; check live). Then it returns 1.

### Same-size AS3 edits (pin with full-movie SHA256 like `menu_patches.hpp`)

All indices below are **VERIFIED** from the constant pools. All max_stack values are sufficient.

| Movie | Method (code offset, len, max_stack) | Edit |
|---|---|---|
| ui/corvo_pause_menu.iggy | PauseMenuScreen._onStatsClicked (480279, 37, 5) | after `4f f5 24 01` at +12 insert `d0 66 ee 2a 4f b7 1e 00` (`getlocal0; getproperty root(mn 5486); callpropvoid _C_ShowOptionSection(mn 3895) 0`), rest stays `47` |
| ui/emily_pause_menu.iggy | same (476913, 37, 5) | `d0 66 cf 2a 4f 98 1e 00` (root 5455, _C_ShowOptionSection 3864) |
| ui/main_menu.iggy | MainHomeScreen._onQuit (501197, 144, 7) | after `4f bc 24 01` insert `d0 66 cd 2a 4f c7 1e 00` (root 5453, _C_ShowOptionSection 3911) |
| ui/lib_options.iggy | AbsOptionSectionScreen._BPressed (489391, 55, 3) | file offset 489418, 16 bytes: old `60a00366cf0460a20366a721140b0000` (`profile != DEBUG` → return) becomes new `020202d066f61b66950d2464150b0000` (`if (_data.instanceIndex < 100) return;`, then the existing DEBUG branch `root._C_BackFromOptionSection()`) |

Notes on these edits:
- All three click methods are instance methods of display-object screens, so `root` resolves to the scene
  (**VERIFIED** owner classes; scene-as-root is INFERRED and matches the authored DEBUG pattern).
- The `_C_ShowOptionSection` multinames are public QNames.
- The `iflt` keeps the original jump target, 43+11 = returnvoid at 54.
- The `lib_options` edit does not overlap the existing glyph row at offset 293698. Pin it after that row.

### Hooks

- The existing hub hook at 0x34800 extends its name filter to `VR_SETTINGS_EVT`, `VR_SETTINGS_OPEN_REQUEST`,
  `VR_SETTING_CHANGED`, and `OnBackFromSettingsList`/`OnRestoreSettings` **only when the instanceIndex is 100**.
- The existing movie-load hook at 0x2E4D0 applies the four edits.
- New code: a pinned Iggy-API table. Verify the prologue bytes of each RVA above, and refuse both the edits and the
  page if any check fails. Without the data, `_C_ShowOptionSection` would open a stale or null `screenClassName`.
- The GDI `settings_panel_image.hpp` path can be retired once the page works live.

### Risks and live checks needed (all INFERRED)

1. Writing values inside the external callback is allowed: the setters are not re-entry guarded. This is the main
   live check. Fallback: register a real engine-deferred handler with `0x2cd20(arkIggyFile*, name, fn, owner)`.
   The file comes from `*(player+0x78)`. Do the writes plus `IggyPlayerCallMethodRS(_C_ShowOptionSection)` from that
   handler, exactly as the engine does. Lifetime and unregistration still need RE.
2. `IggyValueSet*RS` on accessor properties (`length`, `slider.value`, `selectedIndex`, `screenClassName`) should
   invoke the AS3 setters. The engine relies on exactly this.
3. The widget pool: the page opens without `OptionsElementsPool`. `OptionsListItem` falls back to
   `DynamicInstance.newInstance` (VERIFIED code). Check that this works when the user has never opened Options this
   session.
4. Main menu: `_C_ShowOptionSection` sends `GO_TO_OPTIONS_LIST` to the 3D background. Back goes through
   `NavigationManager.backward()` without the engine's options state, so check the camera returns to the home pose.
5. Slider values must be integral: `GetS32RS` rejects non-integral Numbers (VERIFIED). Use integer
   `minimum/maximum/interval` and scale internally. Strings are UTF-8 and the plate uses `htmlText`, so escape them.
6. All RVAs and offsets are pinned to this build and these movie hashes. Re-verify after any game update.
7. Headset readability and comfort of the page cannot be established statically.
8. **VERIFIED (sim v392/v393, 2026-10-13): at most 10 rows.** `OptionsListContainer._setPoolContainer` sets
   `rowViewCount` 10 (12 for the small keybinding item), and `maxItems = min(ghostIndex+1, lastIndex+1)`. On our page
   the list never scrolls past the 10th row: the 11th row ("Test: unlock all") could never be selected.
   `OptionSectionDefinition._getMapping` starts a new tab (`settingsSorted[categoryIndex]`, labelled
   `categories[categoryIndex]`) whenever a row's `categoryIndex` changes. Tabs do render with the game's LB/RB glyphs.
   But in v393, LB/RB (the grips) mostly reset the list to its first row instead of switching tabs. The left grip
   reached the second tab only once, so tabs are not usable. The page therefore stays at one tab of 10 rows
   (`NativeOptionsPage::max_rows`, asserted in `tests/vr_settings_native_check.cpp`). Test unlock stays on the
   launcher's Tools page and the `unlock` test command.

## Q4. Fallback assets (only needed if the live check of risk 1 and its fallback both fail)

- **VERIFIED** Each movie's bitmaps are in an external, nameless raw texture pack next to it. Latest entries are in
  `game1_patch.index`:
  - `ui/lib_options.raw.iggytex` (2,716,816 B)
  - `ui/lib_widgets.raw.iggytex` (956,592 B)
  - `ui/lib_common.raw.iggytex` (4,030,192 B)
  - `ui/corvo_pause_menu.raw.iggytex`, `ui/main_menu.raw.iggytex`, and so on.

  The pack header has no names. Textures are referenced by index from the `.iggy` bitmap table, which is not parsed
  here.
- **VERIFIED** Fonts come from `ui/lib_loc_<lang>_font.iggy` (English: 1,453,747 B) through the `com.bsp.font`
  classes `TitleFont`, `NormalFont`, `NormalFontSmallCaps`, `ItalicFont`, `HandWrittenGrungeFont` and
  `HandWrittenElegantFont` (`Dis2FontList`).
- **VERIFIED** Visual symbols on the page (lib_options linkage):
  - rows and highlight: `optionsList_item` / `optionsList_item_s` (MenuButton states);
  - tabs: `optionListTab`;
  - sliders: `w_slider` (`slider_track`, lift, value label);
  - steppers: `w_optionStepper` / `w_optionStepperBoolean` (`WidgetArrow`);
  - scroll bar: `Dis2ScrollBar` / `Dis2ScrollBarLifter`;
  - description plate: `OptionDetails` (`OptionDetailLabel`, `OptionDetailDescriptionLabel`);
  - background: `ng_bkgdGrad2` and the particle layers `o_blades`, `o_glass`, `o_absBlades`, `o_section_*`.
- Mapping these symbols to texture indices would need an Iggy shape and bitmap table parser (not done). Given the
  FEASIBLE verdict, letting Iggy render the real page is preferable to compositing extracted textures.
