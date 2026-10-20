#pragma once
#include "game_rig.hpp"
#include <cstdint>
#include <array>
#include <optional>
#include <vector>

namespace d2vr {
// Read-only: which stealth takedowns the game offers right now, from the player's own sensor
// components (artifacts/takedown-re; Steam build b671f824...). Each sensor writes its current
// target spawn id every frame (0x3fff0010 = none) after its own distance/angle/awareness gates:
//   arkCpntPlayerSensorChoke             vtable 0x1b3e158, target id +0x50
//   arkCpntPlayerSensorAssassination     vtable 0x1b3cc38, target id +0x40 (+0x48 byte must be 0)
//   arkCpntPlayerSensorDropAssassination vtable 0x1b3f0a8, candidate list +0x70 (count low 24 bits of +0x78)
// Common base: +0x10 owner (the player), +0x20 player state machine (vtable 0x1c73d58), +0x38 enabled.
// Choke is a native BLOCK (RB) hold; lethal and drop kills are a MELEE (RT) press [inferred].
struct TakedownState {bool choke{},kill{},drop{};std::uint32_t spawn{};
    bool offered{}; // GlobalAssassination +0x1d0 (the rule handler that passed, 0 = nothing offered): the exact 3 m / 1.7 m / unaware verdict, steady where the sensor target flickers // target spawn id (kill, else choke, else first drop candidate); 0 = none
    std::uint32_t kill_spawn{},drop_spawn{};
    bool any() const noexcept {return choke || kill || drop;}};
// Finds the three sensors (a component walk, done rarely); poll() then reads only their fields.
class NativeTakedowns {
    std::uintptr_t player_{},global_{};std::array<std::uintptr_t,3> sensor_{};std::uint64_t next_scan_{};
    static constexpr std::array<std::uintptr_t,3> vtables{0x1b3e158,0x1b3cc38,0x1b3f0a8};
    static constexpr std::uint32_t none=0x3fff0010u;
    template<class Read> bool sensor_valid(Read& read,std::uintptr_t base,std::uintptr_t c,unsigned kind) const {
        using namespace rig;
        if(!c || value<std::uintptr_t>(read,c)!=base+vtables[kind] || value<std::uintptr_t>(read,c+0x10)!=player_) return false;
        const auto machine=value<std::uintptr_t>(read,c+0x20);
        return machine && typed(read,base,*machine,0x1c73d58);
    }
    template<class Read> void scan(Read& read,std::uintptr_t base) {
        using namespace rig;
        player_=0;global_=0;sensor_={};
        auto game=value<std::uintptr_t>(read,base+0x39a5290);
        if(!game || !typed(read,base,*game,0x1c940a8)) return;
        auto player=value<std::uintptr_t>(read,*game+0x3130b0);
        if(!player || !typed(read,base,*player,0x1c5e428)) return;
        auto components=list(read,*player+0x268,4096);
        if(!components || !components->packed_count) return;
        std::vector<std::uintptr_t> addresses(components->packed_count);
        if(!read(components->data,addresses.data(),addresses.size()*8)) return;
        player_=*player;global_=0;
        for(const auto c:addresses) for(unsigned k=0;k<3;++k) if(!sensor_[k] && sensor_valid(read,base,c,k)) sensor_[k]=c;
        for(const auto c:addresses) if(value<std::uintptr_t>(read,c)==base+0x1b3f998 && value<std::uintptr_t>(read,c+0x10)==player_) {global_=c;break;} // GlobalAssassination
    }
public:
    std::uintptr_t choke_sensor() const noexcept {return sensor_[0];}
    std::uintptr_t global_assassination() const noexcept {return global_;}
    template<class Read> TakedownState poll(Read& read,std::uintptr_t base,std::uint64_t now) {
        using namespace rig;
        TakedownState out;
        bool stale=false;
        for(unsigned k=0;k<3;++k) if(sensor_[k] && !sensor_valid(read,base,sensor_[k],k)) stale=true;
        if(player_) { // a new player (level change) while the old components still look valid
            const auto game=value<std::uintptr_t>(read,base+0x39a5290);
            if(!game || value<std::uintptr_t>(read,*game+0x3130b0)!=player_) stale=true;
        }
        if(stale || (!sensor_[0] && !sensor_[1] && !sensor_[2] && now>=next_scan_)) {scan(read,base);next_scan_=now+2000;}
        auto valid_id=[](std::optional<std::uint32_t> id) {return id && *id!=none && *id!=0xffffffffu;};
        auto enabled=[&](std::uintptr_t c) {return value<std::uint8_t>(read,c+0x38).value_or(0)!=0;};
        std::uint32_t choke_id{},kill_id{},drop_id{};
        if(const auto c=sensor_[0]; c && enabled(c)) {const auto target=value<std::uint32_t>(read,c+0x50);out.choke=valid_id(target);if(out.choke)choke_id=*target;}
        if(const auto c=sensor_[1]; c && enabled(c)) {const auto target=value<std::uint32_t>(read,c+0x40);out.kill=valid_id(target) && value<std::uint8_t>(read,c+0x48).value_or(1)==0;if(out.kill)kill_id=*target;}
        if(const auto c=sensor_[2]; c && enabled(c)) {
            const auto count=value<std::uint32_t>(read,c+0x78).value_or(0)&0xffffff;
            const auto data=value<std::uintptr_t>(read,c+0x70);
            const auto target=count>0 && data && pointer(*data)?value<std::uint32_t>(read,*data):std::nullopt;
            out.drop=valid_id(target);
            if(out.drop)drop_id=*target;
        }
        if(global_ && value<std::uintptr_t>(read,global_)==base+0x1b3f998 && value<std::uintptr_t>(read,global_+0x10)==player_)
            out.offered=value<std::uintptr_t>(read,global_+0x1d0).value_or(0)!=0;
        out.spawn=out.kill?kill_id:out.choke?choke_id:out.drop?drop_id:0;
        out.kill_spawn=kill_id;out.drop_spawn=drop_id;
        return out;
    }
    unsigned sensors() const noexcept {return (sensor_[0]?1u:0u)|(sensor_[1]?2u:0u)|(sensor_[2]?4u:0u);}
    // The player state machine the sensors point at (+0x20), for native_crouch().
    template<class Read> std::uintptr_t machine(Read& read) const {
        for(const auto c:sensor_) if(c) if(const auto m=rig::value<std::uintptr_t>(read,c+0x20)) return *m;
        return 0;
    }
};
// World position of a takedown target (entity+0x300 floats, native units; the same field
// tools/inspect_*_actors.py read). The entity origin is at the feet: callers add head height.
struct TakedownTarget {std::uintptr_t entity{};std::uint32_t spawn{};std::array<float,3> origin{};};
template<class Read> std::optional<TakedownTarget> takedown_target(Read& read,std::uintptr_t base,std::uint32_t spawn) {
    using namespace rig;
    if(!spawn || spawn==0x3fff0010u || spawn==0xffffffffu || (spawn>>16)>=0x3fff) return {};
    const auto game=value<std::uintptr_t>(read,base+0x39a5290);
    if(!game || !typed(read,base,*game,0x1c940a8)) return {};
    const auto entity=value<std::uintptr_t>(read,*game+0x3130b0+(spawn>>16)*8);
    if(!entity || !pointer(*entity) || value<std::uint32_t>(read,*entity+0xa8)!=spawn) return {};
    const auto at=value<std::array<float,3>>(read,*entity+0x300);
    if(!at) return {};
    for(const auto v:*at) if(!std::isfinite(v)) return {};
    if(value<std::uintptr_t>(read,*game+0x3130b0+(spawn>>16)*8)!=entity) return {};
    return TakedownTarget{*entity,spawn,*at};
}
// Native crouch (artifacts/crouch-re): player state 6 arkPlayerStateSneaking (vtable 0x1c84258) is the
// crouch; 10 AUTOCROUCH and 11 WALLCROUCH are forced crouches (standing up impossible). The state pool
// is machine+0x90, state N at *(pool+0x10+N*8): +8 id, +0x10 machine, +0x20 active byte.
struct CrouchState {bool valid{},crouched{},forced{};};
template<class Read> CrouchState native_crouch(Read& read,std::uintptr_t base,std::uintptr_t machine) {
    using namespace rig;
    CrouchState out;
    if(!pointer(machine) || value<std::uintptr_t>(read,machine)!=base+0x1c73d58) return out;
    const auto pool=value<std::uintptr_t>(read,machine+0x90);
    if(!pool || !pointer(*pool)) return out;
    auto state=[&](unsigned id,std::uintptr_t vtable)->std::optional<bool> {
        const auto s=value<std::uintptr_t>(read,*pool+0x10+id*8);
        if(!s || !pointer(*s) || value<std::uint32_t>(read,*s+8)!=id || value<std::uintptr_t>(read,*s+0x10)!=machine) return {};
        if(vtable && value<std::uintptr_t>(read,*s)!=base+vtable) return {};
        const auto active=value<std::uint8_t>(read,*s+0x20);
        if(!active || *active>1) return {};
        return *active==1;
    };
    const auto sneaking=state(6,0x1c84258);
    if(!sneaking) return out;
    const bool auto_crouch=state(10,0).value_or(false),wall_crouch=state(11,0).value_or(false);
    out.valid=true;out.forced=auto_crouch || wall_crouch;out.crouched=*sneaking || out.forced;
    return out;
}
} // namespace d2vr
