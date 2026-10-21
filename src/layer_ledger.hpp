#pragma once
// Bookkeeping for the held object's collision-layer swap (RIGID_BODY -> DEBRIS while held): every swap must end in exactly one
// restore, or an explicit give-up when the engine no longer has the object (it then owns the layer). A restore that cannot run yet
// (body not readable this frame) is retried for a bounded number of attempts instead of being forgotten.
#include <cstdint>
namespace d2vr {
class LayerLedger {
public:
    enum class Step {none,restore,give_up};
    static constexpr unsigned max_attempts=180;
    void swapped() noexcept {if(pending_) {++abandoned_;}pending_=true;attempts_=0;++swaps_;}   // a swap while one is pending (should not happen) counts the old one as given up
    bool pending() const noexcept {return pending_;}
    // Called when a restore is wanted (release of any kind, retry, or a second object taking the hold). alive: the engine still has the entity.
    Step attempt(bool alive) noexcept {
        if(!pending_) return Step::none;
        if(!alive || attempts_>=max_attempts) {pending_=false;++abandoned_;return Step::give_up;}
        ++attempts_;return Step::restore;
    }
    // The restore ran: ok = the engine took the original filter. A failed restore stays pending (retry).
    void result(bool ok) noexcept {if(ok && pending_) {pending_=false;++restores_;}}
    std::uint64_t swaps() const noexcept {return swaps_;}
    std::uint64_t restores() const noexcept {return restores_;}
    std::uint64_t abandoned() const noexcept {return abandoned_;}
    std::uint64_t releases() const noexcept {return restores_+abandoned_;}
    double restores_per_release() const noexcept {return releases()?double(restores_)/double(releases()):0.;}
private:
    bool pending_{};unsigned attempts_{};std::uint64_t swaps_{},restores_{},abandoned_{};
};
} // namespace d2vr
