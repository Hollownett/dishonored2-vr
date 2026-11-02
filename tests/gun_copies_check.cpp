// Row A (docs/dual-wield-two-hands-plan.md): the pure math behind held copies and thrown gun copies (src/held_copy.hpp).
#include "../src/held_copy.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace d2vr;
using namespace d2vr::held_copy;
static Pose yawed(Vec3 at,double yaw,double pitch=0,double roll=0) {
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
int main() {
    {   // what a def is, by name (guard defs included), case-insensitive
        assert(kind_of("models/campaign/dunwall/midnightassets/gun_corvo_midnight.def")==Kind::pistol);
        assert(kind_of("models/campaign/dunwall/midnightassets/CROSSBOW_emily_midnight.def")==Kind::crossbow);
        assert(kind_of("models/campaign/dunwall/midnightassets/sword_player_midnight.def")==Kind::sword);
        assert(kind_of("models/weapons/gun_elite/elite_gun.def")==Kind::pistol);
        assert(kind_of("models/weapons/sword_guard/sword_guard.def")==Kind::sword);
        assert(kind_of("models/props/bottle.def")==Kind::other && kind_of("")==Kind::other);
    }
    {   // grip offset compose: a measured offset puts the copy back exactly where the weapon was; the other hand gets its mirror
        const Pose hand=yawed({1.2,-0.4,1.3},.7,.3,-.4);
        const Pose weapon=to_world(hand,yawed({.03,-.05,.08},.2,-.5,.35));
        const Pose off=to_local(hand,weapon);
        Pose out{};
        assert(place(hand,off,false,out) && near_pose(out,weapon));                       // same hand: the weapon itself
        const Pose other=yawed({-.3,.5,1.1},-.4,.1,.2);
        Pose a{},b{};
        assert(place(other,off,true,a) && target(other,off,b) && near_pose(a,b));         // the other hand: the mirrored offset (== the mirror service's target)
        assert(valid(a) && !near_pose(a,weapon));
        // sword and other keep the identity constant (palm, forward along the hand): the copy sits exactly at the hand
        for(auto k:{Kind::sword,Kind::other}) {Pose c{};assert(place(hand,grip_constant(k).offset,false,c) && near_pose(c,hand));}
        // v773 (headset v772: the left pistol copy pointed up): a measured gun hold is kept only near the game's own hold of its kind
        {const Pose bad{{.0445,-.0806,-.0886},{Vec3{.6945,.0722,.7159},Vec3{.0275,.9916,-.1267},Vec3{-.719,.1077,.6866}}};   // headset v772 (barrel up)
         const Pose good{{.0232,-.0552,.009},{Vec3{.4188,.1225,-.8998},Vec3{-.1397,.9878,.0695},Vec3{.8973,.0966,.4307}}}; // headset v770
         assert(!plausible_grip(Kind::pistol,bad,0) && plausible_grip(Kind::pistol,good,0) && plausible_grip(Kind::pistol,mirror(good),1));
         assert(plausible_grip(Kind::sword,bad,0));}                                                                   // no constant to judge a sword by
        // never place on a half-written hand or offset
        Pose bad=hand;bad.position.x=NAN;assert(!place(bad,off,false,out) && !place(hand,bad,false,out));
        bad=off;bad.basis[1]=bad.basis[1]*2;assert(!place(hand,bad,true,out));
    }
    {   // pickup reach and which copy: the nearest present one, none when out of range
        const Vec3 gun[2]{{0,0,0},{3,0,0}};const bool present[2]{true,true};
        assert(in_reach({.3,0,0},gun[0]) && !in_reach({.5,0,0},gun[0]) && in_reach({.5,0,0},gun[0],.6) && !in_reach({NAN,0,0},gun[0]));
        assert(nearest(gun,present,2,{2.5,0,0})==1 && nearest(gun,present,2,{.2,.1,0})==0);
        const bool one[2]{false,true};
        assert(nearest(gun,one,2,{0,0,0})==1 && nearest(gun,one,2,{0,0,0},1.0)==-1);
        const bool none[2]{false,false};assert(nearest(gun,none,2,{0,0,0})==-1);
        const Vec3 nan[2]{{NAN,0,0},{1,0,0}};assert(nearest(nan,present,2,{0,0,0})==1);
    }
    {   // a copy lying too long or too far goes back to the hip
        assert(!should_return(29.9,11.9) && should_return(30.1,1) && should_return(1,12.1) && !should_return(0,0));
    }
    std::cout<<"gun_copies_check ok\n";
}
