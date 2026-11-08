// v1090 lab (user, headset v1089: "when I carry severed legs, heads and so on it is very unstable on the run, make it smooth and stable").
// The v1089 run's native-physical-grab.csv: a held head at 5-7 m/s had its error to the hand target flip sign every ~50 ms (+-3 cm) and its
// commanded velocity swing 2.6-6.8 m/s. The clocks: the hand is published every ~11 ms (90 Hz) with GetTickCount64 ticks (a 15.6 ms
// quantum), the game runs ~81 fps with 10-15 ms frames, and NativeGrab gated its write (>= 10 ms) and took the motor's dt on that tick clock.
// This drives the REAL sticking law (PhysicalGrab::corpse_motor, a chained prop = a severed part) with that timing and a point body that
// integrates the commanded velocity each game frame. Old: every clock is the tick. New (v1090): the hand velocity from the publication's
// precise time (sample at_s), the write gate and dt on the precise clock. Metric: the body's error to the true hand target at each game frame.
#include "../src/physical_grab.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>
using namespace d2vr;
namespace {
unsigned lcg=777;double rnd() {lcg=lcg*1664525u+1013904223u;return double(lcg>>8)/double(1u<<24);}
std::uint64_t tick_of(double t) {return static_cast<std::uint64_t>(std::floor(std::floor(t/.015625)*15.625))+1000;}   // GetTickCount64: 15.6 ms steps
Vec3 hand_at(double t,double speed) {   // running along +y with a gait bob and a sway
    return {.3+.03*std::sin(2*3.14159265*1.25*t),speed*t+.4,1.+.025*std::sin(2*3.14159265*2.5*t)};
}
struct Stat {double jitter_mm,step_mm,mean_mm;};
Stat run(bool precise,double speed) {
    lcg=777;PhysicalGrab g;WorldTrackedPose hand{};hand.basis={Vec3{1,0,0},Vec3{0,1,0},Vec3{0,0,1}};
    const Vec3 offset{0,.06,-.02};   // the held part's origin in the hand (hand frame = world: no turn)
    // press and bind at rest
    hand.position=hand_at(0,0);g.sample(hand,0,true,1,tick_of(0),precise?1e-9:0);
    g.sample(hand,1,true,1,tick_of(.011),precise?.011:0);
    WorldTrackedPose body=hand;body.position=hand.position+offset;assert(g.bind(body));
    Vec3 vel{};double last_s=0;std::uint64_t last_tick=0,bound_tick=tick_of(.011);
    double next_pub=.022,pub_t=.011;std::uint64_t pub_tick=tick_of(.011);Vec3 pub_hand=hand.position;unsigned serial=1,consumed=1;
    double t=.011;std::vector<Vec3> err;
    while(t<3.) {
        const double dt_frame=.010+.005*rnd();const double tn=t+dt_frame;
        body.position=body.position+vel*dt_frame;t=tn;
        const double v_now=t<.5?0.:speed;   // stands, then runs
        auto truth=[&](double at){return at<.5?hand_at(at,0):hand_at(at,speed)-Vec3{0,speed*.5,0};};
        while(next_pub<=t) {pub_t=next_pub;pub_tick=tick_of(pub_t);pub_hand=truth(pub_t);++serial;next_pub+=1./90;}
        if(serial!=consumed) {consumed=serial;hand.position=pub_hand;g.sample(hand,1,true,1,pub_tick,precise?pub_t:0);}
        const std::uint64_t now=tick_of(t);
        double dt;
        if(precise) {if(last_s>0 && t-last_s<.004) continue;dt=last_s>0?t-last_s:1./60;last_s=t;}
        else {if(last_tick && now-last_tick<10) continue;const auto since=last_tick?last_tick:bound_tick;dt=now>since?double(now-since)/1000.:1./60;last_tick=now;}
        CorpseMotorInput in;in.corpse=false;in.stick=true;in.chain=true;in.velocity_known=true;in.body_velocity=vel;in.object_kg=3;in.inertia_kgm2=.02;in.dt=dt;
        in.carrier={0,v_now,0};
        if(precise) in.now_s=t;
        const auto m=g.corpse_motor(body,true,now,pub_tick,in);assert(m && !m->motor.release);
        vel=m->motor.linear;
        if(t>1.2) err.push_back(body.position-(truth(t)+offset));
    }
    Vec3 mean{};for(const auto& e:err) mean=mean+e;mean=mean*(1./double(err.size()));
    double j=0,s=0;for(std::size_t i=0;i<err.size();++i) {j+=dot(err[i]-mean,err[i]-mean);if(i) s+=dot(err[i]-err[i-1],err[i]-err[i-1]);}
    return {std::sqrt(j/double(err.size()))*1000,std::sqrt(s/double(err.size()-1))*1000,length(mean)*1000};
}
}
int main() {
    for(const double speed:{0.,3.,6.}) {
        const Stat o=run(false,speed),n=run(true,speed);
        std::printf("speed %.0f m/s  old: jitter %.1f mm, frame step %.1f mm, mean lag %.1f mm | new: jitter %.1f mm, frame step %.1f mm, mean lag %.1f mm\n",
            speed,o.jitter_mm,o.step_mm,o.mean_mm,n.jitter_mm,n.step_mm,n.mean_mm);
        if(speed>0) assert(n.jitter_mm<o.jitter_mm*.6 && n.step_mm<o.step_mm*.6);
        assert(n.jitter_mm<8 && n.mean_mm<20);
    }
    std::puts("held_run_jitter_check: ok");
    return 0;
}
