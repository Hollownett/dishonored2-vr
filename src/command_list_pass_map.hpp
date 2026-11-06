#pragma once
// Bounded, lock-light command-list -> render-pass map for the v999 pass census.
//
// The engine's deferred job executor (0x2c5d50) runs a pass callback, then finishes that job's
// deferred context into an ID3D11CommandList.  The list is executed later on the immediate context.
// The callback and the FinishCommandList both happen inside the executor, so a thread-local current
// pass set around the executor is available to the existing FinishCommandList observer; that observer
// records (command list pointer, pass id) here and the ExecuteCommandList path looks it up.
//
// Recording job threads write, the render thread reads.  No mutex: open addressing with atomic keys,
// a release key store after the pass store, and an acquire key load before the pass load.  The table
// is bounded (8192 slots, 16 probes); a full probe window evicts the home slot and is counted, so a
// pathological run loses samples instead of blocking a job thread or growing without bound.  Entries
// are not cleared on read: D3D11 allows a command list to be executed more than once, and the engine's
// reuse experiment needs the same list to keep its pass.  A later list allocated at the same address is
// re-recorded by FinishCommandList before it is executed, so a stale key cannot outlive its list.
#include <array>
#include <atomic>
#include <cstdint>

namespace d2vr {
class CommandListPassMap {
public:
    static constexpr unsigned capacity=8192;
    static constexpr unsigned probe_limit=16;
    struct Stats {
        std::uint64_t recorded{},updated{},evicted{},rejected{};
    };
private:
    struct Slot {
        std::atomic<std::uintptr_t> key{0};
        std::atomic<int> pass{-1};
    };
    static unsigned home(std::uintptr_t key) noexcept {
        key>>=4;
        key^=key>>33;
        key*=0x9e3779b97f4a7c15ull;
        key^=key>>29;
        return static_cast<unsigned>(key)&(capacity-1);
    }
    std::array<Slot,capacity> slots_{};
    std::atomic<std::uint64_t> recorded_{},updated_{},evicted_{},rejected_{};
public:
    // Returns false only for a null list or a negative pass; a full table still records (evicting).
    bool record(const void* list,int pass) noexcept {
        if(!list || pass<0) {++rejected_;return false;}
        const auto key=reinterpret_cast<std::uintptr_t>(list);
        const unsigned start=home(key);
        for(unsigned i=0;i<probe_limit;++i) {
            auto& slot=slots_[(start+i)&(capacity-1)];
            const auto current=slot.key.load(std::memory_order_relaxed);
            if(current==key) {
                slot.pass.store(pass,std::memory_order_release);
                ++updated_;
                return true;
            }
            if(current==0) {
                slot.pass.store(pass,std::memory_order_relaxed);
                std::uintptr_t expected=0;
                if(slot.key.compare_exchange_strong(expected,key,std::memory_order_release,std::memory_order_relaxed)) {
                    ++recorded_;
                    return true;
                }
                if(expected==key) {
                    slot.pass.store(pass,std::memory_order_release);
                    ++updated_;
                    return true;
                }
            }
        }
        auto& slot=slots_[start];
        slot.pass.store(pass,std::memory_order_relaxed);
        slot.key.store(key,std::memory_order_release);
        ++evicted_;
        return true;
    }
    int find(const void* list) const noexcept {
        if(!list) return -1;
        const auto key=reinterpret_cast<std::uintptr_t>(list);
        const unsigned start=home(key);
        for(unsigned i=0;i<probe_limit;++i) {
            const auto& slot=slots_[(start+i)&(capacity-1)];
            const auto current=slot.key.load(std::memory_order_acquire);
            if(current==key) return slot.pass.load(std::memory_order_acquire);
            if(current==0) return -1;
        }
        return -1;
    }
    void clear() noexcept {
        for(auto& slot:slots_) {
            slot.key.store(0,std::memory_order_relaxed);
            slot.pass.store(-1,std::memory_order_relaxed);
        }
        recorded_=0;updated_=0;evicted_=0;rejected_=0;
    }
    Stats stats() const noexcept {
        return {recorded_.load(std::memory_order_relaxed),updated_.load(std::memory_order_relaxed),
            evicted_.load(std::memory_order_relaxed),rejected_.load(std::memory_order_relaxed)};
    }
};
} // namespace d2vr
