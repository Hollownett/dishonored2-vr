#pragma once
// Plan B task B13: the real-engine pieces shared by the B11 spike (src/hand_body_spike_engine.hpp, dev knob build/hand-body-spike) and the
// production hand body (src/hand_body_runtime.hpp, build/hand-body). Promoted from the spike engine, not duplicated: the pins, the count-only
// G1/G2 hooks, the pool reads and the R1 recipe (create / wake / destroy) live here once. Nothing here is installed or called unless one of
// the two knobs started its owner; the two owners never run together (the runtime refuses while the spike knob exists).
// docs/native-hand-body-re.md is the authority for every RVA and layout.
//
// Hooks (count-only, MinHook, byte-pinned): 0x8ad260 game frame entry (frames, thread), 0x97bc0 physics step (depth, thread, 0x8a7820 at entry),
// 0x945f0 the step's drain (depth), 0x853220 owner wake (calls with the hand's owner), 0x8527a0 OnExitedBroadPhase (calls for a hand's handle).
// Engine calls (all pinned, all through guarded_call, each with 0x8a7820 re-read false immediately before it, from the 0x89fb0 G1 point):
//   0x18cb90 userdata ctor and 0xca6550 sphere ctor (pure stores into mod memory, Q1 1.4 / Q2), 0xca65f0 cinfo ctor, 0xc8e5b0 createBody
//   (addMode 1, activation 1), 0xc85dc0 velocity, 0xc8f6b0 destroyBodies (mode 0, as every engine caller passes).
// Never called: 0x91b50 / 0x91e20 (no pair is ever disabled), 0xc85860, 0xc90de0, any spawn, any queue record the B8 encoders leave out.
#include "native_guarded_call.hpp"
#include <windows.h>
#include <MinHook.h>
#include "hand_body.hpp"
#include "hand_body_probe.hpp"
#include "native_hand_volume.hpp"
#include "native_world_trace.hpp"
#include "crash_observer.hpp"
#include <array>
#include <atomic>
#include <cstring>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace d2vr {

// Mod memory for one body (Q1 1.4, Q2, Q3). It must outlive the body: it is reused only after destroyBodies has confirmed the slot empty,
// and a body abandoned with its world keeps its storage for the process (the engine's teardown may still read it).
struct R1BodyStorage {
    alignas(16) std::array<std::byte,0x100> userdata{};   // >= 0x80 (Q1 1.4)
    alignas(16) std::array<std::byte,0x60> sphere{};      // Q2: 0x60, memSize 0 after construction (every refcount op is a no-op)
    alignas(16) std::array<std::byte,0x100> cinfo{};      // >= 0x98 (Q3)
};

class HandBodyNative {
public:
    using Read=bool (*)(std::uintptr_t,void*,std::size_t);
    struct Pin {std::uintptr_t rva;std::array<unsigned char,16> code;};
    // First 16 file bytes at every called or hooked RVA (Dishonored2.exe b671f824...; docs/native-hand-body-re.md pins, 0xc8f6b0 and 0xc85dc0
    // read from the exe for B11). The sphere ctor is pinned over its whole 113 bytes by NativeHandVolume::constructor_code.
    static constexpr std::array<Pin,11> pins{{
        {0x8ad260,{0x40,0x57,0x48,0x83,0xec,0x50,0x48,0xc7,0x44,0x24,0x20,0xfe,0xff,0xff,0xff,0x48}},
        {0x97bc0,{0x48,0x8b,0xc4,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48}},
        {0x945f0,{0x48,0x8b,0xc4,0x57,0x41,0x56,0x41,0x57,0x48,0x83,0xec,0x70,0x48,0xc7,0x40,0x98}},
        {0x853220,{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x30,0x48}},
        {0x8527a0,{0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0xfa,0x48,0x8b,0x12}},
        {0xc8e5b0,{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x48,0x89,0x7c,0x24,0x18,0x4c}},
        {0xca65f0,{0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x33,0xff,0x0f,0x57,0xc9,0x48}},
        {0xc8f6b0,{0x48,0x8b,0xc4,0x48,0x83,0xec,0x48,0x48,0x89,0x58,0x08,0x48,0x89,0x68,0x10,0x48}},
        {0x18cb90,{0x0f,0x57,0xc0,0x33,0xd2,0x48,0x8d,0x05,0x9c,0x24,0x97,0x01,0x48,0x89,0x01,0x48}},
        {0x8a7820,{0x80,0xb9,0x21,0x71,0x33,0x00,0x00,0x75,0x1c,0x80,0xb9,0x22,0x71,0x33,0x00,0x00}},
        {0xc85dc0,{0x48,0x89,0x6c,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x89,0x54,0x24,0x10,0x57,0x48}},
    }};
    static constexpr std::uintptr_t frame_rva=0x8ad260,step_rva=0x97bc0,drain_rva=0x945f0,wake_rva=0x853220,exit_rva=0x8527a0,
        create_rva=0xc8e5b0,cinfo_rva=0xca65f0,destroy_rva=0xc8f6b0,userdata_rva=0x18cb90,velocity_rva=0xc85dc0,sphere_rva=0xca6550,
        userdata_vtable_rva=0x1aff038,userdata_dtor_rva=0x801380,player_userdata_vtable_rva=0x1b02768,sphere_vtable_rva=0x1ec1e78;
    static constexpr std::uint32_t no_handle=0xffffffffu;

    // Checks every pin and the two userdata vtables' first slot (the base dtor 0x801380). False disables the owner.
    static bool verify(std::uintptr_t base,Read read,std::string& why) {
        for(const auto& pin:pins) {
            std::array<unsigned char,16> got{};
            if(!read(base+pin.rva,got.data(),got.size()) || got!=pin.code) {std::ostringstream o;o<<"pin mismatch at 0x"<<std::hex<<pin.rva;why=o.str();return false;}
        }
        std::array<unsigned char,113> sphere{};
        if(!read(base+sphere_rva,sphere.data(),sphere.size()) || sphere!=NativeHandVolume::constructor_code) {why="pin mismatch at 0xca6550";return false;}
        for(const auto vt:{userdata_vtable_rva,player_userdata_vtable_rva}) {
            std::uint64_t slot{};
            if(!read(base+vt,&slot,8) || slot!=base+userdata_dtor_rva) {std::ostringstream o;o<<"vtable slot mismatch at 0x"<<std::hex<<vt;why=o.str();return false;}
        }
        return true;
    }

    // ---- count-only hooks (installed by the spike or the runtime, never both). verify() must have passed for this base.
    static bool install_hooks(std::uintptr_t base,Read read,std::string& why) {
        base_=base;read_=read;
        reset_counters();
        struct Target {std::uintptr_t rva;void* detour;void** original;};
        const std::array<Target,5> targets{{{frame_rva,reinterpret_cast<void*>(&frame_hook),reinterpret_cast<void**>(&frame_original_)},
            {step_rva,reinterpret_cast<void*>(&step_hook),reinterpret_cast<void**>(&step_original_)},
            {drain_rva,reinterpret_cast<void*>(&drain_hook),reinterpret_cast<void**>(&drain_original_)},
            {wake_rva,reinterpret_cast<void*>(&wake_hook),reinterpret_cast<void**>(&wake_original_)},
            {exit_rva,reinterpret_cast<void*>(&exit_hook),reinterpret_cast<void**>(&exit_original_)}}};
        for(const auto& t:targets) {
            void* target=reinterpret_cast<void*>(base+t.rva);
            if(!*t.original && MH_CreateHook(target,t.detour,t.original)!=MH_OK) {why="cannot create a count-only hook";disable_hooks();return false;}
            if(MH_EnableHook(target)!=MH_OK) {why="cannot enable a count-only hook";disable_hooks();return false;}
            enabled_rvas_.push_back(t.rva);
        }
        counting_.store(true,std::memory_order_release);
        return true;
    }
    // Keeps the RVAs that failed to disable (a later stop can retry). The hooks are count-only and harmless while installed.
    static bool disable_hooks() {
        counting_.store(false,std::memory_order_release);
        bool ok=true;
        std::vector<std::uintptr_t> kept;
        for(const auto rva:enabled_rvas_) if(MH_DisableHook(reinterpret_cast<void*>(base_+rva))!=MH_OK) {ok=false;kept.push_back(rva);}
        enabled_rvas_=kept;return ok;
    }
    // Classifies one 0x89fb0 dispatch (counts it for G1) and says whether it is the G1 point: outside the step (0x97bc0 depth 0), on the
    // 0x8ad260 thread. frame: the 0x8ad260 count at this dispatch (the owner acts once per game frame).
    struct Dispatch {bool g1_point{};std::uint64_t frame{};};
    static Dispatch note_dispatch() {
        const auto id=static_cast<std::uint32_t>(GetCurrentThreadId());
        const auto n=dispatches_.fetch_add(1,std::memory_order_relaxed)+1;
        dispatch_thread_.store(id,std::memory_order_relaxed);
        const auto frame_thread=frame_thread_.load(std::memory_order_relaxed);
        if(!frame_thread) before_frame_.fetch_add(1,std::memory_order_relaxed);
        else if(id!=frame_thread) off_frame_thread_.fetch_add(1,std::memory_order_relaxed);
        bool outside=false;
        if(step_depth_.load()==0) {outside_.fetch_add(1,std::memory_order_relaxed);outside=true;}
        else if(drain_depth_.load()>0 && step_thread_.load()==id) step_drain_.fetch_add(1,std::memory_order_relaxed);
        else overlap_.fetch_add(1,std::memory_order_relaxed);
        if(n%64==0) {   // 0x8a7820 sampled on every 64th dispatch (five guarded reads), as the B7 probe does
            if(const auto busy=physics_busy_flag(read_,base_)) {dispatch_busy_samples_.fetch_add(1,std::memory_order_relaxed);if(*busy) dispatch_busy_.fetch_add(1,std::memory_order_relaxed);}
        }
        return {outside && frame_thread && id==frame_thread,frames_.load(std::memory_order_relaxed)};
    }
    static G1Stats g1_now(double observed_s) {
        G1Stats s;s.observed_s=observed_s;s.frames=frames_.load();s.steps=steps_.load();s.step_busy=step_busy_.load();s.dispatches=dispatches_.load();
        s.dispatch_busy_samples=dispatch_busy_samples_.load();s.dispatch_busy=dispatch_busy_.load();s.dispatch_outside_step=outside_.load();
        s.dispatch_step_drain=step_drain_.load();s.dispatch_overlap=overlap_.load();s.dispatch_off_frame_thread=off_frame_thread_.load();
        s.dispatch_before_frame=before_frame_.load();s.frame_threads=frame_threads_.load();s.frame_thread=frame_thread_.load();s.dispatch_thread=dispatch_thread_.load();
        return s;
    }
    // G2 watch: 0x853220 calls with this owner, 0x8527a0 calls for these handles (full handle, generation included). no_handle: unwatched.
    static void watch_owner(std::uintptr_t owner) {watch_owner_.store(owner,std::memory_order_relaxed);}
    static void watch_handle(unsigned slot,std::uint32_t handle) {if(slot<watch_handles_.size()) watch_handles_[slot].store(handle,std::memory_order_relaxed);}
    static G2Counts g2_now() {return {wake_owner_.load(),exit_hand_.load()};}
    static std::uint64_t wake_total() {return wake_total_.load();}
    static std::uint64_t exit_total() {return exit_total_.load();}

    // ---- reads
    static std::uintptr_t world_now() {
        const auto game=rig::value<std::uintptr_t>(read_,base_+0x39a5290);
        const auto world=game?rig::value<std::uintptr_t>(read_,*game+0x60410):std::nullopt;
        return world && rig::pointer(*world)?*world:0;
    }
    static bool busy_now() {return physics_busy_flag(read_,base_).value_or(true);}
    // Is the pool slot of `handle` our body? present: the slot holds exactly this handle (+0x70, generation included) and our userdata (+0x90);
    // absent: it does not (destroyed, recycled, another world); unreadable: the pool or the slot could not be read this time (retry, not gone).
    enum class Own {present,absent,unreadable};
    static Own own_state(std::uintptr_t world,std::uint32_t handle,const R1BodyStorage& s,std::uintptr_t* record_out=nullptr) {
        if(!world) return Own::unreadable;
        const auto pool=rig::value<std::uintptr_t>(read_,world+0x28);const auto count=rig::value<std::uint32_t>(read_,world+0x30);
        if(!pool || !count || !rig::pointer(*pool) || !*count || *count>0x1000000u) return Own::unreadable;
        const std::uint32_t index=handle&0xffffffu;
        if(index==0xffffffu || index>=*count) return Own::absent;
        const auto record=*pool+static_cast<std::uintptr_t>(index)*0xb0;
        const auto stored=rig::value<std::uint32_t>(read_,record+0x70);const auto user=rig::value<std::uintptr_t>(read_,record+0x90);
        if(!stored || !user) return Own::unreadable;
        if(*stored!=handle || *user!=reinterpret_cast<std::uintptr_t>(s.userdata.data())) return Own::absent;
        if(record_out) *record_out=record;
        return Own::present;
    }
    // After a createBody fault: is there a record holding our userdata? Its handle, or no_handle.
    static std::uint32_t scan_for_own_body(std::uintptr_t world,const R1BodyStorage& s) {
        const auto pool=rig::value<std::uintptr_t>(read_,world+0x28);const auto count=rig::value<std::uint32_t>(read_,world+0x30);
        if(!pool || !count || !rig::pointer(*pool) || *count>0x1000000u) return no_handle;
        for(std::uint32_t i=0;i<*count;++i) {
            const auto record=*pool+static_cast<std::uintptr_t>(i)*0xb0;
            if(rig::value<std::uintptr_t>(read_,record+0x90)==reinterpret_cast<std::uintptr_t>(s.userdata.data()))
                if(const auto h=rig::value<std::uint32_t>(read_,record+0x70); h && (*h&0xffffffu)==i) return *h;
        }
        return no_handle;
    }
    // The body's translation (record +0x30). nullopt: the slot no longer holds our body. Throws HandBodyUnreadable.
    static std::optional<Vec3> position(std::uintptr_t world,std::uint32_t handle,const R1BodyStorage& s) {
        std::uintptr_t record{};const auto own=own_state(world,handle,s,&record);
        if(own==Own::unreadable) throw HandBodyUnreadable{};
        if(own==Own::absent) return std::nullopt;
        const auto t=rig::value<std::array<float,3>>(read_,record+0x30);if(!t) throw HandBodyUnreadable{};
        return Vec3{(*t)[0],(*t)[1],(*t)[2]};
    }
    // Raw words of a slot (handle +0x70, flags +0x44, userdata low half +0x90), index and count checked; for the logs.
    static std::optional<std::array<std::uint32_t,3>> slot_words(std::uintptr_t world,std::uint32_t handle) {
        const auto pool=rig::value<std::uintptr_t>(read_,world+0x28);const auto count=rig::value<std::uint32_t>(read_,world+0x30);
        if(!pool || !count || !rig::pointer(*pool) || *count>0x1000000u || (handle&0xffffffu)>=*count) return std::nullopt;
        const auto slot=*pool+static_cast<std::uintptr_t>(handle&0xffffffu)*0xb0;
        return std::array<std::uint32_t,3>{rig::value<std::uint32_t>(read_,slot+0x70).value_or(0xdeadbeef),rig::value<std::uint32_t>(read_,slot+0x44).value_or(0xdeadbeef),
                                            rig::value<std::uint32_t>(read_,slot+0x90).value_or(0xdeadbeef)};
    }
    static std::optional<std::uint32_t> flags(std::uintptr_t world,std::uint32_t handle,const R1BodyStorage& s) {
        std::uintptr_t record{};const auto own=own_state(world,handle,s,&record);
        if(own==Own::unreadable) throw HandBodyUnreadable{};
        if(own==Own::absent) return std::nullopt;
        const auto f=rig::value<std::uint32_t>(read_,record+0x44);if(!f) throw HandBodyUnreadable{};
        return *f;
    }

    // ---- the R1 recipe (B11 route results; B12 GO). Caller: the 0x89fb0 G1 point only.
    // create: builds userdata (ud+8 = owner), sphere and cinfo in `s`, then createBody(world,&id,cinfo,1,1) with 0x8a7820 re-read false right
    // before it. false: refused with no createBody call. Throws HandBodyEngineFault (with the handle of a record holding our userdata, if a
    // faulting or unverified create left one). g2_at_create (optional): the G2 counters taken just before the call.
    static bool create(R1BodyStorage& s,std::uintptr_t world,std::uintptr_t owner,Vec3 at,double radius,std::uint32_t& handle,G2Counts* g2_at_create=nullptr) {
        if(!world || !finite(at) || !(radius>0 && radius<.2) || !hand_body_probe_detail::user_pointer(owner) || busy_now()) return false;
        std::memset(s.userdata.data(),0,s.userdata.size());std::memset(s.sphere.data(),0,s.sphere.size());std::memset(s.cinfo.data(),0,s.cinfo.size());
        using Ctor=void* (__fastcall*)(void*);
        using Create=std::uint32_t* (__fastcall*)(void*,std::uint32_t*,const void*,unsigned char,unsigned char);
        const auto ud=reinterpret_cast<Ctor>(base_+userdata_rva);
        const auto sphere=reinterpret_cast<NativeHandVolume::Construct>(base_+sphere_rva);
        const auto cinfo_ctor=reinterpret_cast<Ctor>(base_+cinfo_rva);
        const auto create_body=reinterpret_cast<Create>(base_+create_rva);
        alignas(16) const std::array<float,4> centre{};
        bool built=false;
        auto build=[&] {
            ud(s.userdata.data());
            sphere(s.sphere.data(),centre.data(),static_cast<float>(radius));
            cinfo_ctor(s.cinfo.data());
            built=true;
        };
        if(!guarded_call(build) || !built) throw HandBodyEngineFault("userdata/sphere/cinfo ctor faulted");
        std::uintptr_t ud_vtable{},sphere_vtable{};std::memcpy(&ud_vtable,s.userdata.data(),8);std::memcpy(&sphere_vtable,s.sphere.data(),8);
        if(ud_vtable!=base_+userdata_vtable_rva || sphere_vtable!=base_+sphere_vtable_rva) return false;
        std::memcpy(s.userdata.data()+8,&owner,8);                              // 0x694c20 (ud, owner): the single store ud+8 = owner
        const std::uint16_t mem_size=0;std::memcpy(s.sphere.data()+0x10,&mem_size,2);   // Q2: memSize 0, refcount ops become no-ops
        const auto put=[&s](std::size_t at_offset,const void* v,std::size_t n) {std::memcpy(s.cinfo.data()+at_offset,v,n);};
        const std::uintptr_t shape=reinterpret_cast<std::uintptr_t>(s.sphere.data()),user=reinterpret_cast<std::uintptr_t>(s.userdata.data());
        const std::uint32_t flags=R1Recipe::flags,filter=R1Recipe::filter;const std::uint16_t material=0;const unsigned char motion_type=R1Recipe::motion_type;
        const std::array<float,4> position{static_cast<float>(at.x),static_cast<float>(at.y),static_cast<float>(at.z),0};const float mass=R1Recipe::mass_kg;
        put(0x00,&shape,8);put(0x08,&flags,4);put(0x10,&filter,4);put(0x14,&material,2);put(0x20,&user,8);put(0x28,&motion_type,1);
        put(0x30,position.data(),16);put(0x70,&mass,4);   // +0x40 orientation stays the ctor's identity, +0x80 props 0xffff (dynamic preset, gravity 1)
        std::uint32_t id=no_handle;std::uint32_t* out=nullptr;
        auto call=[&] {out=create_body(reinterpret_cast<void*>(world),&id,s.cinfo.data(),1,1);};
        CrashOperation operation("hand body createBody");
        if(g2_at_create) *g2_at_create=g2_now();   // G2 counts from here: an event inside createBody counts
        if(busy_now()) return false;                                     // 0x8a7820 re-read immediately before the call (nothing runs in between)
        if(!guarded_call(call)) throw HandBodyEngineFault("createBody faulted",scan_for_own_body(world,s));
        if(out) id=*out;
        if(own_state(world,id,s)!=Own::present)   // created but not where the doc says: find our record; if any, it gets the one destroy attempt
            throw HandBodyEngineFault("created body did not verify (record +0x70 / +0x90)",scan_for_own_body(world,s));
        handle=id;
        return true;
    }
    // destroyBodies(world,&id,1,0) on our own slot only, with 0x8a7820 re-read false right before it; the slot is read again afterwards.
    // Throws HandBodyEngineFault (the state is then unknown: the caller keeps the body excluded and never retries).
    static DestroyOutcome destroy(const R1BodyStorage& s,std::uintptr_t world,std::uintptr_t world_at_create,std::uint32_t handle) {
        if(!world || world!=world_at_create) return DestroyOutcome::refused;
        const auto own=own_state(world,handle,s);
        if(own==Own::absent) return DestroyOutcome::absent;   // not our body any more (removed, recycled): never destroy someone else's
        if(own==Own::unreadable) return DestroyOutcome::refused;
        using Destroy=void (__fastcall*)(void*,const std::uint32_t*,int,int);
        const auto destroy_bodies=reinterpret_cast<Destroy>(base_+destroy_rva);
        const std::uint32_t ids[1]{handle};
        auto call=[&] {destroy_bodies(reinterpret_cast<void*>(world),ids,1,0);};
        CrashOperation operation("hand body destroyBodies");
        if(busy_now()) return DestroyOutcome::refused;     // 0x8a7820 re-read immediately before the call
        if(!guarded_call(call)) throw HandBodyEngineFault("destroyBodies faulted");
        const auto after=own_state(world,handle,s);
        return after==Own::present?DestroyOutcome::still_present:DestroyOutcome::gone;   // unreadable after a clean return: gone (logged by the owner)
    }
    // 0xc85dc0 velocity on our own slot only, with 0x8a7820 re-read false right before it. false: refused, no call. Caps: 2 m/s, 20 rad/s.
    static bool velocity(const R1BodyStorage& s,std::uintptr_t world,std::uintptr_t world_at_create,std::uint32_t handle,Vec3 v,Vec3 w) {
        if(!world || world!=world_at_create || !finite(v) || !finite(w) || length(v)>2 || length(w)>20 || own_state(world,handle,s)!=Own::present) return false;
        using Velocity=void (__fastcall*)(void*,UINT,const float*,const float*,int);
        const auto set_velocity=reinterpret_cast<Velocity>(base_+velocity_rva);
        alignas(16) const std::array<float,4> linear{static_cast<float>(v.x),static_cast<float>(v.y),static_cast<float>(v.z),0};
        alignas(16) const std::array<float,4> angular{static_cast<float>(w.x),static_cast<float>(w.y),static_cast<float>(w.z),0};
        auto call=[&] {set_velocity(reinterpret_cast<void*>(world),handle,linear.data(),angular.data(),0);};
        if(busy_now()) return false;                       // 0x8a7820 re-read immediately before the call
        if(!guarded_call(call)) throw HandBodyEngineFault("velocity write faulted");
        return true;
    }
    // Owner (a)'s static G2 evidence from B7's chain on the player's query body (read live by probe_body_handle).
    static OwnerEvidence owner_a_evidence(const std::optional<BodyProbeRow>& row) {
        OwnerEvidence e;
        if(!row) return e;
        const auto& r=*row;
        e.a_resolved=hand_body_probe_detail::user_pointer(r.userdata_plus8);
        e.a_userdata_is_player=r.userdata_vtable_rva==player_userdata_vtable_rva;
        e.a_entity_is_player=r.owner_is_player;e.a_type_decoded=r.typeinfo_decoded;e.a_type_in_range=r.type_in_range;
        return e;
    }
    static std::uintptr_t base() {return base_;}
    static Read reader() {return read_;}

private:
    using Frame=char (__fastcall*)(void*,void*,void*,void*);
    using Pass2=void (__fastcall*)(void*,void*,void*,void*);
    inline static Frame frame_original_{};inline static Pass2 step_original_{},drain_original_{},wake_original_{},exit_original_{};
    inline static std::uintptr_t base_{};inline static Read read_{};
    inline static std::atomic<bool> counting_{};
    inline static std::atomic<std::uint64_t> frames_{},steps_{},step_busy_{},dispatches_{},dispatch_busy_samples_{},dispatch_busy_{},outside_{},step_drain_{},overlap_{},
        off_frame_thread_{},before_frame_{},frame_threads_{},wake_owner_{},wake_total_{},exit_hand_{},exit_total_{};
    inline static std::atomic<int> step_depth_{},drain_depth_{};
    inline static std::atomic<std::uint32_t> frame_thread_{},step_thread_{},dispatch_thread_{};
    inline static std::array<std::atomic<std::uint32_t>,8> frame_thread_ids_{};
    inline static std::atomic<std::uintptr_t> watch_owner_{};
    inline static std::array<std::atomic<std::uint32_t>,2> watch_handles_{{no_handle,no_handle}};
    inline static std::vector<std::uintptr_t> enabled_rvas_;
    static void reset_counters() {
        for(auto* c:{&frames_,&steps_,&step_busy_,&dispatches_,&dispatch_busy_samples_,&dispatch_busy_,&outside_,&step_drain_,&overlap_,&off_frame_thread_,&before_frame_,
                     &frame_threads_,&wake_owner_,&wake_total_,&exit_hand_,&exit_total_}) c->store(0);
        step_depth_=0;drain_depth_=0;frame_thread_=0;step_thread_=0;dispatch_thread_=0;for(auto& t:frame_thread_ids_) t=0;
        watch_owner_=0;for(auto& h:watch_handles_) h=no_handle;
    }
    static char __fastcall frame_hook(void* game,void* a,void* b,void* c) {
        if(counting_.load(std::memory_order_relaxed)) {
            const auto id=static_cast<std::uint32_t>(GetCurrentThreadId());
            frames_.fetch_add(1,std::memory_order_relaxed);frame_thread_.store(id,std::memory_order_relaxed);
            for(auto& slot:frame_thread_ids_) {
                auto seen=slot.load(std::memory_order_relaxed);
                if(seen==id) break;
                if(!seen && slot.compare_exchange_strong(seen,id)) {frame_threads_.fetch_add(1,std::memory_order_relaxed);break;}
                if(seen==id) break;
            }
        }
        return frame_original_(game,a,b,c);
    }
    static void __fastcall step_hook(void* physics,void* a,void* b,void* c) {
        if(counting_.load(std::memory_order_relaxed)) {
            steps_.fetch_add(1,std::memory_order_relaxed);step_thread_.store(static_cast<std::uint32_t>(GetCurrentThreadId()),std::memory_order_relaxed);
            if(busy_now()) step_busy_.fetch_add(1,std::memory_order_relaxed);
        }
        step_depth_.fetch_add(1);step_original_(physics,a,b,c);step_depth_.fetch_sub(1);
    }
    static void __fastcall drain_hook(void* p,void* a,void* b,void* c) {drain_depth_.fetch_add(1);drain_original_(p,a,b,c);drain_depth_.fetch_sub(1);}
    static void __fastcall wake_hook(void* p,void* owner,void* a,void* b) {
        if(counting_.load(std::memory_order_relaxed)) {
            wake_total_.fetch_add(1,std::memory_order_relaxed);
            const auto watched=watch_owner_.load(std::memory_order_relaxed);
            if(watched && reinterpret_cast<std::uintptr_t>(owner)==watched) wake_owner_.fetch_add(1,std::memory_order_relaxed);
        }
        wake_original_(p,owner,a,b);
    }
    static void __fastcall exit_hook(void* p,void* event,void* info,void* c) {
        if(counting_.load(std::memory_order_relaxed)) {
            exit_total_.fetch_add(1,std::memory_order_relaxed);
            bool any=false;for(const auto& h:watch_handles_) any=any || h.load(std::memory_order_relaxed)!=no_handle;
            if(any)
                if(const auto h=rig::value<std::uint32_t>(read_,reinterpret_cast<std::uintptr_t>(info)+8))
                    for(const auto& w:watch_handles_) if(const auto x=w.load(std::memory_order_relaxed); x!=no_handle && *h==x) {exit_hand_.fetch_add(1,std::memory_order_relaxed);break;}
        }
        exit_original_(p,event,info,c);
    }
};

} // namespace d2vr
