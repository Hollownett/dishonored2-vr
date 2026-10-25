#pragma once
// Track T item B: the left hand's aim pose for the mine / spring razor placement ray and the grenade throw-preview sensor.
// Pure: a pose with a freshness window, and a scoped value swap over raw float3 fields (write the hand's values, run the native
// sensor, restore exactly what was there). The native hooks live in native_gadget_aim.hpp.
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace d2vr {
struct GadgetAimPose {
    std::array<float,3> origin{},forward{};
    std::uint64_t tick{};
    bool valid() const noexcept {
        for(unsigned i=0;i<3;++i) if(!std::isfinite(origin[i]) || !std::isfinite(forward[i])) return false;
        const float n=forward[0]*forward[0]+forward[1]*forward[1]+forward[2]*forward[2];
        return n>.98f && n<1.02f;
    }
};
inline constexpr std::uint64_t gadget_aim_fresh_ms=100;
inline bool gadget_aim_fresh(const GadgetAimPose& pose,std::uint64_t now) noexcept {return pose.tick && now>=pose.tick && now-pose.tick<=gadget_aim_fresh_ms && pose.valid();}

// Overwrites up to three float3 fields (null = skipped) and puts them back on destruction. `xy_only` fields (the throw-hit sensor builds a
// horizontal direction from x,y only) take the hand's x,y and keep their own z.
class FieldSwap {
    struct Slot {float* at{};float saved[3]{};bool xy_only{};};
    std::array<Slot,3> slots_{};unsigned count_{};
public:
    FieldSwap() = default;
    FieldSwap(const FieldSwap&) = delete;FieldSwap& operator=(const FieldSwap&) = delete;
    void put(float* at,const std::array<float,3>& value,bool xy_only=false) noexcept {
        if(!at || count_>=slots_.size()) return;
        auto& s=slots_[count_++];s.at=at;s.xy_only=xy_only;std::memcpy(s.saved,at,12);
        at[0]=value[0];at[1]=value[1];if(!xy_only) at[2]=value[2];
    }
    void restore() noexcept {for(unsigned i=0;i<count_;++i) std::memcpy(slots_[i].at,slots_[i].saved,12);count_=0;}
    unsigned size() const noexcept {return count_;}
    ~FieldSwap() {restore();}
};
} // namespace d2vr
