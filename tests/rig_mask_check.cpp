// v1163 offline check of the rig mask (src/rig_mask.hpp) on WARP, through the real code: RigMask::Scope + after() replay a "skinned" draw
// whose vertex shader writes SV_Position as its SECOND output and whose pixel shader writes two targets; a later non-rig surface covers part
// of it; the real copy/compare shader (rig_mask_copy_source, eps from rig_mask_eps) then gives the mask. Checks: the mask is exactly the
// visible rig, the engine's targets/PS/depth state/blend are restored and its targets got nothing from the replay, and |rig - scene| on
// rig pixels (printed: the measured depth agreement). D24 (typeless, like the game) and D32.   Build/run: build\check-rig-mask.cmd
// v1179: the rig target is RG32F (depth, own-motion flag) and the mask 255 (own motion) / 254 (rig without). Modes: 0 a plain rig draw
// (254), 1 the prepass form (t0 previous wrapper, t1 current) after three ring rotations at the "skin copy" with rows_own (255; t0 holds
// the ring's P = the palette of the first rotation during the draw and the engine's wrapper again after it), 2 the same with rows_own
// false (the current palette at t0 during the draw, 254).
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include "../src/rig_mask.hpp"
#pragma comment(lib,"d3d11.lib")
#pragma comment(lib,"d3dcompiler.lib")
using Microsoft::WRL::ComPtr;
static int failures=0;
#define CHECK(x) do {if(!(x)) {std::printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#x);++failures;}} while(0)
constexpr UINT N=64;

template<class T> static ComPtr<ID3DBlob> compile(const char* src,const char* target) {
    ComPtr<ID3DBlob> code,errors;
    if(FAILED(D3DCompile(src,std::strlen(src),nullptr,nullptr,nullptr,"main",target,0,0,&code,&errors))) {
        std::printf("FAIL compile %s: %s\n",target,errors?static_cast<const char*>(errors->GetBufferPointer()):"");++failures;
    }
    return code;
}
static std::vector<std::uint8_t> read_back(ID3D11Device* d,ID3D11DeviceContext* c,ID3D11Texture2D* t,UINT bpp) {
    D3D11_TEXTURE2D_DESC desc{};t->GetDesc(&desc);desc.BindFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.MiscFlags=0;
    ComPtr<ID3D11Texture2D> s;std::vector<std::uint8_t> out(N*N*bpp);
    if(FAILED(d->CreateTexture2D(&desc,nullptr,&s))) {++failures;return out;}
    c->CopyResource(s.Get(),t);D3D11_MAPPED_SUBRESOURCE m{};
    if(FAILED(c->Map(s.Get(),0,D3D11_MAP_READ,0,&m))) {++failures;return out;}
    for(UINT y=0;y<N;++y) std::memcpy(&out[y*N*bpp],static_cast<const char*>(m.pData)+y*m.RowPitch,N*bpp);
    c->Unmap(s.Get(),0);return out;
}
static void run(bool d24,int mode) {
    ComPtr<ID3D11Device> dev;ComPtr<ID3D11DeviceContext> c;
    if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&dev,nullptr,&c))) {std::printf("FAIL WARP\n");++failures;return;}
    d2vr::RigMask::reset();std::setvbuf(stdout,nullptr,_IONBF,0);
    // scene depth (typeless, like the game's), two engine colour targets
    D3D11_TEXTURE2D_DESC t{};t.Width=t.Height=N;t.MipLevels=t.ArraySize=1;t.SampleDesc.Count=1;t.Format=d24?DXGI_FORMAT_R24G8_TYPELESS:DXGI_FORMAT_R32_TYPELESS;
    t.BindFlags=D3D11_BIND_DEPTH_STENCIL|D3D11_BIND_SHADER_RESOURCE;ComPtr<ID3D11Texture2D> depth;dev->CreateTexture2D(&t,nullptr,&depth);
    D3D11_DEPTH_STENCIL_VIEW_DESC dv{};dv.Format=d24?DXGI_FORMAT_D24_UNORM_S8_UINT:DXGI_FORMAT_D32_FLOAT;dv.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2D;
    ComPtr<ID3D11DepthStencilView> dsv;CHECK(SUCCEEDED(dev->CreateDepthStencilView(depth.Get(),&dv,&dsv)));
    const DXGI_FORMAT read_format=d24?DXGI_FORMAT_R24_UNORM_X8_TYPELESS:DXGI_FORMAT_R32_FLOAT;
    D3D11_SHADER_RESOURCE_VIEW_DESC sv{};sv.Format=read_format;sv.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;sv.Texture2D.MipLevels=1;
    ComPtr<ID3D11ShaderResourceView> depth_srv;CHECK(SUCCEEDED(dev->CreateShaderResourceView(depth.Get(),&sv,&depth_srv)));
    D3D11_TEXTURE2D_DESC ct=t;ct.Format=DXGI_FORMAT_R8G8B8A8_UNORM;ct.BindFlags=D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> colour[2];ComPtr<ID3D11RenderTargetView> rtv[2];
    for(int i=0;i<2;++i) {dev->CreateTexture2D(&ct,nullptr,&colour[i]);dev->CreateRenderTargetView(colour[i].Get(),nullptr,&rtv[i]);}
    // "skin palettes": the rig's and another model's, bound at VS t0
    const float pal[4]{0,0,0,0};D3D11_BUFFER_DESC bd{};bd.ByteWidth=16;bd.Usage=D3D11_USAGE_DEFAULT;bd.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA init{pal,0,0};ComPtr<ID3D11Buffer> rig_pal,other_pal;dev->CreateBuffer(&bd,&init,&rig_pal);dev->CreateBuffer(&bd,&init,&other_pal);
    D3D11_SHADER_RESOURCE_VIEW_DESC bv{};bv.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;bv.ViewDimension=D3D11_SRV_DIMENSION_BUFFER;bv.Buffer.NumElements=1;
    ComPtr<ID3D11ShaderResourceView> rig_view,other_view;dev->CreateShaderResourceView(rig_pal.Get(),&bv,&rig_view);dev->CreateShaderResourceView(other_pal.Get(),&bv,&other_view);
    ComPtr<ID3D11Buffer> rig_prev;dev->CreateBuffer(&bd,&init,&rig_prev);ComPtr<ID3D11ShaderResourceView> prev_view;dev->CreateShaderResourceView(rig_prev.Get(),&bv,&prev_view);
    d2vr::RigMask::publish({reinterpret_cast<std::uintptr_t>(rig_pal.Get()),reinterpret_cast<std::uintptr_t>(rig_prev.Get()),0,0,0,0});
    const bool ring=mode==1 || mode==2; // v1183: mode 3 = a held rigid item (no rig palette, marked held: 253)
    d2vr::RigMask::motion=ring;d2vr::RigMask::begin_epoch();
    // v1211: the ring is created by the engine's skin copy (skin_copied) but its slots are written only at the rig draws (capture_drawn), from
    // the current palette each render's draw binds at t1 (seeded below once the pass state is bound)
    if(ring) {const float junk[4]{99,0,0,0};d2vr::RigMask::skin_copied(c.Get(),0,rig_pal.Get(),1,junk,16,true);}
    if(ring) {const float v[4]{0,0,0,0};c->UpdateSubresource(rig_pal.Get(),0,nullptr,v,0,0);} // this render's current palette
    // the engine's shaders: SV_Position after a TEXCOORD output; depth sloped across x; the PS writes two targets
    static const char vs_src[]="Buffer<float4> skin:register(t0);cbuffer C:register(b0){float4 r;};"
        "struct O{float4 c:TEXCOORD0;float4 p:SV_Position;};"
        "O main(uint i:SV_VertexID){const float2 q[6]={float2(0,0),float2(1,0),float2(0,1),float2(1,0),float2(1,1),float2(0,1)};"
        "const float2 u=q[i];O o;const float x=lerp(r.x,r.z,u.x),y=lerp(r.y,r.w,u.y);"
        "o.p=float4(x,y,0.5+0.17*x+0.05*y,1)*1.7+skin[0]*0;o.c=float4(u,0,1);return o;}"; // w = 1.7: depth 0.5 + 0.17 x + 0.05 y (t0 read, its value unused: the replay sees the engine's t0 again)
    static const char vs_world[]="cbuffer C:register(b0){float4 r;float4 z;};"
        "struct O{float4 c:TEXCOORD0;float4 p:SV_Position;};"
        "O main(uint i:SV_VertexID){const float2 q[6]={float2(0,0),float2(1,0),float2(0,1),float2(1,0),float2(1,1),float2(0,1)};"
        "O o;const float2 u=q[i];o.p=float4(lerp(r.x,r.z,u.x),lerp(r.y,r.w,u.y),z.x,1);o.c=float4(u,0,1);return o;}";
    static const char ps_src[]="struct O{float4 a:SV_Target0;float4 b:SV_Target1;};O main(float4 c:TEXCOORD0,float4 p:SV_Position){O o;o.a=c;o.b=float4(1,0,1,1);return o;}";
    const auto vsb=compile<int>(vs_src,"vs_5_0"),vwb=compile<int>(vs_world,"vs_5_0"),psb=compile<int>(ps_src,"ps_5_0");
    if(!vsb || !vwb || !psb) return;
    ComPtr<ID3D11VertexShader> vs,vw;ComPtr<ID3D11PixelShader> ps;
    dev->CreateVertexShader(vsb->GetBufferPointer(),vsb->GetBufferSize(),nullptr,&vs);dev->CreateVertexShader(vwb->GetBufferPointer(),vwb->GetBufferSize(),nullptr,&vw);
    dev->CreatePixelShader(psb->GetBufferPointer(),psb->GetBufferSize(),nullptr,&ps);
    D3D11_BUFFER_DESC cbd{};cbd.ByteWidth=32;cbd.Usage=D3D11_USAGE_DEFAULT;cbd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;ComPtr<ID3D11Buffer> cb;dev->CreateBuffer(&cbd,nullptr,&cb);
    D3D11_DEPTH_STENCIL_DESC ds{};ds.DepthEnable=TRUE;ds.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;ds.DepthFunc=D3D11_COMPARISON_GREATER;
    ComPtr<ID3D11DepthStencilState> prepass;dev->CreateDepthStencilState(&ds,&prepass);
    D3D11_BLEND_DESC bl{};bl.RenderTarget[0].RenderTargetWriteMask=0x7;ComPtr<ID3D11BlendState> blend;dev->CreateBlendState(&bl,&blend);
    D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;ComPtr<ID3D11RasterizerState> rs;dev->CreateRasterizerState(&rd,&rs);
    const D3D11_VIEWPORT vp{0,0,float(N),float(N),0,1};c->RSSetViewports(1,&vp);c->RSSetState(rs.Get());
    ID3D11RenderTargetView* engine_rtvs[2]{rtv[0].Get(),rtv[1].Get()};c->OMSetRenderTargets(2,engine_rtvs,dsv.Get());
    c->OMSetDepthStencilState(prepass.Get(),3);const float bf[4]{.25f,.5f,.75f,1};c->OMSetBlendState(blend.Get(),bf,0xfff);
    c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);c->PSSetShader(ps.Get(),nullptr,0);c->VSSetConstantBuffers(0,1,cb.GetAddressOf());
    c->ClearDepthStencilView(dsv.Get(),D3D11_CLEAR_DEPTH|D3D11_CLEAR_STENCIL,0.f,0);
    const float clear[4]{0,0,0,0};c->ClearRenderTargetView(rtv[0].Get(),clear);c->ClearRenderTargetView(rtv[1].Get(),clear);
    d2vr::RigMask::enabled=true;d2vr::RigMask::view=1;d2vr::RigMask::take_view_draws();
    if(ring) for(std::uint32_t f=1;f<=7;++f) { // renders 1..7: each rig draw binds its current palette x = f/1000 at t1
        // v1211 case: at render 6 the skin copy hook sees an upload the image skin did not commit (CPU palette 99, v1210 tagged it as frame 6),
        // while render 6's draw binds x = 0.006: the slot must hold what the draw read, never the uncommitted upload
        const float v[4]{f*1e-3f,0,0,0},junk[4]{99,0,0,0};
        if(f==6) d2vr::RigMask::skin_copied(c.Get(),0,rig_pal.Get(),f,junk,16,true,nullptr,false);
        c->UpdateSubresource(rig_pal.Get(),0,nullptr,v,0,0);
        ID3D11ShaderResourceView* pair[2]{prev_view.Get(),rig_view.Get()};c->VSSetShaderResources(0,2,pair);
        d2vr::RigMask::Scope scope(c.Get(),1);CHECK(d2vr::RigMask::draw_is_rig());d2vr::RigMask::capture_drawn(c.Get(),f);
        if(f==6) {const float later[4]{77,0,0,0};c->UpdateSubresource(rig_pal.Get(),0,nullptr,later,0,0);} // a later upload: the copy was recorded at the draw
    }
    if(ring) {const float v[4]{0,0,0,0};c->UpdateSubresource(rig_pal.Get(),0,nullptr,v,0,0);d2vr::RigMask::take_view_draws();} // render 8's current palette
    const auto draw=[](ID3D11DeviceContext* x,const d2vr::RigMask::DrawArgs& a) noexcept {x->Draw(a.count,a.start);};
    const auto targets=[](ID3D11DeviceContext* x,UINT n,ID3D11RenderTargetView* const* r,ID3D11DepthStencilView* d) noexcept {x->OMSetRenderTargets(n,r,d);};
    float bound_x=-1;bool restored=true;
    const auto engine_draw=[&](ID3D11VertexShader* v,ID3D11ShaderResourceView* skin,const float* k,bool rig=false) {
        c->UpdateSubresource(cb.Get(),0,nullptr,k,0,0);c->VSSetShader(v,nullptr,0);
        ID3D11ShaderResourceView* pair[2]{rig && ring?prev_view.Get():skin,rig && ring?rig_view.Get():nullptr};c->VSSetShaderResources(0,2,pair);
        d2vr::RigMask::Scope scope(c.Get(),1);
        if(rig && mode==3) {CHECK(!d2vr::RigMask::draw_is_rig());d2vr::RigMask::mark_held();}
        if(rig && ring) {
            CHECK(d2vr::RigMask::draw_is_rig());
            // v1206: the tagged ring. This draw is render 8's; its receipt's previous render is 6. The NEXT render's skin copy (9) is issued
            // on the context before this draw executes (a late draw list): slot 6 is untouched, the draw still reads frame 6's palette. A frame
            // whose slot was reused (1, now 9) or an unknown render gets no palette.
            if(mode==1) {
                d2vr::RigMask::capture_drawn(c.Get(),9); // render 9's draw (late list order): slot 1 reused, slot 6 untouched
                CHECK(!d2vr::RigMask::previous_palette(c.Get(),0,1,true));CHECK(!d2vr::RigMask::previous_palette(c.Get(),0,~0u,true));
                CHECK(!d2vr::RigMask::previous_palette(c.Get(),0,8,true)); // render 8's own slot is not written by previous_palette
                CHECK(d2vr::RigMask::previous_palette(c.Get(),0,3,true));
                // v1212: the slot is taken only with its capturing draw's rows; v1213: a VR-pose switch between the two renders is zero motion
                CHECK(!d2vr::RigMask::previous_palette(c.Get(),0,3,true,0x1234));
                {const float junk[4]{99,0,0,0};d2vr::RigMask::skin_copied(c.Get(),0,rig_pal.Get(),10,junk,16,true,nullptr,true,false,1,false);}
                d2vr::RigMask::capture_drawn(c.Get(),10); // render 10 draws the engine pose (slot 2)
                CHECK(!d2vr::RigMask::previous_palette(c.Get(),0,3,true,0,10));CHECK(d2vr::RigMask::previous_palette(c.Get(),0,3,true,0,~0u));
                {const float junk[4]{99,0,0,0};d2vr::RigMask::skin_copied(c.Get(),0,rig_pal.Get(),11,junk,16,true);}
            }
            const bool own=d2vr::RigMask::previous_palette(c.Get(),0,6,mode==1);CHECK(own==(mode==1));
            { // v1210: a draw hook entered from inside this one (the same draw) is inert: the outer's state and its t0 binding survive it
                d2vr::RigMask::Scope inner(c.Get(),1);CHECK(d2vr::RigMask::nested_draw());CHECK(d2vr::RigMask::draw_is_rig());
                inner.after({2,3,1,0,0,0},draw,targets);
            }
            CHECK(!d2vr::RigMask::nested_draw() && d2vr::RigMask::draw_is_rig());
            ID3D11ShaderResourceView* t0{};c->VSGetShaderResources(0,1,&t0); // what the draw reads as its previous palette
            if(t0) {ComPtr<ID3D11Resource> r;t0->GetResource(&r);ComPtr<ID3D11Buffer> b;r.As(&b);
                D3D11_BUFFER_DESC sd{};b->GetDesc(&sd);sd.Usage=D3D11_USAGE_STAGING;sd.BindFlags=0;sd.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Buffer> st;
                if(SUCCEEDED(dev->CreateBuffer(&sd,nullptr,&st))) {c->CopyResource(st.Get(),b.Get());D3D11_MAPPED_SUBRESOURCE mm{};
                    if(SUCCEEDED(c->Map(st.Get(),0,D3D11_MAP_READ,0,&mm))) {bound_x=static_cast<const float*>(mm.pData)[0];c->Unmap(st.Get(),0);}}
                t0->Release();}
        }
        c->Draw(6,0);scope.after({2,6,1,0,0,0},draw,targets);
        if(rig && ring) {ID3D11ShaderResourceView* t0{};c->VSGetShaderResources(0,1,&t0);restored=t0==prev_view.Get();if(t0) t0->Release();}
    };
    // world far everywhere; the rig (rect x -0.8..0.4, y -0.6..0.6 NDC, at 1.7x clip scale); a nearer world surface over x > 0
    const float far_world[8]{-1,-1,1,1,.1f,0,0,0},rig_rect[8]{-.8f,-.6f,.4f,.6f,0,0,0,0},near_world[8]{0,-1,1,1,.9f,0,0,0};
    engine_draw(vw.Get(),other_view.Get(),far_world);
    engine_draw(vs.Get(),mode==3?other_view.Get():rig_view.Get(),rig_rect,true);
    if(ring) {CHECK(restored);CHECK(bound_x==(mode==1?6e-3f:0.f));std::printf("mode %d: t0 during the draw holds x=%g (ring P = frame 6: 0.006; current: 0)\n",mode,bound_x);}
    { // the engine's state after the replay is its own
        ID3D11RenderTargetView* r[8]{};ID3D11DepthStencilView* d{};c->OMGetRenderTargets(8,r,&d);
        CHECK(r[0]==rtv[0].Get() && r[1]==rtv[1].Get() && !r[2] && d==dsv.Get());
        for(auto* x:r) if(x) x->Release();if(d) d->Release();
        ID3D11PixelShader* p{};c->PSGetShader(&p,nullptr,nullptr);CHECK(p==ps.Get());if(p) p->Release();
        ID3D11DepthStencilState* s{};UINT ref{};c->OMGetDepthStencilState(&s,&ref);CHECK(s==prepass.Get() && ref==3);if(s) s->Release();
        ID3D11BlendState* b{};float f[4]{};UINT m{};c->OMGetBlendState(&b,f,&m);CHECK(b==blend.Get() && f[1]==.5f && m==0xfff);if(b) b->Release();
    }
    engine_draw(vw.Get(),other_view.Get(),near_world);
    // the copy/compare
    ComPtr<ID3D11ShaderResourceView> rig_srv;ComPtr<ID3D11RenderTargetView> rig_rtv;
    CHECK(d2vr::RigMask::take_view_draws()==3);
    if(!d2vr::RigMask::target(depth.Get(),rig_srv,rig_rtv)) {std::printf("FAIL no rig target\n");++failures;return;}
    D3D11_TEXTURE2D_DESC mt=t;mt.Format=DXGI_FORMAT_R8_TYPELESS;mt.BindFlags=D3D11_BIND_UNORDERED_ACCESS|D3D11_BIND_SHADER_RESOURCE;ComPtr<ID3D11Texture2D> mask;dev->CreateTexture2D(&mt,nullptr,&mask);
    D3D11_UNORDERED_ACCESS_VIEW_DESC mu{};mu.Format=DXGI_FORMAT_R8_UINT;mu.ViewDimension=D3D11_UAV_DIMENSION_TEXTURE2D;ComPtr<ID3D11UnorderedAccessView> mask_uav;dev->CreateUnorderedAccessView(mask.Get(),&mu,&mask_uav);
    D3D11_BUFFER_DESC nb{};nb.ByteWidth=16;nb.BindFlags=D3D11_BIND_UNORDERED_ACCESS;nb.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;ComPtr<ID3D11Buffer> counts;dev->CreateBuffer(&nb,nullptr,&counts);
    D3D11_UNORDERED_ACCESS_VIEW_DESC nu{};nu.Format=DXGI_FORMAT_R32_TYPELESS;nu.ViewDimension=D3D11_UAV_DIMENSION_BUFFER;nu.Buffer.NumElements=4;nu.Buffer.Flags=D3D11_BUFFER_UAV_FLAG_RAW;
    ComPtr<ID3D11UnorderedAccessView> counts_uav;dev->CreateUnorderedAccessView(counts.Get(),&nu,&counts_uav);
    const UINT zero[4]{};c->ClearUnorderedAccessViewUint(counts_uav.Get(),zero);
    const auto csb=compile<int>(d2vr::rig_mask_copy_source,"cs_5_0");if(!csb) return;
    ComPtr<ID3D11ComputeShader> cs;dev->CreateComputeShader(csb->GetBufferPointer(),csb->GetBufferSize(),nullptr,&cs);
    float k[4]{0,0,float(N),float(N)};d2vr::rig_mask_eps(read_format,k[0],k[1]);
    D3D11_BUFFER_DESC kb{};kb.ByteWidth=16;kb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;D3D11_SUBRESOURCE_DATA kd{k,0,0};ComPtr<ID3D11Buffer> kbuf;dev->CreateBuffer(&kb,&kd,&kbuf);
    c->OMSetRenderTargets(0,nullptr,nullptr); // the depth is read now
    // v1209: the TAA's motion input (UV): wild (20 = 20 render widths) left of x = N/4, a real small motion (0.001) elsewhere
    std::vector<float> mvi(N*N*2);for(UINT y=0;y<N;++y) for(UINT x=0;x<N;++x) mvi[(y*N+x)*2]=x<N/4?20.f:x<N/2?.6f:.001f; // v1213: 0.6 widths = a fast swing, true motion (not wild)
    D3D11_TEXTURE2D_DESC mvd=t;mvd.Format=DXGI_FORMAT_R32G32_FLOAT;mvd.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;D3D11_SUBRESOURCE_DATA mvs{mvi.data(),N*8,0};
    ComPtr<ID3D11Texture2D> mvt;dev->CreateTexture2D(&mvd,&mvs,&mvt);ComPtr<ID3D11ShaderResourceView> mv_srv;dev->CreateShaderResourceView(mvt.Get(),nullptr,&mv_srv);
    ID3D11ShaderResourceView* in[3]{depth_srv.Get(),rig_srv.Get(),mv_srv.Get()};ID3D11UnorderedAccessView* outs[2]{mask_uav.Get(),counts_uav.Get()};
    c->CSSetShader(cs.Get(),nullptr,0);c->CSSetShaderResources(0,3,in);c->CSSetUnorderedAccessViews(0,2,outs,nullptr);c->CSSetConstantBuffers(0,1,kbuf.GetAddressOf());
    c->Dispatch(N/8,N/8,1);
    ID3D11ShaderResourceView* none[3]{};ID3D11UnorderedAccessView* no[2]{};c->CSSetShaderResources(0,3,none);c->CSSetUnorderedAccessViews(0,2,no,nullptr);
    D3D11_SHADER_RESOURCE_VIEW_DESC msd{};msd.Format=DXGI_FORMAT_R8_UINT;msd.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;msd.Texture2D.MipLevels=1;
    ComPtr<ID3D11ShaderResourceView> mask_srv;dev->CreateShaderResourceView(mask.Get(),&msd,&mask_srv);
    const bool guarded=d2vr::RigMask::guard(c.Get(),mvt.Get(),mask_srv.Get(),N,N);CHECK(guarded);
    const auto mvo=read_back(dev.Get(),c.Get(),mvt.Get(),8);
    const auto m=read_back(dev.Get(),c.Get(),mask.Get(),1),zb=read_back(dev.Get(),c.Get(),depth.Get(),4),c1=read_back(dev.Get(),c.Get(),colour[1].Get(),4);
    ComPtr<ID3D11Resource> rig_res;rig_srv->GetResource(&rig_res);ComPtr<ID3D11Texture2D> rig_tex;rig_res.As(&rig_tex);
    const auto rb=read_back(dev.Get(),c.Get(),rig_tex.Get(),8);
    // expected rig pixels: centres inside the rig rect (pixel x from -0.8..0.4 NDC, y -0.6..0.6) and x < 0 NDC (not under the near surface)
    unsigned wrong=0,rig_px=0,mv_bad=0;double worst=0,worst_steps=0;
    for(UINT y=0;y<N;++y) for(UINT x=0;x<N;++x) {
        const double nx=(x+.5)/N*2-1,ny=1-(y+.5)/N*2;
        const bool inside=nx>-.8 && nx<.4 && ny>-.6 && ny<.6,want=inside && nx<0;
        // v1209: a rig (not held) pixel with a wild vector is 252 and its vector is zero after the guard; every other vector is untouched
        const bool wild_rig=mode!=3 && m[y*N+x]==252;float mvx;std::memcpy(&mvx,&mvo[(y*N+x)*8],4);
        if(wild_rig) {CHECK(x<N/4);CHECK(mvx==0.f);} else CHECK(mvx==(x<N/4?20.f:x<N/2?.6f:.001f));
        const bool got=m[y*N+x]>=252;CHECK(m[y*N+x]==0 || wild_rig || m[y*N+x]==(mode==1?255:mode==3?253:254));
        if(got && mode!=3 && x<N/4) CHECK(wild_rig);
        if(got!=want && std::abs(nx+.8)>.04 && std::abs(nx-.4)>.04 && std::abs(nx)>.04 && std::abs(std::abs(ny)-.6)>.04) ++wrong; // edges: rasterization rule
        if(got) {
            ++rig_px;std::uint32_t raw;std::memcpy(&raw,&zb[(y*N+x)*4],4);float rv;std::memcpy(&rv,&rb[(y*N+x)*8],4);
            float zv;if(d24) zv=float(raw&0xffffff)/16777215.f;else std::memcpy(&zv,&raw,4);
            worst=std::max(worst,double(std::abs(rv-zv)));worst_steps=d24?worst*16777215.:worst/(std::abs(zv)*1.1920929e-7);
        }
        std::uint32_t mv;std::memcpy(&mv,&c1[(y*N+x)*4],4);if(inside && nx<0 && mv!=0x00ff00ffu) {if(!mv_bad++) std::printf("second target %08x at %u,%u%s",mv,x,y,"\n");} // the engine's second target: its own draw only
    }
    CHECK(wrong==0);CHECK(rig_px>300);CHECK(mv_bad==0);std::fflush(stdout);
    std::printf("%s: rig pixels %u, max |rig - scene| %.3g (%.2f %s)\n",d24?"D24":"D32",rig_px,worst,worst_steps,d24?"D24 steps":"ulp");
}
int main() {
    for(int mode=0;mode<4;++mode) {run(true,mode);run(false,mode);}
    if(failures) {std::printf("rig_mask_check: %d failure(s)\n",failures);return 1;}
    std::printf("PASS: rig_mask_check (replay through RigMask::Scope, SV_Position-only PS on a VS with SV_Position second, engine state restored, copy/compare)\n");
    return 0;
}
