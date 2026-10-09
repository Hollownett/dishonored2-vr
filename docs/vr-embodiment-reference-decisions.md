# Embodiment reference decisions

Reviewed 9 October 2026. Meta recommends faithful tracked movement, natural resting states after tracking loss, explicit avatar checks, and context-specific holding/gripping/swimming animations. These are design references; the Meta avatar SDK is not a Void-engine integration.

[Meta avatar design guidance](https://developers.meta.com/vr/documentation/unity/meta-avatars-best-practices/) supports calibrating before hand acceptance, checking sleeve/wrist geometry, and preserving authored occupied-weapon grasps rather than applying an Empty fist to equipment. Head/controller data alone cannot measure torso/elbow intent.

[Meta hands technology](https://developers.meta.com/vr/design/hands-technology/) discusses hand scale calibration. Our controller-based T-pose measures avatar fit, not individual finger joints; a correct scale does not certify finger closure.

[OpenXR specification](https://registry.khronos.org/OpenXR/specs/1.1/html/xrspec.html) distinguishes valid inferred poses from actively tracked position. New physical translation/query input must require position tracked, use one coherent reference epoch, and revoke across focus or owner changes.

Implementation decisions: retain accepted head-directed locomotion; block calibrated hand/grab publication until measured fit; use one frozen pose/model transform for native attachments and GPU output; preserve native authored grasp when occupied; measure keyhole/climb native outcomes separately from scripted commands. Independent head queries remain disabled until retirement and teardown proof.

[Godot room-scale guidance](https://docs.godotengine.org/en/stable/tutorials/xr/xr_room_scale.html) explains why physically moving the head must remain faithfully tracked: moving the tracking origin to stop that motion can cause nausea. It recommends fading when the player physically moves into blocked geometry. The character body follows the head, while the tracking origin compensates only for body movement actually admitted by collision.

Design inference for this mod: freeze one physical tracking displacement for eyes, controllers and interaction rays. Use collision results to admit body movement and determine a full-view obstruction fade, rather than clamp the head camera. Capture a new reference only at explicit spawn/load/recenter boundaries after native ownership is ready. [Valve's SteamVR fade implementation](https://github.com/ValveSoftware/steamvr_unity_plugin/blob/master/Assets/SteamVR/Scripts/SteamVR_Fade.cs) provides a reference for the fade layer, not collision policy. These decisions are not yet enabled runtime features.
