#pragma once
// Put down or throw an UNARMED grenade (also sticky grenade, stun mine, spring razor) and take it back with the game's own loot
// (docs: artifacts/claude/dual-wield/fleet/D-grenade-drop.md). Static ABI, Steam 17576867, image base 0x140000000. Never run in the game.
// - World pickup defs (arkPickup, native Use = loot, adds m_inventoryItem x m_quantity 1):
//   models/interactive/pickups_entities/ammo_grenade_regular.def | ammo_grenade_sticky.def | ammo_stun_mine.def | ammo_springrazor.def.
//   FindDecl 1e030(entityDef type 0x2434170, name, 0, 0), SpawnEntityDefAt 8ae9b0 (as NativeWeaponHands::spawn_def), Teleport 87d1e0.
// - One grenade out of the inventory: RemoveItem 614750(inventory, itemDecl, 1) (what the native throw consumes through 639360), then
//   63a2b0(weaponComponent) re-reads the count into comp+0xa0 (the "loaded" number the HUD and 638290 use). Posts the game's own 0x9c/0x16b events.
// - Put back (only when the native Use does not find the entity): AddItem 6156e0(inventory, itemDecl, 1) (the call the loot state makes at
//   0x841289), then 883f60 removes the entity. The inventory is *(playerFsm+0x328), the equipped gadget record *(inventory+0x70).
// - Runtime spawns have never simulated (sim v407/v413: components stay at identity flags 0x20000002), so the mod flies the pickup like the
//   dropped sword (gravity, native sphere cast, Teleport each game frame) and leaves it resting. If the entity moves by itself after a
//   Teleport (physics started), the mod stops steering it and counts physics_seen.
// - Mines and razors: the ammo_* defs embed the live cpntstunmine / cpntmine component. Sim v767 (user's chapter-4 save): on a runtime spawn it
//   starts, as on the level's own loot mines; whether it can strike while lying is unverified (status stun_probe/razor_probe.state = comp+0x108).
//   On since v768 (user: toss and catch every held item); mines_enabled=false turns them off.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "game_rig.hpp"
#include "native_world_trace.hpp"
#include "tracked_pose.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace d2vr {
namespace gadget_drop {
enum class Kind : unsigned {any,regular_grenade,sticky_grenade,stun_mine,spring_razor,heart};
struct KindInfo {Kind kind;const char* label;const char* equipment;const char* pickup_def;bool mine;const char* item;bool keep;}; // item: the inventory item decl (live name);
// keep: the item never leaves the inventory (the Heart, v773: its pickup is only seen in the world; a take or a re-equip removes it)
inline constexpr std::array<KindInfo,5> kinds{{
    {Kind::regular_grenade,"regular_grenade","regular_grenade_equipment","models/interactive/pickups_entities/ammo_grenade_regular.def",false,"regular_grenade.inventoryitem",false},
    {Kind::sticky_grenade,"sticky_grenade","sticky_grenade_equipment","models/interactive/pickups_entities/ammo_grenade_sticky.def",false,"sticky_grenade.inventoryitem",false},
    {Kind::stun_mine,"stun_mine","stun_mine_equipment","models/interactive/pickups_entities/ammo_stun_mine.def",true,"stun_mine.inventoryitem",false},
    {Kind::spring_razor,"spring_razor","springrazor_equipment","models/interactive/pickups_entities/ammo_springrazor.def",true,"springrazor.inventoryitem",false},
    {Kind::heart,"heart","heart_equipment","models/weapons/heart/heart_pickup.def",false,"heart.inventoryitem",true}}};
inline const KindInfo* info(Kind k) noexcept {for(const auto& i:kinds) if(i.kind==k) return &i;return nullptr;}
// v781: the name NativeWeaponHands::test_equip finds the kind's inventory item by (the "equip" test command's, live since sim v769).
inline const char* equip_key(Kind k) noexcept {
    switch(k) {
    case Kind::regular_grenade: return "regular_grenade";case Kind::sticky_grenade: return "sticky_grenade";case Kind::stun_mine: return "stun_mine";
    case Kind::spring_razor: return "springrazor";case Kind::heart: return "heart";default: return nullptr;
    }
}
// The equipped weapon entity's decl name (".../regular_grenade_equipment.def", any case) -> its kind; Kind::any = not a gadget.
inline Kind classify(std::string_view name) noexcept {
    std::string lower(name);for(auto& c:lower) c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for(const auto& i:kinds) if(lower.find(i.equipment)!=std::string::npos || lower.find(i.item)!=std::string::npos) return i.kind;
    return Kind::any;
}

// A request after validation: finite, speed clamped, a proper right-handed frame (rows: forward, left, up, as entity+0x30c).
struct Launch {std::array<float,3> pos{},vel{};std::array<float,9> axis{1,0,0,0,1,0,0,0,1};};
inline constexpr float max_speed=25.f,min_reach=.05f,max_reach=1.f;
inline bool axis_valid(const std::array<float,9>& a) noexcept {
    for(float v:a) if(!std::isfinite(v)) return false;
    const Vec3 r[3]{{a[0],a[1],a[2]},{a[3],a[4],a[5]},{a[6],a[7],a[8]}};
    for(unsigned i=0;i<3;++i) {
        if(std::abs(length(r[i])-1)>.05) return false;
        for(unsigned j=0;j<i;++j) if(std::abs(dot(r[i],r[j]))>.1) return false;
    }
    return dot(cross(r[0],r[1]),r[2])>.9;
}
inline std::optional<Launch> sanitize(const std::array<float,3>& pos,const std::array<float,3>& vel,const std::array<float,9>& axis) noexcept {
    for(float v:pos) if(!std::isfinite(v) || std::abs(v)>1e7f) return {};
    for(float v:vel) if(!std::isfinite(v)) return {};
    if(!axis_valid(axis)) return {};
    Launch l{pos,vel,axis};
    const double speed=std::hypot(vel[0],std::hypot(vel[1],vel[2]));
    if(speed>max_speed) for(float& v:l.vel) v=static_cast<float>(v*(max_speed/speed));
    return l;
}

// A gadget the mod flies (pure: the world is a callback). Game world, Z up. Same model as SwordFlight, with a few damped bounces
// (a grenade is not a blade) and a landing that lies flat on the surface it met.
struct GadgetFlight {
    Vec3 pos{},vel{},spin_axis{0,0,1};std::array<Vec3,3> axis{Vec3{1,0,0},Vec3{0,1,0},Vec3{0,0,1}};double spin{},age{};unsigned bounces{};bool flying{};
    static constexpr double gravity=9.81,radius=.06,max_flight_s=6.,restitution=.35,friction=.7,rest_normal_speed=.9;
    static constexpr unsigned max_bounces=3;
    static bool finite(const GadgetFlight& f) noexcept {
        const auto ok=[](const Vec3& v){return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);};
        return ok(f.pos) && ok(f.vel) && ok(f.spin_axis) && ok(f.axis[0]) && ok(f.axis[1]) && ok(f.axis[2]) && std::isfinite(f.spin);
    }
    void launch(const Launch& l) noexcept {
        *this=GadgetFlight{};
        pos={l.pos[0],l.pos[1],l.pos[2]};vel={l.vel[0],l.vel[1],l.vel[2]};
        for(unsigned i=0;i<3;++i) axis[i]={l.axis[i*3],l.axis[i*3+1],l.axis[i*3+2]};
        const double speed=length(vel);const Vec3 side=cross(vel,Vec3{0,0,1});
        if(speed>2 && length(side)>1e-3) {spin_axis=side*(1/length(side));spin=-std::min(speed*1.2,12.);} // tumbles end over end
        flying=true;
    }
    // cast(from, to) -> the world hit, if any. Returns true while still flying.
    template<class Cast> bool step(double dt,Cast&& cast) {
        if(!flying) return false;
        age+=dt;
        if(age>=max_flight_s || !finite(*this)) {flying=false;vel={};return false;} // off the map: stays at the last finite position
        vel.z-=gravity*dt;const auto next=pos+vel*dt;
        const std::optional<WorldTraceHit> hit=cast(pos,next);
        if(!hit || !hit->static_support) {pos=next;tumble(dt);return true;} // free air, or a moving body (a guard) that does not stop it
        const Vec3 n=hit->normal_valid?hit->normal:Vec3{0,0,1};
        if(hit->initial_overlap) {
            if(hit->normal_valid && n.z>=.7) {pos=hit->point+n*(radius+.01);land(n);return false;}
            pos=next;return true;
        }
        pos=hit->centre+n*.01;
        const double into=dot(vel,n);
        if(into<0) {
            if(bounces<max_bounces && -into>rest_normal_speed) {vel=(vel-n*into)*friction+n*(-into*restitution);++bounces;spin*=.6;}
            else {vel=vel-n*into;if(n.z>=.7) {land(n);return false;}} // slides down a wall until the floor
        }
        return true;
    }
    void tumble(double dt) noexcept {
        if(spin==0) return;
        const double a=spin*dt,c=std::cos(a),s=std::sin(a);const auto u=spin_axis;
        for(auto& v:axis) v=v*c+cross(u,v)*s+u*(dot(u,v)*(1-c));
    }
    void land(Vec3 n) noexcept { // it lies on the surface: heading kept, up = the surface normal
        flying=false;vel={};spin=0;
        auto fwd=axis[0]-n*dot(axis[0],n);if(length(fwd)<1e-3) fwd=axis[1]-n*dot(axis[1],n);
        if(length(fwd)<1e-3) fwd=std::abs(n.x)<.9?Vec3{1,0,0}:Vec3{0,1,0};
        fwd=fwd*(1/length(fwd));fwd=fwd-n*dot(fwd,n);fwd=fwd*(1/length(fwd));
        axis={fwd,cross(n,fwd),n};
    }
    std::array<float,9> rows() const noexcept {
        std::array<float,9> a{};
        for(unsigned i=0;i<3;++i) {a[i*3]=static_cast<float>(axis[i].x);a[i*3+1]=static_cast<float>(axis[i].y);a[i*3+2]=static_cast<float>(axis[i].z);}
        return a;
    }
};

// The probe: what the engine started on the spawned pickup (read ~1 s after the spawn).
struct Probe {
    bool have{};unsigned components{};bool pickable{},pickable_started{},loot{},loot_started{},body{},body_started{},mine{},mine_started{};std::uint32_t entity_flags{};
    std::int32_t mine_state{-1};   // the mine logic's state (component +0x108, D-grenade-drop.md), -1 unread
    std::array<float,3> at{};
};
} // namespace gadget_drop

class NativeGadgetDrop {
public:
    using Kind=gadget_drop::Kind;using Probe=gadget_drop::Probe;using Launch=gadget_drop::Launch;
    enum class Op : unsigned {drop,take,spawn};
    struct Request {Op op{};Kind kind{};Launch launch{};float reach{};};
    // A tracked drop, published for the control thread (position, whether it lies still).
    struct Published {std::array<float,3> pos{};Kind kind{};bool resting{};std::uintptr_t entity{};bool keep{};};
    static constexpr std::size_t max_tracked=8,max_queue=4;
    // v784 (user, headset v793: "I drop a grenade and the hand stays blocked"; 82 drops met "no gadget in the left slot"): the left slot still
    // holds the gadget item with a count of 0 after its last one left. Game thread writes, any thread reads; false when unknown.
    inline static std::atomic<bool> left_spent{};
    inline static std::atomic<bool> mines_enabled{true};    // see the header: the mine defs embed a live mine component
    inline static std::atomic<std::uint64_t> requested{},rejected{},queue_full{},services{},dropped{},not_verified{},no_item{},kind_mismatch{},mines_blocked{},
        spawn_failed{},remove_failed{},rolled_back{},infinite_ammo{},landed{},physics_seen{},deviated{},untracked{},taken{},take_failed{},take_none{},faults{},self_hits{};
    // Any thread. kind Kind::any = whatever gadget the left slot holds. pos/vel in game world metres and m/s, axis rows forward,left,up.
    static bool drop(Kind kind,const std::array<float,3>& pos,const std::array<float,3>& vel,const std::array<float,9>& axis) {
        ++requested;
        const auto l=gadget_drop::sanitize(pos,vel,axis);
        if(!l || (kind!=Kind::any && !gadget_drop::info(kind))) {++rejected;return false;}
        return push({Op::drop,kind,*l,0});
    }
    // Any thread, sim probes only: the same pickup flown from `pos`, with NOTHING taken from the inventory (the test saves hold no grenades).
    // The native Use then shows whether the game's loot works on a mod-spawned pickup. It cannot be put back by take().
    static bool spawn_free(Kind kind,const std::array<float,3>& pos,const std::array<float,3>& vel,const std::array<float,9>& axis) {
        ++requested;
        const auto l=gadget_drop::sanitize(pos,vel,axis);
        if(!l || kind==Kind::any || !gadget_drop::info(kind)) {++rejected;return false;}
        return push({Op::spawn,kind,*l,0});
    }
    // Any thread: the hand at `pos` takes the nearest tracked drop within `reach` metres back into the inventory (when the native Use does not).
    static bool take(const std::array<float,3>& pos,float reach) {
        ++requested;
        Launch l;l.pos=pos;
        if(!std::all_of(pos.begin(),pos.end(),[](float v){return std::isfinite(v);}) || !std::isfinite(reach)) {++rejected;return false;}
        return push({Op::take,Kind::any,l,std::clamp(reach,gadget_drop::min_reach,gadget_drop::max_reach)});
    }
    static std::vector<Published> published() {std::lock_guard<std::mutex> l(m_);return published_;}
    static Probe probe() {std::lock_guard<std::mutex> l(m_);return probe_;}
    static const char* last_error() {std::lock_guard<std::mutex> l(m_);return last_error_;}
    static std::string status_json() {
        char b[1900];
        const auto p=probe();const auto t=published();
        std::snprintf(b,sizeof(b),"{\"requested\":%llu,\"rejected\":%llu,\"queue_full\":%llu,\"services\":%llu,\"dropped\":%llu,\"not_verified\":%llu,\"no_item\":%llu,"
            "\"kind_mismatch\":%llu,\"mines_blocked\":%llu,\"mines_enabled\":%s,\"spawn_failed\":%llu,\"remove_failed\":%llu,\"rolled_back\":%llu,\"infinite_ammo\":%llu,"
            "\"landed\":%llu,\"physics_seen\":%llu,\"deviated\":%llu,\"untracked\":%llu,\"taken\":%llu,\"take_failed\":%llu,\"take_none\":%llu,\"faults\":%llu,\"tracked\":%zu,"
            "\"last_error\":\"%s\",\"probe\":{\"have\":%s,\"components\":%u,\"pickable\":%s,\"pickable_started\":%s,\"loot\":%s,\"loot_started\":%s,\"body\":%s,"
            "\"body_started\":%s,\"mine\":%s,\"mine_started\":%s,\"entity_flags\":%u,\"at\":[%.2f,%.2f,%.2f]}}",
            (unsigned long long)requested.load(),(unsigned long long)rejected.load(),(unsigned long long)queue_full.load(),(unsigned long long)services.load(),
            (unsigned long long)dropped.load(),(unsigned long long)not_verified.load(),(unsigned long long)no_item.load(),(unsigned long long)kind_mismatch.load(),
            (unsigned long long)mines_blocked.load(),mines_enabled.load()?"true":"false",(unsigned long long)spawn_failed.load(),(unsigned long long)remove_failed.load(),
            (unsigned long long)rolled_back.load(),(unsigned long long)infinite_ammo.load(),(unsigned long long)landed.load(),(unsigned long long)physics_seen.load(),
            (unsigned long long)deviated.load(),(unsigned long long)untracked.load(),(unsigned long long)taken.load(),(unsigned long long)take_failed.load(),
            (unsigned long long)take_none.load(),(unsigned long long)faults.load(),t.size(),last_error(),
            p.have?"true":"false",p.components,p.pickable?"true":"false",p.pickable_started?"true":"false",p.loot?"true":"false",p.loot_started?"true":"false",
            p.body?"true":"false",p.body_started?"true":"false",p.mine?"true":"false",p.mine_started?"true":"false",p.entity_flags,p.at[0],p.at[1],p.at[2]);
        std::string out(b);std::array<Probe,2> mp;{std::lock_guard<std::mutex> l(m_);mp=mine_probes_;}
        for(unsigned i=0;i<2;++i) if(mp[i].have) { // the mine probes (sim): did the pickup's own mine component start?
            char m[180];std::snprintf(m,sizeof(m),",\"%s\":{\"mine\":%s,\"mine_started\":%s,\"state\":%d,\"components\":%u,\"flags\":%u}",i?"razor_probe":"stun_probe",
                mp[i].mine?"true":"false",mp[i].mine_started?"true":"false",mp[i].mine_state,mp[i].components,mp[i].entity_flags);
            out.insert(out.size()-1,m);}
        out.insert(out.size()-1,",\"self_hits\":"+std::to_string(self_hits.load()));
        return out;
    }

    // Game thread (NativeInteractionAim stage-0 scope, gameplay only; wrap in a CrashOperation like the other services).
    // v783: this service's flight steps (game thread), for the thrown stun the caller applies right after (NativeWeaponHands::thrown_stun).
    // `struck` points into tracked_, valid until the next service or request runs.
    struct Flying {std::uintptr_t entity{};std::uint32_t id{};Vec3 from{},to{},axis{};double speed{};std::vector<std::uintptr_t>* struck{};};
    static const std::vector<Flying>& flights() noexcept {return flights_;}
    template<class Read> static void service(std::uintptr_t base,Read& read) {
        flights_.clear();
        std::vector<Request> work;{std::lock_guard<std::mutex> l(m_);work.swap(queue_);}
        ++services;
        if(!verified(base,read)) {not_verified+=work.size();fail("pinned bytes differ");return;}
        for(const auto& r:work) {
            try {if(r.op==Op::take) do_take(base,read,r);else do_drop(base,read,r);} catch(...) {++faults;}
        }
        try {fly_all(base,read);} catch(...) {++faults;}
        try {drop_kept_twin(base,read);} catch(...) {++faults;}
        try {const auto c=context(base,read);const auto eq=c?equipped(base,read,*c):std::nullopt;left_spent=eq && eq->count<=0;} catch(...) {left_spent=false;}
        publish();
    }
    // A keep item (the Heart) back in the hand by any way (the wheel) while its pickup still lies in the world: the pickup goes, one Heart
    // only. The first second after a drop is left alone (the hand's unequip follows the drop request).
    template<class Read> static void drop_kept_twin(std::uintptr_t base,Read& read) {
        if(std::none_of(tracked_.begin(),tracked_.end(),[](const Tracked& t){return t.keep;})) return;
        const auto c=context(base,read);if(!c) return;const auto eq=equipped(base,read,*c);if(!eq) return;
        const auto* k=gadget_drop::info(eq->kind);if(!k || !k->keep) return;
        const double now=now_s();
        for(auto it=tracked_.begin();it!=tracked_.end();) {
            if(it->keep && it->kind==eq->kind && now-it->spawn_s>=1.) {engine_remove_entity(base,it->entity);it=tracked_.erase(it);++taken;}
            else ++it;
        }
    }

private:
    struct Tracked {
        std::uintptr_t entity{},item_decl{};std::uint32_t id{};Kind kind{};gadget_drop::GadgetFlight flight;
        std::array<float,3> commanded{},rest_pos{};std::array<float,9> rest_axis{};bool have_command{},physics{},rested{},probed{};
        bool keep{};                                // KindInfo::keep: the item stayed in the inventory
        int count{1};                               // v790: the stack this pickup carries (0: the game consumed nothing)
        std::optional<NativeCastExclusion> self;   // its own body, once the engine started it (sim v767: else the sweep met only the item and it fell through the deck)
        double last_s{},rest_s{},spawn_s{},probe_s{};
        std::vector<std::uintptr_t> struck;        // v783: the guards this throw stunned (NativeWeaponHands::thrown_stun)
    };
    inline static std::mutex m_;inline static std::vector<Request> queue_;inline static std::vector<Published> published_;
    inline static Probe probe_{};inline static std::array<Probe,2> mine_probes_{};inline static const char* last_error_=""; // stun mine, spring razor
    inline static std::vector<Tracked> tracked_;   // game thread only
    inline static std::vector<Flying> flights_;    // game thread only (flights())
    static constexpr double settle_watch_s=3.,probe_after_s=1.;
    static constexpr std::uintptr_t game_rva=0x39a5290,game_vtable=0x1c940a8,player_vtable=0x1c5e428,fsm_vtable=0x1c73d58,inventory_vtable=0x1b9a878,
        throwable_vtable=0x1b9d8b8,placeable_vtable=0x1b54538,pickable_vtable=0x1c5a3b8,loot_vtable=0x1c55bb8,body_vtable=0x1c745c8,throwable_body_vtable=0x1b64b68,
        mine_vtable=0x1b50df8,stun_mine_vtable=0x1b537e8,heart_vtable=0x1b99c68; // arkCpntMine (spring razor), arkCpntStunMine, arkCpntHeart (rtti.pkl)
    static void fail(const char* why) {std::lock_guard<std::mutex> l(m_);last_error_=why;}
    static bool push(const Request& r) {
        std::lock_guard<std::mutex> l(m_);
        if(queue_.size()>=max_queue) {++queue_full;return false;}
        queue_.push_back(r);return true;
    }
#ifdef D2VR_TESTING
public:
    struct Calls {   // tests replace the engine
        std::uintptr_t (*spawn)(std::uintptr_t,const char*,const float*,const float*){};
        int (*remove_item)(std::uintptr_t,std::uintptr_t,std::uintptr_t,int){};
        int (*add_item)(std::uintptr_t,std::uintptr_t,std::uintptr_t,int){};
        void (*sync_ammo)(std::uintptr_t,std::uintptr_t){};
        void (*teleport)(std::uintptr_t,std::uintptr_t,const float*,const float*){};
        void (*remove_entity)(std::uintptr_t,std::uintptr_t){};
    };
    inline static Calls calls{};
    inline static std::optional<WorldTraceHit> (*cast_hook)(Vec3,Vec3){};
    inline static double (*clock_hook)(){};
    static void reset_for_tests() {std::lock_guard<std::mutex> l(m_);queue_.clear();published_.clear();probe_={};mine_probes_={};tracked_.clear();last_error_="";}
private:
#endif
    static double now_s() {
#ifdef D2VR_TESTING
        if(clock_hook) return clock_hook();
#endif
        return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    // ---- the engine (each call guarded; a fault is a failure, never a crash) ----
    static std::uintptr_t engine_spawn(std::uintptr_t base,const char* def,const float* pos,const float* axis) noexcept {
#ifdef D2VR_TESTING
        if(calls.spawn) return calls.spawn(base,def,pos,axis);
#endif
        __try {
            void* decl=reinterpret_cast<void* (__fastcall*)(std::uintptr_t,const char*,bool,bool)>(base+0x1e030)(base+0x2434170,def,false,false);
            const auto game=*reinterpret_cast<const std::uintptr_t*>(base+game_rva);
            if(!decl || !game) return 0;
            static const float zero[3]{};
            return reinterpret_cast<std::uintptr_t (__fastcall*)(std::uintptr_t,void*,const float*,const float*,std::uintptr_t,bool,const float*,const float*,const float*)>(
                base+0x8ae9b0)(game,decl,pos,axis,0,false,nullptr,zero,zero);
        } __except(EXCEPTION_EXECUTE_HANDLER) {return 0;}
    }
    static int engine_remove_item(std::uintptr_t base,std::uintptr_t inventory,std::uintptr_t decl,int n) noexcept {
#ifdef D2VR_TESTING
        if(calls.remove_item) return calls.remove_item(base,inventory,decl,n);
#endif
        __try {return static_cast<int>(reinterpret_cast<std::uint64_t (__fastcall*)(std::uintptr_t,std::uintptr_t,int)>(base+0x614750)(inventory,decl,n));}
        __except(EXCEPTION_EXECUTE_HANDLER) {return -1;}
    }
    static int engine_add_item(std::uintptr_t base,std::uintptr_t inventory,std::uintptr_t decl,int n) noexcept {
#ifdef D2VR_TESTING
        if(calls.add_item) return calls.add_item(base,inventory,decl,n);
#endif
        __try {return static_cast<int>(reinterpret_cast<std::uint64_t (__fastcall*)(std::uintptr_t,std::uintptr_t,int)>(base+0x6156e0)(inventory,decl,n));}
        __except(EXCEPTION_EXECUTE_HANDLER) {return -1;}
    }
    static void engine_sync_ammo(std::uintptr_t base,std::uintptr_t comp) noexcept {
#ifdef D2VR_TESTING
        if(calls.sync_ammo) {calls.sync_ammo(base,comp);return;}
#endif
        __try {reinterpret_cast<void (__fastcall*)(std::uintptr_t)>(base+0x63a2b0)(comp);} __except(EXCEPTION_EXECUTE_HANDLER) {}
    }
    static void engine_teleport(std::uintptr_t base,std::uintptr_t e,const std::array<float,3>& pos,const std::array<float,9>& axis) noexcept {
        if(!std::all_of(pos.begin(),pos.end(),[](float v){return std::isfinite(v);}) || !std::all_of(axis.begin(),axis.end(),[](float v){return std::isfinite(v);})) return; // Teleport 87d1e0 has no finite check
        alignas(16) std::array<float,3> o=pos;alignas(16) std::array<float,9> a=axis;
#ifdef D2VR_TESTING
        if(calls.teleport) {calls.teleport(base,e,o.data(),a.data());return;}
#endif
        __try {reinterpret_cast<void (__fastcall*)(std::uintptr_t,const float*,const float*,int)>(base+0x87d1e0)(e,o.data(),a.data(),0);} __except(EXCEPTION_EXECUTE_HANDLER) {}
    }
    static void engine_remove_entity(std::uintptr_t base,std::uintptr_t e) noexcept {
#ifdef D2VR_TESTING
        if(calls.remove_entity) {calls.remove_entity(base,e);return;}
#endif
        __try {reinterpret_cast<void (__fastcall*)(std::uintptr_t,std::uintptr_t,int)>(base+0x883f60)(e,base+0x39a6f30,0);} __except(EXCEPTION_EXECUTE_HANDLER) {}
    }

    template<class Read> static bool verified(std::uintptr_t base,Read& read) {
        static const bool ok=[&]{
            static const std::array<std::pair<std::uintptr_t,std::array<unsigned char,16>>,7> pins{{
                {0x1e030,{0x44,0x88,0x44,0x24,0x18,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57}},
                {0x8ae9b0,{0x40,0x57,0x41,0x56,0x41,0x57,0x48,0x83,0xec,0x40,0x48,0xc7,0x44,0x24,0x20,0xfe}},
                {0x883f60,{0x48,0x83,0xec,0x28,0x45,0x33,0xc9,0xe8,0xc4,0x00,0x00,0x00,0x48,0x83,0xc4,0x28}},
                {0x87d1e0,{0x48,0x83,0xec,0x38,0xc7,0x44,0x24,0x20,0x00,0x00,0x00,0x00,0xe8,0x0f,0x00,0x00}},
                {0x614750,{0x4c,0x8b,0xdc,0x57,0x48,0x83,0xec,0x60,0x49,0xc7,0x43,0xb8,0xfe,0xff,0xff,0xff}},
                {0x6156e0,{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x50,0x8b}},
                {0x63a2b0,{0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0x81,0x98,0x00,0x00}}}};
            for(const auto& [rva,code]:pins) {std::array<unsigned char,16> got{};if(!read(base+rva,got.data(),got.size()) || got!=code) return false;}
            return true;
        }();
        return ok;
    }

    // ---- the world ----
    struct Context {std::uintptr_t game{},player{},inventory{};};
    template<class Read> static std::optional<Context> context(std::uintptr_t base,Read& read) {
        Context c;
        const auto game=rig::value<std::uintptr_t>(read,base+game_rva);if(!game || !rig::typed(read,base,*game,game_vtable)) return {};
        const auto player=rig::value<std::uintptr_t>(read,*game+0x3130b0);if(!player || !rig::typed(read,base,*player,player_vtable)) return {};
        const auto entries=rig::list(read,*player+0x268,4096);if(!entries) return {};
        std::uintptr_t fsm=0;
        for(unsigned i=0;i<entries->packed_count && !fsm;++i) {
            const auto comp=rig::value<std::uintptr_t>(read,entries->data+i*8ull);
            if(comp && rig::typed(read,base,*comp,fsm_vtable) && rig::value<std::uintptr_t>(read,*comp+0x10)==*player && rig::started(read,*comp)) fsm=*comp;
        }
        if(!fsm) return {};
        const auto inventory=rig::value<std::uintptr_t>(read,fsm+0x328);
        if(!inventory || !rig::typed(read,base,*inventory,inventory_vtable) || rig::value<std::uintptr_t>(read,*inventory+0x10)!=*player) return {};
        c.game=*game;c.player=*player;c.inventory=*inventory;return c;
    }
    template<class Read> static bool alive_entity(std::uintptr_t game,Read& read,std::uintptr_t e,std::uint32_t id) {
        if(!e || !id || (id>>16)>=0x3fff) return false;
        return rig::value<std::uintptr_t>(read,game+0x3130b0+(id>>16)*8ull)==e && rig::value<std::uint32_t>(read,e+0xa8)==id;
    }
    template<class Read> static std::string decl_name(Read& read,std::uintptr_t e) {
        const auto decl=rig::value<std::uintptr_t>(read,e+0x2e8);const auto name=decl?rig::value<std::uintptr_t>(read,*decl+0x48):std::nullopt;
        std::array<char,128> text{};
        if(!name || !rig::pointer(*name) || !read(*name,text.data(),text.size()-1)) return {};
        return std::string(text.data());
    }
    // An arkDeclInventoryItem's name (+0x8, live v769: "components/equipment/gadgets/stun_mine.inventoryitem").
    template<class Read> static std::string item_name(Read& read,std::uintptr_t decl) {
        const auto name=rig::value<std::uintptr_t>(read,decl+0x8);std::array<char,128> text{};
        if(!name || !rig::pointer(*name) || !read(*name,text.data(),text.size()-1)) return {};
        return std::string(text.data());
    }
    // The gadget in the left slot (inventory+0x70): its record, item decl, weapon component, count and kind. The kind is the item's (live v769: every
    // grenade weapon entity is the generic grenade_base.def, so the entity name refused every real put-down); the entity name stays a fallback.
    struct Equipped {std::uintptr_t record{},item_decl{},comp{};int count{};Kind kind{};};
    template<class Read> static std::optional<Equipped> equipped(std::uintptr_t base,Read& read,const Context& c) {
        const auto rec=rig::value<std::uintptr_t>(read,c.inventory+0x70);
        if(!rec || !rig::pointer(*rec)) return {};
        const auto decl=rig::value<std::uintptr_t>(read,*rec);const auto id=rig::value<std::uint32_t>(read,*rec+8);const auto comp=rig::value<std::uintptr_t>(read,*rec+0x10);
        const auto count=rig::value<std::int32_t>(read,*rec+0x20);
        if(!decl || !rig::pointer(*decl) || !id || !comp || !count || (*id>>16)>=0x3fff) return {};
        const auto entity=rig::value<std::uintptr_t>(read,c.game+0x3130b0+(*id>>16)*8ull);
        if(!entity || !alive_entity(c.game,read,*entity,*id)) return {};
        Kind kind=gadget_drop::classify(item_name(read,*decl));if(kind==Kind::any) kind=gadget_drop::classify(decl_name(read,*entity));
        Equipped e{*rec,*decl,*comp,*count,kind};
        if(!rig::typed(read,base,*comp,throwable_vtable) && !rig::typed(read,base,*comp,placeable_vtable) && !rig::typed(read,base,*comp,heart_vtable)) e.kind=Kind::any;
        return e;
    }
    template<class Read> static int item_count(Read& read,std::uintptr_t record) {return rig::value<std::int32_t>(read,record+0x20).value_or(-1);}

    template<class Read> static void do_drop(std::uintptr_t base,Read& read,const Request& r) {
        const bool free=r.op==Op::spawn;
        const auto c=context(base,read);if(!c) {++no_item;fail("no player or inventory");return;}
        std::optional<Equipped> eq;Kind kind=r.kind;
        if(!free) {
            eq=equipped(base,read,*c);
            if(!eq || eq->kind==Kind::any || eq->count<=0) {++no_item;fail("no gadget in the left slot");return;}
            if(r.kind!=Kind::any && r.kind!=eq->kind) {++kind_mismatch;fail("the left slot holds another gadget");return;}
            kind=eq->kind;
        }
        const auto* k=gadget_drop::info(kind);
        if(k->mine && !mines_enabled.load() && !free) {++mines_blocked;fail("mines are off (unverified)");return;} // the free spawn is the probe that decides
        if(tracked_.size()>=max_tracked) {tracked_.erase(tracked_.begin());++untracked;} // the oldest stays in the world, the game's Use still takes it
        const auto entity=engine_spawn(base,k->pickup_def,r.launch.pos.data(),r.launch.axis.data());
        if(!entity) {++spawn_failed;fail("spawn failed");return;}
        const auto id=rig::value<std::uint32_t>(read,entity+0xa8).value_or(0);
        int stack=1;
        if(eq && !k->keep) {
            // v790 (user, headset v799: "dropping a grenade I cannot pick up the second one; drop them as one pack, as loot in games"): the
            // whole stack leaves as this one pickup, and taking it back returns the whole stack (Tracked::count)
            const int before=eq->count;
            engine_remove_item(base,c->inventory,eq->item_decl,before);
            const int after=item_count(read,eq->record);
            if(after<0 || after>before) {engine_remove_entity(base,entity);++rolled_back;++remove_failed;fail("inventory not readable after the removal");return;}
            if(after==before) ++infinite_ammo;   // the game's own rule: nothing is consumed (inventory+0x168 > 0)
            stack=before-after;
            engine_sync_ammo(base,eq->comp);
        }
        Tracked t;t.entity=entity;t.id=id;t.item_decl=eq?eq->item_decl:0;t.kind=kind;t.keep=k->keep;t.count=stack;t.spawn_s=t.last_s=now_s();
        t.flight.launch(r.launch);t.rest_pos=r.launch.pos;t.rest_axis=r.launch.axis;
        tracked_.push_back(t);++dropped;
    }
    template<class Read> static void do_take(std::uintptr_t base,Read& read,const Request& r) {
        const auto c=context(base,read);if(!c) {++take_failed;fail("no player or inventory");return;}
        std::size_t best=tracked_.size();double best_d=r.reach;
        for(std::size_t i=0;i<tracked_.size();++i) {
            auto& t=tracked_[i];
            if(!t.item_decl || !alive_entity(c->game,read,t.entity,t.id)) continue;   // a free probe spawn has no item to give back
            const auto p=rig::value<std::array<float,3>>(read,t.entity+0x300);if(!p) continue;
            const double d=length(Vec3{(*p)[0]-r.launch.pos[0],(*p)[1]-r.launch.pos[1],(*p)[2]-r.launch.pos[2]});
            if(d<=best_d) {best_d=d;best=i;}
        }
        if(best==tracked_.size()) {++take_none;return;}
        auto& t=tracked_[best];
        if(!t.keep && t.count>0 && engine_add_item(base,c->inventory,t.item_decl,t.count)<=0) {++take_failed;fail("inventory refused the item (full)");return;} // the entity stays
        engine_remove_entity(base,t.entity);
        if(const auto eq=equipped(base,read,*c); !t.keep && eq && eq->kind==t.kind) engine_sync_ammo(base,eq->comp); // v779: the Heart has no ammo count (63a2b0 tail-calls vtable+0x108, past arkCpntHeart's 32 slots); a catch within a frame of the unequip meets it still in the slot
        tracked_.erase(tracked_.begin()+static_cast<std::ptrdiff_t>(best));++taken;
    }

    template<class Read> static Probe read_probe(std::uintptr_t base,Read& read,const Tracked& t) {
        Probe p;p.have=true;
        p.entity_flags=rig::value<std::uint32_t>(read,t.entity+0xd0).value_or(0);
        p.at=rig::value<std::array<float,3>>(read,t.entity+0x300).value_or(std::array<float,3>{});
        const auto list=rig::list(read,t.entity+0x268,4096);if(!list) return p;
        p.components=list->packed_count;
        for(unsigned i=0;i<list->packed_count;++i) {
            const auto comp=rig::value<std::uintptr_t>(read,list->data+i*8ull);if(!comp) continue;
            const bool st=rig::started(read,*comp).has_value();
            if(rig::typed(read,base,*comp,pickable_vtable)) {p.pickable=true;p.pickable_started=st;}
            else if(rig::typed(read,base,*comp,loot_vtable)) {p.loot=true;p.loot_started=st;}
            else if(rig::typed(read,base,*comp,body_vtable) || rig::typed(read,base,*comp,throwable_body_vtable)) {p.body=true;p.body_started=st;}
            else if(rig::typed(read,base,*comp,mine_vtable) || rig::typed(read,base,*comp,stun_mine_vtable)) {p.mine=true;p.mine_started=st;p.mine_state=rig::value<std::int32_t>(read,*comp+0x108).value_or(-1);}
        }
        return p;
    }

    template<class Read> static void fly_all(std::uintptr_t base,Read& read) {
        if(tracked_.empty()) return;
        const auto game=rig::value<std::uintptr_t>(read,base+game_rva);
        if(!game) return;
        const auto world=rig::value<std::uintptr_t>(read,*game+NativeWorldTrace::world_offset);
        bool can_cast=false;
#ifdef D2VR_TESTING
        can_cast=cast_hook!=nullptr;
#else
        static const bool trace_ok=NativeWorldTrace::verify(base,read);
        can_cast=trace_ok && world && *world;
#endif
        const double now=now_s();
        for(std::size_t i=0;i<tracked_.size();) {
            auto& t=tracked_[i];
            if(!alive_entity(*game,read,t.entity,t.id)) {tracked_.erase(tracked_.begin()+static_cast<std::ptrdiff_t>(i));continue;}   // taken by the native Use, removed or unloaded
            ++i;
            const bool mine=t.kind==Kind::stun_mine || t.kind==Kind::spring_razor;
            if((!t.probed && now-t.spawn_s>=probe_after_s) || (mine && t.probed && now-t.probe_s>=1)) { // a lying mine: its state every second (does it arm?)
                const bool first=!t.probed;t.probed=true;t.probe_s=now;const auto p=read_probe(base,read,t);
                std::lock_guard<std::mutex> l(m_);if(first) probe_=p;if(mine) mine_probes_[t.kind==Kind::spring_razor?1:0]=p;
            }
            if(t.physics) continue;
            if(t.have_command) {   // it moved by itself since our last Teleport: the engine simulates it now
                const auto at=rig::value<std::array<float,3>>(read,t.entity+0x300);
                if(at && length(Vec3{(*at)[0]-t.commanded[0],(*at)[1]-t.commanded[1],(*at)[2]-t.commanded[2]})>.08) {
                    if(t.flight.flying || now-t.rest_s>settle_watch_s) {t.physics=true;t.flight.flying=false;++physics_seen;continue;}
                    ++deviated;engine_teleport(base,t.entity,t.rest_pos,t.rest_axis);t.commanded=t.rest_pos;continue; // settling: put it back
                }
            }
            if(!t.flight.flying || !can_cast) continue;   // blind: it stays at its last pose
            const double dt=std::min(now-t.last_s,.05);
            if(dt<.001) continue;
            t.last_s=now;
            const Vec3 from=t.flight.pos;const double speed=length(t.flight.vel);
            t.flight.step(dt,[&](Vec3 a,Vec3 b)->std::optional<WorldTraceHit> {
                std::optional<WorldTraceHit> hit;
#ifdef D2VR_TESTING
                if(cast_hook) hit=cast_hook(a,b);else
#endif
                {
                    const auto sweep=[&] {return NativeWorldTrace::sphere_cast(base,read,reinterpret_cast<NativeWorldTrace::Cast>(base+NativeWorldTrace::cast_rva),
                        reinterpret_cast<NativeWorldTrace::Construct>(base+NativeWorldTrace::sphere_ctor_rva),*world,a,b,gadget_drop::GadgetFlight::radius,
                        kWorldOccluderLayers,kWorldOccluderRequired,t.self?&*t.self:nullptr);};
                    hit=sweep();
                    if(hit && hit->entity==t.entity && !t.self) { // its own body (a pickup: native_cast_exclusion takes props only), from the hit's handle as
                        // native_rigid_body maps one (pool world+0x28, count +0x30, record +index*0xb0, full id +0x70, filter word +0x6c); this sweep again
                        const auto pool=rig::value<std::uintptr_t>(read,*world+NativeWorldTrace::body_pool_offset);
                        const auto count=rig::value<std::uint32_t>(read,*world+NativeWorldTrace::body_pool_offset+8);const auto index=hit->body_handle&0xffffffu;
                        if(pool && rig::pointer(*pool) && count && index<*count) {
                            const auto record=*pool+index*0xb0ull;
                            if(rig::value<std::uint32_t>(read,record+0x70)==hit->body_handle) {
                                t.self=NativeCastExclusion{record,hit->body_handle,rig::value<std::uint32_t>(read,record+0x6c).value_or(0)};hit=sweep();}
                        }
                    }
                }
                if(hit && hit->entity==t.entity) {++self_hits;hit->static_support=false;} // its own body is never its floor
                return hit;
            });
            if(!gadget_drop::GadgetFlight::finite(t.flight)) {t.flight.flying=false;continue;}   // never hand the engine a non-finite transform
            flights_.push_back({t.entity,t.id,from,t.flight.pos,t.flight.axis[0],speed,&t.struck});
            t.commanded={float(t.flight.pos.x),float(t.flight.pos.y),float(t.flight.pos.z)};t.have_command=true;
            const auto rows=t.flight.rows();
            engine_teleport(base,t.entity,t.commanded,rows);
            if(!t.flight.flying) {t.rest_pos=t.commanded;t.rest_axis=rows;t.rested=true;t.rest_s=now;++landed;}
        }
    }
    static void publish() {
        std::vector<Published> out;
        for(const auto& t:tracked_) out.push_back({t.have_command?t.commanded:t.rest_pos,t.kind,t.rested || t.physics,t.entity,t.keep});
        std::lock_guard<std::mutex> l(m_);published_=std::move(out);
    }
};
} // namespace d2vr
