#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "../src/eye_sharpen.hpp"
#include "../src/eye_antialiasing.hpp"
#include "../src/render_size.hpp"
#include <iostream>
#include <vector>
#include <cstdlib>
#include <cmath>

using Microsoft::WRL::ComPtr;
void require(bool condition,const char* message) {if(!condition) throw std::runtime_error(message);}
ComPtr<ID3D11Texture2D> source_texture(ID3D11Device* device,UINT w,UINT h,const std::vector<std::byte>& pixels,
                                       DXGI_FORMAT format,UINT bindings) {
    require(pixels.size()==static_cast<std::size_t>(w)*h*4,"Input byte count differs");
    D3D11_TEXTURE2D_DESC desc{};desc.Width=w;desc.Height=h;desc.MipLevels=1;desc.ArraySize=1;
    desc.Format=format;desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=bindings;
    D3D11_SUBRESOURCE_DATA data{};data.pSysMem=pixels.data();data.SysMemPitch=w*4;
    ComPtr<ID3D11Texture2D> result;
    require(SUCCEEDED(device->CreateTexture2D(&desc,&data,&result)),"Test source allocation failed");return result;
}
// A soft vertical edge: 64 flat, a four pixel ramp 64->192, then 192 flat.
std::vector<std::byte> edge_image(UINT width,UINT height) {
    constexpr unsigned ramp[4]{64,107,149,192};
    std::vector<std::byte> pixels(static_cast<std::size_t>(width)*height*4);
    for(UINT y=0;y<height;++y) for(UINT x=0;x<width;++x) {
        const auto shade=std::byte(x<28?64:(x<32?ramp[x-28]:192));
        const auto i=(static_cast<std::size_t>(y)*width+x)*4;
        pixels[i]=pixels[i+1]=pixels[i+2]=shade;pixels[i+3]=std::byte{255};
    }
    return pixels;
}
int channel(const std::vector<std::byte>& bytes,UINT width,UINT x,UINT y,unsigned component=0) {
    return std::to_integer<int>(bytes[(static_cast<std::size_t>(y)*width+x)*4+component]);
}
int main() {
    try {
        ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
        require(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,
            D3D11_SDK_VERSION,&device,nullptr,&context)),"Cannot create WARP device");
        d2vr::EyeSharpen sharpen(device.Get());
        const D3D11_VIEWPORT native{7,11,123,99,.1f,.9f};context->RSSetViewports(1,&native);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
        constexpr UINT width=64,height=16;
        const auto pixels=edge_image(width,height);
        auto srgb=source_texture(device.Get(),width,height,pixels,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,D3D11_BIND_SHADER_RESOURCE);
        require(sharpen.apply(srgb.Get(),0.f)==srgb.Get(),"Zero sharpness did not return the source unchanged");
        require(sharpen.apply(srgb.Get(),-1.f)==srgb.Get(),"Negative sharpness did not return the source unchanged");
        const auto sharpened=d2vr::readback_rgba(sharpen.apply(srgb.Get(),1.f));
        require(d2vr::readback_rgba(srgb.Get()).bytes==pixels,"Sharpening modified its source");
        require(sharpened.width==width && sharpened.height==height,"Sharpened size differs from the source");
        for(UINT y=0;y<height;++y) {
            require(channel(sharpened.bytes,width,28,y)==channel(sharpened.bytes,width,28,0),"Soft edge picked up a row dependency");
            require(channel(pixels,width,28,0)-channel(sharpened.bytes,width,28,y)>=8,"Dark side of the edge was not sharpened darker");
            require(channel(sharpened.bytes,width,31,y)-channel(pixels,width,31,0)>=8,"Bright side of the edge was not sharpened brighter");
        }
        for(UINT y=0;y<height;++y) for(UINT x=0;x<width;++x) {
            if(x>=28 && x<32) continue;
            for(unsigned component=0;component<4;++component)
                require(std::abs(channel(sharpened.bytes,width,x,y,component)-channel(pixels,width,x,y,component))<=1,
                    "Flat region changed");
        }
        // A UNORM source must still be sharpened in linear light, whether the
        // D3D11 driver allows an _SRGB view over it or the typeless copy is used.
        auto linear=source_texture(device.Get(),width,height,pixels,DXGI_FORMAT_R8G8B8A8_UNORM,D3D11_BIND_SHADER_RESOURCE);
        const auto from_linear=d2vr::readback_rgba(sharpen.apply(linear.Get(),1.f));
        for(std::size_t i=0;i<sharpened.bytes.size();++i)
            require(std::abs(std::to_integer<int>(from_linear.bytes[i])-std::to_integer<int>(sharpened.bytes[i]))<=1,
                "A UNORM source was not sampled in linear light");
        // Same descriptor as the SMAA output: typeless, render target only.
        auto render_target=source_texture(device.Get(),width,height,pixels,DXGI_FORMAT_R8G8B8A8_TYPELESS,D3D11_BIND_RENDER_TARGET);
        const auto from_render_target=d2vr::readback_rgba(sharpen.apply(render_target.Get(),1.f));
        require(from_render_target.width==width && from_render_target.height==height,"Render-target-only source size differs");
        require(channel(pixels,width,28,0)-channel(from_render_target.bytes,width,28,0)>=8,
            "A source without a shader resource binding was not sharpened");
        // The SMAA result is typeless with a render-target binding only; the
        // chain must accept it as the CAS source.
        d2vr::EyeAntialiasing aa(device.Get());
        auto* antialiased=aa.apply(srgb.Get());
        const auto smoothed=d2vr::readback_rgba(antialiased);
        const auto chained=d2vr::readback_rgba(sharpen.apply(antialiased,1.f));
        require(chained.width==width && chained.height==height,"Sharpened SMAA output size differs");
        unsigned moved{};
        for(std::size_t i=0;i<chained.bytes.size();++i)
            if(std::abs(std::to_integer<int>(chained.bytes[i])-std::to_integer<int>(smoothed.bytes[i]))>1) ++moved;
        require(moved>8,"Sharpening an SMAA result changed nothing");
        // The next eye may arrive at a different resolution.
        const auto smaller=edge_image(40,24);
        auto resized=source_texture(device.Get(),40,24,smaller,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,D3D11_BIND_SHADER_RESOURCE);
        const auto followed=d2vr::readback_rgba(sharpen.apply(resized.Get(),1.f));
        require(followed.width==40 && followed.height==24,"Output did not follow the source size");
        require(channel(smaller,40,28,0)-channel(followed.bytes,40,28,0)>=8,"Resized soft edge was not sharpened");
        require(channel(followed.bytes,40,31,0)-channel(smaller,40,31,0)>=8,"Resized bright side was not sharpened brighter");
        D3D11_VIEWPORT restored{};UINT count=1;context->RSGetViewports(&count,&restored);
        D3D11_PRIMITIVE_TOPOLOGY topology{};context->IAGetPrimitiveTopology(&topology);
        require(restored.TopLeftX==7 && restored.TopLeftY==11 && restored.Width==123 && topology==D3D11_PRIMITIVE_TOPOLOGY_LINELIST,
            "Game graphics state was not restored");
        std::cout << "PASS: CAS edge contrast, flat-region invariance, alpha, zero/negative passthrough, UNORM and render-target-only sources, SMAA chaining, resizing, source preservation and D3D11 state restoration; dark="
            << channel(pixels,width,28,0)-channel(sharpened.bytes,width,28,0) << " bright="
            << channel(sharpened.bytes,width,31,0)-channel(pixels,width,31,0) << '\n';
        return 0;
    } catch(const std::exception& error) {std::cerr << error.what() << '\n';return 1;}
}
