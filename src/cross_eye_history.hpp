#pragma once
// v1180 performance mode: engine history that crosses eyes, found and (opt-in) fixed generically.
//
// In alternate-eye mode one view renders per engine frame and the eyes alternate, so a target the engine reads "from the previous frame"
// holds the OTHER eye's render. The shader cache shows which passes keep such history (docs/superpowers/specs/2026-11-11-engine-side-handover.md
// item v): TAA (per-eye already), the temporal distance/volumetric fog (R_FOG_TEMPORAL: lerp(current, history sampled at the reprojected UV,
// 0.875), history ro_atm_distancefog_texdestimage), and nothing else that survives the VR cvar defaults (SSAO, DOF, camera motion blur and lens
// flares are off). This file does not rely on that list:
//
// Census (knob build/cross-eye-census, diagnostics): pixel-shader inputs (t0..t15) of draws in an eye view's passes are reads; render/depth
// targets of every draw, copy destinations, cleared views and compute UAVs are writes. A read of a texture whose last write belongs to the
// other eye's render is a cross-eye read; the 16 most frequent (texture, pass, pixel shader, slot) are in the status with their size and
// format and whether the draw's first render target has the same description (the usual "history in, new history out" shape).
//
// v1187 (sim v1186: the paired run crashed in json(), which described raw resource addresses after the engine had released some; and the
// alternate list blamed the other eye for targets written in the SAME render: draws were labelled with the engine frame counter at CPU record
// time, which the next Setup advances while job threads still record this render's late passes). The census now works in GPU order:
//  - record time (any thread): events go to the context's buffer, with the texture's size and format taken while it is bound (the status never
//    touches a resource again; a resource is only an address there);
//  - FinishCommandList moves a deferred context's events to its command list; ExecuteCommandList on the immediate context applies them in
//    order, labelled with the render they execute in (render_, advanced at each Present); immediate-context events apply at once;
//  - a read is cross-eye when the texture's last write executed in the previous render (one-view frames), or by the other eye's view in this
//    or the previous render (paired frames), and nothing of this render or view wrote it since.
// Writes it cannot see: Map/UpdateSubresource uploads (constant/dynamic data, streaming), a target written by an unhooked method.
//
// Fix (knob build/alt-eye-history, opt-in): a role = (pixel shader, SRV slot) the census saw as a cross-eye read on >= 32 renders with
// render target 0 shaped like the history. Per role two textures rotate by GPU order (like rig_mask.hpp's palette ring): before the role's
// draw, B (its output two executions ago = this eye's previous render) is copied into the history texture; after the draw B <- A,
// A <- render target 0. Needs the role's draw in consecutive renders (a gap leaves the engine's history for that render). The census runs
// while the fix still learns (600 renders) and stops then unless its own knob is on.
//
// v1181 paired stereo: the role's draws alternate eyes in GPU order (L R L R ...), so "two executions back" is this eye's previous render in
// both modes; only the CPU-side run bookkeeping differs. Knob build/eye-history enables the fix on paired frames. The reprojection inside
// those passes uses the view's own cb_previousviewprojectionmatrix or this view's motion vectors, so restoring the eye's own history makes
// the pass consistent.
#include <d3d11.h>
#include <MinHook.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
#include "render_pass_table.hpp"

namespace d2vr {
class CrossEyeHistory {
    struct Role;
public:
    inline static std::atomic<bool> census{false},fix{false},fix_paired{false};
    inline static std::atomic<std::uintptr_t> view{};                 // the alternate one-view (0: paired frames, inert)
    using FrameFn=std::uint32_t(*)() noexcept;inline static std::atomic<FrameFn> frame_fn{}; // the engine frame counter (the fix's bookkeeping)
    static constexpr unsigned min_frames=32,learn_frames=600;
    // A census event (record time). kind 0 write, 1 read. Size/format are of the bound texture (0 for a buffer).
    struct Event {std::uintptr_t r{},ps{};std::uint32_t w{},h{};std::uint16_t format{};std::int16_t pass{-1};std::uint8_t kind{},slot{};std::int8_t eye{-1};bool shaped{},buffer{};};
    class Scope {
    public:
        // paired_eye: the eye whose view `pass_view` is on a paired frame (ObjectMotion::eye_of_view), else -1
        Scope(ID3D11DeviceContext* c,std::uintptr_t pass_view,int pass,int paired_eye=-1) noexcept {
            const bool alternate=pass_view && pass_view==view.load(std::memory_order_relaxed);
            if(!c) return;
            const bool counting=census.load(std::memory_order_relaxed);
            if(!pass_view || (!alternate && paired_eye<0)) { // not an eye view's pass: its targets are still writes (shadow maps, depth-only draws)
                if(counting) {Events ev(c);targets(c,ev,-1,pass);}
                return;
            }
            const bool fixing=alternate?fix.load(std::memory_order_relaxed):fix_paired.load(std::memory_order_relaxed);
            const bool learning=fixing && learned_.load(std::memory_order_relaxed)<learn_frames;
            const bool watch=counting || learning;
            if(!(watch || fixing)) return;
            eye_=alternate?-1:paired_eye;
            const auto fn=frame_fn.load(std::memory_order_relaxed);frame_=fn?fn():0;
            ID3D11PixelShader* ps{};c->PSGetShader(&ps,nullptr,nullptr);if(ps) ps->Release(); // identity only
            if(!ps) {if(watch) {Events ev(c);targets(c,ev,eye_,pass);}return;}
            const int role=fixing && fn?find_role(ps):-1;
            if(!watch && role<0) return;
            ID3D11ShaderResourceView* srv[16]{};ID3D11RenderTargetView* rtv[8]{};ID3D11DepthStencilView* dsv{};
            c->PSGetShaderResources(0,16,srv);c->OMGetRenderTargets(8,rtv,&dsv);
            ID3D11Resource* out0{};if(rtv[0]) rtv[0]->GetResource(&out0);
            if(watch) {
                Events ev(c);
                for(unsigned s=0;s<16;++s) if(srv[s]) {
                    ID3D11Resource* r{};srv[s]->GetResource(&r);if(!r) continue;
                    Event e=describe_event(r);e.kind=1;e.slot=static_cast<std::uint8_t>(s);e.pass=static_cast<std::int16_t>(pass);e.eye=static_cast<std::int8_t>(eye_);
                    e.ps=reinterpret_cast<std::uintptr_t>(ps);e.shaped=out0 && same_shape(r,out0);ev.push(e);r->Release();
                }
                for(auto* v:rtv) if(v) {ID3D11Resource* r{};v->GetResource(&r);if(r) {ev.push(write_event(r,eye_,pass));r->Release();}}
                if(dsv) {ID3D11Resource* r{};dsv->GetResource(&r);if(r) {ev.push(write_event(r,eye_,pass));r->Release();}}
            }
            if(role>=0 && out0) begin_role(c,static_cast<unsigned>(role),srv[roles_[role].slot],out0);
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
        std::uint32_t frame_{};int eye_{-1};ID3D11DeviceContext* c_{};unsigned role_{};ID3D11Resource* out_{};
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
    // The current pass and its paired eye (dxgi_probe), for writes made outside draws.
    struct Writer {int eye{-1},pass{-1};};using WriterFn=Writer(*)() noexcept;
    inline static std::atomic<WriterFn> writer_fn{};
    // A write made outside a draw (copy destination, cleared view, compute UAV) on context `c`.
    static void note_resource(ID3D11DeviceContext* c,ID3D11Resource* r) noexcept {
        if(!c || !r || !census.load(std::memory_order_relaxed)) return;
        const auto wf=writer_fn.load(std::memory_order_relaxed);const Writer w=wf?wf():Writer{};
        Events ev(c);ev.push(write_event(r,w.eye,w.pass));other_writes_.fetch_add(1,std::memory_order_relaxed);
    }
    template<class View> static void note_view(ID3D11DeviceContext* c,View* v) noexcept {
        if(!c || !v || !census.load(std::memory_order_relaxed)) return;ID3D11Resource* r{};v->GetResource(&r);if(r) {note_resource(c,r);r->Release();}
    }
    static void note_dispatch(ID3D11DeviceContext* c) noexcept { // compute: the bound UAVs are writes
        if(!c || !census.load(std::memory_order_relaxed)) return;
        ID3D11UnorderedAccessView* u[8]{};c->CSGetUnorderedAccessViews(0,8,u);
        for(auto* v:u) if(v) {note_view(c,v);v->Release();}
    }
    // FinishCommandList (deferred context, after success) / ExecuteCommandList (immediate, before the original) / each Present.
    static void finished(ID3D11DeviceContext* c,ID3D11CommandList* list) noexcept {
        if(!c || !list) return;
        Buffer* b=buffer(c,false);if(!b || b->events.empty()) return;
        std::lock_guard<std::mutex> lock(lists_mutex_);
        if(lists_.size()>512) {lists_.clear();dropped_lists_.fetch_add(1,std::memory_order_relaxed);} // never executed: drop (diagnostic)
        try {auto& v=lists_[list];v.insert(v.end(),b->events.begin(),b->events.end());} catch(...) {}
        b->events.clear();
    }
    static void executing(ID3D11CommandList* list) noexcept {
        if(!list) return;
        std::vector<Event> events;
        {std::lock_guard<std::mutex> lock(lists_mutex_);const auto it=lists_.find(list);if(it==lists_.end()) return;events.swap(it->second);lists_.erase(it);}
        std::lock_guard<std::mutex> lock(apply_mutex_);
        for(const auto& e:events) apply(e);
    }
    static void present() noexcept {render_.fetch_add(1,std::memory_order_relaxed);frames_.fetch_add(1,std::memory_order_relaxed);learned_.fetch_add(1,std::memory_order_relaxed);}
    static void install(ID3D11Device* device) noexcept; // the copy and render-target/UAV clear hooks (slots 46, 47, 50, 51, 52)
    static void begin_epoch() noexcept {std::lock_guard<std::mutex> lock(mutex_);for(auto& r:roles_) {r.run=0;r.last=~0u;r.last_eye=-1;}}
    static std::string json() {
        std::ostringstream o;
        o<<"{\"census\":"<<(census.load()?"true":"false")<<",\"fix\":"<<(fix.load()?"true":"false")<<",\"fix_paired\":"<<(fix_paired.load()?"true":"false")<<",\"frames\":"<<frames_.load()<<",\"cross_reads\":"<<cross_reads_.load()
         <<",\"restores\":"<<restores_.load()<<",\"other_writes\":"<<other_writes_.load()<<",\"applied\":"<<applied_.load()<<",\"dropped_lists\":"<<dropped_lists_.load()<<",\"top\":[";
        // the 16 entries with the most cross-eye reads (only stored values: no resource is touched here)
        std::array<const Entry*,16> top{};
        for(const auto& e:table_) if(e.cross.load()) {
            const Entry* x=&e;
            for(auto& t:top) if(!t || x->cross.load()>t->cross.load()) std::swap(t,x);
        }
        bool first=true;
        for(const Entry* e:top) if(e) {
            const int pass=e->pass.load(),writer=e->writer_pass.load();
            o<<(first?"":",")<<"{\"texture\":\""<<std::hex<<e->key.load()<<"\",\"ps\":\""<<e->ps.load()<<std::dec<<"\",\"reads\":"<<e->cross.load()<<",\"frames\":"<<e->frames.load()
             <<",\"pass\":"<<pass<<",\"pass_name\":\""<<(pass>=0?render_pass_name(static_cast<unsigned>(pass)):"untagged")
             <<"\",\"writer_pass\":"<<writer<<",\"writer_name\":\""<<(writer>=0?render_pass_name(static_cast<unsigned>(writer)):"untagged")<<"\",\"writer_eye\":"<<e->writer.load()
             <<",\"slot\":"<<e->slot.load()<<",\"buffer\":"<<(e->buffer.load()?"true":"false")<<",\"size\":["<<e->w.load()<<','<<e->h.load()<<"],\"format\":"<<e->format.load()
             <<",\"shaped\":"<<(e->shaped.load()?"true":"false")<<'}';
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
    struct Entry {std::atomic<std::uintptr_t> key{};std::atomic<std::uint64_t> written{};std::atomic<std::uint64_t> flag_render{~0ull};std::atomic<int> writer{-1};
        std::atomic<std::uint64_t> cross{};std::atomic<unsigned> frames{},slot{},w{},h{},format{};
        std::atomic<int> pass{-1},writer_pass{-1};std::atomic<std::uintptr_t> ps{};std::atomic<bool> shaped{},buffer{};};
    struct Role {std::uintptr_t ps{};unsigned slot{};ID3D11Texture2D* a{};ID3D11Texture2D* b{};ID3D11Device* device{};D3D11_TEXTURE2D_DESC desc{};
        std::uint32_t last{~0u};int last_eye{-1};unsigned run{};std::uint64_t restored{};};
    struct Buffer {std::atomic<ID3D11DeviceContext*> context{};std::vector<Event> events;};
    static constexpr unsigned table_size=4096,max_roles=8,context_slots=64;
    inline static std::array<Entry,table_size> table_{};
    inline static std::array<Role,max_roles> roles_{};inline static std::atomic<unsigned> role_count_{};
    inline static std::mutex mutex_,lists_mutex_,apply_mutex_;
    inline static std::array<Buffer,context_slots> buffers_{};
    inline static std::unordered_map<ID3D11CommandList*,std::vector<Event>> lists_;
    inline static std::atomic<std::uint64_t> render_{1},frames_{},learned_{},cross_reads_{},restores_{},other_writes_{},applied_{},dropped_lists_{};
    // Events of one call: applied at once on the immediate context, else buffered for the context's command list.
    class Events {
    public:
        explicit Events(ID3D11DeviceContext* c) noexcept:immediate_(c->GetType()==D3D11_DEVICE_CONTEXT_IMMEDIATE),buffer_(immediate_?nullptr:buffer(c,true)) {}
        void push(const Event& e) noexcept {
            if(immediate_) {std::lock_guard<std::mutex> lock(apply_mutex_);apply(e);}
            else if(buffer_) try { // ponytail: a list never finished through the hook (hook missing) is dropped at 1M events, not kept
                if(buffer_->events.size()>=(1u<<20)) {buffer_->events.clear();dropped_lists_.fetch_add(1,std::memory_order_relaxed);}
                buffer_->events.push_back(e);} catch(...) {}
        }
    private:
        bool immediate_;Buffer* buffer_;
    };
    // A deferred context is used by one thread at a time (its events need no lock); a slot is claimed once per context.
    static Buffer* buffer(ID3D11DeviceContext* c,bool claim) noexcept {
        for(auto& b:buffers_) if(b.context.load(std::memory_order_acquire)==c) return &b;
        if(!claim) return nullptr;
        for(auto& b:buffers_) {ID3D11DeviceContext* none{};if(b.context.compare_exchange_strong(none,c,std::memory_order_acq_rel)) return &b;if(none==c) return &b;}
        return nullptr;
    }
    static Event describe_event(ID3D11Resource* r) noexcept {
        Event e{};e.r=reinterpret_cast<std::uintptr_t>(r);
        D3D11_RESOURCE_DIMENSION dim{};r->GetType(&dim);
        if(dim==D3D11_RESOURCE_DIMENSION_TEXTURE2D) {D3D11_TEXTURE2D_DESC d{};static_cast<ID3D11Texture2D*>(r)->GetDesc(&d);e.w=d.Width;e.h=d.Height;e.format=static_cast<std::uint16_t>(d.Format);}
        else if(dim==D3D11_RESOURCE_DIMENSION_BUFFER) {D3D11_BUFFER_DESC d{};static_cast<ID3D11Buffer*>(r)->GetDesc(&d);e.w=d.ByteWidth;e.buffer=true;}
        return e;
    }
    static Event write_event(ID3D11Resource* r,int eye,int pass) noexcept {
        Event e{};e.r=reinterpret_cast<std::uintptr_t>(r);e.kind=0;e.eye=static_cast<std::int8_t>(eye);e.pass=static_cast<std::int16_t>(pass);return e;
    }
    static void targets(ID3D11DeviceContext* c,Events& ev,int eye,int pass) noexcept {
        ID3D11RenderTargetView* rtv[8]{};ID3D11DepthStencilView* dsv{};c->OMGetRenderTargets(8,rtv,&dsv);
        for(auto* v:rtv) if(v) {ID3D11Resource* r{};v->GetResource(&r);if(r) {ev.push(write_event(r,eye,pass));r->Release();}v->Release();}
        if(dsv) {ID3D11Resource* r{};dsv->GetResource(&r);if(r) {ev.push(write_event(r,eye,pass));r->Release();}dsv->Release();}
    }
    // GPU order, under apply_mutex_ (the immediate context's thread, or a test).
    static void apply(const Event& ev) noexcept {
        applied_.fetch_add(1,std::memory_order_relaxed);
        Entry* e=entry(ev.r);if(!e) return;
        const std::uint64_t render=render_.load(std::memory_order_relaxed);
        if(ev.kind==0) {e->written.store(render,std::memory_order_relaxed);e->writer.store(ev.eye,std::memory_order_relaxed);e->writer_pass.store(ev.pass,std::memory_order_relaxed);return;}
        const std::uint64_t written=e->written.load(std::memory_order_relaxed);const int writer=e->writer.load(std::memory_order_relaxed);
        const bool cross=ev.eye<0?written!=0 && written+1==render:writer>=0 && writer!=ev.eye && written!=0 && (written==render || written+1==render);
        if(!cross) return;
        e->cross.fetch_add(1,std::memory_order_relaxed);e->pass.store(ev.pass,std::memory_order_relaxed);e->ps.store(ev.ps,std::memory_order_relaxed);
        e->slot.store(ev.slot,std::memory_order_relaxed);e->shaped.store(ev.shaped,std::memory_order_relaxed);e->buffer.store(ev.buffer,std::memory_order_relaxed);
        e->w.store(ev.w,std::memory_order_relaxed);e->h.store(ev.h,std::memory_order_relaxed);e->format.store(ev.format,std::memory_order_relaxed);
        if(e->flag_render.exchange(render,std::memory_order_relaxed)!=render) {
            const unsigned frames=e->frames.fetch_add(1,std::memory_order_relaxed)+1;
            if(ev.shaped && frames>=min_frames) add_role(ev.ps,ev.slot);
        }
        cross_reads_.fetch_add(1,std::memory_order_relaxed);
    }
    // the hooks (one per slot; a slot another owner already hooked stays unobserved)
    using CopyFn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11Resource*,ID3D11Resource*);
    using CopyRegionFn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11Resource*,UINT,UINT,UINT,UINT,ID3D11Resource*,UINT,const D3D11_BOX*);
    using ClearRtvFn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11RenderTargetView*,const FLOAT*);
    using ClearUavUFn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11UnorderedAccessView*,const UINT*);
    using ClearUavFFn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11UnorderedAccessView*,const FLOAT*);
    inline static std::array<CopyFn,2> copy_{};inline static std::array<CopyRegionFn,2> region_{};inline static std::array<ClearRtvFn,2> clear_rtv_{};
    inline static std::array<ClearUavUFn,2> clear_uavu_{};inline static std::array<ClearUavFFn,2> clear_uavf_{};inline static std::array<void*,10> targets_{};
    template<int I> static void STDMETHODCALLTYPE copy_hook(ID3D11DeviceContext* c,ID3D11Resource* d,ID3D11Resource* s) {note_resource(c,d);copy_[I](c,d,s);}
    template<int I> static void STDMETHODCALLTYPE region_hook(ID3D11DeviceContext* c,ID3D11Resource* d,UINT ds,UINT x,UINT y,UINT z,ID3D11Resource* s,UINT ss,const D3D11_BOX* b) {
        note_resource(c,d);region_[I](c,d,ds,x,y,z,s,ss,b);}
    template<int I> static void STDMETHODCALLTYPE clear_rtv_hook(ID3D11DeviceContext* c,ID3D11RenderTargetView* v,const FLOAT* k) {note_view(c,v);clear_rtv_[I](c,v,k);}
    template<int I> static void STDMETHODCALLTYPE clear_uavu_hook(ID3D11DeviceContext* c,ID3D11UnorderedAccessView* v,const UINT* k) {note_view(c,v);clear_uavu_[I](c,v,k);}
    template<int I> static void STDMETHODCALLTYPE clear_uavf_hook(ID3D11DeviceContext* c,ID3D11UnorderedAccessView* v,const FLOAT* k) {note_view(c,v);clear_uavf_[I](c,v,k);}
    template<class Fn> static bool hook_slot(ID3D11DeviceContext* context,unsigned slot,void* replacement,Fn& original) noexcept {
        void* target=(*reinterpret_cast<void***>(context))[slot];
        for(auto* t:targets_) if(t==target) return true; // already hooked by this class (shared implementation)
        if(MH_CreateHook(target,replacement,reinterpret_cast<void**>(&original))!=MH_OK) {original=nullptr;return false;}
        if(MH_EnableHook(target)!=MH_OK) {MH_RemoveHook(target);original=nullptr;return false;}
        for(auto& t:targets_) if(!t) {t=target;break;}
        return true;
    }
    // ponytail: a texture's address is its identity; a released and recreated texture at the same address inherits the old entry (diagnostic only)
    static Entry* entry(std::uintptr_t key) noexcept {
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
    static void add_role(std::uintptr_t key,unsigned slot) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
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
