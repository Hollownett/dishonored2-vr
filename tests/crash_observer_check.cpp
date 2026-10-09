#include "../src/crash_observer.hpp"
#include <cassert>
#include <fstream>
#include <iostream>
#include <string>

bool handled_fault() {
    const ULONG_PTR parameters[2]{0,0x1234};
    __try {RaiseException(EXCEPTION_ACCESS_VIOLATION,0,2,parameters);}
    __except(EXCEPTION_EXECUTE_HANDLER) {return true;}
    return false;
}
int main() {
    d2vr::CrashObserver observer;
    // No module match: an untagged, externally handled fault is ignored.
    observer.install(L"build/crash-observer-check.txt",0x10000,1);
    assert(handled_fault() && observer.records()==0);
    {d2vr::CrashOperation operation("consumer-test");assert(handled_fault() && observer.records()==1);}
    assert(d2vr::crash_operation==nullptr && observer.remove());
    {d2vr::CrashOperation operation("removed-test");assert(handled_fault() && observer.records()==1);}
    std::ifstream input("build/crash-observer-check.txt");std::string text((std::istreambuf_iterator<char>(input)),{});
    assert(text.find("first_chance=1")!=std::string::npos && text.find("operation=consumer-test")!=std::string::npos);
    assert(text.find("access_address=0x1234")!=std::string::npos && text.find("removed-test")==std::string::npos);
    observer.install(L"build/crash-observer-native-check.txt",0x10000,1);observer.observe_native_faults(true);
    assert(handled_fault() && observer.records()==1 && observer.remove());
    std::ifstream native("build/crash-observer-native-check.txt");text.assign(std::istreambuf_iterator<char>(native),{});
    assert(text.find("operation=native/external thread")!=std::string::npos && text.find("stack_slot=0")!=std::string::npos);
    std::cout<<"PASS: tagged/native exception evidence, bounded stack read, unchanged external SEH handling and removal\n";
}
