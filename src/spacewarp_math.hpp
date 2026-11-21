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
// v1460 (user: "fix the player's body and legs warp in spacewarp mode, do the same as for the hands"): where the full-body solver hangs the
// torso for a tracked centre eye (local space, OpenXR axes: +y up; eye = local-from-view). body_ik.hpp: the neck target lands exactly under the
// eye moved `back` metres along the level view (FullBodyDriver::solve_locked; level as the solver takes it, robust looking straight down:
// forward + up * -forward.up), less the head joint's offset (the eye 3.28 cm ahead of and 0.8 cm above it: turned with the head) and the
// neck's (10.53 cm under the head and 3.02 cm behind it: turned by half the head's rotation over the torso's yaw). torso_yaw: the torso's
// heading (a rotation about +y); within the solver's 40 deg dead zone the chest does not turn with the head, so a step holds it (lab:
// tests/spacewarp_body_check.cpp, the solver's own neck). scale: the skeleton's avatar scale (BodySolver scale_). The torso follows this
// point the way a drawn hand follows its grip.
inline SwRigid spacewarp_level_heading(const SwRigid& eye,std::array<double,2>* flat=nullptr) {  // the eye's heading about +y (the solver's level view)
    const std::array<double,3> f{-eye.r[2],-eye.r[5],-eye.r[8]},u{eye.r[1],eye.r[4],eye.r[7]};  // the view's forward (-z) and up (+y)
    const double hx=f[0]-u[0]*f[1],hz=f[2]-u[2]*f[1],hl=std::hypot(hx,hz);
    SwRigid yaw;if(flat) *flat={0,0};
    if(hl>1e-6) {
        const double c=-hz/hl,s=-hx/hl;yaw.r={c,0,s, 0,1,0, -s,0,c};  // about +y, taking -z to the level heading
        if(flat) *flat={hx/hl,hz/hl};
    }
    return yaw;
}
inline std::array<double,3> spacewarp_torso_anchor(const SwRigid& eye,const SwRigid& torso_yaw,double back,double scale) {
    std::array<double,3> a=eye.t;std::array<double,2> flat{};(void)spacewarp_level_heading(eye,&flat);
    a[0]-=back*flat[0];a[2]-=back*flat[1];
    SwRigid turn=eye;turn.t={0,0,0};
    const SwRigid half=torso_yaw*(torso_yaw.inverse()*turn).power(.5);
    const auto r1=turn.apply({0,.008*scale,-.0328*scale}),r2=half.apply({0,.1053*scale,-.0302*scale});
    for(int i=0;i<3;++i) a[i]-=r1[i]+r2[i];
    return a;
}
// v1460: the torso's step (local space) from one tracked centre-eye pose to another, the torso's heading held at the first's
inline std::array<double,3> spacewarp_torso_step(const SwRigid& from,const SwRigid& to,double back,double scale) {
    const auto yaw=spacewarp_level_heading(from);
    const auto a=spacewarp_torso_anchor(from,yaw,back,scale),b=spacewarp_torso_anchor(to,yaw,back,scale);
    return {b[0]-a[0],b[1]-a[1],b[2]-a[2]};
}
// v1460: a drawn leg point's own motion to the slot, in source camera coordinates, beyond the locomotion L (the pass draws it at L(P + r)).
// p: the point in this image; carried: the same point of the previous image carried by the play space's step since (O_now O_prev^-1), both in
// source coordinates; world_step: its world step since the previous image (source axes); share: the slot's share of that step; speed: its world
// speed (m/s). A moving point takes its step in the play space (as a hand its grip's), carried by L like everything the player carries; a
// planted one (slower than 0.2 m/s) stays where it is in the world: L(P + r) = P + share * world_step. Between 0.2 and 0.8 m/s a blend.
inline std::array<double,3> spacewarp_leg_own(const std::array<double,3>& p,const std::array<double,3>& carried,const std::array<double,3>& world_step,
                                              double share,double speed,const SwRigid& L) {
    std::array<double,3> local{},world{};
    const auto x=std::clamp((speed-.2)/.6,0.,1.),w=x*x*(3-2*x);
    const auto there=L.inverse().apply({p[0]+share*world_step[0],p[1]+share*world_step[1],p[2]+share*world_step[2]});
    for(int i=0;i<3;++i) {local[i]=share*(p[i]-carried[i]);world[i]=there[i]-p[i];}
    return {world[0]+(local[0]-world[0])*w,world[1]+(local[1]-world[1])*w,world[2]+(local[2]-world[2])*w};
}
// v1419: a hand's weapon body for spacewarp (dxgi_probe: the weapon collision model's visible blade line or gun barrel), in the drawn grip's
// frame (forward, left, up; metres): the segment a-b and its radius. valid false: that hand holds no weapon.
struct SpacewarpWeaponLocal {bool valid{};std::array<double,3> a{},b{};double radius{};};
} // namespace d2vr
