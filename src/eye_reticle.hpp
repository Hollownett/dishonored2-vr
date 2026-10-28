#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include "game_art.hpp"

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
// The game's own HUD crosshair: the glowing dot (ui/hud.iggy texture 926, 8x8) over its dark contrast halo (texture 922, 12x12),
// imported to artifacts/game-art/crosshair_dot.rgba / crosshair_halo.rgba. Both sit centred in a 12-unit frame (the halo's
// size, 1:1) bilinearly enlarged to the 32x32 straight-alpha reticle texture, composited and interpolated premultiplied so
// edges stay clean. The halo is optional: without it the dot alone is used.
inline std::array<std::uint8_t,32*32*4> eye_reticle_game_pixels(const GameArt& dot,const GameArt* halo=nullptr) noexcept {
    std::array<std::uint8_t,32*32*4> pixels{};
    const auto valid=[](const GameArt& a) {return a.width && a.height && a.rgba.size()>=static_cast<std::size_t>(a.width)*a.height*4;};
    if(!valid(dot) || (halo && !valid(*halo)))return pixels;
    // premultiplied RGBA of art whose size spans `extent` frame units centred in the 12-unit frame, at frame position (px,py)
    const auto sample=[](const GameArt& art,float extent,float px,float py,float out[4]) {
        const float u=(px-(12.f-extent)/2)*art.width/extent-.5f,v=(py-(12.f-extent)/2)*art.height/extent-.5f;
        const int u0=static_cast<int>(std::floor(u)),v0=static_cast<int>(std::floor(v));const float fu=u-u0,fv=v-v0;
        for(int c=0;c<4;++c)out[c]=0;
        for(int k=0;k<4;++k) {
            const int x=u0+(k&1),y=v0+(k>>1);const float w=((k&1)?fu:1-fu)*((k>>1)?fv:1-fv);
            if(x<0||y<0||x>=static_cast<int>(art.width)||y>=static_cast<int>(art.height))continue;
            const auto* p=&art.rgba[(static_cast<std::size_t>(y)*art.width+x)*4];
            for(int c=0;c<3;++c)out[c]+=w*p[c]*p[3]/255.f;out[3]+=w*p[3];
        }
    };
    for(unsigned y=0;y<32;++y)for(unsigned x=0;x<32;++x) {
        const float px=(x+.5f)*12.f/32.f,py=(y+.5f)*12.f/32.f;float d[4],h[4]{};
        sample(dot,8.f,px,py,d);if(halo)sample(*halo,12.f,px,py,h);
        const float k=1.f-d[3]/255.f;float s[4];for(int c=0;c<4;++c)s[c]=d[c]+h[c]*k;
        const auto i=4*(y*32+x);
        for(int c=0;c<3;++c)pixels[i+c]=static_cast<std::uint8_t>(s[3]>0.5f?std::clamp(s[c]*255.f/s[3],0.f,255.f)+.5f:0.f);
        pixels[i+3]=static_cast<std::uint8_t>(std::clamp(s[3]+.5f,0.f,255.f));
    }
    return pixels;
}
inline std::array<std::uint8_t,32*32*4> eye_reticle_pixels() noexcept {
    try {if(const auto art=game_art().get("crosshair_dot"))return eye_reticle_game_pixels(*art,game_art().get("crosshair_halo").get());} catch(...) {}
    // No imported game art: procedural ring and dot.
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
