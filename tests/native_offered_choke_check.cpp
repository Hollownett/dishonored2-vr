#include "../src/native_offered_choke.hpp"
#include <cassert>
#include <iostream>
#include <map>
#include <stdexcept>

extern "C" MH_STATUS WINAPI MH_CreateHook(LPVOID,LPVOID,LPVOID*) {return MH_ERROR_UNSUPPORTED_FUNCTION;}
extern "C" MH_STATUS WINAPI MH_EnableHook(LPVOID) {return MH_ERROR_NOT_CREATED;}
extern "C" MH_STATUS WINAPI MH_DisableHook(LPVOID) {return MH_ERROR_DISABLED;}
namespace {
using Policy=d2vr::NativeOfferedChoke;
constexpr std::uintptr_t base=0x140000000,game_address=0x200000000,player_address=0x201000000,machine=0x202000000,
    container=0x203000000,sensor=0x204000000,hud=0x205000000,victim=0x206000000,components_address=0x207000000,
    records=0x208000000,pool=0x209000000,choke_state=0x20a000000;
constexpr std::uint32_t target_spawn=0x10001;
std::map<std::uintptr_t,unsigned char> memory;
Policy* active{};
unsigned update_calls{},hint_calls{},mode{},logic_value=2;
bool throw_read{},fault_read{};
std::uintptr_t mutate_read{};
unsigned mutation_reads{};
bool mutate_list{};
unsigned owner_mutation{};
template<class Value> void put(std::uintptr_t address,const Value& value) {
    const auto* bytes=reinterpret_cast<const unsigned char*>(&value);
    for(std::size_t offset=0;offset<sizeof(value);++offset)memory[address+offset]=bytes[offset];
}
bool read(std::uintptr_t address,void* output,std::size_t size) {
    if(throw_read)throw std::runtime_error("read");if(fault_read)RaiseException(0xe0421111,0,0,nullptr);
    for(std::size_t offset=0;offset<size;++offset) {
        const auto found=memory.find(address+offset);if(found==memory.end())return false;
        static_cast<unsigned char*>(output)[offset]=found->second;
    }
    if(address==mutate_read && ++mutation_reads==1) {
        if(owner_mutation==1)put(machine+0x338,container+8);
        else if(owner_mutation==2)put(container+0x18,d2vr::rig::Identity{3,10});
        else if(mutate_list)put(hud+0x80,d2vr::rig::List{records+0x100,1,1u<<8});
        else put(records+0x3a,static_cast<unsigned char>(8));
    }
    return true;
}
unsigned logic() {return logic_value;}
void reconcile_records() {
    unsigned char flags{};assert(read(records+0x3a,&flags,1));
    if(!(flags&2) && (flags&4))flags=static_cast<unsigned char>((flags&~4u)|8u);
    flags=static_cast<unsigned char>(flags&~2u);put(records+0x3a,flags);
}
void display_pending_record() {
    unsigned char flags{};assert(read(records+0x3a,&flags,1));
    if(flags&1)flags=static_cast<unsigned char>((flags&~1u)|4u);
    put(records+0x3a,flags);
}
void owners() {
    using namespace d2vr;
    memory.clear();mutate_read=0;mutation_reads=0;mutate_list=false;owner_mutation=0;for(unsigned index=0;index<2;++index)put(base+Policy::rvas[index],Policy::prologues[index]);
    put(base+0x39a5290,game_address);put(game_address,base+0x1c940a8);put(game_address+0x3130b0,player_address);put(game_address+0x3130b0+8,victim);
    put(player_address,base+0x1c5e428);put(player_address+0xa8,123u);put(player_address+0x268,rig::List{components_address,2,2u<<8});put(components_address,machine);put(components_address+8,container);
    put(machine,base+0x1c73d58);put(machine+0x10,player_address);put(machine+0x18,rig::Identity{1,10});put(machine+0x90,pool);
    put(container+0x18,rig::Identity{2,10});put(container+0x10,player_address);put(machine+0x338,container);
    put(player_address+0x338,std::uintptr_t{0x3f800000});put(container+0xd8,sensor);
    put(sensor,base+0x1b3e158);put(sensor+0x18,rig::Identity{3,10});put(sensor+0x10,player_address);put(sensor+0x20,machine);put(sensor+0x30,container);
    put(sensor+0x38,static_cast<unsigned char>(1));put(sensor+0x50,target_spawn);put(victim,base+0x1c5e108);put(victim+0xa8,target_spawn);
    put(hud,base+0x1b3a3c8);put(hud+0x40,1);put(hud+0x48,machine);put(hud+0x60,2);put(hud+0x68,container);
    put(hud+0x80,rig::List{records,1,1u<<8});put(records,9);put(records+0x38,static_cast<unsigned char>(1));
    put(records+0x39,static_cast<unsigned char>(0));put(records+0x3a,static_cast<unsigned char>(4));put(base+0x397d3a8,1);
    put(pool+0x158,choke_state);put(choke_state,base+0x1c77c18);put(choke_state+8,0x29u);put(choke_state+0x10,machine);
    put(choke_state+0x20,static_cast<unsigned char>(1));put(choke_state+0xb8,target_spawn);
}
void __fastcall native_hint(std::uintptr_t address,int action,unsigned char normal,unsigned char alternate) {
    assert(address==hud && (action==9 || action==8));++hint_calls;
    put(records,action);put(records+0x38,normal);put(records+0x39,alternate);
    unsigned char flags{};assert(read(records+0x3a,&flags,1));put(records+0x3a,static_cast<unsigned char>(flags|2));
    if(mode==6)active->clear();
    if(mode==7)throw std::runtime_error("native hint");
    if(mode==8)RaiseException(0xe0422222,0,0,nullptr);
    if(mode==9)put(victim+0xa8,target_spawn+1);
}
void __fastcall native_update(std::uintptr_t address) {
    assert(address==hud);++update_calls;
    if(mode==1)return;
    active->invoke_hint(base+(mode==2?0x3913e4:0x3913e3),hud,mode==3?8:9,mode==4?0:1,0);
    if(mode==5) {const auto previous=mode;mode=1;active->invoke_update(hud);mode=previous;}
}
bool seh_update() {__try {active->invoke_update(hud);return false;}__except(EXCEPTION_EXECUTE_HANDLER) {return GetExceptionCode()==0xe0422222;}}
}
namespace d2vr {
struct NativeOfferedChokeFixture {
    static NativeOfferedChoke& make() {
        auto* owner=new NativeOfferedChoke;owner->base_=base;owner->read_=&read;owner->logic_=&logic;owner->stopping_=false;
        owner->installed_[0]=true;owner->installed_[1]=true;owner->originals_[0]=reinterpret_cast<void*>(&native_update);owner->originals_[1]=reinterpret_cast<void*>(&native_hint);
        owner->created_[0]=true;owner->created_[1]=true;
        return *owner;
    }
    static bool clean() {return !NativeOfferedChoke::frame_ && !NativeOfferedChoke::depth_ && !active->in_flight();}
    static std::optional<NativeOfferedChoke::Receipt> receipt() {return active->receipt_;}
    static bool qualify(NativeOfferedChoke::QualifyPhase phase) {return active->qualify(hud,phase).has_value();}
    static void restart() {active->clear();active->installed_[0]=active->installed_[1]=true;active->stop_complete_=false;active->stopping_=false;}
    static void preflight() {auto* owner=new NativeOfferedChoke;assert(!owner->install(base,read,logic) && owner->stopped() && owner->ready_for_install());}
    static void exhaustion() {auto& owner=make();owner.epoch_=UINT64_MAX;owner.clear();assert(!owner.enabled() && !owner.ready_for_install());}
    static void unwind() {
        auto& owner=make();
        for(unsigned index=0;index<2;++index) {
            auto* code=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));assert(code);
            std::memcpy(code,NativeOfferedChoke::prologues[index].data(),5);
            const auto jump=NativeOfferedChoke::tail(base+NativeOfferedChoke::rvas[index]+5,false);
            const auto relay=NativeOfferedChoke::tail(reinterpret_cast<std::uintptr_t>(NativeOfferedChoke::hooks()[index]),false);
            std::memcpy(code+5,jump.data(),14);std::memcpy(code+19,relay.data(),14);
            DWORD previous{};assert(VirtualProtect(code,4096,PAGE_EXECUTE_READ,&previous));owner.originals_[index]=code;
            const bool prepared=owner.prepare(index);
            if(!prepared) {std::cerr<<"prepare failed index="<<index<<" code="<<static_cast<void*>(code)<<" meta="<<owner.metadata_[index]<<" registered="<<owner.registered_[index]<<" patched="<<owner.patched_[index]<<" error="<<GetLastError()<<std::endl;}
            assert(prepared);assert(owner.prepare(index));
            for(const unsigned offset:{0u,3u,5u,15u,18u,19u}) {
                alignas(16) std::array<std::uintptr_t,48> stack{};
                CONTEXT context{};context.ContextFlags=CONTEXT_FULL;
                const auto stack_base=reinterpret_cast<std::uintptr_t>(stack.data()+16);
                stack[16]=0x12345678;stack[18]=0x11223344;
                context.Rip=reinterpret_cast<DWORD64>(code+offset);context.Rsp=stack_base;
                context.Rbx=0x11223344;context.R14=0x11223344;
                if(offset>=5 && offset<19 && index==1) {context.Rsp-=8;*reinterpret_cast<std::uintptr_t*>(context.Rsp)=0x11223344;context.R14=0xdeadbeef;}
                DWORD64 image{};const auto function=RtlLookupFunctionEntry(context.Rip,&image,nullptr);assert(function);
                PVOID handler{};DWORD64 establisher{};
                RtlVirtualUnwind(UNW_FLAG_NHANDLER,image,context.Rip,function,&context,&handler,&establisher,nullptr);
                assert(context.Rip==0x12345678 && context.Rsp==stack_base+8 && context.Rbx==0x11223344 && context.R14==0x11223344);
            }
            owner.cache_pending_[index]=true;owner.stop_complete_=true;assert(!owner.stopped());assert(owner.protect(index));
        }
    }
};
}
int main() {
    using Fixture=d2vr::NativeOfferedChokeFixture;
    active=&Fixture::make();owners();active->invoke_update(hud);
    const auto offered=active->offered(GetTickCount64());assert(offered && offered->spawn==target_spawn && offered->victim==victim && update_calls==1 && hint_calls==1);assert(Fixture::clean());
    assert(!active->offered(offered->tick-1) && !active->offered(offered->tick+101));
    assert(Policy::active_victim(read,base,machine)==target_spawn);
    for(unsigned variant=0;variant<6;++variant) {
        owners();
        if(variant==0)put(choke_state,base+0x1c77c19);if(variant==1)put(choke_state+8,0x28u);
        if(variant==2)put(choke_state+0x20,static_cast<unsigned char>(0));if(variant==3)put(choke_state+0x10,machine+8);
        if(variant==4)put(victim+0xa8,target_spawn+1);if(variant==5)put(choke_state+0xb8,0x3fff0010u);
        assert(!Policy::active_victim(read,base,machine));
    }
    for(mode=1;mode<=9;++mode) {
        owners();const auto before_update=update_calls,before_hint=hint_calls;
        if(mode==7) {try {active->invoke_update(hud);assert(false);}catch(const std::runtime_error&) {}}
        else if(mode==8)assert(seh_update());else active->invoke_update(hud);
        assert(!active->offered(GetTickCount64()) && Fixture::clean());
        assert(update_calls==before_update+(mode==5?2:1));assert(hint_calls==before_hint+(mode==1?0:1));
    }
    mode=0;
    for(unsigned variant=0;variant<8;++variant) {
        owners();
        if(variant==0)put(base+0x397d3a8,0);if(variant==1)put(records+0x3a,static_cast<unsigned char>(0));
        if(variant==2)put(hud+0x40,7);if(variant==3)put(hud+0x60,7);if(variant==4)put(machine+0x338,container+8);
        if(variant==5)put(sensor+0x20,machine+8);if(variant==6)put(sensor+0x38,static_cast<unsigned char>(0));if(variant==7)logic_value=0;
        active->invoke_update(hud);assert(!active->offered(GetTickCount64()) && Fixture::clean());logic_value=2;
    }
    owners();active->invoke_update(hud);const auto current=active->offered(GetTickCount64());assert(current);
    put(base+0x397d3a8,0);assert(!active->current(*current,GetTickCount64()));put(base+0x397d3a8,1);
    throw_read=true;assert(!active->offered(GetTickCount64()));throw_read=false;
    fault_read=true;assert(!active->offered(GetTickCount64()));fault_read=false;
    put(sensor+0x18,d2vr::rig::Identity{4,10});assert(!active->current(*current,GetTickCount64()));
    owners();active->clear();assert(!active->current(*current,GetTickCount64()) && !active->offered(GetTickCount64()));
    active->invoke_update(hud);assert(active->offered(GetTickCount64()));assert(active->disable() && active->stopped() && !active->offered(GetTickCount64()));
    Fixture::restart();owners();mode=0;
    put(records+0x3a,static_cast<unsigned char>(1));
    active->invoke_update(hud);
    const auto pending=Fixture::receipt();assert(pending);
    assert(!active->offered(pending->tick));
    reconcile_records();
    assert(!active->current(*pending,pending->tick));
    display_pending_record();
    assert(active->current(*pending,pending->tick) && active->offered(pending->tick));
    assert(active->current(*pending,pending->tick+100) && !active->current(*pending,pending->tick+101));
    reconcile_records();
    assert(!active->current(*pending,pending->tick));
    put(records+0x3a,static_cast<unsigned char>(12));
    assert(!active->current(*pending,pending->tick));
    put(records+0x3a,static_cast<unsigned char>(4));
    mode=1;active->invoke_update(hud);assert(!active->offered(GetTickCount64()));mode=0;
    owners();active->invoke_update(hud);const auto shown=active->offered(GetTickCount64());assert(shown);
    put(sensor+0x50,target_spawn+1);put(victim+0xa8,target_spawn+1);
    assert(!active->current(*shown,shown->tick));
    put(sensor+0x50,0x3fff0010u);assert(!active->current(*shown,shown->tick));
    owners();active->invoke_update(hud);const auto stable=active->offered(GetTickCount64());assert(stable);
    put(hud+0x80,d2vr::rig::List{records,2,2u<<8});put(records+0x40,9);
    put(records+0x78,static_cast<unsigned char>(1));put(records+0x79,static_cast<unsigned char>(0));put(records+0x7a,static_cast<unsigned char>(4));
    assert(!active->current(*stable,stable->tick));
    active->invoke_update(hud);assert(!active->offered(GetTickCount64()));
    owners();active->invoke_update(hud);const auto raced=active->offered(GetTickCount64());assert(raced);
    mutate_read=records+0x3a;assert(!active->current(*raced,raced->tick));mutate_read=0;
    owners();active->invoke_update(hud);const auto header_raced=active->offered(GetTickCount64());assert(header_raced);
    mutate_read=records+0x3a;mutate_list=true;assert(!active->current(*header_raced,header_raced->tick));mutate_read=0;
    owners();active->invoke_update(hud);const auto owner_raced=active->offered(GetTickCount64());assert(owner_raced);
    mutate_read=sensor+0x50;assert(!active->current(*owner_raced,owner_raced->tick));mutate_read=0;
    owners();active->invoke_update(hud);const auto before_stop=active->offered(GetTickCount64());assert(before_stop);
    assert(active->disable() && !active->current(*before_stop,before_stop->tick));
    Fixture::restart();assert(!active->current(*before_stop,before_stop->tick));
    active->invoke_update(hud);assert(active->offered(GetTickCount64()));
    const auto diagnostics=active->diagnostics();
    assert(diagnostics.updates && diagnostics.hints && diagnostics.exact_candidates && diagnostics.published);
    assert(diagnostics.rejected[static_cast<unsigned>(Policy::Rejection::expired)] &&
        diagnostics.rejected[static_cast<unsigned>(Policy::Rejection::record)] &&
        diagnostics.rejected[static_cast<unsigned>(Policy::Rejection::owner)] &&
        diagnostics.rejected[static_cast<unsigned>(Policy::Rejection::no_hint)]);
    owners();active->invoke_update(hud);const auto correct_receiver=active->offered(GetTickCount64());assert(correct_receiver);
    put(player_address+0x338,std::uintptr_t{0x40000000});assert(active->current(*correct_receiver,GetTickCount64()));
    owners();put(player_address+0x338,container);put(machine+0x338,container+8);
    active->invoke_update(hud);assert(!active->offered(GetTickCount64()) && Fixture::clean());
    for(unsigned change=1;change<=2;++change) {
        owners();active->invoke_update(hud);const auto receiver_raced=active->offered(GetTickCount64());assert(receiver_raced);
        owner_mutation=change;mutate_read=change==1?machine+0x338:container+0x18;
        assert(!active->current(*receiver_raced,GetTickCount64()) && Fixture::clean());
        owners();owner_mutation=change;mutate_read=change==1?machine+0x338:container+0x18;
        active->invoke_update(hud);assert(!active->offered(GetTickCount64()) && Fixture::clean());
    }
    for(unsigned phase=0;phase<static_cast<unsigned>(Policy::QualifyPhase::count);++phase)
        for(unsigned stage=0;stage<static_cast<unsigned>(Policy::QualifyStage::count);++stage) {
            owners();const auto before=active->diagnostics().qualify_rejected;
            if(stage==0)put(hud,base+0x1b3a3c9);
            if(stage==1)put(hud+0x40,7);
            if(stage==2)put(machine+0x338,container+8);
            if(stage==3)put(sensor+0x20,machine+8);
            if(stage==4)put(victim+0xa8,target_spawn+1);
            if(stage==5) {owner_mutation=1;mutate_read=machine+0x338;}
            assert(!Fixture::qualify(static_cast<Policy::QualifyPhase>(phase)));
            const auto after=active->diagnostics().qualify_rejected;
            for(unsigned observed_phase=0;observed_phase<after.size();++observed_phase)
                for(unsigned observed_stage=0;observed_stage<after[observed_phase].size();++observed_stage)
                    assert(after[observed_phase][observed_stage]==before[observed_phase][observed_stage]+
                        (observed_phase==phase && observed_stage==stage?1u:0u));
        }
    assert(!Policy::verify(base,read));Fixture::preflight();Fixture::unwind();Fixture::exhaustion();
    std::cout<<"PASS native offered Choke producer semantics/current victim/nested/C++/SEH/stop (synthetic native callbacks, not installed hooks)\n";
}
