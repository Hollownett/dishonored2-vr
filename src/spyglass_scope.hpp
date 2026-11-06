#pragma once
// The R3 spyglass as a right-eye scope (v1040; Corvo's mask carries its telescope over the right eye). While the spyglass zoom runs the
// RIGHT eye's view is rendered by the engine with its projection narrowed by the game's own zoom factor (real detail, no crop of a
// full-FOV image), centred on that view's axis, and the finished image is shown only inside a round scope with a soft dark rim (black
// outside). The left eye is untouched. The narrowed render maps one to one onto the scope: the output at tangent t shows the scene
// direction t/m, which the narrowed image holds at the very texel the unzoomed image holds direction t, so the mask needs no resample.
// Pure parts (narrowing, scope size, rim, axis, entry/exit state) are checked by tests/spyglass_scope_check.cpp.
#include "context_state.hpp"
#include "spyglass_lens_art.hpp"
#include <openxr/openxr.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

namespace d2vr {
namespace spyglass_scope {
constexpr double pi=3.14159265358979323846;
constexpr double on_above=1.02,off_below=1.01,max_zoom=16.;   // hysteresis around "no zoom"
// A frustum (angles from the axis, radians) seen m times closer: every edge's tangent divides by m (about the view axis).
inline std::array<float,4> narrowed(float left,float right,float up,float down,double m) noexcept {
    if(!(m>1.0)) return {left,right,up,down};
    const auto n=[m](float a) {return static_cast<float>(std::atan(std::tan(static_cast<double>(a))/std::min(m,max_zoom)));};
    return {n(left),n(right),n(up),n(down)};
}
// Scope radius as a tangent from the eye's own runtime FOV: a diameter of half the smaller span, 40-56 deg across.
inline double radius_tan(double left,double right,double up,double down) noexcept {
    const double span=std::min(right-left,up-down);
    if(!(span>.1 && span<3.1)) return std::tan(24*pi/180);
    return std::tan(std::clamp(span*.25,20*pi/180,28*pi/180));
}
// Darkness (alpha of black) at q = distance from the centre / scope radius: clear inside .8, a soft dark rim to .9 at the edge,
// then black by 1.06 R.
inline float coverage(double q) noexcept {
    const auto smooth=[](double a,double b,double x) {const double t=std::clamp((x-a)/(b-a),0.,1.);return t*t*(3-2*t);};
    return static_cast<float>(q<1?.9*smooth(.8,1.,q):.9+.1*smooth(1.,1.06,q));
}
// Where a view's axis falls in the eye's straight frame when the view is turned by `q` (x,y,z,w; OpenXR, -z forward): the tangents (x right, y up).
inline std::array<float,2> axis_tangent(const std::array<float,4>& q) noexcept {
    const double x=q[0],y=q[1],z=q[2],w=q[3];
    const double cx=2*(x*z+w*y),cy=2*(y*z-w*x),cz=1-2*(x*x+y*y);
    if(!(cz>1e-6)) return {0.f,0.f};
    return {static_cast<float>(-cx/cz),static_cast<float>(-cy/cz)};
}
struct Step {double m{1};bool active{},reset{};};
// Entry/exit of the scope from the zoom factor the engine's fades and levels give (once per displayed frame). `reset` asks for the
// temporal histories to be dropped: at entry, at exit, and (opt-in) on every frame the factor moves in a fade.
class State {
    bool active_{};double last_{1};
public:
    Step update(double raw,bool reset_in_fades) noexcept {
        const bool want=std::isfinite(raw) && raw>(active_?off_below:on_above);
        Step s;s.active=want;s.m=want?std::clamp(raw,1.,max_zoom):1.;s.reset=want!=active_;
        if(reset_in_fades && want && active_ && std::abs(s.m-last_)>.03*last_) s.reset=true;
        active_=want;last_=s.m;return s;
    }
    bool active() const noexcept {return active_;}
};
} // namespace spyglass_scope

// Darkens an eye's swapchain image outside the scope (black, soft rim): one alpha-blended full-screen triangle, no source read.
// v1041: with the game's lens art (spyglass_lens_art.hpp) the outside of the art's square is black and a second triangle blends the
// game's own mask texture over the square (the draw's own blend state); without it, or when the art cannot be used, the procedural rim.
class SpyglassScopeMask {
    template<class T>using Ptr=Microsoft::WRL::ComPtr<T>;
    Ptr<ID3D11Device> device_;Ptr<ID3D11DeviceContext> context_;ContextState state_;
    Ptr<ID3D11VertexShader> vs_;Ptr<ID3D11PixelShader> ps_;Ptr<ID3D11Buffer> constants_;
    Ptr<ID3D11RasterizerState> raster_;Ptr<ID3D11PixelShader> art_ps_;Ptr<ID3D11SamplerState> sampler_;
    const LensArt* art_key_{};std::shared_ptr<LensArt> art_hold_;Ptr<ID3D11ShaderResourceView> art_srv_;Ptr<ID3D11BlendState> art_blend_;bool art_ok_{};Ptr<ID3D11DepthStencilState> depth_;Ptr<ID3D11BlendState> blend_;
    static void checked(HRESULT value,const char* what) {if(FAILED(value)) throw std::runtime_error(std::string("Spyglass scope: ")+what);}
public:
    explicit SpyglassScopeMask(ID3D11Device* device):device_(device),state_(device) {
        device_->GetImmediateContext(&context_);
        // b0: the layer FOV's tangents (left, right, up, down); the scope (centre x, centre y, radius, 0); the image size (w, h, 0, 0);
        // the art (texture aspect, 1 = art on, 0, 0). The PS is spyglass_scope::coverage; with art, spyglass_art::texture_uv.
        constexpr char source[]=R"HLSL(
cbuffer Scope:register(b0){float4 fov;float4 scope;float4 size;float4 art;}
Texture2D<float4> mask:register(t0);SamplerState smp:register(s0);
struct V{float4 pos:SV_POSITION;};
V VS(uint id:SV_VertexID){V v;float2 c=float2((id<<1)&2,id&2);v.pos=float4(c*2-1,0,1);v.pos.y=-v.pos.y;return v;}
float2 Tex(V v){float2 uv=v.pos.xy/size.xy;float2 t=float2(lerp(fov.x,fov.y,uv.x),lerp(fov.z,fov.w,uv.y));
    float2 s=(t-scope.xy)/scope.z;return float2(.5+s.x/(2*art.x),.5-s.y*.5);}
float4 PS(V v):SV_TARGET{
    float2 uv=v.pos.xy/size.xy;float2 t=float2(lerp(fov.x,fov.y,uv.x),lerp(fov.z,fov.w,uv.y));
    float q=length(t-scope.xy)/scope.z;
    float a=q<1?.9*smoothstep(.8,1,q):.9+.1*smoothstep(1,1.06,q);
    if(art.y>.5){float2 u=Tex(v);a=(u.x>=0&&u.x<=1&&u.y>=0&&u.y<=1)?0:1;}
    return float4(0,0,0,a);}
float4 PSArt(V v):SV_TARGET{
    float2 u=Tex(v);float4 c=mask.Sample(smp,saturate(u));
    if(art.z>.5) c=float4(0,0,0,c.r); // a one-channel mask (the game's BC4 lens: 0 = the opening, 1 = masked) is black with that coverage
    clip(float4(u.x,1-u.x,u.y,1-u.y));return c;}
)HLSL";
        Ptr<ID3DBlob> vs,ps,errors;
        checked(D3DCompile(source,sizeof(source)-1,"D2VR spyglass scope",nullptr,nullptr,"VS","vs_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&vs,&errors),"vertex shader compile");
        checked(D3DCompile(source,sizeof(source)-1,"D2VR spyglass scope",nullptr,nullptr,"PS","ps_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&ps,&errors),"pixel shader compile");
        Ptr<ID3DBlob> art_ps;checked(D3DCompile(source,sizeof(source)-1,"D2VR spyglass scope",nullptr,nullptr,"PSArt","ps_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&art_ps,&errors),"art pixel shader compile");
        checked(device_->CreatePixelShader(art_ps->GetBufferPointer(),art_ps->GetBufferSize(),nullptr,&art_ps_),"art pixel shader");
        D3D11_SAMPLER_DESC sd{};sd.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sd.MaxLOD=D3D11_FLOAT32_MAX;sd.ComparisonFunc=D3D11_COMPARISON_NEVER;
        checked(device_->CreateSamplerState(&sd,&sampler_),"sampler");
        checked(device_->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&vs_),"vertex shader");
        checked(device_->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&ps_),"pixel shader");
        D3D11_BUFFER_DESC b{};b.ByteWidth=64;b.BindFlags=D3D11_BIND_CONSTANT_BUFFER;b.Usage=D3D11_USAGE_DEFAULT;checked(device_->CreateBuffer(&b,nullptr,&constants_),"constants");
        D3D11_RASTERIZER_DESC r{};r.FillMode=D3D11_FILL_SOLID;r.CullMode=D3D11_CULL_NONE;r.DepthClipEnable=TRUE;checked(device_->CreateRasterizerState(&r,&raster_),"rasterizer");
        D3D11_DEPTH_STENCIL_DESC d{};d.DepthFunc=D3D11_COMPARISON_ALWAYS;checked(device_->CreateDepthStencilState(&d,&depth_),"depth state");
        D3D11_BLEND_DESC blend{};auto& t=blend.RenderTarget[0];t.BlendEnable=TRUE;t.SrcBlend=D3D11_BLEND_ONE;t.DestBlend=D3D11_BLEND_INV_SRC_ALPHA;t.BlendOp=D3D11_BLEND_OP_ADD;
        t.SrcBlendAlpha=D3D11_BLEND_ZERO;t.DestBlendAlpha=D3D11_BLEND_ONE;t.BlendOpAlpha=D3D11_BLEND_OP_ADD;t.RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
        checked(device_->CreateBlendState(&blend,&blend_),"blend state");
    }
    // The art's SRV and blend state, made once per art (a fault leaves the procedural rim for that art). Returns whether the art is usable.
    bool prepare_art_(const std::shared_ptr<LensArt>& art) {
        if(art.get()==art_key_) return art_ok_;
        art_key_=art.get();art_hold_=art;art_ok_=false;art_srv_.Reset();art_blend_.Reset();
        try {
            const auto bad=[](D3D11_BLEND b) {return b==D3D11_BLEND_BLEND_FACTOR || b==D3D11_BLEND_INV_BLEND_FACTOR || b==D3D11_BLEND_SRC1_COLOR || b==D3D11_BLEND_INV_SRC1_COLOR || b==D3D11_BLEND_SRC1_ALPHA || b==D3D11_BLEND_INV_SRC1_ALPHA;};
            if(!art->texture || bad(art->eff_src()) || bad(art->eff_dst())) return false;
            D3D11_BLEND_DESC blend{};auto& t=blend.RenderTarget[0];t.BlendEnable=TRUE;t.SrcBlend=art->eff_src();t.DestBlend=art->eff_dst();t.BlendOp=art->eff_op();
            t.SrcBlendAlpha=D3D11_BLEND_ZERO;t.DestBlendAlpha=D3D11_BLEND_ONE;t.BlendOpAlpha=D3D11_BLEND_OP_ADD;t.RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_RED|D3D11_COLOR_WRITE_ENABLE_GREEN|D3D11_COLOR_WRITE_ENABLE_BLUE;
            checked(device_->CreateBlendState(&blend,&art_blend_),"art blend state");
            checked(device_->CreateShaderResourceView(art->texture.Get(),&art->view,&art_srv_),"art view");
            art_ok_=true;
            if(art->dump_pending.exchange(false)) dump_(*art);
        } catch(const std::exception&) {art_srv_.Reset();art_blend_.Reset();art_ok_=false;}
        return art_ok_;
    }
    // One-shot, on the first use of the art: mip 0 of the game's lens texture as it sits in memory (row pitch and rows in the json).
    void dump_(const LensArt& art) {
        if(art.dump_stem.empty()) return;
        const auto& d=art.desc;const bool bc=(d.Format>=DXGI_FORMAT_BC1_TYPELESS && d.Format<=DXGI_FORMAT_BC5_SNORM) || (d.Format>=DXGI_FORMAT_BC6H_TYPELESS && d.Format<=DXGI_FORMAT_BC7_UNORM_SRGB);
        D3D11_TEXTURE2D_DESC sd{};sd.Width=d.Width;sd.Height=d.Height;sd.MipLevels=sd.ArraySize=1;sd.Format=d.Format;sd.SampleDesc.Count=1;sd.Usage=D3D11_USAGE_STAGING;sd.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        Ptr<ID3D11Texture2D> staging;checked(device_->CreateTexture2D(&sd,nullptr,&staging),"dump staging");
        D3D11_BOX box{0,0,0,d.Width,d.Height,1};context_->CopySubresourceRegion(staging.Get(),0,0,0,0,art.texture.Get(),0,&box);
        D3D11_MAPPED_SUBRESOURCE map{};checked(context_->Map(staging.Get(),0,D3D11_MAP_READ,0,&map),"dump map");
        const UINT rows=bc?(d.Height+3)/4:d.Height;const bool rgba=d.Format>=DXGI_FORMAT_R8G8B8A8_TYPELESS && d.Format<=DXGI_FORMAT_R8G8B8A8_SINT;
        auto raw=art.dump_stem;raw+=rgba?L".rgba":L".raw";
        std::ofstream(raw,std::ios::binary|std::ios::trunc).write(static_cast<const char*>(map.pData),static_cast<std::streamsize>(map.RowPitch)*rows);
        context_->Unmap(staging.Get(),0);
        auto json=art.dump_stem;json+=L".json";
        std::ofstream(json,std::ios::trunc)<<"{\"width\":"<<d.Width<<",\"height\":"<<d.Height<<",\"format\":"<<d.Format<<",\"view_format\":"<<art.view.Format<<",\"mips\":"<<d.MipLevels
            <<",\"row_pitch\":"<<map.RowPitch<<",\"rows\":"<<rows<<",\"compressed\":"<<(bc?"true":"false")<<",\"slot\":"<<art.slot<<",\"blend_enabled\":"<<(art.blend_enabled?"true":"false")
            <<",\"src\":"<<art.src<<",\"dst\":"<<art.dst<<",\"op\":"<<art.op<<'}';
    }
    // `eye`: the swapchain image (the scope's eye, already copied); `fov`: its layer's FOV; `axis`: the scope centre (tangents);
    // `art`: the game's lens mask when it was found (else the procedural rim).
    void draw(ID3D11Texture2D* eye,const XrFovf& fov,const std::array<float,2>& axis,const std::shared_ptr<LensArt>& art={}) {
        if(!eye) throw std::invalid_argument("Spyglass scope: arguments");
        D3D11_TEXTURE2D_DESC e{};eye->GetDesc(&e);
        if(e.SampleDesc.Count!=1) throw std::invalid_argument("Spyglass scope: geometry");
        ContextState::Scope scope(state_);
        const bool use_art=art && prepare_art_(art);
        const DXGI_FORMAT write=e.Format==DXGI_FORMAT_R8G8B8A8_TYPELESS?DXGI_FORMAT_R8G8B8A8_UNORM:e.Format;   // as the lens: typeless is written byte for byte
        D3D11_RENDER_TARGET_VIEW_DESC rd{};rd.Format=write;rd.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
        Ptr<ID3D11RenderTargetView> output;checked(device_->CreateRenderTargetView(eye,&rd,&output),"eye view");
        const std::array<float,16> data{std::tan(fov.angleLeft),std::tan(fov.angleRight),std::tan(fov.angleUp),std::tan(fov.angleDown),
            axis[0],axis[1],static_cast<float>(spyglass_scope::radius_tan(fov.angleLeft,fov.angleRight,fov.angleUp,fov.angleDown)),0,
            static_cast<float>(e.Width),static_cast<float>(e.Height),0,0,
            use_art?static_cast<float>(art->desc.Width)/static_cast<float>(art->desc.Height):1.f,use_art?1.f:0.f,
            use_art && spyglass_art::one_channel(art->view.Format!=DXGI_FORMAT_UNKNOWN?art->view.Format:art->desc.Format)?1.f:0.f,0};
        context_->UpdateSubresource(constants_.Get(),0,nullptr,data.data(),0,0);
        D3D11_VIEWPORT viewport{0,0,static_cast<float>(e.Width),static_cast<float>(e.Height),0,1};context_->RSSetViewports(1,&viewport);context_->RSSetState(raster_.Get());
        context_->IASetInputLayout(nullptr);context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context_->VSSetShader(vs_.Get(),nullptr,0);context_->PSSetShader(ps_.Get(),nullptr,0);
        context_->GSSetShader(nullptr,nullptr,0);context_->HSSetShader(nullptr,nullptr,0);context_->DSSetShader(nullptr,nullptr,0);
        context_->OMSetDepthStencilState(depth_.Get(),0);context_->OMSetBlendState(blend_.Get(),nullptr,0xffffffff);
        auto* constant=constants_.Get();context_->VSSetConstantBuffers(0,1,&constant);context_->PSSetConstantBuffers(0,1,&constant);
        auto* rtv=output.Get();context_->OMSetRenderTargets(1,&rtv,nullptr);context_->Draw(3,0);
        if(use_art) { // the game's art over its square, blended as the game's own draw blended it
            auto* srv=art_srv_.Get();auto* smp=sampler_.Get();
            context_->PSSetShader(art_ps_.Get(),nullptr,0);context_->PSSetShaderResources(0,1,&srv);context_->PSSetSamplers(0,1,&smp);
            context_->OMSetBlendState(art_blend_.Get(),nullptr,0xffffffff);context_->Draw(3,0);
            ID3D11ShaderResourceView* none{};context_->PSSetShaderResources(0,1,&none);
        }
        context_->OMSetRenderTargets(0,nullptr,nullptr);
    }
};
} // namespace d2vr
