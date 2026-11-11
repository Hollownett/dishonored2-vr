#pragma once
// v1182 performance mode: this eye's own previous skin palette for every skinned model that writes motion vectors (NPCs, bodies, creatures),
// knob build/stale-eye-skin-motion (opt-in). The player rig keeps its own path (rig_mask.hpp, same idea).
//
// Why: the skinned depth-prepass VS skins the previous position with ro_previousframeskinningmatrices (VS t0) = the palette of the last
// ENGINE frame (0x25dfd0 swaps the two wrappers each frame), i.e. one tick old, while ObjectMotion gives the draw this eye's own previous rows
// (two ticks, other_last chain) in alternate mode: a walking NPC's limb vectors carried its root motion over two ticks but its bone motion
// over one. Fix, as for the rig: at each engine frame's GPU skin copy of a model (0x25f930 hook, on the engine's upload context, ahead of the
// draws) three buffers rotate P <- B <- A <- the new current palette, so P holds the palette of two engine frames back = this eye's
// previous render; a draw whose rows are the eye's own (other_last) and whose VS t1 is that model's current wrapper gets P at t0 for the draw
// (restored by ObjectMotion::DrawScope). Valid after three consecutive engine frames (rig_ring_valid); a model the engine skipped restarts.
// Bound: max_models entries and max_bytes of ring buffers; a model beyond either is counted (overflow) and keeps the engine's palette. An
// entry not rotated for 240 engine frames is reused.
#include <d3d11.h>
#include <wrl/client.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <sstream>
#include <string>
#include "rig_mask.hpp" // rig_ring_run / rig_ring_valid

namespace d2vr {
class SkinMotion {
public:
    inline static std::atomic<bool> enabled{false};
    static constexpr unsigned max_models=64;static constexpr std::uint64_t max_bytes=8ull<<20;static constexpr std::uint32_t stale_frames=240;
    // The 0x25f930 hook, after the engine's upload: `model` the render model, `w0`/`w1` its two palette wrappers' buffers, `current` the one
    // holding this render's palette, `frame` the engine frame counter.
    static void skin_copied(ID3D11DeviceContext* c,std::uintptr_t model,ID3D11Buffer* w0,ID3D11Buffer* w1,ID3D11Buffer* current,std::uint32_t frame) noexcept {
        if(!enabled.load(std::memory_order_relaxed) || !c || !model || !current) return;
        D3D11_BUFFER_DESC d{};current->GetDesc(&d);
        ID3D11Device* owner{};current->GetDevice(&owner);if(owner) owner->Release(); // identity only
        std::lock_guard<std::mutex> lock(mutex_);
        Entry* e=find_model(model);
        if(!e) e=claim(model,frame);
        if(!e) {overflow_.fetch_add(1,std::memory_order_relaxed);return;}
        e->w0=w0;e->w1=w1;e->seen=frame;
        if(!e->p || e->bytes!=d.ByteWidth || e->device!=owner) {
            release(*e);
            if(bytes_+3ull*d.ByteWidth>max_bytes) {overflow_.fetch_add(1,std::memory_order_relaxed);e->model=0;return;}
            d.Usage=D3D11_USAGE_DEFAULT;d.CPUAccessFlags=0;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
            if(!owner || FAILED(owner->CreateBuffer(&d,nullptr,&e->p)) || FAILED(owner->CreateBuffer(&d,nullptr,&e->a)) || FAILED(owner->CreateBuffer(&d,nullptr,&e->b))) {
                release(*e);e->model=0;failures_.fetch_add(1,std::memory_order_relaxed);return;
            }
            e->bytes=d.ByteWidth;e->device=owner;bytes_+=3ull*d.ByteWidth;
        }
        if(e->last==frame) return; // one rotation per engine frame
        const unsigned run=rig_ring_run(e->last,e->run,frame);
        if(run==1 && e->last!=~0u) restarts_.fetch_add(1,std::memory_order_relaxed);
        c->CopyResource(e->p.Get(),e->b.Get());c->CopyResource(e->b.Get(),e->a.Get());c->CopyResource(e->a.Get(),current);
        e->last=frame;e->run=run;rotations_.fetch_add(1,std::memory_order_relaxed);
    }
    // ObjectMotion, at a non-rig draw that got this eye's own previous rows in alternate mode: P at t0 when t1 is a known model's wrapper and
    // its ring is valid. True: bound (restore() after the draw puts the engine's t0 back).
    static bool bind_previous(ID3D11DeviceContext* c) noexcept {
        if(!enabled.load(std::memory_order_relaxed) || !c || tl_saved_set_) return false;
        ID3D11ShaderResourceView* v[2]{};c->VSGetShaderResources(0,2,v);
        bool bound=false;
        if(v[0] && v[1]) {
            ID3D11Resource* r0{};ID3D11Resource* r1{};v[0]->GetResource(&r0);v[1]->GetResource(&r1);
            {
                std::lock_guard<std::mutex> lock(mutex_);
                Entry* e=r1?find_buffer(r1):nullptr;
                // the prepass form: t1 this model's current wrapper, t0 its other wrapper (or the same: the engine's own history reset)
                if(e && (r0==e->w0 || r0==e->w1) && rig_ring_valid(e->run) && e->p) {
                    if(!e->view) {
                        D3D11_SHADER_RESOURCE_VIEW_DESC vd{};v[1]->GetDesc(&vd);ID3D11Device* device{};c->GetDevice(&device);
                        if(device && device==e->device && FAILED(device->CreateShaderResourceView(e->p.Get(),&vd,&e->view))) failures_.fetch_add(1,std::memory_order_relaxed);
                        if(device) device->Release();
                    }
                    if(e->view) {tl_held_=e->view;bound=true;}
                } else if(!e) unknown_.fetch_add(1,std::memory_order_relaxed);
                else not_ready_.fetch_add(1,std::memory_order_relaxed);
            }
            if(r0) r0->Release();
            if(r1) r1->Release();
        }
        if(bound) {
            tl_saved_t0_=v[0];v[0]=nullptr;tl_saved_set_=true; // keeps the engine's reference until restore()
            ID3D11ShaderResourceView* p=tl_held_.Get();c->VSSetShaderResources(0,1,&p);binds_.fetch_add(1,std::memory_order_relaxed);
        }
        for(auto* x:v) if(x) x->Release();
        return bound;
    }
    static void restore(ID3D11DeviceContext* c) noexcept {
        if(!tl_saved_set_) return;
        if(c) c->VSSetShaderResources(0,1,&tl_saved_t0_);
        if(tl_saved_t0_) tl_saved_t0_->Release();
        tl_saved_t0_=nullptr;tl_saved_set_=false;tl_held_.Reset();
    }
    static void begin_epoch() noexcept {std::lock_guard<std::mutex> lock(mutex_);for(auto& e:entries_) {e.run=0;e.last=~0u;}}
    static std::string json() {
        std::lock_guard<std::mutex> lock(mutex_);unsigned models=0;for(const auto& e:entries_) models+=e.model!=0;
        std::ostringstream o;
        o<<"{\"enabled\":"<<(enabled.load()?"true":"false")<<",\"models\":"<<models<<",\"bytes\":"<<bytes_<<",\"overflow\":"<<overflow_.load()<<",\"rotations\":"<<rotations_.load()
         <<",\"restarts\":"<<restarts_.load()<<",\"binds\":"<<binds_.load()<<",\"unknown\":"<<unknown_.load()<<",\"not_ready\":"<<not_ready_.load()<<",\"failures\":"<<failures_.load()<<'}';
        return o.str();
    }
private:
    struct Entry {std::uintptr_t model{};ID3D11Buffer* w0{};ID3D11Buffer* w1{};Microsoft::WRL::ComPtr<ID3D11Buffer> p,a,b;Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
        ID3D11Device* device{};UINT bytes{};std::uint32_t last{~0u},seen{};unsigned run{};};
    inline static std::array<Entry,max_models> entries_{};inline static std::uint64_t bytes_{};inline static std::mutex mutex_;
    inline static std::atomic<std::uint64_t> overflow_{},rotations_{},restarts_{},binds_{},unknown_{},not_ready_{},failures_{};
    inline static thread_local ID3D11ShaderResourceView* tl_saved_t0_{};inline static thread_local bool tl_saved_set_{};
    inline static thread_local Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> tl_held_;
    static void release(Entry& e) noexcept {
        if(e.p) bytes_-=3ull*e.bytes;
        e.p.Reset();e.a.Reset();e.b.Reset();e.view.Reset();e.bytes=0;e.device=nullptr;e.run=0;e.last=~0u;
    }
    static Entry* find_model(std::uintptr_t model) noexcept {for(auto& e:entries_) if(e.model==model) return &e;return nullptr;}
    // ponytail: linear scans of 64 entries under one lock; a hash if the NPC count or draw rate makes it show in the CPU timings
    static Entry* find_buffer(ID3D11Resource* r) noexcept {
        for(auto& e:entries_) if(e.model && (static_cast<ID3D11Resource*>(e.w0)==r || static_cast<ID3D11Resource*>(e.w1)==r)) return &e;
        return nullptr;
    }
    static Entry* claim(std::uintptr_t model,std::uint32_t frame) noexcept {
        for(auto& e:entries_) if(!e.model || frame-e.seen>stale_frames) {release(e);e.model=model;e.w0=e.w1=nullptr;e.seen=frame;return &e;}
        return nullptr;
    }
};
} // namespace d2vr
