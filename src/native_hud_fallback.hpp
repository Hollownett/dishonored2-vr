#pragma once
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <mutex>
#include <memory>

namespace d2vr {
// Full native child fallback, recorded into the caller's immediate/deferred
// queue after capture restores its original sole target. Never reads GPU data.
class NativeHudFallback {
    template<class T> using Ptr=Microsoft::WRL::ComPtr<T>;
    struct Device {
        Ptr<ID3D11Device> device;
        Ptr<ID3D11VertexShader> vs;Ptr<ID3D11PixelShader> ps,ps_scaled;
        Ptr<ID3D11SamplerState> sampler,sampler_linear;Ptr<ID3D11RasterizerState> raster;
        Ptr<ID3D11DepthStencilState> depth;Ptr<ID3D11BlendState> premultiplied,straight;
        Ptr<ID3D11Buffer> uv;
        struct View {Ptr<ID3D11Texture2D> texture;Ptr<ID3D11ShaderResourceView> srv;DXGI_FORMAT format{};};
        std::array<View,12> views;unsigned next{};
    };
    std::array<std::shared_ptr<Device>,4> devices_;
    std::mutex mutex_;
    struct State {
        ID3D11DeviceContext* c;
        Ptr<ID3D11VertexShader> vs;Ptr<ID3D11PixelShader> ps;Ptr<ID3D11GeometryShader> gs;
        Ptr<ID3D11HullShader> hs;Ptr<ID3D11DomainShader> ds;
        std::array<std::array<ID3D11ClassInstance*,256>,5> classes{};
        std::array<UINT,5> counts{256,256,256,256,256};
        Ptr<ID3D11InputLayout> layout;D3D11_PRIMITIVE_TOPOLOGY topology{};
        Ptr<ID3D11ShaderResourceView> srv;Ptr<ID3D11SamplerState> sampler;Ptr<ID3D11Buffer> ps_constant;
        Ptr<ID3D11BlendState> blend;FLOAT factors[4]{};UINT mask{};
        Ptr<ID3D11DepthStencilState> depth;UINT stencil{};
        std::array<ID3D11RenderTargetView*,8> targets{};Ptr<ID3D11DepthStencilView> dsv;
        std::array<ID3D11UnorderedAccessView*,8> uavs{};
        Ptr<ID3D11RasterizerState> raster;UINT viewport_count=16;std::array<D3D11_VIEWPORT,16> viewports{};
        Ptr<ID3D11Predicate> predicate;BOOL predicate_value{};
        bool modified{};
        explicit State(ID3D11DeviceContext* context):c(context) {
            c->VSGetShader(&vs,classes[0].data(),&counts[0]);c->PSGetShader(&ps,classes[1].data(),&counts[1]);
            c->GSGetShader(&gs,classes[2].data(),&counts[2]);c->HSGetShader(&hs,classes[3].data(),&counts[3]);c->DSGetShader(&ds,classes[4].data(),&counts[4]);
            c->IAGetInputLayout(&layout);c->IAGetPrimitiveTopology(&topology);
            c->PSGetShaderResources(0,1,&srv);c->PSGetSamplers(0,1,&sampler);c->PSGetConstantBuffers(0,1,&ps_constant);
            c->OMGetBlendState(&blend,factors,&mask);c->OMGetDepthStencilState(&depth,&stencil);
            c->OMGetRenderTargetsAndUnorderedAccessViews(8,targets.data(),&dsv,0,8,uavs.data());
            c->RSGetState(&raster);c->RSGetViewports(&viewport_count,viewports.data());c->GetPredication(&predicate,&predicate_value);
        }
        ~State() {
            if(modified) {
            ID3D11ShaderResourceView* empty=nullptr;c->PSSetShaderResources(0,1,&empty);
            // The caller requires a sole RTV; preserve any compatible pixel
            // UAV slots and their append counters rather than binding 8 RTVs.
            c->OMSetRenderTargetsAndUnorderedAccessViews(1,targets.data(),dsv.Get(),1,7,uavs.data()+1,nullptr);
            c->VSSetShader(vs.Get(),classes[0].data(),counts[0]);c->PSSetShader(ps.Get(),classes[1].data(),counts[1]);
            c->GSSetShader(gs.Get(),classes[2].data(),counts[2]);c->HSSetShader(hs.Get(),classes[3].data(),counts[3]);c->DSSetShader(ds.Get(),classes[4].data(),counts[4]);
            c->IASetInputLayout(layout.Get());c->IASetPrimitiveTopology(topology);
            auto* source=srv.Get();c->PSSetShaderResources(0,1,&source);auto* sample=sampler.Get();c->PSSetSamplers(0,1,&sample);
            auto* constant=ps_constant.Get();c->PSSetConstantBuffers(0,1,&constant);
            c->OMSetBlendState(blend.Get(),factors,mask);c->OMSetDepthStencilState(depth.Get(),stencil);
            c->RSSetState(raster.Get());c->RSSetViewports(viewport_count,viewports.data());c->SetPredication(predicate.Get(),predicate_value);
            }
            for(auto* target:targets)if(target)target->Release();
            for(auto* uav:uavs)if(uav)uav->Release();
            for(auto& stage:classes)for(auto* instance:stage)if(instance)instance->Release();
        }
        State(const State&)=delete;State& operator=(const State&)=delete;
    };
    static bool rgba(DXGI_FORMAT f) {return f==DXGI_FORMAT_R8G8B8A8_UNORM || f==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;}
    static std::shared_ptr<Device> create(ID3D11Device* device) {
        auto d=std::make_shared<Device>();d->device=device;
        constexpr char shader[]=R"(
cbuffer Uv:register(b0){float4 uv_rect;float4 fade;};
Texture2D art:register(t0);SamplerState sampleArt:register(s0);
struct V {float4 position:SV_POSITION;float2 uv:TEXCOORD0;};
V VS(uint id:SV_VertexID) {V v;v.uv=float2((id<<1)&2,id&2);v.position=float4(v.uv*float2(2,-2)+float2(-1,1),0,1);return v;}
float4 PS(V v):SV_TARGET {return art.Sample(sampleArt,v.uv);}
float4 PSScaled(V v):SV_TARGET {return art.Sample(sampleArt,uv_rect.xy+v.uv*uv_rect.zw)*fade;}
)";
        Ptr<ID3DBlob> vs,ps,ps_scaled,error;
        if(FAILED(D3DCompile(shader,sizeof(shader)-1,nullptr,nullptr,nullptr,"VS","vs_5_0",0,0,&vs,&error)) ||
           FAILED(D3DCompile(shader,sizeof(shader)-1,nullptr,nullptr,nullptr,"PS","ps_5_0",0,0,&ps,&error)) ||
           FAILED(D3DCompile(shader,sizeof(shader)-1,nullptr,nullptr,nullptr,"PSScaled","ps_5_0",0,0,&ps_scaled,&error)) ||
           FAILED(device->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&d->vs)) ||
           FAILED(device->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&d->ps)) ||
           FAILED(device->CreatePixelShader(ps_scaled->GetBufferPointer(),ps_scaled->GetBufferSize(),nullptr,&d->ps_scaled)))return {};
        D3D11_SAMPLER_DESC sampler{};sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
        sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sampler.MaxLOD=D3D11_FLOAT32_MAX;
        D3D11_SAMPLER_DESC linear=sampler;linear.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        D3D11_RASTERIZER_DESC raster{};raster.FillMode=D3D11_FILL_SOLID;raster.CullMode=D3D11_CULL_NONE;raster.DepthClipEnable=TRUE;
        D3D11_DEPTH_STENCIL_DESC depth{};depth.DepthFunc=D3D11_COMPARISON_ALWAYS;
        D3D11_BLEND_DESC blend{};auto& target=blend.RenderTarget[0];target.BlendEnable=TRUE;target.SrcBlend=D3D11_BLEND_ONE;
        target.DestBlend=D3D11_BLEND_INV_SRC_ALPHA;target.BlendOp=D3D11_BLEND_OP_ADD;target.SrcBlendAlpha=D3D11_BLEND_ONE;
        target.DestBlendAlpha=D3D11_BLEND_INV_SRC_ALPHA;target.BlendOpAlpha=D3D11_BLEND_OP_ADD;target.RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
        D3D11_BUFFER_DESC cb{};cb.ByteWidth=32;cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        if(FAILED(device->CreateSamplerState(&sampler,&d->sampler)) || FAILED(device->CreateSamplerState(&linear,&d->sampler_linear)) ||
           FAILED(device->CreateRasterizerState(&raster,&d->raster)) ||
           FAILED(device->CreateDepthStencilState(&depth,&d->depth)) || FAILED(device->CreateBlendState(&blend,&d->premultiplied)) ||
           FAILED(device->CreateBuffer(&cb,nullptr,&d->uv)))return {};
        target.SrcBlend=D3D11_BLEND_SRC_ALPHA;if(FAILED(device->CreateBlendState(&blend,&d->straight)))return {};return d;
    }
    std::shared_ptr<Device> device_for(ID3D11Device* device) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::shared_ptr<Device> d;
        for(const auto& entry:devices_)if(entry && entry->device.Get()==device){d=entry;break;}
        if(!d) {for(auto& entry:devices_)if(!entry){entry=create(device);d=entry;break;}if(!d)return {};}
        return d;
    }
    static Ptr<ID3D11ShaderResourceView> view_for(Device& d,ID3D11Texture2D* art,DXGI_FORMAT format) {
        for(const auto& entry:d.views)if(entry.texture.Get()==art && entry.format==format)return entry.srv;
        Ptr<ID3D11ShaderResourceView> srv;
        D3D11_SHADER_RESOURCE_VIEW_DESC desc{};desc.Format=format;desc.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;desc.Texture2D.MipLevels=1;
        if(FAILED(d.device->CreateShaderResourceView(art,&desc,&srv)))return {};
        auto& entry=d.views[d.next++%d.views.size()];entry.texture=art;entry.srv=srv;entry.format=format;
        return srv;
    }
public:
    bool blit(ID3D11DeviceContext* context,ID3D11RenderTargetView* original,ID3D11Texture2D* art,DXGI_FORMAT format,bool premultiplied=true) {
        if(!context || !original || !art || !rgba(format))return false;
        Ptr<ID3D11Device> device,target_device,art_device;context->GetDevice(&device);original->GetDevice(&target_device);art->GetDevice(&art_device);
        if(!device || device.Get()!=target_device.Get() || device.Get()!=art_device.Get())return false;
        Ptr<ID3D11Resource> target_resource;Ptr<ID3D11Texture2D> target;original->GetResource(&target_resource);
        if(FAILED(target_resource.As(&target)) || target.Get()==art)return false;
        D3D11_TEXTURE2D_DESC a{},t{};art->GetDesc(&a);target->GetDesc(&t);D3D11_RENDER_TARGET_VIEW_DESC view{};original->GetDesc(&view);
        if(!rgba(view.Format) || view.Format!=format || view.ViewDimension!=D3D11_RTV_DIMENSION_TEXTURE2D || view.Texture2D.MipSlice ||
           a.Width!=t.Width || a.Height!=t.Height || !a.Width || !a.Height || a.ArraySize!=1 || t.ArraySize!=1 ||
           a.SampleDesc.Count!=1 || t.SampleDesc.Count!=1 || !(a.BindFlags&D3D11_BIND_SHADER_RESOURCE) ||
           (a.Format!=DXGI_FORMAT_R8G8B8A8_TYPELESS && a.Format!=format))return false;
        std::shared_ptr<Device> d;Ptr<ID3D11ShaderResourceView> srv;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for(const auto& entry:devices_)if(entry && entry->device.Get()==device.Get()){d=entry;break;}
            if(!d) {for(auto& entry:devices_)if(!entry){entry=create(device.Get());d=entry;break;}if(!d)return false;}
            for(const auto& entry:d->views)if(entry.texture.Get()==art && entry.format==format){srv=entry.srv;break;}
            if(!srv) {
                D3D11_SHADER_RESOURCE_VIEW_DESC desc{};desc.Format=format;desc.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;desc.Texture2D.MipLevels=1;
                if(FAILED(device->CreateShaderResourceView(art,&desc,&srv)))return false;
                auto& entry=d->views[d->next++%d->views.size()];entry.texture=art;entry.srv=srv;entry.format=format;
            }
        }
        State saved(context);
        if(saved.targets[0]!=original)return false;
        for(unsigned i=1;i<saved.targets.size();++i)if(saved.targets[i])return false;
        saved.modified=true;
        context->SetPredication(nullptr,FALSE);
        context->IASetInputLayout(nullptr);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(d->vs.Get(),nullptr,0);context->PSSetShader(d->ps.Get(),nullptr,0);
        context->GSSetShader(nullptr,nullptr,0);context->HSSetShader(nullptr,nullptr,0);context->DSSetShader(nullptr,nullptr,0);
        auto* input=srv.Get();auto* sampler=d->sampler.Get();context->PSSetShaderResources(0,1,&input);context->PSSetSamplers(0,1,&sampler);
        context->RSSetState(d->raster.Get());const D3D11_VIEWPORT viewport{0,0,static_cast<float>(a.Width),static_cast<float>(a.Height),0,1};context->RSSetViewports(1,&viewport);
        context->OMSetDepthStencilState(d->depth.Get(),0);context->OMSetBlendState(premultiplied?d->premultiplied.Get():d->straight.Get(),nullptr,0xffffffff);
        context->OMSetRenderTargets(1,&original,nullptr);context->Draw(3,0);return true;
    }
    // Alpha-blend a sub-rectangle of a captured texture into a sub-rectangle of the sole bound target, with a constant
    // fade. Used by the wrist watch to draw its generated face and the game's own health/mana/equipment art. A
    // premultiplied source is faded by multiplying rgb and a; a straight-alpha source fades its alpha only, so the
    // existing straight blend state applies the fade exactly once. The caller binds the destination render target
    // first; every pipeline stage, the PS constant-buffer slot and the viewport are restored on exit.
    bool blit_scaled(ID3D11DeviceContext* context,ID3D11RenderTargetView* original,ID3D11Texture2D* art,
        const D3D11_BOX& source,const D3D11_RECT& destination,DXGI_FORMAT format,bool premultiplied=true,float alpha=1.f) {
        if(!context || !original || !art || !rgba(format))return false;
        if(!std::isfinite(alpha))return false;
        alpha=std::clamp(alpha,0.f,1.f);
        Ptr<ID3D11Device> device,target_device,art_device;context->GetDevice(&device);original->GetDevice(&target_device);art->GetDevice(&art_device);
        if(!device || device.Get()!=target_device.Get() || device.Get()!=art_device.Get())return false;
        Ptr<ID3D11Resource> target_resource;Ptr<ID3D11Texture2D> target;original->GetResource(&target_resource);
        if(FAILED(target_resource.As(&target)) || target.Get()==art)return false;
        D3D11_TEXTURE2D_DESC a{},t{};art->GetDesc(&a);target->GetDesc(&t);D3D11_RENDER_TARGET_VIEW_DESC view{};original->GetDesc(&view);
        if(!rgba(view.Format) || view.Format!=format || view.ViewDimension!=D3D11_RTV_DIMENSION_TEXTURE2D || view.Texture2D.MipSlice ||
           !a.Width || !a.Height || a.ArraySize!=1 || t.ArraySize!=1 || a.SampleDesc.Count!=1 || t.SampleDesc.Count!=1 ||
           !(a.BindFlags&D3D11_BIND_SHADER_RESOURCE) || (a.Format!=DXGI_FORMAT_R8G8B8A8_TYPELESS && a.Format!=format) ||
           source.left>=source.right || source.top>=source.bottom || source.right>a.Width || source.bottom>a.Height ||
           destination.left>=destination.right || destination.top>=destination.bottom ||
           destination.left<0 || destination.top<0 || static_cast<UINT>(destination.right)>t.Width ||
           static_cast<UINT>(destination.bottom)>t.Height)return false;
        auto d=device_for(device.Get());if(!d)return false;
        auto srv=view_for(*d,art,format);if(!srv)return false;
        State saved(context);
        if(saved.targets[0]!=original)return false;
        for(unsigned i=1;i<saved.targets.size();++i)if(saved.targets[i])return false;
        saved.modified=true;
        const float uv[8]={static_cast<float>(source.left)/a.Width,static_cast<float>(source.top)/a.Height,
            static_cast<float>(source.right-source.left)/a.Width,static_cast<float>(source.bottom-source.top)/a.Height,
            premultiplied?alpha:1.f,premultiplied?alpha:1.f,premultiplied?alpha:1.f,alpha};
        context->UpdateSubresource(d->uv.Get(),0,nullptr,uv,0,0);
        context->SetPredication(nullptr,FALSE);
        context->IASetInputLayout(nullptr);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(d->vs.Get(),nullptr,0);context->PSSetShader(d->ps_scaled.Get(),nullptr,0);
        context->GSSetShader(nullptr,nullptr,0);context->HSSetShader(nullptr,nullptr,0);context->DSSetShader(nullptr,nullptr,0);
        auto* input=srv.Get();auto* sampler=d->sampler_linear.Get();auto* constant=d->uv.Get();
        context->PSSetShaderResources(0,1,&input);context->PSSetSamplers(0,1,&sampler);context->PSSetConstantBuffers(0,1,&constant);
        context->RSSetState(d->raster.Get());
        const D3D11_VIEWPORT viewport{static_cast<float>(destination.left),static_cast<float>(destination.top),
            static_cast<float>(destination.right-destination.left),static_cast<float>(destination.bottom-destination.top),0,1};
        context->RSSetViewports(1,&viewport);
        context->OMSetDepthStencilState(d->depth.Get(),0);context->OMSetBlendState(premultiplied?d->premultiplied.Get():d->straight.Get(),nullptr,0xffffffff);
        context->OMSetRenderTargets(1,&original,nullptr);context->Draw(3,0);return true;
    }
    void clear() {std::lock_guard<std::mutex> lock(mutex_);for(auto& entry:devices_)entry.reset();}
};
}
