#pragma once
#include "tracked_pose.hpp"
#include "context_state.hpp"
#include <openxr/openxr.h>
#include <d3dcompiler.h>
#include <optional>
#include <array>

namespace d2vr {
struct NativeArtRect {float left{},top{},right{},bottom{};};
// Test all physical quad corners in BOTH asymmetric eye frusta. Centre-only
// tests allow long health/mana panels to extend outside the visible lens area.
inline bool native_panel_contained(const std::array<XrView,2>& eyes,const TrackedPose& pose,
        double width,double height,double margin=.08) noexcept {
    if(!finite(pose.position) || !std::isfinite(width+height+margin) || width<=0 || height<=0 || margin<0 || margin>=.5)return false;
    try {
        const auto panel=normalized(pose.orientation);
        for(const auto& eye:eyes) {
            const auto q=normalized(Quaternion{eye.pose.orientation.x,eye.pose.orientation.y,eye.pose.orientation.z,eye.pose.orientation.w});
            const Quaternion inverse{-q.x,-q.y,-q.z,q.w};
            const double left=std::tan(eye.fov.angleLeft),right=std::tan(eye.fov.angleRight),
                down=std::tan(eye.fov.angleDown),up=std::tan(eye.fov.angleUp);
            if(!std::isfinite(left+right+down+up) || right<=left || up<=down)return false;
            for(int x:{-1,1})for(int y:{-1,1}) {
                const auto corner=pose.position+rotated(panel,{x*width*.5,y*height*.5,0});
                const auto local=rotated(inverse,corner-Vec3{eye.pose.position.x,eye.pose.position.y,eye.pose.position.z});
                if(!finite(local) || -local.z<.06)return false;
                const double u=(local.x/-local.z-left)/(right-left),v=(local.y/-local.z-down)/(up-down);
                if(u<margin || u>1-margin || v<margin || v>1-margin)return false;
            }
        }
        return true;
    }catch(...){return false;}
}
// Preserve the requested angular size/aspect and orientation. Move only as
// far toward the central binocular reading area as needed; never clip pixels.
inline std::optional<TrackedPose> native_panel_fit(const std::array<XrView,2>& eyes,TrackedPose desired,
        double width,double height,double margin=.08) noexcept {
    if(native_panel_contained(eyes,desired,width,height,margin))return desired;
    try {
        const auto q=normalized(Quaternion{eyes[0].pose.orientation.x,eyes[0].pose.orientation.y,eyes[0].pose.orientation.z,eyes[0].pose.orientation.w});
        const Vec3 head{(eyes[0].pose.position.x+eyes[1].pose.position.x)*.5,
            (eyes[0].pose.position.y+eyes[1].pose.position.y)*.5,(eyes[0].pose.position.z+eyes[1].pose.position.z)*.5};
        const auto forward=rotated(q,{0,0,-1});
        const double depth=dot(desired.position-head,forward);
        if(!std::isfinite(depth) || depth<.18 || depth>.9)return {};
        auto central=desired;central.position=head+forward*depth;
        if(!native_panel_contained(eyes,central,width,height,margin))return {};
        double low=0,high=1;
        for(unsigned i=0;i<24;++i) {
            const double t=(low+high)*.5;auto trial=desired;
            trial.position=desired.position*(1-t)+central.position*t;
            if(native_panel_contained(eyes,trial,width,height,margin))high=t;else low=t;
        }
        desired.position=desired.position*(1-high)+central.position*high;return desired;
    }catch(...){return {};}
}
// A native-art billboard centred at a verified XR-local object point. The
// eye's own asymmetric FOV and rendered pose determine placement/parallax.
inline std::optional<NativeArtRect> native_art_rect(const XrView& eye,Vec3 point,double width,double aspect) {
    if(!finite(point) || !std::isfinite(width) || width<=0 || width>64 || !std::isfinite(aspect) || aspect<=0) return {};
    const auto& p=eye.pose;Quaternion q{p.orientation.x,p.orientation.y,p.orientation.z,p.orientation.w};
    try {q=normalized(q);}catch(...){return {};}
    const auto local=rotated({-q.x,-q.y,-q.z,q.w},point-Vec3{p.position.x,p.position.y,p.position.z});
    const double depth=-local.z;
    if(!finite(local) || depth<.05) return {};
    const double l=std::tan(eye.fov.angleLeft),r=std::tan(eye.fov.angleRight),u=std::tan(eye.fov.angleUp),d=std::tan(eye.fov.angleDown);
    if(!std::isfinite(l+r+u+d) || r<=l || u<=d) return {};
    const double x=(2*local.x/depth-r-l)/(r-l),y=(2*local.y/depth-u-d)/(u-d);
    const double halfw=width/(depth*(r-l)),halfh=width/(aspect*depth*(u-d));
    const NativeArtRect rect{static_cast<float>(x-halfw),static_cast<float>(y+halfh),static_cast<float>(x+halfw),static_cast<float>(y-halfh)};
    if(rect.right<=-1 || rect.left>=1 || rect.top<=-1 || rect.bottom>=1) return {};
    return rect;
}
// Draws supplied native pixels into an acquired eye, never another XR layer.
// The producer must isolate native artwork before publishing it; this class
// does not identify children by rectangles or paint replacement HUD content.
class NativeEyeArt {
    template<class T>using Ptr=Microsoft::WRL::ComPtr<T>;
    Ptr<ID3D11Device> device_;Ptr<ID3D11DeviceContext> context_;ContextState state_;
    Ptr<ID3D11VertexShader> vs_;Ptr<ID3D11PixelShader> ps_;Ptr<ID3D11Buffer> constants_;
    Ptr<ID3D11SamplerState> sampler_;Ptr<ID3D11RasterizerState> raster_;Ptr<ID3D11DepthStencilState> depth_;
    Ptr<ID3D11BlendState> premultiplied_,straight_;
    static void checked(HRESULT value){if(FAILED(value))throw std::runtime_error("Native eye artwork graphics operation failed");}
public:
    explicit NativeEyeArt(ID3D11Device* device):device_(device),state_(device){
        device_->GetImmediateContext(&context_);
        constexpr char source[]=R"HLSL(
Texture2D nativeArt:register(t0);SamplerState artSampler:register(s0);
cbuffer Layout:register(b0){float4 rect;float4 crop;}
struct V{float4 pos:SV_POSITION;float2 uv:TEXCOORD0;};
V VS(uint id:SV_VertexID){float2 c=float2(id&1,(id>>1)&1);V v;v.pos=float4(lerp(rect.xy,rect.zw,c),0,1);v.uv=lerp(crop.xy,crop.zw,c);return v;}
float4 PS(V v):SV_TARGET{return nativeArt.Sample(artSampler,v.uv);}
)HLSL";
        Ptr<ID3DBlob> vs,ps,errors;checked(D3DCompile(source,sizeof(source)-1,nullptr,nullptr,nullptr,"VS","vs_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&vs,&errors));
        checked(D3DCompile(source,sizeof(source)-1,nullptr,nullptr,nullptr,"PS","ps_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&ps,&errors));
        checked(device_->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&vs_));checked(device_->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&ps_));
        D3D11_BUFFER_DESC b{};b.ByteWidth=32;b.BindFlags=D3D11_BIND_CONSTANT_BUFFER;b.Usage=D3D11_USAGE_DEFAULT;checked(device_->CreateBuffer(&b,nullptr,&constants_));
        D3D11_SAMPLER_DESC s{};s.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;s.AddressU=s.AddressV=s.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;s.MaxLOD=D3D11_FLOAT32_MAX;checked(device_->CreateSamplerState(&s,&sampler_));
        D3D11_RASTERIZER_DESC r{};r.FillMode=D3D11_FILL_SOLID;r.CullMode=D3D11_CULL_NONE;r.DepthClipEnable=TRUE;checked(device_->CreateRasterizerState(&r,&raster_));
        D3D11_DEPTH_STENCIL_DESC d{};d.DepthFunc=D3D11_COMPARISON_ALWAYS;checked(device_->CreateDepthStencilState(&d,&depth_));
        D3D11_BLEND_DESC blend{};auto& target=blend.RenderTarget[0];target.BlendEnable=TRUE;target.SrcBlend=D3D11_BLEND_ONE;target.DestBlend=D3D11_BLEND_INV_SRC_ALPHA;target.BlendOp=D3D11_BLEND_OP_ADD;
        target.SrcBlendAlpha=D3D11_BLEND_ONE;target.DestBlendAlpha=D3D11_BLEND_INV_SRC_ALPHA;target.BlendOpAlpha=D3D11_BLEND_OP_ADD;target.RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
        checked(device_->CreateBlendState(&blend,&premultiplied_));target.SrcBlend=D3D11_BLEND_SRC_ALPHA;checked(device_->CreateBlendState(&blend,&straight_));
    }
    void draw(ID3D11Texture2D* eye,ID3D11Texture2D* art,const D3D11_BOX& crop,NativeArtRect rect,DXGI_FORMAT format,bool premultiplied=true,DXGI_FORMAT source_format=DXGI_FORMAT_UNKNOWN){
        if(!eye || !art || eye==art)throw std::invalid_argument("Native artwork requires separate source and eye textures");
        D3D11_TEXTURE2D_DESC e{},a{};eye->GetDesc(&e);art->GetDesc(&a);
        Ptr<ID3D11Device> eo,ao;eye->GetDevice(&eo);art->GetDevice(&ao);
        if(eo.Get()!=device_.Get() || ao.Get()!=device_.Get() || e.SampleDesc.Count!=1 || a.SampleDesc.Count!=1 ||
           e.ArraySize!=1 || a.ArraySize!=1 || !e.Width || !e.Height || crop.left>=crop.right || crop.top>=crop.bottom || crop.right>a.Width || crop.bottom>a.Height || crop.front || crop.back!=1)
            throw std::invalid_argument("Invalid native artwork capture geometry/device");
        ContextState::Scope scope(state_);
        D3D11_SHADER_RESOURCE_VIEW_DESC sd{};sd.Format=source_format==DXGI_FORMAT_UNKNOWN?format:source_format;sd.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;sd.Texture2D.MipLevels=1;
        Ptr<ID3D11ShaderResourceView> input;checked(device_->CreateShaderResourceView(art,&sd,&input));
        D3D11_RENDER_TARGET_VIEW_DESC rd{};rd.Format=format;rd.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;Ptr<ID3D11RenderTargetView> output;checked(device_->CreateRenderTargetView(eye,&rd,&output));
        const std::array<float,8> data{rect.left,rect.top,rect.right,rect.bottom,static_cast<float>(crop.left)/a.Width,static_cast<float>(crop.top)/a.Height,static_cast<float>(crop.right)/a.Width,static_cast<float>(crop.bottom)/a.Height};
        context_->UpdateSubresource(constants_.Get(),0,nullptr,data.data(),0,0);
        D3D11_VIEWPORT viewport{0,0,static_cast<float>(e.Width),static_cast<float>(e.Height),0,1};context_->RSSetViewports(1,&viewport);context_->RSSetState(raster_.Get());
        context_->IASetInputLayout(nullptr);context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);context_->VSSetShader(vs_.Get(),nullptr,0);context_->PSSetShader(ps_.Get(),nullptr,0);
        context_->GSSetShader(nullptr,nullptr,0);context_->HSSetShader(nullptr,nullptr,0);context_->DSSetShader(nullptr,nullptr,0);context_->OMSetDepthStencilState(depth_.Get(),0);
        context_->OMSetBlendState(premultiplied?premultiplied_.Get():straight_.Get(),nullptr,0xffffffff);
        auto* constant=constants_.Get();context_->VSSetConstantBuffers(0,1,&constant);auto* sampler=sampler_.Get();context_->PSSetSamplers(0,1,&sampler);auto* srv=input.Get();context_->PSSetShaderResources(0,1,&srv);
        auto* rtv=output.Get();context_->OMSetRenderTargets(1,&rtv,nullptr);context_->Draw(4,0);
        ID3D11ShaderResourceView* empty{};context_->PSSetShaderResources(0,1,&empty);context_->OMSetRenderTargets(0,nullptr,nullptr);
    }
};
} // namespace d2vr
