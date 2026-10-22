// Plan H2: the aim One Euro filter (src/aim_smoother.hpp).
#include "../src/aim_smoother.hpp"
#include <cstdio>
#include <vector>
using namespace d2vr;
namespace {
int failures=0;
#define CHECK(x) do{if(!(x)){std::printf("FAIL line %d: %s\n",__LINE__,#x);++failures;}}while(0)
constexpr double kHz=90,kDt=1/kHz,kPi=3.14159265358979323846;
struct Rng {std::uint64_t s=88172645463325252ull;double next() {s^=s<<13;s^=s>>7;s^=s<<17;return double(s>>11)/double(1ull<<53)*2-1;}};   // uniform -1..1
WorldTrackedPose identity_frame() {return {{0,0,0},{{{1,0,0},{0,1,0},{0,0,1}}}};}
// A forward/left/up basis yawed by `yaw` and pitched by `pitch` (radians).
WorldTrackedPose aim_pose(Vec3 position,double yaw,double pitch) {
    const Vec3 f{std::cos(pitch)*std::cos(yaw),std::cos(pitch)*std::sin(yaw),std::sin(pitch)};
    Vec3 l=cross({0,0,1},f);l=l*(1/length(l));const Vec3 u=cross(f,l);
    return {position,{f,l,u}};
}
std::uint64_t us(unsigned frame) {return 5000000+std::uint64_t(std::llround(frame*1e6/kHz));}
double angle_between(Vec3 a,Vec3 b) {return std::acos(std::clamp(dot(a,b)/(length(a)*length(b)),-1.,1.));}

void still_hand_jitter_reduced() {
    // 1 mm uniform position noise and a matching ~0.06 deg direction noise around a fixed hand, 6 seconds at 90 Hz.
    AimSmoother filter;Rng rng;const Vec3 centre{1,2,3};double raw_pos=0,out_pos=0,raw_ang=0,out_ang=0;
    for(unsigned i=0;i<540;++i) {
        const Vec3 noise{rng.next()*.001,rng.next()*.001,rng.next()*.001};
        const auto raw=aim_pose(centre+noise,rng.next()*.001,rng.next()*.001);
        const auto out=filter.sample(raw,identity_frame(),i+1,1,1,us(i));
        if(i<90) continue;   // let the filter settle
        raw_pos+=dot(raw.position-centre,raw.position-centre);out_pos+=dot(out.position-centre,out.position-centre);
        const auto ref=aim_pose(centre,0,0);
        const double ar=angle_between(raw.basis[0],ref.basis[0]),ao=angle_between(out.basis[0],ref.basis[0]);raw_ang+=ar*ar;out_ang+=ao*ao;
    }
    const double pos_ratio=std::sqrt(raw_pos/out_pos),ang_ratio=std::sqrt(raw_ang/out_ang);
    std::printf("still: position noise reduced %.1fx, direction noise reduced %.1fx\n",pos_ratio,ang_ratio);
    CHECK(pos_ratio>=5 && ang_ratio>=5);
}
void fast_sweep_lag_bounded() {
    // A hand sweeping 3 m/s along +y with its aim rotating at the matching 0.5 m arm rate (6 rad/s = 344 deg/s), at 72, 90 and 120 Hz.
    for(const double hz:{72.,90.,120.}) {
        AimSmoother filter;const double v=3,w=v/.5;WorldTrackedPose out{};WorldTrackedPose raw{};
        const unsigned frames=unsigned(hz*2);
        for(unsigned i=0;i<frames;++i) {
            const double t=i/hz;   // the forward rotates about +z at w rad/s
            raw=aim_pose({0,v*t,0},w*t,0);
            out=filter.sample(raw,identity_frame(),i+1,1,1,5000000+std::uint64_t(std::llround(i*1e6/hz)));
        }
        const double per_frame_m=v/hz,per_frame_rad=w/hz;
        const double lag_pos=length(raw.position-out.position)/per_frame_m,lag_ang=angle_between(raw.basis[0],out.basis[0])/per_frame_rad;
        std::printf("sweep %3.0f Hz: position lag %.2f frames, direction lag %.2f frames\n",hz,lag_pos,lag_ang);
        CHECK(lag_pos<=2.0 && lag_ang<=2.0);
    }
}
void slow_deliberate_aim_follows() {
    // A slow 20 deg/s pan: lag stays under 1.2 degrees at steady state (the filter must not feel sticky at ordinary aim speeds).
    AimSmoother filter;WorldTrackedPose out{},raw{};
    for(unsigned i=0;i<270;++i) {raw=aim_pose({0,0,0},20*kPi/180*i*kDt,0);out=filter.sample(raw,identity_frame(),i+1,1,1,us(i));}
    const double lag=angle_between(raw.basis[0],out.basis[0])*180/kPi;
    std::printf("slow pan 20 deg/s: lag %.2f deg\n",lag);
    CHECK(lag<1.2);
}
void reset_on_epoch() {
    AimSmoother filter;WorldTrackedPose out{};
    for(unsigned i=0;i<60;++i) out=filter.sample(aim_pose({0,0,0},0,0),identity_frame(),i+1,1,1,us(i));
    const auto jump=aim_pose({1,0,0},1.0,.2);
    // same epochs: the filter lags behind a sudden change
    {AimSmoother g=filter;const auto o=g.sample(jump,identity_frame(),100,1,1,us(60));CHECK(length(o.position-jump.position)>.5);}
    // pose epoch changed: pass-through, then smoothing restarts from the new pose
    {AimSmoother g=filter;const auto o=g.sample(jump,identity_frame(),100,2,1,us(60));
     CHECK(length(o.position-jump.position)==0 && length(o.basis[0]-jump.basis[0])==0);
     const auto next=g.sample(jump,identity_frame(),101,2,1,us(61));CHECK(length(next.position-jump.position)<1e-9);CHECK(g.resets()==1);}
    // reference epoch (calibration / recentre) changed
    {AimSmoother g=filter;const auto o=g.sample(jump,identity_frame(),100,1,2,us(60));CHECK(length(o.position-jump.position)==0 && g.resets()==1);}
    // teleport: the anchor jumps more than 1 m
    {AimSmoother g=filter;auto far=identity_frame();far.position={30,0,0};const auto at=aim_pose({30,0,0},0,0);
     const auto o=g.sample(at,far,100,1,1,us(60));CHECK(length(o.position-at.position)==0 && g.resets()==1);}
    // a gap longer than 0.25 s
    {AimSmoother g=filter;const auto o=g.sample(jump,identity_frame(),100,1,1,us(60)+400000);CHECK(length(o.position-jump.position)==0 && g.resets()==1);}
    // non-finite input passes through and the next good sample re-seeds
    {AimSmoother g=filter;auto bad=jump;bad.position.x=std::nan("");const auto o=g.sample(bad,identity_frame(),100,1,1,us(60));CHECK(!std::isfinite(o.position.x));
     const auto good=g.sample(jump,identity_frame(),101,1,1,us(61));CHECK(length(good.position-jump.position)==0);}
}
void once_per_displayed_frame() {
    // A second call in the same displayed frame (the crosshair after the shot aim) returns the same pose and does not advance the filter.
    AimSmoother filter;Rng rng;const Vec3 c{1,1,1};
    for(unsigned i=0;i<30;++i) filter.sample(aim_pose(c+Vec3{rng.next()*.001,0,0},0,0),identity_frame(),i+1,1,1,us(i));
    const auto a=filter.sample(aim_pose(c+Vec3{.0007,0,0},0,0),identity_frame(),31,1,1,us(30));
    const auto b=filter.sample(aim_pose(c+Vec3{-.0009,.2,0},0,0),identity_frame(),31,1,1,us(30));
    CHECK(length(a.position-b.position)==0 && filter.samples()==30);
}
void anchor_motion_is_not_hand_motion() {
    // The hand holds still in the player frame while the player snap-turns 45 degrees in one frame: the aim follows at once (no smoothing lag on a turn).
    AimSmoother filter;WorldTrackedPose out{};const Vec3 local_pos{.3,.2,-.1};
    auto frame_at=[](double yaw) {return aim_pose({0,0,0},yaw,0);};
    auto world_aim=[&](const WorldTrackedPose& fr) {WorldTrackedPose p;p.position=fr.position+fr.basis[0]*local_pos.x+fr.basis[1]*local_pos.y+fr.basis[2]*local_pos.z;p.basis=fr.basis;return p;};
    for(unsigned i=0;i<60;++i) {const auto fr=frame_at(0);out=filter.sample(world_aim(fr),fr,i+1,1,1,us(i));}
    const auto turned=frame_at(kPi/4);const auto raw=world_aim(turned);
    out=filter.sample(raw,turned,100,1,1,us(60));
    CHECK(length(out.position-raw.position)<1e-9 && angle_between(out.basis[0],raw.basis[0])<1e-9 && filter.resets()==0);
}
void output_basis_is_proper_and_disabled_is_neutral() {
    AimSmoother filter;Rng rng;
    for(unsigned i=0;i<300;++i) {
        const auto raw=aim_pose({0,0,1},.4*std::sin(i*.05)+rng.next()*.01,.2*std::cos(i*.07)+rng.next()*.01);
        const auto out=filter.sample(raw,identity_frame(),i+1,1,1,us(i));
        CHECK(std::abs(length(out.basis[0])-1)<1e-9 && std::abs(length(out.basis[1])-1)<1e-9 && std::abs(length(out.basis[2])-1)<1e-9);
        CHECK(std::abs(dot(out.basis[0],out.basis[1]))<1e-9 && std::abs(dot(out.basis[0],out.basis[2]))<1e-9 && dot(cross(out.basis[0],out.basis[1]),out.basis[2])>1-1e-9);
    }
}
void params_parse_and_bounds() {
    bool ok=false;
    auto p=AimSmootherParams::parse("1.0 0.02 1.0\n",&ok);CHECK(ok && p.min_cutoff==1 && p.beta==.02 && p.d_cutoff==1);
    p=AimSmootherParams::parse("0.5",&ok);CHECK(ok && p.min_cutoff==.5 && p.beta==.02 && p.d_cutoff==1);
    p=AimSmootherParams::parse("2 0.05",&ok);CHECK(ok && p.min_cutoff==2 && p.beta==.05);
    for(const char* bad:{"","abc","0","100","1 -1","1 0.02 1 7","1 2","nan"}) {p=AimSmootherParams::parse(bad,&ok);CHECK(!ok && p.min_cutoff==1 && p.beta==.02 && p.d_cutoff==1);}
    AimSmootherParams d;CHECK(d.min_cutoff==1 && d.beta==.02 && d.d_cutoff==1);   // the shipped defaults
    // a stronger setting smooths more, a weaker one less
    auto run=[&](AimSmootherParams q) {AimSmoother f(q);Rng rng;double e=0;for(unsigned i=0;i<400;++i) {const auto o=f.sample(aim_pose({rng.next()*.001,0,0},0,0),identity_frame(),i+1,1,1,us(i));if(i>90) e+=o.position.x*o.position.x;}return e;};
    AimSmootherParams strong;strong.min_cutoff=.5;AimSmootherParams weak;weak.min_cutoff=4;
    CHECK(run(strong)<run(d) && run(d)<run(weak));
}
}
int main() {
    still_hand_jitter_reduced();fast_sweep_lag_bounded();slow_deliberate_aim_follows();reset_on_epoch();once_per_displayed_frame();
    anchor_motion_is_not_hand_motion();output_basis_is_proper_and_disabled_is_neutral();params_parse_and_bounds();
    if(failures) {std::printf("aim_smoother_check: %d failure(s)\n",failures);return 1;}
    std::printf("aim_smoother_check passed\n");return 0;
}
