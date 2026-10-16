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
    assert "const bool right_physical=grip_intent_out[1].fist || native_grab.holding(1);" in source
    assert "if(right_physical) input.right_grip=right_native;" in source
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
    )
    for index, mutation in enumerate(mutations):
        assert mutation != source, f"Mutation {index} did not reach its production target"
        try:
            check(mutation)
        except (AssertionError, ValueError):
            continue
        raise AssertionError(f"Unsafe production mutation {index} was accepted")
    print("PASS physical/native right grip split, reserved-owner isolation, and negative source mutations")
