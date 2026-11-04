#pragma once
#include <MinHook.h>
#include <windows.h>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstring>

namespace d2vr {
// In-flight hook callback count, one cache line per thread slot: every engine draw enters and leaves a hook, from ~9 render
// workers at once, and one shared atomic bounced its cache line between cores on both edges (v983 profile: the two locked
// instructions were the mod's hottest spots in the draw hooks). A callback enters and leaves on one thread, so its stripe is
// stable; load() sums the stripes (shutdown only). Threads past 64 share stripes, which stays correct.
class StripedDepth {
#pragma warning(suppress:4324) // the padding is the point
    struct alignas(64) Slot {std::atomic<UINT> n{0};};
    std::array<Slot,64> slots_{};
    static Slot& mine(std::array<Slot,64>& s) noexcept {
        static std::atomic<unsigned> next{0};
        thread_local const unsigned index=next.fetch_add(1,std::memory_order_relaxed)%64;
        return s[index];
    }
public:
    void fetch_add(UINT v,std::memory_order o=std::memory_order_acq_rel) noexcept {mine(slots_).n.fetch_add(v,o);}
    void fetch_sub(UINT v,std::memory_order o=std::memory_order_acq_rel) noexcept {mine(slots_).n.fetch_sub(v,o);}
    UINT load(std::memory_order o=std::memory_order_acquire) const noexcept {
        UINT total=0;for(const auto& s:slots_) total+=s.n.load(o);
        return total;
    }
};
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
    bool simultaneous_targets_{};
    std::atomic<bool> poisoned_{false};
    const char* reason_="unbound";
    unsigned rebinds_{},reuses_{},created_{},fast_paths_{},verifications_{};
    std::uint64_t verified_at_{};
    bool enabled_{}; // false after disable_all: an unchanged binding must still be re-enabled by the next bind
    // v984: the native HUD validation calls bind() once per HUD child draw (dozens per frame, all with the same seven context
    // methods). Every call re-verified up to Methods x capacity retained hooks with one ReadProcessMemory each and re-checked each
    // target's code page (verified_d3d11_target): the 2026-11-03 profile put that scope at hud_validate 1.51 ms per frame, 58% of
    // the mod's whole CPU tax and 13.6% of the 90 Hz budget. The same targets cannot change between two HUD children of one frame,
    // so an unchanged, already-enabled target set is re-verified at most every revalidate_ms; any change still runs the full check.
    static constexpr std::uint64_t revalidate_ms=250;
    // The immediate context and the native HUD's deferred context stay enabled together (simultaneous_targets_), so two target sets
    // alternate: remember when each was last fully verified, and otherwise answer from that (still re-verified every revalidate_ms).
    struct Verified {Targets set{};std::uint64_t at{};};
    std::array<Verified,4> verified_sets_{};
    unsigned verified_next_{};
    void note_verified(const Targets& targets) noexcept {
        verified_at_=GetTickCount64();++verifications_;
        for(auto& v:verified_sets_) if(v.at && v.set==targets) {v.at=verified_at_;return;}
        auto& v=verified_sets_[verified_next_++%verified_sets_.size()];v.set=targets;v.at=verified_at_;
    }
    bool recently_verified(const Targets& targets) const noexcept {
        const auto now=GetTickCount64();
        if(!enabled_ || !verified_at_ || now-verified_at_>=revalidate_ms) return false;
        if(targets==active_) return true;
        if(!simultaneous_targets_) return false;
        for(const auto& v:verified_sets_) if(v.at && v.set==targets && now-v.at<revalidate_ms) return true;
        return false;
    }
    bool fail(const char* reason) noexcept {reason_=reason;poisoned_=true;return false;}
public:
    explicit GpuDispatchBindings(Api api=Api{},bool simultaneous_targets=false)
        :api_(api),simultaneous_targets_(simultaneous_targets) {}
    bool poisoned() const noexcept {return poisoned_.load(std::memory_order_acquire);}
    const char* reason() const noexcept {return reason_;}
    const Targets& active() const noexcept {return active_;}
    unsigned rebinds() const noexcept {return rebinds_;}
    unsigned reuses() const noexcept {return reuses_;}
    unsigned created() const noexcept {return created_;}
    unsigned fast_paths() const noexcept {return fast_paths_;}     // binds answered without re-reading every retained hook
    unsigned verifications() const noexcept {return verifications_;} // binds that ran the full byte verification
    void* original(std::size_t method,std::size_t binding) const noexcept {
        return bindings_[method][binding].original.load(std::memory_order_acquire);
    }
    template<class Read,class Validate,class Snapshot>
    bool bind(const Targets& targets,const Wrappers& wrappers,Read read,Validate valid,Snapshot snapshot) {
        if(poisoned()) {reason_="registry poisoned; restart process";return false;}
        if(recently_verified(targets)) {++fast_paths_;reason_="unchanged";return true;}
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
        // The immediate and native HUD deferred contexts remain live together.
        // Once qualified, alternating between them must not repatch code and
        // suspend every process thread on each HUD/Present callback.
        bool all_enabled=simultaneous_targets_ || targets==active_;
        for(std::size_t method=0;method<Methods;++method)
            all_enabled=all_enabled && bindings_[method][selected[method]].enabled;
        if(all_enabled) {active_=targets;enabled_=true;note_verified(targets);reason_="unchanged";return true;}
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
            if(simultaneous_targets_ && b.enabled) continue;
            const auto status=i==selected[method]?api_.enable(b.target):api_.disable(b.target);
            if(status!=MH_OK) return fail("graphics hook queue failed");
        }
        if(api_.apply()!=MH_OK) return fail("graphics hook batch apply failed");
        for(std::size_t method=0;method<Methods;++method) for(std::size_t i=0;i<capacity;++i) {
            auto& b=bindings_[method][i];if(!b.created) continue;
            const bool kept_enabled=b.enabled && (simultaneous_targets_ || i==selected[method]);
            b.enabled=i==selected[method] || (simultaneous_targets_ && b.enabled);
            Bytes bytes{};
            if(!read(b.target,bytes.data(),bytes.size())) return fail("cannot verify applied graphics hook");
            if(kept_enabled && bytes!=b.patched) return fail("retained graphics target changed during batch");
            if(b.enabled) {if(!kept_enabled)b.patched=bytes;}
            else if(bytes!=b.before) return fail("disabled graphics hook bytes not restored");
        }
        active_=targets;enabled_=true;note_verified(targets);if(changed) ++rebinds_;reason_=changed?"accepted dispatch transition":"accepted binding";
        return true;
    }
    template<class Depth,class Read> bool disable_all(Depth& depth,Read read,DWORD timeout_ms=1000) noexcept {
        bool okay=true;enabled_=false;
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
