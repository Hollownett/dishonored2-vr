#pragma once
#include <cstdint>

namespace d2vr {
inline constexpr std::uint64_t tracked_pose_freshness_ms=250;
inline constexpr double tracked_pose_freshness_seconds=tracked_pose_freshness_ms*.001;
} // namespace d2vr
