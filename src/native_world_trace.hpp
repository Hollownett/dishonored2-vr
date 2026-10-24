#pragma once
#include <atomic>
// Typed adapter for the native synchronous physics-world shape cast (RVA 0xc89bc0)
// with a closest-hit collector. Evidence and unverified items: docs/native-world-trace.md.
#include "native_hand_volume.hpp"
#include "native_world_query_source.hpp"
#include "native_initial_contact.hpp"
#include "native_regular_contact.hpp"
#include "weapon_collision.hpp"
#include "hand_clamp.hpp"
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
    std::uint64_t shape_tag_codec{};
    std::uint64_t filter{};             // 0x08 NativeCastFilter* (cc99c0 passes world+0x470 here)
    std::uint16_t group{0xffff};        // 0x10
    std::uint16_t pad12{};
    std::uint32_t query_id{};
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
    std::uint64_t source_body{};
};
static_assert(sizeof(NativeCastRequest)==0x70 && alignof(NativeCastRequest)==16);
static_assert(offsetof(NativeCastRequest,filter)==0x08 && offsetof(NativeCastRequest,query_id)==0x14 &&
              offsetof(NativeCastRequest,kind)==0x20 && offsetof(NativeCastRequest,shape)==0x28 &&
              offsetof(NativeCastRequest,origin)==0x30 && offsetof(NativeCastRequest,delta)==0x40 &&
              offsetof(NativeCastRequest,inverse_delta)==0x50 && offsetof(NativeCastRequest,flags)==0x5c &&
              offsetof(NativeCastRequest,tolerance)==0x60 && offsetof(NativeCastRequest,source_body)==0x68);

// Stack filter object (vtable RVA 0x1ad4b68; slot 4 = 0x996d0 shouldCollide):
// collide iff (layer_mask >> (info & 31)) & 1 and ((info >> 5) & required) == required,
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
// Final review P3-1: the hand body's create / snap free probe also sees WORLD_LOW 0, which the hand body collides with (hand_body_commands.hpp
// mask 0x442f) and kWorldOccluderLayers does not include. Only the hand body's own probe uses it (NativeInteractionAim::hand_body_free_slot).
inline constexpr std::uint32_t kHandBodyFreeLayers=kWorldOccluderLayers|1u;
static_assert(kHandBodyFreeLayers==0xc43fu,"0xc43e plus WORLD_LOW 0");
// Not for our use (AI/entity probes, semantics unknown): 0x2f/0x22f(0x638800), 0x2e/0x800(0x83c860),
// 0x200/0x40(0x83a510), 0x22b/0x40(0x44bd90), 0x2b/0x40(0x3a2730).

// A body the cast must not report (request +0x68/+0x14, the native source-body exclusion: 129a310 skips a target whose full id
// equals the source body's). record: the body's pool record (pool + index*0xb0, as NativeWorldQuerySource::body); handle: its full
// id (record+0x70); filter_info: written to +0x14 (the player's lease writes its controller's query filter info there; a prop has
// none, so the hand clamp passes the body's own collision filter word, record+0x6c, or 0). UNVERIFIED in game: that walls still
// report through such a cast (hand clamp status "exclusion").
struct NativeCastExclusion {std::uintptr_t record{};std::uint32_t handle{},filter_info{};};

struct WorldTraceHit {
    Vec3 point{};            // native contact position on the hit surface
    Vec3 centre{};           // from + (to-from)*fraction: where the swept sphere stops
    Vec3 normal{};
    double fraction{};       // 0..1 along from->to
    std::uintptr_t entity{}; // idEntity* or 0 (unknown body)
    std::uint32_t body{};    // 24-bit physics body index
    bool initial_overlap{};  // fraction 0: the sphere started inside geometry
    bool normal_valid{true},static_support{true};
    std::uint16_t material{0xffff}; // collector +0x7c: physics material id of the hit surface (docs/native-impact-fx.md)
    std::uint32_t body_handle{0xffffff};
    std::uint32_t shape_key{0xffffffffu};
    BodyClass body_class{BodyClass::world};   // read from the hit body when the cast answered (HandClamp ranks planes by it)
    bool hand_body{};   // P1-1: a hand body reported because no continuation could see past it (blocked, never "no hit"; entity 0, dynamic)
};
struct WorldTraceInitialHit {
    NativeInitialContact contact{};
    std::uintptr_t entity{};
};
struct WorldTraceInitialBatch {
    std::array<WorldTraceInitialHit,32> hits{};
    unsigned count{};
};
struct WorldTraceRegularBatch {
    std::array<WorldTraceHit,32> hits{};
    unsigned count{};
};
struct WorldTraceQueryResult {
    bool complete{};
    std::optional<WorldTraceHit> hit;
    std::optional<WorldTraceInitialBatch> initial;
    std::optional<WorldTraceRegularBatch> regular;
};

inline float native_delta_clean(double v) {const float f=static_cast<float>(v);return f==0?0.f:f;}
inline std::optional<NativeCastRequest> make_cast_request(Vec3 from,Vec3 to,std::uint64_t shape_tag_codec,
        std::uint64_t filter,std::uint64_t shape) {
    const Vec3 d=to-from;
    if(!finite(from) || !finite(to) || !shape_tag_codec || !filter || !shape) return {};
    const double len=length(d);
    if(!(len>=1e-3) || len>50) return {};
    NativeCastRequest r{};
    r.shape_tag_codec=shape_tag_codec;r.filter=filter;r.shape=shape;
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
// Plan B B11/B13 (dev knobs build/hand-body-spike, build/hand-body): a hand body is DEBRIS (layer 4), which kWorldOccluderLayers includes, so
// every mod cast would report it. While a hand body exists its full handle is in its slot (the spike uses slot 0; the production hand body
// slot 0 for the left hand and 1 for the right; the two never run together). A hit on it is counted (cast_excluded_hits) and is never the
// answer, and never "no hit" either (final review P1-1): the cast continues past it (NativeWorldTrace::continue_past_hand_body, at most
// kMaxCastContinuations casts, counted in cast_continuations), so a wall, a floor or a table behind or under the hand is still reported.
// 0xffffffff (the default, knobs off) never matches: with no hand body alive no continuation runs and the path is the old one.
inline std::atomic<std::uint32_t> cast_excluded_bodies[2]{0xffffffffu,0xffffffffu};
inline std::atomic<std::uint64_t> cast_excluded_hits{0};
// P1-1: continuation casts run past a hand body; continuations that could not see past it (the limit, an unreadable record, an overfull
// collector): the hand-body hit itself was reported as the answer (blocked, never "no hit").
inline std::atomic<std::uint64_t> cast_continuations{0},cast_continuation_exhausted{0};
inline constexpr unsigned kMaxCastContinuations=2;
inline bool cast_excluded(std::uint32_t body) {
    for(const auto& slot:cast_excluded_bodies) if(const auto excluded=slot.load(std::memory_order_relaxed); excluded!=0xffffffffu && body==excluded) return true;
    return false;
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
    h.body=c.body&0xffffff;h.body_handle=c.body;h.entity=h.body==0xffffff?0:entity;
    h.initial_overlap=f<=1e-6;h.material=c.body_key;h.shape_key=c.shape_key;
    const Vec3 back=d*(-1/len);
    const double nl=length(raw);
    Vec3 n=back;
    h.normal_valid=finite(raw) && nl>.5 && nl<1.5;
    if(h.normal_valid) {n=raw*(1/nl);if(!h.initial_overlap && dot(n,d)>0) n=n*-1;}
    h.normal=n;return h;
}
// Feeds WeaponCollision::update's query(from,to). Pass the sphere cast radius = palm_radius
// ONLY if the policy's own palm_radius is set to 0 (otherwise the margin is applied twice).
inline CollisionHit to_collision_hit(const WorldTraceHit& h) {return {h.point,h.normal,h.fraction,h.entity,h.material};}
// Feeds HandClamp's palm_hit/tip_hit/chest_hit. Cast with the probe's own radius: the clamp keeps only skin on top.
inline ClampHit to_clamp_hit(const WorldTraceHit& h) {return {h.point,h.normal,h.entity,h.material,h.initial_overlap,h.normal_valid,h.body_class};}

class NativeWorldTrace {
public:
    using Read=bool (*)(std::uintptr_t,void*,std::size_t);
    using Cast=void (__fastcall*)(void*,const NativeCastRequest*,const float*,NativeClosestCollector*,std::uintptr_t);
    using Construct=NativeHandVolume::Construct; // 0xca6550(storage96, centre vec4*, radius)
    inline static constexpr std::uintptr_t cast_rva=0xc89bc0,filter_vtable_rva=0x1ad4b68,collector_vtable_rva=0x1ad5010,any_hit_collector_vtable_rva=0x1ad5040,
        filter_check_rva=0x996d0,filter_body_rva=0x996a0,filter_pair_rva=0x11d080,collector_add_rva=0xca53a0,any_hit_add_rva=0xca5460,sphere_ctor_rva=0xca6550,
        sphere_vtable_rva=0x1ec1e78,world_offset=0x60410,shape_tag_codec_offset=0x9d8,body_pool_offset=0x28;
    inline static constexpr std::array<unsigned char,48> cast_code{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48,0x89,0x7c,0x24,0x20,0x41,0x56,0x48,0x83,0xec,0x40,0x48,0x8b,0xf9,0x8b,0x0d,0x21,0x42,0x20,0x03,0x49,0x8b,0xf1,0x49,0x8b,0xe8,0x4c,0x8b,0xf2,0xff,0x15,0x06,0x47};
    inline static constexpr std::array<unsigned char,55> filter_code{0x4c,0x8b,0xd1,0x45,0x84,0xc0,0x74,0x0a,0x48,0x8b,0x44,0x24,0x28,0x8b,0x50,0x04,0xeb,0x04,0x41,0x8b,0x51,0x04,0x41,0x8b,0x42,0x20,0x8b,0xca,0x83,0xe1,0x1f,0x0f,0xa3,0xc8,0x73,0x10,0x41,0x8b,0x42,0x24,0xc1,0xea,0x05,0x23,0xd0,0x3b,0xd0,0x75,0x03,0xb0,0x01,0xc3,0x32,0xc0,0xc3};
    inline static constexpr std::array<unsigned char,38> filter_body_code{0x41,0x8b,0x41,0x6c,0x8b,0x51,0x20,0x44,0x8b,0xc0,0x41,0x83,0xe0,0x1f,0x44,0x0f,0xa3,0xc2,0x73,0x0f,0x8b,0x49,0x24,0xc1,0xe8,0x05,0x23,0xc1,0x3b,0xc1,0x75,0x03,0xb0,0x01,0xc3,0x32,0xc0,0xc3};
    inline static constexpr std::array<unsigned char,3> filter_pair_code{0xb0,0x01,0xc3};
    inline static constexpr std::array<unsigned char,32> collector_add_code{0xf3,0x0f,0x10,0x42,0x20,0x0f,0xc6,0xc0,0x00,0x0f,0x2e,0x41,0x20,0x0f,0x83,0xa8,0x00,0x00,0x00,0x0f,0x28,0x02,0x0f,0x29,0x41,0x30,0x0f,0x28,0x4a,0x10,0x0f,0x29};
    inline static constexpr std::array<unsigned char,32> any_hit_add_code{0xf3,0x0f,0x10,0x41,0x50,0x0f,0x2f,0x42,0x20,0x0f,0x86,0x95,0x00,0x00,0x00,0x0f,0x28,0x02,0x0f,0x29,0x41,0x30,0x0f,0x28,0x4a,0x10,0x0f,0x57,0xc0,0x0f,0x29,0x49};

    // Code bytes at every RVA used, plus the two vtables' add-hit slots (image base 0x140000000).
    static bool verify(std::uintptr_t base,Read read) {
        if(!base || !read) return false;
        const auto code=[&](std::uintptr_t rva,const auto& expect) {
            std::array<unsigned char,std::tuple_size<std::decay_t<decltype(expect)>>::value> got{};
            return read(base+rva,got.data(),got.size()) && got==expect;
        };
        const auto qword=[&](std::uintptr_t rva) {std::uint64_t v{};return read(base+rva,&v,8)?v:0;};
        return code(cast_rva,cast_code) && code(filter_check_rva,filter_code) &&
            code(filter_body_rva,filter_body_code) && code(filter_pair_rva,filter_pair_code) &&
            code(collector_add_rva,collector_add_code) && code(any_hit_add_rva,any_hit_add_code) &&
            code(sphere_ctor_rva,NativeHandVolume::constructor_code) &&
            qword(filter_vtable_rva+0x20)==base+filter_check_rva &&
            qword(filter_vtable_rva+0x28)==base+filter_pair_rva &&
            qword(filter_vtable_rva+0x30)==base+filter_body_rva &&
            qword(collector_vtable_rva+0x18)==base+collector_add_rva &&
            qword(any_hit_collector_vtable_rva+0x18)==base+any_hit_add_rva;
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
    struct CheckedHitBody {
        std::uintptr_t pool{},record{},userdata{},component{},entity{};
        std::uint32_t count{},handle{},broadphase{},entity_id{};
        std::uint8_t flags{};
    };
    // What the hit body is, from the body record, validated like inspect_hit_body and native_rigid_body: no body or no entity is the world; the
    // index must lie below the pool count (world+0x30), the record's own handle (+0x70) must be the cast's, it must have a motion object (+0x40,
    // under 0x100000), and flags & 7 == 2 (+0x44) makes it a dynamic body whatever its mass. Any other motion type is an entity that does not
    // move (static or keyframed). A record that does not check out (a freed or reused slot, a short pool, an unreadable word) is entity_static:
    // it must never rank below a prop on a stale read.
    static BodyClass body_class_of(Read read,std::uintptr_t world,std::uint32_t handle,std::uintptr_t entity) {
        const std::uint32_t index=handle&0xffffffu;
        if(handle==0xffffffffu || index==0xffffffu || !entity) return BodyClass::world;
        std::uintptr_t pool{};std::uint32_t count{},flags{},record_handle{},motion{};
        if(!read || !read(world+body_pool_offset,&pool,sizeof(pool)) || !rig::pointer(pool) ||
           !read(world+0x30,&count,sizeof(count)) || !count || count>0x1000000u || index>=count) return BodyClass::entity_static;
        const auto record=pool+static_cast<std::uintptr_t>(index)*0xb0u;
        if(!read(record+0x70,&record_handle,sizeof(record_handle)) || record_handle!=handle ||
           !read(record+0x40,&motion,sizeof(motion)) || !motion || motion>=0x100000u ||
           !read(record+0x44,&flags,sizeof(flags))) return BodyClass::entity_static;
        return (flags&7u)==2u?BodyClass::dynamic:BodyClass::entity_static;
    }
    template<class Reader> static std::optional<CheckedHitBody> inspect_hit_body(Reader& read,const NativeWorldQuerySource& source,std::uint32_t handle) {
        const std::uint32_t index=handle&0xffffffu;
        if(handle==0xffffffffu || index==0xffffffu)return {};
        CheckedHitBody snapshot{};snapshot.handle=handle;
        if(!read(source.world+body_pool_offset,&snapshot.pool,sizeof(snapshot.pool)) || !rig::pointer(snapshot.pool) ||
           !read(source.world+0x30,&snapshot.count,sizeof(snapshot.count)) || !snapshot.count || snapshot.count>0x1000000u || index>=snapshot.count)return {};
        const auto offset=static_cast<std::uintptr_t>(index)*0xb0u;
        if(offset>std::numeric_limits<std::uintptr_t>::max()-snapshot.pool)return {};
        snapshot.record=snapshot.pool+offset;
        if(!rig::pointer(snapshot.record) || !read(snapshot.record+0x44,&snapshot.flags,sizeof(snapshot.flags)) || (snapshot.flags&3u)==0 ||
           !read(snapshot.record+0x70,&snapshot.handle,sizeof(snapshot.handle)) || snapshot.handle!=handle ||
           !read(snapshot.record+0x78,&snapshot.broadphase,sizeof(snapshot.broadphase)) || snapshot.broadphase==0xffffffffu ||
           !read(snapshot.record+0x90,&snapshot.userdata,sizeof(snapshot.userdata)))return {};
        if(snapshot.userdata) {
            if(!rig::pointer(snapshot.userdata) || !read(snapshot.userdata+8,&snapshot.component,sizeof(snapshot.component)))return {};
            if(snapshot.component) {
                if(!rig::pointer(snapshot.component) || !read(snapshot.component+0x10,&snapshot.entity,sizeof(snapshot.entity)))return {};
                if(snapshot.entity) {
                    if(!rig::pointer(snapshot.entity) || !read(snapshot.entity+0xa8,&snapshot.entity_id,sizeof(snapshot.entity_id)) ||
                       !snapshot.entity_id || snapshot.entity_id==0xffffffffu || snapshot.entity_id==0x3fff0010u || (snapshot.entity_id>>16)>=0x3fffu)return {};
                    std::uintptr_t registered{};
                    if(!read(source.game+0x3130b0+static_cast<std::uintptr_t>(snapshot.entity_id>>16)*8,&registered,sizeof(registered)) || registered!=snapshot.entity)return {};
                }
            }
        }
        return snapshot;
    }
    template<class Reader> static std::optional<std::uintptr_t> checked_entity_of(Reader& read,const NativeWorldQuerySource& source,std::uint32_t handle) {
        const auto first=inspect_hit_body(read,source,handle);
        if(!first)return {};
        const auto confirmed=inspect_hit_body(read,source,handle);
        if(!confirmed || first->pool!=confirmed->pool || first->record!=confirmed->record || first->userdata!=confirmed->userdata ||
           first->component!=confirmed->component || first->entity!=confirmed->entity || first->count!=confirmed->count ||
           first->handle!=confirmed->handle || first->broadphase!=confirmed->broadphase || first->entity_id!=confirmed->entity_id || first->flags!=confirmed->flags)return {};
        return confirmed->entity;
    }
    // One sphere sweep from->to (world space, metres as the game uses them: UNIT SCALE UNVERIFIED).
    // radius must be > 0. Returns the closest hit among bodies accepted by the filter.
    static WorldTraceQueryResult sphere_query_checked(std::uintptr_t base,Read read,Cast cast,Construct construct,
            std::uintptr_t world,Vec3 from,Vec3 to,double radius,
            std::uint32_t layers=kWorldOccluderLayers,std::uint32_t required=kWorldOccluderRequired,const NativeWorldQuerySource* source=nullptr,
            bool collect_initial=false,const NativeCastExclusion* exclude=nullptr,bool classify=true) {   // classify: read the hit body's class (only the hand clamp ranks planes by it)
        if(!base || !read || !cast || !construct || !world || !(radius>0.0005) || radius>1) return {};
        if(collect_initial && !source)return {};
        if(exclude && (source || !rig::pointer(exclude->record) || (exclude->handle&0xffffffu)==0xffffffu))return {};
        if(source && (source->base!=base || source->world!=world || !native_world_query_source_current(read,*source)))return {};
        std::uint64_t shape_tag_codec{};
        if(!read(world+shape_tag_codec_offset,&shape_tag_codec,8)) return {};
        // Shape is rebuilt per call: radius lives at +0x28, centre vec at +0x50 (documented ctor); +0x10 word 0 = stack storage.
        alignas(16) std::array<std::byte,96> shape{};
        alignas(16) const float centre[4]{};
        if(construct(shape.data(),centre,static_cast<float>(radius))!=shape.data()) return {};
        const std::uint16_t stack_storage=0;std::memcpy(shape.data()+0x10,&stack_storage,2);
        NativeCastFilter filter{};filter.vtable=base+filter_vtable_rva;filter.layer_mask=layers;filter.required_bits=required;
        auto request=make_cast_request(from,to,shape_tag_codec,reinterpret_cast<std::uintptr_t>(&filter),
                                             reinterpret_cast<std::uintptr_t>(shape.data()));
        if(!request) return {};
        if(source) {request->source_body=source->body;request->query_id=source->query_filter_info;}
        else if(exclude) {request->source_body=exclude->record;request->query_id=exclude->filter_info;}
        auto collector=make_closest_collector(base+collector_vtable_rva);
        std::optional<NativeInitialContactCollector> initial_collector;NativeInitialContactTable initial_table{};
        std::optional<NativeRegularContactCollector> regular_collector;NativeInitialContactTable regular_table{};
        if(collect_initial) {
            initial_collector.emplace();
            if(!NativeInitialContactCollector::initialize(*initial_collector,initial_table,base,read) || !initial_collector->begin())return {};
            regular_collector.emplace();
            if(!NativeRegularContactCollector::initialize(*regular_collector,regular_table,base,read) || !regular_collector->begin())return {};
        }
        alignas(16) const auto rotation=native_cast_identity;
        cast(reinterpret_cast<void*>(world),&*request,rotation.data(),collect_initial?reinterpret_cast<NativeClosestCollector*>(regular_collector->native()):&collector,
            collect_initial?reinterpret_cast<std::uintptr_t>(&*initial_collector):0);
        if(collect_initial && (!initial_collector->finish() || !regular_collector->finish()))return {};
        if(source && !native_world_query_source_current(read,*source))return {};
        // Native then runs filter/collector destructors (0xa9e0d0/0xa9e0b0); both only reset vtables and
        // notify an optional registry for heap objects, so they are intentionally skipped for stack objects.
        WorldTraceQueryResult result{true,{}};
        if(collect_initial) {
            // P1-1: these collectors report every contact, so a hand body's own contacts are skipped (counted) and the rest is the answer.
            // No hand body alive: nothing is skipped and the counts are the collectors' own.
            result.initial.emplace();unsigned kept=0;
            for(unsigned index=0;index<initial_collector->count;++index) {
                const auto contact=initial_collector->contact(index);
                if(!contact || contact->source_body!=source->handle || contact->body==source->handle)return {};
                if(cast_excluded(contact->body)) {cast_excluded_hits.fetch_add(1,std::memory_order_relaxed);continue;}
                const auto entity=checked_entity_of(read,*source,contact->body);
                if(!entity)return {};
                result.initial->hits[kept++]={*contact,*entity};
            }
            result.initial->count=kept;
            if(!native_world_query_source_current(read,*source))return {};
            result.regular.emplace();kept=0;
            for(unsigned index=0;index<regular_collector->count();++index) {
                const auto contact=regular_collector->contact(index);
                if(!contact || contact->source_body!=source->handle || contact->body==source->handle)return {};
                if(cast_excluded(contact->body)) {cast_excluded_hits.fetch_add(1,std::memory_order_relaxed);continue;}
                const auto entity=checked_entity_of(read,*source,contact->body);
                if(!entity)return {};
                auto hit=convert_world_hit(contact_record(*contact),from,to,*entity);if(!hit)return {};
                if(classify)hit->body_class=body_class_of(read,world,hit->body_handle,hit->entity);
                hit->static_support=false;result.regular->hits[kept++]=*hit;
                if(!result.hit || hit->fraction<result.hit->fraction)result.hit=*hit;
            }
            result.regular->count=kept;
            if(!native_world_query_source_current(read,*source))return {};
            return result;
        }
        if(!collector.hit_count)return result;
        if(collector.hit_count!=1)return {};
        if(cast_excluded(collector.body)) {   // P1-1: a hand body is never the answer and never "no hit": look past it
            cast_excluded_hits.fetch_add(1,std::memory_order_relaxed);
            return continue_past_hand_body(base,read,cast,world,*request,collector,from,to,source,exclude,classify);
        }
        std::uintptr_t entity{};
        if(source) {
            if(collector.body==source->handle)return {};
            const auto resolved=checked_entity_of(read,*source,collector.body);
            if(!resolved || !native_world_query_source_current(read,*source))return {};
            entity=*resolved;
        } else entity=entity_of(read,world,collector.body);
        auto hit=convert_world_hit(collector,from,to,entity);
        if(hit && classify)hit->body_class=body_class_of(read,world,hit->body_handle,hit->entity);
        if(source && hit)hit->static_support=false;
        if(!hit)return {};
        result.hit=std::move(hit);return result;
    }
    static std::optional<WorldTraceHit> sphere_cast(std::uintptr_t base,Read read,Cast cast,Construct construct,
            std::uintptr_t world,Vec3 from,Vec3 to,double radius,
            std::uint32_t layers=kWorldOccluderLayers,std::uint32_t required=kWorldOccluderRequired,const NativeCastExclusion* exclude=nullptr,bool classify=true) {
        return sphere_query_checked(base,read,cast,construct,world,from,to,radius,layers,required,nullptr,false,exclude,classify).hit;
    }
    // A regular contact as the closest collector would have stored it (convert_world_hit's input).
    static NativeClosestCollector contact_record(const NativeRegularContact& contact) {
        NativeClosestCollector record{};record.hit_count=1;record.fraction=contact.fraction;
        record.body=contact.body;record.body_key=contact.material;record.shape_key=contact.shape_key;
        const double point[3]{contact.point.x,contact.point.y,contact.point.z};
        const double normal[3]{contact.normal.x,contact.normal.y,contact.normal.z};
        for(unsigned axis=0;axis<3;++axis) {record.position[axis]=static_cast<float>(point[axis]);record.normal[axis]=static_cast<float>(normal[axis]);}
        return record;
    }
    // A body's pool record checked the way body_class_of checks it (index below the count, the record's own full handle), and its collision
    // filter word (+0x6c; 0 unreadable), for the engine's source-body exclusion (NativeCastExclusion's fields).
    static std::optional<NativeCastExclusion> body_exclusion(Read read,std::uintptr_t world,std::uint32_t handle) {
        const std::uint32_t index=handle&0xffffffu;
        if(handle==0xffffffffu || index==0xffffffu) return {};
        std::uintptr_t pool{};std::uint32_t count{},stored{},info{};
        if(!read(world+body_pool_offset,&pool,sizeof(pool)) || !rig::pointer(pool) || !read(world+0x30,&count,sizeof(count)) || !count || count>0x1000000u || index>=count) return {};
        const auto record=pool+static_cast<std::uintptr_t>(index)*0xb0u;
        if(!rig::pointer(record) || !read(record+0x70,&stored,sizeof(stored)) || stored!=handle) return {};
        if(!read(record+0x6c,&info,sizeof(info))) info=0;
        return NativeCastExclusion{record,handle,info};
    }
    // Final review P1-1 (controller ruling): the closest hit was a hand body. The cast looks past it with at most kMaxCastContinuations more
    // casts of the same sweep (same shape, layers and caller exclusion), each counted in cast_continuations:
    // * the caller's exclusion slot free (no source lease, no excluded body): the native closest cast again with the hand body met as the
    //   request's source body (the engine's own exclusion; B4's sims: walls report through it, the excluded body never does);
    // * the slot taken (a held object's sweeps, the player lease), or that cast met the other hand: the mod's non-pruning all-hits collector
    //   (native_regular_contact.hpp) over the sweep, every hand body's contact skipped, the nearest other contact the answer (none: clear).
    // Both sweep from the original start: nothing lies nearer than the closest hit, so this is the continuation from the hit over the remaining
    // length, and a start inside geometry keeps the engine's initial-overlap label. When nothing could be seen past the hand (the limit, an
    // overfull or malformed collector) the hand-body hit itself is the answer (hand_body, entity 0, dynamic): blocked, never "no hit", counted
    // in cast_continuation_exhausted. Only reached through cast_excluded(), i.e. only while a hand body is alive.
    static WorldTraceQueryResult continue_past_hand_body(std::uintptr_t base,Read read,Cast cast,std::uintptr_t world,const NativeCastRequest& original,
            const NativeClosestCollector& first,Vec3 from,Vec3 to,const NativeWorldQuerySource* source,const NativeCastExclusion* exclude,bool classify) {
        const auto blocked=[&](const NativeClosestCollector& hand)->WorldTraceQueryResult {
            cast_continuation_exhausted.fetch_add(1,std::memory_order_relaxed);
            auto hit=convert_world_hit(hand,from,to,0);
            if(!hit) return {};
            hit->entity=0;hit->hand_body=true;hit->body_class=BodyClass::dynamic;hit->static_support=false;
            WorldTraceQueryResult blocked_result{true,{}};blocked_result.hit=std::move(hit);return blocked_result;
        };
        alignas(16) const auto rotation=native_cast_identity;
        unsigned used=0;NativeClosestCollector last=first;
        if(!source && !exclude) if(const auto hand=body_exclusion(read,world,first.body)) {
            NativeCastRequest request=original;request.source_body=hand->record;request.query_id=hand->filter_info;
            auto collector=make_closest_collector(base+collector_vtable_rva);
            ++used;cast_continuations.fetch_add(1,std::memory_order_relaxed);
            cast(reinterpret_cast<void*>(world),&request,rotation.data(),&collector,0);
            if(!collector.hit_count) return WorldTraceQueryResult{true,{}};   // clear past the hand
            if(collector.hit_count!=1) return {};                             // malformed, as a first cast's
            if(!cast_excluded(collector.body)) {                              // what lies behind the hand, reported as a first cast's hit is
                auto hit=convert_world_hit(collector,from,to,entity_of(read,world,collector.body));
                if(!hit) return {};
                if(classify) hit->body_class=body_class_of(read,world,hit->body_handle,hit->entity);
                WorldTraceQueryResult result{true,{}};result.hit=std::move(hit);return result;
            }
            cast_excluded_hits.fetch_add(1,std::memory_order_relaxed);last=collector;   // the other hand (or the exclusion did not apply)
        }
        if(used>=kMaxCastContinuations) return blocked(last);
        NativeRegularContactCollector all{};NativeInitialContactTable table{};
        if(!NativeRegularContactCollector::initialize(all,table,base,read) || !all.begin()) return blocked(last);
        ++used;cast_continuations.fetch_add(1,std::memory_order_relaxed);
        cast(reinterpret_cast<void*>(world),&original,rotation.data(),reinterpret_cast<NativeClosestCollector*>(all.native()),0);
        if(!all.finish()) return blocked(last);
        if(source && !native_world_query_source_current(read,*source)) return {};
        WorldTraceQueryResult result{true,{}};
        for(unsigned index=0;index<all.count();++index) {
            const auto contact=all.contact(index);
            if(!contact) return blocked(last);
            if(cast_excluded(contact->body)) continue;   // a hand body, either side
            std::uintptr_t entity{};
            if(source) {   // the player lease: checked as its own collectors are
                if(contact->source_body!=source->handle || contact->body==source->handle) return {};
                const auto resolved=checked_entity_of(read,*source,contact->body);
                if(!resolved) return {};
                entity=*resolved;
            } else entity=entity_of(read,world,contact->body);
            auto hit=convert_world_hit(contact_record(*contact),from,to,entity);
            if(!hit) return {};
            if(classify) hit->body_class=body_class_of(read,world,hit->body_handle,hit->entity);
            if(source) hit->static_support=false;
            if(!result.hit || hit->fraction<result.hit->fraction) result.hit=*hit;
        }
        if(source && !native_world_query_source_current(read,*source)) return {};
        return result;
    }
};

} // namespace d2vr
#ifdef _MSC_VER
#pragma warning(pop)
#endif
