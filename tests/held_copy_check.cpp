#include "../src/held_copy.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace d2vr;
using namespace d2vr::held_copy;
static Pose yawed(Vec3 at,double yaw,double pitch=0,double roll=0) { // forward/left/up after yaw (about up), pitch (about left), roll (about forward)
    Pose p{at,{Vec3{1,0,0},Vec3{0,1,0},Vec3{0,0,1}}};
    const auto rot=[&](Vec3 axis,double a) {for(auto& v:p.basis) {const double c=std::cos(a),s=std::sin(a);v=v*c+cross(axis,v)*s+axis*(dot(axis,v)*(1-c));}};
    rot({1,0,0},roll);rot({0,1,0},-pitch);rot({0,0,1},yaw);
    return p;
}
static bool near_pose(const Pose& a,const Pose& b,double tol=1e-9) {
    if(length(a.position-b.position)>tol) return false;
    for(unsigned i=0;i<3;++i) if(length(a.basis[i]-b.basis[i])>tol) return false;
    return true;
}
static Pose reflect_x(const Pose& p) { // the pose the other hand has when it mirrors this one across the plane x=0 (left stays the mirrored side)
    const auto r=[](Vec3 v) {return Vec3{-v.x,v.y,v.z};};
    return {r(p.position),{r(p.basis[0]),r(p.basis[1])*-1,r(p.basis[2])}};
}
int main() {
    const Pose hand=yawed({1.2,-0.4,1.3},.7,.3,-.4);
    const Pose weapon=to_world(hand,yawed({.03,-.05,.08},.2,-.5,.35)); // a sword: offset and a tilted grip
    assert(valid(hand) && valid(weapon));
    {   // compose(measure) is the identity
        const Pose off=to_local(hand,weapon);
        assert(valid(off) && near_pose(to_world(hand,off),weapon));
        assert(near_pose(to_local(hand,hand),Pose{{0,0,0},{Vec3{1,0,0},Vec3{0,1,0},Vec3{0,0,1}}}));
    }
    {   // mirror: involution, still a rotation, y of the position negated
        const Pose off=to_local(hand,weapon);const Pose m=mirror(off);
        assert(valid(m) && near_pose(mirror(m),off) && std::abs(m.position.y+off.position.y)<1e-12 && std::abs(m.position.x-off.position.x)<1e-12);
        assert(near_pose(offset_for(off,1,1),off) && near_pose(offset_for(off,0,1),m) && near_pose(offset_for(offset_for(off,0,1),1,0),off)); // v816: a stored grip seen from either hand
        const Pose yaw=yawed({0,.05,0},.35); // yaw about up flips sign in the mirror
        const Pose my=mirror(yaw);
        assert(near_pose(my,yawed({0,-.05,0},-.35)));
        assert(near_pose(mirror(Pose{{.1,.2,.3},{Vec3{1,0,0},Vec3{0,1,0},Vec3{0,0,1}}}),Pose{{.1,-.2,.3},{Vec3{1,0,0},Vec3{0,1,0},Vec3{0,0,1}}})); // identity stays identity
    }
    {   // end to end: the other hand is the exact reflection of the native hand, so the copy is the reflection of the weapon (left axis of the weapon flips)
        const Pose other=reflect_x(hand);
        Pose out{};assert(valid(other) && target(other,to_local(hand,weapon),out));
        const auto r=[](Vec3 v) {return Vec3{-v.x,v.y,v.z};};
        const Pose want{r(weapon.position),{r(weapon.basis[0]),r(weapon.basis[1])*-1,r(weapon.basis[2])}};
        assert(valid(out) && near_pose(out,want,1e-9));
        // swapping hands twice returns the original weapon pose
        Pose back{};assert(target(hand,to_local(other,out),back) && near_pose(back,weapon,1e-9));
    }
    {   // NaN, infinity, scaled, reflected and half-written frames are rejected
        Pose bad=hand;bad.position.x=NAN;assert(!valid(bad));Pose out{};assert(!target(bad,to_local(hand,weapon),out) && !target(hand,bad,out));
        bad=hand;bad.basis[1]=bad.basis[1]*1.5;assert(!valid(bad));
        bad=hand;bad.basis[2]=bad.basis[2]*-1;assert(!valid(bad));
        bad=hand;bad.position.z=INFINITY;assert(!valid(bad));
        bad=hand;bad.basis[0]={0,0,0};assert(!valid(bad));
        OffsetFilter f;assert(!f.add(bad) && !f.have);
        const float rows[9]={1,0,0,0,1,0,0,0,1};const float o[3]={1,2,3};assert(valid(from_rows(o,rows)) && from_rows(o,rows).position.z==3);
    }
    {   // the filter: averages jitter, needs 12 agreeing samples, a jump restarts it
        const Pose off=to_local(hand,weapon);OffsetFilter f;
        for(unsigned i=0;i<11;++i) {Pose s=off;s.position.x+=(i%2?.002:-.002);assert(f.add(s));assert(!f.ready());}
        assert(f.add(off) && f.ready());
        assert(length(f.value.position-off.position)<.002 && angle(f.value,off)<.01 && valid(f.value));
        Pose jump=off;jump.position.x+=.1;assert(f.add(jump) && !f.ready() && f.stable==1 && near_pose(f.value,jump));
        f.reset();assert(!f.have && !f.ready());
    }
    {   // hand motion
        const Pose a=yawed({0,0,1},0),b=yawed({.01,0,1},.01);
        const auto m=motion(a,b,.01);assert(std::abs(m.linear-1)<1e-9 && std::abs(m.angular-1)<1e-6);
        assert(motion(a,a,0).linear>1e8);
    }
    {   // v776: the step a copy is carried ahead by (it is placed before the frame's physics step moves the player): the root's last displacement between two
        // services one frame apart; none after a pause or a hitch, a teleport, no time, or a bad root
        const Vec3 a{1,2,3},b{1.1,2,3.01};
        assert(length(root_step(a,b,1/60.)-(b-a))<1e-12 && length(root_step(a,b,.05)-(b-a))<1e-12);
        assert(length(root_step(a,b,.2))==0 && length(root_step(a,b,0))==0 && length(root_step(a,b,-1))==0);
        assert(length(root_step(a,{a.x+5,a.y,a.z},1/60.))==0 && length(root_step(a,{NAN,0,0},1/60.))==0);
        assert(length(root_step(a,a,1/60.))==0); // standing: nothing to add
        // v779: scaled by this step's game dt over the last one's (the headset's frame pacing varies); an unknown or absurd dt leaves it unscaled
        assert(length(root_step(a,b,1/60.,1/60.,1/60.)-(b-a))<1e-12 && length(root_step(a,b,1/60.,.010,.015)-(b-a)*1.5)<1e-12);
        assert(length(root_step(a,b,1/60.,0,.015)-(b-a))<1e-12 && length(root_step(a,b,1/60.,.010,.5)-(b-a))<1e-12);
    }
    std::cout << "held_copy_check ok\n";
    return 0;
}
