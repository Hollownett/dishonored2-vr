#pragma once
// Corpse on the shoulder (user, 2026-11-05): a body the player puts on a shoulder is handed to the game's own body carry
// (arkPlayerStateCarryCorpse, FSM state 45), so it acts exactly like the native carry: the player's carry pose and animation,
// the mission scripts that expect a carried body (Sokolov out of the Jindosh mansion), the game's Drop and Throw, and the
// native prompt. The physical lane does the lifting and keeps the body where the hand let go at the shoulder
// (grip_decision.hpp ShoulderDock); the hold below hands it over: the game's Use on a dead or unconscious body is its carry
// (interaction decl m_carryCorpse -> PLAYER_STATE_CARRY_CORPSE). It is HELD, not tapped, because the engine's carry is the held
// Use on a body (v613 measured ~0.55 s from the press to the carry), and it is a window rather than one frame for two more reasons:
//  - the interaction sensor answers on a routed query, so the body may become the placing hand's native Use target a frame late;
//  - a body whose Loot action is live would be LOOTED by a Use press (the engine offers Loot as the corpse's primary action and
//    Carry as its secondary one), so the hold waits until that body's Loot is no longer offered. Looting the body first (a plain
//    grip Use on it, v674) is what makes its Carry the offer the press reaches.
// The game's Use while a body is carried is its DROP (a toggle), so the hold ends before then and is never repeated on a carry.
#include <cstdint>
namespace d2vr {
inline constexpr std::uint64_t shoulder_carry_window_ms=1500;   // the body stays docked this long while the handoff is tried (v613:
                                                                 // ~0.55 s from the held Use to the engine's carry, plus the lock and retries)
inline constexpr std::uint64_t shoulder_carry_retry_ms=250;     // the least gap between two held-Use episodes
inline constexpr std::uint64_t shoulder_carry_target_grace_ms=200;   // a lost native target inside this keeps the hold (a locked
                                                                     // query invalidates the selection for a round trip)
inline constexpr std::uint64_t shoulder_carry_min_hold_ms=700;  // once a hold starts it is kept this long even if the routed target
                                                                // reads empty (the engine's carry is the held Use; v613 measured
                                                                // ~0.55 s, so a 200 ms episode is far too short for it)
inline constexpr unsigned shoulder_carry_max_presses=3;         // episodes per placement (a flickering target cannot chatter)
// v1142 (user, 2026-11-11: "grabbing an enemy and a body are different controls"; v1141 moved Loot to the trigger and dropped v674's grip
// loot, so an unlooted body put on the shoulder never reached the carry: loot_blocked, silently). A body still offering Loot is looted by a
// TAP of Use first (the engine's primary action on a press), then carried by the held Use once the offer clears: on the shoulder = carried.
inline constexpr std::uint64_t shoulder_loot_tap_ms=150;        // a tap, well under the ~0.55 s held Use that is the carry
inline constexpr std::uint64_t shoulder_loot_settle_ms=400;     // after a tap, the offer is given this long to clear before another
inline constexpr unsigned shoulder_loot_max_taps=2;
// the whole handoff the input side keeps the query and the press for: the loot taps and their settle, then the carry's own window
inline constexpr std::uint64_t shoulder_handoff_ms=shoulder_carry_window_ms+shoulder_loot_max_taps*(shoulder_loot_tap_ms+shoulder_loot_settle_ms);
// armed: the physical lane holds a placed body at the shoulder (ShoulderDock anchored). target: the placing hand's live native
// Use target entity now (0: the hand answers nothing). loot: the engine offers that body's Loot action now. carrying: the game
// carries a body now. armed_ms: the placement's tick (a new placement restarts the attempt).
struct ShoulderCarryIn {
    bool enabled{};bool armed{};std::uintptr_t body{},target{};bool loot{},carrying{};std::uint64_t now{},armed_ms{};
};
// press: hold the game's Use now (a level: it stays true frame after frame while the handoff is being tried). started: the game
// took the body into its carry this placement. loot_blocked: the body offers its Loot action, so no press was made (the physical
// shoulder keeps it). gave_up: the window closed with the game still carrying nothing (the physical shoulder keeps it). presses:
// held-Use episodes so far this placement.
struct ShoulderCarryOut {bool press{},started{},loot_blocked{},gave_up{};unsigned presses{};};
class ShoulderCarryTrigger {
public:
    ShoulderCarryOut update(const ShoulderCarryIn& in) noexcept {
        ShoulderCarryOut out;
        if(!in.enabled || !in.armed || !in.body || !in.armed_ms) {reset();return out;}
        if(in.body!=body_ || in.armed_ms!=armed_) {reset();body_=in.body;armed_=in.armed_ms;}
        out.presses=presses_;
        if(started_) return out;
        if(in.carrying) {   // the game carries the body: the handoff is done (the dock's hand lets go; the game owns it from here)
            holding_=false;
            if(presses_) {started_=true;out.started=true;}
            return out;
        }
        const bool target_now=in.target==body_;
        if(target_now) last_target_=in.now;
        if(tap_until_) {   // a loot tap in progress: held for its length, then released whatever the offer says
            if(in.now<tap_until_) {out.press=true;return out;}
            tap_until_=0;tap_done_=in.now;
        }
        if(in.loot) {   // never a HELD press while Loot is offered (it would loot, not carry): tap it out first
            holding_=false;
            const bool settled=!tap_done_ || in.now-tap_done_>=shoulder_loot_settle_ms;
            if(target_now && settled && loot_taps_<shoulder_loot_max_taps && in.now>=armed_ && in.now-armed_<shoulder_carry_window_ms) {
                ++loot_taps_;tap_until_=in.now+shoulder_loot_tap_ms;out.press=true;return out;
            }
            if(loot_taps_>=shoulder_loot_max_taps && settled && !loot_reported_) {loot_reported_=true;out.loot_blocked=true;}
            return out;
        }
        if(tap_done_ && !looted_) {looted_=true;window_=in.now;}   // the loot went: the carry gets its whole window from here
        const auto from=looted_?window_:armed_;const bool before_window=in.now>=from && in.now-from<shoulder_carry_window_ms;
        // A lost target keeps the hold for a round trip: taking the game's interaction query for this hand invalidates the selection,
        // so the hand's own target reads empty for a frame or two after the press (a different, non-zero target ends it at once).
        const bool target_ok=target_now || (in.target==0 && last_target_ && in.now>=last_target_ && in.now-last_target_<=shoulder_carry_target_grace_ms);
        // A started hold is kept for the engine's own latency: the game's carry needs the Use actually held (v613 ~0.55 s), and the
        // routed target reads empty while the locked query answers, so a 200 ms episode would never reach it.
        const bool min_hold=holding_ && in.target==0 && episode_ && in.now>=episode_ && in.now-episode_<shoulder_carry_min_hold_ms;
        if(before_window && (target_ok||min_hold) && presses_<shoulder_carry_max_presses) {
            if(!holding_) {
                if(next_ && in.now<next_) return out;   // too soon after the last episode: wait it out
                holding_=true;episode_=in.now;++presses_;out.presses=presses_;
            }
            out.press=true;
            return out;
        }
        if(holding_) {holding_=false;next_=in.now+shoulder_carry_retry_ms;}
        if(!before_window && !gave_up_) {gave_up_=true;out.gave_up=true;}
        return out;
    }
    void reset() noexcept {body_=armed_=next_=last_target_=episode_=tap_until_=tap_done_=window_=0;presses_=loot_taps_=0;loot_reported_=gave_up_=started_=holding_=looted_=false;}
    unsigned loot_taps() const noexcept {return loot_taps_;}
private:
    std::uintptr_t body_{};std::uint64_t armed_{},next_{},last_target_{},episode_{},tap_until_{},tap_done_{},window_{};unsigned presses_{},loot_taps_{};bool loot_reported_{},gave_up_{},started_{},holding_{},looted_{};
};

// v1038 (user, headset v1037: "carrying a corpse on my shoulder, my hands are disabled: no powers, no weapons"). The game's own cause:
// arkPlayerStateCarryCorpse (requester 45) locks and empties BOTH hands' EquipChange on entry (activate 828770 stage 1: the LEFT hand,
// state 64, code 0x21, or 0x81 with a grapple; 829fa0: the right, state 53), and the lock (S+0x158 > 0) keeps every later draw, wheel pick
// and power switch of that hand a remembered selection until the carry ends (docs/native-weapon-equip-research.md 2b).
// v1041 (user, headset v1040: "I cannot put any bodies on my shoulders"): the v1039 rewrite KEPT the left item (0x24), and the carry then never
// starts. Disassembled (read-only): stage 1 is followed, each frame, by 82a270: it waits until EquipChange 64 and 53 are idle, then reads the
// two hand slots of the inventory (fsm+0x328: +0x68 right, +0x70 left); either still holding an item aborts the state with the engine's own
// message "Both hands have not been unequipped" (82a2c1). So the carry REQUIRES both hands empty when it starts. The unequip therefore stays;
// only the LOCK goes (the item is remembered as the hand's selected one, nothing is locked), and CarryLeftRedraw (below) draws that item again
// once the carry is established (stage 3, [state+0x110]), so powers and weapons are usable during the carry as v1039 intended.
// Pure: the caller (NativeManualEquipRuntime) supplies the raw params fields of the left EquipChange (state 64).
inline constexpr std::uint32_t carry_corpse_requester=45;
inline constexpr std::uint32_t carry_corpse_settled_stage=3;   // CarryCorpse [state+0x110]: 1 = emptying the hands, 2 = lifting (~1 s), 3 = carried
                                                               // (sim v1043 journal: 1 -> 2 -> 3, held at 3 until the drop; 4, set at 829f73, never seen in a carry)
inline std::uint32_t carry_left_equip_code(std::uint32_t code,std::uint32_t requester,bool explicit_item) noexcept {
    if(requester!=carry_corpse_requester || explicit_item || !(code&0x01u) || (code&0x0eu)) return code;   // only CarryCorpse's lock-and-empty
    return code&~0x01u;   // still an unequip (the carry needs the hand empty), but no lock: the item stays the hand's selected one
}
// Draws the left hand's item again after CarryCorpse emptied it. armed: the hook rewrote the left unequip of a hand that held an item.
// settled: the game's CarryCorpse is active at its settled stage. left_empty: the left slot holds nothing. update() is true when the control
// thread should queue one draw of the selected item (instant); a failed draw is retried a few times, a carry that never settles gives up.
struct CarryRedrawIn {bool armed{},carrying{},settled{},left_empty{};std::uint64_t now{};};
class CarryLeftRedraw {
public:
    static constexpr std::uint64_t window_ms=3000,retry_ms=300;
    static constexpr unsigned max_draws=3;
    bool update(const CarryRedrawIn& in) noexcept {
        if(in.armed) {on_=true;since_=in.now;draws_=0;next_=0;seen_=false;}
        if(!on_) return false;
        if(in.carrying) seen_=true;
        else if(seen_ || in.now-since_>window_ms) {on_=false;return false;}   // the carry ended (or never began): nothing to give back
        if(!in.carrying || !in.settled) return false;
        if(!in.left_empty) {on_=false;return false;}                          // the item is in the hand again
        if(draws_>=max_draws) {on_=false;return false;}
        if(in.now<next_) return false;
        next_=in.now+retry_ms;++draws_;return true;
    }
    unsigned draws() const noexcept {return draws_;}
private:
    bool on_{},seen_{};std::uint64_t since_{},next_{};unsigned draws_{};
};
} // namespace d2vr
