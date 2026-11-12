// v1249 lab: the engine render pose of a physically held prop (held_render_pose.hpp). A prop is held through PhysicalGrab::corpse_motor (the sticking
// hold) while the player walks/runs with jittered frames; each frame the slot (HeldRenderSlot) decides what the engine's rigid-body sync is given.
// Metrics: (1) the render pose against the true hold goal at the motor's time (target < 2 mm p90; the drawn placement refines the rest) vs today's
// engine pose = the body (motor lag); (2) a body blocked by a wall leaves the render pose at the hand; (3) a release (throw / drop) hands the render
// frame back to the body without a jump: first frame = the last pose given, last frame = the body, monotonic, frames after blend_s untouched;
// (4) a stalled publication blends out on its own; (5) the rotation part of the blend is exact at both ends.
// v1253 ragdolls: (6) a severed clone - the engine's contract (static RE, held_render_pose.hpp): bones are queued in WORLD space and stored as
// palette = inverse(frame) * bone world with the frame of the same tick, drawn at model frame * palette. Moving every bone request and the frame by
// D = goal * inverse(held body) leaves the palette unchanged (model-to-root offsets preserved) and draws every bone at D * bone, the held one at the goal;
// moving the frame alone draws nothing elsewhere (why both are moved). (7) a corpse held by one bone: a translation, capped at 0.6 m, no turn.
#define NOMINMAX
#include "../src/held_render_pose.hpp"
#include <algorithm>
#include <cstdio>
#include <vector>
using namespace d2vr;
static int failures=0;
#define CHECK(x) do {if(!(x)) {std::printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#x);++failures;}} while(0)
static unsigned lcg=7;static double rnd() {lcg=lcg*1664525u+1013904223u;return double(lcg>>8)/double(1u<<24);}
static WorldTrackedPose yawed(Vec3 p,double yaw) {
    WorldTrackedPose w{};w.position=p;const double c=std::cos(yaw),s=std::sin(yaw);
    w.basis={Vec3{c,s,0},Vec3{-s,c,0},Vec3{0,0,1}};return w;
}
static WorldTrackedPose rotated(WorldTrackedPose w,Vec3 spin,double dt) {
    const double a=length(spin)*dt;if(a<1e-12) return w;const Vec3 k=spin*(1/length(spin));
    for(auto& b:w.basis) b=b*std::cos(a)+cross(k,b)*std::sin(a)+k*(dot(k,b)*(1-std::cos(a)));
    return w;
}
static double angle(const WorldTrackedPose& a,const WorldTrackedPose& b) {return length(grab_rotation_error(a,b));}
static double p90(std::vector<double> v) {if(v.empty()) return 0;std::sort(v.begin(),v.end());return v[static_cast<std::size_t>(.9*double(v.size()-1))];}
struct Run {double render_p90_mm,body_p90_mm,wall_render_mm,wall_body_mm;};
// speed: the player's m/s along +x with the hand swinging +-swing m at 1.5 Hz; wall_x: the body cannot pass this x (0 = no wall)
static Run run(double speed,double swing,double wall_x=0) {
    lcg=7;PhysicalGrab::goal_feed=true;PhysicalGrab g;HeldRenderSlot slot;const Vec3 rel{.12,.03,-.02};
    const auto hand_at=[&](double t) {return yawed(Vec3{speed*t,swing*std::sin(t*9.42),1.2},.3*std::sin(t*3));};
    const auto goal_at=[&](double t) {const auto h=hand_at(t);auto r=h;r.position=h.position+h.basis[0]*rel.x+h.basis[1]*rel.y+h.basis[2]*rel.z;return r;};
    double t=.011;std::uint64_t tick=1011;g.sample(hand_at(0),0,true,1,1000,1e-6);g.sample(hand_at(t),1,true,1,tick,t);
    WorldTrackedPose body=goal_at(t);if(!g.bind(body)) {std::printf("bind failed\n");++failures;return {};}
    Vec3 v{},w{};double last=t;GrabTuning tuning;std::vector<double> render,lag;double wall_r=0,wall_b=0;
    while(t<2.) {
        const double dt=.008+.014*rnd();
        body.position=body.position+v*dt;body=rotated(body,w,dt);if(wall_x>0 && body.position.x>wall_x) body.position.x=wall_x;
        t+=dt;tick=1000+static_cast<std::uint64_t>(t*1000);
        const double sample_t=t-.004*rnd();g.sample(hand_at(sample_t),1,true,1,tick,sample_t);   // the newest hand is 0-4 ms old at the motor
        CorpseMotorInput in;in.corpse=false;in.stick=true;in.velocity_known=true;in.body_velocity=v;in.body_spin=w;in.object_kg=1;in.inertia_kgm2=.05;
        in.dt=t-last;in.tuning=&tuning;in.now_s=t;in.carrier=Vec3{speed,0,0};last=t;
        const auto m=g.corpse_motor(body,true,tick,tick,in);
        if(!m || m->motor.release) {std::printf("released at %.3f\n",t);++failures;return {};}
        v=m->motor.linear;w=m->motor.angular;
        slot.publish(1,g.last_goal(),t);
        WorldTrackedPose engine{};bool blending=false;
        CHECK(slot.decide(body,t+.0005,engine,blending) && !blending);   // POST_PHYSICS runs right after the step
        if(t>.3) {
            const auto truth=goal_at(t);
            render.push_back(length(engine.position-truth.position)*1000+angle(engine,truth)*120);   // a point 12 cm off the origin (mm)
            lag.push_back(length(body.position-truth.position)*1000+angle(body,truth)*120);
            if(wall_x>0) {wall_r=std::max(wall_r,length(engine.position-truth.position)*1000);wall_b=std::max(wall_b,length(body.position-truth.position)*1000);}
        }
    }
    return {p90(render),p90(lag),wall_r,wall_b};
}
int main() {
    for(const auto& c:{std::pair<double,double>{0,.05},{1.4,.08},{5.,.12}}) {
        const auto r=run(c.first,c.second);
        std::printf("speed %.1f m/s, hand swing %.2f m: engine render pose p90 %.2f mm (today: the body, p90 %.2f mm)\n",c.first,c.second,r.render_p90_mm,r.body_p90_mm);
        CHECK(r.render_p90_mm<2);CHECK(r.body_p90_mm>r.render_p90_mm);
    }
    {   // the body stopped by a wall 1.8 m ahead while the player walks on: the render pose stays in the hand
        const auto r=run(1.4,.02,1.8);
        std::printf("walk into a wall: render worst %.2f mm from the goal, body worst %.0f mm\n",r.wall_render_mm,r.wall_body_mm);
        CHECK(r.wall_render_mm<3 && r.wall_body_mm>300);
    }
    {   // release: a throw at 6 m/s whose body trails the hand by 15 cm and 0.4 rad when the grip opens
        HeldRenderSlot slot;const auto held=yawed({1,0,1.2},.2);slot.publish(7,held,1.);
        WorldTrackedPose out{};bool blending=false;auto body=yawed({.85,.02,1.18},-.2);
        CHECK(slot.decide(body,1.001,out,blending) && !blending && length(out.position-held.position)<1e-9);
        slot.end(1.002);double t=1.002,prev_offset=1e9;const Vec3 throw_v{6,0,1};int frames=0;bool first=true;WorldTrackedPose prev=out;
        while(true) {
            t+=.011;body.position=body.position+throw_v*.011;
            if(!slot.decide(body,t,out,blending)) break;
            ++frames;CHECK(blending);
            if(first) {   // the first blended frame starts from the last pose given: only the first share of the offset is taken off
                CHECK(length(out.position-prev.position)<=.011/HeldRenderSlot::blend_s*length(prev.position-body.position)+1e-3);first=false;
            }
            const double offset=length(out.position-body.position);CHECK(offset<=prev_offset+1e-9);prev_offset=offset;
            CHECK(length(out.position-prev.position)<length(throw_v*.011)+.15/(HeldRenderSlot::blend_s/.011)+.01);   // no step beyond the body's own + the offset's share
            prev=out;
        }
        std::printf("release blend: %d frames, then the body's own pose\n",frames);
        CHECK(frames>=10 && frames<=15);CHECK(!slot.entity);
        CHECK(!slot.decide(body,t+.011,out,blending));
    }
    {   // a publication that stops without an end (stalled dispatcher) blends out from fresh_s after the last one
        HeldRenderSlot slot;slot.publish(9,yawed({0,0,1},0),5.);WorldTrackedPose out{};bool b=false;const auto body=yawed({.2,0,1},.5);
        CHECK(slot.decide(body,5.05,out,b) && !b);
        CHECK(slot.decide(body,5.12,out,b) && b);
        CHECK(!slot.decide(body,5.3,out,b));
    }
    {   // rotation of the blend: w=0 is body*offset exactly, w=1 the body
        const auto body=yawed({0,0,0},.1),render=rotated(yawed({.1,0,0},1.2),Vec3{.3,-.2,.5},1.);
        const auto a=render_blend(body,render.position-body.position,grab_rotation_error(body,render),0),z=render_blend(body,render.position-body.position,grab_rotation_error(body,render),1);
        std::printf("blend ends: w=0 %.2e rad / %.2e m from the render pose, w=1 %.2e rad from the body\n",angle(a,render),length(a.position-render.position),angle(z,body));
        CHECK(angle(a,render)<1e-6 && length(a.position-render.position)<1e-9 && angle(z,body)<1e-9);
    }
    {   // (6) severed clone: three bones, the frame from bone 0, the hand holds bone 2 (a standalone/root body away from the frame body)
        const auto inv_mul=[](const WorldTrackedPose& f,const WorldTrackedPose& b) {   // inverse(f) * b (local pose of b in f)
            WorldTrackedPose l{};const Vec3 d=b.position-f.position;l.position={dot(f.basis[0],d),dot(f.basis[1],d),dot(f.basis[2],d)};
            for(unsigned i=0;i<3;++i) l.basis[i]={dot(f.basis[0],b.basis[i]),dot(f.basis[1],b.basis[i]),dot(f.basis[2],b.basis[i])};return l;};
        const auto mul=[](const WorldTrackedPose& f,const WorldTrackedPose& l) {   // f * l
            WorldTrackedPose w{};w.position=f.position+f.basis[0]*l.position.x+f.basis[1]*l.position.y+f.basis[2]*l.position.z;
            for(unsigned i=0;i<3;++i) w.basis[i]=f.basis[0]*l.basis[i].x+f.basis[1]*l.basis[i].y+f.basis[2]*l.basis[i].z;return w;};
        const auto to_f=[](const WorldTrackedPose& p,float* pos,float* rot) {pos[0]=float(p.position.x);pos[1]=float(p.position.y);pos[2]=float(p.position.z);
            for(unsigned i=0;i<3;++i) {rot[i*3]=float(p.basis[i].x);rot[i*3+1]=float(p.basis[i].y);rot[i*3+2]=float(p.basis[i].z);}};
        const auto from_f=[](const float* pos,const float* rot) {WorldTrackedPose p{};p.position={pos[0],pos[1],pos[2]};
            for(unsigned i=0;i<3;++i) p.basis[i]={rot[i*3],rot[i*3+1],rot[i*3+2]};return p;};
        const WorldTrackedPose bones[3]={rotated(yawed({1,2,.3},.4),Vec3{.2,.1,.3},1.),rotated(yawed({1.2,2.1,.35},-.7),Vec3{-.4,.2,0},1.),rotated(yawed({1.1,1.8,.25},2.),Vec3{0,.5,.1},1.)};
        const auto frame=bones[0];const auto goal=rotated(yawed({.4,1.5,1.3},.9),Vec3{.1,-.3,.2},1.);
        const HoldDelta d{bones[2],goal};
        WorldTrackedPose moved_frame{};{float p[3],r[9];to_f(frame,p,r);d.apply(p,r);moved_frame=from_f(p,r);}
        double palette_err=0,draw_err=0,frame_only_err=1e9;
        for(unsigned b=0;b<3;++b) {
            float p[3],r[9];to_f(bones[b],p,r);d.apply(p,r);const auto moved=from_f(p,r);
            const auto palette_old=inv_mul(frame,bones[b]),palette_new=inv_mul(moved_frame,moved);
            palette_err=std::max(palette_err,length(palette_new.position-palette_old.position)+angle(palette_new,palette_old));
            const auto drawn=mul(moved_frame,palette_new),want=b==2?goal:WorldTrackedPose{d.point(bones[b].position),{d.turn(bones[b].basis[0]),d.turn(bones[b].basis[1]),d.turn(bones[b].basis[2])}};
            draw_err=std::max(draw_err,length(drawn.position-want.position)+angle(drawn,want));
            const auto frame_only=mul(moved_frame,inv_mul(moved_frame,bones[b]));   // the frame moved, the bones not: re-derived palette, drawn where the bodies are
            frame_only_err=std::min(frame_only_err,length(frame_only.position-bones[b].position));
        }
        std::printf("severed clone: palette change %.2e, drawn vs D*bone %.2e (held bone at the goal), frame-only move leaves bones %.2e m from the bodies\n",palette_err,draw_err,frame_only_err);
        CHECK(palette_err<1e-5 && draw_err<1e-5 && frame_only_err<1e-5);
    }
    {   // (7) corpse held by one bone: translation by the lag, capped, no turn
        const auto bone=yawed({0,0,.2},.3),goal=rotated(yawed({.3,.4,.2},1.),Vec3{0,0,1},.5);
        const auto t=translated_goal(bone,goal,HeldRenderPose::corpse_cap_m);
        CHECK(length(t.position-goal.position)<1e-12 && angle(t,bone)<1e-12);
        const auto capped=translated_goal(bone,yawed({2,0,.2},0),HeldRenderPose::corpse_cap_m);
        CHECK(std::abs(length(capped.position-bone.position)-.6)<1e-12 && angle(capped,bone)<1e-12);
        const HoldDelta d{bone,t};const Vec3 foot{-.8,.3,.1};
        CHECK(length(d.point(foot)-(foot+(t.position-bone.position)))<1e-12);
        std::printf("corpse: translation only (%.2f m), capped at %.2f m\n",length(t.position-bone.position),HeldRenderPose::corpse_cap_m);
    }
    std::printf(failures?"held_render_pose_check: %d FAILED\n":"held_render_pose_check: PASS\n",failures);
    return failures?1:0;
}
