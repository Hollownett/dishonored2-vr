// Task 10: a corpse held through NativeGrab with the force envelope. Fixture style of native_grab_lifetime_check.cpp (player, camera, world, motion
// array) and native_rigid_body_check.cpp (a guard's 17 ragdoll bodies). Time is synthetic: every tick is passed to update_lane.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../src/native_grab.hpp"
#include <cmath>
#include <fstream>
#include <functional>
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
    put(motions+prop_motion*0x80+0x26,std::uint16_t{0x3f00});put(motions+prop_motion*0x80+0x40,std::array<float,3>{0,0,0});
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
    static void consume(NativeGrab& grab,unsigned side,UINT64 now,std::uintptr_t w=world) {std::lock_guard<std::mutex> lock(grab.mutex_);grab.update_lane(side,w,now);}
    static void collar(NativeGrab& grab,bool on) {grab.collar_grab_=on;}
    static bool corpse(NativeGrab& grab,unsigned side) {std::lock_guard<std::mutex> lock(grab.mutex_);return grab.lanes_[side].corpse;}
    static bool capped(NativeGrab& grab,unsigned side) {std::lock_guard<std::mutex> lock(grab.mutex_);return grab.lanes_[side].capped;}
    static int bone(NativeGrab& grab,unsigned side) {std::lock_guard<std::mutex> lock(grab.mutex_);return grab.lanes_[side].held?grab.lanes_[side].held->bone:-99;}
    static double mass(NativeGrab& grab,unsigned side) {std::lock_guard<std::mutex> lock(grab.mutex_);return grab.lanes_[side].mass;}
    static void tune(NativeGrab& grab,const GrabTuning& t) {grab.tuning_=t;}
    static std::string source() {std::ifstream f("src/native_grab.hpp");std::stringstream ss;ss<<f.rdbuf();return ss.str();}
};
}
namespace {
struct Rig {
    NativeGrab grab;NativePlayerCamera owner{};UINT64 t=1000000;
    Rig() {NativeGrabCheck::setup(grab);const auto found=native_player_camera(read,base,camera);if(!found) {std::printf("no owner camera\n");std::exit(3);}owner=*found;}
    WorldTrackedPose pose(Vec3 p) const {return WorldTrackedPose{p,{{{1,0,0},{0,1,0},{0,0,1}}}};}
    // One hand sample: publish, decide (grip filter), run the dispatcher tick. raw>0 is a full fist (the physical lane sees nothing for a grip alone).
    void step(unsigned side,Vec3 hand,float raw,UINT64 dt_ms=100,std::uintptr_t w=world) {
        t+=dt_ms;
        grab.publish(side,owner,pose(hand),raw,1,t,true,false,t);
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
    // bone 12 (a calf, class 3.4 kg): effective mass 5.4 kg, cap 900 N * (1/30 s) / 5.4 kg = 5.6 m/s per write
    const Vec3 calf{2.4,0,0};
    REQUIRE(r.grasp(0,calf));
    CHECK(NativeGrabCheck::bone(r.grab,0)==12 && NativeGrabCheck::corpse(r.grab,0) && NativeGrabCheck::capped(r.grab,0));
    CHECK(has(r.json(),"\"binds\":[1,0]") && has(r.json(),"\"limb\":1"));
    const auto before=writes.size();
    r.step(0,calf+Vec3{.4,0,0},.9f,100);   // 0.4 m in 100 ms
    REQUIRE(writes.size()==before+1);
    const GrabTuning tuning;const double cap=expected_cap(3.4,.1,tuning);
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
    // prop writes still work
    const Vec3 at{prop_x,0,0};
    REQUIRE(r.grasp(0,at));r.step(0,at+Vec3{.05,0,0},.9f,100);
    CHECK(!writes.empty() && writes.back().handle==handle_of(prop_index,0));
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
    const GrabTuning tuning;const double cap=expected_cap(12.,.1,tuning);
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
void source_routing() {
    const auto src=NativeGrabCheck::source();
    CHECK(!src.empty());
    auto count=[&](const std::string& piece) {int c=0;for(auto at=src.find(piece);at!=std::string::npos;at=src.find(piece,at+1)) ++c;return c;};
    // every velocity write of the capped lane goes through write(...); the fault latch trips in the fault branches only
    CHECK(count("write_velocity(")==5);   // write(), tow(), apply_poke(), apply_contact() and the definition
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
    prop_hold_capped_old_release();
    tow_follows_and_limb_saturation_gates_it();
    mass_hangs_on_a_lifted_limb();
    source_routing();
    if(failures) {std::printf("%d check(s) failed\n",failures);return 1;}
    std::printf("corpse_hold_check ok\n");
    return 0;
}
