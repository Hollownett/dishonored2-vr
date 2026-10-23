#pragma once
// Plan B, task B8: pure command-record encoders, filter words and the mutual-collision predicate for the hand body.
// No engine call. Layouts come from docs/native-hand-body-re.md (b6-evidence/r14, the 0x8a730 printer of the 0x89fb0 queue)
// and docs/superpowers/specs/2026-10-18-physical-hands-research.md section 2.4-2.6.
#include "arm_ik.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>

namespace d2vr::hand_body {

// Layer numbers: research 2.4 (enum near 0x19f0c60; 0-11 verified in the bytes).
inline constexpr unsigned layer_world_low=0,layer_world_low_high=1,layer_world_high=2,layer_rigid_body=3,layer_debris=4,layer_mover=5,
    layer_player=6,layer_player_hitbox=7,layer_player_dead=8,layer_npc=9,layer_npc_body_animated=10,layer_npc_body_ragdoll=14,
    layer_interactable=15,layer_player_physics=18,layer_pickup=23;

// Record header [V] (research 2.6, from 0x18d020/0x97600): u16 size @+0, u16 0x0200 @+2, u16 type @+4, body handle @+8,
// payload from +0x0c/+0x10. The handle is the full 32-bit handle (generation included); the dispatcher masks it with 0xffffff.
struct CommandRecord {std::array<unsigned char,0x40> bytes{};std::size_t size{};};

namespace detail {
inline void put16(CommandRecord& r,std::size_t at,std::uint16_t v){std::memcpy(r.bytes.data()+at,&v,sizeof v);}
inline void put32(CommandRecord& r,std::size_t at,std::uint32_t v){std::memcpy(r.bytes.data()+at,&v,sizeof v);}
inline void putf(CommandRecord& r,std::size_t at,float v){std::memcpy(r.bytes.data()+at,&v,sizeof v);}
inline float finite_float(double v){
    const float f=static_cast<float>(v);   // a finite double above FLT_MAX becomes inf here and is refused too
    if(!std::isfinite(v) || !std::isfinite(f)) throw std::invalid_argument("hand_body: non-finite command component");
    return f;
}
inline CommandRecord header(std::uint16_t size,std::uint16_t type,std::uint32_t body){
    CommandRecord r;r.size=size;put16(r,0,size);put16(r,2,0x0200);put16(r,4,type);put32(r,8,body);return r;
}
}

// Type 9 setBodyVelocity [V]: {0x30, 0x0200, 9, id@8, linear (vec4) @0x10, angular (vec4) @0x20}; w lanes stay 0.
// Research 2.6; the 0x8a730 printer reads the two vectors at +0x10 and +0x20 (b6-evidence/r14, case 9).
// Throws std::invalid_argument on any NaN/inf component (Review Focus 3): garbage never reaches a record.
inline CommandRecord encode_set_velocity(std::uint32_t body,Vec3 linear,Vec3 angular){
    const float v[6]={detail::finite_float(linear.x),detail::finite_float(linear.y),detail::finite_float(linear.z),
                      detail::finite_float(angular.x),detail::finite_float(angular.y),detail::finite_float(angular.z)};
    auto r=detail::header(0x30,9,body);
    for(unsigned i=0;i<3;++i){detail::putf(r,0x10+4*i,v[i]);detail::putf(r,0x20+4*i,v[3+i]);}
    return r;
}

// Type 0x1a setBodyCollisionFilterInfo [V]: {0x20, 0x0200, 0x1a, id@8, filter u32 @0xc, u8 0 @0x10}. Research 2.4 (0x97600 pushes
// size 0x20, type 0x1a, body, filter) and the b6-evidence/r12 builder (local_28=0x20, local_26=0x1a0200, id@+8, filter@+0xc,
// byte @+0x10 = 0); the printer (r14, case 0x1a) reads id@8 and filter@0xc.
inline CommandRecord encode_set_filter(std::uint32_t body,std::uint32_t filter){
    auto r=detail::header(0x20,0x1a,body);detail::put32(r,0xc,filter);return r;
}

// Deliberately absent (no [V] payload and no in-game use): add (1) and destroy (2) records. Q3: there is no queued createBody, so the
// hand body is created directly under gate G1. Also absent: setBodyMotionType (0x11; the printer shows a type byte @0xc and no
// activation field, record size unread), setBodyTransform (6; extent of the transform at +0x10 unread) and applyHardKeyFrame (0x32;
// position @0x10 and orientation @0x20 are read by the printer, but the dt @0x30 and the record size rest on research prose only).
// A later task adds one only with its payload pinned from the executor.

// Filter word: layer in bits 0-4, collides-with mask in bits 5-31 (research 2.4; 0x992c0 reads word & 0x1f and word >> 5).
constexpr std::uint32_t filter_word(unsigned layer,std::uint32_t collides_with_mask){
    return (layer&0x1fu)|(collides_with_mask<<5);
}
constexpr unsigned word_layer(std::uint32_t word){return word&0x1fu;}
constexpr std::uint32_t word_mask(std::uint32_t word){return word>>5;}

constexpr std::uint32_t layer_bit(unsigned layer){return 1u<<layer;}

// What the hand body collides with. Base: WORLD_LOW 0, WORLD_LOW_HIGH 1, WORLD_HIGH 2, RIGID_BODY 3, MOVER 5, NPC_BODY_ANIMATED 10,
// NPC_BODY_RAGDOLL 14 (the RE doc's recommended mask 0x443f minus DEBRIS), plus DEBRIS 4 only when hit_debris.
// INTERACTABLE 15 is NOT included: the RE doc (Q1 item 4, recommendation 0x000887e4) recommends mask 0x443f, which lacks bit 15,
// and keeps the hand away from the non-zero-filter queries of other systems; the research's early "layers 0-5, 10, 14, 15" test
// predates that recommendation. Evidence wins, so hand_collides_with(true) == 0x443f and filter_word(4,0x443f) == 0x000887e4.
// Never 6, 7, 8, 18 (player bodies) or 23 (PICKUP).
constexpr std::uint32_t hand_collides_with(bool hit_debris){
    return layer_bit(layer_world_low)|layer_bit(layer_world_low_high)|layer_bit(layer_world_high)|layer_bit(layer_rigid_body)|
           layer_bit(layer_mover)|layer_bit(layer_npc_body_animated)|layer_bit(layer_npc_body_ragdoll)|
           (hit_debris?layer_bit(layer_debris):0u);
}

// The 0x992c0 mutual test without the disabled-pair / same-owner check (0x93a40): both directions must agree.
constexpr bool layers_collide(std::uint32_t a,std::uint32_t b){
    return (((b>>5)>>(a&0x1fu))&1u) && (((a>>5)>>(b&0x1fu))&1u);
}

// The hand's own word: DEBRIS layer plus hand_collides_with.
constexpr std::uint32_t hand_filter_word(bool hit_debris){return filter_word(layer_debris,hand_collides_with(hit_debris));}

} // namespace d2vr::hand_body
