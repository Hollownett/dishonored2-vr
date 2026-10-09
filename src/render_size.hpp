#pragma once
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#include "game_stereo.hpp"

namespace d2vr {
struct EyeSize {UINT width{},height{};};
struct ScaledEyeSize {EyeSize size;double requested{},effective{};bool limited{};};
inline ScaledEyeSize scaled_eye_size(EyeSize base,double multiplier,EyeSize maximum) {
    if(!base.width || !base.height || !maximum.width || !maximum.height ||
       !std::isfinite(multiplier) || multiplier<.5 || multiplier>2)
        throw std::invalid_argument("Resolution multiplier must be between 0.5 and 2.0");
    // Only the runtime/swapchain maximum and the D3D11 texture limit bound the
    // scale. Report the effective scale explicitly when either is reached.
    const auto max_width=std::min<UINT>(maximum.width,D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION),
        max_height=std::min<UINT>(maximum.height,D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION);
    const auto effective=std::min({multiplier,static_cast<double>(max_width)/base.width,
                                 static_cast<double>(max_height)/base.height});
    if(effective<.5) throw std::runtime_error("Runtime limits cannot support the minimum resolution multiplier");
    const auto width=static_cast<UINT>(std::lround(base.width*effective));
    const auto height=static_cast<UINT>(std::lround(base.height*effective));
    if(!width || !height || width>max_width || height>max_height) throw std::runtime_error("Scaled resolution exceeds its allocation limit");
    return {{width,height},multiplier,effective,effective+1e-8<multiplier};
}
// OpenXR's typed colour format can have typeless D3D11 storage. CopyResource
// preserves the bits within this format group; the XR format retains sRGB.
inline bool rgba8_copy_compatible(DXGI_FORMAT actual,DXGI_FORMAT requested) noexcept {
    const auto rgba8=[](DXGI_FORMAT value) {
        return value==DXGI_FORMAT_R8G8B8A8_TYPELESS || value==DXGI_FORMAT_R8G8B8A8_UNORM ||
            value==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    };
    return rgba8(actual) && rgba8(requested);
}
inline bool eye_texture_matches(const D3D11_TEXTURE2D_DESC& actual,UINT width,UINT height,DXGI_FORMAT format) noexcept {
    return actual.Width==width && actual.Height==height && rgba8_copy_compatible(actual.Format,format) &&
        actual.MipLevels==1 && actual.ArraySize==1 && actual.SampleDesc.Count==1 && actual.SampleDesc.Quality==0;
}
struct RgbaPixels {
    UINT width{},height{};
    DXGI_FORMAT format{};
    std::vector<std::byte> bytes;
};
// Diagnostic readback of the actual resource, including typeless XR storage.
// Map waits for the copy to complete; retain neither runtime nor game textures.
// Diagnostic only: an unreadable or oversized resource yields no pixels instead
// of an exception, so a failed readback cannot break the rendered frame.
inline RgbaPixels readback_rgba(ID3D11Texture2D* texture) {
    if(!texture) throw std::runtime_error("Missing eye readback texture");
    D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);
    if(!desc.Width || !desc.Height || desc.Width>D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION ||
       desc.Height>D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION ||
       !eye_texture_matches(desc,desc.Width,desc.Height,DXGI_FORMAT_R8G8B8A8_UNORM))
        return {};
    RgbaPixels result{desc.Width,desc.Height,desc.Format,{}};
    const auto row_bytes=static_cast<std::size_t>(desc.Width)*4;
    result.bytes.resize(row_bytes*desc.Height);
    desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.MiscFlags=0;
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
    texture->GetDevice(&device);device->GetImmediateContext(&context);
    if(FAILED(device->CreateTexture2D(&desc,nullptr,&staging))) throw std::runtime_error("Cannot allocate eye readback");
    context->CopyResource(staging.Get(),texture);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if(FAILED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped))) throw std::runtime_error("Cannot map eye readback");
    if(!mapped.pData || mapped.RowPitch<row_bytes) {
        context->Unmap(staging.Get(),0);throw std::runtime_error("Invalid mapped eye row pitch");
    }
    for(UINT row=0;row<desc.Height;++row)
        std::memcpy(result.bytes.data()+row*row_bytes,static_cast<const std::byte*>(mapped.pData)+
            static_cast<std::size_t>(row)*mapped.RowPitch,row_bytes);
    context->Unmap(staging.Get(),0);
    return result;
}
// One native output serves both eyes. Use the enclosing recommendation so
// neither eye is silently undersampled, and reject unsupported allocations.
inline EyeSize recommended_output(const std::array<EyeSize,2>& recommended,
                                 const std::array<EyeSize,2>& maximum,EyeSize system_maximum) {
    EyeSize result{};
    for(const auto& eye:recommended) {
        if(!eye.width || !eye.height) throw std::runtime_error("Invalid recommended eye dimensions");
        result.width=std::max(result.width,eye.width);result.height=std::max(result.height,eye.height);
    }
    if(!system_maximum.width || !system_maximum.height || result.width>system_maximum.width ||
       result.height>system_maximum.height || result.width>D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION ||
       result.height>D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION)
        throw std::runtime_error("Recommended eye size exceeds graphics limits");
    for(const auto& eye:maximum) if(result.width>eye.width || result.height>eye.height)
        throw std::runtime_error("Common recommended output exceeds an eye's limits");
    return result;
}
// A symmetric image includes pixels outside the runtime's asymmetric frustum.
// Enlarge it so the visible region retains the recommended tangent-space density.
inline EyeSize symmetric_output(const std::array<EyeSize,2>& recommended,
                                const std::array<EyeSize,2>& maximum,EyeSize system_maximum,
                                const std::array<EyeFov,2>& optical) {
    std::array<EyeSize,2> expanded{};
    for(unsigned eye=0;eye<2;++eye) {
        const auto symmetric=enclosing_symmetric_fov(optical[eye]);
        if(!symmetric) throw std::runtime_error("Invalid optical frustum for density preservation");
        const auto& f=optical[eye];const auto& s=*symmetric;
        const double width=static_cast<double>(recommended[eye].width)*(std::tan(s.right)-std::tan(s.left))/(std::tan(f.right)-std::tan(f.left));
        const double height=static_cast<double>(recommended[eye].height)*(std::tan(s.up)-std::tan(s.down))/(std::tan(f.up)-std::tan(f.down));
        if(!std::isfinite(width) || !std::isfinite(height) || width<=0 || height<=0 ||
           width>D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION || height>D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION)
            throw std::runtime_error("Optical-density output exceeds graphics limits");
        expanded[eye]={static_cast<UINT>(std::ceil(width*.5))*2,static_cast<UINT>(std::ceil(height*.5))*2};
    }
    return recommended_output(expanded,maximum,system_maximum);
}
class NativeOutputSize {
    std::atomic<HWND> window_{nullptr};
    std::atomic<std::uint64_t> size_{0};
public:
    void request(HWND window,EyeSize size) {
        if(!window || !size.width || !size.height || size.width>D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION ||
           size.height>D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION) throw std::runtime_error("Invalid native output request");
        window_.store(window,std::memory_order_release);
        size_.store((static_cast<std::uint64_t>(size.width)<<32)|size.height,std::memory_order_release);
    }
    void clear() noexcept {size_.store(0,std::memory_order_release);}
    EyeSize for_window(HWND window) const noexcept {
        const auto packed=size_.load(std::memory_order_acquire);
        if(!packed || !window || window!=window_.load(std::memory_order_acquire)) return {};
        return {static_cast<UINT>(packed>>32),static_cast<UINT>(packed)};
    }
    bool apply(HDC context,UINT* width,UINT* height) const noexcept {
        if(!context || !width || !height) return false;
        const auto size=for_window(WindowFromDC(context));
        if(!size.width) return false;
        *width=size.width;*height=size.height;return true;
    }
};
// The native view builder reads a separate cached width/height pair. Keep that
// pair and the resized GPU output consistent, restoring only our own value.
class CachedOutputSize {
    volatile LONG64* target_{};
    LONG64 original_{},requested_{};
public:
    void apply(volatile LONG64* target,EyeSize size) {
        if(target_ || !target || reinterpret_cast<std::uintptr_t>(target)%8 || !size.width || !size.height ||
           size.width>D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION || size.height>D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION)
            throw std::runtime_error("Invalid cached output-size request");
        const auto previous=InterlockedCompareExchange64(target,0,0);
        const auto width=static_cast<UINT>(previous),height=static_cast<UINT>(static_cast<UINT64>(previous)>>32);
        if(!width || !height || width>D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION || height>D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION)
            throw std::runtime_error("Invalid native cached output dimensions");
        const auto desired=static_cast<LONG64>((static_cast<UINT64>(size.height)<<32)|size.width);
        if(InterlockedCompareExchange64(target,desired,previous)!=previous)
            throw std::runtime_error("Native cached dimensions changed during request");
        target_=target;original_=previous;requested_=desired;
    }
    void update(EyeSize size) {
        if(!target_ || !size.width || !size.height || size.width>D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION ||
           size.height>D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION) throw std::runtime_error("Invalid cached output-size update");
        const auto desired=static_cast<LONG64>((static_cast<UINT64>(size.height)<<32)|size.width);
        LONG64 observed{};
        for(unsigned attempt=0;attempt<8;++attempt) {
            observed=InterlockedCompareExchange64(target_,0,0);
            if(observed==desired) {requested_=desired;return;}
            // Window/focus updates may restore the captured native pair. The
            // engine writes its two fields separately; allow a bounded settle.
            if(observed==requested_ || observed==original_) {
                if(InterlockedCompareExchange64(target_,desired,observed)==observed) {requested_=desired;return;}
            }
            Sleep(1);
        }
        throw std::runtime_error("Native cached dimensions changed during update: observed "+
            std::to_string(static_cast<UINT>(observed))+"x"+
            std::to_string(static_cast<UINT64>(observed)>>32));
    }
    bool restore() noexcept {
        if(!target_) return true;
        const auto previous=InterlockedCompareExchange64(target_,original_,requested_);
        if(previous!=requested_ && previous!=original_) return false;
        target_=nullptr;return true;
    }
};
} // namespace d2vr
