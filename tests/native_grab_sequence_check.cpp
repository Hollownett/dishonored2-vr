#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../src/native_grab.hpp"
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
// A second loose prop (registry index 2, pool record 1) for the two-hand case.
constexpr std::uintptr_t second_entity=0x3300000,second_component=0x3400000,second_entries=0x3500000,second_model=0x3600000;
void second_prop(std::array<float,3> at) {
    using namespace d2vr;
    put(fixture_game+0x3130b0+2*8,second_entity);put(second_entity,fixture_base+0x1c886a8);put(second_entity+0xa8,std::uint32_t{0x20002});
    put(second_entity+0x300,at);put(second_entity+0x268,rig::List{second_entries,1,1<<8});put(second_entries,second_component);
    put(second_entity+0xf0,second_model);put(second_model,fixture_base+0x1b0dd88);put(second_component,fixture_base+0x1c745c8);
    put(second_component+0x10,second_entity);put(second_component+0x18,rig::Identity{8,10});put(second_component+0xe8,fixture_base+0x1c74798);
    constexpr std::uint32_t handle=0x1000001;put(second_component+0xf8,handle);put(second_component+0xc8,std::uintptr_t{});put(second_component+0xd2,static_cast<unsigned char>(0));
    rig::Transform transform{};transform.f[0]=transform.f[5]=transform.f[10]=transform.f[15]=1;transform.f[12]=at[0];transform.f[13]=at[1];transform.f[14]=at[2];
    const auto record=fixture_pool+1*0xb0;put(record,transform);put(record+0x44,std::uint32_t{130});put(record+0x40,std::uint32_t{2});put(record+0x70,handle);put(record+0x74,handle);
}
// The palm at `palm` facing +x (palm axis +z): the probe dxgi_probe builds from the raw grip and the palm fit.
d2vr::PalmProbe probe_at(d2vr::Vec3 palm,d2vr::Vec3 facing={1,0,0}) {
    d2vr::PalmProbe p;p.palm=palm;p.forward={0,0,1};p.direction=facing;p.valid=true;return p;
}
}
namespace d2vr {
struct NativeGrabCheck {
    static void setup(NativeGrab& grab) {grab.active_=true;grab.base_=fixture_base;grab.read_=read;grab.logic_=logic;}
    static void consume(NativeGrab& grab,UINT64 now,unsigned side=0) {std::lock_guard<std::mutex> lock(grab.mutex_);grab.update_lane(side,fixture_world,now);}
    static void buffer_ms(NativeGrab& grab,double ms) {grab.tuning_.grab_buffer_ms=ms;}
};
}
int main() {
    using namespace d2vr;fixture();auto reader=read;const auto owner=native_player_camera(reader,fixture_base,fixture_camera);assert(owner);
    const WorldTrackedPose hand{{},{{{1,0,0},{0,1,0},{0,0,1}}}};
    NativeGrab grab;NativeGrabCheck::setup(grab);
    // No dispatcher callback repeats the release between filter and squeeze.
    grab.publish(0,*owner,hand,0,1,1000,true,false,1000);
    assert(grab.filter_native_grip(0,0,1,1000,true,true,false)==0);
    grab.publish(0,*owner,hand,.9f,1,1010,true,false,1010);
    assert(grab.filter_native_grip(0,.9f,1,1010,true,true,false)==0);
    NativeGrabCheck::consume(grab,1010);assert(grab.holding(0));
    const auto acquired=grab.diagnostics(1010);assert(acquired && (*acquired)[0].held->entity==fixture_entity);
    NativeGrab returned;NativeGrabCheck::setup(returned);
    for(const UINT64 epoch:{1ull,2ull}) {
        const auto tick=2000+epoch*100;
        returned.publish(0,*owner,hand,.9f,epoch,tick,true,false,tick);
        returned.filter_native_grip(0,.9f,epoch,tick,true,true,false);NativeGrabCheck::consume(returned,tick);
        assert(!returned.holding(0)); // Held grip on initial/focus epoch does not arm either latch.
    }
    returned.publish(0,*owner,hand,0,2,2300,true,false,2300);returned.filter_native_grip(0,0,2,2300,true,true,false);
    returned.publish(0,*owner,hand,.9f,2,2310,true,false,2310);returned.filter_native_grip(0,.9f,2,2310,true,true,false);
    NativeGrabCheck::consume(returned,2310);assert(returned.holding(0));
    {   // G3 (plan Task 5): a fist squeezed 100 ms before the hand reaches the prop binds on arrival (the 150 ms buffer)
        NativeGrab early;NativeGrabCheck::setup(early);
        const auto at=[](double x) {return WorldTrackedPose{{x,0,0},{{{1,0,0},{0,1,0},{0,0,1}}}};};
        early.publish(0,*owner,at(-.8),0,1,3000,true,false,3000,nullptr,0,probe_at({-.8,0,0}));early.filter_native_grip(0,0,1,3000,true,true,false);
        NativeGrabCheck::consume(early,3000);
        early.publish(0,*owner,at(-.8),.9f,1,3010,true,false,3010,nullptr,0,probe_at({-.8,0,0}));
        assert(early.filter_native_grip(0,.9f,1,3010,true,true,false)==0);   // nothing within 0.45 m: pending, nothing to the game
        NativeGrabCheck::consume(early,3010);assert(!early.holding(0));
        {const auto d=early.diagnostics(3010);assert(d && (*d)[0].gate==NativeGrab::Gate::buffer_pending && (*d)[0].role==RightGripOwner::Role::pending);}
        assert(early.native_presses()==0 && early.physical_presses()==0);
        for(UINT64 t=3030;t<=3110;t+=20) {   // the hand arrives at 7 m/s: in the 0.20 m gate at +100 ms (x -0.3), bound on that tick (the
            // fixture has no velocity setter, so the hold is checked on its bind tick only)
            const double x=-.8+static_cast<double>(t-3010)*.0071;
            early.publish(0,*owner,at(x),.9f,1,t,true,false,t,nullptr,0,probe_at({x,0,0}));
            assert(early.filter_native_grip(0,.9f,1,t,true,true,false)==0);NativeGrabCheck::consume(early,t);
            assert(early.holding(0)==(t>=3110));
        }
        assert(early.holding(0) && early.physical_presses()==1 && early.native_presses()==0);
        assert(early.buffer_counts(0).first==1 && early.buffer_counts(0).second==0);
        const auto score=early.focus_score(0,3110);assert(score && score->kind==PalmKind::prop && score->distance<.45);
        assert(early.focus_score(0,3110+90,100) && !early.focus_score(0,3110+110,100) && early.focus_score(0,3110+110));   // fix round 1: grip-alone takes <= 100 ms, the default stays 250
    }
    {   // a press with nothing reached in 150 ms resolves native once, and the squeeze then passes as today
        NativeGrab late;NativeGrabCheck::setup(late);
        const WorldTrackedPose away{{-1,0,0},{{{1,0,0},{0,1,0},{0,0,1}}}};
        late.publish(0,*owner,away,0,1,4000,true,false,4000,nullptr,0,probe_at({-1,0,0}));late.filter_native_grip(0,0,1,4000,true,true,false);
        float passed=0;
        for(UINT64 t=4010;t<=4200;t+=10) {
            late.publish(0,*owner,away,.9f,1,t,true,false,t,nullptr,0,probe_at({-1,0,0}));
            passed=late.filter_native_grip(0,.9f,1,t,true,true,false);NativeGrabCheck::consume(late,t);
            if(t<4160) assert(passed==0 && late.native_presses()==0);
        }
        assert(passed==.9f && late.native_presses()==1 && late.physical_presses()==0 && !late.holding(0));
        assert(late.buffer_counts(0).first==0 && late.buffer_counts(0).second==1);
        {   // I2: the sword hand (no_buffer) decides at the edge: the squeeze passes native on the press frame, no 150 ms wait
            NativeGrab sword;NativeGrabCheck::setup(sword);
            sword.publish(0,*owner,away,0,1,4000,true,false,4000,nullptr,0,probe_at({-1,0,0}));sword.filter_native_grip(0,0,1,4000,true,true,false);
            sword.publish(0,*owner,away,.9f,1,4010,true,false,4010,nullptr,0,probe_at({-1,0,0}));
            assert(sword.filter_native_grip(0,.9f,1,4010,true,true,false,false,false,true)==.9f && sword.native_presses()==1 && sword.buffer_counts(0).first==0);
        }
        NativeGrab off;NativeGrabCheck::setup(off);NativeGrabCheck::buffer_ms(off,0);   // grab_buffer_ms 0: today's single-frame decision
        off.publish(0,*owner,away,0,1,4000,true,false,4000,nullptr,0,probe_at({-1,0,0}));off.filter_native_grip(0,0,1,4000,true,true,false);
        off.publish(0,*owner,away,.9f,1,4010,true,false,4010,nullptr,0,probe_at({-1,0,0}));
        assert(off.filter_native_grip(0,.9f,1,4010,true,true,false)==.9f && off.native_presses()==1);
    }
    {   // two_hands_nearest: hovering reserves nothing; either hand can acquire the nearest object.
        second_prop({.1f,.12f,0});
        NativeGrab both;NativeGrabCheck::setup(both);
        const WorldTrackedPose palm{{0,0,0},{{{1,0,0},{0,1,0},{0,0,1}}}};
        for(UINT64 frame=0;frame<200;++frame) {
            const UINT64 t=5000+frame*11;
            for(unsigned side=0;side<2;++side) both.publish(side,*owner,palm,0,1,t,true,false,t,nullptr,0,probe_at({0,0,0}));
            assert(both.focus_entity(0,t)==fixture_entity);
            assert(both.focus_entity(1,t)==fixture_entity);
        }
        const auto left=both.focus_score(0,5000+199*11),right=both.focus_score(1,5000+199*11);
        assert(left && right && left->id==0x10001 && right->id==0x10001);
    }
    std::cout<<"PASS: G3 grab buffer (early squeeze binds on arrival, nothing in 150 ms goes native once, 0 disables), both hands hover the nearest object; ";
    std::cout<<"PASS: real NativeGrab publication/filter/dispatcher caller order: one released epoch sample permits acquisition; held epoch/focus return stays disarmed\n";
}
