#define D2VR_TESTING 1
#include "../src/physical_grab.hpp"
#include <cassert>
#include <iostream>
#include <limits>
#include "../src/grab_force_cap.hpp"
int main() {
    using namespace d2vr;
    {
        std::array<PhysicalGrab,2> grips;
        for(auto& grip:grips) {
            WorldTrackedPose pose{{},{{{1,0,0},{0,1,0},{0,0,1}}}};
            assert(!grip.sample(pose,0,true,1,1000));
            assert(grip.sample(pose,std::nextafter(.45f,0.f),true,1,1010));assert(!grip.pressed());
            assert(grip.sample(pose,.45f,true,1,1020));assert(grip.pressed() && grip.bind(pose));
            pose.position.x=.02;assert(grip.sample(pose,.35f,true,1,1030));assert(grip.pressed());
            const auto retained=grip.motor(pose,true);assert(retained && !retained->release);
            pose.position.x=.04;assert(grip.sample(pose,std::nextafter(.35f,0.f),true,1,1040));assert(!grip.pressed());
            const auto released=grip.motor(pose,true);assert(released && released->release && released->linear.x>0 && !grip.held());
            assert(grip.sample(pose,.45f,true,1,1050));assert(grip.bind(pose));
        }
    }
    WorldTrackedPose hand{{0,0,0},{{{1,0,0},{0,1,0},{0,0,1}}}},object=hand;object.position={.1,0,0};
    PhysicalGrab grab;
    assert(!grab.sample(hand,.9f,true,1,100));assert(!grab.bind(object));
    assert(grab.sample(hand,.9f,true,1,110));assert(!grab.pressed()); // Held grip cannot acquire on focus return.
    grab.sample(hand,0,true,1,120);grab.sample(hand,.9f,true,1,130);assert(grab.bind(object));
    assert(length(grab.target().position-object.position)<1e-9); // No pickup teleport.
    hand.position.x=.2;grab.sample(hand,.9f,true,1,180);const auto moving=grab.motor(object,true);
    assert(moving && moving->linear.x>0 && length(moving->linear)<=10.000001);
    hand.basis={{{0,1,0},{-1,0,0},{0,0,1}}};grab.sample(hand,.9f,true,1,190);
    const auto turned=grab.target();assert(length(turned.position-Vec3{.2,.1,0})<1e-9);
    assert(grab.motor(object,true)->angular.z>0);
    grab.sample(hand,0,true,1,200);const auto thrown=grab.motor(object,true);assert(thrown && thrown->release && thrown->linear.x>0 && length(thrown->linear)<=12.000001);
    assert(!grab.held());object=hand;grab.sample(hand,.9f,true,1,210);assert(grab.bind(object));
    assert(!grab.sample(hand,.9f,true,2,220));assert(grab.motor(object,false)->release);assert(!grab.pressed());
    grab.sample(hand,0,true,2,230);grab.sample(hand,.9f,true,2,240);object.position=hand.position+Vec3{.4,0,0};assert(!grab.bind(object));
    hand.position.x+=.15;grab.sample(hand,.9f,true,2,250);assert(grab.bind(object));
    hand.position.x+=.1;grab.sample(hand,.9f,true,2,260);
    object.position.x-=2;assert(grab.motor(object,true)->release); // Blocked carry releases.
    grab.sample(hand,0,true,2,270);grab.sample(hand,.9f,true,2,280);object=hand;assert(grab.bind(object));
    assert(!grab.sample(hand,.9f,true,2,600));assert(grab.motor(object,false)->linear.x==0);
    // Low submission cadence must not drop a continuously tracked physical hold.
    for(const unsigned interval:{11u,33u,100u,200u}) {
        PhysicalGrab rate;WorldTrackedPose stable{{},{{{1,0,0},{0,1,0},{0,0,1}}}};
        rate.sample(stable,0,true,1,1000);rate.sample(stable,0,true,1,1000+interval);
        rate.sample(stable,1,true,1,1000+2*interval);assert(rate.bind(stable));
        for(unsigned frame=3;frame<9;++frame) {
            assert(rate.sample(stable,1,true,1,1000+frame*interval));
            const auto held=rate.motor(stable,true);assert(held && !held->release);
        }
        rate.sample(stable,0,true,1,1000+9*interval);assert(rate.motor(stable,true)->release);
        rate.sample(stable,1,true,1,1000+10*interval);assert(rate.bind(stable));
        assert(!rate.sample(stable,1,false,1,1001+10*interval));assert(rate.motor(stable,false)->release);
    }
    {   // A press that waits ~400 ms for its owner (a corpse's native carry query) still presses when kept fresh, not otherwise.
        WorldTrackedPose still{{},{{{1,0,0},{0,1,0},{0,0,1}}}};
        PhysicalGrab waited;waited.sample(still,0,true,1,1000);waited.sample(still,0,true,1,1010);
        for(std::uint64_t t=1030;t<=1410;t+=20) waited.keep_fresh(still,1,t);
        assert(waited.sample(still,1,true,1,1420) && waited.pressed() && waited.bind(still));
        PhysicalGrab stale;stale.sample(still,0,true,1,1000);stale.sample(still,0,true,1,1010);
        stale.sample(still,1,true,1,1420);assert(!stale.pressed()); // the gap reset the latch with the grip held
    }
    // Reproduce fast movement followed by stationary 110-250ms samples;
    // slow valid samples must decay the existing throw estimate before release.
    for(const unsigned interval:{110u,200u,250u}) {
        PhysicalGrab rate;WorldTrackedPose stable{{},{{{1,0,0},{0,1,0},{0,0,1}}}};
        rate.sample(stable,0,true,1,1000);rate.sample(stable,0,true,1,1010);
        rate.sample(stable,1,true,1,1020);assert(rate.bind(stable));
        stable.position.x=.12;assert(rate.sample(stable,1,true,1,1040));
        assert(rate.velocity().x>2);
        auto slow_object=stable; // Object caught up to hand, isolating throw velocity.
        assert(rate.sample(stable,1,true,1,1040+interval));
        assert(length(rate.velocity())<.2);
        assert(rate.motor(slow_object,true) && !rate.motor(slow_object,true)->release);
        assert(rate.sample(stable,0,true,1,1040+2*interval));
        const auto release=rate.motor(slow_object,true);
        assert(release && release->release && length(release->linear)<.01);
        // Fresh rearm followed by a slow-interval teleport on the release frame
        // must release without throwing even if caller still says usable.
        rate.sample(stable,1,true,1,1050+2*interval);assert(rate.bind(slow_object));
        stable.position.x+=.6;
        assert(!rate.sample(stable,0,true,1,1050+3*interval));
        const auto teleport=rate.motor(slow_object,true);
        assert(teleport && teleport->release && length(teleport->linear)==0);
    }
    {
        PhysicalGrab duplicate;WorldTrackedPose stable{{},{{{1,0,0},{0,1,0},{0,0,1}}}};
        duplicate.sample(stable,0,true,1,1000);duplicate.sample(stable,0,true,1,1010);
        duplicate.sample(stable,1,true,1,1020);assert(duplicate.bind(stable));
        auto stationary_object=stable;stable.position.x=.6;
        assert(!duplicate.sample(stable,0,true,1,1020));
        const auto release=duplicate.motor(stationary_object,true);
        assert(release && release->release && length(release->linear)==0);
    }
    // Native pending-release lane samples the release publication before throwing.
    for(const unsigned interval:{110u,200u,250u}) {
        PhysicalGrab pending;WorldTrackedPose stable{{},{{{1,0,0},{0,1,0},{0,0,1}}}};
        pending.sample(stable,0,true,1,1000);pending.sample(stable,0,true,1,1010);
        pending.sample(stable,1,true,1,1020);assert(pending.bind(stable));
        stable.position.x=.12;pending.sample(stable,1,true,1,1040);assert(pending.velocity().x>2);
        const auto released=pending.release_sample(stable,0,true,1,1040+interval,true);
        assert(released.release && !pending.held() && length(released.linear)<.2);
        pending.sample(stable,1,true,1,1050+interval);assert(pending.bind(stable));stable.position.x+=.6;
        assert(length(pending.release_sample(stable,0,true,1,1050+2*interval,true).linear)==0);
    }
    const WorldTrackedPose identity{{},{{{1,0,0},{0,1,0},{0,0,1}}}};
    auto rotated=identity;
    for(unsigned axis=0;axis<3;++axis) {
        rotated=identity;for(unsigned i=0;i<3;++i) if(i!=axis) rotated.basis[i]=rotated.basis[i]*-1;
        const auto error=grab_rotation_error(identity,rotated);
        assert(finite(error) && std::abs(length(error)-std::acos(-1.))<1e-9);
        PhysicalGrab half_turn;half_turn.sample(identity,0,true,1,100);half_turn.sample(identity,0,true,1,110);
        half_turn.sample(identity,.9f,true,1,120);assert(half_turn.bind(identity));half_turn.sample(rotated,.9f,true,1,130);
        const auto motor=half_turn.motor(identity,true);assert(motor && !motor->release && std::abs(length(motor->angular)-5)<1e-9);
    }
    for(const double angle:{1e-7,.2,1.57,3.1415,-.2,-3.1415}) {
        rotated=identity;rotated.basis={{{std::cos(angle),std::sin(angle),0},{-std::sin(angle),std::cos(angle),0},{0,0,1}}};
        assert(length(grab_rotation_error(identity,rotated)-Vec3{0,0,angle})<1e-8);
        assert(length(grab_rotation_error(rotated,identity)+Vec3{0,0,angle})<1e-8);
    }
    assert(length(grab_rotation_error(identity,identity))==0);
    // Explicit forgiving acquisition preserves the native object's pose. The
    // ordinary default remains .3; callers cannot request arbitrary remote grabs.
    PhysicalGrab forgiving;
    forgiving.sample(identity,0,true,1,100);forgiving.sample(identity,0,true,1,110);
    forgiving.sample(identity,.9f,true,1,120);object=identity;object.position={.4,0,0};
    assert(!forgiving.bind(object));
    assert(!forgiving.bind(object,object.position,.6));
    assert(!forgiving.bind(object,object.position,std::numeric_limits<double>::quiet_NaN()));
    assert(forgiving.bind(object,object.position,.45));
    assert(length(forgiving.target().position-object.position)<1e-9);
    assert(length(forgiving.motor(object,true)->linear)<1e-9);
    // An occupied native hand (weapon/power/carried body) leaves the lane
    // unusable: the squeeze never acquires and a held prop is released.
    PhysicalGrab occupied;
    occupied.sample(identity,.9f,false,1,100);assert(!occupied.pressed() && !occupied.bind(identity));
    occupied.sample(identity,0,true,1,110);occupied.sample(identity,.9f,true,1,120);assert(occupied.bind(identity));
    occupied.sample(identity,.9f,true,1,130);
    const auto occupied_release=occupied.motor(identity,false);
    assert(occupied_release && occupied_release->release && !occupied.held() && length(occupied_release->linear)==0);
    // Two frames share each 16 ms tick quantum: the in-quantum motion must stay in the baseline, not vanish.
    {
        PhysicalGrab dup;auto pose=identity;
        for(unsigned i=0;i<20;++i) {pose.position.x+=.011;dup.sample(pose,0,true,1,1000+16*(i/2));}
        assert(std::abs(dup.velocity().x-1.375)<1.375*.15);
    }
    // A held object follows the newest pose even when two samples share a clock quantum.
    {
        PhysicalGrab latest;auto pose=identity;auto held=identity;
        latest.sample(pose,0,true,1,100);latest.sample(pose,0,true,1,116);latest.sample(pose,.9f,true,1,132);
        assert(latest.bind(held));
        pose.position.x+=.011;latest.sample(pose,.9f,true,1,148);
        const auto before=latest.target().position.x;
        pose.position.x+=.011;latest.sample(pose,.9f,true,1,148);
        assert(std::abs(latest.target().position.x-before-.011)<1e-4);
    }
    // One non-finite pose sample mid-carry must not poison the velocity baseline for later samples.
    {
        PhysicalGrab nan_hand;auto pose=identity;auto held=identity;
        nan_hand.sample(pose,0,true,1,100);nan_hand.sample(pose,0,true,1,116);nan_hand.sample(pose,.9f,true,1,132);
        assert(nan_hand.bind(held));
        pose.position.x+=.011;nan_hand.sample(pose,.9f,true,1,148);
        auto bad=pose;bad.position.x=std::numeric_limits<double>::quiet_NaN();
        nan_hand.sample(bad,.9f,true,1,164);
        for(unsigned i=0;i<60;++i) {pose.position.x+=.011;nan_hand.sample(pose,0,true,1,180+16*i);}
        assert(std::isfinite(nan_hand.velocity().x) && std::isfinite(nan_hand.velocity().y) && std::isfinite(nan_hand.velocity().z));
        assert(nan_hand.velocity().x>.3);
    }
    // ---- Task 7: capped corpse/prop motor ----
    {
        const WorldTrackedPose id{{},{{{1,0,0},{0,1,0},{0,0,1}}}};
        auto make=[&](PhysicalGrab& g,WorldTrackedPose body,Vec3 contact,std::uint64_t t0=1000) {
            g.sample(id,0,true,1,t0);g.sample(id,0,true,1,t0+10);g.sample(id,1,true,1,t0+20);
            return g.bind(body,contact);
        };
        auto in=[](double kg,double dt,const GrabTuning* t,bool corpse=true) {
            CorpseMotorInput i;i.corpse=corpse;i.object_kg=kg;i.inertia_kgm2=bone_inertia(kg,.2);i.dt=dt;i.tuning=t;i.velocity_known=true;return i;};
        // Hand samples reject jumps over .5 m, so far targets are reached in steps.
        auto reach=[&](PhysicalGrab& g,WorldTrackedPose& h,double x,std::uint64_t t) {
            while(h.position.x<x-1e-12) {h.position.x=std::min(x,h.position.x+.25);g.sample(h,1,true,1,t);}};
        GrabTuning one;one.strength_scale=1;GrabTuning dflt;
        auto body=id;body.position={.1,0,0};
        {   // corpse_bind_no_jump
            PhysicalGrab g;assert(make(g,body,{.1,0,0}));
            body.position={.1,0,0};
            auto r=g.corpse_motor(body,true,0,0,in(10,.01,&dflt));
            assert(r && !r->motor.release && length(r->motor.linear)==0 && length(r->motor.angular)==0 && !r->capped);
        }
        {   // corpse_motor_is_capped
            for(const double scale:{1.,3.}) {
                GrabTuning t;t.strength_scale=scale;
                PhysicalGrab g;auto b=id;assert(make(g,b,{}));
                auto hnd=id;hnd.position.x=.4;g.sample(hnd,1,true,1,1120);
                auto inp=in(10,.1,&t);inp.dt=.01;
                auto r=g.corpse_motor(b,true,0,0,inp);
                assert(r && r->capped && r->cap_fraction>0 && r->cap_fraction<1);
                assert(std::abs(r->motor.linear.x)<=300*scale*.01/12+1e-9 && r->motor.linear.x>0);
                assert(r->implied_force_n>0);
            }
        }
        {   // stale_dt_bounds_the_catch_up: the hand is 0.4 m ahead of the body; a normal frame closes at the full gain, a 0.5 s stale frame does not jump
            GrabTuning big;big.strength_scale=10;   // cap out of the way so the motor law shows
            auto probe=[&](double dt) {
                PhysicalGrab g;auto b=id;assert(make(g,b,{}));
                auto hnd=id;hnd.position.x=.4;g.sample(hnd,1,true,1,1120);
                auto r=g.corpse_motor(b,true,1620,0,in(.6,dt,&big));assert(r && !r->motor.release);return r->motor.linear.x;};   // now = sample + 0.5 s: the jump's own velocity has decayed away
            const double normal=probe(1./90),at_max=probe(big.dt_max),stale=probe(.5);
                        assert(normal>5 && std::abs(at_max-normal)<1e-9);   // dt <= dt_max: unchanged
            assert(stale<1.2 && stale>0 && stale<normal/4);      // 0.4 m x 18 / 15 = 0.48 m/s
            assert(probe(2*big.dt_max)<normal && probe(2*big.dt_max)>stale);   // monotone in the staleness
        }
        {   // stale_recovery: a hitch (raw dt > stale_dt_s) starts a RAMPED cap on the error closure (catch_up_mps + ramp * t); no frame jumps
            GrabTuning big;big.strength_scale=10;
            auto start=[&](PhysicalGrab& g,double hand_x,double angle=0) {
                auto b=id;assert(make(g,b,{}));auto hnd=id;
                if(angle!=0) hnd.basis={{{std::cos(angle),std::sin(angle),0},{-std::sin(angle),std::cos(angle),0},{0,0,1}}};
                double x=0;do {x=std::min(hand_x,x+.25);hnd.position.x=x;g.sample(hnd,1,true,1,1120);} while(x<hand_x-1e-12);};
            auto motor=[&](PhysicalGrab& g,double body_x,double dt) {auto b=id;b.position.x=body_x;auto r=g.corpse_motor(b,true,1620,0,in(.6,dt,&big));assert(r && !r->motor.release);return *r;};
            auto step=[&](PhysicalGrab& g,double body_x,double dt) {return motor(g,body_x,dt).motor.linear.x;};
            const double f=1./90;
            {   PhysicalGrab g;start(g,.4);   // the stale frame itself and the normal frames after it follow cap(t) = 1 + 4 t (0.4 m x 18 = 7.2 m/s wanted)
                assert(step(g,0,.5)<=big.catch_up_mps+1e-3 && g.recovery_s()>0);
                double t=0;
                for(int i=0;i<30;++i) {t+=f;const double v=step(g,0,f);assert(std::abs(v-(big.catch_up_mps+big.catch_up_ramp_mps2*t))<2e-3);}
            }
            {   const double ramp=big.catch_up_ramp_mps2;big.catch_up_ramp_mps2=60;   // a steeper ramp so it reaches 10 m/s inside the 0.25 s lag-release window
                PhysicalGrab g;start(g,.56);   // wants 10.08 m/s (limited to 10); every frame step is bounded by the ramp, including the last
                step(g,0,.5);double prev=step(g,0,f),worst=0;int frames=0;   // (the stale frame itself is scaled by dt_max/dt; the steps are measured from the first normal frame)
                while(g.recovery_s()>0 && frames<400) {const double v=step(g,0,f);worst=std::max(worst,std::abs(v-prev));prev=v;++frames;}
                assert(g.recovery_s()==0 && frames>5 && frames<20);
                assert(worst<=big.catch_up_ramp_mps2*f+2e-3);                  // no step-change at the end of recovery
                const double after=step(g,0,f);assert(std::abs(after-prev)<=big.catch_up_ramp_mps2*f+2e-3 && std::abs(after-10)<2e-3);
                big.catch_up_ramp_mps2=ramp;
            }
            {   PhysicalGrab g;start(g,.4);step(g,0,.5);   // the lag closes to under 5 cm: recovery ends on THIS frame, and a fresh 0.4 m lag then closes at the full gain
                motor(g,.38,f);assert(g.recovery_s()==0);   // lag 0.02 m
                assert(std::abs(step(g,0,f)-7.2)<2e-3);
            }
            {   PhysicalGrab g;start(g,.4);step(g,0,.5);assert(g.recovery_s()>0);   // release resets it; the next grab starts in normal play (bind also resets; reached through the D2VR_TESTING seam)
                g.sample(id,0,true,1,1130);auto b=id;auto r=g.corpse_motor(b,true,1620,0,in(.6,f,&big));assert(r && r->motor.release && g.recovery_s()==0);
                g.set_recovery_for_tests(.7);assert(g.recovery_s()==.7);   // the seam: a stale recovery state left on an idle grab
                auto bb=id;assert(make(g,bb,{},3000) && g.recovery_s()==0);   // bind() itself resets it
            }
            {   PhysicalGrab g;start(g,.4);   // a run of consecutive stale frames (dt 0.15) keeps the ramp running (never re-armed): it reaches the normal cap and ends
                int ended=-1;for(int i=0;i<25 && ended<0;++i) {step(g,0,.15);if(g.recovery_s()==0) ended=i;}
                assert(ended>=10 && ended<=16);   // cap(t) = 1 + 4 t reaches 10 at t = 2.25 s = 15 frames
            }
            {   PhysicalGrab g;start(g,.04,.08);   // no step in either closure across the exit: linear is inside the cap at once, angular (0.96 rad/s) ramps up to its normal value, then recovery ends
                step(g,0,.5);auto first=motor(g,0,f);double pv=first.motor.linear.x,pw=length(first.motor.angular),worstv=0,worstw=0;int n=0;
                while(g.recovery_s()>0 && n<100) {const auto r=motor(g,0,f);worstv=std::max(worstv,std::abs(r.motor.linear.x-pv));worstw=std::max(worstw,std::abs(length(r.motor.angular)-pw));
                    pv=r.motor.linear.x;pw=length(r.motor.angular);++n;}
                assert(g.recovery_s()==0 && n>10 && n<40 && worstv<1e-3 && worstw<=big.catch_up_ramp_mps2*f*.5+1e-3);
                const auto after=motor(g,0,f);assert(std::abs(after.motor.linear.x-pv)<1e-3 && std::abs(length(after.motor.angular)-pw)<=big.catch_up_ramp_mps2*f*.5+1e-3 && std::abs(length(after.motor.angular)-.96)<2e-3);
            }
            {   PhysicalGrab a,b2;start(a,.4);start(b2,.4);   // no stale frame: the old law (error*18 + feed, limited to 10), never in recovery
                for(int i=0;i<10;++i) assert(step(a,0,f)==step(b2,0,f) && a.recovery_s()==0);
                assert(std::abs(step(a,0,f)-7.2)<2e-3);
            }
            {   PhysicalGrab g;start(g,.4);   // sustained 25 fps (dt 0.04 > dt_max, < stale_dt_s) is not a hitch: never recovers, closure = old law x dt_max/dt
                for(int i=0;i<30;++i) {const double v=step(g,0,.04);assert(g.recovery_s()==0 && std::abs(v-7.2*big.dt_max/.04)<2e-3);}
            }
            {   PhysicalGrab g;start(g,0,1.0);   // angular: 1 rad of spin lag wants 12 rad/s (limited to 5); in recovery it ramps at half the linear cap
                const double w0=length(motor(g,0,.5).motor.angular);assert(w0<=big.catch_up_mps*.5+1e-3 && w0>0);
                double t=0;for(int i=0;i<10;++i) {t+=f;const double w=length(motor(g,0,f).motor.angular);assert(std::abs(w-(big.catch_up_mps+big.catch_up_ramp_mps2*t)*.5)<2e-3);}
                PhysicalGrab h;start(h,0,1.0);assert(length(motor(h,0,f).motor.angular)>4.9);   // normal play: the full 5 rad/s
            }
        }
        {   // release_writes_no_velocity
            PhysicalGrab g;auto b=id;assert(make(g,b,{}));
            auto hnd=id;hnd.position.x=.05;g.sample(hnd,1,true,1,1030);g.sample(hnd,0,true,1,1040);
            auto r=g.corpse_motor(b,true,0,0,in(10,.01,&dflt));
            assert(r && r->motor.release && length(r->motor.linear)==0 && length(r->motor.angular)==0 && r->reason==ReleaseReason::open_fist);
            assert(g.release_reason()==ReleaseReason::open_fist && !g.held());
        }
        {   // A-11: state change and open fist on one tick -> state_changed
            PhysicalGrab g;auto b=id;assert(make(g,b,{}));
            g.sample(id,0,true,1,1030);
            auto inp=in(10,.01,&dflt);inp.state_changed=true;
            auto r=g.corpse_motor(b,true,0,0,inp);
            assert(r && r->motor.release && r->reason==ReleaseReason::state_changed);
        }
        {   // lag_release_after_quarter_second, and the stuck body (no velocity) variant
            for(const bool known:{true,false}) {
                PhysicalGrab g;auto b=id;assert(make(g,b,{}));
                auto hnd=id;std::uint64_t t=1030;reach(g,hnd,.6,t);
                auto inp=in(10,.01,&dflt);inp.velocity_known=known;
                std::optional<CorpseMotor> r;
                for(unsigned i=0;i<24;++i) {r=g.corpse_motor(b,true,0,0,inp);assert(r && !r->motor.release && r->lag_m>.59);}
                for(unsigned i=0;i<2 && !(r && r->motor.release);++i) r=g.corpse_motor(b,true,0,0,inp);
                assert(r && r->motor.release && r->reason==ReleaseReason::lag && length(r->motor.linear)==0 && length(r->motor.angular)==0);
            }
            PhysicalGrab g;auto b=id;assert(make(g,b,{}));
            auto hnd=id;hnd.position.x=.49;g.sample(hnd,1,true,1,1030);
            for(unsigned i=0;i<500;++i) {auto r=g.corpse_motor(b,true,0,0,in(10,.01,&dflt));assert(r && !r->motor.release);}
            // 0.6 m excursion that clears resets the timer
            PhysicalGrab h;auto b2=id;assert(make(h,b2,{}));
            for(unsigned round=0;round<2;++round) {
                auto far=id;reach(h,far,.6,1030+round*100);
                for(unsigned i=0;i<20;++i) assert(!h.corpse_motor(b2,true,0,0,in(10,.01,&dflt))->motor.release);
                {auto back=far;back.position.x=.3;h.sample(back,1,true,1,1040+round*100);}
                h.sample(id,1,true,1,1050+round*100);assert(!h.corpse_motor(b2,true,0,0,in(10,.01,&dflt))->motor.release);
            }
        }
        {   // cooldown_blocks_regrab_flapping_grip
            PhysicalGrab g;auto b=id;assert(make(g,b,{}));
            auto hnd=id;reach(g,hnd,.9,1030);
            std::optional<CorpseMotor> r;
            for(unsigned i=0;i<30 && !(r && r->motor.release);++i) r=g.corpse_motor(b,true,0,0,in(10,.01,&dflt));
            assert(r && r->reason==ReleaseReason::lag);
            const std::uint64_t rel=1030;
            g.sample(hnd,0,true,1,rel+10);
            for(std::uint64_t t=rel+20;t<rel+300;t+=20) {
                g.sample(hnd,0,true,1,t);g.sample(hnd,1,true,1,t+5);
                assert(!g.bind(b,hnd.position,.55) );
            }
            g.sample(hnd,0,true,1,rel+290);g.sample(hnd,1,true,1,rel+300);
            auto near=hnd;assert(g.bind(near));
            // open fist release is not delayed
            auto b3=id;PhysicalGrab o;assert(make(o,b3,{}));
            o.sample(id,0,true,1,1030);o.corpse_motor(b3,true,0,0,in(10,.01,&dflt));
            o.sample(id,1,true,1,1040);assert(o.bind(b3,{}));
        }
        {   // two_hands_split_mass: effective mass 2 + .5*20 = 12
            PhysicalGrab g;auto b=id;assert(make(g,b,{}));
            auto hnd=id;hnd.position.x=.4;g.sample(hnd,1,true,1,1120);
            auto inp=in(20,.01,&one);inp.share=.5;
            auto r=g.corpse_motor(b,true,0,0,inp);
            assert(r && std::abs(r->motor.linear.x-300*.01/12)<1e-9);
        }
        {   // anchor_tracks_object_rotation
            auto b=id;b.position={1,2,3};
            PhysicalGrab h;auto hnd=id;hnd.position={1,2,3};h.sample(hnd,0,true,1,1000);h.sample(hnd,0,true,1,1010);h.sample(hnd,1,true,1,1020);
            assert(h.bind(b,{1.1,2,3}));
            assert(length(h.anchor(b)-Vec3{1.1,2,3})<1e-9);
            auto rot=b;rot.basis={{{0,1,0},{-1,0,0},{0,0,1}}};
            assert(length(h.anchor(rot)-Vec3{1,2.1,3})<1e-9);
        }
        {   // A-12 post-release blend: anchor stays for .25 s
            PhysicalGrab g;auto b=id;assert(make(g,b,{.05,0,0}));
            g.sample(id,0,true,1,1030);
            auto r=g.corpse_motor(b,true,0,0,in(10,.01,&dflt));assert(r && r->motor.release);
            auto a=g.released_anchor(1030);assert(a && length(a->position-Vec3{.05,0,0})<1e-9 && std::abs(a->weight-1)<1e-9);
            a=g.released_anchor(1030+125);assert(a && std::abs(a->weight-.5)<1e-9);
            assert(!g.released_anchor(1030+250));
        }
        {   // prop_mode_keeps_old_release
            PhysicalGrab g;auto b=id;assert(make(g,b,{}));
            auto hnd=id;reach(g,hnd,1.6,1030);
            auto r=g.corpse_motor(b,true,0,0,in(5,.01,&dflt,false));assert(r && r->motor.release);
            PhysicalGrab p;auto b2=id;assert(make(p,b2,{}));
            auto hnd2=id;reach(p,hnd2,.8,1030);
            for(unsigned i=0;i<500;++i) {r=p.corpse_motor(b2,true,0,0,in(5,.01,&dflt,false));assert(r && !r->motor.release);}
            assert(r->capped && length(r->motor.linear)<10);
            auto fast=id;PhysicalGrab q;auto b3=id;assert(make(q,b3,{}));
            fast.position.x=.1;q.sample(fast,1,true,1,1050);q.sample(fast,0,true,1,1060);
            r=q.corpse_motor(b3,true,0,0,in(5,.01,&dflt,false));
            assert(r && r->motor.release && r->motor.linear.x>0);
        }
    }
    // ---- Task 7 fix round 1 ----
    {
        const WorldTrackedPose id{{},{{{1,0,0},{0,1,0},{0,0,1}}}};
        const double nan=std::numeric_limits<double>::quiet_NaN();
        GrabTuning dflt;
        auto in=[&](double dt=.01,bool corpse=true) {CorpseMotorInput i;i.corpse=corpse;i.object_kg=10;i.inertia_kgm2=bone_inertia(10,.2);i.dt=dt;i.tuning=&dflt;i.velocity_known=true;return i;};
        auto make=[&](PhysicalGrab& g,const WorldTrackedPose& body,Vec3 contact,const WorldTrackedPose& h=WorldTrackedPose{{},{{{1,0,0},{0,1,0},{0,0,1}}}}) {
            g.sample(h,0,true,1,1000);g.sample(h,0,true,1,1010);g.sample(h,1,true,1,1020);return g.bind(body,contact);};
        auto reach=[&](PhysicalGrab& g,WorldTrackedPose& h,double x,std::uint64_t t) {
            while(h.position.x<x-1e-12) {h.position.x=std::min(x,h.position.x+.25);g.sample(h,1,true,1,t);}};
        auto lag_release=[&](PhysicalGrab& g,WorldTrackedPose& b,double dt=.01) {
            std::optional<CorpseMotor> r;for(unsigned i=0;i<60 && !(r && r->motor.release);++i) r=g.corpse_motor(b,true,0,0,in(dt));return r;};
        {   // 1: release(false) on an idle lane keeps cooldown, reason, anchor; a fresh lane has no anchor
            PhysicalGrab fresh;fresh.release(false);assert(!fresh.released_anchor(0) && fresh.release_reason()==ReleaseReason::none);
            PhysicalGrab g;auto b=id;assert(make(g,b,{}));auto h=id;reach(g,h,.9,1030);
            auto r=lag_release(g,b);assert(r && r->reason==ReleaseReason::lag);
            const auto before=g.released_anchor(1030);assert(before);
            g.release(false);g.release_sample(h,0,true,1,1040,false);
            assert(g.release_reason()==ReleaseReason::lag && g.released_anchor(1030) && length(g.released_anchor(1030)->position-before->position)<1e-12);
            g.sample(h,0,true,1,1100);g.sample(h,1,true,1,1110);auto near=h;assert(!g.bind(near));
        }
        {   // 2: non-finite velocity, spin, share -> fault release, no finite-looking write
            for(unsigned k=0;k<3;++k) {
                PhysicalGrab g;auto b=id;assert(make(g,b,{}));auto h=id;h.position.x=.3;g.sample(h,1,true,1,1030);
                auto inp=in();if(k==0) inp.body_velocity.x=nan;if(k==1) inp.body_spin.y=nan;if(k==2) inp.share=nan;
                auto r=g.corpse_motor(b,true,0,0,inp);
                assert(r && r->motor.release && r->reason==ReleaseReason::fault && length(r->motor.linear)==0 && length(r->motor.angular)==0 && !g.held());
            }
        }
        {   // 3: dt 0 / NaN write nothing and never fire; a .3 s hitch clamps to dt_max and does not release on the first lagging frame
            for(const double dt:{0.,nan,-1.}) {
                PhysicalGrab g;auto b=id;assert(make(g,b,{}));auto h=id;reach(g,h,.6,1030);
                for(unsigned i=0;i<50;++i) {auto r=g.corpse_motor(b,true,0,0,in(dt));assert(r && !r->write && !r->motor.release && g.held());}
            }
            PhysicalGrab g;auto b=id;assert(make(g,b,{}));auto h=id;reach(g,h,.6,1030);
            auto r=g.corpse_motor(b,true,0,0,in(.3));assert(r && r->write && !r->motor.release);
        }
        {   // 4: stale-tick sequence keeps the cooldown
            PhysicalGrab g;auto b=id;assert(make(g,b,{}));auto h=id;reach(g,h,.9,1030);
            auto r=lag_release(g,b);assert(r && r->reason==ReleaseReason::lag);
            g.sample(h,0,true,1,1020);g.sample(h,0,true,1,1026);g.sample(h,1,true,1,1027);
            auto near=h;assert(!g.bind(near));
        }
        {   // 5: release reasons
            PhysicalGrab g;auto b=id;assert(make(g,b,{}));auto h=id;reach(g,h,2.,1030);   // 1.5 m blocked carry (prop)
            h.position.x=0;
            PhysicalGrab p;assert(make(p,b,{}));auto far=id;reach(p,far,1.6,1030);
            auto m=p.motor(b,true);assert(m && m->release && p.release_reason()==ReleaseReason::lag);
            // immediate regrab after a PROP 1.5 m release is allowed
            p.sample(far,0,true,1,1100);p.sample(far,1,true,1,1110);assert(p.bind(b,far.position));
            PhysicalGrab o;assert(make(o,b,{}));o.sample(id,0,true,1,1030);
            m=o.motor(b,true);assert(m && m->release && o.release_reason()==ReleaseReason::open_fist);
            PhysicalGrab hd;assert(make(hd,b,{}));hd.release_sample(id,1,true,1,1030,true);assert(hd.release_reason()==ReleaseReason::lifecycle);
            PhysicalGrab c;assert(make(c,b,{}));auto cf=id;reach(c,cf,1.6,1030);
            auto cr=lag_release(c,b);assert(cr && cr->reason==ReleaseReason::lag);
            // prop throw velocity equals the hand velocity
            PhysicalGrab t;assert(make(t,b,{}));auto th=id;th.position.x=.3;t.sample(th,1,true,1,1040);t.sample(th,0,true,1,1050);
            auto tv=t.corpse_motor(b,true,0,0,in(.01,false));assert(tv && tv->motor.release && length(tv->motor.linear-t.velocity())<1e-12 && t.velocity().x>0);
        }
        {   // 6: NaN object pose leaves the released anchor finite
            PhysicalGrab g;auto b=id;assert(make(g,b,{.05,0,0}));
            auto bad=b;bad.position.x=nan;auto m=g.motor(bad,true);assert(m && m->release);
            auto a=g.released_anchor(1020);assert(a && finite(a->position));
        }
        {   // 7: offset contact + rotated hand/object: lag_m equals the anchor distance; lag release anchor is the object anchor
            WorldTrackedPose rot=id;rot.basis={{{0,1,0},{-1,0,0},{0,0,1}}};
            auto b=rot;b.position={0,0,0};PhysicalGrab g;
            assert(make(g,b,{0,.1,0},rot));
            auto h=rot;std::uint64_t t=1030;
            while(h.position.y<.6-1e-12) {h.position.y=std::min(.6,h.position.y+.25);g.sample(h,1,true,1,t);}
            auto r=g.corpse_motor(b,true,0,0,in());
            assert(r && std::abs(r->lag_m-length(g.anchor(g.target())-g.anchor(b)))<1e-9 && std::abs(r->lag_m-.6)<1e-9);
            r=lag_release(g,b);assert(r && r->reason==ReleaseReason::lag);
            auto a=g.released_anchor(t);assert(a && length(a->position-g.anchor(b))<1e-9);
            // cap axis: hand rotated 90 degrees about z has forward along +y; the axial cap applies to y, the lateral cap to x
            GrabTuning one;one.strength_scale=1;
            PhysicalGrab c;auto cb=rot;assert(make(c,cb,{},rot));
            auto ch=rot;ch.position={.3,.3,0};c.sample(ch,1,true,1,1030);
            auto inp=in();inp.tuning=&one;auto cr=c.corpse_motor(cb,true,0,0,inp);
            assert(cr && cr->capped && std::abs(cr->motor.linear.y)<=300*.01/12+1e-9 && std::abs(cr->motor.linear.x)<=90*.01/12+1e-9);
            assert(std::abs(cr->motor.linear.y)>std::abs(cr->motor.linear.x)*1.5);
            // bind with a real offset contact: no jump
            PhysicalGrab n;auto nb=id;nb.position={.1,0,0};assert(make(n,nb,{.2,0,0}));
            auto nr=n.corpse_motor(nb,true,0,0,in());assert(nr && length(nr->motor.linear)==0 && length(nr->motor.angular)==0 && !nr->capped && nr->cap_fraction==0);
        }
    }
    std::cout<<"PASS: grip hysteresis/rearm, relative pose, bounded dynamic motor/throw, focus/epoch/stale/blocked release, occupied-hand refusal and release\n";
}
