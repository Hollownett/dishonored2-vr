#define NOMINMAX
#include "../src/native_interaction_aim.hpp"
#include <map>
#include <cassert>
#include <iostream>
#include <memory>
namespace {
constexpr std::uintptr_t fixture_base=0x140000000;
std::map<std::uintptr_t,unsigned char> fixture_bytes;
std::map<std::uintptr_t,std::array<unsigned char,32>> fixture_original_bytes;
unsigned fixture_created{},fixture_enabled{},fixture_obstruction_checks{};
bool fixture_checked_before_hook{};
std::uintptr_t fixture_fail_enable{};
void* fixture_slash_trampoline{};
std::array<void*,2> fixture_choke_trampolines{};
std::array<void*,2> fixture_offered_trampolines{};
std::uintptr_t fixture_mutate_at{},fixture_mutate_identity{};
bool fixture_read_fault{};
template<class T>void fixture_put(std::uintptr_t at,const T& value) {
    const auto* bytes=reinterpret_cast<const unsigned char*>(&value);
    for(std::size_t i=0;i<sizeof(value);++i)fixture_bytes[at+i]=bytes[i];
}
bool fixture_read(std::uintptr_t at,void* out,std::size_t size) {
    if(fixture_read_fault)RaiseException(0xe0000071,0,0,nullptr);
    const auto trampoline=reinterpret_cast<std::uintptr_t>(fixture_slash_trampoline);
    if(trampoline && at>=trampoline && at-trampoline<=22 && size<=22-(at-trampoline)) {
        std::memcpy(out,reinterpret_cast<const void*>(at),size);return true;
    }
    if(at==fixture_base+0x3b7b90 && size==32) {
        ++fixture_obstruction_checks;
        if(!fixture_created)fixture_checked_before_hook=true;
    }
    for(std::size_t i=0;i<size;++i) {
        const auto found=fixture_bytes.find(at+i);if(found==fixture_bytes.end())return false;
        static_cast<unsigned char*>(out)[i]=found->second;
    }
    if(at==fixture_mutate_at) {fixture_mutate_at=0;fixture_put(fixture_mutate_identity,d2vr::rig::Identity{9,10});}
    return true;
}
UINT fixture_logic(){return 2;}
}
extern "C" MH_STATUS WINAPI MH_CreateHook(LPVOID target,LPVOID hook,LPVOID* original) {
    const auto at=reinterpret_cast<std::uintptr_t>(target);std::array<unsigned char,32> bytes{};
    assert(fixture_read(at,bytes.data(),bytes.size()));fixture_original_bytes[at]=bytes;
    if(at==fixture_base+d2vr::NativeSlashSceneRate::rvas[0]) {
        if(!fixture_slash_trampoline) {
            fixture_slash_trampoline=VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);assert(fixture_slash_trampoline);
            std::array<unsigned char,22> code{};std::memcpy(code.data(),d2vr::NativeSlashSceneRate::prologues[0].data(),8);
            code[8]=0xff;code[9]=0x25;const auto continuation=at+8;std::memcpy(code.data()+14,&continuation,8);
            std::memcpy(fixture_slash_trampoline,code.data(),code.size());
        }
        *original=fixture_slash_trampoline;
    } else if(at==fixture_base+d2vr::NativeChokeDistanceScope::rvas[0] || at==fixture_base+d2vr::NativeChokeDistanceScope::rvas[1]) {
        const unsigned index=at==fixture_base+d2vr::NativeChokeDistanceScope::rvas[0]?0u:1u;
        const unsigned prefix=index?5u:6u;auto*& allocation=fixture_choke_trampolines[index];assert(!allocation);
        allocation=VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);assert(allocation);
        auto* code=static_cast<unsigned char*>(allocation);std::memcpy(code,d2vr::NativeChokeDistanceScope::prologues[index].data(),prefix);
        code[prefix]=code[prefix+14]=0xff;code[prefix+1]=code[prefix+15]=0x25;
        const auto continuation=at+prefix;std::memcpy(code+prefix+6,&continuation,8);std::memcpy(code+prefix+20,&hook,8);*original=allocation;
    } else if(at==fixture_base+d2vr::NativeOfferedChoke::rvas[0] || at==fixture_base+d2vr::NativeOfferedChoke::rvas[1]) {
        const unsigned index=at==fixture_base+d2vr::NativeOfferedChoke::rvas[0]?0u:1u;
        auto*& allocation=fixture_offered_trampolines[index];assert(!allocation);
        allocation=VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);assert(allocation);
        auto* code=static_cast<unsigned char*>(allocation);std::memcpy(code,d2vr::NativeOfferedChoke::prologues[index].data(),5);
        code[5]=code[19]=0xff;code[6]=code[20]=0x25;
        const auto continuation=at+5;std::memcpy(code+11,&continuation,8);std::memcpy(code+25,&hook,8);*original=allocation;
    } else *original=reinterpret_cast<void*>(at+0x10000000);
    ++fixture_created;return MH_OK;
}
extern "C" MH_STATUS WINAPI MH_EnableHook(LPVOID target) {
    const auto at=reinterpret_cast<std::uintptr_t>(target);
    if(at==fixture_fail_enable)return MH_ERROR_MEMORY_PROTECT;
    assert(fixture_original_bytes.count(at));fixture_bytes[at]=0xe9;++fixture_enabled;return MH_OK;
}
extern "C" MH_STATUS WINAPI MH_DisableHook(LPVOID target) {
    const auto at=reinterpret_cast<std::uintptr_t>(target);const auto found=fixture_original_bytes.find(at);
    if(found!=fixture_original_bytes.end())fixture_put(at,found->second);return MH_OK;
}
namespace d2vr {
struct NativeInteractionRangeFixture {
    static void grip_use_targets() {
        constexpr std::uintptr_t game=0x240000000,player=0x241000000,camera=0x242000000,
            door=0x243000000,component=0x244000000,members=0x245000000;
        for(unsigned side=0;side<2;++side) {
            auto aim=std::make_unique<NativeInteractionAim>();aim->base_=fixture_base;aim->read_=fixture_read;
            aim->routing_.lock(static_cast<int>(side));aim->published_[side]=true;
            aim->hands_[side].camera=camera;aim->hands_[side].player=player;aim->hands_[side].generation=3;
            aim->hands_[side].tick=1000;aim->hands_[side].epoch=1;
            aim->target_object_[side]=component;aim->target_owner_[side]=door;aim->target_spawn_[side]=0x10001;
            aim->target_generation_[side]=4;aim->target_tick_[side]=1000;aim->ready_revision_[side]=aim->routing_.revision();
            const auto seed=[&] {
                fixture_bytes.clear();fixture_mutate_at=0;fixture_read_fault=false;
                fixture_put(fixture_base+0x39a5290,game);fixture_put(game+0x3130b0,player);fixture_put(game+0x3130b8,door);
                fixture_put(camera,fixture_base+camera_vtable);fixture_put(camera+0x20,fixture_base+camera_secondary_vtable);
                fixture_put(camera+0x18,rig::Identity{3,10});fixture_put(camera+0x10,player);
                fixture_put(camera+0x25200,0);fixture_put(camera+0x25204,0);
                fixture_put(camera+0x251b9,static_cast<unsigned char>(0));fixture_put(camera+0x25260,static_cast<unsigned char>(0));
                fixture_put(component,fixture_base+0x1c569c8);fixture_put(component+0x10,door);fixture_put(component+0x18,rig::Identity{4,10});
                fixture_put(door,fixture_base+0x1cb7c68);fixture_put(door+0xa8,0x10001u);fixture_put(door+0x370,component);
                fixture_put(door+0x268,rig::List{members,1,1u<<8});fixture_put(members,component);
            };
            seed();assert(aim->grip_use_target(static_cast<int>(side),1000));
            assert(!aim->grip_use_target(static_cast<int>(side^1u),1000));
            assert(!aim->grip_use_target(static_cast<int>(side),1251));assert(!aim->grip_use_target(static_cast<int>(side),999));
            for(unsigned fault=0;fault<9;++fault) {
                seed();
                if(fault==0)fixture_put(component+0x18,rig::Identity{5,10});
                if(fault==1)fixture_put(door+0xa8,0x10002u);
                if(fault==2)fixture_put(door,fixture_base+0x1c5e108);
                if(fault==3)fixture_put(component,fixture_base+0x1c5e108);
                if(fault==4)fixture_put(component+0x10,player);
                if(fault==5)fixture_put(door+0x370,component+8);
                if(fault==6) {fixture_put(door+0x268,rig::List{members,2,2u<<8});fixture_put(members+8,component);}
                if(fault==7)fixture_put(members,component+8);
                if(fault==8) {fixture_mutate_at=door+0x370;fixture_mutate_identity=component+0x18;}
                assert(!aim->grip_use_target(static_cast<int>(side),1000));
            }
            // Hand-mode takeables need real nearby owner bounds and the proven
            // native non-character type getter, in addition to the selected component.
            const auto sphere_target=[&](bool carryable_owner) {
                seed();
                if(carryable_owner)fixture_put(door,fixture_base+0x1c5e258);
                else fixture_put(component,fixture_base+0x1c5a3b8);
                constexpr std::uintptr_t getter=fixture_base+0x10000,info=fixture_base+0x3a00000,model=0x246000000;
                const auto table=fixture_base+(carryable_owner?0x1c5e258:0x1cb7c68);
                fixture_put(table,getter);
                std::array<unsigned char,8> code{0x48,0x8d,0x05,0,0,0,0,0xc3};
                const auto displacement=static_cast<std::int32_t>(info-(getter+7));std::memcpy(code.data()+3,&displacement,4);
                fixture_put(getter,code);fixture_put(info+0x60,50);fixture_put(fixture_base+0x39ce2c0,100);fixture_put(fixture_base+0x39ce2c4,200);
                fixture_put(door+0xf0,static_cast<std::uintptr_t>(model));fixture_put(model,fixture_base+0x1b0dd88);
                fixture_put(model+0x1840,std::array<float,8>{.19f,-.01f,-.01f,0,.21f,.01f,.01f,0});
                aim->hands_[side].volume_centre={0,0,0};
                assert(aim->grip_use_target(static_cast<int>(side),1000));
                aim->hands_[side].volume_centre={-.10,0,0}; // 29 cm from the nearest actual bound.
                assert(!aim->grip_use_target(static_cast<int>(side),1000));
                aim->hands_[side].volume_centre={0,0,0};
                fixture_put(door+0xf0,std::uintptr_t{});assert(!aim->grip_use_target(static_cast<int>(side),1000));
            };
            sphere_target(true);sphere_target(false);
            seed();fixture_read_fault=true;assert(!aim->grip_use_target(static_cast<int>(side),1000));
            fixture_read_fault=false;assert(aim->grip_use_target(static_cast<int>(side),1000));
            assert(!aim->grip_use_target(-1,1000) && !aim->grip_use_target(2,1000));
        }
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
        const auto block_at=fixture_base+NativeBlockEligibility::prologue_rva;
        std::map<std::uintptr_t,std::array<unsigned char,32>> retained;
        for(const auto at:{block_at,fixture_base+NativeSlashSceneRate::rvas[0],fixture_base+NativeSlashSceneRate::rvas[1],fixture_base+NativeSlashSceneRate::rvas[2],fixture_base+NativeChokeDistanceScope::rvas[0],fixture_base+NativeChokeDistanceScope::rvas[1],fixture_base+NativeOfferedChoke::rvas[0],fixture_base+NativeOfferedChoke::rvas[1]}) {
            const auto found=fixture_original_bytes.find(at);if(found!=fixture_original_bytes.end())retained[at]=found->second;
        }
        fixture_bytes.clear();fixture_original_bytes.clear();fixture_created=fixture_enabled=fixture_obstruction_checks=0;fixture_checked_before_hook=false;fixture_fail_enable=0;
        fixture_original_bytes=std::move(retained);
        fixture_put(fixture_base+NativeOfferedChoke::rvas[0],offered_profile_0);
        fixture_put(fixture_base+NativeOfferedChoke::rvas[1],offered_profile_1);
        fixture_put(fixture_base+NativeWorldTrace::cast_rva,NativeWorldTrace::cast_code);
        fixture_put(fixture_base+NativeWorldTrace::filter_check_rva,NativeWorldTrace::filter_code);
        fixture_put(fixture_base+NativeWorldTrace::filter_body_rva,NativeWorldTrace::filter_body_code);
        fixture_put(fixture_base+NativeWorldTrace::filter_pair_rva,NativeWorldTrace::filter_pair_code);
        fixture_put(fixture_base+NativeWorldTrace::collector_add_rva,NativeWorldTrace::collector_add_code);
        fixture_put(fixture_base+NativeWorldTrace::any_hit_add_rva,NativeWorldTrace::any_hit_add_code);
        fixture_put(fixture_base+NativeWorldTrace::sphere_ctor_rva,NativeHandVolume::constructor_code);
        fixture_put(fixture_base+NativeWorldTrace::filter_vtable_rva+0x20,fixture_base+NativeWorldTrace::filter_check_rva);
        fixture_put(fixture_base+NativeWorldTrace::filter_vtable_rva+0x28,fixture_base+NativeWorldTrace::filter_pair_rva);
        fixture_put(fixture_base+NativeWorldTrace::filter_vtable_rva+0x30,fixture_base+NativeWorldTrace::filter_body_rva);
        fixture_put(fixture_base+NativeWorldTrace::collector_vtable_rva+0x18,fixture_base+NativeWorldTrace::collector_add_rva);
        fixture_put(fixture_base+NativeWorldTrace::any_hit_collector_vtable_rva+0x18,fixture_base+NativeWorldTrace::any_hit_add_rva);
        for(unsigned index=0;index<2;++index)fixture_put(fixture_base+NativeChokeDistanceScope::rvas[index],NativeChokeDistanceScope::prologues[index]);
        fixture_put(fixture_base+0x3a6a9d,NativeChokeDistanceScope::caller_code);
        for(const auto& pin:NativeChokeDistanceScope::consumer_pins)for(unsigned offset=0;offset<pin.size;++offset)fixture_put(fixture_base+pin.rva+offset,pin.bytes[offset]);
        fixture_put(fixture_base+0x1d0b738,fixture_base+0x94fd10);
        fixture_put(block_at,NativeBlockEligibility::prologue);
        fixture_put(fixture_base+NativeBlockEligibility::caller_rva,NativeBlockEligibility::caller_code);
        for(const auto& pin:NativeBlockEligibility::slot_pins)fixture_put(fixture_base+pin.first,fixture_base+pin.second);
        for(unsigned index=0;index<NativeSlashSceneRate::rvas.size();++index)fixture_put(fixture_base+NativeSlashSceneRate::rvas[index],NativeSlashSceneRate::prologues[index]);
        for(const auto& pin:NativeSlashSceneRate::caller_pins)fixture_put(fixture_base+pin.rva,pin.bytes);
        for(const auto& pin:NativeSlashSceneRate::slot_pins)fixture_put(fixture_base+pin.first,fixture_base+pin.second);
        fixture_put(fixture_base+NativeSlashSceneRate::setter_rva,NativeSlashSceneRate::setter_code);
        for(unsigned i=0;i<6;++i)fixture_put(fixture_base+NativeInteractionAim::rvas[i],NativeInteractionAim::expected[i]);
        for(unsigned i=0;i<3;++i) {
            std::array<unsigned char,32> bytes{};
            const auto nibble=[](char c){return c<='9'?c-'0':c-'a'+10;};
            for(unsigned n=0;n<32;++n)bytes[n]=static_cast<unsigned char>(nibble(NativeInteractionAim::ranking_code[i][n*2])*16+nibble(NativeInteractionAim::ranking_code[i][n*2+1]));
            fixture_put(fixture_base+NativeInteractionAim::ranking_rvas[i],bytes);
        }
        fixture_put(fixture_base+0x417820,std::array<unsigned char,32>{});fixture_put(fixture_base+0x417820,NativeInteractionAim::range_origin_code);
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
        fixture_put(fixture_base+0x3b8b05,hand_hover_scan);
        fixture_put(fixture_base+0x3b80a0,native_hand_eligibility_code);
        fixture_put(fixture_base+0x3b81d3,std::array<unsigned char,5>{0xe8,0x48,0xf6,0x05,0});
    }
    static void run() {
        grip_use_targets();
        const auto folder=std::filesystem::absolute(std::filesystem::path(__FILE__)).parent_path().parent_path()/L"artifacts"/L"playground"/L"native-interaction-install-order-check";
        std::filesystem::create_directories(folder);
        for(const bool route:{false,true}) {
            prepare();auto grab=std::make_unique<NativeGrab>();auto owned_aim=std::make_unique<NativeInteractionAim>();auto& aim=*owned_aim;aim.attach_physical_grab(grab.get());
            const bool new_block_hook=!NativeBlockEligibility::process_owner().has_trampoline();
            unsigned new_slash_hooks{};for(unsigned index=0;index<3;++index)if(!NativeSlashSceneRate::process_owner().created(index))++new_slash_hooks;
            unsigned new_choke_hooks{};for(unsigned index=0;index<2;++index)if(!NativeChokeDistanceScope::process_owner().created(index))++new_choke_hooks;
            unsigned new_offered_hooks{};for(unsigned index=0;index<2;++index)if(!NativeOfferedChoke::process_owner().original(index))++new_offered_hooks;
            aim.install(fixture_base,fixture_read,fixture_logic,route,folder/(route?L"route-on.csv":L"route-off.csv"));
            assert(fixture_checked_before_hook && fixture_obstruction_checks && aim.active_);
            assert(fixture_created==(route?11u:6u)+(new_block_hook?1u:0u)+new_slash_hooks+new_choke_hooks+new_offered_hooks && fixture_enabled==(route?19u:14u));
            assert(NativeOfferedChoke::process_owner().enabled());
            assert(NativeBlockEligibility::process_owner().enabled());
            assert(NativeSlashSceneRate::process_owner().enabled());
            assert(NativeChokeDistanceScope::process_owner().installed() && !NativeChokeDistanceScope::process_owner().enabled());
            assert(NativeChokeDistanceScope::process_owner().unwind_registered());
            aim.takedown_ranges(true);assert(NativeChokeDistanceScope::process_owner().enabled() && aim.takedown_range_kill_rules()==0);
            assert(reinterpret_cast<std::uintptr_t>(aim.physical_occlusion_)==fixture_base+0x3b7b90+(route?0x10000000:0));
            if(route)assert(fixture_bytes.at(fixture_base+0x3b7b90)==0xe9); // signature really changed
            assert(aim.disable());
            assert(NativeBlockEligibility::process_owner().stopped());
            assert(NativeSlashSceneRate::process_owner().stopped());
            assert(NativeChokeDistanceScope::process_owner().stopped());
            assert(NativeOfferedChoke::process_owner().stopped());
        }
        for(const bool route:{false,true}) {
            prepare();fixture_bytes[fixture_base+0x3b7b90]=0xcc;
            auto grab=std::make_unique<NativeGrab>();auto owned_aim=std::make_unique<NativeInteractionAim>();auto& aim=*owned_aim;aim.attach_physical_grab(grab.get());bool rejected{};
            try {aim.install(fixture_base,fixture_read,fixture_logic,route,folder/L"corrupt.csv");}
            catch(const std::runtime_error& error){rejected=std::string(error.what())=="Native physical-grab obstruction wrapper differs";}
            assert(rejected && fixture_checked_before_hook && !fixture_created && !fixture_enabled && !aim.active_);
        }
        for(const auto corrupt_at:{NativeBlockEligibility::prologue_rva,NativeBlockEligibility::caller_rva,NativeBlockEligibility::slot_pins[0].first}) {
            prepare();fixture_bytes[fixture_base+corrupt_at]=0xcc;auto aim=std::make_unique<NativeInteractionAim>();bool rejected{};
            try {aim->install(fixture_base,fixture_read,fixture_logic,false,folder/L"block-corrupt.csv");}
            catch(const std::runtime_error& error){rejected=std::string(error.what())=="Native Block eligibility consumer differs";}
            assert(rejected && !fixture_created && !fixture_enabled && !aim->active_ && NativeBlockEligibility::process_owner().stopped());
        }
        for(const auto corrupt_at:{std::uintptr_t{0x417820},std::uintptr_t{0x92630}}) {
            prepare();fixture_bytes[fixture_base+corrupt_at]=0xcc;auto aim=std::make_unique<NativeInteractionAim>();bool rejected{};
            try {aim->install(fixture_base,fixture_read,fixture_logic,true,folder/L"route-corrupt.csv");}
            catch(const std::runtime_error&){rejected=true;}
            assert(rejected && !fixture_created && !fixture_enabled && !aim->active_);
        }
        for(const auto corrupt_at:{NativeWorldTrace::cast_rva,NativeWorldTrace::filter_check_rva,NativeWorldTrace::filter_body_rva,
                                  NativeWorldTrace::filter_pair_rva,NativeWorldTrace::collector_add_rva,NativeWorldTrace::any_hit_add_rva,
                                  NativeWorldTrace::sphere_ctor_rva,NativeWorldTrace::filter_vtable_rva+0x20}) {
            prepare();fixture_bytes[fixture_base+corrupt_at]=0xcc;
            auto grab=std::make_unique<NativeGrab>();auto aim=std::make_unique<NativeInteractionAim>();aim->attach_physical_grab(grab.get());bool rejected{};
            try {aim->install(fixture_base,fixture_read,fixture_logic,false,folder/L"door-ingress-corrupt.csv");}
            catch(const std::runtime_error& error){rejected=std::string(error.what())=="Cannot configure native door contact ingress";}
            assert(rejected && !fixture_created && !fixture_enabled && !aim->active_);
        }
        for(const auto corrupt_at:{NativeSlashSceneRate::rvas[0],NativeSlashSceneRate::rvas[1],NativeSlashSceneRate::rvas[2],NativeSlashSceneRate::caller_pins[0].rva,NativeSlashSceneRate::slot_pins[0].first,NativeSlashSceneRate::setter_rva}) {
            prepare();fixture_bytes[fixture_base+corrupt_at]=0xcc;auto aim=std::make_unique<NativeInteractionAim>();bool rejected{};
            try {aim->install(fixture_base,fixture_read,fixture_logic,false,folder/L"slash-corrupt.csv");}
            catch(const std::runtime_error& error){rejected=std::string(error.what())=="Native Slash scene-rate consumer differs";}
            assert(rejected && !fixture_created && !fixture_enabled && !aim->active_ && NativeSlashSceneRate::process_owner().stopped());
        }
        std::vector<std::uintptr_t> choke_corruptions{NativeChokeDistanceScope::rvas[0],NativeChokeDistanceScope::rvas[1],0x3a6a9d,0x1d0b738};
        for(const auto& pin:NativeChokeDistanceScope::consumer_pins)choke_corruptions.push_back(pin.rva);
        for(const auto corrupt_at:choke_corruptions) {
            prepare();fixture_bytes[fixture_base+corrupt_at]=0xcc;auto aim=std::make_unique<NativeInteractionAim>();bool rejected{};
            try {aim->install(fixture_base,fixture_read,fixture_logic,false,folder/L"choke-corrupt.csv");}
            catch(const std::runtime_error& error){rejected=std::string(error.what())=="Native Choke scope consumer differs";}
            assert(rejected && !fixture_created && !fixture_enabled && !aim->active_ && NativeChokeDistanceScope::process_owner().stopped());
        }
        for(const auto corrupt_at:{NativeOfferedChoke::rvas[0],NativeOfferedChoke::rvas[1],NativeOfferedChoke::rvas[0]+NativeOfferedChoke::sizes[0]-1,NativeOfferedChoke::rvas[1]+NativeOfferedChoke::sizes[1]-1}) {
            prepare();fixture_bytes[fixture_base+corrupt_at]=0xcc;auto aim=std::make_unique<NativeInteractionAim>();bool rejected{};
            try {aim->install(fixture_base,fixture_read,fixture_logic,false,folder/L"choke-offer-corrupt.csv");}
            catch(const std::runtime_error& error){rejected=std::string(error.what())=="Native Choke offer observer differs";}
            assert(rejected && !fixture_created && !fixture_enabled && !aim->active_ && NativeOfferedChoke::process_owner().stopped());
        }
        for(const auto failure_at:{std::uintptr_t{0x95550},NativeBlockEligibility::prologue_rva,NativeSlashSceneRate::rvas[0],NativeSlashSceneRate::rvas[1],NativeSlashSceneRate::rvas[2],NativeChokeDistanceScope::rvas[0],NativeChokeDistanceScope::rvas[1],NativeOfferedChoke::rvas[0],NativeOfferedChoke::rvas[1]}) {
            prepare();fixture_fail_enable=fixture_base+failure_at;auto aim=std::make_unique<NativeInteractionAim>();bool rejected{};
            try {aim->install(fixture_base,fixture_read,fixture_logic,false,folder/L"partial-failure.csv");}
            catch(const std::runtime_error&){rejected=true;}
            assert(rejected && !aim->active_ && !aim->stopping_ && NativeBlockEligibility::process_owner().stopped());
            assert(NativeSlashSceneRate::process_owner().stopped());
            assert(NativeChokeDistanceScope::process_owner().stopped());
            assert(NativeOfferedChoke::process_owner().stopped());
            for(const bool enabled:aim->enabled_)assert(!enabled);
            fixture_fail_enable=0;
            aim->install(fixture_base,fixture_read,fixture_logic,false,folder/L"partial-retry.csv");
            assert(aim->active_ && NativeBlockEligibility::process_owner().enabled() && aim->disable());
        }
        // Preserve bounded fixture logs in the project artifact directory.
    }
};
}
int main(){try {d2vr::NativeInteractionRangeFixture::run();}catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 1;}std::cout<<"PASS: preflight signatures, route on/off, real Choke trampolines/unwind, both partial-enable rollbacks, stop/reinstall; no semantic metadata bypass\n";}
