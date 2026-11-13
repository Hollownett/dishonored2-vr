#pragma once
// Dual wield B: a pistol or crossbow shot from a mod-chosen muzzle frame, through the game's own projectile path.
// Static ABI, Steam 17576867, image base 0x140000000 (RVAs; artifacts/claude/dual-wield/fleet/B-ranged-fire.md has the proof):
//  - the player's inventory (arkCpntInventory, items at +0x38, stride 0x38) keeps one record per item decl: +0 decl, +8 entity id,
//    +0x10 weapon component, +0x18 equipment decl, +0x20 count. The pistol and the crossbow each have a record whether or not
//    they are equipped, so the component of the weapon in the *other* hand is reachable from the record (no slot needed).
//  - one shot = 63b5f0(comp) creates and registers the projectile entity from comp+0x98 (the current ammo entry: bullet, or the
//    selected bolt), owner = the weapon entity's +0xac (the player); 639970(comp, proj, origin, axis3x3, speedScale, 0, 0, -1, 0)
//    places it and starts it with the player's attributes; 63b3c0(comp, 0) is the gunshot sound at the weapon entity's pose.
//    Those are the calls 44ebd0 (gun) and 62b560 (crossbow) make; the camera aim and muzzle lookup they do first are replaced by
//    the hand's frame. Damage, attribution, AI attention (the projectile's own sources) and impact FX are then the game's.
//  - ammo: the reserve is the inventory count of the ammo decl (shared by every weapon that uses it); the magazine is per
//    component (+0xa0 loaded, +0xa4 capacity). Two hands cannot share one magazine of 1, so each hand keeps its own loaded
//    count here and spends only the reserve (614f30 find, 615680 fired counter, 614750 remove 1: exactly the tail of 639360).
//    The native magazine of the component (what the equipped hand's own FireWeapon state uses) is never touched in that mode.
// Own launches are marked (ranged_fire_own_launch) so the NativeShotAim hook on 639970 leaves them alone.
// Game thread only (NativeInteractionAim stage-0 scope), gameplay only.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#if __has_include(<MinHook.h>)
#include <MinHook.h>
#define D2VR_COPY_FIRE_GATE 1   // v1117: the copy's WEAPON_FIRE gate needs MinHook (the DLL); builds without it get a gate that never installs
#else
#define D2VR_COPY_FIRE_GATE 0
#endif
#include "game_rig.hpp"
#include "gameplay_aim.hpp"
#include "crash_observer.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace d2vr {
// Nonzero on the game thread while NativeRangedFire is inside its own 63b5f0 / 639970 calls.
inline thread_local unsigned ranged_fire_launch_depth=0;
inline bool ranged_fire_own_launch() noexcept {return ranged_fire_launch_depth!=0;}

namespace ranged_fire {
enum class Kind : unsigned {pistol,crossbow};
inline constexpr std::uint64_t cooldown_ms=666; // fire scene length (gun_fire / crossbow_fire midnight scenes)
inline constexpr std::uint64_t reload_ms(Kind k) noexcept {return k==Kind::crossbow?900:1500;} // reload scene lengths
enum class Why : unsigned {ok,bad_request,bad_frame,queue_full,pending,cooldown,reloading,empty,no_ammo,no_player,no_weapon,
    no_component,bad_owner,not_ready,unverified,spawn_failed,fault,latched,full,count_};
inline const char* name(Why w) noexcept {
    static constexpr const char* names[]{"ok","bad_request","bad_frame","queue_full","pending","cooldown","reloading","empty","no_ammo",
        "no_player","no_weapon","no_component","bad_owner","not_ready","unverified","spawn_failed","fault","latched","full"};
    return static_cast<unsigned>(w)<sizeof(names)/sizeof(names[0])?names[static_cast<unsigned>(w)]:"?";
}
// One hand's firing state (pure; the game thread owns it). Cooldown, the reload timer and the hand's own magazine.
struct HandState {
    bool used{},reloading{};Kind kind{};int loaded{};std::uint64_t ready_at{},reload_done{};
    // Brings the state to `now` for the weapon `k` with magazine capacity and reserve as read from the game. A different
    // weapon in the hand starts full. True when a reload finished here.
    bool sync(std::uint64_t now,Kind k,int capacity,int pool) noexcept {
        capacity=std::max(capacity,1);pool=std::max(pool,0);
        if(!used || kind!=k) {*this=HandState{};used=true;kind=k;loaded=std::min(capacity,pool);return false;}
        bool finished=false;
        if(reloading && now>=reload_done) {reloading=false;loaded=std::min(capacity,pool);finished=true;}
        loaded=std::min({loaded,capacity,pool}); // the reserve (shared) can have been spent elsewhere
        return finished;
    }
    Why check(std::uint64_t now,int pool) const noexcept {
        if(reloading) return Why::reloading;
        if(now<ready_at) return Why::cooldown;
        if(loaded<=0) return pool<=0?Why::no_ammo:Why::empty;
        return Why::ok;
    }
    // The native cycle is fire scene, then the reload scene: the reload starts when the cooldown ends.
    // v1094: cool / reload_len are the game's scene lengths for the weapon's upgrades (Timing); 0 = the plain weapon.
    void fired(std::uint64_t now,bool auto_reload,std::uint64_t cool=0,std::uint64_t reload_len=0) noexcept {
        loaded=std::max(loaded-1,0);ready_at=now+(cool?cool:cooldown_ms);
        if(loaded==0 && auto_reload) {reloading=true;reload_done=ready_at+(reload_len?reload_len:reload_ms(kind));}
    }
    bool reload(std::uint64_t now,int capacity,int pool,std::uint64_t reload_len=0) noexcept {
        if(!used || reloading || loaded>=std::min(std::max(capacity,1),std::max(pool,0))) return false;
        reloading=true;reload_done=std::max(now,ready_at)+(reload_len?reload_len:reload_ms(kind));return true;
    }
};
// v1094 (user, headset v1093: the copy "fired as the default weapon": fire rate, reload, magazine, ammo, effects). The reload-speed upgrades
// carry no attribute: they set a midnight condition (upgrade decl +0x250: ReloadSpeed, ReloadSpeed2, NailGun; set by 0x61b400 from the
// weapon's m_upgrades) and the fire / reload midnight scenes pick a shorter scene by it (gun_reload / gun_fire / crossbow_fire decls).
// The times below are those scenes (the end, or the BREAK_PLAYER_STATE event that frees the player first). The nailgun also scales the
// crossbow scenes by its playrate attributes (fire 2.4, reload 1.55: crossbow_nailgun_corvo). Burst (Emily) = ammo flag bit 0: 3 shots
// 150 ms apart (FastFire x2 + Fire).
enum Condition : unsigned {reload_speed=1,reload_speed2=2,nail_gun=4};
struct Timing {std::uint64_t cool{},reload{};};
inline Timing timing(Kind k,unsigned conditions) noexcept {
    if(k==Kind::pistol) return {330,(conditions&reload_speed2)?766u:(conditions&reload_speed)?1000u:1500u};
    Timing t{(conditions&(reload_speed|nail_gun))?100u:666u,(conditions&reload_speed)?566u:900u};
    if(conditions&nail_gun) t.reload=static_cast<std::uint64_t>(static_cast<double>(t.reload)/1.55+.5);
    return t;
}
inline unsigned condition_of(const char* name) noexcept {
    if(!name) return 0;
    if(!std::strcmp(name,"ReloadSpeed2")) return reload_speed2;
    if(!std::strcmp(name,"ReloadSpeed")) return reload_speed;
    if(!std::strcmp(name,"NailGun")) return nail_gun;
    return 0;
}
inline constexpr std::uint64_t burst_gap_ms=150;
inline Why validate(unsigned side,unsigned kind,const float* origin,const float* axis) noexcept {
    if(side>1 || kind>1) return Why::bad_request;
    for(unsigned i=0;i<3;++i) if(!std::isfinite(origin[i]) || std::abs(origin[i])>1e6f) return Why::bad_frame;
    std::array<float,9> a{};std::memcpy(a.data(),axis,sizeof(float)*9);
    for(float v:a) if(!std::isfinite(v)) return Why::bad_frame;
    return valid_aim_frame(a)?Why::ok:Why::bad_frame; // unit, orthogonal, right-handed: the test the native aim writer uses
}
} // namespace ranged_fire

class NativeRangedFire {
    friend struct NativeRangedFireTest;
public:
    using Why=ranged_fire::Why;
    // axis rows: forward, left, up (game world, Z up). origin = the muzzle in game world.
    struct Shot {
        enum Kind : unsigned {pistol,crossbow};
        unsigned side{};Kind kind{pistol};float origin[3]{};float axis[9]{1,0,0,0,1,0,0,0,1};
        // The gun in the hand that fires (its held copy's entity + id, 0 = none): 63b3c0 and 44ee80 take their entity from comp+0x10 (the record's own
        // gun, which is the OTHER hand's native pistol: v815 headset 'the flash comes out of the pistol in my left hand'), so they run with this one.
        std::uintptr_t entity{};std::uint32_t entity_id{};
    };
    struct Request {bool reload{},burst{};Shot shot{};};
    // The native calls, replaceable so the pure flow is testable. Real ones run under SEH and return false on a fault.
    struct Natives {
        bool (*create)(std::uintptr_t base,std::uintptr_t comp,std::uintptr_t& projectile);
        bool (*launch)(std::uintptr_t base,std::uintptr_t comp,std::uintptr_t projectile,const float* origin,const float* axis,float speed);
        bool (*sound)(std::uintptr_t base,std::uintptr_t comp,std::uintptr_t entity,const float* origin,const float* axis);
        std::uintptr_t (*component)(std::uintptr_t base,std::uintptr_t entity,std::uintptr_t info_rva);
        bool (*spend)(std::uintptr_t base,std::uintptr_t inventory,std::uintptr_t ammo_decl);
        bool (*native_spend)(std::uintptr_t base,std::uintptr_t comp);
        bool (*native_reload)(std::uintptr_t base,std::uintptr_t comp);
        bool (*mark)(std::uintptr_t base,std::uintptr_t projectile_component);
        bool (*flash)(std::uintptr_t base,std::uintptr_t comp,std::uintptr_t entity,const float* origin,const float* axis)=nullptr; // 44ee80 (muzzle FX) on that entity
    };
    // Knobs.
    inline static std::atomic<bool> dry_run{false};         // resolve everything, log the calls, call nothing, change no state
    inline static std::atomic<bool> play_sound{true};       // 63b3c0 at the muzzle (pose swap of the weapon entity for the call)
    inline static std::atomic<bool> muzzle_flash{false};    // 44ee80 on the firing gun's entity (INFERRED: the FX copies that entity's pose and follows it); off until the headset shows 63b3c0's own
    inline static std::atomic<bool> auto_reload{true};      // a hand reloads by itself when its magazine empties (the native cycle)
    inline static std::atomic<bool> shared_native_magazine{false}; // use comp+0xa0 through 639360 / 63a2b0 instead of per-hand magazines
    inline static std::uint64_t (*clock)()=[]()->std::uint64_t {return GetTickCount64();};

    // Control thread: queue one shot. False when it is refused (the reason is counted).
    static bool fire(const Shot& s) {
        ++requests_;
        if(const auto why=ranged_fire::validate(s.side,s.kind,s.origin,s.axis);why!=Why::ok) return refuse(why);
        const auto now=clock();
        if(now<ready_at_[s.side].load()) return refuse(Why::cooldown);
        if(now<reload_until_[s.side].load()) return refuse(Why::reloading);
        Request r{};r.shot=s;
        return enqueue(r);
    }
    // Control thread: reload the hand's magazine (the kind defaults to what the hand last fired).
    static bool reload(unsigned side) {return side<2 && last_kind_[side].load()>=0 && reload(side,static_cast<Shot::Kind>(last_kind_[side].load()));}
    static bool reload(unsigned side,Shot::Kind kind) {
        ++requests_;
        if(side>1 || kind>Shot::crossbow) return refuse(Why::bad_request);
        Request r{};r.reload=true;r.shot.side=side;r.shot.kind=kind;
        return enqueue(r);
    }
    static bool ready(unsigned side) {return side<2 && clock()>=ready_at_[side].load() && clock()>=reload_until_[side].load();}
    // v1113: a hand whose last shot was refused on the game thread (no ammo...) stays blocked 250 ms for the held trigger; an edge pull still asks.
    static bool held_blocked(unsigned side) {const auto t=side<2?refused_at_[side].load():0;return t && clock()-t<250;}
    static bool queued(unsigned side) {return side<2 && pending_[side];} // v1112: a shot already queued for this hand (the held trigger waits for it)
    // v1094: what each hand holds as a copy (-1 none, 0 pistol, 1 crossbow; render/control thread, every frame) and which hand chose from
    // the wheel last (its choice of ammo for a copy is the copy's own, not the other hand's native weapon's).
    static void note_copies(int left,int right) {copy_kind_[0]=left;copy_kind_[1]=right;}
    static void note_wheel(unsigned hand) {if(hand<2) {wheel_hand_=static_cast<int>(hand);wheel_tick_=clock();wheel_done_=false;}}
    static void note_wheel_open(unsigned hand) {if(hand<2) wheel_hand_=static_cast<int>(hand);} // v1101: known from the opening on (the game's equip may run before the commit is noted)
    // Game thread: the ammo entry a hand's copy fires (0 = the weapon's own) and whether the copy is loaded (its bolt shows).
    static std::uintptr_t copy_ammo(unsigned side,unsigned kind) {return side<2 && kind<2?copy_ammo_[side][kind]:0;}
    static std::uintptr_t native_ammo(unsigned kind) {return kind<2?seen_ammo_[kind]:0;}
    static bool loaded(unsigned side) {return side<2 && (!hands_[side].used || (hands_[side].loaded>0 && !hands_[side].reloading));} // never fired: as the weapon is
    // v1096: the hand's copy is a real weapon with this started component (game thread; 0 = none): its shots, magazine and ammo are its own.
    static void set_copy_comp(unsigned side,std::uintptr_t comp,std::uintptr_t entity=0) {
        if(side>1) return;
        if(comp && comp!=copy_last_[side]) {copy_ammo_[side]={};copy_last_[side]=comp;} // v1101: a new instance starts as the weapon it copies, then keeps its own choice (a hidden one keeps it too)
        copy_comp_[side]=comp;gate_comp_[side].store(comp,std::memory_order_relaxed);gate_ent_[side].store(comp?entity:0,std::memory_order_relaxed);
    }
    // v1118: the player's own pistol (0) / crossbow (1) component and entity, for the event counters (game thread, a few times a second).
    static void note_native(unsigned kind,std::uintptr_t comp,std::uintptr_t entity) {if(kind<2) {native_comp_[kind].store(comp);native_ent_[kind].store(entity);}}
    // [class][event]: class 0 native weapon, 1 a hand's copy, 2 other; event 200, 201, 202, 203, 204, 205, 242.
    static std::uint64_t received(unsigned cls,unsigned ev) {return cls<3 && ev<7?recv_count_[cls][ev].load():0;}
    static std::uint64_t keys_sent(unsigned cls,unsigned ev) {return cls<3 && ev<7?keys_count_[cls][ev].load():0;}
    static int hook_status(unsigned i) {return i<hook_status_.size()?hook_status_[i]:-99;}
    static std::uint64_t sfx_sent(unsigned cls) {return cls<3?sfx_count_[cls].load():0;}
    static std::uint64_t sounds(unsigned cls) {return cls<4?sound_count_[cls].load():0;}   // v1120: messages 169 (play sound) received per class
    static void note_arm(unsigned side,std::uintptr_t entity) {if(side<2) arm_ent_[side].store(entity);}
    inline static std::atomic<std::uint64_t> scene_refills_{};
    // v1117 (user, headset v1116: the copy "does not use the same sound, animations and potentially upgrades"). Static RE: the player's fire,
    // reload and out-of-ammo scenes (gun_fire, crossbow_fire, *_reload, *_ooa .midnightscene) carry event keys on the PlayerLeftEquipment group
    // that 0x84bca0 posts to that group's actor entity: ARK_MDNT_MSG_WEAPON_FIRE 200 (crossbow at 30 ms, gun at 10 ms, every FastFire / Burst
    // loop), CONSUME_AMMO_FROM_MAGAZINE 202, RELOAD 201, RELOAD_MAGAZINE 203, UPGRADE_ANIMATE_FOR_FIRE 204 / _RELOAD 205, BREAK_PLAYER_STATE 242.
    // The weapon component registers its handlers on its own entity at start (crossbow 0x62bd20: 200 -> thunk 0x62bfd0 -> 0x62b560, 201 -> vslot
    // 0x148, 204 -> 0x44f4b0; gun 0x44e570: 200 -> thunk 0x44f430 -> 0x44e0c0 ... 205, 242). So a copy scene (the copy bound as
    // PlayerLeftEquipment) makes the copy's OWN component do everything the native weapon does on that scene, including a second, camera-aimed
    // shot from its 200 handler (0x44e0c0: CanFire = magazine > 0 -> consume, 0x44ee80 flash, 63b3c0 FX + sound, spread, 0x44ebd0 bullet) after
    // this module already fired the hand-aimed one: a pistol copy fired twice per pull (six times per Burst). The gate below swallows a copy's
    // WEAPON_FIRE (MinHook on the two 200 thunks; every other component, the native weapon included, passes through), so the hand-aimed shot
    // stays the only shot and every other scene event still reaches the copy. Installed on the game thread before the first copy scene,
    // removed by ungate_copy_fire() (NativeWeaponHands::dual_wield(false), the stop path).
    // v1118 (sim v1117: gate [1,0,0]: neither the 7 copy fire scenes nor one native crossbow shot reached a hooked 200 thunk): the same hooks
    // also COUNT. Receivers (the handlers the weapon components register on their entity, dispatched by 0x810000 as fn(component, &message)):
    // 200 crossbow 0x62bfd0 / gun 0x44f430 (the gate), 201 crossbow 0x62bfe0 / gun 0x44f440, 202 gun 0x44f3d0, 203 gun 0x44f470, 204 both
    // 0x44f4b0, 205 gun 0x44f4c0, 242 gun 0x44f3b0, each by the receiving component: the player's native pistol / crossbow, a hand's copy, any
    // other. Senders: the scene event track's key firing 0x84bca0(trackInst, keys) (arkMidnightTrackInstEvent slot 0x80; key +0x28 prop, prop +8
    // event id, target = [[trackInst+0x10]+0xd0], the group's actor), by the same target classes. Only the two 200 hooks are required (the gate);
    // the rest are best effort and never block a scene.
    struct EventHook {std::uintptr_t rva;unsigned ev;std::array<unsigned char,8> code;};
    static constexpr std::array<EventHook,9> event_hooks{{
        {0x44f430,0,{0xe9,0x8b,0xec,0xff,0xff,0xcc,0xcc,0xcc}},{0x62bfd0,0,{0xe9,0x8b,0xf5,0xff,0xff,0xcc,0xcc,0xcc}},   // 200 gun / crossbow (the gate)
        {0x62bfe0,1,{0x48,0x8b,0x01,0x48,0xff,0xa0,0x48,0x01}},{0x44f440,1,{0x48,0x8b,0xc1,0x48,0x8b,0x89,0x58,0x01}},   // 201 crossbow / gun
        {0x44f3d0,2,{0x40,0x53,0x48,0x83,0xec,0x20,0x83,0xb9}},{0x44f470,3,{0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b}},   // 202, 203 gun
        {0x44f4b0,4,{0xba,0x03,0x00,0x00,0x00,0xe9,0x96,0xb1}},{0x44f4c0,5,{0xba,0x04,0x00,0x00,0x00,0xe9,0x86,0xb1}},   // 204 both, 205 gun
        {0x44f3b0,6,{0x83,0xb9,0xa4,0x00,0x00,0x00,0x04,0x7e}}}};                                                       // 242 gun
    static constexpr std::array<unsigned char,9> keys_code{0x40,0x55,0x56,0x41,0x54,0x48,0x83,0xec,0x30};             // 0x84bca0
    static constexpr std::array<unsigned char,9> sfx_code{0x48,0x89,0x54,0x24,0x10,0x48,0x89,0x4c,0x24};              // 0x827c30 (Sfx keys: the out-of-ammo click)
    static constexpr std::array<unsigned char,9> sound_code{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24};            // 0x70a030 (audio component, message 169)
    template<class Read> static bool gate_copy_fire(std::uintptr_t base,Read& read) {
        if(const int s=gate_state_.load();s>=0) return s==1;
        bool ok=false;
#if D2VR_COPY_FIRE_GATE
        ok=true;
        for(const auto& r:{std::array<std::uintptr_t,3>{0x44f430,0x44e0c0,0x44e597},std::array<std::uintptr_t,3>{0x62bfd0,0x62b560,0x62bd3d}}) {   // thunk, target, 200 registration
            std::array<unsigned char,7> l{};std::int32_t rel{},lrel{};std::array<unsigned char,5> j{};
            if(!read(base+r[0],j.data(),j.size()) || j[0]!=0xe9 || !read(base+r[2],l.data(),l.size()) || l[0]!=0x48 || l[1]!=0x8d || l[2]!=0x05) {ok=false;break;}
            std::memcpy(&rel,j.data()+1,4);std::memcpy(&lrel,l.data()+3,4);
            if(base+r[0]+5+rel!=base+r[1] || base+r[2]+7+lrel!=base+r[0]) {ok=false;break;}
        }
        std::array<unsigned char,12> c{};
        ok=ok && read(base+0x44e0c0,c.data(),c.size()) && c==std::array<unsigned char,12>{0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0x01,0x48,0x8b,0xd9}
              && read(base+0x62b560,c.data(),c.size()) && c==std::array<unsigned char,12>{0x40,0x55,0x57,0x48,0x8d,0x6c,0x24,0x88,0x48,0x81,0xec,0x78};
        const auto hook=[&](unsigned i,void* at,void* detour,void** original)->int {   // created once, then only enabled / disabled
            std::array<unsigned char,9> got{};
            if(i<event_hooks.size()) {if(!read(reinterpret_cast<std::uintptr_t>(at),got.data(),8) || std::memcmp(got.data(),event_hooks[i].code.data(),8)) return -100;}
            else if(!read(reinterpret_cast<std::uintptr_t>(at),got.data(),9) || got!=(i==9?keys_code:i==10?sfx_code:sound_code)) return -100;
            auto st=hook_at_[i]==at?MH_OK:MH_CreateHook(at,detour,original);
            if(st==MH_OK) {hook_at_[i]=at;st=MH_EnableHook(at);}
            return static_cast<int>(st);
        };
        static constexpr std::array<GateFn,9> receivers{&recv<0>,&recv<1>,&recv<2>,&recv<3>,&recv<4>,&recv<5>,&recv<6>,&recv<7>,&recv<8>};
        for(unsigned i=0;i<event_hooks.size() && ok;++i) {
            hook_status_[i]=hook(i,reinterpret_cast<void*>(base+event_hooks[i].rva),reinterpret_cast<void*>(receivers[i]),reinterpret_cast<void**>(&recv_original_[i]));
            if(i<2 && hook_status_[i]!=MH_OK) ok=false;   // the 200 gate is required; the other receivers are only counted
        }
        if(ok) hook_status_[9]=hook(9,reinterpret_cast<void*>(base+0x84bca0),reinterpret_cast<void*>(&keys_fired),reinterpret_cast<void**>(&keys_original_));
        if(ok) hook_status_[10]=hook(10,reinterpret_cast<void*>(base+0x827c30),reinterpret_cast<void*>(&sfx_fired),reinterpret_cast<void**>(&sfx_original_));
        if(ok) hook_status_[11]=hook(11,reinterpret_cast<void*>(base+0x70a030),reinterpret_cast<void*>(&sound_posted),reinterpret_cast<void**>(&sound_original_));
        if(!ok) for(const auto at:hook_at_) if(at) MH_DisableHook(at);
        ungate_=ok?+[]() noexcept {for(const auto at:hook_at_) if(at) MH_DisableHook(at);gate_state_=-1;}:nullptr;
#else
        (void)base;(void)read;
#endif
        gate_state_=ok?1:0;
        return ok;
    }
    static void ungate_copy_fire() noexcept {if(const auto f=ungate_.exchange(nullptr)) f();}
    static int gate_state() noexcept {return gate_state_.load();}
    // v1117: a dry pull of a hand whose copy has no magazine and no reserve (refused no_ammo): NativeWeaponHands plays the copy's
    // out-of-ammo scene (vslot 0x130 = 0x63bd40), whose Sfx key on PlayerLeftEquipment is the game's own empty click.
    static std::uint64_t dry_serial(unsigned side) {return side<2?dry_serial_[side]:0;}
    // v1101 (headset v1100: the right hand chose a bolt type for its own crossbow three times; each time the game switched the LEFT hand's
    // native crossbow and the mod switched it back 0.4 s later, so neither crossbow showed the choice): the wheel's equip 0x7fb350(controller,
    // item, ammo) is hooked (NativeManualEquipRuntime::wheel_pick). When the hand that opened the wheel holds a real copy of the chosen
    // weapon, the choice is that copy's own: selected on its component here. The game is handed the native weapon's current ammo item,
    // which 0x7fb350 treats as already equipped (slot-1 record and ammo item unchanged: no EquipChange), so the other hand keeps its bolt.
    // Returns the ammo item the game should equip, 0 = the wheel's own choice stands (another item, no copy, or the native not in slot 1).
    template<class Read> static std::uintptr_t wheel_pick(std::uintptr_t base,Read& read,std::uintptr_t item,std::uintptr_t ammo) {
        if(!item || !ammo) return 0;
        // v1277 (user: a crossbow often showed the PREVIOUSLY selected bolt): the wheel's bolt for this weapon, whichever hand chose it and whether
        // or not a copy exists yet. A copy created after the choice starts from it (track_ammo); before, it took seen_ammo_, which only follows
        // the native's ammo 2.5 s after a wheel choice, and kept that old bolt for good.
        for(unsigned kk=0;kk<2;++kk) {Why wk{};if(const auto lk=locate(base,read,static_cast<Shot::Kind>(kk),wk);lk && rig::value<std::uintptr_t>(read,lk->record)==item) last_choice_[kk]=ammo;}
        const int w=wheel_hand_.load();if(w<0 || w>1) return 0;
        const unsigned side=static_cast<unsigned>(w);const int k=copy_kind_[side].load();const auto cc=copy_comp_[side];if(k<0 || k>1 || !cc) return 0;
        Why why{};const auto l=locate(base,read,static_cast<Shot::Kind>(k),why);if(!l) return 0;
        if(rig::value<std::uintptr_t>(read,l->record)!=item) return 0;            // another item was chosen: the arms route it
        const auto entry=ammo_entry(read,cc,ammo);if(!entry) return 0;             // not an ammo this weapon takes
        copy_ammo_[side][static_cast<unsigned>(k)]=entry;wheel_done_=true;++wheel_picks_;
        if(rig::value<std::uintptr_t>(read,cc+0x98)!=entry && select_ammo(base,cc,ammo)) ++copy_ammo_sets_;
        if(rig::value<std::uintptr_t>(read,l->inventory+0x70)!=l->record) return 0; // the native weapon is not in slot 1: the game's equip is wanted as is
        const auto keep=rig::value<std::uintptr_t>(read,l->ammo);                   // the native weapon's own ammo item
        return keep && *keep?*keep:0;
    }
    // The weapon decl's ammo entry (+0x188 array, stride 0x70, +0 item) for an ammo item; 0 = none.
    template<class Read> static std::uintptr_t ammo_entry(Read& read,std::uintptr_t comp,std::uintptr_t ammo_item) {
        const auto decl=rig::value<std::uintptr_t>(read,comp+8);if(!decl || !rig::pointer(*decl)) return 0;
        const auto first=rig::value<std::uintptr_t>(read,*decl+0x188);const auto count=rig::value<std::uint32_t>(read,*decl+0x190);
        if(!first || !*first || !count) return 0;
        for(unsigned i=0;i<(*count&0xffffffu) && i<16;++i) if(rig::value<std::uintptr_t>(read,*first+i*0x70ull)==ammo_item) return *first+i*0x70ull;
        return 0;
    }
    // Test (sim, v1097): "ammo left|right <index>": the hand's copy fires and shows ammo type <index> of its weapon (decl m_ammoTypes order).
    static void test_ammo(unsigned side,int index) {if(side<2) test_ammo_[side]=index;}
    // Serials NativeWeaponHands plays the copy's upgrade animations from (fire = ANIMATE_ON_FIRE, reload start = ANIMATE_ON_RELOAD).
    static std::uint64_t shot_serial(unsigned side) {return side<2?shot_serial_[side]:0;}
    static std::uint64_t reload_serial(unsigned side) {return side<2?reload_serial_[side]:0;}
    static bool burst_follow_up(unsigned side) {return side<2 && burst_follow_[side];}   // v1114: the last shot was a burst's follow-up (the burst's fire scene keeps playing)
    static void reset_latch() {latched_=false;consecutive_faults_=0;}
    static void reset_counters() {
        for(auto* c:{&requests_,&serviced_,&services_,&failures_,&fired_,&created_,&launched_,&reloads_,&dry_plans_,&comps_total_,&comps_started0_,
            &comps_started100_,&proj_comp_started100_,&moved_,&stationary_,&ended_early_,&alive_4s_,&spent_,&ammo_mismatch_}) c->store(0);
        for(auto& r:refused_) r.store(0);
        pool_before_=pool_after_=mag_before_=mag_after_=-1;
    }
    // Level change / leaving gameplay: drop queued shots, hand state and tracked projectiles (counters stay).
    static void reset() {
        {std::lock_guard<std::mutex> l(m_);queue_.clear();pending_[0]=pending_[1]=false;shots_.clear();plans_.clear();}
        tracks_.clear();hands_[0]=hands_[1]=ranged_fire::HandState{};
        for(unsigned i=0;i<2;++i) {refused_at_[i]=0;ready_at_[i]=0;reload_until_[i]=0;last_kind_[i]=-1;last_choice_[i]=0;}
        reset_latch();
    }

    // Game thread, next to NativeWeaponHands::service.
    template<class Read> static void service(std::uintptr_t base,Read& read) {
        static const bool verified=verify(base,read);
        std::vector<Request> work;{std::lock_guard<std::mutex> l(m_);work.swap(queue_);pending_[0]=pending_[1]=false;}
        if(!work.empty()) ++services_;
        if(!verified) {for(const auto& r:work) {(void)r;refuse(Why::unverified);}work.clear();}
        CrashOperation operation("native ranged fire");
        if(verified && !latched_) {try {track_ammo(base,read);} catch(...) {++failures_;}}
        const auto now=clock();
        for(unsigned side=0;side<2;++side) // burst follow-ups (the shot's frame is the first one's)
            if(burst_left_[side] && now>=burst_next_[side]) {Request r{};r.shot=burst_shot_[side];r.burst=true;work.push_back(r);}
        for(unsigned side=0;side<2;++side) if(hands_[side].reloading && !reload_noted_[side] && now>=hands_[side].reload_done-reload_len_[side]) {reload_noted_[side]=true;++reload_serial_[side];}
        for(unsigned side=0;side<2;++side) // v1096: a real copy's magazine refills when its reload ends (its own 0x63a2b0), not at the next pull
            if(const auto cc=copy_comp_[side];cc && verified && hands_[side].used && hands_[side].reloading && now>=hands_[side].reload_done) {
                // v1120: the copy's reload scene refills it at the scene's own RELOAD key (its registered 201 / 203 handler -> vslot 0x148);
                // the mod's refill is the fallback for a reload no scene delivered (magazine still below capacity)
                const auto mag=rig::value<std::int32_t>(read,cc+0xa0).value_or(0),cap=rig::value<std::int32_t>(read,cc+0xa4).value_or(1);
                if(mag>=std::max(cap,1) || real_native_reload(base,cc)) {
                    if(mag>=std::max(cap,1)) ++scene_refills_;
                    hands_[side].reloading=false;hands_[side].loaded=std::max(rig::value<std::int32_t>(read,cc+0xa0).value_or(0),0);++reloads_;publish(side,hands_[side]);}
            }
        if(work.empty() && tracks_.empty()) return;
        try {service_with(base,read,native_table(),work);} catch(...) {++failures_;}
    }
    template<class Read> static void service_with(std::uintptr_t base,Read& read,const Natives& n,const std::vector<Request>& work) {
        const auto now=clock();
        for(const auto& r:work) {
            ++serviced_;
            const Why why=latched_?Why::latched:handle(base,read,n,r,now);
            if(why!=Why::ok) {refuse(why);if(!r.reload && r.shot.side<2) refused_at_[r.shot.side]=now;if(r.burst) burst_left_[r.shot.side]=0;
                if(why==Why::no_ammo && !r.reload && !r.burst && r.shot.side<2) {++dry_serial_[r.shot.side];++dry_pulls_;}}   // v1117: the copy's empty click
        }
        update_tracks(base,read,now);
    }
    // Probe counters.
    static std::string status_json() {
        std::ostringstream o;
        o<<"{\"dry_run\":"<<(dry_run.load()?"true":"false")<<",\"latched\":"<<(latched_.load()?"true":"false")
         <<",\"requests\":"<<requests_.load()<<",\"serviced\":"<<serviced_.load()<<",\"services\":"<<services_.load()
         <<",\"failures\":"<<failures_.load()<<",\"refused\":{";
        bool first=true;
        for(unsigned i=1;i<static_cast<unsigned>(Why::count_);++i) {
            if(!refused_[i].load()) continue;
            o<<(first?"":",")<<'"'<<ranged_fire::name(static_cast<Why>(i))<<"\":"<<refused_[i].load();first=false;
        }
        o<<"},\"fired\":"<<fired_.load()<<",\"dry_plans\":"<<dry_plans_.load()<<",\"projectiles_created\":"<<created_.load()
         <<",\"launched\":"<<launched_.load()<<",\"reloads\":"<<reloads_.load()
         <<",\"components\":{\"total_at_create\":"<<comps_total_.load()<<",\"started_at_create\":"<<comps_started0_.load()
         <<",\"started_at_100ms\":"<<comps_started100_.load()<<",\"projectile_comp_started_at_100ms\":"<<proj_comp_started100_.load()
         <<"},\"moved_100ms\":"<<moved_.load()<<",\"stationary_100ms\":"<<stationary_.load()<<",\"ended_early\":"<<ended_early_.load()
         <<",\"alive_4s\":"<<alive_4s_.load()<<",\"entity_finds\":"<<entity_finds_.load()<<",\"unstarted_shots\":"<<unstarted_shots_.load()<<",\"ammo\":{\"pool_before\":"<<pool_before_.load()<<",\"pool_after\":"<<pool_after_.load()
         <<",\"mag_before\":"<<mag_before_.load()<<",\"mag_after\":"<<mag_after_.load()<<",\"spent\":"<<spent_.load()
         <<",\"mismatch\":"<<ammo_mismatch_.load()<<"},\"v1094\":["<<ammo_swaps_.load()<<','<<ammo_splits_.load()<<','<<spreads_.load()<<','<<bursts_.load()<<','<<own_shots_.load()<<','<<copy_ammo_sets_.load()<<','<<wheel_picks_.load()<<"],\"gate\":["<<gate_state_.load()<<','<<gated_.load()<<','<<gate_passed_.load()
         <<','<<hook_status_[0]<<','<<hook_status_[1]<<','<<dry_pulls_.load()<<','<<scene_refills_.load()<<"],\"hands\":[";   // v1117: copy WEAPON_FIRE gate [state, swallowed, passed, MH gun, MH crossbow], dry pulls
        const auto now=clock();
        for(unsigned s=0;s<2;++s) {
            const auto ra=ready_at_[s].load(),ru=reload_until_[s].load();
            o<<(s?",":"")<<"{\"ready_in_ms\":"<<(ra>now?ra-now:0)<<",\"reload_in_ms\":"<<(ru>now?ru-now:0)<<",\"kind\":"<<last_kind_[s].load()<<'}';
        }
        o<<"]";
        std::lock_guard<std::mutex> l(m_);
        o<<",\"shots\":[";
        for(std::size_t i=0;i<shots_.size();++i) o<<(i?",":"")<<shots_[i];
        o<<"],\"plans\":[";
        for(std::size_t i=0;i<plans_.size();++i) o<<(i?",":"")<<'"'<<plans_[i]<<'"';
        o<<"]}";
        return o.str();
    }
    static std::uint64_t refused_count(Why w) {return refused_[static_cast<unsigned>(w)].load();}
    inline static std::atomic<std::uint64_t> requests_{},serviced_{},services_{},failures_{},fired_{},created_{},launched_{},reloads_{},dry_plans_{},
        comps_total_{},comps_started0_{},comps_started100_{},proj_comp_started100_{},moved_{},stationary_{},ended_early_{},alive_4s_{},spent_{},ammo_mismatch_{},entity_finds_{},unstarted_shots_{}; // entity_finds_: shots whose weapon was found through its entity (v762)
    inline static std::atomic<int> pool_before_{-1},pool_after_{-1},mag_before_{-1},mag_after_{-1};

    // What the sequence needs, read from the game for one shot.
    struct Located {
        std::uintptr_t game{},player{},inventory{},record{},comp{},decl{},entity{},ammo{},ammo_decl{},projectile_def{};
        std::uint32_t entity_id{},player_id{};int capacity{},magazine{},pool{};float speed{1.f};bool mark{},unstarted{};
        std::uintptr_t native_ammo{};ranged_fire::Timing timing{};bool own{};
    };
    inline static constexpr std::uintptr_t game_rva=0x39a5290,entities_offset=0x3130b0,player_vtable=0x1c5e428,game_vtable=0x1c940a8,
        inventory_vtable=0x1b9a878,weapon_entity_vtable=0x1ce1e98,projectile_info_rva=0x39d77e0;
    struct KindInfo {std::uintptr_t comp_vtable,decl_vtable;};
    static constexpr KindInfo kind_info(Shot::Kind k) noexcept {return k==Shot::crossbow?KindInfo{0x1b9cbf8,0x1b9cab8}:KindInfo{0x1b53f58,0x1b53e18};}
    struct Rec {std::uintptr_t decl;std::uint32_t id,pad;std::uintptr_t comp,equipment;std::int32_t count,pad2;};
    static_assert(sizeof(Rec)==0x28);

    // Everything the shot needs, validated; why says what is missing.
    template<class Read> static std::optional<Located> locate(std::uintptr_t base,Read& read,Shot::Kind kind,Why& why) {
        using namespace rig;
        Located l{};why=Why::no_player;
        const auto game=value<std::uintptr_t>(read,base+game_rva);if(!game || !typed(read,base,*game,game_vtable)) return {};
        const auto player=value<std::uintptr_t>(read,*game+entities_offset);if(!player || !typed(read,base,*player,player_vtable)) return {};
        const auto pid=value<std::uint32_t>(read,*player+0xa8);if(!pid || !valid_id(*pid)) return {};
        l.game=*game;l.player=*player;l.player_id=*pid;
        if(const auto comps=list(read,*player+0x268,4096)) {
            for(std::uint32_t i=0;i<comps->packed_count && i<512;++i) {
                const auto c=value<std::uintptr_t>(read,comps->data+i*8ull);
                if(c && typed(read,base,*c,inventory_vtable) && value<std::uintptr_t>(read,*c+0x10)==*player) {l.inventory=*c;break;}
            }
            // v763 (headset v762: no_weapon 63 of 63, entity_finds 0): the inventory is not among the player's own components; the player
            // state machine's +0x328 holds it (what NativeWeaponHands' drops and pickups use live).
            for(std::uint32_t i=0;i<comps->packed_count && i<512 && !l.inventory;++i) {
                const auto c=value<std::uintptr_t>(read,comps->data+i*8ull);
                if(!c || !typed(read,base,*c,0x1c73d58) || value<std::uintptr_t>(read,*c+0x10)!=*player) continue; // arkCpntPlayerStateMachine
                if(const auto inventory=value<std::uintptr_t>(read,*c+0x328);inventory && *inventory && typed(read,base,*inventory,inventory_vtable)) l.inventory=*inventory;
            }
        }
        why=Why::no_weapon;
        if(!l.inventory) return {};
        // v764 (live, the user's chapter-4 save: 816 records; a 512 cap refused every shot as no_weapon). list() bounds it at 4096.
        const auto items=list(read,l.inventory+0x38,4096);if(!items || !items->packed_count) return {};
        const auto info=kind_info(kind);bool candidate=false;
        for(std::uint32_t i=0;i<items->packed_count;++i) {
            const auto rec=value<Rec>(read,items->data+i*0x38ull);
            if(!rec || !rec->comp || !rec->equipment || !typed(read,base,rec->comp,info.comp_vtable) || !typed(read,base,rec->equipment,info.decl_vtable)) continue;
            candidate=true;l.record=items->data+i*0x38ull;l.comp=rec->comp;l.decl=rec->equipment;l.entity_id=rec->id;break;
        }
        // v762 (headset v761: no_weapon 29 of 29, the record's +0x10/+0x18 pair never typed live): the record's weapon entity (+8, the id
        // the mirror and the barrel read use live) and that kind's component among the entity's own (+0x268, as the player's).
        for(std::uint32_t i=0;i<items->packed_count && !candidate;++i) {
            const auto rec=value<Rec>(read,items->data+i*0x38ull);if(!rec || !valid_id(rec->id)) continue;
            const auto entity=value<std::uintptr_t>(read,l.game+entities_offset+(rec->id>>16)*8ull);
            if(!entity || !*entity || !typed(read,base,*entity,weapon_entity_vtable)) continue;
            const auto comps=list(read,*entity+0x268,4096);if(!comps) continue;
            for(std::uint32_t c=0;c<comps->packed_count && c<64;++c) {
                const auto comp=value<std::uintptr_t>(read,comps->data+c*8ull);
                if(!comp || !*comp || !typed(read,base,*comp,info.comp_vtable)) continue;
                candidate=true;++entity_finds_;l.record=items->data+i*0x38ull;l.comp=*comp;l.decl=value<std::uintptr_t>(read,*comp+8).value_or(0);l.entity_id=rec->id;break;
            }
        }
        if(!candidate) return {};
        why=Why::no_component;
        if(value<std::uintptr_t>(read,l.comp+8)!=l.decl || !valid_id(l.entity_id)) return {};
        const auto entity=value<std::uintptr_t>(read,l.game+entities_offset+(l.entity_id>>16)*8ull);
        if(!entity || !typed(read,base,*entity,weapon_entity_vtable) || value<std::uint32_t>(read,*entity+0xa8)!=l.entity_id ||
           value<std::uintptr_t>(read,l.comp+0x10)!=entity) return {};
        l.entity=*entity;
        why=Why::bad_owner;
        if(value<std::uint32_t>(read,l.entity+0xac)!=l.player_id) return {}; // 63b5f0 / 639970 dereference the owner: it must be the player
        why=Why::not_ready;
        const auto identity=value<Identity>(read,l.comp+0x18); // a holstered weapon's component is retained, not started (as native_manual_equip)
        if(!identity || identity->generation==-1 || ((identity->flags>>2)&3u)==3u) return {};
        // v763 (user, headset v762: "only the weapon active last fires"): the copy's kind is often not the equipped one (a pistol beside the
        // native crossbow or a power), so its retained, unstarted component fires too when every field below is set; the calls run under SEH
        // and two faults in a row latch the module off.
        if((identity->flags&3u)!=2u) l.unstarted=true;
        const auto attributes=value<std::uintptr_t>(read,l.comp+0x30),camera=value<std::uintptr_t>(read,l.comp+0x48),
            inventory=value<std::uintptr_t>(read,l.comp+0x68),ammo=value<std::uintptr_t>(read,l.comp+0x98),bag=value<std::uintptr_t>(read,l.comp+0x40);
        if(!bag || !pointer(*bag) || !attributes || !pointer(*attributes) || !camera || !pointer(*camera) || inventory!=l.inventory || !ammo || !pointer(*ammo)) return {};
        const auto ammo_decl=value<std::uintptr_t>(read,*ammo),def=value<std::uintptr_t>(read,*ammo+0x10);
        if(!ammo_decl || !pointer(*ammo_decl) || !def || !pointer(*def)) return {};
        if(!list(read,l.comp+0xa8,64)) return {}; // the upgrade modifier list 63b5f0 and 63b3c0 walk
        l.ammo=*ammo;l.ammo_decl=*ammo_decl;l.projectile_def=*def;
        l.capacity=std::max(value<std::int32_t>(read,l.comp+0xa4).value_or(1),1);
        l.magazine=value<std::int32_t>(read,l.comp+0xa0).value_or(0);
        l.pool=pool_count(read,l.inventory,l.ammo_decl);
        l.native_ammo=l.ammo;
        {   // v1094: the upgrade conditions of this weapon (its m_upgrades +0x50; the last non-empty set is kept: a put-away weapon may clear it)
            unsigned conditions=0;bool any=false;
            if(const auto ups=list(read,l.comp+0x50,64)) for(std::uint32_t i=0;i<ups->packed_count;++i) {
                const auto item=value<std::uintptr_t>(read,ups->data+i*0x28ull);const auto decl=item && pointer(*item)?value<std::uintptr_t>(read,*item):std::nullopt;
                const auto name=decl && *decl?value<std::uintptr_t>(read,*decl+0x250):std::nullopt;any=true;
                std::array<char,24> text{};if(name && pointer(*name) && read(*name,text.data(),text.size()-1)) conditions|=ranged_fire::condition_of(text.data());
            }
            auto& kept=conditions_[static_cast<unsigned>(kind)&1u];if(any) kept=conditions;
            l.timing=ranged_fire::timing(static_cast<ranged_fire::Kind>(kind),kept);
        }
        if(kind==Shot::crossbow) { // 62b560's speed argument and extra flag, both from the player's attribute table
            const auto table=value<std::uintptr_t>(read,*attributes+0x48);
            const auto attribute=[&](std::uintptr_t offset)->std::optional<float> {
                const auto index=value<std::int32_t>(read,l.comp+offset);
                if(!index || *index<0 || !table || !pointer(*table)) return {};
                return value<float>(read,*table+0x24+static_cast<std::uintptr_t>(*index)*0x28);
            };
            if(const auto speed=attribute(0x158);speed && std::isfinite(*speed) && *speed>0) l.speed=*speed; // native passes 0 with no attribute; 1 is the plain bolt
            const auto flag=attribute(0x15c);l.mark=flag && *flag>0;
        }
        why=Why::ok;return l;
    }
    template<class Read> static int pool_count(Read& read,std::uintptr_t inventory,std::uintptr_t ammo_decl) {
        const auto items=rig::list(read,inventory+0x38,4096);if(!items) return 0;
        for(std::uint32_t i=0;i<items->packed_count;++i) {
            const auto rec=rig::value<Rec>(read,items->data+i*0x38ull);
            if(rec && rec->decl==ammo_decl) return std::max(rec->count,0);
        }
        return 0;
    }

    // The real native calls (RVAs verified by verify()).
    static Natives native_table() {
        return {&real_create,&real_launch,&real_sound,&real_component,&real_spend,&real_native_spend,&real_native_reload,&real_mark,&real_flash};
    }
    template<class Read> static bool verify(std::uintptr_t base,Read& read) {
        struct Pin {std::uintptr_t rva;std::array<unsigned char,16> code;};
        static const std::array<Pin,9> pins{{
            {0x63b5f0,{0x48,0x89,0x4c,0x24,0x08,0x53,0x56,0x41,0x55,0x48,0x81,0xec,0xb0,0x00,0x00,0x00}},
            {0x63b3c0,{0x40,0x53,0x57,0x41,0x56,0x48,0x81,0xec,0xa0,0x00,0x00,0x00,0x48,0x8b,0x81,0x98}},
            {0x877720,{0x48,0x89,0x5c,0x24,0x10,0x44,0x89,0x44,0x24,0x18,0x55,0x56,0x57,0x41,0x54,0x41}},
            {0x614f30,{0x4c,0x8b,0xdc,0x48,0x83,0xec,0x68,0x49,0xc7,0x43,0xb8,0xfe,0xff,0xff,0xff,0x48}},
            {0x614750,{0x4c,0x8b,0xdc,0x57,0x48,0x83,0xec,0x60,0x49,0xc7,0x43,0xb8,0xfe,0xff,0xff,0xff}},
            {0x615680,{0x83,0xfa,0xff,0x7e,0x1a,0x8b,0x41,0x40,0x25,0xff,0xff,0xff,0x00,0x3b,0xd0,0x7d}},
            {0x44ee80,{0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xec,0x40,0x48,0x8b,0x71,0x08,0x48,0x8b}}, // 44ee80 read from Dishonored2.exe
            {0x638190,{0x48,0x89,0x6c,0x24,0x20,0x56,0x41,0x56,0x41,0x57,0x48,0x83,0xec,0x20,0x81,0xa1}},   // v1094 modifier rebuild
            {0x63a490,{0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0x59,0x08,0x48,0x8b}}}}; // v1094 ammo switch
        for(const auto& p:pins) {std::array<unsigned char,16> got{};if(!read(base+p.rva,got.data(),got.size()) || got!=p.code) return false;}
        // 639970 is the NativeShotAim / NativeFireSeamTrace hook point: its own prologue, or MinHook's jump over it.
        static const std::array<unsigned char,16> launch{0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18,0x56,0x57,0x41,0x57,0x48,0x83};
        std::array<unsigned char,16> got{};
        if(!read(base+0x639970,got.data(),got.size())) return false;
        return got==launch || got[0]==0xe9 || (got[0]==0xff && got[1]==0x25);
    }

private:
    inline static std::mutex m_;inline static std::vector<Request> queue_;inline static bool pending_[2]{};
    inline static std::atomic<std::uint64_t> refused_[static_cast<unsigned>(Why::count_)]{};
    inline static std::atomic<std::uint64_t> refused_at_[2]{};
    inline static std::atomic<std::uint64_t> ready_at_[2]{},reload_until_[2]{};inline static std::atomic<int> last_kind_[2]{{-1},{-1}};
    inline static std::atomic<bool> latched_{false};inline static std::atomic<unsigned> consecutive_faults_{0};
    inline static std::array<std::array<std::uintptr_t,2>,2> copy_ammo_{};inline static std::array<std::uintptr_t,2> seen_ammo_{},seen_comp_{};inline static std::array<unsigned,2> conditions_{};inline static std::atomic<bool> wheel_done_{true};
    inline static std::array<std::uintptr_t,2> last_choice_{};   // v1277: the ammo item the wheel last equipped with the pistol (0) / crossbow (1); 0 = follow the native
    inline static std::array<std::atomic<int>,2> copy_kind_{{-1,-1}};inline static std::atomic<int> wheel_hand_{-1};inline static std::atomic<std::uint64_t> wheel_tick_{};
    inline static std::uint64_t ammo_looked_{};inline static std::array<std::uintptr_t,2> copy_comp_{},copy_last_{};inline static std::array<std::atomic<int>,2> test_ammo_{{-1,-1}};
    // v1117 copy WEAPON_FIRE gate (see gate_copy_fire): the detours read the hands' copy components from any thread the event pump runs on.
    using GateFn=std::uint64_t (__fastcall*)(std::uintptr_t,std::uintptr_t,std::uintptr_t,std::uintptr_t);
    inline static std::array<std::atomic<std::uintptr_t>,2> gate_comp_{},gate_ent_{},native_comp_{},native_ent_{};
    inline static std::array<GateFn,9> recv_original_{};inline static GateFn keys_original_{},sfx_original_{},sound_original_{};
    inline static std::array<void*,12> hook_at_{};inline static std::array<int,12> hook_status_{{-99,-99,-99,-99,-99,-99,-99,-99,-99,-99,-99,-99}};
    inline static std::array<std::atomic<std::uint64_t>,3> sfx_count_{};inline static std::array<std::atomic<std::uint64_t>,4> sound_count_{};
    inline static std::array<std::atomic<std::uintptr_t>,2> arm_ent_{};
    inline static std::array<std::array<std::atomic<std::uint64_t>,7>,3> recv_count_{},keys_count_{};
    inline static std::atomic<int> gate_state_{-1};inline static std::atomic<void (*)() noexcept> ungate_{nullptr};
    inline static std::array<std::uint64_t,2> dry_serial_{};
    static unsigned comp_class(std::uintptr_t comp) noexcept {
        if(!comp) return 2;
        if(comp==native_comp_[0].load(std::memory_order_relaxed) || comp==native_comp_[1].load(std::memory_order_relaxed)) return 0;
        return comp==gate_comp_[0].load(std::memory_order_relaxed) || comp==gate_comp_[1].load(std::memory_order_relaxed)?1:2;
    }
    static std::uint64_t gate(unsigned k,std::uintptr_t comp,std::uintptr_t a,std::uintptr_t b,std::uintptr_t c) {   // k: event_hooks index
        const auto& h=event_hooks[k];const unsigned cls=comp_class(comp);++recv_count_[cls][h.ev];
        if(h.ev==0 && cls==1) {++gated_;return 0;}   // a copy's WEAPON_FIRE: the hand-aimed shot already left
        if(h.ev==0) ++gate_passed_;
        const auto f=recv_original_[k];return f?f(comp,a,b,c):0;
    }
    template<unsigned K> static std::uint64_t __fastcall recv(std::uintptr_t comp,std::uintptr_t a,std::uintptr_t b,std::uintptr_t c) {return gate(K,comp,a,b,c);}
    static unsigned event_index(std::uint32_t ev) noexcept {return ev>=200 && ev<=205?ev-200:ev==242?6u:7u;}
    static unsigned actor_class(std::uintptr_t track) noexcept {   // the track group's actor ([[track+0x10]+0xd0]) as 0 native weapon, 1 a copy, 2 other
        const auto group=*reinterpret_cast<const std::uintptr_t*>(track+0x10);const auto actor=group?*reinterpret_cast<const std::uintptr_t*>(group+0xd0):0;
        if(actor && (actor==native_ent_[0].load() || actor==native_ent_[1].load())) return 0;
        return actor && (actor==gate_ent_[0].load() || actor==gate_ent_[1].load())?1u:2u;
    }
    static void count_keys(std::uintptr_t track,std::uintptr_t list) noexcept {   // reads only, every pointer guarded
        __try {
            const unsigned cls=actor_class(track);
            const auto data=*reinterpret_cast<const std::uintptr_t*>(list);const auto n=*reinterpret_cast<const std::uint32_t*>(list+8)&0xffffffu;
            for(unsigned i=0;data && i<n && i<64;++i) {
                const auto key=*reinterpret_cast<const std::uintptr_t*>(data+i*0x10ull);const auto prop=key?*reinterpret_cast<const std::uintptr_t*>(key+0x28):0;
                const unsigned e=prop?event_index(*reinterpret_cast<const std::uint32_t*>(prop+8)):7u;
                if(e<7) ++keys_count_[cls][e];
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }
    static std::uint64_t __fastcall keys_fired(std::uintptr_t track,std::uintptr_t list,std::uintptr_t a,std::uintptr_t b) {
        count_keys(track,list);const auto f=keys_original_;return f?f(track,list,a,b):0;
    }
    static void count_sfx(std::uintptr_t track,std::uintptr_t list) noexcept {
        __try {const auto n=*reinterpret_cast<const std::uint32_t*>(list+8)&0xffffffu;if(n) sfx_count_[actor_class(track)]+=n;} __except(EXCEPTION_EXECUTE_HANDLER) {}
    }
    static std::uint64_t __fastcall sfx_fired(std::uintptr_t track,std::uintptr_t list,std::uintptr_t a,std::uintptr_t b) {
        count_sfx(track,list);const auto f=sfx_original_;return f?f(track,list,a,b):0;
    }
    static void count_sound(std::uintptr_t audio) noexcept {   // the receiving audio component's entity (comp+0x10): 0 a copy, 1 a stand-in arm, 2 a native weapon, 3 other
        __try {const auto e=*reinterpret_cast<const std::uintptr_t*>(audio+0x10);
            const unsigned cls=e && (e==gate_ent_[0].load() || e==gate_ent_[1].load())?0u:e && (e==arm_ent_[0].load() || e==arm_ent_[1].load())?1u:
                e && (e==native_ent_[0].load() || e==native_ent_[1].load())?2u:3u;
            ++sound_count_[cls];} __except(EXCEPTION_EXECUTE_HANDLER) {}
    }
    static std::uint64_t __fastcall sound_posted(std::uintptr_t audio,std::uintptr_t message,std::uintptr_t a,std::uintptr_t b) {
        count_sound(audio);const auto f=sound_original_;return f?f(audio,message,a,b):0;
    }
public:
    inline static std::atomic<std::uint64_t> gated_{},gate_passed_{},dry_pulls_{};
private:
public:
    inline static std::atomic<std::uint64_t> own_shots_{},copy_ammo_sets_{},wheel_picks_{};
private:
    inline static std::array<unsigned,2> burst_left_{};inline static std::array<std::uint64_t,2> burst_next_{},reload_len_{};inline static std::array<Shot,2> burst_shot_{};
    inline static std::array<bool,2> reload_noted_{true,true},burst_follow_{};inline static std::array<std::uint64_t,2> shot_serial_{},reload_serial_{};
public:
    inline static std::atomic<std::uint64_t> ammo_swaps_{},ammo_splits_{},spreads_{},bursts_{};
private:
    // An entry of the weapon decl's m_ammoTypes (decl+0x188, count +0x190, stride 0x70): what comp+0x98 points at.
    template<class Read> static bool entry_valid(Read& read,std::uintptr_t comp,std::uintptr_t entry) {
        const auto decl=rig::value<std::uintptr_t>(read,comp+8);if(!decl || !rig::pointer(*decl)) return false;
        const auto first=rig::value<std::uintptr_t>(read,*decl+0x188);const auto count=rig::value<std::uint32_t>(read,*decl+0x190);
        if(!first || !count) return false;
        const auto n=*count&0xffffffu;
        return entry>=*first && entry<*first+n*0x70ull && (entry-*first)%0x70==0;
    }
    template<class Read> static std::optional<float> attribute_value(Read& read,std::uintptr_t comp,std::uintptr_t index_offset) {
        const auto attributes=rig::value<std::uintptr_t>(read,comp+0x30);const auto index=rig::value<std::int32_t>(read,comp+index_offset);
        if(!attributes || !rig::pointer(*attributes) || !index || *index<0) return std::nullopt;
        const auto table=rig::value<std::uintptr_t>(read,*attributes+0x48);if(!table || !rig::pointer(*table)) return std::nullopt;
        const auto v=rig::value<float>(read,*table+0x24+static_cast<std::uintptr_t>(*index)*0x28);
        return v && std::isfinite(*v)?v:std::nullopt;
    }
    static bool set_ammo(std::uintptr_t base,std::uintptr_t comp,std::uintptr_t entry) {
        __try {*reinterpret_cast<std::uintptr_t*>(comp+0x98)=entry;reinterpret_cast<void (__fastcall*)(std::uintptr_t)>(base+0x638190)(comp);return true;}
        __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    // The game's own ammo switch (0x63a490: unload the magazine, select, refresh the bolt, reload) by ammo item decl.
    static bool select_ammo(std::uintptr_t base,std::uintptr_t comp,std::uintptr_t ammo_item) {
        __try {reinterpret_cast<void (__fastcall*)(std::uintptr_t,std::uintptr_t)>(base+0x63a490)(comp,ammo_item);return true;}
        __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    // 44f030 without its camera: the spread-area explosion event (weapon decl +0x238, +0x240 with flag 8, +0x248 with flag 0x10) at the muzzle
    // plus the decl's offset (+0x250..0x258) in the shot's frame, along the shot; posted through the game's vtable +0x3a0.
    template<class Read> static void spread(std::uintptr_t base,Read& read,const Located& l,const float* origin,const float* axis,std::uint8_t flags) {
        const auto decl=rig::value<std::uintptr_t>(read,l.comp+8);if(!decl || !rig::pointer(*decl)) return;
        const auto area=rig::value<std::uintptr_t>(read,*decl+((flags&8)?0x240:(flags&0x10)?0x248:0x238));if(!area || !rig::pointer(*area)) return;
        std::array<float,3> off{};if(!read(*decl+0x250,off.data(),sizeof(off))) return;
        alignas(16) std::array<unsigned char,0x40> ev{};
        const std::uintptr_t a=*area;std::memcpy(ev.data(),&a,8);std::memcpy(ev.data()+8,&l.entity_id,4);std::memcpy(ev.data()+0xc,&l.player_id,4);
        float pos[3],dir[3];
        for(unsigned c=0;c<3;++c) {pos[c]=origin[c]+off[0]*axis[c]+off[1]*axis[3+c]+off[2]*axis[6+c];dir[c]=axis[c];}
        std::memcpy(ev.data()+0x14,pos,12);std::memcpy(ev.data()+0x20,dir,12);
        if(post_event(base,l.game,ev.data())) ++spreads_;else fault();
    }
    static bool post_event(std::uintptr_t base,std::uintptr_t game,const void* ev) {
        (void)base;
        __try {const auto vt=*reinterpret_cast<const std::uintptr_t*>(game);reinterpret_cast<void (__fastcall*)(std::uintptr_t,const void*)>(*reinterpret_cast<const std::uintptr_t*>(vt+0x3a0))(game,ev);return true;}
        __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    // After a shot: the hand's timers from the weapon's upgrades, the burst follow-ups, the serials the copy's upgrade animations play from.
    static void finish_shot(const Shot& s,const Located& l,std::uint64_t now,ranged_fire::HandState& hand,std::uint8_t flags) {
        const bool burst=s.kind==Shot::pistol && (flags&1);
        auto& left=burst_left_[s.side];
        burst_follow_[s.side]=left!=0;
        if(burst && !left && hand.loaded>1) {left=2;burst_shot_[s.side]=s;++bursts_;}   // first shot of a burst: two more follow
        else if(left) --left;
        const std::uint64_t cool=left?ranged_fire::burst_gap_ms:l.timing.cool;
        const bool was=hand.reloading;
        hand.fired(now,auto_reload.load(),cool,l.timing.reload);
        if(left) burst_next_[s.side]=now+ranged_fire::burst_gap_ms;
        if(hand.reloading && !was) {reload_len_[s.side]=l.timing.reload;reload_noted_[s.side]=false;left=0;}
        ++shot_serial_[s.side];
        publish(s.side,hand);
    }
    // Which hand's choice an ammo change is. The wheel's equip sets the weapon's ammo (comp+0x98) for both hands; a choice made by a hand
    // that holds a copy of that weapon is that copy's own: kept for the copy, and the native weapon gets its ammo back through the game's
    // own switch. A change by any other hand (within 2.5 s of its wheel choice or not) is the native weapon's.
    template<class Read> static void track_ammo(std::uintptr_t base,Read& read) {
        const auto now=clock();if(now-ammo_looked_<100) return;
        ammo_looked_=now;
        for(unsigned k=0;k<2;++k) {
            Why why{};const auto l=locate(base,read,static_cast<Shot::Kind>(k),why);if(!l) continue;
            if(seen_comp_[k]!=l->comp) {seen_comp_[k]=l->comp;seen_ammo_[k]=l->ammo;copy_ammo_[0][k]=copy_ammo_[1][k]=0;last_choice_[k]=0;continue;}
            const auto cur=l->ammo;
            const int w=wheel_hand_.load();const auto since=now-wheel_tick_.load();const bool recent=since<2500;
            // a wheel choice by a hand that now holds a copy of this weapon, once the equip has run (0.4 s): what it selected is the copy's
            if(recent && since>=400 && !wheel_done_.load() && w>=0 && copy_kind_[w].load()==static_cast<int>(k)) {
                wheel_done_=true;copy_ammo_[w][k]=cur;
                if(cur!=seen_ammo_[k]) {
                    const auto back=rig::value<std::uintptr_t>(read,seen_ammo_[k]);
                    if(entry_valid(read,l->comp,seen_ammo_[k]) && back && *back && select_ammo(base,l->comp,*back)) ++ammo_splits_;
                    else seen_ammo_[k]=cur;
                }
                continue;
            }
            if(cur!=seen_ammo_[k] && !recent) {seen_ammo_[k]=cur;last_choice_[k]=0;}   // the native hand's own choice (or a pickup, or the game switching an empty type): accepted once no wheel choice is pending
        }
        for(unsigned side=0;side<2;++side) if(const int t=test_ammo_[side].exchange(-1);t>=0) { // the test command
            const int k=copy_kind_[side].load();const auto cc=copy_comp_[side];if(k<0 || k>1 || !cc) continue;
            const auto decl=rig::value<std::uintptr_t>(read,cc+8);const auto first=decl?rig::value<std::uintptr_t>(read,*decl+0x188):std::nullopt;
            if(first && *first) copy_ammo_[side][k]=*first+static_cast<std::uintptr_t>(t)*0x70;
        }
        for(unsigned side=0;side<2;++side) { // v1096: a real copy selects its hand's ammo itself (0x63a490 on its component: the bolt and magazine follow)
            const int k=copy_kind_[side].load();const auto cc=copy_comp_[side];if(k<0 || k>1 || !cc) continue;
            const auto chosen=copy_ammo_[side][k] || !last_choice_[k]?0:ammo_entry(read,cc,last_choice_[k]);   // v1277: a new copy = the wheel's last bolt
            const auto want=copy_ammo_[side][k]?copy_ammo_[side][k]:chosen?chosen:seen_ammo_[k];if(!want || !entry_valid(read,cc,want)) continue;
            copy_ammo_[side][k]=want; // v1101: a copy starts as the weapon it copies and then keeps its own ammo: the native hand's later choice is that hand's
            const auto have=rig::value<std::uintptr_t>(read,cc+0x98);if(!have || *have==want) continue;
            const auto item=rig::value<std::uintptr_t>(read,want);
            if(item && *item && select_ammo(base,cc,*item)) ++copy_ammo_sets_;
        }
    }
    inline static ranged_fire::HandState hands_[2]{};
    inline static std::vector<std::string> shots_,plans_;
    struct Track {
        std::uintptr_t entity{},component{};std::uint32_t id{};std::uint64_t born{};unsigned side{},kind{};
        std::array<float,3> start{},at100{},last{};unsigned comps{},started0{},started100{};std::uint32_t flags0{},flags100{};bool sampled{};float moved{};
    };
    inline static std::vector<Track> tracks_;
    static bool valid_id(std::uint32_t id) noexcept {return id && id!=0x3fff0010 && (id>>16)<0x3fff;}
    static bool refuse(Why why) {++refused_[static_cast<unsigned>(why)];return false;}
    static bool enqueue(const Request& r) {
        std::lock_guard<std::mutex> l(m_);
        if(pending_[r.shot.side]) return refuse(Why::pending);
        if(queue_.size()>=8) return refuse(Why::queue_full);
        queue_.push_back(r);pending_[r.shot.side]=true;return true;
    }
    static void keep(std::vector<std::string>& v,std::string s) {std::lock_guard<std::mutex> l(m_);v.push_back(std::move(s));if(v.size()>8) v.erase(v.begin());}
    struct LaunchScope {LaunchScope() {++ranged_fire_launch_depth;} ~LaunchScope() {--ranged_fire_launch_depth;}};
    static std::string text3(const float* v) {char b[64];std::snprintf(b,sizeof(b),"[%.3f,%.3f,%.3f]",v[0],v[1],v[2]);return b;} // valid JSON (status)

    template<class Read> static Why handle(std::uintptr_t base,Read& read,const Natives& n,const Request& r,std::uint64_t now) {
        const auto& s=r.shot;Why why{};
        auto l=locate(base,read,s.kind,why);if(!l) return why;
        if(const auto cc=copy_comp_[s.side];cc && s.entity && rig::typed(read,base,cc,kind_info(s.kind).comp_vtable) && rig::value<std::uintptr_t>(read,cc+0x10)==s.entity) {
            // v1096: the real copy fires itself: its component, its weapon entity, its selected ammo, its magazine
            const auto ammo=rig::value<std::uintptr_t>(read,cc+0x98);
            const auto ammo_decl=ammo && rig::pointer(*ammo)?rig::value<std::uintptr_t>(read,*ammo):std::nullopt;
            const auto def=ammo && rig::pointer(*ammo)?rig::value<std::uintptr_t>(read,*ammo+0x10):std::nullopt;
            if(ammo_decl && rig::pointer(*ammo_decl) && def && rig::pointer(*def)) {
                l->comp=cc;l->entity=s.entity;l->entity_id=s.entity_id;l->ammo=l->native_ammo=*ammo;l->ammo_decl=*ammo_decl;l->projectile_def=*def;
                l->capacity=std::max(rig::value<std::int32_t>(read,cc+0xa4).value_or(1),1);l->magazine=rig::value<std::int32_t>(read,cc+0xa0).value_or(0);
                l->pool=pool_count(read,l->inventory,l->ammo_decl);l->own=true;++own_shots_;
            }
        }
        if(const auto own=l->own?0:copy_ammo_[s.side][static_cast<unsigned>(s.kind)&1u];own && own!=l->ammo && entry_valid(read,l->comp,own)) { // v1094: this hand's own ammo
            const auto ammo_decl=rig::value<std::uintptr_t>(read,own),def=rig::value<std::uintptr_t>(read,own+0x10);
            if(ammo_decl && rig::pointer(*ammo_decl) && def && rig::pointer(*def)) {l->ammo=own;l->ammo_decl=*ammo_decl;l->projectile_def=*def;l->pool=pool_count(read,l->inventory,l->ammo_decl);}
        }
        if(s.kind==Shot::pistol && !l->own) { // v1094: the magazine as 0x44e720 computes it (GUN_AMMO_CAPACITY at comp+0x160, 1 for an ammo that does not fit)
            const auto fits=rig::value<std::uint8_t>(read,l->ammo+0x48).value_or(1);
            if(!fits) l->capacity=1;
            else if(const auto cap=attribute_value(read,l->comp,0x160);cap && *cap>=1.f && *cap<100000.f) l->capacity=static_cast<int>(std::floor(*cap+.5f));
        }
        auto& hand=hands_[s.side];
        const auto kind=static_cast<ranged_fire::Kind>(s.kind);
        const bool shared=shared_native_magazine.load() || l->own;
        const bool finished=hand.sync(now,kind,l->capacity,l->pool);
        if(shared) hand.loaded=std::min(l->magazine,l->capacity); // the component's own magazine is the truth in that mode
        if(finished && shared && !dry_run.load()) { // the timer replaces the native reload scene: refill the component's magazine (63a2b0, with the HUD update)
            if(n.native_reload(base,l->comp)) {++reloads_;hand.loaded=std::min(l->pool,l->capacity);}
        }
        last_kind_[s.side]=static_cast<int>(s.kind);
        if(r.reload) {
            if(dry_run.load()) {++dry_plans_;keep(plans_,"reload side="+std::to_string(s.side)+" cap="+std::to_string(l->capacity)+" pool="+std::to_string(l->pool));return Why::ok;}
            if(!hand.reload(now,l->capacity,l->pool,l->timing.reload)) return Why::full;
            reload_len_[s.side]=l->timing.reload;reload_noted_[s.side]=false;
            ++reloads_;publish(s.side,hand);return Why::ok;
        }
        if(l->own && !r.reload && !hand.reloading && hand.loaded<=0 && l->pool>0 && hand.reload(now,l->capacity,l->pool,l->timing.reload)) { // a fresh copy starts empty
            reload_len_[s.side]=l->timing.reload;reload_noted_[s.side]=false;publish(s.side,hand);return Why::reloading;}
        const Why check=hand.check(now,l->pool);
        publish(s.side,hand);
        if(check!=Why::ok) {if(r.burst) burst_left_[s.side]=0;return check;}
        if(dry_run.load()) {plan(*l,s);return Why::ok;}
        return execute(base,read,n,*l,r,now,hand);
    }
    static void publish(unsigned side,const ranged_fire::HandState& h) {
        ready_at_[side]=h.ready_at;reload_until_[side]=h.reloading?h.reload_done:0;
    }
    static void plan(const Located& l,const Shot& s) {
        ++dry_plans_;
        char b[768];
        std::snprintf(b,sizeof(b),"%s side=%u comp=%p entity=%p ammo=%p projdef=%p cap=%d mag=%d pool=%d speed=%.2f origin=%s fwd=%s -> create(63b5f0); launch(639970,speed,0,0,-1,0)%s; %s; sound(63b3c0)@muzzle=%d",
            s.kind==Shot::crossbow?"crossbow":"pistol",s.side,reinterpret_cast<void*>(l.comp),reinterpret_cast<void*>(l.entity),
            reinterpret_cast<void*>(l.ammo_decl),reinterpret_cast<void*>(l.projectile_def),l.capacity,l.magazine,l.pool,static_cast<double>(l.speed),
            text3(s.origin).c_str(),text3(s.axis).c_str(),l.mark?"+flag":"",
            shared_native_magazine.load()?"spend(639360 via vtable)":"spend(614f30,615680,614750)",play_sound.load()?1:0);
        keep(plans_,b);
    }
    template<class Read> static Why execute(std::uintptr_t base,Read& read,const Natives& n,const Located& l,const Request& r,std::uint64_t now,ranged_fire::HandState& hand) {
        const auto& s=r.shot;
        alignas(16) float origin[4]{s.origin[0],s.origin[1],s.origin[2],0};
        alignas(16) float axis[12]{};std::memcpy(axis,s.axis,sizeof(float)*9);
        // v1094: the hand's own ammo for this shot only: comp+0x98 and the modifier list it keys (0x638190 rebuilds +0xa8/+0xb0/+0xb8 from
        // m_upgrades for the current ammo), put back right after on the same game-thread call. 0x63b5f0, 0x63b3c0, 0x639360 read +0x98 live.
        struct AmmoSwap {std::uintptr_t base,comp,back;bool on;~AmmoSwap() {if(on) set_ammo(base,comp,back);}};
        AmmoSwap swap{base,l.comp,l.native_ammo,false};
        if(l.ammo!=l.native_ammo) {if(!set_ammo(base,l.comp,l.ammo)) return fault();swap.on=true;++ammo_swaps_;}
        const auto flags=rig::value<std::uint8_t>(read,l.comp+0xb8).value_or(0);
        const bool no_bullet=(flags&0x20)!=0;
        if(s.kind==Shot::pistol && rig::value<std::uint8_t>(read,l.ammo+0x4a).value_or(0)) spread(base,read,l,origin,axis,flags); // 44e0c0's spread-area explosion (the shotgun's damage)
        std::uintptr_t projectile{};bool ok;
        if(!no_bullet) {
            {LaunchScope scope;ok=n.create(base,l.comp,projectile);}
            if(!ok) return fault();
            if(!projectile) return Why::spawn_failed; // nothing was spent
            ++created_;
        }
        if(no_bullet) {
            const bool spent=l.own?n.native_spend(base,l.comp):n.spend(base,l.inventory,l.ammo_decl);if(!spent) fault();else ++spent_;
            if(play_sound.load()) n.sound(base,l.comp,l.entity,origin,axis);
            finish_shot(s,l,now,hand,flags);return Why::ok;
        }
        Track t{};t.entity=projectile;t.id=rig::value<std::uint32_t>(read,projectile+0xa8).value_or(0);t.born=now;t.side=s.side;t.kind=s.kind;
        {LaunchScope scope;ok=n.launch(base,l.comp,projectile,origin,axis,l.speed);}
        if(!ok) return fault();
        ++launched_;
        t.component=n.component(base,projectile,projectile_info_rva);
        if(l.mark && t.component) n.mark(base,t.component);
        if(const auto p=rig::value<std::array<float,3>>(read,projectile+0x300)) t.start=t.last=*p;
        census(read,projectile,t.comps,t.started0);
        t.flags0=t.component?rig::value<rig::Identity>(read,t.component+0x18).value_or(rig::Identity{}).flags:0;
        comps_total_+=t.comps;comps_started0_+=t.started0;
        pool_before_=l.pool;mag_before_=l.magazine;
        bool clean=true;
        const bool spent=(shared_native_magazine.load() || l.own)?n.native_spend(base,l.comp):n.spend(base,l.inventory,l.ammo_decl);
        if(!spent) {fault();clean=false;}
        // The firing gun's entity (a held copy), when it is still the live one; else the record's.
        auto gun=l.entity;
        if(s.entity && s.entity!=l.entity && valid_id(s.entity_id) && rig::value<std::uintptr_t>(read,l.game+entities_offset+(s.entity_id>>16)*8ull)==s.entity &&
           rig::value<std::uint32_t>(read,s.entity+0xa8)==s.entity_id) gun=s.entity;
        if(play_sound.load() && !n.sound(base,l.comp,gun,origin,axis)) {fault();clean=false;} // the shot has left: count it, keep going
        if(muzzle_flash.load() && n.flash && !n.flash(base,l.comp,gun,origin,axis)) {fault();clean=false;}
        const int pool_after=pool_count(read,l.inventory,l.ammo_decl);
        pool_after_=pool_after;mag_after_=rig::value<std::int32_t>(read,l.comp+0xa0).value_or(-1);
        if(spent) {++spent_;if(pool_after!=l.pool-1) ++ammo_mismatch_;}
        finish_shot(s,l,now,hand,flags);
        ++fired_;if(l.unstarted) ++unstarted_shots_;if(clean) consecutive_faults_=0;
        if(tracks_.size()>=8) tracks_.erase(tracks_.begin());
        tracks_.push_back(t);
        return Why::ok;
    }
    static Why fault() {
        if(++consecutive_faults_>=2) latched_=true;
        return Why::fault;
    }
    template<class Read> static void census(Read& read,std::uintptr_t entity,unsigned& total,unsigned& started) {
        total=started=0;
        const auto comps=rig::list(read,entity+0x268,4096);if(!comps) return;
        for(std::uint32_t i=0;i<comps->packed_count && i<64;++i) {
            const auto c=rig::value<std::uintptr_t>(read,comps->data+i*8ull);
            if(!c || !rig::pointer(*c)) continue;
            ++total;if(rig::started(read,*c)) ++started;
        }
    }
    template<class Read> static void update_tracks(std::uintptr_t base,Read& read,std::uint64_t now) {
        for(std::size_t i=0;i<tracks_.size();) {
            auto& t=tracks_[i];bool done=false;
            const auto game=rig::value<std::uintptr_t>(read,base+game_rva);
            const auto live=game && valid_id(t.id) && rig::value<std::uintptr_t>(read,*game+entities_offset+(t.id>>16)*8ull)==t.entity &&
                rig::value<std::uint32_t>(read,t.entity+0xa8)==t.id;
            const auto age=now-t.born;
            if(live) {
                if(const auto p=rig::value<std::array<float,3>>(read,t.entity+0x300)) t.last=*p;
                if(!t.sampled && age>=100) {
                    t.sampled=true;t.at100=t.last;
                    t.moved=std::sqrt((t.at100[0]-t.start[0])*(t.at100[0]-t.start[0])+(t.at100[1]-t.start[1])*(t.at100[1]-t.start[1])+(t.at100[2]-t.start[2])*(t.at100[2]-t.start[2]));
                    unsigned total{};census(read,t.entity,total,t.started100);comps_started100_+=t.started100;
                    t.flags100=t.component?rig::value<rig::Identity>(read,t.component+0x18).value_or(rig::Identity{}).flags:0;
                    if(t.component && rig::started(read,t.component)) ++proj_comp_started100_;
                    if(t.moved>0.1f) ++moved_;else ++stationary_;
                }
                if(age>4000) {++alive_4s_;done=true;}
            } else {
                if(age<4000) ++ended_early_; // gone before it could have flown out of range: an impact or a removal
                done=true;
            }
            if(done) {
                char b[512];
                std::snprintf(b,sizeof(b),"{\"side\":%u,\"kind\":%u,\"id\":%u,\"age_ms\":%llu,\"live\":%s,\"comps\":%u,\"started0\":%u,\"started100\":%u,\"proj_flags0\":%u,\"proj_flags100\":%u,\"moved_100\":%.3f,\"start\":%s,\"last\":%s}",
                    t.side,t.kind,t.id,static_cast<unsigned long long>(age),live?"true":"false",t.comps,t.started0,t.started100,t.flags0,t.flags100,
                    static_cast<double>(t.moved),text3(t.start.data()).c_str(),text3(t.last.data()).c_str());
                keep(shots_,b);tracks_.erase(tracks_.begin()+static_cast<std::ptrdiff_t>(i));
            } else ++i;
        }
    }

    // ---- real natives (each under SEH; no object with a destructor lives in these frames) ----
    static bool real_create(std::uintptr_t base,std::uintptr_t comp,std::uintptr_t& projectile) {
        using Fn=std::uintptr_t (__fastcall*)(std::uintptr_t);
        __try {projectile=reinterpret_cast<Fn>(base+0x63b5f0)(comp);return true;} __except(EXCEPTION_EXECUTE_HANDLER) {projectile=0;return false;}
    }
    static bool real_launch(std::uintptr_t base,std::uintptr_t comp,std::uintptr_t projectile,const float* origin,const float* axis,float speed) {
        using Fn=void (__fastcall*)(std::uintptr_t,std::uintptr_t,const float*,const float*,float,std::uintptr_t,std::uint64_t,int,int);
        __try {reinterpret_cast<Fn>(base+0x639970)(comp,projectile,origin,axis,speed,0,0,-1,0);return true;} __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    // 63b3c0 / 44ee80 build their emitter and FX at the weapon entity's pose (+0x300 position, +0x30c frame; both read the entity from comp+0x10, VERIFIED
    // in the decompile): run with `entity` as the component's weapon and the muzzle frame as its pose, then put both back.
    // v900 (user, headset v863: "the flash comes from the original pistol, not the copy I fire"): 63b3c0 spawns an idEntityFx (type 0x39cf460, the shot's
    // flash and sound) and first overwrites that pose with a joint of the object behind the handle {id comp+0x20, ptr +0x28} (1403da910 -> 14053dac0):
    // the native gun's own muzzle. The handle reads as empty (id -1) for the call, so the pose stays the muzzle frame given here.
    static void swapped(std::uintptr_t base,std::uintptr_t rva,std::uintptr_t comp,std::uintptr_t entity,const float* origin,const float* axis) {
        using Fn=void (__fastcall*)(std::uintptr_t,char);
        unsigned char saved[48]{};bool swapped=false;const auto owner=reinterpret_cast<std::uintptr_t*>(comp+0x10);const auto kept=*owner;
        const auto handle=reinterpret_cast<std::int32_t*>(comp+0x20);const auto handle_kept=*handle;
        __try {
            std::memcpy(saved,reinterpret_cast<const void*>(entity+0x300),sizeof(saved));
            std::memcpy(reinterpret_cast<void*>(entity+0x300),origin,12);std::memcpy(reinterpret_cast<void*>(entity+0x30c),axis,36);*owner=entity;*handle=-1;swapped=true;
            reinterpret_cast<Fn>(base+rva)(comp,0);
        } __finally {if(swapped) {*owner=kept;*handle=handle_kept;std::memcpy(reinterpret_cast<void*>(entity+0x300),saved,sizeof(saved));}}
    }
    static bool real_sound(std::uintptr_t base,std::uintptr_t comp,std::uintptr_t entity,const float* origin,const float* axis) {
        __try {swapped(base,0x63b3c0,comp,entity,origin,axis);return true;} __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    static bool real_flash(std::uintptr_t base,std::uintptr_t comp,std::uintptr_t entity,const float* origin,const float* axis) {
        __try {swapped(base,0x44ee80,comp,entity,origin,axis);return true;} __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    static std::uintptr_t real_component(std::uintptr_t base,std::uintptr_t entity,std::uintptr_t info_rva) {
        using Fn=std::uintptr_t (__fastcall*)(std::uintptr_t,std::uintptr_t,int);
        __try {return reinterpret_cast<Fn>(base+0x877720)(entity,base+info_rva,0);} __except(EXCEPTION_EXECUTE_HANDLER) {return 0;}
    }
    // The inventory tail of 639360: find the ammo record, count the shot in its fired counter, remove one round.
    static bool real_spend(std::uintptr_t base,std::uintptr_t inventory,std::uintptr_t ammo_decl) {
        using Find=int (__fastcall*)(std::uintptr_t,std::uintptr_t);using Count=void (__fastcall*)(std::uintptr_t,int);using Remove=int (__fastcall*)(std::uintptr_t,std::uintptr_t,int);
        __try {
            const int index=reinterpret_cast<Find>(base+0x614f30)(inventory,ammo_decl);
            reinterpret_cast<Count>(base+0x615680)(inventory,index);
            reinterpret_cast<Remove>(base+0x614750)(inventory,ammo_decl,1);
            return true;
        } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    // vtable +0x140 = ConsumeAmmo (639360: comp+0xa0 -1, the same inventory tail, HUD update), +0x148 = reload (63a2b0).
    static bool virtual_call(std::uintptr_t comp,std::uintptr_t slot) {
        using Fn=void (__fastcall*)(std::uintptr_t);
        __try {const auto vtable=*reinterpret_cast<const std::uintptr_t*>(comp);reinterpret_cast<Fn>(*reinterpret_cast<const std::uintptr_t*>(vtable+slot))(comp);return true;}
        __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    static bool real_native_spend(std::uintptr_t,std::uintptr_t comp) {return virtual_call(comp,0x140);}
    static bool real_native_reload(std::uintptr_t,std::uintptr_t comp) {return virtual_call(comp,0x148);}
    static bool real_mark(std::uintptr_t,std::uintptr_t projectile_component) {
        __try {*reinterpret_cast<volatile unsigned char*>(projectile_component+0xa5)|=2;return true;} __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
};
} // namespace d2vr
