# VR v726 (test candidate, 2026-10-31)

The user clarified the v724 boat ride: the boat model was upside down, floated in the air "as if mirrored above the water", and sailed symmetric to the real motion. That describes a reflection drawn in the wrong place, not the boat itself.

## Ocean reflection off in VR
- Water is the ocean renderer. Its only reflection is screen-space (`r_skipOceanSSR`, `r_water_ScreenSpaceMaxStepsCount`); the game has no planar-reflection cvars.
- The SSR reflects from the screen of a single camera. Per eye, and above all in a comfort cutscene, where the eye's rotation differs from the scene camera's, that mirrors the boat to the wrong place.
- Fix: `r_skipOceanSSR` = 1 is a VR session default (`NativeCvars::vr_defaults`), like SSAO and lens flares, which were cut for the same per-eye reason.
  - RVA 0x397aed0, checked with `tools/cvar_locate.py`. The name is checked before the write, and the original is restored at the end of the session.
  - It saves about 0.26 ms (docs/VR-PERF-LAPTOP.md).
- The water keeps its colour and waves; it loses only the reflections.

## Kept from v725
- A comfort cutscene ride faces its direction of travel.
- After a cutscene, the dialogue panel opens 1.6 m ahead.

## Checks
- `native_cvars_check` passes with twelve overrides; temporal indices shift by one.
- `cutscene_view_check`, `anchor_smoother_check`, `world_ui_pose_check`, `vr_combat_check` and `native_world_ui_check` pass.
- The DLL builds.
- Not headset-tested.
