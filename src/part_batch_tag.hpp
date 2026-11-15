#pragma once
// v1318 (user, v1317 headset: "the upgrades of the pistol and the crossbow are still not synchronised ... render them the same way as the
// weapon base"): with TWO identical weapons out (the original and a dual-wield copy) the engine draws their identical rigid parts (bolt,
// upgrade rings and mechanisms) as ONE instanced draw whose world rows come from its dynamic instance buffer (v1317 status: 127170 held
// 2-instance draws, 0 with one instance; sim, pistol pair: 12248); with one weapon each part is its own draw with its matrix in cb2, which
// the draw-time placement re-places exactly like the body (the user, v1315: "with only the original crossbow everything is perfect").
// Engine (static RE 2026-11-14): GenerateInstanceBuffer 0x335360 & co. merge the surfaces of different models whose batching key is equal
// (built by 0x359bd0, compared by 0x33a590): material, model asset (+0x1bb0), mesh buffers, a mergeable byte, a winding bit and the
// model's SHADER-PARM BLOCK (model+0x1950: vec4 values +0x00 / packed count +0x08, u16 parm ids +0x10 / count +0x18), compared bitwise
// by 0x341fb0. A group of one member takes the plain draw (0x335c23). A held part's block is EMPTY (sim: 30071 of 30071), so it gets one
// entry of its own, set on the GAME thread with the engine's own setter 0x343990 (the crossbow's equip 0x6303a0 sets its body's parms the
// same way): a parm its material already has (material+0x50, same block layout), with the material's own value but the lowest mantissa
// bits of one component xor-ed with a per-model tag (1..63, < 8e-6 relative). Whichever of model or material wins a parm lookup (the
// getter chain 0x342640 / 0x342c30: default < model < material), the shader reads the same value; the id is a real one (the block printer
// 0x344430 and parm_by_id 0x310dd0 index the parm table without a bound); the part never merges and is drawn alone, placed like the body.
// Tags are never taken back (a model that left the hand may be freed); a counter keeps a dropped thing and the next one held apart.
// build/legacy-part-batching: off (v1317).
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace d2vr::part_batch {
inline constexpr std::uintptr_t block_offset=0x1950;   // model + this: the shader-parm block
inline constexpr std::uintptr_t surfaces_offset=0x1c00; // model + this: -> {surface data, packed count}; surface stride 0x80, material at +0
inline constexpr std::uintptr_t material_block=0x50;   // material + this: its parm block
inline constexpr std::uintptr_t parm_table=0x32f3538;  // RVA of the parm manager pointer; parm(id) = *(mgr + 0xc290 + id*8) (0x310dd0)
inline constexpr std::uintptr_t setter=0x343990;       // RVA: (block*, parm*, const float* vec4) - finds the id and writes in place, else appends
inline constexpr std::uint32_t max_tag=63;
// Set by the DLL (dxgi_probe.cpp); called on the GAME thread each service while in gameplay (native_interaction_aim.hpp).
inline std::atomic<void(*)() noexcept> game_service{};
struct Vec4 {std::uint32_t c[4]{};};
struct Parm {std::uint16_t id{};Vec4 value{};};
inline bool normal_float(std::uint32_t b) noexcept {const std::uint32_t e=(b>>23)&0xffu;return e!=0 && e!=0xffu;}
// The parm to give a model: present in EVERY surface's material with the same bits (so the model's value is the material's for each
// surface), with a normal component to tag. v1320: else one present in every surface's material whatever its values (the material wins
// every lookup, so no surface reads the model's value either; sim: the tranquilizer bolt's 4 materials share no parm bit for bit and the
// part stayed untagged, batched with its twin and drawn at the game's pose). -1: none.
inline int choose(const std::vector<std::vector<Parm>>& surfaces,Parm& out,unsigned& component) noexcept {
    if(surfaces.empty()) return -1;
    for(const bool same_bits:{true,false})
        for(const Parm& p:surfaces[0]) {
            bool everywhere=true;
            for(std::size_t s=1;s<surfaces.size() && everywhere;++s) {
                bool found=false;
                for(const Parm& q:surfaces[s]) if(q.id==p.id) {found=!same_bits || std::memcmp(q.value.c,p.value.c,16)==0;break;}
                everywhere=found;
            }
            if(!everywhere) continue;
            for(unsigned c=0;c<4;++c) if(normal_float(p.value.c[c])) {out=p;component=c;return p.id;}
        }
    return -1;
}
inline Vec4 tagged(const Vec4& v,unsigned component,std::uint32_t tag) noexcept {Vec4 t=v;t.c[component&3u]^=tag;return t;}
// Per model: the parm it was given, the bits written and its tag (from a counter; a model not in the table reuses the oldest entry).
// v1320: an entry is the model's address AND what it draws (asset +0x1bb0, surface list +0x1c00 and count): a freed model's address is
// reused by the next one (sim: an incendiary bolt kept the tranquilizer's parm choice, writing a parm one of its materials lacks), so a
// changed shape is a new entry.
// A renewed entry remembers the parm it had written (stale_id/stale_written): if the block still holds exactly those bits the service sets
// that parm back to its default, so a surface whose material lacks it reads what it read before any tag.
struct Entry {std::uintptr_t model{},asset{},surfaces{};std::uint32_t nsurfaces{};std::uint16_t id{};std::uint8_t component{};bool chosen{},none{};Vec4 orig{},written{};std::uint32_t tag{};std::uint64_t used{};
    bool stale{};std::uint16_t stale_id{};Vec4 stale_written{};};
template<std::size_t N> Entry& entry(std::array<Entry,N>& t,std::uint32_t& counter,std::uintptr_t model,std::uint64_t now,std::uintptr_t asset=0,std::uintptr_t surfaces=0,std::uint32_t nsurfaces=0) noexcept {
    Entry* old=&t[0];bool renew=false;
    for(auto& e:t) {
        if(e.model==model) {
            if(e.asset==asset && e.surfaces==surfaces && e.nsurfaces==nsurfaces) {e.used=now;return e;}
            old=&e;renew=true;break;   // the same address now draws another shape: renew it in place
        }
        if(e.used<old->used) old=&e;
    }
    const bool had=renew && old->chosen;const std::uint16_t had_id=old->id;const Vec4 had_bits=old->written;
    counter=counter%max_tag+1;
    *old=Entry{};old->model=model;old->asset=asset;old->surfaces=surfaces;old->nsurfaces=nsurfaces;old->tag=counter;old->used=now;
    if(had) {old->stale=true;old->stale_id=had_id;old->stale_written=had_bits;}
    return *old;
}
}
