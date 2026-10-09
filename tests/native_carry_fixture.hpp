#pragma once
#include "native_rig_fixture.hpp"
constexpr std::uintptr_t carry_machine=0x219000000,carry_pool=0x219010000;
constexpr std::array<std::uintptr_t,2> carry_states{0x219020000,0x219030000},carry_declarations{0x219040000,0x219050000};
inline void add_carry_fixture(Memory& m){
    m.list(player_address+0x268,components_address,2,2);m.put(components_address+8,carry_machine);
    m.put(carry_machine,base_address+0x1c73d58);m.put(carry_machine+0x10,player_address);m.put(carry_machine+0x18,r::Identity{500,10});
    m.put(carry_machine+0x300,body_address);m.put(carry_machine+0x90,carry_pool);
    constexpr std::array<int,2> indices{49,45};constexpr std::array<std::uintptr_t,2> tables{0x1c7a998,0x1c77748},decl_tables{0x1c7a868,0x1c77618};
    for(unsigned i=0;i<2;++i){m.put(carry_pool+0x10+static_cast<unsigned>(indices[i])*8,carry_states[i]);m.put(carry_states[i],base_address+tables[i]);
        m.put(carry_states[i]+8,indices[i]);m.put(carry_states[i]+0x10,carry_machine);m.put(carry_states[i]+0x18,carry_declarations[i]);
        m.put(carry_states[i]+0x20,static_cast<unsigned char>(0));m.put(carry_declarations[i],base_address+decl_tables[i]);m.put(carry_declarations[i]+0x60,indices[i]);}
}
