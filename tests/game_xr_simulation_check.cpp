#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include "../src/game_xr.hpp"
#include "../src/simulated_xr.hpp"
#include "../src/simulated_gameplay_script.hpp"
#include <cassert>
#include <iostream>
#include <fstream>
#include <sstream>

using Microsoft::WRL::ComPtr;
namespace sim=d2vr::simxr;
using Pixel=std::array<unsigned char,4>;
static ComPtr<ID3D11Texture2D> texture(ID3D11Device* device,unsigned size,unsigned char red,unsigned char green,unsigned char blue) {
    std::vector<unsigned char> pixels(size*size*4);
    for(std::size_t i=0;i<pixels.size();i+=4) {pixels[i]=red;pixels[i+1]=green;pixels[i+2]=blue;pixels[i+3]=255;}
    D3D11_TEXTURE2D_DESC desc{};desc.Width=desc.Height=size;desc.MipLevels=desc.ArraySize=1;
    desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;desc.SampleDesc.Count=1;
    D3D11_SUBRESOURCE_DATA data{pixels.data(),size*4,0};ComPtr<ID3D11Texture2D> out;
    assert(SUCCEEDED(device->CreateTexture2D(&desc,&data,&out)));return out;
}
static Pixel pixel(ID3D11Device* device,ID3D11DeviceContext* context,ID3D11Texture2D* source) {
    D3D11_TEXTURE2D_DESC desc{};source->GetDesc(&desc);desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;assert(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&staging)));context->CopyResource(staging.Get(),source);
    D3D11_MAPPED_SUBRESOURCE mapped{};assert(SUCCEEDED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped)));
    Pixel out{};std::memcpy(out.data(),mapped.pData,4);context->Unmap(staging.Get(),0);return out;
}
int main() {
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level{};
    assert(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context)));
    sim::configure(device.Get());
    auto left=texture(device.Get(),64,23,147,81),right=texture(device.Get(),64,98,34,231);
    unsigned scenarios=0;
    {
        d2vr::GameXr xr(device.Get(),64,64,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,nullptr,true,true,true,true,1,{}, {},true,&sim::dispatch);
        auto frame=[&](bool gameplay=true,bool authored=false,bool complete=true,bool settings_available=true) {
            if(xr.tracking_views() && complete) {
                const auto& views=*xr.tracking_views();xr.record_rendered_eye(0,views[0].fov);xr.record_rendered_eye(1,views[1].fov);
            }
            xr.present(left.Get(),right.Get(),1.6f,1.6f,gameplay,authored,settings_available);
        };
        frame();frame();assert(xr.last_layer_submitted && xr.last_tracked_pair);
        assert(sim::summary.layer_type==XR_TYPE_COMPOSITION_LAYER_PROJECTION);
        assert(pixel(device.Get(),context.Get(),sim::summary.images[0].Get())==Pixel({23,147,81,255}));
        assert(pixel(device.Get(),context.Get(),sim::summary.images[1].Get())==Pixel({98,34,231,255}));
        assert(xr.timing.display_time==xr.timing.rendered_display_times[0]);
        assert(xr.timing.rendered_display_times[0]==xr.timing.rendered_display_times[1]);
        // Compiled developer head pitch travels through real XR locate/action sampling.
        d2vr::SimulatedGameplayScript head_script;
        std::istringstream head_source("D2VR_GAMEPLAY_SCRIPT 2 10000 1\n1000 2000 0 0 0 0 0 0 0 0 0 0 1 0 0 0 0 -60\n");
        head_script.load(head_source,10);
        const auto stable_head_position=sim::control.head.position;
        const auto stable_grips=sim::control.grips,stable_aims=sim::control.aims;
        float degrees{};sim::control.input=head_script.sample(1500,true,true,nullptr,&degrees);
        auto q=d2vr::SimulatedGameplayScript::pitch_orientation(degrees);
        sim::control.head.orientation={q[0],q[1],q[2],q[3]};frame();frame();
        assert(xr.last_layer_submitted && xr.last_tracked_pair && xr.controller_input.x);
        const auto& head_views=*xr.tracking_views();
        for(unsigned side=0;side<2;++side) {
            assert(std::abs(head_views[side].pose.orientation.x+.5f)<1e-6f);
            assert(std::abs(head_views[side].pose.orientation.w-.8660254f)<1e-6f);
            assert(std::memcmp(&xr.grip_locations[side].pose,&stable_grips[side],sizeof(XrPosef))==0);
            assert(std::memcmp(&xr.aim_locations[side].pose,&stable_aims[side],sizeof(XrPosef))==0);
        }
        assert(std::memcmp(&sim::control.head.position,&stable_head_position,sizeof(XrVector3f))==0);
        sim::control.input=head_script.sample(2000,true,true,nullptr,&degrees);
        q=d2vr::SimulatedGameplayScript::pitch_orientation(degrees);
        sim::control.head.orientation={q[0],q[1],q[2],q[3]};frame();frame();
        assert(!xr.controller_input.x && xr.tracking_views()->at(0).pose.orientation.x==0);++scenarios;
        sim::control.advance=sim::control.period*3;frame();
        assert(xr.timing.display_time-xr.timing.rendered_display_times[0]==sim::control.period*2);
        sim::control.advance=sim::control.period;frame();
        assert(xr.timing.display_time==xr.timing.rendered_display_times[0]);
        ++scenarios;
        // Incomplete eyes cannot submit a reused gameplay image.
        frame(true,false,false);assert(!xr.last_layer_submitted);frame();assert(xr.last_layer_submitted);++scenarios;
        sim::control.input.move_y=.8f;sim::control.input.a=true;sim::control.input.right_grip=.9f;frame();
        assert(xr.controller_input.move_y==.8f && xr.controller_input.a && xr.controller_input.right_grip==.9f);++scenarios;
        // Actual right-hand ray -> native menu pointer -> held-trigger Resume gate.
        sim::control.aims[1]=sim::control.head;sim::control.input.right_trigger=0;frame(false);
        assert(xr.native_menu_input.active && std::abs(xr.native_menu_input.u-.5)<.001 && std::abs(xr.native_menu_input.v-.5)<.001);
        sim::control.input.right_trigger=1;frame(false);assert(xr.native_menu_input.trigger && xr.controller_input.right_trigger==0);
        const auto panel=xr.last_menu_pose;sim::control.head.position.x+=.1f;frame(false);
        assert(xr.last_menu_pose.position.x==panel.position.x);frame();assert(xr.controller_input.right_trigger==0);
        sim::control.input.right_trigger=0;frame();sim::control.input.right_trigger=1;frame();assert(xr.controller_input.right_trigger==1);++scenarios;
        // Desktop/VR return cancels actions and requires a new rendered pair.
        for(unsigned cycle=0;cycle<3;++cycle) {
            sim::push_state(XR_SESSION_STATE_SYNCHRONIZED);frame(false);
            assert(!xr.last_layer_submitted && !xr.controller_input.active);
            sim::push_state(XR_SESSION_STATE_FOCUSED);frame(false);
            assert(xr.last_layer_submitted && xr.native_menu_input.active);
            frame();frame();assert(xr.last_layer_submitted);
        }
        ++scenarios;
        // Authored gameplay keeps distinct tracked stereo eyes and projection.
        frame(true,true);assert(xr.last_authored_camera_layer && xr.last_tracked_pair && sim::summary.layer_type==XR_TYPE_COMPOSITION_LAYER_PROJECTION);
        sim::control.head.position.z+=.2f;frame(true,true);
        assert(pixel(device.Get(),context.Get(),sim::summary.images[0].Get())==Pixel({23,147,81,255}));
        assert(pixel(device.Get(),context.Get(),sim::summary.images[1].Get())==Pixel({98,34,231,255}));
        frame(true,true,false);assert(!xr.last_layer_submitted);
        frame();assert(sim::summary.layer_type==XR_TYPE_COMPOSITION_LAYER_PROJECTION);++scenarios;
        // Native non-pause pages keep Y and revoke an already open panel.
        sim::control.input={};sim::control.input.active=true;frame(false,false,true,false);
        sim::control.input.y=true;frame(false,false,true,false);
        assert(!xr.settings_open() && xr.controller_input.active && xr.controller_input.y);
        frame(false);assert(!xr.settings_open() && xr.controller_input.y); // Held Y cannot open on entry.
        sim::control.input.y=false;frame(false);sim::control.input.y=true;frame(false);assert(xr.settings_open());
        frame(false,false,true,false);
        assert(!xr.settings_open() && xr.controller_input.active && xr.controller_input.y);
        frame();assert(!xr.settings_open() && xr.controller_input.y);++scenarios;
        // Pause settings consumes input and exposes a real resolution request.
        sim::control.input={};sim::control.input.active=true;frame(false);
        sim::control.input.y=true;frame(false);assert(xr.settings_open());assert(!xr.controller_input.active);
        sim::control.input.y=false;frame(false);sim::control.input.move_x=1;frame(false);
        assert(xr.take_resolution_request()==1.1);sim::control.input.move_x=0;frame(false);
        sim::control.input.b=true;frame(false);assert(!xr.settings_open());++scenarios;
        // Required setup opens on Pause, then real XR actions capture a measured pose.
        sim::control.input={};sim::control.input.active=true;
        sim::control.input.left_grip=sim::control.input.right_grip=1;
        sim::control.head={{0,0,0,1},{0,1.65f,0}};
        sim::control.grips={{{{0,0,0,1},{-.8f,1.45f,0}},{{0,0,0,1},{.8f,1.45f,0}}}};
        xr.avatar_measurements({1.6,.38});
        xr.avatar_runtime_status(d2vr::AvatarFitRuntimeStatus::recalibration_required);
        frame(false);assert(xr.settings_open() && !xr.controller_input.active);
        sim::control.input.a=true;frame(false);sim::control.input.a=false;
        for(unsigned sample=0;sample<24;++sample) {Sleep(50);frame(false);}
        const auto calibrated=xr.avatar_calibration();assert(calibrated);
        assert(std::abs(calibrated->arm_scale-1)<.001 && std::abs(calibrated->eye_to_shoulder_metres-.2)<.001);
        assert(!calibrated->standing_eye_height_metres);
        xr.avatar_runtime_status(d2vr::AvatarFitRuntimeStatus::active);xr.suspend_avatar();
        assert(xr.avatar_calibration() && xr.avatar_calibration_state()==d2vr::AvatarCalibrationState::ready);
        assert(xr.avatar_runtime_status()==d2vr::AvatarFitRuntimeStatus::verifying);
        xr.avatar_measurements({1.6,.38});xr.avatar_runtime_status(d2vr::AvatarFitRuntimeStatus::active);
        sim::control.input.y=true;frame(false);assert(!xr.settings_open());
        sim::control.input.y=false;frame(false);sim::control.input.y=true;frame(false);
        sim::control.input.y=false;frame(false);sim::control.input.a=true;frame(false);sim::control.input.a=false;
        assert(xr.avatar_calibration()); // A cancelled retry keeps the previous valid fit.
        sim::control.input.y=true;frame(false);sim::control.input.y=false;
        for(unsigned sample=0;sample<24;++sample) {Sleep(50);frame();}
        assert(xr.avatar_calibration() && !xr.settings_open());
        assert(xr.avatar_calibration()->palm_span_metres==calibrated->palm_span_metres);
        xr.reset_avatar();assert(!xr.avatar_calibration());
        xr.avatar_measurements({1.6,.38});
        sim::control.input.y=true;frame(false);sim::control.input.y=false;frame(false);
        sim::control.input.a=true;frame(false);sim::control.input.a=false;frame(false);
        sim::push_state(XR_SESSION_STATE_STOPPING);frame(false);
        sim::push_state(XR_SESSION_STATE_READY);frame(false);sim::push_state(XR_SESSION_STATE_FOCUSED);frame(false);
        for(unsigned sample=0;sample<24;++sample) {Sleep(50);frame();}
        assert(!xr.avatar_calibration() && !xr.settings_open());++scenarios;
        // Suppressed/invalid poses and reference changes never reuse stale stereo.
        sim::control.should_render=false;frame();assert(!xr.last_layer_submitted);
        sim::control.should_render=true;frame();assert(!xr.last_layer_submitted);frame();assert(xr.last_layer_submitted);
        sim::control.poses_valid=false;frame();assert(!xr.last_layer_submitted);
        sim::control.poses_valid=true;frame();frame();assert(xr.last_layer_submitted);
        const auto epoch=xr.reference_epoch();sim::reference_change(sim::display_time+sim::control.advance);frame();
        assert(xr.reference_epoch()==epoch+1 && !xr.last_layer_submitted);frame();assert(xr.last_layer_submitted);++scenarios;
        // Both chains resize together; output really contains the new native image.
        assert(xr.resize(96,96,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB));
        left=texture(device.Get(),96,120,17,63);right=texture(device.Get(),96,10,80,99);
        frame(true,false,false);frame();assert(xr.last_layer_submitted);
        assert(sim::summary.eyes[0].subImage.imageRect.extent.width==96);
        assert(pixel(device.Get(),context.Get(),sim::summary.images[1].Get())==Pixel({10,80,99,255}));++scenarios;
        // Repeat lifecycle/load at high speed to expose stale handles and leaks.
        for(unsigned i=0;i<1000;++i) frame();++scenarios;
        assert(sim::summary.protocol_errors==0);
    }
    assert(!sim::instantiated && !sim::session_created && sim::spaces.empty() && sim::chains.empty());
    assert(sim::summary.protocol_errors==0 && sim::summary.waits==sim::summary.ends);
    std::ofstream report("artifacts/simulated-vr-check.json");
    report << "{\"simulated_vr\":true,\"bridge\":\"production GameXr\",\"device\":\"D3D11 WARP\",\"scenarios\":" << scenarios
           << ",\"xr_frames\":" << sim::summary.ends << ",\"protocol_errors\":0,\"resource_cleanup_verified\":true,\"native_game_interaction_verified\":false,\"headset_acceptance\":false}\n";
    assert(report.good());
    std::cout << "PASS: production XR bridge simulated stereo pixels, menu ray/trigger, three focus returns, authored camera, input, settings, resize, suppressed poses, reference changes, 1000-frame soak and resource cleanup\n";
}
