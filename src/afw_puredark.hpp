#pragma once
// AFW renderer (ported from the Indurhra DLSS/AFW fork, docs/AFW-RENDERER.md): in alternate-eye gameplay the engine renders one eye per
// displayed frame. The built-in path (stale_eye_warp.hpp) re-projects the OTHER eye's kept, one-frame-old image. This backend instead
// REBUILDS the other eye from the fresh eye of this moment with PureDark's frame-warp plugin (PDAFWPlugin.dll, user supplied, never
// redistributed): CombinedWarping reprojects the fresh colour with its depth and motion vectors into the other eye's view and fills
// disocclusions from the plugin's own temporal history.
//
// The plugin is D3D12 only. The game is D3D11, so a same-adapter D3D12 device and queue are created, and colour/depth/motion are copied
// into shared textures, fenced D3D11 -> D3D12, warped, copied to a shared output and fenced back. No CPU waits or readbacks: three command
// slots guard allocator reuse; a busy slot refuses the frame (the caller falls back to the built-in warp for that frame).
//
// ABI: only the subset of PureDark's public plugin interface this file calls is declared below, laid out field for field and checked by
// size (DeviceParams 40, TextureDesc 56, EyeFrameBuffers 336, CameraData 768 bytes, as verified against the RE4AFW_CPPVRMod header
// 5f064ac0 by the original fork's probe). Another plugin build may differ: InitDevice failing or an incompatible output refuses AFW.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d11_4.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <memory>
#include <mutex>
#include <string>
#pragma comment(lib,"d3d12.lib")

namespace d2vr {
namespace afw {
using Microsoft::WRL::ComPtr;

// ---- PureDark plugin ABI subset (column-major, column-vector 4x4 matrices) ----
namespace pd {
struct Mat4 {float m[16];};
struct DeviceParams {ID3D11Device* d3d11Device{};ID3D11DeviceContext* d3d11Context{};ID3D12Device* d3d12Device{};ID3D12CommandQueue* d3d12Queue{};IDXGIAdapter* dxgiAdapter{};};
struct FrameWarpInitParams {int hmdWidth;int hmdHeight;DXGI_FORMAT eyeFormat=DXGI_FORMAT_R8G8B8A8_UNORM;DXGI_FORMAT backbufferFormat=DXGI_FORMAT_R10G10B10A2_UNORM;};
enum EyeIndex {EyeLeft=0,EyeRight=1};
enum FrameWarpMode {None,AlternateEyeWarping,PreviousFrameWarping,CombinedWarping};
enum ImageType {Image,Depth};
struct TextureDesc {
    ImageType type=Image;ID3D12Resource* pTexture=nullptr;int srvPos=-1;int uavPos=-1;
    D3D12_GPU_DESCRIPTOR_HANDLE shaderResourceViewHandle{0};D3D12_GPU_DESCRIPTOR_HANDLE unorderedAccessViewHandle{0};
    D3D12_CPU_DESCRIPTOR_HANDLE renderTargetViewHandle{0};D3D12_RESOURCE_STATES initialState=D3D12_RESOURCE_STATE_COMMON;
};
struct FrameBufferDesc {TextureDesc color,depth,motionVectors;};
struct EyeFrameBuffers {FrameBufferDesc eyeFrameBuffers[2];};
struct CameraData {Mat4 destWorldToView,destViewToWorld,destViewToClip,destClipToView,srcWorldToView,srcViewToWorld,srcViewToClip,srcClipToView,camWorldToView,camViewToWorld,camViewToClip,camClipToView;};
enum MVType {Normal,FromOtherEye,ObjectOnly};
enum class ShadingRate : std::uint32_t {ShadingRate_1X1=(1u<<3)|1u};
struct FrameWarpEvaluateParams {
    void* InCmdList=nullptr;FrameBufferDesc* InEyeFrameBuffer=nullptr;FrameBufferDesc* OutEyeFrameBuffer=nullptr;TextureDesc* InUIColorAlpha=nullptr;
    float InUIScale[2]={1.f,1.f};float InUIPos[3]={0.f,0.f,-1.f};float InMotionScale[2]={0.f,0.f};
    FrameWarpMode Mode=CombinedWarping;EyeIndex EyeIndex=EyeLeft;CameraData* CameraData=nullptr;bool ClearBeforeWarping=false;
    float IgnoreMotionThreshold=2.5f;bool IsHudlessColor=true;MVType MotionVectorsType=Normal;bool Debug=false;bool IsFoveated=false;RECT FoveatedArea{};
    TextureDesc* InUEVelocityBuffer=nullptr;bool UseUINT64=false;ShadingRate ShadingRate=ShadingRate::ShadingRate_1X1;float Reserved[12]={};
};
// Only the vtable prefix up to SetupTextureDesc (slot 14) is called; the order matches the public interface.
struct __declspec(novtable) D3D12RendererAPI {
    virtual ID3D12GraphicsCommandList* BeginCommandList(int)=0;virtual void EndCommandList(int)=0;virtual void WaitIdle(int)=0;
    virtual ID3D12Device* GetDevice()=0;virtual ID3D12CommandQueue* GetCommandQueue()=0;virtual ID3D12DescriptorHeap* GetViewHeap()=0;
    virtual D3D12_CPU_DESCRIPTOR_HANDLE GetRTV(ID3D12Resource*)=0;virtual D3D12_CPU_DESCRIPTOR_HANDLE GetDSV(ID3D12Resource*)=0;
    virtual int CreateSRV(ID3D12Resource*,int)=0;virtual int CreateUAV(ID3D12Resource*,int)=0;virtual int CreateCBV(ID3D12Resource*,int)=0;
    virtual D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(int)=0;virtual D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(int)=0;
    virtual D3D12_GPU_DESCRIPTOR_HANDLE GetSamplerHandle(int)=0;virtual void SetupTextureDesc(TextureDesc&)=0;
};
using InitDeviceFn=D3D12RendererAPI* (__stdcall*)(DeviceParams);
using InitFrameWarpFn=EyeFrameBuffers (__stdcall*)(FrameWarpInitParams);
using EvaluateFrameWarpFn=void (__stdcall*)(FrameWarpEvaluateParams&);
static_assert(sizeof(DeviceParams)==40 && sizeof(TextureDesc)==56 && sizeof(EyeFrameBuffers)==336 && sizeof(CameraData)==768,"PureDark ABI layout");
} // namespace pd

// ---- Camera math (pure; tests/afw_camera_check.cpp) ----
// A camera as the stale-eye warp knows it: world position, basis (forward, left, up), the OpenXR FOV tangents it rendered with, and the
// engine's reverse-Z depth mapping (device depth d = m11/w - m10 for distance w ahead).
struct Camera {double pos[3]{};double fwd[3]{},left[3]{},up[3]{};double tan_left{},tan_right{},tan_up{},tan_down{};double m10{},m11{};};
// Row-major math matrix, column-vector convention.
using M4=std::array<double,16>;
inline M4 view_matrix(const Camera& c) {
    // View space: x right (= -left), y up, z forward. Rows are the axes; the translation brings the camera to the origin.
    const double r[3]={-c.left[0],-c.left[1],-c.left[2]};
    const double* rows[3]={r,c.up,c.fwd};M4 m{};
    for(int i=0;i<3;++i) {m[i*4+0]=rows[i][0];m[i*4+1]=rows[i][1];m[i*4+2]=rows[i][2];m[i*4+3]=-(rows[i][0]*c.pos[0]+rows[i][1]*c.pos[1]+rows[i][2]*c.pos[2]);}
    m[15]=1;return m;
}
// clip.x/w = (x/z - cx)/sx, clip.y/w likewise (D3D NDC, y up), clip.z/w = m11/z - m10 (the engine's reverse Z), clip.w = z.
inline M4 projection_matrix(const Camera& c,double m10,double m11) {
    const double sx=(c.tan_right-c.tan_left)/2,cx=(c.tan_right+c.tan_left)/2,sy=(c.tan_up-c.tan_down)/2,cy=(c.tan_up+c.tan_down)/2;
    M4 m{};m[0]=1/sx;m[2]=-cx/sx;m[5]=1/sy;m[6]=-cy/sy;m[10]=-m10;m[11]=m11;m[14]=1;return m;
}
inline bool invert(const M4& a,M4& out) {
    M4 inv{};
    inv[0]=a[5]*a[10]*a[15]-a[5]*a[11]*a[14]-a[9]*a[6]*a[15]+a[9]*a[7]*a[14]+a[13]*a[6]*a[11]-a[13]*a[7]*a[10];
    inv[4]=-a[4]*a[10]*a[15]+a[4]*a[11]*a[14]+a[8]*a[6]*a[15]-a[8]*a[7]*a[14]-a[12]*a[6]*a[11]+a[12]*a[7]*a[10];
    inv[8]=a[4]*a[9]*a[15]-a[4]*a[11]*a[13]-a[8]*a[5]*a[15]+a[8]*a[7]*a[13]+a[12]*a[5]*a[11]-a[12]*a[7]*a[9];
    inv[12]=-a[4]*a[9]*a[14]+a[4]*a[10]*a[13]+a[8]*a[5]*a[14]-a[8]*a[6]*a[13]-a[12]*a[5]*a[10]+a[12]*a[6]*a[9];
    inv[1]=-a[1]*a[10]*a[15]+a[1]*a[11]*a[14]+a[9]*a[2]*a[15]-a[9]*a[3]*a[14]-a[13]*a[2]*a[11]+a[13]*a[3]*a[10];
    inv[5]=a[0]*a[10]*a[15]-a[0]*a[11]*a[14]-a[8]*a[2]*a[15]+a[8]*a[3]*a[14]+a[12]*a[2]*a[11]-a[12]*a[3]*a[10];
    inv[9]=-a[0]*a[9]*a[15]+a[0]*a[11]*a[13]+a[8]*a[1]*a[15]-a[8]*a[3]*a[13]-a[12]*a[1]*a[11]+a[12]*a[3]*a[9];
    inv[13]=a[0]*a[9]*a[14]-a[0]*a[10]*a[13]-a[8]*a[1]*a[14]+a[8]*a[2]*a[13]+a[12]*a[1]*a[10]-a[12]*a[2]*a[9];
    inv[2]=a[1]*a[6]*a[15]-a[1]*a[7]*a[14]-a[5]*a[2]*a[15]+a[5]*a[3]*a[14]+a[13]*a[2]*a[7]-a[13]*a[3]*a[6];
    inv[6]=-a[0]*a[6]*a[15]+a[0]*a[7]*a[14]+a[4]*a[2]*a[15]-a[4]*a[3]*a[14]-a[12]*a[2]*a[7]+a[12]*a[3]*a[6];
    inv[10]=a[0]*a[5]*a[15]-a[0]*a[7]*a[13]-a[4]*a[1]*a[15]+a[4]*a[3]*a[13]+a[12]*a[1]*a[7]-a[12]*a[3]*a[5];
    inv[14]=-a[0]*a[5]*a[14]+a[0]*a[6]*a[13]+a[4]*a[1]*a[14]-a[4]*a[2]*a[13]-a[12]*a[1]*a[6]+a[12]*a[2]*a[5];
    inv[3]=-a[1]*a[6]*a[11]+a[1]*a[7]*a[10]+a[5]*a[2]*a[11]-a[5]*a[3]*a[10]-a[9]*a[2]*a[7]+a[9]*a[3]*a[6];
    inv[7]=a[0]*a[6]*a[11]-a[0]*a[7]*a[10]-a[4]*a[2]*a[11]+a[4]*a[3]*a[10]+a[8]*a[2]*a[7]-a[8]*a[3]*a[6];
    inv[11]=-a[0]*a[5]*a[11]+a[0]*a[7]*a[9]+a[4]*a[1]*a[11]-a[4]*a[3]*a[9]-a[8]*a[1]*a[7]+a[8]*a[3]*a[5];
    inv[15]=a[0]*a[5]*a[10]-a[0]*a[6]*a[9]-a[4]*a[1]*a[10]+a[4]*a[2]*a[9]+a[8]*a[1]*a[6]-a[8]*a[2]*a[5];
    const double det=a[0]*inv[0]+a[1]*inv[4]+a[2]*inv[8]+a[3]*inv[12];
    if(!std::isfinite(det) || std::abs(det)<1e-12) return false;
    for(int i=0;i<16;++i) out[i]=inv[i]/det;
    return true;
}
// Row-major math matrix -> glm storage (column-major).
inline pd::Mat4 to_plugin(const M4& m) {pd::Mat4 o{};for(int r=0;r<4;++r) for(int c=0;c<4;++c) o.m[c*4+r]=static_cast<float>(m[r*4+c]);return o;}
// src = the fresh eye (rendered now), dest = the eye to rebuild. Both use the fresh image's depth mapping. False: degenerate input.
inline bool camera_data(const Camera& src,const Camera& dest,pd::CameraData& out) {
    if(!(src.m11>0) || !std::isfinite(src.m10) || !(src.tan_right>src.tan_left) || !(src.tan_up>src.tan_down) ||
       !(dest.tan_right>dest.tan_left) || !(dest.tan_up>dest.tan_down)) return false;
    const M4 sv=view_matrix(src),dv=view_matrix(dest),sp=projection_matrix(src,src.m10,src.m11),dp=projection_matrix(dest,src.m10,src.m11);
    M4 svi{},dvi{},spi{},dpi{};
    if(!invert(sv,svi) || !invert(dv,dvi) || !invert(sp,spi) || !invert(dp,dpi)) return false;
    out={};
    out.destWorldToView=to_plugin(dv);out.destViewToWorld=to_plugin(dvi);out.destViewToClip=to_plugin(dp);out.destClipToView=to_plugin(dpi);
    out.srcWorldToView=to_plugin(sv);out.srcViewToWorld=to_plugin(svi);out.srcViewToClip=to_plugin(sp);out.srcClipToView=to_plugin(spi);
    for(const auto* mat:{&out.destWorldToView,&out.srcViewToClip,&out.srcClipToView,&out.destClipToView}) for(float v:mat->m) if(!std::isfinite(v)) return false;
    return true;
}

// ---- Tuning (docs/AFW-RENDERER.md "Tuning"): live knobs, set from the VR menu / console ----
struct Tuning {
    std::atomic<float> ignore_motion_threshold{2.5f}; // per-object motion (px) the plugin ignores; higher keeps more of the fresh image static
    std::atomic<float> jitter_scale{0.f};             // fraction of the fresh eye's raster jitter removed from depth/motion sampling (0 = none)
    std::atomic<bool>  reset_on_gap{true};            // clear the plugin history when the fresh frames are not consecutive
    std::atomic<int>   max_frame_gap{1};              // consecutive = fresh frames at most this many displayed frames apart
    // Motion-vector multiplier. AFW_RENDERING_GUIDE.md (section 2, pitfall 21) predicts 2 under alternation (history two frames old,
    // vectors one), but the 2026-11-11 simulator A/B (walk, Edge of the World balcony, FSR 3 Native, both eyes at t=10..22 s) showed
    // 2 cracks the arm and hand badly (the background shows through them) while 1 keeps them solid: 1 is the measured default.
    std::atomic<float> motion_scale{1.f};
    // Depth/motion dilation radius (source texels) toward the nearest surface. 2026-11-11 capture A/B (tools/afw_capture_sheet.py, rebuilt vs
    // the eye's next native frame, normalised by the held-frame error): in motion halo 0 = 0.87, 1 = 0.94, 2 = 0.77 hand error and
    // 0.86 / 0.99 / 0.70 ghost fraction; static cost equal (1.12-1.14). Halo 0 left background-coloured cracks along the sleeve and fingers.
    std::atomic<int>   halo{2};
};
inline Tuning& tuning() {static Tuning t;return t;}

// ---- Depth/motion adapter: the engine's render rectangle (top left of a full-size target) -> output-size R32 depth / RG16F motion ----
class InputAdapter {
    ComPtr<ID3D11Device> device_;ComPtr<ID3D11DeviceContext1> context_;ComPtr<ID3DDeviceContextState> state_;
    ComPtr<ID3D11ComputeShader> shader_;ComPtr<ID3D11Buffer> params_;
    ComPtr<ID3D11Texture2D> depth_,motion_;ComPtr<ID3D11UnorderedAccessView> depth_uav_,motion_uav_;UINT width_{},height_{};
public:
    ID3D11Texture2D* depth() const noexcept {return depth_.Get();}
    ID3D11Texture2D* motion() const noexcept {return motion_.Get();}
    bool prepare(ID3D11DeviceContext* c,ID3D11Texture2D* depth,ID3D11Texture2D* motion,UINT rw,UINT rh,UINT ow,UINT oh,float jitter_px_x,float jitter_px_y) {
        if(!c || !depth || !motion || !rw || !rh || !ow || !oh || ow>16384 || oh>16384 || !std::isfinite(jitter_px_x) || !std::isfinite(jitter_px_y) ||
           c->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE) return false;
        ComPtr<ID3D11Device> device;c->GetDevice(&device);
        D3D11_TEXTURE2D_DESC zd{},md{};depth->GetDesc(&zd);motion->GetDesc(&md);
        if(zd.Format!=DXGI_FORMAT_R32_FLOAT || md.Format!=DXGI_FORMAT_R16G16_FLOAT || zd.Width<rw || zd.Height<rh || md.Width<rw || md.Height<rh) return false;
        if(device_.Get()!=device.Get()) {
            *this=InputAdapter{};device_=device;ComPtr<ID3D11Device1> d1;
            if(FAILED(device.As(&d1)) || FAILED(c->QueryInterface(IID_PPV_ARGS(&context_)))) return false;
            auto level=device->GetFeatureLevel();D3D_FEATURE_LEVEL chosen{};
            if(FAILED(d1->CreateDeviceContextState(0,&level,1,D3D11_SDK_VERSION,__uuidof(ID3D11Device),&chosen,&state_))) return false;
            static const char code[]=R"(
Texture2D<float> depth:register(t0);Texture2D<float2> motion:register(t1);
RWTexture2D<float> outDepth:register(u0);RWTexture2D<float2> outMotion:register(u1);
cbuffer P:register(b0){float2 activeSize,outputSize,rasterJitter;float halo,pad;};
[numthreads(8,8,1)]void main(uint3 id:SV_DispatchThreadID){if(any(id.xy>=uint2(outputSize)))return;
 int2 s=clamp(int2(floor((float2(id.xy)+.5)*activeSize/outputSize+rasterJitter)),int2(0,0),int2(activeSize)-1);
 // Halo: the nearest surface (reverse Z: largest depth) within `halo` source texels, with its motion, so the silhouette of near
 // content (hands, weapon) is warped with it instead of leaving background-coloured cracks along its edge.
 int r=(int)halo;int2 best=s;float z=depth.Load(int3(s,0));
 for(int dy=-r;dy<=r;++dy) for(int dx=-r;dx<=r;++dx){int2 q=clamp(s+int2(dx,dy),int2(0,0),int2(activeSize)-1);float d=depth.Load(int3(q,0));if(d>z){z=d;best=q;}}
 outDepth[id.xy]=z;outMotion[id.xy]=motion.Load(int3(best,0));})";
            ComPtr<ID3DBlob> blob,error;
            if(FAILED(D3DCompile(code,sizeof(code)-1,nullptr,nullptr,nullptr,"main","cs_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&blob,&error)) ||
               FAILED(device->CreateComputeShader(blob->GetBufferPointer(),blob->GetBufferSize(),nullptr,&shader_))) return false;
            D3D11_BUFFER_DESC b{};b.ByteWidth=32;b.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
            if(FAILED(device->CreateBuffer(&b,nullptr,&params_))) return false;
        }
        if(!shader_ || !state_ || !params_) return false;
        if(ow!=width_ || oh!=height_) {
            depth_.Reset();motion_.Reset();depth_uav_.Reset();motion_uav_.Reset();width_=height_=0;
            D3D11_TEXTURE2D_DESC d{};d.Width=ow;d.Height=oh;d.MipLevels=d.ArraySize=d.SampleDesc.Count=1;d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;
            d.Format=DXGI_FORMAT_R32_FLOAT;if(FAILED(device->CreateTexture2D(&d,nullptr,&depth_)) || FAILED(device->CreateUnorderedAccessView(depth_.Get(),nullptr,&depth_uav_))) return false;
            d.Format=DXGI_FORMAT_R16G16_FLOAT;if(FAILED(device->CreateTexture2D(&d,nullptr,&motion_)) || FAILED(device->CreateUnorderedAccessView(motion_.Get(),nullptr,&motion_uav_))) return false;
            width_=ow;height_=oh;
        }
        ComPtr<ID3D11ShaderResourceView> z,mv;
        if(FAILED(device->CreateShaderResourceView(depth,nullptr,&z)) || FAILED(device->CreateShaderResourceView(motion,nullptr,&mv))) return false;
        ComPtr<ID3DDeviceContextState> prior;context_->SwapDeviceContextState(state_.Get(),&prior);
        const float values[8]={float(rw),float(rh),float(ow),float(oh),jitter_px_x,jitter_px_y,static_cast<float>(std::clamp(tuning().halo.load(),0,4)),0};
        c->UpdateSubresource(params_.Get(),0,nullptr,values,0,0);ID3D11Buffer* cb=params_.Get();c->CSSetConstantBuffers(0,1,&cb);
        ID3D11ShaderResourceView* srvs[]={z.Get(),mv.Get()};ID3D11UnorderedAccessView* uavs[]={depth_uav_.Get(),motion_uav_.Get()};
        c->CSSetShaderResources(0,2,srvs);c->CSSetUnorderedAccessViews(0,2,uavs,nullptr);c->CSSetShader(shader_.Get(),nullptr,0);
        c->Dispatch((ow+7)/8,(oh+7)/8,1);
        ID3D11ShaderResourceView* no_srv[2]{};ID3D11UnorderedAccessView* no_uav[2]{};c->CSSetShaderResources(0,2,no_srv);c->CSSetUnorderedAccessViews(0,2,no_uav,nullptr);
        context_->SwapDeviceContextState(prior.Get(),nullptr);
        return true;
    }
};

// ---- D3D11 <-> D3D12 plugin bridge ----
class Backend {
public:
    struct Frame {ID3D11Texture2D *color{},*depth{},*motion{};pd::CameraData camera{};int eye=-1;float motion_scale[2]{};bool reset{};};
    const std::string& problem() const noexcept {return problem_;}
    bool initialized() const noexcept {return renderer_!=nullptr;}
    ID3D11Texture2D* output() const noexcept {return valid_?output_.r11.Get():nullptr;}
    // The plugin keeps process-global state and has no shutdown export: a Backend is never destroyed once initialized (see runtime()).
    bool initialize(ID3D11Device* device,const std::wstring& path) {
        if(renderer_) return fail("already initialized");if(!device) return fail("no device");
        HRESULT hr=device->QueryInterface(IID_PPV_ARGS(&d11_));if(FAILED(hr)) return fail("D3D11.4 device required",hr);
        ComPtr<ID3D11DeviceContext> ctx;device->GetImmediateContext(&ctx);if(FAILED(hr=ctx.As(&c11_))) return fail("D3D11.4 context required",hr);
        ComPtr<IDXGIDevice> dx;ComPtr<IDXGIAdapter> adapter;
        if(FAILED(hr=device->QueryInterface(IID_PPV_ARGS(&dx))) || FAILED(hr=dx->GetAdapter(&adapter))) return fail("adapter",hr);
        if(FAILED(hr=D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&d12_)))) return fail("same-adapter D3D12 device",hr);
        D3D12_COMMAND_QUEUE_DESC q{};if(FAILED(hr=d12_->CreateCommandQueue(&q,IID_PPV_ARGS(&queue_)))) return fail("D3D12 queue",hr);
        if(FAILED(hr=d12_->CreateFence(0,D3D12_FENCE_FLAG_SHARED,IID_PPV_ARGS(&fence_)))) return fail("shared fence",hr);
        HANDLE h=nullptr;if(FAILED(hr=d12_->CreateSharedHandle(fence_.Get(),nullptr,GENERIC_ALL,nullptr,&h))) return fail("fence handle",hr);
        hr=d11_->OpenSharedFence(h,IID_PPV_ARGS(&fence11_));CloseHandle(h);if(FAILED(hr)) return fail("D3D11 shared fence",hr);
        module_=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if(!module_) return fail("PDAFWPlugin.dll not found next to Dishonored2.exe",HRESULT_FROM_WIN32(GetLastError()));
        const auto init=reinterpret_cast<pd::InitDeviceFn>(GetProcAddress(module_,"InitDevice"));
        init_frames_=reinterpret_cast<pd::InitFrameWarpFn>(GetProcAddress(module_,"InitFrameWarp"));
        warp_=reinterpret_cast<pd::EvaluateFrameWarpFn>(GetProcAddress(module_,"EvaluateFrameWarp"));
        if(!init || !init_frames_ || !warp_) return fail("plugin exports");
        pd::DeviceParams dp{};dp.d3d12Device=d12_.Get();dp.d3d12Queue=queue_.Get();dp.dxgiAdapter=adapter.Get();
        renderer_=init(dp);if(!renderer_) return fail("plugin InitDevice (needs the D3D12 PDAFWPlugin build)");
        for(auto& s:slots_) {
            if(FAILED(hr=d12_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&s.allocator))) ||
               FAILED(hr=d12_->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,s.allocator.Get(),nullptr,IID_PPV_ARGS(&s.list))) || FAILED(hr=s.list->Close()))
                return fail("command resources",hr);
        }
        problem_.clear();return true;
    }
    // Render thread, immediate context. The output stays valid until the next evaluate (GPU fences order the shared storage).
    bool evaluate(ID3D11DeviceContext* context,const Frame& frame) {
        valid_=false;
        if(!renderer_ || !context || context->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE || frame.eye<0 || frame.eye>1) return fail("invalid frame/context");
        if(!std::isfinite(frame.motion_scale[0]) || !std::isfinite(frame.motion_scale[1]) || !frame.motion_scale[0] || !frame.motion_scale[1]) return fail("invalid motion scale");
        ID3D11Texture2D* textures[3]={frame.color,frame.depth,frame.motion};D3D11_TEXTURE2D_DESC desc[3]{};
        for(int i=0;i<3;++i) {
            if(!textures[i]) return fail("missing input");
            textures[i]->GetDesc(&desc[i]);
            if(desc[i].ArraySize!=1 || desc[i].MipLevels!=1 || desc[i].SampleDesc.Count!=1) return fail("invalid resource");
            if(i==0 && (desc[i].Format==DXGI_FORMAT_R8G8B8A8_TYPELESS || desc[i].Format==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)) desc[i].Format=DXGI_FORMAT_R8G8B8A8_UNORM;
            if(configured_ && (desc[i].Width!=input_[i].desc.Width || desc[i].Height!=input_[i].desc.Height || desc[i].Format!=input_[i].desc.Format))
                return fail("eye size changed: AFW needs a restart");
        }
        if(desc[0].Format!=DXGI_FORMAT_R8G8B8A8_UNORM || desc[1].Format!=DXGI_FORMAT_R32_FLOAT || desc[2].Format!=DXGI_FORMAT_R16G16_FLOAT) {
            char m[96];sprintf_s(m,"unsupported input formats %u/%u/%u",unsigned(desc[0].Format),unsigned(desc[1].Format),unsigned(desc[2].Format));return fail(m);
        }
        if(!configured_) {
            for(int i=0;i<3;++i) if(!share(desc[i],input_[i])) return false;
            if(!share(desc[0],output_)) return false;
            pd::FrameWarpInitParams init{int(desc[0].Width),int(desc[0].Height),desc[0].Format,desc[0].Format};
            const auto eyes=init_frames_(init);
            if(!eyes.eyeFrameBuffers[0].color.pTexture || !eyes.eyeFrameBuffers[1].color.pTexture) return fail("plugin output allocation");
            pd::TextureDesc* t[]={&framebuffer_.color,&framebuffer_.depth,&framebuffer_.motionVectors};
            for(int i=0;i<3;++i) {t[i]->pTexture=input_[i].r12.Get();renderer_->SetupTextureDesc(*t[i]);}
            configured_=true;
        }
        auto& slot=slots_[next_%slots_.size()];
        const auto completed=fence_->GetCompletedValue();
        if(completed==UINT64_MAX) return fail("D3D12 device lost");
        if(completed<slot.done) {++busy_;return fail("GPU command slot busy");}
        HRESULT hr;if(FAILED(hr=slot.allocator->Reset()) || FAILED(hr=slot.list->Reset(slot.allocator.Get(),nullptr))) return fail("command reset",hr);
        for(int i=0;i<3;++i) context->CopyResource(input_[i].r11.Get(),textures[i]);
        if(FAILED(hr=c11_->Signal(fence11_.Get(),++value_))) return fail("D3D11 signal",hr);
        context->Flush();
        if(FAILED(hr=queue_->Wait(fence_.Get(),value_))) return fail("D3D12 wait",hr);
        auto* list=slot.list.Get();pd::TextureDesc* t[]={&framebuffer_.color,&framebuffer_.depth,&framebuffer_.motionVectors};
        for(int i=0;i<3;++i) {transition(list,t[i]->pTexture,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);t[i]->initialState=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;}
        pd::CameraData camera=frame.camera;pd::FrameWarpEvaluateParams ep{};
        ep.InCmdList=list;ep.InEyeFrameBuffer=&framebuffer_;ep.CameraData=&camera;ep.Mode=pd::CombinedWarping;ep.EyeIndex=pd::EyeIndex(frame.eye);
        ep.MotionVectorsType=pd::Normal;ep.InMotionScale[0]=frame.motion_scale[0];ep.InMotionScale[1]=frame.motion_scale[1];ep.ClearBeforeWarping=frame.reset;
        ep.IgnoreMotionThreshold=tuning().ignore_motion_threshold.load();
        warp_(ep);
        if(!ep.OutEyeFrameBuffer || !ep.OutEyeFrameBuffer->color.pTexture) {list->Close();return fail("plugin returned no image");}
        auto& out=ep.OutEyeFrameBuffer->color;const auto actual=out.pTexture->GetDesc();
        if(actual.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D || actual.Width!=output_.desc.Width || actual.Height!=output_.desc.Height ||
           actual.Format!=DXGI_FORMAT_R8G8B8A8_UNORM || actual.SampleDesc.Count!=1 || actual.MipLevels!=1 || actual.DepthOrArraySize!=1) {list->Close();return fail("plugin returned incompatible output");}
        transition(list,out.pTexture,out.initialState,D3D12_RESOURCE_STATE_COPY_SOURCE);transition(list,output_.r12.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_DEST);
        list->CopyResource(output_.r12.Get(),out.pTexture);
        transition(list,out.pTexture,D3D12_RESOURCE_STATE_COPY_SOURCE,out.initialState);transition(list,output_.r12.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COMMON);
        for(int i=0;i<3;++i) {transition(list,t[i]->pTexture,t[i]->initialState,D3D12_RESOURCE_STATE_COMMON);t[i]->initialState=D3D12_RESOURCE_STATE_COMMON;}
        if(FAILED(hr=list->Close())) return fail("command close",hr);
        ID3D12CommandList* execute[]={list};queue_->ExecuteCommandLists(1,execute);slot.done=++value_;
        if(FAILED(hr=queue_->Signal(fence_.Get(),slot.done)) || FAILED(hr=c11_->Wait(fence11_.Get(),slot.done))) return fail("output fence",hr);
        ++next_;valid_=true;problem_.clear();return true;
    }
    std::uint64_t busy() const noexcept {return busy_;}
private:
    struct Shared {ComPtr<ID3D12Resource> r12;ComPtr<ID3D11Texture2D> r11;D3D11_TEXTURE2D_DESC desc{};};
    struct Slot {ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;UINT64 done{};};
    ComPtr<ID3D11Device5> d11_;ComPtr<ID3D11DeviceContext4> c11_;ComPtr<ID3D12Device> d12_;ComPtr<ID3D12CommandQueue> queue_;
    ComPtr<ID3D12Fence> fence_;ComPtr<ID3D11Fence> fence11_;UINT64 value_{};
    HMODULE module_{};pd::D3D12RendererAPI* renderer_{};pd::InitFrameWarpFn init_frames_{};pd::EvaluateFrameWarpFn warp_{};
    Shared input_[3],output_;pd::FrameBufferDesc framebuffer_{};std::array<Slot,3> slots_{};unsigned next_{};
    bool configured_{},valid_{};std::string problem_{"not started"};std::uint64_t busy_{};
    static void transition(ID3D12GraphicsCommandList* c,ID3D12Resource* r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b) {
        if(a==b) return;D3D12_RESOURCE_BARRIER t{};t.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;t.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,a,b};c->ResourceBarrier(1,&t);
    }
    bool fail(const char* text,HRESULT hr=E_FAIL) {char b[32];sprintf_s(b," (0x%08lx)",static_cast<unsigned long>(hr));problem_=std::string(text)+b;valid_=false;return false;}
    bool share(const D3D11_TEXTURE2D_DESC& src,Shared& out) {
        D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=src.Width;d.Height=src.Height;d.DepthOrArraySize=d.MipLevels=1;d.SampleDesc.Count=1;d.Format=src.Format;
        d.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET|D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
        HRESULT hr=d12_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_SHARED,&d,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&out.r12));
        if(FAILED(hr)) return fail("shared texture allocation",hr);
        HANDLE h=nullptr;hr=d12_->CreateSharedHandle(out.r12.Get(),nullptr,GENERIC_ALL,nullptr,&h);if(FAILED(hr)) return fail("shared texture handle",hr);
        hr=d11_->OpenSharedResource1(h,IID_PPV_ARGS(&out.r11));CloseHandle(h);
        if(FAILED(hr)) {char m[64];sprintf_s(m,"open D3D11 texture format %u",unsigned(src.Format));return fail(m,hr);}
        out.desc=src;return true;
    }
};

// ---- Per-session runtime: what the VR menu shows and the stale-eye path calls ----
class Runtime {
public:
    std::atomic<bool> requested{false};   // the VR menu's Renderer row chose AFW
    std::atomic<std::uint64_t> rebuilt{},refused{},fallbacks{};
    // The held eye `eye` rebuilt from the fresh eye. nullptr: use the built-in warp for this frame (reason in problem()).
    ID3D11Texture2D* rebuild(ID3D11DeviceContext* c,int fresh_eye,std::uint64_t frame,ID3D11Texture2D* fresh_color,ID3D11Texture2D* fresh_depth,ID3D11Texture2D* fresh_motion,
                             UINT render_w,UINT render_h,float jitter_px_x,float jitter_px_y,const Camera& src,const Camera& dest,const std::wstring& plugin_path) {
        const auto refuse=[&](const char* why)->ID3D11Texture2D* {++refused;set_problem(why);return nullptr;};
        if(!requested.load() || hard_failed_) return nullptr;
        if(!c || !fresh_color || !fresh_depth || !fresh_motion || fresh_eye<0 || fresh_eye>1) return refuse("missing fresh-eye colour, depth or motion (AFW needs an FSR or DLSS anti-aliasing mode)");
        if(!backend_.initialized()) {
            ComPtr<ID3D11Device> device;c->GetDevice(&device);
            if(!backend_.initialize(device.Get(),plugin_path)) {hard_failed_=true;++fallbacks;set_problem(backend_.problem());return nullptr;}
        }
        D3D11_TEXTURE2D_DESC cd{};fresh_color->GetDesc(&cd);
        const auto& tune=tuning();const float js=tune.jitter_scale.load();
        if(!adapter_.prepare(c,fresh_depth,fresh_motion,render_w,render_h,cd.Width,cd.Height,-jitter_px_x*js,-jitter_px_y*js)) return refuse("depth/motion conversion failed");
        Backend::Frame f{};
        if(!camera_data(src,dest,f.camera)) return refuse("degenerate camera");
        f.color=fresh_color;f.depth=adapter_.depth();f.motion=adapter_.motion();f.eye=fresh_eye;
        const float mv=tune.motion_scale.load(); // engine MV: UV units, current -> previous (fsr_taa Config mv_x/mv_y = -1), times the history age
        f.motion_scale[0]=-float(cd.Width)*mv;f.motion_scale[1]=-float(cd.Height)*mv;
        const int gap=std::max(1,tune.max_frame_gap.load());
        f.reset=tune.reset_on_gap.load() && (!last_frame_ || frame<=last_frame_ || frame-last_frame_>static_cast<std::uint64_t>(gap));
        if(!backend_.evaluate(c,f)) {
            const std::string p=backend_.problem();
            if(p.rfind("GPU command slot busy",0)!=0) {hard_failed_=true;++fallbacks;}
            ++refused;set_problem(p);return nullptr;
        }
        last_frame_=frame;++rebuilt;set_problem("AFW active");
        return backend_.output();
    }
    bool hard_failed() const noexcept {return hard_failed_;}
    std::string problem() const {std::lock_guard<std::mutex> l(mutex_);return problem_;}
    std::uint64_t busy() const noexcept {return backend_.busy();}
private:
    Backend backend_;InputAdapter adapter_;bool hard_failed_{};std::uint64_t last_frame_{};
    mutable std::mutex mutex_;std::string problem_{"off"};
    void set_problem(const std::string& p) {std::lock_guard<std::mutex> l(mutex_);problem_=p;}
};
// Process lifetime (never destroyed: the plugin holds the D3D12 objects without a shutdown export).
inline Runtime& runtime() {static Runtime* r=new Runtime();return *r;}
} // namespace afw
} // namespace d2vr
