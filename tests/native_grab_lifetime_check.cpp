#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../src/native_grab.hpp"
#include "../src/vr_controls.hpp"
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
namespace {
constexpr std::uintptr_t fixture_dispatcher=0x3300000,fixture_new_world=0x3400000,fixture_new_pool=0x3500000;
unsigned fixture_transition{},fixture_originals{},fixture_writes{};std::array<float,3> fixture_last_velocity{};
void __fastcall fixture_velocity(void*,UINT,const float* linear,const float*,int) {
    assert(fixture_originals);++fixture_writes;std::copy(linear,linear+3,fixture_last_velocity.begin());
}
void __fastcall fixture_original(void*,void*) {
    ++fixture_originals;
    if(fixture_transition==1)put(fixture_entity+0xa8,std::uint32_t{0x10002});
    if(fixture_transition==2) {
        put(fixture_game+0x60410,fixture_new_world);put(fixture_dispatcher+0x18,fixture_new_world);
        put(fixture_new_world+0x28,fixture_pool);put(fixture_new_world+0x30,std::uint32_t{3});
    }
    if(fixture_transition==3) {
        for(std::size_t i=0;i<0xb0;++i) {
            const auto found=fixture_bytes.find(fixture_pool+2*0xb0+i);
            if(found!=fixture_bytes.end())fixture_bytes[fixture_new_pool+2*0xb0+i]=found->second;
        }
        put(fixture_world+0x28,fixture_new_pool);
    }
    if(fixture_transition==4)put(fixture_component+0x18,d2vr::rig::Identity{7,26});
}
}
namespace d2vr {
struct NativeGrabCheck {
    static void setup(NativeGrab& grab) {grab.active_=true;grab.base_=fixture_base;grab.read_=read;grab.logic_=logic;grab.original_=fixture_original;grab.velocity_=fixture_velocity;}
    static void dispatch(NativeGrab& grab) {grab.invoke(reinterpret_cast<void*>(fixture_dispatcher),nullptr);}
    static void consume(NativeGrab& grab,UINT64 now) {std::lock_guard<std::mutex> lock(grab.mutex_);grab.update_lane(0,fixture_world,now);}
    static void retained_bone(NativeGrab& grab,int bone) {std::lock_guard<std::mutex> lock(grab.mutex_);assert(grab.lanes_[0].held);grab.lanes_[0].held->bone=bone;}
};
}
namespace {
void physical_routing(unsigned side,unsigned mode,unsigned transition=0) {
    using namespace d2vr;
    fixture();put(fixture_dispatcher+0x18,fixture_world);fixture_transition=0;fixture_originals=fixture_writes=0;
    const auto owner=*native_player_camera(read,fixture_base,fixture_camera);
    WorldTrackedPose hand{{},{{{1,0,0},{0,1,0},{0,0,1}}}};
    NativeGrab grab;NativeGrabCheck::setup(grab);GripIntent intent;
    auto now=GetTickCount64();
    grab.publish(side,owner,hand,0,1,now,true,false,now);grab.filter_native_grip(side,0,1,now,true,true,false);
    intent.update(0,0,now,true);
    const bool sword=mode==2,menu=mode==3,temple=mode==4,scabbard=mode==5,choke=mode==6;
    Sleep(1);now=GetTickCount64();
    const auto gesture=intent.update(.9f,.9f,now,!sword);
    assert(gesture.fist==!sword);
    float physical_grip=.9f,composed_grip=.9f;
    if(temple || scabbard)physical_grip=composed_grip=0;
    composed_grip=0;
    const bool selected=gesture.fist || grab.holding(side);
    const float observed=selected?(mode==1?composed_grip:physical_grip):0.f;
    const bool reserved=sword || menu || choke;
    grab.publish(side,owner,hand,gesture.fist?.9f:0.f,1,now,true,false,now);
    grab.filter_native_grip(side,observed,1,now,true,true,reserved);
    NativeGrabCheck::dispatch(grab);
    assert(grab.holding(side)==(mode==0) && !grab.holding(1-side));
    assert(grab.picked()==(mode==0?1u:0u) && fixture_writes==0);
    if(mode!=0) {
        if(observed<.35f)return;
        Sleep(1);now=GetTickCount64();
        grab.publish(side,owner,hand,.9f,1,now,true,false,now);
        grab.filter_native_grip(side,.9f,1,now,true,true,false);
        NativeGrabCheck::dispatch(grab);assert(!grab.holding(side) && grab.picked()==0 && fixture_writes==0);
        return;
    }
    Sleep(11);now=GetTickCount64();hand.position.x=.04;
    const auto holding=intent.update(.9f,0,now,true);assert(holding.fist);
    grab.publish(side,owner,hand,.9f,1,now,true,false,now);
    grab.filter_native_grip(side,.9f,1,now,true,true,false);
    fixture_transition=transition;NativeGrabCheck::dispatch(grab);
    if(transition && transition!=4) {
        assert(!grab.holding(side) && grab.picked()==1 && grab.lost()==1 && fixture_writes==0);
        NativeGrabCheck::dispatch(grab);assert(!grab.holding(side) && fixture_writes==0);
        return;
    }
    assert(grab.holding(side) && grab.picked()==1 && fixture_writes==1 && fixture_last_velocity[0]>0);
    Sleep(11);now=GetTickCount64();assert(!intent.update(0,0,now,true).fist);
    grab.publish(side,owner,hand,0,1,now,true,false,now);
    grab.filter_native_grip(side,0,1,now,true,true,false);NativeGrabCheck::dispatch(grab);
    assert(!grab.holding(side) && grab.picked()==1 && grab.dropped()==1 && grab.lost()==0 && fixture_writes==2);
    NativeGrabCheck::dispatch(grab);assert(fixture_writes==2);
}
}
int main() {
    using namespace d2vr;
    for(unsigned side=0;side<2;++side)for(unsigned mode=0;mode<7;++mode)physical_routing(side,mode);
    for(unsigned side=0;side<2;++side)for(unsigned transition=1;transition<5;++transition)physical_routing(side,0,transition);
    {
        fixture();put(fixture_dispatcher+0x18,fixture_world);fixture_transition=0;
        put(fixture_model+0x1840,std::array<float,8>{.08f,-.02f,-.02f,0,.12f,.02f,.02f,0});
        put(fixture_model+0x1860,std::array<float,3>{.1f,0,0});
        const auto owner=*native_player_camera(read,fixture_base,fixture_camera);
        const WorldTrackedPose hand{{},{{{1,0,0},{0,1,0},{0,0,1}}}};
        NativeGrab grab;NativeGrabCheck::setup(grab);grab.require_contact_proxies(true);const auto now=GetTickCount64();
        grab.publish(0,owner,hand,0,1,now,true,false,now);
        assert(!grab.focus_point(0,now));
        HandProxies proxies{};for(auto& sphere:proxies)sphere={{-.2,0,0},.02};
        grab.contact_proxies(0,proxies,1,now);assert(!grab.focus_point(0,now)); // Inside the old 45cm bubble, clear of the hand.
        proxies[hand_palm_spheres+3]={{.06,0,0},.02};
        grab.contact_proxies(0,proxies,1,now);assert(grab.focus_entity(0,now)==fixture_entity); // Fingertip alone touches.
        grab.contact_proxies(0,proxies,2,now);assert(!grab.focus_point(0,now));
        grab.contact_proxies(0,proxies,1,now-33);assert(!grab.focus_point(0,now));
        grab.contact_proxies(0,proxies,1,now);
        grab.filter_native_grip(0,0,1,now,true,true,false);
        grab.publish(0,owner,hand,.9f,1,now,true,false,now);grab.filter_native_grip(0,.9f,1,now,true,true,false);
        NativeGrabCheck::dispatch(grab);assert(grab.holding(0));
        grab.clear();grab.publish(0,owner,hand,0,1,now,true,false,now);assert(!grab.focus_point(0,now));
    }
    for(unsigned transition=0;transition<5;++transition) {
        fixture();put(fixture_dispatcher+0x18,fixture_world);fixture_transition=0;fixture_originals=fixture_writes=0;fixture_last_velocity={};
        const auto owner=*native_player_camera(read,fixture_base,fixture_camera);
        WorldTrackedPose hand{{},{{{1,0,0},{0,1,0},{0,0,1}}}};
        NativeGrab grab;NativeGrabCheck::setup(grab);const auto tick=GetTickCount64();
        grab.publish(0,owner,hand,0,1,tick,true,false,tick);grab.filter_native_grip(0,0,1,tick,true,true,false);
        Sleep(1);const auto press=GetTickCount64();
        grab.publish(0,owner,hand,.9f,1,press,true,false,press);grab.filter_native_grip(0,.9f,1,press,true,true,false);
        NativeGrabCheck::dispatch(grab);assert(grab.holding(0) && fixture_originals==1 && fixture_writes==0);
        Sleep(11);const auto move=GetTickCount64();hand.position.x=.05;
        grab.publish(0,owner,hand,.9f,1,move,true,false,move);grab.filter_native_grip(0,.9f,1,move,true,true,false);
        fixture_transition=transition;NativeGrabCheck::dispatch(grab);
        // Transition 4 changes only the component's flag word (same generation): a held prop whose flags change as it moves
        // stays held (headset v447: the ashtray was lost 30 ms after every grab).
        const bool kept=transition==0 || transition==4;
        assert(fixture_originals==2 && grab.holding(0)==kept);
        // All replacements are valid native bodies; retained hold identity alone rejects them.
        const auto current=native_rigid_body(read,fixture_base,fixture_entity);assert(current);
        if(!kept) {
            assert(fixture_writes==0 && grab.lost()==1);
            const auto d=grab.diagnostics();assert(d && (*d)[0].role==RightGripOwner::Role::release && !(*d)[0].focus && !(*d)[0].candidates);
            Sleep(11);const auto held=GetTickCount64();
            grab.publish(0,owner,hand,.9f,1,held,true,false,held);grab.filter_native_grip(0,.9f,1,held,true,true,false);
            NativeGrabCheck::dispatch(grab);assert(!grab.holding(0) && fixture_writes==0); // recreation cannot reuse the same squeeze
            fixture_transition=0;Sleep(1);const auto release=GetTickCount64();
            grab.publish(0,owner,hand,0,1,release,true,false,release);grab.filter_native_grip(0,0,1,release,true,true,false);
            Sleep(1);const auto repress=GetTickCount64();
            grab.publish(0,owner,hand,.9f,1,repress,true,false,repress);grab.filter_native_grip(0,.9f,1,repress,true,true,false);
            NativeGrabCheck::dispatch(grab);
            if(!grab.holding(0)) {
                Sleep(101);const auto retry=GetTickCount64();
                grab.publish(0,owner,hand,.9f,1,retry,true,false,retry);
                grab.filter_native_grip(0,.9f,1,retry,true,true,false);NativeGrabCheck::dispatch(grab);
            }
            if(!grab.holding(0)) {const auto failure_diag=*grab.diagnostics();std::cerr<<"transition="<<transition<<" role="<<static_cast<unsigned>(failure_diag[0].role)<<" candidates="<<failure_diag[0].candidates<<" gate="<<static_cast<unsigned>(failure_diag[0].gate)<<" usable="<<failure_diag[0].usable<<" focus="<<failure_diag[0].focus.has_value()<<"\n";}
            assert(grab.holding(0)); // a new press can acquire after the existing 100ms attempt cadence
        } else assert(fixture_writes==1 && fixture_last_velocity[0]>0);
    }
    for(const int retained_bone:{-1,0,5}) {
        fixture();put(fixture_dispatcher+0x18,fixture_world);fixture_transition=0;fixture_originals=fixture_writes=0;
        const auto owner=*native_player_camera(read,fixture_base,fixture_camera);
        const WorldTrackedPose hand{{},{{{1,0,0},{0,1,0},{0,0,1}}}};
        NativeGrab grab;NativeGrabCheck::setup(grab);const auto tick=GetTickCount64();
        grab.publish(0,owner,hand,0,1,tick,true,false,tick);grab.filter_native_grip(0,0,1,tick,true,true,false);
        Sleep(1);const auto press=GetTickCount64();
        grab.publish(0,owner,hand,.9f,1,press,true,false,press);grab.filter_native_grip(0,.9f,1,press,true,true,false);
        NativeGrabCheck::dispatch(grab);assert(grab.holding(0));
        NativeGrabCheck::retained_bone(grab,retained_bone);
        Sleep(11);const auto failure=GetTickCount64();fixture_transition=1;
        grab.publish(0,owner,hand,.9f,1,failure,true,false,failure);grab.filter_native_grip(0,.9f,1,failure,true,true,false);
        NativeGrabCheck::dispatch(grab);
        assert(!grab.holding(0) && fixture_writes==0 && grab.lost()==(retained_bone<0?1u:0u));
        const auto diagnostics=grab.diagnostics(failure);
        assert(diagnostics && !(*diagnostics)[0].held && !(*diagnostics)[0].pending_release &&
            (*diagnostics)[0].role==RightGripOwner::Role::release);
        Sleep(11);const auto held=GetTickCount64();
        grab.publish(0,owner,hand,.9f,1,held,true,false,held);grab.filter_native_grip(0,.9f,1,held,true,true,false);
        NativeGrabCheck::dispatch(grab);assert(!grab.holding(0) && fixture_writes==0);
    }
    {   // G3 (plan Task 8): discovery looks where the hand is. A hinted registry index (this hand's stage-0 hit owners, its probe hits) is looked up
        // every tick before the slice, so a body there is cached the same tick even with the cursor far away; at most 8 hints.
        d2vr::NativeGrabDiscovery discovery;unsigned lookups=0;std::vector<unsigned> asked;
        d2vr::NativeRigidBody book{};book.entity=0x5000;book.entity_id=(9000u<<16)|1u;book.pose.position={.2,0,0};
        const auto lookup=[&](unsigned index)->std::optional<d2vr::NativeRigidBody> {++lookups;asked.push_back(index);return index==9000?std::optional<d2vr::NativeRigidBody>{book}:std::nullopt;};
        const auto validate=[&](const d2vr::NativeRigidBody& b)->std::optional<d2vr::NativeRigidBody> {return b;};
        assert(discovery.advance({},1,lookup,validate).empty() && discovery.cursor()==1025);   // hint_found_same_tick: the cursor is far from 9000
        const std::array<unsigned,8> hints{9000,0,0,0,0,0,0,0};
        const auto& found=discovery.advance({},2,lookup,validate,&hints);
        assert(found.size()==1 && found[0].entity==book.entity && asked[1024]==9000 && discovery.cursor()==2049);
        assert(discovery.advance({},2,lookup,validate,&hints).size()==1 && lookups==2048+1);   // one pass per tick
        discovery.advance({},3,lookup,validate,&hints);assert(discovery.advance({},3,lookup,validate).size()==1);   // no duplicate entry
        // hints_bounded_to_8: eight indices plus the slice per tick; index 0 (the player) and out-of-range indices are skipped
        const std::array<unsigned,8> many{1,2,3,4,5,6,7,0x3fff};lookups=0;
        discovery.advance({},4,lookup,validate,&many);assert(lookups==1024+7);
    }
    std::cout<<"PASS: discovery hints (same tick, 8 at most); original native dispatcher completes before retained-body validation; entity ID reuse, world/pool recreation and started-identity replacement emit no motor/throw, require release before reacquisition; retained prop/limb classification survives optional reset\n";
}

