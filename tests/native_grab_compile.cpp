#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../src/native_grab.hpp"
#include <cassert>
#include <iostream>
int main() {
    // An occupied lane (native weapon/power/carried body) must hand the raw
    // squeeze back without touching the native read ABI, which is not installed
    // in this process: any read here would fault.
    d2vr::NativeGrab grab;d2vr::NativePlayerCamera owner{};
    d2vr::WorldTrackedPose hand{{0,0,0},{{{1,0,0},{0,1,0},{0,0,1}}}};
    grab.publish(0,owner,hand,.9f,1,100,true,true);
    assert(grab.filter_native_grip(0,.9f,1,100,true,true,false)==.9f);
    assert(grab.filter_native_grip(0,.4f,1,101,true,true,false)==.4f);
    assert(grab.filter_native_grip(0,.9f,1,102,false,false,true)==.9f); // Raw even unfocused or while Use is held.
    assert(!grab.holding(0));
    grab.publish(1,owner,hand,.7f,1,100,true,false); // Defaulted occupied=false still compiles.
    assert(!grab.holding(1));
    std::cout<<"PASS: native_grab.hpp compiles under /W4 /WX and an occupied lane passes the raw native squeeze without acquiring\n";
}
