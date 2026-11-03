#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <d3d11.h>
#include <wrl/client.h>
#include <array>
#include <cstdio>
#include <stdexcept>
#include <vector>
#include "../src/eye_resolve.hpp"

using Microsoft::WRL::ComPtr;
static void check(HRESULT hr,const char* what) {if(FAILED(hr)) throw std::runtime_error(what);}
int main() {
    try {
        ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level{};
        check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context),"device");
        const UINT source_size=32,target_size=16;
        D3D11_TEXTURE2D_DESC desc{};desc.Width=source_size;desc.Height=source_size;desc.MipLevels=1;desc.ArraySize=1;
        desc.Format=DXGI_FORMAT_R8G8B8A8_TYPELESS;desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_DEFAULT;
        desc.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;
        ComPtr<ID3D11Texture2D> source;check(device->CreateTexture2D(&desc,nullptr,&source),"source");
        D3D11_RENDER_TARGET_VIEW_DESC rtv_desc{};rtv_desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;rtv_desc.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
        ComPtr<ID3D11RenderTargetView> rtv;check(device->CreateRenderTargetView(source.Get(),&rtv_desc,&rtv),"source rtv");
        const float colour[4]{0.25f,0.5f,0.75f,1.f};context->ClearRenderTargetView(rtv.Get(),colour);
        d2vr::EyeResolve resolve(device.Get(),target_size,target_size);
        auto* output=resolve.apply(source.Get());
        D3D11_TEXTURE2D_DESC out{};output->GetDesc(&out);
        if(out.Width!=target_size || out.Height!=target_size) throw std::runtime_error("resolve output size");
        D3D11_TEXTURE2D_DESC staging=out;staging.Format=DXGI_FORMAT_R8G8B8A8_UNORM;staging.Usage=D3D11_USAGE_STAGING;staging.BindFlags=0;staging.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> readback;check(device->CreateTexture2D(&staging,nullptr,&readback),"readback");
        context->CopyResource(readback.Get(),output);
        D3D11_MAPPED_SUBRESOURCE mapped{};check(context->Map(readback.Get(),0,D3D11_MAP_READ,0,&mapped),"map");
        const auto* first=static_cast<const unsigned char*>(mapped.pData);
        const int r=first[0],g=first[1],b=first[2];
        context->Unmap(readback.Get(),0);
        std::printf("resolve first pixel %d,%d,%d\n",r,g,b);
        if(r<120 || r>150 || g<175 || g>200 || b<215 || b>240) throw std::runtime_error("resolve colour changed");
        std::printf("PASS: EyeResolve 32x32 -> 16x16, sRGB colour preserved (%d,%d,%d)\n",r,g,b);return 0;
    } catch(const std::exception& e) {std::printf("FAIL: %s\n",e.what());return 1;}
}
