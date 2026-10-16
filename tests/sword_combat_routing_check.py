from pathlib import Path
import subprocess
import tempfile
import re

from combat_scheduler_routing_check import check


def statement(source: str, prefix: str) -> str:
    matches = [line.strip() for line in source.splitlines() if line.strip().startswith(prefix)]
    assert len(matches) == 1, (prefix, len(matches))
    return re.sub(r"\bf\.", "frame.", matches[0].split(" //", 1)[0])


def fixture(source: str) -> str:
    fragments = {
        "sword_hold": statement(source, "sword_out=sword_out && sword_grip.held();"),
        "sword_owner": statement(source, "sword_combat_owned=sword_out && f.sword_equipped;"),
        "equipped": statement(source, "cf.equipped=(sword_out || carrying)"),
        "native_pulse": statement(source, "if(logic==2 && cd.rt)"),
        "left_guard": statement(source, "if(grab_guard[0]) input.left_trigger=0;"),
        "right_guard": statement(source, "if(grab_guard[1]"),
    }
    template = r'''
#include "play_controls.hpp"
#include "vr_controls.hpp"
#include "vr_combat.hpp"
#include <cstdio>
#include <cstdlib>

using ULONGLONG=unsigned long long;
static ULONGLONG fixture_tick{};
static ULONGLONG GetTickCount64() {return fixture_tick;}
static void require(bool condition,const char* message) {
    if(!condition) {std::puts(message);std::exit(1);}
}
struct Routing {
    d2vr::GripTriggerGuard grip_guard;
    d2vr::SwordGrip sword_grip;
    d2vr::VrCombat combat;
    d2vr::CombatFrame cf{};
    struct Frame {bool sword_equipped{true};std::uint64_t now_ms{};} frame;
    struct Input {float left_trigger{},right_trigger{};} input;
    bool sword_bit{true},carrying{},prop{},nonlethal{},allowed{true},tracked{true};
    float physical_trigger{},physical_grip{1};
    unsigned recognized{},pulses{},assists{};
    bool latched{},sword_bypass{};
    ULONGLONG swing_until_tick{};
    Routing() {
        cf.head_pos={0,1.7f,0};cf.head_fwd={0,0,-1};cf.head_right={1,0,0};
        cf.pos={.55f,1.0f,-.2f};cf.aim={0,0,-1};fixture_tick=1000;
    }
    void step(float speed=0) {
        frame.now_ms=++fixture_tick;
        const auto grab_guard=grip_guard.update({0,physical_grip},{0,physical_trigger},{false,prop},frame.now_ms);
        latched=grab_guard[1];
        d2vr::SwordGrip::In grip_input{};
        grip_input.grip=physical_grip;grip_input.gameplay=true;grip_input.drawn=sword_bit;
        grip_input.busy=prop || carrying;grip_input.now=frame.now_ms;
        sword_grip.update(grip_input);
        bool sword_out=sword_bit;
        bool sword_combat_owned=false;
        frame.sword_equipped=!prop;
        @sword_hold@
        @sword_owner@
        sword_bypass=sword_combat_owned;
        @equipped@
        cf.allowed=allowed && !nonlethal;cf.tracked=tracked;cf.now_ms=frame.now_ms;
        cf.trigger=physical_trigger;cf.explicit_block=physical_trigger>.55f && sword_grip.held() && !prop;
        cf.pos.x+=speed*.001f;cf.vel={speed,0,0};
        const auto cd=combat.update(cf);
        input={};
        const unsigned logic=2;
        @native_pulse@
        @left_guard@
        @right_guard@
        if(nonlethal)input.right_trigger=0;
        require(!(input.right_trigger && cd.rb),"native pulse overlapped scheduled block");
        recognized+=cd.swing!=d2vr::SwingKind::none;
        pulses+=input.right_trigger>0;
        assists+=cd.drop_assist;
    }
    void run(unsigned count,float speed=0) {for(unsigned step_index=0;step_index<count;++step_index)step(speed);}
};
int main() {
    for(const bool trigger_held:{false,true}) {
        Routing routing;routing.run(20);
        routing.physical_trigger=trigger_held?1.f:0.f;routing.run(20);
        require(!trigger_held || routing.latched,"raw trigger must actually be consumed by production grab guard");
        routing.run(85,4);routing.run(100);
        require(routing.recognized==1 && routing.pulses>0,"newly gripped native sword must swing before 400ms with either trigger level");
        require(fixture_tick<1400,"regression missed the fresh-grip window");
        if(trigger_held) {
            routing.run(600);require(routing.latched,"raw-trigger consume latch must persist past 400ms");
            routing.recognized=routing.pulses=0;routing.run(85,-4);routing.run(100);
            require(routing.recognized==1 && routing.pulses>0,"latched physical trigger erased later motion pulse");
        }
    }
    {
        Routing routing;routing.run(20);routing.physical_trigger=1;routing.run(800);
        require(!routing.recognized && !routing.pulses,"physical trigger without motion or native drop qualification caused a sword attack");
        routing.physical_trigger=0;routing.run(20);routing.run(85,4);routing.run(100);
        require(routing.recognized==1 && routing.pulses>0,"sword required physical trigger after release");
    }
    {
        Routing routing;routing.run(20);routing.physical_trigger=1;routing.run(20);
        routing.cf.drop=true;
        routing.cf.current_target={d2vr::CombatTarget::Kind::drop,0x12340001};
        routing.run(120);
        require(!routing.recognized && routing.assists==1 && routing.pulses>0,"existing recent-trigger native drop assist was lost");
    }
    for(unsigned gate=0;gate<6;++gate) {
        Routing routing;
        if(gate==0)routing.sword_bit=false;
        if(gate==1)routing.physical_grip=0;
        if(gate==2)routing.prop=true;
        if(gate==3)routing.nonlethal=true;
        if(gate==4)routing.allowed=false;
        if(gate==5)routing.tracked=false;
        routing.run(40);routing.run(85,4);routing.run(100);
        require(!routing.pulses,"sword guard bypass widened an unrelated admission gate");
    }
    {
        Routing routing;routing.sword_bit=false;routing.carrying=true;
        routing.run(40);routing.run(85,4);routing.run(100);
        require(!routing.sword_bypass && !routing.recognized && !routing.pulses,"body carry incorrectly acquired sword guard bypass");
        routing.run(250);
        require(fixture_tick>1400 && !routing.sword_bypass,"body carry must keep the original grab guard timing");
        routing.run(85,-4);routing.run(100);
        require(!routing.sword_bypass && routing.recognized==1 && routing.pulses>0,"ordinary carried-body motion throw was lost after the original grab guard expired");
    }
    for(unsigned side=0;side<2;++side) {
        d2vr::GripTriggerGuard guard;
        std::array<float,2> grips{},triggers{};std::array<bool,2> held{};
        grips[side]=1;held[side]=true;guard.update(grips,triggers,held,1000);
        triggers[side]=1;const auto consumed=guard.update(grips,triggers,held,1100);
        require(consumed[side] && guard.swing_blocked(static_cast<int>(side),5000),"physical object lost its guard");
        require(!consumed[1-side],"object guard leaked to other hand");
    }
    std::puts("PASS extracted production sword admission/composer fragments with real GripTriggerGuard, SwordGrip and VrCombat; not native/headset proof");
}
'''
    for name, fragment in fragments.items():
        template = template.replace(f"@{name}@", fragment)
    assert "@" not in template
    return template


if __name__ == "__main__":
    root = Path(__file__).resolve().parents[1]
    source = (root / "src" / "dxgi_probe.cpp").read_text(encoding="utf-8-sig")
    check(source)
    with tempfile.TemporaryDirectory(prefix="d2vr-sword-routing-") as directory:
        temporary = Path(directory)
        translation_unit = temporary / "sword_routing.cpp"
        executable = temporary / "sword_routing.exe"
        translation_unit.write_text(fixture(source), encoding="utf-8")
        subprocess.run([
            "cl", "/nologo", "/std:c++17", "/utf-8", "/EHsc", "/W4", "/WX", "/O2", "/MT",
            f"/I{root / 'src'}", str(translation_unit), f"/Fo:{temporary / 'sword_routing.obj'}", f"/Fe:{executable}",
        ], check=True, cwd=temporary)
        subprocess.run([str(executable)], check=True, cwd=temporary)
