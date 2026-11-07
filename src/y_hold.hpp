#pragma once
#include <cmath>
namespace d2vr {
// Recognize a left-Y hold. A short Y keeps its meaning (talk, wheel quick action).
// update() is called once per frame with the raw Y state. `blocked` is true while a Talk prompt is offered to Y or the wheel or a menu
// owns Y: a press that sees `blocked` at any moment belongs to that owner and never fires. `fire` is true on exactly one call per
// press (the first one with y held for hold_s); `suppress_y` is true from that call until Y is released, so the game never also sees
// the press. No allocation, no Windows types.
class YHold {
public:
    struct Config {double hold_s{.6};};
    struct Result {bool fire{};bool suppress_y{};};
private:
    Config config_{};
    bool down_{},cancelled_{},fired_{},have_time_{};
    double start_{},last_{};
    void clear() noexcept {down_=cancelled_=fired_=false;}
public:
    YHold() = default;
    explicit YHold(Config config) noexcept : config_(config) {}
    void reset() noexcept {clear();have_time_=false;}
    Result update(bool y,bool blocked,double now_s) noexcept {return update(y,blocked,now_s,[]() noexcept {return false;});}
    // owned_now() is asked once, on the frame the hold would fire, for the CURRENT frame's Talk offer / wheel / modifier ownership (`blocked`
    // is the previous controls pass). If it answers true the press belongs to that owner: the hold is cancelled for this press, nothing
    // fires and Y is not suppressed, so the game still sees the press.
    template<class Owned> Result update(bool y,bool blocked,double now_s,Owned&& owned_now) {
        if(!std::isfinite(now_s) || now_s<0) {clear();have_time_=false;return {};}
        if(have_time_ && now_s<last_) clear(); // time went backwards: forget the press, then treat this call as a fresh sample
        have_time_=true;last_=now_s;
        if(!y) {clear();return {};}
        if(!down_) {down_=true;start_=now_s;cancelled_=blocked;fired_=false;}
        else if(blocked) cancelled_=true;
        Result out{};
        if(!cancelled_ && !fired_ && now_s-start_>=config_.hold_s) {
            if(owned_now()) cancelled_=true;
            else {fired_=true;out.fire=true;}
        }
        out.suppress_y=fired_;
        return out;
    }
};

// Route a fresh hold to the screen where it began. Screen/focus changes and a settings-panel close require release first,
// so the hold that pauses cannot also open settings, and a held close cannot reopen them. No native menu/game addresses.
class YHoldMenu {
public:
    enum class Screen {unavailable,gameplay,pause};
    struct Result {bool pause{},settings{},suppress_y{},blocked_press{};double held_s{};};
private:
    YHold hold_;
    Screen screen_{Screen::unavailable};
    bool wait_release_{true},raw_was_{};
    double press_start_{};
public:
    Result update(bool y,bool blocked,Screen screen,bool settings_open,bool legacy,double now_s) noexcept {
        return update(y,blocked,screen,settings_open,legacy,now_s,[]() noexcept {return false;});
    }
    // owned_now: see YHold::update; asked only on the fire frame of a gameplay hold.
    template<class Owned> Result update(bool y,bool blocked,Screen screen,bool settings_open,bool legacy,double now_s,Owned&& owned_now) {
        if(screen!=screen_) {hold_.reset();wait_release_=y;screen_=screen;}
        const bool eligible=screen!=Screen::unavailable && !settings_open && !(legacy && screen==Screen::pause);
        if(!eligible) {hold_.reset();wait_release_=y;raw_was_=y;return {};}
        if(!y) wait_release_=false;
        blocked=wait_release_ || (screen==Screen::gameplay && blocked);
        Result out{};
        if(y && !raw_was_) {press_start_=now_s;out.blocked_press=blocked;}
        raw_was_=y;
        const auto held=hold_.update(y,blocked,now_s,[&]{return screen==Screen::gameplay && owned_now();});
        if(held.fire) {
            out.pause=screen==Screen::gameplay && !legacy;
            // v1070 (user: "holding Y should open only the game's menu, not the old VR settings"): a hold in Pause opens nothing; VR settings
            // are the game's own Options page. Only the legacy gameplay hold (build/legacy-hold-y-settings) still opens the old panel.
            out.settings=legacy && screen==Screen::gameplay;
            out.held_s=now_s-press_start_;
        }
        out.suppress_y=held.suppress_y || (y && (wait_release_ || screen==Screen::pause));
        return out;
    }
};
}
