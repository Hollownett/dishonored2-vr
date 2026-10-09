#pragma once
#include "vr_settings.hpp"
#include <array>
#include <cstdint>

namespace d2vr {
inline TrackedPose menu_pointer_pose(const TrackedPose& panel,PanelPointer hit,double width,double height) {
    if(!std::isfinite(hit.u) || !std::isfinite(hit.v) || hit.u<0 || hit.u>1 || hit.v<0 || hit.v>1 ||
       !std::isfinite(width) || !std::isfinite(height) || width<=0 || height<=0 || !finite(panel.position))
        throw std::invalid_argument("Invalid menu pointer location");
    const auto orientation=normalized(panel.orientation);
    return {panel.position+rotated(orientation,{(hit.u-.5)*width,(.5-hit.v)*height,.002}),orientation};
}
inline std::array<std::uint8_t,32*32*4> menu_pointer_image(bool pressed) {
    std::array<std::uint8_t,32*32*4> pixels{};
    for(int y=0;y<32;++y) for(int x=0;x<32;++x) {
        const double radius=std::hypot(x-15.5,y-15.5);
        if(radius>13) continue;
        const auto offset=static_cast<std::size_t>((y*32+x)*4);
        const bool outline=radius>10.5;
        pixels[offset]=outline?12:static_cast<std::uint8_t>(pressed?255:80);
        pixels[offset+1]=outline?12:static_cast<std::uint8_t>(pressed?190:235);
        pixels[offset+2]=outline?12:static_cast<std::uint8_t>(pressed?70:255);
        pixels[offset+3]=255;
    }
    return pixels;
}
} // namespace d2vr
