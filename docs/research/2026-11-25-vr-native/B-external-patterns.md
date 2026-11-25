# What makes flat-to-VR and native VR games feel "VR native": patterns for a Dishonored-style mod

Research date 2026-11-25. Method: web search plus fetch of reviews, design write-ups and platform docs.
Confidence tags: [P] primary source (developer talk, platform doc, mod page), [R] reputable review, [F] forum/commentary, [?] uncertain or inference.
"Dishonored mapping" lines are my inference unless marked otherwise.
Already built in the mod (not repeated as recommendations): tracked hands + grab, physical sword + charge, dual wield, hand-aimed powers/gadgets, throw gestures, head-directed locomotion + snap/smooth turn, physical crouch, hand weapon wheel, wrist HUD, note tablet, comfort fades/vignette, cutscene handling, FSR4/DLSS, spacewarp, haptics.

---------------------------------------------------------------------------
## 1. STEALTH

**1.1 Light/visibility indicator on the body, not the HUD** (Thief VR: Legacy of Shadow)
- What: The "light gem" sits on the player's hand. [R] IGN review via AOL: https://www.aol.com/articles/thief-vr-legacy-shadow-translates-170000997.html
- Why: It is the most organic form of UI for VR. Counterpoint: one reviewer says the gem feels pointless because you can get very close to a guard's view while in shadow (https://waytoomany.games/2026/01/13/review-thief-vr-legacy-of-shadows/).
- Dishonored: a wrist or ring-style visibility cue (the existing wrist HUD could carry it). Keep the detection model honest, otherwise the cue is meaningless.

**1.2 Physical stealth: real crouch, real lean, real peeking** (Thief VR)
- What: You physically crouch, lean, stretch, scour shelves and slide drawers open. [R] PSU: https://www.psu.com/reviews/thief-vr-legacy-of-shadow-psvr2-review/ ; Road to VR: https://roadtovr.com/thief-vr-review-quest-3-psvr-2-steamvr/
- Why: Head-offset peeking replaces the lean key and makes the stealth skill bodily. The same reviews praise environmental storytelling and lived-in levels.
- Dishonored: lean = real head lean (roomscale offset moves the camera, not just a roll). Keyholes = put your eye to the hole, with the world mask clipping revealing the room beyond. Peeking over cover = stand up slowly, with your head as the exposure.

**1.3 Hand-based takedowns and chokes** (Hitman 3 VR, Batman: Arkham Shadow)
- What: Grabbing a guard with your hands to choke him works well in Hitman VR Reloaded [R] https://roadtovr.com/hitman-3-vr-reloaded-quest-3-review/ . In Arkham Shadow, "making a grabbing motion with the controllers" triggers stealth takedowns [R] GamesRadar preview (via search result summary) https://www.gamesradar.com/games/action/batman-arkham-shadow-hands-on-preview
- Why: The gesture is the verb. No button prompt, so the body does the stealth.
- Dishonored: back-grab with a hand on the shoulder or neck region, then pull. Hold to choke, release to drop. Use hand-over-mouth (fits Hitman's and Assassin's Creed's whistle-style gestures too).

**1.4 Distraction throwing with a real throw** (Hitman 3 VR; Assassin's Creed Nexus VR)
- What: Pulling out a coin and tossing it feels "effortless" in Hitman VR [R] Road to VR link above. Nexus: hand-thrown objects are hard to aim, so throws sometimes hit walls instead of distracting [R] https://roadtovr.com/assassins-creed-vr-nexus-review-meta-quest/ . Nexus's arrows/knives have strong auto-aim, which the reviewer felt undercut stealth satisfaction (same link).
- Why: The coin/bottle toss is a natural verb, but it needs a trajectory preview or forgiving assist.
- Dishonored: bottle/coin throws need a subtle ballistic hint (a short arc or a landing marker you can switch off). Keep assist optional so a skilled throw remains a skill.

**1.5 Scouting view that returns you to the same facing** (Nexus Animus Scout)
- What: Bird's-eye view to tag guards and read patrols; returning to normal keeps your original facing. [R] https://roadtovr.com/assassins-creed-vr-nexus-review-meta-quest/
- Why: A scouting mode that preserves orientation avoids the disorientation of a view swap.
- Dishonored: relevant to Foresight (out-of-body spirit). Keep the body in place, keep the head tracked in spirit view, and restore exact facing on return. See also section 9 on camera control. [?] No source covers an equivalent of Foresight specifically.

**1.6 Hiding spots that physically hide you** (Thief VR)
- What: Many hiding spots, and guards give up after a short wait once line of sight breaks. [R] Road to VR Thief review link above.
- Why: Hiding becomes a physical act (crouch into a corner).
- Dishonored: shadows/under-table/lockers. Do not let AI break the loop: Thief VR is criticized for weak AI (guards don't alarm each other, you can run past and hide to shake pursuit), which undermines the tension. Same Road to VR link.

**1.7 Carrying/dragging bodies** (Nexus, Hitman)
- Nexus: dragging a body makes you move very slowly and bodies can only be grabbed at fixed points. [R] Road to VR Nexus link. Hitman VR lists "picking up bodies" among interactions that fell back to a generic prompt [R] Road to VR Hitman link.
- Why: A free-grab body (shoulders, ankles) plus a weight-based slowdown feels physical. Fixed grab points and prompts feel gamey.
- Dishonored: let any hand grab a body part. Two hands = lift over a shoulder, with the body carried visibly in the physics scene.

---------------------------------------------------------------------------
## 2. MELEE

**2.1 Physical parry/block with the weapon**
- Source: Pseudo Physical Parry for Skyrim VR (community-recommended for melee builds) [F] https://steamcommunity.com/app/611670/discussions/0/4038104984936444224 ; Thief VR's blackjack: parry three guard hits in a row to stagger [R] Road to VR Thief link; Asgard's Wrath 2: blocks and parries are hard to time, but the melee is still among the most satisfying in a headset [R] Digital Trends via search summary https://digitaltrends.com/?p=3489721
- Why: Defence becomes a hand skill. Reviewers of AC Nexus report blocking/parrying is fun in principle but lacks game feel (https://roadtovr.com/assassins-creed-vr-nexus-review-meta-quest/). Lesson: tune timing windows generously and give strong feedback (see sections 9, 10).
- Dishonored: real parry by sword position/velocity vs enemy swing, with a short timing window and a spark/sound/haptic hit. Show the enemy telegraph in animation clearly.

**2.2 Hits tied to physics, not just swing animation** (PLANCK for Skyrim VR; Blade & Sorcery)
- What: PLANCK adds physically accurate melee hit detection and decouples hits from swings; bashes cost stamina as in flat Skyrim. [P] https://www.nexusmods.com/skyrimspecialedition/mods/66025 . Blade & Sorcery's objects and weapons behave "as though they have heft" so "you can't simply tickle enemies". [R] VR Scout via search summary https://vrscout.com/news/hands-on-blade-sorcery/amp/
- Why: Weight and consequence make swings feel real. The fix for exhausting flailing is a stamina/effort meter: Saints & Sinners caps swinging with an in-game stamina meter, whereas Boneworks is limited only by the player's real energy. [R/P] Game Developer interview via search summary https://gamedeveloper.com/design/designing-around-panic-in-i-the-walking-dead-saints-sinners-i-
- Dishonored: minimum swing speed for damage; a swing that is too slow glances. Sword-fighting should be a low-volume, high-consequence pattern. Dishonored is not a wave shooter, so avoid rewarding endless flailing (Nexus reviewer: enemies exploitable by swinging repeatedly, even on the hardest setting, https://roadtovr.com/assassins-creed-vr-nexus-review-meta-quest/).

**2.3 Embedding and yank-free** (Saints & Sinners)
- What: Weapons embed in enemies and must be pulled out with enough force. [P] Skydance Unreal Engine tech blog (via search summary) https://www.unrealengine.com/tech-blog/skydance-interactive-shows-how-it-injected-vr-physicality-and-melee-combat-in-the-walking-dead-saints-sinners
- Why: Makes each kill a choice with a cost. Probably too heavy for Dishonored's flow; [?] consider only for a hardcore option.

**2.4 Boxing/rhythm combat and gesture counters** (Arkham Shadow)
- What: Fights work like boxing; the dodge and counter keep combos going. [R] TechRadar https://www.techradar.com/au/gaming/batman-arkham-shadow-review . Developers cited Beat Saber and Superhot as influences [R] Android Central via search summary https://www.androidcentral.com/gaming/virtual-reality/batman-arkham-shadow-gameplay-story-interview . Counter timing is called tricky mid-fight [R] GamesRadar link above.
- Dishonored: optional "counter flash": a brief enemy weapon-glow cue helps readability (Nexus offers "highlighting of enemy weapons during combat" as an accessibility option, https://www.uploadvr.com/assassins-creed-nexus-vr-review).

**2.5 Stabilize ledge/object handling** (Nexus "hand stabilization")
- Ledge grabbing was unreliable until hand stabilization was enabled. [R] UploadVR https://www.uploadvr.com/assassins-creed-nexus-vr-review
- Dishonored: apply to sword aim assist and parry detection.

---------------------------------------------------------------------------
## 3. RANGED

**3.1 Real reload and weapon-handling** (Titanfall 2 VR mod; Into the Radius; Alyx)
- Titanfall 2 VR mod: manual reloads (eject magazine, cock weapon), fully articulated finger tracking. But small parts are hard to grip and weapon swapping is "cumbersome and clunky" [R] UploadVR https://www.uploadvr.com/titanfall-2-vr-mod-review-a-flatscreen-classic-trades-power-for-presence/
- Into the Radius: manually inserting each bullet adds immersion [R] via search summary (https://roadtovr.com/into-the-radius-quest-2-review/). Alyx: Road to VR says the most-used items deserve the most design care [R/P] https://www.roadtovr.com/these-details-make-half-life-alyx-unlike-any-other-vr-game-inside-xr-design/
- Why: Reload is the most repeated hand action. Make it fast or optional.
- Dishonored: crossbow cocking/loading by hand motion with a "quick reload" fallback. Pistol: hand to hip for bullets with a short, tolerant grab zone.

**3.2 Hitman VR criticism: "simple button presses"** (throw, vault, reload)
- Hitman 3 VR Reloaded: throwing items at enemies, vaulting and reloading are "just simple button presses" that pull you out of the experience. [R] UploadVR https://www.uploadvr.com/hitman-3-vr-reloaded-review/
- Also: bullets travel slowly, sniper scopes open a full-screen mode carried over from flat (Road to VR Hitman link).
- Dishonored: keep scopes (if any) diegetic. Do not drop to a 2D scope overlay.

**3.3 Aim assist to protect stealth satisfaction** (Nexus)
- See 1.4. Strong auto-aim is the thing reviewers say removes skill. Offer it as a slider.

---------------------------------------------------------------------------
## 4. POWERS / MAGIC

**4.1 Alyx Gravity Gloves: lock-on, pull gesture, catch** (Half-Life: Alyx)
- What: three-stage pull (highlight, lock with trigger so it will not switch if the hand drifts, then a physical pull gesture), with a sound for each stage; tuned trajectories so the object is not an auto-snap. [P] https://www.roadtovr.com/these-details-make-half-life-alyx-unlike-any-other-vr-game-inside-xr-design/
- Why: Player and game agree on the target before commit; the world "responds" to the body. Valve (Robin Walker) said the gloves came from a discrete set of real problems, not a sandbox toy [R] PC Gamer via search summary https://pcgamer.com/the-gravity-gloves-are-the-real-star-of-half-life-alyx . Gloves are always on, not a separate weapon (Kerry Davis, DigiPen) https://www.digipen.edu/showcase/news/with-half-life-alyx-digipen-graduate-kerry-davis-helps-redefine-vr-games [R/P]. Also one search summary says they reduced the amount of teleporting needed, but treat as secondary [?].
- Dishonored: Far Reach = point at a surface, lock the target, then pull gesture (or pull with the wrist flick for a mantle). Blink uses a lock-on target marker with a spectral "legs/feet" hint (see 7.3). Domino: link targets by pointing at each and "confirm" with a pinch. Possession: point + hold, with an eye-contact tone.

**4.2 Skyrim VR: gesture cast + wheel for choosing** (VRIK, Spell Wheel VR, MageVR)
- What: VRIK adds a full body, body-based holstering and gesture-based spellcasting/equipping. [R] UploadVR https://uploadvr.com/best-must-have-skyrim-vr-mods-more-immersive/ . Spell Wheel VR brings a Blade and Sorcery-style wheel for weapons/spells; some users say it is slower than VRIK gestures but balanced [F] https://steamcommunity.com/app/611670/discussions/0/4038104984936444224
- Why: Two selection paths (gesture + wheel) cover both fast and learned players.
- Dishonored: already has a hand wheel. Add a favorites-on-body model (see 6.x) for 3-4 hot powers.

**4.3 Telekinesis tuned to forgiving gestures** (Owlchemy "Cosmonious High" accessibility tips)
- What: Telekinetic grabs use a point-and-wrist-flick so players do not need precise timing. [P] https://unity.com/blog/games/accessible-vr-game-design-tips-from-owlchemy-labs
- Dishonored: favour forgiving gestures for Far Reach/Domino. Use big cones, not pixel aim.

**4.4 Blade & Sorcery powers as physics sandbox** (telekinesis works on all objects, especially heavy ones; slow-mo; lightning) [R] https://vrscout.com/news/hands-on-blade-sorcery/amp/ . B&S's reputation: brutal physics-driven combat; also "undeniably janky" [R] https://uploadvr.com/blade-and-sorcery-review . Dishonored mapping [?]: let powers interact with physics objects (Far Reach can pull a bottle, Domino ragdolls follow physics).

**4.5 Fixed-body spirit** (Foresight)
- No clean analogue found in sources [?]. See 1.5 and 9.x for camera-control principles.

---------------------------------------------------------------------------
## 5. TRAVERSAL / CLIMBING

**5.1 Physical hand-over-hand climbing** (Hitman 3 VR Reloaded)
- What: moving your hands to climb poles/ladders is a real VR improvement. [R] UploadVR https://www.uploadvr.com/hitman-3-vr-reloaded-review/
- Counterpoint: Hitman VR also uses "Press A to interact" for ladders and doors (Road to VR Hitman link), which reviewers called a flat-port holdover.
- Dishonored: ladders and pipes as grab-and-pull with both hands, with a hand-hold highlight.

**5.2 Mantle assist and a glowing-edge hint** (Nexus)
- What: hand-holds succeed only about 80% of the time per one reviewer; the reviewer would have preferred a glowing edge on the surface over a UI icon. [R] https://roadtovr.com/assassins-creed-vr-nexus-review-meta-quest/
- Nexus options: Parkour Assistance, Parkour Visualiser (a line where you will jump), Fear of Heights grid. [R] https://www.uploadvr.com/assassins-creed-nexus-vr-review
- Dishonored: mantling = reach for the edge, with an edge glow while a hand is near and a big assist radius.

**5.3 Avoid the "arm distortion" of canned animations** (Nexus air assassinations)
- Air assassinations undercut the payoff because the player's arm distorts into a twisted pose. [R] Road to VR Nexus link.
- Dishonored: when a canned finisher plays, hide the virtual hands (or hold them in a believable pose) instead of letting IK twist them.

**5.4 Blink and teleport comfort** (see section 8 for sources)
- Blink power: Meta says teleport disorients when facing changes and suggests blinks/warps plus spatial sound that changes position with the listener. [P] https://developers.meta.com/horizon/design/locomotion-comfort-usability/ . Alyx's teleport cursor was shaped like spectral legs so players can picture position and posture before committing. [P] https://gamedeveloper.com/design/how-height-audio-cues-and-trust-sculpted-vr-locomotion-in-i-half-life-alyx-i-
- Dishonored: preserve facing on blink unless the user opts in. Use a short (about 100-150 ms) darken/blur and play positional whoosh audio (timing numbers are my suggestion [?]). Offer "dash-blink" or instant modes. Alyx shipped three modes (blink, shift, continuous); Valve notes no one system suits everyone [P] same link.

---------------------------------------------------------------------------
## 6. INTERACTION / PHYSICS

**6.1 Hand/object collision + grab instead of menu pickup** (HIGGS)
- What: hand and item collisions, direct grabbing, gravity gloves, natural looting, pulling armor off bodies, over-the-shoulder inventory. [R] Road to VR https://roadtovr.com/skyrim-vr-mod-higgsvr-hand-gravity-gloves/ ; Nexus page for HIGGS https://www.nexusmods.com/skyrimspecialedition/mods/43930
- Why: the article calls menus and point-and-click interaction "immersion killers". HIGGS+VRIK "makes the game feel almost native" (UploadVR best-mods link above).
- Dishonored: looting corpses and shelves by grabbing coins, valuables, bone charms. Show loot as visible objects (not dissolving into a bag).

**6.2 Per-object audio and sound that matches grabs** (Alyx)
- Nearly every prop has its own shake sound, louder in the nearer ear. [P] https://www.roadtovr.com/these-details-make-half-life-alyx-unlike-any-other-vr-game-inside-xr-design/
- Dishonored: distinct coin, bottle, rune sounds on grab.

**6.3 Physics-body presence** (Boneworks/Bonelab)
- What: simulated body from IK on head and hands; weight extends to the body ("everything has physics and weight, including your own arms"). [R] Android Central via search summary https://www.androidcentral.com/gaming/virtual-reality/bonelab-review-the-new-standard-for-vr-physics-based-games
- Cost: character tangled in environment, puppet-like feel, can be nauseating. [R] Mixed-news / Gamingtrend via same search.
- Dishonored: do not go full physics body. Use the weight cue only on carried bodies, heavy swords, and doors (keep head/body traversal predictable).

**6.4 Hands that grab even in rushed moments: "fumbly" interactions** (Thief VR criticism)
- Object interaction, inventory and lockpick grabs are "fumbly". [R] https://roadtovr.com/thief-vr-review-quest-3-psvr-2-steamvr/ . Nexus: items teleport into inventory on grip, menus use a laser pointer, ~3s holds for simple actions [R] Road to VR Nexus link.
- Dishonored: oversized grab zones, gentle "magnet" to the nearest valid grabbable, and no long hold-to-confirm for ordinary interactions.

**6.5 Doors, keys and locks as physical objects**
- Thief VR lockpicking is done in hands; Nexus's key grab bug forced lockpick. [R] Road to VR Thief + UploadVR Nexus links.
- Dishonored: safe cracking, key slots, lever handles with hand motion rather than a prompt (Hitman's generic "Press A" is the anti-pattern).

**6.6 Reading notes / audiographs**
- [?] No primary source found. Existing note tablet is good; test font size and tablet distance. Thief VR reviewers say some lighting is too dark for comfortable reading (Road to VR Thief link).

---------------------------------------------------------------------------
## 7. INVENTORY / UI

**7.1 Diegetic wrist/hand UI** (Alyx, Metro Awakening, Thief VR light gem, Hitman smartwatch)
- Alyx: health/ammo/resin on the back of the left glove, items in wrist pockets, a backpack that holds ammo (reach over shoulder). [R] PCGamesN, Destructoid, CGMagazine via search summary https://www.pcgamesn.com/half-life-alyx/vr-design
- Metro Awakening: no on-screen UI; wristwatch tracks light exposure and filter time; players wipe fogged lenses by hand. [R] via search summary https://uploadvr.com/metro-awakening-impressions ; https://roadtovr.com/metro-awakening-vr-review-quest-pc-vr-psvr-2/
- Hitman VR: smartwatch shows map; two quick slots. [R] https://roadtovr.com/hitman-3-vr-reloaded-quest-3-review/
- Why: information is where your hand already is; no HUD plane to fight with the world.
- Dishonored: wrist = mana/health/ammo, light/visibility cue; open the journal/map on the off-hand (hold like a clipboard).

**7.2 Body-worn holsters/slots** (Saints & Sinners, Into the Radius, VRIK)
- S&S: players instinctively toss items over the shoulder into a backpack, so the team built that. [R] TechSpot interview via search summary https://www.techspot.com/article/2375-the-walking-dead-vr-interview/ . Into the Radius: gear on the body works, but one reviewer kept pulling the flashlight instead of the map; spatial-pack backpack clips items. [R] Road to VR https://roadtovr.com/into-the-radius-quest-2-review/
- Dishonored: 4 holsters (sword hip, pistol thigh, crossbow back, gadget chest) plus over-the-shoulder "bone charm/spare bolts" pouch. Mind overlapping zones: keep them spatially distinct and give each a distinct haptic/sound "hover" cue; offer a "snap return" for dropped important items (Into the Radius's map auto-returns).

**7.3 Menus that work with both laser and touch** (Alyx)
- [P] Road to VR Alyx link. Dishonored: journal/map as a touchable tablet plus a laser fallback.

**7.4 Highlight with light, not outlines** (Alyx)
- Key items use subtle lights rather than obvious colored outlines. [P] Road to VR Alyx link. Dishonored: soft glow on interactables, not a hard wireframe, though accessibility modes can have a stronger outline.

**7.5 No dead-center objective marker** (Nexus criticisms)
- Constant objective markers and 2D pop-ups feel flatscreen-like; UI cannot be fully disabled. [R] Road to VR + UploadVR Nexus links.
- Dishonored: let the objective marker, quest text and map dots be turned off, or move them to the wrist/journal.

**7.6 Show the ammo/charge state physically** (Alyx)
- Per-bullet lights and a radial indicator on the magazine; empty magazines emit no light. [P] Road to VR Alyx link.
- Dishonored: pistol and crossbow show remaining count on the weapon itself (small light or a bolt visible in the stock). Mana as a glow in the off-hand.

---------------------------------------------------------------------------
## 8. COMFORT / ACCESSIBILITY

**8.1 Offer multiple locomotion modes, not one** (Alyx)
- Alyx shipped blink teleport, shift teleport, continuous. Valve says none suits all. [P] https://gamedeveloper.com/design/how-height-audio-cues-and-trust-sculpted-vr-locomotion-in-i-half-life-alyx-i-
- Alyx's movement system was changed because tall testers kept getting stuck (PC Gamer: https://pcgamer.com/half-life-alyxs-movement-system-was-changed-because-tall-testers-kept-getting-stuck [R]). Continuous turning was added post-launch (https://www.pcgamer.com/half-life-alyx-smooth-turning).
- Dishonored: add teleport/step-move options for movement (the mod has smooth locomotion); also allow Blink as its own comfort-friendly movement.

**8.2 Turning: snap/quick first** (Cloudhead Games)
- Continuous yaw rotation was the worst for sickness; instantaneous "comfort turns" avoid it. [P] https://roadtovr.com/cloudhead-games-lessons-learned-five-years-vr-locomotion-experiments/2/ . Meta suggests fixed angles (15/30/45/90/180) and reliable controls. [P] https://developers.meta.com/horizon/design/locomotion-comfort-usability/

**8.3 Vignette + acceleration management** (Meta)
- Vection from optic flow; darken edges or only block high-motion regions. Quantized velocity, brief accelerations, soft camera collisions, stabilised camera elevation, matching real speeds (walk ~1.4 m/s, run ~2.8 m/s). [P] https://developers.meta.com/horizon/design/locomotion-comfort-usability/
- Dishonored: Dishonored's flat sprint and mantle speeds feel fast in VR. Offer a speed cap, an acceleration ramp, and stairs/ledge elevation smoothing. (Mapping is mine.)

**8.4 Presets and extensive options** (Nexus)
- Four preset comfort categories by VR experience, adjustable vignette, snap/smooth turn with speed and angle, hand stabilization, sprint on stick, haptics off, break reminders, virtual nose, fear-of-heights grid, swappable buttons, three subtitle sizes. UploadVR: "most extensive comfort and accessibility settings I've ever seen in a VR game". [R] https://www.uploadvr.com/assassins-creed-nexus-vr-review

**8.5 Seated play and height** (Ultraleap, Owlchemy, Thief VR)
- Seated should be a first-class mode; in-game height offset adjustable; reachable height adjuster; grab handle for moving menus. [P] https://docs.ultraleap.com/ultralab/vr-gaming-accessibility-recommendations.html (note Ultraleap is not XR Access). Owlchemy: anything more than an arm's length above the head is unreachable for seated players. [P] https://unity.com/blog/games/accessible-vr-game-design-tips-from-owlchemy-labs . Thief VR: reaching items can be awkward when seated (Road to VR Thief link). Includes artificial crouch for seated.
- Dishonored: holster and wheel positions should be derived from a calibrated shoulder/hip height, not from absolute height; add a "pull holsters up" slider. Auto crouch/lean buttons for seated.

**8.6 One-handed and left-handed modes** (Owlchemy)
- Turning off the second controller enables one-handed mode automatically; menus open by double tap. A 2025 Frontiers paper found only 2 of 39 free VR experiences offered a one-handed mode (via search summary of https://www.frontiersin.org/journals/virtual-reality/articles/10.3389/frvir.2025.1586875/epub) [?].
- Dishonored: all gestures should have a single-hand route (sword + off-hand power can fall back to a single-hand toggle). Swap dominant hand globally (the mod's wrist HUD, holsters and the weapon wheel should mirror).

**8.7 Blink/cutscene comfort specifics** (see section 9)

**8.8 Frame-rate consistency matters more than anything** (Meta)
- Consistent frame rate is crucial; judder is uncomfortable. [P] https://developers.meta.com/horizon/design/locomotion-best-practices/ (via search summary [?])

---------------------------------------------------------------------------
## 9. HAPTICS / AUDIO / PRESENTATION / CUTSCENES

**9.1 Haptic language (Meta guidelines)** [P] https://developers.meta.com/horizon/design/haptics-best-practices/ (via search summary)
- Supporting role (not the star), tightly synced with audio/visual, pauses between effects, clear cause and effect, intensity matched to sound, limited bandwidth (recognition over recall), optional/mutable.
- Nexus uses subtle haptics for ledge grabs and unsheathing swords (UploadVR Nexus link).
- Dishonored: define a small vocabulary. Examples: holster-hover tick, grab click, blade-contact thud (scaled by velocity), parry spark, Blink "charge" (rising), Far Reach "lock" (sharp tick), Dark Vision (low pulse), choke grip (sustained low). No source gave a standard "grab vs impact" vocabulary [?], so treat the list as my suggestion.

**9.2 Spatial audio does reorientation work**
- Meta: spatial sound changing position with the listener helps reorientation during teleports. [P] Locomotion comfort link above. UploadVR's Skyrim list includes True 3D Sound. [R] https://uploadvr.com/best-must-have-skyrim-vr-mods-more-immersive/
- Thief VR has a bug where sound muffling through walls is inconsistent (Road to VR Thief link): in a stealth game, audio bugs are gameplay bugs.
- Dishonored: positional guard footsteps with proper occlusion, and head-relative sound for Foresight.

**9.3 Cutscene camera: do not move the head** (guidance)
- nDreams: cutscenes that pull the camera away "really make people feel sick"; use Half-Life-style scenes where the camera stays with the player [R/P] https://gamedeveloper.com/audio/the-dos-and-don-ts-of-designing-vr-games (via search summary [?]).
- Cymatic Bruce talk: a hard cut is like instant teleportation; keep head tracking through, use fades, let the user control the transition. [P] https://roadtovr.com/vr-design-splash-screen-shutdown/ (via search summary [?]).
- RE7 VR mod review: the game moves the camera on its own in cutscenes with no tunnel vision or virtual-screen option; "can lead to severe motion sickness" and spoiled the game for the reviewer. [R] https://mixed-news.com/en/resident-evil-7-vr-mod-review-pretty-scary-and-a-little-messed-up/
- Dishonored: virtual cinema screen as a fallback for any cutscene that must move the camera, world-locked "diorama" for scripted scenes, and in-world scenes where the camera stays in a fixed place (Dishonored's mission starts, loading screens and the Void). Offer a "skip/summary" for unavoidable ones.

**9.4 Presence from NPC embodiment** (Titanfall 2 VR mod)
- The reviewer's strongest praise: connection with the physically present BT. [R] https://www.uploadvr.com/titanfall-2-vr-mod-review-a-flatscreen-classic-trades-power-for-presence/
- Dishonored: ensure NPCs face and react to head position (a guard noticing you when you look at him).

**9.5 Pre-rendered "watch your avatar" interactions are the immersion-breaker in RE7 VR**
- "Most object interactions still play pre-rendered animations, so you mostly watch your avatar." [R] mixed-news RE7 link.
- Dishonored: for interactions like opening doors, replace the canned animation with a hand-driven version, and hide the arms during unavoidable ones.

---------------------------------------------------------------------------
## 10. PERFORMANCE

- Titanfall 2 VR mod "runs great, zero crashes" earned goodwill; "jank, jitters" cost it [R] UploadVR link.
- RE7 mod: 25-30 fps untuned, ~45 fps after settings tuning with motion smoothing to 90 [R] mixed-news link.
- Meta: consistent frame rate is crucial; judder is uncomfortable (Meta locomotion best-practices link).
- Hitman 3 VR (Quest 3): visuals "muddy textures, popping geometry, blocky copy-paste NPCs"; stability issues; the game scored 3/10 (Road to VR). Nexus on Quest 3 stutters during waves, objects pop in (UploadVR).
- Thief VR / Skyrim VR: VR FPS Stabilizer is a "must have" for reducing reprojection spikes [R] UploadVR best-mods link.
- Dishonored: expose per-effect toggles (SSAO, post-process, volumetrics, shadows); use dynamic resolution; keep a stable lower frame rate with motion smoothing instead of unstable high.

---------------------------------------------------------------------------
## 11. WHAT REVIEWERS CRITICISE IN VR PORTS (with sources)

1. **Generic "Press A to interact"** (ladders, doors, picking up bodies) makes a port feel like a port. Hitman 3 VR Reloaded: https://roadtovr.com/hitman-3-vr-reloaded-quest-3-review/
2. **2D menus/inventories and objective markers/pop-ups** left in: Hitman (Road to VR), Nexus (https://roadtovr.com/assassins-creed-vr-nexus-review-meta-quest/).
3. **Simple button presses for throw/vault/reload.** UploadVR Hitman: https://www.uploadvr.com/hitman-3-vr-reloaded-review/
4. **Full-screen scope or other flat modes.** Road to VR Hitman link.
5. **Mod that is gamepad-only / no motion controls / no VR menus / no cutscene handling**: RE Requiem praydog mod ("basic, little VR-specific design") https://roadtovr.com/resident-evil-requiem-vr-mod-praydog/ ; Luke Ross R.E.A.L. mods criticised for no motion-controller support (comments on https://www.roadtovr.com/?p=126611 via search summary [F]), while defenders say presence matters more [F].
6. **Cutscenes that take the camera**: RE7 VR mod (mixed-news link). No tunnel vision or virtual-screen option.
7. **Pre-rendered interaction animations** (RE7 mod).
8. **Jank, jitter, performance**: Titanfall 2 (UploadVR), Hitman 3 VR (Road to VR).
9. **Hands that fail to grab reliably** ("fumbly"): Thief VR, Nexus ledge grabbing.
10. **Weak AI undermining stealth** (Thief VR; Nexus NPCs "often fail to notice the player"). UploadVR Nexus link; Road to VR Thief link.
11. **Canned animations with distorted arms** (Nexus air assassinations).
12. **Auto-aim that removes skill** (Nexus).
13. **Recharging health removes tension** (Nexus): suggestion discrete hit points as a difficulty option.
14. **Physics body tangling/puppeteer feel**: Bonelab (Mixed-news / Gamingtrend via search summary).
15. **Melee without force feedback** is an unavoidable limit: UploadVR on Asgard's Wrath 2 via search summary https://uploadvr.com/asgards-wrath-2-review/. Mitigate with haptics + stamina + audio (see 9.1).
16. **Camera/UI placement and unintended camera angles** for UEVR conversions: MoguraVR (via search summary, Japanese) https://www.moguravr.com/?p=243731 [F/?].

---------------------------------------------------------------------------
## 12. PRIORITISED SHORTLIST FOR THE DISHONORED MOD (my synthesis)

High value / low-to-medium effort:
1. Physical lean and keyhole peek via head offset (1.2).
2. Hand-based chokes and body carry with free grab points (1.3, 1.7).
3. Gravity-gloves-style lock + pull for Far Reach and targeted powers; Blink with destination preview, preserved facing, spatial audio (4.1, 5.4).
4. Loot by grabbing (HIGGS model) and remove any menu-based pick-up (6.1).
5. Body holsters/over-shoulder pouch with distinct hover haptics (7.2).
6. Wrist-based visibility cue and objective hints; ability to disable all 2D markers (1.1, 7.1, 7.5).
7. Haptic vocabulary + per-object sound (9.1, 6.2).
8. Comfort presets; locomotion alternatives; speed/acceleration caps; seated/height-aware holsters; handedness mirror; one-hand fallback (8.x).
9. Cutscene fallback: world-locked virtual screen or fade-to-black; camera-free where possible (9.3).

Medium:
10. Parry tuning with telegraph glow and generous windows; swing-speed damage threshold (2.1, 2.2).
11. Foresight: preserve body and head tracking, restore exact facing (1.5).
12. Physical ledge grabs with edge glow and assist radius (5.2).
13. Hand-driven door/lock/key interactions (6.5).

Gaps/uncertainty:
- I found no primary source on a Foresight-like out-of-body mechanic, Semblance (face stealing), or Domino-like linking in other VR games.
- Flat2VR Studios: confirmed titles include Trombone Champ: Unflattened, FlatOut, Roboquest, Wrath: Aeon of Ruin, Surviving Mars: Pioneer (https://roadtovr.com/flat2vr-announcement-port-wrath-aeon-ruin-roboquest-flatout-trombone-champ/). I could not confirm any Thief or Kingdom Come entry [?]. Half-Life 2 VR and Doom VR modders joined Flat2VR (https://www.androidcentral.com/gaming/virtual-reality/flat2vr-studios-hires-talented-vr-modders-for-licensed-quest-ports).
- reference VR mod, Half-Life 2 VR mod, Into the Radius stealth, Red Matter and Asgard's Wrath 2 telekinesis details: not researched in depth.
- Many items cite "via search summary" because the source page was only seen through search snippets; verify before quoting.
