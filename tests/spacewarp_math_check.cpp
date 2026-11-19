// Lab check of src/spacewarp_math.hpp: the generated camera and hand motions against direct world-space construction.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define XR_NO_PROTOTYPES
#include "../src/spacewarp_math.hpp"
#include <cstdio>
#include <string>
#pragma comment(lib,"d3d11.lib")
#pragma comment(lib,"d3dcompiler.lib")

using namespace d2vr;
static SwRigid make(double yaw_deg,double pitch_deg,std::array<double,3> t) {  // yaw about y, then pitch about x
    const double y=yaw_deg*3.14159265358979/180,p=pitch_deg*3.14159265358979/180;
    SwRigid Y;Y.r={std::cos(y),0,std::sin(y),0,1,0,-std::sin(y),0,std::cos(y)};
    SwRigid P;P.r={1,0,0,0,std::cos(p),-std::sin(p),0,std::sin(p),std::cos(p)};
    SwRigid r=Y*P;r.t=t;return r;
}
static double diff(const SwRigid& a,const SwRigid& b) {double d=0;for(int i=0;i<9;++i) d=std::max(d,std::abs(a.r[i]-b.r[i]));for(int i=0;i<3;++i) d=std::max(d,std::abs(a.t[i]-b.t[i]));return d;}

int main() {
    int failures=0;const auto check=[&](bool ok,const std::string& what) {std::printf("%s %s\n",ok?"ok  ":"FAIL",what.c_str());failures+=!ok;};
    const SwRigid O=make(37,3,{12,-4,2}),V=make(-20,8,{.1,1.6,-.3});const SwRigid S=O*V;  // a play space, a tracked view, the camera they make
    {   // nothing moves: everything is the identity
        const auto m=spacewarp_eye_motion(S,V,SwRigid{},SwRigid{},{SwRigid{},SwRigid{}});
        check(diff(m.target,SwRigid{})<1e-12 && diff(m.rest,SwRigid{})<1e-12 && diff(m.hand[0],SwRigid{})<1e-12,"identity");
    }
    {   // the general case: the target camera in source coordinates equals S^-1 * D with D built in the world
        const SwRigid dO=make(4,0,{.05,0,.02}),dV=make(2,-1,{.003,0,-.001}),dG0=make(5,2,{.02,.01,0}),dG1=make(-3,0,{0,.01,.03});
        const auto m=spacewarp_eye_motion(S,V,dO,dV,{dG0,dG1});
        const SwRigid D=dO*O*(dV*V);
        check(diff(S*m.target,D)<1e-9,"target camera: S * target == dO * O * dV * V");
        // a point on hand 0 (world X) moves with its grip and the play space: X' = dO O dG0 O^-1 X; in camera coordinates P' = hand * P
        const std::array<double,3> X{12.4,-3.6,2.9};const auto P=S.inverse().apply(X);
        const auto Xm=(dO*O*dG0*O.inverse()).apply(X);const auto Pm=m.hand[0].apply(P);const auto Pw=S.apply(Pm);
        check(std::abs(Pw[0]-Xm[0])+std::abs(Pw[1]-Xm[1])+std::abs(Pw[2]-Xm[2])<1e-9,"hand point follows its grip and the play space");
        const auto Xr=dO.apply(X);const auto Pr=S.apply(m.rest.apply(P));
        check(std::abs(Pr[0]-Xr[0])+std::abs(Pr[1]-Xr[1])+std::abs(Pr[2]-Xr[2])<1e-9,"rest of the rig follows the play space");
    }
    {   // a share of a motion
        const SwRigid r=make(20,0,{2,0,0});const auto h=r.power(.5);
        check(std::abs(h.angle()-10*3.14159265358979/180)<1e-9 && std::abs(h.t[0]-1)<1e-12,"power(0.5): half the angle, half the step");
        check(diff(h*h,SwRigid{r.r,{0,0,0}}*SwRigid{})<1e-9 || std::abs((h*h).angle()-r.angle())<1e-9,"power(0.5) twice turns as far as the whole");
    }
    {   // pose round trip and the world-camera axes
        const XrPosef p{{.1f,.2f,-.3f,.927f},{1.f,2.f,3.f}};const auto q=sw_to_pose(sw_from_pose(p));
        const double n=std::sqrt(.1*.1+.2*.2+.3*.3+.927*.927);
        check(std::abs(q.orientation.x-.1/n)<1e-5 && std::abs(q.orientation.w-.927/n)<1e-5 && std::abs(q.position.z-3)<1e-6,"XrPosef round trip");
        const auto c=sw_from_world_camera({1,2,3},{{{0,1,0},{-1,0,0},{0,0,1}}});  // forward +y, left -x, up +z
        const auto right=c.apply({1,0,0}),back=c.apply({0,0,1});  // camera x (right) and z (back) in the world, offset by the position
        check(std::abs(right[0]-2)<1e-12 && std::abs(back[1]-1)<1e-12,"world camera: right = -left, back = -forward");
    }
    std::printf("%s\n",failures?"FAILED":"all passed");return failures?1:0;
}
