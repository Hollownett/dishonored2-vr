#pragma once
// v1453 (SteamVR spacewarp; docs/research/STEAMVR-SAFE-SUBMIT.md sections 11, 14-16; knob build/xr-own-device, ON by default since v1457, "off" turns it off): the OpenXR
// session on the mod's own D3D11 device, on the game's adapter, so the runtime's frame calls never touch the game's device. Then spacewarp's
// waiting thread can wait for and send each generated frame in its slot on any runtime, as it does on Virtual Desktop (v1420), and the
// v1421/v1444 lock cycles (a runtime call on the game's immediate context beside the engine's own) cannot form.
//
// The game's render thread keeps drawing every XR image on the game's device, as before: GameXr gets, for each swapchain image, a "mirror"
// texture on the game's device (same description, D3D11_RESOURCE_MISC_SHARED), opened on the own device. Order across the two devices:
//   - release (the image's content is complete on the game device): the game device signals fence A and flushes; the own device waits for
//     A (on the GPU), copies the mirror into the runtime's image, signals fence B and flushes; then the runtime's xrReleaseSwapchainImage;
//   - wait (before the game device draws a mirror again): the game device waits for B up to that image's last copy (on the GPU), so a copy
//     still queued can never read a half-drawn mirror.
// The own device is multithread-protected: the runtime may use it from spacewarp's thread while the render thread queues copies (GameXr
// keeps their frame calls apart anyway, v1421). Nothing else ever uses it: it presents nothing and the probe hooks only the game's context.
//
// Indices follow OpenXR's rules: xrAcquireSwapchainImage hands out indices in order, xrWaitSwapchainImage waits for the oldest acquired one
// and xrReleaseSwapchainImage releases the oldest waited one, so a queue per swapchain is enough.
#include <windows.h>
#include <d3d11_4.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <map>
#include <string>
#include <vector>

namespace d2vr {
// v1457: a status text as the contents of a JSON string (quotes, backslashes and control characters escaped). The own device's why and
// blocked texts quote file names and driver messages; unescaped, one quote made xr-live-status.json unreadable (sim 2026-11-21).
inline std::string json_text(const std::string& text) {
    std::string out;out.reserve(text.size()+8);
    for(const char c:text) {
        if(c=='"' || c=='\\') {out+='\\';out+=c;}
        else if(static_cast<unsigned char>(c)<0x20) {char code[8];std::snprintf(code,sizeof code,"\\u%04x",static_cast<unsigned>(static_cast<unsigned char>(c)));out+=code;}
        else out+=c;
    }
    return out;
}
class XrOwnDevice {
    template<class T> using Com=Microsoft::WRL::ComPtr<T>;
public:
    // Creates the own device on `game`'s adapter (the game's feature level, BGRA support, multithread protection), both shared fences, and
    // proves one mirror round trip (a shared texture opened on the own device, a copy between the fences). false: why() says what failed and
    // nothing was kept (GameXr then binds the game's device, as before).
    bool create(ID3D11Device* game) noexcept {
        try {
            reset();
            if(!game) return fail("no game device");
            Com<IDXGIDevice> dxgi;Com<IDXGIAdapter> adapter;
            if(FAILED(game->QueryInterface(IID_PPV_ARGS(&dxgi))) || FAILED(dxgi->GetAdapter(&adapter))) return fail("no game adapter");
            game_=game;game_->GetImmediateContext(&game_ctx_);
            if(FAILED(game_.As(&game5_)) || FAILED(game_ctx_.As(&game_ctx4_))) return fail("game device has no D3D11.4 fences");
            const D3D_FEATURE_LEVEL level=game->GetFeatureLevel();
            // Its work is a few copies and the runtime's calls: no driver worker threads for it (the v1421 hang was a lock cycle through the
            // driver's threaded optimisation)
            UINT flags=D3D11_CREATE_DEVICE_BGRA_SUPPORT|D3D11_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS;
            if(FAILED(D3D11CreateDevice(adapter.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,flags,&level,1,D3D11_SDK_VERSION,&own_,nullptr,&own_ctx_)))
                return fail("cannot create the own device");
            if(FAILED(own_.As(&own5_)) || FAILED(own_ctx_.As(&own_ctx4_))) return fail("own device has no D3D11.4 fences");
            Com<ID3D11Multithread> protection;
            if(FAILED(own_ctx_.As(&protection)) || !protection) return fail("own device has no multithread protection");
            protection->SetMultithreadProtected(TRUE);
            if(!shared_fence(game5_.Get(),own5_.Get(),game_fence_,game_fence_on_own_)) return fail("cannot share the game device's fence");
            if(!shared_fence(own5_.Get(),game5_.Get(),own_fence_,own_fence_on_game_)) return fail("cannot share the own device's fence");
            if(!self_test()) return fail("mirror round trip failed");
            why_.clear();return true;
        } catch(...) {return fail("exception");}
    }
    explicit operator bool() const noexcept {return static_cast<bool>(own_);}
    ID3D11Device* device() const noexcept {return own_.Get();}
    const std::string& why() const noexcept {return why_;}
    // The last image that could not be mirrored: its description and the failing call (for the status; empty when none failed)
    const std::string& mirror_failure() const noexcept {return mirror_failure_;}

    // After xrEnumerateSwapchainImages filled `textures` (count runtime images on the own device): each entry becomes the image's mirror
    // on the game device. Enumerating the same swapchain again hands out the same mirrors. false: nothing replaced (the caller fails).
    bool mirror(std::uintptr_t chain,ID3D11Texture2D** textures,std::uint32_t count) noexcept {
        try {
            auto& c=chains_[chain];
            const bool same=c.images.size()==count && [&] {for(std::uint32_t i=0;i<count;++i) if(c.images[i].runtime.Get()!=textures[i] && c.images[i].game.Get()!=textures[i]) return false;return true;}();
            if(!same) {
                std::vector<Image> made(count);
                for(std::uint32_t i=0;i<count;++i) if(!make_mirror(textures[i],made[i])) {chains_.erase(chain);++failures_;chain_count_.store(chains_.size(),std::memory_order_relaxed);return false;}
                c.images=std::move(made);c.acquired.clear();c.waited=0;
            }
            for(std::uint32_t i=0;i<count;++i) textures[i]=c.images[i].game.Get();
            ++mirrored_chains_;chain_count_.store(chains_.size(),std::memory_order_relaxed);return true;
        } catch(...) {chains_.erase(chain);++failures_;chain_count_.store(chains_.size(),std::memory_order_relaxed);return false;}
    }
    void acquired(std::uintptr_t chain,std::uint32_t index) noexcept {
        try {const auto at=chains_.find(chain);if(at!=chains_.end() && index<at->second.images.size()) at->second.acquired.push_back(index);} catch(...) {}
    }
    // The oldest acquired image is now the app's: the game device's next draws into its mirror wait for that mirror's last copy.
    void waited(std::uintptr_t chain) noexcept {
        const auto at=chains_.find(chain);if(at==chains_.end()) return;
        Chain& c=at->second;if(c.waited>=c.acquired.size()) return;
        const Image& image=c.images[c.acquired[c.waited++]];
        if(image.last_copy) game_ctx4_->Wait(own_fence_on_game_.Get(),image.last_copy);
    }
    // The oldest waited image is complete on the game device: copy it into the runtime's image (call before xrReleaseSwapchainImage).
    void released(std::uintptr_t chain) noexcept {
        const auto at=chains_.find(chain);if(at==chains_.end()) return;
        Chain& c=at->second;if(!c.waited || c.acquired.empty()) return;
        Image& image=c.images[c.acquired.front()];c.acquired.pop_front();--c.waited;
        copy(image);
    }
    void forget(std::uintptr_t chain) noexcept {try {chains_.erase(chain);chain_count_.store(chains_.size(),std::memory_order_relaxed);} catch(...) {}}
    void reset() noexcept {
        chains_.clear();chain_count_.store(0,std::memory_order_relaxed);game_fence_.Reset();game_fence_on_own_.Reset();own_fence_.Reset();own_fence_on_game_.Reset();
        own_ctx4_.Reset();own5_.Reset();own_ctx_.Reset();own_.Reset();game_ctx4_.Reset();game_ctx_.Reset();game5_.Reset();game_.Reset();
        game_value_=own_value_=0;
    }
    // status (read by the status writer while the render thread works: telemetry only)
    std::uint64_t copies() const noexcept {return copies_.load(std::memory_order_relaxed);}
    std::uint64_t failures() const noexcept {return failures_.load(std::memory_order_relaxed);}
    std::uint64_t mirrored_chains() const noexcept {return mirrored_chains_.load(std::memory_order_relaxed);}
    std::size_t chains() const noexcept {return chain_count_.load(std::memory_order_relaxed);}

private:
    struct Image {Com<ID3D11Texture2D> runtime,game,game_on_own;std::uint64_t last_copy{};};
    struct Chain {std::vector<Image> images;std::deque<std::uint32_t> acquired;std::size_t waited{};};
    bool fail(const char* why) noexcept {try {why_=why;} catch(...) {}reset();return false;}
    static bool shared_fence(ID3D11Device5* owner,ID3D11Device5* other,Com<ID3D11Fence>& fence,Com<ID3D11Fence>& opened) noexcept {
        if(FAILED(owner->CreateFence(0,D3D11_FENCE_FLAG_SHARED,IID_PPV_ARGS(&fence)))) return false;
        HANDLE handle{};
        if(FAILED(fence->CreateSharedHandle(nullptr,GENERIC_ALL,nullptr,&handle)) || !handle) return false;
        const HRESULT hr=other->OpenSharedFence(handle,IID_PPV_ARGS(&opened));
        CloseHandle(handle);
        return SUCCEEDED(hr) && opened;
    }
    // A texture on the game device with the runtime image's description, shared (legacy handle: any 2D single-mip format), opened on the own
    // device.
    bool make_mirror(ID3D11Texture2D* runtime,Image& out) noexcept {
        if(!runtime) return refused("no image",{},S_OK);
        D3D11_TEXTURE2D_DESC desc{};runtime->GetDesc(&desc);
        if(desc.MipLevels!=1 || desc.SampleDesc.Count!=1 || desc.ArraySize!=1) return refused("mipmapped, multisampled or array image",desc,S_OK);
        desc.Usage=D3D11_USAGE_DEFAULT;desc.CPUAccessFlags=0;desc.MiscFlags=D3D11_RESOURCE_MISC_SHARED;
        if(!desc.BindFlags) desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        if(const HRESULT hr=game_->CreateTexture2D(&desc,nullptr,&out.game);FAILED(hr)) return refused("CreateTexture2D",desc,hr);
        Com<IDXGIResource> resource;HANDLE handle{};
        if(const HRESULT hr=out.game.As(&resource);FAILED(hr)) return refused("IDXGIResource",desc,hr);
        if(const HRESULT hr=resource->GetSharedHandle(&handle);FAILED(hr) || !handle) return refused("GetSharedHandle",desc,hr);
        if(const HRESULT hr=own_->OpenSharedResource(handle,IID_PPV_ARGS(&out.game_on_own));FAILED(hr) || !out.game_on_own) return refused("OpenSharedResource",desc,hr);
        out.runtime=runtime;out.last_copy=0;
        return true;
    }
    bool refused(const char* call,const D3D11_TEXTURE2D_DESC& desc,HRESULT hr) noexcept {
        try {
            char text[192];
            std::snprintf(text,sizeof text,"%s 0x%08lx: %ux%u format %u mips %u array %u samples %u bind 0x%x misc 0x%x",call,static_cast<unsigned long>(hr),
                desc.Width,desc.Height,static_cast<unsigned>(desc.Format),desc.MipLevels,desc.ArraySize,desc.SampleDesc.Count,desc.BindFlags,desc.MiscFlags);
            mirror_failure_=text;
        } catch(...) {}
        return false;
    }
    void copy(Image& image) noexcept {
        game_ctx4_->Signal(game_fence_.Get(),++game_value_);
        game_ctx_->Flush();  // the own device's wait can only end once the game device's work and its signal reach the GPU
        own_ctx4_->Wait(game_fence_on_own_.Get(),game_value_);
        own_ctx_->CopyResource(image.runtime.Get(),image.game_on_own.Get());
        own_ctx4_->Signal(own_fence_.Get(),++own_value_);
        own_ctx_->Flush();    // and the game device's next wait for this copy only once the copy reaches the GPU
        image.last_copy=own_value_;copies_.fetch_add(1,std::memory_order_relaxed);
    }
    bool self_test() noexcept {
        D3D11_TEXTURE2D_DESC desc{};desc.Width=desc.Height=16;desc.MipLevels=desc.ArraySize=1;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
        Com<ID3D11Texture2D> runtime;
        if(FAILED(own_->CreateTexture2D(&desc,nullptr,&runtime))) return false;
        Image image;if(!make_mirror(runtime.Get(),image)) return false;
        copy(image);
        return true;
    }
    Com<ID3D11Device> game_;Com<ID3D11Device5> game5_;Com<ID3D11DeviceContext> game_ctx_;Com<ID3D11DeviceContext4> game_ctx4_;
    Com<ID3D11Device> own_;Com<ID3D11Device5> own5_;Com<ID3D11DeviceContext> own_ctx_;Com<ID3D11DeviceContext4> own_ctx4_;
    Com<ID3D11Fence> game_fence_,game_fence_on_own_,own_fence_,own_fence_on_game_;
    std::uint64_t game_value_{},own_value_{};
    std::map<std::uintptr_t,Chain> chains_;
    std::string why_,mirror_failure_;
    std::atomic<std::uint64_t> copies_{},failures_{},mirrored_chains_{};
    std::atomic<std::size_t> chain_count_{};
};
} // namespace d2vr
