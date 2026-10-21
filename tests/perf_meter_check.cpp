#include "../src/perf_meter.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace d2vr;
static double field(const std::string& j,const std::string& k){const auto p=j.find("\""+k+"\":");assert(p!=std::string::npos);return std::stod(j.substr(p+k.size()+3));}
static bool near(double a,double b){return std::fabs(a-b)<1e-6;}
int main(){
    const std::uint64_t P=11'111'111;
    {PerfMeter m; // pace 1..100 ms, cpu 1..100 us
     for(std::uint64_t i=1;i<=100;++i) m.feed(i*1'000'000,P,false,i*1000,.77f);
     const auto j=m.json();
     assert(near(field(j,"pace_ms_p50"),50)&&near(field(j,"pace_ms_p95"),95));
     assert(near(field(j,"mod_cpu_ms_p95"),.095)&&near(field(j,"mod_cpu_ms_mean"),.0505));
     assert(near(field(j,"frames"),100)&&near(field(j,"fraction"),.77f)&&std::fabs(field(j,"period_ms")-11.111111)<1e-3);}
    {PerfMeter m;for(int i=0;i<200;++i) m.feed(P,P,i%8==0,0,1.f); // 25 of 200
     assert(near(field(m.json(),"missed_fraction"),.125));}
    {PerfMeter m; // 900 slow frames then 900 fast: the slow ones must be gone
     for(int i=0;i<900;++i) m.feed(50'000'000,P,true,2'000'000,1.f);
     for(int i=0;i<900;++i) m.feed(12'000'000,P,false,1'000'000,.9f);
     const auto j=m.json();
     assert(near(field(j,"frames"),900)&&near(field(j,"pace_ms_p95"),12)&&near(field(j,"missed_fraction"),0)&&near(field(j,"mod_cpu_ms_mean"),1));
     m.feed(30'000'000,P,true,1'000'000,.9f); // one more rolls exactly one old sample out
     assert(near(field(m.json(),"frames"),900)&&near(field(m.json(),"missed_fraction"),1./900));}
    {PerfMeter m;m.feed(9'000'000,0,true,1,.5f);
     assert(near(field(m.json(),"frames"),0)&&near(field(m.json(),"missed_fraction"),0));}
    {PerfMeter m;const auto j=m.json();assert(near(field(j,"frames"),0)&&near(field(j,"pace_ms_p50"),0));}
    std::cout<<"PASS: perf meter\n";
}
