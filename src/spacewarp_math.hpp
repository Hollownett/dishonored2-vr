#pragma once
// Spacewarp camera and hand motion (docs/SPACEWARP-PLAN-2026-11-19.md section 10). Pure maths; lab: tests/spacewarp_math_check.cpp.
//
// A rendered eye camera is S = O * V: V the tracked view it was rendered with (tracking/local space, an XrPosef: local-from-camera,
// OpenXR axes) and O the play space (world-from-local) at that moment. O is never read from the game: O = S * V^-1 from the camera the
// engine rendered (WarpCamera, world metres) and its view, so offsets and turns the mod applies are inside it. Between two real frames
// of one eye, dO = O(n) * O(n-1)^-1 is the play space's own motion in the world (smooth locomotion, smooth turn).
//
// The generated frame shows that eye at a later time: its camera D = dO^a * O * (dV * V), dV = the head's tracked change between the two
// display times (located now for both), a = the share of the last real-frame step to extrapolate. In source camera coordinates
// S^-1 * D = L * H with L = S^-1 dO^a S (locomotion) and H = V^-1 dV V (head motion). A drawn hand moves by L * (V^-1 dG V), dG = its
// grip's tracked change between the same two times; the rest of the rig by L alone; the world does not move.
#include "spacewarp_pass.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <openxr/openxr.h>

namespace d2vr {
struct SwRigid {  // P' = r * P + t (r row-major)
    std::array<double,9> r{1,0,0,0,1,0,0,0,1};std::array<double,3> t{};
    SwRigid operator*(const SwRigid& b) const {
        SwRigid c;c.r={};
        for(int i=0;i<3;++i) {for(int j=0;j<3;++j) for(int k=0;k<3;++k) c.r[i*3+j]+=r[i*3+k]*b.r[k*3+j];
                              c.t[i]=t[i];for(int k=0;k<3;++k) c.t[i]+=r[i*3+k]*b.t[k];}
        return c;
    }
    SwRigid inverse() const {
        SwRigid c;for(int i=0;i<3;++i) for(int j=0;j<3;++j) c.r[i*3+j]=r[j*3+i];
        for(int i=0;i<3;++i) {c.t[i]=0;for(int k=0;k<3;++k) c.t[i]-=c.r[i*3+k]*t[k];}
        return c;
    }
    std::array<double,3> apply(const std::array<double,3>& p) const {
        std::array<double,3> o{};for(int i=0;i<3;++i) o[i]=r[i*3]*p[0]+r[i*3+1]*p[1]+r[i*3+2]*p[2]+t[i];return o;
    }
    double angle() const {return std::acos(std::clamp((r[0]+r[4]+r[8]-1)/2,-1.,1.));}
    double step() const {return std::sqrt(t[0]*t[0]+t[1]*t[1]+t[2]*t[2]);}
    // a share of this motion: the rotation's angle times a about the same axis, the translation times a (small steps: within a few mm of
    // the screw motion)
    SwRigid power(double a) const {
        SwRigid c;const double th=angle();
        if(th>1e-9) {
            const double s=2*std::sin(th);std::array<double,3> k{(r[7]-r[5])/s,(r[2]-r[6])/s,(r[3]-r[1])/s};
            const double n=std::sqrt(k[0]*k[0]+k[1]*k[1]+k[2]*k[2]);if(n>0) for(auto& v:k) v/=n;
            const double x=th*a,cs=std::cos(x),sn=std::sin(x),C=1-cs;
            c.r={cs+k[0]*k[0]*C,k[0]*k[1]*C-k[2]*sn,k[0]*k[2]*C+k[1]*sn, k[1]*k[0]*C+k[2]*sn,cs+k[1]*k[1]*C,k[1]*k[2]*C-k[0]*sn,
                 k[2]*k[0]*C-k[1]*sn,k[2]*k[1]*C+k[0]*sn,cs+k[2]*k[2]*C};
        }
        for(int i=0;i<3;++i) c.t[i]=t[i]*a;
        return c;
    }
};
inline SwRigid sw_from_pose(const XrPosef& p) {  // local-from-object
    const double x=p.orientation.x,y=p.orientation.y,z=p.orientation.z,w=p.orientation.w,n=std::sqrt(x*x+y*y+z*z+w*w);
    SwRigid r;if(!(n>1e-9)) return r;const double a=x/n,b=y/n,c=z/n,d=w/n;
    r.r={1-2*(b*b+c*c),2*(a*b-c*d),2*(a*c+b*d), 2*(a*b+c*d),1-2*(a*a+c*c),2*(b*c-a*d), 2*(a*c-b*d),2*(b*c+a*d),1-2*(a*a+b*b)};
    r.t={p.position.x,p.position.y,p.position.z};return r;
}
inline XrPosef sw_to_pose(const SwRigid& m) {
    const auto& r=m.r;double w,x,y,z;const double tr=r[0]+r[4]+r[8];
    if(tr>0) {const double s=std::sqrt(tr+1)*2;w=s/4;x=(r[7]-r[5])/s;y=(r[2]-r[6])/s;z=(r[3]-r[1])/s;}
    else if(r[0]>r[4] && r[0]>r[8]) {const double s=std::sqrt(1+r[0]-r[4]-r[8])*2;w=(r[7]-r[5])/s;x=s/4;y=(r[1]+r[3])/s;z=(r[2]+r[6])/s;}
    else if(r[4]>r[8]) {const double s=std::sqrt(1+r[4]-r[0]-r[8])*2;w=(r[2]-r[6])/s;x=(r[1]+r[3])/s;y=s/4;z=(r[5]+r[7])/s;}
    else {const double s=std::sqrt(1+r[8]-r[0]-r[4])*2;w=(r[3]-r[1])/s;x=(r[2]+r[6])/s;y=(r[5]+r[7])/s;z=s/4;}
    XrPosef p{};p.orientation={static_cast<float>(x),static_cast<float>(y),static_cast<float>(z),static_cast<float>(w)};
    p.position={static_cast<float>(m.t[0]),static_cast<float>(m.t[1]),static_cast<float>(m.t[2])};return p;
}
// A WarpCamera-style world pose (position; basis rows forward, left, up in the world) as world-from-camera with OpenXR camera axes
// (x right = -left, y up, z back = -forward): its columns are those axes.
inline SwRigid sw_from_world_camera(const std::array<double,3>& position,const std::array<std::array<double,3>,3>& rows) {
    SwRigid r;const auto& f=rows[0];const auto& l=rows[1];const auto& u=rows[2];
    r.r={-l[0],u[0],-f[0], -l[1],u[1],-f[1], -l[2],u[2],-f[2]};r.t=position;return r;
}
struct SpacewarpEyeMotion {SwRigid target;SwRigid hand[2];SwRigid rest;};
// S: the source camera (world-from-camera); V: its tracked view; dO_share: the play space's motion to apply (world); dV, dG[2]: the
// head's and each grip's tracked change (local) between the source and the target display times.
inline SpacewarpEyeMotion spacewarp_eye_motion(const SwRigid& S,const SwRigid& V,const SwRigid& dO_share,const SwRigid& dV,const std::array<SwRigid,2>& dG) {
    const auto Si=S.inverse(),Vi=V.inverse();
    const SwRigid L=Si*dO_share*S;
    SpacewarpEyeMotion m;m.target=L*(Vi*dV*V);m.rest=L;
    for(int s=0;s<2;++s) m.hand[s]=L*(Vi*dG[s]*V);
    return m;
}
inline SpacewarpRigid to_pass(const SwRigid& r) {return {r.r,r.t};}
} // namespace d2vr
