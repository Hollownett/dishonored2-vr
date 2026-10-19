// Reach clamp (physical hands phase 1: G2, C4): the soft-IK reach rule body_ik uses, the largest wrist distance it
// still reaches exactly, and the translation-only clamp that moves a grip back inside that reach.
#include "hand_reach.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
using namespace d2vr;
namespace {
int failures=0;
void check(bool ok,const char* what) {if(!ok) {std::printf("FAIL %s\n",what);++failures;}}
bool same(const WorldTrackedPose& a,const WorldTrackedPose& b) {
    const auto eq=[](Vec3 x,Vec3 y) {return x.x==y.x && x.y==y.y && x.z==y.z;};
    return eq(a.position,b.position) && eq(a.basis[0],b.basis[0]) && eq(a.basis[1],b.basis[1]) && eq(a.basis[2],b.basis[2]);
}
Vec3 wrist_of(const WorldTrackedPose& grip,Vec3 local) {
    return grip.position+grip.basis[0]*local.x+grip.basis[1]*local.y+grip.basis[2]*local.z;
}
// A grip turned off the axes, placed so that its wrist sits `distance` from the shoulder along `direction`.
WorldTrackedPose grip_with_wrist_at(const ArmReach& reach,Vec3 direction,double distance) {
    const auto q=normalized(Quaternion{.2,-.35,.15,.9});
    WorldTrackedPose grip{{},{rotated(q,{1,0,0}),rotated(q,{0,1,0}),rotated(q,{0,0,1})}};
    const auto wrist=reach.shoulder+unit(direction)*distance;
    grip.position=wrist-(wrist_of(grip,reach.wrist_local)-grip.position);
    return grip;
}
ArmReach sample_reach() {return {{.1,.2,1.4},.6,{-.05,.01,.02},true};}

void soft_reach_rule() {
    const auto r=soft_reach(.5,.6);
    check(r.target_distance==.5 && r.extension==1,"soft_reach(.5,.6) is {.5,1}");
    const double m=max_exact_reach(.6);
    check(std::isfinite(m) && m>.6*.92 && m<.6*1.06,"max_exact_reach(.6) lies in the soft band");
    const double inside=m-1e-4,outside=m+1e-3;
    check(std::abs(soft_reach(inside,.6).target_distance-inside)<=1e-9,"just inside max_exact_reach the wrist is reached exactly");
    check(soft_reach(outside,.6).target_distance<outside,"just beyond max_exact_reach the wrist stops short");
    check(soft_reach(outside,.6).extension==1.06,"beyond max_exact_reach the stretch cap is engaged");
    check(max_exact_reach(0)==0 && max_exact_reach(-1)==0 && max_exact_reach(std::numeric_limits<double>::quiet_NaN())==0,
          "an invalid arm length has no reach");
}
void clamp_beyond_reach() {
    const auto reach=sample_reach();
    const Vec3 direction{.3,.8,-.2};
    const auto grip=grip_with_wrist_at(reach,direction,reach.max_wrist+.2);
    bool clamped=false;
    const auto out=reach_clamp(grip,reach,&clamped);
    const auto wrist=wrist_of(out,reach.wrist_local);
    check(clamped,"a grip beyond reach reports clamped");
    check(std::abs(length(wrist-reach.shoulder)-reach.max_wrist)<=1e-9,"the clamped wrist sits exactly at max_wrist");
    check(length(unit(wrist-reach.shoulder)-unit(direction))<=1e-9,"the wrist moves along shoulder->wrist");
    for(unsigned a=0;a<3;++a)
        check(out.basis[a].x==grip.basis[a].x && out.basis[a].y==grip.basis[a].y && out.basis[a].z==grip.basis[a].z,"orientation unchanged");
    check(std::abs(length(out.position-grip.position)-.2)<=1e-9,"the grip moves by the overshoot");
}
void clamp_inside_reach() {
    const auto reach=sample_reach();
    const auto grip=grip_with_wrist_at(reach,{-.2,.9,.4},reach.max_wrist-.05);
    bool clamped=true;
    const auto out=reach_clamp(grip,reach,&clamped);
    check(!clamped && same(out,grip),"a grip inside reach is unchanged");
    check(same(reach_clamp(grip,reach),grip),"the clamped flag is optional");
}
void clamp_invalid_reach() {
    const double nan=std::numeric_limits<double>::quiet_NaN(),inf=std::numeric_limits<double>::infinity();
    const auto base=sample_reach();
    const auto grip=grip_with_wrist_at(base,{.3,.8,-.2},base.max_wrist+.2);
    const auto unchanged=[&](ArmReach reach,const char* what) {
        bool clamped=true;const auto out=reach_clamp(grip,reach,&clamped);
        check(!clamped && same(out,grip),what);
    };
    auto r=base;r.valid=false;unchanged(r,"valid=false leaves the grip unchanged");
    r=base;r.shoulder.x=nan;unchanged(r,"a non-finite shoulder leaves the grip unchanged");
    r=base;r.max_wrist=nan;unchanged(r,"a non-finite max_wrist leaves the grip unchanged");
    r=base;r.max_wrist=inf;unchanged(r,"an infinite max_wrist leaves the grip unchanged");
    r=base;r.max_wrist=0;unchanged(r,"a zero max_wrist leaves the grip unchanged");
    r=base;r.wrist_local.z=inf;unchanged(r,"a non-finite wrist_local leaves the grip unchanged");
    auto bad=grip;bad.position.y=nan;
    bool clamped=true;const auto out=reach_clamp(bad,base,&clamped);
    check(!clamped && std::isnan(out.position.y),"a non-finite grip is passed through unclamped");
}
}
int main() {
    soft_reach_rule();
    clamp_beyond_reach();
    clamp_inside_reach();
    clamp_invalid_reach();
    {   // the threshold is a function of the arm length alone: bisected once per length, not on every diagnostic() call (four a frame, two arms each)
        ExactReachCache cache;
        const double first=cache.get(.6);
        check(first==max_exact_reach(.6) && cache.computed()==1,"the cache returns the bisected threshold");
        for(int i=0;i<10;++i) check(cache.get(.6)==first,"a repeated length returns the same threshold");
        check(cache.computed()==1,"a repeated length is not bisected again");
        check(cache.get(.61)==max_exact_reach(.61) && cache.computed()==2,"a new length is bisected once");
        check(cache.get(.61)==max_exact_reach(.61) && cache.computed()==2,"and then served from the cache");
        check(cache.get(0)==0 && cache.get(std::nan(""))==0,"an invalid arm has no reach");
    }
    if(failures) {std::printf("hand_reach_check: %d failure(s)\n",failures);return 1;}
    std::printf("PASS hand_reach_check: soft_reach rule, max_exact_reach bisection, translation-only reach_clamp\n");
    return 0;
}
