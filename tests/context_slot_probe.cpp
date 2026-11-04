// Scratch probe: which ID3D11DeviceContext vtable slots are the PS-state setters. Calls a candidate slot through its raw vtable
// entry with the arguments of one known method, then reads the state back with the matching Get method. A slot is accepted only
// when the call does not fault and the readable state changed to the value passed. WARP device, so a wrong guess crashes here and
// nowhere else. Prints one line per slot tested.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <cstdio>

static ID3D11Device* device{};
static ID3D11DeviceContext* context{};
static ID3D11ShaderResourceView* srv{};
static ID3D11ShaderResourceView* srv2{};
static ID3D11Buffer* cbuffer{};
static ID3D11SamplerState* sampler{};
static ID3D11Buffer* vertex_buffer{};

static bool make_objects() {
    D3D11_TEXTURE2D_DESC td{};td.Width=4;td.Height=4;td.MipLevels=1;td.ArraySize=1;td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count=1;td.Usage=D3D11_USAGE_DEFAULT;td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    ID3D11Texture2D* texture{};
    if(FAILED(device->CreateTexture2D(&td,nullptr,&texture))) return false;
    D3D11_SHADER_RESOURCE_VIEW_DESC sd{};sd.Format=td.Format;sd.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;sd.Texture2D.MipLevels=1;
    ID3D11ShaderResourceView* made{};device->CreateShaderResourceView(texture,&sd,&made);texture->Release();
    ID3D11Texture2D* texture2{};
    if(FAILED(device->CreateTexture2D(&td,nullptr,&texture2))) return false;
    ID3D11ShaderResourceView* made2{};device->CreateShaderResourceView(texture2,&sd,&made2);texture2->Release();
    srv=made;srv2=made2;
    D3D11_BUFFER_DESC bd{};bd.ByteWidth=64;bd.Usage=D3D11_USAGE_DEFAULT;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    if(FAILED(device->CreateBuffer(&bd,nullptr,&cbuffer))) return false;
    D3D11_SAMPLER_DESC sp{};sp.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;sp.AddressU=sp.AddressV=sp.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;
    sp.ComparisonFunc=D3D11_COMPARISON_NEVER;sp.MaxLOD=D3D11_FLOAT32_MAX;
    if(FAILED(device->CreateSamplerState(&sp,&sampler))) return false;
    D3D11_BUFFER_DESC vb{};vb.ByteWidth=64;vb.Usage=D3D11_USAGE_DEFAULT;vb.BindFlags=D3D11_BIND_VERTEX_BUFFER;
    if(FAILED(device->CreateBuffer(&vb,nullptr,&vertex_buffer))) return false;
    return true;
}

int main() {
    if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,nullptr))) {
        std::printf("no WARP device\n");return 1;
    }
    device->GetImmediateContext(&context);
    if(!make_objects()) {std::printf("object creation failed\n");return 1;}
    void** vt=*reinterpret_cast<void***>(context);

    // slot 8 as PSSetShaderResources(0,1,&srv)
    {ID3D11ShaderResourceView* views[1]{srv};
     __try {reinterpret_cast<void(__stdcall*)(ID3D11DeviceContext*,UINT,UINT,ID3D11ShaderResourceView* const*)>(vt[8])(context,0,1,views);}
     __except(EXCEPTION_EXECUTE_HANDLER) {std::printf("slot 8: FAULT\n");return 1;}
     ID3D11ShaderResourceView* got{};context->PSGetShaderResources(0,1,&got);
     std::printf("slot 8 as PSSetShaderResources: %s\n",got==srv?"MATCH":(got?"other":"no"));
     if(got) got->Release();}
    // slot 22 as PSSetShaderResources (the other common position)
    if(true){ID3D11ShaderResourceView* views[1]{srv2};
     __try {reinterpret_cast<void(__stdcall*)(ID3D11DeviceContext*,UINT,UINT,ID3D11ShaderResourceView* const*)>(vt[22])(context,0,1,views);}
     __except(EXCEPTION_EXECUTE_HANDLER) {std::printf("slot 22: FAULT\n");return 0;}
     ID3D11ShaderResourceView* got{};context->PSGetShaderResources(0,1,&got);
     std::printf("slot 22 as PSSetShaderResources: %s\n",got==srv2?"MATCH":(got?"other":"no"));
     if(got) got->Release();}
    // slot 10 as PSSetSamplers(0,1,&sampler)
    {ID3D11SamplerState* states[1]{sampler};
     __try {reinterpret_cast<void(__stdcall*)(ID3D11DeviceContext*,UINT,UINT,ID3D11SamplerState* const*)>(vt[10])(context,0,1,states);}
     __except(EXCEPTION_EXECUTE_HANDLER) {std::printf("slot 10: FAULT\n");return 0;}
     ID3D11SamplerState* got{};context->PSGetSamplers(0,1,&got);
     std::printf("slot 10 as PSSetSamplers: %s\n",got==sampler?"MATCH":(got?"other":"no"));
     if(got) got->Release();}
    // slot 16 as PSSetConstantBuffers(0,1,&cbuffer)
    {ID3D11Buffer* buffers[1]{cbuffer};
     __try {reinterpret_cast<void(__stdcall*)(ID3D11DeviceContext*,UINT,UINT,ID3D11Buffer* const*)>(vt[16])(context,0,1,buffers);}
     __except(EXCEPTION_EXECUTE_HANDLER) {std::printf("slot 16: FAULT\n");return 0;}
     ID3D11Buffer* got{};context->PSGetConstantBuffers(0,1,&got);
     std::printf("slot 16 as PSSetConstantBuffers: %s\n",got==cbuffer?"MATCH":(got?"other":"no"));
     if(got) got->Release();}
    return 0;
}
