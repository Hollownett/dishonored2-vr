#pragma once
#include "tracked_pose.hpp"
#include <array>
#include <cstdint>
#include <optional>

namespace d2vr {
// Object-local cue. It faces the eyes and sits just above the visible surface;
// there is no hand-to-object ribbon or controller-relative marker offset.
inline std::optional<TrackedPose> grab_indicator_pose(Vec3 object,const TrackedPose& head) {
    if(!finite(object) || !finite(head.position)) return {};
    const auto to_eye=head.position-object;const double distance=length(to_eye);
    if(distance<.05 || distance>4) return {};
    return TrackedPose{object+to_eye*(.008/distance),normalized(head.orientation)};
}
inline std::array<std::uint8_t,32*32*4> grab_indicator_image() {
    std::array<std::uint8_t,32*32*4> pixels{};
    for(int y=0;y<32;++y) for(int x=0;x<32;++x) {
        const double r=std::hypot(x-15.5,y-15.5);
        const double alpha=std::exp(-std::pow((r-9.5)/1.25,2));
        const auto i=static_cast<std::size_t>((y*32+x)*4);
        pixels[i]=235;pixels[i+1]=226;pixels[i+2]=207;
        pixels[i+3]=static_cast<std::uint8_t>(std::clamp(alpha*230,0.,255.));
    }
    return pixels;
}
} // namespace d2vr
