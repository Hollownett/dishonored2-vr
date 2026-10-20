#define D2VR_TESTING 1
#include "ragdoll_safety.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>
#include <string>
using namespace d2vr;
#define CHECK(x) do{if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
namespace {
const double nan_value=std::nan("");
using Vels=std::array<std::optional<Vec3>,17>;
std::filesystem::path fresh_dir(const char* name) {
    auto p=std::filesystem::temp_directory_path()/name;
    std::filesystem::remove_all(p);std::filesystem::create_directories(p);
    return p;
}
void once_per_wake() {
    EventPostBudget b;
    CHECK(b.request(1,false,0)==PostDecision::allow);
    CHECK(b.request(1,false,0.5)==PostDecision::retry_wait);
    CHECK(b.posted()==1);CHECK(b.suppressed()==1);
}
void already_dynamic_never_posts() {
    EventPostBudget b;
    CHECK(b.request(1,true,0)==PostDecision::already_dynamic);
    CHECK(b.request(1,true,5)==PostDecision::already_dynamic);
    CHECK(b.posted()==0);
    CHECK(b.request(1,false,6)==PostDecision::allow);   // dynamic reads leave no state behind
}
void retry_after_1s_once_then_refused() {
    EventPostBudget b;
    CHECK(b.request(1,false,0)==PostDecision::allow);
    CHECK(b.request(1,false,0.99)==PostDecision::retry_wait);
    CHECK(b.request(1,false,1.0)==PostDecision::allow);
    CHECK(b.request(1,false,1.5)==PostDecision::refused);
    CHECK(b.request(1,false,5)==PostDecision::refused);
    CHECK(b.posted()==2);CHECK(b.refused()==2);
}
void forget_restarts() {
    EventPostBudget b;
    CHECK(b.request(1,false,0)==PostDecision::allow);
    CHECK(b.request(1,false,1)==PostDecision::allow);
    CHECK(b.request(1,false,2)==PostDecision::refused);
    b.forget(1);
    CHECK(b.request(1,false,3)==PostDecision::allow);
    b.forget(99);   // unknown entity is harmless
}
void ceiling_2_per_second() {
    EventPostBudget b;
    CHECK(b.request(1,false,0)==PostDecision::allow);
    CHECK(b.request(2,false,0)==PostDecision::allow);
    CHECK(b.request(3,false,0)==PostDecision::ceiling);
    CHECK(b.request(3,false,0.99)==PostDecision::ceiling);
    CHECK(b.request(3,false,1.0)==PostDecision::allow);
    CHECK(b.posted()==3);CHECK(b.suppressed()==2);CHECK(b.refused()==0);
}
void flapping_200_cycles_within_ceiling() {
    EventPostBudget b;
    double allowed[400];int n=0;
    for(int i=0;i<200;++i) {
        double t=i*0.3;
        b.forget(7);
        if(b.request(7,false,t)==PostDecision::allow) allowed[n++]=t;
    }
    CHECK(n>=60);   // ~2 per second still gets through
    for(int i=0;i<n;++i) {
        int in_window=0;
        for(int j=0;j<n;++j) if(allowed[j]>=allowed[i]&&allowed[j]<allowed[i]+1.0) ++in_window;
        CHECK(in_window<=2);
    }
    CHECK(b.posted()==static_cast<std::uint64_t>(n));
    CHECK(b.suppressed()==static_cast<std::uint64_t>(200-n));CHECK(b.refused()==0);
}
void disabled_blocks_all() {
    EventPostBudget b;
    b.disable(true);
    CHECK(b.request(1,false,0)==PostDecision::disabled);
    CHECK(b.request(2,true,0)==PostDecision::disabled);
    CHECK(b.posted()==0);
    b.disable(false);
    CHECK(b.request(1,false,1)==PostDecision::allow);
}
void eviction_keeps_working() {
    EventPostBudget b;
    for(std::uint32_t e=0;e<40;++e) CHECK(b.request(e,false,e*2.0)==PostDecision::allow);
}
void runaway_speed_12() {
    Vels v;v[3]=Vec3{12.01,0,0};
    auto r=RunawayDetector::check(v);
    CHECK(r.runaway);CHECK(!r.non_finite);CHECK(std::fabs(r.max_speed-12.01)<1e-9);
    v[3]=Vec3{11.99,0,0};
    CHECK(!RunawayDetector::check(v).runaway);
    v[3]=Vec3{0,0,0};v[16]=Vec3{0,-12.5,0};
    CHECK(RunawayDetector::check(v).runaway);
}
void runaway_non_finite() {
    Vels v;v[0]=Vec3{0,nan_value,0};
    auto r=RunawayDetector::check(v);
    CHECK(r.runaway);CHECK(r.non_finite);
    Vels w;w[5]=Vec3{INFINITY,0,0};
    CHECK(RunawayDetector::check(w).non_finite);
}

// Fake game memory: one flat buffer mapped at fake_base, read through the real native_motion().
constexpr std::uintptr_t fake_base=0x100000;
struct FakeMemory {
    std::vector<unsigned char> bytes=std::vector<unsigned char>(0x4000);
    template<class T> void put(std::uintptr_t at,T v) {std::memcpy(&bytes[at-fake_base],&v,sizeof v);}
    bool operator()(std::uintptr_t at,void* out,std::size_t n) {
        if(at<fake_base||at+n>fake_base+bytes.size()) return false;
        std::memcpy(out,&bytes[at-fake_base],n);return true;
    }
};
NativeRigidBody fake_body(FakeMemory& m,float vx,float vy,float vz) {
    NativeRigidBody b;
    b.record=fake_base;b.world=fake_base+0x1000;
    const std::uintptr_t array=fake_base+0x2000;
    m.put<std::uint32_t>(b.record+0x40,0);
    m.put<std::uintptr_t>(b.world+0x120,array);
    m.put<std::array<float,3>>(array+0x40,{vx,vy,vz});
    m.put<std::uint16_t>(array+0x26,7);
    return b;
}
Vels one_body(FakeMemory& m,const NativeRigidBody& b) {
    Vels v;
    auto motion=native_motion(m,b);
    v[2]=runaway_sample(true,motion);
    return v;
}
void runaway_through_native_motion() {
    FakeMemory m;
    auto ok=fake_body(m,3,0,0);
    CHECK(native_motion(m,ok).has_value());
    CHECK(!RunawayDetector::check(one_body(m,ok)).runaway);
    auto fast=fake_body(m,0,150,0);                       // > 100 m/s: native_motion refuses it
    CHECK(!native_motion(m,fast).has_value());
    auto r=RunawayDetector::check(one_body(m,fast));
    CHECK(r.runaway&&r.non_finite);
    auto bad=fake_body(m,std::nanf(""),0,0);              // non-finite
    CHECK(!native_motion(m,bad).has_value());
    CHECK(RunawayDetector::check(one_body(m,bad)).runaway);
    auto inf=fake_body(m,INFINITY,0,0);
    CHECK(RunawayDetector::check(one_body(m,inf)).runaway);
    auto mid=fake_body(m,0,0,50);                         // valid read, above 12 m/s
    CHECK(RunawayDetector::check(one_body(m,mid)).runaway);
    Vels none; none[2]=runaway_sample(false,std::nullopt);   // unresolved body is ignored
    CHECK(!RunawayDetector::check(none).runaway);
}
void refused_survives_eviction_churn() {
    EventPostBudget b;
    CHECK(b.request(1000,false,0)==PostDecision::allow);
    CHECK(b.request(1000,false,1)==PostDecision::allow);
    CHECK(b.request(1000,false,2)==PostDecision::refused);
    double t=3;
    for(std::uint32_t e=0;e<20;++e,t+=0.5) CHECK(b.request(e,false,t)==PostDecision::allow);
    CHECK(b.request(1000,false,t)==PostDecision::refused);
    CHECK(b.request(1000,false,t+30)==PostDecision::refused);
    CHECK(b.refused()>=3);
}
void full_table_of_young_entries_defers() {
    EventPostBudget b;
    // 16 retry-posted (refused) entities fill the table; none are stale
    double t=0;
    for(std::uint32_t e=0;e<16;++e) {
        t+=1.5;
        CHECK(b.request(e,false,t)==PostDecision::allow);
        CHECK(b.request(e,false,t+1.0)==PostDecision::allow);
        t+=1.0;
    }
    CHECK(b.request(500,false,t+1.5)==PostDecision::ceiling);
    CHECK(b.request(0,false,t+1.5)==PostDecision::refused);
}
void lockout_blocks_posts() {
    EventPostBudget b;EntityLockout l;
    l.lock(1,10);
    CHECK(b.request(1,false,10.5,l)==PostDecision::locked);
    CHECK(b.request(2,false,10.5,l)==PostDecision::allow);
    CHECK(b.posted()==1);
    CHECK(b.request(1,false,11.0,l)==PostDecision::allow);   // lock expired, entity was never consumed
    b.disable(true);
    CHECK(b.request(1,false,11.5,l)==PostDecision::disabled);
}
void kill_switch_fails_closed() {
    auto d=fresh_dir("d2vr_ragdoll_ks_b");
    auto k=KillSwitches::read(d,[](const std::filesystem::path&,std::error_code& ec) {
        ec=std::make_error_code(std::errc::permission_denied);return false;});
    CHECK(k.no_posts&&k.no_corpse_grab);
    k=KillSwitches::read(d,[](const std::filesystem::path&,std::error_code&) {return false;});
    CHECK(!k.no_posts&&!k.no_corpse_grab);
}
void ms_conversion() {
    EventPostBudget b;
    CHECK(b.request(1,false,seconds_from_ms(5000))==PostDecision::allow);
    CHECK(b.request(1,false,seconds_from_ms(5990))==PostDecision::retry_wait);
    CHECK(b.request(1,false,seconds_from_ms(6000))==PostDecision::allow);
    EntityLockout l;l.lock(3,seconds_from_ms(2000));
    CHECK(l.locked(3,seconds_from_ms(2999)));CHECK(!l.locked(3,seconds_from_ms(3000)));
    CHECK(seconds_from_ms(1500)==1.5);
}
void ceiling_deferred_retry_allowed_later() {
    EventPostBudget b;
    CHECK(b.request(1,false,0)==PostDecision::allow);
    CHECK(b.request(2,false,0.125)==PostDecision::allow);
    CHECK(b.request(3,false,0.5)==PostDecision::ceiling);
    CHECK(b.request(3,false,1.0)==PostDecision::allow);      // the t=0 slot aged out
    CHECK(b.request(1,false,1.0625)==PostDecision::ceiling); // retry due, but posts at 0.125 and 1.0 fill the window
    CHECK(b.request(1,false,1.125)==PostDecision::allow);    // the deferred retry is still available
    CHECK(b.request(1,false,3)==PostDecision::refused);
    CHECK(b.posted()==4);CHECK(b.suppressed()==2);CHECK(b.refused()==1);
}
void runaway_ignores_missing_bodies() {
    Vels v;
    auto r=RunawayDetector::check(v);
    CHECK(!r.runaway);CHECK(!r.non_finite);CHECK(r.max_speed==0);
}
void lockout_1s() {
    EntityLockout l;
    CHECK(!l.locked(1,0));
    l.lock(1,10);
    CHECK(l.locked(1,10.99));
    CHECK(!l.locked(1,11.0));
    CHECK(!l.locked(2,10.5));
}
void fault_latch_sticks() {
    FaultLatch::reset_for_tests();
    CHECK(!FaultLatch::tripped());
    FaultLatch::trip();
    CHECK(FaultLatch::tripped());
    FaultLatch::trip();
    CHECK(FaultLatch::tripped());
    FaultLatch::reset_for_tests();
    CHECK(!FaultLatch::tripped());
}
void kill_switches_read() {
    auto d=fresh_dir("d2vr_ragdoll_ks_a");
    auto k=KillSwitches::read(d);CHECK(!k.no_posts&&!k.no_corpse_grab);
    std::ofstream(d/"no-ragdoll-posts")<<"";
    k=KillSwitches::read(d);CHECK(k.no_posts&&!k.no_corpse_grab);
    std::ofstream(d/"no-corpse-grab")<<"";
    k=KillSwitches::read(d);CHECK(k.no_posts&&k.no_corpse_grab);
    std::filesystem::remove(d/"no-ragdoll-posts");
    k=KillSwitches::read(d);CHECK(!k.no_posts&&k.no_corpse_grab);
    k=KillSwitches::read(d/"missing");CHECK(!k.no_posts&&!k.no_corpse_grab);
}
}
int main() {
    once_per_wake();already_dynamic_never_posts();retry_after_1s_once_then_refused();
    forget_restarts();ceiling_2_per_second();flapping_200_cycles_within_ceiling();
    disabled_blocks_all();eviction_keeps_working();
    runaway_speed_12();runaway_non_finite();runaway_ignores_missing_bodies();
    lockout_1s();runaway_through_native_motion();refused_survives_eviction_churn();full_table_of_young_entries_defers();lockout_blocks_posts();kill_switch_fails_closed();ms_conversion();ceiling_deferred_retry_allowed_later();fault_latch_sticks();kill_switches_read();
    std::puts("ragdoll_safety_check OK");
    return 0;
}
