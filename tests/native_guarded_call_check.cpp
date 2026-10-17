#include "../src/native_guarded_call.hpp"
#include <cstdio>

static int failures{};
static void require(bool passed,const char* message) {if(!passed) {std::fprintf(stderr,"FAIL: %s\n",message);++failures;}}

int main() {
    int hit=0;
    auto normal=[&]{hit=1;};
    require(d2vr::guarded_call(normal) && hit==1,"guarded_call runs the callable and reports normal return");
    auto fault=[&]{hit=2;*reinterpret_cast<volatile int*>(0x10)=1;hit=3;};
    require(!d2vr::guarded_call(fault) && hit==2,"guarded_call catches an access violation and the process continues");
    auto again=[&]{hit=4;};
    require(d2vr::guarded_call(again) && hit==4,"guarded_call is reusable after a fault");
    return failures?1:0;
}
