#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include "../src/game_xr.hpp"
#include "../src/simulated_xr.hpp"
#include "../src/play_controls.hpp"
#include <cassert>
#include <limits>
#include <iostream>
namespace sim=d2vr::simxr;
using Microsoft::WRL::ComPtr;
static unsigned mode{};
static XrResult XRAPI_CALL locate(XrSpace space,XrSpace base,XrTime time,XrSpaceLocation* out) {
    const auto result=sim::locate_space(space,base,time,out);
    auto* velocity=static_cast<XrSpaceVelocity*>(out->next);
    if(velocity && velocity->type==XR_TYPE_SPACE_VELOCITY) {
        const auto hand=sim::spaces.at(space)->hand;
        velocity->velocityFlags=mode==1?0:XR_SPACE_VELOCITY_LINEAR_VALID_BIT;
        velocity->linearVelocity={hand==1?3.f:.2f,0,0};
        if(mode==2)velocity->linearVelocity.x=std::numeric_limits<float>::quiet_NaN();
    }
    return result;
}
static XrResult XRAPI_CALL dispatch(XrInstance instance,const char* name,PFN_xrVoidFunction* out) {
    if(std::strcmp(name,"xrLocateSpace")==0) {*out=reinterpret_cast<PFN_xrVoidFunction>(&locate);return XR_SUCCESS;}
    return sim::dispatch(instance,name,out);
}
int main() {
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level{};
    assert(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context)));
    sim::configure(device.Get());
    ComPtr<ID3D11Texture2D> scene;
    D3D11_TEXTURE2D_DESC desc{};desc.Width=desc.Height=64;desc.MipLevels=desc.ArraySize=1;
    desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;desc.SampleDesc.Count=1;
    assert(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&scene)));
    d2vr::GameXr xr(device.Get(),64,64,desc.Format,nullptr,true,true,true,true,1,{}, {},true,&dispatch);
    auto frame=[&] {
        if(const auto views=xr.tracking_views())for(unsigned eye=0;eye<2;++eye)xr.record_rendered_eye(eye,views->at(eye).fov);
        xr.present(scene.Get(),scene.Get(),1.6f,1.6f,true,false,true);
    };
    frame();frame();assert(xr.grip_velocity_hands==3 && xr.grip_tracked_hands==3 && xr.grip_speeds[1]==3);
    mode=1;frame();assert(xr.grip_velocity_hands==0 && xr.grip_speeds[1]==0 && xr.grip_tracked_hands==3);
    mode=2;frame();assert(xr.grip_velocity_hands==0 && xr.grip_speeds[1]==0);
    mode=0;sim::control.poses_valid=false;frame();assert(xr.grip_tracked_hands==0);
    assert((xr.grip_tracked_hands&xr.grip_velocity_hands&2u)==0);
    sim::control.poses_valid=true;sim::control.hands=1;frame();assert(xr.grip_velocity_hands==1 && xr.grip_speeds[1]==0);
    sim::control.hands=3;frame();assert(xr.grip_velocity_hands==3);
    xr.close();assert(sim::spaces.empty());
    std::cout<<"PASS: production GameXr keeps valid per-hand runtime velocity separate from zero/missing, nonfinite and tracking/action loss\n";
}
