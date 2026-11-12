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
#include <windows.h>
#include <intrin.h>
#include <MinHook.h>
#include "physical_grab.hpp"
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
// One held (or just released) entity: its published goal and what the engine was last given for it.
struct HeldRenderSlot {
    static constexpr double fresh_s=.1,blend_s=.15;
    std::uintptr_t entity{};WorldTrackedPose goal{};double at_s{};bool held{};
    WorldTrackedPose written{};bool has_written{};double released_s{};bool offset_set{};Vec3 off_pos{},off_rot{};
    void publish(std::uintptr_t e,const WorldTrackedPose& g,double now) noexcept {
        if(e!=entity) *this={};
        entity=e;goal=g;at_s=now;held=true;offset_set=false;
    }
    void end(double now) noexcept {if(held) {held=false;released_s=now;offset_set=false;}}
    // The pose the engine is given for this entity whose body is at `body` at `now` (s). false: the body's own (and a finished blend frees the slot).
    bool decide(const WorldTrackedPose& body,double now,WorldTrackedPose& out,bool& blending) noexcept {
        blending=false;
        if(held && !(now-at_s<=fresh_s)) end(at_s+fresh_s);   // publications stopped without an end (a stalled pass): blend out from then
        if(held) {out=goal;written=goal;has_written=true;return true;}
        if(!has_written || !(now>=released_s) || !(now-released_s<blend_s)) {*this={};return false;}
        if(!offset_set) {off_pos=written.position-body.position;off_rot=grab_rotation_error(body,written);offset_set=true;}
        out=render_blend(body,off_pos,off_rot,(now-released_s)/blend_s);written=out;blending=true;return true;
    }
};
class HeldRenderPose {
public:
    enum Refusal : unsigned {ragdoll,too_far,invalid,refusals};
    static constexpr double max_goal_m=1.6;   // the motor lets a prop go at 1.5 m of lag (PhysicalGrab::corpse_motor)
    inline static constexpr std::uintptr_t rva=0x754ea0,site_rva=0x80b9af;   // the call from the rigid body's POST_PHYSICS sync returns here
    inline static constexpr std::array<unsigned char,16> code{0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x41,0x56,0x41,0x57,0x48};
    inline static constexpr std::array<unsigned char,5> site_code{0xe8,0xf1,0x94,0xf4,0xff};   // call 0x754ea0 at 0x80b9aa
    static HeldRenderPose& instance() noexcept {static HeldRenderPose p;return p;}
    // NativeGrab, per prop motor write (dispatcher thread): the hold's goal this physics step; `body` = the body the motor read.
    void publish(unsigned side,std::uintptr_t entity,const WorldTrackedPose& goal,const WorldTrackedPose& body,double now) noexcept {
        if(!installed_.load(std::memory_order_relaxed)) return;
        if(!entity || side>1 || !grab_pose_valid(goal)) {refuse(invalid);end(entity,now);return;}
        if(!(length(goal.position-body.position)<=max_goal_m)) {refuse(too_far);end(entity,now);return;}
        std::lock_guard<std::mutex> l(m_);
        unsigned i=slots_[side^1u].entity==entity?side^1u:side;
        slots_[i].publish(entity,goal,now);watched_[i].store(entity,std::memory_order_release);published_.fetch_add(1,std::memory_order_relaxed);
    }
    void end(std::uintptr_t entity,double now) noexcept {
        if(!entity) return;std::lock_guard<std::mutex> l(m_);
        for(auto& s:slots_) if(s.entity==entity && s.held) {s.end(now);ended_.fetch_add(1,std::memory_order_relaxed);}
    }
    void refuse(Refusal r) noexcept {if(r<refusals) refused_[r].fetch_add(1,std::memory_order_relaxed);}
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
            ",\"far\":"+std::to_string(refused_[too_far].load())+",\"invalid\":"+std::to_string(refused_[invalid].load())+"}}";
    }
    static bool install(std::uintptr_t base) noexcept {
        auto& p=instance();if(!base) return false;
        if(!target_) {
            if(!same(base+rva,code.data(),code.size()) || !same(base+site_rva-5,site_code.data(),site_code.size())) return false;
            void* t=reinterpret_cast<void*>(base+rva);
            if(MH_CreateHook(t,reinterpret_cast<void*>(&hook),reinterpret_cast<void**>(&original_))!=MH_OK) return false;
            target_=t;site_=base+site_rva;
        }
        const auto r=MH_EnableHook(target_);
        const bool ok=r==MH_OK || r==MH_ERROR_ENABLED;p.installed_.store(ok);return ok;
    }
    static void disable() noexcept {
        auto& p=instance();p.installed_.store(false);p.watched_[0].store(0);p.watched_[1].store(0);
        if(!target_) return;MH_DisableHook(target_);
        const auto deadline=GetTickCount64()+2000;while(depth_.load() && GetTickCount64()<deadline) Sleep(1);
    }
private:
    using Sync=void (__fastcall*)(void*,const float*,const float*,const float*,const float*,const char*,char);
    inline static Sync original_{};inline static void* target_{};inline static std::uintptr_t site_{};inline static std::atomic<unsigned> depth_{};
    std::mutex m_;std::array<HeldRenderSlot,2> slots_{};std::array<std::atomic<std::uintptr_t>,2> watched_{};std::atomic<bool> installed_{};
    std::atomic<std::uint64_t> published_{},written_{},blended_{},ended_{};std::array<std::atomic<std::uint64_t>,refusals> refused_{};
    static bool same(std::uintptr_t at,const unsigned char* bytes,std::size_t n) noexcept {
        __try {return std::memcmp(reinterpret_cast<const void*>(at),bytes,n)==0;} __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    static std::uintptr_t entity_of(void* holder) noexcept {   // rigid+0xe8 -> [+8] reference frame component -> [+0x10] entity
        __try {const auto rf=*reinterpret_cast<const std::uintptr_t*>(reinterpret_cast<std::uintptr_t>(holder)+8);
            return rf?*reinterpret_cast<const std::uintptr_t*>(rf+0x10):0;} __except(EXCEPTION_EXECUTE_HANDLER) {return 0;}
    }
    // Under m_: the replacement origin/axis for `entity` (game job thread, inside the engine's POST_PHYSICS stage).
    bool substitute(std::uintptr_t entity,const float* origin,const float* axis,float* o,float* a) noexcept {
        WorldTrackedPose body{};body.position={origin[0],origin[1],origin[2]};
        for(unsigned i=0;i<3;++i) body.basis[i]={axis[i*3],axis[i*3+1],axis[i*3+2]};
        std::lock_guard<std::mutex> l(m_);
        for(unsigned i=0;i<2;++i) {
            auto& s=slots_[i];if(s.entity!=entity) continue;
            WorldTrackedPose out{};bool blending=false;
            if(!s.decide(body,precise_s(),out,blending)) {watched_[i].store(0,std::memory_order_release);return false;}
            if(!finite(out.position) || !finite(out.basis[0]) || !finite(out.basis[1]) || !finite(out.basis[2])) {refuse(invalid);s={};watched_[i].store(0);return false;}
            o[0]=float(out.position.x);o[1]=float(out.position.y);o[2]=float(out.position.z);
            for(unsigned k=0;k<3;++k) {a[k*3]=float(out.basis[k].x);a[k*3+1]=float(out.basis[k].y);a[k*3+2]=float(out.basis[k].z);}
            (blending?blended_:written_).fetch_add(1,std::memory_order_relaxed);return true;
        }
        return false;
    }
    static void __fastcall hook(void* holder,const float* origin,const float* axis,const float* linear,const float* angular,const char* tag,char flag) {
        depth_.fetch_add(1);auto& p=instance();
        alignas(16) float o[4]{},a[12]{};bool replaced=false;
        if(reinterpret_cast<std::uintptr_t>(_ReturnAddress())==site_ && origin && axis && (p.watched_[0].load(std::memory_order_acquire)|p.watched_[1].load(std::memory_order_acquire))) {
            const auto e=entity_of(holder);
            if(e && (e==p.watched_[0].load() || e==p.watched_[1].load())) replaced=p.substitute(e,origin,axis,o,a);
        }
        original_(holder,replaced?o:origin,replaced?a:axis,linear,angular,tag,flag);
        depth_.fetch_sub(1);
    }
};
inline HeldRenderPose& held_render_pose() noexcept {return HeldRenderPose::instance();}
}
