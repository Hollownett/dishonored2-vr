#pragma once
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <string>
#include <stdexcept>
#include "context_state.hpp"

namespace d2vr {
// Contrast adaptive sharpening in the style of AMD FidelityFX CAS. The 3x3
// cross neighbourhood is read through an _SRGB view and written through an
// _SRGB target so the adaptive weight works in linear light while the source
// and output bytes stay in the 0..1 range. One instance serves both eyes
// sequentially; the caller copies the result out at once, so a single output
// texture is enough. No frame history, jitter or cross-eye state.
class EyeSharpen {
    template<class T> using Ptr=Microsoft::WRL::ComPtr<T>;
    Ptr<ID3D11Device> device_;
    Ptr<ID3D11DeviceContext> context_;
    ContextState state_;
    Ptr<ID3D11VertexShader> vertex_;
    Ptr<ID3D11PixelShader> pixel_;
    Ptr<ID3D11SamplerState> sampler_;
    Ptr<ID3D11Buffer> parameters_;
    Ptr<ID3D11RasterizerState> raster_;
    Ptr<ID3D11DepthStencilState> depth_;
    Ptr<ID3D11Texture2D> copy_,output_;
    Ptr<ID3D11ShaderResourceView> copy_srv_,output_srv_;
    Ptr<ID3D11RenderTargetView> output_rtv_;
    UINT width_{},height_{},copy_width_{},copy_height_{};
    static void checked(HRESULT result,const char* label) {
        if(FAILED(result)) throw std::runtime_error(std::string("CAS: ")+label+" ("+std::to_string(result)+")");
    }
    static Ptr<ID3DBlob> compile(const std::string& source,const char* entry,const char* profile) {
        Ptr<ID3DBlob> binary,errors;
        const auto result=D3DCompile(source.data(),source.size(),"D2VR CAS",nullptr,nullptr,entry,profile,
            D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&binary,&errors);
        if(FAILED(result)) throw std::runtime_error(std::string("CAS shader ")+entry+": "+
            (errors?std::string(static_cast<const char*>(errors->GetBufferPointer()),errors->GetBufferSize()):"compile failed"));
        return binary;
    }
    Ptr<ID3D11Texture2D> texture(UINT width,UINT height,DXGI_FORMAT format,UINT bindings) {
        D3D11_TEXTURE2D_DESC desc{};desc.Width=width;desc.Height=height;desc.MipLevels=1;
        desc.ArraySize=1;desc.Format=format;desc.SampleDesc.Count=1;desc.BindFlags=bindings;
        desc.Usage=D3D11_USAGE_DEFAULT;
        Ptr<ID3D11Texture2D> result;
        checked(device_->CreateTexture2D(&desc,nullptr,&result),"texture allocation");return result;
    }
    Ptr<ID3D11ShaderResourceView> srv(ID3D11Texture2D* texture,DXGI_FORMAT format) {
        D3D11_SHADER_RESOURCE_VIEW_DESC desc{};desc.Format=format;desc.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;
        desc.Texture2D.MipLevels=1;Ptr<ID3D11ShaderResourceView> result;
        checked(device_->CreateShaderResourceView(texture,&desc,&result),"texture read view");return result;
    }
    // A typed resource only accepts a view in its own format, so a linear-light
    // read of a game or SMAA texture may not be available; the caller falls
    // back to a typeless copy in that case.
    Ptr<ID3D11ShaderResourceView> try_srv(ID3D11Texture2D* texture,DXGI_FORMAT format) {
        D3D11_SHADER_RESOURCE_VIEW_DESC desc{};desc.Format=format;desc.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;
        desc.Texture2D.MipLevels=1;Ptr<ID3D11ShaderResourceView> result;
        device_->CreateShaderResourceView(texture,&desc,&result);return result;
    }
    Ptr<ID3D11RenderTargetView> rtv(ID3D11Texture2D* texture,DXGI_FORMAT format) {
        D3D11_RENDER_TARGET_VIEW_DESC desc{};desc.Format=format;desc.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
        Ptr<ID3D11RenderTargetView> result;
        checked(device_->CreateRenderTargetView(texture,&desc,&result),"texture write view");return result;
    }
    void resize(UINT width,UINT height) {
        if(width==width_ && height==height_) return;
        if(!width || !height || width>D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION || height>D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION) throw std::runtime_error("CAS size exceeds D3D11 texture limits");
        // Allocate the complete replacement before relinquishing the old output.
        auto output=texture(width,height,DXGI_FORMAT_R8G8B8A8_TYPELESS,D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET);
        auto read=srv(output.Get(),DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
        auto write=rtv(output.Get(),DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
        output_=output;output_srv_=read;output_rtv_=write;width_=width;height_=height;
    }
    void ensure_copy(UINT width,UINT height) {
        if(copy_ && copy_width_==width && copy_height_==height) return;
        auto copy=texture(width,height,DXGI_FORMAT_R8G8B8A8_TYPELESS,D3D11_BIND_SHADER_RESOURCE);
        copy_srv_=srv(copy.Get(),DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
        copy_=copy;copy_width_=width;copy_height_=height;
    }
public:
    explicit EyeSharpen(ID3D11Device* device):device_(device),state_(device) {
        device_->GetImmediateContext(&context_);
        const std::string source=R"HLSL(
Texture2D sourceTex : register(t0);
SamplerState sourceSampler : register(s0);
cbuffer Parameters : register(b0) { float4 params; }
struct Vertex { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
Vertex SharpenVS(uint id : SV_VertexID) {
    const float2 uv=float2((id<<1)&2,id&2);
    Vertex v;v.uv=uv;v.position=float4(uv*float2(2,-2)+float2(-1,1),0,1);return v;
}
float4 SharpenPS(Vertex v) : SV_TARGET {
    const float2 texel=params.yz;
    const float4 centre=sourceTex.SampleLevel(sourceSampler,v.uv,0);
    const float3 middle=centre.rgb;
    const float3 up=sourceTex.SampleLevel(sourceSampler,v.uv-float2(0,texel.y),0).rgb;
    const float3 down=sourceTex.SampleLevel(sourceSampler,v.uv+float2(0,texel.y),0).rgb;
    const float3 left=sourceTex.SampleLevel(sourceSampler,v.uv-float2(texel.x,0),0).rgb;
    const float3 right=sourceTex.SampleLevel(sourceSampler,v.uv+float2(texel.x,0),0).rgb;
    const float3 lowest=min(min(up,down),min(min(left,right),middle));
    const float3 highest=max(max(up,down),max(max(left,right),middle));
    // CAS adaptive amplitude for a 0..1 signal; an all-black neighbourhood has
    // no local structure to sharpen.
    const float3 amplitude=sqrt(saturate(min(lowest,1-highest)/max(highest,1e-5)));
    const float3 weight=amplitude*lerp(-0.125,-0.2,params.x);
    const float3 neighbours=up+down+left+right;
    return float4(saturate((neighbours*weight+middle)/(1+4*weight)),centre.a);
}
)HLSL";
        auto v=compile(source,"SharpenVS","vs_5_0"),p=compile(source,"SharpenPS","ps_5_0");
        checked(device_->CreateVertexShader(v->GetBufferPointer(),v->GetBufferSize(),nullptr,&vertex_),"vertex shader");
        checked(device_->CreatePixelShader(p->GetBufferPointer(),p->GetBufferSize(),nullptr,&pixel_),"pixel shader");
        D3D11_SAMPLER_DESC sampler{};sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sampler.MaxLOD=D3D11_FLOAT32_MAX;
        checked(device_->CreateSamplerState(&sampler,&sampler_),"sampler");
        D3D11_BUFFER_DESC constants{};constants.ByteWidth=16;constants.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        constants.Usage=D3D11_USAGE_DEFAULT;
        checked(device_->CreateBuffer(&constants,nullptr,&parameters_),"parameter buffer");
        D3D11_RASTERIZER_DESC rasterizer{};rasterizer.FillMode=D3D11_FILL_SOLID;rasterizer.CullMode=D3D11_CULL_NONE;
        rasterizer.DepthClipEnable=TRUE;
        checked(device_->CreateRasterizerState(&rasterizer,&raster_),"rasterizer");
        D3D11_DEPTH_STENCIL_DESC depthState{};depthState.DepthFunc=D3D11_COMPARISON_ALWAYS;
        checked(device_->CreateDepthStencilState(&depthState,&depth_),"depth state");
    }
    ID3D11Texture2D* apply(ID3D11Texture2D* source,float sharpness) {
        if(!source) throw std::runtime_error("CAS input is missing");
        D3D11_TEXTURE2D_DESC desc{};source->GetDesc(&desc);
        Ptr<ID3D11Device> owner;source->GetDevice(&owner);
        if(owner.Get()!=device_.Get() || desc.SampleDesc.Count!=1 || desc.ArraySize!=1 || desc.MipLevels!=1 ||
            (desc.Format!=DXGI_FORMAT_R8G8B8A8_UNORM && desc.Format!=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB &&
             desc.Format!=DXGI_FORMAT_R8G8B8A8_TYPELESS))
            throw std::runtime_error("CAS input format or device differs");
        if(!(sharpness>0)) return source;
        ContextState::Scope scope(state_);
        resize(desc.Width,desc.Height);
        Ptr<ID3D11ShaderResourceView> read;
        if(desc.BindFlags&D3D11_BIND_SHADER_RESOURCE) read=try_srv(source,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
        if(!read) {
            // The SMAA output and an eye-sized swapchain copy can both lack a
            // readable view; CopyResource still accepts them as a source.
            ensure_copy(desc.Width,desc.Height);
            context_->CopyResource(copy_.Get(),source);
            read=copy_srv_;
        }
        const std::array<float,4> parameters{std::min(1.f,sharpness),1.f/static_cast<float>(width_),
            1.f/static_cast<float>(height_),0.f};
        context_->UpdateSubresource(parameters_.Get(),0,nullptr,parameters.data(),0,0);
        const D3D11_VIEWPORT viewport{0,0,static_cast<float>(width_),static_cast<float>(height_),0,1};
        context_->RSSetViewports(1,&viewport);context_->RSSetState(raster_.Get());
        context_->IASetInputLayout(nullptr);context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context_->GSSetShader(nullptr,nullptr,0);context_->HSSetShader(nullptr,nullptr,0);context_->DSSetShader(nullptr,nullptr,0);
        context_->OMSetDepthStencilState(depth_.Get(),0);context_->OMSetBlendState(nullptr,nullptr,0xffffffff);
        auto constants=parameters_.Get();context_->VSSetConstantBuffers(0,1,&constants);context_->PSSetConstantBuffers(0,1,&constants);
        ID3D11SamplerState* samplers[]{sampler_.Get()};context_->PSSetSamplers(0,1,samplers);
        ID3D11ShaderResourceView* inputs[]{read.Get()};context_->PSSetShaderResources(0,1,inputs);
        auto* target=output_rtv_.Get();context_->OMSetRenderTargets(1,&target,nullptr);
        context_->VSSetShader(vertex_.Get(),nullptr,0);context_->PSSetShader(pixel_.Get(),nullptr,0);
        context_->Draw(3,0);
        ID3D11ShaderResourceView* empty[1]{};context_->PSSetShaderResources(0,1,empty);
        context_->OMSetRenderTargets(0,nullptr,nullptr);
        return output_.Get();
    }
};
} // namespace d2vr
