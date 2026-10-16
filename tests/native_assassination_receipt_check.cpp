#define NOMINMAX
#include "../src/native_assassination_receipt.hpp"
#include <cassert>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

using Hooks=d2vr::NativeAssassinationReceiptHooks;
using Receipt=d2vr::NativeAssassinationReceipt;
namespace {
Hooks* active_hooks{};
unsigned mode{},before_calls{},after_calls{};
std::array<unsigned,6> native_calls{};
std::atomic<bool> parked{},release_native{};
bool completed_after{},current_after{},result_after{};
bool create_enabled{},disable_failure{};
unsigned read_fault{};
std::filesystem::path fixture_package;
const void* expected_packet{};
void observe(void*,unsigned phase,unsigned,std::uintptr_t,std::uintptr_t,int,const void* packet,bool completed,bool current,bool result) {
    assert(!packet || packet==expected_packet);
    if(!phase) {++before_calls;if(mode==3)throw std::runtime_error("observer");if(mode==4)RaiseException(0xe0425531,0,0,nullptr);}
    else {++after_calls;completed_after=completed;current_after=current;result_after=result;}
}
void __fastcall update(std::uintptr_t subject) {
    assert(subject==0x10000);++native_calls[0];
    if(mode==1)throw std::runtime_error("native");
    if(mode==2)RaiseException(0xe0425532,0,0,nullptr);
    if(mode==5)active_hooks->invoke(1,0x123,0x20000,13,expected_packet);
    if(mode==6)assert(!active_hooks->disable(0));
    if(mode==7){parked=true;while(!release_native.load())Sleep(1);}
}
bool __fastcall accept13(std::uintptr_t subject,const void* packet) {assert(subject==0x20000 && packet==expected_packet);++native_calls[1];return true;}
bool __fastcall accept19(std::uintptr_t subject,const void* packet) {assert(subject==0x60000 && packet==expected_packet);++native_calls[5];return false;}
void __fastcall assassination(std::uintptr_t subject,const void* packet) {assert(subject==0x30000 && packet==expected_packet);++native_calls[2];}
void __fastcall drop(std::uintptr_t subject,const void* packet) {assert(subject==0x40000 && packet==expected_packet);++native_calls[3];}
bool __fastcall arbitration(std::uintptr_t subject,bool special) {assert(subject==0x50000);++native_calls[4];return special;}
bool native_seh(Hooks& hooks) {
    __try {hooks.invoke(0,0,0x10000,0,nullptr);return false;}
    __except(EXCEPTION_EXECUTE_HANDLER) {return GetExceptionCode()==0xe0425532;}
}
std::vector<unsigned char> memory(0x4000000);
constexpr std::uintptr_t base=0x140000000,fixture_game=base+0x10000,fixture_player=base+0x3400000,machine=base+0x3500000;
constexpr std::uintptr_t fixture_components=base+0x3600000,pool=base+0x3610000,state13=base+0x3620000,state19=base+0x3630000;
constexpr std::uintptr_t sensor=base+0x3640000,enemy=base+0x3650000,packets=base+0x3660000,slash=base+0x3670000;
constexpr std::uint32_t victim_spawn=0x20001;
template<class Value>void put(std::uintptr_t address,Value value) {assert(address>=base && address-base+sizeof(value)<=memory.size());std::memcpy(memory.data()+address-base,&value,sizeof(value));}
bool read(std::uintptr_t address,void* output,std::size_t size) {
    if(read_fault==1)throw std::runtime_error("recorder read fault");
    if(read_fault==2)RaiseException(0xe0425533,0,0,nullptr);
    if(address<base || address-base>memory.size() || size>memory.size()-(address-base))return false;
    std::memcpy(output,memory.data()+address-base,size);return true;
}
void seed() {
    std::fill(memory.begin(),memory.end(),static_cast<unsigned char>(0));
    put(base+0x39a5290,fixture_game);put(fixture_game,base+0x1c940a8);put(fixture_game+0x3130b0,fixture_player);put(fixture_player,base+0x1c5e428);
    put(fixture_player+0xa8,std::uint32_t{0x10001});put(fixture_player+0x268,d2vr::rig::List{fixture_components,2,2u<<8});put(fixture_components,machine);put(fixture_components+8,sensor);
    put(machine,base+0x1c73d58);put(machine+0x10,fixture_player);put(machine+0x18,d2vr::rig::Identity{4,0xa});put(machine+0x90,pool);
    for(const auto state:{13u,19u,39u}) {
        const auto address=state==13?state13:state==19?state19:slash;
        put(pool+0x10+state*8ull,address);put(address,base+(state==13?0x1c83468u:state==19?0x1c79478u:0x1c85d28u));
        put(address+8,state);put(address+0x10,machine);
    }
    put(state13+0xc4,std::uint32_t{0x3fff0010});put(state19+0x124,std::uint32_t{0x3fff0010});
    put(state13+0x30,d2vr::rig::List{packets,0,4u<<8});put(state19+0xb0,d2vr::rig::List{packets+0x1000,0,4u<<8});
    put(sensor,base+0x1b3f998);put(sensor+0x10,fixture_player);put(sensor+0x18,d2vr::rig::Identity{8,0xa});put(sensor+0x20,machine);put(sensor+0x38,std::uint8_t{1});
    put(sensor+0x1d0,std::uintptr_t{0x77770000});put(sensor+0x1d8,std::uint32_t{2});put(sensor+0x1dc,std::uint8_t{1});put(sensor+0x1a8+2*8,std::uintptr_t{0x77770000});
    put(sensor+0x78,base+0x1b3f978);put(sensor+0x80,sensor);put(sensor+0x98,victim_spawn);put(slash+0x88,sensor);
    put(fixture_game+0x3130b0+(victim_spawn>>16)*8ull,enemy);put(enemy+0xa8,victim_spawn);
}
bool __fastcall receipt_accept(std::uintptr_t subject,const void* packet) {
    assert(subject==state13 && reinterpret_cast<std::uintptr_t>(packet)==packets);
    put(state13+0x38,std::uint32_t{1});return true;
}
void __fastcall receipt_enter(std::uintptr_t subject,const void* packet) {
    assert(subject==state13 && reinterpret_cast<std::uintptr_t>(packet)==packets);
    put(state13+0x20,std::uint8_t{1});put(state13+0xc4,victim_spawn);
}
}
extern "C" MH_STATUS WINAPI MH_CreateHook(LPVOID target,LPVOID detour,LPVOID* original) {
    if(!create_enabled)return MH_ERROR_UNSUPPORTED_FUNCTION;
    unsigned selected=6;
    for(unsigned index=0;index<6;++index)if(reinterpret_cast<std::uintptr_t>(target)==base+Hooks::rvas[index])selected=index;
    assert(selected<6);const auto prefix=Hooks::prefixes[selected];
    auto* code=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));assert(code);
    assert(read(reinterpret_cast<std::uintptr_t>(target),code,prefix));
    code[prefix]=code[prefix+14]=0xff;code[prefix+1]=code[prefix+15]=0x25;
    const auto continuation=reinterpret_cast<std::uintptr_t>(target)+prefix;
    std::memcpy(code+prefix+6,&continuation,8);std::memcpy(code+prefix+20,&detour,8);*original=code;return MH_OK;
}
extern "C" MH_STATUS WINAPI MH_EnableHook(LPVOID) {return MH_OK;}
extern "C" MH_STATUS WINAPI MH_DisableHook(LPVOID) {return disable_failure?MH_ERROR_MEMORY_PROTECT:MH_OK;}
namespace d2vr {
struct NativeAssassinationReceiptHooksFixture {
    static void recorder_fault_call(Hooks& hooks) {
        const auto original=hooks.originals_[1].load();hooks.originals_[1]=reinterpret_cast<void*>(&receipt_accept);
        hooks.invoke(1,base+0x80251c,state13,13,reinterpret_cast<const void*>(packets));hooks.originals_[1]=original;
    }
    static void receipt_callbacks(Hooks& hooks,Hooks::Callbacks callbacks) {
        hooks.callbacks_=callbacks;hooks.originals_[1]=reinterpret_cast<void*>(&receipt_accept);hooks.originals_[2]=reinterpret_cast<void*>(&receipt_enter);
    }
    static void unwind() {
        auto& hooks=*new Hooks;hooks.base_=base;
        for(unsigned index=0;index<6;++index) {
            const auto prefix=Hooks::prefixes[index];
            auto* code=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));assert(code);
            std::memcpy(code,Hooks::prologues[index].data(),prefix);
            const auto continuation=Hooks::tail(base+Hooks::rvas[index]+prefix,false);
            const auto relay=Hooks::tail(reinterpret_cast<std::uintptr_t>(Hooks::hooks()[index]),false);
            std::memcpy(code+prefix,continuation.data(),14);std::memcpy(code+prefix+14,relay.data(),14);
            hooks.originals_[index]=code;assert(hooks.prepare(index));
            for(unsigned offset=0;offset<prefix+28;++offset) {
                DWORD64 image{};assert(RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(code)+offset,&image,nullptr));
            }
            std::vector<unsigned> boundaries{0,prefix,prefix+10,prefix+13,prefix+14};
            if(index==0)boundaries.push_back(2);
            if(index==2){boundaries.push_back(2);boundaries.push_back(3);boundaries.push_back(4);}
            if(index==4 || index==1 || index==5)boundaries.push_back(4);
            for(const auto offset:boundaries) {
                std::array<std::uintptr_t,128> stack{};const auto entry=reinterpret_cast<std::uintptr_t>(stack.data()+64);
                stack[64]=0x12345678;CONTEXT context{};context.ContextFlags=CONTEXT_FULL;
                context.Rip=reinterpret_cast<DWORD64>(code)+offset;context.Rsp=entry;
                context.Rbx=0x11112222;context.Rdi=0x22223333;context.Rbp=0x33334444;context.Rsi=0x44445555;context.R12=0x55556666;
                const auto push=[&](std::uintptr_t reg){context.Rsp-=8;*reinterpret_cast<std::uintptr_t*>(context.Rsp)=reg;};
                if(offset<prefix+14) {
                    if(index==0){if(offset>=2)push(context.Rdi);if(offset>=6)context.Rsp-=64;}
                    if((index==1 || index==5) && offset>=4)context.Rsp-=40;
                    if(index==2){if(offset>=2)push(context.Rbp);if(offset>=3)push(context.Rbx);if(offset>=4)push(context.Rsi);if(offset>=6)push(context.R12);}
                    if(index==3 && offset>=5)*reinterpret_cast<std::uintptr_t*>(entry+24)=context.Rbx;
                    if(index==4 && offset>=5)push(context.Rbp);
                }
                DWORD64 image{},establisher{};PVOID handler{};
                const auto function=RtlLookupFunctionEntry(context.Rip,&image,nullptr);assert(function);
                RtlVirtualUnwind(UNW_FLAG_NHANDLER,image,context.Rip,function,&context,&handler,&establisher,nullptr);
                assert(context.Rip==0x12345678 && context.Rsp==entry+8 && context.Rbx==0x11112222 && context.Rdi==0x22223333 &&
                    context.Rbp==0x33334444 && context.Rsi==0x44445555 && context.R12==0x55556666);
            }
        }
    }
    static Hooks& make() {
        auto& hooks=*new Hooks;hooks.callbacks_={nullptr,&observe};hooks.stopping_=false;hooks.stop_complete_=false;
        hooks.originals_[0]=reinterpret_cast<void*>(&update);hooks.originals_[1]=reinterpret_cast<void*>(&accept13);
        hooks.originals_[2]=reinterpret_cast<void*>(&assassination);hooks.originals_[3]=reinterpret_cast<void*>(&drop);hooks.originals_[4]=reinterpret_cast<void*>(&arbitration);hooks.originals_[5]=reinterpret_cast<void*>(&accept19);
        for(unsigned index=0;index<6;++index){hooks.created_[index]=true;hooks.installed_[index]=true;}hooks.enabled(true);return hooks;
    }
    static bool clean(Hooks& hooks){return !Hooks::depth_ && !hooks.in_flight();}
};
struct NativeAssassinationReceiptFixture {
    static void run() {
        auto& receipt=Receipt::process_owner();receipt.base_=base;receipt.read_=read;receipt.active_=true;receipt.activation_=1;
        Packet13(receipt);
        Packet19(receipt);
        seed();put(packets,std::uint32_t{29});put(packets+4,std::uint32_t{13});put(packets+8,victim_spawn);
        auto& routed=NativeAssassinationReceiptHooksFixture::make();
        NativeAssassinationReceiptHooksFixture::receipt_callbacks(routed,{&receipt,&Receipt::observe});
        routed.invoke(1,base+0x80251c,state13,13,reinterpret_cast<const void*>(packets));
        routed.invoke(2,base+0x8052a8,state13,13,reinterpret_cast<const void*>(packets));
        assert(receipt.count_==2 && receipt.rows_[0].accepted_copy && receipt.rows_[1].active_victim_match &&
            receipt.rows_[0].packet.serial==receipt.rows_[1].packet.serial && NativeAssassinationReceiptHooksFixture::clean(routed));
        assert(routed.disable());receipt.head_=receipt.count_=0;
        const auto offer=receipt.capture(0,sensor,0,{});assert(offer.owner_valid && offer.offer_valid && offer.victim==victim_spawn);
        const auto arbitration_offer=receipt.capture(4,slash,0,{});assert(arbitration_offer.owner_valid && arbitration_offer.offer_valid && arbitration_offer.sensor==sensor);
        put(machine+0x18,rig::Identity{5,0xa});const auto changed=receipt.capture(0,sensor,0,{});assert(changed.owner_valid && !(changed.owner==offer.owner));
        put(sensor+0x20,std::uintptr_t{});assert(!receipt.capture(0,sensor,0,{}).owner_valid);seed();
        receipt.begin(4,base+0x86c630,slash,0,nullptr);const auto parent=pending_id();
        receipt.begin(1,base+0x80251c,state13,13,reinterpret_cast<const void*>(packets));
        assert(pending_id()!=parent && Receipt::pending_[Receipt::depth_-1].row.parent==parent);
        receipt.end(1,state13,true,true,true);receipt.end(4,slash,true,true,true);assert(!Receipt::depth_);
        for(unsigned index=0;index<65;++index)receipt.begin(4,0,slash,0,nullptr);
        for(unsigned index=0;index<65;++index)receipt.end(4,slash,true,true,true);
        assert(!Receipt::depth_ && receipt.overflow_==1);
        receipt.head_=receipt.count_=0;for(unsigned index=0;index<257;++index)receipt.enqueue({});
        assert(receipt.count_==256 && receipt.dropped_==1);
        receipt.head_=receipt.count_=0;
        receipt.begin(2,0,state13,13,reinterpret_cast<const void*>(packets));put(machine+0x18,rig::Identity{9,0xa});receipt.end(2,state13,true,true,false);
        assert(!receipt.rows_[0].same_owner && !receipt.rows_[0].active_victim_match);seed();receipt.head_=receipt.count_=0;
        receipt.begin(2,0,state13,13,reinterpret_cast<const void*>(packets));receipt.end(2,state13,true,false,false);
        assert(!receipt.rows_[0].current && !receipt.rows_[0].active_victim_match);
        receipt.head_=receipt.count_=0;receipt.active_=false;
        lifecycle(receipt);
    }
    static void lifecycle(Receipt& receipt) {
        if(fixture_package.empty())return;
        const auto& folder=fixture_package;
        const std::array<std::filesystem::path,6> files{
            folder.parent_path()/L"abi-peer/function-003b1700.bin",folder.parent_path()/L"victim-receipt/function-008636a0.bin",
            folder.parent_path()/L"victim-receipt/function-00860eb0.bin",folder.parent_path()/L"victim-receipt/function-00832d20.bin",
            folder.parent_path()/L"abi-peer/function-0086b050.bin",folder.parent_path()/L"victim-receipt/function-00833940.bin"};
        for(unsigned index=0;index<6;++index) {
            std::ifstream input(files[index],std::ios::binary);assert(input);
            const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(input)),{});assert(bytes.size()==Hooks::sizes[index]);
            std::memcpy(memory.data()+Hooks::rvas[index],bytes.data(),bytes.size());
        }
        assert(Hooks::verify(base,read));
        assert(!receipt.start(base,read,folder/L"checks") && !receipt.output_.is_open() && Hooks::process_owner().stopped());
        create_enabled=true;
        assert(receipt.start(base,read,folder/L"checks") && receipt.enabled());const auto activation=receipt.activation_;
        receipt.enqueue({});disable_failure=true;
        assert(!receipt.stop() && receipt.output_.is_open() && !receipt.enabled() && receipt.count_==1);
        assert(!receipt.start(base,read,folder/L"checks") && receipt.output_.is_open() && receipt.activation_==activation);
        disable_failure=false;
        assert(receipt.stop() && !receipt.output_.is_open() && !receipt.count_ && Hooks::process_owner().stopped());
        assert(receipt.start(base,read,folder/L"checks") && receipt.activation_==activation+1 && !receipt.failed());
        assert(receipt.stop() && Hooks::process_owner().stopped());
        for(const auto fault:{1u,2u}) {
            assert(receipt.start(base,read,folder/L"checks") && receipt.enabled() && !receipt.failed());
            read_fault=fault;NativeAssassinationReceiptHooksFixture::recorder_fault_call(Hooks::process_owner());read_fault=0;
            assert(!receipt.enabled() && receipt.failed() && Hooks::process_owner().observer_failed() &&
                NativeAssassinationReceiptHooksFixture::clean(Hooks::process_owner()) && !Receipt::depth_);
            assert(receipt.stop() && !receipt.enabled() && receipt.failed());
            assert(receipt.start(base,read,folder/L"checks") && receipt.enabled() && !receipt.failed());
            assert(receipt.stop() && !receipt.enabled() && !receipt.failed());
        }
        memory[Hooks::rvas[0]+16]^=1;assert(!Hooks::verify(base,read));memory[Hooks::rvas[0]+16]^=1;
    }
    static std::uint64_t pending_id(){return Receipt::pending_[Receipt::depth_-1].row.id;}
    static void Packet13(Receipt& receipt) {
        put(packets,std::uint32_t{27});put(packets+4,std::uint32_t{13});put(packets+8,victim_spawn);put(packets+0x30,std::uint32_t{2});
        put(packets+0x20,std::uint64_t{0x65536e6f00002b0f});
        std::array<unsigned char,56> native_source{};assert(read(packets,native_source.data(),native_source.size()));
        const auto source_address=packets+0x2000;put(source_address,native_source);put(source_address+0x20,std::uint64_t{0x0000000000002b0f});
        const auto source_packet=receipt.packet(source_address,13);const auto stored_packet=receipt.packet(packets,13);
        assert(source_packet.requester==0x2b0fu && source_packet==stored_packet);
        put(state13+0x38,std::uint32_t{1});Receipt::Snapshot copied{};copied.state=state13;bool found{};
        assert(receipt.present(copied,source_packet,found) && found);
        put(packets+0x20,std::uint64_t{0x65536e6f00002b10});
        assert(!(source_packet==receipt.packet(packets,13)) && !receipt.present(copied,source_packet,found));
        put(packets+0x20,std::uint64_t{0x65536e6f00002b0f});put(state13+0x38,std::uint32_t{0});
        const auto packet=receipt.packet(packets,13);assert(packet.valid && packet.victim==victim_spawn);
        receipt.begin(1,base+0x80251c,state13,13,reinterpret_cast<const void*>(packets));put(state13+0x38,std::uint32_t{1});receipt.end(1,state13,true,true,true);
        assert(receipt.rows_[0].accepted_copy && !receipt.rows_[0].active_victim_match);receipt.head_=receipt.count_=0;
        receipt.begin(2,0,state13,13,reinterpret_cast<const void*>(packets));put(state13+0x20,std::uint8_t{1});put(state13+0xc4,victim_spawn);receipt.end(2,state13,true,true,false);
        assert(receipt.rows_[0].active_victim_match);receipt.head_=receipt.count_=0;
        put(enemy+0xa8,victim_spawn+1);assert(!receipt.capture(2,state13,13,packet).victim_valid);put(enemy+0xa8,victim_spawn);
        put(pool+0x10+13*8,std::uintptr_t{});assert(!receipt.capture(2,state13,13,packet).state_valid);put(pool+0x10+13*8,state13);
    }
    static void Packet19(Receipt& receipt) {
        const auto address=packets+0x1000;put(address,std::uint32_t{28});put(address+4,std::uint32_t{19});put(address+8,std::uint32_t{1});put(address+12,victim_spawn);
        const auto packet=receipt.packet(address,19);assert(packet.valid && packet.mode==1 && packet.victim==victim_spawn);
        receipt.begin(3,0,state19,19,reinterpret_cast<const void*>(address));put(state19+0x20,std::uint8_t{1});put(state19+0x120,std::uint32_t{1});put(state19+0x124,victim_spawn);receipt.end(3,state19,true,true,false);
        assert(receipt.rows_[0].active_victim_match && receipt.rows_[0].after.mode==1);receipt.head_=receipt.count_=0;
        put(state19+0x124,victim_spawn+1);assert(!receipt.capture(3,state19,19,packet).victim_valid);put(state19+0x124,victim_spawn);
    }
};
}
int main(int argc,char** argv) {
    assert(argc<=2);if(argc==2)fixture_package=argv[1];
    using Fixture=d2vr::NativeAssassinationReceiptHooksFixture;
    Fixture::unwind();
    std::array<unsigned char,56> packet{};expected_packet=packet.data();
    auto& hooks=Fixture::make();active_hooks=&hooks;
    hooks.invoke(0,0,0x10000,0,nullptr);hooks.invoke(1,0,0x20000,13,expected_packet);hooks.invoke(2,0,0x30000,13,expected_packet);hooks.invoke(3,0,0x40000,19,expected_packet);
    assert(hooks.invoke(4,0,0x50000,1,nullptr) && result_after);assert(!hooks.invoke(4,0,0x50000,0,nullptr) && !result_after);
    assert(!hooks.invoke(5,0,0x60000,19,expected_packet));
    assert((native_calls==std::array<unsigned,6>{1,1,1,1,2,1}) && before_calls==7 && after_calls==7 && Fixture::clean(hooks));
    mode=1;try{hooks.invoke(0,0,0x10000,0,nullptr);assert(false);}catch(const std::runtime_error&){}assert(!completed_after && Fixture::clean(hooks));
    mode=2;assert(native_seh(hooks) && !completed_after && Fixture::clean(hooks));
    mode=5;hooks.invoke(0,0,0x10000,0,nullptr);assert(Fixture::clean(hooks));
    for(const auto failure:{3u,4u}) {auto& failing=Fixture::make();active_hooks=&failing;mode=failure;const auto previous=native_calls[0];failing.invoke(0,0,0x10000,0,nullptr);assert(native_calls[0]==previous+1 && !current_after && Fixture::clean(failing));assert(failing.disable());}
    active_hooks=&hooks;mode=6;hooks.invoke(0,0,0x10000,0,nullptr);assert(!current_after && hooks.disable() && hooks.stopped());
    auto& retained=Fixture::make();active_hooks=&retained;mode=7;
    std::thread worker([&]{retained.invoke(0,0,0x10000,0,nullptr);});while(!parked.load())Sleep(1);
    assert(!retained.disable(0) && !retained.stopped());release_native=true;worker.join();assert(retained.disable() && retained.stopped() && Fixture::clean(retained));
    seed();d2vr::NativeAssassinationReceiptFixture::run();
    std::cout<<"passive assassination original-once/lifetime/ownership/packet/overflow checks passed\n";
}
