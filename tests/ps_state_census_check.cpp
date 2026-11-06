// v993: the PS state census/elision model without D3D. Proves the invalidation rules (a skip is only offered while the shadow
// is known true and the context kind's barriers are installed), the opt-in knob default, the per-kind counters and adopt
// accounting, and that a hashed context collision costs elision, never correctness. The WARP lab next to it checks the same
// model against real readable D3D11 state.
#include "../src/ps_state_census.hpp"
#include "../src/elision_ab.hpp"
#include "../src/wide_state_census.hpp"
#include <cassert>
#include <cstring>
#include <iostream>
#include <memory>
#include <thread>

using Census=d2vr::PsStateCensus;
using Wide=d2vr::WideStateCensus;
static_assert(Census::context_slots==64,"v993 must track several deferred contexts from job threads");
static_assert(Census::kind_count==2,"census counters are split immediate/deferred");
static_assert(Census::hazard_count==4,"v995: 33/34/68/37 are four separate output-binding hazards");
static_assert(Census::hazard_all==15u,"all four output-binding hazard bits");
static_assert(Wide::method_count==14,"v997 wide census covers fourteen state methods");

static void* const a=reinterpret_cast<void*>(0x10);       // (0x10>>4)&63 == 1
static void* const b=reinterpret_cast<void*>(0x20);       // (0x20>>4)&63 == 2
static void* const collide=reinterpret_cast<void*>(0x410); // (0x410>>4)&63 == 1: same record as a
static void* const r0=reinterpret_cast<void*>(0x1000);
static void* const r1=reinterpret_cast<void*>(0x2000);
static void* const r2=reinterpret_cast<void*>(0x3000);
static void* const s0=reinterpret_cast<void*>(0x4000);
static void* const c0=reinterpret_cast<void*>(0x5000);

// v998: wide elision readiness comes from the PS census in production; the model lab supplies the same shape so a test can
// prove "not ready -> never skip" without a device.
static bool wide_ready[Wide::kind_count]{false,false};
static bool wide_ready_source(unsigned kind) noexcept {return kind<Wide::kind_count && wide_ready[kind];}
static const char* wide_reason_source(unsigned kind) noexcept {
    return kind<Wide::kind_count && !wide_ready[kind]?"test: wide barrier not ready":"";
}
static void set_wide_ready(bool immediate,bool deferred) noexcept {wide_ready[Wide::immediate]=immediate;wide_ready[Wide::deferred]=deferred;}
// The ObjectMotion hook order (object_motion.hpp observe_then_filter): its bookkeeping runs first and sees every call; the
// filter decides only whether the original is forwarded.
template<class Observe,class Filter> static bool observe_then_filter(Observe&& observe,Filter&& filter) {
    observe();
    return filter();
}

static bool bind(Census& census,void* context,unsigned method,unsigned start,unsigned count,
                 void* const* values,unsigned phase=0,unsigned kind=Census::immediate) {
    return census.consider(context,method,start,count,values,phase,kind);
}
template<class T> static T* ptr(std::uintptr_t value) {return reinterpret_cast<T*>(value);}

int main() {
    { // v993 default: the knob is off, so a repeat is counted but always forwarded
        Census census;
        assert(!census.elision());
        void* const first[2]{r0,r1};
        void* value{};
        assert(!bind(census,a,Census::resources,0,2,first));
        assert(census.shadow(a,Census::resources,0,value) && value==*first);
        assert(!bind(census,a,Census::resources,0,2,first));      // repeat, but elision off: forward it
        census.set_elision(true);
        census.set_barrier_ready(Census::immediate,true,"");
        assert(bind(census,a,Census::resources,0,2,first));       // repeat, elision on, barrier ready: skip
        assert(census.shadow(a,Census::resources,1,value) && value==r1);
        const auto json=census.json();
        assert(json.find("\"elision\":true")!=std::string::npos);
        assert(json.find("\"calls\":{\"immediate\":[3,0,0],\"deferred\":[0,0,0]}")!=std::string::npos);
        assert(json.find("\"repeats\":{\"immediate\":[2,0,0],\"deferred\":[0,0,0]}")!=std::string::npos);
        assert(json.find("\"skipped\":{\"immediate\":[1,0,0],\"deferred\":[0,0,0]}")!=std::string::npos);
        assert(json.find("\"adopts\":1")!=std::string::npos);
        assert(json.find("\"barrier_ready\":{\"immediate\":true,\"deferred\":false}")!=std::string::npos);
    }
    { // a kind whose barriers are not installed never elides, even with the knob on; the other kind still does
        Census census;census.set_elision(true);census.set_barrier_ready(Census::immediate,true,"");
        void* const v[1]{r0};
        assert(!bind(census,a,Census::resources,0,1,v,0,Census::deferred));
        assert(!bind(census,a,Census::resources,0,1,v,0,Census::deferred)); // repeat counted, no skip
        census.set_barrier_ready(Census::deferred,false,"FinishCommandList hook not installed");
        assert(!bind(census,a,Census::resources,0,1,v,0,Census::deferred));
        const auto json=census.json();
        assert(json.find("\"repeats\":{\"immediate\":[0,0,0],\"deferred\":[2,0,0]}")!=std::string::npos);
        assert(json.find("\"skipped\":{\"immediate\":[0,0,0],\"deferred\":[0,0,0]}")!=std::string::npos);
        assert(json.find("FinishCommandList hook not installed")!=std::string::npos);
    }
    { // one changed slot makes the whole range change; a partially known range is never a repeat
        Census census;census.set_elision(true);census.set_barrier_ready(Census::immediate,true,"");
        void* const v0[3]{r0,r1,r2};
        void* const v1[3]{r0,r2,r2};
        assert(!bind(census,a,Census::resources,4,3,v0));
        assert(!bind(census,a,Census::resources,4,3,v1));
        assert(bind(census,a,Census::resources,4,3,v1));
        assert(!bind(census,a,Census::resources,6,2,v0+1));       // slot 7 known, slot 8 never set
        void* const single[1]{r0};
        assert(!bind(census,a,Census::resources,0,1,single));     // a different range is new state
        assert(bind(census,a,Census::resources,0,1,single));
        assert(!bind(census,a,Census::resources,127,2,single));   // 127+2 is past the 128 slots: forwarded, untracked
        assert(!bind(census,a,Census::resources,0,0,single));     // zero-count call: forwarded, not a repeat
    }
    { // barriers and phases forget the shadow for that context only
        Census census;census.set_elision(true);census.set_barrier_ready(Census::immediate,true,"");
        void* const v[1]{r0};
        assert(!bind(census,a,Census::resources,0,1,v,0));
        assert(bind(census,a,Census::resources,0,1,v,0));
        census.barrier(a);
        assert(!bind(census,a,Census::resources,0,1,v,0));
        assert(bind(census,a,Census::resources,0,1,v,0));
        assert(!bind(census,a,Census::resources,0,1,v,1));        // phase change (pre -> eye0)
        assert(bind(census,a,Census::resources,0,1,v,1));
        census.barrier_all();
        assert(!bind(census,a,Census::resources,0,1,v,1));
        assert(census.json().find("\"adopts\":1")!=std::string::npos);  // a frame barrier is not a context takeover
        census.barrier(b);                                         // a context with no record: nothing to invalidate
        census.reset();
        const auto json=census.json();
        assert(json.find("\"calls\":{\"immediate\":[0,0,0],\"deferred\":[0,0,0]}")!=std::string::npos);
        assert(json.find("\"adopts\":0")!=std::string::npos);
    }
    { // per-context shadows and the samplers/constant-buffer arrays
        Census census;census.set_elision(true);census.set_barrier_ready(Census::immediate,true,"");
        void* const sa[1]{s0};
        void* const ca[1]{c0};
        assert(!bind(census,a,Census::samplers,3,1,sa));
        assert(bind(census,a,Census::samplers,3,1,sa));
        assert(!bind(census,b,Census::samplers,3,1,sa));          // b has never bound it
        assert(!bind(census,a,Census::constant_buffers,13,1,ca));
        assert(bind(census,a,Census::constant_buffers,13,1,ca));
        assert(!bind(census,a,Census::constant_buffers,14,1,ca)); // past the 14 constant-buffer slots
        assert(!bind(census,a,Census::method_count,0,1,sa));      // unknown method: counted nowhere, forwarded
    }
    { // a hash collision costs the elision on both contexts, never a wrong skip; the takeover is counted
        Census census;census.set_elision(true);census.set_barrier_ready(Census::immediate,true,"");
        void* const va[1]{r0};
        void* const vb[1]{r1};
        assert(!bind(census,a,Census::resources,0,1,va));
        assert(!bind(census,collide,Census::resources,0,1,vb));   // adopts the record: a's shadow is gone
        assert(!bind(census,a,Census::resources,0,1,va));         // must not be answered from b's shadow
        assert(!bind(census,collide,Census::resources,0,1,vb));
        void* value{};
        assert(census.shadow(collide,Census::resources,0,value) && value==r1);
        assert(!census.shadow(a,Census::resources,0,value));
        assert(census.json().find("\"adopts\":4")!=std::string::npos);
    }
    { // a skip is never offered for a context that never bound anything
        Census census;census.set_elision(true);census.set_barrier_ready(Census::immediate,true,"");
        assert(!bind(census,b,Census::resources,0,1,nullptr));
        void* const v[1]{r0};
        assert(!bind(census,b,Census::resources,0,1,v));
        assert(bind(census,b,Census::resources,0,1,v));
        assert(!bind(census,nullptr,Census::resources,0,1,v));
    }
    { // v995 hazard completeness: elision is refused until 33, 34, 68 and 37 are ALL covered for the kind
        Census census;census.set_elision(true);census.set_pass_ready(Census::deferred,true,"");
        void* const v[1]{r0};
        for(unsigned missing=0;missing<Census::hazard_count;++missing) {
            for(unsigned hazard=0;hazard<Census::hazard_count;++hazard)
                census.set_hazard_ready(Census::deferred,hazard,hazard!=missing,missing==hazard?"test: hazard not covered":"");
            assert(!census.barrier_ready(Census::deferred));
            assert(std::string(census.barrier_reason(Census::deferred)).find("test: hazard not covered")!=std::string::npos);
            assert(!bind(census,a,Census::resources,0,1,v,0,Census::deferred));  // always forwarded while one output route is missing
            assert(!bind(census,a,Census::resources,0,1,v,0,Census::deferred));  // and a repeat is not skipped
            assert(census.json().find("\"skipped\":{\"immediate\":[0,0,0],\"deferred\":[0,0,0]}")!=std::string::npos);
        }
        for(unsigned hazard=0;hazard<Census::hazard_count;++hazard)
            census.set_hazard_ready(Census::deferred,hazard,true,"");
        assert(census.barrier_ready(Census::deferred));                          // all four covered: the repeat now skips
        assert(bind(census,a,Census::resources,0,1,v,0,Census::deferred));
        assert(census.json().find("\"hazard_ready\":{\"immediate\":[false,false,false,false],\"deferred\":[true,true,true,true]}")!=std::string::npos);
    }
    { // v995 PSSetConstantBuffers1 is never elided and marks its covered CB slots unknown
        Census census;census.set_elision(true);census.set_barrier_ready(Census::deferred,true,"");
        void* const cb[1]{c0};
        assert(!bind(census,a,Census::constant_buffers,0,1,cb,0,Census::deferred));
        assert(bind(census,a,Census::constant_buffers,0,1,cb,0,Census::deferred));       // base repeat: skip
        census.note_constant_buffers1(a,0,1,Census::deferred);                            // CB1 same pointer, different offset
        assert(census.cb1_calls(Census::deferred)==1);
        assert(!bind(census,a,Census::constant_buffers,0,1,cb,0,Census::deferred));      // must be forwarded, not skipped
        assert(bind(census,a,Census::constant_buffers,0,1,cb,0,Census::deferred));        // known again: repeat
        census.note_constant_buffers1(a,0,1,Census::deferred);census.note_constant_buffers1(a,0,1,Census::deferred);
        assert(census.cb1_calls(Census::deferred)==3);                                    // every CB1 call counted
        assert(census.cb1_calls(Census::immediate)==0);
        assert(!bind(census,a,Census::constant_buffers,0,2,cb,0,Census::deferred));      // slot 1 remains known but slot 0 is not: never a repeat
        assert(census.json().find("\"cb1_calls\":{\"immediate\":0,\"deferred\":3}")!=std::string::npos);
    }
    { // v996 SwapDeviceContextState: barriers only the swapping context, counts and records the caller; elision and deferred
      // readiness stay available, because an immediate swap cannot change a deferred recording stream's state.
        Census census;census.set_elision(true);
        census.set_barrier_ready(Census::immediate,true,"");
        census.set_barrier_ready(Census::deferred,true,"");
        void* const v[1]{r0};
        assert(!bind(census,b,Census::resources,0,1,v,0,Census::deferred));
        assert(bind(census,b,Census::resources,0,1,v,0,Census::deferred));             // deferred shadow known: skip
        assert(!bind(census,a,Census::resources,0,1,v,0,Census::immediate));
        assert(bind(census,a,Census::resources,0,1,v,0,Census::immediate));            // immediate shadow known: skip
        census.note_device_context_state_swap(a,"Dishonored2.exe",0x1234);
        assert(census.state_swap_count()==1);
        assert(census.barrier_ready(Census::immediate));                               // readiness does not depend on the swap
        assert(census.barrier_ready(Census::deferred));
        assert(!bind(census,a,Census::resources,0,1,v,0,Census::immediate));           // first bind after the swap is forwarded
        assert(bind(census,b,Census::resources,0,1,v,0,Census::deferred));             // deferred shadow untouched: still skips
        census.note_device_context_state_swap(a,"Dishonored2.exe",0x1234);
        census.note_device_context_state_swap(b,"d3d11.dll",0x5678);
        const auto json=census.json();
        assert(json.find("\"state_swaps\":{\"count\":3,\"callers\":[{\"module\":\"Dishonored2.exe\",\"rva\":4660,\"count\":2},"
            "{\"module\":\"d3d11.dll\",\"rva\":22136,\"count\":1}]}")!=std::string::npos);
        assert(json.find("SwapDeviceContextState observed")==std::string::npos);
        Census many_census;
        for(unsigned i=0;i<Census::state_swap_caller_limit+3;++i)
            many_census.note_device_context_state_swap(a,"Dishonored2.exe",0x9000u+i*0x10u);
        const auto many=many_census.json();
        assert(many.find("\"count\":11")!=std::string::npos);
        assert(many.find("\"module\":\"Dishonored2.exe\",\"rva\":36864")!=std::string::npos);       // first distinct caller
        assert(many.find("\"module\":\"Dishonored2.exe\",\"rva\":36976")!=std::string::npos);       // eighth
        assert(many.find("\"module\":\"Dishonored2.exe\",\"rva\":36992")==std::string::npos);       // ninth: counted, not listed
    }
    { // v997 live toggle: the shadow is maintained with elision off, so switching on mid-session skips the very next repeat,
      // and switching off again forwards it. Nothing is re-recorded and no barrier is needed for the switch itself.
        Census census;census.set_barrier_ready(Census::immediate,true,"");
        void* value{};
        void* const v[1]{r0};
        assert(!bind(census,a,Census::resources,0,1,v));   // off: forwarded, shadow recorded
        assert(!bind(census,a,Census::resources,0,1,v));   // off: repeat counted, still forwarded
        census.set_elision(true);
        assert(bind(census,a,Census::resources,0,1,v));    // on mid-session: skipped without a rebind
        assert(census.shadow(a,Census::resources,0,value) && value==r0);
        census.set_elision(false);
        assert(!bind(census,a,Census::resources,0,1,v));   // off again: forwarded
        const auto json=census.json();
        assert(json.find("\"calls\":{\"immediate\":[4,0,0],\"deferred\":[0,0,0]}")!=std::string::npos);
        assert(json.find("\"repeats\":{\"immediate\":[3,0,0],\"deferred\":[0,0,0]}")!=std::string::npos);
        assert(json.find("\"skipped\":{\"immediate\":[1,0,0],\"deferred\":[0,0,0]}")!=std::string::npos);
    }
    { // v997 sharded PS counters: four threads on four contexts post to their own cache lines; the status read sums them.
        Census census;
        std::array<std::thread,4> threads;
        for(unsigned t=0;t<threads.size();++t) threads[t]=std::thread([&census,t] {
            void* const context=reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x10000u+t*0x100u));
            void* const v[1]{r0};
            for(int i=0;i<250;++i) bind(census,context,Census::resources,0,1,v,0,Census::immediate);
        });
        for(auto& thread:threads) thread.join();
        assert(census.calls(Census::immediate,Census::resources)==1000);
        assert(census.repeats(Census::immediate,Census::resources)==996);
        const auto json=census.json();
        assert(json.find("\"calls\":{\"immediate\":[1000,0,0]")!=std::string::npos);
    }
    { // v998 ElisionAb: FOUR windows keyed by the combined PS x wide state, nearest-rank percentiles from the shared FrameSplit
      // helper, per-frame skipped/barrier counts, the per-frame largest list p90, and a per-frame TOTAL replay histogram whose
      // buckets sum to that window's frames (the v997 defect: it summed per-list buckets).
        auto ab=std::make_unique<d2vr::ElisionAb>();
        for(int i=0;i<5;++i) {
            d2vr::ReplayFrame replay;replay.valid=true;
            replay.total_ns=static_cast<std::uint64_t>(i+1)*1'000'000ull;
            replay.max_list_ns=replay.total_ns;
            replay.hist[0]=1;replay.hist[7]=2; // must be ignored by the A/B histogram
            ab->feed(false,false,replay,static_cast<std::uint64_t>(10+i)*1'000'000ull,2'000'000,3'000'000,1'000'000,0,
                static_cast<std::uint64_t>(i));
        }
        for(int i=0;i<5;++i) {
            d2vr::ReplayFrame replay;replay.valid=true;
            replay.total_ns=static_cast<std::uint64_t>(i+1)*2'000'000ull;
            replay.max_list_ns=replay.total_ns;
            replay.hist[1]=3;
            ab->feed(true,false,replay,static_cast<std::uint64_t>(20+i)*1'000'000ull,4'000'000,5'000'000,2'000'000,
                static_cast<std::uint64_t>(100+i),static_cast<std::uint64_t>(10+i));
        }
        for(int i=0;i<3;++i) {
            d2vr::ReplayFrame replay;replay.valid=true;replay.total_ns=static_cast<std::uint64_t>(i==2?1'000'000ull:0ull);
            ab->feed(false,true,replay,static_cast<std::uint64_t>(30+i)*1'000'000ull,6'000'000,7'000'000,3'000'000,7,1);
        }
        for(int i=0;i<2;++i) {
            d2vr::ReplayFrame replay;replay.valid=true;replay.total_ns=static_cast<std::uint64_t>(20+i*10)*1'000'000ull;
            ab->feed(true,true,replay,static_cast<std::uint64_t>(40+i)*1'000'000ull,8'000'000,9'000'000,4'000'000,9,2);
        }
        ab->note_toggle();ab->note_toggle();ab->note_toggle();
        const auto json=ab->json();
        assert(json.find("\"ps_off_wide_off\":{\"frames\":5,\"ecl_ms_p50\":3,\"ecl_ms_p90\":5,\"interval_ms_p50\":12,"
            "\"interval_ms_p95\":14,\"eye0_ms_p50\":2,\"eye1_ms_p50\":3,\"hook_ms_p50\":1,\"skipped_per_frame_p50\":0,"
            "\"barriers_p50\":2,\"barriers_p90\":4,\"max_list_ms_p90\":5,\"ecl_hist\":[0,1,2,2,0,0,0,0]}")!=std::string::npos);
        assert(json.find("\"ps_on_wide_off\":{\"frames\":5,\"ecl_ms_p50\":6,\"ecl_ms_p90\":10,\"interval_ms_p50\":22,"
            "\"interval_ms_p95\":24,\"eye0_ms_p50\":4,\"eye1_ms_p50\":5,\"hook_ms_p50\":2,\"skipped_per_frame_p50\":102,"
            "\"barriers_p50\":12,\"barriers_p90\":14,\"max_list_ms_p90\":10,\"ecl_hist\":[0,0,1,1,1,2,0,0]}")!=std::string::npos);
        assert(json.find("\"ps_off_wide_on\":{\"frames\":3,\"ecl_ms_p50\":0,\"ecl_ms_p90\":1,")!=std::string::npos);
        assert(json.find("\"skipped_per_frame_p50\":7,\"barriers_p50\":1,\"barriers_p90\":1,")!=std::string::npos);
        assert(json.find("\"ecl_hist\":[2,1,0,0,0,0,0,0]}")!=std::string::npos);
        assert(json.find("\"ps_on_wide_on\":{\"frames\":2,\"ecl_ms_p50\":20,\"ecl_ms_p90\":30,")!=std::string::npos);
        assert(json.find("\"skipped_per_frame_p50\":9,\"barriers_p50\":2,\"barriers_p90\":2,")!=std::string::npos);
        assert(json.find("\"ecl_hist\":[0,0,0,0,0,0,0,2]}")!=std::string::npos);
        assert(json.find("\"toggles\":3")!=std::string::npos);
        assert(ab->frames(false,false)==5 && ab->frames(true,false)==5 && ab->frames(false,true)==3 && ab->frames(true,true)==2);
        ab->reset();
        const auto empty=ab->json();
        for(const char* name:{"ps_off_wide_off","ps_on_wide_off","ps_off_wide_on","ps_on_wide_on"}) {
            std::string key=std::string("\"")+name+"\":{\"frames\":0";
            assert(empty.find(key)!=std::string::npos);
        }
        assert(empty.find("\"ecl_hist\":[0,0,0,0,0,0,0,0]")!=std::string::npos);
        assert(empty.find("\"toggles\":0")!=std::string::npos);
    }
    { // v997 wide census: exact-repeat rules and the parameter comparisons, method by method, on the deferred kind.
        auto wide=std::make_unique<Wide>();wide->set_enabled(true);
        auto* c=ptr<ID3D11DeviceContext>(0x20000);
        auto* b0=ptr<ID3D11Buffer>(0x7000),*b1=ptr<ID3D11Buffer>(0x7008);
        auto* srv0=ptr<ID3D11ShaderResourceView>(0x7100),*srv1=ptr<ID3D11ShaderResourceView>(0x7108);
        auto* sampler0=ptr<ID3D11SamplerState>(0x7200),*sampler1=ptr<ID3D11SamplerState>(0x7208);
        auto* ps0=ptr<ID3D11PixelShader>(0x7300),*ps1=ptr<ID3D11PixelShader>(0x7308);
        auto* vs0=ptr<ID3D11VertexShader>(0x7400);
        auto* layout0=ptr<ID3D11InputLayout>(0x7500),*layout1=ptr<ID3D11InputLayout>(0x7508);
        auto* blend0=ptr<ID3D11BlendState>(0x7600);
        auto* depth0=ptr<ID3D11DepthStencilState>(0x7700);
        auto* raster0=ptr<ID3D11RasterizerState>(0x7800);
        auto* class0=ptr<ID3D11ClassInstance>(0x7900);
        ID3D11ClassInstance* one_class[1]{class0};
        // VS SRVs: identical range repeats; a different view or an unbound slot does not.
        wide->observe_vs_shader_resources(c,0,1,&srv0,0);
        wide->observe_vs_shader_resources(c,0,1,&srv0,0);
        wide->observe_vs_shader_resources(c,0,1,&srv1,0);
        wide->observe_vs_shader_resources(c,0,1,&srv1,0);
        wide->observe_vs_shader_resources(c,1,1,&srv1,0);
        // VS samplers.
        wide->observe_vs_samplers(c,0,1,&sampler0,0);
        wide->observe_vs_samplers(c,0,1,&sampler0,0);
        wide->observe_vs_samplers(c,0,1,&sampler1,0);
        // VS constant buffers: base repeat, then CB1 with offsets clears the base shadow, then base repeat again.
        wide->observe_vs_constant_buffers(c,0,1,&b0,0);
        wide->observe_vs_constant_buffers(c,0,1,&b0,0);
        const UINT first16[1]{16},num16[1]{16},first8[1]{8};
        wide->observe_vs_constant_buffers1(c,0,1,&b0,first16,num16,0);
        wide->observe_vs_constant_buffers1(c,0,1,&b0,first16,num16,0);
        wide->observe_vs_constant_buffers1(c,0,1,&b0,first8,num16,0);   // same buffer, different first: not a repeat
        wide->observe_vs_constant_buffers1(c,0,1,&b0,first8,num16,0);
        wide->observe_vs_constant_buffers(c,0,1,&b0,0);                // base after CB1 is not a repeat
        wide->observe_vs_constant_buffers(c,0,1,&b0,0);
        // Shader + class-instance list.
        wide->observe_ps_shader(c,ps0,nullptr,0,0);
        wide->observe_ps_shader(c,ps0,nullptr,0,0);
        wide->observe_ps_shader(c,ps0,one_class,1,0);
        wide->observe_ps_shader(c,ps0,one_class,1,0);
        wide->observe_ps_shader(c,ps1,nullptr,0,0);
        wide->observe_vs_shader(c,vs0,nullptr,0,0);
        wide->observe_vs_shader(c,vs0,nullptr,0,0);
        // IA.
        wide->observe_ia_input_layout(c,layout0,0);
        wide->observe_ia_input_layout(c,layout0,0);
        wide->observe_ia_input_layout(c,layout1,0);
        const UINT stride12[1]{12},stride16[1]{16},offset0[1]{0},offset4[1]{4};
        wide->observe_ia_vertex_buffers(c,0,1,&b0,stride12,offset0,0);
        wide->observe_ia_vertex_buffers(c,0,1,&b0,stride12,offset0,0);
        wide->observe_ia_vertex_buffers(c,0,1,&b0,stride16,offset0,0);  // different stride: not a repeat
        wide->observe_ia_vertex_buffers(c,0,1,&b0,stride16,offset4,0);  // different offset: not a repeat
        wide->observe_ia_vertex_buffers(c,0,1,&b0,stride16,offset4,0);
        wide->observe_ia_vertex_buffers(c,0,1,&b1,stride16,offset4,0);  // different buffer: not a repeat
        wide->observe_ia_index_buffer(c,b0,DXGI_FORMAT_R16_UINT,0,0);
        wide->observe_ia_index_buffer(c,b0,DXGI_FORMAT_R16_UINT,0,0);
        wide->observe_ia_index_buffer(c,b0,DXGI_FORMAT_R32_UINT,0,0);   // different format: not a repeat
        wide->observe_ia_index_buffer(c,b0,DXGI_FORMAT_R32_UINT,4,0);   // different offset: not a repeat
        wide->observe_ia_index_buffer(c,b0,DXGI_FORMAT_R32_UINT,4,0);
        // Topology: repeat, phase change invalidates, barrier invalidates, other context has no shadow.
        wide->observe_ia_primitive_topology(c,D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,0);
        wide->observe_ia_primitive_topology(c,D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,0);
        wide->observe_ia_primitive_topology(c,D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,1); // phase change: not a repeat
        wide->observe_ia_primitive_topology(c,D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,1);
        wide->barrier(c);
        wide->observe_ia_primitive_topology(c,D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,1); // after a pass barrier: not a repeat
        wide->observe_ia_primitive_topology(c,D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,1);
        wide->barrier_all();
        wide->observe_ia_primitive_topology(c,D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,1);
        wide->observe_ia_primitive_topology(c,D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,1);
        auto* other=ptr<ID3D11DeviceContext>(0x21000);
        wide->observe_ia_primitive_topology(other,D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,1); // a different context: no shadow
        // Fixed-function blends: state + all four factors + mask; a NULL factor normalizes to (1,1,1,1).
        const FLOAT factor0[4]{0.25f,0.5f,0.75f,1.f},factor1[4]{0.5f,0.5f,0.75f,1.f},ones[4]{1.f,1.f,1.f,1.f};
        wide->observe_om_blend_state(c,blend0,factor0,0x0f0f0f0fu,0);
        wide->observe_om_blend_state(c,blend0,factor0,0x0f0f0f0fu,0);
        wide->observe_om_blend_state(c,blend0,factor1,0x0f0f0f0fu,0);   // different factor: not a repeat
        wide->observe_om_blend_state(c,blend0,factor1,0x0f0f0f0fu,0);
        wide->observe_om_blend_state(c,blend0,nullptr,0x0f0f0f0fu,0);   // normalizes to ones, so not factor1
        wide->observe_om_blend_state(c,blend0,ones,0x0f0f0f0fu,0);      // same as the NULL-factor call: repeat
        wide->observe_om_blend_state(c,blend0,ones,0x00ff00ffu,0);      // different mask: not a repeat
        wide->observe_om_depth_stencil_state(c,depth0,7,0);
        wide->observe_om_depth_stencil_state(c,depth0,7,0);
        wide->observe_om_depth_stencil_state(c,depth0,8,0);
        wide->observe_rs_state(c,raster0,0);
        wide->observe_rs_state(c,raster0,0);
        const D3D11_RECT rects_a[2]{{0,0,4,4},{1,2,3,4}},rects_b[2]{{0,0,8,8},{1,2,3,4}};
        wide->observe_rs_scissor_rects(c,2,rects_a,0);
        wide->observe_rs_scissor_rects(c,2,rects_a,0);
        wide->observe_rs_scissor_rects(c,2,rects_b,0);
        wide->observe_rs_scissor_rects(c,2,rects_b,0);
        assert(wide->calls(Wide::vs_shader_resources,Wide::deferred)==5);
        assert(wide->repeats(Wide::vs_shader_resources,Wide::deferred)==2);
        assert(wide->calls(Wide::vs_samplers,Wide::deferred)==3 && wide->repeats(Wide::vs_samplers,Wide::deferred)==1);
        assert(wide->calls(Wide::vs_constant_buffers,Wide::deferred)==4 && wide->repeats(Wide::vs_constant_buffers,Wide::deferred)==2);
        assert(wide->calls(Wide::vs_constant_buffers1,Wide::deferred)==4 && wide->repeats(Wide::vs_constant_buffers1,Wide::deferred)==2);
        assert(wide->calls(Wide::ps_shader,Wide::deferred)==5 && wide->repeats(Wide::ps_shader,Wide::deferred)==2);
        assert(wide->calls(Wide::vs_shader,Wide::deferred)==2 && wide->repeats(Wide::vs_shader,Wide::deferred)==1);
        assert(wide->calls(Wide::ia_input_layout,Wide::deferred)==3 && wide->repeats(Wide::ia_input_layout,Wide::deferred)==1);
        assert(wide->calls(Wide::ia_vertex_buffers,Wide::deferred)==6 && wide->repeats(Wide::ia_vertex_buffers,Wide::deferred)==2);
        assert(wide->calls(Wide::ia_index_buffer,Wide::deferred)==5 && wide->repeats(Wide::ia_index_buffer,Wide::deferred)==2);
        assert(wide->calls(Wide::ia_primitive_topology,Wide::deferred)==9 && wide->repeats(Wide::ia_primitive_topology,Wide::deferred)==4);
        assert(wide->calls(Wide::om_blend_state,Wide::deferred)==7 && wide->repeats(Wide::om_blend_state,Wide::deferred)==3);
        assert(wide->calls(Wide::om_depth_stencil_state,Wide::deferred)==3 && wide->repeats(Wide::om_depth_stencil_state,Wide::deferred)==1);
        assert(wide->calls(Wide::rs_state,Wide::deferred)==2 && wide->repeats(Wide::rs_state,Wide::deferred)==1);
        assert(wide->calls(Wide::rs_scissor_rects,Wide::deferred)==4 && wide->repeats(Wide::rs_scissor_rects,Wide::deferred)==2);
        const auto json=wide->json();
        assert(json.find("\"enabled\":true")!=std::string::npos);
        assert(json.find("\"VSSetConstantBuffers1\":{\"calls\":4,\"repeats\":2,\"skipped\":0}")!=std::string::npos);
        assert(json.find("\"IASetVertexBuffers\":{\"calls\":6,\"repeats\":2,\"skipped\":0}")!=std::string::npos);
        assert(json.find("\"OMSetBlendState\":{\"calls\":7,\"repeats\":3,\"skipped\":0}")!=std::string::npos);
        assert(json.find("\"RSSetScissorRects\":{\"calls\":4,\"repeats\":2,\"skipped\":0}")!=std::string::npos);
        assert(json.find("\"elision\":false")!=std::string::npos);
        // The immediate kind is counted separately.
        auto* immediate_context=ptr<ID3D11DeviceContext>(0x22000);
        wide->set_immediate_context(immediate_context);
        wide->observe_ia_primitive_topology(immediate_context,D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,1);
        wide->observe_ia_primitive_topology(immediate_context,D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,1);
        assert(wide->calls(Wide::ia_primitive_topology,Wide::immediate)==2);
        assert(wide->repeats(Wide::ia_primitive_topology,Wide::immediate)==1);
        assert(wide->calls(Wide::ia_primitive_topology,Wide::deferred)==9);
    }
    { // v998 wide elision model: every method rule, the negative parameter cases, readiness/barriers/phase, the CB1 rule and
      // ObjectMotion's direct pool substitution. A skip is only ever offered for an exact repeat with readiness proven.
        Wide::barrier_ready_source.store(&wide_ready_source);
        Wide::barrier_reason_source.store(&wide_reason_source);
        auto wide=std::make_unique<Wide>();wide->set_enabled(true);
        auto* c=ptr<ID3D11DeviceContext>(0x40000);
        auto* b0=ptr<ID3D11Buffer>(0x8000),*b1=ptr<ID3D11Buffer>(0x8008),*pool=ptr<ID3D11Buffer>(0x8010);
        auto* srv0=ptr<ID3D11ShaderResourceView>(0x8100),*srv1=ptr<ID3D11ShaderResourceView>(0x8108);
        auto* sampler0=ptr<ID3D11SamplerState>(0x8200),*sampler1=ptr<ID3D11SamplerState>(0x8208);
        auto* ps0=ptr<ID3D11PixelShader>(0x8300),*ps1=ptr<ID3D11PixelShader>(0x8308);
        auto* vs0=ptr<ID3D11VertexShader>(0x8400),*vs1=ptr<ID3D11VertexShader>(0x8408);
        auto* layout0=ptr<ID3D11InputLayout>(0x8500),*layout1=ptr<ID3D11InputLayout>(0x8508);
        auto* blend0=ptr<ID3D11BlendState>(0x8600);
        auto* depth0=ptr<ID3D11DepthStencilState>(0x8700);
        auto* raster0=ptr<ID3D11RasterizerState>(0x8800),*raster1=ptr<ID3D11RasterizerState>(0x8808);
        auto* class0=ptr<ID3D11ClassInstance>(0x8900);
        ID3D11ClassInstance* one_class[1]{class0};
        // v998 opt-in: with the knob off an exact repeat is counted but never skipped.
        assert(!wide->observe_ia_primitive_topology(c,D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,0));
        assert(!wide->observe_ia_primitive_topology(c,D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,0));
        assert(wide->repeats(Wide::ia_primitive_topology,Wide::deferred)==1);
        assert(wide->skipped(Wide::ia_primitive_topology,Wide::deferred)==0);
        wide->set_elision(true);
        // Not ready: still never skips.
        assert(!wide->observe_ia_primitive_topology(c,D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,0));
        set_wide_ready(true,true);
        // Now the exact repeat skips.
        assert(wide->observe_ia_primitive_topology(c,D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,0));
        assert(wide->skipped(Wide::ia_primitive_topology,Wide::deferred)==1);
        // Per-method exact repeats and every parameter comparison.
        assert(!wide->observe_ia_input_layout(c,layout0,0));
        assert(wide->observe_ia_input_layout(c,layout0,0));
        assert(!wide->observe_ia_input_layout(c,layout1,0));
        assert(!wide->observe_rs_state(c,raster0,0));
        assert(wide->observe_rs_state(c,raster0,0));
        assert(!wide->observe_rs_state(c,raster1,0));
        assert(!wide->observe_om_depth_stencil_state(c,depth0,7,0));
        assert(wide->observe_om_depth_stencil_state(c,depth0,7,0));
        assert(!wide->observe_om_depth_stencil_state(c,depth0,8,0));  // changed stencil ref
        const FLOAT factor0[4]{0.25f,0.5f,0.75f,1.f},factor1[4]{0.5f,0.5f,0.75f,1.f},ones[4]{1.f,1.f,1.f,1.f};
        assert(!wide->observe_om_blend_state(c,blend0,factor0,0x0f0f0f0fu,0));
        assert(wide->observe_om_blend_state(c,blend0,factor0,0x0f0f0f0fu,0));
        assert(!wide->observe_om_blend_state(c,blend0,factor1,0x0f0f0f0fu,0)); // changed factor
        assert(wide->observe_om_blend_state(c,blend0,factor1,0x0f0f0f0fu,0));
        assert(!wide->observe_om_blend_state(c,blend0,factor1,0x00ff00ffu,0)); // changed mask
        assert(!wide->observe_om_blend_state(c,blend0,nullptr,0x00ff00ffu,0)); // NULL = (1,1,1,1), not factor1
        assert(wide->observe_om_blend_state(c,blend0,ones,0x00ff00ffu,0));     // same as the NULL-factor state
        const D3D11_RECT rects_a[2]{{0,0,4,4},{1,2,3,4}},rects_b[2]{{0,0,8,8},{1,2,3,4}};
        assert(!wide->observe_rs_scissor_rects(c,2,rects_a,0));
        assert(wide->observe_rs_scissor_rects(c,2,rects_a,0));
        assert(!wide->observe_rs_scissor_rects(c,2,rects_b,0)); // changed rect
        assert(!wide->observe_rs_scissor_rects(c,3,rects_a,0)); // changed count
        // Shaders: an exact repeat with class instances is counted but never elided.
        assert(!wide->observe_ps_shader(c,ps0,nullptr,0,0));
        assert(wide->observe_ps_shader(c,ps0,nullptr,0,0));
        assert(!wide->observe_ps_shader(c,ps0,one_class,1,0));
        assert(!wide->observe_ps_shader(c,ps0,one_class,1,0)); // same class list, count 1: repeat, not skipped
        assert(wide->repeats(Wide::ps_shader,Wide::deferred)>=1);
        assert(!wide->observe_ps_shader(c,ps1,nullptr,0,0));
        assert(!wide->observe_vs_shader(c,vs0,nullptr,0,0));
        assert(wide->observe_vs_shader(c,vs0,nullptr,0,0));
        assert(!wide->observe_vs_shader(c,vs0,one_class,1,0));
        assert(!wide->observe_vs_shader(c,vs1,nullptr,0,0));
        // VS SRV/samplers, ranges and values.
        assert(!wide->observe_vs_shader_resources(c,0,1,&srv0,0));
        assert(wide->observe_vs_shader_resources(c,0,1,&srv0,0));
        assert(!wide->observe_vs_shader_resources(c,0,1,&srv1,0));
        assert(!wide->observe_vs_shader_resources(c,1,1,&srv1,0));
        assert(!wide->observe_vs_samplers(c,0,1,&sampler0,0));
        assert(wide->observe_vs_samplers(c,0,1,&sampler0,0));
        assert(!wide->observe_vs_samplers(c,0,1,&sampler1,0));
        // IA VB: all three parameters; IB: buffer + format + offset.
        const UINT stride12[1]{12},stride16[1]{16},offset0[1]{0},offset4[1]{4};
        assert(!wide->observe_ia_vertex_buffers(c,0,1,&b0,stride12,offset0,0));
        assert(wide->observe_ia_vertex_buffers(c,0,1,&b0,stride12,offset0,0));
        assert(!wide->observe_ia_vertex_buffers(c,0,1,&b0,stride16,offset0,0));
        assert(!wide->observe_ia_vertex_buffers(c,0,1,&b0,stride16,offset4,0));
        assert(wide->observe_ia_vertex_buffers(c,0,1,&b0,stride16,offset4,0));
        assert(!wide->observe_ia_vertex_buffers(c,0,1,&b1,stride16,offset4,0));
        assert(!wide->observe_ia_index_buffer(c,b0,DXGI_FORMAT_R16_UINT,0,0));
        assert(wide->observe_ia_index_buffer(c,b0,DXGI_FORMAT_R16_UINT,0,0));
        assert(!wide->observe_ia_index_buffer(c,b0,DXGI_FORMAT_R32_UINT,0,0));
        assert(!wide->observe_ia_index_buffer(c,b0,DXGI_FORMAT_R32_UINT,4,0));
        // VS CB1 is NEVER elided; it invalidates the covered base slot, so the next identical base bind is forwarded.
        const UINT first16[1]{16},num16[1]{16};
        assert(!wide->observe_vs_constant_buffers(c,0,1,&b0,0));
        assert(wide->observe_vs_constant_buffers(c,0,1,&b0,0));
        assert(!wide->observe_vs_constant_buffers1(c,0,1,&b0,first16,num16,0));
        assert(!wide->observe_vs_constant_buffers1(c,0,1,&b0,first16,num16,0)); // repeat, but CB1 is never elided
        assert(wide->repeats(Wide::vs_constant_buffers1,Wide::deferred)==1);
        assert(!wide->observe_vs_constant_buffers(c,0,1,&b0,0));                 // base after CB1: forwarded
        assert(wide->observe_vs_constant_buffers(c,0,1,&b0,0));
        // ObjectMotion's own pool substitution bypasses the hooked setter: the exact pointer is recorded, so the next game bind
        // of the previous buffer is a change, not a repeat, and is never elided.
        wide->note_direct_vs_constant_buffers(c,2,1,&pool);
        assert(!wide->observe_vs_constant_buffers(c,2,1,&b0,0));
        assert(wide->observe_vs_constant_buffers(c,2,1,&b0,0));
        // Pass boundaries and phase changes forget the shadow; an output bind is the same barrier (the owner calls barrier()).
        wide->barrier(c);
        assert(!wide->observe_ia_primitive_topology(c,D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,0));
        assert(wide->observe_ia_primitive_topology(c,D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,0));
        assert(!wide->observe_ia_primitive_topology(c,D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,1));
        wide->barrier_all();
        assert(!wide->observe_ia_primitive_topology(c,D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,1));
        // Immediate kind: blocked while its own readiness is not proven, while the deferred kind still elides.
        auto* immediate=ptr<ID3D11DeviceContext>(0x41100);
        wide->set_immediate_context(immediate);
        set_wide_ready(false,true);
        assert(!wide->observe_ia_primitive_topology(immediate,D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,1));
        assert(!wide->observe_ia_primitive_topology(immediate,D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,1));
        assert(wide->skipped(Wide::ia_primitive_topology,Wide::immediate)==0);
        assert(wide->observe_ia_primitive_topology(c,D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,1));
        const auto json=wide->json();
        assert(json.find("\"elision\":true")!=std::string::npos);
        assert(json.find("\"ia_primitive_topology\"")==std::string::npos); // method_name uses the real D3D names
        assert(json.find("\"IASetPrimitiveTopology\":{\"calls\":")!=std::string::npos);
        assert(json.find("\"skipped\":1")!=std::string::npos);
        assert(json.find("\"barrier_ready\":{\"immediate\":false,\"deferred\":true}")!=std::string::npos);
        assert(json.find("\"barriers\":")!=std::string::npos);
    }
    { // v998 ObjectMotion observer semantics: its bookkeeping runs before the filter and sees every call; a filter that elides
      // only suppresses the forward, never the bookkeeping.
        unsigned observed=0;bool forwarded=false;
        const bool skip=observe_then_filter([&] {++observed;},[&] {return true;});
        assert(observed==1 && skip && !forwarded);
        const bool forward=observe_then_filter([&] {++observed;},[&] {return false;});
        assert(observed==2 && !forward && !forwarded);
    }
    { // v997 wide counter shards: many recording threads, one cache-line-aligned shard each; the read sums them.
        auto wide=std::make_unique<Wide>();wide->set_enabled(true);
        auto* context=ptr<ID3D11DeviceContext>(0x30000);
        std::array<std::thread,4> threads;
        for(auto& thread:threads) thread=std::thread([&wide,context] {
            for(int i=0;i<250;++i) wide->observe_ia_primitive_topology(context,D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,1);
        });
        for(auto& thread:threads) thread.join();
        assert(wide->calls(Wide::ia_primitive_topology,Wide::deferred)==1000);
        assert(wide->repeats(Wide::ia_primitive_topology,Wide::deferred)==999);
    }
    std::cout<<"PASS: PS state census shadow, opt-in elision, per-kind counters, four output hazards, CB1 invalidation, state-swap caller accounting, live toggle, sharded counters, combined-state A/B frame histograms, wide-census repeats, wide elision method/negative rules and ObjectMotion observe-then-filter ordering\n";
    return 0;
}
