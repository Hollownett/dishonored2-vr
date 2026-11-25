# Player wishes for a Dishonored VR mod (research, 2026-11-25)

Method note: web search tool returns summaries only; Reddit pages mostly unreadable (fetch failed). Evidence is thinner than hoped. Items marked [UNCERTAIN] are low-confidence/secondary.

## (a) Dishonored-VR-specific

- No official Dishonored VR exists. Arkane level designer Christophe Carrier (2016) said they were "thinking about it", Blink would be the only movement option (nausea), and gore animations would need change.
  https://www.gamesradar.com/dishonored-vr-were-thinking-about-it-says-level-designer-christophe-carrier
  https://www.playstationlifestyle.net/?p=525979
- Luke Ross R.E.A.L. VR: no Dishonored title found (GTA V, RDR2, NOLF2, Elden Ring, Spider-Man, Indiana Jones, BG3, Doom TDA etc.). [UNCERTAIN: absence of evidence; Patreon list not checked]
  https://roadtovr.com/luke-ross-vr-mods-free-cyberpunk-2077/
- VorpX: Reddit users say Dishonored works via vorpX with lots of tuning; Blink makes climbing easy, but forced camera movement in fights is unpleasant; no motion aiming. [UNCERTAIN: secondhand search summary; thread not read]
  https://cal1.lr.ggtyler.dev/r/dishonored/comments/1gmx4qu/dishonored_vr
  https://redlib.hackliberty.org/r/virtualreality/comments/1l3hwlm/thief_vr_legacy_of_shadow_reveal_trailer
- Hollownett/dishonored2-vr (GitHub, read via gh): releases v1129..v1457 (Nov 2026), latest "v1457: performance improvements". Open/closed issues (6 total):
  - #1 (open): GOG version compatibility requested by two users (owner: planned, Steam stable first).
  - #3/#4/#5: three users asking for Dishonored: Death of the Outsider (owner: "next step").
  - #2: "VR effect gone during animations" (climb/takedown): both eyes showing same frames at ~35.9 fps on RTX 5080 at 200% render scale 3648x3936; fixed by lowering resolution/shadows/DLSS. Lesson: players overspec resolution; need auto presets / fps warning.
  - #6 (open): "Far Reach with Emily" (content not read) [UNCERTAIN].
  https://github.com/Hollownett/dishonored2-vr/issues
  https://github.com/Hollownett/dishonored2-vr/releases
- Reddit features explicitly mentioned (Blink, Dark Vision, leaning, keyholes, possession) were NOT found in readable sources; only Blink/comfort and forced-camera-in-combat. [UNCERTAIN/gap]

## (b) Lessons from comparable games

- Assassin's Creed Nexus VR: praised for combat (parry, active swings, enemy weapon highlight), parkour, "most extensive comfort and accessibility settings"; criticised for ledge-grab failures (hand stabilization fixed), UI that can't be turned off/objective marker, shallow stealth AI, Quest 3 stutter with reinforcements. Other reviews call sword combat generic.
  https://uploadvr.com/assassins-creed-nexus-vr-review
  https://shacknews.com/article/137810/assassins-creed-nexus-vr-review-score
- Thief VR: Legacy of Shadow (2025): praised comfort (8/10, many options, no forced perspective), directional audio, immersion, blackjack parry as recourse; criticised fumbly object grabbing, awkward bow aim, unreliable pulling items from inventory, seated reach problems, dumb guards, stutter on SteamVR/Link, linear/shallow, weak PCVR graphics. Critic Metascore 69; Steam Mostly Positive.
  https://roadtovr.com/thief-vr-review-quest-3-psvr-2-steamvr/
  https://uploadvr.com/thief-vr-legacy-of-shadow-review
  https://www.cgmagonline.com/review/thief-vr-legacy-of-shadow-psvr-2
  https://vaporlens.app/app/2800080/thief_vr_legacy_of_shadow [low confidence aggregator]
- Hitman 3 VR Reloaded (Quest 3): panned (Road to VR 3/10; user ~2.7/5): PS2-like visuals, lost cinematic cutscenes/lighting, awkward controls, technical issues; dual-wield praised. Lesson: visual fidelity and polish loss kills "feel native".
  https://roadtovr.com/hitman-3-vr-reloaded-quest-3-review/
  https://www.uploadvr.com/hitman-3-vr-reloaded-review/
- Skyrim VR HIGGS/PLANCK/VRIK: users say Skyrim VR "doesn't feel like a real VR game" without them; physical melee hit detection, hand collision, grabbing, two-handed weapons. Also fixes for "missed hits" when swinging fast. (anecdotal)
  https://www.nexusmods.com/skyrimspecialedition/mods/43930
  https://roadtovr.com/skyrim-vr-mod-higgsvr-hand-gravity-gloves/
- Half-Life 2 VR mod: ~Overwhelmingly Positive (5.5k+ reviews); praised for Alyx-style QoL: wrist HUD, holographic weapon wheel, comfort features.
  https://www.pcgamer.com/half-life-2-vr-mod-promises-full-rift-vive-and-motion-controller-support
  https://store.steampowered.com/app/658920
- UEVR-style generic mods: limitations = no real motion aiming, missing physical interaction, UI placement; Subnautica 2 mod added head-locked UI option.
  https://roadtovr.com/subnautica-2-vr-mod-vr-controller-support/

## (c) General VR mod requests (mostly inferred; few direct sources found)

- Comfort option breadth (snap/smooth turn, vignette, hand stabilization, UI-off): AC Nexus, Thief VR sources above.
- Seated play/reach: Thief VR review (seated reaching struggles). 
- Performance presets/render-scale guidance, SteamVR/Link stutter: Thief VR, GitHub issue #2.
- Store compatibility (GOG): GitHub issue #1.
- Left-handed, physical crouch/lean, body holsters, subtitle placement, Quest/VD/Steam Link: no direct external sources found [UNCERTAIN - inferred from genre norms]. D2VR README already lists physical crouch/lean, holsters, VD/SteamVR/Steam Link.

## Ranked top 12 for a Dishonored VR mod

1. Performance/auto-presets and stable 72/90 fps (frame pacing, warning on low fps) - issue #2, Thief VR/Hitman lessons
2. Dishonored: Death of the Outsider support - 3 issues
3. GOG (and other store) version support - issue #1
4. Comfort breadth: Blink (with and without teleport-style), vignette, snap/smooth, seated, hand stabilization - Carrier 2016, AC Nexus, Thief
5. Reliable physical melee: parry/block hit detection, forgiving swing speed - Skyrim mods, AC Nexus
6. Reliable grabbing/object & ledge interaction (not fumbly) - Thief VR, AC Nexus
7. Hand-aimed powers / Blink targeting with hand - vorpX complaint of no motion aiming
8. Visual fidelity and cutscene handling (3D/theatre) - Hitman VR lesson
9. In-world/wrist HUD, UI toggles, menus usable in VR - HL2 VR, AC Nexus
10. Weapon aim quality (crossbow/pistol), holsters - Thief bow, Hitman dual wield
11. Stealth feel: directional audio, leaning/peeking, keyholes - Thief VR [UNCERTAIN for Dishonored specifically]
12. Left-handed mode / seated reach / subtitles - genre norm [UNCERTAIN, no direct sources]
