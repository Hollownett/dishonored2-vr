#pragma once
// Typed adapter for the native synchronous physics-world shape cast (RVA 0xc89bc0)
// with a closest-hit collector. Evidence and unverified items: docs/native-world-trace.md.
// NOTHING here is installed or called by default. Call only from the game-thread
// stage-0 sensor scope (same place as NativeInteractionAim::service_obstruction).
#include "native_hand_volume.hpp"
#include "weapon_collision.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <tuple>
#include <type_traits>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4324) // native stack objects are 16-byte aligned on purpose
#endif
namespace d2vr {

// Layout of the 0x70-byte request, identical to the queued-job request that
// 0x8ffb0 copies (slot+0x90) and to the stack object built by 0xcc99c0/0x3b7b90.
struct alignas(16) NativeCastRequest {
    std::uint64_t world_broadphase{};   // 0x00 *(world+0x9d8)
    std::uint64_t filter{};             // 0x08 NativeCastFilter* (cc99c0 passes world+0x470 here)
    std::uint16_t group{0xffff};        // 0x10
    std::uint16_t pad12{};
    std::uint32_t query_id{};           // 0x14 0 in every verified world query
    std::uint64_t reserved18{};         // 0x18
    std::uint32_t kind{2};              // 0x20 constant 2
    std::uint32_t pad24{};
    std::uint64_t shape{};              // 0x28 hknpSphereShape built by 0xca6550
    float origin[4]{};                  // 0x30 start (w unused)
    float delta[4]{0,0,0,1};            // 0x40 end-start, -0 cleared, w = 1.0
    float inverse_delta[3]{};           // 0x50 1/delta, FLT_MAX sentinel for 0
    std::uint32_t flags{0x3f000000};    // 0x5c 0x3f000000 | bit i set when delta[i] >= 0
    float tolerance{0.001f};            // 0x60 DAT_141ab3b04
    float pad64{};
    std::uint64_t reserved68{};         // 0x68
};
static_assert(sizeof(NativeCastRequest)==0x70 && alignof(NativeCastRequest)==16);
static_assert(offsetof(NativeCastRequest,filter)==0x08 && offsetof(NativeCastRequest,query_id)==0x14 &&
              offsetof(NativeCastRequest,kind)==0x20 && offsetof(NativeCastRequest,shape)==0x28 &&
              offsetof(NativeCastRequest,origin)==0x30 && offsetof(NativeCastRequest,delta)==0x40 &&
              offsetof(NativeCastRequest,inverse_delta)==0x50 && offsetof(NativeCastRequest,flags)==0x5c &&
              offsetof(NativeCastRequest,tolerance)==0x60 && offsetof(NativeCastRequest,reserved68)==0x68);

// Stack filter object (vtable RVA 0x1ad4b68; slot 4 = 0x996d0 shouldCollide):
// collide iff (layer_mask >> (info & 31)) & 1 and ((info >> 5) & required) == required,
// where info is the other body's filter dword (+4 of its collidable).
struct NativeCastFilter {
    std::uint64_t vtable{};             // base + 0x1ad4b68
    std::uint64_t zero8{};
    std::uint32_t storage{0x1ffff};     // external-storage marker, never freed
    std::uint32_t pad14{};
    std::uint8_t kind{4};               // 0x18
    std::uint8_t pad19[7]{};
    std::uint32_t layer_mask{};         // 0x20
    std::uint32_t required_bits{};      // 0x24
};
static_assert(sizeof(NativeCastFilter)==0x28 && offsetof(NativeCastFilter,layer_mask)==0x20 &&
              offsetof(NativeCastFilter,required_bits)==0x24);

// Closest-hit collector (vtable RVA 0x1ad5010), initial state as built by 0xcc99c0.
struct alignas(16) NativeClosestCollector {
    std::uint64_t vtable{};             // 0x00 base + 0x1ad5010
    std::uint32_t mode{};               // 0x08 bit0 = early-out (native: 0)
    std::uint32_t hit_count{};          // 0x0c nonzero after a hit (cc99c0 tests this)
    std::uint8_t stop{};                // 0x10
    std::uint8_t pad11[15]{};
    float limit[4]{};                   // 0x20 from .data 0x23cce20 = 0x5f7ffff0 each
    float position[4]{};                // 0x30 hit position (world)
    float normal[4]{};                  // 0x40 hit normal (xyz)
    float fraction{std::numeric_limits<float>::max()}; // 0x50 along delta (0x7f7fffee initial)
    std::uint32_t pad54{};
    std::uint32_t body_a{0xffffff};     // 0x58 (record +0x28)
    std::uint16_t key_a{0xffff};        // 0x5c
    std::uint16_t pad5e{};
    std::uint32_t a60{0xffffffffu}, a64{}, a68{0xffffffffu}, a6c{};
    std::uint64_t a70{};                // 0x70
    std::uint32_t body{0xffffff};       // 0x78 hit body handle (low 24 bits = pool index)
    std::uint16_t body_key{0xffff};     // 0x7c
    std::uint16_t pad7e{};
    std::uint32_t shape_key{0xffffffffu}; // 0x80
    std::uint32_t b84{};
    std::uint32_t b88{0xffffffffu};
    std::uint32_t b8c{};
    std::uint64_t b90{};                // 0x90
};
static_assert(sizeof(NativeClosestCollector)==0xa0 && offsetof(NativeClosestCollector,hit_count)==0x0c &&
              offsetof(NativeClosestCollector,limit)==0x20 && offsetof(NativeClosestCollector,position)==0x30 &&
              offsetof(NativeClosestCollector,normal)==0x40 && offsetof(NativeClosestCollector,fraction)==0x50 &&
              offsetof(NativeClosestCollector,body)==0x78 && offsetof(NativeClosestCollector,shape_key)==0x80);

// Rotation argument: three identity rows (verified .data 0x23ccdd0..0x23ccdf0).
inline constexpr std::array<float,12> native_cast_identity{1,0,0,0, 0,1,0,0, 0,0,1,0};

// Layer masks seen in native stack filters. Only kOccluderLayers is used by a
// player-origin query (0x3b7b90, the interaction obstruction test); that it
// excludes the player's own layer is INFERRED, not observed.
inline constexpr std::uint32_t kWorldOccluderLayers=0xc43e;   // 0x3b7b90, required bits 0
inline constexpr std::uint32_t kWorldOccluderRequired=0;
// Not for our use (AI/entity probes, semantics unknown): 0x2f/0x22f(0x638800), 0x2e/0x800(0x83c860),
// 0x200/0x40(0x83a510), 0x22b/0x40(0x44bd90), 0x2b/0x40(0x3a2730).

struct WorldTraceHit {
    Vec3 point{};            // native contact position on the hit surface
    Vec3 centre{};           // from + (to-from)*fraction: where the swept sphere stops
    Vec3 normal{};           // unit, facing against the sweep (toward free space)
    double fraction{};       // 0..1 along from->to
    std::uintptr_t entity{}; // idEntity* or 0 (unknown body)
    std::uint32_t body{};    // 24-bit physics body index
    bool initial_overlap{};  // fraction 0: the sphere started inside geometry
    std::uint16_t material{0xffff}; // collector +0x7c: physics material id of the hit surface (docs/native-impact-fx.md)
};

inline float native_delta_clean(double v) {const float f=static_cast<float>(v);return f==0?0.f:f;}
inline std::optional<NativeCastRequest> make_cast_request(Vec3 from,Vec3 to,std::uint64_t broadphase,
        std::uint64_t filter,std::uint64_t shape) {
    const Vec3 d=to-from;
    if(!finite(from) || !finite(to) || !broadphase || !filter || !shape) return {};
    const double len=length(d);
    if(!(len>=1e-3) || len>50) return {};
    NativeCastRequest r{};
    r.world_broadphase=broadphase;r.filter=filter;r.shape=shape;
    const double dv[3]{d.x,d.y,d.z},ov[3]{from.x,from.y,from.z};
    for(unsigned i=0;i<3;++i) {
        r.origin[i]=static_cast<float>(ov[i]);r.delta[i]=native_delta_clean(dv[i]);
        r.inverse_delta[i]=r.delta[i]==0?std::numeric_limits<float>::max():1.f/r.delta[i];
        if(!std::isfinite(r.origin[i]) || !std::isfinite(r.inverse_delta[i])) return {};
        if(r.delta[i]>=0) r.flags|=1u<<i;
    }
    return r;
}
inline NativeClosestCollector make_closest_collector(std::uint64_t vtable) {
    NativeClosestCollector c{};c.vtable=vtable;
    for(float& v:c.limit) {const std::uint32_t bits=0x5f7ffff0;std::memcpy(&v,&bits,4);}
    return c;
}
inline std::optional<WorldTraceHit> convert_world_hit(const NativeClosestCollector& c,Vec3 from,Vec3 to,std::uintptr_t entity) {
    if(!c.hit_count) return {};
    const double f=c.fraction;
    if(!std::isfinite(f) || f<0 || f>1.0001) return {};
    const Vec3 point{c.position[0],c.position[1],c.position[2]},raw{c.normal[0],c.normal[1],c.normal[2]};
    const Vec3 d=to-from;const double len=length(d);
    if(!finite(point) || !finite(from) || !finite(to) || !(len>0)) return {};
    WorldTraceHit h{};
    h.point=point;h.fraction=std::min(f,1.0);h.centre=from+d*h.fraction;
    h.body=c.body&0xffffff;h.entity=h.body==0xffffff?0:entity;
    h.initial_overlap=f<=1e-6;h.material=c.body_key;
    const Vec3 back=d*(-1/len);
    const double nl=length(raw);
    Vec3 n=back;
    // Initial-overlap hits (slot 9 / 0xca5460) may carry no usable normal: fall back to the reversed sweep.
    if(finite(raw) && nl>.5 && nl<1.5) {n=raw*(1/nl);if(dot(n,d)>0) n=n*-1;}
    h.normal=n;return h;
}
// Feeds WeaponCollision::update's query(from,to). Pass the sphere cast radius = palm_radius
// ONLY if the policy's own palm_radius is set to 0 (otherwise the margin is applied twice).
inline CollisionHit to_collision_hit(const WorldTraceHit& h) {return {h.point,h.normal,h.fraction,h.entity,h.material};}

class NativeWorldTrace {
public:
    using Read=bool (*)(std::uintptr_t,void*,std::size_t);
    // void c89bc0(world RCX, const NativeCastRequest* RDX, const float rot[12] R8,
    //             NativeClosestCollector* R9, uint64 stack=0)
    using Cast=void (__fastcall*)(void*,const NativeCastRequest*,const float*,NativeClosestCollector*,std::uintptr_t);
    using Construct=NativeHandVolume::Construct; // 0xca6550(storage96, centre vec4*, radius)
    inline static constexpr std::uintptr_t cast_rva=0xc89bc0,filter_vtable_rva=0x1ad4b68,collector_vtable_rva=0x1ad5010,
        filter_check_rva=0x996d0,collector_add_rva=0xca53a0,overlap_add_rva=0xca5460,sphere_ctor_rva=0xca6550,
        sphere_vtable_rva=0x1ec1e78,world_offset=0x60410,broadphase_offset=0x9d8,body_pool_offset=0x28;
    inline static constexpr std::array<unsigned char,48> cast_code{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48,0x89,0x7c,0x24,0x20,0x41,0x56,0x48,0x83,0xec,0x40,0x48,0x8b,0xf9,0x8b,0x0d,0x21,0x42,0x20,0x03,0x49,0x8b,0xf1,0x49,0x8b,0xe8,0x4c,0x8b,0xf2,0xff,0x15,0x06,0x47};
    inline static constexpr std::array<unsigned char,55> filter_code{0x4c,0x8b,0xd1,0x45,0x84,0xc0,0x74,0x0a,0x48,0x8b,0x44,0x24,0x28,0x8b,0x50,0x04,0xeb,0x04,0x41,0x8b,0x51,0x04,0x41,0x8b,0x42,0x20,0x8b,0xca,0x83,0xe1,0x1f,0x0f,0xa3,0xc8,0x73,0x10,0x41,0x8b,0x42,0x24,0xc1,0xea,0x05,0x23,0xd0,0x3b,0xd0,0x75,0x03,0xb0,0x01,0xc3,0x32,0xc0,0xc3};
    inline static constexpr std::array<unsigned char,32> collector_add_code{0xf3,0x0f,0x10,0x42,0x20,0x0f,0xc6,0xc0,0x00,0x0f,0x2e,0x41,0x20,0x0f,0x83,0xa8,0x00,0x00,0x00,0x0f,0x28,0x02,0x0f,0x29,0x41,0x30,0x0f,0x28,0x4a,0x10,0x0f,0x29};
    inline static constexpr std::array<unsigned char,32> overlap_add_code{0xf3,0x0f,0x10,0x41,0x50,0x0f,0x2f,0x42,0x20,0x0f,0x86,0x95,0x00,0x00,0x00,0x0f,0x28,0x02,0x0f,0x29,0x41,0x30,0x0f,0x28,0x4a,0x10,0x0f,0x57,0xc0,0x0f,0x29,0x49};

    // Code bytes at every RVA used, plus the two vtables' add-hit slots (image base 0x140000000).
    static bool verify(std::uintptr_t base,Read read) {
        if(!base || !read) return false;
        const auto code=[&](std::uintptr_t rva,const auto& expect) {
            std::array<unsigned char,std::tuple_size<std::decay_t<decltype(expect)>>::value> got{};
            return read(base+rva,got.data(),got.size()) && got==expect;
        };
        const auto qword=[&](std::uintptr_t rva) {std::uint64_t v{};return read(base+rva,&v,8)?v:0;};
        return code(cast_rva,cast_code) && code(filter_check_rva,filter_code) &&
            code(collector_add_rva,collector_add_code) && code(overlap_add_rva,overlap_add_code) &&
            code(sphere_ctor_rva,NativeHandVolume::constructor_code) &&
            qword(filter_vtable_rva+0x20)==base+filter_check_rva &&
            qword(collector_vtable_rva+0x18)==base+collector_add_rva &&
            qword(collector_vtable_rva+0x48)==base+overlap_add_rva;
    }
    // world = *(*(base+0x39a5290) + 0x60410). Caller must have validated the game object type
    // (RTTI 0x1c940a8) exactly as native_interaction_aim.hpp does at its line ~482.
    // Body handle -> idEntity*: *( *( *(world+0x28) + (body&0xffffff)*0xb0 + 0x90 ) + 8 ) + 0x10 (0x18cd00 + 0x3b7b90).
    static std::uintptr_t entity_of(Read read,std::uintptr_t world,std::uint32_t body) {
        const auto q=[&](std::uintptr_t at)->std::uintptr_t {std::uintptr_t v{};return at&&read(at,&v,8)?v:0;};
        const auto index=body&0xffffff;
        if(index==0xffffff) return 0;
        const auto pool=q(world+body_pool_offset);
        const auto slot=pool?q(pool+static_cast<std::uintptr_t>(index)*0xb0+0x90):0;
        const auto object=slot?q(slot+8):0;
        return object?q(object+0x10):0;
    }
    // One sphere sweep from->to (world space, metres as the game uses them: UNIT SCALE UNVERIFIED).
    // radius must be > 0. Returns the closest hit among bodies accepted by the filter.
    static std::optional<WorldTraceHit> sphere_cast(std::uintptr_t base,Read read,Cast cast,Construct construct,
            std::uintptr_t world,Vec3 from,Vec3 to,double radius,
            std::uint32_t layers=kWorldOccluderLayers,std::uint32_t required=kWorldOccluderRequired) {
        if(!base || !read || !cast || !construct || !world || !(radius>0.0005) || radius>1) return {};
        std::uint64_t broadphase{};
        if(!read(world+broadphase_offset,&broadphase,8)) return {};
        // Shape is rebuilt per call: radius lives at +0x28, centre vec at +0x50 (documented ctor); +0x10 word 0 = stack storage.
        alignas(16) std::array<std::byte,96> shape{};
        alignas(16) const float centre[4]{};
        if(construct(shape.data(),centre,static_cast<float>(radius))!=shape.data()) return {};
        const std::uint16_t stack_storage=0;std::memcpy(shape.data()+0x10,&stack_storage,2);
        NativeCastFilter filter{};filter.vtable=base+filter_vtable_rva;filter.layer_mask=layers;filter.required_bits=required;
        const auto request=make_cast_request(from,to,broadphase,reinterpret_cast<std::uintptr_t>(&filter),
                                             reinterpret_cast<std::uintptr_t>(shape.data()));
        if(!request) return {};
        auto collector=make_closest_collector(base+collector_vtable_rva);
        alignas(16) const auto rotation=native_cast_identity;
        cast(reinterpret_cast<void*>(world),&*request,rotation.data(),&collector,0);
        // Native then runs filter/collector destructors (0xa9e0d0/0xa9e0b0); both only reset vtables and
        // notify an optional registry for heap objects, so they are intentionally skipped for stack objects.
        if(!collector.hit_count) return {};
        return convert_world_hit(collector,from,to,entity_of(read,world,collector.body));
    }
};

} // namespace d2vr
#ifdef _MSC_VER
#pragma warning(pop)
#endif
