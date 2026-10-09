#pragma once
#include <MinHook.h>
#include <windows.h>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstring>

namespace d2vr {
// Caller serializes registry mutation. Published originals are immutable, and
// neither hook records nor trampolines are removed during the DLL's lifetime.
template<std::size_t Methods=7> class GpuDispatchBindings {
public:
    static constexpr std::size_t capacity=4;
    using Targets=std::array<void*,Methods>;
    using Wrappers=std::array<std::array<void*,capacity>,Methods>;
    using Bytes=std::array<std::byte,16>;
    struct Api {
        decltype(&MH_CreateHook) create=&MH_CreateHook;
        decltype(&MH_QueueEnableHook) enable=&MH_QueueEnableHook;
        decltype(&MH_QueueDisableHook) disable=&MH_QueueDisableHook;
        decltype(&MH_ApplyQueued) apply=&MH_ApplyQueued;
        decltype(&MH_DisableHook) immediate_disable=&MH_DisableHook;
    };
private:
    struct Binding {
        void* target{};
        std::atomic<void*> original{};
        Bytes before{},patched{};
        bool created{},enabled{};
    };
    std::array<std::array<Binding,capacity>,Methods> bindings_{};
    Targets active_{};
    Api api_;
    std::atomic<bool> poisoned_{false};
    const char* reason_="unbound";
    unsigned rebinds_{},reuses_{},created_{};
    bool fail(const char* reason) noexcept {reason_=reason;poisoned_=true;return false;}
public:
    explicit GpuDispatchBindings(Api api=Api{}) :api_(api) {}
    bool poisoned() const noexcept {return poisoned_.load(std::memory_order_acquire);}
    const char* reason() const noexcept {return reason_;}
    const Targets& active() const noexcept {return active_;}
    unsigned rebinds() const noexcept {return rebinds_;}
    unsigned reuses() const noexcept {return reuses_;}
    unsigned created() const noexcept {return created_;}
    void* original(std::size_t method,std::size_t binding) const noexcept {
        return bindings_[method][binding].original.load(std::memory_order_acquire);
    }
    template<class Read,class Validate,class Snapshot>
    bool bind(const Targets& targets,const Wrappers& wrappers,Read read,Validate valid,Snapshot snapshot) {
        if(poisoned()) {reason_="registry poisoned; restart process";return false;}
        std::array<std::size_t,Methods> selected{};
        for(std::size_t method=0;method<Methods;++method) {
            if(!targets[method] || !valid(targets[method])) return fail("unverified executable graphics target");
            for(std::size_t other=0;other<Methods;++other) if(other!=method) {
                if(targets[other]==targets[method]) return fail("incompatible duplicate method target");
                for(const auto& b:bindings_[other]) if(b.created && b.target==targets[method])
                    return fail("target belongs to another method");
            }
            std::size_t found=capacity,free=capacity;
            for(std::size_t i=0;i<capacity;++i) {
                const auto& b=bindings_[method][i];
                if(b.created && b.target==targets[method]) found=i;
                else if(!b.created && free==capacity) free=i;
                if(b.created) {
                    Bytes bytes{};
                    if(!valid(b.target) || !read(b.target,bytes.data(),bytes.size()) || bytes!=(b.enabled?b.patched:b.before))
                        return fail("retained graphics target bytes changed");
                }
            }
            selected[method]=found==capacity?free:found;
            if(selected[method]==capacity) return fail("four-target graphics binding budget exhausted");
        }
        bool all_enabled=targets==active_;
        for(std::size_t method=0;method<Methods;++method)
            all_enabled=all_enabled && bindings_[method][selected[method]].enabled;
        if(all_enabled) {reason_="unchanged";return true;}
        const bool changed=active_[0] && targets!=active_;
        for(std::size_t method=0;method<Methods;++method) {
            auto& b=bindings_[method][selected[method]];
            if(b.created) {if(!b.enabled) ++reuses_;continue;}
            if(!wrappers[method][selected[method]] || !read(targets[method],b.before.data(),b.before.size()))
                return fail("cannot inspect new graphics target");
            void* original{};
            if(api_.create(targets[method],wrappers[method][selected[method]],&original)!=MH_OK)
                return fail("graphics hook creation failed");
            b.target=targets[method];b.created=true;++created_;
            b.original.store(original,std::memory_order_release); // Published exactly once, before enabling.
            if(!original) return fail("graphics trampoline missing");
        }
        Targets current{};
        if(!snapshot(current) || current!=targets) return fail("context dispatch changed while preparing hooks");
        for(std::size_t method=0;method<Methods;++method) for(std::size_t i=0;i<capacity;++i) {
            auto& b=bindings_[method][i];if(!b.created) continue;
            const auto status=i==selected[method]?api_.enable(b.target):api_.disable(b.target);
            if(status!=MH_OK) return fail("graphics hook queue failed");
        }
        if(api_.apply()!=MH_OK) return fail("graphics hook batch apply failed");
        for(std::size_t method=0;method<Methods;++method) for(std::size_t i=0;i<capacity;++i) {
            auto& b=bindings_[method][i];if(!b.created) continue;
            b.enabled=i==selected[method];
            Bytes bytes{};
            if(!read(b.target,bytes.data(),bytes.size())) return fail("cannot verify applied graphics hook");
            if(b.enabled) b.patched=bytes;
            else if(bytes!=b.before) return fail("disabled graphics hook bytes not restored");
        }
        active_=targets;if(changed) ++rebinds_;reason_=changed?"accepted dispatch transition":"accepted binding";
        return true;
    }
    template<class Read> bool disable_all(std::atomic<UINT>& depth,Read read,DWORD timeout_ms=1000) noexcept {
        bool okay=true;
        // Immediate disable also clears queued enables, including after partial batch failure.
        for(auto& method:bindings_) for(auto& b:method) if(b.created) {
            if(api_.disable(b.target)!=MH_OK) okay=false;
            const auto status=api_.immediate_disable(b.target);
            if(status==MH_OK || status==MH_ERROR_DISABLED) b.enabled=false;
            else okay=false;
        }
        const auto deadline=GetTickCount64()+timeout_ms;
        while(depth.load(std::memory_order_acquire) && GetTickCount64()<deadline) Sleep(1);
        okay=okay && !depth.load(std::memory_order_acquire);
        for(const auto& method:bindings_) for(const auto& b:method) if(b.created) {
            Bytes bytes{};
            if(!read(b.target,bytes.data(),bytes.size()) || bytes!=b.before) okay=false;
        }
        if(!okay) fail("graphics disable, drain or restoration failed");
        return okay;
    }
};
} // namespace d2vr
