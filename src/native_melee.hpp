#pragma once
// Contact sword hits through the engine's own damage path (docs/native-melee-damage.md, static RE 2026-10-12):
// idEntity::TakeDamage (vtable slot 40, 0x879270 for arkNPC) with an arkDamageParams block built by the engine
// ctor from the sword's own damage decl, inflictor = sword entity (AI alert), instigator = player (kill credit).
// The victim then joins the weapon's already-hit list (0x630540) so the native active frames of the same swing
// skip it. Call ONLY from the game-thread Stage0 scope (NativeInteractionAim), never from render/XR threads.
#include "game_rig.hpp"
#include "../third_party/minhook/include/MinHook.h"
#include <atomic>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <vector>
#include <tuple>
#include <type_traits>
#include <windows.h>

namespace d2vr {
struct MeleeHitRequest {
    std::uintptr_t victim{};
    std::array<float,3> point{},to_attacker{},direction{}; // game world (Z up); unit vectors
    std::uint64_t tick{};
};
class NativeMelee {
public:
    struct DamageResultObservation {
        std::array<unsigned char,32> raw{};
        float computed_damage{},resource_damage{};
        std::uint32_t modifier_mask{};
        std::uintptr_t modifier_payload{};
        std::uint8_t flags{};
        bool returned_buffer{};
    };
    static DamageResultObservation decode_damage_result(const void* bytes,bool returned_buffer) noexcept {
        DamageResultObservation observation{};
        std::memcpy(observation.raw.data(),bytes,observation.raw.size());
        std::memcpy(&observation.computed_damage,observation.raw.data(),4);
        std::memcpy(&observation.resource_damage,observation.raw.data()+4,4);
        std::memcpy(&observation.modifier_mask,observation.raw.data()+8,4);
        std::memcpy(&observation.modifier_payload,observation.raw.data()+0x10,8);
        observation.flags=observation.raw[0x18];observation.returned_buffer=returned_buffer;return observation;
    }
    using Read=bool (*)(std::uintptr_t,void*,std::size_t);
    inline static constexpr std::uintptr_t ctor_rva=0x780260,take_rva=0x879270,hitlist_rva=0x630540,npc_vtable_rva=0x1c5e108,
        inventory_vtable_rva=0x1b9a878,melee_vtable_rva=0x1b9d508,game_rva=0x39a5290,entities_offset=0x3130b0;
    inline static constexpr std::uint32_t invalid_id=0x3fff0010;
    inline static constexpr std::array<unsigned char,48> ctor_code{0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x48,0x89,0x11,0x8b,0x42,0x5c,0x48,0x8b,0xf9,0x89,0x41,0x08,0x48,0x8b,0x42,0x60,0x48,0x8b,0xda,0x48,0x89,0x41,0x10,0x8b,0x42,0x68,0x89,0x41,0x18,0x8b,0x42,0x6c,0x89,0x41,0x1c,0x33,0xc9,0x89};
    inline static constexpr std::array<unsigned char,48> take_code{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0xda,0x49,0x8b,0xf0,0x48,0x8d,0x15,0xd4,0x0e,0x14,0x03,0x41,0xb8,0x02,0x00,0x00,0x00,0xe8,0x89,0xe4,0xff,0xff,0x48,0x8b,0xcb,0x48,0x8b,0xf8,0xe8,0xce,0x71};
    inline static constexpr std::array<unsigned char,32> hitlist_code{0x89,0x54,0x24,0x10,0x48,0x83,0xec,0x28,0x4c,0x8d,0x91,0xd0,0x33,0x00,0x00,0x45,0x33,0xc0,0x41,0x8b,0x42,0x08,0x41,0x8b,0xc8,0x25,0xff,0xff,0xff,0x00,0xff,0xc8};

    static bool verify(std::uintptr_t base,Read read) {
        if(!base || !read) return false;
        const auto code=[&](std::uintptr_t rva,const auto& expect) {
            std::array<unsigned char,std::tuple_size<std::decay_t<decltype(expect)>>::value> got{};
            return read(base+rva,got.data(),got.size()) && got==expect;
        };
        std::uint64_t slot{};
        // take_hooked: NativeSimInvulnerable (tests) replaced the prologue with its jump after verifying these bytes.
        return code(ctor_rva,ctor_code) && (take_hooked.load() || code(take_rva,take_code)) && code(hitlist_rva,hitlist_code) &&
            read(base+npc_vtable_rva+0x140,&slot,8) && slot==base+take_rva;
    }
    // weapon 0: no weapon hit list (a thrown prop); amount 0: the decl's own m_maxDamage. unaware_amount > 0 replaces the amount
    // when the engine's own stealth test (unaware() below) says the victim has not noticed the player.
    struct Sword {std::uintptr_t player{},entity{},weapon{},decl{};std::uint32_t player_id{},sword_id{};float amount{},unaware_amount{};};
    // The right-hand item of the player's inventory (arkCpntInventory +0x68, as native_sword_equipment_ownership)
    // -> its entity -> the arkCpntWeaponMeleePlayer component -> weapon decl +0x120 (damage/player_sword).
    static std::optional<Sword> sword(std::uintptr_t base,Read read) {
        using namespace rig;
        const auto game=value<std::uintptr_t>(read,base+game_rva);if(!game || !pointer(*game)) return {};
        Sword s{};
        const auto player=value<std::uintptr_t>(read,*game+entities_offset);if(!player || !pointer(*player)) return {};
        s.player=*player;
        const auto pid=value<std::uint32_t>(read,s.player+0xa8);if(!pid || *pid==invalid_id) return {};
        s.player_id=*pid;
        std::uintptr_t inventory{};
        for(const auto at:components(read,s.player)) if(typed(read,base,at,inventory_vtable_rva)) {inventory=at;break;}
        if(!inventory) return {};
        const auto item=value<std::uintptr_t>(read,inventory+0x68);if(!item || !pointer(*item)) return {};
        const auto id=value<std::uint32_t>(read,*item+8);
        if(!id || *id==invalid_id || (*id>>16)>=0x3fff) return {};
        const auto entity=value<std::uintptr_t>(read,*game+entities_offset+(*id>>16)*8ull);
        if(!entity || !pointer(*entity) || value<std::uint32_t>(read,*entity+0xa8)!=id) return {};
        s.entity=*entity;s.sword_id=*id;
        for(const auto at:components(read,s.entity)) if(typed(read,base,at,melee_vtable_rva) && value<std::uintptr_t>(read,at+0x10)==s.entity) {s.weapon=at;break;}
        if(!s.weapon) return {};
        const auto wdecl=value<std::uintptr_t>(read,s.weapon+8);if(!wdecl || !pointer(*wdecl)) return {};
        const auto decl=value<std::uintptr_t>(read,*wdecl+0x120);if(!decl || !pointer(*decl)) return {};
        s.decl=*decl;return s;
    }
    // Live, damageable NPC: arkNPC vtable, has health (+0xd7 bit1), not dead (+0xd1 bit 0x08).
    static bool damageable(std::uintptr_t base,Read read,std::uintptr_t victim) {
        using namespace rig;
        const auto health=value<std::uint8_t>(read,victim+0xd7),flags=value<std::uint8_t>(read,victim+0xd1);
        return typed(read,base,victim,npc_vtable_rva) && health && (*health&2) && flags && !(*flags&8);
    }
    // Bloodfly/rat swarm agents (arkSwarmAgentEntity) have no health component, so TakeDamage does nothing to them. The engine's
    // melee kills one with 0x6b8b70(agent record, damage params from the swarm decl at *(base+0x231f330), 0) (callers 0x62f86b,
    // 0x626120); the record is the pooled agent data at agent entity +0x360 (vtable 0x1c417d8, +0x10 entity, +0x18 swarm,
    // +0x84 state, 7 = dead; live v384: the possession component is not it and faulted).
    // docs/native-attack-gating.md "Bloodfly swarms"; live trace 2026-10-13.
    inline static constexpr std::uintptr_t swarm_kill_rva=0x6b8b70,swarm_decl_rva=0x231f330,swarm_agent_vtable_rva=0x1c44ce8,swarm_record_vtable_rva=0x1c417d8;
    inline static std::atomic<bool> swarm_kill_hooked{};
    inline static std::atomic<bool> take_hooked{};
    inline static std::array<std::atomic<std::uint64_t>,4> swarm_why{}; // rejections: code pin, no record, already dead, no decl
    inline static constexpr std::array<unsigned char,32> swarm_kill_code{0x48,0x8b,0xc4,0x53,0x56,0x41,0x56,0x48,0x83,0xec,0x70,0x83,0x3d,0x26,0x4a,0x2d,0x03,0x00,0x45,0x8b,0xf0,0x48,0x8b,0xda,0x48,0x8b,0xf1,0x0f,0x85,0xc9,0x02,0x00};
    // A live, visible swarm agent (dormant pooled agents are hidden: +0xd0 bit 0).
    static bool swarm_agent(std::uintptr_t base,Read read,std::uintptr_t e) {
        const auto flags=rig::value<std::uint8_t>(read,e+0xd0);
        return rig::typed(read,base,e,swarm_agent_vtable_rva) && flags && !(*flags&1);
    }
    // Corpse chopping (static RE 2026-10-13). Damage on a dead NPC severs limbs only through arkCpntSeverable's dead-damage
    // listener (event 0x1a6 -> 0x763780 -> 0x75fea0(severable, params)), and the engine unsubscribes it once
    // m_deadSeveringCooldown (decl +0x9c, counted down at cpnt +0xb0 in 0x763673) runs out; a loaded save never re-subscribes
    // it. So a blade on a corpse calls 0x75fea0 itself with generic_fatality params: BY_RANDOM_COUNT min 1, max 2 with the
    // default chanceOfSeveringMore 0 (decl ctor 0x780490) = exactly one limb, the severable joint nearest params +0x24 (hit point),
    // chanceOfDestroying 0 = it drops whole. 0x75fea0 itself checks params +0x88 bit 2 (decl has sever settings) and the dead flag.
    inline static constexpr std::uintptr_t sever_rva=0x75fea0,sever_pin_rva=0x75ff0f,severable_vtable_rva=0x1c59038,damage_decl_type_rva=0x3990b60,damage_decl_info_rva=0x1c6f1e8,find_decl_rva=0x1e030;
    inline static constexpr std::array<unsigned char,48> sever_code{0x49,0x8b,0x97,0x98,0x00,0x00,0x00,0x48,0x89,0x54,0x24,0x68,0x41,0xf6,0x85,0x88,0x00,0x00,0x00,0x02,0x0f,0x84,0xdb,0x05,0x00,0x00,0xf6,0x83,0xd1,0x00,0x00,0x00,0x08,0x0f,0x84,0xce,0x05,0x00,0x00,0x48,0x85,0xc0,0x0f,0x84,0xc5,0x05,0x00,0x00};
    inline static std::atomic<std::uint64_t> chops{};
    static bool corpse(std::uintptr_t base,Read read,std::uintptr_t victim) {
        const auto flags=rig::value<std::uint8_t>(read,victim+0xd1);
        return rig::typed(read,base,victim,npc_vtable_rva) && flags && (*flags&8);
    }
    // 0 = applied, 1 = rejected (not damageable / no sword), 2 = engine fault (caller should disable the feature).
    static int apply(std::uintptr_t base,Read read,const Sword& s,const MeleeHitRequest& hit,DamageResultObservation* observation=nullptr) {
        if(hit.victim && swarm_agent(base,read,hit.victim)) return kill_swarm_agent(base,read,s,hit);
        if(hit.victim && hit.victim!=s.player && corpse(base,read,hit.victim)) return chop(base,read,s,hit);
        if(!hit.victim || hit.victim==s.player || !damageable(base,read,hit.victim)) return 1;
        const auto victim_id=rig::value<std::uint32_t>(read,hit.victim+0xa8);if(!victim_id || *victim_id==invalid_id) return 1;
        if(s.unaware_amount>0) {++unaware_asked;if(unaware(base,read,hit.victim)) {++unaware_yes;Sword k=s;k.amount=s.unaware_amount;return invoke(base,k,hit,*victim_id,observation);}}
        return invoke(base,s,hit,*victim_id,observation);
    }
    // isUnaware(npc) 0x5bf560, the engine's own stealth test (static RE 2026-10-14, artifacts/claude/sword-stealth-kill): the one
    // behind the assassination rules' assassination.target.isUnaware (0x3b09b0), the choke sensor (0x3a6980), the slash ->
    // Assassination choice (0x86b050) and the unaware damage scaling (0x719d20). True when the NPC is not hostile to the player
    // (relation matrix game+0x1f7bb8 <= 1), or alive, conscious and his AI brain flees/begs or is not alerted to the player
    // (knowledge kind 0 level >= 4 = aware). Pure reads; game thread, before the damage (the hit itself alerts him).
    inline static std::atomic<unsigned> unaware_asked{},unaware_yes{}; // thrown-sword hits that asked the stealth test / were unaware
    inline static constexpr std::uintptr_t unaware_rva=0x5bf560;
    inline static constexpr std::array<unsigned char,32> unaware_code{0x40,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0xf9,0x48,0x8b,0x0d,0x20,0x5d,0x3e,0x03,0xe8,0x2b,0x96,0x2e,0x00,0x48,0x85,0xc0,0x74,0x26,0x48,0x8b,0x0d,0x0f,0x5d,0x3e};
    static std::optional<bool> unaware_result(std::uintptr_t base,Read read,std::uintptr_t npc) {
        std::array<unsigned char,32> got{};
        if(!read(base+unaware_rva,got.data(),got.size()) || got!=unaware_code)return {};
        bool answer{};if(!ask_unaware(base,npc,answer))return {};
        return answer;
    }
    static bool unaware(std::uintptr_t base,Read read,std::uintptr_t npc) {
        return unaware_result(base,read,npc).value_or(false);
    }
private:
    static bool ask_unaware(std::uintptr_t base,std::uintptr_t npc,bool& answer) {
        __try {answer=reinterpret_cast<bool (__fastcall*)(std::uintptr_t)>(base+unaware_rva)(npc);return true;} __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    template<class R> static std::vector<std::uintptr_t> components(R read,std::uintptr_t entity) {
        std::vector<std::uintptr_t> out;
        const auto l=rig::list(read,entity+0x268,4096);if(!l || !l->packed_count) return out;
        out.resize(l->packed_count);if(!read(l->data,out.data(),out.size()*8)) out.clear();
        return out;
    }
    static int kill_swarm_agent(std::uintptr_t base,Read read,const Sword& s,const MeleeHitRequest& hit) {
        using namespace rig;
        std::array<unsigned char,32> got{}; // hooked by NativeSwarmKillGuard: its prologue is the hook's jump, verified at install
        if(!swarm_kill_hooked.load() && (!read(base+swarm_kill_rva,got.data(),got.size()) || got!=swarm_kill_code)) {++swarm_why[0];return 1;}
        const auto at=value<std::uintptr_t>(read,hit.victim+0x360); // the agent's record in the swarm pool (stride 0x100)
        const std::uintptr_t record=at && pointer(*at) && typed(read,base,*at,swarm_record_vtable_rva) && value<std::uintptr_t>(read,*at+0x10)==hit.victim?*at:0;
        const auto state=record?value<std::uint32_t>(read,record+0x84):std::nullopt;
        const auto decl=value<std::uintptr_t>(read,base+swarm_decl_rva);
        if(!record) {++swarm_why[1];return 1;}
        if(!state || *state==7) {++swarm_why[2];return 1;}
        if(!decl || !pointer(*decl)) {++swarm_why[3];return 1;}
        return kill_record(base,s,hit,record,*decl);
    }
    static int kill_record(std::uintptr_t base,const Sword& s,const MeleeHitRequest& hit,std::uintptr_t record,std::uintptr_t decl) {
        alignas(16) unsigned char params[0x90]{};
        using Ctor=void (__fastcall*)(void*,std::uintptr_t);
        using Kill=void (__fastcall*)(std::uintptr_t,void*,int);
        __try {
            reinterpret_cast<Ctor>(base+ctor_rva)(params,decl);
            std::memcpy(params+0x24,hit.point.data(),12);std::memcpy(params+0x30,hit.to_attacker.data(),12);
            std::memcpy(params+0x3c,hit.direction.data(),12);
            std::memcpy(params+0x50,&s.sword_id,4);std::memcpy(params+0x54,&s.player_id,4);
            reinterpret_cast<Kill>(base+swarm_kill_rva)(record,params,0);
        } __except(EXCEPTION_EXECUTE_HANDLER) {return 2;}
        return 0;
    }
    static int chop(std::uintptr_t base,Read read,const Sword& s,const MeleeHitRequest& hit) {
        using namespace rig;
        static const bool pinned=[&]{std::array<unsigned char,48> got{};return read(base+sever_pin_rva,got.data(),got.size()) && got==sever_code;}();
        static const std::uintptr_t decl=[&]()->std::uintptr_t {
            if(!pinned || value<std::uintptr_t>(read,base+damage_decl_type_rva)!=base+damage_decl_info_rva) return 0;
            __try {return reinterpret_cast<std::uintptr_t>(reinterpret_cast<void* (__fastcall*)(std::uintptr_t,const char*,bool,bool)>(base+find_decl_rva)(
                base+damage_decl_type_rva,"components/characters/player/base/damage/generic_fatality.damage",false,false));}
            __except(EXCEPTION_EXECUTE_HANDLER) {return 0;}
        }();
        if(!decl) return 1;
        std::uintptr_t severable{};
        for(const auto at:components(read,hit.victim)) if(typed(read,base,at,severable_vtable_rva) && value<std::uintptr_t>(read,at+0x10)==hit.victim) {severable=at;break;}
        if(!severable) return 1;
        const int r=sever(base,s,hit,severable,decl);if(r==0) ++chops;
        return r;
    }
    static int sever(std::uintptr_t base,const Sword& s,const MeleeHitRequest& hit,std::uintptr_t severable,std::uintptr_t decl) {
        alignas(16) unsigned char params[0x90]{};
        using Ctor=void (__fastcall*)(void*,std::uintptr_t);
        using Sever=void (__fastcall*)(std::uintptr_t,void*);
        __try {
            reinterpret_cast<Ctor>(base+ctor_rva)(params,decl);
            std::memcpy(params+0x24,hit.point.data(),12);std::memcpy(params+0x30,hit.to_attacker.data(),12);
            std::memcpy(params+0x3c,hit.direction.data(),12);
            std::memcpy(params+0x50,&s.sword_id,4);std::memcpy(params+0x54,&s.player_id,4);
            reinterpret_cast<Sever>(base+sever_rva)(severable,params);
        } __except(EXCEPTION_EXECUTE_HANDLER) {return 2;}
        return 0;
    }
    static int invoke(std::uintptr_t base,const Sword& s,const MeleeHitRequest& hit,std::uint32_t victim_id,DamageResultObservation* observation) {
        alignas(16) unsigned char params[0x90]{};alignas(16) unsigned char result[0x20]{};
        using Ctor=void (__fastcall*)(void*,std::uintptr_t);
        using Take=void* (__fastcall*)(std::uintptr_t,void*,void*);
        using HitList=void (__fastcall*)(std::uintptr_t,std::uint32_t);
        __try {
            reinterpret_cast<Ctor>(base+ctor_rva)(params,s.decl);
            if(s.amount>0) std::memcpy(params+0x08,&s.amount,4); // = 780a80(params, amount, no clamp)
            std::memcpy(params+0x24,hit.point.data(),12);std::memcpy(params+0x30,hit.to_attacker.data(),12);
            std::memcpy(params+0x3c,hit.direction.data(),12);
            std::memcpy(params+0x50,&s.sword_id,4);std::memcpy(params+0x54,&s.player_id,4);
            const auto returned=reinterpret_cast<Take>(base+take_rva)(hit.victim,result,params);
            if(observation)*observation=decode_damage_result(result,returned==result);
            if(s.weapon) reinterpret_cast<HitList>(base+hitlist_rva)(s.weapon,victim_id);
        } __except(EXCEPTION_EXECUTE_HANDLER) {return 2;}
        return 0;
    }
};

// Idempotent swarm kill (installed with the blade swarm kill; off with build/no-swarm-contact-kill): 0x6b8b70 re-runs the swarm kill dispatch (0x6a43a0, the dead-id list)
// for an agent already dead (state 7), so a fly the tracked blade killed while it sat in the native swing's contact list died
// twice (sim v385: heap corruption after 10 blade kills). The hook returns at once for a dead agent.
class NativeSwarmKillGuard {
public:
    static bool install(std::uintptr_t base) noexcept {
        if(target_) {const bool ok=MH_EnableHook(target_)==MH_OK;NativeMelee::swarm_kill_hooked=ok;return ok;}
        std::array<unsigned char,32> got{};SIZE_T n{};
        if(!base || !ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(base+NativeMelee::swarm_kill_rva),got.data(),got.size(),&n) ||
           n!=got.size() || got!=NativeMelee::swarm_kill_code) return false;
        void* t=reinterpret_cast<void*>(base+NativeMelee::swarm_kill_rva);
        if(MH_CreateHook(t,reinterpret_cast<void*>(&hook),reinterpret_cast<void**>(&original_))!=MH_OK) return false;
        if(MH_EnableHook(t)!=MH_OK) {MH_RemoveHook(t);return false;}
        target_=t;NativeMelee::swarm_kill_hooked=true;return true;
    }
    static void disable() noexcept {if(target_) MH_DisableHook(target_);NativeMelee::swarm_kill_hooked=false;}
    inline static std::atomic<std::uint64_t> skipped{};
private:
    using Kill=void (__fastcall*)(std::uintptr_t,void*,int);
    inline static Kill original_{};inline static void* target_{};
    static void __fastcall hook(std::uintptr_t record,void* params,int kind) {
        std::uint32_t state{};
        __try {if(record) state=*reinterpret_cast<const std::uint32_t*>(record+0x84);} __except(EXCEPTION_EXECUTE_HANDLER) {state=0;}
        if(state==7) {++skipped;return;}
        original_(record,params,kind);
    }
};

// The blade touching the world plays the engine's own sword-on-surface contact FX (sparks/dust + impact sound) through the
// ContactSystem manager (docs/native-impact-fx.md): Play(game+0x1f8370, event) with matA = the sword's weapon material and
// matB = the physics material of the hit surface (sphere-cast collector +0x7c). Quieter than a hit: speed and f40 are scaled.
// Game-thread Stage0 scope ONLY. Flag 0: scraping makes no AI noise.
struct ImpactFxRequest {std::array<float,3> point{},normal{};float speed{};std::uint16_t material{0xffff};bool gun{};}; // gun: a pistol/crossbow (either hand), else the sword
class NativeImpactFx {
public:
    using Read=NativeMelee::Read;
    inline static constexpr std::uintptr_t play_rva=0x60a8b0,manager_offset=0x1f8370,manager_vtable_rva=0x1b98d90,material_vtable_rva=0x1ad3848,
        find_decl_rva=0x1e030,material_decl_type_rva=0x2435520;
    // FindDecl(typeObject, name, create=0, flag=0) as the engine calls it at 0x8a7a90 (docs/native-impact-fx.md).
    inline static constexpr std::array<unsigned char,48> find_decl_code{0x44,0x88,0x44,0x24,0x18,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8d,0x6c,0x24,0xd9,0x48,0x81,0xec,0x00,0x01,0x00,0x00,0x48,0xc7,0x45,0xcf,0xfe,0xff,0xff,0xff,0x48,0x89,0x9c,0x24,0x40,0x01,0x00,0x00,0x41,0x0f,0xb6,0xf9};
    inline static std::uintptr_t gun_material_{}; // contactsystem/po.metal, resolved once per session on the game thread
    static void reset() noexcept {gun_material_=0;}
    inline static constexpr std::array<unsigned char,48> play_code{0x4c,0x8b,0xdc,0x55,0x57,0x41,0x55,0x49,0x8d,0x6b,0xa8,0x48,0x81,0xec,0x40,0x01,0x00,0x00,0x48,0x8b,0x02,0x48,0x8b,0xfa,0x4c,0x8b,0xe9,0xf6,0x80,0xd2,0x00,0x00,0x00,0x80,0x0f,0x85,0x5c,0x04,0x00,0x00,0xf3,0x0f,0x10,0x52,0x30,0xf3,0x0f,0x10};
    struct Event { // 0x50, filled like the engine ctor 0x60a480
        std::uintptr_t owner{},mat_a{},mat_b{};float pos[3]{};float speed{};float normal[3]{};float dir[3]{};float f40{};std::uint32_t mask{0x1ffff};std::uint8_t flag{};std::uint8_t pad[7]{};
    };
    static_assert(sizeof(Event)==0x50 && offsetof(Event,pos)==0x18 && offsetof(Event,speed)==0x24 && offsetof(Event,normal)==0x28 &&
                  offsetof(Event,dir)==0x34 && offsetof(Event,f40)==0x40 && offsetof(Event,mask)==0x44 && offsetof(Event,flag)==0x48);
    static bool verify(std::uintptr_t base,Read read) {
        std::array<unsigned char,48> got{},find{};return base && read && read(base+play_rva,got.data(),got.size()) && got==play_code &&
            read(base+find_decl_rva,find.data(),find.size()) && find==find_decl_code;
    }
    // 0 played, 1 skipped (no material/manager/owner), 2 engine fault.
    // A pistol or crossbow (in either hand) is not a melee weapon: it sounds like a metal object on the surface.
    static std::optional<std::uintptr_t> gun_material(std::uintptr_t base,Read read) {
        if(!gun_material_) {
            using Find=std::uintptr_t (__fastcall*)(std::uintptr_t,const char*,std::uint8_t,std::uint8_t);
            __try {gun_material_=reinterpret_cast<Find>(base+find_decl_rva)(base+material_decl_type_rva,"contactsystem/po.metal",0,0);} __except(EXCEPTION_EXECUTE_HANDLER) {gun_material_=0;}
        }
        if(!gun_material_ || !rig::typed(read,base,gun_material_,material_vtable_rva)) return {};
        return gun_material_;
    }
    static int play(std::uintptr_t base,Read read,const NativeMelee::Sword& s,const ImpactFxRequest& r) {
        using namespace rig;
        if(r.material==0xffff || !(r.speed>0)) return 1;
        const auto game=value<std::uintptr_t>(read,base+NativeMelee::game_rva);if(!game || !pointer(*game)) return 1;
        const std::uintptr_t manager=*game+manager_offset;
        const auto db=value<std::uintptr_t>(read,manager+0x10);
        if(!typed(read,base,manager,manager_vtable_rva) || !db || !*db) return 1;
        // material_of_id (0x92c10): *( *( *( *(game+0x60410)+0x988 )+0x30 ) + 0x60 + id*0x70 )
        const auto world=value<std::uintptr_t>(read,*game+0x60410);if(!world || !pointer(*world)) return 1;
        const auto table=value<std::uintptr_t>(read,*world+0x988);if(!table || !pointer(*table)) return 1;
        const auto rows=value<std::uintptr_t>(read,*table+0x30);if(!rows || !pointer(*rows)) return 1;
        const auto mat_b=value<std::uintptr_t>(read,*rows+0x60+static_cast<std::uintptr_t>(r.material)*0x70);
        const auto wdecl=value<std::uintptr_t>(read,s.weapon+8);
        const auto mat_a=r.gun?gun_material(base,read):wdecl && pointer(*wdecl)?value<std::uintptr_t>(read,*wdecl+0x138):std::nullopt;
        const std::uintptr_t owner=r.gun?s.player:s.entity;
        const auto suppress=value<std::uint8_t>(read,owner+0xd2);
        if(!mat_b || !typed(read,base,*mat_b,material_vtable_rva) || !mat_a || !typed(read,base,*mat_a,material_vtable_rva) || !suppress || (*suppress&0x80)) return 1;
        Event e{};e.owner=owner;e.mat_a=*mat_a;e.mat_b=*mat_b;e.speed=r.speed;e.f40=.6f;
        for(unsigned i=0;i<3;++i) {e.pos[i]=r.point[i];e.normal[i]=r.normal[i];}
        using Play=void (__fastcall*)(std::uintptr_t,const Event*);
        __try {reinterpret_cast<Play>(base+play_rva)(manager,&e);} __except(EXCEPTION_EXECUTE_HANDLER) {return 2;}
        return 0;
    }
};
} // namespace d2vr
