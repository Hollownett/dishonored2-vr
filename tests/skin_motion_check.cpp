// v1182 offline check of src/skin_motion.hpp on WARP: the per-model palette ring (rotated at the "skin copy" once per engine frame) gives
// a prepass-form draw (t0 the model's other wrapper, t1 its current) the palette of two engine frames back at t0, only once three consecutive
// frames were rotated; restore() puts the engine's t0 back; an unknown model, a second call in one frame and the model cap behave.
//   Build/run: build\check-skin-motion.cmd
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <cstdio>
#include "../src/skin_motion.hpp"
#pragma comment(lib,"d3d11.lib")
#pragma comment(lib,"d3dcompiler.lib")
using Microsoft::WRL::ComPtr;
static int failures=0;
#define CHECK(x) do {if(!(x)) {std::printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#x);++failures;}} while(0)
int main() {
    ComPtr<ID3D11Device> dev;ComPtr<ID3D11DeviceContext> c;
    if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&dev,nullptr,&c))) {std::printf("FAIL WARP\n");return 1;}
    D3D11_BUFFER_DESC bd{};bd.ByteWidth=16;bd.Usage=D3D11_USAGE_DEFAULT;bd.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    D3D11_SHADER_RESOURCE_VIEW_DESC vd{};vd.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;vd.ViewDimension=D3D11_SRV_DIMENSION_BUFFER;vd.Buffer.NumElements=1;
    ComPtr<ID3D11Buffer> w[2];ComPtr<ID3D11ShaderResourceView> v[2];
    for(int i=0;i<2;++i) {dev->CreateBuffer(&bd,nullptr,&w[i]);dev->CreateShaderResourceView(w[i].Get(),&vd,&v[i]);}
    D3D11_BUFFER_DESC sd=bd;sd.Usage=D3D11_USAGE_STAGING;sd.BindFlags=0;sd.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Buffer> st;dev->CreateBuffer(&sd,nullptr,&st);
    const auto t0_value=[&] {ID3D11ShaderResourceView* x{};c->VSGetShaderResources(0,1,&x);float r=-1;
        if(x) {ComPtr<ID3D11Resource> res;x->GetResource(&res);c->CopyResource(st.Get(),res.Get());D3D11_MAPPED_SUBRESOURCE m{};
            if(SUCCEEDED(c->Map(st.Get(),0,D3D11_MAP_READ,0,&m))) {r=*static_cast<float*>(m.pData);c->Unmap(st.Get(),0);}x->Release();}
        return r;};
    d2vr::SkinMotion::enabled=true;
    const std::uintptr_t model=0x1000;
    const auto copy=[&](std::uint32_t f) { // the engine's upload into wrapper f%2, then the hook
        const float p[4]{float(f),0,0,0};c->UpdateSubresource(w[f%2].Get(),0,nullptr,p,0,0);
        d2vr::SkinMotion::skin_copied(c.Get(),model,w[0].Get(),w[1].Get(),w[f%2].Get(),f);
    };
    const auto draw_binding=[&](std::uint32_t f) {ID3D11ShaderResourceView* pair[2]{v[(f+1)%2].Get(),v[f%2].Get()};c->VSSetShaderResources(0,2,pair);};
    copy(10);draw_binding(10);CHECK(!d2vr::SkinMotion::bind_previous(c.Get()));   // one rotation: not valid yet
    copy(11);draw_binding(11);CHECK(!d2vr::SkinMotion::bind_previous(c.Get()));
    copy(12);copy(12);draw_binding(12);                                             // a second call in one frame does nothing
    CHECK(d2vr::SkinMotion::bind_previous(c.Get()));CHECK(t0_value()==10.f);        // P = frame 10's palette (two engine frames back)
    CHECK(!d2vr::SkinMotion::bind_previous(c.Get()));                              // already bound for this draw
    d2vr::SkinMotion::restore(c.Get());CHECK(t0_value()==11.f);                     // the engine's previous wrapper again
    copy(13);draw_binding(13);CHECK(d2vr::SkinMotion::bind_previous(c.Get()) && t0_value()==11.f);d2vr::SkinMotion::restore(c.Get());
    copy(15);draw_binding(15);CHECK(!d2vr::SkinMotion::bind_previous(c.Get()));   // frame 14 had no copy: the run restarts
    { // a draw of a model the ring does not know
        ComPtr<ID3D11Buffer> o;dev->CreateBuffer(&bd,nullptr,&o);ComPtr<ID3D11ShaderResourceView> ov;dev->CreateShaderResourceView(o.Get(),&vd,&ov);
        ID3D11ShaderResourceView* pair[2]{ov.Get(),ov.Get()};c->VSSetShaderResources(0,2,pair);CHECK(!d2vr::SkinMotion::bind_previous(c.Get()));
    }
    for(std::uintptr_t m=1;m<=70;++m) d2vr::SkinMotion::skin_copied(c.Get(),0x2000+m*16,w[0].Get(),w[1].Get(),w[0].Get(),15); // past the cap
    const auto j=d2vr::SkinMotion::json();std::printf("%s\n",j.c_str());
    CHECK(j.find("\"models\":64")!=std::string::npos && j.find("\"overflow\":7")!=std::string::npos);
    if(failures) {std::printf("skin_motion_check: %d failure(s)\n",failures);return 1;}
    std::printf("PASS: skin_motion_check (own previous palette two engine frames back, restore, gaps restart, unknown models, cap)\n");
    return 0;
}
