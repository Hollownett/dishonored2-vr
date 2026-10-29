#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../src/native_eye_art.hpp"
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;
static void require(bool ok,const char* what) {if(!ok)throw std::runtime_error(what);}

int main() {
    try {
        ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> ctx;
        require(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&ctx)),"WARP device");
        D3D11_TEXTURE2D_DESC s{};s.Width=s.Height=8;s.MipLevels=s.ArraySize=1;s.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
        s.SampleDesc.Count=1;s.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        std::array<unsigned char,8*8*4> red{},green{};
        for(unsigned i=0;i<64;++i) {red[4*i]=128;red[4*i+3]=128;green[4*i+1]=64;green[4*i+3]=64;}
        D3D11_SUBRESOURCE_DATA data{};data.pSysMem=red.data();data.SysMemPitch=8*4;
        ComPtr<ID3D11Texture2D> source;require(SUCCEEDED(device->CreateTexture2D(&s,&data,&source)),"source texture");
        D3D11_TEXTURE2D_DESC k=s;k.Width=k.Height=6;ComPtr<ID3D11Texture2D> kept;
        require(SUCCEEDED(device->CreateTexture2D(&k,nullptr,&kept)),"kept texture");
        const D3D11_BOX crop{1,1,0,7,7,1};ctx->CopySubresourceRegion(kept.Get(),0,0,0,0,source.Get(),0,&crop);
        ctx->UpdateSubresource(source.Get(),0,nullptr,green.data(),8*4,0);
        d2vr::NativeEyeArt compositor(device.Get());
        D3D11_TEXTURE2D_DESC e{};e.Width=e.Height=64;e.MipLevels=e.ArraySize=1;e.Format=s.Format;e.SampleDesc.Count=1;e.BindFlags=D3D11_BIND_RENDER_TARGET;
        for(unsigned eye=0;eye<2;++eye) {
            ComPtr<ID3D11Texture2D> target;ComPtr<ID3D11RenderTargetView> rtv;
            require(SUCCEEDED(device->CreateTexture2D(&e,nullptr,&target)),"eye target");
            require(SUCCEEDED(device->CreateRenderTargetView(target.Get(),nullptr,&rtv)),"eye RTV");
            const float bg[]{.2f,.4f,.6f,1.f};ctx->ClearRenderTargetView(rtv.Get(),bg);
            // Aim (40,24) in pixels, pivot (2,4) inside the 6x6 crop.
            const d2vr::NativeArtRect rect{2.f*38/64-1,1-2.f*20/64,2.f*44/64-1,1-2.f*26/64};
            const D3D11_BOX kept_crop{0,0,0,6,6,1};
            compositor.draw(target.Get(),eye?source.Get():kept.Get(),eye?crop:kept_crop,rect,e.Format,true,s.Format);
            auto staging=e;staging.Usage=D3D11_USAGE_STAGING;staging.BindFlags=0;staging.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
            ComPtr<ID3D11Texture2D> read;require(SUCCEEDED(device->CreateTexture2D(&staging,nullptr,&read)),"staging texture");
            ctx->CopyResource(read.Get(),target.Get());D3D11_MAPPED_SUBRESOURCE mapped{};
            require(SUCCEEDED(ctx->Map(read.Get(),0,D3D11_MAP_READ,0,&mapped)),"map target");
            const auto pixel=[&](unsigned x,unsigned y){return static_cast<const unsigned char*>(mapped.pData)+y*mapped.RowPitch+4*x;};
            const auto close=[](int a,int b){return std::abs(a-b)<=2;};
            const auto* p=pixel(40,24);const bool mixed=eye?
                close(p[0],38)&&close(p[1],140)&&close(p[2],115):close(p[0],153)&&close(p[1],51)&&close(p[2],76);
            const auto* left=pixel(37,24);const auto* right=pixel(44,24);const auto* top=pixel(40,19);const auto* bottom=pixel(40,26);
            const auto background=[&](const unsigned char* q){return close(q[0],51)&&close(q[1],102)&&close(q[2],153);};
            const bool bounds=background(left)&&background(right)&&background(top)&&background(bottom);
            const bool corners=!background(pixel(38,20))&&!background(pixel(43,25));
            ctx->Unmap(read.Get(),0);
            require(mixed,"premultiplied blending or saved eye copy differs");
            require(bounds && corners,"off-centre crop/pivot placement differs");
        }
        std::cout<<"PASS: live reticle colours, saved eye pixels, premultiplied alpha, and off-centre pivot\n";
        return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
