#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../src/native_eye_art.hpp"
#include "../src/eye_reticle.hpp"
#include <wrl/client.h>
#include <iostream>
#include <stdexcept>

using Microsoft::WRL::ComPtr;
static void require(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
int main() {
    try {
        ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
        require(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context)),"WARP device");
        const auto pixels=d2vr::eye_reticle_pixels();
        D3D11_TEXTURE2D_DESC source{};source.Width=source.Height=32;source.MipLevels=source.ArraySize=1;
        source.Format=DXGI_FORMAT_R8G8B8A8_UNORM;source.SampleDesc.Count=1;source.Usage=D3D11_USAGE_IMMUTABLE;source.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA initial{};initial.pSysMem=pixels.data();initial.SysMemPitch=32*4;
        ComPtr<ID3D11Texture2D> ring;require(SUCCEEDED(device->CreateTexture2D(&source,&initial,&ring)),"UNORM reticle source");
        d2vr::NativeEyeArt compositor(device.Get());
        for(const auto format:{DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_B8G8R8A8_UNORM,
                DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,DXGI_FORMAT_B8G8R8A8_UNORM_SRGB}) {
            D3D11_TEXTURE2D_DESC desc{};desc.Width=desc.Height=64;desc.MipLevels=desc.ArraySize=1;
            desc.Format=format;desc.SampleDesc.Count=1;desc.BindFlags=D3D11_BIND_RENDER_TARGET;
            ComPtr<ID3D11Texture2D> eye;require(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&eye)),"SRGB eye target");
            ComPtr<ID3D11RenderTargetView> rtv;require(SUCCEEDED(device->CreateRenderTargetView(eye.Get(),nullptr,&rtv)),"Eye RTV");
            const float clear[4]{};context->ClearRenderTargetView(rtv.Get(),clear);
            const auto rect=d2vr::eye_reticle_rect(.75f,-.75f,64,64,6);require(rect.has_value(),"Off-centre reticle rectangle");
            compositor.draw(eye.Get(),ring.Get(),D3D11_BOX{0,0,0,32,32,1},
                {(*rect)[0],(*rect)[1],(*rect)[2],(*rect)[3]},format,false,DXGI_FORMAT_R8G8B8A8_UNORM);
            desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
            ComPtr<ID3D11Texture2D> readback;require(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&readback)),"Readback");
            context->CopyResource(readback.Get(),eye.Get());D3D11_MAPPED_SUBRESOURCE mapped{};
            require(SUCCEEDED(context->Map(readback.Get(),0,D3D11_MAP_READ,0,&mapped)),"Map reticle pixels");
            unsigned visible=0,outside=0;
            for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x) {
                const auto* p=static_cast<const unsigned char*>(mapped.pData)+y*mapped.RowPitch+x*4;
                if(p[0] || p[1] || p[2]) {++visible;if(x<50 || x>=62 || y<50 || y>=62)++outside;}
            }
            context->Unmap(readback.Get(),0);
            require(visible>=8,"Reticle pixels missing from SRGB eye");require(!outside,"Reticle affected pixels outside its projected eye rectangle");
            std::cout<<"PASS: UNORM ring into eye format "<<static_cast<unsigned>(format)<<", visible pixels "<<visible<<'\n';
        }
        return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
