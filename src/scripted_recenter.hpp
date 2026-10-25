#pragma once
// Climbs/takedowns: ease the world (0.4 s) so the action lands in front of the player's current gaze, then fold
// the offset into HeadCalibration at exit so the view stays continuous. Pure state machine; the caller composes
// origin' = yaw(offset) * head_origin and applies fold_rad via HeadCalibration::rotate_origin.
#include "tracked_pose.hpp"
#include <cmath>
#include <cstdint>
#include <optional>

namespace d2vr {
struct ScriptedRecenterParams {double dead_zone_rad=0.17453292519943295,ease_s=0.4;}; // 10 degrees
struct ScriptedRecenterInput {
    double t;                          // seconds, monotonic
    bool authored;                     // authored_camera this frame
    std::optional<double> hmd_rel_yaw; // tracked_yaw(centre_pose, head_origin), wrapped; nullopt if not finite
    std::optional<double> heading;     // AuthoredHeading result this frame
    std::uint64_t heading_snaps;       // AuthoredHeading::snaps()
    std::uint64_t epoch;               // reference epoch ^ player/camera/generation identity hash
    std::optional<double> body_yaw;    // gameplay body heading when !authored, for the exit fold
    bool instant;                      // a hard cut this frame
};
struct ScriptedRecenterOutput {
    double offset_rad;                 // origin' = yaw(offset) * head_origin
    std::optional<double> fold_rad;    // exit frame: rotate HeadCalibration by this
    bool easing;
    std::uint64_t entries,reaims,folds,skipped_small;
    bool residual_easing;              // exit had no body_yaw: the continuity error is easing out via fold_rad deltas
};
// About +Y (tracking up), the convention HeadCalibration stores.
inline Quaternion yaw_quaternion(double yaw) noexcept {return {0,std::sin(yaw*.5),0,std::cos(yaw*.5)};}

class ScriptedRecenter {
    static constexpr double pi=3.14159265358979323846;
    bool in_{},ease_{};
    std::uint64_t epoch_{},snaps_{},entries_{},reaims_{},folds_{},skipped_{};
    double offset_{},from_{},to_{},t0_{},heading_{};
    bool have_heading_{};
    bool res_pending_{},res_started_{};
    double res_err_{},res_t0_{},res_applied_{},res_heading_{};
    static double wrap(double a) noexcept {const double r=std::remainder(a,2*pi);return r<=-pi?r+2*pi:r;} // (-pi,pi]
    void aim(double t,double delta,const ScriptedRecenterParams& p,bool instant) noexcept {
        if(std::fabs(delta)<p.dead_zone_rad) {++skipped_;return;}
        from_=offset_;to_=offset_+delta;t0_=t;ease_=true;
        if(instant) {offset_=to_;ease_=false;}
    }
public:
    ScriptedRecenterOutput update(const ScriptedRecenterInput& in,const ScriptedRecenterParams& p={}) noexcept {
        std::optional<double> fold;
        const bool finite_t=std::isfinite(in.t);
        if((in_ || ease_ || res_pending_ || offset_!=0) && in.epoch!=epoch_) {in_=ease_=false;offset_=0;have_heading_=false;res_pending_=false;} // new owner: drop
        epoch_=in.epoch;
        if(in.authored) {
            res_pending_=false;
            const std::optional<double> rel=in.hmd_rel_yaw && std::isfinite(*in.hmd_rel_yaw) && finite_t ?
                std::optional<double>(wrap(*in.hmd_rel_yaw)) : std::nullopt;
            if(!in_) {
                in_=true;++entries_;snaps_=in.heading_snaps;
                if(rel) aim(in.t,*rel,p,in.instant);
            } else if(in.heading_snaps>snaps_) {
                snaps_=in.heading_snaps;
                if(rel) {++reaims_;aim(in.t,*rel,p,in.instant);}
            }
            if(in.heading && std::isfinite(*in.heading)) {heading_=*in.heading;have_heading_=true;}
        } else if(in_) {
            in_=false;
            double f=offset_;
            if(in.body_yaw && std::isfinite(*in.body_yaw) && have_heading_) f+=wrap(*in.body_yaw-heading_);
            const bool known=in.body_yaw && std::isfinite(*in.body_yaw) && have_heading_;
            if(!known && have_heading_) {res_pending_=true;res_started_=false;res_applied_=0;res_heading_=heading_;} // mark residual
            if(f!=0) {fold=f;++folds_;}
            offset_=0;ease_=false;have_heading_=false;
        }
        else if(res_pending_ && in.body_yaw && std::isfinite(*in.body_yaw) && finite_t) {
            if(!res_started_) {res_started_=true;res_t0_=in.t;res_err_=wrap(*in.body_yaw-res_heading_);}
            const double u=std::fmin(1.,std::fmax(0.,(in.t-res_t0_)/p.ease_s));
            const double target=u>=1?res_err_:res_err_*(u*u*(3-2*u));
            if(target!=res_applied_) {fold=fold.value_or(0)+(target-res_applied_);res_applied_=target;}
            if(u>=1) res_pending_=false;
        }
        if(ease_) {
            const double u=std::fmin(1.,std::fmax(0.,(in.t-t0_)/p.ease_s));
            offset_=from_+(to_-from_)*(u*u*(3-2*u));
            if(u>=1) {offset_=to_;ease_=false;}
        }
        return {offset_,fold,ease_,entries_,reaims_,folds_,skipped_,res_pending_};
    }
    void reset() noexcept {*this=ScriptedRecenter{};}
};
} // namespace d2vr
