#pragma once
#include <windows.h>
#include <d3d11.h>
#include <MinHook.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <vector>
#include <intrin.h>
#include "taa_dispatch.hpp"
#include "alternate_native_taa.hpp"
#include "native_temporal_census.hpp"

// Per-eye temporal-AA history for native two-view stereo.
//
// Static evidence (Steam build, SHA-256 b671f824...6180; docs/image-quality-research.md):
//  - 0x327270 is the ARK_RENDERPASS_POSTPROC job callback (registered by 0x34fe00),
//    `u64 __fastcall pass(JobContext*)`. It runs on a job worker and records into that
//    job's deferred context (*(ctx+0x108)).
//  - ctx+0x70 is the view. TAA runs only if !(view+0x3a34 & 1) && !(view+0x3a37 & 4).
//  - ctx+0x80 points to 16-byte {u32 hash; u32 pad; u64 value} entries; the count is the
//    low 24 bits of ctx+0x88. Insertion: 0x2c2230. The pass uses the FIRST matching hash.
//  - Hash = FNV-1a over the name INCLUDING its NUL. 0x3337e277 is "temporalAAHistory0" and
//    0x0d35680e is "temporalAAHistory1"; these two constants occur nowhere else in .text.
//  - Each value is an image handle, image+0x98 (0x2bfa0); handle[0] is the image (0x2bd230, 0x2f4060).
//    Image (0x148 bytes): desc u32[19] at +0x34, clear u32[5] at +0x80, ID3D11Resource* at +0x130.
//  - Read/write roles swap on *(0x32fc580+0xcc) & 1 (0x35eb00, read at 0x327542).
//    Who increments it (per Present or per view) is NOT located; the alternation below works either way.
//  - *(0x32fc580+8) is a reset countdown. Each pass decrements it twice and clears the history
//    RTV while it is positive (0x35ed60, 0x2bb0e0).
//  - 0x2ecb20 creates or reuses a NAMED image: (manager=*(0x22aba40), name, desc, clear, 1).
//    It takes the image-manager lock (0x2eb750), and re-initialising the same name releases the old resource.
// Unverified live: handle back-pointers, desc copy fidelity, allocator thread safety on the
// render thread, and that both eyes share one history pair (draw trace only).
namespace d2vr {
namespace taa_history {
inline constexpr std::uintptr_t pass_rva=0x327270,allocate_rva=0x2ecb20,image_manager_rva=0x22aba40;
inline constexpr std::uintptr_t render_system_rva=0x32fc580,frame_counter_rva=render_system_rva+0xcc,reset_countdown_rva=render_system_rva+0x8;
inline constexpr std::uintptr_t ctx_view=0x70,ctx_params=0x80,ctx_param_count=0x88,view_flags_a=0x3a34,view_flags_b=0x3a37;
inline constexpr std::uintptr_t image_desc=0x34,image_clear=0x80,image_handle=0x98,image_resource=0x130;
inline constexpr std::size_t desc_words=19,clear_words=5;
inline constexpr std::uint32_t max_params=4096;
inline constexpr LONG reset_passes=4; // two decrements per pass, two eyes: both histories are cleared once

constexpr std::uint32_t name_hash(const char* s) noexcept {
    std::uint32_t h=0x811c9dc5u;
    for(;;++s) {h=(h^static_cast<unsigned char>(*s))*0x1000193u;if(!*s) return h;}
}
inline constexpr std::array<std::uint32_t,2> history_hashes{name_hash("temporalAAHistory0"),name_hash("temporalAAHistory1")};
static_assert(history_hashes[0]==0x3337e277u && history_hashes[1]==0x0d35680eu,"History parameter hashes differ");

struct ParamEntry {std::uint32_t hash,pad;std::uint64_t value;};
static_assert(sizeof(ParamEntry)==16,"Parameter entries are 16 bytes");
struct Slots {std::array<int,2> index{-1,-1};bool complete() const noexcept {return index[0]>=0 && index[1]>=0;}};
inline Slots find_history(const ParamEntry* entries,std::uint32_t count) noexcept {
    Slots slots;
    for(std::uint32_t i=0;i<count;++i) for(int k=0;k<2;++k)
        if(slots.index[k]<0 && entries[i].hash==history_hashes[k]) slots.index[k]=static_cast<int>(i);
    return slots;
}
inline std::uint32_t param_count(std::uint32_t raw) noexcept {return raw&0xffffffu;}
inline bool temporal_runs(std::uint8_t flags_a,std::uint8_t flags_b) noexcept {return !(flags_a&1) && !(flags_b&4);}
inline int eye_of(std::uintptr_t view,std::uintptr_t left,std::uintptr_t right) noexcept {
    if(!view || left==right) return -1;
    return view==left?0:view==right?1:-1;
}
inline bool handle_of(std::uint64_t handle,std::uint64_t back) noexcept {return handle>image_handle && back==handle-image_handle;}

struct ImageSnapshot {
    std::uintptr_t image{},resource{};std::array<std::uint32_t,desc_words> desc{};std::array<std::uint32_t,clear_words> clear{};
    bool same_layout(const ImageSnapshot& o) const noexcept {return desc==o.desc && clear==o.clear;}
    bool operator==(const ImageSnapshot& o) const noexcept {return image==o.image && resource==o.resource && same_layout(o);}
    bool operator!=(const ImageSnapshot& o) const noexcept {return !(*this==o);}
};
template<class Read> bool snapshot(Read read,std::uintptr_t image,ImageSnapshot& out) {
    std::uint64_t back{};
    out.image=image;
    return image && read(image+image_handle,&back,sizeof back) && back==image &&
        read(image+image_desc,out.desc.data(),sizeof out.desc) && read(image+image_clear,out.clear.data(),sizeof out.clear) &&
        read(image+image_resource,&out.resource,sizeof out.resource) && out.resource;
}
inline bool same_texture(const D3D11_TEXTURE2D_DESC& a,const D3D11_TEXTURE2D_DESC& b) noexcept {
    return a.Width==b.Width && a.Height==b.Height && a.MipLevels==b.MipLevels && a.ArraySize==b.ArraySize && a.Format==b.Format &&
        a.SampleDesc.Count==b.SampleDesc.Count && a.SampleDesc.Quality==b.SampleDesc.Quality && a.Usage==b.Usage &&
        a.BindFlags==b.BindFlags && a.CPUAccessFlags==b.CPUAccessFlags && a.MiscFlags==b.MiscFlags;
}

// Each eye alternates its own pair by its own pass count, whatever the engine counter does.
// When the engine role parity equals the eye's own parity, nothing is swapped (flip=false).
class Alternation {
    std::atomic<int> role_{-1};
public:
    bool flip(std::uint32_t counter) noexcept {
        int role=role_.load();
        if(role<0) {role=static_cast<int>(counter&1);role_.store(role);}
        return ((static_cast<std::uint32_t>(role)^counter)&1)!=0;
    }
    void advance() noexcept {const int role=role_.load();if(role>=0) role_.store(role^1);}
    void reset() noexcept {role_.store(-1);}
};
inline std::array<std::uint64_t,2> assignment(const std::array<std::uint64_t,2>& pair,bool flip) noexcept {
    return flip?std::array<std::uint64_t,2>{pair[1],pair[0]}:pair;
}

// Optional DLSS consumer diagnostics; failure never changes native pass planning.
// Call only after the exact Setup/jitter ABI has been independently verified.
template<class Read> std::optional<temporal_eye::NativePass> read_pass_metadata(Read read,std::uintptr_t view) {
    temporal_eye::NativePass result;result.view=view;std::uint32_t after{};
    if(!view || !read(view+alternate_taa::stamp_offset,&result.counter,4) ||
       !read(view+alternate_taa::jitter_offset,result.jitter_uv.data(),8) ||
       !read(view+alternate_taa::previous_jitter_offset,result.jitter_uv.data()+2,8) ||
       !read(view+alternate_taa::current_matrix_offsets[0],result.projection.data(),sizeof result.projection) ||
       !read(view+alternate_taa::stamp_offset,&after,4) || result.counter!=after) return {};
    for(float value:result.jitter_uv) if(!std::isfinite(value)) return {};
    for(float value:result.projection) if(!std::isfinite(value)) return {};
    return result;
}
struct PassInputs {
    std::uintptr_t base{};std::array<std::uintptr_t,2> eye_views{};
    bool twins_ready{};std::array<ImageSnapshot,2> originals{},twins{};
    std::uintptr_t single_view{};std::optional<alternate_taa::Receipt> receipt;
    bool capture_metadata{};
};
enum class PassResult {untracked,idle,fallback,rebuild,planned};
struct PassPlan {
    int eye=-1;std::uint32_t counter{};bool write{};std::optional<std::array<float,4>> jitter;
    std::optional<temporal_eye::NativePass> metadata;
    std::array<std::uintptr_t,2> slots{};std::array<std::uint64_t,2> engine{},replaced{};
};
// Pure planning against engine memory read through `read`. It never writes.
template<class Read> PassResult plan_pass(Read read,const PassInputs& in,std::uintptr_t ctx,std::array<Alternation,2>& alternation,
                                         PassPlan& plan,std::vector<ParamEntry>& scratch) {
    plan.metadata.reset();
    std::uintptr_t view{};
    if(!read(ctx+ctx_view,&view,sizeof view)) return PassResult::untracked;
    int eye=eye_of(view,in.eye_views[0],in.eye_views[1]);
    if(in.single_view && view==in.single_view) {
        std::uint32_t stamp{};
        if(!in.receipt || !read(view+alternate_taa::stamp_offset,&stamp,4) ||
            in.receipt->key.view!=view || in.receipt->key.native_counter!=stamp || !in.receipt->admitted) return PassResult::fallback;
        eye=in.receipt->key.eye;
        plan.jitter=std::array<float,4>{in.receipt->current.jitter[0],in.receipt->current.jitter[1],
            in.receipt->previous.jitter[0],in.receipt->previous.jitter[1]};
    }
    if(eye<0 || eye>1) return PassResult::untracked;
    if(in.capture_metadata) plan.metadata=read_pass_metadata(read,view);
    std::uint8_t flags_a{},flags_b{};
    if(!read(view+view_flags_a,&flags_a,1) || !read(view+view_flags_b,&flags_b,1)) return PassResult::fallback;
    if(!temporal_runs(flags_a,flags_b)) return PassResult::idle;
    std::uintptr_t table{};std::uint32_t raw{};
    if(!read(ctx+ctx_params,&table,sizeof table) || !read(ctx+ctx_param_count,&raw,sizeof raw)) return PassResult::fallback;
    const auto count=param_count(raw);
    if(!table || !count || count>max_params) return PassResult::fallback;
    scratch.resize(count);
    if(!read(table,scratch.data(),count*sizeof(ParamEntry))) return PassResult::fallback;
    const auto slots=find_history(scratch.data(),count);
    if(!slots.complete()) return PassResult::fallback;
    std::array<std::uint64_t,2> engine{};
    for(int k=0;k<2;++k) {
        engine[k]=scratch[static_cast<std::size_t>(slots.index[k])].value;
        std::uint64_t back{};
        if(!read(static_cast<std::uintptr_t>(engine[k]),&back,sizeof back) || !handle_of(engine[k],back)) return PassResult::fallback;
    }
    std::uint32_t counter{};
    if(!read(in.base+frame_counter_rva,&counter,sizeof counter)) return PassResult::fallback;
    auto pair=engine;
    if(eye==1) {
        if(!in.twins_ready) return PassResult::rebuild;
        for(int k=0;k<2;++k) {
            ImageSnapshot current{};
            if(!snapshot(read,static_cast<std::uintptr_t>(engine[k]-image_handle),current) || current!=in.originals[k]) return PassResult::rebuild;
            pair[k]=in.twins[k].image+image_handle;
        }
    }
    plan.eye=eye;plan.counter=counter;plan.engine=engine;
    plan.replaced=assignment(pair,alternation[static_cast<std::size_t>(eye)].flip(counter));
    for(int k=0;k<2;++k) plan.slots[k]=table+static_cast<std::uintptr_t>(slots.index[k])*sizeof(ParamEntry)+offsetof(ParamEntry,value);
    plan.write=plan.replaced!=plan.engine;
    return PassResult::planned;
}
inline void apply_plan(const PassPlan& plan) noexcept {
    if(plan.write) for(int k=0;k<2;++k) *reinterpret_cast<volatile std::uint64_t*>(plan.slots[k])=plan.replaced[k];
}
// Restore only values that are still ours.
inline void restore_plan(const PassPlan& plan) noexcept {
    if(plan.write) for(int k=0;k<2;++k)
        InterlockedCompareExchange64(reinterpret_cast<volatile LONG64*>(plan.slots[k]),static_cast<LONG64>(plan.engine[k]),static_cast<LONG64>(plan.replaced[k]));
}
// Only an unqualified alternate main view uses this scoped native AA gate. All original post-process work still runs once.
using NativePass=unsigned long long (__fastcall*)(void*);
inline unsigned long long without_temporal(NativePass original,void* ctx,std::uintptr_t view) {
    const auto flags=reinterpret_cast<volatile char*>(view+view_flags_b);
    const std::uint32_t stamp=*reinterpret_cast<volatile std::uint32_t*>(view+alternate_taa::stamp_offset);
    char before=*flags;bool ours=false;
    while(!(before&4)) {
        const char seen=_InterlockedCompareExchange8(flags,static_cast<char>(before|4),before);
        if(seen==before) {ours=true;break;}
        before=seen;
    }
    unsigned long long result{};
    __try {result=original(ctx);}
    __finally {
        if(ours && *reinterpret_cast<volatile std::uint32_t*>(view+alternate_taa::stamp_offset)==stamp) {
            char current=*flags;
            while(current&4) {
                const char seen=_InterlockedCompareExchange8(flags,static_cast<char>(current&~4),current);
                if(seen==current) break;
                current=seen;
            }
        }
    }
    return result;
}
} // namespace taa_history

class NativeTaaHistory {
    using Read=bool (*)(std::uintptr_t,void*,std::size_t);
    using Pass=unsigned long long (__fastcall*)(void*);
    using Allocate=void* (__fastcall*)(void*,const char*,const std::uint32_t*,const std::uint32_t*,unsigned char);
    struct Site {std::uintptr_t rva;std::size_t size;std::array<unsigned char,32> code;};
    inline static const std::array<Site,11> sites{{
        {taa_history::pass_rva,32,{0x48,0x8b,0xc4,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8d,0xa8,0x08,0xfb,0xff,0xff,0x48,0x81,0xec,0xb8,0x05,0x00,0x00,0x48,0xc7,0x45}},
        {taa_history::allocate_rva,32,{0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48,0x89,0x7c,0x24,0x20,0x41,0x56,0x48,0x83,0xec,0x20,0x49,0x8b,0xf1,0x49,0x8b,0xf8,0x48,0x8b,0xea,0x4c,0x8b}},
        {0x3272ad,4,{0x4d,0x8b,0x7e,0x70}},                                                       // mov r15,[ctx+0x70]
        {0x3272b8,7,{0x41,0x8b,0x96,0x88,0x00,0x00,0x00}},                                         // mov edx,[ctx+0x88]
        {0x327369,13,{0x49,0x8b,0x86,0x80,0x00,0x00,0x00,0x81,0x38,0x77,0xe2,0x37,0x33}},          // history0 lookup
        {0x32739f,13,{0x49,0x8b,0x86,0x80,0x00,0x00,0x00,0x81,0x38,0x0e,0x68,0x35,0x0d}},          // history1 lookup
        {0x327531,4,{0xf6,0x43,0x67,0x04}},                                                        // test [view+0x3a37],4
        {0x32753b,15,{0x48,0x8d,0x0d,0x3e,0x50,0xfd,0x02,0xe8,0xb9,0x75,0x03,0x00,0x83,0xe0,0x01}},// counter & 1
        {0x35eb00,7,{0x8b,0x81,0xcc,0x00,0x00,0x00,0xc3}},                                         // counter getter
        {0x35ed60,9,{0x8b,0x51,0x08,0x8d,0x42,0xff,0x89,0x41,0x08}},                               // reset countdown
        {0x2bfa0,8,{0x48,0x8d,0x81,0x98,0x00,0x00,0x00,0xc3}},                                     // handle = image+0x98
    }};
    inline static NativeTaaHistory* instance_{};
    Pass original_{};Allocate allocate_{};Read read_{};std::uintptr_t base_{};bool enabled_{};alternate_taa::Runtime* alternate_{};
    std::atomic<bool> active_{},ready_{},rebuild_{},metadata_enabled_{};std::atomic<unsigned> depth_{},in_flight_{};
    std::array<std::atomic<std::uintptr_t>,2> eye_views_{},observed_{};
    std::array<taa_history::Alternation,2> alternation_{};
    // Changed only by maintain() while ready_==false and in_flight_==0.
    std::array<taa_history::ImageSnapshot,2> originals_{},twins_{};
    std::mutex maintain_mutex_;
    std::atomic<UINT64> passes_{},right_swaps_{},left_flips_{},fallbacks_{},rebuild_waits_{},idle_{},rebuilds_{},failures_{};

    bool verify_sites() const {
        for(const auto& site:sites) {
            std::array<unsigned char,32> code{};
            if(!read_(base_+site.rva,code.data(),site.size) || std::memcmp(code.data(),site.code.data(),site.size)) return false;
        }
        return true;
    }
    bool plan(void* ctx,taa_history::PassPlan& plan) noexcept {
        try {
            thread_local std::vector<taa_history::ParamEntry> scratch;
            // Snapshots are copied only after ready_ is observed (maintain() writes them before setting it).
            taa_history::PassInputs in{base_,{eye_views_[0].load(),eye_views_[1].load()}};
            in.capture_metadata=temporal_eye::diagnostic_capture_enabled.load(std::memory_order_relaxed) && metadata_enabled_.load();
            if(alternate_) {
                in.single_view=alternate_->single_view.load();std::uintptr_t view{};
                if(in.single_view && read_(reinterpret_cast<std::uintptr_t>(ctx)+taa_history::ctx_view,&view,8) && view==in.single_view)
                    in.receipt=alternate_->resolve(view);
            }
            if(ready_.load()) {in.twins_ready=true;in.originals=originals_;in.twins=twins_;}
            const auto result=taa_history::plan_pass(read_,in,reinterpret_cast<std::uintptr_t>(ctx),alternation_,plan,scratch);
            if(result==taa_history::PassResult::idle) ++idle_;
            if(result==taa_history::PassResult::fallback) ++fallbacks_;
            if(result==taa_history::PassResult::rebuild) {rebuild_=true;alternation_[1].reset();++rebuild_waits_;}
            if(result==taa_history::PassResult::planned) {
                // Publish the shared images seen this frame for maintain().
                for(int k=0;k<2;++k) observed_[k]=static_cast<std::uintptr_t>(plan.engine[k]-taa_history::image_handle);
                return true;
            }
        } catch(...) {++failures_;}
        return false;
    }
public:
    // Diagnostic observer, called on the pass's job thread after a planned eye pass is recorded.
    void (*after_pass)(void* ctx,int eye)=nullptr;
    using DiagnosticJob=void (*)(bool,void*,temporal_census::Token&) noexcept;
    inline static std::atomic<DiagnosticJob> diagnostic_job{};
    void capture_metadata(bool enabled) noexcept {metadata_enabled_=enabled;}
private:
    unsigned long long run(void* ctx,const taa_history::PassPlan& plan) {
        taa_history::apply_plan(plan);TaaDispatchHook::pass_metadata=plan.metadata;TaaDispatchHook::pass_jitter=plan.jitter;TaaDispatchHook::pass_eye=plan.eye;TaaDispatchHook::post_taa=false;TaaDispatchHook::pass_context=nullptr;
        __try {return original_(ctx);}
        __finally {
            TaaDispatchHook::pass_eye=-1;TaaDispatchHook::pass_metadata.reset();TaaDispatchHook::pass_jitter.reset();taa_history::restore_plan(plan);
            if(const auto end=TaaDispatchHook::pass_end.load()) end(plan.eye,TaaDispatchHook::pass_context);
        }
    }
    unsigned long long invoke(void* ctx) {
        ++passes_;
        taa_history::PassPlan pass{};
        if(!active_.load()) return original_(ctx);
        if(!plan(ctx,pass)) {
            std::uintptr_t view{};
            if(alternate_ && read_(reinterpret_cast<std::uintptr_t>(ctx)+taa_history::ctx_view,&view,8) &&
                view && view==alternate_->single_view.load()) return taa_history::without_temporal(original_,ctx,view);
            return original_(ctx);
        }
        const auto result=run(ctx,pass);
        if(after_pass) after_pass(ctx,pass.eye);
        alternation_[static_cast<std::size_t>(pass.eye)].advance();
        if(pass.eye==1) ++right_swaps_;else if(pass.write) ++left_flips_;
        return result;
    }
    unsigned long long observed_invoke(void* ctx,DiagnosticJob observer) {
        temporal_census::Token token{};
        observer(true,ctx,token);
        __try {return invoke(ctx);}
        __finally {observer(false,ctx,token);}
    }
    static unsigned long long __fastcall dispatch(void* ctx) {
        auto& self=*instance_;++self.depth_;++self.in_flight_;
        const auto observer=diagnostic_job.load(std::memory_order_relaxed);
        __try {return observer?self.observed_invoke(ctx,observer):self.invoke(ctx);} __finally {--self.in_flight_;--self.depth_;}
    }
    static bool same_resource(std::uintptr_t a,std::uintptr_t b) noexcept {
        auto query=[](std::uintptr_t resource,D3D11_TEXTURE2D_DESC& desc) {
            ID3D11Texture2D* texture{};
            if(FAILED(reinterpret_cast<ID3D11Resource*>(resource)->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&texture)))) return false;
            texture->GetDesc(&desc);texture->Release();return true;
        };
        D3D11_TEXTURE2D_DESC da{},db{};
        return query(a,da) && query(b,db) && taa_history::same_texture(da,db);
    }
    void request_reset() noexcept {
        auto counter=reinterpret_cast<volatile LONG*>(base_+taa_history::reset_countdown_rva);
        for(LONG value=*counter;value<taa_history::reset_passes;value=*counter)
            if(InterlockedCompareExchange(counter,taa_history::reset_passes,value)==value) break;
    }
public:
    void alternate_runtime(alternate_taa::Runtime* runtime) noexcept {alternate_=runtime;}
    void reset_histories() noexcept {alternation_[0].reset();alternation_[1].reset();if(base_) request_reset();}
    // Stable native view objects from qualified Setup calls; 0 clears an eye.
    void publish_eye_view(unsigned eye,std::uintptr_t view) noexcept {if(eye<2) eye_views_[eye]=view;}
    void clear_eye_views() noexcept {eye_views_[0]=0;eye_views_[1]=0;alternation_[0].reset();alternation_[1].reset();}
    // Render thread only, outside engine image creation (for example at the left eye's begin call).
    // Creates or recreates the right eye's engine-owned history pair. Never blocks on passes.
    bool maintain() {
        if(!active_.load()) return false;
        const std::array<std::uintptr_t,2> seen{observed_[0].load(),observed_[1].load()};
        if(!seen[0] || !seen[1]) return false;
        std::array<taa_history::ImageSnapshot,2> current{};
        for(int k=0;k<2;++k) if(!taa_history::snapshot(read_,seen[k],current[k])) return false;
        if(ready_.load() && !rebuild_.load() && current==originals_) return true;
        ready_=false;
        if(in_flight_.load()) return false;
        std::lock_guard<std::mutex> lock(maintain_mutex_);
        void* manager{};
        if(!read_(base_+taa_history::image_manager_rva,&manager,sizeof manager) || !manager) {++failures_;return false;}
        static constexpr std::array<const char*,2> names{"_d2vrTemporalAAHistoryEye1_0","_d2vrTemporalAAHistoryEye1_1"};
        std::array<taa_history::ImageSnapshot,2> twins{};
        for(int k=0;k<2;++k) {
            const auto image=reinterpret_cast<std::uintptr_t>(allocate_(manager,names[k],current[k].desc.data(),current[k].clear.data(),1));
            if(!taa_history::snapshot(read_,image,twins[k]) || !twins[k].same_layout(current[k]) || image==current[k].image ||
               !same_resource(twins[k].resource,current[k].resource)) {++failures_;return false;}
        }
        originals_=current;twins_=twins;alternation_[1].reset();request_reset();rebuild_=false;++rebuilds_;ready_=true;
        return true;
    }
    void install(std::uintptr_t base,Read read) {
        base_=base;read_=read;
        if(!verify_sites()) throw std::runtime_error("Native TAA history ABI differs");
        allocate_=reinterpret_cast<Allocate>(base+taa_history::allocate_rva);instance_=this;
        ready_=false;rebuild_=false;clear_eye_views();observed_[0]=0;observed_[1]=0;
        passes_=right_swaps_=left_flips_=fallbacks_=rebuild_waits_=idle_=rebuilds_=failures_=0;
        const auto target=reinterpret_cast<void*>(base+taa_history::pass_rva);
        if(!original_ && MH_CreateHook(target,reinterpret_cast<void*>(&dispatch),reinterpret_cast<void**>(&original_))!=MH_OK)
            throw std::runtime_error("Cannot create native TAA history hook");
        active_=true;
        if(MH_EnableHook(target)!=MH_OK) {active_=false;throw std::runtime_error("Cannot enable native TAA history hook");}
        enabled_=true;
    }
    // Twins stay in the engine image manager (named, reused by a later install).
    bool disable() noexcept {
        active_=false;ready_=false;clear_eye_views();
        if(enabled_) {if(MH_DisableHook(reinterpret_cast<void*>(base_+taa_history::pass_rva))!=MH_OK) return false;enabled_=false;}
        const auto deadline=GetTickCount64()+5000;while(depth_ && GetTickCount64()<deadline) Sleep(1);
        if(depth_) return false;
        std::array<unsigned char,32> code{};
        return !original_ || (read_(base_+taa_history::pass_rva,code.data(),32) && code==sites[0].code);
    }
    bool ready() const noexcept {return ready_.load();}
    UINT64 passes() const {return passes_.load();}UINT64 right_swaps() const {return right_swaps_.load();}
    UINT64 left_flips() const {return left_flips_.load();}UINT64 fallbacks() const {return fallbacks_.load();}
    UINT64 rebuild_waits() const {return rebuild_waits_.load();}UINT64 idle() const {return idle_.load();}
    UINT64 rebuilds() const {return rebuilds_.load();}UINT64 failures() const {return failures_.load();}
};
} // namespace d2vr
