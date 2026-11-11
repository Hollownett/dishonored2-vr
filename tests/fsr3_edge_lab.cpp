// v1169 lab (WARP): the FSR 3 upscale as FsrTaa runs it in upscale mode (LDR, no auto exposure, jittered MVs, reverse-Z), on a flat grey
// input, to find where the bright right/bottom band of the eye images comes from. Prints the luma of the output's outermost columns/rows.
// Variants change one thing at a time: the input texture size (render size vs a larger context), what lies past the render rectangle,
// sharpening, the motion-vector texture size, the jitter.   Build/run: build\check-fsr3-edge.cmd [W H RW RH frames]
#define NOMINMAX
#include <windows.h>
#include <d3d11_1.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include "../src/fsr3_eye_upscaler.hpp"
using Microsoft::WRL::ComPtr;
static ComPtr<ID3D11Device> dev;static ComPtr<ID3D11DeviceContext> imm;
static ComPtr<ID3D11Texture2D> tex(UINT w,UINT h,DXGI_FORMAT f,UINT bind,const void* data,UINT pitch) {
    D3D11_TEXTURE2D_DESC d{};d.Width=w;d.Height=h;d.MipLevels=d.ArraySize=1;d.Format=f;d.SampleDesc.Count=1;d.BindFlags=bind;
    D3D11_SUBRESOURCE_DATA s{data,pitch,0};ComPtr<ID3D11Texture2D> t;
    if(FAILED(dev->CreateTexture2D(&d,data?&s:nullptr,&t))) std::printf("CreateTexture2D failed\n");
    return t;
}
struct Variant {const char* name;bool big_input;unsigned beyond;bool pad;float sharpness;bool full_mv;bool jitter;};
static void run(const Variant& v,UINT W,UINT H,UINT RW,UINT RH,int frames) {
    const UINT CW=v.big_input?W:RW,CH=v.big_input?H:RH;
    std::vector<std::uint32_t> colour(CW*CH);
    for(UINT y=0;y<CH;++y) for(UINT x=0;x<CW;++x) {
        const bool in=x<RW && y<RH;const unsigned g=in?128u:v.beyond;
        colour[y*CW+x]=in || !v.pad?(g|g<<8|g<<16|0xffu<<24):(128u|128u<<8|128u<<16|0xffu<<24);
    }
    std::vector<float> depth(CW*CH,.1f);std::vector<std::uint8_t> reactive(CW*CH,0);
    const UINT MW=v.full_mv?W:CW,MH=v.full_mv?H:CH;std::vector<std::uint32_t> mv(MW*MH,0);
    auto c=tex(CW,CH,DXGI_FORMAT_R8G8B8A8_UNORM,D3D11_BIND_SHADER_RESOURCE,colour.data(),CW*4);
    auto d=tex(CW,CH,DXGI_FORMAT_R32_FLOAT,D3D11_BIND_SHADER_RESOURCE,depth.data(),CW*4);
    auto r=tex(CW,CH,DXGI_FORMAT_R8_UNORM,D3D11_BIND_SHADER_RESOURCE,reactive.data(),CW);
    auto m=tex(MW,MH,DXGI_FORMAT_R16G16_FLOAT,D3D11_BIND_SHADER_RESOURCE,mv.data(),MW*4);
    auto o=tex(W,H,DXGI_FORMAT_R8G8B8A8_UNORM,D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS,nullptr,0);
    auto holder=std::make_unique<d2vr::Fsr3EyeUpscaler>();auto& fsr=*holder;d2vr::Fsr3EyeUpscaler::Desc fd{}; // the FSR context is too big for the stack
    fd.render_w=CW;fd.render_h=CH;fd.display_w=W;fd.display_h=H;fd.mv_jittered=true;fd.hdr=false;fd.auto_exposure=false;
    if(!fsr.create(dev.Get(),fd)) {std::printf("%s: FSR3 create failed\n",v.name);return;}
    for(int f=0;f<frames;++f) {
        d2vr::Fsr3EyeUpscaler::Inputs in{};in.colour=c.Get();in.depth=d.Get();in.motion=m.Get();in.output=o.Get();in.reactive=r.Get();
        in.render_w=RW;in.render_h=RH;in.mv_scale_x=float(RW);in.mv_scale_y=float(RH);
        float jx=0,jy=0;if(v.jitter) ffxFsr3GetJitterOffset(&jx,&jy,f,fsr.phase_count());
        in.jitter_px_x=jx;in.jitter_px_y=jy;in.reset=f==0;in.frame_time_ms=11.1f;in.sharpness=v.sharpness;in.cam_near=.01f;in.cam_far=1e6f;in.vfov_rad=1.6f;
        if(!fsr.dispatch(imm.Get(),in)) {std::printf("%s: dispatch failed\n",v.name);return;}
    }
    D3D11_TEXTURE2D_DESC sd{};o->GetDesc(&sd);sd.Usage=D3D11_USAGE_STAGING;sd.BindFlags=0;sd.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Texture2D> s;dev->CreateTexture2D(&sd,nullptr,&s);
    imm->CopyResource(s.Get(),o.Get());D3D11_MAPPED_SUBRESOURCE map{};if(FAILED(imm->Map(s.Get(),0,D3D11_MAP_READ,0,&map))) return;
    const auto luma=[&](UINT x,UINT y) {const auto* p=static_cast<const std::uint8_t*>(map.pData)+y*map.RowPitch+x*4;return (p[0]+p[1]+p[2])/3.0;};
    const auto col=[&](UINT x) {double t=0;for(UINT y=H/4;y<3*H/4;++y) t+=luma(x,y);return t/(H/2);};
    const auto row=[&](UINT y) {double t=0;for(UINT x=W/4;x<3*W/4;++x) t+=luma(x,y);return t/(W/2);};
    std::string out=std::string(v.name)+": right";for(UINT k=0;k<10;++k) out+=" "+std::to_string(int(col(W-1-k)+.5));
    out+=" | bottom";for(UINT k=0;k<10;++k) out+=" "+std::to_string(int(row(H-1-k)+.5));
    out+=" | top";for(UINT k=0;k<4;++k) out+=" "+std::to_string(int(row(k)+.5));
    out+=" | left";for(UINT k=0;k<4;++k) out+=" "+std::to_string(int(col(k)+.5));
    out+=" | inside "+std::to_string(int(luma(W/2,H/2)+.5));
    std::printf("%s\n",out.c_str());imm->Unmap(s.Get(),0);
}
int main(int argc,char** argv) {
    UINT W=2496,H=2688,RW=1922,RH=2070;int frames=4;
    if(argc>=5) {W=std::atoi(argv[1]);H=std::atoi(argv[2]);RW=std::atoi(argv[3]);RH=std::atoi(argv[4]);}
    if(argc>=6) frames=std::atoi(argv[5]);
    const D3D_FEATURE_LEVEL fl=D3D_FEATURE_LEVEL_11_1;
    if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,&fl,1,D3D11_SDK_VERSION,&dev,nullptr,&imm))) {std::printf("no WARP 11_1\n");return 1;}
    std::printf("eye %ux%u, render %ux%u, %d frames, flat grey 128 input\n",W,H,RW,RH,frames);
    const Variant variants[]={
        {"A render-size input, sharpen off, small MV",false,0,false,-1,false,true},
        {"B larger input, black past rect",true,0,false,-1,false,true},
        {"C larger input, white past rect",true,255,false,-1,false,true},
        {"D larger input, white past rect, 8 px pad",true,255,true,-1,false,true},
        {"E as A + sharpen 0.5",false,0,false,.5f,false,true},
        {"F as A + display-size MV texture",false,0,false,-1,true,true},
        {"G as A, no jitter",false,0,false,-1,false,false},
    };
    for(const auto& v:variants) run(v,W,H,RW,RH,frames);
    return 0;
}
