#define NOMINMAX
#include "../src/native_interaction_aim.hpp"
#include "../src/vr_controls.hpp"
#include "../src/vr_combat.hpp"
#include "../src/play_controls.hpp"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <thread>

namespace {
constexpr std::uintptr_t fixture_image_base=0x140000000,fixture_game=0x200000000,fixture_player=0x201000000,
    fixture_camera=0x202000000,fixture_body=0x203000000,fixture_controller=0x204000000,fixture_physics=0x205000000,
    fixture_delegate=0x206000000,fixture_sensor=0x207000000;
std::map<std::uintptr_t,unsigned char> fixture_memory;
std::map<std::uintptr_t,std::array<unsigned char,32>> fixture_originals;
alignas(16) unsigned char choke_decl[0xa0]{};
alignas(16) unsigned char block_decl[0x110]{},slash_entries[0x28]{};
unsigned hook_mutations{},native_calls{};
std::uintptr_t fail_enable_address{};
bool raise_native_seh{};std::uint64_t failed_prompt_ticket{};
void* slash_trampoline{};
std::array<void*,2> choke_trampolines{};
std::array<void*,2> offered_trampolines{};
template<class Value>void put(std::uintptr_t address,const Value& value) {
    const auto* bytes=reinterpret_cast<const unsigned char*>(&value);
    for(std::size_t offset=0;offset<sizeof(value);++offset)fixture_memory[address+offset]=bytes[offset];
}
bool read_memory(std::uintptr_t address,void* output,std::size_t size) {
    const auto trampoline=reinterpret_cast<std::uintptr_t>(slash_trampoline);
    if(trampoline && address>=trampoline && address-trampoline<=22 && size<=22-(address-trampoline)) {
        std::memcpy(output,reinterpret_cast<const void*>(address),size);return true;
    }
    for(const auto& region:std::array<std::pair<std::uintptr_t,std::size_t>,3>{{
        {reinterpret_cast<std::uintptr_t>(choke_decl),sizeof(choke_decl)},
        {reinterpret_cast<std::uintptr_t>(block_decl),sizeof(block_decl)},
        {reinterpret_cast<std::uintptr_t>(slash_entries),sizeof(slash_entries)}}}) {
        if(address>=region.first && address-region.first<=region.second && size<=region.second-(address-region.first)) {
            std::memcpy(output,reinterpret_cast<const void*>(address),size);return true;
        }
    }
    for(std::size_t offset=0;offset<size;++offset) {
        const auto found=fixture_memory.find(address+offset);if(found==fixture_memory.end())return false;
        static_cast<unsigned char*>(output)[offset]=found->second;
    }
    return true;
}
UINT logic(){return 2;}
void __fastcall native_stage(void*){++native_calls;}
}
extern "C" MH_STATUS WINAPI MH_CreateHook(LPVOID target,LPVOID hook,LPVOID* original) {
    const auto address=reinterpret_cast<std::uintptr_t>(target);std::array<unsigned char,32> bytes{};
    assert(read_memory(address,bytes.data(),bytes.size()));fixture_originals[address]=bytes;
    if(address==fixture_image_base+d2vr::NativeSlashSceneRate::rvas[0]) {
        if(!slash_trampoline) {
            slash_trampoline=VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);assert(slash_trampoline);
            std::memcpy(slash_trampoline,d2vr::NativeSlashSceneRate::prologues[0].data(),8);
            const auto tail=d2vr::NativeSlashSceneRate::expected_perform_tail(fixture_image_base);
            std::memcpy(static_cast<unsigned char*>(slash_trampoline)+8,tail.data(),tail.size());
        }
        *original=slash_trampoline;
    } else if(address==fixture_image_base+d2vr::NativeChokeDistanceScope::rvas[0] || address==fixture_image_base+d2vr::NativeChokeDistanceScope::rvas[1]) {
        const unsigned index=address==fixture_image_base+d2vr::NativeChokeDistanceScope::rvas[0]?0u:1u;
        const unsigned prefix=index?5u:6u;auto*& allocation=choke_trampolines[index];
        assert(!allocation);allocation=VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);assert(allocation);
        auto* code=static_cast<unsigned char*>(allocation);std::memcpy(code,d2vr::NativeChokeDistanceScope::prologues[index].data(),prefix);
        code[prefix]=code[prefix+14]=0xff;code[prefix+1]=code[prefix+15]=0x25;
        const auto continuation=address+prefix;std::memcpy(code+prefix+6,&continuation,8);std::memcpy(code+prefix+20,&hook,8);
        *original=allocation;
    } else if(address==fixture_image_base+d2vr::NativeOfferedChoke::rvas[0] || address==fixture_image_base+d2vr::NativeOfferedChoke::rvas[1]) {
        const unsigned index=address==fixture_image_base+d2vr::NativeOfferedChoke::rvas[0]?0u:1u;
        auto*& allocation=offered_trampolines[index];assert(!allocation);
        allocation=VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);assert(allocation);
        auto* code=static_cast<unsigned char*>(allocation);std::memcpy(code,d2vr::NativeOfferedChoke::prologues[index].data(),5);
        code[5]=code[19]=0xff;code[6]=code[20]=0x25;
        const auto continuation=address+5;std::memcpy(code+11,&continuation,8);std::memcpy(code+25,&hook,8);
        *original=allocation;
    } else *original=reinterpret_cast<void*>(address+0x10000000);
    ++hook_mutations;return MH_OK;
}
extern "C" MH_STATUS WINAPI MH_EnableHook(LPVOID target) {
    const auto address=reinterpret_cast<std::uintptr_t>(target);
    assert(fixture_originals.count(address));if(address==fail_enable_address)return static_cast<MH_STATUS>(1);
    fixture_memory[address]=0xe9;++hook_mutations;return MH_OK;
}
extern "C" MH_STATUS WINAPI MH_DisableHook(LPVOID target) {
    const auto address=reinterpret_cast<std::uintptr_t>(target);
    assert(fixture_originals.count(address));put(address,fixture_originals.at(address));++hook_mutations;return MH_OK;
}
namespace d2vr {
struct NativeChokeDistanceScopeFixture {
    static void pending(bool value) {NativeChokeDistanceScope::process_owner().in_flight_=value?1u:0u;}
};
struct NativeInteractionStopFixture {
    [[noreturn]] static void native_failure() {
        if(raise_native_seh)RaiseException(0xe1370546,EXCEPTION_NONCONTINUABLE,0,nullptr);
        throw std::runtime_error("fixture native stage exception");
    }
    static void __fastcall throw_during_stage(void*) {
        auto* aim=NativeInteractionAim::instance_;
        if(NativeInteractionAim::active_prompt_ticket_) {
            const auto event=aim->prompt_event_receipt(GetTickCount64());
            assert(event.sequence==NativeInteractionAim::active_prompt_ticket_ && !event.completed);
            failed_prompt_ticket=event.sequence;
            std::lock_guard<std::mutex> lock(aim->mutex_);
            aim->prompt_completions_.target(event.sequence,{0x500000,7,0,9,1}); // partial progress must be erased on fault
        }
        native_failure();
    }
    static void __fastcall fault_during_stage(void*) {
        RaiseException(0xe1370546,EXCEPTION_NONCONTINUABLE,0,nullptr);
    }
    static bool invoke_stage_fault(void* sensor) {
        __try {NativeInteractionAim::stage<0>(sensor);}
        __except(GetExceptionCode()==0xe1370546?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {return true;}
        return false;
    }
    static std::uintptr_t __fastcall failing_direct(void*,const InteractionRay*,std::uintptr_t*,float*,void*) {native_failure();}
    static void* __fastcall failing_queue(void*,void*,void*,const InteractionRay*,UINT,UINT) {native_failure();}
    static std::uintptr_t __fastcall failing_fetch(void*,void*,void*) {native_failure();}
    static void __fastcall failing_sort(void*,unsigned,int,unsigned char) {native_failure();}
    static std::uintptr_t __fastcall failing_occlusion(void*,void*,const void*,const void*,const void*,unsigned,unsigned char*) {native_failure();}
    static const float* __fastcall failing_range(void*) {native_failure();}
    static void invoke_callback(NativeInteractionAim& aim,unsigned index) {
        const auto sensor=reinterpret_cast<void*>(fixture_sensor);
        InteractionRay ray{};unsigned char output{};
        switch(index) {
        case 0:NativeInteractionAim::stage<0>(sensor);break;
        case 1:NativeInteractionAim::stage<1>(sensor);break;
        case 2:NativeInteractionAim::stage<2>(sensor);break;
        case 3:NativeInteractionAim::direct(nullptr,&ray,nullptr,nullptr,nullptr);break;
        case 4:NativeInteractionAim::queue<0>(nullptr,nullptr,nullptr,&ray,0,0);break;
        case 5:NativeInteractionAim::queue<1>(nullptr,nullptr,nullptr,&ray,0,0);break;
        case 6:aim.fetch_result(nullptr,nullptr,nullptr,0);break;
        case 7:NativeInteractionAim::sort(nullptr,0,0,0);break;
        case 8:aim.occlusion_result(sensor,nullptr,nullptr,nullptr,nullptr,0,&output,0);break;
        case 9:NativeInteractionAim::range_origin_hook(nullptr);break;
        default:assert(false);
        }
    }
    static bool invoke_callback_fault(NativeInteractionAim& aim,unsigned index) {
        __try {invoke_callback(aim,index);}
        __except(GetExceptionCode()==0xe1370546?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {return true;}
        return false;
    }
    static void __fastcall nested_stage(void* sensor) {NativeInteractionAim::stage<1>(sensor);}
    static void __fastcall nested_queue(void*) {
        InteractionRay ray{};NativeInteractionAim::queue<0>(nullptr,nullptr,nullptr,&ray,0,0);
    }
    static void __fastcall recovering_stage(void*) {
        auto* aim=NativeInteractionAim::instance_;const auto* previous=NativeInteractionAim::context_;
        assert(previous && previous->stage==0 && aim->depth_==1 && aim->owned_depth_==1);
        bool propagated{};
        if(raise_native_seh)propagated=invoke_callback_fault(*aim,1);
        else try {invoke_callback(*aim,1);}catch(const std::runtime_error&) {propagated=true;}
        assert(propagated && NativeInteractionAim::context_==previous && aim->depth_==1 && aim->owned_depth_==1);
    }
    static void callback_exception_cleanup(NativeInteractionAim& aim,const NativeInteractionAim::Context& previous) {
        const auto stages=aim.stages_;const auto queues=aim.queues_;const auto direct=aim.direct_;
        const auto fetch=aim.fetch_;const auto sort=aim.sort_;const auto occlusion=aim.occlusion_;const auto range=aim.range_origin_;
        aim.stages_.fill(throw_during_stage);aim.queues_.fill(failing_queue);aim.direct_=failing_direct;
        aim.fetch_=failing_fetch;aim.sort_=failing_sort;aim.occlusion_=failing_occlusion;aim.range_origin_=failing_range;
        NativeLeanQuerySnapshot previous_lean{};NativeLeanQueryTransaction previous_transaction{};
        NativeInteractionAim::context_=&previous;NativeInteractionAim::lean_context_=&previous_lean;
        NativeInteractionAim::lean_transaction_=&previous_transaction;
        const auto previous_operation=crash_operation;crash_operation="fixture parent operation";
        const auto* expected_operation=crash_operation;
        for(unsigned index=0;index<10;++index) {
            raise_native_seh=false;bool propagated{};
            try {invoke_callback(aim,index);}catch(const std::runtime_error& error) {propagated=std::string(error.what())=="fixture native stage exception";}
            assert(propagated && aim.depth_==0 && aim.owned_depth_==0);
            if(index==0) {const auto completed=aim.prompt_completions().get(failed_prompt_ticket);assert(completed.completed && !completed.target);
                assert(!aim.prompt_event_receipt(GetTickCount64()).sequence && !NativeInteractionAim::active_prompt_ticket_);}
            assert(crash_operation==expected_operation);
            assert(NativeInteractionAim::context_==&previous && NativeInteractionAim::lean_context_==&previous_lean &&
                NativeInteractionAim::lean_transaction_==&previous_transaction);
            raise_native_seh=true;
            assert(invoke_callback_fault(aim,index) && aim.depth_==0 && aim.owned_depth_==0);
            if(index==0) {const auto completed=aim.prompt_completions().get(failed_prompt_ticket);assert(completed.completed && !completed.target);
                assert(!aim.prompt_event_receipt(GetTickCount64()).sequence && !NativeInteractionAim::active_prompt_ticket_);}
            assert(crash_operation==expected_operation);
            assert(NativeInteractionAim::context_==&previous && NativeInteractionAim::lean_context_==&previous_lean &&
                NativeInteractionAim::lean_transaction_==&previous_transaction);
        }
        aim.stages_[0]=nested_stage;aim.stages_[1]=nested_queue;
        for(bool seh:{false,true}) {
            raise_native_seh=seh;bool propagated{};
            if(seh)propagated=invoke_callback_fault(aim,0);
            else try {invoke_callback(aim,0);}catch(const std::runtime_error&) {propagated=true;}
            assert(propagated && aim.depth_==0 && aim.owned_depth_==0);
            assert(crash_operation==expected_operation);
            assert(NativeInteractionAim::context_==&previous && NativeInteractionAim::lean_context_==&previous_lean &&
                NativeInteractionAim::lean_transaction_==&previous_transaction);
        }
        aim.stages_[0]=recovering_stage;
        for(bool seh:{false,true}) {
            raise_native_seh=seh;invoke_callback(aim,0);
            assert(aim.depth_==0 && aim.owned_depth_==0 && NativeInteractionAim::context_==&previous &&
                crash_operation==expected_operation);
        }
        crash_operation=previous_operation;
        raise_native_seh=false;NativeInteractionAim::context_=nullptr;NativeInteractionAim::retrieved_={};
        NativeInteractionAim::lean_context_=nullptr;NativeInteractionAim::lean_transaction_=nullptr;
        aim.stages_=stages;aim.queues_=queues;aim.direct_=direct;aim.fetch_=fetch;aim.sort_=sort;aim.occlusion_=occlusion;aim.range_origin_=range;
    }
    static void stage_exception_cleanup(NativeInteractionAim& aim,const NativeInteractionAim::Context& previous) {
        NativeInteractionAim::context_=&previous;
        const NativeInteractionAim::Retrieved retrieved{1,2,3,4,&previous,true};
        NativeInteractionAim::retrieved_=retrieved;
        aim.stages_[0]=throw_during_stage;
        bool propagated{};
        try {NativeInteractionAim::stage<0>(reinterpret_cast<void*>(fixture_sensor));}
        catch(const std::runtime_error& error) {propagated=std::string(error.what())=="fixture native stage exception";}
        assert(propagated && aim.depth_==0 && aim.owned_depth_==0);
        assert(NativeInteractionAim::context_==&previous && NativeInteractionAim::retrieved_.scope==&previous &&
            NativeInteractionAim::retrieved_.data==retrieved.data && NativeInteractionAim::retrieved_.ranked);
        aim.stages_[0]=fault_during_stage;
        assert(invoke_stage_fault(reinterpret_cast<void*>(fixture_sensor)) && aim.depth_==0 && aim.owned_depth_==0);
        assert(NativeInteractionAim::context_==&previous && NativeInteractionAim::retrieved_.scope==&previous);
        NativeInteractionAim::context_=nullptr;NativeInteractionAim::retrieved_={};aim.stages_[0]=native_stage;
    }
    static void __fastcall request_stop_during_stage(void*) {
        ++native_calls;NativeInteractionAim::instance_->stopping_=true;
    }
    static void prepare() {
        constexpr std::array<unsigned char,366> offered_profile_0{
            0x48,0x89,0x5c,0x24,0x10,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0xba,0x02,0x00,0x00,0x00,0x48,0x83,0xc1,0x40,0xe8,0x75,
            0x96,0x04,0x00,0x48,0x8b,0xf8,0x48,0x85,0xc0,0x0f,0x84,0x3c,0x01,0x00,0x00,0xba,0x2a,0x00,0x00,0x00,0x48,0x8b,0xc8,0xe8,
            0xfc,0x1e,0x47,0x00,0x84,0xc0,0x0f,0x84,0xc8,0x00,0x00,0x00,0x48,0x8b,0x8f,0x38,0x03,0x00,0x00,0xba,0x0b,0x00,0x00,0x00,
            0xe8,0xc3,0x68,0x3e,0x00,0x48,0x8b,0xc8,0x48,0x8b,0xf8,0x48,0x8b,0x10,0xff,0x92,0xd0,0x00,0x00,0x00,0x84,0xc0,0x0f,0x84,
            0xff,0x00,0x00,0x00,0x48,0x8b,0x4b,0x08,0x48,0x8b,0x49,0x08,0xe8,0xbf,0x1f,0x4f,0x00,0x48,0x85,0xc0,0x0f,0x84,0xe9,0x00,
            0x00,0x00,0x48,0x8b,0x4f,0x70,0x8b,0x11,0x48,0x8d,0x88,0xb0,0x30,0x31,0x00,0x89,0x54,0x24,0x30,0x48,0x8d,0x54,0x24,0x30,
            0xe8,0xeb,0x4a,0x42,0x00,0x48,0x8b,0xf8,0x48,0x85,0xc0,0x0f,0x84,0xc2,0x00,0x00,0x00,0x48,0x8b,0xd0,0x48,0x8b,0xcb,0xe8,
            0x94,0xec,0xff,0xff,0x84,0xc0,0x0f,0x85,0xaf,0x00,0x00,0x00,0x48,0x8d,0x15,0x05,0x01,0x64,0x03,0x41,0xb8,0x02,0x00,0x00,
            0x00,0x48,0x8b,0xcf,0xe8,0xd7,0x63,0x4e,0x00,0x48,0x85,0xc0,0x74,0x0a,0xf6,0x40,0x50,0x08,0x0f,0x85,0x8b,0x00,0x00,0x00,
            0x48,0x8d,0x15,0x41,0x6d,0x65,0x03,0x41,0xb8,0x02,0x00,0x00,0x00,0x48,0x8b,0xcf,0xe8,0xb3,0x63,0x4e,0x00,0x48,0x85,0xc0,
            0x74,0x71,0x48,0x8b,0x40,0x08,0xf6,0x80,0xcc,0x00,0x00,0x00,0x20,0x74,0x64,0x45,0x33,0xc0,0xeb,0x50,0xba,0x29,0x00,0x00,
            0x00,0x48,0x8b,0xcf,0xe8,0x1f,0x1e,0x47,0x00,0x84,0xc0,0x74,0x4e,0x48,0x8d,0x4b,0x60,0xba,0x02,0x00,0x00,0x00,0xe8,0x6d,
            0x95,0x04,0x00,0xba,0x17,0x00,0x00,0x00,0x48,0x8b,0xc8,0xe8,0xe0,0x67,0x3e,0x00,0x48,0x85,0xc0,0x74,0x2e,0x8b,0x48,0x50,
            0xe8,0xa3,0xb1,0x34,0x00,0x48,0x85,0xc0,0x74,0x21,0x48,0x8b,0xd0,0x48,0x8b,0xcb,0xe8,0xf3,0xeb,0xff,0xff,0x84,0xc0,0x75,
            0x12,0x41,0xb0,0x01,0x45,0x33,0xc9,0x48,0x8b,0xcb,0x41,0x8d,0x51,0x09,0xe8,0x6d,0x01,0x00,0x00,0x48,0x8b,0x5c,0x24,0x38,
            0x48,0x83,0xc4,0x20,0x5f,0xc3};
        put(fixture_image_base+NativeOfferedChoke::rvas[0],offered_profile_0);
        constexpr std::array<unsigned char,252> offered_profile_1{
            0x48,0x8b,0xc4,0x41,0x56,0x48,0x83,0xec,0x70,0x48,0xc7,0x40,0xa8,0xfe,0xff,0xff,0xff,0x48,0x89,0x58,0x08,0x48,0x89,0x68,
            0x10,0x48,0x89,0x70,0x18,0x48,0x89,0x78,0x20,0x41,0x0f,0xb6,0xf1,0x41,0x0f,0xb6,0xe8,0x8b,0xfa,0x4c,0x8b,0xf1,0x8d,0x42,
            0xf8,0x83,0xf8,0x01,0x77,0x0e,0x83,0x3d,0x1b,0xbe,0x5e,0x03,0x00,0x75,0x12,0xe9,0x9d,0x00,0x00,0x00,0x83,0x3d,0x8d,0xbd,
            0x5e,0x03,0x00,0x0f,0x84,0x90,0x00,0x00,0x00,0x33,0xdb,0x48,0x89,0x5c,0x24,0x30,0x48,0x8d,0x4c,0x24,0x38,0xe8,0x7e,0xe5,
            0xda,0xff,0x48,0x89,0x5c,0x24,0x60,0x66,0x89,0x5c,0x24,0x68,0x80,0x64,0x24,0x6a,0xf0,0xc7,0x44,0x24,0x6c,0x10,0x00,0xff,
            0x3f,0x89,0x7c,0x24,0x30,0x83,0xff,0xff,0x74,0x2c,0x83,0xff,0x08,0x74,0x0b,0x83,0xff,0x09,0x0f,0x95,0xc3,0x83,0xc3,0x15,
            0xeb,0x05,0xbb,0x13,0x00,0x00,0x00,0x8b,0xcb,0xe8,0x72,0x75,0xca,0xff,0x48,0x8d,0x15,0x53,0x12,0x72,0x01,0x48,0x85,0xc0,
            0x48,0x0f,0x45,0xd0,0xeb,0x07,0x48,0x8d,0x15,0x43,0x12,0x72,0x01,0x48,0x8d,0x4c,0x24,0x38,0xe8,0x31,0xe9,0xda,0xff,0x40,
            0x88,0x6c,0x24,0x68,0x40,0x88,0x74,0x24,0x69,0x48,0x8d,0x54,0x24,0x30,0x49,0x8b,0xce,0xe8,0x8a,0xdf,0xff,0xff,0x90,0x48,
            0x8d,0x4c,0x24,0x38,0xe8,0xdf,0xe6,0xda,0xff,0x4c,0x8d,0x5c,0x24,0x70,0x49,0x8b,0x5b,0x10,0x49,0x8b,0x6b,0x18,0x49,0x8b,
            0x73,0x20,0x49,0x8b,0x7b,0x28,0x49,0x8b,0xe3,0x41,0x5e,0xc3};
        put(fixture_image_base+NativeOfferedChoke::rvas[1],offered_profile_1);
        for(unsigned index=0;index<2;++index)put(fixture_image_base+NativeChokeDistanceScope::rvas[index],NativeChokeDistanceScope::prologues[index]);
        put(fixture_image_base+0x3a6a9d,NativeChokeDistanceScope::caller_code);
        for(const auto& pin:NativeChokeDistanceScope::consumer_pins)for(unsigned offset=0;offset<pin.size;++offset)put(fixture_image_base+pin.rva+offset,pin.bytes[offset]);
        put(fixture_image_base+0x1d0b738,fixture_image_base+0x94fd10);
        put(fixture_image_base+NativeBlockEligibility::prologue_rva,NativeBlockEligibility::prologue);
        put(fixture_image_base+NativeBlockEligibility::caller_rva,NativeBlockEligibility::caller_code);
        for(const auto& pin:NativeBlockEligibility::slot_pins)put(fixture_image_base+pin.first,fixture_image_base+pin.second);
        for(unsigned index=0;index<NativeSlashSceneRate::rvas.size();++index)put(fixture_image_base+NativeSlashSceneRate::rvas[index],NativeSlashSceneRate::prologues[index]);
        for(const auto& pin:NativeSlashSceneRate::caller_pins)put(fixture_image_base+pin.rva,pin.bytes);
        for(const auto& pin:NativeSlashSceneRate::slot_pins)put(fixture_image_base+pin.first,fixture_image_base+pin.second);
        put(fixture_image_base+NativeSlashSceneRate::setter_rva,NativeSlashSceneRate::setter_code);
        for(unsigned index=0;index<6;++index)put(fixture_image_base+NativeInteractionAim::rvas[index],NativeInteractionAim::expected[index]);
        put(fixture_image_base+0x417820,std::array<unsigned char,32>{});put(fixture_image_base+0x417820,NativeInteractionAim::range_origin_code);
        // Pinned Steam17576867 SHA256 b671f824...6180, RVA3b8b05..3b8c56.
        constexpr std::array<unsigned char,338> hand_hover_scan{
            0x44,0x0f,0xb6,0xf0,0x48,0x8b,0x4c,0x24,0x50,0x48,0x85,0xc9,0x74,0x44,0x8b,0x51,0x10,0x85,0xd2,0x7e,
            0x3d,0x48,0x8d,0x85,0x80,0x01,0x00,0x00,0x48,0x89,0x44,0x24,0x30,0x89,0x54,0x24,0x28,0x48,0x8d,0x44,
            0x24,0x40,0x48,0x89,0x44,0x24,0x20,0x4c,0x8d,0x4d,0x98,0x4c,0x8d,0x45,0x80,0x48,0x8b,0xd3,0x48,0x8b,
            0xcf,0xe8,0x49,0xf0,0xff,0xff,0x48,0x8b,0x4c,0x24,0x50,0x84,0xc0,0x48,0x0f,0x45,0xce,0x48,0x89,0x4c,
            0x24,0x50,0x48,0x8b,0x54,0x24,0x68,0x48,0x85,0xd2,0x74,0x49,0x8b,0x42,0x10,0x85,0xc0,0x7e,0x42,0x48,
            0x8d,0x8d,0x80,0x01,0x00,0x00,0x48,0x89,0x4c,0x24,0x30,0x89,0x44,0x24,0x28,0x48,0x8d,0x44,0x24,0x40,
            0x48,0x89,0x44,0x24,0x20,0x4c,0x8d,0x4d,0x98,0x4c,0x8d,0x45,0x80,0x48,0x8b,0xd3,0x48,0x8b,0xcf,0xe8,
            0xfb,0xef,0xff,0xff,0x48,0x8b,0x54,0x24,0x68,0x84,0xc0,0x48,0x0f,0x45,0xd6,0x48,0x89,0x54,0x24,0x68,
            0x48,0x8b,0x4c,0x24,0x50,0x48,0x85,0xc9,0x75,0x09,0x48,0x85,0xd2,0x75,0x04,0x32,0xc0,0xeb,0x02,0xb0,
            0x01,0x0f,0xb6,0xb5,0x78,0x01,0x00,0x00,0x45,0x84,0xf6,0x74,0x13,0x44,0x0f,0xb6,0xa5,0x70,0x01,0x00,
            0x00,0x40,0x84,0xf6,0x0f,0x84,0xe9,0xfe,0xff,0xff,0xeb,0x2b,0x84,0xc0,0x74,0x1f,0x41,0xb4,0x01,0x8b,
            0x83,0xa8,0x00,0x00,0x00,0x89,0x87,0x84,0x00,0x00,0x00,0x48,0x89,0x8f,0xa8,0x00,0x00,0x00,0x48,0x89,
            0x97,0xb0,0x00,0x00,0x00,0xeb,0x08,0x44,0x0f,0xb6,0xa5,0x70,0x01,0x00,0x00,0x44,0x8b,0xb5,0x88,0x01,
            0x00,0x00,0x45,0x84,0xff,0x0f,0x84,0xb3,0xfe,0xff,0xff,0x41,0xb4,0x01,0x8b,0x83,0xa8,0x00,0x00,0x00,
            0x89,0x87,0x8c,0x00,0x00,0x00,0x41,0x83,0xfe,0x04,0x74,0x0c,0x8b,0x83,0xa8,0x00,0x00,0x00,0x89,0x87,
            0x88,0x00,0x00,0x00,0x48,0x8b,0x4d,0xc0,0x48,0x8b,0x01,0xff,0x50,0x30,0x89,0x87,0x90,0x00,0x00,0x00,
            0x80,0xa7,0xb8,0x00,0x00,0x00,0xfe,0x40,0x80,0xe6,0x01,0x40,0x08,0xb7,0xb8,0x00,0x00,0x00
        };
        put(fixture_image_base+0x3b8b05,hand_hover_scan);
        put(fixture_image_base+0x3b80a0,native_hand_eligibility_code);
        for(unsigned index=0;index<NativeInteractionAim::ranking_rvas.size();++index) {
            std::array<unsigned char,32> bytes{};
            const auto nibble=[](char value){return value<='9'?value-'0':value-'a'+10;};
            for(unsigned offset=0;offset<bytes.size();++offset)bytes[offset]=static_cast<unsigned char>(
                nibble(NativeInteractionAim::ranking_code[index][2*offset])*16+nibble(NativeInteractionAim::ranking_code[index][2*offset+1]));
            put(fixture_image_base+NativeInteractionAim::ranking_rvas[index],bytes);
        }
        put(fixture_image_base+0x39a5290,fixture_game);put(fixture_game,fixture_image_base+0x1c940a8);
        put(fixture_game+0x3130b0,fixture_player);put(fixture_player,fixture_image_base+0x1c5e428);put(fixture_player+0xa8,std::uint32_t{123});
        put(fixture_camera,std::array<std::byte,camera_read_size>{});put(fixture_camera+0x25150,std::array<std::byte,0x200>{});
        for(const auto& item:std::array<std::pair<std::uintptr_t,std::uintptr_t>,9>{{
            {fixture_camera,0x1c72218},{fixture_camera+0x20,0x1c722f8},{fixture_body,0x1c5a8d8},{fixture_body+0x20,0x1c5a9b0},
            {fixture_controller,0x1c72918},{fixture_physics,0x1c73268},{fixture_physics+0x20,0x1c73340},{fixture_physics+0x30,0x1c73378},{fixture_delegate+0x160,0x1b02768}}})
            put(item.first,fixture_image_base+item.second);
        for(const auto address:{fixture_camera,fixture_body,fixture_controller,fixture_physics,fixture_sensor}) {
            put(address+0x10,fixture_player);put(address+0x18,rig::Identity{5,10});
        }
        put(fixture_camera+0x25158,fixture_controller);put(fixture_camera+0x25160,fixture_physics);put(fixture_camera+0x25170,fixture_body);
        put(fixture_physics+0x48,fixture_delegate);put(fixture_delegate+0x168,fixture_physics);put(fixture_delegate+0x120,fixture_player+0x300);put(fixture_delegate+0x1e0,fixture_delegate);
        put(fixture_delegate+0xd0,0.f);put(fixture_delegate+0xbc,0.f);put(fixture_delegate+0x40,0.f);
        put(fixture_controller+0xd0,std::array<float,2>{});put(fixture_player+0x30c,std::array<float,9>{1,0,0,0,1,0,0,0,1});
        put(fixture_sensor,fixture_image_base+0x1b403b8);put(fixture_sensor+0x38,static_cast<unsigned char>(1));put(fixture_sensor+0x40,fixture_camera);
    }
    static void run() {
        {
            OfferedChokeGrip choke;
            choke.update(false,false,false,true,9,0,0,false);
            const auto drained=choke.update(false,false,true,false,9,0,0,false);
            assert(drained.consume_use && !drained.rb);
            auto route=choke.update(false,false,true,true,9,0,0,false);
            assert(route.consume_use && !route.rb);
            NativeInteractionAim::publish_sword_pose_epoch(9);
            NativeInteractionAim::reserve_nonlethal_grip(route.rb);
            OwnedSwordContact held_contact{};held_contact.pose_epoch=9;
            assert(NativeInteractionAim::request_sword_contact(held_contact));
            NativeInteractionAim::sword_contacts_.clear();
            VrCombat combat;CombatFrame frame{};
            frame.allowed=!route.rb;frame.tracked=frame.equipped=true;
            frame.head_pos={0,1.7f,0};frame.head_fwd={0,0,-1};frame.head_right={1,0,0};
            frame.pos={.3f,1.f,-.4f};frame.aim={0,0,-1};
            bool physical_swing{},generated_attack{};
            for(unsigned sample=0;sample<32;++sample) {
                frame.now_ms=1000+sample*16;
                if(sample>=20) {frame.pos.x+=.08f;frame.vel={5,0,0};}
                const auto decision=combat.update(frame);
                physical_swing=physical_swing || decision.swing!=SwingKind::none;
                generated_attack=generated_attack || decision.rt;
            }
            assert(physical_swing && generated_attack && frame.trigger==0);
            MotionSwordSwing fallback;
            const bool physically_owned=true;
            assert(!fallback.update(.1f,true,!route.rb && physically_owned,9,2000,2000).active);
            const auto fallback_attack=fallback.update(3.f,true,!route.rb && physically_owned,9,2020,2020);
            assert(fallback_attack.started && fallback_attack.active && frame.trigger==0);
            const bool cleared_by_drain=route.consume_use && (route.rb || !physically_owned);
            assert(!cleared_by_drain);
            assert(!fallback.update(3.f,true,!route.rb && !physically_owned,9,2040,2040).active);
            choke.update(false,false,false,true,9,0,0,false);
            route=choke.update(true,true,true,true,9,0,0,false);
            assert(!route.rb);
            choke.update(false,false,false,true,9,0,0,false);
            route=choke.update(true,true,true,true,9,123,0,false);
            assert(route.consume_use && route.rb);
            const auto before_claim=NativeInteractionAim::nonlethal_grip_revision_.load();
            NativeInteractionAim::reserve_nonlethal_grip(route.rb);
            assert(!NativeInteractionAim::request_sword_contact(held_contact));
            assert(!NativeInteractionAim::melee_batch_current(before_claim));
            route=choke.update(true,false,true,true,9,0,123,false);
            assert(route.rb);
            route=choke.update(true,false,true,true,9,0,0,false);
            assert(route.consume_use && !route.rb);
            NativeInteractionAim::reserve_nonlethal_grip(route.rb);
            assert(NativeInteractionAim::request_sword_contact(held_contact));
            NativeInteractionAim::sword_contacts_.clear();
            choke.update(false,false,false,true,9,0,0,false);
            route=choke.update(true,true,true,true,9,123,0,false);
            assert(route.rb);
            route=choke.update(false,false,true,true,10,0,0,false);
            assert(route.consume_use && !route.rb);
        }
        NativeInteractionAim::reserve_nonlethal_grip(false);
        NativeInteractionAim::publish_sword_pose_epoch(9);
        OwnedSwordContact contact{};contact.pose_epoch=9;
        for(unsigned index=0;index<4;++index)assert(NativeInteractionAim::request_sword_contact(contact));
        const auto full_before=NativeInteractionAim::sword_queue_full_.load();
        assert(!NativeInteractionAim::request_sword_contact(contact));
        assert(NativeInteractionAim::sword_queue_full_.load()==full_before+1);
        const auto nonlethal_before=NativeInteractionAim::sword_nonlethal_rejected_.load();
        NativeInteractionAim::reserve_nonlethal_grip(true);
        assert(NativeInteractionAim::sword_contacts_.empty() && !NativeInteractionAim::request_sword_contact(contact));
        assert(NativeInteractionAim::sword_nonlethal_rejected_.load()==nonlethal_before+5);
        NativeInteractionAim::reserve_nonlethal_grip(false);
        assert(NativeInteractionAim::request_sword_contact(contact));
        const auto owned_revision=NativeInteractionAim::nonlethal_grip_revision_.load();
        std::vector<OwnedSwordContact> detached_contacts;detached_contacts.swap(NativeInteractionAim::sword_contacts_);
        NativeInteractionAim::reserve_nonlethal_grip(true);NativeInteractionAim::reserve_nonlethal_grip(false);
        assert(detached_contacts.size()==1 && !NativeInteractionAim::melee_batch_current(owned_revision));
        const auto epoch_before=NativeInteractionAim::sword_epoch_rejected_.load();
        NativeInteractionAim::publish_sword_pose_epoch(10);assert(!NativeInteractionAim::request_sword_contact(contact));
        assert(NativeInteractionAim::sword_epoch_rejected_.load()==epoch_before+1);
        NativeInteractionAim::publish_sword_pose_epoch(0);
        std::atomic<bool> observations_done{};
        std::thread observation_writer([&] {
            for(std::uint32_t index=1;index<=1000;++index) {
                std::lock_guard<std::mutex> guard(NativeInteractionAim::trace_mutex_);
                NativeInteractionAim::sword_last_contact_={};
                NativeInteractionAim::sword_last_contact_.player_spawn=index;
                NativeInteractionAim::sword_last_contact_.sword_spawn=index+1;
                NativeInteractionAim::sword_last_contact_.victim_spawn=index+2;
                NativeInteractionAim::sword_last_contact_.valid=true;
            }
            observations_done=true;
        });
        while(!observations_done) {
            const auto observation=NativeInteractionAim::sword_contact_observation();
            if(observation.valid)assert(observation.sword_spawn==observation.player_spawn+1 && observation.victim_spawn==observation.player_spawn+2);
        }
        observation_writer.join();
        assert(NativeInteractionAim::sword_contact_status_json().find("\"victim_spawn\":1002")!=std::string::npos);
        prepare();
        const auto folder=std::filesystem::absolute(std::filesystem::path(__FILE__).parent_path().parent_path()/L"artifacts"/L"tests"/L"interaction-stop");
        std::filesystem::create_directories(folder);
        auto owner=std::make_unique<NativeInteractionAim>();auto& aim=*owner;
        aim.install(fixture_image_base,read_memory,logic,false,folder/L"first.csv");
        aim.stages_[0]=native_stage;
        constexpr std::uintptr_t contact_world=0x216000000;
        put(fixture_game+NativeWorldTrace::world_offset,contact_world);
        const auto contact_camera=native_player_camera(read_memory,fixture_image_base,fixture_camera);assert(contact_camera);
        NativeInteractionAim::Context contact_scope{*contact_camera,{},0,0,fixture_sensor,0,{5,10}};
        const auto contact_owner=aim.anatomical_scope_owner(contact_scope);assert(contact_owner);
        stage_exception_cleanup(aim,contact_scope);
        callback_exception_cleanup(aim,contact_scope);
        aim.service_anatomical_contact(contact_scope);
        assert(aim.anatomical_owner(0)==*contact_owner && aim.anatomical_cast_count()==0);
        AnatomicalContactRequest contact_request{};contact_request.token={*contact_owner,1,1,1,1,0};contact_request.tick=GetTickCount64();contact_request.safe_from=true;
        for(unsigned proxy=0;proxy<hand_proxy_count;++proxy) {
            contact_request.from[proxy]={{-.1,static_cast<double>(proxy),0},.02};contact_request.to[proxy]={{.01,static_cast<double>(proxy),0},.02};
        }
        assert(!aim.post_anatomical_contact(contact_request));
        const auto admission=aim.anatomical_contact_.admission();assert(admission);
        contact_request.token.admission_epoch=admission->epoch;
        assert(!aim.post_anatomical_contact(contact_request));
        AnatomicalWorldContact::RequestPair contact_pair{};contact_pair[0]=contact_request;
        assert(aim.anatomical_contact_.post_batch_current(*admission,contact_pair));
        auto next_reference=contact_request;next_reference.token.pose_epoch=2;next_reference.token.request_revision=1;
        contact_pair[0]=next_reference;assert(aim.anatomical_contact_.post_batch_current(*admission,contact_pair));
        contact_pair[0]=contact_request;assert(!aim.anatomical_contact_.post_batch_current(*admission,contact_pair));
        assert(!aim.anatomical_result(contact_request.token,contact_request.tick));
        const auto decl_vtable=fixture_image_base+NativeChokeDistanceScope::decl_vtable_rva;std::memcpy(choke_decl,&decl_vtable,8);
        for(unsigned index=0;index<6;++index)std::memcpy(choke_decl+NativeChokeDistanceScope::offsets[index],&NativeChokeDistanceScope::shipped[index],4);
        const auto block_vtable=fixture_image_base+0x1c81938;std::memcpy(block_decl,&block_vtable,8);
        const float block_default=.5f,slash_default=1.f;std::memcpy(block_decl+0x100,&block_default,4);std::memcpy(slash_entries+0x24,&slash_default,4);
        std::array<unsigned char,sizeof(choke_decl)> untouched{};std::memcpy(untouched.data(),choke_decl,sizeof(choke_decl));
        aim.takedown_ranges(true);assert(NativeChokeDistanceScope::process_owner().enabled() && aim.takedown_range_kill_rules()==0);
        put(fixture_camera+0x25200,1);
        assert(aim.disable() && !aim.active_ && !aim.stopping_ && !aim.enabled_[0]);
        assert(aim.restore_takedown_ranges() && NativeChokeDistanceScope::process_owner().stopped());
        assert(!aim.anatomical_owner(0).valid() && !aim.anatomical_contact_.has_pending(0));
        assert(std::memcmp(untouched.data(),choke_decl,sizeof(choke_decl))==0);
        assert(!NativeBlockEligibility::process_owner().enabled() && !NativeSlashSceneRate::process_owner().enabled());
        float block_after{},slash_after{};std::memcpy(&block_after,block_decl+0x100,4);std::memcpy(&slash_after,slash_entries+0x24,4);
        assert(block_after==block_default && slash_after==slash_default);
        put(fixture_camera+0x25200,0);
        aim.install(fixture_image_base,read_memory,logic,false,folder/L"pending.csv");
        auto& choke=NativeChokeDistanceScope::process_owner();
        NativeChokeDistanceScopeFixture::pending(true);
        assert(!choke.disable(0) && !choke.stopped() && !choke.ready_for_install() && !aim.restore_takedown_ranges());
        const auto mutations_before=hook_mutations;bool rejected{};
        try {aim.install(fixture_image_base,read_memory,logic,false,folder/L"blocked.csv");}catch(const std::runtime_error&){rejected=true;}
        assert(rejected && hook_mutations==mutations_before);
        NativeChokeDistanceScopeFixture::pending(false);assert(aim.disable() && aim.restore_takedown_ranges());
        aim.install(fixture_image_base,read_memory,logic,false,folder/L"callback-stop.csv");
        aim.stages_[0]=request_stop_during_stage;NativeInteractionAim::arm_show_=3;
        const auto services_before_request=NativeWeaponHands::services.load();
        NativeInteractionAim::stage<0>(reinterpret_cast<void*>(fixture_sensor));
        assert(aim.active_ && aim.stopping_ && aim.depth_==0 && aim.owned_depth_==0);
        assert(NativeInteractionAim::arm_show_==3 && NativeWeaponHands::services.load()==services_before_request);
        assert(aim.disable() && aim.restore_takedown_ranges() && !aim.stopping_ && NativeInteractionAim::arm_show_==0);
        assert(std::memcmp(untouched.data(),choke_decl,sizeof(choke_decl))==0);
        // Route-enabled installation owns the extra eligibility hook and restores its complete semantic/geometric pin.
        aim.install(fixture_image_base,read_memory,logic,true,folder/L"sphere-stop.csv");
        assert(aim.eligibility_enabled_ && aim.range_origin_enabled_);
        assert(aim.disable() && !aim.eligibility_enabled_ && !aim.range_origin_enabled_);
        std::array<unsigned char,native_hand_eligibility_code.size()> restored_body{};
        assert(read_memory(fixture_image_base+0x3b80a0,restored_body.data(),restored_body.size()) && restored_body==native_hand_eligibility_code);
        // A later hook failure rolls back eligibility too; no partly installed sphere scope survives.
        fail_enable_address=fixture_image_base+NativeInteractionAim::ranking_rvas[0];bool sphere_failed=false;
        try {aim.install(fixture_image_base,read_memory,logic,true,folder/L"sphere-rollback.csv");}catch(const std::runtime_error&) {sphere_failed=true;}
        fail_enable_address=0;
        assert(sphere_failed && !aim.active_ && !aim.eligibility_enabled_ && !aim.range_origin_enabled_ && !aim.stopping_);
        assert(read_memory(fixture_image_base+0x3b80a0,restored_body.data(),restored_body.size()) && restored_body==native_hand_eligibility_code);
        const auto pin_mutations=hook_mutations;
        put(fixture_image_base+0x3b80a0+native_hand_eligibility_code.size()-1,static_cast<unsigned char>(0xcc));
        bool pin_rejected=false;
        try {aim.install(fixture_image_base,read_memory,logic,true,folder/L"sphere-changed-tail.csv");}catch(const std::runtime_error&) {pin_rejected=true;}
        assert(pin_rejected && hook_mutations==pin_mutations); // a matching prologue cannot hide changed geometry/permissions
        put(fixture_image_base+0x3b80a0,native_hand_eligibility_code);
    }
};
}
int main(){SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);_set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);_set_error_mode(_OUT_TO_STDERR);d2vr::NativeInteractionStopFixture::run();std::cout<<"PASS: ten callback C++/SEH and nested TLS cleanup; no shared declaration writes; stop without authored Stage0; pending scoped drain rejects reinstall; callback stop bookkeeping\n";}
