#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../src/native_pad_focus.hpp"
#include <cassert>
#include <iostream>

// Executable fixture only: no game process, Windows focus or input injection.
static bool read_fixture(std::uintptr_t at,void* out,std::size_t size) {
    SIZE_T count{};
    return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(at),out,size,&count) && count==size;
}
template<class T> void put(std::uintptr_t at,const T& value) {std::memcpy(reinterpret_cast<void*>(at),&value,sizeof(value));}
static void caller(std::uintptr_t base,std::uintptr_t return_rva,std::uintptr_t target=0xd1f70) {
    const std::array<unsigned char,4> sub{0x48,0x83,0xec,0x28};put(base+return_rva-9,sub);
    put(base+return_rva-5,static_cast<unsigned char>(0xe8));
    put(base+return_rva-4,static_cast<std::int32_t>(target-return_rva));
    const std::array<unsigned char,5> end{0x48,0x83,0xc4,0x28,0xc3};put(base+return_rva,end);
}
int main() {
    const auto memory=VirtualAlloc(nullptr,0x3200000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);assert(memory);
    const auto base=reinterpret_cast<std::uintptr_t>(memory),root=base+0x3134140;
    const std::array<unsigned char,16> getter{0x0f,0xb6,0x41,0x08,0xc3,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc};
    put(base+0xd1f70,getter);caller(base,0xe1098);caller(base,0xe0146);caller(base,0x21f2c0);
    const std::array<unsigned char,24> window_gate{0x80,0x3d,0x5a,0x42,0x05,0x03,0x00,0x74,0x0c,
        0x80,0x3d,0x52,0x42,0x05,0x03,0x00,0x74,0x03,0xb0,0x01,0xc3,0x32,0xc0,0xc3};
    put(base+0xe3cf0,window_gate);caller(base,0x3a4ab,0xe3cf0);caller(base,0x21f380,0xe3cf0);
    put(root,base+0x1ae06d8);put(root+8,static_cast<unsigned char>(0));
    put(root+0x600,static_cast<unsigned char>(0));put(root+0x5f8,1);
    const auto event=CreateEventW(nullptr,FALSE,FALSE,nullptr);assert(event);put(root+0x5e8,reinterpret_cast<std::uintptr_t>(event));
    assert(FlushInstructionCache(GetCurrentProcess(),memory,0x3200000));
    using Get=unsigned char (__fastcall*)(void*);
    const auto poll=reinterpret_cast<Get>(base+0xe1098-9),serialize=reinterpret_cast<Get>(base+0xe0146-9);
    const auto other=reinterpret_cast<Get>(base+0x21f2c0-9);
    using Window=unsigned char (__fastcall*)();
    const auto pad_window=reinterpret_cast<Window>(base+0x3a4ab-9);
    const auto other_window=reinterpret_cast<Window>(base+0x21f380-9);
    assert(MH_Initialize()==MH_OK);d2vr::NativePadFocus focus;focus.install(base,&read_fixture);
    assert(poll(reinterpret_cast<void*>(root))==0 && serialize(reinterpret_cast<void*>(root))==0);
    assert(pad_window()==0 && other_window()==0);
    const auto now=GetTickCount64();focus.publish(true,now);
    assert(WaitForSingleObject(event,0)==WAIT_OBJECT_0 && focus.wakeups()==1);
    assert(poll(reinterpret_cast<void*>(root))==1 && serialize(reinterpret_cast<void*>(root))==1);
    assert(other(reinterpret_cast<void*>(root))==0 && focus.overrides()==2);
    assert(pad_window()==1 && other_window()==0 && focus.window_overrides()==1);
    assert(*reinterpret_cast<unsigned char*>(base+0x3137f51)==0 && *reinterpret_cast<unsigned char*>(base+0x3137f52)==0);
    assert(poll(reinterpret_cast<void*>(root+0x80))==0 && focus.overrides()==2);
    assert(*reinterpret_cast<unsigned char*>(root+8)==0);
    focus.publish(true,now);assert(focus.wakeups()==1 && WaitForSingleObject(event,0)==WAIT_TIMEOUT);
    focus.publish(false,now);assert(poll(reinterpret_cast<void*>(root))==0);
    assert(pad_window()==0);
    focus.publish(true,GetTickCount64());Sleep(270);assert(pad_window()==0);
    focus.publish(true,GetTickCount64());put(root+0x600,static_cast<unsigned char>(1));
    assert(pad_window()==0);put(root+0x600,static_cast<unsigned char>(0));
    put(base+0x3137f51,static_cast<unsigned char>(1));put(base+0x3137f52,static_cast<unsigned char>(1));
    assert(pad_window()==1 && other_window()==1 && focus.window_overrides()==1);
    put(base+0x3137f51,static_cast<unsigned char>(0));put(base+0x3137f52,static_cast<unsigned char>(0));
    put(root+8,static_cast<unsigned char>(1));assert(other(reinterpret_cast<void*>(root))==1);
    put(root+8,static_cast<unsigned char>(0));assert(focus.disable());
    assert(!std::memcmp(reinterpret_cast<void*>(base+0xd1f70),getter.data(),getter.size()));
    assert(!std::memcmp(reinterpret_cast<void*>(base+0xe3cf0),window_gate.data(),window_gate.size()));
    assert(pad_window()==0 && other_window()==0 && focus.poll_calls()==4 && focus.serializer_calls()==2);
    assert(poll(reinterpret_cast<void*>(root))==0 && MH_Uninitialize()==MH_OK);
    assert(CloseHandle(event) && VirtualFree(memory,0,MEM_RELEASE));
    std::cout<<"PASS: native poll/serialization gates and upstream pad-only window gate, unrelated/foreign/stale passthrough, worker wake and both exact restorations on isolated executable fixture\n";
}
