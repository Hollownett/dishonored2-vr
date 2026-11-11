#pragma once
// v1163 performance mode: the player-rig mask per eye, from the rig draws themselves (replaces v1162's stencil bit: the sim census found
// the engine's depth-stencil states read 0xEF and write 0xBF of the stencil, refs OR 0xFF: no free bit).
//
// The link from a draw to the rig (v1162, sim-proven: 6 buffers, 12 rig draws per Present): a skinned rig model (both arms, the full-body
// avatar) owns two GPU skin-palette buffers (render model +0x2008 / +0x2040), and its draws bind one of them at VS t0..t2. Exact in any
// mode. Not covered (by decision): held weapons and props (the mover rules take them), NPC models the hands hold.
//
// The mask: right after each rig draw that writes the scene depth in a pass of the alternate one-view, the SAME draw is replayed with this
// file's pixel shader (SV_Position.z) into a rig-depth target (R32_FLOAT, the scene depth's size), the engine's depth view bound for the test
// with depth EQUAL, no depth write, no stencil, no blending: the rig depth where the rig won the depth test then. The engine's targets, depth
// state, blend state and pixel shader are restored; nothing of the engine's is written. Visibility is resolved at the copy (FsrTaa):
// Eye::rig = 255 where scene depth > 0 and |rig depth - scene depth| <= eps, so anything drawn over the rig later, in any order, wins.
// The rig target is cleared after each copy. The PS reads only SV_Position: system-value position links with any vertex shader that writes
// it, whatever its other outputs (checked on WARP with SV_Position not the first output, tests/rig_mask_check.cpp).
// Knob build/no-stale-eye-rig (dxgi_probe): inert (no detection, no replay, no mask).
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <sstream>
#include <string>

namespace d2vr {
// The copy/compare: t0 scene depth (float view), t1 rig depth; u0 Eye::rig (R8_UINT), u1 counters: [0] mask pixels, [1] near misses
// (rig depth present, eps < |difference| <= 64 eps: an eps too tight shows here). k.xy eps = x + y * depth, k.zw the render rectangle.
inline constexpr char rig_mask_copy_source[]=
    "Texture2D<float> z:register(t0);Texture2D<float> r:register(t1);RWTexture2D<uint> o:register(u0);RWByteAddressBuffer n:register(u1);"
    "cbuffer C:register(b0){float4 k;};groupshared uint g[2];"
    "[numthreads(8,8,1)] void main(uint3 i:SV_DispatchThreadID,uint t:SV_GroupIndex){"
    "if(t<2) g[t]=0;GroupMemoryBarrierWithGroupSync();"
    "const bool inside=all(float2(i.xy)<k.zw);const float d=z.Load(int3(i.xy,0)),q=r.Load(int3(i.xy,0));"
    "const float e=k.x+k.y*d,a=abs(q-d);const bool rig=inside && d>0 && q>0 && a<=e;"
    "o[i.xy]=rig?255:0;"
    "if(rig) InterlockedAdd(g[0],1);else if(inside && d>0 && q>0 && a<=64*e) InterlockedAdd(g[1],1);"
    "GroupMemoryBarrierWithGroupSync();if(t<2 && g[t]) n.InterlockedAdd(t*4,g[t]);}";
// eps for a depth view format: D24 one step is 2^-24 (the replay's float z vs the stored 24-bit value: half a step + rounding); float: 4 ulp.
inline void rig_mask_eps(DXGI_FORMAT depth_srv,float& absolute,float& relative) noexcept {
    if(depth_srv==DXGI_FORMAT_R24_UNORM_X8_TYPELESS || depth_srv==DXGI_FORMAT_D24_UNORM_S8_UINT) {absolute=2.f/16777215.f;relative=0;}
    else if(depth_srv==DXGI_FORMAT_R16_UNORM || depth_srv==DXGI_FORMAT_D16_UNORM) {absolute=2.f/65535.f;relative=0;}
    else {absolute=1e-9f;relative=4.f/8388608.f;}
}

// v1166: rig motion vectors in alternate mode. The skinned depth-prepass VS (arkdefault/vertex TransformVertex, USESKINNING) skins the
// previous position with ro_previousframeskinningmatrices = VS t0 and the current with ro_matrices = VS t1 (v663 draw census: t0 = the
// model's previous wrapper, t1 its current; the colour pass binds the current at t2), then multiplies with cb_previousmvpmatrix rows.
// The engine ping-pongs the two wrappers every engine frame (0x25dfd0: previous index = current, current ^= 1 while dirty; 0x25f930
// writes the CPU palette into the new current; 0x25da80 supplies the current as "previous" while +0x1fb0 has 4/0x10/0x20), so the
// previous palette is the last ENGINE frame's: in alternate mode the other eye's render one tick ago. Its rows are the other eye's too
// (raw). This file keeps, per eye and rig model, the palette of that eye's own last render (two GPU slots), and ObjectMotion binds it at
// t0 together with the eye's own previous rows; either missing, both become the current ones (zero motion), counted.
// Pure: the history slots for `frame` from the two slots' frames (stored +1, 0 = empty): read = the slot holding frame - step (-1 none),
// write = the other one (done: this frame was copied already).
// v1167: keys carry an epoch (high 32 bits): a new epoch at each start of the one-view mode and when the engine frame counter goes back
// (a load restarts it), so a slot written in an earlier session can never pass for this eye's previous render.
struct RigHistorySlots {int read{-1};unsigned write{};bool done{};};
inline std::uint64_t rig_history_key(std::uint32_t epoch,std::uint32_t frame) noexcept {return std::uint64_t{epoch}<<32|std::uint32_t(frame+1);}
inline RigHistorySlots rig_history_slots(std::uint64_t key0,std::uint64_t key1,std::uint32_t epoch,std::uint32_t frame,unsigned step) noexcept {
    RigHistorySlots s{};const std::uint64_t want=frame>=step?rig_history_key(epoch,frame-step):~std::uint64_t{0},now=rig_history_key(epoch,frame);
    s.read=key0==want?0:key1==want?1:-1;
    if(key0==now) {s.write=0;s.done=true;} else if(key1==now) {s.write=1;s.done=true;} else s.write=s.read==0?1u:0u;
    return s;
}

class RigMask {
public:
    static constexpr unsigned buffer_slots=6;
    inline static std::atomic<bool> enabled{};          // dxgi_probe, each Present: alternate-eye on and build/no-stale-eye-rig absent
    inline static std::atomic<std::uintptr_t> view{};    // the alternate one-view whose passes are replayed (0: a paired frame)
    struct DrawArgs {unsigned kind{};UINT count{},instances{1},start{};INT base{};UINT first{};}; // 0 DrawIndexed 1 ..Instanced 2 Draw 3 DrawInstanced
    using DrawFn=void(*)(ID3D11DeviceContext*,const DrawArgs&) noexcept;               // the engine's draw, past every hook
    using TargetsFn=void(*)(ID3D11DeviceContext*,UINT,ID3D11RenderTargetView* const*,ID3D11DepthStencilView*) noexcept; // OMSetRenderTargets, past the hooks
    static void publish(const std::array<std::uintptr_t,buffer_slots>& buffers) noexcept {
        unsigned n=0;
        for(unsigned i=0;i<buffer_slots;++i) {buffers_[i].store(buffers[i],std::memory_order_relaxed);n+=buffers[i]!=0;}
        published_buffers_.store(n,std::memory_order_relaxed);
    }
    static void present() noexcept { // Present thread
        rig_draws_last_.store(rig_draws_frame_.exchange(0,std::memory_order_relaxed),std::memory_order_relaxed);
        replays_last_.store(replays_frame_.exchange(0,std::memory_order_relaxed),std::memory_order_relaxed);
    }
    static bool draw_is_rig() noexcept {return tl_rig_;} // the draw now in a Scope binds a rig skin buffer (ObjectMotion)
    // v1166, ObjectMotion at a rig draw (alternate): binds at VS t0 this eye's own previous palette when `rows_own` (the draw gets the eye's
    // own previous rows) and that palette exists (this eye's render `frame - 2`), else the current palette (zero motion; the caller then
    // uses the current rows too). Records the current palette for this eye's next render. True: the own pair is in place. Restored in after().
    // v1167: called for every rig draw that has previous rows (a motion-vector writer). One whose palette bindings are not the prepass
    // form (t0/t1 the model's wrappers; t0 == t1 when the engine itself reset the history) cannot be zeroed: its render is not valid.
    static bool previous_palette(ID3D11DeviceContext* c,int eye,std::uint32_t frame,bool rows_own) noexcept {
        if(!tl_rig_ || !c) return false;
        if(!tl_prepass_ || !tl_current_) {
            // v1168: no previous-palette slot to set. Only a draw that writes a colour target (the prepass's motion-vector target) can carry
            // motion; a depth-only draw (no render target bound) cannot, and never touches validity.
            std::array<ID3D11RenderTargetView*,D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> rtvs{};c->OMGetRenderTargets(static_cast<UINT>(rtvs.size()),rtvs.data(),nullptr);
            UINT targets=0;for(auto* r:rtvs) if(r) {++targets;r->Release();}
            const unsigned p=tl_pattern_%625;
            if(targets) {note_frame(frame,false);unpaired_.fetch_add(1,std::memory_order_relaxed);unpaired_patterns_[p].fetch_add(1,std::memory_order_relaxed);
                unpaired_pass_.store(tl_pass_,std::memory_order_relaxed);unpaired_targets_.store(targets,std::memory_order_relaxed);}
            else {depth_only_.fetch_add(1,std::memory_order_relaxed);depth_only_patterns_[p].fetch_add(1,std::memory_order_relaxed);depth_only_pass_.store(tl_pass_,std::memory_order_relaxed);}
            return false;
        }
        const std::uint32_t epoch=epoch_for(frame);
        ID3D11ShaderResourceView* bind=tl_current_;bool own=false;
        if(eye==0 || eye==1) if(History* h=history(c,static_cast<unsigned>(eye),tl_model_)) {
            const auto pick=rig_history_slots(h->key[0].load(std::memory_order_acquire),h->key[1].load(std::memory_order_acquire),epoch,frame,2);
            if(!pick.done) {
                const auto now=rig_history_key(epoch,frame);
                std::uint64_t old=h->key[pick.write].load(std::memory_order_relaxed);
                if(old!=now && h->key[pick.write].compare_exchange_strong(old,now,std::memory_order_acq_rel)) {
                    ID3D11Resource* current{};tl_current_->GetResource(&current);
                    if(current) {c->CopyResource(h->buffer[pick.write].Get(),current);current->Release();history_copies_.fetch_add(1,std::memory_order_relaxed);}
                }
            }
            if(rows_own && pick.read>=0) {bind=h->view[static_cast<unsigned>(pick.read)].Get();own=true;}
            else if(rows_own) no_history_.fetch_add(1,std::memory_order_relaxed);
        }
        if(!tl_saved_set_) {c->VSGetShaderResources(tl_prev_slot_,1,&tl_saved_t0_);tl_saved_set_=true;tl_saved_slot_=tl_prev_slot_;}
        c->VSSetShaderResources(tl_prev_slot_,1,&bind);
        (own?own_pairs_:zero_pairs_).fetch_add(1,std::memory_order_relaxed);note_frame(frame,own);
        return own;
    }
    // FsrTaa's motion pass, once a render (`frame` = its receipt's engine frame): every rig motion-vector draw of exactly that render had
    // its own pair, and there was at least one. v1167: per engine frame (was: since the last call, which a pipelined next render could feed).
    static bool take_rig_motion(std::uint32_t frame) noexcept {
        FrameTally& t=tally_[frame&7];
        if(t.frame.load(std::memory_order_acquire)!=frame+1) return false;
        return t.own.load(std::memory_order_relaxed)>0 && t.zero.load(std::memory_order_relaxed)==0;
    }
    // dxgi_probe: the one-view mode (re)starts (the alternate view appears after paired frames or a mode change): a new history epoch.
    static void begin_epoch() noexcept {epoch_.fetch_add(1,std::memory_order_relaxed);max_frame_.store(0,std::memory_order_relaxed);}
    // FsrTaa's copy: draws of the alternate view seen since the last call (0: no one-view render, the mask is not live), and the rig target
    // of this scene depth (empty: none yet / another depth).
    static std::uint64_t take_view_draws() noexcept {return view_draws_.exchange(0,std::memory_order_relaxed);}
    static bool target(ID3D11Resource* depth,Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>& srv,Microsoft::WRL::ComPtr<ID3D11RenderTargetView>& rtv) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(!depth || target_.depth!=depth || !target_.srv) return false;
        srv=target_.srv;rtv=target_.rtv;return true;
    }
    static void reset() noexcept {std::lock_guard<std::mutex> lock(mutex_);target_={};ps_.Reset();equal_.Reset();blend_.Reset();device_=nullptr;made_=make_failed_=false;}
    static std::string json() {
        std::ostringstream o;
        // v1168: t0..t3 per digit: 0 none, 1 this model's wrapper 0, 2 its wrapper 1, 3 another rig model's, 4 a non-rig resource
        const auto digits=[](const std::array<std::atomic<std::uint64_t>,625>& h) {std::string s;
            for(unsigned p=0;p<625;++p) if(const auto n=h[p].load()) s+=(s.empty()?"":" ")+std::to_string(p/125)+std::to_string(p/25%5)+std::to_string(p/5%5)+std::to_string(p%5)+':'+std::to_string(n);
            return s;};
        const std::string patterns=digits(unpaired_patterns_);
        o<<"{\"enabled\":"<<(enabled.load()?"true":"false")<<",\"rig_motion\":{\"own\":"<<own_pairs_.load()<<",\"zero\":"<<zero_pairs_.load()
         <<",\"unpaired\":"<<unpaired_.load()<<",\"unpaired_t0t1t2t3\":\""<<patterns<<"\",\"unpaired_pass\":"<<unpaired_pass_.load()<<",\"unpaired_targets\":"<<unpaired_targets_.load()
         <<",\"depth_only\":"<<depth_only_.load()<<",\"depth_only_t0t1t2t3\":\""<<digits(depth_only_patterns_)<<"\",\"depth_only_pass\":"<<depth_only_pass_.load()<<",\"epoch\":"<<epoch_.load()
         <<",\"no_previous_slot\":"<<no_history_.load()<<",\"copies\":"<<history_copies_.load()<<"},\"buffers\":"<<published_buffers_.load()<<",\"rig_draws_last_present\":"<<rig_draws_last_.load()
         <<",\"rig_draws\":"<<rig_draws_.load()<<",\"replays_last_present\":"<<replays_last_.load()<<",\"replays\":"<<replays_.load()
         <<",\"skipped\":{\"no_depth_view\":"<<no_dsv_.load()<<",\"msaa\":"<<msaa_.load()<<"},\"failures\":"<<failures_.load()<<'}';
        return o.str();
    }
    // Around one game draw (job threads). Inert unless enabled and the thread's pass renders the alternate view. after() right after the
    // engine's draw, with every engine binding of that draw still in place.
    class Scope {
    public:
        Scope(ID3D11DeviceContext* c,std::uintptr_t pass_view,int pass=-1) noexcept {
            tl_rig_=false;tl_pass_=pass;
            if(!c || !pass_view || !enabled.load(std::memory_order_relaxed) || pass_view!=view.load(std::memory_order_relaxed)) return;
            view_draws_.fetch_add(1,std::memory_order_relaxed);
            ID3D11DepthStencilState* bound{};UINT ref{};c->OMGetDepthStencilState(&bound,&ref);
            const int writes=writes_depth(bound);
            if(bound) bound->Release(); // the cache keeps its own reference
            if(writes<=0) return;
            tl_rig_=is_rig(c);
            if(tl_rig_) {rig_draws_.fetch_add(1,std::memory_order_relaxed);rig_draws_frame_.fetch_add(1,std::memory_order_relaxed);c_=c;}
        }
        void after(const DrawArgs& args,DrawFn draw,TargetsFn targets) noexcept {
            restore_t0(); // v1166: the engine's previous palette again before the replay (it sees the engine's bindings)
            if(c_ && draw && targets) replay(c_,args,draw,targets);
            c_=nullptr;
        }
        ~Scope() {restore_t0();tl_rig_=false;tl_prepass_=false;if(tl_current_) {tl_current_->Release();tl_current_=nullptr;}}
        void restore_t0() noexcept {
            if(!tl_saved_set_ || !c_) return;
            c_->VSSetShaderResources(tl_saved_slot_,1,&tl_saved_t0_);if(tl_saved_t0_) tl_saved_t0_->Release();
            tl_saved_t0_=nullptr;tl_saved_set_=false;
        }
        Scope(const Scope&)=delete;Scope& operator=(const Scope&)=delete;
    private:
        ID3D11DeviceContext* c_{};
    };
private:
    struct Target {ID3D11Resource* depth{};Microsoft::WRL::ComPtr<ID3D11Resource> hold;Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv;Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;UINT w{},h{};};
    struct Entry {std::atomic<ID3D11DepthStencilState*> key{};std::atomic<bool> used{};bool writes{};};
    static constexpr unsigned slots=1024;
    inline static std::array<Entry,slots> table_{};inline static std::atomic<int> default_writes_{1}; // a null state is the default: depth written
    inline static std::mutex mutex_;inline static Target target_{};
    inline static Microsoft::WRL::ComPtr<ID3D11PixelShader> ps_;inline static Microsoft::WRL::ComPtr<ID3D11DepthStencilState> equal_;
    inline static Microsoft::WRL::ComPtr<ID3D11BlendState> blend_;inline static bool made_{},make_failed_{};inline static ID3D11Device* device_{}; // identity: whose objects these are
    inline static std::array<std::atomic<std::uintptr_t>,buffer_slots> buffers_{};inline static std::atomic<unsigned> published_buffers_{};
    inline static std::atomic<std::uint64_t> rig_draws_{},rig_draws_frame_{},rig_draws_last_{},replays_{},replays_frame_{},replays_last_{},view_draws_{},
        no_dsv_{},msaa_{},failures_{};
    inline static thread_local bool tl_rig_{};
    // v1166: this draw's rig model (0, 1 arms, 2 body), its current palette view (t1, held), whether t0/t1 are that model's previous/current
    inline static thread_local unsigned tl_model_{};inline static thread_local ID3D11ShaderResourceView* tl_current_{};inline static thread_local bool tl_prepass_{};
    inline static thread_local ID3D11ShaderResourceView* tl_saved_t0_{};inline static thread_local bool tl_saved_set_{};
    struct History {std::atomic<std::uint64_t> key[2]{};Microsoft::WRL::ComPtr<ID3D11Buffer> buffer[2];Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view[2];
        std::uintptr_t source{};UINT bytes{};std::atomic<bool> ready{};};
    inline static std::array<std::array<History,3>,2> history_{};
    inline static std::atomic<std::uint64_t> own_pairs_{},zero_pairs_{},no_history_{},history_copies_{},unpaired_{};
    // v1167: rig MV draws per engine frame (own pair / not), the unpaired draws' binding pattern (t0,t1,t2: 0 none, 1 previous wrapper,
    // 2 current wrapper... encoded as the rig buffer slot kind: 0 none, 1 this model wrapper 0, 2 wrapper 1, 3 another model), the epoch
    struct FrameTally {std::atomic<std::uint32_t> frame{};std::atomic<std::uint32_t> own{},zero{};};
    inline static std::array<FrameTally,8> tally_{};inline static std::array<std::atomic<std::uint64_t>,625> unpaired_patterns_{},depth_only_patterns_{};
    inline static std::atomic<std::uint64_t> depth_only_{};inline static std::atomic<int> unpaired_pass_{-2},depth_only_pass_{-2};inline static std::atomic<unsigned> unpaired_targets_{};
    inline static thread_local int tl_pass_{-1};inline static thread_local UINT tl_prev_slot_{},tl_saved_slot_{};
    inline static std::atomic<std::uint32_t> epoch_{1},max_frame_{};inline static thread_local unsigned tl_pattern_{};
    static void note_frame(std::uint32_t frame,bool own) noexcept {
        FrameTally& t=tally_[frame&7];std::uint32_t tag=t.frame.load(std::memory_order_acquire);
        if(tag!=frame+1 && t.frame.compare_exchange_strong(tag,frame+1,std::memory_order_acq_rel)) {t.own.store(0,std::memory_order_relaxed);t.zero.store(0,std::memory_order_relaxed);}
        (own?t.own:t.zero).fetch_add(1,std::memory_order_relaxed);
    }
    static std::uint32_t epoch_for(std::uint32_t frame) noexcept { // the engine counter went back (a load): a new epoch
        std::uint32_t seen=max_frame_.load(std::memory_order_relaxed);
        if(seen>frame+16) {if(max_frame_.compare_exchange_strong(seen,frame,std::memory_order_relaxed)) epoch_.fetch_add(1,std::memory_order_relaxed);}
        else while(frame>seen && !max_frame_.compare_exchange_weak(seen,frame,std::memory_order_relaxed)) {}
        return epoch_.load(std::memory_order_relaxed);
    }
    // The eye's two palette slots of `model`, made like the current palette buffer and view (recreated when the model's buffers change).
    static History* history(ID3D11DeviceContext* c,unsigned eye,unsigned model) noexcept {
        History& h=history_[eye][model];
        const auto source=buffers_[model*2].load(std::memory_order_relaxed)^(buffers_[model*2+1].load(std::memory_order_relaxed)<<1);
        if(h.ready.load(std::memory_order_acquire) && h.source==source) return &h;
        std::lock_guard<std::mutex> lock(mutex_);
        if(h.ready.load(std::memory_order_relaxed) && h.source==source) return &h;
        h.ready.store(false,std::memory_order_relaxed);h.key[0]=0;h.key[1]=0;
        ID3D11Resource* r{};tl_current_->GetResource(&r);ID3D11Buffer* b{};
        if(r) {r->QueryInterface(__uuidof(ID3D11Buffer),reinterpret_cast<void**>(&b));r->Release();}
        if(!b) {failures_.fetch_add(1,std::memory_order_relaxed);return nullptr;}
        D3D11_BUFFER_DESC d{};b->GetDesc(&d);b->Release();
        d.Usage=D3D11_USAGE_DEFAULT;d.CPUAccessFlags=0;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        D3D11_SHADER_RESOURCE_VIEW_DESC vd{};tl_current_->GetDesc(&vd);
        ID3D11Device* device{};c->GetDevice(&device);bool ok=device!=nullptr;
        for(unsigned i=0;i<2 && ok;++i) {h.buffer[i].Reset();h.view[i].Reset();ok=SUCCEEDED(device->CreateBuffer(&d,nullptr,&h.buffer[i])) && SUCCEEDED(device->CreateShaderResourceView(h.buffer[i].Get(),&vd,&h.view[i]));}
        if(device) device->Release();
        if(!ok) {failures_.fetch_add(1,std::memory_order_relaxed);return nullptr;}
        h.source=source;h.bytes=d.ByteWidth;h.ready.store(true,std::memory_order_release);
        return &h;
    }

    static unsigned hash(const void* p) noexcept {return static_cast<unsigned>((reinterpret_cast<std::uintptr_t>(p)>>4)*0x9e3779b97f4a7c15ull>>54);}
    static int writes_depth(ID3D11DepthStencilState* s) noexcept { // 1 writes, 0 not, -1 table full
        if(!s) return default_writes_.load(std::memory_order_relaxed);
        const unsigned h=hash(s);
        for(unsigned i=0;i<slots;++i) {
            Entry& e=table_[(h+i)&(slots-1)];
            if(e.used.load(std::memory_order_acquire)) {if(e.key.load(std::memory_order_relaxed)==s) return e.writes?1:0;continue;}
            std::lock_guard<std::mutex> lock(mutex_);
            if(e.used.load(std::memory_order_relaxed)) {--i;continue;} // taken meanwhile: look at it again
            D3D11_DEPTH_STENCIL_DESC d{};s->GetDesc(&d);s->AddRef(); // kept: its address is never reused for another state
            e.writes=d.DepthEnable && d.DepthWriteMask==D3D11_DEPTH_WRITE_MASK_ALL;e.key.store(s,std::memory_order_relaxed);e.used.store(true,std::memory_order_release);
            return e.writes?1:0;
        }
        failures_.fetch_add(1,std::memory_order_relaxed);return -1;
    }
    static bool is_rig(ID3D11DeviceContext* c) noexcept {
        if(!published_buffers_.load(std::memory_order_relaxed)) return false;
        constexpr unsigned scan=8; // v1168: t0..t7 (v1167 looked at t0..t2 only)
        ID3D11ShaderResourceView* views[scan]{};c->VSGetShaderResources(0,scan,views);
        bool hit=false;int which[scan]{-1,-1,-1,-1,-1,-1,-1,-1}; // the rig buffer slot (model*2 + wrapper) each view is of
        for(unsigned k=0;k<scan;++k) if(auto* v=views[k]) {
            ID3D11Resource* r{};v->GetResource(&r);const auto at=reinterpret_cast<std::uintptr_t>(r);
            for(unsigned i=0;i<buffer_slots;++i) if(at && buffers_[i].load(std::memory_order_relaxed)==at) {which[k]=static_cast<int>(i);hit=true;}
            if(r) r->Release();
        }
        // v1166: the prepass binding (t0 previous, t1 current, the two wrappers of one model): keep the current view for previous_palette
        // v1167: t0 == t1 (the engine's own history reset, 0x25da80) is the prepass form too. v1168: the previous palette is the slot other
        // than t1 that holds this model's wrapper (t0 in the measured prepass); none: no previous palette to set (classified by the caller).
        tl_prepass_=false;tl_prev_slot_=0;
        if(which[1]>=0) for(unsigned k=0;k<scan && !tl_prepass_;++k) if(k!=1 && which[k]>=0 && which[k]/2==which[1]/2) {tl_prepass_=true;tl_prev_slot_=k;}
        tl_pattern_=0;
        for(unsigned k=0;k<4;++k) tl_pattern_=tl_pattern_*5+(which[k]>=0?(which[1]>=0 && which[k]/2!=which[1]/2?3u:1u+static_cast<unsigned>(which[k]%2)):views[k] || (k==1 && tl_current_)?4u:0u);
        if(tl_prepass_) {tl_model_=static_cast<unsigned>(which[1]/2);tl_current_=views[1];views[1]=nullptr;}
        for(auto* v:views) if(v) v->Release();
        return hit;
    }
    // Under mutex_: the shader and states (once), and the rig target for this depth resource (recreated on a new depth/size).
    static bool prepare(ID3D11Device* device,ID3D11Resource* depth,const D3D11_TEXTURE2D_DESC& dd,bool& created) {
        created=false;
        if(device_!=device) {target_={};ps_.Reset();equal_.Reset();blend_.Reset();made_=make_failed_=false;device_=device;} // a new device
        if(!made_ && !make_failed_) {
            static const char src[]="float main(float4 p:SV_Position):SV_Target{return p.z;}";
            Microsoft::WRL::ComPtr<ID3DBlob> code;
            D3D11_DEPTH_STENCIL_DESC ds{};ds.DepthEnable=TRUE;ds.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;ds.DepthFunc=D3D11_COMPARISON_EQUAL;ds.StencilEnable=FALSE;
            D3D11_BLEND_DESC bs{};bs.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL; // no blending, every channel
            make_failed_=FAILED(D3DCompile(src,sizeof src-1,nullptr,nullptr,nullptr,"main","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,nullptr)) ||
                FAILED(device->CreatePixelShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&ps_)) ||
                FAILED(device->CreateDepthStencilState(&ds,&equal_)) || FAILED(device->CreateBlendState(&bs,&blend_));
            made_=!make_failed_;
        }
        if(!made_) return false;
        if(target_.depth==depth && target_.w==dd.Width && target_.h==dd.Height && target_.rtv) return true;
        target_={};
        D3D11_TEXTURE2D_DESC t{};t.Width=dd.Width;t.Height=dd.Height;t.MipLevels=t.ArraySize=1;t.Format=DXGI_FORMAT_R32_FLOAT;t.SampleDesc.Count=1;
        t.Usage=D3D11_USAGE_DEFAULT;t.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;Target n{};
        if(FAILED(device->CreateTexture2D(&t,nullptr,&tex)) || FAILED(device->CreateRenderTargetView(tex.Get(),nullptr,&n.rtv)) ||
           FAILED(device->CreateShaderResourceView(tex.Get(),nullptr,&n.srv))) return false;
        n.depth=depth;n.hold=depth;n.w=dd.Width;n.h=dd.Height;target_=n;created=true;
        return true;
    }
    static void replay(ID3D11DeviceContext* c,const DrawArgs& a,DrawFn draw,TargetsFn targets) noexcept {
        std::array<ID3D11RenderTargetView*,D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> rtvs{};ID3D11DepthStencilView* dsv{};
        c->OMGetRenderTargets(static_cast<UINT>(rtvs.size()),rtvs.data(),&dsv);
        const auto release_targets=[&] {for(auto* r:rtvs) if(r) r->Release();if(dsv) dsv->Release();};
        if(!dsv) {no_dsv_.fetch_add(1,std::memory_order_relaxed);release_targets();return;}
        ID3D11RenderTargetView* rig{};Microsoft::WRL::ComPtr<ID3D11PixelShader> ps;Microsoft::WRL::ComPtr<ID3D11DepthStencilState> equal;Microsoft::WRL::ComPtr<ID3D11BlendState> opaque; // held: a reset cannot free them mid-replay
        {
            ID3D11Resource* depth{};dsv->GetResource(&depth);
            D3D11_TEXTURE2D_DESC dd{};ID3D11Texture2D* dt{};
            if(depth && SUCCEEDED(depth->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&dt))) && dt) {dt->GetDesc(&dd);dt->Release();}
            if(dd.SampleDesc.Count!=1) msaa_.fetch_add(1,std::memory_order_relaxed);
            else {
                ID3D11Device* device{};c->GetDevice(&device);bool created=false;
                try {
                    std::lock_guard<std::mutex> lock(mutex_);
                    if(device && dd.Width && prepare(device,depth,dd,created)) {rig=target_.rtv.Get();rig->AddRef();ps=ps_;equal=equal_;opaque=blend_;}
                } catch(...) {}
                if(device) device->Release();
                if(rig && created) {const float zero[4]{};c->ClearRenderTargetView(rig,zero);}
                if(!rig) failures_.fetch_add(1,std::memory_order_relaxed);
            }
            if(depth) depth->Release();
        }
        if(!rig) {release_targets();return;}
        UINT rtv_count=0;for(UINT i=0;i<rtvs.size();++i) if(rtvs[i]) rtv_count=i+1;
        ID3D11PixelShader* engine_ps{};std::array<ID3D11ClassInstance*,256> classes{};UINT class_count=256;c->PSGetShader(&engine_ps,classes.data(),&class_count);
        ID3D11DepthStencilState* dss{};UINT ref{};c->OMGetDepthStencilState(&dss,&ref);
        ID3D11BlendState* blend{};float factor[4]{};UINT mask{};c->OMGetBlendState(&blend,factor,&mask);
        targets(c,1,&rig,dsv);
        c->PSSetShader(ps.Get(),nullptr,0);c->OMSetDepthStencilState(equal.Get(),0);c->OMSetBlendState(opaque.Get(),nullptr,0xffffffff);
        draw(c,a);
        targets(c,rtv_count,rtvs.data(),dsv); // the engine's exact set again; the rig target is unbound
        c->PSSetShader(engine_ps,class_count?classes.data():nullptr,class_count);c->OMSetDepthStencilState(dss,ref);c->OMSetBlendState(blend,factor,mask);
        if(engine_ps) engine_ps->Release();
        for(UINT i=0;i<class_count && i<classes.size();++i) if(classes[i]) classes[i]->Release();
        if(dss) dss->Release();
        if(blend) blend->Release();
        rig->Release();release_targets();
        replays_.fetch_add(1,std::memory_order_relaxed);replays_frame_.fetch_add(1,std::memory_order_relaxed);
    }
};
} // namespace d2vr
