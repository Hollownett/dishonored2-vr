#pragma once
#include "vr_settings.hpp"

namespace d2vr {
// The native 1920x1080 pause menu keeps its navigation on the right. VR settings
// occupy its content area; image rendering and controller hit-testing share it.
struct SettingsMenuLayout {
    static constexpr double stage_width=1920,stage_height=1080;
    static constexpr double x=80,y=190,width=960,height=840;
    struct Rect {std::uint32_t x{},y{},width{},height{};};
    static Rect image_rect(std::uint32_t image_width,std::uint32_t image_height,
                           std::uint32_t band_y,std::uint32_t band_rows) {
        if(!image_width || !image_height || !band_rows || band_y>=image_height || band_rows>image_height-band_y)
            throw std::invalid_argument("Invalid native menu image band");
        const auto left=static_cast<std::uint32_t>(std::lround(x*image_width/stage_width));
        const auto right=static_cast<std::uint32_t>(std::lround((x+width)*image_width/stage_width));
        const auto top=static_cast<std::uint32_t>(std::lround(y*band_rows/stage_height));
        const auto bottom=static_cast<std::uint32_t>(std::lround((y+height)*band_rows/stage_height));
        if(right<=left || bottom<=top) throw std::invalid_argument("Native menu content area is too small");
        return {left,band_y+top,right-left,bottom-top};
    }
    // Input uses the cropped native menu stage, before mapping to the full eye image.
    static std::optional<PanelPointer> pointer(PanelPointer menu) noexcept {
        if(!std::isfinite(menu.u) || !std::isfinite(menu.v)) return {};
        const PanelPointer hit{(menu.u*stage_width-x)/width,(menu.v*stage_height-y)/height};
        if(hit.u<0 || hit.u>1 || hit.v<0 || hit.v>1) return {};
        return hit;
    }
};
} // namespace d2vr
