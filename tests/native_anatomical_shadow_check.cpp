#define NOMINMAX
#include "../src/native_interaction_aim.hpp"
#include "../src/synchronized_observer.hpp"
#include "native_carry_fixture.hpp"
#include <cassert>
#include <iostream>
#include <sstream>
#include <vector>

namespace {
constexpr std::uintptr_t camera_address=0x220000000,controller_address=0x221000000,
    physics_address=0x222000000,delegate_address=0x223000000,inventory_address=0x224000000,
    graph_address=0x225000000,empty_address=0x226000000;
constexpr std::uintptr_t query_controller=0x227000000,world_address=0x228000000,pool_address=0x229000000,sensor_address=0x22a000000;
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

Memory memory;
d2vr::NativeInteractionAim* current_aim{};
d2vr::NativeGrab* current_grab{};
unsigned fixture_cast_count{};
enum class Mutation {none,clear,republish,hand,source};
Mutation mutation{};
UINT gameplay(){return 2;}
bool read_memory(std::uintptr_t at,void* output,std::size_t size) {
    if((at>=base_address+0xc80000 && at-base_address-0xc80000<=0x30000 && size<=0x30000-(at-base_address-0xc80000))) {
        std::memcpy(output,reinterpret_cast<void*>(at),size);return true;
    }
    return memory(at,output,size);
}
void __fastcall fake_cast(void*,const d2vr::NativeCastRequest* request,const float*,d2vr::NativeClosestCollector* regular,std::uintptr_t initial);
void seed_query() {
    const auto prior=memory.bytes;
    memory=empty_fixture();
    memory.put(player_address+0xa8,std::uint32_t{123});
    for(const auto& item:prior)if(item.first>=base_address && item.first<base_address+0x4000000)memory.bytes[item.first]=item.second;
    memory.list(player_address+0x268,components_address,5,5);
    memory.put(components_address,body_address);memory.put(components_address+8,carry_machine);
    memory.put(components_address+16,camera_address);memory.put(components_address+24,controller_address);memory.put(components_address+32,physics_address);
    memory.put(delegate_address+0xd0,1.5f);
    memory.put(delegate_address+0x148,query_controller);memory.put(query_controller,base_address+0x1ad26a0);
    memory.put(query_controller+0x140,game_address+0x60280);memory.put(query_controller+0x40,world_address);
    memory.put(query_controller+0x38,std::uint32_t{0x12000002});memory.put(query_controller+0x3c,std::uint32_t{0x120});
    memory.put(game_address+0x60410,world_address);memory.put(world_address+0x28,pool_address);memory.put(world_address+0x30,std::uint32_t{8});
    memory.put(world_address+d2vr::NativeWorldTrace::shape_tag_codec_offset,std::uintptr_t{0x22b000000});
    for(unsigned index=2;index<4;++index) {
        memory.put(pool_address+index*0xb0,std::array<std::byte,0xb0>{});
        memory.put(pool_address+index*0xb0+0x44,std::uint8_t{3});
        memory.put(pool_address+index*0xb0+0x70,std::uint32_t{0x12000000u+index});
        memory.put(pool_address+index*0xb0+0x78,std::uint32_t{index});
        memory.put(pool_address+index*0xb0+0x6c,std::uint32_t{0x220});
    }
    memory.put(sensor_address,base_address+0x1b403b8);memory.put(sensor_address+0x10,player_address);
    memory.put(sensor_address+0x18,r::Identity{7,10});memory.put(sensor_address+0x40,camera_address);memory.put(sensor_address+0x38,std::uint8_t{1});
}
template<class Bytes>void code(std::uintptr_t at,const Bytes& bytes){
    memory.put(at,bytes);
    if(at>=base_address+0xc80000 && at+bytes.size()<=base_address+0xcb0000)std::memcpy(reinterpret_cast<void*>(at),bytes.data(),bytes.size());
}
void executable_fixture() {
    const auto arena=VirtualAlloc(reinterpret_cast<void*>(base_address+0xc80000),0x30000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(arena);
    assert(VirtualAlloc(reinterpret_cast<void*>(base_address+0x3e80000),0x10000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    using Trace=d2vr::NativeWorldTrace;
    auto* cast=reinterpret_cast<unsigned char*>(base_address+Trace::cast_rva);
    std::memcpy(cast,Trace::cast_code.data(),Trace::cast_code.size());
    cast[48]=cast[49]=0;
    const auto stub=base_address+Trace::cast_rva+0x200;
    const std::array<unsigned char,3> null_profiler{0x31,0xc0,0xc3};
    std::memcpy(reinterpret_cast<void*>(stub),null_profiler.data(),null_profiler.size());
    std::memcpy(cast+50+0x4706,&stub,8);
    std::vector<unsigned char> tail{0x48,0x89,0xf9,0x4c,0x89,0xf2,0x49,0x89,0xe8,0x49,0x89,0xf1,
        0x48,0x8b,0x44,0x24,0x70,0x48,0x89,0x44,0x24,0x20,0x48,0xb8};
    const auto bridge=reinterpret_cast<std::uintptr_t>(&fake_cast);
    for(unsigned byte=0;byte<8;++byte)tail.push_back(static_cast<unsigned char>(bridge>>(byte*8)));
    const std::array<unsigned char,29> ending{0xff,0xd0,0x48,0x83,0xc4,0x40,0x41,0x5e,
        0x48,0x8b,0x5c,0x24,0x08,0x48,0x8b,0x6c,0x24,0x10,0x48,0x8b,0x74,0x24,0x18,0x48,0x8b,0x7c,0x24,0x20,0xc3};
    tail.insert(tail.end(),ending.begin(),ending.end());std::memcpy(cast+50,tail.data(),tail.size());
    std::memcpy(reinterpret_cast<void*>(base_address+Trace::sphere_ctor_rva),d2vr::NativeHandVolume::constructor_code.data(),d2vr::NativeHandVolume::constructor_code.size());
    code(base_address+Trace::filter_check_rva,Trace::filter_code);code(base_address+Trace::filter_body_rva,Trace::filter_body_code);
    code(base_address+Trace::filter_pair_rva,Trace::filter_pair_code);code(base_address+Trace::collector_add_rva,Trace::collector_add_code);
    code(base_address+Trace::any_hit_add_rva,Trace::any_hit_add_code);
    memory.put(base_address+Trace::filter_vtable_rva+0x20,base_address+Trace::filter_check_rva);
    memory.put(base_address+Trace::filter_vtable_rva+0x28,base_address+Trace::filter_pair_rva);
    memory.put(base_address+Trace::filter_vtable_rva+0x30,base_address+Trace::filter_body_rva);
    memory.put(base_address+Trace::collector_vtable_rva+0x18,base_address+Trace::collector_add_rva);
    memory.put(base_address+Trace::any_hit_collector_vtable_rva+0x18,base_address+Trace::any_hit_add_rva);
    for(unsigned index=0;index<d2vr::NativeInitialContactCollector::native_slots.size();++index)
        memory.put(base_address+d2vr::NativeInitialContactCollector::native_vtable_rva+index*8,base_address+d2vr::NativeInitialContactCollector::native_slots[index]);
    code(base_address+d2vr::NativeInitialContactCollector::native_slots[0],d2vr::NativeInitialContactCollector::getter_code);
    code(base_address+d2vr::NativeInitialContactCollector::native_slots[4],d2vr::NativeInitialContactCollector::noop_code);
    DWORD old{};assert(VirtualProtect(arena,0x30000,PAGE_EXECUTE_READ,&old));assert(FlushInstructionCache(GetCurrentProcess(),arena,0x30000));
    assert(Trace::verify(base_address,read_memory));
}
}
extern "C" MH_STATUS WINAPI MH_CreateHook(LPVOID,LPVOID,LPVOID*){return MH_ERROR_UNSUPPORTED_FUNCTION;}
extern "C" MH_STATUS WINAPI MH_EnableHook(LPVOID){return MH_OK;}
extern "C" MH_STATUS WINAPI MH_DisableHook(LPVOID){return MH_OK;}

namespace d2vr {
struct NativeGrabCheck {
    static void activate(NativeGrab& grab){grab.active_=true;}
};
struct NativeInteractionStopFixture {
    static void setup(NativeInteractionAim& aim,NativeGrab& grab) {
        aim.base_=base_address;aim.read_=&read_memory;aim.logic_=&gameplay;aim.physical_grab_=&grab;aim.active_=true;aim.stopping_=false;
        assert(aim.anatomical_contact_.start());
    }
    static void service(NativeInteractionAim& aim,const NativePlayerCamera& camera) {
        NativeInteractionAim::Context scope{};scope.camera=camera;scope.sensor=sensor_address;scope.sensor_identity={7,10};scope.stage=0;
        aim.service_anatomical_contact(scope);
    }
    static void default_off(NativeInteractionAim& aim,const NativePlayerCamera& camera,const NativeWorldQuerySource& source) {
        assert(!aim.full_hand_collision());
        NativeInteractionAim::Context scope{};scope.camera=camera;scope.sensor=sensor_address;scope.sensor_identity={7,10};scope.stage=0;
        const auto owner=aim.anatomical_scope_owner(scope);assert(owner);
        const auto arms=rig::player_arms(read_memory,base_address);assert(arms);
        constexpr std::array<float,15> frame{0,0,0,1,0,0,0,1,0,0,0,1,1,1,1};
        const auto body_skeleton=fullbody::skeleton(fullbody::data::kBody);
        std::array<rig::Transform,20> native_body{};
        for(unsigned joint=0;joint<20;++joint)native_body[joint]=fullbody::to_transform(body_skeleton.bind[joint]);
        std::array<std::array<rig::Transform,69>,2> native_arm{};std::array<rig::ArmBinding,2> bindings{};
        fullbody::Tracking tracking{};tracking.eye=body_skeleton.bind[body_skeleton.find("camera_jnt")];
        tracking.epoch=1;tracking.pose_epoch=7;tracking.reference_epoch=5;tracking.mapping_revision=3;tracking.tracking_revision=31;
        tracking.tracked_hands=tracking.raw_grip_hands=3;
        for(unsigned side=0;side<2;++side) {
            const auto skeleton=fullbody::skeleton(side?fullbody::data::kRightArm:fullbody::data::kLeftArm);
            const std::string prefix=side?"right":"left";auto& binding=bindings[side];auto& palette=native_arm[side];
            binding.chain={static_cast<unsigned>(skeleton.find(prefix+"_arm_jnt")),static_cast<unsigned>(skeleton.find(prefix+"_elbow_jnt")),
                static_cast<unsigned>(skeleton.find(prefix+"_wrist_jnt"))};
            for(unsigned joint=0;joint<69;++joint) {binding.parents[joint]=static_cast<std::int16_t>(skeleton.parent[joint]);palette[joint]=fullbody::to_transform(skeleton.bind[joint]);}
            const std::array<std::string,5> digits{"thumb","index","medium","ring","pinky"};
            for(unsigned finger=0;finger<5;++finger)for(unsigned segment=0;segment<3;++segment)
                binding.finger_chains[finger][segment]=static_cast<unsigned>(skeleton.find(prefix+"_"+digits[finger]+"_0"+std::to_string(segment+(finger?2:1))+"_jnt"));
            binding.palm={static_cast<unsigned>(skeleton.find(prefix+"_helper_underwrist_jnt")),static_cast<unsigned>(skeleton.find(prefix+"_helper_upperwrist_jnt"))};
            binding.bind_wrist=palette[binding.chain.wrist];std::array<rig::Transform,4> knuckles{};
            for(unsigned finger=1;finger<5;++finger)knuckles[finger-1]=palette[binding.finger_chains[finger][0]];
            binding.bind_grip=rig::hand_bind_grip(binding.bind_wrist,knuckles);binding.fingers=rig::finger_binding(palette,binding.parents,binding.finger_chains);
            tracking.grip[side]=body::mul(body_skeleton.bind[body_skeleton.find("center_spine03_jnt")],fullbody::to_xf(binding.bind_grip));
            tracking.grip[side].p.y+=side?.12:-.12;tracking.grip[side].p.x+=.08;tracking.raw_grip[side]=tracking.grip[side];
            tracking.grip_valid[side]=true;tracking.hand_empty[side]=true;
        }
        fullbody::Driver driver;driver.tune(0,1);driver.note_root(frame);
        for(unsigned side=0;side<2;++side)(void)driver.arm(side,native_arm[side],frame);
        aim.full_hand_contact_.observe(*owner);const auto producer=aim.full_hand_contact_.producer(0);assert(producer);
        const auto now=GetTickCount64();tracking.tick=tracking.source_tick=now;tracking.seconds=now/1000.;
        driver.publish(tracking,{});assert(driver.body(native_body,frame,1));
        const auto actual=driver.arm_frozen(0,native_arm[0],frame,now,2);assert(actual && actual->raw_joints);
        const auto prepared=driver.prepare_contact_arm(0,*actual,frame,now);assert(prepared);
        const auto geometry=HandPaletteContact::bind(bindings[0]);assert(geometry);
        const auto proxies=geometry->proxies(*actual->raw_joints,frame);assert(proxies);
        NativeFullHandContact::Sample sample{};sample.owner=*owner;sample.source=source;sample.side=0;sample.frame=1;
        sample.binding=std::make_shared<rig::NativeArmBindingSnapshot>(rig::NativeArmBindingSnapshot{*arms,bindings,{}});
        sample.producer=*producer;sample.prepared=*prepared;sample.fit_revision=actual->fit_revision;sample.tracking=actual->tracking;
        sample.tracking.raw_grip[0]=body::mul(actual->world_from_solve,sample.tracking.raw_grip[0]);sample.proxies=*proxies;
        assert(aim.full_hand_contact_.publish(sample,GetTickCount64()));
        const auto before=aim.full_hand_contact_.casts.load();
        aim.service_full_hand_contact(scope);assert(aim.full_hand_contact_.casts.load()==before);
        assert(aim.full_hand_contact_.latest(0) && !aim.full_hand_contact_.candidate(0,GetTickCount64()));
        aim.full_hand_collision(false);assert(!aim.full_hand_contact_.latest(0));
        aim.service_full_hand_contact(scope);assert(aim.full_hand_contact_.casts.load()==before);
        assert(!aim.full_hand_contact_.candidate(0,GetTickCount64()) && !aim.full_hand_contact_.producer_current(*producer));
    }
};
}
namespace {
void __fastcall fake_cast(void*,const d2vr::NativeCastRequest* request,const float*,d2vr::NativeClosestCollector* regular,std::uintptr_t initial) {
    using namespace d2vr;
    assert(request->source_body==pool_address+2*0xb0 && initial && regular);++fixture_cast_count;
    if(fixture_cast_count==5 && mutation==Mutation::clear)current_aim->clear_anatomical_contact();
    if(fixture_cast_count==5 && mutation==Mutation::republish) {
        current_grab->clear();
        const auto owner=native_player_camera(read_memory,base_address,camera_address);assert(owner);
        const auto now=GetTickCount64();for(unsigned side=0;side<2;++side)current_grab->publish(side,*owner,{},0,7,now,true,false,now);
    }
    if(fixture_cast_count==5 && mutation==Mutation::hand)memory.put(inventory_address+0x18,r::Identity{401,10});
    if(fixture_cast_count==5 && mutation==Mutation::source)memory.put(pool_address+2*0xb0+0x70,std::uint32_t{0x13000002});
    NativeInitialContactRecord record{};
    const std::array<float,3> point{request->origin[0],request->origin[1],request->origin[2]},normal{-1,0,0};
    const float separation=-.003f;const std::uint32_t source=0x12000002,target=0x12000003,key=0;
    std::memcpy(record.bytes.data(),point.data(),12);std::memcpy(record.bytes.data()+0x10,normal.data(),12);
    std::memcpy(record.bytes.data()+0x20,&separation,4);std::memcpy(record.bytes.data()+0x28,&source,4);
    std::memcpy(record.bytes.data()+0x48,&target,4);std::memcpy(record.bytes.data()+0x50,&key,4);
    auto* initial_collector=reinterpret_cast<NativeInitialContactCollector*>(initial);
    const auto* initial_table=reinterpret_cast<const std::uintptr_t*>(initial_collector->vtable);
    reinterpret_cast<void(__fastcall*)(NativeInitialContactCollector*,const void*)>(initial_table[3])(initial_collector,record.bytes.data());
    const float fraction=0;std::memcpy(record.bytes.data()+0x20,&fraction,4);
    const auto* regular_table=reinterpret_cast<const std::uintptr_t*>(regular->vtable);
    reinterpret_cast<void(__fastcall*)(NativeRegularContactCollector*,const void*)>(regular_table[3])(reinterpret_cast<NativeRegularContactCollector*>(regular),record.bytes.data());
}
void publish_pair(d2vr::NativeInteractionAim& aim,const d2vr::NativeWorldQuerySource& source,const d2vr::NativePlayerCamera& camera) {
    using namespace d2vr;
    NativeInteractionStopFixture::service(aim,camera);
    const auto admission=aim.anatomical_admission();assert(admission);
    const auto arms=rig::player_arms(read_memory,base_address);assert(arms);
    auto binding=std::make_shared<rig::NativeArmBindingSnapshot>(rig::NativeArmBindingSnapshot{*arms,{},{}});
    const auto now=GetTickCount64();
    for(unsigned side=0;side<2;++side) {
        AnatomicalRawPair::Publication publication{};publication.admission=*admission;publication.source=source;publication.binding=binding;
        publication.frame=19;publication.solved_revision=23;publication.fit_revision=1;publication.root_mapping_valid=true;publication.side=side;
        publication.model_frame={0,0,0,1,0,0,0,1,0,0,0,1,1,1,1};
        publication.tracking.tick=publication.tracking.source_tick=now;publication.tracking.tracking_revision=31;
        publication.tracking.pose_epoch=7;publication.tracking.reference_epoch=5;publication.tracking.mapping_revision=3;
        publication.tracking.raw_grip_hands=publication.tracking.tracked_hands=3;
        publication.tracking.grip_valid[0]=publication.tracking.grip_valid[1]=true;
        publication.tracking.hand_empty[0]=publication.tracking.hand_empty[1]=true;
        for(unsigned proxy=0;proxy<24;++proxy)publication.proxies[proxy]={{.1*proxy,side?.2:-.2,1},.015};
        assert(aim.publish_anatomical_raw(std::move(publication),now)==(side==1));
    }
}
void run(Mutation mode) {
    using namespace d2vr;
    seed_query();
    if(mode==Mutation::none)executable_fixture();
    NativeGrab grab;NativeGrabCheck::activate(grab);
    NativeInteractionAim aim;current_aim=&aim;current_grab=&grab;NativeInteractionStopFixture::setup(aim,grab);
    const auto camera=native_player_camera(read_memory,base_address,camera_address);assert(camera);
    const auto source=native_world_query_source(read_memory,base_address,camera_address,world_address);assert(source);
    NativeInteractionStopFixture::default_off(aim,*camera,*source);
    const auto now=GetTickCount64();for(unsigned side=0;side<2;++side)grab.publish(side,*camera,{},0,7,now,true,false,now);
    assert(NativeDoorEmptyGate::capture(base_address,read_memory,*camera,grab,now));
    aim.anatomical_shadow(true);publish_pair(aim,*source,*camera);fixture_cast_count=0;mutation=mode;
    NativeInteractionStopFixture::service(aim,*camera);
    const auto diagnostic=aim.anatomical_shadow_diagnostic(GetTickCount64());
    if(mode==Mutation::none) {
        assert(fixture_cast_count==48 && aim.anatomical_cast_count()==48 && diagnostic.valid && diagnostic.empty_verified_at_query);
        assert(diagnostic.source_body==source->handle && diagnostic.casts[0]==24 && diagnostic.casts[1]==24);
        assert(diagnostic.witnesses[0]==48 && diagnostic.witnesses[1]==48 && diagnostic.lifecycle_revision);
        SynchronizedConsumer consumer{};consumer.anatomical_shadow=diagnostic;std::ostringstream json;write_synchronized(json,consumer,1,0);
        assert(json.str().find("\"authorizes_response\":false")!=std::string::npos);
        assert(json.str().find("\"empty_verified_at_query\":true")!=std::string::npos);
        assert(json.str().find("\"casts\":24")!=std::string::npos);
        aim.clear_anatomical_contact();assert(!aim.anatomical_shadow_diagnostic(GetTickCount64()).valid);
        const auto old_casts=fixture_cast_count;NativeInteractionStopFixture::service(aim,*camera);assert(fixture_cast_count==old_casts);
    } else {
        assert(!diagnostic.valid && fixture_cast_count>=5 && fixture_cast_count<=48);
        if(mode==Mutation::hand)assert(fixture_cast_count==48);
        else assert(fixture_cast_count<48);
    }
    assert(aim.disable());assert(!aim.anatomical_admission() && !aim.anatomical_shadow_diagnostic(GetTickCount64()).valid);
    assert(!aim.publish_anatomical_raw({},GetTickCount64()));
    current_aim=nullptr;current_grab=nullptr;
}
}
int main() {
    run(Mutation::none);
    for(const auto mode:{Mutation::clear,Mutation::republish,Mutation::hand,Mutation::source})run(mode);
    std::cout<<"native anatomical shadow check passed: production service48 casts, Source/EmptyGate proof, clear/stop and midquery invalidations; synthetic callbacks only\n";
}
