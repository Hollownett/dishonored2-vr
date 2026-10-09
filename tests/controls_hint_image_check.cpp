#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "../src/controls_hint_image.hpp"
#include <fstream>
#include <iostream>

void require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
int main() {
    try {
        using namespace d2vr;
        const auto first=controls_hint_image();
        require(first.size()==512*384*4,"Controls hint bitmap has wrong dimensions");
        const auto alpha=[&](const std::vector<std::uint8_t>& image,unsigned x,unsigned y) {return image[(y*512+x)*4+3];};
        const auto red=[&](const std::vector<std::uint8_t>& image,unsigned x,unsigned y) {return image[(y*512+x)*4];};
        require(alpha(first,256,200)==255 && alpha(first,40,60)==255,"Controls hint card interior is not opaque");
        require(alpha(first,2,2)==0 && alpha(first,511,383)==0 && alpha(first,256,6)==0,"Controls hint card corners are not transparent");
        // The title spans both halves, so these bands cover only the controller columns.
        unsigned left_text=0,right_text=0;
        for(unsigned y=92;y<340;++y) for(unsigned x=20;x<252;++x) if(red(first,x,y)>150) ++left_text;
        for(unsigned y=92;y<340;++y) for(unsigned x=260;x<492;++x) if(red(first,x,y)>150) ++right_text;
        require(left_text>200 && right_text>200,"Controls hint column text is missing");
        require(first==controls_hint_image(),"Controls hint image is not deterministic");
        const auto scaled=controls_hint_image(1024,768);
        require(scaled.size()==1024*768*4,"Scaled controls hint bitmap has wrong dimensions");
        require(scaled[(384*1024+512)*4+3]==255 && scaled[(4*1024+4)*4+3]==0,"Scaled controls hint card coverage differs");
        std::ofstream preview("artifacts/controls-hint.rgba",std::ios::binary);
        preview.write(reinterpret_cast<const char*>(first.data()),static_cast<std::streamsize>(first.size()));
        require(static_cast<bool>(preview),"Cannot save controls hint preview");
        std::cout<<"PASS: controls hint card size, opaque card with clear corners, text in both controller columns, scaled redraw and determinism\n";
        return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
