#pragma once
// v1254 (user: "when I press grip [the] time travel device will be unfolded and only when I press grip again it will be folded back... from
// hold grip to press only, and hold grip should not block trigger event"). The Timepiece (Gadget_timepiece, left equip pose "Timeshift") is
// raised and folded by the game's ACTION_INTERACT, the native Use (gamepad X): cpntPlayerController item[37] Use -> PLAYER_STATE_UNFOLD_
// TIMESHIFTDEVICE (69, priority 1, i.e. only when no other Use target wins), which runs into 70 _LOOP; a second Use from the loop re-enters
// 69 with m_quit (playermode item[155]) = fold. The time shift is ACTION_POWER_WEAPON (left trigger) -> 68 USE_TIMESHIFTDEVICE. So the slot-1
// grip is a TOGGLE, not a hold: each fresh squeeze flips the device once and letting go changes nothing, leaving the hand free for the trigger.
// The game's state decides the direction: a short Use pulse whenever the press and the device disagree, re-tried after a settle time, at most
// `attempts` times per press (a Use the game gives to a door or pickup instead must not repeat).
#include <atomic>
#include <cstdint>
#include <string_view>

namespace d2vr {
inline bool is_timepiece_equip_name(std::string_view n) noexcept {return n.find("imeshift")!=std::string_view::npos || n.find("imepiece")!=std::string_view::npos;}

class TimepieceGrip {
public:
    inline static constexpr float press=.55f,release=.35f;
    inline static constexpr std::uint64_t pulse_ms=120,settle_ms=700;
    inline static constexpr unsigned attempts=2;
    enum Cause : unsigned {unfold_grip,fold_release,cause_count};
    inline static std::atomic<std::uint64_t> requests[cause_count]{},gave_up{},busy_frames{};
    // state: the player's active Timepiece state (68 shifting, 69 unfolding, 70 up) or 0. busy: the hand's grip is someone else's
    // (a Use target under that hand, a held object or a body), or no gameplay control (menu, wheel, scene). Returns: press Use this frame.
    bool update(bool equipped,float grip,int state,bool busy,std::uint64_t now) noexcept {
        const bool was_down=down_;
        down_=grip>=(down_?release:press);   // a press is a fresh crossing of `press`; the next one waits for `release`
        if(!equipped || busy) {
            if(busy && equipped) ++busy_frames;
            pulse_until_=0;pending_=false;tries_=0;wait_until_=0;   // not the device's press: the next fresh squeeze reads the state then
            return false;
        }
        if(now<pulse_until_) return true;
        if(down_ && !was_down) {want_=!(state==68 || state==69 || state==70);pending_=true;tries_=0;wait_until_=0;} // one flip per squeeze, from the game's state (a shift is up)
        if(!pending_) return false;
        if(state==68) {wait_until_=now+settle_ms;return false;}   // a shift runs: never interrupt it
        const bool up=state==69 || state==70;
        if(up==want_) {pending_=false;return false;}
        if(now<wait_until_) return false;
        if(tries_>=attempts) {if(tries_==attempts) {++gave_up;++tries_;}return false;}
        ++tries_;++requests[want_?unfold_grip:fold_release];
        pulse_until_=now+pulse_ms;wait_until_=now+pulse_ms+settle_ms;
        return true;
    }
private:
    bool down_{},want_{},pending_{};unsigned tries_{};std::uint64_t pulse_until_{},wait_until_{};
};
} // namespace d2vr
