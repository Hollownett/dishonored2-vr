#pragma once
// v993 engine perf step 2: the pass-boundary barriers the PS state census needs, for the setters no other component owns.
// Slots 110 ClearState, 68 CSSetUnorderedAccessViews and 37 SOSetTargets are hooked on the deferred AND the immediate vtable
// implementation; each hook calls barrier(context) before forwarding.
//
// OMSetRenderTargets / OMSetRenderTargetsAndUnorderedAccessViews (33/34) are deliberately NOT hooked here. GpuDispatchBindings
// owns those targets on the immediate context and binds a deferred target set too (begin_native_hud_art, "UI uses the game's own
// deferred context"). A hook created here on the deferred implementation would make that later bind fail with
// MH_ERROR_ALREADY_CREATED, which poisons every GPU hook for the session - the v992 failure in reverse. The barrier for both
// kinds comes from the registry's own targets_hook (note_rt_bind), which barriers on every OM bind; the census only treats a
// kind as ready once that owner reports it is bound for it (GpuDispatchBindings::bound_targets).
//
// Slots proved by tests/context_slot_probe.cpp on a WARP device, immediate and deferred (2026-11-04).
#include <windows.h>
#include <d3d11.h>
#include <MinHook.h>
#include <array>
#include <atomic>

namespace d2vr {
class ContextBarriers {
public:
    using Barrier=void (*)(ID3D11DeviceContext*) noexcept;
    inline static std::atomic<Barrier> barrier{};
    static void install(ID3D11Device* device) noexcept;
    static void disable() noexcept;
    // The three slots this class owns are hooked for that context kind.
    static bool ready(bool deferred) noexcept {
        if(!enabled_.load(std::memory_order_relaxed)) return false;
        const std::size_t kind=deferred?0u:1u;
        return covered_[clear_state][kind] && covered_[cs_uav][kind] && covered_[so_targets][kind];
    }
    // v995: the census tracks the four output-binding hazards separately. These expose the two this class owns (68 and 37)
    // plus ClearState, which is a pass barrier rather than an output route.
    static bool clear_state_ready(bool deferred) noexcept {
        return enabled_.load(std::memory_order_relaxed) && covered_[clear_state][deferred?0u:1u];
    }
    static bool cs_uav_ready(bool deferred) noexcept {
        return enabled_.load(std::memory_order_relaxed) && covered_[cs_uav][deferred?0u:1u];
    }
    static bool so_targets_ready(bool deferred) noexcept {
        return enabled_.load(std::memory_order_relaxed) && covered_[so_targets][deferred?0u:1u];
    }
    static const char* reason(bool deferred) noexcept {
        if(!enabled_.load(std::memory_order_relaxed)) return "barrier hooks not installed";
        const std::size_t kind=deferred?0u:1u;
        if(!covered_[clear_state][kind]) return "ClearState hook not installed";
        if(!covered_[cs_uav][kind]) return "CSSetUnorderedAccessViews hook not installed";
        if(!covered_[so_targets][kind]) return "SOSetTargets hook not installed";
        return "";
    }
private:
    enum Slot : unsigned {clear_state=0,cs_uav=1,so_targets=2,slot_count=3};
    static unsigned vtable_slot(unsigned slot) noexcept {
        static const unsigned slots[slot_count]{110,68,37};
        return slot<slot_count?slots[slot]:0u;
    }
    using ClearStateFn=void (STDMETHODCALLTYPE*)(ID3D11DeviceContext*);
    using CsUavFn=void (STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT,ID3D11UnorderedAccessView* const*,const UINT*);
    using SoTargetsFn=void (STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,ID3D11Buffer* const*,const UINT*);
    inline static std::array<std::array<void*,2>,slot_count> target_{};
    inline static std::array<std::array<void*,2>,slot_count> original_{};
    inline static std::array<std::array<bool,2>,slot_count> covered_{};
    inline static std::atomic<bool> enabled_{false};
    static void call_barrier(ID3D11DeviceContext* context) noexcept {
        if(const auto b=barrier.load(std::memory_order_relaxed)) b(context);
    }
    template<int I> static void STDMETHODCALLTYPE clear_hook(ID3D11DeviceContext* c) {
        call_barrier(c);
        reinterpret_cast<ClearStateFn>(original_[clear_state][I])(c);
    }
    template<int I> static void STDMETHODCALLTYPE cs_uav_hook(ID3D11DeviceContext* c,UINT start,UINT count,ID3D11UnorderedAccessView* const* uavs,const UINT* initial) {
        call_barrier(c);
        reinterpret_cast<CsUavFn>(original_[cs_uav][I])(c,start,count,uavs,initial);
    }
    template<int I> static void STDMETHODCALLTYPE so_hook(ID3D11DeviceContext* c,UINT count,ID3D11Buffer* const* buffers,const UINT* offsets) {
        call_barrier(c);
        reinterpret_cast<SoTargetsFn>(original_[so_targets][I])(c,count,buffers,offsets);
    }
    static void* replacement(unsigned slot,int kind) noexcept {
        switch(slot) {
            case clear_state: return kind==0?reinterpret_cast<void*>(&clear_hook<0>):reinterpret_cast<void*>(&clear_hook<1>);
            case cs_uav: return kind==0?reinterpret_cast<void*>(&cs_uav_hook<0>):reinterpret_cast<void*>(&cs_uav_hook<1>);
            case so_targets: return kind==0?reinterpret_cast<void*>(&so_hook<0>):reinterpret_cast<void*>(&so_hook<1>);
            default: return nullptr;
        }
    }
    static void hook_table(ID3D11DeviceContext* context,int kind,unsigned slot) noexcept {
        const unsigned index=vtable_slot(slot);
        if(!index) return;
        void* target=(*reinterpret_cast<void***>(context))[index];
        if(!target) return;
        for(unsigned s=0;s<slot_count;++s) for(int k=0;k<2;++k) {
            if(target_[s][static_cast<std::size_t>(k)]!=target) continue;
            if(s==slot) {
                // The same implementation serves this slot on both context kinds: the one hook covers both.
                covered_[s][static_cast<std::size_t>(kind)]=true;
                covered_[s][static_cast<std::size_t>(k)]=true;
                MH_EnableHook(target);
            }
            return; // never create a second hook on this code address
        }
        void* original{};
        if(MH_CreateHook(target,replacement(slot,kind),&original)!=MH_OK) return; // another owner has it: stay unhooked and report
        if(MH_EnableHook(target)!=MH_OK) {MH_RemoveHook(target);return;}
        target_[slot][static_cast<std::size_t>(kind)]=target;
        original_[slot][static_cast<std::size_t>(kind)]=original;
        covered_[slot][static_cast<std::size_t>(kind)]=true;
    }
};

inline void ContextBarriers::install(ID3D11Device* device) noexcept {
    if(target_[clear_state][0] || target_[clear_state][1] || target_[cs_uav][0] || target_[cs_uav][1] ||
       target_[so_targets][0] || target_[so_targets][1]) {
        for(auto& slot:target_) for(auto* t:slot) if(t) MH_EnableHook(t);
        enabled_=true;return;
    }
    if(!device) return;
    if(ID3D11DeviceContext* deferred{};SUCCEEDED(device->CreateDeferredContext(0,&deferred))) {
        hook_table(deferred,0,clear_state);hook_table(deferred,0,cs_uav);hook_table(deferred,0,so_targets);
        deferred->Release();
    }
    ID3D11DeviceContext* immediate{};
    device->GetImmediateContext(&immediate);
    if(immediate) {
        hook_table(immediate,1,clear_state);hook_table(immediate,1,cs_uav);hook_table(immediate,1,so_targets);
        immediate->Release();
    }
    enabled_=target_[clear_state][0] || target_[clear_state][1] || target_[cs_uav][0] || target_[cs_uav][1] ||
        target_[so_targets][0] || target_[so_targets][1];
}
inline void ContextBarriers::disable() noexcept {
    enabled_=false;
    for(auto& slot:target_) for(auto* t:slot) if(t) MH_DisableHook(t);
}
} // namespace d2vr
