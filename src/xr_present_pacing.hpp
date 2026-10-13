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
} // namespace d2vr
