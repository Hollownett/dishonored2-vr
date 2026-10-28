#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#pragma warning(disable:4706) // Existing GameXr diagnostic frame gates assign flags in conditions.
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include "../src/game_xr.hpp"
#include "../src/simulated_xr.hpp"
#include "../src/render_size.hpp"
#include <cassert>
#include <fstream>
#include <iostream>
#include <sstream>

using Microsoft::WRL::ComPtr;
namespace sim=d2vr::simxr;

int main() {
    std::ifstream guide("docs/VR-CONTROLS.md",std::ios::binary);assert(guide);
    std::ostringstream text;text<<guide.rdbuf();d2vr::controls_guide_markdown()=text.str();
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level{};
    assert(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context)));
    sim::configure(device.Get());
    D3D11_TEXTURE2D_DESC desc{};desc.Width=desc.Height=64;desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;
    desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> left,right;
    assert(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&left)) && SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&right)));
    {
        d2vr::GameXr xr(device.Get(),64,64,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,nullptr,true,true,false,true,1,{}, {},false,&sim::dispatch);
        xr.legacy_hold_y_settings=true;
        auto frame=[&](bool gameplay=true) {
            if(xr.tracking_views()) {const auto& views=*xr.tracking_views();xr.record_rendered_eye(0,views[0].fov);xr.record_rendered_eye(1,views[1].fov);}
            xr.present(left.Get(),right.Get(),1.6f,1.6f,gameplay,false,true);
        };
        auto guide_submitted=[&] {
            return std::any_of(sim::summary.overlays.begin(),sim::summary.overlays.end(),[](const auto& overlay) {
                return overlay.subImage.imageRect.extent.width==1920 && overlay.subImage.imageRect.extent.height==1200;
            });
        };
        auto guide_pixels=[&] {
            for(const auto& overlay:sim::summary.overlays) if(overlay.subImage.imageRect.extent.width==1920 && overlay.subImage.imageRect.extent.height==1200) {
                const auto& chain=*sim::chains.at(overlay.subImage.swapchain);
                return d2vr::readback_rgba(chain.textures[chain.current].Get()).bytes;
            }
            assert(false && "The actual guide quad must be submitted");return std::vector<std::byte>{};
        };
        frame();frame();assert(xr.last_layer_submitted && !xr.guide_open());
        xr.toggle_guide();frame();assert(xr.guide_open() && xr.hint_layers>0);
        const auto first=guide_pixels();assert(first.size()==1920*1200*4);
        sim::control.input.turn_x=1;sim::control.input.turn_y=.7f;sim::control.input.right_click=true;frame();
        assert(xr.controller_input.turn_x==0 && xr.controller_input.turn_y==0 && !xr.controller_input.right_click);
        const auto second=guide_pixels();assert(second!=first);
        frame();assert(guide_pixels()==second); // held stick must not repeat
        xr.toggle_guide();frame();assert(!xr.guide_open() && !guide_submitted() && xr.guide_stick_blocked());
        assert(xr.controller_input.turn_x==0 && !xr.controller_input.right_click);
        sim::control.input.turn_x=sim::control.input.turn_y=0;sim::control.input.right_click=false;frame();
        assert(!xr.guide_stick_blocked());

        // Open the real fallback gameplay settings through its existing hold-Y route, then activate row 6.
        sim::control.input.y=true;frame();Sleep(650);frame();assert(xr.settings_open());
        sim::control.input.y=false;frame();
        for(int row=0;row<6;++row) {sim::control.input.move_y=-1;frame();sim::control.input.move_y=0;frame();}
        sim::control.input.a=true;frame();
        assert(xr.guide_open());guide_pixels();
        sim::control.input.a=false;frame();assert(xr.guide_open());guide_pixels();
        // B closes settings; the gameplay guide remains visible on subsequent gameplay frames.
        sim::control.input.b=true;frame();sim::control.input.b=false;frame();
        assert(!xr.settings_open() && xr.guide_open());guide_pixels();
        xr.toggle_guide();frame();assert(!xr.guide_open() && !guide_submitted());

        // A guide opened over Pause closes when gameplay resumes.
        frame(false);xr.toggle_guide(true);frame(false);assert(xr.guide_open());guide_pixels();
        frame();assert(!xr.guide_open() && !guide_submitted());
        assert(!sim::summary.protocol_errors && !sim::summary.chain_ops_in_frame);
    }
    assert(!sim::summary.protocol_errors);
    std::cout<<"PASS: actual GameXr guide quad, page pixels, input consumption/debounce, release gate, gameplay fallback row 6 persistence, and pause return\n";
}
