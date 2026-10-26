#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

namespace d2vr {
// Projection receipts belong to the actual submitted eye image. In alternate-eye
// mode the kept image must use its own receipt, never the latest opposite eye.
struct EyeReticle {
    float x{},y{};std::uint64_t frame{},epoch{},reference{},tick{};bool valid{};
};
class EyeReticleFrames {
    std::array<std::array<EyeReticle,4>,2> slots_{};
public:
    void clear() noexcept {slots_={};}
    void publish(unsigned eye,EyeReticle value) noexcept {
        if(eye>=2 || !value.frame)return;
        value.valid=value.valid && value.epoch && value.reference && value.tick &&
            std::isfinite(value.x) && std::isfinite(value.y) && std::abs(value.x)<1.f && std::abs(value.y)<1.f;
        slots_[eye][value.frame%4]=value;
    }
    std::optional<EyeReticle> find(unsigned eye,std::uint64_t frame,std::uint64_t epoch,
            std::uint64_t reference,std::uint64_t now) const noexcept {
        if(eye>=2 || !frame)return {};
        const auto& value=slots_[eye][frame%4];
        if(!value.valid || value.frame!=frame || value.epoch!=epoch || value.reference!=reference ||
           now<value.tick || now-value.tick>250)return {};
        return value;
    }
};
// NDC bounds of a small circular reticle; use pixels in each axis so the
// glyph stays circular in a non-square eye texture. No flat HUD stage/clip.
inline std::optional<std::array<float,4>> eye_reticle_rect(float x,float y,unsigned width,unsigned height,float radius=6.f) noexcept {
    if(!width || !height || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(radius) ||
       radius<=0 || radius>128 || std::abs(x)>=1 || std::abs(y)>=1)return {};
    const float rx=2.f*radius/static_cast<float>(width),ry=2.f*radius/static_cast<float>(height);
    return std::array<float,4>{x-rx,y+ry,x+rx,y-ry};
}
inline std::array<std::uint8_t,32*32*4> eye_reticle_pixels() noexcept {
    std::array<std::uint8_t,32*32*4> pixels{};
    for(unsigned y=0;y<32;++y)for(unsigned x=0;x<32;++x) {
        const float dx=static_cast<float>(x)-15.5f,dy=static_cast<float>(y)-15.5f;
        const float d=std::sqrt(dx*dx+dy*dy);
        // White ring and a small centre dot; smooth transparent edges.
        const float a=std::max(std::clamp(2.f-std::abs(d-10.5f),0.f,1.f),std::clamp(2.f-d,0.f,1.f));
        const auto i=4*(y*32+x);pixels[i]=pixels[i+1]=pixels[i+2]=255;pixels[i+3]=static_cast<std::uint8_t>(a*255.f);
    }
    return pixels;
}
} // namespace d2vr
