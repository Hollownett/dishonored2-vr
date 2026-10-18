// Contract breaks (an update without note_drawn, a repeated update) are counted here instead of asserting.
static int contract_breaks=0;
#define D2VR_HAND_CLAMP_CONTRACT(ok) do{if(!(ok))++contract_breaks;}while(0)
#include "hand_clamp.hpp"
#include "native_world_trace.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
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
    Vec3 chest{0,0,1};
    std::optional<ClampHit> cast(const ClampSweep& s) const {
        if(!s.valid) return std::nullopt;
        const bool from_chest=close_to(s.from,chest,1e-9);
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
    std::function<bool(unsigned)> drop_palm;   // true: lose this frame's palm result
    std::function<bool(unsigned)> post;        // C14 alternate mode: false skips this frame's cast
    std::optional<Vec3> note_override;         // drawn position reported to note_drawn once
    ClampOutput last{};bool have_last=false;int holds=0;   // frames held so far
    explicit Rig(HandClampConfig c={}) : clamp(c) {
        if(g_mode) post=[phase=static_cast<unsigned>(g_mode-1)](unsigned f) {return (f&1u)==phase;};
    }
    bool posting() const {return !post || post(frame-1);}   // whether the last step cast
    double lead() const {return g_mode?2.:1.;}              // frames the last step's sweep predicts
    double now() const {return t0+frame*dt;}
    ClampOutput step_pose(const WorldTrackedPose& target,bool yield=false) {
        ClampInput in{};
        in.now=now();in.epoch=epoch;in.root=root;in.target=target;in.chest=world.chest;
        in.tip=tip;in.tip_enabled=tip_enabled;in.yield=yield;in.post=!post || post(frame);
        if(have_last) {
            const auto slot0=world.cast(last.palm_sweep);
            if(last.state==ClampState::released) in.chest_hit=slot0;
            else if(!(drop_palm && drop_palm(frame))) in.palm_hit=slot0;
            in.tip_hit=world.cast(last.tip_sweep);
        }
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
    CHECK(close_to(r.clamp.keep_out(1,target.basis,{2,0,.5},std::nullopt),{2,0,.5},0));   // the other side holds nothing
    CHECK(close_to(r.clamp.keep_out(5,target.basis,{2,0,.5},std::nullopt),{2,0,.5},0));
    r.clamp.reset(0);
    CHECK(close_to(r.clamp.keep_out(0,target.basis,{2,0,.5},std::nullopt),{2,0,.5},0));   // reset: nothing held
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

int main() {
    // Corner, slide, hold, jump and walk tests run in every posting mode, with the same bounds.
    for(g_mode=0;g_mode<3;++g_mode) {
        sweep_starts_at_previous_drawn();
        corner_two_planes();
        static_corner_press();
        grazing_slides();
        fast_approach_late_results();
        walk_and_turn_carry();
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
    std::puts("PASS");
}
