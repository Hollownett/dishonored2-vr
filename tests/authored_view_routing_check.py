from pathlib import Path


def check(probe: str, features: str) -> None:
    body = probe[probe.index("void prepare_head_view(void* view) noexcept {"):]
    body = body[:body.index("\n}\n")]
    assert "d2vr::authored_head_view(source_anchor,*authored_heading_now," in body
    assert "authored_heading.update(source_anchor,authored_camera,head_pose_epoch)" in body
    # the heading is advanced once per frame (eye 0), both eyes read it
    assert body.index("if(eye==0) {") < body.index("authored_heading.update(")
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
