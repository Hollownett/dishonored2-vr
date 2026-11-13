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
// M1: a 4.5 m/s sword stab 20 cm into a wall, in the publication's order (clamp -> blade from the drawn grip -> compose_blade ->
// note_drawn). The blade's answers are one frame late (the previous segment's hit), as weapon_hit gives them. Judged: once the
// blade touches, the tip never recoils more than 5 mm off the surface and settles on it. v1289 (contact_step, the default): the tip rests
// blade_skin (1 cm) off the surface for a push too (legacy_step's push rested it on the surface), reaches that without overshoot and never
// recoils past it.
void blade_stab(bool legacy) {
    HandClamp clamp;WeaponCollision blade_policy;blade_policy.set_legacy(legacy);ClampOutput out{};double now=0;bool started=false;
    const auto root=level_frame({0,0,0},{1,0,0});const double blade=.65;
    std::optional<CollisionHit> last_hit;   // what the previous frame's blade segment met
    double worst_recoil=0,last_tip=0;bool touched=false;
    for(int f=0;f<90;++f) {
        const double x=std::min(.55,.2+.05*f);const Vec3 target{x,0,1};
        ClampInput in{};in.now=now;in.epoch=1;in.root=root;in.target=pose_at(target);in.chest={0,0,1};in.tip=default_clamp_tip();in.wall_tip=default_clamp_wall_tip();
        if(started) route_clamp_hits(in,out.state,wall_cast(out.palm_sweep),wall_cast(out.tip_sweep),wall_cast(out.wall_tip_sweep),std::nullopt);
        out=clamp.update(0,in);started=true;
        const Vec3 hand=out.drawn.position;
        const auto query=[&](Vec3 a,Vec3 b)->std::optional<CollisionHit> {
            const auto answer=last_hit;last_hit.reset();
            if(b.x>1) {const double t=(1-a.x)/(b.x-a.x);last_hit=CollisionHit{a+(b-a)*t,{-1,0,0},t,7};}
            return answer;
        };
        const auto r=blade_policy.update(0,dt,hand,{1,0,0},blade,query);
        const auto composed=compose_blade(clamp,0,in.target.basis,hand,r,std::nullopt);
        clamp.note_drawn(0,root,composed.noted);
        last_tip=composed.drawn.x+blade;
        if(r.contact) touched=true;
        if(touched) worst_recoil=std::max(worst_recoil,1-last_tip);
        now+=dt;
    }
    const double rest=legacy?0.:CollisionConfig{}.blade_skin;
    CHECK(touched && worst_recoil<=rest+.005 && std::abs(last_tip-(1-rest))<1e-6);
    CHECK(clamp.missed_notes(0)==0 && out.state==ClampState::free);
    std::printf("blade_stab (%s): worst recoil %.4f m, settled tip %.6f\n",legacy?"legacy":"v1289",worst_recoil,last_tip);
}

// A blade push never moves the hand through a plane the clamp holds: the hand pressed into the wall at x=1, the reversed blade's
// tip 10.5 cm into a surface behind (x < 0.4) pushes it straight back toward the wall.
void blade_push_stays_out() {
    HandClamp clamp;ClampOutput out{};double now=0;bool started=false;const auto root=level_frame({0,0,0},{1,0,0});
    for(int f=0;f<60;++f) {
        ClampInput in{};in.now=now;in.epoch=1;in.root=root;in.target=pose_at({1.05,0,1});in.chest={0,0,1};in.tip_enabled=false;
        if(started) route_clamp_hits(in,out.state,wall_cast(out.palm_sweep),std::nullopt,std::nullopt,std::nullopt);
        out=clamp.update(0,in);started=true;clamp.note_drawn(0,root,out.drawn.position);now+=dt;
    }
    CHECK(std::abs(out.drawn.position.x-.945)<1e-6);
    WeaponCollision blade_policy;blade_policy.set_legacy(true);   // legacy_step pushes all 10.5 cm in one frame (v1289 glides there); compose_blade is the same
    const auto r=blade_policy.update(0,dt,out.drawn.position,{-1,0,0},.65,[](Vec3,Vec3)->std::optional<CollisionHit> {return CollisionHit{{.4,0,1},{1,0,0},.5,9};});
    CHECK(r.contact && r.shift.x>.1);                               // the blade alone would push the hand 10 cm into the wall
    const auto composed=compose_blade(clamp,0,pose_at({}).basis,out.drawn.position,r,std::nullopt);
    CHECK(composed.drawn.x<=.945+1e-9 && composed.noted.x==out.drawn.position.x);
    // No blade push: the hand is drawn as given (a met prop's hand is never projected here).
    WeaponCollision none;const auto idle=none.update(0,dt,{.98,0,1},{1,0,0},0,[](Vec3,Vec3)->std::optional<CollisionHit> {return std::nullopt;});
    const auto kept=compose_blade(clamp,0,pose_at({}).basis,{.98,0,1},idle,std::nullopt);
    CHECK(kept.drawn.x==.98 && kept.noted.x==.98);
}

// I2: a pistol pressed through a thin wall (x 1..1.1): the drawn hand rests at its near face, the controller is 20 cm in, the
// controller's aim origin past the far face. Shots leave from the drawn barrel: on the near side.
void shot_origin_thin_wall() {
    HandClamp clamp;ClampOutput out{};double now=0;bool started=false;const auto root=level_frame({0,0,0},{1,0,0});
    const Vec3 controller{1.2,0,1};
    for(int f=0;f<60;++f) {
        ClampInput in{};in.now=now;in.epoch=1;in.root=root;in.target=pose_at(f<10?Vec3{.5+.07*f,0,1}:controller);in.chest={0,0,1};in.tip_enabled=false;
        if(started) route_clamp_hits(in,out.state,wall_cast(out.palm_sweep),std::nullopt,std::nullopt,std::nullopt);
        out=clamp.update(0,in);started=true;clamp.note_drawn(0,root,out.drawn.position);now+=dt;
    }
    CHECK(out.state==ClampState::contact && out.drawn.position.x<1 && clamp.snapbacks(0)==0);
    const Vec3 raw_aim=controller+Vec3{.02,0,.01};                 // the aim pose sits a little ahead of the grip
    CHECK(raw_aim.x>1.1);                                           // the raw origin is behind the wall
    const auto offset=clamped_shot_offset(out.drawn.position,controller,true,1000,7);
    const Vec3 origin=clamped_shot_origin(raw_aim,offset,1016,7);
    CHECK(offset.valid && origin.x<1 && origin.x>.9 && std::abs(origin.z-1.01)<1e-12);   // near side, at the drawn barrel
    // Not clamped, stale, another epoch, or no offset: the controller's origin.
    CHECK(!clamped_shot_offset(out.drawn.position,controller,false,1000,7).valid);
    CHECK(!clamped_shot_offset(controller,controller,true,1000,7).valid);
    CHECK(clamped_shot_origin(raw_aim,offset,1101,7).x==raw_aim.x && clamped_shot_origin(raw_aim,offset,999,7).x==raw_aim.x);
    CHECK(clamped_shot_origin(raw_aim,offset,1016,8).x==raw_aim.x && clamped_shot_origin(raw_aim,ClampedShotOffset{},1016,7).x==raw_aim.x);
    CHECK(!clamped_shot_offset({std::nan(""),0,0},controller,true,1000,7).valid);
}

// M5: without hand geometry the wall tip is a separate default sphere and is swept.
void default_wall_tip() {
    CHECK(!clamp_detail::same_probe(default_clamp_tip(),default_clamp_wall_tip()) && clamp_detail::usable(default_clamp_wall_tip()));
    HandClamp clamp;ClampOutput out{};const auto root=level_frame({0,0,0},{1,0,0});
    for(int f=0;f<3;++f) {
        ClampInput in{};in.now=f*dt;in.epoch=1;in.root=root;in.target=pose_at({.3+.01*f,0,1});in.chest={0,0,1};
        in.tip=default_clamp_tip();in.wall_tip=default_clamp_wall_tip();
        if(f) route_clamp_hits(in,out.state,wall_cast(out.palm_sweep),wall_cast(out.tip_sweep),wall_cast(out.wall_tip_sweep),std::nullopt);
        out=clamp.update(0,in);clamp.note_drawn(0,root,out.drawn.position);
    }
    CHECK(out.wall_tip_sweep.valid && std::abs(out.wall_tip_sweep.radius-.035)<1e-12 && out.tip_sweep.valid);
}
// A 10 cm door, solid for 1 <= x <= 1.1 (near face normal -x), answered like a closest-hit sphere cast from the near side.
std::optional<ClampHit> door_cast(const ClampSweep& s,double thickness=.1) {
    if(!s.valid) return std::nullopt;
    if(s.from.x+s.radius>=1 && s.from.x-s.radius<=1+thickness) return ClampHit{{1,s.from.y,s.from.z},{-1,0,0},5,3,true};
    if(s.from.x<1 && s.to.x+s.radius>1) {
        const double t=(1-s.radius-s.from.x)/(s.to.x-s.from.x);const Vec3 c=s.from+(s.to-s.from)*std::max(0.,t);
        return ClampHit{{1,c.y,c.z},{-1,0,0},5,3,false};
    }
    return std::nullopt;
}

// Ruling 1 (snap-back exploit): the gun pushed 40 cm through a 10 cm door and held 1 s. The side snaps back (G3) and is drawn at
// the controller, past the door; every shot origin of the second stays on the near side. The publication's order: last frame's
// answers -> the released guard reads them -> clamp update -> the guard's eye sweep on the tip slot -> note_drawn -> shot origin.
void released_shot_through_door() {
    HandClamp clamp;ReleasedShotGuard guard;ClampOutput out{};const auto root=level_frame({0,0,0},{1,0,0});
    const Vec3 chest{0,0,1},eye{0,0,1.6};double now=0;std::uint64_t tick=1000;bool started=false;
    std::optional<ClampSweep> tip_slot_posted;   // what the exclusion slot carried last frame (the guard's eye sweep)
    unsigned released_frames=0,guarded=0;double farthest=-1e9;
    for(int f=0;f<100;++f) {
        const Vec3 controller{f<10?.5+.09*f:1.4,0,1};
        const auto slot0=started?door_cast(out.palm_sweep):std::nullopt;
        const auto tip_answer=tip_slot_posted?door_cast(*tip_slot_posted):std::nullopt;
        guard.read(0,slot0,tip_answer,tick);
        ClampInput in{};in.now=now;in.epoch=1;in.root=root;in.target=pose_at(controller);in.chest=chest;in.tip_enabled=false;
        if(started) route_clamp_hits(in,out.state,slot0,std::nullopt,std::nullopt,std::nullopt);
        out=clamp.update(0,in);started=true;
        tip_slot_posted=guard.posted(0,true,out,in.post,eye,controller,tick,7);
        clamp.note_drawn(0,root,out.drawn.position);
        // The shot, published from the next frame's aim: raw origin a little ahead of the grip, behind the door's far face.
        const Vec3 raw_origin=controller+Vec3{.02,0,.01};
        const auto clamped=clamped_shot_offset(out.drawn.position,controller,true,tick,7,&clamp,0);
        const Vec3 origin=guarded_shot_origin(guard,clamped,0,raw_origin,tick+11,7);
        if(out.state==ClampState::released) {++released_frames;guarded+=guard.applies(0,tick+11,7);}
        if(controller.x>1) farthest=std::max(farthest,origin.x);
        now+=dt;tick+=11;
    }
    CHECK(clamp.snapbacks(0)>=1 && released_frames>=60 && guarded==released_frames);
    CHECK(farthest<1);                                              // every origin on the near side of the door
    std::printf("released_shot_through_door: %u released frames, farthest origin x %.4f\n",released_frames,farthest);
    // The released guard's fallbacks: a chest that started inside geometry uses the eye sweep; nothing known: the eye.
    ReleasedShotGuard g2;ClampOutput released{};released.valid=true;released.state=ClampState::released;released.palm_sweep={{.9,0,1},{1.4,0,1},.05,true};
    CHECK(g2.posted(0,true,released,true,eye,{1.4,0,1},2000,7).has_value());
    CHECK(g2.applies(0,2000,7) && !g2.applies(0,2000,8) && !g2.applies(0,2200,7) && !g2.applies(1,2000,7));
    CHECK(length(g2.origin(0,{1.42,0,1.01},2005)-eye)<1e-12);       // posted, not answered yet: the eye
    // The chest overlap reports a normal that would put the barrel in free space (+x): it is not trusted.
    g2.read(0,ClampHit{{0,0,1},{1,0,0},5,3,true},door_cast({eye,{1.4,0,1},eye_sweep_radius,true}),2011);
    const Vec3 by_eye=g2.origin(0,{1.42,0,1.01},2015);
    CHECK(by_eye.x<1 && by_eye.x>.8);                               // on the eye -> barrel segment, before the door
    g2.read(0,std::nullopt,std::nullopt,2022);                      // nothing posted since: answers kept from the last read
    CHECK(length(g2.origin(0,{1.42,0,1.01},2025)-by_eye)<1e-12);
    // A chest sweep that met nothing: the raw origin (the controller is in free space).
    ReleasedShotGuard g3;g3.posted(0,true,released,true,eye,{1.4,0,1},3000,7);g3.read(0,std::nullopt,std::nullopt,3011);
    CHECK(g3.origin(0,{1.42,0,1.01},3015).x==1.42);
    // Stale answers (over 100 ms) are not used.
    CHECK(length(g3.origin(0,{1.42,0,1.01},3200)-eye)<1e-12);
    // Not released (contact or free): the guard does not apply, and clears its answers.
    ClampOutput contact=released;contact.state=ClampState::contact;
    CHECK(!g3.posted(0,true,contact,true,eye,{1.4,0,1},3300,7) && !g3.applies(0,3300,7));
    CHECK(!g3.posted(0,false,released,true,eye,{1.4,0,1},3400,7) && !g3.applies(0,3400,7));   // a yield
    // Answers from an earlier release are dropped when the release ends: released again 20 ms later, nothing answered yet -> the eye.
    {ReleasedShotGuard g4;g4.posted(0,true,released,true,eye,{1.4,0,1},4000,7);g4.read(0,std::nullopt,std::nullopt,4011);
     CHECK(g4.origin(0,{1.42,0,1.01},4012).x==1.42);
     g4.posted(0,true,contact,true,eye,{1.4,0,1},4012,7);g4.posted(0,true,released,true,eye,{1.4,0,1},4023,7);
     CHECK(length(g4.origin(0,{1.42,0,1.01},4025)-eye)<1e-12);}
    // before_surface: the barrel in front of the plane stays; the cut backs off by the margin.
    const ClampHit face{{1,0,0},{-1,0,0},5};
    CHECK(before_surface({0,0,0},{.8,0,0},face,.05).x==.8 && std::abs(before_surface({0,0,0},{2,0,0},face,.05).x-.95)<1e-12);
    CHECK(before_surface({1.2,0,0},{2,0,0},face,.05).x==1.2);
}

// A table at z = 0.8 (normal +z, solid below), answered like a closest-hit sphere cast.
std::optional<ClampHit> table_cast(const ClampSweep& s) {
    if(!s.valid) return std::nullopt;
    const double s0=s.from.z-.8,s1=s.to.z-.8;
    if(s0<s.radius) return ClampHit{{s.from.x,s.from.y,.8},{0,0,1},4,3,true};
    if(s1>=s.radius) return std::nullopt;
    const double t=(s0-s.radius)/(s0-s1);const Vec3 c=s.from+(s.to-s.from)*t;return ClampHit{{c.x,c.y,.8},{0,0,1},4,3,false};
}

// N1: a world UI opens with the hand resting on a table. The hand follows the controller kept out of the table top while within
// 0.35 m of it; lowered further (beside the table) it drops with the controller; a new level's menu ignores the old planes.
void paused_hand_guarded() {
    HandClamp clamp;ClampOutput out{};const auto root=level_frame({0,0,0},{1,0,0});double now=0;bool started=false;
    for(int f=0;f<60;++f) {
        ClampInput in{};in.now=now;in.epoch=3;in.root=root;in.target=pose_at({.4,0,.75});in.chest={0,0,1.3};in.tip_enabled=false;
        if(started) route_clamp_hits(in,out.state,table_cast(out.palm_sweep),std::nullopt,std::nullopt,std::nullopt);
        out=clamp.update(0,in);started=true;clamp.note_drawn(0,root,out.drawn.position);now+=dt;
    }
    CHECK(out.state==ClampState::contact && std::abs(out.drawn.position.z-.855)<1e-6);
    const double gap=clamp.config().snap_gap;
    CHECK(std::abs(paused_hand(clamp,0,pose_at({.4,0,.7}),3,gap).z-.855)<1e-6);   // pressed 15 cm in: kept on the table top
    CHECK(paused_hand(clamp,0,pose_at({.4,0,.9}),3,gap).z==.9);                 // above it: the controller
    CHECK(paused_hand(clamp,0,pose_at({.4,0,.45}),3,gap).z==.45);               // 40 cm below the top: the controller
    CHECK(paused_hand(clamp,0,pose_at({.4,0,.7}),4,gap).z==.7);                 // another epoch (a new level): the controller
    CHECK(paused_hand(clamp,1,pose_at({.4,0,.7}),3,gap).z==.7);                 // the other side holds nothing
    clamp.reset(0);CHECK(paused_hand(clamp,0,pose_at({.4,0,.7}),3,gap).z==.7);  // a reset (yielded) side: the controller
}
// N5: the shot while clamped against a thin door: steady presses into 2, 5 and 10 cm doors and jab-and-fire at 4-6 m/s, with the aim
// origin 5 or 8 cm ahead of the grip, fingertip sweeps on and off (C14), 10 or 30 cm past the near face (30 cm: the side also snaps
// back). The publication's order: the aim (last frame's offset, planes and guard; this frame's controller) -> the hands (answers ->
// guard -> clamp update -> the guard's eye sweep on the exclusion slot -> note_drawn -> store the offset and the held planes).
// Judged: every shot origin of the run on the near side of the door.
double door_fire(double thickness,double speed,double ahead,double depth,bool tips,unsigned* released=nullptr,double* early=nullptr,double* free_hand=nullptr) {
    HandClamp clamp;ReleasedShotGuard guard;ClampOutput out{};ClampedShotOffset stored{};const auto root=level_frame({0,0,0},{1,0,0});
    const Vec3 chest{0,0,1},eye{0,0,1.6};double now=0,x=.4,farthest=-1e9;std::uint64_t tick=1000;bool started=false;
    std::optional<ClampSweep> eye_posted;
    const auto cast=[&](const ClampSweep& sweep) {return door_cast(sweep,thickness);};
    for(int f=0;f<160;++f) {
        x=std::min(1+depth,x+speed*dt);const Vec3 controller{x,0,1};
        const Vec3 raw_origin=controller+Vec3{ahead,0,.01};
        // The aim's first publish, before the hands (last frame's offset and planes): info only, it is published again below.
        if(early) *early=std::max(*early,guarded_shot_origin(guard,stored,0,raw_origin,tick,7).x);
        // The hands.
        const auto slot0=started?cast(out.palm_sweep):std::nullopt;
        const auto eye_answer=eye_posted?cast(*eye_posted):std::nullopt;
        guard.read(0,slot0,eye_answer,tick);
        ClampInput in{};in.now=now;in.epoch=1;in.root=root;in.target=pose_at(controller);in.chest=chest;in.tip_enabled=tips;
        in.tip=default_clamp_tip();in.wall_tip=default_clamp_wall_tip();
        if(started) route_clamp_hits(in,out.state,slot0,cast(out.tip_sweep),cast(out.wall_tip_sweep),std::nullopt);
        out=clamp.update(0,in);started=true;
        eye_posted=guard.posted(0,true,out,in.post,eye,controller,tick,7);
        clamp.note_drawn(0,root,out.drawn.position);
        stored=clamped_shot_offset(out.drawn.position,controller,true,tick,7,&clamp,0);
        // The aim published again after the hands, with this frame's planes, offset and guard (what a shot uses from here on). Judged
        // while the clamp holds the hand (contact) or it is snapped back (released); a free hand is info (no surface met yet).
        const double x_now=guarded_shot_origin(guard,stored,0,raw_origin,tick+1,7).x;
        if(out.state!=ClampState::free) farthest=std::max(farthest,x_now);else if(free_hand) *free_hand=std::max(*free_hand,x_now);
        if(released && out.state==ClampState::released) ++*released;
        now+=dt;tick+=11;
    }
    return farthest;
}
void shot_clipped_at_held_planes() {
    double worst=-1e9,worst_early=-1e9,worst_free=-1e9;unsigned runs=0,released_runs=0;
    for(const double thickness:{.02,.05,.10}) for(const double speed:{.5,4.,5.,6.}) for(const double ahead:{.05,.08})
        for(const double depth:{.1,.3}) for(const bool tips:{true,false}) {
            unsigned released=0;double early=-1e9,free_x=-1e9;const double farthest=door_fire(thickness,speed,ahead,depth,tips,&released,&early,&free_x);
            worst_early=std::max(worst_early,early);worst_free=std::max(worst_free,free_x);
            if(farthest>=1) std::printf("door %.2f speed %.1f ahead %.2f depth %.1f tips %d: origin %.4f\n",thickness,speed,ahead,depth,int(tips),farthest);
            CHECK(farthest<1);worst=std::max(worst,farthest);++runs;released_runs+=released>0;
        }
    CHECK(released_runs>0 && released_runs<runs);                   // both the clamped and the snapped-back paths were judged
    std::printf("shot_clipped_at_held_planes: %u runs (%u snap back), farthest origin x %.4f (door at 1); before-the-hands publish (info, not used: published after them) %.4f; free hand (info) %.4f\n",runs,released_runs,worst,worst_early,worst_free);
    // Resting on the door face (pressed in, then back to the drawn hand: no offset, the touched plane still held): the aim 8 cm ahead
    // of the grip is still clipped at the door.
    {HandClamp clamp;ClampOutput out{};const auto root=level_frame({0,0,0},{1,0,0});double now=0;bool started=false;
     for(int f=0;f<90;++f) {
        ClampInput in{};in.now=now;in.epoch=1;in.root=root;in.target=pose_at({f<40?1.:.945,0,1});in.chest={0,0,1};in.tip_enabled=false;
        if(started) route_clamp_hits(in,out.state,door_cast(out.palm_sweep,.02),std::nullopt,std::nullopt,std::nullopt);
        out=clamp.update(0,in);started=true;clamp.note_drawn(0,root,out.drawn.position);now+=dt;
     }
     const auto rest=clamped_shot_offset(out.drawn.position,{.945,0,1},true,500,7,&clamp,0);
     CHECK(rest.valid && rest.plane_count>0 && length(rest.offset)==0);
     CHECK(clamped_shot_origin({1.025,0,1},rest,505,7).x<1);}
    // The clip alone: a held plane at x = 1 and an origin 3 cm into it, from a grip 5.5 cm before it.
    ClampedShotOffset o{};o.valid=true;o.epoch=7;o.tick=100;o.drawn={.945,0,1};o.planes[0]=ClampHit{{1,0,1},{-1,0,0},5};o.plane_count=1;
    const Vec3 clipped=clamped_shot_origin({1.03,0,1},o,105,7);
    CHECK(clipped.x<1-.04 && clipped.x>=.945);
    o.plane_count=0;CHECK(clamped_shot_origin({1.03,0,1},o,105,7).x==1.03);   // no plane, no offset: the origin as it is
}

// N6: the loads that forget the clamp; a forgotten side draws the controller in the next menu (no pre-death planes).
void reload_forgets() {
    CHECK(clamp_forgets_on(0) && clamp_forgets_on(1u<<15));
    CHECK(!clamp_forgets_on(2) && !clamp_forgets_on(256) && !clamp_forgets_on(16) && !clamp_forgets_on(1024) && !clamp_forgets_on((1u<<15)|2));
    HandClamp clamp;ClampOutput out{};const auto root=level_frame({0,0,0},{1,0,0});double now=0;bool started=false;
    for(int f=0;f<60;++f) {
        ClampInput in{};in.now=now;in.epoch=3;in.root=root;in.target=pose_at({.4,0,.75});in.chest={0,0,1.3};in.tip_enabled=false;
        if(started) route_clamp_hits(in,out.state,table_cast(out.palm_sweep),std::nullopt,std::nullopt,std::nullopt);
        out=clamp.update(0,in);started=true;clamp.note_drawn(0,root,out.drawn.position);now+=dt;
    }
    CHECK(std::abs(paused_hand(clamp,0,pose_at({.4,0,.7}),3,.35).z-.855)<1e-6);   // same epoch, before the reload: kept out
    clamp.reset(0);clamp.reset(1);                                                  // the publication's forget on a load
    CHECK(paused_hand(clamp,0,pose_at({.4,0,.7}),3,.35).z==.7);                    // after it: the controller, same epoch
    std::array<ClampHit,HandClamp::max_held_planes> planes{};CHECK(clamp.held_planes(0,planes)==0);
}

void released_shot_actual_receipts() {
    ReleasedShotGuard guard;ClampOutput released{};released.valid=true;released.state=ClampState::released;
    const Vec3 eye{0,0,1.6},raw{1.4,.4,1};
    const Vec3 from_a{.4,0,1},from_b{1.2,0,1};
    const ClampHit door{{1,0,1},{-1,0,0},7,3,false};
    released.palm_sweep={from_a,raw,.05,true};
    CHECK(guard.posted(0,true,released,true,eye,raw,1000,7)); // game thread takes A
    released.palm_sweep={from_b,raw,.05,true};
    CHECK(guard.posted(0,true,released,true,eye,raw,1010,7)); // render has now posted B
    CHECK(length(guard.origin(0,raw,1011)-eye)<1e-12); // posting alone proves nothing
    guard.read_receipt(0,false,from_a,door,1012);
    const auto expected=before_surface(from_a,raw,door,released_shot_margin);
    CHECK(length(guard.origin(0,raw,1013)-expected)<1e-12 && expected.x<1);
    CHECK(length(expected-before_surface(from_b,raw,door,released_shot_margin))>.1);
    // An older miss, or a changed replay at the same completion tick, cannot replace the wall answer.
    guard.read_receipt(0,false,from_b,std::nullopt,1011);
    guard.read_receipt(0,false,from_b,std::nullopt,1012);
    CHECK(length(guard.origin(0,raw,1014)-expected)<1e-12);
    // A new matched completion with no hit is a true clear answer.
    guard.read_receipt(0,false,from_a,std::nullopt,1020);
    CHECK(length(guard.origin(0,raw,1021)-raw)<1e-12);
    // Publication and replay do not refresh the original completion TTL.
    guard.posted(0,true,released,false,eye,raw,1110,7);
    guard.read_receipt(0,false,from_a,std::nullopt,1020);
    CHECK(length(guard.origin(0,raw,1120)-raw)<1e-12);
    CHECK(length(guard.origin(0,raw,1121)-eye)<1e-12);
    // Chest overlap falls back to the eye receipt's actual origin, not the newer posted eye.
    guard.read_receipt(0,false,from_a,ClampHit{{1,0,1},{-1,0,0},7,3,true},1122);
    guard.read_receipt(0,true,from_a,door,1122);
    CHECK(length(guard.origin(0,raw,1123)-expected)<1e-12);
    guard.clear(0);CHECK(!guard.applies(0,1124,7));
    guard.read_receipt(0,false,from_a,std::nullopt,1122); // reset has no active release to own a completion
    guard.posted(0,true,released,false,eye,raw,1125,7);
    CHECK(length(guard.origin(0,raw,1126)-eye)<1e-12);
    guard.read_receipt(2,false,from_a,std::nullopt,1127);
    guard.read_receipt(0,false,{std::nan(""),0,0},std::nullopt,1127);
    guard.read_receipt(0,false,from_a,std::nullopt,0);
    CHECK(length(guard.origin(0,raw,1128)-eye)<1e-12);
}
} // namespace

int main() {
    yields();posts();routes();probe_sets();hold_store();reacquire();blade_stab(true);blade_stab(false);blade_push_stays_out();shot_origin_thin_wall();default_wall_tip();released_shot_through_door();paused_hand_guarded();shot_clipped_at_held_planes();reload_forgets();released_shot_actual_receipts();
    std::puts("PASS hand_clamp_feed_check: C9 yields, C14 cadence, answer routing, open-hand probe sets, re-acquire hold, blade stab, shot origin");
    return 0;
}
