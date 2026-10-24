#include "../src/finger_touch_curl.hpp"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
namespace r=d2vr::rig;
namespace {
using In=r::FingerTouchInput;
const r::FingerTouchParams P{};
In touched(float trigger,float grip,bool thumb=false) {In i;i.trigger=trigger;i.grip=grip;i.trigger_touch=true;i.thumb_touch=thumb;i.touch_available=true;return i;}
r::FingerTouchOutput settle(r::FingerTouchCurl& c,unsigned side,const In& in,bool held=false,double seconds=2,double hz=90) {
    r::FingerTouchOutput o;for(int i=0,n=static_cast<int>(seconds*hz);i<n;++i) o=c.update(side,in,1/hz,held,P);return o;
}
bool near(float a,float b,float tol=1e-4f) {return std::abs(a-b)<=tol;}
// Time to 63% of a step A: a linear slew at `rate` into the first-order lag of smooth_rate (step 1-e^-t/tau), closed form, by bisection.
double analytic_63(double amplitude,double rate,double smooth) {
    const double tau=1/smooth,ramp=amplitude/rate;
    const auto y=[&](double t) {
        if(t<=ramp) return rate*(t-tau*(1-std::exp(-t/tau)));
        const double at=rate*(ramp-tau*(1-std::exp(-ramp/tau)));
        return amplitude-(amplitude-at)*std::exp(-(t-ramp)/tau);
    };
    double lo=0,hi=5;for(int i=0;i<80;++i) {const double m=(lo+hi)/2;(y(m)<.63*amplitude?lo:hi)=m;}
    return lo;
}
double measured_63(bool thumb,double amplitude) {
    r::FingerTouchCurl c;const double dt=.001;In in=touched(0,0,thumb);in.trigger_touch=!thumb;
    for(int i=1;i<=5000;++i) {const auto o=c.update(0,in,dt,false,P);if(o.curl[thumb?0:1]>=.63*amplitude) return i*dt;}
    return -1;
}
}
int main() {
    {   // index_rest_settles_to_touch_value
        r::FingerTouchCurl c;const auto o=settle(c,0,touched(0,0));
        assert(o.mapping==r::CurlMapping::touch && near(o.curl[1],.3f) && !o.floor_active && !o.touch_silent);
    }
    {   // index_adds_trigger
        r::FingerTouchCurl c;const auto o=settle(c,0,touched(.5f,0));
        assert(near(o.curl[1],std::min(1.f,.5f+.3f)));
        r::FingerTouchCurl d;assert(near(settle(d,0,touched(.9f,0)).curl[1],1.f));
        r::FingerTouchCurl e;auto in=touched(.5f,0);in.trigger_touch=false;assert(near(settle(e,0,in).curl[1],.5f)); // untouched: no rest, the pull alone
    }
    {   // grip_mapping: grip*1.3-0.1 clamped
        for(auto [grip,want]:{std::pair{.07f,0.f},{.08f,.004f},{.85f,1.f},{.5f,.55f},{1.f,1.f},{0.f,0.f}}) {
            r::FingerTouchCurl c;const auto o=settle(c,0,touched(0,grip));
            for(unsigned f=2;f<5;++f) assert(near(o.curl[f],want));
        }
    }
    {   // thumb_touch_closes_and_release_opens
        r::FingerTouchCurl c;assert(near(settle(c,0,touched(0,0,true)).curl[0],1.f));
        assert(near(settle(c,0,touched(0,0,false)).curl[0],0.f));
    }
    {   // slew_times_to_63pct: against the analytic two-stage filter, within 10%
        const double thumb=measured_63(true,1),index=measured_63(false,.3);
        assert(std::abs(thumb-analytic_63(1,25,20))<.1*analytic_63(1,25,20));
        assert(std::abs(index-analytic_63(.3,10,20))<.1*analytic_63(.3,10,20));
    }
    {   // held_floor_0_6
        r::FingerTouchCurl c;In in;in.touch_available=true;const auto o=settle(c,0,in,true);
        assert(o.mapping==r::CurlMapping::touch && o.floor_active);
        for(float v:o.curl) assert(v>=.6f-1e-6f);
        const auto up=settle(c,0,touched(1,1,true),true);for(float v:up.curl) assert(near(v,1.f)); // the floor never lowers
        const auto free=settle(c,0,in,false);assert(!free.floor_active && free.curl[2]<1e-3f);
    }
    {   // legacy_when_touch_unavailable: finger_wants bit-exact
        r::FingerTouchCurl c;
        for(bool held:{false,true}) for(float t:{0.f,.2f,.5f,1.f}) for(float g:{0.f,.3f,.9f,1.f}) {
            In in;in.trigger=t;in.grip=g;in.trigger_touch=true;in.touch_available=false;
            const auto o=c.update(0,in,1/90.,held,P);const auto w=r::finger_wants(held,g,t);
            assert(o.mapping==r::CurlMapping::legacy && !o.floor_active && !o.touch_silent);
            for(unsigned f=0;f<5;++f) assert(std::memcmp(&o.curl[f],&w[f],sizeof(float))==0);
        }
    }
    {   // silent_touch_falls_back_to_legacy
        r::FingerTouchCurl c;In in;in.touch_available=true;
        r::FingerTouchOutput o;float x=0;
        for(int i=0;i<90*9;++i) {x=.5f+.4f*std::sin(i*.1f);in.trigger=x;in.grip=x;o=c.update(0,in,1/90.,false,P);}
        assert(o.mapping==r::CurlMapping::touch && !o.touch_silent); // 9 s of silence: not yet
        for(int i=0;i<90*2;++i) {x=.5f+.4f*std::sin(i*.1f);in.trigger=x;in.grip=x;o=c.update(0,in,1/90.,false,P);}
        const auto w=r::finger_wants(false,x,x);
        assert(o.mapping==r::CurlMapping::legacy && o.touch_silent);
        for(unsigned f=0;f<5;++f) assert(o.curl[f]==w[f]);
        in.thumb_touch=true;o=c.update(0,in,1/90.,false,P);assert(o.mapping==r::CurlMapping::touch && !o.touch_silent);
        // An idle controller (nothing pulled, nothing moving) never goes silent however long it sits.
        r::FingerTouchCurl idle;In z;z.touch_available=true;
        for(int i=0;i<90*60;++i) o=idle.update(0,z,1/90.,false,P);
        assert(o.mapping==r::CurlMapping::touch && !o.touch_silent);
        // Time spent in use but touched resets; a side that never was in use never accumulates.
        r::FingerTouchCurl d;In t;t.touch_available=true;t.trigger_touch=true;t.trigger=.8f;
        for(int i=0;i<90*20;++i) o=d.update(0,t,1/90.,false,P);
        assert(o.mapping==r::CurlMapping::touch);
    }
    {   // silent_timer_stall_capped: one long frame adds at most 0.25 s, so a stall can't trip touch_silent
        r::FingerTouchCurl c;In u;u.touch_available=true;u.trigger=.8f;u.grip=.8f; // in use, no touch
        auto o=c.update(0,u,100.,false,P);assert(o.mapping==r::CurlMapping::touch && !o.touch_silent);
        o=c.update(0,u,1e9,false,P);assert(!o.touch_silent);
        // real time still trips it: 10 s of normal frames
        for(int i=0;i<90*11;++i) o=c.update(0,u,1/90.,false,P);assert(o.touch_silent && o.mapping==r::CurlMapping::legacy);
        // forty stalls (each capped at .25 s) need 10 s of accumulated capped time, not one frame
        r::FingerTouchCurl d;for(int i=0;i<39;++i) o=d.update(0,u,5.,false,P);assert(!o.touch_silent);
        o=d.update(0,u,5.,false,P);assert(o.touch_silent);
    }
    {   // hitch_and_bad_dt_safe
        r::FingerTouchCurl c;const auto base=settle(c,0,touched(.5f,.5f,true));
        const auto big=c.update(0,touched(1,1,true),.5,false,P);
        for(float v:big.curl) assert(v>=0 && v<=1 && std::isfinite(v));
        r::FingerTouchCurl d;settle(d,0,touched(.2f,.2f),false,.5);
        const auto before=d.update(0,touched(.2f,.2f),1/90.,false,P);
        for(double bad:{0.,-1.,std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()}) {
            const auto o=d.update(0,touched(1,1,true),bad,false,P);
            for(unsigned f=0;f<5;++f) assert(o.curl[f]==before.curl[f]);
        }
        const auto after=d.update(0,touched(.2f,.2f),1/90.,false,P);for(unsigned f=0;f<5;++f) assert(near(after.curl[f],before.curl[f],.05f));
        (void)base;
        // A hitch toward a lower target never undershoots it.
        r::FingerTouchCurl e;settle(e,0,touched(1,1,true));const auto low=e.update(0,In{0,0,false,false,true},.5,false,P);
        for(float v:low.curl) assert(v>=0 && v<=1);
    }
    {   // outputs_always_in_0_1
        std::uint32_t seed=12345;const auto rnd=[&] {seed=seed*1664525u+1013904223u;return (seed>>8)/16777216.f;};
        const float nan=std::numeric_limits<float>::quiet_NaN(),inf=std::numeric_limits<float>::infinity();
        r::FingerTouchCurl c;
        for(int i=0;i<10000;++i) {
            In in;in.trigger=i%97==0?nan:i%89==0?inf:i%83==0?-inf:rnd()*2.4f-.7f;in.grip=i%101==0?nan:i%79==0?inf:rnd()*2.4f-.7f;
            in.trigger_touch=rnd()<.5f;in.thumb_touch=rnd()<.5f;in.touch_available=i%1000<900;
            const auto o=c.update(i&1,in,rnd()*.2,rnd()<.3f,P);
            for(float v:o.curl) assert(std::isfinite(v) && v>=0 && v<=1);
        }
        In n;n.trigger=nan;n.grip=inf;n.touch_available=true;n.trigger_touch=true;r::FingerTouchCurl d;
        const auto o=settle(d,0,n);assert(near(o.curl[1],.3f) && near(o.curl[2],0.f)); // non-finite trigger and grip read as 0
    }
    {   // params_parse
        const auto d=r::FingerTouchParams::parse("");assert(d && d->touch_rest==.3f && d->index_slew==10.f && d->thumb_slew==25.f && d->smooth_rate==20.f && d->held_floor==.6f);
        const auto v=r::FingerTouchParams::parse("# tuning\ntouch_rest=.4\nindex_slew = 12\r\nthumb_slew=30\nsmooth_rate=15\n\nheld_floor=.5\n");
        assert(v && v->touch_rest==.4f && v->index_slew==12.f && v->thumb_slew==30.f && v->smooth_rate==15.f && v->held_floor==.5f);
        for(const char* bad:{"touch_rest","touch_rest=","touch_rest=abc","touch_rest=.3x","bogus=1","touch_rest=nan","held_floor=inf","touch_rest=.4\nnope"}) assert(!r::FingerTouchParams::parse(bad));
        const auto cl=r::FingerTouchParams::parse("touch_rest=5\nindex_slew=0\nthumb_slew=1000\nsmooth_rate=-3\nheld_floor=-1");
        assert(cl && cl->touch_rest==1.f && cl->index_slew==1.f && cl->thumb_slew==100.f && cl->smooth_rate==1.f && cl->held_floor==0.f);
        const auto hi=r::FingerTouchParams::parse("held_floor=7");assert(hi && hi->held_floor==1.f);
    }
    {   // sides_independent
        r::FingerTouchCurl c;settle(c,0,touched(1,1,true));
        const auto other=c.update(1,touched(0,0),1/90.,false,P);assert(other.curl[0]<.5f && other.curl[2]<.1f);
        settle(c,1,touched(0,0));c.reset(0);
        const auto fresh=c.update(0,touched(0,0),1/90.,false,P);assert(fresh.curl[2]==0.f && fresh.curl[0]==0.f);
        assert(near(settle(c,1,touched(0,0)).curl[1],.3f));
        // A bad side never throws: update gives the legacy mapping verbatim, reset is a no-op.
        const auto bad=c.update(2,touched(.4f,.7f,true),1/90.,true,P);assert(bad.mapping==r::CurlMapping::legacy && bad.curl==r::finger_wants(true,.7f,.4f));
        const auto bad_free=c.update(7,touched(.4f,.7f),1/90.,false,P);assert(bad_free.mapping==r::CurlMapping::legacy && bad_free.curl==r::finger_wants(false,.7f,.4f));
        c.reset(2);c.reset(99);assert(near(settle(c,1,touched(0,0)).curl[1],.3f));
    }
    std::cout<<"finger_touch_curl_check ok\n";
}
