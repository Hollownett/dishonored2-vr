#pragma once
// Spacewarp generation pass (docs/SPACEWARP-PLAN-2026-11-19.md section 4; the GPU port of tools/spacewarp_replay.cpp's `sw`, whose
// rules section 9 measured): one eye's rendered image extrapolated forward to another camera and moment.
//
// Per source texel (render size) a 3D point from the reverse-Z depth, moved by its class:
//   rig texel (rig mask nonzero) within a drawn palm's reach: that hand's rigid motion (the controller's change since the render);
//   v1418, a held thing's far part (a sword's blade, a gun's barrel: past the palm's reach, within hand_far of it, ahead of the grip) that
//   the previous real frame confirms moved with that hand and did not stand still in the world: that hand's rigid motion too;
//   v1419, anything within a hand's weapon body (the sword's .65 m blade line, a gun's barrel: SpacewarpParams::weapon): that hand's motion;
//   other rig texel: its own motion texture if it has one (w >= 0.5 px), else the rest-of-rig rigid motion (the play space's step);
//   world texel with object motion (w >= mover_px, 3 px): its motion texture's world displacement times `motion_share`;
//   still world: not moved (exact parallax from the camera change alone); sky (depth 0): a direction, turned only.
// Then: Splat (depth test as InterlockedMax on 1/z, a second pass writes the winning texel's index), Crack (a cell between two cells of
// one surface that is empty or farther than both takes the nearer: AFW v2's Crack), Fill (each empty cell takes the farthest of the
// first surfaces it meets in 8 directions: the background is stretched, never the foreground), Resolve (each output pixel's cell depth back-projected into the source
// image, bilinear in linear light through an _SRGB view; filled cells show their seed's own colour; cells nothing reached fall back to
// a rotation-only look-up). The output is drawn into a render target view, so it can be an XR swapchain image directly.
//
// Coordinates: OpenXR camera axes (x right, y up, z back: -z ahead), metres; image u,v in 0..1 with v down; FOVs as tangents. The
// target camera is given in source camera coordinates: columns of m = its axes, c = its centre (a source point P is m^T (P - c) there).
// Rigid motions act on source camera points: P' = r P + t.
//
// Runs on the caller's context inside its own state scope (the caller saves and restores state). Lab: tests/spacewarp_pass_check.cpp;
// replay against the CPU prototype: tools/spacewarp_replay.cpp (SW_GPU=1).
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>

namespace d2vr {
struct SpacewarpRigid {std::array<double,9> r{1,0,0,0,1,0,0,0,1};std::array<double,3> t{};};
struct SpacewarpInputs {
    ID3D11ShaderResourceView* colour{};  // the source eye image (display size), an _SRGB (or float) view: sampled in linear light
    ID3D11ShaderResourceView* depth{};   // R32_FLOAT reverse-Z device depth; the render rectangle (width x height) at the top left
    ID3D11ShaderResourceView* motion{};  // optional: RGBA16F (eye_motion_pass.hpp), same rectangle
    ID3D11ShaderResourceView* rig{};     // optional: R8_UINT rig mask (rig_mask.hpp), same rectangle
    unsigned width{},height{};           // the render rectangle
    ID3D11ShaderResourceView* prev_depth{};unsigned prev_width{},prev_height{};  // v1418, optional: the previous real frame's depth (R32_FLOAT) and its rectangle
};
struct SpacewarpParams {
    std::array<double,4> source_tan{},target_tan{};  // tan(left), tan(right), tan(up), tan(down)
    double m10{},m11{};                               // distance ahead = m11 / (depth + m10)
    std::array<double,9> m{1,0,0,0,1,0,0,0,1};       // the target camera in source camera coordinates (row-major; columns = its axes)
    std::array<double,3> c{};
    std::array<double,9> world_to_source{1,0,0,0,1,0,0,0,1}; // rows: the source camera's axes in the world (motion texture xyz -> camera)
    double motion_share{};                            // share of the motion texture's span to apply (0: movers stay)
    std::array<SpacewarpRigid,2> hand{};std::array<bool,2> hand_on{};
    std::array<std::array<double,4>,2> palm{};        // each drawn palm in source camera coordinates (OpenXR axes) and the reach it owns
    SpacewarpRigid rest{};bool rest_on{};             // the rest of the rig
    double rig_near{};                                // no rig mask: texels nearer than this (metres) or within a palm's reach are rig
    bool encode_srgb{};                               // the target stores sRGB-encoded values in a UNORM format: encode the output
    // The colour image's own camera when it is not the depth's: a canted render is resampled to the straight view before submission
    // (game_xr.hpp, v808: same eye position, the cant turned back off, the runtime's FOV), while the depth stays canted. colour_m: its
    // axes in source camera coordinates (columns, row-major); colour_tan: its FOV. Off: the colour image is the source camera's.
    bool colour_camera{};std::array<double,9> colour_m{1,0,0,0,1,0,0,0,1};std::array<double,4> colour_tan{};
    // v1418 (headset 2026-11-19: the tips of a sword, a gun and a crossbow doubled; the part near the hand was fine): a held thing reaches
    // past its palm's reach (sim dumps: held pixels 0.22-0.43 m from the palm, all ahead of the grip; the forearm 0.17-0.33 m behind it).
    // Without a rig mask its far part was scenery and stayed behind the moving hand. Now a point within hand_far of the nearest drawn palm
    // (past its reach), at most -hand_back behind that hand's grip along the grip's forward axis (grip: axis in source coordinates, w = the
    // axis . the grip's origin), is that hand's when the previous real frame of this eye confirms it: its depth shows a surface where the
    // hand's last step puts the point (prev_hand: source camera -> the previous frame's camera, the point carried back with the hand) and
    // not where it would be had it stood still in the world (prev_world). A wall or a table near the hand is confirmed still and stays; an
    // NPC that does not move with the hand is not the hand's. prev_tan, prev_m10/m11: that render's FOV and depth terms. Off: palm reach only.
    // v1419 (headset on v1418: the sword's tip still doubled): the mod's sword blade is .65 m from the grip (dxgi_probe sword_blade_length),
    // past v1418's .5 m; hand_far is now .8 m, and the hand's own test allows .015 m per metre from the palm (a few tenths of a degree
    // between the drawn grip and the drawn weapon), the still-world test stays exact.
    bool prev_on{};double hand_far{.8},hand_back{-.12};std::array<double,4> prev_tan{};double prev_m10{},prev_m11{};
    SpacewarpRigid prev_world{};std::array<SpacewarpRigid,2> prev_hand{};std::array<bool,2> prev_hand_on{};std::array<std::array<double,4>,2> grip{};
    bool debug_tint_held{};  // v1418 diagnostics (build/spacewarp-tint-held): pixels whose texel the held-far-part rule gave a hand are tinted magenta
    // v1419: each hand's weapon body in source camera coordinates: a (xyz), b (xyz), radius (0: none). Everything within it is that hand's,
    // unconditionally, like its palm's reach: the weapon follows the hand the way the hand does (user, headset v1418: "make the weapon
    // interpolate the same way as the arms").
    std::array<std::array<double,7>,2> weapon{};
};

class SpacewarpPass {
public:
    static constexpr int fill_passes=2;static constexpr std::uint32_t fill_reach=48;  // texels each fill pass searches per direction
    explicit SpacewarpPass(ID3D11Device* device);
    // Draws the extrapolated image into `target` (out_width x out_height). false: a resource could not be made (nothing drawn).
    bool run(ID3D11DeviceContext* c,const SpacewarpInputs& in,const SpacewarpParams& p,ID3D11RenderTargetView* target,unsigned out_width,unsigned out_height);
    std::uint64_t passes{};
private:
    template<class T> using Ptr=Microsoft::WRL::ComPtr<T>;
    struct Constants {
        float source_tan[4],target_tan[4],grid[4],terms[4];  // terms: m10, m11, motion_share, mover_px
        float M[3][4],C[4],W2S[3][4],H0[3][4],H1[3][4],RR[3][4],palm0[4],palm1[4];
        std::uint32_t flags[4];                               // hand0, hand1, rest, motion | rig << 1
        float out[4];
        std::uint32_t step[4];                                // crack/fill: x pass parity (unused), y distance; z encode sRGB
        float CM[3][4],colour_tan[4];                         // the colour image's camera (rows of colour_m) and FOV
        float PW[3][4],PH0[3][4],PH1[3][4],prev_tan[4],prev_terms[4],prev_grid[4],grip0[4],grip1[4];  // v1418; terms: m10, m11, hand_far, hand_back
        std::uint32_t prev_flags[4];                          // v1418: on, hand0, hand1
        float wa0[4],wb0[4],wa1[4],wb1[4];                    // v1419: the weapon bodies (wa.w: radius, 0 none)
    };
    static_assert(sizeof(Constants)%16==0,"constant buffer size");
    struct Grid {Ptr<ID3D11Texture2D> t;Ptr<ID3D11UnorderedAccessView> u;Ptr<ID3D11ShaderResourceView> s;};
    bool grids_(unsigned w,unsigned h);
    Ptr<ID3D11Device> device_;
    Ptr<ID3D11ComputeShader> clear_,splat_depth_,splat_index_,crack_,fill_;
    Ptr<ID3D11VertexShader> vs_;Ptr<ID3D11PixelShader> resolve_;
    Ptr<ID3D11Buffer> constants_;Ptr<ID3D11SamplerState> linear_;
    std::array<Grid,2> depth_,index_;  // ping-pong: [0] the splat's, [1] the other side of each crack/fill pass
    unsigned grid_w_{},grid_h_{};
};

inline SpacewarpPass::SpacewarpPass(ID3D11Device* device):device_(device) {
    const std::string source=R"HLSL(
cbuffer Sw : register(b0) {
    float4 srcTan; float4 dstTan; float4 grid; float4 terms;
    float4 M[3]; float4 C; float4 W2S[3]; float4 H0[3]; float4 H1[3]; float4 RR[3]; float4 palm0; float4 palm1;
    uint4 flags; float4 outSize; uint4 step; float4 CM[3]; float4 colourTan;
    float4 PW[3]; float4 PH0[3]; float4 PH1[3]; float4 prevTan; float4 prevTerms; float4 prevGrid; float4 grip0; float4 grip1; uint4 prevFlags;
    float4 wa0; float4 wb0; float4 wa1; float4 wb1;
};
Texture2D<float> depthTex : register(t0);
Texture2D<float4> motionTex : register(t1);
Texture2D<uint> rigTex : register(t2);
Texture2D<float4> colourTex : register(t3);
Texture2D<uint> depthIn : register(t4);
Texture2D<uint> indexIn : register(t5);
Texture2D<float> prevDepth : register(t6);
SamplerState linearClamp : register(s0);
RWTexture2D<uint> depthOut : register(u0);
RWTexture2D<uint> indexOut : register(u1);
static const uint EMPTY=0xffffffffu, SKY=0x80000000u, FILLED=0x40000000u, INDEX=0x3fffffffu;

float Ahead(float d) {return terms.y/(d+terms.x);}
float3 Ray(float4 t,float2 uv) {return float3(t.x+uv.x*(t.y-t.x),t.z+uv.y*(t.w-t.z),-1);}
float3 MulM(float3 v) {return float3(dot(M[0].xyz,v),dot(M[1].xyz,v),dot(M[2].xyz,v));}
float3 MulMT(float3 v) {return M[0].xyz*v.x+M[1].xyz*v.y+M[2].xyz*v.z;}
float3 MulCT(float3 v) {return CM[0].xyz*v.x+CM[1].xyz*v.y+CM[2].xyz*v.z;}  // a source-camera point or direction in the colour camera
bool ToImage(float4 t,float3 q,out float2 uv) {
    uv=0; if(!(q.z< -1e-6)) return false;
    const float2 a=q.xy/-q.z; uv=float2((a.x-t.x)/(t.y-t.x),(a.y-t.z)/(t.w-t.z)); return all(abs(uv)<1e6);
}
float3 Rigid(float4 R[3],float3 p) {return float3(dot(R[0].xyz,p)+R[0].w,dot(R[1].xyz,p)+R[1].w,dot(R[2].xyz,p)+R[2].w);}
float3 SourcePoint(uint2 t) {return Ray(srcTan,(float2(t)+.5)*grid.zw)*Ahead(depthTex[t]);}
// v1418: does the previous real frame's depth show a surface at P carried by R into that frame's camera? 1 yes, 0 no, -1 outside that
// image. Yes: P's distance there within 0.5 % + 3 mm of one of the 2 x 2 texels around it, or within their range where they lie on one
// smooth surface (a slope between texels), never across an edge.
int PrevShows(float4 R[3],float3 P,float slack) {
    const float3 q=Rigid(R,P); float2 s;
    if(!ToImage(prevTan,q,s) || any(s<0) || any(s>=1)) return -1;
    const int2 o=int2(floor(s*prevGrid.xy-.5));const float z=-q.z,tol=.005*z+.003+slack;
    float lo=1e30,hi=0;bool hit=false;
    [unroll] for(int k=0;k<4;++k) {
        const int2 c=clamp(o+int2(k&1,k>>1),int2(0,0),int2(prevGrid.xy)-1);
        const float d=prevDepth[c]; if(d<=1e-7) continue;  // sky: no surface there
        const float a=prevTerms.y/(d+prevTerms.x); lo=min(lo,a); hi=max(hi,a); hit=hit || abs(a-z)<=tol;
    }
    if(hit) return 1;
    return hi>0 && hi-lo<=.02*z+.01 && z>=lo-tol && z<=hi+tol?1:0;
}
// v1418: the hand (0, 1) that owns P as the far part of what it holds (SpacewarpParams::prev_on), else -1
int FarHand(float3 P,float d0,float d1) {
    if(prevFlags.x==0u) return -1;
    const bool zero=d0<=d1; const float ds=zero?d0:d1;
    if(ds>prevTerms.z || ds<=(zero?palm0.w:palm1.w)) return -1;
    const float slack=.015*ds;  // v1419: the drawn weapon vs the drawn grip, a few tenths of a degree
    if(zero) {
        if(flags.x==0u || prevFlags.y==0u || dot(P,grip0.xyz)-grip0.w<prevTerms.w || PrevShows(PH0,P,slack)!=1) return -1;
        return PrevShows(PW,P,0)==1?-1:0;
    }
    if(flags.y==0u || prevFlags.z==0u || dot(P,grip1.xyz)-grip1.w<prevTerms.w || PrevShows(PH1,P,slack)!=1) return -1;
    return PrevShows(PW,P,0)==1?-1:1;
}
// v1419: distance from p to the segment a-b
float SegmentDistance(float3 p,float3 a,float3 b) {const float3 ab=b-a;const float t=saturate(dot(p-a,ab)/max(dot(ab,ab),1e-12));return distance(p,a+t*ab);}
// v1419: the hand (0, 1) whose weapon body holds P, else -1
int WeaponHand(float3 P) {
    if(flags.x!=0u && wa0.w>0 && SegmentDistance(P,wa0.xyz,wb0.xyz)<=wa0.w) return 0;
    if(flags.y!=0u && wa1.w>0 && SegmentDistance(P,wa1.xyz,wb1.xyz)<=wa1.w) return 1;
    return -1;
}
// The texel's displacement in source camera metres (the rules in this header's comment)
float3 Displace(uint2 t,float3 P) {
    const float4 mo=(flags.w&1u)?motionTex[t]:0;
    const float3 motion=float3(dot(W2S[0].xyz,mo.xyz),dot(W2S[1].xyz,mo.xyz),dot(W2S[2].xyz,mo.xyz))*terms.z;
    const float d0=palm0.w>0?distance(P,palm0.xyz):1e9,d1=palm1.w>0?distance(P,palm1.xyz):1e9;
    int heldBy=WeaponHand(P);if(heldBy<0) heldBy=FarHand(P,d0,d1);
    if(heldBy==0) return Rigid(H0,P)-P;
    if(heldBy==1) return Rigid(H1,P)-P;
    // without a rig mask (true stereo): what lies within a drawn palm's reach, or nearer than C.w metres, is the player's rig
    const bool nearRig=C.w>0 && (length(P)<C.w || d0<=palm0.w || d1<=palm1.w);
    const uint rig=(flags.w&2u)?rigTex[t]:(nearRig?1u:0u);
    if(rig!=0u) {
        if(flags.x!=0u && d0<=palm0.w && d0<=d1) return Rigid(H0,P)-P;
        if(flags.y!=0u && d1<=palm1.w) return Rigid(H1,P)-P;
        if(mo.w>=.5 && terms.z>0) return motion;
        if(flags.z!=0u) return Rigid(RR,P)-P;
        return 0;
    }
    return mo.w>=terms.w?motion:0;
}
// The cell a source texel lands in and its 1/z bits (sky: 1, the farthest non-empty); false: outside the target or behind it
bool Land(uint2 t,out uint2 cell,out uint bits) {
    cell=0; bits=0;
    const float2 uv=(float2(t)+.5)*grid.zw; const float d=depthTex[t]; float3 q;
    if(d<=1e-7) {q=MulMT(Ray(srcTan,uv)); bits=1u;}
    else {const float3 P=Ray(srcTan,uv)*Ahead(d); q=MulMT(P+Displace(t,P)-C.xyz); bits=asuint(1/max(-q.z,1e-6));}
    float2 o; if(!ToImage(dstTan,q,o)) return false;
    const float2 f=floor(o*grid.xy); if(any(f<0) || any(f>=grid.xy)) return false;
    cell=uint2(f); return true;
}
[numthreads(8,8,1)] void Clear(uint3 id : SV_DispatchThreadID) {
    if(any(id.xy>=uint2(grid.xy))) return; depthOut[id.xy]=0; indexOut[id.xy]=EMPTY;
}
[numthreads(8,8,1)] void SplatDepth(uint3 id : SV_DispatchThreadID) {
    if(any(id.xy>=uint2(grid.xy))) return; uint2 cell; uint bits; if(Land(id.xy,cell,bits)) InterlockedMax(depthOut[cell],bits);
}
[numthreads(8,8,1)] void SplatIndex(uint3 id : SV_DispatchThreadID) {
    if(any(id.xy>=uint2(grid.xy))) return; uint2 cell; uint bits; if(!Land(id.xy,cell,bits) || bits!=depthOut[cell]) return;
    InterlockedMin(indexOut[cell],(id.y*uint(grid.x)+id.x)|(bits==1u?SKY:0u));
}
float Z(uint bits) {return bits>1u?1/asfloat(bits):1e30;}
bool Surface(uint idx) {return idx!=EMPTY && (idx&(SKY|FILLED))==0u;}
// Crack (distance step.y): from depthIn/indexIn to depthOut/indexOut
[numthreads(8,8,1)] void Crack(uint3 id : SV_DispatchThreadID) {
    const int2 p=int2(id.xy); if(any(p>=int2(grid.xy))) return;
    uint bd=depthIn[p],bi=indexIn[p]; const float zi=bi==EMPTY?1e30:Z(bd); float best=1e30;
    const int2 dirs[4]={int2(1,0),int2(0,1),int2(1,1),int2(1,-1)};
    for(int k=0;k<4;++k) for(int d=1;d<=2;++d) {
        const int2 a=p-dirs[k]*d,b=p+dirs[k]*d;
        if(any(a<0) || any(b<0) || any(a>=int2(grid.xy)) || any(b>=int2(grid.xy))) continue;
        const uint ia=indexIn[a],ib=indexIn[b]; if(!Surface(ia) || !Surface(ib)) continue;
        const uint da=depthIn[a],db=depthIn[b]; const float za=Z(da),zb=Z(db);
        if(abs(za-zb)>.1*min(za,zb) || !(zi>1.1*max(za,zb))) continue;
        const bool takeA=za<=zb; const float z=takeA?za:zb;
        if(z<best) {best=z;bd=takeA?da:db;bi=takeA?ia:ib;}
    }
    depthOut[p]=bd; indexOut[p]=bi;
}
// Fill: an empty cell looks for the first non-empty cell in 8 directions (up to step.y texels) and takes the farthest of them (the
// background behind what moved, never the foreground beside the hole); among seeds within 1 % of the same depth, the closest. Marked
// filled (a sky seed stays sky).
[numthreads(8,8,1)] void Fill(uint3 id : SV_DispatchThreadID) {
    const int2 p=int2(id.xy); if(any(p>=int2(grid.xy))) return;
    uint bd=depthIn[p],bi=indexIn[p];
    if(bi==EMPTY) {
        uint fd=0,fi=EMPTY; float fz=0; int fs=0;
        const int2 dirs[8]={int2(1,0),int2(-1,0),int2(0,1),int2(0,-1),int2(1,1),int2(-1,-1),int2(1,-1),int2(-1,1)};
        [loop] for(int k=0;k<8;++k) {
            [loop] for(int s=1;s<=int(step.y);++s) {
                const int2 n=p+dirs[k]*s; if(any(n<0) || any(n>=int2(grid.xy))) break;
                const uint ni=indexIn[n]; if(ni==EMPTY) continue;
                const uint nd=depthIn[n]; const float nz=Z(nd);
                if(fi==EMPTY || nz>fz*1.01 || (nz>=fz*.99 && s<fs)) {fd=nd;fi=ni;fz=nz;fs=s;}
                break;
            }
        }
        if(fi!=EMPTY) {bd=fd;bi=(fi&SKY)?fi:(fi|FILLED);}
    }
    depthOut[p]=bd; indexOut[p]=bi;
}
struct Vertex {float4 position : SV_POSITION; float2 uv : TEXCOORD0;};
Vertex FullVS(uint id : SV_VertexID) {Vertex v; v.uv=float2((id<<1)&2,id&2); v.position=float4(v.uv*float2(2,-2)+float2(-1,1),0,1); return v;}
float2 TexelUv(uint idx) {const uint i=idx&INDEX; return (float2(i%uint(grid.x),i/uint(grid.x))+.5)*grid.zw;}
// The source image shows this source-camera point: it falls inside it and the source depth there agrees within 2 %
bool Seen(float3 Pk,out uint2 tt) {
    float2 sk; tt=0; if(!ToImage(srcTan,Pk,sk) || any(sk<0) || any(sk>=1)) return false;
    tt=min(uint2(sk*grid.xy),uint2(grid.xy)-1); const float d=depthTex[tt]; if(d<=1e-7) return false;
    const float a=Ahead(d); return abs(a+Pk.z)<=.02*a;
}
float4 Resolve(Vertex v) : SV_TARGET {
    const float2 uv=v.position.xy*outSize.zw;
    const uint2 cell=min(uint2(uv*grid.xy),uint2(grid.xy)-1);
    const uint idx=depthIn[cell]!=0u?indexIn[cell]:EMPTY;
    // the point (for the sky and uncovered cells: the direction) in source camera coordinates whose colour this pixel shows: the pixel's
    // ray at the cell's depth, back into the source (minus the texel's displacement), wherever the source sees that point (its depth there
    // agrees within 2 %: also in a filled cell on a magnified surface, if its seed did not move); else the texel's own point (a hole behind something that moved
    // shows its background seed, never the foreground in front of it). Then looked up in the colour image through its own camera.
    float3 P;bool tint=false;
    if(idx==EMPTY || (idx&SKY)) P=MulM(Ray(dstTan,uv));
    else {
        const uint i=idx&INDEX; const uint2 t=uint2(i%uint(grid.x),i/uint(grid.x)); const float3 Ps=SourcePoint(t);
        if(step.w!=0u) tint=WeaponHand(Ps)>=0 || FarHand(Ps,palm0.w>0?distance(Ps,palm0.xyz):1e9,palm1.w>0?distance(Ps,palm1.xyz):1e9)>=0;
        const float3 d=Displace(t,Ps),Pk=MulM(Ray(dstTan,uv)*Z(depthIn[cell]))+C.xyz-d;
        // a filled cell takes the back-projected point only from still parts of the source (its seed and the texel it lands on): the
        // place a mover left shows its seed's colour, never the mover seen again
        uint2 tt; bool use=Seen(Pk,tt);
        if(use && (idx&FILLED)) use=all(d==0) && all(Displace(tt,SourcePoint(tt))==0);
        P=use?Pk:Ps;
    }
    float2 s; if(!ToImage(colourTan,MulCT(P),s)) s=uv;
    float3 rgb=colourTex.SampleLevel(linearClamp,saturate(s),0).rgb;
    if(tint) rgb=lerp(rgb,float3(1,0,1),.6);
    if(step.z!=0u) rgb=rgb<=.0031308?rgb*12.92:1.055*pow(max(rgb,1e-8),1/2.4)-.055;  // a UNORM target holding sRGB values
    return float4(rgb,1);
}
)HLSL";
    const auto compile=[&](const char* entry,const char* target) {
        Ptr<ID3DBlob> blob,errors;
        if(FAILED(D3DCompile(source.data(),source.size(),"spacewarp",nullptr,nullptr,entry,target,D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&blob,&errors)))
            throw std::runtime_error(std::string("spacewarp shader ")+entry+": "+(errors?static_cast<const char*>(errors->GetBufferPointer()):"compile failed"));
        return blob;
    };
    const auto cs=[&](const char* entry,Ptr<ID3D11ComputeShader>& out) {
        const auto b=compile(entry,"cs_5_0");
        if(FAILED(device_->CreateComputeShader(b->GetBufferPointer(),b->GetBufferSize(),nullptr,&out))) throw std::runtime_error(std::string("spacewarp CS ")+entry);
    };
    cs("Clear",clear_);cs("SplatDepth",splat_depth_);cs("SplatIndex",splat_index_);cs("Crack",crack_);cs("Fill",fill_);
    {const auto b=compile("FullVS","vs_5_0");if(FAILED(device_->CreateVertexShader(b->GetBufferPointer(),b->GetBufferSize(),nullptr,&vs_))) throw std::runtime_error("spacewarp VS");}
    {const auto b=compile("Resolve","ps_5_0");if(FAILED(device_->CreatePixelShader(b->GetBufferPointer(),b->GetBufferSize(),nullptr,&resolve_))) throw std::runtime_error("spacewarp PS");}
    D3D11_BUFFER_DESC cb{};cb.ByteWidth=sizeof(Constants);cb.Usage=D3D11_USAGE_DYNAMIC;cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;cb.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
    if(FAILED(device_->CreateBuffer(&cb,nullptr,&constants_))) throw std::runtime_error("spacewarp constants");
    D3D11_SAMPLER_DESC sd{};sd.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sd.MaxLOD=D3D11_FLOAT32_MAX;
    if(FAILED(device_->CreateSamplerState(&sd,&linear_))) throw std::runtime_error("spacewarp sampler");
}

inline bool SpacewarpPass::grids_(unsigned w,unsigned h) {
    if(w==grid_w_ && h==grid_h_ && depth_[0].t) return true;
    for(auto* set:{&depth_,&index_}) for(auto& g:*set) g={};
    grid_w_=grid_h_=0;
    D3D11_TEXTURE2D_DESC d{};d.Width=w;d.Height=h;d.MipLevels=1;d.ArraySize=1;d.Format=DXGI_FORMAT_R32_UINT;d.SampleDesc.Count=1;
    d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;
    for(auto* set:{&depth_,&index_}) for(auto& g:*set)
        if(FAILED(device_->CreateTexture2D(&d,nullptr,&g.t)) || FAILED(device_->CreateUnorderedAccessView(g.t.Get(),nullptr,&g.u)) ||
           FAILED(device_->CreateShaderResourceView(g.t.Get(),nullptr,&g.s))) return false;
    grid_w_=w;grid_h_=h;return true;
}

inline bool SpacewarpPass::run(ID3D11DeviceContext* c,const SpacewarpInputs& in,const SpacewarpParams& p,ID3D11RenderTargetView* target,
                               unsigned out_width,unsigned out_height) {
    if(!c || !in.colour || !in.depth || !target || !in.width || !in.height || !out_width || !out_height || in.width*static_cast<std::uint64_t>(in.height)>0x3fffffffull) return false;
    if(!grids_(in.width,in.height)) return false;
    Constants k{};
    const auto f4=[](float* o,double a,double b,double cc,double d) {o[0]=static_cast<float>(a);o[1]=static_cast<float>(b);o[2]=static_cast<float>(cc);o[3]=static_cast<float>(d);};
    f4(k.source_tan,p.source_tan[0],p.source_tan[1],p.source_tan[2],p.source_tan[3]);f4(k.target_tan,p.target_tan[0],p.target_tan[1],p.target_tan[2],p.target_tan[3]);
    f4(k.grid,in.width,in.height,1.0/in.width,1.0/in.height);f4(k.terms,p.m10,p.m11,p.motion_share,3.0);
    for(int i=0;i<3;++i) {
        f4(k.M[i],p.m[i*3],p.m[i*3+1],p.m[i*3+2],0);f4(k.W2S[i],p.world_to_source[i*3],p.world_to_source[i*3+1],p.world_to_source[i*3+2],0);
        f4(k.H0[i],p.hand[0].r[i*3],p.hand[0].r[i*3+1],p.hand[0].r[i*3+2],p.hand[0].t[i]);f4(k.H1[i],p.hand[1].r[i*3],p.hand[1].r[i*3+1],p.hand[1].r[i*3+2],p.hand[1].t[i]);
        f4(k.RR[i],p.rest.r[i*3],p.rest.r[i*3+1],p.rest.r[i*3+2],p.rest.t[i]);
    }
    f4(k.C,p.c[0],p.c[1],p.c[2],in.rig?0:p.rig_near);f4(k.palm0,p.palm[0][0],p.palm[0][1],p.palm[0][2],p.palm[0][3]);f4(k.palm1,p.palm[1][0],p.palm[1][1],p.palm[1][2],p.palm[1][3]);
    k.flags[0]=p.hand_on[0];k.flags[1]=p.hand_on[1];k.flags[2]=p.rest_on;k.flags[3]=(in.motion?1u:0u)|(in.rig?2u:0u);
    f4(k.out,out_width,out_height,1.0/out_width,1.0/out_height);k.step[2]=p.encode_srgb?1u:0u;k.step[3]=p.debug_tint_held?1u:0u;
    {const auto& cm=p.colour_camera?p.colour_m:std::array<double,9>{1,0,0,0,1,0,0,0,1};const auto& ct=p.colour_camera?p.colour_tan:p.source_tan;
     for(int i=0;i<3;++i) f4(k.CM[i],cm[i*3],cm[i*3+1],cm[i*3+2],0);f4(k.colour_tan,ct[0],ct[1],ct[2],ct[3]);}
    f4(k.wa0,p.weapon[0][0],p.weapon[0][1],p.weapon[0][2],p.weapon[0][6]);f4(k.wb0,p.weapon[0][3],p.weapon[0][4],p.weapon[0][5],0);  // v1419
    f4(k.wa1,p.weapon[1][0],p.weapon[1][1],p.weapon[1][2],p.weapon[1][6]);f4(k.wb1,p.weapon[1][3],p.weapon[1][4],p.weapon[1][5],0);
    if(p.prev_on && in.prev_depth && in.prev_width && in.prev_height) {  // v1418
        for(int i=0;i<3;++i) {
            f4(k.PW[i],p.prev_world.r[i*3],p.prev_world.r[i*3+1],p.prev_world.r[i*3+2],p.prev_world.t[i]);
            f4(k.PH0[i],p.prev_hand[0].r[i*3],p.prev_hand[0].r[i*3+1],p.prev_hand[0].r[i*3+2],p.prev_hand[0].t[i]);
            f4(k.PH1[i],p.prev_hand[1].r[i*3],p.prev_hand[1].r[i*3+1],p.prev_hand[1].r[i*3+2],p.prev_hand[1].t[i]);
        }
        f4(k.prev_tan,p.prev_tan[0],p.prev_tan[1],p.prev_tan[2],p.prev_tan[3]);f4(k.prev_terms,p.prev_m10,p.prev_m11,p.hand_far,p.hand_back);
        f4(k.prev_grid,in.prev_width,in.prev_height,0,0);f4(k.grip0,p.grip[0][0],p.grip[0][1],p.grip[0][2],p.grip[0][3]);f4(k.grip1,p.grip[1][0],p.grip[1][1],p.grip[1][2],p.grip[1][3]);
        k.prev_flags[0]=1;k.prev_flags[1]=p.prev_hand_on[0];k.prev_flags[2]=p.prev_hand_on[1];
    }
    const auto upload=[&](std::uint32_t distance) {
        k.step[1]=distance;D3D11_MAPPED_SUBRESOURCE m{};
        if(FAILED(c->Map(constants_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&m))) return false;
        std::memcpy(m.pData,&k,sizeof k);c->Unmap(constants_.Get(),0);return true;
    };
    if(!upload(1)) return false;
    ID3D11Buffer* cbs[]{constants_.Get()};c->CSSetConstantBuffers(0,1,cbs);c->PSSetConstantBuffers(0,1,cbs);
    ID3D11ShaderResourceView* inputs[4]{in.depth,in.motion,in.rig,in.colour};c->CSSetShaderResources(0,4,inputs);
    ID3D11ShaderResourceView* previous[1]{k.prev_flags[0]?in.prev_depth:nullptr};c->CSSetShaderResources(6,1,previous);
    const UINT gx=(in.width+7)/8,gy=(in.height+7)/8;
    const auto bind_out=[&](int side) {ID3D11UnorderedAccessView* u[2]{depth_[side].u.Get(),index_[side].u.Get()};c->CSSetUnorderedAccessViews(0,2,u,nullptr);};
    const auto bind_in=[&](int side) {ID3D11ShaderResourceView* s[2]{side>=0?depth_[side].s.Get():nullptr,side>=0?index_[side].s.Get():nullptr};c->CSSetShaderResources(4,2,s);};
    bind_in(-1);bind_out(0);
    c->CSSetShader(clear_.Get(),nullptr,0);c->Dispatch(gx,gy,1);
    c->CSSetShader(splat_depth_.Get(),nullptr,0);c->Dispatch(gx,gy,1);
    c->CSSetShader(splat_index_.Get(),nullptr,0);c->Dispatch(gx,gy,1);
    int side=0;  // the grid holding the latest result
    const auto step=[&](ID3D11ComputeShader* shader) {
        ID3D11UnorderedAccessView* none[2]{};c->CSSetUnorderedAccessViews(0,2,none,nullptr);
        bind_in(side);bind_out(1-side);c->CSSetShader(shader,nullptr,0);c->Dispatch(gx,gy,1);bind_in(-1);side=1-side;
    };
    step(crack_.Get());step(crack_.Get());
    for(int i=0;i<fill_passes;++i) {if(!upload(fill_reach)) return false;step(fill_.Get());}  // a second pass reaches on from filled seeds
    {ID3D11UnorderedAccessView* none[2]{};c->CSSetUnorderedAccessViews(0,2,none,nullptr);ID3D11ShaderResourceView* nones[7]{};c->CSSetShaderResources(0,7,nones);c->CSSetShader(nullptr,nullptr,0);}
    // resolve at the output size
    D3D11_VIEWPORT vp{0,0,static_cast<float>(out_width),static_cast<float>(out_height),0,1};c->RSSetViewports(1,&vp);
    c->OMSetRenderTargets(1,&target,nullptr);c->OMSetBlendState(nullptr,nullptr,0xffffffff);c->OMSetDepthStencilState(nullptr,0);c->RSSetState(nullptr);
    c->IASetInputLayout(nullptr);c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    c->HSSetShader(nullptr,nullptr,0);c->DSSetShader(nullptr,nullptr,0);c->GSSetShader(nullptr,nullptr,0);
    c->VSSetShader(vs_.Get(),nullptr,0);c->PSSetShader(resolve_.Get(),nullptr,0);
    ID3D11ShaderResourceView* ps[7]{in.depth,in.motion,in.rig,in.colour,depth_[side].s.Get(),index_[side].s.Get(),previous[0]};c->PSSetShaderResources(0,7,ps);
    ID3D11SamplerState* samplers[]{linear_.Get()};c->PSSetSamplers(0,1,samplers);
    c->Draw(3,0);
    ID3D11ShaderResourceView* nones[7]{};c->PSSetShaderResources(0,7,nones);
    ++passes;return true;
}
} // namespace d2vr
