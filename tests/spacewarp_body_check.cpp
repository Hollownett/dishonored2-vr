// Lab check of the v1460 spacewarp body rule's maths (src/spacewarp_math.hpp) against the full-body solver itself (src/body_ik.hpp, driven
// as FullBodyDriver::solve_locked drives it): the torso anchor's step between two tracked head poses is the solver's own neck step (the neck
// lands exactly under the eye, so the torso hangs from it), much closer than the eye's own step; and a leg point's own motion keeps a planted
// foot where it is in the world and carries a moving one with the play space.
//   build\night\lab.cmd spacewarp_body_check /Ithird_party\openxr\include
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define XR_NO_PROTOTYPES
#include "../src/spacewarp_math.hpp"
#include "../src/body_ik.hpp"
#include "../src/player_rig_data.hpp"
#include <cstdio>
#include <string>
#pragma comment(lib,"d3d11.lib")
#pragma comment(lib,"d3dcompiler.lib")

using namespace d2vr;
static body::Skeleton skeleton(const fullbody::data::FixtureJoints& joints) {  // as fullbody::skeleton
    body::Skeleton s;
    for(const auto& j:joints) {
        s.names.push_back(j.name);s.parent.push_back(j.parent);
        s.local.push_back({normalized(Quaternion{j.q[0],j.q[1],j.q[2],j.q[3]}),{j.t[0],j.t[1],j.t[2]}});
    }
    s.finish();return s;
}
static SwRigid make(double yaw_deg,double pitch_deg,std::array<double,3> t) {  // local space: yaw about +y, then pitch about +x (down < 0)
    const double y=yaw_deg*3.14159265358979/180,p=pitch_deg*3.14159265358979/180;
    SwRigid Y;Y.r={std::cos(y),0,std::sin(y),0,1,0,-std::sin(y),0,std::cos(y)};
    SwRigid P;P.r={1,0,0,0,std::cos(p),-std::sin(p),0,std::sin(p),std::cos(p)};
    SwRigid r=Y*P;r.t=t;return r;
}
// local (OpenXR: x right, y up, z back) <-> the solver's world (X forward, Y left, Z up), the play space at identity
static Vec3 to_world(const std::array<double,3>& v) {return {-v[2],-v[0],v[1]};}
static std::array<double,3> to_local(const Vec3& v) {return {-v.y,v.z,-v.x};}
// the tracked centre eye as FullBodyDriver::solve_locked hands it to the solver: camera_jnt looks along +X, +Z up; moved `back` along the
// level view (flat = forward + up * -forward.z)
static body::Xf solver_eye(const SwRigid& e,double back) {
    const auto col=[&](int c) {return std::array<double,3>{e.r[c],e.r[3+c],e.r[6+c]};};
    const auto x=col(0),y=col(1),z=col(2);
    const Vec3 fwd=to_world({-z[0],-z[1],-z[2]}),left=to_world({-x[0],-x[1],-x[2]}),up=to_world(y);
    body::Xf eye{body::from_axes(fwd,left,up),to_world(e.t)};
    auto flat=fwd+up*(-fwd.z);flat.z=0;
    if(length(flat)>1e-6) eye.p=eye.p-unit(flat)*back;
    return eye;
}
static double norm(const std::array<double,3>& a) {return std::sqrt(a[0]*a[0]+a[1]*a[1]+a[2]*a[2]);}
static std::array<double,3> minus(const std::array<double,3>& a,const std::array<double,3>& b) {return {a[0]-b[0],a[1]-b[1],a[2]-b[2]};}

int main() {
    int failures=0;const auto check=[&](bool ok,const std::string& what) {std::printf("%s %s\n",ok?"ok  ":"FAIL",what.c_str());failures+=!ok;};
    try {
        const double back=.08;
        struct Case {const char* name;SwRigid from,to;double eye_height;};
        const Case cases[]={
            {"lean (head translation 1 cm sideways, 0.5 cm down)",make(0,-10,{0,1.70,0}),make(0,-10,{.01,1.695,0}),1.70},
            {"pitch 3 deg down, level gaze, the eye itself still",make(0,0,{0,1.70,0}),make(0,-3,{0,1.70,0}),1.70},
            {"pitch 3 deg down, looking down 50 deg at the body",make(0,-50,{0,1.70,0}),make(0,-53,{0,1.70,0}),1.70},
            {"yaw 3 deg, looking down 40 deg",make(0,-40,{0,1.70,0}),make(3,-40,{0,1.70,0}),1.70},
            {"pitch about a real neck (the HMD swings 9 cm ahead, 12 cm above)",make(0,-30,{0,1.70,0}),make(0,-33,{0,1.70,0}),1.70},
            {"yaw, pitch and a step together, a taller avatar (scale 1.09)",make(10,-45,{.2,1.84,.1}),make(12,-47,{.206,1.837,.104}),1.85},
        };
        for(const auto& c:cases) {
            body::BodySolver solver(skeleton(fullbody::data::kBody),skeleton(fullbody::data::kLeftArm),skeleton(fullbody::data::kRightArm),{c.eye_height,0});
            const int neck=solver.skeleton(0).find("center_neck_jnt");
            const double scale=std::clamp(c.eye_height/solver.skeleton(0).bind[static_cast<std::size_t>(solver.skeleton(0).find("camera_jnt"))].p.z,.6,1.4);
            SwRigid from=c.from,to=c.to;
            if(std::string(c.name).find("real neck")!=std::string::npos) {  // the eye swings about a pivot 12 cm under and 9 cm behind it
                const auto swing=[](const SwRigid& e) {SwRigid r=e;const auto o=SwRigid{e.r,{0,0,0}}.apply({0,-.12,.09});
                    const std::array<double,3> pivot{0,1.70-.12,.09};for(int i=0;i<3;++i) r.t[i]=pivot[i]-o[i];return r;};
                from=swing(c.from);to=swing(c.to);
            }
            body::Inputs in;in.hand_valid[0]=in.hand_valid[1]=false;in.ground=0;
            in.eye=solver_eye(from,back);in.dt=1/45.;
            for(int k=0;k<40;++k) (void)solver.solve(in);  // settle: feet planted, the chest yaw on the head's
            in.dt=1/90.;const auto a=solver.solve(in).body[static_cast<std::size_t>(neck)].p;
            in.eye=solver_eye(to,back);const auto b=solver.solve(in).body[static_cast<std::size_t>(neck)].p;
            const auto truth=to_local(b-a);
            const auto model=spacewarp_torso_step(from,to,back,scale);
            const auto naive=minus(to.t,from.t);  // the eye's own step
            const double err=norm(minus(model,truth))*1000,naive_err=norm(minus(naive,truth))*1000,size=norm(truth)*1000;
            char line[400];std::snprintf(line,sizeof line,"%s: solver neck step %.2f mm, the torso anchor off by %.2f mm (the eye's own step: %.2f mm)",c.name,size,err,naive_err);
            // within 0.35 mm + 10 % of the solver's step (standing, a sideways step also tilts the torso, and the neck takes half of that:
            // the solver's own ~8 %), and never further off than the eye's own step
            check(err<=.35+.1*size && err<=naive_err+1e-6,line);
        }
    } catch(const std::exception& e) {std::printf("FAIL exception: %s\n",e.what());++failures;}
    {   // spacewarp_leg_own: the pass draws a leg point at L(P + r). L: the locomotion to the slot (4 cm along x, turned 1 deg about y)
        SwRigid L=make(1,0,{.04,0,0});
        const std::array<double,3> p{.1,-1.2,-.5};
        const auto drawn=[&](const std::array<double,3>& r) {return L.apply({p[0]+r[0],p[1]+r[1],p[2]+r[2]});};
        {   // a planted foot: no world step, so it stays where it is in the world whatever the locomotion
            const auto r=spacewarp_leg_own(p,{.14,-1.2,-.5},{0,0,0},.5,0,L);
            check(norm(minus(drawn(r),p))<1e-9,"planted foot: stays in the world while the play space moves on");
        }
        {   // a moving point (2 m/s): its step in the play space (p - carried) times the share, carried by the locomotion
            const std::array<double,3> carried{.06,-1.2,-.48};const auto r=spacewarp_leg_own(p,carried,{.03,0,.01},.5,2,L);
            const auto want=drawn({.5*(p[0]-carried[0]),.5*(p[1]-carried[1]),.5*(p[2]-carried[2])});
            check(norm(minus(drawn(r),want))<1e-9,"moving leg point: its own step in the play space, carried by the locomotion (as a hand)");
        }
        {   // 0.5 m/s: halfway between the two
            const std::array<double,3> carried{.06,-1.2,-.48},step{.02,0,0};
            const auto r=spacewarp_leg_own(p,carried,step,.5,.5,L),rp=spacewarp_leg_own(p,carried,step,.5,0,L),rm=spacewarp_leg_own(p,carried,step,.5,2,L);
            check(norm(minus(r,{(rp[0]+rm[0])/2,(rp[1]+rm[1])/2,(rp[2]+rm[2])/2}))<1e-9,"0.5 m/s: halfway between planted and moving");
        }
        {   // even locomotion (the last step equals the slot's): both rules agree
            // walked 8 cm (half of it to the slot: L2), swung 6 cm on in the play space: 14 cm in the world; carried = the previous point + 8 cm
            SwRigid L2;L2.t={.04,0,0};const std::array<double,3> carried{p[0]-.06,p[1],p[2]},world_step{.14,0,0};
            const auto rp=spacewarp_leg_own(p,carried,world_step,.5,0,L2),rm=spacewarp_leg_own(p,carried,world_step,.5,2,L2);
            check(norm(minus(rp,rm))<1e-9,"even steps: the planted and the moving rule draw the same point");
        }
    }
    std::printf("%s\n",failures?"FAILED":"all passed");return failures?1:0;
}
