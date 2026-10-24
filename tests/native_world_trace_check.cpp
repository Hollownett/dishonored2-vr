#include "../src/native_world_trace.hpp"
#include "../src/held_clamp.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
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
static std::size_t class_reads;   // reads of a body record's flag word (+0x44): only body_class_of makes them
bool read_mem(std::uintptr_t at,void* out,std::size_t size);
bool counting_read(std::uintptr_t at,void* out,std::size_t size) {if(at==0x6000000ull+0x123456ull*0xb0+0x44) ++class_reads;return read_mem(at,out,size);}
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
    {   // Final review P3: only the hand clamp ranks planes by body class; every other cast skips the body-record reads
        const auto before=casts;
        class_reads=0;
        const auto plain=NativeWorldTrace::sphere_cast(base,counting_read,fake_cast,fake_construct,world,{0,0,0},{4,0,0},0.05,kWorldOccluderLayers,kWorldOccluderRequired,nullptr,false);
        const auto plain_reads=class_reads;class_reads=0;
        const auto ranked=NativeWorldTrace::sphere_cast(base,counting_read,fake_cast,fake_construct,world,{0,0,0},{4,0,0},0.05,kWorldOccluderLayers,kWorldOccluderRequired,nullptr,true);
        CHECK(casts==before+2 && plain && ranked && plain->entity==ranked->entity && plain->fraction==ranked->fraction);
        CHECK(plain->body_class==BodyClass::world && ranked->body_class==BodyClass::dynamic);   // unclassified keeps the default
        CHECK(plain_reads==0 && class_reads>0);
    }
}
// ---- Final review P1-1: a hand body must never blind a cast. A fake world that answers sphere sweeps the way the engine does: the native
// closest collector keeps the strictly nearest contact (a later equal-fraction candidate is dropped, as the outer traversal drops it), the
// request's source body (+0x68) is skipped by its full handle (+0x70), and any other collector (the mod's all-hits one) receives every contact
// through its own add slot.
constexpr double R1HandRadius=.05;   // R1Recipe::radius (hand_body.hpp)
struct SceneBody {std::uint32_t handle{};bool sphere{};Vec3 at{};double radius{};Vec3 normal{};};   // a sphere (at, radius) or a plane (point at, normal)
std::vector<SceneBody> scene;
unsigned scene_casts{},scene_all_hit_casts{},scene_excluding_casts{};
constexpr std::uintptr_t scene_world=0x5000000,scene_pool=0x6000000;
std::uintptr_t scene_record(std::uint32_t handle) {return scene_pool+static_cast<std::uintptr_t>(handle&0xffffffu)*0xb0;}
struct SceneContact {double t{};Vec3 point{},normal{};};
std::optional<SceneContact> scene_contact(const SceneBody& b,Vec3 o,Vec3 d,double r) {
    if(b.sphere) {
        const Vec3 m=o-b.at;const double rs=b.radius+r,a=dot(d,d),bb=2*dot(m,d),c=dot(m,m)-rs*rs;
        if(c<=0) {const double l=length(m);const Vec3 n=l>1e-12?m*(1/l):Vec3{0,0,1};return SceneContact{0,b.at+n*b.radius,n};}
        const double disc=bb*bb-4*a*c;if(disc<0 || !(a>0)) return {};
        const double t=(-bb-std::sqrt(disc))/(2*a);if(t<0 || t>1) return {};
        const Vec3 p=o+d*t;const Vec3 n=unit(p-b.at);return SceneContact{t,b.at+n*b.radius,n};
    }
    const double s0=dot(o-b.at,b.normal)-r;
    if(s0<=0) return SceneContact{0,o-b.normal*dot(o-b.at,b.normal),b.normal};
    const double dn=dot(d,b.normal);if(dn>=0) return {};
    const double t=s0/-dn;if(t>1) return {};
    return SceneContact{t,o+d*t-b.normal*r,b.normal};
}
void __fastcall scene_cast(void* world,const NativeCastRequest* r,const float* rot,NativeClosestCollector* c,std::uintptr_t zero) {
    ++scene_casts;CHECK(world==reinterpret_cast<void*>(scene_world) && zero==0 && rot);
    const Vec3 o{r->origin[0],r->origin[1],r->origin[2]},d{r->delta[0],r->delta[1],r->delta[2]};
    float radius{};std::memcpy(&radius,reinterpret_cast<const std::byte*>(r->shape)+0x28,4);
    std::uint32_t skip=0xffffffffu;
    if(r->source_body) {++scene_excluding_casts;if(!read_mem(r->source_body+0x70,&skip,4)) skip=0xffffffffu;}
    const bool closest=c->vtable==base+NativeWorldTrace::collector_vtable_rva;
    if(!closest) ++scene_all_hit_casts;
    const auto add=closest?nullptr:reinterpret_cast<void (__fastcall*)(void*,const void*)>(reinterpret_cast<const std::uintptr_t*>(c->vtable)[3]);
    bool have=false;
    for(const auto& b:scene) {
        if(b.handle==skip) continue;
        const auto k=scene_contact(b,o,d,radius);if(!k) continue;
        if(closest) {
            if(have && !(k->t<c->fraction)) continue;
            have=true;c->hit_count=1;c->fraction=static_cast<float>(k->t);c->body=b.handle;c->body_key=7;
            c->position[0]=static_cast<float>(k->point.x);c->position[1]=static_cast<float>(k->point.y);c->position[2]=static_cast<float>(k->point.z);
            c->normal[0]=static_cast<float>(k->normal.x);c->normal[1]=static_cast<float>(k->normal.y);c->normal[2]=static_cast<float>(k->normal.z);
        } else {
            unsigned char rec[0x70]{};const float p[3]{float(k->point.x),float(k->point.y),float(k->point.z)},n[3]{float(k->normal.x),float(k->normal.y),float(k->normal.z)};
            const float f=static_cast<float>(k->t);const std::uint16_t material=7;const std::uint32_t key=0xffffffffu;
            std::memcpy(rec,p,12);std::memcpy(rec+0x10,n,12);std::memcpy(rec+0x20,&f,4);std::memcpy(rec+0x48,&b.handle,4);std::memcpy(rec+0x4c,&material,2);std::memcpy(rec+0x50,&key,4);
            add(c,rec);
        }
    }
}
void scene_body(SceneBody b) {
    scene.push_back(b);
    const std::uintptr_t record=scene_record(b.handle),userdata=0x7100000+(b.handle&0xffffu)*0x40,component=userdata+0x20;
    const std::uint32_t filter=0x000885e4u;
    put(record+0x6c,&filter,4);put(record+0x70,&b.handle,4);put64(record+0x90,userdata);put64(userdata+8,component);put64(component+0x10,0xE0000000ull+b.handle);
}
std::uintptr_t entity_of_scene(std::uint32_t handle) {return 0xE0000000ull+handle;}
WorldTraceQueryResult scene_query(Vec3 from,Vec3 to,double radius,const NativeCastExclusion* exclude=nullptr) {
    return NativeWorldTrace::sphere_query_checked(base,read_mem,scene_cast,fake_construct,scene_world,from,to,radius,kWorldOccluderLayers,kWorldOccluderRequired,nullptr,false,exclude,false);
}
void arm_hands(std::uint32_t left,std::uint32_t right) {cast_excluded_bodies[0].store(left);cast_excluded_bodies[1].store(right);}
void test_hand_body_continuation() {
    // the mod's all-hits collector checks the native closest collector's vtable before it is used (native_initial_contact.hpp)
    {std::array<std::uint64_t,5> slots{};for(unsigned i=0;i<5;++i) slots[i]=base+NativeInitialContactCollector::native_slots[i];
     put(base+NativeInitialContactCollector::native_vtable_rva,slots.data(),sizeof slots);
     put(base+NativeInitialContactCollector::native_slots[0],NativeInitialContactCollector::getter_code.data(),NativeInitialContactCollector::getter_code.size());
     put(base+NativeInitialContactCollector::native_slots[4],NativeInitialContactCollector::noop_code.data(),NativeInitialContactCollector::noop_code.size());}
    const std::uint32_t count=0x200000;put(scene_world+0x30,&count,4);put64(scene_world+0x28,scene_pool);put64(scene_world+0x9d8,0xabcdef0);
    constexpr std::uint32_t hand_l=0x01000101,hand_r=0x01000102,wall=0x00000201,floor_body=0x00000202,tank=0x00000203,crate=0x00000204;
    const auto wall_x=[](double x) {return SceneBody{wall,false,{x,0,0},0,{-1,0,0}};};
    const auto floor_z=[](double z) {return SceneBody{floor_body,false,{0,0,z},0,{0,0,1}};};
    const auto hand_at=[](std::uint32_t h,Vec3 at) {return SceneBody{h,true,at,R1HandRadius,{}};};
    const auto c0=cast_continuations.load(),e0=cast_continuation_exhausted.load();

    {   // knob off (no hand body armed): one cast, no continuation, the old answer exactly (the hand-shaped body is just another body here)
        arm_hands(0xffffffffu,0xffffffffu);scene.clear();scene_body(hand_at(hand_l,{.5,0,0}));scene_body(wall_x(.6));scene_casts=0;
        const auto q=scene_query({0,0,0},{1,0,0},.04);
        CHECK(q.complete && q.hit && q.hit->body_handle==hand_l && std::abs(q.hit->fraction-.41)<1e-6 && scene_casts==1);
        CHECK(cast_continuations.load()==c0 && cast_continuation_exhausted.load()==e0);
        // armed, but the sweep never meets a hand body: still one cast, nothing continued
        arm_hands(hand_l,hand_r);scene.clear();scene_body(hand_at(hand_l,{.5,2,0}));scene_body(wall_x(.6));scene_casts=0;
        const auto w=scene_query({0,0,0},{1,0,0},.04);
        CHECK(w.hit && w.hit->body_handle==wall && scene_casts==1 && cast_continuations.load()==c0);
    }
    {   // wall_behind_hand_body_is_hit: the hand body 0.5 m along the sweep, a wall 0.1 m behind it: the wall is the answer, with its own fraction,
        // point and entity (the engine's exclusion of the hand: one continuation)
        arm_hands(hand_l,hand_r);scene.clear();scene_body(hand_at(hand_l,{.5,0,0}));scene_body(wall_x(.6));scene_casts=scene_excluding_casts=0;
        const auto hits0=cast_excluded_hits.load(),c1=cast_continuations.load();
        const auto q=scene_query({0,0,0},{1,0,0},.04);
        CHECK(q.complete && q.hit && !q.hit->hand_body && q.hit->body_handle==wall && q.hit->entity==entity_of_scene(wall));
        CHECK(std::abs(q.hit->fraction-.56)<1e-6 && std::abs(q.hit->centre.x-.56)<1e-6 && q.hit->normal.x==-1 && !q.hit->initial_overlap);
        CHECK(scene_casts==2 && scene_excluding_casts==1 && cast_continuations.load()==c1+1 && cast_excluded_hits.load()==hits0+1);
        // the same through sphere_cast (every weapon-slot sweep, the hand clamp's palm sweep among them)
        const auto s=NativeWorldTrace::sphere_cast(base,read_mem,scene_cast,fake_construct,scene_world,{0,0,0},{1,0,0},.04);
        CHECK(s && s->body_handle==wall);
        // a caller exclusion takes the engine's slot (a held object's sweeps): the all-hits continuation finds the wall all the same
        scene_body(SceneBody{crate,true,{.2,3,0},.1,{}});
        const NativeCastExclusion held{scene_record(crate),crate,0};scene_casts=scene_all_hit_casts=0;
        const auto x=scene_query({0,0,0},{1,0,0},.04,&held);
        CHECK(x.complete && x.hit && x.hit->body_handle==wall && std::abs(x.hit->fraction-.56)<1e-6 && scene_casts==2 && scene_all_hit_casts==1);
        // nothing behind the hand: an honest no-hit (complete, continued)
        scene.clear();scene_body(hand_at(hand_l,{.5,0,0}));
        const auto clear=scene_query({0,0,0},{1,0,0},.04);CHECK(clear.complete && !clear.hit);
        const auto clear_held=scene_query({0,0,0},{1,0,0},.04,&held);CHECK(clear_held.complete && !clear_held.hit);
    }
    {   // palm_inside_wall_never_reads_free (the P1-1 scenario): a palm-heel press, the body stopped on the table and the next palm sweep starting
        // inside it (an initial overlap on the hand, fraction 0) toward the controller 0.30 m inside the table: the table is reported, never nothing
        arm_hands(hand_l,hand_r);scene.clear();scene_body(hand_at(hand_l,{0,0,.05}));scene_body(floor_z(0));
        const auto q=scene_query({0,0,.06},{0,0,-.30},.04);
        CHECK(q.complete && q.hit && q.hit->body_handle==floor_body && !q.hit->hand_body && q.hit->normal.z==1);
        CHECK(std::abs(q.hit->fraction-.02/.36)<1e-6);
        // the create/snap probe at a palm already inside the table (both the hand and the table overlap the start): an initial overlap on the table
        const auto probe=scene_query({0,0,.03},{0,0,.02},R1HandRadius);
        CHECK(probe.hit && probe.hit->body_handle==floor_body && probe.hit->initial_overlap);
        const NativeCastExclusion held{scene_record(tank),tank,0};
        const auto probe_held=scene_query({0,0,.03},{0,0,.02},R1HandRadius,&held);   // while holding (the held item excluded): the same
        CHECK(probe_held.hit && probe_held.hit->body_handle==floor_body && probe_held.hit->initial_overlap);
        // knob-on before the fix read this as "no hit": the hand body alone in the way is never the whole answer
        scene.clear();scene_body(hand_at(hand_l,{0,0,.05}));scene_body(hand_at(hand_r,{0,0,.12}));scene_body(floor_z(0));
        const auto both=scene_query({0,0,.2},{0,0,-.3},.04);CHECK(both.hit && both.hit->body_handle==floor_body);
    }
    {   // tank_release_cast_sees_floor_through_hand_body (B2/B4's SafeRelease): the tank let go 5 cm into the floor, the hand body still at the grip on
        // its top overlapping the middle probe; the probes exclude the tank (the engine's slot is taken), so the all-hits continuation runs and the
        // floor is an overlap with depth: the DEBRIS layer stays
        arm_hands(hand_l,hand_r);scene.clear();
        HeldProxies proxies;proxies.count=3;for(unsigned i=0;i<3;++i) proxies.sphere[i]={{-.4+.4*i,0,0},.2};
        WorldTrackedPose pose;pose.position={0,0,.15};pose.basis={Vec3{1,0,0},Vec3{0,1,0},Vec3{0,0,1}};
        scene_body(SceneBody{tank,true,{0,0,.15},.2,{}});scene_body(hand_at(hand_l,{0,0,.37}));scene_body(floor_z(0));
        const NativeCastExclusion held{scene_record(tank),tank,0};
        const auto sweeps=release_probe_sweeps(proxies,pose);
        unsigned overlaps=0;
        for(unsigned i=0;i<3;++i) {
            const auto q=scene_query(sweeps[i].from,sweeps[i].to,sweeps[i].radius,&held);
            CHECK(q.complete && q.hit && q.hit->body_handle==floor_body && !q.hit->hand_body);
            if(const auto depth=release_probe_depth(sweeps[i],*q.hit); depth && *depth>0) ++overlaps;
        }
        CHECK(overlaps==3);
        // the middle probe really met the hand first (its continuation ran): without the fix it read "clear"
        const auto before=cast_excluded_hits.load();(void)scene_query(sweeps[1].from,sweeps[1].to,sweeps[1].radius,&held);CHECK(cast_excluded_hits.load()==before+1);
    }
    {   // continuation limit: two hand bodies on the sweep (hands together) and the engine's slot free: the closest continuation meets the other
        // hand, the all-hits one skips both and finds the wall: 1 + 2 casts, never more
        arm_hands(hand_l,hand_r);scene.clear();scene_body(hand_at(hand_l,{.3,0,0}));scene_body(hand_at(hand_r,{.4,0,0}));scene_body(wall_x(.7));
        scene_casts=0;const auto c1=cast_continuations.load();
        const auto q=scene_query({0,0,0},{1,0,0},.04);
        CHECK(q.hit && q.hit->body_handle==wall && std::abs(q.hit->fraction-.66)<1e-6 && scene_casts==1+kMaxCastContinuations && cast_continuations.load()==c1+2);
        // an all-hits collector that overflows (more than 32 contacts) cannot say what is nearest: blocked, the hand-body hit itself, entity 0,
        // dynamic; still 1 + 2 casts at most
        for(std::uint32_t k=0;k<40;++k) scene_body(SceneBody{0x300+k,true,{.8,0,0},.01+.001*k,{}});
        scene_casts=0;const auto e1=cast_continuation_exhausted.load();
        const auto b=scene_query({0,0,0},{1,0,0},.04);
        CHECK(b.complete && b.hit && b.hit->hand_body && b.hit->entity==0 && b.hit->body_class==BodyClass::dynamic && scene_casts==3);
        CHECK(cast_continuation_exhausted.load()==e1+1 && (b.hit->body_handle==hand_l || b.hit->body_handle==hand_r));
        // with a caller exclusion only the all-hits continuation can run: 1 + 1 casts, then blocked
        const NativeCastExclusion held{scene_record(crate),crate,0};scene_casts=0;
        const auto h=scene_query({0,0,0},{1,0,0},.04,&held);
        CHECK(h.hit && h.hit->hand_body && h.hit->body_handle==hand_l && scene_casts==2 && cast_continuation_exhausted.load()==e1+2);
        // an unreadable hand record (the pool slot holds another handle): no engine exclusion, the all-hits continuation instead
        scene.clear();scene_body(hand_at(hand_l,{.5,0,0}));scene_body(wall_x(.6));
        const std::uint32_t other=0x02000101;put(scene_record(hand_l)+0x70,&other,4);scene_casts=scene_excluding_casts=scene_all_hit_casts=0;
        const auto u=scene_query({0,0,0},{1,0,0},.04);
        CHECK(u.hit && u.hit->body_handle==wall && scene_casts==2 && scene_excluding_casts==0 && scene_all_hit_casts==1);
        put(scene_record(hand_l)+0x70,&hand_l,4);
        // an initial overlap on the hand and nothing else: complete and clear (the hand is not the world)
        scene.clear();scene_body(hand_at(hand_l,{0,0,0}));
        const auto o=scene_query({0,0,0},{.2,0,0},.04);CHECK(o.complete && !o.hit);
    }
    arm_hands(0xffffffffu,0xffffffffu);scene.clear();
}
int main() {
    test_pure();test_verify_and_cast();test_hand_body_continuation();
    std::puts("PASS: native world trace layouts, request building, verification and hit conversion");
    return 0;
}
