#pragma once
#include <array>
#include <cmath>
#include <cstdint>

namespace d2vr {
// DPAD order matches installed QUICK_0..3: right, up, left, down.
struct MenuChordOutput {
    bool consumed{},menu_modifier{},back{};
    std::array<bool,4> dpad{};
};
class MenuChord {
    bool valid_{},gameplay_{},waiting_release_{},pressed_{},handled_{},dpad_used_{},journal_fired_{},centred_{};
    std::uint64_t last_tick_{},press_tick_{},back_until_{},dpad_until_{};
    unsigned direction_{};
    MenuChordOutput output(bool menu,std::uint64_t now) const {
        MenuChordOutput out;out.menu_modifier=menu;
        out.consumed=menu && (waiting_release_ || handled_);
        out.back=now<back_until_;
        if(now<dpad_until_) out.dpad[direction_]=true;
        return out;
    }
public:
    MenuChordOutput update(bool menu,bool active,bool gameplay,float right_x,float right_y,std::uint64_t now,bool right_stick_available=true) noexcept {
        if(!active) {*this={};return {};}
        const bool stick_valid=!right_stick_available || (std::isfinite(right_x) && std::isfinite(right_y) && std::abs(right_x)<=1 && std::abs(right_y)<=1);
        if(!valid_ || gameplay_!=gameplay || now<last_tick_ || now-last_tick_>250 || !stick_valid) {
            const bool cancel_pause=valid_;
            *this={};valid_=true;gameplay_=gameplay;last_tick_=now;waiting_release_=menu || cancel_pause;
            auto out=output(menu,now);out.consumed|=cancel_pause;
            return out;
        }
        last_tick_=now;
        if(waiting_release_) {
            if(!menu) waiting_release_=false;
            return output(menu,now);
        }
        if(menu && !pressed_) {
            pressed_=true;press_tick_=now;handled_=dpad_used_=journal_fired_=centred_=false;
            back_until_=dpad_until_=0;
        } else if(!menu && pressed_) {
            // Cancel further gestures. Already committed native pulses have a
            // bounded 150-ms tail; focus/context/stale changes cancel it.
            pressed_=handled_=dpad_used_=journal_fired_=centred_=false;
        }
        if(menu && gameplay && !journal_fired_) {
            const auto radius=std::hypot(right_x,right_y);
            if(!right_stick_available) {centred_=false;dpad_until_=0;}
            else if(radius<=.3f) {if(!centred_) dpad_until_=0;centred_=true;}
            else if(centred_ && radius>=.65f) {
                direction_=std::abs(right_x)>=std::abs(right_y)?(right_x>=0?0u:2u):(right_y>=0?1u:3u);
                centred_=false;handled_=dpad_used_=true;dpad_until_=now+150;back_until_=0;
            }
            if(!dpad_used_ && now-press_tick_>=650) {
                handled_=journal_fired_=true;back_until_=now+150;
            }
        }
        return output(menu,now);
    }
};
} // namespace d2vr
