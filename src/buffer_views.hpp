#pragma once
// v1193: the last classified per-view constants written into each per-view buffer (b1). A draw whose context did not write the newest
// view itself reads the view its bound b1 holds, not the newest one: with one view per engine frame the next render's Setup and view
// writes overlap this render's draw recording, and the newest view is then the OTHER eye's (swing dumps v1190/v1191: the held sword
// retargeted with the previous render's E landed ~300 px from the hand).
#include <array>
#include <cstdint>
#include <mutex>

namespace d2vr {
template<class V,std::size_t N=32> class BufferViews {
    struct Slot {const void* buffer{};V view{};std::uint64_t stamp{};};
    std::array<Slot,N> slots_{};std::uint64_t clock_{};mutable std::mutex mutex_;
public:
    void store(const void* buffer,const V& view) noexcept {
        if(!buffer) return;
        std::lock_guard<std::mutex> lock(mutex_);
        Slot* at=&slots_[0];
        for(auto& s:slots_) {if(s.buffer==buffer) {at=&s;break;} if(s.stamp<at->stamp) at=&s;} // that buffer's slot, else the oldest
        at->buffer=buffer;at->view=view;at->stamp=++clock_;
    }
    bool find(const void* buffer,V& out) const noexcept {
        if(!buffer) return false;
        std::lock_guard<std::mutex> lock(mutex_);
        for(const auto& s:slots_) if(s.buffer==buffer) {out=s.view;return true;}
        return false;
    }
    void clear() noexcept {std::lock_guard<std::mutex> lock(mutex_);slots_={};clock_=0;}
};
} // namespace d2vr
