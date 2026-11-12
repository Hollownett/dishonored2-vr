#pragma once
#include <array>
#include <cstdint>

namespace d2vr {
// Value state owned by the GPU capture mutex. Qualify setup addresses before
// NativeEyePair sees them; auxiliary/recycled/stale views must not trigger copy.
class NativeEyeSources {
    struct Source {std::uintptr_t address{};std::uint64_t frame{},epoch{};std::uint32_t thread{};};
    std::array<Source,2> sources_{};
    std::uint64_t epoch_=1;
public:
    void invalidate() noexcept {if(++epoch_==0) epoch_=1;sources_={};}
    std::uint64_t epoch() const noexcept {return epoch_;}
    std::uintptr_t address(unsigned eye) const noexcept {return eye<sources_.size()?sources_[eye].address:0;} // v1245 view trace
    std::uint64_t frame(unsigned eye) const noexcept {return eye<sources_.size()?sources_[eye].frame:0;}
    bool publish(unsigned eye,std::uintptr_t address,std::uint64_t frame,std::uint32_t thread,std::uint32_t owner_thread) noexcept {
        if(eye>=sources_.size() || !address || !thread || thread!=owner_thread) return false;
        sources_[eye]={address,frame,epoch_,thread};return true;
    }
    // Caller supplies the expected setup frame for its verified native stage.
    int match(std::uintptr_t address,std::uint64_t frame,std::uint32_t thread,std::uint32_t owner_thread) const noexcept {
        if(!address || !thread || thread!=owner_thread) return -1;
        int eye=-1;
        for(unsigned side=0;side<sources_.size();++side) {
            const auto& source=sources_[side];
            if(source.address==address && source.frame==frame && source.epoch==epoch_ && source.thread==thread) {
                if(eye>=0) return -1;
                eye=static_cast<int>(side);
            }
        }
        return eye;
    }
};
} // namespace d2vr
