"""G1 scripted-moment recenter: the helper runs once per displayed frame after the authored heading, both authored views
compose its offset into the origin, the exit fold rotates HeadCalibration, knobs bypass it, and the vignette sees the ease."""
from pathlib import Path


def check(probe: str, vignette: str, action: str) -> None:
    body = probe[probe.index("void prepare_head_view(void* view) noexcept {"):]
    body = body[:body.index("\n}\n")]
    # once per displayed frame, inside the eye-0 comfort block, after the authored heading advanced
    assert probe.count("scripted_recenter.update(") == 1 and body.count("scripted_recenter.update(") == 1
    start = body.index("if(per_frame) { // comfort telemetry")
    end = body.index("\n        }\n", start)
    heading = body.index("authored_heading.update(", start)
    update = body.index("scripted_recenter.update(", start)
    assert start < heading < update < end, "recenter update must follow authored_heading.update inside the per_frame block"
    block = body[start:end]
    # knobs and paused/menu frames bypass the helper; paused frames keep the offset (no reset on logic != 2)
    assert "const bool scripted_view_live=scripted_recenter_enabled && !legacy_authored_camera;" in block
    guard = block[:block.index("scripted_recenter.update(")]
    guard = guard[guard.rindex("if(scripted_view_live"):]
    assert guard.startswith("if(scripted_view_live && game_logic_mask()==2 && (!action_camera_alignment_enabled || action_camera_step.advance)) {")
    assert "else if(!scripted_view_live) {scripted_recenter.reset();scripted_offset_now=0;" in block
    assert block.count("scripted_offset_now=0") == 1, "only the bypass zeroes the offset; a paused frame keeps it"
    # the inputs: HMD yaw against the composed origin, this frame's heading and snaps, the identity epoch, the levelled body yaw
    assert "const double hmd_rel=d2vr::tracked_yaw(centre_pose.orientation,head_origin.orientation);" in block
    assert "const double rel_composed=scripted_wrap(hmd_rel-scripted_offset_now);" in block
    assert "scripted_recenter.update({scripted_clock,authored_camera," in block  # live-frame clock: a pause does not finish the ease
    assert "authored_heading.snaps()" in guard + block[block.index("scripted_recenter.update("):]
    assert "scripted_recenter_epoch(game_xr->reference_epoch(),body->player,body->camera,body->camera_identity.generation)" in block
    assert "std::atan2(static_cast<double>(anchor.basis[1]),static_cast<double>(anchor.basis[0]))" in block
    # every fold (exit and residual deltas) rotates the calibration, and head_origin is refreshed the same frame,
    # before the comfort meter, the 6DoF offset and every gameplay consumer read it
    fold = block.index("if(scripted.fold_rad) {head_calibration.rotate_origin(*scripted.fold_rad);")
    refresh = block.index("head_origin=*folded;", fold)
    assert fold < refresh < block.index("comfort_meter.feed(")
    assert body.index("head_origin=*folded;") < body.index("auto head_offset=centre-head_origin.position;")
    # both authored views compose the same per-frame origin
    composed = "const d2vr::Quaternion composed_origin=d2vr::normalized(d2vr::multiplied(d2vr::yaw_quaternion(scripted_offset_now),head_origin.orientation));"
    assert body.count(composed) == 1 and end < body.index(composed)
    assert body.count("d2vr::authored_head_view(") == 2
    assert body.count("composed_origin,authored_physical_offset)") == 2
    assert "head_origin.orientation,authored_physical_offset)" not in body
    assert body.index(composed) < body.index("native_eye=d2vr::authored_head_view(")
    # knob read with the other authored-camera knobs; reset with the heading
    assert 'scripted_recenter_enabled=!fs::exists(root/L"build"/L"no-scripted-recenter");scripted_recenter.reset();scripted_offset_now=0;' in probe
    assert probe.index('legacy_authored_camera=fs::exists(root/L"build"/L"legacy-authored-camera")') < probe.index('L"no-scripted-recenter"')
    assert "head_calibration={};scripted_recenter.reset();scripted_offset_now=0;" in probe
    # vignette: lifted for the ease only
    assert "inline bool vignette_target_allowed(bool gameplay,bool authored,bool menu_open,bool settings_open,bool recenter_easing=false)" in vignette
    assert "vignette_target_allowed(gameplay && owner,authored_camera,x_wheel.wheel(),game_xr && game_xr->settings_open(),recenter_easing)" in probe
    assert "const bool recenter_easing=scripted_recenter_easing_live(GetTickCount64());" in probe
    assert "recenter_easing?std::max(published_turn_rate_rad_s.load(),scripted_ease_rate_rad_s.load()):published_turn_rate_rad_s.load()" in probe
    # diagnostics: state index from the FSM, status block, trace
    assert "int state_index=-1;" in action and "if(out.state_index<0) out.state_index=static_cast<int>(entry.index);" in action
    assert "action.state_index" in block
    status = probe[probe.index(',\\"scripted_view\\":'):]
    status = status[:status.index("}()")]
    for key in ("entries", "reaims", "folds", "skipped_small", "easing", "last_delta_deg", "last_state_index", "enabled"):
        assert f'\\"{key}\\":' in status, key
    assert probe.index(',\\"comfort\\":') < probe.index(',\\"scripted_view\\":')
    assert 'fs::exists(root/L"build"/L"scripted-view-trace")' in probe and 'output/L"scripted-view.csv"' in probe
    assert '"t,authored,state_index,hmd_rel_deg,rel_composed_deg,heading_deg,offset_deg,fold_deg,body_deg,view_yaw_deg\\n"' in probe
    assert "scripted_view_rows<4096" in block and "scripted_view_rows%64==0" in block


if __name__ == "__main__":
    root = Path(__file__).resolve().parents[1]
    check((root / "src/dxgi_probe.cpp").read_text(encoding="utf-8"),
          (root / "src/comfort_vignette.hpp").read_text(encoding="utf-8"),
          (root / "src/native_action_camera.hpp").read_text(encoding="utf-8"))
    print("scripted_recenter_routing_check ok")
