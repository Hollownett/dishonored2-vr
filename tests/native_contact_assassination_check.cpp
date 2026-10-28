#define NOMINMAX
#include "native_contact_assassination.hpp"
#include <cassert>
#include <cstdio>
#include <functional>
#include <map>

namespace {
constexpr std::uintptr_t fixture_base=0x140000000,fixture_game=0x200000000,fixture_player=0x201000000,fixture_machine=0x202000000,
    fixture_entries=0x203000000,fixture_sensor=0x204000000,fixture_victim=0x205000000,fixture_handler=0x206000000;
constexpr std::uint32_t fixture_spawn=0x20001;
std::map<std::uintptr_t,unsigned char> memory;
std::function<void(std::uintptr_t)> after_read;
template<class T> void put(std::uintptr_t address,T value) {
    const auto* bytes=reinterpret_cast<const unsigned char*>(&value);
    for(std::size_t i=0;i<sizeof(value);++i)memory[address+i]=bytes[i];
}
bool read_memory(std::uintptr_t address,void* out,std::size_t size) {
    for(std::size_t i=0;i<size;++i) {
        const auto it=memory.find(address+i);if(it==memory.end())return false;
        static_cast<unsigned char*>(out)[i]=it->second;
    }
    if(after_read)after_read(address);
    return true;
}
d2vr::NativePlayerStateLease seed() {
    memory.clear();after_read={};
    put(fixture_base+0x39a5290,fixture_game);put(fixture_game,fixture_base+0x1c940a8);put(fixture_game+0x3130b0,fixture_player);
    put(fixture_player,fixture_base+0x1c5e428);put(fixture_player+0xa8,std::uint32_t{1});
    put(fixture_player+0x268,d2vr::rig::List{fixture_entries,2,2u<<8});put(fixture_entries,fixture_machine);put(fixture_entries+8,fixture_sensor);
    put(fixture_machine,fixture_base+0x1c73d58);put(fixture_machine+0x10,fixture_player);put(fixture_machine+0x18,d2vr::rig::Identity{4,10});
    put(fixture_sensor,fixture_base+0x1b3f998);put(fixture_sensor+0x10,fixture_player);put(fixture_sensor+0x20,fixture_machine);put(fixture_sensor+0x18,d2vr::rig::Identity{8,10});
    put(fixture_sensor+0x38,std::uint8_t{1});put(fixture_sensor+0x1d0,fixture_handler);put(fixture_sensor+0x1d8,std::uint32_t{2});
    put(fixture_sensor+0x1dc,std::uint8_t{1});put(fixture_sensor+0x1b8,fixture_handler);
    put(fixture_sensor+0x78,fixture_base+0x1b3f978);put(fixture_sensor+0x80,fixture_sensor);put(fixture_sensor+0x98,fixture_spawn);
    put(fixture_game+0x3130b0+16,fixture_victim);put(fixture_victim,fixture_base+d2vr::NativeMelee::npc_vtable_rva);put(fixture_victim+0xa8,fixture_spawn);
    return {fixture_base,fixture_game,fixture_player,fixture_machine,1,{4,10}};
}
bool offered(const d2vr::NativePlayerStateLease& owner) {
    auto read=&read_memory;return d2vr::native_contact_assassination_offer(read,owner,fixture_spawn);
}
}
int main() {
    assert(offered(seed()));
    for(unsigned mutation=0;mutation<13;++mutation) {
        const auto owner=seed();
        switch(mutation) {
        case 0:put(fixture_sensor+0x98,fixture_spawn+1);break;
        case 1:put(fixture_sensor+0x1d8,std::uint32_t{4});break;
        case 2:put(fixture_sensor+0x1dc,std::uint8_t{});break;
        case 3:put(fixture_sensor+0x10,fixture_player+0x100);break;
        case 4:put(fixture_sensor+0x20,fixture_machine+0x100);break;
        case 5:put(fixture_sensor+0x1b8,fixture_handler+0x100);break;
        case 6:put(fixture_sensor+0x1d0,std::uintptr_t{});break;
        case 7:put(fixture_sensor+0x80,fixture_sensor+0x100);break;
        case 8:put(fixture_sensor+0x38,std::uint8_t{});break;
        case 9:put(fixture_victim+0xa8,fixture_spawn+1);break;
        case 10:put(fixture_victim,fixture_base+0x1c5e428);break;
        case 11:put(fixture_sensor+0x78,fixture_base+0x1b3f998);break;
        case 12:put(fixture_entries+8,fixture_machine);break;
        }
        assert(!offered(owner));
    }
    // Changes after the first read cannot renew an offer under its previous identity/target.
    for(unsigned mutation=0;mutation<4;++mutation) {
        const auto owner=seed();bool changed=false;
        after_read=[&](std::uintptr_t address) {
            if(changed || address!=fixture_sensor+0x98)return;
            changed=true;
            if(mutation==0)put(fixture_sensor+0x18,d2vr::rig::Identity{9,10});
            if(mutation==1)put(fixture_sensor+0x98,fixture_spawn+1);
            if(mutation==2)put(fixture_sensor+0x1d0,fixture_handler+0x100);
            if(mutation==3)put(fixture_machine+0x18,d2vr::rig::Identity{5,10});
        };
        assert(!offered(owner));assert(changed);
    }
    std::puts("PASS exact ordinary assassination offer identity/target/owner/handler/races; memory reads only");
}
