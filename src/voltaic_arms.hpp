#pragma once
#include <cstdint>

namespace d2vr {
// Death of the Outsider, dual wield: which forearms carry Billie's Voltaic Gun (bit 0 the left, bit 1 the right), as Dishonored 2's dual
// wield treats a gun (ArmSet): the wheel a hand opens decides which arm the chosen item goes to, and the other arm keeps exactly what it holds.
// User, DOTO headset v145743: "I cannot switch the crossbow to my right arm"; v145744: "the other hand must keep its Voltaic Gun".
// The engine has ONE slot for the gun, a power, a gadget: `native` = that slot holds the gun. An arm that carries the gun shows the engine's
// own gun when it is the left arm and the gun is native; otherwise it shows the mod's forearm copy, which fires through the player's gun
// component (upgrades, ammo and its pool are that one gun's).
//  - the gun arrives after hand h's wheel commit: arm h gets it (the other arm keeps its copy, if it has one);
//  - the gun is native and hand h chooses it again (the game skips the equip: the same item): arm h gets it too;
//  - hand h chooses another item while the gun is native: arm h loses it, the other arm keeps its copy;
//  - hand h holds a copy and chooses another item (the gun not native): h's copy goes (that item comes to h);
//  - the game puts the gun away with no wheel (a carry, a cutscene): every arm, remembered for its return (pref);
//  - the gun arrives with no wheel (a save load, the end of a carry): the arms it had (the left at first);
//  - restore(): the arrival's other arm held a power (or a gadget) that the caller equips again: the gun leaving slot 1 for it is expected,
//    the arrival's arm keeps the gun as its copy.
// Pure state: the caller feeds the engine truth each control frame (update) and the wheel's events (commit, same).
struct VoltaicArms {
    static constexpr std::uint64_t commit_window_ms=4000,same_after_ms=900,displace_after_ms=1500,restore_window_ms=2500;
    unsigned arms=0u,pref=1u;bool native=false;
    int commit_hand=-1;std::uint64_t commit_tick=0,commit_rev=0;bool commit_native=false,commit_handled=false;
    std::uint64_t restore_until=0;
    unsigned wheel_gain=0,arrived_by=0;   // arms a wheel gave the gun since take_gain(); the hand whose wheel brought the native gun (take_arrival: bit h)
    std::uint64_t same_adds=0,keeps=0,displaced=0,arrivals=0,game_aways=0;
    // A wheel opened by hand h closed on an item (not on an empty sector).
    void commit(unsigned h,std::uint64_t now,std::uint64_t rev,bool native_now) noexcept {
        commit_hand=static_cast<int>(h&1u);commit_tick=now;commit_rev=rev;commit_native=native_now;commit_handled=false;
    }
    // The exact same-item test (the hooked equip named the item slot 1 already held).
    void same(unsigned h) noexcept {
        h&=1u;if(commit_hand!=static_cast<int>(h)) return;
        if(native && commit_native) {if(!(arms&(1u<<h))) {arms|=1u<<h;wheel_gain|=1u<<h;++same_adds;}}
        else if(!commit_native && !commit_handled && (arms&(1u<<h))) {arms&=~(1u<<h);++displaced;commit_handled=true;return;}   // a copy hand chose slot 1's item (a power): it comes to this hand
        commit_hand=-1;if(arms) pref=arms;
    }
    void restore(std::uint64_t now) noexcept {restore_until=now+restore_window_ms;}
    // Test commands: set the arms directly.
    void force(unsigned mask) noexcept {arms=mask&3u;if(arms) pref=arms;commit_hand=-1;}
    unsigned take_gain() noexcept {const unsigned g=wheel_gain;wheel_gain=0;return g;}
    unsigned take_arrival() noexcept {const unsigned a=arrived_by;arrived_by=0;return a;}
    // Each control frame: is the gun slot 1's item (native), the equip revision. Returns the arms carrying it.
    unsigned update(bool native_now,std::uint64_t now,std::uint64_t rev) noexcept {
        const bool committed=commit_hand>=0 && now-commit_tick<=commit_window_ms;
        const unsigned cb=committed?1u<<static_cast<unsigned>(commit_hand):0u;
        if(native_now && !native) {   // the gun came into slot 1
            if(committed && !commit_native) {arms|=cb;wheel_gain|=cb;arrived_by|=cb;++arrivals;}
            else if(!arms) arms=pref?pref:1u;
            commit_hand=-1;
        } else if(!native_now && native) {   // the gun left slot 1
            if(now<restore_until) {restore_until=0;}   // the other arm's item equipped again: every arm keeps what it has
            else if(committed && commit_native) {arms&=~cb;++keeps;}   // that hand chose another item: the other arm keeps the gun
            else {if(arms) pref=arms;arms=0;++game_aways;}
            commit_hand=-1;
        }
        native=native_now;
        if(commit_hand>=0) {
            if(!committed) commit_hand=-1;
            else if(commit_native) {
                // the gun native at the commit: another item makes it leave slot 1 (above); nothing changing is the same item
                if(rev==commit_rev && now-commit_tick>=same_after_ms) {if(!(arms&cb)) {arms|=cb;wheel_gain|=cb;++same_adds;}commit_hand=-1;}
            } else if(!commit_handled && now-commit_tick>=(rev!=commit_rev?displace_after_ms:same_after_ms)) {
                // the gun not native: a copy hand that chose anything but the gun loses its copy. The commit stays open for the rest of its
                // window, so a gun arriving late (its equip slower than this) still comes to this arm (above).
                if(arms&cb) {arms&=~cb;++displaced;}
                commit_handled=true;
            }
        }
        if(arms) pref=arms;
        return arms;
    }
};
// One forearm copy's trigger with the charge upgrade (gaussgun_charge.equipmentupgrade: each ammo's charged projectile), timed as the
// engine's own fire state (playerStateFireWeapon: m_chargedShotHoldDuration 2.0 s default, m_chargedShotAutoFireDuration 4.0 s in
// fire_weapon.playerstatefireweapon; static RE 2026-11-22): released before 2 s the plain shot, from 2 s the charged shot, and at 4 s it fires
// by itself, overcharged (then nothing more until the chord is released). Without the upgrade the plain gun: fire while pulled (the caller
// repeats it when the copy is ready). Pure.
struct VoltaicCharge {
    static constexpr std::uint64_t charge_ms=2000,overcharge_ms=4000;
    bool held=false,ready=false,spent=false;std::uint64_t since=0;
    struct Step {bool fire{},charged{},overcharged{},charging{},charge_started{};};
    Step step(bool pull,std::uint64_t now,bool owned,std::uint64_t need=charge_ms,std::uint64_t auto_fire=overcharge_ms) noexcept {
        Step s{};
        if(!owned) {held=pull;ready=spent=false;s.fire=pull;return s;}
        if(pull) {
            if(!held) {held=true;ready=spent=false;since=now;}
            if(spent) return s;
            if(!ready && now-since>=need) {ready=true;s.charge_started=true;}
            if(ready && now-since>=auto_fire) {s.fire=s.charged=s.overcharged=true;spent=true;return s;}
            s.charging=ready;return s;
        }
        if(held && !spent) {s.fire=true;s.charged=ready;}
        held=ready=spent=false;return s;
    }
};
} // namespace d2vr
