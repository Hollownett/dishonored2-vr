#pragma once

namespace d2vr {
// Diagnostic-only worker exception; never enabled by an ordinary VR profile.
constexpr bool valid_cleanup_exception_profile(int flag,bool simulated,bool xr,bool paired,
                                               bool native_eye,bool head,bool controllers,
                                               unsigned seconds,bool left_aim,bool saved_settings) noexcept {
    return flag==0 || (flag==1 && simulated && xr && paired && native_eye && head && controllers &&
                      seconds>=10 && seconds<=120 && !left_aim && !saved_settings);
}
} // namespace d2vr
