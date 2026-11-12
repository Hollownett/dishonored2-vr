#pragma once
// v1163 performance mode: the player-rig mask per eye, from the rig draws themselves (replaces v1162's stencil bit: the sim census found
// the engine's depth-stencil states read 0xEF and write 0xBF of the stencil, refs OR 0xFF: no free bit).
//
// The link from a draw to the rig (v1162, sim-proven: 6 buffers, 12 rig draws per Present): a skinned rig model (both arms, the full-body
// avatar) owns two GPU skin-palette buffers (render model +0x2008 / +0x2040), and its draws bind one of them at VS t0..t2. Exact in any
// mode. Not covered (by decision): held weapons and props (the mover rules take them), NPC models the hands hold.
//
// The mask: right after each rig draw that writes the scene depth in a pass of the alternate one-view, the SAME draw is replayed with this
// file's pixel shader (SV_Position.z; v1179: and the own-motion flag) into a rig target (R32G32_FLOAT, the scene depth's size), the engine's depth view bound for the test
// with depth EQUAL, no depth write, no stencil, no blending: the rig depth where the rig won the depth test then. The engine's targets, depth
// state, blend state and pixel shader are restored; nothing of the engine's is written. Visibility is resolved at the copy (FsrTaa):
// Eye::rig = 255 where scene depth > 0 and |rig depth - scene depth| <= eps, so anything drawn over the rig later, in any order, wins.
// The rig target is cleared after each copy. The PS reads only SV_Position: system-value position links with any vertex shader that writes
// it, whatever its other outputs (checked on WARP with SV_Position not the first output, tests/rig_mask_check.cpp).
// Knob build/no-stale-eye-rig (dxgi_probe): inert (no detection, no replay, no mask).
#include <d3d11.h>
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>
#include <mutex>
#include <sstream>
#include <string>

namespace d2vr {
// The copy/compare: t0 scene depth (float view), t1 rig target (x rig depth, y 1 = that draw had this eye's own previous palette and rows);
// u0 Eye::rig (R8_UINT): 255 a visible rig pixel whose engine MV is this eye's own motion (v1179), 254 a visible rig pixel without (the
// engine's vectors), 253 (v1183) a visible held weapon / held prop pixel (rigid, attached to a hand, no own motion), 0 not rig (consumers
// that test > 0 see all three as rig); u1 counters: [0] mask pixels, [1] near misses (rig depth present,
// eps < |difference| <= 64 eps: an eps too tight shows here), [2] own-motion pixels, [3] (v1207) rig pixels whose engine motion vector (t2, the
// TAA's MV input, UV of the render rectangle; unbound reads 0) is not finite or longer than 256 px. k.xy eps = x + y * depth, k.zw the render rectangle.
// v1209: the bound is a quarter of the render width (no hand or body point crosses a quarter of the eye's view between two renders of that eye:
// 5 m/s at 0.3 m is ~340 px of 1922; the measured wild vectors are 40,000+ px). Such a pixel is marked 252 (rig, no own motion) and
// RigMask::guard rewrites its motion vector to zero before the motion pass and the upscaler read it.
inline constexpr char rig_mask_copy_source[]=
    "Texture2D<float> z:register(t0);Texture2D<float2> r:register(t1);Texture2D<float2> mv:register(t2);RWTexture2D<uint> o:register(u0);RWByteAddressBuffer n:register(u1);"
    "cbuffer C:register(b0){float4 k;};groupshared uint g[4];"
    "[numthreads(8,8,1)] void main(uint3 i:SV_DispatchThreadID,uint t:SV_GroupIndex){"
    "if(t<4) g[t]=0;GroupMemoryBarrierWithGroupSync();"
    "const bool inside=all(float2(i.xy)<k.zw);const float d=z.Load(int3(i.xy,0));const float2 rq=r.Load(int3(i.xy,0));const float q=rq.x;"
    "const float e=k.x+k.y*d,a=abs(q-d);const bool rig=inside && d>0 && q>0 && a<=e,held=rig && rq.y>1.5,own=rig && !held && rq.y>0.5;"
    "o[i.xy]=held?253:own?255:rig?254:0;"
    "if(rig) InterlockedAdd(g[0],1);else if(inside && d>0 && q>0 && a<=64*e) InterlockedAdd(g[1],1);"
    "if(own) InterlockedAdd(g[2],1);"
    "const float2 m=mv.Load(int3(i.xy,0))*k.zw;const float b=0.25*k.z;const bool wild=rig && !held && !(dot(m,m)<=b*b);if(wild) {o[i.xy]=252;InterlockedAdd(g[3],1);}"
    "GroupMemoryBarrierWithGroupSync();if(t<4 && g[t]) n.InterlockedAdd(t*4,g[t]);}";
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
// maps the new current with WRITE_DISCARD (0x2d3c60 -> context vtable +0x70) and writes the CPU palette into it; 0x25da80 hands out the
// current as "previous" while +0x1fb0 has 4/0x10/0x20), so the previous palette is the last ENGINE frame's: in alternate mode the other
// eye's render one tick ago. Its rows are the other eye's too (raw).
// v1179: the eye's own previous palette comes from GPU order, not from labels. At each render's GPU skin copy of a rig model (0x25f930, once
// per engine frame, on the context the engine uploads on, ahead of that render's draws) three mod buffers rotate: P <- B, B <- A, A <- the
// new current palette. With one render per engine frame and the eyes alternating, P then holds the palette of the render two engine frames
// back = this eye's previous render, whatever eye/frame a draw's classification says. Valid after three consecutive engine frames
// (rig_ring_valid); a skipped frame (no skin copy) restarts the run. The v1166-v1170 per-eye slots keyed by the draw's classified frame,
// and the per-frame validity tally, are gone: validity is per draw (own pair or not) and reaches the motion pass per pixel through the mask.
inline constexpr unsigned rig_ring_run_needed=3;
// v1185 (sim v1183: two renders whose whole rig read 40,000 px p50 with own pairs): a draw takes the ring only when its rotation for the
// engine frame the draw belongs to is already recorded (RigMask::frame_fn at the draw == the ring's last rotation) and the run has lasted
// rig_ring_proven frames; otherwise zero motion (254). Counted: ring_late, ring_young.
inline constexpr unsigned rig_ring_proven=8;
// v1206: the tagged palette ring (RigMask::Ring): a slot is rewritten rig_ring_slots engine frames after its render, so a draw that reads this
// eye's previous render (2 back) stays valid while its command list waits behind up to 5 later skin copies
inline constexpr unsigned rig_ring_slots=8;
// Pure: the run length after a rotation at engine frame `frame` (last = the frame of the previous rotation, ~0u none).
inline unsigned rig_ring_run(std::uint32_t last,unsigned run,std::uint32_t frame) noexcept {
    if(last==frame) return run;                       // same engine frame: already rotated (a second call does nothing)
    return last!=~0u && frame==last+1u?(run<255u?run+1u:run):1u;
}
inline bool rig_ring_valid(unsigned run) noexcept {return run>=rig_ring_run_needed;}

class RigMask {
public:
    static constexpr unsigned buffer_slots=6;
    inline static std::atomic<bool> enabled{};          // dxgi_probe, each Present: alternate-eye on and build/no-stale-eye-rig absent
    inline static std::atomic<std::uintptr_t> view{};    // the alternate one-view whose passes are replayed (0: a paired frame)
    // v1171: rig motion vectors (own previous palette + rows, v1166-v1169) are opt-in: dxgi_probe sets this from build/stale-eye-rig-motion.
    // Off: rig draws keep the engine's previous rows and palette (v1165), no copies, no substitution, rig pixels zeroed by the mask.
    inline static std::atomic<bool> motion{false};
    using FrameFn=std::uint32_t(*)() noexcept;inline static std::atomic<FrameFn> frame_fn{}; // v1185: the engine frame counter (dxgi_probe)
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
    // v1183, ObjectMotion: this depth-writing one-view draw is a held weapon / held prop (its model frame is a held thing's): replayed into
    // the rig target with the held flag (mask 253). Not for rig draws (they keep their own class).
    static void mark_held() noexcept {if(tl_depth_ && !tl_rig_) {tl_held_=true;held_draws_.fetch_add(1,std::memory_order_relaxed);}}
    // v1190 rig target (rig_target.hpp, knob build/stale-eye-rig-target): each rig/held draw this mask replays is drawn once more, depth-only,
    // into the STALE eye's target camera: ObjectMotion binds the draw's per-instance constants with its rows retargeted (target_bind) and
    // restores them (target_restore). Target: R32G32_FLOAT at the scene depth's size (x = reverse-Z depth in the target camera, the engine's
    // depth convention; y = class 255 rig / 253 held; 0 0 = none), its own D32 depth (GREATER: the nearest replayed draw wins). FsrTaa copies it
    // out per render (take_target) and clears it.
    inline static std::atomic<bool> target_on{false};
    // v1199: the target is two slices, one per hypothesis "this draw belongs to a render of eye h" (h = 0, 1): slice h gets the draw's
    // rows retargeted with E of eye h's latest Setup (rig_target::Book::latest_of). For the render the draw really belongs to (eye f), eye f's
    // latest Setup IS that render (eye f's next Setup is two renders away), so slice f is exact whatever thread timing did; the take copies
    // slice f = the fresh eye of the render being taken (FsrTaa knows it from the render's receipt, in GPU order). No per-draw view needed.
    using TargetBind=bool (*)(ID3D11DeviceContext*,unsigned slice) noexcept;using TargetRestore=void (*)(ID3D11DeviceContext*) noexcept;
    inline static std::atomic<TargetBind> target_bind{};inline static std::atomic<TargetRestore> target_restore{};
    static bool target_wanted() noexcept {return target_on.load(std::memory_order_relaxed) && tl_depth_ && (tl_rig_ || tl_held_);}
    static bool draw_is_held() noexcept {return tl_held_;}
    static void mark_target(bool ok) noexcept {tl_target_[0]=tl_target_[1]=ok;} // false: no retarget for this render (counted at the replay)
    static void mark_target(bool ok0,bool ok1) noexcept {tl_target_[0]=ok0;tl_target_[1]=ok1;} // v1199: per slice (hypothesis eye)
    // FsrTaa's copy (immediate/job context of the render's TAA pass): the target of this scene depth copied into `out` (same size and format;
    // created by the caller from target_desc) and cleared; true when this render replayed rig draws and none missed its retarget.
    static bool target_desc(ID3D11Resource* depth,D3D11_TEXTURE2D_DESC& d) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(!depth || target_.depth!=depth || !target_.trg) return false;
        target_.trg->GetDesc(&d);return true;
    }
    // v1191: only the render rectangle (w x h at the top left) is copied and cleared (was the whole target: 54 MB a render at 2496x2688)
    // v1199: `slice` = the fresh eye of the render whose target is taken (its rows were retargeted under that hypothesis); both slices are
    // cleared. Valid: no draw missed that slice's retarget, and draws were replayed this render or the previous one. The draw and miss
    // counters are CPU-side and a render's late draws can be recorded after the previous render's take (v1198: 907 of 5370 renders counted
    // 0 draws while their neighbours counted ~2 renders' worth; the GPU target itself was complete), so one render of slack is allowed.
    static bool take_target(ID3D11DeviceContext* c,ID3D11Resource* depth,ID3D11Texture2D* out,std::uint64_t& draws,UINT w=0,UINT h=0,unsigned slice=0) {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> trg;std::array<Microsoft::WRL::ComPtr<ID3D11RenderTargetView>,2> rtv;std::array<Microsoft::WRL::ComPtr<ID3D11DepthStencilView>,2> dsv;
        {std::lock_guard<std::mutex> lock(mutex_);if(!depth || target_.depth!=depth || !target_.trg) return false;trg=target_.trg;rtv=target_.trg_rtv;dsv=target_.trg_dsv;}
        slice&=1u;
        draws=target_draws_frame_.exchange(0,std::memory_order_relaxed);const auto before=target_draws_last_.exchange(draws,std::memory_order_relaxed);
        const auto misses=target_misses_frame_[slice].exchange(0,std::memory_order_relaxed);target_misses_frame_[slice^1u].store(0,std::memory_order_relaxed);
        D3D11_TEXTURE2D_DESC d{};trg->GetDesc(&d);
        const bool part=w && h && w<=d.Width && h<=d.Height && (w<d.Width || h<d.Height);
        const D3D11_BOX box{0,0,0,part?w:d.Width,part?h:d.Height,1};const D3D11_RECT rect{0,0,static_cast<LONG>(box.right),static_cast<LONG>(box.bottom)};
        if(out) c->CopySubresourceRegion(out,0,0,0,0,trg.Get(),slice,&box); // subresource `slice` = array slice `slice` (one mip)
        const float zero[4]{};
        Microsoft::WRL::ComPtr<ID3D11DeviceContext1> c1;const bool view_clear=part && SUCCEEDED(c->QueryInterface(IID_PPV_ARGS(&c1)));
        for(unsigned k=0;k<2;++k) {
            if(view_clear) c1->ClearView(rtv[k].Get(),zero,&rect,1); // ClearView takes no depth views:
            else c->ClearRenderTargetView(rtv[k].Get(),zero);
            c->ClearDepthStencilView(dsv[k].Get(),D3D11_CLEAR_DEPTH,0.f,0); // a depth clear stays whole (a fast clear on current GPUs)
        }
        const bool valid=(draws>0 || before>0) && misses==0;(valid?target_valid_:target_invalid_).fetch_add(1,std::memory_order_relaxed);
        if(!draws) (before?target_lent_:target_empty_).fetch_add(1,std::memory_order_relaxed);
        return valid;
    }
    // v1166, ObjectMotion at a rig draw (alternate): binds at the previous-palette slot (t0 in the prepass) this eye's own previous palette
    // (v1179: the model's ring P, see rig_ring_valid) when `rows_own` (the draw gets the eye's own previous rows) and the ring is valid, else
    // the current palette (zero motion; the caller then uses the current rows too). True: the own pair is in place; the mask replay of this
    // draw then marks its pixels as own motion (255). Restored in after(). `eye`/`frame` are only diagnostics now (eye < 0: no view).
    // A draw whose palette bindings are not the prepass form cannot be paired (its pixels stay 254: the engine's vectors).
    // v1206: `frame` = the render whose palette is this eye's previous one (the draw's receipt: previous render's engine frame; ~0u unknown)
    static bool previous_palette(ID3D11DeviceContext* c,int eye,std::uint32_t frame,bool rows_own) noexcept {
        tl_own_=false;
        if(!tl_rig_ || !c) return false;
        if(!tl_prepass_ || !tl_current_) {
            // v1168: no previous-palette slot to set. Only a draw that writes a colour target (the prepass's motion-vector target) can carry
            // motion; a depth-only draw (no render target bound) cannot.
            std::array<ID3D11RenderTargetView*,D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> rtvs{};c->OMGetRenderTargets(static_cast<UINT>(rtvs.size()),rtvs.data(),nullptr);
            UINT targets=0;for(auto* r:rtvs) if(r) {++targets;r->Release();}
            const unsigned p=tl_pattern_%625;
            if(targets) {unpaired_.fetch_add(1,std::memory_order_relaxed);unpaired_patterns_[p].fetch_add(1,std::memory_order_relaxed);
                unpaired_pass_.store(tl_pass_,std::memory_order_relaxed);unpaired_targets_.store(targets,std::memory_order_relaxed);}
            else {depth_only_.fetch_add(1,std::memory_order_relaxed);depth_only_patterns_[p].fetch_add(1,std::memory_order_relaxed);depth_only_pass_.store(tl_pass_,std::memory_order_relaxed);}
            return false;
        }
        ID3D11ShaderResourceView* bind=tl_current_;bool own=false;
        if(rows_own && eye>=0) {
            if(ID3D11ShaderResourceView* v=ring_view(c,tl_model_,frame)) {bind=v;own=true;}
            else no_history_.fetch_add(1,std::memory_order_relaxed);
        }
        if(!tl_saved_set_) {c->VSGetShaderResources(tl_prev_slot_,1,&tl_saved_t0_);tl_saved_set_=true;tl_saved_slot_=tl_prev_slot_;}
        c->VSSetShaderResources(tl_prev_slot_,1,&bind);
        (own?own_pairs_:zero_pairs_).fetch_add(1,std::memory_order_relaxed);
        tl_own_=own;return own;
    }
    // v1179, the engine's GPU skin copy of rig model `model` (0x25f930 hook, after the engine's upload): `c` the context it uploaded on,
    // `current` the buffer now holding this render's palette, `frame` the engine frame counter. Rotates the ring once per engine frame.
    // v1208: the slot is written from the CPU palette the engine uploaded (`cpu`, `cpu_bytes` = the wrapper's size; `uploaded` = this call's
    // original upload ran), never copied from the engine's dynamic wrapper: which wrapper +0x1fb2 names at this call, and what a dynamic
    // buffer DISCARD-mapped in another command list holds on this context, are not provable (v1198-v1207: whole renders of 40,000 px rig
    // vectors with a tag-matched slot). A frame without an upload writes the last uploaded palette (the wrappers still hold it). Until the
    // model's first upload is seen nothing is tagged (zero motion). `other`: the second wrapper, only for the fingerprint check.
    // v1210 diagnostics: `commit` = the image skin rewrote the CPU palette right before this upload, `race` = the CPU palette read after the
    // engine's upload differs from the one read just before it (something wrote it meanwhile); kept per slot with the bytes written (shadow).
    static void skin_copied(ID3D11DeviceContext* c,unsigned model,ID3D11Buffer* current,std::uint32_t frame,const void* cpu,UINT cpu_bytes,bool uploaded,ID3D11Buffer* other=nullptr,bool commit=true,bool race=false) noexcept {
        if(!c || !current || model>=ring_.size() || !motion.load(std::memory_order_relaxed)) return;
        Ring& r=ring_[model];
        D3D11_BUFFER_DESC d{};current->GetDesc(&d);
        ID3D11Device* owner{};current->GetDevice(&owner);if(owner) owner->Release(); // identity only
        std::lock_guard<std::mutex> lock(mutex_); // 3 calls a render; reset() and ring_view() take it too
        if(!r.ready.load(std::memory_order_relaxed) || r.bytes!=d.ByteWidth || r.device!=owner) {
            r.ready.store(false,std::memory_order_release);r.clear();
            d.Usage=D3D11_USAGE_DEFAULT;d.CPUAccessFlags=0;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
            bool ok=owner!=nullptr;for(auto& b:r.slot) ok=ok && SUCCEEDED(owner->CreateBuffer(&d,nullptr,&b));
            if(!ok) {r.clear();failures_.fetch_add(1,std::memory_order_relaxed);return;}
            r.bytes=d.ByteWidth;r.device=owner;r.ready.store(true,std::memory_order_release);
        }
        if(uploaded && cpu && cpu_bytes==r.bytes) {
            r.upload.assign(static_cast<const std::uint8_t*>(cpu),static_cast<const std::uint8_t*>(cpu)+cpu_bytes);uploads_.fetch_add(1,std::memory_order_relaxed);
            if(other && !(frame%32)) fingerprint(c,model,current,other);
        } else if(uploaded) layout_refused_.fetch_add(1,std::memory_order_relaxed);
        if(r.upload.size()!=r.bytes) {no_upload_.fetch_add(1,std::memory_order_relaxed);return;} // nothing proven yet: no slot is tagged
        const unsigned k=frame%rig_ring_slots;
        const std::uint32_t last=r.last.load(std::memory_order_relaxed);
        if(last==frame) { // a second call this frame: only an upload replaces what the slot holds
            ring_repeats_.fetch_add(1,std::memory_order_relaxed);
            if(!uploaded || r.from_upload[k]) return;
        } else {
            const unsigned run=rig_ring_run(last,r.run.load(std::memory_order_relaxed),frame);
            if(run==1 && last!=~0u) ring_restarts_.fetch_add(1,std::memory_order_relaxed);
            r.run.store(run,std::memory_order_release);rotations_.fetch_add(1,std::memory_order_relaxed);
        }
        // v1206: this render's own slot, tagged with its frame (no in-place rotation: a draw list that executes after a later render's write
        // still reads the palette its tag names)
        r.tag[k].store(~0u,std::memory_order_release);
        c->UpdateSubresource(r.slot[k].Get(),0,nullptr,r.upload.data(),0,0);r.from_upload[k]=uploaded;
        if(probe_on.load(std::memory_order_relaxed)) {r.shadow[k]=r.upload;r.commit[k]=commit;r.race[k]=uploaded && race;r.writer_deferred[k]=c->GetType()==D3D11_DEVICE_CONTEXT_DEFERRED;}
        if(uploaded && race) races_.fetch_add(1,std::memory_order_relaxed);
        if(!commit) no_commit_.fetch_add(1,std::memory_order_relaxed);
        r.tag[k].store(frame,std::memory_order_release);r.last.store(frame,std::memory_order_relaxed);
    }
    // v1208 diagnostic: every 32nd uploading frame, both engine wrappers are read back and compared with the CPU palette just uploaded (status
    // fingerprint: [checks, current wrapper == CPU, other wrapper == CPU, neither, first differing byte of current]). Proves which wrapper
    // +0x1fb2 names after an upload and whether the CPU palette is the GPU layout.
    static void fingerprint(ID3D11DeviceContext* c,unsigned model,ID3D11Buffer* current,ID3D11Buffer* other) noexcept { // under mutex_
        auto& r=ring_[model];auto& f=fingerprints_[model];if(f.pending || !r.device) return;
        D3D11_BUFFER_DESC d{};current->GetDesc(&d);D3D11_BUFFER_DESC od{};other->GetDesc(&od);if(od.ByteWidth!=d.ByteWidth) return;
        d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;d.MiscFlags=0;d.StructureByteStride=0;
        if((!f.cur && FAILED(r.device->CreateBuffer(&d,nullptr,&f.cur))) || (!f.oth && FAILED(r.device->CreateBuffer(&d,nullptr,&f.oth)))) return;
        c->CopyResource(f.cur.Get(),current);c->CopyResource(f.oth.Get(),other);f.cpu=r.upload;f.pending=true;
    }
    static void poll_fingerprints(ID3D11DeviceContext* immediate) noexcept { // Present thread (immediate context): never waits
        if(!immediate) return;
        std::lock_guard<std::mutex> lock(mutex_);
        for(auto& f:fingerprints_) if(f.pending && f.cur && f.oth) {
            D3D11_MAPPED_SUBRESOURCE a{},b{};
            if(FAILED(immediate->Map(f.cur.Get(),0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,&a)) || !a.pData) continue;
            if(FAILED(immediate->Map(f.oth.Get(),0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,&b)) || !b.pData) {immediate->Unmap(f.cur.Get(),0);continue;}
            const bool cur_eq=!std::memcmp(a.pData,f.cpu.data(),f.cpu.size()),oth_eq=!std::memcmp(b.pData,f.cpu.data(),f.cpu.size());
            fp_[0].fetch_add(1);fp_[cur_eq?1:oth_eq?2:3].fetch_add(1);
            if(!cur_eq) {std::size_t i=0;while(i<f.cpu.size() && static_cast<const std::uint8_t*>(a.pData)[i]==f.cpu[i]) ++i;fp_first_diff_.store(i);}
            immediate->Unmap(f.oth.Get(),0);immediate->Unmap(f.cur.Get(),0);f.pending=false;
        }
        poll_probes(immediate); // v1210
    }
    // dxgi_probe: the one-view mode (re)starts (the alternate view appears after paired frames or a mode change): every ring starts over.
    // v1207: the copy/compare counts per render (FsrTaa::poll_rig_pixels, keyed by the render's receipt display): [mask px, own px, wild-MV px]
    static void note_mask(std::uint64_t display,std::uint32_t rig,std::uint32_t own,std::uint32_t wild) noexcept {
        auto& m=mask_census_[display%mask_census_.size()];m.display.store(~0ull,std::memory_order_release);
        m.n[0].store(rig,std::memory_order_relaxed);m.n[1].store(own,std::memory_order_relaxed);m.n[2].store(wild,std::memory_order_relaxed);m.display.store(display,std::memory_order_release);
        if(wild) {wild_renders_.fetch_add(1,std::memory_order_relaxed);wild_px_.fetch_add(wild,std::memory_order_relaxed);}
    }
    static bool mask_counts(std::uint64_t display,std::uint32_t (&out)[3]) noexcept {
        const auto& m=mask_census_[display%mask_census_.size()];if(m.display.load(std::memory_order_acquire)!=display) return false;
        for(unsigned i=0;i<3;++i) out[i]=m.n[i].load(std::memory_order_relaxed);
        return m.display.load(std::memory_order_acquire)==display;
    }
    inline static std::atomic<std::uint64_t> wild_renders_{},wild_px_{},guard_draws_{},guard_failures_{};
    // v1209 same-render guard (immediate context, in FsrTaa's job right after the mask copy/compare): every mask pixel marked 252 gets motion
    // vector (0,0) in `mv` (the TAA's motion input) = the zero-motion pair (current rows + current palette give exactly 0 since v1178).
    // The touched pipeline state is saved and restored. False: not drawn (counted).
    static bool guard(ID3D11DeviceContext* c,ID3D11Resource* mv,ID3D11ShaderResourceView* mask,UINT w,UINT h) noexcept {
        if(!c || !mv || !mask || !w || !h) return false;
        std::lock_guard<std::mutex> lock(mutex_);
        Microsoft::WRL::ComPtr<ID3D11Device> dev;c->GetDevice(&dev);
        if(guard_.device!=dev.Get()) {guard_={};guard_.device=dev.Get();}
        if(!guard_.vs && !guard_.failed) {
            static const char vs[]="float4 main(uint i:SV_VertexID):SV_Position{const float2 u=float2((i<<1)&2,i&2);return float4(u*float2(2,-2)+float2(-1,1),0,1);}";
            static const char ps[]="Texture2D<uint> m:register(t0);float2 main(float4 p:SV_Position):SV_Target{if(m.Load(int3(p.xy,0))!=252) discard;return float2(0,0);}";
            Microsoft::WRL::ComPtr<ID3DBlob> a,b;D3D11_BLEND_DESC bd{};bd.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_RED|D3D11_COLOR_WRITE_ENABLE_GREEN;
            D3D11_DEPTH_STENCIL_DESC dd{};D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;
            guard_.failed=FAILED(D3DCompile(vs,sizeof vs-1,nullptr,nullptr,nullptr,"main","vs_5_0",0,0,&a,nullptr)) || FAILED(D3DCompile(ps,sizeof ps-1,nullptr,nullptr,nullptr,"main","ps_5_0",0,0,&b,nullptr)) ||
                FAILED(dev->CreateVertexShader(a->GetBufferPointer(),a->GetBufferSize(),nullptr,&guard_.vs)) || FAILED(dev->CreatePixelShader(b->GetBufferPointer(),b->GetBufferSize(),nullptr,&guard_.ps)) ||
                FAILED(dev->CreateBlendState(&bd,&guard_.blend)) || FAILED(dev->CreateDepthStencilState(&dd,&guard_.depth)) || FAILED(dev->CreateRasterizerState(&rd,&guard_.raster));
        }
        if(guard_.failed) {guard_failures_.fetch_add(1,std::memory_order_relaxed);return false;}
        if(guard_.rtv_of!=mv) {
            guard_.rtv.Reset();guard_.rtv_of=nullptr;Microsoft::WRL::ComPtr<ID3D11Texture2D> t;D3D11_TEXTURE2D_DESC td{};
            if(FAILED(mv->QueryInterface(IID_PPV_ARGS(&t)))) {guard_failures_.fetch_add(1);return false;}
            t->GetDesc(&td);D3D11_RENDER_TARGET_VIEW_DESC v{};v.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
            v.Format=td.Format==DXGI_FORMAT_R16G16_TYPELESS?DXGI_FORMAT_R16G16_FLOAT:td.Format==DXGI_FORMAT_R32G32_TYPELESS?DXGI_FORMAT_R32G32_FLOAT:
                td.Format==DXGI_FORMAT_R16G16B16A16_TYPELESS?DXGI_FORMAT_R16G16B16A16_FLOAT:td.Format;
            if(!(td.BindFlags&D3D11_BIND_RENDER_TARGET) || td.SampleDesc.Count!=1 || FAILED(dev->CreateRenderTargetView(mv,&v,&guard_.rtv))) {guard_failures_.fetch_add(1);return false;}
            guard_.rtv_of=mv;
        }
        // save
        ID3D11RenderTargetView* rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};ID3D11DepthStencilView* dsv{};c->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,rtvs,&dsv);
        ID3D11BlendState* blend{};float factor[4]{};UINT sample{};c->OMGetBlendState(&blend,factor,&sample);ID3D11DepthStencilState* ds{};UINT ref{};c->OMGetDepthStencilState(&ds,&ref);
        ID3D11RasterizerState* rs{};c->RSGetState(&rs);UINT nvp=D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;D3D11_VIEWPORT vps[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};c->RSGetViewports(&nvp,vps);
        ID3D11VertexShader* vs{};c->VSGetShader(&vs,nullptr,nullptr);ID3D11PixelShader* ps{};c->PSGetShader(&ps,nullptr,nullptr);ID3D11ShaderResourceView* t0{};c->PSGetShaderResources(0,1,&t0);
        ID3D11InputLayout* il{};c->IAGetInputLayout(&il);D3D11_PRIMITIVE_TOPOLOGY topo{};c->IAGetPrimitiveTopology(&topo);
        // draw
        ID3D11RenderTargetView* out=guard_.rtv.Get();c->OMSetRenderTargets(1,&out,nullptr);c->OMSetBlendState(guard_.blend.Get(),nullptr,0xffffffff);c->OMSetDepthStencilState(guard_.depth.Get(),0);
        c->RSSetState(guard_.raster.Get());const D3D11_VIEWPORT vp{0,0,float(w),float(h),0,1};c->RSSetViewports(1,&vp);
        c->VSSetShader(guard_.vs.Get(),nullptr,0);c->PSSetShader(guard_.ps.Get(),nullptr,0);c->PSSetShaderResources(0,1,&mask);
        c->IASetInputLayout(nullptr);c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);c->Draw(3,0);
        // restore
        ID3D11ShaderResourceView* none{};c->PSSetShaderResources(0,1,&none);
        c->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,rtvs,dsv);c->OMSetBlendState(blend,factor,sample);c->OMSetDepthStencilState(ds,ref);c->RSSetState(rs);if(nvp) c->RSSetViewports(nvp,vps);
        c->VSSetShader(vs,nullptr,0);c->PSSetShader(ps,nullptr,0);c->PSSetShaderResources(0,1,&t0);c->IASetInputLayout(il);c->IASetPrimitiveTopology(topo);
        for(auto* x:rtvs) if(x) x->Release();
        for(IUnknown* x:{static_cast<IUnknown*>(dsv),static_cast<IUnknown*>(blend),static_cast<IUnknown*>(ds),static_cast<IUnknown*>(rs),static_cast<IUnknown*>(vs),static_cast<IUnknown*>(ps),static_cast<IUnknown*>(t0),static_cast<IUnknown*>(il)}) if(x) x->Release();
        guard_draws_.fetch_add(1,std::memory_order_relaxed);return true;
    }
    static void begin_epoch() noexcept {epoch_.fetch_add(1,std::memory_order_relaxed);for(auto& r:ring_) {r.run.store(0,std::memory_order_relaxed);r.last.store(~0u,std::memory_order_relaxed);for(auto& t:r.tag) t.store(~0u,std::memory_order_relaxed);}}
    // FsrTaa's copy: draws of the alternate view seen since the last call (0: no one-view render, the mask is not live), and the rig target
    // of this scene depth (empty: none yet / another depth).
    static std::uint64_t take_view_draws() noexcept {return view_draws_.exchange(0,std::memory_order_relaxed);}
    static bool target(ID3D11Resource* depth,Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>& srv,Microsoft::WRL::ComPtr<ID3D11RenderTargetView>& rtv) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(!depth || target_.depth!=depth || !target_.srv) return false;
        srv=target_.srv;rtv=target_.rtv;return true;
    }
    static void reset() noexcept {std::lock_guard<std::mutex> lock(mutex_);
        for(auto& r:ring_) {r.ready.store(false);r.clear();r.device=nullptr;r.bytes=0;}
        target_={};ps_.Reset();ps_own_.Reset();ps_held_.Reset();equal_.Reset();blend_.Reset();device_=nullptr;made_=make_failed_=false;}
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
         <<",\"foreign_t0\":{\"draws\":"<<foreign_t0_.load()<<",\"bytes\":"<<foreign_bytes_.load()<<",\"current_bytes\":"<<foreign_current_bytes_.load()
         <<",\"bind\":"<<foreign_bind_.load()<<",\"misc\":"<<foreign_misc_.load()<<",\"stride\":"<<foreign_stride_.load()<<",\"usage\":"<<foreign_usage_.load()
         <<",\"cpu\":"<<foreign_cpu_.load()<<",\"srv_format\":"<<foreign_format_.load()<<",\"srv_dimension\":"<<foreign_dimension_.load()<<",\"srv_elements\":"<<foreign_elements_.load()
         <<",\"distinct\":"<<foreign_distinct_.load()<<",\"draw_count\":"<<foreign_count_.load()<<",\"mod_ring\":"<<foreign_ours_.load()<<"}"
         <<",\"no_previous_slot\":"<<no_history_.load()<<",\"ring\":{\"rotations\":"<<rotations_.load()<<",\"restarts\":"<<ring_restarts_.load()
         <<",\"uploads\":"<<uploads_.load()<<",\"no_upload_yet\":"<<no_upload_.load()<<",\"layout_refused\":"<<layout_refused_.load()
         <<",\"fingerprint\":["<<fp_[0].load()<<','<<fp_[1].load()<<','<<fp_[2].load()<<','<<fp_[3].load()<<','<<static_cast<long long>(fp_first_diff_.load())<<']'
         <<",\"repeats\":"<<ring_repeats_.load()<<",\"late\":"<<ring_late_.load()<<",\"young\":"<<ring_young_.load()<<",\"run\":["<<ring_[0].run.load()<<','<<ring_[1].run.load()<<','<<ring_[2].run.load()<<"]}},\"buffers\":"<<published_buffers_.load()<<",\"rig_draws_last_present\":"<<rig_draws_last_.load()
         <<",\"held_draws\":"<<held_draws_.load()<<",\"target\":{\"on\":"<<(target_on.load()?"true":"false")<<",\"draws\":"<<target_draws_.load()
         <<",\"misses\":"<<target_misses_.load()<<",\"slice_misses\":["<<target_slice_misses_[0].load()<<','<<target_slice_misses_[1].load()<<"],\"zero_draw_renders\":{\"lent\":"<<target_lent_.load()<<",\"empty\":"<<target_empty_.load()<<'}'<<",\"valid_renders\":"<<target_valid_.load()<<",\"invalid_renders\":"<<target_invalid_.load()<<",\"draws_last_render\":"<<target_draws_last_.load()<<'}'<<",\"rig_draws\":"<<rig_draws_.load()<<",\"replays_last_present\":"<<replays_last_.load()<<",\"replays\":"<<replays_.load()
         <<",\"skipped\":{\"no_depth_view\":"<<no_dsv_.load()<<",\"msaa\":"<<msaa_.load()<<"},\"failures\":"<<failures_.load();
        // v1210: nested draw hooks (inert inner scopes), slot metadata counts, the draw-time probe: tally [wild][pal_eq][cb_eq] and notes
        o<<",\"nested_scopes\":"<<nested_scopes_.load()<<",\"slot_races\":"<<races_.load()<<",\"slot_no_commit\":"<<no_commit_.load()
         <<",\"draw_probe\":{\"on\":"<<(probe_on.load()?"true":"false")<<",\"fields\":\"still: pal!cb!,pal!cb,pal cb!,pal cb; wild: same\",\"tally\":[";
        for(unsigned i=0;i<probe_tally_.size();++i) o<<(i?",":"")<<probe_tally_[i].load();
        o<<"],\"dropped\":"<<probe_dropped_.load()<<",\"notes\":[";
        {std::lock_guard<std::mutex> lock(mutex_);for(unsigned i=0;i<probe_notes_n_;++i) o<<(i?",":"")<<probe_notes_[i];}
        o<<"]}}";
        return o.str();
    }
    // Around one game draw (job threads). Inert unless enabled and the thread's pass renders the alternate view. after() right after the
    // engine's draw, with every engine binding of that draw still in place.
    class Scope {
    public:
        // v1210: a draw hook entered from inside another draw hook on this thread (rig_draws - replays == foreign_t0 == mod_ring in v1209:
        // one d3d11 draw entry point calls another hooked one, and our own replays re-enter the hooks) is the SAME draw: the outer Scope owns
        // it, the inner one is inert and leaves every thread-local of the outer alone (it used to reset them, see the outer's bound slot at
        // t0 as "foreign", and re-run ObjectMotion on our replays). ObjectMotion::DrawScope asks nested_draw().
        Scope(ID3D11DeviceContext* c,std::uintptr_t pass_view,int pass=-1) noexcept {
            nested_=tl_scope_depth_++>0;
            if(nested_) {nested_scopes_.fetch_add(1,std::memory_order_relaxed);return;}
            tl_probe_display_=~0ull;
            tl_rig_=false;tl_own_=false;tl_held_=false;tl_depth_=false;tl_target_[0]=tl_target_[1]=false;tl_pass_=pass;
            if(!c || !pass_view || !enabled.load(std::memory_order_relaxed) || pass_view!=view.load(std::memory_order_relaxed)) return;
            view_draws_.fetch_add(1,std::memory_order_relaxed);
            ID3D11DepthStencilState* bound{};UINT ref{};c->OMGetDepthStencilState(&bound,&ref);
            const int writes=writes_depth(bound);
            if(bound) bound->Release(); // the cache keeps its own reference
            if(writes<=0) return;
            tl_rig_=is_rig(c);tl_depth_=true;c_=c; // v1183: every depth-writing one-view draw may be replayed (rig, or held when ObjectMotion marks it)
            if(tl_rig_) {rig_draws_.fetch_add(1,std::memory_order_relaxed);rig_draws_frame_.fetch_add(1,std::memory_order_relaxed);}
        }
        void after(const DrawArgs& args,DrawFn draw,TargetsFn targets) noexcept {
            if(nested_) return;
            if(tl_probe_display_!=~0ull && c_) {probe_capture(c_);tl_probe_display_=~0ull;} // v1210: what this draw read, in GPU order
            if(tl_foreign_t0_) foreign_count_.store(args.count,std::memory_order_relaxed); // v1169 diagnostic: the draw's index/vertex count
            restore_t0(); // v1166: the engine's previous palette again before the replay (it sees the engine's bindings)
            if(c_ && draw && targets && (tl_rig_ || tl_held_)) replay(c_,args,draw,targets);
            c_=nullptr;
        }
        ~Scope() {--tl_scope_depth_;if(nested_) return;restore_t0();tl_held_view_.Reset();tl_rig_=false;tl_held_=false;tl_depth_=false;tl_target_[0]=tl_target_[1]=false;tl_prepass_=false;if(tl_current_) {tl_current_->Release();tl_current_=nullptr;}}
        void restore_t0() noexcept {
            if(!tl_saved_set_ || !c_) return;
            c_->VSSetShaderResources(tl_saved_slot_,1,&tl_saved_t0_);if(tl_saved_t0_) tl_saved_t0_->Release();
            tl_saved_t0_=nullptr;tl_saved_set_=false;
        }
        Scope(const Scope&)=delete;Scope& operator=(const Scope&)=delete;
    private:
        ID3D11DeviceContext* c_{};bool nested_{};
    };
    static bool nested_draw() noexcept {return tl_scope_depth_>1;} // inside a draw hook entered from another draw hook (v1210)
private:
    struct Target {ID3D11Resource* depth{};Microsoft::WRL::ComPtr<ID3D11Resource> hold;Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv;Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;UINT w{},h{};
        Microsoft::WRL::ComPtr<ID3D11Texture2D> trg;std::array<Microsoft::WRL::ComPtr<ID3D11RenderTargetView>,2> trg_rtv;std::array<Microsoft::WRL::ComPtr<ID3D11DepthStencilView>,2> trg_dsv;}; // v1190; v1199 two slices
    inline static thread_local bool tl_target_[2]{};
    inline static std::atomic<std::uint64_t> target_draws_{},target_draws_frame_{},target_draws_last_{},target_misses_{},target_valid_{},target_invalid_{},target_lent_{},target_empty_{};
    inline static std::array<std::atomic<std::uint64_t>,2> target_misses_frame_{},target_slice_misses_{};
    inline static Microsoft::WRL::ComPtr<ID3D11PixelShader> ps_target_rig_,ps_target_held_;inline static Microsoft::WRL::ComPtr<ID3D11DepthStencilState> nearest_;
    struct Entry {std::atomic<ID3D11DepthStencilState*> key{};std::atomic<bool> used{};bool writes{};};
    static constexpr unsigned slots=1024;
    inline static std::array<Entry,slots> table_{};inline static std::atomic<int> default_writes_{1}; // a null state is the default: depth written
    inline static std::mutex mutex_;inline static Target target_{};
    inline static Microsoft::WRL::ComPtr<ID3D11PixelShader> ps_,ps_own_,ps_held_;inline static Microsoft::WRL::ComPtr<ID3D11DepthStencilState> equal_;
    inline static Microsoft::WRL::ComPtr<ID3D11BlendState> blend_;inline static bool made_{},make_failed_{};inline static ID3D11Device* device_{}; // identity: whose objects these are
    inline static std::array<std::atomic<std::uintptr_t>,buffer_slots> buffers_{};inline static std::atomic<unsigned> published_buffers_{};
    inline static std::atomic<std::uint64_t> rig_draws_{},rig_draws_frame_{},rig_draws_last_{},replays_{},replays_frame_{},replays_last_{},view_draws_{},
        no_dsv_{},msaa_{},failures_{};
    inline static thread_local bool tl_rig_{};
    // v1166: this draw's rig model (0, 1 arms, 2 body), its current palette view (t1, held), whether t0/t1 are that model's previous/current
    inline static thread_local unsigned tl_model_{};inline static thread_local ID3D11ShaderResourceView* tl_current_{};inline static thread_local bool tl_prepass_{};
    inline static thread_local ID3D11ShaderResourceView* tl_saved_t0_{};inline static thread_local bool tl_saved_set_{};
    // v1179: per rig model, the ring the GPU skin copy rotates (P bound at draws; A, B the last two renders' palettes) and P's view
    // v1206: rig_ring_slots palettes per model, slot frame%rig_ring_slots written once by that render's skin copy and tagged with its frame
    struct Ring {std::vector<std::uint8_t> upload;std::array<bool,rig_ring_slots> from_upload{}; // v1208: the last uploaded CPU palette
        std::array<Microsoft::WRL::ComPtr<ID3D11Buffer>,rig_ring_slots> slot;std::array<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>,rig_ring_slots> view;
        std::array<std::atomic<std::uint32_t>,rig_ring_slots> tag{};UINT bytes{};
        std::atomic<bool> ready{};std::atomic<std::uint32_t> last{~0u};std::atomic<unsigned> run{};ID3D11Device* device{};
        std::array<std::vector<std::uint8_t>,rig_ring_slots> shadow;std::array<bool,rig_ring_slots> commit{},race{},writer_deferred{}; // v1210 probe
        void clear() noexcept {for(unsigned i=0;i<rig_ring_slots;++i) {view[i].Reset();slot[i].Reset();tag[i].store(~0u,std::memory_order_relaxed);from_upload[i]=false;shadow[i].clear();}upload.clear();run.store(0);last.store(~0u);}};
    // v1210 draw-time probe (diagnostics): for the first own rig draw of each model in a render, right after the engine's draw and on its
    // context (GPU order), copies of what that draw read: t0 (our ring slot), t1 (the engine's current palette), b2 (our pool rows). Read
    // back on the immediate context once the render's mask census is in (its command lists have executed) and compared with what the CPU
    // meant to give it: the slot's shadow bytes and the pool bytes ObjectMotion wrote (probe_arm).
    struct Probe {std::uint64_t display{~0ull};unsigned model{},state{},polls{};std::uint32_t tag{};bool deferred{},commit{},race{},writer_deferred{};
        Microsoft::WRL::ComPtr<ID3D11Buffer> pal,cur,cb;std::vector<std::uint8_t> want_pal,want_cb;};
    inline static std::array<Probe,32> probes_{};inline static unsigned probe_next_{};
    inline static std::array<std::atomic<std::uint64_t>,8> probe_tally_{}; // [wild*4 + pal_eq*2 + cb_eq]
    inline static std::array<std::string,12> probe_notes_{};inline static unsigned probe_notes_n_{};
    inline static std::atomic<std::uint64_t> races_{},no_commit_{},nested_scopes_{},probe_dropped_{};
    inline static thread_local unsigned tl_scope_depth_{};inline static thread_local std::uint64_t tl_probe_display_{~0ull};inline static thread_local std::vector<std::uint8_t> tl_probe_cb_;
    static bool make_staging(ID3D11Device* device,ID3D11Resource* src,Microsoft::WRL::ComPtr<ID3D11Buffer>& out) noexcept {
        Microsoft::WRL::ComPtr<ID3D11Buffer> b;if(!device || !src || FAILED(src->QueryInterface(IID_PPV_ARGS(&b)))) return false;
        D3D11_BUFFER_DESC d{};b->GetDesc(&d);
        if(out) {D3D11_BUFFER_DESC h{};out->GetDesc(&h);if(h.ByteWidth==d.ByteWidth && h.MiscFlags==(d.MiscFlags&D3D11_RESOURCE_MISC_BUFFER_STRUCTURED)) return true;out.Reset();}
        d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;d.MiscFlags&=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;if(!d.MiscFlags) d.StructureByteStride=0;
        return SUCCEEDED(device->CreateBuffer(&d,nullptr,&out));
    }
    static void probe_capture(ID3D11DeviceContext* c) noexcept { // Scope::after, right after the engine's draw (t0 still our slot, b2 our pool)
        const unsigned model=tl_model_,k=tl_bound_slot_;if(model>=ring_.size() || k>=rig_ring_slots || !tl_current_) return;
        Microsoft::WRL::ComPtr<ID3D11Buffer> cb;c->VSGetConstantBuffers(2,1,&cb);Microsoft::WRL::ComPtr<ID3D11Resource> cur;tl_current_->GetResource(&cur);
        Microsoft::WRL::ComPtr<ID3D11Device> device;c->GetDevice(&device);
        if(!cb || !cur || !device) return;
        try {
            std::lock_guard<std::mutex> lock(mutex_);
            Ring& r=ring_[model];
            if(!r.ready.load(std::memory_order_relaxed) || r.tag[k].load(std::memory_order_relaxed)!=tl_bound_tag_ || !r.slot[k] || r.shadow[k].size()!=r.bytes || device.Get()!=r.device) return;
            for(const auto& p:probes_) if(p.state && p.display==tl_probe_display_ && p.model==model) return; // one per render and model
            Probe* p=nullptr;for(unsigned i=0;i<probes_.size() && !p;++i) {Probe& q=probes_[(probe_next_+i)%probes_.size()];if(q.state!=1) p=&q;}
            if(!p) {probe_dropped_.fetch_add(1,std::memory_order_relaxed);return;}
            ++probe_next_;p->state=0;
            if(!make_staging(device.Get(),r.slot[k].Get(),p->pal) || !make_staging(device.Get(),cur.Get(),p->cur) || !make_staging(device.Get(),cb.Get(),p->cb)) return;
            c->CopyResource(p->pal.Get(),r.slot[k].Get());c->CopyResource(p->cur.Get(),cur.Get());c->CopyResource(p->cb.Get(),cb.Get());
            p->want_pal=r.shadow[k];p->want_cb=tl_probe_cb_;p->display=tl_probe_display_;p->model=model;p->tag=tl_bound_tag_;p->polls=0;
            p->deferred=c->GetType()==D3D11_DEVICE_CONTEXT_DEFERRED;p->commit=r.commit[k];p->race=r.race[k];p->writer_deferred=r.writer_deferred[k];p->state=1;
        } catch(...) {}
    }
    // max |translation| difference over the joints of two packed 3x4 palettes (metres; NaN-safe: a non-finite difference reads 1e9)
    static double palette_gap(const float* a,const float* b,std::size_t bytes) noexcept {
        double gap=0;for(std::size_t j=0;j+48<=bytes;j+=48) for(unsigned t:{3u,7u,11u}) {const double d=std::abs(double(a[j/4+t])-double(b[j/4+t]));gap=std::max(gap,d==d?d:1e9);}
        return gap;
    }
    static void poll_probes(ID3D11DeviceContext* immediate) noexcept { // under mutex_
        for(auto& p:probes_) if(p.state==1) {
            std::uint32_t m[3]{};
            if(!mask_counts(p.display,m)) {if(++p.polls>2000) p.state=0;continue;} // the render's census (its lists have executed) not in yet
            D3D11_MAPPED_SUBRESOURCE a{},b{},cbm{};
            if(FAILED(immediate->Map(p.pal.Get(),0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,&a)) || !a.pData) continue;
            if(FAILED(immediate->Map(p.cur.Get(),0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,&b)) || !b.pData) {immediate->Unmap(p.pal.Get(),0);continue;}
            if(FAILED(immediate->Map(p.cb.Get(),0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,&cbm)) || !cbm.pData) {immediate->Unmap(p.cur.Get(),0);immediate->Unmap(p.pal.Get(),0);continue;}
            D3D11_BUFFER_DESC pd{},cd{};p.pal->GetDesc(&pd);p.cb->GetDesc(&cd);
            const std::size_t pal_bytes=std::min<std::size_t>(pd.ByteWidth,p.want_pal.size()),cb_bytes=std::min<std::size_t>(cd.ByteWidth,p.want_cb.size());
            const auto* gp=static_cast<const float*>(a.pData);const auto* gc=static_cast<const float*>(b.pData);const auto* gb=static_cast<const float*>(cbm.pData);
            const bool pal_eq=pal_bytes && !std::memcmp(gp,p.want_pal.data(),pal_bytes),cb_eq=cb_bytes && !std::memcmp(gb,p.want_cb.data(),cb_bytes);
            std::size_t pal_diff=0;for(std::size_t i=0;i<pal_bytes;++i) pal_diff+=static_cast<const std::uint8_t*>(a.pData)[i]!=p.want_pal[i];
            const double slot_cur=palette_gap(gp,gc,pal_bytes),want_cur=palette_gap(reinterpret_cast<const float*>(p.want_pal.data()),gc,pal_bytes);
            std::size_t cb_first=~std::size_t{0};for(std::size_t i=0;i+4<=cb_bytes && cb_first==~std::size_t{0};i+=4) if(std::memcmp(gb+i/4,p.want_cb.data()+i,4)) cb_first=i/4;
            const bool wild=m[2]>0;probe_tally_[(wild?4u:0u)+(pal_eq?2u:0u)+(cb_eq?1u:0u)].fetch_add(1,std::memory_order_relaxed);
            if((wild || ((!pal_eq || !cb_eq) && probe_notes_n_<4)) && probe_notes_n_<probe_notes_.size()) try { // wild renders first; at most 4 other mismatches
                float want_f=0;if(cb_first!=~std::size_t{0}) std::memcpy(&want_f,p.want_cb.data()+cb_first*4,4);
                std::ostringstream o;o<<"{\"display\":"<<p.display<<",\"model\":"<<p.model<<",\"tag\":"<<p.tag<<",\"wild\":"<<(wild?1:0)<<",\"pal_eq\":"<<(pal_eq?1:0)<<",\"pal_diff_bytes\":"<<pal_diff
                 <<",\"slot_vs_cur_m\":"<<slot_cur<<",\"want_vs_cur_m\":"<<want_cur<<",\"cb_eq\":"<<(cb_eq?1:0)<<",\"cb_first_float\":"<<static_cast<long long>(cb_first==~std::size_t{0}?-1:static_cast<long long>(cb_first))
                 <<",\"cb_gpu\":"<<(cb_first==~std::size_t{0}?0.f:gb[cb_first])<<",\"cb_cpu\":"<<want_f<<",\"draw_deferred\":"<<(p.deferred?1:0)<<",\"writer_deferred\":"<<(p.writer_deferred?1:0)
                 <<",\"commit\":"<<(p.commit?1:0)<<",\"race\":"<<(p.race?1:0)<<",\"mask\":["<<m[0]<<','<<m[1]<<','<<m[2]<<"]}";
                probe_notes_[probe_notes_n_++]=o.str();
            } catch(...) {}
            immediate->Unmap(p.cb.Get(),0);immediate->Unmap(p.cur.Get(),0);immediate->Unmap(p.pal.Get(),0);p.state=2;
        }
    }
public:
    inline static std::atomic<bool> probe_on{false}; // v1210: dxgi_probe, each Present: rig motion on and diagnostics on
    // ObjectMotion, at an own rig draw: the b2 bytes it binds (its pool), for the draw-time probe of this render (display) and model
    static void probe_arm(std::uint64_t display,const void* cb,UINT bytes) noexcept {
        if(!probe_on.load(std::memory_order_relaxed) || display==~0ull || !cb || !bytes) return;
        try {tl_probe_cb_.assign(static_cast<const std::uint8_t*>(cb),static_cast<const std::uint8_t*>(cb)+bytes);tl_probe_display_=display;} catch(...) {}
    }
private:
    struct Fingerprint {Microsoft::WRL::ComPtr<ID3D11Buffer> cur,oth;std::vector<std::uint8_t> cpu;bool pending{};};
    inline static std::array<Fingerprint,3> fingerprints_{};inline static std::array<std::atomic<std::uint64_t>,4> fp_{};inline static std::atomic<std::uint64_t> fp_first_diff_{~0ull};
    inline static std::atomic<std::uint64_t> uploads_{},no_upload_{},layout_refused_{};
    struct MaskCensus {std::atomic<std::uint64_t> display{~0ull};std::array<std::atomic<std::uint32_t>,3> n{};};
    inline static std::array<MaskCensus,128> mask_census_{};
    struct Guard {ID3D11Device* device{};Microsoft::WRL::ComPtr<ID3D11VertexShader> vs;Microsoft::WRL::ComPtr<ID3D11PixelShader> ps;Microsoft::WRL::ComPtr<ID3D11BlendState> blend;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depth;Microsoft::WRL::ComPtr<ID3D11RasterizerState> raster;Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv;ID3D11Resource* rtv_of{};bool failed{};};
    inline static Guard guard_{};
    inline static std::array<Ring,3> ring_{};inline static thread_local bool tl_own_{};inline static thread_local Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> tl_held_view_;inline static thread_local bool tl_held_{},tl_depth_{};
    inline static std::atomic<std::uint64_t> held_draws_{};
    inline static std::atomic<std::uint64_t> own_pairs_{},zero_pairs_{},no_history_{},unpaired_{},rotations_{},ring_restarts_{},ring_repeats_{},foreign_ours_{},ring_late_{},ring_young_{};
    // P's view (made like the draw's current palette view) when the model's ring holds this eye's previous render, else null
    // Held by the caller's thread until the draw's Scope ends (a reset may release the ring meanwhile). ~13 rig draws a render: the lock is cheap.
    static ID3D11ShaderResourceView* ring_view(ID3D11DeviceContext* c,unsigned model,std::uint32_t frame) noexcept {
        if(model>=ring_.size()) return nullptr;
        Ring& r=ring_[model];
        if(!r.ready.load(std::memory_order_acquire)) return nullptr;
        // v1206: `frame` = the render whose palette the draw needs (this eye's previous render, from its receipt). Only the slot tagged with
        // exactly that frame: written by that render's skin copy and not reused for rig_ring_slots engine frames (no rotation can move it)
        const unsigned k=frame%rig_ring_slots;
        if(frame==~0u || r.tag[k].load(std::memory_order_acquire)!=frame) {ring_late_.fetch_add(1,std::memory_order_relaxed);return nullptr;}
        std::lock_guard<std::mutex> lock(mutex_);
        if(!r.ready.load(std::memory_order_relaxed) || r.tag[k].load(std::memory_order_relaxed)!=frame) return nullptr;
        ID3D11Device* device{};c->GetDevice(&device);if(device) device->Release();
        if(device!=r.device) return nullptr; // the ring is another device's
        if(!r.view[k] && r.slot[k]) {
            D3D11_SHADER_RESOURCE_VIEW_DESC vd{};tl_current_->GetDesc(&vd);
            if(FAILED(device->CreateShaderResourceView(r.slot[k].Get(),&vd,&r.view[k]))) failures_.fetch_add(1,std::memory_order_relaxed);
        }
        tl_bound_slot_=k;tl_bound_tag_=frame;tl_bound_buffer_=reinterpret_cast<std::uintptr_t>(r.slot[k].Get());
        tl_held_view_=r.view[k];return tl_held_view_.Get();
    }
    // the unpaired draws' binding pattern (t0..t3: 0 none, 1 this model wrapper 0, 2 wrapper 1, 3 another rig model's, 4 another resource)
    inline static std::array<std::atomic<std::uint64_t>,625> unpaired_patterns_{},depth_only_patterns_{};
    inline static std::atomic<std::uint64_t> depth_only_{};inline static std::atomic<int> unpaired_pass_{-2},depth_only_pass_{-2};inline static std::atomic<unsigned> unpaired_targets_{};
    inline static thread_local int tl_pass_{-1};inline static thread_local bool tl_foreign_t0_{};
public:
    // v1209 forensics (ObjectMotion's per-draw record): the last ring slot bound on this thread, the draw's model and current palette view
    inline static thread_local unsigned tl_bound_slot_{~0u};inline static thread_local std::uint32_t tl_bound_tag_{~0u};inline static thread_local std::uintptr_t tl_bound_buffer_{};
    static unsigned draw_model() noexcept {return tl_model_;}
    static bool draw_foreign_t0() noexcept {return tl_foreign_t0_;}
    static std::uintptr_t draw_current() noexcept {ID3D11Resource* r{};if(tl_current_) tl_current_->GetResource(&r);const auto a=reinterpret_cast<std::uintptr_t>(r);if(r) r->Release();return a;}
private:
    // v1169 diagnostic: the last foreign t0 buffer seen (see is_rig) and how many distinct ones
    inline static std::atomic<std::uint64_t> foreign_t0_{};inline static std::atomic<UINT> foreign_bytes_{},foreign_current_bytes_{},foreign_bind_{},foreign_misc_{},foreign_stride_{},
        foreign_usage_{},foreign_cpu_{},foreign_format_{},foreign_dimension_{},foreign_elements_{},foreign_distinct_{},foreign_count_{};
    inline static std::array<std::atomic<std::uintptr_t>,16> foreign_seen_{};
    static void describe_foreign(ID3D11ShaderResourceView* t0,ID3D11ShaderResourceView* t1) noexcept {
        const auto buffer_desc=[](ID3D11ShaderResourceView* v,D3D11_BUFFER_DESC& d) {
            ID3D11Resource* r{};v->GetResource(&r);ID3D11Buffer* b{};const auto at=reinterpret_cast<std::uintptr_t>(r);
            if(r) {r->QueryInterface(__uuidof(ID3D11Buffer),reinterpret_cast<void**>(&b));r->Release();}
            if(b) {b->GetDesc(&d);b->Release();}
            return at;};
        D3D11_BUFFER_DESC d{},cd{};const auto at=buffer_desc(t0,d);buffer_desc(t1,cd);
        for(const auto& r:ring_) if(r.ready.load(std::memory_order_acquire)) for(const auto& b:r.slot) if(at==reinterpret_cast<std::uintptr_t>(b.Get())) foreign_ours_.fetch_add(1,std::memory_order_relaxed); // v1179: a mod buffer left bound
        D3D11_SHADER_RESOURCE_VIEW_DESC vd{};t0->GetDesc(&vd);
        foreign_bytes_.store(d.ByteWidth,std::memory_order_relaxed);foreign_current_bytes_.store(cd.ByteWidth,std::memory_order_relaxed);foreign_bind_.store(d.BindFlags,std::memory_order_relaxed);
        foreign_misc_.store(d.MiscFlags,std::memory_order_relaxed);foreign_stride_.store(d.StructureByteStride,std::memory_order_relaxed);foreign_usage_.store(d.Usage,std::memory_order_relaxed);
        foreign_cpu_.store(d.CPUAccessFlags,std::memory_order_relaxed);foreign_format_.store(vd.Format,std::memory_order_relaxed);foreign_dimension_.store(vd.ViewDimension,std::memory_order_relaxed);
        foreign_elements_.store(vd.ViewDimension==D3D11_SRV_DIMENSION_BUFFEREX?vd.BufferEx.NumElements:vd.Buffer.NumElements,std::memory_order_relaxed);
        for(auto& s:foreign_seen_) {std::uintptr_t k=s.load(std::memory_order_relaxed);if(k==at) return;if(!k && s.compare_exchange_strong(k,at)) {foreign_distinct_.fetch_add(1,std::memory_order_relaxed);return;}}
    }inline static thread_local UINT tl_prev_slot_{},tl_saved_slot_{};
    inline static std::atomic<std::uint32_t> epoch_{1};inline static thread_local unsigned tl_pattern_{};

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
        constexpr unsigned slots_max=8; // v1168: t0..t7 (v1167 looked at t0..t2 only); v1171: t0..t2 without the rig motion feature
        const unsigned scan=motion.load(std::memory_order_relaxed)?slots_max:3u;
        ID3D11ShaderResourceView* views[slots_max]{};c->VSGetShaderResources(0,scan,views);
        bool hit=false;int which[slots_max]{-1,-1,-1,-1,-1,-1,-1,-1}; // the rig buffer slot (model*2 + wrapper) each view is of
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
        // v1169 (sim v1168: 3.8 prepass draws per Present, pattern 4100/4200: t1 this model's current palette, t0 a buffer that is none of
        // the six rig palettes, t2/t3 empty): the measured prepass layout (ro_previousframeskinningmatrices at t0, ro_matrices at t1) with a
        // previous palette the engine took elsewhere. t0 is the previous-palette slot by layout: it gets this eye's own copy (or the current
        // palette) like any prepass draw. Its buffer is described in the status (foreign_t0) to name it in the next sim.
        tl_foreign_t0_=false;
        if(motion.load(std::memory_order_relaxed) && !tl_prepass_ && which[1]>=0 && views[0] && which[0]<0 && !views[2] && !views[3]) {
            tl_prepass_=true;tl_prev_slot_=0;tl_foreign_t0_=true;foreign_t0_.fetch_add(1,std::memory_order_relaxed);
            describe_foreign(views[0],views[1]);
        }
        if(tl_prepass_) {tl_model_=static_cast<unsigned>(which[1]/2);tl_current_=views[1];views[1]=nullptr;}
        for(auto* v:views) if(v) v->Release();
        return hit;
    }
    // Under mutex_: the shader and states (once), and the rig target for this depth resource (recreated on a new depth/size).
    static bool prepare(ID3D11Device* device,ID3D11Resource* depth,const D3D11_TEXTURE2D_DESC& dd,bool& created) {
        created=false;
        if(device_!=device) {target_={};ps_.Reset();ps_own_.Reset();ps_held_.Reset();ps_target_rig_.Reset();ps_target_held_.Reset();nearest_.Reset();equal_.Reset();blend_.Reset();made_=make_failed_=false;device_=device;} // a new device
        if(!made_ && !make_failed_) {
            // v1179: y = 1 when the draw had this eye's own previous palette and rows (its MV pixels are own motion), else 0
            static const char src[]="float2 main(float4 p:SV_Position):SV_Target{return float2(p.z,0);}";
            static const char src_own[]="float2 main(float4 p:SV_Position):SV_Target{return float2(p.z,1);}";
            static const char src_held[]="float2 main(float4 p:SV_Position):SV_Target{return float2(p.z,2);}"; // v1183: a held rigid item
            Microsoft::WRL::ComPtr<ID3DBlob> code,code_own,code_held;
            D3D11_DEPTH_STENCIL_DESC ds{};ds.DepthEnable=TRUE;ds.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;ds.DepthFunc=D3D11_COMPARISON_EQUAL;ds.StencilEnable=FALSE;
            D3D11_BLEND_DESC bs{};bs.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL; // no blending, every channel
            make_failed_=FAILED(D3DCompile(src,sizeof src-1,nullptr,nullptr,nullptr,"main","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,nullptr)) ||
                FAILED(device->CreatePixelShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&ps_)) ||
                FAILED(D3DCompile(src_own,sizeof src_own-1,nullptr,nullptr,nullptr,"main","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code_own,nullptr)) ||
                FAILED(device->CreatePixelShader(code_own->GetBufferPointer(),code_own->GetBufferSize(),nullptr,&ps_own_)) ||
                FAILED(D3DCompile(src_held,sizeof src_held-1,nullptr,nullptr,nullptr,"main","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code_held,nullptr)) ||
                FAILED(device->CreatePixelShader(code_held->GetBufferPointer(),code_held->GetBufferSize(),nullptr,&ps_held_)) ||
                FAILED(device->CreateDepthStencilState(&ds,&equal_)) || FAILED(device->CreateBlendState(&bs,&blend_));
            if(!make_failed_) { // v1190: the rig target's shaders and nearest-wins depth state
                static const char src_tr[]="float2 main(float4 p:SV_Position):SV_Target{return float2(p.z,255);}";
                static const char src_th[]="float2 main(float4 p:SV_Position):SV_Target{return float2(p.z,253);}";
                Microsoft::WRL::ComPtr<ID3DBlob> a,b;D3D11_DEPTH_STENCIL_DESC nd{};nd.DepthEnable=TRUE;nd.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;nd.DepthFunc=D3D11_COMPARISON_GREATER;
                make_failed_=FAILED(D3DCompile(src_tr,sizeof src_tr-1,nullptr,nullptr,nullptr,"main","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&a,nullptr)) ||
                    FAILED(device->CreatePixelShader(a->GetBufferPointer(),a->GetBufferSize(),nullptr,&ps_target_rig_)) ||
                    FAILED(D3DCompile(src_th,sizeof src_th-1,nullptr,nullptr,nullptr,"main","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&b,nullptr)) ||
                    FAILED(device->CreatePixelShader(b->GetBufferPointer(),b->GetBufferSize(),nullptr,&ps_target_held_)) ||
                    FAILED(device->CreateDepthStencilState(&nd,&nearest_));
            }
            made_=!make_failed_;
        }
        if(!made_) return false;
        if(target_.depth==depth && target_.w==dd.Width && target_.h==dd.Height && target_.rtv) return true;
        target_={};
        D3D11_TEXTURE2D_DESC t{};t.Width=dd.Width;t.Height=dd.Height;t.MipLevels=t.ArraySize=1;t.Format=DXGI_FORMAT_R32G32_FLOAT;t.SampleDesc.Count=1;
        t.Usage=D3D11_USAGE_DEFAULT;t.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;Target n{};
        if(FAILED(device->CreateTexture2D(&t,nullptr,&tex)) || FAILED(device->CreateRenderTargetView(tex.Get(),nullptr,&n.rtv)) ||
           FAILED(device->CreateShaderResourceView(tex.Get(),nullptr,&n.srv))) return false;
        { // v1190: the rig target and its own depth (same size)
            D3D11_TEXTURE2D_DESC a=t;a.ArraySize=2; // v1199: slice h = the hypothesis "the draw is a render of eye h"
            if(FAILED(device->CreateTexture2D(&a,nullptr,&n.trg))) return false;
            D3D11_TEXTURE2D_DESC z=a;z.Format=DXGI_FORMAT_D32_FLOAT;z.BindFlags=D3D11_BIND_DEPTH_STENCIL;Microsoft::WRL::ComPtr<ID3D11Texture2D> zt;
            if(FAILED(device->CreateTexture2D(&z,nullptr,&zt))) return false;
            for(UINT k=0;k<2;++k) {
                D3D11_RENDER_TARGET_VIEW_DESC rv{};rv.Format=t.Format;rv.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2DARRAY;rv.Texture2DArray.FirstArraySlice=k;rv.Texture2DArray.ArraySize=1;
                D3D11_DEPTH_STENCIL_VIEW_DESC dv{};dv.Format=z.Format;dv.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2DARRAY;dv.Texture2DArray.FirstArraySlice=k;dv.Texture2DArray.ArraySize=1;
                if(FAILED(device->CreateRenderTargetView(n.trg.Get(),&rv,&n.trg_rtv[k])) || FAILED(device->CreateDepthStencilView(zt.Get(),&dv,&n.trg_dsv[k]))) return false;
            }
        }
        n.depth=depth;n.hold=depth;n.w=dd.Width;n.h=dd.Height;target_=n;created=true;
        return true;
    }
    static void replay(ID3D11DeviceContext* c,const DrawArgs& a,DrawFn draw,TargetsFn targets) noexcept {
        std::array<ID3D11RenderTargetView*,D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> rtvs{};ID3D11DepthStencilView* dsv{};
        c->OMGetRenderTargets(static_cast<UINT>(rtvs.size()),rtvs.data(),&dsv);
        const auto release_targets=[&] {for(auto* r:rtvs) if(r) r->Release();if(dsv) dsv->Release();};
        if(!dsv) {no_dsv_.fetch_add(1,std::memory_order_relaxed);release_targets();return;}
        ID3D11RenderTargetView* rig{};Microsoft::WRL::ComPtr<ID3D11PixelShader> ps;Microsoft::WRL::ComPtr<ID3D11DepthStencilState> equal;Microsoft::WRL::ComPtr<ID3D11BlendState> opaque; // held: a reset cannot free them mid-replay
        std::array<Microsoft::WRL::ComPtr<ID3D11RenderTargetView>,2> trg_rtv;std::array<Microsoft::WRL::ComPtr<ID3D11DepthStencilView>,2> trg_dsv;Microsoft::WRL::ComPtr<ID3D11PixelShader> trg_ps;Microsoft::WRL::ComPtr<ID3D11DepthStencilState> nearest; // v1190
        {
            ID3D11Resource* depth{};dsv->GetResource(&depth);
            D3D11_TEXTURE2D_DESC dd{};ID3D11Texture2D* dt{};
            if(depth && SUCCEEDED(depth->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&dt))) && dt) {dt->GetDesc(&dd);dt->Release();}
            if(dd.SampleDesc.Count!=1) msaa_.fetch_add(1,std::memory_order_relaxed);
            else {
                ID3D11Device* device{};c->GetDevice(&device);bool created=false;
                try {
                    std::lock_guard<std::mutex> lock(mutex_);
                    if(device && dd.Width && prepare(device,depth,dd,created)) {rig=target_.rtv.Get();rig->AddRef();ps=tl_held_?ps_held_:tl_own_?ps_own_:ps_;equal=equal_;opaque=blend_;
                        trg_rtv=target_.trg_rtv;trg_dsv=target_.trg_dsv;trg_ps=tl_held_?ps_target_held_:ps_target_rig_;nearest=nearest_;}
                } catch(...) {}
                if(device) device->Release();
                if(rig && created) {const float zero[4]{};c->ClearRenderTargetView(rig,zero);for(unsigned k=0;k<2;++k) {c->ClearRenderTargetView(trg_rtv[k].Get(),zero);c->ClearDepthStencilView(trg_dsv[k].Get(),D3D11_CLEAR_DEPTH,0.f,0);}}
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
        // v1190: the same draw in the stale eye's target camera (retargeted per-instance rows), nearest wins among the replayed draws
        if((tl_target_[0] || tl_target_[1]) && trg_rtv[0] && trg_rtv[1] && trg_dsv[0] && trg_dsv[1]) {
            const auto bind=target_bind.load(std::memory_order_relaxed);const auto restore=target_restore.load(std::memory_order_relaxed);
            bool drew=false;
            for(unsigned k=0;k<2;++k) { // v1199: one replay per hypothesis slice
                ID3D11RenderTargetView* t=trg_rtv[k].Get();
                if(tl_target_[k] && bind && restore && bind(c,k)) {
                    targets(c,1,&t,trg_dsv[k].Get());c->PSSetShader(trg_ps.Get(),nullptr,0);c->OMSetDepthStencilState(nearest.Get(),0);
                    draw(c,a);drew=true;
                } else {target_slice_misses_[k].fetch_add(1,std::memory_order_relaxed);target_misses_frame_[k].fetch_add(1,std::memory_order_relaxed);}
            }
            if(drew && restore) restore(c);
            if(drew) {target_draws_.fetch_add(1,std::memory_order_relaxed);target_draws_frame_.fetch_add(1,std::memory_order_relaxed);}
            else target_misses_.fetch_add(1,std::memory_order_relaxed);
        } else if(target_on.load(std::memory_order_relaxed)) for(auto& m:target_misses_frame_) m.fetch_add(1,std::memory_order_relaxed); // a rig/held draw without its retarget
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
