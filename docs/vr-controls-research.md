# VR controls research: Quest Touch bindings for Dishonored 2

Research date: 2026-10-09. Nothing here was checked in the headset. Items marked
**[unverified]** come from community knowledge or inference, not from a primary source.

## 1. Native Dishonored 2 gamepad bindings

### Primary sources

- **Installed binding file** (authoritative for this machine and build):
  `%USERPROFILE%\Saved Games\Arkane Studios\Dishonored2\base\dishonored2Config.cfg`,
  `bindingVersion 44`, binding set `IN_GAME`, lines 62-80. I only read the file and changed nothing.
- **Official Bethesda Xbox One manual**, page 2 "GAME CONTROLS" ([PDF](https://assets.ctfassets.net/rporu91m20dc/4wyjsRJV32cmKEcymOquUI/43ec13c9e82351efbc591dd33832f262/manuals_dishonored2_xone_en_pl.pdf)).
  The manual gives the text labels below. I could not render its picture, so I matched labels to
  buttons by their order in the PDF and checked each match against the cfg file.
- **PCGamingWiki** ([archived 2025-10-21](https://web.archive.org/web/20251021082851/https://www.pcgamingwiki.com/wiki/Dishonored_2)):
  Controller support is "Full controller support". **Controller remapping** and keyboard
  remapping are both rated "Native support". XInput is supported, Xbox button prompts are
  shown, and mixed controller plus keyboard/mouse input works (the prompts flicker between the two).

| Pad input | cfg action (IN_GAME) | Manual label |
|---|---|---|
| A | `JUMP` | Jump (also mantles; holding A at a ledge to climb is **[unverified]**) |
| B | `CROUCH` | Stealth Mode; while sprinting: slide |
| X | `INTERACT` | Use; Sheathe weapons [HOLD] |
| Y | `LEAN` | "+ Lean", meaning Y held plus the left stick |
| LB | `POWERWHEEL` | Quick Access Wheel (opens while held; on PC it is "hold MOUSE3") |
| RB | `BLOCK` | Block; Non-lethal takedown [HOLD] (the choke) |
| LT | `POWER_WEAPON` | Use Power or Weapon (the left hand) |
| RT | `MELEE` | Sword Attack; Focused strike [HOLD] |
| L3 | `SPRINT` | [CLICK] Sprint |
| R3 | `ZOOM` | [CLICK] Spyglass |
| Left stick / right stick | `MOVE` / `LOOK` | Move / Camera |
| View (Back) | `JOURNAL` (`PAD_TOUCHPAD` is also bound to it) | Journal/Objectives |
| Menu (Start) | `PAUSE` | Pause/Menu |
| D-pad R / U / L / D | `QUICK_0` / `QUICK_1` / `QUICK_2` / `QUICK_3` | Shortcuts |

The `INTERLUDES` set (cutscenes and interludes) binds only Start for pause, X for interact and the right stick for look.

**Actions with keyboard bindings but no pad binding** (cfg lines 23-60):

- `WEAP_0..9` (keys 1-0)
- `WEAP_NEXT` / `WEAP_PREV` (mouse wheel)
- `LEAN_LEFT` / `LEAN_RIGHT` (Q/E)
- `QUICK_USE_HEALTH_POTION` (R)
- `QUICK_USE_MANA_POTION` (T)

The bridge could inject these as keyboard input. That depends on simultaneous controller and
keyboard input working, which PCGamingWiki reports.

**Reload:** the cfg has no `RELOAD` action, and on PC the R key is the health potion. I have
not verified that reload is automatic.

**Other actions:**

- Lethal stealth kill: RT from behind **[unverified; the manual lists only Sword Attack]**.
- Picking up a body or object: X (Interact).
- Dropping what you carry: X **[unverified]**.
- Throwing a carried object: RT **[unverified; inferred from Dishonored 1]**.
- Blink and Far Reach: equip the power to the left hand through the wheel or a shortcut, then
  hold LT to aim and release it to cast **[unverified]**.
- On the wheel, you assign a shortcut by highlighting an item and pressing a D-pad direction
  (number keys on PC) **[the PC part is from community guides]**.

**Wheel pause:** whether the wheel slows or pauses time is **[unverified]**. Either way, a
held LB keeps the game in a wheel state, which matches the reported "laggy" feel.

**Autoaim:** the cfg contains `ark_player_autoaim_strength_pad "1"`. Pad autoaim may fight
aiming with the hands. Test whether setting it to 0 helps **[effect unverified]**.

## 2. Dishonored (1) VR mod (VR-Stereo-Hub/Dishonored-VR)

All references are pinned to commit [`b15e4d3`](https://github.com/VR-Stereo-Hub/Dishonored-VR/tree/b15e4d32a720bd4cfdb5ce4d600f3f8fba8d2cec) (zlib license).

| Quest input | Shipped action | Pad output | Source |
|---|---|---|---|
| Left stick, L3 | Move; L3 toggles sprint | LS, L3 | README.md:264, controller_binds.h:40 |
| Right stick | Turn (smooth or snap). Hold R3 for 0.4 s to drink a health elixir. Tap R3 to zoom. | RS, R3 | README.md:265, docs/dishonored/CONTROLLER_BINDS.md:54-62 |
| Left trigger | Power or gadget | LT | controller_binds.h:45 |
| Right trigger | Sword attack. A swing of the controller adds an attack press. | RT | controller_binds.h:46, docs/dishonored/PHYSICAL_SWING.md:1-20 |
| **Left grip (hold)** | **Power and weapon wheel.** It is a dial in the world: move the hand toward a wedge and release to equip; the sticks do not select. | LB | README.md:268, src/core/input/weapon_dial.h:15-30, src/core/input/pad_bridge.cpp:576-582 |
| **Right grip** | **Block, and choke from behind** | RB | README.md:269, pad_bridge.cpp:258-265 (grip hysteresis 0.9 to press, 0.7 to release) |
| A / B | Jump and climb / toggle crouch (B slides from a run) | A / B | controller_binds.h:36-37 |
| X | Interact; hold to sheathe | X | README.md:272 |
| Y | Lean: hold Y and push the **right** stick (routed to the game's left-stick lean). Y is also adrenaline. | Y + LS | README.md:273, docs/dishonored/CONTROLLER_EMULATION.md:221-227 |
| Menu | Tap to pause. Hold 500 ms, or thumbrest plus Menu, for the journal. | Start / Back | CONTROLLER_EMULATION.md:151-155 |
| Right thumbrest + left stick | D-pad shortcuts 1-4, used on center or release | D-pad | README.md:275, CONTROLLER_EMULATION.md:144-149 |
| Both stick clicks | Tap opens the F10 panel; hold 0.6 s recenters | none | README.md:276-277 |
| X + Y | Pause chord | Start | README.md:278 |

**How it resolves grab against block.** This is "Physical pickup" (docs/dishonored/CONTROLLER_BINDS.md:111-131; pad_bridge.cpp:200-205).

- A grip pressed while loot, a door, a usable object or a carried object is within reach
  (30 cm; 45 cm for books; 20 cm for doors) is taken before the remap. The grip's own action,
  the wheel or RB, does not fire, and Interact is pressed for 130 ms instead.
- On levers and valves, the grip holds Interact for as long as the target stays focused.
- People are never grab targets, so the right grip's choke keeps working.

**Other gesture features:**

- **Motion sword** (PHYSICAL_SWING.md): a hand-speed edge detector pulses RT, with a stab from behind for the stealth kill.
- **Physical choke** (docs/dishonored/CHOKE_GESTURE.md): the right hand at the left shoulder holds RB.
- **Spyglass gesture** (CONTROLLER_BINDS.md:57-62): the right trigger pressed with the right hand at the right eye pulses R3 for zoom.
- **Hand aiming:** Blink, the other powers, the crossbow and the pistol aim along the hand ray.
- **Other:** physical crouch, carried objects thrown along the hand's aim, journal and notes held in the hand, and a full remap UI with "Press to set" (CONTROLLER_BINDS.md:1-46).

## 3. Other high-quality flat-to-VR conversions

**Titanfall 2 VR (CircuitLord).** Closed source; the [installer repo](https://github.com/CircuitLord/CircuitLordVRModInstaller) README lists features only. From the [Aftermath review](https://aftermath.site/titanfall-2-vr-mod-circuitlord) and [Road to VR](https://roadtovr.com/titanfall-2-vr-mod-circuitlord/):

- Hold the left grip and move the controller in one of four directions to pick an ability (a gesture "wheel").
- The off-hand trigger cloaks.
- Make a fist to punch for melee.
- Grenades are grabbed from the chest; the secondary weapon is holstered on the left hip.
- Reloads are manual.
- Crouch by flicking the stick down or by crouching physically.
- Snap turn is available.

The exact buttons for jump and sprint are **[not found]**.

**BioShock trilogy VR** (VR-Stereo-Hub, README at `41f004b`, lines 116-129). This is the same author family as the Dishonored 1 mod:

- Right grip taps through weapons; hold it for the weapon radial. The left grip does the same for plasmids.
- RT fires, LT casts.
- A uses, B jumps, X reloads, Y uses a first-aid kit.
- L3 crouches.
- Left thumbrest plus the right stick selects ammo.
- Menu pauses; hold it for the map. X + Y pauses.

**HL2VR / Source VR mods.** [Road to VR on HL2VR Unleashed](https://roadtovr.com/half-life-2-vr-mod-dual-wield-steam/):

- "Open Weapon Selection" is its own SteamVR action.
- The author recommends putting it on the left stick click instead of sprint, as the most natural spot.
- Physical jumping is an option.
- I did not find the Quest default bindings **[unverified]**.

**"Dishonored 2 VR" community update (cosmic-tangerine, Sept 2026):** **[not found]**. Searches only found the Dishonored 1 mod at 1.0 ([NerdZap, 2026-09-27](https://nerdzap.com/news/best-vr-mods-flatscreen-games/)).

**Common conventions across these mods:**

- **(a) Selection wheels:** hold an input, select by hand direction or the stick, release to equip. All three mods that have one put it on a grip (Dishonored 1, BioShock, Titanfall). Here that conflicts with the user's fist and grab request. HL2VR's author puts it on a stick click.
- **(b) Grab against block:** resolve by context, with the target in reach first. Dishonored 1 swallows the grip near grab targets; enemies stay on the choke and block path. In Titanfall, the grip means holding the weapon or grabbing.
- **(c) Jump, crouch, sprint:** jump and crouch go on face buttons, plus physical crouch. Sprint goes on L3 (toggle) or is replaced by a stick flick.
- **(d) Lean:** physical 6DoF peeking, plus a native lean on a held button with the opposite stick (Dishonored 1: Y + right stick).

## 4. Recommended Quest Touch layout for Dishonored 2 VR

Design rules, from the user's feedback and the references:

1. **Grips never send LB.** Each grip closes its hand and grabs.
2. A native pad action fires only when the grip has no grab target.
3. Every native action stays reachable.

| Quest input | Gameplay action | Pad output | Notes and justification |
|---|---|---|---|
| Left stick | Head-directed move (accepted) | LS | Unchanged |
| L3 click | Sprint toggle; B while sprinting slides | L3 | Native, Dishonored 1 VR, HL2VR |
| Right stick X | Snap turn (accepted) | none | Unchanged |
| **R3 click** | **Quick Access Wheel** | **LB held** | Hold R3 to keep the wheel open. Highlight a wedge with the right stick, which is already under the thumb, or by pointing the left hand (Dishonored 1 dial). Release R3 to equip. A D-pad direction (section 5) while a wedge is highlighted assigns it to a shortcut. Why R3: the right thumb is idle apart from snap turn, so you can keep moving with the left stick (HL2VR uses a stick click; Dishonored 1 uses a hold-and-release dial). Suppress snap turn while the wheel is open. |
| Spyglass gesture (right hand at the eye + RT) | Zoom / Spyglass | R3 pulse | Dishonored 1 ZoomGesture. Native zoom moves here because R3 now opens the wheel. Optional: a short R3 tap with no wedge chosen pulses zoom instead **[tune]**. |
| Right trigger | Attack (accepted); hold for focused strike | RT | Unchanged |
| Left trigger | Power or left-hand weapon (accepted), aimed with the hand | LT | Unchanged |
| **Left grip** | Close the left fist. Grab a physics object, loot, a door or a lever in reach. Carry while held; release to drop; release with hand velocity to throw. | X pulse or hold on grab; RT on a fast release to throw **[the native throw button is unverified]** | Dishonored 1 "Physical pickup". With no target, it only closes the fist and sends nothing. |
| **Right grip** | Close the right fist and grab as on the left. With no grab target and the sword drawn: **Block** while held; behind an unaware enemy: **choke** (non-lethal takedown, held) | X when a target is in reach, otherwise RB | Dishonored 1 right grip = block and choke, with the grab-first priority from that mod. Grabbing a neck is a grab, so it suits the fist. Later option: a guard pose (blade raised across the face) as a gesture block. |
| A | Jump / mantle (accepted) | A | Unchanged |
| B | Crouch toggle (accepted); slide from a sprint | B | Add physical crouch later (Dishonored 1, Titanfall) |
| X | Interact; hold to sheathe | X | Native, Dishonored 1. Still needed for dialogue, keyholes, bodies, and anything outside grab reach. |
| Y (hold) + right stick | Native lean (the latch in the existing `lean_control.hpp`) | Y + LS | Dishonored 1 identical. Physical head peeking still works. |
| Menu, tap | Pause | Start | Accepted |
| Menu, hold ≥650 ms | Journal / objectives | Back | Existing `menu_chord.hpp` |
| Menu held + right stick direction | Shortcuts QUICK_0..3 (R/U/L/D) | D-pad | Existing `menu_chord.hpp`. Alternative: right thumbrest touch + right stick, the Dishonored 1 pattern on the other stick. |
| Menus / UI | Laser pointer + trigger (accepted); A confirms, B goes back | as now | Unchanged |
| Optional gestures | Hand to mouth = health potion; left hand to mouth + grip = mana | Keys R / T | The actions exist in the cfg with keyboard bindings only. The bridge would need keyboard injection **[unverified]**. |

**Why the wheel leaves the left grip.**

- The user reported it as laggy, unusable, and in conflict with making a fist.
- Grip squeeze is analog with a 0.9 press threshold in Dishonored 1 (pad_bridge.cpp:259), and
  the current composer's grip threshold is 140/255. So any relaxed squeeze while holding the
  controller can open the wheel.
- A digital click avoids accidental opens and frees both grips for embodiment.

**Fallback if R3 is uncomfortable:** put the wheel on Y (hold) and move native lean to the
keyboard `LEAN_LEFT` / `LEAN_RIGHT` driven by Y + the right stick. This needs keyboard injection.

**Checks needed before calling this done** (none are covered by automated tests):

- The native wheel highlights from the right stick while LB is held through XInput.
- Whether RT throws a carried object.
- The choke while the grip is held and the hand is closed.
- Mixed pad and keyboard input for the potions.
- Comfort of holding R3 while moving, tested in the headset.
