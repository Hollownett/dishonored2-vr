#include "arm_collision.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
using namespace d2vr;
static const double kPi=3.14159265358979323846;
static bool near(double a,double b,double e=1e-6){return std::fabs(a-b)<e;}
static const Vec3 SH{0,-.2,1.45},WR{.45,-.25,1.25},OUT{0,-1,0};
// natural elbow: down/out pole, bones 0.30 (upper) / 0.28 (fore)
static Vec3 natural(Vec3 s,Vec3 w){
    Vec3 d=w-s;double L=length(d);Vec3 a=d*(1.0/L);double u=.30,f=.28;
    double x=(L*L+u*u-f*f)/(2*L);double rho2=u*u-x*x;double rho=rho2>0?std::sqrt(rho2):0;
    Vec3 pole=unit(Vec3{0,-1,-1});Vec3 p=pole-a*dot(pole,a);
    Vec3 pd=length(p)>1e-9?unit(p):Vec3{0,0,0};
    return s+a*x+pd*rho;
}
static ArmPlane plane(Vec3 p,Vec3 n){ArmPlane q;q.point=p;q.normal=unit(n);q.radius=.055;q.last_hit=0;return q;}
static const ArmCollisionConfig cfg;
static void expect_clear(Vec3 e,const ArmPlane* p,std::size_t n){
    for(std::size_t i=0;i<n;++i) assert(dot(e-p[i].point,p[i].normal)>=p[i].radius+cfg.skin-1e-6);
}
static void side_wall_swivels(){
    Vec3 nat=natural(SH,WR);
    ArmPlane p=plane({0,nat.y+.03,1.3},{0,1,0});     // elbow sits 3 cm behind the wall
    SwivelResult r=solve_swivel(SH,WR,nat,OUT,&p,1,cfg);
    Vec3 e=swivel_elbow(SH,WR,nat,r.theta);
    assert(std::fabs(r.theta)>0&&r.feasible&&r.constrained);
    assert(swivel_violation(SH,WR,e,&p,1,cfg.skin)==0);
    assert(e.y>=p.point.y+.055+.005-1e-6);
}
static void table_under_elbow(){
    Vec3 nat=natural(SH,WR);
    ArmPlane p=plane({0,0,nat.z-.03},{0,0,1});
    SwivelResult r=solve_swivel(SH,WR,nat,OUT,&p,1,cfg);
    Vec3 e=swivel_elbow(SH,WR,nat,r.theta);
    assert(r.feasible&&std::fabs(r.theta)>0&&e.z>=p.point.z+.055+.005-1e-6);
}
static void corner_two_planes(){
    Vec3 nat=natural(SH,WR);
    ArmPlane p[2]={plane({0,nat.y-.04,1.3},{0,1,0}),plane({0,0,nat.z-.04},{0,0,1})};
    SwivelResult r=solve_swivel(SH,WR,nat,OUT,p,2,cfg);
    Vec3 e=swivel_elbow(SH,WR,nat,r.theta);
    assert(r.feasible&&swivel_violation(SH,WR,e,p,2,cfg.skin)==0);expect_clear(e,p,2);
}
static void straight_arm_residual(){
    Vec3 w=SH+unit(Vec3{.45,-.05,-.2})*.58;
    Vec3 nat=natural(SH,w);
    ArmPlane p=plane(nat,{0,0,1});
    SwivelResult r=solve_swivel(SH,w,nat,OUT,&p,1,cfg);
    assert(!r.feasible&&r.residual>0&&std::isfinite(r.theta)&&std::isfinite(r.residual));
    assert(std::fabs(r.theta)<=cfg.max_swivel_deg*kPi/180+1e-9);
}
static void inward_limit(){
    Vec3 nat=natural(SH,WR);
    ArmPlane p=plane({0,nat.y+.08,1.3},{0,1,0});     // clearing it needs ~0.14 m inward
    SwivelResult r=solve_swivel(SH,WR,nat,OUT,&p,1,cfg);
    assert(!r.feasible&&r.residual>0&&std::isfinite(r.theta));
    assert(std::fabs(r.theta)<=cfg.max_swivel_deg*kPi/180+1e-9);
    Vec3 e=swivel_elbow(SH,WR,nat,r.theta);
    assert(dot(e-nat,OUT)>=-cfg.max_inward-r.residual-1e-6);
}
static ArmGeometry geom(){ArmGeometry g;g.shoulder=SH;g.wrist=WR;g.elbow=natural(SH,WR);g.valid=true;return g;}
static void edge_discard(){
    ArmCollision c;ArmGeometry g=geom();
    std::array<std::optional<ArmHit>,2> h;
    ArmHit hit;hit.point={.3,-.3,1.3};hit.normal={0,-1,0};h[0]=hit;     // wrist is behind this plane
    c.observe(0,g,h,0);
    std::array<ArmPlane,ArmCollision::max_planes> out;
    assert(c.planes(0,out,0)==0&&c.stats(0).edge_discards==1&&c.stats(0).planes_made==0);
}
static void plane_expires(){
    ArmCollision c;ArmGeometry g=geom();
    std::array<std::optional<ArmHit>,2> h;
    ArmHit hit;hit.point={.3,-.3,1.3};hit.normal={0,1,0};h[0]=hit;
    c.observe(0,g,h,0);
    std::array<ArmPlane,ArmCollision::max_planes> out;
    assert(c.planes(0,out,.14)==1&&near(out[0].radius,.045));
    assert(c.planes(0,out,.16)==0);
    assert(c.planes(1,out,.1)==0);
}
static void refresh_not_duplicate(){
    ArmCollision c;ArmGeometry g=geom();
    std::array<std::optional<ArmHit>,2> h;
    ArmHit a;a.point={.3,-.3,1.3};a.normal={0,1,0};h[0]=a;c.observe(0,g,h,0);
    double s=std::sin(3*kPi/180),k=std::cos(3*kPi/180);
    ArmHit b;b.normal={0,k,s};b.point=a.point+b.normal*.01;h[0]=b;c.observe(0,g,h,.01);
    std::array<ArmPlane,ArmCollision::max_planes> out;
    assert(c.planes(0,out,.01)==1&&c.stats(0).refreshed==1&&c.stats(0).planes_made==1);
    assert(near(out[0].last_hit,.01));
}
// Frame of the swivel circle for a natural elbow: axis a, unit radial r0 (shoulder->axis foot to elbow), radius rho.
struct Circle{Vec3 a,r0;double rho;};
static Circle circle(Vec3 s,Vec3 w,Vec3 nat){
    Circle c;c.a=unit(w-s);Vec3 v=nat-s;Vec3 p=v-c.a*dot(v,c.a);c.rho=length(p);c.r0=p*(1.0/c.rho);return c;
}
static const Vec3 UP{0,0,1};   // "outward" for the corridor tests: never limits (|dz| < max_inward margin on this arm)
static void corridor_no_flip(){
    // Wall A (normal -r0) needs rho(1-cos t) >= .06 (|t| >= 53 deg); wall B (normal +r0) needs rho(1-cos t) <= .13 (|t| <= 82 deg):
    // feasible windows on BOTH sides of the natural elbow.
    ArmCollision c;
    double dt=1.0/90,theta=-1.0;   // follower starts on the negative side
    for(int i=0;i<200;++i){
        double now=i*dt;
        Vec3 w=WR;w.x+=(i%2?.003:-.003);w.y+=(i%3==0?.003:-.003);   // +-3 mm jitter per frame
        Vec3 nat=natural(SH,w);Circle ci=circle(SH,w,nat);
        ArmGeometry g;g.shoulder=SH;g.wrist=w;g.elbow=nat;g.valid=true;
        ArmHit a;a.point=nat;a.normal=ci.r0*-1.0;
        ArmHit b;b.point=nat-ci.r0*.19;b.normal=ci.r0;
        std::array<std::optional<ArmHit>,2> h;
        if(i%2==0)h[0]=a;else h[1]=b;                                  // alternating hit / miss pattern
        if(i==0)h[1]=b;                                                // both walls known from frame 0
        c.observe(0,g,h,now);
        std::array<ArmPlane,ArmCollision::max_planes> out;std::size_t n=c.planes(0,out,now);
        assert(n==2);
        if(i==0){   // feasible thetas exist on both sides
            bool neg=false,pos=false;
            for(int d=-110;d<=110;++d){
                Vec3 e=swivel_elbow(SH,w,nat,d*kPi/180);
                bool ok=swivel_violation(SH,w,e,out.data(),n,cfg.skin)==0&&dot(e-nat,UP)>=-cfg.max_inward;
                if(ok&&d<0)neg=true;if(ok&&d>0)pos=true;
            }
            assert(neg&&pos);
        }
        SwivelResult r=solve_swivel(SH,w,nat,UP,out.data(),n,cfg,theta);
        assert(r.feasible&&r.theta<0);                                 // side never changes
        double nt=follow_swivel(theta,r.theta,0,dt,cfg);
        assert(std::fabs(nt-theta)<=cfg.engage_rate*dt+1e-12);
        theta=nt;assert(theta<0);
    }
}
static void keeps_side(){
    Vec3 nat=natural(SH,WR);Circle ci=circle(SH,WR,nat);
    double phi=20*kPi/180;Vec3 n=(ci.r0*std::cos(phi)+cross(ci.a,ci.r0)*std::sin(phi))*-1.0;
    ArmPlane p=plane(nat,n);   // feasible: theta >= 77 deg or theta <= -37 deg
    SwivelResult r=solve_swivel(SH,WR,nat,UP,&p,1,cfg,0);
    assert(r.feasible&&r.theta<0&&r.theta>-45*kPi/180);                // nearer window
    r=solve_swivel(SH,WR,nat,UP,&p,1,cfg,100*kPi/180);
    assert(r.feasible&&r.theta>70*kPi/180&&r.theta<85*kPi/180);        // stays in the farther window
    r=solve_swivel(SH,WR,nat,UP,&p,1,cfg,-100*kPi/180);assert(r.feasible&&r.theta<0);
    r=solve_swivel(SH,WR,nat,UP,&p,1,cfg,3*kPi/180);assert(r.feasible&&r.theta<0);   // |prefer| <= 5 deg: ignored
}
static void infeasible_stays_near(){
    // plane almost normal to the axis: theta=0 violates by .020, theta=-100 deg (best grid angle) by .018
    Vec3 nat=natural(SH,WR);Circle ci=circle(SH,WR,nat);
    double t0=-100*kPi/180,sa=.002/(ci.rho*(1-std::cos(t0))),ca=std::sqrt(1-sa*sa);
    Vec3 w=ci.r0*std::cos(t0)+cross(ci.a,ci.r0)*std::sin(t0);
    Vec3 nrm=ci.a*ca+w*sa;
    ArmPlane p=plane(nat-nrm*.04,nrm);p.radius=.055;
    assert(near(swivel_violation(SH,WR,nat,&p,1,cfg.skin),.02,1e-6));
    assert(near(swivel_violation(SH,WR,swivel_elbow(SH,WR,nat,t0),&p,1,cfg.skin),.018,1e-6));
    SwivelResult r=solve_swivel(SH,WR,nat,OUT,&p,1,cfg);
    assert(!r.feasible&&std::fabs(r.theta)<10*kPi/180&&near(r.residual,.02,1e-3));
}
static void release_eases(){
    double th=1.0,dt=1.0/90;int frames=0;
    while(th>=.05&&frames<1000){
        double n=follow_swivel(th,0,0,dt,cfg);assert(n<=th&&n>=0);
        assert(th-n<=cfg.release_rate*dt+1e-12);
        if(frames==0)assert(th-n<.5*cfg.engage_rate*dt);
        th=n;++frames;
    }
    assert(frames>=3&&frames<1000);
    // fast path: target nearer zero but still violating -> engage step
    double e=follow_swivel(1.0,.9,.05,dt,cfg);assert(near(e,.9,1e-12));
    e=follow_swivel(1.0,.2,.05,dt,cfg);assert(near(e,1.0-cfg.engage_rate*dt,1e-12));
}
static void rotation_convention(){
    Vec3 e=swivel_elbow({0,0,0},{1,0,0},{0,1,0},kPi/2);
    assert(near(e.x,0)&&near(e.y,0)&&near(e.z,1));
}
static void engage_fast(){
    double th=0,dt=1.0/90;int f=0;
    while(th<.6-1e-9&&f<20){th=follow_swivel(th,.6,0,dt,cfg);++f;assert(th<=.6+1e-12);}
    assert(f<=5);
}
static void degenerate_inputs(){
    ArmCollision c;ArmGeometry g=geom();g.shoulder.x=NAN;
    auto cs=c.casts(g);assert(!cs[0]&&!cs[1]);
    ArmGeometry z;z.shoulder=z.elbow=z.wrist={0,0,1};z.valid=true;
    cs=c.casts(z);assert(!cs[0]&&!cs[1]);
    ArmGeometry iv=geom();iv.valid=false;cs=c.casts(iv);assert(!cs[0]&&!cs[1]);
    ArmPlane p=plane({0,0,1},{0,0,1});
    SwivelResult r=solve_swivel({NAN,0,0},WR,{0,0,0},OUT,&p,1,cfg);
    assert(r.theta==0&&r.feasible&&std::isfinite(r.residual));
    r=solve_swivel(SH,SH,SH,OUT,&p,1,cfg);assert(r.theta==0&&r.feasible&&std::isfinite(r.residual));
    // unconstrained arm returns exactly 0
    r=solve_swivel(SH,WR,natural(SH,WR),OUT,nullptr,0,cfg);assert(r.theta==0&&r.feasible&&!r.constrained);
    // normal casts exist for the good geometry, forearm starts skip beyond the wrist
    cs=c.casts(geom());assert(cs[0]&&cs[1]&&near(length(cs[0]->from-WR),cfg.forearm_skip)&&near(length(cs[1]->from-SH),cfg.upper_skip));
    // jump resets the side
    std::array<std::optional<ArmHit>,2> none;
    c.observe(0,geom(),none,0);ArmGeometry far=geom();far.wrist.x+=.5;c.observe(0,far,none,.01);
    assert(c.stats(0).resets==1);
}
static void answered_counts(){
    ArmCollision c;ArmGeometry g=geom();
    std::array<std::optional<ArmHit>,2> none;
    c.observe(0,g,none,0);assert(c.stats(0).casts==2);              // default: both casts counted for one call
    c.observe(1,g,none,0,1u<<0);c.observe(1,g,none,0,1u<<1);assert(c.stats(1).casts==2); // one call per answered part: 2 casts, not 4
    c.observe(1,g,none,0,0);assert(c.stats(1).casts==2);            // nothing answered: nothing counted
}
int main(){
    side_wall_swivels();table_under_elbow();corner_two_planes();straight_arm_residual();inward_limit();
    edge_discard();plane_expires();refresh_not_duplicate();corridor_no_flip();keeps_side();infeasible_stays_near();rotation_convention();release_eases();engage_fast();degenerate_inputs();answered_counts();
    std::printf("arm_collision_check ok\n");
}
