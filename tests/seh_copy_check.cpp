// The guarded copy survives a read from (and a write to) a released page under the build's own flags (/EHsc): src/seh_copy.hpp.
#include <windows.h>
#include "../src/seh_copy.hpp"
#include <cstdint>
#include <cstdio>

int main() {
    void* page=VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!page) {std::puts("FAIL: VirtualAlloc");return 1;}
    unsigned char buffer[16]{};
    std::memset(page,0x5a,64);
    if(!d2vr::seh_copy(buffer,page,16) || buffer[0]!=0x5a) {std::puts("FAIL: a copy from a live page");return 1;}
    VirtualFree(page,0,MEM_RELEASE);   // the page is gone: the next copies fault
    if(d2vr::seh_copy(buffer,page,8)) {std::puts("FAIL: a copy from a released page succeeded");return 1;}
    if(d2vr::seh_copy(page,buffer,8)) {std::puts("FAIL: a copy to a released page succeeded");return 1;}
    if(d2vr::seh_copy(buffer,reinterpret_cast<const void*>(std::uintptr_t{0x10}),8)) {std::puts("FAIL: a copy from address 0x10 succeeded");return 1;}
    const auto integer_copy=[](void* d,const void* s,std::size_t n) noexcept {__try {d2vr::seh_integer_copy(d,s,n);return true;} __except(EXCEPTION_EXECUTE_HANDLER) {return false;}};
    unsigned char live[8]{1,2,3,4,5,6,7,8};
    if(!integer_copy(buffer,live,8) || buffer[7]!=8) {std::puts("FAIL: the integer copy from a live buffer");return 1;}
    if(integer_copy(buffer,page,8)) {std::puts("FAIL: the integer copy from a released page succeeded");return 1;}
    std::puts("seh_copy_check passed");
    return 0;
}
