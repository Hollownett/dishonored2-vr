#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#pragma warning(disable:4706) // GameXr's existing frame gates intentionally assign their diagnostic flags in conditions.
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include "../src/game_xr.hpp"
#include "../src/simulated_xr.hpp"
#include <cassert>
#include <iostream>
using Microsoft::WRL::ComPtr;
namespace sim=d2vr::simxr;
int main() {
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level{};
    assert(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context)));
    sim::configure(device.Get());
    D3D11_TEXTURE2D_DESC desc{};desc.Width=desc.Height=64;desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;
    desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> left,right;
    assert(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&left)) && SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&right)));
    d2vr::GameXr xr(device.Get(),64,64,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,nullptr,true,true,true,true,1,{}, {},true,&sim::dispatch);
    sim::control.input.active=true;
    auto frame=[&] {
        if(xr.tracking_views()) {const auto& views=*xr.tracking_views();xr.record_rendered_eye(0,views[0].fov);xr.record_rendered_eye(1,views[1].fov);}
        xr.present(left.Get(),right.Get(),1.6f,1.6f,false,false,false);
    };
    frame();frame();xr.world_ui(left.Get(),2);frame();
    assert(xr.last_world_ui_layer && xr.world_ui_in_hand()); // default retains accepted hand-facing note
    xr.enable_note_tablet(true);xr.alternate_frame=23;
    d2vr::NotePanelReceipt note{{{-.12,1.35,-.45},{0,0,std::sin(.2),std::cos(.2)}},.45,.253125,
        GetTickCount64(),xr.pose_epoch(),xr.reference_epoch(),23};
    xr.note_panel(note);frame();assert(!xr.world_ui_in_hand()); // no matching hand skin: readable fallback
    auto triangles=std::make_shared<const std::vector<d2vr::Vec3>>(std::initializer_list<d2vr::Vec3>{{-1,1,-1},{1,1,-1},{0,2,-1}});
    const d2vr::NoteHandReceipt hand{triangles,note.tick,note.pose_epoch,note.reference_epoch,note.frame};
    xr.note_hands({hand,hand});frame();
    assert(xr.world_ui_in_hand() && !xr.native_menu_input.active && sim::summary.overlays.empty());
    assert(xr.note_eye_draws==2); // the page is composed into BOTH actual submitted eyes, never an over-hands quad
    sim::control.input.turn_y=.7f;frame();
    assert(xr.note_scroll_axis()==.7f && !xr.native_menu_input.active);
    xr.alternate_frame=24;frame();assert(!xr.world_ui_in_hand());
    assert(xr.note_scroll_axis()==.7f); // the fallback page can still scroll
    sim::control.hands=2;frame();assert(!xr.world_ui_in_hand());
    xr.enable_note_tablet(false);frame();assert(xr.note_scroll_axis()==0); // maps/unknown subtype keep their native axes
    xr.world_ui(nullptr,0);frame();assert(xr.note_scroll_axis()==0);
    assert(!sim::summary.protocol_errors);
    std::cout<<"PASS: GameXr composes notes into both eyes with hand masks, rejects missing/stale receipts, keeps fallback scrolling and leaves map axes native\n";
}
