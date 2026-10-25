#include "../src/living_npc_hold.hpp"
#include <cassert>
#include <iostream>
using namespace d2vr;
namespace {
// A guard's collar (bone 2, radius 0.16) at (0,0,1.5); the controller 0.2 m in front of it (+x).
LivingBone collar() {LivingBone b;b.entity=0x9000;b.entity_id=0x20002;b.bone=2;b.position={0,0,1.5};b.radius=.16;return b;}
LivingTick at(Vec3 controller,Vec3 velocity={},float grip=.9f,double dt=.01,bool alive=true,std::uint64_t epoch=1) {
    LivingTick t;t.controller=controller;t.bone={0,0,1.5};t.hand_velocity=velocity;t.grip=grip;t.dt=dt;t.alive=alive;t.epoch=epoch;return t;
}
}
int main() {
    {   // tether_breaks_after_02s_beyond_035: 0.36 m for 0.19 s holds; at 0.21 s it lets go
        LivingHold h(collar(),{.2,0,1.5},1,false);
        for(int i=0;i<19;++i) {const auto s=h.tick(at({.36,0,1.5}));assert(!s.release && !s.hard_pull && !s.pull_step);}
        const auto back=h.tick(at({.30,0,1.5}));assert(!back.release);   // back inside: the clock restarts
        for(int i=0;i<19;++i) assert(!h.tick(at({.36,0,1.5})).release);
        assert(!h.tick(at({.36,0,1.5},{},.9f,.005)).release);             // 0.195 s
        const auto gone=h.tick(at({.36,0,1.5},{},.9f,.015));assert(gone.release && !gone.hard_pull);   // 0.21 s
        assert(h.ended() && h.tick(at({.2,0,1.5})).release);
    }
    {   // grip_release_frees
        LivingHold h(collar(),{.2,0,1.5},1,true);assert(!h.tick(at({.2,0,1.5})).release);
        const auto s=h.tick(at({.2,0,1.5},{},.34f));assert(s.release && !s.hard_pull && !s.pull_step);
    }
    {   // hard_pull_at_22_away_only (ruling H9: 2.2 m/s pinned): 2.3 m/s away -> hard pull; 2.3 m/s toward him, 2.1 away or sideways -> none
        LivingHold a(collar(),{.2,0,1.5},1,false);const auto s=a.tick(at({.2,0,1.5},{2.3,0,0}));assert(s.hard_pull && s.release);
        LivingHold b(collar(),{.2,0,1.5},1,false);assert(!b.tick(at({.2,0,1.5},{-2.3,0,0})).hard_pull);
        LivingHold c(collar(),{.2,0,1.5},1,false);assert(!c.tick(at({.2,0,1.5},{2.1,0,0})).hard_pull);
        LivingHold d(collar(),{.2,0,1.5},1,false);assert(!d.tick(at({.2,0,1.5},{0,3,0})).hard_pull);
        LivingHold e(collar(),{.2,0,1.5},1,false);assert(!e.tick(at({.2,0,1.5},{2.2,0,0})).hard_pull);   // exactly 2.2: not above it
    }
    {   // pull_step_bounded_horizontal: a 0.2 m gap with a vertical part -> a step of at most 5 cm per 100 ms, no z, toward the controller
        LivingHold h(collar(),{.2,0,1.5},1,true);
        const auto s=h.tick(at({.16,.12,1.3},{},.9f,.1));
        assert(s.pull_step && s.pull_step->z==0 && length(*s.pull_step)<=.05+1e-12 && length(*s.pull_step)>.049);
        assert(s.pull_step->x>0 && s.pull_step->y>0 && std::abs(s.pull_step->x/s.pull_step->y-.16/.12)<1e-9);
        const auto small=h.tick(at({.16,.12,1.3},{},.9f,.02));assert(small.pull_step && length(*small.pull_step)<=.01+1e-12);   // 20 ms: 1 cm
        assert(length(s.anchor-Vec3{0,0,1.5})>.159 && length(s.anchor-Vec3{0,0,1.5})<.161);   // the hand seats on his surface toward the controller
    }
    {   // no_pull_when_disabled
        LivingHold h(collar(),{.2,0,1.5},1,false);assert(!h.tick(at({.3,0,1.5},{},.9f,.1)).pull_step);
    }
    {   // no_pull_inside_6cm: a 5 cm horizontal gap (and a large vertical one) gives no step; a 7 cm one only closes to 6 cm
        LivingHold h(collar(),{.2,0,1.5},1,true);assert(!h.tick(at({.05,0,1.2},{},.9f,.1)).pull_step);
        const auto s=h.tick(at({.07,0,1.5},{},.9f,.1));assert(s.pull_step && std::abs(length(*s.pull_step)-.01)<1e-9);
    }
    {   // epoch_change_releases_living_hold
        LivingHold h(collar(),{.2,0,1.5},1,true);const auto s=h.tick(at({.3,0,1.5},{},.9f,.1,true,2));assert(s.release && !s.pull_step && !s.hard_pull);
    }
    {   // living_target_dies_releases_without_write: alive=false -> release, no step (and no hard pull, even with a fast hand)
        LivingHold h(collar(),{.2,0,1.5},1,true);const auto s=h.tick(at({.3,0,1.5},{5,0,0},.9f,.1,false));
        assert(s.release && !s.pull_step && !s.hard_pull);
        LivingHold n(collar(),{.2,0,1.5},1,true);auto bad=at({.3,0,1.5});bad.bone={std::nan(""),0,0};assert(n.tick(bad).release);
    }
    std::cout<<"PASS: living hold tether 0.35 m/0.2 s, grip release, hard pull above 2.2 m/s away only, bounded horizontal pull, epoch and death release\n";
}
