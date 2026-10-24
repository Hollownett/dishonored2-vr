import json
import re
from pathlib import Path


def block(probe: str) -> str:
    start = probe.index("const auto wants_legacy=")
    end = probe.index("hands.has_finger_curls=true;", start)
    return probe[start:end]


def check(probe: str, build: str, features: str) -> None:
    b = block(probe)
    # wants_from_touch_curl: the wanted array finger_contact gets comes from FingerTouchCurl::update (empty and held hands), finger_contact stays last
    assert "finger_touch_curl.update(side," in b and "finger_touch_params" in b
    assert "touch_curl=touch_side && " in b and "touch_out.mapping==d2vr::rig::CurlMapping::touch" in b
    assert "const auto wants=touch_curl?touch_wants:wants_legacy;" in b
    assert "d2vr::rig::finger_contact(*geometry,palm,shapes.data(),count,fdt,finger_ik[side],holding?d2vr::rig::held_curl:1.f,finger_solve_budget,&wants)" in b
    assert b.index("finger_touch_curl.update(") < b.index("d2vr::rig::finger_contact(")
    # held hands: held=true in the update, fingers 1-4 capped at held_curl, thumb not
    assert "finger_touch_curl.update(side,touch_in,fdt,holding,finger_touch_params)" in b
    assert "for(unsigned f=1;f<5;++f) touch_wants[f]=std::min(touch_wants[f],d2vr::rig::held_curl);" in b
    # the visible hand's per-finger turns and the legacy arm's scalars take the touch wants too
    assert "touch_curl?d2vr::rig::finger_turns(*geometry,contact,wants):d2vr::rig::finger_turns(*geometry,contact,hands.curls[side],hands.index_curls[side],holding)" in b
    assert "touch_curl?d2vr::rig::grip_curls(contact,touch_wants):d2vr::rig::grip_curls(contact," in b
    # weapon_hand_keeps_native_fist: a weapon hand or a native-animated hand gets no touch curl (and drops its state)
    assert "const bool touch_side=!legacy_finger_curl && game_xr->touch_status().bound && !(hands.native_hands>>side&1u) && !((fk_weapon_bits>>side)&1u);" in b
    assert "{hand_weapon_bits" not in b and "fk_weapon_bits=hand_weapon_bits;" in probe
    assert "else {finger_touch_curl.reset(side);finger_touch_live[side&1u]=false;}" in b
    # availability is the session property, not the per-frame controller_input (sample_input zeroes that on unfocused / failed frames)
    assert "controller_input.touch_bound" not in b
    # the first touch frame after a reset starts from the legacy wants, and the status shows the curls actually requested
    assert "if(!finger_touch_live[side&1u]) finger_touch_curl.seed(side,wants_legacy);finger_touch_live[side&1u]=true;" in b
    assert "finger_touch_status.curl[side]=touch_side?touch_out.curl:wants_legacy;" in b
    # legacy_knob_restores_finger_wants: the legacy wants are the old finger_wants call, verbatim
    assert "const auto wants_legacy=d2vr::rig::finger_wants(holding,hands.curls[side],hands.index_curls[side]);" in b
    assert 'L"legacy-finger-curl"' in probe and "legacy_finger_curl=fs::exists(" in probe
    assert 'L"finger-touch.cfg"' in probe and "FingerTouchParams::parse(" in probe
    assert 'finger-touch.txt' in probe
    # curl_never_feeds_intent: the curl state is touched only in the finger block, the knob read and the status writer
    names = ("finger_touch_curl", "touch_wants", "touch_out", "finger_touch_status")
    allowed_funcs = ("start_finger_touch_knobs", "finger_touch_status_json")
    glob_start = probe.index("// Bonelab touch-sensor finger curl (plan C")
    glob_end = probe.index("return o.str();\n}\n", probe.index("static std::string finger_touch_status_json(")) 
    blk_start = probe.index(b)
    offset = 0
    for number, line in enumerate(probe.splitlines(True), 1):
        here, offset = offset, offset + len(line)
        if line.lstrip().startswith(("#include", "//")) or not any(re.search(rf"\b{n}\b", line) for n in names):
            continue
        in_block = blk_start <= here < blk_start + len(b)
        in_globals = glob_start <= here <= glob_end
        in_status = "finger_touch_status_json(" in line
        declared = "std::array<float,5> touch_wants{};bool touch_curl=false;" in line
        assert in_block or in_globals or in_status or declared, (number, line[:120])
    # hands.finger_curls / touch_curl readers are drawn-side only. Allowed: the finger block itself (in_block above), the arm pose in arm_tracking.hpp
    # (validation + drawn fingers), and the solid-hand push collider proxies (native_hand_contact publish: drawn-hand physics, which legacy curl
    # already fed). Nothing else, and in particular no grab / Use / attack / aim / gesture / Blink code, may read them.
    allowed_curl_readers = {
        "src/dxgi_probe.cpp": ("hands.finger_curls[side]=touch_curl?", "solid_hands.publish(side,can_push,hands.grips[side],palm_grips[side],hands.has_finger_curls?hands.finger_curls[side]:"),
        "src/arm_tracking.hpp": ("frame.finger_curls", "frame.has_finger_curls"),
    }
    src = Path(__file__).resolve().parents[1] / "src"
    for path in sorted(src.glob("*.[ch]pp")):
        rel = f"src/{path.name}"
        text = path.read_text(encoding="utf-8", errors="replace")
        for number, line in enumerate(text.splitlines(), 1):
            if line.lstrip().startswith(("#include", "//")) or not re.search(r"\bfinger_curls\b|\btouch_curl\b", line):
                continue
            if rel == "src/dxgi_probe.cpp" and ("touch_curl" in line and "finger_curls" not in line):
                continue  # the finger block's own touch_curl uses are pinned by the names scan above
            if rel == "src/arm_tracking.hpp" and "finger_curls{}" in line:
                continue  # the declaration
            ok = any(token in line for token in allowed_curl_readers.get(rel, ()))
            assert ok or (rel == "src/dxgi_probe.cpp" and "hands.has_finger_curls=" in line), (rel, number, line.strip()[:120])
    for fn in allowed_funcs:
        assert f"static void {fn}(" in probe or f"static std::string {fn}(" in probe, fn
    # status_block_present
    for key in ("fingers", "profile", "mapping", "touch_available", "touch_silent", "raw", "trigger_touch", "thumb_touch", "curl", "floor_active", "action_create_failed", "touch_bound"):
        assert f'\\"{key}\\":' in probe, key
    assert '"oculus/touch_controller"' in probe
    assert "<< \",\\\"fingers\\\":\" << finger_touch_status_json(" in probe
    assert "python tests\\finger_touch_routing_check.py" in build
    # feature_row_present
    rows = [r for r in json.loads(features)["features"] if r.get("file") == "legacy-finger-curl"]
    assert len(rows) == 1
    row = rows[0]
    assert row["kind"] == "disable" and row["default"] is True and row["label"] == "Fingers follow the controller's touch sensors"


if __name__ == "__main__":
    root = Path(__file__).resolve().parents[1]
    check((root / "src/dxgi_probe.cpp").read_text(encoding="utf-8"),
          (root / "tools/build-msvc.cmd").read_text(encoding="utf-8"),
          (root / "tools/default_features.json").read_text(encoding="utf-8"))
    print("finger_touch_routing_check ok")
