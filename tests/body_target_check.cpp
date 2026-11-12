// v1250: the downed-body prompt target (src/body_target.hpp): pointing geometry, one stable body target, hand sliding / probe gaps / tremor /
// a prop next to the body / two bodies side by side / leaving. build\check-body-target.cmd
#include "body_target.hpp"
#include <cstdio>
#include <cstdlib>
#include <vector>
#define CHECK(x) do {if(!(x)) {std::printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#x);std::exit(1);}} while(0)
using namespace d2vr;using namespace d2vr::body_target;
namespace {
// A body lying along x at z=-depth on the floor (y=0): parts every 0.25 m, radius 0.1. XR-style axes (y up).
std::vector<Part> lying(double x0,double depth) {std::vector<Part> p;for(int i=0;i<7;++i) p.push_back({{x0+.25*i,.1,-depth},.1});return p;}
Obs point_obs(std::uintptr_t body,const std::vector<Part>& parts,Vec3 origin,Vec3 dir,int side=1) {
    Obs o;o.body=body;const auto f=ray_fit(origin,dir,parts.data(),parts.size(),reach_leave_m);
    if(f.ok) {o.point_gap=f.gap;o.along=f.along;o.point_side=side;o.point_part=f.part;o.los=Los::clear;
        o.anchor={parts[static_cast<std::size_t>(f.part)].at.x,prompt_up_m,parts[static_cast<std::size_t>(f.part)].at.z};o.anchor_valid=true;}
    return o;
}
Obs palm_obs(std::uintptr_t body,const std::vector<Part>& parts,Vec3 palm,int side=0) {
    Obs o;o.body=body;int part=-1;o.palm=palm_gap(palm,parts.data(),parts.size(),&part);o.palm_side=side;o.palm_part=part;
    if(part>=0) {o.anchor={parts[static_cast<std::size_t>(part)].at.x,prompt_up_m,parts[static_cast<std::size_t>(part)].at.z};o.anchor_valid=true;}
    return o;
}
}
int main() {
    const Vec3 hand{0,1.2,0};
    {   // ray vs parts: hit, tolerance, reach limit, behind the hand, nearest part
        const auto body=lying(-.75,2.0);
        const auto f=ray_fit(hand,Vec3{0,.1,-2.0}-hand,body.data(),body.size(),reach_m);
        CHECK(f.ok && f.part==3 && f.gap<0 && f.along>2 && f.along<2.5);
        const auto off=ray_fit(hand,Vec3{0,.1,-2.0}+Vec3{0,0,0}-hand+Vec3{0,.6,0},body.data(),body.size(),reach_m);   // aimed 0.6 m too high at 2 m
        CHECK(off.ok && off.gap>point_tolerance_m);
        const auto far=lying(-.75,3.5);
        CHECK(!ray_fit(hand,Vec3{0,.1,-3.5}-hand,far.data(),far.size(),reach_m).ok);       // 3.6 m along: beyond reach
        CHECK(!ray_fit(hand,Vec3{0,0,1},body.data(),body.size(),reach_m).ok);             // pointing away: behind
        int part=-1;CHECK(palm_gap(Vec3{0,.25,-2.0},body.data(),body.size(),&part)<.1 && part==3);
    }
    {   // nearest body along the ray wins
        const auto a=lying(-.75,1.5),b=lying(-.75,2.5);const Vec3 dir=Vec3{0,.1,-1.5}-hand;
        Obs o[2]{point_obs(0xA,a,hand,dir),point_obs(0xB,b,hand,dir)};
        o[1].point_gap=0;o[1].along=2.6;   // b also on the ray, farther
        Tracker t;t.step(1000,o,2);CHECK(t.body==0xA && t.source==Source::point);
        o[0].los=Los::blocked;Tracker u;u.step(1000,o,2);CHECK(u.body==0xB);           // a wall in front of the near one
        o[0].los=Los::unknown;Tracker w;w.step(1000,o,2);CHECK(w.body==0xB);           // no LOS answer yet: not entered by pointing
    }
    {   // a hand sliding along the body (part switches every frame), 200 ms probe gaps, tremor across the enter gate: one continuous target
        const auto body=lying(-.75,.6);Tracker t;std::uint64_t now=1000;unsigned held=0,frames=0;Vec3 last{};bool have_last=false;double max_step=0;
        for(int i=0;i<300;++i,now+=11) {
            const int k=i%150;const double x=-.75+1.5*(k<75?k:150-k)/75.;   // slides back and forth along the body
            const double tremor=(i%7<3)?.22:.33;   // across the 0.25 enter gate, inside the 0.40 leave gate
            const bool gap=(i%60)>=42;   // 18 frames ~200 ms without any observation of the body
            Obs o=palm_obs(0xB0D7,body,Vec3{x,.1+.1+tremor,-.6});
            t.step(now,gap?nullptr:&o,gap?0:1);++frames;held+=t.body==0xB0D7;
            if(t.anchored) {if(have_last) max_step=std::max(max_step,length(t.anchor-last));last=t.anchor;have_last=true;}
        }
        CHECK(held==frames && t.drops[0]+t.drops[1]+t.drops[2]==0 && t.part_switches>0);
        CHECK(max_step<.06);   // the anchor eases as the nearest part changes; it never jumps a part spacing (0.25 m) in a frame
    }
    {   // a prop lying 15 cm from the body: the game's prompt for it hides the kept body art only while the game draws it
        CHECK(!other_prompt_drawn(false,0x9999,false,0xB0D7,false));
        CHECK(other_prompt_drawn(true,0x9999,false,0xB0D7,false));
        CHECK(!other_prompt_drawn(true,0xB0D7,true,0xB0D7,false) && !other_prompt_drawn(true,0xB0D8,true,0xB0D7,false));   // a body's own (or another body's) art
        // and the prop never enters the target: only bodies are observed
        const auto body=lying(-.75,.6);Tracker t;Obs o=palm_obs(0xB0D7,body,Vec3{0,.3,-.6});t.step(1000,&o,1);CHECK(t.body==0xB0D7);
    }
    {   // two bodies side by side (0.5 m apart): a pointing ray wandering between them does not flap
        const auto a=lying(-.75,2.0),b=lying(-.75,2.5);Tracker t;std::uint64_t now=1000;
        for(int i=0;i<200;++i,now+=11) {
            const double wobble=(i%5)*.05;   // the ray end wanders over 0.2 m between the two bodies
            const Vec3 dir=Vec3{0,.1,-2.15-wobble}-hand;Obs o[2]{point_obs(0xA,a,hand,dir),point_obs(0xB,b,hand,dir)};t.step(now,o,2);
        }
        CHECK(t.body!=0 && t.takeovers<=1 && t.drops[0]+t.drops[1]+t.drops[2]==0);
        // clearly onto the far body for longer than takeover_ms: it takes over once
        std::uint64_t switched=0;
        for(int i=0;i<40;++i,now+=11) {const Vec3 dir=Vec3{0,.1,-2.5}-hand;Obs o[2]{point_obs(0xA,a,hand,dir),point_obs(0xB,b,hand,dir)};
            o[0].point_gap=.6;o[0].along=2.2;   // A is now off the ray
            t.step(now,o,2);if(t.body==0xB && !switched) switched=now;}
        CHECK(t.body==0xB);
    }
    {   // palm-near beats pointing; another body only after takeover_ms
        const auto a=lying(-.75,.6),b=lying(-.75,2.0);Tracker t;
        Obs o[2]{palm_obs(0xA,a,Vec3{0,.25,-.6}),point_obs(0xB,b,hand,Vec3{0,.1,-2.0}-hand)};t.step(1000,o,2);CHECK(t.body==0xA && t.source==Source::palm);
    }
    {   // the hand leaves (or the arm points away): gone within hold_ms, not before
        const auto body=lying(-.75,.6);Tracker t;Obs o=palm_obs(0xB,body,Vec3{0,.25,-.6});t.step(1000,&o,1);CHECK(t.body==0xB);
        Obs away=palm_obs(0xB,body,Vec3{0,1.4,.5});   // 1 m away, not pointing
        std::uint64_t gone=0;for(std::uint64_t now=1011;now<2000;now+=11) {t.step(now,&away,1);if(!t.body && !gone) gone=now;}
        CHECK(gone>1000+hold_ms && gone<=1000+hold_ms+11 && t.drops[static_cast<unsigned>(Drop::away)]==1 && t.drops[static_cast<unsigned>(Drop::within_leave)]==0);
    }
    std::printf("PASS body_target\n");
    return 0;
}
