#pragma once
#include <array>
#include <cstdint>

namespace d2vr::arm {
// Independent per-arm weapon state for dual wield (design: docs/superpowers/specs/2026-11-06-independent-arm-state-design.md).
// Each physical arm (0 left, 1 right) says what it holds and how. The engine has two slots (slot 0 the sword, slot 1 the gun, crossbow,
// gadget, Heart or power); which arm drives a slot is DERIVED from what the arms hold (derive()). An event touches only its own arm;
// the one cross-arm rule is the POWER SWAP in place(): a weapon placed in the arm that holds a slot-1-only item (power, Heart, gadget)
// moves that item to the other arm when it is empty, else puts it away. Engine truth enters only through native_changed(); a mod
// request that makes the engine equip something (wheel commit, hip draw, native pickup, gadget catch) leaves a ticket naming its arm.
// The first five values are the old HandLoadout::Extra numbers (the status JSON keeps its "extra" field readable).
enum class Held : std::uint8_t {none,pistol,crossbow,guard_sword,guard_pistol,sword,gadget,heart,power};
enum class Home : std::uint8_t {none,slot0,slot1,copy};   // slot0/slot1: the engine's own item; copy: a mod-owned model in the hand
constexpr bool native_gun(Held h) noexcept {return h==Held::pistol || h==Held::crossbow;}
constexpr bool is_gun(Held h) noexcept {return native_gun(h) || h==Held::guard_pistol;}
constexpr bool slot1_only(Held h) noexcept {return h==Held::gadget || h==Held::heart || h==Held::power;}
constexpr bool slot1_item(Held h) noexcept {return native_gun(h) || slot1_only(h);}
constexpr bool copyable(Held h) noexcept {return native_gun(h) || h==Held::guard_sword || h==Held::guard_pistol;}
constexpr bool is_weapon(Held h) noexcept {return h==Held::sword || native_gun(h) || h==Held::guard_sword || h==Held::guard_pistol;}

struct Arm {Held what{};Home home{};};
// An outstanding request for the engine to equip something in slot 1 for this arm. chosen (wheel only): 0 not known yet, 1 the same item
// slot 1 already held (the game skips the equip), 2 a different one (an arrival is coming).
struct Pend {bool on{},wheel{};Held want{};unsigned chosen{};std::uint64_t ticket{},deadline{},revision{},since{};};
// What an event asks the caller to do next.
struct Fx {
    unsigned copy_displaced{};   // bit h: arm h's mod-owned copy was replaced (it falls where the hand is)
    unsigned sword_left{};       // bit h: the sword left arm h (reset its grip machine: SwordGrip puts it away)
    unsigned power_moved{};      // bit h: a slot-1-only item moved to arm h (the power swap)
    bool restore_slot1{};        // the engine must equip what slot 1 held before (a gun chosen over a held power; the swap after an arrival)
    bool unequip_slot1{};        // the engine must put slot 1 away (the swap with the other arm occupied)
    bool native{};               // pickup(): the gun goes native, the caller asks the game to equip it
    bool refused{};              // the event did not apply
    unsigned expired{};          // bit h: arm h's pending request ran out
    unsigned arrived{};          // bit h: the engine's slot-1 item landed in arm h as the ticketed request
    bool same_copy{};            // wheel: the chosen item was the one slot 1 held, the opening arm got a copy at once
    void merge(const Fx& o) noexcept {copy_displaced|=o.copy_displaced;sword_left|=o.sword_left;power_moved|=o.power_moved;restore_slot1|=o.restore_slot1;
        unequip_slot1|=o.unequip_slot1;native|=o.native;refused|=o.refused;expired|=o.expired;arrived|=o.arrived;same_copy|=o.same_copy;}
};
struct Native {Held slot1{};bool sword{};std::uint64_t revision{};};   // engine truth, sampled each control frame
struct Plan {unsigned sword_arm=1,slot1_arm=0,cross=0;std::array<Held,2> copy{};
    bool operator==(const Plan& o) const noexcept {return sword_arm==o.sword_arm && slot1_arm==o.slot1_arm && cross==o.cross && copy==o.copy;}
};
// The plan packed into one word for the render thread: bit 0 sword arm, bit 1 slot-1 arm, bits 2-3 the arm-joint cross (bit 0: the slot-1
// item rides the right arm, bit 1: the sword rides the left), 4 bits per arm for its copy. The game's own hands (knob off): 1.
constexpr unsigned pack_default=1u;
constexpr unsigned pack(const Plan& p) noexcept {return (p.sword_arm&1u)|((p.slot1_arm&1u)<<1)|((p.cross&3u)<<2)|(static_cast<unsigned>(p.copy[0])<<4)|(static_cast<unsigned>(p.copy[1])<<8);}
constexpr unsigned sword_arm_of(unsigned pk) noexcept {return pk&1u;}
constexpr unsigned slot1_arm_of(unsigned pk) noexcept {return (pk>>1)&1u;}
constexpr unsigned cross_of(unsigned pk) noexcept {return (pk>>2)&3u;}
constexpr Held copy_of(unsigned pk,unsigned arm) noexcept {return static_cast<Held>((pk>>(4+4*(arm&1u)))&15u);}
// hand_weapon_bits (bit 0 a slot-1 item drawn, bit 1 the sword drawn) as physical hand bits (bit 0 left, bit 1 right), plus each arm's copy.
constexpr unsigned hand_bits(unsigned pk,unsigned slot_bits) noexcept {
    unsigned b=slot_bits&~3u;
    if(slot_bits&1u) b|=1u<<slot1_arm_of(pk);
    if(slot_bits&2u) b|=1u<<sword_arm_of(pk);
    for(unsigned h=0;h<2;++h) if(copy_of(pk,h)!=Held::none) b|=1u<<h;
    return b;
}

struct ArmSet {
    static constexpr std::uint64_t settle_ms=60,pend_ms=3500,same_fallback_ms=700,wheel_ctx_ms=3000,leave_ms=2500;
    std::array<Arm,2> a{};std::array<Pend,2> pend{};
    unsigned rest_slot1=0;                  // where slot 1 lives when nobody holds it (the game's: left); the sword rests on the other arm
    bool wheel_ctx{};std::uint64_t wheel_ctx_until{};   // a wheel is open or its commit is unresolved: the old slot-1 holder stays until the arrival
    std::uint64_t ticket_seq{};
    bool sword_leaving{};std::uint64_t sword_leave_until{};          // the model took the sword from its arm; the engine still has it drawn
    Held leave_kind{};std::uint64_t leave_until{};                   // the model put a slot-1 item away; the engine still reports it
    bool seen_valid{},applied_valid{};Held seen_kind{},applied_kind{};std::uint64_t seen_rev{},applied_rev{},seen_tick{};

    // --- queries -------------------------------------------------------------------------------------------------------------------
    int sword_holder() const noexcept {for(unsigned h=0;h<2;++h) if(a[h].home==Home::slot0) return static_cast<int>(h);return -1;}
    int slot1_holder() const noexcept {for(unsigned h=0;h<2;++h) if(a[h].home==Home::slot1) return static_cast<int>(h);return -1;}
    Held copy_at(unsigned h) const noexcept {return a[h&1u].home==Home::copy?a[h&1u].what:Held::none;}
    bool empty(unsigned h) const noexcept {h&=1u;return a[h].what==Held::none && !pend[h].on;}
    // A hand may take a weapon (the sword, a gun) when it holds nothing or only a power (the power swap moves it).
    bool free_for_weapon(unsigned h) const noexcept {h&=1u;return !pend[h].on && (a[h].what==Held::none || a[h].what==Held::power);}
    // A pickup may also replace a sword that is drawn in the arm but not gripped (the sword then leaves that arm only).
    bool free_for_pickup(unsigned h) const noexcept {h&=1u;return !pend[h].on && (a[h].what==Held::none || a[h].what==Held::power || a[h].home==Home::slot0);}
    bool any_pending() const noexcept {return pend[0].on || pend[1].on;}
    Plan derive() const noexcept {
        const int s=sword_holder(),p=slot1_holder();Plan pl;
        pl.slot1_arm=p>=0?static_cast<unsigned>(p):s>=0?static_cast<unsigned>(s)^1u:rest_slot1&1u;
        pl.sword_arm=s>=0?static_cast<unsigned>(s):pl.slot1_arm^1u;
        pl.cross=(pl.slot1_arm==1u?1u:0u)|(pl.sword_arm==0u?2u:0u);
        pl.copy={copy_at(0),copy_at(1)};return pl;
    }
    // The invariants every event keeps (tests): one sword and one native slot-1 item at most, copies only of copyable things, a native
    // item in its slot, nothing in two places.
    bool valid() const noexcept {
        unsigned s0=0,s1=0;
        for(unsigned h=0;h<2;++h) {
            const Arm& x=a[h];
            if(x.what==Held::none) {if(x.home!=Home::none) return false;continue;}
            if(x.home==Home::none) return false;
            if(x.home==Home::slot0) {++s0;if(x.what!=Held::sword) return false;}
            else if(x.home==Home::slot1) {++s1;if(!slot1_item(x.what)) return false;}
            else if(!copyable(x.what)) return false;
            else if(x.what==Held::sword) return false;
        }
        return s0<=1 && s1<=1;
    }

    // --- the one placement function ---------------------------------------------------------------------------------------------------
    // Arm h now holds `what` (home says how). Newest wins: what the arm held before leaves that arm only; the single exception is the
    // power swap. engine_holds_cur: the engine still has the arm's slot-1 item equipped (false when an arrival already replaced it).
    Fx place(unsigned h,Held what,Home home,std::uint64_t now,bool engine_holds_cur=true) noexcept {
        Fx fx;h&=1u;const unsigned o=h^1u;const Arm cur=a[h];
        if(home==Home::slot0 && a[o].home==Home::slot0) {a[o]={};fx.sword_left|=1u<<o;}   // one sword
        if(home==Home::slot1 && a[o].home==Home::slot1) {   // one native slot-1 item: a displaced gun stays in the other arm as its copy
            const Held old=a[o].what;a[o]=copyable(old)?Arm{old,Home::copy}:Arm{};
        }
        if(cur.home==Home::slot0 && home!=Home::slot0) {fx.sword_left|=1u<<h;sword_leaving=true;sword_leave_until=now+leave_ms;}
        else if(cur.home==Home::copy) fx.copy_displaced|=1u<<h;
        else if(cur.home==Home::slot1 && cur.what!=Held::none && is_weapon(what)) {
            if(slot1_only(cur.what)) {   // POWER SWAP: the power goes to the other arm when that one is empty, else it is put away
                a[h]={};
                if(empty(o)) {a[o]={cur.what,Home::slot1};rest_slot1=o;fx.power_moved|=1u<<o;if(!engine_holds_cur) fx.restore_slot1=true;}
                else if(engine_holds_cur) {fx.unequip_slot1=true;leave_kind=cur.what;leave_until=now+leave_ms;}
            } else if(home!=Home::slot1 && engine_holds_cur) {fx.unequip_slot1=true;leave_kind=cur.what;leave_until=now+leave_ms;}   // a gun the arm held, replaced by a weapon of the mod's
        }
        a[h]={what,home};
        if(home==Home::slot1) rest_slot1=h;else if(home==Home::slot0) rest_slot1=o;
        return fx;
    }

    // --- events: each touches its own arm ---------------------------------------------------------------------------------------------
    // The hand squeezed at a sword holster (or the lying sword) with the sword out of every hand. The sword draws into this arm; a power
    // it held moves (the swap) now, the sword itself arrives through native_changed().
    Fx sword_take(unsigned h,std::uint64_t now) noexcept {
        Fx fx;h&=1u;
        if(!free_for_weapon(h) || sword_holder()>=0) {fx.refused=true;return fx;}
        const unsigned o=h^1u;sword_leaving=false;
        if(a[h].what==Held::power) {
            const Held p=a[h].what;a[h]={};
            if(empty(o)) {a[o]={p,Home::slot1};fx.power_moved|=1u<<o;} else {fx.unequip_slot1=true;leave_kind=p;leave_until=now+leave_ms;}
        }
        rest_slot1=o;return fx;
    }
    // The sword is put away by the mod (its arm keeps the sticky seat).
    void sword_away(unsigned h) noexcept {h&=1u;if(a[h].home==Home::slot0) a[h]={};}
    // The hand squeezed at the gun holster: this arm will draw slot 1. Needs an empty arm and no slot-1 item anywhere.
    Fx hip_draw(unsigned h,std::uint64_t now,std::uint64_t rev) noexcept {
        Fx fx;h&=1u;
        if(!empty(h) || slot1_holder()>=0) {fx.refused=true;return fx;}
        rest_slot1=h;request(h,false,Held::none,now,rev);return fx;
    }
    // A hand takes a lying gun (or a guard's weapon). Native when the engine's slot 1 is free and the arm is empty (the equip then lands
    // in this arm); otherwise this arm's copy. A power in this arm moves to the other arm (swap); a gun picked up while the other arm
    // holds a power or gadget is a copy, never an overwrite.
    Fx pickup(unsigned h,Held kind,bool native_ok,std::uint64_t now,std::uint64_t rev) noexcept {
        Fx fx;h&=1u;
        if(!free_for_pickup(h) || !copyable(kind)) {fx.refused=true;return fx;}
        if(native_ok && native_gun(kind) && a[h].what!=Held::power && slot1_holder()<0 && !pend[h^1u].on) {
            if(a[h].home==Home::slot0) {a[h]={};fx.sword_left|=1u<<h;sword_leaving=true;sword_leave_until=now+leave_ms;}   // newest wins: the sword leaves this arm now, so the gun machine binds here
            rest_slot1=h;request(h,false,kind,now,rev);fx.native=true;return fx;
        }
        fx.merge(place(h,kind,Home::copy,now));return fx;
    }
    // A thrown gadget is caught: back to the inventory, this arm shows it once the engine equips it again.
    // v1046 (user, headset v1045: "the heart should behave as a normal weapon"): the catch is ticketed only after the game thread's take removed the
    // pickup (can_catch_gadget asks first, nothing is queued when it says no). It lands in THIS arm like a wheel choice: what the other arm held in slot 1
    // follows the arrival's newest-wins rules (a gun stays there as its copy, a power or gadget is put away); a sword drawn in this arm but not gripped leaves it.
    bool can_catch_gadget(unsigned h) const noexcept {h&=1u;return free_for_pickup(h) && !pend[h^1u].on;}
    Fx catch_gadget(unsigned h,std::uint64_t now,std::uint64_t rev) noexcept {
        Fx fx;h&=1u;
        if(!can_catch_gadget(h)) {fx.refused=true;return fx;}
        if(a[h].home==Home::slot0) {a[h]={};fx.sword_left|=1u<<h;sword_leaving=true;sword_leave_until=now+leave_ms;}
        rest_slot1=h;request(h,false,Held::none,now,rev);return fx;
    }
    // The hand let go of its copy (or a copy was lost): the arm is empty.
    void release(unsigned h) noexcept {h&=1u;if(a[h].home==Home::copy) a[h]={};}
    // Test/debug: set an arm's copy directly.
    Fx set_copy(unsigned h,Held kind,std::uint64_t now) noexcept {
        Fx fx;h&=1u;
        if(kind==Held::none) {release(h);return fx;}
        if(!copyable(kind)) {fx.refused=true;return fx;}
        return place(h,kind,Home::copy,now);
    }
    void reset() noexcept {*this=ArmSet{};}

    // --- the weapon wheel ---------------------------------------------------------------------------------------------------------------
    // Called each frame while a wheel is open: the old slot-1 holder keeps its seat even if the game empties slot 1 for the wheel.
    void wheel_open(std::uint64_t now) noexcept {wheel_ctx=true;wheel_ctx_until=now+wheel_ctx_ms;}
    void wheel_cancel() noexcept {if(!pend[0].wheel && !pend[1].wheel) wheel_ctx=false;}
    // The wheel opened by arm h closed on a choice. The item lands in h unless it is the one slot 1 already holds (wheel_choice).
    Fx wheel_commit(unsigned h,std::uint64_t now,std::uint64_t rev) noexcept {
        Fx fx;h&=1u;wheel_ctx=true;wheel_ctx_until=now+wheel_ctx_ms;request(h,true,Held::none,now,rev);return fx;
    }
    // The wheel closed on no item: nothing will arrive.
    void wheel_nothing(unsigned h) noexcept {pend[h&1u]={};if(!pend[0].on && !pend[1].on) wheel_ctx=false;}
    // Exact: the hooked equip named the chosen item and the caller compared it with the item slot 1 held at the commit.
    Fx wheel_choice(unsigned h,bool same,std::uint64_t now) noexcept {
        Fx fx;h&=1u;Pend& p=pend[h];
        if(!p.on || !p.wheel) {fx.refused=true;return fx;}
        if(!same) {p.chosen=2;return fx;}
        p.on=false;p.chosen=1;
        const int s=slot1_holder();
        if(s>=0 && static_cast<unsigned>(s)!=h && copyable(a[s].what) && !(a[h].home==Home::copy && a[h].what==a[s].what)) {
            fx.merge(place(h,a[s].what,Home::copy,now));fx.same_copy=true;
        } else if(s>=0 && static_cast<unsigned>(s)!=h && slot1_only(a[s].what)) {
            // v1090 (user, headset v1089: "a power active in the other hand should be placed into the new hand"): a power, gadget or Heart cannot be
            // copied, so it MOVES to the opening arm; the engine keeps it equipped, only the arm that draws slot 1 changes (as the power swap).
            fx.merge(place(h,a[s].what,Home::slot1,now,false));fx.power_moved|=1u<<h;
        }
        if(!pend[0].wheel && !pend[1].wheel && !pend[0].on && !pend[1].on) wheel_ctx=false;
        return fx;
    }

    // --- engine truth -------------------------------------------------------------------------------------------------------------------
    // Each control frame: what the engine reports. A slot-1 change counts once it has stopped changing for settle_ms.
    Fx native_changed(const Native& n,std::uint64_t now) noexcept {
        Fx fx=step(n,now);
        if(n.sword) {
            if(sword_holder()<0 && !(sword_leaving && now<sword_leave_until)) fx.merge(place(derive().sword_arm,Held::sword,Home::slot0,now,false));
        } else {sword_leaving=false;if(const int s=sword_holder();s>=0) a[static_cast<unsigned>(s)]={};}
        if(!seen_valid || n.slot1!=seen_kind || n.revision!=seen_rev) {seen_valid=true;seen_kind=n.slot1;seen_rev=n.revision;seen_tick=now;}
        if((!applied_valid || seen_kind!=applied_kind || seen_rev!=applied_rev) && now-seen_tick>=settle_ms) {
            bool deferred=false;fx.merge(apply_slot1(n.slot1,n.revision,now,deferred));
            if(!deferred) {applied_valid=true;applied_kind=seen_kind;applied_rev=seen_rev;}
        }
        return fx;
    }

private:
    void request(unsigned h,bool wheel,Held want,std::uint64_t now,std::uint64_t rev) noexcept {
        pend[(h&1u)^1u]={};   // the newest request owns the next arrival: an older one for the other arm is abandoned
        Pend& p=pend[h&1u];p={};p.on=true;p.wheel=wheel;p.want=want;p.ticket=++ticket_seq;p.deadline=now+pend_ms;p.revision=rev;p.since=now;
    }
    static bool compatible(Held want,Held got) noexcept {return want==Held::none || want==got || (slot1_only(want) && slot1_only(got));}
    Fx step(const Native& n,std::uint64_t now) noexcept {
        Fx fx;
        for(unsigned h=0;h<2;++h) {
            Pend& p=pend[h];if(!p.on) continue;
            if(now>p.deadline) {p.on=false;fx.expired|=1u<<h;continue;}
            // Fallback only: the hook gave no chosen item and the engine changed nothing: the same item, as the old 160 ms guess but later
            if(p.wheel && p.chosen==0 && now>=p.since+same_fallback_ms && n.revision==p.revision) fx.merge(wheel_choice(h,true,now));
        }
        if(wheel_ctx && now>wheel_ctx_until && !pend[0].on && !pend[1].on) wheel_ctx=false;
        if(sword_leaving && now>=sword_leave_until) sword_leaving=false;
        return fx;
    }
    Fx apply_slot1(Held k,std::uint64_t rev,std::uint64_t now,bool& deferred) noexcept {
        Fx fx;const int p=slot1_holder();
        if(leave_kind!=Held::none) {
            if(k==leave_kind && now<leave_until) {deferred=true;return fx;}   // asked for it to go: the engine has not caught up
            leave_kind=Held::none;
        }
        if(k==Held::none) {
            if(wheel_ctx) {deferred=true;return fx;}
            if(p>=0) a[static_cast<unsigned>(p)]={};
            return fx;
        }
        int h=-1;std::uint64_t best=~0ull;
        for(unsigned i=0;i<2;++i) if(pend[i].on && !(pend[i].wheel && pend[i].chosen==1) && rev!=pend[i].revision && compatible(pend[i].want,k) && pend[i].ticket<best) {best=pend[i].ticket;h=static_cast<int>(i);}
        if(h>=0) {
            const unsigned u=static_cast<unsigned>(h);pend[u].on=false;fx.arrived|=1u<<u;fx.merge(arrive(u,k,now));
            if(!pend[0].on && !pend[1].on) wheel_ctx=false;
            return fx;
        }
        if(p>=0) a[static_cast<unsigned>(p)].what=k;
        else fx.merge(place(derive().slot1_arm,k,Home::slot1,now,false));
        return fx;
    }
    // The ticketed equip landed: slot 1 now holds k natively, for arm h.
    Fx arrive(unsigned h,Held k,std::uint64_t now) noexcept {
        const int p=slot1_holder();
        if(native_gun(k) && p>=0 && slot1_only(a[static_cast<unsigned>(p)].what)) {
            if(static_cast<unsigned>(p)!=h) {   // a gun chosen for this arm while the other holds a power/gadget/Heart: the gun is a copy, the item is equipped again
                Fx fx=place(h,k,Home::copy,now,false);fx.restore_slot1=true;return fx;
            }
            if(empty(h^1u)) return place(h,k,Home::copy,now,false);   // the power swap: the gun is this arm's copy, the power goes to the other arm and is equipped again
            a[h]={};   // the other arm is occupied: the power is put away (the engine already replaced it)
        }
        return place(h,k,Home::slot1,now,false);
    }
};
} // namespace d2vr::arm
