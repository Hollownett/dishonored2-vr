#include "../src/simulated_grab_fixture.hpp"
#include <cassert>
#include <limits>

int main() {
    d2vr::SimulatedGrabClock clock;
    assert(!clock.sample(0,true,false,true));
    assert(!clock.sample(10000,true,false,false));
    assert(!clock.sample(14000,true,true,false));
    assert(clock.sample(14500,true,true,true)==0);
    assert(clock.sample(15700,true,true,true)==1200);
    assert(!clock.sample(17000,true,true,false));
    assert(clock.sample(18300,true,true,true)==3800);
    assert(!clock.sample(19000,true,false,true)); // Reload/fit loss revokes the trial.
    assert(clock.sample(21000,true,true,true)==0);
    assert(clock.sample(1,true,true,true)==0); // Clock rollback cannot underflow.
    clock={};assert(clock.sample(10,false,false,true)==0);
    assert(clock.sample(1210,false,false,true)==1200);
    d2vr::SimulatedGrabFixtureChoice ordinary;
    assert(!ordinary.decided());
    ordinary.choose(.28);
    assert(ordinary.decided() && ordinary.ordinary());
    ordinary.choose(.976); // Lift/release cannot silently change the test path.
    assert(ordinary.ordinary() && ordinary.initial_gap()==.28);
    d2vr::SimulatedGrabFixtureChoice ranged;
    ranged.choose(.940556);
    ranged.choose(.1); // Later player movement does not erase ranged evidence.
    assert(ranged.decided() && !ranged.ordinary() && ranged.initial_gap()==.940556);
    d2vr::SimulatedGrabFixtureChoice boundary;
    boundary.choose(.3);assert(boundary.ordinary());
    boundary={};boundary.choose(.3000001);assert(!boundary.ordinary());
    boundary={};boundary.choose(-.1);assert(!boundary.ordinary() && boundary.initial_gap()==-1);
    boundary={};boundary.choose(std::numeric_limits<double>::quiet_NaN());assert(!boundary.ordinary());
    boundary={};boundary.choose(std::numeric_limits<double>::infinity());assert(!boundary.ordinary());
    boundary={};boundary.choose(0);assert(boundary.ordinary());
}
