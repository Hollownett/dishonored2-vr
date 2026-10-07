#pragma once
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>

namespace d2vr {
// This layout is supported ONLY for the exact executable below. Live input
// correlation established reads, not safe camera writes or animation hooks.
inline constexpr char game_sha256[]="b671f8249422862bf557d00aab2f4bb671ed4515cc10ad8a8a9decad84516180";
inline constexpr std::uintptr_t camera_vtable=0x1c72218;
inline constexpr std::uintptr_t camera_secondary_vtable=0x1c722f8;
inline constexpr std::size_t camera_read_size=0x120;

struct GameCameraPose {
    float horizontal_fov{}, vertical_fov{};
    std::array<float,3> position{};
    std::array<float,9> basis{};
};

inline std::optional<GameCameraPose> decode_view_fields(
        const std::array<std::byte,camera_read_size>& bytes, std::size_t prefix=0x30) {
    if(prefix>0x30) return std::nullopt;
    GameCameraPose pose;
    std::memcpy(&pose.horizontal_fov,bytes.data()+prefix+0x9c,4);
    std::memcpy(&pose.vertical_fov,bytes.data()+prefix+0xa0,4);
    std::memcpy(pose.position.data(),bytes.data()+prefix+0xa8,12);
    std::memcpy(pose.basis.data(),bytes.data()+prefix+0xcc,36);
    if (!(pose.horizontal_fov>0.05f && pose.horizontal_fov<3.13f &&
          pose.vertical_fov>0.05f && pose.vertical_fov<3.13f)) return std::nullopt;
    for (float value : pose.position)
        if (!std::isfinite(value) || std::abs(value)>1e7f) return std::nullopt;
    for (float value : pose.basis) if (!std::isfinite(value)) return std::nullopt;
    for (int i=0;i<3;++i) for (int j=i;j<3;++j) {
        float dot{};
        for (int k=0;k<3;++k) dot+=pose.basis[i*3+k]*pose.basis[j*3+k];
        if (std::abs(dot-(i==j?1.0f:0.0f))>0.02f) return std::nullopt;
    }
    const auto& b=pose.basis;
    float det=b[0]*(b[4]*b[8]-b[5]*b[7])-b[1]*(b[3]*b[8]-b[5]*b[6])+b[2]*(b[3]*b[7]-b[4]*b[6]);
    if (std::abs(det-1.0f)>0.02f) return std::nullopt;
    return pose;
}

// Native renderView_t has no component/descriptor prefix. Its basis rows are
// forward, left, up; idRenderView::Setup copies this to the render side at +0x39d0.
// Independently corroborated by itsloopyo's MIT Steam profile (see notices).
inline std::optional<GameCameraPose> decode_render_view(
        const std::array<std::byte,camera_read_size>& bytes) {
    return decode_view_fields(bytes,0);
}

inline std::optional<GameCameraPose> decode_game_camera(
        const std::array<std::byte,camera_read_size>& bytes, std::uintptr_t image_base) {
    std::uintptr_t primary{}, secondary{};
    std::memcpy(&primary,bytes.data(),8);
    std::memcpy(&secondary,bytes.data()+0x20,8);
    if (primary!=image_base+camera_vtable || secondary!=image_base+camera_secondary_vtable)
        return std::nullopt;
    return decode_view_fields(bytes);
}
} // namespace d2vr
