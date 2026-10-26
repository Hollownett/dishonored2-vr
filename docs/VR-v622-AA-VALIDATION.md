# v622 alternating-eye AA validation — 26 October 2026

Tested unchanged v622 DLL `7a6ff8d74b77330b1e325297dab518f6cf9a2675fed9d373b190277b7937b38d` on the user’s NVIDIA GeForce RTX 3070 Laptop GPU, driver 615.71.09. The game ran under Proton with simulated OpenXR/controller poses, with actual NVIDIA GPU work. The simulated FOV was symmetric (−0.8 to +0.8 radians per axis), so asymmetric/canted headset projection was not exercised. Native Windows/MSVC and headset presentation were not tested.

Evidence root: `/mnt/steamssd/d2vr-releases/v622/aa-validation-20261026`.

## Real game runs

Each script has a 120-second total session budget, including loading. The idle scenario moves the simulated head continuously. The pause variant presses the native menu button at script second 65, resumes at second 70, then continues head motion. All runs use default-on alternate-eye rendering and the same reviewed v622 DLL, temporarily staged under the installed DLL filename by the save/config-restoring harness.

| Mode/provider | GPU evaluations/dispatches | Alternated frames | Pose/image mismatches | Result |
| --- | ---: | ---: | ---: | --- |
| DLAA / Helldivers 2 | 3,392 NGX | 3,457 | 0 across 3,452 checked one-eye Presents | Pass |
| DLSS Quality / Helldivers 2 | 2,736 NGX | 2,764 | 0 across 2,760 checked one-eye Presents | Pass |
| FSR 3 Native + pause/resume | 3,103 FSR | 3,135 | 0 across 3,134 checked one-eye Presents | Pass |
| DLSS Quality + pause/resume / Helldivers 2 | 9,319 NGX | 9,513 | 0 across 9,513 checked one-eye Presents | Pass |
| DLSS Quality + pause/resume / auto-selected Crimson Desert | 11,873 NGX | 12,070 | 0 across 12,068 checked one-eye Presents | Pass |

DLAA and FSR Native rendered at full resolution, 1912×1046 per eye. DLSS Quality queried NGX for 1275×697, adopted fraction 0.666348, and the engine rendered 1274×697 because of integer rounding. Both DLSS eyes reported backend generation 5 and the FSR eyes generation 3. Live samples verified DLSS state 2 and increasing evaluations; the final teardown snapshot returns state to 0 normally.

There were zero NGX evaluation/create failures, owner refusals, NGX-to-FSR fallbacks, or alternate-mode fallbacks. Each run recorded one safe startup current-colour copy before temporal contexts were ready. “No fallback” must not be interpreted as temporal AA operating from the first loading/gameplay frame. The pause runs returned from paired menu rendering to alternating gameplay and exercised fresh history resets for both eyes.

The pose checker joins Setup → native eye Begin → submitted layer and checks both the freshly rendered eye and the retained eye. Its zero mismatch result establishes image/pose correspondence in the observed game pipeline; it is not a headset compositor measurement.

## Providers and real GPU pixel checks

The signed user-owned Helldivers 2 provider was loaded in place, SHA-256 `8707e53b26c68c606b98bf31c223485ff30d310a261b1a36d48b2eaabc1507ec`. Nothing was downloaded, copied or bundled. Automatic discovery on this laptop instead selects Crimson Desert’s newer provider, SHA-256 `3975567b8943c53acce397f2b72380092f84f162d00b0d2c7d08a1025c563983`; that provider passed the separate DLAA and DLSS Quality GPU checks. It also passed an actual game pause/resume run, listed in the matrix.

`tests/alternate_temporal_gpu_check.cpp` passed real RTX/DXVK executions for FSR 3 Native, scaled FSR 3, DLAA and DLSS Quality. It dispatches one eye at a time through the production FsrTaa seam, checks independent red/green spatial patterns in GPU readbacks, requires the opposite eye’s complete retained output to remain byte-identical, and checks skipped work, reset recovery and rejection of an old queued job. The native stale-job test changes the source pattern so an unchanged old output cannot satisfy its oracle. Backend identity and actual NGX evaluation deltas reject silent FSR substitution. Quality tests adopt NGX’s queried optimal size and verify the feature is rebuilt when necessary.

The existing `fsr_taa_gpu_check --dlss` lane was rebuilt and executed on the RTX as well: DLAA and DLSS Quality passed, including native pipeline-state preservation, deselection/reselection, both-eye history reset and duplicate-dispatch/current-colour fallback ordering. This lane uses paired synthetic input; the new regression supplies alternating cadence and the game runs supply native input and pose correspondence.

The first new-fixture attempt failed before meaningful assertions because default D3D device creation selected feature level 11.0, while the production FSR backend requires 11.1, and a nonblocking staging Map returned WAS_STILL_DRAWING. The fixture now explicitly requires 11.1 and retries that host-readback readiness condition within a bounded timeout. The original failure logs and diagnosis are preserved in `gpu-attempt-1`; all output/backend/reset assertions remain enforced.

## Images and limits

Fresh timed left/right source captures were saved every two script seconds. Reviewed first illuminated and late pairs showed no obvious one-eye corruption in the captured base images. A black loading-transition pair is retained separately. These captures are before reticle/HUD overlays, are explicitly tagged `base_before_overlays`, and do not include headset compositor reprojection. Their legacy `antialiasing` string is derived from source-pointer inequality; use the selected AA, backend identity and NGX counters to identify the active algorithm.

The synthetic regression uses zero jitter and checks interior RGB/contrast; it does not certify border pixels, alpha, moving-scene ghosting, history quality during device loss, or headset comfort. Pause/resume and cold process starts do not establish recovery from a runtime fault. Loading consumes part of each 120-second session, so the evidence does not cover two uninterrupted minutes of headset gameplay.

The user’s saved FSR Native choice and installed files are restored after the game harness runs. No headset visual acceptance, native Windows release gate, or headset FPS claim follows from these results. Full shader/capture tracing and readback overhead also prevent interpreting these runs as clean performance benchmarks.
