#pragma once
#include <windows.h>
#include <d3d11.h>
#include <MinHook.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <vector>
#include <wrl/client.h>
#include "bc7_encode.hpp"
#include "controller_glyph_atlas.hpp"

namespace d2vr {
// Quest labels in button prompts for glyph textures the game created before the mod loaded
// (lib_controller_buttons is loaded at boot, so a CreateTexture2D hook never sees it).
// PSSetShaderResources is watched for small one-mip BC7 views of a known glyph size; each
// new one is read back once on the Present thread, identified by the pack signature
// (controller_glyph_atlas.hpp) and, if it is an Xbox glyph, every later bind of that view
// gets a same-format texture painted with the Quest control label instead.
class GlyphSwap {
    friend struct GlyphSwapAsyncCheck;
    template<class T> using Ptr=Microsoft::WRL::ComPtr<T>;
    struct Entry {Ptr<ID3D11ShaderResourceView> source,replacement,wheel,talk;bool pending{};};
    struct Job {Ptr<ID3D11ShaderResourceView> view;Ptr<ID3D11Texture2D> staging;D3D11_TEXTURE2D_DESC desc{};};
    using SetFn=void (STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT,ID3D11ShaderResourceView* const*);
    inline static std::array<SetFn,2> original_{};inline static std::array<void*,2> target_{};
    inline static std::shared_mutex map_mutex_;
    inline static std::unordered_map<ID3D11ShaderResourceView*,Entry> map_; // Empty entry: not a glyph
    inline static std::mutex pending_mutex_;inline static std::array<Ptr<ID3D11ShaderResourceView>,8> pending_{};inline static unsigned pending_count_{};
    inline static std::mutex process_mutex_;static std::array<Job,8> jobs_; // defined after the class: clang-cl needs Job's member initializers complete
    inline static Ptr<IUnknown> device_identity_,context_identity_;
    inline static Ptr<ID3D11ShaderResourceView> transparent_; // 4x4 all-zero BC7 = fully transparent
    static void reset_owned() {
        enabled=false;
        std::unordered_map<ID3D11ShaderResourceView*,Entry> old_map;
        std::array<Ptr<ID3D11ShaderResourceView>,8> old_pending;
        {std::scoped_lock lock(map_mutex_,pending_mutex_);old_map.swap(map_);old_pending.swap(pending_);pending_count_=0;}
        jobs_={};device_identity_.Reset();context_identity_.Reset();transparent_.Reset();pending_readbacks=0;
        // COM releases happen after mailbox/map locks: native callbacks may bind views.
    }
    static bool adopt_owner(ID3D11Device* device) {
        Ptr<IUnknown> identity;Ptr<ID3D11DeviceContext> context;Ptr<IUnknown> context_identity;
        if(!device || FAILED(device->QueryInterface(IID_PPV_ARGS(&identity)))) return false;
        device->GetImmediateContext(&context);
        if(!context || FAILED(context.As(&context_identity))) return false;
        if(device_identity_.Get()!=identity.Get() || context_identity_.Get()!=context_identity.Get()) reset_owned();
        device_identity_=identity;context_identity_=context_identity;return true;
    }
    static bool glyph_size(UINT w,UINT h) noexcept {
        for(const auto& g:kXboxGlyphs) if(g.width==w && g.height==h) return true;
        return false;
    }
    static Ptr<ID3D11ShaderResourceView> lookup(ID3D11ShaderResourceView* view) {
        {
            std::shared_lock<std::shared_mutex> lock(map_mutex_);
            const auto it=map_.find(view);
            if(it!=map_.end()) {
                if(native_labels.load(std::memory_order_relaxed)) return {}; // menus/notes: buttons are not remapped, keep the game's glyph
                if(it->second.pending) return transparent_; // being read back (1-2 frames): no raw Xbox glyph flashing in first
                if(wheel_open.load(std::memory_order_relaxed))return it->second.wheel?it->second.wheel:it->second.replacement;
                const auto talk_context=talk_prompt_context.load(std::memory_order_acquire);
                if(talk_context && talk_context() && it->second.talk)return it->second.talk;
                return it->second.replacement;
            }
        }
        bool candidate=false;
        D3D11_SHADER_RESOURCE_VIEW_DESC d{};view->GetDesc(&d);
        if(d.ViewDimension==D3D11_SRV_DIMENSION_TEXTURE2D && (d.Format==DXGI_FORMAT_BC7_UNORM || d.Format==DXGI_FORMAT_BC7_UNORM_SRGB)) {
            ID3D11Resource* r{};view->GetResource(&r);ID3D11Texture2D* t{};
            if(r && SUCCEEDED(r->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&t)))) {
                D3D11_TEXTURE2D_DESC td{};t->GetDesc(&td);t->Release();
                candidate=td.MipLevels==1 && td.ArraySize==1 && glyph_size(td.Width,td.Height);
            }
            if(r) r->Release();
        }
        Ptr<ID3D11ShaderResourceView> owned;if(candidate) owned=view;
        {std::scoped_lock lock(map_mutex_,pending_mutex_);
            if(!enabled.load() || map_.find(view)!=map_.end()) return {};
            if(candidate && pending_count_==pending_.size()) return {}; // Retry the bind after capacity opens.
            if(candidate) {map_.emplace(view,Entry{{},{},{},{},true});pending_[pending_count_++]=std::move(owned);++pending_readbacks;}
            else map_.emplace(view,Entry{});
        }
        return {};
    }
    template<int I> static void STDMETHODCALLTYPE hook(ID3D11DeviceContext* c,UINT start,UINT count,ID3D11ShaderResourceView* const* views) {
        if(const auto o=observer.load(std::memory_order_relaxed)) o(c,start,count,views);
        if(enabled.load(std::memory_order_relaxed) && views && count && count<=16) {
            std::array<ID3D11ShaderResourceView*,16> local{};std::array<Ptr<ID3D11ShaderResourceView>,16> owned{};bool changed=false;
            for(UINT i=0;i<count;++i) {
                local[i]=views[i];
                if(views[i]) {owned[i]=lookup(views[i]);if(owned[i]) {local[i]=owned[i].Get();changed=true;}}
            }
            if(changed) {++swaps;original_[I](c,start,count,local.data());return;}
        }
        original_[I](c,start,count,views);
    }
    static void hook_table(ID3D11DeviceContext* context,int i,void* replacement) noexcept {
        void* target=(*reinterpret_cast<void***>(context))[8]; // PSSetShaderResources
        if(target_[0]==target || target_[1]==target) return;
        if(MH_CreateHook(target,replacement,reinterpret_cast<void**>(&original_[static_cast<std::size_t>(i)]))!=MH_OK) return;
        if(MH_EnableHook(target)!=MH_OK) {MH_RemoveHook(target);return;}
        target_[static_cast<std::size_t>(i)]=target;
    }
public:
    inline static std::atomic<bool> enabled{false};
    inline static std::atomic<bool> native_labels{false}; // outside gameplay and the wheel: A/B/X/Y act as themselves
    inline static std::atomic<bool> wheel_open{false}; // Quick Access Wheel held: bind wheel_glyph_label art
    using TalkPromptContext=bool (*)() noexcept;
    inline static std::atomic<TalkPromptContext> talk_prompt_context{};
    // Optional observer of every PS view bind (FSR upscale bookkeeping); called before the swap.
    inline static std::atomic<void (*)(ID3D11DeviceContext*,UINT,UINT,ID3D11ShaderResourceView* const*)> observer{};
    inline static std::atomic<std::uint64_t> swaps{},identified{},checked{},deferred{},failed{},completed{};
    inline static std::atomic<unsigned> pending_readbacks{};
    inline static std::atomic<unsigned> identified_mask{}; // bit i = kXboxGlyphs[i]
    static void install(ID3D11Device* device) noexcept {
        std::lock_guard<std::mutex> process(process_mutex_);
        if(!adopt_owner(device))return;
        if(device && !transparent_) {
            D3D11_TEXTURE2D_DESC d{};d.Width=d.Height=4;d.MipLevels=d.ArraySize=1;d.Format=DXGI_FORMAT_BC7_UNORM;d.SampleDesc.Count=1;
            d.Usage=D3D11_USAGE_IMMUTABLE;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;const std::array<std::uint8_t,16> block{};
            D3D11_SUBRESOURCE_DATA init{block.data(),16,0};Ptr<ID3D11Texture2D> t;
            if(SUCCEEDED(device->CreateTexture2D(&d,&init,&t))) device->CreateShaderResourceView(t.Get(),nullptr,&transparent_);
        }
        if(target_[0] || target_[1]) {for(auto* t:target_) if(t) MH_EnableHook(t);enabled=true;return;}
        if(!device) return;
        ID3D11DeviceContext* deferred_context{};
        if(SUCCEEDED(device->CreateDeferredContext(0,&deferred_context))) {hook_table(deferred_context,0,reinterpret_cast<void*>(&hook<0>));deferred_context->Release();}
        ID3D11DeviceContext* immediate{};device->GetImmediateContext(&immediate);
        if(immediate) {hook_table(immediate,1,reinterpret_cast<void*>(&hook<1>));immediate->Release();}
        enabled=target_[0] || target_[1];
    }
    static void disable() noexcept {enabled=false;for(auto* t:target_) if(t) MH_DisableHook(t);std::lock_guard<std::mutex> process(process_mutex_);reset_owned();}
    // Immediate Present context: copy once, then retry a busy read on a later frame.
    static void process(ID3D11DeviceContext* c) noexcept {
        if(!c || !enabled.load() || c->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)return;
        std::unique_lock<std::mutex> process(process_mutex_,std::try_to_lock);if(!process.owns_lock())return;
        Ptr<ID3D11Device> device;Ptr<IUnknown> identity,context_identity;c->GetDevice(&device);
        if(!device || FAILED(device.As(&identity)) || FAILED(c->QueryInterface(IID_PPV_ARGS(&context_identity))) ||
           identity.Get()!=device_identity_.Get() || context_identity.Get()!=context_identity_.Get()) {
            ++failed;reset_owned();return;
        }
        {std::unique_lock<std::shared_mutex> lock(map_mutex_);
            if(map_.size()>16384) for(auto it=map_.begin();it!=map_.end();) {
                const auto& entry=it->second;
                it=entry.pending || entry.replacement || entry.wheel || entry.talk || entry.source?std::next(it):map_.erase(it);
            }
        }
        for(auto& job:jobs_) {
            if(!job.view) {
                std::lock_guard<std::mutex> lock(pending_mutex_);
                if(pending_count_) {job.view=std::move(pending_[--pending_count_]);}
            }
            if(!job.view)continue;
            // Creating/copying resources is outside map/mailbox locks.
            if(!job.staging) {
                Ptr<ID3D11Resource> resource;Ptr<ID3D11Texture2D> texture;Ptr<ID3D11Device> source_device;Ptr<IUnknown> source_identity;
                job.view->GetResource(&resource);
                bool ready=resource && SUCCEEDED(resource.As(&texture));
                if(ready) {texture->GetDevice(&source_device);ready=source_device && SUCCEEDED(source_device.As(&source_identity)) && source_identity.Get()==identity.Get();}
                if(ready) {
                    texture->GetDesc(&job.desc);auto d=job.desc;d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;d.MiscFlags=0;
                    ready=SUCCEEDED(device->CreateTexture2D(&d,nullptr,&job.staging));
                }
                if(ready) {c->CopyResource(job.staging.Get(),texture.Get());continue;}
                ++failed;
            } else {
                D3D11_MAPPED_SUBRESOURCE mapped{};
                const auto result=c->Map(job.staging.Get(),0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,&mapped);
                if(result==DXGI_ERROR_WAS_STILL_DRAWING) {++deferred;continue;}
                if(SUCCEEDED(result)) {
                    Ptr<ID3D11ShaderResourceView> replacement,wheel,talk;bool completion_failed=false;
                    const UINT row=((job.desc.Width+3)/4)*16,bh=(job.desc.Height+3)/4;
                    try {
                        if(!mapped.pData || mapped.RowPitch<row)throw std::runtime_error("Invalid glyph readback row");
                        std::vector<std::uint8_t> blocks(static_cast<std::size_t>(row)*bh);
                        for(UINT y=0;y<bh;++y)std::memcpy(blocks.data()+static_cast<std::size_t>(y)*row,static_cast<const std::uint8_t*>(mapped.pData)+static_cast<std::size_t>(y)*mapped.RowPitch,row);
                        ++checked;
                        if(const auto glyph=find_xbox_glyph(job.desc.Width,job.desc.Height,blocks.data(),blocks.size())) {
                            std::vector<std::uint8_t> rgba(static_cast<std::size_t>(job.desc.Width)*job.desc.Height*4);
                            paint_quest_glyphs(rgba.data(),job.desc.Width,job.desc.Height,glyph->label);
                            const auto encoded=encode_bc7_mode6(rgba.data(),job.desc.Width,job.desc.Height);
                            auto d=job.desc;d.Usage=D3D11_USAGE_IMMUTABLE;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;d.CPUAccessFlags=0;d.MiscFlags=0;
                            D3D11_SUBRESOURCE_DATA init{encoded.data(),row,0};Ptr<ID3D11Texture2D> painted;
                            if(FAILED(device->CreateTexture2D(&d,&init,&painted)))throw std::runtime_error("Cannot create glyph replacement");
                            D3D11_SHADER_RESOURCE_VIEW_DESC sd{};job.view->GetDesc(&sd);
                            if(FAILED(device->CreateShaderResourceView(painted.Get(),&sd,&replacement)))throw std::runtime_error("Cannot create glyph replacement view");
                            if(const auto label=wheel_glyph_label(*glyph)) {
                                paint_quest_glyphs(rgba.data(),job.desc.Width,job.desc.Height,label);
                                const auto wheel_blocks=encode_bc7_mode6(rgba.data(),job.desc.Width,job.desc.Height);
                                D3D11_SUBRESOURCE_DATA wheel_init{wheel_blocks.data(),row,0};Ptr<ID3D11Texture2D> wheel_painted;
                                if(FAILED(device->CreateTexture2D(&d,&wheel_init,&wheel_painted)) ||
                                   FAILED(device->CreateShaderResourceView(wheel_painted.Get(),&sd,&wheel)))throw std::runtime_error("Cannot create wheel glyph");
                            }
                            if(std::strcmp(glyph->name,"X_XBOX")==0 || std::strcmp(glyph->name,"X_XBOX_s")==0) {
                                paint_quest_glyphs(rgba.data(),job.desc.Width,job.desc.Height,"Y");
                                const auto talk_blocks=encode_bc7_mode6(rgba.data(),job.desc.Width,job.desc.Height);
                                D3D11_SUBRESOURCE_DATA talk_init{talk_blocks.data(),row,0};Ptr<ID3D11Texture2D> talk_painted;
                                if(SUCCEEDED(device->CreateTexture2D(&d,&talk_init,&talk_painted)))
                                    device->CreateShaderResourceView(talk_painted.Get(),&sd,&talk);
                            }
                            identified_mask|=1u<<static_cast<unsigned>(glyph-kXboxGlyphs);++identified;
                        }
                    } catch(...) {completion_failed=true;++failed;}
                    c->Unmap(job.staging.Get(),0);
                    {std::unique_lock<std::shared_mutex> lock(map_mutex_);
                        if(completion_failed) map_.erase(job.view.Get());
                        else {auto& entry=map_[job.view.Get()];entry.pending=false;entry.replacement=std::move(replacement);entry.wheel=std::move(wheel);entry.talk=std::move(talk);
                            if(entry.replacement)entry.source=job.view;}

                    }
                    ++completed;--pending_readbacks;job={};continue;
                }
                ++failed;
            }
            {std::unique_lock<std::shared_mutex> lock(map_mutex_);map_.erase(job.view.Get());} // Errors remain retryable.
            --pending_readbacks;job={};
        }
    }
};
inline std::array<GlyphSwap::Job,8> GlyphSwap::jobs_{};
} // namespace d2vr
