# Night report, 2026-10-12

Current build: **v295** (`artifacts/repair-v295-test-candidate.json`). Every item below passed the build's unit tests. Items marked *sim* also passed the headset-free self-test (`tools/vr_selftest.py`) against the live game. Comfort and feel still need your headset session.

## Fixed

| Area | What changed | Verified |
|---|---|---|
| Swimming | The lean controller zeroed the stick whenever the player state changed (constantly in water) or no lean state existed. The head-directed stick also zeroed it when the body heading went stale (swimming). Both now pass the stick through. | sim **in water** (teleported to where you swam): 24.9 m swum with the stick; crouch dives about 0.6 m. |
| HUD | Replaced per-element layout hacks with one **HUD screen**. The whole native HUD stage is fitted by width (the game stretched it to ~2750 px inside a 2496 px eye, clipping the right side). Its angular width is computed from the headset's real FOV and the HUD size setting. It is a curved cylinder layer when the runtime supports one. Wrist widgets (HP/mana/ammo) stay on the wrist. | sim: nothing clipped at the edges, the HUD has content in every scenario. |
| Blinking HUD / wheel | Frames without a fresh capture now re-show the last image (up to 400 ms) instead of dropping the layer. | sim: the wheel is never empty. |
| Wheel | The cursor sits on the wheel quad (0.9 m) and is visible while the wheel is open. Potion icons are clickable with the trigger through the game's own pointer. Health/mana glyphs show B / A, with A sending native X (mana). | sim: the wheel opens and renders fully. |
| Grabbing | An item the game highlights (apple, coin) now beats an unrelated prop that is merely closer to the palm. A palm-wide "beam" ranks what you point at. The second hand gets a fair query slot. | sim: grip block/slash ok; no grabbable items near the save start. |
| Combat | New `VrCombat`: blade-tip swing detection (slash / overhead / thrust), thrust = stealth kill when the game's kill sensor qualifies, choke by reaching to the neck with grip, block by holding the sword horizontally, drop-kill timing assist, haptics. `build/legacy-combat` restores the old motion sword. | sim: a physical swing produces the engine's Slash state. |
| Notes | Notes and books are held above the left hand, facing your eyes, like a sheet of paper. | unit test |
| Anti-aliasing | FSR 3/4: the engine's motion vectors contain the jitter step, so jitter cancellation is now enabled. Native TAA gets the jitter step removed from its motion vectors. This was found and verified by the other Claude session with a sharpness measurement: FSR native is now as sharp as AA Off. FSR 4 Native / Quality can be selected in the VR menu at runtime. | sim measurement |
| Test unlock | VR settings, bottom row, left: 100 runes, infinite coins, every Black Market item. | unit test |

## New tools

- `tools/vr_selftest.py --dll <dll> --scenario all|hud|walk|wheel|jump|lean|swing|grab|power|powers [--launch]`: headset-free checks with PASS/FAIL, using engine player-state verification.
- Test commands through `artifacts/vr-command.txt`: `unlock`, `cvar <name> <int>`, `uidump on`, `pad a|b|x|y|start`.
- **Launcher**: `launcher/dist/DishonoredVRLauncher.exe` provides environment checks, settings, Launch / Stop VR, and a Tools tab. See `docs/LAUNCHER.md`.
- `docs/TESTING.md`.

## Not done / blocked

- **Powers**: every save is chapter 1 (before the Outsider's Mark), so Emily has no powers yet. `--scenario powers` is ready for a later save.
- **Physical weapons**: done in v275. The native world sphere-cast (`0xc89bc0`, found by reverse engineering) is verified live: floor hit 1.29 m below the eyes with an upward normal, a wall hit with a facing normal. The hand/weapon grip stops at geometry with haptics. 672 casts with hands at rest produced no false self-contacts. `build/no-weapon-collision` disables it.
- **In-engine playground**: engine console commands work from the mod (`console god` and `console setviewpos` teleport, verified). `vr_selftest.py --teleport X Y Z`.
- **Performance**: a dynamic-resolution governor (`src/vr_perf_governor.hpp`) runs only in the scaled FSR modes (Quality/Balanced) and lowers the render fraction when frames miss the display; native modes are never touched. `build/no-governor` disables it.
- **Swimming in water** and **grabbing real items** need a scene with water or items: confirm in the headset.

## Please check in the headset

1. Swim: stick movement and diving (right stick down = crouch).
2. HUD screen readability. Change size in VR settings → HUD size if needed.
3. Wheel: cursor visible, potion clicks, no blinking.
4. Grab food/coins with either hand.
5. Combat: slash, stab a guard from behind, choke (hand to neck + grip), block (sword horizontal).
6. AA Off vs TAA vs FSR 3 Native, head still and moving.

## Later in the night (v276-v277)

- **Engine console queue**: several `console` lines in one poll now all run, in order. Before, only the last one ran, so `god` before a teleport was silently dropped. This is why the player died in earlier water tests.
- **Entity list** (`tools/entities.py`, reverse engineered and read-only): NPCs (guards, civilians, pre-placed corpses) and pickups with class, name and position. `vr_selftest.py --near item` teleports next to the nearest pickup on the same floor.
- **Simulated hand pitch** (gameplay script v4, `hand_pitch_degrees`): tests can point the hands at the floor.
- **Pickup test result**: the simulated right hand, reaching for a coin pouch on the floor, physically grabbed the **fruit plate** next to it (`env_interiors_fruit_plate_01`, a physics prop). Physics grab never takes loot (`arkPickup` is excluded from its candidates). The game's Use target was the pouch in 162 query frames, but at the moment of the press the palm was closer to the plate. That is the intended behaviour for props in reach; loot is taken by native Use when the game offers it to that hand. Please check coins and food in the headset; real hand aiming is more precise than the simulated hand.

## Morning (v278-v282): damage indicator and HUD placement

- **Damage vignette**: hud.iggy draws it as a full-screen sibling of MasterHUD (stripes laid out by the game to the real eye target, 0..2957 px tall). It was routed into the HUD screen panel and its soft stripes ended at the panel's hard edges. Full-screen siblings now keep the native eye draw. Verified with `--scenario damage`: the red falls off smoothly to the frame edge, no steps.
- **Health/mana/ammo**: the game sends their stat calls at level load, before the HUD's first draw, so they were rejected and the vitals landed on the HUD screen (top-left) until a value changed. The owner of that early call is now kept and published on the first UI call after the HUD draws (`wrist_early_publications`).
- **Vitals are wrist-only**: their flat copy was drawn head-locked in the lens corner whenever you were not glancing at the wrist. It is never drawn now. The widget's frame decorations (ring backdrop, slot diamonds) are hidden too (`hud_holder_hidden`).
- Result: nothing is drawn flat in the eye any more; the HUD screen holds only the crosshair, prompts and notifications.
- Open: hits kick/blur the view for a moment (bullet push). `g_kickAmplitude`/`g_kickTime` = 0 was tried but that run showed no damage at all, so it is not concluded.

## v283-v285: crosshair and prompts

- **Vitals binding without a stat change** (v283): VR started in an already loaded level got no stat call at all, so health/ammo sat on the HUD screen. The drawn hud.iggy is now probed on the next UI call. Verified both with a level load and with re-injection into a loaded level.
- **Crosshair in the eye on the left hand** (v284): the mod places the native crosshair at the left hand's aim in eye coordinates, but the HUD screen panel had swallowed it, so it was drawn on the panel and did not match the hand. Its display node is resolved from the engine's crosshair element (AS3 object at +0x3A8) and drawn natively in the eye. Sim: hands pitched 20 degrees down move the reticle down onto the floor (`--scenario aim`).
- **Prompts beside the pointing hand** (v284): while a hand points at the prompt's object, the prompt is world art beside that hand (either hand); prompts without a hand target (takedowns, unknown objects) stay on the HUD screen. Sim pickup: 396 frames of hand prompt (`world_prompt_frames`).

## v287-v295: after the first headset test

- Hands count as present when the runtime gives a valid predicted pose (not only optically tracked): no vanishing left hand, no target reset.
- Interaction query goes to the moving hand; a resting second hand is never probed (fixes the 600 ms choke/kill prompt blink and the 0.5-1 s prompt delay). Prompt beside-hand has 250 ms hysteresis.
- Journal: reach a hand over the shoulder (behind the head) and squeeze grip. Menu hold still works.
- Wheel: no blue dot; elixirs and "Hide/Show all" are clicked with the trigger (zones measured in the 1920x1080 stage; sim: the hide zone toggled the wheel). Sector radius now matches the art. Wheel glyph labels follow the native wheel state (no B/A <-> Stick down/Grip flips).
- HUD size now scales all UI (world UI panels, hand prompts, markers); hand prompts 0.22 m.
- Sword: the right-hand blade had length 0 in collision (weapon bit only set while carrying) - fixed; the blade axis matches the drawn sword (projected tip check). During a swing the native melee aim points at the blade tip, so hits land where the blade is (sim: Slash + Recoil against a wall).
- Peer patch: canted eye frusta (off by default, build/cant).
- Open: physics props (books) grab reliability, elixir ray intermittency, notes show "Esc Close", elixir drink not verifiable in sim (console damage does not lower health).
