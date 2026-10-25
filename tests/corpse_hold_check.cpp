// Task 10: a corpse held through NativeGrab with the force envelope. Fixture style of native_grab_lifetime_check.cpp (player, camera, world, motion
// array) and native_rigid_body_check.cpp (a guard's 17 ragdoll bodies). Time is synthetic: every tick is passed to update_lane.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../src/native_grab.hpp"
#include "../src/native_weapon_hands.hpp"
#include <cmath>
#include <fstream>
#include <filesystem>
#include <functional>
#include <future>
#include <thread>
#include <atomic>
#include <chrono>
#include <sstream>
#include <cstdio>
#include <iostream>
#include <map>
#include <string>
#include <vector>

using namespace d2vr;
static int failures=0;
#define REQUIRE(x) do{if(!(x)){std::printf("REQUIRE line %d: %s\n",__LINE__,#x);++failures;return;}}while(0)
#define CHECK(x) do{if(!(x)){std::printf("FAIL line %d: %s\n",__LINE__,#x);++failures;}}while(0)

namespace {
constexpr std::uintptr_t base=0x140000000,game=0x2000000,player=0x2100000,camera=0x2200000,body_cpnt=0x2300000,controller=0x2400000,physics=0x2500000,
    delegate=0x2600000,world=0x3000000,pool=0x3100000,motions=0x3800000,other_world=0x3400000;
constexpr std::uintptr_t guard_a=0x4000000,guard_b=0x4100000,prop=0x4200000;
constexpr float prop_x=30.f;
constexpr std::uint32_t id_a=0x10001,id_b=0x20002,id_prop=0x30003;
constexpr std::uint32_t pool_count=0x60;
std::map<std::uintptr_t,unsigned char> bytes;
template<class T> void put(std::uintptr_t at,const T& value) {
    const auto* source=reinterpret_cast<const unsigned char*>(&value);
    for(std::size_t i=0;i<sizeof(value);++i) bytes[at+i]=source[i];
}
bool read(std::uintptr_t at,void* out,std::size_t size) {
    for(std::size_t i=0;i<size;++i) {const auto it=bytes.find(at+i);if(it==bytes.end()) return false;static_cast<unsigned char*>(out)[i]=it->second;}
    return true;
}
UINT logic() {return 2;}

struct Write {std::uint32_t handle{};std::array<float,3> linear{};std::array<float,3> angular{};};
std::vector<Write> writes;
bool fault_next_write=false;
void __fastcall velocity(void*,UINT handle,const float* linear,const float* angular,int) {
    if(fault_next_write) RaiseException(EXCEPTION_ACCESS_VIOLATION,0,0,nullptr);   // caught by guarded_call: a fault inside the engine's setter
    writes.push_back({handle,{linear[0],linear[1],linear[2]},{angular[0],angular[1],angular[2]}});
}
void __fastcall original(void*,void*) {}

constexpr std::uint32_t a_first=0,b_first=0x20,prop_index=0x40;           // pool slots; a guard's body b is slot first+b
constexpr std::uint32_t a_motion=5,b_motion=40,prop_motion=70;
std::uint32_t handle_of(std::uint32_t first,int bone) {return 0x1000000u+first+static_cast<std::uint32_t>(bone);}
std::uintptr_t record_of(std::uint32_t first,int bone) {return pool+(first+static_cast<std::uint32_t>(bone))*0xb0;}
std::uint32_t motion_slot(std::uint32_t first_motion,int bone) {return first_motion+static_cast<std::uint32_t>(bone);}
std::uintptr_t motion_at(std::uint32_t first_motion,int bone) {return motions+motion_slot(first_motion,bone)*0x80;}

void build_guard(std::uintptr_t npc,std::uint32_t id,std::uintptr_t list,std::uintptr_t cpnt,std::uintptr_t handles,std::uintptr_t model,
                 std::uint32_t first,std::uint32_t first_motion,double x0,unsigned char state) {
    const unsigned index=id>>16;
    put(game+0x3130b0+index*8,npc);put(npc,base+0x1c5e108);put(npc+0xa8,id);put(npc+0xd1,state);put(npc+0xf0,model);
    put(npc+0x300,std::array<float,3>{static_cast<float>(x0),0,0});
    put(npc+0x268,rig::List{list,1,1<<8});put(list,cpnt);
    put(cpnt,base+0x1c54008);put(cpnt+0x18,rig::Identity{3,10});put(cpnt+0x100,handles);
    put(handles+0x28,handles+0x120);put(handles+0x30,std::uint32_t{17});
    for(int bone=0;bone<17;++bone) {
        const auto handle=handle_of(first,bone);put(handles+0x120+static_cast<std::uintptr_t>(bone)*4,handle);
        rig::Transform t{};t.f[0]=t.f[5]=t.f[10]=t.f[15]=1;t.f[12]=static_cast<float>(x0+bone*.2);
        const auto record=record_of(first,bone);
        put(record,t);put(record+0x40,motion_slot(first_motion,bone));put(record+0x44,std::uint32_t{0x82});put(record+0x70,handle);put(record+0x74,handle);
        put(motion_at(first_motion,bone)+0x28,motion_slot(first_motion,bone));   // the entry's attached body id
        put(motion_at(first_motion,bone)+0x26,std::uint16_t{0x3f00});   // inverse 0.5: plausible only for the class values
        put(motion_at(first_motion,bone)+0x40,std::array<float,3>{0,0,0});put(motion_at(first_motion,bone)+0x50,std::array<float,3>{0,0,0});
    }
}
void build_prop(float extent) {
    const std::uintptr_t entries=0x4210000,component=0x4220000,model=0x4240000;
    const unsigned index=id_prop>>16;
    put(game+0x3130b0+index*8,prop);put(prop,base+0x1c886a8);put(prop+0xa8,id_prop);put(prop+0x300,std::array<float,3>{prop_x,0,0});
    put(prop+0x268,rig::List{entries,1,1<<8});put(entries,component);put(prop+0xf0,model);put(model,base+0x1b0dd88);
    put(component,base+0x1c745c8);put(component+0x10,prop);put(component+0x18,rig::Identity{7,10});put(component+0xe8,base+0x1c74798);
    const std::uint32_t handle=handle_of(prop_index,0);put(component+0xf8,handle);put(component+0xc8,std::uintptr_t{});put(component+0xd2,static_cast<unsigned char>(0));
    rig::Transform t{};t.f[0]=t.f[5]=t.f[10]=t.f[15]=1;t.f[12]=prop_x;
    const auto record=pool+prop_index*0xb0;put(record,t);put(record+0x44,std::uint32_t{130});put(record+0x40,prop_motion);put(record+0x70,handle);put(record+0x74,handle);
    const float h=extent*.5f;   // bounds around the body: extent^3 * 350 kg/m3
    put(model+0x1840,std::array<float,8>{prop_x-h,-h,-h,0,prop_x+h,h,h,0});put(model+0x1860,std::array<float,3>{prop_x,0,0});
    put(motions+prop_motion*0x80+0x28,prop_motion);put(motions+prop_motion*0x80+0x26,std::uint16_t{0x3f00});put(motions+prop_motion*0x80+0x40,std::array<float,3>{0,0,0});
}
void fixture(unsigned char state_a=0x08,unsigned char state_b=0x08,bool with_prop=true,float prop_extent=.18f) {
    bytes.clear();writes.clear();fault_next_write=false;FaultLatch::reset_for_tests();
    put(base+0x39a5290,game);put(game,base+0x1c940a8);put(game+0x3130b0,player);put(player,base+0x1c5e428);
    put(player+0x300,std::array<float,3>{50,50,0});
    put(camera+0x25150,std::array<std::byte,0x200>{});
    for(auto [at,table]:std::array<std::pair<std::uintptr_t,std::uintptr_t>,9>{{
        {camera,0x1c72218},{camera+0x20,0x1c722f8},{body_cpnt,0x1c5a8d8},{body_cpnt+0x20,0x1c5a9b0},
        {controller,0x1c72918},{physics,0x1c73268},{physics+0x20,0x1c73340},{physics+0x30,0x1c73378},{delegate+0x160,0x1b02768}}}) put(at,base+table);
    for(auto at:{camera,body_cpnt,controller,physics}) {put(at+0x10,player);put(at+0x18,rig::Identity{5,10});}
    put(camera+0x25158,controller);put(camera+0x25160,physics);put(camera+0x25170,body_cpnt);
    put(physics+0x48,delegate);put(delegate+0x168,physics);put(delegate+0x120,player+0x300);put(delegate+0x1e0,delegate);
    put(delegate+0xd0,0.f);put(delegate+0xbc,0.f);put(delegate+0x40,0.f);
    put(controller+0xd0,std::array<float,2>{});put(player+0x30c,std::array<float,9>{1,0,0,0,1,0,0,0,1});
    put(game+0x60410,world);put(world+0x28,pool);put(world+0x30,pool_count);put(world+0x120,motions);
    put(other_world+0x28,pool);put(other_world+0x30,pool_count);put(other_world+0x120,motions);
    build_guard(guard_a,id_a,0x4010000,0x4020000,0x4030000,0x4040000,a_first,a_motion,0,state_a);
    build_guard(guard_b,id_b,0x4110000,0x4120000,0x4130000,0x4140000,b_first,b_motion,20,state_b);
    if(with_prop) build_prop(prop_extent);
}
}
namespace d2vr {
struct NativeGrabCheck {
    static void setup(NativeGrab& grab) {grab.active_=true;grab.base_=base;grab.read_=read;grab.logic_=logic;grab.original_=original;grab.velocity_=velocity;}
    static void consume(NativeGrab& grab,unsigned side,UINT64 now,std::uintptr_t w=world) {std::lock_guard<std::mutex> lock(grab.mutex_);grab.update_lane(side,w,now);grab.publish_snapshot_locked();}
    static void collar(NativeGrab& grab,bool on) {grab.collar_grab_=on;}
    static bool corpse(NativeGrab& grab,unsigned side) {std::lock_guard<std::mutex> lock(grab.mutex_);return grab.lanes_[side].corpse;}
    static bool capped(NativeGrab& grab,unsigned side) {std::lock_guard<std::mutex> lock(grab.mutex_);return grab.lanes_[side].capped;}
    static int bone(NativeGrab& grab,unsigned side) {std::lock_guard<std::mutex> lock(grab.mutex_);return grab.lanes_[side].held?grab.lanes_[side].held->bone:-99;}
    static double mass(NativeGrab& grab,unsigned side) {std::lock_guard<std::mutex> lock(grab.mutex_);return grab.lanes_[side].mass;}
    static void tune(NativeGrab& grab,const GrabTuning& t) {grab.tuning_=t;grab.prop_tuning_=t.prop_view();}
    static bool contact_blocked(NativeGrab& grab,std::uintptr_t entity,bool guard) {return grab.contact_write_blocked(entity,guard);}
    static std::mutex& mutex(NativeGrab& grab) {return grab.mutex_;}
    static void instance(NativeGrab* grab) {NativeGrab::instance_=grab;}
    static std::string source() {std::ifstream f("src/native_grab.hpp");std::stringstream ss;ss<<f.rdbuf();return ss.str();}
};
}
namespace {
struct Rig {
    NativeGrab grab;NativePlayerCamera owner{};UINT64 t=1000000;
    Rig() {NativeGrabCheck::setup(grab);const auto found=native_player_camera(read,base,camera);if(!found) {std::printf("no owner camera\n");std::exit(3);}owner=*found;}
    WorldTrackedPose pose(Vec3 p) const {return WorldTrackedPose{p,{{{1,0,0},{0,1,0},{0,0,1}}}};}
    // One hand sample: publish, decide (grip filter), run the dispatcher tick. raw>0 is a full fist (the physical lane sees nothing for a grip alone).
    PalmProbe palm{};   // G3 (Task 7): the palm probe published with each step (invalid: the v611 path, as before)
    void step(unsigned side,Vec3 hand,float raw,UINT64 dt_ms=100,std::uintptr_t w=world) {
        t+=dt_ms;
        PalmProbe at=palm;if(at.valid) at.palm=hand-at.forward*palm_axis_push;   // the palm point sits at `hand`
        grab.publish(side,owner,pose(hand),raw,1,t,true,false,t,nullptr,0,at);
        grab.filter_native_grip(side,raw,1,t,true,true,false);
        NativeGrabCheck::consume(grab,side,t,w);
    }
    // Arm with an open hand, then close it at `hand`; true when the lane now holds something.
    // The hover cue (focus_point) runs every frame in the game and advances the discovery cursor one 1024-slot slice per call: 17 hovers cover the whole
    // registry, so a hand that arrives somewhere new has found what is there by the time it closes.
    bool grasp(unsigned side,Vec3 hand,float raw=.9f,int hovers=18) {
        for(int i=0;i<hovers;++i) {step(side,hand,0,20);grab.focus_point(side,t);}
        step(side,hand,raw);
        return grab.holding(side);
    }
    std::string json() {return grab.corpse_diagnostics().json();}
};
bool has(const std::string& text,const std::string& piece) {return text.find(piece)!=std::string::npos;}
double expected_cap(double object_kg,double dt,const GrabTuning& tuning,double share=1.) {return tuning.axial_cap()*std::min(dt,tuning.dt_max)/effective_mass(object_kg,tuning,share);}

void dead_guard_wrist_binds_and_write_is_capped() {
    fixture();Rig r;
    // bone 12 (a calf, class 3.4 kg): effective mass 5.4 kg. At the v608 strength (1800 N) the per-write cap is 11.1 m/s, above the 10 m/s motor limit, so a light
    // calf is no longer capped; the cap path is exercised at the pre-H1 strength (900 N * (1/30 s) / 5.4 kg = 5.6 m/s per write).
    GrabTuning legacy;legacy.strength_scale=kLegacyCorpseStrengthScale;NativeGrabCheck::tune(r.grab,legacy);
    const Vec3 calf{2.4,0,0};
    REQUIRE(r.grasp(0,calf));
    CHECK(NativeGrabCheck::bone(r.grab,0)==12 && NativeGrabCheck::corpse(r.grab,0) && NativeGrabCheck::capped(r.grab,0));
    CHECK(has(r.json(),"\"binds\":[1,0]") && has(r.json(),"\"limb\":1"));
    const auto before=writes.size();
    r.step(0,calf+Vec3{.4,0,0},.9f,100);   // 0.4 m in 100 ms
    REQUIRE(writes.size()==before+1);
    const double cap=expected_cap(3.4,.1,legacy);
    const auto& w=writes.back();
    CHECK(w.handle==handle_of(a_first,12));
    const double dv=std::hypot(w.linear[0],std::hypot(w.linear[1],w.linear[2]));
    CHECK(dv>0 && dv<=cap*1.01);
    CHECK(has(r.json(),"\"capped_writes\":[1,0]"));
    CHECK(r.grab.hold_anchor(0).active && r.grab.hold_anchor(0).tick==r.t && !r.grab.hold_anchor(1).active);
}
void collar_binds_dead_not_awake() {
    for(const unsigned char state:{static_cast<unsigned char>(0x20),static_cast<unsigned char>(0x08)}) {
        fixture(state);Rig r;NativeGrabCheck::collar(r.grab,true);
        REQUIRE(r.grasp(0,{.4,0,0}));CHECK(NativeGrabCheck::bone(r.grab,0)==2 && NativeGrabCheck::corpse(r.grab,0));
        CHECK(has(r.json(),"\"collar\":1"));
    }
    {   // awake and ragdolling (0x80): no candidate, not even a limb
        fixture(0x80);Rig r;NativeGrabCheck::collar(r.grab,true);
        CHECK(!r.grasp(0,{.4,0,0}));
        fixture(0x80);Rig again;NativeGrabCheck::collar(again.grab,true);CHECK(!again.grasp(0,{2,0,0}));
    }
    {   // a grip alone is the game's shoulder carry: the lane never sees it; and the knob is off by default
        fixture(0x08);Rig r;NativeGrabCheck::collar(r.grab,true);
        CHECK(!r.grasp(0,{.4,0,0},0.f));
        fixture(0x08);Rig off;
        off.grasp(0,{.4,0,0});CHECK(NativeGrabCheck::bone(off.grab,0)!=2);
    }
    {   // the hover cue: a grip alone (no fist, squeeze 0) is offered no collar; a fist at the same spot is
        fixture(0x08);Rig r;NativeGrabCheck::collar(r.grab,true);
        for(int i=0;i<18;++i) {r.step(0,{.4,0,0},0,20);r.grab.focus_point(0,r.t);}
        CHECK(!r.grab.focus_point(0,r.t));
        r.step(0,{.4,0,0},.4f,20);   // a fist closing (squeeze .4: reserved, not yet pressed): the collar is now a target
        CHECK(!r.grab.holding(0) && r.grab.focus_point(0,r.t));
    }
    {   // the collar needs the fist while it is held out: a hand that squeezes less than a fist (below .35) is offered only limbs
        fixture(0x08);Rig r;NativeGrabCheck::collar(r.grab,true);
        r.step(0,{.4,0,0},0);r.step(0,{.4,0,0},0);r.step(0,{.4,0,0},.3f);
        CHECK(!r.grab.holding(0));
    }
}
void open_fist_writes_nothing() {
    fixture();Rig r;const Vec3 wrist{2,0,0};
    REQUIRE(r.grasp(0,wrist));CHECK(NativeGrabCheck::bone(r.grab,0)==10);
    r.step(0,wrist+Vec3{.2,0,0},.9f);r.step(0,wrist+Vec3{.4,0,0},.9f);   // swinging
    const auto before=writes.size();
    r.step(0,wrist+Vec3{.6,0,0},0);   // opens the fist
    CHECK(!r.grab.holding(0) && writes.size()==before);
    CHECK(has(r.json(),"\"open_fist\":[1,0]") && has(r.json(),"\"holds\":[0,0]"));
    CHECK(!r.grab.hold_anchor(0).active);
    r.step(0,wrist+Vec3{.6,0,0},0);CHECK(writes.size()==before);
}
void prop_still_throws_on_open_fist() {
    fixture();Rig r;
    const Vec3 at{prop_x,0,0};
    REQUIRE(r.grasp(0,at));CHECK(NativeGrabCheck::bone(r.grab,0)==-1 && !NativeGrabCheck::corpse(r.grab,0) && NativeGrabCheck::capped(r.grab,0));
    CHECK(r.grab.hold_anchor(0).active);
    for(int i=1;i<=6;++i) r.step(0,at+Vec3{.15*i,0,0},.9f,60);   // 2.5 m/s swing
    const auto before=writes.size();
    r.step(0,at+Vec3{1.0,0,0},0,60);
    CHECK(!r.grab.holding(0) && writes.size()==before+1 && writes.back().handle==handle_of(prop_index,0));   // the release write is the throw
    CHECK(writes.back().linear[0]>1.0f);
    CHECK(!has(r.json(),"\"binds\":[1"));   // a prop is not a corpse bind
}
void state_changed_releases_no_write() {
    fixture();Rig r;const Vec3 wrist{2,0,0};
    REQUIRE(r.grasp(0,wrist));r.step(0,wrist+Vec3{.1,0,0},.9f);
    const auto before=writes.size();
    put(guard_a+0xd1,static_cast<unsigned char>(0x00));   // woke up
    r.step(0,wrist+Vec3{.2,0,0},.9f);
    CHECK(!r.grab.holding(0) && writes.size()==before);
    CHECK(has(r.json(),"\"state_changed\":[1,0]"));
}
void state_change_and_open_fist_same_tick() {
    fixture();Rig r;const Vec3 wrist{2,0,0};
    REQUIRE(r.grasp(0,wrist));r.step(0,wrist+Vec3{.1,0,0},.9f);
    const auto before=writes.size();
    put(guard_a+0xd1,static_cast<unsigned char>(0x00));
    r.step(0,wrist+Vec3{.2,0,0},0);   // the fist opens on the very tick the guard wakes
    const auto json=r.json();
    CHECK(!r.grab.holding(0) && writes.size()==before);
    CHECK(has(json,"\"state_changed\":[1,0]") && has(json,"\"open_fist\":[0,0]") && has(json,"\"holds\":[0,0]"));
}
void world_change_during_lag_release() {
    fixture();Rig r;const Vec3 wrist{2,0,0};
    REQUIRE(r.grasp(0,wrist));
    r.step(0,wrist+Vec3{.3,0,0},.9f,40);r.step(0,wrist+Vec3{.6,0,0},.9f,40);   // the hand outruns the (static) body: lag > 0.5 m
    int ticks=0;
    while(r.grab.holding(0) && ticks<30 && NativeGrabCheck::bone(r.grab,0)>=0) {
        const auto json=r.json();
        if(has(json,"\"lag\":[1,0]")) break;
        // run until the tick BEFORE the lag timer expires, then change the world on the next one
        if(ticks>=6) break;
        r.step(0,wrist+Vec3{.6,0,0},.9f,40);++ticks;
    }
    CHECK(r.grab.holding(0) && has(r.json(),"\"lag\":[0,0]"));
    const auto before=writes.size();
    r.step(0,wrist+Vec3{.6,0,0},.9f,40,other_world);   // the lag timer would expire here; the world pointer changed on the same tick
    const auto json=r.json();
    CHECK(!r.grab.holding(0) && writes.size()==before);
    CHECK(has(json,"\"lifecycle\":[1,0]") && has(json,"\"lag\":[0,0]") && has(json,"\"fault\":[0,0]") && !FaultLatch::tripped());
}
void lag_release_without_world_change() {
    fixture();Rig r;const Vec3 wrist{2,0,0};
    REQUIRE(r.grasp(0,wrist));
    r.step(0,wrist+Vec3{.3,0,0},.9f,40);r.step(0,wrist+Vec3{.6,0,0},.9f,40);
    for(int i=0;i<12 && r.grab.holding(0);++i) r.step(0,wrist+Vec3{.6,0,0},.9f,40);
    CHECK(!r.grab.holding(0) && has(r.json(),"\"lag\":[1,0]"));
    // the cooldown (0.3 s) blocks a regrab right away, and the release wrote no velocity of its own
    const auto before=writes.size();
    r.step(0,wrist,0);r.step(0,wrist,.9f,50);
    CHECK(!r.grab.holding(0) && writes.size()==before);
}
void runaway_seeded_releases_no_counter_write() {
    fixture();Rig r;const Vec3 wrist{2,0,0};
    REQUIRE(r.grasp(0,wrist));r.step(0,wrist+Vec3{.05,0,0},.9f);
    const auto before=writes.size();
    put(motion_at(a_motion,0)+0x40,std::array<float,3>{13,0,0});   // the pelvis, another bone of the held guard
    r.step(0,wrist+Vec3{.1,0,0},.9f);
    CHECK(!r.grab.holding(0) && writes.size()==before);
    CHECK(has(r.json(),"\"runaway\":1") && has(r.json(),"\"runaway\":[1,0]"));
    CHECK(r.grab.lockout().locked(id_a,seconds_from_ms(r.t)));
    put(motion_at(a_motion,0)+0x40,std::array<float,3>{0,0,0});
    // within 1 s the same guard is not grabbable again; no write for that entity
    CHECK(!r.grasp(0,wrist,.9f,6) && writes.size()==before);
    // another entity takes a hold at once, and writes resume for him
    const Vec3 other{22,0,0};
    CHECK(r.grasp(0,other));r.step(0,other+Vec3{.3,0,0},.9f,100);
    CHECK(writes.size()==before+1 && writes.back().handle==handle_of(b_first,10));
    // the lockout lapses after a second
    r.step(0,other,0,1200);
    CHECK(r.grasp(0,wrist) && NativeGrabCheck::bone(r.grab,0)==10 && r.grab.lockout().locked(id_a,seconds_from_ms(r.t))==false);
}
void runaway_wins_over_open_fist_same_tick() {
    fixture();Rig r;const Vec3 wrist{2,0,0};
    REQUIRE(r.grasp(0,wrist));r.step(0,wrist+Vec3{.05,0,0},.9f);
    const auto before=writes.size();
    put(motion_at(a_motion,0)+0x40,std::array<float,3>{13,0,0});
    r.step(0,wrist+Vec3{.1,0,0},0);   // the fist opens on the tick a body of the guard is flying
    CHECK(!r.grab.holding(0) && writes.size()==before);
    CHECK(has(r.json(),"\"runaway\":[1,0]") && has(r.json(),"\"open_fist\":[0,0]") && r.grab.lockout().locked(id_a,seconds_from_ms(r.t)));
}
void fault_latch_blocks_further_ragdoll_writes() {
    fixture();Rig r;const Vec3 wrist{2,0,0};
    REQUIRE(r.grasp(0,wrist));
    fault_next_write=true;
    r.step(0,wrist+Vec3{.3,0,0},.9f);
    fault_next_write=false;
    CHECK(FaultLatch::tripped() && !r.grab.holding(0) && writes.empty());
    CHECK(has(r.json(),"\"fault_latch\":true") && has(r.json(),"\"fault\":[1,0]"));
    // a later grab on another guard issues no ragdoll write (it holds, but every tick ends it as a fault)
    const Vec3 other{22,0,0};
    r.step(0,other,0,1500);r.step(0,other,0);
    r.grasp(0,other);r.step(0,other+Vec3{.3,0,0},.9f,100);r.step(0,other+Vec3{.6,0,0},.9f,100);
    CHECK(writes.empty());
    // a capped prop lane honours the tripped latch too: it holds, then ends as a fault on its first tick, and writes nothing
    const Vec3 at{prop_x,0,0};
    r.grasp(0,at);r.step(0,at+Vec3{.05,0,0},.9f,100);r.step(0,at+Vec3{.1,0,0},.9f,100);
    CHECK(writes.empty() && !r.grab.holding(0));
}
void readback_garbage_drops_hold_without_write() {
    struct Garbage {const char* name;std::function<void()> apply;std::function<void()> undo;};
    const std::uintptr_t held_motion=motion_at(a_motion,10);
    const std::vector<Garbage> cases{
        {"nan",[&]{put(held_motion+0x40,std::array<float,3>{std::nanf(""),0,0});},[&]{put(held_motion+0x40,std::array<float,3>{0,0,0});}},
        {"huge",[&]{put(held_motion+0x40,std::array<float,3>{1e9f,0,0});},[&]{put(held_motion+0x40,std::array<float,3>{0,0,0});}},
        {"recycled",[&]{put(record_of(a_first,10)+0x40,std::uint32_t{motion_slot(a_motion,10)+1});},[&]{put(record_of(a_first,10)+0x40,motion_slot(a_motion,10));}}};
    for(const auto& c:cases) {
        fixture();Rig r;const Vec3 wrist{2,0,0};
        REQUIRE(r.grasp(0,wrist));r.step(0,wrist+Vec3{.05,0,0},.9f);
        const auto before=writes.size();
        c.apply();r.step(0,wrist+Vec3{.1,0,0},.9f);
        if(!(!r.grab.holding(0) && writes.size()==before)) std::printf("case %s\n",c.name);
        CHECK(!r.grab.holding(0) && writes.size()==before);
        if(std::string(c.name)!="recycled") CHECK(has(r.json(),"\"fault\":[1,0]") && has(r.json(),"\"runaway\":0"));   // untrusted memory is a fault
        c.undo();
    }
    {   // a motion array that is simply gone: unknown velocity, counted; the hold steps from rest and still writes
        fixture();Rig r;const Vec3 wrist{2,0,0};
        REQUIRE(r.grasp(0,wrist));
        put(world+0x120,std::uintptr_t{0});
        const auto before=writes.size();
        r.step(0,wrist+Vec3{.2,0,0},.9f);
        CHECK(r.grab.velocity_unknown()>=1 && r.grab.holding(0) && writes.size()==before+1);
    }
}
// G3 (plan Task 7): the palm picks among every bone with a known radius. Guard a's bones are moved: the ones a case uses near the origin,
// the rest 3 m away (beyond every gate, still inside the 3.5 m discovery radius of the guard's origin).
void place(int bone,Vec3 at) {rig::Transform t{};t.f[0]=t.f[5]=t.f[10]=t.f[15]=1;t.f[12]=static_cast<float>(at.x);t.f[13]=static_cast<float>(at.y);
    t.f[14]=static_cast<float>(at.z);put(record_of(a_first,bone),t);}
void scatter() {for(int bone=0;bone<17;++bone) place(bone,{0,3,.1*bone});}
PalmProbe facing(Vec3 direction) {PalmProbe p;p.forward={1,0,0};p.direction=direction*(1/length(direction));p.valid=true;return p;}
std::optional<NativeRigidBody> palm_pick(Vec3 palm_point,Vec3 direction,bool collar=false,GrabTuning tuning={}) {
    auto probe=facing(direction);probe.palm=palm_point-probe.forward*palm_axis_push;
    return corpse_candidate(read,base,guard_a,probe,tuning,collar);
}
void torso_reach_catches() {
    fixture(0x08,0x08,false);scatter();place(1,{0,0,0});   // the spine (radius 0.13): the palm 5 cm over it, facing down
    const auto pick=palm_pick({0,0,.18},{0,0,-1});CHECK(pick && pick->bone==1);
    const auto old=corpse_candidate(read,base,guard_a,Vec3{0,0,.18},false);CHECK(old && old->bone!=1);   // the v611 limb set never offered the torso
    Rig r;r.palm=facing({0,0,-1});
    REQUIRE(r.grasp(0,{0,0,.18}));
    CHECK(NativeGrabCheck::bone(r.grab,0)==1 && NativeGrabCheck::corpse(r.grab,0) && NativeGrabCheck::capped(r.grab,0));   // the collar's physical hold
    fixture(0x08,0x08,false);scatter();place(0,{0,0,0});place(11,{.3,.3,0});   // the pelvis and a thigh catch too
    CHECK(palm_pick({0,0,.19},{0,0,-1})->bone==0);CHECK(palm_pick({.3,.3,.13},{0,0,-1})->bone==11);
}
void forearm_preferred_over_torso_at_equal_distance() {
    // The spine 0.12 m below the palm point and the forearm 0.12 m beside it, both 45 degrees off the palm: equal cost but the torso's 1.1
    fixture(0x08,0x08,false);scatter();place(1,{0,0,0});place(6,{0,-.17,.25});
    const Vec3 point{0,0,.25},dir{0,-1,-1};
    CHECK(palm_pick(point,dir)->bone==6);
    GrabTuning flat;flat.priority_torso=.9;CHECK(palm_pick(point,dir,false,flat)->bone==1);   // the priority decides it
}
void head_is_candidate_with_known_radius() {
    // Ruling H10 (spec: "every ragdoll bone with a known radius"): the head (radius 0.11) is a candidate, at limb priority
    fixture(0x08,0x08,false);scatter();place(4,{0,0,0});
    const auto pick=palm_pick({0,0,.16},{0,0,-1});CHECK(pick && pick->bone==4);
    CHECK(corpse_palm_bone(4,false) && !corpse_palm_bone(2,false) && !corpse_palm_bone(3,false) && corpse_palm_bone(2,true) && !corpse_palm_bone(17,true));
    for(int bone=0;bone<17;++bone) CHECK(corpse_palm_bone(bone,true));
}
void collar_needs_squeeze_rule() {
    fixture(0x08,0x08,false);scatter();place(2,{0,0,0});   // the chest under the palm: offered only with the collar rule
    CHECK(palm_pick({0,0,.2},{0,0,-1},false)->bone!=2);CHECK(palm_pick({0,0,.2},{0,0,-1},true)->bone==2);   // (a far bone keeps the body cached)
    {Rig r;r.palm=facing({0,0,-1});CHECK(!r.grasp(0,{0,0,.2}));}   // build/collar-grab off: no chest hold
    {fixture(0x08,0x08,false);scatter();place(2,{0,0,0});Rig r;r.palm=facing({0,0,-1});NativeGrabCheck::collar(r.grab,true);
     REQUIRE(r.grasp(0,{0,0,.2}));CHECK(NativeGrabCheck::bone(r.grab,0)==2);}
    {fixture(0x80,0x08,false);scatter();place(1,{0,0,0});CHECK(!palm_pick({0,0,.18},{0,0,-1}));}   // awake (ragdolling): never a corpse candidate
    {fixture(0x08,0x08,false);scatter();place(6,{0,0,0});   // nothing in the gate: the nearest bone is returned (discovery keeps him) and the selection refuses it
     const auto distant=palm_pick({0,0,1.5},{0,0,-1});CHECK(distant && distant->bone==6);
     Rig r;r.palm=facing({0,0,-1});CHECK(!r.grasp(0,{0,0,1.5}));}
}
void kill_switch_disables_corpse_candidates() {
    fixture();Rig r;CorpseOptions off;off.enabled=false;r.grab.configure_corpse(off);
    CHECK(!r.grasp(0,{2,0,0}));
    fixture();Rig again;again.grab.configure_corpse(CorpseOptions{});   // a fresh discovery: the first scan slice sees the guard
    CHECK(again.grasp(0,{2,0,0}) && NativeGrabCheck::corpse(again.grab,0));
    // props are unaffected by the switch
    fixture();Rig p;p.grab.configure_corpse(off);CHECK(p.grasp(0,{prop_x,0,0}) && NativeGrabCheck::bone(p.grab,0)==-1);
}
void two_hands_two_limbs_independent() {
    fixture();Rig r;GrabTuning tuning;tuning.strength_scale=.5;NativeGrabCheck::tune(r.grab,tuning);   // weak hands: the cap binds on both writes
    const Vec3 left{2,0,0},right{3.2,0,0};   // bone 10 and bone 16: two limbs of one guard
    for(int i=0;i<18;++i) {r.step(0,left,0,20);r.step(1,right,0,20);r.grab.focus_point(0,r.t);r.grab.focus_point(1,r.t);}
    r.step(0,left,.9f);r.step(1,right,.9f,1);
    CHECK(r.grab.holding(0) && r.grab.holding(1) && NativeGrabCheck::bone(r.grab,0)==10 && NativeGrabCheck::bone(r.grab,1)==16);
    const auto before=writes.size();
    r.step(0,left+Vec3{.3,0,0},.9f);r.step(1,right+Vec3{.3,0,0},.9f);
    CHECK(writes.size()>=before+2);
    // each hand's mass share is half; each write is capped by its own limb (class kg of bone 10 and bone 16)
    double combined=0,sum=0;int limb_writes=0;
    for(std::size_t i=before;i<writes.size();++i) {
        const auto& w=writes[i];const double dv=std::hypot(w.linear[0],std::hypot(w.linear[1],w.linear[2]));
        const int bone=w.handle==handle_of(a_first,10)?10:w.handle==handle_of(a_first,16)?16:-1;
        if(bone<0) continue;
        const double cap=expected_cap(ragdoll_class_kg[static_cast<std::size_t>(bone)],.1,tuning,.5);
        CHECK(dv<=cap*1.01 && dv>=cap*.98);   // the cap binds, at the half-share mass (the full share would cap lower)
        combined+=dv;sum+=cap;++limb_writes;
    }
    CHECK(limb_writes==2 && combined<=sum*1.01);
}
void prop_hold_capped_old_release() {
    // A 2 kg prop and a slow hand: the cap does not bind, so the write is the old uncapped motor's, bit for bit.
    fixture(0x08,0x08,true,.18f);Rig r;
    const Vec3 start{prop_x,0,0},slow{prop_x+.01,0,0};
    PhysicalGrab legacy;   // the pre-change motor, fed the same hand on the same ticks
    auto both=[&](Vec3 p,float g,UINT64 dt_ms) {r.step(0,p,g,dt_ms);r.grab.focus_point(0,r.t);legacy.sample(r.pose(p),g,true,1,r.t);};
    for(int i=0;i<18;++i) both(start,0,20);
    both(start,.9f,100);
    REQUIRE(r.grab.holding(0));CHECK(NativeGrabCheck::capped(r.grab,0) && !NativeGrabCheck::corpse(r.grab,0));
    const auto body=native_rigid_body(read,base,prop);REQUIRE(body.has_value());
    CHECK(legacy.bind(body->pose,body->contact(start),grab_acquire_distance));
    const auto before=writes.size();
    both(slow,.9f,30);
    const auto old_motor=legacy.motor(body->pose,true,r.t,r.t);REQUIRE(old_motor && !old_motor->release);
    REQUIRE(writes.size()==before+1);
    const auto& w=writes.back();
    CHECK(std::fabs(w.linear[0]-static_cast<float>(old_motor->linear.x))<1e-4f && std::fabs(w.linear[1]-static_cast<float>(old_motor->linear.y))<1e-4f &&
          std::fabs(w.linear[2]-static_cast<float>(old_motor->linear.z))<1e-4f && w.linear[0]>0);
    // A 12 kg prop and a fast hand: the cap binds, the write is the capped value.
    fixture(0x08,0x08,true,.325f);Rig heavy;
    CHECK(heavy.grasp(0,{prop_x,0,0}));
    const auto heavy_before=writes.size();
    heavy.step(0,{prop_x+.4,0,0},.9f,100);
    REQUIRE(writes.size()==heavy_before+1);
    const GrabTuning tuning=GrabTuning{}.prop_view();const double cap=expected_cap(12.,.1,tuning);   // props keep the v608 scale (v609 G6)
    CHECK(std::fabs(writes.back().linear[0])<=cap*1.01 && std::fabs(writes.back().linear[0])>cap*.5);
    // Beyond 1.5 m the old release stays (a prop: throw, no cooldown).
    fixture(0x08,0x08,true,.18f);Rig distant;
    CHECK(distant.grasp(0,{prop_x,0,0}));
    for(int i=1;i<=3 && distant.grab.holding(0);++i) distant.step(0,{prop_x+.45*i,0,0},.9f,300);
    for(int i=0;i<6 && distant.grab.holding(0);++i) distant.step(0,{prop_x+1.8,0,0},.9f,100);
    CHECK(!distant.grab.holding(0));
}
void tow_follows_and_limb_saturation_gates_it() {
    // Light limb, strong hand (cap does not bind): the pelvis is towed after the limb.
    {
        fixture();Rig r;GrabTuning tuning;tuning.strength_scale=10;NativeGrabCheck::tune(r.grab,tuning);
        const Vec3 foot{2.6,0,0};
        REQUIRE(r.grasp(0,foot));
        for(int i=1;i<=4;++i) r.step(0,foot+Vec3{.02*i,0,0},.9f,50);
        CHECK(r.grab.tows_.load()>=1);
        bool pelvis_written=false;for(const auto& w:writes) pelvis_written=pelvis_written||w.handle==handle_of(a_first,0);
        CHECK(pelvis_written);
    }
    // Saturated limb (the cap binds): the tow stands down and counts it.
    {
        fixture();Rig r;GrabTuning tuning;tuning.strength_scale=.5;NativeGrabCheck::tune(r.grab,tuning);
        const Vec3 foot{2.6,0,0};
        REQUIRE(r.grasp(0,foot));
        for(int i=1;i<=6;++i) r.step(0,foot+Vec3{.3*i,0,0},.9f,100);
        CHECK(r.grab.tows_.load()==0 && !has(r.json(),"\"tow_disabled_saturated\":0"));
    }
}
void mass_hangs_on_a_lifted_limb() {
    fixture();Rig r;const Vec3 calf{2.4,0,0};
    REQUIRE(r.grasp(0,calf));r.step(0,calf+Vec3{0,0,.05},.9f,50);
    const double flat=NativeGrabCheck::mass(r.grab,0);
    // the pelvis rises 0.3 m: a quarter of the rest of the body (lift_fraction) hangs from the limb
    put(record_of(a_first,0)+56,0.3f);   // transform z of body 0 (f[14])
    r.step(0,calf+Vec3{0,0,.1},.9f,50);
    const GrabTuning tuning;const double lifted=NativeGrabCheck::mass(r.grab,0);
    CHECK(lifted>flat+tuning.lift_fraction*(body_class_total_kg-ragdoll_class_kg[0])*.99-1e-9 || lifted>flat+10);
}
void contact_push_blocked_during_lockout_and_latch() {
    fixture();Rig r;const Vec3 wrist{2,0,0};
    CHECK(!NativeGrabCheck::contact_blocked(r.grab,guard_a,true));
    REQUIRE(r.grasp(0,wrist));r.step(0,wrist+Vec3{.05,0,0},.9f);
    put(motion_at(a_motion,0)+0x40,std::array<float,3>{13,0,0});
    r.step(0,wrist+Vec3{.1,0,0},.9f);   // runaway: released and locked
    CHECK(has(r.json(),"\"runaway\":1"));
    CHECK(NativeGrabCheck::contact_blocked(r.grab,guard_a,true));       // the open hand must not push the locked guard
    CHECK(!NativeGrabCheck::contact_blocked(r.grab,guard_b,true));      // another guard is free
    CHECK(!NativeGrabCheck::contact_blocked(r.grab,prop,false));        // props are never the lockout's business
    r.t+=1200;
    FaultLatch::trip();   // a ragdoll write faulted anywhere: guards are left alone, props are not
    CHECK(NativeGrabCheck::contact_blocked(r.grab,guard_b,true) && !NativeGrabCheck::contact_blocked(r.grab,prop,false));
    FaultLatch::reset_for_tests();
}
void hold_anchor_never_waits_for_the_dispatcher_mutex() {
    fixture();Rig r;
    REQUIRE(r.grasp(0,{prop_x,0,0}));
    const auto expected=r.grab.hold_anchor(0);CHECK(expected.active && expected.tick==r.t);
    std::unique_lock<std::mutex> busy(NativeGrabCheck::mutex(r.grab));   // a dispatcher pass is in progress
    auto got=std::async(std::launch::async,[&]{return r.grab.hold_anchor(0);});
    const bool in_time=got.wait_for(std::chrono::milliseconds(500))==std::future_status::ready;
    CHECK(in_time);
    busy.unlock();
    const auto view=got.get();CHECK(view.active && view.tick==expected.tick && view.anchor.x==expected.anchor.x);
}
void motion_slot_owner_and_baseline() {
    {   // the same slot, but the entry now belongs to another body (an ABA recycle): no write, hold ends
        fixture();Rig r;const Vec3 wrist{2,0,0};
        REQUIRE(r.grasp(0,wrist));r.step(0,wrist+Vec3{.05,0,0},.9f);
        const auto before=writes.size();
        put(motion_at(a_motion,10)+0x28,std::uint32_t{999});
        r.step(0,wrist+Vec3{.1,0,0},.9f);
        CHECK(!r.grab.holding(0) && writes.size()==before && has(r.json(),"\"lifecycle\":[1,0]"));
    }
    {   // no baseline at the bind (the motion array was unreadable): the corpse hold is refused, nothing written; the array coming back does not adopt one
        fixture();Rig r;const Vec3 wrist{2,0,0};
        put(world+0x120,std::uintptr_t{0});
        r.grasp(0,wrist);
        put(world+0x120,motions);
        const auto before=writes.size();
        r.step(0,wrist+Vec3{.1,0,0},.9f);r.step(0,wrist+Vec3{.2,0,0},.9f);
        CHECK(!r.grab.holding(0) && writes.size()==before);
    }
}
void one_hand_lift_not_pinned_by_tow() {
    fixture();Rig r;GrabTuning tuning;tuning.strength_scale=10;NativeGrabCheck::tune(r.grab,tuning);
    const Vec3 foot{2.6,0,0};
    REQUIRE(r.grasp(0,foot));
    put(motion_at(a_motion,0)+0x40,std::array<float,3>{0,0,2});   // the pelvis is rising at 2 m/s (the body is being lifted)
    for(int i=1;i<=4;++i) r.step(0,foot+Vec3{.02*i,0,0},.9f,50);
    int pelvis_writes=0;
    for(const auto& w:writes) if(w.handle==handle_of(a_first,0)) {++pelvis_writes;CHECK(std::fabs(w.linear[2]-2.f)<1e-5f);}   // vertical speed left as it was
    CHECK(pelvis_writes>=1);
}
void two_hands_one_runaway_counts_once() {
    fixture();Rig r;
    const Vec3 left{2,0,0},right{3.2,0,0};
    for(int i=0;i<18;++i) {r.step(0,left,0,20);r.step(1,right,0,20);r.grab.focus_point(0,r.t);r.grab.focus_point(1,r.t);}
    r.step(0,left,.9f);r.step(1,right,.9f,1);
    REQUIRE(r.grab.holding(0) && r.grab.holding(1));
    put(motion_at(a_motion,0)+0x40,std::array<float,3>{13,0,0});
    r.step(0,left,.9f,20);r.step(1,right,.9f,20);
    CHECK(!r.grab.holding(0) && !r.grab.holding(1));
    CHECK(has(r.json(),"\"runaway\":1") && has(r.json(),"\"runaway\":[1,0]") && has(r.json(),"\"lifecycle\":[0,1]"));
}
void install_knobs_load_and_switch() {
    namespace fs=std::filesystem;
    const auto root=fs::temp_directory_path()/"d2vr_corpse_knobs";fs::remove_all(root);fs::create_directories(root/"build");
    {
        const auto none=NativeGrab::corpse_knobs_from(root);
        CHECK(none.options.enabled && !none.options.from_file && none.options.tuning.tow && none.options.tuning.strength_scale==15 && none.options.tuning.prop_strength_scale==6 && !none.kill.legacy_corpse_strength && !none.kill.no_posts);
    }
    {std::ofstream f(root/"build"/"grab-tuning.cfg");f<<"strength_scale 5\ntow 0\naxial_n 250\n";}
    {
        const auto k=NativeGrab::corpse_knobs_from(root);
        CHECK(k.options.from_file && k.options.tuning.strength_scale==5 && !k.options.tuning.tow && k.options.tuning.axial_n==250 && k.options.enabled);
        NativeGrab grab;grab.configure_corpse(k.options);   // what install() does with it
        CHECK(has(grab.corpse_diagnostics().json(),"\"tuning\":\"file\"") && has(grab.corpse_diagnostics().json(),"\"tow_enabled\":false"));
    }
    {std::ofstream f(root/"build"/"grab-tuning.cfg");f<<"strength_scale 99\n";}   // out of bounds: defaults and a message
    {
        const auto k=NativeGrab::corpse_knobs_from(root);
        CHECK(!k.options.from_file && k.options.tuning.strength_scale==15 && !k.loaded.error.empty());
    }
    {std::ofstream f(root/"build"/"no-corpse-grab");}{std::ofstream f(root/"build"/"no-ragdoll-posts");}
    {
        const auto k=NativeGrab::corpse_knobs_from(root);
        CHECK(!k.options.enabled && k.kill.no_posts && k.kill.no_corpse_grab);
    }
    // H1: build/legacy-corpse-strength restores the pre-H1 caps (3x), also over a valid cfg's strength_scale; the rest of the cfg still applies.
    fs::remove(root/"build"/"no-corpse-grab");fs::remove(root/"build"/"no-ragdoll-posts");
    {std::ofstream f(root/"build"/"legacy-corpse-strength");}
    {
        const auto k=NativeGrab::corpse_knobs_from(root);
        CHECK(k.kill.legacy_corpse_strength && k.options.tuning.strength_scale==3 && k.options.tuning.prop_strength_scale==6 && k.options.enabled);
        std::ofstream(root/"build"/"grab-tuning.cfg")<<"strength_scale 5\naxial_n 250\n";
        const auto c=NativeGrab::corpse_knobs_from(root);
        CHECK(c.options.from_file && c.options.tuning.strength_scale==3 && c.options.tuning.prop_strength_scale==6 && c.options.tuning.axial_n==250);
    }
    fs::remove_all(root);
}
}   // anonymous namespace
namespace d2vr {
struct WakeBudgetCheck {
    static bool wake(unsigned side,Vec3 hand) {NativeWeaponHands::Request r{};r.op=NativeWeaponHands::Op::wake;r.side=side;r.pos={float(hand.x),float(hand.y),float(hand.z)};return NativeWeaponHands::wake(base,wide_read,r);}
    static void retries(std::uint64_t now) {NativeWeaponHands::service_wake_retries(base,wide_read,now);}
    static bool ragdoll(std::uintptr_t npc) {return NativeWeaponHands::ragdoll(base,wide_read,npc,{0,0,0});}
    static void hooks(void* (*post)(unsigned,unsigned,std::uint32_t),std::uint64_t (*clock)()) {NativeWeaponHands::post_hook=post;NativeWeaponHands::clock_hook=clock;}
    static void hold_down() {NativeWeaponHands::hold_down(base,wide_read);}
    static void reset() {NativeWeaponHands::wake_pending_={};NativeWeaponHands::wake_refused_[0]=NativeWeaponHands::wake_refused_[1]=false;}
    // the registry is a 0x3fff-slot array read in one piece; unmapped slots read as empty
    static bool wide_read(std::uintptr_t at,void* out,std::size_t size) {
        const std::uintptr_t registry=game+0x3130b0,end=registry+0x3fff*8;
        if(at>=registry && at+size<=end) {for(std::size_t i=0;i<size;++i) {const auto it=bytes.find(at+i);static_cast<unsigned char*>(out)[i]=it==bytes.end()?0:it->second;}return true;}
        return read(at,out,size);
    }
};
}
namespace {
std::vector<std::pair<unsigned,std::uint32_t>> posted;std::array<unsigned char,0x20> post_buffer{};std::uint64_t fake_clock=0;
void* post_stub(unsigned msg,unsigned,std::uint32_t target) {posted.emplace_back(msg,target);return post_buffer.data();}
std::uint64_t clock_stub() {return fake_clock;}
struct WakeRig {
    CorpseDiagnostics diag;
    // a settled corpse at x0 (its ragdoll bodies keyframed: they do not read dynamic), the hand 0.2 m beside it
    WakeRig() {
        fixture(0x08,0x08,false);posted.clear();WakeBudgetCheck::reset();
        WakeBudgetCheck::hooks(post_stub,clock_stub);NativeWeaponHands::wake_diag=&diag;NativeWeaponHands::wake_lockout=nullptr;
        NativeWeaponHands::post_budget.disable(false);
    }
    ~WakeRig() {WakeBudgetCheck::hooks(nullptr,nullptr);NativeWeaponHands::wake_diag=nullptr;NativeGrabCheck::instance(nullptr);}
    void corpse(std::uintptr_t npc,std::uint32_t id,std::uint32_t first,double x0,bool dynamic,unsigned char state=0x08) {
        build_guard(npc,id,0x4010000+first*0x100,0x4020000+first*0x100,0x4030000+first*0x100,0x4040000+first*0x100,first,first+5,x0,state);
        put(0x4040000+first*0x100+0x1860,std::array<float,3>{float(x0),0,0});
        if(!dynamic) for(int b=0;b<17;++b) put(record_of(first,b)+0x44,std::uint32_t{0xce});
    }
    std::size_t posts() const {return posted.size();}
};
void wake_posts_once_per_squeeze() {
    WakeRig w;fake_clock=500000;w.corpse(guard_a,0x50005,0,0,false);
    const Vec3 hand{.2,0,.5};
    CHECK(WakeBudgetCheck::wake(0,hand) && w.posts()==1 && posted[0].first==0xb9 && posted[0].second==0x50005);
    fake_clock+=100;WakeBudgetCheck::wake(0,hand);fake_clock+=300;WakeBudgetCheck::wake(1,hand);   // more squeezes inside the second: no post
    CHECK(w.posts()==1);
    const auto json=w.diag.json();
    CHECK(has(json,"\"event_posts_b9\":1") && has(json,"\"event_posts_suppressed\":2") && has(json,"\"wake_attempts\":3") && has(json,"\"wake_refused\":0"));
}
void already_dynamic_corpse_posts_nothing() {
    WakeRig w;fake_clock=600000;w.corpse(guard_a,0x60006,0,0,true);
    CHECK(WakeBudgetCheck::wake(0,{.2,0,.5}));
    CHECK(w.posts()==0 && has(w.diag.json(),"\"wake_attempts\":0"));
}
void retry_after_one_second_then_refused_with_flag() {
    WakeRig w;fake_clock=700000;w.corpse(guard_a,0x70007,0,0,false);
    WakeBudgetCheck::wake(0,{.2,0,.5});CHECK(w.posts()==1);
    for(int step=1;step<=30;++step) {   // services every 100 ms for 3 s
        fake_clock+=100;WakeBudgetCheck::retries(fake_clock);
        if(step==9) CHECK(w.posts()==1);                                        // 0.9 s: nothing yet
        if(step==10) CHECK(w.posts()==2);                                       // 1.0 s: the one retry
        if(step==19) CHECK(w.posts()==2 && !NativeWeaponHands::take_wake_refusal(0));
    }
    CHECK(w.posts()==2);                                                        // nothing after the retry, nothing after the refusal
    CHECK(NativeWeaponHands::take_wake_refusal(0) && !NativeWeaponHands::take_wake_refusal(0) && !NativeWeaponHands::take_wake_refusal(1));
    CHECK(has(w.diag.json(),"\"wake_refused\":1") && has(w.diag.json(),"\"event_posts_b9\":2"));
    WakeBudgetCheck::wake(0,{.2,0,.5});CHECK(w.posts()==2 && NativeWeaponHands::take_wake_refusal(0));   // a later squeeze on the same corpse is refused too, with the flag
}
void wake_stops_when_the_body_turns_dynamic() {
    WakeRig w;fake_clock=800000;w.corpse(guard_a,0x80008,0,0,false);
    WakeBudgetCheck::wake(0,{.2,0,.5});
    for(int b=0;b<17;++b) put(record_of(0,b)+0x44,std::uint32_t{0x82});   // the post worked
    for(int step=1;step<=30;++step) {fake_clock+=100;WakeBudgetCheck::retries(fake_clock);}
    CHECK(w.posts()==1 && !NativeWeaponHands::take_wake_refusal(0));
}
void retry_skips_recycled_entity() {
    // The corpse's block stays mapped with its old id and state (a level change or a recycled registry slot), but the registry no longer
    // points at it: the 1 s retry must drop the entry and post nothing, and a later squeeze starts a fresh budget entry.
    WakeRig w;fake_clock=1200000;w.corpse(guard_a,0x120012,0,0,false);
    WakeBudgetCheck::wake(0,{.2,0,.5});CHECK(w.posts()==1);
    put(game+0x3130b0+(0x120012u>>16)*8,std::uintptr_t{0});                  // the slot was emptied / handed to another entity
    for(int step=1;step<=30;++step) {fake_clock+=100;WakeBudgetCheck::retries(fake_clock);}
    CHECK(w.posts()==1 && !NativeWeaponHands::take_wake_refusal(0));          // no retry post, no refusal haptic for an entity that is gone
    put(game+0x3130b0+(0x120012u>>16)*8,guard_b);                              // another entity now owns the slot
    WakeRig w2;fake_clock=1300000;w2.corpse(guard_a,0x130013,0,0,false);
    WakeBudgetCheck::wake(0,{.2,0,.5});CHECK(w2.posts()==1);
    put(game+0x3130b0+(0x130013u>>16)*8,guard_b);
    for(int step=1;step<=30;++step) {fake_clock+=100;WakeBudgetCheck::retries(fake_clock);}
    CHECK(w2.posts()==1);
}
void lockout_two_threads_stress() {
    // Deterministic part: a locked entity suppresses the wake post through the real lockout while another thread churns the table.
    EntityLockout lockout;
    WakeRig w;fake_clock=1400000;w.corpse(guard_a,0x140014,0,0,false);
    NativeWeaponHands::wake_lockout=&lockout;
    const double t=seconds_from_ms(fake_clock);
    CHECK(lockout.lock(0x140014,t) && lockout.locked(0x140014,t) && !lockout.locked(0x140014,t+EntityLockout::seconds+.01));
    std::atomic<bool> stop{false};std::atomic<std::uint64_t> ops{};
    std::thread writer([&]{                                                   // the physics thread: refresh the runaway lock, churn others
        std::uint32_t other=1000;
        while(!stop.load()) {lockout.lock(0x140014,t);lockout.lock(other++%40+1000,t);ops.fetch_add(1);}
    });
    std::atomic<int> torn{};
    std::thread reader([&]{                                                   // a status / contact thread
        for(int i=0;i<200000;++i) {if(!lockout.locked(0x140014,t+.5)) ++torn;(void)lockout.refused();}
    });
    for(int i=0;i<500;++i) {WakeBudgetCheck::wake(0,{.2,0,.5});if(i%50==0) WakeBudgetCheck::retries(fake_clock+1500);}   // the game thread
    reader.join();stop=true;writer.join();
    CHECK(torn==0);                                                           // a locked entity never reads as unlocked
    CHECK(w.posts()==0);                                                      // and the wake/retry path never posted for it
    CHECK(ops.load()>0);
    // Deterministic interleaving: clear() racing with queries ends with everything unlocked.
    lockout.clear();CHECK(!lockout.locked(0x140014,t) && lockout.refused()==0);
    NativeWeaponHands::wake_lockout=nullptr;
}
void kill_switch_blocks_wake_and_legacy_yank_posts() {
    WakeRig w;fake_clock=900000;w.corpse(guard_a,0x90009,0,0,false);
    NativeWeaponHands::post_budget.disable(true);
    CHECK(WakeBudgetCheck::wake(0,{.2,0,.5}) && w.posts()==0);
    // a living guard (state 0): the legacy yank's ragdoll post is blocked too
    fixture(0x00,0x00,false);build_guard(guard_b,0xa000a,0x4110000,0x4120000,0x4130000,0x4140000,b_first,b_motion,20,0x00);
    CHECK(!WakeBudgetCheck::ragdoll(guard_b) && w.posts()==0);
    NativeWeaponHands::post_budget.disable(false);
    CHECK(WakeBudgetCheck::ragdoll(guard_b) && w.posts()==1);   // control: with the switch off the same call posts
}
void corpse_never_reposted_on_400ms_timer() {
    WakeRig w;fake_clock=1000000;w.corpse(guard_a,0xb000b,0,0,true);
    NativeGrab grab;NativeGrabCheck::setup(grab);
    NativeGrabCheck::instance(&grab);
    // hold the corpse's wrist with a real lane of this grab
    NativeGrab& g=grab;
    auto owner=*native_player_camera(read,base,camera);
    auto step=[&](Vec3 p,float raw,UINT64 dt) {static UINT64 t=3000000;t+=dt;WorldTrackedPose hand{p,{{{1,0,0},{0,1,0},{0,0,1}}}};
        g.publish(0,owner,hand,raw,1,t,true,false,t);g.filter_native_grip(0,raw,1,t,true,true,false);NativeGrabCheck::consume(g,0,t);if(raw<.35f) g.focus_point(0,t);};
    for(int i=0;i<18;++i) step({2,0,0},0,20);
    step({2,0,0},.9f,100);
    CHECK(g.holding(0) && g.held_ragdoll(0)==guard_a);
    for(int i=0;i<5;++i) {WakeBudgetCheck::hold_down();fake_clock+=500;}
    CHECK(w.posts()==0);   // a dead body is never re-posted by the 400 ms hold-down timer
    NativeGrabCheck::instance(nullptr);
}
void ceiling_two_posts_per_second_across_entities() {
    WakeRig w;fake_clock=1100000;
    const double xs[3]{0,20,40};const std::uintptr_t npcs[3]{guard_a,guard_b,0x4300000};
    for(int i=0;i<3;++i) w.corpse(npcs[i],0xc000c+(i<<16),i*0x20,xs[i],false);
    for(int i=0;i<3;++i) WakeBudgetCheck::wake(0,{xs[i]+.2,0,.5});
    CHECK(w.posts()==2);
    CHECK(has(w.diag.json(),"\"event_posts_b9\":2") && has(w.diag.json(),"\"event_posts_suppressed\":1"));
    fake_clock+=1000;WakeBudgetCheck::wake(0,{40.2,0,.5});CHECK(w.posts()==3);   // a second later the deferred one goes through
}
void lockout_never_evicts_active_and_yank_consults_it() {
    {   // the table: 16 active locks cannot be displaced by a 17th
        EntityLockout l;for(std::uint32_t i=1;i<=16;++i) CHECK(l.lock(i,100));
        CHECK(!l.lock(77,100.1) && l.refused()==1 && l.locked(1,100.5) && l.locked(16,100.5) && !l.locked(77,100.5));
    }
    {   // a runaway locked guard is not yanked back into a legacy hold; unlocked he is (control). The corpse envelope is off so only the yank path can bind.
        for(const bool locked:{true,false}) {
            fixture(0x08);Rig r;CorpseOptions off;off.enabled=false;r.grab.configure_corpse(off);
            const Vec3 wrist{2,0,0};
            if(locked) r.grab.lockout().lock(id_a,seconds_from_ms(r.t+300));
            for(int i=0;i<4;++i) r.step(0,wrist,0,20);
            NativeGrab::yanked(0,guard_a,r.t+5000);
            r.step(0,wrist,.9f,50);r.step(0,wrist,.9f,50);
            CHECK(r.grab.holding(0)==!locked);
            NativeGrab::yanked(0,0,0);
        }
    }
    {   // a held living guard (legacy motor) who dies while locked out is dropped, not promoted
        fixture(0x80);Rig r;
        r.grab.configure_corpse(CorpseOptions{});
        NativeGrab::yanked(0,guard_a,r.t+60000);
        const Vec3 wrist{2,0,0};
        for(int i=0;i<4;++i) r.step(0,wrist,0,20);
        r.step(0,wrist,.9f,50);r.step(0,wrist,.9f,50);
        NativeGrab::yanked(0,0,0);
        REQUIRE(r.grab.holding(0) && !NativeGrabCheck::corpse(r.grab,0));
        r.grab.lockout().lock(id_a,seconds_from_ms(r.t+10));
        put(guard_a+0xd1,static_cast<unsigned char>(0x08));
        const auto before=writes.size();
        r.step(0,wrist+Vec3{.1,0,0},.9f,50);
        CHECK(!r.grab.holding(0) && writes.size()==before && has(r.json(),"\"binds\":[0,0]"));   // dropped before a corpse bind was counted
    }
}
void source_routing() {
    const auto src=NativeGrabCheck::source();
    CHECK(!src.empty());
    auto count=[&](const std::string& piece) {int c=0;for(auto at=src.find(piece);at!=std::string::npos;at=src.find(piece,at+1)) ++c;return c;};
    // every velocity write of the capped lane goes through write(...); the fault latch trips in the fault branches only
    CHECK(count("write_velocity(")==6);   // the definition, write(), tow(), apply_poke(), apply_contact() and B4's apply_release_depen() (safe release, one validated write)
    {std::ifstream f("src/native_weapon_hands.hpp");std::stringstream ss;ss<<f.rdbuf();const auto w=ss.str();
     CHECK(w.find("hold_down(base,read);\n        service_wake_retries(base,read,clock_ms());")!=std::string::npos);}   // the retry service runs from the game-thread service, beside hold_down
    CHECK(count("corpse_knobs_from(root)")==1 && count("configure_corpse(knobs.options)")==1 && count("contact_.write_blocked=")==1);   // write(), tow(), apply_poke(), apply_contact() and the definition
    CHECK(count("FaultLatch::trip()")==1 && count("trip_fault_latch()")>=3);
    const auto lock=src.find("lockout_.locked(body.entity_id,now_s)"),motor=src.find("lane.grab.corpse_motor(");
    CHECK(lock!=std::string::npos && motor!=std::string::npos && lock<motor);
}
}
int main() {
    dead_guard_wrist_binds_and_write_is_capped();
    collar_binds_dead_not_awake();
    open_fist_writes_nothing();
    prop_still_throws_on_open_fist();
    state_changed_releases_no_write();
    state_change_and_open_fist_same_tick();
    world_change_during_lag_release();
    lag_release_without_world_change();
    runaway_seeded_releases_no_counter_write();
    runaway_wins_over_open_fist_same_tick();
    fault_latch_blocks_further_ragdoll_writes();
    readback_garbage_drops_hold_without_write();
    kill_switch_disables_corpse_candidates();
    two_hands_two_limbs_independent();
    torso_reach_catches();forearm_preferred_over_torso_at_equal_distance();head_is_candidate_with_known_radius();collar_needs_squeeze_rule();
    prop_hold_capped_old_release();
    tow_follows_and_limb_saturation_gates_it();
    mass_hangs_on_a_lifted_limb();
    contact_push_blocked_during_lockout_and_latch();
    hold_anchor_never_waits_for_the_dispatcher_mutex();
    motion_slot_owner_and_baseline();
    one_hand_lift_not_pinned_by_tow();
    two_hands_one_runaway_counts_once();
    install_knobs_load_and_switch();
    wake_posts_once_per_squeeze();
    already_dynamic_corpse_posts_nothing();
    retry_after_one_second_then_refused_with_flag();
    wake_stops_when_the_body_turns_dynamic();
    kill_switch_blocks_wake_and_legacy_yank_posts();
    corpse_never_reposted_on_400ms_timer();
    ceiling_two_posts_per_second_across_entities();
    retry_skips_recycled_entity();
    lockout_two_threads_stress();
    lockout_never_evicts_active_and_yank_consults_it();
    source_routing();
    if(failures) {std::printf("%d check(s) failed\n",failures);return 1;}
    std::printf("corpse_hold_check ok\n");
    return 0;
}
