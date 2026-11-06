// The gun's physical body (weapon_body.hpp): the model a drawn ranged weapon pushes loose props with. Pure: no engine, no threads.
#include "weapon_body.hpp"
#include "hand_contact.hpp"
#include <cstdio>
#include <cmath>
#include <cstdlib>
using namespace d2vr;
#define CHECK(x) do{if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)

// A grip at `at`, barrel along the world's +x, the gun's "up" along +z (an aim down the x axis).
static WorldTrackedPose grip_at(Vec3 at) {return WorldTrackedPose{at,{{{1,0,0},{0,1,0},{0,0,1}}}};}
// A prop: the box (halves plus the radius) classify_held gives an upright bottle at the origin: .11 x .11 x .28 m.
static rig::HeldShape bottle() {
    rig::HeldShape s{};
    s.frame=WorldTrackedPose{{0,0,0},{{{1,0,0},{0,1,0},{0,0,1}}}};
    s.half={.035,.035,.12};s.radius=.02;
    return s;
}
int main() {
    const WeaponBodyShape pistol{.06,.06,.36,.035};
    { // The barrel runs along the grip's forward axis, from `up` above and `back` behind the grip to the muzzle `length` on.
        const auto body=weapon_body(grip_at({0,0,0}),pistol);
        CHECK(body.valid() && body.count>=2 && body.count<=weapon_body_spheres);
        CHECK(std::abs(body.breech.x+.06)<1e-9 && std::abs(body.breech.z-.06)<1e-9);
        CHECK(std::abs(body.tip.x-.3)<1e-9 && std::abs(body.tip.z-.06)<1e-9);
        CHECK(std::abs(body.sample.x-.156)<1e-9 && std::abs(body.sample.z-.06)<1e-9); // the motion sample: 60% of the way to the muzzle
        for(unsigned i=0;i<body.count;++i) CHECK(std::abs(body.proxies[i].radius-.035)<1e-12 && length(body.proxies[i].centre)<1);
        for(unsigned i=body.count;i<hand_proxy_count;++i) CHECK(body.proxies[i].radius==0 && body.proxies[i].centre.x>1e5); // parked: inside nothing
    }
    { // Coverage: the union of the spheres never leaves the barrel's surface by more than 2.5 mm between two of them.
        const auto body=weapon_body(grip_at({0,0,0}),pistol);
        for(unsigned i=1;i<body.count;++i) {
            const double gap=length(body.proxies[i].centre-body.proxies[i-1].centre),r=pistol.radius;
            CHECK(gap>0 && gap<2*r);
            CHECK(r-std::sqrt(std::max(0.,r*r-gap*gap*.25))<.0025);
        }
    }
    { // A crossbow: its own numbers, not the pistol's.
        const WorldTrackedPose grip{{.2,0,1.5},{{{0,1,0},{-1,0,0},{0,0,1}}}}; // barrel up the world's +y, gun up +z
        const auto body=weapon_body(grip,WeaponBodyShape{.05,.03,.28,.06});
        CHECK(body.valid());
        CHECK(std::abs(body.breech.y+.03)<1e-9 && std::abs(body.breech.z-1.55)<1e-9); // .03 behind the grip, .05 above it
        CHECK(std::abs(length(body.tip-body.breech)-.28)<1e-9);
        CHECK(std::abs(body.proxies[0].radius-.06)<1e-12);
    }
    { // Bad input is no body at all (the caller then clears its lane), never a body standing at the world origin.
        CHECK(!weapon_body(grip_at({0,0,0}),WeaponBodyShape{0,0,0,0}).valid());
        CHECK(!weapon_body(grip_at({0,0,0}),WeaponBodyShape{0,0,.3,-.03}).valid());
        CHECK(!weapon_body(grip_at({0,0,0}),WeaponBodyShape{0,0,.3,.5}).valid());   // thicker than any gun this model takes
        CHECK(!weapon_body(grip_at({0,0,0}),WeaponBodyShape{0,0,1.2,.03}).valid()); // longer than a weapon
        CHECK(!weapon_body(WorldTrackedPose{{NAN,0,0},{{{1,0,0},{0,1,0},{0,0,1}}}},pistol).valid());
        CHECK(!weapon_body(WorldTrackedPose{{0,0,0},{{{0,0,0},{0,1,0},{0,0,1}}}},pistol).valid()); // not a frame
    }
    { // A bottle the barrel is driven into: the gun's proxies touch it from the side it comes from and the hands' solve drives it.
        const auto body=weapon_body(grip_at({0,0,0}),pistol); // the breech sphere .005 before the bottle's near face, the barrel through it
        const auto hit=hand_contact(body.proxies,bottle(),{1,0,0},{},.5,HandContactConfig{},ContactInput{});
        CHECK(hit.touching && hit.push && hit.driving && hit.velocity.x>0);
    }
    { // A bottle the gun is nowhere near is not touched (the parked spheres reach nothing).
        const auto body=weapon_body(grip_at({3,0,0}),pistol);
        const auto miss=hand_contact(body.proxies,bottle(),{1,0,0},{},.5,HandContactConfig{},ContactInput{});
        CHECK(!miss.touching && !miss.push && !miss.driving && length(miss.outward)==0);
    }
    { // The sword: .65 m of 2 cm blade — the full bead count, and the beads overlap, so no prop has a hole to slip through.
        const WeaponBodyShape sword{0,0,.65,.02};
        const auto body=weapon_body(grip_at({0,0,0}),sword);
        CHECK(body.valid() && body.count==weapon_body_spheres);
        CHECK(std::abs(length(body.tip-body.breech)-.65)<1e-9);
        for(unsigned i=1;i<body.count;++i) {
            const double gap=length(body.proxies[i].centre-body.proxies[i-1].centre),r=sword.radius;
            CHECK(gap>0 && gap<2*r);
            CHECK(r-std::sqrt(std::max(0.,r*r-gap*gap*.25))<.008);
        }
        // A reverse grip points the blade back down the hand, as the blade axis does.
        const auto reversed=weapon_body(grip_at({0,0,0}),sword,true);
        CHECK(reversed.valid() && reversed.tip.x<reversed.breech.x && std::abs(reversed.tip.x+.65)<1e-9);
    }
    { // A bottle shoved sideways by the blade is driven along the shove (the blade behind it, on the side it comes from).
        const auto body=weapon_body(grip_at({0,-.06,0}),WeaponBodyShape{0,0,.65,.02}); // the blade at y=-.06, the bottle's near side
        const auto hit=hand_contact(body.proxies,bottle(),{0,1,0},{},.5,HandContactConfig{},ContactInput{});
        CHECK(hit.touching && hit.push && hit.velocity.y>0);
    }
    { // Why v1026's gun never touched anything: the sweep stops a weapon's segment its own radius off the prop, so beads of exactly that
      // radius rest ON the surface with no overlap, and the contact model, which pushes only on an overlap, never fires. A body must reach
      // past it by its skin (dxgi_probe.cpp weapon_body_skin) - the sword already did, because its blade is swept as a line (radius 0).
        const auto held_off=weapon_body(grip_at({.09,0,0}),WeaponBodyShape{0,0,.36,.035}); // the segment held .035 off the +x face
        const auto no_touch=hand_contact(held_off.proxies,bottle(),{-1,0,0},{},.5,HandContactConfig{},ContactInput{});
        CHECK(!no_touch.push && !no_touch.driving && no_touch.depth==0); // it touches at most: no overlap, nothing driven
        const auto skinned=weapon_body(grip_at({.09,0,0}),WeaponBodyShape{0,0,.36,.035+.015});
        const auto pushed=hand_contact(skinned.proxies,bottle(),{-1,0,0},{},.5,HandContactConfig{},ContactInput{});
        CHECK(pushed.touching && pushed.push && pushed.velocity.x<0);
    }
    { // "Collides like the sword": a weapon is a rigid body, so its contact strikes at the weapon's own speed from the first frame. The
      // hands' model ramps a fresh contact (write_step per write) and scales the drive by strength_kg/mass, which is why a barrel moved at
      // aiming or walking speed only made a bottle twitch while a 3-5 m/s sword swing passed the swat threshold and shoved.
        const auto body=weapon_body(grip_at({0,0,0}),pistol); // the barrel through the bottle's near side
        ContactInput soft;soft.dt=1./90;
        const auto ride=hand_contact(body.proxies,bottle(),{0.5,0,0},{},.5,HandContactConfig{},soft);
        ContactInput rigid=soft;rigid.rigid=true;
        const auto strike=hand_contact(body.proxies,bottle(),{0.5,0,0},{},.5,HandContactConfig{},rigid);
        CHECK(ride.push && strike.push);
        CHECK(length(ride.velocity)<0.15);          // the ride: one write_step, a twitch
        CHECK(length(strike.velocity)>0.4);         // the strike: the barrel's own speed
        CHECK(length(strike.velocity)<=length(ride.velocity)+1); // sanity: both bounded
        // A 20 kg limb (the service gives a corpse limb heavy_kg 40): the soft hand takes a tenth of the drive (strength_kg/mass), the rigid
        // weapon takes what its 200 N force cap allows.
        HandContactConfig limb;limb.heavy_kg=40.;
        const auto heavy_ride=hand_contact(body.proxies,bottle(),{0.5,0,0},{},20.,limb,soft);
        const auto heavy_strike=hand_contact(body.proxies,bottle(),{0.5,0,0},{},20.,limb,rigid);
        CHECK(heavy_ride.push && heavy_strike.push);
        CHECK(length(heavy_strike.velocity)>length(heavy_ride.velocity));
        CHECK(heavy_strike.capped);                  // the 200 N cap is what limits it, not the soft-hand gain
    }
    { // A gun at rest inside a prop overlaps it (the hand has to leave it) but drives nothing: the hands' rule.
        const auto body=weapon_body(grip_at({.05,0,0}),pistol); // the barrel through the bottle's middle
        const auto rest=hand_contact(body.proxies,bottle(),{},{},.5,HandContactConfig{},ContactInput{});
        CHECK(length(rest.outward)>0 && !rest.push && !rest.driving);
    }
    { // Heavy things are refused by the same mass rule the hands use (a 20 kg barrel takes nothing from a gun at 1 m/s).
        const auto body=weapon_body(grip_at({0,0,0}),pistol);
        const auto heavy=hand_contact(body.proxies,bottle(),{1,0,0},{},20.,HandContactConfig{},ContactInput{});
        CHECK(heavy.touching && !heavy.push);
    }
    return 0;
}
