#pragma once
// v1403 (docs/PERF-PLAN-90HZ-2026-11-19.md 2A.5): NVIDIA fixed foveated shading through NVAPI's D3D11 Variable Rate Shading, opt-in
// (build/nvidia-vrs). The same technique as BetterVR 1.0 (on by default there, Vulkan fragment shading rate), Metro Exodus VR and
// vrperfkit (D3D11 + NVAPI): full-rate shading around each eye's optical centre, coarser shading in the periphery the lens blurs.
//
// - One R8_UINT shading-rate surface per eye, one texel per 16x16 pixel tile of the engine's full-size scene target (the engine renders
//   into the top-left render rectangle of that target at the current render fraction, fsr_taa.hpp). Texel = index into the viewport's
//   shading-rate table: 0 full rate, 1 one shade per 2x2, 2 one shade per 4x4.
// - The rings are measured from the eye's optical centre (fovea::centre_*, published by GameXr each frame) in units of the render
//   rectangle's height: full rate inside `inner`, 2x2 to `outer`, 4x4 beyond (4x4 only when the knob asks for it).
// - Applied by RenderPassMap's job observer around the main scene passes (FORWARD_WORLD, FORWARD_DYNAMIC) on the job's own deferred
//   context, and switched off again after the job, so nothing else (depth, shadows, post, the mod's passes) is ever coarse-shaded.
// - Any NVAPI failure turns the feature off for the session and is counted in the status ("nvidia_vrs").
// Knob text (all optional): "<inner> <outer> [4x4]", default "0.35 0.55": full rate within 35 % of the render height of the centre,
// 2x2 beyond it, 4x4 beyond 55 % only with the token 4x4.
#include <d3d11.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>
// The SDK (github.com/NVIDIA/nvapi, MIT) lives in third_party/nvapi and is not committed; without it the feature compiles to a stub
// that reports "no_nvapi_sdk", so builds without the SDK keep working.
#if __has_include("../third_party/nvapi/nvapi.h")
#include "../third_party/nvapi/nvapi.h"
#pragma comment(lib,"third_party/nvapi/amd64/nvapi64.lib")
#define D2VR_HAVE_NVAPI 1
#else
#define D2VR_HAVE_NVAPI 0
#endif

namespace d2vr {
#if !D2VR_HAVE_NVAPI
class NvVrs {
public:
    struct Config {float inner=.35f,outer=.55f;bool quarter=false;};
    static NvVrs& instance() noexcept {static NvVrs v;return v;}
    bool init(ID3D11Device*,Config) noexcept {return false;}
    void update(ID3D11DeviceContext*,unsigned,unsigned,unsigned,unsigned,unsigned,float,float) noexcept {}
    void begin(ID3D11DeviceContext*,int) noexcept {}
    void end(ID3D11DeviceContext*) noexcept {}
    bool enabled() const noexcept {return false;}
    std::string json() {return "{\"enabled\":false,\"reason\":\"no_nvapi_sdk\"}";}
};
#else
class NvVrs {
public:
    struct Config {float inner=.35f,outer=.55f;bool quarter=false;};
    static NvVrs& instance() noexcept {static NvVrs v;return v;}
    // Present thread, once per session, on an NVIDIA adapter with the knob present.
    bool init(ID3D11Device* device,Config c) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        config_=c;device_=device;
        if(NvAPI_Initialize()!=NVAPI_OK) {reason_="NvAPI_Initialize";return false;}
        NV_D3D1x_GRAPHICS_CAPS caps{};
        if(NvAPI_D3D1x_GetGraphicsCapabilities(device,NV_D3D1x_GRAPHICS_CAPS_VER,&caps)!=NVAPI_OK) {reason_="GetGraphicsCapabilities";return false;}
        if(!caps.bVariablePixelRateShadingSupported) {reason_="vrs_unsupported";return false;}
        ready_=true;reason_="none";enabled_.store(true,std::memory_order_release);
        return true;
    }
    // Present thread, every frame: the eye's full target size, its render rectangle and optical centre (NDC of the render frustum).
    void update(ID3D11DeviceContext* immediate,unsigned eye,unsigned target_w,unsigned target_h,unsigned render_w,unsigned render_h,float centre_x,float centre_y) noexcept {
        if(!enabled_.load(std::memory_order_acquire) || eye>1 || !target_w || !target_h || !render_w || !render_h) return;
        std::lock_guard<std::mutex> lock(mutex_);
        auto& e=eyes_[eye];
        const unsigned tw=(target_w+15)/16,th=(target_h+15)/16;
        const int cx=static_cast<int>(std::lround((centre_x+1)*.5*render_w)),cy=static_cast<int>(std::lround((1-centre_y)*.5*render_h));
        if(e.view && e.tw==tw && e.th==th && e.rw==render_w && e.rh==render_h && std::abs(e.cx-cx)<16 && std::abs(e.cy-cy)<16) return;
        if(!e.texture || e.tw!=tw || e.th!=th) {
            // a recorded (not yet executed) deferred list may still name the old view: keep it alive for the session
            if(e.view) {retired_.push_back(e.view);e.view=nullptr;}
            if(e.texture) {retired_textures_.push_back(e.texture);e.texture=nullptr;}
            D3D11_TEXTURE2D_DESC d{};d.Width=tw;d.Height=th;d.MipLevels=1;d.ArraySize=1;d.Format=DXGI_FORMAT_R8_UINT;d.SampleDesc.Count=1;
            d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
            if(FAILED(device_->CreateTexture2D(&d,nullptr,&e.texture))) {fail("CreateTexture2D");return;}
            NV_D3D11_SHADING_RATE_RESOURCE_VIEW_DESC v{};v.version=NV_D3D11_SHADING_RATE_RESOURCE_VIEW_DESC_VER;v.Format=DXGI_FORMAT_R8_UINT;
            v.ViewDimension=NV_SRRV_DIMENSION_TEXTURE2D;v.Texture2D.MipSlice=0;
            if(NvAPI_D3D11_CreateShadingRateResourceView(device_,e.texture,&v,&e.view)!=NVAPI_OK || !e.view) {fail("CreateShadingRateResourceView");return;}
        }
        std::vector<std::uint8_t> rates(static_cast<std::size_t>(tw)*th,0);
        const double h=static_cast<double>(render_h);
        for(unsigned y=0;y<th;++y) for(unsigned x=0;x<tw;++x) {
            const double px=x*16.+8,py=y*16.+8;
            std::uint8_t r=0;
            if(px<render_w && py<render_h) { // outside the render rectangle nothing is drawn: leave it full rate
                const double d=std::hypot(px-cx,py-cy)/h;
                r=d<config_.inner?0:(config_.quarter && d>=config_.outer)?2:1;
            }
            rates[static_cast<std::size_t>(y)*tw+x]=r;
        }
        immediate->UpdateSubresource(e.texture,0,nullptr,rates.data(),tw,0);
        std::size_t coarse=0;for(auto r:rates) coarse+=r!=0;
        e.tw=tw;e.th=th;e.rw=render_w;e.rh=render_h;e.cx=cx;e.cy=cy;e.coarse_share=rates.empty()?0.f:static_cast<float>(coarse)/rates.size();
        ++surface_builds_;
    }
    // Job threads (RenderPassMap observer): begin/end of one scene pass on its deferred context.
    void begin(ID3D11DeviceContext* context,int eye) noexcept {
        if(!enabled_.load(std::memory_order_acquire) || eye<0 || eye>1 || !context) return;
        ID3D11NvShadingRateResourceView* view;
        {std::lock_guard<std::mutex> lock(mutex_);view=eyes_[eye].view;}
        if(!view) {skipped_.fetch_add(1,std::memory_order_relaxed);return;}
        NV_D3D11_VIEWPORT_SHADING_RATE_DESC vp[NV_MAX_NUM_VIEWPORTS]{};
        for(auto& v:vp) {
            v.enableVariablePixelShadingRate=true;
            for(auto& r:v.shadingRateTable) r=NV_PIXEL_X1_PER_RASTER_PIXEL;
            v.shadingRateTable[1]=NV_PIXEL_X1_PER_2X2_RASTER_PIXELS;v.shadingRateTable[2]=NV_PIXEL_X1_PER_4X4_RASTER_PIXELS;
        }
        NV_D3D11_VIEWPORTS_SHADING_RATE_DESC desc{};desc.version=NV_D3D11_VIEWPORTS_SHADING_RATE_DESC_VER;desc.numViewports=NV_MAX_NUM_VIEWPORTS;desc.pViewports=vp;
        if(NvAPI_D3D11_RSSetShadingRateResourceView(context,view)!=NVAPI_OK || NvAPI_D3D11_RSSetViewportsPixelShadingRates(context,&desc)!=NVAPI_OK) {
            std::lock_guard<std::mutex> lock(mutex_);fail("set on context");return;
        }
        applied_.fetch_add(1,std::memory_order_relaxed);
    }
    void end(ID3D11DeviceContext* context) noexcept {
        if(!ready_ || !context) return;
        NV_D3D11_VIEWPORTS_SHADING_RATE_DESC desc{};desc.version=NV_D3D11_VIEWPORTS_SHADING_RATE_DESC_VER;desc.numViewports=0;desc.pViewports=nullptr;
        NvAPI_D3D11_RSSetViewportsPixelShadingRates(context,&desc); // zero viewports: off for every viewport
        NvAPI_D3D11_RSSetShadingRateResourceView(context,nullptr);
    }
    bool enabled() const noexcept {return enabled_.load(std::memory_order_acquire);}
    std::string json() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::ostringstream o;o.precision(4);
        o<<"{\"enabled\":"<<(enabled_.load()?"true":"false")<<",\"reason\":\""<<reason_<<"\",\"inner\":"<<config_.inner<<",\"outer\":"<<config_.outer
         <<",\"quarter\":"<<(config_.quarter?"true":"false")<<",\"applied\":"<<applied_.load()<<",\"skipped\":"<<skipped_.load()
         <<",\"surface_builds\":"<<surface_builds_<<",\"coarse_share\":["<<eyes_[0].coarse_share<<','<<eyes_[1].coarse_share<<"]}";
        return o.str();
    }
private:
    struct Eye {ID3D11Texture2D* texture{};ID3D11NvShadingRateResourceView* view{};unsigned tw{},th{},rw{},rh{};int cx{},cy{};float coarse_share{};};
    void fail(const char* why) noexcept {reason_=why;enabled_.store(false,std::memory_order_release);}
    std::mutex mutex_;Config config_{};ID3D11Device* device_{};bool ready_{};std::string reason_{"off"};
    std::atomic<bool> enabled_{false};std::array<Eye,2> eyes_{};
    std::atomic<std::uint64_t> applied_{},skipped_{};std::uint64_t surface_builds_{};
    std::vector<ID3D11NvShadingRateResourceView*> retired_;std::vector<ID3D11Texture2D*> retired_textures_;
};
#endif
} // namespace d2vr
