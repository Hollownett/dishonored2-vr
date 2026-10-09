#include "../src/native_hand_occupancy.hpp"
#include "native_carry_fixture.hpp"
#include <cassert>
#include <iostream>
int main() {
    auto memory=fixture();constexpr std::uintptr_t inventory=0x211000000,graph=0x212000000,
        left_names=0x213000000,right_names=0x214000000,empty=0x215000000,gun=0x215000100;
    add_carry_fixture(memory);
    memory.put(body_address+0x60,inventory);memory.put(inventory,base_address+0x1b9a878);
    memory.put(inventory+0x10,player_address);memory.put(inventory+0x18,r::Identity{400,10});
    memory.put(inventory+0x68,std::array<std::uintptr_t,2>{0,0});
    memory.put(graph,base_address+0x1c4d448);
    const std::array<char,6> empty_name{'E','m','p','t','y','\0'},gun_name{'G','u','n','\0','\0','\0'};
    memory.put(empty,empty_name);memory.put(gun,gun_name);
    memory.list(graph+0xa0,left_names,2,2);memory.list(graph+0xb0,right_names,2,2);
    for(auto names:{left_names,right_names}) {memory.put(names,empty);memory.put(names+8,gun);}
    for(unsigned side=0;side<2;++side) {
        memory.put(descriptors[side]+0x138,graph);
        const auto part=body_address+(side?0x680:0x370),node=graph+(side?0x1d0:0x190);
        memory.put(part+0x18,node);memory.put(part+0x21,std::int8_t{0});memory.put(node,base_address+0x1c4d438);
    }
    auto state=r::native_hand_state(memory,base_address);assert(state && state->empty_candidates==3);
    const auto original=memory;
    assert(r::native_empty_side(memory,base_address,*state,3,state->arms[0])==1);
    assert(r::native_empty_side(memory,base_address,*state,3,state->arms[1])==2);
    assert(r::native_empty_side(memory,base_address,*state,0,state->arms[0])==0);
    const auto captured=*state;
    // Equipment remains Empty/null while native bottle or corpse carry is active.
    for(unsigned i=0;i<2;++i){memory.put(carry_states[i]+0x20,static_cast<unsigned char>(1));
        const auto held=r::native_hand_state(memory,base_address);assert(held && held->carrying && held->empty_candidates==0 && held->slots[0]==0 && held->slots[1]==0);
        assert(r::native_empty_side(memory,base_address,captured,3,captured.arms[0])==0);
        assert(r::native_empty_side(memory,base_address,captured,3,captured.arms[1])==0);memory=original;}
    memory.put(carry_states[0]+0x20,static_cast<unsigned char>(2));assert(!r::native_hand_state(memory,base_address));memory=original;
    memory.put(carry_machine+0x18,r::Identity{501,10});assert(r::native_empty_side(memory,base_address,captured,3,captured.arms[0])==0);memory=original;
    bool flipped=false;
    auto carry_during_callback=[&](std::uintptr_t at,void* out,std::size_t size){
        assert(!(at==components_address && size>8)); // Fixed revalidation must not enumerate components.
        const bool accepted=memory(at,out,size);
        if(at==empty && !flipped){flipped=true;memory.put(carry_states[0]+0x20,static_cast<unsigned char>(1));}
        return accepted;
    };
    assert(r::native_empty_side(carry_during_callback,base_address,captured,3,captured.arms[0])==0 && flipped);memory=original;
    memory.put(body_address+0x391,std::int8_t{1});
    assert(r::native_empty_side(memory,base_address,captured,3,captured.arms[0])==0);memory=original;
    memory.put(inventory+0x68,std::uintptr_t{0x216000000});
    assert(r::native_empty_side(memory,base_address,captured,3,captured.arms[1])==0);memory=original;
    memory.put(empty,gun_name);
    assert(r::native_empty_side(memory,base_address,captured,3,captured.arms[0])==0);memory=original;
    memory.put(descriptors[0]+0x138,graph+8);
    assert(r::native_empty_side(memory,base_address,captured,3,captured.arms[0])==0);memory=original;
    memory.recycle_at=inventory+0x18;
    assert(r::native_empty_side(memory,base_address,captured,3,captured.arms[0])==0);memory=original;
    auto mismatched=captured.arms[0];mismatched.animation_identity.generation++;
    assert(r::native_empty_side(memory,base_address,captured,3,mismatched)==0);
    // Zero inventory slots alone cannot classify a weapon/transition pose.
    memory.put(body_address+0x391,std::int8_t{1});state=r::native_hand_state(memory,base_address);
    assert(state && state->empty_candidates==2);memory=original;
    memory.put(inventory+0x68,std::uintptr_t{0x216000000});state=r::native_hand_state(memory,base_address);
    assert(state && state->empty_candidates==1 && state->slots[1]==0x216000000);memory=original;
    memory.put(inventory+0x10,player_address+8);assert(!r::native_hand_state(memory,base_address));memory=original;
    memory.put(graph,base_address+0x1c4d458);assert(!r::native_hand_state(memory,base_address));memory=original;
    memory.put(body_address+0x388,graph+0x1d0);assert(!r::native_hand_state(memory,base_address));memory=original;
    memory.put(body_address+0x391,std::int8_t{-1});assert(!r::native_hand_state(memory,base_address));memory=original;
    memory.recycle_at=inventory+0x18;assert(!r::native_hand_state(memory,base_address));memory=original;
    state=r::native_hand_state(memory,base_address);assert(state);
    r::NativeHandOccupancy latch;
    for(unsigned i=0;i<20;++i) assert(latch.observe(state,i*25,1,true)==0);
    assert(latch.observe(state,500,1,true)==0);
    for(unsigned i=1;i<=20;++i) latch.observe(state,500+i*25,1,true);
    assert(latch.observe(state,1000,1,true)==3);
    auto native_carry=*state;native_carry.carry.active[0]=true;native_carry.carrying=true;
    assert(latch.observe(native_carry,1000,1,true)==0); // Carry revokes on the same tick.
    for(unsigned i=0;i<=40;++i)latch.observe(state,i*25,1,true);
    assert(latch.observe(state,1000,1,true)==3);
    auto armed=*state;armed.empty_candidates=1;
    assert(latch.observe(armed,1000,1,true)==1); // Same-tick equip clears immediately.
    assert(latch.observe(armed,1025,1,true)==1);
    assert(latch.observe(state,1050,2,true)==0); // Reference change requires a fresh window.
    assert(latch.observe(state,1075,2,false)==0);
    for(unsigned i=0;i<=40;++i) latch.observe(state,2000+i*25,3,true);
    assert(latch.observe(state,3000,3,true)==3);
    auto recycled=*state;recycled.inventory_identity.generation++;
    assert(latch.observe(recycled,3025,3,true)==0);
    assert(latch.observe({},3050,3,true)==0);
    std::cout<<"PASS: exact player inventory/arm graph, null-slot plus Empty requirement, mixed/stale ownership rejection, sustained eligibility and immediate equip/focus/epoch cancellation\n";
}
