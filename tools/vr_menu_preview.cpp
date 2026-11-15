// Renders the ImGui VR settings panel (src/vr_menu_imgui.hpp) to PNG files without the game: the same draw function the mod uses.
// Build + run: tools\build-vr-menu-preview.cmd   ->  artifacts\vr-menu-preview-*.png
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <cstdio>
#include <string>
#include <vector>
#include "vr_menu_imgui.hpp"
#pragma comment(lib,"d3d11.lib")
#pragma comment(lib,"windowscodecs.lib")
#pragma comment(lib,"ole32.lib")
#pragma comment(lib,"d3dcompiler.lib")
using Microsoft::WRL::ComPtr;

static bool save_png(ID3D11Device* device,ID3D11DeviceContext* context,ID3D11Texture2D* texture,const std::wstring& file) {
    D3D11_TEXTURE2D_DESC d{};texture->GetDesc(&d);d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;if(FAILED(device->CreateTexture2D(&d,nullptr,&staging))) return false;
    context->CopyResource(staging.Get(),texture);
    D3D11_MAPPED_SUBRESOURCE m{};if(FAILED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&m))) return false;
    std::vector<unsigned char> bgra(static_cast<std::size_t>(d.Width)*d.Height*4);
    for(UINT y=0;y<d.Height;++y) {const auto* row=static_cast<const unsigned char*>(m.pData)+y*m.RowPitch;
        for(UINT x=0;x<d.Width;++x) {auto* o=&bgra[(y*d.Width+x)*4];o[0]=row[x*4+2];o[1]=row[x*4+1];o[2]=row[x*4+0];o[3]=255;}}
    context->Unmap(staging.Get(),0);
    ComPtr<IWICImagingFactory> wic;if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic)))) return false;
    ComPtr<IWICStream> stream;ComPtr<IWICBitmapEncoder> enc;ComPtr<IWICBitmapFrameEncode> frame;
    if(FAILED(wic->CreateStream(&stream)) || FAILED(stream->InitializeFromFilename(file.c_str(),GENERIC_WRITE)) ||
       FAILED(wic->CreateEncoder(GUID_ContainerFormatPng,nullptr,&enc)) || FAILED(enc->Initialize(stream.Get(),WICBitmapEncoderNoCache)) ||
       FAILED(enc->CreateNewFrame(&frame,nullptr)) || FAILED(frame->Initialize(nullptr)) || FAILED(frame->SetSize(d.Width,d.Height))) return false;
    WICPixelFormatGUID fmt=GUID_WICPixelFormat32bppBGRA;frame->SetPixelFormat(&fmt);
    return SUCCEEDED(frame->WritePixels(d.Height,d.Width*4,static_cast<UINT>(bgra.size()),bgra.data())) && SUCCEEDED(frame->Commit()) && SUCCEEDED(enc->Commit());
}
// Move the selection to `row` with simulated stick-down presses (the panel's own input path).
static void select_row(d2vr::VrSettings& s,unsigned row) {
    d2vr::ControllerInput in{};in.active=true;s.update(in,false,true);   // first frame: controls become valid
    while(s.selected()!=row) {in.move_y=-1;s.update(in,false,true);in.move_y=0;s.update(in,false,true);}
}
int main() {
    CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context)) &&
       FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context))) {std::puts("no D3D11 device");return 1;}
    d2vr::VrMenuImgui menu(device.Get(),context.Get());
    // applied: what renders; select: the row's selection (differs = an Apply pending); capable: the session can alternate.
    struct Shot {const wchar_t* file;unsigned row;int applied,select;bool capable;const char* status;std::optional<d2vr::PanelPointer> pointer;};
    const Shot shots[]={
        {L"artifacts/vr-menu-preview-main.png",0,0,0,true,"",std::nullopt},
        {L"artifacts/vr-menu-preview-renderer-afw.png",12,2,2,true,"AFW active",d2vr::PanelPointer{.78,(64+26*12+12)/560.}},
        {L"artifacts/vr-menu-preview-renderer-apply.png",12,0,2,true,"",std::nullopt},
        {L"artifacts/vr-menu-preview-renderer-unavailable.png",12,0,1,false,"",std::nullopt},
    };
    for(const auto& shot:shots) {
        d2vr::VrSettings s;s.initialize(1.2,true,{},d2vr::MirrorMode::right_eye,.5,7,65,true,{},true,true,true,d2vr::CutsceneMode::vr_comfort,0);
        s.dlss_available(true);s.upscaler_generation(5);s.set_renderer(shot.applied);s.renderer_capable(shot.capable);s.afw_status(shot.status);
        s.open_from_menu();select_row(s,shot.row);while(s.renderer()!=shot.select) s.cycle_renderer(1);
        menu.render(s,1.2,2496,2688,shot.pointer);
        const bool ok=save_png(device.Get(),context.Get(),menu.texture(),shot.file);
        std::wprintf(L"%ls %ls\n",shot.file,ok?L"written":L"FAILED");
    }
    return 0;
}
