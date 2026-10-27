#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "../src/controls_guide_image.hpp"
#include <fstream>
#include <iostream>
#include <sstream>

void require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
int main() {
    try {
        using namespace d2vr;
        std::ifstream in("docs/VR-CONTROLS.md",std::ios::binary);require(static_cast<bool>(in),"docs/VR-CONTROLS.md missing (run from the repo root)");
        std::ostringstream text;text<<in.rdbuf();const auto md=text.str();
        const auto items=parse_controls_guide(md);
        int sections=0,bullets=0;for(const auto& i:items) {sections+=i.kind==GuideItem::section;bullets+=i.kind==GuideItem::bullet;}
        require(!items.empty() && items[0].kind==GuideItem::title && sections>=6 && bullets>=30,"The guide did not parse (title, sections, bullets)");
        for(const auto& i:items) if(i.kind==GuideItem::bullet) require(!i.key.empty() && !i.text.empty(),"A bullet lost its bold input or its description");
        GuideLayout layout{};
        const auto page=controls_guide_image(md,1920,1200,&layout);
        require(page.size()==1920u*1200*4,"Guide bitmap has wrong dimensions");
        {std::ofstream preview("artifacts/controls-guide.rgba",std::ios::binary);
        preview.write(reinterpret_cast<const char*>(page.data()),static_cast<std::streamsize>(page.size()));}
        std::cout<<"layout: "<<layout.drawn<<" of "<<layout.bullets<<" bullets drawn"<<(layout.overflow?", overflow":"")<<'\n';
        require(layout.bullets==bullets && layout.drawn==bullets && !layout.overflow,"Not every bullet fits on the page");
        const auto alpha=[&](unsigned x,unsigned y) {return page[(y*1920+x)*4+3];};
        require(alpha(960,600)==255 && alpha(2,2)==0 && alpha(1919,1199)==0,"Card interior opaque, corners clear");
        require(page==controls_guide_image(md,1920,1200),"Guide image is not deterministic");
        const auto missing=controls_guide_image("",512,320);require(missing.size()==512u*320*4,"An empty guide still draws the card");
        std::cout<<"PASS: controls guide parsed ("<<sections<<" sections, "<<bullets<<" bullets), every bullet drawn, card coverage, determinism\n";
        return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
