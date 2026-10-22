#pragma once
// Plan H2: speed-adaptive smoothing (a One Euro filter) for the hand AIM ray: the one left-hand aim the crosshair, Blink, the hand shots and the powers read.
// Technique (in our words): a low-pass whose cutoff rises with how fast the signal is moving. A still hand gets a low cutoff (about 1 Hz, so tremor and tracker
// noise are averaged away); a moving hand gets a cutoff that grows with its speed, so a sweep has little lag. The speed is itself a low-passed derivative
// (d_cutoff), so one noisy sample does not open the filter.
// What it filters: the aim pose only, after the game-side barrel/offset have shaped it. NOT the drawn hand, the sword, nor the interaction ray (the stealth
// kill / choke / Use targeting keeps the raw hand: filtering it could move which guard is offered).
// Frame: the filter works in the player's anchor frame (the pose projected on the anchor's axes), so walking, a snap turn or a teleport does not look like hand
// motion. Once per displayed frame: a repeated frame id returns the cached result (the gameplay aim and the crosshair share one filter).
// Reset (the next sample passes through unfiltered): pose epoch or reference epoch (calibration / recentre) change, an anchor jump of more than 1 m (teleport),
// a gap of more than 0.25 s between samples, a non-finite input, or an explicit reset().
// Units for the cutoff law: position speed in cm/s, orientation speed in deg/s (so beta 0.02 opens the cutoff to ~7 Hz at a 3 m/s sweep).
#include "tracked_pose.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <sstream>
#include <string>

namespace d2vr {
struct AimSmootherParams {
    double min_cutoff=1.,beta=.02,d_cutoff=1.;   // Hz, per (cm/s or deg/s), Hz
    static constexpr double min_cutoff_lo=.05,min_cutoff_hi=30.,beta_hi=1.,d_cutoff_lo=.05,d_cutoff_hi=30.;
    bool valid() const noexcept {
        return std::isfinite(min_cutoff) && std::isfinite(beta) && std::isfinite(d_cutoff) && min_cutoff>=min_cutoff_lo && min_cutoff<=min_cutoff_hi &&
            beta>=0 && beta<=beta_hi && d_cutoff>=d_cutoff_lo && d_cutoff<=d_cutoff_hi;
    }
    // build/aim-smoothing: "min_cutoff [beta [d_cutoff]]" (whitespace separated; missing values keep the defaults). Anything invalid returns the defaults.
    static AimSmootherParams parse(const std::string& text,bool* ok=nullptr) {
        AimSmootherParams p;std::istringstream in(text);double v[3]{p.min_cutoff,p.beta,p.d_cutoff};unsigned n=0;
        while(n<3 && in>>v[n]) ++n;
        std::string extra;
        if(n==0 || (in.clear(),in>>extra,!extra.empty())) {if(ok) *ok=false;return AimSmootherParams{};}
        AimSmootherParams q;q.min_cutoff=v[0];q.beta=v[1];q.d_cutoff=v[2];
        if(!q.valid()) {if(ok) *ok=false;return AimSmootherParams{};}
        if(ok) *ok=true;
        return q;
    }
};

class AimSmoother {
public:
    explicit AimSmoother(AimSmootherParams p={}) noexcept : params_(p.valid()?p:AimSmootherParams{}) {}
    void set_params(AimSmootherParams p) noexcept {params_=p.valid()?p:AimSmootherParams{};reset();}
    const AimSmootherParams& params() const noexcept {return params_;}
    void reset() noexcept {valid_=false;last_frame_valid_=false;}
    unsigned resets() const noexcept {return resets_;}
    unsigned samples() const noexcept {return samples_;}
    // `world`: the aim pose (game space); `frame`: the anchor (player frame) pose of the same frame; frame_id: the displayed frame; tick_us: a steady clock.
    WorldTrackedPose sample(const WorldTrackedPose& world,const WorldTrackedPose& frame,std::uint64_t frame_id,std::uint64_t pose_epoch,std::uint64_t reference_epoch,std::uint64_t tick_us) noexcept {
        if(last_frame_valid_ && frame_id==last_frame_ && pose_epoch==epoch_ && reference_epoch==reference_) return last_out_;
        const auto out=step(world,frame,pose_epoch,reference_epoch,tick_us);
        last_frame_=frame_id;last_frame_valid_=true;last_out_=out;
        return out;
    }
private:
    struct Channel {Vec3 x{},dx{};};
    static double alpha(double cutoff_hz,double dt) noexcept {
        const double tau=1/(2*3.14159265358979323846*cutoff_hz);return 1/(1+tau/dt);
    }
    static bool frame_ok(const WorldTrackedPose& f) noexcept {
        for(const auto& a:f.basis) if(!finite(a) || std::abs(length(a)-1)>.02) return false;
        return finite(f.position);
    }
    static bool pose_ok(const WorldTrackedPose& p) noexcept {
        for(const auto& a:p.basis) if(!finite(a)) return false;
        return finite(p.position);
    }
    static Vec3 to_local(const WorldTrackedPose& f,Vec3 v) noexcept {return {dot(v,f.basis[0]),dot(v,f.basis[1]),dot(v,f.basis[2])};}
    static Vec3 to_world(const WorldTrackedPose& f,Vec3 v) noexcept {return f.basis[0]*v.x+f.basis[1]*v.y+f.basis[2]*v.z;}
    // Advances a channel's filtered derivative and returns its length in the channel's unit scale (cm/s or deg/s).
    static double track_speed(Channel& c,Vec3 raw,double dt,const AimSmootherParams& p,double unit_scale) noexcept {
        const Vec3 d=(raw-c.x)*(1/dt);
        c.dx=c.dx+(d-c.dx)*alpha(p.d_cutoff,dt);
        return length(c.dx)*unit_scale;
    }
    WorldTrackedPose step(const WorldTrackedPose& world,const WorldTrackedPose& frame,std::uint64_t pose_epoch,std::uint64_t reference_epoch,std::uint64_t tick_us) noexcept {
        if(!pose_ok(world) || !frame_ok(frame) || !tick_us) {reset_state();return world;}
        const bool changed=!valid_ || pose_epoch!=epoch_ || reference_epoch!=reference_ || tick_us<=tick_ || tick_us-tick_>250000 || length(frame.position-frame_pos_)>1.0;
        epoch_=pose_epoch;reference_=reference_epoch;frame_pos_=frame.position;
        const Vec3 p=to_local(frame,world.position-frame.position),f=to_local(frame,world.basis[0]),u=to_local(frame,world.basis[2]);
        if(changed) {
            if(valid_) ++resets_;
            seed(p,f,u,tick_us);return world;
        }
        const double dt=std::clamp(static_cast<double>(tick_us-tick_)*1e-6,1./500,.1);
        tick_=tick_us;++samples_;
        // position: speed in cm/s
        const double sp=track_speed(pos_,p,dt,params_,100.);
        const double cp=params_.min_cutoff+params_.beta*sp;
        pos_.x=pos_.x+(p-pos_.x)*alpha(cp,dt);
        // orientation: the forward and up vectors; angular speed in deg/s from the larger of the two
        const double sf=track_speed(fwd_,f,dt,params_,57.29577951308232),su=track_speed(up_,u,dt,params_,57.29577951308232);
        const double co=params_.min_cutoff+params_.beta*std::max(sf,su),ao=alpha(co,dt);
        fwd_.x=fwd_.x+(f-fwd_.x)*ao;up_.x=up_.x+(u-up_.x)*ao;
        // Rebuild the proper basis: forward unit, up made orthogonal, left = up x forward (the repo's forward/left/up convention).
        Vec3 fo=fwd_.x;const double fl=length(fo);
        if(fl<1e-6) {seed(p,f,u,tick_us);return world;}
        fo=fo*(1/fl);Vec3 uo=up_.x-fo*dot(up_.x,fo);const double ul=length(uo);
        if(ul<1e-6) {seed(p,f,u,tick_us);return world;}
        uo=uo*(1/ul);const Vec3 lo=cross(uo,fo);
        WorldTrackedPose out;
        out.position=frame.position+to_world(frame,pos_.x);
        out.basis={to_world(frame,fo),to_world(frame,lo),to_world(frame,uo)};
        return out;
    }
    void seed(Vec3 p,Vec3 f,Vec3 u,std::uint64_t tick_us) noexcept {
        pos_={p,{}};fwd_={f,{}};up_={u,{}};tick_=tick_us;valid_=true;
    }
    void reset_state() noexcept {valid_=false;}
    AimSmootherParams params_{};
    Channel pos_{},fwd_{},up_{};
    std::uint64_t tick_{},epoch_{},reference_{},last_frame_{};
    Vec3 frame_pos_{};
    WorldTrackedPose last_out_{};
    bool valid_{},last_frame_valid_{};
    unsigned resets_{},samples_{};
};
}
