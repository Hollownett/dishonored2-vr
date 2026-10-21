#include "../src/frame_split.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace d2vr;
static double field(const std::string& j,const std::string& k){const auto p=j.find("\""+k+"\":");assert(p!=std::string::npos);return std::stod(j.substr(p+k.size()+3));}
static bool near(double a,double b){return std::fabs(a-b)<1e-6;}
int main(){
    const std::int64_t F=10'000'000; // 1e7 ticks/s: 10000 ticks = 1 ms
    {FrameSplit m;m.feed({0,60000,110000,125000},-30000,F,0); // split_known_values
     const auto j=m.json();
     assert(near(field(j,"eye0_ms_p50"),6.0)&&near(field(j,"eye1_ms_p50"),5.0)&&near(field(j,"hook_ms_p50"),1.5));
     assert(near(field(j,"interval_ms_p50"),14.0)&&near(field(j,"interval_ms_p95"),14.0));
     assert(near(field(j,"eye1_share"),5./14)&&near(field(j,"frames"),1)&&near(field(j,"split_skipped"),0));}
    {FrameSplit m;m.feed({0,0,110000,125000},-30000,F,0); // missing_eye_frame_skipped
     const auto j=m.json();
     assert(near(field(j,"split_skipped"),1)&&near(field(j,"eye0_ms_p50"),0)&&near(field(j,"eye1_ms_p50"),0)&&near(field(j,"interval_ms_p50"),14.0)&&near(field(j,"frames"),1));}
    {FrameSplit m;m.feed({-1,60000,110000,125000},10000,F,0); // eye 0 never began (view index -1)
     assert(near(field(m.json(),"split_skipped"),1));}
    {FrameSplit m;m.feed({60000,0+30000,110000,125000},-30000,F,0); // out_of_order_skipped
     const auto j=m.json();
     assert(near(field(j,"split_skipped"),1)&&near(field(j,"eye0_ms_p50"),0)&&near(field(j,"eye1_ms_p50"),0));}
    {FrameSplit m;for(int i=0;i<10;++i) m.feed({0,60000,110000,125000},-30000,F,0); // gpu_busy_null_without_timer
     const auto j=m.json();assert(j.find("\"gpu_busy\":null")!=std::string::npos);
     FrameSplit g;g.feed({0,60000,110000,125000},-30000,F,7'000'000); // 7 ms of a 14 ms interval
     assert(near(field(g.json(),"gpu_busy"),.5));}
    {FrameSplit m;m.feed({0,60000,110000,125000},-30000,F,0); // coarse_thread_clock_reports_null
     assert(m.json().find("\"render_thread_busy\":null")!=std::string::npos&&m.json().find("\"game_thread_busy\":null")!=std::string::npos);
     m.thread_sample(-1,.5);
     auto j=m.json();assert(j.find("\"render_thread_busy\":null")!=std::string::npos&&near(field(j,"game_thread_busy"),.5));
     m.thread_sample(.25,-1);j=m.json();assert(near(field(j,"render_thread_busy"),.25)&&j.find("\"game_thread_busy\":null")!=std::string::npos);}
    {FrameSplit m;for(int i=0;i<1000;++i) m.feed({0,60000,110000+i,125000+i},-30000,F,0); // window_rolls_over
     assert(near(field(m.json(),"frames"),900));}
    {FrameSplit m;m.feed({0,60000,110000,125000},0,F,5); // first frame after a gap: no previous present, no interval
     assert(near(field(m.json(),"interval_ms_p50"),0));}
    {FrameSplit m;m.feed({20000,60000,110000,125000},30000,F,0); // stale_eye0_skipped: eye0 began before the previous Present
     const auto j=m.json();
     assert(near(field(j,"split_skipped"),1)&&near(field(j,"eye0_ms_p50"),0)&&near(field(j,"interval_ms_p50"),8.0)&&!m.last_accepted());
     m.feed({30000,60000,110000,125000},30000,F,0);assert(!m.last_accepted()); // eye0 exactly at prev is also stale
     m.feed({30001,60000,110000,125000},30000,F,0);assert(m.last_accepted());}
    {FrameSplit m; // multi_frame_percentiles: intervals 10..19 ms with one bad-eye frame in the middle
     for(int i=0;i<10;++i){const std::int64_t prev=1'000'000,enter=prev+(10+i)*10000;
         m.feed({i==5?prev-1:prev+1000,prev+30000,enter,enter+5000},prev,F,(10+i)*1'000'000ull/2);}
     const auto j=m.json();
     assert(near(field(j,"interval_ms_p50"),14)&&near(field(j,"interval_ms_p95"),19)&&near(field(j,"split_skipped"),1)&&near(field(j,"frames"),10));
     assert(near(field(j,"gpu_busy"),.5)&&near(field(j,"hook_ms_p50"),.5));}
    {FrameSplitTrace t;t.push(1,2,3,4,5,true);assert(t.drain()=="0,1,2,3,4,5,1\n"&&t.drain().empty());}
    std::cout<<"PASS: frame split\n";
}
