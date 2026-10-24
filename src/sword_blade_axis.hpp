#pragma once
// The ONE sword blade-axis convention (G2): the blade lies along the grip's forward axis (basis[0]); a reverse grip points it the other way.
// Collision, the aligns, the poke, the tip and the combat aim all build the axis here; nothing else negates basis[0].
#include "tracked_pose.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace d2vr {

inline Vec3 sword_blade_axis(const WorldTrackedPose& grip,bool reversed) noexcept {return reversed?grip.basis[0]*-1.:grip.basis[0];}
// Angle between two axes in degrees; 180 when either is not a usable direction.
inline double blade_axis_gap_deg(Vec3 a,Vec3 b) noexcept {
    const double la=length(a),lb=length(b);
    if(!finite(a) || !finite(b) || !(la>1e-9) || !(lb>1e-9)) return 180.;
    return std::acos(std::clamp(dot(a,b)/(la*lb),-1.,1.))*180./3.14159265358979323846;
}
// A blade cast that meets the player's own body (its entity, or a point inside the body column around the root) is not a surface.
inline bool blade_hit_is_self(std::uintptr_t hit_entity,Vec3 hit_point,std::uintptr_t player,Vec3 player_root) noexcept {
    if(player && hit_entity==player) return true;
    return std::hypot(hit_point.x-player_root.x,hit_point.y-player_root.y)<.30 && hit_point.z>=player_root.z && hit_point.z<=player_root.z+1.9;
}

} // namespace d2vr
