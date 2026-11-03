#pragma once
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <iomanip>
#include <sstream>
#include <string>
#include <stdexcept>
#include "context_state.hpp"
#include "../third_party/smaa/shader_source.hpp"
#include "../third_party/smaa/Textures/AreaTex.h"
#include "../third_party/smaa/Textures/SearchTex.h"

namespace d2vr {
// SMAA 1x: all three passes finish for one eye before reusing the scratch
// targets for the other. No cross-eye/frame history, jitter or sharpening.
class EyeAntialiasing {
    template<class T> using Ptr=Microsoft::WRL::ComPtr<T>;
    Ptr<ID3D11Device> device_;
    Ptr<ID3D11DeviceContext> context_;
    ContextState state_;
    std::array<Ptr<ID3D11VertexShader>,3> vertex_;
    std::array<Ptr<ID3D11PixelShader>,3> pixel_;
    std::array<Ptr<ID3D11SamplerState>,2> samplers_;
    Ptr<ID3D11Buffer> metrics_;
    Ptr<ID3D11RasterizerState> raster_;
    Ptr<ID3D11DepthStencilState> depth_;
    Ptr<ID3D11ShaderResourceView> area_,search_,input_gamma_,input_linear_,edges_srv_,weights_srv_;
    Ptr<ID3D11Texture2D> input_,edges_,weights_,output_;
    Ptr<ID3D11RenderTargetView> edges_rtv_,weights_rtv_,output_rtv_;
    UINT width_{},height_{};
    int passes_{2};
    float threshold_{.03f};
    static void checked(HRESULT result,const char* label) {
        if(FAILED(result)) throw std::runtime_error(std::string("SMAA: ")+label+" ("+std::to_string(result)+")");
    }
    static Ptr<ID3DBlob> compile(const std::string& source,const char* entry,const char* profile) {
        Ptr<ID3DBlob> binary,errors;
        const auto result=D3DCompile(source.data(),source.size(),"D2VR SMAA",nullptr,nullptr,entry,profile,
            D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&binary,&errors);
        if(FAILED(result)) throw std::runtime_error(std::string("SMAA shader ")+entry+": "+
            (errors?std::string(static_cast<const char*>(errors->GetBufferPointer()),errors->GetBufferSize()):"compile failed"));
        return binary;
    }
    Ptr<ID3D11Texture2D> texture(UINT width,UINT height,DXGI_FORMAT format,UINT bindings,
                                 const void* bytes=nullptr,UINT pitch=0) {
        D3D11_TEXTURE2D_DESC desc{};desc.Width=width;desc.Height=height;desc.MipLevels=1;
        desc.ArraySize=1;desc.Format=format;desc.SampleDesc.Count=1;desc.BindFlags=bindings;
        desc.Usage=bytes?D3D11_USAGE_IMMUTABLE:D3D11_USAGE_DEFAULT;
        D3D11_SUBRESOURCE_DATA data{};data.pSysMem=bytes;data.SysMemPitch=pitch;
        Ptr<ID3D11Texture2D> result;
        checked(device_->CreateTexture2D(&desc,bytes?&data:nullptr,&result),"texture allocation");return result;
    }
    Ptr<ID3D11ShaderResourceView> srv(ID3D11Texture2D* texture,DXGI_FORMAT format) {
        D3D11_SHADER_RESOURCE_VIEW_DESC desc{};desc.Format=format;desc.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;
        desc.Texture2D.MipLevels=1;Ptr<ID3D11ShaderResourceView> result;
        checked(device_->CreateShaderResourceView(texture,&desc,&result),"texture read view");return result;
    }
    Ptr<ID3D11RenderTargetView> rtv(ID3D11Texture2D* texture,DXGI_FORMAT format) {
        D3D11_RENDER_TARGET_VIEW_DESC desc{};desc.Format=format;desc.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
        Ptr<ID3D11RenderTargetView> result;
        checked(device_->CreateRenderTargetView(texture,&desc,&result),"texture write view");return result;
    }
    void resize(UINT width,UINT height) {
        if(width==width_ && height==height_) return;
        if(!width || !height || width>D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION ||
           height>D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION) throw std::runtime_error("SMAA size exceeds graphics limits");
        // Allocate a complete replacement before relinquishing the old buffers.
        auto input=texture(width,height,DXGI_FORMAT_R8G8B8A8_TYPELESS,D3D11_BIND_SHADER_RESOURCE);
        auto gamma=srv(input.Get(),DXGI_FORMAT_R8G8B8A8_UNORM),linear=srv(input.Get(),DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
        auto edges=texture(width,height,DXGI_FORMAT_R8G8B8A8_UNORM,D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET);
        auto weights=texture(width,height,DXGI_FORMAT_R8G8B8A8_UNORM,D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET);
        auto output=texture(width,height,DXGI_FORMAT_R8G8B8A8_TYPELESS,D3D11_BIND_RENDER_TARGET);
        auto edges_read=srv(edges.Get(),DXGI_FORMAT_R8G8B8A8_UNORM),weights_read=srv(weights.Get(),DXGI_FORMAT_R8G8B8A8_UNORM);
        auto edges_write=rtv(edges.Get(),DXGI_FORMAT_R8G8B8A8_UNORM),weights_write=rtv(weights.Get(),DXGI_FORMAT_R8G8B8A8_UNORM);
        auto output_write=rtv(output.Get(),DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
        input_=input;input_gamma_=gamma;input_linear_=linear;edges_=edges;weights_=weights;output_=output;
        edges_srv_=edges_read;weights_srv_=weights_read;edges_rtv_=edges_write;weights_rtv_=weights_write;output_rtv_=output_write;
        width_=width;height_=height;
    }
public:
    UINT64 eye_passes{};
    explicit EyeAntialiasing(ID3D11Device* device,float threshold=.03f,int passes=2)
        :device_(device),state_(device),passes_(passes),threshold_(threshold) {
        device_->GetImmediateContext(&context_);
        const std::string prefix=R"HLSL(
cbuffer Metrics : register(b0) { float4 rtMetrics; }
#define SMAA_RT_METRICS rtMetrics
#define SMAA_HLSL_4_1 1
#define SMAA_PRESET_ULTRA 1
)HLSL";
        const std::string wrappers=R"HLSL(
Texture2D inputTex : register(t0);
Texture2D edgesTex : register(t1);
Texture2D weightsTex : register(t2);
Texture2D areaTex : register(t3);
Texture2D searchTex : register(t4);
void fullscreenCoordinates(uint id, out float4 position, out float2 uv) {
    uv=float2((id<<1)&2,id&2);position=float4(uv*float2(2,-2)+float2(-1,1),0,1);
}
struct EdgeV { float4 p:SV_POSITION;float2 uv:TEXCOORD0;float4 offsets[3]:TEXCOORD1; };
struct WeightV { float4 p:SV_POSITION;float2 uv:TEXCOORD0;float2 pixel:TEXCOORD1;float4 offsets[3]:TEXCOORD2; };
struct BlendV { float4 p:SV_POSITION;float2 uv:TEXCOORD0;float4 offsets:TEXCOORD1; };
EdgeV EdgeVS(uint id:SV_VertexID) { EdgeV v;fullscreenCoordinates(id,v.p,v.uv);SMAAEdgeDetectionVS(v.uv,v.offsets);return v; }
WeightV WeightVS(uint id:SV_VertexID) { WeightV v;fullscreenCoordinates(id,v.p,v.uv);SMAABlendingWeightCalculationVS(v.uv,v.pixel,v.offsets);return v; }
BlendV BlendVS(uint id:SV_VertexID) { BlendV v;fullscreenCoordinates(id,v.p,v.uv);SMAANeighborhoodBlendingVS(v.uv,v.offsets);return v; }
float2 EdgePS(EdgeV v):SV_TARGET { return SMAAColorEdgeDetectionPS(v.uv,v.offsets,inputTex); }
float4 WeightPS(WeightV v):SV_TARGET { return SMAABlendingWeightCalculationPS(v.uv,v.pixel,v.offsets,edgesTex,areaTex,searchTex,0); }
float4 BlendPS(BlendV v):SV_TARGET { return SMAANeighborhoodBlendingPS(v.uv,v.offsets,inputTex,weightsTex); }
)HLSL";
        std::string source=prefix+smaa_hlsl+wrappers;
        // v972: the Ultra preset's 0.05 threshold is tuned for 1080p flat screens. At a 2688x2880 eye the same edge is
        // thinner and a 0.05 colour delta misses low-contrast geometry; 0.03 catches those while the local-contrast
        // adaptation still rejects texture edges. Two blend iterations catch edges the first coverage pass reveals.
        {std::ostringstream value;value<<std::fixed<<std::setprecision(3)<<threshold_;
         const std::string needle="#define SMAA_THRESHOLD 0.05";
         const auto at=source.find(needle);
         if(at==std::string::npos) throw std::runtime_error("SMAA threshold define differs from the vendored version");
         source.replace(at,needle.size(),"#define SMAA_THRESHOLD "+value.str());}
        // Auto-assigned sampler slots differ between the three entry points.
        // Pin upstream's named samplers without changing the algorithm.
        for(const auto& name:std::array<std::string,2>{"LinearSampler","PointSampler"}) {
            const auto needle="SamplerState "+name+" {";const auto at=source.find(needle);
            if(at==std::string::npos || source.find(needle,at+1)!=std::string::npos)
                throw std::runtime_error("SMAA sampler declaration differs from the vendored version");
            source.replace(at,needle.size(),"SamplerState "+name+" : register("+(name=="LinearSampler"?"s0":"s1")+") {");
        }
        constexpr std::array<const char*,3> vs{"EdgeVS","WeightVS","BlendVS"},ps{"EdgePS","WeightPS","BlendPS"};
        for(unsigned pass=0;pass<3;++pass) {
            auto v=compile(source,vs[pass],"vs_5_0"),p=compile(source,ps[pass],"ps_5_0");
            checked(device_->CreateVertexShader(v->GetBufferPointer(),v->GetBufferSize(),nullptr,&vertex_[pass]),"vertex shader");
            checked(device_->CreatePixelShader(p->GetBufferPointer(),p->GetBufferSize(),nullptr,&pixel_[pass]),"pixel shader");
        }
        for(unsigned i=0;i<2;++i) {
            D3D11_SAMPLER_DESC desc{};desc.Filter=i?D3D11_FILTER_MIN_MAG_MIP_POINT:D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
            desc.AddressU=desc.AddressV=desc.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;desc.MaxLOD=D3D11_FLOAT32_MAX;
            checked(device_->CreateSamplerState(&desc,&samplers_[i]),"sampler");
        }
        D3D11_BUFFER_DESC cb{};cb.ByteWidth=16;cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;cb.Usage=D3D11_USAGE_DEFAULT;
        checked(device_->CreateBuffer(&cb,nullptr,&metrics_),"metrics buffer");
        D3D11_RASTERIZER_DESC raster{};raster.FillMode=D3D11_FILL_SOLID;raster.CullMode=D3D11_CULL_NONE;raster.DepthClipEnable=TRUE;
        checked(device_->CreateRasterizerState(&raster,&raster_),"rasterizer");
        D3D11_DEPTH_STENCIL_DESC depth{};depth.DepthFunc=D3D11_COMPARISON_ALWAYS;
        checked(device_->CreateDepthStencilState(&depth,&depth_),"depth state");
        auto area=texture(AREATEX_WIDTH,AREATEX_HEIGHT,DXGI_FORMAT_R8G8_UNORM,D3D11_BIND_SHADER_RESOURCE,areaTexBytes,AREATEX_PITCH);
        auto search=texture(SEARCHTEX_WIDTH,SEARCHTEX_HEIGHT,DXGI_FORMAT_R8_UNORM,D3D11_BIND_SHADER_RESOURCE,searchTexBytes,SEARCHTEX_PITCH);
        area_=srv(area.Get(),DXGI_FORMAT_R8G8_UNORM);search_=srv(search.Get(),DXGI_FORMAT_R8_UNORM);
    }
    ID3D11Texture2D* apply(ID3D11Texture2D* source) {
        if(!source) throw std::runtime_error("SMAA input is missing");
        D3D11_TEXTURE2D_DESC desc{};source->GetDesc(&desc);
        Ptr<ID3D11Device> owner;source->GetDevice(&owner);
        if(owner.Get()!=device_.Get() || desc.SampleDesc.Count!=1 || desc.ArraySize!=1 || desc.MipLevels!=1 ||
            (desc.Format!=DXGI_FORMAT_R8G8B8A8_UNORM && desc.Format!=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB && desc.Format!=DXGI_FORMAT_R8G8B8A8_TYPELESS))
            throw std::runtime_error("SMAA input format or device differs");
        ContextState::Scope scope(state_);
        resize(desc.Width,desc.Height);
        context_->CopyResource(input_.Get(),source);
        const std::array<float,4> metrics{1.f/static_cast<float>(width_),1.f/static_cast<float>(height_),static_cast<float>(width_),static_cast<float>(height_)};
        context_->UpdateSubresource(metrics_.Get(),0,nullptr,metrics.data(),0,0);
        const D3D11_VIEWPORT viewport{0,0,static_cast<float>(width_),static_cast<float>(height_),0,1};
        context_->RSSetViewports(1,&viewport);context_->RSSetState(raster_.Get());
        context_->IASetInputLayout(nullptr);context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context_->GSSetShader(nullptr,nullptr,0);context_->HSSetShader(nullptr,nullptr,0);context_->DSSetShader(nullptr,nullptr,0);
        context_->OMSetDepthStencilState(depth_.Get(),0);context_->OMSetBlendState(nullptr,nullptr,0xffffffff);
        auto cb=metrics_.Get();context_->VSSetConstantBuffers(0,1,&cb);context_->PSSetConstantBuffers(0,1,&cb);
        ID3D11SamplerState* samplers[]{samplers_[0].Get(),samplers_[1].Get()};context_->PSSetSamplers(0,2,samplers);
        constexpr std::array<float,4> clear{};
        ID3D11ShaderResourceView* empty[5]{};
        const std::array<ID3D11RenderTargetView*,3> targets{edges_rtv_.Get(),weights_rtv_.Get(),output_rtv_.Get()};
        for(int iteration=0;iteration<passes_;++iteration) {
            if(iteration) { // feed this eye's first coverage result back through the detector/blender once more
                context_->OMSetRenderTargets(0,nullptr,nullptr);
                context_->CopyResource(input_.Get(),output_.Get());
            }
            context_->ClearRenderTargetView(edges_rtv_.Get(),clear.data());context_->ClearRenderTargetView(weights_rtv_.Get(),clear.data());
            for(unsigned pass=0;pass<3;++pass) {
                context_->PSSetShaderResources(0,5,empty);context_->OMSetRenderTargets(1,&targets[pass],nullptr);
                ID3D11ShaderResourceView* inputs[5]{pass==2?input_linear_.Get():input_gamma_.Get(),
                    pass==1?edges_srv_.Get():nullptr,pass==2?weights_srv_.Get():nullptr,area_.Get(),search_.Get()};
                context_->PSSetShaderResources(0,5,inputs);
                context_->VSSetShader(vertex_[pass].Get(),nullptr,0);context_->PSSetShader(pixel_[pass].Get(),nullptr,0);
                context_->Draw(3,0);
            }
        }
        context_->PSSetShaderResources(0,5,empty);context_->OMSetRenderTargets(0,nullptr,nullptr);
        ++eye_passes;return output_.Get();
    }
};
} // namespace d2vr
