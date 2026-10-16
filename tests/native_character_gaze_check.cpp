#define NOMINMAX
#include "native_character_gaze_refresh_fixture.hpp"
#include "native_carry_fixture.hpp"
#include <cassert>
#include <iostream>
#include <limits>

extern "C" MH_STATUS WINAPI MH_CreateHook(LPVOID,LPVOID,LPVOID*) {return MH_OK;}
extern "C" MH_STATUS WINAPI MH_EnableHook(LPVOID) {return MH_OK;}
extern "C" MH_STATUS WINAPI MH_DisableHook(LPVOID) {return MH_OK;}

int main() {
    d2vr::NativeCharacterGazeFixture::run();
    moving_native_character_gaze_regression();
    using namespace d2vr;
    auto memory=fixture();add_carry_fixture(memory);
    memory.put(player_address+0xa8,std::uint32_t{1});
    for(const auto entry:native_action_camera_states)
        if(!entry.combat)memory.put(carry_pool+0x10+entry.index*8,std::uintptr_t{});
    const auto carry=rig::native_carry_snapshot(memory,base_address,player_address,body_address);assert(carry);
    const auto baseline=memory;
    NativePlayerCamera camera{};camera.player=player_address;camera.body=body_address;
    camera.camera=0x210000000;camera.controller=0x211000000;camera.camera_identity={11,10};camera.physics_identity={12,10};
    using Lanes=std::array<NativeGrab::DiagnosticLane,2>;
    Lanes lanes{};
    for(auto& lane:lanes) {
        lane.epoch=7;lane.player=camera.player;lane.camera=camera.camera;lane.publication_owner=camera;
        lane.serial=1;lane.lifecycle_revision=2;lane.publication_tick=1000;lane.source_tick=1000;
        lane.source_age_valid=true;lane.usable=true;lane.role=RightGripOwner::Role::ready;
    }
    const auto baseline_lanes=lanes;
    bool idle=true,change_lifecycle=false;unsigned diagnostics_calls{};
    std::uint64_t read_calls{},read_bytes{};
    const auto read=[&](std::uintptr_t address,void* output,std::size_t size) {
        ++read_calls;read_bytes+=size;return memory(address,output,size);
    };
    auto diagnostics=[&](std::uint64_t)->std::optional<Lanes> {
        ++diagnostics_calls;if(change_lifecycle && diagnostics_calls==2)++lanes[0].lifecycle_revision;
        return lanes;
    };
    const auto gate=[&] {
        diagnostics_calls=0;
        return native_character_gaze_idle(read,base_address,camera,*carry,7,1001,diagnostics,[&]{return idle;},[] {return std::uint64_t{1001};});
    };
    assert(gate());
    std::cout<<"idle read_calls="<<read_calls<<" read_bytes="<<read_bytes<<'\n';
    for(auto& lane:lanes)lane.gate=NativeGrab::Gate::native_use;
    assert(gate());lanes=baseline_lanes;
    std::optional<NativeOfferedChoke::Receipt> offered=NativeOfferedChoke::Receipt{};
    offered->owner={base_address,carry->game,carry->player,carry->machine,1,carry->machine_identity};
    assert(native_character_choke_owner(offered,camera,*carry));
    const auto matching_offer=offered;
    offered.reset();assert(!native_character_choke_owner(offered,camera,*carry));
    offered=matching_offer;offered->owner.player+=8;assert(!native_character_choke_owner(offered,camera,*carry));
    offered=matching_offer;offered->owner.machine+=8;assert(!native_character_choke_owner(offered,camera,*carry));
    offered=matching_offer;offered->owner.identity.generation++;assert(!native_character_choke_owner(offered,camera,*carry));
    offered=matching_offer;offered->owner.game+=8;assert(!native_character_choke_owner(offered,camera,*carry));
    idle=false;assert(!gate());idle=true;
    for(unsigned side=0;side<2;++side) {
        lanes=baseline_lanes;lanes[side].occupied=true;assert(!gate());
        lanes=baseline_lanes;lanes[side].held=NativeRigidBody{};assert(!gate());
        lanes=baseline_lanes;lanes[side].pending_release=true;assert(!gate());
        lanes=baseline_lanes;lanes[side].deferred=true;assert(!gate());
        lanes=baseline_lanes;lanes[side].raw_grip=.35f;assert(!gate());
        lanes=baseline_lanes;lanes[side].raw_grip=std::numeric_limits<float>::quiet_NaN();assert(!gate());
        lanes=baseline_lanes;lanes[side].role=RightGripOwner::Role::physical;assert(!gate());
        lanes=baseline_lanes;lanes[side].role=RightGripOwner::Role::native;assert(!gate());
        lanes=baseline_lanes;lanes[side].epoch++;assert(!gate());
        lanes=baseline_lanes;lanes[side].player+=8;assert(!gate());
        lanes=baseline_lanes;lanes[side].camera+=8;assert(!gate());
        lanes=baseline_lanes;lanes[side].publication_owner.controller+=8;assert(!gate());
        lanes=baseline_lanes;lanes[side].publication_owner.physics+=8;assert(!gate());
        lanes=baseline_lanes;lanes[side].publication_owner.heading_delegate+=8;assert(!gate());
        lanes=baseline_lanes;lanes[side].publication_owner.physics_identity.generation++;assert(!gate());
        lanes=baseline_lanes;lanes[side].source_tick=1;assert(!gate());
        lanes=baseline_lanes;lanes[side].publication_owner.mode=1;assert(!gate());
    }
    lanes=baseline_lanes;change_lifecycle=true;assert(!gate());change_lifecycle=false;lanes=baseline_lanes;
    for(const auto state:carry_states) {
        memory=baseline;memory.put(state+0x20,static_cast<unsigned char>(1));assert(!gate());
    }
    memory=baseline;
    for(const auto entry:native_action_camera_states)if(!entry.combat) {
        constexpr std::uintptr_t action_state=0x230000000;
        memory=baseline;memory.put(carry_pool+0x10+entry.index*8,action_state);
        memory.put(action_state,base_address+entry.vtable);memory.put(action_state+8,entry.index);
        memory.put(action_state+0x10,carry_machine);memory.put(action_state+0x20,static_cast<unsigned char>(1));
        assert(!gate());
    }
    memory=baseline;memory.put(carry_machine+0x18,rig::Identity{501,10});assert(!gate());
    memory=baseline;assert(gate());
    std::cout<<"PASS: composed native carry/scripted action and physical-lane ownership admission\n";
}
