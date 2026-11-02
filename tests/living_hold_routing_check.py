"""G6 (hands plan Task 14): source routing of the living-NPC hold. A living hold never reaches a velocity write (it has no `held` body), chokes and
takedowns reserve their NPC before the grip filters run, a hard pull yanks the held NPC by name (r.target), build/no-living-hold turns the living
candidates off, and the pull is gated by the Task 13 verdict. Mutations of each rule must fail."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]


def between(text, start, end):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


def check(grab: str, probe: str, hands: str) -> None:
    lane = between(grab, "void update_lane(unsigned side,std::uintptr_t world,UINT64 now) {", "    void update(std::uintptr_t dispatcher) {")
    living = lane.index("if(lane.living) {living_tick(lane,now,physical_usable);return;}")
    assert living < lane.index("write(lane,side"), "the living branch returns before any velocity write"
    tick = between(grab, "void living_tick(Lane& lane,UINT64 now,bool usable) {", "\n    }\n")
    for banned in ("write(", "write_velocity(", "lane.held=", "velocity_("):
        assert banned not in tick, f"a living hold must not {banned}"
    start = between(lane, "if(lane.living_candidate && physical_usable", "if(lane.held && !physical_usable")
    assert "lane.held=" not in start and "write(" not in start and "!npc_reserved(c.entity_id,now)" in start
    assert "lane.living.emplace(*bone,p.hand.position,p.epoch,living_pull_enabled_)" in start
    # the living candidates: grip alone only, never a reserved NPC, and only while build/no-living-hold is absent
    decide = between(grab, "float filter_native_grip(", "    // Called only by the owned stage-0 sensor scope.")
    assert re.search(r"!candidate && grip_alone && living_enabled_ && lane\.living_focus && lane\.score && lane\.score->kind==PalmKind::living &&\s*"
                     r"lane\.score->distance<=gate && !npc_reserved\(lane\.living_focus->entity_id,now\)", decide), "the grip-alone living candidate rule"
    assert "if(living_enabled_) living_candidates(side,now,living_targets,living_bones);" in grab
    cand = between(grab, "void living_candidates(unsigned side,UINT64 now,", "    // One tick of a living hold")
    assert "if(npc_reserved(n.id,now) ||" in cand
    assert 'living_enabled_=!std::filesystem::exists(root/L"build"/L"no-living-hold",ec);' in grab
    assert "t.alive=bone && bone->entity_id==grasp.entity_id && usable && !npc_reserved(grasp.entity_id,now);" in tick
    # chokes and takedowns are reserved before the grip filters run
    feed = probe.index("native_grab.reserve_npc(1,choke_offer?choke_offer->spawn:control_active_choke?control_active_choke:takedown_npc,GetTickCount64());")
    assert probe.index("const std::uint32_t takedown_npc=control_takedown.any()?control_takedown.spawn:0u;") < feed
    assert feed < probe.index("native_grab.reserve_npc(0,takedown_npc,GetTickCount64());") < probe.index("const float right_native=native_grab.filter_native_grip(1,")
    # the hard pull yanks HIM by name
    assert "r.op=d2vr::NativeWeaponHands::Op::yank;r.target=living_yank->entity;" in probe
    assert "const auto living_yank=physical_grab_requested?native_grab.take_hard_pull(h):std::nullopt;" in probe
    yank = between(hands, "template<class Read> static bool yank(", "    // G6 (Task 14): a living hold starts with today's shove")
    assert "if(r.target && rig::pointer(r.target) && rig::typed(read,base,r.target,NativeMelee::npc_vtable_rva) && living(read,r.target)) best=r.target;" in yank
    # the start is a shove (no START_RAGDOLL, no velocity); the pull is the verdict-gated teleport
    start_op = between(hands, "template<class Read> static bool living_start(", "    // A pull step")
    assert "s.op=Op::shove" in start_op and "ragdoll(" not in start_op and "velocity" not in start_op.replace("never a velocity", "")
    pull = between(hands, "template<class Read> static bool living_pull(", "public:")
    assert "if(!living_pull_enabled ||" in pull and "teleport<Read>(base,r.target,r.pos,*axis);" in pull
    assert "native_grab.living_pull(d2vr::NativeWeaponHands::living_pull_enabled);" in probe
    # a reserved guard is no physical body either (a fist at his collar), and the choke hand at his neck never yanks (headset 2026-11-01)
    focus = between(grab, "std::optional<NativeRigidBody> focus_body(unsigned side,UINT64 now,bool refresh) {", "        // Re-rank every publication")
    assert focus.index("if(npc_reserved(*id,now)) return {};") < focus.index("return corpse(*entity);")
    assert "const bool choke=choke_offer.has_value() || control_active_choke!=0 || control_takedown.choke;" in probe
    assert "&& !(choke && h==choke_side && hand_at_neck[h]);" in probe
    # the seam to Track F's GripPoseBlend (ruling H6)
    assert "d2vr::grip_pose_blend_start(h,living_start->radius,now_y);" in probe


if __name__ == "__main__":
    grab = (ROOT / "src" / "native_grab.hpp").read_text(encoding="utf-8")
    probe = (ROOT / "src" / "dxgi_probe.cpp").read_text(encoding="utf-8-sig")
    hands = (ROOT / "src" / "native_weapon_hands.hpp").read_text(encoding="utf-8")
    check(grab, probe, hands)
    mutations = (
        ("grab", " && !npc_reserved(lane.living_focus->entity_id,now)) {lane.living_candidate=lane.living_focus;eligible=true;}", ") {lane.living_candidate=lane.living_focus;eligible=true;}"),
        ("grab", "if(npc_reserved(n.id,now) ||", "if(false ||"),
        ("probe", "native_grab.reserve_npc(1,choke_offer?choke_offer->spawn:control_active_choke?control_active_choke:takedown_npc,GetTickCount64());", ""),
        ("probe", "r.target=living_yank->entity;", ""),
        ("grab", "if(npc_reserved(*id,now)) return {};", ""),
        ("probe", " || control_active_choke!=0 || control_takedown.choke;", ";"),
        ("probe", "&& !(choke && h==choke_side && hand_at_neck[h]);", ";"),
        ("grab", "if(lane.living) {living_tick(lane,now,physical_usable);return;}", "if(lane.living) {living_tick(lane,now,physical_usable);}"),
        ("grab", 'living_enabled_=!std::filesystem::exists(root/L"build"/L"no-living-hold",ec);', "living_enabled_=true;"),
        ("hands", "if(!living_pull_enabled ||", "if("),
        ("grab", "        lane.living_tick=now;\n", "        lane.living_tick=now;write(lane,0,NativeRigidBody{},GrabMotor{});\n"),
    )
    for index, (which, old, new) in enumerate(mutations):
        sources = {"grab": grab, "probe": probe, "hands": hands}
        assert old in sources[which], f"mutation {index} does not reach its target"
        sources[which] = sources[which].replace(old, new, 1)
        try:
            check(sources["grab"], sources["probe"], sources["hands"])
        except (AssertionError, ValueError):
            continue
        raise AssertionError(f"mutation {index} was accepted")
    print("PASS living hold routing: no velocity write, choke/takedown reservation before the filters, yank by name, no-living-hold knob, verdict-gated pull")
