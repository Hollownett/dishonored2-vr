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
#include "game_profile.hpp"
#include "game_rig.hpp"
#include "gameplay_aim.hpp"
#include "crash_observer.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <type_traits>
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
// v1284 (plan 2026-11-13 4.4): who owns a bolt choice, as pure rules (the same decisions NativeRangedFire::wheel_pick / track_ammo make on the
// game's memory; tests/bolt_owner_check.cpp tables them).
// A wheel choice of an ammo for weapon kind k by hand h: that hand's copy of k owns it (the game is handed the native's current ammo); else, with
// the native in slot 1 drawn by the OTHER hand, it is kept for the copy this hand is about to get ("same item" route) and the native keeps its own;
// else the native comes to this hand and the choice is the native's own.
enum class PickRoute : unsigned {copy_own,same_item_keep,native_own};
constexpr PickRoute pick_route(bool hand_copy_of_kind,int slot1_holder,int hand,bool native_in_slot1) noexcept {
    return hand_copy_of_kind?PickRoute::copy_own:slot1_holder>=0 && slot1_holder!=hand && native_in_slot1?PickRoute::same_item_keep:PickRoute::native_own;
}
// v145764 (R3 G-b1): the copy kind NativeRangedFire::note_copies gets for a forearm: the mod's Voltaic copy (DOTO) is a crossbow-kind copy (1),
// so track_ammo keeps its component on its own bolt; every other copy keeps the arm set's kind (-1 none, 0 pistol, 1 crossbow).
constexpr int copy_kind_for(bool voltaic_game,bool voltaic_copy_here,int arm_kind) noexcept {return voltaic_game && voltaic_copy_here?1:arm_kind;}
// The native instance's ammo when the game shows another one than its own: its own type ran out (or is unknown) -> the game's switch stands;
// else it is switched back to its own.
enum class NativeAmmo : unsigned {keep,follow,switch_back};
constexpr NativeAmmo native_ammo(bool current_is_own,bool own_known,int own_pool) noexcept {
    return current_is_own?NativeAmmo::keep:!own_known || own_pool<=0?NativeAmmo::follow:NativeAmmo::switch_back;
}
// A copy instance's ammo: its own (kept through tosses, catches and hand changes), else this hand's last choice for the weapon, else the native's.
template<class T> constexpr T copy_ammo_want(T own,bool own_valid,T hand_choice,T native) noexcept {return own && own_valid?own:hand_choice?hand_choice:native;}
// v1372 (user, headset v1381: "I chose the sleep bolt and got the regular one"): the copy that comes to a hand is often an entity the copy
// pool hands back (NativeWeaponHands::pool_acquire reuses a hidden one of the same weapon, whichever hand had it), still owning the bolt it
// had then, and copy_ammo_want puts that own before the hand's wheel choice. A copy NEW to a hand (not the instance it showed at the last
// look) therefore starts from, in order: this hand's wheel choice of that weapon made within pick_ms (the copy it was chosen for: over a
// power, the same item the other hand draws), the bolt of this hand's native of that weapon seen within carry_ms (displaced into this
// copy when the other hand took the native slot: the same weapon to the player), else nothing new (its own: a caught or picked-up copy keeps
// its bolt; then copy_ammo_want).
inline constexpr std::uint64_t pick_ms=4000,carry_ms=1500;
enum class FreshTake : unsigned {none,pick,native};
constexpr FreshTake fresh_copy_take(bool new_here,bool picked,std::uint64_t pick_age,bool native_same_weapon,std::uint64_t native_age) noexcept {
    return !new_here?FreshTake::none:picked && pick_age<pick_ms?FreshTake::pick:native_same_weapon && native_age<carry_ms?FreshTake::native:FreshTake::none;
}
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
        bool charged{},overcharged{};   // v145745 (DOTO): the Voltaic copy's charged shot (the charge upgrade's projectile), overcharged (held 4 s)
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
    static void note_copies(int left,int right,int slot1_hand=-1) {copy_kind_[0]=left;copy_kind_[1]=right;slot1_hand_=slot1_hand;}
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
        // DOTO (game_traits::ranged_player_weapon_is_voltaic): the forearm Voltaic copy fires through the native gun's component, so both guns
        // share the native's ammo: every wheel choice is the game's own, whichever hand opened the wheel (no copy-owned ammo, no kept choice)
        // v145750 (user, DOTO headset v145749: "when I select the electric bolt from my right forearm it goes to my left forearm"; the copy
        // "rendered with a wrong bolt"): as Dishonored 2 (v1101), a choice made by the wheel of a hand whose forearm carries the mod's copy is that
        // copy's own (copy_ammo_, swapped in for its shots by handle/execute); the game is handed the native gun's current ammo so the engine's
        // gun keeps its own. The hand that carries the engine's gun (voltaic_native_hands_) chooses for it, as the game does.
        if constexpr(game_traits::ranged_player_weapon_is_voltaic) {
            if(!item || !ammo) return 0;
            const int w=wheel_hand_.load();if(w<0 || w>1) return 0;
            const unsigned side=static_cast<unsigned>(w);
            // v145764 (user: "make sure that voltaic gun and bolt type is rendered correctly"; R3 G-b1, G-b2): a hand whose forearm carries the mod's
            // copy (note_copies kind 1 + its component) chooses for that copy's OWN component, through the game's own ammo switch (the copy's
            // model, magazine and shots follow); the engine's gun is handed back its current bolt below and keeps it.
            const auto cc=copy_comp_[side];const bool copy_here=cc && copy_kind_[side].load()==1;
            // v145751 (user: "the left forearm does not always pick up the right bolt"): the left forearm shows the ENGINE's gun whenever it
            // carries one (or gets it from this very choice), so the left wheel chooses for the engine's gun unless the left forearm carries a copy;
            // v145750 asked whether it carried it NOW, and a left choice made while only the right had the gun went to an unused left copy slot.
            if(side==0 && !copy_here) return 0;   // the engine's gun: the game's own choice
            (void)voltaic_native_hands_;
            Why wk{};const auto lk=locate(base,read,Shot::crossbow,wk);
            if(!lk || rig::value<std::uintptr_t>(read,lk->record)!=item) return 0;   // not the Voltaic Gun
            if(copy_here) {
                const auto entry=ammo_entry(read,cc,ammo);if(!entry) return 0;
                set_own(cc,entry);copy_ammo_[side][Shot::crossbow]=entry;hand_choice_[side][Shot::crossbow]=ammo;pick_[side][Shot::crossbow]={};
                ++wheel_picks_;note_pick(side,Shot::crossbow,0,ammo_index(read,cc,entry));
                const auto have=rig::value<std::uintptr_t>(read,cc+0x98);
                if(have && *have!=entry && select_ammo(base,cc,ammo)) ++copy_ammo_sets_;
                const bool slot1=rig::value<std::uintptr_t>(read,lk->inventory+0x70)==lk->record;
                const auto keep_ammo=rig::value<std::uintptr_t>(read,lk->ammo);
                return slot1 && keep_ammo && *keep_ammo?*keep_ammo:0;
            }
            const auto entry=ammo_entry(read,lk->comp,ammo);if(!entry) return 0;
            copy_ammo_[side][Shot::crossbow]=entry;hand_choice_[side][Shot::crossbow]=ammo;++wheel_picks_;++copy_ammo_sets_;
            note_pick(side,Shot::crossbow,0,ammo_index(read,lk->comp,entry));
            const bool in_slot1=rig::value<std::uintptr_t>(read,lk->inventory+0x70)==lk->record;
            const auto keep=rig::value<std::uintptr_t>(read,lk->ammo);
            return in_slot1 && keep && *keep?*keep:0;
        }
        if(!item || !ammo) return 0;
        const int w=wheel_hand_.load();if(w<0 || w>1) return 0;
        const unsigned side=static_cast<unsigned>(w);
        // v1279 (user, v1278: "a weapon behaves as a weapon and the user selects what the user selects"; bolts mixed after tosses and pickups):
        // every crossbow / pistol INSTANCE owns its ammo (own_: per component, the native's own is seen_ammo_). A wheel choice changes only the
        // instance the choosing hand holds or is about to hold:
        //  - the hand holds a copy of the chosen weapon: that copy's own; the game is handed the native's current ammo (no change there);
        //  - the native is in slot 1 and drawn by the OTHER hand ("same item" route: this hand gets a copy): the choice is kept for the copy
        //    to come (hand_choice_), the native keeps its own;
        //  - otherwise the native goes to this hand: the choice is the native's own.
        // track_ammo then holds every instance at its own (the game may change the native only when its own type ran out).
        for(unsigned kk=0;kk<2;++kk) {
            Why wk{};const auto lk=locate(base,read,static_cast<Shot::Kind>(kk),wk);
            if(!lk || rig::value<std::uintptr_t>(read,lk->record)!=item) continue;
            const auto native_keep=[&]()->std::uintptr_t {const auto keep=rig::value<std::uintptr_t>(read,lk->ammo);return keep && *keep?*keep:0;};
            const bool in_slot1=rig::value<std::uintptr_t>(read,lk->inventory+0x70)==lk->record;
            ++wheel_picks_;
            const auto cc=copy_comp_[side];
            switch(ranged_fire::pick_route(cc && copy_kind_[side].load()==static_cast<int>(kk),slot1_hand_.load(),w,in_slot1)) {   // v1284 (4.4): the same rule, pure
            case ranged_fire::PickRoute::copy_own: {   // this hand's copy
                const auto entry=ammo_entry(read,cc,ammo);if(!entry) return 0;
                set_own(cc,entry);copy_ammo_[side][kk]=entry;hand_choice_[side][kk]=ammo;pick_[side][kk]={};note_pick(side,kk,0,ammo_index(read,cc,entry));
                if(rig::value<std::uintptr_t>(read,cc+0x98)!=entry && select_ammo(base,cc,ammo)) ++copy_ammo_sets_;
                return in_slot1?native_keep():0; }
            case ranged_fire::PickRoute::same_item_keep:
                hand_choice_[side][kk]=ammo;copy_ammo_[side][kk]=0;++same_item_keeps_;pick_[side][kk]={ammo,clock()};   // v1372: for the copy that comes (track_ammo)
                note_pick(side,kk,1,ammo_index(read,lk->comp,ammo_entry(read,lk->comp,ammo)));return native_keep();
            case ranged_fire::PickRoute::native_own:
                if(const auto entry=ammo_entry(read,lk->comp,ammo)) {seen_ammo_[kk]=entry;set_own(lk->comp,entry);}   // the native's own (the game equips it)
                hand_choice_[side][kk]=ammo;pick_[side][kk]={ammo,clock()};   // v1372: and the hand's copy, when the gun lands as one (chosen over a power: ArmSet::arrive)
                note_pick(side,kk,2,ammo_index(read,lk->comp,ammo_entry(read,lk->comp,ammo)));
                return 0;
            }
            return 0;
        }
        return 0;
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
    // ---- v145764 (DOTO, R3 G-b): the Voltaic Gun's bolt per hand, for the sim's test commands and the status.
    // The gun a hand shows: its own copy's component when it carries one, else the engine's (the same component the wheel's choice reaches).
    struct BoltGun {std::uintptr_t comp{};bool copy{};};
    template<class Read> static BoltGun bolt_gun(std::uintptr_t base,Read& read,unsigned side) {
        if(side>1) return {};
        if(const auto cc=copy_comp_[side];cc && copy_kind_[side].load()==1) return {cc,true};
        Why w{};const auto l=locate(base,read,Shot::crossbow,w);return l?BoltGun{l->comp,false}:BoltGun{};
    }
    // The gun's ammo list (weapon decl m_ammoTypes, the entries the wheel offers): entry <index>, 0 = none.
    template<class Read> static std::uintptr_t ammo_nth(Read& read,std::uintptr_t comp,int index) {
        const auto decl=comp?rig::value<std::uintptr_t>(read,comp+8):std::nullopt;if(!decl || !rig::pointer(*decl)) return 0;
        const auto first=rig::value<std::uintptr_t>(read,*decl+0x188);const auto count=rig::value<std::uint32_t>(read,*decl+0x190);
        if(!first || !*first || !count || index<0 || static_cast<unsigned>(index)>=(*count&0xffffffu) || index>=16) return 0;
        return *first+static_cast<std::uintptr_t>(index)*0x70ull;
    }
    template<class Read> static int ammo_count(Read& read,std::uintptr_t comp) {
        const auto decl=comp?rig::value<std::uintptr_t>(read,comp+8):std::nullopt;if(!decl || !rig::pointer(*decl)) return 0;
        const auto count=rig::value<std::uint32_t>(read,*decl+0x190);return count?static_cast<int>(std::min<std::uint32_t>(*count&0xffffffu,16u)):0;
    }
    // An ammo entry's name: its item decl's name (+8, as arkDeclInventoryItem), JSON-safe (letters, digits, "_./-"); "ammo<index>" when unreadable.
    template<class Read> static std::string ammo_name(Read& read,std::uintptr_t comp,std::uintptr_t entry) {
        std::string out;
        const auto item=entry?rig::value<std::uintptr_t>(read,entry):std::nullopt;
        const auto name=item && rig::pointer(*item)?rig::value<std::uintptr_t>(read,*item+8):std::nullopt;
        std::array<char,96> text{};
        if(name && rig::pointer(*name) && read(*name,text.data(),text.size()-1))
            for(const char c:std::string(text.data())) if(std::isalnum(static_cast<unsigned char>(c)) || c=='_' || c=='.' || c=='/' || c=='-') out+=c;
        if(out.empty()) out="ammo"+std::to_string(ammo_index(read,comp,entry));
        return out;
    }
    // Test (sim): "voltaic bolt left|right <index>": entry <index> of the hand's gun, applied as that hand's wheel choice would (the game thread does it at
    // the next look: track_ammo). Returns the chosen entry's name, "" when the hand shows no gun or the list has no such entry. Any thread.
    template<class Read> static std::string test_bolt(std::uintptr_t base,Read& read,unsigned side,int index) {
        const auto gun=bolt_gun(base,read,side);if(!gun.comp) return {};
        const auto entry=ammo_nth(read,gun.comp,index);if(!entry) return {};
        bolt_request_[side]=index;
        return ammo_name(read,gun.comp,entry);
    }
    // The current bolt per gun {engine's, left copy, right copy}: [{"gun":..,"index":..,"name":..}], and the list the gun offers.
    template<class Read> static std::string bolts_report(std::uintptr_t base,Read& read) {
        std::ostringstream o;o<<'{';
        const char* hands[]={"left","right"};
        for(unsigned side=0;side<2;++side) {
            const auto gun=bolt_gun(base,read,side);
            const auto a=gun.comp?rig::value<std::uintptr_t>(read,gun.comp+0x98):std::nullopt;
            const bool shared=!gun.copy && copy_kind_[side].load()==1;   // a forearm copy firing through the engine gun's component
            const auto own=shared?copy_ammo_[side][Shot::crossbow]:0;const auto shown=own?std::optional<std::uintptr_t>(own):a;
            o<<(side?",":"")<<'"'<<hands[side]<<"\":{\"gun\":\""<<(!gun.comp?"none":gun.copy?"copy":shared?"copy_shared":"native")<<"\",\"index\":"<<(shown?ammo_index(read,gun.comp,*shown):-1)
             <<",\"name\":"<<(shown && *shown && gun.comp?"\""+ammo_name(read,gun.comp,*shown)+"\"":std::string("null"))<<'}';
        }
        o<<",\"offered\":[";
        Why w{};const auto l=locate(base,read,Shot::crossbow,w);
        for(int i=0,n=l?ammo_count(read,l->comp):0;i<n;++i) o<<(i?",":"")<<'"'<<ammo_name(read,l->comp,ammo_nth(read,l->comp,i))<<'"';
        o<<"]}";return o.str();
    }
    // Status "voltaic_bolts": the bolt each gun shows now, by name (null: no such gun). Names are refreshed by track_ammo.
    static std::string voltaic_bolts_json() {
        std::lock_guard<std::mutex> l(bolt_names_m_);
        const auto q=[](const std::string& n) {return n.empty()?std::string("null"):'"'+n+'"';};
        return "{\"native\":"+q(bolt_names_[0])+",\"left_copy\":"+q(bolt_names_[1])+",\"right_copy\":"+q(bolt_names_[2])+'}';
    }
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
    // v1284 (plan 4.4): a save load / level change (the clamp's load reset, with weapon_draw::Binder::forget): the instances' own ammo is keyed by
    // component pointers, which the new world reuses - a reused pointer would have handed a new crossbow another instance's bolt. Consumed on the game
    // thread at the next track_ammo (the map is the game thread's).
    static void forget_owners() noexcept {forget_owners_.store(true,std::memory_order_release);}
    static void reset() {
        {std::lock_guard<std::mutex> l(m_);queue_.clear();pending_[0]=pending_[1]=false;shots_.clear();plans_.clear();}
        tracks_.clear();hands_[0]=hands_[1]=ranged_fire::HandState{};
        for(unsigned i=0;i<2;++i) {refused_at_[i]=0;ready_at_[i]=0;reload_until_[i]=0;last_kind_[i]=-1;}
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
        if constexpr(game_traits::ranged_player_weapon_is_voltaic) {static std::uint64_t probed{};if(verified && !latched_ && now-probed>=1000) {probed=now;try {probe_charge(base,read);} catch(...) {}}}   // v145745
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
        o<<"},\"no_comp\":["<<no_comp_why_[0].load()<<','<<no_comp_why_[1].load()<<','<<no_comp_why_[2].load()<<','<<no_comp_why_[3].load()<<','<<no_comp_why_[4].load()<<','<<no_comp_why_[5].load()<<','<<no_comp_vtable_.load()
         <<"],\"fired\":"<<fired_.load()<<",\"dry_plans\":"<<dry_plans_.load()<<",\"projectiles_created\":"<<created_.load()
         <<",\"launched\":"<<launched_.load()<<",\"reloads\":"<<reloads_.load()
         <<",\"components\":{\"total_at_create\":"<<comps_total_.load()<<",\"started_at_create\":"<<comps_started0_.load()
         <<",\"started_at_100ms\":"<<comps_started100_.load()<<",\"projectile_comp_started_at_100ms\":"<<proj_comp_started100_.load()
         <<"},\"moved_100ms\":"<<moved_.load()<<",\"stationary_100ms\":"<<stationary_.load()<<",\"ended_early\":"<<ended_early_.load()
         <<",\"alive_4s\":"<<alive_4s_.load()<<",\"entity_finds\":"<<entity_finds_.load()<<",\"unstarted_shots\":"<<unstarted_shots_.load()<<",\"ammo\":{\"pool_before\":"<<pool_before_.load()<<",\"pool_after\":"<<pool_after_.load()
         <<",\"mag_before\":"<<mag_before_.load()<<",\"mag_after\":"<<mag_after_.load()<<",\"spent\":"<<spent_.load()
         <<",\"mismatch\":"<<ammo_mismatch_.load()<<"},\"v1094\":["<<ammo_swaps_.load()<<','<<ammo_splits_.load()<<','<<spreads_.load()<<','<<bursts_.load()<<','<<own_shots_.load()<<','<<copy_ammo_sets_.load()<<','<<wheel_picks_.load()<<','<<same_item_keeps_.load()<<','<<native_follows_.load()<<"],\"bolts\":["<<bolts_[0].load()<<','<<bolts_[1].load()<<','<<bolts_[2].load()<<"],\"fresh\":["<<fresh_picks_.load()<<','<<fresh_carries_.load()<<"],\"picks\":["
         <<[]{std::string t;const unsigned n=pick_n_.load();for(unsigned i=0;i<8;++i) if(const auto p=picks_[(n+i)&7u].load();p&0x80000000u) {   // v1372: oldest first
             const unsigned idx=p&0xffu;t+=(t.empty()?"[":",[")+std::to_string(p>>16&1u)+','+std::to_string(p>>12&1u)+','+std::to_string(p>>8&3u)+','+(idx==0xffu?std::string("-1"):std::to_string(idx))+']';}
         return t;}()
         <<"],\"gate\":["<<gate_state_.load()<<','<<gated_.load()<<','<<gate_passed_.load()
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
        o<<"]";
        if constexpr(game_traits::ranged_player_weapon_is_voltaic) o<<",\"voltaic\":["<<voltaic_marks_.load()<<','<<voltaic_infos_.load()<<','<<(charge_owned_.load()?1:0)<<','<<charged_creates_.load()<<','<<overcharged_creates_.load()<<']';   // DOTO: Gauss shots marked, special-ammo info set; v145745 charge upgrade owned, charged / overcharged copy creates
        o<<'}';
        return o.str();
    }
    static std::uint64_t refused_count(Why w) {return refused_[static_cast<unsigned>(w)].load();}
    inline static std::atomic<std::uint64_t> requests_{},serviced_{},services_{},failures_{},fired_{},created_{},launched_{},reloads_{},dry_plans_{},
        comps_total_{},comps_started0_{},comps_started100_{},proj_comp_started100_{},moved_{},stationary_{},ended_early_{},alive_4s_{},spent_{},ammo_mismatch_{},entity_finds_{},unstarted_shots_{}; // entity_finds_: shots whose weapon was found through its entity (v762)
    inline static std::atomic<int> pool_before_{-1},pool_after_{-1},mag_before_{-1},mag_after_{-1};
    inline static std::array<std::atomic<std::uint32_t>,6> no_comp_why_{};inline static std::atomic<std::uintptr_t> no_comp_vtable_{};

    // What the sequence needs, read from the game for one shot.
    struct Located {
        std::uintptr_t game{},player{},inventory{},record{},comp{},decl{},entity{},ammo{},ammo_decl{},projectile_def{};
        std::uint32_t entity_id{},player_id{};int capacity{},magazine{},pool{};float speed{1.f};bool mark{},unstarted{};
        std::uintptr_t native_ammo{};ranged_fire::Timing timing{};bool own{};
    };
    inline static constexpr std::uintptr_t game_rva=0x39a5290,entities_offset=0x3130b0,player_vtable=0x1c5e428,game_vtable=0x1c940a8,
        inventory_vtable=0x1b9a878,weapon_entity_vtable=0x1ce1e98,projectile_info_rva=0x39d77e0;
    // DOTO: the Voltaic Gun's entity is a plain idEntity (game_traits::voltaic_entity_vtable); Dishonored 2 never takes this route.
    template<class Read> static bool voltaic_entity(Read& read,std::uintptr_t base,std::uintptr_t entity) {
        if constexpr(game_traits::ranged_player_weapon_is_voltaic && game_traits::voltaic_entity_vtable!=0) return rig::typed(read,base,entity,game_traits::voltaic_entity_vtable);
        else {(void)read;(void)base;(void)entity;return false;}
    }
    struct KindInfo {std::uintptr_t comp_vtable,decl_vtable;};
    // Death of the Outsider (game_traits::ranged_player_weapon_is_voltaic): the crossbow kind is Billie's Voltaic Gun, the pistol kind has no
    // player weapon ({0,0}: locate() answers no_weapon at once).
    static constexpr KindInfo kind_info(Shot::Kind k) noexcept {
        if constexpr(game_traits::ranged_player_weapon_is_voltaic) return k==Shot::crossbow?KindInfo{game_traits::voltaic_comp_vtable,game_traits::voltaic_decl_vtable}:KindInfo{0,0};
        else return k==Shot::crossbow?KindInfo{0x1b9cbf8,0x1b9cab8}:KindInfo{0x1b53f58,0x1b53e18};
    }
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
        if(!info.comp_vtable) return {};   // a kind with no player weapon in this game (why: no_weapon)
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
            if(!entity || !*entity || (!typed(read,base,*entity,weapon_entity_vtable) && !voltaic_entity(read,base,*entity))) continue;
            const auto comps=list(read,*entity+0x268,4096);if(!comps) continue;
            for(std::uint32_t c=0;c<comps->packed_count && c<64;++c) {
                const auto comp=value<std::uintptr_t>(read,comps->data+c*8ull);
                if(!comp || !*comp || !typed(read,base,*comp,info.comp_vtable)) continue;
                candidate=true;++entity_finds_;l.record=items->data+i*0x38ull;l.comp=*comp;l.decl=value<std::uintptr_t>(read,*comp+8).value_or(0);l.entity_id=rec->id;break;
            }
        }
        if(!candidate) return {};
        why=Why::no_component;
        // v145741 (DOTO lab8: no_component 8 of 8): which identity check refused [decl, id, entity, entity type, entity id, owner]
        if(value<std::uintptr_t>(read,l.comp+8)!=l.decl) {++no_comp_why_[0];return {};}
        if(!valid_id(l.entity_id)) {++no_comp_why_[1];return {};}
        const auto entity=value<std::uintptr_t>(read,l.game+entities_offset+(l.entity_id>>16)*8ull);
        if(!entity || !*entity) {++no_comp_why_[2];return {};}
        if(!typed(read,base,*entity,weapon_entity_vtable) && !voltaic_entity(read,base,*entity)) {++no_comp_why_[3];no_comp_vtable_=value<std::uintptr_t>(read,*entity).value_or(0)-base;return {};}
        if(value<std::uint32_t>(read,*entity+0xa8)!=l.entity_id) {++no_comp_why_[4];return {};}
        if(value<std::uintptr_t>(read,l.comp+0x10)!=entity) {++no_comp_why_[5];return {};}
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
            if constexpr(game_traits::ranged_player_weapon_is_voltaic) {   // the Gauss fire: speed is the constant 1; its ammo attribute marks the projectile
                const auto flag=attribute(game_traits::voltaic_mark_attribute);l.mark=flag && *flag>0;
            } else {
            if(const auto speed=attribute(0x158);speed && std::isfinite(*speed) && *speed>0) l.speed=*speed; // native passes 0 with no attribute; 1 is the plain bolt
            const auto flag=attribute(0x15c);l.mark=flag && *flag>0;
            }
        }
        why=Why::ok;return l;
    }
    template<class Read> static int pool_count(Read& read,std::uintptr_t inventory,std::uintptr_t ammo_decl) {
        const auto items=rig::list(read,inventory+0x38,4096);if(!items) return 0;
        for(std::uint32_t i=0;i<items->packed_count;++i) {
            const auto rec=rig::value<Rec>(read,items->data+i*0x38ull);
            if(rec && rec->decl==ammo_decl) return infinite_ammo(read,inventory)>0?std::max(rec->count,99):std::max(rec->count,0);
        }
        return 0;
    }
    // Test (sim, 2026-11-25): the inventory's own infinite-ammo field (the remove-item 0x614750 / DOTO 0x66a0a0 skips the removal when [inventory+0x168] > 0;
    // the function is byte-identical in both games). Reads 0 when unreadable.
    template<class Read> static int infinite_ammo(Read& read,std::uintptr_t inventory) {return rig::value<std::int32_t>(read,inventory+0x168).value_or(0);}
    static void write_infinite(std::uintptr_t inventory,int want) noexcept {
        __try {auto* field=reinterpret_cast<volatile std::int32_t*>(inventory+0x168);infinite_old_=*field;*field=want;infinite_new_=*field;infinite_done_=true;}
        __except(EXCEPTION_EXECUTE_HANDLER) {}
    }
    // "d2vr ammo infinite on|off": the game thread sets or clears the field at its next look; the reply carries the old and new value. Any thread.
    static std::string test_infinite(bool on) {
        infinite_done_=false;infinite_request_=on?1:0;
        for(int i=0;i<40 && !infinite_done_.load();++i) Sleep(50);
        if(!infinite_done_.load()) return "ammo infinite: no answer (no player inventory yet?)";
        return std::string("ammo infinite ")+(on?"on":"off")+": inventory+0x168 "+std::to_string(infinite_old_.load())+" -> "+std::to_string(infinite_new_.load());
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
    inline static std::array<std::array<std::uintptr_t,2>,2> hand_choice_{};   // v1278: [hand][pistol/crossbow] ammo item that hand last chose on the wheel; 0 = follow the native
    inline static std::atomic<int> slot1_hand_{-1};inline static std::atomic<std::uint64_t> same_item_keeps_{};
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
    inline static std::array<unsigned,2> burst_left_{};inline static std::array<std::uint64_t,2> burst_next_{},reload_len_{};static std::array<Shot,2> burst_shot_;
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
    // select_ammo_hook: the check's stand-in for the native call (null in the game).
    inline static bool (*select_ammo_hook)(std::uintptr_t comp,std::uintptr_t ammo_item)=nullptr;
    static bool select_ammo(std::uintptr_t base,std::uintptr_t comp,std::uintptr_t ammo_item) {
        if(select_ammo_hook) {(void)base;return select_ammo_hook(comp,ammo_item);}
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
    template<class Read> static int ammo_index(Read& read,std::uintptr_t comp,std::uintptr_t entry) {   // v1281 status: m_ammoTypes index, -1 none
        const auto decl=comp?rig::value<std::uintptr_t>(read,comp+8):std::nullopt;const auto first=decl && *decl?rig::value<std::uintptr_t>(read,*decl+0x188):std::nullopt;
        return first && *first && entry>=*first && (entry-*first)%0x70==0 && (entry-*first)/0x70<16?static_cast<int>((entry-*first)/0x70):-1;
    }
    // Game thread: the test command's choice (test_bolt) as the hand's wheel choice: a copy's through wheel_pick (its own component), the engine's gun
    // through the game's own switch (what the game does for a choice that is no copy's).
    template<class Read> static void apply_bolt(std::uintptr_t base,Read& read,unsigned side,int index) {
        const auto gun=bolt_gun(base,read,side);if(!gun.comp) return;
        const auto entry=ammo_nth(read,gun.comp,index);const auto item=entry?rig::value<std::uintptr_t>(read,entry):std::nullopt;if(!item || !*item) return;
        Why w{};const auto l=locate(base,read,Shot::crossbow,w);
        // Sim 2026-11-24 (v145765 render check): the forearm copy has no started component of its own (copy_comp_ null), its shots borrow the
        // engine gun's component with the hand's copy_ammo_; the pick goes through wheel_pick for it too, never to the engine gun.
        if(gun.copy || copy_kind_[side].load()==1) {
            const auto record_item=l?rig::value<std::uintptr_t>(read,l->record):std::nullopt;if(!record_item || !*record_item) return;
            wheel_hand_=static_cast<int>(side);wheel_pick(base,read,*record_item,*item);return;
        }
        if(!l) return;
        const auto have=rig::value<std::uintptr_t>(read,l->comp+0x98);
        if(have && *have!=entry && select_ammo(base,l->comp,*item)) ++copy_ammo_sets_;
        seen_ammo_[Shot::crossbow]=entry;set_own(l->comp,entry);
    }
    template<class Read> static void track_ammo(std::uintptr_t base,Read& read) {
        const auto now=clock();if(now-ammo_looked_<100) return;
        ammo_looked_=now;
        if(forget_owners_.exchange(false,std::memory_order_acq_rel)) {own_={};seen_comp_={};shown_={};native_seen_={};pick_={};}   // the native's own is re-read from the loaded game
        {Why wb{};const auto lb=locate(base,read,Shot::crossbow,wb);   // v1281 status bolts: [native, left copy, right copy] crossbow ammo index (-1 none)
         bolts_[0]=lb?ammo_index(read,lb->comp,lb->ammo):-1;
         for(unsigned side=0;side<2;++side) {const auto cc=copy_kind_[side].load()==1?copy_comp_[side]:0;const auto a=cc?rig::value<std::uintptr_t>(read,cc+0x98):std::nullopt;bolts_[1+side]=a?ammo_index(read,cc,*a):-1;
             if(!cc && copy_kind_[side].load()==1 && lb) {const auto own=copy_ammo_[side][Shot::crossbow];bolts_[1+side]=ammo_index(read,lb->comp,own?own:lb->ammo);}}   // a copy sharing the engine gun's component: its own pick
         if constexpr(game_traits::ranged_player_weapon_is_voltaic) {   // v145764 status voltaic_bolts: the bolt each gun shows, by name
            std::array<std::string,3> names;
            if(lb) if(const auto a=rig::value<std::uintptr_t>(read,lb->comp+0x98);a && *a) names[0]=ammo_name(read,lb->comp,*a);
            for(unsigned side=0;side<2;++side) {const auto cc=copy_kind_[side].load()==1?copy_comp_[side]:0;const auto a=cc?rig::value<std::uintptr_t>(read,cc+0x98):std::nullopt;if(a && *a) names[1+side]=ammo_name(read,cc,*a);
                else if(!cc && copy_kind_[side].load()==1 && lb) {const auto own=copy_ammo_[side][Shot::crossbow];const auto e=own?own:lb->ammo;if(e) names[1+side]=ammo_name(read,lb->comp,e);}}
            std::lock_guard<std::mutex> g(bolt_names_m_);bolt_names_=std::move(names);
         }}
        if constexpr(game_traits::ranged_player_weapon_is_voltaic) for(unsigned side=0;side<2;++side) if(const int bi=bolt_request_[side].exchange(-1);bi>=0) apply_bolt(base,read,side,bi);
        for(unsigned k=0;k<2;++k) {   // v1279: the native instance keeps its own ammo (wheel_pick sets it); only an empty own type lets the game switch
            Why why{};const auto l=locate(base,read,static_cast<Shot::Kind>(k),why);if(!l) continue;
            if(infinite_request_.load()>=0) if(const int want=infinite_request_.exchange(-1);want>=0) {   // test: the inventory's infinite-ammo field
                write_infinite(l->inventory,want);
            }
            if(const int sh=slot1_hand_.load();(sh==0 || sh==1) && rig::value<std::uintptr_t>(read,l->inventory+0x70)==l->record)
                native_seen_[static_cast<unsigned>(sh)]={static_cast<int>(k),l->ammo,now};   // v1372: the native this hand draws, with its bolt now
            if(seen_comp_[k]!=l->comp) {
                seen_comp_[k]=l->comp;const auto m=own_of(l->comp);
                seen_ammo_[k]=m && entry_valid(read,l->comp,m)?m:l->ammo;set_own(l->comp,seen_ammo_[k]);continue;
            }
            const auto cur=l->ammo,own=seen_ammo_[k];if(cur==own) continue;
            if constexpr(game_traits::ranged_player_weapon_is_voltaic) {seen_ammo_[k]=cur;set_own(l->comp,cur);++native_follows_;continue;}   // DOTO: the native always follows the game (wheel_pick)
            const auto own_decl=entry_valid(read,l->comp,own)?rig::value<std::uintptr_t>(read,own):std::nullopt;
            const bool own_known=own_decl && *own_decl;
            if(ranged_fire::native_ammo(false,own_known,own_known?pool_count(read,l->inventory,*own_decl):0)==ranged_fire::NativeAmmo::follow) {seen_ammo_[k]=cur;set_own(l->comp,cur);++native_follows_;continue;}   // ran out: the game's switch stands
            if(now-native_try_[k]<1000) continue;native_try_[k]=now;   // v1281: a refused switch is retried once a second, not every 100 ms (each try unloads and reloads)
            if(select_ammo(base,l->comp,*own_decl)) ++ammo_splits_;
        }
        for(unsigned side=0;side<2;++side) if(const int t=test_ammo_[side].exchange(-1);t>=0) { // the test command
            const int k=copy_kind_[side].load();const auto cc=copy_comp_[side];if(k<0 || k>1 || !cc) continue;
            const auto decl=rig::value<std::uintptr_t>(read,cc+8);const auto first=decl?rig::value<std::uintptr_t>(read,*decl+0x188):std::nullopt;
            if(first && *first) {copy_ammo_[side][k]=*first+static_cast<std::uintptr_t>(t)*0x70;set_own(cc,copy_ammo_[side][k]);if(const auto it=rig::value<std::uintptr_t>(read,copy_ammo_[side][k]);it && *it) hand_choice_[side][k]=*it;}   // as a wheel choice of this hand
        }
        for(unsigned side=0;side<2;++side) { // v1096: a real copy selects its own ammo itself (0x63a490 on its component: the bolt and magazine follow)
            const int k=copy_kind_[side].load();const auto cc=copy_comp_[side];
            const bool new_here=shown_[side]!=cc;shown_[side]=k<0 || k>1?0:cc;   // v1372
            if(k<0 || k>1 || !cc) continue;
            if(new_here) {   // v1372: a copy new to this hand starts from the choice it came for, or its native's bolt (ranged_fire::fresh_copy_take)
                auto& p=pick_[side][static_cast<unsigned>(k)];const auto& ns=native_seen_[side];
                const auto native_item=ns.kind==k && ns.entry?rig::value<std::uintptr_t>(read,ns.entry).value_or(0):0;
                const auto pick_entry=p.ammo?ammo_entry(read,cc,p.ammo):0,native_entry=native_item?ammo_entry(read,cc,native_item):0;
                switch(ranged_fire::fresh_copy_take(true,pick_entry!=0,now-p.at,native_entry!=0,now-ns.at)) {
                case ranged_fire::FreshTake::pick: set_own(cc,pick_entry);p={};++fresh_picks_;break;
                case ranged_fire::FreshTake::native: set_own(cc,native_entry);++fresh_carries_;break;
                case ranged_fire::FreshTake::none: break;
                }
            }
            // v1279: the instance's own (kept through tosses, catches and hand changes); a copy never seen starts from this hand's last
            // choice for the weapon, else from the native's
            const auto own=own_of(cc);const bool own_ok=own && entry_valid(read,cc,own);
            const std::uintptr_t chosen=!own_ok && hand_choice_[side][k]?ammo_entry(read,cc,hand_choice_[side][k]):0;
            auto want=ranged_fire::copy_ammo_want<std::uintptr_t>(own,own_ok,chosen,seen_ammo_[k]);   // v1284 (4.4): the same rule, pure
            if(!want || !entry_valid(read,cc,want)) continue;
            set_own(cc,want);copy_ammo_[side][k]=want;
            const auto have=rig::value<std::uintptr_t>(read,cc+0x98);if(!have || *have==want) continue;
            if(copy_try_[side].comp==cc && copy_try_[side].entry==want && now-copy_try_[side].at<1000) continue;   // v1281: once a second per refused choice
            copy_try_[side]={cc,want,now};
            const auto item=rig::value<std::uintptr_t>(read,want);
            if(item && *item && select_ammo(base,cc,*item)) ++copy_ammo_sets_;
        }
    }
    struct Try {std::uintptr_t comp{},entry{};std::uint64_t at{};};
    static std::array<Try,2> copy_try_;inline static std::array<std::uint64_t,2> native_try_{};
    // v1279: each weapon instance's own ammo entry, by component (game thread). ponytail: 16 instances, least recently used replaced.
    struct OwnAmmo {std::uintptr_t comp{},entry{};std::uint64_t used{};};
    static std::array<OwnAmmo,16> own_;inline static std::uint64_t own_tick_{};
    inline static std::atomic<std::uint64_t> native_follows_{};inline static std::array<std::atomic<int>,3> bolts_{{-1,-1,-1}};
    inline static std::atomic<int> infinite_request_{-1},infinite_old_{0},infinite_new_{0};inline static std::atomic<bool> infinite_done_{false};
    inline static std::array<std::atomic<int>,2> bolt_request_{{-1,-1}};inline static std::mutex bolt_names_m_;inline static std::array<std::string,3> bolt_names_;   // v145764
    inline static std::atomic<bool> forget_owners_{};   // v1284
    // v1372: per hand (game thread) the copy it showed at the last look, the native it last drew (weapon, that weapon's ammo entry, when), and
    // its last wheel choice per weapon (ammo item, when) still waiting for the copy it was made for (track_ammo, ranged_fire::fresh_copy_take).
    struct NativeSeen {int kind{-1};std::uintptr_t entry{};std::uint64_t at{};};
    struct Pick {std::uintptr_t ammo{};std::uint64_t at{};};
    inline static std::array<std::uintptr_t,2> shown_{};static std::array<NativeSeen,2> native_seen_;static std::array<std::array<Pick,2>,2> pick_;
    inline static std::atomic<std::uint64_t> fresh_picks_{},fresh_carries_{};
    // v1372 status "picks": the last 8 wheel bolt choices as [hand, weapon (0 pistol, 1 crossbow), route (0 its copy, 1 the copy to come,
    // 2 the native), ammo index] - the v1381 log could not show which bolt a choice was or where it went.
    inline static std::array<std::atomic<std::uint32_t>,8> picks_{};inline static std::atomic<unsigned> pick_n_{};
    static void note_pick(unsigned hand,unsigned kind,unsigned route,int index) noexcept {
        picks_[pick_n_.fetch_add(1)&7u].store(0x80000000u|((hand&1u)<<16)|((kind&1u)<<12)|((route&3u)<<8)|(static_cast<unsigned>(index)&0xffu));
    }
    static std::uintptr_t own_of(std::uintptr_t comp) noexcept {for(auto& o:own_) if(comp && o.comp==comp) {o.used=++own_tick_;return o.entry;}return 0;}
    static void set_own(std::uintptr_t comp,std::uintptr_t entry) noexcept {
        if(!comp || !entry) return;
        OwnAmmo* slot=&own_[0];
        for(auto& o:own_) {if(o.comp==comp) {slot=&o;break;}if(o.used<slot->used) slot=&o;}
        *slot=OwnAmmo{comp,entry,++own_tick_};
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
        constexpr bool voltaic=game_traits::ranged_player_weapon_is_voltaic;   // DOTO: the Gauss fire always creates its projectile, has no spread and no muzzle flash
        const bool no_bullet=!voltaic && (flags&0x20)!=0;
        if(s.kind==Shot::pistol && rig::value<std::uint8_t>(read,l.ammo+0x4a).value_or(0)) spread(base,read,l,origin,axis,flags); // 44e0c0's spread-area explosion (the shotgun's damage)
        // The Gauss fire sets the create/sound configure byte from its own source byte before every create (combat-port.md section 2); held for
        // this shot's create and sound, then put back.
        std::conditional_t<voltaic,VoltaicConfigure,NoConfigure> configure{};(void)configure;
        if constexpr(voltaic) {   // put back when this shot returns; v145745: this shot's own charge (VoltaicConfigure)
            const bool charged=s.charged && ((flags&game_traits::voltaic_charge_bit)!=0);configure.begin(l.comp,charged,charged && s.overcharged);
            if(charged) {++charged_creates_;if(s.overcharged) ++overcharged_creates_;}
        }
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
        if constexpr(voltaic) voltaic_before_launch(base,read,n,l,projectile,t);   // the Gauss fire's order: its projectile info is set before the launch
        {LaunchScope scope;ok=n.launch(base,l.comp,projectile,origin,axis,l.speed);}
        if(!ok) return fault();
        ++launched_;
        if constexpr(!voltaic) {
        t.component=n.component(base,projectile,projectile_info_rva);
        if(l.mark && t.component) n.mark(base,t.component);
        }
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
        if(!voltaic && muzzle_flash.load() && n.flash && !n.flash(base,l.comp,gun,origin,axis)) {fault();clean=false;}
        const int pool_after=pool_count(read,l.inventory,l.ammo_decl);
        pool_after_=pool_after;mag_after_=rig::value<std::int32_t>(read,l.comp+0xa0).value_or(-1);
        if(spent) {++spent_;if(pool_after!=l.pool-1 && infinite_ammo(read,l.inventory)<=0) ++ammo_mismatch_;}
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
    // ---- Death of the Outsider: the Voltaic Gun's own steps of its fire (game_traits::ranged_player_weapon_is_voltaic; combat-port.md section 2) ----
    // restore<0: the configure byte takes its source byte's value and the old value is returned (-1 = fault); else `restore` is written back.
    static int voltaic_configure(std::uintptr_t comp,int restore) noexcept {
        __try {
            auto* b=reinterpret_cast<volatile std::uint8_t*>(comp+game_traits::voltaic_configure);
            if(restore<0) {const int old=*b;*b=*reinterpret_cast<const volatile std::uint8_t*>(comp+game_traits::voltaic_configure_from);return old;}
            *b=static_cast<std::uint8_t>(restore);return restore;
        } __except(EXCEPTION_EXECUTE_HANDLER) {return -1;}
    }
    // v145745: one byte of the Gauss component, SEH-guarded: v<0 reads it, else writes v; returns the value before (-1: fault).
    static int voltaic_byte(std::uintptr_t a,int v) noexcept {
        __try {auto* b=reinterpret_cast<volatile std::uint8_t*>(a);const int old=*b;if(v>=0) *b=static_cast<std::uint8_t>(v);return old;} __except(EXCEPTION_EXECUTE_HANDLER) {return -1;}
    }
    // v145745 (static RE of 0x3c1d70 / release 0x8d0bc0, 2026-11-22): the Gauss fire's charge state is comp+0xe0 (charged: held >= 2 s) and
    // comp+0xe1 (overcharged: >= 4 s), written by the native release and never cleared; the fire copies e0 into the create/sound configure byte
    // (+0xb0), and create applies the charge upgrade's modifiers (each ammo's *_charged.cpntprojectile, its fx) only when that byte is set. A copy
    // shot therefore sets e0/e1 to ITS charge (before: it inherited the native's last release, so a copy after a native charged shot was charged
    // too), the configure byte from it, and puts all three back after.
    struct VoltaicConfigure {std::uintptr_t comp{};int old{-1},old_e0{-1},old_e1{-1};
        void begin(std::uintptr_t c,bool charged,bool overcharged) noexcept {
            comp=c;old_e0=voltaic_byte(c+game_traits::voltaic_configure_from,charged?1:0);old_e1=voltaic_byte(c+game_traits::voltaic_shot_overcharged,overcharged?1:0);
            old=voltaic_configure(c,-1);
        }
        ~VoltaicConfigure() {if(!comp) return;if(old>=0) voltaic_configure(comp,old);if(old_e0>=0) voltaic_byte(comp+game_traits::voltaic_configure_from,old_e0);if(old_e1>=0) voltaic_byte(comp+game_traits::voltaic_shot_overcharged,old_e1);}
    };
    struct NoConfigure {void begin(std::uintptr_t,bool,bool) noexcept {}}; // clang-cl checks the discarded if constexpr branch (voltaic is not dependent)
    static bool voltaic_info(std::uintptr_t projectile_component,std::uintptr_t value) noexcept {
        __try {*reinterpret_cast<volatile std::uintptr_t*>(projectile_component+game_traits::voltaic_info_field)=value;return true;} __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    // Between the create and the launch, as the Gauss fire: the projectile's info component; its special-ammo value when the shot's projectile
    // def is the weapon decl's special one; the mark when the ammo attribute is set and the projectile entity is the decl's marked kind.
    template<class Read> static void voltaic_before_launch(std::uintptr_t base,Read& read,const Natives& n,const Located& l,std::uintptr_t projectile,Track& t) {
        t.component=n.component(base,projectile,projectile_info_rva);if(!t.component) return;
        const auto decl=rig::value<std::uintptr_t>(read,l.comp+8);if(!decl || !rig::pointer(*decl)) return;
        if(const auto special=rig::value<std::uintptr_t>(read,*decl+game_traits::voltaic_info_def);special && *special && *special==l.projectile_def)
            if(const auto v=rig::value<std::uintptr_t>(read,*decl+game_traits::voltaic_info_value);v && voltaic_info(t.component,*v)) ++voltaic_infos_;
        const auto marked=rig::value<std::uintptr_t>(read,*decl+game_traits::voltaic_mark_decl);
        if(l.mark && marked && *marked && rig::value<std::uintptr_t>(read,projectile+game_traits::voltaic_entity_decl)==*marked && n.mark(base,t.component)) ++voltaic_marks_;
    }
    // v145745: whether the player owns the charge upgrade: the Gauss component's flags byte has the charge upgrade's m_chargedMode (bit 2), ORed
    // in by the modifier rebuild 0x68cf80 from the upgrade's entry for the ammo in use (the player state tests it at 0x4d5180 before it lets a press
    // charge). Read once a second on the game thread.
    template<class Read> static void probe_charge(std::uintptr_t base,Read& read) {
        if constexpr(game_traits::ranged_player_weapon_is_voltaic) {
            Why why{};const auto l=locate(base,read,Shot::crossbow,why);if(!l) {charge_owned_.store(false,std::memory_order_relaxed);return;}
            const auto f=rig::value<std::uint8_t>(read,l->comp+game_traits::voltaic_charge_flags);
            charge_owned_.store(f && (*f&game_traits::voltaic_charge_bit)!=0,std::memory_order_relaxed);
        } else {(void)base;(void)read;}
    }
public:
    inline static std::atomic<std::uint64_t> voltaic_marks_{},voltaic_infos_{},charged_creates_{},overcharged_creates_{};
    static bool voltaic_charge_owned() noexcept {return charge_owned_.load(std::memory_order_relaxed);}
    inline static std::atomic<unsigned> voltaic_native_hands_{1u};   // v145750: bit h = hand h's forearm carries the engine's gun (dxgi_probe, control thread)
private:
    inline static std::atomic<bool> charge_owned_{};
};
inline std::array<NativeRangedFire::Shot,2> NativeRangedFire::burst_shot_{};
inline std::array<NativeRangedFire::Try,2> NativeRangedFire::copy_try_{};
inline std::array<NativeRangedFire::OwnAmmo,16> NativeRangedFire::own_{};
inline std::array<NativeRangedFire::NativeSeen,2> NativeRangedFire::native_seen_{};
inline std::array<std::array<NativeRangedFire::Pick,2>,2> NativeRangedFire::pick_{};
} // namespace d2vr
