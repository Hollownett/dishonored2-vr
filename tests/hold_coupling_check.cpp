// Plan B task B16: held things follow the hand body (src/hold_coupling.hpp): motor holds pulled behind the body, native holds carried by the
// body and clamped, the body's native servo target without feedback, the hand's filter word while it holds, and the fallback bit-exact.
#define D2VR_TESTING
#include "hold_coupling.hpp"
#include "hand_body.hpp"
#include "hand_visual_source.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace d2vr;
#define CHECK(x) do{if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
namespace {
bool near(double a,double b,double tol=1e-9) {return std::fabs(a-b)<=tol;}
WorldTrackedPose pose_at(double x,double y=0,double z=1) {WorldTrackedPose p;p.position={x,y,z};p.basis={Vec3{1,0,0},Vec3{0,1,0},Vec3{0,0,1}};return p;}
WorldTrackedPose pose_at(Vec3 v) {return pose_at(v.x,v.y,v.z);}
WorldTrackedPose tilted(Vec3 at) {   // a controller with a non-trivial orientation, so "the controller's basis" is checked for real
    WorldTrackedPose p;p.position=at;const double c=std::cos(.4),s=std::sin(.4);p.basis={Vec3{c,s,0},Vec3{-s,c,0},Vec3{0,0,1}};return p;
}
bool bit_equal(const WorldTrackedPose& a,const WorldTrackedPose& b) {return std::memcmp(&a,&b,sizeof a)==0;}
constexpr double dt90=1./90;

// A wall at x = w (free space x < w) answering the held clamp's sphere sweeps one frame late, as the game does.
std::array<std::optional<ClampHit>,held_proxy_capacity> answer(const HeldClampOutput& out,std::size_t n,double w) {
    std::array<std::optional<ClampHit>,held_proxy_capacity> hits{};
    for(std::size_t i=0;i<n;++i) {
        const auto& s=out.sweeps[i];if(!s.valid) continue;
        ClampHit h;h.normal={-1,0,0};
        if(s.from.x+s.radius>w) {h.point={w,s.from.y,s.from.z};h.initial_overlap=true;hits[i]=h;}   // touching is not inside
        else if(s.to.x+s.radius>w) {h.point={w,s.to.y,s.to.z};hits[i]=h;}
    }
    return hits;
}
// The release checks' answers per released object (HandFilterKeeper's overlap callback): clear unless told otherwise.
struct Answers {
    std::array<std::pair<std::uintptr_t,int>,8> state{};   // 0 clear, 1 inside, 2 unanswered
    int of(std::uintptr_t e) const {for(const auto& s:state) if(s.first==e) return s.second;return 0;}
    void set(std::uintptr_t e,int v) {for(auto& s:state) if(s.first==e || !s.first) {s={e,v};return;}}
    void clear(std::uintptr_t e) {set(e,0);}void inside(std::uintptr_t e) {set(e,1);}void unanswered(std::uintptr_t e) {set(e,2);}
    HandOverlap operator()(std::uintptr_t e,bool) const {
        HandOverlap o;const int v=of(e);o.answered=v!=2;
        if(v==1) {ClampHit h;h.point={0,0,1};h.normal={0,0,1};h.entity=e;o.hit=h;o.depth=.02;}
        return o;
    }
};
FilterKeeperInput corpse_hold(double now,std::uintptr_t entity) {
    FilterKeeperInput in{};in.now=now;in.body_ready=true;in.body_generation=3;in.held_layer=hand_body::layer_npc_body_ragdoll;in.held_entity=entity;in.held_corpse=true;return in;
}
// One native hold, frame by frame, exactly as publish_held_object runs it. The drawn grip is the controller (+grip_offset) unless use_b15 runs
// B15's HandVisualSource (then the grip is B15's drawn hand and the weight its body_weight); the clamp sweeps the controller's request while the
// body is drawn (B4's own request otherwise); couple_native gives the target, the body's servo target and the raw-gap release; the hand body is a
// capped first-order servo toward its target, stopped by the wall (or wedged where it is).
struct NativeRig {
    HeldClamp clamp;HoldCoupling coupling;HandVisualSource b15;std::optional<HeldProxies> proxies;
    double wall=1e9,now=100;std::array<std::optional<ClampHit>,held_proxy_capacity> hits{};
    Vec3 body{0,0,1};WorldTrackedPose controller=pose_at(0);Vec3 offset{.3,0,0},grip_offset{};
    bool body_on=true,use_b15=false,wedged=false,contact=false,dropped=false;double weight=1;std::uint64_t seq=1;
    CouplingOutput last{};WorldTrackedPose target{};
    NativeRig() {HeldProxies p;p.count=1;p.sphere[0]={{0,0,0},.1};proxies=p;}
    WorldTrackedPose step(double dt=dt90) {
        now+=dt;
        Vec3 grip=controller.position+grip_offset;
        if(body_on && use_b15) {
            VisualInput vi{};vi.now=now;vi.body_ready=true;vi.body=pose_at(body);vi.body_seq=++seq;vi.clamp=pose_at(grip);vi.controller=pose_at(grip);
            vi.body_contact=vi.world_hit_near=contact;
            const auto v=b15.update(0,vi);weight=v.body_weight;grip=v.drawn.position;
        }
        WorldTrackedPose request=controller;request.position=grip+offset;
        const bool coupled=body_on && weight>0;
        HeldClampInput in{};in.now=now;in.epoch=1;in.proxies=proxies;in.hits=hits;
        in.requested=coupled?HoldCoupling::controller_request(request,grip,controller.position):request;
        const auto out=clamp.update(in);
        hits=answer(out,proxies->count,wall);
        target=out.target;
        if(body_on) {last=couple_native(coupling,0,clamp,proxies,request,controller,pose_at(body),true,coupled,in.requested,out.target,now);if(coupled) target=last.target;}
        clamp.note_published(target);
        dropped=clamp.should_release() || (body_on && last.release);
        if(body_on && !wedged) {
            const Vec3 goal=last.body_target?*last.body_target:controller.position;const Vec3 e=goal-body;const double l=length(e);
            if(l>1e-12) body=body+e*(std::min(std::min(l*12,10.)*dt,l)/l);
            body.x=std::min(body.x,wall-.05);   // the 5 cm hand sphere meets the wall too
        }
        return target;
    }
    // The controller moves to x at 0.5 m/s (the object meets the wall from free air, as in play: a sweep that starts inside makes no plane).
    void approach(double x) {
        while(std::fabs(controller.position.x-x)>1e-12) {
            const double stepx=std::min(std::fabs(x-controller.position.x),.5*dt90);
            controller.position.x+=x>controller.position.x?stepx:-stepx;step();
        }
    }
};
}

int main() {
    {   // body_not_ready_is_todays_behaviour: knob off / fault / not created / stale (body_ready false), a ready flag with no pose, a NaN pose:
        // the target is the controller (motor) or the clamp's output (native), bit-exact, every frame; uses_body false; nothing released
        for(const HoldKind kind:{HoldKind::motor_prop,HoldKind::corpse,HoldKind::native}) {
            HoldCoupling c;double now=10;
            for(int i=0;i<600;++i) {
                now+=dt90;
                CouplingInput in{};in.kind=kind;in.now=now;in.controller=tilted({.1*i,-.2,1.3});
                in.clamped_native=pose_at(.5,.1*i,.7);
                switch(i%4) {
                case 0: in.body_ready=false;in.body=pose_at(9,9,9);break;            // a body far away, but not ready
                case 1: in.body_ready=true;break;                                    // ready, no pose
                case 2: in.body_ready=true;in.body=pose_at(std::nan(""));break;      // garbage
                default: in.body_ready=false;break;
                }
                const auto o=c.update(0,in);
                CHECK(!o.uses_body);CHECK(!o.release);CHECK(o.lag_m==0);CHECK(!o.body_target);
                CHECK(bit_equal(o.target,kind==HoldKind::native?*in.clamped_native:in.controller));
            }
        }
        // fix round 1: a ready body that B15 does not draw (body_weight 0, free air) is today's controller pose too, bit-exact
        for(const HoldKind kind:{HoldKind::motor_prop,HoldKind::corpse}) {
            HoldCoupling c;CouplingInput in{};in.kind=kind;in.body_ready=true;in.controller=tilted({.1,.2,1.3});in.body_weight=0;
            for(int i=0;i<100;++i) {in.now=1+i*dt90;in.controller.position.x+=.02;in.body=pose_at(in.controller.position.x-.1,.2,1.3);
                const auto o=c.update(0,in);CHECK(bit_equal(o.target,in.controller) && !o.uses_body && !o.release && near(o.lag_m,.1,1e-12));}
            in.body_weight=std::nan("");CHECK(!c.update(0,in).uses_body);
        }
        // couple_native without a ready body publishes the clamp's own output for the controller request
        NativeRig r;r.body_on=false;r.wall=.35;HeldClamp b4;std::array<std::optional<ClampHit>,held_proxy_capacity> b4_hits{};double t=r.now;
        for(int i=0;i<300;++i) {
            r.controller.position.x=-.2+.002*i;
            const auto got=r.step();t+=dt90;
            WorldTrackedPose request=r.controller;request.position=r.controller.position+r.offset;
            HeldClampInput in{};in.now=t;in.epoch=1;in.proxies=r.proxies;in.hits=b4_hits;in.requested=request;
            const auto want=b4.update(in);b4_hits=answer(want,1,r.wall);b4.note_published(want.target);
            CHECK(bit_equal(got,want.target));
            const auto o=couple_native(r.coupling,1,r.clamp,r.proxies,request,r.controller,pose_at(0),false,false,request,want.target,t);
            CHECK(bit_equal(o.target,want.target) && !o.uses_body && !o.body_target);
        }
    }
    {   // motor_hold_uses_body_pose: a ready body: the grab's hand pose is the body's position with the controller's orientation
        HoldCoupling c;CouplingInput in{};in.kind=HoldKind::motor_prop;in.now=1;in.body_ready=true;in.body=pose_at(.32,.01,1.2);in.controller=tilted({.3,0,1.2});
        const auto o=c.update(0,in);
        CHECK(o.uses_body && !o.release && !o.body_target);
        CHECK(o.target.position.x==.32 && o.target.position.y==.01 && o.target.position.z==1.2);
        CHECK(std::memcmp(&o.target.basis,&in.controller.basis,sizeof o.target.basis)==0);
        CHECK(near(o.lag_m,std::sqrt(.02*.02+.01*.01)));
        in.kind=HoldKind::corpse;const auto k=c.update(1,in);CHECK(k.uses_body && near(k.target.position.x,.32));
    }
    {   // blocked_hand_lags_held_prop: the world stopped the body 0.3 m short of the controller: the held prop is pulled to the body, lag 0.3, no release
        for(const HoldKind kind:{HoldKind::motor_prop,HoldKind::corpse}) {
            HoldCoupling c;double now=5;CouplingOutput o{};
            for(int i=0;i<200;++i) {   // 2 s
                now+=.01;CouplingInput in{};in.kind=kind;in.now=now;in.body_ready=true;in.body=pose_at(.5);in.controller=pose_at(.8);
                o=c.update(0,in);CHECK(!o.release);
            }
            CHECK(o.uses_body && near(o.target.position.x,.5) && near(o.lag_m,.3,1e-12));
        }
    }
    {   // lag_release_after_quarter_second (plan A's corpse rule, the DwellTimer clock): 0.6 m for 0.24 s no; 0.26 s release
        HoldCoupling c;double now=5;bool fired=false;double held=0;
        CouplingInput in{};in.kind=HoldKind::corpse;in.body_ready=true;in.body=pose_at(0);in.controller=pose_at(.6);
        in.now=now;CHECK(!c.update(0,in).release);   // the first frame starts the clock
        for(int i=0;i<24;++i) {now+=.01;held+=.01;in.now=now;fired=c.update(0,in).release;CHECK(!fired);}
        CHECK(near(held,.24,1e-9));
        now+=.02;in.now=now;CHECK(c.update(0,in).release);   // 0.26 s
        // the clock restarts when the lag drops; a hitch counts at most max_dt (Review Focus 4: a 0.5 s pause never releases at once)
        HoldCoupling h;double t=5;in.now=t;h.update(0,in);
        t+=.5;in.now=t;CHECK(!h.update(0,in).release);
        in.body=pose_at(.3);for(int i=0;i<100;++i) {t+=.01;in.now=t;CHECK(!h.update(0,in).release);}
        // props keep their 1.5 m rule: no dwell, 1.4 m never, 1.6 m at once
        HoldCoupling p;CouplingInput q{};q.kind=HoldKind::motor_prop;q.body_ready=true;q.body=pose_at(0);q.controller=pose_at(1.4);
        for(int i=0;i<100;++i) {q.now=1+i*.01;CHECK(!p.update(0,q).release);}
        q.controller=pose_at(1.6);q.now=3;CHECK(p.update(0,q).release);
        // the corpse rule equals plan A's PhysicalGrab numbers (X-3)
        CHECK(HoldCoupling::lag_release_m==PhysicalGrab::lag_release_m && HoldCoupling::lag_release_s==PhysicalGrab::lag_release_s);
    }
    {   // native_target_is_clamped_pose: the target is the body-carried request pushed out of the wall; the body's own target is the hand pose of the
        // clamped controller request; free air: the body's target is the controller palm exactly
        NativeRig r;r.wall=.35;r.controller=pose_at(-.2);r.body={-.2,0,1};
        for(int i=0;i<10;++i) r.step();
        r.approach(0);for(int i=0;i<90;++i) r.step();
        const auto first=r.target;
        CHECK(r.last.uses_body && r.last.body_target);
        CHECK(near(first.position.x,.245,1e-5));                                   // 0.35 - radius 0.1 - skin 0.005 (the body still converging)
        CHECK(near(r.last.body_target->x,-.055,1e-9));                              // the hand pose whose object just touches the wall (exact)
        CHECK(first.position.x+.1<=r.wall);
        NativeRig f;for(int i=0;i<30;++i) f.step();
        CHECK(f.last.body_target && f.last.body_target->x==f.controller.position.x && f.last.body_target->y==f.controller.position.y &&
              f.last.body_target->z==f.controller.position.z);   // free air: B14's target, exactly
        CHECK(near(f.target.position.x,f.body.x+.3,1e-12));                         // the object is carried by the body
        // the drop rule stays B4's, on the controller's request: 0.36 m deep for 0.25 s releases; 0.2 m never does
        NativeRig d;d.wall=.35;d.controller=pose_at(-.2);d.body={-.2,0,1};d.approach(.2);
        for(int i=0;i<90;++i) {d.step();CHECK(!d.clamp.should_release());}          // 0.255 m past the clamped pose
        d.approach(.42);bool dropped=false;for(int i=0;i<40 && !dropped;++i) {d.step();dropped=d.clamp.should_release();}
        CHECK(dropped);
    }
    {   // no_drift_controller_still_10s (feedback ruling): the controller holds still for 10 s; the target stays within 1 mm, in free air and
        // pressed into a wall, and hand, body and object agree
        for(const double wall:{1e9,.35,.32}) {
            NativeRig r;r.wall=wall;r.controller=pose_at(-.2);r.body={-.24,.02,1.01};
            for(int i=0;i<10;++i) r.step();
            r.approach(0);
            for(int i=0;i<90;++i) r.step();                                          // 1 s to settle
            const Vec3 at=r.target.position;double worst=0,worst_body=0;const Vec3 body_at=r.body;
            for(int i=0;i<900;++i) {r.step();worst=std::max(worst,length(r.target.position-at));worst_body=std::max(worst_body,length(r.body-body_at));}
            CHECK(worst<=1e-3);CHECK(worst_body<=1e-3);
            CHECK(near(length(r.target.position-(r.body+r.offset)),0,1e-3));        // the object sits on the hand body's offset
            if(wall<1e8) CHECK(r.target.position.x+.1<=wall+1e-9);                    // never inside the wall
            else CHECK(length(r.body-r.controller.position)<=1e-3);                   // free air: the body is at the controller
        }
        // no loop is possible: the native target never reads the body (shove the body 0.2 m: the target does not move)
        NativeRig shove;for(int i=0;i<90;++i) shove.step();const Vec3 shove_at=shove.target.position;
        for(int i=0;i<30;++i) {shove.body=shove.body+Vec3{.2,.1,0};shove.step();CHECK(length(shove.target.position-shove_at)<=1e-12);}
    }
    {   // free_air_swing_object_tracks_controller (fix round 1): a 2 m/s swing with no contact; the body lags (~0.17 m), B15 does not draw it, so the
        // held object's target stays within 1 cm of the B4 target throughout: motor holds (B15 + HoldCoupling) and native holds (the full rig)
        HandVisualSource src;HoldCoupling c;double now=10,worst=0;Vec3 body{0,0,1},hand{0,0,1};std::uint64_t seq=1;
        for(int i=0;i<180;++i) {
            now+=dt90;hand.x=.4*std::sin(5.*i*dt90);hand.y=.2*std::cos(5.*i*dt90);   // ~2 m/s peak
            const Vec3 e=hand-body;const double l=length(e);if(l>1e-12) body=body+e*(std::min(l*12*dt90,l)/l);
            VisualInput vi{};vi.now=now;vi.body_ready=true;vi.body=pose_at(body);vi.body_seq=++seq;vi.clamp=pose_at(hand);vi.controller=pose_at(hand);
            const auto v=src.update(0,vi);
            CouplingInput in{};in.kind=HoldKind::corpse;in.body_ready=true;in.body=pose_at(body);in.controller=pose_at(hand);in.now=now;in.body_weight=v.body_weight;
            const auto o=c.update(0,in);worst=std::max(worst,length(o.target.position-hand));CHECK(!o.release);
        }
        CHECK(worst<=.01);
        NativeRig coupled,b4;b4.body_on=false;double worst_native=0,lag=0;
        for(int i=0;i<180;++i) {
            const double x=.4*std::sin(5.*i*dt90);coupled.controller.position.x=b4.controller.position.x=x;
            coupled.step();b4.step();worst_native=std::max(worst_native,length(coupled.target.position-b4.target.position));
            lag=std::max(lag,length(coupled.body-coupled.controller.position));
        }
        CHECK(worst_native<=.01);CHECK(lag>.05);   // the body really lagged
    }
    {   // coupling_edge_blends_no_step (fix round 1): contact makes B15 draw the body; the coupled target eases from the controller to the body over
        // B15's 0.15 s and back when contact ends: no frame steps more than the blend's own rate
        HandVisualSource src;HoldCoupling c;double now=10;std::uint64_t seq=1;const Vec3 hand{0,0,1},body{.08,0,1};
        VisualInput vi{};vi.body_ready=true;vi.body=pose_at(body);vi.clamp=pose_at(hand);vi.controller=pose_at(hand);
        Vec3 last=hand;double worst_step=0;int frames_to_body=-1;
        for(int i=0;i<120;++i) {
            now+=dt90;vi.now=now;vi.body_seq=++seq;vi.body_contact=vi.world_hit_near=i>=20 && i<70;
            const auto v=src.update(0,vi);
            CouplingInput in{};in.kind=HoldKind::motor_prop;in.body_ready=true;in.body=pose_at(body);in.controller=pose_at(hand);in.now=now;in.body_weight=v.body_weight;
            const auto o=c.update(0,in);
            worst_step=std::max(worst_step,length(o.target.position-last));last=o.target.position;
            if(frames_to_body<0 && length(o.target.position-body)<1e-9) frames_to_body=i-20;
        }
        CHECK(worst_step<=.08*dt90/HandVisualSource::blend_seconds+1e-6);   // 5.9 mm a frame at 90 Hz for an 8 cm gap
        CHECK(frames_to_body>=13 && frames_to_body<=15);                    // the full body after the 0.15 s blend
        CHECK(length(last-hand)<1e-9);                                       // back on the controller after contact
    }
    {   // DeepSeek P2: a coupling edge (the body becomes ready or is lost, a snap) moves the hand a grab samples by a known step; without the rebase a
        // step over 0.5 m drops the press, with it the hold goes on and the step is not thrown
        const auto bind=[](PhysicalGrab& g) {
            std::uint64_t tick=1000;
            g.sample(pose_at(0),0.f,true,1,tick);tick+=11;g.sample(pose_at(0),1.f,true,1,tick);tick+=11;
            CHECK(g.pressed() && g.bind(pose_at(.1),Vec3{.05,0,1}));return tick;
        };
        PhysicalGrab plain;auto tick=bind(plain);
        CHECK(!plain.sample(pose_at(.6),1.f,true,1,tick) && !plain.pressed());               // a 0.6 m step: read as a discontinuity
        PhysicalGrab rebased;tick=bind(rebased);
        rebased.rebase(pose_at(.6));CHECK(rebased.sample(pose_at(.6),1.f,true,1,tick) && rebased.pressed() && rebased.held());
        tick+=11;rebased.sample(pose_at(.6),1.f,true,1,tick);CHECK(length(rebased.velocity())<.5);   // the step is not motion
    }
    {   // fix round 2 (review P3-3): a ready body that B15 does not draw (weight 0) is B4 exactly for a native hold, with the grip off the palm: B4's
        // request and B4's clamp output, bit-exact every frame, into a wall and out
        NativeRig r,b4;r.weight=0;b4.body_on=false;r.wall=b4.wall=.35;r.grip_offset=b4.grip_offset={.03,.01,-.02};
        r.controller=b4.controller=pose_at(-.2);
        for(int i=0;i<200;++i) {
            const double x=-.2+std::min(i,80)*.004-std::max(0,i-140)*.004;r.controller.position.x=b4.controller.position.x=x;
            CHECK(bit_equal(r.step(),b4.step()));
        }
        CHECK(r.last.uses_body==false && !r.last.release);
    }
    {   // corpse_stuck_releases_at_half_metre (fix round 2, through the chain): a corpse held by a hand body wedged on geometry while the controller pulls
        // away at 1 m/s; B15 stops drawing the body beyond 0.12 m (the weight falls to 0, the corpse follows the controller), and the RAW gap releases at
        // 0.5 m held for 0.25 s
        HandVisualSource src;HoldCoupling c;double now=10,over_at=-1,released_at=-1,weight_at_release=1;std::uint64_t seq=1;const Vec3 body{0,0,1};Vec3 hand{0,0,1};
        for(int i=0;i<150 && released_at<0;++i) {
            now+=dt90;hand.x=std::min(.9,hand.x+1.*dt90);
            VisualInput vi{};vi.now=now;vi.body_ready=true;vi.body=pose_at(body);vi.body_seq=++seq;vi.clamp=pose_at(hand);vi.controller=pose_at(hand);vi.body_contact=vi.world_hit_near=true;
            const auto v=src.update(0,vi);
            CouplingInput in{};in.kind=HoldKind::corpse;in.body_ready=true;in.body=pose_at(body);in.controller=pose_at(hand);in.now=now;in.body_weight=v.body_weight;
            const auto o=c.update(0,in);
            if(over_at<0 && o.lag_m>HoldCoupling::lag_release_m) over_at=now;
            if(o.release) {released_at=now;weight_at_release=v.body_weight;}
            if(!o.release && hand.x>.2) CHECK(o.target.position.x>=hand.x-.12-1e-9);   // the corpse is pulled toward the controller once B15 lets go
        }
        CHECK(released_at>0 && over_at>0 && released_at-over_at>=HoldCoupling::lag_release_s-dt90-1e-9 && released_at-over_at<=HoldCoupling::lag_release_s+dt90+1e-9);
        CHECK(weight_at_release==0);   // the release fired with B15 drawing the controller: the raw gap, not the drawn one
    }
    {   // prop_keeps_1_5m_rule (fix round 2, through the chain): a wedged hand body, the controller 1.4 m away: no release however long; 1.6 m: at once
        HandVisualSource src;HoldCoupling c;double now=10;std::uint64_t seq=1;const Vec3 body{0,0,1};
        const auto run=[&](double x,int frames) {
            bool any=false;
            for(int i=0;i<frames;++i) {
                now+=dt90;const Vec3 hand{x,0,1};
                VisualInput vi{};vi.now=now;vi.body_ready=true;vi.body=pose_at(body);vi.body_seq=++seq;vi.clamp=pose_at(hand);vi.controller=pose_at(hand);
                const auto v=src.update(0,vi);
                CouplingInput in{};in.kind=HoldKind::motor_prop;in.body_ready=true;in.body=pose_at(body);in.controller=pose_at(hand);in.now=now;in.body_weight=v.body_weight;
                const auto o=c.update(0,in);any=any || o.release;
                if(i>20) CHECK(bit_equal(o.target,pose_at(hand)) && !o.uses_body);   // not drawn: the controller, B4's pose
            }
            return any;
        };
        CHECK(!run(1.4,180));CHECK(run(1.6,1));
    }
    {   // native_hold_stuck_releases (fix round 2, through the chain): the hand body wedged, the controller 0.45 m away in free air (the clamp sees no
        // wall, so HeldClamp never drops it); the object follows the drawn hand (B15 lets go of the far body) and the raw gap drops the hold after 0.25 s
        NativeRig r;r.use_b15=true;r.wedged=true;r.contact=true;r.body={0,0,1};
        double over_at=-1,dropped_at=-1;
        for(int i=0;i<200 && dropped_at<0;++i) {
            r.controller.position.x=std::min(.45,r.controller.position.x+1.*dt90);r.step();
            CHECK(!r.clamp.should_release());
            if(over_at<0 && r.last.lag_m>HoldCoupling::native_release_m) over_at=r.now;
            if(r.dropped) dropped_at=r.now;
        }
        CHECK(dropped_at>0 && dropped_at-over_at>=HoldCoupling::native_release_s-dt90-1e-9 && dropped_at-over_at<=HoldCoupling::native_release_s+dt90+1e-9);
        CHECK(r.weight==0 && near(r.target.position.x,r.controller.position.x+.3,1e-9));   // B15 draws the controller; the object follows it
        // a wedged body near the wall keeps its planes: the clamp keeps sweeping the controller's request (review P3-4)
        NativeRig w;w.wall=.35;w.controller=pose_at(-.2);w.body={-.2,0,1};w.approach(0);w.wedged=true;w.controller.position.x=.05;
        int planes=0;for(int i=0;i<90;++i) {w.step();if(w.clamp.project(pose_at(.3),w.proxies)) ++planes;}
        CHECK(planes>=89);
    }
    {   // HeldClamp::project: the planes applied with nothing changed; equal to update()'s own push-out for the same request
        NativeRig a,b;a.wall=b.wall=.35;a.body_on=b.body_on=false;
        for(int i=0;i<120;++i) {
            a.controller.position.x=b.controller.position.x=-.2+.003*i;
            const auto ta=a.step();(void)a.clamp.project(pose_at(.9,.3,1),a.proxies);(void)a.clamp.project(pose_at(-3),a.proxies);   // a's extra queries
            const auto tb=b.step();
            CHECK(bit_equal(ta,tb));                                                 // project changed nothing
            WorldTrackedPose request=a.controller;request.position=a.controller.position+a.offset;
            if(const auto p=a.clamp.project(request,a.proxies)) CHECK(p->x<=.245+1e-9);
        }
        HeldClamp fresh;CHECK(!fresh.project(pose_at(0),std::optional<HeldProxies>{}));
        NativeRig free;free.step();CHECK(!free.clamp.project(pose_at(0),free.proxies));   // no plane held: none
    }
    {   // hand_does_not_collide_with_held_layer (B8's arithmetic, 0x992c0 without the pair check): false while holding, true after the restore
        using namespace hand_body;
        const std::uint32_t all=0x07ffffffu;   // B-4: a full collides-with mask
        const std::uint32_t hand=R1Recipe::filter;
        // a native hold's item is DEBRIS for the hold (native_held_object swap): never paired with the hand's default word
        CHECK(!layers_collide(hand,filter_word(layer_debris,all)));
        CHECK(layers_collide(hand,filter_word(layer_rigid_body,all)));                 // the tank back on RIGID_BODY after SafeRelease's restore
        // a corpse limb on NPC_BODY_RAGDOLL 14: the keeper's word while held, the default after the restore
        Answers clear;HandFilterKeeper k;FilterKeeperInput in=corpse_hold(1,0xc0);
        const auto held=k.update(0,in,clear);
        CHECK(held.stripped && held.swapped==1 && held.word==0x000085e4u && HandFilterKeeper::allowed(held.word) && hand_filter_word_allowed(held.word));
        CHECK(!layers_collide(held.word,filter_word(layer_npc_body_ragdoll,all)));
        in.held_layer.reset();in.now=1.1;const auto back=k.update(0,in,clear);   // let go, the hand clear of it
        CHECK(back.restored==1 && !back.unresolved && back.word==hand && layers_collide(back.word,filter_word(layer_npc_body_ragdoll,all)));
        // a motor-held prop on RIGID_BODY 3: the same
        CHECK(!layers_collide(HandFilterKeeper::holding_word(layer_rigid_body),filter_word(layer_rigid_body,all)));
        // whatever it holds the hand still meets the world and other things, and never the player
        for(const unsigned layer:{layer_npc_body_ragdoll,layer_rigid_body,layer_debris}) {
            const auto w=HandFilterKeeper::holding_word(layer);
            for(const unsigned world:{layer_world_low,layer_world_low_high,layer_world_high,layer_mover}) CHECK(layers_collide(w,filter_word(world,all)));
            for(const unsigned player:{layer_player,layer_player_hitbox,layer_player_dead,layer_player_physics}) CHECK(!layers_collide(w,filter_word(player,all)));
        }
        CHECK(layers_collide(HandFilterKeeper::holding_word(layer_npc_body_ragdoll),filter_word(layer_rigid_body,all)));   // a held corpse: crates still solid
        // the world layers are never taken out, and DEBRIS was never in the mask
        CHECK(HandFilterKeeper::holding_word(layer_world_low)==hand && HandFilterKeeper::holding_word(layer_mover)==hand && HandFilterKeeper::holding_word(layer_debris)==hand);
        CHECK(!HandFilterKeeper::allowed(hand_filter_word(true)) && !HandFilterKeeper::allowed(filter_word(layer_debris,hand_collides_with(false)|layer_bit(layer_player))));
        CHECK(!HandFilterKeeper::allowed(filter_word(layer_debris,hand_collides_with(false)&~layer_bit(layer_world_low))) && !HandFilterKeeper::allowed(filter_word(layer_rigid_body,hand_collides_with(false))));
    }
    {   // layer_restore_only_via_safe_release: the default word comes back only when SafeRelease says the hand is clear of that object (or 0.5 s:
        // unresolved), or with a new body; never on the hold's end itself, never on an unanswered check
        Answers w;HandFilterKeeper k;FilterKeeperInput in=corpse_hold(10,0xa1);
        CHECK(k.update(0,in,w).stripped);
        in.held_layer.reset();in.now+=dt90;w.unanswered(0xa1);
        auto o=k.update(0,in,w);CHECK(o.stripped && o.releasing && !o.restored);       // the hold ended, unanswered: kept
        w.inside(0xa1);
        for(int i=0;i<30;++i) {in.now+=dt90;o=k.update(0,in,w);CHECK(o.stripped && !o.restored && o.overlap);}   // still inside
        w.clear(0xa1);in.now+=dt90;o=k.update(0,in,w);CHECK(o.restored==1 && !o.stripped && o.word==HandFilterKeeper::default_word && !o.unresolved);
        // never clear: restored anyway after 0.5 s and counted (B2's rule)
        HandFilterKeeper u;in=corpse_hold(20,0xa2);u.update(0,in,w);in.held_layer.reset();w.inside(0xa2);
        bool restored=false;const double since=in.now;
        while(!restored && in.now<since+1) {in.now+=dt90;o=u.update(0,in,w);restored=o.restored>0;if(!restored) CHECK(o.stripped);}
        CHECK(restored && o.unresolved==1 && in.now-since>=SafeRelease::max_seconds-1e-9 && in.now-since<SafeRelease::max_seconds+.05);
        // a new body (a snap, a recreate) while releasing starts from the default word; no body at all: the default
        HandFilterKeeper g;in=corpse_hold(30,0xa3);w.unanswered(0xa3);g.update(0,in,w);
        in.held_layer.reset();g.update(0,in,w);in.body_generation=4;o=g.update(0,in,w);CHECK(o.recreated==1 && !o.stripped && !o.restored);
        HandFilterKeeper n;in=corpse_hold(31,0xa3);n.update(0,in,w);in.body_ready=false;o=n.update(0,in,w);CHECK(o.recreated==1 && o.word==HandFilterKeeper::default_word);
        // a new body DURING the hold is stripped again (it was born with the default word; the hold still needs the strip)
        HandFilterKeeper h;in=corpse_hold(32,0xa3);h.update(0,in,w);in.body_generation=5;o=h.update(0,in,w);CHECK(o.stripped && !o.swapped);
        // a regrab of the same object while its check runs continues that entry: stripped, no new swap, its check dropped
        HandFilterKeeper rg;in=corpse_hold(33,0xa3);rg.update(0,in,w);in.held_layer.reset();rg.update(0,in,w);in=corpse_hold(33.1,0xa3);
        o=rg.update(0,in,w);CHECK(o.stripped && !o.swapped && !o.releasing);
        // DEBRIS (a native item) and the world are never stripped: nothing to restore
        HandFilterKeeper d;in=corpse_hold(34,0xa4);in.held_layer=hand_body::layer_debris;o=d.update(0,in,w);CHECK(!o.stripped && !o.swapped && o.word==HandFilterKeeper::default_word);
    }
    {   // DeepSeek P2: a new hold while an earlier object's check still runs keeps that object's layer out until ITS check clears (two objects,
        // two layers, two checks)
        Answers w;HandFilterKeeper k;FilterKeeperInput in=corpse_hold(40,0xb1);k.update(0,in,w);
        in.held_layer.reset();w.inside(0xb1);in.now+=dt90;k.update(0,in,w);                 // corpse A let go while the hand is inside it
        in.held_layer=hand_body::layer_rigid_body;in.held_entity=0xb2;in.held_corpse=false;in.now+=dt90;   // grab prop B (RIGID_BODY 3)
        auto o=k.update(0,in,w);
        const std::uint32_t both=hand_body::filter_word(hand_body::layer_debris,HandFilterKeeper::default_mask&~hand_body::layer_bit(hand_body::layer_npc_body_ragdoll)&~hand_body::layer_bit(hand_body::layer_rigid_body));
        CHECK(o.word==both && o.swapped==1 && o.releasing);                                  // 14 stays out while A overlaps; 3 out for B
        for(int i=0;i<10;++i) {in.now+=dt90;o=k.update(0,in,w);CHECK(o.word==both);}
        w.clear(0xb1);in.now+=dt90;o=k.update(0,in,w);CHECK(o.restored==1 && o.word==HandFilterKeeper::holding_word(hand_body::layer_rigid_body));
        // corpse A then corpse C, both layer 14: 14 returns only when both checks cleared
        HandFilterKeeper c;in=corpse_hold(50,0xc1);c.update(0,in,w);in.held_layer.reset();w.inside(0xc1);c.update(0,in,w);
        in=corpse_hold(50.1,0xc2);c.update(0,in,w);in.held_layer.reset();w.clear(0xc2);o=c.update(0,in,w);   // C let go clear, A still inside
        CHECK(o.restored==1 && o.stripped && o.word==HandFilterKeeper::holding_word(hand_body::layer_npc_body_ragdoll));
        w.clear(0xc1);in.now+=dt90;o=c.update(0,in,w);CHECK(o.restored==1 && o.word==HandFilterKeeper::default_word);
        // unreadable is not clear: an object that cannot be read keeps its check waiting (then 0.5 s, unresolved)
        NativeRigidBody none[1]{};CHECK(!hand_overlap({0,0,1},R1Recipe::radius,none,0).answered);
    }
    {   // fix round 2 (review P3-1): a stale body pose never ends the hold nor times out a release check: the checks freeze until the pose is fresh
        Answers w;HandFilterKeeper k;FilterKeeperInput in=corpse_hold(60,0xe1);k.update(0,in,w);
        in.pose_fresh=false;for(int i=0;i<90;++i) {in.now+=dt90;const auto o=k.update(0,in,w);CHECK(o.stripped && !o.releasing);}   // still held: kept
        in.pose_fresh=true;in.held_layer.reset();w.inside(0xe1);in.now+=dt90;k.update(0,in,w);   // let go while inside it
        in.pose_fresh=false;for(int i=0;i<120;++i) {in.now+=dt90;const auto o=k.update(0,in,w);CHECK(o.stripped && o.releasing && !o.restored);}   // 1.3 s stale: frozen
        in.pose_fresh=true;w.clear(0xe1);in.now+=dt90;const auto o=k.update(0,in,w);CHECK(o.restored==1 && !o.unresolved && o.word==HandFilterKeeper::default_word);
        // and the clock really resumes: inside again after a freeze, the 0.5 s budget counts only fresh time
        HandFilterKeeper u;in=corpse_hold(80,0xe2);u.update(0,in,w);in.held_layer.reset();w.inside(0xe2);double fresh_time=0;bool restored=false;
        for(int i=0;i<200 && !restored;++i) {in.now+=dt90;in.pose_fresh=(i/10)%2==0;if(in.pose_fresh) fresh_time+=dt90;restored=u.update(0,in,w).restored>0;}
        CHECK(restored && fresh_time>=SafeRelease::max_seconds-2*dt90);
    }
    {   // released_inside_geometry_keeps_layer (B2's behaviour with the body on): a native item released inside the floor keeps DEBRIS until its probes
        // read clear (or 0.5 s); the coupling never arms or skips that restore. A corpse limb released around the hand keeps the hand's word the same way.
        SafeRelease safe;NativeRig r;r.wall=.35;r.controller=pose_at(-.2);r.body={-.2,0,1};r.approach(0);for(int i=0;i<30;++i) r.step();
        CHECK(r.last.uses_body);
        ClampHit floor;floor.point={0,0,0};floor.normal={0,0,1};double t=50;
        SafeReleaseInput in{};in.released=true;
        for(int i=0;i<20;++i) {t+=dt90;in.now=t;in.overlap=floor;in.sweep_answered=true;in.depth=.03;r.step();const auto o=safe.update(in);CHECK(!o.restore_layer && o.write);}
        t+=dt90;in.now=t;in.overlap.reset();const auto o=safe.update(in);CHECK(o.restore_layer && !o.unresolved);
        HandFilterKeeper k;FilterKeeperInput f=corpse_hold(1,0xd1);Answers w;k.update(0,f,w);
        f.held_layer.reset();const NativeRigidBody limb=[] {NativeRigidBody b;b.pose=pose_at(0,0,1);b.bounds=NativeGrabBounds{{-.06,-.06,.94},{.06,.06,1.06}};b.bone=7;return b;}();
        Vec3 hand{.08,0,1};   // the hand sphere 0.02 m from the limb box: inside 0.05 + 0.01
        const auto geometry=[&](std::uintptr_t,bool) {return hand_overlap(hand,R1Recipe::radius,&limb,1);};
        for(int i=0;i<10;++i) {
            f.now+=dt90;const auto ov=geometry(0,true);
            CHECK(ov.answered && ov.hit && near(ov.depth,.04,1e-12));CHECK(k.update(0,f,geometry).stripped);
        }
        hand={.2,0,1};CHECK(geometry(0,true).answered && !geometry(0,true).hit);
        f.now+=dt90;CHECK(k.update(0,f,geometry).restored==1);
    }
    {   // the visible hand while holding (X-4, B15): a coupled hold is not a yield, so the precedence body > grasp anchor > clamp holds; the old
        // yield would have drawn the bare controller over the grasp anchor
        HandVisualSource src;VisualInput in{};in.body_ready=true;in.body=pose_at(.05);in.body_seq=1;in.clamp=pose_at(0);in.controller=pose_at(0);in.anchor=Vec3{.2,0,1};
        double now=1;VisualOutput o{};
        for(int i=0;i<30;++i) {now+=dt90;in.now=now;++in.body_seq;o=src.update(0,in);}
        CHECK(o.source==VisualSource::anchor && near(o.drawn.position.x,.2));
        in.body_contact=in.world_hit_near=true;in.body=pose_at(.03);
        for(int i=0;i<30;++i) {now+=dt90;in.now=now;++in.body_seq;o=src.update(0,in);}
        CHECK(o.source==VisualSource::body);
        HandVisualSource old;in.yield=true;for(int i=0;i<30;++i) {now+=dt90;in.now=now;++in.body_seq;o=old.update(0,in);}
        CHECK(o.source==VisualSource::controller);
    }
    {   // HoldCouplingStats: idle is all zero (no hold: uses_body never set), the default word reported
        HoldCouplingStats s;const auto j=s.json();
        CHECK(j.find("\"uses_body\":0")!=std::string::npos && j.find("\"swaps\":0")!=std::string::npos && j.find("\"word\":558564")!=std::string::npos);
        CouplingOutput o;o.uses_body=true;s.note(1,HoldKind::corpse,o);CHECK(s.json().find("\"uses_body\":1")!=std::string::npos);
    }
    std::printf("hold_coupling_check ok\n");
    return 0;
}
