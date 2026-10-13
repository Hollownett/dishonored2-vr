# Independent body fit and shared native pose repairs

This follows the rejected v221 review and the first source repair pass. v221 remains launch-blocked. The user requested continued work without a headset; no headset session or visual acceptance is part of this pass.

The intended result remains a complete first VR iteration: a body fitted to the player, visible walking legs, stable hands and sword, reliable physical grabbing, usable menus and sustained playable performance. This document records implementation and bounded checks; it does not certify that result.

## Player measurements

The guided capture first measures shoulder landmarks with the controller grip centres placed at the shoulder joints. After a one-second stable hold, the user releases both grips and captures a level T-pose. Left and right shoulder-to-grip reach are measured separately. The solver still divides each reach into upper arm and forearm using the native rig proportions; those two anatomical lengths are not independently measured.

The Measurements page accepts optional eye height, shoulder-joint width, chest width and each reach, in centimetres. Blank chest width stays unmeasured. Chest width is a separate side-to-side measurement; three tracked points do not identify it independently. Supplied dimensions are range checked, and contradictory reach measurements produce an error. Unsupported fits and cancelled retries retain the previous accepted fit.

OpenXR STAGE is queried only if the runtime supports it. Its located floor must have valid position and level orientation. LOCAL is never treated as a floor. Without that datum or a supplied eye height, player standing height stays unknown. Preferences version 7 stores the five optional values; older files leave them unknown. Settings files remain unchanged until the player edits measurements.

## Native geometry and pose ownership

The chest surface baselines come from the extracted, hash-checked Emily and Corvo meshes. A runtime token verifies exact player/body ownership, wrapper-to-mesh mapping, geometry counts, animation generation, skeleton and uniform model scale. Full resource checks use the bounded refresh lane. Both arm and body upload paths also check the current body owner using fixed slots; replacement revokes the retained solution immediately.

The fitted chest transforms widen the native skin across spine joints. Arm cuff transforms receive the same transverse affine factor, while upper-arm, wrist and finger transforms stay rigid. Attachment admission permits bounded positive affine cuff joints only. Native packing preserves all three basis columns. Width fitting accounts for the native model scale and does not divide by the player's height scale: height changes joint centres, not these local mesh offsets.

The body, both arms and equipment consume the same calibrated publication. Full-body mode cannot fall into the separate hand solver when its body is unavailable. Duplicate millisecond publications consume zero temporal time rather than resetting gait. Real discontinuities and authored ownership still invalidate or restore the appropriate native paths.

## Equipment and grabbing

Freshness now includes the clock of the XR pose sample, separately from its publication in native world coordinates. Arm, attachment and physical-grab limits share 250 ms. New equipment admission requires the exact source publication and verified model frame. Retention permits movement of the same native model: the engine composes both its rendered joints and attachment through the current parent frame. Owner, mapping, reference, malformed frame or source-age changes revoke the palette.

Slow valid hand samples update and decay throw velocity. A pending release consumes the final available hand sample before release; teleport and stale-source checks precede a throw. A bounded decision ring records grip press/release and gate/owner transitions, even between observer samples. Only the probe worker writes `native-grab-decisions.jsonl`; archive tools preserve it. This will distinguish native equipment, failed acquisition and physical holds in the next real recording.

## Performance evidence

The rejected recording still demonstrates sustained slow gameplay. The new Present boundary record measures the full callback, desktop Present, control work, logic discovery, lean, grip decisions, takedown/crouch, post-XR housekeeping and GPU hook maintenance. Housekeeping includes context restoration, glyph processing, FSR preparation and timing/status writes. These are nested or adjacent spans; do not sum overlapping spans. Boundary logging after the final clock sample is excluded from its reported duration.

The analyzer matches exact Present/XR end/epoch/configuration records and preserves consecutive slow windows. A fast median, simulated submission rate, successful FSR dispatch or sub-millisecond sampled material cost cannot establish playable headset performance. No measured cause or speedup has yet been established for the seven-second v221 stalls.

Glyph texture identification previously copied a texture and immediately mapped it for CPU reading in Present. That path can wait for the GPU. It now keeps eight active staging jobs and eight queued views, copies once, and retries a busy `Map(...DO_NOT_WAIT)` on later frames. Busy data is retained rather than rejected as a non-glyph. Resource ownership, queue bounds, device/context replacement, native label output and cleanup are checked with CPU COM fixtures. A standalone D3D11 WARP test also checks actual COM ownership, staging retention, later-frame completion, exact native BC7 replacement bytes and teardown. It submits work with a test-only Flush; the production processing path performs no Flush or synchronous wait. Painting and encoding a completed label still occur in Present. This removes a blocking API path; it does not identify the cause of the recorded stalls or establish an FPS improvement.

## Evidence and open acceptance

The complete required build/check script and final MSVC `/W4 /WX /O2 /MD /LD` link passed on 11 October. The isolated output is `build/d2vr_render_probe_repairs_20261011.dll`, SHA-256 `a0255b07bfa3da4fbcc83f761936f5bbc8292c05946d2d5a3ac49a322f4b43d3`. It has not been loaded into the game and is not selected by the normal demo launcher. This is a diagnostic artifact, not a new accepted headset candidate.

Its complete compiler-input archive is `artifacts/build-source/20261011T093246.948484Z-37b329b1`. The archive was stable at capture, and input bytes matched again after the build. The compiler read the workspace, not a frozen checkout. Two failed build attempts remain preserved: their integration fixtures assumed the former one-stage capture and Y-to-dismiss controls. The fixtures now execute the deliberate shoulder/release/T-pose and measurement-page transitions while retaining identity, death/loading, reuse and reference-space assertions.

The evidence index is `artifacts/repair-v221-independent-fit/repair-report.json`. It includes the settings and production GameXr two-stage simulation, arm-first/duplicate/slow-source transitions, guarded native resource fixtures, decision-ring transitions and actual eight-weight mesh deformation at two heights for both characters. Pure cuff widths match; mixed-weight material seams remain unverified.

The preserved native morning run inventory identifies v193 at 09:32–09:35 Warsaw on 10 October, with a matching DLL/loaded hash. It does not establish that this is the user's preferred morning build, and no exact source archive was found for it.

Visible gait, shoulder and clothing seams, sword placement during gameplay, physical object behaviour under real frame stalls, native cadence/compositor performance and comfort remain open. Accepted perspective, locomotion, turning and VR entry/exit must survive that acceptance pass. Other outstanding UI/gameplay entries in `docs/VR_REPAIR_v221.md` remain part of the overall product goal.

Primary references: [OpenXR reference spaces](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrReferenceSpaceType.html), [space enumeration](https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrEnumerateReferenceSpaces.html), [Khronos skinning mathematics](https://github.khronos.org/Vulkan-Site/tutorial/latest/Advanced_glTF/Skeletal_Compute_Skinning/02_skinning_math.html), and [D3D11 Map behaviour](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-map). Native offsets and composition contracts are documented beside the source and in the existing private decompilation archive.
