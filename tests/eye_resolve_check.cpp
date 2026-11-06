#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <array>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>
#include "../src/eye_resolve.hpp"

using Microsoft::WRL::ComPtr;
static void check(HRESULT hr,const char* what) {
    if(FAILED(hr)) throw std::runtime_error(std::string(what)+" (hr=0x"+std::to_string(static_cast<unsigned long>(hr))+")");
}
// The resolve writes sRGB; a UNORM staging copy of the output reads the encoded bytes back.
static std::array<int,3> first_pixel(ID3D11Device* device,ID3D11DeviceContext* context,ID3D11Texture2D* output) {
    D3D11_TEXTURE2D_DESC staging{};output->GetDesc(&staging);
    staging.Format=DXGI_FORMAT_R8G8B8A8_UNORM;staging.Usage=D3D11_USAGE_STAGING;
    staging.BindFlags=0;staging.CPUAccessFlags=D3D11_CPU_ACCESS_READ;staging.MiscFlags=0;
    staging.MipLevels=1;staging.ArraySize=1;staging.SampleDesc.Count=1;staging.SampleDesc.Quality=0;
    ComPtr<ID3D11Texture2D> readback;check(device->CreateTexture2D(&staging,nullptr,&readback),"readback");
    context->CopyResource(readback.Get(),output);
    D3D11_MAPPED_SUBRESOURCE mapped{};check(context->Map(readback.Get(),0,D3D11_MAP_READ,0,&mapped),"map");
    const auto* first=static_cast<const unsigned char*>(mapped.pData);
    const std::array<int,3> result{first[0],first[1],first[2]};
    context->Unmap(readback.Get(),0);
    return result;
}
static void expect_colour(const std::array<int,3>& pixel,const char* what) {
    std::printf("resolve %s first pixel %d,%d,%d\n",what,pixel[0],pixel[1],pixel[2]);
    if(pixel[0]<120 || pixel[0]>150 || pixel[1]<175 || pixel[1]>200 || pixel[2]<215 || pixel[2]>240)
        throw std::runtime_error(std::string("resolve colour changed: ")+what);
}
int main() {
    HWND window{};
    try {
        const UINT source_size=32,target_size=16;
        window=CreateWindowExW(0,L"STATIC",L"D2VR eye resolve check",WS_POPUP,0,0,source_size,source_size,
            nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        if(!window) throw std::runtime_error("test window");
        // One device for the sources and the swapchain, as the game has one device.
        DXGI_SWAP_CHAIN_DESC chain_desc{};chain_desc.BufferDesc.Width=source_size;chain_desc.BufferDesc.Height=source_size;
        chain_desc.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;chain_desc.SampleDesc.Count=1;
        chain_desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;chain_desc.BufferCount=1;
        chain_desc.OutputWindow=window;chain_desc.Windowed=TRUE;chain_desc.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
        ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level{};ComPtr<IDXGISwapChain> chain;
        check(D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,
            &chain_desc,&chain,&device,&level,&context),"device");
        ComPtr<ID3D11Texture2D> backbuffer;check(chain->GetBuffer(0,IID_PPV_ARGS(&backbuffer)),"backbuffer");
        D3D11_TEXTURE2D_DESC backbuffer_desc{};backbuffer->GetDesc(&backbuffer_desc);
        if(backbuffer_desc.BindFlags&D3D11_BIND_SHADER_RESOURCE) throw std::runtime_error("test backbuffer is unexpectedly readable");
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
        expect_colour(first_pixel(device.Get(),context.Get(),output),"readable 32x32");
        // v1021: the render path's real sources are not shader-readable. paired_left is a copy target (no read view),
        // and the right eye is the swapchain backbuffer. Creating the read view on either used to return E_INVALIDARG,
        // which threw out of GameXr::present and restarted the whole XR session. Both must resolve through the copy.
        D3D11_TEXTURE2D_DESC copy_desc=desc;copy_desc.BindFlags=0;
        ComPtr<ID3D11Texture2D> copy_target;check(device->CreateTexture2D(&copy_desc,nullptr,&copy_target),"copy target");
        context->CopyResource(copy_target.Get(),source.Get());
        expect_colour(first_pixel(device.Get(),context.Get(),resolve.apply(copy_target.Get())),"copy target without a read view");
        // The game's own backbuffer format (v685: R8G8B8A8_UNORM_SRGB), still without the read-view flag.
        D3D11_TEXTURE2D_DESC srgb_desc=desc;srgb_desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        srgb_desc.BindFlags=D3D11_BIND_RENDER_TARGET; // a render target with no read view, like the game backbuffer
        ComPtr<ID3D11Texture2D> srgb_source;check(device->CreateTexture2D(&srgb_desc,nullptr,&srgb_source),"sRGB source");
        D3D11_RENDER_TARGET_VIEW_DESC srgb_rtv_desc{};srgb_rtv_desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        srgb_rtv_desc.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
        ComPtr<ID3D11RenderTargetView> srgb_rtv;check(device->CreateRenderTargetView(srgb_source.Get(),&srgb_rtv_desc,&srgb_rtv),"sRGB source rtv");
        context->ClearRenderTargetView(srgb_rtv.Get(),colour);
        expect_colour(first_pixel(device.Get(),context.Get(),resolve.apply(srgb_source.Get())),"unreadable sRGB source");
        // A real swapchain backbuffer: render target only, exactly the right eye's source.
        ComPtr<ID3D11RenderTargetView> backbuffer_rtv;check(device->CreateRenderTargetView(backbuffer.Get(),nullptr,&backbuffer_rtv),"backbuffer rtv");
        context->ClearRenderTargetView(backbuffer_rtv.Get(),colour);
        expect_colour(first_pixel(device.Get(),context.Get(),resolve.apply(backbuffer.Get())),"swapchain backbuffer");
        std::printf("PASS: EyeResolve 32x32 -> 16x16 sRGB colour preserved from a readable texture, a copy target without a read view, and a swapchain backbuffer\n");
        DestroyWindow(window);return 0;
    } catch(const std::exception& e) {if(window) DestroyWindow(window);std::printf("FAIL: %s\n",e.what());return 1;}
}
