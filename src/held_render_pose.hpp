#pragma once
// v1249 (user, headset v1246: "Loose objects: head, plates, ash trays should be perfectly rendered when walking, same as bottles now"): the ENGINE's
// render pose of a physically held prop is the hold's goal, not the motor-driven body that trails it 20-200 mm (hold_sync motor_lag). Every pass the
// renderer composes from it (shadow maps, culling bounds, light/probe choice, the eye draws the placement then refines) is at the hand by construction.
// Engine path (static RE on Dishonored2.exe, 2026-11-12):
//  - arkCpntRigidBody / arkCpntRigidBodyThrowable POST_PHYSICS (component stage 6 = factory vtable slot 8: 0x951c80, 0x951cf0 -> 0x4c9560) -> 0x80b9d0 ->
//    0x80b7c0: reads the hknp body transform (pool record) and the motion's velocities and calls 0x754ea0(rigid+0xe8, origin, axis columns, linear,
//    angular, tag, 1) at 0x80b9aa.
//  - 0x754ea0 stores them as the reference frame's pending frame (0x18e050: rf+0x30 origin, +0x3c axis, +0x60/+0x6c velocities) and, when they differ
//    from the entity's (+0x300/+0x30c/+0x33c/+0x348), queues the frame (0x7521f0 -> 0x8a6030).
//  - The post-physics attach job 0x755030 -> 0x753c10 then writes entity +0x300/+0x30c from it, the render model frame (model+0x1860 origin, +0x186c axis,
//    +0x1890 scale), presents the model (vtbl+0x28, 1) and moves bound children (0x53d6a0). The render (eye + shadow passes, bounds +0x1840) follows.
//  - Nothing on that path writes Havok: the body's PRE_PHYSICS (0x80b9e0) applies only queued velocity requests, so the entity frame never feeds back.
//  - The game's own carry (HoldObject) BINDS the bottle to the player (bind target 0x7506d0 drives a keyframed body toward master * offset; the model
//    follows the bind): that is why a carried bottle is rigid with the hand in every pass.
// Here: a hook on 0x754ea0, acting only on the call from 0x80b7c0 for an entity a prop hold publishes, passes the hold's goal (PhysicalGrab::last_goal,
// the motor's led target of this physics step) instead of the body's transform; the velocities stay the body's. The body stays motor-driven (collisions,
// feel). When the hold ends the render frame returns to the body over blend_s. Ragdolls (severed parts, corpses) render from their bones (0x736820 ->
// 0x76d640 writes the skinned model frame itself): untouched, counted as refusal "ragdoll". build/no-held-render-pose: not installed (v1248 behaviour).
// v1253 ragdolls (user: "head, plates, ash trays"; a severed part is a skinned clone of the cut ragdoll, its held body a root or standalone body).
// Static RE of how a ragdoll's draws get their pose: arkCpntPhysicsSystemEvolved POST_PHYSICS 0x736820 -> 0x76d640 walks its bodies; per dynamic body
// (vtbl+0x148 == 2) bone world = body record transform * the bone's body offset (0xae3080), split by 0x9b900 into rot (3 basis vectors) + pos and QUEUED
// as a WORLD joint request (0x53fbe0(anim, 0x40, joint, mode 2, 0, &pos, &rot) at 0x76db00); the first body gives the model frame (written at
// 0x76da05) and the same origin/axis go to the reference frame (0x754ea0 at 0x76dc12, whenever the system has one: comp+0xf0). The animation
// component's POST_PHYSICS 0x951570 -> 0x550830 sets its pose frame from the reference frame's frame OF THIS TICK (0x750fe0/0x750f70: rf+0x30/+0x3c
// while rf+0x84 is this frame, i.e. 0x754ea0's args) and resolves the requests (0x53bcd0 -> mode 2: 0x9d3f0 + 0x9e2f0 against pose+0x60, the
// frame's matrix): joints are stored MODEL-relative, inverse(frame) * bone world. The upload 0x541270 takes model+0x1860/+0x186c (written from the
// entity frame by 0x753c10 -> 0x541420/0x541390) and draws model frame * palette. So the palette is model-relative and built from the same tick's frame:
// shifting only the frame would re-derive the palette from it and leave the vertices where the bodies are. The rigid move D = goal * inverse(held body)
// is applied to BOTH: every bone request from 0x76db00 and the frame passed at 0x76dc12 (and written to the model frame right after, as 0x76da05 did).
// Palette = inverse(D F) D B = inverse(F) B (unchanged), drawn at D F: the whole clone, its collapsed bones and bound children move rigidly by D in every
// pass. A corpse held by one bone (opt-in build/held-render-pose-corpse) gets a TRANSLATION only (goal - held bone, capped at corpse_cap_m): its other
// bones hang. build/no-held-render-pose-severed: ragdolls untouched (v1252).
#include <windows.h>
#include <intrin.h>
#include <MinHook.h>
#include "physical_grab.hpp"
#include "grip_anchor.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <optional>
#include <string>

namespace d2vr {
// The release blend: `body` turned by k*rot (world rotation vector) and moved by k*pos, k = 1 - w. w=0: body*offset (the last render pose); w=1: body.
inline WorldTrackedPose render_blend(const WorldTrackedPose& body,Vec3 pos,Vec3 rot,double w) noexcept {
    const double k=1-std::clamp(w,0.,1.);WorldTrackedPose out=body;out.position=body.position+pos*k;
    const double a=length(rot)*k;if(!(a>1e-12)) return out;
    const Vec3 u=rot*(1/length(rot));const double c=std::cos(a),s=std::sin(a);
    for(auto& b:out.basis) b=b*c+cross(u,b)*s+u*(dot(u,b)*(1-c));
    return out;
}
// A rigid move of a ragdoll: whatever sat at `from` (the held body this tick) is drawn at `to`. Basis vectors are world columns (WorldTrackedPose).
struct HoldDelta {
    WorldTrackedPose from{},to{};
    Vec3 turn(Vec3 v) const noexcept {return to.basis[0]*dot(from.basis[0],v)+to.basis[1]*dot(from.basis[1],v)+to.basis[2]*dot(from.basis[2],v);}
    Vec3 point(Vec3 p) const noexcept {return to.position+turn(p-from.position);}
    void apply(float* pos,float* rot) const noexcept {   // the engine's bone/frame layout: pos[3], rot = 3 basis vectors of 3
        const Vec3 p=point({pos[0],pos[1],pos[2]});pos[0]=float(p.x);pos[1]=float(p.y);pos[2]=float(p.z);
        for(unsigned i=0;i<3;++i) {const Vec3 b=turn({rot[i*3],rot[i*3+1],rot[i*3+2]});rot[i*3]=float(b.x);rot[i*3+1]=float(b.y);rot[i*3+2]=float(b.z);}
    }
};
// A corpse held by one bone: the body drawn moved by goal - bone, no turn, at most cap_m.
inline WorldTrackedPose translated_goal(const WorldTrackedPose& bone,const WorldTrackedPose& goal,double cap_m) noexcept {
    WorldTrackedPose out=bone;Vec3 d=goal.position-bone.position;const double n=length(d);if(n>cap_m) d=d*(cap_m/n);out.position=bone.position+d;return out;
}
// v1258: where the player's rig stands for a drawn pose: the anchor root position and (camera heading - head follow), item_advance's reference.
struct HeldCarry {Vec3 root{};double heading{};};
// A world pose drawn with `from` as it is drawn with `to`: turned about the root by the heading change, moved with the root (FullBodyDriver::item_advance).
inline WorldTrackedPose held_carried(const WorldTrackedPose& drawn,const HeldCarry& from,const HeldCarry& to) noexcept {
    const double a=to.heading-from.heading,c=std::cos(a),s=std::sin(a);
    const auto turn=[&](Vec3 v) {return Vec3{c*v.x-s*v.y,s*v.x+c*v.y,v.z};};
    WorldTrackedPose out{};out.position=to.root+turn(drawn.position-from.root);
    for(unsigned i=0;i<3;++i) out.basis[i]=turn(drawn.basis[i]);
    return out;
}
// One held (or just released) entity: its published goal and what the engine was last given for it.
// v1258 (headset v1256: a held prop drawn at two poses; 57 % of renders had no draw placed and showed the ENGINE pose, which sat 35-100 mm
// from the drawn hold - hold_sync engine_gap p50/p90): the motor goal is built from the motor's own hand sample and lead, not from the hand
// the render draws, and its lead does not step with the player's root the way the drawn hand does (one root step per game tick). Each render
// the publisher notes the drawn hold as a body pose with the rig reference its image was placed with (note_drawn). The engine is then given the
// drawn hold carried to the rig of NOW (held_carried: the weapons' item_advance, v835) - the pose the next render will draw, whatever the tick
// phase. Without a rig reading, the drawn hold carried by the goal's own motion (goal * X, X = inverse(goal) * drawn); without a note, the goal.
// The physics body is untouched (still motor-driven). drawn_hold = false (build/legacy-held-render-goal): the bare goal (v1256).
struct HeldRenderSlot {
    static constexpr double fresh_s=.1,blend_s=.15,drawn_max_m=.25,carry_max_m=1.;
    inline static bool drawn_hold=true;
    enum Given:unsigned {given_goal,given_relative,given_carried};
    std::uintptr_t entity{};WorldTrackedPose goal{};double at_s{};bool held{};
    unsigned mode{};std::uintptr_t world{};std::uint32_t handle{};HoldDelta delta{};double delta_s{};bool delta_ok{},bones_seen{},frame_seen{};   // v1253 ragdolls
    WorldTrackedPose written{};bool has_written{};double released_s{};bool offset_set{};Vec3 off_pos{},off_rot{};
    WorldTrackedPose drawn{},drawn_rel{};HeldCarry drawn_carry{};double drawn_s{};bool drawn_ok{},rel_ok{},carry_ok{};unsigned used{};   // v1258: the note; what the last decide gave
    // v1266 (headset v1264: the held prop "renders once and then snaps to a new point" while walking/running; engine_gap walk p90 <= 35 mm, run <= 100).
    // The engine is given the drawn hold carried exactly as item_advance carries a held weapon (same rig reads: held_rig_now = advance_attachment's
    // body-model root and camera heading - head follow; same move: about the root by the heading change, with the root's translation), with ONE fixed
    // difference: the prop's sync runs in its POST_PHYSICS stage, BEFORE this game frame's post-physics attach job writes the body model frame, so its
    // root reading is the PREVIOUS frame's root (the weapon's attachment composition already sees this frame's). The heading is this frame's (the
    // turn is applied in the physics step). So this frame's root = the reading + the step the root made between the previous two frames (one game frame
    // per render: the motor writes once per render, held_render_pose.published ~= renders). No comparison with a noted rig, no velocity estimate: v1262
    // guessed "stale or current" against the note and misfired on every turning frame and late note (carried_rig_read 27 %), each one frame of root
    // motion behind (3 m/s: 33 mm, 7 m/s: 77 mm). A second sync inside one frame (same reading within frame_s) keeps the frame's step; a reading that
    // stays put longer is a player standing still (no step). A jump beyond step_max_m is a teleport (the Timepiece's 600 m, a load): no step.
    // The step is walked in the player's heading: it turns with this frame's heading change (the turn is applied before the physics step that moves
    // the root), so a stick turn or a snap turn while walking moves this frame's step with it.
    static constexpr double step_max_m=.25,frame_s=.02;
    HeldCarry seen{};Vec3 step{};double step_heading{},seen_s{};bool have_seen{},stepped{};
    HeldCarry rig_of_frame(const HeldCarry& read,double now) noexcept {
        HeldCarry out=read;stepped=false;
        if(have_seen && length(read.root-seen.root)>1e-6) {   // a new frame's root: the step since the previous frame's, walked in that frame's heading
            const Vec3 d=read.root-seen.root;step=now>seen_s && now-seen_s<.05 && length(d)<step_max_m?d:Vec3{};step_heading=seen.heading;seen=read;seen_s=now;
        } else if(have_seen) {if(!(now-seen_s<frame_s)) step={};else seen.heading=read.heading;}   // the same frame's second sync keeps the step; standing still has none
        else {seen=read;seen_s=now;have_seen=true;step={};}
        if(length(step)>1e-6) {
            const double a=std::remainder(read.heading-step_heading,6.283185307179586),c=std::cos(a),sn=std::sin(a);
            out.root=read.root+Vec3{c*step.x-sn*step.y,sn*step.x+c*step.y,step.z};stepped=true;
        }
        return out;
    }
    static constexpr unsigned ring=4;std::array<WorldTrackedPose,ring> recent{};unsigned recent_n{},recent_next{};   // v1258: the last poses given (newest last)
    std::array<WorldTrackedPose,ring> recent_frame{};std::array<bool,ring> recent_frame_ok{};   // v1266: a ragdoll's model frame written with that given (frame site)
    void note_frame(const WorldTrackedPose& f) noexcept {if(recent_n) {const unsigned i=(recent_next+ring-1)%ring;recent_frame[i]=f;recent_frame_ok[i]=true;}}
    void publish(std::uintptr_t e,const WorldTrackedPose& g,double now) noexcept {
        if(e!=entity) *this={};
        entity=e;goal=g;at_s=now;held=true;offset_set=false;
    }
    // v1262: a release forgets everything noted for this hold (the release blend keeps `written`), so a re-grab of the SAME entity starts like a new one
    // (publish() resets the slot only on an entity change): no old drawn hold or X, no stale given ring for the publisher's K, no old rig history.
    void end(double now) noexcept {
        if(!held) return;
        held=false;released_s=now;offset_set=false;
        drawn_ok=rel_ok=carry_ok=false;note_dt=0;recent_n=recent_next=0;have_seen=stepped=false;step={};
    }
    // v1262 (review): how old a drawn note may be and still be carried by the rig alone. Notes arrive once per render (the publisher at each eye-0
    // begin). The engine's sync runs on the game thread, which may be one frame ahead of the render thread (Setup N is drawn by Begin N+1), so a
    // steady-state sync finds the newest note up to two render intervals old; 2.5 intervals takes the render-time jitter. Anything older means a
    // render or its note was missed (a hitch, a render without an image): the hand's own motion since is unknown to a rig-only carry (a fast hand
    // moves 10-20 cm in 100 ms), while the motor's goal keeps following the hand's samples, so goal * X takes over (until fresh_s), then the goal.
    // The interval is the one measured between the last two notes (whatever the headset's rate); status held_render_pose.drawn_hold.note_age_ms.
    static constexpr double carry_floor_s=.004,carry_cap_s=.04,carry_default_s=.03;
    double note_dt{};
    double carry_age() const noexcept {return note_dt>0 && note_dt<.05?std::clamp(2.5*note_dt,carry_floor_s,carry_cap_s):carry_default_s;}
    // v1258: the hold as the render drew it (a body pose: the drawn model frame * inverse of the body->model map) at `now` (s), and the rig reference
    // its image was placed with (null: none, only goal * X is possible).
    bool note_drawn(const WorldTrackedPose& pose,double now,const HeldCarry* carry=nullptr) noexcept {
        if(!held || !grab_pose_valid(pose) || !grab_pose_valid(goal)) return false;
        const auto x=grip_compose(grip_inverse(goal),pose);
        rel_ok=length(x.position)<=drawn_max_m;if(rel_ok) drawn_rel=x;   // far from the goal (a snap the goal has not caught up with, a teleport): no goal * X
        note_dt=drawn_ok && now>drawn_s?now-drawn_s:0.;
        drawn=pose;drawn_s=now;drawn_ok=true;carry_ok=carry && finite(carry->root) && std::isfinite(carry->heading);if(carry_ok) drawn_carry=*carry;
        return true;
    }
    // The pose the engine is given for this entity whose body is at `body` at `now` (s); `rig` = the player's rig now (null: unknown).
    // false: the body's own (and a finished blend frees the slot).
    bool decide(const WorldTrackedPose& body,double now,WorldTrackedPose& out,bool& blending,const HeldCarry* rig=nullptr) noexcept {
        blending=false;used=given_goal;
        if(held && !(now-at_s<=fresh_s)) end(at_s+fresh_s);   // publications stopped without an end (a stalled pass): blend out from then
        if(held) {
            out=goal;
            const bool have_rig=rig && finite(rig->root) && std::isfinite(rig->heading);
            const HeldCarry tick_rig=have_rig?rig_of_frame(*rig,now):HeldCarry{};   // v1266: the rig history advances at every sync, carried or not
            const double age=now-drawn_s;
            if(drawn_hold && drawn_ok && age<=fresh_s) {
                if(rel_ok) if(const auto d=grip_compose(goal,drawn_rel);grab_pose_valid(d)) {out=d;used=given_relative;}
                if(carry_ok && have_rig && age<=carry_age()) if(const auto d=held_carried(drawn,drawn_carry,tick_rig);
                    grab_pose_valid(d) && length(d.position-drawn.position)<=carry_max_m) {out=d;used=given_carried;}   // a teleport / load is not motion
            }
            written=out;has_written=true;recent[recent_next]=out;recent_frame_ok[recent_next]=false;recent_next=(recent_next+1)%ring;if(recent_n<ring) ++recent_n;return true;
        }
        if(!has_written || !(now>=released_s) || !(now-released_s<blend_s)) {*this={};return false;}
        if(!offset_set) {off_pos=written.position-body.position;off_rot=grab_rotation_error(body,written);offset_set=true;}
        out=render_blend(body,off_pos,off_rot,(now-released_s)/blend_s);written=out;blending=true;return true;
    }
};
class HeldRenderPose {
public:
    enum Refusal : unsigned {ragdoll,too_far,invalid,two_hands,unread,refusals};
    enum Mode : unsigned {prop,severed,corpse};   // severed: the clone moves rigidly with its held body; corpse: translated by the held bone's lag
    static constexpr double corpse_cap_m=.6;
    inline static bool ragdolls=true,corpses=false;   // build/no-held-render-pose-severed, build/held-render-pose-corpse (read at install)
    inline static constexpr std::uintptr_t joint_rva=0x53fbe0,joint_site_rva=0x76db05,frame_site_rva=0x76dc17;   // the ragdoll sync's bone request / frame calls return here
    inline static constexpr std::array<unsigned char,16> joint_code{0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x74,0x24,0x20,0x66,0x44,0x89,0x44,0x24,0x18};
    inline static constexpr std::array<unsigned char,5> joint_site_code{0xe8,0xdb,0x20,0xdd,0xff},frame_site_code{0xe8,0x89,0x72,0xfe,0xff};
    static constexpr double max_goal_m=1.6;   // the motor lets a prop go at 1.5 m of lag (PhysicalGrab::corpse_motor)
    inline static constexpr std::uintptr_t rva=0x754ea0,site_rva=0x80b9af;   // the call from the rigid body's POST_PHYSICS sync returns here
    inline static constexpr std::array<unsigned char,16> code{0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x41,0x56,0x41,0x57,0x48};
    inline static constexpr std::array<unsigned char,5> site_code{0xe8,0xf1,0x94,0xf4,0xff};   // call 0x754ea0 at 0x80b9aa
    static HeldRenderPose& instance() noexcept {static HeldRenderPose p;return p;}
    // NativeGrab, per prop motor write (dispatcher thread): the hold's goal this physics step; `body` = the body the motor read.
    // v1253 mode/world/handle: a ragdoll's held body (the hook reads its transform from the hknp pool at the engine's own sync).
    void publish(unsigned side,std::uintptr_t entity,const WorldTrackedPose& goal,const WorldTrackedPose& body,double now,unsigned mode=prop,std::uintptr_t world=0,std::uint32_t handle=0) noexcept {
        if(!installed_.load(std::memory_order_relaxed)) return;
        if(mode!=prop && (!joint_target_ || !ragdolls || (mode==corpse && !corpses))) {refuse(ragdoll);return;}
        if(!entity || side>1 || !grab_pose_valid(goal) || (mode!=prop && !world)) {refuse(invalid);end(entity,now);return;}
        if(!(length(goal.position-body.position)<=max_goal_m)) {refuse(too_far);end(entity,now);return;}
        std::lock_guard<std::mutex> l(m_);
        const auto& other=slots_[side^1u];
        if(other.entity==entity && other.held && other.handle!=handle) {refuse(two_hands);return;}   // ponytail: two bones of one ragdoll - the first hand's keeps it
        unsigned i=other.entity==entity?side^1u:side;
        slots_[i].publish(entity,goal,now);slots_[i].mode=mode;slots_[i].world=world;slots_[i].handle=handle;
        watched_[i].store(entity,std::memory_order_release);published_.fetch_add(1,std::memory_order_relaxed);
    }
    void end(std::uintptr_t entity,double now) noexcept {
        if(!entity) return;std::lock_guard<std::mutex> l(m_);
        for(auto& s:slots_) if(s.entity==entity && s.held) {s.end(now);ended_.fetch_add(1,std::memory_order_relaxed);}
    }
    void refuse(Refusal r) noexcept {if(r<refusals) refused_[r].fetch_add(1,std::memory_order_relaxed);}
    // v1258: the hold as this render draws it (a body pose), for every later engine pose of `entity` (HeldRenderSlot::note_drawn). Props only.
    bool note_drawn(std::uintptr_t entity,const WorldTrackedPose& drawn,double now,const HeldCarry* carry=nullptr) noexcept {
        if(!entity || !installed_.load(std::memory_order_relaxed)) return false;
        std::lock_guard<std::mutex> l(m_);
        for(auto& s:slots_) if(s.entity==entity && s.held && s.mode!=corpse) {const bool ok=s.note_drawn(drawn,now,carry);(ok?drawn_notes_:drawn_refused_).fetch_add(1,std::memory_order_relaxed);return ok;}   // v1266: severed parts too
        return false;
    }
    void note_frame(std::uintptr_t entity,const float* o,const float* a) noexcept {   // v1266: the ragdoll's model frame written at the frame site
        WorldTrackedPose f{};f.position={o[0],o[1],o[2]};for(unsigned k=0;k<3;++k) f.basis[k]={a[k*3],a[k*3+1],a[k*3+2]};
        std::lock_guard<std::mutex> l(m_);for(auto& s:slots_) if(s.entity==entity && s.mode==severed) s.note_frame(f);
    }
    // v1258: the player's rig now (dxgi_probe: the body model's root position, camera heading - head follow), read on the engine's sync thread.
    using RigNow=bool (*)(HeldCarry&) noexcept;
    inline static RigNow rig_now{};
    // v1258: the poses the engine was given for `entity` (newest first, at most HeldRenderSlot::ring): the publisher finds the one its model frame came from.
    // v1266: severed parts too; `frames` (optional): the model frame the ragdoll sync wrote with each given (frame_ok false: none recorded, a prop).
    unsigned recent(std::uintptr_t entity,WorldTrackedPose* out,unsigned max,WorldTrackedPose* frames=nullptr,bool* frame_ok=nullptr) noexcept {
        if(!entity || !out) return 0;
        std::lock_guard<std::mutex> l(m_);
        for(const auto& s:slots_) if(s.entity==entity && s.mode!=corpse) {
            unsigned n=0;
            for(unsigned i=0;i<s.recent_n && n<max;++i) {
                const unsigned k=(s.recent_next+HeldRenderSlot::ring-1-i)%HeldRenderSlot::ring;
                if(frames) frames[n]=s.recent_frame[k];if(frame_ok) frame_ok[n]=s.recent_frame_ok[k];out[n++]=s.recent[k];
            }
            return n;
        }
        return 0;
    }
    // The pose the engine was last given for `entity` while it is overridden (held or blending): a model frame read now belongs to it, not the body.
    std::optional<WorldTrackedPose> source(std::uintptr_t entity) noexcept {
        if(!entity || (watched_[0].load(std::memory_order_relaxed)!=entity && watched_[1].load(std::memory_order_relaxed)!=entity)) return std::nullopt;
        std::lock_guard<std::mutex> l(m_);
        const double now=precise_s();
        for(const auto& s:slots_) if(s.entity==entity && s.has_written && (s.held?now-s.at_s<=HeldRenderSlot::fresh_s:now-s.released_s<HeldRenderSlot::blend_s)) return s.written;
        return std::nullopt;
    }
    std::string json() const {
        return "{\"installed\":"+std::string(installed_.load()?"true":"false")+",\"published\":"+std::to_string(published_.load())+",\"written\":"+std::to_string(written_.load())+
            ",\"blended\":"+std::to_string(blended_.load())+",\"ended\":"+std::to_string(ended_.load())+",\"refused\":{\"ragdoll\":"+std::to_string(refused_[ragdoll].load())+
            ",\"far\":"+std::to_string(refused_[too_far].load())+",\"invalid\":"+std::to_string(refused_[invalid].load())+",\"two_hands\":"+std::to_string(refused_[two_hands].load())+
            ",\"unread\":"+std::to_string(refused_[unread].load())+"},\"ragdoll\":{\"enabled\":"+(joint_target_ && ragdolls?"true":"false")+",\"corpses\":"+(corpses?"true":"false")+
            ",\"frames\":"+std::to_string(ragdoll_frames_.load())+",\"bones\":"+std::to_string(ragdoll_bones_.load())+",\"unframed\":"+std::to_string(unframed_.load())+"}"+
            ",\"drawn_hold\":{\"on\":"+(HeldRenderSlot::drawn_hold?"true":"false")+",\"notes\":"+std::to_string(drawn_notes_.load())+",\"refused\":"+std::to_string(drawn_refused_.load())+
            ",\"given\":{\"goal\":"+std::to_string(given_[0].load())+",\"relative\":"+std::to_string(given_[1].load())+",\"carried\":"+std::to_string(given_[2].load())+
            ",\"carried_no_step\":"+std::to_string(carry_stepped_[0].load())+",\"carried_frame_step\":"+std::to_string(carry_stepped_[1].load())+"}"+   // v1266: carried with no root step (still) / with this frame's root step
            ",\"note_too_old\":"+std::to_string(too_old_.load())+",\"note_age_ms\":["+[&]{std::string a;for(unsigned i=0;i<age_bins;++i) a+=(i?",":"")+std::to_string(age_n_[i].load());return a;}()+
            "],\"note_age_edges_ms\":[5,10,15,20,25,30,40,100,\"inf\"]}}";   // v1262
    }
    static bool install(std::uintptr_t base) noexcept {
        auto& p=instance();if(!base) return false;
        if(!target_) {
            if(!same(base+rva,code.data(),code.size()) || !same(base+site_rva-5,site_code.data(),site_code.size())) return false;
            void* t=reinterpret_cast<void*>(base+rva);
            if(MH_CreateHook(t,reinterpret_cast<void*>(&hook),reinterpret_cast<void**>(&original_))!=MH_OK) return false;
            target_=t;site_=base+site_rva;
        }
        if(!joint_target_ && ragdolls && same(base+joint_rva,joint_code.data(),joint_code.size()) && same(base+joint_site_rva-5,joint_site_code.data(),5) &&
           same(base+frame_site_rva-5,frame_site_code.data(),5)) {   // v1253: optional - without it ragdolls stay refused
            void* j=reinterpret_cast<void*>(base+joint_rva);
            if(MH_CreateHook(j,reinterpret_cast<void*>(&joint_hook),reinterpret_cast<void**>(&joint_original_))==MH_OK) {
                frame_site_=base+frame_site_rva;joint_site_=base+joint_site_rva;
                if(MH_EnableHook(j)==MH_OK) joint_target_=j;else {MH_RemoveHook(j);frame_site_=joint_site_=0;}
            }
        }
        const auto r=MH_EnableHook(target_);
        const bool ok=r==MH_OK || r==MH_ERROR_ENABLED;p.installed_.store(ok);return ok;
    }
    static void disable() noexcept {
        auto& p=instance();p.installed_.store(false);p.watched_[0].store(0);p.watched_[1].store(0);
        if(joint_target_) MH_DisableHook(joint_target_);
        if(!target_) return;MH_DisableHook(target_);
        const auto deadline=GetTickCount64()+2000;while(depth_.load() && GetTickCount64()<deadline) Sleep(1);
    }
private:
    using Sync=void (__fastcall*)(void*,const float*,const float*,const float*,const float*,const char*,char);
    inline static Sync original_{};inline static void* target_{};inline static std::uintptr_t site_{};inline static std::atomic<unsigned> depth_{};
    using Joint=void (__fastcall*)(void*,int,short,int,char,const float*,const float*);
    inline static Joint joint_original_{};inline static void* joint_target_{};inline static std::uintptr_t joint_site_{},frame_site_{};
    std::atomic<std::uint64_t> ragdoll_frames_{},ragdoll_bones_{},unframed_{};
    std::mutex m_;std::array<HeldRenderSlot,2> slots_{};std::array<std::atomic<std::uintptr_t>,2> watched_{};std::atomic<bool> installed_{};
    std::atomic<std::uint64_t> published_{},written_{},blended_{},ended_{};std::array<std::atomic<std::uint64_t>,refusals> refused_{};
    std::atomic<std::uint64_t> drawn_notes_{},drawn_refused_{};std::array<std::atomic<std::uint64_t>,3> given_{};   // v1258: [goal, goal * X, carried drawn hold]
    std::array<std::atomic<std::uint64_t>,2> carry_stepped_{};   // v1258: carried with the rig as read / with the last tick step repeated (the rig read was not yet this tick's)
    static constexpr unsigned age_bins=9;static constexpr double age_edge_ms[age_bins]={5,10,15,20,25,30,40,100,1e9};
    std::array<std::atomic<std::uint64_t>,age_bins> age_n_{};std::atomic<std::uint64_t> too_old_{};   // v1262: note age at the sync; carries refused as too old
    static bool same(std::uintptr_t at,const unsigned char* bytes,std::size_t n) noexcept {
        __try {return std::memcmp(reinterpret_cast<const void*>(at),bytes,n)==0;} __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    static std::uintptr_t entity_of(void* holder) noexcept {   // rigid+0xe8 -> [+8] reference frame component -> [+0x10] entity
        __try {const auto rf=*reinterpret_cast<const std::uintptr_t*>(reinterpret_cast<std::uintptr_t>(holder)+8);
            return rf?*reinterpret_cast<const std::uintptr_t*>(rf+0x10):0;} __except(EXCEPTION_EXECUTE_HANDLER) {return 0;}
    }
    static std::uintptr_t component_entity(void* component) noexcept {
        __try {return *reinterpret_cast<const std::uintptr_t*>(reinterpret_cast<std::uintptr_t>(component)+0x10);} __except(EXCEPTION_EXECUTE_HANDLER) {return 0;}
    }
    // The held body's transform as the ragdoll sync reads it (hknp world +0x28 pool, 0xb0 a record, columns at +0x00): basis = columns 0-2, origin = column 3.
    static bool body_of(std::uintptr_t world,std::uint32_t handle,float* f) noexcept {
        __try {const auto pool=*reinterpret_cast<const std::uintptr_t*>(world+0x28);if(!pool || (handle&0xffffff)==0xffffff) return false;
            std::memcpy(f,reinterpret_cast<const void*>(pool+std::uintptr_t(handle&0xffffff)*0xb0),16*sizeof(float));return true;} __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    static bool write_model_frame(std::uintptr_t entity,const float* o,const float* a) noexcept {
        __try {const auto model=*reinterpret_cast<const std::uintptr_t*>(entity+0xf0);if(!model) return false;
            std::memcpy(reinterpret_cast<void*>(model+0x1860),o,3*sizeof(float));std::memcpy(reinterpret_cast<void*>(model+0x186c),a,9*sizeof(float));return true;}
        __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    // Under m_: this tick's rigid move of a ragdoll `entity` (one per sync: the bone requests and the frame of one 0x76d640 call share it).
    bool ragdoll_delta(std::uintptr_t entity,bool frame,HoldDelta& d) noexcept {
        HeldCarry rig{};const bool have_rig=HeldRenderSlot::drawn_hold && rig_now && rig_now(rig);   // v1266: the same carry as a prop (read before the lock)
        std::lock_guard<std::mutex> l(m_);
        for(unsigned i=0;i<2;++i) {
            auto& s=slots_[i];if(s.entity!=entity || s.mode==prop) continue;
            const double now=precise_s();
            if(!(s.delta_ok && now-s.delta_s<.002)) {   // a new tick: decide again from the held body as the engine reads it now
                if(s.delta_ok && s.bones_seen && !s.frame_seen) unframed_.fetch_add(1,std::memory_order_relaxed);   // bones moved without the frame (expected never)
                s.delta_ok=s.bones_seen=s.frame_seen=false;
                float f[16]{};if(!body_of(s.world,s.handle,f)) {refuse(unread);s={};watched_[i].store(0);return false;}
                WorldTrackedPose body{};body.position={f[12],f[13],f[14]};for(unsigned k=0;k<3;++k) body.basis[k]={f[k*4],f[k*4+1],f[k*4+2]};
                WorldTrackedPose out{};bool blending=false;
                if(!grab_pose_valid(body) || !s.decide(body,now,out,blending,have_rig && s.mode==severed?&rig:nullptr)) {watched_[i].store(0,std::memory_order_release);return false;}
                if(!blending && s.used<3) given_[s.used].fetch_add(1,std::memory_order_relaxed);if(s.used==HeldRenderSlot::given_carried) carry_stepped_[s.stepped?1:0].fetch_add(1,std::memory_order_relaxed);
                if(s.mode==corpse && !blending) {out=translated_goal(body,out,corpse_cap_m);s.written=out;}
                if(!finite(out.position) || !grab_pose_valid(out)) {refuse(invalid);s={};watched_[i].store(0);return false;}
                s.delta={body,out};s.delta_s=now;s.delta_ok=true;
                if(blending) blended_.fetch_add(1,std::memory_order_relaxed);
            }
            (frame?s.frame_seen:s.bones_seen)=true;
            d=s.delta;return true;
        }
        return false;
    }
    // Under m_: the replacement origin/axis for `entity` (game job thread, inside the engine's POST_PHYSICS stage).
    bool substitute(std::uintptr_t entity,const float* origin,const float* axis,float* o,float* a) noexcept {
        WorldTrackedPose body{};body.position={origin[0],origin[1],origin[2]};
        for(unsigned i=0;i<3;++i) body.basis[i]={axis[i*3],axis[i*3+1],axis[i*3+2]};
        HeldCarry rig{};const bool have_rig=HeldRenderSlot::drawn_hold && rig_now && rig_now(rig);   // v1258: read before the lock (game memory)
        std::lock_guard<std::mutex> l(m_);
        for(unsigned i=0;i<2;++i) {
            auto& s=slots_[i];if(s.entity!=entity || s.mode!=prop) continue;
            WorldTrackedPose out{};bool blending=false;
            if(!s.decide(body,precise_s(),out,blending,have_rig?&rig:nullptr)) {watched_[i].store(0,std::memory_order_release);return false;}
            if(!finite(out.position) || !finite(out.basis[0]) || !finite(out.basis[1]) || !finite(out.basis[2])) {refuse(invalid);s={};watched_[i].store(0);return false;}
            o[0]=float(out.position.x);o[1]=float(out.position.y);o[2]=float(out.position.z);
            for(unsigned k=0;k<3;++k) {a[k*3]=float(out.basis[k].x);a[k*3+1]=float(out.basis[k].y);a[k*3+2]=float(out.basis[k].z);}
            (blending?blended_:written_).fetch_add(1,std::memory_order_relaxed);if(!blending && s.used<3) given_[s.used].fetch_add(1,std::memory_order_relaxed);
            if(s.used==HeldRenderSlot::given_carried) carry_stepped_[s.stepped?1:0].fetch_add(1,std::memory_order_relaxed);
            if(!blending && s.drawn_ok) {   // v1262: how old the newest drawn note was at this sync, and syncs it was too old to carry
                const double ms=(precise_s()-s.drawn_s)*1000.;unsigned bin=0;while(bin+1<age_bins && !(ms<=age_edge_ms[bin])) ++bin;age_n_[bin].fetch_add(1,std::memory_order_relaxed);
                if(s.carry_ok && s.used!=HeldRenderSlot::given_carried && ms>s.carry_age()*1000.) too_old_.fetch_add(1,std::memory_order_relaxed);
            }
            return true;
        }
        return false;
    }
    static void __fastcall hook(void* holder,const float* origin,const float* axis,const float* linear,const float* angular,const char* tag,char flag) {
        depth_.fetch_add(1);auto& p=instance();
        alignas(16) float o[4]{},a[12]{};bool replaced=false;
        const auto from=reinterpret_cast<std::uintptr_t>(_ReturnAddress());std::uintptr_t e{};
        if((from==site_ || (frame_site_ && from==frame_site_)) && origin && axis && (p.watched_[0].load(std::memory_order_acquire)|p.watched_[1].load(std::memory_order_acquire))) {
            e=entity_of(holder);
            if(e && (e==p.watched_[0].load() || e==p.watched_[1].load())) {
                if(from==site_) replaced=p.substitute(e,origin,axis,o,a);
                else if(HoldDelta d;p.ragdoll_delta(e,true,d)) {std::memcpy(o,origin,12);std::memcpy(a,axis,36);d.apply(o,a);replaced=true;}
            }
        }
        original_(holder,replaced?o:origin,replaced?a:axis,linear,angular,tag,flag);
        // v1253: the ragdoll sync wrote the model frame itself (0x76da05) from the unmoved origin/axis: the moved one, as it would have
        if(replaced && from==frame_site_ && write_model_frame(e,o,a)) {p.ragdoll_frames_.fetch_add(1,std::memory_order_relaxed);p.note_frame(e,o,a);}   // v1266: paired with its given
        depth_.fetch_sub(1);
    }
    // v1253: a ragdoll's world bone request (0x53fbe0 from 0x76db00): the bone moved with the held body's move of this tick.
    static void __fastcall joint_hook(void* anim,int type,short joint,int mode,char flag,const float* pos,const float* rot) {
        depth_.fetch_add(1);auto& p=instance();
        alignas(16) float o[4]{},a[12]{};bool replaced=false;
        if(reinterpret_cast<std::uintptr_t>(_ReturnAddress())==joint_site_ && pos && rot && (p.watched_[0].load(std::memory_order_acquire)|p.watched_[1].load(std::memory_order_acquire))) {
            const auto e=component_entity(anim);
            if(e && (e==p.watched_[0].load() || e==p.watched_[1].load())) if(HoldDelta d;p.ragdoll_delta(e,false,d)) {
                std::memcpy(o,pos,12);std::memcpy(a,rot,36);d.apply(o,a);replaced=true;p.ragdoll_bones_.fetch_add(1,std::memory_order_relaxed);
            }
        }
        joint_original_(anim,type,joint,mode,flag,replaced?o:pos,replaced?a:rot);
        depth_.fetch_sub(1);
    }
};
inline HeldRenderPose& held_render_pose() noexcept {return HeldRenderPose::instance();}
}
