#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "../src/note_eye_art.hpp"
#include "../src/render_size.hpp"
#include <cassert>
#include <iostream>
using Microsoft::WRL::ComPtr;
int main() {
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level;
    assert(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context)));
    auto texture=[&](unsigned width,unsigned height,std::array<unsigned char,4> color) {
        std::vector<unsigned char> pixels(width*height*4);for(unsigned i=0;i<width*height;++i)std::copy(color.begin(),color.end(),pixels.begin()+i*4);
        D3D11_TEXTURE2D_DESC d{};d.Width=width;d.Height=height;d.MipLevels=d.ArraySize=d.SampleDesc.Count=1;d.Format=DXGI_FORMAT_R8G8B8A8_UNORM;d.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA data{pixels.data(),width*4,0};ComPtr<ID3D11Texture2D> result;assert(SUCCEEDED(device->CreateTexture2D(&d,&data,&result)));return result;
    };
    const auto hand=[](double left,double right,double depth) {
        auto p=std::make_shared<std::vector<d2vr::Vec3>>(std::initializer_list<d2vr::Vec3>{{left,-.2,depth},{right,-.2,depth},{left,.2,depth},{left,.2,depth},{right,-.2,depth},{right,.2,depth}});return p;
    };
    std::array<d2vr::NoteHandReceipt,2> hands{{{hand(-.3,-.05,-.5),100,1,2,3},{hand(.05,.3,-1.5),100,1,2,3}}};
    assert(hands[0].current(100,1,2,3));assert(!hands[0].current(201,1,2,3));assert(!hands[0].current(100,2,2,3));assert(!hands[0].current(100,1,3,3));assert(!hands[0].current(100,1,2,4));
    d2vr::NoteEyeArt compositor(device.Get());d2vr::NotePanelReceipt panel{{{0,0,-1},{0,0,0,1}},.8,.8,100,1,2,3};
    auto art=texture(8,8,{220,15,30,255});XrView view{XR_TYPE_VIEW};view.pose.orientation.w=1;view.fov={-.78539816f,.78539816f,.78539816f,-.78539816f};
    auto pixel=[](const d2vr::RgbaPixels& image,unsigned x,unsigned y){const auto i=(y*image.width+x)*4;return std::array<unsigned,4>{std::to_integer<unsigned>(image.bytes[i]),std::to_integer<unsigned>(image.bytes[i+1]),std::to_integer<unsigned>(image.bytes[i+2]),std::to_integer<unsigned>(image.bytes[i+3])};};
    for(float eye_x:{-.032f,.032f}) {
        auto eye=texture(128,128,{10,180,40,255});view.pose.position.x=eye_x;
        compositor.draw(eye.Get(),art.Get(),DXGI_FORMAT_R8G8B8A8_UNORM,{0,0,0,8,8,1},view,panel,hands);
        const auto image=d2vr::readback_rgba(eye.Get());
        assert((pixel(image,48,64)==std::array<unsigned,4>{10,180,40,255})); // front hand is never painted over
        assert((pixel(image,76,64)==std::array<unsigned,4>{220,15,30,255})); // page covers the hand behind it
        assert((pixel(image,8,8)==std::array<unsigned,4>{10,180,40,255}));
    }
    // Premultiplied transparency preserves the native eye, including fingers.
    auto clear_art=texture(8,8,{0,0,0,0}),eye=texture(128,128,{10,180,40,255});
    compositor.draw(eye.Get(),clear_art.Get(),DXGI_FORMAT_R8G8B8A8_UNORM,{0,0,0,8,8,1},view,panel,hands);
    assert((pixel(d2vr::readback_rgba(eye.Get()),76,64)==std::array<unsigned,4>{10,180,40,255}));
    bool rejected=false;hands[1].triangles.reset();try{compositor.draw(eye.Get(),art.Get(),DXGI_FORMAT_R8G8B8A8_UNORM,{0,0,0,8,8,1},view,panel,hands);}catch(const std::invalid_argument&){rejected=true;}assert(rejected);
    std::cout<<"note eye art: stereo front/back hand occlusion, alpha and receipt rejection passed\n";
}
