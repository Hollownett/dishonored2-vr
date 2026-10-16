#define NOMINMAX
#include "../src/native_manual_equip_runtime.hpp"
#include "../src/native_weapon_hands.hpp"
#include "../src/vr_controls.hpp"
#include <cassert>
#include <functional>
#include <iostream>
#include <map>
#include <stdexcept>

namespace {
using Runtime=d2vr::NativeManualEquipRuntime;using Hooks=Runtime::Hooks;using Policy=Runtime::Policy;
std::uintptr_t image_base{};
constexpr std::uintptr_t fixture_game=0x200000000,fixture_player=0x201000000,fixture_body=0x202000000,fixture_machine=0x203000000,fixture_pool=0x204000000,
    fixture_controller=0x205000000,inventory=0x206000000,equip=0x207000000,fixture_entries=0x208000000,weapon=0x209000000,
    declaration=0x20a000000,equipment=0x20b000000,equipment_decl=0x20c000000,fixture_components=0x20d000000,
    weapon_components=0x20e000000,hold=0x20f000000,corpse=0x210000000,hold_decl=0x211000000,corpse_decl=0x212000000,
    params_records=0x213000000,queue_records=0x214000000,hold_two=0x215000000,hold_two_decl=0x216000000;
constexpr std::uint32_t weapon_spawn=0x20002;
std::map<std::uintptr_t,unsigned char> memory;
std::array<std::array<unsigned char,32>,2> saved_code;
unsigned processor_calls{},wheel_calls{},fail_native{},locks{};
Policy::Params received{};std::function<void()> native_action;
template<class Value>void put(std::uintptr_t address,const Value& value){
    const auto* bytes=reinterpret_cast<const unsigned char*>(&value);
    for(std::size_t offset=0;offset<sizeof(value);++offset)memory[address+offset]=bytes[offset];
}
bool read_memory(std::uintptr_t address,void* output,std::size_t size){
    if(memory.count(address)){
        for(std::size_t offset=0;offset<size;++offset){const auto found=memory.find(address+offset);if(found==memory.end())return false;static_cast<unsigned char*>(output)[offset]=found->second;}
        return true;
    }
    SIZE_T copied{};return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(address),output,size,&copied) && copied==size;
}
void __fastcall native_processor(std::uintptr_t state,const void* params){
    assert(state);++processor_calls;std::memcpy(&received,params,sizeof(received));
    if(received.get<std::uint32_t>(24)&2){assert(locks);--locks;}
    if(native_action)native_action();
}
void __fastcall native_wheel(std::uintptr_t owner,std::uintptr_t item,std::uintptr_t ammo){
    assert(owner && item);assert(!ammo);++wheel_calls;
    if(native_action)native_action();
    if(fail_native==1)throw std::runtime_error("native wheel");
    if(fail_native==2)RaiseException(0xe0553001,0,0,nullptr);
}
Policy::Params params(std::uint32_t code){
    Policy::Params value{};value.bytes.fill(0xab);value.set(0,std::uint32_t{7});value.set(4,std::uint32_t{0x5c});
    value.set(8,std::uintptr_t{});value.set(16,std::uintptr_t{});value.set(24,code);value.set(28,std::uint32_t{0x5b});
    value.set(32,std::uint64_t{});value.set(40,static_cast<unsigned char>(1));return value;
}
void seed(){
    image_base=reinterpret_cast<std::uintptr_t>(VirtualAlloc(nullptr,0x4000000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));assert(image_base);
    for(unsigned index=0;index<2;++index)std::memcpy(reinterpret_cast<void*>(image_base+Hooks::rvas[index]),Hooks::prologues[index].data(),32);
    for(const auto& pin:Policy::call_pins)std::memcpy(reinterpret_cast<void*>(image_base+pin.rva),pin.bytes.data(),pin.bytes.size());
    for(const auto& slot:Policy::slots){const auto value=image_base+slot.second;std::memcpy(reinterpret_cast<void*>(image_base+slot.first),&value,sizeof(value));}
    put(image_base+0x39a5290,fixture_game);put(fixture_game,image_base+0x1c940a8);put(fixture_game+0x3130b0,fixture_player);
    put(fixture_player,image_base+0x1c5e428);put(fixture_player+0xa8,std::uint32_t{42});put(fixture_player+0x268,d2vr::rig::List{fixture_components,2,2u<<8});put(fixture_components,fixture_body);put(fixture_components+8,fixture_machine);
    put(fixture_body,image_base+0x1c5a8d8);put(fixture_body+0x20,image_base+0x1c5a9b0);put(fixture_body+0x10,fixture_player);put(fixture_body+0x18,d2vr::rig::Identity{123,10});
    put(fixture_machine,image_base+0x1c73d58);put(fixture_machine+0x10,fixture_player);put(fixture_machine+0x18,d2vr::rig::Identity{500,10});
    put(fixture_machine+0x90,fixture_pool);put(fixture_machine+0x300,fixture_body);put(fixture_machine+0x328,inventory);put(fixture_machine+0x2f8,fixture_controller);
    put(fixture_pool+0x10+49*8,hold);put(fixture_pool+0x10+45*8,corpse);put(fixture_pool+0x10+50*8,hold_two);put(fixture_pool+0x10+64*8,equip);
    put(hold_two,image_base+0x1c7a998);put(hold_two+8,50);put(hold_two+0x10,fixture_machine);put(hold_two+0x18,hold_two_decl);
    put(hold_two+0x20,static_cast<unsigned char>(0));put(hold_two_decl,image_base+0x1c7a868);put(hold_two_decl+0x60,50);
    for(unsigned index=0;index<2;++index){const auto state=index?corpse:hold,decl=index?corpse_decl:hold_decl;const int number=index?45:49;
        put(state,image_base+(index?0x1c77748:0x1c7a998));put(state+8,number);put(state+0x10,fixture_machine);put(state+0x18,decl);put(state+0x20,static_cast<unsigned char>(0));
        put(decl,image_base+(index?0x1c77618:0x1c7a868));put(decl+0x60,number);
    }
    put(equip,image_base+0x1c79cf8);put(equip+8,64);put(equip+0x10,fixture_machine);put(equip+0x20,static_cast<unsigned char>(0));
    put(equip+0x108,std::uintptr_t{});put(equip+0x118,std::uintptr_t{});put(equip+0x150,static_cast<unsigned char>(0));
    put(fixture_controller,image_base+0x1c72918);put(fixture_controller+0x10,fixture_player);put(fixture_controller+0x58,fixture_machine);put(fixture_controller+0x171,static_cast<unsigned char>(1));
    put(fixture_controller+0x190,fixture_entries);put(fixture_controller+0x198,std::uintptr_t{});put(fixture_controller+0x1a0,fixture_entries);put(fixture_controller+0x1a8,std::uintptr_t{});
    put(inventory,image_base+0x1b9a878);put(inventory+0x10,fixture_player);put(inventory+0x18,d2vr::rig::Identity{50,10});
    put(inventory+0x38,d2vr::rig::List{fixture_entries,2,2u<<8});put(inventory+0x70,std::uintptr_t{});
    put(fixture_entries,declaration);put(fixture_entries+8,weapon_spawn);put(fixture_entries+0x10,equipment);put(fixture_entries+0x18,equipment_decl);put(fixture_entries+0x38,declaration+0x1000);
    put(declaration,image_base+0x1b9ea48);put(equipment,image_base+0x1b53f58);put(equipment+8,equipment_decl);put(equipment+0x10,weapon);put(equipment+0x18,d2vr::rig::Identity{51,10});
    put(equipment_decl,image_base+0x1b53e18);put(equipment_decl+0x68,1);put(weapon,image_base+0x1ce1e98);put(weapon+0xa8,weapon_spawn);
    put(weapon+0x268,d2vr::rig::List{weapon_components,1,1u<<8});put(weapon_components,equipment);put(fixture_game+0x3130b0+(weapon_spawn>>16)*8ull,weapon);
}
}
extern "C" MH_STATUS WINAPI MH_CreateHook(LPVOID target,LPVOID detour,LPVOID* original){
    const unsigned index=reinterpret_cast<std::uintptr_t>(target)==image_base+Hooks::rvas[0]?0u:1u;
    std::memcpy(saved_code[index].data(),target,32);const auto prefix=Hooks::prefixes[index];
    auto* code=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));assert(code);
    std::memcpy(code,target,prefix);code[prefix]=code[prefix+14]=0xff;code[prefix+1]=code[prefix+15]=0x25;
    const auto continuation=reinterpret_cast<std::uintptr_t>(target)+prefix;std::memcpy(code+prefix+6,&continuation,8);std::memcpy(code+prefix+20,&detour,8);
    *original=code;return MH_OK;
}
extern "C" MH_STATUS WINAPI MH_EnableHook(LPVOID target){*static_cast<unsigned char*>(target)=0xe9;return MH_OK;}
extern "C" MH_STATUS WINAPI MH_DisableHook(LPVOID target){
    const unsigned index=reinterpret_cast<std::uintptr_t>(target)==image_base+Hooks::rvas[0]?0u:1u;
    std::memcpy(target,saved_code[index].data(),32);return MH_OK;
}
namespace d2vr {
struct NativeManualEquipHooksFixture {
    static Hooks& make(){return *new Hooks;}
    static void processor(Hooks& hooks,const Policy::Params& params){
        auto* previous=hooks.originals_[0].exchange(reinterpret_cast<void*>(&native_processor));
        __try{hooks.invoke_processor(image_base+0x837222,equip,&params);}__finally{hooks.originals_[0]=previous;}
    }
    static void wheel(Hooks& hooks,std::uintptr_t caller,std::uintptr_t item=declaration){
        auto* previous=hooks.originals_[1].exchange(reinterpret_cast<void*>(&native_wheel));
        __try{hooks.invoke_wheel(caller,fixture_controller,item,0);}__finally{hooks.originals_[1]=previous;}
    }
};
struct NativeManualEquipRuntimeFixture {
    static void near_exhaustion(Runtime& runtime){runtime.generation_=UINT64_MAX-1;}
    static bool balanced(){return Runtime::wheel_depth_==0 && Runtime::wheel_overflow_==0;}
    static void lose_pending_authority(Runtime& runtime){runtime.policy_.revoke(runtime.generation_+1);}
    static void unarm(Runtime& runtime){runtime.armed_=false;}
};
}
namespace {
bool seh_wheel(Hooks* hooks){
    __try{d2vr::NativeManualEquipHooksFixture::wheel(*hooks,image_base+Policy::wheel_return_rva);}
    __except(GetExceptionCode()==0xe0553001?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){return true;}
    return false;
}
struct Submission {Runtime* runtime{};Hooks* hooks{};d2vr::NativePlayerStateLease owner;unsigned mode{};unsigned calls{};};
void submit_holster(void* context,std::uintptr_t target,const Policy::Params* value){
    auto& submission=*static_cast<Submission*>(context);assert(target==fixture_machine);++submission.calls;
    assert(!submission.runtime->reserve_hip(submission.owner) && !submission.runtime->begin_wheel(submission.owner));
    assert(!submission.runtime->commit_wheel(value->get<std::uint32_t>(0)));
    assert(submission.runtime->status().holster_pending);
    d2vr::NativeManualEquipHooksFixture::processor(*submission.hooks,*value);
    assert(submission.runtime->service_game_callback(fixture_machine) && submission.runtime->status().holster_pending);
    put(params_records,*value);put(equip+0x30,d2vr::rig::List{params_records,1,2u<<8});
    put(queue_records,std::array<std::uint32_t,3>{1,64,value->get<std::uint32_t>(0)});
    put(fixture_machine+0x220,d2vr::rig::List{queue_records,1,2u<<8});
    if(submission.mode==1)put(params_records+0x30,*value),put(equip+0x38,std::uint32_t{2});
    if(submission.mode==2)put(params_records+0x2f,static_cast<unsigned char>(0));
    if(submission.mode==3)throw std::runtime_error("native holster");
    if(submission.mode==4)RaiseException(0xe0553002,0,0,nullptr);
    if(submission.mode==5){auto publication=Runtime::Publication{submission.owner,2,false,true};submission.runtime->publish(publication);}
    if(submission.mode==6)put(fixture_machine+0x18,d2vr::rig::Identity{501,10});
    if(submission.mode==7)memory.erase(params_records+0x2f);
    if(submission.mode==8)put(fixture_machine+0x228,std::uint32_t{});
    if(submission.mode==9)assert(submission.runtime->disable());
    if(submission.mode==10)submission.runtime->publish(Runtime::Publication{submission.owner,1,false,false});
    if(submission.mode==12){auto replacement=submission.owner;++replacement.spawn;submission.runtime->publish(Runtime::Publication{replacement,1,false,true});}
}
bool seh_holster(Runtime* runtime,Submission* submission,const Policy::Params* value,Runtime::Token release){
    __try{runtime->execute_holster(release,submission->owner,1,fixture_machine,*value,&submit_holster,submission);}
    __except(GetExceptionCode()==0xe0553002?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){return true;}
    return false;
}
void barrier_cases(const d2vr::NativePlayerStateLease& owner){
    using Fixture=d2vr::NativeManualEquipHooksFixture;
    for(unsigned mode=0;mode<=12;++mode){
        put(fixture_machine+0x18,d2vr::rig::Identity{500,10});put(equip+0x30,d2vr::rig::List{params_records,0,2u<<8});
        put(fixture_machine+0x220,d2vr::rig::List{queue_records,0,2u<<8});
        auto& hooks=Fixture::make();Runtime runtime(hooks);assert(runtime.install(image_base,read_memory));
        Runtime::Publication publication{owner,1,false,true};runtime.publish(publication);assert(runtime.service_game_callback(fixture_machine));
        auto value=params(0);value.set(0,std::uint32_t{100+mode});const auto release=runtime.holster();Submission submission{&runtime,&hooks,owner,mode};
        if(mode==3){bool caught=false;try{runtime.execute_holster(release,owner,1,fixture_machine,value,&submit_holster,&submission);}catch(const std::runtime_error&){caught=true;}assert(caught);}
        else if(mode==4)assert(seh_holster(&runtime,&submission,&value,release));
        else assert(runtime.execute_holster(release,owner,1,fixture_machine,value,&submit_holster,&submission)==(mode==0 || mode==10 || mode==11));
        assert(submission.calls==1 && runtime.status().holster_pending);
        assert(!runtime.grant_hip(release,fixture_machine) && runtime.status().last_hip_attempt.reason==Runtime::HipReject::barrier);
        assert(!runtime.reserve_hip(owner) && !runtime.begin_wheel(owner));
        if(mode==0){
            assert(runtime.status().holster_receipts==1 && !runtime.status().holster_poisoned);
            put(fixture_machine+0x220,d2vr::rig::List{0,0,0});assert(runtime.service_game_callback(fixture_machine));
            assert(runtime.status().holster_pending && !runtime.reserve_hip(owner));
            put(equip+0x38,std::uint32_t{});put(fixture_machine+0x220,d2vr::rig::List{queue_records,1,2u<<8});
            assert(runtime.service_game_callback(fixture_machine) && runtime.status().holster_pending);
            put(fixture_machine+0x220,d2vr::rig::List{0,0,0});
            native_action=[&]{assert(runtime.service_game_callback(fixture_machine) && runtime.status().holster_pending);};
            Fixture::processor(hooks,params(0x81));native_action={};
            put(equip+0x150,static_cast<unsigned char>(1));assert(runtime.service_game_callback(fixture_machine));
            assert(runtime.status().holster_pending);put(equip+0x150,static_cast<unsigned char>(0));
            put(equip+0x108,std::uintptr_t{1});assert(runtime.service_game_callback(fixture_machine));assert(runtime.status().holster_pending);
            put(equip+0x108,std::uintptr_t{});assert(runtime.service_game_callback(fixture_machine));
            assert(!runtime.status().holster_pending && runtime.status().holster_retirements==1 && runtime.reserve_hip(owner));
            assert(!runtime.execute_holster(release,owner,1,fixture_machine,value,&submit_holster,&submission) && submission.calls==1);
            assert(runtime.ordinary_intent() && !runtime.execute_holster(release,owner,1,fixture_machine,value,&submit_holster,&submission));
        }else if(mode==10 || mode==11){
            assert(runtime.status().holster_receipts==1 && !runtime.status().holster_poisoned);
            publication.focused=false;runtime.publish(publication);
            put(fixture_machine+0x220,d2vr::rig::List{0,0,0});put(equip+0x38,std::uint32_t{});
            assert(!runtime.service_game_callback(fixture_machine) && runtime.status().holster_pending && !runtime.status().holster_poisoned);
            assert(!runtime.reserve_hip(owner) && !runtime.begin_wheel(owner) && !runtime.status().holster_retirements);
            publication.focused=true;runtime.publish(publication);assert(runtime.service_game_callback(fixture_machine));
            assert(!runtime.status().holster_pending && !runtime.status().holster_poisoned && runtime.status().holster_retirements==1);
            assert(runtime.reserve_hip(owner));
        }else{
            assert(runtime.status().holster_poisoned && runtime.status().holster_gaps);
            put(fixture_machine+0x18,d2vr::rig::Identity{500,10});put(equip+0x30,d2vr::rig::List{params_records,0,2u<<8});
            put(fixture_machine+0x220,d2vr::rig::List{queue_records,0,2u<<8});runtime.publish(publication);runtime.service_game_callback(fixture_machine);
            assert(runtime.status().holster_pending && !runtime.reserve_hip(owner));
        }
        assert(runtime.disable());
        if(mode && mode!=10 && mode!=11)assert(!runtime.install(image_base,read_memory));
    }
}
void host_sequence_cases(const d2vr::NativePlayerStateLease& owner){
    using Fixture=d2vr::NativeManualEquipHooksFixture;
    for(unsigned scenario=0;scenario<4;++scenario){
        put(inventory+0x70,std::uintptr_t{});auto& hooks=Fixture::make();Runtime runtime(hooks);
        assert(runtime.install(image_base,read_memory));Runtime::Publication publication{owner,1,false,true};runtime.publish(publication);
        assert(runtime.service_game_callback(fixture_machine));d2vr::GunGrip gun;d2vr::GripIntent intent;
        d2vr::GunGrip::In input{};input.grip=.8f;input.gameplay=input.zone=input.empty=input.loaded=true;input.now=1000;
        auto arbitration=intent.update(input.grip,input.trigger,input.now,true,100,true);
        input.draw_authorized=arbitration.use;auto output=gun.update(input);assert(output.reserved && !output.owns);
        input.now=1101;arbitration=intent.update(input.grip,input.trigger,input.now,true,100,true);
        input.draw_authorized=arbitration.use && !arbitration.fist;output=gun.update(input);
        assert(output.action==d2vr::GunGrip::Action::draw && output.owns);const auto token=runtime.reserve_hip(owner);assert(token);
        if(scenario==0 || scenario==2)assert(runtime.grant_hip(token,fixture_machine));
        input.trigger=.8f;const bool gun_owns=output.owns,actually_holding=scenario==3;
        publication.physical_left_occupied=actually_holding || (!gun_owns && (arbitration.fist || (input.grip>.55f && input.trigger>.55f)));
        runtime.publish(publication);
        const auto after_arbitration=intent.update(input.grip,input.trigger,1117,!gun_owns,100,true);
        assert(!after_arbitration.fist && intent.kind()==d2vr::GripIntent::Kind::drain);
        if(scenario==3){
            assert(publication.physical_left_occupied==true && runtime.status().generation!=token && !runtime.grant_hip(token,fixture_machine));
            assert(runtime.status().last_hip_attempt.reason==Runtime::HipReject::stale_action);
        }else{
            assert(publication.physical_left_occupied==false && runtime.status().generation==token);
            if(scenario==1)assert(runtime.grant_hip(token,fixture_machine));
            Fixture::processor(hooks,params(4));assert(received.get<std::uint32_t>(24)==4 && !runtime.status().vetoes);
            put(inventory+0x70,fixture_entries);assert(runtime.hip_item_current(token));
            const auto status=runtime.status();assert(status.hip_grants==1 && status.last_hip_attempt.reason==Runtime::HipReject::accepted);
            assert(status.last_hip_attempt.token==token && status.last_hip_attempt.current_generation==token && status.last_hip_attempt.owner==owner);
            assert(status.last_hip_attempt.owner_epoch==1 && status.last_hip_attempt.machine==fixture_machine && status.last_hip_attempt.selected_known);
            assert(status.last_hip_attempt.selected_item==fixture_entries && !status.last_hip_attempt.selected_ammo &&
                status.last_hip_attempt.occupancy_known && !status.last_hip_attempt.occupied);
            assert(status.hip_rejections[static_cast<std::size_t>(Runtime::HipReject::accepted)]==1);
            assert(runtime.ordinary_intent());Fixture::processor(hooks,params(4));assert(received.get<std::uint32_t>(24)==0 && runtime.status().vetoes==1);
        }
        assert(runtime.disable());
    }
    put(inventory+0x70,std::uintptr_t{});
}
void retained_lifecycle_cases(const d2vr::NativePlayerStateLease& owner){
    using Fixture=d2vr::NativeManualEquipHooksFixture;
    auto& hooks=Fixture::make();Runtime runtime(hooks);assert(runtime.install(image_base,read_memory));
    Runtime::Publication publication{owner,1,false,true};runtime.publish(publication);assert(runtime.service_game_callback(fixture_machine));
    const auto release=runtime.holster();assert(release);Fixture::processor(hooks,params(0));
    put(inventory+0x70,std::uintptr_t{});put(equipment+0x18,d2vr::rig::Identity{22897,0x20000002});
    const auto token=runtime.reserve_hip(owner);assert(token && runtime.grant_hip(token,fixture_machine));
    const auto draw=params(4);native_action=[]{put(equipment+0x18,d2vr::rig::Identity{22897,0x20000006});put(inventory+0x70,fixture_entries);};
    const auto calls=processor_calls;Fixture::processor(hooks,draw);native_action={};assert(processor_calls==calls+1 && received==draw);
    assert(runtime.hip_item_current(token));Fixture::processor(hooks,draw);assert(received==draw && runtime.hip_item_current(token));
    put(equipment+0x18,d2vr::rig::Identity{22897,0x2000000a});Fixture::processor(hooks,draw);assert(received==draw && runtime.hip_item_current(token));
    put(equipment+0x18,d2vr::rig::Identity{22898,0x2000000a});assert(!runtime.hip_item_current(token));Fixture::processor(hooks,draw);assert(received.get<std::uint32_t>(24)==0);
    put(equipment+0x18,d2vr::rig::Identity{22897,0x20000002});assert(runtime.ordinary_intent());
    for(const auto flags:std::array<std::uint32_t,3>{0x20000002,0x20000006,0x2000000a}){
        put(equipment+0x18,d2vr::rig::Identity{22897,flags});Fixture::processor(hooks,draw);assert(received.get<std::uint32_t>(24)==0 && !runtime.hip_item_current(token));
    }
    const auto fresh=runtime.reserve_hip(owner);assert(fresh && runtime.grant_hip(fresh,fixture_machine));
    put(equipment+0x18,d2vr::rig::Identity{22897,0x2000001a});assert(!runtime.hip_item_current(fresh));Fixture::processor(hooks,draw);assert(received.get<std::uint32_t>(24)==0);
    assert(runtime.disable());put(equipment+0x18,d2vr::rig::Identity{51,10});put(inventory+0x70,std::uintptr_t{});
}
void hip_reason_cases(const d2vr::NativePlayerStateLease& owner){
    using Fixture=d2vr::NativeManualEquipHooksFixture;
    for(const auto reason:{Runtime::HipReject::context,Runtime::HipReject::idle_arm,Runtime::HipReject::unknown_occupancy,
                          Runtime::HipReject::occupied,Runtime::HipReject::selected_missing,Runtime::HipReject::scope,Runtime::HipReject::policy_grant}){
        auto& hooks=Fixture::make();Runtime runtime(hooks);assert(runtime.install(image_base,read_memory));
        Runtime::Publication publication{owner,1,false,true};runtime.publish(publication);assert(runtime.service_game_callback(fixture_machine));
        if(reason==Runtime::HipReject::unknown_occupancy){publication.physical_left_occupied=std::nullopt;runtime.publish(publication);}
        if(reason==Runtime::HipReject::occupied){publication.physical_left_occupied=true;runtime.publish(publication);}
        const auto token=runtime.reserve_hip(owner);assert(token);
        if(reason==Runtime::HipReject::idle_arm){d2vr::NativeManualEquipRuntimeFixture::unarm(runtime);put(equip+0x108,std::uintptr_t{1});}
        if(reason==Runtime::HipReject::selected_missing)put(fixture_controller+0x190,std::uintptr_t{});
        if(reason==Runtime::HipReject::scope)put(equipment_decl+0x68,2);
        if(reason==Runtime::HipReject::policy_grant)d2vr::NativeManualEquipRuntimeFixture::lose_pending_authority(runtime);
        assert(!runtime.grant_hip(token,reason==Runtime::HipReject::context?fixture_machine+8:fixture_machine));
        const auto status=runtime.status();assert(status.last_hip_attempt.reason==reason && status.last_hip_attempt.token==token);
        assert(status.hip_rejections[static_cast<std::size_t>(reason)]==1 && !status.hip_grants);
        if(reason==Runtime::HipReject::unknown_occupancy)assert(!status.last_hip_attempt.occupancy_known);
        if(reason==Runtime::HipReject::occupied)assert(status.last_hip_attempt.occupancy_known && status.last_hip_attempt.occupied);
        if(reason==Runtime::HipReject::selected_missing)assert(status.last_hip_attempt.selected_known && !status.last_hip_attempt.selected_item);
        put(equip+0x108,std::uintptr_t{});put(fixture_controller+0x190,fixture_entries);put(equipment_decl+0x68,1);
        assert(runtime.disable());
    }
}
}
namespace d2vr {
struct NativeWeaponExchangeFixture {
    using Hands=NativeWeaponHands;
    inline static Runtime* runtime_{};
    inline static std::array<unsigned,128> events_{};
    inline static unsigned event_count_{},hide_mode_{},show_mode_{};
    inline static bool refuse_show_cleanup_{};
    static constexpr std::uintptr_t clone=0x217000000,other_clone=0x218000000,clone_table=0x219000000;
    static constexpr std::uint32_t clone_spawn=0x30003,other_spawn=0x40004;
    static void event(unsigned value){assert(event_count_<events_.size());events_[event_count_++]=value;}
    static void unlocked(){assert(Hands::m_.try_lock());Hands::m_.unlock();assert(runtime_ && runtime_->status().installed);}
    static void __fastcall hide(std::uintptr_t entity) {
        assert(entity==clone || entity==other_clone);unlocked();event(1);
        if(hide_mode_==1)assert(runtime_->ordinary_intent());
        if(hide_mode_==2)put(inventory+0x70,fixture_entries);
        if(hide_mode_==3)put(equip+0x20,static_cast<unsigned char>(1));
        if(hide_mode_==5)Sleep(270);
    }
    static void __fastcall show(std::uintptr_t entity) {
        assert(entity==clone || entity==other_clone);unlocked();event(2);
        if(show_mode_==1 || show_mode_==4)assert(runtime_->ordinary_intent());
        if(show_mode_==2)put(inventory+0x70,fixture_entries);
        if(show_mode_==3)put(equip+0x20,static_cast<unsigned char>(1));
        if(show_mode_==4)put(entity+0xa8,std::uint32_t{entity==clone?clone_spawn+1:other_spawn+1});
        if(show_mode_==5)Sleep(270);
        if(refuse_show_cleanup_)put(clone_table+0x80,std::uintptr_t{0x233000000});
    }
    static void __fastcall teleport(std::uintptr_t entity,const float*,const float*,int){assert(entity==clone || entity==other_clone);event(3);}
    static void* __fastcall find_decl(std::uintptr_t kind,const char* name,bool,bool) {
        assert(kind==image_base+0x2434170 && name);event(6);return reinterpret_cast<void*>(declaration);
    }
    static std::uintptr_t __fastcall spawn_clone(std::uintptr_t game,void*,const float*,const float*,std::uintptr_t owner,bool hidden,const float*,const float*,const float*) {
        assert(game==fixture_game && !owner && !hidden);event(7);memory.erase(clone+0xa8);return clone;
    }
    static void __fastcall make_params(void* output,void* item,int code,int requester) {
        assert(!item && code==4 && requester==0x5b);unlocked();event(4);const auto value=params(4);std::memcpy(output,&value,sizeof(value));
    }
    static void __fastcall submit(std::uintptr_t target,int state,void* value) {
        assert(target==fixture_machine && state==64);unlocked();event(5);
        const auto& record=*static_cast<Policy::Params*>(value);assert(record.get<std::uint32_t>(24)==4);
        put(params_records,record);put(equip+0x30,rig::List{params_records,1,2u<<8});
        put(queue_records,std::array<std::uint32_t,3>{1,64,record.get<std::uint32_t>(0)});
        put(fixture_machine+0x220,rig::List{queue_records,1,2u<<8});
    }
    static void jump(std::uintptr_t target,std::uintptr_t callback) {
        std::array<unsigned char,14> code{0xff,0x25};std::memcpy(code.data()+6,&callback,sizeof(callback));
        std::memcpy(reinterpret_cast<void*>(target),code.data(),code.size());
    }
    static void native_empty() {
        put(inventory+0x70,std::uintptr_t{});put(equip+0x20,static_cast<unsigned char>(0));
        put(equip+0x150,static_cast<unsigned char>(0));put(equip+0x108,std::uintptr_t{});put(equip+0x118,std::uintptr_t{});
        put(equip+0x30,rig::List{params_records,0,2u<<8});put(fixture_machine+0x220,rig::List{queue_records,0,2u<<8});
    }
    static void clones(unsigned shown_kind=0) {
        for(unsigned kind=0;kind<2;++kind) {
            const auto entity=kind?other_clone:clone;const auto spawn=kind?other_spawn:clone_spawn;
            put(fixture_game+0x3130b0+(spawn>>16)*8ull,entity);put(entity+0xa8,spawn);put(entity,clone_table);
        }
        put(clone_table+0x80,image_base+0x8784b0);put(clone_table+0x88,image_base+0x87c590);
        Hands::hip_ent_={clone,other_clone};Hands::hip_id_={clone_spawn,other_spawn};
        const auto context=runtime_->hip_context();assert(context);Hands::hip_owner_lease_={context->owner,context->owner};
        Hands::hip_shown_={shown_kind==0,shown_kind==1};Hands::hip_owner_={0,0};Hands::emily_=0;
        Hands::hip_req_={};Hands::hip_exchange_={};hide_mode_=show_mode_=event_count_=0;refuse_show_cleanup_=false;
    }
    static void publish_hip(Runtime& runtime,unsigned kind=0,int avatar=0) {
        while(GetTickCount64()<=Hands::hip_exchange_.publication_fence)Sleep(1);
        Hands::hip({true,kind,avatar,{}, {1,0,0,0,1,0,0,0,1},GetTickCount64()},runtime);
    }
    static bool draw(Runtime& runtime,const NativePlayerStateLease& owner) {
        Hands::Request request{};request.op=Hands::Op::gun_draw;request.manual_generation=runtime.reserve_hip(owner);assert(request.manual_generation);
        auto reader=read_memory;return Hands::service_gun_draw(image_base,reader,request,runtime);
    }
    static void run(const NativePlayerStateLease& owner) {
        auto& hooks=NativeManualEquipHooksFixture::make();Runtime runtime(hooks);runtime_=&runtime;
        assert(runtime.install(image_base,read_memory));runtime.publish(Runtime::Publication{owner,1,false,true});assert(runtime.service_game_callback(fixture_machine));
        put(fixture_body+0x60,inventory);native_empty();
        jump(image_base+0x8784b0,reinterpret_cast<std::uintptr_t>(&hide));jump(image_base+0x87c590,reinterpret_cast<std::uintptr_t>(&show));
        jump(image_base+0x87d1e0,reinterpret_cast<std::uintptr_t>(&teleport));jump(image_base+0x835590,reinterpret_cast<std::uintptr_t>(&make_params));
        jump(image_base+0x1e030,reinterpret_cast<std::uintptr_t>(&find_decl));jump(image_base+0x8ae9b0,reinterpret_cast<std::uintptr_t>(&spawn_clone));
        jump(image_base+0x8024d0,reinterpret_cast<std::uintptr_t>(&submit));auto reader=read_memory;
        clones();publish_hip(runtime);assert(draw(runtime,owner));
        assert(event_count_==3 && events_[0]==1 && events_[1]==4 && events_[2]==5 && !Hands::hip_shown_[0]);
        assert(Hands::hip_exchange_.blocked && !Hands::hip_req_.show);publish_hip(runtime);Hands::service_hip(image_base,reader,runtime);
        assert(Hands::hip_exchange_.blocked && !Hands::hip_shown_[0] && event_count_==3);
        assert(runtime.ordinary_intent());publish_hip(runtime);Hands::service_hip(image_base,reader,runtime);assert(Hands::hip_exchange_.blocked);
        const auto wheel_open=runtime.begin_wheel(owner);assert(wheel_open && runtime.commit_wheel(wheel_open));
        native_action=[] {native_empty();};NativeManualEquipHooksFixture::wheel(hooks,image_base+Policy::wheel_return_rva);native_action={};
        publish_hip(runtime);Hands::service_hip(image_base,reader,runtime);assert(Hands::hip_exchange_.blocked && !Hands::hip_shown_[0]);
        const auto future_hip=runtime.reserve_hip(owner);assert(future_hip && runtime.grant_hip(future_hip,fixture_machine));
        publish_hip(runtime);Hands::service_hip(image_base,reader,runtime);assert(Hands::hip_exchange_.blocked && !Hands::hip_shown_[0]);
        assert(runtime.cancel(future_hip));
        const auto empty_wheel=runtime.begin_wheel(owner);assert(empty_wheel && runtime.commit_wheel(empty_wheel));
        native_action=[] {native_empty();};NativeManualEquipHooksFixture::wheel(hooks,image_base+Policy::wheel_return_rva,declaration+0x1000);native_action={};
        publish_hip(runtime);Hands::service_hip(image_base,reader,runtime);
        assert(!Hands::hip_exchange_.blocked && !Hands::hip_req_.show && !Hands::hip_shown_[0]);
        Hands::service_hip(image_base,reader,runtime);assert(!Hands::hip_shown_[0]);
        publish_hip(runtime);Hands::service_hip(image_base,reader,runtime);assert(Hands::hip_shown_[0] && events_[3]==2 && events_[4]==3);
        for(unsigned mode=0;mode<4;++mode) {
            native_empty();clones();publish_hip(runtime);hide_mode_=mode;
            if(mode==0)put(clone_table+0x80,std::uintptr_t{0x233000000});
            assert(!draw(runtime,owner));assert(Hands::hip_ent_[0]==clone && Hands::hip_id_[0]==clone_spawn);
            assert(event_count_==(mode?1u:0u) && Hands::hip_shown_[0]==(mode==0));
            assert(Hands::hip_exchange_.blocked);
        }
        hide_mode_=0;native_empty();assert(runtime.ordinary_intent());clones();
        Hands::hip_id_[0]=0;publish_hip(runtime);assert(!draw(runtime,owner));
        assert(Hands::hip_ent_[0]==clone && !Hands::hip_id_[0] && Hands::hip_shown_[0] && !event_count_);
        publish_hip(runtime);Hands::service_hip(image_base,reader,runtime);
        assert(Hands::hip_ent_[0]==clone && Hands::hip_shown_[0] && !Hands::hip_shown_[1] && !event_count_);
        native_empty();clones();Hands::hip_ent_={};Hands::hip_id_={};Hands::hip_shown_={};Hands::hip_owner_lease_={};publish_hip(runtime);
        Hands::service_hip(image_base,reader,runtime);
        assert(Hands::hip_ent_[0]==clone && !Hands::hip_id_[0] && Hands::hip_shown_[0] && event_count_==2 && events_[0]==6 && events_[1]==7);
        Hands::service_hip(image_base,reader,runtime);assert(event_count_==2 && Hands::hip_ent_[0]==clone && !Hands::hip_id_[0]);
        assert(!draw(runtime,owner) && event_count_==2 && Hands::hip_ent_[0]==clone && Hands::hip_shown_[0]);
        native_empty();clones();
        put(clone+0xa8,std::uint32_t{clone_spawn+1});assert(draw(runtime,owner));
        assert(!Hands::hip_ent_[0] && !Hands::hip_id_[0] && !Hands::hip_shown_[0] && event_count_==2 && events_[0]==4 && events_[1]==5);
        native_empty();assert(runtime.ordinary_intent());
        for(unsigned desired=0;desired<2;++desired) {
            clones(1-desired);publish_hip(runtime,desired);put(clone_table+0x80,std::uintptr_t{0x233000000});
            Hands::service_hip(image_base,reader,runtime);assert(Hands::hip_shown_[1-desired] && !Hands::hip_shown_[desired] && !event_count_);
            put(clone_table+0x80,image_base+0x8784b0);Hands::service_hip(image_base,reader,runtime);
            assert(!Hands::hip_shown_[1-desired] && Hands::hip_shown_[desired] && events_[0]==1 && events_[1]==2);
        }
        for(unsigned desired=0;desired<2;++desired)for(unsigned mode=1;mode<=3;++mode)for(bool existing:{false,true}) {
            native_empty();clones(1-desired);
            if(!existing){Hands::hip_ent_[desired]=0;Hands::hip_id_[desired]=0;Hands::hip_owner_lease_[desired].reset();}
            publish_hip(runtime,desired);hide_mode_=mode;Hands::service_hip(image_base,reader,runtime);
            assert(event_count_==1 && events_[0]==1 && !Hands::hip_shown_[0] && !Hands::hip_shown_[1]);
            assert(Hands::hip_ent_[desired]==(existing?(desired?other_clone:clone):0));
        }
        native_empty();hide_mode_=0;
        for(unsigned desired=0;desired<2;++desired)for(unsigned mode=1;mode<=3;++mode)for(bool refused:{false,true}) {
            native_empty();clones();Hands::hip_shown_={false,false};publish_hip(runtime,desired);
            show_mode_=mode;refuse_show_cleanup_=refused;Hands::service_hip(image_base,reader,runtime);
            assert(events_[0]==2 && event_count_==(refused?1u:2u));if(!refused)assert(events_[1]==1);
            assert(Hands::hip_shown_[desired]==refused && !Hands::hip_shown_[1-desired]);
        }
        native_empty();clones();Hands::hip_shown_={false,false};publish_hip(runtime);show_mode_=4;
        Hands::service_hip(image_base,reader,runtime);assert(event_count_==1 && events_[0]==2 && Hands::hip_ent_[0]==clone);
        native_empty();clones();
        for(unsigned desired=0;desired<2;++desired) {
            native_empty();clones(1-desired);publish_hip(runtime,desired);hide_mode_=5;
            Hands::service_hip(image_base,reader,runtime);
            assert(event_count_==1 && events_[0]==1 && !Hands::hip_shown_[0] && !Hands::hip_shown_[1]);
        }
        for(bool refused:{false,true}) {
            native_empty();clones();Hands::hip_shown_={false,false};publish_hip(runtime);show_mode_=5;refuse_show_cleanup_=refused;
            Hands::service_hip(image_base,reader,runtime);
            assert(events_[0]==2 && event_count_==(refused?1u:2u));if(!refused)assert(events_[1]==1);
            assert(Hands::hip_ent_[0]==clone && Hands::hip_id_[0]==clone_spawn && Hands::hip_shown_[0]==refused);
        }
        native_empty();clones();
        clones();publish_hip(runtime,0,1);put(clone_table+0x80,std::uintptr_t{0x233000000});
        Hands::service_hip(image_base,reader,runtime);assert(Hands::hip_ent_[0]==clone && Hands::hip_shown_[0] && Hands::hip_owner_[0]==0 && !event_count_);
        clones();auto replacement_owner=owner;++replacement_owner.spawn;put(fixture_player+0xa8,replacement_owner.spawn);
        runtime.publish(Runtime::Publication{replacement_owner,1,false,true});publish_hip(runtime);put(clone_table+0x80,std::uintptr_t{0x233000000});
        Hands::service_hip(image_base,reader,runtime);assert(Hands::hip_ent_[0]==clone && Hands::hip_shown_[0] && !event_count_);
        put(clone_table+0x80,image_base+0x8784b0);Hands::Hip no_show{};no_show.owner=0;no_show.tick=GetTickCount64();Hands::hip(no_show,runtime);
        Hands::service_hip(image_base,reader,runtime);assert(!Hands::hip_ent_[0] && event_count_==1 && events_[0]==1);
        put(fixture_player+0xa8,owner.spawn);runtime.publish(Runtime::Publication{owner,1,false,true});
        clones();publish_hip(runtime);const auto stale=Hands::hip_req_;
        assert(runtime.ordinary_intent());Hands::service_hip(image_base,reader,runtime);assert(!Hands::hip_shown_[0]);
        Hands::hip_req_=stale;Hands::service_hip(image_base,reader,runtime);assert(!Hands::hip_shown_[0]);
        publish_hip(runtime);const auto context=runtime.hip_context();assert(context && runtime.hip_empty_current(*context));
        put(queue_records,std::array<std::uint32_t,3>{7,53,1});put(fixture_machine+0x220,rig::List{queue_records,1,2u<<8});assert(runtime.hip_empty_current(*context));
        put(queue_records,std::array<std::uint32_t,3>{7,64,1});assert(!runtime.hip_empty_current(*context));native_empty();
        put(inventory+0x70,fixture_entries);assert(!runtime.hip_empty_current(*context));native_empty();
        put(equip+0x150,static_cast<unsigned char>(1));assert(!runtime.hip_empty_current(*context));native_empty();
        put(equip+0x108,std::uintptr_t{1});assert(!runtime.hip_empty_current(*context));native_empty();
        put(equip+0x118,std::uintptr_t{1});assert(!runtime.hip_empty_current(*context));native_empty();
        put(hold+0x20,static_cast<unsigned char>(1));assert(!runtime.hip_empty_current(*context));put(hold+0x20,static_cast<unsigned char>(0));
        runtime.publish(Runtime::Publication{owner,1,true,true});const auto occupied_context=runtime.hip_context();
        assert(occupied_context && !runtime.hip_empty_current(*occupied_context));runtime.publish(Runtime::Publication{owner,1,false,true});
        runtime.publish(Runtime::Publication{owner,1,std::nullopt,true});const auto unknown_context=runtime.hip_context();
        assert(unknown_context && !runtime.hip_empty_current(*unknown_context));runtime.publish(Runtime::Publication{owner,1,false,true});
        runtime.publish(Runtime::Publication{owner,2,false,true});assert(!runtime.hip_empty_current(*context));
        Hands::service_hip(image_base,reader,runtime);assert(!Hands::hip_shown_[0]);
        runtime.publish(Runtime::Publication{owner,1,false,true});native_empty();assert(runtime.disable());runtime_=nullptr;
        Hands::hip_ent_={};Hands::hip_id_={};Hands::hip_shown_={};Hands::hip_owner_={-1,-1};Hands::hip_owner_lease_={};Hands::hip_req_={};Hands::hip_exchange_={};Hands::emily_=-1;
    }
};
}
int main(){
    using Fixture=d2vr::NativeManualEquipHooksFixture;seed();
    auto reader=read_memory;const auto owner=d2vr::native_player_state_lease(reader,image_base,fixture_machine);assert(owner);
    d2vr::NativeWeaponExchangeFixture::run(*owner);
    auto& hooks=Fixture::make();Runtime runtime(hooks);assert(runtime.install(image_base,read_memory) && !runtime.status().writer_enabled);
    Runtime::Publication publication{owner,1,false,true};runtime.publish(publication);
    put(equip+0x108,std::uintptr_t{1});assert(!runtime.service_game_callback(fixture_machine));put(equip+0x108,std::uintptr_t{});
    assert(runtime.service_game_callback(fixture_machine) && runtime.status().writer_enabled);
    const auto release_generation=runtime.holster();assert(release_generation && release_generation==runtime.status().generation);
    assert(runtime.allow_holster(release_generation,*owner,publication.xr_epoch,fixture_machine));
    assert(runtime.ordinary_intent() && runtime.allow_holster(release_generation,*owner,publication.xr_epoch,fixture_machine));
    assert(release_generation<runtime.status().generation);
    const auto before_allow=runtime.status().generation;
    assert(runtime.allow_holster(release_generation,*owner,publication.xr_epoch,fixture_machine) && runtime.status().generation==before_allow);
    assert(!runtime.allow_holster(release_generation,*owner,publication.xr_epoch+1,fixture_machine));
    auto different_owner=*owner;++different_owner.spawn;
    assert(!runtime.allow_holster(release_generation,different_owner,publication.xr_epoch,fixture_machine));
    publication.owner=different_owner;runtime.publish(publication);
    assert(!runtime.allow_holster(release_generation,*owner,publication.xr_epoch,fixture_machine));publication.owner=owner;runtime.publish(publication);
    put(fixture_machine+0x18,d2vr::rig::Identity{501,10});
    assert(!runtime.allow_holster(release_generation,*owner,publication.xr_epoch,fixture_machine));put(fixture_machine+0x18,d2vr::rig::Identity{500,10});
    assert(!runtime.allow_holster(release_generation,*owner,publication.xr_epoch,fixture_machine+8));
    publication.focused=false;runtime.publish(publication);assert(!runtime.allow_holster(release_generation,*owner,publication.xr_epoch,fixture_machine));
    publication.focused=true;runtime.publish(publication);assert(runtime.service_game_callback(fixture_machine));
    const auto newer_hip=runtime.reserve_hip(*owner);assert(newer_hip && runtime.status().last_manual_generation==newer_hip);
    assert(!runtime.allow_holster(release_generation,*owner,publication.xr_epoch,fixture_machine));
    assert(runtime.ordinary_intent() && !runtime.allow_holster(release_generation,*owner,publication.xr_epoch,fixture_machine));
    const auto wheel_release=runtime.holster();assert(wheel_release && wheel_release==runtime.status().generation);
    const auto newer_open=runtime.begin_wheel(*owner);assert(newer_open && !runtime.allow_holster(wheel_release,*owner,publication.xr_epoch,fixture_machine));
    assert(runtime.ordinary_intent() && !runtime.allow_holster(wheel_release,*owner,publication.xr_epoch,fixture_machine));
    const auto final_release=runtime.holster();assert(final_release && final_release==runtime.status().generation);
    assert(runtime.ordinary_intent() && runtime.allow_holster(final_release,*owner,publication.xr_epoch,fixture_machine));
    auto restore=params(0x16);locks=1;const auto original_restore=restore;
    const auto first_processor_calls=processor_calls;Fixture::processor(hooks,restore);auto expected_restore=restore;expected_restore.set(24,std::uint32_t{0x12});
    assert(processor_calls==first_processor_calls+1 && !locks && received==expected_restore && restore==original_restore && runtime.status().vetoes==1);
    const auto first=runtime.reserve_hip(*owner);assert(first && runtime.status().hip_grants==0);
    assert(runtime.ordinary_intent() && !runtime.grant_hip(first,fixture_machine));
    const auto hip=runtime.reserve_hip(*owner);assert(hip && runtime.grant_hip(hip,fixture_machine) && !runtime.grant_hip(hip,fixture_machine));
    assert(!runtime.hip_item_current(hip));put(inventory+0x70,fixture_entries);
    const auto before_item_proof=runtime.status().generation;
    assert(runtime.hip_item_current(hip) && !runtime.hip_item_current(hip+1));
    assert(runtime.status().generation==before_item_proof);
    put(inventory+0x70,fixture_entries+0x38);assert(!runtime.hip_item_current(hip));put(inventory+0x70,fixture_entries);
    put(equipment+0x18,d2vr::rig::Identity{52,10});assert(!runtime.hip_item_current(hip));put(equipment+0x18,d2vr::rig::Identity{51,10});
    assert(runtime.hip_item_current(hip));put(inventory+0x70,std::uintptr_t{});
    locks=1;Fixture::processor(hooks,restore);assert(received==restore && !locks && runtime.status().hip_grants==1);
    auto holster=params(0);Fixture::processor(hooks,holster);locks=1;Fixture::processor(hooks,restore);assert(received.get<std::uint32_t>(24)==0x12 && !locks);
    put(inventory+0x70,fixture_entries);assert(!runtime.hip_item_current(hip));put(inventory+0x70,std::uintptr_t{});
    const auto open=runtime.begin_wheel(*owner);assert(open);
    Fixture::wheel(hooks,image_base+Policy::wheel_return_rva);assert(runtime.status().wheel_grants==0 && !runtime.commit_wheel(open));
    const auto opened=runtime.begin_wheel(*owner),commit=runtime.commit_wheel(opened);assert(commit);
    assert(runtime.status().last_manual_generation==commit);
    const auto pending_wheel_generation=runtime.status().generation;runtime.publish(publication);runtime.publish(publication);
    assert(runtime.status().wheel_grants==0 && runtime.status().generation==pending_wheel_generation);
    Fixture::wheel(hooks,image_base+0x4dff48);assert(runtime.status().wheel_grants==0);
    Fixture::wheel(hooks,image_base+Policy::wheel_return_rva);assert(runtime.status().wheel_grants==1);
    assert(d2vr::NativeManualEquipRuntimeFixture::balanced());
    auto lock=params(0x81);Fixture::processor(hooks,lock);locks=1;Fixture::processor(hooks,restore);assert(received==restore && !locks);
    assert(runtime.ordinary_intent());locks=1;Fixture::processor(hooks,restore);assert(received.get<std::uint32_t>(24)==0x12 && !locks);
    auto unknown=params(0x20);unknown.set(8,fixture_entries+0x38);Fixture::processor(hooks,unknown);assert(received==unknown);
    put(inventory+0x70,fixture_entries+0x38);locks=1;Fixture::processor(hooks,restore);assert(received==restore && !locks && runtime.status().observation.current_slot_gap);put(inventory+0x70,std::uintptr_t{});
    auto pending=runtime.reserve_hip(*owner);publication.physical_left_occupied=std::nullopt;runtime.publish(publication);assert(!runtime.grant_hip(pending,fixture_machine));
    publication.physical_left_occupied=false;runtime.publish(publication);
    pending=runtime.reserve_hip(*owner);put(hold+0x20,static_cast<unsigned char>(1));assert(!runtime.grant_hip(pending,fixture_machine));put(hold+0x20,static_cast<unsigned char>(0));
    pending=runtime.reserve_hip(*owner);publication.xr_epoch=2;runtime.publish(publication);assert(!runtime.grant_hip(pending,fixture_machine));assert(runtime.service_game_callback(fixture_machine));
    pending=runtime.reserve_hip(*owner);put(fixture_machine+0x18,d2vr::rig::Identity{501,10});assert(!runtime.grant_hip(pending,fixture_machine));
    put(fixture_machine+0x18,d2vr::rig::Identity{500,10});assert(!runtime.grant_hip(pending,fixture_machine));assert(runtime.service_game_callback(fixture_machine));
    assert(runtime.commit_wheel(runtime.begin_wheel(*owner)));put(fixture_entries+0x38,declaration);
    const auto grants_before_duplicate=runtime.status().wheel_grants;Fixture::wheel(hooks,image_base+Policy::wheel_return_rva);
    assert(runtime.status().wheel_grants==grants_before_duplicate);put(fixture_entries+0x38,declaration+0x1000);
    const auto wheel_open=runtime.begin_wheel(*owner);assert(runtime.commit_wheel(wheel_open));fail_native=1;bool caught{};
    try{Fixture::wheel(hooks,image_base+Policy::wheel_return_rva);}catch(const std::runtime_error&){caught=true;}assert(caught);fail_native=0;
    locks=1;Fixture::processor(hooks,restore);assert(received.get<std::uint32_t>(24)==0x12 && !locks);
    assert(runtime.commit_wheel(runtime.begin_wheel(*owner)));fail_native=2;assert(seh_wheel(&hooks));fail_native=0;
    locks=1;Fixture::processor(hooks,restore);assert(received.get<std::uint32_t>(24)==0x12 && !locks);
    assert(runtime.commit_wheel(runtime.begin_wheel(*owner)));native_action=[&]{assert(!runtime.disable(0));};Fixture::wheel(hooks,image_base+Policy::wheel_return_rva);native_action={};
    assert(runtime.disable() && runtime.install(image_base,read_memory) && !runtime.status().writer_enabled);runtime.publish(publication);assert(runtime.service_game_callback(fixture_machine));
    assert(runtime.commit_wheel(runtime.begin_wheel(*owner)));unsigned nesting{};
    const auto before_nested_wheels=wheel_calls;
    native_action=[&]{if(++nesting<9)Fixture::wheel(hooks,image_base+Policy::wheel_return_rva);else RaiseException(0xe0553001,0,0,nullptr);};
    assert(seh_wheel(&hooks) && nesting==9 && wheel_calls==before_nested_wheels+9 && d2vr::NativeManualEquipRuntimeFixture::balanced());native_action={};
    assert(runtime.disable() && runtime.install(image_base,read_memory) && runtime.service_game_callback(fixture_machine));
    pending=runtime.reserve_hip(*owner);publication.focused=false;runtime.publish(publication);assert(!runtime.grant_hip(pending,fixture_machine));publication.focused=true;runtime.publish(publication);assert(runtime.service_game_callback(fixture_machine));
    d2vr::NativeManualEquipRuntimeFixture::near_exhaustion(runtime);assert(!runtime.reserve_hip(*owner) && runtime.status().exhausted && !runtime.reserve_hip(*owner));
    assert(runtime.holster()==0);
    locks=1;Fixture::processor(hooks,restore);assert(received.get<std::uint32_t>(24)==0x12 && !locks);
    assert(runtime.disable());retained_lifecycle_cases(*owner);host_sequence_cases(*owner);hip_reason_cases(*owner);barrier_cases(*owner);auto* pin=reinterpret_cast<unsigned char*>(image_base+Policy::call_pins[0].rva);*pin^=1;
    assert(!runtime.install(image_base,read_memory));*pin^=1;
    std::cout<<"native_manual_equip_runtime_check passed (composed policy/hooks, stub native, no engine fixture_body execution)\n";
}
