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
    // v1041 ring (build/crash-observer-check-last.txt, written through a mapped file): every fault anywhere is named once per address with
    // a count, the file budget skips an address that repeats, and a fatal/exit line exists. The earlier observer's file is complete once removed.
    std::ifstream ring("build/crash-observer-check-last.txt",std::ios::binary);std::string ring_text((std::istreambuf_iterator<char>(ring)),{});
    assert(ring_text.size()==18*1024 && ring_text.find("count=2")!=std::string::npos && ring_text.find("first-chance code=0xc0000005")!=std::string::npos);
    assert(ring_text.find("FATAL none")!=std::string::npos && ring_text.find("EXIT none")!=std::string::npos);
    {   // the same address 10 times: one ring line (count=10), at most 4 file records
        d2vr::CrashObserver again;again.install(L"build/crash-observer-repeat-check.txt",0x10000,1);again.observe_native_faults(true);
        for(int i=0;i<10;++i) assert(handled_fault());
        assert(again.records()<=4 && again.ring_records()==10);
        again.note_exit(true);assert(again.remove());
        std::ifstream repeat("build/crash-observer-repeat-check-last.txt",std::ios::binary);std::string repeat_text((std::istreambuf_iterator<char>(repeat)),{});
        assert(repeat_text.find("count=10")!=std::string::npos && repeat_text.find("EXIT tick=")!=std::string::npos && repeat_text.find("process_terminating=1")!=std::string::npos);
    }
    std::cout<<"PASS: tagged/native exception evidence, bounded stack read, unchanged external SEH handling, last-fault ring and removal\n";
}
