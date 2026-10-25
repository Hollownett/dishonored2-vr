from pathlib import Path
import re


def check(source: str) -> None:
    capture = re.search(
        r"float\s+physical_left_grip\s*=\s*input\.left_grip\s*;\s*"
        r"float\s+physical_right_grip\s*=\s*input\.right_grip\s*;", source
    )
    assert capture, "Both physical analogues must be captured together before native suppression"
    suppression = source.index("if(logic==2) input.right_grip=0;", capture.end())
    for owner in ("reach", "sheath"):
        pattern = (
            rf"if\s*\(\s*{owner}\.consume\[1\]\s*\)\s*\{{\s*"
            r"input\.right_grip\s*=\s*0\s*;\s*physical_right_grip\s*=\s*0\s*;\s*\}"
        )
        match = re.search(pattern, source)
        assert match and capture.end() < match.start() < suppression, f"{owner} must consume both right channels"
    call = re.search(r"const float right_native\s*=\s*native_grab\.filter_native_grip\((.*?)\);", source, re.S)
    assert call and call.start() > suppression, "Physical filter must follow retained native RB suppression"
    arguments = call.group(1)
    assert re.match(r"\s*1\s*,\s*right_physical\s*\?\s*physical_right_grip\s*:\s*0\.f\s*,", arguments), "Physical filter must not read composed native RB"
    for guard in ("grab_use", "sword_owns", "choke_route.consume_use", "grip_use[1] && right_interaction.interact"):
        assert guard in arguments, f"Missing right reservation: {guard}"
    # G3 (plan Task 6): a grip-alone grab (GripIntent::Kind::grab) is a physical press like the fist, on both hands, and its squeeze is published.
    assert "const bool right_physical=grip_intent_out[1].fist || grip_intent_out[1].grab || native_grab.holding(1);" in source
    assert "(void)native_grab.filter_native_grip(0,grip_intent_out[0].fist || grip_intent_out[0].grab || native_grab.holding(0)?physical_left_grip:0.f," in source
    assert "grab_hands=(grip_intent_out[0].grab?1u:0u)|(grip_intent_out[1].grab?2u:0u);" in source
    publish = re.search(r"native_grab\.publish\(side,\*body,hand,\(\(fist_hands\.load\(\)\|grab_hands\.load\(\)\)&\(1u<<side\)\) \|\| native_grab\.holding\(side\)\?squeeze:0\.f,", source)
    assert publish, "the squeeze is published for a fist or a grip-alone grab"
    # the grip-alone flag reaches each filter (the 0.12 m gate), and build/no-grip-alone-grab turns the one-list decision off
    assert re.search(r"filter_native_grip\(1,.*?grip_intent_out\[1\]\.grab && !grip_intent_out\[1\]\.fist,\s*sword_grip\.held\(\) \|\| right_sword_drawn\);", source, re.S)   # I2: the sword hand skips the fist buffer
    assert re.search(r"filter_native_grip\(0,.*?grip_intent_out\[0\]\.grab && !grip_intent_out\[0\]\.fist\);", source, re.S)
    # fix round 1: fast native actions never wait the 150 ms, and the palm pick used by grip-alone is at most 100 ms old
    assert "in.fast_native=left_gun_model.load()!=0 || gun_owns ||" in source and "d2vr::length(d2vr::Vec3{(*hands[0])[0],(*hands[0])[1],(*hands[0])[2]}-*hip_xr_position)<=.17);}" in source
    assert "in.physical=native_grab.focus_score(h,now_gi,100);" in source
    assert "const bool one_list=physical_grab_requested && !no_grip_alone_grab;" in source
    assert 'no_grip_alone_grab=fs::exists(root/L"build"/L"no-grip-alone-grab");' in source
    assert "grip_choice[h]=d2vr::decide_grip_alone(in,grip_tuning,grip_tuning.hover_radius_m);" in source
    # v613 item 4: the torso-hold handoff to the native carry, its knob and the drop press
    assert 'legacy_corpse_carry=fs::exists(root/L"build"/L"legacy-corpse-carry");' in source
    assert "d2vr::carry_handoff(ci)" in source and "native_grab.corpse_hold(h,now_gi)" in source
    assert "carry_handoff_now[0],carrying_now[0]" in source and "carry_handoff_now[1],carrying_now[1]" in source
    assert "if(now_l<corpse_drop_press_until) input.x=true;" in source
    assert "in.choke_owns=h==1 && (choke_offer.has_value() || control_active_choke!=0);" in source
    assert "in.takedown_offered=control_takedown.any();" in source
    gate = source.index("const bool right_fist_rb=d2vr::fist_may_press_native_rb(native_grab.holding(1),right_carrying,sword_grip.held(),right_sword_drawn);")
    pin = source.index("if(right_physical) input.right_grip=right_fist_rb?right_native:0.f;")
    assert gate < pin, "the fist RB gate must be computed before the physical right grip is written"
    assert "bool right_carrying{},right_sword_drawn{};" in source
    reads = source[source.index("bool right_carrying{},right_sword_drawn{};"):gate]
    assert "std::lock_guard<std::mutex> hand_lock(hand_state_mutex);" in reads, "both hand reads share one hand_state_mutex lock"
    assert "right_carrying=hand_carrying_bits!=0 || hand_holding_object;" in reads
    assert "right_sword_drawn=(hand_weapon_bits&2u)!=0;" in reads
    assert "!sword_owns || choke_offer.has_value() || control_active_choke!=0" in source
    assert "cf.explicit_block=logic==2 && !interaction.consume_trigger[1] && !(physical_grab_requested && native_grab.holding(1)) && raw.right_trigger>.55f && sword_grip.held();" in source
    assert "if(!vr_combat_enabled && right_free && raw.right_trigger>.55f && sword_grip.held()) input.right_grip=1;" in source
    assert "const bool want=vr_combat_enabled?combat_decision.rb:gesture.block_held;" in source


if __name__ == "__main__":
    root = Path(__file__).resolve().parents[1]
    source = (root / "src" / "dxgi_probe.cpp").read_text(encoding="utf-8-sig")
    check(source)
    mutations = (
        source.replace("right_physical?physical_right_grip:0.f", "right_physical?input.right_grip:0.f"),
        source.replace(" || choke_route.consume_use", ""),
        source.replace("physical_right_grip=0;", "physical_right_grip=1;", 1),
        source.replace("if(logic==2) input.right_grip=0;", "if(logic==2) input.right_grip=1;", 1),
        source.replace("if(!vr_combat_enabled && right_free && raw.right_trigger", "if(right_free && raw.right_trigger", 1),
        source.replace("cf.explicit_block=logic==2", "cf.explicit_block=false && logic==2", 1),
        source.replace("right_fist_rb?right_native:0.f", "right_native", 1),
        source.replace("grip_intent_out[1].fist || grip_intent_out[1].grab || native_grab.holding(1)", "grip_intent_out[1].fist || native_grab.holding(1)", 1),
        source.replace("grip_intent_out[0].fist || grip_intent_out[0].grab || native_grab.holding(0)?physical_left_grip", "grip_intent_out[0].fist || native_grab.holding(0)?physical_left_grip", 1),
        source.replace("(fist_hands.load()|grab_hands.load())&(1u<<side)", "fist_hands.load()&(1u<<side)", 1),
        source.replace("grab_hands=(grip_intent_out[0].grab?1u:0u)|(grip_intent_out[1].grab?2u:0u);", "grab_hands=0;", 1),
        source.replace("in.choke_owns=h==1 && (choke_offer.has_value() || control_active_choke!=0);", "in.choke_owns=false;", 1),
    )
    for index, mutation in enumerate(mutations):
        assert mutation != source, f"Mutation {index} did not reach its production target"
        try:
            check(mutation)
        except (AssertionError, ValueError):
            continue
        raise AssertionError(f"Unsafe production mutation {index} was accepted")
    print("PASS physical/native right grip split, reserved-owner isolation, and negative source mutations")

