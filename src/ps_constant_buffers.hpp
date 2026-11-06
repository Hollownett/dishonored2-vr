#pragma once
// v993: the one owner of PSSetConstantBuffers (slot 16). Like SamplerLodBias::hook_table it hooks the deferred and the
// immediate vtable implementation, skips a target that is already hooked, and leaves the feature behaviour untouched: the
// hook itself is a passthrough that only offers the final arguments to the PS state census.
//
// v995: the same owner also hooks the ID3D11DeviceContext1 methods that reach the PS constant-buffer slots:
//   - slot 123 PSSetConstantBuffers1 (both context kinds). It is NEVER elided: its first/num-constant window is state the
//     base shadow does not hold. The hook counts the call (ps_state.cb1_calls) and makes the census mark the covered CB
//     slots unknown, so a later base PSSetConstantBuffers to the same buffer pointer is forwarded when the offsets differ.
//   - slot 131 SwapDeviceContextState (immediate only; Microsoft documents it as not supported on a deferred context). It can
//     replace every PS binding of the immediate context without a setter call. v996: the hook barriers that context before and
//     after the original call and reports the caller site; it no longer refuses elision for the session, and the deferred
//     shadow/readiness are untouched.
// One MinHook per code address: the target scan below covers both context kinds and all three slots.
//
// Slots proved by tests/context_slot_probe.cpp on a WARP device, immediate and deferred (2026-11-04, v995 rows 123/131).
#include <windows.h>
#include <d3d11.h>
#include <d3d11_1.h>
#include <MinHook.h>
#include <array>
#include <atomic>
#include <intrin.h>

namespace d2vr {
class PsConstantBufferHook {
    using SetFn=void (STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT,ID3D11Buffer* const*);
    using Set1Fn=void (STDMETHODCALLTYPE*)(ID3D11DeviceContext1*,UINT,UINT,ID3D11Buffer* const*,const UINT*,const UINT*);
    using SwapFn=void (STDMETHODCALLTYPE*)(ID3D11DeviceContext1*,ID3DDeviceContextState*,ID3DDeviceContextState**);
public:
    // True: the census proved the call redundant and elision is on; do not forward it.
    using Filter=bool (*)(ID3D11DeviceContext*,UINT,UINT,ID3D11Buffer* const*,bool) noexcept;
    // v995: PSSetConstantBuffers1 is never elided; this observer only counts it and invalidates the covered CB slots.
    using Cb1Observer=void (*)(ID3D11DeviceContext*,UINT,UINT,unsigned) noexcept;
    // v995: one sighting of SwapDeviceContextState makes the census refuse elision for the rest of the session.
    // v996: the observer is called before and after the original call with (context, return address, after). The owner barriers
    // the swapping context at both edges and counts/records the call site; it never refuses elision.
    using StateSwapObserver=void (*)(ID3D11DeviceContext*,void*,bool) noexcept;
    inline static std::atomic<Filter> filter{};
    inline static std::atomic<Cb1Observer> cb1_observer{};
    inline static std::atomic<StateSwapObserver> state_swap_observer{};
private:
    enum Slot : unsigned {base_cb=0,cb1=1,swap_state=2,slot_count=3};
    static unsigned vtable_slot(unsigned slot) noexcept {
        static const unsigned slots[slot_count]{16,123,131}; // PSSetConstantBuffers, PSSetConstantBuffers1, SwapDeviceContextState
        return slot<slot_count?slots[slot]:0u;
    }
    inline static std::array<std::array<void*,2>,slot_count> target_{};
    inline static std::array<std::array<void*,2>,slot_count> original_{};
    inline static std::array<std::array<bool,2>,slot_count> covered_{};
    inline static std::atomic<ID3D11DeviceContext*> immediate_context_{nullptr};
    inline static std::atomic<ID3D11DeviceContext1*> immediate_context1_{nullptr};
    inline static std::atomic<bool> enabled_{false};
    static bool is_deferred(ID3D11DeviceContext* c) noexcept {
        // The engine only has one immediate context; every other context is deferred. This avoids a GetType() call on the
        // budget of every PS constant-buffer bind. The Context1 interface pointer is normally the same address; compare it too
        // so a runtime that returns a different interface pointer cannot misclassify the immediate context.
        if(c==immediate_context_.load(std::memory_order_relaxed)) return false;
        auto* immediate1=immediate_context1_.load(std::memory_order_relaxed);
        return !immediate1 || reinterpret_cast<ID3D11DeviceContext*>(immediate1)!=c;
    }
    template<int I> static void STDMETHODCALLTYPE base_hook(ID3D11DeviceContext* c,UINT start,UINT count,ID3D11Buffer* const* buffers) {
        if(const auto f=filter.load(std::memory_order_relaxed)) if(f(c,start,count,buffers,is_deferred(c))) return;
        reinterpret_cast<SetFn>(original_[base_cb][I])(c,start,count,buffers);
    }
    template<int I> static void STDMETHODCALLTYPE cb1_hook(ID3D11DeviceContext1* c,UINT start,UINT count,ID3D11Buffer* const* buffers,
                                                           const UINT* first,const UINT* num) {
        auto* base=reinterpret_cast<ID3D11DeviceContext*>(c);
        if(const auto observe=cb1_observer.load(std::memory_order_relaxed))
            observe(base,start,count,is_deferred(base)?1u:0u);
        reinterpret_cast<Set1Fn>(original_[cb1][I])(c,start,count,buffers,first,num); // never elided, whatever the census says
    }
    template<int I> static void STDMETHODCALLTYPE swap_state_hook(ID3D11DeviceContext1* c,ID3DDeviceContextState* state,
                                                                  ID3DDeviceContextState** previous) {
        auto* base=reinterpret_cast<ID3D11DeviceContext*>(c);
        const auto observe=state_swap_observer.load(std::memory_order_relaxed);
        void* caller=_ReturnAddress();
        if(observe) observe(base,caller,false); // before: the old state is still current
        reinterpret_cast<SwapFn>(original_[swap_state][I])(c,state,previous);
        if(observe) observe(base,caller,true);  // after: the context may hold a different state now
    }
    static void* replacement(unsigned slot,int kind) noexcept {
        switch(slot) {
            case base_cb: return kind==0?reinterpret_cast<void*>(&base_hook<0>):reinterpret_cast<void*>(&base_hook<1>);
            case cb1: return kind==0?reinterpret_cast<void*>(&cb1_hook<0>):reinterpret_cast<void*>(&cb1_hook<1>);
            case swap_state: return kind==0?reinterpret_cast<void*>(&swap_state_hook<0>):reinterpret_cast<void*>(&swap_state_hook<1>);
            default: return nullptr;
        }
    }
    static bool hook_table(ID3D11DeviceContext* context,int kind,unsigned slot) noexcept {
        const unsigned index=vtable_slot(slot);
        void* target=(*reinterpret_cast<void***>(context))[index];
        if(!target) return false;
        for(unsigned s=0;s<slot_count;++s) for(int k=0;k<2;++k) {
            if(target_[s][static_cast<std::size_t>(k)]!=target) continue;
            if(s==slot) {
                // The deferred and immediate vtables can share one implementation; the installed hook covers both then.
                covered_[s][static_cast<std::size_t>(kind)]=true;
                covered_[s][static_cast<std::size_t>(k)]=true;
                MH_EnableHook(target);
            }
            return true; // never create a second hook on one code address (the v992 poisoning rule)
        }
        void* original{};
        if(MH_CreateHook(target,replacement(slot,kind),&original)!=MH_OK) return false;
        if(MH_EnableHook(target)!=MH_OK) {MH_RemoveHook(target);return false;}
        target_[slot][static_cast<std::size_t>(kind)]=target;
        original_[slot][static_cast<std::size_t>(kind)]=original;
        covered_[slot][static_cast<std::size_t>(kind)]=true;
        return true;
    }
    static void hook_context(ID3D11DeviceContext* context,int kind) noexcept {
        hook_table(context,kind,base_cb);
        ID3D11DeviceContext1* context1{};
        if(SUCCEEDED(context->QueryInterface(__uuidof(ID3D11DeviceContext1),reinterpret_cast<void**>(&context1))) && context1) {
            if(kind!=0) immediate_context1_.store(context1,std::memory_order_relaxed);
            hook_table(reinterpret_cast<ID3D11DeviceContext*>(context1),kind,cb1);
            if(kind!=0) hook_table(reinterpret_cast<ID3D11DeviceContext*>(context1),kind,swap_state);
            else covered_[swap_state][0]=true; // documented as not supported on a deferred context: it cannot write this state
            context1->Release();
        }
    }
public:
    // Render thread; MinHook must be initialised. Safe to call repeatedly.
    static void install(ID3D11Device* device) noexcept {
        if(target_[base_cb][0] || target_[base_cb][1] || target_[cb1][0] || target_[cb1][1] ||
           target_[swap_state][0] || target_[swap_state][1]) {
            if(device) {ID3D11DeviceContext* immediate{};device->GetImmediateContext(&immediate);
                if(immediate) {immediate_context_.store(immediate,std::memory_order_relaxed);immediate->Release();}}
            for(auto& slot:target_) for(auto* t:slot) if(t) MH_EnableHook(t);
            enabled_=true;
            return;
        }
        if(!device) return;
        ID3D11DeviceContext* immediate{};device->GetImmediateContext(&immediate);
        if(immediate) immediate_context_.store(immediate,std::memory_order_relaxed);
        ID3D11DeviceContext* deferred{};
        if(SUCCEEDED(device->CreateDeferredContext(0,&deferred))) {hook_context(deferred,0);deferred->Release();}
        if(immediate) {
            hook_context(immediate,1);
            immediate->Release();
        }
        enabled_=false;
        for(const auto& slot:covered_) for(bool c:slot) enabled_=enabled_||c;
    }
    // The census may elide only when the base and CB1 routes are both observed for that kind. SwapDeviceContextState is
    // immediate-only; on a deferred context the documented no-op is covered by construction.
    static bool ready_base(bool deferred) noexcept {return enabled_.load(std::memory_order_relaxed) && covered_[base_cb][deferred?0u:1u];}
    static bool ready_cb1(bool deferred) noexcept {return enabled_.load(std::memory_order_relaxed) && covered_[cb1][deferred?0u:1u];}
    static bool ready_swap(bool deferred) noexcept {return deferred || (enabled_.load(std::memory_order_relaxed) && covered_[swap_state][1]);}
    static const char* route_reason(bool deferred) noexcept {
        if(!enabled_.load(std::memory_order_relaxed)) return "PS constant-buffer hooks not installed";
        if(!ready_base(deferred)) return "PSSetConstantBuffers hook not installed";
        if(!ready_cb1(deferred)) return "PSSetConstantBuffers1 hook not installed";
        if(!ready_swap(deferred)) return "SwapDeviceContextState hook not installed";
        return "";
    }
    static void disable() noexcept {
        enabled_=false;
        for(auto& slot:target_) for(auto* t:slot) if(t) MH_DisableHook(t);
    }
};
} // namespace d2vr
