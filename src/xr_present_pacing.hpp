#pragma once
#include <cstdint>

namespace d2vr {
// xrWaitFrame owns VR pacing. An independently vsynced desktop mirror can make
// the same render thread miss the headset deadline; retain native pacing when
// XR did not submit a visible frame or this is only a simulated diagnostic.
inline unsigned xr_desktop_sync_interval(unsigned native_interval,std::uint32_t flags,
    bool xr_capturing,bool real_runtime,bool frame_submitted,bool runtime_visible) noexcept {
    constexpr std::uint32_t present_test=1;
    return xr_capturing && real_runtime && frame_submitted && runtime_visible && !(flags&present_test)?0:native_interval;
}
// v833: interval 0 alone still let the mirror block: when the desktop queue was full, Present waited a whole monitor refresh
// (17.6 ms on 17% of frames, headset test 2026-11-02 at ~70 fps). While XR owns pacing the mirror frame is dropped instead
// (DXGI_PRESENT_DO_NOT_WAIT; DXGI_ERROR_WAS_STILL_DRAWING is then success for the game).
inline std::uint32_t xr_desktop_present_flags(std::uint32_t flags,bool xr_owns_pacing) noexcept {
    constexpr std::uint32_t do_not_wait=0x8;
    return xr_owns_pacing?flags|do_not_wait:flags;
}
} // namespace d2vr
