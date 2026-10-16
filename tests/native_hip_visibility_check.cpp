#define NOMINMAX
#include "../src/native_weapon_hands.hpp"
#include <cassert>
#include <iostream>
#include <map>

namespace {
unsigned dispatched{};
std::uintptr_t dispatched_entity{};
void __fastcall visibility_returned(std::uintptr_t entity) {++dispatched;dispatched_entity=entity;}
struct Memory {
    std::map<std::uintptr_t,unsigned char> bytes;
    template<class Value> void put(std::uintptr_t address,const Value& value) {
        const auto* data=reinterpret_cast<const unsigned char*>(&value);
        for(std::size_t offset=0;offset<sizeof(value);++offset)bytes[address+offset]=data[offset];
    }
    bool operator()(std::uintptr_t address,void* output,std::size_t size) {
        auto* data=static_cast<unsigned char*>(output);
        for(std::size_t offset=0;offset<size;++offset) {
            const auto found=bytes.find(address+offset);
            if(found==bytes.end())return false;
            data[offset]=found->second;
        }
        return true;
    }
};
}

namespace d2vr {
struct NativeHipVisibilityFixture {
    static void run() {
        using Hands=NativeWeaponHands;
        const auto callback=reinterpret_cast<std::uintptr_t>(&visibility_returned);
        const auto base=callback-0x8784b0;
        constexpr std::uintptr_t entity=0x230000000,table=0x231000000,game=0x232000000;
        constexpr std::uintptr_t player=0x234000000,machine=0x235000000,components=0x236000000;
        constexpr std::uint32_t spawn=0x10001;
        Memory memory;
        memory.put(base+0x39a5290,game);
        memory.put(game,base+0x1c940a8);memory.put(game+0x3130b0,player);
        memory.put(player,base+0x1c5e428);memory.put(player+0xa8,std::uint32_t{42});
        memory.put(player+0x268,rig::List{components,1,1u<<8});memory.put(components,machine);
        memory.put(machine,base+0x1c73d58);memory.put(machine+0x10,player);memory.put(machine+0x18,rig::Identity{123,10});
        const NativePlayerStateLease owner{base,game,player,machine,42,{123,10}};
        memory.put(game+0x3130b0+(spawn>>16)*8ull,entity);
        memory.put(entity+0xa8,spawn);memory.put(entity,table);
        memory.put(table+0x80,std::uintptr_t{0x233000000});
        Hands::hip_ent_={entity,0};Hands::hip_id_={spawn,0};Hands::hip_shown_={true,false};
        Hands::hip_owner_lease_={owner,std::nullopt};
        Hands::hip_owner_={0,-1};Hands::emily_=0;
        Hands::hip_req_={false,0,0,{}, {},GetTickCount64()};
        const auto skips=Hands::visibility_skips.load();
        Hands::service_hip(base,memory);
        assert(Hands::hip_ent_[0]==entity && Hands::hip_id_[0]==spawn && Hands::hip_shown_[0]);
        assert(dispatched==0 && Hands::visibility_skips==skips+1);
        memory.put(table+0x80,callback);
        Hands::service_hip(base,memory);
        assert(Hands::hip_ent_[0]==entity && Hands::hip_id_[0]==spawn && !Hands::hip_shown_[0]);
        assert(dispatched==1 && dispatched_entity==entity);
        Hands::service_hip(base,memory);assert(dispatched==1);

        Hands::hip_shown_[0]=true;Hands::hip_req_.owner=1;
        memory.put(table+0x80,std::uintptr_t{0x233000000});
        Hands::service_hip(base,memory);
        assert(Hands::hip_ent_[0]==entity && Hands::hip_id_[0]==spawn && Hands::hip_shown_[0] && Hands::hip_owner_[0]==0);
        assert(dispatched==1);
        memory.put(table+0x80,callback);
        Hands::service_hip(base,memory);
        assert(!Hands::hip_ent_[0] && !Hands::hip_id_[0] && !Hands::hip_shown_[0] && dispatched==2);

        Hands::hip_ent_={entity,0};Hands::hip_id_={spawn,0};Hands::hip_shown_={false,false};Hands::hip_owner_={0,-1};
        Hands::hip_owner_lease_={owner,std::nullopt};
        Hands::service_hip(base,memory);
        assert(!Hands::hip_ent_[0] && dispatched==2);
        Hands::hip_ent_={entity,0};Hands::hip_id_={spawn,0};Hands::hip_shown_={true,false};Hands::hip_owner_={1,-1};
        Hands::hip_owner_lease_={owner,std::nullopt};
        memory.put(entity+0xa8,std::uint32_t{spawn+1});
        Hands::service_hip(base,memory);
        assert(!Hands::hip_ent_[0] && !Hands::hip_shown_[0] && dispatched==2);

        const auto show_base=callback-0x87c590;
        Memory show_memory;show_memory.put(entity,table);show_memory.put(table+0x88,callback);
        assert(Hands::visible(show_base,show_memory,entity,true) && dispatched==3);
        show_memory.put(table+0x88,std::uintptr_t{0x233000000});
        assert(!Hands::visible(show_base,show_memory,entity,true) && dispatched==3);
        Hands::hip_ent_={entity,0};Hands::hip_id_={spawn,0};Hands::hip_shown_={false,false};Hands::hip_owner_={1,-1};
        Hands::hip_owner_lease_={owner,std::nullopt};
        Hands::hip_req_={true,0,1,{}, {},GetTickCount64()};
        memory.put(entity+0xa8,spawn);memory.put(table+0x88,std::uintptr_t{0x233000000});
        Hands::service_hip(base,memory);
        assert(Hands::hip_ent_[0]==entity && Hands::hip_id_[0]==spawn && !Hands::hip_shown_[0] && dispatched==3);
        Hands::hip_ent_={};Hands::hip_id_={};Hands::hip_shown_={};Hands::hip_owner_={-1,-1};Hands::hip_owner_lease_={};Hands::hip_req_={};Hands::emily_=-1;
    }
};
}
int main() {d2vr::NativeHipVisibilityFixture::run();std::cout<<"PASS hip visibility refusal bookkeeping; synthetic callbacks only\n";}
