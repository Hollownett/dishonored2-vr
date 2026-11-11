#pragma once
// v1163 performance mode: the player-rig mask per eye, from the rig draws themselves (replaces v1162's stencil bit: the sim census found
// the engine's depth-stencil states read 0xEF and write 0xBF of the stencil, refs OR 0xFF: no free bit).
//
// The link from a draw to the rig (v1162, sim-proven: 6 buffers, 12 rig draws per Present): a skinned rig model (both arms, the full-body
// avatar) owns two GPU skin-palette buffers (render model +0x2008 / +0x2040), and its draws bind one of them at VS t0..t2. Exact in any
// mode. Not covered (by decision): held weapons and props (the mover rules take them), NPC models the hands hold.
//
// The mask: right after each rig draw that writes the scene depth in a pass of the alternate one-view, the SAME draw is replayed with this
// file's pixel shader (SV_Position.z) into a rig-depth target (R32_FLOAT, the scene depth's size), the engine's depth view bound for the test
// with depth EQUAL, no depth write, no stencil, no blending: the rig depth where the rig won the depth test then. The engine's targets, depth
// state, blend state and pixel shader are restored; nothing of the engine's is written. Visibility is resolved at the copy (FsrTaa):
// Eye::rig = 255 where scene depth > 0 and |rig depth - scene depth| <= eps, so anything drawn over the rig later, in any order, wins.
// The rig target is cleared after each copy. The PS reads only SV_Position: system-value position links with any vertex shader that writes
// it, whatever its other outputs (checked on WARP with SV_Position not the first output, tests/rig_mask_check.cpp).
// Knob build/no-stale-eye-rig (dxgi_probe): inert (no detection, no replay, no mask).
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <sstream>
#include <string>

namespace d2vr {
// The copy/compare: t0 scene depth (float view), t1 rig depth; u0 Eye::rig (R8_UINT), u1 counters: [0] mask pixels, [1] near misses
// (rig depth present, eps < |difference| <= 64 eps: an eps too tight shows here). k.xy eps = x + y * depth, k.zw the render rectangle.
inline constexpr char rig_mask_copy_source[]=
    "Texture2D<float> z:register(t0);Texture2D<float> r:register(t1);RWTexture2D<uint> o:register(u0);RWByteAddressBuffer n:register(u1);"
    "cbuffer C:register(b0){float4 k;};groupshared uint g[2];"
    "[numthreads(8,8,1)] void main(uint3 i:SV_DispatchThreadID,uint t:SV_GroupIndex){"
    "if(t<2) g[t]=0;GroupMemoryBarrierWithGroupSync();"
    "const bool inside=all(float2(i.xy)<k.zw);const float d=z.Load(int3(i.xy,0)),q=r.Load(int3(i.xy,0));"
    "const float e=k.x+k.y*d,a=abs(q-d);const bool rig=inside && d>0 && q>0 && a<=e;"
    "o[i.xy]=rig?255:0;"
    "if(rig) InterlockedAdd(g[0],1);else if(inside && d>0 && q>0 && a<=64*e) InterlockedAdd(g[1],1);"
    "GroupMemoryBarrierWithGroupSync();if(t<2 && g[t]) n.InterlockedAdd(t*4,g[t]);}";
// eps for a depth view format: D24 one step is 2^-24 (the replay's float z vs the stored 24-bit value: half a step + rounding); float: 4 ulp.
inline void rig_mask_eps(DXGI_FORMAT depth_srv,float& absolute,float& relative) noexcept {
    if(depth_srv==DXGI_FORMAT_R24_UNORM_X8_TYPELESS || depth_srv==DXGI_FORMAT_D24_UNORM_S8_UINT) {absolute=2.f/16777215.f;relative=0;}
    else if(depth_srv==DXGI_FORMAT_R16_UNORM || depth_srv==DXGI_FORMAT_D16_UNORM) {absolute=2.f/65535.f;relative=0;}
    else {absolute=1e-9f;relative=4.f/8388608.f;}
}

class RigMask {
public:
    static constexpr unsigned buffer_slots=6;
    inline static std::atomic<bool> enabled{};          // dxgi_probe, each Present: alternate-eye on and build/no-stale-eye-rig absent
    inline static std::atomic<std::uintptr_t> view{};    // the alternate one-view whose passes are replayed (0: a paired frame)
    struct DrawArgs {unsigned kind{};UINT count{},instances{1},start{};INT base{};UINT first{};}; // 0 DrawIndexed 1 ..Instanced 2 Draw 3 DrawInstanced
    using DrawFn=void(*)(ID3D11DeviceContext*,const DrawArgs&) noexcept;               // the engine's draw, past every hook
    using TargetsFn=void(*)(ID3D11DeviceContext*,UINT,ID3D11RenderTargetView* const*,ID3D11DepthStencilView*) noexcept; // OMSetRenderTargets, past the hooks
    static void publish(const std::array<std::uintptr_t,buffer_slots>& buffers) noexcept {
        unsigned n=0;
        for(unsigned i=0;i<buffer_slots;++i) {buffers_[i].store(buffers[i],std::memory_order_relaxed);n+=buffers[i]!=0;}
        published_buffers_.store(n,std::memory_order_relaxed);
    }
    static void present() noexcept { // Present thread
        rig_draws_last_.store(rig_draws_frame_.exchange(0,std::memory_order_relaxed),std::memory_order_relaxed);
        replays_last_.store(replays_frame_.exchange(0,std::memory_order_relaxed),std::memory_order_relaxed);
    }
    static bool draw_is_rig() noexcept {return tl_rig_;} // the draw now in a Scope binds a rig skin buffer (ObjectMotion)
    // FsrTaa's copy: draws of the alternate view seen since the last call (0: no one-view render, the mask is not live), and the rig target
    // of this scene depth (empty: none yet / another depth).
    static std::uint64_t take_view_draws() noexcept {return view_draws_.exchange(0,std::memory_order_relaxed);}
    static bool target(ID3D11Resource* depth,Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>& srv,Microsoft::WRL::ComPtr<ID3D11RenderTargetView>& rtv) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(!depth || target_.depth!=depth || !target_.srv) return false;
        srv=target_.srv;rtv=target_.rtv;return true;
    }
    static void reset() noexcept {std::lock_guard<std::mutex> lock(mutex_);target_={};ps_.Reset();equal_.Reset();blend_.Reset();device_=nullptr;made_=make_failed_=false;}
    static std::string json() {
        std::ostringstream o;
        o<<"{\"enabled\":"<<(enabled.load()?"true":"false")<<",\"buffers\":"<<published_buffers_.load()<<",\"rig_draws_last_present\":"<<rig_draws_last_.load()
         <<",\"rig_draws\":"<<rig_draws_.load()<<",\"replays_last_present\":"<<replays_last_.load()<<",\"replays\":"<<replays_.load()
         <<",\"skipped\":{\"no_depth_view\":"<<no_dsv_.load()<<",\"msaa\":"<<msaa_.load()<<"},\"failures\":"<<failures_.load()<<'}';
        return o.str();
    }
    // Around one game draw (job threads). Inert unless enabled and the thread's pass renders the alternate view. after() right after the
    // engine's draw, with every engine binding of that draw still in place.
    class Scope {
    public:
        Scope(ID3D11DeviceContext* c,std::uintptr_t pass_view) noexcept {
            tl_rig_=false;
            if(!c || !pass_view || !enabled.load(std::memory_order_relaxed) || pass_view!=view.load(std::memory_order_relaxed)) return;
            view_draws_.fetch_add(1,std::memory_order_relaxed);
            ID3D11DepthStencilState* bound{};UINT ref{};c->OMGetDepthStencilState(&bound,&ref);
            const int writes=writes_depth(bound);
            if(bound) bound->Release(); // the cache keeps its own reference
            if(writes<=0) return;
            tl_rig_=is_rig(c);
            if(tl_rig_) {rig_draws_.fetch_add(1,std::memory_order_relaxed);rig_draws_frame_.fetch_add(1,std::memory_order_relaxed);c_=c;}
        }
        void after(const DrawArgs& args,DrawFn draw,TargetsFn targets) noexcept {
            if(c_ && draw && targets) replay(c_,args,draw,targets);
            c_=nullptr;
        }
        ~Scope() {tl_rig_=false;}
        Scope(const Scope&)=delete;Scope& operator=(const Scope&)=delete;
    private:
        ID3D11DeviceContext* c_{};
    };
private:
    struct Target {ID3D11Resource* depth{};Microsoft::WRL::ComPtr<ID3D11Resource> hold;Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv;Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;UINT w{},h{};};
    struct Entry {std::atomic<ID3D11DepthStencilState*> key{};std::atomic<bool> used{};bool writes{};};
    static constexpr unsigned slots=1024;
    inline static std::array<Entry,slots> table_{};inline static std::atomic<int> default_writes_{1}; // a null state is the default: depth written
    inline static std::mutex mutex_;inline static Target target_{};
    inline static Microsoft::WRL::ComPtr<ID3D11PixelShader> ps_;inline static Microsoft::WRL::ComPtr<ID3D11DepthStencilState> equal_;
    inline static Microsoft::WRL::ComPtr<ID3D11BlendState> blend_;inline static bool made_{},make_failed_{};inline static ID3D11Device* device_{}; // identity: whose objects these are
    inline static std::array<std::atomic<std::uintptr_t>,buffer_slots> buffers_{};inline static std::atomic<unsigned> published_buffers_{};
    inline static std::atomic<std::uint64_t> rig_draws_{},rig_draws_frame_{},rig_draws_last_{},replays_{},replays_frame_{},replays_last_{},view_draws_{},
        no_dsv_{},msaa_{},failures_{};
    inline static thread_local bool tl_rig_{};

    static unsigned hash(const void* p) noexcept {return static_cast<unsigned>((reinterpret_cast<std::uintptr_t>(p)>>4)*0x9e3779b97f4a7c15ull>>54);}
    static int writes_depth(ID3D11DepthStencilState* s) noexcept { // 1 writes, 0 not, -1 table full
        if(!s) return default_writes_.load(std::memory_order_relaxed);
        const unsigned h=hash(s);
        for(unsigned i=0;i<slots;++i) {
            Entry& e=table_[(h+i)&(slots-1)];
            if(e.used.load(std::memory_order_acquire)) {if(e.key.load(std::memory_order_relaxed)==s) return e.writes?1:0;continue;}
            std::lock_guard<std::mutex> lock(mutex_);
            if(e.used.load(std::memory_order_relaxed)) {--i;continue;} // taken meanwhile: look at it again
            D3D11_DEPTH_STENCIL_DESC d{};s->GetDesc(&d);s->AddRef(); // kept: its address is never reused for another state
            e.writes=d.DepthEnable && d.DepthWriteMask==D3D11_DEPTH_WRITE_MASK_ALL;e.key.store(s,std::memory_order_relaxed);e.used.store(true,std::memory_order_release);
            return e.writes?1:0;
        }
        failures_.fetch_add(1,std::memory_order_relaxed);return -1;
    }
    static bool is_rig(ID3D11DeviceContext* c) noexcept {
        if(!published_buffers_.load(std::memory_order_relaxed)) return false;
        ID3D11ShaderResourceView* views[3]{};c->VSGetShaderResources(0,3,views);
        bool hit=false;
        for(auto* v:views) if(v) {
            ID3D11Resource* r{};v->GetResource(&r);const auto at=reinterpret_cast<std::uintptr_t>(r);
            for(const auto& b:buffers_) hit=hit || (at && b.load(std::memory_order_relaxed)==at);
            if(r) r->Release();
            v->Release();
        }
        return hit;
    }
    // Under mutex_: the shader and states (once), and the rig target for this depth resource (recreated on a new depth/size).
    static bool prepare(ID3D11Device* device,ID3D11Resource* depth,const D3D11_TEXTURE2D_DESC& dd,bool& created) {
        created=false;
        if(device_!=device) {target_={};ps_.Reset();equal_.Reset();blend_.Reset();made_=make_failed_=false;device_=device;} // a new device
        if(!made_ && !make_failed_) {
            static const char src[]="float main(float4 p:SV_Position):SV_Target{return p.z;}";
            Microsoft::WRL::ComPtr<ID3DBlob> code;
            D3D11_DEPTH_STENCIL_DESC ds{};ds.DepthEnable=TRUE;ds.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;ds.DepthFunc=D3D11_COMPARISON_EQUAL;ds.StencilEnable=FALSE;
            D3D11_BLEND_DESC bs{};bs.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL; // no blending, every channel
            make_failed_=FAILED(D3DCompile(src,sizeof src-1,nullptr,nullptr,nullptr,"main","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,nullptr)) ||
                FAILED(device->CreatePixelShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&ps_)) ||
                FAILED(device->CreateDepthStencilState(&ds,&equal_)) || FAILED(device->CreateBlendState(&bs,&blend_));
            made_=!make_failed_;
        }
        if(!made_) return false;
        if(target_.depth==depth && target_.w==dd.Width && target_.h==dd.Height && target_.rtv) return true;
        target_={};
        D3D11_TEXTURE2D_DESC t{};t.Width=dd.Width;t.Height=dd.Height;t.MipLevels=t.ArraySize=1;t.Format=DXGI_FORMAT_R32_FLOAT;t.SampleDesc.Count=1;
        t.Usage=D3D11_USAGE_DEFAULT;t.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;Target n{};
        if(FAILED(device->CreateTexture2D(&t,nullptr,&tex)) || FAILED(device->CreateRenderTargetView(tex.Get(),nullptr,&n.rtv)) ||
           FAILED(device->CreateShaderResourceView(tex.Get(),nullptr,&n.srv))) return false;
        n.depth=depth;n.hold=depth;n.w=dd.Width;n.h=dd.Height;target_=n;created=true;
        return true;
    }
    static void replay(ID3D11DeviceContext* c,const DrawArgs& a,DrawFn draw,TargetsFn targets) noexcept {
        std::array<ID3D11RenderTargetView*,D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> rtvs{};ID3D11DepthStencilView* dsv{};
        c->OMGetRenderTargets(static_cast<UINT>(rtvs.size()),rtvs.data(),&dsv);
        const auto release_targets=[&] {for(auto* r:rtvs) if(r) r->Release();if(dsv) dsv->Release();};
        if(!dsv) {no_dsv_.fetch_add(1,std::memory_order_relaxed);release_targets();return;}
        ID3D11RenderTargetView* rig{};Microsoft::WRL::ComPtr<ID3D11PixelShader> ps;Microsoft::WRL::ComPtr<ID3D11DepthStencilState> equal;Microsoft::WRL::ComPtr<ID3D11BlendState> opaque; // held: a reset cannot free them mid-replay
        {
            ID3D11Resource* depth{};dsv->GetResource(&depth);
            D3D11_TEXTURE2D_DESC dd{};ID3D11Texture2D* dt{};
            if(depth && SUCCEEDED(depth->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&dt))) && dt) {dt->GetDesc(&dd);dt->Release();}
            if(dd.SampleDesc.Count!=1) msaa_.fetch_add(1,std::memory_order_relaxed);
            else {
                ID3D11Device* device{};c->GetDevice(&device);bool created=false;
                try {
                    std::lock_guard<std::mutex> lock(mutex_);
                    if(device && dd.Width && prepare(device,depth,dd,created)) {rig=target_.rtv.Get();rig->AddRef();ps=ps_;equal=equal_;opaque=blend_;}
                } catch(...) {}
                if(device) device->Release();
                if(rig && created) {const float zero[4]{};c->ClearRenderTargetView(rig,zero);}
                if(!rig) failures_.fetch_add(1,std::memory_order_relaxed);
            }
            if(depth) depth->Release();
        }
        if(!rig) {release_targets();return;}
        UINT rtv_count=0;for(UINT i=0;i<rtvs.size();++i) if(rtvs[i]) rtv_count=i+1;
        ID3D11PixelShader* engine_ps{};std::array<ID3D11ClassInstance*,256> classes{};UINT class_count=256;c->PSGetShader(&engine_ps,classes.data(),&class_count);
        ID3D11DepthStencilState* dss{};UINT ref{};c->OMGetDepthStencilState(&dss,&ref);
        ID3D11BlendState* blend{};float factor[4]{};UINT mask{};c->OMGetBlendState(&blend,factor,&mask);
        targets(c,1,&rig,dsv);
        c->PSSetShader(ps.Get(),nullptr,0);c->OMSetDepthStencilState(equal.Get(),0);c->OMSetBlendState(opaque.Get(),nullptr,0xffffffff);
        draw(c,a);
        targets(c,rtv_count,rtvs.data(),dsv); // the engine's exact set again; the rig target is unbound
        c->PSSetShader(engine_ps,class_count?classes.data():nullptr,class_count);c->OMSetDepthStencilState(dss,ref);c->OMSetBlendState(blend,factor,mask);
        if(engine_ps) engine_ps->Release();
        for(UINT i=0;i<class_count && i<classes.size();++i) if(classes[i]) classes[i]->Release();
        if(dss) dss->Release();
        if(blend) blend->Release();
        rig->Release();release_targets();
        replays_.fetch_add(1,std::memory_order_relaxed);replays_frame_.fetch_add(1,std::memory_order_relaxed);
    }
};
} // namespace d2vr
