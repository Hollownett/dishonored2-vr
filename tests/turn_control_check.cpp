#include "../src/turn_control.hpp"
#include <cassert>
#include <iostream>
#include <limits>

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
        control.configure({T::Mode::smooth,45,90});
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
