#pragma once
#include "tracked_pose.hpp"
#include "context_state.hpp"
#include <openxr/openxr.h>
#include <d3dcompiler.h>
#include <optional>
#include <array>

namespace d2vr {
struct NativeArtRect {float left{},top{},right{},bottom{};};
// v685: a decorative eye-art draw must never end the XR session. On the headset a failed
// NativeEyeArt draw threw out of GameXr::present into the render callback's handler, which treats it
// as a session fault: vr end, flat desktop for the backoff, then a rebuilt session (18:12:45/18:12:51/
// 18:14:07 in the v684 xr-recoveries.log). draw() now reports the first failing D3D call with its
// HRESULT; the caller drops that art and the frame keeps presenting.
struct NativeEyeArtResult {
    bool drawn{},rejected{};HRESULT hr{S_OK};const char* call{""};DXGI_FORMAT eye_view{},art_view{};
    explicit operator bool() const noexcept {return drawn;}
};
// A view needs a concrete format: OpenXR hands the typed sRGB chain typeless R8G8B8A8 storage
// (xr-transfer-*.json destination_format=27), and a TYPELESS SRV/RTV descriptor is rejected.
inline DXGI_FORMAT native_art_view_format(DXGI_FORMAT format) noexcept {
    return format==DXGI_FORMAT_R8G8B8A8_TYPELESS?DXGI_FORMAT_R8G8B8A8_UNORM:format;
}
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
    Ptr<ID3D11VertexShader> vs_;Ptr<ID3D11PixelShader> ps_,ps_key_,ps_unback_;Ptr<ID3D11Buffer> constants_;
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
// 4x4 taps over the pixel's own footprint: the art is captured at the eye target's resolution and usually drawn smaller,
// and with no mip chain a single bilinear tap shimmered and dropped text strokes (v689 headset: "low resolution, hard to
// read"). When magnified the taps fall inside one texel and this is plain bilinear.
float4 PS(V v):SV_TARGET{float2 dx=ddx(v.uv),dy=ddy(v.uv);float4 c=0;
    [unroll]for(int i=0;i<4;++i)[unroll]for(int j=0;j<4;++j){float2 o=(float2(i,j)+.5)/4-.5;c+=nativeArt.SampleLevel(artSampler,v.uv+o.x*dx+o.y*dy,0);}
    return c/16;}
// v721 dialogue choices: the movie's dim full-screen backdrop (dark, translucent) is keyed out so only the lit choice art stays.
float4 PSKey(V v):SV_TARGET{float4 c=PS(v);const float lit=max(c.r,max(c.g,c.b))/max(c.a,1e-3);return c*smoothstep(.10,.22,lit);}
// v763 hand wheel (user, headset v762: keyed by brightness the ring looked pale): the wheel movie lays a dim full-screen layer under its art,
// premultiplied colour about (.046,.050,.056) at alpha .502 out to .85 of the half-diagonal, then a vignette to alpha 1 in the corners
// (measured on world-ui-1). Un-composite it: the art's own coverage a=(alpha-bgA)/(1-bgA), its colour the premultiplied colour minus the
// layer under it. The ring keeps its full brightness and opacity; only the layer goes.
// v1144 (user, 2026-11-11: "a lot of people complain about the black frame in the weapon wheel"): the game's fade toward the image edges is a rounded
// RECTANGLE with a smoky texture, not the radial model above (headset capture world-ui-1, 2496x2688: mid-left edge alpha .80 vs .60 modelled), so a
// dark half-transparent band stayed round the wheel. Toward the edges (a rounded-rectangle metric) a pixel whose un-composited colour is near black is
// that leftover and goes; the ring in the centre keeps its dark discs (a key everywhere made them pale, v762). Lab: unback_lab.py on world-ui-1.
// v1145 (user, headset v1144: "only the main weapon wheel elements, rendered like inside the game"): the movie's other pieces (the potion and
// shortcut corners, the Hide/Show hint, the d-pad) go too. The ring, its centre art and the item name sit within .245 of the image WIDTH from the
// wheel's centre (the stage is fitted by width; centre (-.005,+.0205) width units off the texture centre); a soft cut to .275 (world-ui-1 lab).
float4 Unframe(float2 uv,float4 r){const float2 q=abs(uv-.5)*2;const float m=pow(pow(q.x,4)+pow(q.y,4),.25);
    const float lit=max(r.r,max(r.g,r.b))/max(r.a,1e-3);float w,h;nativeArt.GetDimensions(w,h);
    const float d=length(float2(uv.x-.5+.005,(uv.y-.5)*h/max(w,1)-.0205));
    return r*(1-smoothstep(.50,.80,m)*(1-smoothstep(.05,.14,lit)))*(1-smoothstep(.245,.275,d));}
float4 PSUnback(V v):SV_TARGET{float4 c=PS(v);const float edge=smoothstep(.85,1.38,length((v.uv-.5)*2));
    const float bgA=.502+.498*edge,a=saturate((c.a-bgA)/max(1-bgA,1e-3));
    return Unframe(v.uv,float4(min(max(c.rgb-float3(.046,.050,.056)*lerp(1.,.6,edge)*(1-a),0),a),a));}
)HLSL";
        Ptr<ID3DBlob> vs,ps,errors;checked(D3DCompile(source,sizeof(source)-1,nullptr,nullptr,nullptr,"VS","vs_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&vs,&errors));
        checked(D3DCompile(source,sizeof(source)-1,nullptr,nullptr,nullptr,"PS","ps_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&ps,&errors));
        checked(device_->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&vs_));checked(device_->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&ps_));
        Ptr<ID3DBlob> key;checked(D3DCompile(source,sizeof(source)-1,nullptr,nullptr,nullptr,"PSKey","ps_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&key,&errors));
        checked(device_->CreatePixelShader(key->GetBufferPointer(),key->GetBufferSize(),nullptr,&ps_key_));
        Ptr<ID3DBlob> unback;checked(D3DCompile(source,sizeof(source)-1,nullptr,nullptr,nullptr,"PSUnback","ps_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&unback,&errors));
        checked(device_->CreatePixelShader(unback->GetBufferPointer(),unback->GetBufferSize(),nullptr,&ps_unback_));
        D3D11_BUFFER_DESC b{};b.ByteWidth=32;b.BindFlags=D3D11_BIND_CONSTANT_BUFFER;b.Usage=D3D11_USAGE_DEFAULT;checked(device_->CreateBuffer(&b,nullptr,&constants_));
        D3D11_SAMPLER_DESC s{};s.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;s.AddressU=s.AddressV=s.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;s.MaxLOD=D3D11_FLOAT32_MAX;checked(device_->CreateSamplerState(&s,&sampler_));
        D3D11_RASTERIZER_DESC r{};r.FillMode=D3D11_FILL_SOLID;r.CullMode=D3D11_CULL_NONE;r.DepthClipEnable=TRUE;checked(device_->CreateRasterizerState(&r,&raster_));
        D3D11_DEPTH_STENCIL_DESC d{};d.DepthFunc=D3D11_COMPARISON_ALWAYS;checked(device_->CreateDepthStencilState(&d,&depth_));
        D3D11_BLEND_DESC blend{};auto& target=blend.RenderTarget[0];target.BlendEnable=TRUE;target.SrcBlend=D3D11_BLEND_ONE;target.DestBlend=D3D11_BLEND_INV_SRC_ALPHA;target.BlendOp=D3D11_BLEND_OP_ADD;
        target.SrcBlendAlpha=D3D11_BLEND_ONE;target.DestBlendAlpha=D3D11_BLEND_INV_SRC_ALPHA;target.BlendOpAlpha=D3D11_BLEND_OP_ADD;target.RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
        checked(device_->CreateBlendState(&blend,&premultiplied_));target.SrcBlend=D3D11_BLEND_SRC_ALPHA;checked(device_->CreateBlendState(&blend,&straight_));
    }
    NativeEyeArtResult draw(ID3D11Texture2D* eye,ID3D11Texture2D* art,const D3D11_BOX& crop,NativeArtRect rect,DXGI_FORMAT format,bool premultiplied=true,DXGI_FORMAT source_format=DXGI_FORMAT_UNKNOWN,bool keyed_replace=false,bool unback=false){ // unback: with keyed_replace, the wheel layer un-composited
        NativeEyeArtResult out{};
        const auto fail=[&out](HRESULT value,const char* call) {out.hr=value;out.call=call;return out;};
        if(!eye || !art || eye==art) {out.rejected=true;out.call="arguments";return out;}
        D3D11_TEXTURE2D_DESC e{},a{};eye->GetDesc(&e);art->GetDesc(&a);
        Ptr<ID3D11Device> eo,ao;eye->GetDevice(&eo);art->GetDevice(&ao);
        if(eo.Get()!=device_.Get() || ao.Get()!=device_.Get() || e.SampleDesc.Count!=1 || a.SampleDesc.Count!=1 ||
           e.ArraySize!=1 || a.ArraySize!=1 || !e.Width || !e.Height || crop.left>=crop.right || crop.top>=crop.bottom || crop.right>a.Width || crop.bottom>a.Height || crop.front || crop.back!=1)
            {out.rejected=true;out.call="geometry";return out;}
        // A TYPELESS descriptor is not a valid view format. OpenXR hands the typed sRGB chain typeless
        // R8G8B8A8 storage here, so the view follows the resource: typeless storage gets the
        // byte-preserving UNORM view (the art pixels are already sRGB-encoded, and an sRGB view of
        // typeless storage composited nothing in the contract fixture), a typed eye keeps its format.
        out.eye_view=e.Format==DXGI_FORMAT_R8G8B8A8_TYPELESS?DXGI_FORMAT_R8G8B8A8_UNORM:native_art_view_format(format);
        out.art_view=source_format==DXGI_FORMAT_UNKNOWN?native_art_view_format(format):native_art_view_format(source_format);
        ContextState::Scope scope(state_);
        // Try the documented format first, then the resource's own concrete format, then plain UNORM:
        // a runtime whose storage is typeless only answers one of them.
        Ptr<ID3D11ShaderResourceView> input;HRESULT hr=E_FAIL;
        for(unsigned attempt=0;attempt<3 && !input;++attempt) {
            const DXGI_FORMAT view_format=attempt==0?out.art_view:attempt==1?native_art_view_format(a.Format):DXGI_FORMAT_R8G8B8A8_UNORM;
            D3D11_SHADER_RESOURCE_VIEW_DESC sd{};sd.Format=view_format;sd.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;sd.Texture2D.MipLevels=1;
            hr=device_->CreateShaderResourceView(art,&sd,&input);
            if(SUCCEEDED(hr)) out.art_view=view_format;
        }
        if(!input) return fail(hr,"CreateShaderResourceView");
        Ptr<ID3D11RenderTargetView> output;hr=E_FAIL;
        for(unsigned attempt=0;attempt<3 && !output;++attempt) {
            const DXGI_FORMAT view_format=attempt==0?out.eye_view:attempt==1?native_art_view_format(e.Format):DXGI_FORMAT_R8G8B8A8_UNORM;
            D3D11_RENDER_TARGET_VIEW_DESC rd{};rd.Format=view_format;rd.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
            hr=device_->CreateRenderTargetView(eye,&rd,&output);
            if(SUCCEEDED(hr)) out.eye_view=view_format;
        }
        if(!output) return fail(hr,"CreateRenderTargetView");
        const std::array<float,8> data{rect.left,rect.top,rect.right,rect.bottom,static_cast<float>(crop.left)/a.Width,static_cast<float>(crop.top)/a.Height,static_cast<float>(crop.right)/a.Width,static_cast<float>(crop.bottom)/a.Height};
        context_->UpdateSubresource(constants_.Get(),0,nullptr,data.data(),0,0);
        D3D11_VIEWPORT viewport{0,0,static_cast<float>(e.Width),static_cast<float>(e.Height),0,1};context_->RSSetViewports(1,&viewport);context_->RSSetState(raster_.Get());
        context_->IASetInputLayout(nullptr);context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);context_->VSSetShader(vs_.Get(),nullptr,0);context_->PSSetShader(keyed_replace?(unback?ps_unback_.Get():ps_key_.Get()):ps_.Get(),nullptr,0);
        context_->GSSetShader(nullptr,nullptr,0);context_->HSSetShader(nullptr,nullptr,0);context_->DSSetShader(nullptr,nullptr,0);context_->OMSetDepthStencilState(depth_.Get(),0);
        context_->OMSetBlendState(premultiplied?premultiplied_.Get():straight_.Get(),nullptr,0xffffffff);
        auto* constant=constants_.Get();context_->VSSetConstantBuffers(0,1,&constant);auto* sampler=sampler_.Get();context_->PSSetSamplers(0,1,&sampler);auto* srv=input.Get();context_->PSSetShaderResources(0,1,&srv);
        auto* rtv=output.Get();if(keyed_replace) {constexpr float clear[4]{};context_->ClearRenderTargetView(rtv,clear);}
        context_->OMSetRenderTargets(1,&rtv,nullptr);context_->Draw(4,0);
        ID3D11ShaderResourceView* empty{};context_->PSSetShaderResources(0,1,&empty);context_->OMSetRenderTargets(0,nullptr,nullptr);
        out.drawn=true;return out;
    }
};
} // namespace d2vr
