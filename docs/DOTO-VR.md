# Death of the Outsider VR

Dishonored: Death of the Outsider (DOTO, Steam app 614570, `Dishonored_DO.exe`) runs on the same Void Engine
family as Dishonored 2. This mod supports it from the **same repository and the same sources**. A generator ports
the Dishonored 2 build to the DOTO executable, and the launcher has an **OUTSIDER** tab that installs and starts it.

Status (2026-11-22, first night): the port pipeline, the DOTO DLL build, the launcher tab and the simulated test runner
exist, and **the VR core runs in Death of the Outsider under the simulated headset**: tracked stereo gameplay with every
frame paired, per-eye head tracking, native turning, comfort settings, per-eye TAA history and the menus on the VR panel
(see §5), Billie's tracked hands and **her full body** (her own coat, torso and Voltaic Gun arm when looking down).
First headset session 2026-11-22 (Virtual Desktop, v145721): controller input did not reach the game, the sword looked
black, quest markers were flat; v145722 addresses the input and marker causes (see the headset row in §5). This document
is the design and the plan. The mod is one source tree for both games (§8).

![Simulated-headset mirror of Death of the Outsider in VR, both eyes side by side](images/doto/play7-stereo-mirror.png)

![Looking down in Death of the Outsider VR: Billie's full body and tracked hands, both eyes](images/doto/play14-full-body.png)

## 1. How the port works

The Dishonored 2 mod is pinned to one exact executable. About 1,400 addresses (functions, hook sites, vtables,
globals, import slots, cvars) and several hundred struct field offsets are written into `src/` as hex literals, and
many hooks compare the engine's code bytes before they install. DOTO's executable was built by the same toolchain
(same sections, full MSVC RTTI, no Denuvo) from a newer engine branch (VoidEngine v1.145.0.0 against D2's v1.77.9.0).

Rather than fork the sources, `tools/doto/` translates them:

| Step | Tool | What it does |
|---|---|---|
| Index | `pe_index.py` | Every function from `.pdata` (116k in D2, 120k in DOTO), a relocation-masked hash, call and data references; all RTTI vtables by class name. |
| Match | `match.py` | Pairs functions: unique masked hash, vtable slots by RTTI name, call-graph and data-reference propagation, shape, neighbourhood (92k of 116k paired). |
| Align | `align.py` | Instruction alignment of every pair: displacement votes (struct offsets), rip-target votes (globals), immediates. 88.7k of 91.7k pairs are instruction-identical. |
| Translate | `translate.py` | Any D2 address to DOTO with a method (exact, aligned, voted, pattern, cvar by name, import by symbol, rip-reference scan). |
| Inventory | `inventory.py` | Classifies every hex literal in `src/` (code / rdata / data address, field offset, byte check, comment). |
| Port | `port.py` | Writes `build/doto-src/src/`: addresses translated, moved offsets rewritten, byte checks and FNV code pins regenerated when only relocations differ, identity switched (exe name, SHA-256, `D2VR_GAME_DOTO`). Anything it cannot translate becomes a poison RVA beyond the image, so the mod's own checks switch that one feature off instead of touching the wrong code. |
| Review rules | `offsets.json`, `merge_review.py` | Reviewed per-site rules with evidence: line rules for uniformly shifted structs, site offsets, site-scoped addresses, raw text (hex strings, sizes), accepted byte differences. |
| Build | `build-doto.cmd <ver>` | Regenerates and compiles `build/doto/d2vr_render_probe_arm_v<ver>.dll` with the release flags. |
| Review helpers | `fn_diff.py`, `who_shifts.py`, `live.py` | Side-by-side disassembly, "which functions moved this offset", read-only live memory of the running game. |
| Test | `doto_sim.py` | Simulated-VR session in DOTO through the proxy, its own mod home `build/doto-home`, window captures, PC-in-use checks. |

D2 itself is untouched by the port: the D2 build compiles `src/` as before. The only shared-source change so far
is ABI-neutral for D2 (the Iggy hub hook forwards a fourth register argument; see §2).

Numbers at the time of writing: 1,397 address literals translated (967 exact, 261 voted, 115 aligned, 32 reviewed,
22 other), 16 unique addresses poisoned (all in weapon, door and grenade feature code), 380 offsets rewritten.

## 2. Engine differences found so far

| Area | Dishonored 2 | Death of the Outsider | Evidence |
|---|---|---|---|
| `renderView_t` (the camera the mod writes per eye) | 16 bytes at +0x4c | removed: every field after +0x48 is 0x10 lower (FOV +0x8c, position +0x98, basis +0xbc, forced projection +0xf0, flag +0x130) | renderView copy 0x444c0 vs 0x455c0; TAA jitter 0x3742b0 vs 0x37b1a0 |
| Prepared view stride | 0x3ab0 | 0x3ab0 (unchanged: the view keeps a 0x10 tail after the shrunk renderView) | live: eye 1 at +0x3ab0 (`live.py views`) |
| Render pipeline | a view's Setup is consumed by the next Present's Begin (depth 1) | Setup and Begin inside one Present interval (depth 0) | live thread trace; trait `render_pipeline_depth` |
| Player body | Corvo/Emily: one hidden proxy surface, the mod swaps in the arm's body material | Billie: `player_body_billie.md6`, six surfaces, all overridden with `player_part_hidden.material` in first person | live dump (`tools/doto/body_dump.py`); the mod clears the overrides so her own materials draw |
| Player state machine (`arkCpntPlayerStateMachine`) | state pool +0x90, requests +0x220, controller..sensors +0x2f8..+0x338 | pool +0x98, requests +0x260, +0x58 from the controller on (+0x350..+0x390) | constructor diff, live (`hand_state_probe.py`); one struct rule rewrites every `machine` read (hand state, action camera, assassination receipts, Block, sword contact, takedown, lean, equip, choke, talk, weapon visibility) |
| `idGameLocal` fields | player +0x3130b0, logic manager +0x1f6ec8, state +0x341058 | +0x327770, +0x1f75f8, +0x355718 | aligned code, confirmed live |
| `arkPlayer` | components +0x268, model +0xf0, origin +0x300, axis +0x30c | +0x270, +0xf8, +0x310, +0x31c | aligned code, live reads |
| Player camera component | mode/pitch/yaw/aim at +0x25200.. | +0x30 (+0x25230..) | aligned code |
| Arm model | +0x1fe8.., flags +0x1c30 | -0x10, flags +0x1c2d | aligned code, live wrappers |
| Skeleton vtable | slot +0x88 | slot +0x90 (one more virtual) | RTTI slot pairing |
| Iggy external-call hub | 3 arguments | reads a 4th (`r9d`) | hub 0x34800 vs 0x35630; the hook now forwards it |
| Player step (turning) | prologue | same arguments, different register allocation and frame | entry detour accepted after review |
| UI movies | D2 `ui/*.iggy` (Corvo and Emily pause menus) | its own movies (one pause menu); the menu AS3 matches D2's except constant-pool indices | the menu and glyph edits are regenerated by method name for DOTO (`tools/doto/menu_patches.py` -> `src/menu_patches_doto.hpp`, `src/controller_glyph_atlas_doto_edits.hpp`) |
| Config | `Dishonored2\base\dishonored2Config.cfg` | `Dishonored_DO\base\dishonored_DOConfig.cfg` | on disk; the launcher and the proxy pick the folder by executable |
| Window title | contains "Dishonored" | "Dishonored: Death of the Outsider using VoidEngine v1.145.0.0" | live |

## 3. Billie Lurk's toolkit in VR

Sources: Bethesda's powers/gadgets announcement, Wikipedia, PCGamesN; the fandom wiki and PCGamingWiki refused automated
fetches, so numbers marked *unverified* must be checked in game. DOTO has **no Heart, no runes and no mana elixirs**:
energy regenerates. Powers are Void artefacts available from the first mission.

### Powers

| Power | In the game | VR design | Hand | Builds on |
|---|---|---|---|---|
| **Displace** | Place a marker only Billie sees, later swap places with it (line of sight needed). Can be placed from Foresight. The Third Eye charm adds a second marker (*unverified*). | Point the casting hand and hold the trigger: a hand-aimed arc and a translucent Billie silhouette at the target. Release places the marker; it stays world-locked. **Swap is a different gesture** (a quick pull of the casting hand toward the chest, or the power button) so placing never teleports by accident. The swap uses the Blink comfort treatment: an 80-120 ms fade or blink, no smooth path, no horizon roll. Two markers get two colours; the swap goes to the one the hand points at. | power hand | `native_blink_aim.hpp` (hand-aimed Blink), comfort vignette |
| **Foresight** | Time stops; Billie leaves her body as a spirit to scout, tags people and objects, sees through walls, can place a Displace marker. Drains energy while active (~18 s, *unverified*). | Highest comfort risk. Default: free spirit camera driven by the left stick at a fixed low speed with the user's own snap/smooth turning and the comfort vignette during motion (time is frozen, so the world is still). Option: "dollhouse" mode, the view stays in the body and a small spirit figure in the casting hand is steered by pointing. A visible tether line back to the body. Tagging = point and trigger; Displace from the spirit uses the same place gesture. | power hand | camera ownership (authored-camera path), `comfort_vignette.hpp`, `native_interaction_aim.hpp` |
| **Semblance** | Steal a face to pass as someone else; drains energy while moving; a face is used once. | Bring the casting hand to the target's face and squeeze, then pull away as if lifting a mask (hand-to-head contact, as the choke and face-grab already do). While active, the stolen face shows on the left wrist next to the watch. | power hand | `native_choke_distance_scope.hpp`, face grab, wrist HUD |
| **Void Strike** | A charged sword blow that knocks enemies back, can assassinate, breaks weak barriers. | Grip + trigger with the sword still, the blade glows and the hand pulses (the existing power-strike charge), release with a fast thrust or swing above a speed threshold. | sword hand | `sword_charge.hpp`, `native_melee.hpp` |
| Original Game+ powers | Blink, Dark Vision, Domino etc. after the campaign (*unverified* list) | The D2 power hand path as is. | power hand | D2 powers |

Void Gaze is Daud's power from the first game, not Billie's; DOTO's equivalents are Foresight's highlights and the
Rat Whispers talisman.

### Weapons and gadgets

| Item | In the game | VR design | Builds on |
|---|---|---|---|
| Sword | Billie's short sword | Unchanged motion combat: swing, block, parry, throw and recall, holster over either shoulder. | D2 sword |
| **Voltaic Gun** | Wrist-mounted launcher, lethal and non-lethal ammo, upgradable | Mounted on the forearm instead of held: aim with the arm, fire with the trigger of that hand, the hand stays free to grab. Ammo type by a wrist twist or the wheel. Left/right arm configurable. | D2 pistol/crossbow fire path, wrist anchoring |
| **Hook Mine** | Sticks to walls, floors, ceilings; pulls enemies in; non-lethal takedown, lethal in numbers | Take it from a hip slot, press it onto a surface by hand (contact places it) or throw it. | grab, throw, `native_gadget_drop.hpp` |
| **Hyperbaric Grenade** | Non-lethal air blast that launches enemies | Take from a holster, throw; optional arc preview, off by default. | `native_grenade_throw.hpp` |
| **Rat Whispers** talisman | Hear rats' clues | Hold the hand to the ear (as the Heart is held up in D2); spatialised audio toward the rats. | Heart listen gesture |
| Bonecharms, Black Market, Contracts | Equip charms; buy gear; optional jobs | The game's own menus on the VR panels; contract text in the left palm like notes. | world UI, note tablet |

### What the engine does (static RE, `tools/doto/reviews/powers-re.md`)

| Item | Engine path in DOTO | Consequence for VR |
|---|---|---|
| Displace | `arkCpntPowerSwapper`; its sensor (`arkCpntPlayerSensorSwapper`, id 0x26) solves the marker from the **player camera's aim block** (`cam+0x252ac` origin, `+0x252b8` axis), target at `sensor+0x148`; place and swap are one Cast virtual (`0x3b7040`); swap requests player state 93 | The mod's gameplay aim already writes that block from the hand, so hand-aimed Displace needs only the publish gate to include the Swapper's aim state (`power+0xa4`). Fallback hook: `0x3e21b0`. |
| Voltaic Gun | `arkCpntWeaponGaussGun`, fire event 206 -> `0x3c1d70` -> launch `0x68e970` (call at `0x3c219d`); target from aim sensor 9, which copies the same camera aim block | Hand aim applies through the existing shot-aim hook; its call table now names `0x3c219d`. Publish the forearm frame (the gun is on the wrist). |
| Foresight | `arkCpntPowerTheEye`: no separate camera; the player entity itself leaves the body under the normal player camera; time is frozen per entity, not globally. The only camera-mode request in the power is a mode-0 push at exit (`0x3be254`) | Head tracking and hand aim apply unchanged (the locomotion states keep camera mode 0); the existing locomotion comfort vignette covers spirit motion; a tether line is optional polish. |
| Semblance | `arkCpntPowerMimic`: no ray; the target is the NPC held in the choke path. The Mimic states (UseMimic 102, Mimicking 96, CancelMimicking 100) request no camera mode: the choke's mode 2 (animation-driven, like takedowns) ends when the next locomotion state pushes mode 0 (an earlier note that Semblance pushes mode 2 was a misread: `0x6e5df0` is the NPC-fatality state) | The hand-to-face gesture reuses the choke selection; while disguised and walking the view is ordinary gameplay (hand aim, tracking). Live read-only check pending: `cam+0x25230/+0x25234` during UseMimic, while disguised, in Foresight (expected 2, 0, 0). |

### Situations that need care

- First mission ("One Last Fight"): scripted opening, powers arrive after it.
- The Bank Job: vault keys, scripted vault interactions (grab handling).
- A Hole in the World: Void scenes and unusual geometry; scripted camera.
- Lifts, trams and other rides: comfort vignette.
- Cutscenes: in-engine; the D2 cutscene modes (3D or theatre) apply.
- Billie's right arm has a Void variant model in the resources; if the game swaps it, the tracked arm is re-acquired.

## 4. Plan

| Phase | Goal | State |
|---|---|---|
| 0 | Research, address-mapping lab, generator, DOTO DLL builds | done (this night) |
| 1 | **VR core boots in DOTO**: stereo, head tracking, controllers, turning, menus on the panels, loading/pause detection | **done in simulation** (play7: 6,887 of 6,887 gameplay frames paired, 13,774 head writes, 2,938 verified turns, logic mask and loading detected); headset pending |
| 2 | Hands and body: Billie's arm and body models, grab, carry, doors | **done in simulation**: Billie's tracked hands (play12) and full body with her own materials (play14), torso fit with her six-surface mesh pins, grab; doors whose engine code changed keep the game's own interaction |
| 3 | Combat: sword, block/parry, takedowns, choke | weapons review in progress |
| 4 | Billie's toolkit: Voltaic Gun on the wrist, Displace aim + swap, Foresight camera, Semblance gesture, Hook Mine, grenade | design above; implementation after phase 1-3 are verified |
| 5 | UI polish: in-game VR Settings entry, controller glyphs | **done in simulation** (play19): pause menu Stats -> **VR Settings**, the native VR options page opens (0 faults) and B goes back; 40 glyph movies patched, 0 refused. Main menu: Credits -> VR Settings (DOTO keeps its Exit), patched on the menu's next load (the first title menu loads before the mod starts) |
| 6 | Release: launcher payload embeds the DOTO DLL (`artifacts\doto\repair-vNNNN-test-candidate.json` + `build\doto\<dll>`), headset test | launcher side done |

Verification rule (AGENTS.md): automated and simulated checks do not establish headset visual quality, comfort or
complete gameplay; each phase ends with a headset session.

## 5. What was verified, and how

Simulated sessions (`tools/doto/doto_sim.py`, the mod's simulated OpenXR runtime, Steam build 17712087, a new campaign):

| Run | DLL | Result |
|---|---|---|
| boot1-boot2 | v145703-v145705 | refused at start: a byte pin split across two lines (comfort), output-size getter pins merged with a neighbour array |
| boot3 | v145707 | first complete session: XR focused, menu panel, title and main menu detected (logic mask 64) |
| play2 | v145708 | menus driven by the simulated controller into a new campaign; in gameplay: **two prepared views**, renderView decoded at the DOTO offsets, arkPlayer chain confirmed live, per-eye TAA history installed, comfort applied and restored; no eye pairs (stride) |
| play4-play5 | v145709-v145710 | stride fixed; the whole player-camera chain verified live (`tools/doto/check_camera.py`); still no eye pairs: Setup and Begin share one Present frame in DOTO |
| play7 | v145712 | **tracked stereo gameplay: 6,887/6,887 frames paired, 0 flat frames, 13,774 head-pose writes, 2,938 verified turns** (pipeline depth 0) |
| play8-play10 | v145712-v145714 | the launcher's full feature set refused at start, one check per run: gameplay-aim producer prologue (a decimal `std::byte{0}` split the pin), its caller, the door owner |
| play11 | v145715 | **the launcher's full feature set (minus saved settings): 7,039/7,039 frames paired, hand aim applied on 7,036 frames, crosshair 7,038, interaction 21,441 calls, grab 7,141 callbacks, FSR eye AA on both eyes, 3,223 verified turns, arm template verified**; only notes: Timepiece lens guard and hand-aimed Blink off (Billie has neither) |
| play12 | v145715 | **the launcher's default feature knobs too (spacewarp, full body, hand body, dual wield, note tablet): 5,447/5,447 frames paired, hand aim 5,445, 3,525 verified turns, grab 5,576 callbacks, Billie's tracked hands drawn in both eyes** |
| play13 | v145715 | look-down walk: full body requested but invisible (`route_or_material`); live dump (`body_dump.py`): Billie's body model has six draw records and six overrides, all `player_part_hidden.material` |
| play14 | v145716 | **Billie's full body visible**: the body renderer clears her overrides so her own six materials draw (`full_body.visible` 1, 0 material failures, 5,240 body uploads, 5,296 body solves, hidden material restored at stop); coat, torso and Voltaic Gun arm in both eyes when looking down; 5,297 stereo frames |
| play15 | v145717 | the same with the target-driven build and the per-game proxy (focus cvars found in `dishonored_DOConfig.cfg`); a clip of the look-down walk recorded from the mirror window (`tools/doto/burst.py`, below) |
| play17-play18 | v145719 | DOTO menu tables: 13 then 40 glyph movies patched, 0 refused; pause and main menu movies patched (0 mismatched); the pause list shows **VR Settings**; play18's cursor overshot to the disabled Tutorials entry (stick auto-repeat), so no click |
| play19 | v145720 | **in-game VR Settings in DOTO**: pause -> VR Settings -> the native VR options page (1 click, 1 page, 0 faults, 0 failed steps) -> B back to the pause list -> B to gameplay (`pause_vr_settings` scenario) |
| play20 | v145720 | avatar check: body IK and arm IK every frame at D2 parity (body solves, body-pose uploads and per-arm two-bone IK poses, 0 rejected); but Billie's fingers used the game's pose (finger mode 3): the hand-state snapshot never read an empty hand. `tools/doto/hand_state_probe.py` (read-only) found the player state machine's pool at +0x98, not +0x90 |
| play21 | v145721 | **state machine layout fixed for every reader** (`offsets.json` "structs"): fingers follow grip and trigger (finger mode 1 on 5,485/5,485 frames, both hands empty); live: pool +0x98, controller +0x350, body +0x358, interaction +0x370, inventory +0x380, attributes +0x388, sensor container +0x390; state ids 18/39/44/45/49/50/53/54/64 unchanged (Leaning, Slash, Block, CarryCorpse, HoldObject, EquipChange, Draw) |
| headset 1 | v145721 | **first real headset session** (user, Virtual Desktop): sticks and Menu did not reach the game; the sword looked black; quest markers flat on the HUD panel. Causes found: (1) every sim run had the game window in the foreground (`game_window_foreground` true, pad-gate overrides 0), so the no-focus input path was never exercised, and the game paused itself on focus loss (logic 4) although the launcher writes `win_pauseOnFocusLoss "0"` / `in_controlInactiveWindow "1"` into its config; v145722 reads both cvars live (`src/focus_cvars.hpp`, idCVar +0x28 value / +0x30 name; objects in `offsets.json` "rvas") and sets them through the engine console when wrong, and the always-on `xr-live-status.json` gets an `input` block (foreground, XR focus, reads, pad gate, cvars). (2) the Iggy value-ref construct pin (D2 0x28e50, DOTO 0x29490: frame 0x30 -> 0x40, same ABI) never matched, so no native HUD art was ever bound (play21: `native_hud_children.published` 0, `marker_published` 0); fixed by a text rule. (3) **black sword in the hand** = D2's v1101 bug again: five render-pass callbacks in the DOTO table kept their D2 values (port.py took them for offsets), so the held weapon's `DEPTHPASS` draws were untagged and never placed while its forward pass was, and temporal AA turned the mismatch black; v145723 maps them to DOTO's registrations (`tools/doto/render_pass_check.py` checks the table against each exe; DOTO registers one extra pass, `UPDATE_CLOTH`, which stays untagged; pass ids stay D2's logical ids). v145723 also installs the bodyless-motion crash guard (pin-audit row 2). Other failing pins: `tools/doto/reviews/pin-audit.md` |

![Clip: walking while looking down in Death of the Outsider VR, Billie's body and hands in both eyes (simulated headset)](images/doto/play15-look-down-walk.webp)

![The VR Settings page inside Death of the Outsider's pause menu (simulated headset)](images/doto/play19-vr-settings-page.png)

![Clip: pause menu -> VR Settings -> back -> gameplay (simulated headset)](images/doto/play19-pause-vr-settings.webp)

Live read-only checks of the running game (`tools/doto/live.py`): prepared views (count 2, eye 1 at +0x3ab0), renderView
fields at -0x10, `arkPlayer` model +0xf8 / components +0x270 (98) / origin +0x310 / axis +0x31c, the logic mask.

## 6. Trying it (headset)

1. Close the running Dishonored VR launcher (one instance at a time), then start
   `launcher\dist-doto\DishonoredVRLauncher.exe` (developer mode: Dishonored 2 keeps using the repository as before).
2. On the **Play** tab pick **Death of the Outsider** in the game switch above the page (Dishonored 2 is the default). It
   installs the Death of the Outsider mod into `%LOCALAPPDATA%\DishonoredVR-DOTO`, copies your Dishonored 2 VR settings
   (option "Use my Dishonored 2 VR settings") and your DLSS location (`build\dlss-dll-path`), and shows the checks.
3. Start Virtual Desktop or SteamVR as for Dishonored 2 and press **LAUNCH VR**. The launcher installs the proxy beside
   `Dishonored_DO.exe` and starts the game through Steam; VR attaches at the title screen.
4. Campaign > Continue loads the campaign the night's tests started (slot 01, the opening of the first mission); New Game works too.
5. **Uninstall proxy (DOTO)** on the same page makes the game flat again.

In the game, **Pause > VR Settings** opens the VR options (the same page as in Dishonored 2); B goes back.

Logs (when "Save diagnostic logs" is on): `%LOCALAPPDATA%\DishonoredVR-DOTO\artifacts\`.

## 7. Developer workflow

    python tools/doto/pe_index.py <D2 exe> artifacts/doto-lab/d2.idx          # once per game build
    python tools/doto/pe_index.py <DOTO exe> artifacts/doto-lab/doto.idx
    python tools/doto/match.py artifacts/doto-lab/d2.idx artifacts/doto-lab/doto.idx artifacts/doto-lab/map.pickle
    python tools/doto/align.py artifacts/doto-lab/d2.idx artifacts/doto-lab/doto.idx artifacts/doto-lab/map.pickle artifacts/doto-lab/align.pickle
    tools\doto\build-doto.cmd 145702                                          # regenerate + compile
    python tools/doto/doto_sim.py --name boot --dll build/doto/d2vr_render_probe_arm_v145702.dll

After changing `src/`, rebuild with `build-doto.cmd`: the generator re-reads the sources, so D2 work flows into DOTO.
Read `artifacts/doto-lab/port-report.json` for what was translated, what was poisoned and which byte checks differ.
To add a reviewed fix, put it in `tools/doto/offsets.json` with its evidence (`why`), never in the generated tree.

## 8. One mod, several games: adding a target

The mod is one source tree. Dishonored 2 is the **base**: `src/` is written against its pinned executable and compiles
as is. Every other game build is a **port target** that the generator translates `src/` to. What is shared and what is
per game:

| Layer | Shared | Per target |
|---|---|---|
| VR runtime (OpenXR, stereo, tracking, panels, comfort, spacewarp, FSR eye AA, hands, body, menus) | all of `src/` | nothing: the generator rewrites addresses, offsets and code pins |
| Engine behaviour that really differs | the code path, chosen by a trait | one block in `src/game_profile.hpp` (`d2vr::game_traits::render_pipeline_depth`, `optional_native_consumers`, `billie_avatar`, ...) |
| Game data (an avatar's mesh pins, resource and material names, UI movie edits) | the code that uses it | a table under the target's define, next to that code (`native_torso_fit.hpp`, `body_renderer.hpp`), or a generated per-target header (`menu_patches_doto.hpp`, `controller_glyph_atlas_doto_edits.hpp` from `tools/doto/menu_patches.py`) |
| Addresses and offsets | `tools/doto/` lab and generator | `tools/doto/targets/<name>.json` + its reviewed rules (`offsets.json` for DOTO) |
| Launcher | tabs, install, payload, settings copy | one `GameProfile` (`launcher/GameProfile.cs`): exe, hash, Saved Games folder, mod home |
| Proxy (`dinput8.dll`) | loader, config handling | the config folder follows the running executable |

Adding a game or a new build of a supported game:

1. **Target file.** Copy `tools/doto/targets/doto.json` to `targets/<name>.json`: display name, exe, Steam folder,
   SHA-256, `size_of_image` (from the PE header), `define` (`D2VR_GAME_<NAME>`), a rules file, lab/output folders.
   The exe is found in any Steam library (`libraryfolders.vdf`) or through `D2VR_EXE_<EXE_NAME>`.
2. **Lab.** Index both executables and run `match.py` and `align.py` into the target's lab folder (§7). Check the
   match rate: below about 80 % paired functions, expect a long review.
3. **Profile.** Add a `#elif defined(D2VR_GAME_<NAME>)` block to `src/game_profile.hpp` with every trait. Start from
   the base's values; change one only on live evidence.
4. **Generate and read the report.** `set D2VR_PORT_TARGET=<name>` then `tools\doto\build-doto.cmd <ver>`
   (`port.py` writes `src/game_target.hpp` with the define and the poison base). `port-report.json` lists poisoned
   addresses (features that will switch themselves off), byte pins that differ semantically and unused rules.
5. **Review.** For each poisoned or semantic entry, compare the functions (`fn_diff.py`, `who_shifts.py`) and add a
   rule with its evidence. Never edit the generated tree.
6. **Verify live.** Struct layouts, per-view data and call order are checked against the running game with read-only
   tools (`live.py`, `check_camera.py`, `body_dump.py`), then a simulated session (`doto_sim.py`), then the headset.
7. **Launcher.** Add a `GameProfile` and a tab; embed the target's candidate (`make_candidate.py`).

A D2 game update is the same procedure with the old D2 build as base and the new one as target.
