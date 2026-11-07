// v1075: a held prop follows a running hand like a carried weapon. The capped prop law closed at 18/s with a feed that fades with the hand
// sample's age and no player carrier, so at 5 m/s with an aged sample it trailed ~v/18; a heavy prop was also force-capped. The new prop law
// (carrier feed, 35/s, no cap) keeps it within a few cm; a corpse keeps the old capped law.
#include "physical_grab.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
using namespace d2vr;
static WorldTrackedPose at(double x,double y,double z) {WorldTrackedPose p{};p.position={x,y,z};p.basis[0]={1,0,0};p.basis[1]={0,1,0};p.basis[2]={0,0,1};return p;}
// Run 3 s: stand 0.5 s, then the player runs at `speed` (the hand rides with him); the physics integrates the written velocity at 60 Hz.
// The hand sample the motor sees is `age_ms` old (the feed fades past 32 ms). Returns the largest gap after 1.5 s (steady running).
static double run(bool corpse,double speed,std::uint64_t age_ms,double kg) {
    PhysicalGrab g;GrabTuning tuning;
    const auto hand0=at(0,0,1.2);
    g.sample(hand0,0.f,true,1,1000);g.sample(hand0,0.f,true,1,1010);g.sample(hand0,1.f,true,1,1020);
    auto body=at(.05,0,1.15);assert(g.bind(body,body.position));
    Vec3 v{};double worst=0;
    for(int i=0;i<180;++i) {
        const double t=i/60.;const double x=t<.5?0:(t-.5)*speed;
        const std::uint64_t tick=1030+static_cast<std::uint64_t>(i*1000/60);
        g.sample(at(x,0,1.2),1.f,true,1,tick);
        CorpseMotorInput in;in.body_velocity=v;in.velocity_known=true;in.corpse=corpse;in.object_kg=kg;in.inertia_kgm2=.05;in.dt=1./60;in.tuning=&tuning;
        in.carrier=t<.5?Vec3{}:Vec3{speed,0,0};
        const auto r=g.corpse_motor(body,true,tick+age_ms,tick,in);
        assert(r && !r->motor.release);
        if(t>=1.5) {const auto goal=g.target();worst=std::max(worst,length(goal.position-body.position));}   // the body and the goal of the same instant
        v=r->motor.linear;body.position=body.position+v*(1./60);
    }
    return worst;
}
int main() {
    const double fresh=run(false,5,10,1.),aged=run(false,5,90,1.),heavy=run(false,5,90,8.),corpse=run(true,5,90,8.);
    std::printf("held prop gap at 5 m/s: fresh sample %.1f mm, aged sample %.1f mm, heavy (8 kg) %.1f mm; corpse (capped law) %.1f mm\n",
        fresh*1000,aged*1000,heavy*1000,corpse*1000);std::fflush(stdout);
    assert(fresh<.01 && aged<.01 && heavy<.01);   // a held prop stays in the hand while running, whatever the sample's age or its weight
    assert(corpse>aged);                          // a corpse limb keeps the heavier Bonelab law
    // v1086: a 45 deg snap turn with the arm out carries the hand 0.7 m in one step (past the 0.5 m jump rule): a squeezed hold stays and the
    // prop is back on its target within a few steps; a released grip that jumps still throws nothing.
    {PhysicalGrab g;GrabTuning tuning;const auto hand0=at(0,0,1.2);
     g.sample(hand0,0.f,true,1,1000);g.sample(hand0,0.f,true,1,1010);g.sample(hand0,1.f,true,1,1020);
     auto body=at(.05,0,1.15);assert(g.bind(body,body.position));
     const double c=std::cos(.785398),s=std::sin(.785398);const double r=.92;   // the hand 0.92 m from the turn pivot
     WorldTrackedPose jumped=at(r*c-r,r*s,1.2);
     const bool kept=g.sample(jumped,1.f,true,1,1030);
     assert(kept && g.held() && g.pressed());
     Vec3 v{};unsigned steps=0;
     for(int i=0;i<30;++i) {
        CorpseMotorInput in;in.body_velocity=v;in.velocity_known=true;in.corpse=false;in.object_kg=1;in.inertia_kgm2=.05;in.dt=1./60;in.tuning=&tuning;
        const std::uint64_t tick=1030+static_cast<std::uint64_t>((i+1)*1000/60);g.sample(jumped,1.f,true,1,tick);
        const auto m=g.corpse_motor(body,true,tick+10,tick,in);assert(m && !m->motor.release);
        if(length(g.target().position-body.position)<.01 && !steps) steps=static_cast<unsigned>(i);
        v=m->motor.linear;body.position=body.position+v*(1./60);
     }
     std::printf("snap turn: hold kept, back in the hand after %u steps\n",steps);std::fflush(stdout);
     assert(steps>0 && steps<=6);
     PhysicalGrab q;q.sample(hand0,0.f,true,2,2000);q.sample(hand0,0.f,true,2,2010);q.sample(hand0,1.f,true,2,2020);auto b2=at(.05,0,1.15);assert(q.bind(b2,b2.position));
     assert(!q.sample(jumped,0.f,true,2,2030));   // an open hand that jumps: the old rule, no throw from the jump
    }
    std::puts("PASS");
}
