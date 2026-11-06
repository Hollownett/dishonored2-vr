// v993: proves the ID3D11DeviceContext vtable slots the PS state filter needs, on a WARP device, for BOTH the immediate context
// and a deferred context. The header names every method; what needs proof is the hardcoded slot index used to hook it. A slot is
// accepted only when calling it through its raw vtable entry with one known method's arguments does not fault AND the matching
// Get method (called through the real interface, so its own index cannot be wrong) reads back exactly the value passed.
//
// v993 proved: 110 ClearState, 68 CSSetUnorderedAccessViews, 37 SOSetTargets, 33 OMSetRenderTargets, 34
// OMSetRenderTargetsAndUnorderedAccessViews, 16 PSSetConstantBuffers. 8/10 are re-checked as controls, 22 as a rejection control.
// v995 adds ID3D11DeviceContext1 slot 123 PSSetConstantBuffers1 (both context kinds, read back through PSGetConstantBuffers1
// including first/num constants) and slot 131 SwapDeviceContextState (immediate only; by design not supported on a deferred
// context). A wrong guess can only crash this probe: it never hooks anything and runs as its own process.
// v997 adds the wide-census slots, each read back through its real Get method on BOTH kinds: 25 VSSetShaderResources,
// 26 VSSetSamplers, 9 PSSetShader, 17 IASetInputLayout, 18 IASetVertexBuffers (buffer+stride+offset), 19 IASetIndexBuffer
// (buffer+format+offset), 24 IASetPrimitiveTopology, 35 OMSetBlendState (state+factor+mask), 36 OMSetDepthStencilState
// (state+ref), 43 RSSetState, 45 RSSetScissorRects (full rects). It also re-proves the ObjectMotion-owned 7 VSSetConstantBuffers,
// 11 VSSetShader and 119 VSSetConstantBuffers1 (the last with first/num), because the wide census now observes inside those hooks.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <cstdio>
#include <cstring>

using Microsoft::WRL::ComPtr;

static ComPtr<ID3D11Device> device;
static ComPtr<ID3D11DeviceContext> immediate;
static ComPtr<ID3D11DeviceContext> deferred;
static ComPtr<ID3D11ShaderResourceView> srv, srv_control;
static ComPtr<ID3D11SamplerState> sampler;
static ComPtr<ID3D11Buffer> constant_buffer, vertex_buffer, index_buffer;
static ComPtr<ID3D11Texture2D> colour_texture, uav_texture;
static ComPtr<ID3D11RenderTargetView> render_target;
static ComPtr<ID3D11UnorderedAccessView> uav;
static ComPtr<ID3D11Buffer> stream_out;
static ComPtr<ID3D11VertexShader> vertex_shader;
static ComPtr<ID3D11PixelShader> pixel_shader;
static ComPtr<ID3D11InputLayout> input_layout;
static ComPtr<ID3D11BlendState> blend_state;
static ComPtr<ID3D11DepthStencilState> depth_state;
static ComPtr<ID3D11RasterizerState> rasterizer_state;

static int passed{}, failed{};

static void report(unsigned slot,const char* method,const char* kind,bool matched) {
    std::printf("slot %3u  %-42s %-9s %s\n",slot,method,kind,matched?"MATCH":"REJECTED");
    if(matched) ++passed; else ++failed;
}

static bool make_texture(D3D11_BIND_FLAG bind,ID3D11Texture2D** out) {
    D3D11_TEXTURE2D_DESC d{};
    d.Width=4;d.Height=4;d.MipLevels=1;d.ArraySize=1;d.Format=DXGI_FORMAT_R8G8B8A8_UNORM;d.SampleDesc.Count=1;
    d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=bind;
    return SUCCEEDED(device->CreateTexture2D(&d,nullptr,out));
}
static bool make_objects() {
    if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,
        &device,nullptr,&immediate))) return false;
    if(FAILED(device->CreateDeferredContext(0,&deferred))) return false;
    D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format=DXGI_FORMAT_R8G8B8A8_UNORM;sd.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;sd.Texture2D.MipLevels=1;
    ComPtr<ID3D11ShaderResourceView>* views[2]{&srv,&srv_control};
    for(auto* view: views) {
        ComPtr<ID3D11Texture2D> texture;
        if(!make_texture(D3D11_BIND_SHADER_RESOURCE,&texture)) return false;
        if(FAILED(device->CreateShaderResourceView(texture.Get(),&sd,view->GetAddressOf()))) return false;
    }
    D3D11_SAMPLER_DESC sp{};
    sp.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sp.AddressU=sp.AddressV=sp.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;
    sp.ComparisonFunc=D3D11_COMPARISON_NEVER;sp.MaxLOD=D3D11_FLOAT32_MAX;
    if(FAILED(device->CreateSamplerState(&sp,&sampler))) return false;
    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth=4096;bd.Usage=D3D11_USAGE_DEFAULT;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER; // room for first=16,num=32
    if(FAILED(device->CreateBuffer(&bd,nullptr,&constant_buffer))) return false;
    if(!make_texture(D3D11_BIND_RENDER_TARGET,&colour_texture)) return false;
    if(FAILED(device->CreateRenderTargetView(colour_texture.Get(),nullptr,&render_target))) return false;
    if(!make_texture(D3D11_BIND_UNORDERED_ACCESS,&uav_texture)) return false;
    if(FAILED(device->CreateUnorderedAccessView(uav_texture.Get(),nullptr,&uav))) return false;
    D3D11_BUFFER_DESC so{};
    so.ByteWidth=64;so.Usage=D3D11_USAGE_DEFAULT;so.BindFlags=D3D11_BIND_STREAM_OUTPUT;
    if(FAILED(device->CreateBuffer(&so,nullptr,&stream_out))) return false;
    // v997 wide-census objects: a trivial VS/PS, an input layout matching the VS's POSITION input, a vertex/index buffer and the
    // three fixed-function states. The readback below is the proof, so the contents do not matter.
    static const char* source=R"(
struct VSOut { float4 pos : SV_Position; };
VSOut VSMain(float3 p : POSITION) { VSOut o; o.pos = float4(p, 1); return o; }
float4 PSMain(VSOut i) : SV_Target { return float4(1, 1, 1, 1); }
)";
    ComPtr<ID3DBlob> vs_blob,ps_blob,error;
    if(FAILED(D3DCompile(source,std::strlen(source),"slot_probe.hlsl",nullptr,nullptr,"VSMain","vs_5_0",0,0,&vs_blob,&error))) return false;
    error.Reset();
    if(FAILED(D3DCompile(source,std::strlen(source),"slot_probe.hlsl",nullptr,nullptr,"PSMain","ps_5_0",0,0,&ps_blob,&error))) return false;
    if(FAILED(device->CreateVertexShader(vs_blob->GetBufferPointer(),vs_blob->GetBufferSize(),nullptr,&vertex_shader))) return false;
    if(FAILED(device->CreatePixelShader(ps_blob->GetBufferPointer(),ps_blob->GetBufferSize(),nullptr,&pixel_shader))) return false;
    const D3D11_INPUT_ELEMENT_DESC element{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0};
    if(FAILED(device->CreateInputLayout(&element,1,vs_blob->GetBufferPointer(),vs_blob->GetBufferSize(),&input_layout))) return false;
    D3D11_BUFFER_DESC vb{};
    vb.ByteWidth=3*12;vb.Usage=D3D11_USAGE_DEFAULT;vb.BindFlags=D3D11_BIND_VERTEX_BUFFER;
    if(FAILED(device->CreateBuffer(&vb,nullptr,&vertex_buffer))) return false;
    D3D11_BUFFER_DESC ib{};
    ib.ByteWidth=3*2;ib.Usage=D3D11_USAGE_DEFAULT;ib.BindFlags=D3D11_BIND_INDEX_BUFFER;
    if(FAILED(device->CreateBuffer(&ib,nullptr,&index_buffer))) return false;
    D3D11_BLEND_DESC blend_desc{};
    blend_desc.RenderTarget[0].BlendEnable=FALSE;
    blend_desc.RenderTarget[0].SrcBlend=D3D11_BLEND_ONE;blend_desc.RenderTarget[0].DestBlend=D3D11_BLEND_ZERO;
    blend_desc.RenderTarget[0].BlendOp=D3D11_BLEND_OP_ADD;blend_desc.RenderTarget[0].SrcBlendAlpha=D3D11_BLEND_ONE;
    blend_desc.RenderTarget[0].DestBlendAlpha=D3D11_BLEND_ZERO;blend_desc.RenderTarget[0].BlendOpAlpha=D3D11_BLEND_OP_ADD;
    blend_desc.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
    if(FAILED(device->CreateBlendState(&blend_desc,&blend_state))) return false;
    D3D11_DEPTH_STENCIL_DESC dd{};
    dd.DepthEnable=TRUE;dd.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;dd.DepthFunc=D3D11_COMPARISON_LESS;
    dd.StencilEnable=FALSE;
    if(FAILED(device->CreateDepthStencilState(&dd,&depth_state))) return false;
    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;rd.ScissorEnable=TRUE;
    return SUCCEEDED(device->CreateRasterizerState(&rd,&rasterizer_state));
}

// PSSetShaderResources(0,1,&srv)
static void probe_ps_resources(ID3D11DeviceContext* c,const char* kind) {
    auto* vt=*reinterpret_cast<void***>(c);
    ID3D11ShaderResourceView* views[1]{srv.Get()};
    __try {reinterpret_cast<void(__stdcall*)(ID3D11DeviceContext*,UINT,UINT,ID3D11ShaderResourceView* const*)>(vt[8])(c,0,1,views);}
    __except(EXCEPTION_EXECUTE_HANDLER) {report(8,"PSSetShaderResources",kind,false);return;}
    ID3D11ShaderResourceView* got{};
    c->PSGetShaderResources(0,1,&got);
    report(8,"PSSetShaderResources",kind,got==srv.Get());
    if(got) got->Release();
}
// PSSetSamplers(0,1,&sampler)
static void probe_ps_samplers(ID3D11DeviceContext* c,const char* kind) {
    auto* vt=*reinterpret_cast<void***>(c);
    ID3D11SamplerState* states[1]{sampler.Get()};
    __try {reinterpret_cast<void(__stdcall*)(ID3D11DeviceContext*,UINT,UINT,ID3D11SamplerState* const*)>(vt[10])(c,0,1,states);}
    __except(EXCEPTION_EXECUTE_HANDLER) {report(10,"PSSetSamplers",kind,false);return;}
    ID3D11SamplerState* got{};
    c->PSGetSamplers(0,1,&got);
    report(10,"PSSetSamplers",kind,got==sampler.Get());
    if(got) got->Release();
}
// PSSetConstantBuffers(0,1,&constant_buffer)
static void probe_ps_constant_buffers(ID3D11DeviceContext* c,const char* kind) {
    auto* vt=*reinterpret_cast<void***>(c);
    ID3D11Buffer* buffers[1]{constant_buffer.Get()};
    __try {reinterpret_cast<void(__stdcall*)(ID3D11DeviceContext*,UINT,UINT,ID3D11Buffer* const*)>(vt[16])(c,0,1,buffers);}
    __except(EXCEPTION_EXECUTE_HANDLER) {report(16,"PSSetConstantBuffers",kind,false);return;}
    ID3D11Buffer* got{};
    c->PSGetConstantBuffers(0,1,&got);
    report(16,"PSSetConstantBuffers",kind,got==constant_buffer.Get());
    if(got) got->Release();
}
// PSSetConstantBuffers1(0,1,&constant_buffer,first=16,num=32), read back through the real Context1 getter.
static void probe_ps_constant_buffers1(ID3D11DeviceContext* c,const char* kind) {
    ID3D11DeviceContext1* c1{};
    if(FAILED(c->QueryInterface(__uuidof(ID3D11DeviceContext1),reinterpret_cast<void**>(&c1))) || !c1) {
        report(123,"PSSetConstantBuffers1",kind,false);return;
    }
    auto* vt=*reinterpret_cast<void***>(c1);
    ID3D11Buffer* buffers[1]{constant_buffer.Get()};
    const UINT first[1]{16},num[1]{32};
    __try {reinterpret_cast<void(__stdcall*)(ID3D11DeviceContext1*,UINT,UINT,ID3D11Buffer* const*,const UINT*,const UINT*)>(vt[123])(c1,0,1,buffers,first,num);}
    __except(EXCEPTION_EXECUTE_HANDLER) {c1->Release();report(123,"PSSetConstantBuffers1",kind,false);return;}
    ID3D11Buffer* got{};
    UINT got_first{},got_num{};
    c1->PSGetConstantBuffers1(0,1,&got,&got_first,&got_num);
    const bool matched=got==constant_buffer.Get() && got_first==16 && got_num==32;
    std::printf("  slot 123 %s readback: buffer=%s first=%u num=%u\n",kind,
        got==constant_buffer.Get()?"same":"different",got_first,got_num);
    report(123,"PSSetConstantBuffers1",kind,matched);
    if(got) got->Release();
    c1->Release();
}
// SwapDeviceContextState: only the immediate context supports it. Bind a PS SRV, swap in a fresh default state through raw slot
// 131, read null, swap the previous state back through the real method, read the SRV again. The deferred context is reported as
// not supported by design (Microsoft: "SwapDeviceContextState is not supported on a deferred context").
static void probe_swap_device_context_state(ID3D11DeviceContext* c,const char* kind) {
    if(c!=immediate.Get()) {std::printf("slot 131  %-42s %-9s %s\n", "SwapDeviceContextState",kind,"NOT SUPPORTED (deferred)");return;}
    ID3D11Device1* device1{};
    const HRESULT device1_hr=device->QueryInterface(__uuidof(ID3D11Device1),reinterpret_cast<void**>(&device1));
    if(FAILED(device1_hr) || !device1) {
        std::printf("  slot 131 %s: ID3D11Device1 QI hr=0x%08X\n",kind,static_cast<unsigned>(device1_hr));
        report(131,"SwapDeviceContextState",kind,false);return;
    }
    ID3D11DeviceContext1* c1{};
    const HRESULT c1_hr=c->QueryInterface(__uuidof(ID3D11DeviceContext1),reinterpret_cast<void**>(&c1));
    if(FAILED(c1_hr) || !c1) {
        std::printf("  slot 131 %s: ID3D11DeviceContext1 QI hr=0x%08X\n",kind,static_cast<unsigned>(c1_hr));
        device1->Release();report(131,"SwapDeviceContextState",kind,false);return;
    }
    const D3D_FEATURE_LEVEL levels[1]{D3D_FEATURE_LEVEL_11_0};
    ID3DDeviceContextState* state{};
    const HRESULT state_hr=device1->CreateDeviceContextState(0,levels,1,D3D11_SDK_VERSION,__uuidof(ID3D11Device),nullptr,&state);
    if(FAILED(state_hr) || !state) {
        std::printf("  slot 131 %s: CreateDeviceContextState hr=0x%08X\n",kind,static_cast<unsigned>(state_hr));
        c1->Release();device1->Release();report(131,"SwapDeviceContextState",kind,false);return;
    }
    ID3D11ShaderResourceView* views[1]{srv.Get()};
    c->PSSetShaderResources(0,1,views);
    ID3DDeviceContextState* previous{};
    auto* vt=*reinterpret_cast<void***>(c1);
    __try {reinterpret_cast<void(__stdcall*)(ID3D11DeviceContext1*,ID3DDeviceContextState*,ID3DDeviceContextState**)>(vt[131])(c1,state,&previous);}
    __except(EXCEPTION_EXECUTE_HANDLER) {
        std::printf("  slot 131 %s: raw call raised exception 0x%08X\n",kind,static_cast<unsigned>(GetExceptionCode()));
        if(previous) previous->Release();state->Release();c1->Release();device1->Release();
        report(131,"SwapDeviceContextState",kind,false);return;
    }
    ID3D11ShaderResourceView* got{};
    c->PSGetShaderResources(0,1,&got);
    const bool cleared=got==nullptr;
    if(got) got->Release();
    const bool had_previous=previous!=nullptr;
    if(previous) {c1->SwapDeviceContextState(previous,nullptr);previous->Release();}
    c->PSGetShaderResources(0,1,&got);
    const bool restored=got==srv.Get();
    if(got) got->Release();
    std::printf("  slot 131 %s: previous=%s cleared=%d restored=%d\n",kind,had_previous?"yes":"no",cleared?1:0,restored?1:0);
    state->Release();c1->Release();device1->Release();
    report(131,"SwapDeviceContextState",kind,cleared && restored);
}
// OMSetRenderTargets(1,&rtv,nullptr)
static void probe_om_targets(ID3D11DeviceContext* c,const char* kind) {
    auto* vt=*reinterpret_cast<void***>(c);
    ID3D11RenderTargetView* targets[1]{render_target.Get()};
    __try {reinterpret_cast<void(__stdcall*)(ID3D11DeviceContext*,UINT,ID3D11RenderTargetView* const*,ID3D11DepthStencilView*)>(vt[33])(c,1,targets,nullptr);}
    __except(EXCEPTION_EXECUTE_HANDLER) {report(33,"OMSetRenderTargets",kind,false);return;}
    ID3D11RenderTargetView* got{};
    c->OMGetRenderTargets(1,&got,nullptr);
    report(33,"OMSetRenderTargets",kind,got==render_target.Get());
    if(got) got->Release();
}
// OMSetRenderTargetsAndUnorderedAccessViews(1,&rtv,nullptr,1,1,&uav,nullptr)
static void probe_om_targets_uav(ID3D11DeviceContext* c,const char* kind) {
    auto* vt=*reinterpret_cast<void***>(c);
    ID3D11RenderTargetView* targets[1]{render_target.Get()};
    ID3D11UnorderedAccessView* uavs[1]{uav.Get()};
    __try {reinterpret_cast<void(__stdcall*)(ID3D11DeviceContext*,UINT,ID3D11RenderTargetView* const*,ID3D11DepthStencilView*,
            UINT,UINT,ID3D11UnorderedAccessView* const*,const UINT*)>(vt[34])(c,1,targets,nullptr,1,1,uavs,nullptr);}
    __except(EXCEPTION_EXECUTE_HANDLER) {report(34,"OMSetRenderTargetsAndUnorderedAccessViews",kind,false);return;}
    ID3D11RenderTargetView* got{};
    ID3D11UnorderedAccessView* got_uav{};
    c->OMGetRenderTargetsAndUnorderedAccessViews(1,&got,nullptr,1,1,&got_uav);
    report(34,"OMSetRenderTargetsAndUnorderedAccessViews",kind,got==render_target.Get() && got_uav==uav.Get());
    if(got) got->Release();
    if(got_uav) got_uav->Release();
    // The same resource may not stay bound as an OM UAV while the CS probes bind it as a CS UAV.
    ID3D11UnorderedAccessView* none[1]{nullptr};
    c->OMSetRenderTargetsAndUnorderedAccessViews(1,const_cast<ID3D11RenderTargetView* const*>(targets),nullptr,1,1,none,nullptr);
}
// SOSetTargets(1,&stream_out,nullptr)
static void probe_so_targets(ID3D11DeviceContext* c,const char* kind) {
    auto* vt=*reinterpret_cast<void***>(c);
    ID3D11Buffer* buffers[1]{stream_out.Get()};
    __try {reinterpret_cast<void(__stdcall*)(ID3D11DeviceContext*,UINT,ID3D11Buffer* const*,const UINT*)>(vt[37])(c,1,buffers,nullptr);}
    __except(EXCEPTION_EXECUTE_HANDLER) {report(37,"SOSetTargets",kind,false);return;}
    ID3D11Buffer* got{};
    c->SOGetTargets(1,&got);
    report(37,"SOSetTargets",kind,got==stream_out.Get());
    if(got) got->Release();
}
// CSSetUnorderedAccessViews(0,1,&uav,nullptr)
static void probe_cs_uavs(ID3D11DeviceContext* c,const char* kind) {
    auto* vt=*reinterpret_cast<void***>(c);
    ID3D11UnorderedAccessView* uavs[1]{uav.Get()};
    __try {reinterpret_cast<void(__stdcall*)(ID3D11DeviceContext*,UINT,UINT,ID3D11UnorderedAccessView* const*,const UINT*)>(vt[68])(c,0,1,uavs,nullptr);}
    __except(EXCEPTION_EXECUTE_HANDLER) {report(68,"CSSetUnorderedAccessViews",kind,false);return;}
    ID3D11UnorderedAccessView* got{};
    c->CSGetUnorderedAccessViews(0,1,&got);
    report(68,"CSSetUnorderedAccessViews",kind,got==uav.Get());
    if(got) got->Release();
}
// ClearState() - proved by binding a PS SRV through the interface, clearing through the slot, and reading null back.
static void probe_clear_state(ID3D11DeviceContext* c,const char* kind) {
    auto* vt=*reinterpret_cast<void***>(c);
    ID3D11ShaderResourceView* views[1]{srv.Get()};
    c->PSSetShaderResources(0,1,views);
    __try {reinterpret_cast<void(__stdcall*)(ID3D11DeviceContext*)>(vt[110])(c);}
    __except(EXCEPTION_EXECUTE_HANDLER) {report(110,"ClearState",kind,false);return;}
    ID3D11ShaderResourceView* got{};
    c->PSGetShaderResources(0,1,&got);
    report(110,"ClearState",kind,got==nullptr);
    if(got) got->Release();
}
// v997 wide-census slots. Each is called through its raw vtable entry with a known value and read back through the real Get.
// VSSetShaderResources(3,1,&srv_control)
static void probe_vs_shader_resources(ID3D11DeviceContext* c,const char* kind) {
    auto* vt=*reinterpret_cast<void***>(c);
    ID3D11ShaderResourceView* views[1]{srv_control.Get()};
    __try {reinterpret_cast<void(__stdcall*)(ID3D11DeviceContext*,UINT,UINT,ID3D11ShaderResourceView* const*)>(vt[25])(c,3,1,views);}
    __except(EXCEPTION_EXECUTE_HANDLER) {report(25,"VSSetShaderResources",kind,false);return;}
    ID3D11ShaderResourceView* got{};
    c->VSGetShaderResources(3,1,&got);
    report(25,"VSSetShaderResources",kind,got==srv_control.Get());
    if(got) got->Release();
}
// VSSetSamplers(2,1,&sampler)
static void probe_vs_samplers(ID3D11DeviceContext* c,const char* kind) {
    auto* vt=*reinterpret_cast<void***>(c);
    ID3D11SamplerState* states[1]{sampler.Get()};
    __try {reinterpret_cast<void(__stdcall*)(ID3D11DeviceContext*,UINT,UINT,ID3D11SamplerState* const*)>(vt[26])(c,2,1,states);}
    __except(EXCEPTION_EXECUTE_HANDLER) {report(26,"VSSetSamplers",kind,false);return;}
    ID3D11SamplerState* got{};
    c->VSGetSamplers(2,1,&got);
    report(26,"VSSetSamplers",kind,got==sampler.Get());
    if(got) got->Release();
}
// VSSetConstantBuffers(1,1,&constant_buffer) - ObjectMotion's slot 7
static void probe_vs_constant_buffers(ID3D11DeviceContext* c,const char* kind) {
    auto* vt=*reinterpret_cast<void***>(c);
    ID3D11Buffer* buffers[1]{constant_buffer.Get()};
    __try {reinterpret_cast<void(__stdcall*)(ID3D11DeviceContext*,UINT,UINT,ID3D11Buffer* const*)>(vt[7])(c,1,1,buffers);}
    __except(EXCEPTION_EXECUTE_HANDLER) {report(7,"VSSetConstantBuffers",kind,false);return;}
    ID3D11Buffer* got{};
    c->VSGetConstantBuffers(1,1,&got);
    report(7,"VSSetConstantBuffers",kind,got==constant_buffer.Get());
    if(got) got->Release();
}
// VSSetConstantBuffers1(1,1,&constant_buffer,first=16,num=32) - ObjectMotion's slot 119, offsets included
static void probe_vs_constant_buffers1(ID3D11DeviceContext* c,const char* kind) {
    ID3D11DeviceContext1* c1{};
    if(FAILED(c->QueryInterface(__uuidof(ID3D11DeviceContext1),reinterpret_cast<void**>(&c1))) || !c1) {
        report(119,"VSSetConstantBuffers1",kind,false);return;
    }
    auto* vt=*reinterpret_cast<void***>(c1);
    ID3D11Buffer* buffers[1]{constant_buffer.Get()};
    const UINT first[1]{16},num[1]{32};
    __try {reinterpret_cast<void(__stdcall*)(ID3D11DeviceContext1*,UINT,UINT,ID3D11Buffer* const*,const UINT*,const UINT*)>(vt[119])(c1,1,1,buffers,first,num);}
    __except(EXCEPTION_EXECUTE_HANDLER) {c1->Release();report(119,"VSSetConstantBuffers1",kind,false);return;}
    ID3D11Buffer* got{};
    UINT got_first{},got_num{};
    c1->VSGetConstantBuffers1(1,1,&got,&got_first,&got_num);
    const bool matched=got==constant_buffer.Get() && got_first==16 && got_num==32;
    std::printf("  slot 119 %s readback: buffer=%s first=%u num=%u\n",kind,
        got==constant_buffer.Get()?"same":"different",got_first,got_num);
    report(119,"VSSetConstantBuffers1",kind,matched);
    if(got) got->Release();
    c1->Release();
}
// PSSetShader(ps,nullptr,0)
static void probe_ps_shader(ID3D11DeviceContext* c,const char* kind) {
    auto* vt=*reinterpret_cast<void***>(c);
    __try {reinterpret_cast<void(__stdcall*)(ID3D11DeviceContext*,ID3D11PixelShader*,ID3D11ClassInstance* const*,UINT)>(vt[9])(c,pixel_shader.Get(),nullptr,0);}
    __except(EXCEPTION_EXECUTE_HANDLER) {report(9,"PSSetShader",kind,false);return;}
    ID3D11PixelShader* got{};
    c->PSGetShader(&got,nullptr,nullptr);
    report(9,"PSSetShader",kind,got==pixel_shader.Get());
    if(got) got->Release();
}
// VSSetShader(vs,nullptr,0) - ObjectMotion's slot 11
static void probe_vs_shader(ID3D11DeviceContext* c,const char* kind) {
    auto* vt=*reinterpret_cast<void***>(c);
    __try {reinterpret_cast<void(__stdcall*)(ID3D11DeviceContext*,ID3D11VertexShader*,ID3D11ClassInstance* const*,UINT)>(vt[11])(c,vertex_shader.Get(),nullptr,0);}
    __except(EXCEPTION_EXECUTE_HANDLER) {report(11,"VSSetShader",kind,false);return;}
    ID3D11VertexShader* got{};
    c->VSGetShader(&got,nullptr,nullptr);
    report(11,"VSSetShader",kind,got==vertex_shader.Get());
    if(got) got->Release();
}
// IASetInputLayout(input_layout)
static void probe_ia_input_layout(ID3D11DeviceContext* c,const char* kind) {
    auto* vt=*reinterpret_cast<void***>(c);
    __try {reinterpret_cast<void(__stdcall*)(ID3D11DeviceContext*,ID3D11InputLayout*)>(vt[17])(c,input_layout.Get());}
    __except(EXCEPTION_EXECUTE_HANDLER) {report(17,"IASetInputLayout",kind,false);return;}
    ID3D11InputLayout* got{};
    c->IAGetInputLayout(&got);
    report(17,"IASetInputLayout",kind,got==input_layout.Get());
    if(got) got->Release();
}
// IASetVertexBuffers(0,1,&vertex_buffer,stride=12,offset=0): the readback must match buffer, stride and offset.
static void probe_ia_vertex_buffers(ID3D11DeviceContext* c,const char* kind) {
    auto* vt=*reinterpret_cast<void***>(c);
    ID3D11Buffer* buffers[1]{vertex_buffer.Get()};
    const UINT stride[1]{12},offset[1]{0};
    __try {reinterpret_cast<void(__stdcall*)(ID3D11DeviceContext*,UINT,UINT,ID3D11Buffer* const*,const UINT*,const UINT*)>(vt[18])(c,0,1,buffers,stride,offset);}
    __except(EXCEPTION_EXECUTE_HANDLER) {report(18,"IASetVertexBuffers",kind,false);return;}
    ID3D11Buffer* got{};
    UINT got_stride{},got_offset{};
    c->IAGetVertexBuffers(0,1,&got,&got_stride,&got_offset);
    report(18,"IASetVertexBuffers",kind,got==vertex_buffer.Get() && got_stride==12 && got_offset==0);
    if(got) got->Release();
}
// IASetIndexBuffer(index_buffer,R16_UINT,offset=0): buffer, format and offset.
static void probe_ia_index_buffer(ID3D11DeviceContext* c,const char* kind) {
    auto* vt=*reinterpret_cast<void***>(c);
    __try {reinterpret_cast<void(__stdcall*)(ID3D11DeviceContext*,ID3D11Buffer*,DXGI_FORMAT,UINT)>(vt[19])(c,index_buffer.Get(),DXGI_FORMAT_R16_UINT,0);}
    __except(EXCEPTION_EXECUTE_HANDLER) {report(19,"IASetIndexBuffer",kind,false);return;}
    ID3D11Buffer* got{};
    DXGI_FORMAT got_format{};UINT got_offset{};
    c->IAGetIndexBuffer(&got,&got_format,&got_offset);
    report(19,"IASetIndexBuffer",kind,got==index_buffer.Get() && got_format==DXGI_FORMAT_R16_UINT && got_offset==0);
    if(got) got->Release();
}
// IASetPrimitiveTopology(TRIANGLESTRIP)
static void probe_ia_primitive_topology(ID3D11DeviceContext* c,const char* kind) {
    auto* vt=*reinterpret_cast<void***>(c);
    __try {reinterpret_cast<void(__stdcall*)(ID3D11DeviceContext*,D3D11_PRIMITIVE_TOPOLOGY)>(vt[24])(c,D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);}
    __except(EXCEPTION_EXECUTE_HANDLER) {report(24,"IASetPrimitiveTopology",kind,false);return;}
    D3D11_PRIMITIVE_TOPOLOGY got{};
    c->IAGetPrimitiveTopology(&got);
    report(24,"IASetPrimitiveTopology",kind,got==D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
}
// OMSetBlendState(blend_state,factor,mask): state, all four blend-factor values and the sample mask.
static void probe_om_blend_state(ID3D11DeviceContext* c,const char* kind) {
    auto* vt=*reinterpret_cast<void***>(c);
    const FLOAT factor[4]{0.25f,0.5f,0.75f,1.f};
    __try {reinterpret_cast<void(__stdcall*)(ID3D11DeviceContext*,ID3D11BlendState*,const FLOAT*,UINT)>(vt[35])(c,blend_state.Get(),factor,0x12345678u);}
    __except(EXCEPTION_EXECUTE_HANDLER) {report(35,"OMSetBlendState",kind,false);return;}
    ID3D11BlendState* got{};
    FLOAT got_factor[4]{};UINT got_mask{};
    c->OMGetBlendState(&got,got_factor,&got_mask);
    report(35,"OMSetBlendState",kind,got==blend_state.Get() && got_mask==0x12345678u &&
        std::memcmp(factor,got_factor,sizeof(factor))==0);
    if(got) got->Release();
}
// OMSetDepthStencilState(depth_state,7): state and stencil reference.
static void probe_om_depth_stencil_state(ID3D11DeviceContext* c,const char* kind) {
    auto* vt=*reinterpret_cast<void***>(c);
    __try {reinterpret_cast<void(__stdcall*)(ID3D11DeviceContext*,ID3D11DepthStencilState*,UINT)>(vt[36])(c,depth_state.Get(),7);}
    __except(EXCEPTION_EXECUTE_HANDLER) {report(36,"OMSetDepthStencilState",kind,false);return;}
    ID3D11DepthStencilState* got{};
    UINT got_ref{};
    c->OMGetDepthStencilState(&got,&got_ref);
    report(36,"OMSetDepthStencilState",kind,got==depth_state.Get() && got_ref==7);
    if(got) got->Release();
}
// RSSetState(rasterizer_state)
static void probe_rs_state(ID3D11DeviceContext* c,const char* kind) {
    auto* vt=*reinterpret_cast<void***>(c);
    __try {reinterpret_cast<void(__stdcall*)(ID3D11DeviceContext*,ID3D11RasterizerState*)>(vt[43])(c,rasterizer_state.Get());}
    __except(EXCEPTION_EXECUTE_HANDLER) {report(43,"RSSetState",kind,false);return;}
    ID3D11RasterizerState* got{};
    c->RSGetState(&got);
    report(43,"RSSetState",kind,got==rasterizer_state.Get());
    if(got) got->Release();
}
// RSSetScissorRects(2,rects): the full rect array.
static void probe_rs_scissor_rects(ID3D11DeviceContext* c,const char* kind) {
    auto* vt=*reinterpret_cast<void***>(c);
    const D3D11_RECT rects[2]{{0,0,4,4},{1,2,3,4}};
    __try {reinterpret_cast<void(__stdcall*)(ID3D11DeviceContext*,UINT,const D3D11_RECT*)>(vt[45])(c,2,rects);}
    __except(EXCEPTION_EXECUTE_HANDLER) {report(45,"RSSetScissorRects",kind,false);return;}
    UINT count{2};D3D11_RECT got[2]{};
    c->RSGetScissorRects(&count,got);
    report(45,"RSSetScissorRects",kind,count==2 && std::memcmp(rects,got,sizeof(rects))==0);
}
int main() {
    std::setvbuf(stdout,nullptr,_IONBF,0);
    if(!make_objects()) {std::printf("cannot create the WARP device or its test objects\n");return 1;}
    ID3D11DeviceContext* contexts[2]{immediate.Get(),deferred.Get()};
    const char* kinds[2]{"immediate","deferred"};
    for(int i=0;i<2;++i) {
        ID3D11DeviceContext* c=contexts[i];
        const char* kind=kinds[i];
        probe_ps_resources(c,kind);
        probe_ps_samplers(c,kind);
        probe_ps_constant_buffers(c,kind);
        probe_ps_constant_buffers1(c,kind);
        probe_swap_device_context_state(c,kind);
        probe_om_targets(c,kind);
        probe_om_targets_uav(c,kind);
        probe_so_targets(c,kind);
        probe_cs_uavs(c,kind);
        probe_clear_state(c,kind);
        // v997 wide census
        probe_vs_shader_resources(c,kind);
        probe_vs_samplers(c,kind);
        probe_vs_constant_buffers(c,kind);
        probe_vs_constant_buffers1(c,kind);
        probe_ps_shader(c,kind);
        probe_vs_shader(c,kind);
        probe_ia_input_layout(c,kind);
        probe_ia_vertex_buffers(c,kind);
        probe_ia_index_buffer(c,kind);
        probe_ia_primitive_topology(c,kind);
        probe_om_blend_state(c,kind);
        probe_om_depth_stencil_state(c,kind);
        probe_rs_state(c,kind);
        probe_rs_scissor_rects(c,kind);
        // Slot 22 is not probed: an arbitrary wrong slot can corrupt the WARP context even under SEH, and it is not one of
        // the slots the mod hooks.
    }
    auto* immediate_vt=*reinterpret_cast<void***>(immediate.Get());
    auto* deferred_vt=*reinterpret_cast<void***>(deferred.Get());
    std::printf("\nshared implementations between immediate and deferred:\n");
    const unsigned slots[24]{7u,8u,9u,10u,11u,16u,17u,18u,19u,24u,25u,26u,33u,34u,35u,36u,37u,43u,45u,68u,110u,119u,123u,131u};
    for(unsigned slot: slots)
        std::printf("  slot %3u: %s\n",slot,immediate_vt[slot]==deferred_vt[slot]?"same":"different");
    std::printf("\n%d matched, %d rejected\n",passed,failed);
    if(failed) {std::printf("FAIL: a required slot did not prove; it must stay unhooked\n");return 1;}
    std::printf("PASS: every required slot proved on the immediate and deferred contexts\n");
    return 0;
}
