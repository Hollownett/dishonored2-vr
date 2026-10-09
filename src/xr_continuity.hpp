#pragma once
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace d2vr {
// Poses and anchored UI belong to one visible reference-space epoch. A frame
// rendered before a resume/recenter must not be submitted in the new epoch.
class XrContinuity {
    bool usable_{};
    std::uint64_t epoch_{};
    std::uint64_t reference_epoch_{1};
    std::vector<std::int64_t> changes_;
public:
    std::uint64_t epoch() const noexcept {return epoch_;}
    std::uint64_t reference_epoch() const noexcept {return reference_epoch_;}
    bool usable() const noexcept {return usable_;}
    void recenter() noexcept {++epoch_;++reference_epoch_;}
    void reference_change(std::int64_t time) {
        if(changes_.size()>=16) throw std::runtime_error("Too many pending XR reference changes");
        changes_.push_back(time);std::sort(changes_.begin(),changes_.end());
    }
    void suspend() noexcept {
        if(usable_) {usable_=false;++epoch_;}
    }
    bool advance(bool visible,bool should_render,bool views_valid,std::int64_t display_time) {
        auto due=std::upper_bound(changes_.begin(),changes_.end(),display_time);
        const bool changed=due!=changes_.begin();
        changes_.erase(changes_.begin(),due);
        if(changed) ++reference_epoch_;
        const bool usable=visible && should_render && views_valid;
        const bool reset=changed || usable!=usable_;
        usable_=usable;
        if(reset) ++epoch_;
        return reset;
    }
};
} // namespace d2vr
