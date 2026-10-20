#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include "../src/game_xr.hpp"
#include "../src/simulated_xr.hpp"
#include <cassert>
#include <algorithm>
#include <iostream>

namespace sim=d2vr::simxr;
using Microsoft::WRL::ComPtr;
static unsigned created{},timeouts{};
static bool broken{},observer_fault{};
static XrResult XRAPI_CALL create_chain(XrSession session,const XrSwapchainCreateInfo* info,XrSwapchain* out) {
    const auto result=sim::create_chain(session,info,out);if(XR_FAILED(result))return result;
    ++created;
    // Real OpenXR DXGI images are typeless, unlike the old simulation.
    auto& chain=*sim::chains.at(*out);
    for(auto& texture:chain.textures) {
        D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);desc.Format=DXGI_FORMAT_R8G8B8A8_TYPELESS;
        if(broken && (info->width==128 || info->width==192))desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        texture.Reset();assert(SUCCEEDED(sim::device->CreateTexture2D(&desc,nullptr,&texture)));
    }
    return result;
}
static XrResult XRAPI_CALL wait_image(XrSwapchain chain,const XrSwapchainImageWaitInfo* info) {
    if(sim::chains.at(chain)->info.width==128 && timeouts) {
        assert(info->timeout==0);--timeouts;return XR_TIMEOUT_EXPIRED;
    }
    return sim::wait_image(chain,info);
}
static XrResult XRAPI_CALL dispatch(XrInstance instance,const char* name,PFN_xrVoidFunction* out) {
    if(std::strcmp(name,"xrCreateSwapchain")==0){*out=reinterpret_cast<PFN_xrVoidFunction>(&create_chain);return XR_SUCCESS;}
    if(std::strcmp(name,"xrWaitSwapchainImage")==0){*out=reinterpret_cast<PFN_xrVoidFunction>(&wait_image);return XR_SUCCESS;}
    return sim::dispatch(instance,name,out);
}
static void observe(unsigned,ID3D11Texture2D*,std::uintptr_t) {
    if(observer_fault)throw std::runtime_error("injected optional layer draw failure");
}
static ComPtr<ID3D11Texture2D> texture(ID3D11Device* device,UINT width,UINT height) {
    D3D11_TEXTURE2D_DESC d{};d.Width=width;d.Height=height;d.MipLevels=d.ArraySize=1;
    d.Format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;d.SampleDesc.Count=1;
    ComPtr<ID3D11Texture2D> out;assert(SUCCEEDED(device->CreateTexture2D(&d,nullptr,&out)));return out;
}
int main() {
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level{};
    assert(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context)));
    const auto scene=texture(device.Get(),64,64),art=texture(device.Get(),90,24),grown=texture(device.Get(),160,24);
    for(unsigned scenario=0;scenario<3;++scenario) {
        sim::configure(device.Get());created=0;timeouts=scenario==2?2:0;broken=scenario==1;observer_fault=false;
        sim::control.grips[1]={{0,.70710678f,0,.70710678f},{.1f,1.5f,-.45f}}; // health watch: right inner wrist toward the eyes
        {
            d2vr::GameXr xr(device.Get(),64,64,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,nullptr,true,true,true,true,1,{}, {},true,&dispatch);
            xr.observe_native_wrist_art=&observe;
            const auto frame=[&]{
                if(const auto views=xr.tracking_views())for(unsigned eye=0;eye<2;++eye)xr.record_rendered_eye(eye,views->at(eye).fov);
                xr.present(scene.Get(),scene.Get(),1.6f,1.6f,true,false,true);
            };
            frame();frame();assert(xr.last_tracked_pair && xr.last_layer_submitted);
            assert(xr.native_art_source(1,true,art.Get(),{0,0,0,90,24,1},123,xr.pose_epoch(),GetTickCount64()));
            frame();assert(xr.last_tracked_pair && xr.last_layer_submitted && !xr.native_art_presented(1,true)); // the canvas is created between frames: this one only records its size
            frame();assert(xr.last_tracked_pair && xr.last_layer_submitted);
            if(scenario==2)assert(!xr.native_art_presented(1,true));
            if(scenario==1) {
                assert(xr.wrist_failures==1 && FAILED(xr.wrist_failure_hresult));
                assert(sim::summary.overlays.empty() && xr.wrist_device_removed==S_OK);
                assert(!xr.native_art_presented(1,true));
            }else {
                frame();frame();assert(!sim::summary.overlays.empty());
                assert(std::any_of(sim::summary.overlays.begin(),sim::summary.overlays.end(),[](const auto& o){return std::abs(o.size.width-.07f)<1e-5f;})); // smaller watch face (the controls card may follow)
                assert(xr.native_art_presented(1,true));
                assert(xr.wrist_target_creations==3); // cached, typed views
                const unsigned base=created; // projection + wrist (+ the left-wrist controls card once that wrist faces the eyes)
                if(scenario==2)assert(xr.wrist_timeouts==2);
                assert(xr.native_art_source(1,true,art.Get(),{0,0,0,89,24,1},123,xr.pose_epoch(),GetTickCount64()));
                frame();assert(xr.wrist_target_creations==3 && created==base); // one-pixel animation reuses canvas
                assert(xr.native_art_source(1,true,grown.Get(),{0,0,0,150,24,1},123,xr.pose_epoch(),GetTickCount64()));
                frame(); // The outgrown canvas is only flagged inside the frame.
                frame(); // First retirement poll (between frames) fences the old GPU work and skips only this panel.
                assert(created==base && xr.wrist_retirement_waits>0 && xr.last_layer_submitted);
                assert(!xr.native_art_presented(1,true) && sim::chains.size()==6); // eyes, pointer, controls card, wheel dot, wrist
                for(unsigned poll=0;poll<100 && created==base;++poll) {Sleep(1);frame();}
                assert(created==base+1 && xr.wrist_target_creations==6 && sim::chains.size()==base);
                assert(xr.native_art_presented(1,true));
                observer_fault=true;frame();assert(xr.wrist_failures==1 && xr.last_layer_submitted);
                assert(sim::summary.overlays.empty());observer_fault=false;
                assert(!xr.native_art_presented(1,true));
            }
            assert(sim::summary.protocol_errors==0 && sim::summary.chain_ops_in_frame==0 && xr.swapchain_creates_in_frame==0);
        }
        assert(sim::chains.empty());
    }
    std::cout << "Typed wrist textures, cached views, allocation/draw faults and nonblocking wait recovery passed\n";
}
