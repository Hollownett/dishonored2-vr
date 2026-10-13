#pragma once
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>

namespace d2vr {
// Captures only a caller-verified native movie scope. The caller binds the returned RTV,
// restores the native target after the scope, and consumes pixels after executing its list.
class WorldUiCapture {
    template<class T> using Ptr=Microsoft::WRL::ComPtr<T>;
public:
    static constexpr unsigned movie_count=3,eye_count=2; // wheel, notes, journal
    struct Snapshot {
        Ptr<ID3D11Texture2D> texture;
        DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN; // native RTV transfer function
        UINT width{},height{};
        std::uint64_t epoch{},recorded_tick{},scope_id{};
        explicit operator bool() const noexcept {return texture.Get()!=nullptr;}
    };
    std::atomic<std::uint64_t> redirects{},clears{},recreates{},rejected{},create_failures{};
private:
    struct Surface {
        Snapshot image;
        Ptr<ID3D11RenderTargetView> rtv;
        Ptr<ID3D11Device> device;
        ID3D11DeviceContext* last_context{}; // identity only; never dereferenced
    };
    std::array<std::array<Surface,eye_count>,movie_count> surfaces_{};
    std::mutex mutex_;
    std::uint64_t next_epoch_{};
    static bool rgba(DXGI_FORMAT f) noexcept {
        return f==DXGI_FORMAT_R8G8B8A8_UNORM || f==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    }
public:
    // A unique nonzero scope_id clears once despite repeated binds within the movie draw.
    // Zero is convenient for callers that redirect exactly once per scope.
    Ptr<ID3D11RenderTargetView> redirect(ID3D11DeviceContext* context,ID3D11RenderTargetView* source,
                                       unsigned movie,unsigned eye=0,std::uint64_t scope_id=0) {
        if(!context || !source || movie>=movie_count || eye>=eye_count) {++rejected;return {};}
        D3D11_RENDER_TARGET_VIEW_DESC view{};source->GetDesc(&view);
        if(!rgba(view.Format) || view.ViewDimension!=D3D11_RTV_DIMENSION_TEXTURE2D || view.Texture2D.MipSlice!=0) {++rejected;return {};}
        Ptr<ID3D11Resource> resource;source->GetResource(&resource);
        Ptr<ID3D11Texture2D> texture;
        if(!resource || FAILED(resource.As(&texture))) {++rejected;return {};}
        D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);
        Ptr<ID3D11Device> device,source_device;context->GetDevice(&device);source->GetDevice(&source_device);
        if(!device || device.Get()!=source_device.Get() || !desc.Width || !desc.Height || desc.ArraySize!=1 ||
           desc.SampleDesc.Count!=1 || (desc.Format!=DXGI_FORMAT_R8G8B8A8_TYPELESS && !rgba(desc.Format))) {++rejected;return {};}
        std::lock_guard<std::mutex> lock(mutex_);
        auto& surface=surfaces_[movie][eye];
        bool recreated=false;
        if(!surface.rtv || surface.device.Get()!=device.Get() || surface.image.width!=desc.Width ||
           surface.image.height!=desc.Height || surface.image.format!=view.Format) {
            D3D11_TEXTURE2D_DESC capture{};capture.Width=desc.Width;capture.Height=desc.Height;
            capture.MipLevels=1;capture.ArraySize=1;capture.Format=DXGI_FORMAT_R8G8B8A8_TYPELESS;
            capture.SampleDesc.Count=1;capture.Usage=D3D11_USAGE_DEFAULT;
            capture.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
            Ptr<ID3D11Texture2D> replacement;Ptr<ID3D11RenderTargetView> rtv;
            if(FAILED(device->CreateTexture2D(&capture,nullptr,&replacement)) ||
               FAILED(device->CreateRenderTargetView(replacement.Get(),&view,&rtv))) {++create_failures;return {};}
            surface={};surface.device=device;surface.rtv=rtv;surface.image.texture=replacement;
            surface.image.format=view.Format;surface.image.width=desc.Width;surface.image.height=desc.Height;
            surface.image.epoch=++next_epoch_;recreated=true;++recreates;
        }
        if(recreated || !scope_id || surface.image.scope_id!=scope_id || surface.last_context!=context) {
            static constexpr FLOAT transparent[4]{};
            context->ClearRenderTargetView(surface.rtv.Get(),transparent);
            surface.image.scope_id=scope_id;surface.last_context=context;++clears;
        }
        surface.image.recorded_tick=GetTickCount64();++redirects;
        return surface.rtv;
    }
    Snapshot snapshot(unsigned movie,unsigned eye=0) {
        if(movie>=movie_count || eye>=eye_count) return {};
        std::lock_guard<std::mutex> lock(mutex_);return surfaces_[movie][eye].image;
    }
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        for(auto& movie:surfaces_) for(auto& surface:movie) surface={};
    }
};
} // namespace d2vr
