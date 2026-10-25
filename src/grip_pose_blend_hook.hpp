#pragma once
#include <atomic>
#include <cstdint>
namespace d2vr {
// SEAM for the merge with Track F (hands plan Task 9, ruling H6). Task 14 (a living-NPC hold) must start Task 9's GripPoseBlend (the 0.25 s ease of the
// fingers into the grip, G4) when the hold starts, but GripPoseBlend is built in the other worktree. Task 14 therefore calls grip_pose_blend_start()
// here: a no-op until Track F stores its starter in grip_pose_blend_hook, e.g. at install:
//     d2vr::grip_pose_blend_hook.store(+[](unsigned side,double radius_m,std::uint64_t now_ms){ grip_pose_blend[side].start(...); });
// side: 0 left, 1 right; radius_m: the held bone's radius (the cylinder the fingers wrap); now_ms: GetTickCount64() of the start (input thread).
using GripPoseBlendStart=void (*)(unsigned side,double radius_m,std::uint64_t now_ms);
inline std::atomic<GripPoseBlendStart> grip_pose_blend_hook{nullptr};
inline void grip_pose_blend_start(unsigned side,double radius_m,std::uint64_t now_ms) noexcept {
    if(const auto start=grip_pose_blend_hook.load(std::memory_order_acquire)) start(side,radius_m,now_ms);
}
} // namespace d2vr
