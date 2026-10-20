#pragma once
#include <cmath>
namespace d2vr {
// Hold the left Y button to open the VR settings in gameplay. A short Y keeps its meaning (talk, wheel quick action).
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
    Result update(bool y,bool blocked,double now_s) noexcept {
        if(!std::isfinite(now_s) || now_s<0) {clear();have_time_=false;return {};}
        if(have_time_ && now_s<last_) clear(); // time went backwards: forget the press, then treat this call as a fresh sample
        have_time_=true;last_=now_s;
        if(!y) {clear();return {};}
        if(!down_) {down_=true;start_=now_s;cancelled_=blocked;fired_=false;}
        else if(blocked) cancelled_=true;
        Result out{};
        if(!cancelled_ && !fired_ && now_s-start_>=config_.hold_s) {fired_=true;out.fire=true;}
        out.suppress_y=fired_;
        return out;
    }
};
}
