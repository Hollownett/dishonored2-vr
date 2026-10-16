#pragma once
// Solid hands, engine side (hand_contact.hpp has the math). The render thread publishes each empty open hand's drawn pose, raw
// tracked pose and finger curls; the physics-command callback (native_grab.hpp, after the dispatcher 0x89fb0) finds the loose
// props near a hand, intersects the hand's sphere proxies with each, and raises the prop's linear velocity along the push
// direction with the velocity setter 0xc85dc0 (the same writer the grab motor and the blade poke use: it wakes a sleeping body).
// Layout read (static, Steam 17576867, c85dc0 decompile): world +0x28 body pool (stride 0xb0, motion index at +0x40), world
// +0x120 motion array (stride 0x80): linear velocity float3 at +0x40, angular at +0x50, first attached body id at +0x28.
// Candidates: the entity registry (game+0x3130b0) is scanned a slice per service for entities within 0.9 m of a palm; only entities
// near a hand get the full native_rigid_body read, with native carryables (bottles, glasses) allowed: a hand may push them though
// it never grabs them. A hand's own grab, the game's HoldObject and a held prop are not pushed.
// Cost (v502 sim: ~75 us a service, 0.5 ms peaks, all of it one read_self per registry entry): a slot's class is cached with its
// entity pointer, so a steady-state slice reads only the position of the few prop-class entities (the three vtables
// native_rigid_body accepts; every other entity can never be a candidate) and a changed pointer is classified once. A slot is
// re-classified every recheck_cycles-th pass (staggered by slot) in case the allocator reused its address for another class,
// and a service reads at most scan_budget entries, resuming mid-slice (the cold first pass over a level takes ~70 services).
// A candidate in reach is read in full (native_rigid_body, ~45 reads) once every full_period_ms (staggered per entity, and at most
// max_full_reads a service, so a table of props arriving together does not cost one tick 400 reads); in between its body is
// refreshed (8 reads: registry slot, pool and motion array pointers, the 0xb0-byte record, component identity and carry fields,
// velocity, render bounds). A refused entity is not read at all until ignore_until.
// Sticky contact: a prop a hand drove stays engaged for engage_ms (hand_contact.hpp). The CSV is written by the render thread.
// The world cast may hold the drawn hand back from a prop the tracked hand is pushing (weapon_collision.hpp: a chest-to-hand sphere sweep
// can cross the prop away from the hand, 4-12 cm in the lab's model of sim v525): publish() takes the entity the cast held the hand at,
// and for that prop the proxies are the tracked hand's (hand_contact.hpp, ContactInput::held).
// A slow service (> 100 us) is told apart as preempted (the thread's CPU time, the caller's `cpu` clock, was under half the wall time) or
// slow in itself (the velocity setter's wake-up, a cache fill), and logged to native-hand-contact-slow.csv with its phases.
// A driven prop gets its velocity and an angular velocity (the uprighting spin, else zero) in one write; PropDrag (per prop) learns the
// friction the engine takes off it between writes, and tells when it is blocked or the hand's ease is stalled.
// The bodies are handled first, the registry scan last (it gets what is left of the service, at most scan_limit_us).
// The drawn hand meets what it pushes (meet(), read by the render thread's world-cast block): the cast's grip hold-off (palm_radius off the first surface a
// chest-to-hand sweep meets) would leave the drawn hand 4-12 cm short of a prop the tracked hand is pushing, so for a light loose prop in reach the cast is not
// applied and the hand is moved out of the prop instead, by the way out of the deepest overlap of the tracked hand (HandContact::outward). A hand more than 12 cm
// into a prop (past its middle: the way out would flip) and a prop that does not give way (PropDrag blocked: pinned) are held off by the cast again, as static geometry is.
// In the game the scan is not in the service at all (inline_discovery false): the physics thread is the one a 150-200 us stall hurts, the scan was
// 85% of its 12 us, and nothing it finds is needed within a frame, so discover() runs it once a frame from the render thread with a reader of
// its own and hands the entities it finds to the service through a small locked list (adopted at the start of a service, try_lock: never waits).
// The registry scan reads through its own reader (`scan`: a direct-mapped cache of validated 64 KB blocks, native_grab.hpp) and stops at
// scan_limit_us: the shared 32-entry, 100 ms region cache thrashed on entities scattered over the heap, a VirtualQuery per read.
#include "hand_contact.hpp"
#include "physical_grab.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace d2vr {
struct NativeMotion {Vec3 velocity{};std::uint16_t inverse_mass{};};
template<class Read> std::optional<NativeMotion> native_motion(Read& read,const NativeRigidBody& body) {
    using namespace rig;
    const auto index=value<std::uint32_t>(read,body.record+0x40);
    const auto array=value<std::uintptr_t>(read,body.world+0x120);
    if(!index || *index>=0x100000 || !array || !pointer(*array)) return {};
    const auto at=*array+static_cast<std::uintptr_t>(*index)*0x80;
    const auto v=value<std::array<float,3>>(read,at+0x40);const auto m=value<std::uint16_t>(read,at+0x26);
    if(!v || !m) return {};
    const Vec3 velocity{(*v)[0],(*v)[1],(*v)[2]};
    if(!finite(velocity) || length(velocity)>100) return {};
    return NativeMotion{velocity,*m};
}

class NativeHandContact {
    friend struct NativeHandContactPublicationFixture;
public:
    static constexpr unsigned registry_size=0x3fff,slice_size=1024,near_capacity=32,max_writes=8,scan_budget=256,recheck_cycles=32;
    static constexpr std::uint64_t full_period_ms=1000;static constexpr unsigned max_full_reads=3;
    static constexpr double scan_reach=.9,keep_reach=1.3,hand_span=.3,fresh_seconds=.1;
    // Counters for the status JSON: services run, entities tested in full, hands touching a prop, velocity writes, faults, and
    // the cost of services past the early-outs (microseconds, total and worst).
    std::atomic<std::uint64_t> publishes{},services{},tested{},touches{},pushes{},faults{},cost_us{},cost_max_us{};
    std::atomic<std::uint64_t> scan_us{},body_us{},math_us{},slow_services{}; // slow_services: services that took over 100 us // the split of cost_us: registry scan, body reads, contact tests + writes
    // preempted: slow services the OS took the thread away from; write_us / write_max_us / slow_writes (> 50 us): time inside the velocity setter;
    // cast_seen: publishes that carried a cast entity, cast_held: hand-prop tests where it was the prop's (the entity pointers agree).
    std::atomic<std::uint64_t> preempted{},write_us{},write_max_us{},slow_writes{},cast_seen{},cast_held{};
    // discoveries: render-thread scans (discover); discover_us / discover_max_us: their cost
    std::atomic<std::uint64_t> discoveries{},discover_us{},discover_max_us{};
    // meets: meet() calls for a pushable prop the cast was holding the hand at; meet_shifts: calls that moved the drawn hand out of a prop; meet_latches: pinned props
    // (held off by the cast again)
    std::atomic<std::uint64_t> meets{},meet_shifts{},meet_latches{};
    std::atomic<std::uint64_t> keepouts{}; // loose props skipped because they touch the player's body or lie under it (set_player)
    bool inline_discovery=true; // true: the registry scan runs inside service() (the unit tests); false: discover() runs it (native_grab.hpp)
    static constexpr std::uint64_t latch_services=150; // ~2 s of services with no discover() call at all: nobody calls it, the service scans after all
    HandContactConfig config{};
    std::int64_t scan_limit_us=40,service_limit_us=90; // a service stops scanning after 40 us and handling bodies after 90 us (the next service resumes)
    bool wanted() const {return wanted_.load(std::memory_order_relaxed);} // an open empty hand is published: the callback has work
    struct NoCpu {double operator()() const {return 0;}}; // the thread's CPU time in microseconds (monotonic; 0: not measured)
    // Render thread, every frame: where the player is (the camera origin). No loose prop that touches the player's body (a cylinder of player_radius around the eye, from
    // player_below under it to player_above over it) or lies under the player's feet is pushed: the engine moves a character out of a prop shoved into it or from under
    // its feet, and the player sees a launch (sim v509 s2a: the camera hopped 1.5 m before any push; no write of the contact explains it, but none may shove what the player stands on).
    // Props the game carries or has attached to the player are refused by native_rigid_body already. Without a position (a unit test) nothing is kept out.
    static constexpr double player_radius=.40,player_below=2.2,player_above=.3;
    void set_player(Vec3 eye) {if(!finite(eye)) return;std::lock_guard<std::mutex> l(mutex_);player_eye_=eye;player_stamp_=seconds();}
    static constexpr double meet_latch=.08,meet_unlatch=.055,meet_fresh=.06,meet_stale=.15; // m, m, s, s
    // Render thread, in the hand's world-cast block (dxgi_probe.cpp). cast_entity: what the palm cast holds the hand at (0: nothing). prop: that is a light loose prop
    // this hand pushes, so the cast must not hold the hand off it; entity, shift: the prop the tracked hand overlaps most and how the drawn hand has to move to leave
    // it (zero: none), so tracked + shift touches the prop's surface. Nothing for a hand that is not pushing (closed, armed, holding), for a prop that is heavy or
    // out of reach, for a prop that does not give way (pinned), or for one the hand is more than meet_latch into (until it is back under meet_unlatch).
    struct Meeting {bool prop{};std::uintptr_t entity{};Vec3 shift{};};
    Meeting meet(unsigned side,std::uintptr_t cast_entity) {
        Meeting m{};if(side>1) return m;
        {std::lock_guard<std::mutex> l(mutex_);if(!lanes_[side].active) {meet_latched_[side]=false;return m;}}
        std::lock_guard<std::mutex> l(meet_mutex_);const double now=seconds();
        if(now-meet_.stamp>meet_stale) {meet_latched_[side]=false;return m;}
        const bool fresh=now-meet_.side_stamp[side]<=meet_fresh;
        const double depth=fresh?length(meet_.outward[side]):0.;
        if(depth>meet_latch) {if(!meet_latched_[side]) ++meet_latches;meet_latched_[side]=true;} else if(depth<meet_unlatch) meet_latched_[side]=false;
        if(meet_latched_[side]) return m;
        if(cast_entity) for(unsigned i=0;i<meet_.count;++i) if(meet_.pushable[i]==cast_entity) {m.prop=true;++meets;}
        if(fresh && depth>0) {m.entity=meet_.entity[side];m.shift=meet_.outward[side];++meet_shifts;}
        return m;
    }

    void install(const std::filesystem::path& file) {
        log_.open(file,std::ios::trunc);log_on_=log_.is_open();
        // gap/error/trim/friction/lag/held: the terms of the control (hand_contact.hpp) behind this write: lag is the size of the cast's shift of the drawn hand (m),
        // held 1 when the cast held the hand at this prop
        if(log_) log_<<"tick_ms,side,entity_id,handle,mass_raw_hex,mass_kg,depth,hand_speed,body_speed_before,body_speed_after,nx,ny,nz,gap,error,trim,friction,lag,held,turn\n";
        slow_.open(file.parent_path()/L"native-hand-contact-slow.csv",std::ios::trunc);
        if(slow_) slow_<<"tick_ms,total_us,cpu_us,scan_us,body_us,math_us,write_max_us,query_max_us,fulls,writes,near,tested\n";
        {std::lock_guard<std::mutex> l(log_mutex_);pending_.clear();slow_pending_.clear();}
        slow_logged_=0;latched_=false;{std::lock_guard<std::mutex> l(found_mutex_);found_.clear();gone_.clear();service_game_=service_world_=0;}
        std::lock_guard<std::mutex> l(mutex_);lanes_={};near_.clear();seen_={};cursor_=1;cycle_=0;slot_entity_.clear();slot_prop_.clear();logged_=0;refresh();
    }
    // Render thread. The geometry (open-hand finger chains) is rebuilt when the arm binding changes.
    void set_geometry(unsigned side,const std::optional<rig::HandGeometry>& geometry) {
        if(side>1) return;
        std::lock_guard<std::mutex> l(mutex_);lanes_[side].geometry=geometry;lanes_[side].active=false;refresh();
    }
    // active: an empty open hand in gameplay. drawn: the pose the hand is drawn at (collision may have moved it); raw: the tracked
    // pose (its velocity drives the push). The motion is tracked even while inactive, so a hand opening on a prop already has it.
    // held_by: the entity the world cast last held this hand at (0: none); the drawn hand is back from the tracked one by its shift.
    void publish(unsigned side,bool active,const WorldTrackedPose& drawn,const WorldTrackedPose& raw,const std::array<float,5>& curl,std::uintptr_t held_by=0,Vec3 palm_offset={},const HandJointTurns* turns=nullptr) {
        if(side>1) return;
        const double now=seconds();++publishes;drain_log(now);
        std::lock_guard<std::mutex> l(mutex_);auto& lane=lanes_[side];
        bool turns_valid=true;
        if(turns)for(const auto& finger:turns->joints)for(const auto turn:finger)turns_valid=turns_valid && std::isfinite(turn);
        if(!turns_valid || !grab_pose_valid(drawn) || !finite(raw.position) || !finite(palm_offset) || length(palm_offset)>.5) {lane.active=false;lane.motion.reset();++lane.serial;refresh();return;}
        lane.motion.sample(raw.position,now);
        lane.active=active && lane.geometry;
        if(lane.active) {auto palm=drawn;palm.position=palm.position+palm_offset;lane.proxies=turns?hand_proxies(*lane.geometry,palm,*turns):hand_proxies(*lane.geometry,palm,curl);lane.velocity=lane.motion.velocity();lane.lag=raw.position-drawn.position;lane.held_by=held_by;lane.stamp=now;}
        ++lane.serial;refresh();
    }
    void release(unsigned side) {if(side>1) return;drain_log(seconds());std::lock_guard<std::mutex> l(mutex_);lanes_[side].active=false;lanes_[side].motion.reset();++lanes_[side].serial;refresh();}
    void clear() {release(0);release(1);}

    // Physics thread, from the dispatcher callback. `world` is the dispatcher's own; `held` are the entities a hand holds
    // physically. write(world, handle, linear[3], angular[3]) is the velocity setter.
    // `scan` reads everything (native_grab.hpp's ScanReader: its block cache serves the entity scan and the body reads alike).
    template<class Scan,class Write,class Cpu=NoCpu> void service(Scan& scan,std::uintptr_t base,std::uintptr_t world,std::uint64_t now_ms,
            const std::array<std::uintptr_t,2>& held,Write&& write,Cpu cpu=Cpu{}) {
        std::array<Hand,2> hands{};const double now=seconds();std::optional<Vec3> player;
        snapshot(hands,now,true,&player);
        if(!inline_discovery && !latched_.load(std::memory_order_relaxed) && services.load()>=latch_services && discoveries.load()==0) latched_=true;
        const bool scan_here=inline_discovery || latched_.load(std::memory_order_relaxed);
        if(!hands[0].active && !hands[1].active) {
            near_.clear();seen_={};
            std::unique_lock<std::mutex> lock(meet_mutex_,std::try_to_lock);if(lock.owns_lock()) meet_.stamp=0; // nothing is met any more (meet() also checks the lane)
            return;
        }
        if(hands[0].serial==seen_[0] && hands[1].serial==seen_[1]) return; // nothing new since the last service
        seen_={hands[0].serial,hands[1].serial};
        const auto began=std::chrono::steady_clock::now();const double cpu_began=cpu();++services;
        const auto micros=[](std::chrono::steady_clock::duration d) {return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(d).count());};
        const auto game=rig::value<std::uintptr_t>(scan,base+0x39a5290);
        if(!game || !rig::typed(scan,base,*game,0x1c940a8)) return;
        const auto live=rig::value<std::uintptr_t>(scan,*game+0x60410);
        if(!live || *live!=world) return;
        const auto palm_distance=[&](Vec3 p) { // to the nearest active palm
            double best=1e9;for(const auto& h:hands) if(h.active) best=std::min(best,length(h.proxies[0].centre-p));return best;};
        // A new level (or a reload) is a new registry: forget the slot classes and the candidates.
        if(scan_here) {
            if(slot_entity_.size()!=registry_size || game_!=*game || world_!=world) {
                slot_entity_.assign(registry_size,0);slot_prop_.assign(registry_size,0);near_.clear();game_=*game;world_=world;cursor_=1;cycle_=0;
            }
        } else {
            if(service_game_!=*game || service_world_!=world) {near_.clear();service_game_=*game;service_world_=world;}
            std::unique_lock<std::mutex> lock(found_mutex_,std::try_to_lock); // what discover() found since the last service; a busy lock waits for the next one
            if(lock.owns_lock()) {
                for(const auto gone:gone_) near_.erase(std::remove_if(near_.begin(),near_.end(),[&](const Near& n){return n.entity==gone;}),near_.end());
                for(const auto entity:found_) if(near_.size()<near_capacity && std::none_of(near_.begin(),near_.end(),[&](const Near& n){return n.entity==entity;})) near_.push_back({entity});
                found_.clear();gone_.clear();
            }
        }
        std::chrono::steady_clock::duration body_time{},math_time{};
        unsigned writes=0,fulls=0;std::uint64_t write_peak=0;
        std::array<std::uintptr_t,8> pushable{};unsigned pushables=0;std::array<Vec3,2> outward{};std::array<std::uintptr_t,2> outward_entity{}; // what meet() is told
        std::array<bool,2> lag_owned{}; // the entity the cast holds a hand at (or the hand meets) is a prop of ours: that hand's lag is trusted for every prop
        for(unsigned side=0;side<2;++side) lag_owned[side]=hands[side].active && hands[side].held_by && std::any_of(near_.begin(),near_.end(),[&](const Near& n){return n.entity==hands[side].held_by;});
        for(auto it=near_.begin();it!=near_.end();) {
            auto& n=*it;
            if(std::chrono::steady_clock::now()-began>std::chrono::microseconds(service_limit_us)) break; // the rest next service
            if(n.ignore_until>now_ms) {++it;continue;} // refused as a prop: not read at all until it expires
            const auto t0=std::chrono::steady_clock::now();
            // Where it is. A body in reach last service is refreshed from its record (which is also its position); the rest cost
            // one position read.
            bool refreshed=false;Vec3 at{};
            if(n.body && n.in_reach) {
                refreshed=refresh_body(scan,*game,n);
                if(refreshed) at=n.body->pose.position;else {n.body.reset();n.in_reach=false;}
            }
            if(!refreshed) {
                const auto pos=rig::value<std::array<float,3>>(scan,n.entity+0x300);
                if(!pos) {it=near_.erase(it);continue;}
                at={(*pos)[0],(*pos)[1],(*pos)[2]};
            }
            const double d=palm_distance(at);
            if(d>keep_reach) {it=near_.erase(it);continue;}
            ++it;
            if(d>n.reach) {n.in_reach=false;body_time+=std::chrono::steady_clock::now()-t0;continue;}
            const bool want_full=!refreshed || now_ms>=n.next_full; // in reach and not just refreshed, or due: the full read
            const bool tired=fulls>=max_full_reads || (fulls>=1 && std::chrono::steady_clock::now()-began>std::chrono::microseconds(service_limit_us/3)); // a full read is ~45 reads
            if(want_full && tired && !refreshed) {body_time+=std::chrono::steady_clock::now()-t0;continue;} // waits for the next service
            if(want_full && !tired) {
                ++fulls;
                const auto body=native_rigid_body(scan,base,n.entity,true);
                const auto motion=body?native_motion(scan,*body):std::nullopt;
                if(!body || !body->bounds || body->world!=world) {n.ignore_until=now_ms+1000;n.body.reset();n.in_reach=false;body_time+=std::chrono::steady_clock::now()-t0;continue;} // not a loose prop
                if(!motion) {n.body.reset();n.in_reach=false;body_time+=std::chrono::steady_clock::now()-t0;continue;}
                n.body=*body;n.next_full=now_ms+full_period_ms+(n.entity>>4)%256;n.velocity=motion->velocity;
                n.inverse_mass=motion->inverse_mass;n.mass=prop_mass(motion->inverse_mass,*body->bounds);
                n.reach=length(body->bounds->maximum-body->bounds->minimum)*.5+hand_span+.05;++tested;
            }
            n.in_reach=true;n.drag.observe(n.velocity,now); // the velocity found now, against what the last write left it with
            const auto t1=std::chrono::steady_clock::now();body_time+=t1-t0;
            if(n.body->entity==held[0] || n.body->entity==held[1]) continue;
            if(player && near_player(*n.body->bounds,*player)) {++keepouts;continue;} // against the player's body or under his feet: never shoved
            const bool met=n.mass>0 && n.mass<config.heavy_kg && !n.drag.blocked(now); // a loose prop in reach the contact pushes and that gives way
            if(met && pushables<pushable.size()) pushable[pushables++]=n.entity;
            rig::HeldShape shape;
            try {shape=prop_shape(*n.body);} catch(const std::invalid_argument&) {continue;}
            Vec3 velocity=n.velocity,spin{};bool pushed=false;
            for(unsigned side=0;side<2;++side) {
                if(!hands[side].active) continue;
                const auto palm=hands[side].proxies[0].centre;
                if(length(n.body->bounds->closest(palm)-palm)>hand_span) continue;
                ContactInput input;input.engaged=n.engaged_until>now_ms;input.lag=hands[side].lag;input.held=hands[side].held_by && hands[side].held_by==n.entity;if(input.held) ++cast_held;input.lag_known=lag_owned[side];input.friction=n.drag.compensation();
                input.trim=n.drag.trim();input.age=input.engaged?now-n.engaged_at:0.;
                input.blocked=n.drag.blocked(now);input.stalled=n.drag.stalled(now);input.basis=n.body->pose.basis;
                const auto result=hand_contact(hands[side].proxies,shape,hands[side].velocity,velocity,n.mass,config,input);
                if(met && length(result.outward)>length(outward[side])) {outward[side]=result.outward;outward_entity[side]=n.entity;}
                if(result.touching) ++touches;
                if(result.driving) {if(n.engaged_until<=now_ms) n.engaged_at=now;n.engaged_until=now_ms+config.engage_ms;}
                n.drag.steer(result,now);
                if(result.push || result.turn) { // a velocity to write, or only the uprighting spin (the velocity stays)
                    log_row(now_ms,side,*n.body,n.inverse_mass,n.mass,result,length(hands[side].velocity),length(velocity),input,!result.push); // every write, spin-only ones too
                    velocity=result.velocity;spin=result.spin;pushed=true;
                }
            }
            if(pushed && writes<max_writes) {
                const std::array<float,3> linear{static_cast<float>(velocity.x),static_cast<float>(velocity.y),static_cast<float>(velocity.z)};
                const std::array<float,3> turn{static_cast<float>(spin.x),static_cast<float>(spin.y),static_cast<float>(spin.z)};
                const auto w0=std::chrono::steady_clock::now();
                write(world,n.body->handle,linear,turn);++writes;++pushes;n.velocity=velocity;n.drag.wrote(velocity,now);
                const auto spent=micros(std::chrono::steady_clock::now()-w0);write_us+=spent;write_peak=std::max(write_peak,spent);
                if(spent>50) ++slow_writes;
                auto top=write_max_us.load();while(spent>top && !write_max_us.compare_exchange_weak(top,spent)) {}
            }
            math_time+=std::chrono::steady_clock::now()-t1;
        }
        {   // for the render thread: the props the contact pushes and the way out of the one each hand overlaps most (a busy lock waits for the next service)
            std::unique_lock<std::mutex> lock(meet_mutex_,std::try_to_lock);
            if(lock.owns_lock()) {
                meet_.pushable=pushable;meet_.count=pushables;meet_.stamp=now;
                for(unsigned side=0;side<2;++side) if(hands[side].active) {meet_.outward[side]=outward[side];meet_.entity[side]=outward_entity[side];meet_.side_stamp[side]=now;}
            }
        }
        // The registry scan comes last (inline mode; in the game discover() does it elsewhere): the pushes (the time-critical part) are never kept
        // waiting by it, and it gets what is left of the service, at most scan_limit_us.
        const auto scan_t0=std::chrono::steady_clock::now();
        if(scan_here) scan_registry(scan,base,*game,hands,std::min(began+std::chrono::microseconds(service_limit_us),scan_t0+std::chrono::microseconds(scan_limit_us)),
            [&](std::uintptr_t entity) {
                if(std::any_of(near_.begin(),near_.end(),[&](const Near& n){return n.entity==entity;})) return;
                if(near_.size()<near_capacity) near_.push_back({entity});},
            [&](std::uintptr_t gone) { // a slot's entity changed or went: it is no longer a candidate
                if(gone) near_.erase(std::remove_if(near_.begin(),near_.end(),[&](const Near& n){return n.entity==gone;}),near_.end());});
        const auto scan_time=std::chrono::steady_clock::now()-scan_t0;
        const std::uint64_t query=query_peak(scan,0); // the slowest address-space query this service's reader made (ScanReader), 0 for a reader that does not time them
        const auto end=std::chrono::steady_clock::now();
        const auto us=[](std::chrono::steady_clock::duration d) {return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(d).count());};
        scan_us+=us(scan_time);body_us+=us(body_time);math_us+=us(math_time);
        const auto total=us(end-began);
        cost_us+=total;auto peak=cost_max_us.load();while(total>peak && !cost_max_us.compare_exchange_weak(peak,total)) {}
        if(total>100) {
            ++slow_services;
            const double cpu_used=cpu_began>0?cpu()-cpu_began:-1.; // the thread's own time: far under the wall time = the OS ran something else meanwhile
            if(cpu_used>=0 && cpu_used*2<static_cast<double>(total)) ++preempted;
            log_slow(now_ms,total,cpu_used,us(scan_time),us(body_time),us(math_time),write_peak,query,fulls,writes);
        }
    }
    // Render thread, once a frame after the hands are published (native_grab.hpp discover_contacts): one slice of the registry scan, with a reader
    // of its own. The entities near a palm go to the service through found_; slots that changed go to gone_.
    template<class Scan> void discover(Scan& scan,std::uintptr_t base) {
        if(inline_discovery || latched_.load(std::memory_order_relaxed) || !wanted()) return;
        std::array<Hand,2> hands{};snapshot(hands,seconds(),false);
        if(!hands[0].active && !hands[1].active) return;
        const auto began=std::chrono::steady_clock::now();
        const auto game=rig::value<std::uintptr_t>(scan,base+0x39a5290);
        if(!game || !rig::typed(scan,base,*game,0x1c940a8)) return;
        const auto live=rig::value<std::uintptr_t>(scan,*game+0x60410);
        if(!live || !*live) return;
        if(slot_entity_.size()!=registry_size || game_!=*game || world_!=*live) { // a new level (or a reload): a new registry
            slot_entity_.assign(registry_size,0);slot_prop_.assign(registry_size,0);game_=*game;world_=*live;cursor_=1;cycle_=0;
            std::lock_guard<std::mutex> lock(found_mutex_);found_.clear();gone_.clear();
        }
        scan_registry(scan,base,*game,hands,began+std::chrono::microseconds(scan_limit_us),
            [&](std::uintptr_t entity) {
                std::lock_guard<std::mutex> lock(found_mutex_);
                if(found_.size()<2*near_capacity && std::find(found_.begin(),found_.end(),entity)==found_.end()) found_.push_back(entity);},
            [&](std::uintptr_t gone) {
                if(!gone) return;
                std::lock_guard<std::mutex> lock(found_mutex_);
                if(gone_.size()<4*near_capacity) gone_.push_back(gone);});
        const auto spent=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-began).count());
        ++discoveries;discover_us+=spent;auto top=discover_max_us.load();while(spent>top && !discover_max_us.compare_exchange_weak(top,spent)) {}
    }
private:
    template<class T> static auto query_peak(T& reader,int)->decltype(static_cast<std::uint64_t>(reader.take_slowest())) {return static_cast<std::uint64_t>(reader.take_slowest());}
    template<class T> static std::uint64_t query_peak(T&,long) {return 0;}
    struct Hand {bool active{};HandProxies proxies{};Vec3 velocity{},lag{};std::uintptr_t held_by{};std::uint64_t serial{};};
    static bool near_player(const NativeGrabBounds& b,Vec3 eye) { // the prop's box against the player's keep-out cylinder
        const double dx=std::max({b.minimum.x-eye.x,0.,eye.x-b.maximum.x}),dy=std::max({b.minimum.y-eye.y,0.,eye.y-b.maximum.y});
        return std::hypot(dx,dy)<player_radius && b.maximum.z>eye.z-player_below && b.minimum.z<eye.z+player_above;
    }
    // The published hands as of now (an inactive or stale lane is inactive); count: the service tallies the cast entities it was given.
    void snapshot(std::array<Hand,2>& hands,double now,bool count,std::optional<Vec3>* player=nullptr) {
        std::lock_guard<std::mutex> l(mutex_);
        if(player && now-player_stamp_<=.25) *player=player_eye_;
        for(unsigned side=0;side<2;++side) {
            const auto& lane=lanes_[side];
            hands[side].serial=lane.serial;hands[side].active=lane.active && now-lane.stamp<=fresh_seconds;
            if(hands[side].active) {hands[side].proxies=lane.proxies;hands[side].velocity=lane.velocity;hands[side].lag=lane.lag;hands[side].held_by=lane.held_by;if(count && lane.held_by) ++cast_seen;}
        }
    }
    // Slices of the registry: entities near a palm are add()ed (the caller dedups and caps), slots whose entity changed are retire()d. One slice a call
    // (the whole registry in ~16 calls, ~32 at rest); four while a hand is fast (>1 m/s), so a prop in the path of a swat is known before the hand arrives.
    template<class Scan,class Add,class Retire> void scan_registry(Scan& scan,std::uintptr_t base,std::uintptr_t game,const std::array<Hand,2>& hands,
            std::chrono::steady_clock::time_point deadline,Add&& add,Retire&& retire) {
        const auto palm_distance=[&](Vec3 p) { // to the nearest active palm
            double best=1e9;for(const auto& h:hands) if(h.active) best=std::min(best,length(h.proxies[0].centre-p));return best;};
        const auto faster=[&](double speed) {return (hands[0].active && length(hands[0].velocity)>speed) || (hands[1].active && length(hands[1].velocity)>speed);};
        const bool swinging=faster(1.);
        // A resting hand scans every other call (a prop moving into reach has ~0.5 m to cover): the first pass over a level and
        // any hand faster than 0.4 m/s scan every call.
        const bool scanning=cycle_==0 || faster(.4) || (++scan_tick_&1);
        unsigned budget=scan_budget; // reads left this call: a class read is 1, a position read 1
        for(unsigned pass=0;pass<(scanning?(swinging?4u:1u):0u) && budget>=2;++pass) {
            std::array<std::uintptr_t,slice_size> slice;
            const unsigned count=std::min<unsigned>(slice_size,registry_size-cursor_);
            unsigned done=count; // registry entries finished; fewer when the budget ends the slice
            if(scan(game+0x3130b0+static_cast<std::uintptr_t>(cursor_)*8,slice.data(),count*sizeof(std::uintptr_t))) {
                for(done=0;done<count && budget>=2;++done) {
                    if((done&3)==3 && std::chrono::steady_clock::now()>deadline) {budget=0;break;} // over its time: the rest next call
                    const unsigned slot=cursor_+done;const auto entity=slice[done];
                    if(!rig::pointer(entity)) {retire(slot_entity_[slot]);slot_entity_[slot]=0;slot_prop_[slot]=0;continue;}
                    if(slot_entity_[slot]!=entity || ((cycle_+slot)%recheck_cycles)==0) {
                        if(slot_entity_[slot]!=entity) retire(slot_entity_[slot]);
                        --budget;slot_entity_[slot]=entity;slot_prop_[slot]=prop_class(scan,base,entity);
                    }
                    if(!slot_prop_[slot]) continue;
                    --budget;
                    const auto pos=rig::value<std::array<float,3>>(scan,entity+0x300);
                    if(!pos || palm_distance({(*pos)[0],(*pos)[1],(*pos)[2]})>scan_reach) continue;
                    add(entity);
                }
            }
            cursor_+=done;if(cursor_>=registry_size) {cursor_=1;++cycle_;}
        }
    }
    struct Near {
        std::uintptr_t entity{};double reach=.75;std::uint64_t ignore_until{},next_full{},engaged_until{};double engaged_at{}; // engaged_at: seconds, when this contact began
        bool in_reach{};                      // within reach at the last service: refreshed from its record, not its position
        std::optional<NativeRigidBody> body;  // the last full read; pose and bounds follow the record
        Vec3 velocity{};double mass{};std::uint16_t inverse_mass{};
        PropDrag drag;                        // the friction the engine takes off it between writes; blocked / stalled
    };
    // The light refresh of a cached body: the same lifetime facts native_rigid_body checks (registry slot, record handle fields and
    // motion type, component identity, no carry parent or lease), 8 reads. False: read it in full.
    template<class Read> static bool refresh_body(Read& read,std::uintptr_t game,Near& n) {
        auto& b=*n.body;
        const auto slot=rig::value<std::uintptr_t>(read,game+0x3130b0+static_cast<std::uintptr_t>(b.entity_id>>16)*8);
        const auto pool=rig::value<std::uintptr_t>(read,b.world+0x28),array=rig::value<std::uintptr_t>(read,b.world+0x120);
        if(!slot || *slot!=b.entity || !pool || !array || !rig::pointer(*pool) || !rig::pointer(*array) || *pool+static_cast<std::uintptr_t>(b.handle&0xffffff)*0xb0!=b.record) return false;
        std::array<unsigned char,0xb0> record;
        if(!read(b.record,record.data(),record.size())) return false;
        rig::Transform t;std::uint32_t motion,flags,first,second;
        std::memcpy(&t,record.data(),sizeof t);std::memcpy(&motion,record.data()+0x40,4);std::memcpy(&flags,record.data()+0x44,4);
        std::memcpy(&first,record.data()+0x70,4);std::memcpy(&second,record.data()+0x74,4);
        if((flags&7)!=2 || !motion || motion>=0x100000 || first!=b.handle || second!=b.handle || !rig::valid(t)) return false;
        const auto identity=rig::value<rig::Identity>(read,b.component+0x18);
        std::array<unsigned char,12> carry; // component +0xc8 native parent, +0xd2 temporary lease
        if(!identity || *identity!=b.identity || !read(b.component+0xc8,carry.data(),carry.size())) return false;
        std::uintptr_t parent;std::memcpy(&parent,carry.data(),8);
        if(parent || carry[10]) return false;
        const auto velocity=rig::value<std::array<float,3>>(read,*array+static_cast<std::uintptr_t>(motion)*0x80+0x40);
        if(!velocity) return false;
        const Vec3 v{(*velocity)[0],(*velocity)[1],(*velocity)[2]};
        if(!finite(v) || length(v)>100) return false;
        const auto& m=t.f;
        const WorldTrackedPose pose{{m[12],m[13],m[14]},{{{m[0],m[1],m[2]},{m[4],m[5],m[6]},{m[8],m[9],m[10]}}}};
        // The drawn bounds (native_rigid_body's checks): refused when stale or not a box, the full read decides what that means.
        const auto box=rig::value<std::array<float,8>>(read,b.model+0x1840);
        if(!box) return false;
        const NativeGrabBounds bounds{{(*box)[0],(*box)[1],(*box)[2]},{(*box)[4],(*box)[5],(*box)[6]}};
        if(!finite(bounds.minimum) || !finite(bounds.maximum) || bounds.minimum.x>bounds.maximum.x || bounds.minimum.y>bounds.maximum.y ||
           bounds.minimum.z>bounds.maximum.z || length(bounds.maximum-bounds.minimum)<=.001 || length(bounds.maximum-bounds.minimum)>3 ||
           length(bounds.closest(pose.position)-pose.position)>.5) return false;
        b.pose=pose;b.flags=flags;b.bounds=bounds;n.velocity=v;
        return true;
    }
    // One CSV row per push (the first 600), queued for the render thread: file I/O costs the physics thread 100+ us.
    void log_row(std::uint64_t now_ms,unsigned side,const NativeRigidBody& body,std::uint16_t inverse_mass,double mass,const HandContact& r,double hand_speed,double before,const ContactInput& in,bool turn_only) {
        if(!log_on_.load(std::memory_order_relaxed) || logged_>=600) return;
        char row[320];
        const double lag=length(in.lag); // the size of the cast's shift
        const int len=std::snprintf(row,sizeof row,"%llu,%u,%u,%u,%04x,%g,%g,%g,%g,%g,%g,%g,%g,%g,%g,%g,%g,%g,%d,%d\n",static_cast<unsigned long long>(now_ms),side,body.entity_id,body.handle,
            inverse_mass,mass,r.depth,hand_speed,before,length(r.velocity),r.normal.x,r.normal.y,r.normal.z,r.has_gap?r.gap:0.,r.has_gap?r.error:0.,in.trim,in.friction,lag,in.held?1:0,turn_only?1:0);
        if(len<=0) return;
        ++logged_;std::lock_guard<std::mutex> l(log_mutex_);pending_.append(row,static_cast<std::size_t>(std::min(len,static_cast<int>(sizeof row)-1)));
    }
    // One CSV row per slow service (the first 200), queued like the others.
    void log_slow(std::uint64_t now_ms,std::uint64_t total,double cpu_used,std::uint64_t scan,std::uint64_t body,std::uint64_t math,std::uint64_t write_peak,std::uint64_t query,unsigned fulls,unsigned writes) {
        if(!log_on_.load(std::memory_order_relaxed) || slow_logged_>=200) return;
        char row[200];
        const int len=std::snprintf(row,sizeof row,"%llu,%llu,%.0f,%llu,%llu,%llu,%llu,%llu,%u,%u,%u,%llu\n",static_cast<unsigned long long>(now_ms),static_cast<unsigned long long>(total),cpu_used,
            static_cast<unsigned long long>(scan),static_cast<unsigned long long>(body),static_cast<unsigned long long>(math),static_cast<unsigned long long>(write_peak),static_cast<unsigned long long>(query),fulls,writes,
            static_cast<unsigned>(near_.size()),static_cast<unsigned long long>(tested.load()));
        if(len<=0) return;
        ++slow_logged_;std::lock_guard<std::mutex> l(log_mutex_);slow_pending_.append(row,static_cast<std::size_t>(std::min(len,static_cast<int>(sizeof row)-1)));
    }
    void drain_log(double now) { // render thread
        if(now-log_drained_<.25) return;
        log_drained_=now;std::string rows,slow;
        {std::lock_guard<std::mutex> l(log_mutex_);if(pending_.empty() && slow_pending_.empty()) return;rows.swap(pending_);slow.swap(slow_pending_);}
        if(log_.is_open() && !rows.empty()) {log_<<rows;log_.flush();}
        if(slow_.is_open() && !slow.empty()) {slow_<<slow;slow_.flush();}
    }
    // The three entity classes native_rigid_body accepts (arkMovable among them); anything else can never be pushed.
    template<class Read> static bool prop_class(Read& read,std::uintptr_t base,std::uintptr_t entity) {
        const auto vtable=rig::value<std::uintptr_t>(read,entity);
        return vtable && (*vtable==base+0x1c886a8 || *vtable==base+0x1c87d58 || *vtable==base+0x1c5de18);
    }
    struct Lane {std::optional<rig::HandGeometry> geometry;HandMotion motion;HandProxies proxies{};Vec3 velocity{},lag{};std::uintptr_t held_by{};bool active{};std::uint64_t serial{};double stamp{};};
    static double seconds() {return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();}
    void refresh() {wanted_.store(lanes_[0].active || lanes_[1].active,std::memory_order_relaxed);} // under mutex_
    std::mutex mutex_;std::array<Lane,2> lanes_{};std::atomic<bool> wanted_{};
    std::vector<Near> near_;std::array<std::uint64_t,2> seen_{};unsigned cursor_{1},cycle_{},scan_tick_{};
    std::vector<std::uintptr_t> slot_entity_;std::vector<std::uint8_t> slot_prop_;std::uintptr_t game_{},world_{}; // slot -> entity pointer and class (the scanner's)
    struct Meet {std::array<std::uintptr_t,8> pushable{};unsigned count{};double stamp{};std::array<std::uintptr_t,2> entity{};std::array<Vec3,2> outward{};std::array<double,2> side_stamp{};};
    Vec3 player_eye_{};double player_stamp_{-1e9}; // under mutex_
    std::mutex meet_mutex_;Meet meet_;std::array<bool,2> meet_latched_{}; // service -> meet(); latched: pinned, the cast holds the hand off the prop
    std::atomic<bool> latched_{};std::mutex found_mutex_;std::vector<std::uintptr_t> found_,gone_;std::uintptr_t service_game_{},service_world_{}; // discover() -> service(); the service's level
    std::ofstream log_,slow_;std::atomic<bool> log_on_{};unsigned logged_{},slow_logged_{};std::mutex log_mutex_;std::string pending_,slow_pending_;double log_drained_{};
};
} // namespace d2vr
