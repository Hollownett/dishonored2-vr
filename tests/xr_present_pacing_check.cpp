#include "../src/xr_present_pacing.hpp"
#include <cassert>
#include <iostream>
int main() {
    using d2vr::xr_desktop_sync_interval;
    assert(xr_desktop_sync_interval(1,0,true,true,true,true)==0);
    assert(xr_desktop_sync_interval(2,0,true,true,true,true)==0);
    assert(xr_desktop_sync_interval(1,1,true,true,true,true)==1);
    assert(xr_desktop_sync_interval(1,0,true,false,true,true)==1);
    assert(xr_desktop_sync_interval(1,0,false,true,true,true)==1);
    assert(xr_desktop_sync_interval(1,0,true,true,false,true)==1);
    assert(xr_desktop_sync_interval(1,0,true,true,true,false)==1);
    assert(d2vr::xr_desktop_present_flags(0,true)==0x8 && d2vr::xr_desktop_present_flags(0x200,true)==0x208);
    assert(d2vr::xr_desktop_present_flags(0,false)==0 && d2vr::xr_desktop_present_flags(1,false)==1);
    std::cout<<"PASS: runtime-owned XR pacing and native/test/simulation/fallback desktop interval\n";
}
