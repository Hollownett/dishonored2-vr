#define NOMINMAX
#include <windows.h>
#include "../src/native_manual_equip_hooks.hpp"
#include <cassert>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <thread>

namespace {
using Hooks=d2vr::NativeManualEquipHooks;
std::uintptr_t image_base{};constexpr std::size_t image_size=0x900000;
unsigned create_calls{},enable_calls{},disable_calls{},fail_create{},fail_enable{},fail_disable{};
unsigned processor_calls{},wheel_calls{},host_calls{},before_calls{},after_calls{},failure_mode{};
std::uintptr_t last_state{},last_controller{},last_item{},last_ammo{},last_caller{};
Hooks::Params last_params{};Hooks* semantic_owner{};bool completed_after{},admission_after{};
std::atomic<bool> parked{},released{};
bool read(std::uintptr_t address,void* output,std::size_t size) {
    if(failure_mode==17)throw std::runtime_error("read");
    if(failure_mode==18)RaiseException(0xe0423326,0,0,nullptr);
    SIZE_T copied{};return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),output,size,&copied) && copied==size;
}
void __fastcall native_processor(std::uintptr_t state,const void* params) {
    ++processor_calls;last_state=state;std::memcpy(&last_params,params,sizeof(last_params));
    if(failure_mode==3)throw std::runtime_error("native processor");
    if(failure_mode==4)RaiseException(0xe0423321,0,0,nullptr);
    if(failure_mode==5)assert(!semantic_owner->disable(0));
}
void __fastcall native_wheel(std::uintptr_t controller,std::uintptr_t item,std::uintptr_t ammo) {
    ++wheel_calls;last_controller=controller;last_item=item;last_ammo=ammo;
    if(failure_mode==6)throw std::runtime_error("native wheel");
    if(failure_mode==7)RaiseException(0xe0423322,0,0,nullptr);
    if(failure_mode==15)assert(!semantic_owner->disable(0));
}
bool processor_host(void*,std::uintptr_t caller,std::uintptr_t,Hooks::Params& params) {
    ++host_calls;last_caller=caller;params.bytes[24]&=0xf3;
    if(failure_mode==1)throw std::runtime_error("host");
    if(failure_mode==2)RaiseException(0xe0423323,0,0,nullptr);
    if(failure_mode==8)return false;
    if(failure_mode==9) {
        failure_mode=0;semantic_owner->invoke_wheel(image_base+Hooks::wheel_return_rva,0x1111222233334444,0x5555666677778888,0x9999aaaabbbbcccc);failure_mode=9;
    }
    if(failure_mode==14) {parked=true;while(!released)std::this_thread::yield();}
    return true;
}
bool before_host(void*,std::uintptr_t caller,std::uintptr_t,std::uintptr_t,std::uintptr_t) {
    ++before_calls;last_caller=caller;
    if(failure_mode==10)throw std::runtime_error("before");
    if(failure_mode==11)RaiseException(0xe0423324,0,0,nullptr);
    return true;
}
bool after_host(void*,std::uintptr_t,std::uintptr_t,std::uintptr_t,std::uintptr_t,bool completed,bool current) {
    ++after_calls;completed_after=completed;admission_after=current;
    if(failure_mode==12)throw std::runtime_error("after");
    if(failure_mode==13)RaiseException(0xe0423325,0,0,nullptr);
    if(failure_mode==19)return false;
    return true;
}
Hooks::Callbacks callbacks() {return {nullptr,&processor_host,&before_host,&after_host};}
void seed(void* requested=nullptr) {
    image_base=reinterpret_cast<std::uintptr_t>(VirtualAlloc(requested,image_size,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));assert(image_base);
    for(unsigned index=0;index<2;++index)std::memcpy(reinterpret_cast<void*>(image_base+Hooks::rvas[index]),Hooks::prologues[index].data(),32);
    std::memcpy(reinterpret_cast<void*>(image_base+Hooks::wheel_return_rva-5),Hooks::wheel_call.data(),5);
}
#ifdef D2VR_MANUAL_EQUIP_REAL_HOOK
void seed_native(const char* path) {
    std::ifstream input(path,std::ios::binary);assert(input);const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(input)),{});
    const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(bytes.data());assert(dos->e_magic==IMAGE_DOS_SIGNATURE);
    const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(bytes.data()+dos->e_lfanew);assert(nt->Signature==IMAGE_NT_SIGNATURE);
    const auto* sections=IMAGE_FIRST_SECTION(nt);
    const auto copy=[&](std::uintptr_t rva,unsigned size) {
        for(unsigned index=0;index<nt->FileHeader.NumberOfSections;++index) {
            const auto& section=sections[index];
            if(rva>=section.VirtualAddress && rva-section.VirtualAddress<=section.SizeOfRawData && size<=section.SizeOfRawData-(rva-section.VirtualAddress)) {
                const auto offset=section.PointerToRawData+rva-section.VirtualAddress;assert(offset+size<=bytes.size());
                std::memcpy(reinterpret_cast<void*>(image_base+rva),bytes.data()+offset,size);return;
            }
        }
        assert(false);
    };
    copy(Hooks::rvas[0],0x474);copy(Hooks::rvas[1],0x150);copy(Hooks::wheel_return_rva-5,5);
}
#endif
bool seh_processor(Hooks& owner,const Hooks::Params& params) {
    __try {owner.invoke_processor(0x1234,0x778899,&params);return false;}
    __except(EXCEPTION_EXECUTE_HANDLER) {return GetExceptionCode()==0xe0423321;}
}
bool seh_wheel(Hooks& owner) {
    __try {owner.invoke_wheel(image_base+Hooks::wheel_return_rva,11,22,33);return false;}
    __except(EXCEPTION_EXECUTE_HANDLER) {return GetExceptionCode()==0xe0423322;}
}
}
#ifndef D2VR_MANUAL_EQUIP_REAL_HOOK
extern "C" MH_STATUS WINAPI MH_CreateHook(LPVOID target,LPVOID detour,LPVOID* original) {
    if(++create_calls==fail_create)return MH_ERROR_UNSUPPORTED_FUNCTION;
    const unsigned index=reinterpret_cast<std::uintptr_t>(target)==image_base+Hooks::rvas[0]?0u:1u;
    const auto prefix=Hooks::prefixes[index];auto* code=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));assert(code);
    std::memcpy(code,target,prefix);code[prefix]=code[prefix+14]=0xff;code[prefix+1]=code[prefix+15]=0x25;
    const auto continuation=reinterpret_cast<std::uintptr_t>(target)+prefix;std::memcpy(code+prefix+6,&continuation,8);std::memcpy(code+prefix+20,&detour,8);
    *original=code;return MH_OK;
}
extern "C" MH_STATUS WINAPI MH_EnableHook(LPVOID) {return ++enable_calls==fail_enable?MH_ERROR_MEMORY_PROTECT:MH_OK;}
extern "C" MH_STATUS WINAPI MH_DisableHook(LPVOID) {return ++disable_calls==fail_disable?MH_ERROR_MEMORY_PROTECT:MH_OK;}
#endif
namespace d2vr {
struct NativeManualEquipHooksFixture {
    static Hooks& make() {return *new Hooks;}
    static Hooks& semantic() {
        auto& owner=make();owner.base_=image_base;owner.read_=read;owner.callbacks_=callbacks();owner.stopping_=false;owner.stop_complete_=false;
        owner.originals_[0]=reinterpret_cast<void*>(&native_processor);owner.originals_[1]=reinterpret_cast<void*>(&native_wheel);
        owner.installed_[0]=owner.installed_[1]=true;owner.created_[0]=owner.created_[1]=true;owner.enabled(true);return owner;
    }
    static bool clean(const Hooks& owner) {return !Hooks::depth_ && !owner.in_flight();}
    static void unwind(const Hooks& owner) {
        for(unsigned index=0;index<2;++index) {
            const auto prefix=Hooks::prefixes[index];const auto native=reinterpret_cast<std::uintptr_t>(owner.original(index));
            for(unsigned offset=0;offset<prefix+28;++offset) {
                DWORD64 lookup{};assert(RtlLookupFunctionEntry(native+offset,&lookup,nullptr));
            }
            const std::array<unsigned,7> instruction_offsets{0,3,4,prefix,prefix+10,prefix+13,prefix+14};
            for(const auto offset:instruction_offsets) {
                if(index==0 && (offset==3 || offset==4))continue;
                std::array<std::uintptr_t,48> stack{};const auto entry=reinterpret_cast<std::uintptr_t>(stack.data()+20);
                stack[20]=0x12345678;stack[23]=0x11112222;
                CONTEXT context{};context.ContextFlags=CONTEXT_FULL;context.Rip=native+offset;context.Rsp=entry;
                context.Rbx=0x11112222;context.Rsi=0x33334444;context.R14=0x55556666;
                if(index==1 && offset>=4 && offset<prefix+14) {context.Rsp-=8;*reinterpret_cast<std::uintptr_t*>(context.Rsp)=context.Rsi;}
                if(index==1 && offset>=6 && offset<prefix+14) {context.Rsp-=8;*reinterpret_cast<std::uintptr_t*>(context.Rsp)=context.R14;}
                DWORD64 lookup{};const auto function=RtlLookupFunctionEntry(context.Rip,&lookup,nullptr);assert(function);
                PVOID handler{};DWORD64 establisher{};RtlVirtualUnwind(UNW_FLAG_NHANDLER,lookup,context.Rip,function,&context,&handler,&establisher,nullptr);
                assert(context.Rip==0x12345678 && context.Rsp==entry+8 && context.Rbx==0x11112222 && context.Rsi==0x33334444 && context.R14==0x55556666);
            }
        }
    }
};
}
int main(int argc,char** argv) {
    using Fixture=d2vr::NativeManualEquipHooksFixture;seed();
#ifdef D2VR_MANUAL_EQUIP_REAL_HOOK
    assert(argc==2);seed_native(argv[1]);
    assert(MH_Initialize()==MH_OK);auto& installed=Hooks::process_owner();
#else
    (void)argc;(void)argv;
    auto& installed=Fixture::make();
#endif
    assert(Hooks::verify(image_base,read));
    assert(installed.install(image_base,read,callbacks()) && !installed.enabled());Fixture::unwind(installed);
    assert(installed.disable() && installed.stopped() && installed.install(image_base,read,callbacks()));
    assert(installed.disable());
    auto* tail_byte=reinterpret_cast<unsigned char*>(installed.original(1))+Hooks::prefixes[1]+13;
    const auto saved=*tail_byte;DWORD protection{};assert(VirtualProtect(tail_byte,1,PAGE_EXECUTE_READWRITE,&protection));*tail_byte=0xcc;
    assert(!installed.install(image_base,read,callbacks()));*tail_byte=saved;DWORD ignored{};assert(VirtualProtect(tail_byte,1,protection,&ignored));
    assert(installed.disable() && installed.install(image_base,read,callbacks()) && installed.disable());
    const auto normal_base=image_base;
    constexpr std::uintptr_t poison_base=0x1c300000000ull;
    seed(reinterpret_cast<void*>(poison_base));assert(image_base==poison_base);
#ifdef D2VR_MANUAL_EQUIP_REAL_HOOK
    seed_native(argv[1]);
#endif
    auto& poisoned=Fixture::make();assert(poisoned.install(image_base,read,callbacks()));
    for(unsigned index=0;index<2;++index){
        const auto* bytes=static_cast<const unsigned char*>(poisoned.original(index))+Hooks::prefixes[index];
        assert(bytes[0]==0x49 && bytes[1]==0xbb && bytes[6]==0xc3);
        std::uintptr_t continuation{};std::memcpy(&continuation,bytes+2,sizeof(continuation));
        assert(continuation==image_base+Hooks::rvas[index]+Hooks::prefixes[index]);
    }
    Fixture::unwind(poisoned);assert(poisoned.disable());image_base=normal_base;
#ifndef D2VR_MANUAL_EQUIP_REAL_HOOK
    assert(installed.install(image_base,read,callbacks()));fail_disable=disable_calls+1;
    assert(!installed.disable(0) && !installed.stopped() && !installed.ready_for_install());
    fail_disable=0;assert(installed.disable(0) && installed.stopped());
    for(unsigned failure=1;failure<=2;++failure) {
        create_calls=enable_calls=disable_calls=0;fail_create=failure;auto& owner=Fixture::make();assert(!owner.install(image_base,read,callbacks()) && owner.stopped());fail_create=0;
        create_calls=enable_calls=disable_calls=0;fail_enable=failure;auto& other=Fixture::make();assert(!other.install(image_base,read,callbacks()) && other.stopped());fail_enable=0;
    }
#endif
    auto& owner=Fixture::semantic();semantic_owner=&owner;Hooks::Params params{};
    for(unsigned index=0;index<48;++index)params.bytes[index]=static_cast<unsigned char>(index+1);params.bytes[24]=0x16;
    owner.invoke_processor(0x123456789abcdef0,0x1234432112344321,&params);
    assert(processor_calls==1 && host_calls==1 && last_caller==0x123456789abcdef0 && last_state==0x1234432112344321 && last_params.bytes[24]==0x12 && params.bytes[24]==0x16);
    for(const unsigned failure:{1u,2u,8u}) {failure_mode=failure;owner.enabled(true);const auto before=processor_calls;owner.invoke_processor(44,55,&params);assert(processor_calls==before+1 && last_params.bytes==params.bytes && !owner.enabled() && Fixture::clean(owner));}
    failure_mode=3;owner.enabled(true);try {owner.invoke_processor(1,2,&params);assert(false);}catch(const std::runtime_error&) {}assert(Fixture::clean(owner));
    failure_mode=4;assert(seh_processor(owner,params) && Fixture::clean(owner));
    for(const unsigned failure:{17u,18u}) {
        failure_mode=failure;owner.enabled(true);const auto before=host_calls;owner.invoke_processor(44,55,&params);
        assert(host_calls==before && last_params.bytes==params.bytes && Fixture::clean(owner));
    }
    for(const unsigned failure:{6u,7u,10u,11u,12u,13u,19u}) {
        failure_mode=failure;owner.enabled(true);const auto before=wheel_calls;
        if(failure==6) {try {owner.invoke_wheel(image_base+Hooks::wheel_return_rva,11,22,33);assert(false);}catch(const std::runtime_error&) {}}
        else if(failure==7)assert(seh_wheel(owner));else owner.invoke_wheel(image_base+Hooks::wheel_return_rva,11,22,33);
        assert(wheel_calls==before+1 && completed_after==(failure!=6 && failure!=7) && Fixture::clean(owner));
    }
    failure_mode=0;owner.enabled(true);const auto before=before_calls;owner.invoke_wheel(image_base+0x4dff48,11,22,33);assert(before_calls==before && last_controller==11 && last_item==22 && last_ammo==33);
    failure_mode=9;owner.invoke_processor(1,2,&params);assert(last_controller==0x1111222233334444 && last_item==0x5555666677778888 && last_ammo==0x9999aaaabbbbcccc && Fixture::clean(owner));
    failure_mode=15;owner.invoke_wheel(image_base+Hooks::wheel_return_rva,11,22,33);
    assert(!owner.enabled() && completed_after && !admission_after && Fixture::clean(owner));
    assert(owner.disable(0));
    auto& parked_owner=Fixture::semantic();semantic_owner=&parked_owner;
    failure_mode=14;parked=false;released=false;
    std::thread pending([&] {parked_owner.invoke_processor(1,2,&params);});
    while(!parked)std::this_thread::yield();
    assert(!parked_owner.disable(0) && !parked_owner.stopped() && !parked_owner.ready_for_install() && !parked_owner.install(image_base,read,callbacks()));
    released=true;pending.join();assert(last_params.bytes==params.bytes && Fixture::clean(parked_owner));
    failure_mode=0;assert(parked_owner.disable(0) && parked_owner.stopped());
    const auto previous_hosts=host_calls;parked_owner.invoke_processor(1,2,&params);assert(host_calls==previous_hosts && last_params.bytes==params.bytes && Fixture::clean(parked_owner));
    auto& aba=Fixture::semantic();semantic_owner=&aba;failure_mode=14;parked=false;released=false;
    std::thread crossed([&] {aba.invoke_processor(1,2,&params);});
    while(!parked)std::this_thread::yield();aba.enabled(false);aba.enabled(true);released=true;crossed.join();
    assert(last_params.bytes==params.bytes && !aba.enabled() && Fixture::clean(aba));
    semantic_owner=&owner;
    failure_mode=5;owner.enabled(true);owner.invoke_processor(1,2,&params);assert(!owner.enabled() && Fixture::clean(owner));failure_mode=0;
#ifndef D2VR_MANUAL_EQUIP_REAL_HOOK
    assert(owner.disable(0) && owner.stopped());
#endif
    std::cout<<"PASS retained two-entry hooks, original-once host isolation, exact wheel caller, partial rollback, reinstall, nested/C++/SEH and generated trampoline unwind; no production/native body execution\n";
}
