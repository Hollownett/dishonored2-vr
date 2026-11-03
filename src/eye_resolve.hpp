#pragma once
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <stdexcept>
#include <string>
#include "context_state.hpp"

namespace d2vr {
// v976: resolve a game eye rendered above the XR eye size (internal supersampling) down to the XR image. The runtime's
// maxImageRect clamps the XR swapchain (Virtual Desktop High: 1.25x), so 150% SSAA renders 1.5x and is box-filtered
// here. Four bilinear taps approximating a 2x2 box; no history, no jitter, both eyes sequential.
class EyeResolve {
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
    Ptr<ID3D11Texture2D> output_;
    Ptr<ID3D11ShaderResourceView> output_srv_;
    Ptr<ID3D11RenderTargetView> output_rtv_;
    UINT width_{},height_{};
    static void checked(HRESULT result,const char* label) {
        if(FAILED(result)) throw std::runtime_error(std::string("SSAA resolve: ")+label+" ("+std::to_string(result)+")");
    }
    static Ptr<ID3DBlob> compile(const std::string& source,const char* entry,const char* profile) {
        Ptr<ID3DBlob> binary,errors;
        const auto result=D3DCompile(source.data(),source.size(),"D2VR SSAA resolve",nullptr,nullptr,entry,profile,
            D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&binary,&errors);
        if(FAILED(result)) throw std::runtime_error(std::string("SSAA resolve shader ")+entry+": "+
            (errors?std::string(static_cast<const char*>(errors->GetBufferPointer()),errors->GetBufferSize()):"compile failed"));
        return binary;
    }
    Ptr<ID3D11Texture2D> texture(UINT width,UINT height) {
        D3D11_TEXTURE2D_DESC desc{};desc.Width=width;desc.Height=height;desc.MipLevels=1;
        desc.ArraySize=1;desc.Format=DXGI_FORMAT_R8G8B8A8_TYPELESS;desc.SampleDesc.Count=1;
        desc.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;desc.Usage=D3D11_USAGE_DEFAULT;
        Ptr<ID3D11Texture2D> result;checked(device_->CreateTexture2D(&desc,nullptr,&result),"texture allocation");return result;
    }
    Ptr<ID3D11ShaderResourceView> srv(ID3D11Texture2D* texture,const D3D11_TEXTURE2D_DESC& desc) {
        D3D11_SHADER_RESOURCE_VIEW_DESC view{};view.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;
        view.Format=desc.Format==DXGI_FORMAT_R8G8B8A8_TYPELESS?DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
            desc.Format==DXGI_FORMAT_R8G8B8A8_UNORM?DXGI_FORMAT_R8G8B8A8_UNORM:DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        view.Texture2D.MipLevels=1;Ptr<ID3D11ShaderResourceView> result;
        checked(device_->CreateShaderResourceView(texture,&view,&result),"texture read view");return result;
    }
    Ptr<ID3D11RenderTargetView> rtv(ID3D11Texture2D* texture) {
        D3D11_RENDER_TARGET_VIEW_DESC view{};view.Format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        view.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;Ptr<ID3D11RenderTargetView> result;
        checked(device_->CreateRenderTargetView(texture,&view,&result),"texture write view");return result;
    }
    void resize(UINT width,UINT height) {
        if(width==width_ && height==height_) return;
        if(!width || !height || width>D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION || height>D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION)
            throw std::runtime_error("SSAA resolve size exceeds D3D11 texture limits");
        auto output=texture(width,height);D3D11_TEXTURE2D_DESC output_desc{};output_desc.Format=DXGI_FORMAT_R8G8B8A8_TYPELESS;
        auto read=srv(output.Get(),output_desc);
        auto write=rtv(output.Get());
        output_=output;output_srv_=read;output_rtv_=write;width_=width;height_=height;
    }
public:
    EyeResolve(ID3D11Device* device,UINT width,UINT height):device_(device),state_(device) {
        device_->GetImmediateContext(&context_);
        const std::string source=R"HLSL(
cbuffer Params : register(b0) { float2 sourceSize; float2 targetSize; }
Texture2D sourceTex : register(t0);
SamplerState linearSampler : register(s0);
struct V { float4 p:SV_POSITION;float2 uv:TEXCOORD0; };
V VS(uint id:SV_VertexID) { V v;v.uv=float2((id<<1)&2,id&2);v.p=float4(v.uv*float2(2,-2)+float2(-1,1),0,1);return v; }
float4 PS(V v):SV_TARGET {
    float2 texel=1.0/sourceSize;
    // Four bilinear taps at the quarter points of one source texel: a compact 2x2 box for a 1.25-1.5x downscale.
    float4 c=sourceTex.Sample(linearSampler,v.uv+float2(-.375,-.375)*texel)
            +sourceTex.Sample(linearSampler,v.uv+float2( .375,-.375)*texel)
            +sourceTex.Sample(linearSampler,v.uv+float2(-.375, .375)*texel)
            +sourceTex.Sample(linearSampler,v.uv+float2( .375, .375)*texel);
    return c*.25;
}
)HLSL";
        auto vs=compile(source,"VS","vs_5_0"),ps=compile(source,"PS","ps_5_0");
        checked(device_->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&vertex_),"vertex shader");
        checked(device_->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&pixel_),"pixel shader");
        D3D11_SAMPLER_DESC sampler{};sampler.Filter=D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sampler.MaxLOD=D3D11_FLOAT32_MAX;
        checked(device_->CreateSamplerState(&sampler,&sampler_),"sampler");
        D3D11_BUFFER_DESC cb{};cb.ByteWidth=16;cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;cb.Usage=D3D11_USAGE_DEFAULT;
        checked(device_->CreateBuffer(&cb,nullptr,&parameters_),"parameters");
        D3D11_RASTERIZER_DESC raster{};raster.FillMode=D3D11_FILL_SOLID;raster.CullMode=D3D11_CULL_NONE;raster.DepthClipEnable=TRUE;
        checked(device_->CreateRasterizerState(&raster,&raster_),"rasterizer");
        D3D11_DEPTH_STENCIL_DESC depth{};depth.DepthFunc=D3D11_COMPARISON_ALWAYS;
        checked(device_->CreateDepthStencilState(&depth,&depth_),"depth state");
        resize(width,height);
    }
    ID3D11Texture2D* apply(ID3D11Texture2D* source) {
        if(!source) throw std::runtime_error("SSAA resolve input is missing");
        D3D11_TEXTURE2D_DESC desc{};source->GetDesc(&desc);
        if(desc.Width<width_ || desc.Height<height_ || desc.SampleDesc.Count!=1 || desc.ArraySize!=1 || desc.MipLevels!=1 ||
            !(desc.Format==DXGI_FORMAT_R8G8B8A8_UNORM || desc.Format==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || desc.Format==DXGI_FORMAT_R8G8B8A8_TYPELESS))
            throw std::runtime_error("SSAA resolve source format or size differs");
        ContextState::Scope scope(state_);
        auto source_srv=srv(source,desc);
        const std::array<float,4> params{static_cast<float>(desc.Width),static_cast<float>(desc.Height),
            static_cast<float>(width_),static_cast<float>(height_)};
        context_->UpdateSubresource(parameters_.Get(),0,nullptr,params.data(),0,0);
        const D3D11_VIEWPORT viewport{0,0,static_cast<float>(width_),static_cast<float>(height_),0,1};
        context_->RSSetViewports(1,&viewport);context_->RSSetState(raster_.Get());
        context_->IASetInputLayout(nullptr);context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context_->GSSetShader(nullptr,nullptr,0);context_->HSSetShader(nullptr,nullptr,0);context_->DSSetShader(nullptr,nullptr,0);
        context_->OMSetDepthStencilState(depth_.Get(),0);context_->OMSetBlendState(nullptr,nullptr,0xffffffff);
        auto cb=parameters_.Get();context_->VSSetConstantBuffers(0,1,&cb);context_->PSSetConstantBuffers(0,1,&cb);
        auto sampler=sampler_.Get();context_->PSSetSamplers(0,1,&sampler);
        ID3D11ShaderResourceView* views[]{source_srv.Get()};context_->PSSetShaderResources(0,1,views);
        auto target=output_rtv_.Get();context_->OMSetRenderTargets(1,&target,nullptr);
        context_->VSSetShader(vertex_.Get(),nullptr,0);context_->PSSetShader(pixel_.Get(),nullptr,0);
        context_->Draw(3,0);
        ID3D11ShaderResourceView* empty{};context_->PSSetShaderResources(0,1,&empty);context_->OMSetRenderTargets(0,nullptr,nullptr);
        return output_.Get();
    }
    UINT width() const noexcept {return width_;}
    UINT height() const noexcept {return height_;}
};
} // namespace d2vr
