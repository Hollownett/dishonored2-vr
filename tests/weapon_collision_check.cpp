#include "weapon_collision.hpp"
#include <cstdio>
#include <cmath>
#include <cstdlib>
using namespace d2vr;
#define CHECK(x) do{if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
// Wall: plane x=1, solid for x>1, normal -x.
static std::optional<CollisionHit> wall(Vec3 a,Vec3 b) {
    if(a.x>=1||b.x<1) return std::nullopt;
    const double t=(1-a.x)/(b.x-a.x);return CollisionHit{a+(b-a)*t,{-1,0,0},t,7};
}
int main() {
    // Blade only (physical hands Task 5): the hand is the hand clamp's (hand_clamp.hpp); the blade sweeps from the drawn grip.
    WeaponCollision c;const Vec3 dir{1,0,0};const double dt=1./90;
    auto r=c.update(0,dt,{.2,0,0},dir,.6,wall);
    CHECK(!r.contact&&!r.event&&!r.haptic&&length(r.shift)==0);
    // Tip pokes .2 into the wall (grip .6, tip 1.2): grip pushed back .2, tip rests on the surface.
    r=c.update(0,dt,{.6,0,0},dir,.6,wall);
    CHECK(r.contact&&r.event&&r.event->blade&&r.event->entity==7&&r.haptic);
    CHECK(std::abs(r.grip.x+.6-1.0)<1e-9&&r.event->speed>10);
    CHECK(r.haptic->amplitude==1.f);
    r=c.update(0,dt,{.7,0,0},dir,.6,wall);
    CHECK(r.contact&&!r.event&&!r.haptic&&std::abs(r.grip.x+.6-1.0)<1e-9);
    { // A bare hand (no blade) sweeps nothing: the grip is the caller's, whatever it is inside.
        unsigned queries=0;WeaponCollision bare;
        const auto q=bare.update(1,dt,{1.2,0,0},dir,0,[&](Vec3 a,Vec3 b){++queries;return wall(a,b);});
        CHECK(queries==0 && !q.contact && !q.event && !q.haptic && length(q.shift)==0 && q.grip.x==1.2);
    }
    { // build/no-hand-clamp: the pre-hand-clamp palm path. A bare hand stops one palm radius off the surface; the blade sweeps
      // from the palm-corrected grip; a wall far from the hand (body anchor inside geometry) never pushes it.
        WeaponCollision legacy;const Vec3 anchor{0,0,0};
        const auto q=legacy.update_with_palm(1,dt,anchor,{1.2,0,0},dir,0,wall);
        CHECK(q.event&&!q.event->blade&&std::abs(q.grip.x-(1-.06))<1e-9);
        unsigned queries=0;Vec3 blade_from{};WeaponCollision both;
        const auto b=both.update_with_palm(0,dt,anchor,{1.02,0,0},{0,1,0},.3,[&](Vec3 a,Vec3 e){if(++queries==2) blade_from=a;return wall(a,e);});
        CHECK(queries==2 && std::abs(blade_from.x-.94)<1e-9 && std::abs(b.grip.x-.94)<1e-9);
        WeaponCollision far;const auto f=far.update_with_palm(1,dt,{1.5,0,0},{.5,0,0},dir,0,[](Vec3 a,Vec3)->std::optional<CollisionHit>{
            if(a.x<=1.2) return std::nullopt;return CollisionHit{{1.2,0,0},{1,0,0},.6,3};}); // hit 0.7 m from the hand
        CHECK(!f.contact && length(f.shift)==0);
        CHECK(!legacy.update_with_palm(0,dt,{NAN,0,0},{.5,0,0},dir,.6,wall).contact);
    }
    { // The blade starts at the grip it is given (the drawn one): a grip already held off the wall sweeps from there.
        unsigned queries=0;Vec3 from{};WeaponCollision drawn;
        drawn.update(0,dt,{.3,0,0},dir,.6,[&](Vec3 a,Vec3 b){++queries;from=a;return wall(a,b);},{0,0,.04});
        CHECK(queries==1 && from.x==.3 && from.z==.04);
    }
    // Release: controller backs out; shift eases monotonically to zero.
    r=c.update(0,dt,{.2,0,0},dir,.6,wall);
    CHECK(r.contact&&length(r.shift)>0&&length(r.shift)<.3);
    double prev=length(r.shift);int n=0;
    while(r.contact) {r=c.update(0,dt,{.2,0,0},dir,.6,wall);CHECK(length(r.shift)<=prev+1e-12);prev=length(r.shift);CHECK(++n<400);}
    CHECK(length(r.shift)==0);
    r=c.update(0,dt,{.6,0,0},dir,.6,wall);CHECK(r.event);
    WeaponCollision cap({.1,1.5,.12,.08,3});
    r=cap.update(0,dt,{.6,0,0},dir,.6,wall);CHECK(std::abs(length(r.shift)-.1)<1e-9);
    r=c.update(0,dt,{NAN,0,0},dir,.6,wall);CHECK(!r.contact);
    { // Angled blade: the hand keeps its tracked position, the blade pivots so its tip rests on the wall.
        WeaponCollision a;const Vec3 d45{std::sqrt(.5),std::sqrt(.5),0};
        const auto q=a.update(0,dt,{.5,0,0},d45,.9,wall); // tip at x=1.136: .136 inside
        CHECK(q.blade_dir && length(q.shift)==0 && std::abs(length(*q.blade_dir)-1)<1e-9);
        const Vec3 tip=Vec3{.5,0,0}+*q.blade_dir*.9;CHECK(std::abs(tip.x-.99)<1e-9 && tip.y>0);
        for(const bool reverse:{false,true}) {
            const double sign=reverse?-1.:1.;
            WorldTrackedPose pose{{.5,0,0},{{d45*sign,Vec3{-d45.y,d45.x,0}*sign,{0,0,1}}}};
            CHECK(align_weapon_blade(pose,*q.blade_dir,reverse));
            CHECK(length(pose.basis[0]*sign-*q.blade_dir)<1e-9);
            CHECK(length(pose.position-Vec3{.5,0,0})==0);
            CHECK(dot(cross(pose.basis[0],pose.basis[1]),pose.basis[2])>1-1e-9);
            const auto settled=pose;
            CHECK(align_weapon_blade(pose,*q.blade_dir,reverse));
            for(unsigned i=0;i<3;++i)CHECK(length(pose.basis[i]-settled.basis[i])<1e-12);
            CHECK(align_weapon_blade(pose,*q.blade_dir*-1.,reverse));
            CHECK(length(pose.basis[0]*sign+*q.blade_dir)<1e-9);
            CHECK(dot(cross(pose.basis[0],pose.basis[1]),pose.basis[2])>1-1e-9);
            for(const auto axis:pose.basis)CHECK(std::abs(length(axis)-1)<1e-9);
            CHECK(!align_weapon_blade(pose,{NAN,0,0},reverse));
            CHECK(!align_weapon_blade(pose,{},reverse));
        }
        WeaponCollision repeated;
        for(const bool reverse:{false,true,false,true,false}) {
            const double sign=reverse?-1.:1.;
            WorldTrackedPose pose{{.5,0,0},{{d45*sign,Vec3{-d45.y,d45.x,0}*sign,{0,0,1}}}};
            const auto contact=repeated.update(1,dt,pose.position,pose.basis[0]*sign,.9,wall);
            CHECK(contact.blade_dir && align_weapon_blade(pose,*contact.blade_dir,reverse));
            const auto visible_tip=pose.position+pose.basis[0]*(sign*.9);
            CHECK(std::abs(visible_tip.x-.99)<1e-9 && visible_tip.y>0);
        }
    }
    const auto h=bisect_segment({0,0,0},{2,0,0},[](Vec3,Vec3 b){return b.x>=1;},12);
    CHECK(h&&std::abs(h->point.x-1)<1e-3&&h->normal.x<0);
    CHECK(!bisect_segment({0,0,0},{.5,0,0},[](Vec3,Vec3 b){return b.x>=1;}));
    std::puts("PASS");
}
