# Billie Lurk as a third avatar (full body, torso fit, body material route, arms)

Read-only study of `D:\Steam\steamapps\common\Dishonored_DeathOfTheOutsider\base` (`game1..3_patch.index` + `.resources`). Nothing was
written to the game folder or to `src/`/`tools/`. Decoded data and helper scripts live only in
`C:\Users\fosnn\AppData\Local\Temp\claude\8711ca7455a4b3d4\01bb84bf-e23e-4896-a5fd-c4cbcb3db1ba\scratchpad\billie\`
(`dx.py` container reader on top of `tools/extract_arm_assets.py`, `chest.py`/`chest2.py` measurement on top of
`tools/build_player_asset_pack.py` + `tools/analyse_arm_mesh.py`, `work/` = patched copy of `src/` and `tests/` with two new checks).
Proposed patch: `artifacts/doto-lab/review/billie-avatar.patch` (applies with `git apply`, touches only `src/native_torso_fit.hpp` and
`src/body_renderer.hpp`).

## Facts

| Item | Value | How |
|---|---|---|
| Body resource (engine `descriptor+8` name) | `models/characters/dlc01/player/billie/player_body_billie.md6` (md6Def) | `entityDef .../billie/body.def` has `model = "...player_body_billie.md6"`; D2's `emily/body.def` has `model = ".../emily/body.md6"`, which is exactly the string the code pins, so the same field is expected. Live read still needed |
| Body mesh | `.../billie/player_body_billie.md6mesh` (bmd6model, 603,996 B), SHA256 `5648f0933803962d52deb4efa0cba775410208bf553b2aca2a520f3bf79ebd12` | index entry (`baseModel`), unpacked with the repo reader |
| Body md6Def | `inherit "models/characters/player/emily/body.md6"`, `lod 0 { clear  mesh player_body_billie.md6mesh }` | decl text |
| Left / right arm | `.../billie/player_leftarm_billie.md6`, `.../player_rightarm_billie.md6` (inherit Emily's `left_arm.md6`/`right_arm.md6`); defs `left_arm.def`, `right_arm.def` | decl text |
| Right arm VOID variant | `.../player_rightarm_void_billie.md6` (inherits Billie's right arm); mesh SHA256 `f1d0f685e9809bf91f21042b0a674adc225a3e0f3de597c1fedae611c54d5b5c` | decl + mesh |
| Skeleton, body | `models/characters/player/emily/player_body_emily.md6skl`, **20 joints**, the same file as Emily (SHA256 `a8f1f47c...6fbc6d`, byte-identical to the D2 install's copy) | mesh header names it; md6skl hash compared D2 vs DOTO |
| Skeleton, arms | `player_leftarm_emily.md6skl` / `player_rightarm_emily.md6skl`, **69 joints**, byte-identical to D2 (SHA256 `00b4e489...`, `d32b4a68...`), void arm uses the right one | mesh headers; every skin joint index < 69 (`decode_mesh(..., 69)` passes for all three) |
| Body surfaces (file order) | 6, all kind 0, skin joints <= 17 (< 20), weights sum 255: glove 173 v / 244 t; upper 4087 / 7028; lower 1671 / 2940; holster 2106 / 2944; footwear 2506 / 3896; pouch 675 / 942. **Total 11,218 vertices, 17,994 triangles** | `decode_mesh(data,20)` (indices / 3 = triangles, same as Emily 13504 and Corvo 18259) |
| Body materials | `models/characters/dlc01/player/billie/asset/body01/texture/ply_billie_glove.material`; `models/characters/dlc01/female/billie/body01/textures/f_billie_body01_{upper,lower,holster,footwear,pouch}_no_dvision.material` | mesh surface table |
| Arm materials | left and right: `ply_billie_glove.material` + `f_billie_body01_upper.material` (3255+568 v / 5827+861 t left; 3281+395 v / 5827+610 t right). Void: `ply_billie_void_hand.material` (surface kind 2, 7031 v / 12059 t) + `f_billie_body01_upper.material` (401 v / 614 t) | mesh surface tables |
| **Chest-band width** | **0.2559487223625183 m** (41 vertices, all in the `upper` surface, all fully weighted to spine01/02/03) | method below |

### Chest-band method (reproduced bit-exactly on the shipped values)

Decoded eight-weight vertices of the whole body mesh whose bind position has |Z - 1.35| <= 0.0125 and whose non-zero weights are all on
joints 2/3/4 (`center_spine01/02/03_jnt`); width = extent (max - min) of their **Y** coordinate. Run on the D2 install this gives
Emily 0.25142084062099457 (62 vertices) and Corvo 0.353106752038002 (68 vertices), identical to the constants in
`native_torso_fit.hpp`; the same script on Billie's mesh gives the value above. (Extent along X is 0.2396 for Billie, 0.2548/0.2907 for
Emily/Corvo; it is not the pinned axis.)

## Differences that make this more than a table row

1. **Billie's body is not a one-surface hidden proxy.** Emily/Corvo: one surface, `player_body_hidden.material`, one draw record, and the mod
   swaps a textured material in (`BodyMaterialOverride`, requires `draw_count==1`, `SetAll` writes every override slot). Billie: six real
   textured surfaces. A single replacement material would paint her whole body (legs, boots, holster) with one texture, and `apply` would fail
   on `draw_count!=1` anyway. `native_torso_fit_token` also required exactly one surface.
2. `body_material_route()` itself pins nothing (it reads the record and override lists). The avatar pins are in `body_material_candidate`,
   `BodyMaterialOverride::still_body_material`, `native_torso_fit_token`/`native_torso_width`, and the `avatar_emily` flag in
   `dxgi_probe.cpp` (~5176).

## Patch (`billie-avatar.patch`, all under `#if defined(D2VR_GAME_DOTO)`)

`native_torso_fit.hpp`
- Billie constants: resource names (md6, md6mesh, bmd6model), the six `{vertices,triangles}` pairs, total 11218, width 0.2559487223625183.
- `native_torso_width()` row for Billie (keyed by the total vertex count).
- `native_torso_fit_token()`: accepts the Billie body resource; for her it requires exactly the six surfaces (kind 0, matched as a
  multiset because the engine's surface order is not verified), instead of the single-surface test. The width is scaled by model scale like the others.

`body_renderer.hpp`
- `billie_body_name`, `billie_body_material_name` (the six body materials), `billie_native_body()` (body is Billie's, 1..6 draw records, every record is one of those materials).
- `BodyMaterialOverride` gets a native mode (`native_`): `apply` writes nothing (no `SetAll`, no refresh), it only records the owner and
  original drawable bit and reports visible once model+0x1c30 bit 0x10 is set; `enable_drawable_owned` sets only that bit (revalidating the
  route before and after); `restore` writes nothing (the caller already restores the bit from `original_drawable()`); `retire` clears the mode.
  The existing Emily/Corvo path is not entered for Billie and is unchanged otherwise.
- Not changed: `avatar_emily` in `dxgi_probe.cpp` (Billie gives -1, so the hip-gun owner falls back as before; she has no pistol/crossbow
  hip clones to choose between; the Voltaic Gun / dagger are separate work).

Verification done (build tools from `tools/build-msvc.cmd` flags, in a scratch copy of the tree):
- D2 build is token-identical: `cl /EP` of `native_torso_fit_check.cpp` and `body_renderer_check.cpp` against the unpatched tree, whitespace removed, identical.
- Existing `native_torso_fit_check` and `body_renderer_check` pass in both the D2 and the `D2VR_GAME_DOTO=1` builds.
- Two new checks (kept in the scratch `work/tests/`, not in the repo): `billie_torso_check` (token accepted with the six surfaces, width 0.2559..., order-independent,
  refuses changed count/vertices/triangles/kind and a different name, Emily still accepted in the DOTO build) and `billie_body_check` (native route: no SetAll/refresh calls,
  bit-only enable, foreign material or wrong name refused and nothing owned, restore clean). Both PASS. The generator tree (`build/doto-src`) was not regenerated.

## Right-arm VOID variant

Same 69-joint skeleton (`player_rightarm_emily.md6skl`, every skin joint < 69), so the arm hierarchy, inverse-bind and finger-chain validation are
unchanged; only the mesh differs (kind-2 `body_void` surface, 7031 + 401 vertices, material `ply_billie_void_hand`). Nothing in `src/` pins an
arm resource name or arm vertex counts (only `note_hand_mesh.hpp` does, see below), so the void arm is accepted as Billie's right arm by the
existing skeleton-based checks. Treat a swap to it as a model change: `avatar_mesh_token` (primary/surfaces) differs, so any cached
fit keyed on the arm mesh will re-acquire, which is the desired behaviour. Needs a look in the headset: the void hand is a different (bulkier) mesh, so the
ring/hand-pose, finger IK and the hand replacement meshes calibrated on Emily's hand may not fit.

## Not covered by the patch

- `note_hand_mesh.hpp` / `dxgi_probe.cpp` ~18923: the note-hand art files are `note_hand_{corvo,emily}_{left,right}.bin` (format field `character` is 0/1 and the
  resource must equal `models/characters/player/<character>/<limb>.md6`). A Billie note hand needs new art built from her arm meshes and a format
  change (character 2, resource `.../dlc01/player/billie/player_{left,right}arm_billie.md6`); asset work, not a table row.
- `native_hand_reference.hpp` / `authored_hand_bank.hpp` are pinned to the D2 exe hash and inert at runtime; Billie hand references would be new data.
- The body-rig blockers (player/model offsets, skeleton vtable slot, `hand_body_native` RVAs) are separate and still decide whether any of this is reached.

## Unverified (needs a live gameplay read or a headset look)

1. That `descriptor+8` reads exactly `models/characters/dlc01/player/billie/player_body_billie.md6` (inferred from `body.def` and the D2 analogue).
2. The engine's surface list for Billie's body: six surfaces, kind 0, vertex/triangle counts as decoded (`surface+0x54/+0x58`), order, and that LOD0 only is listed.
3. The body model's draw records: count (expected 6), and that their material names are the `*_no_dvision` ones listed (if the engine keeps
   hidden/override materials in the draw records, `billie_native_body` refuses and the body stays hidden; the safe failure). Also the override slot contents (untouched in native mode).
4. Whether model+0x1c2d (DOTO's `0x1c30`) bit 0x10 alone makes Billie's body draw in the VR view, and that the body is otherwise suppressed in first person by that bit and not by another mechanism (shadow/hidden flags, a separate first-person mask).
5. Shading of the six textured surfaces in the headset (darkvision, immolation, blood layer materials are material-side and untouched), clipping against the VR camera/arms,
   skinning of the glove surface (173 vertices at chest height with Y span +/-0.25 m, looks like hands pose geometry baked into the body; check it does not float in front of the chest).
6. Chest-band width as a proxy for her real build: like Emily/Corvo it is a clothed cross-section, not a body measurement; whether 0.2559 m scales her avatar sensibly is a headset question.
7. The `hand_body_native` RVAs and the other body-rig blockers (see `tools/doto/reviews/body-rig.json`).
