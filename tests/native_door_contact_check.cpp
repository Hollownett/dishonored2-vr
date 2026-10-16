#include "../src/native_door_contact.hpp"
#include <cassert>
#include <cmath>
#include <limits>

using namespace d2vr;

namespace {
constexpr std::uint32_t source_handle=0x12000002u;
constexpr std::uintptr_t door_entity=0x12345000u;

NativeInitialContact make_initial(float separation=-.04f,std::uint32_t body=0x12000003u,std::uint32_t key=0xffffffffu,
                                  Vec3 normal={0,1,0},std::uint32_t source=source_handle) {
    NativeInitialContact contact{};
    contact.point={1,2,3};contact.normal=normal;contact.signed_separation=separation;
    contact.source_body=source;contact.body=body;contact.shape_key=key;contact.normal_valid=true;
    return contact;
}

WorldTraceHit make_regular(std::uint32_t body=0x12000003u,std::uint32_t key=0xffffffffu,
                           Vec3 normal={0,1,0},double fraction=0.0,std::uintptr_t entity=door_entity) {
    WorldTraceHit hit{};
    hit.point={1,2,3};hit.normal=normal;hit.fraction=fraction;hit.entity=entity;
    hit.body=body&0xffffffu;hit.body_handle=body;hit.shape_key=key;hit.normal_valid=true;
    return hit;
}

WorldTraceQueryResult make_query() {
    WorldTraceQueryResult query{};query.complete=true;query.initial.emplace();query.regular.emplace();return query;
}

void add_pair(WorldTraceQueryResult& query,const NativeInitialContact& initial,std::uintptr_t entity=door_entity,
              const WorldTraceHit* regular_override=nullptr) {
    auto& initial_batch=*query.initial;
    initial_batch.hits[initial_batch.count++]={initial,entity};
    auto& regular_batch=*query.regular;
    regular_batch.hits[regular_batch.count++]=regular_override?*regular_override:
        make_regular(initial.body,initial.shape_key,initial.normal,0.0,entity);
}

void test_target_selection_and_copied_surface() {
    auto query=make_query();
    add_pair(query,make_initial(-.04f));
    const auto selected=NativeDoorContact::select(query,source_handle,door_entity);
    assert(selected && selected->entity==door_entity && selected->full_body==0x12000003u &&
           selected->shape_key==0xffffffffu && std::abs(selected->depth-.04)<1e-7 && selected->normal.y==1.0);
    query.initial->hits[0].contact.point={90,90,90};
    assert(selected->point.x==1.0 && selected->point.y==2.0);
}

void test_empty_and_missing_batches() {
    auto query=make_query();
    assert(!NativeDoorContact::select(query,source_handle,door_entity));
    query.initial.reset();
    assert(!NativeDoorContact::select(query,source_handle,door_entity));
    query=make_query();query.complete=false;
    assert(!NativeDoorContact::select(query,source_handle,door_entity));
    query=make_query();add_pair(query,make_initial());
    assert(!NativeDoorContact::select(query,source_handle,0));
}

void test_ambiguous_and_divergent_targets() {
    auto query=make_query();
    add_pair(query,make_initial(-.02f,0x12000003u,4u));
    add_pair(query,make_initial(-.03f,0x12000003u,5u));
    assert(!NativeDoorContact::select(query,source_handle,door_entity));
    query=make_query();
    add_pair(query,make_initial(-.02f,0x12000003u,4u,{0,1,0}));
    add_pair(query,make_initial(-.03f,0x12000003u,4u,{.3f,.9539392f,0}));
    assert(!NativeDoorContact::select(query,source_handle,door_entity));
    query=make_query();
    add_pair(query,make_initial(-.02f,0x12000003u,4u));
    add_pair(query,make_initial(-.03f,0x13000003u,4u));
    assert(!NativeDoorContact::select(query,source_handle,door_entity));
}

void test_same_shape_planes_choose_deepest() {
    auto query=make_query();
    add_pair(query,make_initial(-.02f,0x12000003u,9u));
    add_pair(query,make_initial(-.05f,0x12000003u,9u,{.01f,.99995f,0}));
    const auto selected=NativeDoorContact::select(query,source_handle,door_entity);
    assert(selected && std::abs(selected->depth-.05)<1e-7 && selected->point.x==1.0);
}

void test_other_overlap_rejects_door_contact() {
    auto query=make_query();
    add_pair(query,make_initial(-.02f,0x12000003u,4u));
    add_pair(query,make_initial(-.01f,0x12000004u,8u),0x99887700u);
    assert(!NativeDoorContact::select(query,source_handle,door_entity));
    query=make_query();
    add_pair(query,make_initial(-.02f,0x12000003u,4u));
    add_pair(query,make_initial(0.f,0x12000004u,8u),0x99887700u);
    assert(NativeDoorContact::select(query,source_handle,door_entity));
}

void test_bad_initial_records_fail_whole_batch() {
    auto query=make_query();add_pair(query,make_initial());
    query.initial->hits[0].contact.signed_separation=.001f;
    assert(!NativeDoorContact::select(query,source_handle,door_entity));
    query=make_query();add_pair(query,make_initial());
    query.initial->hits[0].contact.source_body=0x12000009u;
    assert(!NativeDoorContact::select(query,source_handle,door_entity));
    query=make_query();add_pair(query,make_initial());
    query.initial->hits[0].contact.body=source_handle;
    assert(!NativeDoorContact::select(query,source_handle,door_entity));
    query=make_query();add_pair(query,make_initial());
    query.initial->hits[0].contact.normal_valid=false;
    assert(!NativeDoorContact::select(query,source_handle,door_entity));
    query=make_query();add_pair(query,make_initial());
    query.initial->hits[0].contact.point.x=std::numeric_limits<double>::quiet_NaN();
    assert(!NativeDoorContact::select(query,source_handle,door_entity));
    query=make_query();add_pair(query,make_initial());
    query.initial->hits[0].contact.signed_separation=std::numeric_limits<float>::infinity();
    assert(!NativeDoorContact::select(query,source_handle,door_entity));
    query=make_query();add_pair(query,make_initial(-.09f));
    assert(!NativeDoorContact::select(query,source_handle,door_entity));
    query=make_query();add_pair(query,make_initial(0.f));
    assert(!NativeDoorContact::select(query,source_handle,door_entity));
    query=make_query();add_pair(query,make_initial());
    query.initial->count=33;
    assert(!NativeDoorContact::select(query,source_handle,door_entity));
}

void test_bad_regular_records_fail_whole_batch() {
    auto query=make_query();
    const auto invalid_normal=make_regular();
    auto override=invalid_normal;override.normal_valid=false;override.normal={0,0,0};
    add_pair(query,make_initial(),door_entity,&override);
    assert(NativeDoorContact::select(query,source_handle,door_entity));
    override.fraction=.1;query.regular->hits[0]=override;
    assert(!NativeDoorContact::select(query,source_handle,door_entity));
    query=make_query();add_pair(query,make_initial());
    query.regular->hits[0].fraction=std::numeric_limits<double>::quiet_NaN();
    assert(!NativeDoorContact::select(query,source_handle,door_entity));
    query=make_query();add_pair(query,make_initial());
    query.regular->hits[0].body_handle=source_handle;
    assert(!NativeDoorContact::select(query,source_handle,door_entity));
    query=make_query();add_pair(query,make_initial());
    query.regular->hits[0].body_handle=0x12000009u;
    assert(!NativeDoorContact::select(query,source_handle,door_entity));
    query=make_query();add_pair(query,make_initial());
    query.regular->hits[0].body_handle=0xffffffffu;
    assert(!NativeDoorContact::select(query,source_handle,door_entity));
    query=make_query();add_pair(query,make_initial());
    query.regular->count=33;
    assert(!NativeDoorContact::select(query,source_handle,door_entity));
    query=make_query();add_pair(query,make_initial());
    query.regular->hits[0].shape_key=6;
    assert(!NativeDoorContact::select(query,source_handle,door_entity));
}

void test_recycled_full_id_is_not_index_alias() {
    constexpr std::uint32_t old_handle=0x11000003u;
    constexpr std::uint32_t current_handle=0x12000003u;
    auto query=make_query();
    add_pair(query,make_initial(-.03f,current_handle,0xffffffffu,{0,1,0},old_handle));
    const auto selected=NativeDoorContact::select(query,old_handle,door_entity);
    assert(selected && selected->full_body==current_handle);
}

void test_reserved_body_index_rejected() {
    auto query=make_query();
    add_pair(query,make_initial(-.03f,0x12000003u,0xffffffffu,{0,1,0},0x12ffffffu));
    assert(!NativeDoorContact::select(query,0x12ffffffu,door_entity));
    assert(!NativeDoorContact::select(query,0xffffffffu,door_entity));
    query=make_query();
    add_pair(query,make_initial(-.03f,0x12ffffffu,0xffffffffu));
    assert(!NativeDoorContact::select(query,source_handle,door_entity));
    query=make_query();add_pair(query,make_initial());
    query.regular->hits[0].body_handle=0x12ffffffu;
    query.regular->hits[0].body=0xffffffu;
    assert(!NativeDoorContact::select(query,source_handle,door_entity));
}
}

int main() {
    test_target_selection_and_copied_surface();
    test_empty_and_missing_batches();
    test_ambiguous_and_divergent_targets();
    test_same_shape_planes_choose_deepest();
    test_other_overlap_rejects_door_contact();
    test_bad_initial_records_fail_whole_batch();
    test_bad_regular_records_fail_whole_batch();
    test_recycled_full_id_is_not_index_alias();
    test_reserved_body_index_rejected();
    return 0;
}
