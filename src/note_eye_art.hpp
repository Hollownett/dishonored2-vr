#pragma once
#include "native_eye_art.hpp"
#include "note_reading.hpp"
#include <vector>
#include <memory>

namespace d2vr {
// Actual skinned triangles from the accepted native palette, in XR space.
// Geometry and panel must belong to the exact displayed stereo frame.
struct NoteHandReceipt {
    std::shared_ptr<const std::vector<Vec3>> triangles;
    std::uint64_t tick{},pose_epoch{},reference_epoch{},frame{};
    std::uint64_t generation{};
    bool current(std::uint64_t now,std::uint64_t epoch,std::uint64_t reference,std::uint64_t displayed) const noexcept {
        return triangles && !triangles->empty() && triangles->size()%3==0 && triangles->size()<=600000 &&
            frame && frame==displayed && tick && now>=tick && now-tick<=100 && pose_epoch==epoch && reference_epoch==reference;
    }
};

// Composite isolated native note pixels into each eye. The private depth target
// contains only the matching native hand geometry; it never borrows scene depth.
// Hand pixels already in the eye remain intact when in front of the page.
class NoteEyeArt {
    template<class T>using Ptr=Microsoft::WRL::ComPtr<T>;
    struct Vertex {float x,y,z,w,u,v;};
    Ptr<ID3D11Device> device_;Ptr<ID3D11DeviceContext> context_;ContextState state_;
    Ptr<ID3D11VertexShader> vs_;Ptr<ID3D11PixelShader> ps_;Ptr<ID3D11InputLayout> layout_;
    Ptr<ID3D11Buffer> vertices_;UINT capacity_{};
    Ptr<ID3D11Texture2D> depth_texture_;Ptr<ID3D11DepthStencilView> depth_view_;
    UINT width_{},height_{};
    Ptr<ID3D11DepthStencilState> write_depth_,read_depth_;
    Ptr<ID3D11RasterizerState> raster_;Ptr<ID3D11BlendState> blend_;Ptr<ID3D11SamplerState> sampler_;
    std::vector<Vertex> scratch_;
    static void checked(HRESULT result) {if(FAILED(result))throw std::runtime_error("Note hand occlusion graphics operation failed");}
    void upload(const Vertex* data,UINT count) {
        if(count>capacity_) {
            vertices_.Reset();capacity_=count;
            D3D11_BUFFER_DESC b{};b.ByteWidth=count*sizeof(Vertex);b.Usage=D3D11_USAGE_DYNAMIC;
            b.BindFlags=D3D11_BIND_VERTEX_BUFFER;b.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
            checked(device_->CreateBuffer(&b,nullptr,&vertices_));
        }
        D3D11_MAPPED_SUBRESOURCE mapped{};checked(context_->Map(vertices_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped));
        std::memcpy(mapped.pData,data,count*sizeof(Vertex));context_->Unmap(vertices_.Get(),0);
        auto* buffer=vertices_.Get();const UINT stride=sizeof(Vertex),offset=0;context_->IASetVertexBuffers(0,1,&buffer,&stride,&offset);
    }
public:
    explicit NoteEyeArt(ID3D11Device* device):device_(device),state_(device) {
        device_->GetImmediateContext(&context_);
        constexpr char shader[]=R"HLSL(
Texture2D art:register(t0);SamplerState linearClamp:register(s0);
struct V{float4 p:SV_POSITION;float2 uv:TEXCOORD0;};
V VS(float4 p:POSITION,float2 uv:TEXCOORD0){V v;v.p=p;v.uv=uv;return v;}
float4 PS(V v):SV_TARGET{return art.Sample(linearClamp,v.uv);}
)HLSL";
        Ptr<ID3DBlob> vs,ps,errors;
        checked(D3DCompile(shader,sizeof(shader)-1,nullptr,nullptr,nullptr,"VS","vs_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&vs,&errors));
        checked(D3DCompile(shader,sizeof(shader)-1,nullptr,nullptr,nullptr,"PS","ps_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&ps,&errors));
        checked(device_->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&vs_));
        checked(device_->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&ps_));
        const D3D11_INPUT_ELEMENT_DESC elements[]={{"POSITION",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,16,D3D11_INPUT_PER_VERTEX_DATA,0}};
        checked(device_->CreateInputLayout(elements,2,vs->GetBufferPointer(),vs->GetBufferSize(),&layout_));
        D3D11_DEPTH_STENCIL_DESC d{};d.DepthEnable=TRUE;d.DepthFunc=D3D11_COMPARISON_LESS_EQUAL;d.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;
        checked(device_->CreateDepthStencilState(&d,&write_depth_));d.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;checked(device_->CreateDepthStencilState(&d,&read_depth_));
        D3D11_RASTERIZER_DESC r{};r.FillMode=D3D11_FILL_SOLID;r.CullMode=D3D11_CULL_NONE;r.DepthClipEnable=TRUE;checked(device_->CreateRasterizerState(&r,&raster_));
        D3D11_BLEND_DESC b{};auto& t=b.RenderTarget[0];t.BlendEnable=TRUE;t.SrcBlend=D3D11_BLEND_ONE;t.DestBlend=D3D11_BLEND_INV_SRC_ALPHA;t.BlendOp=D3D11_BLEND_OP_ADD;
        t.SrcBlendAlpha=D3D11_BLEND_ONE;t.DestBlendAlpha=D3D11_BLEND_INV_SRC_ALPHA;t.BlendOpAlpha=D3D11_BLEND_OP_ADD;t.RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
        checked(device_->CreateBlendState(&b,&blend_));
        D3D11_SAMPLER_DESC s{};s.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;s.AddressU=s.AddressV=s.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;s.MaxLOD=D3D11_FLOAT32_MAX;
        checked(device_->CreateSamplerState(&s,&sampler_));
    }
    void draw(ID3D11Texture2D* eye,ID3D11Texture2D* art,DXGI_FORMAT format,const D3D11_BOX& crop,
            const XrView& view,const NotePanelReceipt& panel,const std::array<NoteHandReceipt,2>& hands) {
        if(!eye || !art || eye==art)throw std::invalid_argument("Missing isolated note image");
        D3D11_TEXTURE2D_DESC e{},a{};eye->GetDesc(&e);art->GetDesc(&a);
        Ptr<ID3D11Device> eo,ao;eye->GetDevice(&eo);art->GetDevice(&ao);
        if(eo.Get()!=device_.Get() || ao.Get()!=device_.Get() || e.SampleDesc.Count!=1 || a.SampleDesc.Count!=1 || e.ArraySize!=1 || a.ArraySize!=1 ||
            !e.Width || !e.Height || crop.left>=crop.right || crop.top>=crop.bottom || crop.right>a.Width || crop.bottom>a.Height || crop.front || crop.back!=1)
            throw std::invalid_argument("Invalid note eye dimensions");
        const auto q=normalized(Quaternion{view.pose.orientation.x,view.pose.orientation.y,view.pose.orientation.z,view.pose.orientation.w});
        const Quaternion inverse{-q.x,-q.y,-q.z,q.w};const Vec3 eye_position{view.pose.position.x,view.pose.position.y,view.pose.position.z};
        const double l=std::tan(view.fov.angleLeft),r=std::tan(view.fov.angleRight),b=std::tan(view.fov.angleDown),t=std::tan(view.fov.angleUp);
        if(!std::isfinite(l+r+b+t) || r<=l || t<=b || !std::isfinite(panel.width+panel.height) || panel.width<=0 || panel.height<=0)
            throw std::invalid_argument("Invalid note projection");
        const auto project=[&](Vec3 p,float u=0,float v=0) {
            const auto local=rotated(inverse,p-eye_position);const double depth=-local.z;
            if(!finite(local))throw std::invalid_argument("Nonfinite note geometry");
            constexpr double near_z=.01,far_z=100.;
            return Vertex{static_cast<float>((2*local.x-(r+l)*depth)/(r-l)),static_cast<float>((2*local.y-(t+b)*depth)/(t-b)),
                static_cast<float>(far_z/(far_z-near_z)*depth-far_z*near_z/(far_z-near_z)),static_cast<float>(depth),u,v};
        };
        scratch_.clear();
        for(const auto& hand:hands) {
            if(!hand.triangles || hand.triangles->empty() || hand.triangles->size()%3 || hand.triangles->size()>600000)
                throw std::invalid_argument("Missing accepted note hand triangles");
            for(const auto& p:*hand.triangles)scratch_.push_back(project(p));
        }
        const auto orientation=normalized(panel.pose.orientation);
        std::array<Vertex,4> corners{};
        for(unsigned i=0;i<4;++i) {
            const float u=static_cast<float>(i&1),v=static_cast<float>((i>>1)&1);
            corners[i]=project(panel.pose.position+rotated(orientation,{(u-.5)*panel.width,(.5-v)*panel.height,0}),
                (crop.left+u*(crop.right-crop.left))/a.Width,(crop.top+v*(crop.bottom-crop.top))/a.Height);
        }
        const std::array<Vertex,6> page{corners[0],corners[1],corners[2],corners[2],corners[1],corners[3]};
        if(width_!=e.Width || height_!=e.Height) {
            depth_view_.Reset();depth_texture_.Reset();D3D11_TEXTURE2D_DESC d{};
            d.Width=e.Width;d.Height=e.Height;d.MipLevels=d.ArraySize=d.SampleDesc.Count=1;d.Format=DXGI_FORMAT_D32_FLOAT;d.BindFlags=D3D11_BIND_DEPTH_STENCIL;
            checked(device_->CreateTexture2D(&d,nullptr,&depth_texture_));checked(device_->CreateDepthStencilView(depth_texture_.Get(),nullptr,&depth_view_));width_=e.Width;height_=e.Height;
        }
        Ptr<ID3D11RenderTargetView> output;D3D11_RENDER_TARGET_VIEW_DESC rd{};rd.Format=format;rd.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
        checked(device_->CreateRenderTargetView(eye,&rd,&output));
        Ptr<ID3D11ShaderResourceView> input;D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
        // Read the page with the eye's own transfer, as the quad path's byte copy shows it: the native movie
        // blends gamma-space bytes, and a UNORM view into an sRGB eye encoded them twice (lighter page, loud red ink).
        sd.Format=a.Format==DXGI_FORMAT_R8G8B8A8_TYPELESS?format:a.Format;
        sd.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;sd.Texture2D.MipLevels=1;
        checked(device_->CreateShaderResourceView(art,&sd,&input));
        ContextState::Scope scope(state_);
        const D3D11_VIEWPORT vp{0,0,static_cast<float>(e.Width),static_cast<float>(e.Height),0,1};context_->RSSetViewports(1,&vp);context_->RSSetState(raster_.Get());
        context_->IASetInputLayout(layout_.Get());context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context_->VSSetShader(vs_.Get(),nullptr,0);context_->GSSetShader(nullptr,nullptr,0);context_->HSSetShader(nullptr,nullptr,0);context_->DSSetShader(nullptr,nullptr,0);
        context_->PSSetShader(nullptr,nullptr,0);context_->OMSetBlendState(nullptr,nullptr,0xffffffff);context_->OMSetDepthStencilState(write_depth_.Get(),0);
        context_->OMSetRenderTargets(0,nullptr,depth_view_.Get());context_->ClearDepthStencilView(depth_view_.Get(),D3D11_CLEAR_DEPTH,1,0);
        upload(scratch_.data(),static_cast<UINT>(scratch_.size()));context_->Draw(static_cast<UINT>(scratch_.size()),0);
        auto* rtv=output.Get();context_->OMSetRenderTargets(1,&rtv,depth_view_.Get());context_->OMSetDepthStencilState(read_depth_.Get(),0);context_->OMSetBlendState(blend_.Get(),nullptr,0xffffffff);
        context_->PSSetShader(ps_.Get(),nullptr,0);auto* srv=input.Get();auto* sampler=sampler_.Get();context_->PSSetShaderResources(0,1,&srv);context_->PSSetSamplers(0,1,&sampler);
        upload(page.data(),6);context_->Draw(6,0);
        ID3D11ShaderResourceView* empty{};context_->PSSetShaderResources(0,1,&empty);context_->OMSetRenderTargets(0,nullptr,nullptr);
    }
};
}
