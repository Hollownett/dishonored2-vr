#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include "../src/game_xr.hpp"
#include "../src/simulated_xr.hpp"
#include <cassert>
#include <iostream>
int main(){
    Microsoft::WRL::ComPtr<ID3D11Device> device;Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    assert(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context)));
    namespace sim=d2vr::simxr;sim::configure(device.Get());
    d2vr::GameXr xr(device.Get(),64,64,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,nullptr,true,true,false,true,1,{}, {},false,&sim::dispatch);
    const auto frame=[&](bool gameplay,bool pause){xr.present(nullptr,nullptr,1.6f,1.6f,gameplay,false,pause);};
    sim::control.input={};sim::control.input.active=true;
    xr.avatar_runtime_status(d2vr::AvatarFitRuntimeStatus::recalibration_required);
    // Death/Loading/Ready are non-pause pages: no prompt consumed or acknowledged.
    for(unsigned i=0;i<6;++i){frame(false,false);assert(!xr.settings_open() && !xr.avatar_calibration());}
    xr.suspend_avatar();frame(false,false);
    // New owned rig appears during ordinary gameplay, just like the actual run.
    xr.avatar_measurements({1.6,.38});
    for(unsigned i=0;i<4;++i){frame(true,false);assert(!xr.settings_open() && !xr.avatar_calibration());}
    assert(xr.avatar_runtime_status()==d2vr::AvatarFitRuntimeStatus::recalibration_required);
    // First eligible Pause primes neutral controls, then prompts automatically.
    frame(false,true);assert(!xr.settings_open());frame(false,true);assert(xr.settings_open());
    sim::control.input.left_grip=sim::control.input.right_grip=1;
    sim::control.input.a=true;frame(false,true);sim::control.input.a=false;
    // Default grip span is not a T pose: a started wizard cannot invent a fit.
    for(unsigned i=0;i<24;++i){Sleep(50);frame(false,true);}
    assert(!xr.avatar_calibration() && xr.avatar_calibration_state()==d2vr::AvatarCalibrationState::pose_invalid);
    assert(xr.avatar_runtime_status()==d2vr::AvatarFitRuntimeStatus::recalibration_required);
    // A narrow grip span is still not a T/A pose; an explicit retry with arms out succeeds directly.
    sim::control.head={{0,0,0,1},{0,1.65f,0}};
    sim::control.grips={{{{0,0,0,1},{-.22f,1.45f,0}},{{0,0,0,1},{.22f,1.45f,0}}}};
    sim::control.input.a=true;frame(false,true);sim::control.input.a=false;
    for(unsigned i=0;i<4;++i){Sleep(50);frame(false,true);}
    assert(!xr.avatar_calibration() && xr.avatar_calibration_state()==d2vr::AvatarCalibrationState::pose_invalid);
    sim::control.grips={{{{0,0,0,1},{-.8f,1.45f,0}},{{0,0,0,1},{.8f,1.45f,0}}}};
    for(unsigned i=0;i<24;++i){Sleep(50);frame(false,true);}
    assert(xr.avatar_calibration() && xr.avatar_calibration_state()==d2vr::AvatarCalibrationState::ready);
    const auto fit=xr.avatar_calibration();
    assert(fit->arm_lengths_measured && !fit->shoulder_span_measured && std::abs(fit->shoulder_span_metres-.38)<.001);
    assert(std::abs(*fit->arm_reach_metres[0]-.5)<.2 && std::abs(*fit->arm_reach_metres[0]-*fit->arm_reach_metres[1])<.001);
    assert(!fit->torso_width_measured && !fit->torso_width_metres && !fit->standing_eye_height_metres);
    std::cout<<"PASS: actual GameXr Death/Loading to new rig preserves first Pause prompt; invalid capture gives no fit, explicit single-pose retry succeeds\n";
}
