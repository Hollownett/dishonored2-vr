#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../src/gpu_dispatch_bindings.hpp"
#include <cassert>
#include <iostream>
#include <thread>
using Registry=d2vr::GpuDispatchBindings<1>;
using Function=int (*)(int);
static Registry real_registry;
static Registry concurrent_registry{Registry::Api{},true};
static HANDLE entered{},resume{};
static std::atomic<bool> delay{false};
__declspec(noinline) int first(int x) {return (x*7+19)^(x>>1);}
__declspec(noinline) int second(int x) {return (x*11+23)^(x>>2);}
__declspec(noinline) int third(int x) {return (x*13+29)^(x>>3);}
__declspec(noinline) int fourth(int x) {return (x*17+31)^(x>>4);}
__declspec(noinline) int fifth(int x) {return (x*19+37)^(x>>5);}
template<unsigned Binding> int intercepted(int x) {
    if constexpr(Binding==0) {if(delay.load()) {SetEvent(entered);WaitForSingleObject(resume,INFINITE);}}
    return reinterpret_cast<Function>(real_registry.original(0,Binding))(x)+1000;
}
template<unsigned Binding> int concurrent_intercepted(int x) {
    return reinterpret_cast<Function>(concurrent_registry.original(0,Binding))(x)+2000;
}
static bool read(void* address,void* bytes,std::size_t size) {
    SIZE_T count{};return ReadProcessMemory(GetCurrentProcess(),address,bytes,size,&count) && count==size;
}
static bool valid(void* p) {return p!=nullptr;}
static Registry::Wrappers wrappers() {
    return {{{{reinterpret_cast<void*>(&intercepted<0>),reinterpret_cast<void*>(&intercepted<1>),
        reinterpret_cast<void*>(&intercepted<2>),reinterpret_cast<void*>(&intercepted<3>)}}}};
}
static int fail_create{},fail_queue{},fail_apply{},fail_disable{},creates{},queues{},applies{};
static unsigned char* mutate_during_apply{};
static MH_STATUS WINAPI fake_create(LPVOID target,LPVOID,LPVOID* original) {
    if(++creates==fail_create) return MH_ERROR_MEMORY_ALLOC;
    *original=target;return MH_OK;
}
static MH_STATUS WINAPI fake_queue(LPVOID) {return ++queues==fail_queue?MH_ERROR_NOT_CREATED:MH_OK;}
static MH_STATUS WINAPI fake_apply() {++applies;if(mutate_during_apply)*mutate_during_apply^=1;return fail_apply?MH_ERROR_MEMORY_PROTECT:MH_OK;}
static MH_STATUS WINAPI fake_disable(LPVOID) {return fail_disable?MH_ERROR_MEMORY_PROTECT:MH_ERROR_DISABLED;}
static Registry::Api fake_api() {creates=queues=applies=fail_create=fail_queue=fail_apply=fail_disable=0;mutate_during_apply=nullptr;return {&fake_create,&fake_queue,&fake_queue,&fake_apply,&fake_disable};}
int main() {
    assert(MH_Initialize()==MH_OK);
    const auto replacements=wrappers();
    Registry::Targets current{{reinterpret_cast<void*>(&first)}};
    auto snapshot=[&](Registry::Targets& out) {out=current;return true;};
    assert(real_registry.bind(current,replacements,read,valid,snapshot));
    Function volatile call_first=&first,call_second=&second;
    assert(call_first(3)==((3*7+19)^(3>>1))+1000);
    auto* old_original=real_registry.original(0,0);
    entered=CreateEventW(nullptr,TRUE,FALSE,nullptr);resume=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    assert(entered && resume);delay=true;
    int delayed_result{};
    std::thread worker([&] {delayed_result=call_first(3);});
    assert(WaitForSingleObject(entered,5000)==WAIT_OBJECT_0);
    // Worker is already in the old detour before a callback-depth increment.
    current[0]=reinterpret_cast<void*>(&second);
    assert(real_registry.bind(current,replacements,read,valid,snapshot));
    assert(real_registry.original(0,0)==old_original);
    assert(call_first(3)==((3*7+19)^(3>>1)));
    assert(call_second(3)==((3*11+23)^(3>>2))+1000);
    SetEvent(resume);worker.join();delay=false;
    assert(delayed_result==((3*7+19)^(3>>1))+1000);
    current[0]=reinterpret_cast<void*>(&first);
    assert(real_registry.bind(current,replacements,read,valid,snapshot));
    assert(real_registry.created()==2 && real_registry.rebinds()==2 && real_registry.reuses()==1);
    assert(real_registry.original(0,0)==old_original);
    std::atomic<UINT> depth{0};
    assert(real_registry.disable_all(depth,read,20));
    assert(call_first(3)==((3*7+19)^(3>>1)) && call_second(3)==((3*11+23)^(3>>2)));
    // Disabled hook records and originals remain reusable; nothing was removed.
    assert(MH_CreateHook(reinterpret_cast<void*>(&first),reinterpret_cast<void*>(&intercepted<0>),nullptr)==MH_ERROR_ALREADY_CREATED);
    assert(real_registry.bind(current,replacements,read,valid,snapshot));
    depth=1;assert(!real_registry.disable_all(depth,read,2) && real_registry.poisoned());depth=0;
    assert(real_registry.disable_all(depth,read,20));
    assert(!real_registry.bind(current,replacements,read,valid,snapshot));
    CloseHandle(entered);CloseHandle(resume);
    {
        const Registry::Wrappers concurrent_wrappers{{{{reinterpret_cast<void*>(&concurrent_intercepted<0>),
            reinterpret_cast<void*>(&concurrent_intercepted<1>),reinterpret_cast<void*>(&concurrent_intercepted<2>),
            reinterpret_cast<void*>(&concurrent_intercepted<3>)}}}};
        Function volatile call_third=&third,call_fourth=&fourth;
        current[0]=reinterpret_cast<void*>(&third);
        assert(concurrent_registry.bind(current,concurrent_wrappers,read,valid,snapshot));
        auto* retained=concurrent_registry.original(0,0);
        current[0]=reinterpret_cast<void*>(&fourth);
        assert(concurrent_registry.bind(current,concurrent_wrappers,read,valid,snapshot));
        // Both live context dispatches still reach their own immutable original.
        assert(call_third(3)==((3*13+29)^(3>>3))+2000);
        assert(call_fourth(3)==((3*17+31)^(3>>4))+2000);
        for(unsigned i=0;i<1000;++i) {
            current[0]=reinterpret_cast<void*>(i&1?&third:&fourth);
            assert(concurrent_registry.bind(current,concurrent_wrappers,read,valid,snapshot));
            assert(std::strcmp(concurrent_registry.reason(),"unchanged")==0);
        }
        assert(concurrent_registry.created()==2 && concurrent_registry.original(0,0)==retained);
        assert(concurrent_registry.disable_all(depth,read,20));
        assert(call_third(3)==((3*13+29)^(3>>3)));
        assert(call_fourth(3)==((3*17+31)^(3>>4)));
    }
    // Real MinHook release is test-process teardown only, never a registry operation.
    assert(MH_Uninitialize()==MH_OK);
    const std::array<void*,5> targets{{reinterpret_cast<void*>(&first),reinterpret_cast<void*>(&second),
        reinterpret_cast<void*>(&third),reinterpret_cast<void*>(&fourth),reinterpret_cast<void*>(&fifth)}};
    {
        Registry alternating(fake_api());
        for(unsigned i=0;i<1002;++i) {
            current[0]=targets[i&1];assert(alternating.bind(current,replacements,read,valid,snapshot));
        }
        assert(applies==1002); // Prior production policy repatches every context switch.
        Registry simultaneous(fake_api(),true);
        for(unsigned i=0;i<1002;++i) {
            current[0]=targets[i&1];assert(simultaneous.bind(current,replacements,read,valid,snapshot));
        }
        assert(applies==2 && creates==2 && queues==2);
        assert(simultaneous.disable_all(depth,read,20));
        const auto before_enable=applies;
        current[0]=targets[0];assert(simultaneous.bind(current,replacements,read,valid,snapshot));
        assert(applies==before_enable+1); // Explicit cleanup still disables and re-enable qualifies.
        assert(simultaneous.disable_all(depth,read,20));
    }
    {
        std::array<unsigned char,16> retained{},next{};
        Registry mutation(fake_api(),true);
        current[0]=retained.data();assert(mutation.bind(current,replacements,read,valid,snapshot));
        mutate_during_apply=retained.data();current[0]=next.data();
        assert(!mutation.bind(current,replacements,read,valid,snapshot) && mutation.poisoned());
        assert(std::strcmp(mutation.reason(),"retained graphics target changed during batch")==0);
        mutate_during_apply=nullptr;retained[0]^=1;
        assert(mutation.disable_all(depth,read,20));
    }
    {
        Registry budget(fake_api());
        for(unsigned i=0;i<4;++i) {current[0]=targets[i];assert(budget.bind(current,replacements,read,valid,snapshot));}
        current[0]=targets[4];assert(!budget.bind(current,replacements,read,valid,snapshot));
        assert(budget.poisoned() && budget.created()==4);
    }
    for(unsigned failure=0;failure<3;++failure) {
        Registry partial(fake_api());current[0]=targets[0];
        if(failure==0) fail_create=1;else if(failure==1) fail_queue=1;else fail_apply=1;
        assert(!partial.bind(current,replacements,read,valid,snapshot) && partial.poisoned());
        assert(!partial.bind(current,replacements,read,valid,snapshot));
        fail_queue=fail_apply=0;assert(partial.disable_all(depth,read,20));
    }
    {
        Registry restoration(fake_api());current[0]=targets[0];assert(restoration.bind(current,replacements,read,valid,snapshot));
        fail_disable=1;assert(!restoration.disable_all(depth,read,2));
    }
    {
        Registry changing(fake_api());current[0]=targets[0];
        auto changed=[&](Registry::Targets& out) {out[0]=targets[1];return true;};
        assert(!changing.bind(current,replacements,read,valid,changed) && changing.created()==1 && changing.poisoned());
    }
    {
        d2vr::GpuDispatchBindings<2> duplicates;
        d2vr::GpuDispatchBindings<2>::Targets repeated{{targets[0],targets[0]}};
        d2vr::GpuDispatchBindings<2>::Wrappers unused{};
        auto same=[&](d2vr::GpuDispatchBindings<2>::Targets& out) {out=repeated;return true;};
        assert(!duplicates.bind(repeated,unused,read,valid,same) && duplicates.created()==0);
    }
    {
        using Two=d2vr::GpuDispatchBindings<2>;
        fake_api();fail_create=2;
        Two partial({&fake_create,&fake_queue,&fake_queue,&fake_apply,&fake_disable});
        Two::Targets two_targets{{targets[0],targets[1]}};
        Two::Wrappers two_wrappers{{replacements[0],replacements[0]}};
        auto same=[&](Two::Targets& out) {out=two_targets;return true;};
        assert(!partial.bind(two_targets,two_wrappers,read,valid,same));
        assert(partial.poisoned() && partial.created()==1 && partial.original(0,0)==targets[0]);
        assert(partial.disable_all(depth,read,20));
        assert(partial.original(0,0)==targets[0]); // Partial cleanup cannot reset published originals.
    }
    {
        Registry corrupted(fake_api());current[0]=targets[0];
        assert(corrupted.bind(current,replacements,read,valid,snapshot));
        auto wrong_bytes=[](void* address,void* bytes,std::size_t size) {
            if(!read(address,bytes,size)) return false;
            static_cast<std::byte*>(bytes)[0]^=std::byte{1};return true;
        };
        assert(!corrupted.bind(current,replacements,wrong_bytes,valid,snapshot));
        assert(corrupted.poisoned() && corrupted.created()==1);
        assert(!corrupted.disable_all(depth,wrong_bytes,20));
    }
    std::cout<<"PASS: retained MinHook callbacks, simultaneous dispatches, 1000 switches without repatching, explicit restoration/drain, target budget and poisoned API failures\n";
}

