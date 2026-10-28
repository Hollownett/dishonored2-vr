#pragma once
#include "native_eye_art.hpp"
#include "note_hand_mesh.hpp"
#include <memory>

namespace d2vr {
// Draw the install-derived native arm surfaces from one coherent owned palette.
// Native sleeves bridge the authored open wrist seam; note/map pixels remain owned by the native UI.
class NoteHandArt {
    template<class T>using Ptr=Microsoft::WRL::ComPtr<T>;
    struct Vertex {float x,y,z,w,u,v;};
    Ptr<ID3D11Device> device_;Ptr<ID3D11DeviceContext> context_;ContextState state_;
    Ptr<ID3D11VertexShader> vs_;Ptr<ID3D11PixelShader> ps_;Ptr<ID3D11InputLayout> layout_;
    Ptr<ID3D11Buffer> vertices_;UINT capacity_{};
    Ptr<ID3D11Texture2D> depth_texture_;Ptr<ID3D11DepthStencilView> depth_view_;
    UINT width_{},height_{};
    Ptr<ID3D11DepthStencilState> write_depth_;
    Ptr<ID3D11RasterizerState> raster_;Ptr<ID3D11SamplerState> sampler_;
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
    std::array<std::shared_ptr<const note_hand::Mesh>,2> meshes_;
    std::array<std::vector<Ptr<ID3D11ShaderResourceView>>,2> textures_;
public:
    explicit NoteHandArt(ID3D11Device* device):device_(device),state_(device) {
        device_->GetImmediateContext(&context_);
        constexpr char shader[]=R"HLSL(
Texture2D art:register(t0);SamplerState linearClamp:register(s0);
struct V{float4 p:SV_POSITION;float2 uv:TEXCOORD0;};
V VS(float4 p:POSITION,float2 uv:TEXCOORD0){V v;v.p=p;v.uv=uv;return v;}
float4 PS(V v):SV_TARGET{return float4(art.Sample(linearClamp,v.uv).rgb,1);}
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
        checked(device_->CreateDepthStencilState(&d,&write_depth_));
        D3D11_RASTERIZER_DESC r{};r.FillMode=D3D11_FILL_SOLID;r.CullMode=D3D11_CULL_NONE;r.DepthClipEnable=TRUE;checked(device_->CreateRasterizerState(&r,&raster_));
        D3D11_SAMPLER_DESC s{};s.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;s.AddressU=s.AddressV=s.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;s.MaxLOD=D3D11_FLOAT32_MAX;
        checked(device_->CreateSamplerState(&s,&sampler_));
    }
    void set_mesh(unsigned side,std::shared_ptr<const note_hand::Mesh> mesh) {
        if(side>=2 || !mesh || mesh->side!=side || mesh->surfaces.empty())throw std::invalid_argument("Invalid note hand render mesh");
        if(meshes_[side]==mesh)return;
        std::vector<Ptr<ID3D11ShaderResourceView>> textures;
        for(const auto& surface:mesh->surfaces) {
            const auto& texture=surface.diffuse;
            if(surface.uv.size()!=surface.vertices.size() || !texture.width || !texture.height || texture.width>4096 || texture.height>4096 ||
               texture.rgba.size()!=static_cast<std::size_t>(texture.width)*texture.height*4)throw std::invalid_argument("Missing native note hand texture");
            D3D11_TEXTURE2D_DESC d{};d.Width=texture.width;d.Height=texture.height;d.MipLevels=d.ArraySize=d.SampleDesc.Count=1;
            d.Format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;d.Usage=D3D11_USAGE_IMMUTABLE;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA data{texture.rgba.data(),texture.width*4,0};Ptr<ID3D11Texture2D> image;
            checked(device_->CreateTexture2D(&d,&data,&image));Ptr<ID3D11ShaderResourceView> view;
            checked(device_->CreateShaderResourceView(image.Get(),nullptr,&view));textures.push_back(std::move(view));
        }
        meshes_[side]=std::move(mesh);textures_[side]=std::move(textures);
    }
    void draw(ID3D11Texture2D* eye,DXGI_FORMAT format,const XrView& view,
            const std::array<const note_hand::Geometry*,2>& hands) {
        if(!eye)throw std::invalid_argument("Missing note hand eye");
        D3D11_TEXTURE2D_DESC e{};eye->GetDesc(&e);Ptr<ID3D11Device> owner;eye->GetDevice(&owner);
        if(owner.Get()!=device_.Get() || e.SampleDesc.Count!=1 || e.ArraySize!=1 || !e.Width || !e.Height)
            throw std::invalid_argument("Invalid note hand eye");
        const auto q=normalized(Quaternion{view.pose.orientation.x,view.pose.orientation.y,view.pose.orientation.z,view.pose.orientation.w});
        const Quaternion inverse{-q.x,-q.y,-q.z,q.w};const Vec3 eye_position{view.pose.position.x,view.pose.position.y,view.pose.position.z};
        const double l=std::tan(view.fov.angleLeft),r=std::tan(view.fov.angleRight),b=std::tan(view.fov.angleDown),t=std::tan(view.fov.angleUp);
        if(!finite(eye_position) || !std::isfinite(l+r+b+t) || r<=l || t<=b)throw std::invalid_argument("Invalid note hand projection");
        scratch_.clear();std::array<std::vector<UINT>,2> counts;
        for(unsigned side=0;side<2;++side) {
            if(!meshes_[side] || !hands[side])throw std::invalid_argument("Missing posed native note hand");
            const auto& geometry=*hands[side];std::size_t offset{},indices{};
            for(const auto& surface:meshes_[side]->surfaces) {
                if(offset+surface.vertices.size()>geometry.vertices.size() || indices+surface.indices.size()>geometry.indices.size())
                    throw std::invalid_argument("Mismatched posed note hand geometry");
                for(auto index:surface.indices) {
                    if(index>=surface.vertices.size() || geometry.indices[indices++]!=offset+index)throw std::invalid_argument("Mismatched note hand triangle topology");
                    const auto local=rotated(inverse,geometry.vertices[offset+index]-eye_position);const double depth=-local.z;
                    if(!finite(local))throw std::invalid_argument("Nonfinite note hand geometry");
                    constexpr double near_z=.01,far_z=100.;const auto& uv=surface.uv[index];
                    if(!std::isfinite(uv[0]+uv[1]))throw std::invalid_argument("Nonfinite note hand UV");
                    scratch_.push_back({static_cast<float>((2*local.x-(r+l)*depth)/(r-l)),static_cast<float>((2*local.y-(t+b)*depth)/(t-b)),
                        static_cast<float>(far_z/(far_z-near_z)*depth-far_z*near_z/(far_z-near_z)),static_cast<float>(depth),uv[0],uv[1]});
                }
                counts[side].push_back(static_cast<UINT>(surface.indices.size()));offset+=surface.vertices.size();
            }
            if(offset!=geometry.vertices.size() || indices!=geometry.indices.size() || scratch_.size()>3000000)
                throw std::invalid_argument("Invalid posed note hand bounds");
        }
        if(scratch_.empty())throw std::invalid_argument("Empty note hand geometry");
        if(width_!=e.Width || height_!=e.Height) {
            depth_view_.Reset();depth_texture_.Reset();D3D11_TEXTURE2D_DESC d{};
            d.Width=e.Width;d.Height=e.Height;d.MipLevels=d.ArraySize=d.SampleDesc.Count=1;d.Format=DXGI_FORMAT_D32_FLOAT;d.BindFlags=D3D11_BIND_DEPTH_STENCIL;
            checked(device_->CreateTexture2D(&d,nullptr,&depth_texture_));checked(device_->CreateDepthStencilView(depth_texture_.Get(),nullptr,&depth_view_));width_=e.Width;height_=e.Height;
        }
        Ptr<ID3D11RenderTargetView> output;D3D11_RENDER_TARGET_VIEW_DESC rd{};rd.Format=format;rd.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
        checked(device_->CreateRenderTargetView(eye,&rd,&output));
        ContextState::Scope scope(state_);
        const D3D11_VIEWPORT vp{0,0,static_cast<float>(e.Width),static_cast<float>(e.Height),0,1};context_->RSSetViewports(1,&vp);context_->RSSetState(raster_.Get());
        context_->IASetInputLayout(layout_.Get());context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context_->VSSetShader(vs_.Get(),nullptr,0);context_->GSSetShader(nullptr,nullptr,0);context_->HSSetShader(nullptr,nullptr,0);context_->DSSetShader(nullptr,nullptr,0);
        context_->PSSetShader(ps_.Get(),nullptr,0);context_->OMSetBlendState(nullptr,nullptr,0xffffffff);context_->OMSetDepthStencilState(write_depth_.Get(),0);
        auto* rtv=output.Get();context_->OMSetRenderTargets(1,&rtv,depth_view_.Get());context_->ClearDepthStencilView(depth_view_.Get(),D3D11_CLEAR_DEPTH,1,0);
        auto* sampler=sampler_.Get();context_->PSSetSamplers(0,1,&sampler);upload(scratch_.data(),static_cast<UINT>(scratch_.size()));
        UINT first{};
        for(unsigned side=0;side<2;++side)for(unsigned surface=0;surface<counts[side].size();++surface) {
            if(!counts[side][surface])continue;
            auto* srv=textures_[side][surface].Get();context_->PSSetShaderResources(0,1,&srv);
            context_->Draw(counts[side][surface],first);first+=counts[side][surface];
        }
        ID3D11ShaderResourceView* empty{};context_->PSSetShaderResources(0,1,&empty);context_->OMSetRenderTargets(0,nullptr,nullptr);
    }
};
}
