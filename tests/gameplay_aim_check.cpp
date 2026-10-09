#include "../src/gameplay_aim.hpp"
#include <cassert>
#include <iostream>
int main() {
    using namespace d2vr;
    const GameplayAim native{{1,2,3},{1,0,0,0,1,0,0,0,1}};
    const AimOwner owner{0x10000,0x20000,7};
    TrackedGameplayAim tracked{owner,5,100,{{1,2.5,3},{{{0,1,0},{-1,0,0},{0,0,1}}}},true};
    auto aim=controller_gameplay_aim(native,owner,tracked,5,110,true);assert(aim);
    assert(aim->origin[1]==2.5 && aim->axis[1]==1 && native.axis[0]==1);
    assert(!controller_gameplay_aim(native,owner,tracked,5,110,false));
    assert(!controller_gameplay_aim(native,owner,tracked,6,110,true));
    assert(!controller_gameplay_aim(native,owner,tracked,5,99,true));
    assert(!controller_gameplay_aim(native,owner,tracked,5,351,true));
    auto wrong=owner;++wrong.generation;
    assert(!controller_gameplay_aim(native,wrong,tracked,5,110,true));
    tracked.active=false;assert(!controller_gameplay_aim(native,owner,tracked,5,110,true));tracked.active=true;
    tracked.left.position={10,2,3};assert(!controller_gameplay_aim(native,owner,tracked,5,110,true));
    tracked.left.position={1,2,3};tracked.left.basis[2]={0,0,-1};
    assert(!controller_gameplay_aim(native,owner,tracked,5,110,true));
    tracked.left.basis[2]={0,0,std::numeric_limits<double>::quiet_NaN()};
    assert(!controller_gameplay_aim(native,owner,tracked,5,110,true));
    auto invalid=native;invalid.axis[0]=0;
    assert(!controller_gameplay_aim(invalid,owner,tracked,5,110,true));
    std::cout<<"PASS: native aim pose layout, independent left controller, stale/epoch/owner/state/reach/frame guards\n";
}
