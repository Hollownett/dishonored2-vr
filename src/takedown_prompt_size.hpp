#pragma once
#include <algorithm>
namespace d2vr {
// Width in metres of the takedown (kill/choke) hint drawn as world art over the guard.
// Legacy: dist*0.05 clamped to 0.07..1 gave about 7.5 cm at 1.5 m, unreadable in the headset (plan G task 4).
// v624-v637: never under 0.25 m. Headset v637: too big, and the HUD size (x hud%/65 at draw) barely changed it.
// Now 0.16 m (2x legacy) up to 2 m, then constant angle; HUD size still scales it (85% -> 0.21 m).
constexpr double takedown_prompt_min_width=0.16;
constexpr double takedown_prompt_width(double distance_m,bool legacy) noexcept {
    if(!(distance_m>=0.)) distance_m=0.;
    return legacy?std::clamp(distance_m*.05,.07,1.):std::clamp(distance_m*.08,takedown_prompt_min_width,.6);
}
}
