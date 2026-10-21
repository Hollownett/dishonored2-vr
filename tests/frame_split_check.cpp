#include "../src/frame_split.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace d2vr;
static double field(const std::string& j,const std::string& k){const auto p=j.find("\""+k+"\":");assert(p!=std::string::npos);return std::stod(j.substr(p+k.size()+3));}
static bool near(double a,double b){return std::fabs(a-b)<1e-6;}
int main(){
    const std::int64_t F=10'000'000; // 1e7 ticks/s: 10000 ticks = 1 ms
    {FrameSplit m;m.feed({0,60000,110000,125000},-30000,F,0,0); // split_known_values
     const auto j=m.json();
     assert(near(field(j,"eye0_ms_p50"),6.0)&&near(field(j,"eye1_ms_p50"),5.0)&&near(field(j,"hook_ms_p50"),1.5));
     assert(near(field(j,"interval_ms_p50"),14.0)&&near(field(j,"interval_ms_p95"),14.0));
     assert(near(field(j,"eye1_share"),5./14)&&near(field(j,"frames"),1)&&near(field(j,"split_skipped"),0));}
    {FrameSplit m;m.feed({20000,0,110000,125000},10000,F,0,0,nullptr,nullptr,true); // single_view_frame_accepted_on_eye0 (alternate-eye: one view per Present)
     const auto j=m.json();
     assert(near(field(j,"split_skipped"),0)&&near(field(j,"eye0_ms_p50"),9.0)&&near(field(j,"eye1_ms_p50"),0)&&near(field(j,"interval_ms_p50"),10.0));}
    {FrameSplit m;m.feed({20000,0,110000,125000},10000,F,0,0); // single_view_off_still_skips_a_one_eye_frame (every other mode)
     assert(near(field(m.json(),"split_skipped"),1));}
    {FrameSplit m;m.feed({5000,0,110000,125000},10000,F,0,0,nullptr,nullptr,true); // single_view_still_needs_a_begin_after_the_previous_present
     assert(near(field(m.json(),"split_skipped"),1));}
    {FrameSplit m;m.feed({20000,60000,110000,125000},10000,F,0,0,nullptr,nullptr,true); // single_view_with_both_stamps_uses_the_two_eye_rule (paired refresh frame)
     const auto j=m.json();
     assert(near(field(j,"split_skipped"),0)&&near(field(j,"eye0_ms_p50"),4.0)&&near(field(j,"eye1_ms_p50"),5.0));}
    {FrameSplit m;m.feed({0,0,110000,125000},-30000,F,0,0); // missing_eye_frame_skipped
     const auto j=m.json();
     assert(near(field(j,"split_skipped"),1)&&near(field(j,"eye0_ms_p50"),0)&&near(field(j,"eye1_ms_p50"),0)&&near(field(j,"interval_ms_p50"),14.0)&&near(field(j,"frames"),1));}
    {FrameSplit m;m.feed({-1,60000,110000,125000},10000,F,0,0); // eye 0 never began (view index -1)
     assert(near(field(m.json(),"split_skipped"),1));}
    {FrameSplit m;m.feed({60000,0+30000,110000,125000},-30000,F,0,0); // out_of_order_skipped
     const auto j=m.json();
     assert(near(field(j,"split_skipped"),1)&&near(field(j,"eye0_ms_p50"),0)&&near(field(j,"eye1_ms_p50"),0));}
    {FrameSplit m;for(int i=0;i<10;++i) m.feed({0,60000,110000,125000},-30000,F,0,0); // gpu_busy_null_without_timer
     const auto j=m.json();assert(j.find("\"gpu_busy\":null")!=std::string::npos);
     FrameSplit g;g.feed({0,60000,110000,125000},-30000,F,7'000'000,13'000'000); // lists sum 7 ms, span 13 ms, of a 14 ms interval
     assert(near(field(g.json(),"gpu_busy"),.5)); // busy is the list sum, not the span
     assert(near(field(g.json(),"gpu_span"),13./14));
     assert(j.find("\"gpu_span\":null")!=std::string::npos);}
    {FrameSplit m;m.feed({0,60000,110000,125000},-30000,F,0,0); // coarse_thread_clock_reports_null
     assert(m.json().find("\"render_thread_busy\":null")!=std::string::npos&&m.json().find("\"game_thread_busy\":null")!=std::string::npos);
     m.thread_sample(-1,.5);
     auto j=m.json();assert(j.find("\"render_thread_busy\":null")!=std::string::npos&&near(field(j,"game_thread_busy"),.5));
     m.thread_sample(.25,-1);j=m.json();assert(near(field(j,"render_thread_busy"),.25)&&j.find("\"game_thread_busy\":null")!=std::string::npos);}
    {FrameSplit m;for(int i=0;i<1000;++i) m.feed({0,60000,110000+i,125000+i},-30000,F,0,0); // window_rolls_over
     assert(near(field(m.json(),"frames"),900));}
    {FrameSplit m;m.feed({0,60000,110000,125000},0,F,5,5); // first frame after a gap: no previous present, no interval
     assert(near(field(m.json(),"interval_ms_p50"),0));}
    {FrameSplit m;m.feed({20000,60000,110000,125000},30000,F,0,0); // stale_eye0_skipped: eye0 began before the previous Present
     const auto j=m.json();
     assert(near(field(j,"split_skipped"),1)&&near(field(j,"eye0_ms_p50"),0)&&near(field(j,"interval_ms_p50"),8.0)&&!m.last_accepted());
     m.feed({30000,60000,110000,125000},30000,F,0,0);assert(!m.last_accepted()); // eye0 exactly at prev is also stale
     m.feed({30001,60000,110000,125000},30000,F,0,0);assert(m.last_accepted());}
    {FrameSplit m; // multi_frame_percentiles: intervals 10..19 ms with one bad-eye frame in the middle
     for(int i=0;i<10;++i){const std::int64_t prev=1'000'000,enter=prev+(10+i)*10000;
         m.feed({i==5?prev-1:prev+1000,prev+30000,enter,enter+5000},prev,F,(10+i)*1'000'000ull/2,(10+i)*1'000'000ull);}
     const auto j=m.json();
     assert(near(field(j,"interval_ms_p50"),14)&&near(field(j,"interval_ms_p95"),19)&&near(field(j,"split_skipped"),1)&&near(field(j,"frames"),10));
     assert(near(field(j,"gpu_busy"),.5)&&near(field(j,"gpu_span"),1.)&&near(field(j,"hook_ms_p50"),.5));}
    {FrameSplitTrace t;t.push(1,2,3,4,5,true);assert(t.drain()=="0,1,2,3,4,5,1,0,-9,-9\n"&&t.drain().empty());
     t.push(1,2,3,4,5,false,2,1,1);assert(t.drain()=="1,1,2,3,4,5,0,2,1,1\n");
     assert(std::string(FrameSplitTrace::header())=="frame,eye0,eye1,enter,exit,prev,accepted,begins,idx_a,idx_b");}
    { // eye identity (plan E task 1 fix 2): the eye is the view ADDRESS against the published per-eye views, never the raw index.
      // Real XR sim run 20261021T120930: eye0 was -1 on all 6817 frames while eye1 was stamped, so `index==0` never fired.
      const std::uintptr_t L=0x1000,R=0x2000;
      for(int left_index : {0,1,-1,7}) { // whatever raw index the left view begins with, it is eye 0
          EyeStamps e;e.reset();
          assert(e.begin(L,L,R,left_index,100)==0&&e.begin(R,L,R,1,200)==1&&e.begin(0,L,R,-1,300)==-1);
          assert(e.eye_ns(0)==100&&e.eye_ns(1)==200&&e.begins()==2&&e.idx_a()==left_index&&e.idx_b()==1);
          FrameSplit m;m.feed({e.eye_ns(0),e.eye_ns(1),400,450},50,1'000'000'000,0,0);assert(m.last_accepted());}
      {EyeStamps e;e.reset(); // both views reporting the same raw index still yield two distinct eyes
       assert(e.begin(L,L,R,1,10)==0&&e.begin(R,L,R,1,20)==1&&e.eye_ns(0)==10&&e.eye_ns(1)==20);}
      {EyeStamps e;e.reset(); // order does not matter; a third, unknown view is ignored
       assert(e.begin(R,L,R,0,10)==1&&e.begin(L,L,R,0,20)==0&&e.begin(0x3000,L,R,0,30)==-1&&e.eye_ns(0)==20&&e.eye_ns(1)==10);}
      {EyeStamps e;e.reset(); // views not published yet (or equal): first non-null begin is the left eye, second the right
       assert(e.begin(0,0,0,-1,5)==-1&&e.begin(L,0,0,1,10)==0&&e.begin(R,0,0,1,20)==1&&e.begin(0x3000,0,0,1,30)==-1&&e.eye_ns(0)==10&&e.eye_ns(1)==20);
       e.reset();assert(e.begin(L,L,L,1,10)==0&&e.begin(R,L,L,1,20)==1);}
      {EyeStamps e;e.reset(); // reset between Presents clears both stamps and the order
       e.begin(L,L,R,0,10);e.begin(R,L,R,1,20);e.reset();
       assert(e.eye_ns(0)==-1&&e.eye_ns(1)==0&&e.begins()==0&&e.begin(L,L,R,0,30)==0);}
      {EyeStamps e;e.reset(); // a mono frame (one real view) stamps only eye 0 and is rejected by the window check
       e.begin(L,L,R,0,10);FrameSplit m;m.feed({e.eye_ns(0),e.eye_ns(1),400,450},5,1'000'000'000,0,0);assert(!m.last_accepted());}
    }
    { // GPU phase tagging (plan E task 3b): a list is tagged by the CPU phase it is executed in, from the eye stamps.
      const std::uintptr_t L=0x1000,R=0x2000;
      {EyeStamps e;e.reset(); // list_before_eye0_is_pre (also the edge: the eye stamps of its own frame do not exist yet)
       assert(e.phase(false)==Phase::pre);}
      {EyeStamps e;e.reset();e.begin(L,L,R,0,100); // list_between_eyes_is_eye0
       assert(e.phase(false)==Phase::eye0);}
      {EyeStamps e;e.reset();e.begin(L,L,R,0,100);e.begin(R,L,R,0,200); // list_after_eye1_is_eye1
       assert(e.phase(false)==Phase::eye1);}
      {EyeStamps e;e.reset();e.begin(L,L,R,0,100);e.begin(R,L,R,0,200); // list_in_hook_is_hook, whatever the stamps say
       assert(e.phase(true)==Phase::hook);EyeStamps f;f.reset();assert(f.phase(true)==Phase::hook);}
      {EyeStamps e;e.reset();e.begin(0,L,R,-1,100); // a GUI/null begin stamps nothing: still pre
       assert(e.phase(false)==Phase::pre);}
      {EyeStamps e;e.reset();e.begin(L,L,R,0,0); // a test clock may start at 0: eye 0 at 0 is a real stamp
       assert(e.phase(false)==Phase::eye0);}
      {EyeStamps e;e.reset();e.begin(L,L,R,0,100);e.begin(R,L,R,0,200);e.reset(); // the Present exit reset puts the next lists back in pre
       assert(e.phase(false)==Phase::pre);}
    }
    { // per_phase_sums_over_window: p50 of the per-frame phase sums and list counts, null when no frame carries a sample
      FrameSplit none;none.feed({0,60000,110000,125000},-30000,10'000'000,0,0);
      assert(none.json().find("\"gpu_phase_ms_p50\":null")!=std::string::npos&&none.json().find("\"mod_gpu_ms_p50\":null")!=std::string::npos);
      FrameSplit m;
      for(int i=0;i<5;++i){PhaseGpu p;p.valid=true;p.ms[0]=1+i;p.ms[1]=2;p.ms[2]=3;p.ms[3]=0;p.lists[0]=10+i;p.lists[1]=4;p.lists[2]=5;p.lists[3]=0;
          ModGpu g;g.valid=true;g.ms[0]=.5;g.ms[3]=.25*(i+1);
          m.feed({0,60000,110000,125000},-30000,10'000'000,0,0,&p,&g);}
      const auto j=m.json();
      assert(j.find("\"gpu_phase_ms_p50\":{\"pre\":3,\"eye0\":2,\"eye1\":3,\"hook\":0}")!=std::string::npos);
      assert(j.find("\"gpu_phase_lists_p50\":{\"pre\":12,\"eye0\":4,\"eye1\":5,\"hook\":0}")!=std::string::npos);
      assert(j.find("\"mod_gpu_ms_p50\":{\"fsr_eye0\":0.5,\"fsr_eye1\":0,\"aa_sharpen\":0,\"eye_copy\":0.75,")!=std::string::npos);
      assert(j.find("\"sum\":1.25}")!=std::string::npos);
      assert(std::string(phase_name(0))=="pre"&&std::string(phase_name(3))=="hook"&&std::string(mod_span_name(6))=="eye_capture");}
    std::cout<<"PASS: frame split\n";
}
