#define NOMINMAX
#include "../src/native_held_object.hpp"
#include "native_carry_fixture.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <functional>
#include <stdexcept>
#include <future>
#include <thread>
#include <vector>

namespace d2vr {
struct NativeHeldObjectCheck {
    static void place(std::uintptr_t rf,float* position,float* axis) {
        NativeHeldObject::place(reinterpret_cast<void*>(rf),position,axis,nullptr,nullptr,.01f);
    }
    static bool anchor(const NativeHeldObject::Target& target,std::uintptr_t master) {
        return NativeHeldObject::anchor(target,master);
    }
    static bool changed(const float* o,const float* q,const float* lo,const float* lq) {return NativeHeldObject::rebind_changed(o,q,lo,lq);}
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
        {   // the post-carry bind error is published for the diagnostics: |entity+0x300 - the position the bind was given|
            Fixture bound;bound.place();
            const auto at=get<std::array<float,3>>(entity+0x300);const float e=std::sqrt((at[0]-output_position[0])*(at[0]-output_position[0])+(at[1]-output_position[1])*(at[1]-output_position[1])+(at[2]-output_position[2])*(at[2]-output_position[2]));
            assert(std::abs(Held::bind_entity_err.load()-e)<1e-5f);
            Held::clear();assert(Held::bind_entity_err.load()<0.f);
        }
        // Task 9 C2: the anchor reads back what it last wrote before it writes again; an engine rebind in between is counted.
        const auto base=Held::rebind_overwrites.load();
        Fixture again;assert(d2vr::NativeHeldObjectCheck::anchor(again.target,master) && Held::rebind_overwrites.load()==base);   // first write: nothing to compare
        assert(d2vr::NativeHeldObjectCheck::anchor(again.target,master) && Held::rebind_overwrites.load()==base);               // untouched since: healthy
        {   // an engine recompute with different rounding, or the -q sign, is the same bind: not an overwrite (offset <= 1 mm, rotation <= 0.5 deg)
            auto o=get<std::array<float,3>>(rf+0x3d4);o[0]+=.0005f;put(rf+0x3d4,o);
            assert(d2vr::NativeHeldObjectCheck::anchor(again.target,master) && Held::rebind_overwrites.load()==base);
            auto q=get<std::array<float,4>>(rf+0x3e0);for(float& c:q) c=-c;put(rf+0x3e0,q);
            assert(d2vr::NativeHeldObjectCheck::anchor(again.target,master) && Held::rebind_overwrites.load()==base);
            q=get<std::array<float,4>>(rf+0x3e0);const float k=.0015f;   // ~0.17 deg about z
            const float qn=std::sqrt(q[0]*q[0]+q[1]*q[1]+q[2]*q[2]+q[3]*q[3]);std::array<float,4> nudged{q[0],q[1],q[2]+k,q[3]};
            const float nn=std::sqrt(nudged[0]*nudged[0]+nudged[1]*nudged[1]+nudged[2]*nudged[2]+nudged[3]*nudged[3]);for(float& c:nudged) c*=qn/nn;
            put(rf+0x3e0,nudged);assert(d2vr::NativeHeldObjectCheck::anchor(again.target,master) && Held::rebind_overwrites.load()==base);
        }
        {   // the tolerance rule itself: a real 5 cm / 10 deg rebind is a change; 1 mm / 0.3 deg and -q are not
            const float lo[3]{1,2,3},lq[4]{0,0,0,1};float o[3]{1,2,3},q[4]{0,0,0,1};
            assert(!d2vr::NativeHeldObjectCheck::changed(o,q,lo,lq));
            o[1]=2.0009f;assert(!d2vr::NativeHeldObjectCheck::changed(o,q,lo,lq));o[1]=2.05f;assert(d2vr::NativeHeldObjectCheck::changed(o,q,lo,lq));o[1]=2.f;
            const float a=.3f*3.14159265f/180.f/2.f,b=10.f*3.14159265f/180.f/2.f;
            q[2]=std::sin(a);q[3]=std::cos(a);assert(!d2vr::NativeHeldObjectCheck::changed(o,q,lo,lq));
            q[2]=-std::sin(a);q[3]=-std::cos(a);assert(!d2vr::NativeHeldObjectCheck::changed(o,q,lo,lq));   // -q is the same rotation
            q[2]=std::sin(b);q[3]=std::cos(b);assert(d2vr::NativeHeldObjectCheck::changed(o,q,lo,lq));
        }
        put(rf+0x3d4,std::array<float,3>{9,9,9});                                                                                 // 0x753c10 recomputes the offset
        assert(d2vr::NativeHeldObjectCheck::anchor(again.target,master) && Held::rebind_overwrites.load()==base+1);
        assert(d2vr::NativeHeldObjectCheck::anchor(again.target,master) && Held::rebind_overwrites.load()==base+1);               // our write restored it
        put(rf+0x3e0,std::array<float,4>{0,0,.6f,.8f});
        assert(d2vr::NativeHeldObjectCheck::anchor(again.target,master) && Held::rebind_overwrites.load()==base+2);              // rotation too
        *reinterpret_cast<std::int16_t*>(rf+0x3d0)=5;                                                                             // joint rebound
        assert(d2vr::NativeHeldObjectCheck::anchor(again.target,master) && Held::rebind_overwrites.load()==base+3);
        Held::clear();   // a new hold forgets the last write
        put(rf+0x3d4,std::array<float,3>{8,8,8});assert(d2vr::NativeHeldObjectCheck::anchor(again.target,master) && Held::rebind_overwrites.load()==base+3);
    }
    {   // Task 9: clear(reason) counts per reason (out of range = 0) and still asks for the layer restore; final review P3: only a clear that tears down
        // a publication is counted (the render thread clears every no-hold frame)
        auto& n=Held::clear_reasons;
        Held::clear();   // nothing published
        const auto b0=n[0].load(),b2=n[2].load(),b4=n[4].load();
        Held::layer_restore_.store(false);Held::clear(2);Held::clear(3);Held::clear(99);
        assert(n[2].load()==b2 && n[0].load()==b0 && Held::layer_restore_.load());   // a no-op clear counts nothing, and still re-asks the restore
        {   Fixture state;assert(Held::publish(state.target));
            Held::clear(2);assert(n[2].load()==b2+1);
            Held::clear(2);assert(n[2].load()==b2+1);}   // the publication is gone: the second clear is a no-op
        {   Fixture state;assert(Held::publish(state.target));Held::clear(99);assert(n[0].load()==b0+1);}   // out of range = 0
        {   Fixture state;assert(Held::publish(state.target));Held::clear();assert(n[0].load()==b0+2);}
        {   Fixture state;assert(Held::publish(state.target));Held::clear(4);assert(n[4].load()==b4+1);}
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
        assert(d2vr::NativeHeldObjectCheck::snapshot(captured,revision));while(GetTickCount64()-captured.tick<=150) Sleep(1);
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
    {   // locomotion between the publication and the bind tick: the target rides the master (the player) by exactly that motion
        Fixture state;auto t=state.target;const std::array<float,3> then{1,1,1};const std::array<float,9> identity{1,0,0,0,1,0,0,0,1};
        std::memcpy(t.master_pos,then.data(),12);std::memcpy(t.master_axis,identity.data(),36);t.master_valid=true;
        Held::publish(t);put(master+0x300,std::array<float,3>{1.5f,1,1});state.place();
        assert(output_position[0]==1.5f && output_position[1]==2.f && output_position[2]==3.f && state.driven_right());
        assert((get<std::array<float,3>>(rf+0x3d4)==std::array<float,3>{0,1,2})); // the local bind is the published one
        const std::array<float,9> yaw{0,1,0,-1,0,0,0,0,1};put(master+0x30c,yaw);put(master+0x300,then);Held::publish(t);state.place();
        assert(std::abs(output_position[0])<1e-6f && std::abs(output_position[1]-1.f)<1e-6f && std::abs(output_position[2]-3.f)<1e-6f);
        assert(output_axis[0]==0.f && output_axis[1]==1.f && output_axis[3]==-1.f);
        std::printf("root carry: master moved 0.5 m -> target moved 0.5 m, local bind unchanged; master yawed 90 deg -> target turned with it\n");
    }
    {   // a RIGID_BODY-layer throwable (the tank) takes the DEBRIS layer for the hold and gets its own back at the end; a DEBRIS one (bottle) is left alone
        Fixture state;auto reader=read_memory;
        constexpr std::uintptr_t component=0x240000000,world=0x241000000,pool=0x242000000;constexpr std::uint32_t handle=0x1000002;
        put(entity+0x268,r::List{held_components,2,2u<<8});memory.put(held_components+8,component);
        memory.put(component,base_address+0x1b64b68);memory.put(component+0x10,entity);memory.put(component+0xf8,handle);
        memory.put(game_address+0x60410,world);memory.put(world+0x28,pool);memory.put(world+0x30,std::uint32_t{3});
        memory.put(pool+2*0xb0+0x6c,std::uint32_t{(0x20u<<5)|3u});
        memory.put(pool+2*0xb0+0x70,handle);memory.put(pool+2*0xb0+0x74,handle);memory.put(component+0x18,r::Identity{42,10});
        static std::vector<std::array<std::uint32_t,2>> written;written.clear();
        {   // the body is recreated for the same entity (new handle, same component): the cache re-keys instead of reading a stale record
            Held::LayerCache c;assert(Held::held_layer(base_address,reader,entity,c)==3u && c.handle==handle);
            memory.put(world+0x30,std::uint32_t{4});memory.put(component+0xf8,std::uint32_t{0x1000003});memory.put(pool+3*0xb0+0x6c,std::uint32_t{(0x20u<<5)|4u});
            assert(Held::held_layer(base_address,reader,entity,c)==4u && c.handle==0x1000003);
            memory.put(component+0xf8,handle);memory.put(world+0x30,std::uint32_t{3});
            memory.put(component+0x10,std::uintptr_t{0});   // the component no longer belongs to this entity: rescan finds nothing
            assert(!Held::held_layer(base_address,reader,entity,c));
            memory.put(component+0x10,entity);
        }
        Held::filter_seam_=[](std::uintptr_t game,std::uint32_t h,std::uint32_t f) {assert(game==game_address);written.push_back({h,f});return true;};
        assert(Held::held_layer(base_address,reader,entity)==3u && written.empty()); // Task 9: read only, filter word & 0x1f
        Held::LayerCache cache;assert(Held::held_layer(base_address,reader,entity,cache)==3u && cache.entity==entity && cache.handle==handle && cache.component==component);
        memory.put(pool+2*0xb0+0x6c,std::uint32_t{(0x20u<<5)|4u});   // the cached handle is re-read without the component scan
        memory.put(entity+0x268,r::List{held_components,0,0});
        assert(Held::held_layer(base_address,reader,entity,cache)==4u);
        memory.put(entity+0x268,r::List{held_components,2,2u<<8});memory.put(pool+2*0xb0+0x6c,std::uint32_t{(0x20u<<5)|3u});
        assert(!Held::held_layer(base_address,reader,0,cache) && Held::held_layer(base_address,reader,entity,cache)==3u);
        Held::LayerCache other{entity+8,component,0x1000009};assert(Held::held_layer(base_address,reader,entity,other)==3u && other.handle==handle); // another entity rescans
        assert(Held::debris_layer(base_address,reader,entity) && Held::debris_layer(base_address,reader,entity));
        assert(written.size()==1 && written[0][0]==handle && written[0][1]==((0x20u<<5)|4u));
        assert(Held::restore_layer(base_address,reader) && written.size()==2 && written[1][1]==((0x20u<<5)|3u) && !Held::restore_layer(base_address,reader));
        memory.put(pool+2*0xb0+0x6c,std::uint32_t{(0x20u<<5)|4u});
        assert(!Held::debris_layer(base_address,reader,entity) && written.size()==2);
        assert(Held::held_layer(base_address,reader,entity)==4u && written.size()==2 && !Held::held_layer(base_address,reader,0));
        {   // the body was recreated (new identity) after the swap: the restore is given up and never written
            memory.put(pool+2*0xb0+0x6c,std::uint32_t{(0x20u<<5)|3u});
            assert(Held::debris_layer(base_address,reader,entity) && written.size()==3);
            memory.put(component+0x18,r::Identity{43,10});
            const auto gave_up=Held::layers_abandoned.load();
            assert(!Held::restore_layer(base_address,reader) && written.size()==3 && Held::layers_abandoned.load()==gave_up+1 && Held::ledger_.pending()==0);
            memory.put(component+0x18,r::Identity{42,10});
        }
        std::printf("held layer: tank filter 0x%x -> 0x%x for the hold, restored; bottle (DEBRIS) untouched\n",written[1][1],written[0][1]);
        Held::filter_seam_=nullptr;
    }
    {   // Task 10: a transient clear() restores RIGID_BODY; the service re-applies DEBRIS while the hold goes on, and a real hold end leaves it restored
        Fixture state;auto reader=read_memory;
        constexpr std::uintptr_t component=0x240000000,world=0x241000000,pool=0x242000000;constexpr std::uint32_t handle=0x1000002;
        put(entity+0x268,r::List{held_components,2,2u<<8});memory.put(held_components+8,component);
        memory.put(component,base_address+0x1b64b68);memory.put(component+0x10,entity);memory.put(component+0xf8,handle);
        memory.put(game_address+0x60410,world);memory.put(world+0x28,pool);memory.put(world+0x30,std::uint32_t{3});
        static std::vector<std::array<std::uint32_t,2>> written;written.clear();
        static std::uintptr_t filter_at=0;filter_at=pool+2*0xb0+0x6c;
        memory.put(filter_at,std::uint32_t{(0x20u<<5)|3u});
        memory.put(pool+2*0xb0+0x70,handle);memory.put(pool+2*0xb0+0x74,handle);memory.put(component+0x18,r::Identity{42,10});
        Held::filter_seam_=[](std::uintptr_t,std::uint32_t h,std::uint32_t f) {written.push_back({h,f});memory.put(filter_at,f);return true;}; // the engine keeps what it is given
        Held::clear();Held::layer_restore_.store(false);
        const auto swapped=Held::layers_swapped.load();
        {   // transient_clear_reapplies_debris
            assert(!Held::layer_settled(entity));
            Held::service_hold_layers(base_address,reader,entity);
            assert(written.size()==1 && (written[0][1]&0x1f)==4u && Held::layer_settled(entity) && !Held::layer_settled(entity+8));
            Held::service_hold_layers(base_address,reader,entity);assert(written.size()==1);   // settled: no further write
            Held::clear();assert(!Held::layer_settled(entity));
            Held::service_hold_layers(base_address,reader,entity);
            assert(written.size()==3 && (written[1][1]&0x1f)==3u && (written[2][1]&0x1f)==4u && Held::layer_settled(entity));
            assert(Held::layers_swapped.load()==swapped+2 && (r::value<std::uint32_t>(reader,filter_at).value()&0x1f)==4u);
        }
        {   // hold_end_restores
            Held::clear();Held::service_hold_layers(base_address,reader,0);
            assert(written.size()==4 && (written[3][1]&0x1f)==3u && !Held::layer_settled(entity));
            Held::service_hold_layers(base_address,reader,0);assert(written.size()==4);
        }
        {   // exclusion_body: the held item's own body (record, handle, filter word) for the held sweeps' source-body exclusion
            memory.put(filter_at,std::uint32_t{(0x20u<<5)|4u});
            const auto body=Held::exclusion_body(base_address,reader,entity);
            assert(body && body->record==pool+2*0xb0 && body->handle==handle && body->world==world && (body->filter&0x1f)==4u);
            assert(!Held::exclusion_body(base_address,reader,entity+0x1000));   // an entity without that component maps nothing
            memory.put(filter_at,std::uint32_t{(0x20u<<5)|3u});
        }
        {   // bottle_settled_without_write
            memory.put(filter_at,std::uint32_t{(0x20u<<5)|4u});written.clear();
            assert(!Held::layer_settled(entity));Held::service_hold_layers(base_address,reader,entity);
            assert(written.empty() && Held::layer_settled(entity));
            Held::clear();assert(!Held::layer_settled(entity));
        }
        {   // safe_release_defers_layer_restore: the hold ends, the layer stays DEBRIS until allow_restore(), then the ledger restores it once
            memory.put(filter_at,std::uint32_t{(0x20u<<5)|3u});written.clear();Held::clear();Held::service_hold_layers(base_address,reader,0);written.clear();
            Held::service_hold_layers(base_address,reader,entity);
            assert(written.size()==1 && (written[0][1]&0x1f)==4u);
            Held::begin_safe_release();Held::clear();
            Held::service_hold_layers(base_address,reader,0);Held::service_hold_layers(base_address,reader,0);
            assert(written.size()==1 && (r::value<std::uint32_t>(reader,filter_at).value()&0x1f)==4u);   // nothing restored while the release is pending
            {   // the engine's own drop writes RIGID_BODY back: the pending release puts DEBRIS on again (once; a stable DEBRIS is not written twice)
                const auto before=Held::layers_reasserted.load();
                memory.put(filter_at,std::uint32_t{(0x20u<<5)|3u});
                Held::service_hold_layers(base_address,reader,0);
                assert(written.size()==2 && (written[1][1]&0x1f)==4u && Held::layers_reasserted.load()==before+1);
                Held::service_hold_layers(base_address,reader,0);assert(written.size()==2);
            }
            Held::allow_restore();assert(!Held::restore_hold_.load());
            Held::service_hold_layers(base_address,reader,0);
            assert(written.size()==3 && (written[2][1]&0x1f)==3u && (r::value<std::uint32_t>(reader,filter_at).value()&0x1f)==3u);
            Held::allow_restore();Held::service_hold_layers(base_address,reader,0);assert(written.size()==3);   // exactly once
        }
        {   // a hold that starts again cancels the pending release: the layer is still DEBRIS and the next real end restores it
            written.clear();Held::service_hold_layers(base_address,reader,entity);assert(written.size()==1 && (written[0][1]&0x1f)==4u);
            Held::begin_safe_release();Held::clear();Held::cancel_safe_release();
            Held::service_hold_layers(base_address,reader,entity);assert(written.size()==1);
            Held::clear();Held::service_hold_layers(base_address,reader,0);assert(written.size()==2 && (written[1][1]&0x1f)==3u);
        }
        {   // a render thread that never answers is overtaken after restore_hold_max_ms (the layer is never stranded)
            written.clear();Held::service_hold_layers(base_address,reader,entity);assert(written.size()==1);
            Held::begin_safe_release();Held::clear();
            Held::service_hold_layers(base_address,reader,0);assert(written.size()==1);
            Held::restore_hold_since_.store(1);   // long ago
            Held::service_hold_layers(base_address,reader,0);Held::service_hold_layers(base_address,reader,0);
            assert(written.size()==2 && (written[1][1]&0x1f)==3u && !Held::restore_hold_.load());
        }
        {   // an epoch change (recenter, XR suspend/resume) during the release window must not strand the object on DEBRIS: the ledger restores it, once
            written.clear();memory.put(filter_at,std::uint32_t{(0x20u<<5)|3u});Held::clear();Held::service_hold_layers(base_address,reader,0);written.clear();
            Held::service_hold_layers(base_address,reader,entity);assert(written.size()==1 && (r::value<std::uint32_t>(reader,filter_at).value()&0x1f)==4u);
            Held::begin_safe_release();Held::clear();
            Held::service_hold_layers(base_address,reader,0);assert(written.size()==1 && Held::restore_hold_.load());   // waiting for the release check
            const auto restored=Held::layers_restored.load(),abandoned=Held::layers_abandoned.load();
            Held::note_epoch(Held::epoch_seen_.load()+1);
            Held::service_hold_layers(base_address,reader,0);
            assert(written.size()==2 && (written[1][1]&0x1f)==3u && (r::value<std::uint32_t>(reader,filter_at).value()&0x1f)==3u);   // back on RIGID_BODY
            assert(Held::layers_restored.load()==restored+1 && Held::layers_abandoned.load()==abandoned && !Held::restore_hold_.load());
            Held::allow_restore();Held::service_hold_layers(base_address,reader,0);assert(written.size()==2);   // exactly once
            // the same during a live hold: the held object is restored and takes DEBRIS again on the next service
            Held::service_hold_layers(base_address,reader,entity);assert(written.size()==3 && (written[2][1]&0x1f)==4u);
            Held::note_epoch(Held::epoch_seen_.load()+1);Held::service_hold_layers(base_address,reader,entity);
            assert(written.size()==5 && (written[3][1]&0x1f)==3u && (written[4][1]&0x1f)==4u);
            Held::clear();Held::service_hold_layers(base_address,reader,0);assert(written.size()==6 && (written[5][1]&0x1f)==3u);
            // a level load changes the entity: the entry is given up without a write
            Held::service_hold_layers(base_address,reader,entity);const auto writes=written.size();
            Held::begin_safe_release();Held::clear();
            put(entity+0xa8,std::uint32_t{spawn+0x10000});   // another entity now owns the slot
            Held::note_epoch(Held::epoch_seen_.load()+1);Held::service_hold_layers(base_address,reader,0);
            assert(written.size()==writes && Held::layers_abandoned.load()>abandoned);
            put(entity+0xa8,spawn);
        }
        std::printf("held layer service: tank re-takes DEBRIS after a transient clear, RIGID_BODY after the hold; bottle settled without a write\n");
        Held::filter_seam_=nullptr;
    }
    {   // nan_target_rejected / scaled_axis_rejected / reflection_rejected: invalid targets never replace the published one
        Fixture state;const auto before=Held::rejected_targets.load();
        auto bad=state.target;bad.pos[0]=7.f;bad.axis[4]=NAN;Held::publish(bad);
        Held::Target kept{};assert(Held::diagnostic_target(kept) && kept.pos[0]==1.f && Held::rejected_targets.load()==before+1);
        bad=state.target;bad.pos[1]=INFINITY;Held::publish(bad);assert(Held::rejected_targets.load()==before+2);
        bad=state.target;bad.pos[2]=1e5f;Held::publish(bad);assert(Held::rejected_targets.load()==before+3);
        bad=state.target;for(auto& v:bad.axis)v*=2.f;Held::publish(bad);assert(Held::rejected_targets.load()==before+4);
        bad=state.target;bad.axis[1]=.1f;Held::publish(bad);assert(Held::rejected_targets.load()==before+5); // sheared
        bad=state.target;bad.axis[8]=-1.f;Held::publish(bad);assert(Held::rejected_targets.load()==before+6); // reflection, det -1
        assert(Held::diagnostic_target(kept) && kept.pos[0]==1.f && kept.axis[8]==1.f);
        state.place();assert(output_position[0]==1.f && state.driven_right());
        assert(Held::target_valid(state.target) && Held::rejected_targets.load()==before+6);
        {   // Task 9: publish reports acceptance, so the diagnostics only count frames something was bound for
            auto refused=state.target;refused.axis[8]=-1.f;assert(!Held::publish(refused) && Held::rejected_targets.load()==before+7);
            assert(Held::publish(state.target) && Held::rejected_targets.load()==before+7);
        }
    }
    {   // anchor_rejects_bad_master: bypasses publish. quat_from_rot always normalises (identity on a NaN norm), so the reachable
        // refusal is the |off|<4 bound on the offset computed from a NaN / huge master pose; the rf+0x3d0..0x3f0 bytes stay put.
        // (anchor's finite/|q|~1 guard on the quaternion is defense-in-depth and has no reachable test.)
        Fixture state;
        unsigned char before[0x20];std::memcpy(before,reinterpret_cast<void*>(rf+0x3d0),sizeof(before));
        const auto refused=[&] {
            assert(!d2vr::NativeHeldObjectCheck::anchor(state.target,master));
            assert(std::memcmp(before,reinterpret_cast<void*>(rf+0x3d0),sizeof(before))==0);
        };
        put(master+0x300,std::array<float,3>{NAN,1,1});refused();
        put(master+0x300,std::array<float,3>{1e6f,1,1});refused();
        put(master+0x300,std::array<float,3>{1,1,1});put(master+0x30c,std::array<float,9>{NAN,0,0,0,1,0,0,0,1});refused();
        put(master+0x30c,std::array<float,9>{1,1e6f,0,0,1,0,0,0,1});refused();
    }
    {   // a master carry that leaves the target invalid is refused at the bind and writes nothing
        Fixture state;auto t=state.target;const std::array<float,3> then{1,1,1};const std::array<float,9> identity{1,0,0,0,1,0,0,0,1};
        std::memcpy(t.master_pos,then.data(),12);std::memcpy(t.master_axis,identity.data(),36);t.master_valid=true;
        Held::publish(t);put(master+0x30c,std::array<float,9>{2,0,0,0,2,0,0,0,2});
        const auto before=Held::rejected_targets.load();state.place();state.unchanged();
        assert(!state.driven_right() && Held::rejected_targets.load()==before+1);
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
