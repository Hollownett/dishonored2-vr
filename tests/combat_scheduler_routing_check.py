from pathlib import Path
import re


def check(source: str) -> None:
    assert '#include "combat_target_memory.hpp"' in source
    assert "static d2vr::CombatTargetMemory combat_target_memory;" in source
    begin = source.index("d2vr::CombatTarget current_target{};")
    end = source.index("combat_decision=vr_combat.update(cf);", begin)
    routing = source[begin:end]
    required = (
        "if(!cf.shadow_walk && (takedown.kill || takedown.drop))",
        "takedown.kill?d2vr::CombatTarget::Kind::ground:d2vr::CombatTarget::Kind::drop",
        "takedown.kill?takedown.kill_spawn:takedown.drop_spawn",
        "const auto fresh_takedown=native_takedowns.poll(control_reader,image_base,f.now_ms);",
        "current_target.kind==d2vr::CombatTarget::Kind::ground?",
        "(fresh_takedown.kill?fresh_takedown.kill_spawn:0u):(fresh_takedown.drop?fresh_takedown.drop_spawn:0u)",
        "d2vr::takedown_target(control_reader,image_base,current_target.spawn)",
        "target_valid=current_target && fresh_target==current_target.spawn && target && choke_owner &&",
        "d2vr::rig::value<std::uintptr_t>(control_reader,image_base+0x39a5290)==choke_owner->game",
        "native_takedowns.machine(control_reader)==choke_owner->machine",
        "d2vr::native_player_state_lease(control_reader,image_base,control_machine)==choke_owner",
        "if(!target_valid)cf.allowed=false;",
        "combat_target_memory.update(choke_owner,game_xr->pose_epoch(),current_target,f.now_ms,",
        "target_valid && !cf.shadow_walk && cf.allowed && cf.tracked && cf.equipped",
        "if(target_memory.context_changed) {vr_combat.reset();takedown_kill_tick=0;takedown_kill_held=false;}",
        "takedown_kill_held=target_memory.remembered.operator bool();",
        "cf.current_target=target_valid?current_target:d2vr::CombatTarget{};",
        "cf.remembered_target=target_memory.remembered;",
    )
    for fragment in required:
        assert fragment in routing, f"Missing scheduler target/owner routing: {fragment}"
    assert "stealth_rt_" not in source, "Removed external stealth timers must not return"
    assert not re.search(r"\b(?:cd|combat_decision)\.rt\s*=(?!=)", source), "Only VrCombat may schedule its RT"
    assert "if(!vr_combat_enabled && right_free && raw.right_trigger>.55f && sword_grip.held()) input.right_grip=1;" in source
    assert "const bool want=vr_combat_enabled?combat_decision.rb:gesture.block_held;" in source
    assert "cf.explicit_block=logic==2 && !interaction.consume_trigger[1] && !(physical_grab_requested && native_grab.holding(1)) && raw.right_trigger>.55f && sword_grip.held();" in source
    assert "cf.grip=0;" in source
    assert "cf.allowed=f.allowed && !input.lean_suspended && !choke_route.rb && !authored_camera;" in source
    assert 'reserve_nonlethal_grip(choke_route.rb);' in source
    assert 'reserve_nonlethal_grip(choke_route.consume_use);' not in source
    assert 'if(choke_route.rb || !sword_combat_owned)' in source
    assert 'sword_owns || talk_targets[1] || choke_route.consume_use' in source
    ownership = source[source.index('gesture=gestures.update(f);'):source.index('d2vr::CombatFrame cf{};')]
    assert ownership.index('sword_combat_owned=sword_out && f.sword_equipped;') < ownership.index('if(vr_combat_enabled)')
    fallback = source[source.index('const auto swing=sword_swing.update('):source.index('swing_until_tick=swing.until;')]
    assert '!choke_route.rb && !authored_camera && sword_combat_owned,' in fallback
    assert "bool combat_live=false,sword_combat_owned=false;" in source
    assert "sword_out=sword_out && sword_grip.held();" in source
    assert "f.sword_equipped=!(physical_grab_requested && native_grab.holding(1));" in source
    assert "sword_combat_owned=sword_out && f.sword_equipped;" in source
    assert "cf.equipped=(sword_out || carrying) && f.sword_equipped && (sword_combat_owned || !grip_guard.swing_blocked(1,f.now_ms));" in source
    assert "if(grab_guard[0]) input.left_trigger=0;" in source
    assert "if(grab_guard[1] && !sword_combat_owned) input.right_trigger=0;" in source
    assert source.index("sword_combat_owned=sword_out && f.sword_equipped;") < source.index("combat_decision=vr_combat.update(cf);")
    assert source.index("if(logic==2 && cd.rt)") < source.index("if(grab_guard[1] && !sword_combat_owned)") < source.index("if(logic==2 && choke_route.consume_use)")
    inactive = source[source.index("if(!combat_live) {"):source.index("if(cd.swing!=d2vr::SwingKind::none)")]
    assert "vr_combat.reset();cd=vr_combat.update({});" in inactive
    assert "combat_target_memory.update({},0,{},GetTickCount64(),false);" in inactive
    assert "takedown_kill_tick=0;takedown_kill_held=false;" in inactive
    assert "{std::lock_guard<std::mutex> lock(combat_policy_mutex);combat_policy_observation=cd;}" in source
    status_begin = source.index("static std::string combat_policy_status_json()")
    status_end = source.index("static std::atomic<unsigned> wheel_dbg_w", status_begin)
    status = source[status_begin:status_end]
    assert "{std::lock_guard<std::mutex> lock(combat_policy_mutex);value=combat_policy_observation;}" in status
    assert '\\"policy_rt_edges\\":' in status
    assert "combat_policy_status_json()" in source[status_end:]
    assert 'combat_log("attack_policy",cd.attack_target.spawn' in source


if __name__ == "__main__":
    root = Path(__file__).resolve().parents[1]
    source = (root / "src" / "dxgi_probe.cpp").read_text(encoding="utf-8-sig")
    check(source)
    mutations = (
        ("bool combat_live=false,sword_combat_owned=false;", "bool combat_live=false,sword_combat_owned=true;"),
        ("sword_out=sword_out && sword_grip.held();", "sword_out=sword_out;"),
        ("f.sword_equipped=!(physical_grab_requested && native_grab.holding(1));", "f.sword_equipped=true;"),
        ("sword_combat_owned=sword_out && f.sword_equipped;", "sword_combat_owned=true;"),
        ("sword_combat_owned=sword_out && f.sword_equipped;", "sword_combat_owned=sword_out;"),
        ("(sword_combat_owned || !grip_guard.swing_blocked(1,f.now_ms))", "!grip_guard.swing_blocked(1,f.now_ms)"),
        ("(sword_out || carrying) && f.sword_equipped && (sword_combat_owned", "(sword_out || carrying) && (sword_combat_owned"),
        ("if(grab_guard[1] && !sword_combat_owned)", "if(grab_guard[1])"),
        ("if(grab_guard[0]) input.left_trigger=0;", "if(false) input.left_trigger=0;"),
        ("takedown.kill?takedown.kill_spawn:takedown.drop_spawn", "takedown.spawn"),
        ("fresh_target==current_target.spawn", "true"),
        (" && target && choke_owner &&", " && choke_owner &&"),
        ("==choke_owner->game", "!=choke_owner->game"),
        ("==choke_owner->machine", "!=choke_owner->machine"),
        ("d2vr::native_player_state_lease(control_reader,image_base,control_machine)==choke_owner", "d2vr::native_player_state_lease(control_reader,image_base,control_machine)!=choke_owner"),
        ("if(!target_valid)cf.allowed=false;", "if(!target_valid)cf.allowed=true;"),
        ("combat_target_memory.update(choke_owner,game_xr->pose_epoch()", "combat_target_memory.update(choke_owner,1"),
        ("target_valid && !cf.shadow_walk && cf.allowed && cf.tracked && cf.equipped", "target_valid && cf.allowed"),
        ("if(target_memory.context_changed) {vr_combat.reset();", "if(false) {vr_combat.reset();"),
        ("cf.remembered_target=target_memory.remembered;", "cf.remembered_target={};"),
        ("if(!vr_combat_enabled && right_free && raw.right_trigger", "if(right_free && raw.right_trigger"),
        ("combat_target_memory.update({},0,{},GetTickCount64(),false);", "combat_target_memory.update({},0,{},GetTickCount64(),true);"),
        ("combat_policy_observation=cd;", "combat_policy_observation={};"),
        ("value=combat_policy_observation;", "value={};"),
        ("if(logic==2 && cd.rt)", "if(logic==2 && (cd.rt=true))"),
        ('reserve_nonlethal_grip(choke_route.rb);', 'reserve_nonlethal_grip(choke_route.consume_use);'),
        ('if(choke_route.rb || !sword_combat_owned)', 'if(choke_route.consume_use)'),
        ("static d2vr::CombatTargetMemory combat_target_memory;", "static d2vr::CombatTargetMemory combat_target_memory; static int stealth_rt_until;"),
    )
    for index, (needle, replacement) in enumerate(mutations):
        assert needle in source, f"Mutation {index} missed its production target"
        mutation = source.replace(needle, replacement)
        try:
            check(mutation)
        except (AssertionError, ValueError):
            continue
        raise AssertionError(f"Unsafe source-routing mutation {index} was accepted")
    print(f"PASS combat scheduler source routing and {len(mutations)} negative mutations (not runtime/native execution proof)")
