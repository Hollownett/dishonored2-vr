// v1238 lab: the sticking hold law (PhysicalGrab::corpse_motor, a held prop / severed part / corpse limb) against a goal that moves at constant
// velocity (the player walking or running, the hand still in the rig), and against a goal that swings round the hand (a smooth turn, a turning
// wrist). Each game frame (8-22 ms, jittered) samples the hand on the precise clock, runs the motor and integrates a point body with the
// commanded velocity (orientation rotated with the commanded spin). Metric: the body's distance to the true goal at the motor's time.
// v1090 law (goal_feed=false): feed = the hand's velocity. v1238 law: feed = the goal's own velocity (hand * relative). A capped (weighted,
// build/legacy-weighted-hold) corpse limb yanked by the hand must still lag: the cap is untouched.
#include "../src/physical_grab.hpp"
#include <cmath>
#include <cstdio>
using namespace d2vr;
static int failures=0;
#define CHECK(x) do {if(!(x)) {std::printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#x);++failures;}} while(0)
static unsigned lcg=99;static double rnd() {lcg=lcg*1664525u+1013904223u;return double(lcg>>8)/double(1u<<24);}
static WorldTrackedPose yawed(Vec3 p,double yaw) {
    WorldTrackedPose w{};w.position=p;const double c=std::cos(yaw),s=std::sin(yaw);
    w.basis={Vec3{c,s,0},Vec3{-s,c,0},Vec3{0,0,1}};return w;
}
static WorldTrackedPose rotated(WorldTrackedPose w,Vec3 spin,double dt) {   // Rodrigues on each axis
    const double a=length(spin)*dt;if(a<1e-12) return w;
    const Vec3 k=spin*(1/length(spin));
    for(auto& b:w.basis) b=b*std::cos(a)+cross(k,b)*std::sin(a)+k*(dot(k,b)*(1-std::cos(a)));
    return w;
}
struct Out {double settled_mm,stop_overshoot_mm;};
// speed: player m/s along +x; omega: the player's yaw rate about a pivot 0.4 m behind the hand (rad/s); stop_at: the player stops (s, 0 = never)
static Out run(bool goal_feed,double speed,double omega,double stop_at,bool weighted=false,double yank=0,double clock_jitter=0) {
    PhysicalGrab::goal_feed=goal_feed;lcg=99;
    PhysicalGrab g;const Vec3 pivot0{-.4,0,1};const Vec3 rel{.15,.05,0};   // the held body's origin 15 cm along the hand's forward
    const auto hand_at=[&](double t) {
        const double tm=stop_at>0?std::min(t,stop_at):t;
        const double yaw=omega*tm;const Vec3 pivot=pivot0+Vec3{speed*tm,0,0};
        const double c=std::cos(yaw),s=std::sin(yaw);
        Vec3 p=pivot+Vec3{.4*c,.4*s,0};if(yank>0 && t>.6) p=p+Vec3{0,0,std::min((t-.6)*yank,.5)};
        return yawed(p,yaw);
    };
    const auto goal_at=[&](double t) {const auto h=hand_at(t);return h.position+h.basis[0]*rel.x+h.basis[1]*rel.y+h.basis[2]*rel.z;};
    double t=0;std::uint64_t tick=1000;
    auto hand=hand_at(0);g.sample(hand,0,true,1,tick,1e-6);
    t=.011;tick+=11;hand=hand_at(t);g.sample(hand,1,true,1,tick,t);
    WorldTrackedPose body=hand;body.position=goal_at(t);if(!g.bind(body)) {std::printf("bind failed\n");++failures;return {};}
    Vec3 v{},w{};double worst_settled=0,overshoot=0,last=t;
    GrabTuning tuning;
    while(t<1.5) {
        const double dt=.008+.014*rnd();
        body.position=body.position+v*dt;body=rotated(body,w,dt);t+=dt;tick=1000+static_cast<std::uint64_t>(t*1000);
        hand=hand_at(t);g.sample(hand,1,true,1,tick,t+(rnd()-.5)*2*clock_jitter);   // clock_jitter: the sample's clock is its arrival, +- this from the pose's own time
        CorpseMotorInput in;in.corpse=weighted;in.stick=!weighted;in.velocity_known=true;in.body_velocity=v;in.body_spin=w;in.object_kg=weighted?8:1;
        in.inertia_kgm2=.05;in.dt=t-last;in.tuning=&tuning;in.now_s=t;
        in.carrier=stop_at>0 && t>=stop_at?Vec3{}:Vec3{speed,0,0};last=t;
        const auto m=g.corpse_motor(body,true,tick,tick,in);
        if(!m || m->motor.release) {std::printf("released at %.3f\n",t);++failures;return {};}
        v=m->motor.linear;w=m->motor.angular;
        const double err=length(goal_at(t)-body.position)*1000;
        if(t>.3 && (stop_at<=0 || t<stop_at) && (yank<=0 || t<.6)) worst_settled=std::max(worst_settled,err);
        if(stop_at>0 && t>=stop_at) overshoot=std::max(overshoot,dot(body.position-goal_at(t),Vec3{1,0,0})*1000);
        if(yank>0 && t>.6) overshoot=std::max(overshoot,err);   // yank: the lag behind the hand (the cap at work)
    }
    return {worst_settled,overshoot};
}
int main() {
    for(const double speed:{1.4,6.}) {
        const auto o=run(false,speed,0,0),n=run(true,speed,0,0);
        std::printf("walk %.1f m/s, still hand: worst gap after settle v1090 %.2f mm, v1238 %.2f mm\n",speed,o.settled_mm,n.settled_mm);
        CHECK(n.settled_mm<2);
    }
    {   // a smooth turn at 2 rad/s (115 deg/s) while walking 1.4 m/s: the goal swings round the hand
        const auto o=run(false,1.4,2.,0),n=run(true,1.4,2.,0);
        std::printf("walk 1.4 m/s + turn 2 rad/s: worst gap v1090 %.2f mm, v1238 %.2f mm\n",o.settled_mm,n.settled_mm);
        CHECK(n.settled_mm<3 && o.settled_mm>n.settled_mm*2);
    }
    {   // a stop from 6 m/s: how far the body runs past the goal (the velocity estimate decays over its 35 ms average in both laws)
        const auto o=run(false,6.,0,.8),n=run(true,6.,0,.8);
        std::printf("stop from 6 m/s: overshoot v1090 %.1f mm, v1238 %.1f mm\n",o.stop_overshoot_mm,n.stop_overshoot_mm);
        CHECK(n.stop_overshoot_mm<=o.stop_overshoot_mm+.5);
    }
    for(const double speed:{1.4,6.}) {   // the hand sample's clock = its arrival, +-2 ms off the pose's own time: velocity noise either law
        const auto o=run(false,speed,0,0,false,0,.002),n=run(true,speed,0,0,false,0,.002);
        std::printf("walk %.1f m/s, +-2 ms sample clock: worst gap v1090 %.2f mm, v1238 %.2f mm\n",speed,o.settled_mm,n.settled_mm);
        CHECK(n.settled_mm<=o.settled_mm+1);
    }
    {   // the weighted (capped) law: an 8 kg limb yanked up at 3 m/s still lags behind the hand
        const auto n=run(true,0,0,0,true,3.);
        std::printf("weighted 8 kg yank 3 m/s: worst lag %.0f mm (capped law kept)\n",n.stop_overshoot_mm);
        CHECK(n.stop_overshoot_mm>20);
    }
    if(failures) {std::printf("hold_feedforward_check: %d failure(s)\n",failures);return 1;}
    std::printf("PASS: hold_feedforward_check\n");return 0;
}
