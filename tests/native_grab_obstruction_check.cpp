#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../src/native_interaction_aim.hpp"
#include <cassert>
#include <map>
#include <iostream>
namespace {
constexpr std::uintptr_t fixture_base=0x140000000,fixture_game=0x2000000,fixture_player=0x2100000,fixture_camera=0x2200000,
    fixture_body=0x2300000,fixture_controller=0x2400000,fixture_physics=0x2500000,fixture_delegate=0x2600000,
    fixture_entity=0x2700000,fixture_component=0x2800000,fixture_entries=0x2900000,fixture_world=0x3000000,fixture_pool=0x3100000,fixture_model=0x3200000;
std::map<std::uintptr_t,unsigned char> fixture_bytes;
template<class T>void put(std::uintptr_t at,const T& value) {
    const auto* source=reinterpret_cast<const unsigned char*>(&value);
    for(std::size_t i=0;i<sizeof(value);++i)fixture_bytes[at+i]=source[i];
}
bool read(std::uintptr_t at,void* out,std::size_t size) {
    for(std::size_t i=0;i<size;++i) {const auto it=fixture_bytes.find(at+i);if(it==fixture_bytes.end())return false;static_cast<unsigned char*>(out)[i]=it->second;}
    return true;
}
UINT logic() {return 2;}
void fixture() {
    using namespace d2vr;fixture_bytes.clear();
    put(fixture_base+0x39a5290,fixture_game);put(fixture_game,fixture_base+0x1c940a8);put(fixture_game+0x3130b0,fixture_player);put(fixture_player,fixture_base+0x1c5e428);
    put(fixture_camera+0x25150,std::array<std::byte,0x200>{});
    for(auto [at,table]:std::array<std::pair<std::uintptr_t,std::uintptr_t>,9>{{
        {fixture_camera,0x1c72218},{fixture_camera+0x20,0x1c722f8},{fixture_body,0x1c5a8d8},{fixture_body+0x20,0x1c5a9b0},
        {fixture_controller,0x1c72918},{fixture_physics,0x1c73268},{fixture_physics+0x20,0x1c73340},{fixture_physics+0x30,0x1c73378},{fixture_delegate+0x160,0x1b02768}}})put(at,fixture_base+table);
    for(auto at:{fixture_camera,fixture_body,fixture_controller,fixture_physics}) {put(at+0x10,fixture_player);put(at+0x18,rig::Identity{5,10});}
    put(fixture_camera+0x25158,fixture_controller);put(fixture_camera+0x25160,fixture_physics);put(fixture_camera+0x25170,fixture_body);
    put(fixture_physics+0x48,fixture_delegate);put(fixture_delegate+0x168,fixture_physics);put(fixture_delegate+0x120,fixture_player+0x300);put(fixture_delegate+0x1e0,fixture_delegate);
    put(fixture_delegate+0xd0,0.f);put(fixture_delegate+0xbc,0.f);put(fixture_delegate+0x40,0.f);
    put(fixture_controller+0xd0,std::array<float,2>{});put(fixture_player+0x30c,std::array<float,9>{1,0,0,0,1,0,0,0,1});
    put(fixture_game+0x3130b8,fixture_entity);put(fixture_entity,fixture_base+0x1c886a8);put(fixture_entity+0xa8,std::uint32_t{0x10001});
    put(fixture_entity+0x300,std::array<float,3>{.1f,0,0});put(fixture_entity+0x268,rig::List{fixture_entries,1,1<<8});put(fixture_entries,fixture_component);
    put(fixture_entity+0xf0,fixture_model);put(fixture_model,fixture_base+0x1b0dd88);put(fixture_component,fixture_base+0x1c745c8);
    put(fixture_component+0x10,fixture_entity);put(fixture_component+0x18,rig::Identity{7,10});put(fixture_component+0xe8,fixture_base+0x1c74798);
    constexpr std::uint32_t handle=0x1000002;put(fixture_component+0xf8,handle);put(fixture_component+0xc8,std::uintptr_t{});put(fixture_component+0xd2,static_cast<unsigned char>(0));
    put(fixture_game+0x60410,fixture_world);put(fixture_world+0x28,fixture_pool);put(fixture_world+0x30,std::uint32_t{3});
    rig::Transform transform{};transform.f[0]=transform.f[5]=transform.f[10]=transform.f[15]=1;transform.f[12]=.1f;
    const auto record=fixture_pool+2*0xb0;put(record,transform);put(record+0x44,std::uint32_t{130});put(record+0x40,std::uint32_t{1});put(record+0x70,handle);put(record+0x74,handle);
}
}
namespace {unsigned fixture_motors{};bool fixture_dispatcher{};
void __fastcall fixture_velocity(void*,UINT,const float*,const float*,int) {assert(fixture_dispatcher);++fixture_motors;}
}
namespace d2vr {
struct NativeGrabCheck {
    static void setup(NativeGrab& grab) {grab.active_=true;grab.base_=fixture_base;grab.read_=read;grab.logic_=logic;grab.velocity_=fixture_velocity;}
    static void consume(NativeGrab& grab,UINT64 now) {std::lock_guard<std::mutex> lock(grab.mutex_);fixture_dispatcher=true;grab.update_lane(0,fixture_world,now);fixture_dispatcher=false;}
};
}

namespace {
constexpr std::uintptr_t fixture_sensor=0x3300000;
unsigned fixture_stages{},fixture_queries{},fixture_change{};bool fixture_blocked{};
d2vr::NativeGrab* fixture_grab{};d2vr::NativePlayerCamera fixture_owner{};d2vr::WorldTrackedPose fixture_hand{};
void __fastcall stage_original(void*) {++fixture_stages;}
std::uintptr_t __fastcall obstruction_original(void* sensor,void* entity,const void* origin,const void* axis,const void* point,unsigned range,unsigned char* output) {
    using namespace d2vr;++fixture_queries;
    assert(fixture_stages==fixture_queries); // original stage completes before owned service
    assert(reinterpret_cast<std::uintptr_t>(sensor)==fixture_sensor && reinterpret_cast<std::uintptr_t>(entity)==fixture_entity && range==0);
    const auto* o=static_cast<const float*>(origin);const auto* a=static_cast<const float*>(axis);const auto* p=static_cast<const float*>(point);
    assert(o[0]==0 && o[1]==0 && o[2]==0 && a[0]==1 && a[4]==1 && a[8]==1 && std::abs(p[0]-.1f)<1e-6);
    assert(fixture_grab->diagnostics()); // callback is outside grab lock
    if(fixture_change==1) fixture_grab->publish(0,fixture_owner,fixture_hand,.9f,9,GetTickCount64(),true,false,GetTickCount64());
    if(fixture_change==2) put(fixture_component+0x18,rig::Identity{8,10});
    if(fixture_change==3) put(fixture_sensor+0x18,rig::Identity{9,10});
    if(fixture_change==4) put(fixture_camera+0x25200,1);
    if(fixture_change==5) fixture_grab->filter_native_grip(0,0,1,GetTickCount64(),true,true,false);
    if(fixture_change==6) {rig::Transform moved{};moved.f[0]=moved.f[5]=moved.f[10]=moved.f[15]=1;moved.f[12]=.2f;put(fixture_pool+2*0xb0,moved);}
    if(fixture_change==7) fixture_grab->filter_native_grip(0,.9f,1,GetTickCount64(),true,false,false);
    *output=1;return fixture_blocked?1:0; // continuation does not override blocked result
}
}
namespace d2vr {
struct NativeInteractionRangeFixture {
    static void setup(NativeInteractionAim& aim,NativeGrab& grab) {
        aim.base_=fixture_base;aim.read_=read;aim.logic_=logic;aim.active_=true;aim.stages_[0]=stage_original;
        aim.physical_occlusion_=obstruction_original;aim.attach_physical_grab(&grab);NativeInteractionAim::instance_=&aim;
    }
    static void run() {NativeInteractionAim::stage<0>(reinterpret_cast<void*>(fixture_sensor));}
};
}
int main() {
    using namespace d2vr;
    for(unsigned mode=0;mode<9;++mode) {
        fixture();fixture_stages=fixture_queries=fixture_motors=0;fixture_change=mode<2?0:mode-1;fixture_blocked=mode==1;
        put(fixture_sensor,fixture_base+0x1b403b8);put(fixture_sensor+0x18,rig::Identity{7,10});put(fixture_sensor+0x10,fixture_player);
        put(fixture_sensor+0x40,fixture_camera);put(fixture_sensor+0x38,static_cast<unsigned char>(1));put(fixture_sensor+0x48,std::uint64_t{0x1234});
        fixture_owner=*native_player_camera(read,fixture_base,fixture_camera);fixture_hand={ {},{{{1,0,0},{0,1,0},{0,0,1}}}};
        NativeGrab grab;NativeGrabCheck::setup(grab);fixture_grab=&grab;
        NativeInteractionAim aim;NativeInteractionRangeFixture::setup(aim,grab);
        const auto tick=GetTickCount64();grab.publish(0,fixture_owner,fixture_hand,0,1,tick,true,false,tick);
        grab.filter_native_grip(0,0,1,tick,true,true,false);
        const auto press=tick+1; // use real clock for completion freshness
        while(GetTickCount64()<press)Sleep(1);
        grab.publish(0,fixture_owner,fixture_hand,.9f,1,press,true,false,press);
        assert(grab.filter_native_grip(0,.9f,1,press,true,true,false)==0);
        NativeGrabCheck::consume(grab,press);assert(!grab.holding(0));
        const auto pending=grab.diagnostics();assert(pending && (*pending)[0].gate==NativeGrab::Gate::obstruction_pending);
        NativeInteractionRangeFixture::run();assert(fixture_queries==1 && fixture_stages==1);
        assert(grab.holding(0)==(mode==0));assert(fixture_motors==0);
        if(mode==0) {
            // A clearance commits pure hold state before eye B replaces A.
            auto moved=fixture_hand;moved.position.x=.02;
            for(unsigned frame=0;frame<3;++frame) {
                Sleep(20);const auto next=GetTickCount64(); // > GetTickCount64's ~16 ms step, so each frame gets its own motor tick
                grab.publish(0,fixture_owner,moved,.9f,1,next,true,false,next);
                grab.filter_native_grip(0,.9f,1,next,true,true,false);
                NativeGrabCheck::consume(grab,next);assert(grab.holding(0));
            }
            assert(fixture_queries==1 && fixture_motors==3);
        } else {NativeGrabCheck::consume(grab,GetTickCount64());assert(!grab.holding(0));}
        if(mode==1) {const auto d=grab.diagnostics();assert(d && (*d)[0].gate==NativeGrab::Gate::obstruction_blocked);}
        if(mode==0) assert((*grab.diagnostics())[0].held->entity==fixture_entity); // no Use registration exists
    }
    // A verdict from an old source cannot authorize a replaced publication.
    fixture();NativeGrab grab;NativeGrabCheck::setup(grab);grab.require_obstruction();
    const auto owner=*native_player_camera(read,fixture_base,fixture_camera);const auto tick=GetTickCount64();
    grab.publish(0,owner,fixture_hand,0,1,tick,true,false,tick);grab.filter_native_grip(0,0,1,tick,true,true,false);
    grab.publish(0,owner,fixture_hand,.9f,1,tick,true,false,tick);grab.filter_native_grip(0,.9f,1,tick,true,true,false);
    grab.publish(0,owner,fixture_hand,.9f,1,tick+1,true,false,tick+1);
    unsigned calls{};grab.service_obstruction(owner,tick+1,[&](const NativeGrab::ObstructionRequest&)->std::optional<bool>{++calls;return true;});
    assert(!calls && !grab.holding(0));
    Sleep(2);const auto fresh=GetTickCount64();
    grab.publish(0,owner,fixture_hand,.9f,1,fresh,true,false,fresh);grab.filter_native_grip(0,.9f,1,fresh,true,true,false);
    grab.service_obstruction(owner,fresh,[&](const NativeGrab::ObstructionRequest&)->std::optional<bool>{++calls;return {};});
    assert(calls==1 && !grab.holding(0) && (*grab.diagnostics())[0].role==RightGripOwner::Role::ready);
    {   // G3 (plan Task 8): the LOS is asked at hover, not at press. A hover verdict (<= 100 ms old, the hand within 2 cm, the same body, its contact
        // within 1 cm) binds on the press tick with no new request; a stale one is asked again (today's path); hover requests are rate limited.
        const auto hover_then_press=[&](UINT64 wait_ms,double move_m,unsigned& calls)->bool {
            fixture();NativeGrab g;NativeGrabCheck::setup(g);g.require_obstruction();
            const auto o=*native_player_camera(read,fixture_base,fixture_camera);const auto t0=GetTickCount64();
            g.publish(0,o,fixture_hand,0,1,t0,true,false,t0);g.filter_native_grip(0,0,1,t0,true,true,false);
            assert(g.focus_point(0,t0));   // the hover cue picks the prop and asks for its LOS
            assert(g.hover_los().requests==1);
            g.service_obstruction(o,GetTickCount64(),[&](const NativeGrab::ObstructionRequest& r)->std::optional<bool>{assert(r.hover);++calls;return true;});
            assert(calls==1);
            const auto until=GetTickCount64()+wait_ms;while(GetTickCount64()<until) Sleep(1);
            auto moved=fixture_hand;moved.position.x=move_m;const auto t1=GetTickCount64();
            g.publish(0,o,moved,0,1,t1,true,false,t1);g.filter_native_grip(0,0,1,t1,true,true,false);
            g.publish(0,o,moved,.9f,1,t1,true,false,t1);const float passed=g.filter_native_grip(0,.9f,1,t1,true,true,false);
            assert(passed==0);NativeGrabCheck::consume(g,t1);
            const auto d=g.diagnostics();assert(d);
            if(g.holding(0)) {assert(g.hover_los().used==1 && g.hover_los().requests==1 && (*d)[0].gate==NativeGrab::Gate::physical_reserved);return true;}   // bound on the press tick, no new request
            assert((*d)[0].gate==NativeGrab::Gate::obstruction_pending && g.hover_los().used==0);   // asked again at the press
            unsigned press_calls=0;
            g.service_obstruction(o,GetTickCount64(),[&](const NativeGrab::ObstructionRequest& r)->std::optional<bool>{assert(!r.hover);++press_calls;return true;});
            assert(press_calls==1 && g.holding(0));
            return false;
        };
        unsigned hover_calls=0;
        assert(hover_then_press(20,0,hover_calls));          // hover_verdict_used_at_press
        hover_calls=0;assert(hover_then_press(0,.019,hover_calls)); // 1.9 cm: still used
        hover_calls=0;assert(!hover_then_press(120,0,hover_calls)); // stale_hover_verdict_requeried: older than 100 ms
        hover_calls=0;assert(!hover_then_press(0,.021,hover_calls)); // the hand moved 2.1 cm
        {   // hover_requests_rate_limited: a hover cue every frame asks at most once per 100 ms
            fixture();NativeGrab g;NativeGrabCheck::setup(g);g.require_obstruction();
            const auto o=*native_player_camera(read,fixture_base,fixture_camera);
            const auto start=GetTickCount64();UINT64 t=start;
            while((t=GetTickCount64())<start+250) {g.publish(0,o,fixture_hand,0,1,t,true,false,t);g.filter_native_grip(0,0,1,t,true,true,false);g.focus_point(0,t);Sleep(5);}
            const auto asked=g.hover_los().requests;assert(asked>=2 && asked<=3);
            NativeGrab off;NativeGrabCheck::setup(off);   // obstruction not required: no hover requests at all
            off.publish(0,o,fixture_hand,0,1,t,true,false,t);off.filter_native_grip(0,0,1,t,true,true,false);off.focus_point(0,t);
            assert(off.hover_los().requests==0 && !off.hover_los().required && g.hover_los().required);
        }
    }
    {   // buffered_catch_through_los_binds: a fist squeezed with nothing in reach (pending), the prop then reaches the gate: the catch needs a LOS query
        // (no hover verdict) and must still bind when the query clears it (acceptance sim T124714: caught a corpse, LOS cleared, never bound)
        fixture();NativeGrab g;NativeGrabCheck::setup(g);g.require_obstruction();
        const auto o=*native_player_camera(read,fixture_base,fixture_camera);
        auto pose=fixture_hand;pose.position.x=-.6;
        UINT64 t=GetTickCount64();g.publish(0,o,pose,0,1,t,true,false,t);g.filter_native_grip(0,0,1,t,true,true,false);NativeGrabCheck::consume(g,t);
        Sleep(2);t=GetTickCount64();g.publish(0,o,pose,.9f,1,t,true,false,t);
        assert(g.filter_native_grip(0,.9f,1,t,true,true,false)==0);NativeGrabCheck::consume(g,t);
        assert((*g.diagnostics())[0].gate==NativeGrab::Gate::buffer_pending);
        for(double x=-.5;x<=0.01;x+=.1) {   // the hand arrives
            Sleep(10);t=GetTickCount64();pose.position.x=x;g.publish(0,o,pose,.9f,1,t,true,false,t);g.filter_native_grip(0,.9f,1,t,true,true,false);NativeGrabCheck::consume(g,t);
            if((*g.diagnostics())[0].gate==NativeGrab::Gate::obstruction_pending) break;
        }
        assert((*g.diagnostics())[0].gate==NativeGrab::Gate::obstruction_pending && g.buffer_counts(0).first==1);
        unsigned asked=0;g.service_obstruction(o,GetTickCount64(),[&](const NativeGrab::ObstructionRequest&)->std::optional<bool>{++asked;return true;});
        assert(asked==1 && g.holding(0) && g.physical_presses()==1);
    }
    {   // grip_jump_after_decision_binds: the squeeze reaches the lane at once (0 -> 0.9, a grip-alone grab's decision) and the dispatcher samples it
        // before the filter decides; the LOS then clears and the press must still bind (sim T125301: the grip-alone corpse grab never bound)
        fixture();NativeGrab g;NativeGrabCheck::setup(g);g.require_obstruction();
        const auto o=*native_player_camera(read,fixture_base,fixture_camera);
        UINT64 t=GetTickCount64();g.publish(0,o,fixture_hand,0,1,t,true,false,t);g.filter_native_grip(0,0,1,t,true,true,false);NativeGrabCheck::consume(g,t);
        Sleep(2);t=GetTickCount64();g.publish(0,o,fixture_hand,.9f,1,t,true,false,t);NativeGrabCheck::consume(g,t);   // sampled before the decision
        g.filter_native_grip(0,.9f,1,t,true,true,false,false,true);
        assert((*g.diagnostics())[0].gate==NativeGrab::Gate::obstruction_pending);
        g.service_obstruction(o,GetTickCount64(),[&](const NativeGrab::ObstructionRequest&)->std::optional<bool>{return true;});
        assert(g.holding(0));
    }
    std::cout<<"PASS: grip jump after the decision binds; buffered catch through a LOS query binds; hover LOS verdicts (used at the press, stale or moved re-asked, rate limited); owned original stage0/wrapper ordering, no mailbox lock during native call, arbitrary dynamic prop clear/blocked, A-clear-B-dispatch motor ownership, epoch/body/sensor/authored/motion/focus/release invalidation and replaced source rejection\n";
}
