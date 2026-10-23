#pragma once
#include <cstdint>

namespace d2vr {
// Identity of the source of camera history, independent of the XR pose epoch.
struct ActionCameraKey {
    std::uintptr_t player{},camera{};
    std::int32_t generation{};
    std::uint64_t pose_epoch{},reference_epoch{};
    bool authored{};
    bool operator==(const ActionCameraKey& b) const noexcept {
        return player==b.player && camera==b.camera && generation==b.generation &&
            pose_epoch==b.pose_epoch && reference_epoch==b.reference_epoch && authored==b.authored;
    }
};
// key: the context decided for this displayed frame at its first view; every view of the frame renders with it.
struct ActionCameraStep {bool accepted{},advance{},changed{};std::uint64_t history_epoch{};ActionCameraKey key{};};
class ActionCameraContext {
    bool have_{};
    std::uint64_t frame_{},history_epoch_{};
    ActionCameraKey key_{};
public:
    // Only the normal per-frame callback can begin a snapshot. The first view decides the context for the displayed frame; a later view of
    // the same frame is accepted and handed that decision (step.key), even if the engine's state changed between the two calls, so the
    // eyes never split between the head-owned and the native camera at a climb or takedown transition.
    ActionCameraStep sample(std::uint64_t frame,const ActionCameraKey& key,bool first) noexcept {
        if(have_ && frame==frame_) return {true,false,false,history_epoch_,key_};
        if(!first) return {};
        const bool changed=!have_ || !(key==key_);
        if(changed) ++history_epoch_;
        have_=true;frame_=frame;key_=key;
        return {true,true,changed,history_epoch_,key_};
    }
    void reset() noexcept {have_=false;history_epoch_=0;}
};
} // namespace d2vr
