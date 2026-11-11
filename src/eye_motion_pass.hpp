#pragma once
// v1151 performance-mode motion texture (docs/superpowers/specs/2026-11-11-perf-mode-motion-design.md section 2). Per eye render, next to
// FsrTaa's depth copy: RGBA16F at the render rectangle (top left), xyz = world displacement in metres of the surface at the pixel between
// this eye's previous render and this one, w = its object motion in render pixels (the engine MV less camera reprojection and the jitter
// step). 0 for the sky (depth 0), within arm_reach of the eye (the first-person arms and what they hold: their engine vectors do not follow
// the tracked pose, Phase 0 measured ~300 px; they belong to the warp's hand layers), and when the pass is off. v1153: also 0 (unknown) where
// the surface is within 1 cm of the previous camera plane or either UV lies more than an image beyond the edges, or xyz exceeds 100 m;
// w is clamped to 4096 px: nothing written can be Inf or NaN in half precision.
// Conventions, measured in Phase 0 (tools/alt_motion_probe.py on 64 sim dumps, static median 0.0004 px): the receipt matrices are
// row-major with column vectors (clip = M * v), view-projection in world space (not camera-relative), NDC y up (v = (1 - y)/2), reverse-Z
// (depth 0 = sky; metres ahead = proj[11] / (depth + proj[10])), engine MV in render UV = current - previous, and the previous UV that
// matches the previous snapshot is uv - mv - step, step = (prev.jitter.x - cur.jitter.x, cur.jitter.y - prev.jitter.y)
// (TaaMvDejitter::step on the receipt's jitters).
// The matrices are combined on the CPU in double and made relative to the current camera, so the shader never sees world coordinates.
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include "alternate_taa_state.hpp"

namespace d2vr {
struct EyeMotionConstants {
    float r[16];      // current NDC (x, y, depth, 1) -> previous clip
    float now[16];    // current NDC -> world point minus the current camera position (homogeneous)
    float then[16];   // previous NDC -> world point minus the current camera position (homogeneous)
    float size_step[4]; // render width, height; jitter step (UV)
    float proj[4];      // current projection [10], [11]; arm reach (m); on (1) / off (0)
    float mv_scale[4];  // engine MV -> render UV; [2] v1162: 1 = the rig mask (t2, nonzero = rig) is the arm rule instead of arm reach;
                        // [3] v1179: 1 = rig pixels marked 255 (own previous palette and rows) keep their motion, 254 stay 0
    float other[16];    // v1154: current NDC -> the other eye's clip one Present earlier (its receipt's current render)
    float other_step[4];// v1154: its jitter step (UV), the still bound (px), mover_px; 0 0 0 0 = no other-eye test
};
static_assert(sizeof(EyeMotionConstants)==320,"cbuffer layout");
// v1154: a pixel that moves by its own previous render but sits within this many render pixels of where the other eye's render one Present
// earlier puts it is a still surface whose draw kept the engine's other-eye rows (classes object_motion cannot rewrite: cb2 over 1 KB, not
// yet copied). Sim v1152 kept dumps: such pixels 0.002-0.042 px (p99), a walking guard >= 15 px (p1 of its movers).
// v1158: without that snapshot the pass makes no texture at all (FsrTaa): a still prop on the other eye's rows can never pass as a mover.
inline constexpr double eye_motion_other_still_px=3.; // v1158 (was 0.25): the other-eye fit of those props carries the entity jitter-step
// error (docs/.../perf-mode-motion-design.md "Open: entity jitter step"), measured 0.39-0.91 px in v1157 dumps 2-2, 3-0, 3-4; a jitter mix-up
// stays < 2.83 px, the walking guard's other-eye fit >= 15 px
inline constexpr double eye_motion_mover_px=.75; // stale_eye_warp.hpp mover_px
inline constexpr double eye_motion_arm_reach=.5; // metres; Phase 0: every arm/weapon pixel lies nearer, no world pixel between 0.5 and 1 m
// v1178: the engine MV pass subtracts cb_jitterrelativetopreviousframe = 2(view+0x7858 - view+0x7850) (0x3586a0). Alternate mode seeds +0x7858
// with this eye's previous jitter, so that term cancelled the jitter step of static draws only (MV = unjittered displacement) while FSR, DLSS
// and the native TAA dejitter were told the step is in the vectors. Since v1178 the Setup hook sets +0x7858 = +0x7850 after the receipt is
// taken (as paired mode already has it: the term is 0), so every MV is the jittered displacement against the rows its draw used, and
// the previous UV of a pixel through a jittered previous matrix is simply uv - mv: no step here. False = the v1177 convention
// (knob build/legacy-alt-mv-jitter).
inline std::atomic<bool> eye_motion_mv_jittered{true};
namespace eye_motion {
using M4=std::array<double,16>;
inline M4 widen(const std::array<float,16>& m) {M4 o{};for(int i=0;i<16;++i) o[i]=m[i];return o;}
inline M4 mul(const M4& a,const M4& b) {M4 o{};for(int i=0;i<4;++i) for(int j=0;j<4;++j) {double t=0;for(int k=0;k<4;++k) t+=a[i*4+k]*b[k*4+j];o[i*4+j]=t;}return o;}
inline bool inverse(const M4& m,M4& out) {
    double a[4][8];
    for(int i=0;i<4;++i) for(int j=0;j<4;++j) {a[i][j]=m[i*4+j];a[i][4+j]=i==j?1.0:0.0;}
    for(int c=0;c<4;++c) {
        int p=c;for(int i=c+1;i<4;++i) if(std::abs(a[i][c])>std::abs(a[p][c])) p=i;
        if(!(std::abs(a[p][c])>1e-30)) return false;
        for(int j=0;j<8;++j) std::swap(a[p][j],a[c][j]);
        const double d=a[c][c];for(int j=0;j<8;++j) a[c][j]/=d;
        for(int i=0;i<4;++i) if(i!=c) {const double k=a[i][c];for(int j=0;j<8;++j) a[i][j]-=k*a[c][j];}
    }
    for(int i=0;i<4;++i) for(int j=0;j<4;++j) out[i*4+j]=a[i][4+j];
    return true;
}
inline void narrow(const M4& m,float* out) {for(int i=0;i<16;++i) out[i]=static_cast<float>(m[i]);}
} // namespace eye_motion
// From the eye's receipt snapshots (current render, this eye's previous render). False: no motion (singular or non-finite).
// other: the other eye's render one Present before `cur` (its receipt's current snapshot), or null to skip that test.
inline bool eye_motion_constants(const alternate_taa::Snapshot& cur,const alternate_taa::Snapshot& prev,unsigned width,unsigned height,EyeMotionConstants& out,
                                 const alternate_taa::Snapshot* other=nullptr,bool mv_jittered=eye_motion_mv_jittered.load(std::memory_order_relaxed)) {
    using namespace eye_motion;
    out={};
    const float keep=mv_jittered?0.f:1.f; // v1178: jittered MVs carry the step already
    const M4 vc=widen(cur.matrices[2]),vp=widen(prev.matrices[2]);
    M4 ic{},ip{};
    if(!width || !height || !inverse(vc,ic) || !inverse(vp,ip)) return false;
    M4 t{1,0,0,-double(cur.position[0]),0,1,0,-double(cur.position[1]),0,0,1,-double(cur.position[2]),0,0,0,1};
    narrow(mul(vp,ic),out.r);narrow(mul(t,ic),out.now);narrow(mul(t,ip),out.then);
    out.size_step[0]=static_cast<float>(width);out.size_step[1]=static_cast<float>(height);
    out.size_step[2]=keep*(prev.jitter[0]-cur.jitter[0]);out.size_step[3]=keep*(cur.jitter[1]-prev.jitter[1]);
    out.proj[0]=cur.matrices[0][10];out.proj[1]=cur.matrices[0][11];out.proj[2]=static_cast<float>(eye_motion_arm_reach);out.proj[3]=1;
    out.mv_scale[0]=out.mv_scale[1]=1;
    if(other) {
        narrow(mul(widen(other->matrices[2]),ic),out.other);
        out.other_step[0]=keep*(other->jitter[0]-cur.jitter[0]);out.other_step[1]=keep*(cur.jitter[1]-other->jitter[1]);
        out.other_step[2]=static_cast<float>(eye_motion_other_still_px);out.other_step[3]=static_cast<float>(eye_motion_mover_px);
        bool ok=std::isfinite(out.other_step[0]) && std::isfinite(out.other_step[1]);
        for(float v:out.other) ok=ok && std::isfinite(v);
        if(!ok) {std::memset(out.other,0,sizeof out.other);std::memset(out.other_step,0,sizeof out.other_step);}
    }
    for(float v:out.r) if(!std::isfinite(v)) return false;
    for(float v:out.now) if(!std::isfinite(v)) return false;
    for(float v:out.then) if(!std::isfinite(v)) return false;
    return std::isfinite(out.size_step[2]) && std::isfinite(out.size_step[3]) && out.proj[1]>0;
}
class EyeMotionPass {
    Microsoft::WRL::ComPtr<ID3D11ComputeShader> cs_;Microsoft::WRL::ComPtr<ID3D11Buffer> cb_;
public:
    static constexpr char shader_source[]=
        "Texture2D<float2> mvTex:register(t0);Texture2D<float> depthTex:register(t1);Texture2D<uint> rigTex:register(t2);RWTexture2D<float4> outTex:register(u0);"
        "cbuffer C:register(b0){float4 R[4];float4 Now[4];float4 Then[4];float4 sizeStep;float4 proj;float4 mvScale;float4 Other[4];float4 otherStep;};"
        "float4 Apply(float4 m0,float4 m1,float4 m2,float4 m3,float4 v){return float4(dot(m0,v),dot(m1,v),dot(m2,v),dot(m3,v));}"
        "[numthreads(8,8,1)] void main(uint3 i:SV_DispatchThreadID){"
        "if(any(float2(i.xy)>=sizeStep.xy)) return;"
        "const float d=depthTex.Load(int3(i.xy,0));float4 o=float4(0,0,0,0);"
        "const uint rig=mvScale.z>0?rigTex.Load(int3(i.xy,0)):0;"
        "if(proj.w>0 && d>1e-7 && (mvScale.z>0?(rig==0 || (mvScale.w>0 && rig==255)):proj.y/(d+proj.x)>=proj.z)){"
        "const float2 uv=(float2(i.xy)+0.5)/sizeStep.xy;"
        "const float2 puv=uv-mvTex.Load(int3(i.xy,0))*mvScale.xy-sizeStep.zw;"
        "const float4 n=float4(uv.x*2-1,1-uv.y*2,d,1);"
        "const float4 c=Apply(R[0],R[1],R[2],R[3],n);const float4 a=Apply(Now[0],Now[1],Now[2],Now[3],n);"
        // c and a share the homogeneous scale of the unprojected point: c.w/a.w is its distance ahead of the previous camera (m)
        "if(abs(a.w)>1e-12 && c.w/a.w>=0.01){"
        "const float2 pred=float2(c.x/c.w*0.5+0.5,0.5-c.y/c.w*0.5);"
        "const float4 b=Apply(Then[0],Then[1],Then[2],Then[3],float4(puv.x*2-1,1-puv.y*2,c.z/c.w,1));"
        "if(abs(a.w)>1e-12 && abs(b.w)>1e-12 && all(abs(puv-0.5)<1.5) && all(abs(pred-0.5)<1.5)){"
        "const float3 d=a.xyz/a.w-b.xyz/b.w;const float px=length((puv-pred)*sizeStep.xy);"
        "if(all(isfinite(d)) && isfinite(px) && length(d)<100) o=float4(d,min(px,4096));"
        // v1154: a mover by its own previous that the other eye's render one Present earlier explains is still (0: no motion)
        "if(otherStep.w>0 && o.w>=otherStep.w){"
        "const float4 e=Apply(Other[0],Other[1],Other[2],Other[3],n);"
        "if(e.w/a.w>=0.01){"
        "const float2 q=uv-mvTex.Load(int3(i.xy,0))*mvScale.xy-otherStep.xy;"
        "const float2 r=float2(e.x/e.w*0.5+0.5,0.5-e.y/e.w*0.5);"
        "if(length((q-r)*sizeStep.xy)<otherStep.z) o=float4(0,0,0,0);"
        "}}"
        "}}}"
        "outTex[i.xy]=o;}";
    bool prepare(ID3D11Device* device) noexcept {
        if(cs_) return true;
        if(!device) return false;
        Microsoft::WRL::ComPtr<ID3DBlob> code;
        if(FAILED(D3DCompile(shader_source,sizeof shader_source-1,nullptr,nullptr,nullptr,"main","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,nullptr)) ||
           FAILED(device->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&cs_))) {cs_.Reset();return false;}
        D3D11_BUFFER_DESC d{};d.ByteWidth=sizeof(EyeMotionConstants);d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        if(FAILED(device->CreateBuffer(&d,nullptr,&cb_))) {cs_.Reset();return false;}
        return true;
    }
    // Records the pass on `c` (compute state is the caller's to save and restore). k.proj[3]==0 writes zeros. v1162: k.mv_scale[2] > 0 needs `rig`.
    // False: nothing recorded (no shader, no inputs).
    bool dispatch(ID3D11DeviceContext* c,ID3D11ShaderResourceView* motion,ID3D11ShaderResourceView* depth,ID3D11UnorderedAccessView* out,
                  const EyeMotionConstants& k,ID3D11ShaderResourceView* rig=nullptr) noexcept {
        if(!cs_ || !cb_ || !c || !out || !motion || !depth || (k.mv_scale[2]>0 && !rig)) return false;
        c->UpdateSubresource(cb_.Get(),0,nullptr,&k,0,0);
        ID3D11ShaderResourceView* in[3]{motion,depth,rig};ID3D11Buffer* cb=cb_.Get();
        c->CSSetShader(cs_.Get(),nullptr,0);c->CSSetShaderResources(0,3,in);c->CSSetUnorderedAccessViews(0,1,&out,nullptr);c->CSSetConstantBuffers(0,1,&cb);
        c->Dispatch((static_cast<UINT>(k.size_step[0])+7)/8,(static_cast<UINT>(k.size_step[1])+7)/8,1);
        ID3D11ShaderResourceView* none[3]{};ID3D11UnorderedAccessView* no_uav{};ID3D11Buffer* no_cb{};
        c->CSSetShaderResources(0,3,none);c->CSSetUnorderedAccessViews(0,1,&no_uav,nullptr);c->CSSetConstantBuffers(0,1,&no_cb);
        return true;
    }
};
} // namespace d2vr
