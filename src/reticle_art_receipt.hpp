#pragma once
#include <cstdint>

namespace d2vr {
// The art copy belongs to one saved eye image, not the latest rendered eye.
struct ReticleArtReceipt {
    std::uint64_t frame{},epoch{},reference{},tick{};
    bool matches(std::uint64_t image_frame,std::uint64_t current_epoch,
                 std::uint64_t current_reference,std::uint64_t now) const noexcept {
        return frame && epoch && reference && tick && frame==image_frame && epoch==current_epoch &&
            reference==current_reference && now>=tick && now-tick<=250;
    }
};
}
