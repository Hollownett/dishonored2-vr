# Corpse loot, the native action offsets, and Vasco's note (v674)

## The problem

The note with Vasco's safe code is `ld_npc_belt_scroll_case_01`, a world `arkPickup`
(`cpntPickable_Simple` + `cpntLoot`, item `note_is_vascosafecode.inventoryitem`) spawned by the
`ld_vasco` spawner's `m_specialLoot` and shown on his belt (map
`maps/campaign/institute/institute_p_lowchaos.entities`). It is not a loose item on the floor:
the only interaction the game offers near it is the dead body itself.

In v672 a grip on that body did nothing for the note. The game did offer the body's action (the
native HUD prompt read `Vasco (dead)`), but the mod's corpse gate (v637 / ruling V1: "bodies are
carried by hand only") cleared every Use whose target was a dead or unconscious NPC
(`body_use_blocked`). The grip then went to the physical lane and held a corpse bone, so the
game's **Loot** action never ran.

## What the engine does (pinned Steam build 17576867)

`arkCpntNPCInteraction`'s selector is its vtable `+0xd0` slot, RVA `0x747840`. It fills the two
action slots the interaction sensor offers (`sensor+0xa8` primary, `sensor+0xb0` secondary):

| declaration offset | action | notes |
|---|---|---|
| `+0x70` | `m_carryCorpse` | secondary for a dead/unconscious NPC |
| `+0x98` | `m_loot` | **primary** for a lootable body (`PLAYER_STATE_LOOT_OBJECT`) |
| `+0xc0` | `m_pickpocket` | living NPCs |
| `+0xe8` | `m_talk` | primary Talk |
| `+0x110` | `m_busyTalk` | secondary Talk |
| `+0x138` | `m_assassination` | |

The order matches the shipped `cpntNPCInteraction` declaration and the decompiled selector's
cascade (Talk's `+0xe8`/`+0x110` and the corpse's `+0x70` were already the mod's known offsets).
The engine's Use acts on the **primary** offer, and `m_loot`'s player state is
`PLAYER_STATE_LOOT_OBJECT`. So a dead body with loot offers Loot as its primary action and Carry
as its secondary one: a Use press loots it; the shoulder carry is the hold/secondary route.

Live evidence from the v672 session that reproduced the bug
(`%LOCALAPPDATA%\DishonoredVR\artifacts`, 2026-10-29 09:27-09:35):

- `native-interaction-targets.csv`: the stage-0 candidate in the note area is Vasco's body
  (spawn `670180380`), with **two** eligibility calls per cycle (`eligibility_calls=2`) and no
  native refusal. Both offers were therefore tested: the primary Loot and the secondary Carry.
- `native-hud-child-0.rgba` is the captured native prompt for that body: `Vasco (dead)`.
- `native-grab-acquisition.csv` / `native-physical-grab.csv`: every grip over the body held a
  corpse bone (entity `670180380`, dynamic ragdoll bodies), i.e. the physical lane.
- The status ended with `body_use_blocked: 758`: the v637 guard cleared the body's Use on those
  frames.

## The fix

1. `NativeInteractionAim::loot_offered(owner, now)` validates the game's live Loot receipt for a
   body (the same `native_talk_offer` identity/owner/spawn/sensor checks the Talk route uses,
   with `loot_offset = 0x98`). `target_bounds` classifies such a body as `PalmKind::loot`, so the
   grip-alone decision leaves the corpse branch.
2. `body_carry_prompt` keeps the v637 guard only for a body offered **only** its Carry action.
   A body whose primary action is Loot is looted: the Use press reaches the game, and the world
   prompt (the native Loot art) is shown beside the body instead of being suppressed. The v637
   hand-carry rule still holds for every body without a live Loot offer, so nothing can shoulder
   a corpse.
3. `body_loot_passes` counts the frames where a lootable body's Use was allowed through; it is
   in `xr-live-status.json` next to `body_use_blocked` for the next headset run.

The grip decision itself (v673) also prefers Use when the hand's physical pick is a bone of the
same lootable body (`grip_decision.hpp`, ruling for a corpse whose own native Loot target is the
hand's target). A physical prop on the palm still wins over the body, as before.

## v675: the receipt is not the offer

The first v674 headset test still blocked the body's Use (`body_loot_passes: 0`,
`body_use_blocked: 578`) while the game's prompt remained on the body. The full
`native_talk_offer`-style Loot receipt never validated. Its long identity chain is built for Talk
(the selected reference → link → selected owner chain, the component/selector identity, and the
sensor/machine/interaction ownership); one link is not satisfied for a body's Loot interaction.

The engine's actual action offer is simpler and still readable: the interaction sensor stores the
selector's two descriptors at **`sensor+0xa8` (primary)** and **`sensor+0xb0` (secondary)**. The
selector writes Loot (`decl+0x98`) as the primary and Carry (`decl+0x70`) as the secondary for a
dead NPC. v675 records those two pointers (resolved to declaration offsets) in every stage-0
cycle and accepts the raw primary Loot offer, revalidated against the live sensor and the hand's
player. The strict receipt is still accepted when it validates, but it is no longer required.

`xr-live-status.json` now carries a `corpse_loot` array per hand with `primary`/`secondary`
offsets, `raw` and `receipt`. A body whose primary is `112` (`0x70` Carry) is not looted and
keeps the v637 hand-carry guard; a primary of `152` (`0x98` Loot) is looted. This distinguishes
"the engine offered Carry only" from "the engine offered Loot but the mod's receipt failed"
without needing the trace CSVs.

## Limits and what was not verified

- Only the native Loot **primary** offer is admitted. A body whose Loot eligibility fails (for
  example a body the engine no longer considers lootable) keeps the v637 guard and is handled by
  hand; the mod does not fabricate a loot action.
- The engine's loot event and its map scripting are untouched: no inventory item is granted by
  the mod, no engine function is called, and no interaction field is written. The mod only lets
  the game's own Use press through.
- The v674 candidate was run in a headset and still failed (`body_loot_passes: 0`,
  `body_use_blocked: 578`), which is why the raw primary offer is used. The v675 candidate has not
  been run in a headset yet.
