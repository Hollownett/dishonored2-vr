#pragma once
#include "tracked_pose.hpp"
#include <openxr/openxr.h>
#include <optional>
namespace d2vr {
inline std::optional<double> xr_floor_datum(const XrSpaceLocation& location) {
    constexpr auto required=XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
    if((location.locationFlags&required)!=required) return {};
    const auto& p=location.pose;
    if(!std::isfinite(p.position.y) || std::abs(p.position.y)>3) return {};
    try {
        const auto up=rotated(normalized({p.orientation.x,p.orientation.y,p.orientation.z,p.orientation.w}),{0,1,0});
        if(length(up-Vec3{0,1,0})>.02) return {};
    } catch(const std::invalid_argument&) {return {};}
    return p.position.y;
}
}
