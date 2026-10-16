#include "../src/native_carry_snapshot.hpp"
#include "native_carry_fixture.hpp"
#include <cassert>
#include <iostream>
int main(){
    auto memory=fixture();add_carry_fixture(memory);const auto original=memory;
    auto state=r::native_carry_snapshot(memory,base_address,player_address,body_address);assert(state && !state->carrying() && state->valid_owner() && !state->hands());
    const auto vacant=*state;
    for(unsigned i=0;i<3;++i){memory.put(carry_states[i]+0x20,static_cast<unsigned char>(1));
        state=r::native_carry_snapshot(memory,base_address,player_address,body_address);assert(state && state->carrying() && state->active[i] && state->hands()==(i==1?2u:3u));
        assert(state->hold_object()==(i!=1) && state->hold_state()==(i==1?0:carry_states[i]));
        const auto current=r::native_carry_current(memory,base_address,vacant);assert(current && current->carrying() && *current!=vacant);memory=original;}
    memory.put(carry_states[0]+0x20,static_cast<unsigned char>(2));assert(!r::native_carry_current(memory,base_address,vacant));memory=original;
    memory.put(carry_states[1],base_address+0x1c77758);assert(!r::native_carry_current(memory,base_address,vacant));memory=original;
    memory.put(carry_states[0]+8,45);assert(!r::native_carry_current(memory,base_address,vacant));memory=original;
    memory.put(carry_declarations[0]+0x60,45);assert(!r::native_carry_current(memory,base_address,vacant));memory=original;
    memory.put(carry_declarations[1],base_address+0x1c7a868);assert(!r::native_carry_current(memory,base_address,vacant));memory=original;
    memory.put(carry_states[0]+0x10,carry_machine+8);assert(!r::native_carry_current(memory,base_address,vacant));memory=original;
    memory.put(carry_machine+0x300,body_address+8);assert(!r::native_carry_current(memory,base_address,vacant));memory=original;
    memory.put(carry_machine+0x90,carry_pool+8);assert(!r::native_carry_current(memory,base_address,vacant));memory=original;
    memory.put(components_address+8,body_address);assert(!r::native_carry_snapshot(memory,base_address,player_address,body_address));assert(!r::native_carry_current(memory,base_address,vacant));memory=original;
    memory.put(carry_machine+0x18,r::Identity{501,10});assert(!r::native_carry_current(memory,base_address,vacant));memory=original;
    memory.recycle_at=carry_states[0]+0x20;assert(!r::native_carry_current(memory,base_address,vacant));memory=original;
    memory.put(carry_states[0]+0x20,static_cast<unsigned char>(1));memory.put(carry_states[2]+0x20,static_cast<unsigned char>(1));
    assert(!r::native_carry_current(memory,base_address,vacant));assert(!r::native_carry_snapshot(memory,base_address,player_address,body_address));memory=original;
    for(const auto at:std::array<std::uintptr_t,7>{carry_states[2],carry_states[2]+8,carry_states[2]+0x10,carry_states[2]+0x18,
            carry_declarations[2],carry_declarations[2]+0x60,carry_pool+0x10+50*8}){
        memory.put(at,std::uintptr_t{});assert(!r::native_carry_current(memory,base_address,vacant));memory=original;
    }
    memory.bytes.erase(carry_states[2]+0x20);assert(!r::native_carry_current(memory,base_address,vacant));memory=original;
    memory.put(carry_states[2]+0x20,static_cast<unsigned char>(2));assert(!r::native_carry_current(memory,base_address,vacant));memory=original;
    memory.recycle_at=carry_states[2]+0x20;assert(!r::native_carry_current(memory,base_address,vacant));memory=original;
    auto invalid=vacant;invalid.active[0]=invalid.active[2]=true;assert(!invalid.hold_state());
    memory.list(player_address+0x268,components_address,3,3);memory.put(components_address+16,carry_machine);
    assert(!r::native_carry_snapshot(memory,base_address,player_address,body_address));
    assert(!r::native_carry_current(memory,base_address,r::NativeCarrySnapshot{}));
    std::cout<<"PASS: typed HoldObject49/50 and CarryCorpse45, mixed hold/read/type/declaration/owner/race rejection\n";
}
