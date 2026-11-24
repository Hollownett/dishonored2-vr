# R1 - Dishonored: Death of the Outsider (DOTO) / D2 mechanics reference for the VR port

Method and limits. Web research only. Fandom (dishonored.fandom.com, dishonored-archive.fandom.com), breezewiki, dishonored.wiki, web.archive.org,
PCGamingWiki, GameFAQs and Primagames refused automated fetches (HTTP 402/403/Anubis/blocked). I did not try to bypass those blocks. Fandom content below
therefore comes from **search-engine summaries of the fandom pages** (cited by the page URL the search returned) plus pages that did fetch (Bethesda, ludo.guide,
Steam threads). "UNVERIFIED" = fewer than two independent sources and no primary source. Nothing here was checked in-game.

Page tags: [BETH] Bethesda announcement (primary) https://bethesda.net/en-US/news/dishonored-death-of-the-outsider-new-powers-gadgets-gear ;
[LUDO-n] https://www.ludo.guide/guide/dishonored-death-of-the-outsider/dishonored-death-of-the-outsider-part-n ;
[FW-x] https://dishonored.fandom.com/wiki/x (via search summary only).

## 1. Billie's powers

All four are "Void" powers granted by the Sliver of the Eye / Black Shard Arm in mission "Follow the Ink"; energy regenerates on its own, no runes
[FW-Semblance] https://dishonored.fandom.com/wiki/Semblance, [LUDO-43] .../part-43. No Heart, no runes, no mana elixirs.

| Power | Verified behaviour | Energy | Range / LOS | Cast / cancel | Upgrades / charms | Status |
|---|---|---|---|---|---|---|
| Displace | Place a marker only Billie sees (enemies cannot), later swap places with it. Marker "behind her, down a hall, or on a rooftop" [BETH]. Teleporting into an occupied spot "interpenetrates": kills the occupant and damages Billie [LUDO-43]. Marker despawns when she changes map area; a third marker replaces the oldest (with Third Eye) [FW-Displace] https://dishonored.fandom.com/wiki/Displace | No cost found. Using "too much Void energy in spirit form" blocks Displace for a while [FW-Foresight] https://dishonored.fandom.com/wiki/Foresight | Marker only needs a clear line to Billie, not her gaze [BETH, FW-Displace]. Max distance: "short-range", number UNVERIFIED | Hold activation button = aim, release = place marker (even with Rapid Displacement equipped, aiming still places) [FW-Displace]. Swap: a separate press. PS: "tapping L2 shortly after placing performs a rapid teleport" [LUDO-43]. PC swap key / gamepad cancel: UNVERIFIED | Third Eye: "extra target marker when using Displace and Foresight" [FW-Bone_Charms] https://dishonored.fandom.com/wiki/Bone_Charms ; the Displace page says it allows two Displace markers. Wiki pages disagree on wording. Rapid Displacement charm: travel to the marker immediately | Two-marker Third Eye: 2 wiki pages, same wiki - treat as likely, not confirmed |
| Foresight | Pauses time, Billie leaves her body as a spirit, scouts, can place a Displace marker, marks targets that stay visible through walls after Foresight ends [BETH, LUDO-43]. Spirit cannot interact with or pass through people/objects; moves through vents; there is a height cap [FW-Foresight, Engadget/others via search]. Can end any time, returning to body. Small activation delay before you can move [FW-Foresight]. Max 4 marks at once, oldest dropped [FW-Foresight] | Free to activate, steady drain, about 18 s; about 24 s with Void Conduit charm [FW-Foresight] | n/a (free flight). Walls block the spirit; marks are seen through walls | Button scheme for hold/tap/cancel: UNVERIFIED. Speed of spirit: UNVERIFIED | Void Conduit (duration), Third Eye (extra marker) | duration: FW only (single source) -> UNVERIFIED-ish |
| Semblance | Billie takes the face/voice of a person; works on conscious (knocks them out) or unconscious targets, not dead [FW-Semblance]. Each NPC face can be stolen once [LUDO-43]. Illusion breaks if she attacks or steals; arc pylons and clockwork soldiers are not fooled; wolfhounds still smell her [FW-Semblance]; disguise ends when guards are alerted [LUDO-43]. Bodies seen next to the disguised Billie raise suspicion | Drains only while moving [LUDO-43] | No ray: engine target is the NPC held in a choke (your RE notes agree) | Cast by the power button on a nearby target; exact button UNVERIFIED | Mobile Mimicry, Void Leech (slower drain; Void Leech refills on new faces), Familiar Scent (wolfhounds) [LUDO-43] | LUDO + FW agree |
| Void Strike | Charged attack that pushes enemies away, or assassinates a surprised target [BETH]. It comes from the **Twin-bladed Knife** (Billie's blade, not a "sword"); the knife dissolves into shards and merges with her arm when unused [FW-Twin-bladed_Knife] https://dishonored.fandom.com/wiki/Twin-bladed_Knife . After the Outsider's fall the knife loses Void Strike [same] | cost UNVERIFIED | melee | charge input UNVERIFIED | none found | BETH + FW |

Correction to DOTO-VR.md §3: the design table says Void Strike is a blow with "the sword"; the weapon is the Twin-bladed Knife (the engine path name `knife_midnight.def` in your notes fits). Void Strike "breaks weak barriers" is not in any source I found (UNVERIFIED). Foresight ~18 s is now confirmed by one wiki page (not two).

### Original Game+ (Original Game Plus)
- Replaces Billie's own powers with fully upgraded Dishonored 2 powers: Blink, Dark Vision, Domino. Sources: Digital Trends review https://qa2.www.digitaltrends.com/gaming/dishonored-death-of-the-outsider-review ; CheatCC https://www.cheatcc.com/?p=96322 (both via search summary). One summary says Displace/Foresight are the ones removed.
- Bethesda's own wording is looser: mix Billie's powers with "a curated selection" of Emily/Corvo powers [BETH-search summary]. Whether Displace/Foresight remain selectable, and how the three D2 powers are bound (same slot as Billie's? wheel?) = UNVERIFIED. Test in game.
- Still no runes; Void energy auto-regenerates (Digital Trends, same link).

## 2. Ranged weapons and ammo

Billie has **no crossbow and no pistol**. Her only ranged weapon is the **Voltaic Gun**, an electrified launcher strapped to her wrist [BETH]; it resembles the Howlers' wristbow with a manufactured battery [FW-Voltaic_Gun talk page] https://dishonored.fandom.com/wiki/Voltaic_Gun . Ammo is made from pistol bullets and crossbow bolts the player finds (same page; also Bullets/Crossbow_Bolts pages).

| Ammo | Lethal? | Notes | Unlock cost | Source |
|---|---|---|---|---|
| Voltaic Shot | lethal | default; muffled, not silent | default | [LUDO-44] |
| Electrical Burst | non-lethal in normal fire (shocks and knocks out); kills when fired with Charged Shot; stuns clockwork soldiers briefly | dart-like | 450 coins (Black Market upgrade) | [LUDO-44], https://dishonored.fandom.com/wiki/Electrical_Burst |
| Makeshift Bolt | lethal (improvised) | lets nails, scalpels, fountain pens be used as ammo; nails always break, pens/scalpels sometimes survive; 5 projectile types, most recent pickup loads last | 500 | [LUDO-44], FW talk page |
| Incendiary Bolt | lethal (sets on fire) | like crossbow version | 400 | [LUDO-44], https://dishonored.fandom.com/wiki/Incendiary_Bolts |
| Charged Shot (upgrade) | modifier: more damage, special effects; hold limit, then releases involuntarily and wastes ammo; needed for Hagpearl headshot knockouts and button pressing | | 700 | [LUDO-44], https://dishonored.fandom.com/wiki/Hagpearl |
| Hagpearl | non-lethal distraction round, finite, not sold | | found | Hagpearl page above |
| Reload Speed (upgrade) | | | 300 | [LUDO-44] |

No "sleep dart", "howling bolt" or "stun bolt" exists in DOTO under those names (searched; not found). Per-mission availability of each ammo: the Black Market opens in mission 2 (The Bank Job; Black Market door code via Foresight/rat tunnel [LUDO-44]) and ammo unlocks are bought, not mission-gated, so "which mission each becomes available" = from the first Black Market visit once you have the coins (UNVERIFIED beyond that). **How ammo is switched (weapon wheel, quick swap, button)**: not found in any fetchable source - UNVERIFIED. Gamepad: "L2: use a power or fire the left-hand weapon", D-pad quick-select, L1 hold = quick wheel [LUDO-1] https://www.ludo.guide/guide/dishonored-death-of-the-outsider/dishonored-death-of-the-outsider-part-1 (PS4 Preset A).

## 3. Gadgets

| Gadget | Kind | Behaviour | Cost / unlock | Source |
|---|---|---|---|---|
| Hook Mine | thrown mine | **Mode toggle: hold the use button of the mine for a moment** to switch lethal/non-lethal; lethal glows red, non-lethal blue (fandom summary). Guides: hold L2 (PS) / LT (Xbox) puts it in lethal mode [Hooked achievement guides, https://www.magicgameworld.com/dishonored-death-of-the-outsider-hooked-achievement-guide/ says "set the mine to lethal" but gives no button]. Mode is chosen before throwing (Steam thread). Thrown mine **sticks to the first object it hits**; once attached to a solid surface it grabs the first potential victim in range and pulls them in (magnetic hooked ball reels the target back). Non-lethal: pulled victim is held and shocked unconscious / concussive blast; lethal: dismembers, leaving part of the body on the mine. Two mines close together pull victims apart. A mine attached to a person grabs the nearest other person and pulls them together. Walls, floors, ceilings per [BETH]; throwable objects such as bottles also work (fandom). "Place directly on enemies": one guide says yes, UNVERIFIED (one source, no URL surfaced). Pick-up/disarm: not found. Salvage Mines upgrade combines three used husks into a new mine (implies husks are collectable) | 60 coins; Hook Mine Reach 300 (bigger pull radius); Salvage Mines 500 | https://dishonored.fandom.com/wiki/Hook_Mine ; Steam https://steamcommunity.com/app/614570/discussions/0/1520386297689203802 ; [BETH]; [LUDO-44] |
| Springrazor | lethal mine | releases shrapnel, mostly silent; Reach 300, Double Release 450 | 60 | [LUDO-44], Black_Market_Shops |
| Stun Mine | - | **Not a DOTO gadget** in any source (DOTO mines: Springrazor, Hook Mine) | - | - |
| Grenade (standard) | thrown | area damage; Impact Grenades upgrade makes them explode on contact with living targets (400) | 70 | [LUDO-2], [LUDO-44] |
| Sticky Grenade | thrown | anchors to the target/surface; Extender 400 | 100 (450 upgrade) | [LUDO-44] |
| Hyperbaric Grenade | thrown, non-lethal | compacted-air blast launches groups; Muffler 400 | 80 (400 upgrade) | [BETH], [LUDO-44] |
| Rat Whispers talisman | passive | hear Karnaca's rats (clues) | story | [BETH] |
| Hagpearl | thrown/fired distraction | see ammo | | |

Notes for the VR port: the toggle is a **hold of the gadget button**, not a separate ammo key (fandom summary + two achievement-guide hints agree: UNVERIFIED on exact PC key; DOTO has no discovered PC "alternate fire" binding in any source). The mine is **thrown**, not planted by hand, and sticks to first contact; kills via Hook Mine on moving NPCs work (Hooked: send someone 40 m; Steam "Workplace Harassment" trick places one mine high on a lamp post and a second below) [Steam thread above].

## 4. Dishonored 2: Emily's Far Reach / Blink cancel

- Aim: **PC** hold right mouse button, a blue guide mark shows, release to execute [Steam https://steamcommunity.com/app/403640/discussions/0/217691032437318761/]. **Gamepad**: hold left trigger to aim, release; a thread contrasts holding to aim a teleport with a brief press for pull [steam search summary, https://steamcommunity.com/app/403640/discussions/0/3182216552784956082/ - UNVERIFIED].
- Cancel: the Dishonored wiki archive pages for Blink and Far Reach say "Press [F] to cancel" (F = Use) [https://dishonored-archive.fandom.com/wiki/Far_Reach, https://dishonored-archive.fandom.com/wiki/Blink, via search summary]. Original-game Steam thread: Use key (F) cancels, or switching ability via number key/mouse wheel interrupts, or aim straight down [https://steamcommunity.com/app/205100/discussions/0/34095684647643835/]. A D2 player complains Far Reach has "no way to cancel" in combat [https://steamcommunity.com/app/403640/discussions/0/1742227898987352018/]. **Gamepad cancel button in D2: UNVERIFIED** (not in any source I could read).
- Conclusion for the GitHub issue: expected behaviour is that a non-cast input (Use/F on PC, the use/interact or a switch-item input on pad) aborts the aim; releasing the power button casts. Gripping (a "use/other" input) therefore should cancel, not cast. D2 patch 1.2 notes also say Blink/Far Reach/Heart auto-bind to shortcuts once acquired [https://www.sweclockers.com/forum/post/15488283].

## 5. Default bindings (DOTO)

| Action | PC | Gamepad (PS4 Preset A, same as D2) |
|---|---|---|
| Use power / left-hand weapon / gadget | Right mouse button ("uses gadgets/powers currently held in your left hand") [Steam https://steamcommunity.com/app/614570/discussions/0/2254560552296004320] | L2 [LUDO-1] |
| Sword (right hand) | Left mouse button (same thread) | R2 swing, R1 block / choke [LUDO-1] |
| Weapon / quick wheel | Middle mouse button (hold); Caps Lock used as workaround [https://steamcommunity.com/app/614570/discussions/0/1496741765139514660/] | L1 hold opens wheel; D-pad quick-select [LUDO-1] |
| Use / interact | F (D1/D2 default; in DOTO thread F is a user custom) UNVERIFIED | Square (hold sheathes) |
| Lean | Q / E (original Dishonored; DOTO UNVERIFIED) [https://orcz.com/Dishonored:_Keyboard_Controls] | Triangle hold |
| Crouch toggle / jump / sprint | C (D1) UNVERIFIED | Circle / X / L3 hold |
| Spyglass zoom | UNVERIFIED | R3 |
| Ammo switch | UNVERIFIED (no source) | UNVERIFIED |
| Mine toggle | hold gadget key (UNVERIFIED key) | hold L2 / LT [Hooked guides] |

DOTO has separate controller and K/M tabs in Settings > Controls; bindings live in a config file you can delete to reset [Steam threads above].

## 6. How shipped VR games/mods handle similar mechanics

| # | Mechanic | Example | Notes / comfort | Link |
|---|---|---|---|---|
| 1 | Throw a marker, then go to it | Budget Cuts "teleport/portal" (Unreal Tournament Translocator lineage) | Beacon thrown, a portal appears in hand so the player previews destination, smooth transition, built to avoid nausea; projectile caps distance | https://www.gamedeveloper.com/design/the-design-of-the-portal-locomotion ; https://uploadvr.com/budget-cuts-portal-locomotion-design/ |
| 2 | Teleport comfort | Half-Life: Alyx Blink (default), Shift, Continuous | Brief fade (blink), rotate toward direction during teleport; all modes keep world visible | https://www.uploadvr.com/half-life-alyx-locomotion/ ; https://www.pcgamer.com/half-life-alyx-is-full-of-clever-tricks-to-reduce-motion-sickness |
| 3 | Out-of-body / detached camera | no shipped match found; Meta guidance: vignette during motion, keep elevation consistent, slow before collisions, head always controls camera; controller-driven view not synced to head carries high sickness risk; instant camera jumps startle | https://developers.meta.com/horizon/design/locomotion-best-practices/ ; https://developers.meta.com/vr/design/locomotion-comfort-usability/ |
| 4 | Wrist-mounted weapons | Blade & Sorcery mods: "Tools of the Trade" (Dishonored-style crossbow, wrist layer), "Assassin's Arsenal" (wrist blade and crossbow, one wrist gadget at a time) | Equip by placing the item on the forearm; single wrist slot | https://www.nexusmods.com/bladeandsorcery/mods/4819/ |
| 5 | Stealth/Thief-like VR | Thief VR: Legacy of Shadow (2025), Unknightly | bows rather than disguise or marker powers; no face-stealing precedent found | https://gamesbeat.com/unknightly-update-brings-it-closer-to-being-the-thief-of-vr/ |
| 6 | Sticky mines by hand | none found | Closest: Alyx-style grab and throw physics. Design inference (not sourced): throw to stick, optional hand contact placement, matches the game's thrown-and-stick model | - |
| 7 | Disguise/face stealing | none found in VR | Treat as new design | - |
| 8 | Comfort checklist | Road to VR comfort checklist | snap turn, vignette, options per player | https://www.roadtovr.com/?p=99801 |

Comfort practices from the sources: offer fade/blink teleport rather than smooth glides, vignette when the view moves without the body, always keep head-tracking authoritative, avoid backward/fast/curved camera paths, and make every comfort option configurable.

## Open items to settle in game (all UNVERIFIED)
1. Exact PC keys for: mine toggle, ammo switch, Displace swap, Foresight exit, power cancel.
2. D2 gamepad button that cancels Blink/Far Reach aim.
3. Whether a Hook Mine can be placed directly on a moving enemy, and whether placed mines can be picked up or disarmed.
4. Original Game+ power binding and whether Displace/Foresight remain.
5. Foresight spirit speed and Displace max range.
