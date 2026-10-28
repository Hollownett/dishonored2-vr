#define NOMINMAX
#include "native_sword_contact.hpp"
#include <cassert>
#include <cstdio>
#include <map>
#include <functional>
#include <cstdlib>
#include <new>

static std::uint64_t allocation_count{};
void* operator new(std::size_t size) {
    ++allocation_count;
    if(auto* allocation=std::malloc(size?size:1))return allocation;
    throw std::bad_alloc();
}
void operator delete(void* allocation) noexcept {std::free(allocation);}
void operator delete(void* allocation,std::size_t) noexcept {std::free(allocation);}

namespace {
constexpr std::uintptr_t image_base=0x140000000,fixture_game=0x200000000,fixture_player=0x201000000,fixture_machine=0x202000000,
    fixture_pool=0x203000000,fixture_inventory=0x204000000,fixture_item=0x205000000,fixture_item_decl=0x206000000,
    fixture_sword_entity=0x207000000,fixture_weapon=0x208000000,fixture_weapon_decl=0x209000000,fixture_damage_decl=0x20a000000,
    fixture_player_entries=0x20b000000,fixture_sword_entries=0x20c000000,fixture_victim=0x20d000000,fixture_state=0x20e000000;
constexpr std::uint32_t player_spawn=1,sword_spawn=0x10001,victim_spawn=0x20001;
std::map<std::uintptr_t,unsigned char> memory;
std::function<void(std::uintptr_t)> after_read;
template<class Value> void put(std::uintptr_t address,const Value& value) {
    const auto* bytes=reinterpret_cast<const unsigned char*>(&value);
    for(std::size_t offset=0;offset<sizeof(value);++offset)memory[address+offset]=bytes[offset];
}
bool read_memory(std::uintptr_t address,void* output,std::size_t size) {
    for(std::size_t offset=0;offset<size;++offset) {
        const auto found=memory.find(address+offset);if(found==memory.end())return false;
        static_cast<unsigned char*>(output)[offset]=found->second;
    }
    if(after_read)after_read(address);return true;
}
void fields() {
    memory.clear();after_read={};
    put(image_base+0x39a5290,fixture_game);put(fixture_game,image_base+0x1c940a8);put(fixture_game+0x3130b0,fixture_player);
    put(fixture_player,image_base+0x1c5e428);put(fixture_player+0xa8,player_spawn);
    put(fixture_player+0x268,d2vr::rig::List{fixture_player_entries,2,2u<<8});put(fixture_player_entries,fixture_machine);put(fixture_player_entries+8,fixture_inventory);
    put(fixture_machine,image_base+0x1c73d58);put(fixture_machine+0x10,fixture_player);put(fixture_machine+0x18,d2vr::rig::Identity{5,10});put(fixture_machine+0x90,fixture_pool);
    for(const auto entry:d2vr::native_action_camera_states)if(!entry.combat)put(fixture_pool+0x10+entry.index*8,std::uintptr_t{});
    put(fixture_inventory,image_base+d2vr::NativeMelee::inventory_vtable_rva);put(fixture_inventory+0x10,fixture_player);put(fixture_inventory+0x18,d2vr::rig::Identity{6,10});
    put(fixture_inventory+0x38,d2vr::rig::List{fixture_item,1,1u<<8});put(fixture_inventory+0x68,fixture_item);put(fixture_item,fixture_item_decl);put(fixture_item+8,sword_spawn);
    put(fixture_item+0x10,fixture_weapon);put(fixture_item+0x18,fixture_weapon_decl);
    put(fixture_item_decl,image_base+0x1b9ea48);put(fixture_game+0x3130b0+8,fixture_sword_entity);put(fixture_sword_entity+0xa8,sword_spawn);put(fixture_sword_entity+0xac,player_spawn);
    put(fixture_sword_entity+0x268,d2vr::rig::List{fixture_sword_entries,1,1u<<8});put(fixture_sword_entries,fixture_weapon);
    put(fixture_weapon,image_base+d2vr::NativeMelee::melee_vtable_rva);put(fixture_weapon+8,fixture_weapon_decl);put(fixture_weapon+0x10,fixture_sword_entity);put(fixture_weapon+0x18,d2vr::rig::Identity{7,10});
    put(fixture_weapon_decl,image_base+0x1b9d3c8);put(fixture_weapon_decl+0x120,fixture_damage_decl);
    put(fixture_game+0x3130b0+16,fixture_victim);put(fixture_victim,image_base+d2vr::NativeMelee::npc_vtable_rva);put(fixture_victim+0xa8,victim_spawn);
    put(fixture_victim+0xd1,std::uint8_t{});put(fixture_victim+0xd7,std::uint8_t{2});
}
d2vr::MeleeHitRequest hit() {return {fixture_victim,{1,2,3},{1,0,0},{-1,0,0},1000};}
std::optional<d2vr::OwnedSwordContact> capture(std::uint64_t now=1000) {
    auto reader=&read_memory;return d2vr::capture_sword_contact(reader,image_base,fixture_machine,hit(),9,now);
}
bool current(const d2vr::OwnedSwordContact& contact,std::uint64_t epoch=9,std::uint64_t now=1050) {
    auto reader=&read_memory;return d2vr::sword_contact_current(reader,contact,epoch,now);
}
void populate_state(unsigned index,std::uintptr_t vtable,unsigned active) {
    put(fixture_pool+0x10+index*8,fixture_state);put(fixture_state,image_base+vtable);put(fixture_state+8,index);put(fixture_state+0x10,fixture_machine);put(fixture_state+0x20,static_cast<std::uint8_t>(active));
}
}
int main() {
    // A missing/mismatched native pin and a native-call fault are unavailable, never an "aware" verdict.
    const auto no_read=+[](std::uintptr_t,void*,std::size_t) {return false;};
    assert(!d2vr::NativeMelee::unaware_result(image_base,no_read,fixture_victim).has_value());
    const auto wrong_pin=+[](std::uintptr_t,void* out,std::size_t size) {std::memset(out,0,size);return true;};
    assert(!d2vr::NativeMelee::unaware_result(image_base,wrong_pin,fixture_victim).has_value());
    const auto pinned_read=+[](std::uintptr_t,void* out,std::size_t size) {
        if(size!=d2vr::NativeMelee::unaware_code.size())return false;
        std::memcpy(out,d2vr::NativeMelee::unaware_code.data(),size);return true;
    };
    assert(!d2vr::NativeMelee::unaware_result(0x10000,pinned_read,fixture_victim).has_value());
    fields();const auto contact=capture();assert(contact && current(*contact));
    d2vr::SwordContactPublication initial_publication;initial_publication.publish(true,9,1000);
    const auto hand=initial_publication.current(1050);assert(hand && capture() && current(*contact,hand->epoch));
    const auto baseline_allocations=allocation_count;
    for(std::uint64_t tick=2000;tick<12000;++tick) {
        initial_publication.publish(true,9,tick);assert(initial_publication.current(tick));
        initial_publication.publish(false,9,tick);assert(!initial_publication.current(tick));
    }
    assert(allocation_count==baseline_allocations);
    assert(!current(*contact,10) && !current(*contact,0) && !current(*contact,9,1101) && !current(*contact,9,999));
    assert(current(*contact,9,1100));
    for(unsigned mutation=0;mutation<16;++mutation) {
        fields();const auto saved=capture();assert(saved);
        if(mutation==0)put(fixture_victim+0xa8,std::uint32_t{0x20002});
        if(mutation==1)put(fixture_game+0x3130b0+16,std::uintptr_t{fixture_victim+0x100});
        if(mutation==2)put(fixture_game+0x3130b0,std::uintptr_t{fixture_player+0x100});
        if(mutation==3)put(fixture_machine+0x18,d2vr::rig::Identity{99,10});
        if(mutation==4)put(fixture_inventory+0x18,d2vr::rig::Identity{99,10});
        if(mutation==5)put(fixture_weapon+0x18,d2vr::rig::Identity{99,10});
        if(mutation==6)put(fixture_inventory+0x68,std::uintptr_t{});
        if(mutation==7)put(fixture_item+8,std::uint32_t{0x10002});
        if(mutation==8)put(fixture_weapon_decl+0x120,std::uintptr_t{fixture_damage_decl+0x100});
        if(mutation==9)put(fixture_sword_entity+0xac,std::uint32_t{2});
        if(mutation==10)put(fixture_machine+0x90,std::uintptr_t{fixture_pool+0x100});
        if(mutation==11)put(fixture_weapon+0x10,std::uintptr_t{fixture_victim});
        if(mutation==12)put(fixture_inventory+0x38,d2vr::rig::List{fixture_item+0x38,1,1u<<8});
        if(mutation==13) {put(fixture_item+8,std::uint32_t{0x10002});put(fixture_sword_entity+0xa8,std::uint32_t{0x10002});}
        if(mutation==14)put(fixture_item+0x10,std::uintptr_t{fixture_weapon+0x100});
        if(mutation==15)put(fixture_item+0x18,std::uintptr_t{fixture_weapon_decl+0x100});
        assert(!current(*saved));
    }
    for(const auto entry:d2vr::native_action_camera_states)if(!entry.combat) {
        fields();populate_state(entry.index,entry.vtable,0);assert(capture());
        populate_state(entry.index,entry.vtable,1);assert(!capture());
        populate_state(entry.index,entry.vtable,2);assert(!capture());
        fields();memory.erase(fixture_pool+0x10+entry.index*8);assert(!capture());
        fields();populate_state(entry.index,entry.vtable,0);put(fixture_state+8,entry.index+1);assert(!capture());
    }
    fields();unsigned slot_reads{};
    after_read=[&](std::uintptr_t address) {if(address==fixture_pool+0x10+13*8 && ++slot_reads==1)put(address,fixture_state);};
    assert(!capture());
    for(const auto address:std::array<std::uintptr_t,3>{fixture_item+0x10,fixture_item+0x18,fixture_weapon_decl+0x120}) {
        fields();unsigned link_reads{};
        after_read=[&](std::uintptr_t observed) {if(observed==address && ++link_reads==1)put(address,std::uintptr_t{});};
        assert(!capture());
    }
    fields();unsigned weapon_reads{};
    after_read=[&](std::uintptr_t address) {if(address==fixture_weapon+0x18 && ++weapon_reads==1)put(address,d2vr::rig::Identity{99,10});};
    assert(!capture());
    {
        d2vr::SwordContactPublication publication;assert(!publication.current(1000));
        publication.publish(true,9,1000);assert(publication.current(1100));assert(!publication.current(1101));
        publication.publish(false,9,1000);assert(!publication.current(1000));
        publication.publish(true,0,1000);assert(!publication.current(1000));
        publication.publish(true,9,1001);assert(!publication.current(1000));
        publication.publish(false,0,0);assert(!publication.current(1000));
    }
    {
        std::array<unsigned char,32> bytes{};const float computed=12.5f,resource=3.25f;
        const std::uint32_t mask=0x80000001u;const std::uintptr_t payload=0x123456789abcdef0ull;
        std::memcpy(bytes.data(),&computed,4);std::memcpy(bytes.data()+4,&resource,4);std::memcpy(bytes.data()+8,&mask,4);
        std::memcpy(bytes.data()+0x10,&payload,8);bytes[0x18]=0x45;
        const auto result=d2vr::NativeMelee::decode_damage_result(bytes.data(),true);
        assert(result.raw==bytes && result.computed_damage==computed && result.resource_damage==resource &&
               result.modifier_mask==mask && result.modifier_payload==payload && result.flags==0x45 && result.returned_buffer);
        assert(!d2vr::NativeMelee::decode_damage_result(bytes.data(),false).returned_buffer);
    }
    std::puts("PASS owned sword contact freshness/generations/equipment/registry/scripted-null-state/two-pass/publication/raw-DamageResult; no native damage execution");
}
