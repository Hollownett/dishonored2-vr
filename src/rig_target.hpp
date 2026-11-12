#pragma once
// v1190 performance mode: the first-person rig (arms, body) and what the hands hold, drawn again depth-only into the STALE eye's target camera
// at each one-view render, so the warp gets that eye's exact hand silhouette and depth for this tick (knob build/stale-eye-rig-target).
//
// The retarget (engine convention: row-major matrices, column vectors, clip = M * v; a draw's per-instance rows are the rows of VP_f * M):
//   E = VP_t * inverse(VP_f),  VP_t = P_t * V_t,  V_t = V_f * W_f * inverse(W_t)
// VP_f / V_f / P_f: the fresh render's view-projection (+0x7510, its TAA jitter included), view (+0x7450) and projection (+0x7390) after Setup.
// W_f / W_t: world-from-camera of the fresh camera and of the stale eye's target camera as the mod set them (WorldTrackedPose: forward, left,
// up, position; any axis convention the engine adds cancels in W_f * inverse(W_t)). P_t: the target eye's render frustum (tangents of the
// companion view's FOV, canted as rendered, no jitter) with the engine's depth terms P_f[10], P_f[11] (reverse-Z): the target's depth is in
// the same convention as the engine's scene depth. A draw's retargeted rows are E * rows.
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include "tracked_pose.hpp"

namespace d2vr::rig_target {
using M4=std::array<double,16>;
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
inline M4 widen(const float* m) {M4 o{};for(int i=0;i<16;++i) o[i]=m[i];return o;}
// world-from-camera: columns forward, left, up, position
inline M4 world_from(const WorldTrackedPose& p) {
    const auto& f=p.basis[0];const auto& l=p.basis[1];const auto& u=p.basis[2];
    return {f.x,l.x,u.x,p.position.x, f.y,l.y,u.y,p.position.y, f.z,l.z,u.z,p.position.z, 0,0,0,1};
}
// tangents: tan(left) < tan(right), tan(down) < tan(up) (OpenXR angles: left/down negative)
inline M4 projection(double l,double r,double u,double d,double p10,double p11) {
    M4 o{};o[0]=2/(r-l);o[2]=(r+l)/(r-l);o[5]=2/(u-d);o[6]=(u+d)/(u-d);o[10]=p10;o[11]=p11;o[14]=-1;return o;
}
// E for one render; false when an input is singular or not finite. condition: the largest |E| entry ratio to the smallest diagonal (diagnostic).
inline bool retarget(const M4& vp_f,const M4& v_f,const M4& p_f,const WorldTrackedPose& w_f,const WorldTrackedPose& w_t,
                     double tan_l,double tan_r,double tan_u,double tan_d,M4& e) {
    M4 inv_vp{},inv_wt{};
    if(!(tan_r>tan_l && tan_u>tan_d) || !inverse(vp_f,inv_vp) || !inverse(world_from(w_t),inv_wt)) return false;
    const M4 v_t=mul(mul(v_f,world_from(w_f)),inv_wt);
    e=mul(mul(projection(tan_l,tan_r,tan_u,tan_d,p_f[10],p_f[11]),v_t),inv_vp);
    for(double x:e) if(!std::isfinite(x)) return false;
    return true;
}
// rows (a draw's 4 per-instance MVP rows, x y z w) -> E * rows
inline void apply(const M4& e,const float* rows,float* out) {
    for(int i=0;i<4;++i) for(int j=0;j<4;++j) {double t=0;for(int k=0;k<4;++k) t+=e[i*4+k]*rows[k*4+j];out[i*4+j]=static_cast<float>(t);}
}
// Published at each one-view Setup (render thread), looked up by a draw's view-projection (exact: the classified per-view write's VP).
class Book {
public:
    // v1191: also the render's previous view-projection (view+0x7590 after Setup: this eye's previous render on one-view frames); has_e false
    // when only that was wanted (alt-weapon-place without the rig target)
    // v1199: `eye` = the logical eye this Setup rendered (-1 unknown): latest_of() serves the rig target's per-eye hypotheses
    void publish(const float* vp,const M4& e,bool has_e,const float* prev_vp,std::uint32_t frame,int eye=-1) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);Entry& x=ring_[next_++%ring_.size()];std::memcpy(x.vp,vp,64);x.e=e;x.has_e=has_e;
        std::memcpy(x.prev_vp,prev_vp,64);x.frame=frame;x.eye=eye;x.seq=next_;x.valid=true;
    }
    // v1199: E and VP of the latest Setup of `eye` that has an E (false: none yet)
    bool latest_of(unsigned eye,M4& e,float* vp=nullptr) const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);const Entry* best=nullptr;
        for(const auto& x:ring_) if(x.valid && x.has_e && x.eye==static_cast<int>(eye) && (!best || x.seq>best->seq)) best=&x;
        if(!best) return false;
        e=best->e;if(vp) std::memcpy(vp,best->vp,64);return true;
    }
    bool find(const float* vp,M4& e) const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        for(const auto& x:ring_) if(x.valid && x.has_e && !std::memcmp(x.vp,vp,64)) {e=x.e;return true;}
        return false;
    }
    bool find_prev(const float* vp,M4& prev) const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        for(const auto& x:ring_) if(x.valid && !std::memcmp(x.vp,vp,64)) {prev=widen(x.prev_vp);return true;}
        return false;
    }
    void clear() noexcept {std::lock_guard<std::mutex> lock(mutex_);for(auto& x:ring_) x.valid=false;}
    bool latest(M4& e,std::uint32_t& frame) const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);if(!next_) return false;const Entry& x=ring_[(next_-1)%ring_.size()];
        if(!x.valid || !x.has_e) return false;e=x.e;frame=x.frame;return true;
    }
private:
    struct Entry {float vp[16]{},prev_vp[16]{};M4 e{};std::uint32_t frame{};int eye{-1};unsigned seq{};bool valid{},has_e{};};
    mutable std::mutex mutex_;std::array<Entry,4> ring_{};unsigned next_{};
};
inline Book& book() {static Book b;return b;}
} // namespace d2vr::rig_target
