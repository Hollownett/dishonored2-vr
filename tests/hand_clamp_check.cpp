// Contract breaks (an update without note_drawn, a repeated update) are counted here instead of asserting.
static int contract_breaks=0;
#define D2VR_HAND_CLAMP_CONTRACT(ok) do{if(!(ok))++contract_breaks;}while(0)
#include "hand_clamp.hpp"
#include "native_world_trace.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <vector>
using namespace d2vr;
// Posting mode for the parametrized tests: 0 = every frame, 1/2 = C14 alternate mode casting on even/odd frames.
static int g_mode=0;
#define CHECK(x) do{if(!(x)){std::printf("FAIL %d (mode %d): %s\n",__LINE__,g_mode,#x);std::exit(1);}}while(0)

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
    Vec3 inside_normal{0,0,1}; // the normal those overlaps report
    bool invalid_normals=false;// surface hits report normal_valid=false
    bool floor_first=false;    // every hand sweep reports a surface 6 cm under its start first (shadows all else)
    double wall_x_end=1e9;     // the x wall exists only where y < wall_x_end (a wall that ends)
    bool slab=false;           // a loose prop (entity 20): the slab slab_x <= x <= slab_x + 0.10 in front of the x wall; a sweep excluding it passes through
    double slab_x=.80;
    bool sweeps_ignore_exclusion=false;  // the game thread could not map the excluded entity (a guard's body): it casts the sweep without exclusion
    bool thin=false;           // a thin loose prop (entity 22) lying on the floor: 0 <= z <= 0.015; a sweep excluding it passes through
    bool palm_inside_loose=false;  // the palm_inside overlaps report the loose prop (entity 20), not the world (12): a toppled bottle on the hand
    bool chest_prop=false;     // every chest sweep is stopped half way by a surface of the loose prop (entity 21)
    Vec3 chest{0,0,1};
    std::optional<ClampHit> cast(const ClampSweep& s) const {
        if(!s.valid) return std::nullopt;
        const bool from_chest=close_to(s.from,chest,1e-9);
        if(from_chest && chest_prop) return ClampHit{s.from+(s.to-s.from)*.5,{0,0,1},21,6,false};
        if(!from_chest && palm_inside_loose) return ClampHit{s.from,inside_normal,20,6,true};
        if(slab && !(s.exclude && !sweeps_ignore_exclusion && *s.exclude==20)) {   // the slab's front face, before anything behind it
            const double s0=slab_x-s.radius-s.from.x;
            if(s.from.x>=slab_x-s.radius && s.from.x<=slab_x+.10+s.radius) return ClampHit{s.from,{-1,0,0},20,6,true};
            if(s0>0 && s.to.x>s.from.x && s0<=s.to.x-s.from.x) {
                const Vec3 c=s.from+(s.to-s.from)*(s0/(s.to.x-s.from.x));return ClampHit{{slab_x,c.y,c.z},{-1,0,0},20,6,false};
            }
        }
        if(thin && !(s.exclude && *s.exclude==22)) {   // the thin prop's top face, before the floor under it
            const double s0=s.from.z-s.radius-.015;
            if(s0<=0) return ClampHit{s.from,{0,0,1},22,6,true};
            if(s.to.z<s.from.z && s0<=s.from.z-s.to.z) {
                const Vec3 c=s.from+(s.to-s.from)*(s0/(s.from.z-s.to.z));return ClampHit{{c.x,c.y,.015},{0,0,1},22,6,false};
            }
        }
        if(from_chest && chest_inside) return ClampHit{s.from,{0,0,1},11,6,true};
        if(!from_chest && palm_inside) return ClampHit{s.from,inside_normal,12,6,true};
        if(!from_chest && floor_first) return ClampHit{s.from-Vec3{0,0,s.radius+.01},{0,0,1},13,6,false};
        std::array<Plane,3> planes{{{{1,0,0},{-1,0,0},7,3},{{0,0,0},{0,0,1},8,4},{{0,1,0},{0,-1,0},9,5}}};
        const bool on[3]{wall_x,floor,wall_y};
        std::optional<ClampHit> best;double best_t=2,deepest=1e9;
        for(unsigned i=0;i<3;++i) {
            if(!on[i]) continue;
            const auto& p=planes[i];
            const double s0=dot(s.from-p.point,p.normal),s1=dot(s.to-p.point,p.normal);
            const auto exists=[&](Vec3 c) {return i!=0 || c.y<wall_x_end;};
            if(s0<s.radius && !exists(s.from)) continue;
            if(s0<s.radius) { // starts inside: initial overlap, reported before any later hit
                if(s0<deepest) {deepest=s0;best_t=-1;best=ClampHit{s.from-p.normal*s0,p.normal,p.entity,p.material,true};}
                continue;
            }
            if(s1>=s.radius || best_t<0) continue;
            const double t=(s0-s.radius)/(s0-s1);
            if(!exists(s.from+(s.to-s.from)*t)) continue;
            if(t<best_t) {best_t=t;const Vec3 c=s.from+(s.to-s.from)*t;best=ClampHit{c-p.normal*s.radius,p.normal,p.entity,p.material,false,!invalid_normals};}
        }
        return best;
    }
};

// Drives one side at 90 Hz. Hits answer last frame's posted sweeps (one frame late). Like the game caller, slot 0's
// result goes to chest_hit while the last output was released and to palm_hit otherwise, and note_drawn follows every
// update with the drawn hand.
struct Rig {
    HandClamp clamp;World world;unsigned side=0;std::uint64_t epoch=1;double t0=0;unsigned frame=0;
    WorldTrackedPose root=level_frame({0,0,0},{1,0,0});
    ClampProbe tip{};bool tip_enabled=false;
    ClampProbe wall_tip{};                     // ClampInput::wall_tip (radius 0: none)
    std::vector<ClampProbe> hand;              // ClampInput::hand (empty: planes hold only their own probe)
    std::function<bool(unsigned)> drop_palm;   // true: lose this frame's palm result
    std::function<bool(unsigned)> post;        // C14 alternate mode: false skips this frame's cast
    std::optional<Vec3> note_override;         // drawn position reported to note_drawn once
    std::vector<std::uintptr_t> loose;         // ClampInput::loose
    Vec3 kick{};                               // ClampInput::kick
    std::optional<std::uintptr_t> exclude;     // ClampInput::exclude
    ClampOutput last{};bool have_last=false,strict_answers=false;int holds=0;   // frames held so far
    explicit Rig(HandClampConfig c={}) : clamp(c) {
        if(g_mode) post=[phase=static_cast<unsigned>(g_mode-1)](unsigned f) {return (f&1u)==phase;};
    }
    bool posting() const {return !post || post(frame-1);}   // whether the last step cast
    double lead() const {return g_mode?2.:1.;}              // frames the last step's sweep predicts
    double now() const {return t0+frame*dt;}
    ClampOutput step_pose(const WorldTrackedPose& target,bool yield=false) {
        ClampInput in{};
        in.now=now();in.epoch=epoch;in.root=root;in.target=target;in.chest=world.chest;
        in.strict_answers=strict_answers;
        in.tip=tip;in.tip_enabled=tip_enabled;in.wall_tip=wall_tip;in.yield=yield;in.post=!post || post(frame);
        for(const auto e:loose) if(in.loose_count<ClampInput::loose_capacity) in.loose[in.loose_count++]=e;
        in.kick=kick;
        in.exclude=exclude;
        for(const auto& probe:hand) if(in.hand_count<ClampInput::hand_capacity) in.hand[in.hand_count++]=probe;
        if(have_last) {
            if(strict_answers) {in.answer_ids=last.request_ids;if(drop_palm && drop_palm(frame))in.answer_ids[0]=0;}
            const auto slot0=world.cast(last.palm_sweep);
            if(last.state==ClampState::released) in.chest_hit=slot0;
            else if(!(drop_palm && drop_palm(frame))) in.palm_hit=slot0;
            in.tip_hit=world.cast(last.tip_sweep);
            in.exclude_hit=world.cast(last.exclude_sweep);
            in.wall_tip_hit=world.cast(last.wall_tip_sweep);
        }
        CHECK(unsigned(last.palm_sweep.valid)+last.tip_sweep.valid+last.wall_tip_sweep.valid+last.exclude_sweep.valid<=4);   // C14: at most 4 casts a side
        last=clamp.update(side,in);have_last=true;++frame;holds+=last.held;
        clamp.note_drawn(side,root,note_override?*note_override:last.drawn.position);note_override.reset();
        CHECK(contract_breaks==0);
        return last;
    }
    ClampOutput step(Vec3 target) {return step_pose(pose_at(target));}
    // Moves the target in a straight line at `speed` m/s, one frame per call to `each`.
    template<class Each> ClampOutput move_to(Vec3 from,Vec3 to,double speed,Each&& each) {
        const double distance=length(to-from),step_length=speed*dt;ClampOutput o{};
        for(double done=0;;) {
            done=std::min(distance,done+step_length);
            const Vec3 p=done>=distance?to:from+(to-from)*(done/distance);
            o=step(p);each(o,p);
            if(done>=distance) return o;
        }
    }
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
    while(r.post && !r.post(r.frame)) o=r.step({.5,0,.5});   // alternate mode: jump on a frame that casts
    const Vec3 previous=o.drawn.position;
    o=r.step({1.3,0,.5});
    CHECK(o.palm_sweep.valid && close_to(o.palm_sweep.from,previous,1e-9));
    CHECK(o.held && close_to(o.drawn.position,previous,1e-12)); // off the validated path: wait one frame
    CHECK(o.palm_sweep.to.x<=1.3+HandClamp::max_lead*r.lead()+1e-9);   // a 0.8 m jump is not extrapolated
    for(int i=0;i<15;++i) { // hits arrive from the next frame on; a 0.355 m gap snaps only after 0.25 s
        const Vec3 before=o.drawn.position;
        o=r.step({1.3,0,.5});
        CHECK(o.palm_sweep.valid==r.posting());
        if(!r.posting()) {CHECK(o.drawn.position.x<=1-.05+1e-6 && o.drawn.position.x>=1-.05-.005-1e-6);continue;}
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
        // A jump into the corner: no frame is drawn more than 1 cm inside either wall; both hold within 3 frames.
        ClampOutput o{};
        for(int i=0;i<4;++i) {o=r.step({depth,depth,.5});CHECK(o.drawn.position.x<=.96 && o.drawn.position.y<=.96);}
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
  for(const double speed:{.5,2.5}) {   // and a sustained fast graze: one held cast cycle at most, at its onset
    Rig r;r.step({.5,0,.5});
    Vec3 p{.99,0,.5}; // palm centre 4 cm into the wall
    r.move_x({.5,0,.5},p.x,1,ignore);
    for(int i=0;i<5;++i) r.step(p);
    const int before=r.holds;
    for(int i=0;i<90;++i) {
        p.z+=speed*dt;
        const auto o=r.step(p);
        CHECK(o.state==ClampState::contact);
        CHECK(o.drawn.position.x>=.945-1e-9 && o.drawn.position.x<=.95);
        if(!o.held) CHECK(std::abs(o.drawn.position.z-p.z)<=1e-3 && std::abs(o.drawn.position.y)<=1e-9);
    }
    CHECK(r.holds-before<=static_cast<int>(r.lead()));
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
            // target + v*dt (two frames in C14 alternate mode, on the frames that cast). In contact every other sweep
            // slides along the wall, twice as far ahead.
            const Vec3 ahead=target+(target-previous)*r.lead(),slid{.945,ahead.y,ahead.z};
            CHECK(o.palm_sweep.valid==r.posting());
            if(o.palm_sweep.valid && !o.held) {
                CHECK(close_to(o.palm_sweep.to,ahead,1e-6) || (o.state==ClampState::contact && close_to(o.palm_sweep.to,slid,1e-6)));
                if(o.state==ClampState::free) CHECK(close_to(o.palm_sweep.to,ahead,1e-6));
            }
            previous=target;
        });
        for(int i=0;i<10;++i) {const auto o=r.step(previous);CHECK(o.drawn.position.x<=.95+.005);}
        CHECK(deepest>.94); // it did reach the wall
    }
}

void deep_start_overlap_snaps() {
    // An initial overlap's normal is not trusted (it may point into the surface): it counts, it never pushes.
    for(const Vec3 normal:{Vec3{0,0,1},Vec3{0,0,-1}}) {
        Rig r;r.world.wall_x=false;r.world.floor=false;r.world.palm_inside=true;r.world.inside_normal=normal;
        const Vec3 target{.5,0,.5};
        r.step(target);                   // re-acquire: chest sweep, clear
        auto o=r.step(target);            // first palm sweep posted
        CHECK(o.state==ClampState::free && !o.snapped);
        for(int i=0;i<2;++i) {            // overlaps 1 and 2
            o=r.step(target);
            CHECK(!o.snapped && o.state==ClampState::free && close_to(o.drawn.position,target,0) && !o.contact);
        }
        o=r.step(target);                 // overlap 3
        CHECK(o.snapped && o.state==ClampState::released && close_to(o.drawn.position,target,0));
        CHECK(r.clamp.snapbacks(0)==1);
    }
}

// A surface hit whose normal is invalid makes no plane: the clamp cannot tell which side is free. The hand is never
// pushed; once its sweeps start inside the wall, the deep-start rule releases it to the controller.
void invalid_normal_makes_no_plane() {
    Rig r;r.world.invalid_normals=true;r.step({.5,0,.5});
    const auto never_pushed=[](const ClampOutput& o,Vec3) {CHECK(!o.contact && !o.event && o.state!=ClampState::contact);};
    r.move_x({.5,0,.5},1.1,1,never_pushed);
    ClampOutput o{};
    for(int i=0;i<10;++i) {o=r.step({1.1,0,.5});never_pushed(o,{});}
    CHECK(o.state==ClampState::released && close_to(o.drawn.position,{1.1,0,.5},0) && r.clamp.snapbacks(0)==1);
}

// Moves the hand 0.5 m behind the wall at 2 m/s and returns once it snapped. Shared by the snap and re-arm tests.
void press_until_snap(Rig& r) {
    r.step({.5,0,.5});
    unsigned gap_frames=0;bool snapped=false;
    const auto watch=[&](const ClampOutput& o) {
        if(snapped) return;
        if(o.snapped) {
            const double gap_time=(gap_frames+1)*dt;  // the snap frame itself is over the gap
            CHECK(gap_time>.25 && gap_time<=.26+1e-9);   // C3: more than 0.25 s, not a frame early
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
    // The first clear result arrives a frame late; 0.1 s of clear results (9 frames) re-arm, not one frame sooner.
    do {o=r.step(free_space);++frames;CHECK(close_to(o.drawn.position,free_space,0));
        if(frames<=9) CHECK(o.state==ClampState::released);
    } while(o.state==ClampState::released && frames<20);
    CHECK(o.state==ClampState::free && frames==10);
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
    r.note_override=Vec3{nan_value,0,0};
    auto o=r.step({1.1,0,.5});CHECK(o.state==ClampState::contact);
    o=r.step({1.1,0,.5});
    CHECK(o.valid && o.snapped && o.state==ClampState::released && close_to(o.drawn.position,{1.1,0,.5},0));
    CHECK(finite(o.drawn.position) && length(o.shift)==0 && r.clamp.snapbacks(0)==1);
    o=r.step({nan_value,0,.5});CHECK(!o.valid && !o.palm_sweep.valid && !o.tip_sweep.valid);
    auto bad=pose_at({.5,0,.5});bad.basis[0].y=nan_value;
    o=r.step_pose(bad);CHECK(!o.valid);
    o=r.step({.5,0,.5});CHECK(o.valid && close_to(o.palm_sweep.from,r.world.chest,0)); // reset: re-acquire
}

// C11: walking and turning carry the stored hand; the prediction includes the player's own motion (C8 ruling).
void walk_and_turn_carry() {
    Rig r;r.world.wall_x=false;r.world.floor=false;
    const Vec3 hand_local{.3,.1,-.2};
    double yaw=0;Vec3 at{0,0,1};
    const auto place=[&]() {
        r.root=level_frame(at,{std::cos(yaw),std::sin(yaw),0});r.world.chest=apply(r.root,{0,0,.4});
        auto t=r.root;t.position=apply(r.root,hand_local);return t;
    };
    WorldTrackedPose previous_root{},previous_target{};Vec3 previous_drawn{};
    for(int i=0;i<90;++i) {
        if(i>=2) {at=at+Vec3{std::cos(yaw),std::sin(yaw),0}*(1.4*dt);yaw+=1.5708*dt;}  // 1.4 m/s, 90 deg/s
        const auto target=place();
        const auto o=r.step_pose(target);
        if(i>=2) {
            const Vec3 local{dot(previous_drawn-previous_root.position,previous_root.basis[0]),
                             dot(previous_drawn-previous_root.position,previous_root.basis[1]),
                             dot(previous_drawn-previous_root.position,previous_root.basis[2])};
            CHECK(o.palm_sweep.valid==r.posting());
            if(o.palm_sweep.valid) {
                CHECK(close_to(o.palm_sweep.from,apply(r.root,local),1e-9));   // the carried hand
                CHECK(close_to(o.palm_sweep.to,target.position+(target.position-previous_target.position)*r.lead(),1e-9)); // world motion
            }
            CHECK(!o.held && !o.snapped && o.state==ClampState::free && close_to(o.drawn.position,target.position,1e-12));
        }
        previous_root=r.root;previous_target=target;previous_drawn=o.drawn.position;
    }
    CHECK(r.clamp.snapbacks(0)==0);
}

// Root prediction covers recent render cadence at every locomotion speed. Acceleration can wait for a sweep;
// steady running must not repeatedly freeze because a fixed 15 cm lead is shorter than one frame of travel.
void variable_cadence_locomotion_carries_retained_hand() {
    Rig r;r.world.wall_x=false;r.world.floor=false;
    double clock=0,x=0;unsigned holds=0,steady_holds=0;
    const double cadence[]{.011,.023,.033,.050};
    for(unsigned frame=0;frame<200;++frame) {
        const double h=cadence[frame%4],speed=frame<20?0:frame<60?1.4:frame<100?3.49:frame<150?7.:frame<170?2.:0;
        clock+=h;x+=speed*h;r.t0=clock-r.frame*dt;
        r.root=level_frame({x,0,.5},{1,0,0});r.world.chest=apply(r.root,{0,0,.4});
        const auto target=apply(r.root,{.4,0,0});const auto o=r.step(target);holds+=o.held;
        CHECK(!o.snapped && o.state==ClampState::free);
        const bool steady=(frame>=28 && frame<60) || (frame>=68 && frame<100) || (frame>=108 && frame<150) || frame>=178;
        if(steady) {steady_holds+=o.held;CHECK(close_to(o.drawn.position,target,1e-9));}
    }
    CHECK(steady_holds==0 && holds<=12 && r.clamp.snapbacks(0)==0);
}

// A controller-relative jump still waits for validation while the retained hand travels with the player.
void moving_root_retains_controller_jump() {
    Rig r;r.world.wall_x=false;r.world.floor=false;
    r.post=[](unsigned) {return true;};
    for(int frame=0;frame<6;++frame) {r.root.position.x=.1*frame;r.step({.4+.1*frame,0,.5});}
    r.post=[](unsigned) {return false;}; // no new sweep may certify the transported fallback
    r.root.position.x=.58;r.world.chest.x=.58;
    auto o=r.step({.98,.3,.5});
    CHECK(o.held && close_to(o.drawn.position,{.98,0,.5},1e-9));
    r.root.position.x=.81;r.world.chest.x=.81;
    o=r.step({1.21,.3,.5});
    CHECK(o.held && close_to(o.drawn.position,{.98,0,.5},1e-9)); // transport cannot extend the older swept segment
}

// No known wall is not a clear cast: accelerate from rest toward an unseen surface at an ordinary sprint speed.
void root_acceleration_does_not_cross_unseen_wall() {
    for(bool strict:{false,true}) {
        Rig r;r.world.floor=false;r.strict_answers=strict;
        for(int frame=0;frame<5;++frame) r.step({.8,0,.5});
        r.t0+=.030-dt;r.root.position.x=.21;r.world.chest.x=.21;
        const auto first=r.step({1.01,0,.5});
        CHECK(first.held && close_to(first.drawn.position,{.8,0,.5},1e-9));
        for(int frame=0;frame<12;++frame) {
            const auto o=r.step({1.01,0,.5});CHECK(o.drawn.position.x<=.945+1e-9 && !o.snapped);
        }
        CHECK(r.last.contact && r.last.contact->entity==7);
    }
}

void strict_root_turn_and_discontinuity_need_world_proof() {
    // New movement orthogonal to the predictor cannot borrow proof from the preceding straight corridor.
    Rig r;r.strict_answers=true;r.world.wall_x=false;r.world.wall_y=true;r.world.floor=false;
    for(int frame=0;frame<8;++frame) {r.root.position.x=.08*frame;r.step({.4+.08*frame,.8,.5});}
    const double before=r.last.drawn.position.y;
    r.root.position.y=.21;auto o=r.step({.4+r.root.position.x,1.01,.5});
    CHECK(o.held && o.drawn.position.y<=before+1e-9);
    for(int frame=0;frame<12;++frame) {o=r.step({.4+r.root.position.x,1.01,.5});CHECK(o.drawn.position.y<=.945+1e-9);}
    // A spatial discontinuity retires root velocity; its query is not extended as if it were ordinary travel.
    Rig jump;jump.strict_answers=true;jump.world.wall_x=false;jump.world.floor=false;
    for(int frame=0;frame<8;++frame) {jump.root.position.x=.08*frame;jump.step({.4+.08*frame,0,.5});}
    jump.root.position.x+=2;const auto target=apply(jump.root,{.4,0,.5});o=jump.step(target);
    CHECK(o.held && o.drawn.position.x<target.x-1 && !o.validated);
    if(o.palm_sweep.valid)CHECK(close_to(o.palm_sweep.to,target,1e-9));
}

// The engine may publish one root step across several render frames. A repeated pose must not erase locomotion
// prediction. Cover the native 25.4 ms batch that reproduced the headset's ~17.8 cm sprint holds, and a 30 Hz root.
void batched_root_publications_do_not_stutter() {
    for(bool strict:{false,true}) for(double publication:{.0254,1./30}) for(double speed:{1.4,7.}) {
        Rig r;r.world.wall_x=false;r.world.floor=false;r.strict_answers=strict;
        double clock=0;unsigned steady_holds=0;
        for(unsigned frame=0;frame<420;++frame) {
            const double h=frame%3==0?.022:.011;clock+=h;r.t0=clock-r.frame*dt;
            const double motion_time=std::min(clock,4.);
            const double x=speed*std::floor(motion_time/publication)*publication;
            r.root=level_frame({x,0,.5},{1,0,0});r.world.chest=apply(r.root,{0,0,.4});
            const auto target=apply(r.root,{.4,0,0});const auto o=r.step(target);
            if(frame>40) {steady_holds+=o.held;CHECK(close_to(o.drawn.position,target,1e-9));}
            // A retained predictor may post ahead after a stop; it never moves the real drawn target ahead.
            CHECK(o.drawn.position.x<=target.x+1e-9 && !o.snapped);
        }
        CHECK(steady_holds==0);
        for(int frame=0;frame<12;++frame) r.step(apply(r.root,{.4,0,0}));
        CHECK(r.last.palm_sweep.valid==r.posting());
        if(r.last.palm_sweep.valid)CHECK(close_to(r.last.palm_sweep.to,r.last.drawn.position,1e-9));
    }
}

// Strict receipts distinguish no completion from an answered clear cast and bind delayed A to A's geometry,
// even after the render thread posted B. A later release/reset/overflow must never recycle that proof.
void strict_receipts_bind_geometry_and_generation() {
    HandClamp clamp;ClampInput in{};in.strict_answers=true;in.epoch=1;in.root=level_frame({},{1,0,0});
    in.chest={0,0,1};in.target=pose_at({.4,0,.5});
    const auto step=[&]() {in.now+=dt;auto o=clamp.update(0,in);clamp.note_drawn(0,in.root,o.drawn.position);return o;};
    const auto a=step();CHECK(a.request_ids[0] && a.reacquired);
    in.target.position.x=.8;auto b=step();CHECK(b.held && !b.validated && !b.palm_answered);
    in.answer_ids[0]=a.request_ids[0];auto o=step();
    CHECK(o.chest_answered && !o.palm_answered && o.held && !o.validated && close_to(o.drawn.position,{.4,0,.5},1e-9));
    // A duplicate A (even with a different supplied hit) is not a new result or a source of a new plane.
    in.palm_hit=ClampHit{{.1,0,.5},{-1,0,0},77};o=step();CHECK(!o.chest_answered && !o.palm_answered);
    std::array<ClampHit,HandClamp::max_held_planes> planes{};CHECK(clamp.held_planes(0,planes)==0);
    in.palm_hit.reset();in.answer_ids[0]=0;
    for(int frame=0;frame<5;++frame) {o=step();CHECK(o.held && !o.validated);}
    in.answer_ids[0]=b.request_ids[0];o=step();CHECK(o.palm_answered && o.validated && !o.held && close_to(o.drawn.position,in.target.position,1e-9));
    in.answer_ids[0]=~std::uint64_t{};o=step();CHECK(!o.palm_answered && !o.chest_answered);
    clamp.reset(0);in.answer_ids[0]=b.request_ids[0];in.palm_hit=ClampHit{{.1,0,.5},{-1,0,0},77};
    o=step();CHECK(o.reacquired && o.request_ids[0]>b.request_ids[0]);
    o=step();CHECK(!o.palm_answered && !o.chest_answered && clamp.held_planes(0,planes)==0);
    // Eviction drops proof safely instead of interpreting the old receipt against the latest request geometry.
    const auto evicted=o.request_ids[0];in.palm_hit.reset();in.answer_ids={};
    for(int frame=0;frame<70;++frame)o=step();
    in.answer_ids[0]=evicted;o=step();CHECK(!o.palm_answered && !o.chest_answered && o.held && !o.validated);
    // StartProbe replaces the complete object while native answer storage survives in the process.
    const auto previous_session=o.request_ids[0];clamp=HandClamp{};in.answer_ids[0]=previous_session;
    o=step();CHECK(o.reacquired && o.request_ids[0]>previous_session);
    o=step();CHECK(!o.palm_answered && !o.chest_answered);
}

void strict_auxiliary_receipts_and_chest_rearm() {
    HandClamp clamp;ClampInput in{};in.strict_answers=true;in.epoch=1;in.root=level_frame({},{1,0,0});
    in.chest={0,0,1};in.target=pose_at({.4,0,.5});in.tip={{.09,0,0},.035};
    const auto step=[&]() {in.now+=dt;auto o=clamp.update(0,in);clamp.note_drawn(0,in.root,o.drawn.position);return o;};
    auto a=step();auto b=step();CHECK(b.request_ids[1]);
    in.target.position.x=.8;in.tip.local.x=.3;in.answer_ids[1]=b.request_ids[1];
    in.tip_hit=ClampHit{{.6,0,.5},{-1,0,0},88};auto o=step();
    CHECK(!o.palm_answered && !o.chest_answered);
    CHECK(std::abs(clamp.keep_out(0,in.target.basis,{.8,0,.5},std::nullopt).x-.47)<1e-9); // original .09 probe, not new .3
    in.tip_hit=ClampHit{{.2,0,.5},{-1,0,0},99};o=step();
    std::array<ClampHit,HandClamp::max_held_planes> planes{};auto count=clamp.held_planes(0,planes);
    CHECK(count==1 && planes[0].entity==88); // duplicate tip completion cannot insert a different hit

    clamp.reset(0);in.answer_ids={};in.tip_hit.reset();in.target.position.x=.4;a=step();
    in.answer_ids[0]=a.request_ids[0];in.palm_hit=ClampHit{in.chest,{0,0,1},7,0,true};o=step();
    CHECK(o.state==ClampState::released && o.chest_answered);
    const auto old_chest=o.request_ids[0];in.answer_ids={};in.palm_hit.reset();
    for(int frame=0;frame<15;++frame) {o=step();CHECK(o.state==ClampState::released && !o.chest_answered);}
    for(int frame=0;frame<20 && o.state==ClampState::released;++frame) {in.answer_ids[0]=o.request_ids[0];o=step();}
    CHECK(o.state!=ClampState::released);
    in.answer_ids[0]=old_chest;in.palm_hit=ClampHit{in.chest,{0,0,1},7,0,true};o=step();
    CHECK(!o.chest_answered && o.state!=ClampState::released); // a late old chest result cannot release the rearmed palm
}

// Render and game dispatch have independent clocks. Game takes A, render posts B, then A completes; at 90/30 Hz
// the reply's age must inform future query coverage while each reply keeps its original geometric identity.
void strict_async_game_cadence_covers_batched_root() {
    for(unsigned game_stride:{1u,3u}) for(double publication:{.0254,1./30}) for(double speed:{1.4,7.}) {
        HandClamp clamp;ClampOutput last{};std::array<std::uint64_t,4> taken{};
        unsigned completion=~0u,holds=0;double clock=0;
        for(unsigned frame=0;frame<600;++frame) {
            clock+=frame%3==0?.022:.011;
            const double x=speed*std::floor(clock/publication)*publication;
            ClampInput in{};in.strict_answers=true;in.now=clock;in.epoch=1;
            in.root=level_frame({x,0,.5},{1,0,0});in.target=in.root;in.target.position.x+=.4;in.chest={x,0,.9};
            // One completed clear cast with exact request IDs, or no answer at all. A newer render post must not
            // replace the in-flight game's submitted request while that game is answering it.
            if(frame==completion)in.answer_ids=taken;
            if(frame%game_stride==0) {taken=last.request_ids;completion=frame+1;}
            last=clamp.update(0,in);clamp.note_drawn(0,in.root,last.drawn.position);
            if(frame>40) {holds+=last.held;CHECK(last.validated && close_to(last.drawn.position,in.target.position,1e-9));}
        }
        CHECK(holds==0);
    }
}

// One clear answer authorizes its fixed swept region, not an unlimited chain of small movements. Native replies
// can stop entirely while render continues posting; cached-segment tolerance must never migrate with the hand.
void strict_small_steps_cannot_self_renew_proof() {
    HandClamp clamp;ClampInput in{};in.strict_answers=true;in.epoch=1;in.root=level_frame({},{1,0,0});
    in.chest={0,0,1};in.target=pose_at({.4,0,.5});
    const auto step=[&]() {in.now+=dt;auto o=clamp.update(0,in);clamp.note_drawn(0,in.root,o.drawn.position);return o;};
    const auto a=step();in.answer_ids[0]=a.request_ids[0];auto o=step();CHECK(o.chest_answered && o.validated);
    in.answer_ids={};unsigned held=0;
    for(unsigned frame=1;frame<=200;++frame) {
        in.target.position.x=.4+.01*frame;o=step();held+=o.held;
        if(frame<=4)CHECK(o.validated && !o.held); // retain the existing bounded tolerance around the answered segment
        CHECK(o.drawn.position.x<=.45+1e-9 && !o.palm_answered && !o.chest_answered);
    }
    CHECK(held>150 && o.held && !o.validated);
}

// Transport is constrained by unchanged world planes, including when old sweeps cannot cover the new target.
void moving_root_hold_keeps_world_wall() {
    Rig r;
    for(int frame=0;frame<5;++frame) r.step({.5,0,.5});
    r.move_x({.5,0,.5},1.2,2.,ignore);
    for(int frame=0;frame<12;++frame) r.step({1.2,0,.5});
    CHECK(r.last.contact && r.last.contact->entity==7);
    const double before=r.last.drawn.position.x;
    r.root.position.x=.23;r.world.chest.x=.23;
    const auto o=r.step({1.43,.3,.5});
    CHECK(o.held && o.drawn.position.x<=.945+1e-9 && std::abs(o.drawn.position.x-before)<=1e-9);
}

// Walking arm-first into a wall at 5 m/s: the one-frame-late result must already cover the body's motion. The body
// keeps walking 0.25 m after the hand meets the wall, carrying the stored hand into it every frame (C11).
void walk_into_wall_5ms() {
  for(const double speed:{5.,1.4}) {   // sprint, and a walk that carries the hand 1.6 cm into the wall per frame
    Rig r;
    const Vec3 hand_local{.5,0,0};
    Vec3 at{-1,0,.5};
    const auto place=[&]() {r.root=level_frame(at,{1,0,0});r.world.chest=at+Vec3{0,0,.4};return apply(r.root,hand_local);};
    for(int i=0;i<3;++i) r.step(place());
    double deepest=-1;
    while(at.x<.7) {
        at.x=std::min(.7,at.x+speed*dt);
        const auto o=r.step(place());
        CHECK(o.drawn.position.x<=.95+.01 && !o.snapped);
        deepest=std::max(deepest,o.drawn.position.x);
    }
    for(int i=0;i<20;++i) {const auto o=r.step(place());CHECK(o.drawn.position.x<=.95+.01);}
    CHECK(deepest>.94 && r.last.state==ClampState::contact && std::abs(r.last.drawn.position.x-.945)<1e-6);
    CHECK(r.clamp.snapbacks(0)==0);
  }
  for(const double speed:{5.,1.4}) {   // walking diagonally arm-first into a corner
    Rig r;r.world.wall_y=true;
    const double c=std::sqrt(.5);
    const Vec3 hand_local{.5,0,0};
    Vec3 at{-1,-1,.5};
    const auto place=[&]() {r.root=level_frame(at,{c,c,0});r.world.chest=at+Vec3{0,0,.4};return apply(r.root,hand_local);};
    for(int i=0;i<3;++i) r.step(place());
    while(at.x<.7) {
        at=at+Vec3{c,c,0}*std::min(speed*dt,(.7-at.x)/c);
        const auto o=r.step(place());
        CHECK(o.drawn.position.x<=.96 && o.drawn.position.y<=.96 && !o.snapped);
    }
    for(int i=0;i<30;++i) {const auto o=r.step(place());CHECK(o.drawn.position.x<=.95 && o.drawn.position.y<=.95);}
    CHECK(r.clamp.snapbacks(0)==0);
  }
}

// A diagonal approach into a corner (both walls report at nearly the same fraction), and a palm pressed onto a table
// and into a crate face, held for 3 s: never more than 1 cm inside either surface, no snap, settled on both.
void static_corner_press() {
    for(const Vec3 pressed:{Vec3{1.05,1.03,.5},Vec3{1.02,1.02,.5},Vec3{.99,.5,.03},Vec3{.99,.5,-.03}}) {
        Rig r;r.world.wall_y=pressed.z>.4;
        const auto inside=[&](const ClampOutput& o) {
            return std::max({o.drawn.position.x-.95,r.world.wall_y?o.drawn.position.y-.95:-1.,.05-o.drawn.position.z});
        };
        r.step({.5,.5,.5});r.step({.5,.5,.5});
        r.move_to({.5,.5,.5},pressed,1,[&](const ClampOutput& o,Vec3) {CHECK(inside(o)<=.01 && !o.snapped);});
        for(int i=0;i<270;++i) {const auto o=r.step(pressed);CHECK(inside(o)<=.01 && !o.snapped);}
        CHECK(r.last.state==ClampState::contact && inside(r.last)<=1e-6 && inside(r.last)>=-.005-1e-6);
        CHECK(r.clamp.snapbacks(0)==0);
    }
}

// Pressed on one wall and sliding at 3 m/s toward a second wall that only a slid sweep can meet (the raw sweep always
// reports the first wall): the slid sweep leads two frames, so the hand never enters the second wall.
void corner_slide_3ms() {
  for(const double speed:{1.,1.5,2.,2.5,3.,5.}) {
    Rig r;r.world.wall_y=true;
    r.step({.5,0,.5});
    r.move_x({.5,0,.5},1.,1,ignore);
    for(int i=0;i<5;++i) r.step({1.,0,.5});
    // At up to 2 m/s the slid sweep meets the second wall in time; faster, a hold may stand in for a frame.
    const double bound=speed<=2?.95:.96;
    r.move_to({1.,0,.5},{1.,1.15,.5},speed,[&](const ClampOutput& o,Vec3) {CHECK(o.drawn.position.x<=.95 && o.drawn.position.y<=bound);});
    for(int i=0;i<10;++i) {const auto o=r.step({1.,1.15,.5});CHECK(o.drawn.position.x<=.95 && o.drawn.position.y<=.95);}
  }
}

// A fast swipe along a wall (pressed 5.5 cm, up to 6 m/s): the hand follows the slide with at most one held cast
// cycle at its onset, never a hold/release stutter.
void fast_wall_slide_no_stutter() {
    for(const double speed:{1.6,2.5,4.,5.,6.}) {
        Rig r;r.world.floor=false;
        r.step({.5,0,.5});
        r.move_x({.5,0,.5},1.,1,ignore);
        for(int i=0;i<10;++i) r.step({1.,0,.5});
        int holds=0;
        r.move_to({1.,0,.5},{1.,4,.5},speed,[&](const ClampOutput& o,Vec3 t) {
            holds+=o.held;
            CHECK(std::abs(o.drawn.position.x-.945)<1e-6);
            if(!o.held) CHECK(std::abs(o.drawn.position.y-t.y)<1e-9);
        });
        CHECK(holds<=static_cast<int>(r.lead()));   // one cast cycle at the onset: 1 frame, or 2 in C14 alternate mode
    }
}

// Sliding past the end of a wall at 3-4 m/s: once the wall ends, its plane is refuted but still holds the hand off the
// raw sweep's path for a few frames. A step from a validated hand is accepted, so there is no hold beyond the onset
// cycle, the hand never enters the wall, and it follows the target past the end.
void slide_past_wall_end() {
    for(const double speed:{3.,4.}) {
        Rig r;r.world.floor=false;r.world.wall_x_end=.5;
        const Vec3 start{1.03,-.5,.5};
        r.step({.5,-.5,.5});
        r.move_x({.5,-.5,.5},start.x,1,ignore);
        for(int i=0;i<10;++i) r.step(start);
        const int before=r.holds;
        r.move_to(start,{1.03,1.5,.5},speed,[&](const ClampOutput& o,Vec3) {
            if(o.drawn.position.y<.5-.05) CHECK(o.drawn.position.x<=.96);
        });
        CHECK(r.holds-before<=static_cast<int>(r.lead()));
        for(int i=0;i<30;++i) r.step({1.03,1.5,.5});
        CHECK(close_to(r.last.drawn.position,{1.03,1.5,.5},1e-9));
    }
}

// A target that no answered sweep validates is held at most max_hold_frames in a row, and once the holds run out the
// hand does not go back to holding until a frame validates again (no hold/jump stutter).
void hold_lasts_two_frames() {
    Rig r;r.world.wall_x=false;r.world.floor=false;r.world.floor_first=true;   // every sweep is cut at its start
    const unsigned limit=HandClamp::max_hold_frames*static_cast<unsigned>(r.lead());   // two casts: 4 frames in C14
    const Vec3 a{.5,0,.5},b{.5,.3,.5};
    for(int i=0;i<4;++i) r.step(a);
    ClampOutput o{};
    for(unsigned i=0;i<limit;++i) {o=r.step(b);CHECK(o.held && close_to(o.drawn.position,a,1e-12));}
    o=r.step(b);CHECK(!o.held && close_to(o.drawn.position,b,1e-12));
    o=r.step(b);CHECK(!o.held);
    for(int i=0;i<12;++i) o=r.step(a);   // back to a: holds, then validated again
    CHECK(!o.held && close_to(o.drawn.position,a,1e-12));
    unsigned holds=0;
    for(int i=1;i<=20;++i) {o=r.step(a+Vec3{0,.3*i,0});holds+=o.held;}   // a new 0.3 m jump every frame
    CHECK(holds==limit && close_to(o.drawn.position,a+Vec3{0,6,0},1e-12));
}

// A snap turn swings the target 4.6 cm into a wall in one frame: hold, never draw it inside.
void snap_turn_holds_out_of_wall() {
    Rig r;r.root=level_frame({0,0,.5},{1,0,0});r.world.chest={0,0,.9};
    const double a=50*3.14159265358979/180;
    const Vec3 hand_local{1.05*std::cos(a),1.05*std::sin(a),0};
    const auto place=[&]() {auto t=r.root;t.position=apply(r.root,hand_local);return t;};
    for(int i=0;i<5;++i) r.step_pose(place());
    const double turn=-45*3.14159265358979/180;
    r.root=level_frame({0,0,.5},{std::cos(turn),std::sin(turn),0});
    CHECK(apply(r.root,hand_local).x>1.04);  // the target is inside the wall
    bool held=false;
    for(int i=0;i<30;++i) {const auto o=r.step_pose(place());CHECK(o.drawn.position.x<=.96);held|=o.held;}
    CHECK(held && std::abs(r.last.drawn.position.x-.945)<1e-6 && r.clamp.snapbacks(0)==0);
}

// A 100 ms hitch while approaching at 2 m/s: the hand moved 0.2 m in one frame.
void hitch_holds_out_of_wall() {
    for(const double hitch_at:{.7,.8,.85,.9,.94}) {
        Rig r;double x=.5;r.step({x,0,.5});
        bool hitched=false;
        while(x<1.2) {
            if(!hitched && x>=hitch_at) {r.t0+=.1-dt;x+=.2;hitched=true;}
            else x=std::min(1.2,x+2*dt);
            const auto o=r.step({x,0,.5});
            CHECK(o.drawn.position.x<=.96);
        }
        for(int i=0;i<10;++i) {const auto o=r.step({1.2,0,.5});CHECK(o.drawn.position.x<=.95);}
        CHECK(hitched && r.clamp.snapbacks(0)==0);
    }
    for(const double hitch_at:{.7,.85,.9}) {   // a hitch on a diagonal approach into a corner
        Rig r;r.world.wall_y=true;double x=.5;r.step({x,x,.5});
        bool hitched=false;
        while(x<1.1) {
            if(!hitched && x>=hitch_at) {r.t0+=.1-dt;x+=.15;hitched=true;}
            else x=std::min(1.1,x+1.5*dt);
            const auto o=r.step({x,x,.5});
            CHECK(o.drawn.position.x<=.96 && o.drawn.position.y<=.96 && !o.snapped);
        }
        for(int i=0;i<30;++i) {const auto o=r.step({1.1,1.1,.5});CHECK(o.drawn.position.x<=.95 && o.drawn.position.y<=.95);}
        CHECK(hitched && r.clamp.snapbacks(0)==0);
    }
}

// C14 alternate mode: each side casts every other frame, so the sweep reaches two frames ahead.
void alternate_mode_predicts_two_frames() {
    Rig r;r.post=[](unsigned frame) {return (frame&1u)==0;};
    Vec3 previous{.5,0,.5};
    r.step(previous);
    bool checked=false;int holds=0;
    r.move_x(previous,1.25,3,[&](const ClampOutput& o,Vec3 target) {
        CHECK(o.drawn.position.x<=.95+.005);
        holds+=o.held;   // a 3 m/s start covers 6.7 cm before the first answered sweep that saw it move: one hold
        if(o.palm_sweep.valid && r.frame>2 && o.state==ClampState::free && !o.held) {
            CHECK(close_to(o.palm_sweep.to,target+(target-previous)*2,1e-6));checked=true;
        } else if(r.frame>2 && !o.palm_sweep.valid) CHECK((r.frame&1u)==0);   // frame was incremented: odd frames skip
        previous=target;
    });
    for(int i=0;i<10;++i) {const auto o=r.step(previous);CHECK(o.drawn.position.x<=.95+.005);}
    CHECK(checked && holds<=1);
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
    auto o=r.step({.5,.04,.5});CHECK(close_to(o.palm_sweep.from,{.5,0,.5},1e-9));
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

// A door that forms the second wall of a corner opens: the hand is free of it within 0.3 s.
void door_opens_on_corner() {
    Rig r;r.world.wall_y=true;
    const Vec3 pressed{1.05,1.03,.5};
    r.step({.5,.5,.5});
    r.move_to({.5,.5,.5},pressed,1,[](const ClampOutput& o,Vec3) {CHECK(o.drawn.position.x<=.96 && o.drawn.position.y<=.96);});
    for(int i=0;i<45;++i) {   // held in the corner, without flicker
        const auto o=r.step(pressed);
        CHECK(o.drawn.position.x<=.95 && o.drawn.position.y<=.95 && o.drawn.position.y>=.945-1e-6 && !o.snapped);
    }
    r.world.wall_y=false;
    for(int i=1;i<=90;++i) {
        const auto o=r.step(pressed);
        CHECK(o.drawn.position.x<=.95 && !o.snapped);
        if(i*dt>=.3) CHECK(std::abs(o.drawn.position.y-pressed.y)<1e-3);
    }
}

// A palm pressed onto a table and into a crate; the crate goes away while the hand slides 0.6 m along the table.
void crate_removed_while_sliding() {
    Rig r;   // floor = table top, wall x = crate face
    Vec3 pressed{.99,0,-.05};
    r.step({.5,0,.5});
    r.move_to({.5,0,.5},pressed,1,[](const ClampOutput& o,Vec3) {CHECK(o.drawn.position.x<=.96 && o.drawn.position.z>=.04);});
    for(int i=0;i<27;++i) {
        const auto o=r.step(pressed);
        CHECK(std::abs(o.drawn.position.x-.945)<1e-6 && std::abs(o.drawn.position.z-.055)<1e-6);
    }
    r.world.wall_x=false;
    for(int i=1;i<=45;++i) {
        if(i<=27) pressed.y+=.6/27;   // 0.6 m in 0.3 s
        const auto o=r.step(pressed);
        CHECK(o.drawn.position.z>=.055-1e-6 && !o.snapped);
        if(i*dt>=.3) CHECK(close_to(o.drawn.position,{pressed.x,pressed.y,.055},1e-3));
    }
}

// A plane that some nearer surface always shadows (so no sweep can disprove it) still expires: shadowing refreshes it
// for at most shadow_seconds after its last direct hit, then it ages out.
void shadow_refresh_is_capped() {
    Rig r;r.world.floor=false;
    const Vec3 pressed{.99,0,.5};
    r.step({.5,0,.5});
    r.move_x({.5,0,.5},pressed.x,1,ignore);
    for(int i=0;i<5;++i) r.step(pressed);
    r.world.wall_x=false;r.world.floor_first=true;
    unsigned frames=0;ClampOutput o{};
    do {
        o=r.step(pressed);++frames;
        if(frames*dt<=.15) CHECK(o.drawn.position.x<=.95);   // the shadow holds it for now
    } while(std::abs(o.drawn.position.x-pressed.x)>1e-9 && frames<90);
    CHECK(frames*dt<=HandClamp::shadow_seconds+.1+.045/1.5+3*dt);
}

void haptic_cooldown() {
    Rig r;r.step({.5,0,.5});
    int haptics=0,events=0;
    r.move_x({.5,0,.5},.951,1,[&](const ClampOutput& o,Vec3) {haptics+=o.haptic.has_value();events+=o.event.has_value();});
    CHECK(events==1 && haptics==1);
    auto o=r.step({.9,0,.5});CHECK(o.state==ClampState::free);
    o=r.step({.951,0,.5});
    CHECK(o.event && !o.haptic);             // a new contact within 0.08 s of the last buzz: no second buzz
    for(int i=0;i<9;++i) o=r.step({.9,0,.5});
    for(int i=0;i<3 && !o.event;++i) o=r.step({.951,0,.5});
    CHECK(o.event && o.haptic);              // after the cooldown it buzzes again
}

void contract_counters() {
    HandClamp c;ClampInput in{};in.root=level_frame({},{1,0,0});in.target=pose_at({.5,0,.5});in.chest={0,0,1};
    const int before=contract_breaks;
    in.now=0;c.update(0,in);                         // no note_drawn after it
    in.now=dt;auto first=c.update(0,in);
    CHECK(c.missed_notes(0)==1 && contract_breaks==before+1);
    c.note_drawn(0,in.root,first.drawn.position);
    in.palm_hit=ClampHit{{1,0,.5},{-1,0,0},7,3,false};
    const auto again=c.update(0,in);                 // the same frame again
    CHECK(c.repeated_updates(0)==1 && contract_breaks==before+2);
    CHECK(close_to(again.drawn.position,first.drawn.position,0) && close_to(again.palm_sweep.to,first.palm_sweep.to,0));
    c.note_drawn(0,in.root,again.drawn.position);
    c.note_drawn(0,in.root,again.drawn.position);    // a second note for one update
    CHECK(contract_breaks==before+3 && c.missed_notes(0)==1 && c.repeated_updates(0)==1 && c.missed_notes(1)==0);
    contract_breaks=before;
}

// A non-finite chest on a re-acquire cannot validate the path: stay released (drawn at the controller) and say so.
void chest_non_finite_stays_released() {
    Rig r;r.world.chest={nan_value,0,1};
    const Vec3 target{.5,0,.5};
    for(int i=0;i<10;++i) {
        const auto o=r.step(target);
        CHECK(o.valid && o.state==ClampState::released && close_to(o.drawn.position,target,0) && !o.palm_sweep.valid && !o.snapped);
    }
    CHECK(r.clamp.reacquire_blocked_frames(0)==10 && r.clamp.snapbacks(0)==0);
    r.world.chest={0,0,1};
    ClampOutput o{};
    for(int i=0;i<12 && o.state!=ClampState::free;++i) o=r.step(target);
    CHECK(o.state==ClampState::free && r.clamp.reacquire_blocked_frames(0)==10);
}

void tip_probe_stops_fingers() {
    const ClampProbe tip{{.12,0,0},.03};
    for(const double speed:{1.,2.}) {
        Rig r;r.tip=tip;r.tip_enabled=true;
        r.step({.5,0,.5});
        Vec3 previous{.5,0,.5},previous_drawn{.5,0,.5};
        ClampOutput o=r.move_x({.5,0,.5},1.1,speed,[&](const ClampOutput& q,Vec3 target) {
            CHECK(q.drawn.position.x+.12<=1-.03+.005);
            CHECK(q.tip_sweep.valid==r.posting());
            if(q.tip_sweep.valid) {
                CHECK(std::abs(q.tip_sweep.radius-.03)<1e-12);
                CHECK(close_to(q.tip_sweep.from,previous_drawn+Vec3{.12,0,0},1e-9));                 // previous drawn fingertip
                CHECK(close_to(q.tip_sweep.to,q.palm_sweep.to+Vec3{.12,0,0},1e-9));               // the palm's end, offset
                if(q.state==ClampState::free && !q.held)                                            // + v*dt (two in C14)
                    CHECK(close_to(q.tip_sweep.to,target+Vec3{.12,0,0}+(target-previous)*r.lead(),1e-6));
            }
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
    CHECK(!b.tip_enabled() && b.alternate() && std::abs(b.restore_factor()-2.5)<1e-9);   // 550 -> 100 measured, capped at 2.5
    run(b,100,8,40,false);                    // predicted 250 then 120 (600 -> 550 floored at 1.2): both come back
    CHECK(b.tip_enabled() && !b.alternate());
    // Mode-dependent scenes (the cost of each mode), counting flips of either switch.
    struct Scene {double tip,no_tip,alternate;};
    const auto play=[](CastBudget& budget,const std::function<Scene(double)>& scene,double from,double to,std::vector<double>* flips) {
        for(int i=1;from+i*dt<=to+1e-9;++i) {
            const double t=from+i*dt;const auto c=scene(t);const bool tip=budget.tip_enabled(),alt=budget.alternate();
            budget.note(alt?c.alternate:tip?c.tip:c.no_tip,t);
            if(flips && (tip!=budget.tip_enabled() || alt!=budget.alternate())) flips->push_back(t);
        }
    };
    {   // N2: a steady 600 us scene (300 without the tips, 150 alternating) flips once (the tips go) and never comes back.
        CastBudget steady;std::vector<double> flips;
        play(steady,[](double){return Scene{600,300,150};},0,180,&flips);
        CHECK(flips.size()==1 && !steady.tip_enabled() && !steady.alternate() && std::abs(steady.restore_factor()-2)<.01);
    }
    {   // The scene gets cheap: the tips come back once the predicted cost stays under 0.4 ms for 5 s, and stay.
        CastBudget lighter;std::vector<double> flips;
        play(lighter,[](double t){return t<10?Scene{600,300,150}:Scene{320,160,80};},0,60,&flips);
        CHECK(flips.size()==2 && flips[1]>=15-1e-6 && flips[1]<=18 && lighter.tip_enabled());
    }
    {   // A short rise of the predicted cost to 0.4 ms or more restarts the wait.
        CastBudget dip;std::vector<double> flips;
        play(dip,[](double t){return t<10?Scene{600,300,150}:t>=13 && t<13.5?Scene{900,500,250}:Scene{320,160,80};},0,60,&flips);
        CHECK(flips.size()==2 && flips[1]>=13.5+5-1e-6);
    }
    {   // Backoff: a scene that only fits without the tips but measures a capped factor (600 / 90 -> 4, predicted 360) recovers and
        // steps down again; each wait doubles, 5, 10, 20 ... up to 120 s.
        CastBudget backoff;std::vector<double> flips;
        play(backoff,[](double){return Scene{600,90,45};},0,600,&flips);
        CHECK(flips.size()>=13 && flips.size()%2==1);
        std::vector<double> waits;for(std::size_t i=1;i+0<flips.size();i+=2) waits.push_back(flips[i]-flips[i-1]);
        CHECK(waits.size()>=6);   // from each step down to the next recovery: a covered window (2 s), then 5, 10, 20, 40, 80, 120 ... s
        for(std::size_t i=0;i<waits.size();++i) {const double expect=2+std::min(5*std::pow(2.,double(i)),120.);CHECK(std::abs(waits[i]-expect)<.05);}
        unsigned in_last_minute=0;for(const double f:flips) in_last_minute+=f>540;CHECK(in_last_minute<=2);
    }
    {   // N4: one slow first scope after a load (a 15 s gap) decides nothing, twice; nor does one right after reset_window().
        CastBudget load;std::vector<double> flips;const auto cheap=[](double){return Scene{200,100,50};};
        play(load,cheap,0,10,&flips);
        load.note(900,25);play(load,cheap,25,40,&flips);
        load.note(900,55);load.note(900,55.011);play(load,cheap,55.011,90,&flips);
        CHECK(flips.empty() && load.tip_enabled() && !load.alternate());
        load.reset_window();load.note(5000,90.02);play(load,cheap,90.02,91,&flips);CHECK(flips.empty());
        // After reset_window() the 600 us that follows needs its own covered window (2 s); mixed into the old one it crossed 0.5 ms at ~1.5 s.
        load.reset_window();play(load,[](double){return Scene{600,300,150};},91,92.9,&flips);CHECK(flips.empty() && load.tip_enabled());
        // A window needs 60 samples: a slow stream (2 Hz) over budget for 10 s decides nothing.
        CastBudget sparse;for(int i=0;i<20;++i) sparse.note(900,i*.2);CHECK(sparse.tip_enabled());
        // Continuous cost over budget after a reset still steps down, on a covered window.
        CastBudget heavy;std::vector<double> f2;heavy.reset_window();
        play(heavy,[](double){return Scene{900,300,150};},0,3,&f2);CHECK(f2.size()==1 && std::abs(f2[0]-2)<.02 && !heavy.tip_enabled());
    }
    {   // An affordable scene never steps down.
        CastBudget fits;std::vector<double> flips;play(fits,[](double){return Scene{420,200,100};},0,60,&flips);CHECK(flips.empty() && fits.tip_enabled());
    }
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

// The meet composed with the clamp: a meet shift never moves the drawn hand through a plane the clamp holds; the met
// prop's own planes are skipped; not meeting, the clamp's hand stands; a side holding nothing passes the meet through.
void meet_never_passes_held_plane() {
    Rig r;r.step({.5,0,.5});
    ClampOutput o{};
    for(int i=0;i<30;++i) o=r.step({1.2,0,.5});   // pressed into the wall at x = 1 (entity 7): held at 0.945
    CHECK(o.state==ClampState::contact && std::abs(o.drawn.position.x-.945)<1e-6);
    CHECK(std::abs(clamp_depth(o)-.2)<1e-6);   // the tracked grip 20 cm past the wall, beyond the 5.5 cm shell
    CHECK(clamp_depth(ClampOutput{})==0);
    const auto target=pose_at({1.2,0,.5});
    Vec3 d=meet_drawn(r.clamp,0,o,target,true,std::nullopt,{.03,0,0});            // the meet would draw it 23 cm into the wall
    CHECK(std::abs(d.x-.945)<1e-6 && std::abs(d.y)<1e-12 && std::abs(d.z-.5)<1e-12);
    d=meet_drawn(r.clamp,0,o,target,true,std::uintptr_t{8},{0,.02,0});            // another prop met: the wall still holds; the shift's y stands
    CHECK(std::abs(d.x-.945)<1e-6 && std::abs(d.y-.02)<1e-12);
    d=meet_drawn(r.clamp,0,o,target,true,std::uintptr_t{7},{-.02,0,0});           // the wall is the met prop: its plane is skipped
    CHECK(close_to(d,{1.18,0,.5},1e-12));
    d=meet_drawn(r.clamp,0,o,target,false,std::uintptr_t{7},{-.02,0,0});          // not meeting: the clamp's hand
    CHECK(close_to(d,o.drawn.position,0));
    CHECK(close_to(r.clamp.keep_out(1,target.basis,{.8,-.2,.5},std::nullopt),{.8,-.2,.5},0));   // the other side holds nothing
    CHECK(close_to(r.clamp.keep_out(5,target.basis,{.8,-.2,.5},std::nullopt),{.8,-.2,.5},0));
    r.clamp.reset(0);
    CHECK(close_to(r.clamp.keep_out(0,target.basis,{.8,-.2,.5},std::nullopt),{.8,-.2,.5},0));   // reset: nothing held
}

// Fix round 2: the held_elsewhere start, the way out clipped at the prop's plane, the met prop and a wall both held.
void meet_start_and_clip() {
    Rig r;r.step({.5,0,.5});
    ClampOutput o{};
    for(int i=0;i<30;++i) o=r.step({1.2,0,.5});   // held at the wall (entity 7) at 0.945
    const auto target=pose_at({1.2,0,.5});
    // Another prop is met while the clamp holds the hand at the wall: the meet starts from the clamp's hand, not the target.
    Vec3 d=meet_drawn(r.clamp,0,o,target,true,std::uintptr_t{8},{-.1,0,0});
    CHECK(std::abs(d.x-.845)<1e-6);                                    // from the target it would be 1.1, kept out to 0.945
    // The met prop is the clamp's contact: a way out pointing into it (stale, or flipped past its middle) is clipped at its plane.
    d=meet_drawn(r.clamp,0,o,target,true,std::uintptr_t{7},{.05,.02,0});
    CHECK(close_to(d,{1.2,.02,.5},1e-12));                             // not 1.25: only the part along the plane stands
    // The met prop and a wall both held (the y wall plays the prop): the prop's plane is skipped, the x wall still holds.
    Rig c;c.world.wall_y=true;c.step({.5,.5,.5});
    o=c.move_to({.5,.5,.5},{1.1,1.1,.5},1,ignore);
    for(int i=0;i<10;++i) o=c.step({1.1,1.1,.5});
    CHECK(o.state==ClampState::contact);
    CHECK(o.drawn.position.x<=.945+1e-6 && o.drawn.position.y<=.945+1e-6);
    const auto corner=pose_at({1.1,1.1,.5});
    d=meet_drawn(c.clamp,0,o,corner,true,std::uintptr_t{9},{});
    CHECK(d.x<=.945+1e-6);
}

// I1: a loose prop in front of a wall hides it from the closest-hit sweeps; the exclusion sweep holds it.
void exclusion_sweep_holds_wall_behind_prop() {
    for(const bool excluded:{false,true}) {
        Rig r;r.world.slab=true;r.tip={{.08,0,0},.02};r.tip_enabled=true;r.loose={20};
        if(excluded) r.exclude=std::uintptr_t{20};
        r.step({.5,0,.5});
        ClampOutput o{};
        r.move_x({.5,0,.5},1.3,1.5,[&](const ClampOutput& q,Vec3) {o=q;});
        for(int i=0;i<5;++i) o=r.step({1.3,0,.5});
        CHECK(o.state==ClampState::contact && o.contact && o.contact->entity==20);
        CHECK(o.exclude_sweep.valid && o.exclude_sweep.exclude && *o.exclude_sweep.exclude==20); // posted either way: the touched loose prop is excluded
        // The meet draws the target (the prop is met), kept out of the wall the exclusion sweep found: the tip at 1 - skin.
        const Vec3 d=meet_drawn(r.clamp,0,o,pose_at({1.3,0,.5}),true,std::uintptr_t{20},{});
        CHECK(std::abs(d.x-(1-.005-.02-.08))<1e-6);
    }
    { // Without a loose list the touched slab is not excluded, and nothing posts the exclusion sweep.
        Rig r;r.world.slab=true;r.step({.5,0,.5});
        ClampOutput o{};
        for(int i=0;i<20;++i) o=r.step({1.3,0,.5});
        CHECK(!o.exclude_sweep.valid);
        CHECK(meet_drawn(r.clamp,0,o,pose_at({1.3,0,.5}),true,std::uintptr_t{20},{}).x>1.29);  // the wall unknown: through it
    }
}

// I2: a loose prop on the hand (a toppled bottle) never snaps the clamp; a world overlap still does.
void loose_overlap_never_snaps() {
    Rig r;r.world.wall_x=false;r.world.floor=false;r.world.palm_inside=true;r.world.palm_inside_loose=true;r.loose={20};
    const Vec3 target{.5,0,.5};
    for(int i=0;i<90;++i) {const auto o=r.step(target);CHECK(!o.snapped && o.state!=ClampState::released);}
    CHECK(r.clamp.snapbacks(0)==0);
    r.loose.clear();                                   // not loose (the service dropped it): three overlaps snap
    ClampOutput o{};for(int i=0;i<4;++i) o=r.step(target);
    CHECK(r.clamp.snapbacks(0)==1 && o.state==ClampState::released);
}

// I2: a release re-arms within 0.25 s when only a loose prop stops the chest re-acquire.
void loose_prop_never_keeps_released() {
    Rig r;press_until_snap(r);
    r.world.chest_prop=true;r.loose={21};
    const Vec3 free_space{.5,0,.5};
    unsigned frames=0;ClampOutput o{};
    do {o=r.step(free_space);++frames;} while(o.state==ClampState::released && frames<60);
    CHECK(o.state!=ClampState::released && frames*dt<=.25);
    Rig k;press_until_snap(k);k.world.chest_prop=true;   // not loose: the chest path is blocked, the side stays released
    for(int i=0;i<45;++i) {o=k.step(free_space);CHECK(o.state==ClampState::released);}
}

// I3: contact_fresh says the contact plane was made or re-hit by the last answered cast.
void contact_fresh_tracks_answers() {
    Rig r;r.step({.5,0,.5});
    ClampOutput o{};
    for(int i=0;i<30;++i) o=r.step({1.2,0,.5});
    CHECK(o.contact && o.contact_fresh);
    r.drop_palm=[](unsigned){return true;};            // the results stop
    o=r.step({1.2,0,.5});
    CHECK(o.contact && o.contact_fresh);               // one stale answer is survived (fix round 3: no latch flicker at 4-5 m/s)
    o=r.step({1.2,0,.5});
    CHECK(o.contact && o.contact->entity==7 && !o.contact_fresh);   // two in a row: the plane is held, not fresh
}

// Fix round 3: a loose prop's plane is where the prop was. A new hit on the same face replaces it, and a face no raw cast
// re-hits twice in a row is dropped: the hand follows a bottle knocked away instead of staying at its old face for 0.1 s.
void moving_prop_planes_follow_the_prop() {
    Rig r;r.world.slab=true;r.loose={20};r.step({.5,0,.5});
    ClampOutput o=r.move_x({.5,0,.5},.95,1,ignore);   // 20.5 cm into it: under the snap gap
    for(int i=0;i<10;++i) o=r.step({.95,0,.5});
    CHECK(o.contact && o.contact->entity==20 && std::abs(o.drawn.position.x-.745)<1e-6);
    r.world.slab_x=.86;                                 // the bottle was knocked 6 cm on
    unsigned frames=0;
    do {o=r.step({.95,0,.5});++frames;} while(o.drawn.position.x<.80 && frames<20);
    CHECK(frames<=5 && std::abs(o.drawn.position.x-.805)<1e-6);   // two raw casts miss the old face (one result late): 5 frames, not plane_seconds
}

// Fix round 3: the meet drew somewhere else than the clamp (note_drawn): the clamp's ease continues from what was drawn, and
// taking the hand back does not throw it to the hold it was easing on its own.
void note_drawn_carries_the_ease() {
    Rig r;r.step({.5,0,.5});
    ClampOutput o{};
    for(int i=0;i<30;++i) o=r.step({1.2,0,.5});        // held at 0.945, 25.5 cm short of the target
    r.world.wall_x=false;                               // the wall is gone; the meet draws the target meanwhile
    for(int i=0;i<12;++i) {r.note_override=Vec3{1.2,0,.5};o=r.step({1.2,0,.5});}
    o=r.step({1.2,0,.5});                               // the clamp draws again: from the target, not from its own 25 cm hold eased 12 frames
    CHECK(close_to(o.drawn.position,{1.2,0,.5},1e-6));
}

// Fix round 2 minor: over a met thin prop on a table, the exclusion sweep's table is held by the palm too (≤ 0.2 cm into it).
void thin_prop_palm_stays_out_of_table() {
    for(const bool wall_tip:{false,true}) {   // fix round 4: the wall tip (the same forward probe) sweeps excluded in its own slot, the palm still leads into the prop
        Rig r;r.world.wall_x=false;r.world.thin=true;r.loose={22};r.exclude=std::uintptr_t{22};
        r.tip={{.09,0,0},.035};r.tip_enabled=true;if(wall_tip) r.wall_tip=r.tip;
        r.step({0,0,.5});
        double deepest=0;
        const auto watch=[&](const ClampOutput& o,Vec3 p) {
            const Vec3 d=meet_drawn(r.clamp,0,o,pose_at(p),true,std::uintptr_t{22},{});
            deepest=std::max(deepest,.05-d.z);              // the palm sphere's depth into the table (z = 0)
            r.note_override=d;
        };
        r.move_to({0,0,.5},{.5,0,-.05},1,watch);           // forward and down: the forward tip leads the motion, the palm leads into the prop's top face
        for(int i=0;i<30;++i) {const auto o=r.step({.5,0,-.05});watch(o,{.5,0,-.05});}
        CHECK(deepest<=.002);
        CHECK(r.last.exclude_sweep.valid && r.last.exclude_sweep.radius==.05);   // the palm leads into the prop's top face
        CHECK(r.last.wall_tip_sweep.valid==wall_tip && (!wall_tip || (r.last.wall_tip_sweep.exclude && *r.last.wall_tip_sweep.exclude==22)));
    }
}

// Fix round 4: the leading probe follows the motion's own direction (no axis snapping), and the wall tip leads its horizontal
// part: a wall meets first the probe that reaches farthest horizontally, however steeply the hand rises or falls.
void leading_probes_follow_motion_and_walls() {
    const double pi=3.14159265358979323846,c75=std::cos(75*pi/180),s75=std::sin(75*pi/180);
    const std::array<ClampProbe,3> hand{{{{.114,.065,-.011},.019},{{.091,.016,-.133},.012},{{.030,.015,.026},.027}}};   // thumb tip, a finger tip hanging down, the back of the hand
    const auto grip=pose_at({});const ClampProbe fallback{{.09,0,0},.035};
    CHECK(leading_probe(hand,grip,{1,0,-.3},fallback).local.x==.091);          // descending at 0.3: the hanging finger leads the motion
    CHECK(leading_probe(hand,grip,{c75,0,s75},fallback).local.x==.030);        // rising at 75 degrees: the back of the hand
    CHECK(wall_leading_probe(hand,grip,{1,0,-.3},fallback).local.x==.114);     // ... but the thumb meets a wall first
    CHECK(wall_leading_probe(hand,grip,{c75,0,s75},fallback).local.x==.114);   // (5ee02eb: 7-8 cm into the wall at 45-75 degrees)
    CHECK(wall_leading_probe(hand,grip,Vec3{c75,0,-s75}*.146,fallback).local.x==.114);   // 3.8 cm/s sideways is enough
    CHECK(wall_leading_probe(hand,grip,{0,0,-1},fallback).local.x==.09);       // straight down: no horizontal lead, the probe stays
    CHECK(wall_leading_probe(hand,grip,{.005,0,-1},hand[0]).local.x==.114);   // under 1 cm/s sideways: kept
    CHECK(wall_leading_probe(hand,grip,{.03,0,0},hand[1]).local.x==.091);    // under 0.05 m/s: kept
    // A probe in the lead is kept until another leads it by more than 5 mm: about 45 degrees the choice does not flicker.
    const std::array<ClampProbe,2> pair{{{{.10,0,0},.02},{{0,0,.10},.02}}};   // forward, up
    const auto at=[&](double degrees) {return Vec3{std::cos(degrees*pi/180),0,std::sin(degrees*pi/180)};};
    ClampProbe current=leading_probe(pair,grip,at(40),fallback);
    CHECK(current.local.x==.10);
    unsigned flips=0;
    for(int i=0;i<90;++i) {const auto next=leading_probe(pair,grip,at(i%2?44.:46.),current);flips+=!(next.local.z==current.local.z);current=next;}
    CHECK(flips==0 && current.local.x==.10);                                   // the up probe leads by 2.5 mm at 46 degrees
    CHECK(leading_probe(pair,grip,at(50),current).local.z==.10);              // by 1.2 cm at 50: it takes over
    CHECK(wall_leading_probe(pair,grip,at(80),pair[1]).local.x==.10);         // a probe that does not lead horizontally is not kept
}

// Fix round 4: rising at 60 degrees into a wall, the probe leading the motion is the back of the hand; the wall tip (forward) is
// swept too and holds the hand at the wall.
void wall_tip_holds_a_rising_hand() {
    for(const bool wall_tip:{true,false}) {
        Rig r;r.world.floor=false;r.tip={{0,0,.06},.03};r.tip_enabled=true;
        if(wall_tip) r.wall_tip={{.12,0,0},.02};
        r.step({.5,0,.5});
        double reach=0;unsigned swept=0;
        r.move_to({.5,0,.5},{1.2,0,.5+.7*std::tan(60*3.14159265358979323846/180)},1,[&](const ClampOutput& o,Vec3) {
            reach=std::max(reach,o.drawn.position.x+.12+.02);   // the forward probe's front
            swept+=o.wall_tip_sweep.valid;
        });
        CHECK((swept>0)==wall_tip);
        if(wall_tip) CHECK(reach<=1);             // held at the wall by the wall tip's plane
        else CHECK(reach>1.05);                   // the palm alone: the forward probe 8.5 cm into the wall
    }
    if(g_mode==0) {   // the wall tip that is the tip is swept once (every frame casts here)
        Rig r;r.tip={{.12,0,0},.02};r.tip_enabled=true;r.wall_tip=r.tip;
        r.step({.5,0,.5});                         // the re-acquire
        const auto o=r.step({.5,0,.5});CHECK(o.tip_sweep.valid && !o.wall_tip_sweep.valid);
        r.tip_enabled=false;                       // C14 drops the fingertip sweeps: both
        const auto q=r.step({.5,0,.5});CHECK(!q.tip_sweep.valid && !q.wall_tip_sweep.valid);
    }
}

// Fix round 4: pushing a loose slab into a wall, the wall tip sweeps with the slab excluded in its own slot; the probe leading into
// the slab's pressed face is that same forward probe, so no second exclusion sweep runs. The wall behind holds the forward probe.
void wall_tip_sweeps_past_the_pressed_prop() {
    Rig r;r.world.slab=true;r.tip={{0,0,.06},.03};r.tip_enabled=true;r.wall_tip={{.08,0,0},.02};r.loose={20};r.exclude=std::uintptr_t{20};
    r.step({.5,0,.5});
    ClampOutput o{};
    r.move_x({.5,0,.5},1.3,1.5,[&](const ClampOutput& q,Vec3) {o=q;});
    for(int i=0;i<5;++i) o=r.step({1.3,0,.5});
    CHECK(o.wall_tip_sweep.valid && o.wall_tip_sweep.exclude && *o.wall_tip_sweep.exclude==20 && o.wall_tip_sweep.radius==.02);
    CHECK(!o.exclude_sweep.valid);
    const Vec3 d=meet_drawn(r.clamp,0,o,pose_at({1.3,0,.5}),true,std::uintptr_t{20},{});
    CHECK(std::abs(d.x-(1-.005-.02-.08))<1e-6);
}

// Final review I1: a dead guard (entity 20 stands in for the corpse) pushed against a wall. The game thread now maps his exclusion to his
// limb, so the wall tip's excluded sweep passes through him and the wall behind is held (the drawn hand stops short of it, as for a slab).
// A sweep that could not be mapped is cast without exclusion; the corpse then answers it and, being the excluded entity, adds no plane.
void corpse_pushed_against_a_wall_holds_the_wall() {
    for(const bool mapped:{true,false}) {
        Rig r;r.world.slab=true;r.world.sweeps_ignore_exclusion=!mapped;r.tip={{0,0,.06},.03};r.tip_enabled=true;r.wall_tip={{.08,0,0},.02};
        r.loose={20};r.exclude=std::uintptr_t{20};
        r.step({.5,0,.5});
        ClampOutput o{};
        r.move_x({.5,0,.5},1.3,1.5,[&](const ClampOutput& q,Vec3) {o=q;});
        for(int i=0;i<5;++i) o=r.step({1.3,0,.5});
        CHECK(o.wall_tip_sweep.valid && o.wall_tip_sweep.exclude && *o.wall_tip_sweep.exclude==20);   // posted either way: never dropped
        const Vec3 d=meet_drawn(r.clamp,0,o,pose_at({1.3,0,.5}),true,std::uintptr_t{20},{});
        if(mapped) CHECK(std::abs(d.x-(1-.005-.02-.08))<1e-6);   // the wall behind the corpse is a plane
        else CHECK(d.x>1-.005-.02-.08+1e-3);                       // unmapped: the corpse hides the wall (the degraded case the fix leaves)
    }
}

// Fix round 4 (B): what a caller draws on top of the clamp's hand (the meet's way out, the lab's table lift) never counts toward
// the snap-back gap; the drawn hand still eases from it (note_drawn_carries_the_ease).
void snap_timer_ignores_caller_additions() {
    Rig r;r.world.floor=false;r.step({.5,0,.5});
    ClampOutput o{};
    for(int i=0;i<10;++i) o=r.step({1.1,0,.5});            // held at 0.945: the clamp's own push is 15.5 cm
    for(int i=0;i<60;++i) {r.note_override=Vec3{.945,0,1.0};o=r.step({1.1,0,.5});   // a caller draws it 50 cm higher for 0.67 s
        CHECK(!o.snapped && o.state!=ClampState::released);}
    CHECK(r.clamp.snapbacks(0)==0 && o.drawn.position.z>.5);   // the drawn hand eases down from what was drawn
    for(int i=0;i<60;++i) o=r.step({1.1,0,.5});
    CHECK(close_to(o.drawn.position,{.945,0,.5},1e-6));
}

// Fix round 5: every held plane keeps the whole open hand out, by its deepest sphere along the plane's normal. A small hand: fingers
// 14 cm forward, two sides 6 cm out, one 8 cm down.
const std::vector<ClampProbe> small_hand{{{.12,0,0},.02},{{.06,.04,0},.02},{{.06,-.04,0},.02},{{0,0,-.06},.02}};

// Pressed into the wall and sliding along it, then still: the plane the forward fingers rest on is kept (no sweep reaches it: the
// palm is 9 cm back), so the fingers stay out; without the open hand only the palm is held and the fingers go 9 cm in.
void whole_hand_slides_along_a_wall() {
    for(const bool whole:{true,false}) {
        Rig r;r.world.floor=false;if(whole) r.hand=small_hand;
        r.tip=small_hand[0];r.tip_enabled=true;          // the forward finger leads the push into the wall
        r.step({.5,0,.5});
        double reach=0;
        const auto watch=[&](const ClampOutput& o,Vec3) {reach=std::max(reach,o.drawn.position.x+.14);};
        r.move_x({.5,0,.5},1.03,.5,watch);
        r.tip=small_hand[2];                              // the side probe leads the slide toward -y
        r.move_to({1.03,0,.5},{1.03,-.5,.5},.5,watch);
        for(int i=0;i<180;++i) watch(r.step({1.03,-.5,.5}),{});   // 2 s still
        if(whole) CHECK(reach<=1+1e-6);   // never past the wall
        else CHECK(reach>1.05);
    }
}

// Whole-hand planes and edges: past the end of a wall the hand is let go once a sweep refutes the plane; in a corner it is held
// out of both walls. Holds (frames drawn at last frame's hand while a jump is validated) are counted.
void whole_hand_edges_and_corners() {
    for(const double speed:{.5,3.}) for(const bool whole:{true,false}) {   // the hand slides past the end of the wall at y = 0.5
        Rig r;r.world.floor=false;r.world.wall_x_end=.5;if(whole) {r.hand=small_hand;r.tip=small_hand[0];r.tip_enabled=true;}
        const double side=whole?.06:.05,front=whole?.14:.05;   // how far the hand reaches sideways and forward
        const Vec3 start{1.03,-.5,.5};
        r.step({.5,-.5,.5});
        r.move_x({.5,-.5,.5},start.x,1,ignore);
        for(int i=0;i<10;++i) r.step(start);
        const int before=r.holds;unsigned caught=0;
        r.move_to(start,{1.03,1.5,.5},speed,[&](const ClampOutput& o,Vec3 p) {
            if(o.drawn.position.y<.5-side) CHECK(o.drawn.position.x+front<=1+1e-6);   // in front of the wall: out of it
            if(o.drawn.position.y>.5+side && o.drawn.position.x<p.x-1e-6) ++caught;   // the whole hand past the end and still held
        });
        if(g_mode==0) std::printf("%s past a wall end at %.1f m/s: %u frames caught, %d holds\n",whole?"whole hand":"palm only",speed,caught,r.holds-before);
        // a plane past its wall's end goes once a raw sweep refutes it and it ages out: 5 and 13 frames for the palm alone at 4c68d93
        CHECK(caught<=static_cast<unsigned>(std::lround(2*r.clamp.config().plane_seconds/dt))+2*static_cast<unsigned>(r.lead()) && r.holds-before<=static_cast<int>(r.lead()));
        for(int i=0;i<30;++i) r.step({1.03,1.5,.5});
        CHECK(close_to(r.last.drawn.position,{1.03,1.5,.5},1e-9));
    }
    {   // a static press into the corner x = 1, y = 1
        Rig r;r.world.wall_y=true;r.hand=small_hand;r.tip=small_hand[0];r.tip_enabled=true;r.wall_tip=small_hand[0];   // the forward finger leads
        r.step({.5,.5,.5});r.step({.5,.5,.5});
        const int before=r.holds;
        const auto inside=[&](const ClampOutput& o) {return std::max(o.drawn.position.x+.14-1,o.drawn.position.y+.06-1);};
        r.move_to({.5,.5,.5},{1.05,1.03,.5},1,[&](const ClampOutput& o,Vec3) {CHECK(inside(o)<=.01 && !o.snapped);});
        for(int i=0;i<270;++i) {const auto o=r.step({1.05,1.03,.5});CHECK(inside(o)<=.01 && !o.snapped);}
        if(g_mode==0) std::printf("whole hand in a corner: %d holds, %.4f m from both walls\n",r.holds-before,-inside(r.last));
        CHECK(r.last.state==ClampState::contact && inside(r.last)<=1e-6 && inside(r.last)>=-.005-1e-6 && r.clamp.snapbacks(0)==0);
    }
    for(const double speed:{1.,3.}) {   // pressed into the x wall, sliding into the y wall
        Rig r;r.world.wall_y=true;r.hand=small_hand;r.tip=small_hand[1];r.tip_enabled=true;r.wall_tip=small_hand[1];   // the side probe leads +y
        r.step({.5,0,.5});
        r.move_x({.5,0,.5},1.,1,ignore);
        for(int i=0;i<5;++i) r.step({1.,0,.5});
        const int before=r.holds;double worst=-1;
        r.move_to({1.,0,.5},{1.,1.15,.5},speed,[&](const ClampOutput& o,Vec3) {worst=std::max({worst,o.drawn.position.x+.14-1,o.drawn.position.y+.06-1});});
        for(int i=0;i<10;++i) {const auto o=r.step({1.,1.15,.5});worst=std::max({worst,o.drawn.position.x+.14-1,o.drawn.position.y+.06-1});}
        if(g_mode==0) std::printf("whole hand sliding into a corner at %.1f m/s: %d holds, deepest %.4f m\n",speed,r.holds-before,worst);
        CHECK(worst<=.01);
    }
}

void level_frame_and_hit_conversion() {
    auto f=level_frame({1,2,3},{0,2,5});
    CHECK(close_to(f.position,{1,2,3},0) && close_to(f.basis[0],{0,1,0},1e-12) && close_to(f.basis[1],{-1,0,0},1e-12) && close_to(f.basis[2],{0,0,1},1e-12));
    f=level_frame({0,0,0},{0,0,1});CHECK(close_to(f.basis[0],{1,0,0},0) && close_to(f.basis[1],{0,1,0},0));
    f=level_frame({0,0,0},{nan_value,0,0});CHECK(close_to(f.basis[0],{1,0,0},0));
    WorldTraceHit h{};h.point={1,2,3};h.normal={0,0,1};h.entity=42;h.material=9;h.initial_overlap=true;
    auto c=to_clamp_hit(h);
    CHECK(close_to(c.point,{1,2,3},0) && close_to(c.normal,{0,0,1},0) && c.entity==42 && c.material==9 && c.initial_overlap && c.normal_valid);
    h.normal_valid=false;h.initial_overlap=false;c=to_clamp_hit(h);CHECK(!c.normal_valid && !c.initial_overlap);
    HandClamp clamp;
    CHECK(!clamp.update(2,ClampInput{}).valid); // only sides 0 and 1
    { // post=false: nothing is posted (re-acquire or constraint), and the next frame has nothing to read
        HandClamp quiet;ClampInput in{};in.root=level_frame({},{1,0,0});in.target=pose_at({.5,0,.5});in.chest={0,0,1};
        in.tip={{.12,0,0},.03};in.post=false;
        for(int i=0;i<3;++i) {
            in.now=i*dt;
            in.palm_hit=ClampHit{{.5,0,.5},{0,0,1},1,1,true};  // ignored: this side posted nothing last frame
            const auto o=quiet.update(0,in);quiet.note_drawn(0,in.root,o.drawn.position);
            CHECK(o.valid && !o.palm_sweep.valid && !o.tip_sweep.valid && o.state==ClampState::free && close_to(o.drawn.position,{.5,0,.5},0));
        }
    }
    clamp.note_drawn(5,level_frame({},{1,0,0}),{});clamp.reset(5);
    CHECK(clamp.snapbacks(7)==0 && clamp.contact_frames(7)==0);
}
} // namespace

// Task 6b, cause A: two planes that disagree (a loose prop's face the wall's squeezes the hand against) leave the hand at the last one
// projected. The wall does not move; the prop does. The loose faces are projected first and a wall last, whatever order they were hit in.
// Fed by hand: the wall first, the prop's face second (the order that used to leave the hand 1 cm in the wall).
void walls_win_over_loose_faces() {
    HandClamp c;
    const auto basis=pose_at({}).basis;
    ClampProbe front{{.12,0,0},.03},back{{-.10,0,0},.03};   // an open hand 25 cm deep along x
    const auto frame=[&](unsigned i,Vec3 target,std::optional<ClampHit> palm,std::optional<ClampHit> tip) {
        ClampInput in{};
        in.now=i*dt;in.epoch=1;in.root=level_frame({0,0,0},{1,0,0});in.target=pose_at(target);in.chest={0,0,1};
        in.tip=back;in.tip_enabled=true;in.palm_hit=palm;in.tip_hit=tip;
        in.hand[0]=front;in.hand[1]=back;in.hand_count=2;
        in.loose[0]=30;in.loose_count=1;
        const auto o=c.update(0,in);c.note_drawn(0,in.root,o.drawn.position);return o;
    };
    // A wall at x = 1 (entity 7, facing -x) and the back of a prop at x = 0.8 (entity 30, loose, facing +x): the 20 cm between them is
    // narrower than the 25 cm hand, so no place keeps it out of both.
    const ClampHit wall{{1,0,.5},{-1,0,0},7,3,false,true},prop{{.8,0,.5},{1,0,0},30,6,false,true};
    frame(0,{.9,0,.5},std::nullopt,std::nullopt);
    frame(1,{.9,0,.5},std::nullopt,std::nullopt);
    ClampOutput o{};
    for(unsigned i=2;i<6;++i) o=frame(i,{.9,0,.5},i==2?std::optional<ClampHit>{wall}:std::nullopt,i>=3?std::optional<ClampHit>{prop}:std::nullopt);   // a loose face is hit every frame
    std::array<ClampHit,HandClamp::max_held_planes> held{};
    CHECK(c.held_planes(0,held)==2 && held[0].entity==7 && held[1].entity==30);   // the wall first, the prop's face after it
    CHECK(o.drawn.position.x<=1-.12-.03-.005+1e-9);   // the front of the hand is out of the wall; the prop's face is the one that gives
    CHECK(close_to(c.keep_out(0,basis,{.9,0,.5},std::nullopt),{1-.12-.03-.005,0,.5},1e-9));
}

// Task 6b, cause C: a full plane list drops a loose prop's oldest face, not the wall. A bottle's faces were hit again every frame and
// the wall, hit rarely (behind the bottle), was the oldest: the wall went and the hand followed the bottle through it.
void full_planes_keep_the_wall() {
    HandClamp c;
    const auto face=[](double angle) {return ClampHit{{.1,.1*angle,.5},{-std::cos(angle),std::sin(angle),0},30,6,false,true};};
    const auto frame=[&](unsigned i,std::optional<ClampHit> palm,std::optional<ClampHit> tip,std::optional<ClampHit> wall_tip) {
        ClampInput in{};
        in.now=i*dt;in.epoch=1;in.root=level_frame({0,0,0},{1,0,0});in.target=pose_at({.5,0,.5});in.chest={0,0,1};
        in.tip={{.10,0,0},.02};in.tip_enabled=true;in.wall_tip={{.12,0,.04},.03};
        in.palm_hit=palm;in.tip_hit=tip;in.wall_tip_hit=wall_tip;in.loose[0]=30;in.loose_count=1;
        const auto o=c.update(0,in);c.note_drawn(0,in.root,o.drawn.position);return o;
    };
    frame(0,std::nullopt,std::nullopt,std::nullopt);frame(1,std::nullopt,std::nullopt,std::nullopt);
    frame(2,ClampHit{{1,0,.5},{-1,0,0},7,3,false,true},std::nullopt,std::nullopt);   // the wall, hit once
    frame(3,face(.3),face(.5),face(.7));                                             // three faces of a loose prop: the list is full
    frame(4,face(.9),std::nullopt,std::nullopt);                                    // a fourth: one of the loose ones goes
    std::array<ClampHit,HandClamp::max_held_planes> held{};
    const auto n=c.held_planes(0,held);
    CHECK(n==c.config().max_planes);
    bool wall=false;
    for(std::size_t i=0;i<n;++i) wall=wall || held[i].entity==7;
    CHECK(wall);
}

// Task 6b, cause D: the wall tip and the exclusion probe sweep look_ahead past the target along the horizontal motion. The meet draws
// the hand out of a prop by up to 8 cm a frame after the sweeps were posted, and a plane that disagrees with another pushes it
// along its own normal: a wall 5 cm past the swept tip was met by no sweep until the drawn hand was in it.
void wall_tip_looks_ahead() {
    for(const double look:{.10,0.}) {
        HandClampConfig cfg;cfg.look_ahead=look;
        Rig r(cfg);r.tip={{.10,0,0},.02};r.tip_enabled=true;r.wall_tip={{.12,0,.04},.03};
        r.step({.5,0,.5});
        double extra=-1;bool wall=false;
        Vec3 kicked{};
        r.move_x({.5,0,.5},.80,.5,[&](const ClampOutput& o,Vec3 p) {
            if(!o.wall_tip_sweep.valid || !o.tip_sweep.valid || p.x<.7 || p.x>.795) return;   // while it moves: the last, shorter step reads no motion
            extra=(o.wall_tip_sweep.to.x-o.tip_sweep.to.x)-(.12-.10);   // the probes' own x offsets taken out: what is left is the look
            std::array<ClampHit,HandClamp::max_held_planes> held{};
            const auto n=r.clamp.held_planes(0,held);
            wall=false;for(std::size_t i=0;i<n;++i) wall=wall || held[i].entity==7;
            kicked=r.clamp.keep_out(0,pose_at(p).basis,p+Vec3{.08,0,0},std::nullopt);   // the meet's kick: 8 cm along the motion
        });
        CHECK(extra>=0 && std::abs(extra-look)<1e-9);
        // The tracked hand's wall tip is 5-6 cm short of the wall at x = 1; the kick puts it 2-3 cm into it.
        if(look>0) {CHECK(wall);CHECK(std::abs(kicked.x-(1-.12-.03-.005))<1e-6);}
        else {CHECK(!wall);CHECK(kicked.x>.87);}   // nothing knew of the wall: the kick stands
    }
}

// ---- Fix round 1 (Task 6b review): static geometry outranks every dynamic prop; projection converges; the look-ahead is always on.
// A crate too heavy to push (12 kg and up) is not in ClampInput::loose, but its hit says it is a dynamic body (a 9th prop in reach included).
// The hand is wider than the slot between the wall and the crate: it stays out of the wall and may enter the crate.
void static_outranks_heavy_props() {
    for(const std::uintptr_t crate:{std::uintptr_t{31},std::uintptr_t{32}}) {   // 12 kg and 20 kg: the clamp sees an entity, not a mass
        HandClamp c;
        const auto basis=pose_at({}).basis;
        ClampProbe front{{.12,0,0},.03},back{{-.10,0,0},.03};
        const auto frame=[&](unsigned i,Vec3 target,std::optional<ClampHit> palm,std::optional<ClampHit> tip) {
            ClampInput in{};
            in.now=i*dt;in.epoch=1;in.root=level_frame({0,0,0},{1,0,0});in.target=pose_at(target);in.chest={0,0,1};
            in.tip=back;in.tip_enabled=true;in.palm_hit=palm;in.tip_hit=tip;
            in.hand[0]=front;in.hand[1]=back;in.hand_count=2;
            for(std::uintptr_t e=101;e<109;++e) in.loose[in.loose_count++]=e;   // the lists are full of other props: the rank comes from the hit's own body
            const auto o=c.update(0,in);c.note_drawn(0,in.root,o.drawn.position);return o;
        };
        const ClampHit wall{{1,0,.5},{-1,0,0},7,3,false,true},face{{.8,0,.5},{1,0,0},crate,6,false,true,BodyClass::dynamic};
        frame(0,{.9,0,.5},std::nullopt,std::nullopt);
        frame(1,{.9,0,.5},std::nullopt,std::nullopt);
        ClampOutput o{};
        for(unsigned i=2;i<6;++i) o=frame(i,{.9,0,.5},i==2?std::optional<ClampHit>{wall}:std::nullopt,i>=3?std::optional<ClampHit>{face}:std::nullopt);
        CHECK(o.drawn.position.x<=1-.12-.03-.005+1e-9);   // out of the wall; the crate's face is the one that gives
        CHECK(close_to(c.keep_out(0,basis,{.9,0,.5},std::nullopt),{1-.12-.03-.005,0,.5},1e-9));
    }
}

// All four planes are props' (none loose): a new face drops the oldest face, not the wall.
void all_dynamic_eviction_keeps_the_wall() {
    HandClamp c;
    const auto face=[](double angle) {return ClampHit{{.1,.1*angle,.5},{-std::cos(angle),std::sin(angle),0},31,6,false,true,BodyClass::dynamic};};
    const auto frame=[&](unsigned i,std::optional<ClampHit> palm,std::optional<ClampHit> tip,std::optional<ClampHit> wall_tip) {
        ClampInput in{};
        in.now=i*dt;in.epoch=1;in.root=level_frame({0,0,0},{1,0,0});in.target=pose_at({.5,0,.5});in.chest={0,0,1};
        in.tip={{.10,0,0},.02};in.tip_enabled=true;in.wall_tip={{.12,0,.04},.03};
        in.palm_hit=palm;in.tip_hit=tip;in.wall_tip_hit=wall_tip;
        const auto o=c.update(0,in);c.note_drawn(0,in.root,o.drawn.position);return o;
    };
    frame(0,std::nullopt,std::nullopt,std::nullopt);frame(1,std::nullopt,std::nullopt,std::nullopt);
    frame(2,ClampHit{{1,0,.5},{-1,0,0},7,3,false,true},std::nullopt,std::nullopt);
    frame(3,face(.3),face(.5),face(.7));
    frame(4,face(.9),std::nullopt,std::nullopt);
    std::array<ClampHit,HandClamp::max_held_planes> held{};
    const auto n=c.held_planes(0,held);
    CHECK(n==c.config().max_planes);
    bool wall=false;
    for(std::size_t i=0;i<n;++i) wall=wall || held[i].entity==7;
    CHECK(wall);
}

// Hard inside corners at 30, 45 and 60 degrees (two static planes, the hand deep in both): within 1 cm of each plane, drawn and eased.
void sharp_hard_corners() {
    for(const double degrees:{30.,45.,60.}) {
        const double a=degrees*3.14159265358979323846/180;
        const Vec3 n1{-1,0,0},n2{std::cos(a),std::sin(a),0},free=unit(n1+n2);   // walls meet at (1,0): the free wedge opens along `free`
        HandClamp c;
        ClampProbe front{{.05,0,0},.02},back{{-.04,0,0},.02};   // a small hand: a 30 degree corner takes the big one past the 35 cm snap-back gap
        const auto frame=[&](unsigned i,Vec3 target,bool hits) {
            ClampInput in{};
            in.now=i*dt;in.epoch=1;in.root=level_frame({0,0,0},{1,0,0});in.target=pose_at(target);in.chest={0,0,1};
            in.tip=back;in.tip_enabled=true;
            in.hand[0]=front;in.hand[1]=back;in.hand_count=2;
            if(hits) {in.palm_hit=ClampHit{{1,0,.5},n1,7,3,false,true};in.tip_hit=ClampHit{{1,0,.5},n2,8,3,false,true};}
            const auto o=c.update(0,in);c.note_drawn(0,in.root,o.drawn.position);return o;
        };
        const Vec3 start=Vec3{1,0,.5}+free*.3,deep=Vec3{1,0,.5}+(degrees==30.?Vec3{-.07,0,0}:degrees==45.?Vec3{-.07,-.09,0}:Vec3{-.03,-.03,0});   // deep in both walls: three alternating projections stop short of the corner at 30 and 45 degrees
        frame(0,start,false);frame(1,start,false);
        ClampOutput o{};
        for(unsigned i=2;i<40;++i) {
            o=frame(i,deep,true);
            // The clearance of the drawn grip from each plane's skin: the hand's deepest extent along -n (the front and back spheres) beyond it.
            for(const Vec3 n:{n1,n2}) {
                const double reach=std::max({.05,dot(Vec3{.05,0,0},n*-1.)+.02,dot(Vec3{-.04,0,0},n*-1.)+.02});
                CHECK(finite(o.drawn.position));
                if(i>=3) CHECK(dot(o.drawn.position-Vec3{1,0,.5},n)-reach>=-.01);
            }
        }
    }
}

// The wall tip is the tip (same probe): its sweep looks ahead too, with the production default. A kick of 8 cm along the motion then lands on
// a wall the sweep already met.
void same_probe_wall_tip_looks_ahead() {
    CHECK(HandClampConfig{}.look_ahead==.10);   // the production default
    double end_x[2]{};Vec3 kicked[2]{};bool wall[2]{};
    for(int run=0;run<2;++run) {
        HandClampConfig cfg;if(run) cfg.look_ahead=0;
        Rig r(cfg);r.tip={{.12,0,0},.03};r.tip_enabled=true;r.wall_tip=r.tip;
        r.step({.5,0,.5});
        r.move_x({.5,0,.5},.80,.5,[&](const ClampOutput& o,Vec3 p) {
            if(!o.tip_sweep.valid || p.x<.7 || p.x>.795) return;
            CHECK(!o.wall_tip_sweep.valid);   // one probe, one sweep
            end_x[run]=o.tip_sweep.to.x-p.x;
            std::array<ClampHit,HandClamp::max_held_planes> held{};
            const auto n=r.clamp.held_planes(0,held);
            wall[run]=false;for(std::size_t i=0;i<n;++i) wall[run]=wall[run] || held[i].entity==7;
            kicked[run]=r.clamp.keep_out(0,pose_at(p).basis,p+Vec3{.08,0,0},std::nullopt);
        });
    }
    CHECK(std::abs((end_x[0]-end_x[1])-.10)<1e-9);
    CHECK(wall[0] && !wall[1]);
    CHECK(std::abs(kicked[0].x-(1-.12-.03-.005))<1e-6);
    CHECK(kicked[1].x>.855);   // nothing knew of the wall: the kick stands (p + 8 cm, p over 0.7)
}

// The hand at rest: ClampInput::kick (the meet's way out, from the contact service) says which way the wall tip looks.
void late_meet_kick_at_rest() {
    for(const bool kick:{true,false}) {
        Rig r;r.tip={{.12,0,0},.03};r.tip_enabled=true;r.wall_tip=r.tip;
        if(kick) r.kick={.08,0,0};
        r.step({.8,0,.5});
        ClampOutput o{};
        for(int i=0;i<12;++i) o=r.step({.8,0,.5});
        while(!r.posting()) o=r.step({.8,0,.5});
        std::array<ClampHit,HandClamp::max_held_planes> held{};
        const auto n=r.clamp.held_planes(0,held);
        bool wall=false;for(std::size_t i=0;i<n;++i) wall=wall || held[i].entity==7;
        const Vec3 d=r.clamp.keep_out(0,pose_at({.8,0,.5}).basis,{.88,0,.5},std::nullopt);   // the meet draws the hand 8 cm on
        if(kick) {CHECK(wall);CHECK(std::abs(d.x-(1-.12-.03-.005))<1e-6);}
        else {CHECK(!wall);CHECK(std::abs(d.x-.88)<1e-12);}
    }
}

// ---- Fix round 2: planes are ranked by the hit's own body; an infeasible squeeze retreats the hand toward the chest.
// A slot of two static walls that narrows along +x: a flat upper wall (y <= -0.085 for the grip) and a lower wall rising with x (y >= -0.5 + 0.4 x
// + 0.2). The hand (0.37 m across) fits where x <= 0.57. The controller is at x = 0.8 and the chest at x = 0, both at y = -0.2: the hand must stop at
// the slot's mouth, outside both walls, not be drawn inside them.
namespace {
struct Funnel {
    HandClamp c;ClampProbe front{{0,.15,0},.03},back{{0,-.15,0},.03};
    Vec3 n1{0,-1,0},n2=unit(Vec3{-.4,1,0});
    ClampHit h1{{0,.1,.5},n1,7,3,false,true},h2{{0,-.5,.5},n2,8,3,false,true,BodyClass::entity_static};
    ClampOutput frame(unsigned i,Vec3 target,bool upper,bool lower) {
        ClampInput in{};
        in.now=i*dt;in.epoch=1;in.root=level_frame({0,0,0},{1,0,0});in.target=pose_at(target);in.chest={0,-.2,.5};
        in.tip=back;in.tip_enabled=true;in.hand[0]=front;in.hand[1]=back;in.hand_count=2;
        if(upper) in.palm_hit=h1;
        if(lower) in.tip_hit=h2;
        const auto o=c.update(0,in);c.note_drawn(0,in.root,o.drawn.position);return o;
    }
    // How far inside a wall the hand's deepest sphere is at grip p (negative: outside), the larger of the two walls.
    double inside(Vec3 p) const {
        double worst=-1e9;
        for(const auto& h:{h1,h2}) {
            const double reach=.15*std::abs(h.normal.y)+.03;   // the deepest sphere along -n
            worst=std::max(worst,-(dot(p-h.point,h.normal)-reach));
        }
        return worst;
    }
};
}

void squeeze_entry_stops_at_the_mouth() {
    Funnel f;ClampOutput o{};
    f.frame(0,{.8,-.2,.5},false,false);f.frame(1,{.8,-.2,.5},false,false);
    for(unsigned i=2;i<30;++i) {
        o=f.frame(i,{.8,-.2,.5},true,true);
        CHECK(finite(o.drawn.position));
        if(i>=5) {CHECK(!o.snapped);CHECK(f.inside(o.drawn.position)<=.001);CHECK(o.drawn.position.x<.8 && o.drawn.position.x>.3);}
    }
    CHECK(f.c.squeeze_infeasible(0)==0);   // a place outside both exists: no minimax
}

// One wall holds the hand pressed; a second static plane appears that no place near the controller satisfies with the first: the hand
// leaves the mouth, it is not held inside the new plane.
void new_static_plane_during_a_hold() {
    Funnel f;ClampOutput o{};
    f.frame(0,{.8,-.2,.5},false,false);f.frame(1,{.8,-.2,.5},false,false);
    for(unsigned i=2;i<8;++i) o=f.frame(i,{.8,-.2,.5},true,false);   // the upper wall alone: the hand is drawn at the controller
    for(unsigned i=8;i<30;++i) {
        o=f.frame(i,{.8,-.2,.5},true,true);
        CHECK(finite(o.drawn.position));
        if(i>=12) {CHECK(!o.snapped);CHECK(f.inside(o.drawn.position)<=.001);}
    }
}

// keep_out in the same slot: the meet's position is pushed to where both walls are satisfied.
void infeasible_keep_out_retreats() {
    Funnel f;ClampOutput o{};
    f.frame(0,{.8,-.2,.5},false,false);f.frame(1,{.8,-.2,.5},false,false);
    for(unsigned i=2;i<8;++i) o=f.frame(i,{.8,-.2,.5},true,true);
    const auto basis=pose_at({}).basis;
    const Vec3 kept=f.c.keep_out(0,basis,{.8,-.2,.5},std::nullopt);
    CHECK(f.inside(kept)<=.001);
    CHECK(kept.x<.8 && kept.x>.3);
}

// Two parallel static walls closer than the hand is wide: no place satisfies both, not even the chest's. The hand goes to the place that
// violates them least along the way to the chest and the counter says so. Nothing here asserts that the hand is inside either wall: the
// check is that no place on the controller-to-chest segment is better.
void parallel_squeeze_takes_the_least_violation() {
    HandClamp c;
    ClampProbe front{{0,.15,0},.03},back{{0,-.15,0},.03};
    const ClampHit up{{.5,.1,.5},{0,-1,0},7,3,false,true},down{{.5,-.1,.5},{0,1,0},8,3,false,true};
    const auto violation=[&](Vec3 p) {return std::max(.18-(dot(p-up.point,up.normal)),.18-(dot(p-down.point,down.normal)));};
    ClampOutput o{};
    for(unsigned i=0;i<10;++i) {
        ClampInput in{};
        in.now=i*dt;in.epoch=1;in.root=level_frame({0,0,0},{1,0,0});in.target=pose_at({.5+.01*i,.04,.5});in.chest={0,0,.5};
        in.tip=back;in.tip_enabled=true;in.hand[0]=front;in.hand[1]=back;in.hand_count=2;
        if(i>=2) {in.palm_hit=up;in.tip_hit=down;}
        o=c.update(0,in);c.note_drawn(0,in.root,o.drawn.position);
        CHECK(finite(o.drawn.position));
    }
    double best=1e9;
    for(int k=0;k<=400;++k) best=std::min(best,violation(Vec3{.5+.01*9,.04,.5}+(Vec3{0,0,.5}-Vec3{.5+.01*9,.04,.5})*(k/400.)));
    CHECK(violation(o.drawn.position)<=best+.002);
    CHECK(c.squeeze_infeasible(0)>0);
}

// The tiers: the world is hard, an entity that does not move is met as far as the world allows. A wall (world) and a door's face (entity_static)
// 0.205 m apart, the hand 0.285 m deep: the hand stays out of the wall and enters the door, in the drawn hand and in keep_out; nothing counts
// as infeasible (the world alone is satisfiable).
void wall_and_door_squeeze_stays_out_of_the_wall() {
    HandClamp c;
    ClampProbe front{{.12,0,0},.03},back{{-.10,0,0},.03};
    const ClampHit wall{{1,0,.5},{-1,0,0},7,3,false,true},door{{.795,0,.5},{1,0,0},9,3,false,true,BodyClass::entity_static};
    ClampOutput o{};
    for(unsigned i=0;i<10;++i) {
        ClampInput in{};
        in.now=i*dt;in.epoch=1;in.root=level_frame({0,0,0},{1,0,0});in.target=pose_at({1,0,.5});in.chest={0,0,.5};
        in.tip=back;in.tip_enabled=true;in.hand[0]=front;in.hand[1]=back;in.hand_count=2;
        if(i>=2) {in.palm_hit=wall;in.tip_hit=door;}
        o=c.update(0,in);c.note_drawn(0,in.root,o.drawn.position);
        if(i>=3) CHECK(o.drawn.position.x<=1-.12-.03-.005+1e-3);   // the front of the hand is out of the wall, from the first frame both are held
        if(i>=3) CHECK(o.contact && o.contact->entity==7);   // the wall holds the hand; the door the hand is in is not a contact
    }
    const Vec3 kept=c.keep_out(0,pose_at({}).basis,{1,0,.5},std::nullopt);
    CHECK(kept.x<=1-.12-.03-.005+1e-3);
    CHECK(c.squeeze_infeasible(0)==0);
}

// Four world planes at their last frames of life and a live door hit (entity_static) on one of them: the door is held, and the hand is never
// left unheld. (Which frame the first plane passes plane_seconds depends on the accumulated frame time: the door arrives on each of the three.)
void expired_planes_never_block_a_live_hit() {
    for(unsigned late=9;late<=11;++late) {
        HandClamp c;
        const auto wall=[](double angle,std::uintptr_t entity) {return ClampHit{{1,.1*angle,.5},{-std::cos(angle),std::sin(angle),0},entity,3,false,true};};
        const ClampHit door{{.9,0,.5},{-1,0,0},9,3,false,true,BodyClass::entity_static};
        const auto frame=[&](unsigned i,std::optional<ClampHit> palm,std::optional<ClampHit> tip,std::optional<ClampHit> wall_tip) {
            ClampInput in{};
            in.now=i*dt;in.epoch=1;in.root=level_frame({0,0,0},{1,0,0});in.target=pose_at({.2,0,.5});in.chest={0,0,1};
            in.tip={{.10,0,0},.02};in.tip_enabled=true;in.wall_tip={{.12,0,.04},.03};
            in.palm_hit=palm;in.tip_hit=tip;in.wall_tip_hit=wall_tip;
            const auto o=c.update(0,in);c.note_drawn(0,in.root,o.drawn.position);return o;
        };
        std::array<ClampHit,HandClamp::max_held_planes> held{};
        frame(0,std::nullopt,std::nullopt,std::nullopt);frame(1,std::nullopt,std::nullopt,std::nullopt);
        frame(2,wall(.3,41),wall(.5,42),wall(.7,43));
        frame(3,wall(.9,44),std::nullopt,std::nullopt);
        for(unsigned i=4;i<2+late;++i) frame(i,std::nullopt,std::nullopt,std::nullopt);
        frame(2+late,door,std::nullopt,std::nullopt);
        const auto n=c.held_planes(0,held);
        bool has_door=false;for(std::size_t i=0;i<n;++i) has_door=has_door || held[i].entity==9;
        CHECK(has_door);
    }
}

// A full list of planes that all outrank a prop's new face keeps them: the face is dropped.
void world_planes_outrank_a_new_prop_face() {
    HandClamp c;
    const auto wall=[](double angle,std::uintptr_t entity) {return ClampHit{{1,.1*angle,.5},{-std::cos(angle),std::sin(angle),0},entity,3,false,true};};
    const auto frame=[&](unsigned i,std::optional<ClampHit> palm,std::optional<ClampHit> tip,std::optional<ClampHit> wall_tip) {
        ClampInput in{};
        in.now=i*dt;in.epoch=1;in.root=level_frame({0,0,0},{1,0,0});in.target=pose_at({.5,0,.5});in.chest={0,0,1};
        in.tip={{.10,0,0},.02};in.tip_enabled=true;in.wall_tip={{.12,0,.04},.03};
        in.palm_hit=palm;in.tip_hit=tip;in.wall_tip_hit=wall_tip;
        const auto o=c.update(0,in);c.note_drawn(0,in.root,o.drawn.position);return o;
    };
    frame(0,std::nullopt,std::nullopt,std::nullopt);frame(1,std::nullopt,std::nullopt,std::nullopt);
    frame(2,wall(.3,41),wall(.5,42),wall(.7,43));
    frame(3,wall(.9,44),std::nullopt,std::nullopt);      // the fourth world plane: the list is full of them
    ClampHit crate{{.2,0,.5},{1,0,0},31,6,false,true,BodyClass::dynamic};
    frame(4,crate,std::nullopt,std::nullopt);
    std::array<ClampHit,HandClamp::max_held_planes> held{};
    const auto n=c.held_planes(0,held);
    CHECK(n==c.config().max_planes);
    for(std::size_t i=0;i<n;++i) CHECK(held[i].entity!=31);
}

// The tiers' fallback measures its push from where the solve started: a hand 0.245 m outside the wall's hold, in the door's half-space only, is not
// in contact with the wall (the projection passes had pushed it there before the fallback dropped the door).
void wall_and_door_fallback_reports_no_false_contact() {
    HandClamp c;
    ClampProbe front{{.12,0,0},.03},back{{-.10,0,0},.03};
    const ClampHit wall{{1,0,.5},{-1,0,0},7,3,false,true},door{{.795,0,.5},{1,0,0},9,3,false,true,BodyClass::entity_static};
    for(unsigned i=0;i<12;++i) {
        ClampInput in{};
        in.now=i*dt;in.epoch=1;in.root=level_frame({0,0,0},{1,0,0});in.target=pose_at({.6,0,.5});in.chest={0,0,.5};
        in.tip=back;in.tip_enabled=true;in.hand[0]=front;in.hand[1]=back;in.hand_count=2;
        if(i>=2) {in.palm_hit=wall;in.tip_hit=door;}
        const auto o=c.update(0,in);c.note_drawn(0,in.root,o.drawn.position);
        if(i>=3) {CHECK(!o.contact && !o.event && !o.haptic);CHECK(length(o.shift)<1e-9);CHECK(std::abs(o.drawn.position.x-.6)<1e-9);}
    }
}

// A plane the drawn hand rests at (touched) is not stale-evicted at the age boundary: it would have been rescued this update, and a prop's new face
// on a full list took it instead (the hand then went 8.5 cm past the wall).
void touched_plane_survives_the_age_boundary() {
    // With 10.5 ms frames the wall's age is 94.5 ms after frame 11 and 105 ms before frame 12's purge: the hand reaches the wall (clearance under 1 cm)
    // on frame 11, untouched until then, and a prop's fourth face arrives on frame 12.
    for(unsigned late=12;late<=12;++late) {
        const double h=.0105;
        HandClamp c;
        ClampProbe front{{.12,0,0},.03},back{{-.10,0,0},.03};
        const ClampHit wall{{1,0,.5},{-1,0,0},7,3,false,true};
        const auto face=[](double a) {return ClampHit{{.84,3,.5},{-std::sin(a),-std::cos(a),0},31,6,false,true,BodyClass::dynamic};};
        for(unsigned i=0;i<late+3;++i) {
            ClampInput in{};
            const double x=i<3?.5:i<late-1?.5+.3*(i-2)/(late-4.):.84;
            in.now=i*h;in.epoch=1;in.root=level_frame({0,0,0},{1,0,0});in.target=pose_at({x,0,.5});in.chest={0,0,.5};
            in.tip=back;in.tip_enabled=true;in.hand[0]=front;in.hand[1]=back;in.hand_count=2;
            if(i==2) in.palm_hit=wall;                                       // the wall, hit once; the rest of the list is a prop's faces, re-hit in turn
            else if(i>2 && i<late) in.palm_hit=face(.1+.2*(i%3));
            else if(i==late) in.palm_hit=face(.9);                            // a fourth face on a full list
            const auto o=c.update(0,in);c.note_drawn(0,in.root,o.drawn.position);
            if(i>=late) {
                std::array<ClampHit,HandClamp::max_held_planes> held{};
                const auto n=c.held_planes(0,held);
                bool has_wall=false;for(std::size_t k=0;k<n;++k) has_wall=has_wall || held[k].entity==7;
                CHECK(has_wall);
                CHECK(o.drawn.position.x+.12+.03<=1+.01);
            }
        }
    }
}

// The touched test of the stale slot honours this update's own judge: a touched, stale world wall that a sweep passes through (a plate N is hit
// beyond it) is refuted, so the rescue will not keep it, and it is the plane that gives its slot to N. A valid plane V of the same rank as N is
// not evicted for the frame, and the hand does not clip it.
void refuted_touched_plane_gives_its_slot() {
    const double h=.0105;   // 10.5 ms frames: the wall's age is 94.5 ms after frame 11 and 105 ms before frame 12's purge
    HandClamp c;
    ClampProbe front{{.12,0,0},.03},back{{-.10,0,0},.03};
    const ClampHit wall{{1,0,.5},{-1,0,0},7,3,false,true};
    const ClampHit plates[3]{{{.3,0,.5},{1,0,0},51,3,false,true,BodyClass::entity_static},     // V1: the hand must stay in front of it (x >= 0.435)
                             {{0,3,.5},{0,-1,0},52,3,false,true,BodyClass::entity_static},     // V2, V3: far off
                             {{0,0,3},{0,0,-1},53,3,false,true,BodyClass::entity_static}};
    const ClampHit plate{{1.2,0,.5},{-1,0,0},54,3,false,true,BodyClass::entity_static};      // N: beyond the wall along the sweep
    for(unsigned i=0;i<15;++i) {
        ClampInput in{};
        const double x=i<3?.5:i<11?.5+.3*(i-2)/8:1.3;   // approaching the wall, untouched until frame 11; then the controller goes on through it
        in.now=i*h;in.epoch=1;in.root=level_frame({0,0,0},{1,0,0});in.target=pose_at({x,0,.5});in.chest={0,0,.5};
        in.tip=back;in.tip_enabled=true;in.hand[0]=front;in.hand[1]=back;in.hand_count=2;
        if(i==2) in.palm_hit=wall;
        else if(i>2 && i<12) in.palm_hit=plates[i%3];
        else if(i==12) in.palm_hit=plate;
        const auto o=c.update(0,in);c.note_drawn(0,in.root,o.drawn.position);
        if(i>=12) {
            std::array<ClampHit,HandClamp::max_held_planes> held{};
            const auto n=c.held_planes(0,held);
            bool has[3]{};for(std::size_t k=0;k<n;++k) for(int v=0;v<3;++v) has[v]=has[v] || held[k].entity==static_cast<std::uintptr_t>(51+v);
            CHECK(has[0] && has[1] && has[2]);
            CHECK(o.drawn.position.x>=.3+.10+.03+.005-1e-3);                                    // out of V1's half-space
            CHECK(o.drawn.position.x+.12+.03<=1.2+.01);                                         // and out of N (the refuted wall is gone)
        }
    }
}

int main() {
    strict_receipts_bind_geometry_and_generation();
    strict_auxiliary_receipts_and_chest_rearm();
    strict_async_game_cadence_covers_batched_root();
    strict_small_steps_cannot_self_renew_proof();
    // Corner, slide, hold, jump and walk tests run in every posting mode, with the same bounds.
    for(g_mode=0;g_mode<3;++g_mode) {
        sweep_starts_at_previous_drawn();
        corner_two_planes();
        static_corner_press();
        grazing_slides();
        fast_approach_late_results();
        walk_and_turn_carry();
        variable_cadence_locomotion_carries_retained_hand();
        moving_root_retains_controller_jump();
        root_acceleration_does_not_cross_unseen_wall();
        batched_root_publications_do_not_stutter();
        strict_root_turn_and_discontinuity_need_world_proof();
        moving_root_hold_keeps_world_wall();
        walk_into_wall_5ms();
        corner_slide_3ms();
        fast_wall_slide_no_stutter();
        slide_past_wall_end();
        hold_lasts_two_frames();
        snap_turn_holds_out_of_wall();
        hitch_holds_out_of_wall();
        door_opens_on_corner();
        crate_removed_while_sliding();
        release_eases_out();
        eased_hand_never_enters_plane();
        tip_probe_stops_fingers();
        wall_tip_holds_a_rising_hand();
        whole_hand_edges_and_corners();
        static_outranks_heavy_props();
        all_dynamic_eviction_keeps_the_wall();
        world_planes_outrank_a_new_prop_face();
        sharp_hard_corners();
        same_probe_wall_tip_looks_ahead();
        late_meet_kick_at_rest();
        squeeze_entry_stops_at_the_mouth();
        new_static_plane_during_a_hold();
        infeasible_keep_out_retreats();
        wall_and_door_squeeze_stays_out_of_the_wall();
        expired_planes_never_block_a_live_hit();
        wall_and_door_fallback_reports_no_false_contact();
        touched_plane_survives_the_age_boundary();
        refuted_touched_plane_gives_its_slot();
        parallel_squeeze_takes_the_least_violation();
    }
    g_mode=0;
    deep_start_overlap_snaps();
    snap_after_gap_time();
    no_snap_short_gap();
    rearm_after_clear();
    non_finite_snaps();
    alternate_mode_predicts_two_frames();
    invalid_normal_makes_no_plane();
    shadow_refresh_is_capped();
    haptic_cooldown();
    contract_counters();
    chest_non_finite_stays_released();
    epoch_change_resets();
    tracking_gap_reacquires();
    reacquire_from_inside_geometry_no_flicker();
    intermittent_hits_no_flicker();
    stale_plane_expires();
    leading_probe_picks_front();
    budget_drops_tip_then_alternates();
    yield_passes_through();
    event_on_new_contact();
    level_frame_and_hit_conversion();
    meet_never_passes_held_plane();
    meet_start_and_clip();
    exclusion_sweep_holds_wall_behind_prop();
    loose_overlap_never_snaps();
    loose_prop_never_keeps_released();
    contact_fresh_tracks_answers();
    moving_prop_planes_follow_the_prop();
    note_drawn_carries_the_ease();
    thin_prop_palm_stays_out_of_table();
    leading_probes_follow_motion_and_walls();
    wall_tip_sweeps_past_the_pressed_prop();
    corpse_pushed_against_a_wall_holds_the_wall();
    snap_timer_ignores_caller_additions();
    whole_hand_slides_along_a_wall();
    walls_win_over_loose_faces();
    full_planes_keep_the_wall();
    wall_tip_looks_ahead();
    std::puts("PASS");
}
