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
    {   // v1145: the off-centre shape. At the centre it is the stock (size,0,0,size*aspect); 50 deg off along x the quad's radial half-extent, seen
        // from the eye, spans the same angle as its tangential one (round in the headset), where the stock quad spans ~0.64 of it radially.
        const float fx=1/std::tan(.9f),fy=1/std::tan(.95f),asp=fy/fx,sz=.02f;
        float b[8]{0,0,sz,asp};d2vr::SunSpriteShape::shape(b,fx,fy);
        req(std::fabs(b[4]-sz)<1e-6f && std::fabs(b[7]-sz*asp)<1e-6f && b[5]==0,"centre = stock");
        const double th=50*3.14159265/180,x=std::tan(th)*fx;
        float o[8]{static_cast<float>(x),0,sz,asp};d2vr::SunSpriteShape::shape(o,fx,fy);
        const auto radial=[&](double a){return std::atan((x+a)/fx)-std::atan(x/fx);};
        const auto tangential=[&](double a){return std::atan2(a/fy,std::sqrt(1+(x/fx)*(x/fx)));};
        const double fixed=radial(o[4])/tangential(o[7]),stock=radial(sz)/tangential(sz*asp);
        req(std::fabs(fixed-1)<.03,"round off-axis");req(stock<.7,"the stock quad is squeezed");
        std::printf("  off-axis 50 deg: stock radial/tangential %.3f, shaped %.3f\n",stock,fixed);
        float bad[8]{.3f,.2f,sz,asp};d2vr::SunSpriteShape::shape(bad,0,0);req(bad[4]==sz && bad[7]==sz*asp,"no projection: stock");
    }
    std::printf("sun_sprite_aspect_check %s\n",f?"FAILED":"ok");return f;
}
