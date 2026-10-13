#pragma once
// Real Dishonored 2 UI art for the mod's panels, decoded from the user's own install by tools/extract_game_art.py into the
// git-ignored cache artifacts/game-art/<key>.rgba (u32 width, u32 height, straight RGBA8, top row first). Nothing is
// shipped with the mod; a missing key leaves the caller's fallback in place.
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <algorithm>
#include <atomic>
#include <cmath>

namespace d2vr {
struct GameArt {std::uint32_t width{},height{};std::vector<unsigned char> rgba;};
class GameArtCache {
public:
    explicit GameArtCache(std::filesystem::path folder={}) : folder_(std::move(folder)) {}
    void folder(std::filesystem::path f) {std::lock_guard<std::mutex> l(m_);folder_=std::move(f);cache_.clear();}
    // Loaded once per key (also remembers a miss).
    std::shared_ptr<const GameArt> get(const std::string& key) {
        std::lock_guard<std::mutex> l(m_);
        if(const auto it=cache_.find(key);it!=cache_.end()) return it->second;
        auto& slot=cache_[key];
        std::ifstream in(folder_/(key+".rgba"),std::ios::binary);
        std::uint32_t wh[2]{};
        if(!in || !in.read(reinterpret_cast<char*>(wh),8) || !wh[0] || !wh[1] || wh[0]>4096 || wh[1]>4096) return slot;
        auto art=std::make_shared<GameArt>();art->width=wh[0];art->height=wh[1];
        art->rgba.resize(static_cast<std::size_t>(wh[0])*wh[1]*4);
        if(!in.read(reinterpret_cast<char*>(art->rgba.data()),static_cast<std::streamsize>(art->rgba.size()))) return slot;
        slot=std::move(art);return slot;
    }
private:
    std::mutex m_;std::filesystem::path folder_;std::map<std::string,std::shared_ptr<const GameArt>> cache_;
};
// Blend art (bilinear, straight alpha, times opacity) into a 32-bit BGRX surface over the rectangle [x0,x1)x[y0,y1).
// fit: keep the aspect ratio inside the rectangle (centred); otherwise stretch. tint (0xRRGGBB) multiplies the art's colour.
inline void blit_art(std::uint32_t* dst,int dst_w,int dst_h,const GameArt& art,double x0,double y0,double x1,double y1,
                     double opacity=1,bool fit=true,std::uint32_t tint=0xffffff) {
    if(!dst || art.rgba.empty() || x1<=x0 || y1<=y0) return;
    if(fit) {
        const double s=std::min((x1-x0)/art.width,(y1-y0)/art.height),w=art.width*s,h=art.height*s;
        const double cx=(x0+x1)/2,cy=(y0+y1)/2;x0=cx-w/2;x1=cx+w/2;y0=cy-h/2;y1=cy+h/2;
    }
    const int ix0=std::max(0,static_cast<int>(std::floor(x0))),ix1=std::min(dst_w,static_cast<int>(std::ceil(x1)));
    const int iy0=std::max(0,static_cast<int>(std::floor(y0))),iy1=std::min(dst_h,static_cast<int>(std::ceil(y1)));
    const auto texel=[&](int x,int y,int c) {
        x=std::clamp(x,0,static_cast<int>(art.width)-1);y=std::clamp(y,0,static_cast<int>(art.height)-1);
        return art.rgba[(static_cast<std::size_t>(y)*art.width+x)*4+c];};
    for(int y=iy0;y<iy1;++y) for(int x=ix0;x<ix1;++x) {
        const double u=(x+.5-x0)/(x1-x0)*art.width-.5,v=(y+.5-y0)/(y1-y0)*art.height-.5;
        const int u0=static_cast<int>(std::floor(u)),v0=static_cast<int>(std::floor(v));const double fu=u-u0,fv=v-v0;
        double s[4]{};
        for(int c=0;c<4;++c) s[c]=(texel(u0,v0,c)*(1-fu)+texel(u0+1,v0,c)*fu)*(1-fv)+(texel(u0,v0+1,c)*(1-fu)+texel(u0+1,v0+1,c)*fu)*fv;
        const double a=s[3]/255*opacity;if(a<=0) continue;
        s[0]*=(tint>>16&255)/255.;s[1]*=(tint>>8&255)/255.;s[2]*=(tint&255)/255.;
        auto& p=dst[static_cast<std::size_t>(y)*dst_w+x];
        const double r=(p>>16&255)*(1-a)+s[0]*a,g=(p>>8&255)*(1-a)+s[1]*a,b=(p&255)*(1-a)+s[2]*a;
        p=static_cast<std::uint32_t>(std::lround(r))<<16|static_cast<std::uint32_t>(std::lround(g))<<8|static_cast<std::uint32_t>(std::lround(b));
    }
}
// Blend the source rectangle [sx0,sx1)x[sy0,sy1) of art (texels) into [x0,x1)x[y0,y1) (stretched): 3-slice caps etc.
inline void blit_art_part(std::uint32_t* dst,int dst_w,int dst_h,const GameArt& art,int sx0,int sy0,int sx1,int sy1,
                          double x0,double y0,double x1,double y1,double opacity=1) {
    sx0=std::clamp(sx0,0,static_cast<int>(art.width));sx1=std::clamp(sx1,sx0,static_cast<int>(art.width));
    sy0=std::clamp(sy0,0,static_cast<int>(art.height));sy1=std::clamp(sy1,sy0,static_cast<int>(art.height));
    if(sx1<=sx0 || sy1<=sy0) return;
    GameArt part;part.width=static_cast<std::uint32_t>(sx1-sx0);part.height=static_cast<std::uint32_t>(sy1-sy0);
    part.rgba.resize(static_cast<std::size_t>(part.width)*part.height*4);
    for(int y=sy0;y<sy1;++y) std::copy_n(&art.rgba[(static_cast<std::size_t>(y)*art.width+sx0)*4],part.width*4,&part.rgba[static_cast<std::size_t>(y-sy0)*part.width*4]);
    blit_art(dst,dst_w,dst_h,part,x0,y0,x1,y1,opacity,false);
}
// Horizontal 3-slice: the art's left/right caps (cap texels wide) keep their aspect at the target height, the middle stretches.
inline void blit_art_band(std::uint32_t* dst,int dst_w,int dst_h,const GameArt& art,int cap,double x0,double y0,double x1,double y1,double opacity=1) {
    const int w=static_cast<int>(art.width),h=static_cast<int>(art.height);cap=std::clamp(cap,0,w/2);
    const double cw=cap*(y1-y0)/std::max(1,h);
    blit_art_part(dst,dst_w,dst_h,art,0,0,cap,h,x0,y0,x0+cw,y1,opacity);
    blit_art_part(dst,dst_w,dst_h,art,cap,0,w-cap,h,x0+cw,y0,x1-cw,y1,opacity);
    blit_art_part(dst,dst_w,dst_h,art,w-cap,0,w,h,x1-cw,y0,x1,y1,opacity);
}
// Process-wide cache (folder set at mod start) and the playable character the game loaded (pause menu movie): 1 Corvo, 2 Emily.
inline GameArtCache& game_art() {static GameArtCache cache;return cache;}
inline std::atomic<int> game_character{0};
} // namespace d2vr
