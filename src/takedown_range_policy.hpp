#pragma once
// How close a guard must be before the mod offers or triggers a takedown (headset report 2026-10-22: "triggers too far from the guardian").
//
// The shipped game offers a choke at 3 m / 1.7 m and a kill at distance <= 3 m, height <= 1.7 m, both measured from the BODY ORIGIN,
// which trails the head in VR, so a guard still looks far away when the offer fires. The old policy WIDENED the choke gates x1.5.
// Now one scale S in [0.5, 1.0] (build/takedown-range-scale, default 0.75) only ever shrinks them:
//  * choke: the private-view substitution (NativeChokeDistanceScope) scales the distance and height gates by S and leaves the yaw windows
//    native (a narrower window would make a guard in front harder to grab, not closer);
//  * kill: the engine's assassination rule thresholds are shared game data and are never written (docs/REPAIR-2026-10-15-v536.md removed
//    every shared write), so the mod limits what it OFFERS and what it TRIGGERS to the same scaled box: a kill sensor target farther
//    than 3*S m (or 1.7*S m above/below) is treated as not offered, which drops the world takedown prompt and the mod-initiated kill.
//    A kill the player starts with the game's own attack button is still the game's 3 m rule (documented in docs/PLAYING.md).
// Pickpocket: it rides the shared interaction sensor (comp 0x1b403b8, decl +0x70 = 3.0 m, shared with doors and items, not scaled) and no
// pickpocket-specific sensor or decl is identified yet; see docs/native-takedown-range.md section 5.
#include "native_takedown.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>

namespace d2vr {
inline constexpr float takedown_scale_default=.75f,takedown_scale_min=.5f,takedown_scale_max=1.f;
inline constexpr float kill_native_distance=3.f,kill_native_height=1.7f; // assassination_base.rulehandler fast rules (docs/native-takedown-range.md 2b)

// build/takedown-range-scale: a number from 0.5 to 1.0. Empty, unreadable or non-finite text gives the default; out-of-range numbers clamp.
inline float takedown_range_scale_from_knob(const std::string& text) noexcept {
    const char* b=text.c_str();while(*b==' '||*b=='\t'||*b=='\r'||*b=='\n') ++b;
    char* end=nullptr;const double v=std::strtod(b,&end);
    if(end==b || !std::isfinite(v)) return takedown_scale_default;
    return static_cast<float>(v<takedown_scale_min?takedown_scale_min:v>takedown_scale_max?takedown_scale_max:v);
}
// The scale the private view is given: never above native, never below the knob floor.
inline float choke_view_factor(float scale) noexcept {
    if(!std::isfinite(scale)) return takedown_scale_default;
    return std::clamp(scale,takedown_scale_min,takedown_scale_max);
}
// The kill box for this scale (metres): 3*S distance, 1.7*S height.
inline bool kill_within_scaled_range(const std::array<float,3>& player,const std::array<float,3>& target,float scale) noexcept {
    for(unsigned i=0;i<3;++i) if(!std::isfinite(player[i]) || !std::isfinite(target[i])) return true; // unreadable: leave the native verdict alone
    const float s=choke_view_factor(scale);
    const double dx=target[0]-player[0],dy=target[1]-player[1],dz=target[2]-player[2];
    return std::sqrt(dx*dx+dy*dy+dz*dz)<=kill_native_distance*s && std::fabs(dz)<=kill_native_height*s;
}
// The sensor poll with the kill taken away: not a kill, not offered (unless a drop kill is what the engine offered), spawn re-derived.
inline TakedownState without_kill(TakedownState state) noexcept {
    state.kill=false;state.kill_spawn=0;
    if(!state.drop) state.offered=false;
    state.spawn=state.choke?state.choke_spawn:state.drop?state.drop_spawn:0;
    return state;
}
// Applies the kill box to one poll. Fails open (native behaviour) whenever an origin cannot be read.
template<class Read> TakedownState limit_takedown_to_scale(Read& read,std::uintptr_t base,TakedownState state,float scale) {
    if(!(scale<takedown_scale_max) || !state.kill || !state.kill_spawn) return state;
    using namespace rig;
    const auto game=value<std::uintptr_t>(read,base+0x39a5290);
    if(!game || !typed(read,base,*game,0x1c940a8)) return state;
    const auto player=value<std::uintptr_t>(read,*game+0x3130b0);
    if(!player || !typed(read,base,*player,0x1c5e428)) return state;
    const auto from=value<std::array<float,3>>(read,*player+0x300);
    const auto target=takedown_target(read,base,state.kill_spawn);
    if(!from || !target) return state;
    return kill_within_scaled_range(*from,target->origin,scale)?state:without_kill(state);
}
}
