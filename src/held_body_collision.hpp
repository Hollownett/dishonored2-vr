#pragma once
// The rule (user, 2026-11-05): "object which is hold never collides with the hand and player body".
//
// The engine's own hold keeps the item away from the player by putting it on the DEBRIS layer while it is held (native_held_object.hpp: a throwable on
// RIGID_BODY "collides with it there", so it takes DEBRIS for the hold and gets its word back afterwards). A motor-held prop (the physical grab) stays on
// RIGID_BODY, so it shoves the player's capsule and fights the hand's body: the hand body strips the held layer from its own mask (HandFilterKeeper), but
// nothing strips the *player* from the prop's.
//
// This does for a physical hold exactly what the engine does for a native one: at the game thread, once per hold, the held body's filter word is written with
// the DEBRIS layer (its own mask kept) through the mod's already verified writer (NativeHeldObject::set_filter -> 0x97600 with the busy pre-read), and the
// exact word it had is written back when the hold ends - only if the body is still that body and still carries the layer we wrote, so a respawned or
// engine-rewritten body is never clobbered.
//
// Cost: one 4-byte read per held hand per frame; the writes happen once per hold (and only again if the engine put the old layer back).
#include "native_held_object.hpp"
#include "native_rigid_body.hpp"
#include <cstdint>

namespace d2vr {
class HeldBodyCollision {
public:
    static constexpr std::uintptr_t filter_offset=0x6c;   // the pool record's filter word (native_held_object.hpp: layer in the low 5 bits)
    struct Entry {
        std::uintptr_t entity{},record{};
        std::uint32_t handle{},saved{},entity_id{};std::int32_t generation{};
        bool active{};
    };
    struct Counters {std::uint64_t swapped{},restored{},refused{},skipped{},reasserted{};};
    // `body`: what this hand holds this frame (NativeGrab::held). A corpse limb holds no prop filter here: the caller keeps its own rule for those.
    template<class Read> bool hold(unsigned side,std::uintptr_t base,Read& read,const NativeRigidBody& body) {
        if(side>=2 || !body.record || !body.handle) return false;
        auto& e=entry_[side];
        if(e.active && e.entity==body.entity && e.handle==body.handle && e.generation==body.identity.generation) {
            // the engine wrote its old layer back (a re-drop, a state change): put ours again, once
            const auto filter=rig::value<std::uint32_t>(read,body.record+filter_offset);
            if(filter && (*filter&0x1fu)!=NativeHeldObject::layer_debris && NativeHeldObject::write_filter(base,read,body.handle,(*filter&~0x1fu)|NativeHeldObject::layer_debris)) ++counters_.reasserted;
            return filter.has_value();
        }
        if(e.active) release(side,base,read);   // another object took the hand: the old one gets its word back first
        const auto filter=rig::value<std::uint32_t>(read,body.record+filter_offset);
        if(!filter) {++counters_.refused;return false;}
        Entry next;next.entity=body.entity;next.record=body.record;next.handle=body.handle;next.entity_id=body.entity_id;
        next.saved=*filter;next.generation=body.identity.generation;
        if(!NativeHeldObject::write_filter(base,read,body.handle,(*filter&~0x1fu)|NativeHeldObject::layer_debris)) {++counters_.refused;return false;}
        next.active=true;e=next;++counters_.swapped;
        return true;
    }
    // The hold ended (or the hand is empty, or tracking went): give the body back the exact word it had, once, if it is still that body.
    template<class Read> bool release(unsigned side,std::uintptr_t base,Read& read) {
        if(side>=2) return false;
        auto& e=entry_[side];if(!e.active) return false;
        const auto first=rig::value<std::uint32_t>(read,e.record+0x70),second=rig::value<std::uint32_t>(read,e.record+0x74);
        const auto filter=rig::value<std::uint32_t>(read,e.record+filter_offset);
        bool written=false;
        if(first && second && *first==e.handle && *second==e.handle && filter && (*filter&0x1fu)==NativeHeldObject::layer_debris)
            written=NativeHeldObject::write_filter(base,read,e.handle,e.saved);
        if(written) ++counters_.restored;else ++counters_.skipped;   // never clobber a body the engine changed or that went away
        e=Entry{};
        return written;
    }
    bool active(unsigned side) const noexcept {return side<2 && entry_[side].active;}
    const Entry& entry(unsigned side) const noexcept {static const Entry none{};return side<2?entry_[side]:none;}
    const Counters& counters() const noexcept {return counters_;}
private:
    std::array<Entry,2> entry_{};
    Counters counters_{};
};
} // namespace d2vr
