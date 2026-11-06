// The rule (user, 2026-11-05: "object which is hold never collides with the hand and player body"): src/held_body_collision.hpp swaps a physically held
// prop onto the DEBRIS layer through the engine's own filter writer and puts its exact word back when the hold ends. This drives that ledger with a fake
// body record and records every filter write instead of calling the engine (NativeHeldObject::filter_seam_).
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "../src/held_body_collision.hpp"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

namespace {
// A tiny fake process: the pool record the body lives in, and the game pointer the filter writer resolves.
struct Memory {
    static constexpr std::uintptr_t record=0x7ff600100000,game_at=0x39a5290;   // base +0x39a5290 (the game pointer the filter writer resolves)
    std::array<std::byte,0x200> bytes{};
    std::uintptr_t game{0x7ff600000000};   // a plausible game pointer (rig::pointer rejects small values)

    template<class T> void put(std::uintptr_t at,T v) {std::memcpy(&bytes[at-record],&v,sizeof v);}
    bool read(std::uintptr_t address,void* out,std::size_t size) const {
        if(address==game_at && size==sizeof game) {std::memcpy(out,&game,sizeof game);return true;}
        if(address<record || address+size>record+bytes.size()) return false;
        std::memcpy(out,&bytes[address-record],size);
        return true;
    }
};
struct Write {std::uint32_t handle{},filter{};};
std::vector<Write>* g_writes{};
std::uintptr_t g_game{};
bool seam(std::uintptr_t game,std::uint32_t handle,std::uint32_t filter) {
    assert(game==g_game);   // the writer only ever runs in the game the mod read
    if(g_writes) g_writes->push_back({handle,filter});
    return true;
}
}
int main() {
    constexpr std::uintptr_t base=0;
    Memory m;
    std::vector<Write> writes;
    g_writes=&writes;g_game=m.game;
    d2vr::NativeHeldObject::filter_seam_=&seam;
    const auto read=[&](std::uintptr_t address,void* out,std::size_t size) {return m.read(address,out,size);};
    const auto body=[&](std::uintptr_t entity,std::uint32_t handle,std::uint32_t generation) {
        d2vr::NativeRigidBody b{};b.entity=entity;b.record=Memory::record;b.handle=handle;b.entity_id=static_cast<std::uint32_t>(entity);
        b.identity.generation=generation;return b;
    };
    const auto settle=[&](std::uint32_t handle) {m.put(Memory::record+0x70,handle);m.put(Memory::record+0x74,handle);};
    // A prop on RIGID_BODY (layer 3) whose mask says it collides with the player's layers too.
    constexpr std::uint32_t rigid_word=(3u|(0x61u<<5));
    m.put(Memory::record+0x6c,rigid_word);settle(0x42);
    d2vr::HeldBodyCollision held;
    assert(!held.active(0));
    // 1. The hold takes DEBRIS (layer 4) and keeps the mask: written exactly once, through the seam.

    assert(held.hold(0,base,read,body(0x1111,0x42,7)));
    assert(writes.size()==1 && writes[0].handle==0x42 && writes[0].filter==(4u|(0x61u<<5)));
    assert(held.counters().swapped==1 && held.active(0));
    // 2. While it is held the same body is not written again: the ledger reads the filter and finds its own layer.
    m.put(Memory::record+0x6c,writes[0].filter);
    assert(held.hold(0,base,read,body(0x1111,0x42,7)));
    assert(writes.size()==1);
    // 3. The engine writes the old layer back mid-hold: it is reasserted once, counted.
    m.put(Memory::record+0x6c,rigid_word);
    assert(held.hold(0,base,read,body(0x1111,0x42,7)));
    assert(writes.size()==2 && writes[1].filter==(4u|(0x61u<<5)) && held.counters().reasserted==1);
    // 4. The release puts the exact word it found back, once; the entry is closed.
    m.put(Memory::record+0x6c,writes[1].filter);
    assert(held.release(0,base,read));
    assert(writes.size()==3 && writes[2].handle==0x42 && writes[2].filter==rigid_word);
    assert(held.counters().restored==1 && !held.active(0));
    assert(!held.release(0,base,read) && writes.size()==3);   // idempotent: nothing left to restore
    // 5. A body the engine changed (or respawned: another handle in the record) is never clobbered: the restore is skipped and counted.
    m.put(Memory::record+0x6c,rigid_word);
    assert(held.hold(0,base,read,body(0x2222,0x43,8)));
    m.put(Memory::record+0x70,0x99);m.put(Memory::record+0x74,0x99);   // the record now belongs to another body
    writes.clear();
    assert(!held.release(0,base,read) && writes.empty() && held.counters().skipped==1);
    // 6. Another object taking the hand releases the first one's word before it takes its own.
    m.put(Memory::record+0x70,0x43);m.put(Memory::record+0x74,0x43);
    m.put(Memory::record+0x6c,rigid_word);
    assert(held.hold(1,base,read,body(0x3333,0x43,9)));
    m.put(Memory::record+0x6c,4u|(0x61u<<5));   // the engine applied the DEBRIS word
    writes.clear();
    assert(held.hold(1,base,read,body(0x4444,0x44,10)));
    assert(writes.size()==2 && writes[0].filter==rigid_word && writes[1].filter==(4u|(0x61u<<5)));
    // 7. No readable game pointer: refused, nothing written, the entry stays closed.
    {   d2vr::HeldBodyCollision refused;writes.clear();const auto nowhere=[](std::uintptr_t,void*,std::size_t) {return false;};
        assert(!refused.hold(0,base,nowhere,body(0x5555,0x45,11)));
        assert(writes.empty() && refused.counters().refused==1 && !refused.active(0));
    }
    std::cout<<"held_body_collision_check passed\n";
}
