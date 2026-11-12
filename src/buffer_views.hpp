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
// v1194: where a draw's view comes from. own_newest: its context wrote the newest view; on one-view frames (alternate) a context's own
// older write of the b1 it binds is its record-time snapshot and wins over any shared lookup; then the b1's last write by any context
// (b1_known); else the newest view anywhere (only paired frames, or a b1 never seen written). 0 own, 1 own snapshot, 2 b1, 4 newest.
inline unsigned view_source(bool own_newest,bool alternate,bool own_valid,const void* own_buffer,const void* bound,bool b1_known) noexcept {
    if(own_newest) return 0;
    if(alternate && own_valid && own_buffer && own_buffer==bound) return 1;
    if(alternate && b1_known) return 2;
    return 4;
}
} // namespace d2vr
