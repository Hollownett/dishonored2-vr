#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../src/taa_dispatch.hpp"
#include <wrl/client.h>
#include <array>
#include <iostream>
#include <stdexcept>

using Microsoft::WRL::ComPtr;
static void require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
int main() {
    try {
        ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
        require(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context)),"WARP device");
        D3D11_TEXTURE2D_DESC desc{};desc.Width=4;desc.Height=4;desc.MipLevels=1;desc.ArraySize=1;
        desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;
        std::array<unsigned,16> current{},old_history{};current.fill(0xff783412u);old_history.fill(0xffaa0099u);
        D3D11_SUBRESOURCE_DATA input{current.data(),16,0},old{old_history.data(),16,0};
        ComPtr<ID3D11Texture2D> colour,history,output;
        require(SUCCEEDED(device->CreateTexture2D(&desc,&input,&colour)) && SUCCEEDED(device->CreateTexture2D(&desc,&old,&history)) &&
            SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&output)),"TAA textures");
        d2vr::TaaBindings bindings;
        bindings.res[2]=colour.Get();bindings.res[2]->AddRef();bindings.res[4]=history.Get();bindings.res[4]->AddRef();bindings.res[5]=output.Get();bindings.res[5]->AddRef();
        bindings.tex[2]=bindings.tex[4]=bindings.tex[5]=desc;bindings.is_tex[2]=bindings.is_tex[4]=bindings.is_tex[5]=true;
        require(bindings.copy_current(context.Get()),"Compatible warmup must produce the current eye");
        desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> readback;require(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&readback)),"Readback");
        const auto pixels=[&](ID3D11Texture2D* texture,unsigned expected) {
            context->CopyResource(readback.Get(),texture);D3D11_MAPPED_SUBRESOURCE mapped{};
            require(SUCCEEDED(context->Map(readback.Get(),0,D3D11_MAP_READ,0,&mapped)),"Map output");
            bool match=true;
            for(unsigned y=0;y<4;++y) for(unsigned x=0;x<4;++x)
                match=match && reinterpret_cast<const unsigned*>(static_cast<const unsigned char*>(mapped.pData)+y*mapped.RowPitch)[x]==expected;
            context->Unmap(readback.Get(),0);require(match,"Warmup reused another eye/history or altered shared history");
        };
        pixels(output.Get(),current[0]);pixels(history.Get(),old_history[0]);
        require(!bindings.copy_current(nullptr),"Null context rejected");
        ++bindings.tex[5].Width;require(!bindings.copy_current(context.Get()),"Resized output rejected");bindings.tex[5]=bindings.tex[2];
        bindings.tex[5].Format=DXGI_FORMAT_R16G16B16A16_FLOAT;require(!bindings.copy_current(context.Get()),"Different format rejected");bindings.tex[5]=bindings.tex[2];
        bindings.tex[5].MipLevels=2;require(!bindings.copy_current(context.Get()),"Different mip count rejected");bindings.tex[5]=bindings.tex[2];
        bindings.tex[5].ArraySize=2;require(!bindings.copy_current(context.Get()),"Different array size rejected");bindings.tex[5]=bindings.tex[2];
        bindings.tex[5].SampleDesc.Count=2;require(!bindings.copy_current(context.Get()),"Different sample count rejected");bindings.tex[5]=bindings.tex[2];
        bindings.tex[5].SampleDesc.Quality=1;require(!bindings.copy_current(context.Get()),"Different sample quality rejected");bindings.tex[5]=bindings.tex[2];
        auto* retained=bindings.res[5];bindings.res[5]=bindings.res[2];require(!bindings.copy_current(context.Get()),"Same source/target rejected");bindings.res[5]=retained;
        bindings.res[5]=bindings.res[4];require(!bindings.copy_current(context.Get()),"Shared history/output alias rejected");bindings.res[5]=retained;
        std::cout<<"PASS: temporal warmup copies current eye only; shared history and incompatible resources protected\n";return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
