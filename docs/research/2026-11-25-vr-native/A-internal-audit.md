# Internal audit: Dishonored 2 / DOTO VR mod (branch feat/doto-vr, 2026-11-25)

Read-only. Sources: repo docs, `src/` comments (185 distinct "user, headset vNNN" quotes), `tools/default_features.json`,
GitHub issues #1/#2/#6. Paths are relative to `~/github/dishonored2-vr-doto`. **INFERRED** = my deduction, not stated in a source.

---

## 1. What already exists (do not re-recommend)

**Locomotion / turning**
- Head-directed stick walk, sprint on click; native / snap (30/45/90) / smooth (45/90/120 deg/s) turning (`docs/LAUNCHER.md:87`). User-accepted; AGENTS.md says preserve it.
- Smooth-turn ramp 0.15 s (`turn-ramp`), camera height smoothing on landings/stairs (`camera-smoothing`), physical crouch (`physical-crouch`) + stick-down crouch, physical lean (`docs/PLAYING.md:59`), jump = right stick up or both hands raised over head (README controls table). Swimming dive/rise on stick.
- D2 Possession: host moves where you look (`possession-view-move`, built, not seen in a possession: `docs/VR-DOTO-2026-11-24-TEST-CHECKLIST.md` #12).
- DOTO Foresight: tracked spirit with normal locomotion/turning (`DOTO-VR.md:155`, `tracked_foresight`).

**Hands / grab / physics**
- Tracked hands + full body + tracked arms/legs (`full-body` default on), avatar calibration (`src/avatar_calibration.hpp`), hand reach clamp, physical hand bodies (`hand-body`, experimental), carried objects stop at walls (`held-clamp`), finger IK wrapping any object, fingers stop on surfaces, Touch capacitive finger sensing (`finger-touch`), finger ease-in, palm-based selection (`palm-select`), grip-alone grab, push light props with open hand, prop throws deal physics damage (`docs/PLAYING.md` "Aiming, grabbing").
- Bodies: drag by limb/head, torso hold -> shoulder carry (game CarryCorpse), slip instead of drop, 2x easier corpse pull, living-guard grab/yank to ragdoll (`living-hold`), face grab.
- Haptics: grab/release scaled by mass and speed (`src/hand_feel.hpp:12`), holster zones, wheel sectors, sword swing/charge/power attack, stealth kill, weapon contact/clash, gun fire, mine mode ticks (pulse sites in `src/dxgi_probe.cpp` ~2872-18268).

**Combat / sword / block**
- Motion swings press the game's attack (`intent-attacks`), sword contact damage, weapon collision with world, impact FX, stealth kill by contact, forgiving block ("any blade in front"), parry by push, power strike charge, sword throw + recall, normal/reverse grip from fist orientation, faster slashes 1.7x (`tools/default_features.json`; `docs/VR-CONTROLS.md`).
- DOTO Void Strike via the same charge (hold now 560 ms; sim-proven `DOTO-VR.md:157`).

**Guns / dual wield**
- Hand-aimed shots (`hand-shots`), crosshair on the hand/barrel, aim smoothing, per-hand wheel (X/A), real second weapon copy with upgrades, own ammo, fire/reload anims/sounds (`dual-wield`, README). Hip holsters either hand.
- DOTO Voltaic Gun on either/both forearms, per-hand bolts, charged/overcharged shot cue, sword-hand reticle suppression (`docs/VR-v1475-RELEASE.md`).

**Powers**
- D2: hand-aimed Blink (`hand-blink`), crosshair on left hand for all powers, steady power trigger, grip cancels an aimed power (#6, `power-grip-cancel`), Heart held as a weapon + listen, Shadow Walk grab-kill, Possession view move.
- Power gestures (push / yoink / temple / clap) exist in `src/vr_gestures.hpp:173-237` but are **opt-in, off, never headset-tested** (`src/dxgi_probe.cpp:19695`, `docs/PLAYING.md:256`).
- DOTO: Displace aimed by hand + swap fade, Foresight tracked + exit fade, Semblance from choke offer, Void Strike (all sim-proven or built; none headset-tested).

**Gadgets / mines**
- Grenade: fist, open mid-swing to throw with hand velocity (`hand-grenade-throw`); mines/razors placed by hand aim or palm-on-surface (`hand-mine-aim`), 50 cm forgiving placement; DOTO Hook Mine lethal toggle by 0.3 s hold, thrown mines stick (engine placement).

**Stealth**
- Physical lean/peek, door push / crack-to-peek, keyhole peek by holding grip, choke by gripping neck from behind, closer takedown range (`takedown-range`), readable 25 cm takedown prompt, takedown from behind with any swing, carry bodies (above).

**Traversal**
- Mantle/climb are game-owned (accepted by user, `docs/feedback-2026-10-10-headset.md` "mantle ... accepted"); jump by raising both hands; "recenter on scripted moments" eases view during climbs/takedowns (`docs/PLAYING.md:95`, not headset-confirmed).

**UI**
- Native prompts drawn beside the pointing hand; takedown prompts over guard; native objective markers in world; dialogue next to the speaker (v721); wrist watch HUD (back of left wrist; native HUD crop, `docs/VR-CONTROLS-DESIGN.md:300+`); notes/maps/Black Market in left palm with finger scrolling (`note-tablet`); hand-anchored world wheel pointed by hand; menus world-placed ~1 m, regrab to reposition; VR Settings in pause menu (D2 + DOTO); illustrated in-game controls guide (right temple); journal (left temple); Quest glyphs (`vr-glyphs`, DOTO 40 movies patched); hold-Y pause; B = health elixir.

**Cutscenes / authored cameras**
- Cutscene mode 3D or Theater; headset owns view in takedowns/scenes (`authored-head-view`), heading held, re-aim at >=30 deg; action-camera alignment; Bink videos flat; hold A/X to skip; letterbox hidden; boat-ride fix (`src/native_ocean_eval.hpp:2`).

**Comfort**
- Locomotion vignette (`comfort-vignette`, up to 60 %), teleport fades on Displace/Foresight, camera smoothing, turn ramp, snap turn. Body-carry view does not follow head into the body.

**Rendering / perf**
- Native stereo with per-eye TAA history, FSR 3 / FSR 4 / DLSS/DLAA (user-supplied DLL, auto-found in Steam libs), SMAA, SSAA, sharpening, foveated upscaling, lens visibility mask stamp, shadow sharing between eyes, Spacewarp (own frame generation, Off/Auto/Always, body/legs/hands/UI rules), performance mode (alternate eye + rebuild), NVIDIA VRS opt-in (`src/nv_vrs.hpp`, `build/nvidia-vrs`), refresh-rate policy (`src/refresh_rate_policy.hpp`), static shadow cache observe-only prototype (`docs/STATIC-SHADOW-CACHE.md`), layer pose sync, SteamVR safe submit.

**Audio**
- Wwise listener follows the head pose (`src/native_audio_listener.hpp`, research `docs/native-vr-audio.md`). No HRTF (`docs/native-vr-audio.md`: "Neither approach constitutes HRTF support").

**Launcher / settings**
- One-file launcher, setup checks, D2/DOTO game switch, settings mirrored in-game, feature toggles (49 entries in `tools/default_features.json`), diagnostics off by default, skip intro, uninstall proxy, Linux path (`linux/d2vr-linux`).

---

## 2. Gaps: unfinished, deferred, known-limited

| # | Area | Missing / limited | Evidence | User words | Effort / risk |
|---|---|---|---|---|---|
| G1 | Everything DOTO | Billie toolkit, dual Voltaic, Hook Mine, grip cancel, Possession all **never played in a headset** | `docs/VR-v1475-RELEASE.md:3-5`, checklist header | — | S per item (test) / risk: unknown feel |
| G2 | DOTO Foresight | Tagging by pointing and a tether line to the body not done | `docs/VR-v1475-RELEASE.md:20`; design `DOTO-VR.md:78` | — | M / low |
| G3 | DOTO Displace | Marker landing vs hand ray never measured; second marker (Third Eye charm) unverified; no silhouette/arc preview from the design | `DOTO-VR.md:156`, `:77` | — | S-M / low |
| G4 | DOTO Voltaic | Left (engine) gun's projectile not counted in sim; right copy has no bolt-specific look / no charge lights; copy refuses bolts the save lacks | `VR-v1475-RELEASE.md:33-35`; checklist "Known limits" | DOTO v145749: "there is no second crosshair for the second weapon" (fixed) | M / medium (VFX RE) |
| G5 | DOTO Semblance | Disguise dropped 66 ms after take in one run (unexplained); stolen-face-on-wrist and Rat Whispers hand-to-ear from the design not built (no `talisman` in `src/`) | `DOTO-VR.md:157`, `:79`, `:94` | — | M / medium |
| G6 | DOTO misc | Slash scene rate (11-byte steal) open; power wheel dial unverified; hand grenade throw OFF in DOTO (launch point differs); Bank Job vault / A Hole in the World / lifts not checked | `DOTO-VR.md:153`; R2 `docs/doto-research/2026-11-24-R2-powers.md:102,105`; `DOTO-VR.md:106-113` | — | M / medium |
| G7 | D2 Possession | Widened gate built, never seen in a real possession | checklist #12; `DOTO-VR.md:157` | — | S (test) |
| G8 | Power gestures | Built (push/yoink/temple/clap), opt-in, launcher says "new, untested"; phases 2-4 (per-power gestures, gesture select) not done | `src/dxgi_probe.cpp:19695`; `docs/power-gestures-research.md` §6 | — | S to test; M per new gesture / risk: false casts |
| G9 | Weapon wheel | Sometimes selects a different weapon than pointed at (open known issue in 3 releases) | `docs/VR-v1457-RELEASE.md:58`, `VR-v1445:42`, `VR-v1393:18` | v1037: "after a wheel choice I see the default wheel for a second"; v775: "I turn my hand to the right and the indicator stays on the left" | M / medium |
| G10 | Crossbow bolts | Bolt type can be wrong after floor pickup / hand toss; tutorial range aim wrong | `README.md:34-36` | v1381: "I chose the sleep bolt and got the regular one" | M / medium |
| G11 | Spacewarp artifacts | Glass doubles (no depth); legs/UI ghosting was iterated; wrist HUD blinks with inline FG (opt-in only); SteamVR late frames 17-56 % in sessions | `docs/PERFORMANCE.md` "What it cannot fix"; src quotes v1431-v1450 | v1438: "looks awesome"; left: "legs warp a lot when ... running"; v1450 SteamVR: "better but not very stable" | M-L / medium |
| G12 | Performance in heavy areas | ~12 ms resolution-independent floor; 90 Hz not held. Phase 0 measurement and Phase 2 (2A.1 shadow cache active, 2A.3 share passes, 2A.4 stencil mask, 2B.1/2B.2 shared culling / command replay, 2C game-thread) open; depth submission (4.4) not implemented (no `composition_layer_depth` in `src/`); Steam Link lens mask (1.4) | `docs/PERF-PLAN-90HZ-2026-11-19.md:11-41, 90-116, 128-135`; `docs/STATIC-SHADOW-CACHE.md` (observe-only) | v1419: "when a heavy moment starts I still see low fps for some seconds; it should start right away"; 10-10: "about 69-70 FPS rather than near 90" | L / high |
| G13 | Stereo loss in animations at low fps (GitHub #2) | At ~36 fps, climbs/takedowns showed the same frame in both eyes; closed by lowering settings, not by a code fix | issue #2 comments | reporter: "Both eyes seem to play the same screen during animations such as sliding, climbing, takedown" | M / medium (INFERRED: authored-camera path + pacing interplay) |
| G14 | Comfort | Authored-camera head view, action alignment, camera smoothing, vignette, recenter-on-scripted, performance-mode double-vision fix all "not yet confirmed in the headset" | `docs/PLAYING.md:95,201,215,226,242` | — | S (test) |
| G15 | Head in walls | No head-collision / wall fade; physical head translation is research only ("No head query ... is enabled") | `docs/physical-head-translation.md:215-233`; `docs/vr-embodiment-reference-decisions.md` | — | L / high (native query lifetime) |
| G16 | Seated / handedness / accessibility | No seated mode or height offset, no stick-swap / left-handed preset found (grep: no `seated_mode`, `height_offset`, `swap_sticks`, `handedness` setting); crosshair + powers fixed to left hand | grep of `src/`, `launcher/`; `docs/PLAYING.md:121` | — | S-M / low (INFERRED gap) |
| G17 | Gesture-only actions | Sword draw only by shoulder grip, guide/journal by temple; best-practice doc requires button fallback for all gestures | `docs/vr-conversion-best-practices.md` §3 "Every gesture needs a button fallback" | v778/v779 elixirs "hard to reach" -> moved to B | S / low |
| G18 | Haptics coverage | No haptic found for player taking damage, enemy hit landing on a block, power cast, or gun recoil scaled by weapon (INFERRED from pulse sites); best-practice wants priority model | `src/dxgi_probe.cpp` pulse sites; `docs/vr-conversion-best-practices.md` §4 | — | S / low |
| G19 | Two-handed hold | Never a two-handed weapon hold (crossbow/pistol stabilisation) | `src/weapon_draw_place.hpp:284` ("v1362: never a two-handed hold") | — | M / medium |
| G20 | Full body detail | No per-foot ground height on stairs, no toe/heel roll, no hand-tracking fingers | `docs/full-body-research.md:200` | v1437/1438 legs warp (FG) | M-L / medium |
| G21 | Audio | Listener follows head only; no HRTF/spatialiser; listener ownership in every cinematic state not verified | `docs/native-vr-audio.md` (Verified section, "Not verified live") | — | L / high (Wwise) |
| G22 | Cutscene hands | Cutscenes use game-animated arms, controllers do not move hands; contradicts an older user request | `docs/PLAYING.md` "Menus and cutscenes" | v642: "VR cutscenes now have camera but also should preserve avatar body and player hands control" | M / medium |
| G23 | Body carry / pickup reliability | Shoulder carry needed 2-3 tries; bodies only grabbed with grip+trigger on user's save; prompts flicker | src quotes v1144, v1145, v1216, v1246, v1389, v1390 | v1390: "the shoulder carry still needed 2-3 tries"; v1145: "still flickering while I just look at the guard from behind" | M / medium (iterated many times) |
| G24 | GOG version (GitHub #1) | Only the Steam exe SHA is supported; owner "in my plans" | issue #1; `docs/LAUNCHER.md:101` | owner: "for now I want to make the Steam version stable" | M-L (the DOTO port pipeline `tools/doto/` could target another exe) / medium |
| G25 | DOTO config path | Config file name "unconfirmed" in launcher | `docs/LAUNCHER.md:102` | — | S / low |
| G26 | Stale docs | PLAYING.md still says arm geometry hidden and full arm tracking deferred, but `full-body` is default on; README says DOTO "not in a release" though v1472/v1475 ship it | `docs/PLAYING.md:192-196`; `README.md` DOTO section | — | S / none |

---

## 3. Recurring user themes from headset feedback

Counts are from the 185 "user, headset vNNN" quotes in `src/` and `docs/` plus the 10-10 report.

1. **Each hand must be independent and symmetric** (biggest theme, v762-v799): "each hand is its own object" (v793); "the free hand must always pick things up, whatever the other holds" (v796); "the right hand must hold every weapon exactly as the left does" (v762); "a bottle still blocks the other hand" (v793/v795/v796). DOTO repeat: "I cannot switch the crossbow to my right arm" (v145743).
2. **Weapons/items must behave like the real thing, at once, with upgrades**: "the heart should behave as a normal weapon" (v1045); "the player always sees weapons with the latest upgrades" (v1092); "a weapon picked up or chosen from the wheel must be in the hand at once" (v780); "a pistol picked up into the hand appears with a noticeable delay" (v815); copy "does not use the same sound, animations" (v1116).
3. **Hands/weapons lag, jitter, ghost while moving**: "the weapon lags behind when I start running" (v775); "the doubling of the guns while I move forward" (v815); "the arm freezes in the air for a second while walking" (v1420-v1422); FG ghosting on weapons, carried objects, corpses, legs, UI (v1421-v1438).
4. **Bodies: grabbing and carrying is fiddly**: "I cannot put any bodies on my shoulders" (v1040); "bodies often get stuck in the player's body" (v1144); "grip can't pick them up, grip + trigger can" (v1145); carry needs "2-3 tries" (v1390); "carrying a corpse, my hands are disabled" (v1037).
5. **Prompts / UI must be native, stable, in the world**: invented circles/ink unwanted, separate VR settings UI unwanted (10-10 report); "the dialogue must be drawn next to the person, exactly like the Talk prompt" (v721); prompt "flickering a bit" (v1145, v1216, v1246); "the HUD updates less often than the game" (v686).
6. **Natural grips/fingers (Boneworks/Bonelab bar)**: "fingers must react to every object, like Boneworks/Bonelab" (2026-10-14); "grip is unnatural on books" (11-05); "a thin spoon is held in a full fist" (v1071); thumb "bent into an unrealistic position" (11-05).
7. **Forgiving, simple controls**: "block should be very forgiving" (v447); "just only one grip button on each hand" (10-13); "make placing a mine a bit easier, more forgiving" (v778); "the stealth attack is hard to do" (v720); "the elixirs are still hard to reach; put the health elixir on B" (v779).
8. **Performance and frame pacing**: ~69-70 fps not 90 (10-10); FSR "softens/washes the image without useful FPS improvement" (10-10); "it should start right away" (v1419).
9. **Praise / accepted**: locomotion, turning, mantle, VD away/back accepted (10-10 report); "spacewarp looks very good now" (v1431), "works quite well" (v1437), "looks awesome" (v1438); v637 "very good overall"; parry confirmed v1127; combat/animation and FSR4 Quality look liked. Diagnostics must be optional (v1045).

---

## 4. Top 15 candidate improvements (internal evidence only; ranked impact x feasibility)

1. **Headset pass over the DOTO + v1475 checklist, then fix what fails** (G1, G7, G14). Every DOTO feature and several D2 comfort features are sim-only; one structured session converts "built" into known-good and is the cheapest way to raise real VR quality. Effort S.
2. **Fix weapon-wheel mis-selection** (G9). Open known issue across three releases and touches every weapon swap; user complained repeatedly about wheel feel. Effort M.
3. **Fix crossbow bolt identity after pickup/toss** (G10). Listed in README as the remaining tutorial roughness; user reported wrong bolt (v1381). Effort M.
4. **Haptics pass: player-hit, blocked-hit, power cast, per-weapon recoil, with a priority model** (G18). Cheap, uses existing `GameXr::pulse`, adds presence to combat the user already likes. INFERRED gap. Effort S.
5. **Body grab/carry reliability: grip alone on bodies first try, one-squeeze shoulder carry, prompt stability** (G23). Most repeated stealth complaint (v1040-v1390). Effort M.
6. **Ship power gestures after a headset test (Blink yoink + Dark Vision temple first)** (G8). Code exists, opt-in, phased plan written; flips the "magic is a button" gap. Effort S to test, M to tune.
7. **DOTO Foresight tagging by pointing + tether line** (G2). Explicit "Not done" in the release notes; pointing-to-tag is the VR-native core of Foresight. Effort M.
8. **Measure and correct Displace marker vs hand ray; add the designed silhouette/arc preview** (G3). Billie's main traversal tool; accuracy unmeasured. Effort S-M.
9. **Perf Phase 0 measurement run, then the cheapest Phase 2 items (2A.2 shadow cvars, 2A.4 stencil mask, activate 2A.1 shadow cache)** (G12). Heavy areas cannot hold 90 Hz; this is the root of #2 and of FG artefact complaints. Effort M-L, high value.
10. **Depth submission (`XR_KHR_composition_layer_depth`) as an opt-in** (G12, plan 4.4). Better runtime reprojection when frames are missed, especially SteamVR/Steam Link. Effort M; verify in headset.
11. **Stereo guard for authored animations at low fps** (G13, issue #2). Detect paired-frame repeats during climb/takedown and fall back (e.g. force spacewarp/performance mode or keep eye pairing) instead of showing a mono pair. INFERRED design. Effort M.
12. **Comfort/accessibility options: seated height offset, left-handed preset (powers/crosshair to right, stick swap), button fallbacks for shoulder draw and temple menus** (G16, G17). Small, launcher-level, widens audience. Effort S-M.
13. **Cutscene option: keep tracked hands/body in 3D cutscenes** (G22). Direct user request (v642) that is currently the opposite default; a knob already exists (`build/cutscene-avatar`) - INFERRED it could be surfaced as a setting. Effort S.
14. **Two-handed stabilisation for crossbow/pistol** (G19). Standard VR-native gun feel; explicitly excluded so far. Effort M, medium risk to the attachment code.
15. **DOTO VR-native extras from the design table: Semblance face on wrist, Rat Whispers hand-to-ear, Hyperbaric grenade hand throw in DOTO** (G5, G6). Fill out Billie's kit with gestures the D2 side already has analogues for (Heart listen, wrist HUD, grenade throw). Effort M.

Not ranked (large/high risk, keep in view): head-in-wall fade (G15), HRTF audio (G21), per-foot IK on stairs (G20), GOG port target (G24), doc refresh (G26, trivial).
