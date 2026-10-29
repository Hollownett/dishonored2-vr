# Scripted interaction and item gaps after the Hypatia cure prompt

The v678 fix came from a specific native data path. This note maps every other scripted item
or interaction that uses the same or an adjacent path, so the next headset session can test
them deliberately.

Evidence is from the installed Steam build
`b671f8249422862bf557d00aab2f4bb671ed4515cc10ad8a8a9decad84516180`:

- all 36 `*.entities` map files in `game1/2/3_patch.index`, decompressed read-only
  (`artifacts/scripted-interaction-research/entity-findings.json`);
- all 91 `.tutorial` declarations and 897 `.inventoryitem` declarations
  (`artifacts/scripted-interaction-research/tutorials.json`, `items.json`);
- the existing mod paths in `src/`.

## The exact Hypatia path

`models/campaign/institute/cure_hypatia.tutorial` is a **Kiscule HUD tutorial** whose title,
description and content are `ui_sm_cure_hypatia`. It is shown by
`arkKisculeNodeHUDTutorialMessage` node `1567231016`, driven by
`arkKisculeNodeNPCInteractionNotification` node `1100181816`.

**That notification node appears exactly once in the whole game**, in `institute_p_lowchaos`
and `institute_p_highchaos` (the same authored node in both chaos variants). No other map has
an `arkKisculeNodeNPCInteractionNotification`. So no other item has the *identical* trigger.

## Adjacent paths that can fail in the same way

The same failure shape is "a special interaction whose prompt/UI is not an ordinary world
InteractionWindow". The game data has these explicit scripted messages:

| Map | Message / tutorial | Interaction or item | Risk in VR |
|---|---|---|---|
| Dust District (low/high) | `ui_im_jindoshlock` | Jindosh lock (ring puzzle) | **High**: a native 3D puzzle; the mod has no world-space pointer/ring input. The message may show, but the lock may be inoperable. |
| Curator / Jindosh mansion | `ui_lensassembly_missing` | Ashworth lenses / lens assembly | **High**: mission item plus a special assembly; not covered by the mod's interaction UI. |
| Mad Inventor house | `ui_clockworkpanel_inoperable` | rewire tool / clockwork panel | **Medium-high**: `TYPE_REWIRE_TOOL` item; the mod has no rewire-tool equip/use path. |
| Bunker (low/high) | `ui_crankwheel_missing` | crankwheel | **Medium**: world usable; ordinary Use may work, but the missing-wheel message/trigger is scripted. |
| Bunker (low/high) | `TUTORIAL_Timepiece_title` | timepiece | **Medium**: mission-critical power/item; no `timepiece` reference in the mod. |
| Void 01 | `ui_im_useheart` | the Heart | **Low-medium**: the Heart is held like a grenade in the mod; the tutorial message may not show. |
| Dunwall, EOTW, Mad Inventor, Palace | `ui_sm_window_locked` | locked windows | **Low**: the action is ordinary Use; only the message can be missed. |
| Generic tutorial set | `TUTORIAL_pickpocket` | pickpocket | **Medium**: action rides the interaction sensor, not a dedicated prompt; not headset-tested. |
| Generic tutorial set | `TUTORIAL_bonecrafting` | bonecharm crafting | **Medium**: workbench/journal UI; the mod captures the journal, not a dedicated crafting screen. |

The complete non-empty Kiscule tutorial list is in
`artifacts/scripted-interaction-research/tutorial_sources.json`; the 48 unique entries include
`food`, `hidebodies`, `potions_health`, `assassination_takedown`, `spyglass`, `catfall`,
`runespowers`, `playyourway_01`, `stealth` and the item-specific rows above. Those are ordinary
tutorial triggers (pickup/trigger/use), not the Hypatia NPC-interaction notification.

## Why these are plausible gaps in the current mod

1. **World UI coverage is deliberately narrow.** `d2vr::WorldUiKind` is only
   `{none,wheel,notes,journal}` (`src/native_world_ui.hpp:11`), and the recognized paths are
   only `ui/quick_access_wheel.iggy`, `ui/notes.iggy`, `ui/journal.iggy` and the two black
   market movies. A Jindosh lock, lens assembly or a safe dial using another movie has no
   captured world panel.
2. **The pointer is a menu pointer, not a world pointer.** `native_menu_input` is used for the
   pause/settings menus and the wheel. There is no ray/cursor into a native 3D puzzle.
3. **No rewire/timepiece/lens/Jindosh code.** A source search finds `UseHeart` and `Crank` as
   player FSM states in `full_body_driver.hpp`, but no handling of `rewire`, `timepiece`,
   `lens`, `crankwheel` or `jindosh` interaction or UI.
4. **Mission items are not equipped like weapons.** `syringe`, `cu_ashworth_lenses` and
   `rewire_tool_item` are `TYPE_MISSIONITEM`/`TYPE_REWIRE_TOOL`; the mod's hand/equip path is
   built for weapons, gadgets, notes and bodies.

## What should already work

- Standard world Use: doors, lever, keyhole, loot, notes, coins, food, potions, safes with an
  ordinary Use prompt.
- Talk (Y), loot (v675) and the non-lethal choke (v677/v678).
- Grenades, stun mines, spring razors (v673), the Quick Access Wheel, journal and notes.
- Ordinary tutorial messages: v678 publishes visible `hud.iggy` `_tutorialMessage` /
  `_informationMessage` elements as tutorial art; the Kiscule trigger has to fire.

## Headset test plan

Test these in order; the first two are the likely blockers:

1. **Mission 3, Hypatia** — load a save before she dies, with the crafted syringe. Confirm the
   v678 card. This also proves the fallback path.
2. **Mission 4, Clockwork Mansion** — Jindosh lock. Can you rotate/select the rings with the
   controllers? Does `ui_im_jindoshlock` appear?
3. **Mission 5, Dust District / Curator** — lens assembly and the Jindosh lock solution.
4. **Mission 4, Mad Inventor house** — pick up the rewire tool, equip/use it on a clockwork
   panel.
5. **Mission 6, bunker** — crankwheel and timepiece.
6. **Any locked window** — `ui_sm_window_locked` message.

For each failure, capture `artifacts\xr-live-status.json` and, if present,
`native-prompt-*`, `native-hud-child-*.json/.rgba`, and the world UI paths from
`native-menu-input.csv`.

## Bottom line

There is **no second Hypatia-style NPC-interaction notification** in the game data. The next
risks are the other scripted UI/item interactions, especially the **Jindosh lock and lens
assembly**, the **rewire tool/clockwork panel**, and the **timepiece**. They are not proven
broken; they have no dedicated VR path in the current code and need direct headset tests.
