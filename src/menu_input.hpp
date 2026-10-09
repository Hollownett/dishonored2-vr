#pragma once
#include <cmath>
#include <cstdint>
#include <optional>

namespace d2vr {
struct NativeMenuInput {
    double u{},v{};
    std::uint64_t tick{},epoch{};
    bool active{},trigger{};
};
// A menu click must not become a weapon trigger when Resume takes effect.
class MenuTriggerGate {
    bool held_{};
public:
    float update(float trigger,bool menu,bool active) {
        if(!active || !std::isfinite(trigger) || trigger<0 || trigger>1) return 0;
        if(menu && trigger>.55f) held_=true;
        if(trigger<.25f) held_=false;
        return menu || held_?0:trigger;
    }
};
// A newly opened menu cannot interpret a held combat trigger as a click.
class MenuClick {
    bool valid_{},armed_{},down_{};
    std::uint64_t epoch_{};
public:
    enum class Event {none,down,up};
    bool down() const noexcept {return down_;}
    Event update(const NativeMenuInput& in,std::uint64_t now) {
        const bool usable=in.active && in.epoch && now>=in.tick && now-in.tick<=250 &&
            std::isfinite(in.u) && std::isfinite(in.v) && in.u>=0 && in.u<=1 && in.v>=0 && in.v<=1;
        if(!usable || (valid_ && in.epoch!=epoch_)) {
            const auto result=down_?Event::up:Event::none;valid_=false;armed_=false;down_=false;return result;
        }
        if(!valid_) {valid_=true;epoch_=in.epoch;armed_=!in.trigger;return Event::none;}
        if(!in.trigger) {
            armed_=true;if(down_) {down_=false;return Event::up;}
        } else if(armed_ && !down_) {armed_=false;down_=true;return Event::down;}
        return Event::none;
    }
};
} // namespace d2vr
