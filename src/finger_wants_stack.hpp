#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

// G4 (plan Task 10): what each finger asks for before any contact, composed in the spec's priority order ("Settled after the builder check").
// Lowest first: the touch curl; the hover pre-curl, Bonelab Grip.OnHandHoverUpdate (fingers 1-4 toward 0.5 * squeeze, scaled by closeness:
// 0 at 0.45 m, 1 at contact); the pinch for an item under 3 cm bounds radius (a blend of 0.3 * closeness toward a pinch pose); the loot take
// close (the held floor times TakeClose's weight: Bonelab LerpPose 0.1 s in, 0.25 s out after the item is gone); the held wrap (a hold keeps
// its held wants, as today). The world contact (finger_contact) then caps all of them: the surface contact is always the last stage, so a
// hand reaching a coin on a table pre-curls only as far as the table allows. Visual only. Pure. Curl order: thumb, index, middle, ring, pinky.
namespace d2vr::rig {
inline constexpr double hover_precurl_reach=.45,hover_precurl_share=.5,pinch_radius=.03,pinch_weight=.3,take_close_in=.1,take_close_out=.25;
inline constexpr float take_close_floor=.6f; // the held floor (FingerTouchParams::held_floor, Bonelab's 0.6 held clamp)
inline constexpr std::array<float,5> pinch_pose{.6f,.55f,.15f,.15f,.15f};
struct WantsInput {std::array<float,5> touch{};bool held{};std::optional<double> hover_distance;double hover_radius_bounds{};
    float squeeze{};double take_weight{};};   // take_weight from TakeClose
inline std::array<float,5> compose_finger_wants(const WantsInput& in) noexcept {
    const auto unit=[](double v) {return std::isfinite(v)?std::clamp(v,0.,1.):0.;};
    std::array<float,5> out{};
    for(unsigned f=0;f<5;++f) out[f]=static_cast<float>(unit(in.touch[f]));          // 1. the touch curl
    if(in.held) return out;                                                          // 5. the held wrap overrides everything below it
    const double closeness=in.hover_distance && std::isfinite(*in.hover_distance)?std::clamp(1-*in.hover_distance/hover_precurl_reach,0.,1.):0.;
    const double precurl=hover_precurl_share*unit(in.squeeze)*closeness;
    for(unsigned f=1;f<5;++f) out[f]=std::max(out[f],static_cast<float>(precurl));   // 2. the hover pre-curl
    if(closeness>0 && std::isfinite(in.hover_radius_bounds) && in.hover_radius_bounds<pinch_radius) {
        const double w=pinch_weight*closeness;                                       // 3. the pinch, small items only
        for(unsigned f=0;f<5;++f) out[f]=static_cast<float>(out[f]+(pinch_pose[f]-out[f])*w);
    }
    const float take=static_cast<float>(take_close_floor*unit(in.take_weight));
    for(auto& c:out) c=std::max(c,take);                                             // 4. the take close
    return out;
}
// The loot take close: press() on the squeeze that takes loot, gone() when the item disappears. 0 -> 1 over 0.1 s from the press, then from the
// weight it had when gone, down to 0 over 0.25 s. Each edge continues from the weight it finds, so no step. Seconds on one steady clock.
class TakeClose {
public:
    void press(double t) {const double w=weight(t);pressed_=true;gone_=false;t0_=t;w0_=w;}
    void gone(double t) {if(!pressed_ || gone_) return;wg_=weight(t);gone_=true;tg_=t;}
    double weight(double t) const {
        if(!pressed_ || !std::isfinite(t)) return 0.;
        if(gone_) return wg_*std::clamp(1-(t-tg_)/take_close_out,0.,1.);
        return std::clamp(w0_+std::max(0.,t-t0_)/take_close_in,0.,1.);
    }
    bool pressed() const {return pressed_ && !gone_;}
private:
    bool pressed_{},gone_{};double t0_{},w0_{},tg_{},wg_{};
};
}
