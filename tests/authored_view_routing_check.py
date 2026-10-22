from pathlib import Path


def check(probe: str, features: str) -> None:
    body = probe[probe.index("void prepare_head_view(void* view) noexcept {"):]
    body = body[:body.index("\n}\n")]
    assert "d2vr::authored_head_view(source_anchor,*authored_heading_now," in body
    assert "authored_heading.update(source_anchor,authored_camera,action_camera_alignment_enabled?action_camera_step.history_epoch:head_pose_epoch)" in body
    # the heading is advanced once per frame (eye 0), both eyes read it
    start = body.index("if(per_frame) {")
    update = body.index("authored_heading.update(")
    end = body.index("\n        }\n", start)  # the eye-0 block closes at 8-space indent
    assert start < update < end, "heading update must sit inside the eye-0 block"
    assert "authored_heading_frame=frames.load();" in body[start:end]
    # eye 1 only consumes a heading advanced for this frame
    assert body.count("authored_heading_frame==frames.load()") == 2
    # the recenter origin and the failure handling are part of the call
    call = body[body.index("native_eye=d2vr::authored_head_view("):]
    call = call[:call.index("native_eye.reset();")]
    assert "head_origin.orientation" in call and "catch(const std::invalid_argument&)" in call
    # the audio listener follows the head-owned view in authored scenes, the native camera only under the knob
    assert "listener_view=d2vr::authored_head_view(source_anchor,*authored_heading_now,centre_pose,centre_pose,1.0,head_origin.orientation,authored_physical_offset)" in body
    assert "legacy_authored_camera?&source_anchor:&*listener_view" in body
    assert "authored_camera?&source_anchor:nullptr" not in body
    # The same bounded/crouch-compensated offset reaches both eyes and the listener.
    assert body.count("head_origin.orientation,authored_physical_offset)") == 2
    offset = body.index("action_camera_physical_offset=head_offset;")
    assert body.index("head_offset.y=std::clamp(head_offset.y+crouch_lift_now") < offset
    assert "action_camera_alignment_enabled?action_camera_physical_offset:d2vr::Vec3{}" in body
    assert "action_camera_step=action_camera_context.sample(frames.load()," in body
    assert "body->camera_identity.generation,head_pose_epoch,game_xr->reference_epoch(),authored_camera},per_frame)" in body
    reject = body.index("if(!action_camera_step.accepted)")
    assert reject < body.index("auto source_anchor=*input;")
    assert "if(action_camera_step.changed) {\n                anchor_smoother.reset();anchor_smooth_frame=~0ull;" in body
    assert "if(anchor_smooth_frame!=frames.load())" in body
    assert "(!action_camera_alignment_enabled || action_camera_step.advance)" in body
    assert 'action_camera_alignment_enabled=!fs::exists(root/L"build"/L"legacy-action-camera-alignment")' in probe
    assert '"id": "action-camera-alignment"' in features
    assert '"file": "legacy-action-camera-alignment"' in features
    # the old camera copy survives only under the knob
    assert probe.count("d2vr::authored_eye_camera(") == 1
    assert "if(legacy_authored_camera) native_eye=d2vr::authored_eye_camera(" in body
    assert "if(action.scripted && legacy_authored_camera) {" in body
    assert 'legacy_authored_camera=fs::exists(root/L"build"/L"legacy-authored-camera")' in probe
    assert "comfort_meter.set_authored_snaps(authored_heading.snaps())" in body
    assert '"file": "legacy-authored-camera"' in features and '"kind": "disable"' in features


if __name__ == "__main__":
    root = Path(__file__).resolve().parents[1]
    check((root / "src/dxgi_probe.cpp").read_text(encoding="utf-8"),
          (root / "tools/default_features.json").read_text(encoding="utf-8"))
    print("authored_view_routing_check ok")
