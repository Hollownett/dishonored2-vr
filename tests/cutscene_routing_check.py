"""G2 VR cutscenes (camera plan Task 5): routing of an immersive Cinematic32 frame.

- Only the render-only gates admit an open exact-32 cutscene (sample_stereo's restore, observe_eye_setup/begin, prepare_head_view's
  early return); every side effect stays on raw logic 2 / render_world_scene(), and render_world_scene() itself is unchanged.
- The present thread classifies each Present with CutscenePolicy (complete distinct native pair, camera, no fault) and passes it to
  GameXr::present as authored_scene_render, with raw gameplay; nothing new is shown while the pair forms or after a fallback.
- GameXr::present: an immersive frame submits projection layers with a mandatory tracked pair, the native menu pointer is off, the raw
  trigger passes, the controls block forwards the raw buttons (skip), and pause or any other mask gives the menu quad again.
The boolean routing in GameXr::present is also evaluated here for the immersive, theater, pause and gameplay cases."""
import re
from pathlib import Path


def fn(text: str, signature: str) -> str:
    body = text[text.index(signature):]
    return body[:body.index("\n}\n")]


def cpp_bool(expr: str, names: dict) -> bool:
    """Evaluate a C++ boolean expression of the given names (&&, ||, !, ==, !=, parentheses)."""
    py = expr
    for name in sorted(names, key=len, reverse=True):
        py = py.replace(name, f" __{abs(hash(name))} ")
    py = py.replace("&&", " and ").replace("||", " or ")
    py = re.sub(r"!(?!=)", " not ", py)
    env = {f"__{abs(hash(k))}": v for k, v in names.items()}
    assert re.fullmatch(r"[\s()a-z_0-9!=]*", py), py
    return bool(eval(py, {"__builtins__": {}}, env))  # noqa: S307 - the expression is our own source


def check(probe: str, xr: str) -> None:
    # --- the gates: render-only sites admit the open exact-32 cutscene; render_world_scene() is unchanged
    assert "inline bool render_world_scene() {const auto logic=game_logic_mask();return logic==2 || d2vr::world_ui_logic(logic)!=d2vr::WorldUiKind::none;}" in probe
    assert "inline bool cutscene_scene_render() {return cutscene_gates_allowed.load(std::memory_order_acquire) && game_logic_mask()==d2vr::cinematic_logic_mask;}" in probe
    assert "inline bool render_scene_frame() {return render_world_scene() || cutscene_scene_render();}" in probe
    assert "static std::atomic<bool> cutscene_gates_allowed{false};" in probe
    assert probe.count("render_scene_frame()") == 5, probe.count("render_scene_frame()")  # the definition + 4 sites
    # the definition and render_scene_frame(); Task 6: prepare_head_view's authored scene camera, view decision, provisional origin and
    # comfort-meter skip, and the present thread's cutscene vignette
    assert probe.count("cutscene_scene_render()") == 4, probe.count("cutscene_scene_render()")  # definition, render_scene_frame, the per-frame latch, the vignette
    assert probe.count("cutscene_gates_allowed=") == 2  # the present thread + StartProbe reset
    stereo = fn(probe, "void sample_stereo(IDXGISwapChain* chain,UINT64 number,const DXGI_SWAP_CHAIN_DESC& desc) {")
    assert "if(xr_requested && !render_scene_frame() && !cinematic_probe_keeps_stereo()) {" in stereo
    setup = fn(probe, "void observe_eye_setup(void* view) noexcept {")
    assert "if(!render_scene_frame()) {eye_sources.invalidate();native_eye_pair.reset();return;}" in setup
    begin = fn(probe, "void observe_eye_begin(void* view) noexcept {")
    assert "if(!render_scene_frame()) {eye_sources.invalidate();native_eye_pair.reset();}" in begin
    # the identity / allocation / N->N+1 checks are kept for the cutscene's eyes too
    assert "identity!=eye_camera_owner" in setup and "eye=eye_sources.match(address,frame-1,GetCurrentThreadId(),gpu_render_thread.load());" in begin
    head = fn(probe, "void prepare_head_view(void* view) noexcept {")
    # Ruling 15: prepare_head_view reads the cutscene state once per displayed frame and uses that latch everywhere
    assert head.count("cutscene_scene_render()") == 1 and "if(cutscene_frame_of!=frames.load()) {cutscene_frame_latched=cutscene_scene_render();" in head
    assert head.index("const bool cutscene_frame=cutscene_frame_latched;") < head.index("const bool authored_camera_seen=")
    assert head.count("render_scene_frame()") == 1 and "if(!render_scene_frame()) {native_audio_listener.clear();" in head
    # Task 6 (cutscene_view.hpp): the cutscene state the head view reads is the once-per-frame view decision, the provisional origin and the
    # comfort vignette's scene speed; nothing else (no policy, no hold, no knob file)
    assert "cutscene_policy" not in head and "cutscene_hold" not in head and "refresh_cutscene_knobs" not in head
    assert "cutscene_view_now=cutscene_view_tracker.step(cutscene_view,static_cast<d2vr::CutsceneMode>(cutscene_mode_effective()),frames.load()," in head
    assert "const bool cutscene_view=cutscene_frame && authored_camera && !legacy_authored_camera;" in head
    assert head.index("cutscene_view_tracker.step(") < head.index("if(camera_smoothing_enabled) {")  # the raw pose, and a cut resets the smoother first
    assert "if(cutscene_view_now.cut.cut) { // a hard cut: the vertical smoothing must not glide across it\n                anchor_smoother.reset();" in head
    assert "(cutscene_frame && body->authored_view()); // G3:" in head
    # the room origin is never first-calibrated by a cutscene frame
    assert "const bool room_held=head_calibration.holds(game_xr->reference_epoch()," in head
    assert "const bool provisional_origin=cutscene_frame && !room_held;" in head
    assert "const auto calibration=(provisional_origin?cutscene_calibration:head_calibration).get(" in head
    # Ruling 12: the first gameplay frame of the same owner adopts the provisional origin (position and yaw) before the room origin is read
    adopt = head.index("head_calibration.adopt(*provisional,game_xr->reference_epoch(),body->player,body->camera,body->camera_identity.generation)")
    assert "if(!cutscene_frame && !room_held && cutscene_calibration.holds(" in head
    assert adopt < head.index("const auto calibration=(provisional_origin?cutscene_calibration:head_calibration).get(")
    clear = head.index("if(!cutscene_frame) cutscene_calibration.clear();")  # a never-adopted provisional origin does not outlive gameplay
    assert adopt < clear < head.index("const auto calibration=(provisional_origin?cutscene_calibration:head_calibration).get(")
    assert "scripted_recenter.rebase(" not in head and "dropped_folds" not in probe
    # Ruling 14: the view lasts through the authored camera's logic-2 tail
    assert "const bool cutscene_tail=game_logic_mask()==2 && authored_camera && !legacy_authored_camera;" in head
    assert "source_anchor,cutscene_tail);" in head
    # VR comfort: a cut re-latches the heading; VR full: the scene's full rotation with the head on top
    assert "authored_heading_now=authored_heading.cut_to(source_anchor);" in head and "authored_heading_now=authored_heading.cut_to(source_anchor,false);" in head
    assert "native_eye=d2vr::cutscene_full_view(source_anchor,centre_pose,eye_pose,composed_origin,1.0,authored_physical_offset);" in head
    assert head.index("cutscene_full_view(") < head.index("native_eye=d2vr::authored_head_view(")
    # the comfort meter is gameplay telemetry
    assert "if(!cutscene_frame) { // gameplay telemetry" in head and head.index("if(!cutscene_frame) { // gameplay telemetry") < head.index("comfort_meter.feed(")
    # every side effect after the early return stays on raw logic 2 or the unchanged render_world_scene()
    for gate in ("if(per_frame && game_logic_mask()==2) {physical_drop=-head_offset.y;",
                 "if(per_frame && game_logic_mask()==2 && !authored_camera) {",                      # HUD world frame
                 "if(native_turning_requested && per_frame && game_logic_mask()==2)",                  # turning
                 "if(game_logic_mask()==2 && physical_grab_requested && per_frame",                    # physical grab
                 "if(game_logic_mask()==2 && controller_aim_requested && per_frame) {",               # controller aim
                 "if(game_logic_mask()==2 && (native_camera_trace_requested || native_left_aim_requested || native_shot_aim_installed || native_crosshair_installed) && per_frame) {",
                 "if(render_world_scene() && (tracked_arms_requested || native_weapon_attachment_requested) && per_frame) {",  # arms, attachment
                 "if(listener && game_logic_mask()==2 && game_xr->state==XR_SESSION_STATE_FOCUSED &&",  # the native listener is left alone under 32
                 "const bool scripted_logic=game_logic_mask()==2 || cutscene_step.kind!=d2vr::CutsceneViewKind::none;"):  # the G1 clock (+ the cutscene view)
        assert gate in head, gate
    assert head.count("render_world_scene()") == 3, head.count("render_world_scene()")  # the two arm/attachment sites + the left-aim hand-off
    # --- the present block: classification, hold, the present call
    block = probe[probe.index("const auto gameplay=game_logic_mask()==2;\n                        const bool cutscene_logic="):]
    block = block[:block.index("if(alternate_on()) {\n                                if(game_xr->alternate_error)")]
    assert "if(!owner && (gameplay || cutscene_logic)) owner=d2vr::find_native_player_camera(read_self,image_base);" in block
    assert "const bool authored_camera=gameplay && owner && native_action_camera.poll(" in block  # raw gameplay keeps its authored camera
    cls = block[block.index("bool cutscene_immersive=false;auto cutscene_hold=d2vr::GameXr::SceneHold::none;"):block.index("cutscene_present_immersive=cutscene_immersive;")]
    for need in ("if(!(cutscene_mask&d2vr::cinematic_logic_mask)) {",
                 "if(cutscene_policy.active()) cutscene_policy.end();",
                 "const bool exact=cutscene_mask==d2vr::cinematic_logic_mask;",
                 "if(exact && !cutscene_paired_path() && !knob_off && mode!=d2vr::CutsceneMode::theater) cutscene_policy.reject(d2vr::CutsceneFallback::incomplete_pair);",
                 "frame.paired_complete=pair_ready && !mono_path && native_eye_capture_requested && views.size()==2 && completed_left && completed_right &&",
                 "game_xr && game_xr->rendered_pair_ready();",
                 "frame.eyes_distinct=completed_left.Get()!=completed_right.Get() && views.size()==2 &&",
                 "separated(views[0].pose->position,views[1].pose->position) && rendered_distinct;",
                 "frame.camera_readable=owner && owner->view && owner->authored_view();",  # Ruling 15: camera mode 10 only
                 "frame.fault=recoverable_faults.load()>cutscene_fault_base;",
                 "cutscene_immersive=cutscene_policy.immersive(frame,mode,knob_off);",
                 "if(gates_were_open) cutscene_flush_presents=3;",
                 # fix round 1: pending and post-fallback frames are black; a tolerated miss replays only this cutscene's own layer
                 "cutscene_hold=cutscene_policy.pending() || cutscene_flush_presents>0?d2vr::GameXr::SceneHold::black:",
                 "cutscene_policy.holding()?d2vr::GameXr::SceneHold::replay:d2vr::GameXr::SceneHold::none;",
                 "refresh_cutscene_knobs(GetTickCount64(),(cutscene_mask&d2vr::cinematic_logic_mask)!=0);",
                 "cutscene_gates_allowed=d2vr::cutscene_gates_open(mode,knob_off,cutscene_policy.reason(),cutscene_paired_path());"):
        assert need in cls, need
    assert "if(cutscene_hold!=d2vr::GameXr::SceneHold::none) {completed_left.Reset();completed_right.Reset();}" in block
    # Ruling 10a: the reason is snapshotted before reject(), so a rejected cutscene records its fallback
    assert cls.index("const auto before=cutscene_policy.reason();") < cls.index("cutscene_policy.reject(") < cls.index("cutscene_policy.reason()!=before")
    assert "game_xr->scene_hold=cutscene_hold;game_xr->scene_id=cutscene_count.load();" in block
    assert block.index("game_xr->scene_hold=cutscene_hold;") < block.index("game_xr->present(completed_left.Get(),")
    # the pair readiness and textures are decided before the classification; the hold comes after it
    assert block.index("if(pair_ready) {completed_left=mono_path?paired_backbuffer:paired_left;completed_right=paired_backbuffer;}") < block.index("bool cutscene_immersive=false")
    assert block.index("record_rendered_view(slot_eye,") < block.index("bool cutscene_immersive=false")
    assert "bool cutscene_paired_path() noexcept {return xr_requested && paired_requested && eye_boundary_requested && native_eye_capture_requested && mono_view_state.load()!=1;}" in probe
    call = block[block.index("game_xr->present(completed_left.Get(),completed_right.Get(),"):]
    call = call[:call.index(";")]
    assert "gameplay,authored_camera || cutscene_immersive,game_logic_mask()==4," in call and call.endswith("game_logic_mask()==32,cutscene_immersive)"), call
    # the raw native buttons (skip) still pass: the controls block forwards the XR input whatever the cutscene; its pointer is GameXr's
    controls = probe[probe.index("if(controller_requested) {\n                                CpuSpan controls_span{controls_cpu_ns};"):]
    controls = controls[:controls.index("const auto chord=menu_chord.update(")]
    assert "auto input=game_xr->controller_input;" in controls and "cutscene" not in controls
    assert "native_menu.publish(game_xr->native_menu_input,width.load(),height.load());" in controls
    # --- the knobs, live, and the status section
    # Ruling 10b: the present thread only requests a re-read (500 ms with the cinematic bit, 5 s otherwise); a worker touches the files
    knobs = fn(probe, "void refresh_cutscene_knobs(UINT64 now,bool cinematic) {")
    assert "now-cutscene_knob_tick<500u) return;" in knobs and "if(cinematic && !cutscene_knob_cinematic) cutscene_knob_tick=0;" in knobs
    assert knobs.index("if(!cinematic) return;") < knobs.index("std::thread(")  # Ruling 15: no worker in gameplay
    assert "if(cutscene_knob_reading.exchange(true)) return;" in knobs and ").detach();" in knobs
    assert "fs::exists" not in knobs and "ifstream" not in knobs
    reader = fn(probe, "void read_cutscene_knobs(const fs::path& build) noexcept {")
    assert 'cutscene_knob_off=fs::exists(build/L"no-vr-cutscenes",ignored);' in reader and 'std::ifstream knob(build/L"cutscene-mode");' in reader
    assert "cutscene_mode_override=parsed?static_cast<int>(*parsed):-1;" in reader and "d2vr::parse_cutscene_mode(text)" in reader
    assert probe.count("read_cutscene_knobs(") == 3  # the definition, the worker, StartProbe
    status = fn(probe, "std::string cutscene_status_json() {")
    for field in ("mode", "immersive_frames", "theater_frames", "last_fallback", "cutscenes"):
        assert f'\\"{field}\\":' in status, field
    assert '<<",\\"cutscene\\":"<<cutscene_status_json()<<\'}\';return o.str();}()' in probe  # inside scripted_view
    assert "cutscene_policy=d2vr::CutscenePolicy{};cutscene_gates_allowed=false;" in probe  # StartProbe
    # --- GameXr::present (docs/native-authored-scene-presentation.md section 5)
    present = xr[xr.index("    void present(ID3D11Texture2D* left,ID3D11Texture2D* right,float hfov,float vfov,"):]
    present = present[:present.index("\n    }\n")]
    assert "ID3D11Texture2D* desktop=nullptr,std::array<UINT,2> desktop_view={},bool cinematic=false,bool authored_scene_render=false) {" in present
    lines = {
        "asr": "authored_scene_render=authored_scene_render && authored_camera && !gameplay;",
        "scene": "const bool scene_render=gameplay || authored_scene_render;",
        "world": "const bool world_scene=scene_render || world_ui_kind_!=0;",
        "authored": "authored_camera=scene_render && authored_camera;",
        "pointer": "&& !scene_render && !cinematic && !settings_.open() && continuity_.usable() && state==XR_SESSION_STATE_FOCUSED && controller_input.active) {",
        "trigger": "controller_input.right_trigger=menu_trigger_gate_.update(raw_right_trigger,!scene_render || settings_.open() || settings_consumed,raw_input_active);",
        "tracked": "(gates.tracked_ok=!world_scene || (!tracked_gameplay_required_ && !authored_scene_render) || tracked_pair_()) &&",
        "menu_reset": "if(scene_render && !settings_.open()) menu_panel_.reset();",
        "summon": "if(menu_grip && !menu_grip_was_ && (!scene_render || settings_.open()) && controller_input.right_grip<.5f) {",
    }
    for key, line in lines.items():
        assert present.count(line) == 1, key
    order = [present.index(lines[k]) for k in ("asr", "scene", "world", "authored")]
    assert order == sorted(order) and present.index(lines["world"]) < present.index("XrEventDataBuffer event{}")
    # raw gameplay keeps the settings, calibration and Y-hold semantics; overlays (HUD, wrist, vignette, art) stay off for an authored view
    for raw in ("settings_.update(settings_input,gameplay,state==XR_SESSION_STATE_FOCUSED,pointer,settings_available,palm_fit_dt_);",
                "const bool hold_gameplay=gameplay && !authored_camera && continuity_.usable() && views_valid_ &&",
                "!gameplay && settings_available && settings_enabled_?YHoldMenu::Screen::pause:YHoldMenu::Screen::unavailable;",
                "if(world_scene && !authored_camera) {", "if(gameplay && !authored_camera && tracked_pair_()) {",
                "if(settings_requested.exchange(false) && !gameplay) {settings_.open_from_menu();}"):
        assert raw in present, raw
    assert "bool rendered_pair_ready() const {return tracked_pair_();}" in xr
    # Task 6: an authored scene (a G1 ease, a VR comfort cutscene) submits only the comfort ring over its projection
    assert "} else if(scene_render && authored_camera && (vignette_panel=vignette_layer())) {" in present
    assert "last_authored_camera_layer=last_layer_submitted && authored_camera && !authored_scene_render && !settings_.open();" in present
    # fix round 1: a hold submits black or this scene's own immersive layer, never a projection whose eye chains a quad frame has refilled
    assert "const auto scene_hold_now=scene_hold;const auto hold_scene=scene_id;scene_hold=SceneHold::none;" in present
    loop_end = present.index("if(mirror_rects[0] || mirror_rects[1]) {mirror_->end(desktop);++mirrored_frames;}")
    reset = present.index("last_good_projection_.reset();last_good_scene_=0;")
    quad = present.index("if(!world_scene) {\n                // A quad frame copied")
    assert present.index("checked(xrReleaseSwapchainImage(chains_[eye],&release),\"Release game eye\");") < loop_end < quad < reset  # Ruling 15: quad frames only
    assert present.count("last_good_projection_.reset();") == 1 and present.count("last_good_projection_.capture(") == 1
    capture = present.index("last_good_projection_.capture(")
    assert reset < capture and "last_good_scene_=authored_scene_render?scene_id:0;" in present[capture:capture + 300]
    assert "const bool replay_ok=scene_hold_replays(scene_hold_now==SceneHold::none?0u:scene_hold_now==SceneHold::black?1u:2u,hold_scene,last_good_scene_);" in present
    assert "replay_ok && last_good_projection_.usable(chains_,now_ms,pose_epoch())" in present

    # --- the boolean routing, evaluated: immersive cutscene, theater/pending cutscene, pause, gameplay
    def route(gameplay: bool, authored: bool, asr: bool, cinematic: bool, settings_open=False, world_ui=0, required=True, pair=True):
        asr = cpp_bool(lines["asr"].split("=", 1)[1].rstrip(";"), {"authored_scene_render": asr, "authored_camera": authored, "gameplay": gameplay})
        scene = cpp_bool("gameplay || authored_scene_render", {"gameplay": gameplay, "authored_scene_render": asr})
        world = cpp_bool("scene_render || world_ui_kind_!=0", {"scene_render": scene, "world_ui_kind_": world_ui})
        pointer = cpp_bool("!scene_render && !cinematic && !settings_.open()", {"scene_render": scene, "cinematic": cinematic, "settings_.open()": settings_open})
        menu_trigger = cpp_bool("!scene_render || settings_.open()", {"scene_render": scene, "settings_.open()": settings_open})
        tracked_ok = cpp_bool("!world_scene || (!tracked_gameplay_required_ && !authored_scene_render) || tracked_pair_()",
                              {"world_scene": world, "tracked_gameplay_required_": required, "authored_scene_render": asr, "tracked_pair_()": pair})
        return {"projection": world, "pointer": pointer, "menu_trigger": menu_trigger, "tracked_ok": tracked_ok, "asr": asr}
    immersive = route(False, True, True, True)
    assert immersive == {"projection": True, "pointer": False, "menu_trigger": False, "tracked_ok": True, "asr": True}
    assert not route(False, True, True, True, pair=False)["tracked_ok"]                 # no tracked pair: no scene, even without the gameplay requirement
    assert not route(False, True, True, True, required=False, pair=False)["tracked_ok"]
    assert not route(False, False, True, True)["asr"] and not route(False, False, True, True)["projection"]  # never without the authored camera
    theater = route(False, False, False, True)
    assert not theater["projection"] and not theater["pointer"] and theater["menu_trigger"]  # the TV quad, no pointer in a cutscene
    pause = route(False, False, False, False)
    assert not pause["projection"] and pause["pointer"] and pause["menu_trigger"]             # back to pause: the menu quad and its pointer
    for authored in (False, True):  # gameplay is unchanged by the new argument (it is never an authored scene render)
        assert route(True, authored, False, False) == {**route(True, authored, True, False), "asr": False}
        assert route(True, authored, False, False)["projection"] and not route(True, authored, False, False)["pointer"]

    # --- the hold across mask sequences (a model of the pinned GameXr rules above): every frame that fills the eye chains drops the last
    # projection and only its own projection is kept, tagged with its scene; a black hold never replays, a replay hold only its own scene's
    def replays(frames):
        """frames: (kind, scene, hold) with kind projection|asr|quad|held. Returns, per held frame, what it showed."""
        tag, valid, shown = 0, False, []
        for kind, scene, hold in frames:
            if kind == "dropped":  # Ruling 15: a scene frame whose projection the guard dropped keeps the previous projection and its tag
                continue
            if kind in ("projection", "asr", "quad"):
                valid, tag = kind != "quad", scene if kind == "asr" else 0
            else:
                ok = hold == "none" or (hold == "replay" and scene != 0 and tag == scene)
                shown.append(("replay", tag) if ok and valid else ("black", 0))
        return shown
    # 32 (immersive) -> 36 (quad) -> 32 pending: black, not the old stereo layer the quad has overwritten
    assert replays([("asr", 1, None), ("quad", 0, None), ("held", 1, "black"), ("asr", 1, None)]) == [("black", 0)]
    # 2 -> 34 (quad) -> 32 pending: black
    assert replays([("projection", 0, None), ("quad", 0, None), ("held", 1, "black")]) == [("black", 0)]
    # a short load 0 -> 32768 -> 32: the loading quads leave nothing to replay
    assert replays([("quad", 0, None), ("quad", 0, None), ("held", 1, "black")]) == [("black", 0)]
    # Ruling 9: a tolerated miss right after this cutscene's immersive frame re-shows that layer; twice in a row still that layer
    assert replays([("asr", 2, None), ("held", 2, "replay"), ("held", 2, "replay")]) == [("replay", 2), ("replay", 2)]
    # Ruling 15: a gameplay frame whose projection was dropped (a non-finite pose) replays the previous stereo projection, not black;
    # a dropped cutscene frame keeps its scene's tag; a quad in between still blocks every replay
    assert replays([("projection", 0, None), ("dropped", 0, None), ("held", 0, "none")]) == [("replay", 0)]
    assert replays([("asr", 2, None), ("dropped", 2, None), ("held", 2, "replay")]) == [("replay", 2)]
    assert replays([("projection", 0, None), ("quad", 0, None), ("dropped", 0, None), ("held", 0, "none")]) == [("black", 0)]
    # a replay hold never shows gameplay's projection or another cutscene's layer
    assert replays([("projection", 0, None), ("held", 2, "replay")]) == [("black", 0)]
    assert replays([("asr", 1, None), ("held", 2, "replay")]) == [("black", 0)]
    # theater -> pause 4 (quad) -> retry 32 pending: black
    assert replays([("quad", 0, None), ("quad", 0, None), ("held", 3, "black")]) == [("black", 0)]


if __name__ == "__main__":
    root = Path(__file__).resolve().parents[1]
    check((root / "src/dxgi_probe.cpp").read_text(encoding="utf-8"), (root / "src/game_xr.hpp").read_text(encoding="utf-8"))
    print("cutscene_routing_check ok")
