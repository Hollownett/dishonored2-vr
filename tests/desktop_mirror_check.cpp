#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "../src/desktop_mirror.hpp"
#include "../src/render_size.hpp"
#include <iostream>
#include <vector>
#include <cmath>

using Microsoft::WRL::ComPtr;
void require(bool condition,const char* message) {if(!condition) throw std::runtime_error(message);}
ComPtr<ID3D11Texture2D> solid(ID3D11Device* device,UINT w,UINT h,DXGI_FORMAT format,UINT bindings,
                              std::byte r,std::byte g,std::byte b) {
    std::vector<std::byte> pixels(static_cast<std::size_t>(w)*h*4);
    for(std::size_t i=0;i<pixels.size();i+=4) {pixels[i]=r;pixels[i+1]=g;pixels[i+2]=b;pixels[i+3]=std::byte{255};}
    D3D11_TEXTURE2D_DESC desc{};desc.Width=w;desc.Height=h;desc.MipLevels=1;desc.ArraySize=1;
    desc.Format=format;desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=bindings;
    D3D11_SUBRESOURCE_DATA data{};data.pSysMem=pixels.data();data.SysMemPitch=w*4;
    ComPtr<ID3D11Texture2D> result;
    require(SUCCEEDED(device->CreateTexture2D(&desc,&data,&result)),"Test texture allocation failed");return result;
}
int pixel(const d2vr::RgbaPixels& image,UINT x,UINT y,unsigned component) {
    return std::to_integer<int>(image.bytes[(static_cast<std::size_t>(y)*image.width+x)*4+component]);
}
int main() {
    try {
        const auto both=d2vr::mirror_layout(2,2496,2688,2496,2688);
        require(both[0] && both[1],"Both-eye layout is missing an eye");
        require(both[0]->x==0.f && both[0]->y==672.f && both[0]->width==1248.f && both[0]->height==1344.f,"Left half layout differs");
        require(both[1]->x==1248.f && both[1]->y==672.f && both[1]->width==1248.f && both[1]->height==1344.f,"Right half layout differs");
        const auto right=d2vr::mirror_layout(0,2496,2688,2496,2688);
        require(!right[0] && right[1],"Mode 0 did not select the right eye only");
        require(right[1]->x==0.f && right[1]->y==0.f && right[1]->width==2496.f && right[1]->height==2688.f,
            "Mode 0 did not fill the target");
        const auto left=d2vr::mirror_layout(1,2496,2688,2496,2688);
        require(left[0] && !left[1],"Mode 1 did not select the left eye only");
        require(left[0]->x==0.f && left[0]->y==0.f && left[0]->width==2496.f && left[0]->height==2688.f,
            "Mode 1 did not fill the target");
        const auto pillar=d2vr::mirror_layout(1,1920,1080,2496,2688);
        require(pillar[0] && !pillar[1],"Pillarbox layout is missing the left eye");
        require(pillar[0]->y==0.f && pillar[0]->height==1080.f,"Pillarbox did not fit the eye height");
        require(std::abs(pillar[0]->width-1003.f)<=1.f,"Pillarbox width is not the aspect fit");
        require(std::abs(pillar[0]->x-(1920.f-pillar[0]->width)*.5f)<=1.f,"Pillarbox is not centred");
        require(std::abs(pillar[0]->width/pillar[0]->height-2496.f/2688.f)<.002f,"Pillarbox aspect was not preserved");
        for(const auto layout:{d2vr::mirror_layout(2,0,1080,2496,2688),d2vr::mirror_layout(2,1920,0,2496,2688),
            d2vr::mirror_layout(2,1920,1080,0,2688),d2vr::mirror_layout(2,1920,1080,2496,0),
            d2vr::mirror_layout(3,1920,1080,2496,2688),d2vr::mirror_layout(-1,1920,1080,2496,2688)})
            require(!layout[0] && !layout[1],"Invalid layout request was accepted");
        ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
        require(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,
            D3D11_SDK_VERSION,&device,nullptr,&context)),"Cannot create WARP device");
        d2vr::DesktopMirror mirror(device.Get());
        const D3D11_VIEWPORT native{3,5,77,33,.2f,.8f};context->RSSetViewports(1,&native);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
        // Swapchain-style eye images carry no shader resource binding.
        auto red=solid(device.Get(),32,16,DXGI_FORMAT_R8G8B8A8_UNORM,0,std::byte{255},std::byte{0},std::byte{0});
        auto green=solid(device.Get(),32,16,DXGI_FORMAT_R8G8B8A8_UNORM,0,std::byte{0},std::byte{255},std::byte{0});
        auto blue=solid(device.Get(),32,16,DXGI_FORMAT_R8G8B8A8_UNORM,D3D11_BIND_SHADER_RESOURCE,
            std::byte{0},std::byte{0},std::byte{255});
        auto backbuffer=solid(device.Get(),64,32,DXGI_FORMAT_R8G8B8A8_UNORM,D3D11_BIND_RENDER_TARGET,
            std::byte{9},std::byte{9},std::byte{9});
        const auto layout=d2vr::mirror_layout(2,64,32,32,16);
        require(layout[0] && layout[0]->x==0.f && layout[0]->y==8.f && layout[0]->width==32.f && layout[0]->height==16.f,
            "Small both-eye layout differs");
        mirror.begin(64,32,DXGI_FORMAT_R8G8B8A8_UNORM);
        mirror.draw(red.Get(),*layout[0]);
        mirror.draw(green.Get(),*layout[1]);
        mirror.end(backbuffer.Get());
        const auto mirrored=d2vr::readback_rgba(backbuffer.Get());
        require(mirrored.width==64 && mirrored.height==32,"Mirror target size differs");
        require(pixel(mirrored,16,16,0)>=250 && pixel(mirrored,16,16,1)<=5 && pixel(mirrored,16,16,2)<=5,
            "The left eye was not drawn red in the left half");
        require(pixel(mirrored,48,16,0)<=5 && pixel(mirrored,48,16,1)>=250 && pixel(mirrored,48,16,2)<=5,
            "The right eye was not drawn green in the right half");
        require(pixel(mirrored,31,16,0)>=250 && pixel(mirrored,32,16,1)>=250,"The two eyes do not meet at the centre seam");
        for(const auto sample:std::array<std::array<UINT,2>,3>{{{16,4},{48,27},{16,31}}})
            for(unsigned component=0;component<3;++component)
                require(pixel(mirrored,sample[0],sample[1],component)==0,"Letterbox pixels are not black");
        require(pixel(mirrored,16,16,3)==255,"Drawn eye alpha was not preserved");
        // A readable eye is sampled directly instead of through the copy.
        mirror.begin(64,32,DXGI_FORMAT_R8G8B8A8_UNORM);
        mirror.draw(blue.Get(),*d2vr::mirror_layout(1,64,32,32,16)[0]);
        mirror.end(backbuffer.Get());
        const auto single=d2vr::readback_rgba(backbuffer.Get());
        require(pixel(single,32,16,2)>=250 && pixel(single,32,16,0)<=5 && pixel(single,32,16,1)<=5,
            "A direct eye view was not drawn");
        require(pixel(single,16,16,2)>=250,"Mode 1 did not fill the whole target");
        // sRGB target: the eye SRV and the canvas RTV must be the same curve.
        auto grey=solid(device.Get(),32,16,DXGI_FORMAT_R8G8B8A8_UNORM,0,std::byte{128},std::byte{128},std::byte{128});
        auto srgb_target=solid(device.Get(),64,32,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,D3D11_BIND_RENDER_TARGET,
            std::byte{0},std::byte{0},std::byte{0});
        mirror.begin(64,32,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
        mirror.draw(grey.Get(),*layout[0]);
        mirror.draw(grey.Get(),*layout[1]);
        mirror.end(srgb_target.Get());
        const auto gamma=d2vr::readback_rgba(srgb_target.Get());
        require(std::abs(pixel(gamma,16,16,0)-128)<=2 && std::abs(pixel(gamma,48,16,1)-128)<=2,
            "The sRGB mirror did not preserve mid grey");
        require(pixel(gamma,16,4,0)==0,"The sRGB letterbox is not black");
        // reset() must drop the canvas so a resized target can be composed again.
        mirror.reset();
        mirror.begin(64,32,DXGI_FORMAT_R8G8B8A8_UNORM);
        mirror.draw(red.Get(),*d2vr::mirror_layout(0,64,32,32,16)[1]);
        mirror.end(backbuffer.Get());
        const auto recreated=d2vr::readback_rgba(backbuffer.Get());
        require(pixel(recreated,32,16,0)>=250 && pixel(recreated,32,16,1)<=5,"The mirrored canvas was not recreated after reset");
        D3D11_VIEWPORT restored{};UINT count=1;context->RSGetViewports(&count,&restored);
        D3D11_PRIMITIVE_TOPOLOGY topology{};context->IAGetPrimitiveTopology(&topology);
        require(restored.TopLeftX==3 && restored.TopLeftY==5 && restored.Width==77 && topology==D3D11_PRIMITIVE_TOPOLOGY_LINELIST,
            "Game graphics state was not restored");
        std::cout << "PASS: desktop mirror layout fitting/centring, both-eye composition, letterbox, direct and copied eye reads, sRGB mid grey, reset recreation and D3D11 state restoration\n";
        return 0;
    } catch(const std::exception& error) {std::cerr << error.what() << '\n';return 1;}
}
