// v1180 offline check of src/cross_eye_history.hpp on WARP, through the real Scope: a "temporal fog" pass that ping-pongs two targets
// (frame f reads T[f%2] as history, writes T[(f+1)%2] = 0.5 * history + the eye's value) with the eyes alternating per engine frame.
// The engine alone mixes the eyes (the history is always the other eye's output). Checks: the census flags the history read (shaped like
// target 0); with the fix the pass learns the role and from then on each eye's chain converges to its own fixed point (eye 0: 2 * 100,
// eye 1: 2 * 1), i.e. it reads its own output of two engine frames back; a skipped frame falls back for one frame and recovers.
//   Build/run: build\check-cross-eye-history.cmd
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "../src/cross_eye_history.hpp"
#pragma comment(lib,"d3d11.lib")
#pragma comment(lib,"d3dcompiler.lib")
using Microsoft::WRL::ComPtr;
static int failures=0;
#define CHECK(x) do {if(!(x)) {std::printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#x);++failures;}} while(0)
static std::uint32_t frame_now=1000;
int main() {
    ComPtr<ID3D11Device> dev;ComPtr<ID3D11DeviceContext> c;
    if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&dev,nullptr,&c))) {std::printf("FAIL WARP\n");return 1;}
    D3D11_TEXTURE2D_DESC t{};t.Width=t.Height=4;t.MipLevels=t.ArraySize=1;t.Format=DXGI_FORMAT_R32_FLOAT;t.SampleDesc.Count=1;t.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
    ComPtr<ID3D11Texture2D> tex[2];ComPtr<ID3D11RenderTargetView> rtv[2];ComPtr<ID3D11ShaderResourceView> srv[2];
    for(int i=0;i<2;++i) {dev->CreateTexture2D(&t,nullptr,&tex[i]);dev->CreateRenderTargetView(tex[i].Get(),nullptr,&rtv[i]);dev->CreateShaderResourceView(tex[i].Get(),nullptr,&srv[i]);
        const float z[4]{};c->ClearRenderTargetView(rtv[i].Get(),z);}
    static const char vs_src[]="float4 main(uint i:SV_VertexID):SV_Position{const float2 q[3]={float2(-1,-1),float2(-1,3),float2(3,-1)};return float4(q[i],0,1);}";
    static const char ps_src[]="Texture2D<float> h:register(t1);cbuffer C:register(b0){float4 v;};float main(float4 p:SV_Position):SV_Target{return 0.5*h.Load(int3(p.xy,0))+v.x;}";
    ComPtr<ID3DBlob> vb,pb;
    if(FAILED(D3DCompile(vs_src,sizeof vs_src-1,nullptr,nullptr,nullptr,"main","vs_5_0",0,0,&vb,nullptr)) || FAILED(D3DCompile(ps_src,sizeof ps_src-1,nullptr,nullptr,nullptr,"main","ps_5_0",0,0,&pb,nullptr))) {std::printf("FAIL compile\n");return 1;}
    ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;dev->CreateVertexShader(vb->GetBufferPointer(),vb->GetBufferSize(),nullptr,&vs);dev->CreatePixelShader(pb->GetBufferPointer(),pb->GetBufferSize(),nullptr,&ps);
    D3D11_BUFFER_DESC bd{};bd.ByteWidth=16;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;ComPtr<ID3D11Buffer> cb;dev->CreateBuffer(&bd,nullptr,&cb);
    const D3D11_VIEWPORT vp{0,0,4,4,0,1};c->RSSetViewports(1,&vp);c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    c->VSSetShader(vs.Get(),nullptr,0);c->PSSetShader(ps.Get(),nullptr,0);c->PSSetConstantBuffers(0,1,cb.GetAddressOf());
    D3D11_TEXTURE2D_DESC st=t;st.BindFlags=0;st.Usage=D3D11_USAGE_STAGING;st.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Texture2D> stage;dev->CreateTexture2D(&st,nullptr,&stage);
    const auto read=[&](ID3D11Texture2D* x) {c->CopyResource(stage.Get(),x);D3D11_MAPPED_SUBRESOURCE m{};float v=-1;if(SUCCEEDED(c->Map(stage.Get(),0,D3D11_MAP_READ,0,&m))) {v=*static_cast<float*>(m.pData);c->Unmap(stage.Get(),0);}return v;};
    d2vr::CrossEyeHistory::frame_fn=+[]() noexcept {return frame_now;};d2vr::CrossEyeHistory::view=1;
    float last[2]{};
    const auto frame=[&](bool skip=false) {
        ++frame_now;if(skip) return;
        const unsigned eye=frame_now%2,in=frame_now%2,out=in^1u;
        const float v[4]{eye?1.f:100.f,0,0,0};c->UpdateSubresource(cb.Get(),0,nullptr,v,0,0);
        ID3D11ShaderResourceView* none[2]{};c->PSSetShaderResources(0,2,none);c->OMSetRenderTargets(1,rtv[out].GetAddressOf(),nullptr);
        ID3D11ShaderResourceView* h=srv[in].Get();c->PSSetShaderResources(1,1,&h);
        {d2vr::CrossEyeHistory::Scope s(c.Get(),1,7);c->Draw(3,0);s.after();}
        c->OMSetRenderTargets(0,nullptr,nullptr);c->PSSetShaderResources(1,1,none);
        last[eye]=read(tex[out].Get());
    };
    // 1. engine alone (census only): the history is the other eye's, both chains mix: x = 0.5*y + 100, y = 0.5*x + 1 -> x ~ 134, y ~ 68
    d2vr::CrossEyeHistory::census=true;
    for(int i=0;i<60;++i) frame();
    std::printf("census only: eye 0 %.3f, eye 1 %.3f (mixed fixed point 134.0 / 68.0)\n",last[0],last[1]);
    CHECK(std::abs(last[0]-134.f)<1 && std::abs(last[1]-68.f)<1);
    const auto j=d2vr::CrossEyeHistory::json();std::printf("%s\n",j.c_str());
    CHECK(j.find("\"shaped\":true")!=std::string::npos && j.find("\"slot\":1")!=std::string::npos);
    // 2. the fix: the role was learned during the census (>= 32 frames); each eye converges to its own chain
    d2vr::CrossEyeHistory::fix=true;d2vr::CrossEyeHistory::census=false;
    for(int i=0;i<60;++i) frame();
    std::printf("fix: eye 0 %.3f, eye 1 %.3f (own fixed point 200.0 / 2.0)\n",last[0],last[1]);
    CHECK(std::abs(last[0]-200.f)<.01f && std::abs(last[1]-2.f)<.01f);
    // 3. a skipped engine frame: the next frames fall back (engine history) then recover
    frame(true);
    for(int i=0;i<60;++i) frame();
    std::printf("after a gap: eye 0 %.3f, eye 1 %.3f\n",last[0],last[1]);
    CHECK(std::abs(last[0]-200.f)<.01f && std::abs(last[1]-2.f)<.01f);
    CHECK(d2vr::CrossEyeHistory::json().find("\"roles\":[{")!=std::string::npos);
    d2vr::CrossEyeHistory::reset();
    if(failures) {std::printf("cross_eye_history_check: %d failure(s)\n",failures);return 1;}
    std::printf("PASS: cross_eye_history_check (census flags the history read; per-eye ring restores each eye's own previous output)\n");
    return 0;
}
