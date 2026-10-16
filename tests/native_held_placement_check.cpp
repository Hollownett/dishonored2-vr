#define NOMINMAX
#include "../src/native_held_placement.hpp"
#include "native_carry_fixture.hpp"
#include <cassert>
#include <functional>
#include <iostream>
#include <stdexcept>

namespace {
using Placement=d2vr::NativeHeldPlacement;
Memory memory;
constexpr std::uintptr_t entity=0x230000000,rf=0x231000000,fixture_components=0x232000000;
constexpr std::uint32_t spawn=0x20002;
bool read_fail{},read_throw{},read_seh{};std::function<void()> during_read;
bool read_memory(std::uintptr_t at,void* output,std::size_t size){
    if(read_fail)return false;if(read_throw)throw std::runtime_error("fixture");
    if(read_seh)RaiseException(EXCEPTION_ACCESS_VIOLATION,0,0,nullptr);
    const bool result=memory(at,output,size);if(during_read)during_read();return result;
}
void setup(){
    memory=fixture();add_carry_fixture(memory);memory.put(carry_states[0]+0x20,static_cast<unsigned char>(1));
    memory.put(carry_states[0]+0x68,spawn);memory.put(game_address+0x3130b0+(spawn>>16)*8ull,entity);
    memory.put(entity+0xa8,spawn);memory.put(entity+0x358,rf);memory.list(entity+0x268,fixture_components,1,2);memory.put(fixture_components,rf);
    memory.put(rf,base_address+0x1c573e8);memory.put(rf+0x20,base_address+0x1c574c0);
    memory.put(rf+0x18,r::Identity{42,10});memory.put(rf+0x10,entity);
}
}
int main(){
    assert(&Placement::process_owner()==&Placement::process_owner());
    static_assert(noexcept(std::declval<const Placement&>().current(0,0,0,0,0,0)));
    static_assert(noexcept(std::declval<Placement&>().completed(0,0,0,0,0,0)));
    static_assert(noexcept(std::declval<const Placement&>().current_driven_side(std::declval<const r::NativeCarrySnapshot&>(),0,0)));
    static_assert(noexcept(std::declval<const Placement&>().current_driven_right(0,0,std::declval<const r::NativeCarrySnapshot&>(),0)));
    static_assert(noexcept(std::declval<Placement&>().clear()));
    static_assert(noexcept(std::declval<Placement&>().disable()));
    static_assert(noexcept(std::declval<Placement&>().context(0,false)));
    setup();auto reader=read_memory;const auto carry=r::native_carry_snapshot(reader,base_address,player_address,body_address);assert(carry);
    const auto original=memory;Placement placement;assert(placement.install(base_address,reader));placement.context(9,true);
    const auto proof=Placement::qualify(base_address,reader,*carry,1,9,1000,1000);assert(proof);
    const auto serial=placement.publish(*proof,1000);assert(serial);
    assert(!placement.current_driven_right(serial,9,*carry,1000));
    assert(placement.current(serial,1,entity,rf,9,1000));
    assert(!placement.completed(serial,0,entity,rf,9,1000));assert(!placement.completed(serial,1,entity+8,rf,9,1000));
    assert(placement.completed(serial,1,entity,rf,9,1000) && placement.current_driven_right(serial,9,*carry,1000));
    assert(!placement.current_driven_right(serial+1,9,*carry,1000) && !placement.current_driven_right(serial,10,*carry,1000));
    assert(!placement.current_driven_right(serial,9,*carry,999) && !placement.current_driven_right(serial,9,*carry,1151));
    unsigned mutation{};
    for(const auto& mutate:std::array<std::function<void()>,13>{
        []{memory.put(carry_states[0]+0x68,spawn+1);},[]{memory.put(entity+0xa8,spawn+1);},
        []{memory.put(game_address+0x3130b0+(spawn>>16)*8ull,entity+8);},[]{memory.put(entity+0x358,rf+8);},
        []{memory.put(rf+0x10,entity+8);},[]{memory.put(rf+0x18,r::Identity{43,10});},
        []{memory.put(rf,base_address+0x1c573f0);},[]{memory.put(fixture_components,rf+8);},
        []{memory.put(carry_states[2]+0x20,static_cast<unsigned char>(1));},[]{memory.put(carry_states[1]+0x20,static_cast<unsigned char>(1));},
        []{memory.put(carry_machine+0x18,r::Identity{501,10});},[]{memory.put(carry_states[0]+0x20,static_cast<unsigned char>(0));},
        []{memory.put(rf+0x20,base_address+0x1c574c8);}}){
        mutate();assert(!placement.current_driven_right(serial,9,*carry,1001));
        const auto replacement=Placement::qualify(base_address,reader,*carry,1,9,1000,1001);
        if(mutation==5)assert(replacement && replacement->rf_identity.generation==43);else assert(!replacement);
        ++mutation;memory=original;
    }
    memory.put(fixture_components+8,rf);memory.list(entity+0x268,fixture_components,2,2);assert(!Placement::qualify(base_address,reader,*carry,1,9,1000,1001));memory=original;
    for(const auto mode:std::array<unsigned,1>{1}){
        memory.put(carry_states[0]+0x20,static_cast<unsigned char>(0));memory.put(carry_states[mode]+0x20,static_cast<unsigned char>(1));
        const auto other=r::native_carry_snapshot(reader,base_address,player_address,body_address);assert(other);
        assert(!Placement::qualify(base_address,reader,*other,1,9,1000,1001));assert(!placement.current_driven_right(serial,9,*other,1001));memory=original;
    }
    assert(!Placement::qualify(base_address,reader,*carry,2,9,1000,1001));
    assert(!Placement::qualify(base_address,reader,*carry,1,0,1000,1001));
    assert(!Placement::qualify(base_address,reader,*carry,1,9,1000,1151));
    for(auto* flag:std::array<bool*,3>{&read_fail,&read_throw,&read_seh}){
        *flag=true;assert(!placement.current_driven_right(serial,9,*carry,1001));assert(!Placement::qualify(base_address,reader,*carry,1,9,1000,1001));*flag=false;
    }
    bool changed{};during_read=[&]{if(!changed){changed=true;placement.clear();}};
    assert(!placement.completed(serial,1,entity,rf,9,1001));during_read={};
    const auto next=placement.publish(*proof,1001);assert(next>serial && !placement.completed(serial,1,entity,rf,9,1001));
    placement.context(9,false);assert(!placement.completed(next,1,entity,rf,9,1001));placement.context(9,true);
    const auto fresh=placement.publish(*proof,1001);assert(placement.completed(fresh,1,entity,rf,9,1001));
    placement.context(10,true);assert(!placement.current_driven_right(fresh,9,*carry,1001));placement.context(9,true);
    const auto left=Placement::qualify(base_address,reader,*carry,0,9,1000,1001);assert(left);const auto left_serial=placement.publish(*left,1001);
    assert(placement.completed(left_serial,0,entity,rf,9,1001) && !placement.current_driven_right(left_serial,9,*carry,1001));
    assert(placement.current_driven_side(*carry,9,1001)==0u);
    placement.disable();assert(!placement.completed(left_serial,0,entity,rf,9,1001));
    assert(placement.install(base_address,reader));placement.context(9,true);const auto reinstalled=placement.publish(*proof,1001);assert(reinstalled>left_serial);
    assert(!placement.current_driven_right(left_serial,9,*carry,1001));
    Placement continuity;assert(continuity.install(base_address,reader));continuity.context(9,true);
    const auto continuous=continuity.publish(*proof,1000);assert(continuity.completed(continuous,1,entity,rf,9,1000));
    auto refreshed=*proof;refreshed.pose_tick=1100;
    assert(continuity.publish(refreshed,1100)==continuous && continuity.current_driven_right(continuous,9,*carry,1100));
    assert(!continuity.current_driven_right(continuous,9,*carry,1151));
    assert(continuity.completed(continuous,1,entity,rf,9,1101) && continuity.current_driven_right(continuous,9,*carry,1101));
    refreshed.pose_tick=1110;assert(continuity.publish(refreshed,1110)==continuous && continuity.current_driven_right(continuous,9,*carry,1110));
    assert(continuity.publish(*proof,1110)==0 && continuity.current_driven_right(continuous,9,*carry,1110));
    changed=false;during_read=[&]{if(!changed){changed=true;continuity.disable();assert(continuity.install(base_address,reader));continuity.context(9,true);}};
    assert(continuity.publish(refreshed,1110)==0);during_read={};
    assert(!continuity.current_driven_right(continuous,9,*carry,1110));
    for(const unsigned carry_index:{0u,2u})for(const unsigned side:{0u,1u}) {
        setup();memory.put(carry_states[0]+0x20,static_cast<unsigned char>(0));
        memory.put(carry_states[carry_index]+0x20,static_cast<unsigned char>(1));memory.put(carry_states[carry_index]+0x68,spawn);
        const auto selected=r::native_carry_snapshot(reader,base_address,player_address,body_address);assert(selected);
        const auto selected_proof=Placement::qualify(base_address,reader,*selected,side,9,1000,1000);assert(selected_proof);
        Placement selected_placement;assert(selected_placement.install(base_address,reader));selected_placement.context(9,true);
        const auto selected_serial=selected_placement.publish(*selected_proof,1000);assert(selected_serial);
        assert(!selected_placement.current_driven_side(*selected,9,1000));
        assert(selected_placement.current(selected_serial,side,entity,rf,9,1000));
        assert(selected_placement.completed(selected_serial,side,entity,rf,9,1000));
        assert(selected_placement.current_driven_side(*selected,9,1000)==side);
        assert(selected_placement.current_driven_right(selected_serial,9,*selected,1000)==(carry_index==0 && side==1));
        assert(!selected_placement.current(selected_serial,side,entity,rf,9,1151));
        assert(!Placement::qualify(base_address,reader,*selected,side,9,1000,1151));
        const auto selected_memory=memory;
        for(unsigned invalid=0;invalid<5;++invalid) {
            if(invalid==0)memory.put(carry_states[carry_index]+0x68,spawn+1);
            if(invalid==1)memory.put(rf+0x18,r::Identity{43,10});
            if(invalid==2)memory.put(carry_states[carry_index==0?2:0]+0x20,static_cast<unsigned char>(1));
            if(invalid==3)memory.put(carry_states[1]+0x20,static_cast<unsigned char>(1));
            if(invalid==4)memory.put(fixture_components,rf+8);
            assert(!selected_placement.current(selected_serial,side,entity,rf,9,1001));memory=selected_memory;
        }
        for(auto* flag:std::array<bool*,3>{&read_fail,&read_throw,&read_seh}) {
            *flag=true;assert(!Placement::qualify(base_address,reader,*selected,side,9,1000,1001));
            assert(!selected_placement.current(selected_serial,side,entity,rf,9,1001));*flag=false;
        }
        memory.put(carry_states[carry_index]+0x20,static_cast<unsigned char>(0));
        const unsigned replacement_index=carry_index==0?2u:0u;
        memory.put(carry_states[replacement_index]+0x20,static_cast<unsigned char>(1));memory.put(carry_states[replacement_index]+0x68,spawn);
        const auto transitioned=r::native_carry_snapshot(reader,base_address,player_address,body_address);assert(transitioned);
        assert(!selected_placement.current(selected_serial,side,entity,rf,9,1001));
        const auto transitioned_proof=Placement::qualify(base_address,reader,*transitioned,side,9,1001,1001);assert(transitioned_proof);
        Placement transition_placement;assert(transition_placement.install(base_address,reader));transition_placement.context(9,true);
        memory=selected_memory;const auto before_transition=transition_placement.publish(*selected_proof,1000);assert(before_transition);
        memory.put(carry_states[carry_index]+0x20,static_cast<unsigned char>(0));
        memory.put(carry_states[replacement_index]+0x20,static_cast<unsigned char>(1));memory.put(carry_states[replacement_index]+0x68,spawn);
        const auto after_transition=transition_placement.publish(*transitioned_proof,1001);assert(after_transition>before_transition);
        assert(!transition_placement.completed(before_transition,side,entity,rf,9,1001));
        assert(transition_placement.completed(after_transition,side,entity,rf,9,1001));
    }
    std::cout<<"PASS: exclusive typed49/50 actual-placement receipts,49-right-only free-left authority, ambiguity/transition/owner/fault/stale rejection; no engine execution\n";
}
