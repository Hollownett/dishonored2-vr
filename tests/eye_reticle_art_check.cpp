// Lab: builds the reticle texture from the imported game art (artifacts/game-art/crosshair_dot.rgba) and the procedural
// fallback, then composites both at the real eye size (12 px) over dark/bright backdrops into P6 PPMs (arg: out dir).
#include "../src/eye_reticle.hpp"
#include <cassert>
#include <fstream>
#include <iostream>
#include <string>
using namespace d2vr;
static void sheet(const std::string& path,const std::array<std::uint8_t,32*32*4>& px,int scale) {
    const int cell=40*scale,W=cell*4,H=cell*2;std::vector<unsigned char> img(W*H*3);
    const unsigned bg[4][3]={{12,14,18},{70,80,60},{190,190,185},{250,250,250}};
    for(int y=0;y<H;++y)for(int x=0;x<W;++x) {
        const int c=x/cell;const auto* b=bg[c];const int real=y<cell?0:1; // top: 12 px as shipped (scale 1 only); bottom: 3x zoom
        float s=real?3.f:1.f;float u=((x%cell)-cell/2)/ (s*(real?1.f:1.f))*1.f;float v=((y%cell)-cell/2)/s;
        // map: texture spans 12x12 px centred; sample bilinear-nearest
        const float tx=u/12.f*32.f+16.f,ty=v/12.f*32.f+16.f;unsigned o[3]={b[0],b[1],b[2]};
        if(tx>=0&&ty>=0&&tx<32&&ty<32) {const auto i=4*(static_cast<int>(ty)*32+static_cast<int>(tx));const float a=px[i+3]/255.f;
            for(int k=0;k<3;++k)o[k]=static_cast<unsigned>(px[i+k]*a+b[k]*(1-a));}
        for(int k=0;k<3;++k)img[(y*W+x)*3+k]=static_cast<unsigned char>(o[k]);
    }
    std::ofstream f(path,std::ios::binary);f<<"P6\n"<<W<<" "<<H<<"\n255\n";f.write(reinterpret_cast<char*>(img.data()),img.size());
}
int main(int argc,char** argv) {
    const std::string out=argc>1?argv[1]:".";
    GameArtCache cache("artifacts/game-art");const auto art=cache.get("crosshair_dot");
    assert(art && art->width==8 && art->height==8);
    const auto halo=cache.get("crosshair_halo");assert(halo && halo->width==12);
    const auto game=eye_reticle_game_pixels(*art,halo.get());
    assert(game[3]==0 && game[4*31+3]==0);                // transparent corners
    assert(game[4*(15*32+15)+3]>=240 && game[4*(16*32+16)+3]>=240); // opaque core
    assert(game[4*(15*32+15)]>200 && game[4*(15*32+15)+1]>220); // pale core colour
    assert(game[4*(2*32+16)+3]==0 || game[4*(2*32+16)+0]<80); // halo (if any) is dark
    sheet(out+"/reticle_dot_only.ppm",eye_reticle_game_pixels(*art),1);
    sheet(out+"/reticle_game.ppm",game,1);
    // procedural fallback when art is absent
    GameArtCache none("artifacts/game-art-missing");assert(!none.get("crosshair_dot"));
    sheet(out+"/reticle_fallback.ppm",eye_reticle_pixels(),1);
    std::cout<<"PASS: game crosshair texture\n";
}
