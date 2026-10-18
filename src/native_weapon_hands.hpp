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
#include "native_weapon_visibility.hpp"
#include "native_manual_equip_runtime.hpp"
#include "crash_observer.hpp"
#include <algorithm>
#include <cmath>
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <vector>
#include <string>
#include <cstdio>
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
class NativeWeaponHands {
    friend struct NativeHipVisibilityFixture;
    friend struct NativeWeaponExchangeFixture;
public:
    enum class Op : unsigned {draw,sheathe,drop,pickup,recall,stow,gun_draw,gun_holster,shove,ragdoll,yank,wake};
    // shove: the game's TakeDamage on target (an arkNPC) with damage decl `decl` (a name under the damage type), from pos
    // along vel (the push direction), amount 0 = the decl's own.
    struct Request {Op op{};std::array<float,3> pos{},vel{},spin{};std::array<float,9> axis{1,0,0,0,1,0,0,0,1};std::uintptr_t target{};const char* decl{};float amount{};unsigned side{};bool free_look{};std::uint64_t manual_generation{},manual_xr_epoch{};NativePlayerStateLease manual_owner;};
    // The exact sword the hand holds (the cinematic copy of sword_player.md6: refframe + animation, no physics, no audio). The
    // howler sword prop used before carried an audio component; spawned and removed 47 times in a headset session, the audio
    // update crashed on a freed emitter (2026-10-13, rva 0xc3e00 from 0x709de7). One copy per level, hidden and shown again.
    static constexpr const char* sword_copy_decl="models/campaign/dunwall/midnightassets/sword_player_midnight.def";
    // Any thread: queued for the next game-thread service.
    static bool request(const Request& r) {
        const auto ok=[](const auto& a){return std::all_of(a.begin(),a.end(),[](float v){return std::isfinite(v);});};
        if(!ok(r.pos) || !ok(r.vel) || !ok(r.axis) || !ok(r.spin)) return false; // NaN into Teleport/spawn reaches every physics body (REA Q4)
        std::lock_guard<std::mutex> lock(m_);
        if(queue_.size()<8) {queue_.push_back(r);if(r.op==Op::gun_holster)++holster_enqueued_;return true;}
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
            }
            if(!ok) ++failures;
        }
        if(copy_ && shown_ && flight_.flying) {CrashOperation operation("native weapon flight");try {fly(base,read);} catch(...) {flight_.flying=false;}}
        {CrashOperation operation("native hip holster");try {service_hip(base,read);} catch(...) {}}
        hold_down(base,read);
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
        static const bool melee_ok=NativeMelee::verify(base,read);
        const auto body=native_rigid_body(read,base,prop_.entity);
        if(!body || body->entity_id!=prop_.id || !melee_ok) {prop_.until=0;return;}
        LARGE_INTEGER qpc{},freq{};QueryPerformanceCounter(&qpc);QueryPerformanceFrequency(&freq);
        const double dt=static_cast<double>(qpc.QuadPart-prop_.qpc.QuadPart)/static_cast<double>(freq.QuadPart);if(dt<.002) return;
        const auto from=prop_.last,p=body->pose.position;prop_.last=p;prop_.qpc=qpc;
        const double speed=length(p-from)/dt;if(speed<3) return;
        const auto decl=phys_damage(base,read);const auto game=rig::value<std::uintptr_t>(read,base+0x39a5290);
        const auto player=game?rig::value<std::uintptr_t>(read,*game+0x3130b0):std::nullopt;if(!decl || !player) return;
        NativeMelee::Sword s{*player,prop_.entity,0,decl,rig::value<std::uint32_t>(read,*player+0xa8).value_or(0),prop_.id,
            static_cast<float>(std::clamp(speed*.6,2.,8.))}; // ponytail: 2-8 hp by speed (a guard has 30-40); tune in headset
        int fault{};const Vec3 half=body->bounds?(body->bounds->maximum-body->bounds->minimum)*.5:Vec3{};
        if(strike(base,read,*game,from,p,{0,0,1},std::min(length(half),.5),s,prop_.struck,fault)) ++prop_hits;
        if(fault==2) prop_.until=0;
    }
    // Entity events (ARK_MSG_*): payload = 8112c0(eventManager 810240() = game+0x1e8968, +0xc8e8, msg, size, target id), filled
    // by the caller, delivered by the game. START_RAGDOLL 0xb9 {u64 0, u64 0} is what DropCorpse/Far Reach post (829a10, 43fe94);
    // VELOCITY_IMPULSE 0x149 {v[3], w[3], u16 flags} the generic set-velocity event (docs/native-hold-object.md).
    template<class Read> static void* post(std::uintptr_t base,Read& read,unsigned msg,unsigned size,std::uint32_t target) {
        static const bool pinned=[&]{std::array<unsigned char,14> a{},b{};
            const std::array<unsigned char,14> want_a{0x48,0x8b,0x05,0x49,0x50,0x19,0x03,0x48,0x05,0x68,0x89,0x1e,0x00,0xc3};
            const std::array<unsigned char,14> want_b{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x44,0x89,0x4c,0x24};
            return read(base+0x810240,a.data(),a.size()) && a==want_a && read(base+0x8112c0,b.data(),b.size()) && b==want_b;}();
        if(!pinned) return nullptr;
        const auto manager=reinterpret_cast<std::uintptr_t (__fastcall*)()>(base+0x810240)();
        return manager?reinterpret_cast<void* (__fastcall*)(std::uintptr_t,unsigned,unsigned,std::uint32_t)>(base+0x8112c0)(manager+0xc8e8,msg,size,target):nullptr;
    }
    template<class Read> static bool ragdoll(std::uintptr_t base,Read& read,std::uintptr_t npc,std::array<float,3> velocity) {
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
        if(grabbed_[side] && GetTickCount64()-grabbed_at_[side]<=2000 && living(read,grabbed_[side])) {
            const auto model=rig::value<std::uintptr_t>(read,grabbed_[side]+0xf0);
            const auto at=model && rig::pointer(*model)?rig::value<std::array<float,3>>(read,*model+0x1860):std::nullopt;
            if(at && std::hypot(hand.x-(*at)[0],hand.y-(*at)[1])<1.6) best=grabbed_[side];
        }
        if(!best) best=guard_at(base,read,hand,.5);
        grabbed_[side]=0;
        if(!best) return true; // nobody there: the hand just moved fast
        Vec3 pull{r.vel[0],r.vel[1],r.vel[2]};if(length(pull)>6) pull=pull*(6/length(pull));
        Request s=r;s.op=Op::shove;s.target=best;s.decl="components/base/damage/shove_damage.damage";
        s.vel={float(-pull.x/std::max(length(pull),1e-6)),float(-pull.y/std::max(length(pull),1e-6)),0}; // hit from the side he is pulled to
        shove(base,read,s);
        const bool ok=ragdoll(base,read,best,{float(pull.x),float(pull.y),float(pull.z)});
        if(ok) {++yanks;NativeGrab::yanked(r.side,best,GetTickCount64()+1500);} // the squeezing hand keeps hold of him
        return ok;
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
        auto* p=post(base,read,0xb9,0x10,*id);if(!p) return false;
        std::memset(p,0,0x10);++wakes;NativeGrab::yanked(r.side,best,GetTickCount64()+1500);
        return true;
    }
public:
    static bool block_keeps_gun() noexcept {return NativeBlockEligibility::process_owner().enabled();}
private:
    // A living guard held by a limb stays in his ragdoll while held, up to 3 s (then he breaks free and gets up): his
    // conscious ragdoll recovers after 0.5-2 s by itself (sim v432), faster than a hand can drag him.
    inline static std::array<std::uintptr_t,2> held_guard_{};inline static std::array<std::uint64_t,2> held_since_{},held_post_{};
    template<class Read> static void hold_down(std::uintptr_t base,Read& read) {
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
    template<class Read> static std::uintptr_t spawn_def(std::uintptr_t base,Read& read,const char* name,const float* pos,const float* axis) {
        for(unsigned i=0;i<12;++i) if(!std::isfinite(i<3?pos[i]:axis[i-3])) {++failures;return 0;} // REA Q4: the engine does not check; pos[3], axis[9]
        const auto game=rig::value<std::uintptr_t>(read,base+0x39a5290);if(!game) return 0;
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
        reinterpret_cast<void (__fastcall*)(std::uintptr_t,const float*,const float*,int)>(base+0x87d1e0)(e,o.data(),a.data(),0);
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
            if(hip_exchange_.blocked || !h.show || !h.context || !runtime.hip_empty_current(*h.context))return false;
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
    template<class Read> static void publish(std::uintptr_t base,Read& read) {
        Copy c{};
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
