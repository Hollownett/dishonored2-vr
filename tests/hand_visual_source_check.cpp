#include "hand_visual_source.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace d2vr;
#define CHECK(x) do{if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
namespace {
bool near(double a,double b,double tol=1e-9) {return std::fabs(a-b)<=tol;}
WorldTrackedPose pose_at(double x,double y=0,double z=0) {WorldTrackedPose p;p.position={x,y,z};p.basis={Vec3{1,0,0},Vec3{0,1,0},Vec3{0,0,1}};return p;}
bool bit_equal(const WorldTrackedPose& a,const WorldTrackedPose& b) {return std::memcmp(&a,&b,sizeof a)==0;}
constexpr double dt60=1./60;
// One side's rig: a clock and a body serial that advances every frame (a live body publishes once per game frame).
struct Rig {
    HandVisualSource src;double now{100};std::uint64_t seq{1};
    VisualInput in;
    Rig() {in.body_ready=true;in.clamp=pose_at(0);in.controller=pose_at(0);}
    VisualOutput step(unsigned side=0,bool advance_seq=true,double dt=dt60) {now+=dt;if(advance_seq) ++seq;in.now=now;in.body_seq=seq;return src.update(side,in);}
    VisualOutput run(double seconds,unsigned side=0) {VisualOutput o;for(double t=0;t<seconds-1e-9;t+=dt60) o=step(side);return o;}
};
}
int main() {
    {   // Walking and sprinting leave the standing body through the same 150 ms source transition.
        for(double speed:{1.4,2.,3.49,3.5,3.51,4.,7.}) {
            Rig r;r.in.body=pose_at(.015);CHECK(r.run(.5).source==VisualSource::body);
            r.in.player_speed=speed;r.in.clamp=r.in.controller=pose_at(.2);r.in.body=pose_at(.02);
            auto o=r.step();CHECK(o.source==VisualSource::clamp && o.blending && o.changed);
            CHECK(near(o.drawn.position.x,.215) && o.body_weight>0);
            o=r.run(.2);CHECK(o.source==VisualSource::clamp && !o.blending && bit_equal(o.drawn,r.in.clamp));
            // Crossing the former sprint threshold never switches between controller and clamp.
            for(int frame=0;frame<90;++frame) {
                r.in.player_speed=frame%2?3.49:7.;
                r.in.clamp=r.in.controller=pose_at(.2+(frame+1)*speed/90);
                r.in.body=pose_at(r.in.clamp.position.x-.2);
                o=r.step(0,true,frame%2?.033:.023);
                CHECK(o.source==VisualSource::clamp && bit_equal(o.drawn,r.in.clamp) && !o.changed && !o.blending);
            }
            r.in.player_speed=0;r.in.body=r.in.clamp;
            CHECK(r.run(.3).source==VisualSource::body); // stopping retains the existing standing agreement rule
        }
    }
    {   // Contact, clamp displacement, anchor and yield keep their ownership at every locomotion speed.
        for(double speed:{1.4,3.5,7.}) for(int guard=0;guard<6;++guard) {
            Rig r;r.in.player_speed=speed;r.in.controller=r.in.clamp=pose_at(.2);r.in.body=pose_at(.15);
            if(guard==0)r.in.clamp_contact=true;
            if(guard==1){r.in.body_contact=true;r.in.world_hit_near=true;}
            if(guard==2)r.in.clamp=pose_at(.18);
            if(guard==3)r.in.anchor=Vec3{.25,0,0};
            if(guard==4)r.in.yield=true;
            if(guard==5)r.in.body_behind_surface=true;
            const auto o=r.run(.5);
            if(guard<2)CHECK(o.source==VisualSource::body && near(o.drawn.position.x,.15));
            if(guard==2)CHECK(o.source==VisualSource::clamp && bit_equal(o.drawn,r.in.clamp));
            if(guard==3)CHECK(o.source==VisualSource::anchor && near(o.drawn.position.x,.25));
            if(guard==4)CHECK(o.source==VisualSource::controller);
            if(guard==5)CHECK(o.source==VisualSource::clamp);
        }
    }
    {   // moving_free_air_draws_clamp: speed 3, the body agrees with the clamp pose (gap .01) but nothing touches: the clamp output is drawn
        Rig r;r.in.body=pose_at(.01);r.in.player_speed=3;const auto o=r.run(.5);
        CHECK(o.source==VisualSource::clamp && bit_equal(o.drawn,r.in.clamp));
    }
    {   // moving_contact_still_elects_body: the contact rules are unchanged
        Rig r;r.in.body=pose_at(.05);r.in.player_speed=3;r.in.body_contact=true;r.in.world_hit_near=true;
        CHECK(r.run(.5).source==VisualSource::body);
        Rig q;q.in.body=pose_at(.05);q.in.player_speed=3;q.in.clamp_contact=true;CHECK(q.run(.5).source==VisualSource::body);
    }
    {   // standing_agreement_unchanged: speed 0 (and below the 1.0 entry) elects the agreeing body as before
        Rig r;r.in.body=pose_at(.01);r.in.player_speed=0;CHECK(r.run(.5).source==VisualSource::body);
        Rig q;q.in.body=pose_at(.01);q.in.player_speed=.9;CHECK(q.run(.5).source==VisualSource::body);
    }
    {   // moving_hysteresis_no_chatter: .8 <-> 1.1 m/s every 0.1 s for 3 s: entering at 1.0 and leaving at 0.6 means one change at most
        Rig r;r.in.body=pose_at(.01);int changes=0;
        for(int i=0;i<180;++i) {r.in.player_speed=(i/6)%2?1.1:.8;changes+=r.step().changed?1:0;}
        CHECK(changes<=1);
        Rig q;q.in.body=pose_at(.01);q.in.player_speed=3;q.run(.3);   // and a moving hand returns to the agreeing body only below 0.6 m/s
        q.in.player_speed=.7;CHECK(q.run(.3).source==VisualSource::clamp);
        q.in.player_speed=.5;CHECK(q.run(.3).source==VisualSource::body);
    }
    {   // body_wins_when_sane: in contact, near the clamp pose and the controller
        Rig r;r.in.body=pose_at(.01);r.in.clamp=pose_at(0);r.in.controller=pose_at(0);r.in.body_contact=true;r.in.world_hit_near=true;
        const auto o=r.run(.5);
        CHECK(o.source==VisualSource::body && near(o.drawn.position.x,.01) && !o.snapped);
        CHECK(near(o.drawn.basis[0].x,1) && near(o.drawn.basis[2].z,1));   // the orientation stays the clamp's
    }
    {   // the gap rule: no contact, the body within 0.02 m of the clamp pose is drawn; 0.03 m is not
        Rig r;r.in.body=pose_at(.015);auto o=r.run(.5);CHECK(o.source==VisualSource::body);
        Rig q;q.in.body=pose_at(.03);o=q.run(.5);CHECK(o.source==VisualSource::clamp && bit_equal(o.drawn,q.in.clamp));
    }
    {   // fallback_to_clamp_without_body: no body, an unready body, a vanished body: the clamp pose, bit-exact, every frame, even yielding-free
        Rig r;r.in.clamp=pose_at(.123456789,-.5,.25);r.in.controller=pose_at(1);
        r.in.body_ready=false;r.in.body=std::nullopt;
        for(int i=0;i<200;++i) {const auto o=r.step();CHECK(o.source==VisualSource::clamp && bit_equal(o.drawn,r.in.clamp) && !o.blending && !o.changed);}
        r.in.body_ready=false;r.in.body=pose_at(0);r.in.body_contact=true;r.in.world_hit_near=true;
        for(int i=0;i<10;++i) {const auto o=r.step();CHECK(bit_equal(o.drawn,r.in.clamp) && o.source==VisualSource::clamp);}
        r.in.body_ready=true;r.in.body=std::nullopt;r.in.yield=true;   // no pose: nothing to yield from the body's side, the phase-1 pose stays
        for(int i=0;i<10;++i) {const auto o=r.step();CHECK(bit_equal(o.drawn,r.in.clamp));}
        // a body that existed and went away: back to the clamp, exact after the blend
        Rig q;q.in.body=pose_at(0);q.in.body_contact=true;q.in.world_hit_near=true;q.run(.5);
        q.in.body=std::nullopt;q.in.clamp=pose_at(.2);auto o=q.run(.2);CHECK(bit_equal(o.drawn,q.in.clamp) && o.source==VisualSource::clamp);
    }
    {   // yield_gives_controller: native animation owns the arm; and with no body the phase-1 pose is untouched
        Rig r;r.in.body=pose_at(0);r.in.body_contact=true;r.in.world_hit_near=true;r.in.clamp=pose_at(0);r.in.controller=pose_at(.2);r.in.yield=true;
        const auto o=r.run(.5);CHECK(o.source==VisualSource::controller && near(o.drawn.position.x,.2));
        Rig q;q.in.body=std::nullopt;q.in.body_ready=false;q.in.yield=true;q.in.controller=pose_at(.2);q.in.clamp=pose_at(.1);
        const auto p=q.run(.5);CHECK(bit_equal(p.drawn,q.in.clamp));
    }
    {   // blend_over_150ms: contact starts; the drawn pose goes from the clamp to the body over 0.15 s, linearly, exactly at the end
        Rig r;r.in.body=pose_at(.08);r.in.clamp=pose_at(0);r.in.controller=pose_at(0);
        r.run(.5);CHECK(r.step().source==VisualSource::clamp);
        r.in.body_contact=true;r.in.world_hit_near=true;
        auto o=r.step();CHECK(o.changed && o.source==VisualSource::body && near(o.drawn.position.x,0,1e-9) && o.blending);   // the change frame starts at the old pose
        double t=0;
        while(t<.075-1e-9) {o=r.step();t+=dt60;}
        CHECK(near(o.drawn.position.x,.04,.008));
        while(t<.15-1e-9) {o=r.step();t+=dt60;}
        CHECK(near(o.drawn.position.x,.08,1e-9) && !o.blending);
        o=r.step();CHECK(near(o.drawn.position.x,.08) && !o.changed);
        CHECK(HandVisualSource::blend_seconds==.15);
        // and back: every source change blends
        r.in.body_contact=false;r.in.world_hit_near=false;o=r.step();CHECK(o.changed && o.blending && o.source==VisualSource::clamp);
        o=r.run(.16);CHECK(near(o.drawn.position.x,0) && !o.blending);
    }
    {   // far_body_snaps_after_quarter_second: 0.34 m from the controller for 5 s never; 0.36 m snaps between 0.24 and 0.26 s
        Rig r;r.in.controller=pose_at(0);r.in.body=pose_at(.34);r.in.clamp=pose_at(.34);r.in.body_contact=true;r.in.world_hit_near=true;
        for(int i=0;i<300;++i) {const auto o=r.step();CHECK(!o.snapped && o.source==VisualSource::body);}
        Rig q;q.in.controller=pose_at(0);q.in.body=pose_at(.36);q.in.clamp=pose_at(.36);q.in.body_contact=true;q.in.world_hit_near=true;
        double t=0;bool snapped=false;double at=-1;
        q.step();   // the first frame has no dt
        for(int i=0;i<60 && !snapped;++i) {const auto o=q.step();t+=dt60;if(o.snapped) {snapped=true;at=t;CHECK(o.source==VisualSource::clamp);}}
        CHECK(snapped && at>.24-1e-9 && at<.26+1e-9);
        CHECK(HandVisualSource::snap_gap==.35 && HandVisualSource::snap_seconds==.25);
        // it stays distrusted while far, and is trusted again once the body is back within 0.30 m
        for(int i=0;i<30;++i) CHECK(q.step().source==VisualSource::clamp);
        q.in.body=pose_at(.2);q.in.clamp=pose_at(.2);q.run(.3);
        CHECK(q.step().source==VisualSource::body);
    }
    {   // behind_surface_distrusted: the clamp's tunnelling guard
        Rig r;r.in.body=pose_at(.01);r.in.body_contact=true;r.in.world_hit_near=true;r.run(.3);
        CHECK(r.step().source==VisualSource::body);
        r.in.body_behind_surface=true;auto o=r.run(.3);CHECK(o.source==VisualSource::clamp && o.snapped && bit_equal(o.drawn,r.in.clamp));
        r.in.body_behind_surface=false;o=r.run(.3);CHECK(o.source==VisualSource::body);
        // the plane test itself
        const ClampHit wall{Vec3{.5,0,0},Vec3{-1,0,0}};
        CHECK(behind_held_planes({.6,0,0},&wall,1) && !behind_held_planes({.4,0,0},&wall,1) && !behind_held_planes({.505,0,0},&wall,1));
        CHECK(!behind_held_planes({std::nan(""),0,0},&wall,1) && !behind_held_planes({.6,0,0},&wall,0));
    }
    {   // non_finite_body_ignored
        Rig r;r.in.body=pose_at(.01);r.in.body_contact=true;r.in.world_hit_near=true;r.run(.3);
        r.in.body=pose_at(std::nan(""));auto o=r.run(.3);CHECK(o.source==VisualSource::clamp && bit_equal(o.drawn,r.in.clamp));
        r.in.body=pose_at(std::numeric_limits<double>::infinity());o=r.run(.3);CHECK(bit_equal(o.drawn,r.in.clamp));
        r.in.body=pose_at(.01);r.in.clamp=pose_at(std::nan(""));o=r.step();CHECK(o.source==VisualSource::clamp);   // a bad clamp pose: reset, the input comes back
        r.in.clamp=pose_at(0);o=r.run(.3);CHECK(o.source==VisualSource::body && std::isfinite(o.drawn.position.x));
    }
    {   // sides_independent
        HandVisualSource src;VisualInput a,b;a.body_ready=b.body_ready=true;a.body=pose_at(.01);b.body=pose_at(.5);
        a.clamp=b.clamp=pose_at(0);a.controller=b.controller=pose_at(0);a.body_contact=true;
        double now=10;std::uint64_t seq=1;VisualOutput oa,ob;
        for(int i=0;i<60;++i) {now+=dt60;++seq;a.now=b.now=now;a.body_seq=b.body_seq=seq;oa=src.update(0,a);ob=src.update(1,b);}
        CHECK(oa.source==VisualSource::body && ob.source==VisualSource::clamp && bit_equal(ob.drawn,b.clamp));
        src.reset(0);a.body_contact=false;a.body=pose_at(.5);oa=src.update(0,a);CHECK(oa.source==VisualSource::clamp);
        CHECK(src.update(2,a).source==VisualSource::clamp);   // an out-of-range side is the clamp pose
    }
    {   // no_flicker_on_threshold_noise: the gap alternates 0.349 / 0.351 each frame for 2 s: at most one source change
        Rig r;r.in.body_contact=true;r.in.world_hit_near=true;r.in.controller=pose_at(0);int changes=0;VisualSource last=VisualSource::clamp;bool first=true;
        for(int i=0;i<120;++i) {const double g=(i&1)?.351:.349;r.in.body=pose_at(g);r.in.clamp=pose_at(g);const auto o=r.step();
            if(!first && o.source!=last) ++changes;first=false;last=o.source;}
        CHECK(changes<=1);
    }
    {   // free_air_fast_sweep_draws_clamp: a 6 m/s sweep, no contact, the body lagging 0.6 m: the clamp pose is drawn every frame (no trailing)
        Rig r;double x=0;double worst=0;int body_frames=0;
        for(int i=0;i<180;++i) {   // 2 s at 90 Hz
            x+=6./90;r.in.controller=pose_at(x);r.in.clamp=pose_at(x);r.in.body=pose_at(x-.6<0?0:x-.6);r.in.body_contact=false;r.in.world_hit_near=false;r.in.clamp_contact=false;
            const auto o=r.step(0,true,1./90);worst=std::max(worst,length(o.drawn.position-r.in.clamp.position));if(o.source==VisualSource::body) ++body_frames;
            CHECK(bit_equal(o.drawn,r.in.clamp));
        }
        CHECK(worst==0 && body_frames==0);
        // the sweep ends and the body catches up: the clamp pose all the way, then the body once it agrees, blended
        r.in.body=pose_at(x);const auto o=r.run(.5);CHECK(o.source==VisualSource::body && near(o.drawn.position.x,x));
    }
    {   // contact_draws_body: pressing into a wall (clamp at the wall, the controller 0.1 m through it), the body stopped 0.05 m short of the clamp
        Rig r;r.in.controller=pose_at(.5);r.in.clamp=pose_at(.4);r.in.body=pose_at(.35);
        r.run(.5);CHECK(r.step().source==VisualSource::clamp);   // before the contact signal the clamp pose is drawn
        r.in.body_contact=true;r.in.world_hit_near=true;VisualOutput o;double t=0;
        for(;t<=.15+1e-9;t+=dt60) o=r.step();   // the change frame is t=0: 0.15 s later the blend is done
        CHECK(o.source==VisualSource::body && near(o.drawn.position.x,.35,1e-9));
        // the clamp's own contact is enough as well
        Rig q;q.in.controller=pose_at(.5);q.in.clamp=pose_at(.4);q.in.body=pose_at(.35);q.in.clamp_contact=true;
        o=q.run(.2);CHECK(o.source==VisualSource::body && near(o.drawn.position.x,.35,1e-9));
    }
    {   // stale body pose: unchanged for more than two displayed frames is not sane; two frames still are; a fresh pose recovers
        Rig r;r.in.body=pose_at(.01);r.in.body_contact=true;r.in.world_hit_near=true;r.run(.3);
        auto o=r.step(0,false);CHECK(o.source==VisualSource::body);   // unchanged 1
        o=r.step(0,false);CHECK(o.source==VisualSource::body);        // unchanged 2
        o=r.step(0,false);CHECK(o.source==VisualSource::clamp);       // unchanged 3: stale
        o=r.run(.3,0);CHECK(o.source==VisualSource::body);            // advancing again
        Rig q;q.in.body=pose_at(.01);q.in.body_contact=true;q.in.world_hit_near=true;q.run(.3);for(int i=0;i<10;++i) q.step(0,false);
        o=q.step(0,false);CHECK(o.source==VisualSource::clamp);
        o=q.run(.2,0);CHECK(o.source==VisualSource::body);
    }
    {   // anchor_transition_blends_from_body / _to_body / clamp->anchor: the anchor is a source; no source change ever steps the hand
        const double limit=.3*dt60/HandVisualSource::blend_seconds+1e-6;   // the blend-limited per-frame step for a 0.25 m offset
        Rig r;r.in.body=pose_at(.05);r.in.clamp=pose_at(0);r.in.controller=pose_at(0);r.in.anchor=Vec3{.3,0,0};
        r.in.body_contact=true;r.in.world_hit_near=true;r.run(.5);
        auto o=r.step();CHECK(o.source==VisualSource::body && near(o.drawn.position.x,.05));   // the body outranks the anchor
        r.in.body_contact=false;r.in.world_hit_near=false;   // contact releases: body -> anchor
        double prev=o.drawn.position.x;bool changed=false;
        for(int i=0;i<20;++i) {o=r.step();CHECK(std::fabs(o.drawn.position.x-prev)<=limit);prev=o.drawn.position.x;changed=changed || o.changed;if(i==0) CHECK(near(o.drawn.position.x,.05));}
        CHECK(changed && o.source==VisualSource::anchor && near(o.drawn.position.x,.3) && !o.blending);
        r.in.body_contact=true;r.in.world_hit_near=true;   // anchor -> body
        for(int i=0;i<20;++i) {o=r.step();CHECK(std::fabs(o.drawn.position.x-prev)<=limit);prev=o.drawn.position.x;}
        CHECK(o.source==VisualSource::body && near(o.drawn.position.x,.05));
        // clamp -> anchor (a hold begins with the body not elected)
        Rig q;q.in.body=pose_at(.5);q.in.clamp=pose_at(0);q.in.controller=pose_at(0);q.run(.5);
        q.in.anchor=Vec3{.3,0,0};prev=0;
        for(int i=0;i<20;++i) {o=q.step();CHECK(std::fabs(o.drawn.position.x-prev)<=limit);prev=o.drawn.position.x;}
        CHECK(o.source==VisualSource::anchor && near(o.drawn.position.x,.3));
        // no body in play: the anchor override is phase 1 exactly (no blend, bit-exact), and the knob-off path applies it the same way
        Rig n;n.in.body_ready=false;n.in.body=std::nullopt;n.in.anchor=Vec3{.3,.1,0};o=n.step();
        WorldTrackedPose want=n.in.clamp;want.position=*n.in.anchor;CHECK(bit_equal(o.drawn,want));
        VisualOutput passthrough;passthrough.drawn=n.in.clamp;DrawnAnchor::Result ar;ar.weight=.5;ar.position={.3,.1,0};
        CHECK(bit_equal(resolve_drawn_grip(passthrough,ar).pose,want));
    }
    {   // contact needs a world hit near the body: a shortfall alone (time dilation, a capsule push) is no contact; the clamp's own contact is
        Rig r;r.in.body=pose_at(.05);r.in.body_contact=true;r.in.world_hit_near=false;
        auto o=r.run(.5);CHECK(o.source==VisualSource::clamp && bit_equal(o.drawn,r.in.clamp));
        r.in.world_hit_near=true;o=r.run(.5);CHECK(o.source==VisualSource::body);
        Rig q;q.in.body=pose_at(.05);q.in.clamp_contact=true;o=q.run(.5);CHECK(o.source==VisualSource::body);
        // the world-hit test itself: a held plane within 8 cm of the body, either side
        const ClampHit wall{Vec3{.5,0,0},Vec3{-1,0,0}};
        CHECK(world_hit_near_body({.45,0,0},&wall,1) && world_hit_near_body({.55,0,0},&wall,1) && !world_hit_near_body({.3,0,0},&wall,1) && !world_hit_near_body({.45,0,0},&wall,0));
    }
    {   // the contact gap bound: contact elects the body only within 10 cm of the clamp pose (leaves at 12 cm)
        Rig r;r.in.clamp_contact=true;r.in.body=pose_at(.11);auto o=r.run(.5);CHECK(o.source==VisualSource::clamp);
        r.in.body=pose_at(.09);o=r.run(.3);CHECK(o.source==VisualSource::body);
        r.in.body=pose_at(.115);o=r.run(.3);CHECK(o.source==VisualSource::body);    // hysteresis: stays until 12 cm
        r.in.body=pose_at(.125);o=r.run(.3);CHECK(o.source==VisualSource::clamp);
        r.in.body=pose_at(.105);o=r.run(.3);CHECK(o.source==VisualSource::clamp);   // and does not come back until 10 cm
    }
    {   // grazing_fast_sweep_never_trails_more_than_10cm: a sweep grazes a wall (clamp_contact true all the way) while the body lags more and more
        Rig r;double x=0,worst=0,worst_body_gap=0;
        for(int i=0;i<180;++i) {
            x+=6./90;const double lag=std::min(.65,i*.004);
            r.in.controller=pose_at(x);r.in.clamp=pose_at(x);r.in.body=pose_at(x-lag);r.in.clamp_contact=true;r.in.body_contact=true;r.in.world_hit_near=true;
            const auto o=r.step(0,true,1./90);worst=std::max(worst,length(o.drawn.position-r.in.clamp.position));
            if(o.source==VisualSource::body) worst_body_gap=std::max(worst_body_gap,o.gap_m);
        }
        CHECK(worst_body_gap<=HandVisualSource::contact_leave+1e-9);   // the body is drawn only inside the hysteresis band
        CHECK(worst<=HandVisualSource::contact_leave+1e-9);            // and the hand never trails the clamp pose by more than that, blends included
        const auto o=r.step(0,true,1./90);CHECK(o.source==VisualSource::clamp);
    }
    {   // stale in time: 25 ms floor, and a slow pose cadence (30 Hz) is not stale at 90 Hz display
        Rig r;r.in.body=pose_at(.01);r.in.body_contact=true;r.in.world_hit_near=true;
        VisualOutput o;for(int i=0;i<60;++i) {if(i%3==0) ++r.seq;r.now+=1./90;r.in.now=r.now;r.in.body_seq=r.seq;o=r.src.update(0,r.in);}
        CHECK(o.source==VisualSource::body);
        for(int i=0;i<2;++i) {r.now+=1./90;r.in.now=r.now;o=r.src.update(0,r.in);}   // 2 display frames without a new pose: still fresh
        CHECK(o.source==VisualSource::body);
        for(int i=0;i<3;++i) {r.now+=1./90;r.in.now=r.now;o=r.src.update(0,r.in);}
        CHECK(o.source==VisualSource::clamp);
    }
    {   // X-4: the hand body > the grasp anchor > the clamp output; a yielding hand is the controller
        VisualOutput v;v.drawn=pose_at(1);DrawnAnchor::Result a;a.weight=1;a.position={2,0,0};
        v.source=VisualSource::body;auto g=resolve_drawn_grip(v,a);CHECK(near(g.pose.position.x,1) && !g.anchored);
        CHECK(HandVisualSource::pick(false,true,true)==VisualSource::body && HandVisualSource::pick(false,false,true)==VisualSource::anchor && HandVisualSource::pick(false,false,false)==VisualSource::clamp && HandVisualSource::pick(true,true,true)==VisualSource::controller);
        v.source=VisualSource::clamp;g=resolve_drawn_grip(v,a);CHECK(near(g.pose.position.x,2) && g.anchored);
        a.weight=0;g=resolve_drawn_grip(v,a);CHECK(bit_equal(g.pose,v.drawn) && !g.anchored);
        a.weight=1;v.source=VisualSource::controller;g=resolve_drawn_grip(v,a);CHECK(near(g.pose.position.x,1) && !g.anchored);
    }
    {   // corpse_hold_keeps_limb_seating_in_free_air (final review P2-2, X-4 amended): a motor or corpse hold, the clamp yielding (its pose is the
        // palm), the body driven onto the palm and agreeing within 0.02 m: the grasp anchor (Task 12's seated limb) is drawn, not the body, and the
        // hand never flips between the two as the body's lag crosses 0.02-0.03 m; contact still elects the body
        Rig r;r.in.clamp=pose_at(0);r.in.controller=pose_at(0);r.in.anchor=Vec3{.25,0,0};r.in.body=pose_at(.005);
        VisualOutput o;unsigned changes=0;
        for(int i=0;i<120;++i) {r.in.body=pose_at(i%20<10?.005:.035);o=r.step();if(i>0 && o.changed) ++changes;
            CHECK(o.source==VisualSource::anchor && near(o.drawn.position.x,.25) && o.body_weight==0);}
        CHECK(changes==0);
        // the bare hand (no anchor) keeps the agreement rule
        Rig bare;bare.in.body=pose_at(.005);o=bare.run(.3);CHECK(o.source==VisualSource::body);
        // contact (in reach of the clamp pose) still outranks the anchor
        r.in.body=pose_at(.05);r.in.body_contact=true;r.in.world_hit_near=true;o=r.run(.3);CHECK(o.source==VisualSource::body && near(o.drawn.position.x,.05));
    }
    {   // body_leaving_blends (final review P3-3): the body drawn in contact 0.11 m from the clamp pose disappears (a menu press, a fault latch, a
        // destroy): the hand eases to the clamp pose over the blend, never a 0.11 m pop, and is exact afterwards
        Rig r;r.in.body=pose_at(.09);r.in.body_contact=true;r.in.world_hit_near=true;r.run(.5);
        r.in.body=pose_at(.11);auto o=r.run(.3);CHECK(o.source==VisualSource::body && near(o.drawn.position.x,.11));
        r.in.body_ready=false;r.in.body=std::nullopt;
        const double limit=.11*dt60/HandVisualSource::blend_seconds+1e-6;double prev=o.drawn.position.x;bool blended=false;
        for(int i=0;i<12;++i) {o=r.step();CHECK(std::fabs(o.drawn.position.x-prev)<=limit);prev=o.drawn.position.x;blended=blended || o.blending;}
        CHECK(blended && o.source==VisualSource::clamp && bit_equal(o.drawn,r.in.clamp) && !o.blending);
        // the same with an anchor taking over (a hold), and a body that never was the source leaves nothing to blend
        Rig a;a.in.body=pose_at(.09);a.in.body_contact=true;a.in.world_hit_near=true;a.in.anchor=Vec3{.3,0,0};a.run(.5);
        a.in.body_ready=false;a.in.body=std::nullopt;prev=.09;
        for(int i=0;i<12;++i) {o=a.step();CHECK(std::fabs(o.drawn.position.x-prev)<=.21*dt60/HandVisualSource::blend_seconds+1e-6);prev=o.drawn.position.x;}
        CHECK(o.source==VisualSource::anchor && near(o.drawn.position.x,.3) && !o.blending);
        Rig n;n.in.body=pose_at(.5);n.run(.3);n.in.body_ready=false;n.in.body=std::nullopt;o=n.step();CHECK(bit_equal(o.drawn,n.in.clamp) && !o.blending);
    }
    {   // counters
        HandVisualStats st;VisualOutput v;v.source=VisualSource::body;v.changed=true;st.note(0,v);v.source=VisualSource::clamp;v.changed=false;v.snapped=true;st.note(0,v);st.note(1,v);st.note(5,v);
        const auto j=st.json();
        CHECK(j.find("\"body_frames\":1,\"clamp_frames\":1,\"controller_frames\":0,\"anchor_frames\":0,\"blends\":1,\"snaps\":1")!=std::string::npos);
        CHECK(j.find("\"body_frames\":0,\"clamp_frames\":1")!=std::string::npos);
    }
    std::puts("hand_visual_source_check ok");
}
