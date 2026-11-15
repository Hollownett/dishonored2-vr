// v1327 lab: the rig mask under the engine's threading (src/rig_mask.hpp, the real RigMask code on WARP). Job thread A records the one-view
// render on a DEFERRED context: a far world surface, held part 1, then (after thread B has recorded) held part 2 and a near world surface over a
// corner of part 1. Thread B records, in between, the same held mesh into two other depth passes on its own deferred context (a special bolt's
// parts: a half-size depth and another full-size depth). The command lists execute on the immediate context and the copy takes the mask.
// v1326 (no scene depth named): B's replays re-made the mask target at record time, part 1's keys went to a target the take never reads ->
// part 1 missing (the sims' holes). v1327: B's draws are not replayed, the target is never re-made, both parts are in the mask every render.
// Also: renders in a row (the copy zeroes the keys: no key survives into the next render), D24 and D32, a mesh left in its other pass only.
// Build/run: build\check-rig-mask-deferred.cmd
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>
#include "../src/rig_mask.hpp"
#pragma comment(lib,"d3d11.lib")
#pragma comment(lib,"d3dcompiler.lib")
using Microsoft::WRL::ComPtr;
static int failures=0;static bool quiet=false,show_renders=true;   // quiet: no FAIL lines (the v1326 control); show_renders: per-render lines
#define CHECK(x) do {if(!(x)) {if(!quiet) std::printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#x);++failures;}} while(0)
constexpr UINT N=96;

static ComPtr<ID3DBlob> compile(const char* src,const char* target) {
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
struct Rect {float x0,y0,x1,y1,z;};   // NDC, reverse-Z depth
static bool inside(const Rect& r,UINT x,UINT y) {const double nx=(x+.5)/N*2-1,ny=1-(y+.5)/N*2;return nx>r.x0 && nx<r.x1 && ny>r.y0 && ny<r.y1;}
static bool edge(const Rect& r,UINT x,UINT y) {const double nx=(x+.5)/N*2-1,ny=1-(y+.5)/N*2,e=2.5/N;
    return std::abs(nx-r.x0)<e || std::abs(nx-r.x1)<e || std::abs(ny-r.y0)<e || std::abs(ny-r.y1)<e;}

// One device, `renders` renders. publish: FsrTaa names the scene depth (v1327). other_only: B also draws a held mesh that has NO scene draw.
static void run(bool d24,bool publish,unsigned renders,bool other_only) {
    ComPtr<ID3D11Device> dev;ComPtr<ID3D11DeviceContext> imm;
    if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&dev,nullptr,&imm))) {std::printf("FAIL WARP\n");++failures;return;}
    d2vr::RigMask::reset();d2vr::RigMask::legacy_replay=false;
    const auto remade0=d2vr::RigMask::recreated_.load(),lost0=d2vr::RigMask::lost_events_.load(),other0=d2vr::RigMask::other_depth_.load();
    const DXGI_FORMAT typeless=d24?DXGI_FORMAT_R24G8_TYPELESS:DXGI_FORMAT_R32_TYPELESS,dsv_format=d24?DXGI_FORMAT_D24_UNORM_S8_UINT:DXGI_FORMAT_D32_FLOAT,
        read_format=d24?DXGI_FORMAT_R24_UNORM_X8_TYPELESS:DXGI_FORMAT_R32_FLOAT;
    const auto make_depth=[&](UINT w,ComPtr<ID3D11Texture2D>& tex,ComPtr<ID3D11DepthStencilView>& dsv) {
        D3D11_TEXTURE2D_DESC t{};t.Width=t.Height=w;t.MipLevels=t.ArraySize=1;t.SampleDesc.Count=1;t.Format=typeless;t.BindFlags=D3D11_BIND_DEPTH_STENCIL|D3D11_BIND_SHADER_RESOURCE;
        CHECK(SUCCEEDED(dev->CreateTexture2D(&t,nullptr,&tex)));D3D11_DEPTH_STENCIL_VIEW_DESC dv{};dv.Format=dsv_format;dv.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2D;
        CHECK(SUCCEEDED(dev->CreateDepthStencilView(tex.Get(),&dv,&dsv)));};
    ComPtr<ID3D11Texture2D> scene,half,full2;ComPtr<ID3D11DepthStencilView> scene_dsv,half_dsv,full2_dsv;
    make_depth(N,scene,scene_dsv);make_depth(N/2,half,half_dsv);make_depth(N,full2,full2_dsv);
    D3D11_SHADER_RESOURCE_VIEW_DESC sv{};sv.Format=read_format;sv.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;sv.Texture2D.MipLevels=1;
    ComPtr<ID3D11ShaderResourceView> scene_srv;CHECK(SUCCEEDED(dev->CreateShaderResourceView(scene.Get(),&sv,&scene_srv)));
    if(publish) d2vr::RigMask::scene_depth(scene.Get());
    static const char vs_src[]="cbuffer C:register(b0){float4 r;float4 z;};float4 main(uint i:SV_VertexID):SV_Position{"
        "const float2 q[6]={float2(0,0),float2(1,0),float2(0,1),float2(1,0),float2(1,1),float2(0,1)};const float2 u=q[i];"
        "return float4(lerp(r.x,r.z,u.x),lerp(r.y,r.w,u.y),z.x,1);}";
    static const char ps_src[]="float4 main(float4 p:SV_Position):SV_Target{return 1;}";
    const auto vsb=compile(vs_src,"vs_5_0"),psb=compile(ps_src,"ps_5_0");if(!vsb || !psb) return;
    ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;
    dev->CreateVertexShader(vsb->GetBufferPointer(),vsb->GetBufferSize(),nullptr,&vs);dev->CreatePixelShader(psb->GetBufferPointer(),psb->GetBufferSize(),nullptr,&ps);
    D3D11_DEPTH_STENCIL_DESC ds{};ds.DepthEnable=TRUE;ds.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;ds.DepthFunc=D3D11_COMPARISON_GREATER;
    ComPtr<ID3D11DepthStencilState> prepass;dev->CreateDepthStencilState(&ds,&prepass);
    D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;ComPtr<ID3D11RasterizerState> rs;dev->CreateRasterizerState(&rd,&rs);
    D3D11_BUFFER_DESC cbd{};cbd.ByteWidth=32;cbd.Usage=D3D11_USAGE_DEFAULT;cbd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    // per-draw constant buffers (a deferred context records UpdateSubresource in order, but separate buffers keep the two threads independent)
    const Rect far_world{-1,-1,1,1,.1f},part1{-.8f,-.6f,-.1f,.5f,.5f},part2{.1f,-.7f,.7f,.2f,.55f},near_world{-.5f,-1,0,-.2f,.9f},alone{.2f,.4f,.8f,.9f,.6f};
    const auto cbuf=[&](const Rect& r) {const float k[8]{r.x0,r.y0,r.x1,r.y1,r.z,0,0,0};D3D11_SUBRESOURCE_DATA i{k,0,0};ComPtr<ID3D11Buffer> b;dev->CreateBuffer(&cbd,&i,&b);return b;};
    const auto cb_far=cbuf(far_world),cb_p1=cbuf(part1),cb_p2=cbuf(part2),cb_near=cbuf(near_world),cb_alone=cbuf(alone);
    const auto draw=[](ID3D11DeviceContext* x,const d2vr::RigMask::DrawArgs& a) noexcept {x->Draw(a.count,a.start);};
    const auto targets=[](ID3D11DeviceContext* x,UINT n,ID3D11RenderTargetView* const* r,ID3D11DepthStencilView* d) noexcept {x->OMSetRenderTargets(n,r,d);};
    // copy/compare resources (as FsrTaa)
    D3D11_TEXTURE2D_DESC mt{};mt.Width=mt.Height=N;mt.MipLevels=mt.ArraySize=1;mt.SampleDesc.Count=1;mt.Format=DXGI_FORMAT_R8_TYPELESS;mt.BindFlags=D3D11_BIND_UNORDERED_ACCESS|D3D11_BIND_SHADER_RESOURCE;
    ComPtr<ID3D11Texture2D> mask;dev->CreateTexture2D(&mt,nullptr,&mask);
    D3D11_UNORDERED_ACCESS_VIEW_DESC mu{};mu.Format=DXGI_FORMAT_R8_UINT;mu.ViewDimension=D3D11_UAV_DIMENSION_TEXTURE2D;ComPtr<ID3D11UnorderedAccessView> mask_uav;dev->CreateUnorderedAccessView(mask.Get(),&mu,&mask_uav);
    D3D11_BUFFER_DESC nb{};nb.ByteWidth=16;nb.BindFlags=D3D11_BIND_UNORDERED_ACCESS;nb.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;ComPtr<ID3D11Buffer> counts;dev->CreateBuffer(&nb,nullptr,&counts);
    D3D11_UNORDERED_ACCESS_VIEW_DESC nu{};nu.Format=DXGI_FORMAT_R32_TYPELESS;nu.ViewDimension=D3D11_UAV_DIMENSION_BUFFER;nu.Buffer.NumElements=4;nu.Buffer.Flags=D3D11_BUFFER_UAV_FLAG_RAW;
    ComPtr<ID3D11UnorderedAccessView> counts_uav;dev->CreateUnorderedAccessView(counts.Get(),&nu,&counts_uav);
    const auto csb=compile(d2vr::rig_mask_copy_source,"cs_5_0");if(!csb) return;
    ComPtr<ID3D11ComputeShader> cs;dev->CreateComputeShader(csb->GetBufferPointer(),csb->GetBufferSize(),nullptr,&cs);
    float k[4]{0,0,float(N),float(N)};d2vr::rig_mask_eps(read_format,k[0],k[1]);
    D3D11_BUFFER_DESC kb{};kb.ByteWidth=16;kb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;D3D11_SUBRESOURCE_DATA kd{k,0,0};ComPtr<ID3D11Buffer> kbuf;dev->CreateBuffer(&kb,&kd,&kbuf);
    d2vr::RigMask::enabled=true;d2vr::RigMask::view=1;d2vr::RigMask::take_view_draws();
    unsigned renders_ok=0;
    for(unsigned render=0;render<renders;++render) {
        imm->ClearDepthStencilView(scene_dsv.Get(),D3D11_CLEAR_DEPTH|D3D11_CLEAR_STENCIL,0.f,0);
        imm->ClearDepthStencilView(half_dsv.Get(),D3D11_CLEAR_DEPTH|D3D11_CLEAR_STENCIL,0.f,0);imm->ClearDepthStencilView(full2_dsv.Get(),D3D11_CLEAR_DEPTH|D3D11_CLEAR_STENCIL,0.f,0);
        ComPtr<ID3D11DeviceContext> da,db;CHECK(SUCCEEDED(dev->CreateDeferredContext(0,&da)));CHECK(SUCCEEDED(dev->CreateDeferredContext(0,&db)));
        ComPtr<ID3D11CommandList> la,lb;std::atomic<int> step{0};
        const auto setup=[&](ID3D11DeviceContext* c,ID3D11DepthStencilView* dsv,UINT w) {
            const D3D11_VIEWPORT vp{0,0,float(w),float(w),0,1};c->RSSetViewports(1,&vp);c->RSSetState(rs.Get());c->OMSetRenderTargets(0,nullptr,dsv);
            c->OMSetDepthStencilState(prepass.Get(),0);c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);c->VSSetShader(vs.Get(),nullptr,0);c->PSSetShader(ps.Get(),nullptr,0);};
        const auto engine_draw=[&](ID3D11DeviceContext* c,ID3D11Buffer* cb,bool held) {   // one hooked draw: Scope, the draw, after()
            c->VSSetConstantBuffers(0,1,&cb);d2vr::RigMask::Scope scope(c,1);if(held) d2vr::RigMask::mark_held();
            c->Draw(6,0);scope.after({2,6,1,0,0,0},draw,targets);};
        std::thread a([&] {   // job thread A: the eye render on the scene depth
            setup(da.Get(),scene_dsv.Get(),N);
            engine_draw(da.Get(),cb_far.Get(),false);engine_draw(da.Get(),cb_p1.Get(),true);
            step=1;while(step.load()<2) std::this_thread::yield();   // B records its passes now (between part 1 and part 2)
            engine_draw(da.Get(),cb_p2.Get(),true);engine_draw(da.Get(),cb_near.Get(),false);
            da->FinishCommandList(FALSE,&la);
        });
        std::thread b([&] {   // job thread B: the held meshes in two other depth passes
            while(step.load()<1) std::this_thread::yield();
            setup(db.Get(),half_dsv.Get(),N/2);engine_draw(db.Get(),cb_p1.Get(),true);if(other_only) engine_draw(db.Get(),cb_alone.Get(),true);
            setup(db.Get(),full2_dsv.Get(),N);engine_draw(db.Get(),cb_p2.Get(),true);engine_draw(db.Get(),cb_p1.Get(),true);
            db->FinishCommandList(FALSE,&lb);step=2;
        });
        a.join();b.join();
        if(!la || !lb) {CHECK(la && lb);return;}
        imm->ExecuteCommandList(la.Get(),FALSE);imm->ExecuteCommandList(lb.Get(),FALSE);
        CHECK(d2vr::RigMask::take_view_draws()==7u+(other_only?1u:0u));   // A: 4 draws, B: 3 (+ the mesh with no scene draw)
        ComPtr<ID3D11ShaderResourceView> rig_srv;ComPtr<ID3D11RenderTargetView> rig_rtv;ComPtr<ID3D11UnorderedAccessView> keys;
        if(!d2vr::RigMask::target(scene.Get(),rig_srv,rig_rtv,&keys) || !keys) {CHECK(!"take: no mask target for the scene depth");continue;}
        const UINT zero[4]{};imm->ClearUnorderedAccessViewUint(counts_uav.Get(),zero);
        ID3D11ShaderResourceView* in[3]{scene_srv.Get(),rig_srv.Get(),nullptr};ID3D11UnorderedAccessView* outs[3]{mask_uav.Get(),counts_uav.Get(),keys.Get()};
        imm->CSSetShader(cs.Get(),nullptr,0);imm->CSSetShaderResources(0,3,in);imm->CSSetUnorderedAccessViews(0,3,outs,nullptr);imm->CSSetConstantBuffers(0,1,kbuf.GetAddressOf());
        imm->Dispatch((N+7)/8,(N+7)/8,1);
        ID3D11ShaderResourceView* none[3]{};ID3D11UnorderedAccessView* no[3]{};imm->CSSetShaderResources(0,3,none);imm->CSSetUnorderedAccessViews(0,3,no,nullptr);
        const auto m=read_back(dev.Get(),imm.Get(),mask.Get(),1);
        unsigned wrong=0,p1=0,p2=0,want1=0,want2=0;
        for(UINT y=0;y<N;++y) for(UINT x=0;x<N;++x) {
            // visible held: part 2 over part 1 where they overlap (nearer), the near world surface over both
            const bool covered=inside(near_world,x,y),in2=inside(part2,x,y) && !covered,in1=inside(part1,x,y) && !covered && !in2;
            const bool want=in1 || in2,got=m[y*N+x]==253;
            if(edge(far_world,x,y) || edge(part1,x,y) || edge(part2,x,y) || edge(near_world,x,y)) continue;   // rasterization rule at edges
            want1+=in1;want2+=in2;p1+=in1 && got;p2+=in2 && got;
            if(got!=want || (m[y*N+x]!=0 && m[y*N+x]!=253)) ++wrong;
        }
        if(show_renders) std::printf("%s %s render %u: part 1 %u/%u, part 2 %u/%u, wrong %u\n",d24?"D24":"D32",publish?"v1327":"v1326",render,p1,want1,p2,want2,wrong);
        CHECK(want1>500 && want2>500);CHECK(p1==want1);CHECK(p2==want2);CHECK(wrong==0);
        if(p1==want1 && p2==want2 && !wrong) ++renders_ok;
    }
    if(publish) {
        CHECK(renders_ok==renders);CHECK(d2vr::RigMask::recreated_.load()==remade0);CHECK(d2vr::RigMask::lost_events_.load()==lost0);
        CHECK(d2vr::RigMask::other_depth_.load()==other0+renders*(3u+(other_only?1u:0u)));
    }
    d2vr::RigMask::enabled=false;d2vr::RigMask::view=0;
}
int main() {
    std::setvbuf(stdout,nullptr,_IONBF,0);
    for(const bool d24:{false,true}) for(const bool other_only:{false,true}) run(d24,true,6,other_only);
    { // v1326 control: the scene depth not named - B's replays re-make the target and part 1's keys never reach the take
        const int before=failures;const auto lost=d2vr::RigMask::lost_events_.load();quiet=true;
        run(false,false,3,false);quiet=false;const bool reproduced=failures>before && d2vr::RigMask::lost_events_.load()>lost;failures=before;CHECK(reproduced);
        std::printf("v1326 control (no scene depth named): %s\n",reproduced?"parts lost, as in the sims":"NOT reproduced");
    }
    if(failures) {std::printf("rig_mask_deferred_check: %d failure(s)\n",failures);return 1;}
    std::printf("PASS: rig_mask_deferred_check (deferred job-thread recording, held draws in other depth passes mid-render, renders in a row, D24/D32)\n");
    return 0;
}
