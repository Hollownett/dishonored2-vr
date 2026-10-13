// WARP check of TaaMvDejitter: a stand-in "TAA" shader copies t1 into u1, so u1 shows what the TAA saw.
#define NOMINMAX
#include "../src/taa_mv_dejitter.hpp"
#include "../src/game_stereo.hpp"
#include <cmath>
#include <cstring>
#include <cstdio>
#include <vector>
#pragma comment(lib,"d3d11.lib")
#pragma comment(lib,"d3dcompiler.lib")
using Microsoft::WRL::ComPtr;

static void STDMETHODCALLTYPE call_dispatch(ID3D11DeviceContext* c,UINT x,UINT y,UINT z) {c->Dispatch(x,y,z);}
static unsigned short half(float f) {
    unsigned u;std::memcpy(&u,&f,4);const unsigned sign=(u>>16)&0x8000;int e=static_cast<int>((u>>23)&0xff)-127+15;unsigned m=u&0x7fffff;
    if(e<=0) return static_cast<unsigned short>(sign);
    return static_cast<unsigned short>(sign|(e<<10)|(m>>13));
}
static float from_half(unsigned short h) {
    const unsigned sign=(h&0x8000)<<16,e=(h>>10)&0x1f,m=h&0x3ff;
    if(!e) return 0.f;
    const unsigned u=sign|((e-15+127)<<23)|(m<<13);float f;std::memcpy(&f,&u,4);return f;
}
#define REQUIRE(x) do{if(!(x)){std::printf("FAIL %s (line %d)\n",#x,__LINE__);return 1;}}while(0)

int main() {
    // Sign: still head, jitter index 3 -> 4 must give the measured MV (2,10)/16 px.
    const float W=2496,H=2688;
    const auto j3=d2vr::engine_taa_jitter(3,W,H),j4=d2vr::engine_taa_jitter(4,W,H);
    const auto s=d2vr::TaaMvDejitter::step(j4.x,j4.y,j3.x,j3.y);
    REQUIRE(std::abs(s[0]*W-2.f/16)<1e-5f && std::abs(s[1]*H-10.f/16)<1e-5f);

    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> c;
    REQUIRE(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&c)));
    const UINT w=16,h=8;
    auto tex=[&](DXGI_FORMAT f,UINT bind,const void* data,UINT pitch) {
        D3D11_TEXTURE2D_DESC d{};d.Width=w;d.Height=h;d.MipLevels=1;d.ArraySize=1;d.Format=f;d.SampleDesc.Count=1;d.BindFlags=bind;
        D3D11_SUBRESOURCE_DATA init{data,pitch,0};ComPtr<ID3D11Texture2D> t;device->CreateTexture2D(&d,data?&init:nullptr,&t);return t;
    };
    std::vector<unsigned short> mv(w*h*2);
    for(UINT i=0;i<w*h;++i) {mv[i*2]=half(.01f*static_cast<float>(i%w));mv[i*2+1]=half(-.02f);}
    auto history=tex(DXGI_FORMAT_R16G16B16A16_FLOAT,D3D11_BIND_SHADER_RESOURCE,nullptr,0);
    auto motion=tex(DXGI_FORMAT_R16G16_FLOAT,D3D11_BIND_SHADER_RESOURCE,mv.data(),w*4);
    auto colour=tex(DXGI_FORMAT_R16G16B16A16_FLOAT,D3D11_BIND_SHADER_RESOURCE,nullptr,0);
    auto out0=tex(DXGI_FORMAT_R32G32B32A32_FLOAT,D3D11_BIND_UNORDERED_ACCESS,nullptr,0);
    auto out1=tex(DXGI_FORMAT_R32G32B32A32_FLOAT,D3D11_BIND_UNORDERED_ACCESS,nullptr,0);
    D3D11_BUFFER_DESC bd{};bd.ByteWidth=32;bd.BindFlags=D3D11_BIND_SHADER_RESOURCE;bd.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;bd.StructureByteStride=32;
    ComPtr<ID3D11Buffer> exposure;REQUIRE(SUCCEEDED(device->CreateBuffer(&bd,nullptr,&exposure)));
    D3D11_BUFFER_DESC cbd{};cbd.ByteWidth=16;cbd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    ComPtr<ID3D11Buffer> cb1,cb2;device->CreateBuffer(&cbd,nullptr,&cb1);device->CreateBuffer(&cbd,nullptr,&cb2);
    std::array<ComPtr<ID3D11ShaderResourceView>,4> srv;std::array<ComPtr<ID3D11UnorderedAccessView>,2> uav;
    device->CreateShaderResourceView(history.Get(),nullptr,&srv[0]);device->CreateShaderResourceView(motion.Get(),nullptr,&srv[1]);
    device->CreateShaderResourceView(colour.Get(),nullptr,&srv[2]);device->CreateShaderResourceView(exposure.Get(),nullptr,&srv[3]);
    device->CreateUnorderedAccessView(out0.Get(),nullptr,&uav[0]);device->CreateUnorderedAccessView(out1.Get(),nullptr,&uav[1]);
    static const char taa_src[]="Texture2D<float2> mv:register(t1);RWTexture2D<float4> o:register(u1);"
        "[numthreads(8,8,1)] void main(uint3 p:SV_DispatchThreadID){o[p.xy]=float4(mv.Load(int3(p.xy,0)),0,1);}";
    ComPtr<ID3DBlob> code;REQUIRE(SUCCEEDED(D3DCompile(taa_src,sizeof taa_src-1,nullptr,nullptr,nullptr,"main","cs_5_0",0,0,&code,nullptr)));
    ComPtr<ID3D11ComputeShader> taa;REQUIRE(SUCCEEDED(device->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&taa)));
    ID3D11ShaderResourceView* views[4]{srv[0].Get(),srv[1].Get(),srv[2].Get(),srv[3].Get()};ID3D11UnorderedAccessView* uavs[2]{uav[0].Get(),uav[1].Get()};
    ID3D11Buffer* cbs[2]{cb1.Get(),cb2.Get()};
    c->CSSetShader(taa.Get(),nullptr,0);c->CSSetShaderResources(0,4,views);c->CSSetUnorderedAccessViews(0,2,uavs,nullptr);c->CSSetConstantBuffers(1,2,cbs);

    d2vr::TaaMvDejitter dejitter;
    {d2vr::TaaBindings b;b.read(c.Get());REQUIRE(b.taa());REQUIRE(!dejitter.dispatch(c.Get(),b,s,&call_dispatch,2,1,1));} // not prepared yet
    dejitter.prepare(device.Get());
    {d2vr::TaaBindings b;b.read(c.Get());REQUIRE(dejitter.dispatch(c.Get(),b,{.005f,.01f},&call_dispatch,2,1,1));}
    // t1 and the TAA shader are back as the engine left them.
    ID3D11ShaderResourceView* t1{};c->CSGetShaderResources(1,1,&t1);REQUIRE(t1==srv[1].Get());t1->Release();
    ID3D11ComputeShader* bound{};c->CSGetShader(&bound,nullptr,nullptr);REQUIRE(bound==taa.Get());bound->Release();

    D3D11_TEXTURE2D_DESC d{};out1->GetDesc(&d);d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;device->CreateTexture2D(&d,nullptr,&staging);c->CopyResource(staging.Get(),out1.Get());
    D3D11_MAPPED_SUBRESOURCE m{};REQUIRE(SUCCEEDED(c->Map(staging.Get(),0,D3D11_MAP_READ,0,&m)));
    for(UINT y=0;y<h;++y) for(UINT x=0;x<w;++x) {
        const float* p=reinterpret_cast<const float*>(static_cast<const char*>(m.pData)+y*m.RowPitch)+x*4;
        const float ex=from_half(mv[(y*w+x)*2])-.005f,ey=from_half(mv[(y*w+x)*2+1])-.01f;
        REQUIRE(std::abs(p[0]-ex)<2e-3f && std::abs(p[1]-ey)<2e-3f);
    }
    c->Unmap(staging.Get(),0);
    std::printf("PASS: native TAA sees MV minus the jitter step; t1 and shader restored\n");
    return 0;
}
