#include "../src/native_door_empty_gate.hpp"
#include "native_carry_fixture.hpp"
#include <cassert>
#include <iostream>
#include <stdexcept>
#include <filesystem>
#include <cstring>
#include <limits>

namespace {
void __fastcall grab_original(void*,void*) {}
constexpr std::uintptr_t fake_base=0x60000000;
constexpr std::array<unsigned char,32> dispatch_bytes{0x48,0x83,0xec,0x68,0x48,0xc7,0x44,0x24,0x30,0xfe,0xff,0xff,0xff,0x4c,0x8b,0xd2,0x0f,0xb7,0x42,0x04,0x83,0xf8,0x3e,0x0f,0x87,0xf3,0x05,0x00,0x00,0x48,0x8d,0x15};
constexpr std::array<unsigned char,32> velocity_bytes{0x48,0x89,0x6c,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x89,0x54,0x24,0x10,0x57,0x48,0x83,0xec,0x40,0x44,0x8b,0xd2,0x49,0x8b,0xe9,0x49,0x8b,0xf0,0x41,0x81,0xe2,0xff};
bool install_read(std::uintptr_t address,void* output,std::size_t size) {
    if(address==fake_base+0x89fb0 && size==dispatch_bytes.size()) {std::memcpy(output,dispatch_bytes.data(),size);return true;}
    if(address==fake_base+0xc85dc0 && size==velocity_bytes.size()) {std::memcpy(output,velocity_bytes.data(),size);return true;}
    return false;
}
UINT grab_gameplay() {return 2;}
}

extern "C" MH_STATUS WINAPI MH_CreateHook(LPVOID,LPVOID,LPVOID* original) {
    *original=reinterpret_cast<LPVOID>(&grab_original);return MH_OK;
}
extern "C" MH_STATUS WINAPI MH_EnableHook(LPVOID) {return MH_OK;}
extern "C" MH_STATUS WINAPI MH_DisableHook(LPVOID) {return MH_OK;}

namespace {
constexpr std::uintptr_t camera_address=0x220000000,controller_address=0x221000000,
    physics_address=0x222000000,delegate_address=0x223000000,inventory_address=0x224000000,
    graph_address=0x225000000,empty_address=0x226000000;
Memory empty_fixture() {
    auto memory=fixture();add_carry_fixture(memory);
    memory.put(camera_address,std::array<std::byte,d2vr::camera_read_size>{});
    memory.put(camera_address+0x25150,std::array<std::byte,0x200>{});
    memory.put(camera_address,base_address+d2vr::camera_vtable);memory.put(camera_address+0x20,base_address+d2vr::camera_secondary_vtable);
    for(const auto address:{camera_address,controller_address,physics_address}) {
        memory.put(address+0x10,player_address);memory.put(address+0x18,r::Identity{5,10});
    }
    memory.put(controller_address,base_address+0x1c72918);memory.put(physics_address,base_address+0x1c73268);
    memory.put(physics_address+0x20,base_address+0x1c73340);memory.put(physics_address+0x30,base_address+0x1c73378);
    memory.put(camera_address+0x25158,controller_address);memory.put(camera_address+0x25160,physics_address);
    memory.put(camera_address+0x25170,body_address);memory.put(physics_address+0x48,delegate_address);
    memory.put(delegate_address+0x160,base_address+0x1b02768);memory.put(delegate_address+0x168,physics_address);
    memory.put(delegate_address+0x120,player_address+0x300);memory.put(delegate_address+0x1e0,delegate_address);
    memory.put(delegate_address+0xd0,0.f);memory.put(delegate_address+0xbc,0.f);memory.put(delegate_address+0x40,0.f);
    memory.put(controller_address+0xd0,std::array<float,2>{});memory.put(player_address+0x30c,std::array<float,9>{});
    memory.list(player_address+0x268,components_address,3,3);memory.put(components_address+16,camera_address);
    memory.put(body_address+0x60,inventory_address);memory.put(inventory_address,base_address+0x1b9a878);
    memory.put(inventory_address+0x10,player_address);memory.put(inventory_address+0x18,r::Identity{400,10});
    memory.put(inventory_address+0x68,std::array<std::uintptr_t,2>{});memory.put(graph_address,base_address+0x1c4d448);
    memory.put(empty_address,std::array<char,6>{'E','m','p','t','y','\0'});
    for(unsigned side=0;side<2;++side) {
        const auto names=empty_address+0x100+side*0x100;
        memory.list(graph_address+(side?0xb0:0xa0),names,1,1);memory.put(names,empty_address);
        memory.put(descriptors[side]+0x138,graph_address);
        const auto part=body_address+(side?0x680:0x370),node=graph_address+(side?0x1d0:0x190);
        memory.put(part+0x18,node);memory.put(part+0x21,std::int8_t{0});memory.put(node,base_address+0x1c4d438);
    }
    return memory;
}
using Gate=d2vr::NativeDoorEmptyGate;
using Lanes=std::array<d2vr::NativeGrab::DiagnosticLane,2>;
Lanes make_publications(const d2vr::NativePlayerCamera& owner) {
    Lanes lanes{};
    for(auto& lane:lanes) {
        lane.epoch=7;lane.publication_tick=1000;lane.serial=1;lane.player=player_address;lane.camera=camera_address;
        lane.usable=true;lane.source_tick=1000;lane.source_age_valid=true;lane.publication_owner=owner;
        lane.lifecycle_revision=1;
    }
    return lanes;
}
}
int main() {
    auto memory=empty_fixture();const auto original=memory;
    const auto owner=d2vr::native_player_camera(memory,base_address,camera_address);assert(owner);
    auto publications=[&]{return make_publications(*owner);};
    auto lanes=publications();UINT64 picked=0;
    auto diagnostics=[&](UINT64)->std::optional<Lanes>{return lanes;};auto counter=[&]{return picked;};
    auto capture=[&]{return Gate::capture_with(base_address,memory,*owner,diagnostics,counter,1000);};
    const auto lease=capture();assert(lease && lease->epoch==7);
    assert(lease->admitted_hands==3);
    for(const unsigned mask:{0u,4u,7u,std::numeric_limits<unsigned>::max()}) {
        assert(!Gate::capture_with(base_address,memory,*owner,diagnostics,counter,1000,mask));
        auto invalid=*lease;invalid.admitted_hands=mask;
        assert(!Gate::current_with(base_address,memory,*owner,invalid,diagnostics,counter,1000));
        assert(!Gate::publications_current_with(invalid,diagnostics,counter,1000));
    }
    for(unsigned selected=0;selected<2;++selected) {
        const unsigned mask=1u<<selected,opposite=selected^1u;
        memory=original;lanes=publications();
        const auto armed_slot=inventory_address+(opposite?0x68:0x70);
        const auto armed_names=empty_address+0x100+opposite*0x100;
        const auto sword_name=empty_address+0x900;
        memory.put(armed_slot,std::uintptr_t{0x230000000});
        memory.put(armed_names,sword_name);memory.put(sword_name,std::array<char,6>{'S','w','o','r','d','\0'});
        lanes[opposite].usable=false;lanes[opposite].occupied=true;lanes[opposite].role=d2vr::RightGripOwner::Role::native;
        const auto single=Gate::capture_with(base_address,memory,*owner,diagnostics,counter,1000,mask);
        assert(single && single->admitted_hands==mask && single->hands.empty_candidates==mask);
        assert(!capture());
        assert(!Gate::capture_with(base_address,memory,*owner,diagnostics,counter,1000,1u<<opposite));
        assert(Gate::current_with(base_address,memory,*owner,*single,diagnostics,counter,1001));
        assert(Gate::publications_current_with(*single,diagnostics,counter,1001));
        auto restricted=*single;restricted.admitted_hands=3;
        assert(!Gate::current_with(base_address,memory,*owner,restricted,diagnostics,counter,1001));
        const auto valid_lanes=lanes;
        lanes[selected].held=d2vr::NativeRigidBody{};
        assert(!Gate::capture_with(base_address,memory,*owner,diagnostics,counter,1000,mask));
        lanes=valid_lanes;lanes[selected].deferred=true;
        assert(!Gate::capture_with(base_address,memory,*owner,diagnostics,counter,1000,mask));
        lanes=valid_lanes;lanes[selected].pending_release=true;
        assert(!Gate::capture_with(base_address,memory,*owner,diagnostics,counter,1000,mask));
        lanes=valid_lanes;lanes[selected].occupied=true;
        assert(!Gate::capture_with(base_address,memory,*owner,diagnostics,counter,1000,mask));
        lanes=valid_lanes;lanes[selected].role=d2vr::RightGripOwner::Role::native;
        assert(!Gate::capture_with(base_address,memory,*owner,diagnostics,counter,1000,mask));
        lanes=valid_lanes;lanes[opposite].source_tick=1;
        assert(!Gate::capture_with(base_address,memory,*owner,diagnostics,counter,1000,mask));
        lanes=valid_lanes;lanes[opposite].lifecycle_revision++;
        assert(!Gate::current_with(base_address,memory,*owner,*single,diagnostics,counter,1001));
        lanes=valid_lanes;++picked;
        assert(!Gate::current_with(base_address,memory,*owner,*single,diagnostics,counter,1001));--picked;
        unsigned snapshots=0;
        auto opposite_republish=[&](UINT64)->std::optional<Lanes>{auto result=valid_lanes;result[opposite].serial+=snapshots++;return result;};
        assert(!Gate::capture_with(base_address,memory,*owner,opposite_republish,counter,1000,mask));
        snapshots=0;
        auto lifecycle_change=[&](UINT64)->std::optional<Lanes>{auto result=valid_lanes;if(snapshots++)for(auto& lane:result)++lane.lifecycle_revision;return result;};
        assert(!Gate::capture_with(base_address,memory,*owner,lifecycle_change,counter,1000,mask));
        lanes=valid_lanes;
        memory.put(inventory_address+(selected?0x68:0x70),std::uintptr_t{0x231000000});
        assert(!Gate::capture_with(base_address,memory,*owner,diagnostics,counter,1000,mask));
        for(const auto state:carry_states) {
            memory=original;lanes=publications();memory.put(state+0x20,static_cast<unsigned char>(1));
            assert(!Gate::capture_with(base_address,memory,*owner,diagnostics,counter,1000,mask));
        }
    }
    memory=original;lanes=publications();
    assert(lease->lifecycle_revision==1);
    assert(Gate::current_with(base_address,memory,*owner,*lease,diagnostics,counter,1001));
    unsigned publication_reads=0,picked_reads=0;
    auto publication_diagnostics=[&](UINT64)->std::optional<Lanes>{++publication_reads;return lanes;};
    auto publication_picked=[&]{++picked_reads;return picked;};
    assert(Gate::publications_current_with(*lease,publication_diagnostics,publication_picked,1001));
    assert(publication_reads==2 && picked_reads==2);
    publication_reads=picked_reads=0;
    auto lifecycle_drift=[&](UINT64)->std::optional<Lanes>{auto result=lanes;if(++publication_reads==2)for(auto& lane:result)++lane.lifecycle_revision;return result;};
    assert(!Gate::publications_current_with(*lease,lifecycle_drift,publication_picked,1001));
    auto epoch_drift=lanes;for(auto& lane:epoch_drift)++lane.epoch;
    auto epoch_diagnostics=[&](UINT64)->std::optional<Lanes>{return epoch_drift;};
    assert(!Gate::publications_current_with(*lease,epoch_diagnostics,counter,1001));
    assert(!Gate::publications_current_with(*lease,diagnostics,counter,999));
    assert(!Gate::publications_current_with(*lease,diagnostics,counter,2000));
    auto saturated_lanes=lanes;for(auto& lane:saturated_lanes)lane.lifecycle_revision=std::numeric_limits<UINT64>::max();
    auto saturated_diagnostics=[&](UINT64)->std::optional<Lanes>{return saturated_lanes;};
    assert(!Gate::publications_current_with(*lease,saturated_diagnostics,counter,1001));
    picked_reads=0;auto picked_drift=[&]{return picked_reads++?picked+1:picked;};
    assert(!Gate::publications_current_with(*lease,diagnostics,picked_drift,1001));
    auto moved=*owner;moved.pitch=1;moved.yaw=2;moved.aim_origin[0]=20;
    lanes[0].serial=2;lanes[1].serial=3;
    assert(Gate::current_with(base_address,memory,moved,*lease,diagnostics,counter,1001));
    lanes=publications();lanes[0].lifecycle_revision=2;lanes[1].lifecycle_revision=2;
    assert(!Gate::current_with(base_address,memory,*owner,*lease,diagnostics,counter,1001));
    lanes=publications();lanes[0].lifecycle_revision=std::numeric_limits<UINT64>::max();
    lanes[1].lifecycle_revision=std::numeric_limits<UINT64>::max();assert(!capture());
    lanes=publications();lanes[1].lifecycle_revision=2;assert(!capture());
    lanes=publications();
    memory.put(camera_address+0x25264,1.f);memory.put(camera_address+0x25274,2.f);
    assert(Gate::current_with(base_address,memory,moved,*lease,diagnostics,counter,1001));memory=original;
    for(unsigned side=0;side<2;++side) {
        lanes=publications();lanes[side].held=d2vr::NativeRigidBody{};assert(!capture());
        lanes=publications();lanes[side].pending_release=true;assert(!capture());
        lanes=publications();lanes[side].deferred=true;assert(!capture());
        lanes=publications();lanes[side].occupied=true;assert(!capture());
        lanes=publications();lanes[side].usable=false;assert(!capture());
        for(auto role:{d2vr::RightGripOwner::Role::native,d2vr::RightGripOwner::Role::physical}) {
            lanes=publications();lanes[side].role=role;assert(!capture());
        }
        lanes=publications();lanes[side].player+=8;assert(!capture());
        lanes=publications();lanes[side].camera+=8;assert(!capture());
        lanes=publications();lanes[side].publication_owner.camera_identity.generation++;assert(!capture());
        lanes=publications();lanes[side].publication_owner.physics_identity.generation++;assert(!capture());
        lanes=publications();lanes[side].publication_owner.heading_delegate+=8;assert(!capture());
        lanes=publications();lanes[side].publication_owner.mode=1;assert(!capture());
        lanes=publications();lanes[side].epoch=8;assert(!capture());
        lanes=publications();lanes[side].source_tick=1;assert(!capture());
        lanes=publications();lanes[side].publication_tick=1;assert(!capture());
        lanes=publications();lanes[side].publication_tick=1001;assert(!capture());
        lanes=publications();lanes[side].source_tick=1001;assert(!capture());
        lanes=publications();lanes[side].source_age_valid=false;assert(!capture());
    }
    lanes=publications();picked=1;
    assert(!Gate::current_with(base_address,memory,*owner,*lease,diagnostics,counter,1001));picked=0;
    unsigned calls=0;auto aba=[&]{return static_cast<UINT64>(calls++?1:0);};
    assert(!Gate::capture_with(base_address,memory,*owner,diagnostics,aba,1000));
    calls=0;auto lanes_republished=[&](UINT64)->std::optional<Lanes>{auto result=lanes;result[0].serial+=calls++;return result;};
    assert(!Gate::capture_with(base_address,memory,*owner,lanes_republished,counter,1000));
    auto contended=[](UINT64)->std::optional<Lanes>{return {};};
    assert(!Gate::capture_with(base_address,memory,*owner,contended,counter,1000));
    calls=0;auto second_contended=[&](UINT64)->std::optional<Lanes>{if(calls++)return {};return lanes;};
    assert(!Gate::capture_with(base_address,memory,*owner,second_contended,counter,1000));
    auto cpp_fault=[](std::uintptr_t,void*,std::size_t)->bool {throw std::runtime_error("read");};
    assert(!Gate::capture_with(base_address,cpp_fault,*owner,diagnostics,counter,1000));
    auto seh_fault=[](std::uintptr_t,void*,std::size_t)->bool {RaiseException(0xe0000031,0,0,nullptr);return false;};
    assert(!Gate::capture_with(base_address,seh_fault,*owner,diagnostics,counter,1000));
    auto failed=[](std::uintptr_t,void*,std::size_t){return false;};
    assert(!Gate::capture_with(base_address,failed,*owner,diagnostics,counter,1000));
    for(const auto address:{camera_address+0x18,physics_address+0x18,body_address+0x18,inventory_address+0x18,carry_machine+0x18}) {
        memory=original;memory.put(address,r::Identity{999,10});
        assert(!Gate::current_with(base_address,memory,*owner,*lease,diagnostics,counter,1000));
    }
    memory=original;memory.put(camera_address+0x25200,1);assert(!capture());
    memory=original;memory.recycle_at=camera_address+0x18;assert(!capture());
    for(unsigned side=0;side<2;++side) {
        memory=original;memory.put(carry_states[side]+0x20,static_cast<unsigned char>(1));assert(!capture());
        memory=original;memory.put(inventory_address+0x68+side*8,std::uintptr_t{0x230000000});assert(!capture());
    }
    memory=original;memory.put(empty_address,std::array<char,6>{'S','w','o','r','d','\0'});assert(!capture());
    memory=original;bool changed=false;
    auto carry_changes=[&](std::uintptr_t address,void* output,std::size_t size) {
        const bool result=memory(address,output,size);
        if(address==empty_address && !changed){changed=true;memory.put(carry_states[0]+0x20,static_cast<unsigned char>(1));}
        return result;
    };
    assert(!Gate::capture_with(base_address,carry_changes,*owner,diagnostics,counter,1000));
    memory=original;changed=false;
    auto picked_changes=[&](std::uintptr_t address,void* output,std::size_t size) {
        const bool result=memory(address,output,size);
        if(address==empty_address && !changed){changed=true;++picked;}
        return result;
    };
    assert(!Gate::capture_with(base_address,picked_changes,*owner,diagnostics,counter,1000));picked=0;
    memory=original;lanes=publications();lanes[0].epoch=lanes[1].epoch=8;
    assert(!Gate::current_with(base_address,memory,*owner,*lease,diagnostics,counter,1000));
    lanes=publications();assert(!Gate::current_with(base_address,memory,*owner,*lease,diagnostics,counter,999));
    const auto unchanged=memory.bytes;
    d2vr::NativeGrab grab;
    for(unsigned side=0;side<2;++side)grab.publish(side,*owner,{},0,7,1000,true,false,1000);
    const auto actual=Gate::capture(base_address,memory,*owner,grab,1000);assert(actual);
    assert(Gate::publications_current(grab,*actual,1000));
    assert(Gate::current(base_address,memory,*owner,grab,*actual,1001));
    const auto prior_lifecycle=actual->lifecycle_revision;
    grab.clear();
    for(unsigned side=0;side<2;++side)grab.publish(side,*owner,{},0,7,1000,true,false,1000);
    const auto republished=grab.diagnostics(1000);assert(republished && (*republished)[0].lifecycle_revision>prior_lifecycle &&
        (*republished)[0].lifecycle_revision==(*republished)[1].lifecycle_revision);
    assert(!Gate::publications_current(grab,*actual,1000));
    assert(!Gate::current(base_address,memory,*owner,grab,*actual,1001));
    const auto after_clear=Gate::capture(base_address,memory,*owner,grab,1000);assert(after_clear && after_clear->lifecycle_revision>actual->lifecycle_revision);
    auto transitioned_owner=*owner;++transitioned_owner.camera_identity.generation;
    for(unsigned side=0;side<2;++side)grab.publish(side,transitioned_owner,{},0,8,1001,true,false,1001);
    const auto transitioned=grab.diagnostics(1001);assert(transitioned && (*transitioned)[0].lifecycle_revision>after_clear->lifecycle_revision &&
        (*transitioned)[0].lifecycle_revision==(*transitioned)[1].lifecycle_revision);
    assert(!Gate::current(base_address,memory,*owner,grab,*after_clear,1002));
    const auto test_root=std::filesystem::current_path()/L"artifacts/door-empty-lifecycle-20261015/tmp";
    std::filesystem::create_directories(test_root);
    assert(std::filesystem::is_directory(test_root));
    const auto before_install=grab.diagnostics(1000);assert(before_install);
    grab.install(fake_base,install_read,grab_gameplay,test_root/L"grab.csv");
    const auto installed=grab.diagnostics(1000);assert(installed && (*installed)[0].lifecycle_revision>(*before_install)[0].lifecycle_revision);
    grab.disable();
    assert(!Gate::publications_current(grab,*actual,1000));
    const auto before_reinstall=grab.diagnostics(1000);assert(before_reinstall);
    grab.install(fake_base,install_read,grab_gameplay,test_root/L"grab.csv");
    const auto reinstalled=grab.diagnostics(1000);assert(reinstalled && (*reinstalled)[0].lifecycle_revision>(*before_reinstall)[0].lifecycle_revision);
    grab.disable();
    memory.put(camera_address+0x18,r::Identity{6,10});
    const auto recycled=d2vr::native_player_camera(memory,base_address,camera_address);assert(recycled);
    assert(!Gate::capture(base_address,memory,*recycled,grab,1001));memory=original;
    grab.clear();assert(!Gate::current(base_address,memory,*owner,grab,*actual,1001));
    assert(memory.bytes==unchanged);
    std::cout<<"PASS: paired grab publication, lifecycle clear/install ABA, publisher owner/epoch transitions, fresh empty inventory/carry and fail-closed reads\n";
}
