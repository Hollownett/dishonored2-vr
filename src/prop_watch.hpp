#pragma once
// The loose props near the hands, by entity: the bounds, axes and origin each had a moment ago. A native pickup flies the prop to the camera bind within a frame
// or two, so by the hold's first frame it no longer lies where the hand pressed it; the finger touch (finger_ik.hpp held_touch) is measured on where it was.
// The interaction aim caches only the one prop it targets (sim v528: the bottle beside the glass the hand took), so this is a cache of every prop in reach, found
// the way native_hand_contact.hpp finds them (the entity registry game+0x3130b0, a slice a frame; prop classes by vtable: three movable classes) and keyed by the
// entity id the game's HoldObject names. Render thread, once a frame; no game writes.
// An entry is read when a prop first comes within `reach` of a hand and again every refresh_ms while it stays. A prop that jumps more than `jump` between two reads
// closer than jump_window_ms apart is being flown (picked up, thrown): its entry freezes at the pose before the jump. The held prop is never read again.
#include "game_rig.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

namespace d2vr {
class PropWatch {
public:
    static constexpr unsigned registry_size=0x3fff,slice_size=256,read_budget=96,max_entries=16;
    static constexpr double reach=.9,jump=.25;
    static constexpr std::uint64_t refresh_ms=100,jump_window_ms=400,forget_ms=3000;
    struct Entry {
        std::uintptr_t entity{};
        std::array<float,8> box{};std::array<float,9> axes{};std::array<float,3> origin{},position{}; // the model's world box (+0x1840), axes (+0x186c), origin (+0x1860); entity +0x300
        std::uint64_t tick{};      // when the entry was last read
        std::uint64_t near_tick{}; // when a hand was last within reach of it
        bool frozen{};
    };
    // hands: a tracked hand's world position (empty: no such hand). held: the entity a hand holds now (never read).
    template<class Read> void step(Read& read,std::uintptr_t base,std::uintptr_t game,const std::array<std::optional<std::array<float,3>>,2>& hands,std::uint64_t now,std::uintptr_t held=0) {
        reads_=0;
        if(!rig::pointer(game)) return;
        if(game!=game_ || slot_entity_.size()!=registry_size) {game_=game;slot_entity_.assign(registry_size,0);slot_prop_.assign(registry_size,0);entries_.clear();cursor_=1;}
        if(!hands[0] && !hands[1]) return;
        const auto near_hand=[&](const std::array<float,3>& p) {
            double best=1e9;
            for(const auto& h:hands) if(h) best=std::min(best,std::hypot(std::hypot(static_cast<double>(p[0]-(*h)[0]),static_cast<double>(p[1]-(*h)[1])),static_cast<double>(p[2]-(*h)[2])));
            return best;
        };
        // 1. one slice of the registry: entities of a prop class within reach of a hand are looked at (a new one is read at once)
        std::array<std::uintptr_t,slice_size> slice{};
        const unsigned count=std::min<unsigned>(slice_size,registry_size-cursor_);
        unsigned budget=read_budget,done=0;
        ++reads_;
        if(read(game+0x3130b0+static_cast<std::uintptr_t>(cursor_)*8,slice.data(),count*sizeof(std::uintptr_t))) {
            for(;done<count && budget>=2;++done) {
                const unsigned slot=cursor_+done;const auto entity=slice[done];
                if(!rig::pointer(entity)) {slot_entity_[slot]=0;slot_prop_[slot]=0;continue;}
                if(slot_entity_[slot]!=entity) {
                    --budget;++reads_;slot_entity_[slot]=entity;
                    const auto vtable=rig::value<std::uintptr_t>(read,entity);
                    slot_prop_[slot]=vtable && (*vtable==base+0x1c886a8 || *vtable==base+0x1c87d58 || *vtable==base+0x1c5de18);
                }
                if(!slot_prop_[slot] || entity==held) continue;
                --budget;++reads_;
                const auto position=rig::value<std::array<float,3>>(read,entity+0x300);
                if(!position || near_hand(*position)>reach) continue;
                auto* entry=find_entry(entity);
                if(entry) {entry->near_tick=now;continue;}
                if(entries_.size()>=max_entries) { // the one the hands left longest ago goes
                    auto oldest=entries_.begin();for(auto it=entries_.begin();it!=entries_.end();++it) if(it->near_tick<oldest->near_tick) oldest=it;
                    if(oldest->near_tick+refresh_ms>now) continue;
                    entries_.erase(oldest);
                }
                Entry fresh{};fresh.entity=entity;fresh.near_tick=now;
                if(read_entry(read,fresh,now)) entries_.push_back(fresh);
            }
        }
        cursor_+=done;if(cursor_>=registry_size) cursor_=1;
        // 2. the entries: forgotten when no hand has been near them for a while; the two read longest ago are read again
        entries_.erase(std::remove_if(entries_.begin(),entries_.end(),[&](const Entry& e) {return now>e.near_tick && now-e.near_tick>forget_ms;}),entries_.end());
        for(unsigned refreshed=0;refreshed<2;++refreshed) {
            Entry* due=nullptr;
            for(auto& e:entries_) if(!e.frozen && e.entity!=held && now>=e.tick && now-e.tick>=refresh_ms && (!due || e.tick<due->tick)) due=&e;
            if(!due) break;
            Entry again=*due;
            if(!read_entry(read,again,now)) {due->tick=now;continue;} // unreadable this time: try again later, the old pose stays
            const double moved=std::hypot(std::hypot(static_cast<double>(again.position[0]-due->position[0]),static_cast<double>(again.position[1]-due->position[1])),static_cast<double>(again.position[2]-due->position[2]));
            if(moved>jump && now-due->tick<=jump_window_ms) {due->frozen=true;due->tick=now;continue;} // flown: the entry stays at the pose before the jump
            again.near_tick=due->near_tick;if(near_hand(again.position)<=reach) again.near_tick=now;
            *due=again;
        }
    }
    // The entry of `entity` read within `window` ms (a frozen one counts: its pose is the last before the pickup).
    std::optional<Entry> find(std::uintptr_t entity,std::uint64_t now,std::uint64_t window=1500) const {
        for(const auto& e:entries_) if(e.entity==entity && now>=e.tick && now-e.tick<=window) return e;
        return {};
    }
    // Reads the last step made (the cost: one registry block, a vtable per new entity, a position per prop-class entity, a few per refresh).
    unsigned reads() const {return reads_;}
    std::size_t size() const {return entries_.size();}
private:
    Entry* find_entry(std::uintptr_t entity) {for(auto& e:entries_) if(e.entity==entity) return &e;return nullptr;}
    template<class Read> bool read_entry(Read& read,Entry& e,std::uint64_t now) {
        const auto model=rig::value<std::uintptr_t>(read,e.entity+0xf0);reads_+=1;
        if(!model || !rig::pointer(*model)) return false;
        const auto box=rig::value<std::array<float,8>>(read,*model+0x1840);
        const auto axes=rig::value<std::array<float,9>>(read,*model+0x186c);
        const auto origin=rig::value<std::array<float,3>>(read,*model+0x1860);
        const auto position=rig::value<std::array<float,3>>(read,e.entity+0x300);
        reads_+=4;
        if(!box || !axes || !origin || !position) return false;
        for(unsigned i:{0u,1u,2u,4u,5u,6u}) if(!std::isfinite((*box)[i])) return false;
        if(!((*box)[4]>(*box)[0] && (*box)[5]>(*box)[1] && (*box)[6]>(*box)[2])) return false;
        const double size=std::hypot(std::hypot(static_cast<double>((*box)[4]-(*box)[0]),static_cast<double>((*box)[5]-(*box)[1])),static_cast<double>((*box)[6]-(*box)[2]));
        if(size>3) return false; // not a prop (the registry's tables and walls have no such box here)
        for(float v:*axes) if(!std::isfinite(v)) return false;
        for(float v:*origin) if(!std::isfinite(v)) return false;
        e.box=*box;e.axes=*axes;e.origin=*origin;e.position=*position;e.tick=now;
        return true;
    }
    std::uintptr_t game_{};unsigned cursor_{1},reads_{};
    std::vector<std::uintptr_t> slot_entity_;std::vector<std::uint8_t> slot_prop_;
    std::vector<Entry> entries_;
};
} // namespace d2vr
