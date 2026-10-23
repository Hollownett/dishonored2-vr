#pragma once
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>
#include <cstdio>
#include <optional>
#include <string>
#include "fsr3_eye_upscaler.hpp"
#include "fsr4_eye_upscaler.hpp"
#include "dlss_eye_upscaler.hpp"
#include "d3d11_temporal_owner.hpp"
#include "dlss_mode.hpp"
#include "taa_dispatch.hpp"
#include "post_tonemap_pass.hpp"
#include "temporal_eye_job.hpp"
#include "gpu_pass_timer.hpp"
#include "mod_gpu_timer.hpp"

namespace d2vr {
// Replaces the native TAA dispatch of each eye with its own FSR 3.1 context (Luma's D2 map:
// t1 UV motion vectors, t2 pre-TAA colour, u1 TAA result). Recorded on the game's deferred
// context in command order, so this eye's scene depth (shared by both eyes, rendered just
// before this pass) is sampled by the existing FSR heuristic. This is not DLSS producer proof.
// The scene depth is learned from depth-stencil clears of the
// TAA colour size; it is R24G8, so a tiny CS copies it to R32F for FSR.
// Every device call (contexts, textures, views) happens in prepare() on the Present thread;
// the job thread only records commands. Output overwrites u1 at the same size (native-
// resolution AA). The compute state of the game's context is restored afterwards.
// DLSS (anti-aliasing modes 7 DLAA and 8 DLSS Quality) is an alternative backend at this same seam: the colour,
// depth copy, motion vectors, jitter, reset and exposure inputs are the ones FSR 3 uses, and the FSR 3 contexts stay
// built as the per-session fallback. NGX features are created and evaluated on the immediate context only
// (DlssEyeUpscaler), isolated from the game's pipeline state by D3D11TemporalOwner, which also retires each
// generation's features only after an EVENT query proves its GPU work complete.
class FsrTaa {
public:
    struct Config {float sharpness=.3f;float mv_x=-1,mv_y=-1;bool mv_full=false;float dlss_jitter_x=1,dlss_jitter_y=1;}; // dlss_jitter_*: multipliers on the FSR-convention jitter for DLSS only (build/dlss-jitter-sign, diagnostics)
private:
    template<class T> struct Ref {
        T* p{};~Ref() {if(p) p->Release();}
        void reset(T* v=nullptr) {if(v) v->AddRef();if(p) p->Release();p=v;}
        void adopt(T* v) {if(p) p->Release();p=v;}
    };
    struct Eye {
        Fsr3EyeUpscaler fsr;Fsr4EyeUpscaler fsr4;Ref<ID3D11Texture2D> depth,reactive,colour_in,upscaled;Ref<ID3D11UnorderedAccessView> depth_uav,reactive_uav;
        LARGE_INTEGER last{};bool reset=true;GpuPassTimer timer;
    };
    struct ClearSnapshot {
        temporal_eye::DepthClearObservation event;
        temporal_eye::Lease context,view,resource;
        D3D11_TEXTURE2D_DESC resource_desc{};
        D3D11_DEPTH_STENCIL_VIEW_DESC view_desc{};
    };
    struct Candidate {ID3D11Resource* resource{};UINT w{},h{};DXGI_FORMAT srv{};std::uint64_t clears{};std::shared_ptr<const ClearSnapshot> clear;};
    std::mutex source_mutex_;
    temporal_eye::NativePairBook source_pairs_;
    struct MotionReceipt {
        temporal_eye::Lease resource;
        ID3D11Resource* pointer{};
        D3D11_TEXTURE2D_DESC desc{};
        std::optional<temporal_eye::ViewReceipt> view;
        std::uint64_t capture_sequence{};
    };
    std::array<MotionReceipt,2> stamped_motion_{};
    std::array<std::uint64_t,2> last_taa_setup_serial_{};
    std::uint64_t source_capture_sequence_{};
    std::shared_ptr<const ClearSnapshot> selected_clear_;
    std::uint64_t depth_selection_generation_{};
    std::atomic<std::uint64_t> clear_sequence_{};
    template<class T> static temporal_eye::Lease retain_native(T* pointer) {
        if(!pointer) return {};
        pointer->AddRef();
        return temporal_eye::Lease(pointer,[](const void* value) {const_cast<T*>(static_cast<const T*>(value))->Release();});
    }
    std::mutex mutex_; // guards eyes_, depth views and config between prepare() and the job threads
    std::unique_ptr<std::array<Eye,2>> eyes_{std::make_unique<std::array<Eye,2>>()};
    Ref<ID3D11ComputeShader> copy_;Ref<ID3D11Resource> depth_resource_;Ref<ID3D11ShaderResourceView> depth_view_;
    UINT ready_w_{},ready_h_{},render_w_{},render_h_{},context_w_{},context_h_{};float ready_fraction_{1}; // context_*: max render size the FSR contexts/inputs were built for
    DXGI_FORMAT ready_native_format_{DXGI_FORMAT_UNKNOWN};
    std::array<Ref<ID3D11Resource>,2> motion_{}; // upscale mode: this frame's motion vectors per eye (from the TAA dispatch)
    static thread_local Ref<ID3D11Resource> pass_source_,pass_target_; // upscale draw seen in this post-tonemap pass; defined after the class: clang-cl needs Ref's member initializers complete
    std::array<Candidate,4> candidates_{};std::mutex candidate_mutex_;
    std::atomic<UINT> wanted_w_{},wanted_h_{};
    std::atomic<DXGI_FORMAT> wanted_native_format_{DXGI_FORMAT_UNKNOWN};
    std::atomic<std::uint64_t> reset_epoch_{};
    Config config_;
    // DLSS backend state, guarded by mutex_. One feature pair (both eyes) per render/output size and format. Every
    // function below is a noexcept boundary: an escaping exception becomes the FSR 3 fallback (dlss_guarded), never
    // std::terminate inside the game's command stream.
    DlssFallback dlss_fallback_;
    DlssRefusalStreak dlss_refusals_;
    DlssRetirementLedger dlss_ledger_;
    DlssEyeUpscaler dlss_;                 // production owner: official SDK calls and signed-provider validation
    DlssEyeUpscaler::Lease dlss_lease_;    // the active pair; empty while off, pending or failed
    std::vector<std::pair<std::uint64_t,DlssEyeUpscaler::Lease>> dlss_retired_leases_; // retired pairs until the ledger releases them
    DlssEyeUpscaler::Desc dlss_desc_{};bool dlss_desc_valid_{};
    DlssEyeUpscaler::OptimalSettings dlss_optimal_{};UINT dlss_optimal_for_w_{},dlss_optimal_for_h_{};
    std::uint64_t dlss_device_generation_{},dlss_dimension_generation_{},dlss_feature_generation_{};
    std::shared_ptr<int> dlss_scratch_;    // this generation's scratch owner (the SDK owner's lease contract)
    std::array<std::uint64_t,2> dlss_frame_{};
    std::array<std::array<float,2>,2> dlss_previous_jitter_{};
    ID3D11Device* dlss_device_{};ID3D11DeviceContext* dlss_immediate_{}; // identities only; the owner below holds the references
    std::optional<D3D11TemporalOwner> dlss_owner_;D3D11TemporalOwner::DrainOwner dlss_drain_;
    bool dlss_facade_dirty_{},dlss_core_live_{}; // facade still owns a pair retired off-thread; an NGX Core is initialized
    struct DlssUse {std::uint64_t generation{};};   // EVENT ticket payload: which pair's GPU work it proves complete
    static std::optional<DlssEyeUpscaler::Format> dlss_format(DXGI_FORMAT f) noexcept {
        switch(f) {
        case DXGI_FORMAT_R16G16B16A16_FLOAT:return DlssEyeUpscaler::Format::rgba16_float;
        case DXGI_FORMAT_R8G8B8A8_UNORM:return DlssEyeUpscaler::Format::rgba8_unorm;
        case DXGI_FORMAT_R11G11B10_FLOAT:return DlssEyeUpscaler::Format::r11g11b10_float;
        default:return std::nullopt;
        }
    }
    void dlss_publish_state_locked() noexcept {
        dlss_state=static_cast<unsigned>(dlss_fallback_.state());dlss_last_reason=dlss_fallback_.last_reason();dlss_last_result=dlss_fallback_.last_result();
    }
    // NGX lifecycle calls (create/release/optimal settings) record on the immediate context: isolate the game's
    // pipeline state around them whenever the owner exists; without an owner nothing NGX runs anyway.
    template<class Work> void dlss_isolated_locked(Work&& work) noexcept {
        if(dlss_owner_ && dlss_owner_->isolate([&](ID3D11DeviceContext1*) {work();return true;})==D3D11TemporalOwner::Status::ok) return;
        work();
    }
    // Releases every retired pair the ledger marks releasable (its last EVENT completed, or it was abandoned).
    void dlss_release_locked() noexcept {
        dlss_guarded([&] {
            for(const auto generation:dlss_ledger_.take_releasable()) {
                for(auto r=dlss_retired_leases_.begin();r!=dlss_retired_leases_.end();) {
                    if(r->first==generation) {dlss_isolated_locked([&] {DlssEyeUpscaler::confirm_gpu_retirement(r->second);});r=dlss_retired_leases_.erase(r);}
                    else ++r;
                }
            }
            dlss_isolated_locked([] {DlssEyeUpscaler::collect_retired();});
        },[] {});
    }
    void dlss_fail_locked(DlssEyeUpscaler::Reason reason,NVSDK_NGX_Result result=NVSDK_NGX_Result_Success) noexcept {
        if(dlss_fallback_.wants_feature()) dlss_fallback_.note_prepare(false,static_cast<unsigned>(reason),static_cast<int>(result));
        else if(dlss_fallback_.should_evaluate()) dlss_fallback_.note_evaluate(false,static_cast<unsigned>(reason),static_cast<int>(result));
        else dlss_fallback_.note_device_lost(static_cast<unsigned>(reason),static_cast<int>(result));
        dlss_publish_state_locked();
        dlss_guarded([&] {dlss_retire_locked();},[&] {dlss_publish_state_locked();});
    }
    // Completed EVENT tickets release their pair's GPU-busy count through the ledger.
    void dlss_poll_locked() noexcept {
        if(!dlss_owner_) return;
        if(dlss_drain_.poll()!=D3D11TemporalOwner::Status::ok) return;
        while(auto done=dlss_drain_.take_completed()) {
            const auto* use=static_cast<const DlssUse*>(done->payload.get());
            dlss_ledger_.completed(use?use->generation:0);
        }
        dlss_release_locked();
    }
    void dlss_retire_locked() noexcept {
        dlss_guarded([&] {
            if(dlss_lease_) {dlss_retired_leases_.push_back({dlss_lease_.generation(),dlss_lease_});dlss_lease_={};}
            dlss_ledger_.retire();
        },[&] {dlss_lease_={};dlss_ledger_.abandon_outstanding();dlss_ledger_.retire();}); // no room to track it: abandon rather than strand
        dlss_isolated_locked([&] {dlss_.retire();}); // the NGX Core stays initialized for the next pair
        dlss_desc_valid_=false;dlss_scratch_.reset();
        dlss_release_locked();
    }
    // Device/context change or session end: outstanding EVENTs are abandoned (the GPU work is dead or long submitted)
    // so every retired pair is released now and none strands the SDK owner's registry.
    void dlss_abandon_locked() noexcept {dlss_ledger_.abandon_outstanding();dlss_release_locked();}
    // Executing thread (inside the ExecuteCommandList hook, i.e. whichever game thread owns the immediate context right
    // now; the game uses more than one, one at a time), before this eye's dispatch: keep the NGX feature pair matching
    // the current render/output size, formats and conventions, or retire it when DLSS is deselected or has failed this
    // session. Every NGX and context call (create, evaluate, state swap, EVENT End/GetData) happens here under mutex_,
    // after the caller saved the game's compute state and inside the owner's pipeline-state isolation; the Present
    // thread only publishes the mode. The state/query owner is therefore used without thread affinity.
    void dlss_maintain_locked(ID3D11DeviceContext* immediate) noexcept {
        dlss_guarded([&] {dlss_maintain_unguarded(immediate);},[&] {dlss_fail_locked(DlssEyeUpscaler::Reason::exception);});
    }
    void dlss_maintain_unguarded(ID3D11DeviceContext* immediate) {
        if(dlss_facade_dirty_) {dlss_facade_dirty_=false;dlss_isolated_locked([&] {dlss_.retire();});dlss_release_locked();} // work deferred from an off-thread session end
        dlss_poll_locked();
        const bool wanted=dlss_active.load() && !dlss_provider_directory.empty() && !dlss_cache_directory.empty() && immediate && immediate->GetType()==D3D11_DEVICE_CONTEXT_IMMEDIATE;
        if(dlss_fallback_.select(wanted) && !wanted) dlss_retire_locked();
        dlss_publish_state_locked();
        if(!dlss_fallback_.engaged()) {
            if(!wanted && dlss_lease_) dlss_retire_locked();
            if(!wanted && dlss_core_live_ && !dlss_lease_ && dlss_retired_leases_.empty()) {dlss_isolated_locked([&] {dlss_.disable();});dlss_core_live_=false;} // nothing left: shut the NGX Core down here, on the owning thread
            return;
        }
        if(!ready.load()) return;
        Ref<ID3D11Device> device_ref;immediate->GetDevice(&device_ref.p);
        ID3D11Device* const device=device_ref.p;
        if(!device) {dlss_fail_locked(DlssEyeUpscaler::Reason::wrong_device);return;}
        if(dlss_device_!=device || dlss_immediate_!=immediate || !dlss_owner_) {
            // One state/query owner per immediate context, kept across sessions (the registry refuses a second one). A new
            // device/context is a new NGX device generation; the old one's GPU work is dead, so its tickets are abandoned.
            if(dlss_device_!=device || dlss_immediate_!=immediate) {
                if(dlss_owner_) ++dlss_owner_rebuilds;
                dlss_retire_locked();dlss_abandon_locked();dlss_owner_.reset();dlss_drain_={};++dlss_device_generation_;
            }
            D3D11TemporalOwner::Result result;
            dlss_owner_=D3D11TemporalOwner::prepare(device,immediate,result,false);dlss_owner_status=static_cast<unsigned>(result.status);
            if(!dlss_owner_) {dlss_fail_locked(DlssEyeUpscaler::Reason::invalid_context);return;}
            dlss_drain_=dlss_owner_->drain_owner();dlss_device_=device;dlss_immediate_=immediate;dlss_retire_locked();
        }
        const bool upscale=ready_fraction_<.999f;
        const UINT w=ready_w_,h=ready_h_,rw=upscale?render_w_:w,rh=upscale?render_h_:h;
        const auto colour_format=dlss_format(upscale?DXGI_FORMAT_R8G8B8A8_UNORM:ready_native_format_);
        if(!colour_format) {dlss_fail_locked(DlssEyeUpscaler::Reason::invalid_resource);return;}
        const DlssEyeUpscaler::Paths paths{dlss_provider_directory,dlss_cache_directory};
        const bool quality=dlss_quality.load() || rw<w || rh<h;
        if(quality) {
            // NGX decides the render size for this output: publish it as the engine fraction and create the
            // features only once the engine renders inside the accepted range (bounded by the fallback's wait budget).
            if(dlss_optimal_for_w_!=w || dlss_optimal_for_h_!=h) {
                NVSDK_NGX_Result result{};DlssEyeUpscaler::OptimalSettings optimal{};
                DlssEyeUpscaler::Reason reason{};
                dlss_isolated_locked([&] {reason=dlss_.optimal_settings(device,immediate,paths,dlss_device_generation_,w,h,DlssEyeUpscaler::Quality::quality,optimal,&result);});
                ++dlss_optimal_queries;dlss_core_live_=true;
                if(reason!=DlssEyeUpscaler::Reason::none) {dlss_fail_locked(reason,result);return;}
                const auto optimal_value=dlss_mode::optimal_fraction(w,h,optimal.optimal_width,optimal.optimal_height);
                if(!optimal_value) {dlss_fail_locked(DlssEyeUpscaler::Reason::optimal_failed);return;} // an unusable render size is a failure, not a wait
                dlss_optimal_=optimal;dlss_optimal_for_w_=w;dlss_optimal_for_h_=h;dlss_optimal_width=optimal.optimal_width;dlss_optimal_height=optimal.optimal_height;
                dlss_quality_fraction=*optimal_value;
            }
            if(!upscale || rw<dlss_optimal_.min_width || rw>dlss_optimal_.max_width || rh<dlss_optimal_.min_height || rh>dlss_optimal_.max_height) {
                ++dlss_waiting;if(dlss_lease_) dlss_retire_locked();
                if(dlss_fallback_.note_waiting()) dlss_fail_locked(DlssEyeUpscaler::Reason::pending_timeout);
                return;
            }
        }
        DlssEyeUpscaler::Desc d{};
        d.width=w;d.height=h;d.render_width=rw;d.render_height=rh;d.quality=quality?DlssEyeUpscaler::Quality::quality:DlssEyeUpscaler::Quality::dlaa;
        d.colour=d.output=*colour_format;
        // Motion vectors: the engine renders colour, depth and motion into the top-left render subrect of its full-size
        // targets (dynamic resolution), so the motion field is render-sized content in a display-sized texture; the
        // scale is the FSR one (UV times the render size) and the Quality feature reads it as low-resolution motion.
        const float mw=upscale?(config_.mv_full?static_cast<float>(w):static_cast<float>(rw)):static_cast<float>(w),mh=upscale?(config_.mv_full?static_cast<float>(h):static_cast<float>(rh)):static_cast<float>(h);
        d.motion={config_.mv_x*mw,config_.mv_y*mh,true,temporal_eye::MotionDirection::current_to_previous,temporal_eye::MotionUnits::uv,temporal_eye::MotionResolution::active_render};
        // Native: linear HDR scene colour with NGX AutoExposure. Upscale: the game's tonemapped LDR gamma image, where
        // exposure is meaningless (guide 3.1: exposure applies to HDR input), so a fixed unit exposure.
        d.colour_contract=upscale?
            temporal_eye::ColourContract{temporal_eye::ColourDomain::post_tonemap,temporal_eye::ColourEncoding::perceptual_gamma,temporal_eye::ExposureSource::fixed,false,false,1,1,{},{}}:
            temporal_eye::ColourContract{temporal_eye::ColourDomain::native_scene,temporal_eye::ColourEncoding::linear,temporal_eye::ExposureSource::automatic,true,true,1,1,{},{}};
        d.inverted_depth=true;d.mv_low_resolution=upscale;
        d.jitter={0,0,0,0,0,32,false};
        d.device_generation=dlss_device_generation_;
        if(dlss_desc_valid_ && dlss_lease_ && dlss_.ready()) {
            auto current=dlss_desc_;current.feature_generation=current.dimension_generation=current.retry_generation=0;
            auto next=d;next.feature_generation=next.dimension_generation=next.retry_generation=0;
            if(dlss_same_desc(current,next)) return; // unchanged
            dlss_fallback_.note_rebuild();
        }
        dlss_retire_locked();
        d.dimension_generation=++dlss_dimension_generation_;d.feature_generation=++dlss_feature_generation_;
        bool created=false;
        dlss_isolated_locked([&] {created=dlss_.prepare(device,immediate,d,paths);});
        if(!created) {
            const auto status=dlss_.status();++dlss_create_failures;dlss_core_live_=true;dlss_fail_locked(status.reason,status.result);return;
        }
        dlss_lease_=dlss_.lease();dlss_ledger_.activate(dlss_lease_.generation());dlss_core_live_=true;
        dlss_desc_=d;dlss_desc_valid_=true;dlss_scratch_=std::make_shared<int>(0);dlss_frame_={};dlss_previous_jitter_={};
        for(auto& e:*eyes_) e.reset=true;
        ++dlss_features_created;dlss_fallback_.note_prepare(true);dlss_publish_state_locked();
    }
    static bool dlss_same_desc(const DlssEyeUpscaler::Desc& a,const DlssEyeUpscaler::Desc& b) noexcept {
        return a.width==b.width && a.height==b.height && a.render_width==b.render_width && a.render_height==b.render_height &&
            a.quality==b.quality && a.colour==b.colour && a.output==b.output && a.device_generation==b.device_generation &&
            a.motion==b.motion && a.colour_contract==b.colour_contract && a.inverted_depth==b.inverted_depth && a.mv_low_resolution==b.mv_low_resolution;
    }
    // Session end (stop thread, hooks drained): NO NGX or context call may happen here, the game's render thread
    // still owns the immediate context (releasing features from this thread faulted DXVK's submission thread in a real
    // run). Outstanding EVENTs are abandoned and every pair queues for release on the next executing-thread dispatch,
    // where the NGX Core follows once nothing is left; until then their VRAM stays allocated (bounded: one pair).
    void dlss_session_end_locked() noexcept {
        dlss_guarded([&] {
            if(dlss_lease_) {dlss_retired_leases_.push_back({dlss_lease_.generation(),dlss_lease_});dlss_lease_={};}
            dlss_ledger_.retire();dlss_ledger_.abandon_outstanding();
        },[&] {dlss_lease_={};dlss_ledger_.retire();dlss_ledger_.abandon_outstanding();});
        dlss_desc_valid_=false;dlss_scratch_.reset();dlss_facade_dirty_=true;
        dlss_fallback_.session_end();dlss_refusals_=DlssRefusalStreak{};dlss_optimal_for_w_=dlss_optimal_for_h_=0;dlss_publish_state_locked();
    }
    static DXGI_FORMAT depth_srv(DXGI_FORMAT f) noexcept {
        switch(f) {
        case DXGI_FORMAT_R24G8_TYPELESS:case DXGI_FORMAT_D24_UNORM_S8_UINT:return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        case DXGI_FORMAT_R32_TYPELESS:case DXGI_FORMAT_D32_FLOAT:return DXGI_FORMAT_R32_FLOAT;
        case DXGI_FORMAT_R32G8X24_TYPELESS:case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
        default:return DXGI_FORMAT_UNKNOWN;
        }
    }
    struct CsState {
        ID3D11ComputeShader* shader{};ID3D11Buffer* cb[8]{};ID3D11ShaderResourceView* srv[16]{};ID3D11UnorderedAccessView* uav[8]{};ID3D11SamplerState* smp[8]{};
        void save(ID3D11DeviceContext* c) {c->CSGetShader(&shader,nullptr,nullptr);c->CSGetConstantBuffers(0,8,cb);c->CSGetShaderResources(0,16,srv);c->CSGetUnorderedAccessViews(0,8,uav);c->CSGetSamplers(0,8,smp);}
        void restore(ID3D11DeviceContext* c) {
            ID3D11ShaderResourceView* null_srv[16]{};ID3D11UnorderedAccessView* null_uav[8]{};
            c->CSSetShaderResources(0,16,null_srv);c->CSSetUnorderedAccessViews(0,8,null_uav,nullptr);
            c->CSSetShader(shader,nullptr,0);c->CSSetConstantBuffers(0,8,cb);c->CSSetShaderResources(0,16,srv);c->CSSetUnorderedAccessViews(0,8,uav,nullptr);c->CSSetSamplers(0,8,smp);
            if(shader) shader->Release();
            for(auto* p:cb) if(p) p->Release();
            for(auto* p:srv) if(p) p->Release();
            for(auto* p:uav) if(p) p->Release();
            for(auto* p:smp) if(p) p->Release();
        }
    };
public:
    // Source diagnostics are NOT an opt-in or a producer certificate. The three
    // unresolved native boundaries keep production takeover before any split.
    inline static constexpr unsigned dlss_unresolved_native_boundaries=7; // depth, projection consumption, callback contract
    std::atomic<std::uint64_t> dlss_setup_receipts{},dlss_setup_rejected{},dlss_pass_receipts{},dlss_pass_rejected{},dlss_mv_matches{},dlss_mv_missing{},dlss_clear_events{},dlss_clear_capture_failed{},dlss_cpu_projection_matches{},dlss_cpu_projection_mismatches{};
    std::atomic<unsigned> dlss_input_gate{static_cast<unsigned>(temporal_eye::Admission::missing_pair)};
    std::atomic<std::uint64_t> dlss_depth_selection_generation{},dlss_mv_capture_sequence{};
    void observe_dlss_setup(const temporal_eye::SetupObservation& setup) noexcept {
        if(!temporal_eye::diagnostic_capture_enabled.load(std::memory_order_relaxed)) return;
        std::lock_guard<std::mutex> lock(source_mutex_);
        if(source_pairs_.publish(setup)) {
            ++dlss_setup_receipts;
            if(setup.submitted_projection_present) {
                if(setup.submitted_projection==setup.pass.projection) ++dlss_cpu_projection_matches;else ++dlss_cpu_projection_mismatches;
            }
        } else ++dlss_setup_rejected;
    }
    void invalidate_dlss_sources(bool force_cleanup=false) noexcept {
        if(!force_cleanup && !temporal_eye::diagnostic_capture_enabled.load(std::memory_order_relaxed)) return;
        std::lock_guard<std::mutex> lock(source_mutex_);source_pairs_.invalidate();stamped_motion_={};last_taa_setup_serial_={};
        dlss_input_gate=static_cast<unsigned>(temporal_eye::Admission::missing_pair);
    }
    void observe_dlss_taa(unsigned eye,const TaaBindings& bindings,const std::optional<temporal_eye::NativePass>& metadata) noexcept {
        if(!temporal_eye::diagnostic_capture_enabled.load(std::memory_order_relaxed)) return;
        if(eye>1) return;
        try {
            std::lock_guard<std::mutex> lock(source_mutex_);
            auto& motion=stamped_motion_[eye];motion={};
            if(!metadata) {++dlss_pass_rejected;dlss_input_gate=static_cast<unsigned>(temporal_eye::Admission::stale_pass);return;}
            const auto view=source_pairs_.resolve(eye,*metadata);
            if(!view || !bindings.res[1] || !bindings.is_tex[1]) {++dlss_pass_rejected;dlss_input_gate=static_cast<unsigned>(temporal_eye::Admission::missing_pair);return;}
            if(last_taa_setup_serial_[eye]==view->serial) {++dlss_pass_rejected;dlss_input_gate=static_cast<unsigned>(temporal_eye::Admission::stale_pass);return;}
            last_taa_setup_serial_[eye]=view->serial;
            const auto capture=++source_capture_sequence_;
            motion={retain_native(bindings.res[1]),bindings.res[1],bindings.tex[1],view,capture};++dlss_pass_receipts;dlss_mv_capture_sequence=capture;
            // No native scope currently proves this consumer's selected depth producer.
            dlss_input_gate=static_cast<unsigned>(temporal_eye::Admission::missing_depth_producer);
        } catch(...) {++dlss_pass_rejected;}
    }
    bool dlss_post_motion_matches(unsigned eye,const std::optional<temporal_eye::NativePass>& metadata) noexcept {
        if(!temporal_eye::diagnostic_capture_enabled.load(std::memory_order_relaxed)) return false;
        std::lock_guard<std::mutex> lock(source_mutex_);
        if(eye>1 || !metadata) {++dlss_mv_missing;return false;}
        const auto current=source_pairs_.resolve(eye,*metadata);const auto& motion=stamped_motion_[eye];
        if(!current || !motion.resource || !motion.view || current->serial!=motion.view->serial ||
           current->pair!=motion.view->pair || current->owner_generation!=motion.view->owner_generation) {++dlss_mv_missing;return false;}
        ++dlss_mv_matches;return true;
    }
    std::atomic<std::uint64_t> replaced{},not_ready{},failures{},create_failures{};
    std::atomic<bool> enabled{false},ready{false};
    std::wstring dump_path; // diagnostics: one R32 depth dump of the left eye after dump_after replacements
    std::uint64_t dump_after=0;
    std::atomic<bool> reactive_enabled{true}; // build/no-fsr-reactive disables the near-field mask
    std::atomic<float> fraction{1}; // <1: upscale mode (the engine renders each eye at this fraction)
    // Upscale contexts are built for max(fraction, max_fraction) and reused for any smaller fraction (FSR
    // maxRenderSize semantics): a rebuild creates both eyes' FSR3+FSR4 contexts (~0.5 s frames), so the
    // governor's steps must not trigger one. Set to the governor's ceiling while it runs.
    std::atomic<float> max_fraction{0};
    std::atomic<std::uint64_t> context_builds{};
    std::atomic<std::uint64_t> taa_copies{},upscaled{},upscale_skipped{};
    std::atomic<std::uint64_t> fallback_copies{},stale_jobs{};
    std::array<std::atomic<std::uint64_t>,2> eye_dispatches{},eye_resets{};
    std::atomic<bool> timing_enabled{false};
    std::wstring fsr4_provider_path; // Optional signed DX12 provider, configured before rendering starts.
    std::atomic<bool> fsr4_active{true}; // runtime choice (VR menu FSR 4 modes); FSR 3 when false or unavailable
    std::atomic<std::uint64_t> fsr4_create_failures{},fsr4_dispatches{},fsr4_fallbacks{};
    // DLSS runtime choice (VR menu modes 7/8) and the user-supplied provider (configured before rendering starts).
    std::atomic<bool> dlss_active{false},dlss_quality{false};
    std::wstring dlss_provider_directory,dlss_cache_directory; // NGX PathListInfo directory holding nvngx_dlss.dll; empty = no provider
    std::atomic<std::uint64_t> dlss_evaluations{},dlss_evaluate_failures{},dlss_create_failures{},dlss_fallbacks{},dlss_owner_refusals{},dlss_features_created{},dlss_optimal_queries{},dlss_waiting{},dlss_owner_rebuilds{};
    std::atomic<unsigned> dlss_state{},dlss_last_reason{},dlss_owner_status{},dlss_optimal_width{},dlss_optimal_height{};
    std::atomic<int> dlss_last_result{};
    std::atomic<float> dlss_quality_fraction{dlss_mode::default_quality_fraction}; // engine render fraction for DLSS Quality (NGX optimal settings)
    bool dlss_engaged() const noexcept {const auto s=dlss_state.load(std::memory_order_relaxed);return s==static_cast<unsigned>(DlssFallback::State::pending) || s==static_cast<unsigned>(DlssFallback::State::active);}
    bool dlss_failed() const noexcept {return dlss_state.load(std::memory_order_relaxed)==static_cast<unsigned>(DlssFallback::State::failed);}
    std::array<std::atomic<unsigned>,2> eye_upscaler_generation{};
    std::array<std::atomic<std::uint64_t>,2> eye_fsr4_version{};
    std::atomic<std::uint64_t> timing_create_failures{};
    std::array<std::atomic<float>,2> eye_gpu_ms{},eye_cpu_ms{};
    std::array<std::atomic<double>,2> eye_gpu_total_ms{};
    std::array<std::atomic<std::uint64_t>,2> eye_gpu_samples{},eye_gpu_disjoint{},eye_gpu_dropped{};
    std::array<std::atomic<UINT>,2> eye_render_width{},eye_render_height{},eye_display_width{},eye_display_height{};
    std::array<std::atomic<float>,2> eye_fsr4_record_ms{},eye_fsr4_handoff_ms{};
    std::array<std::atomic<double>,2> eye_fsr4_compute_gpu_ms{},eye_fsr4_compute_gpu_total_ms{};
    std::array<std::atomic<std::uint64_t>,2> eye_fsr4_compute_gpu_samples{};
    std::atomic<bool> fsr4_compute_timing_enabled{false};
    bool (*fsr4_diagnostics_observer)(void*,int,Fsr4EyeUpscaler::Diagnostics&) noexcept{};
    void* fsr4_diagnostics_user{};
    void (*diagnostic_finished_observer)(void*,ID3D11DeviceContext*,ID3D11CommandList*) noexcept{};
    void* diagnostic_finished_user{};
    void poll_fsr4_gpu_timing() {
        std::lock_guard<std::mutex> lock(mutex_);
        for(std::size_t side=0;side<2;++side) {
            auto& fsr4=(*eyes_)[side].fsr4;fsr4.poll_gpu_timing();publish_fsr4_gpu_timing(side,fsr4);
        }
    }
    std::array<std::atomic<UINT>,2> eye_fsr4_failure{},eye_colour_width{},eye_colour_height{},eye_colour_format{},eye_colour_mips{};
    std::array<std::atomic<UINT>,2> eye_motion_width{},eye_motion_height{}; // the motion texture DLSS consumed (full-size target holding the render subrect)
    // Post-tonemap pass, job thread: the game's upscale draw binds the tonemapped image and the eye target.
    // The pass holds one draw (measured: tonemapped R8G8B8A8 source -> sRGB eye target, full viewport);
    // the last bind seen wins.
    void observe_post_tonemap_bind(ID3D11DeviceContext* c,ID3D11ShaderResourceView* const* views,UINT count) {
        if(fraction.load()>=.999f || !c || !views || !count || !views[0]) return;
        ID3D11Resource* r{};views[0]->GetResource(&r);pass_source_.reset(r);if(r) r->Release();
        ID3D11RenderTargetView* rtv{};c->OMGetRenderTargets(1,&rtv,nullptr);
        ID3D11Resource* t{};if(rtv) {rtv->GetResource(&t);rtv->Release();}
        pass_target_.reset(t);if(t) t->Release();
    }
    // Post-tonemap pass end (job thread): split after the upscale draw; FSR runs between prefix and remainder.
    void post_tonemap_end(int eye,ID3D11DeviceContext* c) {
        // This match is diagnostic only; missing optional metadata must not alter FSR.
        if(temporal_eye::diagnostic_capture_enabled.load(std::memory_order_relaxed) && eye>=0 && eye<2) (void)dlss_post_motion_matches(static_cast<unsigned>(eye),PostTonemapPass::pass_metadata);
        Ref<ID3D11Resource> source,target;source.reset(pass_source_.p);target.reset(pass_target_.p);pass_source_.reset();pass_target_.reset();
        if(fraction.load()>=.999f || !enabled.load() || eye<0 || eye>1 || !c || !source.p || !target.p) return;
        if(!ready.load()) {++upscale_skipped;return;}
        auto job=std::make_unique<Job>();job->fraction=fraction.load();job->epoch=reset_epoch_.load();
        ID3D11Texture2D* output_texture{};
        if(SUCCEEDED(target.p->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&output_texture)))) {
            D3D11_TEXTURE2D_DESC d{};output_texture->GetDesc(&d);output_texture->Release();job->width=d.Width;job->height=d.Height;
        }
        job->eye=eye;job->upscale=true;job->vfov=last_vfov[static_cast<std::size_t>(eye)].load();
        job->jitter_x=last_jitter_x[static_cast<std::size_t>(eye)].load();job->jitter_y=last_jitter_y[static_cast<std::size_t>(eye)].load();
        job->colour.reset(source.p);job->output.reset(target.p);
        {std::lock_guard<std::mutex> lock(mutex_);job->motion.reset(motion_[static_cast<std::size_t>(eye)].p);}
        if(!job->motion.p) {++upscale_skipped;return;}
        if(c->GetType()!=D3D11_DEVICE_CONTEXT_DEFERRED) {dispatch(c,*job);return;} // already on the immediate context
        std::lock_guard<std::mutex> lock(jobs_mutex_);
        decltype(recording_)::value_type* slot{};
        for(auto& r:recording_) if(r.first==c) {slot=&r;break;}
        if(!slot) for(auto& r:recording_) if(!r.first) {slot=&r;break;}
        if(!slot) {++failures;return;}
        ID3D11CommandList* prefix{};
        if(FAILED(TaaDispatchHook::finish_original(c,TRUE,&prefix)) || !prefix) {++failures;return;}
        if(diagnostic_finished_observer) diagnostic_finished_observer(diagnostic_finished_user,c,prefix);
        job->prefix.adopt(prefix);
        if(slot->first==c && slot->second) { // both eyes in one list: chain the second split
            Job* last=slot->second.get();while(last->next) last=last->next.get();
            last->next=std::move(job);
        } else {slot->first=c;slot->second=std::move(job);}
    }
    // The TAA dispatch publishes the jitter/fov FSR must use for this eye's frame.
    std::array<std::atomic<float>,2> last_jitter_x{},last_jitter_y{},last_vfov{};
    void configure(const Config& c) {std::lock_guard<std::mutex> lock(mutex_);config_=c;}
    void request_reset() {std::lock_guard<std::mutex> lock(mutex_);++reset_epoch_;for(auto& e:*eyes_) e.reset=true;}
    // Any thread, from depth-stencil clears: count clears per depth target (no device calls).
    void observe_depth(ID3D11DepthStencilView* dsv,ID3D11DeviceContext* context=nullptr,UINT flags=0,FLOAT depth=0,UINT8 stencil=0) noexcept {
        if(!dsv || !enabled.load(std::memory_order_relaxed)) return;
        ID3D11Resource* r{};dsv->GetResource(&r);if(!r) return;
        std::lock_guard<std::mutex> lock(candidate_mutex_);
        std::shared_ptr<const ClearSnapshot> observed;
        // Keep instrumentation bounded to an already discovered candidate of the
        // current TAA size; shadow/unknown clears keep their existing cheap inventory path.
        bool relevant=false;
        const auto capture_w=wanted_w_.load(),capture_h=wanted_h_.load();
        if(temporal_eye::diagnostic_capture_enabled.load(std::memory_order_relaxed) && context && capture_w && capture_h) for(const auto& candidate:candidates_)
            if(candidate.resource==r && candidate.w==capture_w && candidate.h==capture_h) relevant=true;
        if(relevant) try { temporal_eye::capture_diagnostic([&] {
            auto value=std::make_shared<ClearSnapshot>();const auto sequence=++clear_sequence_;
            value->event={{reinterpret_cast<std::uintptr_t>(context),sequence},{reinterpret_cast<std::uintptr_t>(dsv),sequence},
                {reinterpret_cast<std::uintptr_t>(r),sequence},sequence,GetCurrentThreadId(),flags,depth,stencil};
            value->context=retain_native(context);value->view=retain_native(dsv);value->resource=retain_native(r);dsv->GetDesc(&value->view_desc);
            ID3D11Texture2D* texture{};
            if(SUCCEEDED(r->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&texture))) && texture) {texture->GetDesc(&value->resource_desc);texture->Release();}
            observed=std::move(value);++dlss_clear_events;
        }); } catch(...) {++dlss_clear_capture_failed;}
        for(auto& c:candidates_) if(c.resource==r) {++c.clears;if(observed)c.clear=std::move(observed);r->Release();return;}
        ID3D11Texture2D* t{};D3D11_TEXTURE2D_DESC d{};
        if(SUCCEEDED(r->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&t)))) {t->GetDesc(&d);t->Release();}
        const auto srv=depth_srv(d.Format);
        if(srv==DXGI_FORMAT_UNKNOWN || !(d.BindFlags&D3D11_BIND_SHADER_RESOURCE) || d.SampleDesc.Count!=1) {r->Release();return;}
        auto* slot=&candidates_[0];
        for(auto& c:candidates_) if(!c.resource || c.clears<slot->clears) slot=&c;
        if(slot->resource) slot->resource->Release();
        *slot={r,d.Width,d.Height,srv,1,std::move(observed)}; // keeps the reference
    }
    // Present thread: create or resize everything the job threads will record with.
    void prepare(ID3D11Device* device) noexcept {
        if(!fsr4_provider_path.empty()) Fsr4EyeUpscaler::poll_retired();
        if(!enabled.load() || !device) return;
        const UINT w=wanted_w_.load(),h=wanted_h_.load();
        if(!w || !h) return;
        Candidate best{};
        {
            std::lock_guard<std::mutex> lock(candidate_mutex_);
            for(const auto& c:candidates_) if(c.resource && c.w==w && c.h==h && c.clears>best.clears) best=c;
            if(best.resource) best.resource->AddRef();
        }
        Ref<ID3D11Resource> best_ref;best_ref.adopt(best.resource);
        if(!best.resource) {
            // No depth target of this size yet (new size): decay old counts so it can get in.
            std::lock_guard<std::mutex> lock(candidate_mutex_);for(auto& c:candidates_) c.clears/=2;
            return;
        }
        const float f=fraction.load();
        if(ready.load() && ready_w_==w && ready_h_==h && ready_fraction_==f && ready_native_format_==wanted_native_format_.load() && depth_resource_.p==best.resource &&
           (!timing_enabled.load() || ((*eyes_)[0].timer.valid() && (*eyes_)[1].timer.valid()))) {
            if(temporal_eye::diagnostic_capture_enabled.load(std::memory_order_relaxed)) {std::lock_guard<std::mutex> lock(mutex_);selected_clear_=best.clear;}return;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        ready=false;
        if(!copy_.p) {
            // Depth copy plus a reactive mask: the first-person arms/weapons render in their own near
            // depth range (measured ~0.10-0.20 reversed-Z; nearest world surfaces < 0.01), and their motion
            // vectors do not follow the tracked arm pose, so FSR must not trust their history.
            static const char src[]="Texture2D<float> d:register(t0);RWTexture2D<float> o:register(u0);RWTexture2D<float> r:register(u1);"
                "[numthreads(8,8,1)] void main(uint3 i:SV_DispatchThreadID){float z=d.Load(int3(i.xy,0));o[i.xy]=z;r[i.xy]=z>0.02?0.9:0;}"; // ponytail: 0.02 (was 0.06) also covers the full-body avatar beyond arm reach; nearest world <0.01. Unmeasured, tune in-headset.
            ID3DBlob* code{};
            if(FAILED(D3DCompile(src,sizeof src-1,nullptr,nullptr,nullptr,"main","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,nullptr))) {++create_failures;enabled=false;return;}
            ID3D11ComputeShader* cs{};
            const bool ok=SUCCEEDED(device->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&cs));
            code->Release();
            if(!ok) {++create_failures;enabled=false;return;}
            copy_.p=cs;
        }
        if(depth_resource_.p!=best.resource) {
            D3D11_SHADER_RESOURCE_VIEW_DESC sd{};sd.Format=best.srv;sd.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;sd.Texture2D.MipLevels=1;
            ID3D11ShaderResourceView* view{};
            if(FAILED(device->CreateShaderResourceView(best.resource,&sd,&view))) {++create_failures;return;}
            depth_view_.adopt(view);depth_resource_.reset(best.resource);
            if(temporal_eye::diagnostic_capture_enabled.load(std::memory_order_relaxed)) {selected_clear_=best.clear;dlss_depth_selection_generation=++depth_selection_generation_;}
        }
        const bool upscale=f<.999f;
        const UINT rw=upscale?static_cast<UINT>(std::lround(w*f)):w,rh=upscale?static_cast<UINT>(std::lround(h*f)):h;
        if(ready_w_!=w || ready_h_!=h || upscale!=(ready_fraction_<.999f) || rw>context_w_ || rh>context_h_ ||
           ready_native_format_!=wanted_native_format_.load() || !(*eyes_)[0].fsr.valid() || !(*eyes_)[1].fsr.valid()) {
            ready_w_=ready_h_=0;
            const float cap=std::min(1.f,std::max(f,max_fraction.load()));
            const UINT cw=upscale?std::max(rw,static_cast<UINT>(std::lround(w*cap))):w,ch=upscale?std::max(rh,static_cast<UINT>(std::lround(h*cap))):h;
            ++context_builds;
            for(auto& e:*eyes_) {
                e.timer.clear();
                e.fsr.destroy();e.fsr4.destroy();e.depth.reset();e.depth_uav.reset();e.reactive.reset();e.reactive_uav.reset();e.colour_in.reset();e.upscaled.reset();e.reset=true;
                D3D11_TEXTURE2D_DESC d{};d.Width=cw;d.Height=ch;d.MipLevels=1;d.ArraySize=1;d.Format=DXGI_FORMAT_R32_FLOAT;d.SampleDesc.Count=1;
                d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;
                ID3D11Texture2D* t{};ID3D11UnorderedAccessView* u{};
                if(FAILED(device->CreateTexture2D(&d,nullptr,&t))) {++create_failures;return;}
                e.depth.adopt(t);
                if(FAILED(device->CreateUnorderedAccessView(t,nullptr,&u))) {++create_failures;return;}
                e.depth_uav.adopt(u);
                d.Format=DXGI_FORMAT_R8_UNORM;t=nullptr;u=nullptr;
                if(FAILED(device->CreateTexture2D(&d,nullptr,&t))) {++create_failures;return;}
                e.reactive.adopt(t);
                if(FAILED(device->CreateUnorderedAccessView(t,nullptr,&u))) {++create_failures;return;}
                e.reactive_uav.adopt(u);
                Fsr3EyeUpscaler::Desc fd{};fd.render_w=cw;fd.render_h=ch;fd.display_w=w;fd.display_h=h;
                // The game's motion vectors carry the frame-to-frame jitter delta (measured: a still head gives a
                // uniform MV equal to the engine pattern step, e.g. (2,-4)/16 px), so FSR must cancel it itself.
                fd.mv_jittered=true;
                if(upscale) {
                    // Upscale mode works on the tonemapped LDR image (gamma bytes), after the game's own upscale draw.
                    fd.hdr=false;fd.auto_exposure=false;
                    d.Format=DXGI_FORMAT_R8G8B8A8_UNORM;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;t=nullptr;
                    if(FAILED(device->CreateTexture2D(&d,nullptr,&t))) {++create_failures;return;}
                    e.colour_in.adopt(t);
                    d.Width=w;d.Height=h;d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;t=nullptr;
                    if(FAILED(device->CreateTexture2D(&d,nullptr,&t))) {++create_failures;return;}
                    e.upscaled.adopt(t);
                }
                if(!e.fsr.create(device,fd)) {++create_failures;enabled=false;return;}
                const auto format=upscale?DXGI_FORMAT_R8G8B8A8_UNORM:wanted_native_format_.load();
                if(!fsr4_provider_path.empty() && !e.fsr4.create(device,fd,fsr4_provider_path.c_str(),format,format)) ++fsr4_create_failures;
            }
            ready_w_=w;ready_h_=h;render_w_=rw;render_h_=rh;context_w_=cw;context_h_=ch;ready_fraction_=f;ready_native_format_=wanted_native_format_.load();
            for(std::size_t side=0;side<2;++side) {eye_gpu_ms[side]=0;eye_gpu_total_ms[side]=0;eye_gpu_samples[side]=0;eye_gpu_disjoint[side]=0;eye_gpu_dropped[side]=0;}
            for(std::size_t side=0;side<2;++side) {eye_fsr4_compute_gpu_ms[side]=0;eye_fsr4_compute_gpu_total_ms[side]=0;eye_fsr4_compute_gpu_samples[side]=0;}
            ++reset_epoch_;
        } else {render_w_=rw;render_h_=rh;ready_fraction_=f;} // FSR takes a new render size per dispatch; history stays
        if(timing_enabled.load()) for(auto& e:*eyes_) if(!e.timer.prepare(device)) {++timing_create_failures;timing_enabled=false;break;}
        ready=true;
    }
    void clear() {
        invalidate_dlss_sources(true);
        std::lock_guard<std::mutex> lock(mutex_);
        ready=false;++reset_epoch_;
        dlss_session_end_locked();
        for(auto& e:*eyes_) {e.fsr.destroy();e.fsr4.destroy();e.timer.clear();e.depth.reset();e.depth_uav.reset();e.reactive.reset();e.reactive_uav.reset();e.colour_in.reset();e.upscaled.reset();}
        Fsr4EyeUpscaler::poll_retired();
        for(auto& m:motion_) m.reset();
        selected_clear_.reset();dlss_depth_selection_generation=++depth_selection_generation_;depth_view_.reset();depth_resource_.reset();copy_.reset();ready_w_=ready_h_=0;wanted_w_=0;wanted_h_=0;ready_native_format_=DXGI_FORMAT_UNKNOWN;wanted_native_format_=DXGI_FORMAT_UNKNOWN;
        {std::lock_guard<std::mutex> jobs(jobs_mutex_);for(auto& r:recording_) {r.first=nullptr;r.second.reset();}queued_.clear();}
        std::lock_guard<std::mutex> lock2(candidate_mutex_);
        for(auto& c:candidates_) {if(c.resource) c.resource->Release();c={};}
    }
    // Job thread, inside the TAA dispatch hook. Seals everything recorded before the TAA dispatch
    // into its own command list and skips the dispatch; FSR then runs on the immediate context
    // between that prefix and the remainder (execute()). True when the dispatch was taken over.
    bool replace(ID3D11DeviceContext* c,int eye,const TaaBindings& b,float vfov,float jitter_x,float jitter_y) {
        if(!enabled.load() || !c || eye<0 || eye>1 || !b.taa() || c->GetType()!=D3D11_DEVICE_CONTEXT_DEFERRED) return false;
        const UINT w=b.tex[5].Width,h=b.tex[5].Height;
        wanted_w_=w;wanted_h_=h;
        // Both the upscale TAA bypass and the native failure fallback use a whole-resource copy.
        if(b.tex[5].Format!=b.tex[2].Format || b.tex[5].MipLevels!=b.tex[2].MipLevels ||
           b.tex[5].ArraySize!=b.tex[2].ArraySize || b.tex[5].SampleDesc.Count!=b.tex[2].SampleDesc.Count ||
           b.tex[5].SampleDesc.Quality!=b.tex[2].SampleDesc.Quality) return false;
        wanted_native_format_=b.tex[2].Format;
        if(fraction.load()<.999f) {
            // Upscale mode: FSR accumulates at the end (post-tonemap); the native TAA becomes a copy so
            // there is no second temporal filter, and its motion vectors are kept for FSR.
            {std::lock_guard<std::mutex> lock(mutex_);motion_[static_cast<std::size_t>(eye)].reset(b.res[1]);
                if(!ready.load() || ready_w_!=w || ready_h_!=h || ready_fraction_!=fraction.load()) {++not_ready;return false;}}
            c->CopyResource(b.res[5],b.res[2]);++taa_copies;
            return true;
        }
        {std::lock_guard<std::mutex> lock(mutex_);
            if(!ready.load() || ready_w_!=w || ready_h_!=h || ready_fraction_!=fraction.load()) {++not_ready;return false;}}
        auto job=std::make_unique<Job>();
        job->fraction=fraction.load();job->epoch=reset_epoch_.load();job->width=w;job->height=h;
        job->eye=eye;job->vfov=vfov;job->jitter_x=jitter_x;job->jitter_y=jitter_y;
        job->colour.reset(b.res[2]);job->motion.reset(b.res[1]);job->output.reset(b.res[5]);
        // Claim this context's slot first: a context already split (a second TAA dispatch before its
        // Finish) or no free slot leaves the native dispatch in place rather than dropping a prefix.
        std::lock_guard<std::mutex> lock(jobs_mutex_);
        decltype(recording_)::value_type* slot{};
        for(auto& r:recording_) if(r.first==c) {++not_ready;return false;}
        for(auto& r:recording_) if(!r.first) {slot=&r;break;}
        if(!slot) {++failures;return false;}
        ID3D11CommandList* prefix{};
        if(FAILED(TaaDispatchHook::finish_original(c,TRUE,&prefix)) || !prefix) {++failures;return false;}
        if(diagnostic_finished_observer) diagnostic_finished_observer(diagnostic_finished_user,c,prefix);
        job->prefix.adopt(prefix);
        slot->first=c;slot->second=std::move(job);
        return true;
    }
    // Any job thread: the game finished the remainder of a split context.
    void finished(ID3D11DeviceContext* c,ID3D11CommandList* list) {
        std::lock_guard<std::mutex> lock(jobs_mutex_);
        for(auto& slot:recording_) if(slot.first==c && slot.second) {
            auto job=std::move(slot.second);slot.first=nullptr;job->remainder.reset(list); // held: no address reuse
            queued_.push_back(std::move(job)); // every returned list retains its prefix until execution
            return;
        }
    }
    // Immediate context (render thread). Runs prefix, FSR, remainder for a split list.
    bool execute(ID3D11DeviceContext* c,ID3D11CommandList* list,BOOL restore,TaaDispatchHook::ExecuteFn original) {
        std::unique_ptr<Job> job;
        {
            std::lock_guard<std::mutex> lock(jobs_mutex_);
            for(auto q=queued_.begin();q!=queued_.end();++q) if((*q)->remainder.p==list) {job=std::move(*q);queued_.erase(q);break;}
        }
        if(!job) return false;
        // Preserve the caller's state around every prefix when ExecuteCommandList(TRUE) requested it.
        for(Job* j=job.get();j;j=j->next.get()) {original(c,j->prefix.p,restore);dispatch(c,*j);}
        original(c,list,restore);
        return true;
    }
private:
    void publish_fsr4_gpu_timing(std::size_t side,const Fsr4EyeUpscaler& fsr4) {
        eye_fsr4_compute_gpu_ms[side]=fsr4.compute_gpu_ms();
        eye_fsr4_compute_gpu_total_ms[side]=fsr4.compute_gpu_total_ms();
        eye_fsr4_compute_gpu_samples[side]=fsr4.compute_gpu_samples();
    }
    struct Job {
        int eye{};float vfov{},jitter_x{},jitter_y{},fraction=1;bool upscale{};UINT width{},height{};std::uint64_t epoch{};
        Ref<ID3D11Resource> colour,motion,output;Ref<ID3D11CommandList> prefix,remainder;
        std::unique_ptr<Job> next; // a later split of the same context (commands since this one)
    };
    std::mutex jobs_mutex_;
    std::array<std::pair<ID3D11DeviceContext*,std::unique_ptr<Job>>,8> recording_{};
    std::vector<std::unique_ptr<Job>> queued_;
    // Immediate context, under mutex_: one NGX evaluate for this eye, isolated from the game's pipeline state and
    // tracked by an EVENT ticket. False leaves the eye to the FSR path; a failed evaluate parks DLSS for the session.
    bool dlss_dispatch_locked(ID3D11DeviceContext* c,const Job& job,Eye& e,const Fsr3EyeUpscaler::Inputs& in,float dt,unsigned previous_generation) noexcept {
        bool evaluated=false;
        dlss_guarded([&] {evaluated=dlss_dispatch_unguarded(c,job,e,in,dt,previous_generation);},[&] {evaluated=false;e.reset=true;dlss_fail_locked(DlssEyeUpscaler::Reason::exception);});
        return evaluated;
    }
    bool dlss_dispatch_unguarded(ID3D11DeviceContext* c,const Job& job,Eye& e,const Fsr3EyeUpscaler::Inputs& in,float dt,unsigned previous_generation) {
        if(!dlss_owner_ || !dlss_lease_ || c!=dlss_immediate_) {++dlss_owner_refusals;return false;}
        const auto eye_index=static_cast<std::size_t>(job.eye);
        // The motion texture must be the colour texture's size: both are the engine's full-size targets holding the
        // render subrect, which is what the feature's scale and MVLowRes flag describe. Anything else is unknown input.
        {
            Ref<ID3D11Texture2D> motion_texture,colour_texture;D3D11_TEXTURE2D_DESC md{},cd{};
            if(!job.motion.p || FAILED(job.motion.p->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&motion_texture.p))) ||
               !job.colour.p || FAILED(job.colour.p->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&colour_texture.p)))) {dlss_fail_locked(DlssEyeUpscaler::Reason::invalid_resource);return false;}
            motion_texture.p->GetDesc(&md);colour_texture.p->GetDesc(&cd);
            eye_motion_width[eye_index]=md.Width;eye_motion_height[eye_index]=md.Height;
            if(md.Width!=cd.Width || md.Height!=cd.Height || md.Width<dlss_desc_.render_width || md.Height<dlss_desc_.render_height) {dlss_fail_locked(DlssEyeUpscaler::Reason::invalid_resource);return false;}
        }
        DlssEyeUpscaler::Eval ev{};
        ev.eye=static_cast<unsigned>(job.eye);ev.frame=ev.pair=++dlss_frame_[eye_index];
        ev.device_generation=dlss_desc_.device_generation;ev.dimension_generation=dlss_desc_.dimension_generation;ev.feature_generation=dlss_desc_.feature_generation;
        ev.context=c;
        ev.colour=job.upscale?static_cast<ID3D11Resource*>(e.colour_in.p):job.colour.p;
        ev.depth=e.depth.p;ev.motion=job.motion.p;
        ev.output=job.upscale?static_cast<ID3D11Resource*>(e.upscaled.p):job.output.p;
        const float jx=in.jitter_px_x*config_.dlss_jitter_x,jy=in.jitter_px_y*config_.dlss_jitter_y;
        ev.jitter={jx,jy,dlss_previous_jitter_[eye_index][0],dlss_previous_jitter_[eye_index][1],
            static_cast<std::uint32_t>((ev.frame-1)%dlss_desc_.jitter.phase_count),dlss_desc_.jitter.phase_count,false};
        ev.motion_contract=dlss_desc_.motion;ev.colour_contract=dlss_desc_.colour_contract;
        ev.frame_time_ms=dt;ev.discontinuity=e.reset || previous_generation!=dlss_mode::generation;ev.scratch_owner=dlss_scratch_;
        auto use=std::make_shared<DlssUse>();use->generation=dlss_lease_.generation();
        D3D11TemporalOwner::Status status;
        auto dlss_ticket=dlss_owner_->reserve(use,dlss_lease_.retained_owner(),status);
        if(!dlss_ticket && status==D3D11TemporalOwner::Status::full) { // GPU latency may have freed a slot since the last poll
            dlss_poll_locked();dlss_ticket=dlss_owner_->reserve(use,dlss_lease_.retained_owner(),status);
        }
        if(!dlss_ticket) {
            ++dlss_owner_refusals;dlss_owner_status=static_cast<unsigned>(status);
            if(dlss_refusals_.note(true)) {e.reset=true;dlss_fail_locked(DlssEyeUpscaler::Reason::owner_full);} // persistent refusal: FSR 3 for the session, logged
            return false;
        }
        dlss_refusals_.note(false);
        dlss_ledger_.reserved();
        DlssEyeUpscaler::Status result{};bool evaluated=false;
        const auto recorded=dlss_owner_->record(*dlss_ticket,[&](D3D11TemporalOwner::Recording& recording) {
            return recording.isolate([&](ID3D11DeviceContext1*) {evaluated=DlssEyeUpscaler::evaluate(dlss_lease_,ev,&result);return evaluated;})==D3D11TemporalOwner::Status::ok;
        });
        if(recorded!=D3D11TemporalOwner::Status::ok && recorded!=D3D11TemporalOwner::Status::callback_failed) dlss_owner_status=static_cast<unsigned>(recorded);
        if(!evaluated) {
            ++dlss_evaluate_failures;e.reset=true;
            dlss_fail_locked(result.reason==DlssEyeUpscaler::Reason::none?DlssEyeUpscaler::Reason::invalid_context:result.reason,result.result);
            return false;
        }
        ++dlss_evaluations;dlss_previous_jitter_[eye_index]={jx,jy};
        return true;
    }
    void dispatch(ID3D11DeviceContext* c,Job& job) {
        std::lock_guard<std::mutex> lock(mutex_);
        // A split native TAA list still needs a result if settings/resize invalidated FSR before execution.
        const auto fallback=[&]() {if(!job.upscale && job.colour.p && job.output.p) {c->CopyResource(job.output.p,job.colour.p);++fallback_copies;} else ++upscale_skipped;};
        if(!enabled.load() || !ready.load() || !depth_view_.p) {fallback();return;}
        if(job.epoch!=reset_epoch_.load() || job.fraction!=ready_fraction_ || job.width!=ready_w_ || job.height!=ready_h_) {
            ++stale_jobs;fallback();return;
        }
        auto& e=(*eyes_)[static_cast<std::size_t>(job.eye)];
        const UINT w=ready_w_,h=ready_h_;
        if(job.upscale && (!e.colour_in.p || !e.upscaled.p)) {++failures;return;}
        LARGE_INTEGER cpu_start{},cpu_end{},cpu_freq{};QueryPerformanceCounter(&cpu_start);QueryPerformanceFrequency(&cpu_freq);
        const int ticket=timing_enabled.load()?e.timer.begin(c):-1;
        ModGpuScope mod_scope(c,job.eye==0?ModSpan::fsr_eye0:ModSpan::fsr_eye1); // task 3b: the mod's FSR GPU time per eye
        CsState saved;saved.save(c);
        dlss_maintain_locked(c); // DLSS features live on the thread that executes the lists, inside the state save/restore
        ID3D11ShaderResourceView* null_srv[16]{};ID3D11UnorderedAccessView* null_uav[8]{};
        c->CSSetShaderResources(0,16,null_srv);c->CSSetUnorderedAccessViews(0,8,null_uav,nullptr);
        ID3D11UnorderedAccessView* outs[2]{e.depth_uav.p,e.reactive_uav.p};
        c->CSSetShader(copy_.p,nullptr,0);c->CSSetShaderResources(0,1,&depth_view_.p);c->CSSetUnorderedAccessViews(0,2,outs,nullptr);
        const UINT depth_w=job.upscale?render_w_:w,depth_h=job.upscale?render_h_:h;
        c->Dispatch((depth_w+7)/8,(depth_h+7)/8,1);
        c->CSSetShaderResources(0,1,null_srv);c->CSSetUnorderedAccessViews(0,2,null_uav,nullptr);
        LARGE_INTEGER now{},freq{};QueryPerformanceCounter(&now);QueryPerformanceFrequency(&freq);
        float dt=e.last.QuadPart?static_cast<float>(double(now.QuadPart-e.last.QuadPart)*1000.0/double(freq.QuadPart)):11.1f;
        e.last=now;if(!(dt>0 && dt<100)) dt=11.1f;
        Fsr3EyeUpscaler::Inputs in{};
        in.colour=job.colour.p;in.motion=job.motion.p;in.output=job.output.p;in.depth=e.depth.p;in.reactive=reactive_enabled?e.reactive.p:nullptr;in.render_w=w;in.render_h=h;
        in.mv_scale_x=config_.mv_x*static_cast<float>(w);in.mv_scale_y=config_.mv_y*static_cast<float>(h);
        if(job.upscale) {
            in.output=e.upscaled.p;in.render_w=render_w_;in.render_h=render_h_;
            const float mw=config_.mv_full?static_cast<float>(w):static_cast<float>(render_w_),mh=config_.mv_full?static_cast<float>(h):static_cast<float>(render_h_);
            in.mv_scale_x=config_.mv_x*mw;in.mv_scale_y=config_.mv_y*mh;
        }
        in.jitter_px_x=job.jitter_x;in.jitter_px_y=job.jitter_y;
        in.reset=e.reset;in.frame_time_ms=dt;in.sharpness=config_.sharpness;
        in.cam_near=.01f;in.cam_far=1e6f;in.vfov_rad=job.vfov>.1f && job.vfov<3?job.vfov:1.6f;
        if(job.eye==0 && !dump_path.empty() && replaced.load()>=dump_after) {
            D3D11_TEXTURE2D_DESC d{};e.depth.p->GetDesc(&d);d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
            ID3D11Device* dev{};c->GetDevice(&dev);ID3D11Texture2D* st{};
            if(dev && SUCCEEDED(dev->CreateTexture2D(&d,nullptr,&st))) {
                c->CopyResource(st,e.depth.p);D3D11_MAPPED_SUBRESOURCE m{};
                if(SUCCEEDED(c->Map(st,0,D3D11_MAP_READ,0,&m))) {
                    FILE* f{};if(!_wfopen_s(&f,dump_path.c_str(),L"wb") && f) {for(UINT y=0;y<d.Height;++y) fwrite(static_cast<char*>(m.pData)+y*m.RowPitch,4,d.Width,f);fclose(f);}
                    c->Unmap(st,0);
                }
                st->Release();
            }
            // Motion vectors too (dump_path + ".mv"): u32 width,height,format,row pitch, then mip 0 rows.
            ID3D11Texture2D* mv{};
            if(dev && job.motion.p && SUCCEEDED(job.motion.p->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&mv)))) {
                D3D11_TEXTURE2D_DESC md{};mv->GetDesc(&md);md.Usage=D3D11_USAGE_STAGING;md.BindFlags=0;md.CPUAccessFlags=D3D11_CPU_ACCESS_READ;md.MiscFlags=0;
                ID3D11Texture2D* ms{};
                if(SUCCEEDED(dev->CreateTexture2D(&md,nullptr,&ms))) {
                    c->CopyResource(ms,mv);D3D11_MAPPED_SUBRESOURCE m{};
                    if(SUCCEEDED(c->Map(ms,0,D3D11_MAP_READ,0,&m))) {
                        FILE* f{};
                        if(!_wfopen_s(&f,(dump_path+L".mv").c_str(),L"wb") && f) {
                            const UINT header[4]{md.Width,md.Height,static_cast<UINT>(md.Format),m.RowPitch};fwrite(header,4,4,f);
                            for(UINT y=0;y<md.Height;++y) fwrite(static_cast<char*>(m.pData)+y*m.RowPitch,1,m.RowPitch,f);
                            fclose(f);
                        }
                        c->Unmap(ms,0);
                    }
                    ms->Release();
                }
                mv->Release();
            }
            if(dev) dev->Release();
            dump_path.clear();
        }
        const auto previous_generation=eye_upscaler_generation[static_cast<std::size_t>(job.eye)].load();
        if(in.colour) {
            Ref<ID3D11Texture2D> texture;
            if(SUCCEEDED(in.colour->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&texture.p)))) {
                D3D11_TEXTURE2D_DESC d{};texture.p->GetDesc(&d);
                const auto eye=static_cast<std::size_t>(job.eye);
                eye_colour_width[eye]=d.Width;eye_colour_height[eye]=d.Height;eye_colour_format[eye]=d.Format;eye_colour_mips[eye]=d.MipLevels;
            }
        }
        // DLSS (modes 7/8) evaluates first; a refusal or failure leaves this eye to the FSR path below.
        bool dl=false;
        if(dlss_fallback_.should_evaluate() && dlss_lease_) {
            if(job.upscale) {const D3D11_BOX active{0,0,0,render_w_,render_h_,1};c->CopySubresourceRegion(e.colour_in.p,0,0,0,0,job.colour.p,0,&active);}
            dl=dlss_dispatch_locked(c,job,e,in,dt,previous_generation);
        }
        if(dlss_fallback_.selected() && !dl) ++dlss_fallbacks;
        in.reset=e.reset || previous_generation!=4;
        // FSR4 copies its shared output directly to the native eye target. FSR3
        // still needs its UAV-capable intermediate for targets without UAV binds.
        if(job.upscale) in.output=job.output.p;
        Fsr4EyeUpscaler::Diagnostics diagnostics{};
        const bool observe_fsr4=!dl && fsr4_diagnostics_observer && fsr4_active.load(std::memory_order_relaxed) && e.fsr4.valid() &&
            fsr4_diagnostics_observer(fsr4_diagnostics_user,job.eye,diagnostics);
        const bool ml=!dl && fsr4_active.load(std::memory_order_relaxed) && e.fsr4.valid() &&
            e.fsr4.dispatch(c,in,fsr4_compute_timing_enabled.load(),observe_fsr4?&diagnostics:nullptr);
        publish_fsr4_gpu_timing(static_cast<std::size_t>(job.eye),e.fsr4);
        eye_fsr4_failure[static_cast<std::size_t>(job.eye)]=ml?0u:e.fsr4.failure_code();
        eye_fsr4_record_ms[static_cast<std::size_t>(job.eye)]=ml?e.fsr4.record_ms():0.f;eye_fsr4_handoff_ms[static_cast<std::size_t>(job.eye)]=ml?e.fsr4.handoff_ms():0.f;
        if(ml) ++fsr4_dispatches;
        else if(!fsr4_provider_path.empty() && fsr4_active.load(std::memory_order_relaxed)) ++fsr4_fallbacks;
        // FSR4 already crops the native padded source into its shared input.
        // Materialize the smaller D3D11 source only for FSR3 or its fallback.
        if(!ml && !dl && job.upscale) {
            const D3D11_BOX active{0,0,0,render_w_,render_h_,1};
            c->CopySubresourceRegion(e.colour_in.p,0,0,0,0,job.colour.p,0,&active);
            in.colour=e.colour_in.p;
            in.output=e.upscaled.p;
        }
        in.reset=e.reset || previous_generation!=3;
        const bool ok=dl || ml || e.fsr.dispatch(c,in);
        saved.restore(c);
        if(!ok) {e.timer.end(c,ticket);++failures;e.reset=true;fallback();return;}
        eye_upscaler_generation[static_cast<std::size_t>(job.eye)]=dl?dlss_mode::generation:ml?4u:3u;
        eye_fsr4_version[static_cast<std::size_t>(job.eye)]=ml?e.fsr4.version_id():0;
        if(job.upscale) {if(!ml) c->CopyResource(job.output.p,e.upscaled.p);++upscaled;} // over the game's bilinear upscale
        e.timer.end(c,ticket);
        const auto timing=e.timer.stats();const auto side=static_cast<std::size_t>(job.eye);
        eye_gpu_ms[side]=static_cast<float>(timing.last_ms);eye_gpu_samples[side]=timing.samples;
        eye_gpu_total_ms[side]=timing.total_ms;
        eye_gpu_disjoint[side]=timing.disjoint;eye_gpu_dropped[side]=timing.dropped;
        QueryPerformanceCounter(&cpu_end);const double cpu_dispatch_ms=double(cpu_end.QuadPart-cpu_start.QuadPart)*1000.0/double(cpu_freq.QuadPart);
        eye_cpu_ms[side]=static_cast<float>(cpu_dispatch_ms);
        if(ml && observe_fsr4 && diagnostics.callback)
            diagnostics.callback(diagnostics.user,diagnostics.id,Fsr4EyeUpscaler::Phase::cpu_dispatch,c,cpu_dispatch_ms);
        eye_render_width[side]=in.render_w;eye_render_height[side]=in.render_h;eye_display_width[side]=w;eye_display_height[side]=h;
        ++eye_dispatches[static_cast<std::size_t>(job.eye)];
        if(e.reset) ++eye_resets[static_cast<std::size_t>(job.eye)];
        e.reset=false;++replaced;
    }
public:
};
inline thread_local FsrTaa::Ref<ID3D11Resource> FsrTaa::pass_source_{};
inline thread_local FsrTaa::Ref<ID3D11Resource> FsrTaa::pass_target_{};
} // namespace d2vr
