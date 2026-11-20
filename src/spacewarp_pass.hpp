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
#include "shader_cache.hpp"
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
    bool debug_tint_classes{};  // v1432 diagnostics (build/spacewarp-tint-classes): every texel by class: hands green, rest of rig yellow, movers cyan, body magenta
    // v1419: each hand's weapon body in source camera coordinates: a (xyz), b (xyz), radius (0: none). Everything within it is that hand's,
    // unconditionally, like its palm's reach: the weapon follows the hand the way the hand does (user, headset v1418: "make the weapon
    // interpolate the same way as the arms").
    std::array<std::array<double,7>,2> weapon{};
    // v1423 (user, headset v1422: "corpses artifact a lot while walking"): what the player carries (a body on the shoulder, a dragged corpse,
    // the player's own body) moves with the play space. A point between rig_near and carry_reach whose previous real frame shows it where
    // the play space's step puts it (prev_rest: source camera -> the previous camera, the point carried back with the play space), and not
    // where it would be had it stood still in the world (prev_world), takes the rest-of-rig motion. Needs prev_on.
    bool carry_on{};SpacewarpRigid prev_rest{};double carry_reach{1.6},carry_slack{.03};
    // v1424: rims and moving edges (the Edge pass). Off (build/spacewarp-legacy-edges): every texel drawn with its own colour, as v1423.
    bool soft_edges{true};
    // v1426: a mover's extrapolated displacement is capped (metres): walking NPCs (1.5 m/s: 1.7 cm a slot) pass, a sword swing that reverses
    // between real frames (5-10 m/s) cannot overshoot far (sim: a guard's raised arm carried up while the next real frame swung it down)
    double mover_cap{.03};
    // v1429: the colour is resampled with Catmull-Rom (5 bilinear taps) clamped to the 2x2 texels around the point, not bilinear: a generated
    // frame as sharp as the real ones (bilinear at fractional positions softened fine detail every other frame: 45 Hz shimmer). Off: bilinear.
    bool sharp_resample{true};
    // v1432 (user, headset v1431: "player body (especially legs) still has a lot of ghosting"): the player's own body. v1431 had no rule for
    // it: legs past the carried rule's 1.6 m (the feet, looking down) were still world, and the carried test itself fails inside a walking
    // leg (the leg overlaps its own previous place, so the previous frame "confirms it still"): parts of a leg stayed behind every generated
    // frame while the rest moved on. The engine draws the player's body without motion vectors (sim class tint 2026-11-20: w < 1 px over the
    // legs), so the body is found by its place instead: inside the player's own vertical cylinder (axis through body_axis, the player model's
    // root at the feet, source camera coordinates; body_radius about the collision capsule's, so no wall or crate stands inside it), more
    // than body_floor above the root (the floor and a planted foot stay still) and lower than body_top (below the head), outside both palms'
    // reach, and not shown as scenery by the previous real frame (still in the world and not carried with the player: rubble above the
    // root). Such a texel takes the play space's step (walking, turning: class 3), or where the engine does give it a motion vector that
    // stays within body_cap of that step (its own animation), its full extrapolated motion (class 5). A motion vector further off (a
    // guard's sword swinging beside the player) leaves it to the other rules (a capped mover).
    bool body_on{};std::array<double,3> body_up{0,1,0},body_axis{};double body_radius{.35},body_floor{.06},body_top{1.4},body_cap{.08};
};

class SpacewarpPass {
public:
    // v1429: three passes (144 texels): a close hand moving fast (5 m/s at 0.4 m: ~125 render texels a slot) left the middle of its hole
    // unfilled, and an unfilled cell looks its direction up in the source: the hand at its old place
    static constexpr int fill_passes=3;static constexpr std::uint32_t fill_reach=48;  // texels each fill pass searches per direction
    explicit SpacewarpPass(ID3D11Device* device);
    static const std::string& shader_source();
    static void warm_cache() noexcept;  // v1428: fill the shader cache off the main thread
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
        std::uint32_t step[4];                                // x v1424 soft edges on; crack/fill: y distance; z encode sRGB; w tint
        float CM[3][4],colour_tan[4];                         // the colour image's camera (rows of colour_m) and FOV
        float PW[3][4],PH0[3][4],PH1[3][4],prev_tan[4],prev_terms[4],prev_grid[4],grip0[4],grip1[4];  // v1418; terms: m10, m11, hand_far, hand_back
        std::uint32_t prev_flags[4];                          // v1418: on, hand0, hand1
        float wa0[4],wb0[4],wa1[4],wb1[4];                    // v1419: the weapon bodies (wa.w: radius, 0 none)
        float PR[3][4],carry[4];                              // v1423: carried-with-the-player test; carry: reach, slack per metre, on
        float misc[4];                                        // v1426: x mover cap (metres)
        float body_up[4],body[4],body_axis[4];                // v1432: up (w on); x radius, y floor, z top, w cap; the root (source)
    };
    static_assert(sizeof(Constants)%16==0,"constant buffer size");
    struct Grid {Ptr<ID3D11Texture2D> t;Ptr<ID3D11UnorderedAccessView> u;Ptr<ID3D11ShaderResourceView> s;};
    bool grids_(unsigned w,unsigned h);
    Ptr<ID3D11Device> device_;
    Ptr<ID3D11ComputeShader> clear_,splat_depth_,splat_index_,crack_,fill_,classify_,edge_,blend_cs_,blur_cs_;
    Ptr<ID3D11VertexShader> vs_;Ptr<ID3D11PixelShader> resolve_;
    Ptr<ID3D11Buffer> constants_;Ptr<ID3D11SamplerState> linear_;
    std::array<Grid,2> depth_,index_;  // ping-pong: [0] the splat's, [1] the other side of each crack/fill pass
    std::array<Grid,2> word_;          // v1424: [0] Classify's classes, [1] Edge's texel words (render size, R32_UINT)
    Grid blend_;                       // v1424: each uncovered cell's blended background (render size, RGBA16F; w 1 = valid)
    Grid disp_;                        // v1425: each texel's displacement (render size, RGBA32F), Edge's output
    Grid blend2_;                      // v1425: BlurBlend's other side (RGBA16F)
    unsigned grid_w_{},grid_h_{};
};

inline const std::string& SpacewarpPass::shader_source() {static const std::string text=R"HLSL(
cbuffer Sw : register(b0) {
    float4 srcTan; float4 dstTan; float4 grid; float4 terms;
    float4 M[3]; float4 C; float4 W2S[3]; float4 H0[3]; float4 H1[3]; float4 RR[3]; float4 palm0; float4 palm1;
    uint4 flags; float4 outSize; uint4 step; float4 CM[3]; float4 colourTan;
    float4 PW[3]; float4 PH0[3]; float4 PH1[3]; float4 prevTan; float4 prevTerms; float4 prevGrid; float4 grip0; float4 grip1; uint4 prevFlags;
    float4 wa0; float4 wb0; float4 wa1; float4 wb1;
    float4 PR[3]; float4 carryTerms;
    float4 misc;
    float4 bodyUp; float4 body; float4 bodyAxis;  // v1432: the player's body cylinder (bodyUp.w on; body: radius, floor, top, cap; the root)
};
Texture2D<float> depthTex : register(t0);
Texture2D<float4> motionTex : register(t1);
Texture2D<uint> rigTex : register(t2);
Texture2D<float4> colourTex : register(t3);
Texture2D<uint> depthIn : register(t4);
Texture2D<uint> indexIn : register(t5);
Texture2D<float> prevDepth : register(t6);
Texture2D<uint> wordTex : register(t7);    // v1424: each source texel's word (Edge's output): class, tint, rim, edge and its inward direction
Texture2D<uint> classTex : register(t8);   // v1424: Classify's output (class, tint), Edge's input
SamplerState linearClamp : register(s0);
RWTexture2D<uint> depthOut : register(u0);
RWTexture2D<uint> indexOut : register(u1);
RWTexture2D<uint> wordOut : register(u2);
Texture2D<float4> blendTex : register(t9);     // v1424: Blend's output, read by Resolve
RWTexture2D<float4> blendOut : register(u3);
Texture2D<float4> blurIn : register(t11);      // v1425: BlurBlend's input (one direction per pass)
Texture2D<float4> dispTex : register(t10);     // v1425: each texel's displacement (Edge's output, the class's motion feathered on one surface)
RWTexture2D<float4> dispOut : register(u4);
static const uint EMPTY=0xffffffffu, SKY=0x80000000u, FILLED=0x40000000u, INDEX=0x3fffffffu;  // v1432: class 5 = the player's body
// v1424 texel word: bits 0-2 the class (0 still, 1 hand 0, 2 hand 1, 3 the rest of the rig, 4 its motion texture, 7 sky), bit 3 held-rule
// tint, bit 4 rim (a texel within EDGE_R of a nearer surface that moves differently: its colour is part that surface's), bit 5 a moving
// surface's edge (its colour is part what lies behind it), bits 6-8 that edge's inward direction (k * 45 degrees, texel x right, y down)
static const uint CLASS=7u, SKYCLASS=7u, TINT=8u, RIM=16u, FGEDGE=32u, FEATHER=512u;  // v1425: bit 9, the displacement is dispTex's
static const int EDGE_R=2;

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
// The texel's class (the rules in this header's comment; v1424: decided once per texel, its displacement from the class)
uint ClassOf(uint2 t,float3 P) {
    const float4 mo=(flags.w&1u)?motionTex[t]:0;
    const float d0=palm0.w>0?distance(P,palm0.xyz):1e9,d1=palm1.w>0?distance(P,palm1.xyz):1e9;
    int heldBy=WeaponHand(P);if(heldBy<0) heldBy=FarHand(P,d0,d1);
    if(heldBy==0) return 1u|TINT;
    if(heldBy==1) return 2u|TINT;
    // v1432: the player's own body (legs, torso): inside the player's cylinder above the floor (SpacewarpParams::body_on)
    if(bodyUp.w>0 && d0>palm0.w && d1>palm1.w) {
        const float3 q=P-bodyAxis.xyz; const float h=dot(q,bodyUp.xyz); const float3 hor=q-h*bodyUp.xyz;
        if(h>body.y && h<body.z && dot(hor,hor)<body.x*body.x) {
            if((flags.w&1u)!=0u && terms.z>0 && mo.w>=terms.w) {
                // a motion vector: its own motion when it stays within body_cap of the play space's step (sim 2026-11-20: a guard's sword
                // swing in melee range is further off and stays with the other rules below)
                const float3 rest=flags.z!=0u?Rigid(RR,P)-P:0;
                const float3 m=float3(dot(W2S[0].xyz,mo.xyz),dot(W2S[1].xyz,mo.xyz),dot(W2S[2].xyz,mo.xyz))*terms.z;
                if(length(m-rest)<=body.w) return 5u;
            } else if(flags.z!=0u) {  // none (the engine draws the player's body without one): the play space's step, unless the previous real
                // frame shows scenery there (sim 2026-11-20: rubble a few cm above the root inside the cylinder slid with the player): confirmed
                // still in the world and not where the play space's step puts it. A leg overlapping its own previous place matches both: body.
                // without a usable previous frame (after a gap or a cut) scenery cannot be told apart: no body texel then (review)
                if(prevFlags.x!=0u && carryTerms.z>0 && !(PrevShows(PW,P,0)==1 && PrevShows(PR,P,carryTerms.y*length(P))!=1)) return 3u;
            }
        }
    }
    // v1423: what the player carries, confirmed by the previous real frame, moves with the rest of the rig
    if(prevFlags.x!=0u && carryTerms.z>0) {
        const float r=length(P);
        if(r>=C.w && r<carryTerms.x && PrevShows(PR,P,carryTerms.y*r)==1 && PrevShows(PW,P,0)!=1) return flags.z!=0u?3u:0u;
    }
    // without a rig mask (true stereo): what lies within a drawn palm's reach, or nearer than C.w metres, is the player's rig
    const bool nearRig=C.w>0 && (length(P)<C.w || d0<=palm0.w || d1<=palm1.w);
    const uint rig=(flags.w&2u)?rigTex[t]:(nearRig?1u:0u);
    if(rig!=0u) {
        if(flags.x!=0u && d0<=palm0.w && d0<=d1) return 1u;
        if(flags.y!=0u && d1<=palm1.w) return 2u;
        if(mo.w>=.5 && terms.z>0) return 4u;
        if(flags.z!=0u) return 3u;
        return 0u;
    }
    return mo.w>=terms.w && terms.z>0?4u:0u;
}
)HLSL"
R"HLSL(  // (split: MSVC caps one string literal at 16 KB)
// The texel's displacement in source camera metres, from its class
float3 DisplaceW(uint w,uint2 t,float3 P) {
    const uint c=w&CLASS;
    if(c==1u) return Rigid(H0,P)-P;
    if(c==2u) return Rigid(H1,P)-P;
    if(c==3u) return Rigid(RR,P)-P;
    if(c==5u) {  // v1432: the play space's step plus the texel's own motion beyond it (the walk cycle), only that part capped
        const float3 rest=flags.z!=0u?Rigid(RR,P)-P:0;
        const float4 mo=motionTex[t];
        float3 r=float3(dot(W2S[0].xyz,mo.xyz),dot(W2S[1].xyz,mo.xyz),dot(W2S[2].xyz,mo.xyz))*terms.z-rest;
        const float l=length(r); if(body.w>0 && l>body.w) r*=body.w/l;
        return rest+r;
    }
    if(c==4u) {
        const float4 mo=motionTex[t];float3 m=float3(dot(W2S[0].xyz,mo.xyz),dot(W2S[1].xyz,mo.xyz),dot(W2S[2].xyz,mo.xyz))*terms.z;
        const float l=length(m);if(misc.x>0 && l>misc.x) m*=misc.x/l;  // v1426: the mover cap
        return m;
    }
    return 0;
}
[numthreads(8,8,1)] void Classify(uint3 id : SV_DispatchThreadID) {
    if(any(id.xy>=uint2(grid.xy))) return;
    wordOut[id.xy]=depthTex[id.xy]<=1e-7?SKYCLASS:ClassOf(id.xy,SourcePoint(id.xy));
}
// v1424 (user, headset v1423: "around the hands, weapons, carried bodies and my own body a silhouette frame, a lot of ghosting"): the colour
// image's edges are soft (anti-aliasing, the upscaler, the depth's own jitter) while the depth's are hard. Where two neighbours move
// differently (by more than half a texel), the farther one within EDGE_R texels of the nearer is a rim: its colour is part the nearer
// surface's, so it is not drawn (its cells are filled from beyond it) and no filled cell shows it. The nearer one's edge texel takes its
// colour from further inside (bits 6-8), not the background it leaves. A thin thing seen with background on both sides keeps its own.
[numthreads(8,8,1)] void Edge(uint3 id : SV_DispatchThreadID) {
    const int2 p=int2(id.xy); if(any(p>=int2(grid.xy))) return;
    uint w=classTex[p]; const uint c=w&CLASS; const bool sky=c==SKYCLASS;
    const float3 D=sky?0:DisplaceW(w,uint2(p),SourcePoint(uint2(p)));
    if(step.x==0u) {wordOut[p]=w;return;}  // build/spacewarp-legacy-edges
    const float z=sky?1e30:Ahead(depthTex[p]);
    const float texel=(srcTan.y-srcTan.x)*grid.z;  // tangent per texel
    bool rim=false; float2 away=0; float behind=0;
    [loop] for(int y=-EDGE_R;y<=EDGE_R;++y) [loop] for(int x=-EDGE_R;x<=EDGE_R;++x) {
        const int2 n=p+int2(x,y);
        if((x==0 && y==0) || any(n<0) || any(n>=int2(grid.xy))) continue;
        const uint wn=classTex[n]; const uint cn=wn&CLASS; if(cn==c) continue;
        const bool skyn=cn==SKYCLASS; const float zn=skyn?1e30:Ahead(depthTex[n]);
        const float3 Dn=skyn?0:DisplaceW(wn,uint2(n),SourcePoint(uint2(n)));
        const float gap=length(Dn-D);
        if(zn<z*.95-.01) {if(gap>.5*texel*zn) rim=true;}
        else if(zn>z*1.05+.01 && gap>.5*texel*z) {away+=float2(x,y)*rsqrt(float(x*x+y*y)); behind+=1;}
    }
    if(rim) w|=RIM;
    else if(!sky && behind>0 && length(away)>.5*behind) {
        const uint dir=uint(round(atan2(-away.y,-away.x)/.78539816)+8)%8u;
        w|=FGEDGE|(dir<<6);
    }
    // v1425 (sim crops: thin lines of the wall through a sleeve): one surface split between two classes (the part of an arm within the palm's
    // reach moves with the hand, the rest with the body) tore along the class boundary whenever the two motions differed. Within 4 texels of
    // such a boundary on the same surface (depth within 5 %), the displacement is the tent-weighted mean of that surface's texels around it:
    // the surface stretches across the boundary instead of tearing. Only between the player's own parts (the hands, the rest of the rig):
    // a mover on a wall, or a carried body on the floor, keeps its hard boundary. Depth edges keep theirs (the rim rules above).
    float3 Df=D;
    const bool own=(c>=1u && c<=3u) || c==5u;  // hand 0, hand 1, the rest of the rig, v1432 the body
    if(own) {
        bool seam=false;
        [loop] for(int y=-4;y<=4 && !seam;y+=2) [loop] for(int x=-4;x<=4;x+=2) {
            const int2 n=p+int2(x,y); if(any(n<0) || any(n>=int2(grid.xy))) continue;
            const uint cn=classTex[n]&CLASS; if(cn==c || ((cn<1u || cn>3u) && cn!=5u)) continue;
            if(abs(Ahead(depthTex[n])-z)<=.05*z+.005) {seam=true;break;}
        }
        if(seam) {
            float3 sum=0; float wsum=0;
            [loop] for(int y=-4;y<=4;++y) [loop] for(int x=-4;x<=4;++x) {
                const int2 n=p+int2(x,y); if(any(n<0) || any(n>=int2(grid.xy))) continue;
                const uint wn=classTex[n]; if(((wn&CLASS)<1u || (wn&CLASS)>3u) && (wn&CLASS)!=5u) continue;
                const float dn=depthTex[n]; if(abs(Ahead(dn)-z)>.05*z+.005) continue;
                const float wt=5-float(max(abs(x),abs(y)));
                sum+=DisplaceW(wn,uint2(n),SourcePoint(uint2(n)))*wt; wsum+=wt;
            }
            if(wsum>0) {Df=sum/wsum;w|=FEATHER;dispOut[p]=float4(Df,0);}  // only feathered texels write (and read) the texture
        }
    }
    wordOut[p]=w;
}
// v1425: a texel's displacement: Edge's feathered one, else its class's
float3 Disp(uint w,uint2 t,float3 P) {return (w&FEATHER)?dispTex[t].xyz:DisplaceW(w,t,P);}
// The cell a source texel lands in and its 1/z bits (sky: 1, the farthest non-empty); false: outside the target or behind it, or a rim
bool Land(uint2 t,out uint2 cell,out uint bits) {
    cell=0; bits=0;
    const uint w=wordTex[t]; if(w&RIM) return false;
    const float2 uv=(float2(t)+.5)*grid.zw; const float d=depthTex[t]; float3 q;
    if(d<=1e-7) {q=MulMT(Ray(srcTan,uv)); bits=1u;}
    else {const float3 P=Ray(srcTan,uv)*Ahead(d); q=MulMT(P+Disp(w,t,P)-C.xyz); bits=asuint(1/max(-q.z,1e-6));}  // v1425: feathered where Edge said so
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
)HLSL"
R"HLSL(  // (split: MSVC caps one string literal at 16 KB)
struct Vertex {float4 position : SV_POSITION; float2 uv : TEXCOORD0;};
Vertex FullVS(uint id : SV_VertexID) {Vertex v; v.uv=float2((id<<1)&2,id&2); v.position=float4(v.uv*float2(2,-2)+float2(-1,1),0,1); return v;}
float2 TexelUv(uint idx) {const uint i=idx&INDEX; return (float2(i%uint(grid.x),i/uint(grid.x))+.5)*grid.zw;}
// The source image shows this source-camera point: it falls inside it and the source depth there agrees within 2 %
bool Seen(float3 Pk,out uint2 tt) {
    float2 sk; tt=0; if(!ToImage(srcTan,Pk,sk) || any(sk<0) || any(sk>=1)) return false;
    tt=min(uint2(sk*grid.xy),uint2(grid.xy)-1); const float d=depthTex[tt]; if(d<=1e-7) return false;
    const float a=Ahead(d); return abs(a+Pk.z)<=.02*a;
}
// v1429: Catmull-Rom from 5 bilinear taps (the corner taps dropped, weights renormalised), clamped to the 2x2 texels around the point
// (no ringing or halo at an edge). uv in 0..1 of the colour image.
float3 SharpSample(float2 uv) {
    uint w,h;colourTex.GetDimensions(w,h);const float2 size=float2(w,h);
    const float2 pos=uv*size,t1=floor(pos-.5)+.5,f=pos-t1;
    const float2 w0=f*(-.5+f*(1-.5*f)),w1=1+f*f*(-2.5+1.5*f),w2=f*(.5+f*(2-1.5*f)),w3=f*f*(-.5+.5*f);
    const float2 w12=w1+w2,t12=(t1+w2/w12)/size,t0=(t1-1)/size,t3=(t1+2)/size;
    float3 c=colourTex.SampleLevel(linearClamp,float2(t0.x,t12.y),0).rgb*(w0.x*w12.y)
            +colourTex.SampleLevel(linearClamp,float2(t12.x,t0.y),0).rgb*(w12.x*w0.y)
            +colourTex.SampleLevel(linearClamp,t12,0).rgb*(w12.x*w12.y)
            +colourTex.SampleLevel(linearClamp,float2(t3.x,t12.y),0).rgb*(w3.x*w12.y)
            +colourTex.SampleLevel(linearClamp,float2(t12.x,t3.y),0).rgb*(w12.x*w3.y);
    c/=(w0.x*w12.y+w12.x*w0.y+w12.x*w12.y+w3.x*w12.y+w12.x*w3.y);
    const int2 b=clamp(int2(floor(pos-.5)),int2(0,0),int2(size)-2);
    const float3 a00=colourTex.Load(int3(b,0)).rgb,a10=colourTex.Load(int3(b+int2(1,0),0)).rgb,a01=colourTex.Load(int3(b+int2(0,1),0)).rgb,a11=colourTex.Load(int3(b+int2(1,1),0)).rgb;
    return clamp(c,min(min(a00,a10),min(a01,a11)),max(max(a00,a10),max(a01,a11)));
}
// v1424: a grid cell's texel colour (linear light) through the colour camera: a surface texel at its point, a sky texel in its direction
float3 TexelColour(uint idx) {
    const uint i=idx&INDEX; const uint2 t=uint2(i%uint(grid.x),i/uint(grid.x));
    const float3 Pt=(idx&SKY)?Ray(srcTan,TexelUv(idx)):SourcePoint(t);
    float2 s; if(!ToImage(colourTan,MulCT(Pt),s)) s=TexelUv(idx);
    return colourTex.SampleLevel(linearClamp,saturate(s),0).rgb;
}
// v1424 (the shards and streaks where a moving thing was): an uncovered cell's colour blends the background around it, the first drawn
// (not filled) cell in each of 8 directions that is not nearer than 3/4 of the cell's own depth (the thing that moved is) and moves like
// the cell's own seed (a mover on a wall at the wall's depth is not its background), weighted by 1/distance. false: none found (the seed's
// own colour then)
uint WordOf(uint idx) {const uint i=idx&INDEX; return wordTex[uint2(i%uint(grid.x),i/uint(grid.x))];}
bool Uncovered(uint2 cell,uint seed,out float3 rgb) {
    rgb=0; float wsum=0; const float zc=Z(depthIn[cell]); const uint cls=WordOf(seed)&CLASS;
    const int2 dirs[8]={int2(1,0),int2(-1,0),int2(0,1),int2(0,-1),int2(1,1),int2(-1,-1),int2(1,-1),int2(-1,1)};
    [loop] for(int k=0;k<8;++k) {
        [loop] for(int s=1;s<=96;++s) {  // v1429: 96 (wider holes)
            const int2 n=int2(cell)+dirs[k]*s; if(any(n<0) || any(n>=int2(grid.xy))) break;
            const uint ni=indexIn[n]; if(ni==EMPTY || (ni&FILLED)) continue;
            if(Z(depthIn[n])<.75*zc || (WordOf(ni)&CLASS)!=cls) break;
            const float wgt=1/float(s); rgb+=TexelColour(ni)*wgt; wsum+=wgt; break;
        }
    }
    if(wsum<=0) return false;
    rgb/=wsum; return true;
}
// v1424: once per uncovered cell (render size, not per output pixel): its blended background
[numthreads(8,8,1)] void Blend(uint3 id : SV_DispatchThreadID) {
    const uint2 p=id.xy; if(any(p>=uint2(grid.xy))) return;
    const uint idx=depthIn[p]!=0u?indexIn[p]:EMPTY;
    float3 rgb=0; bool ok=false;
    if(step.x!=0u && idx!=EMPTY && (idx&FILLED)) ok=Uncovered(p,idx,rgb);
    blendOut[p]=float4(rgb,ok?1+float(WordOf(idx)&CLASS):0);  // w: 1 + the seed's class (BlurBlend mixes only equal ones)
}
// v1425: the blend's 8-direction seams (a star in each uncovered area) softened: a tent blur over the uncovered cells only (drawn cells
// neither contribute nor change), filled from the same class of background, 7 taps, along x when step.y is 1, along y when 2
[numthreads(8,8,1)] void BlurBlend(uint3 id : SV_DispatchThreadID) {
    const int2 p=int2(id.xy); if(any(p>=int2(grid.xy))) return;
    const float4 c0=blurIn[p]; if(c0.w<=0) {blendOut[p]=c0;return;}
    const int2 dir=step.y==1u?int2(1,0):int2(0,1);
    float3 sum=0; float n=0;
    [unroll] for(int k=-3;k<=3;++k) {
        const int2 q=p+dir*k; if(any(q<0) || any(q>=int2(grid.xy))) continue;
        const float4 v=blurIn[q]; if(v.w!=c0.w) continue;
        const float wt=4-abs(float(k)); sum+=v.rgb*wt; n+=wt;
    }
    blendOut[p]=float4(sum/n,c0.w);
}
float4 Resolve(Vertex v) : SV_TARGET {
    const float2 uv=v.position.xy*outSize.zw;
    const uint2 cell=min(uint2(uv*grid.xy),uint2(grid.xy)-1);
    const uint idx=depthIn[cell]!=0u?indexIn[cell]:EMPTY;
    // the point (for the sky and uncovered cells: the direction) in source camera coordinates whose colour this pixel shows: the pixel's
    // ray at the cell's depth, back into the source (minus the texel's displacement), wherever the source sees that point (its depth there
    // agrees within 2 %: also in a filled cell on a magnified surface, if its seed did not move); else the texel's own point (a hole behind something that moved
    // shows its background seed, never the foreground in front of it). Then looked up in the colour image through its own camera.
    float3 P;bool tint=false,blended=false;float2 inward=0;float3 blend=0;uint tintClass=0u;
    if(idx==EMPTY || (idx&SKY)) {
        P=MulM(Ray(dstTan,uv));
        // v1424: a sky cell whose direction the source shows covered by something that moved (or by a rim) shows its own sky texel instead:
        // the sky a moving hand uncovers is not the hand looked up again
        float2 s0;
        if(idx!=EMPTY && ToImage(srcTan,P,s0) && all(s0>=0) && all(s0<1)) {
            const uint w0=wordTex[min(uint2(s0*grid.xy),uint2(grid.xy)-1)];
            if((w0&CLASS)!=SKYCLASS || (w0&RIM)!=0u) P=Ray(srcTan,TexelUv(idx));
        }
    } else {
        const uint i=idx&INDEX; const uint2 t=uint2(i%uint(grid.x),i/uint(grid.x)); const float3 Ps=SourcePoint(t); const uint w=wordTex[t];
        if(step.w!=0u) {tint=(w&TINT)!=0u;tintClass=w&CLASS;}
        const float3 d=Disp(w,t,Ps),Pk=MulM(Ray(dstTan,uv)*Z(depthIn[cell]))+C.xyz-d;
        // a filled cell takes the back-projected point only from still parts of the source (its seed and the texel it lands on): the
        // place a mover left shows its seed's colour, never the mover seen again. v1424: no cell takes it from a rim texel either.
        uint2 tt; bool use=Seen(Pk,tt);
        if(use) {
            const uint wt=wordTex[tt];
            if(wt&RIM) use=false;
            else if(idx&FILLED) use=all(d==0) && all(Disp(wt,tt,SourcePoint(tt))==0);
        }
        P=use?Pk:Ps;
        if(!use && (idx&FILLED) && step.x!=0u) {const float4 b=blendTex[cell];if(b.w>0) {blended=true;blend=b.rgb;}}  // v1424: not one seed's streak (Blend)
        // v1424: a moving surface's edge texel shows the colour 1.5 texels further in, not the background it was drawn over
        if(!(idx&FILLED) && (w&FGEDGE)) {const float a=float((w>>6)&7u)*.78539816;inward=float2(cos(a),sin(a))*1.5;}
    }
    float2 s; if(!ToImage(colourTan,MulCT(P),s)) s=uv;
    s+=inward*grid.zw*float2((srcTan.y-srcTan.x)/(colourTan.y-colourTan.x),(srcTan.w-srcTan.z)/(colourTan.w-colourTan.z));
    float3 rgb;  // v1429 (an if, not a ternary: HLSL evaluates both sides of ?:)
    if(blended) rgb=blend;
    else if(misc.y>0) rgb=SharpSample(saturate(s));
    else rgb=colourTex.SampleLevel(linearClamp,saturate(s),0).rgb;
    if(step.w==2u) {  // v1432 diagnostics: the class of the texel each pixel came from
        if(tintClass==1u || tintClass==2u) rgb=lerp(rgb,float3(0,1,0),.5);
        else if(tintClass==3u) rgb=lerp(rgb,float3(1,1,0),.5);
        else if(tintClass==4u) rgb=lerp(rgb,float3(0,1,1),.5);
        else if(tintClass==5u) rgb=lerp(rgb,float3(1,0,1),.6);
    } else if(tint) rgb=lerp(rgb,float3(1,0,1),.6);
    if(step.z!=0u) rgb=rgb<=.0031308?rgb*12.92:1.055*pow(max(rgb,1e-8),1/2.4)-.055;  // a UNORM target holding sRGB values
    return float4(rgb,1);
}
)HLSL";return text;}
inline void SpacewarpPass::warm_cache() noexcept {
    try {
        const auto& text=shader_source();
        for(const char* e:{"Clear","SplatDepth","SplatIndex","Crack","Fill","Classify","Edge","Blend","BlurBlend"}) {Ptr<ID3DBlob> b,er;(void)shader_cache::compile(text,"spacewarp",e,"cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,&b,&er);}
        {Ptr<ID3DBlob> b,er;(void)shader_cache::compile(text,"spacewarp","FullVS","vs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,&b,&er);}
        {Ptr<ID3DBlob> b,er;(void)shader_cache::compile(text,"spacewarp","Resolve","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,&b,&er);}
    } catch(...) {}
}
inline SpacewarpPass::SpacewarpPass(ID3D11Device* device):device_(device) {
    const std::string& source=shader_source();  // v1428: static (warm_cache)
    const auto compile=[&](const char* entry,const char* target) {
        Ptr<ID3DBlob> blob,errors;
        if(FAILED(shader_cache::compile(source,"spacewarp",entry,target,D3DCOMPILE_OPTIMIZATION_LEVEL3,&blob,&errors)))  // v1428: disk cache
            throw std::runtime_error(std::string("spacewarp shader ")+entry+": "+(errors?static_cast<const char*>(errors->GetBufferPointer()):"compile failed"));
        return blob;
    };
    const auto cs=[&](const char* entry,Ptr<ID3D11ComputeShader>& out) {
        const auto b=compile(entry,"cs_5_0");
        if(FAILED(device_->CreateComputeShader(b->GetBufferPointer(),b->GetBufferSize(),nullptr,&out))) throw std::runtime_error(std::string("spacewarp CS ")+entry);
    };
    cs("Clear",clear_);cs("SplatDepth",splat_depth_);cs("SplatIndex",splat_index_);cs("Crack",crack_);cs("Fill",fill_);
    cs("Classify",classify_);cs("Edge",edge_);cs("Blend",blend_cs_);cs("BlurBlend",blur_cs_);  // v1424, v1425
    {const auto b=compile("FullVS","vs_5_0");if(FAILED(device_->CreateVertexShader(b->GetBufferPointer(),b->GetBufferSize(),nullptr,&vs_))) throw std::runtime_error("spacewarp VS");}
    {const auto b=compile("Resolve","ps_5_0");if(FAILED(device_->CreatePixelShader(b->GetBufferPointer(),b->GetBufferSize(),nullptr,&resolve_))) throw std::runtime_error("spacewarp PS");}
    D3D11_BUFFER_DESC cb{};cb.ByteWidth=sizeof(Constants);cb.Usage=D3D11_USAGE_DYNAMIC;cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;cb.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
    if(FAILED(device_->CreateBuffer(&cb,nullptr,&constants_))) throw std::runtime_error("spacewarp constants");
    D3D11_SAMPLER_DESC sd{};sd.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sd.MaxLOD=D3D11_FLOAT32_MAX;
    if(FAILED(device_->CreateSamplerState(&sd,&linear_))) throw std::runtime_error("spacewarp sampler");
}

inline bool SpacewarpPass::grids_(unsigned w,unsigned h) {
    if(w==grid_w_ && h==grid_h_ && depth_[0].t) return true;
    for(auto* set:{&depth_,&index_,&word_}) for(auto& g:*set) g={};
    blend_={};disp_={};blend2_={};
    grid_w_=grid_h_=0;
    D3D11_TEXTURE2D_DESC d{};d.Width=w;d.Height=h;d.MipLevels=1;d.ArraySize=1;d.Format=DXGI_FORMAT_R32_UINT;d.SampleDesc.Count=1;
    d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;
    for(auto* set:{&depth_,&index_,&word_}) for(auto& g:*set)
        if(FAILED(device_->CreateTexture2D(&d,nullptr,&g.t)) || FAILED(device_->CreateUnorderedAccessView(g.t.Get(),nullptr,&g.u)) ||
           FAILED(device_->CreateShaderResourceView(g.t.Get(),nullptr,&g.s))) return false;
    d.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;  // v1425: displacements
    if(FAILED(device_->CreateTexture2D(&d,nullptr,&disp_.t)) || FAILED(device_->CreateUnorderedAccessView(disp_.t.Get(),nullptr,&disp_.u)) ||
       FAILED(device_->CreateShaderResourceView(disp_.t.Get(),nullptr,&disp_.s))) return false;
    d.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;  // v1424: Blend's colours
    if(FAILED(device_->CreateTexture2D(&d,nullptr,&blend_.t)) || FAILED(device_->CreateUnorderedAccessView(blend_.t.Get(),nullptr,&blend_.u)) ||
       FAILED(device_->CreateShaderResourceView(blend_.t.Get(),nullptr,&blend_.s))) return false;
    if(FAILED(device_->CreateTexture2D(&d,nullptr,&blend2_.t)) || FAILED(device_->CreateUnorderedAccessView(blend2_.t.Get(),nullptr,&blend2_.u)) ||
       FAILED(device_->CreateShaderResourceView(blend2_.t.Get(),nullptr,&blend2_.s))) return false;
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
    f4(k.out,out_width,out_height,1.0/out_width,1.0/out_height);k.step[0]=p.soft_edges?1u:0u;f4(k.misc,p.mover_cap,p.sharp_resample?1:0,0,0);
    k.step[2]=p.encode_srgb?1u:0u;k.step[3]=p.debug_tint_classes?2u:p.debug_tint_held?1u:0u;
    if(p.body_on) {f4(k.body_up,p.body_up[0],p.body_up[1],p.body_up[2],1);f4(k.body,p.body_radius,p.body_floor,p.body_top,p.body_cap);  // v1432
                   f4(k.body_axis,p.body_axis[0],p.body_axis[1],p.body_axis[2],0);}
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
        if(p.carry_on) {  // v1423
            for(int i=0;i<3;++i) f4(k.PR[i],p.prev_rest.r[i*3],p.prev_rest.r[i*3+1],p.prev_rest.r[i*3+2],p.prev_rest.t[i]);
            f4(k.carry,p.carry_reach,p.carry_slack,1,0);
        }
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
    {   // v1424: each texel's class once, then its rim / edge word (Edge reads the classes as t8, writes u2; the rest read the words as t7)
        ID3D11UnorderedAccessView* none[1]{};ID3D11ShaderResourceView* nones[2]{};c->CSSetShaderResources(7,2,nones);
        ID3D11UnorderedAccessView* cls[1]{word_[0].u.Get()};c->CSSetUnorderedAccessViews(2,1,cls,nullptr);
        c->CSSetShader(classify_.Get(),nullptr,0);c->Dispatch(gx,gy,1);
        c->CSSetUnorderedAccessViews(2,1,none,nullptr);
        ID3D11ShaderResourceView* classes[1]{word_[0].s.Get()};c->CSSetShaderResources(8,1,classes);
        ID3D11UnorderedAccessView* words[1]{word_[1].u.Get()};c->CSSetUnorderedAccessViews(2,1,words,nullptr);
        ID3D11UnorderedAccessView* disps[1]{disp_.u.Get()};c->CSSetUnorderedAccessViews(4,1,disps,nullptr);  // v1425
        c->CSSetShader(edge_.Get(),nullptr,0);c->Dispatch(gx,gy,1);
        c->CSSetUnorderedAccessViews(4,1,none,nullptr);
        ID3D11ShaderResourceView* displacements[1]{disp_.s.Get()};c->CSSetShaderResources(10,1,displacements);
        c->CSSetUnorderedAccessViews(2,1,none,nullptr);
        ID3D11ShaderResourceView* texel_words[2]{word_[1].s.Get(),nullptr};c->CSSetShaderResources(7,2,texel_words);
    }
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
    if(p.soft_edges) {   // v1424: the uncovered cells' blended background, from the final grid (t4/t5), the colour (t3) and the texel words (t7)
        ID3D11UnorderedAccessView* none[2]{};c->CSSetUnorderedAccessViews(0,2,none,nullptr);bind_in(side);
        ID3D11SamplerState* samplers[]{linear_.Get()};c->CSSetSamplers(0,1,samplers);
        ID3D11UnorderedAccessView* b[1]{blend_.u.Get()};c->CSSetUnorderedAccessViews(3,1,b,nullptr);
        c->CSSetShader(blend_cs_.Get(),nullptr,0);c->Dispatch(gx,gy,1);
        ID3D11UnorderedAccessView* nb[1]{};c->CSSetUnorderedAccessViews(3,1,nb,nullptr);bind_in(-1);
        // v1425: blur the blend along x (blend_ -> blend2_), then along y (blend2_ -> blend_)
        c->CSSetShader(blur_cs_.Get(),nullptr,0);ID3D11ShaderResourceView* nsrv[1]{};
        for(int pass_=0;pass_<2;++pass_) {
            if(!upload(pass_?2u:1u)) return false;
            Grid& from=pass_?blend2_:blend_;Grid& to=pass_?blend_:blend2_;
            ID3D11ShaderResourceView* src[1]{from.s.Get()};c->CSSetShaderResources(11,1,src);
            ID3D11UnorderedAccessView* dst[1]{to.u.Get()};c->CSSetUnorderedAccessViews(3,1,dst,nullptr);
            c->Dispatch(gx,gy,1);
            c->CSSetUnorderedAccessViews(3,1,nb,nullptr);c->CSSetShaderResources(11,1,nsrv);
        }
    }
    {ID3D11UnorderedAccessView* none[2]{};c->CSSetUnorderedAccessViews(0,2,none,nullptr);ID3D11ShaderResourceView* nones[12]{};c->CSSetShaderResources(0,12,nones);c->CSSetShader(nullptr,nullptr,0);}
    // resolve at the output size
    D3D11_VIEWPORT vp{0,0,static_cast<float>(out_width),static_cast<float>(out_height),0,1};c->RSSetViewports(1,&vp);
    c->OMSetRenderTargets(1,&target,nullptr);c->OMSetBlendState(nullptr,nullptr,0xffffffff);c->OMSetDepthStencilState(nullptr,0);c->RSSetState(nullptr);
    c->IASetInputLayout(nullptr);c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    c->HSSetShader(nullptr,nullptr,0);c->DSSetShader(nullptr,nullptr,0);c->GSSetShader(nullptr,nullptr,0);
    c->VSSetShader(vs_.Get(),nullptr,0);c->PSSetShader(resolve_.Get(),nullptr,0);
    ID3D11ShaderResourceView* ps[11]{in.depth,in.motion,in.rig,in.colour,depth_[side].s.Get(),index_[side].s.Get(),previous[0],word_[1].s.Get(),nullptr,blend_.s.Get(),disp_.s.Get()};
    c->PSSetShaderResources(0,11,ps);
    ID3D11SamplerState* samplers[]{linear_.Get()};c->PSSetSamplers(0,1,samplers);
    c->Draw(3,0);
    ID3D11ShaderResourceView* nones[11]{};c->PSSetShaderResources(0,11,nones);
    ++passes;return true;
}
} // namespace d2vr
