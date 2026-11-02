#pragma once
// The spyglass zoom (R3) as a VR lens. The game zooms by narrowing its camera (fx ply_*_spyglass_01/02: m_fov 0.4375 / 0.1875,
// percentage mode, 0.2 s fades) and draws flat-screen post effects (fx ply_spyglass_pp_01: a lens-mask quad after tonemap,
// the fsdisto "use_lens" distortion and a radial blur). The eye views are forced to the runtime FOV, so in VR only those post
// effects reached the eyes: both eyes bulged and blurred a little (user, 2026-11-02). Here the post effects are taken out of the
// view while the spyglass state is on, and each eye gets a round lens around its forward axis that magnifies its own image by the
// game's own zoom (its narrowed FOV against its usual one). ABI notes: side-session-notes spyglass-zoom-abi.md.
#include "context_state.hpp"
#include <openxr/openxr.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

namespace d2vr {
namespace spyglass {
constexpr double pi=3.14159265358979323846;
constexpr unsigned state_spyglass=76,state_zoom=77;   // PLAYER_STATE_SPYGLASS / _ZOOM (enum table RVA 0x2391a90)
constexpr double rim_outer=1.12;                      // the lens' soft shadow ends at 1.12 lens radii
// The lens radius as the tangent of its angle, from the eye's own runtime FOV: 0.21 of the smaller span, 35-45 deg across.
inline double lens_tan(double left,double right,double up,double down) noexcept {
    const double span=std::min(right-left,up-down);
    if(!(span>.1 && span<3.1)) return std::tan(20*pi/180);
    return std::tan(std::clamp(span*.21,17.5*pi/180,22.5*pi/180));
}
// The game's own zoom: its narrowed camera against its usual one (rectilinear, so a ratio of half-angle tangents).
inline double magnification(double base_fov_x,double zoom_fov_x) noexcept {
    if(!(base_fov_x>.2 && base_fov_x<3. && zoom_fov_x>.02 && zoom_fov_x<3.)) return 1;
    return std::clamp(std::tan(base_fov_x*.5)/std::tan(zoom_fov_x*.5),1.,16.);
}
// What the lens shows at an image point given as tangents of the eye's forward axis (x right, y up): the point it samples
// (t/m inside the lens), the brightness there (the inner rim darkens) and the coverage (1 inside, a fading shadow ring outside).
// The HLSL in Lens::PS is this function.
struct Sample {double x{},y{};float shade{},alpha{};};
inline Sample sample(double tx,double ty,double radius_tan,double m) noexcept {
    const double q=std::hypot(tx,ty)/radius_tan;
    const auto smooth=[](double a,double b,double x) {const double t=std::clamp((x-a)/(b-a),0.,1.);return t*t*(3-2*t);};
    if(q<=1) return {tx/m,ty/m,static_cast<float>(1-.8*smooth(.86,1.,q)),1.f};
    return {tx,ty,0.f,static_cast<float>(.85*(1-smooth(1.,rim_outer,q)))};
}
} // namespace spyglass

// Follows the engine's camera FOV through a zoom. Base = its FOV while no spyglass state was seen for 0.5 s; meanwhile the
// magnification follows the engine's own fades and levels (the state ends before the 0.2 s fade-out does).
class SpyglassZoom {
    double base_{};std::uint64_t last_{};bool seen_{},idle_=true;
public:
    double update(bool spyglass,double fov_x,std::uint64_t now_ms) noexcept {
        if(spyglass) {last_=now_ms;seen_=true;}
        idle_=!seen_ || now_ms-last_>=500;
        if(idle_) {if(fov_x>.2 && fov_x<3.) base_=fov_x;return 1;}
        return base_>0?spyglass::magnification(base_,fov_x):1;
    }
    double base() const noexcept {return base_;}
    bool idle() const noexcept {return idle_;}   // no zoom and none fading: the view holds only what it holds without one
};

// Takes the spyglass' flat-screen post effects out of a view's renderView_t before Setup copies it to the render side, and
// restore() puts the bytes back after Setup (the game's view stays its own). learn() reads a view while no zoom runs; during a
// zoom apply() drops what the zoom added since: the fsdisto parms of m_UberMVGenParmBlock (lens distortion, radial blur and its
// dead zone) and the full-screen quads (the lens mask). Read/Write: bool(uintptr_t, void* / const void*, size_t).
class SpyglassViewSuppress {
public:
    static constexpr std::size_t uber_block=0x3c0,uber_capacity=64,uber_size=0x20+uber_capacity*18; // idStaticParmBlock<64>, read by the fsdisto pass
    static constexpr std::size_t quad_list=0x8b0,quad_size=0x2e0,quad_stages=5,quad_stage_size=0x90,quad_capacity=4,radial_blur=0xb91;
private:
    using Uber=std::array<std::uint8_t,uber_size>;using Quads=std::array<std::uint8_t,quad_size>;
    std::array<std::uint16_t,uber_capacity> base_ids_{};std::uint32_t base_count_{};bool base_uber_{};
    std::array<std::array<std::uint64_t,quad_capacity>,quad_stages> base_quads_{};std::array<std::uint32_t,quad_stages> base_quad_count_{};bool base_quad_{};
    Uber saved_uber_{};Quads saved_quads_{};std::uint8_t saved_radial_{};std::uintptr_t view_{};bool held_uber_{},held_quads_{},held_radial_{};
    template<class T> static T get(const std::uint8_t* p) {T v;std::memcpy(&v,p,sizeof v);return v;}
    // A static block keeps its values (16 B) and ids (2 B) in its own storage: header {values, ?, ids, count|flags}.
    static bool uber_ok(const Uber& b,std::uintptr_t at) {
        return get<std::uintptr_t>(b.data())==at+0x20 && get<std::uintptr_t>(b.data()+0x10)==at+0x20+uber_capacity*16 && (get<std::uint32_t>(b.data()+0x18)&0xffffffu)<=uber_capacity;
    }
    // idList {ptr (own storage), num, flags} of 5 stages, each an idList of up to 4 x 0x20 quads (first field: the material).
    static bool stage_ok(const Quads& q,std::uintptr_t at,std::size_t k) {
        const auto s=0x10+k*quad_stage_size;   // num carries flags in its top byte (0x05000001), as the parm block's count does
        return get<std::uintptr_t>(q.data()+s)==at+s+0x10 && (get<std::uint32_t>(q.data()+s+8)&0xffffffu)<=quad_capacity;
    }
public:
    unsigned parms{},quads{},blur{}; // what the last apply() took out
    template<class Read> void learn(Read& read,std::uintptr_t view) {
        Uber u{};base_uber_=read(view+uber_block,u.data(),u.size()) && uber_ok(u,view+uber_block);
        if(base_uber_) {base_count_=get<std::uint32_t>(u.data()+0x18)&0xffffffu;for(std::uint32_t i=0;i<base_count_;++i) base_ids_[i]=get<std::uint16_t>(u.data()+0x420+i*2);}
        Quads q{};base_quad_=read(view+quad_list,q.data(),q.size());
        for(std::size_t k=0;k<quad_stages && base_quad_;++k) {
            if(!stage_ok(q,view+quad_list,k)) {base_quad_=false;break;}
            base_quad_count_[k]=get<std::uint32_t>(q.data()+0x10+k*quad_stage_size+8)&0xffffffu;
            for(std::uint32_t i=0;i<base_quad_count_[k];++i) base_quads_[k][i]=get<std::uint64_t>(q.data()+0x20+k*quad_stage_size+i*0x20);
        }
    }
    template<class Read,class Write> void apply(Read& read,Write& write,std::uintptr_t view) {
        parms=quads=blur=0;view_=view;
        if(read(view+radial_blur,&saved_radial_,1) && saved_radial_==1) {const std::uint8_t off{};held_radial_=write(view+radial_blur,&off,1);blur=held_radial_;}
        if(base_uber_ && read(view+uber_block,saved_uber_.data(),uber_size) && uber_ok(saved_uber_,view+uber_block)) {
            auto out=saved_uber_;const auto n=get<std::uint32_t>(out.data()+0x18);std::uint32_t kept{};
            for(std::uint32_t i=0;i<(n&0xffffffu);++i) {
                const auto id=get<std::uint16_t>(saved_uber_.data()+0x420+i*2);
                if(std::find(base_ids_.begin(),base_ids_.begin()+base_count_,id)==base_ids_.begin()+base_count_) {++parms;continue;}
                std::memcpy(out.data()+0x20+kept*16,saved_uber_.data()+0x20+i*16,16);std::memcpy(out.data()+0x420+kept*2,&id,2);++kept;
            }
            const std::uint32_t count=(n&0xff000000u)|kept;std::memcpy(out.data()+0x18,&count,4);
            if(parms) held_uber_=write(view+uber_block,out.data(),uber_size);
        }
        if(base_quad_ && read(view+quad_list,saved_quads_.data(),quad_size)) {
            auto out=saved_quads_;
            for(std::size_t k=0;k<quad_stages;++k) {
                if(!stage_ok(saved_quads_,view+quad_list,k)) {quads=0;break;}
                const auto s=0x10+k*quad_stage_size;const auto n=get<std::uint32_t>(saved_quads_.data()+s+8);std::uint32_t kept{};
                for(std::uint32_t i=0;i<(n&0xffffffu);++i) {
                    const auto material=get<std::uint64_t>(saved_quads_.data()+s+0x10+i*0x20);
                    if(std::find(base_quads_[k].begin(),base_quads_[k].begin()+base_quad_count_[k],material)==base_quads_[k].begin()+base_quad_count_[k]) {++quads;continue;}
                    std::memmove(out.data()+s+0x10+kept*0x20,saved_quads_.data()+s+0x10+i*0x20,0x20);++kept;
                }
                const std::uint32_t count=(n&0xff000000u)|kept;std::memcpy(out.data()+s+8,&count,4);
            }
            if(quads) held_quads_=write(view+quad_list,out.data(),quad_size);
        }
    }
    template<class Write> void restore(Write& write) {
        if(held_uber_) write(view_+uber_block,saved_uber_.data(),uber_size);
        if(held_quads_) write(view_+quad_list,saved_quads_.data(),quad_size);
        if(held_radial_) write(view_+radial_blur,&saved_radial_,1);
        held_uber_=held_quads_=held_radial_=false;
    }
    bool holding() const noexcept {return held_uber_ || held_quads_ || held_radial_;}
};

// Draws the lens into an eye's swapchain image from the image it was copied from (same size and view).
class SpyglassLens {
    template<class T>using Ptr=Microsoft::WRL::ComPtr<T>;
    Ptr<ID3D11Device> device_;Ptr<ID3D11DeviceContext> context_;ContextState state_;
    Ptr<ID3D11VertexShader> vs_;Ptr<ID3D11PixelShader> ps_;Ptr<ID3D11Buffer> constants_;
    Ptr<ID3D11SamplerState> sampler_;Ptr<ID3D11RasterizerState> raster_;Ptr<ID3D11DepthStencilState> depth_;Ptr<ID3D11BlendState> blend_;
    static void checked(HRESULT value,const char* what) {if(FAILED(value)) throw std::runtime_error(std::string("Spyglass lens: ")+what);}
public:
    explicit SpyglassLens(ID3D11Device* device):device_(device),state_(device) {
        device_->GetImmediateContext(&context_);
        // b0: the eye's FOV tangents (left, right, up, down), the lens (radius tangent, magnification, shadow end) and the
        // source size (1/w, 1/h, w, h). The quad covers the lens and its shadow; the PS is spyglass::sample with a
        // Catmull-Rom read (9 bilinear taps), as a magnified bilinear read is soft.
        constexpr char source[]=R"HLSL(
Texture2D eye:register(t0);SamplerState linearClamp:register(s0);
cbuffer Lens:register(b0){float4 fov;float4 lens;float4 texel;}
struct V{float4 pos:SV_POSITION;float2 t:TEXCOORD0;};
V VS(uint id:SV_VertexID){float2 c=float2(id&1,(id>>1)&1)*2-1;V v;v.t=c*lens.x*lens.z;
    v.pos=float4((2*v.t.x-(fov.y+fov.x))/(fov.y-fov.x),(2*v.t.y-(fov.z+fov.w))/(fov.z-fov.w),0,1);return v;}
float2 uv_of(float2 t){return float2((t.x-fov.x)/(fov.y-fov.x),(fov.z-t.y)/(fov.z-fov.w));}
float3 catmull_rom(float2 uv){
    float2 p=uv*texel.zw-.5,f=frac(p),s=floor(p)+.5;
    float2 w0=f*(-.5+f*(1-.5*f)),w1=1+f*f*(-2.5+1.5*f),w2=f*(.5+f*(2-1.5*f)),w3=f*f*(-.5+.5*f);
    float2 w12=w1+w2,o12=w2/w12,t0=(s-1)*texel.xy,t3=(s+2)*texel.xy,t12=(s+o12)*texel.xy;
    float3 c=0;
    c+=eye.SampleLevel(linearClamp,float2(t0.x,t0.y),0).rgb*w0.x*w0.y;c+=eye.SampleLevel(linearClamp,float2(t12.x,t0.y),0).rgb*w12.x*w0.y;c+=eye.SampleLevel(linearClamp,float2(t3.x,t0.y),0).rgb*w3.x*w0.y;
    c+=eye.SampleLevel(linearClamp,float2(t0.x,t12.y),0).rgb*w0.x*w12.y;c+=eye.SampleLevel(linearClamp,float2(t12.x,t12.y),0).rgb*w12.x*w12.y;c+=eye.SampleLevel(linearClamp,float2(t3.x,t12.y),0).rgb*w3.x*w12.y;
    c+=eye.SampleLevel(linearClamp,float2(t0.x,t3.y),0).rgb*w0.x*w3.y;c+=eye.SampleLevel(linearClamp,float2(t12.x,t3.y),0).rgb*w12.x*w3.y;c+=eye.SampleLevel(linearClamp,float2(t3.x,t3.y),0).rgb*w3.x*w3.y;
    return max(c,0);}
float4 PS(V v):SV_TARGET{const float q=length(v.t)/lens.x;
    if(q>1)return float4(0,0,0,.85*(1-smoothstep(1,lens.z,q)));
    return float4(catmull_rom(uv_of(v.t/lens.y))*(1-.8*smoothstep(.86,1,q)),1);}
)HLSL";
        Ptr<ID3DBlob> vs,ps,errors;
        checked(D3DCompile(source,sizeof(source)-1,"D2VR spyglass lens",nullptr,nullptr,"VS","vs_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&vs,&errors),"vertex shader compile");
        checked(D3DCompile(source,sizeof(source)-1,"D2VR spyglass lens",nullptr,nullptr,"PS","ps_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&ps,&errors),"pixel shader compile");
        checked(device_->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&vs_),"vertex shader");
        checked(device_->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&ps_),"pixel shader");
        D3D11_BUFFER_DESC b{};b.ByteWidth=48;b.BindFlags=D3D11_BIND_CONSTANT_BUFFER;b.Usage=D3D11_USAGE_DEFAULT;checked(device_->CreateBuffer(&b,nullptr,&constants_),"constants");
        D3D11_SAMPLER_DESC s{};s.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;s.AddressU=s.AddressV=s.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;s.MaxLOD=D3D11_FLOAT32_MAX;checked(device_->CreateSamplerState(&s,&sampler_),"sampler");
        D3D11_RASTERIZER_DESC r{};r.FillMode=D3D11_FILL_SOLID;r.CullMode=D3D11_CULL_NONE;r.DepthClipEnable=TRUE;checked(device_->CreateRasterizerState(&r,&raster_),"rasterizer");
        D3D11_DEPTH_STENCIL_DESC d{};d.DepthFunc=D3D11_COMPARISON_ALWAYS;checked(device_->CreateDepthStencilState(&d,&depth_),"depth state");
        D3D11_BLEND_DESC blend{};auto& t=blend.RenderTarget[0];t.BlendEnable=TRUE;t.SrcBlend=D3D11_BLEND_ONE;t.DestBlend=D3D11_BLEND_INV_SRC_ALPHA;t.BlendOp=D3D11_BLEND_OP_ADD;
        t.SrcBlendAlpha=D3D11_BLEND_ZERO;t.DestBlendAlpha=D3D11_BLEND_ONE;t.BlendOpAlpha=D3D11_BLEND_OP_ADD;t.RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
        checked(device_->CreateBlendState(&blend,&blend_),"blend state");
    }
    // `eye`: the swapchain image; `source`: the image just copied into it; `fov`: the submitted layer's FOV (radians).
    void draw(ID3D11Texture2D* eye,ID3D11Texture2D* source,const XrFovf& fov,float magnification) {
        if(!eye || !source || eye==source) throw std::invalid_argument("Spyglass lens: arguments");
        D3D11_TEXTURE2D_DESC e{},s{};eye->GetDesc(&e);source->GetDesc(&s);
        if(e.Width!=s.Width || e.Height!=s.Height || e.SampleDesc.Count!=1 || s.SampleDesc.Count!=1) throw std::invalid_argument("Spyglass lens: geometry");
        ContextState::Scope scope(state_);
        // Read the source as the copy wrote it: typeless storage (the headset's swapchain) is written byte for byte (UNORM),
        // a typed image (the sim's sRGB chain) through its own format, and a typeless source is read the same way.
        const DXGI_FORMAT write=e.Format==DXGI_FORMAT_R8G8B8A8_TYPELESS?DXGI_FORMAT_R8G8B8A8_UNORM:e.Format;
        D3D11_SHADER_RESOURCE_VIEW_DESC sd{};sd.Format=s.Format==DXGI_FORMAT_R8G8B8A8_TYPELESS?write:s.Format;sd.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;sd.Texture2D.MipLevels=1;
        D3D11_RENDER_TARGET_VIEW_DESC rd{};rd.Format=write;rd.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
        Ptr<ID3D11ShaderResourceView> input;Ptr<ID3D11RenderTargetView> output;
        checked(device_->CreateShaderResourceView(source,&sd,&input),"source view");
        checked(device_->CreateRenderTargetView(eye,&rd,&output),"eye view");
        const std::array<float,12> data{std::tan(fov.angleLeft),std::tan(fov.angleRight),std::tan(fov.angleUp),std::tan(fov.angleDown),
            static_cast<float>(spyglass::lens_tan(fov.angleLeft,fov.angleRight,fov.angleUp,fov.angleDown)),magnification,static_cast<float>(spyglass::rim_outer),0,
            1.f/s.Width,1.f/s.Height,static_cast<float>(s.Width),static_cast<float>(s.Height)};
        context_->UpdateSubresource(constants_.Get(),0,nullptr,data.data(),0,0);
        D3D11_VIEWPORT viewport{0,0,static_cast<float>(e.Width),static_cast<float>(e.Height),0,1};context_->RSSetViewports(1,&viewport);context_->RSSetState(raster_.Get());
        context_->IASetInputLayout(nullptr);context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        context_->VSSetShader(vs_.Get(),nullptr,0);context_->PSSetShader(ps_.Get(),nullptr,0);
        context_->GSSetShader(nullptr,nullptr,0);context_->HSSetShader(nullptr,nullptr,0);context_->DSSetShader(nullptr,nullptr,0);
        context_->OMSetDepthStencilState(depth_.Get(),0);context_->OMSetBlendState(blend_.Get(),nullptr,0xffffffff);
        auto* constant=constants_.Get();context_->VSSetConstantBuffers(0,1,&constant);context_->PSSetConstantBuffers(0,1,&constant);
        auto* sampler=sampler_.Get();context_->PSSetSamplers(0,1,&sampler);auto* srv=input.Get();context_->PSSetShaderResources(0,1,&srv);
        auto* rtv=output.Get();context_->OMSetRenderTargets(1,&rtv,nullptr);context_->Draw(4,0);
        ID3D11ShaderResourceView* empty{};context_->PSSetShaderResources(0,1,&empty);context_->OMSetRenderTargets(0,nullptr,nullptr);
    }
};
} // namespace d2vr
