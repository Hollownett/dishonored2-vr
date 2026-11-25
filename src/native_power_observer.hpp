#pragma once
// Read-only observer of the player's power components and the player states around them (plan 2026-11-24-doto-billie-vr Task 3;
// docs/doto-research/2026-11-24-R2-powers.md section 2). Every read goes through the guarded Read: a bad pointer yields defaults.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "game_profile.hpp"
#include "game_rig.hpp"
#include "native_takedown.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace d2vr {
enum class PowerKind : std::uint32_t {none,blink,far_reach,doppelganger,possession,domino,mesmerize,windblast,bendtime,dark_vision,shadow_walk,
    devouring_swarm,clone,displace,foresight,semblance,count_};
inline const char* power_kind_name(std::uint32_t k) {
    static constexpr const char* names[]={"none","blink","far_reach","doppelganger","possession","domino","mesmerize","windblast","bendtime",
        "dark_vision","shadow_walk","devouring_swarm","clone","displace","foresight","semblance"};
    return k<sizeof(names)/sizeof(names[0])?names[k]:"unknown";
}
// Player-state bits of PowerSnapshot::states
namespace power_state_bit {
inline constexpr std::uint32_t choke=1u<<0,use_power=1u<<1,change_weapon=1u<<2,swapper=1u<<3,cancel_void_strike=1u<<4,mimicking=1u<<5,
    use_mimic=1u<<6,possession=1u<<7;
}
struct PowerSnapshot {
    bool found_player{};
    int power_count{};
    std::uint32_t present_mask{};         // bit PowerKind: the powers the player owns (player list or inventory)
    int hook_mines{-1};                   // DOTO: Hook Mines in the inventory (the HookMineEquipment record's count, +0x20), -1 none
    int hook_mine_mode{-1};               // DOTO: the Hook Mine's mode, its WeaponStates current index: 0 non-lethal, 1 lethal, -1 unknown
    int hook_mine_pending{-1};            // its pending index while a switch runs, else -1
    std::uint32_t aiming_kind{};          // PowerKind of the first power whose aim byte (+0xa4) is set
    bool aim_active{},cast_flag{};        // any power: +0xa4, +0xa5
    bool swapper_present{},swapper_aiming{};
    std::uint8_t swapper_mode{};          // +0x259
    bool swapper_has_target{};
    float swapper_target[3]{};
    int theeye_state{-1};                 // +0x19c, -1 absent
    float theeye_body[3]{};float theeye_body_yaw{};
    bool mimic_present{},mimic_armed{};   // +0x120 byte
    int mimic_state{-1};                  // arkCpntMimic +0x4c: 2 = disguised
    std::uint32_t states{};               // power_state_bit::*, active flags
    std::uint32_t states_known{};         // bits whose state read succeeded (the id exists in the pool)
    // DOTO Void Strike (R6): the Slash player state (pool id 39). owned = slash+0xdb (set when a slash starts), commit_s = decl+0xa0 (float
    // seconds the hold time slash+0x38 must reach), mana = decl+0xf0 (int, -1 unknown), hold_s = slash+0x38 (float seconds, accumulates per frame)
    bool voidstrike_found{},voidstrike_owned{},voidstrike_committed{};
    float voidstrike_commit_s{},slash_hold_s{};
    int voidstrike_mana{-1};
    std::array<std::uint64_t,2> active_ids{};   // 2026-11-25: every active player state id 0..127 (diagnostics: which states run in a possession / a mimic choke)
};
// Per-game table: D2 vtables are D2 literals (tools/doto/port.py maps them by RTTI name); DOTO-only classes and the state ids come from game_traits.
struct PowerVtables {
    struct Entry {std::uintptr_t vtable;PowerKind kind;};
    std::array<Entry,15> entries{};
    std::array<std::uint32_t,8> state_ids{};   // order of power_state_bit
    std::uintptr_t hook_mine_vtable{};          // arkCpntHookMineEquipment (DOTO), 0 = none
    std::uintptr_t slash_vtable{};              // arkPlayerStateSlash (DOTO), 0 = no Void Strike read
    static PowerVtables current() {
        namespace t=game_traits;
        PowerVtables v;
        v.entries={{{0x1b9bb78,PowerKind::blink},{0x1b52818,PowerKind::far_reach},{0x1b52258,PowerKind::doppelganger},{0x1b531c8,PowerKind::possession},
            {0x1b52ec8,PowerKind::domino},{0x1b9bea8,PowerKind::mesmerize},{0x1b53488,PowerKind::windblast},{0x1b9b748,PowerKind::bendtime},
            {0x1b51c18,PowerKind::dark_vision},{0x1b9c248,PowerKind::shadow_walk},{0x1b51f28,PowerKind::devouring_swarm},{0x1b51828,PowerKind::clone},
            {t::power_swapper_vtable,PowerKind::displace},{t::power_theeye_vtable,PowerKind::foresight},{t::power_mimic_vtable,PowerKind::semblance}}};
        v.hook_mine_vtable=t::hook_mine_equipment_vtable;v.slash_vtable=t::slash_state_vtable;
        v.state_ids={t::state_choke,t::state_use_power,t::state_change_weapon,t::state_swapper,t::state_cancel_void_strike,t::state_mimicking,
            t::state_use_mimic,t::state_possession};
        return v;
    }
};
namespace power_observer_detail {
inline bool plausible(float v) {return std::isfinite(v) && std::fabs(v)<=1e6f;}
}
// machine==0 skips the player-state bits.
template<class Read> PowerSnapshot read_powers(Read& read,std::uintptr_t base,const PowerVtables& vt,std::uintptr_t machine=0) {
    using namespace rig;
    PowerSnapshot out;
    const auto game=value<std::uintptr_t>(read,base+0x39a5290);
    if(!game || !typed(read,base,*game,0x1c940a8)) return out;
    const auto player=value<std::uintptr_t>(read,*game+0x3130b0);
    if(!player || !typed(read,base,*player,0x1c5e428)) return out;
    out.found_player=true;
    if(machine) for(unsigned bit=0;bit<vt.state_ids.size();++bit) {
        if(vt.state_ids[bit]==0xffffffffu) continue;
        const auto active=native_player_state(read,base,machine,vt.state_ids[bit],0);
        if(!active) continue;
        out.states_known|=1u<<bit;
        if(*active) out.states|=1u<<bit;
    }
    if(machine) for(unsigned id=0;id<128;++id) if(const auto a=native_player_state(read,base,machine,id,0);a && *a) out.active_ids[id>>6]|=1ull<<(id&63u);
    if(machine && vt.slash_vtable) if(const auto slash=native_player_state_object(read,base,machine,39,vt.slash_vtable)) {
        out.voidstrike_found=true;
        out.voidstrike_owned=value<std::uint8_t>(read,slash+0xdb).value_or(0)==1;
        // 2026-11-25 (sim void_strike): slash+0xdb is only written when a slash starts, so the first charged strike of a session read "not
        // owned" and pressed 340 ms (no commit). The attribute itself, as the slash start reads it (0x8d56b6), says it before any slash.
        if constexpr(game_traits::void_strike_attr_index!=0) if(!out.voidstrike_owned) if(const auto attrs=value<std::uintptr_t>(read,machine+0x330);attrs && pointer(*attrs))
            if(const auto idx=value<std::int32_t>(read,*attrs+game_traits::void_strike_attr_index);idx && *idx>=0 && *idx<=4096)
                if(const auto entries=value<std::uintptr_t>(read,*attrs+game_traits::attribute_entries);entries && pointer(*entries))
                    if(const auto v=value<float>(read,*entries+static_cast<std::uintptr_t>(*idx)*game_traits::attribute_stride+game_traits::attribute_value);v && std::isfinite(*v) && *v>0.f)
                        out.voidstrike_owned=true;
        out.voidstrike_committed=value<std::uint8_t>(read,slash+0xda).value_or(0)==1;   // 0x8d6ab0: the held time passed decl+0xa0 (a Void Strike, not a heavy slash)
        const auto hold=value<float>(read,slash+0x38);
        out.slash_hold_s=hold && power_observer_detail::plausible(*hold)?*hold:0.f;
        if(const auto decl=value<std::uintptr_t>(read,slash+0x18); decl && pointer(*decl)) {
            const auto commit=value<float>(read,*decl+0xa0);
            out.voidstrike_commit_s=commit && power_observer_detail::plausible(*commit)?*commit:0.f;
            out.voidstrike_mana=value<std::int32_t>(read,*decl+0xf0).value_or(-1);
        }
    }
    std::vector<std::uintptr_t> seen;
    auto examine=[&](std::uintptr_t at) {
        if(!pointer(at) || std::find(seen.begin(),seen.end(),at)!=seen.end()) return;
        const auto table=value<std::uintptr_t>(read,at);
        if(!table) return;
        PowerKind kind=PowerKind::none;
        for(const auto& e:vt.entries) if(e.vtable && *table==base+e.vtable) {kind=e.kind;break;}
        if(kind==PowerKind::none) return;
        seen.push_back(at);out.present_mask|=1u<<static_cast<std::uint32_t>(kind);
        ++out.power_count;
        const auto aim=value<std::uint8_t>(read,at+0xa4),cast=value<std::uint8_t>(read,at+0xa5);
        const bool aiming=aim && *aim==1;
        if(aiming) {out.aim_active=true;if(!out.aiming_kind) out.aiming_kind=static_cast<std::uint32_t>(kind);}
        if(cast && *cast==1) out.cast_flag=true;
        if(kind==PowerKind::displace) {
            out.swapper_present=true;out.swapper_aiming=aiming;
            out.swapper_mode=value<std::uint8_t>(read,at+0x259).value_or(0);
            const auto sensor=value<std::uintptr_t>(read,at+0xc8);
            const auto target=sensor && pointer(*sensor)?value<std::array<float,3>>(read,*sensor+0x148):std::nullopt;
            if(target && power_observer_detail::plausible((*target)[0]) && power_observer_detail::plausible((*target)[1]) &&
               power_observer_detail::plausible((*target)[2])) {
                out.swapper_has_target=true;for(int i=0;i<3;++i) out.swapper_target[i]=(*target)[i];
            }
        } else if(kind==PowerKind::foresight) {
            out.theeye_state=value<std::int32_t>(read,at+0x19c).value_or(-1);
            if(const auto body=value<std::array<float,3>>(read,at+0x358)) for(int i=0;i<3;++i) out.theeye_body[i]=(*body)[i];
            out.theeye_body_yaw=value<float>(read,at+0x364).value_or(0.f);
        } else if(kind==PowerKind::semblance) {
            out.mimic_present=true;
            out.mimic_armed=value<std::uint8_t>(read,at+0x120).value_or(0)==1;
            // 2026-11-25: the disguise itself: the power's arkCpntMimic (+0xb8, init 0x3b5650) state +0x4c (2 = mimicking: 0x3b6240 drains while it is)
            if(const auto mc=value<std::uintptr_t>(read,at+0xb8);mc && pointer(*mc)) out.mimic_state=value<std::int32_t>(read,*mc+0x4c).value_or(-1);
        }
    };
    if(const auto components=list(read,*player+0x268,4096); components && components->packed_count) {
        std::vector<std::uintptr_t> addresses(components->packed_count);
        if(read(components->data,addresses.data(),addresses.size()*8)) for(const auto at:addresses) examine(at);
    }
    // Powers are inventory items, like the weapons (sim 2026-11-24, DOTO chapter 2: 0 power components on the player): the player state
    // machine's inventory (machine+0x328, DOTO +0x380 by the struct rule) lists records of 0x38 bytes whose +0x10 is the item's component
    // (NativeWeaponHands::test_equip reads them the same way).
    if(machine) if(const auto inventory=value<std::uintptr_t>(read,machine+0x328); inventory && *inventory && pointer(*inventory))
        if(const auto entries=list(read,*inventory+0x38,4096)) for(unsigned i=0;i<entries->packed_count && i<1024;++i)
            if(const auto comp=value<std::uintptr_t>(read,entries->data+i*0x38ull+0x10); comp && *comp) {
                examine(*comp);
                if(vt.hook_mine_vtable && pointer(*comp) && value<std::uintptr_t>(read,*comp).value_or(0)==base+vt.hook_mine_vtable) {
                    out.hook_mines=value<std::int32_t>(read,entries->data+i*0x38ull+0x20).value_or(-1);
                    // its lethal/non-lethal mode: the mine weapon entity's WeaponStates current index (0 non-lethal, 1 lethal), R7
                    if constexpr(game_traits::weapon_states_vtable!=0) if(const auto ent=value<std::uintptr_t>(read,*comp+0x10); ent && pointer(*ent))
                        if(const auto cl=list(read,*ent+game_traits::entity_components_offset,256)) for(unsigned k=0;k<cl->packed_count && k<256;++k)
                            if(const auto c=value<std::uintptr_t>(read,cl->data+k*8ull); c && pointer(*c) && value<std::uintptr_t>(read,*c).value_or(0)==base+game_traits::weapon_states_vtable) {
                                out.hook_mine_mode=value<std::int32_t>(read,*c+game_traits::weapon_state_current).value_or(-1);
                                out.hook_mine_pending=value<std::int32_t>(read,*c+game_traits::weapon_state_pending).value_or(-1);break;}
                }
            }
    return out;
}
// Test (sim, 2026-11-25: "d2vr voidstrike own|off"): DOTO's Void Strike attribute, as the slash start 0x8d56b6 reads it (game_traits
// void_strike_attr_index...): attributes object = machine+0x330 in D2 terms (the port's structs rule: DOTO +0x388), entry index at +0x124, entries
// of 0x28 bytes, float value +0x24, owned when > 0. On: the control thread keeps the value at 1 (the game may recompute it); off: the old value goes
// back. The slash reads it at its start (slash+0xdb, the observer's void_strike.owned), so ownership shows from the next sword strike.
struct VoidStrikeTest {
    inline static std::atomic<int> want{},index{-2};   // want: 0 untouched, 1 own, 2 restore; index: the attribute's (-2 not read)
    inline static std::atomic<std::uint64_t> writes{};inline static std::atomic<float> old_value{-1.f},value_now{-1.f};
    static bool write_float(std::uintptr_t a,float v) noexcept {__try {*reinterpret_cast<volatile float*>(a)=v;return true;} __except(EXCEPTION_EXECUTE_HANDLER) {return false;}}
    template<class Read> static void apply(Read& read,std::uintptr_t machine) noexcept {
        if constexpr(game_traits::void_strike_attr_index!=0) {
            const int w=want.load(std::memory_order_relaxed);if(!w || !machine) return;
            const auto attrs=rig::value<std::uintptr_t>(read,machine+0x330);if(!attrs || !rig::pointer(*attrs)) return;
            const auto idx=rig::value<std::int32_t>(read,*attrs+game_traits::void_strike_attr_index);if(!idx) return;
            index.store(*idx,std::memory_order_relaxed);if(*idx<0 || *idx>4096) return;
            const auto entries=rig::value<std::uintptr_t>(read,*attrs+game_traits::attribute_entries);if(!entries || !rig::pointer(*entries)) return;
            const auto at=*entries+static_cast<std::uintptr_t>(*idx)*game_traits::attribute_stride+game_traits::attribute_value;
            const auto cur=rig::value<float>(read,at);if(!cur || !std::isfinite(*cur)) return;
            value_now.store(*cur,std::memory_order_relaxed);
            if(w==1) {if(old_value.load(std::memory_order_relaxed)<0) old_value.store(*cur,std::memory_order_relaxed);
                if(*cur<=0.f && write_float(at,1.f)) {writes.fetch_add(1,std::memory_order_relaxed);value_now.store(1.f,std::memory_order_relaxed);}}
            else {const float o=old_value.load(std::memory_order_relaxed);if(o>=0 && write_float(at,o)) writes.fetch_add(1,std::memory_order_relaxed);
                old_value.store(-1.f,std::memory_order_relaxed);want.store(0,std::memory_order_relaxed);}
        } else {(void)read;(void)machine;}
    }
    static std::string json() {
        std::ostringstream o;o<<"{\"want\":"<<want.load()<<",\"index\":"<<index.load()<<",\"writes\":"<<writes.load()<<",\"old\":"<<old_value.load()<<",\"now\":"<<value_now.load()<<'}';return o.str();
    }
};

class NativePowerObserver {
public:
    static void publish(const PowerSnapshot& s) {
        std::lock_guard<std::mutex> l(mutex());
        const auto rising=s.states&~snapshot().states;   // per state bit: entries seen at the 50 ms reads (a state shorter than that may be missed)
        for(unsigned b=0;b<edges_.size();++b) if(rising>>b&1u) {edges_[b].fetch_add(1,std::memory_order_relaxed);note(1,b);}
        if(s.found_player && snapshot().found_player && s.hook_mines!=snapshot().hook_mines) note(2,static_cast<std::uint32_t>(s.hook_mines));
        if(s.found_player && snapshot().found_player && s.hook_mine_mode!=snapshot().hook_mine_mode) note(3,static_cast<std::uint32_t>(s.hook_mine_mode));
        if(s.found_player && snapshot().found_player && s.theeye_state!=snapshot().theeye_state) note(4,static_cast<std::uint32_t>(s.theeye_state));
        if(s.mimic_state==2) mimic_ms_.fetch_add(50,std::memory_order_relaxed);   // the observer reads every ~50 ms
        if(s.mimic_state!=snapshot().mimic_state && s.found_player && snapshot().found_player) note(7,static_cast<std::uint32_t>(s.mimic_state));   // kind 7: the disguise state changed
        if(s.voidstrike_committed && !snapshot().voidstrike_committed) {void_commits_.fetch_add(1,std::memory_order_relaxed);note(6,1);}   // kind 6: a Void Strike committed
        if(s.found_player && snapshot().found_player) for(unsigned w=0;w<2;++w) if(const auto d=s.active_ids[w]^snapshot().active_ids[w]) for(unsigned b=0;b<64;++b) if(d>>b&1u)
            note(5,(w*64+b)|((s.active_ids[w]>>b&1u)?0x1000u:0u));   // any player state on (id | 0x1000) / off (id)
        if(s.swapper_aiming && s.swapper_has_target) {   // where the Displace marker sits while aiming (compare with native-left-aim.csv at that tick)
            aim_target_={static_cast<double>(GetTickCount64()),s.swapper_target[0],s.swapper_target[1],s.swapper_target[2]};++aim_target_samples_;
        }
        snapshot()=s;reads_.fetch_add(1,std::memory_order_relaxed);
        if(!s.found_player) failures_.fetch_add(1,std::memory_order_relaxed);
    }
    // A charged-release press whose length came from the Void Strike commit time (attack_intent.hpp void_strike_press_ms).
    static void note_void_strike_press(int ms) {void_presses_.fetch_add(1,std::memory_order_relaxed);void_last_ms_.store(ms,std::memory_order_relaxed);}
    static PowerSnapshot latest() {std::lock_guard<std::mutex> l(mutex());return snapshot();}
    static std::string status_json() {
        const auto s=latest();
        std::ostringstream o;
        o<<"{\"reads\":"<<reads_.load()<<",\"failures\":"<<failures_.load()<<",\"found_player\":"<<(s.found_player?"true":"false")
         <<",\"power_count\":"<<s.power_count<<",\"present\":["<<[&]{std::string o;for(std::uint32_t k=1;k<static_cast<std::uint32_t>(PowerKind::count_);++k) if(s.present_mask>>k&1u) {if(!o.empty()) o+=',';o+='"';o+=power_kind_name(k);o+='"';}return o;}()<<"]"<<",\"aiming\":\""<<power_kind_name(s.aiming_kind)<<"\",\"aim_active\":"<<(s.aim_active?"true":"false")
         <<",\"cast_flag\":"<<(s.cast_flag?"true":"false")<<",\"swapper\":{\"present\":"<<(s.swapper_present?"true":"false")
         <<",\"aiming\":"<<(s.swapper_aiming?"true":"false")<<",\"mode\":"<<static_cast<unsigned>(s.swapper_mode)
         <<",\"has_target\":"<<(s.swapper_has_target?"true":"false")<<",\"target\":["<<s.swapper_target[0]<<","<<s.swapper_target[1]<<","<<s.swapper_target[2]
         <<"]},\"theeye\":{\"state\":"<<s.theeye_state<<",\"body\":["<<s.theeye_body[0]<<","<<s.theeye_body[1]<<","<<s.theeye_body[2]
         <<"],\"yaw\":"<<s.theeye_body_yaw<<"},\"mimic\":{\"present\":"<<(s.mimic_present?"true":"false")<<",\"armed\":"<<(s.mimic_armed?"true":"false")<<",\"state\":"<<s.mimic_state<<",\"disguised_ms\":"<<mimic_ms_.load()
         <<"},\"states\":"<<s.states<<",\"states_known\":"<<s.states_known<<",\"hook_mines\":"<<s.hook_mines<<",\"hook_mine_mode\":"<<s.hook_mine_mode<<",\"hook_mine_pending\":"<<s.hook_mine_pending
         <<",\"state_entries\":{\"choke\":"<<edges_[0].load()<<",\"use_power\":"<<edges_[1].load()<<",\"change_weapon\":"<<edges_[2].load()
         <<",\"swapper\":"<<edges_[3].load()<<",\"cancel_void_strike\":"<<edges_[4].load()<<",\"mimicking\":"<<edges_[5].load()
         <<",\"use_mimic\":"<<edges_[6].load()<<",\"possession\":"<<edges_[7].load()<<"},\"events\":"<<events_json()
         <<",\"active_ids\":["<<[&]{std::string o;for(unsigned id=0;id<128;++id) if(s.active_ids[id>>6]>>(id&63u)&1u) {if(!o.empty()) o+=',';o+=std::to_string(id);}return o;}()<<"]"
         <<",\"swapper_aim\":"<<[]{std::lock_guard<std::mutex> l(mutex());std::ostringstream a;a<<std::setprecision(10)<<"{\"samples\":"<<aim_target_samples_
            <<",\"last\":["<<aim_target_[0]<<','<<aim_target_[1]<<','<<aim_target_[2]<<','<<aim_target_[3]<<"]}";return a.str();}()
         <<",\"void_strike\":{\"found\":"<<(s.voidstrike_found?"true":"false")<<",\"owned\":"<<(s.voidstrike_owned?"true":"false")
         <<",\"commit_s\":"<<s.voidstrike_commit_s<<",\"mana\":"<<s.voidstrike_mana<<",\"hold_s\":"<<s.slash_hold_s
         <<",\"presses\":"<<void_presses_.load()<<",\"last_press_ms\":"<<void_last_ms_.load()<<",\"commits\":"<<void_commits_.load()<<"}}";
        return o.str();
    }
private:
    // The last 64 changes, for timing questions (which press entered a state, which one used a mine): [tick ms, kind, value];
    // kind 1 = a state entered (value = state bit), 2 = the Hook Mine count changed (value = the new count), 3 = the Hook Mine mode changed, 4 = Foresight's state changed (value = the new state), 5 = any player state on (value = id | 0x1000) or off (id). Called under mutex().
    struct Event {std::uint64_t tick;std::uint32_t kind,value;};
    static constexpr std::uint32_t ring_size=64;
    static std::array<Event,ring_size>& ring() {static std::array<Event,ring_size> r{};return r;}
    inline static std::uint32_t ring_n_{};
    static void note(std::uint32_t kind,std::uint32_t value) {ring()[ring_n_++%ring_size]={GetTickCount64(),kind,value};}
    static std::string events_json() {
        std::lock_guard<std::mutex> l(mutex());std::ostringstream o;o<<'[';
        const std::uint32_t n=ring_n_<ring_size?ring_n_:ring_size;
        for(std::uint32_t i=0;i<n;++i) {const auto& e=ring()[(ring_n_-n+i)%ring_size];o<<(i?",":"")<<'['<<e.tick<<','<<e.kind<<','<<static_cast<std::int32_t>(e.value)<<']';}
        o<<']';return o.str();
    }
    static std::mutex& mutex() {static std::mutex m;return m;}
    static PowerSnapshot& snapshot() {static PowerSnapshot s;return s;}
    inline static std::atomic<std::uint64_t> reads_{},failures_{};
    inline static std::array<std::atomic<std::uint64_t>,8> edges_{};
    inline static std::array<double,4> aim_target_{};inline static std::uint64_t aim_target_samples_{};   // under mutex(): [tick, x, y, z]
    inline static std::atomic<std::uint64_t> void_presses_{},void_commits_{},mimic_ms_{};
    inline static std::atomic<int> void_last_ms_{};
};
} // namespace d2vr
