#pragma once
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <atomic>
#include <mutex>
#include "taa_dispatch.hpp"

namespace d2vr {
// The native TAA reads the game's motion vectors (t1: UV, current minus previous position) and they carry
// the frame-to-frame jitter step: the eye projection is jittered and the engine's MV pass does not cancel
// it (measured: a still head gives a uniform MV equal to the engine pattern step, e.g. (2,10)/16 px). The
// history is then fetched one jitter step off every frame, which smears the image (worst in x).
// Before the native TAA dispatch, MV - step is written to a mod texture that is bound as t1 for that one
// dispatch. Resources are made in prepare() on the Present thread; the job thread only records commands.
class TaaMvDejitter {
    template<class T> using Ptr=Microsoft::WRL::ComPtr<T>;
    std::mutex mutex_;
    Ptr<ID3D11ComputeShader> cs_;Ptr<ID3D11Buffer> cb_;
    Ptr<ID3D11Texture2D> texture_;Ptr<ID3D11ShaderResourceView> srv_;Ptr<ID3D11UnorderedAccessView> uav_;
    UINT ready_w_{},ready_h_{};
    std::atomic<UINT> wanted_w_{},wanted_h_{};
public:
    std::atomic<bool> enabled{true};
    std::atomic<UINT64> applied{},skipped{},failures{},from_cb_applied{};
    std::atomic<bool> from_cb{true}; // v910: step from the dispatch's own b1; knob build/legacy-taa-jitter = the CPU pair (read live)
    // UV step that the motion vectors carry, from the per-eye jitter (engine sign, see engine_taa_jitter):
    // the content moves by (-j.x,+j.y) in UV, so MV = (-(cur.x-prev.x), cur.y-prev.y).
    static std::array<float,2> step(float cur_x,float cur_y,float prev_x,float prev_y) noexcept {return {prev_x-cur_x,cur_y-prev_y};}
    // from_cb (v910): the step comes from the TAA dispatch's own b1 (CBPerViewGlobals, still bound at slot 1): the frustum
    // shift of cb_previousprojectionmatrix minus cb_projectionmatrix (floats 8,9 at float4 25 and 13), i.e. exactly the
    // jitter pair inside the VP/prevVP the motion vectors were built from, whatever the CPU bookkeeping saw (taa_jitter_check.hpp).
    // The engine's motion-vector pass never writes the sky (depth 0 keeps the cleared (0,0), v911 dump: 24% of a frame), so
    // only written texels carry the jitter step; subtracting it from (0,0) made the sky move by -step and, through the TAA's
    // longest-vector-in-3x3 pick, dragged the history of every silhouette against the sky. from_cb keeps exact zeros.
    static constexpr char shader_source[]="Texture2D<float2> i:register(t0);RWTexture2D<float2> o:register(u0);cbuffer c:register(b0){float2 s;float from_cb;float pad;};"
        "cbuffer v:register(b1){float4 r[50];};"
        "[numthreads(8,8,1)] void main(uint3 p:SV_DispatchThreadID){float2 d=from_cb>.5?float2(r[25].x-r[13].x,r[13].y-r[25].y)*.5:s;"
        "float2 m=i.Load(int3(p.xy,0));o[p.xy]=from_cb>.5 && m.x==0 && m.y==0?m:m-d;}";
    // Present thread.
    void prepare(ID3D11Device* device) noexcept {
        const UINT w=wanted_w_.load(),h=wanted_h_.load();
        if(!enabled.load() || !device || !w || !h) return;
        std::lock_guard<std::mutex> lock(mutex_);
        if(ready_w_==w && ready_h_==h && cs_) return;
        ready_w_=ready_h_=0;
        if(!cs_) {
            Ptr<ID3DBlob> code;
            if(FAILED(D3DCompile(shader_source,sizeof shader_source-1,nullptr,nullptr,nullptr,"main","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,nullptr)) ||
               FAILED(device->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&cs_))) {++failures;enabled=false;return;}
            D3D11_BUFFER_DESC bd{};bd.ByteWidth=16;bd.Usage=D3D11_USAGE_DEFAULT;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
            if(FAILED(device->CreateBuffer(&bd,nullptr,&cb_))) {++failures;enabled=false;cs_.Reset();return;}
        }
        D3D11_TEXTURE2D_DESC d{};d.Width=w;d.Height=h;d.MipLevels=1;d.ArraySize=1;d.Format=DXGI_FORMAT_R16G16_FLOAT;d.SampleDesc.Count=1;
        d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;
        Ptr<ID3D11Texture2D> t;Ptr<ID3D11ShaderResourceView> s;Ptr<ID3D11UnorderedAccessView> u;
        if(FAILED(device->CreateTexture2D(&d,nullptr,&t)) || FAILED(device->CreateShaderResourceView(t.Get(),nullptr,&s)) ||
           FAILED(device->CreateUnorderedAccessView(t.Get(),nullptr,&u))) {++failures;return;}
        texture_=t;srv_=s;uav_=u;ready_w_=w;ready_h_=h;
    }
    // Job thread, inside the TAA dispatch hook: records MV - step, the TAA dispatch reading it, and restores
    // t1 and the compute state. False leaves the dispatch to the caller.
    bool dispatch(ID3D11DeviceContext* c,const TaaBindings& b,std::array<float,2> uv_step,TaaDispatchHook::Fn original,UINT x,UINT y,UINT z,bool use_cb=false) {
        if(!enabled.load() || !c || !original || !b.taa() || !b.is_tex[1]) return false;
        const auto& mv=b.tex[1];
        wanted_w_=mv.Width;wanted_h_=mv.Height;
        std::lock_guard<std::mutex> lock(mutex_);
        if(!cs_ || ready_w_!=mv.Width || ready_h_!=mv.Height || mv.MipLevels!=1 || mv.ArraySize!=1) {++skipped;return false;}
        ID3D11ComputeShader* shader{};c->CSGetShader(&shader,nullptr,nullptr);
        ID3D11Buffer* cb0{};c->CSGetConstantBuffers(0,1,&cb0);
        ID3D11UnorderedAccessView* uav0{};c->CSGetUnorderedAccessViews(0,1,&uav0);
        ID3D11ShaderResourceView* srv0{};c->CSGetShaderResources(0,1,&srv0);
        D3D11_BUFFER_DESC per_view{};if(b.cb[0]) b.cb[0]->GetDesc(&per_view);
        const float constants[4]{uv_step[0],uv_step[1],use_cb && per_view.ByteWidth==0x320?1.f:0.f,0};
        if(constants[2]>.5f) ++from_cb_applied;
        c->UpdateSubresource(cb_.Get(),0,nullptr,constants,0,0);
        ID3D11ShaderResourceView* null_srv{};ID3D11UnorderedAccessView* null_uav{};
        c->CSSetShaderResources(0,1,&b.srv[1]);c->CSSetUnorderedAccessViews(0,1,uav_.GetAddressOf(),nullptr);
        c->CSSetConstantBuffers(0,1,cb_.GetAddressOf());c->CSSetShader(cs_.Get(),nullptr,0);
        original(c,(mv.Width+7)/8,(mv.Height+7)/8,1);
        // Native state back, with the corrected vectors as t1 for the TAA dispatch only.
        c->CSSetUnorderedAccessViews(0,1,&null_uav,nullptr);
        c->CSSetShader(shader,nullptr,0);c->CSSetConstantBuffers(0,1,&cb0);c->CSSetShaderResources(0,1,&srv0);
        c->CSSetUnorderedAccessViews(0,1,&uav0,nullptr);
        c->CSSetShaderResources(1,1,srv_.GetAddressOf());
        original(c,x,y,z);
        c->CSSetShaderResources(1,1,&null_srv);c->CSSetShaderResources(1,1,&b.srv[1]);
        if(shader) shader->Release();
        if(cb0) cb0->Release();
        if(uav0) uav0->Release();
        if(srv0) srv0->Release();
        ++applied;
        return true;
    }
    void clear() {std::lock_guard<std::mutex> lock(mutex_);texture_.Reset();srv_.Reset();uav_.Reset();ready_w_=ready_h_=0;wanted_w_=0;wanted_h_=0;}
};
} // namespace d2vr
