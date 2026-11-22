# Held note in the hand (DOTO, headset build v145734): why it is not like D2

Static comparison (pefile+capstone, D2 vs DOTO exe), generated tree `build/doto-src/src` (14:40 tree), user's last session log.

## Verdict
Everything up to the *page* is fine in DOTO. What fails is the **hand art**: the D2 held-note path needs a skinned note-hand mesh
bound to the live arm, and DOTO's player is Billie, for whom no mesh file exists and whom the code cannot accept.
Without it D2's whole "tablet in the hand with the real hands around it" route never switches on; the mod shows only the
fallback page.

## What was checked and is NOT the cause (evidence)
* **Logic mask**: `ARK_GAME_LOGIC_STATE_*` string order is identical in both exes for ids 0..15 (NONE, INGAME, PAUSE, NOTIFICATION,
  READNOTE, CINEMATIC, MAINMENU, JOURNAL, POWERWHEEL, CONVERSATION, BLACKMARKET, RESULTS, TUTORIAL, GAMEOVER, CHARACTER_CHOICE,
  MAP_TRANSITION); only id 16 differs (D2 DEMO, DOTO VIDEO = 65536). READNOTE = id 4 = mask 16, JOURNAL 128, wheel 256 in both,
  so `native_note_reader_logic` (src/native_world_ui.hpp:14: 16 or 128|16) and `world_ui_logic` are correct for DOTO.
  The user's `linux-proxy.log` shows `logic_mask 2 -> 16 -> 2` at 15:11:00, 15:12:23, 15:12:30 (three note/map reads) and
  `2 -> 128` at 15:10:39 (journal). The probe samples about once a second (every wheel open also shows as exactly 1.0 s), so the
  1 s duration of mask 16 says nothing about how long the note was open.
* **Movie name**: `ui/notes.iggy` is in DOTO's `base/game1*.index` (also `ui/journal.iggy`, `lib_journal_notes.iggy`), same as D2.
* **World-UI owner / draw scope** (`native_world_ui.hpp`, generated lines 46/83/108/128-129): root 0x2274270->0x23352c0, draw hook
  0x2dd580->0x2e27b0, set-target 0x2e3840->0x2e89c0, call site 0x2dd632->0x2e2862. The 25/26-byte hook prologues at those DOTO RVAs
  match the pinned byte arrays, and 0x2e2862-5 is a `call`.
* **Note kind** (`native_note_kind.hpp`, generated): all six FNV regions re-hashed against the DOTO exe -> all six match
  (0x7f5da0/594, 0x7f6000/471, 0x29520/149, 0x29090/60, 0x2a640/23, 0x2c980/8). Return addresses 0x7f6175 / 0x7f5f94 are both the
  return of `call 0x290d0`, which is exactly the function `native_wrist_hud.hpp:220` hooks in the generated tree (D2 0x28a90).
  The Iggy helper functions are instruction-identical to D2 (`[rcx+0x1780]` root table, 40-byte records at +0x18/+0x20).
  `configure()` should therefore verify; this could not be confirmed from the live status (below).
* The user's `xr-live-status.json` has no `note_tablet` block (that is in the larger status dump, dxgi_probe.cpp:12886), so
  `note_tablet.kind.verified/accepted_shows` and `note_tracking_reason` were not available from this session.

## Cause 1 (certain, ranked first): no note-hand art for Billie
* `src/dxgi_probe.cpp:19126` (generated; D2 at the same code) loads only `note_hand_{corvo,emily}_{left,right}.bin`
  (`for character<2`); the user's `game-art` has exactly those four D2 files (dated Nov 18). They come from
  `launcher/NoteHandMesh.cs:31-48` (`ExportNoteHandMeshes`, Corvo/Emily only).
* `src/note_hand_mesh.hpp:25` rejects `character>1`; `:37` requires resource `models/characters/player/<corvo|emily>/<left_arm|right_arm>.md6`;
  `bind()` (`:93-107`) additionally requires `models/characters/player/<character>/player_<leftarm|rightarm>_<character>.md6mesh` as the
  primary mesh name, and `matches()` (`:46-56`) requires equal surface count / kind / vertex / triangle counts with the live arm.
  Billie's arms are `models/characters/dlc01/player/billie/player_{left,right}arm_billie.md6` (glove 3255 + upper 568 v left,
  3281 + 395 right; `tools/doto/reviews/billie-avatar.md` facts table and "Not covered by the patch"). So `bind()` returns `{}` for every
  mesh on every attempt (`dxgi_probe.cpp:15423-15430`, retried each second).
* Consequence chain: `game_xr->note_models()` is never called -> `note_posers_`/`note_binding_` empty, `note_models_active_` false
  (`game_xr.hpp:4114`) -> `note_composition_ready()` false (`:822`) -> `note_tracking_reason="native_models_unavailable"`,
  `note_last_fallback="hand_art_unavailable"` (`dxgi_probe.cpp:15450`, `:15460`) -> `native_note_targets()` (`:9598`) returns `{}` (needs
  `binding==game_xr->note_model_owner()`), so the game's own arms are never posed onto the tablet and `NoteEyeArt` (game_xr.hpp:4373) never
  draws the page into the eyes. The plain world-UI quad is all that remains: `world_ui_pose()` (`game_xr.hpp:938`) skips the tablet branch (`note_posers_[0]` null) and
  uses `world_ui_hand_pose(...,.12)` (`:944-946`): a flat 0.45 m quad at the left grip (the "controller attachment" fallback), without
  the hands holding it, finger poses, occlusion or scroll-by-touch. This is "not in my hand like D2".

## Fix for cause 1 (asset + small source change, no offsets.json row can do this)
1. Exporter (`launcher/NoteHandMesh.cs`): add character `"billie"` (index 2) reading
   `generated/basemodel/models/characters/dlc01/player/billie/player_{leftarm,rightarm}_billie.bmd6model` from the DOTO container
   (`ArtImport.Containers` with the `models/characters/` filter already covers it, but must be pointed at the DOTO install; the skeleton stays
   `models/characters/player/emily/player_{limb}_emily.edgeskl`, 69 joints, byte-identical per billie-avatar.md), resource string
   `models/characters/dlc01/player/billie/player_{leftarm,rightarm}_billie.md6`, file `note_hand_billie_{left,right}.bin`, format field
   `character=2` (version stays 2).
2. `src/note_hand_mesh.hpp`: `read()` accept `character<=2` and compare against a small table
   `{corvo: "models/characters/player/corvo/", emily: "models/characters/player/emily/", billie: "models/characters/dlc01/player/billie/"}` with names
   `left_arm.md6`/`right_arm.md6` for the first two and `player_leftarm_billie.md6`/`player_rightarm_billie.md6` for Billie; `bind()` primary name
   `player_<leftarm|rightarm>_<character>.md6mesh` under the same directory. Do this behind `game_traits` (src/game_profile.hpp, a new
   `note_hand_characters`/directory trait) so the D2 build is byte-identical.
3. `dxgi_probe.cpp`: `note_hand_meshes` size 4 -> 6, both loops (`:19126`, `:15422`, `:15444`) `character<3`; the three arrays are indexed `character*2+side`.
4. UNVERIFIED (read-only live check by a helper): the string at `arm.model+0x1bb0` (= `descriptor+8`, the "logical resource") for Billie's arms. billie-avatar.md says
   her arm md6 files "inherit Emily's left_arm.md6/right_arm.md6", so the engine may report Emily's logical name or Billie's own. Read it with
   `tools/entities.py`-style ReadProcessMemory (`avatar_mesh_token` reads `arm.model+0x1fd8` descriptor, `+8` source, `+0x68` geometry, primary at descriptor+0x?? ->
   name `+8`), then set `resource`/primary strings in step 2 to whatever it prints. Also check the primary name (`player_leftarm_billie.md6mesh` presumably).
   The void right arm (`player_rightarm_void_billie`) has a different mesh (7031+401 v) and would need its own bin; `bind()` should simply refuse it and the fallback shows.
5. Rebuild art (`PoseRender.ExportNoteHandMeshes` runs at launcher start; `NoteHandMeshesMissing` (NoteHandMesh.cs:11-20) must also check the Billie files).

## Cause 2 (probable, cheap, ranked second): the hand-held *quad* fallback is the only thing shown, and it is too bare
Until cause 1 is done, make the fallback closer to D2: `game_xr.hpp:942-946` already holds the quad at the left grip + 0.12 m when the tablet pose is not
available, but without hands. Optional src-only mitigation: none needed for correctness; do not extend, the real fix is cause 1.

## Cause 3 (low): kind verification
If the DOTO `note_tablet.kind.verified` is false in a sim, `generation` stays 0 and `game_xr->enable_note_tablet(false)` (dxgi_probe.cpp:15449);
the hand route is dead even once art exists. All pins re-verified above, so expect true; nothing to change.
Residual risk: `native_note_kind.hpp:50` `argc` rule (`caller==0x7f6175?0:5`) assumes ShowMap/ShowNote still pass 0/5 arguments; both functions are
the same instruction stream (pin-audit.md #4), only `[rdx+0xf0]/[rdx+0xe4]` fields moved.

## Headset-free check
1. Statically: after the fix, `tools/doto/render_pass_check.py`-style script is not needed; run the existing `note_hand_mesh_check` with a Billie fixture
   (the check's Emily case plus a Billie case, refused for a different character).
2. Sim run (coordinator, game launched by the coordinator, PC free): load a level with a note (tutorial has one in the first area), open it. Read
   `xr-live-status`/the full status: `note_tablet.kind.verified` true, `accepted_shows >= 1`, `note_tracking_reason` = `native_models_ready`,
   `note_ready_frames > 0` (`note_last_fallback` empty), `game_xr.note_eye_draws > 0`. Currently expected: kind verified true but `native_models_unavailable` / `hand_art_unavailable`.
3. Read-only memory check of the arm resource strings (step 4 above) is the one fact needed before coding.

## Done (cause 1; static only)
* Launcher: `NoteHandMesh.cs` exports per game profile; DOTO = `billie` (id 2, folder `models/characters/dlc01/player/billie/`, md6
  `player_{left,right}arm_billie.md6`, Emily's 69-joint skeleton), read from DOTO's game1..game3 containers (`GameProfile.ResourceContainers`; DOTO has
  no game4) into the DOTO home's `artifacts\game-art` from `RefreshDotoChecks` (start, Re-check, Launch VR) while missing, and in `--check-package --game doto`.
  Exported from the user's install: left 3255+568 v / 5827+861 t, right 3281+395 v / 5827+610 t, inverse bind rows byte-identical to Emily's files; the D2
  export is byte-identical to the installed `note_hand_{corvo,emily}_*.bin`.
* src: `game_traits::note_hand_first_character/note_hand_character_end` (D2 0..2, DOTO 2..3) and the id table `note_hand::identities`; `dxgi_probe.cpp`
  loads/binds only this game's ids (array 2*end). Decision for step 4 (no live read): her arm defs' `renderModelInfo.model` is her own md6 (D2's pinned
  string is exactly Emily's def `model`, so her own name is expected at descriptor+8); since her md6 inherits Emily's, `bind()` also accepts Emily's
  `left_arm.md6`/`right_arm.md6` for id 2 only. The primary mesh must be `.../billie/player_<limb>_billie.md6mesh` (the `lod 0 { clear mesh ... }` line,
  a full path like D2's) and the surface kinds/counts and inverse rows must match, so either logical name still pins her own mesh. The void right arm
  (`m_voidRightArmMd6` in her `body.cpntplayerbody`, swapped by the game) has another primary mesh and surfaces: refused, quad fallback.
* Checks: `note_hand_mesh_check` (D2 and `/DD2VR_GAME_DOTO`, the latter added to `tools/build-msvc.cmd`) reads the exported files and tests the Billie identity.
