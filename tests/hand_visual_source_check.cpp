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
    {   // body_wins_when_sane: in contact, near the clamp pose and the controller
        Rig r;r.in.body=pose_at(.01);r.in.clamp=pose_at(0);r.in.controller=pose_at(0);r.in.body_contact=true;
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
        r.in.body_ready=false;r.in.body=pose_at(0);r.in.body_contact=true;
        for(int i=0;i<10;++i) {const auto o=r.step();CHECK(bit_equal(o.drawn,r.in.clamp) && o.source==VisualSource::clamp);}
        r.in.body_ready=true;r.in.body=std::nullopt;r.in.yield=true;   // no pose: nothing to yield from the body's side, the phase-1 pose stays
        for(int i=0;i<10;++i) {const auto o=r.step();CHECK(bit_equal(o.drawn,r.in.clamp));}
        // a body that existed and went away: back to the clamp, exact after the blend
        Rig q;q.in.body=pose_at(0);q.in.body_contact=true;q.run(.5);
        q.in.body=std::nullopt;q.in.clamp=pose_at(.2);auto o=q.run(.2);CHECK(bit_equal(o.drawn,q.in.clamp) && o.source==VisualSource::clamp);
    }
    {   // yield_gives_controller: native animation owns the arm; and with no body the phase-1 pose is untouched
        Rig r;r.in.body=pose_at(0);r.in.body_contact=true;r.in.clamp=pose_at(0);r.in.controller=pose_at(.2);r.in.yield=true;
        const auto o=r.run(.5);CHECK(o.source==VisualSource::controller && near(o.drawn.position.x,.2));
        Rig q;q.in.body=std::nullopt;q.in.body_ready=false;q.in.yield=true;q.in.controller=pose_at(.2);q.in.clamp=pose_at(.1);
        const auto p=q.run(.5);CHECK(bit_equal(p.drawn,q.in.clamp));
    }
    {   // blend_over_150ms: contact starts; the drawn pose goes from the clamp to the body over 0.15 s, linearly, exactly at the end
        Rig r;r.in.body=pose_at(.1);r.in.clamp=pose_at(0);r.in.controller=pose_at(0);
        r.run(.5);CHECK(r.step().source==VisualSource::clamp);
        r.in.body_contact=true;
        auto o=r.step();CHECK(o.changed && o.source==VisualSource::body && near(o.drawn.position.x,0,1e-9) && o.blending);   // the change frame starts at the old pose
        double t=0;
        while(t<.075-1e-9) {o=r.step();t+=dt60;}
        CHECK(near(o.drawn.position.x,.05,.008));
        while(t<.15-1e-9) {o=r.step();t+=dt60;}
        CHECK(near(o.drawn.position.x,.1,1e-9) && !o.blending);
        o=r.step();CHECK(near(o.drawn.position.x,.1) && !o.changed);
        CHECK(HandVisualSource::blend_seconds==.15);
        // and back: every source change blends
        r.in.body_contact=false;o=r.step();CHECK(o.changed && o.blending && o.source==VisualSource::clamp);
        o=r.run(.16);CHECK(near(o.drawn.position.x,0) && !o.blending);
    }
    {   // far_body_snaps_after_quarter_second: 0.34 m from the controller for 5 s never; 0.36 m snaps between 0.24 and 0.26 s
        Rig r;r.in.controller=pose_at(0);r.in.body=pose_at(.34);r.in.clamp=pose_at(.34);r.in.body_contact=true;
        for(int i=0;i<300;++i) {const auto o=r.step();CHECK(!o.snapped && o.source==VisualSource::body);}
        Rig q;q.in.controller=pose_at(0);q.in.body=pose_at(.36);q.in.clamp=pose_at(.36);q.in.body_contact=true;
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
        Rig r;r.in.body=pose_at(.01);r.in.body_contact=true;r.run(.3);
        CHECK(r.step().source==VisualSource::body);
        r.in.body_behind_surface=true;auto o=r.run(.3);CHECK(o.source==VisualSource::clamp && o.snapped && bit_equal(o.drawn,r.in.clamp));
        r.in.body_behind_surface=false;o=r.run(.3);CHECK(o.source==VisualSource::body);
        // the plane test itself
        const ClampHit wall{Vec3{.5,0,0},Vec3{-1,0,0}};
        CHECK(behind_held_planes({.6,0,0},&wall,1) && !behind_held_planes({.4,0,0},&wall,1) && !behind_held_planes({.505,0,0},&wall,1));
        CHECK(!behind_held_planes({std::nan(""),0,0},&wall,1) && !behind_held_planes({.6,0,0},&wall,0));
    }
    {   // non_finite_body_ignored
        Rig r;r.in.body=pose_at(.01);r.in.body_contact=true;r.run(.3);
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
        Rig r;r.in.body_contact=true;r.in.controller=pose_at(0);int changes=0;VisualSource last=VisualSource::clamp;bool first=true;
        for(int i=0;i<120;++i) {const double g=(i&1)?.351:.349;r.in.body=pose_at(g);r.in.clamp=pose_at(g);const auto o=r.step();
            if(!first && o.source!=last) ++changes;first=false;last=o.source;}
        CHECK(changes<=1);
    }
    {   // free_air_fast_sweep_draws_clamp: a 6 m/s sweep, no contact, the body lagging 0.6 m: the clamp pose is drawn every frame (no trailing)
        Rig r;double x=0;double worst=0;int body_frames=0;
        for(int i=0;i<180;++i) {   // 2 s at 90 Hz
            x+=6./90;r.in.controller=pose_at(x);r.in.clamp=pose_at(x);r.in.body=pose_at(x-.6<0?0:x-.6);r.in.body_contact=false;r.in.clamp_contact=false;
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
        r.in.body_contact=true;VisualOutput o;double t=0;
        for(;t<=.15+1e-9;t+=dt60) o=r.step();   // the change frame is t=0: 0.15 s later the blend is done
        CHECK(o.source==VisualSource::body && near(o.drawn.position.x,.35,1e-9));
        // the clamp's own contact is enough as well
        Rig q;q.in.controller=pose_at(.5);q.in.clamp=pose_at(.4);q.in.body=pose_at(.35);q.in.clamp_contact=true;
        o=q.run(.2);CHECK(o.source==VisualSource::body && near(o.drawn.position.x,.35,1e-9));
    }
    {   // stale body pose: unchanged for more than two displayed frames is not sane; two frames still are; a fresh pose recovers
        Rig r;r.in.body=pose_at(.01);r.in.body_contact=true;r.run(.3);
        auto o=r.step(0,false);CHECK(o.source==VisualSource::body);   // unchanged 1
        o=r.step(0,false);CHECK(o.source==VisualSource::body);        // unchanged 2
        o=r.step(0,false);CHECK(o.source==VisualSource::clamp);       // unchanged 3: stale
        o=r.run(.3,0);CHECK(o.source==VisualSource::body);            // advancing again
        Rig q;q.in.body=pose_at(.01);q.in.body_contact=true;q.run(.3);for(int i=0;i<10;++i) q.step(0,false);
        o=q.step(0,false);CHECK(o.source==VisualSource::clamp);
        o=q.run(.2,0);CHECK(o.source==VisualSource::body);
    }
    {   // X-4: the hand body > the grasp anchor > the clamp output; a yielding hand is the controller
        VisualOutput v;v.drawn=pose_at(1);DrawnAnchor::Result a;a.weight=1;a.position={2,0,0};
        v.source=VisualSource::body;auto g=resolve_drawn_grip(v,a);CHECK(near(g.pose.position.x,1) && !g.anchored);
        v.source=VisualSource::clamp;g=resolve_drawn_grip(v,a);CHECK(near(g.pose.position.x,2) && g.anchored);
        a.weight=0;g=resolve_drawn_grip(v,a);CHECK(bit_equal(g.pose,v.drawn) && !g.anchored);
        a.weight=1;v.source=VisualSource::controller;g=resolve_drawn_grip(v,a);CHECK(near(g.pose.position.x,1) && !g.anchored);
    }
    {   // counters
        HandVisualStats st;VisualOutput v;v.source=VisualSource::body;v.changed=true;st.note(0,v);v.source=VisualSource::clamp;v.changed=false;v.snapped=true;st.note(0,v);st.note(1,v);st.note(5,v);
        const auto j=st.json();
        CHECK(j.find("\"body_frames\":1,\"clamp_frames\":1,\"controller_frames\":0,\"blends\":1,\"snaps\":1")!=std::string::npos);
        CHECK(j.find("\"body_frames\":0,\"clamp_frames\":1")!=std::string::npos);
    }
    std::puts("hand_visual_source_check ok");
}
