# Native palm and controller alignment audit

The existing `hand_bind_grip` axis construction agrees with the OpenXR grip convention. This audit does **not** establish physical controller alignment, and it does not justify changing the runtime translation or adding controller angles.

## Pose convention

[Khronos OpenXR grip semantics](https://registry.khronos.org/OpenXR/specs/1.0-khr/html/xrspec.html#semantic-paths-grip) define +X as the palm normal outward for the left hand and inward for the right hand, and -Z along the gripping tube from little finger toward thumb. A tracked hand's grip origin is the palm surface centroid when forming a fist. A handheld controller's fixed grip origin lies inside the controller and generally aligns with the palm centroid in a neutral grip. These are related definitions, not proof that a skeletal mesh surface point is a particular controller's reported origin.

Our world frame stores forward=-XR Z, left=-XR X and up=XR Y. With `fingers=mean(MCP)-wrist` and `width=indexMCP-pinkyMCP`, the bind frame is forward=`unit(width)`, left=`-unit(cross(fingers,width))`, up=`cross(forward,left)`. Native mirrored geometry supplies the side-dependent sign. No handedness angle correction is needed.

[Meta controller overview](https://developers.meta.com/vr/documentation/unreal/unreal-controllers-overview/) distinguishes OpenXR grip from aim. [Meta's controller troubleshooting](https://developers.meta.com/vr/documentation/unreal/unreal-controllers-troubleshooting/) discusses legacy/default asset conventions; those optional Unreal conventions do not establish an angle to apply to this direct OpenXR pipeline.

## Private mesh evidence

Reproduce with `tools/audit_native_palm_binding.py --pose-report artifacts/research/native-arm-assets/hand-capture-unmodified-v72-14304/pose-0020.json --output <new-private-report.json>`. The tool verifies the pinned executable, read-only capture label, both 69-joint rigs, rigid inverse binds and extracted mesh hashes. It analyzes only the exact native Emily hand-flesh material, excluding gloves, ring, sleeve and alternate body surfaces. The latest private result is `artifacts/research/native-arm-assets/native-palm-binding-audit-v96b.json`.

| Derived quantity | Left | Right |
| --- | ---: | ---: |
| Wrist to mean MCP, native units | 0.10329610 | 0.10329543 |
| Index to pinky MCP, native units | 0.08320206 | 0.08320240 |
| Estimated origin to outward flesh, central ray, native units | 0.02174852 | 0.02157844 |
| Estimated origin to dorsal flesh, central ray, native units | 0.02385157 | 0.02387160 |

The estimated origin is halfway between wrist and mean MCP and lies inside the flesh. Native idle curvature of all four fingers projects positive onto the derived XR +X on the left and negative on the right. This corroborates the anatomical normal signs through geometry; it is an inference, not a physical tracking measurement.

A central square ray grid gives outward surface distances from about 0.01876 to 0.02952 native units. An area-weighted outward central flesh patch lies approximately 0.02282 units along left XR +X and 0.02249 units opposite right XR +X from the skeletal estimate. The patch is a reproducible diagnostic region, **not** a measured closed-fist palm centroid. A millimetre interpretation assumes one native unit per metre; this audit has not independently verified that conversion. Source capture labels alone also do not certify absence of every external pose modification; hashes and source metadata are retained in the report.

## What calibration establishes

The current wizard uses grip-position span, eye-to-hand-midpoint vertical drop, and optional explicitly supplied floor height. It derives avatar reach/shoulder proportions and normalizes valid hand quaternions. It does not solve wrist orientation, controller-to-palm translation, hand size or finger lengths. Quaternion normalization is not an orientation calibration. LOCAL reference-space height alone is not floor height.

A span measurement cannot identify a separate six-degree-of-freedom palm offset for each controller. The smallest defensible next step is a per-side alignment preview in a stable neutral grip, with explicit user confirmation and independently observable hand correspondences: supported hand tracking, measured controller geometry/pose origin, or a deliberate visual alignment procedure. Store any accepted offset by controller interaction profile and side, invalidate it on profile/reference identity changes, and keep avatar scale separate. A controller pose alone cannot reveal its physical palm correspondence.

The bounded source change only clarifies these conventions and tests mirrored XR normals plus transform covariance. No production offset, scale, locomotion or controller-angle change is made. Headset alignment and comfort remain user acceptance work.
