#include "grab_force_cap.hpp"
#include "dwell_timer.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
using namespace d2vr;
#define CHECK(x) do{if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
namespace {
const double nan_value=std::nan("");
bool near(double a,double b,double tol=1e-9) {return std::fabs(a-b)<=tol;}
const Vec3 X{1,0,0},Y{0,1,0},zero{};
GrabTuning scale1() {GrabTuning t;t.strength_scale=1;return t;}
std::filesystem::path write_file(const char* name,const std::string& text) {
    auto p=std::filesystem::temp_directory_path()/name;
    std::ofstream(p,std::ios::binary)<<text;
    return p;
}
void expect_defaults_with_error(const char* name,const std::string& text) {
    auto r=load_grab_tuning(write_file(name,text));
    GrabTuning d;
    CHECK(r.source==TuningSource::defaults);
    CHECK(!r.error.empty());
    CHECK(r.value.axial_n==d.axial_n && r.value.lateral_n==d.lateral_n && r.value.torque_nm==d.torque_nm);
    CHECK(r.value.strength_scale==d.strength_scale && r.value.lift_fraction==d.lift_fraction);
    CHECK(r.value.mass_floor_kg==d.mass_floor_kg && r.value.mass_ceiling_kg==d.mass_ceiling_kg);
    CHECK(r.value.hand_kg==d.hand_kg && r.value.dt_max==d.dt_max && !r.value.tow && !r.value.use_measured_mass);
}
}
int main() {
    // axial_cap_300n
    {
        auto c=cap_velocity_step(zero,zero,X*10,zero,X,10,1,.01,scale1());
        CHECK(near(c.linear.x,300*.01/12) && near(c.linear.y,0) && c.linear_capped);
        auto d=cap_velocity_step(zero,zero,X*10,zero,X,10,1,.01,GrabTuning{});
        CHECK(near(d.linear.x,900*.01/12) && d.linear_capped);
    }
    // lateral_cap_90n
    {
        auto c=cap_velocity_step(zero,zero,Y*10,zero,X,10,1,.01,scale1());
        CHECK(near(length(c.linear),90*.01/12) && c.linear_capped);
        auto d=cap_velocity_step(zero,zero,Y*10,zero,X,10,1,.01,GrabTuning{});
        CHECK(near(length(d.linear),270*.01/12));
    }
    // light_limb_follows
    {
        auto c=cap_velocity_step(zero,zero,X*.3,zero,X,.6,1,.01,scale1());
        CHECK(!c.linear_capped && c.linear.x==.3 && c.linear.y==0);
    }
    // mass_floor_and_ceiling
    {
        auto a=cap_velocity_step(zero,zero,X*10,zero,X,.01,1,.01,scale1());
        CHECK(near(a.linear.x,300*.01/2.5));
        auto b=cap_velocity_step(zero,zero,X*10,zero,X,500,1,.01,scale1());
        CHECK(near(b.linear.x,300*.01/62));
    }
    // torque_cap
    {
        auto c=cap_velocity_step(zero,zero,zero,Y*100,X,10,.02,.01,scale1());
        CHECK(near(length(c.angular),30*.01/.02) && c.angular_capped);
        CHECK(near(c.implied_torque_nm,30,1e-6));
    }
    // two_hands_share_half
    {
        GrabTuning t;
        CHECK(near(effective_mass(20,t,.5),12) && near(effective_mass(20,t),22));
        auto c=cap_velocity_step(zero,zero,X*10,zero,X,20,1,.01,scale1(),.5);
        CHECK(near(c.linear.x,300*.01/12));
    }
    // hitch_dt_clamped
    {
        auto a=cap_velocity_step(zero,zero,X*10,Y,X,10,.02,.5,scale1());
        auto b=cap_velocity_step(zero,zero,X*10,Y,X,10,.02,1./30,scale1());
        CHECK(a.linear.x==b.linear.x && a.angular.y==b.angular.y);
        CHECK(near(a.linear.x,300./30/12));
        Vec3 vo{1,2,3},wo{4,5,6};
        for(double dt:{0.,-1.,nan_value}) {
            auto c=cap_velocity_step(vo,wo,X*10,Y,X,10,1,dt,scale1());
            CHECK(c.linear.x==1 && c.linear.y==2 && c.linear.z==3 && c.angular.x==4 && c.angular.z==6);
            CHECK(!c.linear_capped && !c.angular_capped);
        }
    }
    // non_finite_motor_holds
    {
        Vec3 vo{1,2,3},wo{4,5,6};
        auto a=cap_velocity_step(vo,wo,Vec3{nan_value,0,0},zero,X,10,1,.01,scale1());
        auto b=cap_velocity_step(vo,wo,zero,Vec3{0,nan_value,0},X,10,1,.01,scale1());
        for(auto c:{a,b}) {
            CHECK(c.linear.x==1 && c.linear.y==2 && c.linear.z==3 && c.angular.x==4 && c.angular.y==5 && c.angular.z==6);
            CHECK(!c.linear_capped && !c.angular_capped);
        }
    }
    // bad axis falls back to whole delta as lateral
    {
        auto c=cap_velocity_step(zero,zero,X*10,zero,Vec3{nan_value,0,0},10,1,.01,scale1());
        CHECK(near(c.linear.x,90*.01/12));
        auto d=cap_velocity_step(zero,zero,X*10,zero,X*5,10,1,.01,scale1());
        CHECK(near(d.linear.x,90*.01/12));
    }
    // bone_inertia
    CHECK(near(bone_inertia(10,.5),.4*10*.25) && bone_inertia(0,0)==1e-4);
    // implied_force_within_cap_1pct
    {
        unsigned s=12345;
        auto rnd=[&]{s=s*1664525u+1013904223u;return (double)(s>>8)/(double)(1u<<24)*2-1;};
        GrabTuning t;
        for(int i=0;i<1000;++i) {
            Vec3 vo{rnd()*3,rnd()*3,rnd()*3},vm{rnd()*20,rnd()*20,rnd()*20},wo{rnd(),rnd(),rnd()},wm{rnd()*50,rnd()*50,rnd()*50};
            Vec3 axis{rnd(),rnd(),rnd()};axis=unit(axis);
            double kg=(rnd()+1)*30,inertia=.01+(rnd()+1)*.5,dt=.001+(rnd()+1)*.02,share=i%2?1.:.5;
            auto c=cap_velocity_step(vo,wo,vm,wm,axis,kg,inertia,dt,t,share);
            double m=effective_mass(kg,t,share),edt=std::fmin(dt,t.dt_max);
            Vec3 dv=c.linear-vo;
            double ax=dot(dv,axis);
            Vec3 lat=dv-axis*ax;
            CHECK(std::fabs(ax)*m/edt<=t.axial_cap()*1.01);
            CHECK(length(lat)*m/edt<=t.lateral_cap()*1.01);
            CHECK(c.implied_force_n<=std::hypot(t.axial_cap(),t.lateral_cap())*1.01);
            CHECK(c.implied_torque_nm<=t.torque_cap()*1.01);
        }
    }
    // tuning loader
    {
        auto missing=load_grab_tuning(std::filesystem::temp_directory_path()/"d2vr_no_such_grab_tuning.cfg");
        CHECK(missing.source==TuningSource::defaults && missing.error.empty() && missing.value.axial_n==300);
        auto r=load_grab_tuning(write_file("d2vr_gt_ok.cfg","# comment\naxial_n 250\nstrength_scale 2\ntow 1\n\nlift_fraction .25\n"));
        CHECK(r.source==TuningSource::file && r.error.empty());
        CHECK(r.value.axial_n==250 && r.value.strength_scale==2 && r.value.tow && r.value.lift_fraction==.25);
        CHECK(r.value.lateral_n==90 && !r.value.use_measured_mass && r.value.axial_cap()==500);
        expect_defaults_with_error("d2vr_gt_unknown.cfg","bogus 1\n");
        expect_defaults_with_error("d2vr_gt_nan.cfg","axial_n 250\nlateral_n nan\n");
        expect_defaults_with_error("d2vr_gt_novalue.cfg","axial_n 250\ntow\n");
        expect_defaults_with_error("d2vr_gt_big.cfg",std::string(5000,'#'));
        struct B {const char* k;double lo,hi;};
        const B bounds[]={{"axial_n",50,600},{"lateral_n",20,300},{"torque_nm",5,60},{"strength_scale",.5,10},
                          {"lift_fraction",0,1},{"mass_floor_kg",.1,5},{"mass_ceiling_kg",10,200}};
        for(auto& b:bounds) {
            for(double v:{b.lo-(b.hi-b.lo)*.01-1e-6,b.hi+(b.hi-b.lo)*.01+1e-6}) {
                char buf[96];std::snprintf(buf,sizeof buf,"axial_n 250\n%s %.9g\n",b.k,v);
                expect_defaults_with_error("d2vr_gt_oob.cfg",buf);
            }
            for(double v:{b.lo,b.hi}) {
                char buf[96];std::snprintf(buf,sizeof buf,"%s %.9g\n",b.k,v);
                CHECK(load_grab_tuning(write_file("d2vr_gt_edge.cfg",buf)).source==TuningSource::file);
            }
        }
    }
    // DwellTimer
    {
        DwellTimer t{.25};
        CHECK(!t.update(true,.1) && !t.update(true,.1) && t.update(true,.1));
        CHECK(!t.update(false,.1) && t.seconds==0);
        CHECK(!t.update(true,.2) && !t.update(true,nan_value) && !t.update(true,-1) && t.update(true,.05));
        t.reset();
        CHECK(t.seconds==0 && !t.update(true,.2));
    }
    std::puts("grab_force_cap_check passed");
    return 0;
}
