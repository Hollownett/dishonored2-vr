#include <cstdio>
#include <cstdlib>
#include "perf_mode_advisor.hpp"
using namespace d2vr;
#define CHECK(x) do{if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
static constexpr std::uint64_t P=11'111'111;
static void run(PerfModeAdvisor& a,unsigned n,double work,std::uint64_t stall) {for(unsigned i=0;i<n;++i) a.feed(static_cast<std::uint64_t>(P*work),P,stall);}
int main() {
    { PerfModeAdvisor a; run(a,899,1.3,4'000'000); CHECK(a.advice==PerfModeAdvice::none && a.decisions==0); }      // not a full window yet
    { PerfModeAdvisor a; run(a,900,.85,4'000'000); CHECK(a.advice==PerfModeAdvice::none && a.decisions==1); }      // within budget
    { PerfModeAdvisor a; run(a,900,1.35,4'000'000); CHECK(a.advice==PerfModeAdvice::gpu_bound && a.gpu_windows==1); } // the GPU-bound sim case
    { PerfModeAdvisor a; run(a,900,1.05,200'000); CHECK(a.advice==PerfModeAdvice::cpu_bound && a.cpu_windows==1); }   // the CPU-bound sim case
    { PerfModeAdvisor a; run(a,700,.9,200'000); run(a,200,1.2,4'000'000); CHECK(a.advice==PerfModeAdvice::none); }   // 22 % over: holds
    { PerfModeAdvisor a; run(a,900,1.35,4'000'000); run(a,900,.8,200'000); CHECK(a.advice==PerfModeAdvice::none && a.decisions==2); } // recovers
    { PerfModeAdvisor a; run(a,10,9,0); run(a,900,1.2,4'000'000); CHECK(a.decisions==1 && a.advice==PerfModeAdvice::gpu_bound); } // hitches ignored
    std::puts("perf_mode_advisor_check ok");
}
