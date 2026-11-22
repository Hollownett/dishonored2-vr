#pragma once
#include <cstdint>

namespace d2vr {
// Death of the Outsider, dual wield: which forearms carry Billie's Voltaic Gun (bit 0 the left = the engine's own gun, bit 1 the right =
// the mod's forearm copy). User, DOTO headset v145743: "I cannot switch the crossbow to my right arm; when I select it on my right arm it
// just updates it on my left arm" - as in the Dishonored 2 mod, the wheel a hand opens decides which arm the chosen item goes to:
//  - the gun arrives (it was not equipped) after a wheel commit by hand h: it goes to arm h only;
//  - the gun is already equipped and hand h chooses it again (the game skips the equip: the same item): arm h gets it too (both arms);
//  - the gun arrives with no wheel commit (a save load, the game's own equip): the arms it last had (the left at first).
// Pure state: the caller feeds the engine truth each control frame (update) and the wheel's events (commit, same).
struct VoltaicArms {
    static constexpr std::uint64_t commit_window_ms=4000,same_after_ms=900;
    unsigned arms=1u,pref=1u;bool equipped=false;
    int commit_hand=-1;std::uint64_t commit_tick=0,commit_rev=0;bool commit_equipped=false;
    unsigned wheel_gain=0;std::uint64_t same_adds=0;   // arms a wheel choice gave the gun since the caller last took them (take_gain); same-item adds
    // A wheel opened by hand h closed on an item (not on an empty sector).
    void commit(unsigned h,std::uint64_t now,std::uint64_t rev,bool equipped_now) noexcept {
        commit_hand=static_cast<int>(h&1u);commit_tick=now;commit_rev=rev;commit_equipped=equipped_now;
    }
    // The exact same-item test (the hooked equip named the item slot 1 already held).
    void same(unsigned h) noexcept {
        h&=1u;if(equipped && commit_equipped && commit_hand==static_cast<int>(h)) {arms|=1u<<h;pref=arms;wheel_gain|=1u<<h;++same_adds;}
        if(commit_hand==static_cast<int>(h)) commit_hand=-1;
    }
    unsigned take_gain() noexcept {const unsigned g=wheel_gain;wheel_gain=0;return g;}
    // Test command: set the arms directly.
    void force(unsigned mask) noexcept {mask&=3u;if(!mask) return;arms=pref=mask;commit_hand=-1;}
    // Each control frame: is the Voltaic Gun slot 1's item, the equip revision. Returns the arms carrying it (0: not equipped).
    unsigned update(bool equipped_now,std::uint64_t now,std::uint64_t rev) noexcept {
        if(equipped_now && !equipped) {   // it arrived
            if(commit_hand>=0 && !commit_equipped && now-commit_tick<=commit_window_ms) {arms=1u<<static_cast<unsigned>(commit_hand);wheel_gain|=arms;}
            else arms=pref?pref:1u;
            pref=arms;commit_hand=-1;
        }
        equipped=equipped_now;
        if(commit_hand>=0) {
            if(now-commit_tick>commit_window_ms) commit_hand=-1;
            // fallback same-item test: the gun was equipped at the commit and nothing changed since (the game skipped the equip)
            else if(commit_equipped && equipped_now && rev==commit_rev && now-commit_tick>=same_after_ms) {
                const unsigned b=1u<<static_cast<unsigned>(commit_hand);arms|=b;pref=arms;wheel_gain|=b;++same_adds;commit_hand=-1;}
            else if(commit_equipped && !equipped_now) commit_hand=-1;   // another item replaced it
        }
        return equipped?arms:0u;
    }
};
} // namespace d2vr
