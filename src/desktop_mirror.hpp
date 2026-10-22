#pragma once
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <string>
#include <stdexcept>
#include "context_state.hpp"

namespace d2vr {
struct MirrorRect {float x,y,width,height;};
// Modes 0 (right eye), 1 (left eye) and 2 (both) draw into the desktop window; 3 (off) and anything else draw nothing: no canvas, no copy.
inline bool mirror_draws(int mode) noexcept {return mode>=0 && mode<=2;}
// Desktop composition plan: index 0 is the left eye, 1 the right eye. A shown
// eye is fitted inside its area with its aspect preserved and the leftover
// letterbox/pillarbox space centred. Mode 0/1 fill the target with one eye,
// mode 2 splits the target into a left and a right half.
inline std::array<std::optional<MirrorRect>,2> mirror_layout(int mode,UINT target_width,UINT target_height,
                                                            UINT eye_width,UINT eye_height) {
    std::array<std::optional<MirrorRect>,2> result{};
    if(!mirror_draws(mode) || !target_width || !target_height || !eye_width || !eye_height) return result;
    const auto fit=[](UINT area_x,UINT area_y,UINT area_width,UINT area_height,UINT source_width,UINT source_height) {
        const auto scale=std::min(static_cast<double>(area_width)/source_width,
                                  static_cast<double>(area_height)/source_height);
        const auto width=static_cast<UINT>(std::clamp<long>(std::lround(source_width*scale),1,static_cast<long>(area_width)));
        const auto height=static_cast<UINT>(std::clamp<long>(std::lround(source_height*scale),1,static_cast<long>(area_height)));
        return MirrorRect{static_cast<float>(area_x+(area_width-width)/2),static_cast<float>(area_y+(area_height-height)/2),
            static_cast<float>(width),static_cast<float>(height)};
    };
    if(mode==2) {
        const auto half=target_width/2;
        if(half) result[0]=fit(0,0,half,target_height,eye_width,eye_height);
        if(half<target_width) result[1]=fit(half,0,target_width-half,target_height,eye_width,eye_height);
    } else result[mode==0?1:0]=fit(0,0,target_width,target_height,eye_width,eye_height);
    return result;
}
// Composes the VR eye images into the game's desktop backbuffer. The canvas is
// an internal copy-compatible target that the caller blits over the backbuffer
// once both eyes are drawn; reset() drops it when the window changes size.
class DesktopMirror {
    template<class T> using Ptr=Microsoft::WRL::ComPtr<T>;
    Ptr<ID3D11Device> device_;
    Ptr<ID3D11DeviceContext> context_;
    ContextState state_;
    Ptr<ID3D11VertexShader> vertex_;
    Ptr<ID3D11PixelShader> pixel_;
    Ptr<ID3D11SamplerState> sampler_;
    Ptr<ID3D11Buffer> geometry_;
    Ptr<ID3D11RasterizerState> raster_;
    Ptr<ID3D11DepthStencilState> depth_;
    Ptr<ID3D11Texture2D> canvas_,eye_copy_;
    Ptr<ID3D11RenderTargetView> canvas_rtv_;
    Ptr<ID3D11ShaderResourceView> eye_copy_srv_;
    UINT width_{},height_{},eye_copy_width_{},eye_copy_height_{};
    DXGI_FORMAT format_{DXGI_FORMAT_UNKNOWN},eye_copy_format_{DXGI_FORMAT_UNKNOWN};
    static void checked(HRESULT result,const char* label) {
        if(FAILED(result)) throw std::runtime_error(std::string("Desktop mirror: ")+label+" ("+std::to_string(result)+")");
    }
    static bool rgba8(DXGI_FORMAT format) noexcept {
        return format==DXGI_FORMAT_R8G8B8A8_TYPELESS || format==DXGI_FORMAT_R8G8B8A8_UNORM ||
            format==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    }
    static Ptr<ID3DBlob> compile(const std::string& source,const char* entry,const char* profile) {
        Ptr<ID3DBlob> binary,errors;
        const auto result=D3DCompile(source.data(),source.size(),"D2VR desktop mirror",nullptr,nullptr,entry,profile,
            D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&binary,&errors);
        if(FAILED(result)) throw std::runtime_error(std::string("Desktop mirror shader ")+entry+": "+
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
    // A typed eye only accepts a view in its own format, and a backbuffer-style
    // source may have no readable view at all; both fall back to a copy.
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
    Ptr<ID3D11ShaderResourceView> readable(ID3D11Texture2D* eye,const D3D11_TEXTURE2D_DESC& desc) {
        if(desc.BindFlags&D3D11_BIND_SHADER_RESOURCE) {
            if(auto view=try_srv(eye,format_)) return view;
        }
        if(!eye_copy_ || eye_copy_width_!=desc.Width || eye_copy_height_!=desc.Height || eye_copy_format_!=format_) {
            auto copy=texture(desc.Width,desc.Height,DXGI_FORMAT_R8G8B8A8_TYPELESS,D3D11_BIND_SHADER_RESOURCE);
            eye_copy_srv_=srv(copy.Get(),format_);
            eye_copy_=copy;eye_copy_width_=desc.Width;eye_copy_height_=desc.Height;eye_copy_format_=format_;
        }
        context_->CopyResource(eye_copy_.Get(),eye);
        return eye_copy_srv_;
    }
public:
    explicit DesktopMirror(ID3D11Device* device):device_(device),state_(device) {
        device_->GetImmediateContext(&context_);
        const std::string source=R"HLSL(
Texture2D eyeTex : register(t0);
SamplerState eyeSampler : register(s0);
cbuffer Geometry : register(b0) { float4 rect; float4 target; }
struct Vertex { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
Vertex MirrorVS(uint id : SV_VertexID) {
    const float2 corner=float2(id&1,(id>>1)&1);
    const float2 pixel=rect.xy+corner*rect.zw;
    Vertex v;v.uv=corner;
    v.position=float4(pixel.x/target.x*2-1,1-pixel.y/target.y*2,0,1);return v;
}
float4 MirrorPS(Vertex v) : SV_TARGET { return eyeTex.Sample(eyeSampler,v.uv); }
)HLSL";
        auto v=compile(source,"MirrorVS","vs_5_0"),p=compile(source,"MirrorPS","ps_5_0");
        checked(device_->CreateVertexShader(v->GetBufferPointer(),v->GetBufferSize(),nullptr,&vertex_),"vertex shader");
        checked(device_->CreatePixelShader(p->GetBufferPointer(),p->GetBufferSize(),nullptr,&pixel_),"pixel shader");
        D3D11_SAMPLER_DESC sampler{};sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sampler.MaxLOD=D3D11_FLOAT32_MAX;
        checked(device_->CreateSamplerState(&sampler,&sampler_),"sampler");
        D3D11_BUFFER_DESC constants{};constants.ByteWidth=32;constants.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        constants.Usage=D3D11_USAGE_DEFAULT;
        checked(device_->CreateBuffer(&constants,nullptr,&geometry_),"geometry buffer");
        D3D11_RASTERIZER_DESC rasterizer{};rasterizer.FillMode=D3D11_FILL_SOLID;rasterizer.CullMode=D3D11_CULL_NONE;
        rasterizer.DepthClipEnable=TRUE;
        checked(device_->CreateRasterizerState(&rasterizer,&raster_),"rasterizer");
        D3D11_DEPTH_STENCIL_DESC depthState{};depthState.DepthFunc=D3D11_COMPARISON_ALWAYS;
        checked(device_->CreateDepthStencilState(&depthState,&depth_),"depth state");
    }
    void begin(UINT target_width,UINT target_height,DXGI_FORMAT target_format) {
        if(!target_width || !target_height) throw std::runtime_error("Desktop mirror target size is invalid");
        if(target_format!=DXGI_FORMAT_R8G8B8A8_UNORM && target_format!=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)
            throw std::runtime_error("Desktop mirror target format is unsupported");
        if(!canvas_ || target_width!=width_ || target_height!=height_ || target_format!=format_) {
            auto canvas=texture(target_width,target_height,DXGI_FORMAT_R8G8B8A8_TYPELESS,D3D11_BIND_RENDER_TARGET);
            auto view=rtv(canvas.Get(),target_format);
            canvas_=canvas;canvas_rtv_=view;width_=target_width;height_=target_height;format_=target_format;
        }
        // Clearing is independent of the game's render-target binding.
        constexpr std::array<float,4> black{};
        context_->ClearRenderTargetView(canvas_rtv_.Get(),black.data());
    }
    void draw(ID3D11Texture2D* eye,MirrorRect rect) {
        if(!canvas_) throw std::runtime_error("Desktop mirror target is missing");
        if(!eye) throw std::runtime_error("Desktop mirror eye is missing");
        if(!(rect.width>0) || !(rect.height>0)) throw std::runtime_error("Desktop mirror rectangle is invalid");
        D3D11_TEXTURE2D_DESC desc{};eye->GetDesc(&desc);
        Ptr<ID3D11Device> owner;eye->GetDevice(&owner);
        if(owner.Get()!=device_.Get() || desc.SampleDesc.Count!=1 || desc.ArraySize!=1 || desc.MipLevels!=1 ||
            !rgba8(desc.Format))
            throw std::runtime_error("Desktop mirror eye format or device differs");
        ContextState::Scope scope(state_);
        auto read=readable(eye,desc);
        const std::array<float,8> geometry{rect.x,rect.y,rect.width,rect.height,
            static_cast<float>(width_),static_cast<float>(height_),0.f,0.f};
        context_->UpdateSubresource(geometry_.Get(),0,nullptr,geometry.data(),0,0);
        const D3D11_VIEWPORT viewport{0,0,static_cast<float>(width_),static_cast<float>(height_),0,1};
        context_->RSSetViewports(1,&viewport);context_->RSSetState(raster_.Get());
        context_->IASetInputLayout(nullptr);context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        context_->GSSetShader(nullptr,nullptr,0);context_->HSSetShader(nullptr,nullptr,0);context_->DSSetShader(nullptr,nullptr,0);
        context_->OMSetDepthStencilState(depth_.Get(),0);context_->OMSetBlendState(nullptr,nullptr,0xffffffff);
        auto constants=geometry_.Get();context_->VSSetConstantBuffers(0,1,&constants);context_->PSSetConstantBuffers(0,1,&constants);
        ID3D11SamplerState* samplers[]{sampler_.Get()};context_->PSSetSamplers(0,1,samplers);
        ID3D11ShaderResourceView* inputs[]{read.Get()};context_->PSSetShaderResources(0,1,inputs);
        auto* target=canvas_rtv_.Get();context_->OMSetRenderTargets(1,&target,nullptr);
        context_->VSSetShader(vertex_.Get(),nullptr,0);context_->PSSetShader(pixel_.Get(),nullptr,0);
        context_->Draw(4,0);
        ID3D11ShaderResourceView* empty[1]{};context_->PSSetShaderResources(0,1,empty);
        context_->OMSetRenderTargets(0,nullptr,nullptr);
    }
    void end(ID3D11Texture2D* backbuffer) {
        if(!canvas_) throw std::runtime_error("Desktop mirror canvas is missing");
        if(!backbuffer) throw std::runtime_error("Desktop mirror backbuffer is missing");
        D3D11_TEXTURE2D_DESC desc{};backbuffer->GetDesc(&desc);
        Ptr<ID3D11Device> owner;backbuffer->GetDevice(&owner);
        if(owner.Get()!=device_.Get() || desc.Width!=width_ || desc.Height!=height_ || !rgba8(desc.Format))
            throw std::runtime_error("Desktop mirror backbuffer does not match the canvas");
        context_->CopyResource(backbuffer,canvas_.Get());
    }
    // Window resize: drop every size- or format-dependent resource.
    void reset() {
        canvas_.Reset();canvas_rtv_.Reset();eye_copy_.Reset();eye_copy_srv_.Reset();
        width_=height_=eye_copy_width_=eye_copy_height_=0;
        format_=eye_copy_format_=DXGI_FORMAT_UNKNOWN;
    }
};
} // namespace d2vr
