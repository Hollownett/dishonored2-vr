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
#include <array>
#include <cstdint>
#include <optional>

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

// v1145 (user, headset v1144: "bodies pretty often get stuck in the player's body, fix this collision"): a held CORPSE gets the same rule. Its bones stayed on
// NPC_BODY_RAGDOLL (14), which the player's query-source mask accepts (docs/native-hand-body-re.md: 0x0041c62b), and the player is never moved by a body
// pushed into him (0.000 m), so a dragged body sat in the capsule. While a hand holds any bone, EVERY bone of that body takes DEBRIS (the engine's own held
// layer); when the hold ends each bone gets its exact word back, but only once no bone is within reach of the player's feet (near(), the caller's test) or
// after release_grace_ms - a body let go inside the player falls out of him instead of locking there. Same writer, same never-clobber checks as a prop.
class CorpseCollision {
public:
    static constexpr std::uint64_t release_grace_ms=2000;
    using Entry=HeldBodyCollision::Entry;
    // bodies: the held body's ragdoll bones this frame (native_ragdoll_body 0..16, missing ones empty).
    template<class Read,std::size_t N> void hold(std::uintptr_t base,Read& read,std::uintptr_t entity,const std::array<std::optional<NativeRigidBody>,N>& bodies) {
        if(active_ && entity_!=entity) restore(base,read);
        entity_=entity;active_=true;released_at_=0;
        for(std::size_t i=0;i<N && i<entries_.size();++i) {
            if(!bodies[i] || !bodies[i]->record || !bodies[i]->handle) continue;
            const auto& b=*bodies[i];auto& e=entries_[i];
            const auto filter=rig::value<std::uint32_t>(read,b.record+HeldBodyCollision::filter_offset);
            if(!filter) {++counters_.refused;continue;}
            if(e.active && e.handle==b.handle && e.record==b.record) {   // ours already; the engine may have put its layer back
                if((*filter&0x1fu)!=NativeHeldObject::layer_debris && NativeHeldObject::write_filter(base,read,b.handle,(*filter&~0x1fu)|NativeHeldObject::layer_debris)) ++counters_.reasserted;
                continue;
            }
            if(e.active) restore_one(base,read,e);
            if((*filter&0x1fu)==NativeHeldObject::layer_debris) continue;   // already debris (not ours): nothing to give back later
            Entry next;next.entity=b.entity;next.record=b.record;next.handle=b.handle;next.entity_id=b.entity_id;next.saved=*filter;next.generation=b.identity.generation;
            if(!NativeHeldObject::write_filter(base,read,b.handle,(*filter&~0x1fu)|NativeHeldObject::layer_debris)) {++counters_.refused;continue;}
            next.active=true;e=next;++counters_.swapped;
        }
    }
    // No hand holds this body now. at_feet: any of its bones is still at the player's feet. Restores once it is clear (or the grace ran out).
    template<class Read> void unheld(std::uintptr_t base,Read& read,std::uint64_t now,bool at_feet) {
        if(!active_) return;
        if(!released_at_) released_at_=now?now:1;
        if(at_feet && now>=released_at_ && now-released_at_<release_grace_ms) {++counters_.deferred_frames;return;}
        restore(base,read);
    }
    template<class Read> void restore(std::uintptr_t base,Read& read) {for(auto& e:entries_) if(e.active) restore_one(base,read,e);active_=false;entity_=0;released_at_=0;}
    bool active() const noexcept {return active_;}
    std::uintptr_t entity() const noexcept {return entity_;}
    const std::array<Entry,17>& entries() const noexcept {return entries_;}
    struct Counters {std::uint64_t swapped{},restored{},refused{},skipped{},reasserted{},deferred_frames{};};
    const Counters& counters() const noexcept {return counters_;}
private:
    template<class Read> void restore_one(std::uintptr_t base,Read& read,Entry& e) {
        const auto first=rig::value<std::uint32_t>(read,e.record+0x70),second=rig::value<std::uint32_t>(read,e.record+0x74);
        const auto filter=rig::value<std::uint32_t>(read,e.record+HeldBodyCollision::filter_offset);
        const bool ok=first && second && *first==e.handle && *second==e.handle && filter && (*filter&0x1fu)==NativeHeldObject::layer_debris &&
            NativeHeldObject::write_filter(base,read,e.handle,e.saved);
        if(ok) ++counters_.restored;else ++counters_.skipped;
        e=Entry{};
    }
    std::array<Entry,17> entries_{};std::uintptr_t entity_{};std::uint64_t released_at_{};bool active_{};Counters counters_{};
};
} // namespace d2vr
