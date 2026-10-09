#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include "../src/context_state.hpp"
#include <iostream>
#include <stdexcept>

using Microsoft::WRL::ComPtr;
void require(bool passed,const char* message) {if(!passed) throw std::runtime_error(message);}
int main() {
    try {
        ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
        require(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,
            D3D11_SDK_VERSION,&device,nullptr,&context)),"Cannot create software test device");
        D3D11_BUFFER_DESC desc{};desc.ByteWidth=64;desc.Usage=D3D11_USAGE_DEFAULT;
        desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        ComPtr<ID3D11Buffer> buffer;
        require(SUCCEEDED(device->CreateBuffer(&desc,nullptr,&buffer)),"Cannot create test buffer");
        auto raw=buffer.Get();context->VSSetConstantBuffers(7,1,&raw);context->CSSetConstantBuffers(5,1,&raw);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
        const D3D11_VIEWPORT viewport{11,13,320,240,.1f,.9f};context->RSSetViewports(1,&viewport);
        d2vr::ContextState isolated(device.Get());
        for(int pass=0;pass<2;++pass) {
            try {
                d2vr::ContextState::Scope scope(isolated);
                context->ClearState();
                context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                const D3D11_VIEWPORT foreign{0,0,512,512,0,1};context->RSSetViewports(1,&foreign);
                if(pass) throw std::runtime_error("Injected runtime failure");
            } catch(const std::runtime_error&) {require(pass==1,"Unexpected isolation exception");}
            D3D11_VIEWPORT restored{};UINT count=1;context->RSGetViewports(&count,&restored);
            require(count==1 && restored.TopLeftX==11 && restored.TopLeftY==13 && restored.Width==320 &&
                restored.Height==240 && restored.MinDepth==.1f && restored.MaxDepth==.9f,"Viewport was not restored");
            D3D11_PRIMITIVE_TOPOLOGY topology{};context->IAGetPrimitiveTopology(&topology);
            require(topology==D3D11_PRIMITIVE_TOPOLOGY_LINELIST,"Input assembler was not restored");
            ComPtr<ID3D11Buffer> vs,cs;context->VSGetConstantBuffers(7,1,&vs);context->CSGetConstantBuffers(5,1,&cs);
            require(vs.Get()==buffer.Get() && cs.Get()==buffer.Get(),"Game shader bindings were not restored");
        }
        std::cout << "PASS: native D3D11 state restoration after runtime mutations and exception, WARP device\n";
        return 0;
    } catch(const std::exception& error) {std::cerr << error.what() << '\n';return 1;}
}
