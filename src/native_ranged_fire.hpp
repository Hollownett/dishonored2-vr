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
    void fired(std::uint64_t now,bool auto_reload) noexcept {
        loaded=std::max(loaded-1,0);ready_at=now+cooldown_ms;
        if(loaded==0 && auto_reload) {reloading=true;reload_done=ready_at+reload_ms(kind);}
    }
    bool reload(std::uint64_t now,int capacity,int pool) noexcept {
        if(!used || reloading || loaded>=std::min(std::max(capacity,1),std::max(pool,0))) return false;
        reloading=true;reload_done=std::max(now,ready_at)+reload_ms(kind);return true;
    }
};
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
    struct Request {bool reload{};Shot shot{};};
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
        for(unsigned i=0;i<2;++i) {ready_at_[i]=0;reload_until_[i]=0;last_kind_[i]=-1;}
        reset_latch();
    }

    // Game thread, next to NativeWeaponHands::service.
    template<class Read> static void service(std::uintptr_t base,Read& read) {
        static const bool verified=verify(base,read);
        std::vector<Request> work;{std::lock_guard<std::mutex> l(m_);work.swap(queue_);pending_[0]=pending_[1]=false;}
        if(!work.empty()) ++services_;
        if(!verified) {for(const auto& r:work) {(void)r;refuse(Why::unverified);}work.clear();}
        if(work.empty() && tracks_.empty()) return;
        CrashOperation operation("native ranged fire");
        try {service_with(base,read,native_table(),work);} catch(...) {++failures_;}
    }
    template<class Read> static void service_with(std::uintptr_t base,Read& read,const Natives& n,const std::vector<Request>& work) {
        const auto now=clock();
        for(const auto& r:work) {
            ++serviced_;
            const Why why=latched_?Why::latched:handle(base,read,n,r,now);
            if(why!=Why::ok) refuse(why);
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
         <<",\"mismatch\":"<<ammo_mismatch_.load()<<"},\"hands\":[";
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
        static const std::array<Pin,7> pins{{
            {0x63b5f0,{0x48,0x89,0x4c,0x24,0x08,0x53,0x56,0x41,0x55,0x48,0x81,0xec,0xb0,0x00,0x00,0x00}},
            {0x63b3c0,{0x40,0x53,0x57,0x41,0x56,0x48,0x81,0xec,0xa0,0x00,0x00,0x00,0x48,0x8b,0x81,0x98}},
            {0x877720,{0x48,0x89,0x5c,0x24,0x10,0x44,0x89,0x44,0x24,0x18,0x55,0x56,0x57,0x41,0x54,0x41}},
            {0x614f30,{0x4c,0x8b,0xdc,0x48,0x83,0xec,0x68,0x49,0xc7,0x43,0xb8,0xfe,0xff,0xff,0xff,0x48}},
            {0x614750,{0x4c,0x8b,0xdc,0x57,0x48,0x83,0xec,0x60,0x49,0xc7,0x43,0xb8,0xfe,0xff,0xff,0xff}},
            {0x615680,{0x83,0xfa,0xff,0x7e,0x1a,0x8b,0x41,0x40,0x25,0xff,0xff,0xff,0x00,0x3b,0xd0,0x7d}},
            {0x44ee80,{0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xec,0x40,0x48,0x8b,0x71,0x08,0x48,0x8b}}}}; // 44ee80 read from Dishonored2.exe
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
    inline static std::atomic<std::uint64_t> ready_at_[2]{},reload_until_[2]{};inline static std::atomic<int> last_kind_[2]{{-1},{-1}};
    inline static std::atomic<bool> latched_{false};inline static std::atomic<unsigned> consecutive_faults_{0};
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
        const auto l=locate(base,read,s.kind,why);if(!l) return why;
        auto& hand=hands_[s.side];
        const auto kind=static_cast<ranged_fire::Kind>(s.kind);
        const bool shared=shared_native_magazine.load();
        const bool finished=hand.sync(now,kind,l->capacity,l->pool);
        if(shared) hand.loaded=std::min(l->magazine,l->capacity); // the component's own magazine is the truth in that mode
        if(finished && shared && !dry_run.load()) { // the timer replaces the native reload scene: refill the component's magazine (63a2b0, with the HUD update)
            if(n.native_reload(base,l->comp)) {++reloads_;hand.loaded=std::min(l->pool,l->capacity);}
        }
        last_kind_[s.side]=static_cast<int>(s.kind);
        if(r.reload) {
            if(dry_run.load()) {++dry_plans_;keep(plans_,"reload side="+std::to_string(s.side)+" cap="+std::to_string(l->capacity)+" pool="+std::to_string(l->pool));return Why::ok;}
            if(!hand.reload(now,l->capacity,l->pool)) return Why::full;
            ++reloads_;publish(s.side,hand);return Why::ok;
        }
        const Why check=hand.check(now,l->pool);
        publish(s.side,hand);
        if(check!=Why::ok) return check;
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
        std::uintptr_t projectile{};bool ok;
        {LaunchScope scope;ok=n.create(base,l.comp,projectile);}
        if(!ok) return fault();
        if(!projectile) return Why::spawn_failed; // nothing was spent
        ++created_;
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
        const bool spent=shared_native_magazine.load()?n.native_spend(base,l.comp):n.spend(base,l.inventory,l.ammo_decl);
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
        hand.fired(now,auto_reload.load());publish(s.side,hand);
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
