#pragma once
#include <windows.h>
#include <d3d11.h>
#include <MinHook.h>
#include <array>
#include <optional>
#include <atomic>

namespace d2vr {
// Deferred-context Dispatch hook for the native TAA dispatch. The engine records the post-process
// pass (0x327270) on a job thread into that job's deferred context; NativeTaaHistory marks the
// thread with the eye for the duration of the pass (NativeTaaHistory::current_eye), so every
// other Dispatch in the process passes straight through after one thread_local read.
// Bindings of the TAA compute shader (Luma D2 map, CS 06BBC941/8EDF67D9): t0 history read,
// t1 motion vectors (UV), t2 pre-TAA colour, t3 exposure structured buffer, u0 history write,
// u1 result, b1 per-view globals (CBPerViewGlobals), b2 per-instance constants.
struct TaaBindings {
    ID3D11ShaderResourceView* srv[4]{};ID3D11UnorderedAccessView* uav[2]{};ID3D11Buffer* cb[2]{};
    ID3D11Resource* res[6]{}; // t0..t3,u0,u1
    D3D11_TEXTURE2D_DESC tex[6]{};bool is_tex[6]{};
    void read(ID3D11DeviceContext* c) noexcept {
        c->CSGetShaderResources(0,4,srv);c->CSGetUnorderedAccessViews(0,2,uav);c->CSGetConstantBuffers(1,2,cb);
        for(int i=0;i<6;++i) {
            if(i<4?!srv[i]:!uav[i-4]) continue;
            if(i<4) srv[i]->GetResource(&res[i]);else uav[i-4]->GetResource(&res[i]);
            ID3D11Texture2D* t{};
            if(res[i] && SUCCEEDED(res[i]->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&t)))) {t->GetDesc(&tex[i]);is_tex[i]=true;t->Release();}
        }
    }
    // TAA signature: four SRVs (t3 a buffer), two same-size UAV textures matching the colour input.
    bool taa() const noexcept {
        return srv[0] && srv[1] && srv[2] && srv[3] && uav[0] && uav[1] && cb[0] && is_tex[0] && is_tex[1] && is_tex[2] && !is_tex[3] &&
            is_tex[4] && is_tex[5] && tex[5].Width==tex[2].Width && tex[5].Height==tex[2].Height &&
            tex[4].Width==tex[5].Width && tex[4].Height==tex[5].Height;
    }
    ~TaaBindings() {
        for(auto* p:srv) if(p) p->Release();
        for(auto* p:uav) if(p) p->Release();
        for(auto* p:cb) if(p) p->Release();
        for(auto* p:res) if(p) p->Release();
    }
};
class TaaDispatchHook {
public:
    using Fn=void (STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT,UINT);
    // Called inside a marked pass; returns true when it recorded a replacement instead of the original.
    using Handler=bool (*)(ID3D11DeviceContext*,int eye,UINT,UINT,UINT,Fn original);
    inline static std::atomic<Handler> handler{};
    inline static std::atomic<UINT64> marked{},replaced{};
    inline static thread_local int pass_eye=-1;
    inline static thread_local std::optional<std::array<float,4>> pass_jitter; // set by NativeTaaHistory around a planned eye pass
    inline static thread_local bool post_taa=false; // the TAA dispatch of this pass has been recorded
    inline static thread_local ID3D11DeviceContext* pass_context=nullptr; // deferred context seen at that dispatch
    // Called on the job thread when a marked pass returns (eye, its deferred context or null).
    inline static std::atomic<void (*)(int,ID3D11DeviceContext*)> pass_end{};
    // Depth-stencil clears (scene depth is cleared once per eye); any context, any thread.
    using ClearFn=void (STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11DepthStencilView*,UINT,FLOAT,UINT8);
    inline static std::atomic<void (*)(ID3D11DepthStencilView*)> depth_observer{};
    // Command-list split (Luma's D2 pattern): FinishCommandList results of deferred contexts, and
    // ExecuteCommandList on the immediate context, which a handler may take over (returns true).
    using FinishFn=HRESULT (STDMETHODCALLTYPE*)(ID3D11DeviceContext*,BOOL,ID3D11CommandList**);
    using ExecuteFn=void (STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11CommandList*,BOOL);
    inline static std::atomic<void (*)(ID3D11DeviceContext*,ID3D11CommandList*)> finish_observer{};
    inline static std::atomic<bool (*)(ID3D11DeviceContext*,ID3D11CommandList*,BOOL,ExecuteFn)> execute_handler{};
    inline static FinishFn finish_original{};inline static ExecuteFn execute_original{};
private:
    inline static std::array<void*,2> list_target_{};
    static HRESULT STDMETHODCALLTYPE finish_hook(ID3D11DeviceContext* c,BOOL restore,ID3D11CommandList** list) {
        const HRESULT hr=finish_original(c,restore,list);
        if(SUCCEEDED(hr) && list && *list) if(const auto o=finish_observer.load()) o(c,*list);
        return hr;
    }
    static void STDMETHODCALLTYPE execute_hook(ID3D11DeviceContext* c,ID3D11CommandList* list,BOOL restore) {
        if(const auto h=execute_handler.load()) if(h(c,list,restore,execute_original)) return;
        execute_original(c,list,restore);
    }
    template<class Fn> static void hook_slot(ID3D11DeviceContext* context,unsigned slot,void* replacement,Fn& original,void*& target_out) noexcept {
        void* target=(*reinterpret_cast<void***>(context))[slot];
        if(target_out==target && original) {MH_EnableHook(target);return;}
        if(MH_CreateHook(target,replacement,reinterpret_cast<void**>(&original))!=MH_OK) return;
        if(MH_EnableHook(target)!=MH_OK) {MH_RemoveHook(target);original=nullptr;return;}
        target_out=target;
    }
    inline static std::array<ClearFn,2> clear_original_{};inline static std::array<void*,2> clear_target_{};
    template<int I> static void STDMETHODCALLTYPE clear_hook(ID3D11DeviceContext* c,ID3D11DepthStencilView* v,UINT f,FLOAT d,UINT8 s) {
        if(const auto o=depth_observer.load()) o(v);
        clear_original_[I](c,v,f,d,s);
    }
    static void hook_clear(ID3D11DeviceContext* context,int i,void* replacement) noexcept {
        void* target=(*reinterpret_cast<void***>(context))[53]; // ClearDepthStencilView
        if(clear_target_[0]==target || clear_target_[1]==target) return;
        if(MH_CreateHook(target,replacement,reinterpret_cast<void**>(&clear_original_[static_cast<std::size_t>(i)]))!=MH_OK) return;
        if(MH_EnableHook(target)!=MH_OK) {MH_RemoveHook(target);return;}
        clear_target_[static_cast<std::size_t>(i)]=target;
    }
    inline static Fn original_{};inline static void* target_{};inline static std::atomic<int> depth_{};
    static void STDMETHODCALLTYPE hook(ID3D11DeviceContext* c,UINT x,UINT y,UINT z) {
        const int eye=pass_eye;
        if(eye<0) {original_(c,x,y,z);return;}
        ++depth_;++marked;
        pass_context=c;
        const auto h=handler.load();
        if(!h || !h(c,eye,x,y,z,original_)) original_(c,x,y,z);else ++replaced;
        --depth_;
    }
public:
    static bool install(ID3D11Device* device) noexcept {
        if(target_) {for(auto* t:clear_target_) if(t) MH_EnableHook(t);for(auto* t:list_target_) if(t) MH_EnableHook(t);return MH_EnableHook(target_)==MH_OK;}
        if(!device) return false;
        ID3D11DeviceContext* deferred{};
        if(FAILED(device->CreateDeferredContext(0,&deferred))) return false;
        void* target=(*reinterpret_cast<void***>(deferred))[41]; // ID3D11DeviceContext::Dispatch
        hook_clear(deferred,0,reinterpret_cast<void*>(&clear_hook<0>));
        hook_slot(deferred,114,reinterpret_cast<void*>(&finish_hook),finish_original,list_target_[0]); // FinishCommandList
        deferred->Release();
        ID3D11DeviceContext* immediate{};device->GetImmediateContext(&immediate);
        if(immediate) {
            hook_clear(immediate,1,reinterpret_cast<void*>(&clear_hook<1>));
            hook_slot(immediate,58,reinterpret_cast<void*>(&execute_hook),execute_original,list_target_[1]); // ExecuteCommandList
            immediate->Release();
        }
        // A replacement can split the deferred list only when both ends of the split are intercepted.
        if(!list_target_[0] || !list_target_[1] || !finish_original || !execute_original) return false;
        if(MH_CreateHook(target,reinterpret_cast<void*>(&hook),reinterpret_cast<void**>(&original_))!=MH_OK) {original_=nullptr;return false;}
        if(MH_EnableHook(target)!=MH_OK) {MH_RemoveHook(target);original_=nullptr;return false;}
        target_=target;return true;
    }
    static bool disable() noexcept {
        handler=nullptr;depth_observer=nullptr;finish_observer=nullptr;execute_handler=nullptr;
        for(auto* t:clear_target_) if(t) MH_DisableHook(t);
        for(auto* t:list_target_) if(t) MH_DisableHook(t);
        if(!target_) return true;
        if(MH_DisableHook(target_)!=MH_OK) return false;
        const auto deadline=GetTickCount64()+2000;while(depth_.load() && GetTickCount64()<deadline) Sleep(1);
        return depth_.load()==0;
    }
    static bool installed() noexcept {return target_!=nullptr && handler.load()!=nullptr;}
    static Fn original() noexcept {return original_;}
};
} // namespace d2vr
