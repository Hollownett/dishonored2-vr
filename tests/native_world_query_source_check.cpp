#define NOMINMAX
#include "../src/native_world_trace.hpp"
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <map>
#if defined(_WIN32)
#include <windows.h>
#endif

namespace {
using namespace d2vr;
constexpr std::uintptr_t kBase=0x140000000,kGame=0x200000000,kPlayer=0x201000000,kCamera=0x202000000;
constexpr std::uintptr_t kRenderBody=0x203000000,kController=0x204000000,kCameraController=0x204100000,kPhysics=0x205000000;
constexpr std::uintptr_t kDelegate=0x206000000,kWorld=0x207000000,kPool=0x208000000;
constexpr std::uintptr_t kBody=kPool+2*0xb0;
constexpr std::uint32_t kHandle=0x12000002;
constexpr std::uintptr_t kHitBody=kPool+3*0xb0,kHitUserdata=0x20a100000,kHitComponent=0x20a200000,kHitEntity=0x20a300000;
constexpr std::uint32_t kHitHandle=0x12000003,kHitEntityId=0x00010002;
struct Memory {
    std::map<std::uintptr_t,unsigned char> bytes;
    std::uintptr_t fail_at{},mutate_at{},mutate_target{},raise_at{};
    std::array<unsigned char,8> mutation{};
    std::uintptr_t throw_at{};
    bool operator()(std::uintptr_t at,void* out,std::size_t size) {
        if(at==fail_at)return false;
        for(std::size_t index=0;index<size;++index) {
            const auto found=bytes.find(at+index);if(found==bytes.end())return false;
            static_cast<unsigned char*>(out)[index]=found->second;
        }
        if(at==throw_at)throw 7;
        if(at==mutate_at) {for(std::size_t index=0;index<mutation.size();++index)bytes[mutate_target+index]=mutation[index];mutate_at=0;}
#if defined(_WIN32)
        if(at==raise_at)RaiseException(0xe0424242,0,0,nullptr);
#endif
        return true;
    }
    template<class T> void put(std::uintptr_t at,const T& value) {
        const auto* data=reinterpret_cast<const unsigned char*>(&value);
        for(std::size_t index=0;index<sizeof(value);++index)bytes[at+index]=data[index];
    }
    void erase(std::uintptr_t at,std::size_t size) {for(std::size_t index=0;index<size;++index)bytes.erase(at+index);}
    template<class T> void mutate_after_read(std::uintptr_t read_at,std::uintptr_t write_at,const T& value) {
        assert(sizeof(value)<=mutation.size());mutate_at=read_at;mutate_target=write_at;std::memcpy(mutation.data(),&value,sizeof(value));
    }
};
Memory* active_memory{};
std::uint64_t expected_source_body{};
std::uint32_t expected_query_info{};
unsigned source_casts{};
bool source_hit_enabled{};
bool source_collect_initial{},source_invalid_initial{},source_stop_regular{};
bool source_wrong_initial_source{};
bool source_wrong_regular_source{};
unsigned source_initial_count{};
unsigned source_regular_count{1};
std::uint32_t source_hit_handle{kHitHandle};
enum class Drift {none,handle,world,filter};
Drift source_drift{};
bool read_active(std::uintptr_t address,void* out,std::size_t size) {return active_memory && (*active_memory)(address,out,size);}
Memory fixture() {
    Memory memory;
    memory.put(kBase+0x39a5290,kGame);memory.put(kGame,kBase+0x1c940a8);memory.put(kGame+0x3130b0,kPlayer);
    memory.put(kPlayer,kBase+0x1c5e428);memory.put(kPlayer+0xa8,std::uint32_t{123});
    memory.put(kGame+0x60410,kWorld);memory.put(kGame+0x60280,kGame+0x60280);
    memory.put(kCamera,std::array<std::byte,d2vr::camera_read_size>{});memory.put(kCamera+0x25150,std::array<std::byte,0x200>{});
    for(const auto [address,table]:std::array<std::pair<std::uintptr_t,std::uintptr_t>,10>{
        {{kCamera,0x1c72218},{kCamera+0x20,0x1c722f8},{kRenderBody,0x1c5a8d8},{kRenderBody+0x20,0x1c5a9b0},
         {kCameraController,0x1c72918},{kController,0x1ad26a0},{kPhysics,0x1c73268},{kPhysics+0x20,0x1c73340},
         {kPhysics+0x30,0x1c73378},{kDelegate+0x160,0x1b02768}}})
        memory.put(address,kBase+table);
    for(const auto address:{kCamera,kRenderBody,kCameraController,kController,kPhysics}) {
        memory.put(address+0x10,kPlayer);memory.put(address+0x18,rig::Identity{5,10});
    }
    memory.put(kCamera+0x25158,kCameraController);memory.put(kCamera+0x25160,kPhysics);memory.put(kCamera+0x25170,kRenderBody);
    constexpr std::uintptr_t components=0x20a000000;
    memory.put(kPlayer+0x268,rig::List{components,4,4u<<8});memory.put(components,kCamera);
    memory.put(components+8,kRenderBody);memory.put(components+16,kCameraController);memory.put(components+24,kPhysics);
    memory.put(kPhysics+0x48,kDelegate);memory.put(kDelegate+0x168,kPhysics);memory.put(kDelegate+0x120,kPlayer+0x300);
    memory.put(kDelegate+0x1e0,kDelegate);memory.put(kDelegate+0xd0,1.5f);memory.put(kDelegate+0xbc,0.f);memory.put(kDelegate+0x40,0.f);
    memory.put(kPlayer+0x30c,std::array<float,9>{1,0,0,0,1,0,0,0,1});memory.put(kCameraController+0xd0,std::array<float,2>{.1f,-.2f});
    memory.put(kDelegate+0x148,kController);memory.put(kController+0x140,kGame+0x60280);memory.put(kController+0x40,kWorld);
    memory.put(kController+0x38,kHandle);memory.put(kController+0x3c,std::uint32_t{0x120});
    memory.put(kWorld+0x28,kPool);memory.put(kWorld+0x30,std::uint32_t{8});memory.put(kWorld+NativeWorldTrace::shape_tag_codec_offset,std::uint64_t{0xabcdef0});
    memory.put(kBody+0x44,std::uint8_t{3});memory.put(kBody+0x70,kHandle);memory.put(kBody+0x78,std::uint32_t{1});
    memory.put(kBody+0x6c,std::uint32_t{0x220});
    for(unsigned index=0;index<NativeInitialContactCollector::native_slots.size();++index)
        memory.put(kBase+NativeInitialContactCollector::native_vtable_rva+index*8,kBase+NativeInitialContactCollector::native_slots[index]);
    memory.put(kBase+NativeInitialContactCollector::native_slots[0],NativeInitialContactCollector::getter_code);
    memory.put(kBase+NativeInitialContactCollector::native_slots[4],NativeInitialContactCollector::noop_code);
    return memory;
}
void install_hit_body(Memory& memory,std::uint32_t handle,std::uintptr_t userdata=kHitUserdata) {
    const auto index=handle&0xffffffu;const auto body=kPool+static_cast<std::uintptr_t>(index)*0xb0;
    memory.put(body+0x44,std::uint8_t{3});memory.put(body+0x70,handle);memory.put(body+0x78,std::uint32_t{2});
    memory.put(body+0x90,userdata);
    if(userdata) {
        memory.put(userdata+8,kHitComponent);memory.put(kHitComponent+0x10,kHitEntity);
        memory.put(kHitEntity+0xa8,kHitEntityId);memory.put(kGame+0x3130b0+8,kHitEntity);
    }
}
void* __fastcall source_construct(void* storage,const float*,float radius) {
    auto* bytes=static_cast<std::byte*>(storage);std::memcpy(bytes+0x28,&radius,sizeof(radius));return storage;
}
void __fastcall source_cast(void* cast_world,const NativeCastRequest* request,const float*,NativeClosestCollector* collector,std::uintptr_t initial_address) {
    ++source_casts;assert(reinterpret_cast<std::uintptr_t>(cast_world)==kWorld && (initial_address!=0)==source_collect_initial);
    assert(request->source_body==expected_source_body && request->query_id==expected_query_info);
    const auto* filter=reinterpret_cast<const NativeCastFilter*>(request->filter);
    assert(filter->layer_mask==kWorldOccluderLayers && filter->required_bits==kWorldOccluderRequired);
    if(source_hit_enabled) {
        if(source_collect_initial) {
            NativeInitialContactRecord record{};
            const std::array<float,3> point{.5f,0,0},normal{-1.f,0,0};const float fraction=.5f;
            const auto regular_source=source_wrong_regular_source?kHandle+0x01000000u:kHandle;
            const std::uint16_t material=7;const std::uint32_t shape_key=0x1234;
            std::memcpy(record.bytes.data(),point.data(),12);std::memcpy(record.bytes.data()+0x10,normal.data(),12);
            std::memcpy(record.bytes.data()+0x20,&fraction,4);std::memcpy(record.bytes.data()+0x28,&regular_source,4);
            std::memcpy(record.bytes.data()+0x48,&source_hit_handle,4);std::memcpy(record.bytes.data()+0x4c,&material,2);
            std::memcpy(record.bytes.data()+0x50,&shape_key,4);
            auto* regular=reinterpret_cast<NativeRegularContactCollector*>(collector);
            const auto* table=reinterpret_cast<const std::uintptr_t*>(regular->native()->vtable);
            const auto add=reinterpret_cast<void(__fastcall*)(NativeRegularContactCollector*,const void*)>(table[3]);
            for(unsigned index=0;index<source_regular_count;++index)add(regular,record.bytes.data());
        } else {
            collector->hit_count=1;collector->fraction=.5f;collector->position[0]=.5f;collector->normal[0]=-1.f;
            collector->body=source_hit_handle;collector->body_key=7;collector->shape_key=0x1234;
        }
    }
    if(source_collect_initial) {
        auto* initial=reinterpret_cast<NativeInitialContactCollector*>(initial_address);assert(initial->active);
        NativeInitialContactRecord record{};
        const std::array<float,3> point{.5f,0,0},normal{-1.f,0,0};const float separation=source_invalid_initial?.1f:-.02f;
        const std::uint32_t shape_key=0x1234;
        const auto initial_source=source_wrong_initial_source?kHandle+0x01000000u:kHandle;
        std::memcpy(record.bytes.data(),point.data(),12);std::memcpy(record.bytes.data()+0x10,normal.data(),12);
        std::memcpy(record.bytes.data()+0x20,&separation,4);std::memcpy(record.bytes.data()+0x28,&initial_source,4);
        std::memcpy(record.bytes.data()+0x48,&source_hit_handle,4);std::memcpy(record.bytes.data()+0x50,&shape_key,4);
        const auto* table=reinterpret_cast<const std::uintptr_t*>(initial->vtable);
        const auto add=reinterpret_cast<void(__fastcall*)(NativeInitialContactCollector*,const void*)>(table[3]);
        for(unsigned index=0;index<source_initial_count;++index)add(initial,record.bytes.data());
        if(source_stop_regular)reinterpret_cast<NativeRegularContactCollector*>(collector)->native()->stop=1;
    }
    if(source_drift==Drift::handle)active_memory->put(kController+0x38,std::uint32_t{0x13000002});
    if(source_drift==Drift::world)active_memory->put(kGame+0x60410,std::uintptr_t{0x20f000000});
    if(source_drift==Drift::filter)active_memory->put(kController+0x3c,expected_query_info+1);
}
WorldTraceQueryResult source_query(Memory& memory,const NativeWorldQuerySource& source) {
    active_memory=&memory;expected_source_body=source.body;expected_query_info=source.query_filter_info;
    return NativeWorldTrace::sphere_query_checked(kBase,read_active,source_cast,source_construct,
        kWorld,{0,0,0},{1,0,0},.025,kWorldOccluderLayers,kWorldOccluderRequired,&source,source_collect_initial);
}
}

int main() {
    auto memory=fixture();
    const auto camera_snapshot=native_player_camera(memory,kBase,kCamera);assert(camera_snapshot);
    const auto inspected=native_world_query_source_detail::inspect(memory,kBase,kCamera,kWorld);
    assert(inspected);
    auto source=native_world_query_source(memory,kBase,kCamera,kWorld);
    assert(source);
    assert(source->valid());
    assert(source->body==kBody && source->handle==kHandle && source->query_filter_info==0x120 &&
        source->body_filter_info==0x220 && source->body_count==8);
    assert(source->component_list_data==0x20a000000 && source->component_list_count==4 && source->component_list_capacity==4);
    auto unrelated_empty_component=memory;
    unrelated_empty_component.put(kPlayer+0x268,rig::List{0x20a000000,5,5u<<8});
    unrelated_empty_component.put(0x20a000000+32,std::uintptr_t{});
    assert(native_world_query_source(unrelated_empty_component,kBase,kCamera,kWorld));
    assert(native_world_query_source_current(memory,*source));
    source_casts=0;source_drift=Drift::none;
    const auto native_query=source_query(memory,*source);
    assert(native_query.complete && !native_query.hit && source_casts==1);
    install_hit_body(memory,kHitHandle);source_hit_enabled=true;source_hit_handle=kHitHandle;
    const auto resolved_hit=source_query(memory,*source);
    assert(resolved_hit.complete && resolved_hit.hit && resolved_hit.hit->body_handle==kHitHandle && resolved_hit.hit->body==3 &&
        resolved_hit.hit->entity==kHitEntity && resolved_hit.hit->material==7 && !resolved_hit.hit->static_support);
    source_collect_initial=true;source_initial_count=2;
    const auto initial_hit=source_query(memory,*source);
    assert(initial_hit.complete && initial_hit.initial && initial_hit.initial->count==2 && initial_hit.hit);
    assert(initial_hit.regular && initial_hit.regular->count==1 && initial_hit.regular->hits[0].shape_key==0x1234 &&
        initial_hit.regular->hits[0].material==7 && initial_hit.regular->hits[0].fraction==.5);
    assert(initial_hit.initial->hits[0].entity==kHitEntity && initial_hit.initial->hits[0].contact.body==kHitHandle &&
        initial_hit.initial->hits[0].contact.source_body==kHandle && initial_hit.initial->hits[0].contact.shape_key==0x1234 &&
        initial_hit.initial->hits[0].contact.signed_separation==-.02f && initial_hit.initial->hits[0].contact.normal.x==-1);
    {   // final review P1-1: a hand body's own contacts (initial and regular) are skipped and counted; they never fail the whole query (which
        // blinded full-hand contact, anatomical contact and door ingress while a body existed) and the rest stays the answer
        const auto skipped_before=cast_excluded_hits.load();cast_excluded_bodies[1].store(kHitHandle);
        const auto skipped=source_query(memory,*source);
        cast_excluded_bodies[1].store(0xffffffffu);
        assert(skipped.complete && skipped.initial && skipped.initial->count==0 && skipped.regular && skipped.regular->count==0 && !skipped.hit);
        assert(cast_excluded_hits.load()==skipped_before+3);
        const auto again=source_query(memory,*source);   // nothing armed: the same contacts are the answer again
        assert(again.complete && again.initial->count==2 && again.regular->count==1 && again.hit && cast_excluded_hits.load()==skipped_before+3);
    }
    source_hit_enabled=false;source_initial_count=0;
    const auto empty_initial=source_query(memory,*source);
    assert(empty_initial.complete && !empty_initial.hit && empty_initial.initial && !empty_initial.initial->count);
    source_initial_count=33;const auto overflow_initial=source_query(memory,*source);
    assert(!overflow_initial.complete && !overflow_initial.hit && !overflow_initial.initial);
    source_initial_count=1;source_invalid_initial=true;assert(!source_query(memory,*source).complete);source_invalid_initial=false;
    source_hit_enabled=true;source_regular_count=33;assert(!source_query(memory,*source).complete);source_regular_count=1;
    source_wrong_regular_source=true;assert(!source_query(memory,*source).complete);source_wrong_regular_source=false;
    source_stop_regular=true;assert(!source_query(memory,*source).complete);source_stop_regular=false;
    source_wrong_initial_source=true;assert(!source_query(memory,*source).complete);source_wrong_initial_source=false;
    source_hit_handle=kHandle;assert(!source_query(memory,*source).complete);source_hit_handle=kHitHandle;
    auto stale_initial=memory;stale_initial.put(kHitBody+0x70,kHitHandle+0x01000000u);
    const auto stale_initial_query=source_query(stale_initial,*source);assert(!stale_initial_query.complete && !stale_initial_query.initial);
    auto failed_initial=memory;failed_initial.fail_at=kHitUserdata+8;assert(!source_query(failed_initial,*source).complete);
    auto wrong_initial_profile=memory;wrong_initial_profile.put(kBase+NativeInitialContactCollector::native_slots[0],std::uint64_t{});
    const auto before_profile_failure=source_casts;assert(!source_query(wrong_initial_profile,*source).complete && source_casts==before_profile_failure);
    const auto before_unowned_initial=source_casts;
    assert(!NativeWorldTrace::sphere_query_checked(kBase,read_active,source_cast,source_construct,kWorld,{0,0,0},{1,0,0},.025,
        kWorldOccluderLayers,kWorldOccluderRequired,nullptr,true).complete && source_casts==before_unowned_initial);
    source_collect_initial=false;source_initial_count=0;source_hit_enabled=true;
    auto anonymous_memory=fixture();install_hit_body(anonymous_memory,kHitHandle,0);
    const auto anonymous_source=native_world_query_source(anonymous_memory,kBase,kCamera,kWorld);assert(anonymous_source);
    const auto anonymous_hit=source_query(anonymous_memory,*anonymous_source);
    assert(anonymous_hit.complete && anonymous_hit.hit && anonymous_hit.hit->entity==0 && !anonymous_hit.hit->static_support);
    const auto rejects_hit=[&](Memory changed,std::uint32_t returned_handle) {
        auto changed_source=native_world_query_source(changed,kBase,kCamera,kWorld);assert(changed_source);
        source_hit_handle=returned_handle;const auto query=source_query(changed,*changed_source);
        assert(!query.complete && !query.hit);
    };
    auto stale_hit=fixture();install_hit_body(stale_hit,kHitHandle+0x01000000u);rejects_hit(stale_hit,kHitHandle);
    auto out_of_bounds_hit=fixture();rejects_hit(out_of_bounds_hit,0x12000008u);
    auto dead_hit=fixture();install_hit_body(dead_hit,kHitHandle);dead_hit.put(kHitBody+0x44,std::uint8_t{0});rejects_hit(dead_hit,kHitHandle);
    auto failed_chain=fixture();install_hit_body(failed_chain,kHitHandle);failed_chain.fail_at=kHitUserdata+8;rejects_hit(failed_chain,kHitHandle);
    auto changed_chain=fixture();install_hit_body(changed_chain,kHitHandle);changed_chain.mutate_after_read(kHitBody+0x90,kHitBody+0x90,std::uintptr_t{0});rejects_hit(changed_chain,kHitHandle);
    auto body_drift=fixture();install_hit_body(body_drift,kHitHandle);body_drift.mutate_after_read(kHitBody+0x70,kHitBody+0x70,std::uint32_t{kHitHandle+0x01000000u});rejects_hit(body_drift,kHitHandle);
    auto count_drift=fixture();install_hit_body(count_drift,kHitHandle);count_drift.mutate_after_read(kHitBody+0x90,kWorld+0x30,std::uint32_t{7});rejects_hit(count_drift,kHitHandle);
    auto bad_entity_registry=fixture();install_hit_body(bad_entity_registry,kHitHandle);bad_entity_registry.put(kGame+0x3130b0+8,std::uintptr_t{0});rejects_hit(bad_entity_registry,kHitHandle);
    source_hit_enabled=false;source_hit_handle=kHitHandle;
    auto invalid_source=*source;invalid_source.query_filter_info+=1;const auto before_invalid=source_casts;
    assert(!source_query(memory,invalid_source).complete && source_casts==before_invalid);
    for(const auto drift:{Drift::handle,Drift::world,Drift::filter}) {
        auto drift_memory=fixture();auto drift_source=native_world_query_source(drift_memory,kBase,kCamera,kWorld);assert(drift_source);
        source_drift=drift;const auto before_drift=source_casts;
        assert(!source_query(drift_memory,*drift_source).complete && source_casts==before_drift+1);
    }
    source_drift=Drift::none;active_memory=nullptr;
    auto same_index_new_generation=memory;const std::uint32_t new_handle=0x13000002;
    same_index_new_generation.put(kController+0x38,new_handle);same_index_new_generation.put(kBody+0x70,new_handle);
    auto newer=native_world_query_source(same_index_new_generation,kBase,kCamera,kWorld);
    assert(newer && newer->body==source->body && newer->handle==new_handle && !native_world_query_source_current(same_index_new_generation,*source));
    const auto rejects=[&](std::uintptr_t at,const auto& value) {
        auto changed=memory;changed.put(at,value);assert(!native_world_query_source(changed,kBase,kCamera,kWorld));
    };
    rejects(kController,kBase+0x1c72918);
    rejects(kDelegate+0x148,std::uintptr_t{0x20b000000});
    rejects(kController+0x140,std::uintptr_t{0x20c000000});
    rejects(kController+0x40,std::uintptr_t{0x20d000000});
    rejects(kCamera+0x25158,std::uintptr_t{0x20b000010});
    rejects(kCamera+0x25160,std::uintptr_t{0x20b000018});
    rejects(kDelegate+0x168,std::uintptr_t{0x20b000020});
    rejects(kDelegate+0x120,std::uintptr_t{0x20b000028});
    rejects(kPhysics+0x48,std::uintptr_t{0x20b000030});
    rejects(kGame+0x60410,std::uintptr_t{0x20e000000});
    rejects(kGame+0x3130b0,std::uintptr_t{0x20b000038});
    rejects(kPlayer+0xa8,std::uint32_t{0x00020001});
    auto missing_camera=memory;missing_camera.put(kPlayer+0x268,rig::List{0x20a000000,3,4u<<8});
    assert(!native_world_query_source(missing_camera,kBase,kCamera,kWorld));
    auto missing_body=memory;missing_body.put(0x20a000000,kCamera);missing_body.put(0x20a000008,kCameraController);
    missing_body.put(0x20a000010,kPhysics);missing_body.put(kPlayer+0x268,rig::List{0x20a000000,3,4u<<8});
    assert(!native_world_query_source(missing_body,kBase,kCamera,kWorld));
    auto duplicate_camera=memory;duplicate_camera.put(0x20a000018,kCamera);
    assert(!native_world_query_source(duplicate_camera,kBase,kCamera,kWorld));
    auto duplicate_physics=memory;duplicate_physics.put(0x20a000008,kPhysics);
    assert(!native_world_query_source(duplicate_physics,kBase,kCamera,kWorld));
    auto recycled_components=memory;recycled_components.put(kPlayer+0x268,rig::List{0x20b000000,4,4u<<8});
    assert(!native_world_query_source(recycled_components,kBase,kCamera,kWorld));
    auto failed_component_read=memory;failed_component_read.fail_at=0x20a000010;
    assert(!native_world_query_source(failed_component_read,kBase,kCamera,kWorld));
    auto racing_components=memory;const std::uint64_t changed_component_list=(static_cast<std::uint64_t>(4u<<8)<<32)|5u;
    racing_components.mutate_after_read(kPlayer+0x268,kPlayer+0x270,changed_component_list);
    assert(!native_world_query_source(racing_components,kBase,kCamera,kWorld));
    auto changed_component_count=memory;changed_component_count.put(kPlayer+0x268,rig::List{0x20a000000,5,5u<<8});
    assert(!native_world_query_source_current(changed_component_count,*source));
    auto changed_component_data=memory;changed_component_data.put(kPlayer+0x268,rig::List{0x20b000000,4,4u<<8});
    assert(!native_world_query_source_current(changed_component_data,*source));
    auto changed_identity=memory;changed_identity.put(kCamera+0x18,rig::Identity{6,10});
    auto camera_generation=native_world_query_source(changed_identity,kBase,kCamera,kWorld);
    assert(camera_generation && !native_world_query_source_current(changed_identity,*source));
    auto changed_physics_identity=memory;changed_physics_identity.put(kPhysics+0x18,rig::Identity{6,10});
    auto physics_generation=native_world_query_source(changed_physics_identity,kBase,kCamera,kWorld);
    assert(physics_generation && !native_world_query_source_current(changed_physics_identity,*source));
    auto changed_query_filter=memory;changed_query_filter.put(kController+0x3c,std::uint32_t{0x121});
    assert(native_world_query_source(changed_query_filter,kBase,kCamera,kWorld));
    assert(!native_world_query_source_current(changed_query_filter,*source));
    auto changed_body_filter=memory;changed_body_filter.put(kBody+0x6c,std::uint32_t{0x221});
    assert(native_world_query_source(changed_body_filter,kBase,kCamera,kWorld));
    assert(!native_world_query_source_current(changed_body_filter,*source));
    auto changed_pool=memory;changed_pool.put(kWorld+0x28,std::uintptr_t{0x20f000000});
    assert(!native_world_query_source_current(changed_pool,*source));
    auto changed_count=memory;changed_count.put(kWorld+0x30,std::uint32_t{9});
    assert(!native_world_query_source_current(changed_count,*source));
    auto changed_flags=memory;changed_flags.put(kBody+0x44,std::uint8_t{1});
    assert(!native_world_query_source_current(changed_flags,*source));
    auto changed_broadphase=memory;changed_broadphase.put(kBody+0x78,std::uint32_t{2});
    assert(!native_world_query_source_current(changed_broadphase,*source));
    auto bad_index=memory;bad_index.put(kController+0x38,std::uint32_t{0x12ffffff});
    assert(!native_world_query_source(bad_index,kBase,kCamera,kWorld));
    auto out_of_count=memory;out_of_count.put(kController+0x38,std::uint32_t{0x12000008});
    assert(!native_world_query_source(out_of_count,kBase,kCamera,kWorld));
    auto oversized_count=memory;oversized_count.put(kWorld+0x30,std::uint32_t{0x1000001});
    assert(!native_world_query_source(oversized_count,kBase,kCamera,kWorld));
    auto invalid_broadphase=memory;invalid_broadphase.put(kBody+0x78,std::uint32_t{0xffffffff});
    assert(!native_world_query_source(invalid_broadphase,kBase,kCamera,kWorld));
    auto failed_read=memory;failed_read.fail_at=kBody+0x70;
    assert(!native_world_query_source(failed_read,kBase,kCamera,kWorld));
    auto throwing_read=memory;throwing_read.throw_at=kBody+0x70;
    assert(!native_world_query_source(throwing_read,kBase,kCamera,kWorld));
    auto racing_read=memory;racing_read.mutate_after_read(kBody+0x70,kBody+0x70,new_handle);
    assert(!native_world_query_source(racing_read,kBase,kCamera,kWorld));
    auto changed_spawn=memory;changed_spawn.put(kPlayer+0xa8,std::uint32_t{0x00010002});
    assert(!native_world_query_source_current(changed_spawn,*source));
#if defined(_WIN32)
    auto structured_failure=memory;structured_failure.raise_at=kBase+0x39a5290;
    assert(!native_world_query_source(structured_failure,kBase,kCamera,kWorld));
#endif
    std::cout<<"native world-query source lease, component membership, full body identity, ownership and revalidation passed\n";
}
