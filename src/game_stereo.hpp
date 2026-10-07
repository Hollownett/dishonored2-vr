#pragma once
#include "game_camera.hpp"

namespace d2vr {
// Exact-build, disassembly-derived profile. Runtime checks are still required.
inline constexpr std::uintptr_t prepared_views_rva=0x2bc58a0;
inline constexpr std::uintptr_t selected_mode_rva=0x2be6910;
inline constexpr std::uintptr_t single_mode_rva=0x1aca0b8;
inline constexpr std::uintptr_t side_by_side_mode_rva=0x1aca220;
inline constexpr std::uintptr_t separation_value_rva=0x2be694c;
inline constexpr std::uintptr_t screen_separation_value_rva=0x2be69cc;
inline constexpr std::size_t prepared_view_stride=0x3ab0;

inline bool valid_stereo_metadata(const std::array<std::byte,80>& bytes,std::uintptr_t base) {
    for(int eye=0;eye<2;++eye) {
        auto data=bytes.data()+eye*40;
        std::uintptr_t label{};
        std::uint32_t view_index{};
        std::array<float,4> viewport{};
        float side{};
        std::memcpy(&label,data,8);
        std::memcpy(&view_index,data+8,4);
        std::memcpy(viewport.data(),data+12,16);
        std::memcpy(&side,data+32,4);
        if(label!=base+0x1ab2848 || view_index!=0 ||
           viewport!=std::array<float,4>{eye==0?0.0f:0.5f,0.0f,0.5f,1.0f} ||
           side!=(eye==0?-1.0f:1.0f) || data[28]!=std::byte{1} || data[29]!=std::byte{1}) return false;
    }
    return true;
}
} // namespace d2vr
