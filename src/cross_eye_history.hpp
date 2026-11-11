#pragma once
// v1180 performance mode: engine history that crosses eyes, found and (opt-in) fixed generically.
//
// In alternate-eye mode one view renders per engine frame and the eyes alternate, so a target the engine reads "from the previous frame"
// holds the OTHER eye's render. The shader cache shows which passes keep such history (docs/superpowers/specs/2026-11-11-engine-side-handover.md
// item v): TAA (per-eye already), the temporal distance/volumetric fog (R_FOG_TEMPORAL: lerp(current, history sampled at the reprojected UV,
// 0.875), history ro_atm_distancefog_texdestimage), and nothing else that survives the VR cvar defaults (SSAO, DOF, camera motion blur and lens
// flares are off). This file does not rely on that list:
//
// Census (knob build/cross-eye-census, diagnostics): at every draw of a pass of the one-view, the pixel shader's SRVs (t0..t15) are reads and
// the bound render targets / depth are writes. A read of a texture last written in the previous engine frame (the other eye's render) and not
// yet in this one is a cross-eye history read; the 16 most frequent (texture, pass, pixel shader, slot) are in the status with their size and
// format and whether the draw's first render target has the same description (the usual "history in, new history out" shape).
//
// Fix (knob build/alt-eye-history, opt-in): a role = (pixel shader, SRV slot) the census saw as a cross-eye read on >= 32 engine frames with
// render target 0 shaped like the history. Per role two textures rotate by GPU order (like rig_mask.hpp's palette ring): before the role's
// draw, B (its output two engine frames ago = this eye's previous render) is copied into the history texture; after the draw B <- A,
// A <- render target 0. Needs the role's draw in consecutive engine frames (a gap leaves the engine's history for that frame). The census
// runs while the fix still learns (600 frames) and stops then unless its own knob is on.
//
// v1181 paired stereo: two eye views render per engine frame (ObjectMotion::eye_of_view names them). A read of a texture last written by
// the OTHER eye's view in this or the previous engine frame, and not since by this view, is a cross-eye read there. The ring needs no
// change of idea: the role's draws alternate eyes in GPU order (L R L R ...), so "two executions back" is this eye's previous render in
// both modes; only the CPU-side run bookkeeping differs (paired: consecutive executions alternate eyes within one engine frame step).
// Knob build/eye-history enables the fix on paired frames (build/alt-eye-history on one-view frames); census as before covers both.
// The reprojection inside those passes uses the view's own cb_previousviewprojectionmatrix (paired: each eye view rolls its own previous;
// alternate: the seeded per-eye previous) or this view's motion vectors, so restoring the eye's own history makes the pass consistent.
#include <d3d11.h>
#include <MinHook.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <sstream>
#include <string>
#include "render_pass_table.hpp"

namespace d2vr {
class CrossEyeHistory {
    struct Role;
public:
    inline static std::atomic<bool> census{false},fix{false},fix_paired{false};
    inline static std::atomic<std::uintptr_t> view{};                 // the alternate one-view (0: paired frames, inert)
    using FrameFn=std::uint32_t(*)() noexcept;inline static std::atomic<FrameFn> frame_fn{};
    static constexpr unsigned min_frames=32,learn_frames=600;
    class Scope {
    public:
        // paired_eye: the eye whose view `pass_view` is on a paired frame (ObjectMotion::eye_of_view), else -1
        Scope(ID3D11DeviceContext* c,std::uintptr_t pass_view,int pass,int paired_eye=-1) noexcept {
            const bool alternate=pass_view && pass_view==view.load(std::memory_order_relaxed);
            // v1186: every draw's targets are writes, in any pass (shadow views, depth-only draws without a pixel shader included); reads
            // count only in the eye views' passes
            if(!c) return;
            if(!pass_view || (!alternate && paired_eye<0)) {
                if(census.load(std::memory_order_relaxed)) if(const auto fn=frame_fn.load(std::memory_order_relaxed)) observe_targets(c,fn(),-1,pass);
                return;
            }
            const bool fixing=alternate?fix.load(std::memory_order_relaxed):fix_paired.load(std::memory_order_relaxed);
            const bool learning=fixing && learned_.load(std::memory_order_relaxed)<learn_frames;
            const bool watch=census.load(std::memory_order_relaxed) || learning;
            if(!(watch || fixing)) return;
            const auto fn=frame_fn.load(std::memory_order_relaxed);if(!fn) return;
            frame_=fn();note_frame(frame_);eye_=alternate?-1:paired_eye;pass_=pass;
            ID3D11PixelShader* ps{};c->PSGetShader(&ps,nullptr,nullptr);if(ps) ps->Release(); // identity only
            if(!ps) {if(watch) observe_targets(c,frame_,eye_,pass);return;}
            ID3D11ShaderResourceView* srv[16]{};ID3D11RenderTargetView* rtv[8]{};ID3D11DepthStencilView* dsv{};
            const int role=fixing?find_role(ps):-1;
            if(!watch && role<0) return;
            c->PSGetShaderResources(0,16,srv);c->OMGetRenderTargets(8,rtv,&dsv);
            ID3D11Resource* out0{};if(rtv[0]) rtv[0]->GetResource(&out0);
            if(watch) for(unsigned s=0;s<16;++s) if(srv[s]) {ID3D11Resource* r{};srv[s]->GetResource(&r);if(r) {note_read(r,out0,pass,ps,s);r->Release();}}
            if(role>=0 && out0) begin_role(c,static_cast<unsigned>(role),srv[roles_[role].slot],out0);
            if(watch) {
                for(auto* v:rtv) if(v) {ID3D11Resource* r{};v->GetResource(&r);if(r) {note_write(r);r->Release();}}
                if(dsv) {ID3D11Resource* r{};dsv->GetResource(&r);if(r) {note_write(r);r->Release();}}
            }
            for(auto* v:srv) if(v) v->Release();
            for(auto* v:rtv) if(v) v->Release();
            if(dsv) dsv->Release();
            if(out0) out0->Release();
        }
        void after() noexcept {if(c_ && out_) end_role(c_,role_,out_);}
        ~Scope() {if(out_) out_->Release();}
        Scope(const Scope&)=delete;Scope& operator=(const Scope&)=delete;
    private:
        friend class CrossEyeHistory;
        std::uint32_t frame_{};int eye_{-1},pass_{-1};ID3D11DeviceContext* c_{};unsigned role_{};ID3D11Resource* out_{};
        void note_read(ID3D11Resource* r,ID3D11Resource* out0,int pass,ID3D11PixelShader* ps,unsigned slot) noexcept {
            Entry* e=entry(r);if(!e) return;
            const std::uint32_t written=e->written.load(std::memory_order_relaxed);const int writer=e->writer.load(std::memory_order_relaxed);
            // alternate: last written in the previous engine frame (stored +1), not yet in this one; paired: last written by the other eye's
            // view in this or the previous engine frame (this view has not written it since)
            const bool cross=eye_<0?written==frame_:writer>=0 && writer!=eye_ && (written==frame_+1 || written==frame_);
            if(cross) {
                e->cross.fetch_add(1,std::memory_order_relaxed);e->pass.store(pass,std::memory_order_relaxed);
                e->ps.store(reinterpret_cast<std::uintptr_t>(ps),std::memory_order_relaxed);e->slot.store(slot,std::memory_order_relaxed);
                const bool shaped=out0 && same_shape(r,out0);e->shaped.store(shaped,std::memory_order_relaxed);
                if(e->flag_frame.exchange(frame_,std::memory_order_relaxed)!=frame_) {
                    const unsigned frames=e->frames.fetch_add(1,std::memory_order_relaxed)+1;
                    if(shaped && frames>=min_frames) add_role(ps,slot);
                }
                cross_reads_.fetch_add(1,std::memory_order_relaxed);
            }
        }
        void note_write(ID3D11Resource* r) noexcept {write(r,frame_,eye_,pass_);}
        // the role's previous rotation was the execution just before this one in the eye sequence (alternate: one per engine frame;
        // paired: the other eye, same or previous engine frame)
        bool follows(const Role& ro) const noexcept {
            if(ro.last==~0u) return false;
            return eye_<0?ro.last_eye<0 && frame_==ro.last+1u:ro.last_eye>=0 && ro.last_eye!=eye_ && (frame_==ro.last || frame_==ro.last+1u);
        }
        void begin_role(ID3D11DeviceContext* c,unsigned role,ID3D11ShaderResourceView* history,ID3D11Resource* out0) noexcept {
            if(!history) return;
            Role& ro=roles_[role];std::lock_guard<std::mutex> lock(mutex_);
            if(!ensure(c,ro,out0)) return;
            ID3D11Resource* h{};history->GetResource(&h);if(!h) return;
            if(same_shape(h,out0) && follows(ro) && ro.run>=2) {c->CopyResource(h,ro.b);++ro.restored;restores_.fetch_add(1,std::memory_order_relaxed);}
            h->Release();
            c_=c;role_=role;out_=out0;out0->AddRef();
        }
        void end_role(ID3D11DeviceContext* c,unsigned role,ID3D11Resource* out0) noexcept {
            Role& ro=roles_[role];std::lock_guard<std::mutex> lock(mutex_);
            if(!ro.a || (ro.last==frame_ && ro.last_eye==eye_)) return; // one rotation per render
            ro.run=follows(ro)?(ro.run<255u?ro.run+1u:ro.run):1u;
            c->CopyResource(ro.b,ro.a);c->CopyResource(ro.a,out0);ro.last=frame_;ro.last_eye=eye_;
        }
    };
    // v1186: writes the census sees outside draws (CopyResource, CopySubresourceRegion, the clears, compute UAVs at Dispatch); `eye` the
    // writing view's paired eye or -1. Callers: this file's copy/clear hooks and taa_dispatch.hpp's depth-clear/dispatch hooks.
    static void write(ID3D11Resource* r,std::uint32_t frame,int eye,int pass) noexcept {
        if(Entry* e=entry(r)) {e->written.store(frame+1,std::memory_order_relaxed);e->writer.store(eye,std::memory_order_relaxed);e->writer_pass.store(pass,std::memory_order_relaxed);}
    }
    struct Writer {int eye{-1},pass{-1};};using WriterFn=Writer(*)() noexcept; // the current pass and its paired eye (dxgi_probe)
    inline static std::atomic<WriterFn> writer_fn{};
    static void note_resource(ID3D11Resource* r) noexcept {
        if(!r || !census.load(std::memory_order_relaxed)) return;
        const auto fn=frame_fn.load(std::memory_order_relaxed);const auto wf=writer_fn.load(std::memory_order_relaxed);if(!fn) return;
        const Writer w=wf?wf():Writer{};write(r,fn(),w.eye,w.pass);other_writes_.fetch_add(1,std::memory_order_relaxed);
    }
    template<class View> static void note_view(View* v) noexcept {if(!v || !census.load(std::memory_order_relaxed)) return;ID3D11Resource* r{};v->GetResource(&r);if(r) {note_resource(r);r->Release();}}
    static void note_dispatch(ID3D11DeviceContext* c) noexcept { // compute: the bound UAVs are writes
        if(!c || !census.load(std::memory_order_relaxed)) return;
        ID3D11UnorderedAccessView* u[8]{};c->CSGetUnorderedAccessViews(0,8,u);
        for(auto* v:u) if(v) {note_view(v);v->Release();}
    }
    static void install(ID3D11Device* device) noexcept; // the copy and render-target/UAV clear hooks (slots 46, 47, 50, 51, 52)
    static void begin_epoch() noexcept {std::lock_guard<std::mutex> lock(mutex_);for(auto& r:roles_) {r.run=0;r.last=~0u;r.last_eye=-1;}}
    static std::string json() {
        std::ostringstream o;
        o<<"{\"census\":"<<(census.load()?"true":"false")<<",\"fix\":"<<(fix.load()?"true":"false")<<",\"fix_paired\":"<<(fix_paired.load()?"true":"false")<<",\"frames\":"<<frames_.load()<<",\"cross_reads\":"<<cross_reads_.load()
         <<",\"restores\":"<<restores_.load()<<",\"other_writes\":"<<other_writes_.load()<<",\"top\":[";
        // the 16 entries with the most cross-eye reads: texture, reads, frames, pass, pixel shader, slot, w, h, format, history-shaped
        std::array<const Entry*,16> top{};
        for(const auto& e:table_) if(e.cross.load()) {
            const Entry* x=&e;
            for(auto& t:top) if(!t || x->cross.load()>t->cross.load()) std::swap(t,x);
        }
        bool first=true;
        for(const Entry* e:top) if(e) {
            D3D11_TEXTURE2D_DESC d{};describe(reinterpret_cast<ID3D11Resource*>(e->key.load()),d);
            o<<(first?"":",")<<"{\"texture\":\""<<std::hex<<e->key.load()<<"\",\"ps\":\""<<e->ps.load()<<std::dec<<"\",\"reads\":"<<e->cross.load()<<",\"frames\":"<<e->frames.load()
             <<",\"pass\":"<<e->pass.load()<<",\"pass_name\":\""<<(e->pass.load()>=0?render_pass_name(static_cast<unsigned>(e->pass.load())):"untagged")
             <<"\",\"writer_pass\":"<<e->writer_pass.load()<<",\"writer_name\":\""<<(e->writer_pass.load()>=0?render_pass_name(static_cast<unsigned>(e->writer_pass.load())):"untagged")<<"\""<<",\"slot\":"<<e->slot.load()<<",\"size\":["<<d.Width<<','<<d.Height<<"],\"format\":"<<d.Format<<",\"shaped\":"<<(e->shaped.load()?"true":"false")<<'}';
            first=false;
        }
        o<<"],\"roles\":[";
        {std::lock_guard<std::mutex> lock(mutex_);
         for(unsigned i=0;i<role_count_.load();++i) o<<(i?",":"")<<"{\"ps\":\""<<std::hex<<roles_[i].ps<<std::dec<<"\",\"slot\":"<<roles_[i].slot<<",\"restored\":"<<roles_[i].restored<<",\"run\":"<<roles_[i].run<<'}';}
        o<<"]}";
        return o.str();
    }
    static void reset() noexcept { // a device change: drop the role textures (roles and census stay)
        std::lock_guard<std::mutex> lock(mutex_);
        for(auto& r:roles_) {if(r.a) r.a->Release();if(r.b) r.b->Release();r.a=r.b=nullptr;r.run=0;r.last=~0u;r.device=nullptr;}
    }
private:
    struct Entry {std::atomic<std::uintptr_t> key{};std::atomic<std::uint32_t> written{},flag_frame{~0u};std::atomic<int> writer{-1};std::atomic<std::uint64_t> cross{};std::atomic<unsigned> frames{},slot{};
        std::atomic<int> pass{-1},writer_pass{-1};std::atomic<std::uintptr_t> ps{};std::atomic<bool> shaped{};};
    struct Role {std::uintptr_t ps{};unsigned slot{};ID3D11Texture2D* a{};ID3D11Texture2D* b{};ID3D11Device* device{};D3D11_TEXTURE2D_DESC desc{};
        std::uint32_t last{~0u};int last_eye{-1};unsigned run{};std::uint64_t restored{};};
    static constexpr unsigned table_size=4096,max_roles=8;
    inline static std::array<Entry,table_size> table_{};
    inline static std::array<Role,max_roles> roles_{};inline static std::atomic<unsigned> role_count_{};
    inline static std::mutex mutex_;
    inline static std::atomic<std::uint32_t> last_frame_{~0u};inline static std::atomic<std::uint64_t> frames_{},learned_{},cross_reads_{},restores_{},other_writes_{};
    static void observe_targets(ID3D11DeviceContext* c,std::uint32_t frame,int eye,int pass) noexcept {
        ID3D11RenderTargetView* rtv[8]{};ID3D11DepthStencilView* dsv{};c->OMGetRenderTargets(8,rtv,&dsv);
        for(auto* v:rtv) if(v) {ID3D11Resource* r{};v->GetResource(&r);if(r) {write(r,frame,eye,pass);r->Release();}v->Release();}
        if(dsv) {ID3D11Resource* r{};dsv->GetResource(&r);if(r) {write(r,frame,eye,pass);r->Release();}dsv->Release();}
    }
    // the hooks (one per slot; a slot another owner already hooked stays unobserved)
    using CopyFn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11Resource*,ID3D11Resource*);
    using CopyRegionFn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11Resource*,UINT,UINT,UINT,UINT,ID3D11Resource*,UINT,const D3D11_BOX*);
    using ClearRtvFn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11RenderTargetView*,const FLOAT*);
    using ClearUavUFn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11UnorderedAccessView*,const UINT*);
    using ClearUavFFn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11UnorderedAccessView*,const FLOAT*);
    inline static std::array<CopyFn,2> copy_{};inline static std::array<CopyRegionFn,2> region_{};inline static std::array<ClearRtvFn,2> clear_rtv_{};
    inline static std::array<ClearUavUFn,2> clear_uavu_{};inline static std::array<ClearUavFFn,2> clear_uavf_{};inline static std::array<void*,10> targets_{};
    template<int I> static void STDMETHODCALLTYPE copy_hook(ID3D11DeviceContext* c,ID3D11Resource* d,ID3D11Resource* s) {note_resource(d);copy_[I](c,d,s);}
    template<int I> static void STDMETHODCALLTYPE region_hook(ID3D11DeviceContext* c,ID3D11Resource* d,UINT ds,UINT x,UINT y,UINT z,ID3D11Resource* s,UINT ss,const D3D11_BOX* b) {
        note_resource(d);region_[I](c,d,ds,x,y,z,s,ss,b);}
    template<int I> static void STDMETHODCALLTYPE clear_rtv_hook(ID3D11DeviceContext* c,ID3D11RenderTargetView* v,const FLOAT* k) {note_view(v);clear_rtv_[I](c,v,k);}
    template<int I> static void STDMETHODCALLTYPE clear_uavu_hook(ID3D11DeviceContext* c,ID3D11UnorderedAccessView* v,const UINT* k) {note_view(v);clear_uavu_[I](c,v,k);}
    template<int I> static void STDMETHODCALLTYPE clear_uavf_hook(ID3D11DeviceContext* c,ID3D11UnorderedAccessView* v,const FLOAT* k) {note_view(v);clear_uavf_[I](c,v,k);}
    template<class Fn> static bool hook_slot(ID3D11DeviceContext* context,unsigned slot,void* replacement,Fn& original) noexcept {
        void* target=(*reinterpret_cast<void***>(context))[slot];
        for(auto* t:targets_) if(t==target) return true; // already hooked by this class (shared implementation)
        if(MH_CreateHook(target,replacement,reinterpret_cast<void**>(&original))!=MH_OK) {original=nullptr;return false;}
        if(MH_EnableHook(target)!=MH_OK) {MH_RemoveHook(target);original=nullptr;return false;}
        for(auto& t:targets_) if(!t) {t=target;break;}
        return true;
    }
    static void note_frame(std::uint32_t f) noexcept {
        if(last_frame_.exchange(f,std::memory_order_relaxed)!=f) {frames_.fetch_add(1,std::memory_order_relaxed);learned_.fetch_add(1,std::memory_order_relaxed);}
    }
    // ponytail: a texture's address is its identity; a released and recreated texture at the same address inherits the old entry (diagnostic only)
    static Entry* entry(ID3D11Resource* r) noexcept {
        const auto key=reinterpret_cast<std::uintptr_t>(r);
        const unsigned h=static_cast<unsigned>((key>>4)*0x9e3779b97f4a7c15ull>>52)&(table_size-1);
        for(unsigned i=0;i<64;++i) {
            Entry& e=table_[(h+i)&(table_size-1)];std::uintptr_t k=e.key.load(std::memory_order_acquire);
            if(k==key) return &e;
            if(!k && e.key.compare_exchange_strong(k,key,std::memory_order_acq_rel)) return &e;
            if(k==key) return &e;
        }
        return nullptr;
    }
    static bool describe(ID3D11Resource* r,D3D11_TEXTURE2D_DESC& d) noexcept {
        D3D11_RESOURCE_DIMENSION dim{};r->GetType(&dim);if(dim!=D3D11_RESOURCE_DIMENSION_TEXTURE2D) return false;
        static_cast<ID3D11Texture2D*>(r)->GetDesc(&d);return true;
    }
    static bool same_shape(ID3D11Resource* a,ID3D11Resource* b) noexcept {
        D3D11_TEXTURE2D_DESC x{},y{};
        return a!=b && describe(a,x) && describe(b,y) && x.Width==y.Width && x.Height==y.Height && x.Format==y.Format && x.MipLevels==y.MipLevels &&
            x.ArraySize==y.ArraySize && x.SampleDesc.Count==y.SampleDesc.Count && x.SampleDesc.Count==1;
    }
    static void add_role(ID3D11PixelShader* ps,unsigned slot) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);const auto key=reinterpret_cast<std::uintptr_t>(ps);
        const unsigned n=role_count_.load(std::memory_order_relaxed);
        for(unsigned i=0;i<n;++i) if(roles_[i].ps==key && roles_[i].slot==slot) return;
        if(n>=max_roles) return;
        roles_[n].ps=key;roles_[n].slot=slot;role_count_.store(n+1,std::memory_order_release);
    }
    static int find_role(ID3D11PixelShader* ps) noexcept {
        const auto key=reinterpret_cast<std::uintptr_t>(ps);const unsigned n=role_count_.load(std::memory_order_acquire);
        for(unsigned i=0;i<n;++i) if(roles_[i].ps==key) return static_cast<int>(i);
        return -1;
    }
    // Under mutex_: the role's two textures, shaped like render target 0 (recreated on a size/format/device change: the run restarts)
    static bool ensure(ID3D11DeviceContext* c,Role& ro,ID3D11Resource* out0) noexcept {
        D3D11_TEXTURE2D_DESC d{};if(!describe(out0,d) || d.SampleDesc.Count!=1) return false;
        ID3D11Device* device{};c->GetDevice(&device);if(device) device->Release();
        if(ro.a && ro.device==device && ro.desc.Width==d.Width && ro.desc.Height==d.Height && ro.desc.Format==d.Format && ro.desc.MipLevels==d.MipLevels && ro.desc.ArraySize==d.ArraySize) return true;
        if(ro.a) ro.a->Release();if(ro.b) ro.b->Release();ro.a=ro.b=nullptr;ro.run=0;ro.last=~0u;
        if(!device) return false;
        ro.desc=d;D3D11_TEXTURE2D_DESC t=d;t.Usage=D3D11_USAGE_DEFAULT;t.BindFlags=D3D11_BIND_SHADER_RESOURCE;t.CPUAccessFlags=0;t.MiscFlags=0;
        if(FAILED(device->CreateTexture2D(&t,nullptr,&ro.a)) || FAILED(device->CreateTexture2D(&t,nullptr,&ro.b))) {
            if(ro.a) ro.a->Release();if(ro.b) ro.b->Release();ro.a=ro.b=nullptr;return false;
        }
        ro.device=device;return true;
    }
};
inline void CrossEyeHistory::install(ID3D11Device* device) noexcept {
    if(!device || targets_[0]) return;
    ID3D11DeviceContext* deferred{};ID3D11DeviceContext* immediate{};
    if(FAILED(device->CreateDeferredContext(0,&deferred))) return;
    device->GetImmediateContext(&immediate);
    void** dv=*reinterpret_cast<void***>(deferred);void** iv=immediate?*reinterpret_cast<void***>(immediate):nullptr;
    hook_slot(deferred,47,reinterpret_cast<void*>(&copy_hook<0>),copy_[0]);hook_slot(deferred,46,reinterpret_cast<void*>(&region_hook<0>),region_[0]);
    hook_slot(deferred,50,reinterpret_cast<void*>(&clear_rtv_hook<0>),clear_rtv_[0]);hook_slot(deferred,51,reinterpret_cast<void*>(&clear_uavu_hook<0>),clear_uavu_[0]);
    hook_slot(deferred,52,reinterpret_cast<void*>(&clear_uavf_hook<0>),clear_uavf_[0]);
    if(immediate) {
        // the same code on both context kinds: the deferred hook (trampoline copy_[0] etc.) serves the immediate one as well
        if(iv[47]==dv[47]) copy_[1]=copy_[0];else hook_slot(immediate,47,reinterpret_cast<void*>(&copy_hook<1>),copy_[1]);
        if(iv[46]==dv[46]) region_[1]=region_[0];else hook_slot(immediate,46,reinterpret_cast<void*>(&region_hook<1>),region_[1]);
        if(iv[50]==dv[50]) clear_rtv_[1]=clear_rtv_[0];else hook_slot(immediate,50,reinterpret_cast<void*>(&clear_rtv_hook<1>),clear_rtv_[1]);
        if(iv[51]==dv[51]) clear_uavu_[1]=clear_uavu_[0];else hook_slot(immediate,51,reinterpret_cast<void*>(&clear_uavu_hook<1>),clear_uavu_[1]);
        if(iv[52]==dv[52]) clear_uavf_[1]=clear_uavf_[0];else hook_slot(immediate,52,reinterpret_cast<void*>(&clear_uavf_hook<1>),clear_uavf_[1]);
        immediate->Release();
    }
    deferred->Release();
}
} // namespace d2vr
