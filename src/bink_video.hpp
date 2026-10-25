#pragma once
// Track T C1: is a pre-rendered Bink video playing? Pure tracker (BinkTracker) fed by thin hooks on the public bink2w64.dll exports
// BinkOpen / BinkOpenWithOptions (the HBINK they return), BinkClose, BinkDoFrame, BinkNextFrame. "Playing" = an open handle that decoded or
// advanced a frame within the last 500 ms. A video plays outside the player state machine, so this is the only reliable signal; with it a
// cutscene frame goes to the theater at once (no 30-frame black wait). The hooks are optional: no module, no export or a hook failure
// leaves playing() false and everything else as before (fail soft).
#include <array>
#include <cstdint>

namespace d2vr {
class BinkTracker {
    struct Slot {std::uintptr_t handle{};std::uint64_t frame_ms{};bool used{};};
    std::array<Slot,8> slots_{};
    Slot* find(std::uintptr_t h) noexcept {for(auto& s:slots_) if(s.used && s.handle==h) return &s;return nullptr;}
public:
    static constexpr std::uint64_t active_ms=500;
    void open(std::uintptr_t h,std::uint64_t now) noexcept {
        if(!h) return;
        if(auto* s=find(h)) {s->frame_ms=now;return;}
        for(auto& s:slots_) if(!s.used) {s={h,now,true};return;}
        Slot* oldest=&slots_[0];for(auto& s:slots_) if(s.frame_ms<oldest->frame_ms) oldest=&s;   // a leaked handle never blocks a new one
        *oldest={h,now,true};
    }
    void close(std::uintptr_t h) noexcept {if(auto* s=find(h)) *s={};}
    // A frame of a handle we never saw opened (bink2w64.dll loaded after the game's BinkOpen, so the open was missed) is adopted: that video is playing.
    void frame(std::uintptr_t h,std::uint64_t now) noexcept {if(auto* s=find(h)) s->frame_ms=now;else open(h,now);}
    bool playing(std::uint64_t now) const noexcept {
        for(const auto& s:slots_) if(s.used && now>=s.frame_ms && now-s.frame_ms<=active_ms) return true;
        return false;
    }
    unsigned open_count() const noexcept {unsigned n=0;for(const auto& s:slots_) n+=s.used;return n;}
};
} // namespace d2vr
