#pragma once
#include <algorithm>
namespace d2vr {
// Width in metres of the takedown (kill/choke) hint drawn as world art over the guard.
// Legacy: dist*0.05 clamped to 0.07..1 gave about 7.5 cm at 1.5 m, unreadable in the headset (plan G task 4).
// Now: never under 0.25 m, so the glyph is legible at kill range.
constexpr double takedown_prompt_min_width=0.25;
constexpr double takedown_prompt_width(double distance_m,bool legacy) noexcept {
    if(!(distance_m>=0.)) distance_m=0.;
    return legacy?std::clamp(distance_m*.05,.07,1.):std::clamp(distance_m*.12,takedown_prompt_min_width,1.);
}
}
