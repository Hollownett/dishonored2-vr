#include "hand_clamp.hpp"
#include "native_world_trace.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
using namespace d2vr;
#define CHECK(x) do{if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)

namespace {
constexpr double dt=1./90;
const double nan_value=std::nan("");
bool close_to(Vec3 a,Vec3 b,double tolerance) {return length(a-b)<=tolerance;}
WorldTrackedPose pose_at(Vec3 p) {return {p,{{{1,0,0},{0,1,0},{0,0,1}}}};}
Vec3 apply(const WorldTrackedPose& frame,Vec3 local) {
    return frame.position+frame.basis[0]*local.x+frame.basis[1]*local.y+frame.basis[2]*local.z;
}

// Fake world: half-spaces answered like a closest-hit sphere cast. A wall at x = 1 (normal -x), a floor at z = 0
// (normal +z) and an optional second wall at y = 1 (normal -y).
struct Plane {Vec3 point,normal;std::uintptr_t entity;std::uint16_t material;};
struct World {
    bool wall_x=true,floor=true,wall_y=false;
    bool chest_inside=false;   // every sweep that starts at the chest begins inside geometry
    bool palm_inside=false;    // every other sweep begins inside geometry
    Vec3 chest{0,0,1};
    std::optional<ClampHit> cast(const ClampSweep& s) const {
        if(!s.valid) return std::nullopt;
        const bool from_chest=close_to(s.from,chest,1e-9);
        if(from_chest && chest_inside) return ClampHit{s.from,{0,0,1},11,6,true};
        if(!from_chest && palm_inside) return ClampHit{s.from,{0,0,1},12,6,true};
        std::array<Plane,3> planes{{{{1,0,0},{-1,0,0},7,3},{{0,0,0},{0,0,1},8,4},{{0,1,0},{0,-1,0},9,5}}};
        const bool on[3]{wall_x,floor,wall_y};
        std::optional<ClampHit> best;double best_t=2,deepest=1e9;
        for(unsigned i=0;i<3;++i) {
            if(!on[i]) continue;
            const auto& p=planes[i];
            const double s0=dot(s.from-p.point,p.normal),s1=dot(s.to-p.point,p.normal);
            if(s0<s.radius) { // starts inside: initial overlap, reported before any later hit
                if(s0<deepest) {deepest=s0;best_t=-1;best=ClampHit{s.from-p.normal*s0,p.normal,p.entity,p.material,true};}
                continue;
            }
            if(s1>=s.radius || best_t<0) continue;
            const double t=(s0-s.radius)/(s0-s1);
            if(t<best_t) {best_t=t;const Vec3 c=s.from+(s.to-s.from)*t;best=ClampHit{c-p.normal*s.radius,p.normal,p.entity,p.material,false};}
        }
        return best;
    }
};

// Drives one side at 90 Hz. Hits answer last frame's posted sweeps (one frame late). Like the game caller, slot 0's
// result goes to chest_hit while the last output was released and to palm_hit otherwise.
struct Rig {
    HandClamp clamp;World world;unsigned side=0;std::uint64_t epoch=1;double t0=0;unsigned frame=0;
    WorldTrackedPose root=level_frame({0,0,0},{1,0,0});
    ClampProbe tip{};bool tip_enabled=false;
    std::function<bool(unsigned)> drop_palm;   // true: lose this frame's palm result
    ClampOutput last{};bool have_last=false;
    explicit Rig(HandClampConfig c={}) : clamp(c) {}
    double now() const {return t0+frame*dt;}
    ClampOutput step_pose(const WorldTrackedPose& target,bool yield=false) {
        ClampInput in{};
        in.now=now();in.epoch=epoch;in.root=root;in.target=target;in.chest=world.chest;
        in.tip=tip;in.tip_enabled=tip_enabled;in.yield=yield;
        if(have_last) {
            const auto slot0=world.cast(last.palm_sweep);
            if(last.state==ClampState::released) in.chest_hit=slot0;
            else if(!(drop_palm && drop_palm(frame))) in.palm_hit=slot0;
            in.tip_hit=world.cast(last.tip_sweep);
        }
        last=clamp.update(side,in);have_last=true;++frame;
        return last;
    }
    ClampOutput step(Vec3 target) {return step_pose(pose_at(target));}
    // Moves the target from its current x to `x` at `speed` m/s (y, z fixed), one frame per call to `each`.
    template<class Each> ClampOutput move_x(Vec3 from,double x,double speed,Each&& each) {
        Vec3 p=from;ClampOutput o{};
        const double step_x=speed*dt*(x>from.x?1:-1);
        for(;;) {
            const bool done=std::abs(x-p.x)<=std::abs(step_x);
            p.x=done?x:p.x+step_x;
            o=step(p);each(o,p);
            if(done) return o;
        }
    }
};
const auto ignore=[](const ClampOutput&,Vec3){};

void sweep_starts_at_previous_drawn() {
    Rig r;r.step({.5,0,.5});
    auto o=r.step({.5,0,.5});
    const Vec3 previous=o.drawn.position;
    o=r.step({1.3,0,.5});
    CHECK(o.palm_sweep.valid && close_to(o.palm_sweep.from,previous,1e-9));
    for(int i=0;i<15;++i) { // hits arrive from the next frame on; a 0.355 m gap snaps only after 0.25 s
        const Vec3 before=o.drawn.position;
        o=r.step({1.3,0,.5});
        CHECK(o.palm_sweep.valid);
        // The previous drawn hand; one the late results left inside the wall starts outside the plane found since.
        if(before.x<=.95) CHECK(close_to(o.palm_sweep.from,before,1e-9));
        else CHECK(std::abs(o.palm_sweep.from.x-.945)<1e-9 && std::abs(o.palm_sweep.from.z-before.z)<1e-9);
        CHECK(o.drawn.position.x<=1-.05+1e-6 && o.drawn.position.x>=1-.05-.005-1e-6);
        CHECK(!o.snapped && o.state==ClampState::contact);
    }
}

void corner_two_planes() {
    for(const double depth:{1.2,1.1}) {
        Rig r;r.world.wall_y=true;
        r.step({.5,.5,.5});r.step({.5,.5,.5});
        // The jump frame is drawn unclamped (results are one frame late); both walls hold within the next 3 frames.
        ClampOutput o=r.step({depth,depth,.5});
        for(int i=0;i<3;++i) o=r.step({depth,depth,.5});
        CHECK(o.drawn.position.x<=.95 && o.drawn.position.y<=.95);
        // The closest-hit cast reports one wall per frame: the other must not expire and let the hand into it.
        const int frames=depth>1.15?18:180; // a 0.36 m gap snaps after 0.25 s; 0.22 m never does
        for(int i=0;i<frames;++i) {
            o=r.step({depth,depth,.5});
            CHECK(finite(o.drawn.position) && o.valid);
            CHECK(o.drawn.position.x<=.95 && o.drawn.position.y<=.95);
            CHECK(o.drawn.position.x>=.945-1e-6 && o.drawn.position.y>=.945-1e-6);
            CHECK(!o.snapped);
        }
    }
}

void grazing_slides() {
    Rig r;r.step({.5,0,.5});
    Vec3 p{.99,0,.5}; // palm centre 4 cm into the wall
    r.move_x({.5,0,.5},p.x,1,ignore);
    for(int i=0;i<5;++i) r.step(p);
    for(int i=0;i<90;++i) {
        p.z+=.5*dt;
        const auto o=r.step(p);
        CHECK(o.state==ClampState::contact);
        CHECK(o.drawn.position.x>=.945-1e-9 && o.drawn.position.x<=.95);
        CHECK(std::abs(o.drawn.position.z-p.z)<=1e-3 && std::abs(o.drawn.position.y)<=1e-9);
    }
}

void fast_approach_late_results() {
    for(const double speed:{2.,3.}) {
        Rig r;Vec3 previous{.5,0,.5};
        r.step(previous);
        double deepest=-1;
        r.move_x(previous,1.25,speed,[&](const ClampOutput& o,Vec3 target) {
            CHECK(o.drawn.position.x<=.95+.005);
            deepest=std::max(deepest,o.drawn.position.x);
            CHECK(o.palm_sweep.valid && close_to(o.palm_sweep.to,target+(target-previous),1e-6)); // target + v*dt
            previous=target;
        });
        for(int i=0;i<10;++i) {const auto o=r.step(previous);CHECK(o.drawn.position.x<=.95+.005);}
        CHECK(deepest>.94); // it did reach the wall
    }
}

void deep_start_overlap_snaps() {
    Rig r;r.world.wall_x=false;r.world.floor=false;r.world.palm_inside=true;
    const Vec3 target{.5,0,.5};
    r.step(target);                   // re-acquire: chest sweep, clear
    auto o=r.step(target);            // first palm sweep posted
    CHECK(o.state==ClampState::free && !o.snapped);
    o=r.step(target);CHECK(!o.snapped && o.state!=ClampState::released); // overlap 1
    o=r.step(target);CHECK(!o.snapped && o.state!=ClampState::released); // overlap 2
    o=r.step(target);                                                     // overlap 3
    CHECK(o.snapped && o.state==ClampState::released && close_to(o.drawn.position,target,0));
    CHECK(r.clamp.snapbacks(0)==1);
}

// Moves the hand 0.5 m behind the wall at 2 m/s and returns once it snapped. Shared by the snap and re-arm tests.
void press_until_snap(Rig& r) {
    r.step({.5,0,.5});
    unsigned gap_frames=0;bool snapped=false;
    const auto watch=[&](const ClampOutput& o) {
        if(snapped) return;
        if(o.snapped) {
            const double gap_time=(gap_frames+1)*dt;  // the snap frame itself is over the gap
            CHECK(gap_time>.24 && gap_time<=.26+1e-9);
            snapped=true;return;
        }
        gap_frames=length(o.shift)>.35?gap_frames+1:0;
    };
    r.move_x({.5,0,.5},1.45,2,[&](const ClampOutput& o,Vec3) {watch(o);});
    for(int i=0;i<60 && !snapped;++i) watch(r.step({1.45,0,.5}));
    CHECK(snapped && r.clamp.snapbacks(0)==1);
}

void snap_after_gap_time() {Rig r;press_until_snap(r);}

void no_snap_short_gap() {
    Rig r;r.step({.5,0,.5});
    const Vec3 target{.945+.30,0,.5};
    r.move_x({.5,0,.5},target.x,1,ignore);
    for(int i=0;i<180;++i) {
        const auto o=r.step(target);
        CHECK(!o.snapped && o.state==ClampState::contact && o.drawn.position.x<=.95);
    }
    CHECK(r.clamp.snapbacks(0)==0);
}

void rearm_after_clear() {
    Rig r;press_until_snap(r);
    const Vec3 behind{1.45,0,.5};
    for(int i=0;i<45;++i) { // 0.5 s with the chest sweep blocked by the wall
        const auto o=r.step(behind);
        CHECK(o.state==ClampState::released && close_to(o.drawn.position,behind,0) && !o.snapped);
        CHECK(o.palm_sweep.valid && close_to(o.palm_sweep.from,r.world.chest,0) && close_to(o.palm_sweep.to,behind,0));
        CHECK(!o.tip_sweep.valid);
    }
    const Vec3 free_space{.5,0,.5};
    unsigned frames=0;ClampOutput o{};
    do {o=r.step(free_space);++frames;CHECK(close_to(o.drawn.position,free_space,0));
        if(frames*dt<=.05) CHECK(o.state==ClampState::released);
    } while(o.state==ClampState::released && frames<20);
    CHECK(o.state==ClampState::free && frames*dt<=.1+2*dt+1e-9);
    CHECK(close_to(o.palm_sweep.from,free_space,1e-9)); // re-armed from the target, not the chest
    double deepest=0;bool contact=false;
    r.move_x(free_space,1.1,1,[&](const ClampOutput& q,Vec3) {deepest=std::max(deepest,q.drawn.position.x);contact|=q.state==ClampState::contact;});
    for(int i=0;i<5;++i) {o=r.step({1.1,0,.5});deepest=std::max(deepest,o.drawn.position.x);}
    CHECK(contact && deepest<=.95 && std::abs(o.drawn.position.x-.945)<1e-6);
    CHECK(r.clamp.snapbacks(0)==1);
}

void non_finite_snaps() {
    Rig r;r.step({.5,0,.5});
    r.move_x({.5,0,.5},1.1,1,ignore);
    auto o=r.step({1.1,0,.5});CHECK(o.state==ClampState::contact);
    r.clamp.note_drawn(0,r.root,{nan_value,0,0});
    o=r.step({1.1,0,.5});
    CHECK(o.valid && o.snapped && o.state==ClampState::released && close_to(o.drawn.position,{1.1,0,.5},0));
    CHECK(finite(o.drawn.position) && length(o.shift)==0 && r.clamp.snapbacks(0)==1);
    o=r.step({nan_value,0,.5});CHECK(!o.valid && !o.palm_sweep.valid && !o.tip_sweep.valid);
    auto bad=pose_at({.5,0,.5});bad.basis[0].y=nan_value;
    o=r.step_pose(bad);CHECK(!o.valid);
    o=r.step({.5,0,.5});CHECK(o.valid && close_to(o.palm_sweep.from,r.world.chest,0)); // reset: re-acquire
}

void walk_and_turn_carry() {
    Rig r;r.world.wall_x=false;r.world.floor=false;
    r.root=level_frame({0,0,1},{1,0,0});
    const Vec3 hand_local{.3,0,0};
    r.world.chest=apply(r.root,{0,0,.4});
    for(int i=0;i<3;++i) {auto t=r.root;t.position=apply(r.root,hand_local);const auto o=r.step_pose(t);CHECK(close_to(o.drawn.position,t.position,1e-12));}
    const double c=std::sqrt(.5);
    r.root=level_frame({1,0,1},{c,c,0});
    r.world.chest=apply(r.root,{0,0,.4});
    auto target=r.root;target.position=apply(r.root,hand_local);
    const auto o=r.step_pose(target);
    CHECK(o.palm_sweep.valid && close_to(o.palm_sweep.from,target.position,1e-9)); // the carried previous hand
    CHECK(close_to(o.palm_sweep.to,target.position,1e-9));                         // no velocity from walking
    CHECK(!o.snapped && o.state==ClampState::free && close_to(o.drawn.position,target.position,1e-12));
    CHECK(r.clamp.snapbacks(0)==0);
}

void epoch_change_resets() {
    Rig r;r.step({.5,0,.5});
    r.move_x({.5,0,.5},1.1,1,ignore);
    auto o=r.step({1.1,0,.5});CHECK(o.state==ClampState::contact && o.drawn.position.x<=.95);
    ++r.epoch;
    const Vec3 behind{1.3,0,.5};
    o=r.step(behind);
    CHECK(o.valid && !o.snapped && o.state==ClampState::free && close_to(o.drawn.position,behind,0) && length(o.shift)==0);
    CHECK(o.palm_sweep.valid && close_to(o.palm_sweep.from,r.world.chest,0) && close_to(o.palm_sweep.to,behind,0));
    CHECK(!o.contact);
    o=r.step(behind); // the chest sweep met the wall: re-validated against this world
    CHECK(o.drawn.position.x<=.95 && o.state==ClampState::contact);
    CHECK(r.clamp.snapbacks(0)==0);
}

void tracking_gap_reacquires() {
    Rig r;r.world.wall_x=false;
    for(int i=0;i<5;++i) r.step({.5,0,.5});
    r.t0+=.2; // a short gap keeps the stored hand
    auto o=r.step({.5,.05,.5});CHECK(close_to(o.palm_sweep.from,{.5,0,.5},1e-9));
    r.t0+=.4;
    const Vec3 away{.5,.65,.5};
    o=r.step(away);
    CHECK(o.valid && o.palm_sweep.valid && close_to(o.palm_sweep.from,r.world.chest,0) && close_to(o.palm_sweep.to,away,0));
    CHECK(!o.snapped && close_to(o.drawn.position,away,0) && o.state==ClampState::free);
    o=r.step(away);
    CHECK(close_to(o.palm_sweep.from,away,1e-9) && close_to(o.palm_sweep.to,away,1e-9)); // no velocity across the gap
    CHECK(!o.snapped && r.clamp.snapbacks(0)==0);
}

void reacquire_from_inside_geometry_no_flicker() {
    { // Crouched under a table from the start: the chest sweep always begins inside geometry.
        Rig r;r.world.chest_inside=true;
        const Vec3 target{.5,0,.5};
        r.step(target);
        for(int i=0;i<61;++i) {
            const auto o=r.step(target);
            CHECK(o.state==ClampState::released && close_to(o.drawn.position,target,0) && !o.snapped);
            CHECK(o.palm_sweep.valid && close_to(o.palm_sweep.from,r.world.chest,0));
        }
        CHECK(r.clamp.snapbacks(0)==0);
    }
    { // Released after a snap, then the chest ends up inside geometry.
        Rig r;press_until_snap(r);
        r.world.chest_inside=true;
        const Vec3 target{.5,0,.5};
        for(int i=0;i<60;++i) {
            const auto o=r.step(target);
            CHECK(o.state==ClampState::released && close_to(o.drawn.position,target,0) && !o.snapped);
        }
        CHECK(r.clamp.snapbacks(0)==1);
    }
}

void intermittent_hits_no_flicker() {
    Rig r;r.step({.5,0,.5});
    const Vec3 pressed{.99,0,.5};
    r.move_x({.5,0,.5},pressed.x,1,ignore);
    for(int i=0;i<10;++i) r.step(pressed);
    r.drop_palm=[](unsigned frame) {return (frame&1u)!=0;};
    for(int i=0;i<90;++i) {
        const auto o=r.step(pressed);
        CHECK(o.drawn.position.x>=.945-1e-9 && o.drawn.position.x<=.95 && o.state==ClampState::contact);
    }
}

void release_eases_out() {
    Rig r;r.step({.5,0,.5});
    const Vec3 pressed{.945+.1,0,.5};
    r.move_x({.5,0,.5},pressed.x,1,ignore);
    ClampOutput o{};
    for(int i=0;i<5;++i) o=r.step(pressed);
    CHECK(std::abs(length(o.shift)-.1)<1e-6);
    double previous=length(o.shift);
    const Vec3 back{pressed.x-.2,0,.5};
    for(int i=0;i<20;++i) {
        o=r.step(back);
        const double l=length(o.shift);
        CHECK(l<=previous+1e-12 && previous-l<=1.5*dt+1e-9);
        CHECK(o.drawn.position.x<=.95);
        previous=l;
    }
    CHECK(previous==0 && o.state==ClampState::free && close_to(o.drawn.position,back,1e-12));
}

// A surface that stops answering (a door opened) holds the hand for plane_seconds, then lets it ease to the target.
void stale_plane_expires() {
    Rig r;r.step({.5,0,.5});
    const Vec3 pressed{.99,0,.5};
    r.move_x({.5,0,.5},pressed.x,1,ignore);
    for(int i=0;i<5;++i) r.step(pressed);
    r.world.wall_x=false;
    ClampOutput o{};unsigned frames=0;
    do {
        o=r.step(pressed);++frames;
        if(frames*dt<=.05) CHECK(o.drawn.position.x<=.95 && o.state==ClampState::contact);
    } while(o.state!=ClampState::free && frames<40);
    CHECK(o.state==ClampState::free && close_to(o.drawn.position,pressed,1e-12) && frames*dt<=.1+5*dt);
}

// Easing out of one plane while a new one appears: the eased hand is pushed out of the new plane too.
void eased_hand_never_enters_plane() {
    Rig r;r.step({.5,0,.5});
    const Vec3 pressed{.945+.1,0,.5};
    r.move_x({.5,0,.5},pressed.x,1,ignore);
    for(int i=0;i<5;++i) r.step(pressed);
    const Vec3 low{.985,0,.02};  // needs a smaller wall push but a floor push: |wanted| < |shift|, so it eases
    r.step(low);                 // the floor is not known yet this frame (results are one frame late)
    for(int i=0;i<30;++i) {
        const auto o=r.step(low);
        CHECK(o.drawn.position.z>=.05 && o.drawn.position.x<=.95);
    }
}

void tip_probe_stops_fingers() {
    const ClampProbe tip{{.12,0,0},.03};
    for(const double speed:{1.,2.}) {
        Rig r;r.tip=tip;r.tip_enabled=true;
        r.step({.5,0,.5});
        Vec3 previous{.5,0,.5},previous_drawn{.5,0,.5};
        ClampOutput o=r.move_x({.5,0,.5},1.1,speed,[&](const ClampOutput& q,Vec3 target) {
            CHECK(q.drawn.position.x+.12<=1-.03+.005);
            CHECK(q.tip_sweep.valid && std::abs(q.tip_sweep.radius-.03)<1e-12);
            CHECK(close_to(q.tip_sweep.from,previous_drawn+Vec3{.12,0,0},1e-9));                     // previous drawn fingertip
            CHECK(close_to(q.tip_sweep.to,target+Vec3{.12,0,0}+(target-previous),1e-6));              // + v*dt
            previous=target;previous_drawn=q.drawn.position;
        });
        for(int i=0;i<10;++i) {o=r.step({1.1,0,.5});CHECK(o.drawn.position.x+.12<=1-.03+.005);}
        CHECK(std::abs(o.drawn.position.x+.12-(1-.03-.005))<1e-6); // the fingertip rests on the wall
        CHECK(o.contact && std::abs(o.contact->normal.x+1)<1e-9);
    }
    Rig r;r.tip=tip;r.tip_enabled=false;
    r.step({.5,0,.5});
    ClampOutput o=r.move_x({.5,0,.5},1.1,1,[](const ClampOutput& q,Vec3) {CHECK(!q.tip_sweep.valid);});
    for(int i=0;i<10;++i) o=r.step({1.1,0,.5});
    CHECK(std::abs(o.drawn.position.x-.945)<1e-6 && o.drawn.position.x+.12>1-.03); // only the palm is held
}

void leading_probe_picks_front() {
    const std::array<ClampProbe,3> hand{{{{0,0,0},.02},{{.08,0,0},.02},{{.15,0,0},.02}}};
    const ClampProbe fallback{{.09,0,0},.035};
    const auto grip=pose_at({1,2,3});
    CHECK(leading_probe(hand,grip,{1,0,0},fallback).local.x==.15);
    CHECK(leading_probe(hand,grip,{-1,0,0},fallback).local.x==0);
    CHECK(leading_probe(hand,grip,{0,0,0},fallback).local.x==.09);
    CHECK(leading_probe(hand,grip,{.04,0,0},fallback).local.x==.09);     // under 0.05 m/s
    CHECK(leading_probe(hand,grip,{nan_value,0,0},fallback).local.x==.09);
    // The grip frame rotates the probes: forward along world +y.
    const WorldTrackedPose turned{{0,0,0},{{{0,1,0},{-1,0,0},{0,0,1}}}};
    CHECK(leading_probe(hand,turned,{0,2,0},fallback).local.x==.15);
    CHECK(leading_probe(hand,turned,{2,0,0},fallback).local.x==0); // sideways: all tie, the first wins
    // A bigger sphere leads when the offsets tie.
    const std::array<ClampProbe,2> wide{{{{0,0,0},.02},{{0,0,0},.05}}};
    CHECK(leading_probe(wide,grip,{1,0,0},fallback).radius==.05);
}

void budget_drops_tip_then_alternates() {
    const auto run=[](CastBudget& b,double us,double from,double to,bool include_start) {
        for(int i=include_start?0:1;from+i*dt<=to+1e-9;++i) b.note(us,from+i*dt);
    };
    CastBudget b;
    CHECK(b.tip_enabled() && !b.alternate());
    run(b,600,0,2,true);
    CHECK(!b.tip_enabled() && !b.alternate());
    CHECK(std::abs(b.mean_us()-600)<1e-9 && std::abs(b.max_us()-600)<1e-9);
    run(b,550,2,4,false);
    CHECK(!b.tip_enabled() && b.alternate());
    run(b,100,4,8,false);
    CHECK(!b.tip_enabled() && b.alternate()); // both stay until reset
    b.reset();
    CHECK(b.tip_enabled() && !b.alternate() && b.mean_us()==0 && b.max_us()==0);
    CastBudget under;
    run(under,400,0,4,true);
    CHECK(under.tip_enabled() && !under.alternate() && std::abs(under.mean_us()-400)<1e-9);
    CastBudget spike; // a short spike does not drop the tip before a full window
    run(spike,5000,0,.5,true);
    CHECK(spike.tip_enabled() && spike.max_us()==5000);
    spike.note(nan_value,.6);CHECK(spike.tip_enabled() && spike.max_us()==5000);
}

void yield_passes_through() {
    Rig r;r.tip={{.12,0,0},.03};r.tip_enabled=true;
    r.step({.5,0,.5});
    r.move_x({.5,0,.5},1.1,1,ignore);
    auto o=r.step({1.1,0,.5});CHECK(o.state==ClampState::contact);
    o=r.step_pose(pose_at({1.1,0,.5}),true);
    CHECK(o.valid && close_to(o.drawn.position,{1.1,0,.5},0) && length(o.shift)==0);
    CHECK(!o.palm_sweep.valid && !o.tip_sweep.valid && o.state==ClampState::free && !o.snapped && !o.event && !o.haptic);
    o=r.step({1.1,0,.5});
    CHECK(close_to(o.palm_sweep.from,r.world.chest,0)); // the side was reset: re-acquire from the chest
    CHECK(r.clamp.snapbacks(0)==0);
}

void event_on_new_contact() {
    Rig r;r.side=1;r.step({.5,0,.5});
    int contacts=0;
    r.move_x({.5,0,.5},1.1,1,[&](const ClampOutput& o,Vec3) {
        if(o.state!=ClampState::contact) {CHECK(!o.event && !o.haptic);return;}
        if(++contacts==1) {
            CHECK(o.event && !o.event->blade && o.event->entity==7 && o.event->material==3 && o.event->side==1);
            CHECK(std::abs(o.event->speed-o.approach_speed)<1e-12 && std::abs(o.approach_speed-1)<1e-6);
            CHECK(std::abs(o.event->normal.x+1)<1e-12 && std::abs(o.event->point.x-1)<1e-9);
            CHECK(o.haptic && o.haptic->amplitude>=.2f && o.haptic->amplitude<=1.f);
            CHECK(std::abs(o.haptic->amplitude-static_cast<float>(.2+.8/3))<1e-6f && std::abs(o.haptic->seconds-.05f)<1e-6f);
            CHECK(o.contact && o.contact->entity==7 && o.contact->material==3);
        } else CHECK(!o.event && !o.haptic);
    });
    CHECK(contacts>=2 && r.clamp.contact_frames(1)>=2 && r.clamp.contact_frames(0)==0);
}

void level_frame_and_hit_conversion() {
    auto f=level_frame({1,2,3},{0,2,5});
    CHECK(close_to(f.position,{1,2,3},0) && close_to(f.basis[0],{0,1,0},1e-12) && close_to(f.basis[1],{-1,0,0},1e-12) && close_to(f.basis[2],{0,0,1},1e-12));
    f=level_frame({0,0,0},{0,0,1});CHECK(close_to(f.basis[0],{1,0,0},0) && close_to(f.basis[1],{0,1,0},0));
    f=level_frame({0,0,0},{nan_value,0,0});CHECK(close_to(f.basis[0],{1,0,0},0));
    WorldTraceHit h{};h.point={1,2,3};h.normal={0,0,1};h.entity=42;h.material=9;h.initial_overlap=true;
    const auto c=to_clamp_hit(h);
    CHECK(close_to(c.point,{1,2,3},0) && close_to(c.normal,{0,0,1},0) && c.entity==42 && c.material==9 && c.initial_overlap);
    HandClamp clamp;
    CHECK(!clamp.update(2,ClampInput{}).valid); // only sides 0 and 1
    { // post=false: nothing is posted (re-acquire or constraint), and the next frame has nothing to read
        HandClamp quiet;ClampInput in{};in.root=level_frame({},{1,0,0});in.target=pose_at({.5,0,.5});in.chest={0,0,1};
        in.tip={{.12,0,0},.03};in.post=false;
        for(int i=0;i<3;++i) {
            in.now=i*dt;
            in.palm_hit=ClampHit{{.5,0,.5},{0,0,1},1,1,true};  // ignored: this side posted nothing last frame
            const auto o=quiet.update(0,in);
            CHECK(o.valid && !o.palm_sweep.valid && !o.tip_sweep.valid && o.state==ClampState::free && close_to(o.drawn.position,{.5,0,.5},0));
        }
    }
    clamp.note_drawn(5,level_frame({},{1,0,0}),{});clamp.reset(5);
    CHECK(clamp.snapbacks(7)==0 && clamp.contact_frames(7)==0);
}
} // namespace

int main() {
    sweep_starts_at_previous_drawn();
    corner_two_planes();
    grazing_slides();
    fast_approach_late_results();
    deep_start_overlap_snaps();
    snap_after_gap_time();
    no_snap_short_gap();
    rearm_after_clear();
    non_finite_snaps();
    walk_and_turn_carry();
    epoch_change_resets();
    tracking_gap_reacquires();
    reacquire_from_inside_geometry_no_flicker();
    intermittent_hits_no_flicker();
    release_eases_out();
    stale_plane_expires();
    eased_hand_never_enters_plane();
    tip_probe_stops_fingers();
    leading_probe_picks_front();
    budget_drops_tip_then_alternates();
    yield_passes_through();
    event_on_new_contact();
    level_frame_and_hit_conversion();
    std::puts("PASS");
}
