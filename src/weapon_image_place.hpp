#pragma once
// v864: a held weapon is placed by the tick's attach pass from the hand it reads there (before that tick's arm upload, v834), so the render
// that follows draws it where that read put the hand, not where the render draws the hand (the image's own solve, v861). Right before the
// render, the weapon's model frame (+0x1860 origin, +0x186c forward/left/up rows) is moved by the change from the read hand to the image
// hand. A frame the tick did not place again since our last write keeps its tick base (no compounding). Pure: the caller reads and writes.
#include "full_body_driver.hpp"
#include <array>
#include <optional>

namespace d2vr {
struct WeaponPlace {
    std::array<float,12> base{},written{};bool have{};
    std::optional<std::array<float,12>> place(const std::array<float,12>& frame,const fullbody::Xf& read,const fullbody::Xf& image) {
        if(!have || frame!=written) base=frame;   // the tick placed it (again): that is the base
        const auto delta=body::mul(image,body::inverse(read));
        if(length(image.p-read.p)>.5 || std::abs(delta.q.w)<std::cos(.5)) return {};   // not one frame of hand motion: leave the tick's
        std::array<float,12> out{};
        const auto p=delta.p+rotated(delta.q,Vec3{base[0],base[1],base[2]});
        out[0]=static_cast<float>(p.x);out[1]=static_cast<float>(p.y);out[2]=static_cast<float>(p.z);
        for(unsigned r=0;r<3;++r) {
            const auto a=rotated(delta.q,Vec3{base[3+r*3],base[4+r*3],base[5+r*3]});
            out[3+r*3]=static_cast<float>(a.x);out[4+r*3]=static_cast<float>(a.y);out[5+r*3]=static_cast<float>(a.z);
        }
        written=out;have=true;return out;
    }
};
} // namespace d2vr
