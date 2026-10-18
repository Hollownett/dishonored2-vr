// Physical hands Task 5: the pure part of the hand publication's clamp feed (src/hand_clamp_feed.hpp) and the clamp's
// re-acquire flag. dxgi_probe.cpp's routing of these is guarded by tests/hand_clamp_routing_check.py.
#include "hand_clamp_feed.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
using namespace d2vr;
#define CHECK(x) do{if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)

namespace {
constexpr double dt=1./90;
WorldTrackedPose pose_at(Vec3 p) {return {p,{{{1,0,0},{0,1,0},{0,0,1}}}};}

// A wall at x = 1 (normal -x, entity 7) answered like a closest-hit sphere cast.
std::optional<ClampHit> wall_cast(const ClampSweep& s) {
    if(!s.valid) return std::nullopt;
    const double s0=1-s.from.x,s1=1-s.to.x;
    if(s0<s.radius) return ClampHit{{1,s.from.y,s.from.z},{-1,0,0},7,3,true};
    if(s1>=s.radius) return std::nullopt;
    const double t=(s0-s.radius)/(s0-s1);const Vec3 c=s.from+(s.to-s.from)*t;
    return ClampHit{{1,c.y,c.z},{-1,0,0},7,3,false};
}

void yields() {
    CHECK(!clamp_yields(0,0,0,false) && !clamp_yields(1,0,0,false));
    CHECK(clamp_yields(0,1,0,false) && !clamp_yields(1,1,0,false));   // an authored left arm
    CHECK(clamp_yields(1,2,0,false) && !clamp_yields(0,2,0,false));
    CHECK(clamp_yields(0,3,0,false) && clamp_yields(1,3,0,false));    // an authored camera: both arms
    CHECK(clamp_yields(1,0,2,false) && !clamp_yields(0,0,2,false));   // the native carry's side only
    CHECK(clamp_yields(0,0,0,true) && clamp_yields(1,0,0,true));      // a physical hold
    CHECK(clamp_yields(2,0,0,false));                                 // no such side
}

void posts() {
    for(std::uint64_t f=0;f<8;++f) CHECK(clamp_posts(false,f,0) && clamp_posts(false,f,1));
    unsigned left=0,right=0;
    for(std::uint64_t f=0;f<8;++f) {
        CHECK(clamp_posts(true,f,0)!=clamp_posts(true,f,1));            // never both hands on one frame
        CHECK(clamp_posts(true,f,0)!=clamp_posts(true,f+1,0));          // each hand every other frame
        left+=clamp_posts(true,f,0);right+=clamp_posts(true,f,1);
    }
    CHECK(left==4 && right==4);
}

void routes() {
    const ClampHit a{{1,0,0},{-1,0,0},1},b{{2,0,0},{-1,0,0},2},c{{3,0,0},{-1,0,0},3},d{{4,0,0},{-1,0,0},4};
    ClampInput in{};in.chest_hit=d;
    route_clamp_hits(in,ClampState::free,a,b,c,d);
    CHECK(in.palm_hit && in.palm_hit->entity==1 && !in.chest_hit && in.tip_hit->entity==2 && in.wall_tip_hit->entity==3 && in.exclude_hit->entity==4);
    route_clamp_hits(in,ClampState::contact,a,std::nullopt,std::nullopt,std::nullopt);
    CHECK(in.palm_hit && !in.chest_hit && !in.tip_hit && !in.wall_tip_hit && !in.exclude_hit);
    route_clamp_hits(in,ClampState::released,a,b,c,d);
    CHECK(!in.palm_hit && in.chest_hit && in.chest_hit->entity==1);
    route_clamp_hits(in,ClampState::released,std::nullopt,b,c,d);
    CHECK(!in.palm_hit && !in.chest_hit);
}

void probe_sets() {
    HandProxies proxies{};
    for(std::size_t i=0;i<proxies.size();++i) proxies[i]={{.01*double(i),0,.02},.01+.001*double(i)};
    proxies[3].radius=0;proxies[5].centre.x=std::nan("");        // not usable spheres are left out
    const auto set=clamp_probe_set(proxies,{0,0,.03});
    CHECK(set.valid && set.count==proxies.size()-2 && set.palm_local.z==.03);
    CHECK(set.hand[3].local.x==.04 && set.hand[set.count].radius==0);   // packed; the rest of the capacity is unused
    for(std::size_t i=0;i<set.count;++i) CHECK(clamp_detail::usable(set.hand[i]));
    // The leading probe picks from the set (the zero-radius tail never leads).
    const auto tip=leading_probe(set.hand,pose_at({0,0,0}),{1,0,0},default_clamp_tip());
    CHECK(clamp_detail::same_probe(tip,set.hand[set.count-1]));
    HandProxies none{};CHECK(!clamp_probe_set(none,{}).valid);
    CHECK(!clamp_probe_set(proxies,{std::nan(""),0,0}).valid);
    // Rebuilt only once the palm offset drifts past 1 cm (a probe keeps its identity frame to frame).
    CHECK(!clamp_probe_set_stale(set,{0,0,.035}) && !clamp_probe_set_stale(set,{.0099,0,.03}));
    CHECK(clamp_probe_set_stale(set,{.011,0,.03}) && clamp_probe_set_stale(ClampProbeSet{},{0,0,0}));
    CHECK(!clamp_probe_set_stale(set,{std::nan(""),0,0}));
    // A rebuild keeps the chosen sphere: same index in the new set.
    HandProxies moved=proxies;for(auto& p:moved) p.centre.z+=.015;
    const auto rebuilt=clamp_probe_set(moved,{0,0,.045});
    const auto kept=remap_clamp_probe(set,rebuilt,set.hand[7]);
    CHECK(clamp_detail::same_probe(kept,rebuilt.hand[7]) && !clamp_detail::same_probe(kept,set.hand[7]));
    CHECK(clamp_detail::same_probe(remap_clamp_probe(set,rebuilt,default_clamp_tip()),default_clamp_tip()));
    CHECK(clamp_detail::same_probe(remap_clamp_probe(set,ClampProbeSet{},set.hand[2]),set.hand[2]));
}

void hold_store() {
    ClampReacquireHold hold;
    const auto root=level_frame({0,0,0},{1,0,0});
    hold.note(0,5,root,{.9,.1,1});
    CHECK(hold.hold(0,5,root,{1.1,.1,1},.35) && length(*hold.hold(0,5,root,{1.1,.1,1},.35)-Vec3{.9,.1,1})<1e-12);
    CHECK(!hold.hold(1,5,root,{1.1,.1,1},.35));                    // the other side keeps nothing
    CHECK(!hold.hold(0,6,root,{1.1,.1,1},.35));                    // another epoch (a level load, a re-centre)
    CHECK(!hold.hold(0,5,root,{1.3,.1,1},.35));                    // the controller is far from it: draw the controller
    CHECK(!hold.hold(0,5,root,{std::nan(""),0,0},.35));
    // The kept hand moves with the player (C11): walk 0.5 m along +y and turn 90 degrees.
    const auto turned=level_frame({0,.5,0},{0,1,0});
    const auto carried=hold.hold(0,5,turned,{-.1,1.4,1},.35);
    CHECK(carried && length(*carried-Vec3{-.1,1.4,1})<1e-12);
    hold.clear(0);CHECK(!hold.hold(0,5,root,{1.1,.1,1},.35));
    hold.note(2,5,root,{0,0,0});hold.clear(2);                     // out of range: ignored
}

// The clamp flags its re-acquire frames; held there, the hand never shows inside the wall it rested on before a pause.
void reacquire() {
    HandClamp clamp;ClampReacquireHold keep;const auto root=level_frame({0,0,0},{1,0,0});const Vec3 chest{0,0,1};
    ClampOutput out{};double now=0;std::uint64_t epoch=1;bool started=false;
    const auto step=[&](Vec3 target,bool use_hold) {
        ClampInput in{};in.now=now;in.epoch=epoch;in.root=root;in.target=pose_at(target);in.chest=chest;in.tip_enabled=false;
        if(started) route_clamp_hits(in,out.state,wall_cast(out.palm_sweep),std::nullopt,std::nullopt,std::nullopt);
        out=clamp.update(0,in);started=true;
        Vec3 drawn=out.drawn.position;
        if(use_hold && out.reacquired) if(const auto at=keep.hold(0,epoch,root,target,clamp.config().snap_gap)) drawn=*at;
        clamp.note_drawn(0,root,drawn);keep.note(0,epoch,root,drawn);
        now+=dt;return drawn;
    };
    CHECK(step({.5,0,1},true).x==.5 && out.reacquired);            // the first update
    {const double x=step({.6,0,1},true).x;CHECK(x>=.5 && x<=.6 && !out.reacquired);}
    for(int i=0;i<60;++i) step({1.1,0,1},true);                    // pressed 10 cm into the wall: held at its surface
    CHECK(!out.reacquired && std::abs(out.drawn.position.x-.945)<1e-6 && out.state==ClampState::contact);
    const double rest=out.drawn.position.x;
    // A pause (0.5 s, no updates), the controller still 10 cm in the wall.
    now+=.5;
    const Vec3 first=step({1.1,0,1},true);
    CHECK(out.reacquired && out.drawn.position.x==1.1);            // the clamp re-acquires at the target (Task 2)
    CHECK(std::abs(first.x-rest)<1e-12);                           // the publication holds the hand where it rested
    double deepest=first.x;
    for(int i=0;i<60;++i) {deepest=std::max(deepest,step({1.1,0,1},true).x);CHECK(!out.reacquired);}
    CHECK(deepest<=.95+1e-9 && std::abs(out.drawn.position.x-.945)<1e-6);   // never inside, back at the surface
    CHECK(clamp.missed_notes(0)==0 && clamp.repeated_updates(0)==0);
    // Without the hold the re-acquire frame shows the hand 10 cm inside the wall (what the hold is for).
    now+=.5;CHECK(step({1.1,0,1},false).x==1.1 && out.reacquired);
    // An epoch change re-acquires, and the hand kept from the old epoch is not used.
    for(int i=0;i<10;++i) step({1.1,0,1},true);
    ++epoch;const Vec3 other=step({1.1,0,1},true);
    CHECK(out.reacquired && other.x==1.1);
    // A yield resets the side: the next frame re-acquires.
    {ClampInput in{};in.now=now;in.epoch=epoch;in.root=root;in.target=pose_at({.5,0,1});in.chest=chest;in.yield=true;
     out=clamp.update(0,in);CHECK(out.valid && !out.reacquired && out.drawn.position.x==.5);now+=dt;}
    step({.5,0,1},true);CHECK(out.reacquired);
    step({.5,0,1},true);CHECK(!out.reacquired);
    CHECK(clamp.missed_notes(0)==0 && clamp.repeated_updates(0)==0);
}
} // namespace

int main() {
    yields();posts();routes();probe_sets();hold_store();reacquire();
    std::puts("PASS hand_clamp_feed_check: C9 yields, C14 cadence, answer routing, open-hand probe sets, re-acquire hold");
    return 0;
}
