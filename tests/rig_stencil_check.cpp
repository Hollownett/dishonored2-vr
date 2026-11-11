// v1162 offline check of the rig stencil mark (src/rig_stencil.hpp): the state derivation rules, and on WARP that the derived states make
// the bit belong to the last surface that wrote a pixel's depth whatever the draw order, keep the engine's own stencil bits, and leave
// pixels whose depth test fails alone.   Build: build\check-rig-stencil.cmd   Run: build\rig_stencil_check.exe
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstring>
#include "../src/rig_stencil.hpp"
#pragma comment(lib,"d3d11.lib")
#pragma comment(lib,"d3dcompiler.lib")
using Microsoft::WRL::ComPtr;
static int failures=0;
#define CHECK(x) do {if(!(x)) {std::printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#x);++failures;}} while(0)

static D3D11_DEPTH_STENCIL_DESC reverse_z() {auto d=d2vr::default_depth_stencil_desc();d.DepthFunc=D3D11_COMPARISON_GREATER;return d;}
static void pure() {
    const UINT8 b=d2vr::rig_stencil_bit;
    { // stencil off, depth write: bit only, REPLACE on depth pass
        const auto r=d2vr::rig_stencil_info(reverse_z(),b);
        CHECK(r.writes_depth && r.derivable && !r.conflict && r.derived.StencilEnable && r.derived.StencilWriteMask==b && r.derived.StencilReadMask==0);
        CHECK(r.derived.FrontFace.StencilPassOp==D3D11_STENCIL_OP_REPLACE && r.derived.FrontFace.StencilFailOp==D3D11_STENCIL_OP_KEEP &&
              r.derived.FrontFace.StencilDepthFailOp==D3D11_STENCIL_OP_KEEP && r.derived.BackFace.StencilFunc==D3D11_COMPARISON_ALWAYS);
        CHECK(r.derived.DepthFunc==D3D11_COMPARISON_GREATER && r.derived.DepthWriteMask==D3D11_DEPTH_WRITE_MASK_ALL);
    }
    { // depth not written: nothing to mark
        auto d=reverse_z();d.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;const auto r=d2vr::rig_stencil_info(d,b);
        CHECK(!r.writes_depth && !r.derivable && !r.conflict);
    }
    { // the engine's glass-style marker: REPLACE 0x04, its bit kept, ours added
        auto d=reverse_z();d.StencilEnable=TRUE;d.StencilReadMask=0x04;d.StencilWriteMask=0x04;
        d.FrontFace={D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_REPLACE,D3D11_COMPARISON_ALWAYS};d.BackFace=d.FrontFace;
        const auto r=d2vr::rig_stencil_info(d,b);
        CHECK(r.derivable && !r.conflict && r.derived.StencilWriteMask==(0x04|b) && r.derived.StencilReadMask==0x04);
    }
    { // a stencil test with every bit in its read mask: the bit would change its result
        auto d=reverse_z();d.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;d.StencilEnable=TRUE;d.StencilReadMask=0xff;
        d.FrontFace={D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_KEEP,D3D11_COMPARISON_EQUAL};d.BackFace=d.FrontFace;
        CHECK(d2vr::rig_stencil_info(d,b).conflict);
        d.StencilReadMask=0x7f;CHECK(!d2vr::rig_stencil_info(d,b).conflict); // the same test without the bit
        d.FrontFace.StencilFunc=d.BackFace.StencilFunc=D3D11_COMPARISON_ALWAYS;d.StencilReadMask=0xff;CHECK(!d2vr::rig_stencil_info(d,b).conflict); // reads nothing
    }
    { // a write of every bit (REPLACE 0xff) would clear or set ours
        auto d=reverse_z();d.StencilEnable=TRUE;d.StencilWriteMask=0xff;
        d.FrontFace={D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_REPLACE,D3D11_COMPARISON_ALWAYS};d.BackFace=d.FrontFace;
        CHECK(d2vr::rig_stencil_info(d,b).conflict);
    }
    { // not derivable: INCR, a fail op, mixed faces
        auto d=reverse_z();d.StencilEnable=TRUE;d.StencilWriteMask=0x04;
        d.FrontFace={D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_INCR_SAT,D3D11_COMPARISON_ALWAYS};d.BackFace=d.FrontFace;
        CHECK(!d2vr::rig_stencil_info(d,b).derivable && !d2vr::rig_stencil_info(d,b).conflict);
        d.FrontFace.StencilPassOp=d.BackFace.StencilPassOp=D3D11_STENCIL_OP_REPLACE;d.FrontFace.StencilDepthFailOp=D3D11_STENCIL_OP_ZERO;
        CHECK(!d2vr::rig_stencil_info(d,b).derivable);
        d.FrontFace.StencilDepthFailOp=D3D11_STENCIL_OP_KEEP;d.BackFace.StencilPassOp=D3D11_STENCIL_OP_KEEP;
        CHECK(!d2vr::rig_stencil_info(d,b).derivable);
        d.BackFace.StencilPassOp=D3D11_STENCIL_OP_REPLACE;CHECK(d2vr::rig_stencil_info(d,b).derivable);
    }
    { // engine writes nothing (KEEP everywhere) but tests: only the bit is written
        auto d=reverse_z();d.StencilEnable=TRUE;d.StencilReadMask=0x04;d.StencilWriteMask=0xff;
        d.FrontFace={D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_KEEP,D3D11_COMPARISON_EQUAL};d.BackFace=d.FrontFace;
        const auto r=d2vr::rig_stencil_info(d,b);
        CHECK(!r.conflict && r.derivable && r.derived.StencilWriteMask==b && r.derived.FrontFace.StencilPassOp==D3D11_STENCIL_OP_REPLACE && r.derived.StencilReadMask==0x04);
    }
    CHECK(d2vr::rig_stencil_ref(0x04,true,b)==0x84 && d2vr::rig_stencil_ref(0x84,false,b)==0x04 && d2vr::rig_stencil_ref(0,false,b)==0);
}

// WARP: 64x64 D24S8, reverse-Z (depth cleared to 0, GREATER). Full-viewport quads at a constant depth.
static void warp() {
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> c;
    if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&c))) {std::printf("FAIL no WARP device\n");++failures;return;}
    constexpr UINT n=64;
    D3D11_TEXTURE2D_DESC t{};t.Width=t.Height=n;t.MipLevels=t.ArraySize=1;t.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;t.SampleDesc.Count=1;t.BindFlags=D3D11_BIND_DEPTH_STENCIL;
    ComPtr<ID3D11Texture2D> depth;ComPtr<ID3D11DepthStencilView> dsv;
    CHECK(SUCCEEDED(device->CreateTexture2D(&t,nullptr,&depth)) && SUCCEEDED(device->CreateDepthStencilView(depth.Get(),nullptr,&dsv)));
    static const char vs_src[]="cbuffer C:register(b0){float4 z;};float4 main(uint i:SV_VertexID):SV_Position{float2 p=float2((i<<1)&2,i&2);return float4(p*2-1,z.x,1);}";
    ComPtr<ID3DBlob> code;ComPtr<ID3D11VertexShader> vs;
    CHECK(SUCCEEDED(D3DCompile(vs_src,sizeof vs_src-1,nullptr,nullptr,nullptr,"main","vs_5_0",0,0,&code,nullptr)) &&
          SUCCEEDED(device->CreateVertexShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&vs)));
    D3D11_BUFFER_DESC bd{};bd.ByteWidth=16;bd.Usage=D3D11_USAGE_DEFAULT;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;ComPtr<ID3D11Buffer> cb;device->CreateBuffer(&bd,nullptr,&cb);
    D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;ComPtr<ID3D11RasterizerState> rs;device->CreateRasterizerState(&rd,&rs);
    c->OMSetRenderTargets(0,nullptr,dsv.Get());c->VSSetShader(vs.Get(),nullptr,0);c->PSSetShader(nullptr,nullptr,0);c->RSSetState(rs.Get());
    c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);c->VSSetConstantBuffers(0,1,cb.GetAddressOf());
    c->ClearDepthStencilView(dsv.Get(),D3D11_CLEAR_DEPTH|D3D11_CLEAR_STENCIL,0.f,0);
    const UINT8 b=d2vr::rig_stencil_bit;
    const auto derived=[&](const D3D11_DEPTH_STENCIL_DESC& d) {const auto r=d2vr::rig_stencil_info(d,b);ComPtr<ID3D11DepthStencilState> s;CHECK(r.derivable);device->CreateDepthStencilState(&r.derived,&s);return s;};
    auto glass=reverse_z();glass.StencilEnable=TRUE;glass.StencilReadMask=0x04;glass.StencilWriteMask=0x04;
    glass.FrontFace={D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_REPLACE,D3D11_COMPARISON_ALWAYS};glass.BackFace=glass.FrontFace;
    const auto plain=derived(reverse_z()),marker=derived(glass);
    const auto draw=[&](ID3D11DepthStencilState* s,UINT engine_ref,bool rig,float z,float x,float y,float w,float h) {
        const float k[4]{z,0,0,0};c->UpdateSubresource(cb.Get(),0,nullptr,k,0,0);
        const D3D11_VIEWPORT v{x,y,w,h,0,1};c->RSSetViewports(1,&v);
        c->OMSetDepthStencilState(s,d2vr::rig_stencil_ref(engine_ref,rig,b));c->Draw(3,0);
    };
    const float h=static_cast<float>(n),half=h/2;
    draw(marker.Get(),4,false,.3f,0,0,half,h);     // the engine's marker (4) on the left half, a world surface
    draw(plain.Get(),0,true,.5f,0,0,h,h);          // the rig nearer, everywhere: bit set, the engine's 4 kept
    draw(plain.Get(),0,false,.8f,half,0,half,h);   // a world surface nearer still on the right half, drawn later: bit cleared there
    draw(plain.Get(),0,false,.2f,0,0,h,half);      // a farther world surface on the top half: depth fails, nothing changes
    D3D11_TEXTURE2D_DESC sd=t;sd.BindFlags=0;sd.Usage=D3D11_USAGE_STAGING;sd.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Texture2D> staging;
    CHECK(SUCCEEDED(device->CreateTexture2D(&sd,nullptr,&staging)));
    c->CopyResource(staging.Get(),depth.Get());D3D11_MAPPED_SUBRESOURCE m{};
    if(FAILED(c->Map(staging.Get(),0,D3D11_MAP_READ,0,&m))) {std::printf("FAIL map\n");++failures;return;}
    const auto stencil=[&](UINT x,UINT y) {std::uint32_t v;std::memcpy(&v,static_cast<const char*>(m.pData)+y*m.RowPitch+x*4,4);return v>>24;};
    CHECK(stencil(8,8)==(0x04u|b) && stencil(8,56)==(0x04u|b));   // left: rig over the marked surface
    CHECK(stencil(56,8)==0 && stencil(56,56)==0);                 // right: the later nearer world surface owns it
    c->Unmap(staging.Get(),0);
}
int main() {
    pure();warp();
    if(failures) {std::printf("rig_stencil_check: %d failure(s)\n",failures);return 1;}
    std::printf("PASS: rig_stencil_check (derivation rules; WARP: last depth writer owns the bit, engine bits kept, failed depth leaves it)\n");
    return 0;
}
