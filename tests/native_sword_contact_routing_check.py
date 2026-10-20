from pathlib import Path
import re
import subprocess
import tempfile


def contact_condition(source: str) -> str:
    begin = source.index("const unsigned why=")
    return source[begin:source.index(":0u;", begin) + 4]


def check(source: str, consumer: str) -> None:
    condition = contact_condition(source)
    assert not re.search(r"trigger|raw\.|takedown|button", condition)
    assert "!drawn?1u:!contact_hand?2u:contact_hand->epoch!=game_xr->pose_epoch()?3u:" in condition
    assert "(physical_grab_requested && native_grab.holding(1))?4u:" in condition
    assert "too_slow?5u:" in condition
    assert source.index("classify_contact_speed(") < source.index("const unsigned why=")
    assert "stealth_strike(speed,rear_side,stealth_speed)" in source and "stealth_strike(speed,rear_side,d2vr::SwordHitGate::hit_speed)" not in source
    assert 'L"stealth-speed"' in source and "stealth_speed_from_knob(" in source
    assert "const bool too_slow=speed_class==d2vr::ContactSpeedClass::too_slow || (speed_class==d2vr::ContactSpeedClass::stealth_only && !rear_side);" in source
    assert "d2vr::arms_native_attack(owned_contact->stealth_strike,owned_contact->stealth_only,owned_contact->native_offer)) contact_attack_tick=now;" in source
    assert "if(owned_contact->stealth_strike) contact_attack_tick=now;" not in source
    assert "slow_reject_latch.first(contact->entity,now)" in source
    assert "resolve_stealth_act(" in consumer
    assert "const auto act=resolve_stealth_act(raw_act,c.stealth_only);" in consumer
    drop = consumer.index("if(contact.stealth_only) {++stealth_slow_unheld_drops_;continue;}")
    assert drop < consumer.index("NativeMelee::apply(self->base_,self->read_,contact.equipment.sword,contact.hit,&observation)")
    assert "held_payload->stealth_only=false" in consumer
    begin = source.index("const unsigned why=")
    end = source.index("// The tip striking", begin)
    producer = source[begin:end]
    assert not re.search(r"trigger|raw\.|takedown_kill_held", producer)
    assert "capture_sword_contact(contact_reader,image_base,*contact_machine,hit,contact_hand->epoch,now)" in producer
    assert "owned_contact && d2vr::NativeInteractionAim::request_sword_contact(*owned_contact)" in producer
    assert producer.index("request_sword_contact") < producer.index("sword_hit_gate.commit(contact->entity,now);")
    assert "request_melee_hit" not in producer
    assert "publish_sword_hand(sword_controls_live && sword_native_held && sword_grip.held(),game_xr->pose_epoch(),sword_hand_tick)" in source
    assert "const bool sword_controls_live=logic==2 && input.active && controls_focus && !input.menu_modifier && !x_wheel.wheel() &&" in source
    assert "!input.lean_suspended && !authored_camera && (game_xr->grip_tracked_hands&2u)!=0 &&" in source
    assert "sword_pose_tick && sword_hand_tick>=sword_pose_tick && sword_hand_tick-sword_pose_tick<=100 &&" in source
    assert "(hand_weapon_bits&2u)!=0 && !hand_holding_object && !hand_carrying_bits;" in source
    assert "publish_sword_pose_epoch(game_xr->pose_epoch())" in source
    for fragment in (
        "contacts.swap(sword_contacts_)", "!melee_batch_current(melee_revision)",
        "!sword_contact_current(self->read_,contact,hand->epoch,now)",
        "hand->epoch!=sword_pose_epoch_.load(std::memory_order_acquire)",
        "camera->player!=contact.owner.player", "camera->authored_view()",
        "contact.equipment.sword,contact.hit,&observation", "sword_contacts_.clear()",
        "std::lock_guard<std::mutex> guard(trace_mutex_);return sword_last_contact_;",
        "sword_last_contact_={contact.hit.tick,final_now,contact.pose_epoch,contact.victim_spawn",
        "publish_sword_pose_epoch(0);publish_sword_hand(false,0,0)",
        "final_now-contact.hit.tick>100 || !melee_batch_current(melee_revision)",
        "final_hand->epoch!=contact.pose_epoch",
    ):
        assert fragment in consumer, fragment


if __name__ == "__main__":
    root = Path(__file__).resolve().parents[1]
    source = (root / "src/dxgi_probe.cpp").read_text(encoding="utf-8-sig")
    consumer = (root / "src/native_interaction_aim.hpp").read_text(encoding="utf-8-sig")
    check(source, consumer)
    mutations = (
        ("const unsigned why=!drawn?1u:", "const unsigned why=raw.right_trigger>.55f?9u:!drawn?1u:"),
        ("const unsigned why=!drawn?1u:", "const unsigned why=!takedown_kill_held.load()?9u:!drawn?1u:"),
        ("contact_hand->epoch!=game_xr->pose_epoch()?3u:", "false?3u:"),
        ("(physical_grab_requested && native_grab.holding(1))?4u:too_slow?5u:", "false?4u:too_slow?5u:"),
        ("request_sword_contact(*owned_contact)", "request_melee_hit(hit)"),
    )
    for needle, replacement in mutations:
        assert needle in source
        try:
            check(source.replace(needle, replacement), consumer)
        except (AssertionError, ValueError):
            continue
        raise AssertionError(f"Contact producer mutation accepted: {needle}")
    for needle in (
        "!melee_batch_current(melee_revision)",
        "!sword_contact_current(self->read_,contact,hand->epoch,now)",
        "hand->epoch!=sword_pose_epoch_.load(std::memory_order_acquire)",
        "camera->player!=contact.owner.player",
    ):
        assert needle in consumer
        try:
            check(source, consumer.replace(needle, "false"))
        except AssertionError:
            continue
        raise AssertionError(f"Contact consumer mutation accepted: {needle}")
    controls_begin = source.index("const bool sword_controls_live=") + len("const bool sword_controls_live=")
    controls_end = source.index(";", controls_begin)
    controls_condition = source[controls_begin:controls_end]
    native_begin = source.index("sword_native_held=hand_state_tick") + len("sword_native_held=")
    native_condition = source[native_begin:source.index(";", native_begin)]
    fixture = r'''
#include <array>
#include <cstdint>
#include <optional>
#include <cassert>
#include "sword_contact_policy.hpp"
#include <cstdio>
struct Raw {float left_trigger{},right_trigger{},left_grip{},right_grip{};unsigned buttons{};};
struct Hand {std::uint64_t epoch{9};};
struct Xr {unsigned grip_tracked_hands{2};std::uint64_t pose_epoch() const {return 9;}};
struct Grab {bool held{};bool holding(unsigned) const {return held;}};
struct Contact {std::uintptr_t entity{0x10000};};
struct Input {bool active{true},menu_modifier{},lean_suspended{};};
struct Wheel {bool opened{};bool wheel() const {return opened;}};
int main() {
    Xr xr;auto* game_xr=&xr;Grab native_grab;Contact observed;auto* contact=&observed;
    bool drawn=true,physical_grab_requested=true;
    std::optional<Hand> contact_hand=Hand{};
    double speed=2;bool too_slow=false;std::uint64_t now=1000;unsigned side=1;std::array<double,2> tip_speed{};d2vr::SwordHitGate sword_hit_gate;
    const auto gate=[&] {@CONDITION@ return why==0u;};
    for(unsigned buttons=0;buttons<64;++buttons) {
        Raw raw{float(buttons&1),float((buttons>>1)&1),float((buttons>>2)&1),float((buttons>>3)&1),buttons};
        assert(raw.buttons==buttons && gate());
    }
    drawn=false;assert(!gate());drawn=true;native_grab.held=true;assert(!gate());native_grab.held=false;
    contact_hand.reset();assert(!gate());contact_hand=Hand{10};assert(!gate());contact_hand=Hand{};
    too_slow=true;assert(!gate());too_slow=false;sword_hit_gate.commit(contact->entity,900);assert(!gate());
    sword_hit_gate.commit(contact->entity,500);assert(gate());
    unsigned logic=2;bool controls_focus=true,authored_camera=false;Input input;Wheel x_wheel;
    std::uint64_t sword_hand_tick=1000,sword_pose_tick=1000;
    const auto controls_gate=[&] {return @CONTROLS@;};assert(controls_gate());
    input.active=false;assert(!controls_gate());input.active=true;
    controls_focus=false;assert(!controls_gate());controls_focus=true;
    input.menu_modifier=true;assert(!controls_gate());input.menu_modifier=false;
    input.lean_suspended=true;assert(!controls_gate());input.lean_suspended=false;
    authored_camera=true;assert(!controls_gate());authored_camera=false;
    x_wheel.opened=true;assert(!controls_gate());x_wheel.opened=false;
    xr.grip_tracked_hands=0;assert(!controls_gate());xr.grip_tracked_hands=2;
    sword_pose_tick=899;assert(!controls_gate());sword_pose_tick=1001;assert(!controls_gate());sword_pose_tick=1000;
    logic=4;assert(!controls_gate());logic=2;assert(controls_gate());
    std::uint64_t hand_state_tick=1000;unsigned hand_weapon_bits=2,hand_carrying_bits=0;bool hand_holding_object=false;
    const auto native_gate=[&] {return @NATIVE@;};assert(native_gate());
    hand_weapon_bits=0;assert(!native_gate());hand_weapon_bits=2;
    hand_holding_object=true;assert(!native_gate());hand_holding_object=false;
    hand_carrying_bits=2;assert(!native_gate());hand_carrying_bits=0;
    hand_state_tick=699;assert(!native_gate());hand_state_tick=700;assert(native_gate());hand_state_tick=1001;assert(!native_gate());hand_state_tick=1000;
    assert(native_gate() && controls_gate());
    std::puts("PASS extracted real impact condition for64 button combinations and ownership/prop/epoch/speed/debounce gates plus9 routing negatives");
}
'''.replace("@CONDITION@", contact_condition(source)).replace("@CONTROLS@", controls_condition).replace("@NATIVE@", native_condition)
    with tempfile.TemporaryDirectory(prefix="d2vr-contact-routing-") as directory:
        temporary = Path(directory)
        translation_unit = temporary / "contact_routing.cpp"
        executable = temporary / "contact_routing.exe"
        translation_unit.write_text(fixture, encoding="utf-8")
        subprocess.run(["cl", "/nologo", "/std:c++17", "/EHsc", "/W4", "/WX", "/O2", "/MT", f"/I{root / 'src'}", str(translation_unit),
                        f"/Fo:{temporary / 'contact_routing.obj'}", f"/Fe:{executable}"], check=True, cwd=temporary)
        subprocess.run([str(executable)], check=True, cwd=temporary)
