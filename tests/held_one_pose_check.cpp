// v1258 lab: one pose per presented frame for a held loose prop, replaying the v1256 headset failure (22:51-22:58).
// Headset facts it models: held_census.prop 7201 renders, 4132 (57 %) with no draw placed - those frames show the ENGINE pose, which sat
// 35-100 mm from the drawn hold (hold_sync.prop.engine_gap p50 still / walk-run); placed renders were exact in every pass and both eyes.
// Renders at 90 Hz on a ~64 Hz game tick (dt 15/16 ms, ~1 render in 3 on a repeated tick, as the v1252/v1256 walking bursts); the player's rig
// (root position, heading) steps once per tick, the drawn hand B = rig of the tick * this render's hand tracking * the hold; the engine pose of a
// held prop is written once per tick by the rigid-body sync (held_render_pose.hpp); the motor goal comes from the motor's own hand sample (the
// newest publication, an offset from the drawn hand) led by the player's velocity. 57 % of renders draw the thing at the engine pose, the rest at B.
// The certificate (weapon_draw::Place one_pose) is fed as the publisher feeds it.
//   v1256 (HeldRenderSlot::drawn_hold off, the bare goal): the flip against the drawn hand is the engine offset - FAILS the one-pose bar.
//   v1258: the engine pose = the last drawn hold carried to the tick's rig (held_carried, the weapons' item_advance) - PASSES, standing, walking,
//   running, stick turning and snap turning, whether the engine's sync reads the rig after this tick's attach (current) or before it (stale).
#define NOMINMAX
#include "../src/held_render_pose.hpp"
#include "../src/held_engine_pose.hpp"
#include "../src/weapon_draw_place.hpp"
#include <cstdio>
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
struct Motion {const char* name{};double speed{},yaw_rate{},snap{};};   // snap: a heading jump every 0.5 s (rad)
struct Result {double shown_p90{},flip_p90{},flip_max{},eye_max{};std::uint64_t renders{},unplaced{};};
static double edge_p(const wd::Place::OneHist& h,double q) {
    std::uint64_t t=0;for(auto v:h.n) t+=v;std::uint64_t run=0;
    for(unsigned i=0;i<wd::Place::OneHist::bins;++i) {run+=h.n[i];if(t && double(run)>=q*double(t)) return wd::Place::OneHist::edge[i];}
    return 0;
}
static wd::Place::OneHist merged(const wd::Place::OneHist& a,const wd::Place::OneHist& b) {auto o=a;for(unsigned i=0;i<o.n.size();++i) o.n[i]+=b.n[i];o.max=std::max(a.max,b.max);return o;}
static Result run(const Motion& m,bool drawn_hold,bool stale_rig) {
    lcg=11;HeldRenderSlot::drawn_hold=drawn_hold;wd::Place::clear();wd::Place::reset_one_pose();
    HeldRenderSlot slot;const std::uintptr_t entity=0x1234;
    const WorldTrackedPose rel=tilt(yawed({.10,.02,-.03},.4),{1,0,0},.3);          // the prop in the drawn grip (body pose)
    const WorldTrackedPose motor_off=tilt(yawed({.035,.018,-.01},.12),{0,1,0},.06); // the motor's hand vs the drawn hand (~40 mm, ~8 deg)
    const auto track_at=[&](double t) {   // the hand in the rig (tracking): a slow 2 cm sway and wrist turn (~0.13 m/s, ~0.4 rad/s)
        return tilt(yawed({.35+.02*std::sin(t*6.3),-.2+.015*std::sin(t*4.1),1.1+.01*std::sin(t*7.7)},.2*std::sin(t*2.)),{1,0,0},.1*std::sin(t*3.3));};
    // the rig per tick: tick k at T[k], root and heading as the attach job writes them that tick
    std::vector<double> T{0};std::vector<HeldCarry> rig{HeldCarry{{0,0,0},0}};
    while(T.back()<3.2) {
        const double dt=(rnd()<.5?.015:.016)+(rnd()<.01?.015:0);   // 15/16 ms, an occasional 31 ms hitch
        HeldCarry r=rig.back();r.root=r.root+Vec3{std::cos(r.heading),std::sin(r.heading),0}*(m.speed*dt);r.heading+=m.yaw_rate*dt;
        if(m.snap!=0 && std::floor((T.back()+dt)/.5)>std::floor(T.back()/.5)) r.heading+=m.snap;
        T.push_back(T.back()+dt);rig.push_back(r);
    }
    const auto rig_pose=[&](std::size_t k) {return yawed(rig[k].root,rig[k].heading);};
    const auto drawn_at=[&](std::size_t k,double t) {return grip_compose(grip_compose(rig_pose(k),track_at(t)),rel);};
    std::size_t tick=0;WorldTrackedPose engine{};bool have_engine=false;double last_render_t=0;std::size_t last_render_tick=0;std::uint32_t frame=100;
    for(double t=0.;t<3.;t+=1./90) {
        // the game ticks before this render's composition: the motor writes its goal, then the rigid-body sync gives the engine its pose
        while(tick+1<T.size() && T[tick+1]<=t) {
            ++tick;
            const auto sample=grip_compose(grip_compose(rig_pose(last_render_tick),track_at(last_render_t)),motor_off);   // the newest publication
            auto goal=grip_compose(sample,rel);const Vec3 v{std::cos(rig[tick].heading)*m.speed,std::sin(rig[tick].heading)*m.speed,0};
            goal.position=goal.position+v*(T[tick]-last_render_t);   // led to the motor's time with the player's velocity
            slot.publish(entity,goal,T[tick]);
            const HeldCarry read=stale_rig?rig[tick-1]:rig[tick];   // the sync reads the body model before (stale) or after (current) this tick's attach
            WorldTrackedPose out{};bool blending=false;
            if(slot.decide(goal,T[tick]+.0005,out,blending,&read)) {engine=out;have_engine=true;}
        }
        const auto drawn=drawn_at(tick,t);   // B: the rig of this render's tick, the hand of this render
        const double yaw_dps=frame>100?std::remainder(rig[tick].heading-rig[last_render_tick].heading,6.283185307179586)*90*57.29577951308232:0.;
        if(t>=.3 && t-1./90<.3) wd::Place::reset_one_pose();   // the hold has settled: measure from here
        if(have_engine) {
            std::array<float,15> raw{};const wd::Mat fm=wd::pose_mat(engine),bm=wd::pose_mat(drawn);HeldEnginePose::frame_from_pose(fm.data(),raw.data());
            slot.note_drawn(drawn,t,&rig[tick]);   // the publisher: the drawn hold and the rig its image was placed with
            wd::Place::stage(0,wd::Place::kind_prop);wd::Place::stage_motion(0,m.speed,yaw_dps,&fm,&fm);
            wd::Place::publish_bound(frame,{raw.data(),nullptr},{&bm,nullptr},{.5,0.},{entity,0},t);
            if(rnd()>=.574) for(int eye=0;eye<2;++eye) {wd::Result r;r.side=0;r.eye=eye;r.stage=wd::placed_pass;wd::Place::census(r,true);wd::Place::census(r,false);}
            // else: no draw of this render was placed - the frame shows the engine pose
        }
        ++frame;last_render_t=t;last_render_tick=tick;
    }
    wd::Place::publish_bound(frame,{nullptr,nullptr},{nullptr,nullptr});   // rolls the last render
    wd::Place::OneCell all{};
    for(unsigned b=0;b<wd::Place::buckets;++b) {const auto c=wd::Place::one_pose_cell(wd::Place::kind_prop,b);
        all.renders+=c.renders;all.unplaced+=c.unplaced;all.shown=merged(all.shown,c.shown);all.flip=merged(all.flip,c.flip);all.eye=merged(all.eye,c.eye);}
    return {edge_p(all.shown,.9),edge_p(all.flip,.9),all.flip.max,all.eye.max,all.renders,all.unplaced};
}
int main() {
    const Motion motions[]={{"still",0,0,0},{"walk 3 m/s",3,0,0},{"run 7 m/s",7,0,0},{"walk 1.5 + stick turn 90/s",1.5,1.571,0},{"walk 1.5 + snap 45 deg",1.5,0,.785}};
    for(const auto& m:motions) {
        const auto before=run(m,false,false),current=run(m,true,false),stale=run(m,true,true);
        std::printf("%-27s v1256 flip p90 <=%5.1f max %6.1f mm | v1258 rig current: shown p90 <=%4.1f flip p90 <=%4.1f max %5.1f | rig stale: shown p90 <=%4.1f flip p90 <=%4.1f max %5.1f  (%llu renders, %llu unplaced)\n",
            m.name,before.flip_p90,before.flip_max,current.shown_p90,current.flip_p90,current.flip_max,stale.shown_p90,stale.flip_p90,stale.flip_max,
            static_cast<unsigned long long>(current.renders),static_cast<unsigned long long>(current.unplaced));
        CHECK(current.renders>200 && current.unplaced>current.renders/3);   // the replay really runs the 57 % unplaced pattern
        CHECK(before.flip_p90>=20);                                            // v1256 reproduces the two-pose flip
        for(const auto* r:{&current,&stale}) {
            CHECK(r->shown_p90<=5 && r->flip_p90<=5);                          // v1258: one pose per frame against the drawn hand
            if(m.snap==0) CHECK(r->flip_max<=10);                              // a snap turn's own tick may flash once when the rig was read stale
            CHECK(r->eye_max==0);
        }
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
    {   // rig_of_tick: a reading equal to the last image's rig repeats the last step; a moved reading is used as read
        HeldRenderSlot s;s.publish(1,yawed({0,0,1},0),0.);const HeldCarry image{{0,0,0},0};s.note_drawn(yawed({.1,0,1},0),0.,&image);
        (void)s.rig_of_tick(HeldCarry{{-.10,0,0},-.02},.000);   // two earlier syncs, one tick apart
        (void)s.rig_of_tick(HeldCarry{{-.05,0,0},-.01},.016);
        const auto a=s.rig_of_tick(HeldCarry{{0,0,0},0},.032);   // unmoved since the image: not yet this tick's -> carried one tick on (16 ms at 3.1 m/s)
        CHECK(length(a.root-Vec3{.05,0,0})<1e-9 && std::abs(a.heading-.01)<1e-12 && s.stepped);
        const auto b=s.rig_of_tick(HeldCarry{{.05,0,0},.01},.048);   // moved past the image: as read
        CHECK(length(b.root-Vec3{.05,0,0})<1e-9 && !s.stepped);
        const auto c=s.rig_of_tick(HeldCarry{{.05,0,0},.01+.785},.064);   // a snap turn is never extrapolated into the next tick
        CHECK(std::abs(c.heading-(.01+.785))<1e-12);
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
        CHECK(s.recent_n==1 && s.have_seen && s.drawn_ok && s.rel_ok && s.carry_ok);
        s.end(.01);
        CHECK(!s.drawn_ok && !s.rel_ok && !s.carry_ok && s.recent_n==0 && !s.have_seen && s.note_dt==0);
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
