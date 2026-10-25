#pragma once
// Track T C2: hold A or X during a cutscene or a pre-rendered video = the game's own skip. The skip prompt is INTERACT held (a ring
// of ~3 s), so once A/X has been down for hold_ms the game is sent a held X (Interact) for as long as the button stays down, and the
// physical A / X are masked. A button pressed inside the scene that is still down when the cutscene or video ends (even before hold_ms) stays masked until it is released, so the
// press never leaks into the next screen (a menu confirm, a jump). Before hold_ms and outside a cutscene nothing changes.
// Pure state; the probe calls update() once per controller frame.
#include <cstdint>

namespace d2vr {
struct SkipHoldOut {bool inject_x{},mask_a{},mask_x{};};
class SkipHold {
    std::uint64_t down_since_{};bool down_{},latched_{},injecting_{},armed_{};
    std::uint64_t holds_{},leaks_stopped_{};
public:
    static constexpr std::uint64_t hold_ms=600;
    // context: an exact cinematic (not paused, not a mixed menu mask) or a Bink video is up. a / x: the physical buttons.
    SkipHoldOut update(bool context,bool a,bool x,std::uint64_t now) noexcept {
        SkipHoldOut out{};
        const bool down=a || x;
        if(!down) {down_=false;latched_=false;injecting_=false;armed_=false;return out;}
        if(!down_) {down_=true;down_since_=now;latched_=false;injecting_=false;armed_=context;}   // only a press that began inside the scene can skip it
        if(latched_) {out.mask_a=out.mask_x=true;return out;}                    // leaked-press guard: until released
        if(!context) {
            if(injecting_ || armed_) {injecting_=false;armed_=false;latched_=true;++leaks_stopped_;out.mask_a=out.mask_x=true;}   // the scene ended under a press that began inside it (skip hold or not yet 0.6 s)
            return out;
        }
        if(armed_ && now>=down_since_ && now-down_since_>=hold_ms) {
            if(!injecting_) {injecting_=true;++holds_;}
            out.inject_x=true;out.mask_a=out.mask_x=true;
        }
        return out;
    }
    bool injecting() const noexcept {return injecting_;}
    std::uint64_t holds() const noexcept {return holds_;}
    std::uint64_t leaks_stopped() const noexcept {return leaks_stopped_;}
};
} // namespace d2vr
