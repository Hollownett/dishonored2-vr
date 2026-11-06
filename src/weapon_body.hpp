#pragma once
// The drawn weapon's own physical model (user 2026-11-05: "add to guns a real physical model, same as to hands, so guns can collide with
// other physical objects"; then "can do same for sword?"). The hands are solid because NativeHandContact solves loose dynamic props
// (bottles, chairs, corpse limbs) against a set of sphere proxies and writes the prop's velocity (hand_contact.hpp); a hand holding a
// weapon had no such body of its own (dxgi_probe.cpp publishes only an empty open hand), so a pistol's barrel or a sword's blade passed
// through what it was driven into and only the world sweep (weapon_collision.hpp) stopped the hand.
//
// This builds the same kind of proxy set for the weapon: overlapping balls of its half thickness along the barrel or blade, so the union's
// surface is the weapon's box (a thin blade's chain of beads, a thick barrel's sleeve). The numbers are the ones the weapon sweep already
// uses for its segment (dxgi_probe.cpp: up, back, length, radius, from the weapon models and build/gun-shape), so what the weapon collides
// with as a body and what already stops it at a wall are one shape. NativeHandContact::publish_weapon hands the proxies to the same solve
// the hands get.
//
// Pure: no engine read, no thread, no allocation.
#include "hand_contact.hpp"
#include "tracked_pose.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace d2vr {

// The drawn weapon's body in the grip's own frame, metres: the barrel (or blade) runs along the grip's forward axis (sword_blade_axis,
// basis[0]); its breech sits `up` along basis[2] and `back` behind the grip, the tip `length` further on; `radius` is the half thickness,
// which is also the sweep's radius. `reversed` (a reverse grip) points it back down the hand, as the blade axis does.
struct WeaponBodyShape {double up{},back{},length{},radius{};};

// The proxies are the hands' type, so the contact model solves them unchanged. The count follows the half thickness: a 3.5 cm barrel gets
// 16 (spaced 0.7 r), a 2 cm blade the whole array (its beads overlap enough that a 1 cm prop has no hole to slip through). The ceiling is
// the hands' own array size: a bead past it would write over this body's fields, so it is asserted, not assumed. The parking value fills
// the rest of the array.
inline constexpr unsigned weapon_body_spheres=static_cast<unsigned>(hand_proxy_count);
static_assert(weapon_body_spheres<=hand_proxy_count,"a weapon body must fit the hands' proxy array");
inline constexpr Vec3 weapon_body_park{1e6,1e6,1e6};

struct WeaponBody {
    HandProxies proxies{};   // for NativeHandContact::publish_weapon
    unsigned count{};        // real spheres (the rest are parked at weapon_body_park, radius 0: inside nothing)
    Vec3 breech{},tip{};     // the barrel/blade segment in the world
    Vec3 sample{};           // the point the body's motion is sampled at: 60% of the way out (a poke is the tip's speed, a shove along the
                             // middle's; one velocity cannot be right for both. A blade's tip in a swing moves far faster than this point,
                             // which is what the sword's own impact poke is for)
    bool valid() const {return count>0 && finite(breech) && finite(tip) && finite(sample);}
};

// The overlap is chosen so the union stays close to the box: spheres of radius r spaced s dip r - sqrt(r^2-(s/2)^2) below the surface
// between them (2.2 mm at s = 0.7 r for a 3.5 cm barrel; ~6 mm for a 2 cm blade at the array's limit).
inline constexpr double weapon_body_spacing=.7;

inline WeaponBody weapon_body(const WorldTrackedPose& grip,const WeaponBodyShape& shape,bool reversed=false) noexcept {
    WeaponBody out{};
    for(auto& proxy:out.proxies) proxy={weapon_body_park,0};
    if(!finite(grip.position) || !(shape.length>0) || !(shape.radius>0) || shape.radius>.2 || shape.length>.8) return out;
    for(const auto axis:grip.basis) if(!finite(axis) || std::abs(dot(axis,axis)-1)>1e-4) return out;
    const auto forward=grip.basis[0]*(reversed?-1.:1.);
    out.breech=grip.position+grip.basis[2]*shape.up-forward*shape.back;
    out.tip=out.breech+forward*shape.length;
    const double spacing=shape.radius*weapon_body_spacing;
    unsigned count=static_cast<unsigned>(std::ceil(shape.length/spacing))+1;
    count=std::clamp(count,2u,weapon_body_spheres);
    for(unsigned i=0;i<count;++i) {
        const double t=static_cast<double>(i)/static_cast<double>(count-1);
        out.proxies[i]={out.breech+(out.tip-out.breech)*t,shape.radius};
    }
    out.count=count;out.sample=out.breech+(out.tip-out.breech)*.6;
    return out;
}

// The same body for a shape given as the four numbers the sweep takes (dxgi_probe.cpp's GunShape), so the two never drift.
inline WeaponBody weapon_body(const WorldTrackedPose& grip,double up,double back,double length,double radius,bool reversed=false) noexcept {
    return weapon_body(grip,WeaponBodyShape{up,back,length,radius},reversed);
}

}  // namespace d2vr
