#include "../src/turn_control.hpp"
#include <cassert>
#include <iostream>
#include <limits>

static bool close_to(double a,double b,double e) {return std::abs(a-b)<=e;}

int main() {
    using T=d2vr::TurnControl;
    constexpr double pi=3.14159265358979323846;
    T control;control.configure({T::Mode::snap,90,90});
    assert(control.publish(1,true,true,true,true,0,1));
    assert(!control.consume(.01,true,1,1,1)); // Held input on activation.
    double heading{};
    for(unsigned i=0;i<4;++i) {
        auto now=10+30*i;
        control.publish(0,true,true,true,true,now,1);control.publish(1,true,true,true,true,now+1,1);
        const auto step=control.consume(.01,true,now+2,1,2+i*2);assert(step);
        heading=std::remainder(heading+step->radians,2*pi);
        assert(!control.consume(.01,true,now+3,1,2+i*2)); // Duplicate simulation/eye replay.
        control.publish(1,true,true,true,true,now+4,1);
        assert(!control.consume(.01,true,now+5,1,3+i*2)); // Held stick.
    }
    assert(std::abs(heading)<1e-12);
    control.configure({T::Mode::snap,45,90});
    control.publish(0,true,true,true,true,200,2);control.publish(1,true,true,true,true,201,2);
    assert(!control.publish(1,true,false,true,true,202,2)); // Menu owns stick.
    assert(!control.consume(.01,true,203,2,1));
    control.publish(1,true,true,true,true,204,2);assert(!control.consume(.01,true,205,2,2));
    control.publish(0,true,true,true,true,210,2);control.publish(1,true,true,true,true,211,2);
    assert(!control.consume(.01,true,212,3,3)); // Recenter cancels.
    control.publish(0,true,true,true,true,220,3);control.publish(1,true,true,true,true,221,3);
    assert(!control.consume(.01,true,472,3,4)); // Stale input lease.
    assert(!control.publish(1,true,true,false,true,480,3)); // Weapon wheel owns stick.
    assert(control.publish(1,true,true,true,false,481,3)); // No native-turn fallback while writer is unavailable.
    assert(!control.consume(.01,true,482,3,5));
    control.publish(0,true,true,true,true,490,3);control.publish(1,true,true,true,true,491,3);
    assert(!control.consume(.2,true,492,3,6)); // Long native stall cannot cause a large jump.

    for(unsigned hz:{20u,45u,90u}) {
        control.configure({T::Mode::smooth,45,90,0}); // ramp_zero_is_old_behaviour: full speed from the first update
        control.publish(0,true,true,true,true,1000,4);
        double total{};
        for(unsigned i=0;i<hz;++i) {
            const auto now=1000+1000*i/hz;
            control.publish(1,true,true,true,true,now,4);
            auto step=control.consume(1./hz,true,now+1,4,i+1);assert(step);total+=step->radians;
            assert(!control.consume(1./hz,true,now+2,4,i+1));
        }
        assert(std::abs(total-pi/2)<1e-12); // Same speed across native frame rates.
    }
    {   // ramp: 0 -> 1 over ramp_s once the stick leaves the deadzone, restarting after it returns
        const auto run=[&](T& c,double axis,std::uint64_t& now,std::uint64_t& update,double dt) {
            now+=10;c.publish(axis,true,true,true,true,now,7);
            const auto step=c.consume(dt,true,now+1,7,++update);return step?step->radians:0.;
        };
        const double full=pi/2*.01; // 90 deg/s over a 10 ms update
        T ramped;ramped.configure({T::Mode::smooth,45,90});
        std::uint64_t now=5000,update=0;
        assert(run(ramped,0,now,update,.01)==0);
        const auto first=run(ramped,1,now,update,.01);
        assert(first>0 && first<full*.1); // ramp_starts_at_zero: the first 10 ms is a sliver of full speed
        double total=first,last=first;
        for(int i=1;i<15;++i) {last=run(ramped,1,now,update,.01);assert(last>0 && last<=full+1e-12);total+=last;}
        assert(close_to(last,full,full*.07) && last<full); // 140..150 ms: almost, not yet full
        assert(close_to(total,pi/2*(.15/2),1e-9)); // area of the ramp: ramp_s/2 seconds at full speed
        assert(close_to(run(ramped,1,now,update,.01),full,1e-12)); // ramp_reaches_full_at_150ms
        assert(close_to(run(ramped,1,now,update,.01),full,1e-12));
        assert(run(ramped,0,now,update,.01)==0); // ramp_resets_on_centre
        const auto again=run(ramped,1,now,update,.01);
        assert(again>0 && close_to(again,first,1e-12));
        T off;off.configure({T::Mode::smooth,45,90,0});now=6000;update=0;
        run(off,0,now,update,.01);
        assert(close_to(run(off,1,now,update,.01),full,1e-12)); // ramp_zero_is_old_behaviour
        T half;half.configure({T::Mode::smooth,45,90,.15});now=7000;update=0;
        run(half,0,now,update,.01);
        double slow{},fast{};
        for(int i=0;i<3;++i) slow+=run(half,1,now,update,.01);
        T once;once.configure({T::Mode::smooth,45,90,.15});now=8000;update=0;
        run(once,0,now,update,.01);
        fast=run(once,1,now,update,.03); // same elapsed time in one coarse update gives the same angle
        assert(close_to(slow,fast,1e-12));
        bool rejected=false;try {T bad;bad.configure({T::Mode::smooth,45,90,-1});} catch(const std::invalid_argument&) {rejected=true;}
        assert(rejected);
    }
    control.configure({T::Mode::smooth,45,90});
    control.publish(1,true,true,true,true,2000,5);assert(!control.consume(.01,true,2001,5,1));
    control.publish(0,true,true,true,true,2002,5);control.publish(-1,true,true,true,true,2003,5);
    auto left=control.consume(.01,true,2004,5,2);assert(left && left->radians<0);
    assert(!control.consume(std::numeric_limits<double>::quiet_NaN(),true,2005,5,3));
    control.configure({T::Mode::native,45,90});assert(!control.publish(1,true,true,true,true,2100,5));
    assert(d2vr::quest_turn_axis_to_native(1)<0); // Right must point towards negative game-left.
    using F=d2vr::TurnFeedback;
    F feedback;const F::Owner owner{1,2,3,4};
    auto pending=[&] {feedback.begin({owner,{-.785398163,10,1,1},1.42900419,.643606007,.025,100});};
    pending();assert(feedback.poll(owner,.643606007,1.42900419,true,101)==F::State::waiting);
    assert(feedback.poll(owner,.643606007,.643606007,true,125)==F::State::acknowledged);
    assert(!feedback.pending()); // The observed first native snap is accepted after transform publication.
    pending();assert(feedback.poll(owner,.643606007,1.42900419,true,601)==F::State::failed);
    pending();assert(feedback.poll({1,2,3,9},.643606007,1.42900419,true,125)==F::State::cancelled);
    pending();assert(feedback.poll(owner,.743606007,.643606007,true,125)==F::State::failed);
    pending();assert(feedback.poll(owner,.743606007,.643606007,false,125)==F::State::cancelled);
    feedback.begin({owner,{.01,10,1,1},3.14,-3.13,.01,100});
    assert(feedback.poll(owner,-3.13,-3.13+2*pi,true,125)==F::State::acknowledged);
    std::cout<<"PASS: native-update snap consumption, four full-circle turns, smooth rate at 20/45/90 Hz, menu/wheel ownership, activation/recenter/stale/stall cancellation; no game writes\n";
    std::cout<<"PASS: recorded deferred entity update, bounded acknowledgement, overwritten heading, ownership/animation takeover and angle-wrap feedback\n";
}
