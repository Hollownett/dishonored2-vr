#pragma once
// Dual wield, agent E: the weapons the game drops from NPCs, found around a hand (artifacts/claude/dual-wield/fleet/E-guard-weapons.md,
// RE in artifacts/claude/dual-wield/D-npc-weapons.md). Static ABI, Steam 17576867:
// - an NPC's weapon is its own idEntity (vtable 0x1c87d58, def under models/weapons/, an arkCpntWeaponMeleeNPC 0x1b9cf48 or an
//   arkCpntWeaponGunNPC 0x1b54278 component), bound to the NPC while it is equipped. ARK_MSG_DROP_EQUIPMENT 0xb6 (dead, knocked out,
//   limb severed, kill scenes) runs 0x615520: each weapon leaves the NPC's equipped list (inventory +0x58, record +8 = its spawn id) and
//   goes to the DEAD context (entity +0x2e0 = 2, set through 0x87ce50 -> 0x875fc0), which starts its rigidbody_npc_sword_dropped body.
// - So a DROPPED weapon is: idEntity, context 2, a model, a weapon component, and its id is in NO NPC's equipped list. A carried
//   weapon of a dead NPC also reads context 2 (0x619dc0 copies the owner's context), which is why the equipped lists decide.
//   Mech swords have no model (the limb is the weapon) and are skipped by the model test.
// - take hides the native entity (idEntity Hide, vtable +0x80 = 0x8784b0; never removed, so a save made while it is held still has it).
//   release moves it to the given pose (Teleport 0x87d1e0), shows it (+0x88 = 0x87c590) and posts the velocity (event 0x149, handler
//   0x727100). All engine calls happen in service() on the game thread; the control thread only queues.
// Threads: service/stop/sword_identity on the game thread (NativeInteractionAim stage-0 scope); nearest/take/release/held/take_lost/
// status_json from any thread.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "game_rig.hpp"
#include "native_melee.hpp"
#include "tracked_pose.hpp"
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
#include <string>
#include <tuple>
#include <type_traits>
#include <vector>

namespace d2vr {
namespace npc_weapon {
enum class Kind : unsigned char {sword,pistol,other};
inline const char* kind_name(Kind k) noexcept {return k==Kind::sword?"sword":k==Kind::pistol?"pistol":"other";}
inline std::string lowered(std::string s) {for(auto& c:s) c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));return s;}
// A dropped weapon's kind from its def and components. Only defs under models/weapons/ count. A gun component wins, then a melee
// component (swords and the docker tools); the thrown item (rock, bottle) has neither and is told by name. Anything else is no pickup.
inline std::optional<Kind> classify(const std::string& def,bool melee_npc,bool gun_npc) {
    const auto l=lowered(def);
    if(l.rfind("models/weapons/",0)!=0) return {};
    if(gun_npc) return Kind::pistol;
    if(melee_npc) return Kind::sword;
    if(l.find("item_throw")!=std::string::npos || l.find("thrown_item")!=std::string::npos) return Kind::other;
    return {};
}
// The model's world AABB (model +0x1840: min xyz, pad, max xyz, pad).
struct Box {
    Vec3 lo{},hi{};
    Vec3 closest(Vec3 p) const {return {std::clamp(p.x,lo.x,hi.x),std::clamp(p.y,lo.y,hi.y),std::clamp(p.z,lo.z,hi.z)};}
};
// Finite, ordered, a diagonal of 1 mm to 3 m, and the entity origin within 0.5 m of it (native_rigid_body's accept_bounds, same limits).
inline std::optional<Box> sane_box(const std::array<float,8>& v,Vec3 origin) {
    const Box b{{v[0],v[1],v[2]},{v[4],v[5],v[6]}};
    if(!finite(b.lo) || !finite(b.hi) || !finite(origin) || b.lo.x>b.hi.x || b.lo.y>b.hi.y || b.lo.z>b.hi.z) return {};
    const double diagonal=length(b.hi-b.lo);
    if(!(diagonal>.001) || !(diagonal<=3) || !(length(b.closest(origin)-origin)<=.5)) return {};
    return b;
}
// What one scan saw.
struct Found {
    std::uintptr_t entity{};std::uint32_t id{};Kind kind{Kind::other};std::string def;WorldTrackedPose pose{};std::optional<Box> bounds;bool hidden{};
};
// What a query returns. distance: hand to the model box (or the origin when there is no box), metres.
struct Hit {
    std::uintptr_t entity{};std::uint32_t id{};Kind kind{Kind::other};std::string def;WorldTrackedPose pose{};double distance{};
};
struct Entry {
    std::uintptr_t entity{};std::uint32_t id{};Kind kind{Kind::other};std::string def;WorldTrackedPose pose{};std::optional<Box> bounds;
    int holder{-1};          // -1 lying in the world, else the hand (0 left, 1 right) that holds it
    bool hidden_by_us{};     // the native entity is hidden because of a take (shown again on release or teardown)
    bool seen{};unsigned miss{};std::uint64_t settle_until{};
};
// Pure book of the dropped weapons (no engine access): list upkeep, nearest, and the take/release state machine. One entry per spawn id,
// at most `capacity`. A hand holds at most one. The engine side-effects it owes are queued as Ops for the game thread.
class Book {
public:
    static constexpr std::size_t capacity=16;
    static constexpr unsigned forget_after_misses=3;
    static constexpr std::uint64_t settle_ms=1500;     // a released weapon is not forgotten for this long (it is hidden until its show runs)
    struct Op {
        bool show{};std::uintptr_t entity{};std::uint32_t id{};
        bool place{};Vec3 pos{},vel{};std::array<float,9> axis{};bool have_axis{};  // show only
    };
    struct Refresh {std::uint32_t id{};bool alive{};WorldTrackedPose pose{};std::optional<Box> bounds;bool have_pose{};};
    std::size_t size() const {return entries_.size();}
    const std::vector<Entry>& entries() const {return entries_;}
    std::size_t lying() const {return static_cast<std::size_t>(std::count_if(entries_.begin(),entries_.end(),[](const Entry& e){return e.holder<0;}));}
    std::size_t held_count() const {return entries_.size()-lying();}
    const Entry* find(std::uint32_t id) const {for(const auto& e:entries_) if(e.id==id) return &e;return nullptr;}
    // Fold in a scan. Lying entries the scan did not see age out after forget_after_misses scans (unless just released); held ones stay.
    // Over capacity the lying entries farthest from `around` (the player) go. Returns the new entries kept.
    std::size_t merge_scan(const std::vector<Found>& found,Vec3 around,std::uint64_t now) {
        std::vector<std::uint32_t> added_ids;
        for(auto& e:entries_) e.seen=false;
        for(const auto& f:found) {
            if(!f.id) continue;
            if(auto* e=mutable_find(f.id)) {
                e->seen=true;
                if(e->holder>=0) continue;      // hidden by us: a scan does not see it, and if it does, the take stays
                e->entity=f.entity;e->kind=f.kind;e->def=f.def;e->pose=f.pose;e->bounds=f.bounds;e->miss=0;
                continue;
            }
            Entry n{f.entity,f.id,f.kind,f.def,f.pose,f.bounds};n.seen=true;
            entries_.push_back(std::move(n));added_ids.push_back(f.id);
        }
        for(auto& e:entries_) if(e.holder<0 && !e.seen && now>=e.settle_until) ++e.miss;
        entries_.erase(std::remove_if(entries_.begin(),entries_.end(),[](const Entry& e){return e.holder<0 && !e.seen && e.miss>=forget_after_misses;}),entries_.end());
        while(entries_.size()>capacity) {                                       // over capacity: the lying weapon farthest from the player goes
            Entry* farthest=nullptr;double farthest_d=-1;
            for(auto& e:entries_) if(e.holder<0 && now>=e.settle_until) {const double d=length(e.pose.position-around);if(d>farthest_d) {farthest_d=d;farthest=&e;}}
            if(!farthest) break;                                                // only held or just-released ones left: allow the overflow
            entries_.erase(entries_.begin()+(farthest-entries_.data()));
        }
        return static_cast<std::size_t>(std::count_if(added_ids.begin(),added_ids.end(),[&](std::uint32_t id){return find(id)!=nullptr;}));
    }
    // The nearest lying weapon within `reach` of the hand (to its model box when there is one). Empty when the hand already holds one.
    std::optional<Hit> nearest(unsigned side,Vec3 hand,double reach,std::optional<Kind> only={}) const {
        if(!finite(hand) || !(reach>0) || holder_of(side&1u)) return {};
        const Entry* best=nullptr;double best_d=reach;
        for(const auto& e:entries_) {
            if(e.holder>=0 || (only && e.kind!=*only)) continue;
            const double d=distance(e,hand);
            if(d<=best_d) {best_d=d;best=&e;}
        }
        if(!best) return {};
        return Hit{best->entity,best->id,best->kind,best->def,best->pose,best_d};
    }
    // The hand takes a lying weapon: the native entity is to be hidden. False if the hand is full or the weapon is not lying.
    bool take(std::uint32_t id,unsigned side) {
        side&=1u;auto* e=mutable_find(id);
        if(!e || e->holder>=0 || holder_of(side)) return false;
        e->holder=static_cast<int>(side);lost_[side]=false; // a stale loss from an earlier weapon never drops this one
        ops_.push_back({false,e->entity,e->id});
        return true;
    }
    // The hand lets go: it lies in the world again at pos (the native entity is to be moved there, shown and given vel). The released
    // weapon is returned (its def and kind). Empty if the hand held nothing.
    std::optional<Hit> release(unsigned side,Vec3 pos,Vec3 vel,const std::optional<std::array<float,9>>& axis,std::uint64_t now) {
        side&=1u;auto* e=mutable_find_holder(side);
        if(!e || !finite(pos) || !finite(vel)) return {};
        e->holder=-1;e->pose.position=pos;e->miss=0;e->settle_until=now+settle_ms;
        if(axis) for(unsigned i=0;i<3;++i) e->pose.basis[i]={(*axis)[i*3],(*axis)[i*3+1],(*axis)[i*3+2]};
        Op op{true,e->entity,e->id};op.place=true;op.pos=pos;op.vel=vel;
        if(axis) {op.axis=*axis;op.have_axis=true;}
        ops_.push_back(op);
        return Hit{e->entity,e->id,e->kind,e->def,e->pose,0};
    }
    std::optional<Hit> held(unsigned side) const {
        const auto* e=holder_of(side&1u);
        return e?std::optional<Hit>{Hit{e->entity,e->id,e->kind,e->def,e->pose,0}}:std::nullopt;
    }
    std::vector<Op> drain_ops() {std::vector<Op> out;out.swap(ops_);return out;}
    // Game thread, after the hide ran / the show ran / the hide failed.
    void on_hidden(std::uint32_t id) {if(auto* e=mutable_find(id)) e->hidden_by_us=true;}
    void on_shown(std::uint32_t id) {if(auto* e=mutable_find(id)) e->hidden_by_us=false;}
    // The take could not be carried out (or the weapon vanished): the hand is empty again. Returns the side that held it, or -1.
    int drop_holder(std::uint32_t id,bool erase) {
        auto* e=mutable_find(id);if(!e) return -1;
        const int side=e->holder;e->holder=-1;
        if(erase) entries_.erase(entries_.begin()+(e-entries_.data()));
        if(side>=0) lost_[side]=true;
        return side;
    }
    // Apply what the game thread read for the tracked entries (a lying one that is gone is erased, a held one that is gone frees its hand).
    void apply_refresh(const std::vector<Refresh>& r) {
        for(const auto& x:r) {
            auto* e=mutable_find(x.id);if(!e) continue;
            if(!x.alive) {drop_holder(x.id,true);continue;}
            if(e->holder<0 && x.have_pose) {e->pose=x.pose;e->bounds=x.bounds;}
        }
    }
    // A new level or load: nothing carries over. Hands that held something are told (take_lost).
    void reset() {
        for(const auto& e:entries_) if(e.holder>=0) lost_[e.holder]=true;
        entries_.clear();ops_.clear();
    }
    // Teardown: every weapon the book hid is to be shown again. Returned (entity, id); the book is then empty and the hands told.
    std::vector<std::pair<std::uintptr_t,std::uint32_t>> teardown() {
        std::vector<std::pair<std::uintptr_t,std::uint32_t>> out;
        for(const auto& e:entries_) if(e.hidden_by_us) out.push_back({e.entity,e.id});
        // A hide still queued has not run: nothing to undo for it (its entity is simply not hidden).
        reset();
        return out;
    }
    bool take_lost(unsigned side) {const bool v=lost_[side&1u];lost_[side&1u]=false;return v;}
private:
    std::vector<Entry> entries_;std::vector<Op> ops_;std::array<bool,2> lost_{};
    Entry* mutable_find(std::uint32_t id) {for(auto& e:entries_) if(e.id==id) return &e;return nullptr;}
    Entry* mutable_find_holder(unsigned side) {for(auto& e:entries_) if(e.holder==static_cast<int>(side)) return &e;return nullptr;}
    const Entry* holder_of(unsigned side) const {for(const auto& e:entries_) if(e.holder==static_cast<int>(side)) return &e;return nullptr;}
    static double distance(const Entry& e,Vec3 hand) {return length((e.bounds?e.bounds->closest(hand):e.pose.position)-hand);}
};
} // namespace npc_weapon

class NpcWeaponPickup {
public:
    using Kind=npc_weapon::Kind;
    using Hit=npc_weapon::Hit;
    inline static constexpr std::uintptr_t id_entity_vtable_rva=0x1c87d58,melee_npc_vtable_rva=0x1b9cf48,gun_npc_vtable_rva=0x1b54278,
        teleport_rva=0x87d1e0,hide_rva=0x8784b0,show_rva=0x87c590,context_dead=2;
    inline static constexpr std::uint64_t scan_interval_ms=300;
    inline static constexpr std::uint32_t velocity_event=0x149;
    inline static constexpr const char* player_sword_damage="components/characters/player/base/damage/player_sword.damage";
    // First scan after a (re)start restores weapons that are hidden, dropped and in nobody's hands: ours from a save made while a hand
    // held one (the take hid the native entity; the save keeps the hidden bit). Off: such a sword stays gone until the level reloads.
    inline static std::atomic<bool> orphan_sweep{true};
    inline static std::atomic<std::uint64_t> services{},scans{},scan_candidates{},scan_degraded{},takes{},take_refused{},hides{},hide_failed{},releases{},
        shows{},teleports{},velocity_posts{},lost{},resets{},orphans_restored{},unverified{};
    inline static std::atomic<float> last_release_err_mm{-1.f};

    // ---- any thread -------------------------------------------------------------------------------------------------------------
    // The nearest dropped weapon within `reach` of a hand's world position (game Z up), or nothing. A hand that holds one gets nothing.
    static std::optional<Hit> nearest(unsigned side,Vec3 hand,double reach,std::optional<Kind> only={}) {
        std::lock_guard<std::mutex> l(m_);return book_.nearest(side,hand,reach,only);
    }
    // The hand takes it: the game thread hides the native weapon. false = the hand is full or the weapon is not lying (e.g. just taken).
    static bool take(std::uint32_t id,unsigned side) {
        std::lock_guard<std::mutex> l(m_);const bool ok=book_.take(id,side);
        if(ok) ++takes;else ++take_refused;
        return ok;
    }
    // The hand lets go at pos (game world) with velocity vel (m/s): the native weapon is moved there, shown and thrown. The weapon lies
    // in the world again whether or not the caller draws a copy; to let a copy fly first (A's held_copy), keep the copy flying and call
    // this at its landing pose with a zero velocity (and the landed axis rows forward,left,up when known).
    static std::optional<Hit> release(unsigned side,Vec3 pos,Vec3 vel={},const std::optional<std::array<float,9>>& axis={}) {
        std::lock_guard<std::mutex> l(m_);auto r=book_.release(side,pos,vel,axis,clock_ms());
        if(r) ++releases;
        return r;
    }
    static std::optional<Hit> held(unsigned side) {std::lock_guard<std::mutex> l(m_);return book_.held(side);}
    // True once if the weapon this hand held is gone (the entity vanished, the level changed, the hide failed or teardown ran): drop the copy.
    static bool take_lost(unsigned side) {std::lock_guard<std::mutex> l(m_);return book_.take_lost(side);}
    // Show everything the book hid on the next service (session stop from a non-game thread). stop() is the direct game-thread form.
    static void request_teardown() {teardown_requested_=true;}
    static bool teardown_pending() noexcept {return teardown_requested_.load();}
    static std::size_t tracked() {std::lock_guard<std::mutex> l(m_);return book_.size();}
    static std::string status_json() {
        std::size_t tracked_n{},lying_n{},held_n{};
        {std::lock_guard<std::mutex> l(m_);tracked_n=book_.size();lying_n=book_.lying();held_n=book_.held_count();}
        char b[768];
        std::snprintf(b,sizeof b,"{\"tracked\":%zu,\"lying\":%zu,\"held\":%zu,\"services\":%llu,\"scans\":%llu,\"candidates\":%llu,\"degraded\":%llu,\"takes\":%llu,"
            "\"take_refused\":%llu,\"hides\":%llu,\"hide_failed\":%llu,\"releases\":%llu,\"shows\":%llu,\"teleports\":%llu,\"velocity_posts\":%llu,\"lost\":%llu,"
            "\"resets\":%llu,\"orphans_restored\":%llu,\"unverified\":%llu,\"release_err_mm\":%.1f}",
            tracked_n,lying_n,held_n,u(services),u(scans),u(scan_candidates),u(scan_degraded),u(takes),u(take_refused),u(hides),u(hide_failed),u(releases),
            u(shows),u(teleports),u(velocity_posts),u(lost),u(resets),u(orphans_restored),u(unverified),static_cast<double>(last_release_err_mm.load()));
        return b;
    }

    // ---- game thread ------------------------------------------------------------------------------------------------------------
    // Next to NativeWeaponHands::service, gameplay only. Cheap: the queued engine calls, a pose read per tracked weapon, and a registry scan
    // every scan_interval_ms.
    template<class Read> static void service(std::uintptr_t base,Read& read) {
        CrashOperation operation("npc weapon pickup");
        ++services;
        if(!verified(base,read)) {++unverified;return;}
        const auto game=rig::value<std::uintptr_t>(read,base+NativeMelee::game_rva);
        const auto player=game?rig::value<std::uintptr_t>(read,*game+NativeMelee::entities_offset):std::nullopt;
        const auto player_id=player&&rig::pointer(*player)?rig::value<std::uint32_t>(read,*player+0xa8):std::nullopt;
        if(!game || !rig::pointer(*game) || !player_id) {restart();return;}      // no world: the next level starts fresh
        if(*game!=game_ || *player_id!=player_id_) {restart();game_=*game;player_id_=*player_id;}
        if(teardown_requested_.exchange(false)) {stop(base,read);game_=*game;player_id_=*player_id;}
        run_ops(base,read);
        refresh(base,read);
        const auto now=clock_ms();
        if(now>=next_scan_) {next_scan_=now+scan_interval_ms;scan(base,read,*game,*player);}
    }
    // Session stop / level change: every weapon the book hid is shown where it was, the book is emptied, the hands are told.
    template<class Read> static void stop(std::uintptr_t base,Read& read) {
        std::vector<std::pair<std::uintptr_t,std::uint32_t>> shown;
        {std::lock_guard<std::mutex> l(m_);shown=book_.teardown();}
        for(const auto& [e,id]:shown) if(alive_entity(base,read,e,id)) {if(visible(base,read,e,true)) ++shows;}
        arm_sweep_=orphan_sweep.load();
    }

    // The sword identity a guard sword in the player's hand hits with, so a hit alerts and credits like the player's own (NativeMelee::apply
    // consumes player, entity (inflictor), decl, ids, amount, unaware_amount; weapon 0 = no native hit list, the caller keeps one hit per
    // victim per swing). The player's equipped sword gives the inflictor and its damage decl; without it the decl is looked up by name
    // (player_sword.damage) and the player is the inflictor. unaware_amount > 0: an unaware victim takes that instead (a stealth kill).
    template<class Read> static std::optional<NativeMelee::Sword> sword_identity(std::uintptr_t base,Read& read,float unaware_amount=0) {
        auto s=NativeMelee::sword(base,read);
        if(s) s->weapon=0;
        else {
            const auto game=rig::value<std::uintptr_t>(read,base+NativeMelee::game_rva);if(!game || !rig::pointer(*game)) return {};
            const auto player=rig::value<std::uintptr_t>(read,*game+NativeMelee::entities_offset);if(!player || !rig::pointer(*player)) return {};
            const auto id=rig::value<std::uint32_t>(read,*player+0xa8);if(!id || *id==NativeMelee::invalid_id) return {};
            const auto decl=damage_decl(base,read);if(!decl) return {};
            s=NativeMelee::Sword{*player,*player,0,decl,*id,*id,0,0};
        }
        s->amount=0;s->unaware_amount=unaware_amount;
        return s;
    }

#ifdef D2VR_TESTING
    // tests: replaces the engine calls. what: 0 hide, 1 show, 2 teleport (a = pos[3], b = axis[9]), 3 velocity (a = v[3]).
    inline static bool (*engine_hook)(int what,std::uintptr_t entity,const float* a,const float* b)=nullptr;
    inline static std::uint64_t (*clock_hook)()=nullptr;
    inline static std::uintptr_t (*decl_hook)()=nullptr;
    static void reset_for_test() {
        {std::lock_guard<std::mutex> l(m_);book_=npc_weapon::Book{};}
        game_=player_id_=0;next_scan_=0;arm_sweep_=true;teardown_requested_=false;
    }
#endif

private:
    inline static std::mutex m_;inline static npc_weapon::Book book_;
    inline static std::uintptr_t game_{};inline static std::uint32_t player_id_{};inline static std::uint64_t next_scan_{};
    inline static bool arm_sweep_{true};inline static std::atomic<bool> teardown_requested_{false};
    inline static std::uintptr_t verified_base_{};inline static bool verified_ok_{};
    inline static std::vector<std::uintptr_t> registry_;
    static unsigned long long u(const std::atomic<std::uint64_t>& a) {return static_cast<unsigned long long>(a.load());}
    static std::uint64_t clock_ms() {
#ifdef D2VR_TESTING
        if(clock_hook) return clock_hook();
#endif
        return GetTickCount64();
    }
    static void restart() {
        std::lock_guard<std::mutex> l(m_);
        if(book_.size()) ++resets;
        book_.reset();game_=player_id_=0;next_scan_=0;arm_sweep_=orphan_sweep.load();
    }
    // Pins of what release calls (the same bytes native_weapon_hands.hpp pins for these functions).
    template<class Read> static bool verified(std::uintptr_t base,Read& read) {
#ifdef D2VR_TESTING
        if(engine_hook) return true;
#endif
        if(verified_base_==base) return verified_ok_;
        static const std::array<unsigned char,16> teleport_code{0x48,0x83,0xec,0x38,0xc7,0x44,0x24,0x20,0x00,0x00,0x00,0x00,0xe8,0x0f,0x00,0x00};
        static const std::array<unsigned char,14> manager_code{0x48,0x8b,0x05,0x49,0x50,0x19,0x03,0x48,0x05,0x68,0x89,0x1e,0x00,0xc3};
        static const std::array<unsigned char,14> post_code{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x44,0x89,0x4c,0x24};
        const auto pinned=[&](std::uintptr_t rva,const auto& want) {
            std::array<unsigned char,std::tuple_size<std::decay_t<decltype(want)>>::value> got{};return read(base+rva,got.data(),got.size()) && got==want;};
        const bool ok=pinned(teleport_rva,teleport_code) && pinned(0x810240,manager_code) && pinned(0x8112c0,post_code);
        verified_base_=base;verified_ok_=ok;return ok;
    }
    template<class Read> static bool alive_entity(std::uintptr_t base,Read& read,std::uintptr_t e,std::uint32_t id) {
        if(!e || !id || (id>>16)>=0x3fff) return false;
        const auto game=rig::value<std::uintptr_t>(read,base+NativeMelee::game_rva);
        return game && rig::value<std::uintptr_t>(read,*game+NativeMelee::entities_offset+(id>>16)*8ull)==e && rig::value<std::uint32_t>(read,e+0xa8)==id;
    }
    // idEntity Hide (vtable +0x80) / Show (+0x88) when the entity's slot is the base function or a one-jump thunk to it (as native_weapon_hands.hpp).
    template<class Read> static bool visible(std::uintptr_t base,Read& read,std::uintptr_t e,bool on) {
#ifdef D2VR_TESTING
        if(engine_hook) return engine_hook(on?1:0,e,nullptr,nullptr);
#endif
        const std::uintptr_t want=base+(on?show_rva:hide_rva);
        const auto vt=rig::value<std::uintptr_t>(read,e);const auto fn=vt?rig::value<std::uintptr_t>(read,*vt+(on?0x88:0x80)):std::nullopt;
        if(!fn) return false;
        bool ok=*fn==want;
        if(!ok) {std::array<unsigned char,5> jmp{};
            if(read(*fn,jmp.data(),jmp.size()) && jmp[0]==0xe9) {std::int32_t rel{};std::memcpy(&rel,jmp.data()+1,4);ok=*fn+5+rel==want;}}
        return ok && call_visible(want,e);
    }
    static bool call_visible(std::uintptr_t fn,std::uintptr_t e) {
        __try {reinterpret_cast<void (__fastcall*)(std::uintptr_t)>(fn)(e);return true;} __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    static bool teleport(std::uintptr_t base,std::uintptr_t e,const std::array<float,3>& pos,const std::array<float,9>& axis) {
        const auto ok=[](const auto& v){return std::all_of(v.begin(),v.end(),[](float x){return std::isfinite(x);});};
        if(!ok(pos) || !ok(axis)) return false;                                       // Teleport 87d1e0 has no finite check
#ifdef D2VR_TESTING
        if(engine_hook) return engine_hook(2,e,pos.data(),axis.data());
#endif
        alignas(16) std::array<float,3> o=pos;alignas(16) std::array<float,9> a=axis;
        __try {reinterpret_cast<void (__fastcall*)(std::uintptr_t,const float*,const float*,int)>(base+teleport_rva)(e,o.data(),a.data(),0);return true;}
        __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    // VELOCITY_IMPULSE 0x149 {v[3], w[3], u16 flags}: payload from 8112c0(8112c0's manager = 810240() + 0xc8e8, msg, size, target id).
    static bool post_velocity(std::uintptr_t base,std::uintptr_t e,std::uint32_t id,Vec3 v) {
        if(!finite(v)) return false;
        if(length(v)>15) v=v*(15/length(v));                                          // a hand flick is not a rifle
        const float f[3]{static_cast<float>(v.x),static_cast<float>(v.y),static_cast<float>(v.z)};
#ifdef D2VR_TESTING
        if(engine_hook) return engine_hook(3,e,f,nullptr);
#endif
        (void)e;
        __try {
            const auto manager=reinterpret_cast<std::uintptr_t (__fastcall*)()>(base+0x810240)();
            if(!manager) return false;
            auto* p=static_cast<unsigned char*>(reinterpret_cast<void* (__fastcall*)(std::uintptr_t,unsigned,unsigned,std::uint32_t)>(base+0x8112c0)(manager+0xc8e8,velocity_event,0x1c,id));
            if(!p) return false;
            std::memset(p,0,0x1c);std::memcpy(p,f,12);return true;
        } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    template<class Read> static void run_ops(std::uintptr_t base,Read& read) {
        std::vector<npc_weapon::Book::Op> ops;{std::lock_guard<std::mutex> l(m_);ops=book_.drain_ops();}
        for(const auto& op:ops) {
            const bool alive=alive_entity(base,read,op.entity,op.id);
            if(!op.show) {                                                           // a take
                if(alive && visible(base,read,op.entity,false)) {std::lock_guard<std::mutex> l(m_);book_.on_hidden(op.id);++hides;}
                else {++hide_failed;std::lock_guard<std::mutex> l(m_);if(book_.drop_holder(op.id,!alive)>=0) ++lost;}
                continue;
            }
            if(!alive) {std::lock_guard<std::mutex> l(m_);book_.drop_holder(op.id,true);continue;}
            if(op.place) {
                std::array<float,9> axis=op.axis;
                if(!op.have_axis) {const auto cur=rig::value<std::array<float,9>>(read,op.entity+0x30c);axis=cur?*cur:std::array<float,9>{1,0,0,0,1,0,0,0,1};}
                const std::array<float,3> pos{static_cast<float>(op.pos.x),static_cast<float>(op.pos.y),static_cast<float>(op.pos.z)};
                if(teleport(base,op.entity,pos,axis)) ++teleports;
                if(visible(base,read,op.entity,true)) {++shows;std::lock_guard<std::mutex> l(m_);book_.on_shown(op.id);}
                if(length(op.vel)>0 && post_velocity(base,op.entity,op.id,op.vel)) ++velocity_posts;
                const auto at=rig::value<std::array<float,3>>(read,op.entity+0x300);
                if(at) last_release_err_mm=static_cast<float>(1000*length(Vec3{(*at)[0],(*at)[1],(*at)[2]}-op.pos));
            } else if(visible(base,read,op.entity,true)) {++shows;std::lock_guard<std::mutex> l(m_);book_.on_shown(op.id);}
        }
    }
    // Pose of a tracked entity: origin +0x300, axis rows +0x30c (forward, left, up), model box +0x1840.
    template<class Read> static std::optional<npc_weapon::Book::Refresh> read_pose(std::uintptr_t base,Read& read,std::uintptr_t e,std::uint32_t id) {
        npc_weapon::Book::Refresh r{id};
        r.alive=alive_entity(base,read,e,id);
        if(!r.alive) return r;
        const auto o=rig::value<std::array<float,3>>(read,e+0x300);const auto a=rig::value<std::array<float,9>>(read,e+0x30c);
        if(!o || !a) return r;
        r.pose=held_pose(*o,*a);r.have_pose=finite(r.pose.position);
        if(const auto model=rig::value<std::uintptr_t>(read,e+0xf0); model && rig::pointer(*model))
            if(const auto box=rig::value<std::array<float,8>>(read,*model+0x1840)) r.bounds=npc_weapon::sane_box(*box,r.pose.position);
        return r;
    }
    static WorldTrackedPose held_pose(const std::array<float,3>& o,const std::array<float,9>& a) {
        return {{o[0],o[1],o[2]},{Vec3{a[0],a[1],a[2]},Vec3{a[3],a[4],a[5]},Vec3{a[6],a[7],a[8]}}};
    }
    // Every service: tracked weapons gone from the world are forgotten (a held one frees its hand), lying ones keep a fresh pose.
    template<class Read> static void refresh(std::uintptr_t base,Read& read) {
        std::vector<std::pair<std::uintptr_t,std::uint32_t>> targets;
        {std::lock_guard<std::mutex> l(m_);for(const auto& e:book_.entries()) targets.push_back({e.entity,e.id});}
        if(targets.empty()) return;
        std::vector<npc_weapon::Book::Refresh> out;
        for(const auto& [e,id]:targets) if(const auto r=read_pose(base,read,e,id)) out.push_back(*r);
        std::lock_guard<std::mutex> l(m_);
        for(const auto& r:out) if(!r.alive) {const auto* e=book_.find(r.id);if(e && e->holder>=0) ++lost;}
        book_.apply_refresh(out);
    }
    // One candidate from a registry slot: an idEntity in the DEAD context with a model, a def under models/weapons/ and a weapon component.
    template<class Read> static std::optional<npc_weapon::Found> candidate(std::uintptr_t base,Read& read,std::uintptr_t e,unsigned slot) {
        const auto ctx=rig::value<std::int32_t>(read,e+0x2e0);
        if(!ctx || *ctx!=static_cast<std::int32_t>(context_dead)) return {};
        const auto id=rig::value<std::uint32_t>(read,e+0xa8);
        if(!id || *id==NativeMelee::invalid_id || (*id>>16)!=slot) return {};
        const auto model=rig::value<std::uintptr_t>(read,e+0xf0);
        if(!model || !rig::pointer(*model)) return {};                                  // clockwork swords: the limb is the weapon
        const auto decl=rig::value<std::uintptr_t>(read,e+0x2e8);
        const auto name=decl?rig::value<std::uintptr_t>(read,*decl+0x48):std::nullopt;
        std::array<char,128> text{};
        if(!name || !rig::pointer(*name) || !read(*name,text.data(),text.size()-1)) return {};
        const std::string def(text.data());
        bool melee=false,gun=false;
        if(const auto comps=rig::list(read,e+0x268,256); comps && comps->packed_count) {
            std::vector<std::uintptr_t> data(comps->packed_count);
            if(read(comps->data,data.data(),data.size()*8))
                for(const auto c:data) {
                    if(rig::typed(read,base,c,melee_npc_vtable_rva) && rig::value<std::uintptr_t>(read,c+0x10)==e) melee=true;
                    if(rig::typed(read,base,c,gun_npc_vtable_rva) && rig::value<std::uintptr_t>(read,c+0x10)==e) gun=true;
                }
        }
        const auto kind=npc_weapon::classify(def,melee,gun);
        if(!kind) return {};
        const auto pose=read_pose(base,read,e,*id);
        if(!pose || !pose->alive || !pose->have_pose) return {};
        const auto hidden=rig::value<std::uint8_t>(read,e+0xd0);
        npc_weapon::Found f{e,*id,*kind,def,pose->pose,pose->bounds,hidden && (*hidden&1)};
        return f;
    }
    // The spawn ids in every NPC's equipped list (inventory +0x58, record +8). false if one inventory could not be read.
    template<class Read> static bool equipped_ids(std::uintptr_t base,Read& read,const std::vector<std::uintptr_t>& npcs,std::vector<std::uint32_t>& out) {
        for(const auto npc:npcs) {
            const auto comps=rig::list(read,npc+0x268,4096);if(!comps) return false;
            std::vector<std::uintptr_t> data(comps->packed_count);
            if(!data.empty() && !read(comps->data,data.data(),data.size()*8)) return false;
            for(const auto c:data) {
                if(!rig::typed(read,base,c,NativeMelee::inventory_vtable_rva) || rig::value<std::uintptr_t>(read,c+0x10)!=npc) continue;
                const auto list=rig::list(read,c+0x58,1024);if(!list) return false;
                std::vector<std::uintptr_t> records(list->packed_count);
                if(!records.empty() && !read(list->data,records.data(),records.size()*8)) return false;
                for(const auto r:records) if(rig::pointer(r)) if(const auto id=rig::value<std::uint32_t>(read,r+8)) out.push_back(*id);
                break;
            }
        }
        std::sort(out.begin(),out.end());return true;
    }
    template<class Read> static void scan(std::uintptr_t base,Read& read,std::uintptr_t game,std::uintptr_t player) {
        ++scans;
        registry_.resize(0x3fff);
        if(!read(game+NativeMelee::entities_offset,registry_.data(),registry_.size()*8)) return;
        std::vector<npc_weapon::Found> found;std::vector<std::uintptr_t> npcs;
        for(unsigned i=1;i<registry_.size();++i) {
            const auto e=registry_[i];
            if(!rig::pointer(e)) continue;
            const auto vt=rig::value<std::uintptr_t>(read,e);if(!vt) continue;
            if(*vt==base+NativeMelee::npc_vtable_rva) {if(npcs.size()<256) npcs.push_back(e);continue;}
            if(*vt!=base+id_entity_vtable_rva) continue;
            if(auto f=candidate(base,read,e,i)) found.push_back(std::move(*f));
        }
        scan_candidates+=found.size();
        std::vector<std::uint32_t> equipped;
        if(!found.empty() && !equipped_ids(base,read,npcs,equipped)) {++scan_degraded;return;}   // cannot tell carried from dropped: keep what we have
        std::vector<npc_weapon::Found> dropped;
        for(auto& f:found) {
            if(std::binary_search(equipped.begin(),equipped.end(),f.id)) continue;               // still in an NPC's hands
            if(f.hidden) {                                                                         // hidden and dropped: the book's own, or an orphan of a save
                bool ours;{std::lock_guard<std::mutex> l(m_);const auto* e=book_.find(f.id);ours=e!=nullptr;}
                if(ours || !arm_sweep_) continue;
                if(!visible(base,read,f.entity,true)) continue;
                f.hidden=false;++orphans_restored;
            }
            dropped.push_back(std::move(f));
        }
        arm_sweep_=false;
        Vec3 around{};
        if(const auto o=rig::value<std::array<float,3>>(read,player+0x300)) around={(*o)[0],(*o)[1],(*o)[2]};
        std::lock_guard<std::mutex> l(m_);
        book_.merge_scan(dropped,around,clock_ms());
    }
    // player_sword.damage by name, resolved once (the decl objects live for the process).
    template<class Read> static std::uintptr_t damage_decl(std::uintptr_t base,Read& read) {
#ifdef D2VR_TESTING
        if(decl_hook) return decl_hook();
#endif
        static std::uintptr_t decl{};
        if(decl) return decl;
        std::array<unsigned char,48> find{};
        if(!read(base+NativeMelee::find_decl_rva,find.data(),find.size()) || find!=NativeImpactFx::find_decl_code ||
           rig::value<std::uintptr_t>(read,base+NativeMelee::damage_decl_type_rva)!=base+NativeMelee::damage_decl_info_rva) return 0;
        decl=find_decl(base);return decl;
    }
    static std::uintptr_t find_decl(std::uintptr_t base) {
        __try {return reinterpret_cast<std::uintptr_t>(reinterpret_cast<void* (__fastcall*)(std::uintptr_t,const char*,bool,bool)>(base+NativeMelee::find_decl_rva)(
            base+NativeMelee::damage_decl_type_rva,player_sword_damage,false,false));}
        __except(EXCEPTION_EXECUTE_HANDLER) {return 0;}
    }
};
} // namespace d2vr
