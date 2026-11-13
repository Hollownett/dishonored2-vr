// v1266/v1270 lab: one pose per presented frame for a held loose prop AND a held severed limb, while standing, walking, running, stick turning and snap
// turning - the model of the v1264 headset session (00:34-00:38) and the reason v1264 still ghosted on the move.
// Engine order it models (the game runs ONE game frame per render: held_render_pose.published ~= renders; the bursts' 15/16/0 ms "ticks" are
// GetTickCount64's 15.6 ms resolution, not game ticks): game frame k = physics, the held thing's POST_PHYSICS sync (the engine is given its pose
// P_k), the post-physics attach jobs (the body model root of frame k), the Setup of frame k (a publication: rig_k), then - on the render thread,
// concurrently with game frame k+1 - Begin k (the publisher notes the drawn hold of image k with rig_k) and the draws of render k, composed from P_k.
// At the sync of frame k the body-model root is still frame k-1's (the attach job runs after it); the heading is frame k's (the turn is applied in
// the physics step). Begin k-1 has or has not happened before sync k (the game thread up to one frame ahead).
// 58 % of renders draw the thing at the engine pose (the v1264 census), the rest at B. The certificate (weapon_draw::Place one_pose) is fed as the
// publisher feeds it. Severed limb: a jointed clone - the model frame is another body of the clone, flopping against the held body; the engine moves
// the whole clone so the held body is at the pose it was given; placement B = H * inverse(G) * F.
//   v1264: the v1262 rig rule (stale-or-current guessed against the noted rig, velocity from sync readings); the limb given the bare motor goal.
//   v1266: the root one fixed frame on from the reading (the step between the previous two readings), the heading as read; the limb carried too.
//   v1270 (shared step slot): measured only when the last character step before the sync was the player's (an NPC step after it: fallback).
//   v1274: this frame's root read from the player's own reference frame, stamped with the frame (held_rig_now); NPC steps write their own.
#define NOMINMAX
#include "../src/held_render_pose.hpp"
#include "../src/held_engine_pose.hpp"
#include "../src/weapon_draw_place.hpp"
#include <cstdio>
#include <vector>
#include <limits>
using namespace d2vr;
namespace wd=d2vr::weapon_draw;
static int failures=0;
#define CHECK(x) do {if(!(x)) {std::printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#x);++failures;}} while(0)
static unsigned lcg=11;static double rnd() {lcg=lcg*1664525u+1013904223u;return double(lcg>>8)/double(1u<<24);}
static WorldTrackedPose yawed(Vec3 p,double yaw) {
    WorldTrackedPose w{};w.position=p;const double c=std::cos(yaw),s=std::sin(yaw);
    w.basis={Vec3{c,s,0},Vec3{-s,c,0},Vec3{0,0,1}};return w;
}
static WorldTrackedPose tilt(WorldTrackedPose w,Vec3 axis,double a) {   // rotate the basis about a world axis
    const Vec3 k=axis*(1/length(axis));
    for(auto& b:w.basis) b=b*std::cos(a)+cross(k,b)*std::sin(a)+k*(dot(k,b)*(1-std::cos(a)));
    return w;
}
// The v1262 rule, verbatim in effect (held_render_pose.hpp before v1266): a reading equal to the noted rig is "stale" and extrapolated by the velocity
// between the last two readings over this sync interval; any other reading is used as read.
struct V1262Rig {
    HeldCarry seen{};double seen_s{},seen_dt{};bool have_seen{},stepped{};
    HeldCarry of(const HeldCarry& read,double now,const HeldCarry& noted) {
        HeldCarry out=read;stepped=false;const double dt=have_seen?now-seen_s:0.;
        const bool unmoved=length(read.root-noted.root)<1e-4 && std::abs(std::remainder(read.heading-noted.heading,6.283185307179586))<1e-5;
        if(unmoved && have_seen && dt>0 && dt<.05 && seen_dt>0 && seen_dt<.05) {
            const Vec3 v=(read.root-seen.root)*(1/seen_dt);const double w=std::remainder(read.heading-seen.heading,6.283185307179586)/seen_dt;
            if(length(v)<15) {out.root=read.root+v*dt;stepped=true;}
            if(std::abs(w)<6) {out.heading=read.heading+w*dt;}
        }
        seen_dt=dt;seen=read;seen_s=now;have_seen=true;return out;
    }
};
// rate: frames per second; stop_at / start_at (s): the player stops / starts there; hitch_at: one frame 3x long; current_every: every n-th sync's root
// reading is already this frame's (an attach job that ran before the sync)
// npc_every: an NPC's character step runs after the player's on every n-th frame; step_after_every: the player's own step runs after the sync on
// every n-th frame (its stamp is the previous frame's)
struct Motion {const char* name{};double speed{},yaw_rate{},snap{},rate{90},stop_at{},start_at{},hitch_at{};unsigned current_every{},npc_every{},step_after_every{};};
struct Result {double shown_p90{},flip_p90{},flip_max{},eye_max{},gap_p90{},gap_max{};std::uint64_t renders{},unplaced{},stepped{},carried{},measured{},fallback{};};
static double edge_p(const wd::Place::OneHist& h,double q) {
    std::uint64_t t=0;for(auto v:h.n) t+=v;std::uint64_t run=0;
    for(unsigned i=0;i<wd::Place::OneHist::bins;++i) {run+=h.n[i];if(t && double(run)>=q*double(t)) return wd::Place::OneHist::edge[i];}
    return 0;
}
static wd::Place::OneHist merged(const wd::Place::OneHist& a,const wd::Place::OneHist& b) {auto o=a;for(unsigned i=0;i<o.n.size();++i) o.n[i]+=b.n[i];o.max=std::max(a.max,b.max);return o;}
// v1264: the v1262 rule; v1266: the predicted step (PlayerRigTracker without a frame reading); v1270: measured only when the shared step slot holds
// the player's step; v1274: the player's own stamped reference frame
enum Mode {v1264,v1266,v1270,v1274};
static double begin_before_p=.9;
static Result run(const Motion& m,Mode mode,bool limb) {
    lcg=11;HeldRenderSlot::drawn_hold=true;wd::Place::clear();wd::Place::reset_one_pose();
    HeldRenderSlot slot;V1262Rig old;PlayerRigTracker tracker;const std::uintptr_t entity=0x1234;
    const WorldTrackedPose rel=tilt(yawed({.10,.02,-.03},.4),{1,0,0},.3);          // the held body in the drawn grip
    const WorldTrackedPose motor_off=tilt(yawed({.035,.018,-.01},.12),{0,1,0},.06); // the motor's hand vs the drawn hand (~40 mm, ~8 deg)
    const auto track_at=[&](double t) {   // the hand in the rig (tracking): a 2 cm sway and wrist turn (~0.13 m/s, ~0.4 rad/s)
        return tilt(yawed({.35+.02*std::sin(t*6.3),-.2+.015*std::sin(t*4.1),1.1+.01*std::sin(t*7.7)},.2*std::sin(t*2.)),{1,0,0},.1*std::sin(t*3.3));};
    const auto joint_at=[&](double t) {   // limb: the clone's frame body against the held body (a 30 cm lever flopping +-17 deg)
        return limb?tilt(yawed({.3,0,-.05},.3*std::sin(t*3.)),{0,1,0},.2*std::sin(t*2.3)):yawed({0,0,0},0);};
    const auto speed_at=[&](double t) {return (m.stop_at>0 && t>=m.stop_at) || (m.start_at>0 && t<m.start_at)?0.:m.speed;};
    // game frames, one per render (rate Hz, +-0.25 ms), each with its rig: the turn first (the physics step's delegate), then the root moves
    std::vector<double> T{0};std::vector<HeldCarry> rig{HeldCarry{{0,0,0},0}};
    while(T.back()<3.2) {
        double dt=1./m.rate+(rnd()-.5)*.0005;if(m.hitch_at>0 && T.back()<m.hitch_at && T.back()+dt>=m.hitch_at) dt*=3;
        HeldCarry r=rig.back();
        r.heading+=m.yaw_rate*dt;if(m.snap!=0 && std::floor((T.back()+dt)/.5)>std::floor(T.back()/.5)) r.heading+=m.snap;
        r.root=r.root+Vec3{std::cos(r.heading),std::sin(r.heading),0}*(speed_at(T.back()+dt)*dt);
        T.push_back(T.back()+dt);rig.push_back(r);
    }
    const auto drawn_at=[&](std::size_t k) {return grip_compose(grip_compose(yawed(rig[k].root,rig[k].heading),track_at(T[k])),rel);};   // B (body) of render k
    std::vector<double> gaps;std::uint64_t stepped=0,carried=0;
    std::size_t last_note=0;bool noted=false;
    for(std::size_t k=2;k+1<T.size();++k) {
        const bool begin_before=rnd()<begin_before_p;   // v1264 note ages: 90 % of syncs found a note <= 10 ms old (Begin k-1 already done)
        const auto note=[&](std::size_t j) {slot.note_drawn(drawn_at(j),T[j]+.004,&rig[j]);last_note=j;noted=true;};
        if(begin_before) note(k-1);
        // the motor of frame k: the newest publication's hand (frame k-1) led by the player's velocity to frame k
        const auto sample=grip_compose(grip_compose(yawed(rig[k-1].root,rig[k-1].heading),track_at(T[k-1])),motor_off);
        auto goal=grip_compose(sample,rel);goal.position=goal.position+Vec3{std::cos(rig[k].heading),std::sin(rig[k].heading),0}*(speed_at(T[k])*(T[k]-T[k-1]));
        slot.publish(entity,goal,T[k]);
        const bool current=m.current_every && k%m.current_every==0;   // the attach job ran before this sync
        const bool step_after=m.step_after_every && k%m.step_after_every==0,npc_after=m.npc_every && k%m.npc_every==0;
        const bool frame_ok=(mode==v1274 && !step_after) || (mode==v1270 && !step_after && !npc_after);
        HeldCarry read{current?rig[k].root:rig[k-1].root,rig[k].heading,rig[k].root,frame_ok,0x5000,frame_ok?HeldCarry::taken:HeldCarry::not_this_frame};
        WorldTrackedPose given{};bool blending=false;const double now=T[k]+.002;
        if(mode==v1264) {
            if(limb) given=goal;   // v1264: a severed part was given the bare motor goal
            else {const auto r=old.of(read,now,noted?rig[last_note]:read);given=noted?held_carried(slot.drawn,slot.drawn_carry,r):goal;++carried;if(old.stepped) ++stepped;}
        } else {
            const auto r=tracker.frame_rig(read,now);slot.decide(goal,now,given,blending,&r);
            if(slot.used==HeldRenderSlot::given_carried) ++carried;
        }
        if(!begin_before) note(k-1);
        // render k: composed from the given pose; the model frame F = given * J (the clone's frame body; a prop: J = identity)
        const double t=T[k];const auto j=joint_at(t);const auto f=grip_compose(given,j),h=drawn_at(k);
        const auto b=grip_compose(h,j);   // B = H * inverse(G) * F = H * J
        std::array<float,15> raw{};const wd::Mat fm=wd::pose_mat(f),bm=wd::pose_mat(b);HeldEnginePose::frame_from_pose(fm.data(),raw.data());
        if(t>=.3 && t-1./m.rate<.3) {wd::Place::reset_one_pose();gaps.clear();stepped=carried=0;}
        if(t>=.3) gaps.push_back(length(f.position-b.position)*1000);
        const double yaw_dps=std::remainder(rig[k].heading-rig[k-1].heading,6.283185307179586)/(T[k]-T[k-1])*57.29577951308232;
        wd::Place::stage(0,limb?wd::Place::kind_severed:wd::Place::kind_prop);wd::Place::stage_motion(0,speed_at(t),yaw_dps,&fm,&fm);
        wd::Place::publish_bound(static_cast<std::uint32_t>(100+k),{raw.data(),nullptr},{&bm,nullptr},{.5,0.},{entity,0},t);
        if(rnd()>=.58) for(int eye=0;eye<2;++eye) {wd::Result r;r.side=0;r.eye=eye;r.stage=wd::placed_pass;wd::Place::census(r,true,static_cast<std::uint32_t>(100+k));wd::Place::census(r,false,static_cast<std::uint32_t>(100+k));}
    }
    wd::Place::publish_bound(9999,{nullptr,nullptr},{nullptr,nullptr});   // rolls the last render
    wd::Place::OneCell all{};
    for(unsigned b=0;b<wd::Place::buckets;++b) {const auto c=wd::Place::one_pose_cell(limb?wd::Place::kind_severed:wd::Place::kind_prop,b);
        all.renders+=c.renders;all.unplaced+=c.unplaced;all.shown=merged(all.shown,c.shown);all.flip=merged(all.flip,c.flip);all.eye=merged(all.eye,c.eye);}
    std::sort(gaps.begin(),gaps.end());
    return {edge_p(all.shown,.9),edge_p(all.flip,.9),all.flip.max,all.eye.max,gaps.empty()?0:gaps[static_cast<std::size_t>(.9*double(gaps.size()-1))],gaps.empty()?0:gaps.back(),
            all.renders,all.unplaced,stepped,carried,tracker.measured,tracker.fallback};
}
int main() {
    const Motion motions[]={{"still",0,0,0},{"walk 1.5 m/s",1.5,0,0},{"walk 3 m/s",3,0,0},{"run 7 m/s",7,0,0},{"walk 1.5 + stick turn 90/s",1.5,1.571,0},
                            {"run 5 + stick turn 60/s",5,1.047,0},{"walk 1.5 + snap 45 deg",1.5,0,.785}};
    const Motion events[]={{"stop from 3 m/s, 90 Hz",3,0,0,90,1.},{"stop from 3 m/s, 120 Hz",3,0,0,120,1.},{"stop from 7 m/s, 90 Hz",7,0,0,90,1.},{"stop from 7 m/s, 120 Hz",7,0,0,120,1.},
                           {"start to 3 m/s, 90 Hz",3,0,0,90,0,1.},{"start to 7 m/s, 120 Hz",7,0,0,120,0,1.},{"run 7, one 3x hitch frame",7,0,0,90,0,0,1.},
                           {"run 7, every 5th reading current",7,0,0,90,0,0,0,5},{"run 7, player step after sync 1/7",7,0,0,90,0,0,0,0,0,7}};
    for(const bool limb:{false,true}) {
        for(const auto& m:motions) {
            const auto before=run(m,v1264,limb),after=run(m,v1274,limb);
            std::printf("%-5s %-33s v1264: gap p90 %5.1f, flip p90 <=%5.1f max %6.1f | v1274: gap p90 %4.1f, flip p90 <=%4.1f max %5.1f, eye %.1f (%llu renders, %llu unplaced)\n",
                limb?"limb":"prop",m.name,before.gap_p90,before.flip_p90,before.flip_max,after.gap_p90,after.flip_p90,after.flip_max,after.eye_max,
                static_cast<unsigned long long>(after.renders),static_cast<unsigned long long>(after.unplaced));
            CHECK(after.renders>200 && after.unplaced>after.renders/3);
            if(m.speed>=3 || m.yaw_rate>0 || m.snap>0) CHECK(before.flip_p90>=20);   // v1264 reproduces the moving ghost
            CHECK(after.flip_p90<=5 && after.flip_max<=10 && after.gap_p90<=5 && after.eye_max==0);
        }
        for(const auto& m:events) {   // v1266's predicted step fails each event; v1274's measured rig does not
            const auto v66=run(m,v1266,limb),v70=run(m,v1274,limb);
            // gap = the engine pose against the drawn hold: exactly what a render whose draws are not placed shows (the event's flip whatever the
            // placement did on that render); flip = the certificate's frame-to-frame figure on this run's placed/unplaced pattern
            std::printf("%-5s %-33s v1266: gap max %6.1f mm, flip max %6.1f | v1274: gap max %4.1f mm, flip max %4.1f, flip p90 <=%3.1f (%llu measured, %llu fallback)\n",
                limb?"limb":"prop",m.name,v66.gap_max,v66.flip_max,v70.gap_max,v70.flip_max,v70.flip_p90,static_cast<unsigned long long>(v70.measured),static_cast<unsigned long long>(v70.fallback));
            if(m.step_after_every) {   // a sync before the player's step: the v1266 step on those frames only (counted), the rest measured
                CHECK(v70.fallback>0 && v70.measured>5*v70.fallback && v70.flip_p90<=5);continue;
            }
            CHECK(v66.gap_max>10);                                                                 // the predicted step pops on the event
            CHECK(v70.gap_max<=10 && v70.flip_max<=10 && v70.flip_p90<=5 && v70.eye_max==0);      // the measured rig does not
        }
        // v1274 review (1): an NPC's character step after the player's. v1270 kept ONE slot for the last step's delegate and body, so the player's frame
        // was measured only when no NPC step followed; v1274 reads the player's own reference frame.
        for(const unsigned every:{1u,2u}) {
            const Motion m{every==1?"run 5 + turn 60/s, stop, NPC step after every frame":"run 5 + turn 60/s, stop, NPC step after every 2nd",5,1.047,0,90,1.,0,0,0,every};
            const auto v70=run(m,v1270,limb),v74=run(m,v1274,limb);
            std::printf("%-5s %-46s v1270: %llu measured / %llu fallback, gap max %5.1f, flip max %5.1f | v1274: %llu / %llu, gap max %3.1f, flip max %3.1f\n",
                limb?"limb":"prop",m.name,static_cast<unsigned long long>(v70.measured),static_cast<unsigned long long>(v70.fallback),v70.gap_max,v70.flip_max,
                static_cast<unsigned long long>(v74.measured),static_cast<unsigned long long>(v74.fallback),v74.gap_max,v74.flip_max);
            CHECK(v70.fallback>v70.measured/3);                                              // v1270: the NPC's step takes the slot
            CHECK(v74.fallback==0 && v74.flip_p90<=5 && v74.flip_max<=5 && v74.gap_max<=5);   // v1274: always measured
        }
    }
    {   // v1264's own counters from the same model: the share of v1262's "rig read as current" decisions on a walk with turning, the headset's 27 %
        lcg=5;double read_current=0,total=0;   // the session's mix: 25 % of renders turning (one_pose turn 1832 of 7209)
        for(const auto& [m,w]:{std::pair{Motion{"",3,0,0},.75},std::pair{Motion{"",3,1.2,0},.25}}) {const auto r=run(m,v1264,false);read_current+=w*double(r.carried-r.stepped)/double(r.carried);total+=w;}
        const double pct=100.*read_current/total;
        std::printf("v1262 rule in the model: %.0f %% of carries took the reading as current (headset v1264: 1979 of 7194 = 27 %%)\n",pct);
        CHECK(std::abs(pct-27)<8);
    }
    {   // a placed render whose second eye drew the engine pose is reported separately (eye_mm)
        HeldRenderSlot::drawn_hold=false;wd::Place::clear();wd::Place::reset_one_pose();
        const auto b=yawed({1,0,1},0),f=yawed({1.04,0,1},0);std::array<float,15> raw{};const wd::Mat fm=wd::pose_mat(f),bm=wd::pose_mat(b);
        HeldEnginePose::frame_from_pose(fm.data(),raw.data());
        wd::Place::stage(0,wd::Place::kind_prop);wd::Place::stage_motion(0,0,0,&fm,&fm);
        wd::Place::publish_bound(5,{raw.data(),nullptr},{&bm,nullptr},{.5,0.},{0x99,0},1.);
        wd::Result r;r.side=0;r.eye=0;r.stage=wd::placed_pass;wd::Place::census(r,true);
        wd::Place::publish_bound(6,{nullptr,nullptr},{nullptr,nullptr});
        const auto c=wd::Place::one_pose_cell(wd::Place::kind_prop,wd::Place::b_still);
        std::printf("one eye placed: eye_split %llu, eye_mm max %.1f\n",static_cast<unsigned long long>(c.eye_split),c.eye.max);
        CHECK(c.eye_split==1 && std::abs(c.eye.max-40)<.5);
    }
    {   // PlayerRigTracker (v1274): a stamped reading is this frame's root as read; a second sync of the same frame reuses it; a reading stamped with the
        // previous frame takes the v1266 step (counted by reason); a new player (a load) starts the fallback over
        PlayerRigTracker t;const std::uintptr_t rf=0x7000;
        const auto rd=[&](Vec3 stale,Vec3 now_root,bool ok) {return HeldCarry{stale,0.,now_root,ok,rf,ok?HeldCarry::taken:HeldCarry::not_this_frame};};
        const auto a=t.frame_rig(rd({-.10,0,1},{-.05,0,1},true),.000);CHECK(length(a.root-Vec3{-.05,0,1})<1e-12 && t.measured==1);
        const auto a2=t.frame_rig(rd({-.10,0,1},{-.05,0,1},true),.001);CHECK(length(a2.root-a.root)<1e-12 && !t.fresh && t.measured==1);
        const auto b=t.frame_rig(rd({-.05,0,1},{0,0,1},true),.011);CHECK(length(b.root-Vec3{0,0,1})<1e-12 && t.measured==2);
        const auto c=t.frame_rig(rd({0,0,1},{0,0,1},false),.022);   // the player's step had not run yet: the stale reading + the last step (5 cm)
        CHECK(length(c.root-Vec3{.05,0,1})<1e-9 && t.fallback==1 && t.why[HeldCarry::not_this_frame]==1);
        HeldCarry other=rd({30,0,1},{30.05,0,1},false);other.source=0x8000;   // a level load: another reference frame
        const auto d=t.frame_rig(other,.033);CHECK(length(d.root-Vec3{30,0,1})<1e-9 && t.resets==1);   // no step carried over from the old level
    }
    {   // v1274 review (2): a grab while running at 7 m/s, 90 Hz, every root reading already this frame's (the attach job ran before the sync). v1270
        // seeded c from its first pairing by stage order (the stale pairing) and then kept whichever pairing agreed with it: one step of lead for good.
        struct V1270Tracker {   // held_render_pose.hpp v1270, its offset learning (the premise: the body is this frame's character body)
            Vec3 c{},body_prev{};bool c_ok{},prev_ok{};double heading_prev{},prev_s{};
            static Vec3 turned(Vec3 v,double a) {const double cs=std::cos(a),sn=std::sin(a);return {cs*v.x-sn*v.y,sn*v.x+cs*v.y,v.z};}
            Vec3 root(Vec3 read_root,double heading,Vec3 body,double now) {
                const bool paired=prev_ok && now>prev_s && now-prev_s<.05;const Vec3 cb=turned(read_root-body,-heading);
                if(paired) {const Vec3 ca=turned(read_root-body_prev,-heading_prev);
                    c=length(body-body_prev)<1e-6 || !c_ok || length(ca-c)<=length(cb-c)?ca:cb;c_ok=true;}
                body_prev=body;heading_prev=heading;prev_s=now;prev_ok=true;
                return c_ok?body+turned(c,heading):read_root;
            }
        };
        V1270Tracker old;PlayerRigTracker now_t;const Vec3 c_true{0,0,.9};double lead70=0,lead74=0;
        for(int k=0;k<60;++k) {
            const double t=k/90.;const Vec3 body{7*t,0,.1},root=body+c_true;   // the grab at k = 0, mid-run
            const Vec3 r70=old.root(root,0,body,t);
            const auto r74=now_t.frame_rig(HeldCarry{root,0.,root,true,0x7000,HeldCarry::taken},t).root;   // held_rig_now: the stamped frame root
            if(k>=5) {lead70=std::max(lead70,length(r70-root)*1000);lead74=std::max(lead74,length(r74-root)*1000);}
        }
        std::printf("grab while running 7 m/s, first reading current: v1270 constant lead %.1f mm, v1274 %.3f mm\n",lead70,lead74);
        CHECK(lead70>70 && lead74<1e-6);
    }
    {   // v1274 review (3): the held-exact safety net. A 10 m/s swing for 5 frames with a wrist turn of 6 rad/s; the engine's frame F written from the
        // pose given two frames before the render (the game frame a frame ahead, the note a render old). v1270's residual |B - F| is the hand's motion
        // since that given: 3 renders over 0.2 m ended the placement for the hold. HeldKGuard's residual |E * K - F| (E: the entity frame the same
        // attach job wrote F from) has no hand in it; it suspends a wrong K for good and re-arms after a passing disagreement.
        const auto K=wd::pose_mat(tilt(yawed({.04,.01,-.12},.3),{1,0,0},.2));
        // torn_at: one render whose E is a frame newer than its F (an attach between the two reads) at 20 m/s; off_from/off_to: F off E * K by 0.3 m
        const auto run_guard=[&](const wd::Mat& k_used,int torn_at,int off_from,int off_to,unsigned& placed70,unsigned& placed74,unsigned& rearms) {
            std::vector<WorldTrackedPose> hand;double x=0,yaw=0;
            for(int k=0;k<60;++k) {if(k>=10 && k<15) {x+=10./90;yaw+=6./90;} if(k==torn_at) x+=20./90;hand.push_back(yawed({.4+x,-.2,1.1},yaw));}
            bool valid70=true;unsigned off70=0;HeldKGuard guard;placed70=placed74=rearms=0;
            for(int k=8;k<60;++k) {
                const auto G=wd::pose_mat(hand[static_cast<std::size_t>(k-2)]);auto F=wd::mul(G,K);   // the given F was written from: E = G
                if(k>=off_from && k<off_to) F[3]+=.3;
                const auto E=k==torn_at+1?wd::pose_mat(hand[static_cast<std::size_t>(k-1)]):G;
                wd::Mat g_inv{};wd::inverse(G,g_inv);
                const auto B=wd::mul(wd::mul(wd::pose_mat(hand[static_cast<std::size_t>(k)]),g_inv),F);
                const double old_res=wd::distance(B,F);if(old_res>.2 && old_res<1.) {if(++off70>=3) valid70=false;} else off70=0;
                HeldKGuard::Event e{};const bool place=guard.place(wd::distance(wd::mul(E,k_used),F),e);if(e==HeldKGuard::rearm) ++rearms;
                if(k>=20) {placed70+=valid70?1u:0u;placed74+=place?1u:0u;}
            }
        };
        unsigned p70{},p74{},re{};
        run_guard(K,-10,0,0,p70,p74,re);
        std::printf("10 m/s swing for 5 frames: renders placed afterwards (of 40) v1270 %u, v1274 %u\n",p70,p74);
        CHECK(p70==0 && p74==40);
        run_guard(K,30,0,0,p70,p74,re);
        std::printf("one torn entity/model pair at 20 m/s: v1274 placed %u of 40 (needs 3 in a row)\n",p74);
        CHECK(p74==40);
        run_guard(K,-10,30,34,p70,p74,re);
        std::printf("F off E * K for 4 renders: v1274 suspended 2, re-armed %u time(s), %u of 40 placed\n",re,p74);
        CHECK(re==1 && p74==38);
        auto k_wrong=K;k_wrong[3]+=.3;
        run_guard(k_wrong,-10,0,0,p70,p74,re);
        std::printf("wrong K (30 cm): v1274 placed %u of 40 renders after it (suspended, no re-arm)\n",p74);
        CHECK(p74==0 && re==0);
    }
    // v1262 review checks -------------------------------------------------------------------------------------------------------------------------
    HeldRenderSlot::drawn_hold=true;
    {   // (1) the two ends of the carry must be ONE rig definition: a fixed 2 cm / 1 deg difference between the noted rig and the rig read at the sync
        // lands whole on the engine pose even standing still; read by the same function it is 0 (status one_pose.*.rig_mm / rig_deg measure it live).
        const auto drawn=yawed({1.3,.2,1.1},.3);const HeldCarry noted{{1,0,0},.2};
        HeldRenderSlot s;s.publish(1,drawn,0.);s.note_drawn(drawn,0.,&noted);
        WorldTrackedPose same{},offset{};bool b=false;
        const HeldCarry read_same=noted,read_offset{{1.02,0,0},.2+1./57.29577951308232};
        CHECK(s.decide(drawn,.005,same,b,&read_same));
        HeldRenderSlot s2;s2.publish(1,drawn,0.);s2.note_drawn(drawn,0.,&noted);CHECK(s2.decide(drawn,.005,offset,b,&read_offset));
        const double e_same=length(same.position-drawn.position)*1000,e_off=length(offset.position-drawn.position)*1000;
        std::printf("rig sources: one definition %.3f mm, a 2 cm / 1 deg definition offset %.1f mm on the engine pose standing still\n",e_same,e_off);
        CHECK(e_same<1e-6 && e_off>15);   // 2 cm plus the 1 deg turn's share at 0.36 m from the root
    }
    {   // (3) release + re-grab of the SAME entity: nothing noted for the old hold survives (publish resets only on an entity change)
        HeldRenderSlot s;const auto g=yawed({0,0,1},0);const HeldCarry rig{{0,0,0},0};
        s.publish(7,g,0.);s.note_drawn(yawed({.05,0,1},.1),0.,&rig);WorldTrackedPose out{};bool b=false;CHECK(s.decide(g,.005,out,b,&rig));
        CHECK(s.recent_n==1 && s.drawn_ok && s.rel_ok && s.carry_ok);
        s.end(.01);
        CHECK(!s.drawn_ok && !s.rel_ok && !s.carry_ok && s.recent_n==0 && s.note_dt==0);
        CHECK(s.decide(g,.02,out,b,&rig) && b);   // the release blend still runs from the last pose given
        s.publish(7,g,.05);   // the same entity again, before the blend ended
        CHECK(s.decide(g,.055,out,b,&rig) && !b && s.used==HeldRenderSlot::given_goal && length(out.position-g.position)<1e-12);   // no old note: the goal
        std::printf("re-grab of the same entity: old note, X, given ring and rig history cleared\n");
    }
    {   // (2) a hitch: no note for 80 ms while the hand sweeps at 1.5 m/s through the rig. The rig-only carry keeps the last drawn hold (the hand's sweep
        // is invisible to it); past carry_age the engine takes goal * X, which follows the hand with the motor's samples.
        HeldRenderSlot s;const HeldCarry rig{{0,0,0},0};const auto x_off=yawed({.03,.01,0},.05);   // the motor-hand vs drawn-hand offset (X)
        const auto drawn_at=[&](double t) {return yawed({.4+1.5*std::max(0.,t-.1),-.2,1.},0);};   // the hand starts sweeping at t = 0.1 s
        double carry_only=0,v1262=0,steady_age_ok=1;
        for(double t=0;t<.3;t+=1./90) {
            const auto truth=drawn_at(t);const auto goal=grip_compose(truth,grip_inverse(x_off));   // the motor goal tracks the hand (minus X)
            s.publish(3,goal,t);
            const bool hitch=t>=.1 && t<.18;
            if(!hitch) s.note_drawn(truth,t,&rig);
            const double sync=t+.006;WorldTrackedPose out{};bool b=false;s.decide(goal,sync,out,b,&rig);
            const double err=length(out.position-drawn_at(sync).position)*1000;   // vs the hand at the sync's time
            if(hitch && t>.1+.035) {v1262=std::max(v1262,err);carry_only=std::max(carry_only,length(s.drawn.position-drawn_at(sync).position)*1000);}
            if(!hitch && t>.03 && t<.1 && s.used!=HeldRenderSlot::given_carried) steady_age_ok=0;   // steady rendering: always the rig carry
        }
        std::printf("hitch (80 ms without notes, hand 1.5 m/s): rig-only carry of the last note lags up to %.0f mm; v1262 (goal * X past %.0f ms) %.1f mm\n",carry_only,s.carry_age()*1000,v1262);
        CHECK(steady_age_ok==1 && carry_only>50 && v1262<15);
    }
    {   // (4) the drawn body pose is refused, not repaired, when K's scale/handedness disagrees (the publisher's same_scales / unit-column guard is
        // dxgi_probe code; here the property it relies on): a mirrored basis fails grab_pose_valid, so a refused note leaves the last good one in place
        HeldRenderSlot s;const auto g=yawed({0,0,1},0);s.publish(1,g,0.);
        WorldTrackedPose mirrored=g;mirrored.basis[2]=mirrored.basis[2]*-1.;
        CHECK(!s.note_drawn(mirrored,0.,nullptr) && !s.drawn_ok);
    }
    std::printf(failures?"held_one_pose_check: %d FAILED\n":"held_one_pose_check: PASS\n",failures);
    return failures?1:0;
}
