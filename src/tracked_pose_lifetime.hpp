#pragma once
#include <cstdint>

namespace d2vr {
inline constexpr std::uint64_t tracked_pose_freshness_ms=250;
// How far (ms) a callback may have read its clock before the newest tracking publication and still use it. A callback that began
// before the head-view thread published is not looking at a stale pose; GetTickCount64 also moves in 15.6 ms quanta.
inline constexpr std::uint64_t tracked_pose_future_skew_ms=100;
inline constexpr double tracked_pose_freshness_seconds=tracked_pose_freshness_ms*.001;
} // namespace d2vr
