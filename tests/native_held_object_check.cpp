#define NOMINMAX
#include "../src/native_held_object.hpp"
#include "native_carry_fixture.hpp"
#include <cassert>
#include <cstdio>
#include <functional>
#include <stdexcept>
#include <future>
#include <thread>

namespace d2vr {
struct NativeHeldObjectCheck {
    static void place(std::uintptr_t rf,float* position,float* axis) {
        NativeHeldObject::place(reinterpret_cast<void*>(rf),position,axis,nullptr,nullptr,.01f);
    }
    static bool anchor(const NativeHeldObject::Target& target,std::uintptr_t master) {
        return NativeHeldObject::anchor(target,master);
    }
    static bool snapshot(NativeHeldObject::Target& target,std::uint64_t& revision) {
        return NativeHeldObject::snapshot(target,&revision);
    }
    static unsigned commit(const NativeHeldObject::Target& target,std::uint64_t revision,float* position,float* axis) {
        return NativeHeldObject::commit(target,revision,position,axis,nullptr,nullptr,.01f);
    }
    static bool fault_commit(const NativeHeldObject::Target& target,std::uint64_t revision,float* position,float* axis) {
        __try {(void)commit(target,revision,position,axis);return false;}
        __except(EXCEPTION_EXECUTE_HANDLER) {return true;}
    }
    static void near_exhaustion() {
        AcquireSRWLockExclusive(&NativeHeldObject::publication_lock_);
        NativeHeldObject::publication_revision_=(std::numeric_limits<std::uint64_t>::max)()-1;
        ReleaseSRWLockExclusive(&NativeHeldObject::publication_lock_);
    }
};
}

namespace {
using Held=d2vr::NativeHeldObject;
using Placement=d2vr::NativeHeldPlacement;
alignas(16) std::array<unsigned char,0x500> entity_storage{},rf_storage{},master_storage{};
Memory memory;
constexpr std::uint32_t spawn=0x20002;
constexpr std::uintptr_t held_components=0x232000000;
constexpr std::uint64_t epoch=9;
std::uintptr_t entity{},rf{},master{};
bool fail_read{},throw_read{},seh_read{},recycle_after_output{},recycled{};
std::function<void()> refresh_before_refusal;bool refresh_after_output{};unsigned refresh_fault{};
std::function<void()> during_owned_read;
float output_position[3]{},output_axis[9]{};

template<class Value> void put(std::uintptr_t at,const Value& value) {
    std::memcpy(reinterpret_cast<void*>(at),&value,sizeof(value));
}
template<class Value> Value get(std::uintptr_t at) {
    Value value{};std::memcpy(&value,reinterpret_cast<const void*>(at),sizeof(value));return value;
}
template<std::size_t Size> bool read_storage(const std::array<unsigned char,Size>& storage,
        std::uintptr_t at,void* output,std::size_t size) {
    const auto start=reinterpret_cast<std::uintptr_t>(storage.data());
    if(at<start || size>Size || at-start>Size-size)return false;
    std::memcpy(output,reinterpret_cast<const void*>(at),size);return true;
}
bool read_memory(std::uintptr_t at,void* output,std::size_t size) {
    if(during_owned_read) {auto action=std::move(during_owned_read);action();}
    if(fail_read)return false;
    if(throw_read)throw std::runtime_error("fixture read");
    if(seh_read)RaiseException(EXCEPTION_ACCESS_VIOLATION,0,0,nullptr);
    if(refresh_before_refusal && (!refresh_after_output || output_position[0]==1.f)) {
        auto refresh=std::move(refresh_before_refusal);refresh();
        if(refresh_fault==1)throw std::runtime_error("old callback read");
        if(refresh_fault==2)RaiseException(EXCEPTION_ACCESS_VIOLATION,0,0,nullptr);
        return false;
    }
    if(recycle_after_output && !recycled && output_position[0]==1.f) {
        recycled=true;put(rf+0x18,r::Identity{43,10});
    }
    return read_storage(entity_storage,at,output,size) || read_storage(rf_storage,at,output,size) ||
        read_storage(master_storage,at,output,size) || memory(at,output,size);
}
struct Fixture {
    r::NativeCarrySnapshot carry;
    Held::Target target;
    Fixture(unsigned side=1,bool receipt=true,unsigned carry_index=0) {
        Held::clear();Placement::process_owner().disable();
        fail_read=throw_read=seh_read=recycle_after_output=recycled=false;
        refresh_before_refusal={};refresh_after_output=false;refresh_fault=0;
        during_owned_read={};
        entity_storage.fill(0);rf_storage.fill(0);master_storage.fill(0);
        entity=reinterpret_cast<std::uintptr_t>(entity_storage.data());
        rf=reinterpret_cast<std::uintptr_t>(rf_storage.data());master=reinterpret_cast<std::uintptr_t>(master_storage.data());
        memory=fixture();add_carry_fixture(memory);
        memory.put(carry_states[carry_index]+0x20,static_cast<unsigned char>(1));memory.put(carry_states[carry_index]+0x68,spawn);
        memory.put(game_address+0x3130b0+(spawn>>16)*8ull,entity);memory.put(held_components,rf);
        put(entity+0xa8,spawn);put(entity+0x358,rf);put(entity+0x268,r::List{held_components,1,2u<<8});
        put(entity+0x300,std::array<float,3>{.1f,.2f,.3f});
        const std::array<float,9> identity{1,0,0,0,1,0,0,0,1};put(entity+0x30c,identity);
        put(master+0x300,std::array<float,3>{1,1,1});put(master+0x30c,identity);
        put(rf,base_address+0x1c573e8);put(rf+0x20,base_address+0x1c574c0);
        put(rf+0x18,r::Identity{42,10});put(rf+0x10,entity);put(rf+0x3c8,master);
        put(rf+0x3d0,std::int16_t{7});put(rf+0x3d4,std::array<float,3>{8,9,10});
        put(rf+0x3e0,std::array<float,4>{.1f,.2f,.3f,.4f});
        auto reader=read_memory;const auto captured=r::native_carry_snapshot(reader,base_address,player_address,body_address);
        assert(captured);carry=*captured;
        auto& placement=Placement::process_owner();assert(placement.install(base_address,reader));placement.context(epoch,true);
        target.rf=rf;target.entity=entity;target.pos[0]=1;target.pos[1]=2;target.pos[2]=3;
        std::memcpy(target.axis,identity.data(),sizeof(target.axis));target.tick=GetTickCount64();target.pose_epoch=epoch;target.source_side=side;
        if(receipt) {
            const auto proof=Placement::qualify(base_address,reader,carry,side,epoch,target.tick,target.tick);assert(proof);
            target.placement_serial=placement.publish(*proof,target.tick);assert(target.placement_serial);
        }
        std::fill(std::begin(output_position),std::end(output_position),-10.f);
        std::fill(std::begin(output_axis),std::end(output_axis),-20.f);
        Held::publish(target);
    }
    bool driven_right()const {
        return Placement::process_owner().current_driven_right(target.placement_serial,epoch,carry,GetTickCount64());
    }
    void place(){d2vr::NativeHeldObjectCheck::place(rf,output_position,output_axis);}
    void unchanged()const {
        for(const auto value:output_position)assert(value==-10.f);
        for(const auto value:output_axis)assert(value==-20.f);
        assert(get<std::int16_t>(rf+0x3d0)==7);
        assert((get<std::array<float,3>>(rf+0x3d4)==std::array<float,3>{8,9,10}));
        assert((get<std::array<float,4>>(rf+0x3e0)==std::array<float,4>{.1f,.2f,.3f,.4f}));
    }
};
}

int main() {
    {
        Fixture state;assert(!state.driven_right());
        assert(!Placement::process_owner().current_driven_side(state.carry,epoch,GetTickCount64()));
        state.place();assert(state.driven_right());
        for(unsigned index=0;index<3;++index)assert(output_position[index]==state.target.pos[index]);
        for(unsigned index=0;index<9;++index)assert(output_axis[index]==state.target.axis[index]);
        assert(get<std::int16_t>(rf+0x3d0)==-1);
        assert((get<std::array<float,3>>(rf+0x3d4)==std::array<float,3>{0,1,2}));
        assert((get<std::array<float,4>>(rf+0x3e0)==std::array<float,4>{0,0,0,1}));
        assert((get<std::array<float,3>>(entity+0x300)==std::array<float,3>{.1f,.2f,.3f}));
    }
    {
        Fixture state;assert(d2vr::NativeHeldObjectCheck::anchor(state.target,master));assert(!state.driven_right());
    }
    for(unsigned mode=0;mode<10;++mode) {
        Fixture state;
        if(mode==0)++state.target.placement_serial;
        if(mode==1)state.target.pose_epoch=epoch+1;
        if(mode==2)state.target.source_side=0;
        if(mode==3)put(rf+0x10,entity+8);
        if(mode==4)put(rf+0x18,r::Identity{43,10});
        if(mode==5)fail_read=true;
        if(mode==6)throw_read=true;
        if(mode==7)seh_read=true;
        if(mode==8)Placement::process_owner().clear();
        if(mode==9)state.target.tick=GetTickCount64()-151;
        Held::publish(state.target);state.place();state.unchanged();assert(!state.driven_right());
    }
    {
        Fixture state;Held::clear();state.place();state.unchanged();assert(!state.driven_right());
    }
    {
        Fixture state;put(rf+0x3c8,std::uintptr_t{});state.place();
        assert(output_position[0]==1 && get<std::int16_t>(rf+0x3d0)==7 && !state.driven_right());
    }
    {
        Fixture state;put(master+0x300,std::array<float,3>{10,0,0});state.place();
        assert(output_position[0]==1 && get<std::int16_t>(rf+0x3d0)==7 && !state.driven_right());
    }
    {
        Fixture state;put(rf+0x3c8,std::uintptr_t{1});state.place();
        assert(output_position[0]==1 && get<std::int16_t>(rf+0x3d0)==7 && !state.driven_right());
    }
    {
        Fixture state;recycle_after_output=true;state.place();assert(recycled && !state.driven_right());
    }
    for(const unsigned side:{0u,1u}) {
        Fixture state(side,false);state.place();state.unchanged();
        assert(!state.driven_right() && !Placement::process_owner().current_driven_side(state.carry,epoch,GetTickCount64()));
    }
    {
        Fixture state(1,false);memory.put(carry_states[0]+0x20,static_cast<unsigned char>(0));
        memory.put(carry_states[2]+0x20,static_cast<unsigned char>(1));auto reader=read_memory;
        const auto two_handed=r::native_carry_snapshot(reader,base_address,player_address,body_address);assert(two_handed);
        state.carry=*two_handed;state.place();state.unchanged();
        assert(!state.driven_right() && !Placement::process_owner().current_driven_side(state.carry,epoch,GetTickCount64()));
    }
    {
        Fixture state(0);state.place();assert(!state.driven_right());
        assert(Placement::process_owner().current_driven_side(state.carry,epoch,GetTickCount64())==0u);
    }
    for(const unsigned carry_index:{0u,2u})for(const unsigned side:{0u,1u}) {
        Fixture state(side,true,carry_index);state.place();
        for(unsigned index=0;index<3;++index)assert(output_position[index]==state.target.pos[index]);
        assert(get<std::int16_t>(rf+0x3d0)==-1);
        assert(Placement::process_owner().current_driven_side(state.carry,epoch,GetTickCount64())==side);
        assert(state.driven_right()==(carry_index==0 && side==1));
    }
    for(const unsigned side:{0u,1u})for(unsigned refusal=0;refusal<7;++refusal) {
        Fixture state(side,true,2);
        if(refusal==0)state.target.placement_serial=0;
        if(refusal==1)memory.put(carry_states[0]+0x20,static_cast<unsigned char>(1));
        if(refusal==2)memory.put(carry_states[2]+0x68,spawn+1);
        if(refusal==3)fail_read=true;
        if(refusal==4)throw_read=true;
        if(refusal==5)seh_read=true;
        if(refusal==6)state.target.tick=GetTickCount64()-151;
        Held::publish(state.target);state.place();state.unchanged();assert(!state.driven_right());
    }
    for(const bool after_output:{false,true})for(unsigned fault=0;fault<3;++fault) {
        Fixture state;Held::Target replacement=state.target;refresh_after_output=after_output;refresh_fault=fault;
        refresh_before_refusal=[&] {
            const auto now=GetTickCount64();auto reader=read_memory;
            const auto proof=Placement::qualify(base_address,reader,state.carry,1,epoch,now,now);assert(proof);
            replacement.placement_serial=Placement::process_owner().publish(*proof,now);
            assert(replacement.placement_serial==state.target.placement_serial);
            replacement.tick=now;replacement.pos[0]=2.f;
            Held::publish(replacement);
        };
        state.place();assert(!refresh_before_refusal);
        Held::Target retained{};assert(Held::diagnostic_target(retained));
        assert(retained.pos[0]==2.f && retained.placement_serial==replacement.placement_serial);
        assert(Placement::process_owner().current(replacement.placement_serial,1,entity,rf,epoch,GetTickCount64()));
        assert(!state.driven_right());
        if(!after_output)state.unchanged();
        state.place();assert(output_position[0]==2.f);
        assert(state.driven_right());
        assert(!Placement::process_owner().current_driven_side(state.carry,epoch,GetTickCount64()+151));
    }
    {
        Fixture state;auto next=state.target;next.pos[0]=2.f;
        during_owned_read=[&] {Held::publish(next);state.place();assert(output_position[0]==2.f);};
        state.place();assert(!during_owned_read && output_position[0]==2.f);
        assert((get<std::array<float,3>>(rf+0x3d4)[0]==1.f && state.driven_right()));
    }
    {
        Fixture state;auto next=state.target;next.pos[0]=2.f;
        std::promise<void> captured,resume;auto ready=captured.get_future();auto proceed=resume.get_future();
        during_owned_read=[&] {captured.set_value();proceed.wait();};
        std::thread old([&] {state.place();});
        assert(ready.wait_for(std::chrono::seconds{5})==std::future_status::ready);
        Held::publish(next);state.place();assert(output_position[0]==2.f);
        resume.set_value();old.join();assert(output_position[0]==2.f && state.driven_right());
    }
    {
        Fixture state;Held::Target captured{};std::uint64_t revision{};
        assert(d2vr::NativeHeldObjectCheck::snapshot(captured,revision));Held::publish(state.target);
        assert(!d2vr::NativeHeldObjectCheck::commit(captured,revision,output_position,output_axis));state.unchanged();
        assert(d2vr::NativeHeldObjectCheck::snapshot(captured,revision));Held::clear();Held::publish(state.target);
        assert(!d2vr::NativeHeldObjectCheck::commit(captured,revision,output_position,output_axis));state.unchanged();
    }
    {
        Fixture state;Held::Target captured{};std::uint64_t revision{};
        assert(d2vr::NativeHeldObjectCheck::snapshot(captured,revision));auto next=state.target;++next.pose_epoch;
        Held::publish(next);assert(!d2vr::NativeHeldObjectCheck::commit(captured,revision,output_position,output_axis));state.unchanged();
    }
    {
        Fixture state;Held::Target captured{};std::uint64_t revision{};
        assert(d2vr::NativeHeldObjectCheck::snapshot(captured,revision));Sleep(151);
        assert(!d2vr::NativeHeldObjectCheck::commit(captured,revision,output_position,output_axis));state.unchanged();
    }
    for(unsigned fault=0;fault<2;++fault) {
        Fixture state;Held::Target captured{};std::uint64_t revision{};
        assert(d2vr::NativeHeldObjectCheck::snapshot(captured,revision));
        if(fault==0) {captured.entity=1;put(rf+0x10,std::uintptr_t{1});}
        assert(d2vr::NativeHeldObjectCheck::fault_commit(captured,revision,
            fault==1?reinterpret_cast<float*>(1):output_position,output_axis));
        put(rf+0x10,entity);Held::publish(state.target);state.place();
        assert(output_position[0]==1.f && state.driven_right());
    }
    {
        Fixture state;
        for(unsigned frame=0;frame<20;++frame) {state.target.pos[0]=1.f+frame*.01f;Held::publish(state.target);state.place();assert(state.driven_right());}
    }
    {
        Fixture state;d2vr::NativeHeldObjectCheck::near_exhaustion();Held::publish(state.target);
        Held::Target captured{};std::uint64_t revision{};assert(d2vr::NativeHeldObjectCheck::snapshot(captured,revision));
        assert(revision==(std::numeric_limits<std::uint64_t>::max)());Held::clear();Held::publish(state.target);
        assert(!Held::diagnostic_target(captured));
        assert(!d2vr::NativeHeldObjectCheck::commit(state.target,revision,output_position,output_axis));state.unchanged();
    }
    Held::clear();Placement::process_owner().disable();
    std::puts("PASS actual held publish/place/anchor outputs and typed completion authority; owned buffers only, no engine/hook calls");
}
