# Cutscene polish (Track T item C, v612)

Builds on the v610 cutscene code (cutscene_policy.hpp, cutscene_view.hpp, GameXr SceneHold). Ideas from the external GPL-3.0 mod's documented
facts; no code shared. Knobs (absent = on): `build/no-bink-theater`, `build/no-cutscene-skip-hold`, `build/no-letterbox-hide`.
Status JSON `cutscene.bink`, `cutscene.skip_hold`, `cutscene.letterbox_hidden`, `last_fallback:"bink_video"`.

1. Bink videos: `src/bink_video.hpp` (pure tracker) + `src/native_bink_hooks.hpp`: MinHook on bink2w64.dll exports BinkOpen,
   BinkOpenWithOptions, BinkClose, BinkDoFrame, BinkNextFrame (all five exist in our install's bink2w64.dll, checked with a PE export
   read). Installed only when the module is loaded, polled about once a second until then, a failed hook leaves everything as before.
   Playing = an open handle with a frame in the last 500 ms. An exact-32 frame while a video plays calls
   `CutscenePolicy::reject(bink_video)`: Theater at once, no pending black, sticky until the cinematic bit leaves.
2. Skip: `src/cutscene_skip.hpp`. A or X held 0.6 s (the press must begin inside an exact-32 cutscene or a playing video) sends a held X
   (the game's Interact, whose hold is the skip ring) and masks physical A/X for as long as it is down; if the scene ends under the
   press, A/X stay masked until released. Short presses and non-cutscene presses are untouched. Mixed masks (dialog choices) and
   pause (4) are not a skip context.
3. Letterbox: `src/letterbox_gate.hpp` + `skip_letterbox_draw` in the DrawIndexed hook: during immersive scene frames (gates open) a
   39-index, 1-instance, triangle-list draw with nothing at PS texture slot 0 is skipped, at most two per frame. NOT verified by the
   DXBC hash 7ACC1A9A27C4D9E2 (no CreateVertexShader hook exists here; adding one just for this was judged not cheap), so the match is
   structural; confirm with `letterbox_hidden` ~2 per immersive frame in the sim and that no scene geometry disappears.
4. Mixed masks: `CutscenePolicy::immersive` holds the immersive state through a mixed mask (cinematic bit plus another, not pause) for up
   to 0.5 s after the last immersive frame (`holding()` -> SceneHold::replay shows the cutscene's last layer, no Theater flash). Past
   0.5 s, or with the pause bit, the old rule applies. Back on exact 32 the pair has the entry-grace number of frames (30) to re-form
   before incomplete_pair, instead of 2.
