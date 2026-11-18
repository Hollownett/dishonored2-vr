#include "../src/xr_fault_policy.hpp"
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do{if(!(x)){std::printf("FAIL line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
using A=d2vr::XrFaultPolicy::Action;
int main() {
    {   // A single fault in a running session is ridden through; the session is not torn down.
        d2vr::XrFaultPolicy p;
        CHECK(p.fault(1000,false,true)==A::ride_through);
        CHECK(p.ridden==1 && p.recoveries==0);
    }
    {   // A fatal fault, or one without a running session, always recovers.
        d2vr::XrFaultPolicy p;
        CHECK(p.fault(1000,true,true)==A::recover);
        CHECK(p.fault(1001,false,false)==A::recover);
        CHECK(p.ridden==0);
    }
    {   // Sporadic faults (one a second) never add up to a recovery, however long the session runs.
        d2vr::XrFaultPolicy p;
        for(unsigned i=0;i<600;++i) {CHECK(p.fault(1000+i*1000ull,false,true)==A::ride_through);p.frame_ok(1500+i*1000ull,true);} // clean frames in between
        CHECK(p.storms==0);
    }
    {   // A Steam Link blip: a fault on every frame (90 Hz) for 2 s is ridden through.
        d2vr::XrFaultPolicy p;
        for(unsigned i=0;i<180;++i) CHECK(p.fault(5000+i*11ull,false,true)==A::ride_through);
        CHECK(p.storms==0);
        // After a quiet second with clean frames the next run starts over: another 2 s blip is ridden through too.
        p.frame_ok(7500,true);
        for(unsigned i=0;i<180;++i) CHECK(p.fault(8000+i*11ull,false,true)==A::ride_through);
    }
    {   // A state that faults every frame for 3 s is a storm and recovers.
        d2vr::XrFaultPolicy q;A last=A::ride_through;std::uint64_t t=10000;
        for(unsigned n=0;n<1000 && last==A::ride_through;++n) {t=10000+n*11ull;last=q.fault(t,false,true);}
        CHECK(last==A::recover && t>=13000 && t<13011+11 && q.storms==1);
        // Every other frame at 45 fps (one fault each 44 ms) is a storm too.
        d2vr::XrFaultPolicy r;last=A::ride_through;
        for(unsigned n=0;n<1000 && last==A::ride_through;++n) {t=50000+n*44ull;last=r.fault(t,false,true);r.frame_ok(t+22,true);}
        CHECK(last==A::recover && t>=53000 && t<53044+44);
        // A fault that blocks for 1 s on every Present (an image wait timing out) never forms a run, but with no clean Present in
        // between it is a storm after 3 s.
        d2vr::XrFaultPolicy b;last=A::ride_through;
        for(unsigned n=0;n<100 && last==A::ride_through;++n) {t=70000+n*1000ull;last=b.fault(t,false,true);}
        CHECK(last==A::recover && t==73000 && b.storms==1);
    }
    {   // Faults 0.6 s apart never form a run: no storm however long it goes on.
        d2vr::XrFaultPolicy p;
        for(unsigned i=0;i<500;++i) {CHECK(p.fault(20000+i*600ull,false,true)==A::ride_through);p.frame_ok(20300+i*600ull,true);}
    }
    {   // Backoff: 1, 2, 4, 8, 10, 10 s while recoveries keep coming ...
        d2vr::XrFaultPolicy p;
        CHECK(p.recovered()==1000 && p.recovered()==2000 && p.recovered()==4000 && p.recovered()==8000 && p.recovered()==10000 && p.recovered()==10000);
        CHECK(p.recoveries==6);
        // ... and back to 1 s after the recovered session showed frames for 30 s.
        p.frame_ok(100000,true);
        p.frame_ok(129999,true);CHECK(p.recoveries==6);
        p.frame_ok(130000,true);CHECK(p.recoveries==0 && p.backoff_resets==1);
        CHECK(p.recovered()==1000);
        // A recovery restarts the healthy clock: a short stretch does not reset it.
        p.frame_ok(200000,true);p.frame_ok(210000,true);CHECK(p.recoveries==1);
        // Time without frames shown (session idle, headset off) is not healthy: the clock starts again when frames show.
        p.frame_ok(240000,false);p.frame_ok(250000,true);p.frame_ok(279999,true);CHECK(p.recoveries==1);
        p.frame_ok(280000,true);CHECK(p.recoveries==0);
    }
    {   // A recovery clears the burst window: the new session starts with a clean count.
        d2vr::XrFaultPolicy p;
        for(unsigned i=0;i<200;++i) p.fault(1000+i*11ull,false,true); // 2.2 s of faults
        p.recovered();
        for(unsigned i=0;i<200;++i) CHECK(p.fault(3300+i*11ull,false,true)==A::ride_through); // the run restarted at the recovery
    }
    {   // A clean Present ends the no-clean-frame streak.
        d2vr::XrFaultPolicy p;
        CHECK(p.fault(1000,false,true)==A::ride_through);
        p.frame_ok(2000,false); // a clean Present, even without a shown frame (session idle)
        CHECK(p.fault(4500,false,true)==A::ride_through); // 3.5 s after the first fault, but a clean Present came between
    }
    // Session-fatal results.
    CHECK(d2vr::xr_result_session_fatal(XR_ERROR_SESSION_LOST) && d2vr::xr_result_session_fatal(XR_ERROR_INSTANCE_LOST));
    CHECK(d2vr::xr_result_session_fatal(XR_ERROR_SESSION_NOT_RUNNING) && d2vr::xr_result_session_fatal(XR_ERROR_HANDLE_INVALID));
    CHECK(!d2vr::xr_result_session_fatal(XR_ERROR_RUNTIME_FAILURE) && !d2vr::xr_result_session_fatal(XR_ERROR_LAYER_INVALID));
    CHECK(!d2vr::xr_result_session_fatal(XR_ERROR_POSE_INVALID) && !d2vr::xr_result_session_fatal(XR_ERROR_CALL_ORDER_INVALID));
    CHECK(!d2vr::xr_result_session_fatal(XR_SUCCESS) && !d2vr::xr_result_session_fatal(XR_TIMEOUT_EXPIRED));
    std::puts("xr_fault_policy_check OK");
}
