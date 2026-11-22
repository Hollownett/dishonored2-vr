# DOTO tutorial screens render blank (blue/black) in VR: review

Read-only investigation 2026-11-22 against DOTO v145730 (user report: "in-game tutorial screens are just blue/black screens"). No source, rule or build changed.

## What a DOTO tutorial screen is

* Logic mask = OR of `1 << state id`. The state ids are the order of the `ARK_GAME_LOGIC_STATE_*` strings in the exe (identical in both exes except D2's `DEMO` (16) is DOTO's `VIDEO` (16)):
  NONE 0, INGAME 1 (mask 2), PAUSE 2 (4), NOTIFICATION 3 (8), READNOTE 4 (16), CINEMATIC 5 (32), MAINMENU 6 (64), JOURNAL 7 (128), POWERWHEEL 8 (256), CONVERSATION 9 (512), BLACKMARKET 10 (1024), RESULTS 11 (2048), **TUTORIAL 12 (4096)**, GAMEOVER 13 (8192), CHARACTER_CHOICE 14 (16384), MAP_TRANSITION 15 (32768, loading), VIDEO 16 (65536).
* The user's session log (`DishonoredVR-DOTO\artifacts\linux-proxy.log`) shows `2 -> 4096 -> 2` seven times, each 1 s (the user dismissing the card): those are the tutorial screens. 256 = wheel, 16 = notes, 128 = journal, 4 = pause, 8192 = game over (death, then 0 and a reload). Mask 65536 (the new Video state) never occurs, so no Bink video state was entered in this session.
* `state_tutorial.gamelogicstatetutorial.decl` (DOTO resources): `m_iggyInfo = "ui/tutorial.iggyfile"` (the movie `ui/tutorials.iggy`), `m_screenFX = models/effects/gui/pause.fx` (the dim/blue pause backdrop). The movie has `TutorialsScreen`, `SingleTutorialScreen`, `TutorialScreenVideoElement` (`PlayTutorialVideo`, `_renderTexture`, `ScreenTexture`): the card holds title, text and an image or a Bink clip (`base\video\dlc01\*_tuto_*.bk2`, e.g. 06_tuto_combat, 07_tuto_stealth, 11..18_tuto_video_*) rendered into an Iggy texture.
* The mod gives mask 4096 no special treatment (no `4096` in `src`; `world_ui_logic(4096) == none`, `render_world_scene()` false), so it is a flat menu-style frame: the game's frame with pause.fx and the Iggy draw of `tutorials.iggy` on it.

## Cause 1 (certain, from code): the HUD-art binding eats the tutorial movie's children outside gameplay

`NativeHudChildDraw::hook` (src\native_hud_child_draw.hpp) runs for every Iggy draw. `tutorial_child()` (line 453) claims **every nesting-2 child of `ui/tutorials.iggy`** as a `NativeHudChildKind::tutorial` scope as soon as `active_` and `tutorial_epoch` are set (no logic gate; `tutorial_epoch` is set at dxgi_probe.cpp:10171 every pose update). It was written for the HUD's small tutorial/information messages in gameplay (v710/v814). Then:

1. `start(scope)` = `begin_native_hud_art` (dxgi_probe.cpp:8718) returns false at once: `game_logic_mask() != 2`. So `capture` is false.
2. native_hud_child_draw.hpp:675-677 (v814 rule): `suppress = ... (scope.kind == tutorial) && !capture && start` is true, so `original_` is never called: **every child of the tutorial card (title, text, image, video element, buttons) is skipped in the flat frame**. Only pause.fx (blue/black dim) remains.
3. Even if it were captured, the art is a head-locked quad slot 11 shown only in gameplay (`native_art_source`), so there is no VR copy of a modal card either.

Why DOTO only: D2 used the same code, but D2's tutorial hints come through the HUD `_tutorialMessage`; the D2 tutorial state is essentially never entered. DOTO's `ui/tutorials.iggy` is a real modal card (state 4096), and the v145722 activation of the native HUD art binding in DOTO (it was inactive before) turned the suppress rule on for it. Before v145722 the cards would have drawn flat.

The same predicate (`tutorial_movie = is_tutorial_movie`) also (a) adds `tutorials.iggy` to the root/stage fit (`window && nesting_==1`, line 615) and (b) applies the HUD shrink `hud_scale` to it (line 619). Both are wrong for a modal card, harmless once nothing is suppressed, but the gate below removes them too.

## Fix (src change, not an offsets.json rule: this is generic shared code, not a D2 address)

Gate the predicate to gameplay, `src\dxgi_probe.cpp`:

* add next to `is_tutorial_movie` (line 5787-5796):
  `bool is_gameplay_tutorial_movie(void* iggy) noexcept {return game_logic_mask()==2 && is_tutorial_movie(iggy);}`
  (`game_logic_mask()` is a cached read, used from hooks already; declared at line 973.)
* line 18900: `d2vr::NativeHudChildDraw::tutorial_movie=&is_gameplay_tutorial_movie;`
  (line 8737's diagnostic keeps `is_tutorial_movie`.)

Effect: under logic 4096 `tutorial_child` returns none, nothing is suppressed, the card draws flat into the menu frame like the pause menu (which works). In gameplay (logic 2) the HUD message behaviour is unchanged. If the generic generator needs it (D2 build is unaffected: the D2 state is never 4096), apply it in `src` itself so port.py regenerates it; no offsets.json entry is needed. (If it must be a port rule: `{"file":"src/dxgi_probe.cpp","contains":"tutorial_movie=&is_tutorial_movie;","from":"tutorial_movie=&is_tutorial_movie;","to":"tutorial_movie=&is_gameplay_tutorial_movie;","why":"...","source":"review/tutorial-screens"}` plus the new function; the `src` edit is preferred.)

## Other candidates, ranked below

2. Bink clips inside the card (`PlayTutorialVideo` -> Iggy texture): after the fix the image/text show; whether the clip's texture frame is visible in the flat capture is unverified. `NativeBinkHooks` only hooks bink2w64.dll **exports by name** (DOTO ships bink2w64.dll, same exports, no address pins) and only feeds the cutscene theater policy (exact mask 32) and the A/X skip-hold; it never draws or blocks video, so it cannot blank this card. The pins audit (tools\doto\reviews\pin-audit.md) lists nothing for Bink or tutorials. Confidence the Bink path is not the cause: high.
3. Glyph edit of `ui/tutorials.iggy` (and `lib_tutorials.iggy`, `video.iggy`): both are in `controller_glyph_atlas_doto_edits.hpp` (rows at lines 49, 60, 61), pinned by size + SHA-256 (tutorials 324117 / `2c36d01d`, video 167379 / `953a040e`), same 12-byte `getButtonImage` retarget as the 41 other movies; menu_patches.py has no special tutorial handling. A wrong patch would show a missing glyph, not suppress the whole card. Not the cause; not verified byte-for-byte live.
4. The mask path itself (4096 treated as a normal flat frame) is correct: the pause menu (mask 4) uses the same path and renders in DOTO.

## Verify in the headset-free sim

`tools\doto\doto_sim.py` (new-game scenario, mission 1; the first tutorial popups appear early after the opening). The run keeps `artifacts` of the home. Checks:
* Before the fix: `linux-proxy.log` shows `logic_mask 2 -> 4096`; the shot taken during 4096 is blue/black; the status json's HUD-child counters (`suppressed`, `identified`) rise while the logic is 4096 (counters of `NativeHudChildDraw`, dxgi_probe.cpp around line 13072-13180) and `native-message-slots.jsonl`/`native-message-shows.jsonl` stay empty (begin returns false before the slot assignment).
* After the fix: the shot in 4096 shows the card text/image over the dim; `suppressed` does not increase during 4096; gameplay HUD messages (logic 2) still produce `native-message-shows` rows.
* A read-only live check on a running game can confirm the mask: `tools\entities.py`/the proxy log `logic_mask ... -> 4096`.
* Not established by the sim: headset legibility or comfort of the card (flat panel in VR), and whether a Bink clip texture plays inside it.
