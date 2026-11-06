#pragma once
// v997 engine perf: WIDE state census (count only), v998: the same census with an opt-in redundant-bind FILTER.
// Opt-in with build/state-census-wide (count only) or build/state-elision-wide (count + elide; the knob also installs these
// hooks, as if build/state-census-wide were set). Off by default: each extra hook costs time on the recording threads.
//
// Covered methods (slot numbers proved on WARP for immediate AND deferred in tests/context_slot_probe.cpp):
//   25 VSSetShaderResources, 26 VSSetSamplers, 9 PSSetShader, 17 IASetInputLayout, 18 IASetVertexBuffers,
//   19 IASetIndexBuffer, 24 IASetPrimitiveTopology, 35 OMSetBlendState, 36 OMSetDepthStencilState,
//   43 RSSetState, 45 RSSetScissorRects.
// VSSetConstantBuffers (7), VSSetConstantBuffers1 (119) and VSSetShader (11) already have an owner (ObjectMotion, one MinHook
// per code address rule). Their calls reach this class through ObjectMotion's filter callbacks, after ObjectMotion's own
// bookkeeping and on the final forwarded argument list. VSSetConstantBuffers1 is never elided: it only counts and invalidates
// the base shadow of the slots it covers.
//
// The per-context shadow follows the same barrier rules as the PS census (ps_state_census.hpp): a render-target/UAV/SO bind,
// ClearState, CSSetUnorderedAccessViews, FinishCommandList, ExecuteCommandList on the immediate context, a device-context state
// swap, a CPU phase change and Present all invalidate it (the owner call sites call barrier()/barrier_all() next to the PS
// census). Binding a resource as output unbinds it from every SRV slot, a stream-output target also unbinds a vertex buffer, and
// any output bind can unbind a buffer from every VB/IB/CB slot, which is why those are full barriers here too.
//
// Elision is allowed only when the PS census's own readiness says the context kind has every pass/output barrier installed
// (readiness_source below points at PsStateCensus::barrier_ready, so the two filters can never drift). The count-only mode never
// skips: observe_* returns true only when elision is on.
//
// Method rules (compare ALL parameters; anything not compared is never a repeat):
//   topology, input layout, rasterizer state, depth state+stencil ref, full scissor rects;
//   blend state+factor+sample mask (a NULL factor normalizes to ones);
//   shaders only with class-instance count 0 (the class list is still compared for the repeat census);
//   VS SRV/samplers exactly like the PS arrays; VS CB pointers; VB buffers+strides+offsets over all slots; IB buffer+format+offset.
//
// Counters are sharded per thread and cache-line aligned, like v997 PsStateCensus: the deferred recording threads bind at
// millions of calls per second and must not share a cache line. The status writer sums the shards once per write.
#include <windows.h>
#include <d3d11.h>
#include <MinHook.h>
#include <array>
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
class WideStateCensus {
public:
    enum Method : unsigned {
        vs_shader_resources=0,vs_samplers,vs_constant_buffers,vs_constant_buffers1,ps_shader,vs_shader,
        ia_input_layout,ia_vertex_buffers,ia_index_buffer,ia_primitive_topology,om_blend_state,
        om_depth_stencil_state,rs_state,rs_scissor_rects,method_count
    };
    enum Kind : unsigned {immediate=0,deferred=1,kind_count=2};
    static constexpr unsigned srv_slots=128;                 // D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT
    static constexpr unsigned sampler_slots=16;              // D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT
    static constexpr unsigned constant_buffer_slots=14;      // D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT
    static constexpr unsigned class_instance_slots=16;       // more than this in one call: counted, never a repeat
    static constexpr unsigned vertex_buffer_slots=32;        // D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT
    static constexpr unsigned scissor_slots=16;              // D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE
    static constexpr unsigned context_slots=64;
    static constexpr unsigned shard_count=64;

    static const char* method_name(unsigned method) noexcept {
        static const char* const names[method_count]{
            "VSSetShaderResources","VSSetSamplers","VSSetConstantBuffers","VSSetConstantBuffers1","PSSetShader",
            "VSSetShader","IASetInputLayout","IASetVertexBuffers","IASetIndexBuffer","IASetPrimitiveTopology",
            "OMSetBlendState","OMSetDepthStencilState","RSSetState","RSSetScissorRects"};
        return method<method_count?names[method]:"unknown";
    }
    static unsigned capacity(unsigned method) noexcept {
        switch(method) {
            case vs_shader_resources: return srv_slots;
            case vs_samplers: return sampler_slots;
            case vs_constant_buffers: case vs_constant_buffers1: return constant_buffer_slots;
            case vs_shader: case ps_shader: return class_instance_slots;
            case ia_vertex_buffers: return vertex_buffer_slots;
            case rs_scissor_rects: return scissor_slots;
            default: return 1;
        }
    }
    void set_enabled(bool on) noexcept {enabled_.store(on,std::memory_order_relaxed);}
    bool enabled() const noexcept {return enabled_.load(std::memory_order_relaxed);}
    // v998: opt-in elision. The shadow is maintained with elision off too, so a live toggle needs no re-record.
    void set_elision(bool on) noexcept {elision_.store(on,std::memory_order_relaxed);}
    bool elision() const noexcept {return elision_.load(std::memory_order_relaxed);}
    unsigned kind_of(ID3D11DeviceContext* c) const noexcept {
        const auto immediate_context=immediate_context_.load(std::memory_order_relaxed);
        return immediate_context && c==immediate_context?immediate:deferred;
    }
    void set_immediate_context(ID3D11DeviceContext* c) noexcept {immediate_context_.store(c,std::memory_order_relaxed);}
    // The CPU phase (pre/eye0/eye1/hook) of a state call: a phase change on the same context invalidates the shadow, exactly as
    // in PsStateCensus. The probe points this at ps_phase(); the labs point it at their own clock.
    inline static std::atomic<unsigned (*)() noexcept> phase_source{};
    static unsigned phase() noexcept {const auto source=phase_source.load(std::memory_order_relaxed);return source?source():0u;}
    // True outside the mod's own ContextState / D3D11TemporalOwner isolate scopes, whose calls are not the game's state (the
    // PS census suppresses them the same way). Null in the labs.
    inline static std::atomic<bool (*)() noexcept> tracking_source{};
    static bool tracking() noexcept {const auto source=tracking_source.load(std::memory_order_relaxed);return !source || source();}
    // v998: readiness is the PS census's own (pass + four output hazards). A null source means "not proven", so a lab or a
    // session that has not wired it never elides. In dxgi_probe these point at PsStateCensus::barrier_ready/reason.
    inline static std::atomic<bool (*)(unsigned kind) noexcept> barrier_ready_source{};
    inline static std::atomic<const char* (*)(unsigned kind) noexcept> barrier_reason_source{};
    bool barrier_ready(unsigned kind) const noexcept {
        if(kind>=kind_count) return false;
        const auto source=barrier_ready_source.load(std::memory_order_relaxed);
        return source && source(kind);
    }
    const char* barrier_reason(unsigned kind) const noexcept {
        if(kind>=kind_count) return "";
        const auto source=barrier_reason_source.load(std::memory_order_relaxed);
        return source?source(kind):"wide barrier readiness source not installed";
    }
    // ObjectMotion owns 7/11/119; dxgi_probe reports whether that owner installed, so the status can distinguish "no calls" from
    // "the method is not observed in this session".
    void set_owner_ready(bool ready) noexcept {
        for(unsigned kind=0;kind<kind_count;++kind) {
            covered_[vs_constant_buffers][kind]=ready;
            covered_[vs_constant_buffers1][kind]=ready;
            covered_[vs_shader][kind]=ready;
        }
    }
    std::uint64_t calls(unsigned method,unsigned kind) const noexcept {
        if(method>=method_count || kind>=kind_count) return 0;
        std::uint64_t n=0;
        for(const auto& shard:counter_shards_[kind]) n+=shard.calls[method].load(std::memory_order_relaxed);
        return n;
    }
    std::uint64_t repeats(unsigned method,unsigned kind) const noexcept {
        if(method>=method_count || kind>=kind_count) return 0;
        std::uint64_t n=0;
        for(const auto& shard:counter_shards_[kind]) n+=shard.repeats[method].load(std::memory_order_relaxed);
        return n;
    }
    std::uint64_t skipped(unsigned method,unsigned kind) const noexcept {
        if(method>=method_count || kind>=kind_count) return 0;
        std::uint64_t n=0;
        for(const auto& shard:counter_shards_[kind]) n+=shard.skipped[method].load(std::memory_order_relaxed);
        return n;
    }
    std::uint64_t skipped_total() const noexcept {
        std::uint64_t n=0;
        for(unsigned kind=0;kind<kind_count;++kind) for(unsigned method=0;method<method_count;++method) n+=skipped(method,kind);
        return n;
    }
    std::uint64_t barriers_total() const noexcept {return barriers_.load(std::memory_order_relaxed);}

    // ---- observers/filters: called on the final forwarded argument list. Return true only when the CALLER MUST NOT forward
    // the call (an exact repeat, wide elision on, and this kind's PS barrier readiness proven). ----
    bool observe_vs_shader_resources(ID3D11DeviceContext* c,UINT start,UINT count,ID3D11ShaderResourceView* const* views,unsigned phase_now) noexcept {
        if(!ready(c)) return false;
        const unsigned kind=kind_of(c);
        if(!views && count) {invalidate_context(c,kind,phase_now);note(kind,vs_shader_resources,false);return false;}
        if(!count) {note(kind,vs_shader_resources,false);return false;}
        if(start>=srv_slots || count>srv_slots-start) {invalidate_context(c,kind,phase_now);note(kind,vs_shader_resources,false);return false;}
        Slot& s=enter(c,kind,phase_now);
        bool repeat=true;
        for(UINT i=0;i<count && repeat;++i) repeat=known_bit(s.vs_srv_known,start+i) && s.vs_srv[start+i]==views[i];
        if(!repeat) for(UINT i=0;i<count;++i) {s.vs_srv[start+i]=views[i];set_known(s.vs_srv_known,start+i);}
        return finish(kind,vs_shader_resources,repeat,repeat,s);
    }
    bool observe_vs_samplers(ID3D11DeviceContext* c,UINT start,UINT count,ID3D11SamplerState* const* samplers,unsigned phase_now) noexcept {
        if(!ready(c)) return false;
        const unsigned kind=kind_of(c);
        if(!samplers && count) {invalidate_context(c,kind,phase_now);note(kind,vs_samplers,false);return false;}
        if(!count) {note(kind,vs_samplers,false);return false;}
        if(start>=sampler_slots || count>sampler_slots-start) {invalidate_context(c,kind,phase_now);note(kind,vs_samplers,false);return false;}
        Slot& s=enter(c,kind,phase_now);
        bool repeat=true;
        for(UINT i=0;i<count && repeat;++i) repeat=known_bit(s.vs_sampler_known,start+i) && s.vs_samplers[start+i]==samplers[i];
        if(!repeat) for(UINT i=0;i<count;++i) {s.vs_samplers[start+i]=samplers[i];set_known(s.vs_sampler_known,start+i);}
        return finish(kind,vs_samplers,repeat,repeat,s);
    }
    // VSSetConstantBuffers inside ObjectMotion's hook, after its state tracking. A base bind sets first=0/num=all, so it
    // invalidates the CB1 shadow of the covered slots and clears any ObjectMotion substitution the caller did not report.
    bool observe_vs_constant_buffers(ID3D11DeviceContext* c,UINT start,UINT count,ID3D11Buffer* const* buffers,unsigned phase_now) noexcept {
        if(!ready(c)) return false;
        const unsigned kind=kind_of(c);
        if(!buffers && count) {invalidate_context(c,kind,phase_now);note(kind,vs_constant_buffers,false);return false;}
        if(!count) {note(kind,vs_constant_buffers,false);return false;}
        if(start>=constant_buffer_slots || count>constant_buffer_slots-start) {invalidate_context(c,kind,phase_now);note(kind,vs_constant_buffers,false);return false;}
        Slot& s=enter(c,kind,phase_now);
        bool repeat=true;
        for(UINT i=0;i<count && repeat;++i) repeat=known_bit(s.vs_cb_known,start+i) && s.vs_cb[start+i]==buffers[i];
        for(UINT i=0;i<count;++i) {
            const unsigned at=start+i;
            s.vs_cb[at]=buffers[i];set_known(s.vs_cb_known,at);
            clear_known(s.vs_cb1_known,at); // the base bind replaced the CB1 window
        }
        return finish(kind,vs_constant_buffers,repeat,repeat,s);
    }
    // VSSetConstantBuffers1: never elided. It must still record the window and invalidate the base shadow so that a later base
    // VSSetConstantBuffers to the same pointer is forwarded (the PS CB1 rule).
    bool observe_vs_constant_buffers1(ID3D11DeviceContext* c,UINT start,UINT count,ID3D11Buffer* const* buffers,
                                      const UINT* first,const UINT* num,unsigned phase_now) noexcept {
        if(!ready(c)) return false;
        const unsigned kind=kind_of(c);
        if((!buffers || !first || !num) && count) {invalidate_context(c,kind,phase_now);note(kind,vs_constant_buffers1,false);return false;}
        if(!count) {note(kind,vs_constant_buffers1,false);return false;}
        if(start>=constant_buffer_slots || count>constant_buffer_slots-start) {invalidate_context(c,kind,phase_now);note(kind,vs_constant_buffers1,false);return false;}
        Slot& s=enter(c,kind,phase_now);
        bool repeat=true;
        for(UINT i=0;i<count && repeat;++i) {
            const unsigned at=start+i;
            repeat=known_bit(s.vs_cb1_known,at) && s.vs_cb1_buffer[at]==buffers[i] &&
                s.vs_cb1_first[at]==first[i] && s.vs_cb1_num[at]==num[i];
        }
        for(UINT i=0;i<count;++i) {
            const unsigned at=start+i;
            s.vs_cb1_buffer[at]=buffers[i];s.vs_cb1_first[at]=first[i];s.vs_cb1_num[at]=num[i];
            set_known(s.vs_cb1_known,at);
            clear_known(s.vs_cb_known,at); // the window makes a later base bind not a repeat
        }
        return finish(kind,vs_constant_buffers1,repeat,false,s);
    }
    // ObjectMotion binds its own pool buffer at VS b2 for one draw and restores the game's buffer after it, both through the
    // original function (never through the hooked setter). Recording those exact pointers keeps the shadow true without counting
    // them as game calls; without it the next identical game bind could be skipped while the pool buffer was actually bound.
    void note_direct_vs_constant_buffers(ID3D11DeviceContext* c,UINT start,UINT count,ID3D11Buffer* const* buffers) noexcept {
        if(!enabled_.load(std::memory_order_relaxed) || !c || !tracking()) return;
        const unsigned kind=kind_of(c);
        if((!buffers && count) || !count) {invalidate_context(c,kind,phase());return;}
        if(start>=constant_buffer_slots || count>constant_buffer_slots-start) {invalidate_context(c,kind,phase());return;}
        Slot& s=enter(c,kind,phase());
        for(UINT i=0;i<count;++i) {
            const unsigned at=start+i;
            s.vs_cb[at]=buffers[i];set_known(s.vs_cb_known,at);
            clear_known(s.vs_cb1_known,at);
        }
        s.spin.release();
    }
    bool observe_ps_shader(ID3D11DeviceContext* c,ID3D11PixelShader* shader,ID3D11ClassInstance* const* classes,UINT count,unsigned phase_now) noexcept {
        return observe_shader(c,ps_shader,shader,classes,count,phase_now);
    }
    bool observe_vs_shader(ID3D11DeviceContext* c,ID3D11VertexShader* shader,ID3D11ClassInstance* const* classes,UINT count,unsigned phase_now) noexcept {
        return observe_shader(c,vs_shader,shader,classes,count,phase_now);
    }
    bool observe_ia_input_layout(ID3D11DeviceContext* c,ID3D11InputLayout* layout,unsigned phase_now) noexcept {
        if(!ready(c)) return false;
        const unsigned kind=kind_of(c);
        Slot& s=enter(c,kind,phase_now);
        const bool repeat=s.ia_layout_known && s.ia_layout==layout;
        s.ia_layout=layout;s.ia_layout_known=true;
        return finish(kind,ia_input_layout,repeat,repeat,s);
    }
    bool observe_ia_vertex_buffers(ID3D11DeviceContext* c,UINT start,UINT count,ID3D11Buffer* const* buffers,
                                   const UINT* strides,const UINT* offsets,unsigned phase_now) noexcept {
        if(!ready(c)) return false;
        const unsigned kind=kind_of(c);
        if((!buffers || !strides || !offsets) && count) {invalidate_context(c,kind,phase_now);note(kind,ia_vertex_buffers,false);return false;}
        if(!count) {note(kind,ia_vertex_buffers,false);return false;}
        if(start>=vertex_buffer_slots || count>vertex_buffer_slots-start) {invalidate_context(c,kind,phase_now);note(kind,ia_vertex_buffers,false);return false;}
        Slot& s=enter(c,kind,phase_now);
        bool repeat=true;
        for(UINT i=0;i<count && repeat;++i) {
            const unsigned at=start+i;
            repeat=known_bit(s.ia_vb_known,at) && s.ia_vb[at]==buffers[i] &&
                s.ia_vb_stride[at]==strides[i] && s.ia_vb_offset[at]==offsets[i];
        }
        for(UINT i=0;i<count;++i) {
            const unsigned at=start+i;
            s.ia_vb[at]=buffers[i];s.ia_vb_stride[at]=strides[i];s.ia_vb_offset[at]=offsets[i];
            set_known(s.ia_vb_known,at);
        }
        return finish(kind,ia_vertex_buffers,repeat,repeat,s);
    }
    bool observe_ia_index_buffer(ID3D11DeviceContext* c,ID3D11Buffer* buffer,DXGI_FORMAT format,UINT offset,unsigned phase_now) noexcept {
        if(!ready(c)) return false;
        const unsigned kind=kind_of(c);
        Slot& s=enter(c,kind,phase_now);
        const bool repeat=s.ia_index_known && s.ia_index==buffer && s.ia_index_format==static_cast<unsigned>(format) && s.ia_index_offset==offset;
        s.ia_index=buffer;s.ia_index_format=static_cast<unsigned>(format);s.ia_index_offset=offset;s.ia_index_known=true;
        return finish(kind,ia_index_buffer,repeat,repeat,s);
    }
    bool observe_ia_primitive_topology(ID3D11DeviceContext* c,D3D11_PRIMITIVE_TOPOLOGY topology,unsigned phase_now) noexcept {
        if(!ready(c)) return false;
        const unsigned kind=kind_of(c);
        Slot& s=enter(c,kind,phase_now);
        const bool repeat=s.ia_topology_known && s.ia_topology==static_cast<unsigned>(topology);
        s.ia_topology=static_cast<unsigned>(topology);s.ia_topology_known=true;
        return finish(kind,ia_primitive_topology,repeat,repeat,s);
    }
    // A NULL blend factor means (1,1,1,1), so it normalizes to the explicit value: the same state must count as a repeat.
    bool observe_om_blend_state(ID3D11DeviceContext* c,ID3D11BlendState* state,const FLOAT* factor,UINT sample_mask,unsigned phase_now) noexcept {
        if(!ready(c)) return false;
        const unsigned kind=kind_of(c);
        static const FLOAT ones[4]{1.f,1.f,1.f,1.f};
        const FLOAT* value=factor?factor:ones;
        Slot& s=enter(c,kind,phase_now);
        const bool repeat=s.om_blend_known && s.om_blend==state && s.om_sample_mask==sample_mask &&
            std::memcmp(s.om_blend_factor,value,sizeof(FLOAT)*4)==0;
        s.om_blend=state;s.om_sample_mask=sample_mask;
        for(unsigned i=0;i<4;++i) s.om_blend_factor[i]=value[i];
        s.om_blend_known=true;
        return finish(kind,om_blend_state,repeat,repeat,s);
    }
    bool observe_om_depth_stencil_state(ID3D11DeviceContext* c,ID3D11DepthStencilState* state,UINT stencil_ref,unsigned phase_now) noexcept {
        if(!ready(c)) return false;
        const unsigned kind=kind_of(c);
        Slot& s=enter(c,kind,phase_now);
        const bool repeat=s.om_depth_known && s.om_depth==state && s.om_stencil_ref==stencil_ref;
        s.om_depth=state;s.om_stencil_ref=stencil_ref;s.om_depth_known=true;
        return finish(kind,om_depth_stencil_state,repeat,repeat,s);
    }
    bool observe_rs_state(ID3D11DeviceContext* c,ID3D11RasterizerState* state,unsigned phase_now) noexcept {
        if(!ready(c)) return false;
        const unsigned kind=kind_of(c);
        Slot& s=enter(c,kind,phase_now);
        const bool repeat=s.rs_state_known && s.rs_state==state;
        s.rs_state=state;s.rs_state_known=true;
        return finish(kind,rs_state,repeat,repeat,s);
    }
    bool observe_rs_scissor_rects(ID3D11DeviceContext* c,UINT count,const D3D11_RECT* rects,unsigned phase_now) noexcept {
        if(!ready(c)) return false;
        const unsigned kind=kind_of(c);
        if(!rects && count) {invalidate_context(c,kind,phase_now);note(kind,rs_scissor_rects,false);return false;}
        if(!count) {note(kind,rs_scissor_rects,false);return false;}
        if(count>scissor_slots) {invalidate_context(c,kind,phase_now);note(kind,rs_scissor_rects,false);return false;}
        Slot& s=enter(c,kind,phase_now);
        const bool repeat=s.rs_scissors_known && s.rs_scissor_count==count &&
            std::memcmp(s.rs_scissors,rects,sizeof(D3D11_RECT)*count)==0;
        std::memcpy(s.rs_scissors,rects,sizeof(D3D11_RECT)*count);s.rs_scissor_count=count;s.rs_scissors_known=true;
        return finish(kind,rs_scissor_rects,repeat,repeat,s);
    }

    // ---- barriers: the same pass boundaries as PsStateCensus. ----
    void barrier(void* context) noexcept {
        if(!enabled_.load(std::memory_order_relaxed) || !context) return;
        Slot& s=slot_for(context);
        s.spin.acquire();
        if(s.context.load(std::memory_order_relaxed)==context) {invalidate(s);barriers_.fetch_add(1,std::memory_order_relaxed);}
        s.spin.release();
    }
    void barrier_all() noexcept {
        if(!enabled_.load(std::memory_order_relaxed)) return;
        for(auto& s:slots_) {s.spin.acquire();invalidate(s);s.spin.release();}
        barriers_.fetch_add(1,std::memory_order_relaxed);
    }
    void reset() noexcept {
        for(auto& kind:counter_shards_) for(auto& shard:kind) {
            for(unsigned method=0;method<method_count;++method) {
                shard.calls[method].store(0,std::memory_order_relaxed);
                shard.repeats[method].store(0,std::memory_order_relaxed);
                shard.skipped[method].store(0,std::memory_order_relaxed);
            }
        }
        adopts_.store(0,std::memory_order_relaxed);
        barriers_.store(0,std::memory_order_relaxed);
        for(auto& s:slots_) {s.spin.acquire();s.context.store(nullptr,std::memory_order_relaxed);invalidate(s);s.spin.release();}
    }
    std::string json() const {
        std::ostringstream out;
        out<<"{\"enabled\":"<<(enabled()?"true":"false")<<",\"elision\":"<<(elision()?"true":"false")<<",\"hooked\":[";
        for(unsigned method=0;method<method_count;++method) {
            if(method) out<<',';
            out<<((covered_[method][0] || covered_[method][1])?"true":"false");
        }
        out<<"],\"barrier_ready\":{\"immediate\":"<<(barrier_ready(immediate)?"true":"false")
           <<",\"deferred\":"<<(barrier_ready(deferred)?"true":"false")<<"}"
           <<",\"reason\":{\"immediate\":\""<<barrier_reason(immediate)<<"\",\"deferred\":\""<<barrier_reason(deferred)<<"\"}"
           <<",\"deferred\":{";kind_json(out,deferred);out<<"},\"immediate\":{";kind_json(out,immediate);
        out<<"},\"contexts\":"<<tracked_contexts()<<",\"adopts\":"<<adopts_.load(std::memory_order_relaxed)
           <<",\"barriers\":"<<barriers_.load(std::memory_order_relaxed)<<'}';
        return out.str();
    }

    // ---- installation: only the slots with no other owner. Idempotent; safe to call from the Present maintenance block. ----
    // The caller owns the one census object (dxgi_probe's wide_state); the MinHook bodies route to it through instance_.
    static void install(WideStateCensus& census,ID3D11Device* device) noexcept;
    static void disable() noexcept;
private:
#pragma warning(push)
#pragma warning(disable:4324) // the padding is the point: one cache line per recording thread
    struct alignas(64) CounterShard {
        std::atomic<std::uint64_t> calls[method_count]{},repeats[method_count]{},skipped[method_count]{};
    };
#pragma warning(pop)
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
        void* vs_srv[srv_slots]{};std::uint64_t vs_srv_known[2]{};
        void* vs_samplers[sampler_slots]{};std::uint32_t vs_sampler_known{};
        void* vs_cb[constant_buffer_slots]{};std::uint32_t vs_cb_known{};
        void* vs_cb1_buffer[constant_buffer_slots]{};std::uint32_t vs_cb1_first[constant_buffer_slots]{},vs_cb1_num[constant_buffer_slots]{};std::uint32_t vs_cb1_known{};
        void* ps_shader{};void* ps_classes[class_instance_slots]{};unsigned ps_class_count{~0u};bool ps_known{};
        void* vs_shader{};void* vs_classes[class_instance_slots]{};unsigned vs_class_count{~0u};bool vs_known{};
        void* ia_layout{};bool ia_layout_known{};
        void* ia_vb[vertex_buffer_slots]{};std::uint32_t ia_vb_stride[vertex_buffer_slots]{},ia_vb_offset[vertex_buffer_slots]{};std::uint32_t ia_vb_known{};
        void* ia_index{};unsigned ia_index_format{};unsigned ia_index_offset{};bool ia_index_known{};
        unsigned ia_topology{};bool ia_topology_known{};
        void* om_blend{};float om_blend_factor[4]{};unsigned om_sample_mask{};bool om_blend_known{};
        void* om_depth{};unsigned om_stencil_ref{};bool om_depth_known{};
        void* rs_state{};bool rs_state_known{};
        D3D11_RECT rs_scissors[scissor_slots]{};unsigned rs_scissor_count{};bool rs_scissors_known{};
    };
    static unsigned shard_index() noexcept {
        static std::atomic<unsigned> next{0};
        thread_local const unsigned index=next.fetch_add(1,std::memory_order_relaxed)%shard_count;
        return index;
    }
    static bool known_bit(const std::uint64_t* words,unsigned index) noexcept {return ((words[index/64u]>>(index%64u))&std::uint64_t(1))!=0;}
    static bool known_bit(std::uint64_t word,unsigned index) noexcept {return ((word>>(index%64u))&std::uint64_t(1))!=0;}
    static bool known_bit(std::uint32_t word,unsigned index) noexcept {return ((word>>(index%32u))&1u)!=0;}
    static void set_known(std::uint64_t* words,unsigned index) noexcept {words[index/64u]|=std::uint64_t(1)<<(index%64u);}
    static void set_known(std::uint64_t& word,unsigned index) noexcept {word|=std::uint64_t(1)<<(index%64u);}
    static void set_known(std::uint32_t& word,unsigned index) noexcept {word|=1u<<(index%32u);}
    static void clear_known(std::uint32_t& word,unsigned index) noexcept {word&=~(1u<<(index%32u));}
    static void invalidate(Slot& s) noexcept {
        ++s.generation;
        s.vs_srv_known[0]=s.vs_srv_known[1]=0;
        s.vs_sampler_known=0;s.vs_cb_known=0;s.vs_cb1_known=0;
        s.ps_known=false;s.vs_known=false;
        s.ia_layout_known=false;s.ia_vb_known=0;s.ia_index_known=false;s.ia_topology_known=false;
        s.om_blend_known=false;s.om_depth_known=false;s.rs_state_known=false;s.rs_scissors_known=false;
    }
    Slot& slot_for(void* context) noexcept {
        const auto address=reinterpret_cast<std::uintptr_t>(context);
        return slots_[(address>>4u)&(context_slots-1u)];
    }
    void adopt(Slot& s,void* context,unsigned phase_now) noexcept {
        s.context.store(context,std::memory_order_relaxed);
        s.phase=phase_now;
        invalidate(s);
        adopts_.fetch_add(1,std::memory_order_relaxed);
    }
    Slot& enter(ID3D11DeviceContext* c,unsigned kind,unsigned phase_now) noexcept {
        (void)kind;
        Slot& s=slot_for(c);
        s.spin.acquire();
        if(s.context.load(std::memory_order_relaxed)!=c) adopt(s,c,phase_now);
        else if(s.phase!=phase_now) {s.phase=phase_now;invalidate(s);}
        return s;
    }
    bool ready(ID3D11DeviceContext* c) const noexcept {
        return enabled_.load(std::memory_order_relaxed) && c && tracking();
    }
    void invalidate_context(ID3D11DeviceContext* c,unsigned kind,unsigned phase_now) noexcept {
        (void)kind;
        Slot& s=slot_for(c);
        s.spin.acquire();
        if(s.context.load(std::memory_order_relaxed)!=c) adopt(s,c,phase_now);
        else invalidate(s);
        s.spin.release();
    }
    void note(unsigned kind,unsigned method,bool repeat) noexcept {
        CounterShard& shard=counter_shards_[kind<kind_count?kind:deferred][shard_index()];
        shard.calls[method].fetch_add(1,std::memory_order_relaxed);
        if(repeat) shard.repeats[method].fetch_add(1,std::memory_order_relaxed);
    }
    // Called with the slot lock held. Counts the call/repeat, makes the elision decision on the already updated shadow, and
    // releases the lock. `elide_ok` is the method-specific extra rule (class instances for shaders, never for CB1).
    bool finish(unsigned kind,unsigned method,bool repeat,bool elide_ok,Slot& s) noexcept {
        CounterShard& shard=counter_shards_[kind<kind_count?kind:deferred][shard_index()];
        shard.calls[method].fetch_add(1,std::memory_order_relaxed);
        if(repeat) shard.repeats[method].fetch_add(1,std::memory_order_relaxed);
        const bool elide=repeat && elide_ok && elision_.load(std::memory_order_relaxed) && barrier_ready(kind);
        if(elide) shard.skipped[method].fetch_add(1,std::memory_order_relaxed);
        s.spin.release();
        return elide;
    }
    bool observe_shader(ID3D11DeviceContext* c,unsigned method,void* shader,
                        ID3D11ClassInstance* const* classes,UINT count,unsigned phase_now) noexcept {
        if(!ready(c)) return false;
        const unsigned kind=kind_of(c);
        if(count>class_instance_slots || (count && !classes)) {
            invalidate_context(c,kind,phase_now);
            note(kind,method,false);
            return false;
        }
        Slot& s=enter(c,kind,phase_now);
        void* stored_shader=method==ps_shader?static_cast<void*>(s.ps_shader):static_cast<void*>(s.vs_shader);
        const bool known=method==ps_shader?s.ps_known:s.vs_known;
        const unsigned stored_count=method==ps_shader?s.ps_class_count:s.vs_class_count;
        void* const* stored_classes=method==ps_shader?s.ps_classes:s.vs_classes;
        bool repeat=known && stored_shader==shader && stored_count==count;
        for(UINT i=0;i<count && repeat;++i) repeat=stored_classes[i]==classes[i];
        if(method==ps_shader) {
            s.ps_shader=shader;s.ps_class_count=count;s.ps_known=true;
            for(UINT i=0;i<count;++i) s.ps_classes[i]=classes[i];
        } else {
            s.vs_shader=shader;s.vs_class_count=count;s.vs_known=true;
            for(UINT i=0;i<count;++i) s.vs_classes[i]=classes[i];
        }
        // Method rule: only a no-class-instance bind can be elided. A class-list repeat is still counted as an opportunity.
        return finish(kind,method,repeat,repeat && count==0,s);
    }
    void kind_json(std::ostringstream& out,unsigned kind) const {
        for(unsigned method=0;method<method_count;++method) {
            if(method) out<<',';
            out<<'"'<<method_name(method)<<"\":{\"calls\":"<<calls(method,kind)<<",\"repeats\":"<<repeats(method,kind)
               <<",\"skipped\":"<<skipped(method,kind)<<'}';
        }
    }
    unsigned tracked_contexts() const noexcept {
        unsigned n=0;
        for(const auto& s:slots_) if(s.context.load(std::memory_order_relaxed)) ++n;
        return n;
    }

    // ---- hooks ----
    using VsSrvFn=void (STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT,ID3D11ShaderResourceView* const*);
    using VsSamplerFn=void (STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT,ID3D11SamplerState* const*);
    using PsShaderFn=void (STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11PixelShader*,ID3D11ClassInstance* const*,UINT);
    using IaLayoutFn=void (STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11InputLayout*);
    using IaVbFn=void (STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT,ID3D11Buffer* const*,const UINT*,const UINT*);
    using IaIndexFn=void (STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11Buffer*,DXGI_FORMAT,UINT);
    using IaTopologyFn=void (STDMETHODCALLTYPE*)(ID3D11DeviceContext*,D3D11_PRIMITIVE_TOPOLOGY);
    using OmBlendFn=void (STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11BlendState*,const FLOAT*,UINT);
    using OmDepthFn=void (STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11DepthStencilState*,UINT);
    using RsStateFn=void (STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11RasterizerState*);
    using RsScissorFn=void (STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,const D3D11_RECT*);

    static WideStateCensus& self() noexcept {return *instance_;}

    // Each hook offers the final arguments to the filter and forwards only when it says no: one place, both context kinds.
    template<int I> static void STDMETHODCALLTYPE vs_srv_hook(ID3D11DeviceContext* c,UINT start,UINT count,ID3D11ShaderResourceView* const* views) {
        if(self().observe_vs_shader_resources(c,start,count,views,phase())) return;
        reinterpret_cast<VsSrvFn>(original_[vs_shader_resources][I])(c,start,count,views);
    }
    template<int I> static void STDMETHODCALLTYPE vs_sampler_hook(ID3D11DeviceContext* c,UINT start,UINT count,ID3D11SamplerState* const* samplers) {
        if(self().observe_vs_samplers(c,start,count,samplers,phase())) return;
        reinterpret_cast<VsSamplerFn>(original_[vs_samplers][I])(c,start,count,samplers);
    }
    template<int I> static void STDMETHODCALLTYPE ps_shader_hook(ID3D11DeviceContext* c,ID3D11PixelShader* shader,ID3D11ClassInstance* const* classes,UINT count) {
        if(self().observe_ps_shader(c,shader,classes,count,phase())) return;
        reinterpret_cast<PsShaderFn>(original_[ps_shader][I])(c,shader,classes,count);
    }
    template<int I> static void STDMETHODCALLTYPE ia_layout_hook(ID3D11DeviceContext* c,ID3D11InputLayout* layout) {
        if(self().observe_ia_input_layout(c,layout,phase())) return;
        reinterpret_cast<IaLayoutFn>(original_[ia_input_layout][I])(c,layout);
    }
    template<int I> static void STDMETHODCALLTYPE ia_vb_hook(ID3D11DeviceContext* c,UINT start,UINT count,ID3D11Buffer* const* buffers,const UINT* strides,const UINT* offsets) {
        if(self().observe_ia_vertex_buffers(c,start,count,buffers,strides,offsets,phase())) return;
        reinterpret_cast<IaVbFn>(original_[ia_vertex_buffers][I])(c,start,count,buffers,strides,offsets);
    }
    template<int I> static void STDMETHODCALLTYPE ia_index_hook(ID3D11DeviceContext* c,ID3D11Buffer* buffer,DXGI_FORMAT format,UINT offset) {
        if(self().observe_ia_index_buffer(c,buffer,format,offset,phase())) return;
        reinterpret_cast<IaIndexFn>(original_[ia_index_buffer][I])(c,buffer,format,offset);
    }
    template<int I> static void STDMETHODCALLTYPE ia_topology_hook(ID3D11DeviceContext* c,D3D11_PRIMITIVE_TOPOLOGY topology) {
        if(self().observe_ia_primitive_topology(c,topology,phase())) return;
        reinterpret_cast<IaTopologyFn>(original_[ia_primitive_topology][I])(c,topology);
    }
    template<int I> static void STDMETHODCALLTYPE om_blend_hook(ID3D11DeviceContext* c,ID3D11BlendState* state,const FLOAT* factor,UINT mask) {
        if(self().observe_om_blend_state(c,state,factor,mask,phase())) return;
        reinterpret_cast<OmBlendFn>(original_[om_blend_state][I])(c,state,factor,mask);
    }
    template<int I> static void STDMETHODCALLTYPE om_depth_hook(ID3D11DeviceContext* c,ID3D11DepthStencilState* state,UINT ref) {
        if(self().observe_om_depth_stencil_state(c,state,ref,phase())) return;
        reinterpret_cast<OmDepthFn>(original_[om_depth_stencil_state][I])(c,state,ref);
    }
    template<int I> static void STDMETHODCALLTYPE rs_state_hook(ID3D11DeviceContext* c,ID3D11RasterizerState* state) {
        if(self().observe_rs_state(c,state,phase())) return;
        reinterpret_cast<RsStateFn>(original_[rs_state][I])(c,state);
    }
    template<int I> static void STDMETHODCALLTYPE rs_scissor_hook(ID3D11DeviceContext* c,UINT count,const D3D11_RECT* rects) {
        if(self().observe_rs_scissor_rects(c,count,rects,phase())) return;
        reinterpret_cast<RsScissorFn>(original_[rs_scissor_rects][I])(c,count,rects);
    }
    static unsigned vtable_slot(unsigned method) noexcept {
        static const unsigned slots[method_count]{
            25,26,7,119,9,11,17,18,19,24,35,36,43,45};
        return method<method_count?slots[method]:0u;
    }
    static constexpr bool hooked_here(unsigned method) noexcept {
        return method==vs_shader_resources || method==vs_samplers || method==ps_shader || method==ia_input_layout ||
            method==ia_vertex_buffers || method==ia_index_buffer || method==ia_primitive_topology ||
            method==om_blend_state || method==om_depth_stencil_state || method==rs_state || method==rs_scissor_rects;
    }
    static void* replacement(unsigned method,int kind) noexcept {
        switch(method) {
            case vs_shader_resources: return kind==0?reinterpret_cast<void*>(&vs_srv_hook<0>):reinterpret_cast<void*>(&vs_srv_hook<1>);
            case vs_samplers: return kind==0?reinterpret_cast<void*>(&vs_sampler_hook<0>):reinterpret_cast<void*>(&vs_sampler_hook<1>);
            case ps_shader: return kind==0?reinterpret_cast<void*>(&ps_shader_hook<0>):reinterpret_cast<void*>(&ps_shader_hook<1>);
            case ia_input_layout: return kind==0?reinterpret_cast<void*>(&ia_layout_hook<0>):reinterpret_cast<void*>(&ia_layout_hook<1>);
            case ia_vertex_buffers: return kind==0?reinterpret_cast<void*>(&ia_vb_hook<0>):reinterpret_cast<void*>(&ia_vb_hook<1>);
            case ia_index_buffer: return kind==0?reinterpret_cast<void*>(&ia_index_hook<0>):reinterpret_cast<void*>(&ia_index_hook<1>);
            case ia_primitive_topology: return kind==0?reinterpret_cast<void*>(&ia_topology_hook<0>):reinterpret_cast<void*>(&ia_topology_hook<1>);
            case om_blend_state: return kind==0?reinterpret_cast<void*>(&om_blend_hook<0>):reinterpret_cast<void*>(&om_blend_hook<1>);
            case om_depth_stencil_state: return kind==0?reinterpret_cast<void*>(&om_depth_hook<0>):reinterpret_cast<void*>(&om_depth_hook<1>);
            case rs_state: return kind==0?reinterpret_cast<void*>(&rs_state_hook<0>):reinterpret_cast<void*>(&rs_state_hook<1>);
            case rs_scissor_rects: return kind==0?reinterpret_cast<void*>(&rs_scissor_hook<0>):reinterpret_cast<void*>(&rs_scissor_hook<1>);
            default: return nullptr;
        }
    }
    static void hook_table(ID3D11DeviceContext* context,int kind,unsigned method) noexcept {
        if(!hooked_here(method)) return;
        void* target=(*reinterpret_cast<void***>(context))[vtable_slot(method)];
        if(!target) return;
        for(unsigned other=0;other<method_count;++other) for(int k=0;k<2;++k) {
            if(target_[other][static_cast<std::size_t>(k)]!=target) continue;
            if(other==method) {
                covered_[method][static_cast<std::size_t>(kind)]=true;
                covered_[method][static_cast<std::size_t>(k)]=true;
                if(!original_[method][static_cast<std::size_t>(kind)]) original_[method][static_cast<std::size_t>(kind)]=original_[method][static_cast<std::size_t>(k)];
                MH_EnableHook(target);
            }
            return; // never create a second MinHook on one code address
        }
        void* original{};
        if(MH_CreateHook(target,replacement(method,kind),&original)!=MH_OK) return;
        if(MH_EnableHook(target)!=MH_OK) {MH_RemoveHook(target);return;}
        target_[method][static_cast<std::size_t>(kind)]=target;
        original_[method][static_cast<std::size_t>(kind)]=original;
        covered_[method][static_cast<std::size_t>(kind)]=true;
    }
    inline static std::array<std::array<void*,2>,method_count> target_{};
    inline static std::array<std::array<void*,2>,method_count> original_{};
    inline static std::array<std::array<bool,2>,method_count> covered_{};
    inline static std::atomic<ID3D11DeviceContext*> immediate_context_{nullptr};
    inline static std::atomic<bool> enabled_{false};
    inline static WideStateCensus* instance_{nullptr};
    CounterShard counter_shards_[kind_count][shard_count];
    std::atomic<bool> elision_{false};
    std::atomic<std::uint64_t> adopts_{0};
    std::atomic<std::uint64_t> barriers_{0};
    Slot slots_[context_slots];
};

inline void WideStateCensus::install(WideStateCensus& census,ID3D11Device* device) noexcept {
    if(enabled_.load(std::memory_order_relaxed)) return;
    if(!device) return;
    instance_=&census;
    static constexpr unsigned methods[11]{
        vs_shader_resources,vs_samplers,ps_shader,ia_input_layout,ia_vertex_buffers,ia_index_buffer,
        ia_primitive_topology,om_blend_state,om_depth_stencil_state,rs_state,rs_scissor_rects};
    ID3D11DeviceContext* immediate{};
    device->GetImmediateContext(&immediate);
    if(immediate) {
        census.set_immediate_context(immediate);
        for(unsigned method:methods) hook_table(immediate,1,method);
        immediate->Release();
    }
    ID3D11DeviceContext* deferred{};
    if(SUCCEEDED(device->CreateDeferredContext(0,&deferred))) {
        for(unsigned method:methods) hook_table(deferred,0,method);
        deferred->Release();
    }
    enabled_.store(true,std::memory_order_relaxed);
}
inline void WideStateCensus::disable() noexcept {
    enabled_.store(false,std::memory_order_relaxed);
    for(auto& slot:target_) for(auto* target:slot) if(target) MH_DisableHook(target);
}
} // namespace d2vr
