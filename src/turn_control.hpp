#pragma once
#include "vr_turning.hpp"
#include <algorithm>
#include <optional>

namespace d2vr {
// Game basis rows are forward/left/up: positive yaw turns left. Quest RX is
// positive to the right, unlike the donor's Unreal yaw convention.
inline double quest_turn_axis_to_native(double axis) {return -axis;}
// Controller samples are published on the XR thread; only a fresh native
// physics update consumes them. Smooth turning uses that update's timestep.
class TurnControl {
public:
    enum class Mode {native=0,snap=1,smooth=2};
    struct Config {Mode mode=Mode::native;double snap_degrees=45,smooth_degrees_per_second=90;
        double ramp_s=.15;}; // smooth turning eases in over ramp_s seconds after the stick leaves the deadzone (0 = off: full speed at once)
    struct Request {double radians{};std::uint64_t input_ms{},update{},epoch{};};
private:
    std::mutex mutex_;
    Config config_;
    SnapTurn snap_;
    double axis_{};
    std::uint64_t input_ms_{},epoch_{},last_update_{};
    bool live_=false,centred_=false;
    double ramp_t_{}; // seconds the stick has been past the deadzone, driving smooth turning
    void cancel() {snap_.suspend();live_=false;centred_=false;ramp_t_=0;}
    // Area under the 0..1 ramp between t0 and t0+dt, in seconds: turning stays independent of the native frame rate.
    static double ramp_seconds(double t0,double dt,double ramp) {
        if(ramp<=0) return dt;
        const auto area=[ramp](double t) {return t<ramp?t*t/(2*ramp):ramp/2+(t-ramp);};
        return area(t0+dt)-area(t0);
    }
public:
    void configure(Config config) {
        if(config.mode<Mode::native || config.mode>Mode::smooth ||
           !std::isfinite(config.smooth_degrees_per_second) || config.smooth_degrees_per_second<15 || config.smooth_degrees_per_second>360 ||
           !std::isfinite(config.ramp_s) || config.ramp_s<0 || config.ramp_s>1)
            throw std::invalid_argument("Invalid VR turning configuration");
        std::lock_guard<std::mutex> lock(mutex_);
        snap_.configure({config.mode==Mode::snap,config.snap_degrees,.6,.3});
        config_=config;live_=false;centred_=false;ramp_t_=0;input_ms_=0;epoch_=0;last_update_=0;
    }
    // Claim RX during configured gameplay even if its writer is unavailable:
    // falling back to a sensitivity-dependent native turn would be surprising.
    // Native menus and weapon wheels retain their original stick input.
    bool publish(double axis,bool active,bool gameplay,bool axis_available,bool writer_available,
                 std::uint64_t now,std::uint64_t epoch) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(epoch!=epoch_) {cancel();epoch_=epoch;}
        if(config_.mode==Mode::native || !active || !gameplay || !axis_available) {cancel();return false;}
        if(!writer_available || !std::isfinite(axis) || std::abs(axis)>1.01) {cancel();return true;}
        axis_=axis;input_ms_=now;live_=true;
        if(config_.mode==Mode::snap) snap_.observe(axis,true,true,now,epoch);
        else if(std::abs(axis)<.15) {centred_=true;ramp_t_=0;} // back in the deadzone: the next push eases in again
        return true;
    }
    std::optional<Request> consume(double dt,bool native_gameplay_ready,
            std::uint64_t now,std::uint64_t epoch,std::uint64_t update) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(epoch!=epoch_ || !native_gameplay_ready || !live_ || now<input_ms_ || now-input_ms_>250 ||
           !std::isfinite(dt) || dt<=0 || dt>.1 || !update || update<last_update_) {cancel();return {};}
        if(update==last_update_) return {};
        last_update_=update;
        double delta{};
        if(config_.mode==Mode::snap) delta=snap_.take(true,now,epoch);
        else if(config_.mode==Mode::smooth && centred_ && std::abs(axis_)>.15) {
            const auto speed=std::copysign((std::min(std::abs(axis_),1.)-.15)/.85,axis_);
            delta=speed*config_.smooth_degrees_per_second*3.14159265358979323846/180.*ramp_seconds(ramp_t_,dt,config_.ramp_s);
            ramp_t_+=dt;
        } else ramp_t_=0;
        if(delta==0) return {};
        return Request{delta,input_ms_,update,epoch};
    }
    // True while smooth turning owns the stick and it was centred after activation: the stick can really turn the player.
    bool smooth_live() {std::lock_guard<std::mutex> lock(mutex_);return config_.mode==Mode::smooth && live_ && centred_;}
    void suspend() {std::lock_guard<std::mutex> lock(mutex_);cancel();}
};

// Physics queues its entity transform; that transform can still be old when
// the step returns. Acknowledge on a later native update, without reapplying.
class TurnFeedback {
public:
    struct Owner {
        std::uintptr_t player{},delegate{};
        std::int32_t camera_generation{},physics_generation{};
        bool operator==(const Owner& b) const {
            return player==b.player && delegate==b.delegate && camera_generation==b.camera_generation && physics_generation==b.physics_generation;
        }
    };
    struct Pending {Owner owner;TurnControl::Request request;double before{},expected{},dt{};std::uint64_t applied_ms{};};
    enum class State {none,waiting,acknowledged,cancelled,failed};
private:
    std::optional<Pending> pending_;
public:
    std::optional<Pending> pending() const {return pending_;}
    void begin(Pending value) {
        if(pending_ || !std::isfinite(value.expected) || !std::isfinite(value.before))
            throw std::logic_error("Invalid or overlapping native turn feedback");
        pending_=value;
    }
    State poll(Owner owner,double physics_heading,double entity_heading,bool gameplay_owns_heading,std::uint64_t now) {
        if(!pending_) return State::none;
        const auto p=*pending_;
        if(!(owner==p.owner) || !gameplay_owns_heading || now<p.applied_ms) {pending_.reset();return State::cancelled;}
        constexpr double tau=6.283185307179586;
        if(!std::isfinite(physics_heading) || !std::isfinite(entity_heading) ||
           std::abs(std::remainder(physics_heading-p.expected,tau))>.001) {pending_.reset();return State::failed;}
        if(std::abs(std::remainder(entity_heading-p.expected,tau))<.001) {pending_.reset();return State::acknowledged;}
        if(now-p.applied_ms>500) {pending_.reset();return State::failed;}
        return State::waiting;
    }
    void cancel() {pending_.reset();}
};
} // namespace d2vr
