#include "../src/native_lean_snapshot.hpp"
#include "native_rig_fixture.hpp"
#include <cassert>
#include <iostream>
int main() {
    constexpr std::uintptr_t machine=0x220000000,controller=0x220010000,pool=0x220020000,state=0x220030000,decl=0x220040000,device=0x220050000;
    auto m=fixture();m.put(player_address+0xa8,std::uint32_t{16});
    m.list(player_address+0x268,components_address,2,2);m.put(components_address+8,machine);
    m.put(machine,base_address+0x1c73d58);m.put(machine+0x10,player_address);m.put(machine+0x18,r::Identity{10,10});
    m.put(machine+0x2f8,controller);m.put(machine+0x300,body_address);
    m.put(controller,base_address+0x1c72918);m.put(controller+0x10,player_address);m.put(controller+0x18,r::Identity{11,10});
    m.put(machine+0x90,pool);m.put(pool+0x10+18*8,state);m.put(state,base_address+0x1c82b28);
    m.put(state+8,18);m.put(state+0x10,machine);m.put(state+0x18,decl);m.put(state+0x20,std::uint8_t{1});m.put(state+0x30,std::uint32_t{11});
    m.put(decl,base_address+0x1c829f8);m.put(decl+0x60,18);
    m.put(base_address+0x2274340,device);m.put(device+0xa544,std::uint8_t{1});
    m.put(base_address+0x3995628,1);m.put(base_address+0x3995630,base_address+0x1c82bf8);
    const auto baseline=m;const auto owner=d2vr::find_native_lean_owner(m,base_address);assert(owner);
    auto snap=d2vr::native_lean_snapshot(m,base_address,*owner);assert(snap && snap->active && snap->gamepad && snap->toggle_option==1 && snap->action_index==11);
    // Snapshot polling succeeds without the component list: cache is bounded.
    m.bytes.erase(player_address+0x268);assert(d2vr::native_lean_snapshot(m,base_address,*owner));m=baseline;
    m.put(state+0x20,std::uint8_t{0});snap=d2vr::native_lean_snapshot(m,base_address,*owner);assert(snap && !snap->active);m=baseline;
    for(auto at:{machine+0x18,controller+0x18,body_address+0x18}) {
        m.put(at,r::Identity{99,10});assert(!d2vr::native_lean_snapshot(m,base_address,*owner));m=baseline;
    }
    m.recycle_at=machine+0x18;assert(!d2vr::native_lean_snapshot(m,base_address,*owner));m=baseline;
    m.put(game_address+0x3130b0,player_address+8);assert(!d2vr::native_lean_snapshot(m,base_address,*owner));m=baseline;
    m.put(player_address+0xa8,std::uint32_t{17});assert(!d2vr::native_lean_snapshot(m,base_address,*owner));m=baseline;
    m.put(state+0x10,machine+8);assert(!d2vr::native_lean_snapshot(m,base_address,*owner));m=baseline;
    m.put(state+8,17);assert(!d2vr::native_lean_snapshot(m,base_address,*owner));m=baseline;
    m.put(state,base_address+0x1c82b30);assert(!d2vr::native_lean_snapshot(m,base_address,*owner));m=baseline;
    m.put(state+0x20,std::uint8_t{2});assert(!d2vr::native_lean_snapshot(m,base_address,*owner));m=baseline;
    m.put(state+0x30,std::uint32_t{64});assert(!d2vr::native_lean_snapshot(m,base_address,*owner));m=baseline;
    m.put(decl+0x60,17);assert(!d2vr::native_lean_snapshot(m,base_address,*owner));m=baseline;
    m.put(base_address+0x3995630,base_address+0x1c82bf0);assert(!d2vr::native_lean_snapshot(m,base_address,*owner));m=baseline;
    m.put(device+0xa544,std::uint8_t{2});assert(!d2vr::native_lean_snapshot(m,base_address,*owner));m=baseline;
    m.put(device+0xa544,std::uint8_t{0});snap=d2vr::native_lean_snapshot(m,base_address,*owner);assert(snap && !snap->gamepad);m=baseline;
    m.put(base_address+0x3995628,0);snap=d2vr::native_lean_snapshot(m,base_address,*owner);assert(snap && !snap->toggle_option);m=baseline;
    auto changing=[&](std::uintptr_t at,void* out,std::size_t size) {
        const bool ok=m(at,out,size);if(at==state+0x20 && ok)m.put(at,std::uint8_t{0});return ok;
    };
    assert(!d2vr::native_lean_snapshot(changing,base_address,*owner));m=baseline;
    auto pool_replaced=[&](std::uintptr_t at,void* out,std::size_t size) {
        const bool ok=m(at,out,size);if(at==pool+0x10+18*8 && ok)m.put(machine+0x90,pool+8);return ok;
    };
    assert(!d2vr::native_lean_snapshot(pool_replaced,base_address,*owner));m=baseline;
    auto owner_replaced=[&](std::uintptr_t at,void* out,std::size_t size) {
        const bool ok=m(at,out,size);if(at==controller+0x10 && ok)m.put(at,player_address+8);return ok;
    };
    assert(!d2vr::native_lean_owner(owner_replaced,base_address,player_address,machine));m=baseline;
    m.list(player_address+0x268,components_address,3,3);m.put(components_address+16,machine);
    assert(!d2vr::find_native_lean_owner(m,base_address));
    std::cout<<"PASS: exact lean owner/state/declaration/device, bounded cached polling, active/inactive, malformed and mixed ownership rejection\n";
}
