#pragma once
// Read-only observer of the player's power components and the player states around them (plan 2026-11-24-doto-billie-vr Task 3;
// docs/doto-research/2026-11-24-R2-powers.md section 2). Every read goes through the guarded Read: a bad pointer yields defaults.
#include "game_profile.hpp"
#include "game_rig.hpp"
#include "native_takedown.hpp"
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
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
    std::uint32_t aiming_kind{};          // PowerKind of the first power whose aim byte (+0xa4) is set
    bool aim_active{},cast_flag{};        // any power: +0xa4, +0xa5
    bool swapper_present{},swapper_aiming{};
    std::uint8_t swapper_mode{};          // +0x259
    bool swapper_has_target{};
    float swapper_target[3]{};
    int theeye_state{-1};                 // +0x19c, -1 absent
    float theeye_body[3]{};float theeye_body_yaw{};
    bool mimic_present{},mimic_armed{};   // +0x120 byte
    std::uint32_t states{};               // power_state_bit::*, active flags
    std::uint32_t states_known{};         // bits whose state read succeeded (the id exists in the pool)
};
// Per-game table: D2 vtables are D2 literals (tools/doto/port.py maps them by RTTI name); DOTO-only classes and the state ids come from game_traits.
struct PowerVtables {
    struct Entry {std::uintptr_t vtable;PowerKind kind;};
    std::array<Entry,15> entries{};
    std::array<std::uint32_t,8> state_ids{};   // order of power_state_bit
    static PowerVtables current() {
        namespace t=game_traits;
        PowerVtables v;
        v.entries={{{0x1b9bb78,PowerKind::blink},{0x1b52818,PowerKind::far_reach},{0x1b52258,PowerKind::doppelganger},{0x1b531c8,PowerKind::possession},
            {0x1b52ec8,PowerKind::domino},{0x1b9bea8,PowerKind::mesmerize},{0x1b53488,PowerKind::windblast},{0x1b9b748,PowerKind::bendtime},
            {0x1b51c18,PowerKind::dark_vision},{0x1b9c248,PowerKind::shadow_walk},{0x1b51f28,PowerKind::devouring_swarm},{0x1b51828,PowerKind::clone},
            {t::power_swapper_vtable,PowerKind::displace},{t::power_theeye_vtable,PowerKind::foresight},{t::power_mimic_vtable,PowerKind::semblance}}};
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
    const auto components=list(read,*player+0x268,4096);
    if(!components || !components->packed_count) return out;
    std::vector<std::uintptr_t> addresses(components->packed_count);
    if(!read(components->data,addresses.data(),addresses.size()*8)) return out;
    for(const auto at:addresses) {
        if(!pointer(at)) continue;
        const auto table=value<std::uintptr_t>(read,at);
        if(!table) continue;
        PowerKind kind=PowerKind::none;
        for(const auto& e:vt.entries) if(e.vtable && *table==base+e.vtable) {kind=e.kind;break;}
        if(kind==PowerKind::none) continue;
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
        }
    }
    return out;
}
class NativePowerObserver {
public:
    static void publish(const PowerSnapshot& s) {
        std::lock_guard<std::mutex> l(mutex());
        snapshot()=s;reads_.fetch_add(1,std::memory_order_relaxed);
        if(!s.found_player) failures_.fetch_add(1,std::memory_order_relaxed);
    }
    static PowerSnapshot latest() {std::lock_guard<std::mutex> l(mutex());return snapshot();}
    static std::string status_json() {
        const auto s=latest();
        std::ostringstream o;
        o<<"{\"reads\":"<<reads_.load()<<",\"failures\":"<<failures_.load()<<",\"found_player\":"<<(s.found_player?"true":"false")
         <<",\"power_count\":"<<s.power_count<<",\"aiming\":\""<<power_kind_name(s.aiming_kind)<<"\",\"aim_active\":"<<(s.aim_active?"true":"false")
         <<",\"cast_flag\":"<<(s.cast_flag?"true":"false")<<",\"swapper\":{\"present\":"<<(s.swapper_present?"true":"false")
         <<",\"aiming\":"<<(s.swapper_aiming?"true":"false")<<",\"mode\":"<<static_cast<unsigned>(s.swapper_mode)
         <<",\"has_target\":"<<(s.swapper_has_target?"true":"false")<<",\"target\":["<<s.swapper_target[0]<<","<<s.swapper_target[1]<<","<<s.swapper_target[2]
         <<"]},\"theeye\":{\"state\":"<<s.theeye_state<<",\"body\":["<<s.theeye_body[0]<<","<<s.theeye_body[1]<<","<<s.theeye_body[2]
         <<"],\"yaw\":"<<s.theeye_body_yaw<<"},\"mimic\":{\"present\":"<<(s.mimic_present?"true":"false")<<",\"armed\":"<<(s.mimic_armed?"true":"false")
         <<"},\"states\":"<<s.states<<",\"states_known\":"<<s.states_known<<"}";
        return o.str();
    }
private:
    static std::mutex& mutex() {static std::mutex m;return m;}
    static PowerSnapshot& snapshot() {static PowerSnapshot s;return s;}
    inline static std::atomic<std::uint64_t> reads_{},failures_{};
};
} // namespace d2vr
