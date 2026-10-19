#include "../src/native_world_trace.hpp"
#include <cstdio>
#include <cstdlib>
#include <map>
#include <vector>
using namespace d2vr;
#define CHECK(x) do{if(!(x)){std::printf("FAIL line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
constexpr std::uintptr_t base=0x140000000;
std::map<std::uintptr_t,std::vector<unsigned char>> memory;
bool corrupt_cast{},fail_construct{};
float fixture_fraction=.25f;
unsigned fixture_hit_count=1;
bool read_mem(std::uintptr_t at,void* out,std::size_t size) {
    for(const auto& [start,bytes]:memory) if(at>=start && at+size<=start+bytes.size()) {std::memcpy(out,bytes.data()+(at-start),size);return true;}
    return false;
}
void put(std::uintptr_t at,const void* p,std::size_t n) {const auto* b=static_cast<const unsigned char*>(p);memory[at]=std::vector<unsigned char>(b,b+n);}
void put64(std::uintptr_t at,std::uint64_t v) {put(at,&v,8);}
void* __fastcall fake_construct(void* out,const float* centre,float radius) {
    CHECK(reinterpret_cast<std::uintptr_t>(out)%16==0 && reinterpret_cast<std::uintptr_t>(centre)%16==0);
    if(fail_construct)return nullptr;
    const std::uint64_t vt=base+NativeWorldTrace::sphere_vtable_rva;const std::uint32_t ref=0x1ffff;
    auto* b=static_cast<std::byte*>(out);std::memcpy(b,&vt,8);std::memcpy(b+0x10,&ref,4);std::memcpy(b+0x28,&radius,4);return out;
}
unsigned casts{};NativeCastRequest seen{};NativeCastFilter seen_filter{};float seen_radius{};
void __fastcall fake_cast(void* world,const NativeCastRequest* r,const float* rot,NativeClosestCollector* c,std::uintptr_t zero) {
    ++casts;CHECK(world && zero==0 && reinterpret_cast<std::uintptr_t>(r)%16==0 && reinterpret_cast<std::uintptr_t>(c)%16==0);
    CHECK(c->vtable==base+NativeWorldTrace::collector_vtable_rva && c->fraction==std::numeric_limits<float>::max() && c->body==0xffffff && !c->hit_count);
    for(unsigned i=0;i<12;++i) CHECK(rot[i]==native_cast_identity[i]);
    seen=*r;std::memcpy(&seen_filter,reinterpret_cast<const void*>(r->filter),sizeof(seen_filter));
    std::memcpy(&seen_radius,reinterpret_cast<const std::byte*>(r->shape)+0x28,4);
    if(corrupt_cast) return;
    // Wall at x = 1.0 hit by a sweep along +x: stop at fraction 0.5 for from.x=0.5? caller picks numbers.
    c->hit_count=fixture_hit_count;c->fraction=fixture_fraction;c->position[0]=1;c->position[1]=0;c->position[2]=0;c->normal[0]=-1;c->body=0x00123456;
}
void test_pure() {
    CHECK(!make_cast_request({0,0,0},{0,0,0},1,1,1));            // zero sweep
    CHECK(!make_cast_request({0,0,0},{1,0,0},0,1,1));
    CHECK(!make_cast_request({0,0,0},{NAN,0,0},1,1,1));
    auto r=make_cast_request({1,2,3},{1,2,-1},7,8,9);
    CHECK(r && r->kind==2 && r->group==0xffff && r->query_id==0 && r->delta[3]==1.f && r->tolerance==0.001f);
    CHECK(r->delta[0]==0 && r->delta[1]==0 && r->delta[2]==-4.f);
    CHECK(r->inverse_delta[0]==std::numeric_limits<float>::max() && r->inverse_delta[2]==-0.25f);
    CHECK(r->flags==(0x3f000000u|1u|2u));                         // x,y deltas are zero (>=0), z negative
    std::uint32_t neg_zero_bits=0x80000000;float nz;std::memcpy(&nz,&neg_zero_bits,4);CHECK(native_delta_clean(nz)==0 && !std::signbit(native_delta_clean(nz)));
    auto c=make_closest_collector(base+0x1ad5010);
    std::uint32_t lim;std::memcpy(&lim,&c.limit[3],4);CHECK(lim==0x5f7ffff0);
    // No hit.
    CHECK(!convert_world_hit(c,{0,0,0},{1,0,0},5));
    // Hit: normal faces against the sweep even if the native one points along it.
    c.hit_count=1;c.fraction=0.5f;c.position[0]=.5f;c.normal[0]=1;c.body=0x42;
    auto h=convert_world_hit(c,{0,0,0},{1,0,0},0x1234);
    CHECK(h && h->fraction==0.5 && h->normal.x==-1 && h->centre.x==0.5 && h->entity==0x1234 && h->body==0x42 && !h->initial_overlap);
    c.body=0x9a000042;h=convert_world_hit(c,{0,0,0},{1,0,0},0x1234);
    CHECK(h && h->body==0x42 && h->body_handle==0x9a000042 && h->entity==0x1234);
    auto cc=to_collision_hit(*h);CHECK(cc.fraction==0.5 && cc.entity==0x1234 && cc.normal.x==-1 && cc.point.x==.5);
    // Initial overlap with a junk normal falls back to the reversed sweep; unknown body has no entity.
    c.fraction=0;c.normal[0]=0;c.body=0xffffff;
    h=convert_world_hit(c,{0,0,0},{0,2,0},0x1234);
    CHECK(h && h->initial_overlap && !h->normal_valid && h->normal.y==-1 && h->entity==0);
    c.fraction=0;c.position[0]=.075f;c.normal[0]=1;c.body=0x42;
    h=convert_world_hit(c,{.09,0,0},{.094,0,0},0x1234);
    CHECK(h && h->initial_overlap && h->normal_valid && h->normal.x==1 && h->centre.x==.09);
    cc=to_collision_hit(*h);CHECK(cc.normal.x==1 && cc.fraction==0);
    // The hand clamp reads the same overlap (WeaponCollision is blade-only since physical hands Task 5): the valid overlap normal
    // and the initial-overlap flag reach it unchanged, so it keeps no plane from it and counts it toward the overlap snap.
    const auto clamp_hit=to_clamp_hit(*h);
    CHECK(clamp_hit.initial_overlap && clamp_hit.normal_valid && clamp_hit.normal.x==1 && clamp_hit.entity==0x1234 && std::abs(clamp_hit.point.x-.075)<1e-6);
    c.normal[0]=-1;h=convert_world_hit(c,{.09,0,0},{.094,0,0},0x1234);
    CHECK(h && h->initial_overlap && h->normal_valid && h->normal.x==-1);
    c.fraction=-.8f;CHECK(!convert_world_hit(c,{0,0,0},{1,0,0},1));
    c.fraction=2;CHECK(!convert_world_hit(c,{0,0,0},{1,0,0},1));
    c.fraction=NAN;CHECK(!convert_world_hit(c,{0,0,0},{1,0,0},1));
}
void test_verify_and_cast() {
    const auto w=NativeWorldTrace::cast_code;
    put(base+NativeWorldTrace::cast_rva,w.data(),w.size());
    put(base+NativeWorldTrace::filter_check_rva,NativeWorldTrace::filter_code.data(),NativeWorldTrace::filter_code.size());
    put(base+NativeWorldTrace::filter_body_rva,NativeWorldTrace::filter_body_code.data(),NativeWorldTrace::filter_body_code.size());
    put(base+NativeWorldTrace::filter_pair_rva,NativeWorldTrace::filter_pair_code.data(),NativeWorldTrace::filter_pair_code.size());
    put(base+NativeWorldTrace::collector_add_rva,NativeWorldTrace::collector_add_code.data(),NativeWorldTrace::collector_add_code.size());
    put(base+NativeWorldTrace::any_hit_add_rva,NativeWorldTrace::any_hit_add_code.data(),NativeWorldTrace::any_hit_add_code.size());
    put(base+NativeWorldTrace::sphere_ctor_rva,NativeHandVolume::constructor_code.data(),NativeHandVolume::constructor_code.size());
    put64(base+NativeWorldTrace::filter_vtable_rva+0x20,base+NativeWorldTrace::filter_check_rva);
    put64(base+NativeWorldTrace::filter_vtable_rva+0x28,base+NativeWorldTrace::filter_pair_rva);
    put64(base+NativeWorldTrace::filter_vtable_rva+0x30,base+NativeWorldTrace::filter_body_rva);
    put64(base+NativeWorldTrace::collector_vtable_rva+0x18,base+NativeWorldTrace::collector_add_rva);
    put64(base+NativeWorldTrace::any_hit_collector_vtable_rva+0x18,base+NativeWorldTrace::any_hit_add_rva);
    CHECK(NativeWorldTrace::verify(base,read_mem));
    memory[base+NativeWorldTrace::cast_rva][5]^=1;CHECK(!NativeWorldTrace::verify(base,read_mem));memory[base+NativeWorldTrace::cast_rva][5]^=1;
    CHECK(NativeWorldTrace::verify(base,read_mem));
    // World / body table: world+0x9d8 broadphase, world+0x28 pool, record 0x123456*0xb0+0x90 -> slot -> +8 -> +0x10 entity.
    constexpr std::uintptr_t world=0x5000000,pool=0x6000000,slot=0x7000000,object=0x8000000;
    put64(world+0x9d8,0xabcdef0);put64(world+0x28,pool);
    put64(pool+0x123456ull*0xb0+0x90,slot);put64(slot+8,object);put64(object+0x10,0xE17717);
    CHECK(NativeWorldTrace::entity_of(read_mem,world,0x00123456)==0xE17717);
    CHECK(NativeWorldTrace::entity_of(read_mem,world,0xffffff)==0 && NativeWorldTrace::entity_of(read_mem,world,0x1)==0);
    // The hit body's class, read from its record as inspect_hit_body and native_rigid_body read it: index below the pool count, the record's own
    // handle equal to the cast's, a motion object, and flags & 7 == 2 for a dynamic body; no body or entity is the world. Anything that does
    // not check (a freed or reused slot, a short pool, a bad record) is an entity that does not move, never a prop.
    {   const std::uint32_t dynamic_flags=0x2,keyframed_flags=0x6,count=0x200000,handle=0x123456,motion=0x1000,no_motion=0,other_handle=0x123457,short_pool=0x100;
        const std::uintptr_t record=pool+0x123456ull*0xb0;
        put(world+0x30,&count,4);put(record+0x70,&handle,4);put(record+0x40,&motion,4);put(record+0x44,&dynamic_flags,4);
        CHECK(NativeWorldTrace::body_class_of(read_mem,world,0x123456,0xE17717)==BodyClass::dynamic);
        put(record+0x44,&keyframed_flags,4);
        CHECK(NativeWorldTrace::body_class_of(read_mem,world,0x123456,0xE17717)==BodyClass::entity_static);
        put(record+0x44,&dynamic_flags,4);
        put(record+0x70,&other_handle,4);   // the slot now holds another body
        CHECK(NativeWorldTrace::body_class_of(read_mem,world,0x123456,0xE17717)==BodyClass::entity_static);
        put(record+0x70,&handle,4);
        put(record+0x40,&no_motion,4);      // no motion object
        CHECK(NativeWorldTrace::body_class_of(read_mem,world,0x123456,0xE17717)==BodyClass::entity_static);
        put(record+0x40,&motion,4);
        put(world+0x30,&short_pool,4);           // the pool is shorter than the index
        CHECK(NativeWorldTrace::body_class_of(read_mem,world,0x123456,0xE17717)==BodyClass::entity_static);
        put(world+0x30,&count,4);
        CHECK(NativeWorldTrace::body_class_of(read_mem,world,0x123456,0)==BodyClass::world && NativeWorldTrace::body_class_of(read_mem,world,0xffffff,0xE17717)==BodyClass::world);
        CHECK(NativeWorldTrace::body_class_of(read_mem,world,0x1fffff,0xE17717)==BodyClass::entity_static);   // the record is not mapped
    }
    // Carry M6: a guard's ragdoll limb is hit as a dynamic body, so the clamp ranks its plane below a wall's. The record fields are those
    // native_ragdoll_body reads on a dead or unconscious guard (flags 0x82 / 0x8a asleep, motion index, own handle at +0x70) and the
    // keyframed bodies of a living one (0xce): only the first are dynamic. The limb's entity is the guard's.
    {   const std::uint32_t count=0x200000,handle=0x123456,motion=0x1005,limb_awake=0x82,limb_asleep=0x8a,animated=0xce;
        const std::uintptr_t record=pool+0x123456ull*0xb0,guard=0xE17718;
        put(world+0x30,&count,4);put(record+0x70,&handle,4);put(record+0x40,&motion,4);
        for(const auto flags:{limb_awake,limb_asleep}) {put(record+0x44,&flags,4);CHECK(NativeWorldTrace::body_class_of(read_mem,world,handle,guard)==BodyClass::dynamic);}
        put(record+0x44,&animated,4);CHECK(NativeWorldTrace::body_class_of(read_mem,world,handle,guard)==BodyClass::entity_static);
        const std::uint32_t restored=0x2;put(record+0x44,&restored,4); // the cast below hits this record as a plain dynamic body
    }
    const auto hit=NativeWorldTrace::sphere_cast(base,read_mem,fake_cast,fake_construct,world,{0,0,0},{4,0,0},0.05);
    CHECK(hit && casts==1 && hit->entity==0xE17717 && hit->body_class==BodyClass::dynamic && hit->fraction==0.25 && hit->centre.x==1.0 && hit->normal.x==-1);
    CHECK(seen.shape_tag_codec==0xabcdef0 && seen.shape && seen.origin[0]==0 && seen.delta[0]==4.f && seen.inverse_delta[0]==.25f);
    CHECK(seen.flags==(0x3f000000u|7u) && seen_radius==0.05f);
    CHECK(seen_filter.vtable==base+0x1ad4b68 && seen_filter.layer_mask==0xc43e && seen_filter.required_bits==0 && seen_filter.storage==0x1ffff);
    CHECK(!NativeWorldTrace::sphere_cast(base,read_mem,fake_cast,fake_construct,world,{0,0,0},{4,0,0},0) && casts==1);
    CHECK(!NativeWorldTrace::sphere_cast(base,read_mem,fake_cast,fake_construct,world,{0,0,0},{0,0,0},0.05) && casts==1);
    corrupt_cast=true;
    CHECK(!NativeWorldTrace::sphere_cast(base,read_mem,fake_cast,fake_construct,world,{0,0,0},{4,0,0},0.05) && casts==2);
    const auto empty=NativeWorldTrace::sphere_query_checked(base,read_mem,fake_cast,fake_construct,world,{0,0,0},{4,0,0},.025);
    CHECK(empty.complete && !empty.hit && casts==3);
    const auto before_failure=casts;
    CHECK(!NativeWorldTrace::sphere_query_checked(base,read_mem,fake_cast,fake_construct,world,{0,0,0},{0,0,0},.025).complete);
    CHECK(!NativeWorldTrace::sphere_query_checked(base,read_mem,fake_cast,fake_construct,world,{0,0,0},{4,0,0},NAN).complete);
    CHECK(!NativeWorldTrace::sphere_query_checked(base,read_mem,fake_cast,fake_construct,world,{0,0,0},{4,0,0},INFINITY).complete);
    CHECK(!NativeWorldTrace::sphere_query_checked(base,read_mem,nullptr,fake_construct,world,{0,0,0},{4,0,0},.025).complete);
    memory.erase(world+0x9d8);
    CHECK(!NativeWorldTrace::sphere_query_checked(base,read_mem,fake_cast,fake_construct,world,{0,0,0},{4,0,0},.025).complete);
    put64(world+0x9d8,0xabcdef0);fail_construct=true;
    CHECK(!NativeWorldTrace::sphere_query_checked(base,read_mem,fake_cast,fake_construct,world,{0,0,0},{4,0,0},.025).complete && casts==before_failure);
    fail_construct=false;corrupt_cast=false;
    for(const auto fraction:std::array<float,3>{-.8f,2.f,NAN}) {
        fixture_fraction=fraction;
        const auto malformed=NativeWorldTrace::sphere_query_checked(base,read_mem,fake_cast,fake_construct,world,{0,0,0},{4,0,0},.025);
        CHECK(!malformed.complete && !malformed.hit);
    }
    fixture_fraction=.25f;fixture_hit_count=2;
    const auto malformed_count=NativeWorldTrace::sphere_query_checked(base,read_mem,fake_cast,fake_construct,world,{0,0,0},{4,0,0},.025);
    CHECK(!malformed_count.complete && !malformed_count.hit);fixture_hit_count=1;
    const auto checked=NativeWorldTrace::sphere_query_checked(base,read_mem,fake_cast,fake_construct,world,{0,0,0},{4,0,0},.025);
    CHECK(checked.complete && checked.hit && checked.hit->entity==0xE17717 && checked.hit->fraction==.25);
    CHECK(seen.source_body==0 && seen.query_id==0); // a legacy cast excludes nothing
    // The hand clamp's exclusion sweep (physical hands Task 5): the met prop's body record and filter word go to +0x68/+0x14.
    const NativeCastExclusion prop{0x9000000,0x2a000777,0x00ff0004};
    const auto excluded_casts=casts;
    const auto excluded=NativeWorldTrace::sphere_cast(base,read_mem,fake_cast,fake_construct,world,{0,0,0},{4,0,0},.05,kWorldOccluderLayers,kWorldOccluderRequired,&prop);
    CHECK(excluded && casts==excluded_casts+1 && seen.source_body==0x9000000 && seen.query_id==0x00ff0004 && excluded->entity==0xE17717);
    CHECK(seen_filter.layer_mask==0xc43e && seen_filter.required_bits==0); // the layer filter is the legacy one
    const NativeCastExclusion zero_info{0x9000000,0x2a000777,0};
    CHECK(NativeWorldTrace::sphere_cast(base,read_mem,fake_cast,fake_construct,world,{0,0,0},{4,0,0},.05,kWorldOccluderLayers,kWorldOccluderRequired,&zero_info) &&
          seen.source_body==0x9000000 && seen.query_id==0);
    // A record that is no pointer, or a body without an index, casts nothing.
    for(const auto bad:{NativeCastExclusion{0x9000001,0x2a000777,4},NativeCastExclusion{0,0x2a000777,4},NativeCastExclusion{0x9000000,0xffffff,4}}) {
        const auto before=casts;
        CHECK(!NativeWorldTrace::sphere_query_checked(base,read_mem,fake_cast,fake_construct,world,{0,0,0},{4,0,0},.05,kWorldOccluderLayers,kWorldOccluderRequired,nullptr,false,&bad).complete && casts==before);
    }
    // An exclusion never combines with the player's source lease.
    NativeWorldQuerySource lease{};
    {const auto before=casts;CHECK(!NativeWorldTrace::sphere_query_checked(base,read_mem,fake_cast,fake_construct,world,{0,0,0},{4,0,0},.05,kWorldOccluderLayers,kWorldOccluderRequired,&lease,false,&prop).complete && casts==before);}
}
int main() {
    test_pure();test_verify_and_cast();
    std::puts("PASS: native world trace layouts, request building, verification and hit conversion");
    return 0;
}
