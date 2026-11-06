#pragma once
// v993 engine perf step 2: pixel-shader state census, and redundant-bind elision behind the opt-in build/state-elision knob.
//
// The three PS setters were the slots the plan could not guess. The WARP vtable probe (tests/context_slot_probe.cpp) proves
// slot 8 = PSSetShaderResources, 10 = PSSetSamplers, 16 = PSSetConstantBuffers; this class is the model behind their hooks: a
// per-context shadow of exactly those three arrays, the call/repeat census, and the elision decision.
//
// A call is a REPEAT when every slot in [start,start+count) is known and already holds the value being passed. With elision
// ON (build/state-elision; off by default) and the context kind's barriers all installed, the hook returns without forwarding
// it and counts skipped. With elision off it forwards it and still counts the repeat (the census is the measurement either
// way). A call that changes any slot is never elided.
//
// Why a skip is safe: a redundant set leaves the pipeline exactly where it is, so it can only be skipped while the shadow is
// true. The shadow is invalidated (generation++, every slot unknown) whenever something may have changed the PS arrays unseen:
//   - a render-target bind on the same context (pass boundary; the immediate implementation through GpuDispatchBindings, the
//     deferred implementations through ContextBarriers);
//   - ClearState (slot 110) on that context;
//   - CSSetUnorderedAccessViews (slot 68) on that context: a UAV bind unbinds overlapping PS SRVs;
//   - SOSetTargets (slot 37) on that context;
//   - FinishCommandList on a deferred context and ExecuteCommandList on the immediate context (TaaDispatchHook);
//   - a CPU phase change on the same context (pre/eye0/eye1/hook), i.e. the per-view boundary;
//   - the caller's frame barrier at Present; and
//   - the mod's own ContextState scopes, whose calls are not offered to this class at all (the swap restores the game state
//     the shadow describes, so suppressing them is what keeps the shadow true).
// If any of a context kind's barriers are not installed, elision refuses for that kind (the status reason says which one).
//
// v995: four output-binding entry points are tracked as separate HAZARDS, not one combined "barrier ready" bit. Binding a
// resource as output unbinds it from every SRV slot of every stage, so elision on a context kind is allowed only once all of
// OMSetRenderTargets (33), OMSetRenderTargetsAndUnorderedAccessViews (34), CSSetUnorderedAccessViews (68) and SOSetTargets
// (37) are covered for that kind. On deferred contexts 33/34 depend on the GPU registry binding the game's deferred target
// set (gpu_deferred_targets_bound); the status reason names the first missing hazard.
//
// v995: PSSetConstantBuffers1 (ID3D11DeviceContext1 slot 123) shares the 14 PS constant-buffer slots with the base method but
// also carries a first/num-constant window. It is NEVER elided: note_constant_buffers1() counts it and marks every covered CB
// slot unknown, so a later base PSSetConstantBuffers to the same buffer pointer is not mistaken for a repeat when the offsets
// differ. SwapDeviceContextState (slot 131) replaces the whole state of the context that swaps, and only the immediate context
// supports it. v996: a sighting barriers that one context (the hook barriers before and after the original call) and is counted
// in state_swaps; it does not refuse elision or touch the deferred shadow/readiness, because a deferred context's recorded state
// cannot be changed by an immediate swap.
//
// Locking: one spinlock per context record. A context's calls are normally one thread's, but the same d3d11 entry point also
// serves other contexts from other threads, so the record is guarded rather than free-running.
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <string>
#include <thread>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace d2vr {
class PsStateCensus {
public:
    enum Method : unsigned {resources=0,samplers=1,constant_buffers=2,method_count=3};
    enum Kind : unsigned {immediate=0,deferred=1,kind_count=2};
    // v995: every entry point that can unbind a PS SRV by binding a resource as output. All four must be covered for a kind.
    enum Hazard : unsigned {
        hazard_render_targets=0,
        hazard_render_targets_uav=1,
        hazard_cs_unordered_access=2,
        hazard_stream_output=3,
        hazard_count=4,
        hazard_all=(1u<<hazard_count)-1u
    };
    static constexpr unsigned srv_slots=128;               // D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT
    static constexpr unsigned sampler_slots=16;            // D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT
    static constexpr unsigned constant_buffer_slots=14;    // D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT
    static constexpr unsigned total_slots=srv_slots+sampler_slots+constant_buffer_slots;
    // v993: 64, because the engine records on several deferred contexts from job threads. Hashed: a collision only costs the
    // elision, never correctness.
    static constexpr unsigned context_slots=64;
    static constexpr unsigned known_words=(total_slots+63u)/64u;
    // v996: status keeps the first eight distinct SwapDeviceContextState caller sites, with their module and RVA.
    static constexpr unsigned state_swap_caller_limit=8;
    static constexpr unsigned state_swap_module_chars=64;

    static unsigned capacity(unsigned method) noexcept {
        switch(method) {
            case resources: return srv_slots;
            case samplers: return sampler_slots;
            case constant_buffers: return constant_buffer_slots;
            default: return 0;
        }
    }
    static unsigned base(unsigned method) noexcept {
        switch(method) {
            case resources: return 0;
            case samplers: return srv_slots;
            case constant_buffers: return srv_slots+sampler_slots;
            default: return total_slots;
        }
    }
    static const char* method_name(unsigned method) noexcept {
        static const char* const names[method_count]{"psset_shader_resources","psset_samplers","psset_constant_buffers"};
        return method<method_count?names[method]:"unknown";
    }
    static const char* kind_name(unsigned kind) noexcept {
        static const char* const names[kind_count]{"immediate","deferred"};
        return kind<kind_count?names[kind]:"unknown";
    }
    static const char* hazard_name(unsigned hazard) noexcept {
        static const char* const names[hazard_count]{
            "om_set_render_targets","om_set_render_targets_and_uavs","cs_set_unordered_access_views","so_set_targets"};
        return hazard<hazard_count?names[hazard]:"unknown";
    }

    void set_elision(bool on) noexcept {elision_.store(on,std::memory_order_relaxed);}
    bool elision() const noexcept {return elision_.load(std::memory_order_relaxed);}

    // v995: the finish/execute pass barrier is separate from the four output-binding hazards. Both are required for a skip.
    void set_pass_ready(unsigned kind,bool ready,const char* reason) noexcept {
        if(kind>=kind_count) return;
        pass_ready_[kind].store(ready,std::memory_order_relaxed);
        pass_reason_[kind].store(reason?reason:"",std::memory_order_relaxed);
    }
    bool pass_ready(unsigned kind) const noexcept {
        return kind<kind_count && pass_ready_[kind].load(std::memory_order_relaxed);
    }
    void set_hazard_ready(unsigned kind,unsigned hazard,bool ready,const char* reason) noexcept {
        if(kind>=kind_count || hazard>=hazard_count) return;
        const unsigned bit=1u<<hazard;
        if(ready) hazards_[kind].fetch_or(bit,std::memory_order_relaxed);
        else hazards_[kind].fetch_and(~bit,std::memory_order_relaxed);
        hazard_reason_[kind][hazard].store(ready?"":(reason?reason:""),std::memory_order_relaxed);
    }
    bool hazard_ready(unsigned kind,unsigned hazard) const noexcept {
        return kind<kind_count && hazard<hazard_count &&
            (hazards_[kind].load(std::memory_order_relaxed)&(1u<<hazard))!=0;
    }
    // Convenience for callers that really do have the whole set (labs). Production install uses set_pass_ready plus the four
    // set_hazard_ready calls so a missing output route can never be papered over.
    void set_barrier_ready(unsigned kind,bool ready,const char* reason) noexcept {
        if(kind>=kind_count) return;
        set_pass_ready(kind,ready,reason);
        for(unsigned hazard=0;hazard<hazard_count;++hazard) set_hazard_ready(kind,hazard,ready,reason);
    }
    bool barrier_ready(unsigned kind) const noexcept {
        return kind<kind_count &&
            pass_ready_[kind].load(std::memory_order_relaxed) &&
            hazards_[kind].load(std::memory_order_relaxed)==hazard_all;
    }
    const char* barrier_reason(unsigned kind) const noexcept {
        if(kind>=kind_count) return "";
        if(!pass_ready_[kind].load(std::memory_order_relaxed)) {
            const char* reason=pass_reason_[kind].load(std::memory_order_relaxed);
            if(reason && *reason) return reason;
        }
        for(unsigned hazard=0;hazard<hazard_count;++hazard) if(!hazard_ready(kind,hazard)) {
            const char* reason=hazard_reason_[kind][hazard].load(std::memory_order_relaxed);
            return reason?reason:"";
        }
        return "";
    }
    std::uint64_t state_swap_count() const noexcept {return state_swap_count_.load(std::memory_order_relaxed);}
    // v996: a SwapDeviceContextState replaces the whole state of the swapping context only. The hook calls its observer before
    // and after the original call: the before call barriers, this after call barriers again, counts the sighting and records
    // the caller site. Nothing else is touched: the deferred shadow and readiness are independent of an immediate swap.
    void note_device_context_state_swap(void* context,const char* module,std::uintptr_t rva) noexcept {
        barrier(context);
        state_swap_count_.fetch_add(1,std::memory_order_relaxed);
        const char* name=module?module:"";
        state_swap_spin_.acquire();
        for(unsigned i=0;i<state_swap_caller_count_;++i) {
            StateSwapCaller& caller=state_swap_callers_[i];
            if(caller.rva!=rva || std::strncmp(caller.module,name,sizeof(caller.module))!=0) continue;
            ++caller.count;
            state_swap_spin_.release();
            return;
        }
        if(state_swap_caller_count_<state_swap_caller_limit) {
            StateSwapCaller& caller=state_swap_callers_[state_swap_caller_count_++];
            caller.rva=rva;
            caller.count=1;
            unsigned n=0;
            for(;n+1<sizeof(caller.module) && name[n];++n) caller.module[n]=name[n];
            caller.module[n]=0;
        }
        state_swap_spin_.release();
    }
    std::uint64_t cb1_calls(unsigned kind) const noexcept {
        return kind<kind_count?cb1_calls_[kind].load(std::memory_order_relaxed):0;
    }
    // v997: counters are sharded per thread (see CounterShard), so a read sums the shards. The JSON below uses the same sums;
    // these accessors are for the labs and for the per-frame deltas the elision_ab window stores.
    std::uint64_t calls(unsigned kind,unsigned method) const noexcept {
        if(kind>=kind_count || method>=method_count) return 0;
        std::uint64_t n=0;
        for(const auto& shard:counter_shards_[kind]) n+=shard.calls[method].load(std::memory_order_relaxed);
        return n;
    }
    std::uint64_t repeats(unsigned kind,unsigned method) const noexcept {
        if(kind>=kind_count || method>=method_count) return 0;
        std::uint64_t n=0;
        for(const auto& shard:counter_shards_[kind]) n+=shard.repeats[method].load(std::memory_order_relaxed);
        return n;
    }
    std::uint64_t skipped(unsigned kind,unsigned method) const noexcept {
        if(kind>=kind_count || method>=method_count) return 0;
        std::uint64_t n=0;
        for(const auto& shard:counter_shards_[kind]) n+=shard.skipped[method].load(std::memory_order_relaxed);
        return n;
    }
    std::uint64_t skipped_total() const noexcept {
        std::uint64_t n=0;
        for(unsigned kind=0;kind<kind_count;++kind) for(unsigned method=0;method<method_count;++method) n+=skipped(kind,method);
        return n;
    }
    std::uint64_t barriers_total() const noexcept {return barriers_.load(std::memory_order_relaxed);}
    // v995: PSSetConstantBuffers1 (slot 123) is never a repeat: its first/num window is state the base shadow does not hold.
    // Count it and mark the covered PS constant-buffer slots unknown so the next base bind is always forwarded.
    void note_constant_buffers1(void* context,unsigned start,unsigned count,unsigned kind) noexcept {
        const unsigned side=kind<kind_count?kind:deferred;
        cb1_calls_[side].fetch_add(1,std::memory_order_relaxed);
        if(!context) return;
        const unsigned n=capacity(constant_buffers);
        if(start>=n || !count) return;
        Slot& slot=slot_for(context);
        slot.spin.acquire();
        if(slot.context.load(std::memory_order_relaxed)==context)
            for(unsigned i=0;i<count && start+i<n;++i) clear_known(slot,constant_buffers,start+i);
        slot.spin.release();
    }

    // True: the call is redundant, elision is on and this context kind's barriers are installed, so the caller must NOT
    // forward it. False: forward it.
    bool consider(void* context,unsigned method,unsigned start,unsigned count,void* const* values,unsigned phase,unsigned kind) noexcept {
        if(method>=method_count || !context) return false;
        const unsigned side=kind<kind_count?kind:deferred;
        // v997: post to this thread's cache line, never to one shared counter (the deferred recording threads all bind PS state).
        CounterShard& counters=counter_shards_[side][shard_index()];
        counters.calls[method].fetch_add(1,std::memory_order_relaxed);
        if(!values || !count) return false;
        const unsigned n=capacity(method);
        if(start>=n || count>n-start) return false;   // out of the D3D11 slot range: never tracked, never elided
        Slot& slot=slot_for(context);
        slot.spin.acquire();
        if(slot.context.load(std::memory_order_relaxed)!=context) adopt(slot,context,phase);
        else if(slot.phase!=phase) {slot.phase=phase;invalidate(slot);}
        bool repeat=true;
        for(unsigned i=0;i<count && repeat;++i)
            repeat=known(slot,method,start+i) && slot.values[base(method)+start+i]==values[i];
        if(!repeat) {
            for(unsigned i=0;i<count;++i) {
                const unsigned at=base(method)+start+i;
                slot.values[at]=values[i];mark_known(slot,method,start+i);
            }
            slot.spin.release();
            return false;
        }
        counters.repeats[method].fetch_add(1,std::memory_order_relaxed);
        const bool elide=elision_.load(std::memory_order_relaxed) && barrier_ready(side);
        if(elide) counters.skipped[method].fetch_add(1,std::memory_order_relaxed);
        slot.spin.release();
        return elide;
    }

    // The context's PS shadow is no longer known to be true (render-target set changed, or a pass boundary of any kind).
    void barrier(void* context) noexcept {
        if(!context) return;
        Slot& slot=slot_for(context);
        slot.spin.acquire();
        if(slot.context.load(std::memory_order_relaxed)==context) {invalidate(slot);barriers_.fetch_add(1,std::memory_order_relaxed);}
        slot.spin.release();
    }
    // Every context: Present, session start/stop. Keep the context pointers: a record for a live context must survive the
    // frame boundary so `adopts` counts real takeovers/hash collisions, not one reset per context per frame.
    void barrier_all() noexcept {
        for(auto& slot:slots_) {
            slot.spin.acquire();
            invalidate(slot);
            slot.spin.release();
        }
        barriers_.fetch_add(1,std::memory_order_relaxed);
    }
    void reset() noexcept {
        for(auto& side:counter_shards_) for(auto& shard:side) for(unsigned method=0;method<method_count;++method) {
            shard.calls[method].store(0,std::memory_order_relaxed);
            shard.repeats[method].store(0,std::memory_order_relaxed);
            shard.skipped[method].store(0,std::memory_order_relaxed);
        }
        adopts_.store(0,std::memory_order_relaxed);
        barriers_.store(0,std::memory_order_relaxed);
        for(unsigned kind=0;kind<kind_count;++kind) {
            cb1_calls_[kind].store(0,std::memory_order_relaxed);
            pass_ready_[kind].store(false,std::memory_order_relaxed);
            pass_reason_[kind].store("",std::memory_order_relaxed);
            hazards_[kind].store(0,std::memory_order_relaxed);
            for(unsigned hazard=0;hazard<hazard_count;++hazard) hazard_reason_[kind][hazard].store("",std::memory_order_relaxed);
        }
        state_swap_count_.store(0,std::memory_order_relaxed);
        state_swap_spin_.acquire();
        state_swap_caller_count_=0;
        for(auto& caller:state_swap_callers_) caller={};
        state_swap_spin_.release();
        for(auto& slot:slots_) {
            slot.spin.acquire();
            slot.context.store(nullptr,std::memory_order_relaxed);
            invalidate(slot);
            slot.spin.release();
        }
        barriers_.store(0,std::memory_order_relaxed);
    }
    // Diagnostic/test view of the shadow: the recorded value of one slot and whether the slot is known.
    bool shadow(void* context,unsigned method,unsigned index,void*& value) noexcept {
        bool known_slot=false;
        if(method>=method_count || index>=capacity(method) || !context) return false;
        Slot& slot=slot_for(context);
        slot.spin.acquire();
        if(slot.context.load(std::memory_order_relaxed)==context && known(slot,method,index)) {
            value=slot.values[base(method)+index];known_slot=true;
        }
        slot.spin.release();
        return known_slot;
    }
    unsigned tracked_contexts() const noexcept {
        unsigned n=0;
        for(const auto& slot:slots_) {
            if(slot.context.load(std::memory_order_relaxed)) {
                // const read of a mutable record is fine for a diagnostic counter: a torn value costs one status line.
                ++n;
            }
        }
        return n;
    }
    std::string json() const {
        std::ostringstream out;
        out<<"{\"elision\":"<<(elision()?"true":"false");
        counter_json(out,"calls",field_calls);
        counter_json(out,"repeats",field_repeats);
        counter_json(out,"skipped",field_skipped);
        out<<",\"adopts\":"<<adopts_.load(std::memory_order_relaxed)
           <<",\"barriers\":"<<barriers_.load(std::memory_order_relaxed)
           <<",\"contexts\":"<<tracked_contexts()
           <<",\"cb1_calls\":{\"immediate\":"<<cb1_calls_[immediate].load(std::memory_order_relaxed)
           <<",\"deferred\":"<<cb1_calls_[deferred].load(std::memory_order_relaxed)<<'}'
           <<",\"state_swaps\":{\"count\":"<<state_swap_count()<<",\"callers\":[";
        state_swap_spin_.acquire();
        for(unsigned i=0;i<state_swap_caller_count_;++i) {
            const StateSwapCaller& caller=state_swap_callers_[i];
            if(i) out<<',';
            out<<"{\"module\":\""<<caller.module<<"\",\"rva\":"<<caller.rva<<",\"count\":"<<caller.count<<'}';
        }
        state_swap_spin_.release();
        out<<"]}"
           <<",\"hazard_ready\":{\"immediate\":[";
        for(unsigned hazard=0;hazard<hazard_count;++hazard) out<<(hazard?",":"")<<(hazard_ready(immediate,hazard)?"true":"false");
        out<<"],\"deferred\":[";
        for(unsigned hazard=0;hazard<hazard_count;++hazard) out<<(hazard?",":"")<<(hazard_ready(deferred,hazard)?"true":"false");
        out<<"]}"
           <<",\"barrier_ready\":{\"immediate\":"<<(barrier_ready(immediate)?"true":"false")
           <<",\"deferred\":"<<(barrier_ready(deferred)?"true":"false")<<'}'
           <<",\"reason\":{\"immediate\":\""<<barrier_reason(immediate)<<"\",\"deferred\":\""<<barrier_reason(deferred)<<"\"}}";
        return out.str();
    }
private:
    // v997: one counter set per (thread, context kind), 64-byte aligned so two recording threads never share a cache line.
    // A thread keeps its shard for the process lifetime (a thread past the 64th shares one, which stays correct). Reads sum.
#pragma warning(push)
#pragma warning(disable:4324) // the padding is the point
    struct alignas(64) CounterShard {
        std::atomic<std::uint64_t> calls[method_count]{},repeats[method_count]{},skipped[method_count]{};
    };
#pragma warning(pop)
    static constexpr unsigned shard_count=64;
    static unsigned shard_index() noexcept {
        static std::atomic<unsigned> next{0};
        thread_local const unsigned index=next.fetch_add(1,std::memory_order_relaxed)%shard_count;
        return index;
    }
    enum CounterField : unsigned {field_calls=0,field_repeats=1,field_skipped=2};
    struct StateSwapCaller {char module[state_swap_module_chars]{};std::uintptr_t rva{};std::uint64_t count{};};
    void counter_json(std::ostringstream& out,const char* name,unsigned field) const {
        const auto sum=[&](unsigned kind,unsigned method) {
            std::uint64_t n=0;
            for(const auto& shard:counter_shards_[kind]) {
                const auto& value=field==field_calls?shard.calls[method]:field==field_repeats?shard.repeats[method]:shard.skipped[method];
                n+=value.load(std::memory_order_relaxed);
            }
            return n;
        };
        out<<",\""<<name<<"\":{\"immediate\":[";
        for(unsigned m=0;m<method_count;++m) out<<(m?",":"")<<sum(immediate,m);
        out<<"],\"deferred\":[";
        for(unsigned m=0;m<method_count;++m) out<<(m?",":"")<<sum(deferred,m);
        out<<"]}";
    }
    class Spin {
        std::atomic<bool> busy_{false};
    public:
        Spin()=default;
        Spin(const Spin&)=delete;
        Spin& operator=(const Spin&)=delete;
        void acquire() noexcept {
            unsigned spins=0;
            while(busy_.exchange(true,std::memory_order_acquire)) {
                if(++spins<128) {
#if defined(_MSC_VER)
                    _mm_pause();
#endif
                } else {
                    spins=0;
                    std::this_thread::yield();
                }
            }
        }
        void release() noexcept {busy_.store(false,std::memory_order_release);}
    };
    struct Slot {
        std::atomic<void*> context{nullptr};
        Spin spin;
        std::uint64_t generation{1};
        unsigned phase{0xffffffffu};
        void* values[total_slots]{};
        std::uint64_t known[known_words]{};
    };
    static bool known(const Slot& slot,unsigned method,unsigned index) noexcept {
        const unsigned bit=base(method)+index;
        return ((slot.known[bit/64u]>>(bit%64u))&std::uint64_t(1))!=0;
    }
    static void mark_known(Slot& slot,unsigned method,unsigned index) noexcept {
        const unsigned bit=base(method)+index;
        slot.known[bit/64u]|=std::uint64_t(1)<<(bit%64u);
    }
    static void clear_known(Slot& slot,unsigned method,unsigned index) noexcept {
        const unsigned bit=base(method)+index;
        slot.known[bit/64u]&=~(std::uint64_t(1)<<(bit%64u));
    }
    static void invalidate(Slot& slot) noexcept {
        ++slot.generation;
        for(auto& word:slot.known) word=0;
    }
    void adopt(Slot& slot,void* context,unsigned phase) noexcept {
        slot.context.store(context,std::memory_order_relaxed);
        slot.phase=phase;
        invalidate(slot);
        adopts_.fetch_add(1,std::memory_order_relaxed);
    }
    Slot& slot_for(void* context) noexcept {
        const auto address=reinterpret_cast<std::uintptr_t>(context);
        return slots_[(address>>4u)&(context_slots-1u)];
    }
    CounterShard counter_shards_[kind_count][shard_count];
    std::atomic<bool> elision_{false};
    std::atomic<bool> pass_ready_[kind_count]{};
    std::atomic<const char*> pass_reason_[kind_count]{};
    std::atomic<unsigned> hazards_[kind_count]{};
    std::atomic<const char*> hazard_reason_[kind_count][hazard_count]{};
    std::atomic<std::uint64_t> cb1_calls_[kind_count]{};
    std::atomic<std::uint64_t> state_swap_count_{0};
    mutable Spin state_swap_spin_;
    StateSwapCaller state_swap_callers_[state_swap_caller_limit]{};
    unsigned state_swap_caller_count_{};
    std::atomic<std::uint64_t> adopts_{0};
    std::atomic<std::uint64_t> barriers_{0};
    Slot slots_[context_slots];
};
} // namespace d2vr
