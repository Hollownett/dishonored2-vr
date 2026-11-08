#pragma once
// Full-grip sword on the game thread (user 2026-10-13: hold the grip to hold a weapon; release at its holster to holster it,
// anywhere else to drop it). Static ABI, Steam 17576867 (docs/native-weapon-equip-research.md):
// - every equip goes through the player FSM's EquipChange state (53 right hand, 64 left): params 835590(out, item, code,
//   requester), request 8024d0(fsm, state, params). Codes: 0 holster, 4 draw the remembered item, 0x20 instant, 0x40 fast clip;
//   requester 0x5b = none. The FSM queue has no lock: game thread only.
// - no lock while the sword lies in the world (an anonymous lock may be saved with the game and leave the hand stuck): a
//   native redraw simply calls the dropped copy back (SwordGrip).
// - the dropped sword is the game's sword prop: FindDecl 1e030(entityDef type 0x2434170, name, 0, 0), SpawnEntityDefAt
//   8ae9b0(game, decl, origin, axis, owner, hidden, scale, linVel, angVel), removal 883f60(entity, remove event 0x39a6f30, 0).
//   Spawned this way its components never start (sim v407: identity flags 0x20000002) and Havok never moves it, not even with
//   the grab motor's velocity (sim v413: three writes, no motion). So the mod flies it: gravity, spin, the native world
//   sphere-cast for what it meets, and idEntity::Teleport 87d1e0(entity, origin, axis rows, 0) every game frame.
#include <windows.h>
#include "game_rig.hpp"
#include "native_rigid_body.hpp"
#include "native_melee.hpp"
#include "native_slash_speed.hpp"
#include "native_block_eligibility.hpp"
#include "native_world_trace.hpp"
#include "native_grab.hpp"
#include "ragdoll_safety.hpp"
#include "corpse_diagnostics.hpp"
#include "guard_probe.hpp"
#include "native_weapon_visibility.hpp"
#include "native_manual_equip_runtime.hpp"
#include "crash_observer.hpp"
#include "held_copy.hpp"
#include "native_ranged_fire.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <set>
#include <vector>
#include <string>
#include <string_view>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <cstring>

namespace d2vr {
// One step of a thrown or dropped rigid sword the mod moves itself (pure: the world is a callback). Game world, Z up.
struct SwordFlight {
    Vec3 pos{},vel{},spin_axis{};std::array<Vec3,3> axis{};double spin{};unsigned bounces{};bool flying{};
    double age{}; // seconds since launch
    static constexpr double gravity=9.81,radius=.04,max_flight_s=6.0; // a sword that never meets the world (off the map) stops here
    static bool finite(const SwordFlight& f) noexcept {
        const auto ok=[](const Vec3& v){return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);};
        return ok(f.pos) && ok(f.vel) && ok(f.spin_axis) && ok(f.axis[0]) && ok(f.axis[1]) && ok(f.axis[2]) && std::isfinite(f.spin);
    }
    // cast(from, to) -> the world hit, if any. Returns true while still flying.
    template<class Cast> bool step(double dt,Cast&& cast) {
        if(!flying) return false;
        age+=dt;
        if(age>=max_flight_s || !finite(*this)) {flying=false;vel={};return false;} // pos stays the last finite position
        vel.z-=gravity*dt;const auto next=pos+vel*dt;
        if(const std::optional<WorldTraceHit> hit=cast(pos,next)) {
            const auto n=hit->normal;
            if(!hit->static_support) {pos=next;return true;}
            if(hit->initial_overlap) {
                if(hit->normal_valid && n.z>=.7) {pos=hit->point+n*(radius+.01);land(n);return false;}
                pos=next;return true;
            }
            pos=hit->centre+n*.01;
            if(length(vel)>3 && bounces<2 && n.z<.7) {vel=(vel-n*(2*dot(vel,n)))*.3;spin*=.5;++bounces;} // glances off a wall
            else if(n.z>=.7) land(n);
            else if(const double into=dot(vel,n);into<0) vel=vel-n*into;
            return flying;
        }
        pos=next;
        if(spin!=0) {const double a=spin*dt,c=std::cos(a),s=std::sin(a);const auto u=spin_axis;
            for(auto& v:axis) v=v*c+cross(u,v)*s+u*(dot(u,v)*(1-c));}
        return true;
    }
    void land(Vec3 n) { // it lies on the surface: the blade in the surface plane, flat side down
        flying=false;vel={};
        auto blade=axis[0]-n*dot(axis[0],n);if(length(blade)<1e-3) blade=axis[1]-n*dot(axis[1],n);
        if(length(blade)<1e-3) blade=std::abs(n.x)<.9?Vec3{1,0,0}:Vec3{0,1,0};
        blade=blade*(1/length(blade));blade=blade-n*dot(blade,n);blade=blade*(1/length(blade));
        axis={blade,cross(n,blade),n};
    }
};
// A settled corpse a wake posted for (see NativeWeaponHands::wake_pending_).
struct WakeEntry {bool used{},retried{};std::uintptr_t entity{};std::uint32_t id{};unsigned side{};std::uint64_t at_ms{};};
// Dual wield visuals (NativeWeaponHands::service_mirror): the drawn pose each hand was last fed, one record per engine slot while the hands are
// mirrored (slot 0 = the sword, slot 1 = pistol/crossbow/...), and the copy entities (by def index).
struct HeldFeed {held_copy::Pose pose{};bool have{};std::chrono::steady_clock::time_point at{};held_copy::Motion speed{1,10};Vec3 root{};bool have_root{};}; // root: the player's model origin when the hand was drawn
struct HeldCopy {std::uintptr_t ent{};std::uint32_t id{};bool shown{};};
struct HeldSlot {
    static constexpr unsigned none=~0u;
    std::uintptr_t native{},cand{},owed{};std::uint32_t native_id{},cand_id{},owed_id{},fail_id{};unsigned def{none},cand_def{none};
    held_copy::OffsetFilter offset;bool locked{},we_hid{},cand_ok{},have_cmd{};held_copy::Pose cmd{};std::uint64_t looked{},owed_looked{};
    unsigned side{}; // the hand the offset was measured at (dual wield: the native weapon may ride the other hand)
    void drop() {native=0;native_id=0;def=none;offset.reset();locked=we_hid=have_cmd=false;} // looked/cand/owed/fail_id outlive the item
};
// Dual wield, row A (docs/dual-wield-two-hands-plan.md): entities the mod spawns from *_midnight.def / guard defs are pooled (hidden and shown
// again, removed only on session stop: the sword copy's rule), a gun thrown into the world is one of two LyingGun, and a copy a hand holds is a HeldSide.
struct PoolEnt {const char* def{};std::uintptr_t ent{};std::uint32_t id{};bool in_use{},shown{};};
struct LyingGun {
    bool present{},native{};int pe{-1};const char* def{};unsigned kind{};SwordFlight flight{};LARGE_INTEGER qpc{};std::uint64_t since{},stow_ms{},checked_ms{};
    std::vector<std::uintptr_t> struck; // v783: the guards this throw stunned (thrown_stun)
    // native only: what brings it back (the inventory record is found again by decl and entity id; the record pointer moves when the list grows)
    std::uintptr_t item_decl{},ammo_decl{},entity{};std::uint32_t item_spawn{};
};
struct HeldSide {int pe{-1};bool shown{},have_cmd{};const char* failed{};unsigned tries{};std::uint64_t retry_at{};held_copy::Pose cmd{};}; // failed: the def that did not spawn; retried every 250 ms, 12 times
struct Adopt {int pe{-1};const char* def{};std::uint64_t tick{};}; // v1113: a lying gun picked up as a copy, still shown, waiting for held_copy() to want it (no hide-and-reshow flicker)
struct HeldPub {bool valid{};held_copy::Pose pose{};std::uint64_t tick{};std::uintptr_t ent{};std::uint32_t id{};};
// v1101: the copy a hand draws (the mirror copy of a natively equipped weapon, or a mod-owned held copy), as the render thread's weapon
// publisher needs it for draw-time placement (src/dxgi_probe.cpp publish_weapon_draw -> weapon_draw_place.hpp). `read` is the hand
// pose the copy was last placed from (the same 15-float model-frame layout: origin, three axis rows, scales), `serial` counts those
// placements: exactly the native weapon's (tick frame, hand read, read serial) pair, so the same pair-validity rules apply. The draw's
// model frame itself is read live from entity+0xf0 -> model+0x1860 by the publisher, like the native weapon's.
struct CopyDraw {bool valid{};std::uintptr_t entity{};std::uint32_t id{};std::array<float,15> read{};std::array<float,15> offset{};bool offset_valid{};std::uint64_t serial{};std::uint64_t write_serial{};unsigned kind{};};
// A lying gun as the input thread sees it (NativeWeaponHands::gun_copy).
struct GunCopyInfo {bool present{},flying{},native{};unsigned kind{};std::array<float,3> pos{};std::array<float,9> axis{1,0,0,0,1,0,0,0,1};const char* def{};std::uint64_t since{};};
// v1102: a copy placement's report to the draw-time weapon publisher (src/weapon_draw_place.hpp WriteLog): the physical hand
// that draws the copy, its entity, the exact hand pose it was placed from and the writer kind (2 mirror, 3 mod-held). v1104: returns
// the WriteLog serial that identifies that note (0 when the note was dropped), so a fallback candidate built from CopyDraw is ordered
// in the same serial space as the ring instead of the CopyDraw's own serial.
using CopyDrawWrite=std::uint64_t (*)(unsigned side,std::uintptr_t entity,std::uint32_t id,const float* read15,unsigned kind) noexcept;
class NativeWeaponHands {
    friend struct NativeHipVisibilityFixture;
    friend struct NativeWeaponExchangeFixture;
    friend struct WakeBudgetCheck;
    friend struct GunCopiesFixture;
public:
    enum class Op : unsigned {draw,sheathe,drop,pickup,recall,stow,gun_draw,gun_holster,shove,ragdoll,yank,wake,living_start,living_pull,
        gun_drop,gun_pickup,slot1_restore,test_equip,slot1_unequip,carry_left_redraw,real_copy}; // dual wield (row A): see gun_drop / gun_pickup / slot1_restore below; test_equip: the "equip" test command; slot1_unequip: the Heart let go (v771)
    // G6 (plan Task 14): the hold of a living NPC pulls him (the existing teleport 0x87d1e0, horizontal steps toward the hand) only when the guard_probe
    // experiments (plan Task 13, docs/BONELAB-FEEL-GUARD-PULL-RE.md) showed no snap-back and no yaw pop. Set from that verdict.
    static constexpr bool living_pull_enabled=false;
    // shove: the game's TakeDamage on target (an arkNPC) with damage decl `decl` (a name under the damage type), from pos
    // along vel (the push direction), amount 0 = the decl's own.
    struct Request {Op op{};std::array<float,3> pos{},vel{},spin{};std::array<float,9> axis{1,0,0,0,1,0,0,0,1};std::uintptr_t target{};const char* decl{};float amount{};unsigned side{};bool free_look{},as_copy{};std::uint64_t manual_generation{},manual_xr_epoch{};NativePlayerStateLease manual_owner;};
    // The exact sword the hand holds (the cinematic copy of sword_player.md6: refframe + animation, no physics, no audio). The
    // howler sword prop used before carried an audio component; spawned and removed 47 times in a headset session, the audio
    // update crashed on a freed emitter (2026-10-13, rva 0xc3e00 from 0x709de7). One copy per level, hidden and shown again.
    static constexpr const char* sword_copy_decl="models/campaign/dunwall/midnightassets/sword_player_midnight.def";
    // Any thread: queued for the next game-thread service.
    static bool request(const Request& r) {
        const auto ok=[](const auto& a){return std::all_of(a.begin(),a.end(),[](float v){return std::isfinite(v);});};
        if(!ok(r.pos) || !ok(r.vel) || !ok(r.axis) || !ok(r.spin)) return false; // NaN into Teleport/spawn reaches every physics body (REA Q4)
        Request q=r;if(q.op==Op::gun_drop && q.decl) q.decl=intern(q.decl); // a def name from the caller's stack or a game string must outlive the queue
        std::lock_guard<std::mutex> lock(m_);
        if(queue_.size()<8) {queue_.push_back(q);if(r.op==Op::gun_holster)++holster_enqueued_;return true;}
        if(r.op==Op::gun_holster)++holster_dropped_;
        return false;
    }
    static NativeWeaponVisibility::Snapshot visibility() {
        std::lock_guard<std::mutex> lock(m_);auto snapshot=visibility_;
        snapshot.holster_enqueued=holster_enqueued_;snapshot.holster_dropped=holster_dropped_;
        snapshot.holster_serviced=holster_serviced_;snapshot.holster_request_returned=holster_returned_;snapshot.holster_failed=holster_failed_;
        return snapshot;
    }
    // The dropped copy, published by the game thread for the input thread (pickup reach, recall).
    struct Copy {bool present{},flying{};std::array<float,3> pos{},blade{};std::uint64_t since{};std::string trace;};
    static Copy copy() {std::lock_guard<std::mutex> l(m_);return published_;}
    inline static std::atomic<std::uint64_t> ops{},failures{},spawned{},removed{},thrown_hits{},flight_frames{},services{},props_marked{},prop_hits{};
    inline static std::atomic<float> thrown_peak{}; // launch speed of the last drop, m/s
    inline static std::atomic<bool> phys_decl_ok{};inline static std::atomic<std::uint64_t> ragdolls{},yanks{},wakes{},holds_down{},squeezes{},guard_grabs{};
    // START_RAGDOLL budget (Bonelab feel Task 11). Every wake of a settled corpse asks post_budget first: one post per wake, one retry after 1 s
    // if the primary body still does not read dynamic, then a refusal (take_wake_refusal: the input thread pulses the hand), global ceiling 2/s.
    // build/no-ragdoll-posts disables it (dxgi_probe), which also stops the legacy living-guard yank's posts (no ceiling for those, deviation 4).
    // wake_diag / wake_lockout are set by dxgi_probe (the NativeGrab's diagnostics and runaway lockout); null in a bare process.
    inline static EventPostBudget post_budget;
    inline static CorpseDiagnostics* wake_diag=nullptr;
    inline static EntityLockout* wake_lockout=nullptr;
    static bool take_wake_refusal(unsigned side) noexcept {return wake_refused_[side&1u].exchange(false);}
    inline static std::atomic<float> last_guard_d{},last_guard_up{}; // diagnostics: the nearest living guard to the hand at the last search
    inline static std::atomic<int> last_ragdoll{};inline static std::atomic<int> last_shove{-2}; // -2 none, -1 no decl, 0 applied, 1 rejected, 2 fault
    template<class Read> static bool verify(std::uintptr_t base,Read& read) {
        static const std::array<std::pair<std::uintptr_t,std::array<unsigned char,16>>,6> pins{{
            {0x835590,{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x20,0x8b}},
            {0x8024d0,{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x30,0x48}},
            {0x1e030,{0x44,0x88,0x44,0x24,0x18,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57}},
            {0x8ae9b0,{0x40,0x57,0x41,0x56,0x41,0x57,0x48,0x83,0xec,0x40,0x48,0xc7,0x44,0x24,0x20,0xfe}},
            {0x883f60,{0x48,0x83,0xec,0x28,0x45,0x33,0xc9,0xe8,0xc4,0x00,0x00,0x00,0x48,0x83,0xc4,0x28}},
            {0x87d1e0,{0x48,0x83,0xec,0x38,0xc7,0x44,0x24,0x20,0x00,0x00,0x00,0x00,0xe8,0x0f,0x00,0x00}}}};
        for(const auto& [rva,code]:pins) {std::array<unsigned char,16> got{};if(!read(base+rva,got.data(),got.size()) || got!=code) return false;}
        return true;
    }
    // Game thread (NativeInteractionAim stage-0 scope), gameplay only.
    template<class Read> static void service(std::uintptr_t base,Read& read) {
        std::vector<Request> work;{std::lock_guard<std::mutex> l(m_);work.swap(queue_);}
        ++services;static const bool verified=verify(base,read);
        static const bool decl_probe=[&]{phys_decl_ok=phys_damage(base,read)!=0;return true;}();(void)decl_probe; // live check of the damage-type FindDecl
        if(!verified) {failures+=work.size();return;}
        auto& manual_equip=NativeManualEquipRuntime::process_owner();
        const auto manual_status=manual_equip.status();
        if(manual_status.installed)manual_equip.service_game_callback(player_fsm(base,read));
        for(const auto& r:work) {
            ++ops;CrashOperation operation("native weapon hands");
            bool ok=true;
            switch(r.op) {
            case Op::draw: ok=equip(base,read,0x04);break;
            case Op::sheathe: ok=equip(base,read,0x00);break;
            case Op::drop: // out of the hand at once; its damage identity is taken first, while the sword is still in the hand
                sword_=NativeMelee::sword(base,read);
                // user 2026-10-14: a thrown sword kills an unaware guard outright. 1000 = generic_fatality's m_maxDamage, the game's
                // own fatality amount; the health component (0x718fe0) clamps it to his hp, so he dies the game's own death.
                if(sword_) sword_->unaware_amount=1000.f;
                ok=equip(base,read,0x20);
                if(ok) {if(alive(base,read)) {visible(base,read,copy_,true);shown_=true;} else ok=spawn(base,read,r);}
                if(ok) launch(r);
                break;
            case Op::pickup: stash(base,read);ok=equip(base,read,0x44);break;
            case Op::recall: stash(base,read);break;
            case Op::stow: ok=equip(base,read,0x20);break; // a drop met a running equip clip, which cancels an instant unequip
            case Op::gun_draw:
                ok=service_gun_draw(base,read,r,manual_equip);
                break;
            case Op::gun_holster:
                ++holster_serviced_;ok=equip_holster(base,read,r);
                if(ok)++holster_returned_;else ++holster_failed_;
                break;
            case Op::shove: ok=shove(base,read,r);break;
            case Op::ragdoll: ok=ragdoll(base,read,r.target,r.vel);break;
            case Op::yank: ok=yank(base,read,r);break;
            case Op::wake: ok=wake(base,read,r);break;
            case Op::living_start: ok=living_start(base,read,r);break;
            case Op::living_pull: ok=living_pull(base,read,r);break;
            case Op::gun_drop: try {ok=gun_drop(base,read,r);} catch(...) {ok=false;} if(!ok) ++gun_failed;break;
            case Op::gun_pickup: try {ok=gun_pickup(base,read,r,manual_equip);} catch(...) {ok=false;} if(!ok) ++gun_failed;break;
            case Op::slot1_restore: try {ok=slot1_restore(base,read);} catch(...) {ok=false;} if(ok) ++slot1_restores;else ++gun_failed;break;
            case Op::test_equip: try {ok=test_equip(base,read,r.decl);} catch(...) {ok=false;} break;
            case Op::real_copy: {CrashOperation operation("real copy experiment");try {ok=real_copy(base,read,r.decl,r.pos);} catch(...) {ok=false;}} break;
            case Op::carry_left_redraw: try {ok=carry_redraw(base,read,r.target);} catch(...) {ok=false;} break; // v1041/v1044: the left item CarryCorpse put away, equipped again
            case Op::slot1_unequip: try {ok=dual_wield_.load(std::memory_order_relaxed) && equip(base,read,0x20,64);} catch(...) {ok=false;} break; // the engine's own instant unequip (as a native gun_drop)
            }
            if(!ok) ++failures;
        }
        if(copy_ && shown_ && flight_.flying) {CrashOperation operation("native weapon flight");try {fly(base,read);} catch(...) {flight_.flying=false;}}
        {CrashOperation operation("native hip holster");try {service_hip(base,read);} catch(...) {}}
        {CrashOperation operation("native mirrored hands");try {service_mirror(base,read);} catch(...) {}}
        {CrashOperation operation("native gun copies");try {service_guns(base,read);} catch(...) {}}
        {CrashOperation operation("native copy upgrades");try {service_upgrades(base,read);} catch(...) {}}
        hold_down(base,read);
        service_wake_retries(base,read,clock_ms());
        service_guard_probe(base,read);
        if(const auto t=NativeGrab::take_thrown()) {CrashOperation operation("native thrown prop");try {thrown_prop(base,read,*t);} catch(...) {}}
        if(prop_.until && GetTickCount64()<prop_.until) {CrashOperation operation("native thrown prop flight");try {fly_prop(base,read);} catch(...) {prop_.until=0;}}
        publish(base,read);
        const auto visibility_now=GetTickCount64();
        if(visibility_now-visibility_tick_>=100) {
            visibility_tick_=visibility_now;
            NativeWeaponVisibility::Snapshot snapshot{};
            try {snapshot=NativeWeaponVisibility::capture(base,read,hip_id_,visibility_now);} catch(...) {snapshot.tick=visibility_now;}
            std::lock_guard<std::mutex> lock(m_);
            snapshot.holster_enqueued=holster_enqueued_;snapshot.holster_dropped=holster_dropped_;
            snapshot.holster_serviced=holster_serviced_;snapshot.holster_request_returned=holster_returned_;snapshot.holster_failed=holster_failed_;
            visibility_=snapshot;
        }
    }
private:
    inline static std::mutex m_;inline static std::vector<Request> queue_;static Copy published_; // defined after the class: clang-cl needs Copy's member initializers complete
    inline static NativeWeaponVisibility::Snapshot visibility_{};
    inline static std::uint64_t holster_enqueued_{},holster_dropped_{},visibility_tick_{};
    inline static std::atomic<std::uint64_t> holster_serviced_{},holster_returned_{},holster_failed_{};
    inline static std::uintptr_t copy_{};inline static std::uint32_t copy_id_{};inline static std::uint64_t copy_since_{};inline static bool shown_{};
    inline static SwordFlight flight_{};inline static LARGE_INTEGER flight_qpc_{};
    // A thrown sword striking a guard is a sword hit: the engine's TakeDamage with the player's own sword decl (NativeMelee),
    // once per guard per throw. Its path this frame and its blade (+-0.6 m around the prop's origin, which is not known to be
    // the hilt) are swept against a body capsule at the guard's drawn model origin (feet to head, 0.28 m): a living guard has
    // no ragdoll bodies to test (sim v418: its handle list is garbage until he dies or is knocked out).
    inline static std::optional<NativeMelee::Sword> sword_{};inline static std::vector<std::uintptr_t> struck_{};
    inline static std::string trace_{}; // diagnostics: the last throw's launch and what it met
    static std::string text(Vec3 v) {char b[64];std::snprintf(b,sizeof(b),"(%.2f %.2f %.2f)",v.x,v.y,v.z);return b;}
    static void launch(const Request& r) {
        flight_={};flight_.pos={r.pos[0],r.pos[1],r.pos[2]};flight_.vel={r.vel[0],r.vel[1],r.vel[2]};
        for(unsigned i=0;i<3;++i) flight_.axis[i]={r.axis[i*3],r.axis[i*3+1],r.axis[i*3+2]};
        const auto side=cross(flight_.vel,Vec3{0,0,1});const double speed=length(flight_.vel);
        if(speed>4 && length(side)>1e-3) {flight_.spin_axis=side*(1/length(side));flight_.spin=-std::min(speed*1.6,16.);} // tumbles end over end
        trace_="launch "+text(flight_.pos)+" v"+text(flight_.vel);
        copy_since_=GetTickCount64();flight_.flying=true;QueryPerformanceCounter(&flight_qpc_);struck_.clear();thrown_peak=static_cast<float>(speed);
    }
    template<class Read> static void fly(std::uintptr_t base,Read& read) {
        static const bool trace_ok=NativeWorldTrace::verify(base,read),melee_ok=NativeMelee::verify(base,read);
        LARGE_INTEGER qpc{},freq{};QueryPerformanceCounter(&qpc);QueryPerformanceFrequency(&freq);
        const double dt=std::min(static_cast<double>(qpc.QuadPart-flight_qpc_.QuadPart)/static_cast<double>(freq.QuadPart),.05);
        if(dt<.001) return;
        flight_qpc_=qpc;
        const auto game=rig::value<std::uintptr_t>(read,base+0x39a5290);
        const auto world=game?rig::value<std::uintptr_t>(read,*game+NativeWorldTrace::world_offset):std::nullopt;
        if(!trace_ok || !world || !*world || !alive(base,read)) return;
        const auto from=flight_.pos;
        flight_.step(dt,[&](Vec3 a,Vec3 b) {
            auto hit=NativeWorldTrace::sphere_cast(base,read,reinterpret_cast<NativeWorldTrace::Cast>(base+NativeWorldTrace::cast_rva),
                reinterpret_cast<NativeWorldTrace::Construct>(base+NativeWorldTrace::sphere_ctor_rva),*world,a,b,SwordFlight::radius);
            if(hit && trace_.size()<600) trace_+=" hit "+text(hit->centre)+" n"+text(hit->normal)+(hit->initial_overlap?" overlap":"")+
                " e"+std::to_string(hit->entity?(rig::value<std::uintptr_t>(read,hit->entity).value_or(base)-base):0);
            if(hit && hit->entity && rig::typed(read,base,hit->entity,NativeMelee::npc_vtable_rva)) hit->static_support=false;
            return hit;
        });
        ++flight_frames;
        const double speed=length(flight_.pos-from)/dt;
        if(speed>=3 && melee_ok && sword_) {int fault{};
            if(strike(base,read,*game,from,flight_.pos,flight_.axis[0],.6,*sword_,struck_,fault)) {++thrown_hits;flight_.vel=flight_.vel*-.15;flight_.spin*=.3;} // the blade stops in the body and drops
            if(fault==2) sword_.reset();}
        if(!SwordFlight::finite(flight_)) {flight_.flying=false;return;} // never hand the engine a non-finite transform: stay at the last one
        alignas(16) std::array<float,3> origin{float(flight_.pos.x),float(flight_.pos.y),float(flight_.pos.z)};
        alignas(16) std::array<float,9> axis{};
        for(unsigned i=0;i<3;++i) {axis[i*3]=float(flight_.axis[i].x);axis[i*3+1]=float(flight_.axis[i].y);axis[i*3+2]=float(flight_.axis[i].z);}
        reinterpret_cast<void (__fastcall*)(std::uintptr_t,const float*,const float*,int)>(base+0x87d1e0)(copy_,origin.data(),axis.data(),0);
    }
    // The first damageable guard whose body capsule (drawn model origin = feet, 0.1-1.75 m, r 0.34) the path from->p or the
    // segment p +- axis*half touches gets the hit; 0 if none. One hit per guard per throw (struck).
    template<class Read> static std::uintptr_t strike(std::uintptr_t base,Read& read,std::uintptr_t game,Vec3 from,Vec3 p,Vec3 axis,double half,
            const NativeMelee::Sword& s,std::vector<std::uintptr_t>& struck,int& fault) {
        std::array<std::uintptr_t,0x3fff> registry{};if(!read(game+0x3130b0,registry.data(),sizeof(registry))) return 0;
        for(unsigned i=1;i<registry.size();++i) {
            const auto npc=registry[i];
            if(!rig::pointer(npc) || !rig::typed(read,base,npc,NativeMelee::npc_vtable_rva) || std::find(struck.begin(),struck.end(),npc)!=struck.end()) continue;
            const auto model=rig::value<std::uintptr_t>(read,npc+0xf0);
            const auto at=model && rig::pointer(*model)?rig::value<std::array<float,3>>(read,*model+0x1860):std::nullopt;
            if(!at) continue;
            const Vec3 feet{(*at)[0],(*at)[1],(*at)[2]};
            if(std::hypot(feet.x-p.x,feet.y-p.y)>3 || !NativeMelee::damageable(base,read,npc)) continue;
            Vec3 q{};bool touched=false;
            for(const auto& [a,b]:{std::pair{from,p},std::pair{p-axis*half,p+axis*half}}) // the path, then the blade
                for(unsigned k=0;k<=8 && !touched;++k) {
                    const auto c=a+(b-a)*(k/8.);const Vec3 spine{feet.x,feet.y,std::clamp(c.z,feet.z+.1,feet.z+1.75)};
                    if(length(c-spine)<=.34) {touched=true;q=c;}
                }
            if(!touched) continue;
            const auto d=p-from;const auto dir=d*(1/std::max(length(d),1e-6));const auto f=[](double v){return static_cast<float>(v);};
            const MeleeHitRequest hit{npc,{f(q.x),f(q.y),f(q.z)},{f(-dir.x),f(-dir.y),f(-dir.z)},{f(dir.x),f(dir.y),f(dir.z)},GetTickCount64()};
            fault=NativeMelee::apply(base,read,s,hit);struck.push_back(npc);
            return fault==0?npc:0;
        }
        return 0;
    }
    // A prop the hands threw (NativeGrab::take_thrown). A native throwable (arkCpntRigidBodyThrowable 0x1b64b68: bottles, cups)
    // gets the game's own thrown state 4c94e0(component): thrower = player, +0x438 |= 0x48, so its contact damage
    // (phys_damage above m_velocityDamageThreshold 2 m/s, instead of 15 m/s for a drop) and AI reactions are the game's.
    // Other dynamic props have no thrown damage at all: the mod sweeps them for 2.5 s against the guards and applies the
    // game's phys_damage (NPC_HITREACT_BIG) with an amount from the speed.
    struct PropFlight {std::uintptr_t entity{};std::uint32_t id{};Vec3 last{};LARGE_INTEGER qpc{};std::uint64_t until{};std::vector<std::uintptr_t> struck;};
    static PropFlight prop_; // defined after the class: clang-cl needs PropFlight's member initializers complete
    template<class Read> static void thrown_prop(std::uintptr_t base,Read& read,const NativeGrab::Thrown& t) {
        const auto game=rig::value<std::uintptr_t>(read,base+0x39a5290);
        if(!game || !t.entity_id || (t.entity_id>>16)>=0x3fff || rig::value<std::uintptr_t>(read,*game+0x3130b0+(t.entity_id>>16)*8)!=t.entity ||
           rig::value<std::uint32_t>(read,t.entity+0xa8)!=t.entity_id || GetTickCount64()-t.tick>500) return;
        if(rig::typed(read,base,t.component,0x1b64b68) && rig::value<std::uintptr_t>(read,t.component+0x10)==t.entity) {
            static const bool pinned=[&]{std::array<unsigned char,16> got{};const std::array<unsigned char,16> want{0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0x48,0x8b,0x0d};
                return read(base+0x4c94e0,got.data(),got.size()) && got==want;}();
            if(pinned) {reinterpret_cast<void (__fastcall*)(std::uintptr_t)>(base+0x4c94e0)(t.component);++props_marked;}
            return;
        }
        prop_={t.entity,t.entity_id,{},{},GetTickCount64()+2500,{}};
        if(const auto body=native_rigid_body(read,base,t.entity)) prop_.last=body->pose.position;else prop_.until=0;
        QueryPerformanceCounter(&prop_.qpc);
    }
    template<class Read> static void fly_prop(std::uintptr_t base,Read& read) {
        const auto body=native_rigid_body(read,base,prop_.entity);
        if(!body || body->entity_id!=prop_.id) {prop_.until=0;return;}
        LARGE_INTEGER qpc{},freq{};QueryPerformanceCounter(&qpc);QueryPerformanceFrequency(&freq);
        const double dt=static_cast<double>(qpc.QuadPart-prop_.qpc.QuadPart)/static_cast<double>(freq.QuadPart);if(dt<.002) return;
        const auto from=prop_.last,p=body->pose.position;prop_.last=p;prop_.qpc=qpc;
        const double speed=length(p-from)/dt;
        int fault{};const Vec3 half=body->bounds?(body->bounds->maximum-body->bounds->minimum)*.5:Vec3{};
        if(thrown_stun(base,read,from,p,{0,0,1},std::min(length(half),.5),speed,prop_.entity,prop_.id,prop_.struck,fault)) ++prop_hits;
        if(fault==2) prop_.until=0;
    }
public:
    // A thrown thing that is no native throwable (a prop, a gun, a gadget) striking a guard: the game's phys_damage (NPC_HITREACT_BIG,
    // the stagger a bottle gives), at 3 m/s or more, once per guard per throw. v783 (user, headset v791: "any weapon but the sword, thrown
    // or swung into a guard, stuns him for a moment, as a bottle does"). true = a guard was struck.
    template<class Read> static bool thrown_stun(std::uintptr_t base,Read& read,Vec3 from,Vec3 p,Vec3 axis,double half,double speed,
            std::uintptr_t inflictor,std::uint32_t inflictor_id,std::vector<std::uintptr_t>& struck,int& fault) {
        if(speed<3) return false; // every held gun runs this each service: a slow hand costs nothing
        static const bool melee_ok=NativeMelee::verify(base,read);if(!melee_ok) return false;
        const auto decl=phys_damage(base,read);const auto game=rig::value<std::uintptr_t>(read,base+0x39a5290);
        const auto player=game?rig::value<std::uintptr_t>(read,*game+0x3130b0):std::nullopt;if(!decl || !player) return false;
        NativeMelee::Sword s{*player,inflictor,0,decl,rig::value<std::uint32_t>(read,*player+0xa8).value_or(0),inflictor_id,
            static_cast<float>(std::clamp(speed*.6,2.,8.))}; // ponytail: 2-8 hp by speed (a guard has 30-40); tune in headset
        return strike(base,read,*game,from,p,axis,half,s,struck,fault)!=0;
    }
private:
    // Entity events (ARK_MSG_*): payload = 8112c0(eventManager 810240() = game+0x1e8968, +0xc8e8, msg, size, target id), filled
    // by the caller, delivered by the game. START_RAGDOLL 0xb9 {u64 0, u64 0} is what DropCorpse/Far Reach post (829a10, 43fe94);
    // VELOCITY_IMPULSE 0x149 {v[3], w[3], u16 flags} the generic set-velocity event (docs/native-hold-object.md).
#ifdef D2VR_TESTING
    inline static void* (*post_hook)(unsigned msg,unsigned size,std::uint32_t target)=nullptr;   // tests: replaces the engine event manager
    inline static std::uint64_t (*clock_hook)()=nullptr;
#endif
    static std::uint64_t clock_ms() {
#ifdef D2VR_TESTING
        if(clock_hook) return clock_hook();
#endif
        return GetTickCount64();
    }
    template<class Read> static void* post(std::uintptr_t base,Read& read,unsigned msg,unsigned size,std::uint32_t target) {
#ifdef D2VR_TESTING
        if(post_hook) return post_hook(msg,size,target);
#endif
        static const bool pinned=[&]{std::array<unsigned char,14> a{},b{};
            const std::array<unsigned char,14> want_a{0x48,0x8b,0x05,0x49,0x50,0x19,0x03,0x48,0x05,0x68,0x89,0x1e,0x00,0xc3};
            const std::array<unsigned char,14> want_b{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x44,0x89,0x4c,0x24};
            return read(base+0x810240,a.data(),a.size()) && a==want_a && read(base+0x8112c0,b.data(),b.size()) && b==want_b;}();
        if(!pinned) return nullptr;
        const auto manager=reinterpret_cast<std::uintptr_t (__fastcall*)()>(base+0x810240)();
        return manager?reinterpret_cast<void* (__fastcall*)(std::uintptr_t,unsigned,unsigned,std::uint32_t)>(base+0x8112c0)(manager+0xc8e8,msg,size,target):nullptr;
    }
    template<class Read> static bool ragdoll(std::uintptr_t base,Read& read,std::uintptr_t npc,std::array<float,3> velocity) {
        if(post_budget.disabled()) {last_ragdoll=-3;return false;} // build/no-ragdoll-posts
        const auto id=rig::value<std::uint32_t>(read,npc+0xa8);
        if(!id || !rig::typed(read,base,npc,NativeMelee::npc_vtable_rva) || !living(read,npc)) {last_ragdoll=-1;return false;}
        auto* p=post(base,read,0xb9,0x10,*id);if(!p) {last_ragdoll=-2;return false;}
        std::memset(p,0,0x10);++ragdolls;last_ragdoll=1;
        if(velocity[0]!=0 || velocity[1]!=0 || velocity[2]!=0) if(auto* v=static_cast<unsigned char*>(post(base,read,0x149,0x1c,*id))) {
            std::memset(v,0,0x1c);std::memcpy(v,velocity.data(),12);last_ragdoll=2;}
        return true;
    }
    // The hand pulling at a living guard's upper body (capsule at his drawn model origin: 0.8-1.85 m up, 0.5 m; pos = where the
    // hand squeezed): the game's
    // shove reaction (DAMAGETYPE_SHOVED, no damage: he knows who did it) and START_RAGDOLL with the hand's pull (max 6 m/s).
    // Sim v424: the bodies go dynamic within 1 s, he is on the floor at 2 s and stands up by himself at 4-6 s.
    // Alive and conscious (+0xd1: 0x08 dead, 0x20 unconscious). Not NativeMelee::damageable: some guards carry no health
    // flag (+0xd7 = 0, sim v435) yet ragdoll and recover like any other.
    template<class Read> static bool living(Read& read,std::uintptr_t npc) {
        const auto state=rig::value<unsigned char>(read,npc+0xd1);return state && !(*state&0x28);
    }
    // The living guard whose upper body (0.8-1.85 m up, within `reach`) is at the hand, or 0.
    template<class Read> static std::uintptr_t guard_at(std::uintptr_t base,Read& read,Vec3 hand,double reach) {
        const auto game=rig::value<std::uintptr_t>(read,base+0x39a5290);if(!game) return 0;
        std::array<std::uintptr_t,0x3fff> registry{};if(!read(*game+0x3130b0,registry.data(),sizeof(registry))) return 0;
        std::uintptr_t best{};double best_d=reach,nearest_d=99,nearest_up=0;
        for(unsigned i=1;i<registry.size();++i) {
            const auto npc=registry[i];
            if(!rig::pointer(npc) || !rig::typed(read,base,npc,NativeMelee::npc_vtable_rva)) continue;
            const auto model=rig::value<std::uintptr_t>(read,npc+0xf0);
            const auto at=model && rig::pointer(*model)?rig::value<std::array<float,3>>(read,*model+0x1860):std::nullopt;
            if(!at) continue;
            const double up=hand.z-(*at)[2],d=std::hypot(hand.x-(*at)[0],hand.y-(*at)[1]);
            if(d<nearest_d && living(read,npc)) {nearest_d=d;nearest_up=up;}
            if(up<.8 || up>1.85 || d>=best_d || !living(read,npc)) continue;
            best=npc;best_d=d;
        }
        last_guard_d=static_cast<float>(nearest_d);last_guard_up=static_cast<float>(nearest_up);
        return best;
    }
    // The guard a squeeze took hold of (wake runs on every fresh squeeze): a yank in the same squeeze pulls him even after he
    // stepped back (his personal-space reaction moved him 2.5 m in a second, sim v433), while he is still within arm's length.
    inline static std::array<std::uintptr_t,2> grabbed_{};inline static std::array<std::uint64_t,2> grabbed_at_{};
    template<class Read> static bool yank(std::uintptr_t base,Read& read,const Request& r) {
        const Vec3 hand{r.pos[0],r.pos[1],r.pos[2]};const auto side=r.side&1u;
        std::uintptr_t best{};
        // G6 (Task 14): a hard pull out of a living hold names him: the hold's NPC ragdolls, wherever the hand now is.
        if(r.target && rig::pointer(r.target) && rig::typed(read,base,r.target,NativeMelee::npc_vtable_rva) && living(read,r.target)) best=r.target;
        else if(grabbed_[side] && GetTickCount64()-grabbed_at_[side]<=2000 && living(read,grabbed_[side])) {
            const auto model=rig::value<std::uintptr_t>(read,grabbed_[side]+0xf0);
            const auto at=model && rig::pointer(*model)?rig::value<std::array<float,3>>(read,*model+0x1860):std::nullopt;
            if(at && std::hypot(hand.x-(*at)[0],hand.y-(*at)[1])<1.6) best=grabbed_[side];
        }
        if(!best) best=guard_at(base,read,hand,.5);
        grabbed_[side]=0;
        if(!best) return true; // nobody there: the hand just moved fast
        if(guard_probe_fire(base,read,best,hand,r)) return true; // dev knob build/guard-probe: one lever experiment instead of the pull
        Vec3 pull{r.vel[0],r.vel[1],r.vel[2]};if(length(pull)>6) pull=pull*(6/length(pull));
        Request s=r;s.op=Op::shove;s.target=best;s.decl="components/base/damage/shove_damage.damage";
        s.vel={float(-pull.x/std::max(length(pull),1e-6)),float(-pull.y/std::max(length(pull),1e-6)),0}; // hit from the side he is pulled to
        shove(base,read,s);
        const bool ok=ragdoll(base,read,best,{float(pull.x),float(pull.y),float(pull.z)});
        if(ok) {++yanks;NativeGrab::yanked(r.side,best,GetTickCount64()+1500);} // the squeezing hand keeps hold of him
        return ok;
    }
    // G6 (Task 14): a living hold starts with today's shove (shove_damage, no damage): the stagger and, as measured, the alert. Never START_RAGDOLL
    // and never a velocity: his bodies are keyframed. r.target is the held NPC, r.vel the direction he is shoved from (the hand's side).
    template<class Read> static bool living_start(std::uintptr_t base,Read& read,const Request& r) {
        if(!r.target || !rig::pointer(r.target) || !rig::typed(read,base,r.target,NativeMelee::npc_vtable_rva) || !living(read,r.target)) return false;
        Request s=r;s.op=Op::shove;s.decl="components/base/damage/shove_damage.damage";s.amount=0;
        const bool ok=shove(base,read,s);if(ok) ++living_shoves;return ok;
    }
    // A pull step (r.pos: his new feet position, at most 5 cm per 100 ms and horizontal, LivingHold) through the existing teleport; only with
    // living_pull_enabled (the Task 13 verdict), never on a dead or recycled NPC. The orientation is kept as read (the probe's yaw-pop check).
    template<class Read> static bool living_pull(std::uintptr_t base,Read& read,const Request& r) {
        if constexpr(!living_pull_enabled) return false; // the Task 13 verdict: the branch is compiled out (C4127 under /WX otherwise)
        if(!r.target || !rig::pointer(r.target) || !rig::typed(read,base,r.target,NativeMelee::npc_vtable_rva) || !living(read,r.target)) return false;
        const auto axis=rig::value<std::array<float,9>>(read,r.target+0x30c);
        if(!axis) return false;
        teleport<Read>(base,r.target,r.pos,*axis);return true;
    }
public:
    inline static std::atomic<unsigned> living_shoves{};
private:
    // Task 14 dev probe (build/guard-probe, off by default, read once at start by arm_guard_probe): the next full-fist squeeze at a living guard runs ONE
    // lever experiment instead of the pull (shove alone, a named damage decl, or horizontal teleport steps toward the hand; never START_RAGDOLL or a
    // velocity), samples him every 50 ms for 6 s into artifacts/guard-probe.csv, and deletes the knob file. The engine calls are the existing ones.
    // The teleport keeps no orientation (identity axis): the experiment measures displacement and what the AI does, not facing.
    inline static GuardProbePlan guard_probe_plan_{};inline static std::filesystem::path guard_probe_knob_,guard_probe_csv_;inline static GuardProbeRun guard_probe_run_{};
public:
    inline static std::atomic<unsigned> guard_probes{};
    static void arm_guard_probe(const std::filesystem::path& knob,const std::filesystem::path& csv) {
        guard_probe_plan_={};guard_probe_run_=GuardProbeRun{};guard_probe_knob_=knob;guard_probe_csv_=csv;
        std::error_code ec;if(csv.empty() || !std::filesystem::exists(knob,ec)) return; // v1046: diagnostics off = empty csv: never armed (it would skip the pull and eat its knob)
        std::ifstream in(knob);std::string text((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());
        if(const auto plan=parse_guard_probe(text)) guard_probe_plan_=*plan;
    }
    static bool guard_probe_armed() {return guard_probe_plan_.experiment!=GuardExperiment::none;}
private:
    static bool guard_probe_ask_unaware(std::uintptr_t base,std::uintptr_t npc,bool& answer) {
        __try {answer=reinterpret_cast<bool (__fastcall*)(std::uintptr_t)>(base+NativeMelee::unaware_rva)(npc);return true;} __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    template<class Read> static GuardSample guard_probe_sample(std::uintptr_t base,Read& read,std::uintptr_t npc) {
        return sample_guard(read,base,npc,clock_ms(),[&](std::uintptr_t n)->std::optional<bool> {
            std::array<unsigned char,32> got{};
            if(!read(base+NativeMelee::unaware_rva,got.data(),got.size()) || got!=NativeMelee::unaware_code) return std::nullopt; // the engine function is not the pinned one
            bool answer{};if(!guard_probe_ask_unaware(base,n,answer)) return std::nullopt; // a fault inside the engine test is "did not run", never "alerted"
            return answer;});
    }
    template<class Read> static bool guard_probe_fire(std::uintptr_t base,Read& read,std::uintptr_t npc,Vec3 hand,const Request& r) {
        if(!guard_probe_armed() || guard_probe_run_.active) return false;
        const auto plan=guard_probe_plan_;
        auto& run=guard_probe_run_;GuardProbeRun next;
        next.out.open(guard_probe_csv_,std::ios::trunc);if(!next.out) return true; // the plan and the knob stay (nothing was consumed), and the pull is skipped rather than falling back to the ragdoll
        guard_probe_plan_={};++guard_probes;
        std::error_code ec;std::filesystem::remove(guard_probe_knob_,ec); // one experiment per run
        run=std::move(next);
        run.id=rig::value<std::uint32_t>(read,npc+0xa8).value_or(0);
        run.out<<guard_sample_csv_header()<<'\n'<<guard_sample_csv_row(guard_probe_sample(base,read,npc))<<'\n'; // the state before anything happens
        const auto now=clock_ms();run.active=true;run.npc=npc;run.hand=hand;run.until=now+6000;run.next_sample=now+50;
        if(plan.experiment==GuardExperiment::teleport_step) {run.steps_left=plan.steps;run.step_m=plan.step_m;run.next_step=now+100;return true;}
        Vec3 pull{r.vel[0],r.vel[1],r.vel[2]};if(length(pull)>6) pull=pull*(6/length(pull));
        const double l=std::max(length(pull),1e-6);
        Request s=r;s.op=Op::shove;s.target=npc;s.amount=0;
        const std::string decl=plan.experiment==GuardExperiment::shove_decl?"components/base/damage/"+plan.decl+".damage":"components/base/damage/shove_damage.damage";
        s.decl=decl.c_str();s.vel={float(-pull.x/l),float(-pull.y/l),0}; // as the pull does: hit from the side he is pulled to
        const bool applied=shove(base,read,s);                              // the probe posts no START_RAGDOLL and no velocity itself; the shove's own reaction is what is measured
        std::ofstream note(guard_probe_csv_.parent_path()/"guard-probe-result.txt",std::ios::trunc);
        note<<"experiment="<<(plan.experiment==GuardExperiment::shove_decl?"shove_decl":"shove")<<" decl="<<decl<<" applied="<<(applied?1:0)<<" last_shove="<<last_shove.load()<<'\n';
        return true;
    }
    template<class Read> static void service_guard_probe(std::uintptr_t base,Read& read) {
        auto& run=guard_probe_run_;if(!run.active) return;
        const auto now=clock_ms();
        if(run.steps_left && now>=run.next_step) {
            const auto at=rig::value<std::array<float,3>>(read,run.npc+0x300);
            const auto id_now=rig::value<std::uint32_t>(read,run.npc+0xa8);
            if(!id_now || *id_now!=run.id || !rig::typed(read,base,run.npc,NativeMelee::npc_vtable_rva)) run.steps_left=1; // the guard is gone or the slot was reused: stop stepping
            else if(at && living(read,run.npc)) {
                const double dx=run.hand.x-(*at)[0],dy=run.hand.y-(*at)[1],d=std::hypot(dx,dy);
                if(d>1e-3 && std::isfinite(d)) {
                    const double m=std::min(run.step_m,d);
                    teleport<Read>(base,run.npc,{static_cast<float>((*at)[0]+dx/d*m),static_cast<float>((*at)[1]+dy/d*m),(*at)[2]},{1,0,0,0,1,0,0,0,1});
                }
            }
            --run.steps_left;run.next_step=now+100;
        }
        if(now>=run.next_sample) {run.out<<guard_sample_csv_row(guard_probe_sample(base,read,run.npc))<<'\n';run.next_sample+=50;}
        if(now>=run.until) {run.out.flush();run.out.close();run.active=false;}
    }
    // A free hand squeezing at a body (dead or unconscious) whose ragdoll is not live: corpses settle into a static or
    // un-instanced ragdoll some minutes after a load (sim v419), so a limb could not be dragged. The game's DropCorpse wakes a
    // body with START_RAGDOLL (832980): the same here, then the squeezing hand takes the nearest limb (NativeGrab::yanked).
    // A full-fist squeeze (grip + trigger) of a free hand (input thread) at a body drags it by its nearest limb, woken if its
    // ragdoll settled; the grip alone is the game's Use (carry, talk, doors) in dxgi_probe (user, headset 2026-10-14: "full
    // hand = grab, grip = what the game proposes").
public:
    inline static std::atomic<std::uint64_t> carries{};
private:
    template<class Read> static bool wake(std::uintptr_t base,Read& read,const Request& r) {
        const Vec3 hand{r.pos[0],r.pos[1],r.pos[2]};
        grabbed_[r.side&1u]=guard_at(base,read,hand,.5);grabbed_at_[r.side&1u]=GetTickCount64(); // a living guard taken by this squeeze
        ++squeezes;if(grabbed_[r.side&1u]) ++guard_grabs;
        const auto game=rig::value<std::uintptr_t>(read,base+0x39a5290);if(!game) return false;
        std::array<std::uintptr_t,0x3fff> registry{};if(!read(*game+0x3130b0,registry.data(),sizeof(registry))) return false;
        std::uintptr_t best{};double best_d=1.3;
        for(unsigned i=1;i<registry.size();++i) {
            const auto npc=registry[i];
            if(!rig::pointer(npc) || !rig::typed(read,base,npc,NativeMelee::npc_vtable_rva)) continue;
            const auto state=rig::value<unsigned char>(read,npc+0xd1);if(!state || !(*state&0x28)) continue; // dead or unconscious
            const auto model=rig::value<std::uintptr_t>(read,npc+0xf0);
            const auto at=model && rig::pointer(*model)?rig::value<std::array<float,3>>(read,*model+0x1860):std::nullopt;
            if(!at) continue;
            const double up=hand.z-(*at)[2],d=std::hypot(hand.x-(*at)[0],hand.y-(*at)[1]);
            if(up<-.4 || up>1.2 || d>=best_d) continue;
            best=npc;best_d=d;
        }
        // Chest or limb: the nearest live body part, else the distance to the body's origin (it follows the pelvis). The hand
        // must be AT the body (0.45 m of a part, 1 m of the pelvis): a squeeze at a door beside a corpse is not a grab.
        double limb=1e9,torso=1e9;bool live=false,at_body=false;
        if(best) {
            for(int bone=0;bone<17;++bone) if(const auto b=native_ragdoll_body(read,base,best,bone)) {
                live=true;const double d=length(b->pose.position-hand);auto& slot=ragdoll_grab_bone(bone)?limb:torso;slot=std::min(slot,d);}
            if(live) at_body=std::min(limb,torso)<=.45;
            else {const auto model=rig::value<std::uintptr_t>(read,best+0xf0);
                const auto at=model && rig::pointer(*model)?rig::value<std::array<float,3>>(read,*model+0x1860):std::nullopt;
                torso=at?length(Vec3{(*at)[0],(*at)[1],(*at)[2]}-hand):1e9;at_body=torso<=1.;}
        }
        if(!at_body) return true;
        if(live) return true; // a live limb: the hand's own grab takes it
        const auto id=rig::value<std::uint32_t>(read,best+0xa8);if(!id) return false;
        const auto now=clock_ms();
        if(wake_diag) wake_diag->note_wake_attempt();
        static const EntityLockout none;
        switch(post_budget.request(*id,false,seconds_from_ms(now),wake_lockout?*wake_lockout:none)) {
        case PostDecision::allow: {
            auto* p=post(base,read,0xb9,0x10,*id);if(!p) return false;
            std::memset(p,0,0x10);++wakes;if(wake_diag) wake_diag->note_post(true);
            remember_wake(best,*id,r.side,now);NativeGrab::yanked(r.side,best,now+1500);
            return true;
        }
        case PostDecision::refused: refuse_wake(r.side);return true;
        default: if(wake_diag) wake_diag->note_post(false);return true;   // waiting for the retry, ceiling, locked, disabled: no post
        }
    }
    // The settled corpse a wake posted for, until its primary body reads dynamic (or it is refused).
    inline static std::array<WakeEntry,4> wake_pending_{};
    inline static std::array<std::atomic<bool>,2> wake_refused_{};
    static void refuse_wake(unsigned side) {if(wake_diag) wake_diag->note_wake_refused();wake_refused_[side&1u]=true;}
    static void remember_wake(std::uintptr_t entity,std::uint32_t id,unsigned side,std::uint64_t now) {
        WakeEntry* slot=nullptr;
        for(auto& e:wake_pending_) if(e.used && e.id==id) slot=&e;
        for(auto& e:wake_pending_) if(!slot && !e.used) slot=&e;
        if(!slot) {slot=&wake_pending_[0];for(auto& e:wake_pending_) if(e.at_ms<slot->at_ms) slot=&e;}
        *slot=WakeEntry{true,false,entity,id,side&1u,now};
    }
public:
    // Game thread, beside hold_down. A remembered corpse whose primary body still does not read dynamic one second after its post gets the one retry;
    // one more second without it and the budget refuses: the side is flagged for the refusal haptic and the entry is dropped. A corpse that wakes,
    // changes state or leaves the registry is forgotten (and the budget's entry with it).
    template<class Read> static void service_wake_retries(std::uintptr_t base,Read& read,std::uint64_t now_ms) {
        static const EntityLockout none;
        for(auto& e:wake_pending_) {
            if(!e.used) continue;
            const auto id=rig::value<std::uint32_t>(read,e.entity+0xa8);const auto state=rig::value<unsigned char>(read,e.entity+0xd1);
            // The entity must still be the one the first post targeted: same registry slot and id (alive_entity), still dead/unconscious, and the primary body
            // still not dynamic. Anything else (level load, recycled slot or id) drops the entry without a post.
            if(!alive_entity(base,read,e.entity,e.id) || !id || *id!=e.id || !state || !(*state&0x28) || native_ragdoll_body(read,base,e.entity,0)) {post_budget.forget(e.id);e.used=false;continue;}
            if(now_ms<e.at_ms || now_ms-e.at_ms<1000) continue;
            switch(post_budget.request(e.id,false,seconds_from_ms(now_ms),wake_lockout?*wake_lockout:none)) {
            case PostDecision::allow: { // the one retry
                auto* p=post(base,read,0xb9,0x10,e.id);if(!p) {e.used=false;break;}
                std::memset(p,0,0x10);++wakes;if(wake_diag) wake_diag->note_post(true);
                e.retried=true;e.at_ms=now_ms;break;
            }
            case PostDecision::refused: refuse_wake(e.side);e.used=false;break;
            default: if(wake_diag) wake_diag->note_post(false);break;   // ceiling or lockout: try again next service
            }
        }
    }
private:
public:
    static bool block_keeps_gun() noexcept {return NativeBlockEligibility::process_owner().enabled();}
private:
    // A living guard held by a limb stays in his ragdoll while held, up to 3 s (then he breaks free and gets up): his
    // conscious ragdoll recovers after 0.5-2 s by itself (sim v432), faster than a hand can drag him.
    inline static std::array<std::uintptr_t,2> held_guard_{};inline static std::array<std::uint64_t,2> held_since_{},held_post_{};
    template<class Read> static void hold_down(std::uintptr_t base,Read& read) {
        if(post_budget.disabled()) return; // build/no-ragdoll-posts
        auto* grab=NativeGrab::instance();if(!grab) return;
        const auto now=GetTickCount64();
        for(unsigned side=0;side<2;++side) {
            const auto e=grab->held_ragdoll(side);
            if(e!=held_guard_[side]) {held_guard_[side]=e;held_since_[side]=now;held_post_[side]=0;}
            if(!e || now-held_since_[side]>3000 || now-held_post_[side]<400) continue;
            const auto state=rig::value<unsigned char>(read,e+0xd1);if(!state || (*state&0x28)) continue; // corpses stay down anyway
            const auto id=rig::value<std::uint32_t>(read,e+0xa8);if(!id) continue;
            if(auto* p=post(base,read,0xb9,0x10,*id)) {std::memset(p,0,0x10);held_post_[side]=now;++holds_down;}
        }
    }
    template<class Read> static std::uintptr_t damage_decl(std::uintptr_t base,Read& read,const char* name) {
        if(!name || rig::value<std::uintptr_t>(read,base+0x3990b60)!=base+0x1c6f1e8) return 0;
        return reinterpret_cast<std::uintptr_t>(reinterpret_cast<void* (__fastcall*)(std::uintptr_t,const char*,bool,bool)>(base+0x1e030)(base+0x3990b60,name,false,false));
    }
    template<class Read> static bool shove(std::uintptr_t base,Read& read,const Request& r) {
        static const bool melee_ok=NativeMelee::verify(base,read);
        const auto decl=damage_decl(base,read,r.decl);const auto game=rig::value<std::uintptr_t>(read,base+0x39a5290);
        const auto player=game?rig::value<std::uintptr_t>(read,*game+0x3130b0):std::nullopt;
        const auto pid=player?rig::value<std::uint32_t>(read,*player+0xa8):std::nullopt;
        if(!melee_ok || !decl || !pid) {last_shove=-1;return false;}
        const NativeMelee::Sword s{*player,*player,0,decl,*pid,*pid,r.amount};
        const auto& d=r.vel;
        const MeleeHitRequest hit{r.target,r.pos,{-d[0],-d[1],-d[2]},d,GetTickCount64()};
        last_shove=NativeMelee::apply(base,read,s,hit);return last_shove==0;
    }
    template<class Read> static std::uintptr_t phys_damage(std::uintptr_t base,Read& read) {
        static const std::uintptr_t decl=[&]()->std::uintptr_t { // FindDecl on the "damage" decl type (arkDeclDamage, registered at 138c810)
            if(rig::value<std::uintptr_t>(read,base+0x3990b60)!=base+0x1c6f1e8) return 0;
            return reinterpret_cast<std::uintptr_t>(reinterpret_cast<void* (__fastcall*)(std::uintptr_t,const char*,bool,bool)>(base+0x1e030)(
                base+0x3990b60,"components/base/damage/phys_damage.damage",false,false));
        }();
        return decl;
    }
    template<class Read> static std::uintptr_t player(std::uintptr_t base,Read& read) {
        const auto game=rig::value<std::uintptr_t>(read,base+0x39a5290);if(!game || !rig::typed(read,base,*game,0x1c940a8)) return 0;
        const auto p=rig::value<std::uintptr_t>(read,*game+0x3130b0);return p && rig::typed(read,base,*p,0x1c5e428)?*p:0;
    }
    template<class Read> static std::uintptr_t player_fsm(std::uintptr_t base,Read& read) {
        const auto p=player(base,read);const auto entries=p?rig::list(read,p+0x268,4096):std::nullopt;if(!entries) return 0;
        for(unsigned i=0;i<entries->packed_count;++i) {
            const auto c=rig::value<std::uintptr_t>(read,entries->data+i*8);
            if(c && rig::typed(read,base,*c,0x1c73d58) && rig::value<std::uintptr_t>(read,*c+0x10)==p && rig::started(read,*c)) return *c;
        }
        return 0;
    }
    template<class Read> static bool equip(std::uintptr_t base,Read& read,int code,int state=53) {
        const auto fsm=player_fsm(base,read);if(!fsm) return false;
        alignas(16) std::array<unsigned char,0x30> params{};
        reinterpret_cast<void (__fastcall*)(void*,void*,int,int)>(base+0x835590)(params.data(),nullptr,code,0x5b);
        if(state==53 && (code&0x0c)) {std::uint32_t id{};std::memcpy(&id,params.data(),4);NativeManualEquipRuntime::authorize_right_draw(id);} // v692: only these right draws pass
        reinterpret_cast<void (__fastcall*)(std::uintptr_t,int,void*)>(base+0x8024d0)(fsm,state,params.data()); // its al is no success flag (sim v406)
        return true;
    }
    static void submit_holster(void* context,std::uintptr_t machine,const NativeManualEquip::Params* params) {
        const auto base=reinterpret_cast<std::uintptr_t>(context);
        reinterpret_cast<void (__fastcall*)(std::uintptr_t,int,const void*)>(base+0x8024d0)(machine,64,params);
    }
    template<class Read> static bool equip_holster(std::uintptr_t base,Read& read,const Request& request) {
        const auto machine=player_fsm(base,read);if(!machine)return false;
        NativeManualEquip::Params params{};
        reinterpret_cast<void (__fastcall*)(void*,void*,int,int)>(base+0x835590)(params.bytes.data(),nullptr,0,0x5b);
        return NativeManualEquipRuntime::process_owner().execute_holster(request.manual_generation,request.manual_owner,
            request.manual_xr_epoch,machine,params,&submit_holster,reinterpret_cast<void*>(base));
    }
    template<class Read> static bool spawn(std::uintptr_t base,Read& read,const Request& r) {
        const auto game=rig::value<std::uintptr_t>(read,base+0x39a5290);const auto owner=player(base,read);if(!game || !owner) return false;
        void* decl=reinterpret_cast<void* (__fastcall*)(std::uintptr_t,const char*,bool,bool)>(base+0x1e030)(base+0x2434170,sword_copy_decl,false,false);
        if(!decl) return false;
        const auto entity=reinterpret_cast<std::uintptr_t (__fastcall*)(std::uintptr_t,void*,const float*,const float*,std::uintptr_t,bool,const float*,const float*,const float*)>(
            base+0x8ae9b0)(*game,decl,r.pos.data(),r.axis.data(),owner,false,nullptr,r.vel.data(),r.spin.data());
        if(!entity) return false;
        copy_=entity;copy_id_=rig::value<std::uint32_t>(read,entity+0xa8).value_or(0);copy_since_=GetTickCount64();++spawned;shown_=true;
        return true;
    }
    template<class Read> static bool alive(std::uintptr_t base,Read& read) {
        if(!copy_ || !copy_id_ || (copy_id_>>16)>=0x3fff) return false;
        const auto game=rig::value<std::uintptr_t>(read,base+0x39a5290);
        return game && rig::value<std::uintptr_t>(read,*game+0x3130b0+(copy_id_>>16)*8)==copy_ && rig::value<std::uint32_t>(read,copy_+0xa8)==copy_id_;
    }
    // The copy goes out of sight (picked up, called back): hidden, kept for the next drop.
    template<class Read> static void stash(std::uintptr_t base,Read& read) {
        if(alive(base,read) && shown_) {visible(base,read,copy_,false);++removed;}
        shown_=false;flight_.flying=false;
    }
    // idEntity Hide (vtable +0x80, 8784b0) / Show (+0x88, 87c590), only when the entity uses the base functions directly or
    // through a one-jump thunk (idDynamicEntity: 882060 jmp 8784b0, 8820e0 jmp 87c590; the exact-address test skipped them,
    // so the picked-up sword stayed in the world, headset v436).
    template<class Read> static bool visible(std::uintptr_t base,Read& read,std::uintptr_t e,bool on) {
        const std::uintptr_t want=base+(on?0x87c590:0x8784b0);
        const auto vt=rig::value<std::uintptr_t>(read,e);const auto fn=vt?rig::value<std::uintptr_t>(read,*vt+(on?0x88:0x80)):std::nullopt;
        if(!fn) return false;
        bool ok=*fn==want;
        if(!ok) {std::array<unsigned char,5> jmp{};
            if(read(*fn,jmp.data(),jmp.size()) && jmp[0]==0xe9) {std::int32_t rel{};std::memcpy(&rel,jmp.data()+1,4);ok=*fn+5+rel==want;}}
        if(ok) reinterpret_cast<void (__fastcall*)(std::uintptr_t)>(want)(e);
        else ++visibility_skips;
        return ok;
    }
    template<class Read> static bool alive_entity(std::uintptr_t base,Read& read,std::uintptr_t e,std::uint32_t id) {
        if(!e || !id || (id>>16)>=0x3fff) return false;
        const auto game=rig::value<std::uintptr_t>(read,base+0x39a5290);
        return game && rig::value<std::uintptr_t>(read,*game+0x3130b0+(id>>16)*8)==e && rig::value<std::uint32_t>(read,e+0xa8)==id;
    }
    // v1096 (user, headset v1094: "copied weapons should carry all original weapon features - upgrades, animations ... a correct display of
    // the ammo type"; live sim v1095 realcopy): a copy of the player's pistol or crossbow is a second instance of the player's own weapon
    // entity (its decl at weapon+0x2e8) with the player as owner. The game starts its components by itself within ~2 s: its weapon component
    // points at the player's attributes, bag, camera, inventory (+0x30..+0x70) as the native one does, it selects ammo (+0x98), keeps its own
    // magazine (+0xa0/+0xa4) and spawns its own loaded bolt (crossbow +0x160). It only lacks the upgrades (set below through vslot 0xd8).
    // Every other def (the sword copy, guard weapons, a kind the player has never equipped) is spawned by name as before.
    template<class Read> static std::uintptr_t real_weapon_decl(std::uintptr_t base,Read& read,const char* name) {
        if(!name || !real_copies_.load(std::memory_order_relaxed)) return 0;
        bool player_gun=false;for(unsigned d=1;d<mirror_def_names.size();++d) player_gun=player_gun || !std::strcmp(name,mirror_def_names[d]);
        if(!player_gun) return 0;
        const auto comp=native_equipment(base,read,held_copy::kind_of(name));
        const auto weapon_ent=comp?rig::value<std::uintptr_t>(read,comp+0x10):std::nullopt; // (named apart from the checks' globals: /WX C4459)
        const auto decl=weapon_ent && *weapon_ent?rig::value<std::uintptr_t>(read,*weapon_ent+0x2e8):std::nullopt;
        return decl && rig::pointer(*decl)?*decl:0;
    }
    template<class Read> static std::uintptr_t spawn_def(std::uintptr_t base,Read& read,const char* name,const float* pos,const float* axis,bool allow_real=true) {
        for(unsigned i=0;i<12;++i) if(!std::isfinite(i<3?pos[i]:axis[i-3])) {++failures;return 0;} // REA Q4: the engine does not check; pos[3], axis[9]
        const auto game=rig::value<std::uintptr_t>(read,base+0x39a5290);if(!game) return 0;
        if(const auto real=allow_real?real_weapon_decl(base,read,name):0) {
            const auto owner=player(base,read);
            if(owner) {
                static const std::array<float,3> zero{};++real_spawned;
                return reinterpret_cast<std::uintptr_t (__fastcall*)(std::uintptr_t,std::uintptr_t,const float*,const float*,std::uintptr_t,bool,const float*,const float*,const float*)>(
                    base+0x8ae9b0)(*game,real,pos,axis,owner,false,nullptr,zero.data(),zero.data());
            }
        }
        void* decl=reinterpret_cast<void* (__fastcall*)(std::uintptr_t,const char*,bool,bool)>(base+0x1e030)(base+0x2434170,name,false,false);
        if(!decl) return 0;
        static const std::array<float,3> zero{};
        return reinterpret_cast<std::uintptr_t (__fastcall*)(std::uintptr_t,void*,const float*,const float*,std::uintptr_t,bool,const float*,const float*,const float*)>(
            base+0x8ae9b0)(*game,decl,pos,axis,0,false,nullptr,zero.data(),zero.data());
    }
    template<class Read> static void teleport(std::uintptr_t base,std::uintptr_t e,const std::array<float,3>& pos,const std::array<float,9>& axis) {
        const auto ok=[](const auto& v){return std::all_of(v.begin(),v.end(),[](float x){return std::isfinite(x);});};
        if(!ok(pos) || !ok(axis)) return; // REA Q4: Teleport 87d1e0 has no finite check
        alignas(16) std::array<float,3> o=pos;alignas(16) std::array<float,9> a=axis;
        using Teleport=void (__fastcall*)(std::uintptr_t,const float*,const float*,int);
        for(const auto& an:anchors_) if(an.copy==e && an.anchor) { // v1099: a real weapon copy rides its anchor (it is bound to it)
            reinterpret_cast<Teleport>(base+0x87d1e0)(an.anchor,o.data(),a.data(),0);
            // v1100 (sim v1099: the copy's entity followed its anchor, its model stayed at the player): the copy at the same pose (its
            // offset to the anchor stays identity), then its model from it: 0x87dd30 (entity +0xa0 / +0xb8 -> model +0x1860), which presents
            // the model only when +0xd0 bits 0 and 1 are clear; a weapon that is not equipped keeps bit 1 (live 0x8a), so the model's own
            // present (vtable +0x28, 1) is called here unless the copy is hidden (bit 0).
            reinterpret_cast<Teleport>(base+0x87d1e0)(e,o.data(),a.data(),0);
            present_model(base,e);
            return;
        }
        reinterpret_cast<Teleport>(base+0x87d1e0)(e,o.data(),a.data(),0);
    }
    static void present_model(std::uintptr_t base,std::uintptr_t e) {
        __try {
            present_one(base,e);
            // v1111 (user, headset v1100/v1110: the second-hand copy and the dropped weapon showed the base model; sim v1111: the copy's upgrade
            // parts and loaded bolt existed, were bound to the copy's joints, visible-flagged and 11 cm from it, yet never drawn): the parts are
            // bound children of the copy and carry the weapon's own 'not presented' bit (+0xd0 bit1, live 0x8a) as the copy does, so the game
            // never submits their models either; only the copy was presented here. Its bound children (+0xb0 list, count +0xb8) are presented with it.
            const auto data=*reinterpret_cast<const std::uintptr_t*>(e+0xb0);const auto n=*reinterpret_cast<const std::uint32_t*>(e+0xb8)&0xffffffu;
            if(data && n<64) for(unsigned i=0;i<n;++i) {const auto c=*reinterpret_cast<const std::uintptr_t*>(data+i*8ull);if(c) {present_one(base,c);++children_presented;}}
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }
    static void present_one(std::uintptr_t base,std::uintptr_t e) {
        reinterpret_cast<void (__fastcall*)(std::uintptr_t)>(base+0x87dd30)(e);
        const auto flags=*reinterpret_cast<const volatile std::uint8_t*>(e+0xd0);const auto model=*reinterpret_cast<const std::uintptr_t*>(e+0xf0);
        if(!model || (flags&1)) return;
        const auto vt=*reinterpret_cast<const std::uintptr_t*>(model);
        if(flags&2) reinterpret_cast<void (__fastcall*)(std::uintptr_t,int)>(*reinterpret_cast<const std::uintptr_t*>(vt+0x28))(model,1);
        // v1111 (sim: the crossbow copy was never drawn although presented and placed, the pistol copy was): an animated-class model (vtable
        // 0x1b0d628) publishes its render-world bounds sphere and cull byte only from model vslot 0x78 (0x25f3c0), which the game runs from the
        // animation component's pose upload (0x25efe0 <- 0x541270). A weapon copy without a running animation component (the crossbow) keeps the
        // sphere it last published, or none at all (cull byte 9 = never drawn). Republished here after every move; with no pose it takes the
        // rest-bounds path from the current origin and axis.
        if(vt==base+0x1b0d628) {reinterpret_cast<void (__fastcall*)(std::uintptr_t)>(*reinterpret_cast<const std::uintptr_t*>(vt+0x78))(model);++bounds_published;}
    }
    inline static std::atomic<std::uint64_t> bounds_published{};
    inline static std::atomic<std::uint64_t> children_presented{},tree_presents{},tree_present_blocked{};
    // +0xd0 bit1 = 'not presented' (set by entity vslot 0x90 = 0x8784d0 from the Hide recursion). Cleared raw on the hidden anchor only: its own
    // model stays out of the render world (bit0), while what is bound to it may be presented.
    static bool clear_not_presented(std::uintptr_t e) {
        __try {auto& f=*reinterpret_cast<volatile std::uint8_t*>(e+0xd0);if(f&2) f=static_cast<std::uint8_t>(f&~2u);return true;} __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
public:
    // The holstered gun at the left hip (published by the render thread every frame): the player's own pistol or crossbow
    // model (Corvo's or Emily's cinematic copy), shown and moved with the body while the hip holds it.
    struct Hip {bool show{};unsigned kind{};int owner{-1};std::array<float,3> pos{};std::array<float,9> axis{};std::uint64_t tick{};
        std::optional<NativeManualEquipRuntime::HipContext> context;}; // owner: 1 Emily, 0 Corvo
    static void hip(const Hip& value,NativeManualEquipRuntime& runtime=NativeManualEquipRuntime::process_owner()) {
        auto request=value;request.context=runtime.hip_context();
        std::lock_guard<std::mutex> lock(m_);hip_req_=request;
    }
    inline static std::atomic<std::uint64_t> hip_frames{},visibility_skips{};
private:
    static Hip hip_req_; // defined after the class: clang-cl needs Hip's member initializers complete
    inline static std::array<std::uintptr_t,2> hip_ent_{};inline static std::array<std::uint32_t,2> hip_id_{};
    inline static std::array<bool,2> hip_shown_{};inline static int emily_=-1;inline static std::array<int,2> hip_owner_{-1,-1};
    inline static std::array<std::optional<NativePlayerStateLease>,2> hip_owner_lease_{};
    struct HipExchange {bool blocked{};NativeManualEquipRuntime::HipContext draw;std::uint64_t publication_fence{};};
    static HipExchange hip_exchange_; // defined after the class: clang-cl needs HipExchange's member initializers complete
    template<class Read> static std::optional<bool> hip_entity_current(std::uintptr_t base,Read& read,unsigned kind) {
        const auto entity=hip_ent_[kind];const auto spawn=hip_id_[kind];
        if(!entity)return false;
        if(!spawn || (spawn>>16)>=0x3fff || !hip_owner_lease_[kind] || !hip_owner_lease_[kind]->game)return {};
        const auto game=rig::value<std::uintptr_t>(read,base+0x39a5290);
        if(!game || !rig::typed(read,base,*game,0x1c940a8))return {};
        if(*game!=hip_owner_lease_[kind]->game)return false;
        const auto registered=game?rig::value<std::uintptr_t>(read,*game+0x3130b0+(spawn>>16)*8ull):std::nullopt;
        const auto current_spawn=rig::value<std::uint32_t>(read,entity+0xa8);
        if(!game || !registered || !current_spawn)return {};
        return *registered==entity && *current_spawn==spawn;
    }
    template<class Read> static bool service_gun_draw(std::uintptr_t base,Read& read,const Request& request,NativeManualEquipRuntime& runtime) {
        const auto submit=[&] {
        if(!request.manual_generation || !runtime.grant_hip(request.manual_generation,player_fsm(base,read)))return false;
        const auto context=runtime.hip_context();
        if(!context || context->generation!=request.manual_generation)return false;
        hip_exchange_.blocked=true;hip_exchange_.draw=*context;
        {std::lock_guard<std::mutex> lock(m_);hip_req_.show=false;hip_req_.tick=0;}
        for(unsigned kind=0;kind<hip_ent_.size();++kind)if(hip_ent_[kind] && hip_shown_[kind]) {
            const auto current=hip_entity_current(base,read,kind);
            if(!current)return false;
            if(!*current){hip_ent_[kind]=0;hip_id_[kind]=0;hip_shown_[kind]=false;hip_owner_lease_[kind].reset();continue;}
            if(!visible(base,read,hip_ent_[kind],false))return false;
            hip_shown_[kind]=false;
        }
        return runtime.hip_draw_current(request.manual_generation) && equip(base,read,0x04,64);
        };
        const bool accepted=submit();if(!accepted)runtime.cancel(request.manual_generation);return accepted;
    }
    template<class Read> static void service_hip(std::uintptr_t base,Read& read,NativeManualEquipRuntime& runtime=NativeManualEquipRuntime::process_owner()) {
        Hip h{};{std::lock_guard<std::mutex> l(m_);h=hip_req_;}
        if(hip_exchange_.blocked && h.context) {
            const bool same_lifetime=h.context->owner==hip_exchange_.draw.owner && h.context->xr_epoch==hip_exchange_.draw.xr_epoch;
            if(runtime.hip_empty_current(*h.context,same_lifetime?hip_exchange_.draw.generation:0)) {
                hip_exchange_.blocked=false;hip_exchange_.publication_fence=GetTickCount64();
                std::lock_guard<std::mutex> lock(m_);hip_req_.show=false;hip_req_.tick=0;h.show=false;
            }
        }
        const auto current_show=[&] {
            if(hip_exchange_.blocked || gun_native_lying_ || !h.show || !h.context || !runtime.hip_empty_current(*h.context))return false; // gun_native_lying_: the gun lies in the world (dual wield), the hip holds nothing
            const auto now=GetTickCount64();
            return h.tick>hip_exchange_.publication_fence && now>=h.tick && now-h.tick<=250;
        };
        const bool show=current_show();const unsigned kind=h.kind&1u;
        if(h.owner>=0) emily_=h.owner; // the drawn gun's own decl (both avatars); the player's decl is only the first guess
        if(emily_<0) { // which character: the player entity's decl name
            const auto game=rig::value<std::uintptr_t>(read,base+0x39a5290);const auto p=game?rig::value<std::uintptr_t>(read,*game+0x3130b0):std::nullopt;
            const auto decl=p?rig::value<std::uintptr_t>(read,*p+0x2e8):std::nullopt;const auto name=decl?rig::value<std::uintptr_t>(read,*decl+0x48):std::nullopt;
            std::array<char,96> text{};
            if(name && read(*name,text.data(),text.size()-1)) {std::string n(text.data());emily_=n.find("emily")!=std::string::npos?1:0;}
        }
        bool unwanted_hidden=true;
        for(unsigned clone_kind=0;clone_kind<hip_ent_.size();++clone_kind)if(hip_ent_[clone_kind]) {
            const auto current=hip_entity_current(base,read,clone_kind);
            if(!current){unwanted_hidden=false;continue;}
            if(!*current){hip_ent_[clone_kind]=0;hip_id_[clone_kind]=0;hip_shown_[clone_kind]=false;hip_owner_lease_[clone_kind].reset();continue;}
            const bool wanted=show && clone_kind==kind && hip_owner_[clone_kind]==(emily_==1?1:0) &&
                hip_owner_lease_[clone_kind] && h.context && *hip_owner_lease_[clone_kind]==h.context->owner;
            if(!wanted && hip_shown_[clone_kind]) {
                if(visible(base,read,hip_ent_[clone_kind],false))hip_shown_[clone_kind]=false;
                else unwanted_hidden=false;
            }
        }
        if(!unwanted_hidden)return;
        if(show && !current_show())return;
        for(unsigned k=0;k<2;++k) {
            const bool mine=show && k==kind;
            if(hip_ent_[k]) {
                const auto current=hip_entity_current(base,read,k);if(!current)continue;
                if(!*current){hip_ent_[k]=0;hip_id_[k]=0;hip_shown_[k]=false;hip_owner_lease_[k].reset();}
            }
            if(hip_ent_[k] && (hip_owner_[k]!=(emily_==1?1:0) ||
               (h.context && hip_owner_lease_[k] && !(*hip_owner_lease_[k]==h.context->owner)))) { // the other character's gun: put it away, spawn the right one
                if(hip_shown_[k] && !visible(base,read,hip_ent_[k],false))continue;
                hip_ent_[k]=0;hip_id_[k]=0;hip_shown_[k]=false;hip_owner_lease_[k].reset();}
            if(mine && !hip_ent_[k]) {
                static const char* names[2][2]={{"models/campaign/dunwall/midnightassets/gun_corvo_midnight.def","models/campaign/dunwall/midnightassets/crossbow_corvo_midnight.def"},
                                                {"models/campaign/dunwall/midnightassets/gun_emily_midnight.def","models/campaign/dunwall/midnightassets/crossbow_emily_midnight.def"}};
                hip_ent_[k]=spawn_def(base,read,names[emily_==1?1:0][k],h.pos.data(),h.axis.data());
                hip_id_[k]=hip_ent_[k]?rig::value<std::uint32_t>(read,hip_ent_[k]+0xa8).value_or(0):0;hip_shown_[k]=hip_ent_[k]!=0;hip_owner_[k]=emily_==1?1:0;
                hip_owner_lease_[k]=h.context?std::optional<NativePlayerStateLease>{h.context->owner}:std::nullopt;
                if(hip_ent_[k]){const auto current=hip_entity_current(base,read,k);if(!current || !*current)continue;}
                if(hip_ent_[k] && !current_show()) {
                    const auto current=hip_entity_current(base,read,k);
                    if(current && *current && hip_shown_[k] && visible(base,read,hip_ent_[k],false))hip_shown_[k]=false;
                    return;
                }
            }
            if(!hip_ent_[k]) continue;
            if(mine) {
                if(!hip_shown_[k]) {if(!visible(base,read,hip_ent_[k],true))continue;hip_shown_[k]=true;}
                if(!current_show()) {
                    const auto current=hip_entity_current(base,read,k);
                    if(current && *current && visible(base,read,hip_ent_[k],false))hip_shown_[k]=false;
                    continue;
                }
                const auto current=hip_entity_current(base,read,k);if(!current || !*current)continue;
                teleport<Read>(base,hip_ent_[k],h.pos,h.axis);++hip_frames;
            }
            else if(hip_shown_[k] && visible(base,read,hip_ent_[k],false))hip_shown_[k]=false;
        }
    }
public:
    // Dual wield visuals (docs/research-dual-wield.md; off unless mirror_hands(true), then nothing below runs). The engine keeps the sword in slot 0
    // on the RIGHT arm and the pistol/crossbow in slot 1 on the LEFT arm; when the controllers swap roles the lead mirrors the hands, the arms stay
    // with their own controllers, and these weapons would be drawn in the wrong hands. So the native weapon entity is hidden and a copy
    // (the cinematic *_midnight.def models of the dropped sword and the hip gun: no physics, no audio) is teleported to the OTHER hand at
    // hand_pose[other] * mirror(offset), offset = inv(hand_pose[native side]) * the native weapon's world frame, measured live while the native
    // weapon rides its own arm, then held. Grenades, mines, razors and powers have no pure-visual copy def (their *_equipment.def carry the
    // real throwable/placeable component and audio): they stay native.
    // hand_pose: any thread (render), every frame, the DRAWN hand in game world; side 0 left, 1 right. mirror_hands: any thread, every control
    // frame. clear_copies: remove the copies on the next game-thread service (session stop); a level change needs no call (every pointer is checked).
    // root: the player's body model origin read on the render thread with this hand (render_player_root). A copy placed on the game thread
    // is carried by the player's motion since then (v771, headset v770: running, the right hand's copy jerked a frame behind the hand).
    static void head_follow(double rad) noexcept {follow_rad_.store(rad,std::memory_order_relaxed);} // render thread: NativeTurning::head_follow_total() (v816, service_held)
    static void hand_pose(unsigned side,const WorldTrackedPose& drawn,const std::optional<Vec3>& root=std::nullopt) noexcept {
        if(side>1 || !held_copy::valid(drawn)) return;
        const auto now=std::chrono::steady_clock::now();
        std::lock_guard<std::mutex> l(feed_m_);auto& f=feed_[side];
        const double dt=f.have?std::chrono::duration<double>(now-f.at).count():1e9;
        if(dt>.0005 && dt<.25) {const auto m=held_copy::motion(f.pose,drawn,dt);f.speed={f.speed.linear*.7+m.linear*.3,f.speed.angular*.7+m.angular*.3};}
        else if(dt>=.25) f.speed={1,10}; // unknown after a gap: calm only after a few frames
        f.pose=drawn;f.at=now;f.have=true;f.have_root=root.has_value();if(root) f.root=*root;
    }
    static void mirror_hands(bool mirrored) noexcept {mirrored_.store(mirrored,std::memory_order_relaxed);}
    static void held_root_step(bool on) noexcept {held_step_on_.store(on,std::memory_order_relaxed);} // v776 test switch (see service_held)
    static void clear_copies() noexcept {clear_req_.store(true,std::memory_order_relaxed);clear_guns_req_.store(true,std::memory_order_relaxed);}
    // Status: samples taken, copies spawned, teleports, native hides (re-hides included) and restores, failures; err = the copy's drawn model
    // origin vs the pose it was told (mm, hand still; -1 unknown: a constant error is a model/entity frame offset to add); bits: 1<<slot offset
    // locked, 4<<slot copy shown; offset = per slot the held offset position in the hand frame (m).
    inline static std::atomic<std::uint64_t> mirror_fast{},mirror_measured{},mirror_spawned{},mirror_teleports{},mirror_hides{},mirror_restores{},mirror_failed{};
    inline static std::atomic<float> mirror_err_mm{-1.f};inline static std::atomic<unsigned> mirror_bits{};
    inline static std::array<std::atomic<float>,6> mirror_offset{};
    static std::string mirror_status() {
        char b[448];
        std::snprintf(b,sizeof(b),"{\"mirrored\":%d,\"bits\":%u,\"measured\":%llu,\"spawned\":%llu,\"teleports\":%llu,\"hides\":%llu,\"restores\":%llu,\"failed\":%llu,\"err_mm\":%.1f,"
            "\"offset\":[%.3f,%.3f,%.3f,%.3f,%.3f,%.3f]}",mirrored_.load()?1:0,mirror_bits.load(),static_cast<unsigned long long>(mirror_measured.load()),
            static_cast<unsigned long long>(mirror_spawned.load()),static_cast<unsigned long long>(mirror_teleports.load()),static_cast<unsigned long long>(mirror_hides.load()),
            static_cast<unsigned long long>(mirror_restores.load()),static_cast<unsigned long long>(mirror_failed.load()),mirror_err_mm.load(),
            mirror_offset[0].load(),mirror_offset[1].load(),mirror_offset[2].load(),mirror_offset[3].load(),mirror_offset[4].load(),mirror_offset[5].load());
        return b;
    }
private:
    inline static std::atomic<bool> mirrored_{},clear_req_{};inline static std::mutex feed_m_;inline static std::array<HeldFeed,2> feed_{};
    inline static std::array<CopyDraw,2> copy_draw_{};inline static std::array<std::uint64_t,2> copy_serial_{};inline static std::atomic<CopyDrawWrite> draw_write_{};
    // The copy's read hand in the native model-frame layout (weapon_draw::model_of): origin, three axis rows, scales.
    static std::array<float,15> draw_frame(const held_copy::Pose& p) noexcept {
        std::array<float,15> f{};
        f[0]=static_cast<float>(p.position.x);f[1]=static_cast<float>(p.position.y);f[2]=static_cast<float>(p.position.z);
        for(unsigned c=0;c<3;++c) {f[3+c*3]=static_cast<float>(p.basis[c].x);f[3+c*3+1]=static_cast<float>(p.basis[c].y);f[3+c*3+2]=static_cast<float>(p.basis[c].z);}
        f[12]=f[13]=f[14]=1;
        return f;
    }
    // Called by the copy services after the copy is placed/shown. A new serial is the new (copy frame, hand read) pair. v1102: also
    // reports the placement (and its physical hand) to the draw-time publisher; kind: 2 mirror copy, 3 mod-held copy. v1104: the read is
    // the controller GRIP frame and the note says so (WriteLog read_grip); the returned WriteLog serial is kept for the publisher's fallback.
    // v1106: also report the EXACT local offset the copy service applied (`applied`; already mirrored for the other hand). The binding
    // learns G from it directly, so a copy is never re-derived from a propagated entity frame and cannot inherit the hand feed's world.
    static void note_copy_draw(unsigned side,std::uintptr_t entity,std::uint32_t id,const held_copy::Pose& read,
            const held_copy::Pose& applied,unsigned kind) noexcept {
        if(side>1 || !entity || !held_copy::valid(read)) return;
        const auto frame=draw_frame(read);
        std::lock_guard<std::mutex> l(feed_m_);
        CopyDraw& d=copy_draw_[side&1u];
        d.valid=true;d.entity=entity;d.id=id;d.read=frame;d.serial=++copy_serial_[side&1u];d.kind=kind;d.write_serial=0;
        if(held_copy::valid(applied)) {d.offset=draw_frame(applied);d.offset_valid=true;}
        else {d.offset={};d.offset_valid=false;}
        if(const auto sink=draw_write_.load(std::memory_order_acquire)) d.write_serial=sink(side,entity,id,frame.data(),kind);
    }
    // The copy is hidden, released, or replaced: no source (the publisher also rejects a pair whose model frame moved without a serial).
    static void clear_copy_draw(std::uintptr_t entity=0) noexcept {
        std::lock_guard<std::mutex> l(feed_m_);
        for(auto& d:copy_draw_) if(d.valid && (!entity || d.entity==entity)) d=CopyDraw{};
    }
    inline static std::array<HeldSlot,2> mirror_slots_{};inline static std::array<HeldCopy,5> copies_{};inline static std::uintptr_t mirror_game_{};inline static bool mirror_live_{};
    // Copy defs by index: 0 the sword, then gun/crossbow of Corvo, gun/crossbow of Emily (the hip clones' defs; 1+2*emily+crossbow).
    static constexpr std::array<const char*,5> mirror_def_names{{sword_copy_decl,"models/campaign/dunwall/midnightassets/gun_corvo_midnight.def",
        "models/campaign/dunwall/midnightassets/crossbow_corvo_midnight.def","models/campaign/dunwall/midnightassets/gun_emily_midnight.def",
        "models/campaign/dunwall/midnightassets/crossbow_emily_midnight.def"}};
    static bool feed(unsigned side,held_copy::Pose& pose,held_copy::Motion& speed,double max_age_s,std::optional<Vec3>* root=nullptr) {
        std::lock_guard<std::mutex> l(feed_m_);const auto& f=feed_[side&1u];
        if(!f.have || std::chrono::duration<double>(std::chrono::steady_clock::now()-f.at).count()>max_age_s) return false;
        pose=f.pose;speed=f.speed;if(root) *root=f.have_root?std::optional<Vec3>{f.root}:std::nullopt;return true;
    }
    // The player's body model origin (model+0x1860, as render_player_root reads it on the render thread).
    // yaw (optional): the frame's yaw (+0x186c rows) minus NativeTurning's head-follow total: the heading the camera is placed with (BodyAnchor).
    template<class Read> static std::optional<Vec3> player_root(std::uintptr_t base,Read& read,double* yaw=nullptr) {
        const auto p=player(base,read);const auto model=p?rig::value<std::uintptr_t>(read,p+0xf0):std::nullopt;std::array<float,12> o{};
        if(!model || !rig::pointer(*model) || !read(*model+0x1860,o.data(),yaw?sizeof(o):12)) return std::nullopt;
        if(yaw) *yaw=held_copy::frame_yaw(held_copy::from_rows(o.data(),o.data()+3))-follow_rad_.load(std::memory_order_relaxed);
        return Vec3{o[0],o[1],o[2]};
    }
    static void remove_entity(std::uintptr_t base,std::uintptr_t e) { // the removal pinned in verify(): killEntity's remove event
        reinterpret_cast<void (__fastcall*)(std::uintptr_t,std::uintptr_t,int)>(base+0x883f60)(e,base+0x39a6f30,0);
    }
    template<class Read> static std::string entity_decl_name(Read& read,std::uintptr_t e) {
        const auto decl=rig::value<std::uintptr_t>(read,e+0x2e8);const auto name=decl?rig::value<std::uintptr_t>(read,*decl+0x48):std::nullopt;
        std::array<char,96> text{};
        if(!name || !rig::pointer(*name) || !read(*name,text.data(),text.size()-1)) return {};
        std::string n(text.data());for(auto& c:n) c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return n;
    }
    // A weapon entity's kind by its components (entity+0x268, as the player's): 0 pistol, 1 crossbow, -1 neither or unreadable.
    template<class Read> static int weapon_kind(std::uintptr_t base,Read& read,std::uintptr_t entity) {
        const auto comps=rig::list(read,entity+0x268,4096);if(!comps) return -1;
        for(unsigned i=0;i<comps->packed_count && i<64;++i) {
            const auto c=rig::value<std::uintptr_t>(read,comps->data+i*8ull);if(!c || !*c) continue;
            if(rig::typed(read,base,*c,0x1b53f58)) return 0;  // arkCpntWeaponGun
            if(rig::typed(read,base,*c,0x1b9cbf8)) return 1;  // arkCpntWeaponCrossbow
        }
        return -1;
    }
    // v1105: the kind of the EQUIPPED component of an inventory item record, through the path the left-weapon socket receipts use
    // (src/native_left_weapon_socket.hpp:104-107): the record names its component (+0x10) and declaration (+0x18), and the component
    // matches the exe's arkCpntWeaponGun / arkCpntWeaponCrossbow vtable together with that class's declaration vtable. The vtable RVAs
    // are the ones proved against Dishonored2.exe on disk (artifacts/round5/weapon-rtti.txt: the gun's CompleteObjectLocator at RVA
    // 0x1f9ac38 names vtable RVA 0x1b53f58, the crossbow's 0x1fbfe28 names 0x1b9cbf8, both from RTTI TypeDescriptor .?AVarkCpnt...).
    // This is the item's own component, not a scan of the entity's list: the list may hold several components and the scan skipped the
    // declaration check the game's own equip path uses. -1 = not a gun/crossbow (or unreadable).
    template<class Read> static int item_kind(std::uintptr_t base,Read& read,std::uintptr_t item) {
        if(!item) return -1;
        const auto component=rig::value<std::uintptr_t>(read,item+0x10);
        const auto declaration=rig::value<std::uintptr_t>(read,item+0x18);
        if(!component || !declaration || !rig::pointer(*component) || !rig::pointer(*declaration)) return -1;
        if(rig::typed(read,base,*component,0x1b53f58) && rig::typed(read,base,*declaration,0x1b53e18)) return 0;  // arkCpntWeaponGun + its decl
        if(rig::typed(read,base,*component,0x1b9cbf8) && rig::typed(read,base,*declaration,0x1b9cab8)) return 1;  // arkCpntWeaponCrossbow + its decl
        return -1;
    }
    // The item in an inventory slot (+0x68 sword, +0x70 the other hand; item+8 = its weapon entity's id) and the copy def it gets. false = unreadable
    // this time (keep the state); true with cand 0 = the slot is empty.
    template<class Read> static bool mirror_lookup(std::uintptr_t base,Read& read,unsigned slot,HeldSlot& s) {
        s.cand=0;s.cand_id=0;s.cand_def=HeldSlot::none;
        const auto arms=rig::player_arms(read,base);if(!arms) return false;
        const auto inventory=rig::value<std::uintptr_t>(read,(*arms)[0].body+0x60);
        if(!inventory || !rig::typed(read,base,*inventory,0x1b9a878) || rig::value<std::uintptr_t>(read,*inventory+0x10)!=(*arms)[0].player) return false;
        const auto item=rig::value<std::uintptr_t>(read,*inventory+0x68+slot*8ull);if(!item) return false;
        if(!*item) return true;
        const auto id=rig::pointer(*item)?rig::value<std::uint32_t>(read,*item+8):std::nullopt;
        if(!id || *id==0x3fff0010 || (*id>>16)>=0x3fff) return false;
        const auto game=rig::value<std::uintptr_t>(read,base+0x39a5290);
        const auto entity=game?rig::value<std::uintptr_t>(read,*game+0x3130b0+(*id>>16)*8ull):std::nullopt;
        if(!entity || !alive_entity(base,read,*entity,*id)) return false;
        s.cand=*entity;s.cand_id=*id;
        if(slot==0) {s.cand_def=0;return true;}
        const auto name=entity_decl_name(read,*entity);
        // v762: the kind by the weapon's own component (RTTI arkCpntWeaponGun / arkCpntWeaponCrossbow). The decl-name read never matched live
        // (headset v761: gun_item_frames 0, all 20 native gun drops refused, copies never measured), so the name is only a fallback.
        const int by_component=weapon_kind(base,read,*entity);
        const bool crossbow=by_component==1 || (by_component<0 && name.find("crossbow")!=std::string::npos),
            gun=by_component==0 || (by_component<0 && (name.find("gun")!=std::string::npos || name.find("pistol")!=std::string::npos));
        if(!crossbow && !gun) return true; // grenades, mines, razors, powers, the Heart: no copy def, they stay native
        const bool emily=name.find("emily")!=std::string::npos || (name.find("corvo")==std::string::npos && emily_==1);
        s.cand_def=1+(emily?2u:0u)+(crossbow?1u:0u);return true;
    }
    // The slot's copy goes out of sight (or is removed) and the native weapon is shown again, but only if it is the equipped item: a holstered
    // weapon is the engine's to show (owed: shown the moment it is equipped again, in case the engine never hid it itself).
    template<class Read> static void mirror_release(std::uintptr_t base,Read& read,unsigned slot,bool remove=false) {
        auto& s=mirror_slots_[slot];
        if(s.def<copies_.size()) {auto& c=copies_[s.def];
            if(c.ent) {const bool live=alive_entity(base,read,c.ent,c.id);
                clear_copy_draw(c.ent);   // v1101: no copy is drawn by this hand any more
                if(live && remove) remove_entity(base,c.ent);
                else if(live && c.shown) visible(base,read,c.ent,false);
                if(!live || remove) c={};else c.shown=false;}}
        if(s.we_hid && s.native && alive_entity(base,read,s.native,s.native_id)) {
            HeldSlot probe;
            if(mirror_lookup(base,read,slot,probe) && probe.cand==s.native) {if(visible(base,read,s.native,true)) ++mirror_restores;}
            else {s.owed=s.native;s.owed_id=s.native_id;}
        }
        s.drop();
    }
    template<class Read> static void mirror_owed(std::uintptr_t base,Read& read,unsigned slot,std::uint64_t now) {
        auto& s=mirror_slots_[slot];if(!s.owed || now-s.owed_looked<20) return;
        s.owed_looked=now;
        if(!alive_entity(base,read,s.owed,s.owed_id)) {s.owed=0;return;}
        HeldSlot probe;if(!mirror_lookup(base,read,slot,probe) || probe.cand!=s.owed) return; // not equipped yet
        const auto flags=rig::value<std::uint8_t>(read,s.owed+0xd0);
        if(flags && (*flags&1) && visible(base,read,s.owed,true)) ++mirror_restores;
        s.owed=0;
    }
    template<class Read> static void service_mirror(std::uintptr_t base,Read& read) {
        const bool want=mirrored_.load(std::memory_order_relaxed),clear=clear_req_.exchange(false,std::memory_order_relaxed);
        if(!want && !mirror_live_ && !clear) return; // knob off: nothing below runs and nothing is touched
        const auto game=rig::value<std::uintptr_t>(read,base+0x39a5290);
        if(!game || *game!=mirror_game_) { // a new level or session: every pointer is dead, call nothing
            for(auto& s:mirror_slots_) s={};
            copies_={};clear_copy_draw();mirror_game_=game?*game:0;mirror_live_=false;mirror_bits=0;
            if(!game) return;
        }
        const auto now=GetTickCount64();
        if(clear) {
            for(unsigned slot=0;slot<2;++slot) mirror_release(base,read,slot,true);
            for(auto& c:copies_) {if(c.ent && alive_entity(base,read,c.ent,c.id)) remove_entity(base,c.ent);c={};}
        } else for(unsigned slot=0;slot<2;++slot) {mirror_owed(base,read,slot,now);mirror_slot(base,read,slot,want,now);}
        bool live=false;unsigned bits=0;
        for(unsigned slot=0;slot<2;++slot) {const auto& s=mirror_slots_[slot];
            live=live || s.native || s.owed;if(s.locked) bits|=1u<<slot;if(s.def<copies_.size() && copies_[s.def].shown) bits|=4u<<slot;}
        mirror_live_=live;mirror_bits=bits;
    }
    template<class Read> static void mirror_slot(std::uintptr_t base,Read& read,unsigned slot,bool want,std::uint64_t now) {
        auto& s=mirror_slots_[slot];
        if(!want) {if(s.native) mirror_release(base,read,slot);return;}
        if(now-s.looked>=20) {s.looked=now;s.cand_ok=mirror_lookup(base,read,slot,s);}
        if(s.cand_ok) {
            const bool dropped=slot==0 && copy_ && shown_; // the dropped sword lies in the world (a different entity): no held sword copy meanwhile
            const bool usable=!dropped && s.cand && s.cand_def!=HeldSlot::none && s.cand_id!=s.fail_id;
            if(s.native && (!usable || s.cand_id!=s.native_id)) mirror_release(base,read,slot);
            if(!usable) return;
            if(!s.native) {s.native=s.cand;s.native_id=s.cand_id;s.def=s.cand_def;if(s.owed==s.cand) s.owed=0;}
        }
        if(!s.native) return;
        if(!alive_entity(base,read,s.native,s.native_id)) {s.drop();return;}
        const unsigned native_side=slot==0?1u:0u,other_side=1u-native_side;
        const auto flags=rig::value<std::uint8_t>(read,s.native+0xd0);if(!flags) return;
        bool native_hidden=(*flags&1)!=0;
        held_copy::Pose hand{};held_copy::Motion speed{};
        // v816 (user, headset v815: "a pistol picked up into the hand appears with a noticeable delay"): a gun's grip is the kind's last measured one at once; the 12 still samples
        // below (a hand nearly still for 0.25 s, the native gun meanwhile on the wrong arm) are only the first time. The sword keeps measuring (v775).
        if(!s.locked && s.def!=0 && s.def<mirror_def_names.size())
            if(const auto& k=kind_offset_[static_cast<unsigned>(held_copy::kind_of(mirror_def_names[s.def]))&3u];k.have) {
                s.side=native_side;s.offset.value=held_copy::offset_for(k.offset,k.side,native_side);s.offset.have=true;s.offset.stable=12;s.locked=true;++mirror_fast;}
        if(!s.locked) { // measure: the native weapon rides its own arm at its own controller; sample only while that hand is nearly still
            if(native_hidden || !feed(native_side,hand,speed,.1) || speed.linear>.3 || speed.angular>2) return;
            const auto model=rig::value<std::uintptr_t>(read,s.native+0xf0);
            std::array<float,12> frame{}; // model+0x1860 origin[3], +0x186c rows forward/left/up (the frame the barrel aim reads)
            if(!model || !rig::pointer(*model) || !rig::typed(read,base,*model,0x1b0d628) || !read(*model+0x1860,frame.data(),sizeof(frame))) return;
            s.side=native_side;if(s.offset.add(held_copy::to_local(hand,held_copy::from_rows(frame.data(),frame.data()+3)))) ++mirror_measured;
            if(s.offset.ready()) {
                s.locked=true;const auto& p=s.offset.value.position;
                mirror_offset[slot*3]=static_cast<float>(p.x);mirror_offset[slot*3+1]=static_cast<float>(p.y);mirror_offset[slot*3+2]=static_cast<float>(p.z);
            }
            return;
        }
        auto& c=copies_[s.def];
        if(c.ent && !alive_entity(base,read,c.ent,c.id)) c={};
        if(!native_hidden) { // hidden before the copy shows; and again whenever the game shows it
            if(!visible(base,read,s.native,false)) {s.fail_id=s.native_id;++mirror_failed;mirror_release(base,read,slot);return;}
            s.we_hid=true;++mirror_hides;
        }
        held_copy::Pose out{};
        const bool placed=s.we_hid && feed(other_side,hand,speed,.25) && held_copy::target(hand,s.offset.value,out); // hidden by the engine alone: no stand-in
        if(!placed) {if(c.ent && c.shown && visible(base,read,c.ent,false)) c.shown=false;clear_copy_draw(c.ent);return;}
        std::array<float,3> pos{float(out.position.x),float(out.position.y),float(out.position.z)};std::array<float,9> axis{};
        for(unsigned i=0;i<3;++i) {axis[i*3]=float(out.basis[i].x);axis[i*3+1]=float(out.basis[i].y);axis[i*3+2]=float(out.basis[i].z);}
        if(!c.ent) {
            c.ent=spawn_def(base,read,mirror_def_names[s.def],pos.data(),axis.data());
            c.id=c.ent?rig::value<std::uint32_t>(read,c.ent+0xa8).value_or(0):0;c.shown=c.ent!=0;
            if(!c.ent || !alive_entity(base,read,c.ent,c.id)) {c={};s.fail_id=s.native_id;++mirror_failed;mirror_release(base,read,slot);return;} // no copy: the weapon stays native
            ++mirror_spawned;
        } else {
            teleport<Read>(base,c.ent,pos,axis);++mirror_teleports;
            if(!c.shown && visible(base,read,c.ent,true)) c.shown=true; // moved first, then shown: no frame at the old place
        }
        if(s.have_cmd && speed.linear<.05) { // diagnostics: where the engine drew the copy's model vs where it was told (constant = frame offset)
            std::array<float,3> at{};const auto model=rig::value<std::uintptr_t>(read,c.ent+0xf0);
            if(model && rig::pointer(*model) && read(*model+0x1860,at.data(),sizeof(at)))
                mirror_err_mm=static_cast<float>(length(Vec3{at[0],at[1],at[2]}-s.cmd.position)*1000);
        }
        s.cmd=out;s.have_cmd=true;
        note_copy_draw(other_side,c.ent,c.id,hand,held_copy::mirror(s.offset.value),2);   // v1101/v1106: the read and the exact mirrored offset
    }
public:
    // ---- Dual wield, row A (docs/dual-wield-two-hands-plan.md; artifacts/claude/dual-wield/fleet/A-gun-copies.md) ----
    // dual_wield(bool): any thread, every control frame (the lead). Off = nothing below runs and nothing is touched; turning it off removes what it made.
    // gun_drop / gun_pickup (Request ops, game thread): a gun in the left slot leaves the hand at once (EquipChange 64, code 0x20, as the sword's drop on 53)
    //   and a flown copy of its *_midnight.def (the hip clone's def) takes its place in the world; the mod flies it like the dropped sword (SwordFlight).
    //   Op::gun_drop with decl == nullptr drops the native slot-1 gun (pos/axis only a fallback: the pose is taken from where the gun is drawn);
    //   with decl == a def name (a mod-owned copy, e.g. a guard pistol) nothing native changes: that def's copy is thrown from the pose of this side's held copy.
    //   Op::gun_pickup takes the lying copy nearest r.pos: a native one comes back into slot 1 (through the hip lease when r.manual_generation was reserved with
    //   reserve_hip, else/after a refusal as the wheel equips it), a mod-owned one just disappears (the caller held_copy()s it next).
    //   A copy lying > 30 s or > 12 m from the player returns to the hip by itself; a native gun drawn again meanwhile (wheel) calls its copy back.
    // held_copy(side, def): any thread, every frame the caller wants it: a copy of that entity def is shown at that hand (pose = hand_pose(side) * grip offset),
    //   nullptr = none. A def that fails to spawn shows nothing and counts in held_failed (it is not retried until the def changes).
    using GunCopy=GunCopyInfo;
    static void dual_wield(bool on) noexcept {dual_wield_.store(on,std::memory_order_relaxed);}
    // The native weapons ride the other hands (the lead's attachment cross while mirrored): grip offsets are measured at the hand they ride.
    static void native_rides_other(bool on) noexcept {ride_other_.store(on,std::memory_order_relaxed);}
    inline static std::atomic<std::uint64_t> slot1_restores{};
    static bool dual_wield() noexcept {return dual_wield_.load(std::memory_order_relaxed);}
    static GunCopy gun_copy(unsigned index=0) {std::lock_guard<std::mutex> l(m_);return published_guns_[index&1u];}
    static std::array<GunCopy,2> gun_copies() {std::lock_guard<std::mutex> l(m_);return published_guns_;}
    // The first present lying native gun (the one a GunGrip pickup is for), else an empty GunCopy.
    static GunCopy gun_copy_native() {std::lock_guard<std::mutex> l(m_);for(const auto& g:published_guns_) if(g.present && g.native) return g;return {};}
    static void held_copy(unsigned side,const char* def_name) {held_want_[side&1u].store(def_name && *def_name?intern(def_name):nullptr,std::memory_order_relaxed);}
    // The held copy's current world frame (its model's row 0 is the barrel/blade forward; position = the model origin), nothing while it is not shown.
    static std::optional<WorldTrackedPose> held_copy_pose(unsigned side) {
        std::lock_guard<std::mutex> l(feed_m_);const auto& p=held_pub_[side&1u];
        if(!p.valid || steady_ms()-p.tick>250) return std::nullopt;
        return p.pose;
    }
    // v1101: the copy this hand draws (mirror copy or mod-owned held copy), for the weapon placement's publisher. Any thread.
    static CopyDraw copy_draw(unsigned side) noexcept {
        if(side>1) return {};
        std::lock_guard<std::mutex> l(feed_m_);
        return copy_draw_[side];
    }
    // v1104: the slot-1 item's component type as the grip-clip table keys it (1 pistol, 2 crossbow, 0 other/unknown). The decl names
    // do not carry the kind (the dual-wield notes), so this is the same arkCpntWeaponGun / arkCpntWeaponCrossbow test the copy code uses.
    template<class Read> static int weapon_type(std::uintptr_t base,Read& read,std::uintptr_t entity) {
        const int kind=weapon_kind(base,read,entity);
        return kind==0?1:kind==1?2:0;
    }
    // v1105: the type of the item record itself (grip_clips::Type: 1 pistol, 2 crossbow, 0 other/unknown), from the same verified
    // component+declaration pair as the game's equip path. This is what dxgi_probe reads from the hand-state snapshot, so the type
    // cannot go to 0 just because a second player-graph walk on another thread failed (the v1012 status stayed [0,0]).
    template<class Read> static int item_type(std::uintptr_t base,Read& read,std::uintptr_t item) {
        const int kind=item_kind(base,read,item);
        if(kind>=0) return kind==0?1:2;
        // v1106: the item record's pair is the primary path (the game's own equip path). If it does not type this frame (a stale record
        // during an equip/load, or an item whose declaration is not the class vtable), fall back to the weapon ENTITY's component list,
        // the round-4 path that the barrel aim already uses live. It is a union test (a component of the right class anywhere in the
        // entity), so it is only a fallback; a record that types is never overridden by it.
        try {
            const auto id=rig::value<std::uint32_t>(read,item+8);
            if(!id || *id==0x3fff0010 || (*id>>16)>=0x3fff) return 0;
            const auto game=rig::value<std::uintptr_t>(read,base+0x39a5290);
            if(!game || !*game) return 0;
            const auto entity=rig::value<std::uintptr_t>(read,*game+0x3130b0+(*id>>16)*8ull);
            if(!entity || !rig::pointer(*entity)) return 0;
            const int by_entity=weapon_kind(base,read,*entity);
            return by_entity==0?1:by_entity==1?2:0;
        } catch(...) {return 0;}
    }
    template<class Read> static int slot1_type(std::uintptr_t base,Read& read) {
        const auto arms=rig::player_arms(read,base);if(!arms) return 0;
        const auto inventory=rig::value<std::uintptr_t>(read,(*arms)[0].body+0x60);if(!inventory || !*inventory) return 0;
        const auto item=rig::value<std::uintptr_t>(read,*inventory+0x70);if(!item || !*item) return 0;
        return item_type(base,read,*item);
    }
    // v1102: every copy placement (service_mirror, service_held) also reports the exact hand pose it placed from, so the draw-time
    // publisher can correct the copy to the hand its render draws (src/weapon_draw_place.hpp WriteLog).
    using DrawWrite=CopyDrawWrite;
    static void draw_write(DrawWrite sink) noexcept {draw_write_.store(sink,std::memory_order_release);}
    // The held copy's entity and id (the gun that fires from that hand; the game thread re-checks both), {0,0} while it is not shown.
    static std::pair<std::uintptr_t,std::uint32_t> held_copy_entity(unsigned side) {
        std::lock_guard<std::mutex> l(feed_m_);const auto& p=held_pub_[side&1u];
        return p.valid && steady_ms()-p.tick<=250?std::make_pair(p.ent,p.id):std::make_pair<std::uintptr_t,std::uint32_t>(0,0);
    }
    inline static std::atomic<std::uint64_t> gun_drops{},gun_drops_native{},gun_pickups{},gun_pickups_explicit{},gun_returns{},gun_redraws{},gun_failed{},gun_spawned{},
        gun_teleports{},gun_flight_frames{},gun_stows{},held_spawned{},held_teleports{},held_failed{},held_hides{},held_measured{},held_carried{},held_rejected{},gun_hits{},bash_hits{},gadget_hits{};
    inline static std::atomic<unsigned> held_bits{}; // 1<<side: a copy is shown there; 4<<side: its offset is measured from the native twin (else the per-kind constant)
    inline static std::atomic<float> last_return_dz{},last_return_age_s{}; // v781: the last lying gun sent back to the hip: its height over the player (m), its age (s)
    static std::string gun_status() {
        const auto lying=gun_copies();std::string l;
        for(unsigned i=0;i<2;++i) {
            const auto& g=lying[i];char b[200];
            if(!g.present) {l+=i?",null":"null";continue;}
            std::snprintf(b,sizeof(b),"%s{\"native\":%d,\"flying\":%d,\"kind\":%u,\"pos\":[%.2f,%.2f,%.2f],\"since\":%llu}",i?",":"",g.native?1:0,g.flying?1:0,g.kind,
                g.pos[0],g.pos[1],g.pos[2],static_cast<unsigned long long>(g.since));
            l+=b;
        }
        std::string failed;{std::lock_guard<std::mutex> lock(feed_m_);failed=held_last_failed_.data();}
        for(auto& c:failed) if(c=='"' || c=='\\' || static_cast<unsigned char>(c)<32) c='?';
        std::string kinds; // the last measured grip per kind (pistol, crossbow, sword): have, side, position, basis rows (sim: the copies' constants)
        for(unsigned i=1;i<4;++i) {const auto& o=kind_offset_[i];const auto& q=o.offset;char kb[300];
            std::snprintf(kb,sizeof(kb),"%s[%d,%u,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f]",i>1?",":"",o.have?1:0,o.side,q.position.x,q.position.y,q.position.z,
                q.basis[0].x,q.basis[0].y,q.basis[0].z,q.basis[1].x,q.basis[1].y,q.basis[1].z,q.basis[2].x,q.basis[2].y,q.basis[2].z);kinds+=kb;}
        std::string pin;   // v1114: the copy scenes' pinned prologues (scene_pin_rvas order), once read
        if(scene_pin_.load()>=0) for(const auto& p:scene_pin_bytes_) {if(!pin.empty()) pin+=' ';for(const auto c:p) {char h[4];std::snprintf(h,sizeof(h),"%02x",static_cast<unsigned>(c));pin+=h;}}
        char b[3072];
        std::snprintf(b,sizeof(b),"{\"on\":%d,\"drops\":%llu,\"native_drops\":%llu,\"pickups\":%llu,\"explicit_pickups\":%llu,\"returns\":%llu,\"redraws\":%llu,\"failed\":%llu,"
            "\"spawned\":%llu,\"teleports\":%llu,\"flight_frames\":%llu,\"stows\":%llu,\"lying\":[%s],\"held\":{\"spawned\":%llu,\"teleports\":%llu,\"failed\":%llu,\"hides\":%llu,"
            "\"measured\":%llu,\"rejected\":%llu,\"carried\":%llu,\"step_mm\":%.1f,\"turn_mdeg\":%.0f,\"bits\":%u,\"want\":[%d,%d],\"last_failed\":\"%s\",\"kinds\":[%s]},\"restores\":%llu,\"rides_other\":%d,"
            "\"last_return\":[%.2f,%.1f],\"stuns\":[%llu,%llu,%llu],\"upgrade_parts\":[%llu,%llu,%llu,%llu,%llu,%llu],\"real\":[%llu,%llu,%llu,%llu,%llu],\"parts\":[%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu],"
            "\"scenes\":{\"knob_off\":%d,\"pin\":%d,\"faults\":%llu,\"conds\":%llu,\"late_conds\":%llu,\"stops\":%llu,\"bytes\":\"%s\"}}",dual_wield()?1:0,
            static_cast<unsigned long long>(gun_drops.load()),static_cast<unsigned long long>(gun_drops_native.load()),static_cast<unsigned long long>(gun_pickups.load()),
            static_cast<unsigned long long>(gun_pickups_explicit.load()),static_cast<unsigned long long>(gun_returns.load()),static_cast<unsigned long long>(gun_redraws.load()),
            static_cast<unsigned long long>(gun_failed.load()),static_cast<unsigned long long>(gun_spawned.load()),static_cast<unsigned long long>(gun_teleports.load()),
            static_cast<unsigned long long>(gun_flight_frames.load()),static_cast<unsigned long long>(gun_stows.load()),l.c_str(),
            static_cast<unsigned long long>(held_spawned.load()),static_cast<unsigned long long>(held_teleports.load()),static_cast<unsigned long long>(held_failed.load()),
            static_cast<unsigned long long>(held_hides.load()),static_cast<unsigned long long>(held_measured.load()),static_cast<unsigned long long>(held_rejected.load()),static_cast<unsigned long long>(held_carried.load()),static_cast<double>(held_step_mm.load()),static_cast<double>(held_turn_mdeg.load()),held_bits.load(),
            held_want_[0].load()?1:0,held_want_[1].load()?1:0,failed.c_str(),kinds.c_str(),static_cast<unsigned long long>(slot1_restores.load()),ride_other_.load()?1:0,
            static_cast<double>(last_return_dz.load()),static_cast<double>(last_return_age_s.load()),
            static_cast<unsigned long long>(gun_hits.load()),static_cast<unsigned long long>(bash_hits.load()),static_cast<unsigned long long>(gadget_hits.load()),
            static_cast<unsigned long long>(upgrade_parts.load()),static_cast<unsigned long long>(upgrade_faults.load()),static_cast<unsigned long long>(upgrade_skips.load()),static_cast<unsigned long long>(upgrade_rebinds.load()),
            static_cast<unsigned long long>(upgrade_anims.load()),static_cast<unsigned long long>(copy_bolts.load()),
            static_cast<unsigned long long>(real_spawned.load()),static_cast<unsigned long long>(real_upgrade_sets.load()),static_cast<unsigned long long>(real_reshows.load()),
            static_cast<unsigned long long>(real_anchors.load()),static_cast<unsigned long long>(real_rebinds.load()),
            static_cast<unsigned long long>(real_parts_near.load()),static_cast<unsigned long long>(real_parts_far.load()),static_cast<unsigned long long>(real_parts_unhid.load()),
            static_cast<unsigned long long>(real_parts_rebound.load()),static_cast<unsigned long long>(real_bolt_far.load()),static_cast<unsigned long long>(children_presented.load()),
            static_cast<unsigned long long>(tree_presents.load()),static_cast<unsigned long long>(tree_present_blocked.load()),static_cast<unsigned long long>(handovers.load()),
            static_cast<unsigned long long>(scene_fire.load()),static_cast<unsigned long long>(scene_reload.load()),static_cast<unsigned long long>(scene_fail.load()),static_cast<unsigned long long>(scene_running.load()),
            scene_knob_.load(),scene_pin_.load(),static_cast<unsigned long long>(scene_faults.load()),static_cast<unsigned long long>(scene_conds.load()),
            static_cast<unsigned long long>(scene_late.load()),static_cast<unsigned long long>(scene_stops.load()),pin.c_str());
        return b;
    }
private:
    inline static std::atomic<bool> dual_wield_{},clear_guns_req_{};
    inline static std::atomic<bool> ride_other_{};inline static std::uintptr_t slot1_cur_{},slot1_prev_{};inline static std::uint64_t slot1_looked_{};
    inline static std::array<LyingGun,2> lying_{};inline static bool gun_native_lying_{},guns_live_{};inline static std::uintptr_t guns_game_{};
    inline static std::array<PoolEnt,8> pool_{};inline static std::array<HeldSide,2> held_{};inline static std::array<Adopt,2> adopt_{};inline static std::atomic<std::uint64_t> handovers{};inline static std::array<HeldSlot,2> meas_{};
    inline static std::array<std::atomic<const char*>,2> held_want_{};inline static std::array<HeldPub,2> held_pub_{};inline static std::array<char,96> held_last_failed_{};
    inline static std::array<GunCopy,2> published_guns_{};inline static std::uint64_t guns_player_check_{};inline static float guns_player_[3]{};
    static std::uint64_t steady_ms() {return static_cast<std::uint64_t>(std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count()*1000);}
    static const char* intern(const char* name) { // std::set nodes keep their address: a stable const char* for the life of the process
        static std::mutex mutex;static std::set<std::string,std::less<>> names;
        std::lock_guard<std::mutex> l(mutex);
        const auto found=names.find(std::string_view(name)); // held_copy() is called every frame: no allocation for a name already known
        return (found!=names.end()?found:names.emplace(name).first)->c_str();
    }
    static held_copy::Pose request_pose(const Request& r) {return held_copy::from_rows(r.pos.data(),r.axis.data());}
    template<class Read> static bool pool_alive(std::uintptr_t base,Read& read,int pe) {
        return pe>=0 && pe<static_cast<int>(pool_.size()) && pool_[pe].ent && alive_entity(base,read,pool_[pe].ent,pool_[pe].id);
    }
    // A copy of `def` at (pos, axis), visible: a pooled hidden one is moved there and shown, else a new one is spawned (a full pool removes an idle entry). -1 = failed.
    template<class Read> static int pool_acquire(std::uintptr_t base,Read& read,const char* def,const std::array<float,3>& pos,const std::array<float,9>& axis) {
        if(!def) return -1;
        for(int i=0;i<static_cast<int>(pool_.size());++i) {auto& e=pool_[i];
            if(!e.ent || e.in_use) continue;
            if(!alive_entity(base,read,e.ent,e.id)) {e={};continue;}
            if(std::strcmp(e.def,def)) continue;
            teleport<Read>(base,e.ent,pos,axis);
            if(!e.shown && !visible(base,read,e.ent,true)) return -1;
            e.shown=true;e.in_use=true;return i;
        }
        int slot=-1;
        for(int i=0;i<static_cast<int>(pool_.size());++i) if(!pool_[i].ent) {slot=i;break;}
        if(slot<0) for(int i=0;i<static_cast<int>(pool_.size());++i) if(!pool_[i].in_use) {
            if(alive_entity(base,read,pool_[i].ent,pool_[i].id)) remove_entity(base,pool_[i].ent);
            pool_[i]={};slot=i;break;
        }
        if(slot<0) return -1;
        const auto ent=spawn_def(base,read,def,pos.data(),axis.data());
        const std::uint32_t id=ent?rig::value<std::uint32_t>(read,ent+0xa8).value_or(0):0u;
        if(!ent || !alive_entity(base,read,ent,id)) return -1;
        pool_[slot]=PoolEnt{def,ent,id,true,true};++gun_spawned;return slot;
    }
    template<class Read> static void pool_release(std::uintptr_t base,Read& read,int pe) {
        if(pe<0 || pe>=static_cast<int>(pool_.size())) return;
        auto& e=pool_[pe];if(!e.ent) return;
        if(!alive_entity(base,read,e.ent,e.id)) {e={};return;}
        if(e.shown && visible(base,read,e.ent,false)) e.shown=false;
        e.in_use=false;
    }
    template<class Read> static void gun_release(std::uintptr_t base,Read& read,LyingGun& g) {
        pool_release(base,read,g.pe);g=LyingGun{};
        gun_native_lying_=false;for(const auto& o:lying_) gun_native_lying_=gun_native_lying_ || (o.present && o.native);
    }
    // The native slot-1 gun (inv+0x70) with its controller memory: entity and def index from the mirror lookup, the item record and the ammo record selected with it.
    struct Slot1 {std::uintptr_t fsm{},inventory{},item{},ammo{},item_decl{},ammo_decl{},entity{};std::uint32_t id{},spawn{};unsigned def{HeldSlot::none};};
    template<class Read> static bool read_slot1(std::uintptr_t base,Read& read,Slot1& s) {
        HeldSlot probe;if(!mirror_lookup(base,read,1,probe) || !probe.cand || probe.cand_def==HeldSlot::none) return false;
        s.entity=probe.cand;s.id=probe.cand_id;s.def=probe.cand_def;
        s.fsm=player_fsm(base,read);if(!s.fsm) return false;
        const auto inventory=rig::value<std::uintptr_t>(read,s.fsm+0x328),controller=rig::value<std::uintptr_t>(read,s.fsm+0x2f8);
        if(!inventory || !*inventory || !rig::typed(read,base,*inventory,0x1b9a878)) return false;
        s.inventory=*inventory;
        const auto item=rig::value<std::uintptr_t>(read,*inventory+0x70);if(!item || !*item) return false;
        s.item=*item;const auto decl=rig::value<std::uintptr_t>(read,*item);const auto spawn=rig::value<std::uint32_t>(read,*item+8);
        if(!decl || !*decl || !spawn || *spawn!=s.id) return false;
        s.item_decl=*decl;s.spawn=*spawn;
        if(controller && *controller && rig::value<std::uintptr_t>(read,*controller+0x190)==s.item) { // the ammo selected with this gun
            const auto ammo=rig::value<std::uintptr_t>(read,*controller+0x198);
            if(ammo && *ammo) {s.ammo=*ammo;s.ammo_decl=rig::value<std::uintptr_t>(read,*ammo).value_or(0);}
        }
        return true;
    }
    // The inventory record (stride 0x38 from +0x38; +0 decl, +8 the weapon entity's id) of a decl: the pointer moves when the list grows.
    template<class Read> static std::uintptr_t find_record(Read& read,std::uintptr_t inventory,std::uintptr_t decl,std::uint32_t spawn) {
        const auto entries=decl?rig::list(read,inventory+0x38,4096):std::nullopt;if(!entries) return 0;
        for(unsigned i=0;i<entries->packed_count;++i) {
            const auto record=entries->data+i*0x38ull;
            if(rig::value<std::uintptr_t>(read,record)==decl && (!spawn || rig::value<std::uint32_t>(read,record+8)==spawn)) return record;
        }
        return 0;
    }
    // The wheel's own equip (7fb350 builds exactly this and requests 64): 835520(params, item record, ammo record, code 0x20 for a gun, requester 0x5b).
    template<class Read> static bool equip_item(std::uintptr_t base,Read& read,std::uintptr_t item,std::uintptr_t ammo,int code=0x20) {
        static const bool pinned=[&]{std::array<unsigned char,16> got{};
            const std::array<unsigned char,16> want{0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x8b,0x05,0x00,0xf5,0x15,0x03};
            return read(base+0x835520,got.data(),got.size()) && got==want;}();
        const std::uintptr_t fsm=pinned && item?player_fsm(base,read):0;if(!fsm) return false;
        alignas(16) std::array<unsigned char,0x30> params{};
        reinterpret_cast<void (__fastcall*)(void*,std::uintptr_t,std::uintptr_t,int,int)>(base+0x835520)(params.data(),item,ammo,code,0x5b);
        reinterpret_cast<void (__fastcall*)(std::uintptr_t,int,void*)>(base+0x8024d0)(fsm,64,params.data());
        return true;
    }
    template<class Read> static bool native_frame(std::uintptr_t base,Read& read,std::uintptr_t entity,held_copy::Pose& out) {
        const auto flags=rig::value<std::uint8_t>(read,entity+0xd0);if(!flags || (*flags&1)) return false; // hidden: it is not drawn there
        const auto model=rig::value<std::uintptr_t>(read,entity+0xf0);
        std::array<float,12> frame{};
        if(!model || !rig::pointer(*model) || !rig::typed(read,base,*model,0x1b0d628) || !read(*model+0x1860,frame.data(),sizeof(frame))) return false;
        out=held_copy::from_rows(frame.data(),frame.data()+3);return held_copy::valid(out);
    }
    template<class Read> static bool gun_drop(std::uintptr_t base,Read& read,const Request& r) {
        if(!dual_wield_.load(std::memory_order_relaxed)) return false;
        const char* def=r.decl;const bool native=!def;const unsigned side=r.side&1u;
        held_copy::Pose at=request_pose(r);Slot1 s1{};
        if(native) {
            if(!read_slot1(base,read,s1)) return false; // not a gun/crossbow in the hand
            def=mirror_def_names[s1.def];
            const auto& m=mirror_slots_[1];
            if(m.native==s1.entity && m.have_cmd && m.def<copies_.size() && copies_[m.def].shown) at=m.cmd; // mirrored: the copy is what the player sees in the hand
            else {held_copy::Pose drawn{};if(native_frame(base,read,s1.entity,drawn)) at=drawn;}
        } else if(const auto& h=held_[side];h.pe>=0 && h.have_cmd && pool_alive(base,read,h.pe) && !std::strcmp(pool_[h.pe].def,def)) at=h.cmd; // the held copy leaves from where it is
        if(!held_copy::valid(at)) return false;
        std::array<float,3> pos{float(at.position.x),float(at.position.y),float(at.position.z)};std::array<float,9> axis{};
        for(unsigned i=0;i<3;++i) {axis[i*3]=float(at.basis[i].x);axis[i*3+1]=float(at.basis[i].y);axis[i*3+2]=float(at.basis[i].z);}
        int li=-1;for(int i=0;i<2;++i) if(!lying_[i].present) {li=i;break;}
        if(li<0) {li=lying_[0].since<=lying_[1].since?0:1;gun_release(base,read,lying_[li]);} // two lie already: the older goes back
        const bool reuse=!native && held_[side].pe>=0 && pool_alive(base,read,held_[side].pe) && !std::strcmp(pool_[held_[side].pe].def,def); // v1113: the held entity becomes the lying gun (already at the held pose)
        const int pe=reuse?held_[side].pe:pool_acquire(base,read,def,pos,axis);if(pe<0) return false;
        if(native) {
            if(!equip(base,read,0x20,64)) {pool_release(base,read,pe);return false;}
            auto& c=copies_[s1.def]; // a mirrored copy of this gun in the other hand goes now, not at the mirror's next look
            if(mirror_slots_[1].native==s1.entity && c.ent && c.shown && alive_entity(base,read,c.ent,c.id) && visible(base,read,c.ent,false)) c.shown=false;
        } else if(reuse) {held_[side]=HeldSide{};++handovers;}
        // v1041: held_want_ is the caller's alone. The drop used to null it, which erased the copy the caller had just asked for in the same hand
        // (a wheel choice or same-item copy that displaced this one: "selected, not shown until I release and squeeze again"); a drop of the
        // copy the hand released has its want cleared by the caller already.
        auto& g=lying_[li];g=LyingGun{};
        g.present=true;g.native=native;g.pe=pe;g.def=pool_[pe].def;g.kind=static_cast<unsigned>(held_copy::kind_of(def));
        g.since=GetTickCount64();g.stow_ms=g.since;g.checked_ms=g.since;
        if(native) {g.item_decl=s1.item_decl;g.ammo_decl=s1.ammo_decl;g.entity=s1.entity;g.item_spawn=s1.spawn;gun_native_lying_=true;++gun_drops_native;}
        auto& f=g.flight;f.pos=at.position;f.vel={r.vel[0],r.vel[1],r.vel[2]};f.axis={at.basis[0],at.basis[1],at.basis[2]};
        const auto sidev=cross(f.vel,Vec3{0,0,1});const double speed=length(f.vel);
        if(speed>4 && length(sidev)>1e-3) {f.spin_axis=sidev*(1/length(sidev));f.spin=-std::min(speed*1.6,16.);} // tumbles end over end, as the sword
        f.flying=true;QueryPerformanceCounter(&g.qpc);
        ++gun_drops;return true;
    }
    template<class Read> static bool gun_take_native(std::uintptr_t base,Read& read,const LyingGun& g,const Request& r,NativeManualEquipRuntime& manual) {
        Slot1 now{};
        if(read_slot1(base,read,now) && now.entity==g.entity) return true; // already back in the hand (the wheel drew it)
        if(r.manual_generation && service_gun_draw(base,read,r,manual)) return true; // the hip draw's lease: the remembered item is this gun
        const auto fsm=player_fsm(base,read);if(!fsm) return false;
        const auto inventory=rig::value<std::uintptr_t>(read,fsm+0x328);if(!inventory || !*inventory) return false;
        const auto item=find_record(read,*inventory,g.item_decl,g.item_spawn);if(!item) return false;
        const auto ammo=g.ammo_decl?find_record(read,*inventory,g.ammo_decl,0):0;
        if(!equip_item(base,read,item,ammo)) return false;
        ++gun_pickups_explicit;return true;
    }
    template<class Read> static bool gun_pickup(std::uintptr_t base,Read& read,const Request& r,NativeManualEquipRuntime& manual) {
        if(!dual_wield_.load(std::memory_order_relaxed)) return false;
        Vec3 pos[2]{};bool present[2]{};
        // r.decl set = a mod-owned copy's pickup, else the native gun's; either within reach of the hand (review: no cross-picking, no 1e9 reach).
        // as_copy: whichever lies nearest, taken as the hand's mod-owned copy (a native one stays in the inventory, unequipped).
        for(unsigned i=0;i<2;++i) {present[i]=lying_[i].present && (r.as_copy || lying_[i].native==(r.decl==nullptr));pos[i]=lying_[i].flight.pos;}
        const int i=held_copy::nearest(pos,present,2,Vec3{r.pos[0],r.pos[1],r.pos[2]},.6);if(i<0) return false;
        auto& g=lying_[i];
        if(g.native && !r.as_copy && !gun_take_native(base,read,g,r,manual)) return false;
        if((r.as_copy || r.decl) && pool_alive(base,read,g.pe)) { // v1113: the entity stays shown and in_use; service_held hands it to the hand's copy
            auto& a=adopt_[r.side&1u];if(a.pe>=0) pool_release(base,read,a.pe);
            a=Adopt{g.pe,pool_[g.pe].def,GetTickCount64()};g=LyingGun{};
            gun_native_lying_=false;for(const auto& o:lying_) gun_native_lying_=gun_native_lying_ || (o.present && o.native);
        } else gun_release(base,read,g);
        ++gun_pickups;return true;
    }
    // One lying gun per game tick: flight, then the stow / redraw / return rules.
    template<class Read> static void gun_fly(std::uintptr_t base,Read& read,LyingGun& g) {
        static const bool trace_ok=NativeWorldTrace::verify(base,read);
        LARGE_INTEGER qpc{},freq{};QueryPerformanceCounter(&qpc);QueryPerformanceFrequency(&freq);
        const double dt=std::min(static_cast<double>(qpc.QuadPart-g.qpc.QuadPart)/static_cast<double>(freq.QuadPart),.05);
        if(dt<.001) return;
        g.qpc=qpc;
        const auto game=rig::value<std::uintptr_t>(read,base+0x39a5290);
        const auto world=game?rig::value<std::uintptr_t>(read,*game+NativeWorldTrace::world_offset):std::nullopt;
        if(!trace_ok || !world || !*world) return;
        const Vec3 from=g.flight.pos;const double speed=length(g.flight.vel);
        g.flight.step(dt,[&](Vec3 a,Vec3 b) {
            auto hit=NativeWorldTrace::sphere_cast(base,read,reinterpret_cast<NativeWorldTrace::Cast>(base+NativeWorldTrace::cast_rva),
                reinterpret_cast<NativeWorldTrace::Construct>(base+NativeWorldTrace::sphere_ctor_rva),*world,a,b,SwordFlight::radius);
            if(hit && hit->entity && rig::typed(read,base,hit->entity,NativeMelee::npc_vtable_rva)) hit->static_support=false; // a guard is not a floor
            return hit;
        });
        ++gun_flight_frames;
        if(!SwordFlight::finite(g.flight)) {g.flight.flying=false;return;} // never hand the engine a non-finite transform
        if(int fault{};thrown_stun(base,read,from,g.flight.pos,g.flight.axis[0],.15,speed,pool_[g.pe].ent,
               rig::value<std::uint32_t>(read,pool_[g.pe].ent+0xa8).value_or(0),g.struck,fault)) {++gun_hits;g.flight.vel=g.flight.vel*-.2;g.flight.spin*=.3;} // it bounces off the guard
        auto& e=pool_[g.pe];
        std::array<float,3> origin{float(g.flight.pos.x),float(g.flight.pos.y),float(g.flight.pos.z)};
        std::array<float,9> axis{};
        for(unsigned i=0;i<3;++i) {axis[i*3]=float(g.flight.axis[i].x);axis[i*3+1]=float(g.flight.axis[i].y);axis[i*3+2]=float(g.flight.axis[i].z);}
        teleport<Read>(base,e.ent,origin,axis);++gun_teleports;
    }
    template<class Read> static void service_lying(std::uintptr_t base,Read& read,LyingGun& g,std::uint64_t now) {
        if(!pool_alive(base,read,g.pe)) {if(g.pe>=0 && g.pe<static_cast<int>(pool_.size())) pool_[g.pe]={};gun_release(base,read,g);return;} // the engine took it (level change, save): the native gun is still the hip's
        if(g.flight.flying) gun_fly(base,read,g);
        if(now-g.checked_ms<100) return;
        g.checked_ms=now;
        if(g.native) { // the gun in the slot again: within 1.5 s of the drop it is the unequip a running clip cancelled (stow again), later a native redraw (call the copy back)
            Slot1 s1{};
            if(read_slot1(base,read,s1) && s1.entity==g.entity) {
                if(now-g.since<1500) {if(now-g.stow_ms>=300) {g.stow_ms=now;if(equip(base,read,0x20,64)) ++gun_stows;}}
                else {++gun_redraws;gun_release(base,read,g);return;}
            }
        }
        if(now-guns_player_check_>=250) {
            guns_player_check_=now;
            const auto p=player(base,read);const auto at=p?rig::value<std::array<float,3>>(read,p+0x300):std::nullopt;
            if(at) std::copy(at->begin(),at->end(),guns_player_);else guns_player_[0]=guns_player_[1]=guns_player_[2]=1e9f;
        }
        const Vec3 you{guns_player_[0],guns_player_[1],guns_player_[2]};
        if(guns_player_[0]<1e8f && held_copy::should_return(static_cast<double>(now-g.since)/1000.,length(g.flight.pos-you))) {
            last_return_dz=static_cast<float>(g.flight.pos.z-you.z);last_return_age_s=static_cast<float>((now-g.since)/1000.); // v781: below the floor? (a gun fallen through it)
            ++gun_returns;gun_release(base,read,g);}
    }
    // The native weapons' grip offsets, measured live while the native weapon rides its own hand: the copy of the same def in a hand looks like it.
    template<class Read> static void service_measure(std::uintptr_t base,Read& read,std::uint64_t now) {
        for(unsigned slot=0;slot<2;++slot) {
            auto& s=meas_[slot];
            if(now-s.looked<(s.locked?250u:20u)) continue;
            s.looked=now;
            if(!mirror_lookup(base,read,slot,s)) continue;
            if(s.native && (s.cand!=s.native || s.cand_id!=s.native_id)) s.drop();
            if(!s.cand || s.cand_def==HeldSlot::none) {if(s.native) s.drop();continue;}
            if(!s.native) {s.native=s.cand;s.native_id=s.cand_id;s.def=s.cand_def;}
            if(s.locked) continue;
            held_copy::Pose hand{},frame{};held_copy::Motion speed{};
            const unsigned side=(slot==0?1u:0u)^(ride_other_.load(std::memory_order_relaxed)?1u:0u);
            if(side!=s.side) {s.offset.reset();s.side=side;} // the weapon changed hands mid-measure
            if(!feed(side,hand,speed,.1) || speed.linear>.3 || speed.angular>2 || !native_frame(base,read,s.native,frame)) continue;
            if(s.offset.add(held_copy::to_local(hand,frame))) ++held_measured;
            if(s.offset.ready()) {
                const auto kind=s.def<mirror_def_names.size()?held_copy::kind_of(mirror_def_names[s.def]):held_copy::Kind::other;
                // v773 (headset v772: a left pistol copy pointed the wrong way): a hold read mid-clip (holster, reload, a hand change) is no grip.
                // A gun's barrel must lie within 45 degrees of the game's own hold of that kind (grip_constant, measured live), else measure again.
                if(!held_copy::plausible_grip(kind,s.offset.value,s.side)) {s.offset.reset();++held_rejected;continue;}
                s.locked=true;if(s.def<mirror_def_names.size()) kind_offset_[static_cast<unsigned>(kind)&3u]={s.offset.value,s.side,true};}
        }
    }
    // The last measured grip per kind (other, pistol, crossbow, sword): it outlives the native item (v762, headset v761: a pistol copy beside
    // the native crossbow lost the pistol's measurement and sat on the constant, muzzle up), and serves the same kind's other defs (guard weapons).
    struct KindOffset {held_copy::Pose offset{};unsigned side{};bool have{};};
    inline static std::array<KindOffset,4> kind_offset_{};
    // The grip offset of `def` for a copy at `side`: from a native weapon of the same def (measured here, or by the mirror), mirrored across the hand when it
    // was measured on the other one; else the per-kind constant. true = it came from a native twin.
    static bool grip_offset(const char* def,unsigned side,held_copy::Pose& offset,bool& mirrored) {
        for(unsigned slot=0;slot<2;++slot)
            for(const HeldSlot* src:{&meas_[slot],&mirror_slots_[slot]})
                if(src->locked && src->def<mirror_def_names.size() && !std::strcmp(mirror_def_names[src->def],def)) {
                    offset=src->offset.value;mirrored=side!=src->side;return true;
                }
        if(const auto& k=kind_offset_[static_cast<unsigned>(held_copy::kind_of(def))&3u];k.have) {offset=k.offset;mirrored=side!=k.side;return true;}
        const auto c=held_copy::grip_constant(held_copy::kind_of(def));offset=c.offset;mirrored=side!=c.side;return false;
    }
    template<class Read> static void held_hide(std::uintptr_t base,Read& read,unsigned side) {
        auto& h=held_[side];
        if(h.pe>=0 && pool_alive(base,read,h.pe) && pool_[h.pe].shown && visible(base,read,pool_[h.pe].ent,false)) {pool_[h.pe].shown=false;++held_hides;}
        clear_copy_draw(h.pe>=0?pool_[h.pe].ent:0);   // v1101: this hand draws no copy now
        h.shown=false;held_bits&=~(1u<<side);
        std::lock_guard<std::mutex> l(feed_m_);held_pub_[side].valid=false;
    }
    // v776: the player's root as the last two services saw it (a copy is placed before the physics step moves the player; held_copy::root_step).
    inline static std::optional<Vec3> root_seen_{};inline static std::chrono::steady_clock::time_point root_seen_at_{};inline static std::atomic<float> held_step_mm{};
    inline static double yaw_seen_{};inline static std::atomic<double> follow_rad_{};inline static std::atomic<float> held_turn_mdeg{}; // v816: the camera heading root_seen_ came with (player_root)
    inline static double dt_seen_{}; // v779: the GAME clock dt of the step root_seen_ was read in (held_copy::root_step scales the step by this step's over it)
    inline static std::atomic<bool> held_step_on_{true}; // test command "copystep on|off": the sim compares the copies with and without the step
    // This sim step's GAME clock dt: idGameTimeManager at game+0x2e7428, clock 2 (GAME) dt float at +0xc8, written by AdvanceFrame 0x59990 before the stages run.
    template<class Read> static double step_dt(std::uintptr_t base,Read& read) {
        const auto game=rig::value<std::uintptr_t>(read,base+0x39a5290);
        const auto dt=game && *game?rig::value<float>(read,*game+0x2e7428+0xc8):std::nullopt;
        return dt && std::isfinite(*dt)?*dt:0.;
    }
    template<class Read> static void service_held(std::uintptr_t base,Read& read) {
        std::optional<Vec3> now_root;Vec3 step{};double turn{},yaw{};
        if(held_want_[0].load(std::memory_order_relaxed) || held_want_[1].load(std::memory_order_relaxed)) { // no copy wanted: no read, and the history goes stale on its own (100 ms)
            now_root=player_root(base,read,&yaw);const auto at=std::chrono::steady_clock::now();const double dt=step_dt(base,read);
            if(now_root) {if(root_seen_) {const double gap=std::chrono::duration<double>(at-root_seen_at_).count();
                step=held_copy::root_step(*root_seen_,*now_root,gap,dt_seen_,dt);turn=held_copy::yaw_step(yaw_seen_,yaw,gap,dt_seen_,dt);}
                root_seen_=now_root;yaw_seen_=yaw;root_seen_at_=at;dt_seen_=dt;}
            else root_seen_.reset();
            if(!held_step_on_.load(std::memory_order_relaxed)) {step=Vec3{};turn=0;}
            held_step_mm.store(static_cast<float>(length(step)*1000),std::memory_order_relaxed);held_turn_mdeg.store(static_cast<float>(turn*180000/3.14159265358979323846),std::memory_order_relaxed);
        }
        for(unsigned side=0;side<2;++side) {
            auto& h=held_[side];const char* want=held_want_[side].load(std::memory_order_relaxed);
            if(auto& a=adopt_[side];a.pe>=0 && (GetTickCount64()-a.tick>500 || !pool_alive(base,read,a.pe))) {pool_release(base,read,a.pe);a=Adopt{};} // nobody wanted it
            if(h.pe>=0 && !pool_alive(base,read,h.pe)) {if(h.pe<static_cast<int>(pool_.size())) pool_[h.pe]={};h=HeldSide{};held_bits&=~(5u<<side);}
            if(h.failed && h.failed!=want) {h.failed=nullptr;h.tries=0;} // another def: try again
            if(!want) {if(h.pe>=0) {held_hide(base,read,side);pool_release(base,read,h.pe);h=HeldSide{};}continue;}
            if(h.pe>=0 && std::strcmp(pool_[h.pe].def,want)) {const auto failed=h.failed;const auto tries=h.tries;held_hide(base,read,side);pool_release(base,read,h.pe);h=HeldSide{};h.failed=failed;h.tries=tries;}
            held_copy::Pose hand{},offset{},out{};held_copy::Motion speed{};bool mirrored{};std::optional<Vec3> drawn_root;
            const bool measured=grip_offset(want,side,offset,mirrored);
            if(measured) held_bits|=4u<<side;else held_bits&=~(4u<<side);
            if(!feed(side,hand,speed,.25,&drawn_root)) {held_hide(base,read,side);continue;} // no fresh hand: no stand-in
            // v1106: a feed whose recorded root is in another world (a load/teleport between the render publication and this service) still
            // describes the OLD world. It used to be teleported with the stale hand after the load, which is the v1014 16 m gate rejection
            // (the read was 16.3-17.5 m from the image hand while the weapon's own origin gap was 30.8 mm). Drop that frame instead.
            if(drawn_root && now_root && length(*now_root-*drawn_root)>=1.) {++held_rejected;held_hide(base,read,side);continue;}
            if(drawn_root && now_root) {const auto moved=*now_root-*drawn_root;
                if(length(moved)<1.) {hand.position=hand.position+moved;hand=held_copy::carry(hand,*now_root,step,turn);++held_carried;}} // the player's motion since the hand was drawn, and the step and turn the physics is about to add (v776, v816)
            if(!held_copy::place(hand,offset,mirrored,out)) {held_hide(base,read,side);continue;}
            std::array<float,3> pos{float(out.position.x),float(out.position.y),float(out.position.z)};std::array<float,9> axis{};
            for(unsigned i=0;i<3;++i) {axis[i*3]=float(out.basis[i].x);axis[i*3+1]=float(out.basis[i].y);axis[i*3+2]=float(out.basis[i].z);}
            if(h.pe<0 && want) if(auto& a=adopt_[side];a.pe>=0 && !std::strcmp(a.def,want)) {h.pe=a.pe;a=Adopt{};++handovers;} // v1113: take the picked-up gun's entity as is
            if(h.pe<0) {
                if(h.failed==want && (h.tries>=12 || GetTickCount64()<h.retry_at)) continue; // v1041: a transient failure (def not loaded yet, a hidden-state call refused) is retried, not latched
                h.pe=pool_acquire(base,read,want,pos,axis);
                if(h.pe<0) {
                    h.failed=want;++held_failed;++h.tries;h.retry_at=GetTickCount64()+250;
                    std::lock_guard<std::mutex> l(feed_m_);std::snprintf(held_last_failed_.data(),held_last_failed_.size(),"%s",want);
                    continue;
                }
                ++held_spawned;
            } else {
                auto& e=pool_[h.pe];teleport<Read>(base,e.ent,pos,axis);++held_teleports;
                if(!e.shown && visible(base,read,e.ent,true)) e.shown=true;
            }
            h.shown=true;h.cmd=out;h.have_cmd=true;held_bits|=1u<<side;
            note_copy_draw(side,pool_[h.pe].ent,pool_[h.pe].id,hand,mirrored?held_copy::mirror(offset):offset,3);   // v1101/v1106: read + exact applied offset
            std::lock_guard<std::mutex> l(feed_m_);
            held_pub_[side]=HeldPub{true,out,steady_ms(),pool_[h.pe].ent,pool_[h.pe].id};
        }
    }
    // v783: a gun (native or copy) swung into a guard stuns him as a thrown one does (thrown_stun: the hand at 3 m/s or more, the path of the
    // gun's frame since the last service plus its barrel). A struck guard can be struck again 600 ms after the hand's last hit.
    inline static std::array<std::optional<Vec3>,2> bash_last_{};inline static std::array<std::vector<std::uintptr_t>,2> bash_struck_{};
    inline static std::array<std::uint64_t,2> bash_ms_{};
    template<class Read> static void service_bash(std::uintptr_t base,Read& read,std::uint64_t now) {
        for(unsigned side=0;side<2;++side) {
            held_copy::Pose gun{};std::uintptr_t ent{};bool have=false;
            if(const auto& h=held_[side];h.shown && h.have_cmd && h.pe>=0 && pool_[h.pe].def && held_copy::kind_of(pool_[h.pe].def)!=held_copy::Kind::sword) {gun=h.cmd;ent=pool_[h.pe].ent;have=true;}
            for(unsigned slot=1;slot<2 && !have;++slot) { // the native slot-1 gun, in the hand it rides (service_measure's side)
                const auto& m=meas_[slot];
                if(m.native && m.def<mirror_def_names.size() && held_copy::kind_of(mirror_def_names[m.def])!=held_copy::Kind::sword &&
                   (0u^(ride_other_.load(std::memory_order_relaxed)?1u:0u))==side && native_frame(base,read,m.native,gun)) {ent=m.native;have=true;}
            }
            held_copy::Pose hand{};held_copy::Motion speed{};
            if(!have || !feed(side,hand,speed,.1)) {bash_last_[side].reset();continue;}
            const Vec3 from=bash_last_[side].value_or(gun.position);bash_last_[side]=gun.position;
            if(now-bash_ms_[side]>600) bash_struck_[side].clear();
            if(int fault{};thrown_stun(base,read,from,gun.position,gun.basis[0],.12,speed.linear,ent,rig::value<std::uint32_t>(read,ent+0xa8).value_or(0),bash_struck_[side],fault)) {
                ++bash_hits;bash_ms_[side]=now;}
        }
    }
    template<class Read> static void guns_teardown(std::uintptr_t base,Read& read) {
        for(auto& u:ups_) upgrades_drop(base,read,u);
        for(auto& b:bolts_) bolt_drop(base,read,b);
        for(auto& an:anchors_) {if(alive_entity(base,read,an.anchor,an.anchor_id)) remove_entity(base,an.anchor);an={};}
        for(auto& e:pool_) {if(e.ent && alive_entity(base,read,e.ent,e.id)) remove_entity(base,e.ent);e={};}
        guns_forget();
    }
    // v1091 (user, headset v1090: "if the player has upgrades, the copied weapon should also have them"). The copies are the *_midnight.def
    // models, which carry no upgrades. The player's weapon keeps them in arkCpntEquipment::m_upgrades (+0x50, idList<upgrade_t>, 0x28 each:
    // m_item, m_originalModel, m_staticAttachment, m_animatedAttachment, m_anim; the exe's generated type records). The game builds an
    // upgrade_t with the equipment vtable's slot 0xf0 (0x61a2b0, shared by the gun, crossbow and every other equipment class), which reads
    // only this+0x10 (the owner entity): it spawns the upgrade decl's static model / attachment def at the owner and binds it to the decl's
    // joint tag (0x875e80), and applies the decl's model and material overrides. The same call with the copy as owner gives the copy the same
    // parts. A hidden or removed copy loses its parts (removed); a shown one gets them again. ponytail: a model override is not undone when an
    // upgrade leaves the list (the game only adds upgrades); the copy entity would need a respawn.
    // v1111 (sim: the player's crossbow carries 9 upgrades, the copy got 8): the lists were capped at 8 items.
    static constexpr unsigned max_upgrades=16;
    struct CopyUpgrades {std::uintptr_t ent{};std::uint32_t id{};unsigned n{};std::array<std::uintptr_t,max_upgrades> item{};
        std::array<std::array<std::uintptr_t,2>,max_upgrades> att{};std::array<std::array<std::uint32_t,2>,max_upgrades> att_id{};
        std::array<std::array<std::uintptr_t,5>,max_upgrades> rec{};   // v1094: the copy's own upgrade_t array (m_item = the real item), what 0x61a650 walks
        std::uint64_t shot_seen{},reload_seen{};int side{-1};};
    inline static std::array<CopyUpgrades,10> ups_{};inline static std::uint64_t ups_looked_{};
    inline static std::atomic<std::uint64_t> upgrade_parts{},upgrade_faults{},upgrade_skips{},upgrade_rebinds{};
    template<class Read> static void upgrades_drop(std::uintptr_t base,Read& read,CopyUpgrades& u) {
        for(unsigned i=0;i<u.n;++i) for(unsigned k=0;k<2;++k) if(alive_entity(base,read,u.att[i][k],u.att_id[i][k])) remove_entity(base,u.att[i][k]);
        u={};
    }
    // The player's own weapon component of a kind (the inventory record's component, as test_equip finds it): 0 none.
    template<class Read> static std::uintptr_t native_equipment(std::uintptr_t base,Read& read,held_copy::Kind kind) {
        const auto fsm=player_fsm(base,read);const auto inv=fsm?rig::value<std::uintptr_t>(read,fsm+0x328):std::nullopt;
        const auto entries=inv && *inv?rig::list(read,*inv+0x38,4096):std::nullopt;if(!entries) return 0;
        for(unsigned i=0;i<entries->packed_count;++i) {
            const auto comp=rig::value<std::uintptr_t>(read,entries->data+i*0x38ull+0x10);
            if(comp && *comp && rig::typed(read,base,*comp,kind==held_copy::Kind::crossbow?0x1b9cbf8:0x1b53f58)) return *comp;
        }
        return 0;
    }
    // v1093 (user, headset v1092: "simple rule: the player always sees weapons with the latest upgrades", also a dropped weapon). A copy with
    // no animation component (the crossbow copies) cannot take a joint-tagged part (v1092 crash: the bind update reads the owner's animation
    // component at bind+0x400 when the joint index is set). Such a part is attached from a copy of its decl with an empty tag (bound at the
    // owner's origin, joint index -1, never read) and then bound again where it sits on the player's own weapon: the bind 0x875e80(owner,
    // child, tag*, offset*, axis*, 1) with no tag places the child at offset/axis in the owner's frame (the game passes vec3_origin
    // 0x31af668 and mat3_identity 0x228a780 there). That place is measured live on the player's weapon: the part's entity frame (+0x300 /
    // +0x30c) in the weapon's model frame (model+0x1860, the frame a copy is placed with), cached per upgrade decl and part.
    // The player's weapon may clear m_upgrades while it is put away or dropped: the last non-empty list of each kind is kept and used then.
    struct PartPlace {std::uintptr_t decl{};unsigned part{};held_copy::Pose local{};};
    inline static std::array<PartPlace,32> places_{};inline static std::array<std::array<std::uintptr_t,max_upgrades>,4> last_items_{};inline static std::array<unsigned,4> last_n_{};
    static const PartPlace* place_of(std::uintptr_t decl,unsigned part) {
        for(const auto& p:places_) if(p.decl==decl && p.part==part) return &p;
        return nullptr;
    }
    template<class Read> static std::optional<held_copy::Pose> entity_frame(Read& read,std::uintptr_t e) {
        std::array<float,12> f{};if(!read(e+0x300,f.data(),sizeof(f))) return std::nullopt;
        const auto pose=held_copy::from_rows(f.data(),f.data()+3);return held_copy::valid(pose)?std::optional<held_copy::Pose>{pose}:std::nullopt;
    }
    // The player's weapon of `comp`: its upgrade items (and, while its parts live, where each part sits on it).
    template<class Read> static unsigned native_upgrades(std::uintptr_t base,Read& read,std::uintptr_t comp,held_copy::Kind kind,std::array<std::uintptr_t,max_upgrades>& items) {
        const auto list=rig::list(read,comp+0x50,64);unsigned n=0;
        const auto weapon=rig::value<std::uintptr_t>(read,comp+0x10);
        const auto model=weapon && *weapon?rig::value<std::uintptr_t>(read,*weapon+0xf0):std::nullopt;
        std::optional<held_copy::Pose> frame;std::array<float,12> mf{};
        if(model && rig::pointer(*model) && rig::typed(read,base,*model,0x1b0d628) && read(*model+0x1860,mf.data(),sizeof(mf))) {
            const auto f=held_copy::from_rows(mf.data(),mf.data()+3);if(held_copy::valid(f)) frame=f;}
        for(unsigned i=0;list && i<list->packed_count && n<items.size();++i) {
            const auto at=list->data+i*0x28ull;const auto item=rig::value<std::uintptr_t>(read,at);
            if(!item || !rig::pointer(*item)) continue;
            items[n++]=*item;
            const auto decl=rig::value<std::uintptr_t>(read,*item);if(!frame || !decl || !*decl) continue;
            for(unsigned k=0;k<2;++k) {
                const auto part=rig::value<std::uintptr_t>(read,at+0x10+8ull*k);if(!part || !*part) continue;
                const auto pid=rig::value<std::uint32_t>(read,*part+0xa8);if(!pid || !alive_entity(base,read,*part,*pid)) continue;
                const auto pf=entity_frame(read,*part);if(!pf) continue;
                PartPlace* slot=nullptr;for(auto& p:places_) if(p.decl==*decl && p.part==k) slot=&p;
                if(!slot) for(auto& p:places_) if(!p.decl) {slot=&p;break;}
                if(slot) *slot={*decl,k,held_copy::to_local(*frame,*pf)};
            }
        }
        auto& last=last_items_[static_cast<unsigned>(kind)&3u];auto& ln=last_n_[static_cast<unsigned>(kind)&3u];
        if(n) {last=items;ln=n;return n;}
        items=last;return ln;
    }
    template<class Read> static void upgrades_sync(std::uintptr_t base,Read& read,std::uintptr_t ent,std::uint32_t id,const std::array<std::uintptr_t,max_upgrades>& want,unsigned wn) {
        CopyUpgrades* u=nullptr;
        for(auto& c:ups_) if(c.ent==ent && c.id==id) u=&c;
        if(!u) for(auto& c:ups_) if(!c.ent) {u=&c;break;}
        if(!u) return;
        u->ent=ent;u->id=id;
        for(unsigned i=0;i<u->n;) { // an upgrade that left the list (a parent replaced by its child): its parts go
            if(std::find(want.begin(),want.begin()+wn,u->item[i])!=want.begin()+wn) {++i;continue;}
            for(unsigned k=0;k<2;++k) if(alive_entity(base,read,u->att[i][k],u->att_id[i][k])) remove_entity(base,u->att[i][k]);
            --u->n;u->item[i]=u->item[u->n];u->att[i]=u->att[u->n];u->att_id[i]=u->att_id[u->n];u->rec[i]=u->rec[u->n];
        }
        const bool animated=reinterpret_cast<std::uintptr_t (__fastcall*)(std::uintptr_t,std::uintptr_t,int)>(base+0x877720)(ent,base+0x39dc9d0,0)!=0;
        static const char empty_tag[1]{};
        for(unsigned w=0;w<wn && u->n<u->item.size();++w) {
            if(std::find(u->item.begin(),u->item.begin()+u->n,want[w])!=u->item.begin()+u->n) continue;
            alignas(16) std::array<std::uintptr_t,4> self{0,0,ent,0};alignas(16) std::array<std::uintptr_t,5> out{};
            const auto decl=rig::value<std::uintptr_t>(read,want[w]);const auto tag=decl && *decl?rig::value<std::uintptr_t>(read,*decl+0x240):std::nullopt;
            const auto first=tag && *tag?rig::value<char>(read,*tag):std::optional<char>{};
            if(!decl || !*decl || !tag || (*tag && !first)) continue;   // unreadable: try again later
            const bool tagged=first && *first;
            if(!tagged || animated) reinterpret_cast<void (__fastcall*)(void*,void*,std::uintptr_t)>(base+0x61a2b0)(self.data(),out.data(),want[w]);
            else {
                const auto has=[&](std::uintptr_t at){return rig::value<std::uintptr_t>(read,*decl+at).value_or(0)!=0;};
                const bool need[2]{has(0x230),has(0x238)};
                if((need[0] && !place_of(*decl,0)) || (need[1] && !place_of(*decl,1))) {++upgrade_skips;continue;}   // not measured yet: next pass
                alignas(16) std::array<unsigned char,0x280> fake{};if(!read(*decl,fake.data(),fake.size())) continue;
                const auto empty=reinterpret_cast<std::uintptr_t>(empty_tag);std::memcpy(fake.data()+0x240,&empty,8);
                const std::uintptr_t fake_decl=reinterpret_cast<std::uintptr_t>(fake.data());const std::uintptr_t fake_item=reinterpret_cast<std::uintptr_t>(&fake_decl);
                reinterpret_cast<void (__fastcall*)(void*,void*,std::uintptr_t)>(base+0x61a2b0)(self.data(),out.data(),fake_item);
                for(unsigned k=0;k<2;++k) if(out[2+k]) if(const auto pl=place_of(*decl,k)) {
                    alignas(16) std::array<float,3> o{float(pl->local.position.x),float(pl->local.position.y),float(pl->local.position.z)};alignas(16) std::array<float,9> m{};
                    for(unsigned r=0;r<3;++r) {m[r*3]=float(pl->local.basis[r].x);m[r*3+1]=float(pl->local.basis[r].y);m[r*3+2]=float(pl->local.basis[r].z);}
                    const std::uintptr_t tag_field=empty;
                    reinterpret_cast<void (__fastcall*)(std::uintptr_t,std::uintptr_t,const std::uintptr_t*,const float*,const float*,bool)>(base+0x875e80)(ent,out[2+k],&tag_field,o.data(),m.data(),true);
                    ++upgrade_rebinds;
                }
            }
            const unsigned i=u->n++;u->item[i]=want[w];u->rec[i]={want[w],out[1],out[2],out[3],out[4]};
            for(unsigned k=0;k<2;++k) {u->att[i][k]=out[2+k];u->att_id[i][k]=out[2+k]?rig::value<std::uint32_t>(read,out[2+k]+0xa8).value_or(0):0u;if(out[2+k]) ++upgrade_parts;}
        }
    }
    // v1094: the copy's upgrade parts animate as the player's weapon's do. The weapon plays them from its own fire / reload clips (anim events
    // 0xcc / 0xcd -> 0x61a650(comp, 3 ANIMATE_ON_FIRE / 4 ANIMATE_ON_RELOAD); per frame 0x61a650(comp, 1 LOOPING)); 0x61a650 reads only
    // comp+0x50 / +0x58 (the upgrade_t list) and each part's m_anim, so the copy's own list in a stand-in buffer plays its parts.
    static bool play_upgrades(std::uintptr_t base,void* stand_in,int type) {
        __try {reinterpret_cast<void (__fastcall*)(void*,int)>(base+0x61a650)(stand_in,type);return true;} __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    static void copy_anim(std::uintptr_t base,CopyUpgrades& u,int type) {
        if(!u.n) return;
        alignas(16) std::array<unsigned char,0x60> stand_in{};const auto list=reinterpret_cast<std::uintptr_t>(u.rec.data());const std::uint32_t n=u.n;
        std::memcpy(stand_in.data()+0x50,&list,8);std::memcpy(stand_in.data()+0x58,&n,4);
        if(play_upgrades(base,stand_in.data(),type)) ++upgrade_anims;else ++upgrade_faults;
    }
    // v1094: the crossbow copy shows its hand's loaded bolt. The weapon's own (vslot 0x100, 0x62b240) is the ammo entry's +8 def spawned at the
    // weapon and bound to the decl's bolt tag, kept at comp+0x160; the copy has no animation component, so the copy's bolt is bound with no tag
    // at the place the player's bolt has on the player's crossbow (measured live, kept).
    struct CopyBolt {std::uintptr_t copy{},bolt{},def{};std::uint32_t copy_id{},bolt_id{};};
    struct RealSeen {std::uintptr_t comp{};std::uint64_t shot{},reload{};};inline static std::array<RealSeen,2> real_seen_{};
    static void stand_in_type(std::uintptr_t base,std::uintptr_t comp,int type) {if(type>=0 && !play_upgrades(base,reinterpret_cast<void*>(comp),type)) ++upgrade_faults;}
    inline static std::array<CopyBolt,2> bolts_{};inline static std::optional<held_copy::Pose> bolt_place_{};
    inline static std::atomic<std::uint64_t> upgrade_anims{},copy_bolts{};
    template<class Read> static std::optional<held_copy::Pose> weapon_frame(std::uintptr_t base,Read& read,std::uintptr_t weapon) {
        const auto model=weapon?rig::value<std::uintptr_t>(read,weapon+0xf0):std::nullopt;std::array<float,12> mf{};
        if(!model || !rig::pointer(*model) || !rig::typed(read,base,*model,0x1b0d628) || !read(*model+0x1860,mf.data(),sizeof(mf))) return std::nullopt;
        const auto f=held_copy::from_rows(mf.data(),mf.data()+3);return held_copy::valid(f)?std::optional<held_copy::Pose>{f}:std::nullopt;
    }
    template<class Read> static void bolt_drop(std::uintptr_t base,Read& read,CopyBolt& b) {
        if(alive_entity(base,read,b.bolt,b.bolt_id)) remove_entity(base,b.bolt);
        b={};
    }
    template<class Read> static void service_bolt(std::uintptr_t base,Read& read,unsigned side,std::uintptr_t copy,std::uint32_t copy_id,std::uintptr_t comp) {
        auto& b=bolts_[side];
        if(comp) { // where the player's bolt sits on the player's crossbow
            const auto native_bolt=rig::value<std::uintptr_t>(read,comp+0x160);const auto weapon=rig::value<std::uintptr_t>(read,comp+0x10);
            if(native_bolt && *native_bolt && weapon && *weapon) {
                const auto id=rig::value<std::uint32_t>(read,*native_bolt+0xa8);
                if(id && alive_entity(base,read,*native_bolt,*id)) if(const auto frame=weapon_frame(base,read,*weapon)) if(const auto bf=entity_frame(read,*native_bolt)) bolt_place_=held_copy::to_local(*frame,*bf);
            }
        }
        std::uintptr_t def=0;
        if(copy && comp && NativeRangedFire::loaded(side)) {
            auto entry=NativeRangedFire::copy_ammo(side,1);if(!entry) entry=rig::value<std::uintptr_t>(read,comp+0x98).value_or(0);
            if(entry) def=rig::value<std::uintptr_t>(read,entry+8).value_or(0);
        }
        if(b.bolt && (b.copy!=copy || b.copy_id!=copy_id || b.def!=def || !alive_entity(base,read,b.bolt,b.bolt_id))) bolt_drop(base,read,b);
        if(b.bolt || !def || !bolt_place_ || !rig::pointer(def)) return;
        const auto game=rig::value<std::uintptr_t>(read,base+0x39a5290);const auto at=entity_frame(read,copy);if(!game || !at) return;
        alignas(16) std::array<float,3> pos{float(at->position.x),float(at->position.y),float(at->position.z)};alignas(16) std::array<float,9> axis{};
        for(unsigned r=0;r<3;++r) {axis[r*3]=float(at->basis[r].x);axis[r*3+1]=float(at->basis[r].y);axis[r*3+2]=float(at->basis[r].z);}
        static const std::array<float,3> zero{};
        const auto bolt=reinterpret_cast<std::uintptr_t (__fastcall*)(std::uintptr_t,std::uintptr_t,const float*,const float*,std::uintptr_t,bool,const float*,const float*,const float*)>(
            base+0x8ae9b0)(*game,def,pos.data(),axis.data(),0,false,nullptr,zero.data(),zero.data());
        if(!bolt) return;
        *reinterpret_cast<volatile std::uint8_t*>(bolt+0xd4)|=0x10;   // as 0x62b240 marks its bolt
        static const char empty_tag[1]{};const std::uintptr_t tag=reinterpret_cast<std::uintptr_t>(empty_tag);
        const auto& pl=*bolt_place_;
        alignas(16) std::array<float,3> o{float(pl.position.x),float(pl.position.y),float(pl.position.z)};alignas(16) std::array<float,9> m{};
        for(unsigned r=0;r<3;++r) {m[r*3]=float(pl.basis[r].x);m[r*3+1]=float(pl.basis[r].y);m[r*3+2]=float(pl.basis[r].z);}
        reinterpret_cast<void (__fastcall*)(std::uintptr_t,std::uintptr_t,const std::uintptr_t*,const float*,const float*,bool)>(base+0x875e80)(copy,bolt,&tag,o.data(),m.data(),true);
        b={copy,bolt,def,copy_id,rig::value<std::uint32_t>(read,bolt+0xa8).value_or(0)};++copy_bolts;
    }
    // v1114 (user: the copy plays the original's weapon BODY animation on fire and reload, upgrades included). The weapon component has no body
    // clips: the player's fire / reload states play the weapon's midnight scene through 0x854be0(state, &handle, scene, flags, 0) with the
    // equipped weapon bound as actor PlayerLeftEquipment (0x8542b0 flag 8: entity id [[[state+0x10]+0x328]+0x70]+8, needs a model at +0xf0).
    // The copy gets the same call: a fake state per side whose fake player's slot-1 record names the copy's entity. The driver calls the state's
    // vslot 0x88 between create and play (the game's 0x845ad0): here scene_conditions, the copy component's 0x63a7c0 (its upgrades' conditions,
    // Burst / BurstLoop); every other fake slot is the game's own 3-byte no-op 0x998b0. Scene and rate come from the copy's own getters
    // (vslot 0x120 fire, 0x128 reload with dl=1), the rate is applied by 0x7d8fe0, a fire is followed by the copy's vslot 0x108 (bolt / bullets).
    // build/no-copy-scenes turns it off (read once). Rows scene / scene_fail / scene_after / pin in artifacts/real-parts.csv:
    // tick,what,side,kind,handle,rate (fail: why 1 getter 2 no scene 3 driver fault 4 no handle; after: 1 still running),rate_ok,scene,decl+0x11c,
    // native mag,native bolt +0xd0,native hidden parts (hex, bit 2i+k),copy mag,copy bolt,copy hidden,cond source (1 state+0x24, 2 out, 3 after play,
    // -1 no handle when asked),cond rdx (low 32 bits, decimal like the handle). Marks are read before the scene starts; scene_after 1 s later.
    struct CopyScene {std::uintptr_t comp{},game{};std::uint32_t handle{};char kind{};bool cond{};std::uint64_t at{};};
    struct SceneAfter {std::uint64_t due{};std::uint32_t handle{};char kind{};held_copy::Kind wk{};};
    struct SceneFake {   // fake fire state, its player, the player's inventory and the slot-1 record (sizes generous, all zero but the links)
        std::array<std::uintptr_t,0x80> state{},player{};std::array<std::uintptr_t,0x20> equip{};std::array<std::uintptr_t,4> rec{};};
    struct Marks {int mag{-1},bolt{-1};unsigned hidden{};};
    struct SceneMarks {Marks native,copy;int d11c{-1};};
    inline static std::array<CopyScene,2> scenes_{};inline static std::array<SceneAfter,2> scene_after_{};
    inline static std::array<SceneFake,2> scene_fakes_{};inline static std::array<std::uintptr_t,64> scene_vt_{};inline static std::uintptr_t scene_base_{};
    inline static std::array<int,2> scene_cond_src_{};inline static std::array<std::uint32_t,2> scene_cond_arg_{};inline static unsigned scene_rows_{};
    inline static std::atomic<int> scene_knob_{-1},scene_pin_{-1};   // -1 not read yet; knob 1 = build/no-copy-scenes; pin 1 = verified
    static constexpr std::array<std::uintptr_t,8> scene_pin_rvas{0x854be0,0x7d6040,0x7d9270,0x7d8fe0,0x7d63f0,0x7d9590,0x63a7c0,0x998b0};
    inline static std::array<std::array<unsigned char,16>,8> scene_pin_bytes_{};
    inline static std::atomic<std::uint64_t> scene_fire{},scene_reload{},scene_fail{},scene_running{},scene_faults{},scene_conds{},scene_late{},scene_stops{};
    static bool scenes_on() {   // build/no-copy-scenes, read once (the knob folder is the artifacts folder's sibling, as dxgi_probe's root/build)
        if(scene_knob_.load()<0) {if(report_dir_.empty()) return false;std::error_code ec;scene_knob_=std::filesystem::exists(report_dir_.parent_path()/"build"/"no-copy-scenes",ec)?1:0;}
        return scene_knob_.load()==0;
    }
    static bool scene_of(std::uintptr_t fn,std::uintptr_t comp,bool reload,float* rate,std::uintptr_t* scene) {
        __try {*scene=reload?reinterpret_cast<std::uintptr_t (__fastcall*)(std::uintptr_t,bool,float*)>(fn)(comp,true,rate)
                            :reinterpret_cast<std::uintptr_t (__fastcall*)(std::uintptr_t,float*)>(fn)(comp,rate);return true;}
        __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    static bool scene_drive(std::uintptr_t base,std::uintptr_t state,std::uint32_t* out,std::uintptr_t scene) {
        __try {reinterpret_cast<std::uint32_t* (__fastcall*)(std::uintptr_t,std::uint32_t*,std::uintptr_t,std::uint32_t,float)>(base+0x854be0)(state,out,scene,8u,0.f);return true;}
        __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    static bool comp_call(std::uintptr_t fn,std::uintptr_t comp,std::uint32_t handle) {   // vslot 0x108 (handle unused) / 0x63a7c0 conditions
        __try {reinterpret_cast<void (__fastcall*)(std::uintptr_t,std::uint32_t)>(fn)(comp,handle);return true;}
        __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    static int scene_rate(std::uintptr_t base,std::uintptr_t mgr,std::uint32_t h,float rate) {   // 1 set, 0 refused (no such scene / ended), -1 fault
        __try {return reinterpret_cast<bool (__fastcall*)(std::uintptr_t,std::uint32_t,float)>(base+0x7d8fe0)(mgr,h,rate)?1:0;}
        __except(EXCEPTION_EXECUTE_HANDLER) {return -1;}
    }
    static int scene_finished(std::uintptr_t base,std::uintptr_t mgr,std::uint32_t h) {
        __try {return reinterpret_cast<bool (__fastcall*)(std::uintptr_t,std::uint32_t)>(base+0x7d63f0)(mgr,h)?1:0;}
        __except(EXCEPTION_EXECUTE_HANDLER) {return -1;}
    }
    static bool scene_halt(std::uintptr_t base,std::uintptr_t mgr,std::uint32_t h) {   // 0x7d9590 stop (inferred (mgr, handle); r8/r9 zero)
        __try {reinterpret_cast<void (__fastcall*)(std::uintptr_t,std::uint32_t,std::uintptr_t,std::uintptr_t)>(base+0x7d9590)(mgr,h,0,0);return true;}
        __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    // The fake state's vslot 0x88, called by the driver between create and play: the copy's own conditions on the new scene.
    static void __fastcall scene_conditions(std::uintptr_t state,std::uintptr_t arg) {
        for(unsigned side=0;side<2;++side) {
            auto& f=scene_fakes_[side];if(state!=reinterpret_cast<std::uintptr_t>(f.state.data())) continue;
            auto& s=scenes_[side];std::uint32_t h{};std::memcpy(&h,reinterpret_cast<const unsigned char*>(f.state.data())+0x24,4);
            scene_cond_arg_[side]=static_cast<std::uint32_t>(arg);scene_cond_src_[side]=h?1:s.handle?2:-1;if(!h) h=s.handle;
            if(!h || !s.comp) return;
            if(comp_call(scene_base_+0x63a7c0,s.comp,h)) {s.cond=true;++scene_conds;} else ++scene_faults;
            return;
        }
    }
    // One-time pin: plausible prologues (REX / push) for the driver, the manager's create / play / finished / stop and the conditions, the rate
    // setter's exact bytes (NativeSlashSceneRate::setter_code), the no-op's 3 bytes, and the getters / refresh / conditions in both weapon vtables.
    template<class Read> static bool scene_pinned(std::uintptr_t base,Read& read) {
        if(const int p=scene_pin_.load();p>=0) return p==1;
        static constexpr std::array<unsigned char,16> rate_code{0x40,0x53,0x48,0x83,0xec,0x50,0x48,0xc7,0x44,0x24,0x20,0xfe,0xff,0xff,0xff,0x0f};
        struct Slot {std::uintptr_t vt,slot,fn;};
        static constexpr std::array<Slot,8> slots{{{0x1b53f58,0x120,0x44f1e0},{0x1b53f58,0x128,0x63bd90},{0x1b53f58,0x108,0x44e180},{0x1b53f58,0xe0,0x63a7c0},
            {0x1b9cbf8,0x120,0x63bc00},{0x1b9cbf8,0x128,0x63bd90},{0x1b9cbf8,0x108,0x62b990},{0x1b9cbf8,0xe0,0x63a7c0}}};
        bool good=true;
        for(unsigned i=0;i<scene_pin_rvas.size();++i) {
            auto& b=scene_pin_bytes_[i];if(!read(base+scene_pin_rvas[i],b.data(),b.size())) {good=false;continue;}
            if(i+1==scene_pin_rvas.size()) good=good && ((b[0]==0xc2 && b[1]==0 && b[2]==0) || ((b[0]==0x32 || b[0]==0x33) && b[1]==0xc0 && b[2]==0xc3));   // ret 0 / xor; ret
            else good=good && (b[0]==0x40 || b[0]==0x48 || b[0]==0x4c || b[0]==0x53 || b[0]==0x55 || b[0]==0x56 || b[0]==0x57);
        }
        good=good && scene_pin_bytes_[3]==rate_code;
        for(const auto& s:slots) good=good && rig::value<std::uintptr_t>(read,base+s.vt+s.slot)==base+s.fn;
        if(good) {
            scene_base_=base;for(auto& v:scene_vt_) v=base+0x998b0;scene_vt_[0x88/8]=reinterpret_cast<std::uintptr_t>(&scene_conditions);
            for(auto& f:scene_fakes_) {f.state[0]=reinterpret_cast<std::uintptr_t>(scene_vt_.data());f.state[0x10/8]=reinterpret_cast<std::uintptr_t>(f.player.data());
                f.player[0x328/8]=reinterpret_cast<std::uintptr_t>(f.equip.data());f.equip[0x70/8]=reinterpret_cast<std::uintptr_t>(f.rec.data());}
        }
        scene_pin_=good?1:0;
        if(!report_dir_.empty()) if(std::ofstream o(report_dir_/"real-parts.csv",std::ios::app);o) {
            o<<GetTickCount64()<<",pin,"<<(good?1:0);
            for(unsigned i=0;i<scene_pin_rvas.size();++i) {char h[40];o<<','<<std::hex<<scene_pin_rvas[i]<<std::dec<<':';for(const auto c:scene_pin_bytes_[i]) {std::snprintf(h,sizeof(h),"%02x",static_cast<unsigned>(c));o<<h;}}
            for(const auto& s:slots) o<<','<<std::hex<<s.vt<<'+'<<s.slot<<'='<<(rig::value<std::uintptr_t>(read,base+s.vt+s.slot).value_or(0)-base)<<std::dec;
            o<<'\n';
        }
        return good;
    }
    template<class Read> static Marks weapon_marks(std::uintptr_t base,Read& read,std::uintptr_t comp) {
        Marks m{};if(!comp || !rig::pointer(comp)) return m;
        m.mag=rig::value<std::int32_t>(read,comp+0xa0).value_or(-1);
        if(rig::typed(read,base,comp,0x1b9cbf8)) if(const auto b=rig::value<std::uintptr_t>(read,comp+0x160);b && rig::pointer(*b)) m.bolt=rig::value<std::uint8_t>(read,*b+0xd0).value_or(0);
        if(const auto list=rig::list(read,comp+0x50,64)) for(unsigned i=0;i<list->packed_count && i<16;++i) for(unsigned k=0;k<2;++k)
            if(const auto p=rig::value<std::uintptr_t>(read,list->data+i*0x28ull+0x10+8ull*k);p && rig::pointer(*p) && (rig::value<std::uint8_t>(read,*p+0xd0).value_or(0)&1)) m.hidden|=1u<<(i*2+k);
        return m;
    }
    template<class Read> static SceneMarks scene_marks(std::uintptr_t base,Read& read,std::uintptr_t own,held_copy::Kind kind) {
        SceneMarks m{weapon_marks(base,read,native_equipment(base,read,kind)),weapon_marks(base,read,own)};
        const auto decl=own?rig::value<std::uintptr_t>(read,own+8).value_or(0):0;
        if(decl && rig::pointer(decl)) m.d11c=rig::value<std::int32_t>(read,decl+0x11c).value_or(-1);
        return m;
    }
    static void scene_row(const char* what,unsigned side,char kind,std::uint32_t h,double x,int rate_ok,std::uintptr_t scene,const SceneMarks& m) {
        if(report_dir_.empty() || scene_rows_>=400) return;   // diagnostics: the first 400 rows of a session
        ++scene_rows_;std::ofstream o(report_dir_/"real-parts.csv",std::ios::app);if(!o) return;
        o<<GetTickCount64()<<','<<what<<','<<side<<','<<(kind=='R'?"reload":"fire")<<','<<h<<','<<x<<','<<rate_ok<<','<<std::hex<<scene<<std::dec<<','<<m.d11c<<','
         <<m.native.mag<<','<<m.native.bolt<<','<<std::hex<<m.native.hidden<<std::dec<<','<<m.copy.mag<<','<<m.copy.bolt<<','<<std::hex<<m.copy.hidden<<std::dec
         <<','<<scene_cond_src_[side&1u]<<','<<scene_cond_arg_[side&1u]<<'\n';
    }
    static void scene_stop(std::uintptr_t base,unsigned side,std::uintptr_t game) {   // calls the manager only in the game that made the handle, and only if still running
        auto& s=scenes_[side];
        if(s.handle && game && s.game==game) {
            const int done=scene_finished(base,game+0x1f5338,s.handle);
            if(done<0) ++scene_faults;
            else if(!done) {if(scene_halt(base,game+0x1f5338,s.handle)) ++scene_stops;else ++scene_faults;}
        }
        s={};
    }
    template<class Read> static void scene_play(std::uintptr_t base,Read& read,unsigned side,std::uintptr_t own,held_copy::Kind kind,bool reload,std::uintptr_t game) {
        auto& s=scenes_[side];const char k=reload?'R':'F';const bool crossbow=kind==held_copy::Kind::crossbow;
        const auto ent=rig::value<std::uintptr_t>(read,own+0x10).value_or(0);const auto id=ent && rig::pointer(ent)?rig::value<std::uint32_t>(read,ent+0xa8).value_or(0):0u;
        const auto before=scene_marks(base,read,own,kind);
        float rate=1.f;std::uintptr_t scene=0;
        const bool got=id && scene_of(base+(reload?0x63bd90:crossbow?0x63bc00:0x44f1e0),own,reload,&rate,&scene);
        if(!got || !scene) {++scene_fail;if(id && !got) ++scene_faults;scene_row("scene_fail",side,k,0,got?2:1,0,scene,before);return;}
        if(s.handle) scene_stop(base,side,game);   // a new shot or the reload replaces the running scene (the game's state exit ends its own)
        auto& f=scene_fakes_[side];f.rec[1]=id;std::memset(reinterpret_cast<unsigned char*>(f.state.data())+0x24,0,4);
        scene_cond_src_[side]=0;scene_cond_arg_[side]=0;
        s={own,game,0,k,false,GetTickCount64()};
        const bool ran=scene_drive(base,reinterpret_cast<std::uintptr_t>(f.state.data()),&s.handle,scene);
        if(!ran || !s.handle) {++scene_fail;if(!ran) ++scene_faults;const auto h=s.handle;if(!ran) scene_stop(base,side,game);s={};scene_row("scene_fail",side,k,h,ran?4:3,0,scene,before);return;}
        if(!s.cond) {if(comp_call(base+0x63a7c0,own,s.handle)) {s.cond=true;++scene_late;scene_cond_src_[side]=3;} else ++scene_faults;}   // the driver never asked: set them after play
        const int rated=std::isfinite(rate) && rate>0.f?scene_rate(base,game+0x1f5338,s.handle,rate):-2;if(rated<0 && rated!=-2) ++scene_faults;
        if(!reload && !comp_call(base+(crossbow?0x62b990:0x44e180),own,0)) ++scene_faults;   // after a fire: the bolt / bullets as the weapon shows them
        ++(reload?scene_reload:scene_fire);
        scene_row("scene",side,k,s.handle,rate,rated,scene,before);
        scene_after_[side]={GetTickCount64()+1000,s.handle,k,kind};
    }
    // Per side, per tick: ends a scene that finished, or whose copy is gone, hidden, dropped or replaced (or another game's); a new shot plays
    // the fire scene (not on a burst's follow-up shots while the burst's scene runs), a new reload the reload scene.
    template<class Read> static void service_copy_scene(std::uintptr_t base,Read& read,unsigned side,std::uintptr_t own,held_copy::Kind kind,bool fired,bool reloading) {
        auto& s=scenes_[side];auto& a=scene_after_[side];
        if(!s.handle && !a.due && !(own && (fired || reloading))) return;
        const auto game=rig::value<std::uintptr_t>(read,base+0x39a5290).value_or(0);const auto now=GetTickCount64();
        if(s.handle && (s.comp!=own || s.game!=game || now-s.at>10000)) scene_stop(base,side,game);
        else if(s.handle) {const int done=scene_finished(base,game+0x1f5338,s.handle);if(done) {if(done<0) ++scene_faults;s={};}}
        if(a.due && now>=a.due) {scene_row("scene_after",side,a.kind,a.handle,s.handle==a.handle?1:0,-9,0,scene_marks(base,read,own,a.wk));a={};}
        if(fired && !reloading && s.handle && s.kind=='F' && NativeRangedFire::burst_follow_up(side)) fired=false;
        if(own && game && (fired || reloading) && scenes_on() && scene_pinned(base,read)) scene_play(base,read,side,own,kind,reloading,game);
        scene_running=(scenes_[0].handle?1u:0u)+(scenes_[1].handle?1u:0u);
    }
    // Per game tick, before the 250 ms gate: each hand's held copy plays its parts on its shots and reloads (and the looping ones), and the
    // crossbow copy keeps its loaded bolt.
    template<class Read> static void service_copy_state(std::uintptr_t base,Read& read) {
        static const bool pinned=[&]{static constexpr std::array<unsigned char,16> code{0x48,0x89,0x5c,0x24,0x20,0x56,0x48,0x83,0xec,0x50,0x48,0x8b,0x41,0x50,0x8b,0x49};
            std::array<unsigned char,16> got{};return read(base+0x61a650,got.data(),got.size()) && got==code;}();
        for(unsigned side=0;side<2;++side) {
            const auto& h=held_[side];std::uintptr_t ent=0;std::uint32_t id=0;held_copy::Kind kind=held_copy::Kind::other;
            if(h.pe>=0 && h.pe<static_cast<int>(pool_.size()) && h.shown && pool_[h.pe].ent && pool_[h.pe].def) {ent=pool_[h.pe].ent;id=pool_[h.pe].id;kind=held_copy::kind_of(pool_[h.pe].def);}
            if(ent && !alive_entity(base,read,ent,id)) ent=0;
            const auto own=ent && kind!=held_copy::Kind::other && kind!=held_copy::Kind::sword?own_weapon(base,read,ent,kind):0;
            NativeRangedFire::set_copy_comp(side,own && started_weapon(read,own)?own:0);
            if(own) {
                auto& seen=real_seen_[side];
                const auto shot=NativeRangedFire::shot_serial(side),reload=NativeRangedFire::reload_serial(side);
                if(seen.comp!=own) seen={own,shot,reload};
                const bool fired=seen.shot!=shot,reloading=seen.reload!=reload;
                stand_in_type(base,own,fired?3:-1);seen.shot=shot;
                stand_in_type(base,own,reloading?4:-1);seen.reload=reload;
                stand_in_type(base,own,1);
                {CrashOperation operation("native copy scene");try {service_copy_scene(base,read,side,own,kind,fired,reloading);} catch(...) {++scene_faults;}}
                if(kind==held_copy::Kind::crossbow) { // its own bolt: hidden while the hand's magazine is empty or reloading
                    const auto bolt=rig::value<std::uintptr_t>(read,own+0x160).value_or(0);const auto bid=bolt?rig::value<std::uint32_t>(read,bolt+0xa8).value_or(0):0u;
                    if(bolt && alive_entity(base,read,bolt,bid)) {
                        const bool want=NativeRangedFire::loaded(side);const auto f=rig::value<std::uint8_t>(read,bolt+0xd0).value_or(0);
                        if(want==((f&1)!=0)) visible(base,read,bolt,want);   // +0xd0 bit 0 = hidden (what NativeWeaponHands' mirror reads)
                    }
                }
                try {service_bolt(base,read,side,0,0,0);} catch(...) {}
                continue;
            }
            {CrashOperation operation("native copy scene");try {service_copy_scene(base,read,side,0,kind,false,false);} catch(...) {++scene_faults;}}   // no real copy in the hand: its scene ends
            if(pinned && ent) for(auto& u:ups_) if(u.ent==ent && u.id==id) {
                const auto shot=NativeRangedFire::shot_serial(side),reload=NativeRangedFire::reload_serial(side);
                if(u.side!=static_cast<int>(side)) {u.side=static_cast<int>(side);u.shot_seen=shot;u.reload_seen=reload;}
                if(shot!=u.shot_seen) {u.shot_seen=shot;copy_anim(base,u,3);}
                if(reload!=u.reload_seen) {u.reload_seen=reload;copy_anim(base,u,4);}
                copy_anim(base,u,1);
            }
            const auto comp=ent && kind==held_copy::Kind::crossbow?native_equipment(base,read,held_copy::Kind::crossbow):0;
            try {service_bolt(base,read,side,kind==held_copy::Kind::crossbow?ent:0,id,comp);} catch(...) {++upgrade_faults;}
        }
    }
    // The copy's own weapon component (a real weapon copy, v1096): 0 for a model-only copy.
    template<class Read> static std::uintptr_t own_weapon(std::uintptr_t base,Read& read,std::uintptr_t ent,held_copy::Kind kind) {
        const auto comps=ent?rig::list(read,ent+0x268,4096):std::nullopt;if(!comps) return 0;
        for(unsigned i=0;i<comps->packed_count && i<48;++i) {
            const auto c=rig::value<std::uintptr_t>(read,comps->data+i*8ull);
            if(c && *c && rig::typed(read,base,*c,kind==held_copy::Kind::crossbow?0x1b9cbf8:0x1b53f58) && rig::value<std::uintptr_t>(read,*c+0x10)==ent) return *c;
        }
        return 0;
    }
    template<class Read> static bool started_weapon(Read& read,std::uintptr_t comp) {
        const auto id=comp?rig::value<rig::Identity>(read,comp+0x18):std::nullopt;
        return id && id->generation!=-1 && (id->flags&3u)==2u;
    }
    struct ItemList {const std::uintptr_t* data;std::uint32_t count,capacity;}; // an idList<const arkInventoryItem*> as 0x61a750 reads it (+0 data, +8 count)
    static bool set_upgrades(std::uintptr_t comp,const ItemList* add,const ItemList* remove) {
        __try {const auto vt=*reinterpret_cast<const std::uintptr_t*>(comp);
            reinterpret_cast<void (__fastcall*)(std::uintptr_t,const ItemList*,const ItemList*)>(*reinterpret_cast<const std::uintptr_t*>(vt+0xd8))(comp,add,remove);return true;}
        __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    // The game's own upgrade change on the copy's component (vslot 0xd8 = 0x638020: unload, 0x61a750 detach the removed / grow m_upgrades
    // and AttachUpgrade each added item, vslot 0x100 refresh (modifiers, bolt), vslot 0x148 reload): the player weapon's items, shared.
    template<class Read> static void real_upgrades(std::uintptr_t base,Read& read,std::uintptr_t comp,const std::array<std::uintptr_t,max_upgrades>& want,unsigned wn) {
        if(!wn || !started_weapon(read,comp)) return;
        std::array<std::uintptr_t,max_upgrades> have{};unsigned hn=0;
        if(const auto list=rig::list(read,comp+0x50,64)) for(unsigned i=0;i<list->packed_count && hn<have.size();++i)
            if(const auto item=rig::value<std::uintptr_t>(read,list->data+i*0x28ull);item && *item) have[hn++]=*item;
        std::array<std::uintptr_t,max_upgrades> add{},remove{};unsigned an=0,rn=0;
        for(unsigned i=0;i<wn;++i) if(std::find(have.begin(),have.begin()+hn,want[i])==have.begin()+hn) add[an++]=want[i];
        for(unsigned i=0;i<hn;++i) if(std::find(want.begin(),want.begin()+wn,have[i])==want.begin()+wn) remove[rn++]=have[i];
        if(!an && !rn) {try {real_parts_audit(base,read,comp);} catch(...) {++upgrade_faults;}return;}   // v1111: wired already: prove the parts, repair the far ones
        const ItemList a{add.data(),an,an<<8},r{remove.data(),rn,rn<<8};
        if(set_upgrades(comp,&a,&r)) ++real_upgrade_sets;else ++upgrade_faults;
    }
    // v1111 (user, headset v1100/v1110: the second-hand copy and the dropped weapon showed the BASE model although set_upgrades had run once per
    // copy and the copy's list matched the player's; nothing ever looked at the parts): every part in the copy's m_upgrades (+0x50, stride 0x28,
    // static part +0x10, animated part +0x18) and the loaded bolt (+0x160) is audited each pass: hidden (+0xd0 bit0) -> shown; farther than 0.3 m
    // from the copy -> bound again at the place measured on the player's own weapon (place_of, as the model-only path binds), untagged, in the
    // copy's frame. Gauges in status guns.parts = [near, far, unhidden, rebound, bolt_far]; rows in artifacts/real-parts.csv (one per part per second).
    template<class Read> static void real_parts_audit(std::uintptr_t base,Read& read,std::uintptr_t comp) {
        const auto copy=rig::value<std::uintptr_t>(read,comp+0x10).value_or(0);const auto cf=copy?entity_frame(read,copy):std::nullopt;
        const auto list=rig::list(read,comp+0x50,64);if(!cf || !list) return;
        const auto now=GetTickCount64();auto& stamp=parts_logged_[(comp>>4)&7u];const bool log=now-stamp>=1000;if(log) stamp=now;
        std::ofstream o;if(log) o.open(report_dir_/"real-parts.csv",std::ios::app);
        static const char empty_tag[1]{};const std::uintptr_t tag=reinterpret_cast<std::uintptr_t>(empty_tag);
        unsigned near_n=0,far_n=0;
        const auto row=[&](const char* what,unsigned i,unsigned k,std::uintptr_t e,std::uint8_t fl,double dist,std::uintptr_t master,int joint,const std::string& name) {
            if(!log || !o) return;
            o<<now<<','<<what<<','<<std::hex<<copy<<','<<comp<<std::dec<<','<<i<<','<<k<<','<<std::hex<<e<<std::dec<<','<<unsigned(fl)<<','<<dist<<','<<(master==copy?"copy":master?"other":"none")<<','<<joint<<','<<name<<'\n';};
        const auto audit=[&](const char* what,unsigned i,unsigned k,std::uintptr_t e,std::uintptr_t decl,const std::string& name)->void {
            const auto pid=rig::value<std::uint32_t>(read,e+0xa8);if(!pid || !alive_entity(base,read,e,*pid)) return;
            const auto fl=rig::value<std::uint8_t>(read,e+0xd0).value_or(0);
            const auto bind=rig::value<std::uintptr_t>(read,e+0x358).value_or(0);
            const auto master=bind && rig::pointer(bind)?rig::value<std::uintptr_t>(read,bind+0x3c8).value_or(0):0;const int joint=bind && rig::pointer(bind)?rig::value<std::int16_t>(read,bind+0x3d0).value_or(-9):-9;
            const auto pf=entity_frame(read,e);const double dist=pf?length(pf->position-cf->position):-1.0;
            row(what,i,k,e,fl,dist,master,joint,name);
            if(fl&1) {if(k<2 && visible(base,read,e,true)) ++real_parts_unhid;}   // v1113: the bolt's visibility belongs to service_copy_state (hidden while the hand reloads)
            if(!pf || dist<0) return;
            if(dist<.3) {++near_n;return;}
            ++far_n;if(k>1) {++real_bolt_far;return;}   // the bolt: the game's own refresh (0x62b240) places it; only counted
            const auto pl=decl?place_of(decl,k):nullptr;if(!pl) return;
            alignas(16) std::array<float,3> pos{float(pl->local.position.x),float(pl->local.position.y),float(pl->local.position.z)};alignas(16) std::array<float,9> m{};
            for(unsigned r=0;r<3;++r) {m[r*3]=float(pl->local.basis[r].x);m[r*3+1]=float(pl->local.basis[r].y);m[r*3+2]=float(pl->local.basis[r].z);}
            reinterpret_cast<void (__fastcall*)(std::uintptr_t,std::uintptr_t,const std::uintptr_t*,const float*,const float*,bool)>(base+0x875e80)(copy,e,&tag,pos.data(),m.data(),true);
            ++real_parts_rebound;};
        for(unsigned i=0;i<list->packed_count && i<max_upgrades;++i) {
            const auto at=list->data+i*0x28ull;const auto item=rig::value<std::uintptr_t>(read,at);
            const auto decl=item && rig::pointer(*item)?rig::value<std::uintptr_t>(read,*item).value_or(0):0;
            std::string name;if(log && decl) {const auto n=rig::value<std::uintptr_t>(read,decl+0x8);std::array<char,96> t{};if(n && rig::pointer(*n) && read(*n,t.data(),t.size()-1)) {name=t.data();for(auto& c:name) if(c==',' || c<32) c='_';}}
            for(unsigned k=0;k<2;++k) if(const auto part=rig::value<std::uintptr_t>(read,at+0x10+8ull*k);part && *part) audit("part",i,k,*part,decl,name);
        }
        if(rig::typed(read,base,comp,0x1b9cbf8)) if(const auto bolt=rig::value<std::uintptr_t>(read,comp+0x160);bolt && *bolt && rig::pointer(*bolt)) audit("bolt",99,2,*bolt,0,log?entity_decl_name(read,*bolt):std::string{});
        if(log && o) {const auto anim=rig::value<std::uintptr_t>(read,comp+0x20).value_or(0);   // v1112: whether the copy's body can animate at all
            o<<now<<",anim,"<<std::hex<<copy<<','<<comp<<std::dec<<",0,0,"<<std::hex<<anim<<std::dec<<','<<(anim && rig::pointer(anim)?(rig::value<std::uint32_t>(read,anim+0x1c).value_or(0)&3u):9u)<<",-1,"
             <<(anim?"comp":"none")<<','<<int(rig::value<std::uint8_t>(read,copy+0x288).value_or(0))<<",anim_component\n";}
        real_parts_near=near_n;real_parts_far=far_n;
    }
    inline static std::atomic<std::uint64_t> real_parts_near{},real_parts_far{},real_parts_unhid{},real_parts_rebound{},real_bolt_far{};inline static std::array<std::uint64_t,8> parts_logged_{};
    // v1099 (sim v1098: the real copies sat at the player's origin, ent_pos = model_pos = the player, bind master = the player at joint 2): a
    // weapon owned by the player is bound to the player by its own components, so the mod's teleports never moved it. Each real copy is bound
    // instead (0x875e80, no tag, zero offset, identity: joint index -1) to a hidden anchor of its own (the kind's *_midnight.def, spawned raw)
    // that teleport() moves wherever the copy is placed; re-bound whenever its master is not the anchor. An anchor goes with its copy.
    struct Anchor {std::uintptr_t copy{},anchor{};std::uint32_t copy_id{},anchor_id{};};
    inline static std::array<Anchor,16> anchors_{};
    struct ShownRef {std::uintptr_t ent;std::uint32_t id;held_copy::Kind kind;};
    template<class Read> static void service_anchors(std::uintptr_t base,Read& read,const void* shown_v,unsigned ns) {
        struct S {std::uintptr_t ent;std::uint32_t id;held_copy::Kind kind;};const auto* shown=static_cast<const S*>(shown_v);
        for(auto& an:anchors_) if(an.copy && !alive_entity(base,read,an.copy,an.copy_id)) {   // the copy is gone: so is its anchor
            if(alive_entity(base,read,an.anchor,an.anchor_id)) remove_entity(base,an.anchor);
            an={};
        }
        static const char empty_tag[1]{};const std::uintptr_t tag=reinterpret_cast<std::uintptr_t>(empty_tag);
        alignas(16) static const std::array<float,3> zero{};alignas(16) static const std::array<float,9> identity{1,0,0,0,1,0,0,0,1};
        for(unsigned i=0;i<ns;++i) {
            const auto e=shown[i].ent;
            if(!own_weapon(base,read,e,shown[i].kind)) continue;
            Anchor* an=nullptr;for(auto& a:anchors_) if(a.copy==e && a.copy_id==shown[i].id) an=&a;
            if(an && !alive_entity(base,read,an->anchor,an->anchor_id)) {*an={};an=nullptr;}
            if(!an) {
                for(auto& a:anchors_) if(!a.copy) {an=&a;break;}
                if(!an) continue;
                std::array<float,12> f{};if(!read(e+0x300,f.data(),sizeof(f))) continue;
                const char* def=mirror_def_names[shown[i].kind==held_copy::Kind::crossbow?2:1];
                const auto anchor=spawn_def(base,read,def,f.data(),f.data()+3,false);if(!anchor) continue;
                visible(base,read,anchor,false);clear_not_presented(anchor);
                *an={e,anchor,shown[i].id,rig::value<std::uint32_t>(read,anchor+0xa8).value_or(0)};++real_anchors;
            }
            clear_not_presented(an->anchor);   // v1111: the Hide above set the anchor's +0xd0 bit1 as well (0x87e0a0 unpresents what it hides); every Show over the copy inherits
                                               // the MASTER's bit1 (RE 0x87e040) and unpresented the copy and its parts. Bit0 keeps the anchor itself unseen.
            const auto bind=rig::value<std::uintptr_t>(read,e+0x358).value_or(0);
            const auto master=bind && rig::pointer(bind)?rig::value<std::uintptr_t>(read,bind+0x3c8).value_or(0):0;
            if(master!=an->anchor) {
                reinterpret_cast<void (__fastcall*)(std::uintptr_t,std::uintptr_t,const std::uintptr_t*,const float*,const float*,bool)>(base+0x875e80)(an->anchor,e,&tag,zero.data(),identity.data(),true);
                ++real_rebinds;
            }
        }
    }
    template<class Read> static void service_upgrades(std::uintptr_t base,Read& read) {
        static const bool pinned=[&]{static constexpr std::array<unsigned char,16> code{0x48,0x8b,0xc4,0x57,0x41,0x56,0x41,0x57,0x48,0x83,0xec,0x60,0x48,0xc7,0x40,0xd8};
            static constexpr std::array<unsigned char,12> get_component{0x48,0x89,0x5c,0x24,0x10,0x44,0x89,0x44,0x24,0x18,0x55,0x56};
            std::array<unsigned char,16> got{};std::array<unsigned char,12> gc{};
            static constexpr std::array<unsigned char,16> bind{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57};std::array<unsigned char,16> bd{};
            return read(base+0x61a2b0,got.data(),got.size()) && got==code && read(base+0x877720,gc.data(),gc.size()) && gc==get_component
                && read(base+0x875e80,bd.data(),bd.size()) && bd==bind;}();
        if(!pinned) return;
        struct Shown {std::uintptr_t ent;std::uint32_t id;held_copy::Kind kind;};std::array<Shown,16> shown{};unsigned ns=0;
        const auto player_kind=[](const char* def) {
            for(unsigned d=1;d<mirror_def_names.size();++d) if(def && !std::strcmp(def,mirror_def_names[d])) return held_copy::kind_of(def);
            return held_copy::Kind::other;}; // guard weapons and the sword copy carry no player upgrades
        for(const auto& e:pool_) if(e.ent && e.in_use && e.shown) if(const auto k=player_kind(e.def);k!=held_copy::Kind::other) shown[ns++]={e.ent,e.id,k};
        for(unsigned d=1;d<copies_.size();++d) if(const auto& c=copies_[d];c.ent && c.shown) shown[ns++]={c.ent,c.id,player_kind(mirror_def_names[d])};
        for(unsigned k=0;k<2;++k) if(hip_ent_[k] && hip_shown_[k] && ns<shown.size()) shown[ns++]={hip_ent_[k],hip_id_[k],k?held_copy::Kind::crossbow:held_copy::Kind::pistol}; // v1096: the hip gun too
        for(auto& u:ups_) if(u.ent && std::none_of(shown.begin(),shown.begin()+ns,[&](const Shown& s){return s.ent==u.ent && s.id==u.id;})) upgrades_drop(base,read,u);
        service_copy_state(base,read);
        try {service_anchors(base,read,shown.data(),ns);} catch(...) {++upgrade_faults;}
        for(unsigned i=0;i<ns;++i) { // v1096: a real weapon copy that is not equipped hides itself (+0xd0 bit 0, live 0x8b): a shown copy stays shown
            const auto f=rig::value<std::uint8_t>(read,shown[i].ent+0xd0);
            if(f && (*f&1) && alive_entity(base,read,shown[i].ent,shown[i].id) && own_weapon(base,read,shown[i].ent,shown[i].kind) && visible(base,read,shown[i].ent,true)) ++real_reshows;
        }
        for(unsigned i=0;i<ns;++i) { // v1111 (sim: the copy's parts and bolt had +0xd0 bit1 'not presented', live 0x8a, so the render world never held their models;
            // a bound child is presented only by its master's Show recursion, 0x87e0a0 -> entity vslot 0x98 = 0x87c5d0): when the copy or any bound child
            // still carries bit1 and the copy's master does not, one Hide/Show cycle on the copy presents the whole tree in the same tick.
            const auto e=shown[i].ent;if(!alive_entity(base,read,e,shown[i].id) || !own_weapon(base,read,e,shown[i].kind)) continue;
            bool need=(rig::value<std::uint8_t>(read,e+0xd0).value_or(0)&3)==2;
            if(const auto ch=rig::list(read,e+0xb0,64)) for(unsigned c=0;c<ch->packed_count && c<16 && !need;++c)   // v1113: a child hidden on purpose (the bolt while reloading) is left alone (headset v1112: 354 cycles)
                if(const auto p=rig::value<std::uintptr_t>(read,ch->data+c*8ull);p && *p && rig::pointer(*p)) need=(rig::value<std::uint8_t>(read,*p+0xd0).value_or(0)&3)==2;
            if(!need) continue;
            const auto bind=rig::value<std::uintptr_t>(read,e+0x358).value_or(0);
            const auto master=bind && rig::pointer(bind)?rig::value<std::uintptr_t>(read,bind+0x3c8).value_or(0):0;
            if(master && (rig::value<std::uint8_t>(read,master+0xd0).value_or(0)&2)) {++tree_present_blocked;continue;}   // the master would unpresent it again
            if(visible(base,read,e,false) && visible(base,read,e,true)) ++tree_presents;else ++upgrade_faults;
        }
        const auto now=GetTickCount64();
        bool fresh=std::any_of(shown.begin(),shown.begin()+ns,[&](const Shown& s){
            return !own_weapon(base,read,s.ent,s.kind) && std::none_of(ups_.begin(),ups_.end(),[&](const CopyUpgrades& u){return u.ent==s.ent && u.id==s.id;});}); // v1111: a real copy has no ups_ entry
        if(!fresh && now-ups_looked_<250) return;   // a newly shown copy at once, the rest (a bought upgrade) four times a second
        ups_looked_=now;
        std::array<std::array<std::uintptr_t,max_upgrades>,4> items{};std::array<unsigned,4> n{};
        for(const auto kind:{held_copy::Kind::pistol,held_copy::Kind::crossbow}) // measured even with no copy out: a later copy (or drop) needs the places
            if(const auto comp=native_equipment(base,read,kind)) {const auto k=static_cast<unsigned>(kind);try {n[k]=native_upgrades(base,read,comp,kind,items[k]);} catch(...) {++upgrade_faults;}}
        for(unsigned i=0;i<ns;++i) {
            if(!alive_entity(base,read,shown[i].ent,shown[i].id)) continue;
            const auto k=static_cast<unsigned>(shown[i].kind)&3u;
            if(const auto own=own_weapon(base,read,shown[i].ent,shown[i].kind)) {try {real_upgrades(base,read,own,items[k],n[k]);} catch(...) {++upgrade_faults;}continue;}
            try {upgrades_sync(base,read,shown[i].ent,shown[i].id,items[k],n[k]);} catch(...) {++upgrade_faults;}
        }
    }
    static void guns_forget() { // every pointer is dead or already removed: call nothing
        lying_={};held_={};adopt_={};meas_={};pool_={};ups_={};places_={};bolts_={};bolt_place_.reset();real_seen_={};scenes_={};scene_after_={};anchors_={};NativeRangedFire::set_copy_comp(0,0);NativeRangedFire::set_copy_comp(1,0);bash_last_={};bash_struck_={};gun_native_lying_=guns_live_=false;held_bits=0;slot1_cur_=slot1_prev_=0;
        std::lock_guard<std::mutex> l(feed_m_);held_pub_={};copy_draw_={};
    }
    template<class Read> static void service_guns(std::uintptr_t base,Read& read) {
        const bool on=dual_wield_.load(std::memory_order_relaxed),clear=clear_guns_req_.exchange(false,std::memory_order_relaxed);
        if(!on && !clear && !guns_live_) return; // flag off: nothing below runs and nothing is touched
        const auto game=rig::value<std::uintptr_t>(read,base+0x39a5290);
        if(!game || *game!=guns_game_) {guns_forget();guns_game_=game?*game:0;if(!game) return;} // a new level or session
        if(clear || !on) {guns_teardown(base,read);return;}
        const auto now=GetTickCount64();
        for(auto& g:lying_) if(g.present) service_lying(base,read,g,now);
        if(now-slot1_looked_>=50) {slot1_looked_=now; // what slot 1 held before its last change (slot1_restore)
            const auto fsm=player_fsm(base,read);const auto inv=fsm?rig::value<std::uintptr_t>(read,fsm+0x328):std::nullopt;
            const auto item=inv && *inv?rig::value<std::uintptr_t>(read,*inv+0x70):std::nullopt;
            const auto decl=item && *item?rig::value<std::uintptr_t>(read,*item):std::nullopt;
            if(item) {const std::uintptr_t d=decl?*decl:0;if(d!=slot1_cur_) {if(slot1_cur_) slot1_prev_=slot1_cur_;slot1_cur_=d;}}}
        service_measure(base,read,now);
        service_held(base,read);
        service_bash(base,read,now);
        bool live=false;for(const auto& g:lying_) live=live || g.present;for(const auto& e:pool_) live=live || e.ent!=0;for(const auto& m:meas_) live=live || m.native!=0;
        guns_live_=live;
    }
    // Op::carry_left_redraw (v1044): the left item CarryCorpse put away (its item decl, remembered before the carry), equipped again as the
    // wheel does (code 0x30) once the carry holds. v1041's EquipChange draw of the "selected" item (0x24) was refused three times in a row
    // during a carry (sim v1044: queued, left slot stayed empty); this is slot1_restore's path with an explicit item.
    template<class Read> static bool carry_redraw(std::uintptr_t base,Read& read,std::uintptr_t decl) {
        if(!decl) return false;
        const auto fsm=player_fsm(base,read);if(!fsm) return false;
        const auto inventory=rig::value<std::uintptr_t>(read,fsm+0x328);if(!inventory || !*inventory) return false;
        const auto item=find_record(read,*inventory,decl,0);if(!item) return false;
        return equip_item(base,read,item,0,0x30);
    }
    // Op::slot1_restore: the item slot 1 held before its last change is equipped again, as the wheel does (code 0x30), e.g. the power a wheel
    // swapped out for a gun that went to the other hand as its copy.
    template<class Read> static bool slot1_restore(std::uintptr_t base,Read& read) {
        if(!dual_wield_.load(std::memory_order_relaxed) || !slot1_prev_ || slot1_prev_==slot1_cur_) return false;
        const auto fsm=player_fsm(base,read);if(!fsm) return false;
        const auto inventory=rig::value<std::uintptr_t>(read,fsm+0x328);if(!inventory || !*inventory) return false;
        const auto item=find_record(read,*inventory,slot1_prev_,0);if(!item) return false;
        return equip_item(base,read,item,0,0x30);
    }
    // Test command "equip pistol|crossbow" (sim, v765): the wheel's own equip of that weapon with its selected ammo. The native wheel ignored
    // the sim's stick in the chapter-4 save, so scenarios equip through this.
    // v769: also "regular_grenade|sticky_grenade|stun_mine|springrazor": the gadget record whose item decl names it (code 0x30, as slot1_restore).
    // EXPERIMENT (v1095 live RE, "realcopy pistol|crossbow|report|remove"): a second instance of the player's own weapon entity (its decl at
    // entity+0x2e8) spawned with the player as owner, and a dump of its components next to the player's weapon's, to learn whether the game
    // wires a second weapon by itself. Writes realcopy-report.txt in the artifacts folder.
public:
    inline static std::atomic<bool> real_copies_{true};inline static std::atomic<std::uint64_t> real_spawned{},real_upgrade_sets{},real_ammo_sets{},real_reshows{},real_anchors{},real_rebinds{};
    inline static std::filesystem::path report_dir_{};inline static std::uintptr_t rc_ent_{};inline static std::uint32_t rc_id_{};inline static int rc_kind_{-1};
    static void set_report_dir(const std::filesystem::path& d) {report_dir_=d;}
    // v1111: the render model's state words (RE: +0x18bc flags, bit1 = first-person list; +0x1c30 bit0 = freed / not in the render world; +0x1bd0;
    // +0x1bf8 render world; +0x1c18 def index) and 16 floats after the transform block as bounds candidates.
    template<class Read> static std::string model_fields(Read& read,std::uintptr_t m) {
        std::ostringstream s;s<<std::hex<<" m18bc="<<rig::value<std::uint32_t>(read,m+0x18bc).value_or(0)<<" m1c30="<<unsigned(rig::value<std::uint8_t>(read,m+0x1c30).value_or(0))<<" m1bd0="<<rig::value<std::uint64_t>(read,m+0x1bd0).value_or(0)
         <<" world="<<rig::value<std::uintptr_t>(read,m+0x1bf8).value_or(0)<<" defidx="<<rig::value<std::uint32_t>(read,m+0x1c18).value_or(0)<<" m1c31="<<unsigned(rig::value<std::uint8_t>(read,m+0x1c31).value_or(0))<<std::dec
         <<" surfaces="<<rig::value<std::uint32_t>(read,m+0xd38).value_or(0)<<" m1fb0="<<std::hex<<rig::value<std::uint32_t>(read,m+0x1fb0).value_or(0)<<" pose="<<rig::value<std::uintptr_t>(read,m+0x1f48).value_or(0)<<std::dec;
        {std::array<float,6> bb{};if(read(m+0x1840,bb.data(),12) && read(m+0x1850,bb.data()+3,12)) s<<" aabb=["<<bb[0]<<","<<bb[1]<<","<<bb[2]<<" / "<<bb[3]<<","<<bb[4]<<","<<bb[5]<<"]";}
        const auto world=rig::value<std::uintptr_t>(read,m+0x1bf8);const auto idx=rig::value<std::uint32_t>(read,m+0x1c18);
        if(world && rig::pointer(*world) && idx && *idx<0x10000) { // RE: sphere at world+0x3e550+idx*16, cull byte at world+0x7a560+idx (9 never drawn, 0xb/0xf drawn)
            std::array<float,4> sp{};if(read(*world+0x3e550+static_cast<std::uintptr_t>(*idx)*16,sp.data(),16)) s<<" sphere=["<<sp[0]<<","<<sp[1]<<","<<sp[2]<<" r"<<sp[3]<<"]";
            s<<" cull="<<std::hex<<unsigned(rig::value<std::uint8_t>(read,*world+0x7a560+*idx).value_or(0))<<std::dec;
        }
        return s.str();
    }
    template<class Read> static void dump_entity(std::uintptr_t base,Read& read,std::ostream& o,const char* label,std::uintptr_t e) {
        o<<label<<" entity="<<std::hex<<e<<std::dec;if(!e) {o<<"\n";return;}
        o<<" id="<<std::hex<<rig::value<std::uint32_t>(read,e+0xa8).value_or(0)<<" owner="<<rig::value<std::uint32_t>(read,e+0xac).value_or(0)
         <<" vt="<<(rig::value<std::uintptr_t>(read,e).value_or(base)-base)<<" decl="<<entity_decl_name(read,e)<<" flags_d0="<<unsigned(rig::value<std::uint8_t>(read,e+0xd0).value_or(0))
         <<" model="<<rig::value<std::uintptr_t>(read,e+0xf0).value_or(0)<<std::dec;
        const auto text=[&](std::uintptr_t p){std::array<char,96> t{};if(p && rig::pointer(p) && read(p,t.data(),t.size()-1)) return std::string(t.data());return std::string{};};
        if(const auto m=rig::value<std::uintptr_t>(read,e+0xf0);m && rig::pointer(*m)) // v1111: which model object the entity draws (an upgrade may have replaced it)
            o<<std::hex<<" model_vt="<<(rig::value<std::uintptr_t>(read,*m).value_or(base)-base)<<" asset_static="<<rig::value<std::uintptr_t>(read,*m+0x1c50).value_or(0)<<" asset_anim="<<rig::value<std::uintptr_t>(read,*m+0x1fe8).value_or(0)<<std::dec
             <<" children="<<(rig::value<std::uint32_t>(read,e+0xb8).value_or(0)&0xffffff)<<std::hex<<" d4="<<unsigned(rig::value<std::uint8_t>(read,e+0xd4).value_or(0))<<std::dec<<" c8="<<rig::value<float>(read,e+0xc8).value_or(-1.f)
             <<model_fields(read,*m);
        if(const auto ch=rig::list(read,e+0xb0,64)) for(unsigned c=0;c<ch->packed_count && c<max_upgrades;++c) if(const auto p=rig::value<std::uintptr_t>(read,ch->data+c*8ull);p && *p && rig::pointer(*p)) {
            std::array<float,3> pp{};read(*p+0x300,pp.data(),12);const auto pm=rig::value<std::uintptr_t>(read,*p+0xf0).value_or(0);
            o<<"\n  child"<<c<<std::hex<<" ent="<<*p<<" d0="<<unsigned(rig::value<std::uint8_t>(read,*p+0xd0).value_or(0))<<std::dec<<" pos=["<<pp[0]<<","<<pp[1]<<","<<pp[2]<<"]"<<(pm && rig::pointer(pm)?model_fields(read,pm):std::string{});
        }
        {std::array<float,3> ep{},mp{};read(e+0x300,ep.data(),12);const auto m=rig::value<std::uintptr_t>(read,e+0xf0);if(m && rig::pointer(*m)) read(*m+0x1860,mp.data(),12);
         o<<" ent_pos=["<<ep[0]<<","<<ep[1]<<","<<ep[2]<<"] model_pos=["<<mp[0]<<","<<mp[1]<<","<<mp[2]<<"]";
         const auto bind=rig::value<std::uintptr_t>(read,e+0x358).value_or(0);o<<" bind="<<std::hex<<bind<<std::dec;
         if(bind && rig::pointer(bind)) o<<" bind_master="<<std::hex<<rig::value<std::uintptr_t>(read,bind+0x3c8).value_or(0)<<" joint="<<std::dec<<rig::value<std::int16_t>(read,bind+0x3d0).value_or(-9);}
        o<<"\n";
        const auto comps=rig::list(read,e+0x268,4096);if(!comps) {o<<"  no component list\n";return;}
        for(unsigned i=0;i<comps->packed_count && i<48;++i) {
            const auto c=rig::value<std::uintptr_t>(read,comps->data+i*8ull);if(!c || !*c) continue;
            const auto vt=rig::value<std::uintptr_t>(read,*c).value_or(0);const auto id=rig::value<rig::Identity>(read,*c+0x18);
            o<<"  comp "<<i<<" @"<<std::hex<<*c<<" vt="<<(vt?vt-base:0)<<" owner="<<rig::value<std::uintptr_t>(read,*c+0x10).value_or(0)<<" gen="<<std::dec<<(id?id->generation:-9)<<" flags="<<std::hex<<(id?id->flags:0);
            if(rig::typed(read,base,*c,0x1b53f58) || rig::typed(read,base,*c,0x1b9cbf8)) {
                o<<" WEAPON +30="<<rig::value<std::uintptr_t>(read,*c+0x30).value_or(0)<<" +40="<<rig::value<std::uintptr_t>(read,*c+0x40).value_or(0)<<" +48="<<rig::value<std::uintptr_t>(read,*c+0x48).value_or(0)
                 <<" +68="<<rig::value<std::uintptr_t>(read,*c+0x68).value_or(0)<<" +70="<<rig::value<std::uintptr_t>(read,*c+0x70).value_or(0)<<" +98="<<rig::value<std::uintptr_t>(read,*c+0x98).value_or(0)
                 <<std::dec<<" mag="<<rig::value<std::int32_t>(read,*c+0xa0).value_or(-9)<<"/"<<rig::value<std::int32_t>(read,*c+0xa4).value_or(-9)
                 <<" ups="<<(rig::value<std::uint32_t>(read,*c+0x58).value_or(0)&0xffffff)<<" mods="<<(rig::value<std::uint32_t>(read,*c+0xb0).value_or(0)&0xffffff)
                 <<std::hex<<" b8="<<unsigned(rig::value<std::uint8_t>(read,*c+0xb8).value_or(0))<<" +160="<<rig::value<std::uintptr_t>(read,*c+0x160).value_or(0)<<std::dec
                 <<" started="<<(rig::value<std::uint32_t>(read,*c+0x1c).value_or(0)&3u);
                {const auto anim=rig::value<std::uintptr_t>(read,*c+0x20).value_or(0);   // v1112: the weapon body's own animation component (plays decl+0xd0 loaded / +0x228 empty clips)
                 o<<std::hex<<" anim="<<anim<<std::dec;
                 if(anim && rig::pointer(anim)) o<<" anim_vt="<<std::hex<<(rig::value<std::uintptr_t>(read,anim).value_or(base)-base)<<std::dec<<" anim_started="<<(rig::value<std::uint32_t>(read,anim+0x1c).value_or(0)&3u)<<" anim_model="<<std::hex<<rig::value<std::uintptr_t>(read,anim+0x38).value_or(0)<<std::dec;
                 o<<" ctx="<<unsigned(rig::value<std::uint8_t>(read,e+0x288).value_or(0));}
                if(const auto ups=rig::list(read,*c+0x50,64)) for(unsigned u=0;u<ups->packed_count && u<max_upgrades;++u) { // v1111: every upgrade_t and its parts (RE: udecl +0x200/+0x208 model override, +0x230 static part, +0x238 part def, +0x240 joint tag)
                    const auto at=ups->data+u*0x28ull;const auto item=rig::value<std::uintptr_t>(read,at).value_or(0);
                    const auto udecl=item && rig::pointer(item)?rig::value<std::uintptr_t>(read,item).value_or(0):0;
                    o<<"\n    up"<<u<<std::hex<<" item="<<item<<" decl="<<udecl<<" orig_model="<<rig::value<std::uintptr_t>(read,at+8).value_or(0)<<" anim="<<rig::value<std::uintptr_t>(read,at+0x20).value_or(0);
                    if(udecl && rig::pointer(udecl)) o<<" name="<<text(rig::value<std::uintptr_t>(read,udecl+0x8).value_or(0))<<" m200="<<rig::value<std::uintptr_t>(read,udecl+0x200).value_or(0)<<" m208="<<rig::value<std::uintptr_t>(read,udecl+0x208).value_or(0)
                        <<" p230="<<rig::value<std::uintptr_t>(read,udecl+0x230).value_or(0)<<" d238="<<rig::value<std::uintptr_t>(read,udecl+0x238).value_or(0)<<" tag="<<text(rig::value<std::uintptr_t>(read,udecl+0x240).value_or(0));
                    o<<std::dec;
                    for(unsigned k=0;k<2;++k) if(const auto part=rig::value<std::uintptr_t>(read,at+0x10+8ull*k).value_or(0);part && rig::pointer(part)) {
                        std::array<float,3> pp{},mp{};read(part+0x300,pp.data(),12);const auto pm=rig::value<std::uintptr_t>(read,part+0xf0).value_or(0);if(pm && rig::pointer(pm)) read(pm+0x1860,mp.data(),12);
                        const auto bind=rig::value<std::uintptr_t>(read,part+0x358).value_or(0);const bool bp=bind && rig::pointer(bind);
                        o<<"\n      part"<<k<<std::hex<<" ent="<<part<<" id="<<rig::value<std::uint32_t>(read,part+0xa8).value_or(0)<<" d0="<<unsigned(rig::value<std::uint8_t>(read,part+0xd0).value_or(0))<<" d4="<<unsigned(rig::value<std::uint8_t>(read,part+0xd4).value_or(0))
                         <<" model="<<pm<<" master="<<(bp?rig::value<std::uintptr_t>(read,bind+0x3c8).value_or(0):0)<<std::dec<<" joint="<<(bp?rig::value<std::int16_t>(read,bind+0x3d0).value_or(-9):-9)
                         <<" pos=["<<pp[0]<<","<<pp[1]<<","<<pp[2]<<"] model_pos=["<<mp[0]<<","<<mp[1]<<","<<mp[2]<<"] def="<<entity_decl_name(read,part);
                    }
                }
                if(const auto bolt=rig::value<std::uintptr_t>(read,*c+0x160).value_or(0);bolt && rig::pointer(bolt)) {
                    std::array<float,3> pp{};read(bolt+0x300,pp.data(),12);const auto bind=rig::value<std::uintptr_t>(read,bolt+0x358).value_or(0);const bool bp=bind && rig::pointer(bind);
                    o<<"\n    bolt"<<std::hex<<" ent="<<bolt<<" d0="<<unsigned(rig::value<std::uint8_t>(read,bolt+0xd0).value_or(0))<<" master="<<(bp?rig::value<std::uintptr_t>(read,bind+0x3c8).value_or(0):0)<<std::dec<<" joint="<<(bp?rig::value<std::int16_t>(read,bind+0x3d0).value_or(-9):-9)
                     <<" pos=["<<pp[0]<<","<<pp[1]<<","<<pp[2]<<"] def="<<entity_decl_name(read,bolt);
                }
            }
            o<<"\n";
        }
    }
    template<class Read> static bool real_copy(std::uintptr_t base,Read& read,const char* what,const std::array<float,3>& at) {
        if(!what) return false;
        std::ofstream o(report_dir_/"realcopy-report.txt",std::ios::app);
        o<<"=== "<<what<<" t="<<GetTickCount64()<<"\n";
        const bool spawn=!std::strcmp(what,"pistol") || !std::strcmp(what,"crossbow");
        if(spawn) rc_kind_=!std::strcmp(what,"crossbow")?1:0;
        const auto kind=rc_kind_==1?held_copy::Kind::crossbow:held_copy::Kind::pistol;
        const auto comp=native_equipment(base,read,kind);
        const auto native=comp?rig::value<std::uintptr_t>(read,comp+0x10).value_or(0):0;
        dump_entity(base,read,o,"NATIVE",native);
        if(const auto other=native_equipment(base,read,rc_kind_==1?held_copy::Kind::pistol:held_copy::Kind::crossbow)) dump_entity(base,read,o,"NATIVE_OTHER",rig::value<std::uintptr_t>(read,other+0x10).value_or(0)); // v1111: both player weapons
        if(!std::strcmp(what,"remove")) {if(alive_entity(base,read,rc_ent_,rc_id_)) remove_entity(base,rc_ent_);rc_ent_=0;rc_id_=0;o<<"removed\n";return true;}
        if(spawn) {
            if(alive_entity(base,read,rc_ent_,rc_id_)) remove_entity(base,rc_ent_);
            const auto game=rig::value<std::uintptr_t>(read,base+0x39a5290);const auto owner=player(base,read);
            const auto decl=native?rig::value<std::uintptr_t>(read,native+0x2e8).value_or(0):0;
            o<<"spawn decl="<<std::hex<<decl<<" owner="<<owner<<std::dec<<"\n";
            if(!game || !owner || !decl) return false;
            alignas(16) std::array<float,3> pos=at;alignas(16) std::array<float,9> axis{1,0,0,0,1,0,0,0,1};static const std::array<float,3> zero{};
            const auto e=reinterpret_cast<std::uintptr_t (__fastcall*)(std::uintptr_t,std::uintptr_t,const float*,const float*,std::uintptr_t,bool,const float*,const float*,const float*)>(
                base+0x8ae9b0)(*game,decl,pos.data(),axis.data(),owner,false,nullptr,zero.data(),zero.data());
            rc_ent_=e;rc_id_=e?rig::value<std::uint32_t>(read,e+0xa8).value_or(0):0;
        }
        dump_entity(base,read,o,"COPY",alive_entity(base,read,rc_ent_,rc_id_)?rc_ent_:0);
        for(unsigned side=0;side<2;++side) if(const auto& h=held_[side];h.pe>=0 && h.pe<static_cast<int>(pool_.size()) && pool_[h.pe].ent && alive_entity(base,read,pool_[h.pe].ent,pool_[h.pe].id))
            {dump_entity(base,read,o,side?"HELD_RIGHT":"HELD_LEFT",pool_[h.pe].ent);const auto& c=held_[side].cmd.position;o<<"  target=["<<c.x<<","<<c.y<<","<<c.z<<"] player="<<std::hex<<player(base,read)<<std::dec<<"\n";}
        return true;
    }
private:
    template<class Read> static bool test_equip(std::uintptr_t base,Read& read,const char* what) {
        if(!what) return false;
        const bool crossbow=!std::strcmp(what,"crossbow"),gun=crossbow || !std::strcmp(what,"pistol");
        const auto game=rig::value<std::uintptr_t>(read,base+0x39a5290);
        const auto fsm=player_fsm(base,read);const auto inventory=fsm?rig::value<std::uintptr_t>(read,fsm+0x328):std::nullopt;
        const auto entries=inventory && *inventory?rig::list(read,*inventory+0x38,4096):std::nullopt;if(!entries || !game) return false;
        for(unsigned i=0;i<entries->packed_count;++i) {
            const auto record=entries->data+i*0x38ull;const auto comp=rig::value<std::uintptr_t>(read,record+0x10);
            if(!comp || !*comp) continue;
            if(gun) {
                if(!rig::typed(read,base,*comp,crossbow?0x1b9cbf8:0x1b53f58)) continue;
                const auto entry=rig::value<std::uintptr_t>(read,*comp+0x98);const auto ammo_decl=entry && *entry?rig::value<std::uintptr_t>(read,*entry):std::nullopt;
                return equip_item(base,read,record,ammo_decl && *ammo_decl?find_record(read,*inventory,*ammo_decl,0):0);
            }
            if(!rig::typed(read,base,*comp,0x1b9d8b8) && !rig::typed(read,base,*comp,0x1b54538) && !rig::typed(read,base,*comp,0x1b99c68)) continue; // throwable, placeable, the Heart
            if(rig::value<std::int32_t>(read,record+0x20).value_or(0)<=0) continue;
            const auto decl=rig::value<std::uintptr_t>(read,record);const auto name=decl && *decl?rig::value<std::uintptr_t>(read,*decl+0x8):std::nullopt; // the item's own name (live v769)
            std::array<char,128> text{};
            if(name && rig::pointer(*name) && read(*name,text.data(),text.size()-1) && std::strstr(text.data(),what)) return equip_item(base,read,record,0,0x30);
        }
        return false;
    }
    static void publish_guns() {
        std::array<GunCopy,2> out{};
        for(unsigned i=0;i<2;++i) {const auto& g=lying_[i];
            if(!g.present) continue;
            auto& c=out[i];c.present=true;c.flying=g.flight.flying;c.native=g.native;c.kind=g.kind;c.def=g.def;c.since=g.since;
            c.pos={float(g.flight.pos.x),float(g.flight.pos.y),float(g.flight.pos.z)};
            for(unsigned k=0;k<3;++k) {c.axis[k*3]=float(g.flight.axis[k].x);c.axis[k*3+1]=float(g.flight.axis[k].y);c.axis[k*3+2]=float(g.flight.axis[k].z);}
        }
        std::lock_guard<std::mutex> l(m_);published_guns_=out;
    }
private:
    template<class Read> static void publish(std::uintptr_t base,Read& read) {
        publish_guns();Copy c{};
        if(alive(base,read) && shown_) { // the mod owns the copy's pose (it never simulates by itself)
            const auto& p=flight_.pos;const auto& b=flight_.axis[0];
            c={true,flight_.flying,{float(p.x),float(p.y),float(p.z)},{float(b.x),float(b.y),float(b.z)},copy_since_,trace_};
        } else {if(!alive(base,read)) {copy_=0;copy_id_=0;}flight_.flying=false;}
        std::lock_guard<std::mutex> l(m_);published_=c;
    }
};
inline NativeWeaponHands::Copy NativeWeaponHands::published_{};
inline NativeWeaponHands::PropFlight NativeWeaponHands::prop_{};
inline NativeWeaponHands::Hip NativeWeaponHands::hip_req_{};
inline NativeWeaponHands::HipExchange NativeWeaponHands::hip_exchange_{};
} // namespace d2vr
