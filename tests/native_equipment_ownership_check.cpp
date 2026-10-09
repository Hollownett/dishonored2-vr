#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "../src/native_equipment_ownership.hpp"
#include "native_rig_fixture.hpp"
#include <cassert>
#include <iostream>
#include <cstdlib>
static bool forbid_allocations{};
void* operator new(std::size_t size) {
    if(forbid_allocations) throw std::bad_alloc();
    if(auto* memory=std::malloc(size)) return memory;
    throw std::bad_alloc();
}
void operator delete(void* memory) noexcept {std::free(memory);}
int main() {
    using namespace d2vr;using namespace d2vr::rig;
    constexpr std::uintptr_t inventory=0x20c000000,item=0x20d000000,item_decl=0x20e000000,
        entity=0x20f000000,parent=0x210000000,binding=0x211000000,equipment=0x212000000,
        equipment_decl=0x213000000,child_components=0x214000000,attachment_decl=0x215000000;
    auto memory=fixture();const auto arms=player_arms(memory,base_address);assert(arms);
    NativeArmBindingSnapshot snapshot{*arms,{},{}};
    memory.put(player_address+0xa8,std::uint32_t{0x10});
    memory.put(entity+0xa8,std::uint32_t{0x10010});memory.put(entity+0xac,std::uint32_t{0x10});
    memory.put(parent+0xa8,std::uint32_t{0x20010});
    memory.put(game_address+0x3130b0+8,entity);memory.put(game_address+0x3130b0+16,parent);
    memory.put(entity,base_address+0x1ce1e98);memory.put(parent,base_address+0x1c87d58);
    memory.put(parent+0xf0,models[1]);memory.put(animations[1]+0x10,parent);
    memory.put(body_address+0x680,std::uint32_t{0x20010});memory.put(body_address+0x60,inventory);
    memory.list(player_address+0x268,components_address,2,2);memory.put(components_address+8,inventory);
    memory.put(inventory,base_address+0x1b9a878);memory.put(inventory+0x10,player_address);
    memory.put(inventory+0x18,Identity{300,10});memory.put(inventory+0x68,std::array<std::uintptr_t,2>{item,0});
    memory.list(inventory+0x38,item,1,1);memory.put(item,std::array<unsigned char,0x38>{});
    memory.put(item,item_decl);memory.put(item+8,std::uint32_t{0x10010});memory.put(item+0x10,player_address);
    memory.put(item_decl,base_address+0x1b9ea48);
    memory.put(binding,base_address+0x1c573e8);memory.put(binding+0x20,base_address+0x1c574c0);
    memory.put(binding+8,attachment_decl);memory.put(attachment_decl,base_address+0x123450);
    memory.put(binding+0x10,entity);memory.put(entity+0x358,binding);memory.put(binding+0x18,Identity{301,10});
    memory.put(binding+0x3c8,parent);memory.put(binding+0x400,animations[1]);
    memory.put(binding+0x3d0,std::int16_t{59});memory.put(binding+0x84,std::uint32_t{100});
    memory.list(entity+0x268,child_components,2,2);memory.put(child_components,std::array<std::uintptr_t,2>{binding,equipment});
    memory.put(equipment,base_address+0x1b9d508);memory.put(equipment+8,equipment_decl);
    memory.put(equipment+0x10,entity);memory.put(equipment+0x18,Identity{302,10});memory.put(equipment+0x38,body_address);
    memory.put(equipment_decl,base_address+0x1b9d3c8);memory.put(equipment_decl+0x68,std::int32_t{0});
    const auto candidate=native_attachment_candidate(memory,base_address,snapshot,binding,3);assert(candidate);
    const auto acquire=[&](auto& read){return native_sword_equipment_ownership(read,base_address,snapshot,*candidate);};
    const auto lease=acquire(memory);assert(lease && lease->equipment==equipment && lease->item!=equipment && native_equipment_current(memory,*lease));
    forbid_allocations=true;assert(native_equipment_current(memory,*lease));forbid_allocations=false;
    assert(native_sword_equipment_ownership(memory,base_address,snapshot));
    auto chunked=*lease;std::array<unsigned char,700> range{};range.fill(7);
    constexpr std::uintptr_t chunk_address=0x216000000;
    memory.put(chunk_address,range);chunked.guards.push_back({chunk_address,{range.begin(),range.end()}});
    std::size_t largest{};auto bounded=[&](std::uintptr_t at,void* out,std::size_t size) {largest=std::max(largest,size);return memory(at,out,size);};
    forbid_allocations=true;assert(native_equipment_current(bounded,chunked));forbid_allocations=false;assert(largest<=256);
    bool changed_early{};auto mid_read=[&](std::uintptr_t at,void* out,std::size_t size) {
        const auto okay=memory(at,out,size);
        if(at==chunk_address+256 && !changed_early) {changed_early=true;memory.put(chunk_address,std::uint8_t{8});}
        return okay;
    };
    forbid_allocations=true;assert(!native_equipment_current(mid_read,chunked));forbid_allocations=false;
    memory.put(chunk_address,std::uint8_t{7});
    auto rejected=[&](auto at,auto wrong){auto changed=memory;changed.put(at,wrong);assert(!acquire(changed));assert(!native_equipment_current(changed,*lease));};
    rejected(item+8,std::uint32_t{0x10011}); // Stale registry generation, same entity index.
    auto recycled=memory;recycled.put(equipment+0x18,Identity{303,10});
    assert(!native_equipment_current(recycled,*lease));assert(acquire(recycled)); // Fresh lease captures new generation.
    rejected(equipment+0x38,body_address+0x1000);
    rejected(equipment_decl,base_address+0x1b9ad28); // Base declaration cannot replace exact Sword declaration.
    rejected(equipment_decl+0x68,std::int32_t{1});
    rejected(game_address+0x3130b0+8,entity+0x1000);
    rejected(body_address+0x680,std::uint32_t{0x20011});
    rejected(entity+0xac,std::uint32_t{0x10010});
    rejected(inventory+0x38,List{item+0x38,1,1<<8}); // Slot falls outside owner's list.
    rejected(child_components,std::array<std::uintptr_t,2>{equipment,equipment}); // Missing attachment/duplicate component.
    memory.put(binding+0x84,std::uint32_t{101});assert(native_equipment_current(memory,*lease));
    memory.recycle_at=equipment+0x18;assert(!acquire(memory)); // Mutation during acquisition must reject.
    assert(!native_equipment_current(memory,*lease));
    std::cout<<"PASS: typed Sword inventory item/registry/component/declaration ownership, list membership, slot side, ABA generation and mid-read rejection; native frame progress excluded\n";
}
