#pragma once
#include <cmath>
#include <cstdint>
namespace d2vr {
// v1041: a resolution step grows the engine's render targets, our FSR/DLSS contexts and the XR eye chains with the pixel count. No code
// checked the GPU's memory before; an allocation that fails inside the engine ends the game by its own fatal-error path (no crash
// report), which is what "the game just closes when I change the resolution scale" looks like. This is the pure decision; dxgi_probe
// feeds it IDXGIAdapter3::QueryVideoMemoryInfo (this process's usage and budget, local segment) once per applied step.
struct VramState {std::uint64_t usage{},budget{};bool valid{};};
// `scalable_share` is the part of today's usage that scales with the output pixel count; `limit` the budget fraction that may be used.
// ponytail: both are unmeasured (resolution-apply.log records usage/budget before and after every step to fit them); 0.5 / 0.95 refuse
// only a step that clearly cannot fit. Raise the share if a crash still shows up below the limit.
inline constexpr double vram_scalable_share=.5,vram_limit=.95;
inline bool resolution_fits_vram(const VramState& state,double pixel_ratio,double share=vram_scalable_share,double limit=vram_limit) noexcept {
    if(!state.valid || !state.budget || !std::isfinite(pixel_ratio) || pixel_ratio<=1) return true; // shrinking, or nothing known: allowed
    return static_cast<double>(state.usage)*(1+share*(pixel_ratio-1))<=static_cast<double>(state.budget)*limit;
}
// The largest multiplier at most `requested` (5 % steps down, never below `current` when `current` itself is smaller) whose step from
// `current` fits. Pixel count follows the square of the multiplier.
inline double fit_resolution_multiplier(const VramState& state,double current,double requested) noexcept {
    if(!(current>0) || !(requested>current)) return requested;
    double m=requested;
    while(m>current+1e-9) {
        if(resolution_fits_vram(state,(m/current)*(m/current))) return m;
        m-=.05;
    }
    return current;
}
}
