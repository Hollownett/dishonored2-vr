#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include "../src/game_xr.hpp"
#include "../src/simulated_xr.hpp"
#include <cassert>
#include <cstdlib>
#include <iostream>

// v776 (headset v775: "the hands must still be rendered while the wheel is open"): the dual-wield hand wheel at its hand is drawn INTO both eyes like a note page
// (NoteEyeArt ring mode), behind the hands in front of it, with no quad layer; the head-front wheel (knob off) keeps its quad; a fault in the ring draw hands the
// wheel back to the quad layer. Drives the real GameXr::present against the simulated OpenXR runtime (the same harness as game_xr_simulation_check).
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
static Pixel pixel(ID3D11Device* device,ID3D11DeviceContext* context,ID3D11Texture2D* source,unsigned x=0,unsigned y=0) {
    D3D11_TEXTURE2D_DESC desc{};source->GetDesc(&desc);desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;assert(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&staging)));context->CopyResource(staging.Get(),source);
    D3D11_MAPPED_SUBRESOURCE mapped{};assert(SUCCEEDED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped)));
    Pixel out{};std::memcpy(out.data(),static_cast<const unsigned char*>(mapped.pData)+y*mapped.RowPitch+x*4,4);context->Unmap(staging.Get(),0);return out;
}
int main() {
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level{};
    assert(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context)));
    sim::configure(device.Get());
    auto left=texture(device.Get(),64,23,147,81),right=texture(device.Get(),64,98,34,231);
    d2vr::GameXr xr(device.Get(),64,64,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,nullptr,true,true,true,true,1,{}, {},true,&sim::dispatch);
    auto frame=[&](bool gameplay=true,bool authored=false,bool complete=true,bool settings_available=true) {
        if(xr.tracking_views() && complete) {
            const auto& views=*xr.tracking_views();xr.record_rendered_eye(0,views[0].fov);xr.record_rendered_eye(1,views[1].fov);
        }
        xr.present(left.Get(),right.Get(),1.6f,1.6f,gameplay,authored,settings_available);
    };
    frame();frame();assert(xr.last_layer_submitted && xr.last_tracked_pair);
    // The wheel movie as the game's capture is: raw gamma bytes in typeless storage (opaque grey 200).
    D3D11_TEXTURE2D_DESC wd{};wd.Width=wd.Height=64;wd.MipLevels=wd.ArraySize=wd.SampleDesc.Count=1;wd.Format=DXGI_FORMAT_R8G8B8A8_TYPELESS;wd.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    std::vector<unsigned char> grey(64*64*4,200);for(std::size_t i=3;i<grey.size();i+=4)grey[i]=255;ComPtr<ID3D11Texture2D> wheel_art;D3D11_SUBRESOURCE_DATA wdata{grey.data(),64*4,0};
    assert(SUCCEEDED(device->CreateTexture2D(&wd,&wdata,&wheel_art)));
    sim::control.input={};sim::control.input.active=true;
    sim::control.grips[0]={{0,0,0,1},{0,1.65f,-.5f}};sim::control.aims[0]=sim::control.grips[0];sim::control.hands=3; // the left hand straight ahead of the eyes, aiming forward: its ring covers the middle of the view
    frame();                                                                  // a plain gameplay frame: the eyes' own colours
    const std::array<Pixel,2> base{pixel(device.Get(),context.Get(),sim::summary.images[0].Get(),32,32),pixel(device.Get(),context.Get(),sim::summary.images[1].Get(),32,32)};
    assert(base[0]==Pixel({23,147,81,255}) && base[1]==Pixel({98,34,231,255}));
    // Knob off: the head-front wheel is the quad layer, exactly as before.
    xr.world_ui(wheel_art.Get(),1,false,930);frame(false,false,true,false);
    assert(xr.last_world_ui_layer && sim::summary.overlays.size()==1 && xr.wheel_eye_draws==0 && !xr.wheel_eye_ready());
    // Dual wield with build/wheel-quad (the headset A/B): the hand wheel keeps its quad layer.
    xr.hand_wheel_mode=true;xr.set_wheel_hand(0);xr.wheel_eye_art_enabled=false;
    xr.world_ui(wheel_art.Get(),1,false,930);frame(false,false,true,false);
    assert(xr.last_world_ui_layer && sim::summary.overlays.size()==1 && xr.wheel_eye_draws==0 && !xr.wheel_eye_ready());
    // Dual wield: the ring at the left hand goes into both eyes, no quad layer.
    xr.wheel_eye_art_enabled=true;
    xr.world_ui(wheel_art.Get(),1,false,931);frame(false,false,true,false);
    assert(xr.wheel_eye_draws==2 && xr.wheel_eye_occluded==0 && xr.wheel_eye_failures==0);           // both eyes, no hand given
    assert(!xr.last_world_ui_layer && sim::summary.overlays.empty());                               // and no quad layer
    for(unsigned eye=0;eye<2;++eye) {
        const auto ring=pixel(device.Get(),context.Get(),sim::summary.images[eye].Get(),32,32);
        assert(ring!=base[eye] && std::abs(static_cast<int>(ring[0])-200)<=2 && ring[3]==255);      // the ring is in the eye image itself
    }
    const auto hand_receipt=[&](std::uint64_t frame_number,double z) {
        auto triangles=std::make_shared<std::vector<d2vr::Vec3>>(std::initializer_list<d2vr::Vec3>{{-.2,1.5,z},{.2,1.5,z},{0,1.9,z}});
        return d2vr::NoteHandReceipt{triangles,GetTickCount64(),xr.pose_epoch(),xr.reference_epoch(),frame_number,1};
    };
    xr.alternate_frame=40;xr.accept_wheel_hands({hand_receipt(40,-.3),hand_receipt(40,-.3)});          // the hands of this very frame, in front of the ring
    xr.world_ui(wheel_art.Get(),1,false,932);frame(false,false,true,false);
    assert(xr.wheel_eye_draws==4 && xr.wheel_eye_occluded==2 && xr.wheel_eye_grace==0);
    for(unsigned eye=0;eye<2;++eye) assert(pixel(device.Get(),context.Get(),sim::summary.images[eye].Get(),32,32)==base[eye]); // the hand is over the ring
    xr.accept_wheel_hands({hand_receipt(40,-.9),hand_receipt(40,-.9)});                                // the same frame, the hands behind the ring: the ring is over them
    xr.world_ui(wheel_art.Get(),1,false,933);frame(false,false,true,false);
    assert(xr.wheel_eye_draws==6 && xr.wheel_eye_occluded==4);
    for(unsigned eye=0;eye<2;++eye) assert(pixel(device.Get(),context.Get(),sim::summary.images[eye].Get(),32,32)!=base[eye]);
    xr.alternate_frame=41;xr.accept_wheel_hands({hand_receipt(40,-.3),hand_receipt(40,-.3)});          // a frame whose hands are missing keeps the last frame's (no flicker)
    xr.world_ui(wheel_art.Get(),1,false,934);frame(false,false,true,false);
    assert(xr.wheel_eye_draws==8 && xr.wheel_eye_occluded==6 && xr.wheel_eye_grace==2);
    for(unsigned eye=0;eye<2;++eye) assert(pixel(device.Get(),context.Get(),sim::summary.images[eye].Get(),32,32)==base[eye]);
    xr.alternate_frame=42;xr.accept_wheel_hands({hand_receipt(40,-.3),hand_receipt(40,-.3)});          // two frames old: no longer a hand of this frame
    xr.world_ui(wheel_art.Get(),1,false,935);frame(false,false,true,false);
    assert(xr.wheel_eye_draws==10 && xr.wheel_eye_occluded==6 && xr.wheel_eye_grace==2 && xr.wheel_eye_failures==0);
    xr.alternate_frame=50;auto broken=hand_receipt(50,-.3);                                            // a hand mesh the projection refuses
    broken.triangles=std::make_shared<std::vector<d2vr::Vec3>>(std::initializer_list<d2vr::Vec3>{{static_cast<double>(NAN),1.5,-.3},{.2,1.5,-.3},{0,1.9,-.3}});
    xr.accept_wheel_hands({broken,broken});
    xr.world_ui(wheel_art.Get(),1,false,936);frame(false,false,true,false);
    assert(xr.wheel_eye_failures==1 && xr.last_world_ui_layer && sim::summary.overlays.size()==1);     // the eye art disabled itself: the quad layer shows the wheel again
    xr.world_ui(wheel_art.Get(),1,false,937);frame(false,false,true,false);
    assert(xr.wheel_eye_draws==10 && xr.last_world_ui_layer && xr.wheel_eye_failures==1);              // and stays the quad; the session keeps presenting
    assert(xr.last_layer_submitted && !xr.wheel_eye_ready());
    std::cout<<"PASS: hand wheel in both eyes without a quad, behind/over the hands, one frame of hand memory, quad fallback on a fault, head-front wheel unchanged\n";
}
