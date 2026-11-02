#define NOMINMAX
#include "../src/held_copy.hpp"
#include "../src/native_power_hand.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>
// v776 lab (user, headset v775: "the weapon in the hands lags behind when I start running, especially with dual wield").
// The player ramps 0 -> 7 m/s over 0.3 s (then holds it) with the hands still in the player's frame; one game frame per row. The truth is where the arm is
// DRAWN: the post-physics attach pass writes the player's new frame (P_j) and carries the arms with it. What differs is where the weapon is put.
//   copy  : a mod-owned weapon is teleported at Stage0, before the pass: it can only know the root the previous pass wrote (P_j-1).
//           old = hand fed by the render thread + (root read now - root at the feed): lands on P_j-1 + hand. new = the same + held_copy::root_step.
//   cross : a native item riding the other hand: the engine composes the converted joint with the destination arm's live frame (F_j, written just before
//           its item is evaluated). Both palettes were uploaded in the animation stage, before the pass, against F_j-1.
//           old = power_hand::cross_joint with the models' LIVE frames: the origin arm is a sibling that the pass may not have updated yet (F_j-1) ->
//                 the item rides F_j-1; if the origin happens to be updated first (F_j) it is exact.
//           new = the PUBLISHED frames (both F_j-1): the conversion is the identity and the item takes the destination's live frame, F_j, like the arm.
namespace {
using d2vr::Vec3;
namespace ph=d2vr::power_hand;
using d2vr::rig::Transform;
using d2vr::rig::ModelFrame;
constexpr double ramp_s=.3,top_mps=7.;
double distance_at(double t) { // analytic: constant acceleration to the top speed, then constant speed
    return t<ramp_s?top_mps/ramp_s*t*t/2:top_mps*(t-ramp_s/2);
}
std::array<float,15> frame_at(double x) {return {static_cast<float>(x),0,0, 1,0,0, 0,1,0, 0,0,1, 1,1,1};} // the arm model frame: the player's root, moving along x
Transform joint_at(Vec3 p) {Transform t;t.f={1,0,0,0, 0,1,0,0, 0,0,1,0, static_cast<float>(p.x),static_cast<float>(p.y),static_cast<float>(p.z),1};return t;}
struct Row {double t,speed,step,old_copy,new_copy,old_cross,new_cross,fresh_cross;};
std::vector<Row> run(double fps,bool print) {
    const double dt=1/fps;
    const Vec3 hand{.4,-.25,1.2},palette_joint{.2,.3,.1};
    std::vector<Row> rows;
    double previous_root=0;                                       // P_j-2 at Stage0 of frame j (the service before saw it)
    if(print) std::printf("--- %.0f game frames/s: speed ramp 0 -> %.0f m/s over %.1f s; errors = weapon to drawn hand, mm ---\n"
        "  frame   t(ms)  speed   step | copy old  copy new | cross old(stale) cross old(fresh)  cross new\n",fps,top_mps,ramp_s);
    for(unsigned j=1;j<=static_cast<unsigned>(fps*.45);++j) {
        const double t=j*dt,p_now=distance_at(t),p_prev=distance_at(t-dt);
        const double speed=(p_now-p_prev)/dt,step=p_now-p_prev;
        const Vec3 drawn=Vec3{p_now,0,0}+hand;                    // the arm after the pass of frame j
        // copy: Stage0 of frame j reads P_j-1; the render thread's last feed is the hand it drew (hand at P_j-1) with the root it read then
        const Vec3 fed=Vec3{p_prev,0,0}+hand,fed_root{p_prev,0,0},now_root{p_prev,0,0};
        const auto old_copy=fed+(now_root-fed_root);
        const auto new_copy=old_copy+d2vr::held_copy::root_step(Vec3{previous_root*(j>1),0,0},now_root,j>1?dt:0);
        // cross: published frames F_j-1 (both arms), live destination frame F_j, origin live F_j-1 (stale) or F_j (updated first)
        const ModelFrame published(frame_at(p_prev)),live_dest(frame_at(p_now)),live_origin_stale(frame_at(p_prev)),live_origin_fresh(frame_at(p_now));
        const auto source=joint_at(palette_joint);
        const auto world=[&](const ModelFrame& from,const ModelFrame& to) { // what the engine draws the item at: its live destination frame * the coordinates handed back
            const auto coords=ph::cross_joint(source,from,to,source);
            return live_dest.world_point(d2vr::rig::position(coords));
        };
        const Vec3 hand_world=live_dest.world_point(palette_joint); // the origin arm drawn in F_j with the same palette
        const auto err=[&](Vec3 a,Vec3 b) {return d2vr::length(a-b)*1000;};
        Row r{t,speed,step,err(old_copy,drawn),err(new_copy,drawn),err(world(live_origin_stale,live_dest),hand_world),err(world(published,published),hand_world),err(world(live_origin_fresh,live_dest),hand_world)};
        rows.push_back(r);
        if(print) std::printf("  %5u %7.1f %6.2f %6.1f | %9.2f %9.2f | %16.2f %16.2f %10.3f\n",j,t*1000,speed,step*1000,r.old_copy,r.new_copy,r.old_cross,r.fresh_cross,r.new_cross);
        previous_root=p_prev;
    }
    return rows;
}
}
// v779 (user, headset v778: "the weapon chosen second still jitters when running"): the headset ran 70 frames/s with the frame time changing by
// 1.3 ms median, 5.7 ms p99 from one frame to the next. A run at 6 m/s with such pacing (deterministic, 10-18.6 ms frames): the copy carried by the
// last step (v776) misses by v*|dt_j - dt_j-1| every frame; scaled by this step's dt over the last one's (v779) it lands on the drawn hand.
static void paced(double& old_p90,double& new_max) {
    std::vector<double> old_err;new_max=0;unsigned seed=12345;double t=0,dt_last=0,p_last=0,p_last2=0;const double v=6;
    for(unsigned j=1;j<=400;++j) {
        seed=seed*1103515245u+12345u;const double dt=.010+.0086*((seed>>16)%1000)/999.;
        t+=dt;const double p=v*t;
        if(j>2) { // Stage0 of frame j: the roots of the last two frames, their step's dt and this step's
            const auto d=d2vr::held_copy::root_step(Vec3{p_last2,0,0},Vec3{p_last,0,0},.012);
            const auto s=d2vr::held_copy::root_step(Vec3{p_last2,0,0},Vec3{p_last,0,0},.012,dt_last,dt);
            old_err.push_back(std::abs(p_last+d.x-p)*1000);new_max=std::max(new_max,std::abs(p_last+s.x-p)*1000);
        }
        p_last2=p_last;p_last=p;dt_last=dt;
    }
    std::sort(old_err.begin(),old_err.end());old_p90=old_err[old_err.size()*9/10];
}
int main() {
    {double old_p90{},new_max{};paced(old_p90,new_max);
     std::printf("6 m/s at 10-18.6 ms frames: the last step missed the drawn hand by %.1f mm (p90); scaled by the step's dt: %.4f mm (max)\n",old_p90,new_max);
     assert(old_p90>10 && new_max<1e-3);}
    for(const double fps:{90.,60.}) {
        const auto rows=run(fps,true);
        const double dt=1/fps,accel=top_mps/ramp_s;
        for(std::size_t i=0;i<rows.size();++i) {
            const auto& r=rows[i];
            assert(std::abs(r.old_copy-r.step*1000)<1e-3);                    // the copy trails the drawn hand by exactly one frame of the player's motion
            assert(std::abs(r.old_cross-r.step*1000)<1e-3);                   // so does a crossed item whose origin arm is updated after it
            assert(r.fresh_cross<1e-3);                                       // and one whose origin arm happened to be updated first is exact
            assert(r.new_cross<1e-3);                                         // published frames: exact either way
            if(i>=1) assert(r.new_copy<=accel*dt*dt*1000+1e-3);               // the copy is within a*dt^2 while accelerating (3 mm at 90/s) ...
            if(r.t>ramp_s+2*dt) assert(r.new_copy<1e-3);                      // ... and exact at a steady speed
        }
        const auto& top=rows.back();
        assert(std::abs(top.old_copy-top_mps/fps*1000)<1e-3 && top.new_copy<1e-3);
        std::printf("at %.0f m/s and %.0f frames/s the old weapon trailed the hand by %.1f mm; new: copy %.2f mm, crossed item %.3f mm (a*dt^2 = %.2f mm)\n",
            top_mps,fps,top.old_copy,top.new_copy,top.new_cross,accel*dt*dt*1000);
    }
    std::printf("at 6 m/s: 90 frames/s -> %.1f mm, 60 frames/s -> %.1f mm one frame of root motion\n",6./90*1000,6./60*1000);
    std::printf("weapon follow lab ok\n");
    return 0;
}
