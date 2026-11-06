// v993 WARP lab for the PS state census/elision model. Everything here runs on a WARP device, so a wrong barrier rule shows as a
// wrong pixel, not as a headset failure.
//
// On a DEFERRED recording context:
//   (a) a redundant PSSetShaderResources rebind is elided and PSGetShaderResources reads the identical value back;
//   (b) after FinishCommandList(FALSE) the first identical bind in the next recording is NOT elided;
//   (c) binding a texture as a render target unbinds its SRV, so the next identical PSSetShaderResources is not elided;
//   (d) the same after CSSetUnorderedAccessViews;
//   (e) the same after ClearState.
// Then both recorded lists are executed on the immediate context and a 1x1 draw samples the bound SRV; the pixel must equal the
// pixel of the same sequence with elision off.
// On the IMMEDIATE context: ExecuteCommandList(FALSE) followed by the same targets and the same PS state must not be elided.
#include "../src/ps_state_census.hpp"
#include "../src/elision_ab.hpp"
#include "../src/wide_state_census.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <vector>

using Microsoft::WRL::ComPtr;
using Census=d2vr::PsStateCensus;
using Wide=d2vr::WideStateCensus;

static void require(bool passed,const char* message) {if(!passed) throw std::runtime_error(message);}

namespace {
struct Lab {
    Census census;
    unsigned skipped{};
    bool elision{};
    Lab(bool on,bool deferred_ready,bool immediate_ready) : elision(on) {
        census.set_elision(on);
        census.set_barrier_ready(Census::deferred,deferred_ready,deferred_ready?"":"test: deferred barrier missing");
        census.set_barrier_ready(Census::immediate,immediate_ready,immediate_ready?"":"test: immediate barrier missing");
    }
    unsigned kind_of(ID3D11DeviceContext* c) {
        return c==immediate_ ? Census::immediate : Census::deferred;
    }
    ID3D11DeviceContext* immediate_{};
    // Mirrors the hooks: ask the census with the final arguments, forward only when it says no. The phase is 0 throughout: this
    // lab is about the pass barriers, not the per-view phase.
    bool set_resources(ID3D11DeviceContext* c,UINT start,UINT count,ID3D11ShaderResourceView* const* views) {
        const bool skip=census.consider(c,Census::resources,start,count,reinterpret_cast<void* const*>(views),0,kind_of(c));
        if(!skip) c->PSSetShaderResources(start,count,views);
        else ++skipped;
        return !skip;
    }
    bool set_samplers(ID3D11DeviceContext* c,UINT start,UINT count,ID3D11SamplerState* const* samplers) {
        const bool skip=census.consider(c,Census::samplers,start,count,reinterpret_cast<void* const*>(samplers),0,kind_of(c));
        if(!skip) c->PSSetSamplers(start,count,samplers);
        else ++skipped;
        return !skip;
    }
    bool set_constant_buffers(ID3D11DeviceContext* c,UINT start,UINT count,ID3D11Buffer* const* buffers) {
        const bool skip=census.consider(c,Census::constant_buffers,start,count,reinterpret_cast<void* const*>(buffers),0,kind_of(c));
        if(!skip) c->PSSetConstantBuffers(start,count,buffers);
        else ++skipped;
        return !skip;
    }
    // v995: PSSetConstantBuffers1 is never offered to the census: it is always forwarded, and the observation marks its
    // covered PS constant-buffer slots unknown. Mirrors PsConstantBufferHook::cb1_hook.
    void set_constant_buffers1(ID3D11DeviceContext1* c,UINT start,UINT count,ID3D11Buffer* const* buffers,const UINT* first,const UINT* num) {
        census.note_constant_buffers1(reinterpret_cast<ID3D11DeviceContext*>(c),start,count,kind_of(reinterpret_cast<ID3D11DeviceContext*>(c)));
        c->PSSetConstantBuffers1(start,count,buffers,first,num);
    }
    // The barrier each owning hook emits.
    void barrier(ID3D11DeviceContext* c) {census.barrier(c);}
};

// v998: wide elision readiness is the PS census's readiness in production; the WARP lab supplies the same shape so it can prove
// both "ready -> repeat skips" and "not ready -> never skips".
bool wide_ready[Wide::kind_count]{true,true};
bool wide_ready_source(unsigned kind) noexcept {return kind<Wide::kind_count && wide_ready[kind];}
const char* wide_reason_source(unsigned kind) noexcept {
    return kind<Wide::kind_count && !wide_ready[kind]?"test: wide barrier not ready":"";
}

// The wide filter's call path, one wrapper per hooked setter: ask the census with the final arguments, forward only when it says
// no, and count the skip. This is the same shape dxgi_probe/WideStateCensus::*_hook and ObjectMotion's filters use.
struct WideLab {
    Wide wide;
    unsigned skipped{};
    unsigned bookkeeping{};
    bool elision{};
    ID3D11DeviceContext* immediate_{};
    explicit WideLab(bool on) : elision(on) {wide.set_enabled(true);wide.set_elision(on);}
    unsigned kind_of(ID3D11DeviceContext* c) const noexcept {return c==immediate_?Wide::immediate:Wide::deferred;}
    template<class F> bool forward(bool skip,F&& f) {
        if(skip) {++skipped;return false;}
        f();return true;
    }
    bool topology(ID3D11DeviceContext* c,D3D11_PRIMITIVE_TOPOLOGY v) {
        return forward(wide.observe_ia_primitive_topology(c,v,0),[&] {c->IASetPrimitiveTopology(v);});
    }
    bool layout(ID3D11DeviceContext* c,ID3D11InputLayout* v) {
        return forward(wide.observe_ia_input_layout(c,v,0),[&] {c->IASetInputLayout(v);});
    }
    bool vs_srv(ID3D11DeviceContext* c,UINT start,UINT count,ID3D11ShaderResourceView* const* v) {
        return forward(wide.observe_vs_shader_resources(c,start,count,v,0),[&] {c->VSSetShaderResources(start,count,v);});
    }
    bool vs_samplers(ID3D11DeviceContext* c,UINT start,UINT count,ID3D11SamplerState* const* v) {
        return forward(wide.observe_vs_samplers(c,start,count,v,0),[&] {c->VSSetSamplers(start,count,v);});
    }
    // ObjectMotion owns 7/11: its bookkeeping runs first (the counters below model it), then the filter decision, then the
    // forward. A skipped call still increments bookkeeping.
    bool vs_shader(ID3D11DeviceContext* c,ID3D11VertexShader* v) {
        ++bookkeeping;
        return forward(wide.observe_vs_shader(c,v,nullptr,0,0),[&] {c->VSSetShader(v,nullptr,0);});
    }
    bool ps_shader(ID3D11DeviceContext* c,ID3D11PixelShader* v) {
        return forward(wide.observe_ps_shader(c,v,nullptr,0,0),[&] {c->PSSetShader(v,nullptr,0);});
    }
    bool vs_cb(ID3D11DeviceContext* c,UINT start,UINT count,ID3D11Buffer* const* v) {
        ++bookkeeping;
        return forward(wide.observe_vs_constant_buffers(c,start,count,v,0),[&] {c->VSSetConstantBuffers(start,count,v);});
    }
    bool vs_cb1(ID3D11DeviceContext1* c,UINT start,UINT count,ID3D11Buffer* const* v,const UINT* first,const UINT* num) {
        wide.observe_vs_constant_buffers1(reinterpret_cast<ID3D11DeviceContext*>(c),start,count,v,first,num,0);
        c->VSSetConstantBuffers1(start,count,v,first,num); // NEVER elided
        return true;
    }
    bool vb(ID3D11DeviceContext* c,UINT start,UINT count,ID3D11Buffer* const* v,const UINT* strides,const UINT* offsets) {
        return forward(wide.observe_ia_vertex_buffers(c,start,count,v,strides,offsets,0),
            [&] {c->IASetVertexBuffers(start,count,v,strides,offsets);});
    }
    bool ib(ID3D11DeviceContext* c,ID3D11Buffer* v,DXGI_FORMAT format,UINT offset) {
        return forward(wide.observe_ia_index_buffer(c,v,format,offset,0),[&] {c->IASetIndexBuffer(v,format,offset);});
    }
    bool blend(ID3D11DeviceContext* c,ID3D11BlendState* v,const FLOAT* factor,UINT mask) {
        return forward(wide.observe_om_blend_state(c,v,factor,mask,0),[&] {c->OMSetBlendState(v,factor,mask);});
    }
    bool depth(ID3D11DeviceContext* c,ID3D11DepthStencilState* v,UINT ref) {
        return forward(wide.observe_om_depth_stencil_state(c,v,ref,0),[&] {c->OMSetDepthStencilState(v,ref);});
    }
    bool raster(ID3D11DeviceContext* c,ID3D11RasterizerState* v) {
        return forward(wide.observe_rs_state(c,v,0),[&] {c->RSSetState(v);});
    }
    bool scissor(ID3D11DeviceContext* c,UINT count,const D3D11_RECT* rects) {
        return forward(wide.observe_rs_scissor_rects(c,count,rects,0),[&] {c->RSSetScissorRects(count,rects);});
    }
    void barrier(ID3D11DeviceContext* c) {wide.barrier(c);}
};

bool srv_is_bound(ID3D11DeviceContext* c,UINT slot,ID3D11ShaderResourceView* expected) {
    ID3D11ShaderResourceView* view{};
    c->PSGetShaderResources(slot,1,&view);
    const bool matches=view==expected;
    if(view) view->Release();
    return matches;
}

struct Scene {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> immediate;
    ComPtr<ID3D11ShaderResourceView> red, blue;
    ComPtr<ID3D11RenderTargetView> red_rtv;
    ComPtr<ID3D11UnorderedAccessView> blue_uav;
    ComPtr<ID3D11SamplerState> sampler;
    ComPtr<ID3D11Texture2D> output, staging;
    ComPtr<ID3D11RenderTargetView> output_rtv;
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11Buffer> constants;       // v995 CB1 lab: 32 constants; 0..15 red, 16..31 blue
    ComPtr<ID3D11PixelShader> color_ps;  // v995 CB1 lab: returns cb0[0]
    // v997 wide-census lab objects.
    ComPtr<ID3D11Buffer> vertex_buffer,index_buffer;
    ComPtr<ID3D11Buffer> stream_output_buffer; // v998 SO hazard: binding it as output is a full wide barrier
    ComPtr<ID3D11VertexShader> input_vs;
    ComPtr<ID3D11InputLayout> input_layout;
    ComPtr<ID3D11BlendState> blend_state;
    ComPtr<ID3D11DepthStencilState> depth_state;
    ComPtr<ID3D11RasterizerState> rasterizer_state;
};

bool make_view_texture(ID3D11Device* device,UINT colour,UINT bind,ID3D11Texture2D** texture,ID3D11ShaderResourceView** srv) {
    D3D11_TEXTURE2D_DESC d{};
    d.Width=1;d.Height=1;d.MipLevels=1;d.ArraySize=1;d.Format=DXGI_FORMAT_R8G8B8A8_UNORM;d.SampleDesc.Count=1;
    d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=bind;
    const std::uint32_t pixel=colour;
    D3D11_SUBRESOURCE_DATA init{&pixel,4,0};
    if(FAILED(device->CreateTexture2D(&d,&init,texture))) return false;
    D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format=d.Format;sd.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;sd.Texture2D.MipLevels=1;
    return SUCCEEDED(device->CreateShaderResourceView(*texture,&sd,srv));
}

bool compile_shaders(ID3D11Device* device,Scene& scene) {
    static const char* source=R"(
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
VSOut VSMain(uint id : SV_VertexID) {
    VSOut o;
    float2 p = float2((id << 1) & 2, id & 2);
    o.pos = float4(p * float2(2,-2) + float2(-1,1), 0, 1);
    o.uv = p;
    return o;
}
struct VSIn { float3 p : POSITION; };
VSOut VSInput(VSIn i) { VSOut o; o.pos = float4(i.p, 1); o.uv = 0; return o; }
Texture2D tex : register(t0);
SamplerState samp : register(s0);
float4 PSMain(VSOut i) : SV_Target { return tex.Sample(samp, i.uv); }
cbuffer PSConstants : register(b0) { float4 tint; };
float4 PSColor(VSOut i) : SV_Target { return tint; }
)";
    ComPtr<ID3DBlob> vs_blob,ps_blob,color_blob,input_blob,error;
    if(FAILED(D3DCompile(source,std::strlen(source),"census_lab.hlsl",nullptr,nullptr,"VSMain","vs_5_0",0,0,&vs_blob,&error))) return false;
    error.Reset();
    if(FAILED(D3DCompile(source,std::strlen(source),"census_lab.hlsl",nullptr,nullptr,"PSMain","ps_5_0",0,0,&ps_blob,&error))) return false;
    error.Reset();
    if(FAILED(D3DCompile(source,std::strlen(source),"census_lab.hlsl",nullptr,nullptr,"PSColor","ps_5_0",0,0,&color_blob,&error))) return false;
    error.Reset();
    if(FAILED(D3DCompile(source,std::strlen(source),"census_lab.hlsl",nullptr,nullptr,"VSInput","vs_5_0",0,0,&input_blob,&error))) return false;
    if(FAILED(device->CreateVertexShader(vs_blob->GetBufferPointer(),vs_blob->GetBufferSize(),nullptr,&scene.vs))) return false;
    if(FAILED(device->CreatePixelShader(ps_blob->GetBufferPointer(),ps_blob->GetBufferSize(),nullptr,&scene.ps))) return false;
    if(FAILED(device->CreateVertexShader(input_blob->GetBufferPointer(),input_blob->GetBufferSize(),nullptr,&scene.input_vs))) return false;
    const D3D11_INPUT_ELEMENT_DESC element{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0};
    if(FAILED(device->CreateInputLayout(&element,1,input_blob->GetBufferPointer(),input_blob->GetBufferSize(),&scene.input_layout))) return false;
    return SUCCEEDED(device->CreatePixelShader(color_blob->GetBufferPointer(),color_blob->GetBufferSize(),nullptr,&scene.color_ps));
}

bool make_scene(Scene& scene) {
    if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,
        &scene.device,nullptr,&scene.immediate))) return false;
    ComPtr<ID3D11Texture2D> red_texture,blue_texture;
    if(!make_view_texture(scene.device.Get(),0xFF0000FFu,D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET,red_texture.GetAddressOf(),scene.red.GetAddressOf())) return false;   // RGBA8: R=255,G=0,B=0,A=255
    if(!make_view_texture(scene.device.Get(),0xFFFF0000u,D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS,blue_texture.GetAddressOf(),scene.blue.GetAddressOf())) return false;  // RGBA8: R=0,G=0,B=255,A=255
    if(FAILED(scene.device->CreateRenderTargetView(red_texture.Get(),nullptr,&scene.red_rtv))) return false;
    if(FAILED(scene.device->CreateUnorderedAccessView(blue_texture.Get(),nullptr,&scene.blue_uav))) return false;
    D3D11_SAMPLER_DESC sp{};
    sp.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
    sp.AddressU=sp.AddressV=sp.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
    sp.ComparisonFunc=D3D11_COMPARISON_NEVER;sp.MaxLOD=D3D11_FLOAT32_MAX;
    if(FAILED(scene.device->CreateSamplerState(&sp,&scene.sampler))) return false;
    // v995 CB1 lab: 32 constants. Constant 0 is red, constant 16 is blue; a first-constant window of 16 must read blue.
    std::array<float,32*4> constants{};
    constants[0]=1;constants[3]=1;                      // cb0[0] (buffer constant 0) = red
    constants[16*4+2]=1;constants[16*4+3]=1;            // cb0[0] under first=16 (buffer constant 16) = blue
    D3D11_BUFFER_DESC cb{};
    cb.ByteWidth=static_cast<UINT>(constants.size()*sizeof(float));cb.Usage=D3D11_USAGE_DEFAULT;cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    D3D11_SUBRESOURCE_DATA cb_init{constants.data(),0,0};
    if(FAILED(scene.device->CreateBuffer(&cb,&cb_init,&scene.constants))) return false;
    D3D11_TEXTURE2D_DESC out{};
    out.Width=1;out.Height=1;out.MipLevels=1;out.ArraySize=1;out.Format=DXGI_FORMAT_R8G8B8A8_UNORM;out.SampleDesc.Count=1;
    out.Usage=D3D11_USAGE_DEFAULT;out.BindFlags=D3D11_BIND_RENDER_TARGET;
    if(FAILED(scene.device->CreateTexture2D(&out,nullptr,&scene.output))) return false;
    if(FAILED(scene.device->CreateRenderTargetView(scene.output.Get(),nullptr,&scene.output_rtv))) return false;
    auto staging_desc=out;staging_desc.Usage=D3D11_USAGE_STAGING;staging_desc.BindFlags=0;staging_desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    if(FAILED(scene.device->CreateTexture2D(&staging_desc,nullptr,&scene.staging))) return false;
    // v997 wide-census lab objects: a real VB/IB and the three fixed-function states (the census compares exactly these).
    // v998: the VB/IB carry one full-screen triangle so the wide pixel A/B can draw through the input assembler.
    const float vertices[3][3]{{-1.f,-1.f,0.f},{3.f,-1.f,0.f},{-1.f,3.f,0.f}};
    D3D11_BUFFER_DESC vb{};
    vb.ByteWidth=sizeof vertices;vb.Usage=D3D11_USAGE_DEFAULT;vb.BindFlags=D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA vb_init{vertices,0,0};
    if(FAILED(scene.device->CreateBuffer(&vb,&vb_init,&scene.vertex_buffer))) return false;
    const std::uint16_t indices[3]{0,1,2};
    D3D11_BUFFER_DESC ib{};
    ib.ByteWidth=sizeof indices;ib.Usage=D3D11_USAGE_DEFAULT;ib.BindFlags=D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA ib_init{indices,0,0};
    if(FAILED(scene.device->CreateBuffer(&ib,&ib_init,&scene.index_buffer))) return false;
    D3D11_BUFFER_DESC so{};
    so.ByteWidth=16;so.Usage=D3D11_USAGE_DEFAULT;so.BindFlags=D3D11_BIND_STREAM_OUTPUT;
    if(FAILED(scene.device->CreateBuffer(&so,nullptr,&scene.stream_output_buffer))) return false;
    D3D11_BLEND_DESC blend{};
    blend.RenderTarget[0].BlendEnable=FALSE;blend.RenderTarget[0].SrcBlend=D3D11_BLEND_ONE;
    blend.RenderTarget[0].DestBlend=D3D11_BLEND_ZERO;blend.RenderTarget[0].BlendOp=D3D11_BLEND_OP_ADD;
    blend.RenderTarget[0].SrcBlendAlpha=D3D11_BLEND_ONE;blend.RenderTarget[0].DestBlendAlpha=D3D11_BLEND_ZERO;
    blend.RenderTarget[0].BlendOpAlpha=D3D11_BLEND_OP_ADD;blend.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
    if(FAILED(scene.device->CreateBlendState(&blend,&scene.blend_state))) return false;
    D3D11_DEPTH_STENCIL_DESC depth{};
    depth.DepthEnable=FALSE;depth.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;depth.DepthFunc=D3D11_COMPARISON_LESS;
    if(FAILED(scene.device->CreateDepthStencilState(&depth,&scene.depth_state))) return false;
    D3D11_RASTERIZER_DESC raster{};
    raster.FillMode=D3D11_FILL_SOLID;raster.CullMode=D3D11_CULL_NONE;raster.DepthClipEnable=TRUE;raster.ScissorEnable=FALSE;
    if(FAILED(scene.device->CreateRasterizerState(&raster,&scene.rasterizer_state))) return false;
    return compile_shaders(scene.device.Get(),scene);
}

std::uint32_t read_output(Scene& scene) {
    auto* immediate=scene.immediate.Get();
    immediate->CopyResource(scene.staging.Get(),scene.output.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    require(SUCCEEDED(immediate->Map(scene.staging.Get(),0,D3D11_MAP_READ,0,&mapped)),"cannot map the 1x1 readback");
    std::uint32_t pixel{};
    std::memcpy(&pixel,mapped.pData,4);
    immediate->Unmap(scene.staging.Get(),0);
    return pixel;
}

// Runs the whole recorded sequence with elision on or off and returns the pixel the final draw produced.
std::uint32_t run_sequence(Scene& scene,bool elision,unsigned& skips,unsigned& immediate_skips) {
    ComPtr<ID3D11DeviceContext> deferred;
    require(SUCCEEDED(scene.device->CreateDeferredContext(0,&deferred)),"cannot create a deferred context");
    Lab lab(elision,true,true);
    lab.immediate_=scene.immediate.Get();
    ID3D11ShaderResourceView* const red[1]{scene.red.Get()};
    ID3D11ShaderResourceView* const blue[1]{scene.blue.Get()};
    ID3D11RenderTargetView* const red_rtv[1]{scene.red_rtv.Get()};
    ID3D11RenderTargetView* const no_rtv[1]{nullptr};
    ID3D11UnorderedAccessView* const blue_uav[1]{scene.blue_uav.Get()};
    ID3D11UnorderedAccessView* const no_uav[1]{nullptr};

    auto* d=deferred.Get();
    // (a) redundant rebind on the recording context
    require(lab.set_resources(d,0,1,red),"first PS bind was elided");
    const unsigned before=lab.skipped;
    const bool first_repeat=!lab.set_resources(d,0,1,red);
    require(elision?first_repeat:true,"an identical PS rebind on the deferred context was not elided");
    require(elision?lab.skipped==before+1:true,"the skip counter did not move");
    require(srv_is_bound(d,0,scene.red.Get()),"the elided bind left a different SRV bound");
    // (b) FinishCommandList ends the recording stream: the next identical bind must not be elided
    ComPtr<ID3D11CommandList> list1;
    require(SUCCEEDED(deferred->FinishCommandList(FALSE,&list1)),"FinishCommandList failed");
    lab.barrier(d);
    require(lab.set_resources(d,0,1,red),"the first bind after FinishCommandList was elided");
    // (c) binding the texture as a render target unbinds its SRV
    require(lab.set_resources(d,0,1,red)==false || !elision,"the identical rebind before the target bind was not elided");
    deferred->OMSetRenderTargets(1,red_rtv,nullptr);
    lab.barrier(d); // the OM barrier the registry targets_hook emits
    const bool unbound_c=!srv_is_bound(d,0,scene.red.Get());
    require(unbound_c,"the render-target bind did not unbind the overlapping SRV");
    deferred->OMSetRenderTargets(1,no_rtv,nullptr);
    lab.barrier(d);
    const bool forwarded_c=lab.set_resources(d,0,1,red);
    require(!elision || !unbound_c || forwarded_c,"the bind after the SRV was unbound by a target bind was elided");
    require(srv_is_bound(d,0,scene.red.Get()),"the SRV is not bound after the rebind");
    // (d) CSSetUnorderedAccessViews
    require(lab.set_resources(d,0,1,blue),"the CS-UAV setup bind was elided");
    const bool repeat_d=!lab.set_resources(d,0,1,blue);
    require(!elision || repeat_d,"the identical CS-UAV setup rebind was not elided");
    deferred->CSSetUnorderedAccessViews(0,1,blue_uav,nullptr);
    lab.barrier(d); // the CS barrier ContextBarriers emits
    const bool unbound_d=!srv_is_bound(d,0,scene.blue.Get());
    require(unbound_d,"the CS UAV bind did not unbind the overlapping SRV");
    deferred->CSSetUnorderedAccessViews(0,1,no_uav,nullptr);
    lab.barrier(d);
    const bool forwarded_d=lab.set_resources(d,0,1,blue);
    require(!elision || !unbound_d || forwarded_d,"the bind after CSSetUnorderedAccessViews was elided");
    deferred->CSSetUnorderedAccessViews(0,1,no_uav,nullptr);
    lab.barrier(d);
    // (e) ClearState
    deferred->ClearState();
    lab.barrier(d); // the ClearState barrier ContextBarriers emits
    require(lab.set_resources(d,0,1,red),"the bind after ClearState was elided");
    // The list ends in a real draw, so the runtime commits the recorded state when the list executes. WARP does not surface a
    // list PS bind to the immediate context Get method, so the pixel is the only honest check of what the list bound.
    const D3D11_VIEWPORT viewport{0,0,1,1,0,1};
    deferred->OMSetRenderTargets(1,no_rtv,nullptr);
    deferred->OMSetRenderTargets(1,scene.output_rtv.GetAddressOf(),nullptr);
    lab.barrier(d);
    deferred->RSSetViewports(1,&viewport);
    deferred->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    deferred->VSSetShader(scene.vs.Get(),nullptr,0);
    deferred->PSSetShader(scene.ps.Get(),nullptr,0);
    deferred->PSSetSamplers(0,1,scene.sampler.GetAddressOf());
    require(lab.set_resources(d,0,1,red),"the draw PS bind was elided");
    deferred->Draw(3,0);
    ComPtr<ID3D11CommandList> list2;
    require(SUCCEEDED(deferred->FinishCommandList(FALSE,&list2)),"FinishCommandList 2 failed");
    lab.barrier(d);
    skips=lab.skipped;

    auto* immediate=scene.immediate.Get();
    // The execute hook barriers the immediate context before and after every list.
    lab.barrier(immediate);
    immediate->ExecuteCommandList(list1.Get(),FALSE);
    lab.barrier(immediate);
    lab.barrier(immediate);
    immediate->ExecuteCommandList(list2.Get(),FALSE);
    lab.barrier(immediate);
    // Immediate ECL case: the list changed the immediate PS state, so the same bind must not be elided.
    const unsigned immediate_before=lab.skipped;
    require(lab.set_resources(immediate,0,1,red),"the first PS bind on the immediate context was elided");
    const bool repeat=!lab.set_resources(immediate,0,1,red);
    require(elision?repeat:true,"an identical immediate PS rebind was not elided");
    lab.barrier(immediate);
    immediate->ExecuteCommandList(list1.Get(),FALSE);
    lab.barrier(immediate);
    require(lab.set_resources(immediate,0,1,red),"the same PS bind after ExecuteCommandList was elided");
    immediate_skips=lab.skipped-immediate_before;
    return read_output(scene);
}

// v995: the CB1 hazard lab. A base bind of buffer B, then PSSetConstantBuffers1 B with first=16, then base B again. The last
// base call must be forwarded (the CB1 call marks slot 0 unknown), and the pixel each draw produced must match elision off.
struct Cb1Pixels {std::uint32_t base{},window{},after{};unsigned skips{};std::uint64_t cb1_calls{};};
Cb1Pixels run_cb1_sequence(Scene& scene,bool elision) {
    ComPtr<ID3D11DeviceContext> deferred;
    require(SUCCEEDED(scene.device->CreateDeferredContext(0,&deferred)),"cannot create a deferred context for the CB1 lab");
    ComPtr<ID3D11DeviceContext1> deferred1;
    require(SUCCEEDED(deferred.As(&deferred1)),"the deferred context does not expose ID3D11DeviceContext1");
    Lab lab(elision,true,true);
    lab.immediate_=scene.immediate.Get();
    auto* d=deferred.Get();
    auto* d1=deferred1.Get();
    ID3D11Buffer* const buffers[1]{scene.constants.Get()};
    ID3D11RenderTargetView* const target[1]{scene.output_rtv.Get()};
    const D3D11_VIEWPORT viewport{0,0,1,1,0,1};
    auto record_begin=[&] {
        d->OMSetRenderTargets(1,target,nullptr);
        lab.barrier(d); // the OM barrier the registry targets_hook emits
        d->RSSetViewports(1,&viewport);
        d->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        d->VSSetShader(scene.vs.Get(),nullptr,0);
        d->PSSetShader(scene.color_ps.Get(),nullptr,0);
    };
    auto execute=[&](ID3D11CommandList* list) {
        auto* immediate=scene.immediate.Get();
        lab.barrier(immediate);
        immediate->ExecuteCommandList(list,FALSE);
        lab.barrier(immediate);
    };
    Cb1Pixels pixels;
    { // (1) base bind: cb0[0] reads buffer constant 0 = red
        record_begin();
        require(lab.set_constant_buffers(d,0,1,buffers),"the first base CB bind was elided");
        d->Draw(3,0);
        ComPtr<ID3D11CommandList> list;require(SUCCEEDED(deferred->FinishCommandList(FALSE,&list)),"FinishCommandList 1 failed");
        lab.barrier(d);execute(list.Get());pixels.base=read_output(scene);
    }
    { // (2) CB1 with first=16: the same buffer now reads buffer constant 16 = blue, and CB1 is never elided
        record_begin();
        const UINT first[1]{16},num[1]{16};
        lab.set_constant_buffers1(d1,0,1,buffers,first,num);
        ID3D11Buffer* got{};UINT got_first{},got_num{};
        d1->PSGetConstantBuffers1(0,1,&got,&got_first,&got_num);
        require(got==scene.constants.Get() && got_first==16 && got_num==16,"PSSetConstantBuffers1 did not record first/num constants");
        if(got) got->Release();
        d->Draw(3,0);
        ComPtr<ID3D11CommandList> list;require(SUCCEEDED(deferred->FinishCommandList(FALSE,&list)),"FinishCommandList 2 failed");
        lab.barrier(d);execute(list.Get());pixels.window=read_output(scene);
    }
    { // (3) base B, redundant base B (skip), CB1 B offset 16, identical CB1 again (never skipped), base B again. The last
      // base bind must be forwarded: if it were elided the draw would still read the offset window (blue) instead of red.
        record_begin();
        require(lab.set_constant_buffers(d,0,1,buffers),"the base CB bind before the redundant bind was elided");
        const bool redundant_forwarded=lab.set_constant_buffers(d,0,1,buffers);
        require(elision?!redundant_forwarded:redundant_forwarded,elision?"the redundant base CB bind was not elided":"the redundant base CB bind was skipped with elision off");
        const UINT first[1]{16},num[1]{16};
        lab.set_constant_buffers1(d1,0,1,buffers,first,num);
        lab.set_constant_buffers1(d1,0,1,buffers,first,num); // identical CB1: still forwarded
        require(lab.set_constant_buffers(d,0,1,buffers),"the base CB bind after PSSetConstantBuffers1 was elided");
        d->Draw(3,0);
        ComPtr<ID3D11CommandList> list;require(SUCCEEDED(deferred->FinishCommandList(FALSE,&list)),"FinishCommandList 3 failed");
        lab.barrier(d);execute(list.Get());pixels.after=read_output(scene);
    }
    pixels.skips=lab.skipped;
    pixels.cb1_calls=lab.census.cb1_calls(Census::deferred);
    return pixels;
}
// v996: a real immediate SwapDeviceContextState. The swap barriers only the immediate context: the deferred shadow and deferred
// elision readiness must survive it, and the first immediate PS bind after the swap must be forwarded.
bool run_swap_sequence(Scene& scene) {
    ComPtr<ID3D11Device1> device1;
    ComPtr<ID3D11DeviceContext1> immediate1;
    if(FAILED(scene.device.As(&device1)) || FAILED(scene.immediate.As(&immediate1))) return false;
    const D3D_FEATURE_LEVEL levels[1]{D3D_FEATURE_LEVEL_11_0};
    ComPtr<ID3DDeviceContextState> state;
    if(FAILED(device1->CreateDeviceContextState(0,levels,1,D3D11_SDK_VERSION,__uuidof(ID3D11Device),nullptr,&state)) || !state) return false;
    ComPtr<ID3D11DeviceContext> deferred;
    // The census hashes contexts into 64 records; a collision only costs elision, but this lab needs the deferred and immediate
    // streams in different records to prove that an immediate swap leaves the deferred shadow alone.
    for(unsigned attempt=0;attempt<8;++attempt) {
        deferred.Reset();
        require(SUCCEEDED(scene.device->CreateDeferredContext(0,&deferred)),"cannot create a deferred context for the swap lab");
        const auto deferred_slot=(reinterpret_cast<std::uintptr_t>(deferred.Get())>>4)&(Census::context_slots-1u);
        const auto immediate_slot=(reinterpret_cast<std::uintptr_t>(scene.immediate.Get())>>4)&(Census::context_slots-1u);
        if(deferred_slot!=immediate_slot) break;
    }
    Lab lab(true,true,true);
    lab.immediate_=scene.immediate.Get();
    auto* immediate=scene.immediate.Get();
    auto* d=deferred.Get();
    ID3D11ShaderResourceView* const red[1]{scene.red.Get()};
    // Both kinds know red: the deferred and the immediate repeat both elide.
    require(lab.set_resources(d,0,1,red),"the deferred setup bind was elided");
    require(!lab.set_resources(d,0,1,red),"the deferred setup repeat was not elided");
    require(lab.set_resources(immediate,0,1,red),"the immediate setup bind was elided");
    require(!lab.set_resources(immediate,0,1,red),"the immediate setup repeat was not elided");
    // The hook barriers before and after the original call; the census records the call site.
    lab.barrier(immediate);
    ID3DDeviceContextState* previous{};
    immediate1->SwapDeviceContextState(state.Get(),&previous); // the method itself returns void
    require(!srv_is_bound(immediate,0,scene.red.Get()),"the swapped-in state did not clear the immediate PS SRV");
    lab.census.note_device_context_state_swap(immediate,"census_warp_check.exe",0x1234);
    require(lab.census.barrier_ready(Census::immediate),"the immediate kind is not ready after a swap");
    require(lab.census.barrier_ready(Census::deferred),"an immediate swap disabled the deferred kind");
    require(lab.set_resources(immediate,0,1,red),"the first immediate PS bind after the swap was elided");
    require(!lab.set_resources(d,0,1,red),"the immediate swap invalidated the deferred shadow");
    if(previous) immediate1->SwapDeviceContextState(previous,nullptr);
    if(previous) previous->Release();
    require(lab.census.state_swap_count()==1,"the swap was not counted");
    require(lab.census.json().find("\"state_swaps\":{\"count\":1,\"callers\":[{\"module\":\"census_warp_check.exe\",\"rva\":4660,\"count\":1}]}")!=std::string::npos,
        "the swap caller was not recorded");
    return true;
}
// v997 live toggle on a real recording context: start with elision off, toggle on mid-stream (the shadow was maintained while
// off, so the very next identical bind is skipped), verify the skipped call left the same SRV bound, then toggle off again.
bool run_toggle_sequence(Scene& scene) {
    ComPtr<ID3D11DeviceContext> deferred;
    require(SUCCEEDED(scene.device->CreateDeferredContext(0,&deferred)),"cannot create a deferred context for the toggle lab");
    Lab lab(false,true,true); // elision starts OFF
    lab.immediate_=scene.immediate.Get();
    auto* d=deferred.Get();
    ID3D11ShaderResourceView* const red[1]{scene.red.Get()};
    require(lab.set_resources(d,0,1,red),"the off-mode first bind was not forwarded");
    require(lab.set_resources(d,0,1,red),"the off-mode repeat was not forwarded");
    require(lab.census.skipped(Census::deferred,Census::resources)==0,"elision off skipped a deferred bind");
    lab.census.set_elision(true); // mid-session: no re-record, no barrier
    require(!lab.set_resources(d,0,1,red),"the first repeat after switching elision on was not skipped");
    require(lab.census.skipped(Census::deferred,Census::resources)==1,"the mid-session skip was not counted");
    require(srv_is_bound(d,0,scene.red.Get()),"the mid-session elided bind left a different SRV bound");
    lab.census.set_elision(false);
    require(lab.set_resources(d,0,1,red),"the repeat after switching elision off was skipped");
    require(lab.census.skipped(Census::deferred,Census::resources)==1,"elision off kept skipping");
    return true;
}
// v997 A/B windows and histogram: synthetic frames (the model test pins the exact percentiles too), run here so the same binary
// that proves the census also proves the reporting path the headset reads.
bool run_ab_window_lab() {
    auto ab=std::make_unique<d2vr::ElisionAb>();
    for(int i=0;i<4;++i) {
        d2vr::ReplayFrame replay;replay.valid=true;
        replay.total_ns=static_cast<std::uint64_t>(i+1)*1'000'000ull;
        replay.max_list_ns=replay.total_ns;
        replay.hist[0]=1;replay.hist[2]=3; // per-list data must not reach the A/B frame histogram
        ab->feed(false,false,replay,static_cast<std::uint64_t>(10+i)*1'000'000ull,2'000'000,3'000'000,1'000'000,0,
            static_cast<std::uint64_t>(i));
    }
    for(int i=0;i<4;++i) {
        d2vr::ReplayFrame replay;replay.valid=true;
        replay.total_ns=static_cast<std::uint64_t>(i+1)*3'000'000ull;
        replay.max_list_ns=replay.total_ns;
        replay.hist[3]=2;
        ab->feed(true,false,replay,static_cast<std::uint64_t>(20+i)*1'000'000ull,4'000'000,5'000'000,2'000'000,
            static_cast<std::uint64_t>(50+i),static_cast<std::uint64_t>(7+i));
    }
    ab->note_toggle();
    const auto json=ab->json();
    require(json.find("\"ps_off_wide_off\":{\"frames\":4,\"ecl_ms_p50\":2,\"ecl_ms_p90\":4,")!=std::string::npos,"off window percentiles wrong");
    require(json.find("\"ecl_hist\":[0,1,2,1,0,0,0,0]")!=std::string::npos,"off histogram is not a per-frame total histogram");
    require(json.find("\"ps_on_wide_off\":{\"frames\":4,\"ecl_ms_p50\":6,\"ecl_ms_p90\":12,")!=std::string::npos,"on window percentiles wrong");
    require(json.find("\"ecl_hist\":[0,0,1,0,1,1,1,0]")!=std::string::npos,"on histogram is not a per-frame total histogram");
    require(json.find("\"toggles\":1")!=std::string::npos,"the toggle was not counted");
    return true;
}
// v997 wide census on a real deferred WARP context. Every observation is of a REAL object read back through the matching Get,
// including the parameter comparisons: a different VB stride/offset, index format/offset, blend factor/mask, stencil ref and
// scissor rect are distinct state, not repeats.
bool run_wide_census_lab(Scene& scene) {
    ComPtr<ID3D11DeviceContext> deferred;
    require(SUCCEEDED(scene.device->CreateDeferredContext(0,&deferred)),"cannot create a deferred context for the wide lab");
    auto wide=std::make_unique<Wide>();wide->set_enabled(true);
    auto* d=deferred.Get();
    ID3D11ShaderResourceView* const red[1]{scene.red.Get()};
    d->VSSetShaderResources(0,1,red);
    ID3D11ShaderResourceView* got_srv{};
    d->VSGetShaderResources(0,1,&got_srv);
    wide->observe_vs_shader_resources(d,0,1,&got_srv,0);
    wide->observe_vs_shader_resources(d,0,1,&got_srv,0);
    if(got_srv) got_srv->Release();
    require(wide->calls(Wide::vs_shader_resources,Wide::deferred)==2,"VS SRV calls not counted");
    require(wide->repeats(Wide::vs_shader_resources,Wide::deferred)==1,"an identical VS SRV bind was not a repeat");

    d->VSSetConstantBuffers(0,1,scene.constants.GetAddressOf());
    ID3D11Buffer* got_cb{};d->VSGetConstantBuffers(0,1,&got_cb);
    wide->observe_vs_constant_buffers(d,0,1,&got_cb,0);
    wide->observe_vs_constant_buffers(d,0,1,&got_cb,0);
    if(got_cb) got_cb->Release();
    const UINT first16[1]{16},num16[1]{16},first8[1]{8};
    wide->observe_vs_constant_buffers1(d,0,1,scene.constants.GetAddressOf(),first16,num16,0);
    wide->observe_vs_constant_buffers1(d,0,1,scene.constants.GetAddressOf(),first16,num16,0);
    require(wide->repeats(Wide::vs_constant_buffers1,Wide::deferred)==1,"an identical CB1 window was not a repeat");
    wide->observe_vs_constant_buffers1(d,0,1,scene.constants.GetAddressOf(),first8,num16,0);
    require(wide->repeats(Wide::vs_constant_buffers1,Wide::deferred)==1,"a CB1 bind with a different first offset counted as a repeat");
    wide->observe_vs_constant_buffers(d,0,1,scene.constants.GetAddressOf(),0);
    require(wide->repeats(Wide::vs_constant_buffers,Wide::deferred)==1,"a base CB bind after a CB1 window counted as a repeat");

    // IA input layout and vertex buffers, read back from the context.
    d->IASetInputLayout(scene.input_layout.Get());
    ID3D11InputLayout* got_layout{};d->IAGetInputLayout(&got_layout);
    wide->observe_ia_input_layout(d,got_layout,0);
    wide->observe_ia_input_layout(d,got_layout,0);
    if(got_layout) got_layout->Release();
    const UINT stride12[1]{12},stride16[1]{16},offset0[1]{0},offset4[1]{4};
    d->IASetVertexBuffers(0,1,scene.vertex_buffer.GetAddressOf(),stride12,offset0);
    ID3D11Buffer* got_vb{};UINT got_stride{},got_offset{};
    d->IAGetVertexBuffers(0,1,&got_vb,&got_stride,&got_offset);
    wide->observe_ia_vertex_buffers(d,0,1,&got_vb,&got_stride,&got_offset,0);
    wide->observe_ia_vertex_buffers(d,0,1,&got_vb,&got_stride,&got_offset,0);
    if(got_vb) got_vb->Release();
    wide->observe_ia_vertex_buffers(d,0,1,scene.vertex_buffer.GetAddressOf(),stride16,offset0,0);
    require(wide->repeats(Wide::ia_vertex_buffers,Wide::deferred)==1,"a different VB stride counted as a repeat");
    wide->observe_ia_vertex_buffers(d,0,1,scene.vertex_buffer.GetAddressOf(),stride16,offset4,0);
    require(wide->repeats(Wide::ia_vertex_buffers,Wide::deferred)==1,"a different VB offset counted as a repeat");

    d->IASetIndexBuffer(scene.index_buffer.Get(),DXGI_FORMAT_R16_UINT,0);
    ID3D11Buffer* got_ib{};DXGI_FORMAT got_format{};UINT got_index_offset{};
    d->IAGetIndexBuffer(&got_ib,&got_format,&got_index_offset);
    wide->observe_ia_index_buffer(d,got_ib,got_format,got_index_offset,0);
    wide->observe_ia_index_buffer(d,got_ib,got_format,got_index_offset,0);
    if(got_ib) got_ib->Release();
    wide->observe_ia_index_buffer(d,scene.index_buffer.Get(),got_format,4,0);
    require(wide->repeats(Wide::ia_index_buffer,Wide::deferred)==1,"a different index offset counted as a repeat");

    d->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    D3D11_PRIMITIVE_TOPOLOGY got_topology{};d->IAGetPrimitiveTopology(&got_topology);
    wide->observe_ia_primitive_topology(d,got_topology,0);
    wide->observe_ia_primitive_topology(d,got_topology,0);
    require(wide->repeats(Wide::ia_primitive_topology,Wide::deferred)==1,"an identical topology was not a repeat");

    const FLOAT factor[4]{0.25f,0.5f,0.75f,1.f},other_factor[4]{0.5f,0.5f,0.75f,1.f};
    d->OMSetBlendState(scene.blend_state.Get(),factor,0x0f0f0f0fu);
    ID3D11BlendState* got_blend{};FLOAT got_factor[4]{};UINT got_mask{};
    d->OMGetBlendState(&got_blend,got_factor,&got_mask);
    wide->observe_om_blend_state(d,got_blend,got_factor,got_mask,0);
    wide->observe_om_blend_state(d,got_blend,got_factor,got_mask,0);
    if(got_blend) got_blend->Release();
    wide->observe_om_blend_state(d,scene.blend_state.Get(),other_factor,got_mask,0);
    require(wide->repeats(Wide::om_blend_state,Wide::deferred)==1,"a different blend factor counted as a repeat");

    d->OMSetDepthStencilState(scene.depth_state.Get(),7);
    ID3D11DepthStencilState* got_depth{};UINT got_ref{};
    d->OMGetDepthStencilState(&got_depth,&got_ref);
    wide->observe_om_depth_stencil_state(d,got_depth,got_ref,0);
    wide->observe_om_depth_stencil_state(d,got_depth,got_ref,0);
    if(got_depth) got_depth->Release();
    wide->observe_om_depth_stencil_state(d,scene.depth_state.Get(),got_ref+1,0);
    require(wide->repeats(Wide::om_depth_stencil_state,Wide::deferred)==1,"a different stencil reference counted as a repeat");

    d->RSSetState(scene.rasterizer_state.Get());
    ID3D11RasterizerState* got_raster{};d->RSGetState(&got_raster);
    wide->observe_rs_state(d,got_raster,0);
    wide->observe_rs_state(d,got_raster,0);
    if(got_raster) got_raster->Release();
    const D3D11_RECT rects_a[2]{{0,0,4,4},{1,2,3,4}},rects_b[2]{{0,0,8,8},{1,2,3,4}};
    d->RSSetScissorRects(2,rects_a);
    UINT got_count{2};D3D11_RECT got_rects[2]{};
    d->RSGetScissorRects(&got_count,got_rects);
    wide->observe_rs_scissor_rects(d,got_count,got_rects,0);
    wide->observe_rs_scissor_rects(d,got_count,got_rects,0);
    wide->observe_rs_scissor_rects(d,2,rects_b,0);
    require(wide->repeats(Wide::rs_scissor_rects,Wide::deferred)==1,"a different scissor rect counted as a repeat");

    // A pass barrier invalidates the whole shadow: the next identical topology is not a repeat.
    wide->barrier(d);
    wide->observe_ia_primitive_topology(d,got_topology,0);
    require(wide->repeats(Wide::ia_primitive_topology,Wide::deferred)==1,"a bind after a pass barrier counted as a repeat");
    return true;
}
// v998 WARP pixel A/B for the WIDE filter. A deferred list records redundant topology, input layout, VS/Pixel shader, VS SRV,
// VB, IB, blend, depth, rasterizer, scissor and VS constant-buffer sets, then draws the full-screen triangle that samples the
// red texture. Wide elision ON and OFF must render the same pixel, and ON must have skipped the redundant binds.
std::uint32_t run_wide_pixel_sequence(Scene& scene,bool elision,unsigned& skips) {
    wide_ready[Wide::immediate]=wide_ready[Wide::deferred]=true;
    ComPtr<ID3D11DeviceContext> deferred;
    require(SUCCEEDED(scene.device->CreateDeferredContext(0,&deferred)),"cannot create a deferred context for the wide pixel lab");
    WideLab lab(elision);lab.immediate_=scene.immediate.Get();
    auto* d=deferred.Get();
    ID3D11RenderTargetView* const target[1]{scene.output_rtv.Get()};
    ID3D11ShaderResourceView* const red[1]{scene.red.Get()};
    ID3D11SamplerState* const sampler[1]{scene.sampler.Get()};
    ID3D11Buffer* const vb[1]{scene.vertex_buffer.Get()};
    const UINT stride[1]{12},offset[1]{0};
    const FLOAT factor[4]{1.f,1.f,1.f,1.f};
    const D3D11_RECT rect{0,0,1,1};
    const D3D11_VIEWPORT viewport{0,0,1,1,0,1};
    auto expect_repeat=[&](bool forwarded,const char* what) {
        require(elision?!forwarded:forwarded,what);
    };
    d->OMSetRenderTargets(1,target,nullptr);
    lab.barrier(d); // the registry's OM barrier
    d->RSSetViewports(1,&viewport);
    ID3D11SamplerState* const samplers[1]{sampler[0]};
    d->PSSetSamplers(0,1,samplers);
    // First bind of every method is forwarded, the identical second is the test.
    require(lab.topology(d,D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST),"the first topology bind was elided");
    expect_repeat(lab.topology(d,D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST),"redundant topology elision state wrong");
    require(lab.layout(d,scene.input_layout.Get()),"the first input-layout bind was elided");
    expect_repeat(lab.layout(d,scene.input_layout.Get()),"redundant input-layout elision state wrong");
    require(lab.vs_srv(d,0,1,red),"the first VS SRV bind was elided");
    expect_repeat(lab.vs_srv(d,0,1,red),"redundant VS SRV elision state wrong");
    require(lab.vb(d,0,1,vb,stride,offset),"the first VB bind was elided");
    expect_repeat(lab.vb(d,0,1,vb,stride,offset),"redundant VB elision state wrong");
    require(lab.ib(d,scene.index_buffer.Get(),DXGI_FORMAT_R16_UINT,0),"the first IB bind was elided");
    expect_repeat(lab.ib(d,scene.index_buffer.Get(),DXGI_FORMAT_R16_UINT,0),"redundant IB elision state wrong");
    require(lab.vs_shader(d,scene.vs.Get()),"the first VS shader bind was elided");
    expect_repeat(lab.vs_shader(d,scene.vs.Get()),"redundant VS shader elision state wrong");
    require(lab.ps_shader(d,scene.ps.Get()),"the first PS shader bind was elided");
    expect_repeat(lab.ps_shader(d,scene.ps.Get()),"redundant PS shader elision state wrong");
    require(lab.blend(d,scene.blend_state.Get(),factor,0x0f0f0f0fu),"the first blend binder was elided");
    expect_repeat(lab.blend(d,scene.blend_state.Get(),factor,0x0f0f0f0fu),"redundant blend elision state wrong");
    require(lab.depth(d,scene.depth_state.Get(),7),"the first depth bind was elided");
    expect_repeat(lab.depth(d,scene.depth_state.Get(),7),"redundant depth elision state wrong");
    require(lab.raster(d,scene.rasterizer_state.Get()),"the first rasterizer bind was elided");
    expect_repeat(lab.raster(d,scene.rasterizer_state.Get()),"redundant rasterizer elision state wrong");
    require(lab.scissor(d,1,&rect),"the first scissor bind was elided");
    expect_repeat(lab.scissor(d,1,&rect),"redundant scissor elision state wrong");
    require(lab.bookkeeping>=2,"ObjectMotion's bookkeeping did not see every shader call");
    d->PSSetShaderResources(0,1,red); // the PS SRV array is the PS census's, not the wide one
    d->Draw(3,0);
    ComPtr<ID3D11CommandList> list;
    require(SUCCEEDED(deferred->FinishCommandList(FALSE,&list)),"FinishCommandList failed in the wide pixel lab");
    lab.barrier(d); // the FinishCommandList barrier the TAA owner emits
    auto* immediate=scene.immediate.Get();
    lab.barrier(immediate);
    immediate->ExecuteCommandList(list.Get(),FALSE);
    lab.barrier(immediate);
    skips=lab.skipped;
    return read_output(scene);
}

// v998: the same pixel A/B after every hazard type: FinishCommandList, immediate ExecuteCommandList, ClearState, OM targets,
// OM targets+UAV (33/34), CS UAV, SO targets, a VS CB1 window, and ObjectMotion's direct pool substitution. Each hazard must
// re-arm the census (the next identical bind is forwarded); the final draw must remain pixel-identical with wide elision off.
std::uint32_t run_wide_hazard_sequence(Scene& scene,bool elision,unsigned& skips) {
    wide_ready[Wide::immediate]=wide_ready[Wide::deferred]=true;
    ComPtr<ID3D11DeviceContext> deferred;
    require(SUCCEEDED(scene.device->CreateDeferredContext(0,&deferred)),"cannot create a deferred context for the wide hazard lab");
    ComPtr<ID3D11DeviceContext1> deferred1;
    require(SUCCEEDED(deferred.As(&deferred1)),"the deferred context does not expose ID3D11DeviceContext1");
    WideLab lab(elision);lab.immediate_=scene.immediate.Get();
    auto* d=deferred.Get();
    auto* immediate=scene.immediate.Get();
    ID3D11RenderTargetView* const target[1]{scene.output_rtv.Get()};
    ID3D11RenderTargetView* const red_rtv[1]{scene.red_rtv.Get()};
    ID3D11ShaderResourceView* const red[1]{scene.red.Get()};
    ID3D11ShaderResourceView* const blue[1]{scene.blue.Get()};
    ID3D11SamplerState* const sampler[1]{scene.sampler.Get()};
    ID3D11UnorderedAccessView* const blue_uav[1]{scene.blue_uav.Get()};
    ID3D11UnorderedAccessView* const no_uav[1]{nullptr};
    ID3D11Buffer* const vb[1]{scene.vertex_buffer.Get()};
    ID3D11Buffer* const cb[1]{scene.constants.Get()};
    ID3D11Buffer* const so[1]{scene.stream_output_buffer.Get()};
    const UINT stride[1]{12},offset[1]{0},so_offset[1]{0};
    const UINT first16[1]{16},num16[1]{16};
    const FLOAT factor[4]{1.f,1.f,1.f,1.f};
    const D3D11_RECT rect{0,0,1,1};
    const D3D11_VIEWPORT viewport{0,0,1,1,0,1};
    auto expect_forward=[&](bool forwarded,const char* what) {require(forwarded,what);};
    auto expect_repeat=[&](bool forwarded,const char* what) {require(elision?!forwarded:forwarded,what);};
    auto start=[&] {
        d->OMSetRenderTargets(1,target,nullptr);
        lab.barrier(d);
        d->RSSetViewports(1,&viewport);
        d->RSSetScissorRects(1,&rect);
        d->PSSetSamplers(0,1,sampler);
    };
    start();
    // (1) FinishCommandList ends the recording stream: the next identical bind must be forwarded.
    require(lab.topology(d,D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST),"hazard lab: first topology was elided");
    expect_repeat(lab.topology(d,D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST),"hazard lab: redundant topology elision state wrong");
    {
        ComPtr<ID3D11CommandList> list;
        require(SUCCEEDED(deferred->FinishCommandList(FALSE,&list)),"hazard lab: FinishCommandList 1 failed");
        lab.barrier(d);
        expect_forward(lab.topology(d,D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST),"hazard lab: bind after FinishCommandList was elided");
    }
    // (2) ExecuteCommandList on the immediate context: the same bind after it must be forwarded.
    require(lab.topology(immediate,D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST),"hazard lab: immediate first topology was elided");
    expect_repeat(lab.topology(immediate,D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST),"hazard lab: immediate redundant topology elision state wrong");
    {
        ComPtr<ID3D11CommandList> list;
        require(SUCCEEDED(deferred->FinishCommandList(FALSE,&list)),"hazard lab: FinishCommandList 2 failed");
        lab.barrier(d);
        lab.barrier(immediate);
        immediate->ExecuteCommandList(list.Get(),FALSE);
        lab.barrier(immediate);
        expect_forward(lab.topology(immediate,D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST),"hazard lab: bind after ExecuteCommandList was elided");
    }
    // (3) ClearState.
    start();
    d->ClearState();
    lab.barrier(d);
    expect_forward(lab.topology(d,D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST),"hazard lab: bind after ClearState was elided");
    // (4) OM target bind unbinds an overlapping SRV.
    start();
    require(lab.vs_srv(d,0,1,red),"hazard lab: first red VS SRV was elided");
    expect_repeat(lab.vs_srv(d,0,1,red),"hazard lab: redundant red VS SRV elision state wrong");
    d->OMSetRenderTargets(1,red_rtv,nullptr);
    lab.barrier(d);
    expect_forward(lab.vs_srv(d,0,1,red),"hazard lab: VS SRV after an OM target bind was elided");
    // (5) OMSetRenderTargetsAndUnorderedAccessViews (slot 34).
    start();
    require(lab.vs_srv(d,0,1,blue),"hazard lab: first blue VS SRV was elided");
    d->OMSetRenderTargetsAndUnorderedAccessViews(0,nullptr,nullptr,0,1,blue_uav,nullptr);
    lab.barrier(d);
    expect_forward(lab.vs_srv(d,0,1,blue),"hazard lab: VS SRV after the OM UAV form was elided");
    // (6) CSSetUnorderedAccessViews (slot 68).
    d->CSSetUnorderedAccessViews(0,1,blue_uav,nullptr);
    lab.barrier(d);
    expect_forward(lab.vs_srv(d,0,1,blue),"hazard lab: VS SRV after a CS UAV bind was elided");
    d->CSSetUnorderedAccessViews(0,1,no_uav,nullptr);
    lab.barrier(d);
    // (7) SOSetTargets (slot 37): a full barrier for VB/IB (and the rest of the wide shadow).
    start();
    require(lab.vb(d,0,1,vb,stride,offset),"hazard lab: first VB was elided");
    d->SOSetTargets(1,so,so_offset);
    lab.barrier(d);
    expect_forward(lab.vb(d,0,1,vb,stride,offset),"hazard lab: VB after SOSetTargets was elided");
    // (8) VSSetConstantBuffers1 is never elided and invalidates the covered base slot.
    start();
    require(lab.vs_cb(d,0,1,cb),"hazard lab: first base VS CB was elided");
    expect_repeat(lab.vs_cb(d,0,1,cb),"hazard lab: redundant base VS CB elision state wrong");
    lab.vs_cb1(deferred1.Get(),0,1,cb,first16,num16);
    lab.vs_cb1(deferred1.Get(),0,1,cb,first16,num16); // identical CB1: still forwarded
    expect_forward(lab.vs_cb(d,0,1,cb),"hazard lab: base VS CB after a CB1 window was elided");
    // (9) ObjectMotion's direct pool substitution, then the game's buffer again: a change, never elided.
    ID3D11Buffer* const pool[1]{scene.vertex_buffer.Get()};
    lab.wide.note_direct_vs_constant_buffers(d,2,1,pool);
    expect_forward(lab.vs_cb(d,2,1,cb),"hazard lab: game VS CB after an ObjectMotion substitution was elided");
    d->SOSetTargets(0,nullptr,nullptr);
    lab.barrier(d);
    // Final drawn list after all the hazards.
    start();
    require(lab.topology(d,D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST),"hazard lab: final topology was elided");
    require(lab.layout(d,scene.input_layout.Get()),"hazard lab: final input layout was elided");
    require(lab.vs_srv(d,0,1,red),"hazard lab: final VS SRV was elided");
    require(lab.vb(d,0,1,vb,stride,offset),"hazard lab: final VB was elided");
    require(lab.ib(d,scene.index_buffer.Get(),DXGI_FORMAT_R16_UINT,0),"hazard lab: final IB was elided");
    require(lab.vs_shader(d,scene.vs.Get()),"hazard lab: final VS shader was elided");
    require(lab.ps_shader(d,scene.ps.Get()),"hazard lab: final PS shader was elided");
    require(lab.blend(d,scene.blend_state.Get(),factor,0x0f0f0f0fu),"hazard lab: final blend was elided");
    require(lab.depth(d,scene.depth_state.Get(),7),"hazard lab: final depth bind was elided");
    require(lab.raster(d,scene.rasterizer_state.Get()),"hazard lab: final rasterizer was elided");
    require(lab.scissor(d,1,&rect),"hazard lab: final scissor was elided");
    d->PSSetShaderResources(0,1,red); // the PS SRV array is the PS census's, not the wide one
    d->Draw(3,0);
    ComPtr<ID3D11CommandList> list;
    require(SUCCEEDED(deferred->FinishCommandList(FALSE,&list)),"hazard lab: final FinishCommandList failed");
    lab.barrier(d);
    lab.barrier(immediate);
    immediate->ExecuteCommandList(list.Get(),FALSE);
    lab.barrier(immediate);
    skips=lab.skipped;
    return read_output(scene);
}
} // namespace

int main() {
    try {
        Wide::barrier_ready_source.store(&wide_ready_source);
        Wide::barrier_reason_source.store(&wide_reason_source);
        Scene scene;
        require(make_scene(scene),"cannot create the WARP device, textures or shaders");
        unsigned skips_on{},skips_off{},immediate_on{},immediate_off{};
        const auto pixel_on=run_sequence(scene,true,skips_on,immediate_on);
        const auto pixel_off=run_sequence(scene,false,skips_off,immediate_off);
        std::printf("elision on : pixel=0x%08X skips=%u immediate_skips=%u\n",pixel_on,skips_on,immediate_on);
        std::printf("elision off: pixel=0x%08X skips=%u immediate_skips=%u\n",pixel_off,skips_off,immediate_off);
        require(skips_on>0,"elision on skipped nothing");
        require(skips_off==0,"elision off skipped something");
        require(immediate_on==1,"the immediate ExecuteCommandList case did not skip exactly the one redundant bind");
        require(immediate_off==0,"elision off skipped an immediate bind");
        require(pixel_on==pixel_off,"the elided sequence rendered a different pixel");
        require(pixel_on==0xFF0000FFu,"the sampled texture is not the red one the sequence bound");
        // v995: base B -> CB1 B at first=16 -> base B again. The last base bind must not be skipped, and the pixels must match.
        const auto cb_on=run_cb1_sequence(scene,true);
        const auto cb_off=run_cb1_sequence(scene,false);
        std::printf("CB1 elision on : base=0x%08X window=0x%08X after=0x%08X skips=%u cb1=%llu\n",
            cb_on.base,cb_on.window,cb_on.after,cb_on.skips,static_cast<unsigned long long>(cb_on.cb1_calls));
        std::printf("CB1 elision off: base=0x%08X window=0x%08X after=0x%08X skips=%u cb1=%llu\n",
            cb_off.base,cb_off.window,cb_off.after,cb_off.skips,static_cast<unsigned long long>(cb_off.cb1_calls));
        require(cb_on.base==0xFF0000FFu && cb_off.base==0xFF0000FFu,"the base CB bind did not read buffer constant 0 (red)");
        require(cb_on.window==0xFFFF0000u && cb_off.window==0xFFFF0000u,"the CB1 first=16 bind did not read buffer constant 16 (blue)");
        require(cb_on.after==0xFF0000FFu && cb_off.after==0xFF0000FFu,"the base bind after PSSetConstantBuffers1 was skipped or read the window (blue)");
        require(cb_on.after==cb_off.after,"the CB1 sequence rendered a different pixel with elision on");
        require(cb_on.skips==1 && cb_off.skips==0,"the CB1 sequence did not skip exactly the redundant base bind with elision on");
        require(cb_on.cb1_calls==3 && cb_off.cb1_calls==3,"PSSetConstantBuffers1 calls were not all counted/forwarded");
        require(run_swap_sequence(scene),"the immediate SwapDeviceContextState sequence failed");
        require(run_toggle_sequence(scene),"the live elision toggle sequence failed");
        require(run_ab_window_lab(),"the elision A/B window lab failed");
        require(run_wide_census_lab(scene),"the WARP wide-census repeat lab failed");
        // v998: the wide filter's own pixel A/B, then the same with every hazard type in the sequence.
        unsigned wide_skips_on{},wide_skips_off{},wide_hazard_on{},wide_hazard_off{};
        const auto wide_pixel_on=run_wide_pixel_sequence(scene,true,wide_skips_on);
        const auto wide_pixel_off=run_wide_pixel_sequence(scene,false,wide_skips_off);
        std::printf("wide elision on : pixel=0x%08X skips=%u\n",wide_pixel_on,wide_skips_on);
        std::printf("wide elision off: pixel=0x%08X skips=%u\n",wide_pixel_off,wide_skips_off);
        require(wide_skips_on>0,"wide elision on skipped nothing");
        require(wide_skips_off==0,"wide elision off skipped something");
        require(wide_pixel_on==wide_pixel_off,"the wide elision sequence rendered a different pixel");
        require(wide_pixel_on==0xFF0000FFu,"the wide sequence did not sample the red texture");
        const auto wide_hazard_pixel_on=run_wide_hazard_sequence(scene,true,wide_hazard_on);
        const auto wide_hazard_pixel_off=run_wide_hazard_sequence(scene,false,wide_hazard_off);
        std::printf("wide hazards on : pixel=0x%08X skips=%u\n",wide_hazard_pixel_on,wide_hazard_on);
        std::printf("wide hazards off: pixel=0x%08X skips=%u\n",wide_hazard_pixel_off,wide_hazard_off);
        require(wide_hazard_on>0,"the wide hazard sequence skipped nothing with elision on");
        require(wide_hazard_off==0,"the wide hazard sequence skipped something with elision off");
        require(wide_hazard_pixel_on==wide_hazard_pixel_off,"the wide hazard sequence rendered a different pixel");
        require(wide_hazard_pixel_on==0xFF0000FFu,"the wide hazard sequence did not sample the red texture");
        std::printf("PASS: WARP deferred PS census - skips leave identical state, every pass barrier re-arms, CB1 window A/B equal, immediate swap keeps deferred elision, live toggle, A/B frame histograms, wide-census parameter comparisons, wide pixel A/B and every wide hazard type pixel-identical\n");
        return 0;
    } catch(const std::exception& error) {
        std::printf("FAIL: %s\n",error.what());
        return 1;
    }
}
