// v1190 check of the rig target (src/rig_target.hpp math, src/rig_mask.hpp replay) on WARP.
//  1. Math: two cameras 6.4 cm apart, canted 5 degrees apart, asymmetric frusta, the fresh one jittered; the view matrices are built with an
//     engine-like axis map C (view = C * inverse(world_from_camera)) that retarget() never sees. E * VP_f must equal P_t * V_t (no jitter).
//  2. WARP: a "rig" box drawn for eye A through RigMask::Scope (A's rows, jittered), retargeted by the replay with E * rows into the rig
//     target; the same box drawn directly with B's rows into its own depth. Coverage XOR <= 0.1 % of covered pixels and depth within 1 mm.
//     Then a "held" draw (not a rig palette, marked held after placement-style rows) lands in the target with class 253.
//   Build/run: build\check-rig-target.cmd
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
#include "../src/rig_target.hpp"
#pragma comment(lib,"d3d11.lib")
#pragma comment(lib,"d3dcompiler.lib")
using Microsoft::WRL::ComPtr;using d2vr::rig_target::M4;
static int failures=0;
#define CHECK(x) do {if(!(x)) {std::printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#x);++failures;}} while(0)
static d2vr::WorldTrackedPose pose(double x,double y,double z,double yaw) { // world: x forward, y left, z up (game axes); yaw about z
    const double c=std::cos(yaw),s=std::sin(yaw);
    d2vr::WorldTrackedPose p{};p.position={x,y,z};p.basis[0]={c,s,0};p.basis[1]={-s,c,0};p.basis[2]={0,0,1};return p;
}
static const M4 C{0,-1,0,0, 0,0,1,0, -1,0,0,0, 0,0,0,1}; // local (forward, left, up) -> view (right, up, back)
static M4 view_of(const d2vr::WorldTrackedPose& w) {M4 inv{};d2vr::rig_target::inverse(d2vr::rig_target::world_from(w),inv);return d2vr::rig_target::mul(C,inv);}
static const double p10=1.4e-6,p11=0.01; // the engine's reverse-Z depth terms
static M4 proj(double l,double r,double u,double d,double jx,double jy) {auto p=d2vr::rig_target::projection(l,r,u,d,p10,p11);p[2]+=2*jx;p[6]+=2*jy;return p;}
constexpr UINT N=128;
int main() {
    // cameras: fresh A (left eye), target B (right eye) 6.4 cm to its right, canted 5 degrees outward each
    const double cant=5*3.14159265358979/180;
    const auto wa=pose(0,0.032,1.6,cant),wb=pose(0,-0.032,1.6,-cant);
    const double al_=std::tan(-0.9),ar=std::tan(0.7),au=std::tan(0.8),ad=std::tan(-0.85),bl=std::tan(-0.7),br=std::tan(0.9),bu=std::tan(0.8),bd=std::tan(-0.85);
    const M4 va=view_of(wa),vb=view_of(wb),pa=proj(al_,ar,au,ad,0.3/N,-0.2/N),pb=proj(bl,br,bu,bd,0,0);
    const M4 vpa=d2vr::rig_target::mul(pa,va),vpb=d2vr::rig_target::mul(pb,vb);
    M4 e{};CHECK(d2vr::rig_target::retarget(vpa,va,pa,wa,wb,bl,br,bu,bd,e));
    const M4 got=d2vr::rig_target::mul(e,vpa);double worst=0;
    for(int i=0;i<16;++i) worst=std::max(worst,std::abs(got[i]-vpb[i]));
    std::printf("math: max |E*VP_f - VP_t| %.3g\n",worst);CHECK(worst<1e-9);
    // WARP
    ComPtr<ID3D11Device> dev;ComPtr<ID3D11DeviceContext> c;
    if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&dev,nullptr,&c))) {std::printf("FAIL WARP\n");return 1;}
    D3D11_TEXTURE2D_DESC t{};t.Width=t.Height=N;t.MipLevels=t.ArraySize=1;t.SampleDesc.Count=1;t.Format=DXGI_FORMAT_R32_TYPELESS;t.BindFlags=D3D11_BIND_DEPTH_STENCIL|D3D11_BIND_SHADER_RESOURCE;
    ComPtr<ID3D11Texture2D> depth_a,depth_b;dev->CreateTexture2D(&t,nullptr,&depth_a);dev->CreateTexture2D(&t,nullptr,&depth_b);
    D3D11_DEPTH_STENCIL_VIEW_DESC dv{};dv.Format=DXGI_FORMAT_D32_FLOAT;dv.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2D;
    ComPtr<ID3D11DepthStencilView> dsv_a,dsv_b;dev->CreateDepthStencilView(depth_a.Get(),&dv,&dsv_a);dev->CreateDepthStencilView(depth_b.Get(),&dv,&dsv_b);
    D3D11_TEXTURE2D_DESC ct=t;ct.Format=DXGI_FORMAT_R8G8B8A8_UNORM;ct.BindFlags=D3D11_BIND_RENDER_TARGET;ComPtr<ID3D11Texture2D> colour;ComPtr<ID3D11RenderTargetView> rtv;
    dev->CreateTexture2D(&ct,nullptr,&colour);dev->CreateRenderTargetView(colour.Get(),nullptr,&rtv);
    // a box 0.5 m ahead: 0.2 x 0.12 x 0.15 m (an arm-sized thing), as 12 triangles from SV_VertexID
    static const char vs_src[]="Buffer<float4> skin:register(t0);cbuffer C:register(b0){float4 r[4];};"
        "float4 main(uint i:SV_VertexID):SV_Position{const uint f[36]={0,1,2,2,1,3,4,6,5,5,6,7,0,4,1,1,4,5,2,3,6,6,3,7,0,2,4,4,2,6,1,5,3,3,5,7};"
        "const uint k=f[i];const float4 p=float4((k&1)?0.6:0.4,(k&2)?0.06:-0.06,(k&4)?1.68:1.53,1)+skin[0]*0;"
        "return float4(dot(r[0],p),dot(r[1],p),dot(r[2],p),dot(r[3],p));}";
    static const char ps_src[]="float4 main(float4 p:SV_Position):SV_Target{return float4(1,1,1,1);}";
    ComPtr<ID3DBlob> vs_code,ps_code;
    if(FAILED(D3DCompile(vs_src,sizeof vs_src-1,nullptr,nullptr,nullptr,"main","vs_5_0",0,0,&vs_code,nullptr)) || FAILED(D3DCompile(ps_src,sizeof ps_src-1,nullptr,nullptr,nullptr,"main","ps_5_0",0,0,&ps_code,nullptr))) {std::printf("FAIL compile\n");return 1;}
    ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;dev->CreateVertexShader(vs_code->GetBufferPointer(),vs_code->GetBufferSize(),nullptr,&vs);dev->CreatePixelShader(ps_code->GetBufferPointer(),ps_code->GetBufferSize(),nullptr,&ps);
    D3D11_BUFFER_DESC cbd{};cbd.ByteWidth=64;cbd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;ComPtr<ID3D11Buffer> cb;dev->CreateBuffer(&cbd,nullptr,&cb);
    const float pal[4]{};D3D11_BUFFER_DESC pd{};pd.ByteWidth=16;pd.BindFlags=D3D11_BIND_SHADER_RESOURCE;D3D11_SUBRESOURCE_DATA init{pal,0,0};
    ComPtr<ID3D11Buffer> rig_pal,other_pal;dev->CreateBuffer(&pd,&init,&rig_pal);dev->CreateBuffer(&pd,&init,&other_pal);
    D3D11_SHADER_RESOURCE_VIEW_DESC bv{};bv.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;bv.ViewDimension=D3D11_SRV_DIMENSION_BUFFER;bv.Buffer.NumElements=1;
    ComPtr<ID3D11ShaderResourceView> rig_view,other_view;dev->CreateShaderResourceView(rig_pal.Get(),&bv,&rig_view);dev->CreateShaderResourceView(other_pal.Get(),&bv,&other_view);
    d2vr::RigMask::reset();d2vr::RigMask::publish({reinterpret_cast<std::uintptr_t>(rig_pal.Get()),0,0,0,0,0});
    D3D11_DEPTH_STENCIL_DESC ds{};ds.DepthEnable=TRUE;ds.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;ds.DepthFunc=D3D11_COMPARISON_GREATER;ComPtr<ID3D11DepthStencilState> prepass;dev->CreateDepthStencilState(&ds,&prepass);
    D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;ComPtr<ID3D11RasterizerState> rs;dev->CreateRasterizerState(&rd,&rs);
    const D3D11_VIEWPORT vp{0,0,float(N),float(N),0,1};c->RSSetViewports(1,&vp);c->RSSetState(rs.Get());c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    c->VSSetShader(vs.Get(),nullptr,0);c->PSSetShader(ps.Get(),nullptr,0);c->VSSetConstantBuffers(0,1,cb.GetAddressOf());c->OMSetDepthStencilState(prepass.Get(),0);
    const auto rows_of=[](const M4& m) {std::array<float,16> r{};for(int i=0;i<16;++i) r[static_cast<std::size_t>(i)]=static_cast<float>(m[static_cast<std::size_t>(i)]);return r;};
    static std::array<float,16> drawn_rows{},target_rows{};static ID3D11DeviceContext* ctx{};static ID3D11Buffer* rows_cb{};
    ctx=c.Get();rows_cb=cb.Get();
    d2vr::RigMask::target_on=true;d2vr::RigMask::enabled=true;d2vr::RigMask::view=1;
    d2vr::RigMask::target_bind=+[](ID3D11DeviceContext* x) noexcept {x->UpdateSubresource(rows_cb,0,nullptr,target_rows.data(),0,0);return true;};
    d2vr::RigMask::target_restore=+[](ID3D11DeviceContext* x) noexcept {x->UpdateSubresource(rows_cb,0,nullptr,drawn_rows.data(),0,0);};
    const auto draw=[](ID3D11DeviceContext* x,const d2vr::RigMask::DrawArgs& a) noexcept {x->Draw(a.count,a.start);};
    const auto targets=[](ID3D11DeviceContext* x,UINT n,ID3D11RenderTargetView* const* r,ID3D11DepthStencilView* d) noexcept {x->OMSetRenderTargets(n,r,d);};
    const float zero[4]{};
    // 2a. eye A's draw (jittered rows), replayed into the target with E * rows
    drawn_rows=rows_of(vpa);d2vr::rig_target::apply(e,drawn_rows.data(),target_rows.data());
    c->ClearDepthStencilView(dsv_a.Get(),D3D11_CLEAR_DEPTH,0.f,0);c->ClearRenderTargetView(rtv.Get(),zero);c->OMSetRenderTargets(1,rtv.GetAddressOf(),dsv_a.Get());
    c->UpdateSubresource(cb.Get(),0,nullptr,drawn_rows.data(),0,0);
    {ID3D11ShaderResourceView* s=rig_view.Get();c->VSSetShaderResources(0,1,&s);
     d2vr::RigMask::Scope scope(c.Get(),1);CHECK(d2vr::RigMask::draw_is_rig());d2vr::RigMask::mark_target(true);c->Draw(36,0);scope.after({2,36,1,0,0,0},draw,targets);}
    // 2b. the truth: B's own render of the box
    c->ClearDepthStencilView(dsv_b.Get(),D3D11_CLEAR_DEPTH,0.f,0);c->OMSetRenderTargets(1,rtv.GetAddressOf(),dsv_b.Get());
    const auto rows_b=rows_of(vpb);c->UpdateSubresource(cb.Get(),0,nullptr,rows_b.data(),0,0);c->Draw(36,0);c->OMSetRenderTargets(0,nullptr,nullptr);
    D3D11_TEXTURE2D_DESC td{};CHECK(d2vr::RigMask::target_desc(depth_a.Get(),td));
    td.BindFlags=0;td.Usage=D3D11_USAGE_STAGING;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Texture2D> out;dev->CreateTexture2D(&td,nullptr,&out);
    std::uint64_t draws{};const bool valid=d2vr::RigMask::take_target(c.Get(),depth_a.Get(),out.Get(),draws);CHECK(valid && draws==1);
    D3D11_TEXTURE2D_DESC sd=t;sd.BindFlags=0;sd.Usage=D3D11_USAGE_STAGING;sd.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Texture2D> truth;dev->CreateTexture2D(&sd,nullptr,&truth);c->CopyResource(truth.Get(),depth_b.Get());
    D3D11_MAPPED_SUBRESOURCE mo{},mt{};CHECK(SUCCEEDED(c->Map(out.Get(),0,D3D11_MAP_READ,0,&mo)));CHECK(SUCCEEDED(c->Map(truth.Get(),0,D3D11_MAP_READ,0,&mt)));
    unsigned covered=0,xor_px=0;double worst_mm=0;
    const auto metres=[](float z) {return p11/(z+p10);}; // reverse-Z: distance ahead
    for(UINT y=0;y<N;++y) for(UINT x=0;x<N;++x) {
        const float* g=static_cast<const float*>(mo.pData)+y*(mo.RowPitch/4)+x*2;const float b=static_cast<const float*>(mt.pData)[y*(mt.RowPitch/4)+x];
        const bool in_g=g[1]>0,in_b=b>0;covered+=in_b;if(in_g!=in_b) ++xor_px;
        if(in_g && in_b) {worst_mm=std::max(worst_mm,std::abs(metres(g[0])-metres(b))*1000);CHECK(g[1]==255.f);}
    }
    c->Unmap(out.Get(),0);c->Unmap(truth.Get(),0);
    std::printf("retarget: covered %u px, coverage XOR %u (%.3f%%), worst depth %.4f mm\n",covered,xor_px,covered?100.0*xor_px/covered:0.,worst_mm);
    CHECK(covered>200 && xor_px*1000<=covered && worst_mm<1.0);
    // 2c. a held item (no rig palette; ObjectMotion marks it held after its placement): class 253
    drawn_rows=rows_of(vpa);d2vr::rig_target::apply(e,drawn_rows.data(),target_rows.data());
    c->ClearDepthStencilView(dsv_a.Get(),D3D11_CLEAR_DEPTH,0.f,0);c->OMSetRenderTargets(1,rtv.GetAddressOf(),dsv_a.Get());c->UpdateSubresource(cb.Get(),0,nullptr,drawn_rows.data(),0,0);
    {ID3D11ShaderResourceView* s=other_view.Get();c->VSSetShaderResources(0,1,&s);
     d2vr::RigMask::Scope scope(c.Get(),1);CHECK(!d2vr::RigMask::draw_is_rig());d2vr::RigMask::mark_held();d2vr::RigMask::mark_target(true);c->Draw(36,0);scope.after({2,36,1,0,0,0},draw,targets);}
    c->OMSetRenderTargets(0,nullptr,nullptr);
    // v1191: copied and cleared over the render rectangle only (here all but the last row and column)
    CHECK(d2vr::RigMask::take_target(c.Get(),depth_a.Get(),out.Get(),draws,N-1,N-1) && draws==1);
    CHECK(SUCCEEDED(c->Map(out.Get(),0,D3D11_MAP_READ,0,&mo)));unsigned held=0;
    for(UINT y=0;y<N;++y) for(UINT x=0;x<N;++x) held+=static_cast<const float*>(mo.pData)[y*(mo.RowPitch/4)+x*2+1]==253.f;
    c->Unmap(out.Get(),0);std::printf("held: %u px of class 253\n",held);CHECK(held*1000>=covered*999 && held<=covered+covered/1000+1);
    // 2e. v1191: a held draw the engine rasterizes to nothing (its pixel shader discards; dump 8's invisible second sword) stays out of the target
    {static const char discard_src[]="float4 main(float4 p:SV_Position):SV_Target{discard;return 0;}";ComPtr<ID3DBlob> code;ComPtr<ID3D11PixelShader> none;
     CHECK(SUCCEEDED(D3DCompile(discard_src,sizeof discard_src-1,nullptr,nullptr,nullptr,"main","ps_5_0",0,0,&code,nullptr)));dev->CreatePixelShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&none);
     c->ClearDepthStencilView(dsv_a.Get(),D3D11_CLEAR_DEPTH,0.f,0);c->OMSetRenderTargets(1,rtv.GetAddressOf(),dsv_a.Get());c->PSSetShader(none.Get(),nullptr,0);
     ID3D11ShaderResourceView* s=other_view.Get();c->VSSetShaderResources(0,1,&s);
     {d2vr::RigMask::Scope scope(c.Get(),1);d2vr::RigMask::mark_held();d2vr::RigMask::mark_target(true);c->Draw(36,0);scope.after({2,36,1,0,0,0},draw,targets);}
     c->PSSetShader(ps.Get(),nullptr,0);c->OMSetRenderTargets(0,nullptr,nullptr);
     CHECK(d2vr::RigMask::take_target(c.Get(),depth_a.Get(),out.Get(),draws) && draws==1);
     CHECK(SUCCEEDED(c->Map(out.Get(),0,D3D11_MAP_READ,0,&mo)));unsigned unseen=0;
     for(UINT y=0;y<N;++y) for(UINT x=0;x<N;++x) unseen+=static_cast<const float*>(mo.pData)[y*(mo.RowPitch/4)+x*2+1]!=0.f;
     c->Unmap(out.Get(),0);std::printf("unseen held draw: %u px in the target\n",unseen);CHECK(unseen==0);}
    // 2d. a rig draw without its retarget makes the render invalid
    c->ClearDepthStencilView(dsv_a.Get(),D3D11_CLEAR_DEPTH,0.f,0);c->OMSetRenderTargets(1,rtv.GetAddressOf(),dsv_a.Get());
    {ID3D11ShaderResourceView* s=rig_view.Get();c->VSSetShaderResources(0,1,&s);
     d2vr::RigMask::Scope scope(c.Get(),1);d2vr::RigMask::mark_target(false);c->Draw(36,0);scope.after({2,36,1,0,0,0},draw,targets);}
    c->OMSetRenderTargets(0,nullptr,nullptr);
    CHECK(!d2vr::RigMask::take_target(c.Get(),depth_a.Get(),out.Get(),draws));
    d2vr::RigMask::reset();
    if(failures) {std::printf("rig_target_check: %d failure(s)\n",failures);return 1;}
    std::printf("PASS: rig_target_check (E math, retargeted replay = the other eye's render within 1 mm, held class, missing retarget invalid)\n");
    return 0;
}
