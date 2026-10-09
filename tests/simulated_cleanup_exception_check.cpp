#include "../src/simulated_cleanup_exception.hpp"
#include <cassert>
#include <initializer_list>

int main() {
    using d2vr::valid_cleanup_exception_profile;
    assert(valid_cleanup_exception_profile(0,false,false,false,false,false,false,0,true,true));
    for(unsigned seconds : {10u,30u,120u})
        assert(valid_cleanup_exception_profile(1,true,true,true,true,true,true,seconds,false,false));
    for(unsigned guard=0;guard<6;++guard) {
        bool flags[6]{true,true,true,true,true,true};flags[guard]=false;
        assert(!valid_cleanup_exception_profile(1,flags[0],flags[1],flags[2],flags[3],flags[4],flags[5],30,false,false));
    }
    for(unsigned seconds : {0u,9u,121u})
        assert(!valid_cleanup_exception_profile(1,true,true,true,true,true,true,seconds,false,false));
    assert(!valid_cleanup_exception_profile(1,true,true,true,true,true,true,30,true,false));
    assert(!valid_cleanup_exception_profile(1,true,true,true,true,true,true,30,false,true));
    assert(!valid_cleanup_exception_profile(-1,true,true,true,true,true,true,30,false,false));
    assert(!valid_cleanup_exception_profile(2,true,true,true,true,true,true,30,false,false));
}
