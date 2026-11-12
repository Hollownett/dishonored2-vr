#pragma once
// Per-eye previous MVP for entity draws in paired stereo (both eyes drawn in one engine frame).
// An entity's motion vector comes from cb_previousmvpmatrix{x,y,z,w} in its per-instance cbuffer (VS b2, layout per
// material). The engine fills it from its cache of that draw's MVP in the LAST RENDERED view: in paired stereo the other
// eye's, so TAA/FSR/DLSS read the stereo disparity as motion and ghost every entity.
// v1103: a shader need not declare the previous rows at all. The held weapon's colour/forward vertex shader declares only
// cb_mvpmatrix{x,y,z,w} (the cache's 1,931 current-only model variants; docs/WEAPON-DRAW-PLACE-TEMPORAL.md "Round 3"); the
// proxy records those with 0xff in every previous byte. Such a draw is placed on its current rows and has no previous rows
// to rewrite: its weapon motion vector comes from the Z prepass, which declares both row sets and is placed too.
// Each draw is fixed when it is made, where the bindings are final (at the cb2 write they usually are not, v824):
// cb2 writes (Map/Unmap, UpdateSubresource) are shadowed per context; at a draw whose vertex shader has previous rows
// (reflected by the dinput8 proxy at CreateVertexShader, linux/proxy/vs_layouts.hpp: the game creates its shaders long
// before the mod loads), the eye and engine frame of its per-view cbuffer (VS b1, matched against the eye views'
// matrices) and PrevMvpTable give this eye's own previous rows. A corrected copy (one dynamic cbuffer per context,
// renamed by each DISCARD) is bound at b2 for that draw only.
// The draws are hooked by dxgi_probe (gpu_bindings: DrawIndexed, Draw and their instanced forms), each in a DrawScope.
// Knobs (build/): no-object-motion = not installed (read at the first Present), or hooks pass through when it appears
// later; object-motion-mode = 0 diagnostic only, 1 fix (default). Writes artifacts/object-motion.json.
#include <windows.h>
#include <d3d11_1.h>
#include <MinHook.h>
#include <intrin.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <new>
#include <sstream>
#include <string>
#include "weapon_draw_place.hpp"
#include "rig_mask.hpp"
#include "skin_motion.hpp"
#include "rig_target.hpp"
#include "buffer_views.hpp"
#include "alternate_taa_state.hpp"

namespace d2vr {
// Draws of the recent eye views keyed by their exact current rows -> the engine's original previous rows.
// L(n) carries MVP_R(n-1): the R(n-1) draw with those current rows carried MVP_L(n-1). R(n) carries MVP_L(n): the L(n)
// draw with those rows carried MVP_R(n-1). A replacement must itself be the current rows of a draw of this eye's last
// frame, so a cache that behaves otherwise is never "fixed" into another wrong matrix (it counts as unproven).
class PrevMvpTable {
public:
    static constexpr unsigned gens=8,slots=1u<<12,probes=64; // v1206: 8 (was 4): an eye's render 4 frames back (a counter skip) keeps its own slot
    enum Kind:unsigned {same,own_last,other_now,other_last,unproven,miss,kinds};
    static std::uint32_t hash(const float* m) noexcept {
        std::uint64_t h=0xcbf29ce484222325ull;
        for(int i=0;i<16;++i) {std::uint32_t w;std::memcpy(&w,m+i,4);h=(h^w)*0x100000001b3ull;}
        return static_cast<std::uint32_t>(h^(h>>29));
    }
    void insert(unsigned eye,std::uint32_t frame,const float* cur,const float* prev) noexcept {
        const std::uint32_t h=hash(cur),key=frame+1;const std::uint64_t want=std::uint64_t{key}<<32|h;
        Slot* t=table(eye,frame);
        for(unsigned i=0;i<probes;++i) {
            Slot& e=t[(h+i)&(slots-1)];std::uint64_t tag=e.tag.load(std::memory_order_acquire);
            if(tag==want && e.ready.load(std::memory_order_acquire)==key && !std::memcmp(e.cur,cur,64)) return; // another pass of the draw
            if(static_cast<std::uint32_t>(tag>>32)==key || !e.tag.compare_exchange_strong(tag,want,std::memory_order_acq_rel)) continue;
            std::memcpy(e.cur,cur,64);std::memcpy(e.prev,prev,64);
            e.ready.store(key,std::memory_order_release);return;
        }
    }
    bool find(unsigned eye,std::uint32_t frame,const float* cur,float* prev) const noexcept {
        const std::uint32_t h=hash(cur),key=frame+1;const std::uint64_t want=std::uint64_t{key}<<32|h;
        const Slot* t=table(eye,frame);
        for(unsigned i=0;i<probes;++i) {
            const Slot& e=t[(h+i)&(slots-1)];const std::uint64_t tag=e.tag.load(std::memory_order_acquire);
            if(static_cast<std::uint32_t>(tag>>32)!=key) return false; // the frame's probe chain ends at an older or empty slot
            if(tag!=want || e.ready.load(std::memory_order_acquire)!=key || std::memcmp(e.cur,cur,64)) continue;
            std::memcpy(prev,e.prev,64);
            return e.tag.load(std::memory_order_acquire)==want; // not reclaimed by a newer frame while copied
        }
        return false;
    }
    // Records this draw, then rewrites `prev` to this eye's own previous MVP when the chain proves it.
    // v1151: own_step = the engine frames back to this eye's own last render: 1 paired, 2 alternate-eye (one view per frame, eyes
    // alternating). In alternate mode the engine's previous rows are the OTHER eye's render one frame ago (Phase 0 H1, measured), and
    // that draw's own raw previous rows are this eye's of two frames ago: the same chain, other_last, own one frame further back.
    Kind fix(unsigned eye,std::uint32_t frame,const float* cur,float* prev,unsigned own_step=1) noexcept {
        insert(eye,frame,cur,prev);
        if(!std::memcmp(cur,prev,64)) return same; // an object's first frame: the engine reports no motion
        float got[16],own[16];
        if(find(eye,frame-own_step,prev,own)) return own_last; // already this eye's own (culled in the other eye's last view)
        const Kind kind=find(eye^1u,frame,prev,got)?other_now:find(eye^1u,frame-1,prev,got)?other_last:miss;
        if(kind==miss) return miss;
        if(!find(eye,frame-own_step,got,own)) return unproven;
        std::memcpy(prev,got,64);return kind;
    }
    // v1206 alternate mode: the chain to exactly `own_frame` (the draw's receipt: this eye's previous render, whose jitter the TAA pair names).
    // own: the raw rows are already that render's (`allow_own`; never for the rig, which every render draws). other: the raw rows are an
    // other-eye render's between own_frame and frame, whose own raw rows are own_frame's. Every hop is an exact 64-byte match under an exact
    // label (only content-certain draws are inserted: their labels are the receipt's), so the output is own_frame's rows or nothing (miss/unproven).
    Kind fix_render(unsigned eye,std::uint32_t frame,std::uint32_t own_frame,const float* cur,float* prev,bool allow_own) noexcept {
        insert(eye,frame,cur,prev);
        if(!std::memcmp(cur,prev,64)) return same;
        if(own_frame>=frame || frame-own_frame>gens) return miss; // older than the table keeps
        float got[16],own[16];
        if(allow_own && find(eye,own_frame,prev,own)) return own_last;
        bool hit=false;
        for(std::uint32_t f=frame-1;f!=own_frame && !hit;--f) hit=find(eye^1u,f,prev,got);
        if(!hit) return miss;
        if(!find(eye,own_frame,got,own)) return unproven;
        std::memcpy(prev,got,64);return other_last;
    }
    // v1207: the rig without a content view. The rig is drawn in every render, so its raw rows are the render before its own (an other-eye
    // render): the candidate render (eye, frame, own_frame) whose fix_render chain proves both hops (exact rows under exact labels) is the draw's
    // render. Returns the index of the one candidate that proves, -1 when none or more than one does. Nothing is inserted.
    struct RenderKey {unsigned eye;std::uint32_t frame,own_frame;};
    int prove_render(const RenderKey* r,int n,const float* prev) const noexcept {
        int found=-1;float got[16],own[16];
        for(int i=0;i<n;++i) {
            const RenderKey& k=r[i];if(k.own_frame>=k.frame || k.frame-k.own_frame>gens) continue;
            bool hit=false;for(std::uint32_t f=k.frame-1;f!=k.own_frame && !hit;--f) hit=find(k.eye^1u,f,prev,got);
            if(!hit || !find(k.eye,k.own_frame,got,own)) continue;
            if(found>=0) return -1;
            found=i;
        }
        return found;
    }
    // v1185 rig draws in alternate mode: the rig is drawn in every render, so its raw previous rows are always the other eye's render one
    // engine frame back, whose own raw previous rows are this eye's render two back. The chain is followed by content alone in a window of
    // labels around `frame` (both eyes, frames frame+1 .. frame-6): a draw recorded under the wrong eye/frame label (v1183 street: one eye's
    // rig draws all fell to own_last at the end of the run) still finds its rows. Both hops must match exactly (64 bytes).
    Kind fix_any(unsigned eye,std::uint32_t frame,const float* cur,float* prev) noexcept {
        insert(eye,frame,cur,prev);
        if(!std::memcmp(cur,prev,64)) return same;
        float got[16],own[16];
        const auto find_window=[&](const float* key,float* out) {
            for(int d=-1;d<=6;++d) for(unsigned e=0;e<2;++e) if(find(e,frame-static_cast<std::uint32_t>(d),key,out)) return true;
            return false;
        };
        if(!find_window(prev,got)) return miss;
        if(!find_window(got,own)) return unproven;
        std::memcpy(prev,got,64);return other_last;
    }
private:
    struct Slot {std::atomic<std::uint64_t> tag{};std::atomic<std::uint32_t> ready{};float cur[16]{},prev[16]{};};
    std::array<Slot,2*gens*slots> slots_{};
    Slot* table(unsigned eye,std::uint32_t frame) noexcept {return &slots_[(eye*gens+frame%gens)*slots];}
    const Slot* table(unsigned eye,std::uint32_t frame) const noexcept {return &slots_[(eye*gens+frame%gens)*slots];}
};

class ObjectMotion {
public:
    using Read=bool(*)(std::uintptr_t,void*,std::size_t);
    // v997/v998: the wide state census hooks slots 7/11/119 through these callbacks, so it never creates a second MinHook on this
    // owner's targets. The callbacks see the FINAL forwarded argument list. wide_cb_filter/wide_vs_filter return true only when
    // the census proved the call redundant and wide elision is on; the forward is then skipped, after this class's own
    // bookkeeping has already run (ObserveThenFilter below). wide_cb1_observer is count/invalidate only: CB1 is never elided.
    using WideCbFilter=bool (*)(ID3D11DeviceContext*,UINT,UINT,ID3D11Buffer* const*) noexcept;
    using WideCb1Observer=void (*)(ID3D11DeviceContext*,UINT,UINT,ID3D11Buffer* const*,const UINT*,const UINT*) noexcept;
    using WideVsFilter=bool (*)(ID3D11DeviceContext*,ID3D11VertexShader*,ID3D11ClassInstance* const*,UINT) noexcept;
    using WideDirectCbObserver=void (*)(ID3D11DeviceContext*,UINT,UINT,ID3D11Buffer* const*) noexcept;
    inline static std::atomic<WideCbFilter> wide_cb_filter{};
    inline static std::atomic<WideCb1Observer> wide_cb1_observer{};
    inline static std::atomic<WideVsFilter> wide_vs_filter{};
    // This class itself binds the pool buffer at VS b2 and restores the game's b2 through the original function (bind_pool and
    // DrawScope). Those calls bypass the hooked setter, so the census is told the exact pointer that is now bound; otherwise it
    // could think the game's previous buffer was still current and elide a bind that was actually needed.
    inline static std::atomic<WideDirectCbObserver> wide_direct_cb_observer{};
    // v998 order contract: ObjectMotion's per-shader/per-draw bookkeeping must observe every call and never depends on the call
    // being forwarded, so a filter that can elide runs after it. The model lab pins this ordering.
    template<class Observe,class Filter> static bool observe_then_filter(Observe&& observe,Filter&& filter) noexcept {
        observe();
        return filter();
    }
    static bool installed() noexcept {return installed_;}
    static constexpr unsigned absent=0xff,max_floats=1024; // shadowed per-instance cbuffers: up to 4 KB (v1158: was 1 KB; 649,663 oversize draws a v1157 run kept the other eye's rows)
    // One draw's rows by its shader's layout (linux/proxy/vs_layouts.hpp: byte k = float4 index of
    // cb_mvpmatrix{x,y,z,w}[k], then of cb_previousmvpmatrix; 0xff = not declared). A missing z row is `z`, the eye VP's
    // z row: with reverse-Z every MVP's z row is (0,0,0,near). gather_current needs the x, y and w current rows; gather
    // (both halves) also needs all four previous rows, which a current-only layout does not have.
    // v1101/v1103: the current rows alone (a depth/velocity-only or a colour/forward vertex-shader layout may not declare
    // cb_previousmvpmatrix; the proxy records it with 0xff in every previous byte). The placed current rows are identical
    // either way; the previous rows are derived from the publisher, not from this draw's cache.
    static bool gather_current(std::uint64_t layout,const float* f,UINT size,const float* z,float* cur) noexcept {
        for(unsigned k=0;k<4;++k) {
            const auto row=static_cast<unsigned>(layout>>(8*k)&255);float* out=cur+4*k;
            if(row==absent && (k&3)==2) std::memcpy(out,z,16);
            else if(row==absent || (row+1)*16>size) return false;
            else std::memcpy(out,f+4*row,16);
        }
        return true;
    }
    static bool gather(std::uint64_t layout,const float* f,UINT size,const float* z,float* cur,float* prev) noexcept {
        if(!gather_current(layout,f,size,z,cur)) return false;
        return gather_previous(layout,f,size,z,prev);
    }
    // The previous rows alone (v1101: the current rows were gathered before the weapon branch decided whether it needs them).
    static bool gather_previous(std::uint64_t layout,const float* f,UINT size,const float* z,float* prev) noexcept {
        for(unsigned k=4;k<8;++k) {
            const auto row=static_cast<unsigned>(layout>>(8*k)&255);float* out=prev+4*(k&3);
            if(row==absent && (k&3)==2) std::memcpy(out,z,16);
            else if(row==absent || (row+1)*16>size) return false;
            else std::memcpy(out,f+4*row,16);
        }
        return true;
    }
    // v1040: whether gather_previous can succeed for this layout and cb2 size (it never looks at the values). A current-only
    // layout (83% of the hooked draws in play: colour/forward shaders) cannot, so without weapon placement nothing can happen to it.
    static bool has_previous(std::uint64_t layout,UINT size) noexcept {
        for(unsigned k=4;k<8;++k) {
            const auto row=static_cast<unsigned>(layout>>(8*k)&255);
            if(row==absent) {if((k&3)!=2) return false;}
            else if((row+1)*16>size) return false;
        }
        return true;
    }
    // The previous rows into the draw's rows. A current-only layout has 0xff in every previous byte, so nothing is written;
    // a null `prev` (a current-only draw) is the same "nothing to rewrite".
    static void scatter(std::uint64_t layout,const float* prev,float* f) noexcept {
        if(!prev) return;
        for(unsigned k=4;k<8;++k) if(const auto row=static_cast<unsigned>(layout>>(8*k)&255);row!=absent) std::memcpy(f+4*row,prev+4*(k-4),16);
    }
    static void scatter_cur(std::uint64_t layout,const float* cur,float* f) noexcept { // v930: the current rows (a missing z row stays missing)
        for(unsigned k=0;k<4;++k) if(const auto row=static_cast<unsigned>(layout>>(8*k)&255);row!=absent) std::memcpy(f+4*row,cur+4*k,16);
    }
    // Row 3 of inverse(A), A = m (row-major) or its transpose: rows R = A*M with M affine give r*R = (0,0,0,1).
    static bool inverse_last_row(const float* m,bool transpose,float* r) noexcept {
        double a[4][5];
        for(int i=0;i<4;++i) {for(int j=0;j<4;++j) a[i][j]=transpose?m[i*4+j]:m[j*4+i];a[i][4]=i==3?1.0:0.0;}
        for(int c=0;c<4;++c) {
            int p=c;for(int i=c+1;i<4;++i) if(std::abs(a[i][c])>std::abs(a[p][c])) p=i;
            if(std::abs(a[p][c])<1e-12) return false;
            for(int j=0;j<5;++j) std::swap(a[p][j],a[c][j]);
            for(int i=0;i<4;++i) if(i!=c) {const double k=a[i][c]/a[c][c];for(int j=c;j<5;++j) a[i][j]-=k*a[c][j];}
        }
        for(int i=0;i<4;++i) r[i]=static_cast<float>(a[i][4]/a[i][i]);
        return true;
    }
    // Row-major 4x4: full inverse (false when singular) and product.
    static bool inverse(const float* m,float* out) noexcept {
        double a[4][8];
        for(int i=0;i<4;++i) for(int j=0;j<4;++j) {a[i][j]=m[i*4+j];a[i][4+j]=i==j?1.0:0.0;}
        for(int c=0;c<4;++c) {
            int p=c;for(int i=c+1;i<4;++i) if(std::abs(a[i][c])>std::abs(a[p][c])) p=i;
            if(std::abs(a[p][c])<1e-12) return false;
            for(int j=0;j<8;++j) std::swap(a[p][j],a[c][j]);
            const double d=a[c][c];for(int j=0;j<8;++j) a[c][j]/=d;
            for(int i=0;i<4;++i) if(i!=c) {const double k=a[i][c];for(int j=0;j<8;++j) a[i][j]-=k*a[c][j];}
        }
        for(int i=0;i<4;++i) for(int j=0;j<4;++j) out[i*4+j]=static_cast<float>(a[i][4+j]);
        return true;
    }
    static void mul(const float* a,const float* b,float* out) noexcept {
        for(int i=0;i<4;++i) for(int j=0;j<4;++j) {double t=0;for(int k=0;k<4;++k) t+=double(a[i*4+k])*b[k*4+j];out[i*4+j]=static_cast<float>(t);}
    }
    // Screen motion of the model origin (NDC/1000) between two MVPs; huge when the origin is not in front of the eye.
    static double origin_motion(const float* cur,const float* prev) noexcept {
        if(!(cur[15]>.01f && prev[15]>.01f)) return 1e30;
        return 1000.0*std::hypot(double(cur[3]/cur[15]-prev[3]/prev[15]),double(cur[7]/cur[15]-prev[7]/prev[15]));
    }
    static bool affine(const float* r,const float* block) noexcept {
        for(int j=0;j<4;++j) {
            double x=0,scale=0;
            for(int k=0;k<4;++k) {const double t=double(r[k])*block[4*k+j];x+=t;scale+=std::abs(t);}
            if(!(std::abs(x-(j==3?1.0:0.0))<=2e-5*scale+1e-5)) return false;
        }
        return true;
    }
    // v1106: the eye a pass view names (the visibility stamp), -1 when it is not one of the two eye views.
    static int eye_of_view(std::uintptr_t view) noexcept {
        if(view) for(unsigned e=0;e<2;++e) if(eye_views_[e].load(std::memory_order_relaxed)==view) return static_cast<int>(e);
        return -1;
    }
    // v1151 alternate-eye (one engine view renders the eyes in turn): that view (0 = not alternate or a paired frame), and the logical eye
    // of the engine frame it holds now (the alternate_taa receipt: -1 unknown). Its records go to that eye; own previous = two frames back.
    using AlternateEye=int (*)(std::uintptr_t view,std::uint32_t frame) noexcept;
    static void note_alternate_view(std::uintptr_t view) noexcept {alt_view_.store(view,std::memory_order_relaxed);}
    // v1200: at the end of an alternate Setup (dxgi_probe, next to the rig target publish): the record of this render from the view's FINAL
    // matrices (the per-view-write snapshot can predate them, v1160), so the content candidates hold exactly the VP the draws are built with
    // v1206: from the receipt alternate_taa published at that Setup's end (v1200 resolved it, which needs admission: it never was at Setup end,
    // so records came from mid-frame snapshots that a next Setup could already have changed). The only source of alternate records: final
    // matrices, the render's eye/frame/display, and the previous render (frame, VP, jitter) its TAA pair names.
    static void note_alternate_render(const alternate_taa::Receipt& r) noexcept {
        if(!on() || (r.key.eye!=0 && r.key.eye!=1)) return;
        const unsigned eye=static_cast<unsigned>(r.key.eye);
        EyeView v{};v.frame=r.key.native_counter;
        std::memcpy(v.m[0],r.current.matrices[1].data(),64);std::memcpy(v.m[2],r.current.matrices[2].data(),64);
        if(!complete_record(v)) return;
        v.receipt=true;v.has_prev=!r.reset && r.previous_counter!=r.key.native_counter;v.prev_frame=r.previous_counter;v.display=r.key.display;v.phase=r.phase;
        std::memcpy(v.prev_vp,r.previous.matrices[2].data(),64);std::memcpy(v.jitter,r.current.jitter.data(),8);
        std::lock_guard<std::mutex> lock(views_mutex_);
        float src[2]{};bool src_ok=false; // the jitter of the render the rows will come from (its own receipt record)
        for(const auto& o:eye_records_[eye]) if(o.valid && o.receipt && o.frame==v.prev_frame) {std::memcpy(src,o.jitter,8);src_ok=true;}
        store(eye,v);
        render_open(eye,v,r.previous.jitter.data(),src_ok?src:nullptr,r.reset);
    }
    static void set_alternate_eye(AlternateEye f) noexcept {alt_eye_.store(f,std::memory_order_relaxed);}
    static bool alternate() noexcept {return alt_view_.load(std::memory_order_relaxed)!=0;}
    // From the engine's view Setup (entry: the view still holds its last frame's matrices). 0 = not a paired eye.
    static void note_eye_view(unsigned eye,std::uintptr_t view) noexcept {
        if(eye>1) return;
        eye_views_[eye].store(view,std::memory_order_relaxed);
        if(view && on()) {std::lock_guard<std::mutex> lock(views_mutex_);snapshot(eye,view);}
    }
    // v1151 status: the alternate-mode draws by kind (same,own_last,other_now,other_last,unproven,miss), camera-only fallbacks, untagged.
    static std::string alternate_status_json() {
        std::string s="{\"active\":"+std::string(alternate()?"true":"false")+",\"kinds\":[";
        for(unsigned k=0;k<PrevMvpTable::kinds;++k) s+=(k?",":"")+std::to_string(alt_kinds_[k].load(std::memory_order_relaxed));
        return s+"],\"resnapped\":"+std::to_string(alt_resnapped_.load())+",\"camera_only\":"+std::to_string(alt_camera_.load())+",\"rig_draws\":"+std::to_string(alt_rig_.load())+",\"rig_own\":"+std::to_string(alt_rig_own_.load())+",\"untagged\":"+std::to_string(alt_untagged_.load())
            +",\"view_src\":["+[] {std::string o;for(unsigned i=0;i<view_sources;++i) o+=(i?",":"")+std::to_string(alt_view_src_[i].load());return o;}()
            +"],\"newest_why\":["+std::to_string(alt_newest_why_[0].load())+','+std::to_string(alt_newest_why_[1].load())+','+std::to_string(alt_newest_why_[2].load())
            +"],\"view_use\":["+[] {std::string o;for(unsigned i=0;i<view_sources;++i) {o+=i?",[":"[";for(unsigned u=0;u<use_count;++u) o+=(u?",":"")+std::to_string(alt_view_use_[i][u].load());o+="]";}return o;}()+"]"
            +",\"weapon_place\":["+std::to_string(alt_place_placed_.load())+','+std::to_string(alt_place_own_prev_.load())+','+std::to_string(alt_place_no_prev_.load())+','+std::to_string(alt_place_no_view_.load())+"]"
            +",\"view_mode\":\""+(alt_view_heuristic.load()?"heuristic":"certain")+"\",\"certainty\":{"+[] {std::string o; // v1199: per consumer [certain, fell back]
                static constexpr const char* names[cert_consumers]{"rig_target","placement","previous_rows","held_mask"};
                for(unsigned k=0;k<cert_consumers;++k) {o+=std::string(k?",":"")+'"'+names[k]+"\":["+std::to_string(alt_cert_[k][0].load())+','+std::to_string(alt_cert_[k][1].load())+','+std::to_string(alt_cert_[k][2].load())+']';
                    o+=",\""+std::string(names[k])+"_fallback_why\":[";for(unsigned w=0;w<5;++w) o+=(w?",":"")+std::to_string(alt_cert_why_[k][w].load());o+=']';}
                return o;}()+",\"hypothesis_check\":["+std::to_string(alt_hyp_[0].load())+','+std::to_string(alt_hyp_[1].load())+"],\"rig_zero_why\":["+std::to_string(rig_zero_why_[0].load())+','+std::to_string(rig_zero_why_[1].load())+','+std::to_string(rig_zero_why_[2].load())+','+std::to_string(rig_zero_why_[3].load())+','+std::to_string(rig_zero_why_[4].load())+','+std::to_string(rig_zero_why_[5].load())+"],\"uncertain_zero\":"+std::to_string(alt_uncertain_.load())+",\"raw_rows\":{\"draws\":"+std::to_string(raw_total_.load())+",\"renders_over_16\":"+std::to_string(raw_renders_.load())+",\"worst\":"+std::to_string(raw_worst_.load())+",\"worst_frame\":"+std::to_string(raw_worst_frame_.load())+"}"
            +",\"content\":"+content_json()+"},\"renders\":"+renders_json()+",\"probed\":"+probed_json()+",\"fallbacks\":"+fallback_json()+",\"draw_views\":"+draw_views_json()
            +",\"wild_forensics\":["+[] {std::lock_guard<std::mutex> lock(pin_mutex_);std::string o;for(unsigned i=0;i<forensics_n_;++i) o+=(i?",":"")+forensics_[i];return o;}()+"]"
            +",\"rig_wild_renders\":"+std::to_string(RigMask::wild_renders_.load())
            +",\"rig_guard\":{\"renders\":"+std::to_string(RigMask::wild_renders_.load())+",\"px\":"+std::to_string(RigMask::wild_px_.load())+",\"draws\":"+std::to_string(RigMask::guard_draws_.load())+",\"failures\":"+std::to_string(RigMask::guard_failures_.load())+"}}";
    }
    // v1206: one render's row (the probe adds it to its dump of that render), "null" when the ring no longer holds it
    static void pin(std::uint64_t display) noexcept { // v1207: the probe dumps this render
        std::lock_guard<std::mutex> lock(pin_mutex_);if(pinned_n_<pinned_.size()) {pinned_[pinned_n_]=display;pinned_json_[pinned_n_].clear();++pinned_n_;}
    }
    static std::string render_json(std::uint64_t display) {
        const RenderRow& w=render_rows_[display%render_ring];
        return w.display.load(std::memory_order_acquire)==display?row_json(w):std::string("null");
    }
    // v950: from each eye's native TAA dispatch, its per-view globals buffer (CS b1, 800 bytes), which the eye's scene draws
    // bind at VS b1. Two per eye (a view may alternate buffers); one seen for both eyes names neither.
    static void note_taa_b1(unsigned eye,const void* b) noexcept {
        if(eye>1 || !b) return;
        auto& own=taa_b1_[eye];
        if(own[0].load(std::memory_order_relaxed)==b || own[1].load(std::memory_order_relaxed)==b) return;
        own[1].store(own[0].load(std::memory_order_relaxed),std::memory_order_relaxed);own[0].store(b,std::memory_order_relaxed);
    }
    static void note_mv_texture(const void* r) noexcept {note_mv_target(r);} // v1208: the eye TAA's motion-vector input (dxgi_probe TAA dispatch)
    static int b1_eye(const void* b) noexcept { // -1: no eye's
        int eye=-1;
        if(b) for(int e=0;e<2;++e) for(const auto& p:taa_b1_[static_cast<unsigned>(e)]) if(p.load(std::memory_order_relaxed)==b) eye=eye==-1 || eye==e?e:-2;
        return eye<0?-1:eye;
    }
    // Every Present: installs once, then polls the knobs and writes the statistics every two seconds.
    static bool install(ID3D11Device* device,const std::filesystem::path& output,const std::filesystem::path& knobs,Read read) noexcept {
        if(installed_) {poll();return true;}
        if(tried_ || !device || !read) return false;
        tried_=true;std::error_code error;
        if(std::filesystem::exists(knobs/L"no-object-motion",error)) return false; // on by default since v828 (sim-proven)
        output_=output;knobs_=knobs;read_=read;
        if(const HMODULE proxy=GetModuleHandleW(L"dinput8.dll")) { // the game folder's proxy, loaded at process start
            layout_=reinterpret_cast<LayoutFn>(reinterpret_cast<void*>(GetProcAddress(proxy,"D2vrVertexShaderLayout")));
            proxy_stats_=reinterpret_cast<StatsFn>(reinterpret_cast<void*>(GetProcAddress(proxy,"D2vrVertexShaderStats")));
        }
        ID3D11DeviceContext* deferred{};
        if(layout_ && SUCCEEDED(device->CreateDeferredContext(0,&deferred))) {
            ID3D11DeviceContext* immediate{};device->GetImmediateContext(&immediate);
            void** d=*reinterpret_cast<void***>(deferred);void** i=immediate?*reinterpret_cast<void***>(immediate):nullptr;
            installed_=hook2(d,i,14,&map_hook<0>,&map_hook<1>,map_) && hook2(d,i,15,&unmap_hook<0>,&unmap_hook<1>,unmap_) &&
                hook2(d,i,48,&update_hook<0>,&update_hook<1>,update_) && hook2(d,i,7,&set_cb_hook<0>,&set_cb_hook<1>,set_cb_) &&
                hook2(d,i,11,&set_vs_hook<0>,&set_vs_hook<1>,set_vs_);
            ID3D11DeviceContext1* d1{};ID3D11DeviceContext1* i1{}; // ranged binds and UpdateSubresource1
            if(installed_ && SUCCEEDED(deferred->QueryInterface(IID_PPV_ARGS(&d1)))) {
                if(immediate) immediate->QueryInterface(IID_PPV_ARGS(&i1));
                void** dv=*reinterpret_cast<void***>(d1);void** iv=i1?*reinterpret_cast<void***>(i1):nullptr;
                hook2(dv,iv,116,&update1_hook<0>,&update1_hook<1>,update1_);hook2(dv,iv,119,&set_cb1_hook<0>,&set_cb1_hook<1>,set_cb1_);
                if(i1) i1->Release();
                d1->Release();
            }
            if(immediate) immediate->Release();
            deferred->Release();
            if(!installed_) disable();
        }
        poll();
        return installed_;
    }
    // v930: the held weapons' placement is wanted (mod installed, not opted out) / the probe is on / a probe line (any thread).
    static bool weapon_wanted() noexcept {return installed_ && mode_.load(std::memory_order_relaxed)>=1 &&
        (weapon_mode_.load(std::memory_order_relaxed)>=1 || (alt_weapon_place.load(std::memory_order_relaxed) && alternate()));}
    // v1191 (knob build/alt-weapon-place): held weapons and props placed at their draws on one-view frames too (Place::place_one_view)
    inline static std::atomic<bool> alt_weapon_place{false};
    inline static std::atomic<std::uint64_t> alt_place_placed_{},alt_place_own_prev_{},alt_place_no_prev_{},alt_place_no_view_{};
    // v1194: where a one-view draw's view came from: 0 own newest write, 1 own older write of its bound b1, 2/3 another context's write of its
    // b1 (of the newest frame / older), 4 the newest view anywhere (the pre-v1193 fallback); and per source, what used it (view_use)
    inline static thread_local unsigned tl_view_src_{};
    // v1199: 6 certain (the draw binds the b1 its own pass job wrote, buffer_views.hpp view_certain), 7 uncertain (no view: consumers fall back)
    static constexpr unsigned view_sources=8;
    inline static std::array<std::atomic<std::uint64_t>,view_sources> alt_view_src_{};
    inline static std::atomic<bool> alt_view_heuristic{false}; // v1199 knob build/alt-view-heuristic: the v1197 timing rules instead (A/B only)
    // v1199 status certainty: per consumer, draws it handled with a certain view (or, rig target: both hypothesis slices retargeted) / fell back
    enum CertConsumer : unsigned {cert_rig_target,cert_placement,cert_previous_rows,cert_held_mask,cert_consumers};
    // v1201: [certain, fell back, not applicable]; per consumer the fallbacks by the draw's content outcome (tl_content_why_: 0 not tested,
    // 1 ambiguous across eyes, 2 none fits, 3 another pass's view, 4 no candidates)
    enum CertOutcome : unsigned {cert_certain,cert_fallback,cert_na,cert_outcomes};
    inline static std::array<std::array<std::atomic<std::uint64_t>,cert_outcomes>,cert_consumers> alt_cert_{};
    inline static std::array<std::array<std::atomic<std::uint64_t>,5>,cert_consumers> alt_cert_why_{};
    inline static thread_local unsigned tl_content_why_{};
    static void certainty(CertConsumer k,CertOutcome o) noexcept {
        alt_cert_[k][o].fetch_add(1,std::memory_order_relaxed);
        if(o==cert_fallback) alt_cert_why_[k][tl_content_why_<5?tl_content_why_:0].fetch_add(1,std::memory_order_relaxed);
    }
    static void certainty(CertConsumer k,bool certain) noexcept {certainty(k,certain?cert_certain:cert_fallback);}
    // v1199: certain draws whose eye's latest published Setup VP equals their own view's VP (agree) / not (the rig target's hypothesis slice
    // for that eye would have used another render's E: must stay 0)
    inline static std::array<std::atomic<std::uint64_t>,2> alt_hyp_{};
    inline static std::array<std::atomic<std::uint64_t>,6> rig_zero_why_{}; // v1204: why a rig draw got zero motion; v1206 + no content view, history reset
    inline static std::atomic<std::uint64_t> alt_uncertain_{}; // v1206: draws with previous rows and no content view: zero motion (were: chained under a timing label)
    inline static std::array<std::atomic<std::uint64_t>,3> alt_newest_why_{}; // v1196: no b1 bound / own view of another buffer, b1 unknown / no own view, b1 unknown
    enum ViewUse : unsigned {use_held_mask,use_rig_target,use_rig_motion,use_place,use_fix,use_camera_only,use_near,use_count};
    inline static std::array<std::array<std::atomic<std::uint64_t>,use_count>,view_sources> alt_view_use_{};
    static void view_use(ViewUse u) noexcept {alt_view_use_[tl_view_src_][u].fetch_add(1,std::memory_order_relaxed);}
    // v1101: the placement gate (the knob build/no-weapon-draw-place sets weapon_mode 0; object-motion-mode 0 is diagnostic only).
    static bool weapon_draw_on(int weapon_mode,int mode) noexcept {return weapon_mode>=1 && mode>=1;}
    // v1140 (user, 2026-11-10: held props lag and wobble; the v1132 headset status had weapon_mode 0 and held_draw_placed [0,0]): the held-prop
    // exact draw (v1067-v1074) rides this placement, which v1019 made opt-in for weapons, so it never ran on a default install. A held thing
    // is now placed whenever the publisher placed one this render (held_placing_); weapons stay opt-in. build/no-held-draw-place turns it off.
    static bool held_wanted() noexcept {return installed_ && mode_.load(std::memory_order_relaxed)>=1;}
    static void set_held_placing(bool on) noexcept {held_placing_.store(on,std::memory_order_relaxed);}
    static int placing_mode(int weapon_mode,bool held) noexcept {return weapon_mode>=1 || held?1:0;}
    static int placing_mode() noexcept {return placing_mode(weapon_mode_.load(std::memory_order_relaxed),held_placing_.load(std::memory_order_relaxed));}
    // v1101: the render pass a draw hook runs in. `view` is the pass job's view (item+0x70, src/render_pass_map.hpp): one of the eye
    // views for every pass that renders that eye (the exact eye, no cache certificate), a light/other view for a pass that does not.
    struct DrawPass {int pass{-1};std::uintptr_t view{0};std::uint64_t job{0};}; // v1199: job = RenderPassMap::current_job()
    using PassSource=DrawPass(*)() noexcept;
    static void set_pass_source(PassSource source) noexcept {pass_source_.store(source,std::memory_order_relaxed);}
    static void set_pass_name(const char* (*name)(unsigned)) noexcept {pass_name_.store(name,std::memory_order_relaxed);} // status: pass id -> ARK_RENDERPASS_* name
    // Per hand the draws that reached the weapon placement and those it placed, by pass; the reason of every rejection stage.
    static std::string weapon_status_json(); // {"published":[l,r],"placed":[l,r],"skip":[{},{}],"passes":[{"id","name","near","matched","placed","left","right"}]}
    static bool weapon_probe() noexcept {return weapon_probe_on_.load(std::memory_order_relaxed);}
    static void probe_line(const char* text) noexcept {
        std::lock_guard<std::mutex> lock(probe_mutex_);
        if(probe_text_.size()<=(24u<<20)) probe_text_.append(text);
    }
    static void disable() noexcept {
        mode_=-1;
        for(unsigned i=0;i<target_count_;++i) MH_DisableHook(targets_[i]);
        Sleep(50); // the hook bodies are microseconds long
    }
private:
    enum Counter:unsigned {layout_draws,motion,no_shadow,untagged,no_rows,guard_fail,fixed_l,fixed_r,pool_fail,shadow_writes,oversize,
        view_writes,view_vp,view_v,view_none,learned_cb2,learned_view,engine_sum_l,engine_sum_r,out_sum_l,out_sum_r,engine_big_l,
        engine_big_r,out_big_l,out_big_r,corrected_l,corrected_r,camera_l,camera_r,
        weapon_ticks,weapon_stage,weapon_stage1,weapon_stage2,weapon_stage3,weapon_stage4,weapon_stage5,weapon_stage6,weapon_stage7,weapon_stage8,
        weapon_stage9, // v1017: one counter per weapon_draw::Stage (placed_pass was landing in weapon_eye0)
        weapon_eye0,weapon_eye1,weapon_label_wrong,weapon_engine_sum_l,weapon_engine_sum_r,
        weapon_out_sum_l,weapon_out_sum_r,weapon_probe_lines,weapon_near,weapon_placed_current_only, // v931/v1101/v1103
        own_b1_agree,own_b1_disagree,own_b1_unknown,latest_b1_agree,latest_b1_disagree,latest_b1_unknown,b1_retagged,b1_lost, // v950
        ticks,kind_l,kind_r=kind_l+PrevMvpTable::kinds,near_l=kind_r+PrevMvpTable::kinds,
        near_r=near_l+PrevMvpTable::kinds,counters=near_r+PrevMvpTable::kinds};
    static_assert(weapon_eye0==weapon_stage+weapon_draw::stages,"one weapon counter per weapon_draw::Stage");
    // b2 of shaders with previous rows (shadowed); b1 of any draw (per-view candidates: the mesh shaders' own b1 is an
    // 800-byte buffer they do not read and the game never rewrites; the VP-carrying per-view constants are b1 of other
    // shaders); a candidate that never matches an eye view in 16 writes is ignored.
    enum Learn:unsigned {cb2_kind=1,view_kind=2,ignored_kind=3};
    static constexpr unsigned matched=1u<<30; // misses of a candidate that matched once: never ignored
    using LayoutFn=std::uint64_t(WINAPI*)(void*);
    using StatsFn=void(WINAPI*)(std::uint64_t*);
    using Map=HRESULT(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11Resource*,UINT,D3D11_MAP,UINT,D3D11_MAPPED_SUBRESOURCE*);
    using Unmap=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11Resource*,UINT);
    using Update=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11Resource*,UINT,const D3D11_BOX*,const void*,UINT,UINT);
    using Update1=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext1*,ID3D11Resource*,UINT,const D3D11_BOX*,const void*,UINT,UINT,UINT);
    using SetVs=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11VertexShader*,ID3D11ClassInstance* const*,UINT);
    using SetCb=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT,ID3D11Buffer* const*);
    using SetCb1=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext1*,UINT,UINT,ID3D11Buffer* const*,const UINT*,const UINT*);
    // Inverse last rows of the eye's VP (both storage orders), its z row, and the view correction for unproven draws.
    struct View {std::uint32_t frame{};unsigned eye{};float inv[2][4]{};float z[4]{};float corr[16]{},camera[16]{};bool corr_ok{},camera_ok{};float vp[16]{}; // v1183: vp (held mask)
        std::uint32_t prev_frame{};bool has_prev{},receipt{};std::uint64_t display{~0ull};}; // v1206: from the render's receipt (alternate)
    struct EyeView {std::uint32_t frame{};bool valid{};float m[4][16]{};float inv[2][4]{}; // view, view^T, VP, VP^T
        bool receipt{},has_prev{};std::uint32_t prev_frame{},phase{};std::uint64_t display{~0ull};float prev_vp[16]{},jitter[2]{};}; // v1206: alternate receipt record
    inline static BufferViews<View> buffer_views_{}; // v1193: per b1 buffer, its last classified view
    struct Shadow {const void* buffer{};UINT size{};float data[max_floats]{};};
    struct ContextState { // a context is used by one thread at a time: no locks
        unsigned type{}; // 0 deferred, 1 immediate: whose original methods this context takes
        void* vs{};std::uint64_t layout{};ID3D11Buffer* cb[2]{}; // the bound vertex shader, its rows, b1 and b2 as the game bound them
        ID3D11Buffer* pool{};bool pool_failed{};
        View view{};bool view_valid{};std::uint32_t view_frame{~0u}; // the last per-view write recorded here, and the newest frame then
        const void* view_buffer{}; // v1194: the buffer that write went to (the context's own record-time snapshot of that b1)
        std::uint64_t view_job{};  // v1199: the pass job (command list) that write was recorded in (RenderPassMap::current_job, 0 outside one)
        // v1158: 256 slots (was 64, direct-mapped: two live cb2 buffers on one slot evict each other, the draw then counts no_shadow)
        std::array<Shadow,256> shadows{};
        Shadow& shadow(const void* b) noexcept {return shadows[(reinterpret_cast<std::uintptr_t>(b)>>4)*0x9e3779b97f4a7c15ull>>56];}
    };
    struct Latest {std::atomic<std::uint32_t> seq{};View view{};}; // an eye's newest classified view (seqlock: odd while written)
    struct B1Seen {std::atomic<const void*> buffer{};std::atomic<std::uint64_t> n[3]{};}; // v950 diagnostics: draws by own-context tag L/R, fallback
    struct Learned {std::atomic<const void*> buffer{};std::atomic<unsigned> kind{},misses{};};
    struct Pending {ID3D11DeviceContext* context{};ID3D11Resource* resource{};void* data{};Learned* entry{};};
    static constexpr unsigned context_slots=64,learned_slots=1u<<14;

    inline static PrevMvpTable table_;
    inline static std::array<std::atomic<ID3D11DeviceContext*>,context_slots> context_keys_{};
    inline static std::array<std::atomic<ContextState*>,context_slots> context_states_{};
    inline static std::array<Learned,learned_slots> learned_{};
    inline static std::array<Latest,2> latest_{}; // v950: per eye (was one, the newest anywhere)
    inline static std::atomic<unsigned> latest_eye_{}; // the eye classified last at the newest frame: the old fallback
    inline static std::array<std::array<std::atomic<const void*>,2>,2> taa_b1_{}; // v950: per eye, the CS b1 its native TAA binds
    inline static std::atomic<bool> b1_eye_on_{true}; // build/no-object-motion-b1-eye: the old tag
    inline static std::array<B1Seen,8> b1_seen_{};
    inline static std::atomic<std::uint32_t> newest_frame_{};
    inline static std::mutex views_mutex_;
    inline static std::array<std::atomic<std::uintptr_t>,2> eye_views_{};
    inline static std::uint32_t alt_snapped_frame_{~0u}; // v1153: under views_mutex_
    inline static std::uint32_t alt_retry_frame_{~0u};inline static unsigned alt_retries_{}; // v1160: under views_mutex_
    inline static std::atomic<std::uint64_t> alt_resnapped_{};
    inline static std::array<std::atomic<std::uint64_t>,17> oversize_kb_{}; // v1158: oversize cb2 writes by size in KB (16 = 16 KB or more)
    inline static std::atomic<std::uintptr_t> alt_view_{};inline static std::atomic<AlternateEye> alt_eye_{}; // v1151
    inline static std::array<std::atomic<std::uint64_t>,PrevMvpTable::kinds> alt_kinds_{};inline static std::atomic<std::uint64_t> alt_camera_{},alt_untagged_{},alt_rig_{},alt_rig_own_{};
    inline static std::array<std::array<EyeView,4>,2> eye_records_{};
    inline static std::array<std::atomic<std::uint32_t>,2> newest_eye_frame_{}; // the frame of that eye's newest recorded view (lock-free for the draw hooks)
    inline static std::array<std::array<std::atomic<float>,4>,2> eye_z_{};       // that view's VP z row (0,0,0,near), for a layout without a z row
    inline static std::atomic<unsigned> vp_at_{~0u}; // float index of the VP in the per-view constants, as last matched
    inline static std::array<std::atomic<std::uint64_t>,counters> stats_{};
    inline static std::array<std::array<std::atomic<std::uint32_t>,64>,2> unmatched_size_{}; // per eye: unmatched draws by cb2 float4s
    inline static thread_local std::array<std::uint64_t,counters> local_{};
    inline static thread_local unsigned local_n_{};
    inline static thread_local std::array<Pending,4> pending_{};
    inline static thread_local unsigned pending_next_{};
    inline static thread_local ID3D11DeviceContext* tl_context_{};
    inline static thread_local ContextState* tl_state_{};
    inline static std::atomic<int> mode_{-1}; // -1 until installed: note_eye_view runs from the first eye setup either way
    // v930: held weapons placed at the draw (weapon_draw_place.hpp). 1 on (default), 0 off (build/no-weapon-draw-place); the probe
    // (build/weapon-draw-probe) logs the model matrices of near draws and the published placements to artifacts/weapon-draw-probe.csv.
    inline static std::atomic<int> weapon_mode_{1};
    inline static std::atomic<bool> held_placing_{};   // v1140: the newest render published a held prop for placement
    inline static std::atomic<bool> weapon_probe_on_{};
    // v1101: the draw's render pass (src/render_pass_map.hpp). No pass map installed: the v931 certificate path runs unchanged.
    inline static std::atomic<PassSource> pass_source_{};
    inline static std::atomic<const char* (*)(unsigned)> pass_name_{};
    // Status: draws that reached the weapon placement ("near": a held weapon's part is at most near_w deep) and the ones placed, by pass
    // (index render_pass_count = a draw outside any pass), and per hand. Skipped = near - placed; the stage counters say why.
    inline static constexpr unsigned pass_slots=65;
    inline static std::array<std::atomic<std::uint64_t>,pass_slots> weapon_pass_near_{},weapon_pass_matched_{},weapon_pass_placed_{};
    inline static std::array<std::array<std::atomic<std::uint64_t>,pass_slots>,2> weapon_pass_side_{};
    inline static std::array<std::atomic<std::uint64_t>,2> weapon_placed_side_{},weapon_alt_side_{};   // alt (v1072): placed from the previous read F
    inline static std::array<std::array<std::atomic<std::uint64_t>,weapon_draw::stages>,2> weapon_skip_side_{}; // per hand by rejection stage
    struct ViewsEntry {std::atomic<std::uint32_t> ready{};std::uint32_t previous0{},previous1{};weapon_draw::Views views{};};
    inline static std::array<ViewsEntry,8> views_cache_{};
    inline static std::mutex views_cache_mutex_;
    inline static std::mutex probe_mutex_;
    inline static std::string probe_text_;
    inline static std::array<std::uint32_t,2> probe_frame_{~0u,~0u};inline static std::array<unsigned,2> probe_in_frame_{};
    inline static std::array<void*,32> targets_{};inline static unsigned target_count_{};
    inline static std::array<Map,2> map_{};inline static std::array<Unmap,2> unmap_{};inline static std::array<Update,2> update_{};
    inline static std::array<Update1,2> update1_{};inline static std::array<SetVs,2> set_vs_{};
    inline static std::array<SetCb,2> set_cb_{};inline static std::array<SetCb1,2> set_cb1_{};
    inline static LayoutFn layout_{};inline static StatsFn proxy_stats_{};
    inline static Read read_{};inline static std::filesystem::path output_,knobs_;
    inline static bool installed_{},tried_{};
    inline static std::uint64_t poll_frames_{},last_poll_qpc_{},last_poll_ticks_{};

    static bool on() noexcept {
        return mode_.load(std::memory_order_relaxed)>=0 && ((eye_views_[0].load(std::memory_order_relaxed) && eye_views_[1].load(std::memory_order_relaxed)) ||
            alt_view_.load(std::memory_order_relaxed));
    }
    static void count(Counter c,std::uint64_t n=1) noexcept {local_[c]+=n;if(!(++local_n_&255)) flush();}
    static void flush() noexcept {for(unsigned i=0;i<counters;++i) if(local_[i]) {stats_[i].fetch_add(local_[i],std::memory_order_relaxed);local_[i]=0;}}
    template<class Fn> static bool hook(void* target,Fn replacement,Fn& original) noexcept {
        if(target_count_>=targets_.size() || MH_CreateHook(target,reinterpret_cast<void*>(replacement),reinterpret_cast<void**>(&original))!=MH_OK) return false;
        if(MH_EnableHook(target)!=MH_OK) {MH_RemoveHook(target);original=nullptr;return false;}
        targets_[target_count_++]=target;return true;
    }
    // The deferred context's method, and the immediate context's when it is another function (else the hook is shared).
    template<class T> struct same {using type=T;}; // the hooks' type comes from the originals
    template<class Fn> static bool hook2(void** deferred,void** immediate,unsigned slot,typename same<Fn>::type d,typename same<Fn>::type i,std::array<Fn,2>& original) noexcept {
        if(!hook(deferred[slot],d,original[0])) return false;
        if(immediate && immediate[slot]==deferred[slot]) original[1]=original[0];
        else if(immediate) hook(immediate[slot],i,original[1]);
        return true;
    }

    static ContextState* state(ID3D11DeviceContext* c) noexcept {
        if(tl_context_==c) return tl_state_;
        const auto h=static_cast<unsigned>((reinterpret_cast<std::uintptr_t>(c)>>4)*0x9e3779b97f4a7c15ull>>58);
        for(unsigned i=0;i<context_slots;++i) {
            const unsigned at=(h+i)&(context_slots-1);
            ID3D11DeviceContext* key=context_keys_[at].load(std::memory_order_acquire);
            if(!key && context_keys_[at].compare_exchange_strong(key,c,std::memory_order_acq_rel)) {
                auto* s=new(std::nothrow) ContextState{};
                if(s) s->type=c->GetType()==D3D11_DEVICE_CONTEXT_IMMEDIATE?1u:0u;
                context_states_[at].store(s,std::memory_order_release);key=c;
            }
            if(key!=c) continue;
            ContextState* s=context_states_[at].load(std::memory_order_acquire);
            if(s) {tl_context_=c;tl_state_=s;}
            return s;
        }
        return nullptr;
    }
    // Buffers seen at draws of shaders with previous rows: b2 (per-instance rows, shadowed) and b1 (per-view, classified).
    static unsigned learned_hash(const void* b) noexcept {return static_cast<unsigned>((reinterpret_cast<std::uintptr_t>(b)>>4)*0x9e3779b97f4a7c15ull>>50);}
    static Learned* learned(const void* b) noexcept {
        const unsigned h=learned_hash(b);
        for(unsigned i=0;i<32;++i) {
            auto& e=learned_[(h+i)&(learned_slots-1)];const void* key=e.buffer.load(std::memory_order_acquire);
            if(key==b) return &e;
            if(!key) return nullptr;
        }
        return nullptr;
    }
    static Learned* shadowed(const void* b) noexcept { // a learned buffer whose writes are kept
        Learned* e=learned(b);
        return e && e->kind.load(std::memory_order_relaxed)!=ignored_kind?e:nullptr;
    }
    static void learn(const void* b,unsigned kind) noexcept {
        const unsigned h=learned_hash(b);
        for(unsigned i=0;i<32;++i) {
            auto& e=learned_[(h+i)&(learned_slots-1)];const void* key=e.buffer.load(std::memory_order_acquire);
            if(!key && e.buffer.compare_exchange_strong(key,b,std::memory_order_acq_rel)) key=b;
            if(key!=b) continue;
            if(e.kind.exchange(kind,std::memory_order_relaxed)!=kind) {e.misses.store(0,std::memory_order_relaxed);count(kind==cb2_kind?learned_cb2:learned_view);}
            return;
        }
    }

    // v1183 held mask: the engine frames (render model +0x1860, 15 floats as weapon_draw::model_of reads them) of what the hands hold now
    // (weapons, dual-wield copies, held props), published at each one-view render's eye begin with the previous publish kept (the engine
    // composes some per-instance constants from the previous read, weapon_draw_place.hpp v1072), and a reach per frame (0.8 m weapons:
    // their parts sit at offsets with the same orientation; 0.5 m held things). Matched like the placement does: the draw's model rows
    // (inverse(VP) * rows) have the frame's orientation within Place::axes_tol and its origin within the reach.
public:
    struct HeldFrames {std::array<weapon_draw::Mat,6> f{};std::array<double,6> reach{};unsigned n{};};
    inline static std::atomic<bool> held_mask{false};
    static void publish_held_frames(const HeldFrames& now) noexcept {std::lock_guard<std::mutex> lock(held_mutex_);held_[1]=held_[0];held_[0]=now;}
    static std::uint64_t held_matches() noexcept {return held_matches_.load(std::memory_order_relaxed);}
private:
    inline static std::mutex held_mutex_;inline static std::array<HeldFrames,2> held_{};inline static std::atomic<std::uint64_t> held_matches_{};
    static bool held_match(const View& view,const float* cur) noexcept {return held_match(view.vp,cur);}
    static bool held_match(const float* view_vp,const float* cur) noexcept {
        weapon_draw::Mat vp{},inv{};for(int i=0;i<16;++i) vp[static_cast<std::size_t>(i)]=view_vp[i];
        if(!weapon_draw::inverse(vp,inv)) return false;
        const auto rows=weapon_draw::Place::model_rows(inv.data(),cur);
        const double tol=weapon_draw::Place::axes_tol.load(std::memory_order_relaxed);
        std::lock_guard<std::mutex> lock(held_mutex_);
        for(const auto& g:held_) for(unsigned i=0;i<g.n;++i)
            if(weapon_draw::Place::same_orientation(rows,g.f[i],tol) && weapon_draw::Place::origin_distance(rows,g.f[i])<=g.reach[i]) {
                held_matches_.fetch_add(1,std::memory_order_relaxed);return true;
            }
        return false;
    }
    // v1190: the draw's retargeted rows (prepare) and their binding for RigMask's target replay (same thread, right after the engine's draw)
    // v1199: per hypothesis slice h (rig_mask.hpp): the drawn rows retargeted with E of eye h's latest Setup
    inline static thread_local float tl_target_rows_[2][16]{};inline static thread_local ContextState* tl_target_s_{};inline static thread_local const Shadow* tl_target_sh_{};
    // The rig target's two hypotheses for this draw's drawn rows (the engine's, or the placed ones). `view`: the draw's certain view, if any,
    // only to check the hypothesis of its eye against it (status hypothesis_check). Both slices retargeted: certain for the target.
    static void target_dual(ContextState& s,const Shadow* sh,const float* rows,const View* view,bool counted=true) noexcept {
        bool ok[2]{};
        for(unsigned h=0;h<2;++h) {
            rig_target::M4 e{};float vp[16]{};
            ok[h]=rig_target::book().latest_of(h,e,vp);
            if(ok[h]) rig_target::apply(e,rows,tl_target_rows_[h]);
            if(counted && ok[h] && view && view->eye==h) alt_hyp_[std::memcmp(vp,view->vp,64)?1:0].fetch_add(1,std::memory_order_relaxed);
        }
        tl_target_s_=&s;tl_target_sh_=sh;RigMask::mark_target(ok[0],ok[1]);if(counted) certainty(cert_rig_target,ok[0] && ok[1]);
    }
public:
    static bool target_bind(ID3D11DeviceContext* c,unsigned slice) noexcept {
        ContextState* s=tl_target_s_;const Shadow* sh=tl_target_sh_;
        if(!s || !sh || sh->buffer!=s->cb[1] || slice>1) return false;
        return bind_pool(c,*s,*sh,tl_target_rows_[slice],nullptr); // previous rows as the engine wrote them (a depth-only replay never reads them)
    }
    static void target_restore(ID3D11DeviceContext* c) noexcept { // the engine's per-instance buffer again (DrawScope restores it too)
        ContextState* s=tl_target_s_;if(!s) return;
        if(const auto observe=wide_direct_cb_observer.load(std::memory_order_relaxed)) observe(c,2,1,&s->cb[1]);
        set_cb_[s->type](c,2,1,&s->cb[1]);tl_target_s_=nullptr;tl_target_sh_=nullptr;
    }
private:
    // Eye views: snapshots of view (+0x7450) and view-projection (+0x7510) at engine frame +0x77B4.
    static void snapshot(unsigned eye,std::uintptr_t view) noexcept {
        if(!read_) return;
        EyeView v{};
        if(!read_(view+0x77b4,&v.frame,4) || !read_(view+0x7450,v.m[0],64) || !read_(view+0x7510,v.m[2],64) || !complete_record(v)) return;
        store(eye,v);
    }
    static bool complete_record(EyeView& v) noexcept { // transposes and inverse rows from m[0] (view) and m[2] (VP)
        for(int r=0;r<4;++r) for(int c=0;c<4;++c) {v.m[1][c*4+r]=v.m[0][r*4+c];v.m[3][c*4+r]=v.m[2][r*4+c];}
        if(!inverse_last_row(v.m[2],false,v.inv[0]) || !inverse_last_row(v.m[2],true,v.inv[1])) return false;
        return v.valid=true;
    }
    static void store(unsigned eye,const EyeView& v) noexcept { // under views_mutex_
        auto& ring=eye_records_[eye];auto* slot=&ring[0];
        for(auto& old:ring) {if(old.valid && old.frame==v.frame) {if(old.receipt && !v.receipt) return; slot=&old;break;} if(!old.valid || old.frame<slot->frame) slot=&old;}
        *slot=v;records_gen_.fetch_add(1,std::memory_order_release); // v1200: the content candidates are rebuilt
        std::uint32_t newest=newest_eye_frame_[eye].load(std::memory_order_relaxed);
        if(v.frame>=newest) { // the draw hooks read these without the views lock
            newest_eye_frame_[eye].store(v.frame,std::memory_order_release);
            for(unsigned k=0;k<4;++k) eye_z_[eye][k].store(v.m[2][8+k],std::memory_order_relaxed);
        }
    }
    static bool near16(const float* a,const float* b) noexcept {
        for(int i=0;i<16;++i) if(!(std::abs(a[i]-b[i])<=1e-5f*(1.f+std::abs(b[i])))) return false;
        return true;
    }
    // A per-view constants write: the eye view and engine frame its matrices belong to (VP first: its jitter makes the
    // frame unique even with the head still). The VP's offset is remembered, so most writes compare one float4 block.
    static bool classify(const float* f,UINT size,View& out) noexcept {
        std::lock_guard<std::mutex> lock(views_mutex_);
        for(unsigned eye=0;eye<2;++eye) if(const auto view=eye_views_[eye].load(std::memory_order_relaxed)) snapshot(eye,view);
        // v1206: alternate records come only from receipts (note_alternate_render); a snapshot here could catch the view inside the next
        // Setup (its stamp still this frame's, matrices already seeded) and file that under this render
        int best_eye=-1,best_rank=-1;const EyeView* best=nullptr;UINT best_at=0;
        const auto scan=[&](UINT o,int first) {
            for(unsigned eye=0;eye<2;++eye) for(const auto& r:eye_records_[eye]) if(r.valid) for(int m=first;m<4;++m) {
                const int rank=m>=2?1:0;
                if(rank<best_rank || (rank==best_rank && best && r.frame<=best->frame) || !near16(f+o,r.m[m])) continue;
                best=&r;best_eye=static_cast<int>(eye);best_rank=rank;best_at=o;
            }
        };
        const UINT n=size/4,at=vp_at_.load(std::memory_order_relaxed);
        if(at<n && at+16<=n) scan(at,2);
        if(best_rank<1) {
            for(UINT o=0;o+16<=n;o+=4) scan(o,0);
            if(best_rank==1) vp_at_.store(best_at,std::memory_order_relaxed);
        }
        count(best_rank==1?view_vp:best_rank==0?view_v:view_none);
        if(!best) return false;
        fill_view(static_cast<unsigned>(best_eye),*best,out);
        return true;
    }
    // v1200: content identification (buffer_views.hpp rows_shear). Candidates: every recorded eye view (both eyes, ~4 renders each), rebuilt
    // per thread when a snapshot changes them (records_gen_). Each candidate carries its full View (fill_view) and its projection's x/y terms.
    struct Candidate {View view{};ProjectionXY p{};};
    inline static std::atomic<std::uint64_t> records_gen_{1};
    inline static thread_local std::uint64_t tl_cand_gen_{};inline static thread_local std::array<Candidate,8> tl_cand_{};inline static thread_local int tl_cand_n_{};
    static void refresh_candidates() noexcept {
        const auto gen=records_gen_.load(std::memory_order_acquire);if(gen==tl_cand_gen_) return;
        std::lock_guard<std::mutex> lock(views_mutex_);tl_cand_n_=0;
        for(unsigned eye=0;eye<2;++eye) for(const auto& r:eye_records_[eye]) if(r.valid && tl_cand_n_<8) {
            // v1203: only the newest two renders of each eye (span 4: v1202 made any two renders within 3 differ; the 4-deep ring reached
            // renders 7 apart whose jitter can repeat: the v1202 cross-eye ties)
            unsigned newer=0;for(const auto& o:eye_records_[eye]) if(o.valid && o.frame>r.frame) ++newer;
            if(newer>=2) continue;
            Candidate& c=tl_cand_[static_cast<std::size_t>(tl_cand_n_)];c.p=projection_of(r.m[2]);if(!c.p.ok) continue;
            corrections(eye,r,c.view);++tl_cand_n_;
        }
        tl_cand_gen_=gen;
    }
    // Tolerances (knob build/alt-view-content-tol: "<fit> <ratio> <floor>"), outcome counters and score histograms (log10 decades -9..0).
    // v1203: fit 1e-5 (was 1e-4): a model skew of a jitter shear's size (1e-4..1e-3) could cancel the wrong render's shear; skewed models
    // go to the Gram test (previous rows) instead
    inline static std::atomic<double> content_fit_{1e-4},content_ratio_{4.0},content_floor_{2e-6};
    enum ContentOutcome : unsigned {content_certain,content_ambiguous,content_none_fit,content_other_pass,content_no_candidates,content_tie_same_eye,content_certain_gram,content_outcomes};
    inline static std::array<std::atomic<std::uint64_t>,content_outcomes> content_outcome_{};
    inline static std::array<std::array<std::atomic<std::uint64_t>,11>,2> content_hist_{}; // [0] best, [1] second best
    inline static std::array<std::atomic<std::uint64_t>,2> content_tick_{}; // tick: picked the newest of its eye / an older one
    // v1201: content vs the v1199 same-job b1 view: agree, other eye, same eye and content's render newer, same eye and content's older
    inline static std::array<std::atomic<std::uint64_t>,4> content_vs_job_{};
    // v1202: the draws no candidate fits, named: by kind (0 rig, 1 near non-rig, 2 far with previous rows) x best-shear decade, and the
    // engine frames they occurred in (first, last) to tell a start-up burst from a steady class
    inline static thread_local unsigned tl_draw_kind_{};
    inline static thread_local bool tl_view_heuristic_{}; // v1205: the view current_view returned is the v1197 heuristic's, not content's
    // v1205: per render (engine frame), draws left with the engine's raw previous rows (other eye's) though they had previous rows
    inline static std::atomic<std::uint32_t> raw_frame_{~0u};inline static std::atomic<std::uint64_t> raw_now_{},raw_renders_{},raw_worst_{},raw_total_{};
    inline static std::atomic<std::uint32_t> raw_worst_frame_{};
    static void note_raw() noexcept {
        raw_total_.fetch_add(1,std::memory_order_relaxed);
        const auto f=newest_frame_.load(std::memory_order_relaxed);auto at=raw_frame_.load(std::memory_order_relaxed);
        if(f!=at && raw_frame_.compare_exchange_strong(at,f)) { // the previous render's tally is final
            const auto n=raw_now_.exchange(0,std::memory_order_relaxed);
            if(n>raw_threshold) raw_renders_.fetch_add(1,std::memory_order_relaxed);
            if(n>raw_worst_.load(std::memory_order_relaxed)) {raw_worst_.store(n,std::memory_order_relaxed);raw_worst_frame_.store(at,std::memory_order_relaxed);}
        }
        raw_now_.fetch_add(1,std::memory_order_relaxed);
    }
    static constexpr std::uint64_t raw_threshold=16;
    // v1206: per alternate render (keyed by its receipt's display number, as the probe prints it) the previous-row outcomes of its draws
    enum RenderCount : unsigned {rr_same,rr_own_last,rr_other_now,rr_other_last,rr_unproven,rr_miss,rr_camera_only,rr_raw,rr_uncertain,
        rr_own_d2,rr_own_d4,rr_own_dother,rr_rig_own,rr_rig_zero,rr_rig_palette_miss,rr_rig_zero_unnamed,rr_count};
    static_assert(static_cast<unsigned>(rr_miss)==static_cast<unsigned>(PrevMvpTable::miss),"render counts start with the table's kinds");
    static constexpr unsigned render_ring=64;
    struct RenderRow {std::atomic<std::uint64_t> display{~0ull};std::uint32_t frame{},prev_frame{},phase{};unsigned eye{};bool reset{},has_prev{},src_known{};
        float jitter[2]{},prev_jitter[2]{},src_jitter[2]{};std::array<std::atomic<std::uint32_t>,rr_count> n{};};
    inline static std::array<RenderRow,render_ring> render_rows_{};
    inline static std::atomic<std::uint64_t> newest_display_{~0ull}; // uncertain draws are filed under the newest render (approximate)
    // v1207: the probe pins the renders it dumps (at its TAA dispatch the scene draws are often not processed yet): when the ring reuses a pinned
    // render's slot, its final row is kept as text (status "probed")
    inline static std::array<std::uint64_t,64> pinned_{};inline static std::array<std::string,64> pinned_json_{};inline static unsigned pinned_n_{};
    static void render_open(unsigned eye,const EyeView& v,const float* prev_jitter,const float* src_jitter,bool reset) noexcept { // under views_mutex_
        RenderRow& w=render_rows_[v.display%render_ring];
        if(w.display.load(std::memory_order_relaxed)==v.display) return; // a duplicate receipt
        {std::lock_guard<std::mutex> lock(pin_mutex_);const auto old=w.display.load(std::memory_order_relaxed);
         if(old!=~0ull) for(unsigned i=0;i<pinned_n_;++i) if(pinned_[i]==old && pinned_json_[i].empty()) try {pinned_json_[i]=row_json(w);} catch(...) {}
         // v1209 forensics: a wild render (census), or the same eye's render just before one, is kept with its rig draws
         std::uint32_t m[3]{},m2[3]{};const bool wild=old!=~0ull && RigMask::mask_counts(old,m) && m[2];const bool before=old!=~0ull && !wild && RigMask::mask_counts(old+2,m2) && m2[2];
         if((wild || before) && forensics_n_<forensics_.size()) try {forensics_[forensics_n_++]=std::string("{\"wild\":")+(wild?"true":"false")+",\"row\":"+row_json(w)+",\"rig_draws\":"+rig_draws_json(old)+'}';} catch(...) {}}
        w.display.store(~0ull,std::memory_order_release);
        for(auto& k:w.n) k.store(0,std::memory_order_relaxed);
        w.frame=v.frame;w.prev_frame=v.prev_frame;w.phase=v.phase;w.eye=eye;w.reset=reset;w.has_prev=v.has_prev;w.src_known=src_jitter!=nullptr;
        std::memcpy(w.jitter,v.jitter,8);std::memcpy(w.prev_jitter,prev_jitter,8);if(src_jitter) std::memcpy(w.src_jitter,src_jitter,8);
        w.display.store(v.display,std::memory_order_release);newest_display_.store(v.display,std::memory_order_relaxed);
    }
    inline static std::mutex pin_mutex_;
    static std::string probed_json() {
        std::lock_guard<std::mutex> lock(pin_mutex_);std::string o="[";
        for(unsigned i=0;i<pinned_n_;++i) {const std::string row=pinned_json_[i].empty()?render_json(pinned_[i]):pinned_json_[i];o+=(i?",":"")+row;}
        return o+"]";
    }
    // v1207: fallback draws (uncertain previous rows, unnamed rig) by render pass and by whether a render target is bound (a depth-only pass
    // writes no motion vectors: zero motion there is harmless)
    inline static std::array<std::array<std::atomic<std::uint64_t>,2>,65> fallback_pass_{};inline static std::array<std::atomic<std::uint64_t>,3> fallback_rt_{};
    inline static std::atomic<std::uint64_t> rig_na_{},fallback_other_view_{}; // v1210: MV-texture writers of another engine view (n/a)
    inline static std::atomic<std::uint64_t> rig_chained_{};
    // v1208: true when one of the draw's render targets is the motion-vector texture the eye's TAA reads (note_mv_target, the TAA dispatch's
    // motion input; unknown yet: true). rt status: [no target, a target but not the MV texture, the MV texture]; by_pass counts only those.
    inline static std::array<std::atomic<const void*>,4> mv_target_{};inline static std::atomic<unsigned> mv_next_{}; // the engine may rotate them
    static void note_mv_target(const void* r) noexcept {
        if(!r) return;for(const auto& m:mv_target_) if(m.load(std::memory_order_relaxed)==r) return;
        mv_target_[mv_next_.fetch_add(1,std::memory_order_relaxed)%mv_target_.size()].store(r,std::memory_order_relaxed);
    }
    static bool note_pass_fallback(ID3D11DeviceContext* c,bool rig) noexcept {
        ID3D11RenderTargetView* r[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};c->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,r,nullptr);
        bool any=false,writes=!mv_target_[0].load(std::memory_order_relaxed);
        for(auto* x:r) if(x) {any=true;ID3D11Resource* res{};x->GetResource(&res);if(res) {for(const auto& m:mv_target_) writes=writes || m.load(std::memory_order_relaxed)==res;res->Release();}x->Release();}
        // v1210: an MV-texture writer whose pass renders another engine view than the alternate one-view (no eye render: a companion/flat
        // window view, a probe view) feeds no eye's TAA through this draw: not applicable by view identity, its signature kept (draw_views)
        if(any && writes && alternate()) if(const auto source=pass_source_.load(std::memory_order_relaxed)) if(const auto v=source().view;v && v!=alt_view_.load(std::memory_order_relaxed)) {
            fallback_other_view_.fetch_add(1,std::memory_order_relaxed);draw_view_note(c,2);return false;}
        fallback_rt_[!any?0:writes?2:1].fetch_add(1,std::memory_order_relaxed);
        if(!any) writes=false;
        if(writes) draw_view_note(c,0);
        if(writes) {int pass=-1;if(const auto source=pass_source_.load(std::memory_order_relaxed)) pass=source().pass;
            fallback_pass_[pass>=0 && pass<64?static_cast<unsigned>(pass):64u][rig?1:0].fetch_add(1,std::memory_order_relaxed);}
        return writes;
    }
    static std::string fallback_json() {
        const auto name=pass_name_.load(std::memory_order_relaxed);
        std::string o="{\"rt\":["+std::to_string(fallback_rt_[0].load())+','+std::to_string(fallback_rt_[1].load())+','+std::to_string(fallback_rt_[2].load())+"],\"other_view_na\":"+std::to_string(fallback_other_view_.load())+",\"rig_na\":"+std::to_string(rig_na_.load())
            +",\"rig_chained\":"+std::to_string(rig_chained_.load())+",\"by_pass_mv\":{";
        bool first=true;
        for(unsigned p=0;p<65;++p) if(const auto a=fallback_pass_[p][0].load(),b=fallback_pass_[p][1].load();a || b) {
            const char* nm=p<64 && name?name(p):nullptr;o+=std::string(first?"":",")+'"'+(p==64?std::string("none"):nm?std::string(nm):std::to_string(p))+"\":["+std::to_string(a)+','+std::to_string(b)+']';first=false;}
        return o+"}}";
    }
    static bool rig_render_by_chain(const float* prev,View& out) noexcept { // v1207: see PrevMvpTable::prove_render
        std::lock_guard<std::mutex> lock(views_mutex_);
        PrevMvpTable::RenderKey keys[8];const EyeView* recs[8];unsigned eyes[8];int n=0;
        for(unsigned e=0;e<2;++e) for(const auto& r:eye_records_[e]) if(r.valid && r.receipt && r.has_prev && n<8) {keys[n]={e,r.frame,r.prev_frame};recs[n]=&r;eyes[n]=e;++n;}
        const int i=table_.prove_render(keys,n,prev);
        if(i<0) return false;
        corrections(eyes[i],*recs[i],out);return true;
    }
    // v1209 forensics: per render up to 16 rig draws (model, t1 buffer, slot index/tag/buffer bound at t0, row hashes, chain kind), frozen into
    // the status when the census finds that render wild (and the same eye's render before a wild one, for a side-by-side)
    struct RigDraw {std::uint32_t model,slot,tag,kind,own,chained,foreign,vs_w,vs_h;std::uint64_t t1,slot_buffer,t0,cur,raw,used;};
    struct RigDraws {std::atomic<std::uint64_t> display{~0ull};std::atomic<unsigned> n{};std::array<RigDraw,16> d{};};
    inline static std::array<RigDraws,render_ring> rig_draws_{};
    inline static std::array<std::string,8> forensics_{};inline static unsigned forensics_n_{};
    static std::uint64_t hash64(const float* m) noexcept {std::uint64_t h=0xcbf29ce484222325ull;for(int i=0;i<16;++i) {std::uint32_t w;std::memcpy(&w,m+i,4);h=(h^w)*0x100000001b3ull;}return h;}
    static void rig_record(ID3D11DeviceContext* c,std::uint64_t display,const float* cur,const float* raw,const float* used,unsigned kind,bool own,bool chained) noexcept {
        if(display==~0ull) return;
        RigDraws& w=rig_draws_[display%render_ring];
        auto at=w.display.load(std::memory_order_acquire);
        if(at!=display) {if(!w.display.compare_exchange_strong(at,display)) return;w.n.store(0,std::memory_order_release);}
        const unsigned i=w.n.fetch_add(1,std::memory_order_acq_rel);if(i>=w.d.size()) return;
        RigDraw& r=w.d[i];r={};r.model=RigMask::draw_model();r.slot=RigMask::tl_bound_slot_;r.tag=RigMask::tl_bound_tag_;r.slot_buffer=RigMask::tl_bound_buffer_;
        r.kind=kind;r.own=own;r.chained=chained;r.foreign=RigMask::draw_foreign_t0();r.t1=RigMask::draw_current();
        ID3D11ShaderResourceView* t0{};c->VSGetShaderResources(0,1,&t0);if(t0) {ID3D11Resource* res{};t0->GetResource(&res);r.t0=reinterpret_cast<std::uintptr_t>(res);if(res) res->Release();t0->Release();}
        UINT n=1;D3D11_VIEWPORT vp{};c->RSGetViewports(&n,&vp);r.vs_w=static_cast<std::uint32_t>(vp.Width);r.vs_h=static_cast<std::uint32_t>(vp.Height);
        r.cur=hash64(cur);r.raw=hash64(raw);r.used=hash64(used);
    }
    static std::string rig_draws_json(std::uint64_t display) {
        const RigDraws& w=rig_draws_[display%render_ring];if(w.display.load()!=display) return "[]";
        std::ostringstream o;o<<std::hex;o<<'[';const unsigned n=std::min<unsigned>(w.n.load(),static_cast<unsigned>(w.d.size()));
        for(unsigned i=0;i<n;++i) {const auto& r=w.d[i];o<<(i?",":"")<<"{\"model\":"<<std::dec<<r.model<<",\"slot\":"<<static_cast<int>(r.slot)<<",\"tag\":"<<static_cast<int>(r.tag)<<",\"kind\":"<<r.kind
            <<",\"own\":"<<r.own<<",\"chained\":"<<r.chained<<",\"foreign_t0\":"<<r.foreign<<",\"viewport\":["<<r.vs_w<<','<<r.vs_h<<"]"<<std::hex
            <<",\"t1\":\""<<r.t1<<"\",\"slot_buffer\":\""<<r.slot_buffer<<"\",\"t0\":\""<<r.t0<<"\",\"cur\":\""<<r.cur<<"\",\"raw\":\""<<r.raw<<"\",\"used\":\""<<r.used<<"\"}";}
        o<<']';return o.str();
    }
    // v1209: the views draws record into, by state signature: fallback draws that write an MV texture vs certain rig draws
    // v1210: n[2] = MV-texture writers of another engine view (not applicable); `which` = the view's identity when first seen (2 the alternate
    // one-view, 0/1 a paired eye view, -1 another view) and its view-projection (+0x7510: row norms of the 3x3 = the projection's x/y scales)
    struct DrawView {std::atomic<std::uint64_t> key{};std::uint32_t vp_w{},vp_h{};std::uintptr_t view{},dsv{},rtv0{};float near_w{};std::array<std::atomic<std::uint64_t>,3> n{};int which{-1};float vp[16]{};bool vp_ok{};};
    inline static std::array<DrawView,16> draw_views_{};
    static void draw_view_note(ID3D11DeviceContext* c,unsigned certain) noexcept { // certain: 0 fallback, 1 certain, 2 another view (n/a)
        UINT nv=1;D3D11_VIEWPORT vp{};c->RSGetViewports(&nv,&vp);
        ID3D11RenderTargetView* r0{};ID3D11DepthStencilView* d{};c->OMGetRenderTargets(1,&r0,&d);
        const auto res=[](ID3D11View* v) {std::uintptr_t a=0;if(v) {ID3D11Resource* r{};v->GetResource(&r);a=reinterpret_cast<std::uintptr_t>(r);if(r) r->Release();v->Release();}return a;};
        const std::uintptr_t rt=res(r0),ds=res(d);std::uintptr_t view=0;if(const auto source=pass_source_.load(std::memory_order_relaxed)) view=source().view;
        const std::uint64_t key=(static_cast<std::uint64_t>(vp.Width)<<48)^(static_cast<std::uint64_t>(vp.Height)<<32)^ds^(rt<<7)^(view<<13)|1;
        for(auto& e:draw_views_) {auto k=e.key.load(std::memory_order_acquire);
            if(!k && e.key.compare_exchange_strong(k,key)) {e.vp_w=static_cast<std::uint32_t>(vp.Width);e.vp_h=static_cast<std::uint32_t>(vp.Height);e.view=view;e.dsv=ds;e.rtv0=rt;k=key;
                e.which=view && view==alt_view_.load(std::memory_order_relaxed)?2:eye_of_view(view);e.vp_ok=view && read_ && read_(view+0x7510,e.vp,64);}
            if(k==key) {e.n[certain].fetch_add(1,std::memory_order_relaxed);return;}}
    }
    static std::string draw_views_json() {
        std::ostringstream o;o<<"{\"alt_view\":\""<<std::hex<<alt_view_.load()<<"\",\"views\":[";bool first=true;
        for(const auto& e:draw_views_) if(e.key.load()) {o<<(first?"":",")<<"{\"viewport\":["<<std::dec<<e.vp_w<<','<<e.vp_h<<"],\"view\":\""<<std::hex<<e.view<<"\",\"dsv\":\""<<e.dsv<<"\",\"rtv0\":\""<<e.rtv0
            <<"\",\"fallback\":"<<std::dec<<e.n[0].load()<<",\"certain\":"<<e.n[1].load()<<",\"other_view_na\":"<<e.n[2].load()<<",\"which\":"<<e.which;
            if(e.vp_ok) {o<<",\"vp\":[";for(int i=0;i<16;++i) o<<(i?",":"")<<e.vp[i];o<<"],\"scale_rows\":["<<std::hypot(e.vp[0],e.vp[1],e.vp[2])<<','<<std::hypot(e.vp[4],e.vp[5],e.vp[6])
                <<"],\"scale_cols\":["<<std::hypot(e.vp[0],e.vp[4],e.vp[8])<<','<<std::hypot(e.vp[1],e.vp[5],e.vp[9])<<']';}
            o<<'}';first=false;}
        o<<"]}";return o.str();
    }
    static void render_note(std::uint64_t display,RenderCount k) noexcept {
        if(display==~0ull) return;
        RenderRow& w=render_rows_[display%render_ring];
        if(w.display.load(std::memory_order_acquire)==display) w.n[k].fetch_add(1,std::memory_order_relaxed);
    }
    static std::string row_json(const RenderRow& w) {
        std::ostringstream o;o.precision(9);
        o<<"{\"display\":"<<w.display.load()<<",\"frame\":"<<w.frame<<",\"eye\":"<<w.eye<<",\"prev_frame\":"<<w.prev_frame<<",\"phase\":"<<w.phase
         <<",\"reset\":"<<(w.reset?"true":"false")<<",\"has_prev\":"<<(w.has_prev?"true":"false")
         <<",\"jitter\":["<<w.jitter[0]<<','<<w.jitter[1]<<"],\"prev_jitter\":["<<w.prev_jitter[0]<<','<<w.prev_jitter[1]<<']';
        if(w.src_known) o<<",\"rows_src_jitter\":["<<w.src_jitter[0]<<','<<w.src_jitter[1]<<"],\"src_matches_pair\":"<<(w.src_jitter[0]==w.prev_jitter[0] && w.src_jitter[1]==w.prev_jitter[1]?"true":"false");
        else o<<",\"rows_src_jitter\":null";
        static constexpr const char* names[rr_count]{"same","own_last","other_now","other_last","unproven","miss","camera_only","raw","uncertain",
            "own_d2","own_d4","own_dother","rig_own","rig_zero","rig_palette_miss","rig_zero_unnamed"};
        for(unsigned k=0;k<rr_count;++k) o<<",\""<<names[k]<<"\":"<<w.n[k].load(std::memory_order_relaxed);
        if(std::uint32_t m[3]{};RigMask::mask_counts(w.display.load(),m)) o<<",\"mask\":{\"rig_px\":"<<m[0]<<",\"own_px\":"<<m[1]<<",\"wild_mv_px\":"<<m[2]<<'}'; // v1207 GPU census
        o<<'}';return o.str();
    }
    static std::string renders_json() {
        std::array<const RenderRow*,render_ring> rows{};unsigned n=0;
        for(const auto& w:render_rows_) if(w.display.load(std::memory_order_acquire)!=~0ull) rows[n++]=&w;
        std::sort(rows.begin(),rows.begin()+n,[](const RenderRow* a,const RenderRow* b) {return a->display.load()<b->display.load();});
        std::string o="[";for(unsigned i=0;i<n;++i) o+=(i?",":"")+row_json(*rows[i]);return o+"]";
    }
    inline static thread_local float early_z_[4]{}; // v1203: the z row the early gathers used
    inline static std::array<std::array<std::atomic<std::uint64_t>,11>,3> none_fit_kind_{};
    inline static std::atomic<std::uint32_t> none_fit_first_{~0u},none_fit_last_{},rig_none_fit_first_{~0u},rig_none_fit_last_{};inline static std::atomic<std::uint64_t> rig_none_fit_spans_{};
    static unsigned decade(double x) noexcept {if(!(x>0)) return 0;const int d=static_cast<int>(std::floor(std::log10(x)))+10;return static_cast<unsigned>(std::clamp(d,0,10));}
    // The view this draw's rows were built with, by content; false = ambiguous / not this form (counted). `rows`: the draw's engine rows.
    static bool content_view(const float* rows,View& out,const float* prev_rows=nullptr) noexcept {
        if(const auto source=pass_source_.load(std::memory_order_relaxed)) { // a pass of another view (shadow, light): not this test's form
            const auto v=source().view;const auto alt=alt_view_.load(std::memory_order_relaxed);
            if(v && alt && v!=alt) {content_outcome_[content_other_pass].fetch_add(1,std::memory_order_relaxed);tl_content_why_=3;return false;}
        }
        refresh_candidates();
        if(tl_cand_n_<1) {content_outcome_[content_no_candidates].fetch_add(1,std::memory_order_relaxed);tl_content_why_=4;return false;}
        double shear[8]{};unsigned eyes[8]{};std::uint32_t frames[8]{};
        for(int i=0;i<tl_cand_n_;++i) {const auto& c=tl_cand_[static_cast<std::size_t>(i)];shear[i]=rows_shear(c.p,rows);eyes[i]=c.view.eye;frames[i]=c.view.frame;}
        bool tied=false;
        const int pick=pick_by_shear_eyes(shear,eyes,frames,tl_cand_n_,content_fit_.load(std::memory_order_relaxed),content_ratio_.load(std::memory_order_relaxed),content_floor_.load(std::memory_order_relaxed),tied);
        int best=0,second=-1;for(int i=1;i<tl_cand_n_;++i) if(shear[i]<shear[best]) best=i;
        for(int i=0;i<tl_cand_n_;++i) if(i!=best && eyes[i]!=eyes[best] && (second<0 || shear[i]<shear[second])) second=i; // the best of the OTHER eye
        content_hist_[0][decade(shear[best])].fetch_add(1,std::memory_order_relaxed);
        if(second>=0) content_hist_[1][decade(shear[second])].fetch_add(1,std::memory_order_relaxed);
        // v1203: not decided by orthogonality and the draw has previous rows: the Gram test (buffer_views.hpp rows_gram). Per current-render
        // candidate h, the best agreement with any OLDER candidate g under which the previous rows were built; picked like the shear.
        if(pick<0 && prev_rows) {
            double g_cur[8][6]{},g_prev[8][6]{},score[8]{};bool ok_c[8]{},ok_p[8]{};
            for(int i=0;i<tl_cand_n_;++i) {const auto& c=tl_cand_[static_cast<std::size_t>(i)];ok_c[i]=rows_gram(c.p,rows,g_cur[i]);ok_p[i]=rows_gram(c.p,prev_rows,g_prev[i]);}
            for(int h=0;h<tl_cand_n_;++h) {
                score[h]=1e30;if(!ok_c[h]) continue;
                for(int g=0;g<tl_cand_n_;++g) if(ok_p[g] && frames[g]<frames[h]) score[h]=std::min(score[h],gram_distance(g_cur[h],g_prev[g]));
            }
            bool gram_tied=false;
            const int gram=pick_by_shear_eyes(score,eyes,frames,tl_cand_n_,content_fit_.load(std::memory_order_relaxed),content_ratio_.load(std::memory_order_relaxed),content_floor_.load(std::memory_order_relaxed),gram_tied);
            if(gram>=0) {
                out=tl_cand_[static_cast<std::size_t>(gram)].view;content_outcome_[content_certain_gram].fetch_add(1,std::memory_order_relaxed);
                content_tick_[0].fetch_add(1,std::memory_order_relaxed);return true;
            }
        }
        if(pick==-1) {
            none_fit_kind_[tl_draw_kind_<3?tl_draw_kind_:2][decade(shear[best])].fetch_add(1,std::memory_order_relaxed);
            const auto f=newest_frame_.load(std::memory_order_relaxed);
            for(auto m=none_fit_first_.load();f<m && !none_fit_first_.compare_exchange_weak(m,f);) {}
            for(auto m=none_fit_last_.load();f>m && !none_fit_last_.compare_exchange_weak(m,f);) {}
            if(tl_draw_kind_==0) { // v1211: unnamed rig draws: first/last engine frame and the number of separate spans (a gap > 8 frames starts one)
                for(auto m=rig_none_fit_first_.load();f<m && !rig_none_fit_first_.compare_exchange_weak(m,f);) {}
                const auto last=rig_none_fit_last_.exchange(f);if(last==0 || f>last+8) rig_none_fit_spans_.fetch_add(1,std::memory_order_relaxed);
            }
        }
        if(pick<0) {content_outcome_[pick==-1?content_none_fit:content_ambiguous].fetch_add(1,std::memory_order_relaxed);tl_content_why_=pick==-1?2u:1u;return false;}
        out=tl_cand_[static_cast<std::size_t>(pick)].view;content_outcome_[tied?content_tie_same_eye:content_certain].fetch_add(1,std::memory_order_relaxed);
        bool newest=true;for(int i=0;i<tl_cand_n_;++i) if(tl_cand_[static_cast<std::size_t>(i)].view.eye==out.eye && tl_cand_[static_cast<std::size_t>(i)].view.frame>out.frame) newest=false;
        content_tick_[newest?0:1].fetch_add(1,std::memory_order_relaxed);
        return true;
    }
    static std::string content_json() {
        std::string o="{\"outcome\":{\"certain\":"+std::to_string(content_outcome_[0].load())+",\"ambiguous\":"+std::to_string(content_outcome_[1].load())
            +",\"none_fit\":"+std::to_string(content_outcome_[2].load())+",\"other_pass\":"+std::to_string(content_outcome_[3].load())+",\"no_candidates\":"+std::to_string(content_outcome_[4].load())
            +",\"certain_same_eye_tie\":"+std::to_string(content_outcome_[5].load())+",\"certain_gram\":"+std::to_string(content_outcome_[6].load())+"}";
        for(unsigned h=0;h<2;++h) {o+=h?",\"second_log10\":[":",\"best_log10\":[";for(unsigned k=0;k<11;++k) o+=(k?",":"")+std::to_string(content_hist_[h][k].load());o+="]";}
        o+=",\"tick\":["+std::to_string(content_tick_[0].load())+','+std::to_string(content_tick_[1].load())+"],\"vs_job\":["+std::to_string(content_vs_job_[0].load())+','+std::to_string(content_vs_job_[1].load())
            +','+std::to_string(content_vs_job_[2].load())+','+std::to_string(content_vs_job_[3].load())+"]";
        o+=",\"none_fit_frames\":["+std::to_string(none_fit_first_.load())+','+std::to_string(none_fit_last_.load())+"],\"rig_none_fit_frames\":["+std::to_string(rig_none_fit_first_.load())+','+std::to_string(rig_none_fit_last_.load())+','+std::to_string(rig_none_fit_spans_.load())+"],\"none_fit_by_kind\":{";
        {static constexpr const char* kinds[3]{"rig","near","far"};
         for(unsigned k=0;k<3;++k) {o+=std::string(k?",":"")+'"'+kinds[k]+"\":[";for(unsigned d=0;d<11;++d) o+=(d?",":"")+std::to_string(none_fit_kind_[k][d].load());o+=']';}}
        o+="}";
        o+=",\"tol\":["+std::to_string(content_fit_.load())+','+std::to_string(content_ratio_.load())+','+std::to_string(content_floor_.load())+"]}";
        return o;
    }
    // classify's view of a record (under views_mutex_): the eye records give the correction and camera-only previous; publishes latest_
    static void fill_view(unsigned eye,const EyeView& rec,View& out) noexcept {
        corrections(eye,rec,out);
        if(Latest& l=latest_[out.eye];out.frame>=l.view.frame) { // one writer: under views_mutex_
            l.seq.fetch_add(1,std::memory_order_acq_rel);l.view=out;l.seq.fetch_add(1,std::memory_order_release);
        }
        if(out.frame>=newest_frame_.load(std::memory_order_relaxed)) {latest_eye_.store(out.eye,std::memory_order_relaxed);newest_frame_.store(out.frame,std::memory_order_release);}
    }
    // A record's view: its correction and camera-only previous (under views_mutex_)
    static void corrections(unsigned eye,const EyeView& rec,View& out) noexcept {
        const EyeView* best=&rec;const unsigned own_step=alt_view_.load(std::memory_order_relaxed)?2u:1u;
        out=View{};out.eye=eye;out.frame=best->frame;
        std::memcpy(out.inv,best->inv,32);std::memcpy(out.z,best->m[2]+8,16);std::memcpy(out.vp,best->m[2],64);
        out.prev_frame=rec.prev_frame;out.has_prev=rec.has_prev;out.receipt=rec.receipt;out.display=rec.display;
        if(rec.receipt) { // v1206 alternate: the receipt's previous render (its VP: exactly the TAA pair's previous camera), never a guessed frame
            float inverse_source[16],inverse_own[16];const EyeView* source=nullptr;
            for(const auto& r:eye_records_[eye^1u]) if(r.valid && r.receipt && r.frame>rec.prev_frame && r.frame<rec.frame && (!source || r.frame>source->frame)) source=&r;
            out.corr_ok=rec.has_prev && source && inverse(source->m[2],inverse_source);if(out.corr_ok) mul(rec.prev_vp,inverse_source,out.corr);
            out.camera_ok=inverse(rec.m[2],inverse_own);if(out.camera_ok) mul(rec.has_prev?rec.prev_vp:rec.m[2],inverse_own,out.camera); // reset: no motion
            return;
        }
        // Draws the chain cannot prove (rival's PrevCorrection idea): a previous MVP cached from the other eye's view (case A:
        // R(n-1) for the left eye, L(n) for the right) moved into this eye's last view, VP_own(n-1) * inverse(VP_source).
        const EyeView* own=nullptr;const EyeView* source=nullptr;const std::uint32_t source_frame=out.eye && own_step==1?out.frame:out.frame-1;
        for(const auto& r:eye_records_[out.eye]) if(r.valid && r.frame==out.frame-own_step) own=&r;
        for(const auto& r:eye_records_[out.eye^1u]) if(r.valid && r.frame==source_frame) source=&r;
        float inverse_source[16];
        out.corr_ok=own && source && inverse(source->m[2],inverse_source);
        if(out.corr_ok) mul(own->m[2],inverse_source,out.corr);
        // And the camera-only previous, VP_own(n-1) * inverse(VP_own(n)) * current: exact for static objects (the left eye's
        // unmatched draws are not from the other eye's view, v871: their cache was last written by an unrecorded view).
        float inverse_own[16];
        // (alternate: own = this eye's record two frames back, as v1152; the v1153 variant from the view's +0x7590 was removed in v1157)
        out.camera_ok=own && inverse(best->m[2],inverse_own);
        if(out.camera_ok) mul(own->m[2],inverse_own,out.camera);
    }
    static bool latest(unsigned eye,View& out) noexcept {
        const Latest& l=latest_[eye&1u];
        for(int i=0;i<4;++i) {
            const auto q=l.seq.load(std::memory_order_acquire);
            if(q&1) continue;
            out=l.view;
            std::atomic_thread_fence(std::memory_order_acquire);
            if(l.seq.load(std::memory_order_relaxed)==q) return q!=0;
        }
        return false;
    }
    // Diagnostics, every 16th tagged draw per thread: the draws' VS b1 buffers by the tag's eye (own context) or fallback.
    static void note_b1(const void* b,bool own,unsigned eye) noexcept {
        static thread_local unsigned n=0;
        if(!b || (++n&15)) return;
        for(auto& e:b1_seen_) {
            const void* key=e.buffer.load(std::memory_order_relaxed);
            if(!key && e.buffer.compare_exchange_strong(key,b,std::memory_order_relaxed)) key=b;
            if(key!=b) continue;
            e.n[own?eye&1u:2u].fetch_add(1,std::memory_order_relaxed);return;
        }
    }
    // The eye view of a draw: the last per-view write on its context when that is of the newest frame (a shadow or other
    // view's constants there leave the draw untagged), else the newest eye view written anywhere.
    // v950: in paired stereo that newest view is often the other eye's (weapon-pin: weapon draws tagged with the other eye).
    // The draw's VS b1, when it is the per-view globals buffer an eye's native TAA binds, names the eye; the tag keeps it.
    static bool current_view(const ContextState& s,View& out,const float* rows=nullptr,const float* prev_rows=nullptr) noexcept {
        const std::uint32_t newest=newest_frame_.load(std::memory_order_acquire);
        const bool own=s.view_frame==newest;
        bool ok;
        // v1193/v1194 (buffer_views.hpp view_source): one-view frames never take the newest view anywhere while the draw's own context, or any
        // context, recorded what its bound b1 holds: the next render's Setup and view writes overlap this render's recording (the other eye)
        const bool alt=alternate();
        if(alt && !alt_view_heuristic.load(std::memory_order_relaxed)) { // v1199: certain or nothing (buffer_views.hpp view_certain)
            std::uint64_t job=0;if(const auto source=pass_source_.load(std::memory_order_relaxed)) job=source().job;
            const bool job_certain=view_certain(s.view_valid,s.view_buffer,s.cb[0],s.view_job,job,s.type==1);
            // v1200: by the rows' content (the VP they were built with); the same-job b1 rule stays as a cross-check and a fallback
            tl_content_why_=0;
            const bool by_content=rows && content_view(rows,out,prev_rows);
            // v1201: the same-job b1 view (v1199) is only compared (v1200 sim: it disagreed with content in 60 % of the draws both named)
            if(by_content && job_certain) content_vs_job_[out.eye!=s.view.eye?1u:out.frame==s.view.frame?0u:out.frame>s.view.frame?2u:3u].fetch_add(1,std::memory_order_relaxed);
            const bool certain=by_content;
            tl_view_src_=certain?6u:7u;alt_view_src_[tl_view_src_].fetch_add(1,std::memory_order_relaxed);
            tl_view_heuristic_=!certain;
            if(certain) return true;
            // v1205 (v1203 probe: whole renders of broken world vectors when uncertain draws kept the engine's other-eye rows): an uncertain
            // draw takes the v1197 timing view as a LABEL for the previous-row chain (v1198 measured clean world vectors with it). The chain
            // itself is exact by content (PrevMvpTable::fix); only consumers that need the render itself (placement, rig palette, held
            // mask, hypothesis check) require the content view (have_content in prepare).
        }
        View shared{};const bool b1_known=alt && !own && buffer_views_.find(s.cb[0],shared);
        tl_view_src_=view_source(own,alt,s.view_valid,s.view_buffer,s.cb[0],b1_known);
        if(tl_view_src_==0) {out=s.view;ok=s.view_valid;}
        else if(tl_view_src_==1 || tl_view_src_==5) {out=s.view;ok=true;}
        else if(tl_view_src_==2) {out=shared;ok=true;if(out.frame!=newest) tl_view_src_=3;}
        else ok=latest(latest_eye_.load(std::memory_order_acquire),out) && out.frame==newest;
        if(alternate()) {alt_view_src_[tl_view_src_].fetch_add(1,std::memory_order_relaxed);
            if(tl_view_src_==4) alt_newest_why_[!s.cb[0]?0:(s.view_valid && s.view_buffer)?1:2].fetch_add(1,std::memory_order_relaxed);} // v1196
        if(!ok) return false;
        const int key=b1_eye(s.cb[0]);
        note_b1(s.cb[0],own,out.eye);
        count(static_cast<Counter>((own?own_b1_agree:latest_b1_agree)+(key<0?2u:static_cast<unsigned>(key)==out.eye?0u:1u)));
        // v1160: not in alternate mode. There one engine frame holds one eye, so the classified frame already names the eye exactly; the
        // retag (built for paired frames, where both eyes share a frame) looks for the other eye's view of this frame, which never exists,
        // and drops the draw as untagged (b1_lost) whenever the view's rotating b1 buffers were last seen with the other eye.
        if(key<0 || static_cast<unsigned>(key)==out.eye || !b1_eye_on_.load(std::memory_order_relaxed) || alternate()) return true;
        ok=latest(static_cast<unsigned>(key),out) && out.frame==newest; // that eye's view of this frame, else untagged
        count(ok?b1_retagged:b1_lost);
        return ok;
    }
    // A write of a learned buffer: per-instance rows are shadowed for the draws, per-view constants classified.
    static void written(ID3D11DeviceContext* c,ID3D11Resource* r,const void* data,Learned& entry) noexcept {
        if(!on()) return;
        const auto t=__rdtsc();
        D3D11_RESOURCE_DIMENSION dimension{};r->GetType(&dimension);
        D3D11_BUFFER_DESC desc{};
        if(dimension==D3D11_RESOURCE_DIMENSION_BUFFER) static_cast<ID3D11Buffer*>(r)->GetDesc(&desc);
        ContextState* s=(desc.BindFlags&D3D11_BIND_CONSTANT_BUFFER)?state(c):nullptr;
        const unsigned kind=entry.kind.load(std::memory_order_relaxed);
        if(s && kind==cb2_kind && desc.ByteWidth>max_floats*4) {count(oversize);oversize_kb_[std::min<UINT>(desc.ByteWidth/1024,16u)].fetch_add(1,std::memory_order_relaxed);}
        else if(s && kind==cb2_kind) {
            Shadow& sh=s->shadow(r);sh.buffer=r;sh.size=desc.ByteWidth;std::memcpy(sh.data,data,desc.ByteWidth);count(shadow_writes);
        } else if(s && kind==view_kind) {
            View view{};const bool valid=classify(static_cast<const float*>(data),desc.ByteWidth,view);
            count(view_writes);s->view=view;s->view_valid=valid;if(valid) buffer_views_.store(r,view); /* v1193 */ s->view_buffer=valid?r:nullptr; /* v1194 */ s->view_frame=valid?view.frame:newest_frame_.load(std::memory_order_acquire);
            s->view_job=0;if(const auto source=pass_source_.load(std::memory_order_relaxed)) s->view_job=source().job; // v1199
            if(valid) entry.misses.store(matched,std::memory_order_relaxed);
            else if(entry.misses.fetch_add(1,std::memory_order_relaxed)==15) entry.kind.store(ignored_kind,std::memory_order_relaxed);
        }
        count(ticks,__rdtsc()-t);
    }
    // Sim proof: the screen motion of the model origin (NDC/1000) by the engine's and by the submitted previous rows.
    static void measure(unsigned eye,const float* cur,const float* engine,const float* out) noexcept {
        const auto motion=[&](const float* p) {
            if(!(cur[15]>.01f && p[15]>.01f)) return -1.0;
            return 1000.0*std::hypot(double(cur[3]/cur[15]-p[3]/p[15]),double(cur[7]/cur[15]-p[7]/p[15]));
        };
        if(const double e=motion(engine),o=motion(out);e>=0) {
            count(eye?engine_sum_r:engine_sum_l,static_cast<std::uint64_t>(e));count(eye?out_sum_r:out_sum_l,static_cast<std::uint64_t>(o));
            if(e>2) count(eye?engine_big_r:engine_big_l);
            if(o>2) count(eye?out_big_r:out_big_l);
        }
    }
    static bool make_pool(ID3D11DeviceContext* c,ContextState& s) noexcept {
        if(s.pool_failed) return false;
        ID3D11Device* device{};c->GetDevice(&device);
        D3D11_BUFFER_DESC desc{};desc.ByteWidth=max_floats*4;desc.Usage=D3D11_USAGE_DYNAMIC;
        desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;desc.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
        const bool ok=device && SUCCEEDED(device->CreateBuffer(&desc,nullptr,&s.pool)) && s.pool;
        if(device) device->Release();
        if(!ok) {s.pool=nullptr;s.pool_failed=true;count(pool_fail);}
        return ok;
    }
    // This draw's own previous rows into the context's pool buffer; true when it is to be bound at b2 for the draw.
    // v1101: a weapon draw in a render pass whose view is one of the eye views is placed for that eye whether or not this draw's shader
    // declares previous rows and whether or not the context's per-view constants classify: the pass names the eye exactly. The previous
    // rows come from the publisher (the placed previous render), so every pass of the same part gets the same two rows. Passes whose view
    // is not an eye view (shadow maps) are left at the engine's tick frame and counted (docs/WEAPON-DRAW-PLACE-TEMPORAL.md).
    static bool prepare(ID3D11DeviceContext* c,ContextState& s) noexcept {
        count(layout_draws);
        const Shadow* sh=s.cb[1]?&s.shadow(s.cb[1]):nullptr;
        if(!sh || sh->buffer!=s.cb[1]) {if(s.cb[1]) learn(s.cb[1],cb2_kind);count(no_shadow);return false;}
        // v1040: no previous rows and no weapon placement: the draw ends in no_rows below whatever the view says. Skip the view lookup,
        // the pass callback and the row gather (the b1 agree/untagged diagnostics no longer count these draws).
        const bool one_view_place=alt_weapon_place.load(std::memory_order_relaxed) && alternate() && mode_.load(std::memory_order_relaxed)>=1; // v1191
        if(!has_previous(s.layout,sh->size) && !weapon_draw_on(placing_mode(),mode_.load(std::memory_order_relaxed)) && !one_view_place) {
            count(motion);count(no_rows);return false;
        }
        View view{};
        // v1200: alternate certain mode identifies the view from the rows, so they are gathered first (z row: the newest eye's; the content
        // test never reads the z row, and the view's own z row re-gathers below when it differs)
        float early[16]{};bool early_ok=false;
        if(alternate() && !alt_view_heuristic.load(std::memory_order_relaxed)) {
            float zn[4]{};const unsigned e=latest_eye_.load(std::memory_order_relaxed)&1u;for(unsigned k=0;k<4;++k) zn[k]=eye_z_[e][k].load(std::memory_order_relaxed);
            early_ok=gather_current(s.layout,sh->data,sh->size,zn,early);std::memcpy(early_z_,zn,16);
        }
        // v1201: only draws a consumer can use are tested: previous rows, near (held / placement), rig (rig motion, rig target)
        const float near_w0=weapon_draw::Place::near_w.load(std::memory_order_relaxed);
        const bool content_wanted=early_ok && (has_previous(s.layout,sh->size) || (early[15]>0.01f && early[15]<near_w0) || RigMask::draw_is_rig());
        tl_draw_kind_=RigMask::draw_is_rig()?0u:early_ok && early[15]>0.01f && early[15]<near_w0?1u:2u; // v1202 diagnostics
        float early_prev[16]{};const bool early_prev_ok=content_wanted && gather_previous(s.layout,sh->data,sh->size,early_z_,early_prev); // v1203: Gram test
        tl_view_heuristic_=false;
        const bool have_view=current_view(s,view,content_wanted?early:nullptr,early_prev_ok?early_prev:nullptr);
        const bool have_content=have_view && !(alternate() && tl_view_heuristic_); // v1205: the render named by content (paired: any view)
        // The pass this draw is recorded in, when build/native-gpu-timing or build/shadow-share (or the weapon placement itself, which
        // installs the pass map) is on: its id and the eye its view names. A v1101 pass job's view is one of the eye views for a view pass.
        int pass=-1,pass_eye=-1;
        if(const auto source=pass_source_.load(std::memory_order_relaxed)) {
            const DrawPass p=source();
            pass=p.pass;
            if(p.view) for(unsigned e=0;e<2;++e) if(eye_views_[e].load(std::memory_order_relaxed)==p.view) {pass_eye=static_cast<int>(e);break;}
        }
        float z_local[4]{};
        const float* z=view.z;
        if(!have_view) { // a draw whose pass the engine did not tag still has the pass eye's z row (reverse-Z: 0,0,0,near)
            const unsigned e=pass_eye>=0?static_cast<unsigned>(pass_eye):0u;
            for(unsigned k=0;k<4;++k) z_local[k]=eye_z_[e][k].load(std::memory_order_relaxed);
            z=z_local;
        }
        float cur[16],prev[16],engine[16];
        if(!gather_current(s.layout,sh->data,sh->size,z,cur)) {count(no_rows);return false;}
        // v1183 (knob stale-eye-held-mask): a near draw whose model frame is a held weapon's / held thing's engine frame joins the rig mask (253)
        // v1199: an uncertain draw is held when both eyes' latest views agree it is (the match tolerance is far wider than the eye separation;
        // they disagree only at the edge of the reach: then not held, counted as a fallback)
        if(alternate() && held_mask.load(std::memory_order_relaxed) && cur[15]>0.01f && cur[15]<weapon_draw::Place::near_w.load(std::memory_order_relaxed)) {
            bool held=false,decided=true;
            if(have_content) held=held_match(view,cur);
            else {
                bool m[2]{},any[2]{};
                for(unsigned h=0;h<2;++h) {rig_target::M4 e{};float vp[16]{};any[h]=rig_target::book().latest_of(h,e,vp);m[h]=any[h] && held_match(vp,cur);}
                decided=any[0] && any[1] && m[0]==m[1];held=decided && m[0];
            }
            if(held) {RigMask::mark_held();view_use(use_held_mask);}
            if(held || !decided) certainty(cert_held_mask,decided);
        }
        if(have_view && alternate() && cur[15]>0.01f && cur[15]<1.5f) view_use(use_near); // v1194: near draws (hands, held items, body) by view source
        // v1190 (rig_target.hpp, knob stale-eye-rig-target): a depth-writing rig/held draw of the one-view gets its rows for the stale eye's target
        // camera, E * rows, E found by this draw's view-projection (the per-view write it was classified by); the mask replay draws it with them
        // v1199: both hypotheses (rig_mask.hpp); the view is not needed
        if(alternate() && RigMask::target_wanted()) {target_dual(s,sh,cur,have_content?&view:nullptr);view_use(use_rig_target);}
        bool have_prev=gather_previous(s.layout,sh->data,sh->size,z,prev);
        count(motion);if(have_prev) std::memcpy(engine,prev,64);
        const float near_w=weapon_draw::Place::near_w.load(std::memory_order_relaxed);
        const bool near_draw=cur[15]>0.01f && cur[15]<near_w;
        const bool alt=alternate(); // v1151: the held-weapon placement is a paired-mode binder; alternate draws take the general path
        // v1162: a rig draw (rig_mask.hpp: binds an arm/body skin buffer) keeps the engine's own rows in alternate mode: the mod re-poses
        // those models per render (image skin), so no table row of them is this eye's true previous; the warp's hand layers own their pixels.
        // v1166: no longer the raw rows. The eye's own previous rows (the two-hop chain) together with the eye's own previous palette
        // (RigMask::previous_palette at VS t0); without either, both are the current ones (zero motion). Never one without the other.
        if(alt && RigMask::draw_is_rig()) {
            alt_rig_.fetch_add(1,std::memory_order_relaxed);
            if(mode_.load(std::memory_order_relaxed)<1 || !RigMask::motion.load(std::memory_order_relaxed)) return false; // v1171 opt-in: off = v1165 (raw rows, engine palette)
            if(!have_prev) return false; // no previous rows: not a motion-vector writer (the colour pass), nothing to pair
            // v1204 (still-probe sims: rig own 63k/zero 98 in v1190, own 9.7k/zero 70k in v1202): the rig's own motion needs NO render label.
            // Its rows come from the content chain (fix_any: both hops matched by exact 64-byte rows in a window of labels) and its palette
            // from the GPU-order ring (P = two engine frames back). v1193-v1203 ran that only for draws with a (certain) view, so an
            // uncertain rig draw was neither fixed nor INSERTED into the table, and the next render's chain through it broke as well. Now
            // every rig draw is recorded and chained; the label is the certain view's when there is one, else the newest engine frame
            // (fix_any searches both eyes and frames -1..+6 around it).
            // v1206: by construction. Only a content-certain draw (its render's receipt: eye, frame, previous render) is chained and recorded;
            // rows and palette both come from exactly the receipt's previous render (fix_render, the tagged palette ring), whose jitter is the
            // TAA pair's previous. Anything else: zero motion (current rows + current palette), counted.
            // v1179: never own_last: the rig is drawn in every render, so its raw previous rows are always an other-eye render's.
            auto kind=PrevMvpTable::miss;
            // v1207 (v1206 stillprobe: 49,289 rig draws zeroed for "no content"): v1204's rule again, by construction. Without a content view the
            // chain itself names the render (prove_render: exactly one receipt record whose two hops match); that record is then the draw's render.
            bool chained=false;
            if((!have_content || !view.receipt) && std::memcmp(cur,prev,64)) chained=rig_render_by_chain(prev,view);
            const bool have_render=(have_content && view.receipt) || chained;
            if(chained) rig_chained_.fetch_add(1,std::memory_order_relaxed);
            if(have_render && view.has_prev) kind=table_.fix_render(view.eye,view.frame,view.prev_frame,cur,prev,false);
            else if(have_content && view.receipt) table_.insert(view.eye,view.frame,cur,prev); // a history reset: recorded for the next render
            const bool rows_own=kind==PrevMvpTable::other_last;
            RigMask::tl_bound_slot_=~0u;RigMask::tl_bound_tag_=~0u;RigMask::tl_bound_buffer_=0;
            const bool own=RigMask::previous_palette(c,have_render?static_cast<int>(view.eye):-1,rows_own?view.prev_frame:~0u,rows_own);
            if(have_render) RigMask::capture_drawn(c,view.frame); // v1211: this render's slot = the current palette this draw binds (t1), in GPU order
            if(have_render) rig_record(c,view.display,cur,engine,own?prev:cur,static_cast<unsigned>(kind),own,chained);
            draw_view_note(c,have_render?1u:0u);
            // status rig_zero_why: [first frame (cur == prev), chain miss, chain unproven (the second hop), palette (no slot tagged with the
            // previous render), no content view (or no receipt record), history reset]
            // v1208: an unnamed rig draw that does not write the eye's motion-vector target (another view's depth pass) is not applicable
            const bool applicable=have_render || note_pass_fallback(c,true);
            if(!own && applicable) rig_zero_why_[!have_render?4:!view.has_prev?5:kind==PrevMvpTable::same?0:kind==PrevMvpTable::miss?1:kind==PrevMvpTable::unproven?2:3].fetch_add(1,std::memory_order_relaxed);
            if(!applicable) rig_na_.fetch_add(1,std::memory_order_relaxed);
            else render_note(have_render?view.display:newest_display_.load(std::memory_order_relaxed),own?rr_rig_own:have_render?rr_rig_zero:rr_rig_zero_unnamed);
            if(have_render && rows_own && !own) render_note(view.display,rr_rig_palette_miss);
            if(have_view) view_use(use_rig_motion);
            certainty(cert_previous_rows,own || have_content?cert_certain:tl_content_why_==3 || !applicable?cert_na:cert_fallback);
            if(own) alt_rig_own_.fetch_add(1,std::memory_order_relaxed);
            if(!own) std::memcpy(prev,cur,64);
            if(own && have_render && RigMask::probe_on.load(std::memory_order_relaxed)) { // v1210: the b2 bytes bind_pool writes, for the draw-time probe
                static thread_local float want[max_floats];std::memcpy(want,sh->data,sh->size);scatter(s.layout,prev,want);RigMask::probe_arm(view.display,want,sh->size);
            }
            return bind_pool(c,s,*sh,nullptr,prev);
        }
        // v1191: one-view frames: the held weapon/prop placed in this render's drawn hand, previous rows from this eye's previous render
        // v1201: a placement candidate = a near draw matching a held thing's frame under either eye's latest view (weapons, copies, props);
        // other near draws (the body, scenery) are not applicable
        const auto held_any=[&] {for(unsigned h=0;h<2;++h) {rig_target::M4 e{};float vp[16]{};if(rig_target::book().latest_of(h,e,vp) && held_match(vp,cur)) return true;}return false;};
        if(near_draw && one_view_place && !have_content && alt) certainty(cert_placement,held_any()?cert_fallback:cert_na); // v1199: uncertain: the engine's pose
        if(near_draw && one_view_place && have_content) {
            rig_target::M4 prev_vp{};
            if(rig_target::book().find_prev(view.vp,prev_vp)) {
                float placed[16]{},placed_prev[16]{};bool own_prev=false;
                rig_target::M4 vp{};for(int i=0;i<16;++i) vp[static_cast<std::size_t>(i)]=view.vp[i];
                const auto r=weapon_draw::Place::place_one_view(vp,prev_vp,cur,placed,placed_prev,own_prev);
                if(r.side>=0) {
                    alt_place_placed_.fetch_add(1,std::memory_order_relaxed);view_use(use_place);(own_prev?alt_place_own_prev_:alt_place_no_prev_).fetch_add(1,std::memory_order_relaxed);
                    certainty(cert_placement,true);
                    if(RigMask::target_wanted()) target_dual(s,sh,placed,&view,false); // v1199: the placed rows, both hypotheses (counted above)
                    if(!bind_pool(c,s,*sh,placed,have_prev?placed_prev:nullptr)) return false;
                    count(view.eye?fixed_r:fixed_l);return true;
                }
                certainty(cert_placement,cert_na); // v1201: certain view, not a held thing's part
            } else {alt_place_no_view_.fetch_add(1,std::memory_order_relaxed);certainty(cert_placement,held_any()?cert_fallback:cert_na);}
        }
        if(near_draw && !alt && weapon_draw_on(placing_mode(),mode_.load(std::memory_order_relaxed))) {
            count(weapon_near);
            const unsigned pass_index=pass>=0 && static_cast<unsigned>(pass)<pass_slots-1?static_cast<unsigned>(pass):pass_slots-1;
            weapon_pass_near_[pass_index].fetch_add(1,std::memory_order_relaxed);
            // A pass the map names but whose view is not an eye view (shadow maps, light passes) cannot use an eye view-projection: its
            // draws stay at the engine's tick frame. Without the pass map (pass < 0) the v931 certificate path runs unchanged.
            const bool view_pass=pass<0 || pass_eye>=0;
            if(view_pass) {
                std::uint32_t frame=have_view?view.frame:0;
                // v1146 (user, headset v1145: "a carried object is unstable in my LEFT eye, the right eye is good"): the held prop was placed in the
                // right eye 6x as often as the left (weapon_placed_view1 14197 vs view0 2385): left draws carried a classified view frame with no
                // published slot, or a pass-eye frame check that failed, and kept the engine's tick pose. The pass names the eye exactly; the render's
                // published frame (both eyes are placed from the one publish at eye-0 begin) is the slot whatever the per-draw classifier says.
                if(pass_eye>=0) {
                    const std::uint32_t latest=weapon_draw::Place::latest_frame();
                    if(latest && weapon_draw::Place::slot_of(latest)) frame=latest;
                }
                float placed[16]={},placed_prev[16]={};
                static thread_local unsigned weapon_sample{};const bool timed=!(++weapon_sample&15);   // one draw in 16 is timed
                const auto started=timed?__rdtsc():0;
                const auto r=weapon_draw::Place::place(frame,&weapon_views,cur,have_prev?prev:nullptr,placed,placed_prev,
                    have_view?static_cast<int>(view.eye):-1,pass_eye);
                if(timed) count(weapon_ticks,(__rdtsc()-started)*16);
                if(r.hint>=0) weapon_pass_matched_[pass_index].fetch_add(1,std::memory_order_relaxed);
                if(r.stage) {count(static_cast<Counter>(weapon_stage+r.stage));if(r.hint>=0) weapon_skip_side_[static_cast<unsigned>(r.hint)][r.stage].fetch_add(1,std::memory_order_relaxed);}
                if(weapon_probe_on_.load(std::memory_order_relaxed) && have_view && (r.stage || cur[15]<1.5f)) probe_draw(view,cur,*sh,s,r);
                if(r.side>=0) {
                    count(r.eye?weapon_eye1:weapon_eye0);
                    if(!have_prev) count(weapon_placed_current_only);   // v1103: a colour/forward draw (no previous rows to rewrite)
                    if(have_view && static_cast<unsigned>(r.eye)!=view.eye) count(weapon_label_wrong);
                    if(have_prev) if(const double e=origin_motion(cur,engine),o=origin_motion(placed,placed_prev);e<1e29 && o<1e29) {
                        count(r.eye?weapon_engine_sum_r:weapon_engine_sum_l,static_cast<std::uint64_t>(e));
                        count(r.eye?weapon_out_sum_r:weapon_out_sum_l,static_cast<std::uint64_t>(o));
                    }
                    weapon_pass_placed_[pass_index].fetch_add(1,std::memory_order_relaxed);
                    weapon_pass_side_[static_cast<unsigned>(r.side)][pass_index].fetch_add(1,std::memory_order_relaxed);
                    weapon_placed_side_[static_cast<unsigned>(r.side)].fetch_add(1,std::memory_order_relaxed);
                    if(r.alt) weapon_alt_side_[static_cast<unsigned>(r.side)].fetch_add(1,std::memory_order_relaxed);
                    if(!bind_pool(c,s,*sh,placed,placed_prev)) return false;
                    count(r.eye?fixed_r:fixed_l);
                    return true;
                }
            }
        }
        // Not a placed weapon draw: the general per-eye previous-row correction (unchanged), which needs a classified view and this
        // draw's own previous rows.
        // v1206: an alternate draw whose render content cannot name has no provable previous render: zero motion (its error is at most the jitter
        // step, never the other eye's parallax), not recorded, counted. Another pass's view (shadow, light): left as the engine made it.
        if(alt && (!have_content || !view.receipt) && !alt_view_heuristic.load(std::memory_order_relaxed) && have_prev) {
            if(tl_content_why_==3) {certainty(cert_previous_rows,cert_na);return false;}
            if(note_pass_fallback(c,false)) {certainty(cert_previous_rows,cert_fallback);alt_uncertain_.fetch_add(1,std::memory_order_relaxed);render_note(newest_display_.load(std::memory_order_relaxed),rr_uncertain);}
            else certainty(cert_previous_rows,cert_na); // v1208: not the eye's motion-vector target: not applicable (still zero motion)
            if(mode_.load(std::memory_order_relaxed)<1) return false;
            std::memcpy(prev,cur,64);return bind_pool(c,s,*sh,nullptr,prev);
        }
        if(!have_view) {count(untagged);if(alt) {alt_untagged_.fetch_add(1,std::memory_order_relaxed);if(have_prev) {certainty(cert_previous_rows,tl_content_why_==3?cert_na:cert_fallback);if(tl_content_why_!=3) note_raw();}}return false;} // v1201: another pass's view (shadow): n/a
        if(!have_prev) {count(no_rows);return false;}
        // The current rows must be this view's: a stale shadow or another view's constants never are.
        if(!affine(view.inv[0],cur) && !affine(view.inv[1],cur)) {count(guard_fail);if(alt && tl_content_why_!=3) note_raw();return false;}
        // v1206 alternate: exactly the receipt's previous render (a reset: recorded, camera-only = no motion)
        auto kind=PrevMvpTable::miss;
        if(!alt) kind=table_.fix(view.eye,view.frame,cur,prev,1u);
        else if(view.receipt && view.has_prev) kind=table_.fix_render(view.eye,view.frame,view.prev_frame,cur,prev,true);
        else if(view.receipt) table_.insert(view.eye,view.frame,cur,prev);
        count(static_cast<Counter>((view.eye?kind_r:kind_l)+kind));
        if(alt) {alt_kinds_[kind].fetch_add(1,std::memory_order_relaxed);view_use(use_fix);certainty(cert_previous_rows,have_content?cert_certain:cert_fallback);} // v1205: heuristic label = fallback
        // Which draws stay unmatched (headset v832: ~20% per eye, sim ~5%): near ones (model origin closer than 1.5 m: hands,
        // held items, weapons) per kind, and the cb2 size of misses and unproven draws.
        if(cur[15]<1.5f) count(static_cast<Counter>((view.eye?near_r:near_l)+kind));
        if(kind==PrevMvpTable::miss || kind==PrevMvpTable::unproven) unmatched_size_[view.eye][sh->size/16<63?sh->size/16:63].fetch_add(1,std::memory_order_relaxed);
        bool apply=mode_.load(std::memory_order_relaxed)>=1 && (kind==PrevMvpTable::other_now || kind==PrevMvpTable::other_last);
        // v1151 alternate: an unproven or unmatched draw takes the camera-only previous (static assumption, design section 1), never
        // the other eye's rows (their parallax is not motion).
        if(alt && !apply && mode_.load(std::memory_order_relaxed)>=1 && (kind==PrevMvpTable::miss || kind==PrevMvpTable::unproven) && view.camera_ok) {
            mul(view.camera,cur,prev);apply=true;count(view.eye?camera_r:camera_l);alt_camera_.fetch_add(1,std::memory_order_relaxed);view_use(use_camera_only);
        }
        // Unproven or unmatched (headset ~20% per eye): the view correction, kept only when it shrinks the apparent motion.
        if(!alt && !apply && mode_.load(std::memory_order_relaxed)>=1 && (kind==PrevMvpTable::miss || kind==PrevMvpTable::unproven)) { // v1206: not alternate (unprovable)
            float moved[16],still[16];double best_motion=origin_motion(cur,engine);const float* pick=nullptr;
            if(view.corr_ok) {mul(view.corr,engine,moved);if(origin_motion(cur,moved)<best_motion) pick=moved;} // keeps the object's own motion
            if(!pick && view.camera_ok) {mul(view.camera,cur,still);if(origin_motion(cur,still)<best_motion) pick=still;}
            if(pick) {std::memcpy(prev,pick,64);apply=true;count(pick==moved?(view.eye?corrected_r:corrected_l):(view.eye?camera_r:camera_l));}
        }
        measure(view.eye,cur,engine,apply?prev:engine);
        if(alt && !apply && kind!=PrevMvpTable::same && kind!=PrevMvpTable::own_last) note_raw(); // v1205: the engine's other-eye rows stay (v1208: own_last rows are already own, not raw)
        if(alt) { // v1206 per-render ring
            const bool camera=apply && (kind==PrevMvpTable::miss || kind==PrevMvpTable::unproven);
            render_note(view.display,camera?rr_camera_only:!apply && kind!=PrevMvpTable::same && kind!=PrevMvpTable::own_last?rr_raw:static_cast<RenderCount>(kind));
            if(kind==PrevMvpTable::own_last || kind==PrevMvpTable::other_last) render_note(view.display,view.frame-view.prev_frame==2?rr_own_d2:view.frame-view.prev_frame==4?rr_own_d4:rr_own_dother);
        }
        if(!apply || !bind_pool(c,s,*sh,nullptr,prev)) return false;
        // v1182 (knob stale-eye-skin-motion): a skinned draw on this eye's own rows also gets this eye's own previous palette (skin_motion.hpp)
        if(alt && kind==PrevMvpTable::other_last) SkinMotion::bind_previous(c);
        count(view.eye?fixed_r:fixed_l);
        return true;
    }
    // The shadowed cb2 with its previous rows (and, for a placed weapon, its current rows) replaced, in the context's pool buffer.
    // A current-only layout has no previous rows: `prev_rows` may be null and scatter writes nothing there.
    static bool bind_pool(ID3D11DeviceContext* c,ContextState& s,const Shadow& sh,const float* cur_rows,const float* prev_rows) noexcept {
        const unsigned t=s.type;
        if(!map_[t] || !unmap_[t] || !set_cb_[t] || (!s.pool && !make_pool(c,s))) return false;
        D3D11_MAPPED_SUBRESOURCE m{};
        if(FAILED(map_[t](c,s.pool,0,D3D11_MAP_WRITE_DISCARD,0,&m)) || !m.pData) {count(pool_fail);return false;}
        auto* out=static_cast<float*>(m.pData);std::memcpy(out,sh.data,sh.size);
        if(cur_rows) scatter_cur(s.layout,cur_rows,out);
        scatter(s.layout,prev_rows,out);
        unmap_[t](c,s.pool,0);
        // The census must know the pool buffer is now at VS b2 (the draw hook's restore below reports the game's buffer again).
        if(const auto observe=wide_direct_cb_observer.load(std::memory_order_relaxed)) observe(c,2,1,&s.pool);
        set_cb_[t](c,2,1,&s.pool);
        return true;
    }
    // Probe (build/weapon-draw-probe): near draws and every draw the placement looked at: the tag's eye, the placement's result, and the model
    // matrix (3 rows of inverse(VP) * cb2 rows) under the eye it certified (else the tag's), 64 per eye and render.
    static void probe_draw(const View& view,const float* cur,const Shadow& sh,const ContextState& s,const weapon_draw::Result& r) noexcept {
        if(view.eye>1) return;
        const weapon_draw::Views* views=any_cached_views(view.frame);
        std::lock_guard<std::mutex> lock(probe_mutex_);
        if(probe_text_.size()>(24u<<20)) return;
        if(probe_frame_[view.eye]!=view.frame) {probe_frame_[view.eye]=view.frame;probe_in_frame_[view.eye]=0;}
        if(++probe_in_frame_[view.eye]>64) return;
        const unsigned eye_used=r.eye>=0?static_cast<unsigned>(r.eye):view.eye;
        char line[640];
        int n=std::snprintf(line,sizeof line,"D,%u,%u,%x,%u,%.3f,%u,%d,%d",view.frame,view.eye,static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(s.vs)>>4),sh.size/16,cur[15],static_cast<unsigned>(r.stage),r.side,r.eye);
        if(views && views->ok[eye_used]) {
            const auto m=weapon_draw::Place::model_rows(views->inv[eye_used].data(),cur);
            for(std::size_t k=0;k<12 && n>0 && n<600;++k) n+=std::snprintf(line+n,sizeof line-static_cast<std::size_t>(n),",%.5f",m[k]);
        }
        if(n>0 && n<620) {line[n]='\n';probe_text_.append(line,static_cast<std::size_t>(n)+1);count(weapon_probe_lines);}
    }
    // The two eye views' view-projections of an engine frame, and per side the previous published render's (0: none), built once per
    // frame from the eye records; null until both eyes' records of the frame exist.
    static const weapon_draw::Views* cached_views(std::uint32_t frame,std::uint32_t previous0,std::uint32_t previous1) noexcept {
        ViewsEntry& entry=views_cache_[frame&7];
        if(entry.ready.load(std::memory_order_acquire)!=frame+1 || entry.previous0!=previous0 || entry.previous1!=previous1) return nullptr;
        return &entry.views;
    }
    // The frame's built views whatever the previous-render key was (the probe's diagnostic model matrices only need the current ones).
    static const weapon_draw::Views* any_cached_views(std::uint32_t frame) noexcept {
        ViewsEntry& entry=views_cache_[frame&7];
        return entry.ready.load(std::memory_order_acquire)==frame+1?&entry.views:nullptr;
    }
    static const weapon_draw::Views* weapon_views(std::uint32_t frame,std::uint32_t previous0,std::uint32_t previous1) noexcept {        if(const auto* cached=cached_views(frame,previous0,previous1)) return cached;
        std::lock_guard<std::mutex> build(views_cache_mutex_);
        if(const auto* cached=cached_views(frame,previous0,previous1)) return cached;
        ViewsEntry& entry=views_cache_[frame&7];
        entry.ready.store(0,std::memory_order_release);
        weapon_draw::Views v{};
        const std::uint32_t previous[2]={previous0,previous1};
        {
            std::lock_guard<std::mutex> lock(views_mutex_);
            for(unsigned e=0;e<2;++e) for(const auto& r:eye_records_[e]) if(r.valid) {
                if(r.frame==frame) {v.vp[e]=weapon_draw::widen(r.m[2]);v.ok[e]=weapon_draw::inverse(v.vp[e],v.inv[e]);}
                for(unsigned s=0;s<2;++s) if(previous[s] && r.frame==previous[s]) {
                    v.prev_frame[s]=previous[s];
                    v.prev_vp[s][e]=weapon_draw::widen(r.m[2]);
                    v.prev_ok[s][e]=weapon_draw::inverse(v.prev_vp[s][e],v.prev_inv[s][e]);
                }
            }
        }
        if(!v.ok[0] || !v.ok[1]) return nullptr;
        entry.previous0=previous0;entry.previous1=previous1;entry.views=v;
        entry.ready.store(frame+1,std::memory_order_release);
        return &entry.views;
    }
    static ContextState* before(ID3D11DeviceContext* c) noexcept {
        if(!on()) return nullptr;
        ContextState* s=state(c);
        if(!s || !s->layout) return nullptr;
        const auto t=__rdtsc();
        const bool bound=prepare(c,*s);
        if(bound) set_cb_[s->type](c,2,1,&s->pool);
        count(ticks,__rdtsc()-t);
        return bound?s:nullptr;
    }

    template<int I> static void STDMETHODCALLTYPE set_vs_hook(ID3D11DeviceContext* c,ID3D11VertexShader* vs,ID3D11ClassInstance* const* classes,UINT n) {
        // Bookkeeping first (every call, including one the wide filter elides), then the filter on the forwarded arguments.
        const bool skip=observe_then_filter([&] {
            if(ContextState* s=state(c);s && vs!=s->vs) {s->vs=vs;s->layout=vs?layout_(vs):0;}
        },[&] {
            const auto filter=wide_vs_filter.load(std::memory_order_relaxed);
            return filter && filter(c,vs,classes,n);
        });
        if(!skip) set_vs_[I](c,vs,classes,n);
    }
    // b1/b2 as the game binds them; a range that does not start at the buffer's first constant is never shadowed.
    static void track(ID3D11DeviceContext* c,UINT start,UINT n,ID3D11Buffer* const* buffers,const UINT* first) noexcept {
        if(start>2 || start+n<2) return;
        ContextState* s=state(c);
        if(!s) return;
        for(UINT i=0;i<n;++i) if(start+i==1 || start+i==2) {
            ID3D11Buffer* b=buffers && !(first && first[i])?buffers[i]:nullptr;
            if(start+i==1 && b && b!=s->cb[0] && !learned(b)) learn(b,view_kind);
            s->cb[start+i-1]=b;
        }
    }
    template<int I> static void STDMETHODCALLTYPE set_cb_hook(ID3D11DeviceContext* c,UINT start,UINT n,ID3D11Buffer* const* buffers) {
        // ObjectMotion forwards the game's pointers unchanged here; the filter compares exactly those. Its own pool binds are
        // reported through wide_direct_cb_observer.
        const bool skip=observe_then_filter([&] {track(c,start,n,buffers,nullptr);},[&] {
            const auto filter=wide_cb_filter.load(std::memory_order_relaxed);
            return filter && filter(c,start,n,buffers);
        });
        if(!skip) set_cb_[I](c,start,n,buffers);
    }
    template<int I> static void STDMETHODCALLTYPE set_cb1_hook(ID3D11DeviceContext1* c,UINT start,UINT n,ID3D11Buffer* const* buffers,const UINT* first,const UINT* num) {
        if(const auto observe=wide_cb1_observer.load(std::memory_order_relaxed))
            observe(reinterpret_cast<ID3D11DeviceContext*>(c),start,n,buffers,first,num);
        track(c,start,n,buffers,first);set_cb1_[I](c,start,n,buffers,first,num);
    }
    template<int I> static HRESULT STDMETHODCALLTYPE map_hook(ID3D11DeviceContext* c,ID3D11Resource* r,UINT sub,D3D11_MAP type,UINT flags,D3D11_MAPPED_SUBRESOURCE* out) {
        const HRESULT hr=map_[I](c,r,sub,type,flags,out);
        if(SUCCEEDED(hr) && type==D3D11_MAP_WRITE_DISCARD && out && out->pData && r && on())
            if(Learned* e=shadowed(r)) pending_[pending_next_++&3]={c,r,out->pData,e};
        return hr;
    }
    template<int I> static void STDMETHODCALLTYPE unmap_hook(ID3D11DeviceContext* c,ID3D11Resource* r,UINT sub) {
        if(r) for(auto& p:pending_) if(p.resource==r && p.context==c) {p.resource=nullptr;written(c,r,p.data,*p.entry);break;}
        unmap_[I](c,r,sub);
    }
    template<int I> static void STDMETHODCALLTYPE update_hook(ID3D11DeviceContext* c,ID3D11Resource* r,UINT sub,const D3D11_BOX* box,const void* src,UINT rp,UINT dp) {
        if(!box && src && r && on()) if(Learned* e=shadowed(r)) written(c,r,src,*e);
        update_[I](c,r,sub,box,src,rp,dp);
    }
    template<int I> static void STDMETHODCALLTYPE update1_hook(ID3D11DeviceContext1* c,ID3D11Resource* r,UINT sub,const D3D11_BOX* box,const void* src,UINT rp,UINT dp,UINT flags) {
        if(!box && src && r && on()) if(Learned* e=shadowed(r)) written(c,r,src,*e);
        update1_[I](c,r,sub,box,src,rp,dp,flags);
    }

    static void poll() noexcept {
        ++poll_frames_;
        LARGE_INTEGER now{},frequency{};QueryPerformanceCounter(&now);QueryPerformanceFrequency(&frequency);
        if(last_poll_qpc_ && now.QuadPart-static_cast<LONGLONG>(last_poll_qpc_)<2*frequency.QuadPart) return;
        std::error_code error;
        int mode=1;
        if(std::filesystem::exists(knobs_/L"no-object-motion",error)) mode=-1;
        else if(std::ifstream knob{knobs_/L"object-motion-mode"}) knob>>mode;
        mode_.store(installed_?mode:-1,std::memory_order_relaxed);
        // v1019: the held weapon is a normal engine object by default. The tick's item advance already follows the drawn hand
        // (v1017 headset: 1.03 mm on the native pistol, 22 mm on the crossed sword, engine vs bound), while the per-draw per-eye
        // placement rewrites a near object's MVP/depth/motion vectors - the only object in the frame whose temporal inputs are
        // authored by the mod - which is what FSR/TAA turn into a black or ghosting weapon in one eye. build/weapon-draw-place
        // opts back into the round-6 binder (A/B and lab work); build/no-weapon-draw-place still forces it off.
        const bool draw_place_knob=std::filesystem::exists(knobs_/L"weapon-draw-place",error);
        const bool no_draw_place_knob=std::filesystem::exists(knobs_/L"no-weapon-draw-place",error);
        weapon_mode_.store(draw_place_knob && !no_draw_place_knob?1:0,std::memory_order_relaxed);
        weapon_probe_on_.store(!output_.empty() && std::filesystem::exists(knobs_/L"weapon-draw-probe",error),std::memory_order_relaxed);   // v1046: diagnostics off (empty output_) = no probe text is collected
        b1_eye_on_.store(!std::filesystem::exists(knobs_/L"no-object-motion-b1-eye",error),std::memory_order_relaxed); // v950
        alt_view_heuristic.store(std::filesystem::exists(knobs_/L"alt-view-heuristic",error),std::memory_order_relaxed); // v1199: the v1197 rules (A/B)
        if(std::ifstream tol{knobs_/L"alt-view-content-tol"}) {double f=0,r=0,fl=0;if(tol>>f>>r>>fl && f>0 && r>=1 && fl>=0) {content_fit_.store(f);content_ratio_.store(r);content_floor_.store(fl);}} // v1200
        if(std::ifstream tolerance{knobs_/L"weapon-draw-tol"}) { // "<orientation> <reach m> <identity>", for the model-matrix match
            double axes=0,reach=0,identity=0;
            if(tolerance>>axes>>reach>>identity && axes>0 && reach>0 && identity>0) {weapon_draw::Place::axes_tol.store(axes);weapon_draw::Place::reach.store(reach);weapon_draw::Place::identity_tol.store(identity);}
        }
        {
            std::string text;
            {std::lock_guard<std::mutex> lock(probe_mutex_);text.swap(probe_text_);}
            if(!text.empty() && !output_.empty()) {std::ofstream probe(output_/L"weapon-draw-probe.csv",std::ios::app|std::ios::binary);probe<<text;}
        }
        flush();
        const auto tick_now=__rdtsc();
        const double seconds=last_poll_qpc_?double(now.QuadPart-static_cast<LONGLONG>(last_poll_qpc_))/double(frequency.QuadPart):0;
        const double ns_per_tick=seconds>0?seconds*1e9/double(tick_now-last_poll_ticks_):0;
        static std::uint64_t last_ticks{},last_frames{};
        const auto spent=stats_[ticks].load(),frames=poll_frames_;
        const double us_per_frame=frames>last_frames?double(spent-last_ticks)*ns_per_tick/1000.0/double(frames-last_frames):0;
        static std::uint64_t last_weapon_ticks{};const auto weapon_spent=stats_[weapon_ticks].load();
        const double weapon_us=frames>last_frames?double(weapon_spent-last_weapon_ticks)*ns_per_tick/1000.0/double(frames-last_frames):0;last_weapon_ticks=weapon_spent;
        last_ticks=spent;last_frames=frames;last_poll_qpc_=static_cast<std::uint64_t>(now.QuadPart);last_poll_ticks_=tick_now;
        static constexpr const char* names[]={"layout_draws","motion","no_shadow","untagged","no_rows","guard_fail","fixed_left",
            "fixed_right","pool_fail","shadow_writes","oversize","view_writes","view_vp","view_v","view_none","learned_cb2","learned_view",
            "engine_motion_sum_left","engine_motion_sum_right","submitted_motion_sum_left","submitted_motion_sum_right",
            "engine_moving_left","engine_moving_right","submitted_moving_left","submitted_moving_right","corrected_left","corrected_right","camera_left","camera_right",
            "weapon_tsc_ticks_sampled","weapon_rejected","weapon_no_views","weapon_no_certificate","weapon_far_away","weapon_ambiguous",
            "weapon_unbound","weapon_placed_same_frame","weapon_placed_last_frame","weapon_placed_tagged","weapon_placed_pass",
            "weapon_placed_view0","weapon_placed_view1","weapon_label_wrong","weapon_engine_motion_sum_view0","weapon_engine_motion_sum_view1",
            "weapon_submitted_motion_sum_view0","weapon_submitted_motion_sum_view1","weapon_probe_lines","weapon_near_draws",
            "weapon_placed_current_only",
            "own_b1_agree","own_b1_disagree","own_b1_unknown","latest_b1_agree","latest_b1_disagree","latest_b1_unknown","b1_retagged","b1_lost"};
        static_assert(sizeof names/sizeof *names==ticks,"one name per counter before ticks");
        std::uint64_t proxy[5]{};
        if(proxy_stats_) proxy_stats_(proxy);
        if(output_.empty()) return;   // v1046: diagnostics off = no folder (output_/name would be relative); the throttle state above is already updated
        std::ofstream json(output_/L"object-motion.json",std::ios::trunc);
        json<<"{\"mode\":"<<mode<<",\"installed\":"<<(installed_?"true":"false")<<",\"proxy_layouts\":"<<(layout_?"true":"false")
            <<",\"proxy_shaders\":["<<proxy[0]<<','<<proxy[1]<<','<<proxy[2]<<','<<proxy[3]<<','<<proxy[4]
            <<"],\"proxy_fields\":\"created,with_previous,unstored,unhooked_devices,current_only\""
            <<",\"presents\":"<<frames<<",\"us_per_present\":"<<us_per_frame;
        for(unsigned i=0;i<ticks;++i) json<<",\""<<names[i]<<"\":"<<stats_[i].load();
        json<<",\"weapon_us_per_present\":"<<weapon_us<<",\"weapon_mode\":"<<weapon_mode_.load()<<",\"held_placing\":"<<(held_placing_.load()?1:0)<<",\"weapon_published\":"<<weapon_draw::Place::published()
            <<",\"weapon_status\":"<<weapon_status_json();
        json<<",\"b1_eye_mode\":"<<(b1_eye_on_.load()?1:0)<<",\"taa_b1\":\""; // v950: eyes' TAA b1; sampled draws' VS b1 "buffer:own L/own R/fallback"
        for(unsigned e=0;e<2;++e) for(const auto& p:taa_b1_[e]) json<<(e?'R':'L')<<std::hex<<reinterpret_cast<std::uintptr_t>(p.load())<<std::dec<<' ';
        json<<"\",\"vs_b1_sampled\":\"";
        for(const auto& e:b1_seen_) if(const void* b=e.buffer.load()) json<<std::hex<<reinterpret_cast<std::uintptr_t>(b)<<std::dec<<':'<<e.n[0].load()<<'/'<<e.n[1].load()<<'/'<<e.n[2].load()<<' ';
        json<<'"';
        json<<",\"kinds\":\"same,own_last,other_now,other_last,unproven,miss\",\"alternate\":"<<alternate_status_json();
        json<<",\"oversize_kb\":[";for(unsigned k=0;k<oversize_kb_.size();++k) json<<(k?",":"")<<oversize_kb_[k].load();json<<']';
        for(unsigned eye=0;eye<2;++eye) {
            json<<(eye?",\"right\":[":",\"left\":[");
            for(unsigned k=0;k<PrevMvpTable::kinds;++k) json<<(k?",":"")<<stats_[(eye?kind_r:kind_l)+k].load();
            json<<(eye?"],\"near_right\":[":"],\"near_left\":[");
            for(unsigned k=0;k<PrevMvpTable::kinds;++k) json<<(k?",":"")<<stats_[(eye?near_r:near_l)+k].load();
            json<<(eye?"],\"unmatched_cb2_float4s_right\":\"":"],\"unmatched_cb2_float4s_left\":\"");
            for(unsigned n=0,first=1;n<64;++n) if(const auto v=unmatched_size_[eye][n].load();v) {json<<(first?"":" ")<<n<<':'<<v;first=0;}
            json<<'"';
        }
        json<<"}\n";
    }
public:
    // Around each game draw: binds this draw's corrected b2 copy when it needs one, and the game's b2 again after it.
    class DrawScope {
    public:
        // v1210: a draw hook entered from inside another one (RigMask::nested_draw) is the same draw: the outer scope already bound its rows
        explicit DrawScope(ID3D11DeviceContext* c) noexcept:c_(c),nested_(RigMask::nested_draw()),s_(installed_ && !nested_?before(c):nullptr) {}
        ~DrawScope() {
            if(nested_) return;
            SkinMotion::restore(c_); // v1182: the engine's previous palette at t0 again
            if(!s_) return;
            if(const auto observe=wide_direct_cb_observer.load(std::memory_order_relaxed)) observe(c_,2,1,&s_->cb[1]);
            set_cb_[s_->type](c_,2,1,&s_->cb[1]);
        }
        DrawScope(const DrawScope&)=delete;
        DrawScope& operator=(const DrawScope&)=delete;
    private:
        ID3D11DeviceContext* c_;bool nested_;ContextState* s_;
    };
};
// v1101 status, in arm_flicker.weapon_draw (and object-motion.json): per hand published/placed and skipped by stage, and every render pass
// that saw a near weapon draw with how many of them were placed (per hand). "near" is the count of candidate draws, not a rejection count.
inline std::string ObjectMotion::weapon_status_json() {
    std::ostringstream out;
    out<<"{\"published\":["<<weapon_draw::Place::published(0)<<','<<weapon_draw::Place::published(1)<<']'
       <<",\"placed\":["<<weapon_placed_side_[0].load(std::memory_order_relaxed)<<','<<weapon_placed_side_[1].load(std::memory_order_relaxed)<<']'
       <<",\"alt\":["<<weapon_alt_side_[0].load(std::memory_order_relaxed)<<','<<weapon_alt_side_[1].load(std::memory_order_relaxed)<<']'
       <<",\"skip\":[";
    // One name per weapon_draw::Stage, in enum order. v1016 read past this list (`unbound` was added to the enum but not here) when
    // a draw was actually placed, and the out-of-bounds pointer faulted every status poll after a save load. The static_asserts
    // keep the list and the counters tied to the enum.
    static constexpr const char* stage_names[]={"rejected","no_views","no_certificate","far_away","ambiguous","unbound",
        "placed_same_frame","placed_last_frame","placed_tagged","placed_pass"};
    static_assert(sizeof stage_names/sizeof *stage_names==weapon_draw::stages,"one stage name per weapon_draw::Stage");
    for(unsigned side=0;side<2;++side) {
        out<<(side?",{":"{");
        bool first=true;
        for(unsigned s=1;s<weapon_draw::stages;++s) { // stage 0 (not the weapon's) is the scene: no side is involved
            const auto n=weapon_skip_side_[side][s].load(std::memory_order_relaxed);
            if(!n) continue;
            out<<(first?"":",")<<"\""<<stage_names[s]<<"\":"<<n;first=false;
        }
        out<<'}';
    }
    out<<"],\"passes\":[";
    const auto name=pass_name_.load(std::memory_order_relaxed);
    bool first_pass=true;
    for(unsigned i=0;i<pass_slots;++i) {
        const auto near_count=weapon_pass_near_[i].load(std::memory_order_relaxed);
        if(!near_count) continue;
        out<<(first_pass?"":",");
        out<<"{\"id\":";
        if(i+1==pass_slots) out<<"-1,\"name\":\"untagged\"";
        else out<<i<<",\"name\":\""<<(name?name(i):"pass")<<"\"";
        out<<",\"near\":"<<near_count<<",\"matched\":"<<weapon_pass_matched_[i].load(std::memory_order_relaxed)
           <<",\"placed\":"<<weapon_pass_placed_[i].load(std::memory_order_relaxed)
           <<",\"left\":"<<weapon_pass_side_[0][i].load(std::memory_order_relaxed)
           <<",\"right\":"<<weapon_pass_side_[1][i].load(std::memory_order_relaxed)<<'}';
        first_pass=false;
    }
    out<<"]}";
    return out.str();
}
} // namespace d2vr
