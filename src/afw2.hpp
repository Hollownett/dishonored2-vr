#pragma once
// v1177 AFW v2 (coordinator 2026-11-11, docs/AER-REFERENCE-RESEARCH-2026-11-11.md A.1-A.6, C.1-C.3): the rebuilt eye as a FORWARD splat of
// this eye's own kept frame. Per kept texel a 3D motion from the kept tick to the target tick (none for the static world; movers and the
// player's rig confirmed by chained look-ups in the fresh eye), every kept texel splatted into the target camera with a depth test
// (InterlockedMax on 1/z: the nearer surface wins at every edge), holes filled from the FARTHEST neighbour (the background is stretched,
// never the foreground), and each output pixel resolved from the kept eye's own colour at the point its target depth puts on its ray.
// The fresh eye supplies depth, class and motion; its colour shows only for a moving hand no kept texel explains (rig_from_fresh, v1173).
// Included by stale_eye_warp.hpp after StaleLayers / StaleDepth / StaleMotion / StaleRig; run from StaleEyeWarp::apply inside its
// ContextState scope (the caller's state is swapped out and back whole).
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string>

namespace d2vr {
// v1187: a texel moves by its own vector from this object motion (render px over the eye's two ticks): entity vectors are exact since
// v1178 (still props p99 0.05 px), Metro's kMovingPixels
inline constexpr double afw2_moving_px=.25;
// A camera-to-camera rigid map in camera metres (x right, y up, z forward): P_from = r * P_to + t.
struct Afw2Rigid {std::array<double,9> r{1,0,0,0,1,0,0,0,1};std::array<double,3> t{};};
inline Afw2Rigid afw2_mul(const Afw2Rigid& a,const Afw2Rigid& b) { // a after b: P = a(b(P))
    Afw2Rigid c{};c.r={};
    for(int i=0;i<3;++i) {for(int j=0;j<3;++j) for(int k=0;k<3;++k) c.r[i*3+j]+=a.r[i*3+k]*b.r[k*3+j];
                          c.t[i]=a.t[i];for(int k=0;k<3;++k) c.t[i]+=a.r[i*3+k]*b.t[k];}
    return c;
}
inline Afw2Rigid afw2_inverse(const Afw2Rigid& a) {
    Afw2Rigid c{};
    for(int i=0;i<3;++i) for(int j=0;j<3;++j) c.r[i*3+j]=a.r[j*3+i];
    for(int i=0;i<3;++i) {c.t[i]=0;for(int k=0;k<3;++k) c.t[i]-=c.r[i*3+k]*a.t[k];}
    return c;
}
inline std::array<double,9> afw2_inv3(const std::array<double,9>& m) {
    const double det=m[0]*(m[4]*m[8]-m[5]*m[7])-m[1]*(m[3]*m[8]-m[5]*m[6])+m[2]*(m[3]*m[7]-m[4]*m[6]);
    return {(m[4]*m[8]-m[5]*m[7])/det,(m[2]*m[7]-m[1]*m[8])/det,(m[1]*m[5]-m[2]*m[4])/det,
            (m[5]*m[6]-m[3]*m[8])/det,(m[0]*m[8]-m[2]*m[6])/det,(m[2]*m[3]-m[0]*m[5])/det,
            (m[3]*m[7]-m[4]*m[6])/det,(m[1]*m[6]-m[0]*m[7])/det,(m[0]*m[4]-m[1]*m[3])/det};
}
// The rigid map of a StaleParallax layer (its `from` image -> its `to` view): world = I_from * m * A_to, shift = I_from * step (OpenXR
// axes), so m = I_from^-1 world A_to^-1 and step = I_from^-1 shift; then flipped to z forward.
inline Afw2Rigid afw2_rigid(const StaleParallax& p,const WarpFov& from,const WarpFov& to) {
    const auto I=afw2_inv3(warp_image_matrix(from));
    const double tl=std::tan(to.left),tr=std::tan(to.right),tu=std::tan(to.up),td=std::tan(to.down);
    const auto A=afw2_inv3({tr-tl,0,tl, 0,td-tu,tu, 0,0,-1});
    std::array<double,9> x{},m{};
    for(int i=0;i<3;++i) for(int j=0;j<3;++j) for(int k=0;k<3;++k) x[i*3+j]+=I[i*3+k]*p.world[k*3+j];
    for(int i=0;i<3;++i) for(int j=0;j<3;++j) for(int k=0;k<3;++k) m[i*3+j]+=x[i*3+k]*A[k*3+j];
    Afw2Rigid r{};
    const double c[3]{1,1,-1};
    for(int i=0;i<3;++i) for(int j=0;j<3;++j) r.r[i*3+j]=c[i]*m[i*3+j]*c[j];
    for(int i=0;i<3;++i) {double s=0;for(int k=0;k<3;++k) s+=I[i*3+k]*p.shift[k];r.t[i]=c[i]*s;}
    return r;
}

class Afw2Pass {
    template<class T> using Ptr=Microsoft::WRL::ComPtr<T>;
    Ptr<ID3D11Device> device_;Ptr<ID3D11DeviceContext> context_;
    enum Stage {motion,repair,splat,origins,crack,fill,fresh_splat,scatter,scatter_write,decide,stages};
    std::array<Ptr<ID3D11ComputeShader>,stages> cs_{};
    Ptr<ID3D11VertexShader> vs_;Ptr<ID3D11PixelShader> ps_;
    Ptr<ID3D11Buffer> constants_;Ptr<ID3D11SamplerState> sampler_;
    Ptr<ID3D11RasterizerState> raster_;Ptr<ID3D11DepthStencilState> depth_;
    struct Buffer {Ptr<ID3D11Buffer> b;Ptr<ID3D11ShaderResourceView> srv;Ptr<ID3D11UnorderedAccessView> uav;};
    std::array<Buffer,2> motion_{},keys_{},landed_{},origins_{},near_{};Buffer fresh_keys_{},holes_{},votes_{},decisions_{},tiles_{},stats_{};Ptr<ID3D11Buffer> fill_args_;Ptr<ID3D11Buffer> holes_read_;
    UINT gw_{},gh_{},fw_{},fh_{};
public:
    static constexpr int profile_stages=10;
private:
    struct Probe {Ptr<ID3D11Query> disjoint;std::array<Ptr<ID3D11Query>,profile_stages+1> ts;Ptr<ID3D11Buffer> stats;bool waiting{};};
    std::array<Probe,3> probes_{};unsigned probe_next_{};
    bool collect_profile(bool wait=false) {
        bool any=false;
        for(auto& p:probes_) if(p.waiting) {
            D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj{};
            if(context_->GetData(p.disjoint.Get(),&dj,sizeof dj,wait?0:D3D11_ASYNC_GETDATA_DONOTFLUSH)!=S_OK) continue;
            std::array<UINT64,profile_stages+1> t{};bool ok=true;
            for(int i=0;i<=profile_stages && ok;++i) ok=context_->GetData(p.ts[i].Get(),&t[i],sizeof(UINT64),wait?0:D3D11_ASYNC_GETDATA_DONOTFLUSH)==S_OK;
            if(!ok) continue;
            p.waiting=false;
            if(!dj.Disjoint && dj.Frequency) {for(int i=0;i<profile_stages;++i) stage_ms[i]=double(t[i+1]-t[i])*1000.0/double(dj.Frequency);++profiled;any=true;}
            D3D11_MAPPED_SUBRESOURCE m{};   // (the pass is done: its stats copy too)
            if(p.stats && SUCCEEDED(context_->Map(p.stats.Get(),0,D3D11_MAP_READ,0,&m))) {for(int i=0;i<4;++i) rig_stats[i]=static_cast<const std::uint32_t*>(m.pData)[i];context_->Unmap(p.stats.Get(),0);}
        }
        return any;
    }
    Ptr<ID3D11Texture2D> output_;Ptr<ID3D11RenderTargetView> output_rtv_;UINT ow_{},oh_{};
    static void checked(HRESULT r,const char* what) {if(FAILED(r)) throw std::runtime_error(std::string("AFW2: ")+what+" ("+std::to_string(r)+")");}
    Buffer buffer(UINT count,UINT stride,bool append=false) {
        Buffer x;D3D11_BUFFER_DESC d{};d.ByteWidth=count*stride;d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;
        d.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;d.StructureByteStride=stride;
        checked(device_->CreateBuffer(&d,nullptr,&x.b),"buffer");
        D3D11_SHADER_RESOURCE_VIEW_DESC s{};s.Format=DXGI_FORMAT_UNKNOWN;s.ViewDimension=D3D11_SRV_DIMENSION_BUFFER;s.Buffer.NumElements=count;
        checked(device_->CreateShaderResourceView(x.b.Get(),&s,&x.srv),"buffer read view");
        D3D11_UNORDERED_ACCESS_VIEW_DESC u{};u.Format=DXGI_FORMAT_UNKNOWN;u.ViewDimension=D3D11_UAV_DIMENSION_BUFFER;u.Buffer.NumElements=count;u.Buffer.Flags=append?D3D11_BUFFER_UAV_FLAG_APPEND:0;
        checked(device_->CreateUnorderedAccessView(x.b.Get(),&u,&x.uav),"buffer write view");
        return x;
    }
    void grids(UINT gw,UINT gh,UINT fw,UINT fh) {
        if(gw!=gw_ || gh!=gh_) {
            for(int i=0;i<2;++i) {motion_[i]=buffer(gw*gh,16);keys_[i]=buffer(gw*gh,4);landed_[i]=buffer(gw*gh,4);origins_[i]=buffer(gw*gh,4);near_[i]=buffer(gw*gh,8);}
            fresh_keys_=buffer(gw*gh,4);decisions_=buffer(gw*gh,8);tiles_=buffer(((gw+7)/8)*((gh+7)/8),4,true);holes_=buffer(1+((gw+7)/8)*((gh+7)/8),4);gw_=gw;gh_=gh;
        }
        fw_=fw;fh_=fh;
    }
    void target(UINT w,UINT h) {
        if(w==ow_ && h==oh_) return;
        D3D11_TEXTURE2D_DESC d{};d.Width=w;d.Height=h;d.MipLevels=1;d.ArraySize=1;d.Format=DXGI_FORMAT_R8G8B8A8_TYPELESS;d.SampleDesc.Count=1;
        d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;d.Usage=D3D11_USAGE_DEFAULT;
        Ptr<ID3D11Texture2D> t;checked(device_->CreateTexture2D(&d,nullptr,&t),"output");
        D3D11_RENDER_TARGET_VIEW_DESC r{};r.Format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;r.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
        Ptr<ID3D11RenderTargetView> v;checked(device_->CreateRenderTargetView(t.Get(),&r,&v),"output view");
        output_=t;output_rtv_=v;ow_=w;oh_=h;
    }
public:
    std::uint64_t passes{};
    // v1191 per-pass GPU time: timestamp queries around each stage, read back a few passes late (never waits). stage_ms: the latest
    // complete measurement, in stage_names order; profile=false: no queries issued.
    static constexpr const char* stage_names[profile_stages]{"scatter","motion","repair","splat","origins","fresh_splat","crack+copies","fill","decide","resolve"};
    bool profile{true};
    std::array<double,profile_stages> stage_ms{};std::uint64_t profiled{};
    // tools: wait for this pass's timestamps (WARP replays)
    bool profile_wait() {for(int n=0;n<200;++n) {if(collect_profile(true)) return true;} return false;}
    // v1197 safety net, from the same late read-back as stage_ms: [0] the rig target 0 none, 1 used, 2 ignored (it disagreed with this eye's
    // rig as a whole); [1] pixels sampled where the used target has the rig (every 16th), [2] of them resolved to the world. A share [2]/[1]
    // above a few percent: the hand path is broken.
    std::array<std::uint32_t,4> rig_stats{};
    double rig_world_share() const {return rig_stats[1]?double(rig_stats[2])/rig_stats[1]:0.0;}
    std::array<std::uint32_t,4> rig_stats_wait() {   // tools and the lab: this pass's, waiting
        D3D11_BUFFER_DESC d{};stats_.b->GetDesc(&d);d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        Ptr<ID3D11Buffer> st;checked(device_->CreateBuffer(&d,nullptr,&st),"stats read-back");context_->CopyResource(st.Get(),stats_.b.Get());
        D3D11_MAPPED_SUBRESOURCE m{};checked(context_->Map(st.Get(),0,D3D11_MAP_READ,0,&m),"stats read");
        std::array<std::uint32_t,4> r{};for(int i=0;i<4;++i) r[i]=static_cast<const std::uint32_t*>(m.pData)[i];
        context_->Unmap(st.Get(),0);return r;
    }
    int rig_order{},rig_debug{};                                // v1190: inside the rig target, 0 this eye's kept rig first, 1 the fresh eye's first (tools)
    explicit Afw2Pass(ID3D11Device* device);
    // Holes left after the fill on the last pass (reads back and waits: tools and the lab only).
    std::uint32_t holes() {
        if(!holes_.b) return 0;
        const D3D11_BOX first{0,0,0,4,1,1};context_->CopySubresourceRegion(holes_read_.Get(),0,0,0,0,holes_.b.Get(),0,&first);
        D3D11_MAPPED_SUBRESOURCE m{};checked(context_->Map(holes_read_.Get(),0,D3D11_MAP_READ,0,&m),"holes read");
        const auto n=*static_cast<const std::uint32_t*>(m.pData);context_->Unmap(holes_read_.Get(),0);return n;
    }
    // The 8x8 tiles of the target grid that held a hole after the splat (the fill runs there only) and all tiles (tools only: waits).
    std::array<std::uint32_t,2> hole_tiles() {
        std::array<std::uint32_t,2> r{};if(!holes_.b) return r;
        D3D11_BUFFER_DESC d{};holes_.b->GetDesc(&d);d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        Ptr<ID3D11Buffer> st;checked(device_->CreateBuffer(&d,nullptr,&st),"tiles read-back");context_->CopyResource(st.Get(),holes_.b.Get());
        D3D11_MAPPED_SUBRESOURCE m{};checked(context_->Map(st.Get(),0,D3D11_MAP_READ,0,&m),"tiles read");
        const auto* v=static_cast<const std::uint32_t*>(m.pData);r[1]=d.ByteWidth/4-1;
        for(std::uint32_t i=1;i<=r[1];++i) r[0]+=v[i]!=0;
        context_->Unmap(st.Get(),0);return r;
    }
    // The look-up votes of the last pass per owner (1 head's rig, 2 left, 3 right): confirmed, refuted (reads back and waits: tools only).
    std::array<std::uint32_t,8> votes() {
        std::array<std::uint32_t,8> v{};if(!votes_.b) return v;
        D3D11_BUFFER_DESC d{};votes_.b->GetDesc(&d);d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        Ptr<ID3D11Buffer> st;checked(device_->CreateBuffer(&d,nullptr,&st),"votes read-back");context_->CopyResource(st.Get(),votes_.b.Get());
        D3D11_MAPPED_SUBRESOURCE m{};checked(context_->Map(st.Get(),0,D3D11_MAP_READ,0,&m),"votes read");
        for(int i=0;i<8;++i) v[i]=static_cast<const std::uint32_t*>(m.pData)[i];
        context_->Unmap(st.Get(),0);return v;
    }
    // kept / fresh: _SRGB read views of the eye images (display size); depth, motion and rig views at render size (motion / rig null: off).
    ID3D11Texture2D* run(const StaleLayers& L,UINT width,UINT height,ID3D11ShaderResourceView* kept,ID3D11ShaderResourceView* kept_depth,UINT kw,UINT kh,
                         ID3D11ShaderResourceView* fresh,ID3D11ShaderResourceView* fresh_depth,UINT fw,UINT fh,
                         ID3D11ShaderResourceView* kept_motion,ID3D11ShaderResourceView* fresh_motion,
                         ID3D11ShaderResourceView* kept_rig,ID3D11ShaderResourceView* fresh_rig,bool rig_target=false);
};

inline Afw2Pass::Afw2Pass(ID3D11Device* device):device_(device) {
    device_->GetImmediateContext(&context_);
    const std::string source=R"HLSL(
cbuffer Afw : register(b0) {
    float4 keptFov; float4 targetFov; float4 freshFov;   // tan left, right, up, down
    float4 grid;                                          // kept depth = target grid (w, h, 1/w, 1/h)
    float4 freshGrid;                                     // fresh depth rectangle (w, h, 1/w, 1/h)
    float4 terms;                                         // kept m10, m11, fresh m10, m11 (ahead = m11 / (d + m10))
    float4 KT[3]; float4 TK[3]; float4 KF[3]; float4 FT[3]; float4 TF[3];
    float4 GC[3]; float4 GL[3]; float4 GR[3];             // carried, left hand, right hand: kept point -> world-equivalent kept point
    float4 palmL; float4 palmR;                           // kept camera metres, w reach (0: none)
    float4 axes[3];                                       // world -> kept camera
    float4 flags;      // x masks, y motion, z rig texels may carry their own motion (mask 255), w mover_px
    float4 parts;      // fresh_part, kept_part, left hand moving, right hand moving (masks and rig_from_fresh)
    float4 layersOn;   // carried, left, right on; still (identical cameras)
    float4 display;    // output w, h, 1/w, 1/h
    float4 validRect;  // kept u, v max, fresh u, v max
    float4 fillStep;   // x step (texels), y last pass, z the camera's sideways step in texels per inverse metre, w the near-mover gate (px)
    float4 rigMode;    // x the rig target (t15) is bound, y colour order inside it (0: this eye's kept rig first, 1: the fresh eye's first)
}
Texture2D keptColour : register(t0); Texture2D<float> keptDepth : register(t1);
Texture2D freshColour : register(t2); Texture2D<float> freshDepth : register(t3);
Texture2D<float4> keptMotion : register(t4); Texture2D<float4> freshMotion : register(t5);
Texture2D<float> keptRig : register(t6); Texture2D<float> freshRig : register(t7);
StructuredBuffer<float4> motionIn : register(t8); StructuredBuffer<uint> keysIn : register(t9);
StructuredBuffer<uint> landedIn : register(t10); StructuredBuffer<uint2> nearIn : register(t14);  /* (the resolve reads it too: a hole the rig left) */ StructuredBuffer<uint> originsIn : register(t11); StructuredBuffer<uint> freshKeysIn : register(t12);
RWStructuredBuffer<float4> motionOut : register(u0); RWStructuredBuffer<uint> keysOut : register(u1);
RWStructuredBuffer<uint> landedOut : register(u2); RWStructuredBuffer<uint2> nearOut : register(u7); RWStructuredBuffer<uint> originsOut : register(u3);
RWStructuredBuffer<uint> freshKeysOut : register(u4); RWStructuredBuffer<uint> holesOut : register(u5);
// per drawn hand (2 left, 3 right): its rig texels' look-ups, [hand*2] confirmed, [hand*2+1] refuted (Motion counts, Splat reads)
RWStructuredBuffer<uint> handVotes : register(u6); StructuredBuffer<uint> handVotesIn : register(t13);
SamplerState linearSampler : register(s0);
// v1190 (engine side, rig_target.hpp): the rig and held items of this tick drawn into THIS eye's target camera: x reverse-Z depth (0 none),
// y class (255 rig, 253 held, 0 none); render-sized, the target grid's texels
Texture2D<float2> rigTarget : register(t15);
// v1197: the resolve's decisions per target texel (Decide writes, ResolvePS reads)
StructuredBuffer<uint2> decisionsIn : register(t16); RWStructuredBuffer<uint2> decisionsOut : register(u0);
// v1197: the 8x8 tiles holding a hole after the crack (Crack appends, Fill runs over them)
AppendStructuredBuffer<uint> tilesOut : register(u4);
// v1197 cost: the crack writes landed and near into both fill buffers (no copy; it reads neither)
RWStructuredBuffer<uint> landedOut0 : register(u0); RWStructuredBuffer<uint2> nearOut0 : register(u6); StructuredBuffer<uint> tilesIn : register(t12);
// motion flags (w)
static const uint kConfirmed=1,kRig=2,kMoving=4,kDrop=8,kSky=16,kOutside=128,kHeld=256;
// key low bits
static const uint kKeyRig=1,kKeyMoving=2,kKeyFlags=7,kSkyKey=8;
static const uint kFilled=0x80000000u,kSeed=0x40000000u;
// (rows passed one by one: fxc miscompiled Apply(TK,Apply(FT,p)) with cbuffer arrays as parameters, using FT twice)
float3 Apply3(float4 a,float4 b,float4 c,float3 p) {return float3(dot(a.xyz,p)+a.w,dot(b.xyz,p)+b.w,dot(c.xyz,p)+c.w);}
float3 Turn3(float4 a,float4 b,float4 c,float3 p) {return float3(dot(a.xyz,p),dot(b.xyz,p),dot(c.xyz,p));}
#define Apply(M,p) Apply3(M[0],M[1],M[2],p)
#define Turn(M,p) Turn3(M[0],M[1],M[2],p)
float3 Ray(float4 fov,float2 uv) {return float3(lerp(fov.x,fov.y,uv.x),lerp(fov.z,fov.w,uv.y),1);}
float2 Project(float4 fov,float3 p) {return float2((p.x/p.z-fov.x)/(fov.y-fov.x),(p.y/p.z-fov.z)/(fov.w-fov.z));}
float Tol(float z) {return 0.01+0.03*z;}   // the same surface: 3 % and 1 cm (Metro's kDepthTolerance)
float KeptAhead(int2 i) {const float d=keptDepth.Load(int3(i,0));return d<=1e-7?0:terms.y/(d+terms.x);}
float FreshAhead(int2 i) {const float d=freshDepth.Load(int3(i,0));return d<=1e-7?0:terms.w/(d+terms.z);}
bool KeptRigAt(int2 i) {return flags.x>0 && keptRig.Load(int3(i,0))>0;}
bool FreshRigAt(int2 i) {return flags.x>0 && freshRig.Load(int3(i,0))>0;}
// v1177 (engine 4bd8ad2): mask 255 = rig with valid own motion in the motion texture, 254 = rig without
bool KeptRigMoves(int2 i) {return flags.z>0 && keptRig.Load(int3(i,0))>254.5/255;}
bool InKept(float2 uv) {return all(uv>=0) && all(uv<=validRect.xy);}
bool InFresh(float2 uv) {return all(uv>=0) && all(uv<=validRect.zw);}
uint Index(int2 i) {return uint(i.y)*uint(grid.x)+uint(i.x);}
float3 KeptStep(float3 d,float part) {return part*float3(dot(axes[0].xyz,d),dot(axes[1].xyz,d),dot(axes[2].xyz,d));}
float3 KeptPointAt(int2 i,out float a) {a=KeptAhead(i);return Ray(keptFov,(float2(i)+0.5)*grid.zw)*a;}
// the drawn hand that owns rig point p (kept metres): 2 left, 3 right, 1 the head's (carried); v1159/v1164 rules. Without the masks the
// v811 palm sphere (22 cm) gives a hand or nothing (0).
int Owner(float3 p,bool rig) {
    const float dl=palmL.w>0?distance(p,palmL.xyz):1e9,dr=palmR.w>0?distance(p,palmR.xyz):1e9;
    if(flags.x>0) {
        if(!rig) return 0;
        if(min(dl,dr)>=min(0.45,length(p))) return 1;
        return dl<=dr?2:3;
    }
    if(dl<=dr && dl<palmL.w) return 2;
    if(dr<dl && dr<palmR.w) return 3;
    return 0;
}
)HLSL" R"HLSL(// Look: kept point p moved by d, seen by the fresh eye. 1 the same class and surface there (any of the 2x2 texels around: a grazing
// silhouette lands a texel off), 0 something nearer hides it, -1 something farther or of another class (not there), 2 outside its view.
// fm: the fresh motion texel there.
int Look(float3 p,float3 d,bool rig,out float4 fm) {
    fm=float4(0,0,0,0);
    const float3 f=Apply(KF,p+d);
    if(f.z<=1e-3) return 2;
    const float2 uv=Project(freshFov,f);
    if(!InFresh(uv)) return 2;
    const int2 c=clamp(int2(floor(uv*freshGrid.xy-0.5)),int2(0,0),int2(freshGrid.xy)-2);
    const float tol=Tol(f.z);bool nearer=false;
    [unroll] for(int n=0;n<4;++n) {
        const int2 i=c+int2(n&1,n>>1);
        const float a=FreshAhead(i);
        if(a>0 && abs(a-f.z)<tol && (flags.x<=0 || FreshRigAt(i)==rig)) {
            if(flags.y>0 && (!rig || (flags.z>0 && freshRig.Load(int3(i,0))>254.5/255))) fm=freshMotion.Load(int3(i,0));
            return 1;
        }
        nearer=nearer || (a>0 && a<f.z-tol);
    }
    return nearer?0:-1;
}
// Three look-ups from d (Metro's Chain): each confirmed look reads the fresh eye's motion there (its point now came from fresh_part of
// it back at the kept tick) as the next d; confirmed when the last lands on the same surface. refine false: one look, d unchanged.
int Chain(float3 p,inout float3 d,bool rig,bool refine) {
    float4 fm;int r=Look(p,d,rig,fm);
    if(!refine) return r;
    [unroll] for(int n=0;n<2;++n) {
        if(r!=1) return r;
        d=KeptStep(fm.xyz,parts.x);
        r=Look(p,d,rig,fm);
    }
    return r;
}
float3 LayerStep(int owner,float3 p) {
    if(owner==1) return layersOn.x>0?Apply(GC,p)-p:float3(0,0,0);
    if(owner==2) return layersOn.y>0?Apply(GL,p)-p:float3(0,0,0);
    return layersOn.z>0?Apply(GR,p)-p:float3(0,0,0);
}
// Scatter (Metro's warp_scatter): a mover the fresh eye sees (its own vector) was at P_now - fresh_part * its motion at the kept tick;
// where the kept depth has the same surface of the world there, it hands that motion to that kept texel (nearest wins); ScatterWrite
// stores the winners' motion (w 1) for Motion, which still confirms it by the chain. For kept texels whose own vector is missing or still.
bool SameSurface(float a,float b) {return a>0 && b>0 && abs(a-b)<Tol(min(a,b));}
bool ScatterOf(int2 i,out uint at,out uint key,out float3 d) {
    at=0;key=0;d=float3(0,0,0);
    const float2 uv=(float2(i)+0.5)*freshGrid.zw;
    if(!InFresh(uv) || FreshRigAt(i)) return false;
    const float4 m=freshMotion.Load(int3(i,0));
    if(m.w<flags.w) return false;
    const float a=FreshAhead(i);if(a<=0) return false;
    d=KeptStep(m.xyz,parts.x);
    const float3 then=Apply(TK,Apply(FT,Ray(freshFov,uv)*a))-d;
    if(then.z<=1e-3) return false;
    const float2 kv=Project(keptFov,then);
    if(!InKept(kv)) return false;
    const int2 k=int2(kv*grid.xy);
    const float ka=KeptAhead(k);
    if(!(ka>0) || abs(ka-then.z)>=Tol(then.z) || KeptRigAt(k)) return false;
    at=Index(k);key=asuint(1/then.z)|1;
    return true;
}
[numthreads(8,8,1)] void Scatter(uint3 id : SV_DispatchThreadID) {
    if(any(id.xy>=uint2(freshGrid.xy))) return;
    uint at,key;float3 d;if(ScatterOf(int2(id.xy),at,key,d)) InterlockedMax(keysOut[at],key);
}
[numthreads(8,8,1)] void ScatterWrite(uint3 id : SV_DispatchThreadID) {
    if(any(id.xy>=uint2(freshGrid.xy))) return;
    uint at,key;float3 d;
    if(!ScatterOf(int2(id.xy),at,key,d) || keysIn[at]!=key) return;
    // the winner seeds its kept texel and the texels of that surface within 2 (Metro's kSeedRadius: a mover moves as one, and the fresh
    // texel grid does not land on every kept texel of it). Neighbours a winner holds keep their own.
    const int2 c=int2(at%uint(grid.x),at/uint(grid.x));const float ca=KeptAhead(c);
    for(int y=-2;y<=2;++y) for(int x=-2;x<=2;++x) {
        const int2 j=c+int2(x,y);
        if(any(j<0) || any(j>=int2(grid.xy)) || KeptRigAt(j) || !SameSurface(KeptAhead(j),ca)) continue;
        if(x!=0 || y!=0) {if(keysIn[Index(j)]!=0) continue;}
        motionOut[Index(j)]=float4(d,1);
    }
}
// a fresh mover (its own vector) within 8 texels of where kept point p lies in the fresh image (9 samples 4 texels apart)
bool FreshMoverNear(float3 p) {
    const float3 f=Apply(KF,p);if(f.z<=1e-3) return false;
    const int2 c=int2(Project(freshFov,f)*freshGrid.xy);
    [unroll] for(int n=0;n<9;++n) {
        const int2 j=clamp(c+(int2(n%3,n/3)-1)*4,int2(0,0),int2(freshGrid.xy)-1);
        if(freshMotion.Load(int3(j,0)).w>=fillStep.w) return true;
    }
    return false;
}
// v1197: the target's texel i (every 4th) seen by the fresh eye at its depth: on the fresh eye's rig, or not (TargetOn)
void TargetVote(int2 i) {
    if(rigMode.x<=0 || any((i&3)!=0)) return;
    const float2 r=rigTarget.Load(int3(i,0));if(!(r.x>1e-7) || !(r.y>0)) return;
    const float z=terms.y/(r.x+terms.x);
    const float3 f=Apply(TF,Ray(targetFov,(float2(i)+0.5)*grid.zw)*z);if(f.z<=1e-3) return;
    const float2 uv=Project(freshFov,f);if(!InFresh(uv)) return;
    const int2 c=clamp(int2(floor(uv*freshGrid.xy-0.5)),int2(0,0),int2(freshGrid.xy)-2);
    bool on=false,hidden=false;
    [unroll] for(int n=0;n<4;++n) {
        const int2 j=c+int2(n&1,n>>1);const float a=FreshAhead(j);
        on=on || (FreshRigAt(j) && a>0 && abs(a-f.z)<Tol(f.z));
        hidden=hidden || (a>0 && a<f.z-Tol(f.z) && !FreshRigAt(j));
    }
    if(on || !hidden) InterlockedAdd(handVotes[on?26:27],1);
}
// v1190: the rig target has rig at target texel i (Motion's outline votes; RigTargetAt below gives the depth)
bool RigTargetHas(int2 i) {
    if(rigMode.x<=0 || any(i<0) || any(i>=int2(grid.xy))) return false;
    const float2 r=rigTarget.Load(int3(i,0));return r.x>1e-7 && r.y>0;
}
// kept rig texel i (depth a) lies on the rig's own outline: a texel 2 away is not rig and farther (not a scene occluder's edge)
bool KeptRigOutline(int2 i,float a) {
    [unroll] for(int n=0;n<8;++n) {
        float2 dir;sincos(n*0.7853982,dir.y,dir.x);const int2 j=clamp(i+int2(round(dir*2)),int2(0,0),int2(grid.xy)-1);
        const float b=KeptAhead(j);
        if(!KeptRigAt(j) && (b<=0 || b>a+Tol(a))) return true;
    }
    return false;
}
// Motion: per kept texel the displacement d (kept metres) from the kept tick to the target tick, before the world (camera) map.
[numthreads(8,8,1)] void Motion(uint3 id : SV_DispatchThreadID) {
    if(any(id.xy>=uint2(grid.xy))) return;
    const int2 i=int2(id.xy);float a;const float3 p=KeptPointAt(i,a);
    TargetVote(i);
    float3 d=float3(0,0,0);uint w=0;
    if(a<=0) {motionOut[Index(i)]=float4(0,0,0,kSky|kConfirmed);return;}
    const bool rig=KeptRigAt(i);
    const int owner=Owner(p,rig);
    const float4 own=flags.y>0?keptMotion.Load(int3(i,0)):float4(0,0,0,0);
    if(owner==0) {
        // the world: stands unless its own vector moves it (a mover); then confirmed by the chain, else it goes on as it moved
        if(own.w>=flags.w) {
            d=KeptStep(own.xyz,parts.y);
            float3 c=d;
            if(Chain(p,c,false,flags.y>0)==1) {d=c;w|=kConfirmed;}
            w|=kMoving;
        } else {
            // its own vector says still: motion the fresh eye handed over to it (Scatter), confirmed by the chain
            w|=kConfirmed;
            const float4 sm=flags.y>0?motionIn[Index(i)]:float4(0,0,0,0);
            if(sm.w>0.5) {float3 c=sm.xyz;if(Chain(p,c,false,true)==1) {d=c;w|=kMoving;}}
            // a near thing that moved with no usable vector (a held prop outside the rig mask: its engine motion is ~300 px off, v1152):
            // the fresh eye no longer sees it here while it shows a mover within 8 texels: it stands down for the fresh eye's (FreshSplat)
            else if(a<1.5 && flags.y>0 && rigMode.x<=0) {float4 fm;if(Look(p,float3(0,0,0),false,fm)==-1 && FreshMoverNear(p)) w|=kDrop;}
        }
    } else {
        w|=kRig;
        const bool moving=owner==2?parts.z>0:owner==3?parts.w>0:false;
        int r=-2;
        if(KeptRigMoves(i)) {d=KeptStep(own.xyz,parts.y);float3 c=d;r=Chain(p,c,true,true);if(r==1) d=c;}  // (a) its own rig motion
        if(r!=1) {                                                                                     // (b) its owner's rigid motion
            const float3 l=LayerStep(owner,p);float4 fm;const int s=Look(p,l,true,fm);
            if(s==1 || r==-2) {d=l;r=s;}
        }
        const bool any_moving=parts.z>0 || parts.w>0;   // the head's rig (owner 1: a forearm nearer the eye than its palm) votes while a hand moves too
        if((owner>=2?moving:any_moving) && (r==1 || r==-1)) InterlockedAdd(handVotes[owner*2+(r==1?0:1)],1);
        w|=uint(owner)<<5;                          // the owner (bits 5-6): Splat drops a moving hand its look-ups refute as a whole
        if(flags.x>0 && keptRig.Load(int3(i,0))<253.5/255) w|=kHeld;   // v1191: a held item (253) votes and stands down apart from the arm
        // v1190 outline votes: with the rig target, the rig's own outline moved by its motion must land on the target's outline (within 2
        // texels; 3 for a sword's thin blade): a flat hand confirms any wrong shift texel by texel, its outline does not (an exact silhouette, same grid, same tick)
        if(rigMode.x>0 && KeptRigOutline(i,a)) {
            const float3 t=Apply(KT,p+d);bool on=false;
            if(t.z>1e-3) {
                const int2 c=int2(Project(targetFov,t)*grid.xy);
                if(RigTargetHas(c)) [unroll] for(int n=0;n<8;++n) {float2 dir;sincos(n*0.7853982,dir.y,dir.x);on=on || !RigTargetHas(c+int2(round(dir*3)));}
                else [unroll] for(int n=0;n<8;++n) {float2 dir;sincos(n*0.7853982,dir.y,dir.x);on=on || RigTargetHas(c+int2(round(dir*3)));}
            }
            const bool held=keptRig.Load(int3(i,0))<253.5/255;
            InterlockedAdd(handVotes[(held?18:10)+owner*2+(on?0:1)],1);
        }
        if(r==1) w|=kConfirmed;
        if(r==2) w|=kOutside;
        if(r==2 && moving) d=LayerStep(1,p);   // v1174: a moving hand outside the fresh view shows in place (moves with the head)
        if(dot(d,d)>0) w|=kMoving;
    }
    if(dot(d,d)>0 && !(w&kDrop)) {const float3 m=p+d;if(m.z>1e-3) InterlockedMax(handVotes[8],uint(length((Project(keptFov,m)-Project(keptFov,p))*grid.xy))+1);}
    motionOut[Index(i)]=float4(d,w);
}
)HLSL" R"HLSL(// Repair (Metro's warp_repair): an unconfirmed texel takes the nearest confirmed motion of its class and surface within 4 texels (else
// keeps its own); a confirmed moving texel outvoted by its 5x5 same-surface neighbours (>= 3 voters, fewer than half within a texel of
// it) takes the motion most of them agree with.
[numthreads(8,8,1)] void Repair(uint3 id : SV_DispatchThreadID) {
    if(any(id.xy>=uint2(grid.xy))) return;
    const int2 i=int2(id.xy);const float4 m=motionIn[Index(i)];const uint w=uint(m.w);
    float4 o=m;
    if(!(w&(kSky|kDrop))) {
        const float a=KeptAhead(i);
        const float px=a*abs(keptFov.y-keptFov.x)*grid.z;   // a texel at this depth, metres
        if(!(w&kConfirmed)) {
            bool found=false;
            [loop] for(int r=1;r<=4 && !found;++r) {
                float best=1e9;
                for(int y=-r;y<=r;++y) for(int x=-r;x<=r;++x) {
                    if(max(abs(x),abs(y))!=r) continue;                       // the ring at Chebyshev distance r
                    const int2 j=i+int2(x,y);
                    if(any(j<0) || any(j>=int2(grid.xy)) || x*x+y*y>=best) continue;
                    const float4 n=motionIn[Index(j)];const uint nw=uint(n.w);
                    if((nw&(kConfirmed|kSky|kDrop))!=kConfirmed || (nw&kRig)!=(w&kRig) || !SameSurface(KeptAhead(j),a)) continue;
                    best=x*x+y*y;found=true;o=float4(n.xyz,((w|kConfirmed)&~kMoving)|(dot(n.xyz,n.xyz)>0?kMoving:0));
                }
            }
        } else if(w&kMoving) {
            int voters=0,agree=0;
            for(int y=-2;y<=2;++y) for(int x=-2;x<=2;++x) if(x!=0 || y!=0) {
                const int2 j=i+int2(x,y);
                if(any(j<0) || any(j>=int2(grid.xy))) continue;
                const float4 n=motionIn[Index(j)];const uint nw=uint(n.w);
                if((nw&(kConfirmed|kSky|kDrop))!=kConfirmed || (nw&kRig)!=(w&kRig) || !SameSurface(KeptAhead(j),a)) continue;
                ++voters;agree+=distance(n.xyz,m.xyz)<px;
            }
            if(voters>=3 && agree*2<voters) {                                 // outvoted: the motion most of them agree with
                float3 pick=m.xyz;int pick_n=-1;
                for(int y=-2;y<=2;++y) for(int x=-2;x<=2;++x) if(x!=0 || y!=0) {
                    const int2 j=i+int2(x,y);
                    if(any(j<0) || any(j>=int2(grid.xy))) continue;
                    const float4 n=motionIn[Index(j)];const uint nw=uint(n.w);
                    if((nw&(kConfirmed|kSky|kDrop))!=kConfirmed || (nw&kRig)!=(w&kRig) || !SameSurface(KeptAhead(j),a)) continue;
                    int count=0;
                    for(int yy=-2;yy<=2;++yy) for(int xx=-2;xx<=2;++xx) {
                        const int2 k=i+int2(xx,yy);
                        if(any(k<0) || any(k>=int2(grid.xy))) continue;
                        const float4 q=motionIn[Index(k)];
                        count+=(uint(q.w)&(kConfirmed|kSky|kDrop))==kConfirmed && distance(q.xyz,n.xyz)<px;
                    }
                    if(count>pick_n) {pick_n=count;pick=n.xyz;}
                }
                o.xyz=pick;
            }
        }
    }
    motionOut[Index(i)]=o;
}
// v1197 (in-game street 4-9, room 3/4/6/10/12/14/15: the engine's rig target drawn ~290 texels off, as from a camera one IPD beyond the
// fresh eye; with it trusted, every kept rig texel outside it was dropped and its pixels shown as the world: both hands striped floor): the
// target is used only while the fresh eye confirms it: of its texels (every 4th across and down) the fresh eye sees (not hidden behind
// something nearer there), at least half land on the fresh eye's rig at their depth (TargetVote in Motion: [26] on, [27] off). Not this
// eye's rig moved by its motion: that is what the target corrects (lab R, a grip that says still). Otherwise the pass runs as without one.
bool TargetOn() {
    if(rigMode.x<=0) return false;
    const uint on=handVotesIn[26],off=handVotesIn[27];
    return on+off<32 || on>=off;
}
// rig group `owner` (1 the head's, 2 left, 3 right) moved some other way than its motions say: while its hand moves, its look-ups refute more
// than a tenth of its texels (a flat hand confirms any shift within its own outline texel by texel, so only a clear majority counts)
bool StandsDown(uint owner,bool held=false) {
    if(owner<1 || owner>3) return false;
    // v1190: with the rig target, its outline votes decide, moving or not (a hand whose grip stood still while the drawn hand moved): a
    // half of its outline off the target's stands it down (a third stood a sword hand down that had moved 3.6 mm: swing 8)
    if(TargetOn()) {const uint b=(held?18:10)+owner*2,on=handVotesIn[b],off=handVotesIn[b+1];return on+off>=16 && off*2>on+off;}
    const bool moving=owner==1?parts.z>0 || parts.w>0:owner==2?parts.z>0:parts.w>0;
    return moving && handVotesIn[owner*2+1]*10>handVotesIn[owner*2]+handVotesIn[owner*2+1];
}
// The key a kept texel writes into the target grid and the target texels it covers: the texel's square (its depth and motion at its four
// corners) projected into the target, every target texel whose centre lies in its bounds (half open: a still frame covers exactly its own
// texel; a magnified view leaves no seams). false: no source (the band, dropped, behind the target camera, outside the target).
// v1187 (v1183 street 11 and 21: a beam 16 cm from the eye and a sleeve seen edge-on came out as dots): corner n of kept texel i (its depth
// a) at the depth its surface has there: the mean of the four texels around the corner that lie on this surface (15 % + 2 cm), so a slanted
// surface's squares meet edge to edge after the move instead of shrinking to their centres' depth. (A local float[4] of these, indexed in
// the unrolled projection loop, miscompiled under fxc: every splat moved; computed per corner instead.)
float CornerAhead(int2 i,int n,float a) {
    // (v1191: on a flat stretch, the 2 axis neighbours toward this corner within 1 %, the centre's depth: 2 loads instead of 4)
    const int2 o=int2(n&1,n>>1)*2-1;
    const float bx=KeptAhead(clamp(i+int2(o.x,0),int2(0,0),int2(grid.xy)-1)),by=KeptAhead(clamp(i+int2(0,o.y),int2(0,0),int2(grid.xy)-1));
    if(abs(bx-a)<0.01*a && abs(by-a)<0.01*a) return a+0.25*(bx-a)+0.25*(by-a);
    float sum=0,cnt=0;
    [unroll] for(int k=0;k<4;++k) {
        const float b=KeptAhead(clamp(i+int2(n&1,n>>1)-int2(k&1,k>>1),int2(0,0),int2(grid.xy)-1));
        if(b>0 && abs(b-a)<0.02+0.15*a) {sum+=b;cnt+=1;}
    }
    return cnt>0?sum/cnt:a;
}
bool SplatOf(int2 i,out uint key,out int2 lo,out int2 hi) {
    key=0;lo=int2(0,0);hi=int2(-1,-1);
    const float2 uv=(float2(i)+0.5)*grid.zw;
    if(!InKept(uv)) return false;                                   // v1167: the eye image's band is no source
    const float4 m=motionIn[Index(i)];const uint w=uint(m.w);
    if(w&kDrop) return false;
    // a moving hand whose look-ups refute more than a tenth of its texels moved some other way than its layer says (a flat hand confirms
    // any shift within its own outline texel by texel): the whole hand stands down for the fresh eye's (rig_from_fresh). Known gap (v1165
    // door 6): a large smooth forearm confirms texel by texel under a wrong grip motion (1 % refuted); an outline-only vote refuted every
    // hand (render-sized outlines); the real fix is the rig's own motion (mask 255) or the hand fit as a correction.
    if((w&kRig) && !(w&kOutside) && !TargetOn() && StandsDown((w>>5)&3)) return false;   // (v1187: what the fresh eye cannot see stays this eye's own)
    const bool sky=(w&kSky)!=0;
    float a=1;if(!sky) a=KeptAhead(i);
    float2 bmin=1e9,bmax=-1e9;float zc=0;
    [unroll] for(int n=0;n<5;++n) {
        const float2 c=n<4?(float2(i)+float2(n&1,n>>1))*grid.zw:uv;
        const float3 t=sky?Turn(KT,Ray(keptFov,c)):Apply(KT,Ray(keptFov,c)*(n<4?CornerAhead(i,n,a):a)+m.xyz);
        if(t.z<=1e-3) return false;
        const float2 tv=Project(targetFov,t)*grid.xy;
        if(n<4) {bmin=min(bmin,tv);bmax=max(bmax,tv);} else zc=t.z;
    }
    if(any(bmax-bmin>16)) return false;                             // a texel torn across the view (behind the camera's plane): none
    lo=max(int2(ceil(bmin-0.5)),int2(0,0));hi=min(int2(ceil(bmax-0.5))-1,int2(grid.xy)-1);
    key=sky?kSkyKey:(asuint(1/zc)&~kKeyFlags)|((w&kRig)?kKeyRig:0)|((w&(kMoving|kRig))?kKeyMoving:0);
    return all(hi>=lo);
}
// v1190: the rig target has the player's rig and held items at target texel i (metres ahead in `ahead`)
bool RigTargetAt(int2 i,out float ahead) {
    ahead=0;if(!TargetOn()) return false;
    const float2 r=rigTarget.Load(int3(clamp(i,int2(0,0),int2(grid.xy)-1),0));
    if(!(r.x>1e-7) || !(r.y>0)) return false;
    ahead=terms.y/(r.x+terms.x);return true;
}
// v1190 rule 3: with the rig target a kept rig texel lands only where the target has the rig (a hand carried there by a wrong or late motion
// is no hand: the fill shows what lies behind)
bool Lands(uint key,int2 j) {float a;return !TargetOn() || !(key&kKeyRig) || RigTargetAt(j,a);}
[numthreads(8,8,1)] void Splat(uint3 id : SV_DispatchThreadID) {
    if(any(id.xy>=uint2(grid.xy))) return;
    uint key;int2 lo,hi;
    if(SplatOf(int2(id.xy),key,lo,hi)) for(int y=lo.y;y<=hi.y;++y) for(int x=lo.x;x<=hi.x;++x) if(Lands(key,int2(x,y))) InterlockedMax(keysOut[Index(int2(x,y))],key);
}
// Origins: a moving texel whose key won a target texel leaves its own index there (the resolve takes its motion back off)
[numthreads(8,8,1)] void Origins(uint3 id : SV_DispatchThreadID) {
    if(any(id.xy>=uint2(grid.xy))) return;
    if(!(uint(motionIn[Index(int2(id.xy))].w)&(kMoving|kRig))) return;   // (v1191: a still world texel has no motion to leave)
    uint key;int2 lo,hi;
    if(SplatOf(int2(id.xy),key,lo,hi) && (key&kKeyMoving))
        for(int y=lo.y;y<=hi.y;++y) for(int x=lo.x;x<=hi.x;++x) if(keysIn[Index(int2(x,y))]==key) originsOut[Index(int2(x,y))]=Index(int2(id.xy));
}
uint Tile(int2 i) {return uint(i.y/8)*((uint(grid.x)+7)/8)+uint(i.x/8);}
uint Pack(int2 i,bool filled) {return uint(i.x)|(uint(i.y)<<15)|(filled?kFilled:0);}
int2 Unpack(uint v) {return int2(v&0x7fff,(v>>15)&0x7fff);}
float KeyZ(uint key) {return (key&~kKeyFlags)<=kSkyKey?1e30:1/asfloat(key&~kKeyFlags);}
)HLSL" R"HLSL(// Crack: a target texel no kept texel reached between two of one surface (left and right, or above and below) is that surface on its
// own ray (a splat seam where the view magnifies, not an uncovered strip); every reached texel is copied with itself as its ray.
[numthreads(8,8,1)] void Crack(uint3 id : SV_DispatchThreadID) {
    if(any(id.xy>=uint2(grid.xy))) return;
    const int2 i=int2(id.xy);const uint at=Index(i);
    uint key=keysIn[at],origin=originsIn[at];
    if(key==0) [unroll] for(int n=0;n<2;++n) {
        const int2 o=n==0?int2(1,0):int2(0,1);const int2 a=i-o,b=i+o;
        if(any(a<0) || any(b>=int2(grid.xy))) continue;
        const uint ka=keysIn[Index(a)],kb=keysIn[Index(b)];
        if(ka==0 || kb==0 || (ka&kKeyRig)!=(kb&kKeyRig)) continue;
        const float za=KeyZ(ka),zb=KeyZ(kb);
        if((za<1e29)!=(zb<1e29) || (za<1e29 && abs(za-zb)>=Tol(min(za,zb)))) continue;
        key=max(ka,kb);origin=ka>=kb?originsIn[Index(a)]:originsIn[Index(b)];break;
    }
    // v1185: a reached texel beside a hole is a SEED of the fill; only seeds and what they filled are taken (v1184 in-game: a jump flood
    // over every texel reached 64 texels across a sleeve's edge into the bright courtyard behind the wall and lit the sleeve's outline)
    bool seed=false;
    if(key!=0) [unroll] for(int n=0;n<8;++n) {
        float2 dir;sincos(n*0.7853982,dir.y,dir.x);
        const int2 j=i+int2(round(dir));
        seed=seed || (all(j>=0) && all(j<int2(grid.xy)) && keysIn[Index(j)]==0);
    }
    const uint l=Pack(i,false)|(seed?kSeed:0);const uint2 nr=uint2(asuint(key!=0?0.0:1e9),asuint(key!=0?((key&kKeyRig)?-KeyZ(key):KeyZ(key)):1e30));
    keysOut[at]=key;landedOut[at]=l;landedOut0[at]=l;originsOut[at]=origin;nearOut[at]=nr;nearOut0[at]=nr;
    if(key==0) {uint was;InterlockedOr(holesOut[1+Tile(i)],1,was);if(was==0) tilesOut.Append(Tile(i));}   // (v1197: the fill's tile list)
}
// Fill (jump flood, steps fillStep.x decreasing to 1): an empty or filled texel takes, among itself and its 8 neighbours fillStep.x
// apart that are seeds or filled, the FARTHEST surface (smallest key): an uncovered strip is the background's, stretched from its edge.
// v1197 cost: dispatched over the tiles that hold a hole only (the crack's list; DispatchIndirect): a tile without holes has the same
// texels in both buffers already
[numthreads(8,8,1)] void Fill(uint3 g : SV_GroupID,uint3 t : SV_GroupThreadID) {
    const uint tile=tilesIn[g.x],across=(uint(grid.x)+7)/8;
    const int2 i=int2((tile%across)*8+t.x,(tile/across)*8+t.y);
    if(any(i>=int2(grid.xy))) return;
    const uint at=Index(i);
    uint key=keysIn[at],landed=landedIn[at],origin=originsIn[at];const uint2 nr=nearIn[at];float nearest=asfloat(nr.x),zfore=abs(asfloat(nr.y));bool fore_rig=asfloat(nr.y)<0;
    if(key==0 || (landed&kFilled)) {
        // v1191: the nearest seed that lies BEHIND the nearest surface around the hole (the foreground that uncovered it, the player's rig
        // included); without one, the nearest seed (a crack within one surface). v1185-v1190 took the farthest seed in reach: swing 7 filled
        // the place a sleeve left with a courtyard wall 10 m away instead of the ground right above it
        const int s=int(fillStep.x);
        float z=key==0?0:KeyZ(key),d=key==0?1e9:length(float2(Unpack(landed)-i));
        uint ks[8],ls[8],os[8];
        [unroll] for(int n=0;n<8;++n) {                           // the candidates, and the nearest seed any of them reached
            const int2 j=i+int2(n<3?n-1:n==3?-1:n==4?1:n-6,n<3?-1:n<5?0:1)*s;
            ks[n]=0;ls[n]=0;os[n]=0;
            if(any(j<0) || any(j>=int2(grid.xy))) continue;
            const uint k=keysIn[Index(j)],l=landedIn[Index(j)];
            // a seed or what one filled; never the player's rig (v1190 swing 7: a sword 1.1 m away was the farthest seed beside a sleeve and
            // its colour was stretched over the place the hand left; where the rig is, the rig target says so)
            if(k==0 || !(l&(kFilled|kSeed))) continue;
            if(KeyZ(k)<zfore) {zfore=KeyZ(k);fore_rig=(k&kKeyRig)!=0;}   // (the rig counts as the foreground, never as a source)
            if(TargetOn() && (k&kKeyRig)) continue;
            ks[n]=k;ls[n]=l;os[n]=originsIn[Index(j)];
            nearest=min(nearest,length(float2(Unpack(l)-i)));
        }
        // v1185 bound: a seed farther than twice the nearest one (+2 texels) counts only as far as the strip such a step can uncover
        // between the nearest seed's surface and its own: the camera's sideways step times the inverse-depth gap, plus the largest
        // displacement of anything that moved (Motion): a strip beside a near object takes its background, a crack beside a sleeve the
        // wall beside it, never a courtyard 60 texels away another hole's seeds offer (v1184 room: bright lines along the sleeve)
        const float zf=zfore,moved=float(handVotesIn[8]);
        #define BOUND(zk) (2*nearest+2+fillStep.z*max((zf<1e29?1/zf:0)-(zk<1e29?1/zk:0),0)+moved)
        bool held=key!=0 && d<=BOUND(z);                          // what it holds came from within the bound (else any candidate replaces it)
        #define BEHIND(zz) ((zz)>=1e29 || (zz)>zfore*1.15+0.02)
        bool behind=key!=0 && BEHIND(z);
        [unroll] for(int n=0;n<8;++n) {
            const uint k=ks[n],l=ls[n];if(k==0) continue;
            const float zk=KeyZ(k),dk=length(float2(Unpack(l)-i));
            if(dk>BOUND(zk)) continue;
            const bool bk=BEHIND(zk);
            if(key==0 || !held || (bk && !behind) || (bk==behind && dk<d)) {key=k;landed=(l&~(kFilled|kSeed))|kFilled;origin=os[n];z=zk;d=dk;held=true;behind=bk;}
        }
    }
    keysOut[at]=key;landedOut[at]=landed;originsOut[at]=origin;nearOut[at]=uint2(asuint(nearest),asuint(fore_rig?-zfore:zfore));
    if(fillStep.y>0 && key==0) InterlockedAdd(holesOut[0],1);
}
// FreshSplat (rig_from_fresh, v1173): while a hand moves, the fresh eye's rig texels splatted into the target grid on their own keys (the
// resolve shows them only where nothing of this eye's own lies as near)
[numthreads(8,8,1)] void FreshSplat(uint3 id : SV_DispatchThreadID) {
    if(any(id.xy>=uint2(freshGrid.xy))) return;
    const int2 i=int2(id.xy);
    const float2 uv=(float2(i)+0.5)*freshGrid.zw;
    if(!InFresh(uv)) return;
    const float a=FreshAhead(i);if(a<=0) return;
    // the rig of a group that stood down (v1185: only then; the fresh eye's rim-lit fingers showed around a kept hand otherwise); a near
    // mover (within reach: its vector may be unusable, see Motion), never a far one (its own shading)
    const bool rig=FreshRigAt(i);
    if(rig && (TargetOn() || !StandsDown(uint(Owner(Apply(TK,Apply(FT,Ray(freshFov,uv)*a)),true))))) return;
    if(!rig && !(flags.y>0 && a<1.5 && freshMotion.Load(int3(i,0)).w>=fillStep.w)) return;
    const float3 t=Apply(FT,Ray(freshFov,uv)*a);
    if(t.z<=1e-3) return;
    const float2 tv=Project(targetFov,t);
    if(any(tv<0) || any(tv>=1)) return;
    InterlockedMax(freshKeysOut[Index(int2(tv*grid.xy))],asuint(1/t.z)&~kKeyFlags);
}
)HLSL" R"HLSL(float4 Fetch(Texture2D image,float2 texel) {return image.SampleLevel(linearSampler,texel*display.zw,0);}
// Catmull-Rom through nine bilinear taps, the detail restore and the 2x2 clamp: the warp's own filter (StaleEyeWarp Smooth, v803)
float4 Smooth(Texture2D image,float2 q) {
    const float2 p=q*display.xy;
    const float2 c=floor(p-0.5)+0.5,f=p-c;
    const float2 w0=f*(-0.5+f*(1-0.5*f)),w1=1+f*f*(-2.5+1.5*f),w2=f*(0.5+f*(2-1.5*f)),w3=f*f*(-0.5+0.5*f);
    const float2 w12=w1+w2,c12=c+w2/w12,c0=c-1,c3=c+2;
    const float4 r=Fetch(image,float2(c0.x,c0.y))*w0.x*w0.y+Fetch(image,float2(c12.x,c0.y))*w12.x*w0.y+Fetch(image,float2(c3.x,c0.y))*w3.x*w0.y
                  +Fetch(image,float2(c0.x,c12.y))*w0.x*w12.y+Fetch(image,float2(c12.x,c12.y))*w12.x*w12.y+Fetch(image,float2(c3.x,c12.y))*w3.x*w12.y
                  +Fetch(image,float2(c0.x,c3.y))*w0.x*w3.y+Fetch(image,float2(c12.x,c3.y))*w12.x*w3.y+Fetch(image,float2(c3.x,c3.y))*w3.x*w3.y;
    const float4 restored=r+1.5*(r-image.SampleLevel(linearSampler,q,0));
    const int2 i0=int2(floor(p-0.5)),top=int2(display.xy)-1;
    const float4 t00=image.Load(int3(clamp(i0,int2(0,0),top),0)),t10=image.Load(int3(clamp(i0+int2(1,0),int2(0,0),top),0)),
                 t01=image.Load(int3(clamp(i0+int2(0,1),int2(0,0),top),0)),t11=image.Load(int3(clamp(i0+int2(1,1),int2(0,0),top),0));
    return saturate(clamp(restored,min(min(t00,t10),min(t01,t11)),max(max(t00,t10),max(t01,t11))));
}
// The kept colour at q of the surface `ahead` metres away (0: the sky): the filter, unless the depth texels around q (3x3: the filter's
// footprint at a render fraction down to 2/3) reach another surface (an edge); then the nearest of the 2x2 colour pixels around q whose
// whole area (its centre and corners) lies on this surface: no colour bleeds across an edge, also where render-sized depth puts the
// edge a colour pixel off
bool SameAt(float2 uv,float ahead) {
    const float a=KeptAhead(clamp(int2(uv*grid.xy),int2(0,0),int2(grid.xy)-1));
    return ahead<=0?a<=0:(a>0 && abs(a-ahead)<Tol(ahead));
}
float4 Clean(float2 q,float ahead) {
    const int2 c=int2(q*grid.xy);bool mixed=false;
    [unroll] for(int y=-1;y<=1;++y) [unroll] for(int x=-1;x<=1;++x) mixed=mixed || !SameAt((float2(c+int2(x,y))+0.5)*grid.zw,ahead);
    if(!mixed) return Smooth(keptColour,q);
    const float2 p=q*display.xy-0.5;const int2 i0=int2(floor(p));const float2 f=p-i0;
    float4 sum=0;float weight=0;
    [unroll] for(int b=0;b<4;++b) {
        const int2 o=int2(b&1,b>>1);const float2 m=float2(i0+o)+0.5;
        bool same=SameAt(m*display.zw,ahead);
        [unroll] for(int k=0;k<4;++k) same=same && SameAt((m+float2(k&1,k>>1)-0.5)*display.zw,ahead);
        const float w=(o.x?f.x:1-f.x)*(o.y?f.y:1-f.y);
        if(same) {sum+=w*keptColour.Load(int3(clamp(i0+o,int2(0,0),int2(display.xy)-1),0));weight+=w;}
    }
    if(weight>0.25) return sum/weight;
    int2 pick=int2(0,0);int2 centre=pick;float best=99,best_centre=99;
    [unroll] for(int n=0;n<16;++n) {                              // the filter's 4x4 pixels, nearest first by distance
        const int2 o=int2(n&3,n>>2)-1;const float2 m=float2(i0+o)+0.5;
        const bool at=SameAt(m*display.zw,ahead);bool same=at;
        [unroll] for(int k=0;k<4;++k) same=same && SameAt((m+float2(k&1,k>>1)-0.5)*display.zw,ahead);
        const float d=dot(f-o,f-o);
        if(same && d<best) {best=d;pick=o;}
        if(at && d<best_centre) {best_centre=d;centre=o;}
    }
    if(best>98) {pick=centre;best=best_centre;}                   // else the nearest whose centre lies on it
    if(best>98) return Smooth(keptColour,q);
    return keptColour.Load(int3(clamp(i0+pick,int2(0,0),int2(display.xy)-1),0));
}
// v1190: the fresh eye's rig colour at fresh uv fu of a rig point fz metres ahead: the bilinear blend of the 2x2 colour pixels around fu
// whose texel is rig at that depth (no colour of a post or wall beside the hand bleeds in); ok false: none of the four is
)HLSL" R"HLSL(// v1190 tools (rigMode.z): the source of a pixel as a colour code instead of its colour: rig 1 kept, 2 fresh, 3 kept neighbour, 4 kept
// hidden-both, 5 fresh neighbour, 6 fresh unmasked; world 0 kept, 7 kept by the fill's stretch, 8 fresh beyond the kept view, 9 kept by
// the turn (no key), 10 the fresh eye's near mover
// v1197 safety net (rig_stats, Decide): every 16th texel (4x4 grid) the rig target has: counted, and counted again when it resolved to the world
// (codes 0, 7-10: a broken hand path shows as a share, not only in a picture). [0] the target: 1 used, 2 ignored (TargetOn), 0 none.
RWStructuredBuffer<uint> rigStats : register(u1);
static bool countRig=false;
float4 RigSource(float4 c,int code) {
    if(countRig) {InterlockedAdd(rigStats[1],1);if(code==0 || code>=7) InterlockedAdd(rigStats[2],1);}
    return rigMode.z>0?float4(code*20/255.0,0,0,1):c;
}
float4 FreshRigSample(float2 fu,float fz,out bool ok) {
    const float2 p=fu*display.xy-0.5;const int2 i0=int2(floor(p));const float2 f=p-i0;
    float4 sum=0;float weight=0;
    [unroll] for(int n=0;n<4;++n) {
        const int2 o=int2(n&1,n>>1);const int2 c=clamp(i0+o,int2(0,0),int2(validRect.zw*display.xy)-1);
        const int2 j=clamp(int2((float2(c)+0.5)*display.zw*freshGrid.xy),int2(0,0),int2(freshGrid.xy)-1);
        if(!FreshRigAt(j) || abs(FreshAhead(j)-fz)>=Tol(fz)) continue;
        const float w=max((o.x?f.x:1-f.x)*(o.y?f.y:1-f.y),1e-4);
        sum+=w*freshColour.Load(int3(c,0));weight+=w;
    }
    ok=weight>0;return ok?sum/weight:float4(0,0,0,0);
}
struct Vertex {float4 position : SV_POSITION; float2 uv : TEXCOORD0;};
Vertex ResolveVS(uint id : SV_VertexID) {
    const float2 uv=float2((id<<1)&2,id&2);
    Vertex v;v.uv=uv;v.position=float4(uv*float2(2,-2)+float2(-1,1),0,1);return v;
}
)HLSL" R"HLSL(// v1197 cost: the resolve decides per target texel (render size, Decide) and the display pass (ResolvePS) only samples. A decision:
// x the source place q (uv of the image it reads, 2 x unorm16), y bits 0 the image (fresh), 1-2 how (smooth, clean, the fresh rig's
// masked sample, one pixel), 3 q is the texel's whole (a fill's stretch, an inpaint pixel), 4 unclamped (the still frame), 5-8 the
// RigSource code, 16-31 the surface's metres ahead (f16, Clean and the masked sample). Otherwise q is the texel centre's and each pixel
// adds its own offset from that centre (smooth: the offsets of the 2x2 texels around it interpolated where they move as one).
static const uint kImgFresh=1,kSmooth=0,kClean=2,kRigSample=4,kLoad=6,kHow=6,kWhole=8,kRaw=16;
uint2 Rec(float2 q,uint how,int code,float ahead) {
    const uint2 u=uint2(saturate(q)*65535+0.5);
    return uint2(u.x|(u.y<<16),how|(uint(code)<<5)|(f32tof16(max(ahead,0))<<16));
}
float2 RecQ(uint x) {return float2(x&0xffff,x>>16)/65535.0;}
// Decide (resolve, per target texel): the target depth (its own texel's, or the fill's), the point on its ray (the filled texel's ray: the
// stretch), back into the kept camera less the motion of the texel it came from: where this eye's own colour is read.
uint2 DecideAt(int2 i,float2 uv) {
    const uint at=Index(i);
    const uint key=keysIn[at],landed=landedIn[at];
    const float z=key==0?1e30:KeyZ(key);
    // (c) rig_from_fresh: a moving hand no kept texel placed, nearer than what was splatted here
    const uint fk=freshKeysIn[at];
    if(fk!=0) {
        const float zf=1/asfloat(fk);
        if(key==0 || zf<z-Tol(zf) || ((landed&kFilled) && zf<z)) {
            const float3 f=Apply(TF,Ray(targetFov,uv)*zf);
            const float2 fu=Project(freshFov,f);
            if(f.z>1e-3 && InFresh(fu)) return Rec(fu,kImgFresh|kSmooth,10,0);
        }
    }
    // v1190: the rig target decides the player's pixels. Rule 1: rig where the target has it and nothing of the world splatted here is
    // nearer (3 % + 1 cm). Rule 2: its colour from (b) this eye's kept rig texel the splat put here at that depth, or (a) the fresh eye's rig
    // at the exact reprojection of (pixel, target depth) where its mask and depth agree (rigMode.y picks the order); (c) the nearest kept rig
    // texel of the target's rig within 2 texels, at this pixel's depth with its motion; last the fresh eye's sample there. Never background.
    float zr;
    if(RigTargetAt(i,zr)) {
        const bool key_rig=key!=0 && (key&kKeyRig) && !(landed&kFilled);
        if(key_rig || key==0 || zr<z-Tol(zr)) {
            const float3 r=Ray(targetFov,uv);
            bool kept_ok=false,fresh_ok=false;
            // (b) only from a group the fresh eye's votes did not stand down: a flat hand confirms a wrong grip texel by texel
            const uint wo=(key&kKeyMoving)?uint(motionIn[originsIn[at]].w):0;
            const bool down=(key&kKeyMoving) && StandsDown((wo>>5)&3,(wo&kHeld)!=0);
            uint2 kept_any=0;bool kept_any_ok=false;    // (the same from a group that stood down: only before showing what covered the rig)
            if(key_rig && abs(z-zr)<Tol(zr)) {
                float3 pk=Apply(TK,r*z);if(key&kKeyMoving) pk-=motionIn[originsIn[at]].xyz;
                // (and the kept frame showed that point: not hidden behind something nearer there, a post before the hand)
                const float2 kq=pk.z>1e-4?clamp(Project(keptFov,pk),0.5*display.zw,validRect.xy-2*display.zw):float2(-1,-1);
                if(pk.z>1e-4 && SameAt(kq,pk.z)) {kept_any=Rec(kq,kClean,1,pk.z);kept_any_ok=true;}   // (Clean: no colour of the post beside it)
                kept_ok=kept_any_ok && !down;
            }
            const float3 f=Apply(TF,r*zr);const float2 fu=f.z>1e-3?Project(freshFov,f):float2(-1,-1);
            if(InFresh(fu)) FreshRigSample(fu,f.z,fresh_ok);
            const uint2 fresh_c=Rec(fu,kImgFresh|kRigSample,2,f.z);
            if(rigMode.y<0.5) {if(kept_ok) return kept_any;if(fresh_ok) return fresh_c;}
            else {if(fresh_ok) return fresh_c;if(kept_ok) return kept_any;}
            float best=99;uint best_at=0;
            [unroll] for(int n=0;n<25;++n) {
                const int2 o=int2(n%5,n/5)-2;const int2 j=clamp(i+o,int2(0,0),int2(grid.xy)-1);const uint aj=Index(j);
                const uint kj=keysIn[aj];float zj;
                if(kj==0 || !(kj&kKeyRig) || (landedIn[aj]&kFilled) || !RigTargetAt(j,zj) || abs(KeyZ(kj)-zj)>=Tol(zj) || dot(o,o)>=best) continue;
                if((kj&kKeyMoving) && StandsDown((uint(motionIn[originsIn[aj]].w)>>5)&3,(uint(motionIn[originsIn[aj]].w)&kHeld)!=0)) continue;
                best=dot(o,o);best_at=aj;
            }
            if(best<99) {
                float3 pk=Apply(TK,r*zr);if(keysIn[best_at]&kKeyMoving) pk-=motionIn[originsIn[best_at]].xyz;
                const float2 kq=pk.z>1e-4?clamp(Project(keptFov,pk),0.5*display.zw,validRect.xy-2*display.zw):float2(-1,-1);
                if(pk.z>1e-4 && SameAt(kq,pk.z)) return Rec(kq,kClean,3,pk.z);
                // hidden from both eyes there (behind a post in both views): the kept hand's own pixels nearest that place (Clean keeps
                // to the hand's surface), not whatever covered it
                if(pk.z>1e-4) {const float2 cq=kq;const int2 cc=int2(cq*grid.xy);bool hand_near=false;
                    [unroll] for(int n=0;n<9;++n) hand_near=hand_near || SameAt((float2(cc+int2(n%3,n/3)-1)+0.5)*grid.zw,pk.z);
                    if(hand_near) return Rec(cq,kClean,4,pk.z);
                    // else the hand's nearest own pixel within 8 render texels (16 directions)
                    [loop] for(int kr=2;kr<=8;++kr) [loop] for(int nn=0;nn<16;++nn) {const int n=nn;
                        float2 dir;sincos(n*0.3926991,dir.y,dir.x);const float2 u=cq+round(dir*kr)*grid.zw;const int2 px=int2(u*display.xy);
                        if(InKept(u) && SameAt((float2(px)+0.5)*display.zw,pk.z)) return Rec((float2(px)+0.5)*display.zw,kLoad|kWhole,4,0);
                    }}
            }
            if(f.z>1e-3) {
                const float2 fc=clamp(fu,display.zw,validRect.zw-2*display.zw);bool any_rig;FreshRigSample(fc,f.z,any_rig);
                if(any_rig) return Rec(fc,kImgFresh|kRigSample,2,f.z);
                // (c) in the fresh eye: hidden from both eyes here (behind a post in both views): the fresh eye's nearest rig pixel at this
                // depth within 8 render texels, never what covered it
                [loop] for(int rr=2;rr<=8;++rr) [loop] for(int nn=0;nn<16;++nn) {const int n=nn;   // ([unroll] here with the return inside: the Decide compute shader found nothing, lab R)
                    float2 dir;sincos(n*0.3926991,dir.y,dir.x);const float2 u=fc+round(dir*rr)*freshGrid.zw;
                    const int2 px=int2(u*display.xy);   // the colour pixel there, judged by the texel under its centre
                    const int2 j=clamp(int2((float2(px)+0.5)*display.zw*freshGrid.xy),int2(0,0),int2(freshGrid.xy)-1);
                    if(InFresh(u) && FreshRigAt(j) && abs(FreshAhead(j)-f.z)<0.05+0.1*f.z) return Rec((float2(px)+0.5)*display.zw,kImgFresh|kLoad|kWhole,5,0);
                }
                if(kept_any_ok) return uint2(kept_any.x,(kept_any.y&~(15u<<5))|(3u<<5));
                // v1191 (in-game swing 8: a phantom second sword in the rig target): no eye has this class near here: not the rig; the
                // world behind shows through (the resolve below), never noise or an inpaint over it
            }
        }
    }
    if(key==0) {                                                  // never black: the kept image by the camera turn alone
        const float3 d=Turn(TK,Ray(targetFov,uv));
        return Rec(clamp(d.z>1e-6?Project(keptFov,d):uv,display.zw,validRect.xy-display.zw),kSmooth,9,0);
    }
    bool filled=(landed&kFilled)!=0;
    // beyond the kept frame's view (a turn, a step sideways at the edge): no texel of this eye's own ever saw it, and a stretch from the
    // edge would smear tens of pixels; the fresh eye's surface on the ray (two fixed-point steps from the fill's depth), as v807 did
    if(filled) {
        const float3 o=z<1e29?Apply(TK,Ray(targetFov,uv)*z):Turn(TK,Ray(targetFov,uv));
        const float2 oq=o.z>1e-6?Project(keptFov,o):float2(-1,-1);
        // v1190 (swing 7: a hand's old place filled by stretching one row of texels over 6500 px): a hole wider than 4 texels where the rig
        // target is known (the place a hand or held item left) takes the fresh eye's world surface on the ray, which sees it now
        const bool wide=TargetOn() && length(float2(Unpack(landed)-i))>4 && asfloat(nearIn[at].y)<0;
        if(!InKept(oq) || wide) {
            const float3 r=Ray(targetFov,uv);float zf=min(z,1e4);float2 fu=float2(-1,-1);bool sky=false;
            [unroll] for(int n=0;n<3;++n) {
                const float3 f=Apply(TF,r*zf);fu=f.z>1e-3?Project(freshFov,f):float2(-1,-1);
                if(!InFresh(fu) || n==2) break;
                const float a=FreshAhead(clamp(int2(fu*freshGrid.xy),int2(0,0),int2(freshGrid.xy)-1));
                if(a<=0) {sky=true;const float3 d=Turn(TF,r);fu=d.z>1e-6?Project(freshFov,d):fu;break;}
                const float k=dot(TF[2].xyz,r);if(k<=1e-6) break;
                zf=max((a-TF[2].w)/k,1e-3);
            }
            const int2 fj=clamp(int2(fu*freshGrid.xy),int2(0,0),int2(freshGrid.xy)-1);
            if(InFresh(fu) && (!wide || !FreshRigAt(fj))) return Rec(fu,kImgFresh|kSmooth,8,0);
        }
    }
    if(filled && z<1e29 && !(key&kKeyMoving)) {          // a filled texel whose own ray, at the background's depth, meets that surface in the
        const float3 o=Apply(TK,Ray(targetFov,uv)*z);     // kept frame (the edge of the strip, a render texel coarser than the colour): its own ray
        const float2 oq=o.z>1e-4?Project(keptFov,o):float2(-1,-1);
        if(InKept(oq) && SameAt(oq,o.z)) filled=false;
    }
    const float2 ray=filled?(float2(Unpack(landed))+0.5)*grid.zw:uv;
    if(layersOn.w>0 && !(key&kKeyMoving) && !(landed&kFilled)) return Rec(uv,kSmooth|kRaw,0,0);   // a still frame: this eye's own image, exactly
    float2 q;float3 p=float3(0,0,0);
    // v1186 (in-game: silhouettes stair-stepped under a 7 mm step; lab F): the kept image's anti-aliased edge pixels are blends of both
    // surfaces. Where every surface within a render texel (the 3x3 target texels) goes back to within 0.75 px of the same kept place, the
    // edge moved as one: the continuous back-projection is filtered as it is (its blend pixels with it); only where the surfaces part
    // does Clean keep one surface's colour from bleeding onto the other's
    bool apart=false;float zs[9];
    [unroll] for(int n=0;n<9;++n) zs[n]=-1;
    if(!filled) {
        float2 q0=float2(-9,-9);
        [unroll] for(int n=0;n<9;++n) {
            const int2 j=clamp(i+int2(n%3,n/3)-1,int2(0,0),int2(grid.xy)-1);const uint at_j=Index(j);
            const uint k=keysIn[at_j];if(k==0 || (landedIn[at_j]&kFilled)) continue;
            const float zj=KeyZ(k);float2 qj;zs[n]=zj;
            // (v1191 cost: a still texel of this pixel's own surface goes back to the same place: no projection)
            if(n!=4 && !(k&kKeyMoving) && !(key&kKeyMoving) && zj<1e29 && z<1e29 && abs(zj-z)<0.002*z) continue;
            if(zj>=1e29) {const float3 d=Turn(TK,Ray(targetFov,uv));qj=d.z>1e-6?Project(keptFov,d):uv;}
            else {float3 pj=Apply(TK,Ray(targetFov,uv)*zj);if(k&kKeyMoving) pj-=motionIn[originsIn[at_j]].xyz;qj=pj.z>1e-4?Project(keptFov,pj):uv;}
            if(q0.x<-8) q0=qj; else apart=apart || length((qj-q0)*display.xy)>0.75;
        }
    } else apart=true;
    if(z>=1e29) {const float3 d=Turn(TK,Ray(targetFov,ray));q=d.z>1e-6?Project(keptFov,d):uv;}
    else {
        p=Apply(TK,Ray(targetFov,ray)*z);
        if(key&kKeyMoving) p-=motionIn[originsIn[at]].xyz;
        q=p.z>1e-4?Project(keptFov,p):uv;
    }
    const float2 qc=clamp(q,0.5*display.zw,validRect.xy-2*display.zw);  // v1185: the filter never reaches the band (a bright line at the right edge)
    // and every surface the kept filter would read there is one that lands here too (else it is a surface this pixel no longer shows: the
    // far side of an uncovered strip, whose kept neighbour is the edge that moved away)
    if(!apart) {
        const int2 c=int2(qc*grid.xy);
        [unroll] for(int n=0;n<9;++n) {
            const float a=KeptAhead(clamp(c+int2(n%3,n/3)-1,int2(0,0),int2(grid.xy)-1));
            bool here=false;
            [unroll] for(int m=0;m<9;++m) here=here || (zs[m]>0 && (a<=0?zs[m]>=1e29:zs[m]<1e29 && abs(zs[m]-a)<Tol(a)));
            apart=apart || !here;
        }
    }
    return Rec(qc,(apart?kClean:kSmooth)|(filled?kWhole:0),filled?7:0,z>=1e29?0:p.z);
}
[numthreads(8,8,1)] void Decide(uint3 id : SV_DispatchThreadID) {
    if(any(id.xy>=uint2(grid.xy))) return;
    const uint2 r=DecideAt(int2(id.xy),min((float2(id.xy)+0.5)*grid.zw,validRect.xy));
    decisionsOut[Index(int2(id.xy))]=r;
    float zt;
    if(all((id.xy&3)==0) && RigTargetAt(int2(id.xy),zt)) {const uint code=(r.y>>5)&15;InterlockedAdd(rigStats[1],1);if(code==0 || code>=7) InterlockedAdd(rigStats[2],1);}
    if(all(id.xy==0)) rigStats[0]=rigMode.x>0?(TargetOn()?1:2):0;
}
// the display pass: each pixel reads its texel's decision and samples there
float2 Offset(float2 uv,uint how,float2 own) {        // smooth: the 2x2 texels' offsets interpolated where all four move as one (0.75 px)
    const float2 p=uv*grid.xy-0.5;const int2 t0=int2(floor(p));const float2 f=p-float2(t0);
    float2 sum=0;
    [unroll] for(int n=0;n<4;++n) {
        const int2 o=int2(n&1,n>>1);const int2 j=clamp(t0+o,int2(0,0),int2(grid.xy)-1);const uint2 r=decisionsIn[Index(j)];
        const float2 off=RecQ(r.x)-(float2(j)+0.5)*grid.zw;
        if((r.y&31)!=how || any(abs(off-own)*display.xy>0.75)) return own;
        sum+=(o.x?f.x:1-f.x)*(o.y?f.y:1-f.y)*off;
    }
    return sum;
}
float4 ResolvePS(Vertex v) : SV_TARGET {
    const float2 uv=min(v.uv,validRect.xy);                       // v1174: the band rebuilt from the clamped edge
    const int2 i=min(int2(uv*grid.xy),int2(grid.xy)-1);
    const uint2 r=decisionsIn[Index(i)];
    const uint how=r.y&31;const int code=int((r.y>>5)&15);const float ahead=f16tof32(r.y>>16);
    float2 q=RecQ(r.x);
    if(how&kRaw) q=uv;                                            // the still frame: exactly its own pixel
    else if(!(how&kWhole)) {const float2 own=q-(float2(i)+0.5)*grid.zw;q=uv+((how&kHow)==kSmooth?Offset(uv,how,own):own);}
    if((how&kHow)==kLoad) return RigSource((how&kImgFresh)?freshColour.Load(int3(int2(q*display.xy),0)):keptColour.Load(int3(int2(q*display.xy),0)),code);
    if(how&kImgFresh) {
        q=min(q,validRect.zw-2*display.zw);
        if((how&kHow)==kRigSample) {     // (the texel centre's sample found the rig: where this pixel's does not, the centre's)
            bool ok;float4 c=FreshRigSample(q,ahead,ok);if(!ok) c=FreshRigSample(min(RecQ(r.x),validRect.zw-2*display.zw),ahead,ok);
            if(ok) return RigSource(c,code);
        }
        return RigSource(Smooth(freshColour,q),code);
    }
    if(!(how&kRaw)) q=clamp(q,0.5*display.zw,validRect.xy-2*display.zw);
    return RigSource((how&kHow)==kClean?Clean(q,ahead):Smooth(keptColour,q),code);
}
)HLSL";
    const auto compile=[&](const char* entry,const char* profile) {
        Ptr<ID3DBlob> binary,errors;
        if(FAILED(D3DCompile(source.data(),source.size(),"D2VR AFW2",nullptr,nullptr,entry,profile,D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&binary,&errors)))
            throw std::runtime_error(std::string("AFW2 shader ")+entry+": "+(errors?std::string(static_cast<const char*>(errors->GetBufferPointer()),errors->GetBufferSize()):"compile failed"));
        return binary;
    };
    const char* entries[stages]{"Motion","Repair","Splat","Origins","Crack","Fill","FreshSplat","Scatter","ScatterWrite","Decide"};
    for(int s=0;s<stages;++s) {const auto b=compile(entries[s],"cs_5_0");checked(device_->CreateComputeShader(b->GetBufferPointer(),b->GetBufferSize(),nullptr,&cs_[s]),"compute shader");}
    {const auto b=compile("ResolveVS","vs_5_0");checked(device_->CreateVertexShader(b->GetBufferPointer(),b->GetBufferSize(),nullptr,&vs_),"vertex shader");}
    {const auto b=compile("ResolvePS","ps_5_0");checked(device_->CreatePixelShader(b->GetBufferPointer(),b->GetBufferSize(),nullptr,&ps_),"pixel shader");}
    D3D11_BUFFER_DESC c{};c.ByteWidth=4*4*48;c.BindFlags=D3D11_BIND_CONSTANT_BUFFER;c.Usage=D3D11_USAGE_DEFAULT;
    checked(device_->CreateBuffer(&c,nullptr,&constants_),"constants");
    D3D11_SAMPLER_DESC s{};s.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;s.AddressU=s.AddressV=s.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;s.MaxLOD=D3D11_FLOAT32_MAX;
    checked(device_->CreateSamplerState(&s,&sampler_),"sampler");
    D3D11_RASTERIZER_DESC r{};r.FillMode=D3D11_FILL_SOLID;r.CullMode=D3D11_CULL_NONE;r.DepthClipEnable=TRUE;checked(device_->CreateRasterizerState(&r,&raster_),"rasterizer");
    D3D11_DEPTH_STENCIL_DESC ds{};ds.DepthFunc=D3D11_COMPARISON_ALWAYS;checked(device_->CreateDepthStencilState(&ds,&depth_),"depth state");
    votes_=buffer(28,4);stats_=buffer(4,4);
    {D3D11_BUFFER_DESC a{};a.ByteWidth=12;a.Usage=D3D11_USAGE_DEFAULT;a.MiscFlags=D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;
     const UINT one[3]{0,1,1};D3D11_SUBRESOURCE_DATA i{one,0,0};checked(device_->CreateBuffer(&a,&i,&fill_args_),"fill arguments");}
    D3D11_BUFFER_DESC h{};h.ByteWidth=4;h.Usage=D3D11_USAGE_STAGING;h.CPUAccessFlags=D3D11_CPU_ACCESS_READ;h.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;h.StructureByteStride=4;
    checked(device_->CreateBuffer(&h,nullptr,&holes_read_),"holes read-back");
}

inline ID3D11Texture2D* Afw2Pass::run(const StaleLayers& L,UINT width,UINT height,ID3D11ShaderResourceView* kept,ID3D11ShaderResourceView* kept_depth,UINT kw,UINT kh,
                                      ID3D11ShaderResourceView* fresh,ID3D11ShaderResourceView* fresh_depth,UINT fw,UINT fh,
                                      ID3D11ShaderResourceView* kept_motion,ID3D11ShaderResourceView* fresh_motion,
                                      ID3D11ShaderResourceView* kept_rig,ID3D11ShaderResourceView* fresh_rig,bool rig_target) {
    grids(kw,kh,fw,fh);target(width,height);
    const auto fov_or=[&](const WarpFov& f) {return warp_fov_valid(f)?f:L.kept_fov;};
    const WarpFov kf=L.kept_fov,tf=fov_or(L.target_fov),ff=fov_or(L.fresh_fov);
    const Afw2Rigid TK=afw2_rigid(L.world,kf,tf),TF=afw2_rigid(*L.fresh,ff,tf);
    const Afw2Rigid KT=afw2_inverse(TK),FT=afw2_inverse(TF),KF=afw2_mul(TF,KT);
    const auto layer=[&](const std::optional<StaleParallax>& p) {return p?afw2_mul(TK,afw2_inverse(afw2_rigid(*p,kf,tf))):Afw2Rigid{};};
    std::array<float,4*48> c{};std::size_t at=0;
    const auto put=[&](double x,double y,double z,double w) {c[at++]=float(x);c[at++]=float(y);c[at++]=float(z);c[at++]=float(w);};
    const auto tans=[&](const WarpFov& f) {put(std::tan(f.left),std::tan(f.right),std::tan(f.up),std::tan(f.down));};
    const auto rigid=[&](const Afw2Rigid& r) {for(int i=0;i<3;++i) put(r.r[i*3],r.r[i*3+1],r.r[i*3+2],r.t[i]);};
    tans(kf);tans(tf);tans(ff);
    put(kw,kh,1./kw,1./kh);put(fw,fh,1./fw,1./fh);put(L.world.m10,L.world.m11,L.fresh->m10,L.fresh->m11);
    rigid(KT);rigid(TK);rigid(KF);rigid(FT);rigid(TF);
    rigid(layer(L.carried));rigid(layer(L.hand[0]));rigid(layer(L.hand[1]));
    for(int s=0;s<2;++s) put(L.palms[s][0],L.palms[s][1],L.palms[s][2],L.palms[s][3]);
    for(int i=0;i<3;++i) put(L.kept_axes[i*3],L.kept_axes[i*3+1],L.kept_axes[i*3+2],0);
    const bool masks=kept_rig && fresh_rig,motions=kept_motion && fresh_motion && L.motion_rules;
    const bool rig_motion=motions && masks && L.rig_motion_rules;  // per texel: mask 255 (engine 4bd8ad2), not the per-render flags
    put(masks?1:0,motions?1:0,rig_motion?1:0,afw2_moving_px);
    const bool hands=masks && L.rig_from_fresh;
    put(L.fresh_part,L.kept_part,hands && L.hand_moving[0]?1:0,hands && L.hand_moving[1]?1:0);
    bool still=true;for(int i=0;i<9;++i) still=still && std::abs(TK.r[i]-(i%4==0?1:0))<1e-7;for(double t:TK.t) still=still && std::abs(t)<1e-7;
    put(L.carried?1:0,L.hand[0]?1:0,L.hand[1]?1:0,still?1:0);
    put(width,height,1./width,1./height);
    {const double u=1-std::clamp(L.border[0],0.,.25),v=1-std::clamp(L.border[1],0.,.25);put(u,v,u,v);}
    const std::size_t step_at=at;put(0,0,std::hypot(TK.t[0],TK.t[1])*kw/(std::tan(kf.right)-std::tan(kf.left)),mover_px);
    put(rig_target && kept_rig && fresh_rig?1:0,rig_order,rig_debug,0);   // (the rig target needs the masks: its colour comes from rig texels)
    const auto upload=[&](double step,bool last) {c[step_at]=float(step);c[step_at+1]=last?1.f:0.f;context_->UpdateSubresource(constants_.Get(),0,nullptr,c.data(),0,0);};
    upload(0,false);
    ID3D11Buffer* cb=constants_.Get();context_->CSSetConstantBuffers(0,1,&cb);context_->PSSetConstantBuffers(0,1,&cb);
    ID3D11SamplerState* sm=sampler_.Get();context_->PSSetSamplers(0,1,&sm);
    ID3D11ShaderResourceView* inputs[8]{kept,kept_depth,fresh,fresh_depth,motions?kept_motion:nullptr,motions?fresh_motion:nullptr,masks?kept_rig:nullptr,masks?fresh_rig:nullptr};
    context_->CSSetShaderResources(0,8,inputs);
    const auto bind=[&](std::initializer_list<ID3D11ShaderResourceView*> srv,std::initializer_list<ID3D11UnorderedAccessView*> uav) {
        ID3D11ShaderResourceView* none_s[7]{};context_->CSSetShaderResources(8,7,none_s);
        ID3D11UnorderedAccessView* none_u[8]{};context_->CSSetUnorderedAccessViews(0,8,none_u,nullptr);
        ID3D11ShaderResourceView* s[7]{};std::size_t n=0;for(auto* x:srv) s[n++]=x;context_->CSSetShaderResources(8,7,s);
        ID3D11UnorderedAccessView* u[8]{};n=0;for(auto* x:uav) u[n++]=x;context_->CSSetUnorderedAccessViews(0,8,u,nullptr);
    };
    const auto go=[&](Stage s,UINT w,UINT h) {context_->CSSetShader(cs_[s].Get(),nullptr,0);context_->Dispatch((w+7)/8,(h+7)/8,1);};
    collect_profile();
    Probe* probe=nullptr;
    if(profile) {
        Probe& p=probes_[probe_next_];
        if(!p.disjoint) {
            D3D11_QUERY_DESC q{};q.Query=D3D11_QUERY_TIMESTAMP_DISJOINT;checked(device_->CreateQuery(&q,&p.disjoint),"profile query");
            q.Query=D3D11_QUERY_TIMESTAMP;for(auto& t:p.ts) checked(device_->CreateQuery(&q,&t),"profile query");
            D3D11_BUFFER_DESC d{};stats_.b->GetDesc(&d);d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
            checked(device_->CreateBuffer(&d,nullptr,&p.stats),"stats read-back");
        }
        if(!p.waiting) {probe=&p;probe_next_=(probe_next_+1)%probes_.size();context_->Begin(p.disjoint.Get());}
    }
    const auto mark=[&](int n) {if(probe) context_->End(probe->ts[n].Get());};
    mark(0);
    const UINT zero[4]{};
    // 1 motion, 2 repair (0 -> 1)
    context_->ClearUnorderedAccessViewUint(votes_.uav.Get(),zero);
    // 0 scatter (fresh movers -> kept texels): keys in keys 1, their motion in motion 1 (both free until the repair and the crack)
    context_->ClearUnorderedAccessViewUint(motion_[1].uav.Get(),zero);   // (a structured buffer: the bit pattern 0 = 0.0f)
    if(motions) {
        context_->ClearUnorderedAccessViewUint(keys_[1].uav.Get(),zero);
        bind({},{nullptr,keys_[1].uav.Get()});go(scatter,fw,fh);
        bind({nullptr,keys_[1].srv.Get()},{motion_[1].uav.Get()});go(scatter_write,fw,fh);
    }
    mark(1);
    bind({motion_[1].srv.Get()},{motion_[0].uav.Get(),nullptr,nullptr,nullptr,nullptr,nullptr,votes_.uav.Get()});go(motion,kw,kh);mark(2);
    bind({motion_[0].srv.Get()},{motion_[1].uav.Get()});go(repair,kw,kh);mark(3);
    // 3 splat (keys 0), 4 origins (origins 0); fresh rig keys
    context_->ClearUnorderedAccessViewUint(keys_[0].uav.Get(),zero);context_->ClearUnorderedAccessViewUint(origins_[0].uav.Get(),zero);
    context_->ClearUnorderedAccessViewUint(fresh_keys_.uav.Get(),zero);context_->ClearUnorderedAccessViewUint(holes_.uav.Get(),zero);
    bind({motion_[1].srv.Get(),nullptr,nullptr,nullptr,nullptr,votes_.srv.Get()},{nullptr,keys_[0].uav.Get()});go(splat,kw,kh);mark(4);
    bind({motion_[1].srv.Get(),keys_[0].srv.Get(),nullptr,nullptr,nullptr,votes_.srv.Get()},{nullptr,nullptr,nullptr,origins_[0].uav.Get()});go(origins,kw,kh);mark(5);
    const bool fresh_hands=hands && (L.hand_moving[0] || L.hand_moving[1]);
    if(fresh_hands || motions) {bind({nullptr,nullptr,nullptr,nullptr,nullptr,votes_.srv.Get()},{nullptr,nullptr,nullptr,nullptr,fresh_keys_.uav.Get()});go(fresh_splat,fw,fh);}
    mark(6);
    // 5 crack (0 -> 1), 6 fill: jump flood 32 .. 1 (ping-pong)
    bind({nullptr,keys_[0].srv.Get(),nullptr,origins_[0].srv.Get()},{landed_[0].uav.Get(),keys_[1].uav.Get(),landed_[1].uav.Get(),origins_[1].uav.Get(),nullptr,holes_.uav.Get(),near_[0].uav.Get(),near_[1].uav.Get()});
    {ID3D11UnorderedAccessView* t=tiles_.uav.Get();const UINT empty=0;context_->CSSetUnorderedAccessViews(4,1,&t,&empty);}   // (the tile list starts empty)
    go(crack,kw,kh);
    bind({},{});
    context_->CopyStructureCount(fill_args_.Get(),0,tiles_.uav.Get());   // the fill's groups: one per listed tile
    context_->CopyResource(keys_[0].b.Get(),keys_[1].b.Get());   // (the crack reads these two around each texel: copied)
    context_->CopyResource(origins_[0].b.Get(),origins_[1].b.Get());
    mark(7);
    int from=1;
    for(int step=512;step>=1;step/=2) {      // ponytail: 10 passes cover a hole 1023 texels across (v1197: over the hole tiles only); a GPU-side step bound if the fill still costs
        upload(step,step==1);
        const int to=1-from;
        bind({nullptr,keys_[from].srv.Get(),landed_[from].srv.Get(),origins_[from].srv.Get(),tiles_.srv.Get(),votes_.srv.Get(),near_[from].srv.Get()},
             {nullptr,keys_[to].uav.Get(),landed_[to].uav.Get(),origins_[to].uav.Get(),nullptr,holes_.uav.Get(),nullptr,near_[to].uav.Get()});
        context_->CSSetShader(cs_[fill].Get(),nullptr,0);context_->DispatchIndirect(fill_args_.Get(),0);
        from=to;
    }
    mark(8);
    // 7 the resolve: its decisions per target texel (render size; v1190: the rig masks and the hand votes too, the rig target's colour rules)
    bind({},{});
    ID3D11ShaderResourceView* ps_in[15]{kept,kept_depth,fresh,fresh_depth,inputs[4],inputs[5],inputs[6],inputs[7],motion_[1].srv.Get(),keys_[from].srv.Get(),landed_[from].srv.Get(),
                                        origins_[from].srv.Get(),fresh_keys_.srv.Get(),votes_.srv.Get(),near_[from].srv.Get()};
    context_->CSSetShaderResources(0,15,ps_in);
    context_->ClearUnorderedAccessViewUint(stats_.uav.Get(),zero);
    {ID3D11UnorderedAccessView* u[2]{decisions_.uav.Get(),stats_.uav.Get()};context_->CSSetUnorderedAccessViews(0,2,u,nullptr);}
    go(decide,kw,kh);mark(9);
    bind({},{});ID3D11ShaderResourceView* none8[8]{};context_->CSSetShaderResources(0,8,none8);context_->CSSetShader(nullptr,nullptr,0);
    // 8 the display pass: each pixel samples where its texel's decision says
    const D3D11_VIEWPORT vp{0,0,float(width),float(height),0,1};
    context_->RSSetViewports(1,&vp);context_->RSSetState(raster_.Get());context_->IASetInputLayout(nullptr);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);context_->OMSetDepthStencilState(depth_.Get(),0);context_->OMSetBlendState(nullptr,nullptr,0xffffffff);
    context_->GSSetShader(nullptr,nullptr,0);context_->HSSetShader(nullptr,nullptr,0);context_->DSSetShader(nullptr,nullptr,0);
    context_->PSSetShaderResources(0,15,ps_in);
    {ID3D11ShaderResourceView* d=decisions_.srv.Get();context_->PSSetShaderResources(16,1,&d);}
    auto* rtv=output_rtv_.Get();context_->OMSetRenderTargets(1,&rtv,nullptr);
    context_->VSSetShader(vs_.Get(),nullptr,0);context_->PSSetShader(ps_.Get(),nullptr,0);context_->Draw(3,0);
    mark(10);if(probe) {context_->CopyResource(probe->stats.Get(),stats_.b.Get());context_->End(probe->disjoint.Get());probe->waiting=true;}
    ID3D11ShaderResourceView* none13[17]{};context_->PSSetShaderResources(0,17,none13);context_->OMSetRenderTargets(0,nullptr,nullptr);
    ++passes;
    return output_.Get();
}
} // namespace d2vr
