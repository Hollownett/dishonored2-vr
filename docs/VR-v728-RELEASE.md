# VR v728 (test candidate, 2026-10-31)

The user ran the boat ride with v727: the boat was still upside down and going backwards. The user was right that this is not a reflection, and the v726 ocean-SSR change is withdrawn.

## What v727 recorded (`cutscene-anchor.csv`, raw scene camera rows and player root)
- At the dock the player root was at z −5.3.
- At the ride start, the root and camera jump into the boat: root z +0.30, camera +1.36, upright. That is about 6 m above the dock.
- 24 frames later, in one frame:
  - root z +0.30 → −0.30 and camera +1.36 → −1.36;
  - forward and up negated, left unchanged (det +1): a 180-degree pitch about the boat's lateral axis, through a point at z ≈ 0.
- For the next 20 s the boat rides upside down, facing backwards.
- v723: the camera height swung between about +1.4 and −2.4 every frame, in ~3 cm (half-float) steps. That is the same boat frame, unstable.
- So the boat's own frame sits at height 0 and takes a flipped orientation. Its motion comes from the ocean:
  - `arkMotionOceanOption` (`normalWeight`, `offsetZ`);
  - `arkOcean::RegisterPointsToBeEvaluatedOnGPU` / `PendingGPUEval` (half-float results read back later).
- The evaluated height and normal are wrong in VR.

## Change
- `r_oceanUseAsync` = 0 in VR (default 1, RVA 0x32eb0a0, `NativeCvars::vr_defaults`, name-checked, restored at the end).
- Suspected: the async ocean evaluation readback breaks when the engine renders paired native eyes each frame. Not proven; this run tests it.
- `r_skipOceanSSR` is no longer touched.

## Checks
- `native_cvars_check` passes.
- The DLL builds.
- Not headset-tested.
