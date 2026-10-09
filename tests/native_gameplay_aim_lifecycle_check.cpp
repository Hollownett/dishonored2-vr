#include "../src/native_gameplay_aim.hpp"
#include "../src/native_gameplay_aim_trace.hpp"
#include <cassert>
#include <iostream>
static unsigned hook_calls=0;
extern "C" MH_STATUS WINAPI MH_CreateHook(LPVOID,LPVOID,LPVOID*) {++hook_calls;return MH_ERROR_NOT_INITIALIZED;}
extern "C" MH_STATUS WINAPI MH_EnableHook(LPVOID) {++hook_calls;return MH_ERROR_NOT_INITIALIZED;}
extern "C" MH_STATUS WINAPI MH_DisableHook(LPVOID) {++hook_calls;return MH_ERROR_NOT_INITIALIZED;}
static bool no_read(std::uintptr_t,void*,std::size_t) {return false;}
static bool no_write(std::uintptr_t,const void*,std::size_t) {assert(false);return false;}
static UINT logic() {return 2;}
int main() {
    d2vr::NativeGameplayAimTrace trace;
    assert(trace.disable() && trace.disable() && hook_calls==0);
    bool trace_rejected=false;
    try {trace.install(0x140000000,no_read,logic,L"unused.csv");} catch(const std::runtime_error&) {trace_rejected=true;}
    assert(trace_rejected && trace.disable() && hook_calls==0);
    d2vr::NativeGameplayAim writer;
    assert(writer.disable() && writer.disable() && hook_calls==0);
    bool rejected=false;
    try {writer.install(0,nullptr,nullptr,nullptr,L"unused.csv");} catch(const std::runtime_error&) {rejected=true;}
    assert(rejected && writer.disable() && hook_calls==0);
    rejected=false;
    try {writer.install(0x140000000,no_read,logic,no_write,L"unused.csv");} catch(const std::runtime_error&) {rejected=true;}
    assert(rejected && writer.disable() && hook_calls==0 && writer.applied()==0);
    std::cout<<"PASS: no-op cleanup before install, invalid callbacks/build rejected before hooks or writes\n";
}
