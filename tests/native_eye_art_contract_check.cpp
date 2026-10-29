// Native eye art must never end the XR session (v685). The headset v684 session lost VR three times
// because a decorative draw threw out of GameXr::present into the render callback's session recovery.
// This fixture pins the replacement contract on a real WARP device, in the shape the game uses:
//   * the runtime's typeless eye storage composites through a concrete view (pixels actually land),
//   * a refused draw returns its failing call and HRESULT instead of throwing,
//   * argument refusals are reported, not thrown.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../src/native_eye_art.hpp"
#include <wrl/client.h>
#include <iostream>
#include <stdexcept>

using Microsoft::WRL::ComPtr;
static void require(bool value,const char* message) {if(!value)throw std::runtime_error(message);}

static unsigned red_pixels(ID3D11Device* device,ID3D11DeviceContext* context,ID3D11Texture2D* eye) {
    D3D11_TEXTURE2D_DESC desc{};eye->GetDesc(&desc);
    desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> readback;
    require(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&readback)),"Eye readback");
    context->CopyResource(readback.Get(),eye);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    require(SUCCEEDED(context->Map(readback.Get(),0,D3D11_MAP_READ,0,&mapped)),"Map eye pixels");
    unsigned red=0;
    for(unsigned y=0;y<desc.Height;++y)for(unsigned x=0;x<desc.Width;++x) {
        const auto* p=static_cast<const unsigned char*>(mapped.pData)+y*mapped.RowPitch+x*4;
        if(p[0]>150 && p[1]<80) ++red;
    }
    context->Unmap(readback.Get(),0);
    return red;
}

int main() {
    try {
        ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
        require(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context)),"WARP device");
        d2vr::NativeEyeArt compositor(device.Get());
        std::array<unsigned char,64*64*4> pixels{};
        for(std::size_t i=0;i<pixels.size();i+=4) {pixels[i]=220;pixels[i+1]=35;pixels[i+2]=10;pixels[i+3]=255;}
        D3D11_TEXTURE2D_DESC art_desc{};art_desc.Width=art_desc.Height=64;art_desc.MipLevels=art_desc.ArraySize=1;
        art_desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;art_desc.SampleDesc.Count=1;art_desc.Usage=D3D11_USAGE_IMMUTABLE;
        art_desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA initial{};initial.pSysMem=pixels.data();initial.SysMemPitch=64*4;
        ComPtr<ID3D11Texture2D> art;require(SUCCEEDED(device->CreateTexture2D(&art_desc,&initial,&art)),"Native art source");
        const D3D11_BOX crop{0,0,0,64,64,1};
        require(d2vr::native_art_view_format(DXGI_FORMAT_R8G8B8A8_TYPELESS)==DXGI_FORMAT_R8G8B8A8_UNORM,"Typeless resolves to a concrete view format");
        require(d2vr::native_art_view_format(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,"Typed format is preserved");

        // The eye image as the runtime creates it for the sRGB chain: typeless storage. The game asks for
        // the sRGB format; the view must follow the resource and the pixels must actually land.
        D3D11_TEXTURE2D_DESC eye_desc{};eye_desc.Width=eye_desc.Height=64;eye_desc.MipLevels=eye_desc.ArraySize=1;
        eye_desc.Format=DXGI_FORMAT_R8G8B8A8_TYPELESS;eye_desc.SampleDesc.Count=1;eye_desc.BindFlags=D3D11_BIND_RENDER_TARGET;
        ComPtr<ID3D11Texture2D> eye;require(SUCCEEDED(device->CreateTexture2D(&eye_desc,nullptr,&eye)),"Typeless eye target");
        const float black[4]{0,0,0,1};
        {D3D11_RENDER_TARGET_VIEW_DESC rt{};rt.Format=DXGI_FORMAT_R8G8B8A8_UNORM;rt.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
         ComPtr<ID3D11RenderTargetView> rtv;require(SUCCEEDED(device->CreateRenderTargetView(eye.Get(),&rt,&rtv)),"Typeless eye RTV");context->ClearRenderTargetView(rtv.Get(),black);}
        const auto drawn=compositor.draw(eye.Get(),art.Get(),crop,{-1,-1,0,0},DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
        require(drawn.drawn && drawn.hr==S_OK,"Typeless eye storage draws through a concrete view");
        require(drawn.eye_view==DXGI_FORMAT_R8G8B8A8_UNORM,"Typeless storage takes the byte-preserving UNORM view");
        require(drawn.art_view!=DXGI_FORMAT_R8G8B8A8_TYPELESS,"The art view is concrete");
        const auto composited=red_pixels(device.Get(),context.Get(),eye.Get());
        require(composited>100,"Composited pixels reach the typeless eye image");
        std::cout<<"PASS: "<<composited<<" composited pixels in typeless eye storage (views "<<static_cast<unsigned>(drawn.eye_view)
                 <<'/'<<static_cast<unsigned>(drawn.art_view)<<")\n";

        // A draw the device refuses must come back as a result. Art without the SRV bind flag cannot be
        // sampled: exactly the failure class that used to throw and end the session.
        D3D11_TEXTURE2D_DESC no_srv_desc=art_desc;
        no_srv_desc.BindFlags=D3D11_BIND_RENDER_TARGET;no_srv_desc.Usage=D3D11_USAGE_DEFAULT;
        ComPtr<ID3D11Texture2D> no_srv;require(SUCCEEDED(device->CreateTexture2D(&no_srv_desc,nullptr,&no_srv)),"Non-sampled art");
        const auto refused=compositor.draw(eye.Get(),no_srv.Get(),crop,{-1,-1,0,0},DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
        require(!refused.drawn && !refused.rejected,"A refused draw is reported");
        require(refused.hr==E_INVALIDARG,"The refused draw carries its HRESULT");
        require(std::string(refused.call)=="CreateShaderResourceView","The refused draw names the failing call");
        std::cout<<"PASS: a refused draw returns "<<refused.call<<" hr=0x"<<std::hex<<static_cast<unsigned long>(refused.hr)<<std::dec<<" instead of throwing\n";

        // Refusals that are not device faults are reported separately and never reach D3D.
        require(compositor.draw(eye.Get(),eye.Get(),crop,{-1,-1,0,0},DXGI_FORMAT_R8G8B8A8_UNORM_SRGB).rejected,"Art may not be its own eye image");
        require(compositor.draw(nullptr,art.Get(),crop,{-1,-1,0,0},DXGI_FORMAT_R8G8B8A8_UNORM_SRGB).rejected,"A missing eye is rejected");
        require(compositor.draw(eye.Get(),art.Get(),D3D11_BOX{0,0,0,128,128,1},{-1,-1,0,0},DXGI_FORMAT_R8G8B8A8_UNORM_SRGB).rejected,"A crop outside the art is rejected");
        std::cout<<"PASS: argument refusals are reported, nothing throws\n";
        return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
