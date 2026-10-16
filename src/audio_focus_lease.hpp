#pragma once
#include <cstdint>

namespace d2vr {
struct AudioFocusOwner {
    std::uintptr_t main{};
    std::uint64_t generation{};
    bool operator==(const AudioFocusOwner& other) const noexcept {return main==other.main && generation==other.generation;}
    bool operator!=(const AudioFocusOwner& other) const noexcept {return !(*this==other);}
};
struct AudioFocusSnapshot {
    AudioFocusOwner owner{};
    bool initialized{},suspend_enabled{},window_active{},focus_suspended{};
};
struct AudioFocusTracking {
    std::uint64_t epoch{},tick{};
    bool focused{},tracked{},simulated{};
    bool fresh(std::uint64_t now) const noexcept {
        return epoch && tick && focused && tracked && !simulated && now>=tick && now-tick<=250;
    }
};
// Owned-value policy only. The bridge must serialize these calls and provide a
// REAL native lifetime/settings fence; snapshots are not that fence.
class AudioFocusLease {
public:
    enum class Phase {unknown,clear,native_held,bypassed};
    enum class Action {none,forward,skip,suspend,resume};
    struct Plan {std::uint64_t serial{};Action action{Action::none};Phase next{Phase::unknown};std::uint64_t epoch{};};
private:
    AudioFocusOwner owner_{};
    Phase phase_{Phase::unknown};
    std::uint64_t serial_{},pending_{},bypass_epoch_{};
    bool stopping_{},faulted_{};
    bool adopt(AudioFocusOwner owner) noexcept {
        if(!owner.main || !owner.generation || pending_ || faulted_)return false;
        if(owner!=owner_) {
            // An outstanding bypass may only be discarded by authoritative
            // retirement, never by observing a different global main pointer.
            if(phase_==Phase::bypassed)return false;
            owner_=owner;phase_=Phase::unknown;bypass_epoch_=0;
        }
        return true;
    }
    Plan plan(Action action,Phase next,std::uint64_t epoch=0) noexcept {
        if(serial_==~std::uint64_t{}) {faulted_=true;return {};}
        pending_=++serial_;return {pending_,action,next,epoch};
    }
public:
    Plan focus(const AudioFocusSnapshot& s,bool suspend,const AudioFocusTracking& tracking,
               std::uint64_t now,bool writer) noexcept {
        if(pending_) {faulted_=true;return {0,Action::forward};}
        if(!adopt(s.owner) || !s.initialized)return {0,Action::forward};
        if(!suspend) {
            if(phase_==Phase::bypassed)return plan(Action::skip,Phase::clear);
            return plan(Action::forward,Phase::clear);
        }
        if(phase_==Phase::native_held || phase_==Phase::bypassed)
            return {0,Action::forward}; // Extra native requests remain native.
        if(s.focus_suspended || !s.suspend_enabled)return {0,Action::forward};
        if(writer && !stopping_ && tracking.fresh(now))
            return plan(Action::skip,Phase::bypassed,tracking.epoch);
        return plan(Action::forward,Phase::native_held);
    }
    Plan service(const AudioFocusSnapshot& s,const AudioFocusTracking& tracking,
                 std::uint64_t now,bool writer) noexcept {
        if(!adopt(s.owner) || !s.initialized)return {};
        if(phase_==Phase::unknown && s.window_active && !s.focus_suspended)phase_=Phase::clear;
        if(!s.focus_suspended)return {}; // A native resume can be in flight.
        const bool focused=writer && !stopping_ && tracking.fresh(now);
        if(phase_==Phase::native_held && focused && s.suspend_enabled)
            return plan(Action::resume,Phase::bypassed,tracking.epoch);
        if(phase_==Phase::bypassed && (!focused || bypass_epoch_!=tracking.epoch) && s.suspend_enabled)
            return plan(Action::suspend,Phase::native_held);
        // Unknown inactive startup and changed native suspend settings never
        // authorize subtracting or manufacturing a counter contribution.
        return {};
    }
    void finish(const Plan& p,bool success) noexcept {
        if(!p.serial)return;
        if(pending_!=p.serial) {faulted_=true;return;}
        pending_=0;
        if(!success) {faulted_=true;return;} // Native call may have partly run.
        phase_=p.next;bypass_epoch_=p.next==Phase::bypassed?p.epoch:0;
    }
    bool retired(AudioFocusOwner owner) noexcept {
        if(owner!=owner_ || pending_)return false;
        owner_={};phase_=Phase::unknown;bypass_epoch_=0;faulted_=false;return true;
    }
    void stop() noexcept {stopping_=true;}
    bool restart() noexcept {
        if(!stop_ready())return false;
        stopping_=false;owner_={};phase_=Phase::unknown;bypass_epoch_=0;return true;
    }
    bool stop_ready() const noexcept {return !pending_ && !faulted_ && phase_!=Phase::bypassed;}
    bool busy() const noexcept {return pending_!=0;}
    bool faulted() const noexcept {return faulted_;}
    Phase phase() const noexcept {return phase_;}
    AudioFocusOwner owner() const noexcept {return owner_;}
};
} // namespace d2vr
