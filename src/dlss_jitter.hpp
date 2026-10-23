#pragma once
// DLSS sub-pixel jitter: a 32-phase Halton(2,3) sequence replaces the engine's eight-phase 1/16 px pattern
// while a DLSS mode owns the projection (DLSS Programming Guide 3.7: at least 16 phases, 32 for DLAA-class
// ratios). Both eyes of a pair use the same phase (the engine's frame counter), exactly like the native pattern.
// Values are in render pixels with the ENGINE's sign (game_stereo.hpp: the projection shifts by +2j/W, +2j/H and
// the view stores j); the FSR/DLSS-facing jitter applies fsr_jitter_sign on top, as it does for the native pattern.
#include "game_stereo.hpp"
#include <array>
#include <cstdint>

namespace d2vr {
inline constexpr std::uint32_t dlss_jitter_phase_count=32;
// Van der Corput radical inverse for index>=1 (index 0 is the degenerate 0 sample and is skipped).
inline float halton(std::uint32_t index,std::uint32_t base) noexcept {
    float f=1,r=0;
    while(index) {f/=static_cast<float>(base);r+=f*static_cast<float>(index%base);index/=base;}
    return r;
}
// Phase p (any counter; wraps at 32) -> render-pixel offset in [-0.5,0.5).
inline std::array<float,2> dlss_jitter_px(std::uint32_t phase) noexcept {
    const std::uint32_t index=phase%dlss_jitter_phase_count+1;
    return {halton(index,2)-.5f,halton(index,3)-.5f};
}
// The sub-pixel sample for this frame counter in render pixels (engine sign): the native pattern, or the Halton
// sequence while DLSS owns the jitter. Used by both the forced projection and the TAA replacement so they agree.
inline std::array<float,2> taa_sample_px(std::uint32_t counter,bool dlss) noexcept {
    if(dlss) return dlss_jitter_px(counter);
    const auto& p=engine_taa_pattern[counter%8];
    return {p[0]/16.f,p[1]/16.f};
}
// The engine-side jitter (UV of the render size) for the forced projection and the view's stored vector.
inline TaaJitter taa_sample_uv(std::uint32_t counter,bool dlss,float width,float height) noexcept {
    const auto s=taa_sample_px(counter,dlss);
    return {s[0]/width,s[1]/height};
}
} // namespace d2vr
