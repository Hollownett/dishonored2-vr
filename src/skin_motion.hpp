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
// Bound: max_models entries and max_bytes of ring buffers; a model beyond either is counted (overflow) and keeps the engine's palette.
// v1185 (sim v1183: 64 models were full, 1.3M draws overflowed, 230-260 KB used): 2048 models / 32 MB, hashed lookups; when full, the entry
// drawn (or copied) least recently, and not in the last 2 engine frames, is evicted. distinct = models ever seen; live = models now.
// The same proven/own-frame gate as the rig ring (rig_mask.hpp rig_ring_proven, RigMask::frame_fn).
#include <d3d11.h>
#include <wrl/client.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include "rig_mask.hpp" // rig_ring_run / rig_ring_valid

namespace d2vr {
class SkinMotion {
public:
    inline static std::atomic<bool> enabled{false};
    static constexpr unsigned max_models=2048;static constexpr std::uint64_t max_bytes=32ull<<20;
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
        if(e->w0!=w0 || e->w1!=w1) {by_buffer_.erase(reinterpret_cast<std::uintptr_t>(e->w0));by_buffer_.erase(reinterpret_cast<std::uintptr_t>(e->w1));
            e->w0=w0;e->w1=w1;const auto i=static_cast<unsigned>(e-entries_.data());by_buffer_[reinterpret_cast<std::uintptr_t>(w0)]=i;by_buffer_[reinterpret_cast<std::uintptr_t>(w1)]=i;}
        e->seen=frame;if(e->used+2u<frame || e->used>frame) e->used=frame;
        if(!e->p || e->bytes!=d.ByteWidth || e->device!=owner) {
            release(*e);
            if(bytes_+3ull*d.ByteWidth>max_bytes) {overflow_.fetch_add(1,std::memory_order_relaxed);drop(*e);return;}
            d.Usage=D3D11_USAGE_DEFAULT;d.CPUAccessFlags=0;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
            if(!owner || FAILED(owner->CreateBuffer(&d,nullptr,&e->p)) || FAILED(owner->CreateBuffer(&d,nullptr,&e->a)) || FAILED(owner->CreateBuffer(&d,nullptr,&e->b))) {
                drop(*e);failures_.fetch_add(1,std::memory_order_relaxed);return;
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
                const auto fn=RigMask::frame_fn.load(std::memory_order_relaxed);const std::uint32_t now=fn?fn():e?e->last:0;
                if(e) e->used=now;
                if(e && (r0==e->w0 || r0==e->w1) && e->run>=rig_ring_proven && e->last==now && e->p) {
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
        o<<"{\"enabled\":"<<(enabled.load()?"true":"false")<<",\"models\":"<<models<<",\"distinct\":"<<seen_models_.size()<<",\"bytes\":"<<bytes_<<",\"overflow\":"<<overflow_.load()<<",\"evictions\":"<<evictions_.load()<<",\"rotations\":"<<rotations_.load()
         <<",\"restarts\":"<<restarts_.load()<<",\"binds\":"<<binds_.load()<<",\"unknown\":"<<unknown_.load()<<",\"not_ready\":"<<not_ready_.load()<<",\"failures\":"<<failures_.load()<<'}';
        return o.str();
    }
private:
    struct Entry {std::uintptr_t model{};ID3D11Buffer* w0{};ID3D11Buffer* w1{};Microsoft::WRL::ComPtr<ID3D11Buffer> p,a,b;Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
        ID3D11Device* device{};UINT bytes{};std::uint32_t last{~0u},seen{},used{};unsigned run{};};
    inline static std::array<Entry,max_models> entries_{};inline static std::uint64_t bytes_{};inline static std::mutex mutex_;
    inline static std::atomic<std::uint64_t> evictions_{},overflow_{},rotations_{},restarts_{},binds_{},unknown_{},not_ready_{},failures_{};
    inline static thread_local ID3D11ShaderResourceView* tl_saved_t0_{};inline static thread_local bool tl_saved_set_{};
    inline static thread_local Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> tl_held_;
    static void release(Entry& e) noexcept {
        if(e.p) bytes_-=3ull*e.bytes;
        e.p.Reset();e.a.Reset();e.b.Reset();e.view.Reset();e.bytes=0;e.device=nullptr;e.run=0;e.last=~0u;
    }
    inline static std::unordered_map<std::uintptr_t,unsigned> by_model_,by_buffer_;inline static std::unordered_set<std::uintptr_t> seen_models_;
    static Entry* find_model(std::uintptr_t model) {const auto it=by_model_.find(model);return it==by_model_.end()?nullptr:&entries_[it->second];}
    static Entry* find_buffer(ID3D11Resource* r) {const auto it=by_buffer_.find(reinterpret_cast<std::uintptr_t>(r));return it==by_buffer_.end()?nullptr:&entries_[it->second];}
    static void drop(Entry& e) {
        release(e);by_model_.erase(e.model);by_buffer_.erase(reinterpret_cast<std::uintptr_t>(e.w0));by_buffer_.erase(reinterpret_cast<std::uintptr_t>(e.w1));
        e.model=0;e.w0=e.w1=nullptr;
    }
    // A free entry, else the least recently drawn one not used in the last 2 engine frames (evicted), else none.
    static Entry* claim(std::uintptr_t model,std::uint32_t frame) {
        Entry* pick=nullptr;
        for(auto& e:entries_) {
            if(!e.model) {pick=&e;break;}
            if(e.used+2u<frame && (!pick || e.used<pick->used)) pick=&e;
        }
        if(!pick) return nullptr;
        if(pick->model) {drop(*pick);evictions_.fetch_add(1,std::memory_order_relaxed);}
        pick->model=model;pick->seen=pick->used=frame;by_model_[model]=static_cast<unsigned>(pick-entries_.data());seen_models_.insert(model);
        return pick;
    }
};
} // namespace d2vr
