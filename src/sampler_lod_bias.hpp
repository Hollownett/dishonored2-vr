#pragma once
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <MinHook.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

namespace d2vr {
// Texture mip LOD bias for upscaled rendering. When FSR renders below output resolution, material
// textures are still sampled at render-resolution mips, so the upscaled image keeps render-resolution
// texture detail ("looks like plain lower resolution", headset v307). Upscalers expect the game to
// bias texture sampling by log2(render/output) (AMD FSR docs add -1; Luma's D2 mod does the same).
// D2 builds its samplers at load (image_lodbias 0x32ea470 is read there, a live change has no effect), so
// PSSetSamplers is hooked instead: mip-filtered samplers are swapped for biased clones while bias()!=0.
// Shadow (comparison), point and mip-less samplers are left alone. Originals are kept referenced so a
// freed sampler's address can never be reused by an unrelated sampler while it is in the map.
class SamplerLodBias {
    template<class T> using Ptr=Microsoft::WRL::ComPtr<T>;
    using SetSamplers=void (STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT,ID3D11SamplerState* const*);
    // v993: the PS state census observes the FINAL samplers this hook forwards (after a biased-clone substitution) and may ask
    // to elide the call. It runs even with a zero bias, so the hook is a plain passthrough then.
    using Filter=bool (*)(ID3D11DeviceContext*,UINT,UINT,ID3D11SamplerState* const*,bool) noexcept;
    struct Entry {Ptr<ID3D11SamplerState> original,clone;float bias{};bool eligible{};};
    inline static std::shared_mutex mutex_;
    inline static std::unordered_map<ID3D11SamplerState*,Entry> map_;
    inline static std::vector<Ptr<ID3D11SamplerState>> retired_; // clones replaced by a bias change: another thread may be binding one
    inline static std::array<SetSamplers,2> original_{};
    inline static std::array<void*,2> target_{};
    inline static std::atomic<ID3D11DeviceContext*> immediate_context_{nullptr};
    static bool eligible(const D3D11_SAMPLER_DESC& d) noexcept {
        if(d.Filter&0x80 || d.Filter&0x100) return false;   // comparison / minimum-maximum reduction
        const bool mip_linear=(d.Filter&0x1)!=0;           // D3D11 filter bit 0: linear mip interpolation
        return (d.Filter==D3D11_FILTER_ANISOTROPIC || mip_linear) && d.MaxLOD>d.MinLOD;
    }
    // Returns the sampler to bind for `s` under bias `b` (s itself when unchanged).
    static ID3D11SamplerState* lookup(ID3D11SamplerState* s,float b) {
        {
            std::shared_lock<std::shared_mutex> read(mutex_);
            const auto it=map_.find(s);
            if(it!=map_.end() && (!it->second.eligible || it->second.bias==b)) return it->second.eligible?it->second.clone.Get():s;
        }
        D3D11_SAMPLER_DESC d{};s->GetDesc(&d);
        Entry e;e.original=s;e.eligible=eligible(d);e.bias=b;
        if(e.eligible) {
            Ptr<ID3D11Device> device;s->GetDevice(&device);
            d.MipLODBias=std::clamp(d.MipLODBias+b,-16.f,15.99f);
            if(!device || FAILED(device->CreateSamplerState(&d,&e.clone))) {++failures;e.eligible=false;e.clone.Reset();}
        }
        std::unique_lock<std::shared_mutex> write(mutex_);
        auto& slot=map_[s];if(slot.clone) retired_.push_back(slot.clone);slot=std::move(e);
        return slot.eligible?slot.clone.Get():s;
    }
    template<int I> static void STDMETHODCALLTYPE hook(ID3D11DeviceContext* c,UINT start,UINT count,ID3D11SamplerState* const* samplers) {
        const float b=bias_.load(std::memory_order_relaxed);
        ID3D11SamplerState* const* forwarded=samplers;
        // v1040: bias 0 (feature off or not upscaling) forwards untouched; the local array exists only on the biased path.
        std::array<ID3D11SamplerState*,D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT> local;
        if(b!=0 && samplers && count && count<=D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT) {
            try {
                bool changed=false;
                for(UINT i=0;i<count;++i) {local[i]=samplers[i];if(samplers[i]) {local[i]=resolve(samplers[i]);changed|=local[i]!=samplers[i];}}
                if(changed) {++swaps;forwarded=local.data();}
            } catch(...) {++failures;}
        }
        // v993: the census sits inside the one owner of slot 10 and sees exactly what is forwarded. Classify by the one
        // immediate context, not by which vtable this hook was installed from.
        const bool is_deferred=c!=immediate_context_.load(std::memory_order_relaxed);
        if(const auto f=filter.load(std::memory_order_relaxed)) if(f(c,start,count,forwarded,is_deferred)) return;
        original_[I](c,start,count,forwarded);
    }
    static void hook_table(ID3D11DeviceContext* context,int i,void* replacement) noexcept {
        void* target=(*reinterpret_cast<void***>(context))[10]; // PSSetSamplers
        if(target_[0]==target || target_[1]==target) return;
        if(MH_CreateHook(target,replacement,reinterpret_cast<void**>(&original_[static_cast<std::size_t>(i)]))!=MH_OK) return;
        if(MH_EnableHook(target)!=MH_OK) {MH_RemoveHook(target);return;}
        target_[static_cast<std::size_t>(i)]=target;
    }
    inline static std::atomic<float> bias_{0};
    inline static std::atomic<bool> installed_{false};
public:
    inline static std::atomic<std::uint64_t> swaps{},failures{};
    inline static std::atomic<Filter> filter{}; // v993 PS state census/elision on the final forwarded samplers
    // Bias for a render fraction (render/output, per axis): log2(fraction) + extra, also at native size (AMD's rule for
    // temporal AA: native AA gets extra alone; v873, the user found distant detail soft with every native mode).
    static float bias_for(float fraction,float extra) noexcept {
        if(!(fraction>0)) return 0;
        return std::log2(fraction<1.f?fraction:1.f)+extra;
    }
    static void set_bias(float b) noexcept {bias_.store(std::isfinite(b)?b:0.f,std::memory_order_relaxed);}
    static float bias() noexcept {return bias_.load(std::memory_order_relaxed);}
    // The sampler the hook binds in place of `s` under the current bias (s itself when unchanged).
    static ID3D11SamplerState* resolve(ID3D11SamplerState* s) {const float b=bias();return b!=0 && s?lookup(s,b):s;}
    // Render thread; MinHook must be initialised.
    static void install(ID3D11Device* device) noexcept {
        if(installed_.load(std::memory_order_relaxed)) return;
        if(target_[0] || target_[1]) {
            if(device) {ID3D11DeviceContext* immediate{};device->GetImmediateContext(&immediate);
                if(immediate) {immediate_context_.store(immediate,std::memory_order_relaxed);immediate->Release();}}
            for(auto* t:target_) if(t) MH_EnableHook(t);installed_=true;return;
        }
        if(!device) return;
        ID3D11DeviceContext* immediate{};device->GetImmediateContext(&immediate);
        if(immediate) immediate_context_.store(immediate,std::memory_order_relaxed);
        ID3D11DeviceContext* deferred{};
        if(SUCCEEDED(device->CreateDeferredContext(0,&deferred))) {hook_table(deferred,0,reinterpret_cast<void*>(&hook<0>));deferred->Release();}
        if(immediate) {
            hook_table(immediate,1,reinterpret_cast<void*>(&hook<1>));
            immediate->Release();
        }
        installed_=target_[0] || target_[1];
    }
    static void disable() noexcept {
        set_bias(0);installed_=false;for(auto* t:target_) if(t) MH_DisableHook(t);
        std::unique_lock<std::shared_mutex> write(mutex_);map_.clear();retired_.clear();
    }
    static std::size_t cached() {std::shared_lock<std::shared_mutex> read(mutex_);return map_.size();}
};
} // namespace d2vr
