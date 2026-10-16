#pragma once
#include "../src/native_interaction_aim.hpp"
#include "native_carry_fixture.hpp"
#include <cassert>
#include <iostream>


inline void moving_native_character_gaze_regression() {
    using namespace d2vr;
    auto memory=fixture();add_carry_fixture(memory);memory.put(player_address+0xa8,std::uint32_t{1});
    for(const auto entry:native_action_camera_states)
        if(!entry.combat)memory.put(carry_pool+0x10+entry.index*8,std::uintptr_t{});
    const auto carry=rig::native_carry_snapshot(memory,base_address,player_address,body_address);assert(carry);
    const auto baseline_memory=memory;
    NativePlayerCamera camera{};camera.player=player_address;camera.body=body_address;
    camera.camera=0x210000000;camera.controller=0x211000000;camera.camera_identity={11,10};camera.physics_identity={12,10};
    using Lanes=std::array<NativeGrab::DiagnosticLane,2>;
    Lanes lanes{};
    for(auto& lane:lanes) {
        lane.epoch=7;lane.player=camera.player;lane.camera=camera.camera;lane.publication_owner=camera;
        lane.serial=10;lane.lifecycle_revision=2;lane.publication_tick=1000;lane.source_tick=1000;
        lane.source_age_valid=true;lane.usable=true;lane.role=RightGripOwner::Role::ready;lane.gate=NativeGrab::Gate::native_use;
    }
    const auto baseline_lanes=lanes;
    unsigned scenario{},samples{},clock_calls{},idle_calls{};
    std::uint64_t reads{},bytes{};
    auto read=[&](std::uintptr_t address,void* output,std::size_t size){++reads;bytes+=size;return memory(address,output,size);};
    const auto idle=[&]{++idle_calls;return !(scenario==17 && idle_calls==2);};
    const auto clock=[&] {
        ++clock_calls;
        if(scenario==20 && clock_calls==1)return std::uint64_t{1003};
        if(scenario==22 && clock_calls==1)return std::uint64_t{1000};
        if(scenario==18 && clock_calls==2)return std::uint64_t{1000};
        return std::uint64_t{clock_calls==1?1001u:1005u};
    };
    const auto diagnostics=[&](std::uint64_t tick)->std::optional<Lanes> {
        ++samples;
        if(samples==1 && (scenario==20 || scenario==21)) {
            for(auto& lane:lanes) {
                lane.publication_tick=1002;lane.source_tick=1002;lane.source_age_valid=tick>=lane.source_tick;
            }
        }
        if(samples==1 && scenario==23)lanes[0].source_tick=1;
        if(samples==2) {
            for(auto& lane:lanes) {
                lane.serial+=1;lane.publication_tick=1004;lane.source_tick=1004;
                lane.source_age_valid=tick>=lane.source_tick;
            }
            if(scenario==1)++lanes[0].lifecycle_revision;
            if(scenario==2)lanes[0].serial=9;
            if(scenario==3)lanes[0].publication_tick=999;
            if(scenario==4)lanes[0].source_tick=999;
            if(scenario==5)lanes[0].publication_tick=1006;
            if(scenario==6)lanes[0].source_tick=1006;
            if(scenario==7)lanes[0].source_tick=1;
            if(scenario==8)lanes[0].held=NativeRigidBody{};
            if(scenario==9)lanes[0].pending_release=true;
            if(scenario==10)lanes[0].deferred=true;
            if(scenario==11)lanes[0].raw_grip=.35f;
            if(scenario==12)lanes[0].role=RightGripOwner::Role::physical;
            if(scenario==13)++lanes[0].epoch;
            if(scenario==14)lanes[0].publication_owner.physics_identity.generation++;
            if(scenario==15)lanes[0].occupied=true;
            if(scenario==16)lanes[0].camera+=8;
            if(scenario==19)lanes[0].source_tick=0;
        }
        return lanes;
    };
    const auto run=[&](unsigned chosen) {
        scenario=chosen;samples=clock_calls=idle_calls=0;lanes=baseline_lanes;reads=bytes=0;
        return native_character_gaze_idle(read,base_address,camera,*carry,7,1001,diagnostics,idle,clock);
    };
    assert(run(0));assert(samples==2 && clock_calls==2 && !lanes[0].source_age_valid);
    std::cout<<"refresh accepted read_calls="<<reads<<" read_bytes="<<bytes<<'\n';
    for(unsigned chosen=1;chosen<=19;++chosen)assert(!run(chosen));
    assert(run(20));assert(!run(21));assert(!run(22));assert(!run(23));
    memory=baseline_memory;
    for(const auto state:carry_states) {
        memory=baseline_memory;memory.put(state+0x20,static_cast<unsigned char>(1));assert(!run(0));
    }
    for(const auto entry:native_action_camera_states)if(!entry.combat) {
        constexpr std::uintptr_t action_state=0x230000000;
        memory=baseline_memory;memory.put(carry_pool+0x10+entry.index*8,action_state);
        memory.put(action_state,base_address+entry.vtable);memory.put(action_state+8,entry.index);
        memory.put(action_state+0x10,carry_machine);memory.put(action_state+0x20,static_cast<unsigned char>(1));assert(!run(0));
    }
    memory=baseline_memory;memory.put(carry_machine+0x18,rig::Identity{501,10});assert(!run(0));
    memory=baseline_memory;assert(run(0));
    const auto refuse=[] {return false;};samples=clock_calls=0;lanes=baseline_lanes;
    assert(!native_character_gaze_idle(read,base_address,camera,*carry,7,1001,diagnostics,refuse,clock));
    assert(samples==0 && clock_calls==0);
    std::cout<<"PASS: semantic refresh; live clock; rollback/future/stale/lifecycle/physical/carry/action/idle revocation\n";
}

namespace d2vr {
struct NativeCharacterGazeFixture {
    static void run() {
        NativeInteractionAim aim;
        NativePlayerCamera camera{};camera.player=0x100000;camera.camera=0x200000;camera.camera_identity={11,10};
        WorldTrackedPose pose{};pose.basis={Vec3{1,0,0},Vec3{0,1,0},Vec3{0,0,1}};
        std::array<std::optional<WorldTrackedPose>,2> tracked_poses{pose,pose};
        const auto publish=[&](std::uint64_t epoch=7){aim.publish_hands(camera,tracked_poses,epoch,GetTickCount64());};
        const auto capture=[&] {
            NativeInteractionAim::Context scope{};scope.camera=camera;scope.side=aim.routing_.selected();scope.stage=1;
            scope.hand=aim.hands_[scope.side];scope.revision=aim.routing_.revision();scope.ownership_revision=aim.routing_.ownership_revision();
            return scope;
        };
        publish();auto scope=capture();assert(aim.idle_character_publication(scope));
        const auto ownership=scope.ownership_revision;
        tracked_poses[0]->position.x+=.1;publish();
        aim.routing_.next({true,true},GetTickCount64()+1000);
        assert(aim.routing_.revision()!=scope.revision && aim.routing_.ownership_revision()==ownership);
        assert(aim.idle_character_publication(scope));
        aim.lock(1);assert(!aim.idle_character_publication(scope));
        aim.unlock();assert(!aim.idle_character_publication(scope));
        scope=capture();assert(aim.idle_character_publication(scope));
        aim.clear();assert(!aim.idle_character_publication(scope));publish();
        assert(!aim.idle_character_publication(scope));
        scope=capture();assert(aim.idle_character_publication(scope));
        aim.publish_hands(camera,tracked_poses,7,GetTickCount64(),{},{true,false});
        assert(!aim.idle_character_publication(scope));publish();
        assert(!aim.idle_character_publication(scope));
        scope=capture();publish(8);assert(!aim.idle_character_publication(scope));publish();
        scope=capture();tracked_poses[1].reset();publish();assert(!aim.idle_character_publication(scope));
        tracked_poses[1]=pose;publish();scope=capture();camera.player+=8;publish();assert(!aim.idle_character_publication(scope));
        camera.player-=8;publish();scope=capture();aim.published_[scope.side]=false;assert(!aim.idle_character_publication(scope));
        aim.published_[scope.side]=true;aim.hands_[scope.side].active=false;assert(!aim.idle_character_publication(scope));
        std::cout<<"PASS: actual Aim publication/fairness accepts; clear-republish/lock-unlock/reach/epoch/owner/tracking ABA revokes\n";
    }
};
}
