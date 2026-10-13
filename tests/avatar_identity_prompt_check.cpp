#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include "../src/game_xr.hpp"
#include "../src/simulated_xr.hpp"
#include <cassert>
#include <iostream>
int main(){
    Microsoft::WRL::ComPtr<ID3D11Device> device;assert(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,nullptr)));
    namespace sim=d2vr::simxr;sim::configure(device.Get());
    d2vr::GameXr xr(device.Get(),64,64,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,nullptr,true,true,false,true,1,{}, {},false,&sim::dispatch);
    sim::control.input={};sim::control.input.active=true;
    const auto pause=[&]{xr.present(nullptr,nullptr,1.6f,1.6f,false,false,true);};
    const auto dismiss=[&] {
        // B returns from capture to settings, then a fresh B dismisses settings.
        sim::control.input.b=true;pause();sim::control.input.b=false;pause();
        sim::control.input.b=true;pause();sim::control.input.b=false;pause();
        assert(!xr.settings_open());
    };
    xr.avatar_runtime_status(d2vr::AvatarFitRuntimeStatus::recalibration_required);pause();pause();pause();assert(xr.settings_open());
    sim::control.input.a=true;pause();sim::control.input.a=false;pause();assert(xr.avatar_calibration_state()==d2vr::AvatarCalibrationState::idle);
    sim::control.input.y=true;pause();sim::control.input.y=false;pause();assert(xr.settings_open());
    sim::control.input.y=true;pause();sim::control.input.y=false;pause();assert(xr.settings_open());
    dismiss();
    // Old main owner replacement uses suspension, which cannot distinguish focus/dismiss from a new identity.
    xr.suspend_avatar();xr.avatar_measurements({1.6,.38});
    xr.present(nullptr,nullptr,1.6f,1.6f,true,false,false);pause();pause();assert(!xr.settings_open());
    // A proved identity replacement, unlike ordinary suspension, rearms once.
    xr.avatar_identity_changed();xr.avatar_measurements({1.6,.38});pause();pause();assert(xr.settings_open());
    sim::control.head={{0,0,0,1},{0,1.65f,0}};
    sim::control.grips={{{{0,0,0,1},{-.8f,1.45f,0}},{{0,0,0,1},{.8f,1.45f,0}}}};
    sim::control.input.left_grip=sim::control.input.right_grip=1;
    sim::control.input.a=true;pause();sim::control.input.a=false;
    for(unsigned i=0;i<24;++i){Sleep(50);pause();}
    assert(xr.avatar_calibration() && xr.avatar_calibration_state()==d2vr::AvatarCalibrationState::ready);
    assert(xr.avatar_calibration()->arm_lengths_measured);
    xr.avatar_identity_changed();assert(!xr.avatar_calibration());
    // The fit reuse lane independently proves compatibility and reapplies active.
    xr.avatar_measurements({1.6,.38});xr.avatar_runtime_status(d2vr::AvatarFitRuntimeStatus::active);pause();pause();assert(!xr.settings_open());
    // A reference-space identity change rearms even if a prior notice was dismissed.
    xr.avatar_runtime_status(d2vr::AvatarFitRuntimeStatus::recalibration_required);pause();assert(xr.settings_open());
    dismiss();
    const auto previous_reference=xr.reference_epoch();sim::reference_change(sim::display_time+sim::control.advance);pause();pause();pause();
    assert(xr.reference_epoch()!=previous_reference && xr.settings_open());
    assert(!xr.avatar_calibration());
    std::cout<<"PASS: unavailable/dismissed wizard identity rearm, suspension nonnag, stale measured candidate cleared, active reuse suppresses prompt, actual XR reference rearm\n";
}
