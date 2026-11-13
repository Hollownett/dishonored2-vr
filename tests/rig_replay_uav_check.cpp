// v1286 check of the UAV rig/held replay (src/rig_mask.hpp) against the v1285 render-target replay (build/legacy-rig-replay), on WARP,
// through the real RigMask::Scope + after() + take_target. A scene of six draws (world far, rig A sloped, held B nearer over A, rig C farther
// drawn after A/B, held D farther than C drawn after it, world near) is replayed both ways; per engine binding set:
//   0  two RTVs (the fast path: engine targets kept, one OM call adds the UAVs, one removes them)
//   1  seven RTVs (no UAV slot left: fallback with the engine RTVs unbound)
//   2  two RTVs + an engine PS UAV at u3 (fallback; the engine UAV restored)
//   3  eight RTVs (fallback; the eight-RTV restore)
// and D32 / D24 scene depth. Checks: the mask texture is bit-identical, both converted slices (take_target of slice 0 and 1) are
// bit-identical (depth and class of the nearest), the engine's colour targets are bit-identical (the UAV path writes none), the engine's
// RTVs/DSV/UAVs/PS/depth state/blend are restored exactly, spot pixels hold the nearest draw's class (held over rig; the farther draw
// replayed last loses), the status counts the path and the fallbacks, and (when the SDK layers are installed) no D3D11 debug-layer error.
//   Build/run: build\tmp-check-run.cmd rig_replay_uav_check /DNOMINMAX
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <d3d11sdklayers.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "../src/rig_mask.hpp"
#pragma comment(lib,"d3d11.lib")
#pragma comment(lib,"d3dcompiler.lib")
using Microsoft::WRL::ComPtr;
static int failures=0;
#define CHECK(x) do {if(!(x)) {std::printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#x);++failures;}} while(0)
constexpr UINT N=64;
struct Params {float r[4];float z[4];float o[4];};
static Params cur{};static ID3D11Buffer* params_cb{};
static const float offsets[2][2]{{.12f,-.05f},{-.07f,.09f}}; // the "retargeted rows" of slice 0 / 1: a shift in clip space
struct Result {std::vector<std::uint8_t> mask,slice[2],colour;unsigned errors{};bool debug{};};
static std::vector<std::uint8_t> read_back(ID3D11Device* d,ID3D11DeviceContext* c,ID3D11Resource* r,UINT bpp) {
    ComPtr<ID3D11Texture2D> t;r->QueryInterface(IID_PPV_ARGS(&t));D3D11_TEXTURE2D_DESC desc{};t->GetDesc(&desc);
    desc.BindFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.MiscFlags=0;
    ComPtr<ID3D11Texture2D> s;std::vector<std::uint8_t> out(N*N*bpp);
    if(FAILED(d->CreateTexture2D(&desc,nullptr,&s))) {++failures;return out;}
    c->CopyResource(s.Get(),t.Get());D3D11_MAPPED_SUBRESOURCE m{};
    if(FAILED(c->Map(s.Get(),0,D3D11_MAP_READ,0,&m))) {++failures;return out;}
    for(UINT y=0;y<N;++y) std::memcpy(&out[y*N*bpp],static_cast<const char*>(m.pData)+y*m.RowPitch,N*bpp);
    c->Unmap(s.Get(),0);return out;
}
static unsigned legacy_uav_lost=0; // the v1285 replay restores targets with OMSetRenderTargets, which drops an engine PS UAV (kept as it was)
static std::uint64_t num(const std::string& j,const char* key) {const auto p=j.find(key);return p==std::string::npos?~0ull:std::strtoull(j.c_str()+p+std::strlen(key),nullptr,10);}
static Result run(bool legacy,int cfg,bool d24,bool want_debug,bool aligned=true) {
    Result res;
    ComPtr<ID3D11Device> dev;ComPtr<ID3D11DeviceContext> c;
    if(want_debug && SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,D3D11_CREATE_DEVICE_DEBUG,nullptr,0,D3D11_SDK_VERSION,&dev,nullptr,&c))) res.debug=true;
    else if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&dev,nullptr,&c))) {std::printf("FAIL WARP\n");++failures;return res;}
    ComPtr<ID3D11InfoQueue> info;if(res.debug) dev.As(&info);
    d2vr::RigMask::reset();d2vr::RigMask::legacy_replay=legacy;
    D3D11_TEXTURE2D_DESC t{};t.Width=t.Height=N;t.MipLevels=t.ArraySize=1;t.SampleDesc.Count=1;t.Format=d24?DXGI_FORMAT_R24G8_TYPELESS:DXGI_FORMAT_R32_TYPELESS;
    t.BindFlags=D3D11_BIND_DEPTH_STENCIL|D3D11_BIND_SHADER_RESOURCE;ComPtr<ID3D11Texture2D> depth;dev->CreateTexture2D(&t,nullptr,&depth);
    D3D11_DEPTH_STENCIL_VIEW_DESC dv{};dv.Format=d24?DXGI_FORMAT_D24_UNORM_S8_UINT:DXGI_FORMAT_D32_FLOAT;dv.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2D;
    ComPtr<ID3D11DepthStencilView> dsv;CHECK(SUCCEEDED(dev->CreateDepthStencilView(depth.Get(),&dv,&dsv)));
    D3D11_TEXTURE2D_DESC ct=t;ct.Format=DXGI_FORMAT_R8G8B8A8_UNORM;ct.BindFlags=D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> colour[8];ComPtr<ID3D11RenderTargetView> rtv[8];
    for(int i=0;i<8;++i) {dev->CreateTexture2D(&ct,nullptr,&colour[i]);dev->CreateRenderTargetView(colour[i].Get(),nullptr,&rtv[i]);}
    D3D11_TEXTURE2D_DESC ut=t;ut.Format=DXGI_FORMAT_R32_UINT;ut.BindFlags=D3D11_BIND_UNORDERED_ACCESS;ComPtr<ID3D11Texture2D> engine_uav_tex;ComPtr<ID3D11UnorderedAccessView> engine_uav;
    dev->CreateTexture2D(&ut,nullptr,&engine_uav_tex);dev->CreateUnorderedAccessView(engine_uav_tex.Get(),nullptr,&engine_uav);
    const float pal[4]{};D3D11_BUFFER_DESC bd{};bd.ByteWidth=16;bd.BindFlags=D3D11_BIND_SHADER_RESOURCE;D3D11_SUBRESOURCE_DATA init{pal,0,0};
    ComPtr<ID3D11Buffer> rig_pal,other_pal;dev->CreateBuffer(&bd,&init,&rig_pal);dev->CreateBuffer(&bd,&init,&other_pal);
    D3D11_SHADER_RESOURCE_VIEW_DESC bv{};bv.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;bv.ViewDimension=D3D11_SRV_DIMENSION_BUFFER;bv.Buffer.NumElements=1;
    ComPtr<ID3D11ShaderResourceView> rig_view,other_view;dev->CreateShaderResourceView(rig_pal.Get(),&bv,&rig_view);dev->CreateShaderResourceView(other_pal.Get(),&bv,&other_view);
    d2vr::RigMask::publish({reinterpret_cast<std::uintptr_t>(rig_pal.Get()),0,0,0,0,0});
    static const char vs_src[]="Buffer<float4> skin:register(t0);cbuffer C:register(b0){float4 r;float4 z;float4 o;};"
        "float4 main(uint i:SV_VertexID):SV_Position{const float2 q[6]={float2(0,0),float2(1,0),float2(0,1),float2(1,0),float2(1,1),float2(0,1)};"
        "const float2 u=q[i];const float x=lerp(r.x,r.z,u.x)+o.x,y=lerp(r.y,r.w,u.y)+o.y;return float4(x,y,z.x+z.y*x+z.z*y,1)+skin[0]*0;}";
    static const char ps_src[]="struct O{float4 a:SV_Target0;float4 b:SV_Target1;};O main(float4 p:SV_Position){O o;o.a=float4(p.z,.5,.25,1);o.b=float4(1,0,1,1);return o;}";
    ComPtr<ID3DBlob> vsb,psb,err;
    if(FAILED(D3DCompile(vs_src,sizeof vs_src-1,nullptr,nullptr,nullptr,"main","vs_5_0",0,0,&vsb,&err)) || FAILED(D3DCompile(ps_src,sizeof ps_src-1,nullptr,nullptr,nullptr,"main","ps_5_0",0,0,&psb,&err))) {
        std::printf("FAIL compile %s\n",err?static_cast<const char*>(err->GetBufferPointer()):"");++failures;return res;}
    ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;dev->CreateVertexShader(vsb->GetBufferPointer(),vsb->GetBufferSize(),nullptr,&vs);dev->CreatePixelShader(psb->GetBufferPointer(),psb->GetBufferSize(),nullptr,&ps);
    D3D11_BUFFER_DESC cbd{};cbd.ByteWidth=sizeof(Params);cbd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;ComPtr<ID3D11Buffer> cb;dev->CreateBuffer(&cbd,nullptr,&cb);params_cb=cb.Get();
    D3D11_DEPTH_STENCIL_DESC ds{};ds.DepthEnable=TRUE;ds.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;ds.DepthFunc=D3D11_COMPARISON_GREATER;ComPtr<ID3D11DepthStencilState> prepass;dev->CreateDepthStencilState(&ds,&prepass);
    D3D11_BLEND_DESC bl{};bl.RenderTarget[0].RenderTargetWriteMask=0xf;ComPtr<ID3D11BlendState> blend;dev->CreateBlendState(&bl,&blend);
    D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;ComPtr<ID3D11RasterizerState> rs;dev->CreateRasterizerState(&rd,&rs);
    const D3D11_VIEWPORT vp{0,0,float(N),float(N),0,1};c->RSSetViewports(1,&vp);c->RSSetState(rs.Get());c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    c->VSSetShader(vs.Get(),nullptr,0);c->PSSetShader(ps.Get(),nullptr,0);c->VSSetConstantBuffers(0,1,cb.GetAddressOf());
    const UINT nrt=cfg==1?7u:cfg==3?8u:2u;ID3D11RenderTargetView* engine_rtvs[8]{};for(UINT i=0;i<nrt;++i) engine_rtvs[i]=rtv[i].Get();
    const float clear[4]{};for(auto& r:rtv) c->ClearRenderTargetView(r.Get(),clear);c->ClearDepthStencilView(dsv.Get(),D3D11_CLEAR_DEPTH|D3D11_CLEAR_STENCIL,0.f,0);
    if(cfg==2) {ID3D11UnorderedAccessView* u=engine_uav.Get();c->OMSetRenderTargetsAndUnorderedAccessViews(nrt,engine_rtvs,dsv.Get(),3,1,&u,nullptr);}
    else c->OMSetRenderTargets(nrt,engine_rtvs,dsv.Get());
    const float bf[4]{.25f,.5f,.75f,1};c->OMSetBlendState(blend.Get(),bf,0xfff);c->OMSetDepthStencilState(prepass.Get(),3);
    d2vr::RigMask::enabled=true;d2vr::RigMask::view=1;d2vr::RigMask::target_on=true;d2vr::RigMask::take_view_draws();
    d2vr::RigMask::target_bind=+[](ID3D11DeviceContext* x,unsigned slice) noexcept {Params p=cur;p.o[0]=offsets[slice][0];p.o[1]=offsets[slice][1];x->UpdateSubresource(params_cb,0,nullptr,&p,0,0);return true;};
    d2vr::RigMask::target_restore=+[](ID3D11DeviceContext* x) noexcept {x->UpdateSubresource(params_cb,0,nullptr,&cur,0,0);};
    const auto draw=[](ID3D11DeviceContext* x,const d2vr::RigMask::DrawArgs& a) noexcept {x->Draw(a.count,a.start);};
    const auto targets=[](ID3D11DeviceContext* x,UINT n,ID3D11RenderTargetView* const* r,ID3D11DepthStencilView* d) noexcept {x->OMSetRenderTargets(n,r,d);};
    // kind 0 world, 1 rig, 2 held. Reverse-Z: larger z is nearer.
    struct D {float r[4];float z[3];int kind;};
    const D scene[]{
        {{-1,-1,1,1},{.1f,0,0},0},            // world far
        {{-.8f,aligned?-.625f:-.6f,.3f,.5f},{.4f,.05f,.02f},1}, // rig A, sloped (aligned: its bottom edge, where C passes below, on a 2x2 quad boundary)
        {{-.2f,-.3f,.7f,.7f},{.6f,0,0},2},    // held B, nearer, over A (two classes)
        {{-.5f,-.8f,.5f,.2f},{.3f,0,0},1},    // rig C, farther than A/B, replayed after them
        {{.2f,-.9f,.9f,-.1f},{.25f,0,0},2},   // held D, farther than C, replayed after it
        {{.5f,.5f,1,1},{.9f,0,0},0}};         // world near
    for(UINT take=0;take<2;++take) { // the scene twice: a take converts one slice and clears both
        for(const D& d:scene) {
            cur={};std::memcpy(cur.r,d.r,16);std::memcpy(cur.z,d.z,12);c->UpdateSubresource(cb.Get(),0,nullptr,&cur,0,0);
            ID3D11ShaderResourceView* s=d.kind==1?rig_view.Get():other_view.Get();c->VSSetShaderResources(0,1,&s);
            d2vr::RigMask::Scope scope(c.Get(),1);
            if(d.kind==2) d2vr::RigMask::mark_held();
            if(d.kind) d2vr::RigMask::mark_target(true,true);
            c->Draw(6,0);scope.after({2,6,1,0,0,0},draw,targets);
        }
        { // the engine's state is its own again
            ID3D11RenderTargetView* r[8]{};ID3D11DepthStencilView* dd{};ID3D11UnorderedAccessView* u[8]{};c->OMGetRenderTargetsAndUnorderedAccessViews(8,r,&dd,0,8,u);
            for(UINT i=0;i<8;++i) CHECK(r[i]==engine_rtvs[i]);
            CHECK(dd==dsv.Get());for(UINT i=0;i<8;++i) if(u[i]!=(cfg==2 && i==3?engine_uav.Get():nullptr)) {if(legacy) {++legacy_uav_lost;continue;} std::printf("FAIL %s path: engine UAV slot %u not restored\n",legacy?"legacy":"uav",i);++failures;}
            for(auto* x:r) if(x) x->Release();
            for(auto* x:u) if(x) x->Release();
            if(dd) dd->Release();
            ID3D11PixelShader* p{};c->PSGetShader(&p,nullptr,nullptr);CHECK(p==ps.Get());if(p) p->Release();
            ID3D11DepthStencilState* st{};UINT ref{};c->OMGetDepthStencilState(&st,&ref);CHECK(st==prepass.Get() && ref==3);if(st) st->Release();
            ID3D11BlendState* b{};float f[4]{};UINT m{};c->OMGetBlendState(&b,f,&m);CHECK(b==blend.Get() && f[2]==.75f && m==0xfff);if(b) b->Release();
        }
        ComPtr<ID3D11ShaderResourceView> mask_srv;ComPtr<ID3D11RenderTargetView> mask_rtv;
        if(!d2vr::RigMask::target(depth.Get(),mask_srv,mask_rtv)) {std::printf("FAIL no rig target\n");++failures;return res;}
        ComPtr<ID3D11Resource> mask_res;mask_srv->GetResource(&mask_res);
        if(!take) {res.mask=read_back(dev.Get(),c.Get(),mask_res.Get(),8);res.colour=read_back(dev.Get(),c.Get(),colour[0].Get(),4);}
        c->ClearRenderTargetView(mask_rtv.Get(),clear); // FsrTaa clears the mask after its copy
        D3D11_TEXTURE2D_DESC td{};CHECK(d2vr::RigMask::target_desc(depth.Get(),td));
        CHECK(td.Format==DXGI_FORMAT_R32G32_FLOAT && td.ArraySize==2 && td.Width==N && ((td.BindFlags&D3D11_BIND_UNORDERED_ACCESS)!=0)==!legacy);
        td.BindFlags=D3D11_BIND_SHADER_RESOURCE|(td.BindFlags&D3D11_BIND_UNORDERED_ACCESS); // as FsrTaa makes it
        ComPtr<ID3D11Texture2D> out;CHECK(SUCCEEDED(dev->CreateTexture2D(&td,nullptr,&out)));
        std::uint64_t draws{};CHECK(d2vr::RigMask::take_target(c.Get(),depth.Get(),out.Get(),draws,0,0,take) && draws==4);
        res.slice[take]=read_back(dev.Get(),c.Get(),out.Get(),8);
    }
    if(info) {const UINT64 n=info->GetNumStoredMessages();
        for(UINT64 i=0;i<n;++i) {SIZE_T len=0;info->GetMessage(i,nullptr,&len);std::vector<char> buf(len);auto* m=reinterpret_cast<D3D11_MESSAGE*>(buf.data());
            if(SUCCEEDED(info->GetMessage(i,m,&len)) && m->Severity<=D3D11_MESSAGE_SEVERITY_ERROR) {if(res.errors++<4) std::printf("  debug layer: %.*s\n",static_cast<int>(m->DescriptionByteLength),m->pDescription);}}}
    d2vr::RigMask::reset();
    return res;
}
static float f_at(const std::vector<std::uint8_t>& v,UINT x,UINT y,unsigned k) {float f;std::memcpy(&f,&v[(y*N+x)*8+k*4],4);return f;}
static UINT px(float ndc) {return static_cast<UINT>((ndc+1)*.5f*N);}
static UINT py(float ndc) {return static_cast<UINT>((1-ndc)*.5f*N);}
int main() {
    std::setvbuf(stdout,nullptr,_IONBF,0);
    const bool debug=run(false,0,false,true).debug;std::printf("D3D11 debug layer: %s\n",debug?"on":"not installed (errors unchecked)");
    for(int cfg=0;cfg<4;++cfg) for(const bool d24:{false,true}) {
        const std::string before=d2vr::RigMask::json();
        const Result u=run(false,cfg,d24,debug);const std::string mid=d2vr::RigMask::json();const Result l=run(true,cfg,d24,debug);const std::string after=d2vr::RigMask::json();
        const bool mask_eq=u.mask==l.mask,s0=u.slice[0]==l.slice[0],s1=u.slice[1]==l.slice[1],col=u.colour==l.colour;
        unsigned mask_px=0,held_mask=0,s_px[2]{},s_held[2]{};
        for(UINT y=0;y<N;++y) for(UINT x=0;x<N;++x) {
            if(f_at(l.mask,x,y,0)>0) {++mask_px;held_mask+=f_at(l.mask,x,y,1)==2.f;}
            for(unsigned k=0;k<2;++k) if(f_at(l.slice[k],x,y,1)>0) {++s_px[k];s_held[k]+=f_at(l.slice[k],x,y,1)==253.f;}
        }
        const auto d=[&](const char* key) {return num(mid,key)-num(before,key);};
        const auto dl=[&](const char* key) {return num(after,key)-num(mid,key);};
        std::printf("cfg %d %s: mask %s (%u px, %u held), slice0 %s (%u px, %u held), slice1 %s (%u px, %u held), engine colour %s; uav replays %llu (legacy run %llu), fallbacks no_slot %llu engine_uavs %llu, conversions %llu; debug errors %u/%u\n",
            cfg,d24?"D24":"D32",mask_eq?"identical":"DIFFERENT",mask_px,held_mask,s0?"identical":"DIFFERENT",s_px[0],s_held[0],s1?"identical":"DIFFERENT",s_px[1],s_held[1],col?"identical":"DIFFERENT",
            d("\"uav_replays\":"),dl("\"uav_replays\":"),d("\"no_slot\":"),d("\"engine_uavs\":"),d("\"conversions\":"),u.errors,l.errors);
        if(!mask_eq) {unsigned n=0;for(UINT y=0;y<N;++y) for(UINT x=0;x<N;++x) if(std::memcmp(&u.mask[(y*N+x)*8],&l.mask[(y*N+x)*8],8)) {if(n++<4) std::printf("  mask %u,%u: uav (%.9g,%g) legacy (%.9g,%g)\n",x,y,f_at(u.mask,x,y,0),f_at(u.mask,x,y,1),f_at(l.mask,x,y,0),f_at(l.mask,x,y,1));}std::printf("  mask differs at %u px\n",n);}
        CHECK(mask_eq && s0 && s1 && col);CHECK(u.errors==0 && l.errors==0);
        CHECK(mask_px>200 && held_mask>50 && s_px[0]>200 && s_held[0]>50 && s_px[1]>200 && s_held[1]>50);
        CHECK(d("\"uav_replays\":")==8 && dl("\"uav_replays\":")==0 && d("\"conversions\":")==2 && dl("\"conversions\":")==0); // 4 rig/held draws x 2 takes
        CHECK(d("\"no_slot\":")==(cfg==1 || cfg==3?8u:0u) && d("\"engine_uavs\":")==(cfg==2?8u:0u));
        // spot pixels, in each slice's shifted space: held B over rig A (B nearer, flat 0.6); rig C over held D (D farther, replayed last)
        for(unsigned k=0;k<2;++k) {
            const float ox=offsets[k][0],oy=offsets[k][1];
            CHECK(f_at(u.slice[k],px(0.f+ox),py(.2f+oy),1)==253.f && std::abs(f_at(u.slice[k],px(0.f+ox),py(.2f+oy),0)-.6f)<1e-6f);
            CHECK(f_at(u.slice[k],px(.35f+ox),py(-.5f+oy),1)==255.f && std::abs(f_at(u.slice[k],px(.35f+ox),py(-.5f+oy),0)-.3f)<1e-6f);
            CHECK(f_at(u.slice[k],px(-.3f+ox),py(0.f+oy),1)==255.f && f_at(u.slice[k],px(-.3f+ox),py(0.f+oy),0)>.35f); // rig A over the later, farther rig C
        }
        // the mask (engine depth EQUAL at each replay): B's held class over A, C's rig class over the later D
        CHECK(f_at(u.mask,px(0),py(.2f),1)==2.f && f_at(u.mask,px(.35f),py(-.5f),1)==0.f && f_at(u.mask,px(.35f),py(-.5f),0)>.29f);
    }
    { // WARP and early depth: in a 2x2 quad where some pixels pass the replay's depth EQUAL and others fail it, WARP keeps the failing pixels'
      // UAV writes (they run as helpers; the D3D spec forbids external effects of an invocation that fails an early depth test, and real
      // GPUs mask helper lanes). With A's bottom edge inside a quad row, the later, farther rig C overwrites A's mask on that one pixel row
      // (the v1285 replay does not). Measured here, not required equal: every difference must be such a quad-mate write.
        const Result u=run(false,0,false,debug,false),l=run(true,0,false,debug,false);unsigned diff=0,leak=0;
        for(UINT y=0;y<N;++y) for(UINT x=0;x<N;++x) if(std::memcmp(&u.mask[(y*N+x)*8],&l.mask[(y*N+x)*8],8)) {++diff;bool mate=false;
            for(UINT qy=y&~1u;qy<(y&~1u)+2;++qy) for(UINT qx=x&~1u;qx<(x&~1u)+2;++qx) mate|=(qx!=x || qy!=y) && !std::memcmp(&l.mask[(qy*N+qx)*8],&u.mask[(y*N+x)*8],8);
            leak+=mate;}
        std::printf("mid-quad occlusion edge (WARP): mask differs at %u px, %u of them a quad-mate's write; slices %s\n",diff,leak,u.slice[0]==l.slice[0] && u.slice[1]==l.slice[1]?"identical":"DIFFERENT");
        CHECK(diff==leak && diff<=N && u.slice[0]==l.slice[0] && u.slice[1]==l.slice[1]);
    }
    std::printf("v1285 replay with an engine PS UAV bound: the UAV was lost %u times (state checks), the UAV replay restored it every time\n",legacy_uav_lost);
    if(failures) {std::printf("rig_replay_uav_check: %d failure(s)\n",failures);return 1;}
    std::printf("PASS: rig_replay_uav_check (UAV replay == v1285 replay: mask, both slices, engine targets untouched and restored, fallbacks counted)\n");
    return 0;
}
