# Authored Peeking24 scene presentation

The current repository has no `xr_game_preview.hpp`. The presentation code is `src/game_xr.hpp`; native render qualification and stereo lifecycle are in `src/dxgi_probe.cpp`.

## Actual keyhole result supersedes the proposed32 route

Root entered an ordinary native keyhole with heldX on v96. `artifacts/native-keyhole-active-camera-v96-20261009.json` contains four consistent mode3/target3 samples. The corresponding archive `20261009T042941.424921Z-pid44016-arm-v96` has40 native camera trace rows within100ms of those samples, all exact logic2, mode3/target3 and valid view. Four matched rendered-head pairs (presents510,516,522,528) submit authored tracked projection layers, retain the same native authored source origin for both eyes, and measure0.0640000043 native-unit eye separation. XR orientation is identity during that correlated interval, so interactive physical head rotation is not established. Run totals are1015 native pairs,699 authored layers,2032 matched head writes and2032 source restores, zero callback failure/faults. The earlier captured mode3 pair is distinct across eyes, but its present120 cannot be attributed to the later read-only sample time alone. Proof: `artifacts/native-keyhole-presentation-proof-v96-20261009.json`.

The observed keyhole already uses the existing logic2 immersive path. **Do not register or activate the unused logic32+Peeking helper for this result.** It remains an isolated guarded proposal, not a required fix. Generic Cinematic32 presentation remains a separate unresolved case; no typed32+Peeking capture has established that route. The call-site map below describes a conditional future integration, not work to apply now. Headset aperture geometry, physical head motion and comfort remain user acceptance work.

`game_logic_mask` represents active manager state IDs as bits. The local MIT primary headtracking source names state5 Cinematic, so exact mask32 is Cinematic, not general gameplay. The exact pinned player's FSM state24 is separately RTTI-confirmed `arkPlayerStatePeeking` (table1c7c4a8) with declaration `arkDeclPlayerStatePeeking` (1c7c378), declaration index24. Native Interact dispatch871020 selects the secondary usage state, including24. This distinction matters: Cinematic alone does not prove a keyhole camera.

## Removed experimental guard (research record)

The unused experimental `native_authored_scene.hpp` and its unregistered tests were removed after the actual logic2 keyhole capture. The proposed logic32+Peeking path has no authoritative observed use. Its historical design required the primary game/player, started typed body/FSM ownership and fixed component membership using the established carry guard; exact pool24 slot, state/declaration types and indices; active byte20==1; the started native camera/physics identities, body/controller/physics ownership, valid native view, and authored camera state. Before/after checks,50ms expiry and nonzero matching XR epochs were proposed requirements, not a currently installed API. No runtime presentation was changed.

Before removal, isolated MSVC/W4/WX tests passed for the experimental guards. A read-only installed-PE check confirmed exact executable hash, RTTI COL records, state table activation853d40/entry8426b0/update842d10 and prior owned pool24 declaration index. These exact native type facts remain useful research evidence; they do not establish a logic32 presentation route.

## Minimal integration map (root owns edits)

Keep `gameplay=logic==2` for native gameplay, arms, turning, aim and physical-grab eligibility. Add distinct `scene_render=native_scene_render(logic, verified_peeking)` at render-only boundaries. Acquire the proof from current `NativePlayerCamera` and current XR epochs; fixed-revalidate before each use. Invalidate proof on leaving32, stale tracking, owner change or reference reset.

1. `dxgi_probe.cpp` `sample_stereo`: the current `if(xr_requested && game_logic_mask()!=2)` restores stereo and invalidates eye sources. Use the separate render eligibility here so a proved32 scene retains native stereo.
2. `observe_eye_setup` and `observe_eye_begin`: their exact2 checks currently discard32 eyes. Use the same fresh typed proof plus existing native view-header/allocation, exact player/camera/generation and pipeline frame/thread checks. Keep all existing ownership and N→N+1 eye matching checks.
3. `prepare_head_view`: replace only its early render rejection with scene eligibility. Continue preserving the authored native camera origin/leveled yaw through `BodyAnchor`, optical projection and distinct pupil offsets. Crucially, wrap the later turning, physical-grab, left-aim and arm/attachment publication sections in raw `gameplay`; permitting32 rendering must not enable their side effects. Clear their publications for the authored32 branch, then continue only the existing render-view path. No new physics/head query or engine camera update is necessary.
4. Present block near1686: acquire owned camera also for candidate32, derive the render proof separately, retain raw gameplay for all native commands and eligibility. Pass raw gameplay plus a new explicit authored-scene-render argument to `GameXr::present`. Merely passing `gameplay=true` would incorrectly grant menu/input behavior.
5. `GameXr::present`: append a default-false `authored_scene_render` argument; define `scene_render=gameplay || authored_scene_render`. Require `authored_camera` for the latter. Use scene_render for projection versus quad, native menu pointer/trigger routing, menu reset/counters and the complete tracked eye-pair requirement. Keep raw gameplay for settings/calibration semantics; `settings_available` remains exact pause4. Unknown/incomplete authored scene must not submit a duplicate/mono texture as immersive stereo. Disable the native menu pointer for this authored scene, preserving raw native button actions.

WARP acceptance should exercise `gameplay=false, authored_camera=true, authored_scene_render=true` with distinct eyes: projection layer, correct per-eye pixels, no native menu pointer, no consumedY, tracked pair mandatory; incomplete pair submits no scene. Return to pause4 must restore a normal menu quad and settings eligibility. Pure policy tests reject all other masks including mixed2|32, loading, pause and generic Cinematic without Peeking proof. Actual keyhole acceptance needs owned active24+authored view, valid paired native source allocations, both rendered eye poses/FOVs, distinct eyes, zero callback faults and clean restoration. Existing v82 mantle evidence covers the logic2 authored route, not this32 route.

This keeps the immersive objective explicit. The current flat square is caused by classification: exact2 render gates restore native stereo under32 and `GameXr::present(!gameplay)` selects a quad. This audit does not reclassify all cinematics or substitute a comfort theater for proved authored scenes.

## Cinematic32 capture, 2026-10-25

This is the spike for G2 of `docs/superpowers/specs/2026-10-25-camera-recenter-vr-cutscenes-design.md` (camera plan Task 4).
The full ledger is `.superpowers/sdd/2026-10-25-camera-recenter-vr-cutscenes/g2-spike.md`. **Verdict: go.**

- **Probe.** The knob `build/cinematic-probe` writes `artifacts/cinematic-probe.csv` (at most 2048 rows), plus a
  `cinematic_probe` block in `xr-live-status.json`. The plain knob only reads. It is off without the knob (see
  `tests/cinematic_probe_check.py`). The knob text can name two experiments, and they run only on the simulated backend:
  - `stereo`: `sample_stereo` skips its restore for exact 32;
  - `offset`: a +5 cm test write goes through the gameplay head-view window (`view+0x9c`, `head_saved`, `head_wrote`), and
    `restore_head_view` puts it back.

  Neither experiment is a new kind of engine write. Scenario: `tools/linux/scenarios/cutscene.py` (it runs `loadgame 22`
  after Continue). Report: `tools/linux/cinematic_probe_report.py`.
- **Scene.** The user's SLOT22 (`campaign/void/void_01/void_01_p`, the chapter 2 start) plays an exact-32 cinematic for 10 s,
  about 13 s after the load command.
- **Camera.** The game renders the cinematic from the **player camera**. It is the same player, camera pointer and generation
  as in gameplay, in camera mode 10 (`target_mode` turns to 10 a few frames before `mode`). The render view equals that camera's
  own view to 0.000 m on every frame. No other camera entity is involved.
- **Hooks.** On the shipping path (stereo restored) the hooks see one Setup and one Begin per frame, and the Setup matches the
  one native view. With native stereo kept on, the engine prepares two native views from the next frame. The hooks then see
  4 Setups per frame, just as in gameplay (2 centre views plus the 2 eye views), with 2 matched eyes 0.064 m apart, and
  2 Begins. The Begins consume the previous frame's Setups (N→N+1) on every frame but the entry frame. A Setup at `frames()==F`
  shows at Present F+2, as in gameplay.
- **The lever.** A +5 cm test offset on 4 Setups moved the presented image by 16 px horizontally, on exactly those frames
  (Present F+2). That was 2 clean bursts each in mono and in stereo. The frame after the write ended matched the frame before
  it. Probe writes equal probe restores (12/12, 24/24), gameplay `head_pose_writes == head_source_restores`, and there were
  0 recoverable faults.
- **Paired binds.** The legacy bind-count baseline does not hold in gameplay either (2 binds per frame in both). The sim and the
  shipping configuration certify pairs with native eye capture (`native_eye_pair`), so it is not needed.

So the "minimal integration map" above applies to the generic Cinematic32 case without a Peeking proof. The proof is the exact
mask 32 plus a complete native eye pair, with the player camera identity unchanged. Steps 1–5 carry over. The file:line list for
the camera plan's Task 5 is in the ledger.

Not established: other cinematics (Meagan or Wale scenes, takedowns), pre-rendered video, skip, and pause during a cinematic.
Headset comfort is Tier C.
