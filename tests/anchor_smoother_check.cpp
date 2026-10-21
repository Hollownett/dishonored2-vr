#include "../src/anchor_smoother.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <vector>
using namespace d2vr;
static constexpr double dt=.011;
static Vec3 P(double z,double x=0,double y=0){return {x,y,z};}
int main(){
    {AnchorSmoother s; // landing dip: 10 cm down and back over 120 ms
     for(int i=0;i<30;++i) s.update(P(1.6),dt,1);
     double worst=0;
     for(double t=0;t<.5;t+=dt){
         double z=1.6;if(t<.06) z=1.6-.1*t/.06;else if(t<.12) z=1.6-.1*(.12-t)/.06;
         worst=std::fmax(worst,1.6-s.update(P(z),dt,1).z);}
     assert(worst<=.04);assert(s.resets()==0);}
    {AnchorSmoother s; // crouch: 0.5 m step down
     for(int i=0;i<30;++i) s.update(P(1.6),dt,1);
     double prev=1.6,peak=0,t=0,z=1.6;
     for(;t<.6;t+=dt){z=s.update(P(1.1),dt,1).z;peak=std::fmax(peak,(prev-z)/dt);prev=z;}
     assert(peak<=1.5+1e-6);assert(std::fabs(z-1.1)<.02);assert(s.resets()==0);}
    {AnchorSmoother s; // a real fall passes through after 100 ms
     double raw=10;for(int i=0;i<5;++i) s.update(P(raw),dt,1);
     Vec3 o{};for(double t=0;t<.1+dt;t+=dt){raw-=5*dt;o=s.update(P(raw),dt,1);}
     assert(std::fabs(o.z-raw)<1e-9);}
    {AnchorSmoother s;s.update(P(1,0),dt,1);const auto o=s.update(P(1,3),dt,1);assert(o.x==3 && s.resets()==1);}
    {AnchorSmoother s;s.update(P(1),dt,1);const auto o=s.update(P(2.5),dt,1);assert(o.z==2.5 && s.resets()==1);}
    {AnchorSmoother s;s.update(P(1),dt,1);const auto o=s.update(P(1.5,.01,.02),dt,1);assert(o.x==.01 && o.y==.02);assert(s.resets()==0);}
    {AnchorSmoother s;s.update(P(1),dt,1);const auto o=s.update(P(1.2),dt,2);assert(o.z==1.2 && s.resets()==1);}
    {AnchorSmoother s;s.update(P(1),dt,1);
     assert(s.update(P(1.2),0,1).z==1.2 && s.resets()==1);
     assert(s.update(P(1.3),-1,1).z==1.3 && s.resets()==2);
     assert(s.update(P(1.4),.3,1).z==1.4 && s.resets()==3);
     assert(s.update(P(1.5),std::numeric_limits<double>::quiet_NaN(),1).z==1.5 && s.resets()==4);
     const auto o=s.update(P(std::numeric_limits<double>::quiet_NaN()),dt,1);assert(!std::isfinite(o.z) && s.resets()==5);
     assert(s.update(P(1),dt,1).z==1 && s.resets()==5);} // restart after bad input is not counted twice
    {AnchorSmoother s;for(int i=0;i<30;++i) s.update(P(1.6),dt,1);s.reset();assert(s.update(P(1.0),dt,1).z==1.0);}
    {AnchorSmoother s; // stairs: 0.2 m steps every 0.35 s
     double raw=0,prev=0,worst=0;s.update(P(0),dt,1);
     for(double t=0;t<5;t+=dt){raw=.2*std::floor(t/.35);const double z=s.update(P(raw),dt,1).z;worst=std::fmax(worst,std::fabs(z-prev));prev=z;}
     assert(worst<=.02);assert(s.resets()==0);}
    std::cout<<"PASS: anchor smoother\n";
}
