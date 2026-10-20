#include "ragdoll_safety.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
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
    lockout_1s();fault_latch_sticks();kill_switches_read();
    std::puts("ragdoll_safety_check OK");
    return 0;
}
