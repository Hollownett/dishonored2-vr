#pragma once
// Vertical smoothing for the engine camera anchor: landings, crouching and stairs move the engine's eye height in
// steps that, rendered 1:1, wobble the player's view. The vertical axis (game z) follows the raw value through a
// first-order filter with a speed limit; horizontal axes pass through unchanged (locomotion latency makes people sick).
// A real fall (raw z dropping faster than fall_rate_mps for fall_hold_s) passes straight through, and anything that
// looks like a teleport, a new epoch or a bad time step resets to the raw value. Units: metres per second, the caller
// converts game units. Called once per frame; no allocation.
#include "arm_ik.hpp"
#include <cmath>
#include <cstdint>

namespace d2vr {
struct AnchorSmootherConfig {double tau_s=.12,max_rate_mps=1.5,fall_rate_mps=3,fall_hold_s=.1,teleport_m=1.0;};

class AnchorSmoother {
    AnchorSmootherConfig cfg_;
    bool have_{};
    std::uint64_t epoch_{},resets_{};
    Vec3 raw_{};
    double out_z_{},fall_s_{};
    Vec3 reset_to(const Vec3& raw,std::uint64_t epoch,bool count) noexcept {
        if(count) ++resets_;
        have_=true;epoch_=epoch;raw_=raw;out_z_=raw.z;fall_s_=0;
        return raw;
    }
public:
    explicit AnchorSmoother(AnchorSmootherConfig cfg={}) noexcept : cfg_(cfg) {}
    Vec3 update(const Vec3& raw,double dt,std::uint64_t epoch) noexcept {
        if(!finite(raw)) {have_=false;++resets_;return raw;} // nothing sane to follow; the next finite sample starts afresh
        if(!have_) return reset_to(raw,epoch,false);
        if(epoch!=epoch_ || !std::isfinite(dt) || dt<=0 || dt>.25) return reset_to(raw,epoch,true);
        const Vec3 d=raw-raw_;
        if(std::fabs(d.x)>cfg_.teleport_m || std::fabs(d.y)>cfg_.teleport_m || std::fabs(d.z)>cfg_.teleport_m)
            return reset_to(raw,epoch,true);
        if(-d.z/dt>cfg_.fall_rate_mps) fall_s_+=dt;else fall_s_=0;
        raw_=raw;
        if(fall_s_+1e-9>=cfg_.fall_hold_s) out_z_=raw.z;
        else {
            double step=(raw.z-out_z_)*(1-std::exp(-dt/cfg_.tau_s));
            const double limit=cfg_.max_rate_mps*dt;
            step=std::fmax(-limit,std::fmin(limit,step));
            out_z_+=step;
        }
        return {raw.x,raw.y,out_z_};
    }
    void reset() noexcept {have_=false;fall_s_=0;}
    std::uint64_t resets() const {return resets_;}
};
} // namespace d2vr
