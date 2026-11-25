# What would make Dishonored 2 / DOTO feel more VR native (research, 2026-11-25)

Three research tracks, merged here:
- **A**: an internal audit of this repo: what exists, what is unfinished, and what the user reported from headset sessions (185 quotes in code comments and docs). See [A-internal-audit.md](A-internal-audit.md).
- **B**: how native VR games and the best flat-to-VR mods make interactions feel native. Sources include Half-Life: Alyx, Thief VR, Assassin's Creed Nexus, Hitman 3 VR, Skyrim VR's HIGGS/PLANCK/VRIK, Meta and Owlchemy guidelines. See [B-external-patterns.md](B-external-patterns.md).
- **C**: what players ask for from a Dishonored VR mod, and what they criticise in comparable ports. See [C-player-wishes.md](C-player-wishes.md).

Caveats:
- Many web citations in B and C come from search summaries rather than full page reads. They are marked there; verify before quoting.
- Readable player sources for Dishonored itself are thin.
- Nothing here has been tried in a headset.

The bar the mod is already near: B and C agree that a port "feels native" when interactions happen with the hands and body, not button prompts. They also name stable frame pacing, comfort options and cutscenes that never take the camera. The mod already does most of the big ones:
- 6DoF hands and a full body
- physical grab, lean, door peek, keyhole by grip and hand chokes
- a motion sword with block and parry, hand-aimed guns, powers and gadgets
- a hand wheel, a wrist HUD and a palm note tablet
- 3D or theater cutscenes with the head owning the view, comfort fades and a vignette
- FSR4/DLSS and its own spacewarp

What is left is mostly **reliability**, **feedback** (haptics, audio, diegetic state), **comfort breadth**, and **making each power a hand verb**. Avoid the things reviewers punish in ports: fumbly grabs, "Press A" for physical acts, camera-stealing scenes and unstable frame rate.

## Tier 1: highest impact, small to medium effort, mostly checkable in the Linux sims

**Already resolved, dropped from the list:**
- Item 1, frame-rate stability (C: GitHub #2 ran at 36 fps on an RTX 5080; A G12).
- Item 2, both eyes showing one frame during climbs and takedowns at low fps (GitHub #2; A G13).

The automatic spacewarp covers both (user, 2026-11-25). The other items keep their numbers so the appendices still match.

| # | Improvement | Why (evidence) | How in this mod | Effort / risk | Sim-checkable? |
|---|---|---|---|---|---|
| 3 | **Grabs that work the first time: bodies and shoulder carry** | The user's most repeated stealth complaint: "the shoulder carry still needed 2-3 tries" (v1390), "grip can't pick them up" (v1145). Reviews call fumbly grabs the top immersion breaker (B 6.4, C: Thief VR, Nexus) | Grip alone on any limb or torso grabs. A torso hold raised to the shoulder hands off in one squeeze. Keep the prompt stable while a body is targeted | M / medium (iterated before) | Yes: the corpse_* scenarios and test commands to put Billie or Corvo next to a body |
| 4 | **Weapon wheel picks what you point at** | A known issue in 3 releases (v1393, v1445, v1457); user: "I turn my hand to the right and the indicator stays on the left" (v775) | Select by the hand ray's sector at release, with hysteresis. Never commit a sector the pointer left more than X ms ago. Log pointed versus committed | M / medium | Yes: the scripted wheel and the native wheel state |
| 5 | **A haptic vocabulary** | Meta: short, tightly synced, a small vocabulary, optional (B 9.1). Audit: no pulse when you are hit, when a block takes a blow, on a power cast, or scaled by weapon recoil (A G18) | One table of named pulses with a priority (hit taken > block impact > cast > fire > UI tick) and per-hand routing: hit taken (both, strong), block impact (sword hand), power cast or commit (power hand), recoil per weapon, low health heartbeat (wrist), Dark Vision and Heart pulses | S / low | Counters yes, feel no |
| 6 | **Comfort and accessibility breadth** | Nexus is praised for the widest options; one-handed, seated and handedness modes are standard (B 8.4-8.6). The mod has none of seated, height offset or left-handed (A G16) | (a) Seated mode with a height offset; holster and temple zones relative to the calibrated height. (b) Left-handed preset: mirror powers and crosshair to the right, the sword to the left, swap sticks. (c) A button fallback for every gesture: shoulder draw, temple guide and journal (A G17). (d) An optional fade on Blink and Far Reach, like the existing Displace fade | S-M / low | Mostly yes |
| 7 | **Crossbow bolt identity after a pickup or toss** | Listed as remaining roughness in the README; user: "I chose the sleep bolt and got the regular one" (v1381) | Re-assert the hand's own ammo entry after any pickup or re-equip, as the Voltaic per-hand bolts do | M / medium | Yes |

## Tier 2: make every power a hand verb (B section 4; the user's "native" bar)

The B research found one consistent pattern: player and game **agree on the target before commit**, with a distinct sound and tick per stage (Alyx Gravity Gloves: highlight, lock, pull). That is cheap in this mod because the hand aim, power-cancel and haptics plumbing exists.

| # | Power | VR-native verb | Notes |
|---|---|---|---|
| 8 | Far Reach (D2) | Point, lock with the trigger, then a wrist-flick pull, so the hand drifting does not change the target. The existing "yoink" gesture (`src/vr_gestures.hpp`, opt-in, untested) is the start | Grip cancel already exists (#6). Add tick, lock and pull stages |
| 9 | Domino (D2) | Point at each guard and pinch to link; each link ticks the hand | Uses the game's own targeting per press |
| 10 | Possession (D2) | Point and hold: a rising tone and tick as the target locks, then release | First confirm the widened possession gate in a headset (checklist #12) |
| 11 | Blink and Displace | Destination preview that keeps your facing (the game's own marker), a short fade option, positional whoosh (B 5.4) | The Displace fade exists; extend it to Blink as an option. Measure where the Displace marker lands versus the hand ray (A G3) |
| 12 | Foresight (DOTO) | **Tag by pointing** while scouting, and a faint tether line back to the body (A G2: "Not done" in the release notes). Restore exact facing on return (B 1.5) | Point and press uses the game's own ACTION_EYE_MARK on the hand ray |
| 13 | Dark Vision, Bend Time | Optional gestures: palm to the temple (exists, opt-in) and a clap or fist close | Ship the power gestures after a headset test. Keep the wheel and trigger path as the default (B 4.2: both paths serve different players) |
| 14 | Semblance (DOTO) | Done in sims (behind an unaware NPC, or a sleeping body). Show the stolen face on the wrist so you know you are disguised (design `DOTO-VR.md`) | The disguise state is now observed (mimic +0x4c) |

## Tier 3: diegetic state and feel (B sections 7 and 9; reviewers' "port tells")

| # | Improvement | Why | How |
|---|---|---|---|
| 15 | Ammo and charge on the weapon itself | Alyx shows rounds as lights on the magazine (B 7.6). Our right Voltaic copy shows no charge lights (sim 2026-11-25) | The pistol and crossbow count on the weapon model, or a small wrist glyph near it. Light the Voltaic copy's charge lamps by copying the engine gun's shader parameter (needs RE) |
| 16 | Objective markers, quest text and map dots can all be switched off or moved to the wrist and journal | "UI cannot be turned off" is a top Nexus criticism (B 7.5) | Use the existing native marker path, plus a setting |
| 17 | Distraction throw hint (optional) | Real throws are loved but hard to aim (Nexus); a short arc or landing marker helps (B 1.4) | Off by default. Draw it only while a bottle or coin is held behind the shoulder |
| 18 | Two-handed stabilisation for the crossbow and pistol | Standard VR gun feel; never done (A G19) | The off hand on the barrel smooths aim. Optional |
| 19 | Keep tracked hands and body in 3D cutscenes (option) | User asked for it (v642). Reviewers punish scenes that take the camera or the hands (B 9.3, 9.5) | A knob exists (`build/cutscene-avatar`); surface it in VR Settings |
| 20 | Head-in-wall fade | Standard comfort guard (A G15) | Only after a reliable head query exists (`docs/physical-head-translation.md`) |

## Long term or high risk (keep in view)

- **True spatial audio (HRTF)**: the listener follows the head today but there is no spatialiser (A G21). In a stealth game, audio bugs are gameplay bugs (B 9.2, Thief VR). L / high (Wwise).
- **GOG version** (GitHub #1, two users): the DOTO port pipeline (`tools/doto/port.py`, the target JSON) could in principle target another exe. M-L.
- **Per-foot IK on stairs**, toe and heel roll (A G20).
- **Physical reload** (Alyx, Titanfall 2 VR): high effort against the game's animation-driven reloads. Only if everything above is done.

## Deliberately not recommended

- **Hand-over-hand climbing.** The user accepted the game's mantle, and Dishonored's levels are built around it. Reviewers also flag fumbly ledge grabs (Nexus).
- **Replacing native prompts or menus with invented UI.** The user's standing rule (10-10 report): the game's own prompts, placed in the world.
- **A full physics body (Bonelab style)** beyond the current hand bodies: big risk of tangling and a "puppeteer" feel (B section 11 item 14). The experimental `hand-body` covers the useful part.

## What only a headset can settle (no sim can)

Everything DOTO, all Possession movement, and the D2 comfort items marked "not yet confirmed" in `docs/PLAYING.md`. The checklist is `docs/VR-DOTO-2026-11-24-TEST-CHECKLIST.md`. One structured session turns many "built" items into known-good. The audit ranked it the single most valuable step (A, top 15 #1).

## Suggested order

Items 1 and 2 are resolved by the automatic spacewarp.

1. Items 3, 4, 7: the three interaction reliability bugs.
2. Items 5, 6: haptic vocabulary, seated and left-handed mode, gesture fallbacks.
3. Items 8 to 14: powers as hand verbs (Foresight tagging and Far Reach lock-pull first).
4. Items 15 to 19: diegetic state, UI toggles, cutscene hands.

All of 3 to 7 can be built and checked with the Linux lab: scenarios, test commands, state traces.
