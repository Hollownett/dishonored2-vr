// Renders the in-headset Controls & gestures guide (right grip at the right temple) to PNGs without the game:
// src/controls_guide_image.hpp with docs/VR-CONTROLS.md and the user's extracted game art (tools/extract_game_art.py).
// Build + run: tools\build-vr-menu-preview.cmd guide   ->  artifacts\controls-guide-<page>.png
#define NOMINMAX
#include <windows.h>
#include <fstream>
#include <sstream>
#include <cstdio>
#include "controls_guide_image.hpp"
#pragma comment(lib,"gdi32.lib")
#pragma comment(lib,"user32.lib")
#pragma comment(lib,"msimg32.lib")
static void write_bmp(const std::vector<std::uint8_t>& px,unsigned w,unsigned h,const char* file) {
    BITMAPFILEHEADER f{};BITMAPINFOHEADER i{};i.biSize=sizeof i;i.biWidth=static_cast<LONG>(w);i.biHeight=-static_cast<LONG>(h);i.biPlanes=1;i.biBitCount=32;
    f.bfType=0x4d42;f.bfOffBits=sizeof f+sizeof i;f.bfSize=f.bfOffBits+static_cast<DWORD>(px.size());
    std::vector<std::uint8_t> bgra(px);for(std::size_t k=0;k+3<bgra.size();k+=4) std::swap(bgra[k],bgra[k+2]);
    std::ofstream o(file,std::ios::binary);o.write(reinterpret_cast<const char*>(&f),sizeof f);o.write(reinterpret_cast<const char*>(&i),sizeof i);
    o.write(reinterpret_cast<const char*>(bgra.data()),static_cast<std::streamsize>(bgra.size()));
}
int main(int argc,char** argv) {
    d2vr::game_art().folder(L"artifacts/game-art");
    std::ifstream in("docs/VR-CONTROLS.md",std::ios::binary);std::ostringstream md;md<<in.rdbuf();
    const bool emily=argc>1 && std::string(argv[1])=="emily";
    for(unsigned page=0;page<d2vr::guide_page_count;++page) {
        d2vr::GuideLayout layout{};
        const auto px=d2vr::controls_guide_image(md.str(),1920,1200,&layout,page,emily,true,true); // combined menu: VR Settings tab first
        char name[96];std::snprintf(name,sizeof name,"artifacts/controls-guide-%s.bmp",d2vr::guide_page_ids[page]);
        write_bmp(px,1920,1200,name);
        std::printf("%s bullets %d drawn %d overflow %d\n",name,layout.bullets,layout.drawn,layout.overflow?1:0);
    }
}
