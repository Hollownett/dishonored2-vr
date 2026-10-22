#include "held_clamp.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <limits>
using namespace d2vr;
#define CHECK(x) do{if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)

namespace {
constexpr double dt=1./90,radius=.2;
WorldTrackedPose pose_at(Vec3 p) {return {p,{{{1,0,0},{0,1,0},{0,0,1}}}};}
bool identical(const WorldTrackedPose& a,const WorldTrackedPose& b) {
    return a.position.x==b.position.x && a.position.y==b.position.y && a.position.z==b.position.z;
}
HeldProxies tank() {HeldProxies p{};p.count=1;p.sphere[0]={{0,0,0},radius};return p;}
HeldProxies three() {HeldProxies p{};p.count=3;for(std::size_t i=0;i<3;++i) p.sphere[i]={{0,0,-.5+.5*static_cast<double>(i)},radius};return p;}

// Fake world: a wall at x = 1 (normal -x) and a floor at z = 0 (normal +z), answering a closest-hit sphere cast.
struct World {
    bool wall=true,floor=true;
    std::optional<ClampHit> cast(const ClampSweep& s) const {
        if(!s.valid) return std::nullopt;
        struct P {Vec3 point,normal;bool on;};
        const P planes[2]{{{1,0,0},{-1,0,0},wall},{{0,0,0},{0,0,1},floor}};
        std::optional<ClampHit> best;double best_t=2;
        for(const auto& p:planes) {
            if(!p.on) continue;
            const double s0=dot(s.from-p.point,p.normal),s1=dot(s.to-p.point,p.normal);
            if(s0<s.radius) {if(best_t>=0) {best_t=-1;best=ClampHit{s.from-p.normal*s0,p.normal,7,3,true};}continue;}
            if(s1>=s.radius || best_t<0) continue;
            const double t=(s0-s.radius)/(s0-s1);
            if(t<best_t) {best_t=t;best=ClampHit{s.from+(s.to-s.from)*t-p.normal*s.radius,p.normal,7,3,false};}
        }
        return best;
    }
};
// One held object at 90 Hz; hits answer the previous frame's sweeps.
struct Rig {
    HeldClamp clamp;World world;HeldProxies proxies=tank();bool bounds=true;std::uint64_t epoch=1;unsigned frame=0;
    std::array<ClampSweep,held_proxy_capacity> last_sweeps{};HeldClampOutput out{};
    explicit Rig(HeldClampConfig c={}) : clamp(c) {}
    double now() const {return frame*dt;}
    HeldClampOutput step(const WorldTrackedPose& requested,bool yield=false) {
        HeldClampInput in{};
        in.now=now();in.epoch=epoch;in.requested=requested;in.yield=yield;
        if(bounds) in.proxies=proxies;
        for(std::size_t i=0;i<held_proxy_capacity;++i) in.hits[i]=world.cast(last_sweeps[i]);
        out=clamp.update(in);
        last_sweeps=out.sweeps;
        if(out.valid) clamp.note_published(out.target);
        ++frame;
        return out;
    }
};
// The sphere surface past the plane at x = 1 (positive: inside the wall).
double past_wall(const HeldClampOutput& o) {return o.target.position.x+radius-1;}

void wall_carry(double speed) {
    Rig rig;
    double x=-.5;unsigned after_hit=0;
    const unsigned frames=static_cast<unsigned>(2.2/speed/dt)+120;   // far enough to reach the wall and press on
    for(unsigned f=0;f<frames;++f) {
        x+=speed*dt;
        const auto o=rig.step(pose_at({std::min(x,1.6),0,2}));
        CHECK(o.valid && std::isfinite(o.target.position.x));
        if(rig.frame>1 && o.clamped) ++after_hit;
        if(after_hit>0) {
            CHECK(past_wall(o)<=.01);
            CHECK(past_wall(o)>=-.03);     // stopped at the wall, not backed away from it
        }
    }
    CHECK(after_hit>0);
}
void wall_carry_0p1_to_3ms() {for(double v:{.1,.5,1.,2.,3.}) wall_carry(v);}

void floor_press_stops_at_floor() {
    Rig rig;
    for(unsigned f=0;f<180;++f) {
        const double z=f<30?1.-1.*f*dt*3:-.3;   // descends, then the requested target stays 0.3 m below the floor
        const auto o=rig.step(pose_at({0,0,std::max(z,-.3)}));
        const double bottom=o.target.position.z-radius;
        if(f>=35) CHECK(bottom>=-.01);
        if(f>=35) CHECK(bottom<=.02);          // pressed on the floor, not floating
        CHECK(std::isfinite(o.target.position.z));
    }
}

void corner_two_planes_no_penetration() {
    Rig rig;
    unsigned inside=0;
    for(unsigned f=0;f<60;++f) {   // long enough for every plane to lapse unless it is re-hit
        const auto o=rig.step(pose_at({1.3,0,-.3}));   // inside both the wall and the floor
        CHECK(finite(o.target.position));
        if(f>=3) {
            CHECK(past_wall(o)<=.01);
            CHECK(o.target.position.z-radius>=-.01);
        } else ++inside;
    }
    CHECK(inside==3);
    const auto o=rig.out;
    CHECK(o.clamped);
}

void free_space_untouched() {
    Rig rig;
    for(unsigned f=0;f<30;++f) {
        const auto req=pose_at({-.5+f*.01,.3,2});
        const auto o=rig.step(req);
        CHECK(identical(o.target,req) && !o.clamped && !o.no_bounds && o.valid);
        CHECK(o.shift.x==0 && o.shift.y==0 && o.shift.z==0 && o.gap_m==0);
        CHECK(o.sweeps[0].valid && !o.sweeps[1].valid);
    }
    CHECK(!rig.clamp.should_release());
}

void unreadable_bounds_no_clamp() {
    Rig rig;rig.bounds=false;
    for(unsigned f=0;f<30;++f) {
        const auto req=pose_at({1.5,0,-.3});    // deep in the wall and floor
        const auto o=rig.step(req);
        CHECK(identical(o.target,req) && o.no_bounds && !o.clamped && o.valid);
        for(const auto& s:o.sweeps) CHECK(!s.valid);
    }
    CHECK(!rig.clamp.should_release());
}

void held_proxies_long_axis() {
    const auto pose=pose_at({0,0,0});
    const auto box=held_proxies({{-.15,-.15,-.45},{.15,.15,.45}},pose);
    CHECK(box && box->count>=3 && box->count<=4);
    for(std::size_t i=0;i<box->count;++i) {
        CHECK(box->sphere[i].local.x==0 && box->sphere[i].local.y==0);
        CHECK(std::abs(box->sphere[i].radius-.15)<1e-9);
        CHECK(std::abs(box->sphere[i].local.z)<=.45-.15+1e-9);
    }
    CHECK(box->sphere[0].local.z<box->sphere[box->count-1].local.z);
    const auto flat=held_proxies({{-.45,-.15,-.15},{.45,.15,.15}},pose);   // long axis x
    CHECK(flat && flat->count>=3 && flat->sphere[0].local.x!=flat->sphere[1].local.x);
    const auto cube=held_proxies({{-.2,-.2,-.2},{.2,.2,.2}},pose);
    CHECK(cube && cube->count==1 && cube->sphere[0].local.x==0 && std::abs(cube->sphere[0].radius-.2)<1e-9);
    const auto plank=held_proxies({{-1.4,-.05,-.05},{1.4,.05,.05}},pose);
    CHECK(plank && plank->count==held_proxy_capacity);
    CHECK(!held_proxies({{-1.6,-.1,-.1},{1.6,.1,.1}},pose));
    CHECK(!held_proxies({{-.1,-.1,-.1},{.1,.1,std::nan("")}},pose));
    CHECK(!held_proxies({{.1,.1,.1},{-.1,-.1,-.1}},pose));
    CHECK(!held_proxies({{-.1,-.1,-.1},{.1,.1,.1}},pose_at({std::nan(""),0,0})));
}

void release_after_quarter_second() {
    // The wall holds the object at x ~ 0.8 while the request is 0.4 m behind it.
    const auto run=[](double gap,double seconds,double& when_released) {
        Rig rig;when_released=-1;
        for(unsigned f=0;f<static_cast<unsigned>(seconds/dt)+2;++f) {
            rig.step(pose_at({.8+gap,0,2}));
            if(rig.clamp.should_release() && when_released<0) when_released=rig.out.valid?rig.now()-dt:0;
        }
        return rig.clamp.should_release();
    };
    double when=0;
    // Gap time is counted from the first frame the clamp holds; a hit arrives one frame late.
    CHECK(run(.4,.20,when)==false);
    CHECK(run(.4,.50,when)==true);
    CHECK(when>=.25 && when<=.34);
    CHECK(run(.30,2.,when)==false);
}
void release_timer_exact() {
    // Hand-fed plane (no fake-world lag): the gap is 0.4 m from the second update on.
    HeldClamp clamp;
    HeldClampInput in{};
    in.epoch=1;in.proxies=tank();
    in.hits[0]=ClampHit{{1,0,2},{-1,0,0},7,3,false};
    in.requested=pose_at({1.2,0,2});    // target stops at 1 - 0.2 - skin = 0.795 -> gap 0.405
    clamp.update(in);   // t = 0, plane made, timer sees dt 0
    CHECK(!clamp.should_release());
    for(double t=.01;t<.245;t+=.01) {in.now=t;clamp.update(in);CHECK(!clamp.should_release());}   // .24 s
    in.now=.26;clamp.update(in);
    CHECK(clamp.should_release());
}

void non_finite_requested_invalid() {
    Rig rig;
    rig.step(pose_at({0,0,2}));
    for(const double bad:{std::nan(""),std::numeric_limits<double>::infinity()}) {
        const auto o=rig.step(pose_at({bad,0,2}));
        CHECK(!o.valid && !o.clamped);
        for(const auto& s:o.sweeps) CHECK(!s.valid);
    }
    auto broken=pose_at({0,0,2});broken.basis[1]={std::nan(""),0,0};
    CHECK(!rig.step(broken).valid);
    CHECK(rig.step(pose_at({0,0,2})).valid);
}

void epoch_change_resets_planes() {
    Rig rig;
    for(unsigned f=0;f<20;++f) rig.step(pose_at({1.2,0,2}));
    CHECK(rig.out.clamped);
    rig.epoch=2;
    rig.last_sweeps={};   // the new object has no sweeps in flight
    const auto o=rig.step(pose_at({1.2,0,2}));
    CHECK(!o.clamped && o.target.position.x==1.2);   // the old wall plane is forgotten
}

void yield_clears_state() {
    Rig rig;
    for(unsigned f=0;f<20;++f) rig.step(pose_at({1.2,0,2}));
    CHECK(rig.out.clamped);
    const auto req=pose_at({1.2,0,2});
    const auto y=rig.step(req,true);
    CHECK(y.valid && identical(y.target,req) && !y.clamped);
    CHECK(!rig.clamp.should_release());
    const auto o=rig.step(req);   // planes and sweeps are gone: nothing to clamp with until the next hit arrives
    CHECK(identical(o.target,req) && !o.clamped);
}

void two_proxies_keep_every_sphere_out() {
    // A tilted-free upright tank: the lowest sphere meets the floor, the others stay above it.
    Rig rig;rig.proxies=three();
    for(unsigned f=0;f<60;++f) {
        const auto o=rig.step(pose_at({0,0,.2-.8*std::min(f,30u)/30.}));
        if(f>=10) CHECK(o.target.position.z-.5-radius>=-.01);
    }
}

// ---- SafeRelease ----
constexpr double fdt=.02;
// Lab-style floor: the object's penetration shrinks by the commanded velocity along +z; free when depth <= 0.
struct FloorLab {
    double depth=.04;double max_written=0;bool restored=false;bool restored_inside=false;
    // Returns the answer for the zero-length cast at the current pose.
    SafeReleaseInput ask(double now) const {
        SafeReleaseInput in{};in.now=now;in.released=true;in.sweep_answered=true;in.depth=depth;
        if(depth>0) in.overlap=ClampHit{{0,0,0},{0,0,1},7,3,true};
        return in;
    }
    SafeReleaseOutput step(SafeRelease& r,double now) {
        const auto o=r.update(ask(now));
        if(o.write) {max_written=std::max(max_written,length(o.velocity));depth-=o.velocity.z*fdt;}
        if(o.restore_layer) {restored=true;if(depth>0) restored_inside=true;}
        return o;
    }
};
void release_in_free_space_restores_immediately() {
    SafeRelease r;SafeReleaseInput in{};in.now=1;in.released=true;in.sweep_answered=true;
    const auto o=r.update(in);
    CHECK(o.restore_layer && !o.write && !o.unresolved && o.phase==ReleasePhase::done);
    CHECK(!r.update(in).restore_layer);   // exactly once
}
void release_inside_floor_resolves_within_half_second() {
    SafeRelease r;FloorLab lab;bool unresolved=false;double when=-1;
    for(unsigned f=0;f<40 && !lab.restored;++f) {
        const auto o=lab.step(r,f*fdt);
        if(o.restore_layer) {when=f*fdt;unresolved=o.unresolved;}
        CHECK(o.write==(o.phase==ReleasePhase::depenetrating));
    }
    CHECK(lab.restored && !lab.restored_inside && !unresolved && when<=.5);
    CHECK(lab.max_written<=1.5+1e-12 && lab.max_written>0);
}
void layer_not_restored_while_checking() {
    SafeRelease r;
    for(double t=0;t<.4;t+=.02) {
        SafeReleaseInput in{};in.now=t;in.released=true;
        const auto o=r.update(in);
        CHECK(!o.restore_layer && !o.write && o.phase==ReleasePhase::checking);
    }
    SafeReleaseInput in{};in.now=.5;in.released=true;   // never answered: gives up, counted
    const auto o=r.update(in);
    CHECK(o.restore_layer && o.unresolved);
}
void never_frees_counts_unresolved() {
    SafeRelease r;bool restored=false,unresolved=false;
    for(unsigned f=0;f<=30 && !restored;++f) {
        SafeReleaseInput in{};in.now=f*.02;in.released=true;in.sweep_answered=true;in.depth=.1;
        in.overlap=ClampHit{{0,0,0},{0,0,1},7,3,true};
        const auto o=r.update(in);
        if(!o.restore_layer) CHECK(o.phase!=ReleasePhase::done && length(o.velocity)<=1.5+1e-12);
        else {CHECK(f*.02>=.5-1e-9);restored=true;unresolved=o.unresolved;}
    }
    CHECK(restored && unresolved);
}
void corner_release_resolves_or_counts() {
    SafeRelease r;bool done=false;double speed=0;
    for(unsigned f=0;f<60 && !done;++f) {
        SafeReleaseInput in{};in.now=f*.02;in.released=true;in.sweep_answered=true;in.depth=.03;
        in.overlap=ClampHit{{0,0,0},f%2?Vec3{-1,0,0}:Vec3{0,0,1},7,3,true};
        const auto o=r.update(in);
        speed=std::max(speed,length(o.velocity));
        if(o.restore_layer) {done=true;CHECK(f*.02<=.5+1e-9);}
    }
    CHECK(done && speed<=1.5+1e-12);
}
void no_normal_pushes_up() {
    SafeRelease r;SafeReleaseInput in{};in.now=0;in.released=true;in.sweep_answered=true;in.depth=.02;
    ClampHit hit{{0,0,0},{0,0,0},7,3,true,false};in.overlap=hit;
    const auto o=r.update(in);
    CHECK(o.write && o.velocity.x==0 && o.velocity.y==0 && o.velocity.z>0 && !o.restore_layer);
}
void non_finite_overlap_ignored() {
    SafeRelease r;SafeReleaseInput in{};in.now=0;in.released=true;in.sweep_answered=true;in.depth=.02;
    in.overlap=ClampHit{{0,0,0},{std::nan(""),0,1},7,3,true};
    auto o=r.update(in);
    CHECK(!o.write && !o.restore_layer && o.phase==ReleasePhase::checking);
    in.overlap=ClampHit{{0,0,0},{0,0,1},7,3,true};in.depth=std::nan("");   // a bad depth still pushes at the floor speed
    o=r.update(in);
    CHECK(o.write && std::isfinite(o.velocity.z) && o.velocity.z<=1.5);
}
void reset_idles() {
    SafeRelease r;SafeReleaseInput in{};in.now=0;in.released=true;in.sweep_answered=true;in.depth=.02;
    in.overlap=ClampHit{{0,0,0},{0,0,1},7,3,true};
    r.update(in);
    r.reset();
    SafeReleaseInput idle{};idle.now=1;
    const auto o=r.update(idle);
    CHECK(o.phase==ReleasePhase::idle && !o.write && !o.restore_layer);
    in.now=2;in.overlap.reset();
    CHECK(r.update(in).restore_layer);   // starts a fresh check
}
} // namespace

int main() {
    wall_carry_0p1_to_3ms();
    floor_press_stops_at_floor();
    corner_two_planes_no_penetration();
    free_space_untouched();
    unreadable_bounds_no_clamp();
    held_proxies_long_axis();
    release_after_quarter_second();
    release_timer_exact();
    non_finite_requested_invalid();
    epoch_change_resets_planes();
    yield_clears_state();
    two_proxies_keep_every_sphere_out();
    release_in_free_space_restores_immediately();
    release_inside_floor_resolves_within_half_second();
    layer_not_restored_while_checking();
    never_frees_counts_unresolved();
    corner_release_resolves_or_counts();
    no_normal_pushes_up();
    non_finite_overlap_ignored();
    reset_idles();
    std::puts("held_clamp_check OK");
}
