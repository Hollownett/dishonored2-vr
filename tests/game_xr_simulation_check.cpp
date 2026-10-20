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
static ComPtr<ID3D11Texture2D> texture(ID3D11Device* device,unsigned size,unsigned char red,unsigned char green,unsigned char blue,UINT bindings=0) {
    std::vector<unsigned char> pixels(size*size*4);
    for(std::size_t i=0;i<pixels.size();i+=4) {pixels[i]=red;pixels[i+1]=green;pixels[i+2]=blue;pixels[i+3]=255;}
    D3D11_TEXTURE2D_DESC desc{};desc.Width=desc.Height=size;desc.MipLevels=desc.ArraySize=1;
    desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;desc.SampleDesc.Count=1;desc.BindFlags=bindings;
    D3D11_SUBRESOURCE_DATA data{pixels.data(),size*4,0};ComPtr<ID3D11Texture2D> out;
    assert(SUCCEEDED(device->CreateTexture2D(&desc,&data,&out)));return out;
}
static Pixel pixel(ID3D11Device* device,ID3D11DeviceContext* context,ID3D11Texture2D* source,unsigned x=0,unsigned y=0) {
    D3D11_TEXTURE2D_DESC desc{};source->GetDesc(&desc);desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;assert(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&staging)));context->CopyResource(staging.Get(),source);
    D3D11_MAPPED_SUBRESOURCE mapped{};assert(SUCCEEDED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped)));
    Pixel out{};std::memcpy(out.data(),static_cast<const unsigned char*>(mapped.pData)+y*mapped.RowPitch+x*4,4);context->Unmap(staging.Get(),0);return out;
}
int main() {
    {   // Tutorial panel: 1.2 m ahead of the head midpoint, below eye level, rotating with the head.
        XrPosef l{},r{};l.orientation.w=r.orientation.w=1;l.position={-.03f,1.6f,0};r.position={.03f,1.6f,0};
        auto p=d2vr::head_locked_art_point(l,r);assert(p && std::abs(p->x)<1e-6 && std::abs(p->y-1.35)<1e-6 && std::abs(p->z+1.2)<1e-6);
        const float h=std::sqrt(.5f);l.orientation={0,h,0,h}; // 90 degrees yaw left: -Z becomes -X
        p=d2vr::head_locked_art_point(l,r);assert(p && std::abs(p->x+1.2)<1e-5 && std::abs(p->z)<1e-5);
        l.orientation={0,0,0,0};assert(!d2vr::head_locked_art_point(l,r));
    }
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
        // Supplied native artwork is composited in the actual two eye images;
        // no prompt quad is submitted, and original pixels outside it survive.
        auto native_art=texture(device.Get(),64,10,220,35,D3D11_BIND_SHADER_RESOURCE);
        const auto native_point=d2vr::Vec3{sim::control.head.position.x,sim::control.head.position.y,sim::control.head.position.z-1};
        const D3D11_BOX native_crop{0,0,0,64,64,1};
        assert(!xr.native_art_source(0,false,native_art.Get(),native_crop,0,xr.pose_epoch(),GetTickCount64(),native_point));
        assert(xr.native_art_source(0,false,native_art.Get(),native_crop,123,xr.pose_epoch(),GetTickCount64(),native_point,.3));
        frame();assert(xr.native_object_art_draws==2 && sim::summary.overlays.empty());
        assert(xr.native_art_presented(0,false));
        assert(pixel(device.Get(),context.Get(),sim::summary.images[0].Get(),32,32)==Pixel({10,220,35,255}));
        assert(pixel(device.Get(),context.Get(),sim::summary.images[1].Get(),32,32)==Pixel({10,220,35,255}));
        assert(pixel(device.Get(),context.Get(),sim::summary.images[0].Get())==Pixel({23,147,81,255}));
        assert(xr.native_art_source(0,false,native_art.Get(),native_crop,123,xr.pose_epoch()+1,GetTickCount64(),native_point,.3));
        frame();assert(xr.native_object_art_draws==2); // wrong epoch cannot reuse art
        assert(!xr.native_art_presented(0,false));
        xr.native_art_source(0,false,nullptr,{},0,0,0);++scenarios;
        // A distant objective can require a billboard wider than two metres
        // to retain a small bounded angular size. Both eyes still own placement.
        const auto distant_point=d2vr::Vec3{native_point.x,native_point.y,sim::control.head.position.z-100};
        const auto distant_rect=d2vr::native_art_rect(xr.tracking_views()->at(0),distant_point,8,1);
        assert(distant_rect && distant_rect->right-distant_rect->left>0 && distant_rect->right-distant_rect->left<.15f);
        assert(!d2vr::native_art_rect(xr.tracking_views()->at(0),distant_point,65,1));
        assert(xr.native_art_source(1,false,native_art.Get(),native_crop,124,xr.pose_epoch(),GetTickCount64(),distant_point,8));
        const auto object_draws=xr.native_object_art_draws;frame();
        assert(xr.native_object_art_draws==object_draws+2 && sim::summary.overlays.empty());
        assert(pixel(device.Get(),context.Get(),sim::summary.images[0].Get(),32,32)==Pixel({10,220,35,255}));
        assert(pixel(device.Get(),context.Get(),sim::summary.images[1].Get(),32,32)==Pixel({10,220,35,255}));
        assert(!xr.native_art_source(1,false,native_art.Get(),native_crop,124,xr.pose_epoch(),GetTickCount64(),distant_point,65));
        xr.native_art_source(1,false,nullptr,{},0,0,0);++scenarios;
        // Individual native health and mana pixels share the right watch at
        // their original relative pixel scale, with transparent spare rows.
        const auto resting_grips=sim::control.grips;
        sim::control.grips[0]={{0,-.70710678f,0,.70710678f},{-.1f,1.50f,-.45f}}; // inner (palm) wrist toward the eyes
        sim::control.grips[1]={{0,.70710678f,0,.70710678f},{.1f,1.50f,-.45f}};
        auto health_art=texture(device.Get(),64,180,20,25),mana_art=texture(device.Get(),64,15,40,210);
        const D3D11_BOX health_crop{0,0,0,32,48,1},mana_crop{0,0,0,16,24,1};
        assert(xr.native_art_source(1,true,health_art.Get(),health_crop,321,xr.pose_epoch(),GetTickCount64()));
        assert(xr.native_art_source(2,true,mana_art.Get(),mana_crop,322,xr.pose_epoch(),GetTickCount64()));
        assert(xr.native_art_source(0,true,native_art.Get(),native_crop,323,xr.pose_epoch(),GetTickCount64()));
        frame();assert(sim::summary.overlays.size()==2);
        const auto combined=sim::summary.overlays.front(); // health/mana (left watch) is submitted first
        assert(combined.subImage.imageRect.extent.width==48 && combined.subImage.imageRect.extent.height==48);
        auto combined_image=sim::chains.at(combined.subImage.swapchain)->textures.at(sim::chains.at(combined.subImage.swapchain)->current);
        assert(pixel(device.Get(),context.Get(),combined_image.Get(),1,1)==Pixel({180,20,25,255}));
        assert(pixel(device.Get(),context.Get(),combined_image.Get(),33,13)==Pixel({15,40,210,255}));
        assert(pixel(device.Get(),context.Get(),combined_image.Get(),33,1)==Pixel({0,0,0,0}));
        assert(std::abs(combined.size.width-.07f)<1e-5f && std::abs(combined.size.height-.07f)<1e-5f);
        assert(xr.native_art_source(2,true,mana_art.Get(),mana_crop,322,xr.pose_epoch()+1,GetTickCount64()));
        frame();const auto health_only=sim::summary.overlays.front();
        assert(health_only.subImage.imageRect.extent.width==32 && health_only.subImage.imageRect.extent.height==48);
        assert(std::abs(health_only.size.width-.07f)<1e-5f);
        sim::control.hands=2;frame();frame();assert(sim::summary.overlays.size()==1); // left tracking absent: only the right (health) watch
        sim::control.hands=3;sim::control.grips=resting_grips;frame();frame();assert(sim::summary.overlays.empty());
        for(unsigned slot=0;slot<3;++slot)xr.native_art_source(slot,true,nullptr,{},0,0,0);
        ++scenarios;
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
        // Face-height wheel/text overlay distinct stereo eyes, independent of resting hands.
        sim::control.input={};sim::control.input.active=true;
        xr.world_ui(nullptr,1);frame(false,false,true,false);
        assert(sim::summary.layer_type==XR_TYPE_COMPOSITION_LAYER_PROJECTION && !xr.last_menu_layer);
        xr.world_ui(left.Get(),1);frame(false,false,true,false);
        assert(xr.last_world_ui_layer && !xr.last_menu_layer && sim::summary.layer_type==XR_TYPE_COMPOSITION_LAYER_PROJECTION);
        assert(!sim::summary.overlays.empty());
        const auto dial_pose=sim::summary.overlays.front().pose;
        const auto full_wheel=sim::summary.overlays.front();
        assert(full_wheel.subImage.imageRect.offset.y==0 && full_wheel.subImage.imageRect.extent.height==64);
        assert(std::abs(full_wheel.size.width-full_wheel.size.height)<1e-5f); // entire square fixture, not a 16:9 centre crop
        assert(std::abs(dial_pose.position.y-sim::control.head.position.y)<1e-5f);
        // Wheel supports the same right-hand native cursor as readable pages (its sector selection runs through it).
        sim::control.aims[1]=sim::control.head;frame(false,false,true,false);
        assert(xr.native_menu_input.active && !xr.last_pointer_layer && xr.pointer_layers==0);
        sim::control.grips[0].position.x+=.2f;frame(false,false,true,false);
        assert(sim::summary.overlays.front().pose.position.x==dial_pose.position.x);
        xr.world_ui(left.Get(),3);frame(false,false,true,false);
        const auto text_pose=sim::summary.overlays.front().pose;
        assert(std::abs(text_pose.position.y-sim::control.head.position.y)<1e-5f);
        sim::control.grips[0].position.x+=.1f;frame(false,false,true,false);
        assert(sim::summary.overlays.front().pose.position.x==text_pose.position.x);
        // The black market shows the whole image (its controls sit outside the 16:9 band).
        xr.world_ui(left.Get(),3,true);frame(false,false,true,false);
        assert(sim::summary.overlays.front().subImage.imageRect.offset.y==0 && sim::summary.overlays.front().subImage.imageRect.extent.height==64);
        // ...held in the left hand like a note: .45 m, its bottom edge on the hand; the right-hand ray still points into it.
        const auto shop=sim::summary.overlays.front();const auto& hand=sim::control.grips[0].position;const auto head_aim=sim::control.aims[1];
        assert(std::abs(shop.size.width-.45f)<1e-5f && std::abs(shop.pose.position.x-hand.x)<1e-5f && std::abs(shop.pose.position.z-hand.z)<1e-5f);
        assert(std::abs(shop.pose.position.y-(hand.y+shop.size.height*.5f))<1e-5f);
        const auto back=d2vr::rotated({shop.pose.orientation.x,shop.pose.orientation.y,shop.pose.orientation.z,shop.pose.orientation.w},{0,0,.2});
        sim::control.aims[1]={shop.pose.orientation,{shop.pose.position.x+static_cast<float>(back.x),shop.pose.position.y+static_cast<float>(back.y),shop.pose.position.z+static_cast<float>(back.z)}};
        frame(false,false,true,false);
        assert(xr.native_menu_input.active && std::abs(xr.native_menu_input.u-.5)<.01 && std::abs(xr.native_menu_input.v-.5)<.01);
        sim::control.hands=2;frame(false,false,true,false);frame(false,false,true,false); // left hand lost: the 1 m panel ahead
        assert(std::abs(sim::summary.overlays.front().size.width-1.f)<1e-5f && std::abs(sim::summary.overlays.front().pose.position.y-sim::control.head.position.y)<1e-5f);
        sim::control.hands=3;sim::control.aims[1]=head_aim;
        xr.world_ui(nullptr,0);frame();++scenarios;
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
        // Settings remain pixels in the native menu stage, with the native navigation intact.
        assert(sim::summary.layer_type==XR_TYPE_COMPOSITION_LAYER_QUAD);
        assert(sim::summary.quad.subImage.imageRect.extent.width==64);
        // the menu panel's chain is the 36-row band (eye rows 14..49), so eye row y shows at y-14
        const auto native_pixel=pixel(device.Get(),context.Get(),sim::summary.images[0].Get());
        assert(native_pixel==Pixel({23,147,81,255}));
        assert(pixel(device.Get(),context.Get(),sim::summary.images[0].Get(),20,18)!=native_pixel);
        // The measured native right navigation column and top mission header stay untouched.
        assert(pixel(device.Get(),context.Get(),sim::summary.images[0].Get(),41,15)==native_pixel);
        assert(pixel(device.Get(),context.Get(),sim::summary.images[0].Get(),41,25)==native_pixel);
        assert(pixel(device.Get(),context.Get(),sim::summary.images[0].Get(),32,3)==native_pixel);
        sim::control.input.y=false;frame(false);sim::control.input.move_x=1;frame(false);
        assert(xr.take_resolution_request()==1.1);sim::control.input.move_x=0;frame(false);
        sim::control.input.b=true;frame(false);assert(!xr.settings_open());++scenarios;
        // Required setup opens on Pause, then real XR actions capture a measured pose.
        sim::control.input={};sim::control.input.active=true;
        sim::control.input.left_grip=sim::control.input.right_grip=1;
        sim::control.head={{0,0,0,1},{0,1.65f,0}};
        sim::control.grips={{{{0,0,0,1},{-.75f,1.45f,0}},{{0,0,0,1},{.85f,1.45f,0}}}};
        xr.avatar_measurements({1.6,.38});
        xr.avatar_runtime_status(d2vr::AvatarFitRuntimeStatus::recalibration_required);
        frame(false);assert(xr.settings_open() && !xr.controller_input.active);
        sim::control.input.a=true;frame(false);sim::control.input.a=false;
        for(unsigned sample=0;sample<24;++sample) {Sleep(50);frame(false);}
        const auto calibrated=xr.avatar_calibration();assert(calibrated);
        assert(std::abs(calibrated->eye_to_shoulder_metres-.2)<.001);
        assert(std::abs(calibrated->shoulder_span_metres-.38)<.001 && !calibrated->shoulder_span_measured && calibrated->arm_lengths_measured);
        assert(calibrated->arm_reach_metres[0] && calibrated->arm_reach_metres[1]);
        assert(*calibrated->arm_reach_metres[0]>.5 && *calibrated->arm_reach_metres[1]>*calibrated->arm_reach_metres[0]+.08);
        assert(!calibrated->standing_eye_height_metres && !xr.calibration_floor_y());
        assert(!calibrated->torso_width_metres && !calibrated->torso_width_measured);
        xr.avatar_runtime_status(d2vr::AvatarFitRuntimeStatus::active);xr.suspend_avatar();
        assert(xr.avatar_calibration() && xr.avatar_calibration_state()==d2vr::AvatarCalibrationState::ready);
        assert(xr.avatar_runtime_status()==d2vr::AvatarFitRuntimeStatus::verifying);
        xr.avatar_measurements({1.6,.38});xr.avatar_runtime_status(d2vr::AvatarFitRuntimeStatus::active);
        sim::control.input.b=true;frame(false);sim::control.input.b=false;frame(false);
        sim::control.input.b=true;frame(false);sim::control.input.b=false;frame(false);assert(!xr.settings_open());
        xr.avatar_runtime_status(d2vr::AvatarFitRuntimeStatus::recalibration_required);frame(false);
        assert(xr.settings_open());sim::control.input.a=true;frame(false);sim::control.input.a=false;
        assert(xr.avatar_calibration()); // A cancelled retry keeps the previous valid fit.
        frame(); // gameplay departure cancels either capture stage
        for(unsigned sample=0;sample<24;++sample) {Sleep(50);frame();}
        assert(xr.avatar_calibration() && !xr.settings_open());
        assert(xr.avatar_calibration()->palm_span_metres==calibrated->palm_span_metres);
        xr.reset_avatar();assert(!xr.avatar_calibration());
        xr.avatar_identity_changed();
        xr.avatar_measurements({1.6,.38});
        sim::control.input={};sim::control.input.active=true;frame(false);frame(false);
        sim::control.input.a=true;frame(false);sim::control.input.a=false;frame(false);
        sim::push_state(XR_SESSION_STATE_STOPPING);frame(false);
        sim::push_state(XR_SESSION_STATE_READY);frame(false);sim::push_state(XR_SESSION_STATE_FOCUSED);frame(false);
        for(unsigned sample=0;sample<24;++sample) {Sleep(50);frame();}
        // The player's body survives an avatar identity change: refit without a new pose.
        assert(xr.avatar_calibration() && !xr.settings_open());++scenarios;
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
        assert(sim::summary.protocol_errors==0 && sim::summary.chain_ops_in_frame==0 && xr.swapchain_creates_in_frame==0 && sim::summary.subrect_layers==0 && xr.subrect_layers==0);
    }
    assert(!sim::instantiated && !sim::session_created && sim::spaces.empty() && sim::chains.empty());
    assert(sim::summary.protocol_errors==0 && sim::summary.subrect_layers==0 && sim::summary.waits==sim::summary.ends);
    std::ofstream report("artifacts/simulated-vr-check.json");
    report << "{\"simulated_vr\":true,\"bridge\":\"production GameXr\",\"device\":\"D3D11 WARP\",\"scenarios\":" << scenarios
           << ",\"xr_frames\":" << sim::summary.ends << ",\"protocol_errors\":0,\"resource_cleanup_verified\":true,\"native_game_interaction_verified\":false,\"headset_acceptance\":false}\n";
    assert(report.good());
    std::cout << "PASS: production XR bridge simulated stereo pixels, menu ray/trigger, black market held in the left hand, three focus returns, authored camera, input, settings, resize, suppressed poses, reference changes, 1000-frame soak and resource cleanup\n";
}
