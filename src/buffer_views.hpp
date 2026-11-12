#pragma once
// v1193: the last classified per-view constants written into each per-view buffer (b1). A draw whose context did not write the newest
// view itself reads the view its bound b1 holds, not the newest one: with one view per engine frame the next render's Setup and view
// writes overlap this render's draw recording, and the newest view is then the OTHER eye's (swing dumps v1190/v1191: the held sword
// retargeted with the previous render's E landed ~300 px from the hand).
#include <array>
#include <cstdint>
#include <mutex>

namespace d2vr {
// v1196: hashed (1024 slots, 16-slot probe window; the oldest in the window is replaced). v1195 sims still sent ~1% of one-view draws, rig
// draws among them, to the newest view: the 32-slot LRU table was evicted by the many per-view buffers a frame writes before the draw looked up.
template<class V,std::size_t N=1024> class BufferViews {
    static_assert((N&(N-1))==0,"power of two");
    static constexpr std::size_t probe=N<16?N:16;
    struct Slot {const void* buffer{};V view{};std::uint64_t stamp{};};
    std::array<Slot,N> slots_{};std::uint64_t clock_{};mutable std::mutex mutex_;
    static std::size_t home(const void* b) noexcept {return static_cast<std::size_t>((reinterpret_cast<std::uintptr_t>(b)>>4)*0x9e3779b97f4a7c15ull>>40)&(N-1);}
public:
    void store(const void* buffer,const V& view) noexcept {
        if(!buffer) return;
        std::lock_guard<std::mutex> lock(mutex_);
        const std::size_t h=home(buffer);Slot* at=&slots_[h];
        for(std::size_t i=0;i<probe;++i) {Slot& s=slots_[(h+i)&(N-1)];if(s.buffer==buffer) {at=&s;break;} if(s.stamp<at->stamp) at=&s;} // that buffer's slot, else the oldest in the window
        at->buffer=buffer;at->view=view;at->stamp=++clock_;
    }
    bool find(const void* buffer,V& out) const noexcept {
        if(!buffer) return false;
        std::lock_guard<std::mutex> lock(mutex_);
        const std::size_t h=home(buffer);
        for(std::size_t i=0;i<probe;++i) {const Slot& s=slots_[(h+i)&(N-1)];if(s.buffer==buffer) {out=s.view;return true;}}
        return false;
    }
    void clear() noexcept {std::lock_guard<std::mutex> lock(mutex_);slots_={};clock_=0;}
};
// v1194: where a draw's view comes from. own_newest: its context wrote the newest view; on one-view frames (alternate) a context's own
// older write of the b1 it binds is its record-time snapshot and wins over any shared lookup; then the b1's last write by any context
// (b1_known); else the newest view anywhere (only paired frames, or a b1 never seen written). 0 own, 1 own snapshot, 2 b1, 4 newest, 5 own view of another buffer (v1197).
inline unsigned view_source(bool own_newest,bool alternate,bool own_valid,const void* own_buffer,const void* bound,bool b1_known) noexcept {
    if(own_newest) return 0;
    if(alternate && own_valid && own_buffer && own_buffer==bound) return 1;
    if(alternate && b1_known) return 2;
    // v1197: a b1 no classified write ever filled (v1196 runs: 99.99% of the remaining fallbacks, rig and near draws among them), on a context
    // whose own last view is older than the newest: that own view is this render's, the newest is the next render's (the other eye)
    if(alternate && own_valid && own_buffer) return 5;
    return 4;
}
// v1199: the view a one-view draw was recorded for, by construction. The engine computes a draw's rows from the view object at record time
// and fills its per-view constants (b1) with a Map(DISCARD) recorded into a command list; the next render's Setup and per-view writes can be
// recorded on other job threads before this render's late draws, so no "newest"/"older" timing rule names the render (v1193-v1197 sims: both
// failed, by thread timing). What is certain: a draw on a deferred context reads, at execution, the content its OWN command list last
// Map(DISCARD)ed into the b1 it binds. So a draw's view is certain when its context's last classified per-view write went to the very buffer
// it binds at b1 (whole-buffer bind) in the SAME pass job (= the same command list; `job` 0 = outside any pass job: never certain on a
// deferred context), or on the immediate context, where writes and draws are in execution order. Anything else is uncertain: the
// consumers that change a real draw (placement, previous rows, held mask) then leave it alone, and the rig target needs no view (rig_target.hpp).
inline bool view_certain(bool own_valid,const void* own_buffer,const void* bound,std::uint64_t own_job,std::uint64_t job,bool immediate) noexcept {
    if(!own_valid || !own_buffer || own_buffer!=bound) return false;
    return immediate || (job!=0 && own_job==job);
}
} // namespace d2vr
