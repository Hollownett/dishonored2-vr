#include "../src/comfort_meter.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
using namespace d2vr;
static ComfortSample S(double t,double z=0,double ay=0,double hy=0,bool a=false){return {t,{0,0,z},ay,hy,a};}
static bool has(const std::string& j,const std::string& k){return j.find(k)!=std::string::npos;}
static double field(const std::string& j,const std::string& k){const auto p=j.find("\""+k+"\":");assert(p!=std::string::npos);return std::stod(j.substr(p+k.size()+3));}
int main(){
    const double dt=1./90;
    {ComfortMeter m;for(int i=0;i<180;++i) m.feed(S(i*dt,1.5));const auto j=m.json();
     assert(field(j,"anchor_v_peak")==0 && field(j,"anchor_jerk_events")==0 && field(j,"view_yaw_rate_peak")==0 && field(j,"view_yaw_events")==0);}
    {ComfortMeter m;for(int i=0;i<60;++i){const double t=i*dt;double z=0;
        if(t>=.2&&t<.25) z=-.1*(t-.2)/.05; else if(t>=.25) z=-.1;
        m.feed(S(t,z));}
     assert(field(m.json(),"anchor_jerk_events")>=1 && field(m.json(),"anchor_v_peak")>1);}
    {ComfortMeter m;for(int i=0;i<=90;++i) m.feed(S(i*dt,0,0,0,true));const auto j=m.json();
     assert(std::fabs(field(j,"authored_ms")-1000)<=12 && field(j,"authored_entries")==1);}
    {ComfortMeter m;for(int i=0;i<90;++i) m.feed(S(i*dt,0,0,3.*i*dt));assert(field(m.json(),"view_yaw_events")==0);
     ComfortMeter n;for(int i=0;i<90;++i) n.feed(S(i*dt,0,2.*i*dt));assert(field(n.json(),"view_yaw_events")>0);}
    {ComfortMeter m;for(int i=0;i<10;++i) m.feed(S(i*dt,0));
     m.feed(S(11*dt,std::numeric_limits<double>::quiet_NaN()));
     m.feed(S(10*dt+.5,5.0)); // stall: skipped, rebased, no 5 m jump counted
     m.feed(S(10*dt+.5+dt,5.0));
     const auto j=m.json();assert(field(j,"skipped")==2 && field(j,"anchor_v_peak")==0 && field(j,"anchor_jerk_events")==0);}
    {ComfortMeter m;m.feed(S(0,0,3.14));m.feed(S(dt,0,-3.14));assert(field(m.json(),"view_yaw_events")==0);} // wrap
    assert(has(ComfortMeter{}.json(),"\"skipped\":0"));
    std::cout<<"PASS: comfort meter\n";
}
