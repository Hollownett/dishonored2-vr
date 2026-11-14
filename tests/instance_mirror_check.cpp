// v1310 check of the held-only instance mirror in the rig mask replay (src/rig_mask.hpp swap_instances / SwapView, class 3), on WARP.
// One instanced draw, three instances from a 96-byte-record structured buffer at VS t0 (world rows, the engine's ro_directinstancebuffer
// shape): held part A, a WORLD instance W batched with them (the bolt mesh with the world's bolts), held part B. The mirror keeps A and B
// and zeroes W. Checks: the mask keys hold class 3 (held, no rig target) over A and B and nothing over W; the engine's view is bound at t0
// again after the replay; the draw voids no rig target (no_retarget unchanged); without the swap, W would be held too (the v1309 bug).
// v1315: with per-slice retargeted copies (RigMask::target_instances; here A and B moved up by 0.4 NDC in slice 0, down in slice 1), the
// mask holds A and B as class 2 (held, rig target) and the taken slice holds them, class 253, at the moved places only; the engine's view is
// back at t0 and the target is valid (one draw, no miss).
//   Build/run: build\tmp-check-run.cmd instance_mirror_check /DNOMINMAX
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <array>
#include "../src/rig_mask.hpp"
#pragma comment(lib,"d3d11.lib")
#pragma comment(lib,"d3dcompiler.lib")
using Microsoft::WRL::ComPtr;
static int failures=0;
#define CHECK(x) do {if(!(x)) {std::printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#x);++failures;}} while(0)
constexpr UINT N=64;
static std::vector<std::uint32_t> keys_of(ID3D11Device* d,ID3D11DeviceContext* c,ID3D11Resource* r) {
    ComPtr<ID3D11Texture2D> t;r->QueryInterface(IID_PPV_ARGS(&t));D3D11_TEXTURE2D_DESC desc{};t->GetDesc(&desc);
    desc.BindFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.MiscFlags=0;
    ComPtr<ID3D11Texture2D> s;std::vector<std::uint32_t> out(N*N);
    if(FAILED(d->CreateTexture2D(&desc,nullptr,&s))) {++failures;return out;}
    c->CopyResource(s.Get(),t.Get());D3D11_MAPPED_SUBRESOURCE m{};
    if(FAILED(c->Map(s.Get(),0,D3D11_MAP_READ,0,&m))) {++failures;return out;}
    for(UINT y=0;y<N;++y) std::memcpy(&out[y*N],static_cast<const char*>(m.pData)+y*m.RowPitch,N*4);
    c->Unmap(s.Get(),0);return out;
}
static std::uint64_t num(const std::string& j,const char* key) {const auto p=j.find(key);return p==std::string::npos?~0ull:std::strtoull(j.c_str()+p+std::strlen(key),nullptr,10);}
struct Run {std::vector<std::uint32_t> keys;bool engine_view_back{};std::uint64_t no_retarget{};std::vector<float> slice;bool target_valid{};std::uint64_t target_draws{};};
// one record: modelMatrixX/Y/Z (world rows; translation in .w) then the previous rows (unused by the depth replay)
static void record(float* r,float x,float y,float z) {std::memset(r,0,96);r[0]=1;r[3]=x;r[5]=1;r[7]=y;r[10]=1;r[11]=z;std::memcpy(r+12,r,48);}
static Run run(bool swap,int target_slice=-1) {
    Run out;
    ComPtr<ID3D11Device> dev;ComPtr<ID3D11DeviceContext> c;
    if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&dev,nullptr,&c))) {std::printf("FAIL WARP\n");++failures;return out;}
    d2vr::RigMask::reset();d2vr::RigMask::legacy_replay=false;
    D3D11_TEXTURE2D_DESC t{};t.Width=t.Height=N;t.MipLevels=t.ArraySize=1;t.SampleDesc.Count=1;t.Format=DXGI_FORMAT_R32_TYPELESS;
    t.BindFlags=D3D11_BIND_DEPTH_STENCIL|D3D11_BIND_SHADER_RESOURCE;ComPtr<ID3D11Texture2D> depth;dev->CreateTexture2D(&t,nullptr,&depth);
    D3D11_DEPTH_STENCIL_VIEW_DESC dv{};dv.Format=DXGI_FORMAT_D32_FLOAT;dv.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2D;ComPtr<ID3D11DepthStencilView> dsv;dev->CreateDepthStencilView(depth.Get(),&dv,&dsv);
    D3D11_TEXTURE2D_DESC ct=t;ct.Format=DXGI_FORMAT_R8G8B8A8_UNORM;ct.BindFlags=D3D11_BIND_RENDER_TARGET;ComPtr<ID3D11Texture2D> colour;ComPtr<ID3D11RenderTargetView> rtv;
    dev->CreateTexture2D(&ct,nullptr,&colour);dev->CreateRenderTargetView(colour.Get(),nullptr,&rtv);
    // the engine buffer: A at the left, W in the middle, B at the right (each a quad of half size 0.15 NDC); the mirror zeroes W
    std::array<float,72> engine{},mirror{};record(&engine[0],-.6f,0,.5f);record(&engine[24],0,0,.5f);record(&engine[48],.6f,0,.5f);
    mirror=engine;std::memset(&mirror[24],0,96);
    const auto make=[&](const std::array<float,72>& data,ComPtr<ID3D11Buffer>& b,ComPtr<ID3D11ShaderResourceView>& v) {
        D3D11_BUFFER_DESC bd{};bd.ByteWidth=sizeof data;bd.Usage=D3D11_USAGE_DYNAMIC;bd.BindFlags=D3D11_BIND_SHADER_RESOURCE;bd.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
        bd.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;bd.StructureByteStride=96;D3D11_SUBRESOURCE_DATA init{data.data(),0,0};
        CHECK(SUCCEEDED(dev->CreateBuffer(&bd,&init,&b)));D3D11_SHADER_RESOURCE_VIEW_DESC sv{};sv.Format=DXGI_FORMAT_UNKNOWN;sv.ViewDimension=D3D11_SRV_DIMENSION_BUFFER;sv.Buffer.NumElements=3;
        CHECK(SUCCEEDED(dev->CreateShaderResourceView(b.Get(),&sv,&v)));};
    ComPtr<ID3D11Buffer> eb,mb;ComPtr<ID3D11ShaderResourceView> ev,mv;make(engine,eb,ev);make(mirror,mb,mv);
    std::array<float,72> up=mirror,down=mirror;up[7]+=.4f;up[55]+=.4f;down[7]-=.4f;down[55]-=.4f;   // v1315: the slices' retargeted records
    ComPtr<ID3D11Buffer> ub,db;ComPtr<ID3D11ShaderResourceView> uv,dv_;make(up,ub,uv);make(down,db,dv_);
    static const char vs_src[]="struct R{float4 x,y,z,px,py,pz;};StructuredBuffer<R> inst:register(t0);"
        "float4 main(uint i:SV_VertexID,uint n:SV_InstanceID):SV_Position{const float2 q[6]={float2(-1,-1),float2(1,-1),float2(-1,1),float2(1,-1),float2(1,1),float2(-1,1)};"
        "const R r=inst[n];const float4 l=float4(q[i]*.15,0,1);return float4(dot(l,r.x),dot(l,r.y),dot(l,r.z),1);}";
    static const char ps_src[]="float4 main(float4 p:SV_Position):SV_Target{return float4(p.z,0,0,1);}";
    ComPtr<ID3DBlob> vsb,psb,err;
    if(FAILED(D3DCompile(vs_src,sizeof vs_src-1,nullptr,nullptr,nullptr,"main","vs_5_0",0,0,&vsb,&err)) || FAILED(D3DCompile(ps_src,sizeof ps_src-1,nullptr,nullptr,nullptr,"main","ps_5_0",0,0,&psb,&err))) {
        std::printf("FAIL compile %s\n",err?static_cast<const char*>(err->GetBufferPointer()):"");++failures;return out;}
    ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;dev->CreateVertexShader(vsb->GetBufferPointer(),vsb->GetBufferSize(),nullptr,&vs);dev->CreatePixelShader(psb->GetBufferPointer(),psb->GetBufferSize(),nullptr,&ps);
    D3D11_DEPTH_STENCIL_DESC ds{};ds.DepthEnable=TRUE;ds.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;ds.DepthFunc=D3D11_COMPARISON_GREATER;ComPtr<ID3D11DepthStencilState> prepass;dev->CreateDepthStencilState(&ds,&prepass);
    D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;ComPtr<ID3D11RasterizerState> rs;dev->CreateRasterizerState(&rd,&rs);
    const D3D11_VIEWPORT vp{0,0,float(N),float(N),0,1};c->RSSetViewports(1,&vp);c->RSSetState(rs.Get());c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    c->VSSetShader(vs.Get(),nullptr,0);c->PSSetShader(ps.Get(),nullptr,0);c->VSSetShaderResources(0,1,ev.GetAddressOf());
    const float clear[4]{};c->ClearRenderTargetView(rtv.Get(),clear);c->ClearDepthStencilView(dsv.Get(),D3D11_CLEAR_DEPTH,0.f,0);
    c->OMSetRenderTargets(1,rtv.GetAddressOf(),dsv.Get());c->OMSetDepthStencilState(prepass.Get(),0);
    d2vr::RigMask::enabled=true;d2vr::RigMask::view=1;d2vr::RigMask::target_on=true;d2vr::RigMask::take_view_draws();
    const auto draw=[](ID3D11DeviceContext* x,const d2vr::RigMask::DrawArgs& a) noexcept {x->DrawInstanced(a.count,a.instances,a.start,a.first);};
    const auto targets=[](ID3D11DeviceContext* x,UINT n,ID3D11RenderTargetView* const* r,ID3D11DepthStencilView* d) noexcept {x->OMSetRenderTargets(n,r,d);};
    const std::string before=d2vr::RigMask::json();
    {d2vr::RigMask::Scope scope(c.Get(),1);
     if(swap) d2vr::RigMask::swap_instances(0,mv.Get());
     d2vr::RigMask::mark_held_untargeted();
     if(target_slice>=0) {ID3D11ShaderResourceView* const slices[2]{uv.Get(),dv_.Get()};d2vr::RigMask::target_instances(slices);}
     c->DrawInstanced(6,3,0,0);scope.after({3,6,3,0,0,0},draw,targets);}
    {ID3D11ShaderResourceView* now{};c->VSGetShaderResources(0,1,&now);out.engine_view_back=now==ev.Get();if(now) now->Release();}
    out.no_retarget=num(d2vr::RigMask::json(),"\"invalid_by_no_retarget\":")-num(before,"\"invalid_by_no_retarget\":");
    ComPtr<ID3D11ShaderResourceView> mask_srv;ComPtr<ID3D11RenderTargetView> mask_rtv;ComPtr<ID3D11UnorderedAccessView> mask_keys;
    if(!d2vr::RigMask::target(depth.Get(),mask_srv,mask_rtv,&mask_keys) || !mask_keys) {std::printf("FAIL no mask keys\n");++failures;return out;}
    ComPtr<ID3D11Resource> res;mask_keys->GetResource(&res);out.keys=keys_of(dev.Get(),c.Get(),res.Get());
    if(target_slice>=0) {   // v1315: the slice as FsrTaa takes it (x depth, y class)
        D3D11_TEXTURE2D_DESC td{};CHECK(d2vr::RigMask::target_desc(depth.Get(),td));
        D3D11_TEXTURE2D_DESC od=td;od.BindFlags=D3D11_BIND_SHADER_RESOURCE|(td.BindFlags&D3D11_BIND_UNORDERED_ACCESS);ComPtr<ID3D11Texture2D> o;dev->CreateTexture2D(&od,nullptr,&o);
        out.target_valid=d2vr::RigMask::take_target(c.Get(),depth.Get(),o.Get(),out.target_draws,0,0,static_cast<unsigned>(target_slice));
        td.BindFlags=0;td.Usage=D3D11_USAGE_STAGING;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Texture2D> s;dev->CreateTexture2D(&td,nullptr,&s);c->CopyResource(s.Get(),o.Get());
        D3D11_MAPPED_SUBRESOURCE m{};out.slice.assign(N*N,0.f);
        if(SUCCEEDED(c->Map(s.Get(),0,D3D11_MAP_READ,0,&m))) {for(UINT y=0;y<N;++y) for(UINT x=0;x<N;++x) out.slice[y*N+x]=static_cast<const float*>(m.pData)[y*(m.RowPitch/4)+x*2+1];c->Unmap(s.Get(),0);}
        else ++failures;
    }
    d2vr::RigMask::reset();
    return out;
}
int main() {
    std::setvbuf(stdout,nullptr,_IONBF,0);
    const auto at=[](const std::vector<std::uint32_t>& k,float x,float y) {const UINT px=static_cast<UINT>((x+1)*.5f*N),py=static_cast<UINT>((1-y)*.5f*N);return k[py*N+px];};
    const Run s=run(true),w=run(false);
    const auto cls=[](std::uint32_t key) {return key?int(key&3u):-1;};
    std::printf("mirror: A class %d, W class %d, B class %d; engine view back %s; targets voided %llu\n",cls(at(s.keys,-.6f,0)),cls(at(s.keys,0,0)),cls(at(s.keys,.6f,0)),
        s.engine_view_back?"yes":"NO",static_cast<unsigned long long>(s.no_retarget));
    std::printf("no mirror (v1309): A class %d, W class %d, B class %d\n",cls(at(w.keys,-.6f,0)),cls(at(w.keys,0,0)),cls(at(w.keys,.6f,0)));
    CHECK(cls(at(s.keys,-.6f,0))==3 && cls(at(s.keys,.6f,0))==3 && cls(at(s.keys,0,0))==-1);
    CHECK(s.engine_view_back && s.no_retarget==0);
    CHECK(cls(at(w.keys,0,0))==3);   // the whole-draw mark held the world instance too
    // v1315: the rig target for the instanced held parts
    for(int k=0;k<2;++k) {
        const Run t=run(true,k);const float dy=k?-.4f:.4f;
        const auto in=[&](float x,float y) {const UINT px=static_cast<UINT>((x+1)*.5f*N),py=static_cast<UINT>((1-y)*.5f*N);return t.slice.empty()?0.f:t.slice[py*N+px];};
        std::printf("target slice %d: mask A class %d, W class %d, B class %d; slice at A+dy %g, B+dy %g, A %g, W+dy %g; valid %s draws %llu; engine view back %s; voided %llu\n",k,
            cls(at(t.keys,-.6f,0)),cls(at(t.keys,0,0)),cls(at(t.keys,.6f,0)),in(-.6f,dy),in(.6f,dy),in(-.6f,0),in(0,dy),t.target_valid?"yes":"NO",
            static_cast<unsigned long long>(t.target_draws),t.engine_view_back?"yes":"NO",static_cast<unsigned long long>(t.no_retarget));
        CHECK(cls(at(t.keys,-.6f,0))==2 && cls(at(t.keys,.6f,0))==2 && cls(at(t.keys,0,0))==-1);
        CHECK(in(-.6f,dy)==253.f && in(.6f,dy)==253.f && in(-.6f,0)==0.f && in(.6f,0)==0.f && in(0,dy)==0.f);
        CHECK(t.target_valid && t.target_draws==1 && t.engine_view_back && t.no_retarget==0);
    }
    if(failures) {std::printf("instance_mirror_check: %d failure(s)\n",failures);return 1;}
    std::printf("PASS: instance_mirror_check\n");
    return 0;
}
