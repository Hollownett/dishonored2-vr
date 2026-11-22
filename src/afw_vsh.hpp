#pragma once
// AFW (alternate frame warping), open source: VR Stereo Hub's held-eye rebuild from their Dishonored 1 VR mod
// (third_party/vsh_afw, zlib license, modified), replacing the closed PureDark plugin. In alternate-eye gameplay the engine renders one
// eye per displayed frame; the other (held) eye is REBUILT from two sources, each with its own depth: the fresh eye of this moment
// (near content: the hands, the weapon, and the world a moved hand or a turn uncovered) and the held eye's own last image (the
// world from its own viewpoint, carried by the camera change since). Moved content is caught by the stale test.
//
// Dishonored 2 adapter (this file): per captured fresh eye, its R32 reverse-Z device depth (render rectangle) becomes linear view
// depth in metres (alpha of an RGBA16F texture, distance = m11/(d+m10)), handed to the rebuild through the depth_probe adapter; the
// eye's pose is its GAME camera (WarpCamera: world position + forward/left/up basis, metres), so head motion, stick turning and
// walking are all in it. No engine matrices are passed (the rebuild's pose path). Any refusal leaves the built-in warp for that frame.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <cstdio>
#include <string>
#include "../third_party/vsh_afw/core/gfx/afw_warp.h"
#include "../third_party/vsh_afw/core/gfx/depth_probe.h"
#include "../third_party/vsh_afw/core/util/log.h"
#include "stale_eye_warp.hpp"

namespace d2vr {
namespace afw {
using Microsoft::WRL::ComPtr;

// Live tuning (`afw tune <key> <value>`, build/afw-tune). Metres: the depth handed to the rebuild is in metres.
struct Tuning {
    std::atomic<float> stale{0.015f};       // the stale test's relative depth tolerance (VSH run 18)
    std::atomic<float> fg_depth{0.75f};     // metres: nearer pixels are the foreground (the player's hands, weapon) from the fresh eye
    std::atomic<float> body_depth{0.45f};   // metres: nearer is the body
    std::atomic<float> near_miss{6.f};      // texels: a near-miss candidate beats the fill
    std::atomic<bool>  debug{false};        // tints the rebuilt eye by source
    std::atomic<float> own_hands{-1.f};     // <0 keeps VSH's default; 0..1: still hands from the held eye's own image within this agreement
    std::atomic<int>   edge_hands{-1},held_hands{-1},still_shade{-1}; // -1 keeps VSH's default, 0 off, 1 on
    std::atomic<float> still_mm{3.f},still_deg{1.f}; // hands moved less than this (head-relative, per two frames) count as still
    // The player models' depth compression (r_playerDepthHack): rig device depth / this. The engine's 5 unless the native player-depth path
    // has verifiably set the models to world depth (then 1); stereo disparity of still hands measured 5.04 (world 1.00). `afw tune rigdepth` pins it.
    std::atomic<float> rig_depth{5.f};std::atomic<bool> rig_depth_manual{false};
    // Device depth above this is the player-model range while the compression is on (the held sword and props, which the rig mask does not
    // cover: measured 0.10-0.20 for things in the hands; a world surface reaches 0.04 only within 25 cm). 0 = the rig mask alone.
    std::atomic<float> near_range{0.04f};
    std::atomic<float> held_depth{9.f};     // their compression: still-pair stereo disparity of the held sword measured 9.0 (the rig 5.0)
    std::atomic<bool>  mask{true};          // the fresh eye's player-rig mask (arms, body, held weapon) is the foreground, not a depth limit
    // Kept from the PureDark backend so old knob files and commands still parse; unused by this backend.
    std::atomic<float> ignore_motion_threshold{2.5f},jitter_scale{0.f},motion_scale{1.f};std::atomic<bool> reset_on_gap{true};
    std::atomic<int> max_frame_gap{1},halo{2};
};
inline Tuning& tuning() {static Tuning t;return t;}

// OpenXR-style pose of a game camera: x right (= -left), y up, z back (= -forward); position in metres.
inline dvr::afw::Pose pose_of(const WorldTrackedPose& w) {
    const Vec3& f=w.basis[0];const Vec3& l=w.basis[1];const Vec3& u=w.basis[2];
    const double m[3][3]={{-l.x,u.x,-f.x},{-l.y,u.y,-f.y},{-l.z,u.z,-f.z}}; // columns: right, up, back
    double q[4];const double tr=m[0][0]+m[1][1]+m[2][2];
    if(tr>0) {const double s=std::sqrt(tr+1.)*2;q[3]=.25*s;q[0]=(m[2][1]-m[1][2])/s;q[1]=(m[0][2]-m[2][0])/s;q[2]=(m[1][0]-m[0][1])/s;}
    else if(m[0][0]>m[1][1] && m[0][0]>m[2][2]) {const double s=std::sqrt(1.+m[0][0]-m[1][1]-m[2][2])*2;q[3]=(m[2][1]-m[1][2])/s;q[0]=.25*s;q[1]=(m[0][1]+m[1][0])/s;q[2]=(m[0][2]+m[2][0])/s;}
    else if(m[1][1]>m[2][2]) {const double s=std::sqrt(1.+m[1][1]-m[0][0]-m[2][2])*2;q[3]=(m[0][2]-m[2][0])/s;q[0]=(m[0][1]+m[1][0])/s;q[1]=.25*s;q[2]=(m[1][2]+m[2][1])/s;}
    else {const double s=std::sqrt(1.+m[2][2]-m[0][0]-m[1][1])*2;q[3]=(m[1][0]-m[0][1])/s;q[0]=(m[0][2]+m[2][0])/s;q[1]=(m[1][2]+m[2][1])/s;q[2]=.25*s;}
    const double n=std::sqrt(q[0]*q[0]+q[1]*q[1]+q[2]*q[2]+q[3]*q[3]);
    dvr::afw::Pose p{};for(int i=0;i<4;++i) p.q[i]=static_cast<float>(n>0?q[i]/n:(i==3?1:0));
    p.p[0]=static_cast<float>(w.position.x);p.p[1]=static_cast<float>(w.position.y);p.p[2]=static_cast<float>(w.position.z);
    return p;
}
inline dvr::afw::Pose pose_of(const WarpCamera& c) {return pose_of(c.pose);}
// The grips the image drew its hands from (WarpCamera.hands, world space like the eye poses): the held eye's hands follow their
// controllers instead of staying where the old image drew them (VSH run 25).
inline void hands_of(const WarpCamera& c,dvr::afw::HandPose out[2]) {
    for(unsigned s=0;s<2;++s) {
        out[s]=dvr::afw::HandPose{};
        if(!(c.hand_bits>>s&1u)) continue;
        const auto p=pose_of(c.hands[s]);out[s].ok=true;
        for(int i=0;i<3;++i) out[s].p[i]=p.p[i];
        for(int i=0;i<4;++i) out[s].q[i]=p.q[i];
    }
}

class Runtime {
public:
    std::atomic<bool> requested{false};    // the VR menu's Renderer row chose AFW
    std::atomic<std::uint64_t> rebuilt{},refused{},fallbacks{},captured{},masked{};
    bool hard_failed() const noexcept {return hard_failed_;}
    std::uint64_t busy() const noexcept {return 0;}
    std::string problem() const {std::lock_guard<std::mutex> l(mutex_);return problem_;}
    void log_to(void (*sink)(const char*)) noexcept {dvr::log::sink()=sink;}
    // Render thread, when the fresh eye is kept: its image, game camera, the other eye's camera at this Setup (the pose the held
    // eye is rebuilt at), and its R32 device depth (render rectangle depth_w x depth_h at the top left).
    void capture(ID3D11DeviceContext* c,int eye,std::uint64_t frame,ID3D11Texture2D* image,const WarpCamera& fresh,const WarpCamera& other,
                 ID3D11Texture2D* depth,UINT depth_w,UINT depth_h,ID3D11Texture2D* rig=nullptr,UINT rig_w=0,UINT rig_h=0,std::array<float,4> tans={-1.f,1.f,1.f,-1.f}) {
        if(!requested.load() || hard_failed_ || !c || !image || !depth || !depth_w || !depth_h || eye<0 || eye>1) return;
        apply_tuning();
        ComPtr<ID3D11Device> device;c->GetDevice(&device);
        // The rig mask (R8: 252..255 = the player's rig or a held weapon/prop, 0 = not) of this same render, at the depth's size.
        ID3D11ShaderResourceView* rig_srv=rig && rig_w==depth_w && rig_h==depth_h?rig_view(device.Get(),rig):nullptr;
        const float rig_scale=tuning().rig_depth.load(),near_range=rig_scale>1.01f?tuning().near_range.load():0.f;
        // The two compression classes overlap in device depth (a hand holding a weapon takes the weapon's): each rig pixel takes the class whose
        // point lands nearer a grip or palm of this render (eye space: right, up, forward; metres).
        float points[16]{};
        {const Vec3& F=fresh.pose.basis[0];const Vec3& L=fresh.pose.basis[1];const Vec3& U=fresh.pose.basis[2];int n=0;
         auto add=[&](const Vec3& w){const Vec3 r=w-fresh.pose.position;float* p=points+4*n++;p[0]=static_cast<float>(-(r.x*L.x+r.y*L.y+r.z*L.z));
             p[1]=static_cast<float>(r.x*U.x+r.y*U.y+r.z*U.z);p[2]=static_cast<float>(r.x*F.x+r.y*F.y+r.z*F.z);p[3]=1.f;};
         for(unsigned s=0;s<2;++s) if(fresh.hand_bits>>s&1u) {add(fresh.hands[s].position);
             const Vec3& q=fresh.palms[s];if(q.x!=0 || q.y!=0 || q.z!=0) add(q);}}
        if(!convert_depth(device.Get(),c,depth,depth_w,depth_h,fresh.m10,fresh.m11,rig_srv,rig_scale,near_range,tuning().held_depth.load(),tans.data(),points)) {set_problem("depth conversion failed");return;}
        auto& probe=dvr::depthprobe::current();
        probe.serial=static_cast<std::uint32_t>(frame);probe.depth=depth_srv_.Get();probe.mask=nullptr;probe.w=depth_w;probe.h=depth_h;probe.maskDraws=0;
        if(tuning().mask.load() && (rig_srv || near_range>0)) {probe.mask=mask_srv_.Get();probe.maskDraws=1;++masked;}
        dvr::afw::Pose targets[2];targets[eye]=pose_of(fresh);targets[1-eye]=pose_of(other);
        dvr::afw::note_capture(device.Get(),c,eye,image,static_cast<std::uint32_t>(frame),targets[eye],false,0.f,targets,nullptr,nullptr,nullptr,nullptr);
        dvr::afw::HandPose hands[2];hands_of(fresh,hands);dvr::afw::note_hands(eye,hands);
        dvr::afw::note_hands_still(hands_still(eye,fresh));
        if(!geometry_logged_) { // one-time geometry check: basis handedness, eye separation, the hands' distance
            geometry_logged_=true;
            const Vec3& f=fresh.pose.basis[0];const Vec3& l=fresh.pose.basis[1];const Vec3& u=fresh.pose.basis[2];
            const double det=f.x*(l.y*u.z-l.z*u.y)-f.y*(l.x*u.z-l.z*u.x)+f.z*(l.x*u.y-l.y*u.x);
            const Vec3 d=other.pose.position-fresh.pose.position;
            const double sep=std::sqrt(d.x*d.x+d.y*d.y+d.z*d.z),side=d.x*l.x+d.y*l.y+d.z*l.z;
            char line[400];
            std::snprintf(line,sizeof line,"d2 geometry: eye %d basis det(fwd,left,up) %.3f, other eye %.4f m away (%.4f m to the fresh eye's left), m10 %.5f m11 %.5f, hands %u",
                eye,det,sep,side,fresh.m10,fresh.m11,fresh.hand_bits);
            if(dvr::log::sink()) dvr::log::sink()(line);
            for(unsigned h=0;h<2;++h) if(fresh.hand_bits>>h&1u) {
                const Vec3 r=fresh.hands[h].position-fresh.pose.position;
                std::snprintf(line,sizeof line,"d2 geometry: hand %u at %.3f m ahead, %.3f m left, %.3f m up of the eye",h,r.x*f.x+r.y*f.y+r.z*f.z,r.x*l.x+r.y*l.y+r.z*l.z,r.x*u.x+r.y*u.y+r.z*u.z);
                if(dvr::log::sink()) dvr::log::sink()(line);
            }
        }
        ++captured;
    }
    // Render thread, for the held eye: rebuild it into dst (w x h, render-target capable). False: the built-in warp draws this frame.
    bool rebuild(ID3D11DeviceContext* c,int held,int fresh,std::uint64_t frame,ID3D11Texture2D* dst,UINT w,UINT h,float tan_h,float tan_v) {
        if(!requested.load() || hard_failed_ || !c || !dst) return false;
        ComPtr<ID3D11Device> device;c->GetDevice(&device);
        dvr::afw::Pose out{};const char* why="";
        if(!dvr::afw::warp_held(device.Get(),c,held,fresh,static_cast<std::uint32_t>(frame),dst,w,h,tan_h,tan_v,&out,&why)) {
            ++refused;set_problem(std::string("refused: ")+(why && *why?why:"no reason"));return false;
        }
        ++rebuilt;set_problem("AFW active");return true;
    }
private:
    bool hard_failed_{};mutable std::mutex mutex_;std::string problem_{"off"};
    ComPtr<ID3D11ComputeShader> shader_;ComPtr<ID3D11Buffer> params_;ComPtr<ID3D11Texture2D> depth_tex_;
    ComPtr<ID3D11ShaderResourceView> depth_srv_;ComPtr<ID3D11UnorderedAccessView> depth_uav_;UINT depth_w_{},depth_h_{};
    ComPtr<ID3D11Texture2D> mask_tex_;ComPtr<ID3D11ShaderResourceView> mask_srv_;ComPtr<ID3D11UnorderedAccessView> mask_uav_; // R8: 1 = the player's arms, body, held things
    float applied_[4]{-1,-1,-1,-1};bool configured_{},debug_applied_{},geometry_logged_{},mask_set_{},mask_applied_{};float own_applied_{-1.f};int edge_applied_{-1},held_applied_{-1},still_applied_{-1};
    ID3D11Texture2D* rig_key_[2]{};ComPtr<ID3D11ShaderResourceView> rig_srv_[2];unsigned rig_next_{};
    ID3D11ShaderResourceView* rig_view(ID3D11Device* device,ID3D11Texture2D* rig) { // R8_TYPELESS read as R8_UNORM
        for(int i=0;i<2;++i) if(rig_key_[i]==rig && rig_srv_[i]) return rig_srv_[i].Get();
        D3D11_SHADER_RESOURCE_VIEW_DESC v{};v.Format=DXGI_FORMAT_R8_UNORM;v.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;v.Texture2D.MipLevels=1;
        const unsigned s=rig_next_++&1u;rig_srv_[s].Reset();rig_key_[s]=nullptr;
        if(FAILED(device->CreateShaderResourceView(rig,&v,&rig_srv_[s]))) return nullptr;
        rig_key_[s]=rig;return rig_srv_[s].Get();
    }
    // Both controllers still relative to the head since this eye's last capture (two displayed frames): the held eye's own
    // weapon shading is then kept (VSH run 21). Head-relative, so walking and turning with still hands count as still.
    struct HandRel {Vec3 p[2]{};Vec3 f[2]{};unsigned bits{};std::uint64_t frame{};};HandRel last_rel_[2]{};
    bool hands_still(int eye,const WarpCamera& c) {
        HandRel now{};now.bits=c.hand_bits&3u;
        const Vec3& F=c.pose.basis[0];const Vec3& L=c.pose.basis[1];const Vec3& U=c.pose.basis[2];
        auto local=[&](const Vec3& v){return Vec3{v.x*F.x+v.y*F.y+v.z*F.z,v.x*L.x+v.y*L.y+v.z*L.z,v.x*U.x+v.y*U.y+v.z*U.z};};
        for(unsigned s=0;s<2;++s) if(now.bits>>s&1u) {now.p[s]=local(c.hands[s].position-c.pose.position);now.f[s]=local(c.hands[s].basis[0]);}
        const HandRel was=last_rel_[eye];last_rel_[eye]=now;
        if(!now.bits || now.bits!=was.bits) return false;
        const double tol=tuning().still_mm.load()*1e-3,cos_tol=std::cos(tuning().still_deg.load()*3.14159265358979/180.);
        for(unsigned s=0;s<2;++s) if(now.bits>>s&1u) {
            const Vec3 d=now.p[s]-was.p[s];if(d.x*d.x+d.y*d.y+d.z*d.z>tol*tol) return false;
            const double dot=now.f[s].x*was.f[s].x+now.f[s].y*was.f[s].y+now.f[s].z*was.f[s].z;if(dot<cos_tol) return false;
        }
        return true;
    }
    void set_problem(const std::string& p) {std::lock_guard<std::mutex> l(mutex_);problem_=p;}
    void apply_tuning() {
        if(!configured_) {
            configured_=true;
            dvr::afw::set_world_scale(1.f);                  // depth is handed over in metres: 1 world unit per metre
            dvr::afw::set_enabled(true,"d2vr");dvr::afw::set_stereo(true,"d2vr");dvr::afw::set_matrices(false,"d2vr"); // poses carry walking/turning
            dvr::afw::set_fg(false,"d2vr");                  // D2's hands are drawn with the world's projection (no separate weapon FOV)
            dvr::afw::set_clean(false,"d2vr");               // no separate pre-UI image is captured
        }
        auto& t=tuning();
        const float v[4]={t.stale.load(),t.fg_depth.load(),t.body_depth.load(),t.near_miss.load()};
        if(v[0]!=applied_[0]) dvr::afw::set_stale(v[0],"d2vr");
        if(v[1]!=applied_[1]) dvr::afw::set_fg_depth(v[1],"d2vr");
        if(v[2]!=applied_[2]) dvr::afw::set_body_depth(v[2],"d2vr");
        if(v[3]!=applied_[3]) dvr::afw::set_near_miss(v[3],"d2vr");
        for(int i=0;i<4;++i) applied_[i]=v[i];
        if(!mask_set_ || t.mask.load()!=mask_applied_) {mask_set_=true;mask_applied_=t.mask.load();dvr::afw::set_fg_mask(mask_applied_,"d2vr");} // off: nearer than fg_depth
        if(t.own_hands.load()>=0 && t.own_hands.load()!=own_applied_) {own_applied_=t.own_hands.load();dvr::afw::set_own_hands(own_applied_,"d2vr");}
        auto flag=[](int want,int& have,void(*set)(bool,const char*)){if(want>=0 && want!=have) {have=want;set(want!=0,"d2vr");}};
        flag(t.edge_hands.load(),edge_applied_,dvr::afw::set_edge_hands);flag(t.held_hands.load(),held_applied_,dvr::afw::set_held_hands);flag(t.still_shade.load(),still_applied_,dvr::afw::set_still_shade);
        if(t.debug.load()!=debug_applied_) {debug_applied_=t.debug.load();dvr::afw::set_debug(debug_applied_,"d2vr");}
    }
    // R32 reverse-Z device depth (render rectangle) -> RGBA16F, alpha = linear distance ahead in metres.
    // Rig pixels (mask > 0.5) are drawn with the player-model depth compression: their device depth is divided by rig_scale first.
    bool convert_depth(ID3D11Device* device,ID3D11DeviceContext* c,ID3D11Texture2D* depth,UINT w,UINT h,double m10,double m11,ID3D11ShaderResourceView* rig,float rig_scale,float near_range,float held_scale,const float* tans,const float* points) {
        if(!(m11>0) || !std::isfinite(m10)) return false;
        if(!shader_) {
            static const char code[]=R"(
Texture2D<float> depth:register(t0);Texture2D<float> rig:register(t1);RWTexture2D<float4> outDepth:register(u0);RWTexture2D<unorm float> outMask:register(u1);
cbuffer P:register(b0){float m10,m11,size_x,size_y,rig_scale,has_rig,near_range,held_scale;float4 tans;float4 pts[4];};
float gripMiss(float3 p){float m=1e9;[unroll] for(int i=0;i<4;++i) if(pts[i].w>0.5) m=min(m,length(p-pts[i].xyz));return m;}
[numthreads(8,8,1)]void main(uint3 id:SV_DispatchThreadID){if(id.x>=(uint)size_x || id.y>=(uint)size_y)return;
 float d=depth.Load(int3(id.xy,0));bool body=has_rig>0.5 && rig.Load(int3(id.xy,0))>0.5,held=!body && near_range>0.0 && d>near_range,player=body || held;
 float2 uv=(float2(id.xy)+0.5)/float2(size_x,size_y);float3 ray=float3(lerp(tans.x,tans.y,uv.x),lerp(tans.z,tans.w,uv.y),1.0);
 if(body && near_range>0.0 && d>1e-7 && pts[0].w>0.5) {float za=m11/(d/rig_scale+m10),zb=m11/(d/held_scale+m10);if(gripMiss(ray*zb)<gripMiss(ray*za)) {body=false;held=true;}}
 if(body) d/=rig_scale;else if(held) d/=held_scale;float lin=d>1e-7?m11/(d+m10):1000.0;outDepth[id.xy]=float4(0,0,0,min(lin,1000.0));outMask[id.xy]=player?1.0:0.0;})";
            ComPtr<ID3DBlob> blob,error;
            if(FAILED(D3DCompile(code,sizeof(code)-1,nullptr,nullptr,nullptr,"main","cs_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&blob,&error)) ||
               FAILED(device->CreateComputeShader(blob->GetBufferPointer(),blob->GetBufferSize(),nullptr,&shader_))) {hard_failed_=true;++fallbacks;return false;}
            D3D11_BUFFER_DESC b{};b.ByteWidth=112;b.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
            if(FAILED(device->CreateBuffer(&b,nullptr,&params_))) {hard_failed_=true;++fallbacks;return false;}
        }
        if(w!=depth_w_ || h!=depth_h_ || !depth_tex_) {
            depth_tex_.Reset();depth_srv_.Reset();depth_uav_.Reset();mask_tex_.Reset();mask_srv_.Reset();mask_uav_.Reset();depth_w_=depth_h_=0;
            D3D11_TEXTURE2D_DESC d{};d.Width=w;d.Height=h;d.MipLevels=d.ArraySize=d.SampleDesc.Count=1;d.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;
            d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;
            if(FAILED(device->CreateTexture2D(&d,nullptr,&depth_tex_)) || FAILED(device->CreateShaderResourceView(depth_tex_.Get(),nullptr,&depth_srv_)) ||
               FAILED(device->CreateUnorderedAccessView(depth_tex_.Get(),nullptr,&depth_uav_))) return false;
            d.Format=DXGI_FORMAT_R8_UNORM;
            if(FAILED(device->CreateTexture2D(&d,nullptr,&mask_tex_)) || FAILED(device->CreateShaderResourceView(mask_tex_.Get(),nullptr,&mask_srv_)) ||
               FAILED(device->CreateUnorderedAccessView(mask_tex_.Get(),nullptr,&mask_uav_))) return false;
            depth_w_=w;depth_h_=h;
        }
        ComPtr<ID3D11ShaderResourceView> src;if(FAILED(device->CreateShaderResourceView(depth,nullptr,&src))) return false;
        ComPtr<ID3D11ComputeShader> old_cs;ID3D11Buffer* old_cb{};ID3D11ShaderResourceView* old_srv[2]{};ID3D11UnorderedAccessView* old_uav[2]{};
        c->CSGetShader(&old_cs,nullptr,nullptr);c->CSGetConstantBuffers(0,1,&old_cb);c->CSGetShaderResources(0,2,old_srv);c->CSGetUnorderedAccessViews(0,2,old_uav);
        float values[28]={static_cast<float>(m10),static_cast<float>(m11),static_cast<float>(w),static_cast<float>(h),rig_scale>0.01f?rig_scale:1.f,rig?1.f:0.f,near_range,held_scale>0.01f?held_scale:1.f,
            tans[0],tans[1],tans[2],tans[3]};
        for(int i=0;i<16;++i) values[12+i]=points[i];
        c->UpdateSubresource(params_.Get(),0,nullptr,values,0,0);ID3D11Buffer* cb=params_.Get();c->CSSetConstantBuffers(0,1,&cb);
        ID3D11ShaderResourceView* srv[2]={src.Get(),rig};ID3D11UnorderedAccessView* uav[2]={depth_uav_.Get(),mask_uav_.Get()};
        c->CSSetShaderResources(0,2,srv);c->CSSetUnorderedAccessViews(0,2,uav,nullptr);c->CSSetShader(shader_.Get(),nullptr,0);
        c->Dispatch((w+7)/8,(h+7)/8,1);
        ID3D11ShaderResourceView* no_srv[2]{};ID3D11UnorderedAccessView* no_uav[2]{};c->CSSetShaderResources(0,2,no_srv);c->CSSetUnorderedAccessViews(0,2,no_uav,nullptr);
        c->CSSetShader(old_cs.Get(),nullptr,0);c->CSSetConstantBuffers(0,1,&old_cb);c->CSSetShaderResources(0,2,old_srv);c->CSSetUnorderedAccessViews(0,2,old_uav,nullptr);
        if(old_cb) old_cb->Release();for(auto* s:old_srv) if(s) s->Release();for(auto* u:old_uav) if(u) u->Release();
        return true;
    }
};
// Process lifetime.
inline Runtime& runtime() {static Runtime* r=new Runtime();return *r;}
} // namespace afw
} // namespace d2vr
