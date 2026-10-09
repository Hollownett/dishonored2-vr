#pragma once
#include <cstdint>

namespace d2vr {
// Altered adaptation of Dishonored-VR controller_emulation.h's menu tap:
// Copyright (c) 2026 GingasVR, zlib/libpng (third_party/dishonored_vr/LICENSE).
// Upstream commit b15e4d32a720bd4cfdb5ce4d600f3f8fba8d2cec holds Start for
// 150 ms after a released tap. This adapter keeps that pause pulse, without
// its modifier/chord/journal bindings, and cancels tails on lost focus.
class MenuTap {
    bool valid_{},held_{},ignore_tail_{};
    std::uint64_t until_{},last_tick_{};
public:
    bool update(bool menu,bool active,std::uint64_t now) noexcept {
        if(!active || (valid_ && now<last_tick_)) {*this={};return false;}
        last_tick_=now;
        if(!valid_) {valid_=true;held_=menu;ignore_tail_=menu;return false;}
        if(ignore_tail_) {held_=menu;ignore_tail_=menu;return false;}
        if(menu && !held_) until_=0;
        else if(!menu && held_) until_=now+150;
        held_=menu;
        return now<until_;
    }
};
} // namespace d2vr
