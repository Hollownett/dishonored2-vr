#include "../src/native_door_ingress.hpp"
#include <cstdio>
#include <cstdlib>
#include <map>
#include <stdexcept>
#include <vector>
#ifndef main
#define D2VR_SELECTED_DOOR_FIXTURE
#include "../src/native_door_interaction.hpp"
#pragma warning(push)
#pragma warning(disable:4715)
#define main inherited_door_skeleton_check
#include "native_door_skeleton_content_check.cpp"
#undef main
namespace EmptyFixture {
#define main inherited_door_empty_check
#include "native_door_empty_gate_check.cpp"
#undef main
}
#pragma warning(pop)
#endif
using namespace d2vr;
#define CHECK(condition) do {if(!(condition)) {std::printf("FAIL %d: %s\n",__LINE__,#condition);std::exit(1);}}while(0)
namespace {
constexpr std::uintptr_t fixture_base=0x140000000;
std::map<std::uintptr_t,std::vector<unsigned char>> memory;
bool throw_read{};
bool read_memory(std::uintptr_t address,void* output,std::size_t size) {
    if(throw_read)throw std::runtime_error("reader");
    for(const auto& entry:memory)if(address>=entry.first && address-entry.first<=entry.second.size() && size<=entry.second.size()-(address-entry.first)) {
        std::memcpy(output,entry.second.data()+address-entry.first,size);return true;
    }
    return false;
}
template<class Value> void put(std::uintptr_t address,const Value& value) {
    const auto* bytes=reinterpret_cast<const unsigned char*>(&value);memory[address]={bytes,bytes+sizeof(value)};
}
void pins() {
    put(fixture_base+NativeWorldTrace::cast_rva,NativeWorldTrace::cast_code);
    put(fixture_base+NativeWorldTrace::filter_check_rva,NativeWorldTrace::filter_code);
    put(fixture_base+NativeWorldTrace::filter_body_rva,NativeWorldTrace::filter_body_code);
    put(fixture_base+NativeWorldTrace::filter_pair_rva,NativeWorldTrace::filter_pair_code);
    put(fixture_base+NativeWorldTrace::collector_add_rva,NativeWorldTrace::collector_add_code);
    put(fixture_base+NativeWorldTrace::any_hit_add_rva,NativeWorldTrace::any_hit_add_code);
    put(fixture_base+NativeWorldTrace::sphere_ctor_rva,NativeHandVolume::constructor_code);
    put(fixture_base+NativeWorldTrace::filter_vtable_rva+0x20,fixture_base+NativeWorldTrace::filter_check_rva);
    put(fixture_base+NativeWorldTrace::filter_vtable_rva+0x28,fixture_base+NativeWorldTrace::filter_pair_rva);
    put(fixture_base+NativeWorldTrace::filter_vtable_rva+0x30,fixture_base+NativeWorldTrace::filter_body_rva);
    put(fixture_base+NativeWorldTrace::collector_vtable_rva+0x18,fixture_base+NativeWorldTrace::collector_add_rva);
    put(fixture_base+NativeWorldTrace::any_hit_collector_vtable_rva+0x18,fixture_base+NativeWorldTrace::any_hit_add_rva);
}
NativeDoorIngress::Publication publication(std::uint64_t tick=1000) {
    NativeDoorIngress::Publication result{};
    auto& source=result.source;
    source.base=fixture_base;source.game=0x2000000;source.player=0x2100000;source.camera=0x2200000;source.camera_controller=0x2300000;
    source.physics=0x2400000;source.delegate=0x2500000;source.controller=0x2600000;source.world=0x2700000;source.pool=0x2800000;source.body=0x28000b0;
    source.handle=0xab000001;source.spawn=0x10001;source.body_count=4;source.broadphase=3;source.body_flags=1;
    source.camera_identity={7,0x4a};source.physics_identity={9,0x4a};source.component_list_data=0x2900000;source.component_list_count=4;source.component_list_capacity=4;
    result.mapping_axes={Vec3{1,0,0},Vec3{0,1,0},Vec3{0,0,1}};
    result.grips[0].position={0,.25,1};result.grips[1].position={0,-.25,1};
    result.tick=tick;result.source_tick=tick;result.pose_epoch=1;result.reference_epoch=2;result.revision=3;result.hands=3;
    return result;
}
HandProxies fitted(unsigned touching_proxy) {
    HandProxies result{};for(auto& sphere:result)sphere={{-.2,-1,1},.012};
    result[touching_proxy]={{-.01,-1,1},.016};return result;
}
WorldTraceQueryResult door_query(ContactSphere sphere) {
    WorldTraceQueryResult result{};result.complete=true;result.initial.emplace();result.regular.emplace();
    if(std::abs(sphere.centre.x)>=sphere.radius)return result;
    result.initial->count=1;auto& initial=result.initial->hits[0];initial.entity=0x3100000;
    initial.contact.point={0,-1,1};initial.contact.normal={-1,0,0};initial.contact.signed_separation=static_cast<float>(std::abs(sphere.centre.x)-sphere.radius);
    initial.contact.normal_valid=true;initial.contact.source_body=publication().source.handle;initial.contact.body=0xab000002;initial.contact.shape_key=7;
    result.regular->count=1;auto& regular=result.regular->hits[0];regular.point=initial.contact.point;regular.normal=initial.contact.normal;
    regular.initial_overlap=true;regular.body=2;regular.body_handle=initial.contact.body;regular.shape_key=7;regular.entity=initial.entity;
    return result;
}
void fitted_reach() {
    auto input=publication();input.grips[0].position={-.2,-1,1};input.require_fitted_contact=true;
    CHECK(!NativeDoorIngress::contact_sphere(input,0,0,1000));
    auto legacy=input;legacy.require_fitted_contact=false;
    const auto legacy_sphere=NativeDoorIngress::contact_sphere(legacy,0,0,1000);
    CHECK(legacy_sphere && legacy_sphere->radius==NativeDoorIngress::intent_radius && !NativeDoorIngress::contact_sphere(legacy,0,1,1000));
    CHECK(!NativeDoorContact::select(door_query(*legacy_sphere),input.source.handle,0x3100000));
    const PhysicalDoorGeometry::Frame frame{{0,0,0},{1,0,0},{0,1,0},{0,0,1}};
    for(const unsigned touching:{7u,3u}) {
        input.proxies[0]=fitted(touching);input.proxy_tick[0]=1000;CHECK(NativeDoorIngress::fresh(input,1000));
        unsigned examined{};bool found{};
        for(unsigned index=0;index<hand_proxy_count;++index) {
            const auto sphere=NativeDoorIngress::contact_sphere(input,0,index,1000);CHECK(sphere);++examined;
            CHECK(sphere->radius==(*input.proxies[0])[index].radius);
            const auto surface=NativeDoorContact::select(door_query(*sphere),input.source.handle,0x3100000);
            if(!surface)continue;
            const auto geometry=PhysicalDoorGeometry::sample(frame,*surface,sphere->centre,{.1,0,0},sphere->radius,true);
            CHECK(geometry && geometry->pressure==PhysicalDoorController::Pressure::Opening);found=true;break;
        }
        CHECK(found && examined==touching+1);
        CHECK(!NativeDoorIngress::contact_sphere(input,0,hand_proxy_count,1000));
        CHECK(!NativeDoorIngress::contact_sphere(input,1,0,1000));
    }
    CHECK(!NativeDoorIngress::contact_sphere(input,0,0,1033));
    auto changed=input;(*changed.proxies[0])[0].radius=.0005;CHECK(!NativeDoorIngress::fresh(changed,1000));
    changed=input;(*changed.proxies[0])[0].centre.x=NAN;CHECK(!NativeDoorIngress::contact_sphere(changed,0,0,1000));
    changed.require_fitted_contact=false;CHECK(!NativeDoorIngress::contact_sphere(changed,0,0,1000));
    changed=input;changed.source_tick=967;CHECK(!NativeDoorIngress::fresh(changed,1000));
    changed=input;changed.proxy_tick[0]=0;CHECK(!NativeDoorIngress::fresh(changed,1000));
    changed=input;changed.proxy_tick[0]=967;CHECK(!NativeDoorIngress::fresh(changed,1000));
    changed=input;changed.proxy_tick[0]=1001;CHECK(!NativeDoorIngress::fresh(changed,1000));
    pins();NativeDoorIngress ingress;CHECK(ingress.configure(fixture_base,read_memory));CHECK(!ingress.publish(input));
    auto next=input;next.tick=next.source_tick=1010;CHECK(ingress.publish(next));CHECK(ingress.publish(next));
    changed=next;(*changed.proxies[0])[0].centre.y+=.001;CHECK(!ingress.publish(changed));
    ingress.clear();CHECK(!ingress.publish(input));CHECK(ingress.publish(next));
    changed=next;(*changed.proxies[0])[0].radius+=.001;CHECK(!ingress.publish(changed));
    ingress.clear();CHECK(!ingress.publish(input));CHECK(ingress.publish(next));
    changed=next;changed.require_fitted_contact=false;CHECK(!ingress.publish(changed));
    ingress.clear();CHECK(!ingress.publish(input));CHECK(ingress.publish(next));
    changed=next;changed.proxies[0].reset();CHECK(!ingress.publish(changed));
    ingress.clear();CHECK(!ingress.publish(input));CHECK(ingress.publish(next));
    changed=next;++changed.proxy_tick[0];CHECK(!ingress.publish(changed));
}
}

#ifdef D2VR_SELECTED_DOOR_FIXTURE
namespace { const auto& ingress_pin_memory() {return memory;} }
namespace {
using namespace d2vr;
constexpr std::uintptr_t world_address=0x230000000,controller_address=0x231000000,pool_address=0x232000000,
    userdata_address=0x233000000,holder_address=0x234000000,arena_address=0x235000000,
    clips_address=0x236000000,clip_blob_address=0x237000000;
constexpr std::uint32_t source_id=0x12000002,hit_id=0x12000003;
Memory* current_memory{};
std::uint64_t clock_tick=1010;
std::uint64_t clock_now() {return clock_tick;}
bool integrated_read(std::uintptr_t address,void* output,std::size_t size) {return current_memory && (*current_memory)(address,output,size);}
std::uintptr_t relocate(std::uintptr_t value) {
    if(value>=Fixture::game && value<Fixture::game+0x400000)return EmptyFixture::game_address+(value-Fixture::game);
    if(value>=Fixture::player && value<Fixture::player+0x1000)return EmptyFixture::player_address+(value-Fixture::player);
    if(value==Fixture::world)return world_address;
    return value;
}
struct SelectedDoorFixture {
    Memory memory{};
    NativeGrab grab{};
    NativeDoorIngress ingress{};
    NativePlayerCamera camera{};
    NativeWorldQuerySource source{};
    NativeDoorIngress::Publication latest{};
    unsigned queries{},shape_key{9};
    SelectedDoorFixture() {
        SkeletonFixture skeleton;
        const auto empty=EmptyFixture::empty_fixture();memory.bytes=empty.bytes;
        for(const auto& entry:skeleton.memory.bytes)memory.bytes[relocate(entry.first)]=entry.second;
        for(const auto& entry:skeleton.memory.bytes)if(entry.first%8==0) {
            std::uintptr_t value{};bool present=true;
            for(unsigned index=0;index<8;++index) {
                const auto found=skeleton.memory.bytes.find(entry.first+index);
                if(found==skeleton.memory.bytes.end()){present=false;break;}
                value|=static_cast<std::uintptr_t>(found->second)<<(index*8);
            }
            if(present && relocate(value)!=value)memory.put(relocate(entry.first),relocate(value));
        }
        pins();
        for(const auto& entry:ingress_pin_memory())for(std::size_t index=0;index<entry.second.size();++index)
            memory.put(entry.first+index,entry.second[index]);
        const auto game=EmptyFixture::game_address,player=EmptyFixture::player_address;
        const auto delegate=EmptyFixture::delegate_address;
        memory.put(delegate+0x148,controller_address);memory.put(controller_address,Fixture::base+0x1ad26a0);
        memory.put(controller_address+0x140,game+0x60280);memory.put(controller_address+0x40,world_address);
        memory.put(controller_address+0x38,source_id);memory.put(controller_address+0x3c,std::uint32_t{0x120});
        memory.put(world_address+0x28,pool_address);memory.put(world_address+0x30,std::uint32_t{8});
        memory.put(world_address+NativeWorldTrace::shape_tag_codec_offset,std::uintptr_t{0x238000000});
        const auto source_body=pool_address+2*0xb0,hit_body=pool_address+3*0xb0;
        for(const auto record:{source_body,hit_body}) {
            memory.put(record+0x44,std::uint8_t{3});memory.put(record+0x78,std::uint32_t{1});
        }
        memory.put(source_body+0x70,source_id);memory.put(source_body+0x6c,std::uint32_t{0x220});
        memory.put(hit_body+0x70,hit_id);memory.put(hit_body+0x90,userdata_address);memory.put(userdata_address+8,Fixture::component);
        memory.put(player+0x300,std::array<float,3>{-1,0,0});
        memory.put(Fixture::part+0x300,std::array<float,12>{0,0,0,1,0,0,0,1,0,0,0,1});
        memory.put(player+0x268,rig::List{EmptyFixture::components_address,5,5u<<8});
        memory.put(EmptyFixture::components_address+24,EmptyFixture::controller_address);
        memory.put(EmptyFixture::components_address+32,EmptyFixture::physics_address);
        const auto owner=NativeDoorOwner::capture(Fixture::base,memory,Fixture::door);assert(owner);
        memory.put(owner->container,holder_address);memory.put(holder_address,arena_address);
        const auto context=arena_address+owner->context_offset;
        memory.put(context+0x10,owner->container+0x38);memory.put(context+0x18,owner->container+0x20);
        memory.put(context+0xa78,rig::List{clips_address,9,32u<<8});
        memory.put(clips_address+owner->clips[1]*0x48ull+0x30,SkeletonFixture::clip);
        memory.put(SkeletonFixture::clip+0x38,1.f);memory.put(SkeletonFixture::clip+0x48,clip_blob_address);
        memory.put(SkeletonFixture::clip+0x50,std::uint32_t{NativeDoorClipContent::resident_size});
        memory.put(clip_blob_address,NativeDoorClipContent::ccw_bytes);
        const auto native_camera=native_player_camera(memory,Fixture::base,EmptyFixture::camera_address);assert(native_camera);camera=*native_camera;
        const auto native_source=native_world_query_source(memory,Fixture::base,camera.camera,world_address);assert(native_source);source=*native_source;
        const auto hands=rig::native_hand_state(memory,Fixture::base);assert(hands && hands->empty_candidates==3);
        assert(NativeDoorClipSelection::capture(memory,*owner,true));
        assert(NativeDoorSkeletonContent::capture(Fixture::base,memory,*owner,SkeletonFixture::clip));
        current_memory=&memory;assert(ingress.configure(Fixture::base,integrated_read));
        publish(1000);publish(1010);
    }
    void publish(std::uint64_t tick) {
        NativeDoorIngress::Publication publication{};publication.source=source;publication.tick=publication.source_tick=tick;
        publication.curls=latest.curls;publication.pose_epoch=7;publication.reference_epoch=8;publication.hands=3;publication.player_origin={-1,0,0};
        publication.mapping_axes={Vec3{1,0,0},Vec3{0,1,0},Vec3{0,0,1}};
        for(unsigned side=0;side<2;++side) {
            publication.grips[side].position={tick==1000?.005:.01,-1.-side*.1,1};
            grab.publish(side,camera,publication.grips[side],0,7,tick,true,false,tick);
        }
        latest=publication;const bool accepted=ingress.publish(publication);assert(accepted==(tick!=1000));
    }
    WorldTraceQueryResult query(const NativeWorldQuerySource& actual,Vec3 from,Vec3 to,double radius,NativeDoorIngress::Read read) {
        ++queries;assert(actual==source && read==integrated_read && radius==NativeDoorIngress::intent_radius);
        assert(length(to-from-Vec3{0,0,.002})<1e-9);
        WorldTraceQueryResult result{};result.complete=true;result.initial.emplace();result.regular.emplace();
        result.initial->count=result.regular->count=1;
        auto& initial=result.initial->hits[0];initial.entity=Fixture::door;
        initial.contact.point={0,from.y,1};initial.contact.normal={-1,0,0};initial.contact.signed_separation=-.01f;
        initial.contact.source_body=source_id;initial.contact.body=hit_id;initial.contact.shape_key=shape_key;initial.contact.normal_valid=true;
        auto& regular=result.regular->hits[0];regular.entity=Fixture::door;regular.body=3;regular.body_handle=hit_id;regular.shape_key=shape_key;
        regular.point=initial.contact.point;regular.normal=initial.contact.normal;regular.normal_valid=true;regular.fraction=0;
        return result;
    }
    void service() {
        ingress.service_with(camera,grab,clock_tick,[]{return true;},[&](const auto& actual,auto from,auto to,auto radius,auto read) {
            return query(actual,from,to,radius,read);
        },clock_now);
    }
    bool contact() {const auto result=ingress.contacts(clock_tick);return result[0].has_value() || result[1].has_value();}
};
}
namespace d2vr {
struct NativeGrabCheck {
    static void picked(NativeGrab& grab) {++grab.picked_;}
    static void occupied(NativeGrab& grab,unsigned side,unsigned mode) {
        if(mode==0)grab.lanes_[side].held.emplace();
        if(mode==1)grab.lanes_[side].pending_release=true;
        if(mode==2)grab.lanes_[side].decision_deferred=true;
        if(mode==3)grab.lanes_[side].owner.claim_physical();
    }
};
struct NativeDoorInteractionFixture {
    static bool same_lane(const NativeDoorIngress::Contact& previous,const NativeDoorIngress::Contact& next) {
        NativeDoorInteraction::Lane lane{};lane.valid=true;lane.contact=previous;
        NativeDoorInteraction::Contacts contacts{};contacts[next.side]=next;
        return NativeDoorInteraction::choose(contacts,lane,clock_tick)!=nullptr;
    }
    static bool current(const NativeDoorIngress::Contact& contact,const NativePlayerCamera& camera,NativeGrab& grab) {
        NativeDoorInteraction controller;controller.base_=Fixture::base;controller.read_=&integrated_read;
        controller.logic_=+[]()->UINT {return 2;};controller.grab_=&grab;
        NativeDoorInteraction::Authority authority{};authority.contact=contact;authority.camera=camera;
        authority.sensor=0x239000000;authority.sensor_identity={77,0xa};
        return controller.validate(authority,clock_tick,true);
    }
};
}
namespace {
void arm(SelectedDoorFixture& fixture,unsigned side) {
    const auto names=EmptyFixture::empty_address+0x100+side*0x100;
    const auto name=EmptyFixture::empty_address+0x900;
    fixture.memory.put(EmptyFixture::inventory_address+(side?0x68:0x70),std::uintptr_t{0x23a000000});
    fixture.memory.put(names,name);fixture.memory.put(name,std::array<char,6>{'S','w','o','r','d','\0'});
}
void selected_hand_admission() {
    for(unsigned side=0;side<2;++side)for(unsigned mode=0;mode<15;++mode) {
        clock_tick=1010;SelectedDoorFixture fixture;const auto opposite=side^1u;
        arm(fixture,opposite);fixture.latest.curls[opposite]=1.f;fixture.publish(1020);clock_tick=1020;
        fixture.memory.put(0x239000000,Fixture::base+0x1b403b8);
        fixture.memory.put(0x239000018,rig::Identity{77,0xa});
        fixture.memory.put(0x239000010,fixture.camera.player);
        fixture.memory.put(0x239000038,static_cast<unsigned char>(1));
        fixture.memory.put(0x239000040,fixture.camera.camera);
        if(mode==1)arm(fixture,side);
        if(mode>=2 && mode<=4)NativeGrabCheck::occupied(fixture.grab,side,mode-2);
        if(mode==5)fixture.memory.put(EmptyFixture::carry_states[0]+0x20,static_cast<unsigned char>(1));
        if(mode==6)clock_tick=1121;
        if(mode==7)fixture.latest.curls[side]=.56f,fixture.publish(1030),clock_tick=1030;
        if(mode==13)fixture.grab.publish(opposite,fixture.camera,fixture.latest.grips[opposite],0,7,769,true,false,769);
        if(mode==14)NativeGrabCheck::occupied(fixture.grab,side,3);
        unsigned owner_reads{};
        if(mode==10)fixture.memory.before=[&](std::uintptr_t address) {
            if(fixture.queries && address==Fixture::component+0x18 && ++owner_reads==2)
                fixture.memory.put(Fixture::component+0x18,rig::Identity{99,0xa});
        };
        if(mode>=9 && mode<=12) {
            fixture.ingress.service_with(fixture.camera,fixture.grab,clock_tick,[]{return true;},
                [&](const auto& actual,auto from,auto to,auto radius,auto read) {
                    const auto result=fixture.query(actual,from,to,radius,read);
                    if(mode==9)NativeGrabCheck::picked(fixture.grab);
                    if(mode==11)fixture.memory.put(EmptyFixture::carry_states[0]+0x20,static_cast<unsigned char>(1));
                    if(mode==12)clock_tick=1121;
                    return result;
                },clock_now);
        }else fixture.service();
        const auto contacts=fixture.ingress.contacts(clock_tick);
        if(mode!=0 && mode!=8) {if(contacts[side])std::printf("unexpected contact side=%u mode=%u\n",side,mode);CHECK(!contacts[side]);continue;}
        CHECK(contacts[side] && !contacts[opposite] && contacts[side]->empty.admitted_hands==(1u<<side));
        CHECK(!NativeDoorEmptyGate::capture(Fixture::base,fixture.memory,fixture.camera,fixture.grab,clock_tick));
        CHECK(NativeDoorInteractionFixture::current(*contacts[side],fixture.camera,fixture.grab));
        auto closed=*contacts[side];closed.publication.curls[side]=.56f;
        CHECK(!NativeDoorInteractionFixture::current(closed,fixture.camera,fixture.grab));
        if(mode==8) {
            const auto token=contacts[side]->contact_token;++fixture.shape_key;fixture.service();
            const auto changed=fixture.ingress.contacts(clock_tick);CHECK(changed[side] && changed[side]->contact_token!=token);
            CHECK(!NativeDoorInteractionFixture::same_lane(*contacts[side],*changed[side]));
        }
        arm(fixture,side);
        CHECK(!NativeDoorInteractionFixture::current(*contacts[side],fixture.camera,fixture.grab));
    }
    clock_tick=1010;
}
}

#endif
int main() {
#ifdef D2VR_SELECTED_DOOR_FIXTURE
    selected_hand_admission();
#endif
    constexpr std::uintptr_t component=0x3100000;
    put(component+0x48,2u);put(component+0x4c,2u);put(component+0xb8,fixture_base+0x1c56988);
    put(component+0xc0,component+0x40);put(component+0xd0,component);
    put(fixture_base+0x1c56988+0x28,fixture_base+NativeDoorEventAdmission::opened_state_getter_rva);
    put(fixture_base+NativeDoorEventAdmission::opened_state_getter_rva,NativeDoorEventAdmission::opened_state_getter_code);
    for(const unsigned char sense:{static_cast<unsigned char>(0),static_cast<unsigned char>(1)}) for(const unsigned observed_field:{0u,1u,2u}) {
        put(component+0xc8,sense);put(component+0xd8,observed_field);unsigned char captured=2;
        CHECK(NativeDoorEventAdmission::opened_state(fixture_base,read_memory,component,captured));CHECK(captured==sense);
    }
    unsigned char captured{};throw_read=true;CHECK(!NativeDoorEventAdmission::opened_state(fixture_base,read_memory,component,captured));throw_read=false;
    put(fixture_base+NativeDoorEventAdmission::opened_state_getter_rva,std::array<unsigned char,6>{0xb8,1,0,0,0,0xc3});
    CHECK(!NativeDoorEventAdmission::opened_state(fixture_base,read_memory,component,captured));memory.clear();
    auto first=publication();CHECK(NativeDoorIngress::fresh(first,1000));CHECK(NativeDoorIngress::fresh(first,1001));
    CHECK(!NativeDoorIngress::fresh(first,999));CHECK(!NativeDoorIngress::fresh(first,1101));
    auto changed=first;changed.source_tick=999999;CHECK(!NativeDoorIngress::fresh(changed,1000));
    changed=first;changed.hands=1;CHECK(!NativeDoorIngress::fresh(changed,1000));
    changed=first;changed.curls[1]=.56f;CHECK(NativeDoorIngress::fresh(changed,1000));
    CHECK(NativeDoorIngress::contact_sphere(changed,0,0,1000));CHECK(!NativeDoorIngress::contact_sphere(changed,1,0,1000));
    changed.curls[0]=.56f;CHECK(!NativeDoorIngress::contact_sphere(changed,0,0,1000));
    changed=first;changed.curls[1]=1.01f;CHECK(!NativeDoorIngress::fresh(changed,1000));
    changed=first;changed.curls[1]=NAN;CHECK(!NativeDoorIngress::fresh(changed,1000));
    changed=first;changed.grips[0].position.x=NAN;CHECK(!NativeDoorIngress::fresh(changed,1000));
    changed=first;changed.velocities[1].z=INFINITY;CHECK(!NativeDoorIngress::fresh(changed,1000));
    changed=first;changed.mapping_axes[2]={0,0,-1};CHECK(!NativeDoorIngress::fresh(changed,1000));
    changed=first;changed.source.camera_identity.flags=0x3a;CHECK(NativeDoorIngress::same_source(first.source,changed.source));
    changed.source.camera_identity.generation++;CHECK(!NativeDoorIngress::same_source(first.source,changed.source));
    changed=first;changed.source.physics_identity.flags^=0x80000000;CHECK(!NativeDoorIngress::same_source(first.source,changed.source));
    changed=first;changed.source.handle^=0x1000000;CHECK(!NativeDoorIngress::same_source(first.source,changed.source));
    auto second=publication(1010);second.grips[0].position.x=.01;CHECK(NativeDoorIngress::continuous(first,second));
    changed=second;changed.pose_epoch++;CHECK(!NativeDoorIngress::continuous(first,changed));
    changed=second;changed.reference_epoch++;CHECK(!NativeDoorIngress::continuous(first,changed));
    changed=second;changed.source_tick=first.source_tick;CHECK(!NativeDoorIngress::continuous(first,changed));
    changed=second;changed.player_origin.x=.21;CHECK(!NativeDoorIngress::continuous(first,changed));
    changed=second;changed.grips[1].position.x=.36;CHECK(!NativeDoorIngress::continuous(first,changed));
    changed=second;changed.mapping_axes={Vec3{0,1,0},Vec3{-1,0,0},Vec3{0,0,1}};CHECK(!NativeDoorIngress::continuous(first,changed));
    NativeDoorIngress ingress;CHECK(!ingress.publish(first));CHECK(!ingress.configure(fixture_base,read_memory));pins();
    throw_read=true;CHECK(!ingress.configure(fixture_base,read_memory));throw_read=false;
    CHECK(ingress.configure(fixture_base,read_memory));CHECK(!ingress.publish(first));CHECK(ingress.publish(second));
    throw_read=true;CHECK(!ingress.configure(fixture_base,read_memory));throw_read=false;
    CHECK(!ingress.publish(publication(1015)));CHECK(ingress.configure(fixture_base,read_memory));
    changed=first;changed.velocity_ready=true;changed.velocities[0]={15,0,0};CHECK(!ingress.publish(changed));
    CHECK(ingress.publish(second));
    NativeGrab grab;unsigned queries{};
    auto query=[&](const NativeWorldQuerySource&,Vec3,Vec3,double,NativeDoorIngress::Read) {++queries;return WorldTraceQueryResult{};};
    ingress.service_with(NativePlayerCamera{},grab,1010,[]{return false;},query);CHECK(!queries);
    ingress.service_with(NativePlayerCamera{},grab,1010,[]{return true;},query);CHECK(!queries);
    CHECK(!ingress.contacts(1010)[0] && !ingress.contacts(1010)[1]);
    changed=publication(1020);changed.grips[0].position.x=2;CHECK(!ingress.publish(changed));
    changed=publication(1030);changed.grips[0].position.x=2.01;CHECK(!ingress.publish(changed));
    changed=publication(1221);changed.grips[0].position.x=2.02;CHECK(!ingress.publish(changed));
    changed=publication(1231);changed.grips[0].position.x=2.03;CHECK(!ingress.publish(changed));
    ingress.clear();CHECK(!ingress.publish(publication(1300)));CHECK(ingress.publish(publication(1310)));
    CHECK(ingress.publish(publication(1310)));
    changed=publication(1310);changed.tick=1311;CHECK(ingress.publish(changed));
    changed=publication(1310);changed.grips[0].position.x=.01;CHECK(!ingress.publish(changed));
    CHECK(ingress.publish(publication(1320)));
    ingress.stop();CHECK(!ingress.publish(publication(1330)));
    fitted_reach();
    std::puts("native door ingress check passed");
}
