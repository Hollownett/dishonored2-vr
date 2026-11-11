#include "../src/sun_sprite_aspect.hpp"
#include <cstdio>
// v1141: the patch bytes decode to mov eax,imm32 / movd xmm13,eax / nop nop and replace exactly the 12 original bytes; the eye aspect is width/height.
int main() {
    int f=0;auto req=[&](bool c,const char* w){if(!c){++f;std::printf("FAIL %s\n",w);}};
    const auto b=d2vr::SunSpriteAspect::patched(.935f);
    req(b.size()==d2vr::SunSpriteAspect::original.size(),"same length");
    float v;std::memcpy(&v,b.data()+1,4);req(b[0]==0xb8 && v==.935f,"imm32 aspect");
    req(b[5]==0x66 && b[6]==0x44 && b[7]==0x0f && b[8]==0x6e && b[9]==0xe8,"movd xmm13,eax");
    const float sq=d2vr::SunSpriteAspect::eye_aspect(-.8f,.8f,.8f,-.8f);req(std::fabs(sq-1.f)<1e-6f,"square eye 1");
    const float wide=d2vr::SunSpriteAspect::eye_aspect(-1.f,1.f,.6f,-.6f);req(std::fabs(wide-std::tan(1.)/std::tan(.6))<1e-5,"tan ratio");
    req(d2vr::SunSpriteAspect::eye_aspect(0,0,1,-1)==0.f,"degenerate");
    std::printf("sun_sprite_aspect_check %s\n",f?"FAILED":"ok");return f;
}
