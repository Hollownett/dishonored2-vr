#include "../src/native_action_camera.hpp"
#include "native_rig_fixture.hpp"
#include <cassert>
#include <iostream>

int main() {
    auto memory=fixture();
    constexpr std::uintptr_t machine=0x220000000,pool=0x221000000,assassin=0x222000000,
        attack=0x223000000,block=0x224000000;
    memory.list(player_address+0x268,components_address,2,2);memory.put(components_address+8,machine);
    memory.put(machine,base_address+0x1c73d58);memory.put(machine+0x10,player_address);memory.put(machine+0x90,pool);
    const auto state=[&](unsigned index,std::uintptr_t at,std::uintptr_t vtable,unsigned char active) {
        memory.put(pool+0x10+index*8,at);memory.put(at,base_address+vtable);
        memory.put(at+8,index);memory.put(at+0x10,machine);memory.put(at+0x20,active);
    };
    state(13,assassin,0x1c83468,1);state(39,attack,0x1c85d28,0);state(44,block,0x1c81a68,0);
    d2vr::NativeActionCameraReader reader;
    auto action=reader.poll(memory,base_address,player_address,100);
    assert(action.scripted && !action.combat && action.owns_camera(false) && action.state_index==13);
    memory.put(assassin+0x20,static_cast<unsigned char>(0));memory.put(block+0x20,static_cast<unsigned char>(1));
    action=reader.poll(memory,base_address,player_address,101);
    assert(!action.scripted && action.combat && !action.owns_camera(true) && action.state_index==44);
    assert(action.owns_camera(true,true));
    {   // the hands follow an authored camera only into a scripted scene (a cutscene keeps them near the model, v642; user, headset v636: hands vanished on other camera modes)
        d2vr::NativeActionCamera plain{};
        assert(!plain.takes_hands(true,false) && !plain.takes_hands(true,true) && plain.takes_hands(true,true,false,true) && plain.takes_hands(true,false,true) && !plain.takes_hands(false,true,false,true));
        assert(d2vr::NativeActionCamera::cutscene_keeps_hands(true,false,.2,1.6) && !d2vr::NativeActionCamera::cutscene_keeps_hands(true,true,.2,1.6) &&
               !d2vr::NativeActionCamera::cutscene_keeps_hands(true,false,4,1.6) && !d2vr::NativeActionCamera::cutscene_keeps_hands(true,false,.2,6) && !d2vr::NativeActionCamera::cutscene_keeps_hands(false,false,.2,1.6));
        d2vr::NativeActionCamera scene{};scene.scripted=true;
        assert(scene.takes_hands(true,false) && !scene.takes_hands(false,false));
    }
    memory.put(block+0x20,static_cast<unsigned char>(0));memory.put(attack+0x20,static_cast<unsigned char>(1));
    action=reader.poll(memory,base_address,player_address,102);
    assert(!action.scripted && action.combat && !action.owns_camera(true) && action.state_index==39);
    // A silent assassination overrides an overlapping sword state; both flags
    // may legitimately be active during a native transition.
    memory.put(assassin+0x20,static_cast<unsigned char>(1));
    action=reader.poll(memory,base_address,player_address,103);
    assert(action.scripted && action.combat && action.owns_camera(true) && action.state_index==13); // the first active entry in table order
    memory.put(assassin+0x10,player_address);action=reader.poll(memory,base_address,player_address,104);
    assert(!action.scripted && action.state_index==39);memory.put(assassin+0x10,machine);
    bool changed=false;
    auto racing=[&](std::uintptr_t at,void* out,size_t size) {
        const bool ok=memory(at,out,size);
        if(at==assassin+0x20 && !changed) {changed=true;memory.put(assassin+0x20,static_cast<unsigned char>(0));}
        return ok;
    };
    action=reader.poll(racing,base_address,player_address,105);
    assert(changed && !action.scripted && !action.combat && action.state_index==-1); // reject mixed active-state snapshot
    std::cout<<"PASS: silent assassination owns camera, ordinary block/attack remain tracked, overlap priority, foreign and racing FSM rejection\n";
}
