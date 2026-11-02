// SamplerLodBias on WARP: mip-filtered samplers get a biased clone, others stay; bias 0 binds originals;
// a bias change makes new clones and keeps the old ones alive; bias_for follows log2(fraction)+extra.
#define NOMINMAX
#include "../src/sampler_lod_bias.hpp"
#include <cstdio>
#pragma comment(lib,"d3d11.lib")
using Microsoft::WRL::ComPtr;
#define REQUIRE(x) do{if(!(x)){std::printf("FAIL %s (line %d)\n",#x,__LINE__);return 1;}}while(0)
static float bias_of(ID3D11SamplerState* s) {D3D11_SAMPLER_DESC d{};s->GetDesc(&d);return d.MipLODBias;}
int main() {
    using d2vr::SamplerLodBias;
    REQUIRE(SamplerLodBias::bias_for(1.f,-.5f)==-.5f && SamplerLodBias::bias_for(1.2f,-.5f)==-.5f && SamplerLodBias::bias_for(0.f,-.5f)==0); // native AA keeps the extra (v873)
    REQUIRE(std::abs(SamplerLodBias::bias_for(.5f,0)+1)<1e-6f && std::abs(SamplerLodBias::bias_for(.77f,-.5f)-(std::log2(.77f)-.5f))<1e-6f);
    ComPtr<ID3D11Device> device;
    REQUIRE(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,nullptr)));
    auto make=[&](D3D11_FILTER f,float max_lod,float bias=0) {
        D3D11_SAMPLER_DESC d{};d.Filter=f;d.AddressU=d.AddressV=d.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;d.MaxAnisotropy=16;
        d.ComparisonFunc=D3D11_COMPARISON_LESS;d.MinLOD=0;d.MaxLOD=max_lod;d.MipLODBias=bias;
        ComPtr<ID3D11SamplerState> s;device->CreateSamplerState(&d,&s);return s;
    };
    auto aniso=make(D3D11_FILTER_ANISOTROPIC,D3D11_FLOAT32_MAX),trilinear=make(D3D11_FILTER_MIN_MAG_MIP_LINEAR,D3D11_FLOAT32_MAX,.25f);
    auto point=make(D3D11_FILTER_MIN_MAG_MIP_POINT,D3D11_FLOAT32_MAX),nomip=make(D3D11_FILTER_MIN_MAG_MIP_LINEAR,0);
    auto shadow=make(D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR,D3D11_FLOAT32_MAX);
    REQUIRE(aniso && trilinear && point && nomip && shadow);
    // Off: originals.
    REQUIRE(SamplerLodBias::resolve(aniso.Get())==aniso.Get());
    SamplerLodBias::set_bias(-.88f);
    auto* a=SamplerLodBias::resolve(aniso.Get());auto* t=SamplerLodBias::resolve(trilinear.Get());
    REQUIRE(a!=aniso.Get() && std::abs(bias_of(a)+.88f)<1e-6f);
    REQUIRE(t!=trilinear.Get() && std::abs(bias_of(t)-(.25f-.88f))<1e-6f); // adds to the game's own bias
    REQUIRE(SamplerLodBias::resolve(point.Get())==point.Get() && SamplerLodBias::resolve(nomip.Get())==nomip.Get() &&
            SamplerLodBias::resolve(shadow.Get())==shadow.Get());
    REQUIRE(SamplerLodBias::resolve(aniso.Get())==a); // cached
    // Bias change: new clone, old one still valid.
    SamplerLodBias::set_bias(-.38f);
    auto* a2=SamplerLodBias::resolve(aniso.Get());
    REQUIRE(a2!=a && std::abs(bias_of(a2)+.38f)<1e-6f && std::abs(bias_of(a)+.88f)<1e-6f);
    SamplerLodBias::set_bias(0);
    REQUIRE(SamplerLodBias::resolve(aniso.Get())==aniso.Get());
    REQUIRE(SamplerLodBias::cached()==5 && SamplerLodBias::failures.load()==0);
    SamplerLodBias::disable();REQUIRE(SamplerLodBias::cached()==0);
    std::printf("PASS: mip-filtered samplers biased (aniso, trilinear +own bias), point/mip-less/comparison untouched, bias change keeps old clones\n");
    return 0;
}
