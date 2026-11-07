#include "hand_weapon_contact.hpp"
#include <cassert>
#include <cstdio>
using namespace d2vr;
static const HwcCapsule away{{9,9,9},{9,9,9},.045};
static HwcSide blade(Vec3 a,Vec3 b,Vec3 v={}) {HwcSide s;s.tracked=true;s.has_weapon=true;s.weapon_is_blade=true;
    s.part[0]=away;s.part[0].a.x=s.part[0].b.x=9+a.x*7+a.y*13;s.part[1]={a,b,.02};s.vel[0]=v;s.vel[1]=v;return s;}
static HwcSide hand(Vec3 p,double r=.045) {HwcSide s;s.tracked=true;s.part[0]={p,p,r};return s;}
static bool near(double a,double b,double e=1e-6){return std::fabs(a-b)<e;}
int main(){
    HandWeaponContact c;
    // crossed blades at rest: z offset 0.01 -> overlap 0.04-0.01 = 0.03
    HwcSide L=blade({0,-.3,0},{0,.3,0}),R=blade({-.3,0,.01},{.3,0,.01});
    L.part[0]={{0,-5,0},{0,-5,0},.045};R.part[0]={{5,0,0},{5,0,0},.045};
    HwcSide in[2]={L,R};
    auto r=c.update(in,.011);
    assert(r.contact && near(r.depth,.03));
    assert(near(length(r.correction[1]-r.correction[0]),.03));            // separates by exactly the overlap
    assert(near(r.correction[0].z,-.015) && near(r.correction[1].z,.015));
    assert(r.event && !r.event->clash);                                    // at rest: no clash
    for(int i=0;i<30;++i){auto q=c.update(in,.011);assert(q.contact&&!q.event&&near(q.correction[0].z,-.015));}
    in[1].part[1].a.z=in[1].part[1].b.z=.0105;assert(near(c.update(in,.011).correction[0].z,-.015));   // <1 mm: held
    in[1].part[1].a.z=in[1].part[1].b.z=.02;assert(near(c.update(in,.011).correction[0].z,-.01));      // >1 mm: recomputed
    // parallel overlapping blades, 3 cm apart
    c.reset();HwcSide P=blade({0,0,0},{0,.6,0}),Q=blade({.03,0,0},{.03,.6,0});
    HwcSide pq[2]={P,Q};
    r=c.update(pq,.01);assert(r.contact&&near(r.depth,.01)&&near(r.correction[1].x,.005)&&near(r.correction[0].x,-.005));
    // blade through the other palm; the swinging blade yields less than the still hand
    c.reset();HwcSide B=blade({0,0,0},{0,0,.65},{0,0,2}),H=hand({.01,0,.3});
    HwcSide bh[2]={B,H};r=c.update(bh,.01);
    assert(r.contact&&r.event&&near(r.event->rel_speed,2)&&!r.event->clash);
    assert(length(r.correction[1])>length(r.correction[0]));
    assert(r.correction[1].x>0);
    // hand vs hand
    c.reset();HwcSide h1=hand({0,0,0}),h2=hand({.06,0,0});HwcSide hh2[2]={h1,h2};
    r=c.update(hh2,.01);assert(r.contact&&near(r.depth,.03)&&near(r.correction[0].x,-.015)&&near(r.correction[1].x,.015));
    // no contact
    c.reset();HwcSide far[2]={blade({0,0,0},{0,0,.6}),blade({1,0,0},{1,0,.6})};
    r=c.update(far,.01);assert(!r.contact&&length(r.correction[0])==0&&length(r.correction[1])==0);
    // clash flag only above speed
    for(double sp:{1.0,3.0}){c.reset();HwcSide a=L,b=R;a.vel[1]={sp,0,0};a.vel[0]={sp,0,0};HwcSide ab[2]={a,b};
        r=c.update(ab,.01);assert(r.event&&r.event->clash==(sp>=1.5));}
    // event once, re-arm after 0.08 s
    c.reset();HwcSide near2[2]={L,R},sep[2]={far[0],far[1]};
    assert(c.update(near2,.01).event);assert(!c.update(near2,.01).event);
    c.update(sep,.01);assert(!c.update(near2,.01).event);          // 0.02 s since the event: cooling
    c.update(sep,.01);c.update(sep,.05);assert(c.update(near2,.01).event);   // re-armed
    // untracked / NaN
    c.reset();HwcSide ut[2]={L,R};ut[1].tracked=false;r=c.update(ut,.01);assert(!r.contact&&length(r.correction[0])==0);
    HwcSide nn[2]={L,R};nn[0].part[1].b.x=NAN;r=c.update(nn,.01);assert(!r.contact&&!r.event);
    // cap 5 cm
    c.reset();HwcSide g1=hand({0,0,0},.5),g2=hand({0,0,0},.5);HwcSide gg[2]={g1,g2};
    r=c.update(gg,.01);assert(r.contact&&length(r.correction[0])<=.05+1e-12&&length(r.correction[1])<=.05+1e-12);
    // v1065: a gun barrel (a weapon, not a blade) against the other hand and against a fast sword: pushed apart, never a clash
    {c.reset();HwcSide G=blade({0,0,0},{0,0,.36});G.weapon_is_blade=false;G.part[1].radius=.035;
     HwcSide gh[2]={G,hand({.06,0,.2})};auto q=c.update(gh,.01);assert(q.contact && q.correction[1].x>0 && q.event && !q.event->clash);
     c.reset();HwcSide S=blade({.04,-.3,.2},{.04,.3,.2},{-3,0,0});HwcSide gs[2]={G,S};q=c.update(gs,.01);assert(q.contact && q.event && !q.event->clash);}
    std::puts("PASS");
}
