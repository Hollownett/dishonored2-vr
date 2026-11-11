#pragma once
// v1162 performance mode: the player-rig mask per eye as a stencil bit of the eye's scene depth (FsrTaa copies it into Eye::rig).
//
// The link from a draw to the rig: a skinned rig model (both arms, the full-body avatar) owns two GPU skin-palette buffers, the wrappers
// at render model +0x2008 / +0x2040 (native_note_source.hpp, the GPU copy 0x25f930 fills them), and every draw of that model binds one of
// them at VS t0..t2 (v663 note-draw census: the depth-prepass variant binds current + previous at t0/t1, the colour pass the current at t2;
// R32G32B32A32_FLOAT BUFFEREX views). A buffer belongs to exactly one model, so "a rig skin buffer is bound" is exact in paired and in
// alternate mode, needs no matrices, no eye tag and no tolerance. The models are the ones the arm upload hook names (image_skin_slots).
// Not covered: rigid models (held weapons and props, dual-wield copies) and NPC models the hands hold (a carried body, severed parts).
//
// The mark: in each pass whose view is the alternate one-view (RenderPassMap item+0x70), every depth-WRITING draw gets a depth-stencil
// state equal to the bound one plus the write of `bit` on depth pass (REPLACE, ref bit = 1 for a rig draw, 0 for any other), so the bit
// at a pixel always belongs to the last surface that wrote its depth, whatever the draw order. The derived state is cached per engine
// state (the engine state is kept referenced, so its address cannot be reused) and left bound after a non-rig draw (the next game
// OMSetDepthStencilState replaces it); after a rig draw the clear ref is restored. Draws whose state does not allow the derivation
// (stencil ops on fail, INCR/DECR..., mixed faces) keep their state and are counted.
//
// Safety: a census of every depth-stencil state those passes bind. The bit must be outside the read mask of any state that tests stencil
// and outside the write mask of any state that writes stencil (measured stencil values 0, 4 glass, 32 marker: bit 7 is free if the census
// agrees). Marking starts after `warmup` gameplay Presents with a clean census and stops for the session at the first conflicting state
// (status reason). Stencil-plane reads by shaders (an SRV of the stencil plane) are not visible to the census.
// Knob build/no-stale-eye-rig (dxgi_probe): no detection, no marking, the raw stencil copy as in v1160.
#include <d3d11.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <sstream>
#include <string>

namespace d2vr {
inline constexpr UINT8 rig_stencil_bit=0x80;
inline D3D11_DEPTH_STENCIL_DESC default_depth_stencil_desc() noexcept { // the state a null OMSetDepthStencilState binds
    D3D11_DEPTH_STENCIL_DESC d{};d.DepthEnable=TRUE;d.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;d.DepthFunc=D3D11_COMPARISON_LESS;
    d.StencilEnable=FALSE;d.StencilReadMask=D3D11_DEFAULT_STENCIL_READ_MASK;d.StencilWriteMask=D3D11_DEFAULT_STENCIL_WRITE_MASK;
    d.FrontFace={D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_KEEP,D3D11_COMPARISON_ALWAYS};d.BackFace=d.FrontFace;
    return d;
}
struct RigStencilInfo {bool writes_depth{},tests{},writes{},conflict{},derivable{};D3D11_DEPTH_STENCIL_DESC derived{};};
// Pure: what an engine state means for the mark, and the derived state (valid when derivable).
inline RigStencilInfo rig_stencil_info(const D3D11_DEPTH_STENCIL_DESC& d,UINT8 bit) noexcept {
    RigStencilInfo r{};
    r.writes_depth=d.DepthEnable && d.DepthWriteMask==D3D11_DEPTH_WRITE_MASK_ALL;
    const auto tests=[](const D3D11_DEPTH_STENCILOP_DESC& f) {return f.StencilFunc!=D3D11_COMPARISON_ALWAYS && f.StencilFunc!=D3D11_COMPARISON_NEVER;};
    const auto writes=[](const D3D11_DEPTH_STENCILOP_DESC& f) {return f.StencilFailOp!=D3D11_STENCIL_OP_KEEP || f.StencilDepthFailOp!=D3D11_STENCIL_OP_KEEP || f.StencilPassOp!=D3D11_STENCIL_OP_KEEP;};
    if(d.StencilEnable) {
        r.tests=tests(d.FrontFace) || tests(d.BackFace);r.writes=writes(d.FrontFace) || writes(d.BackFace);
        r.conflict=(r.tests && (d.StencilReadMask&bit)) || (r.writes && (d.StencilWriteMask&bit));
    }
    if(!r.writes_depth || r.conflict) return r;
    r.derived=d;
    const D3D11_DEPTH_STENCILOP_DESC mark{D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_REPLACE,D3D11_COMPARISON_ALWAYS};
    if(!d.StencilEnable) {
        r.derived.StencilEnable=TRUE;r.derived.StencilReadMask=0;r.derived.StencilWriteMask=bit;r.derived.FrontFace=mark;r.derived.BackFace=mark;
        r.derivable=true;return r;
    }
    const auto keep_fails=[](const D3D11_DEPTH_STENCILOP_DESC& f) {return f.StencilFailOp==D3D11_STENCIL_OP_KEEP && f.StencilDepthFailOp==D3D11_STENCIL_OP_KEEP;};
    if(!keep_fails(d.FrontFace) || !keep_fails(d.BackFace)) return r; // a fail op would write the bit where the depth was not written
    const auto pf=d.FrontFace.StencilPassOp,pb=d.BackFace.StencilPassOp;
    if(pf==D3D11_STENCIL_OP_REPLACE && pb==D3D11_STENCIL_OP_REPLACE) {r.derived.StencilWriteMask=static_cast<UINT8>(d.StencilWriteMask|bit);r.derivable=true;}
    else if(pf==D3D11_STENCIL_OP_KEEP && pb==D3D11_STENCIL_OP_KEEP) { // the engine writes nothing: only the bit is written
        r.derived.StencilWriteMask=bit;r.derived.FrontFace.StencilPassOp=D3D11_STENCIL_OP_REPLACE;r.derived.BackFace.StencilPassOp=D3D11_STENCIL_OP_REPLACE;r.derivable=true;
    }
    return r;
}
// Pure: the ref a marked draw binds (the bit's value; the engine's other ref bits unchanged).
inline UINT rig_stencil_ref(UINT engine_ref,bool rig,UINT8 bit) noexcept {return (engine_ref&~UINT{bit})|(rig?UINT{bit}:0u);}

class RigStencil {
public:
    static constexpr UINT8 bit=rig_stencil_bit;
    static constexpr unsigned warmup=90,buffer_slots=6;
    inline static std::atomic<bool> enabled{};          // dxgi_probe, each Present: alternate-eye on and build/no-stale-eye-rig absent
    inline static std::atomic<std::uintptr_t> view{};    // the alternate one-view whose passes are marked (0: a paired frame, nothing marked)
    // Present thread: the rig models' skin buffers (current and previous of each arm and of the body; 0 = none).
    static void publish(const std::array<std::uintptr_t,buffer_slots>& buffers) noexcept {
        for(unsigned i=0;i<buffer_slots;++i) buffers_[i].store(buffers[i],std::memory_order_relaxed);
        unsigned n=0;for(auto b:buffers) n+=b!=0;published_buffers_.store(n,std::memory_order_relaxed);
    }
    // Present thread, once a Present: arms the marking after a clean warmup; per-Present counters.
    static void present(bool gameplay) noexcept {
        const bool on=enabled.load(std::memory_order_relaxed);
        if(!on || conflict_.load(std::memory_order_relaxed)) {armed_.store(false,std::memory_order_relaxed);clean_presents_=0;}
        else if(gameplay && view.load(std::memory_order_relaxed) && ++clean_presents_>=warmup && !armed_.load(std::memory_order_relaxed)) {
            armed_.store(true,std::memory_order_relaxed);armed_present_=presents_;
        }
        ++presents_;
        live_.store(armed_.load(std::memory_order_relaxed) && presents_>=armed_present_+2,std::memory_order_relaxed);
        rig_draws_last_.store(rig_draws_frame_.exchange(0,std::memory_order_relaxed),std::memory_order_relaxed);
    }
    static bool live() noexcept {return live_.load(std::memory_order_relaxed);} // marked for at least two whole Presents
    static std::uint64_t take_marks() noexcept {return marks_since_.exchange(0,std::memory_order_relaxed);} // depth-writing draws marked since the last call
    static bool draw_is_rig() noexcept {return tl_rig_;} // the draw now in a Scope binds a rig skin buffer (ObjectMotion)
    static std::string json() {
        std::ostringstream o;
        o<<"{\"enabled\":"<<(enabled.load()?"true":"false")<<",\"armed\":"<<(armed_.load()?"true":"false")<<",\"live\":"<<(live_.load()?"true":"false")
         <<",\"bit\":"<<unsigned(bit)<<",\"buffers\":"<<published_buffers_.load()<<",\"rig_draws_last_present\":"<<rig_draws_last_.load()<<",\"rig_draws\":"<<rig_draws_.load()
         <<",\"marked\":"<<marked_.load()<<",\"swaps\":"<<swaps_.load()<<",\"underivable\":"<<underivable_.load()<<",\"states\":"<<states_.load()
         <<",\"census\":{\"read_mask\":"<<read_union_.load()<<",\"write_mask\":"<<write_union_.load()<<",\"refs\":"<<ref_union_.load()
         <<",\"stencil_states\":"<<stencil_states_.load()<<",\"conflicts\":"<<conflicts_.load()<<"},\"reason\":\""
         <<(conflict_.load()?"conflict":!enabled.load()?"off":armed_.load()?"marking":"warmup")<<"\",\"failures\":"<<failures_.load()<<'}';
        return o.str();
    }
    // Around one game draw (job threads). Inert unless enabled and the thread's pass renders the alternate view.
    class Scope {
    public:
        Scope(ID3D11DeviceContext* c,std::uintptr_t pass_view) noexcept {
            tl_rig_=false;
            if(!c || !pass_view || !enabled.load(std::memory_order_relaxed) || pass_view!=view.load(std::memory_order_relaxed)) return;
            ID3D11DepthStencilState* bound{};UINT ref{};c->OMGetDepthStencilState(&bound,&ref);
            const Entry* e=find(bound);
            if(!e) e=learn(c,bound,ref);
            if(bound) bound->Release(); // the cache keeps its own reference
            if(!e) return;
            const Entry* g=e->game?e->game:e; // the engine's state (e: ours, bound since an earlier draw)
            if(!g->writes_depth) return;
            tl_rig_=is_rig(c);
            if(tl_rig_) {rig_draws_.fetch_add(1,std::memory_order_relaxed);rig_draws_frame_.fetch_add(1,std::memory_order_relaxed);}
            if(!armed_.load(std::memory_order_relaxed)) { // stopped (a conflict) with one of ours still bound: the engine's own state again
                if(e->game) c->OMSetDepthStencilState(e->game==&default_?nullptr:e->game->key.load(std::memory_order_relaxed),ref&~UINT{bit});
                return;
            }
            if(!g->derived) {underivable_.fetch_add(1,std::memory_order_relaxed);return;}
            const UINT want=rig_stencil_ref(ref,tl_rig_,bit);
            if(bound!=g->derived || ref!=want) {c->OMSetDepthStencilState(g->derived,want);swaps_.fetch_add(1,std::memory_order_relaxed);}
            marked_.fetch_add(1,std::memory_order_relaxed);marks_since_.fetch_add(1,std::memory_order_relaxed);
            if(tl_rig_) {c_=c;restore_=g->derived;restore_ref_=rig_stencil_ref(ref,false,bit);}
        }
        ~Scope() {
            if(restore_) c_->OMSetDepthStencilState(restore_,restore_ref_);
            tl_rig_=false;
        }
        Scope(const Scope&)=delete;Scope& operator=(const Scope&)=delete;
    private:
        ID3D11DeviceContext* c_{};ID3D11DepthStencilState* restore_{};UINT restore_ref_{};
    };
private:
    struct Entry {
        std::atomic<ID3D11DepthStencilState*> key{};std::atomic<bool> used{}; // `used` with key: a null key is the default state
        const Entry* game{};ID3D11DepthStencilState* derived{};bool writes_depth{};
    };
    static constexpr unsigned slots=1024;
    inline static std::array<Entry,slots> table_{};inline static Entry default_{};inline static std::atomic<bool> default_ready_{};
    inline static std::mutex mutex_;
    inline static std::array<std::atomic<std::uintptr_t>,buffer_slots> buffers_{};inline static std::atomic<unsigned> published_buffers_{};
    inline static std::atomic<bool> armed_{},live_{},conflict_{};
    inline static unsigned clean_presents_{};inline static std::uint64_t presents_{},armed_present_{}; // Present thread only
    inline static std::atomic<std::uint64_t> rig_draws_{},rig_draws_frame_{},rig_draws_last_{},marked_{},marks_since_{},swaps_{},underivable_{},states_{},
        stencil_states_{},conflicts_{},failures_{};
    inline static std::atomic<unsigned> read_union_{},write_union_{},ref_union_{};
    inline static thread_local bool tl_rig_{};

    static unsigned hash(const void* p) noexcept {return static_cast<unsigned>((reinterpret_cast<std::uintptr_t>(p)>>4)*0x9e3779b97f4a7c15ull>>54);}
    static const Entry* find(ID3D11DepthStencilState* s) noexcept {
        if(!s) return default_ready_.load(std::memory_order_acquire)?&default_:nullptr;
        const unsigned h=hash(s);
        for(unsigned i=0;i<slots;++i) {
            const Entry& e=table_[(h+i)&(slots-1)];
            if(!e.used.load(std::memory_order_acquire)) return nullptr;
            if(e.key.load(std::memory_order_relaxed)==s) return &e;
        }
        return nullptr;
    }
    static Entry* claim(ID3D11DepthStencilState* s) noexcept { // under mutex_
        const unsigned h=hash(s);
        for(unsigned i=0;i<slots;++i) {Entry& e=table_[(h+i)&(slots-1)];if(!e.used.load(std::memory_order_relaxed)) return &e;}
        return nullptr;
    }
    static const Entry* learn(ID3D11DeviceContext* c,ID3D11DepthStencilState* s,UINT ref) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        if(const Entry* e=find(s)) return e;
        D3D11_DEPTH_STENCIL_DESC d=default_depth_stencil_desc();if(s) s->GetDesc(&d);
        const auto info=rig_stencil_info(d,bit);
        if(d.StencilEnable) {
            stencil_states_.fetch_add(1,std::memory_order_relaxed);ref_union_.fetch_or(ref&0xff,std::memory_order_relaxed);
            if(info.tests) read_union_.fetch_or(d.StencilReadMask,std::memory_order_relaxed);
            if(info.writes) write_union_.fetch_or(d.StencilWriteMask,std::memory_order_relaxed);
        }
        if(info.conflict) {conflicts_.fetch_add(1,std::memory_order_relaxed);conflict_.store(true,std::memory_order_relaxed);armed_.store(false,std::memory_order_relaxed);live_.store(false,std::memory_order_relaxed);}
        Entry* e=s?claim(s):&default_;
        if(!e) {failures_.fetch_add(1,std::memory_order_relaxed);return nullptr;}
        ID3D11DepthStencilState* derived{};
        if(info.derivable) {
            ID3D11Device* device{};c->GetDevice(&device);
            if(!device || FAILED(device->CreateDepthStencilState(&info.derived,&derived))) {derived=nullptr;failures_.fetch_add(1,std::memory_order_relaxed);}
            if(device) device->Release();
        }
        e->game=nullptr;e->derived=derived;e->writes_depth=info.writes_depth;
        if(s) {s->AddRef();e->key.store(s,std::memory_order_relaxed);e->used.store(true,std::memory_order_release);} else default_ready_.store(true,std::memory_order_release);
        states_.fetch_add(1,std::memory_order_relaxed);
        // ours, so a draw that finds it still bound knows the engine state behind it (a dedup with an engine state already cached keeps that entry)
        if(derived && !find(derived)) if(Entry* o=claim(derived)) {
            o->game=e;o->derived=nullptr;o->writes_depth=true;derived->AddRef();
            o->key.store(derived,std::memory_order_relaxed);o->used.store(true,std::memory_order_release);
        }
        return e;
    }
    static bool is_rig(ID3D11DeviceContext* c) noexcept {
        if(!published_buffers_.load(std::memory_order_relaxed)) return false;
        ID3D11ShaderResourceView* views[3]{};c->VSGetShaderResources(0,3,views);
        bool hit=false;
        for(auto* v:views) if(v) {
            ID3D11Resource* r{};v->GetResource(&r);
            const auto at=reinterpret_cast<std::uintptr_t>(r);
            for(const auto& b:buffers_) hit=hit || (at && b.load(std::memory_order_relaxed)==at);
            if(r) r->Release();
            v->Release();
        }
        return hit;
    }
};
} // namespace d2vr
