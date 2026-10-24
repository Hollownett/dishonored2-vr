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
// A blade cast that meets the player's own body is not a surface: the player's entity always; a hit with no known entity (0) also when its point lies
// inside the body column (.15 m around the root, from .15 m above the feet to 1.9 m). A known non-player entity (wall, guard, floor) is a real hit.
inline bool blade_hit_is_self(std::uintptr_t hit_entity,Vec3 hit_point,std::uintptr_t player,Vec3 player_root) noexcept {
    if(player && hit_entity==player) return true;
    if(hit_entity) return false;
    return std::hypot(hit_point.x-player_root.x,hit_point.y-player_root.y)<.15 && hit_point.z>=player_root.z+.15 && hit_point.z<=player_root.z+1.9;
}

} // namespace d2vr
