#include "../src/anchor_smoother.hpp"
#include <cassert>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <vector>
using namespace d2vr;
static constexpr double dt=.011;
static Vec3 P(double z,double x=0,double y=0){return {x,y,z};}
// Raw-height inputs over time (starting settled at raw(0)).
static std::vector<std::vector<double>> inputs(){
    std::vector<std::vector<double>> out;
    {std::vector<double> r;double z=0,v=0;for(int i=0;i<30;++i) r.push_back(0);for(double t=dt;t<1.0;t+=dt){v+=9.81*dt;z-=v*dt;r.push_back(z);}for(int i=0;i<60;++i) r.push_back(z);out.push_back(r);} // free fall, then landing
    {std::vector<double> r;for(int i=0;i<30;++i) r.push_back(1.6);double z=1.6;while(z>1.1){z-=5*dt;r.push_back(z);}for(int i=0;i<80;++i) r.push_back(z);out.push_back(r);} // 5 m/s crouch ramp of 0.5 m
    {std::vector<double> r;for(int i=0;i<30;++i) r.push_back(0);double z=0;while(z<1.2){z+=2*dt;r.push_back(z);}for(int i=0;i<80;++i) r.push_back(z);out.push_back(r);} // mantle up 1.2 m at 2 m/s
    {std::vector<double> r;unsigned seed=12345;for(int i=0;i<400;++i){seed=seed*1664525u+1013904223u;r.push_back(1.6+((seed>>16)&0xff)/255.*.02-.01);}out.push_back(r);} // noise
    return out;
}
int main(){
    for(const auto& raw:inputs()){ // invariants over every input
        AnchorSmoother s;double prev_out=raw[0],prev_raw=raw[0];s.update(P(raw[0]),dt,1);
        for(std::size_t i=1;i<raw.size();++i){
            const double o=s.update(P(raw[i]),dt,1).z;
            assert(std::fabs(raw[i]-o)<=.20+1e-9);                                      // lag_never_exceeds_020
            assert(std::fabs(o-prev_out)<=std::fabs(raw[i]-prev_raw)+2.0*dt+1e-6);     // no_step_faster_than_raw_plus_catch_up
            prev_out=o;prev_raw=raw[i];}
        assert(s.resets()==0);
        assert(std::fabs(prev_out-prev_raw)<.02||raw.size()<100); // settled raw: the lag closes
    }
    {AnchorSmoother s; // a single-frame raw step is allowed to move the output by up to the raw step
     for(int i=0;i<30;++i) s.update(P(1.6),dt,1);
     const double o=s.update(P(1.1),dt,1).z;assert(std::fabs(o-1.1)<=.20+1e-9 && 1.6-o<=.5+1e-9);}
    {AnchorSmoother s; // landing dip: 10 cm down and back over 120 ms
     for(int i=0;i<30;++i) s.update(P(1.6),dt,1);
     double worst=0;
     for(double t=0;t<.5;t+=dt){
         double z=1.6;if(t<.06) z=1.6-.1*t/.06;else if(t<.12) z=1.6-.1*(.12-t)/.06;
         worst=std::fmax(worst,1.6-s.update(P(z),dt,1).z);}
     assert(worst<=.04);assert(s.resets()==0);}
    {AnchorSmoother s; // crouch ramp: the view is never faster than raw
     for(int i=0;i<30;++i) s.update(P(1.6),dt,1);
     double raw=1.6,prev=1.6,peak_out=0,peak_raw=0;
     for(int i=0;i<200;++i){const double r0=raw;if(raw>1.1) raw=std::fmax(1.1,raw-5*dt);
         const double o=s.update(P(raw),dt,1).z;peak_out=std::fmax(peak_out,(prev-o)/dt);peak_raw=std::fmax(peak_raw,(r0-raw)/dt);prev=o;}
     assert(peak_out<=peak_raw+1e-9);assert(std::fabs(prev-1.1)<.02);}
    {AnchorSmoother s; // stairs: 0.2 m steps every 0.35 s
     double raw=0,prev=0,worst=0;s.update(P(0),dt,1);
     for(double t=0;t<5;t+=dt){raw=.2*std::floor(t/.35);const double z=s.update(P(raw),dt,1).z;worst=std::fmax(worst,std::fabs(z-prev));prev=z;}
     assert(worst<=.02);assert(s.resets()==0);}
    {AnchorSmoother s;s.update(P(1,0),dt,1);const auto o=s.update(P(1,3),dt,1);assert(o.x==3 && s.resets()==1);}
    {AnchorSmoother s;s.update(P(1,0,0),dt,1);const auto o=s.update(P(1,0,3),dt,1);assert(o.y==3 && s.resets()==1);} // y teleport
    {AnchorSmoother s;s.update(P(1,0,0),dt,1);const auto o=s.update(P(1,0,.5),dt,1);assert(o.y==.5 && s.resets()==0);}
    {AnchorSmoother s;s.update(P(1),dt,1);const auto o=s.update(P(2.5),dt,1);assert(o.z==2.5 && s.resets()==1);} // blink up
    {AnchorSmoother s;s.update(P(1),dt,1);const auto o=s.update(P(1.5,.01,.02),dt,1);assert(o.x==.01 && o.y==.02);assert(s.resets()==0);}
    {AnchorSmoother s;s.update(P(1),dt,1);const auto o=s.update(P(1.2),dt,2);assert(o.z==1.2 && s.resets()==1);}
    {AnchorSmoother s;s.update(P(1),dt,1);
     assert(s.update(P(1.2),0,1).z==1.2 && s.resets()==1);
     assert(s.update(P(1.3),-1,1).z==1.3 && s.resets()==2);
     assert(s.update(P(1.4),.3,1).z==1.4 && s.resets()==3);
     assert(s.update(P(1.5),std::numeric_limits<double>::quiet_NaN(),1).z==1.5 && s.resets()==4);
     const auto o=s.update(P(std::numeric_limits<double>::quiet_NaN()),dt,1);assert(!std::isfinite(o.z) && s.resets()==5);
     assert(s.update(P(1),dt,1).z==1 && s.resets()==5);}
    {AnchorSmoother s;for(int i=0;i<30;++i) s.update(P(1.6),dt,1);s.reset();assert(s.update(P(1.0),dt,1).z==1.0);}
    std::cout<<"PASS: anchor smoother\n";
}
