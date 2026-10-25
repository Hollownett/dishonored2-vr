#pragma once
// Locomotion comfort vignette: the opacity of a head-locked dark ring that closes in while the player is moved or
// turned by the stick (artificial motion), and eases away at rest. Pure logic; GameXr draws it. Physical head
// motion never feeds it, and snap turns are instant so they never do.
#include "turn_control.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace d2vr {
struct VignetteInput {double move_speed_mps{};double turn_rate_rad_s{};double dt{};};

class ComfortVignette {
public:
    static constexpr double full_move_mps=3.0,full_turn_rad_s=1.5,rise_s=.1,fall_s=.3;
    static constexpr float default_max_opacity=.6f;
private:
    float max_opacity_=default_max_opacity,opacity_{};
    bool enabled_=true;
public:
    void set_max_opacity(float value) noexcept {max_opacity_=std::isfinite(value)?std::clamp(value,0.f,1.f):default_max_opacity;}
    void set_enabled(bool value) noexcept {enabled_=value;if(!value) opacity_=0;}
    float max_opacity() const noexcept {return max_opacity_;}
    bool enabled() const noexcept {return enabled_;}
    float opacity() const noexcept {return opacity_;}
    // Slews linearly towards the target: 0 -> max in rise_s, max -> 0 in fall_s. A bad dt leaves the opacity alone.
    void reset() noexcept {opacity_=0;}
    float update(const VignetteInput& in) noexcept {
        if(!enabled_) {opacity_=0;return 0;}
        if(!std::isfinite(in.dt) || in.dt<=0) return opacity_;
        const double move=std::isfinite(in.move_speed_mps)?std::fabs(in.move_speed_mps)/full_move_mps:0;
        const double turn=std::isfinite(in.turn_rate_rad_s)?std::fabs(in.turn_rate_rad_s)/full_turn_rad_s:0;
        const double target=max_opacity_*std::clamp(std::max(move,turn),0.,1.);
        const double step=max_opacity_*std::min(in.dt,1.)/(target>opacity_?rise_s:fall_s);
        const double next=target>opacity_?std::min(target,opacity_+step):std::max(target,opacity_-step);
        opacity_=static_cast<float>(std::clamp(next,0.,static_cast<double>(max_opacity_)));
        return opacity_;
    }
};

// The vignette only has a non-zero target during free gameplay: not in authored scenes (takedowns, cutscenes), menus or the
// settings panel, nor while loading (gameplay false). recenter_easing: the 0.4 s scripted-moment turn (G1) lifts the authored
// inhibit for the ease only.
inline bool vignette_target_allowed(bool gameplay,bool authored,bool menu_open,bool settings_open,bool recenter_easing=false) noexcept {
    return gameplay && (!authored || recenter_easing) && !menu_open && !settings_open;
}

// Knob file `comfort-vignette`: a strength 0..1. Anything unreadable keeps the default.
inline float parse_vignette_strength(const std::string& text,float fallback=ComfortVignette::default_max_opacity) {
    const char* begin=text.c_str();char* end{};
    const double value=std::strtod(begin,&end);
    if(end==begin || !std::isfinite(value)) return fallback;
    while(*end==' ' || *end=='\t' || *end=='\r' || *end=='\n') ++end;
    if(*end) return fallback;
    return static_cast<float>(std::clamp(value,0.,1.));
}

// Stick locomotion speed. The game's own walk speed is not read; 3 m/s is the nominal full-stick speed (it matches the
// vignette's full-strength speed), so a half-pushed stick gives half strength.
inline double stick_move_speed_mps(double move_x,double move_y,double nominal_mps=ComfortVignette::full_move_mps) {
    if(!std::isfinite(move_x) || !std::isfinite(move_y)) return 0;
    const double magnitude=std::min(std::hypot(move_x,move_y),1.);
    return magnitude<.15?0:magnitude*nominal_mps;
}

// Only smooth turning counts: a snap turn is instant, and native mode is not ours to judge.
inline double stick_turn_rate_rad_s(TurnControl::Mode mode,double turn_axis,double degrees_per_second) {
    if(mode!=TurnControl::Mode::smooth || !std::isfinite(turn_axis) || !std::isfinite(degrees_per_second)) return 0;
    const double magnitude=std::min(std::fabs(turn_axis),1.);
    return magnitude<=.15?0:(magnitude-.15)/.85*degrees_per_second*3.14159265358979323846/180.;
}

// The ring texture: RGBA8, black, alpha 0 inside `clear_radius` (as a fraction of the half-width) and rising smoothly to 255
// at the edge midpoints and beyond (the corners).
inline std::vector<std::uint8_t> vignette_ring_image(unsigned size,double clear_radius=.55) {
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(size)*size*4,0);
    for(unsigned y=0;y<size;++y) for(unsigned x=0;x<size;++x) {
        const double dx=(x+.5)/size*2-1,dy=(y+.5)/size*2-1,r=std::hypot(dx,dy);
        const double t=std::clamp((r-clear_radius)/(1-clear_radius),0.,1.);
        pixels[(static_cast<std::size_t>(y)*size+x)*4+3]=static_cast<std::uint8_t>(std::lround(t*t*(3-2*t)*255));
    }
    return pixels;
}
} // namespace d2vr
