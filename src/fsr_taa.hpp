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
#include <string>
#include "fsr3_eye_upscaler.hpp"
#include "fsr4_eye_upscaler.hpp"
#include "taa_dispatch.hpp"
#include "gpu_pass_timer.hpp"

namespace d2vr {
// Replaces the native TAA dispatch of each eye with its own FSR 3.1 context (Luma's D2 map:
// t1 UV motion vectors, t2 pre-TAA colour, u1 TAA result). Recorded on the game's deferred
// context in command order, so this eye's scene depth (shared by both eyes, rendered just
// before this pass) is current. The scene depth is learned from depth-stencil clears of the
// TAA colour size; it is R24G8, so a tiny CS copies it to R32F for FSR.
// Every device call (contexts, textures, views) happens in prepare() on the Present thread;
// the job thread only records commands. Output overwrites u1 at the same size (native-
// resolution AA). The compute state of the game's context is restored afterwards.
class FsrTaa {
public:
    struct Config {float sharpness=.3f;float mv_x=-1,mv_y=-1;bool mv_full=false;};
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
    struct Candidate {ID3D11Resource* resource{};UINT w{},h{};DXGI_FORMAT srv{};std::uint64_t clears{};};
    std::mutex mutex_; // guards eyes_, depth views and config between prepare() and the job threads
    std::unique_ptr<std::array<Eye,2>> eyes_{std::make_unique<std::array<Eye,2>>()};
    Ref<ID3D11ComputeShader> copy_;Ref<ID3D11Resource> depth_resource_;Ref<ID3D11ShaderResourceView> depth_view_;
    UINT ready_w_{},ready_h_{},render_w_{},render_h_{},context_w_{},context_h_{};float ready_fraction_{1}; // context_*: max render size the FSR contexts/inputs were built for
    DXGI_FORMAT ready_native_format_{DXGI_FORMAT_UNKNOWN};
    std::array<Ref<ID3D11Resource>,2> motion_{}; // upscale mode: this frame's motion vectors per eye (from the TAA dispatch)
    inline static thread_local Ref<ID3D11Resource> pass_source_{},pass_target_{}; // upscale draw seen in this post-tonemap pass
    std::array<Candidate,4> candidates_{};std::mutex candidate_mutex_;
    std::atomic<UINT> wanted_w_{},wanted_h_{};
    std::atomic<DXGI_FORMAT> wanted_native_format_{DXGI_FORMAT_UNKNOWN};
    std::atomic<std::uint64_t> reset_epoch_{};
    Config config_;
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
    std::array<std::atomic<unsigned>,2> eye_upscaler_generation{};
    std::array<std::atomic<std::uint64_t>,2> eye_fsr4_version{};
    std::atomic<std::uint64_t> timing_create_failures{};
    std::array<std::atomic<float>,2> eye_gpu_ms{},eye_cpu_ms{};
    std::array<std::atomic<double>,2> eye_gpu_total_ms{};
    std::array<std::atomic<std::uint64_t>,2> eye_gpu_samples{},eye_gpu_disjoint{},eye_gpu_dropped{};
    std::array<std::atomic<UINT>,2> eye_render_width{},eye_render_height{},eye_display_width{},eye_display_height{};
    std::array<std::atomic<float>,2> eye_fsr4_record_ms{},eye_fsr4_handoff_ms{};
    std::array<std::atomic<UINT>,2> eye_fsr4_failure{},eye_colour_width{},eye_colour_height{},eye_colour_format{},eye_colour_mips{};
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
    void observe_depth(ID3D11DepthStencilView* dsv) noexcept {
        if(!dsv || !enabled.load(std::memory_order_relaxed)) return;
        ID3D11Resource* r{};dsv->GetResource(&r);if(!r) return;
        std::lock_guard<std::mutex> lock(candidate_mutex_);
        for(auto& c:candidates_) if(c.resource==r) {++c.clears;r->Release();return;}
        ID3D11Texture2D* t{};D3D11_TEXTURE2D_DESC d{};
        if(SUCCEEDED(r->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&t)))) {t->GetDesc(&d);t->Release();}
        const auto srv=depth_srv(d.Format);
        if(srv==DXGI_FORMAT_UNKNOWN || !(d.BindFlags&D3D11_BIND_SHADER_RESOURCE) || d.SampleDesc.Count!=1) {r->Release();return;}
        auto* slot=&candidates_[0];
        for(auto& c:candidates_) if(!c.resource || c.clears<slot->clears) slot=&c;
        if(slot->resource) slot->resource->Release();
        *slot={r,d.Width,d.Height,srv,1}; // keeps the reference
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
           (!timing_enabled.load() || ((*eyes_)[0].timer.valid() && (*eyes_)[1].timer.valid()))) return;
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
            ++reset_epoch_;
        } else {render_w_=rw;render_h_=rh;ready_fraction_=f;} // FSR takes a new render size per dispatch; history stays
        if(timing_enabled.load()) for(auto& e:*eyes_) if(!e.timer.prepare(device)) {++timing_create_failures;timing_enabled=false;break;}
        ready=true;
    }
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        ready=false;++reset_epoch_;
        for(auto& e:*eyes_) {e.fsr.destroy();e.fsr4.destroy();e.timer.clear();e.depth.reset();e.depth_uav.reset();e.reactive.reset();e.reactive_uav.reset();e.colour_in.reset();e.upscaled.reset();}
        Fsr4EyeUpscaler::poll_retired();
        for(auto& m:motion_) m.reset();
        depth_view_.reset();depth_resource_.reset();copy_.reset();ready_w_=ready_h_=0;wanted_w_=0;wanted_h_=0;ready_native_format_=DXGI_FORMAT_UNKNOWN;wanted_native_format_=DXGI_FORMAT_UNKNOWN;
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
    struct Job {
        int eye{};float vfov{},jitter_x{},jitter_y{},fraction=1;bool upscale{};UINT width{},height{};std::uint64_t epoch{};
        Ref<ID3D11Resource> colour,motion,output;Ref<ID3D11CommandList> prefix,remainder;
        std::unique_ptr<Job> next; // a later split of the same context (commands since this one)
    };
    std::mutex jobs_mutex_;
    std::array<std::pair<ID3D11DeviceContext*,std::unique_ptr<Job>>,8> recording_{};
    std::vector<std::unique_ptr<Job>> queued_;
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
        CsState saved;saved.save(c);
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
        in.reset=e.reset || previous_generation!=4;
        const bool ml=fsr4_active.load(std::memory_order_relaxed) && e.fsr4.valid() && e.fsr4.dispatch(c,in);
        eye_fsr4_failure[static_cast<std::size_t>(job.eye)]=ml?0u:e.fsr4.failure_code();
        eye_fsr4_record_ms[static_cast<std::size_t>(job.eye)]=ml?e.fsr4.record_ms():0.f;eye_fsr4_handoff_ms[static_cast<std::size_t>(job.eye)]=ml?e.fsr4.handoff_ms():0.f;
        if(ml) ++fsr4_dispatches;
        else if(!fsr4_provider_path.empty() && fsr4_active.load(std::memory_order_relaxed)) ++fsr4_fallbacks;
        // FSR4 already crops the native padded source into its shared input.
        // Materialize the smaller D3D11 source only for FSR3 or its fallback.
        if(!ml && job.upscale) {
            const D3D11_BOX active{0,0,0,render_w_,render_h_,1};
            c->CopySubresourceRegion(e.colour_in.p,0,0,0,0,job.colour.p,0,&active);
            in.colour=e.colour_in.p;
        }
        in.reset=e.reset || previous_generation!=3;
        const bool ok=ml || e.fsr.dispatch(c,in);
        saved.restore(c);
        if(!ok) {e.timer.end(c,ticket);++failures;e.reset=true;fallback();return;}
        eye_upscaler_generation[static_cast<std::size_t>(job.eye)]=ml?4u:3u;
        eye_fsr4_version[static_cast<std::size_t>(job.eye)]=ml?e.fsr4.version_id():0;
        if(job.upscale) {c->CopyResource(job.output.p,e.upscaled.p);++upscaled;} // over the game's bilinear upscale
        e.timer.end(c,ticket);
        const auto timing=e.timer.stats();const auto side=static_cast<std::size_t>(job.eye);
        eye_gpu_ms[side]=static_cast<float>(timing.last_ms);eye_gpu_samples[side]=timing.samples;
        eye_gpu_total_ms[side]=timing.total_ms;
        eye_gpu_disjoint[side]=timing.disjoint;eye_gpu_dropped[side]=timing.dropped;
        QueryPerformanceCounter(&cpu_end);eye_cpu_ms[side]=static_cast<float>(double(cpu_end.QuadPart-cpu_start.QuadPart)*1000.0/double(cpu_freq.QuadPart));
        eye_render_width[side]=in.render_w;eye_render_height[side]=in.render_h;eye_display_width[side]=w;eye_display_height[side]=h;
        ++eye_dispatches[static_cast<std::size_t>(job.eye)];
        if(e.reset) ++eye_resets[static_cast<std::size_t>(job.eye)];
        e.reset=false;++replaced;
    }
public:
};
} // namespace d2vr
