#pragma once
// Native held object (HoldObject 49: bottle, glass, severed arm, skull) in the tracked VR hand, and player arms the engine
// hid while the mod tracks them. docs/native-hold-object.md (static RE 2026-10-12); live 2026-10-12: the left arm entity is
// hidden whenever the left inventory slot is empty and through every equip change, so the tracked left hand vanished.
// - Bind target: every pre-physics step the engine drives the bound object's keyframed body toward master x bind offset
//   through 0x7506d0, which only writes its four output buffers. Its post-hook replaces them with the hand pose. It runs on
//   engine task threads: a versioned publication serializes native writes and makes no engine calls. The drawn model follows the bind, not the
//   body (live 2026-10-13: bound to the camera joint), so anchor() rebinds it to the hand (bottle in the fist, sim v376).
//   The engine itself holsters the sword for the hold (rebound to the player's joint 2, hidden; live v378).
// - Throw velocity: the throw posts the generic set-velocity event 0x149, applied by the rigid-body handler 0x727100(rb, ev).
//   For the armed entity only, its linear velocity becomes the hand's; native spin, thrown flags, AI noise and the release
//   wall sweep stay. A throw blocked by a wall (native 0.1 m/s) is kept.
// - Arm hide/show: 0x774a10(u32* arm id field, show) is the engine's own arm hide (+0x80) / show (+0x88); 18 callers, among them
//   Block's update hiding an empty left arm every frame (docs/native-attack-gating.md). Its hook skips hides of the arms the mod
//   keeps (published within 200 ms); show_arms() shows an arm hidden by any other path (game thread only).
#include <windows.h>
#include <intrin.h>
#include "../third_party/minhook/include/MinHook.h"
#include "game_rig.hpp"
#include "native_held_placement.hpp"
#include "physical_grab.hpp"
#include "../third_party/dishonored_vr/grab_pose.h"
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

namespace d2vr {
class NativeHeldObject {
    friend struct NativeHeldObjectCheck;
public:
    inline static constexpr std::uintptr_t bind_rva=0x7506d0,velocity_rva=0x727100,arm_show_rva=0x774a10,render_scale_rva=0x87b760;
    // 0x87b760(entity, float): entity+0xc8 first-person render scale (HoldObject entry sets it: the object is drawn as a view-model,
    // pinned to the camera whatever its world pose; exit sets 0). Pinned: ucomiss/jp/je/movss [rcx+0xc8].
    inline static constexpr std::array<unsigned char,19> render_scale_code{0x0f,0x2e,0x89,0xc8,0x00,0x00,0x00,0x7a,0x02,0x74,0x0d,0xf3,0x0f,0x11,0x89,0xc8,0x00,0x00,0x00};
    inline static constexpr std::array<unsigned char,18> bind_code{0x48,0x8b,0xc4,0x48,0x81,0xec,0x68,0x01,0x00,0x00,0x66,0x83,0xb9,0xd0,0x03,0x00,0x00,0xff};
    inline static constexpr std::array<unsigned char,16> velocity_code{0x4c,0x8b,0x42,0x08,0xc6,0x81,0xb2,0x00,0x00,0x00,0x01,0x41,0x80,0x78,0x19,0x00};
    inline static constexpr std::array<unsigned char,11> arm_show_code{0x40,0x53,0x48,0x83,0xec,0x20,0x8b,0x09,0x0f,0xb6,0xda};
    // World target of the held object's origin (game metres, Z up) and its axis rows (idMat3, as entity+0x30c).
    struct Target {std::uintptr_t rf{},entity{};float pos[3]{},axis[9]{};std::uint64_t tick{};
        std::uint64_t placement_serial{},pose_epoch{};unsigned source_side{2};};
    using BindObserver=void (*)(unsigned,std::uintptr_t,int,std::uintptr_t,const float*,const float*,const float*,const float*,float) noexcept;
    inline static std::atomic<BindObserver> bind_observer{};
    static bool diagnostic_target(Target& out) noexcept {
        return snapshot(out) && out.rf && out.rf==rf_.load(std::memory_order_acquire);
    }
    static bool retire_bind_observer() noexcept {
        bind_observer=nullptr;
        const auto deadline=GetTickCount64()+5000;
        while(depth_ && GetTickCount64()<deadline)Sleep(1);
        return !depth_;
    }

    // Render thread (single writer).
    static void publish(const Target& t) noexcept {
        AcquireSRWLockExclusive(&publication_lock_);
        __try {
            if(!advance_publication_locked())return;
            data_=t;rf_.store(t.rf,std::memory_order_release);
        } __finally {ReleaseSRWLockExclusive(&publication_lock_);}
    }
    static void clear() noexcept {
        NativeHeldPlacement::process_owner().clear();
        AcquireSRWLockExclusive(&publication_lock_);
        __try {
            (void)advance_publication_locked();data_={};rf_.store(0,std::memory_order_release);
        } __finally {ReleaseSRWLockExclusive(&publication_lock_);}
    }
    // Render thread: the next set-velocity event of this entity (within 600 ms) carries v (m/s, game world).
    static void arm_throw(std::uintptr_t entity,const float v[3]) noexcept {
        for(unsigned i=0;i<3;++i) throw_v_[i]=v[i];
        throw_tick_.store(GetTickCount64(),std::memory_order_relaxed);throw_entity_.store(entity,std::memory_order_release);
    }

    static bool install(std::uintptr_t base) noexcept {
        if(!base || depth_) return false;
        if(!bind_target_) {
            if(!code(base+bind_rva,bind_code) || !code(base+velocity_rva,velocity_code)) return false;
            void* b=reinterpret_cast<void*>(base+bind_rva);void* v=reinterpret_cast<void*>(base+velocity_rva);
            if(MH_CreateHook(b,reinterpret_cast<void*>(&bind_hook),reinterpret_cast<void**>(&bind_))!=MH_OK) return false;
            if(MH_CreateHook(v,reinterpret_cast<void*>(&velocity_hook),reinterpret_cast<void**>(&velocity_))!=MH_OK) {MH_RemoveHook(b);return false;}
            bind_target_=b;velocity_target_=v;
            void* s=reinterpret_cast<void*>(base+arm_show_rva); // optional: without it show_arms() still restores hidden arms
            if(code(base+arm_show_rva,arm_show_code) && MH_CreateHook(s,reinterpret_cast<void*>(&arm_part_hook),reinterpret_cast<void**>(&arm_part_))==MH_OK) arm_target_=s;
        }
        const auto a=MH_EnableHook(bind_target_),c=MH_EnableHook(velocity_target_);
        if(arm_target_) MH_EnableHook(arm_target_);
        return (a==MH_OK || a==MH_ERROR_ENABLED) && (c==MH_OK || c==MH_ERROR_ENABLED);
    }
    static bool disable() noexcept {
        bind_observer=nullptr;
        clear();throw_entity_.store(0);keep_mask_.store(0);
        NativeHeldPlacement::process_owner().disable();
        if(!bind_target_) return true;
        if(arm_target_) MH_DisableHook(arm_target_);
        const auto a=MH_DisableHook(bind_target_),c=MH_DisableHook(velocity_target_);
        if((a!=MH_OK && a!=MH_ERROR_DISABLED) || (c!=MH_OK && c!=MH_ERROR_DISABLED)) return false;
        const auto deadline=GetTickCount64()+5000;
        while(depth_ && GetTickCount64()<deadline) Sleep(1);
        return !depth_; // trampolines stay for a racing original-only call
    }

    // Game thread only. mask bit0 = left arm (id at body+0x370), bit1 = right (body+0x680): show the arm entity if the engine hid it.
    template<class Read> static unsigned show_arms(std::uintptr_t base,Read& read,unsigned mask) {
        if(!mask || arm_show_failed) return 0;
        // Hooked, the prologue is the hook's jump: install() verified the original bytes before hooking.
        if(!arm_target_ && !code(base+arm_show_rva,arm_show_code)) {arm_show_failed=true;return 0;}
        const auto arms=rig::player_arms(read,base);const auto game=rig::value<std::uintptr_t>(read,base+0x39a5290);
        if(!arms || !game || !rig::pointer(*game)) return 0;
        unsigned shown=0;
        for(unsigned side=0;side<2;++side) if(mask&(1u<<side)) {
            const auto part=(*arms)[0].body+(side?0x680:0x370);
            const auto id=rig::value<std::uint32_t>(read,part);
            if(!id || *id==0x3fff0010 || (*id>>16)>=0x3fff) continue;
            const auto e=rig::value<std::uintptr_t>(read,*game+0x3130b0+(*id>>16)*8ull);
            if(!e || !rig::pointer(*e) || rig::value<std::uint32_t>(read,*e+0xa8)!=id) continue;
            const auto flags=rig::value<std::uint8_t>(read,*e+0xd0);
            if(!flags || !(*flags&1)) continue; // visible
            if(!call_part(base+arm_show_rva,reinterpret_cast<std::uint32_t*>(part),1)) {arm_show_failed=true;return shown;}
            ++shown;++arm_shows;
        }
        return shown;
    }
    // Game thread only: draw the held object in the world (render scale 0) so it shows where the hand holds it.
    template<class Read> static bool world_render(std::uintptr_t base,Read& read,std::uintptr_t entity) {
        static const bool verified=code(base+render_scale_rva,render_scale_code);
        if(!verified || !entity) return false;
        const auto game=rig::value<std::uintptr_t>(read,base+0x39a5290);const auto id=rig::value<std::uint32_t>(read,entity+0xa8);
        if(!game || !id || *id==0x3fff0010 || (*id>>16)>=0x3fff || rig::value<std::uintptr_t>(read,*game+0x3130b0+(*id>>16)*8ull)!=entity) return false;
        const auto scale=rig::value<float>(read,entity+0xc8);if(!scale || *scale==0.f) return true;
        __try {reinterpret_cast<void (__fastcall*)(std::uintptr_t,float)>(base+render_scale_rva)(entity,0.f);} __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
        ++world_renders;return true;
    }
    // Render/any thread: is this arm entity currently hidden by the engine? (bit0 left, bit1 right)
    template<class Read> static unsigned hidden_arms(std::uintptr_t base,Read& read,std::uintptr_t body) {
        const auto game=rig::value<std::uintptr_t>(read,base+0x39a5290);if(!game || !rig::pointer(*game) || !body) return 0;
        unsigned out=0;
        for(unsigned side=0;side<2;++side) {
            const auto id=rig::value<std::uint32_t>(read,body+(side?0x680:0x370));
            if(!id || *id==0x3fff0010 || (*id>>16)>=0x3fff) continue;
            const auto e=rig::value<std::uintptr_t>(read,*game+0x3130b0+(*id>>16)*8ull);
            if(!e || !rig::pointer(*e) || rig::value<std::uint32_t>(read,*e+0xa8)!=id) continue;
            if(const auto f=rig::value<std::uint8_t>(read,*e+0xd0);f && (*f&1)) out|=1u<<side;
        }
        return out;
    }
    // Render thread: arms (bit0 left, bit1 right) of this body whose engine hides are skipped; republish every frame.
    static void keep_arms(std::uintptr_t body,unsigned mask) noexcept {
        keep_body_.store(body,std::memory_order_relaxed);keep_tick_.store(GetTickCount64(),std::memory_order_relaxed);keep_mask_.store(mask,std::memory_order_release);
    }
    inline static std::atomic<std::uint64_t> placed{},anchored{},thrown{},throw_kept{},arm_shows{},hides_skipped{},world_renders{};
    inline static std::atomic<bool> arm_show_failed{};
private:
    using Bind=std::uintptr_t (__fastcall*)(void*,int,float*,float*,float*,float*,float);
    using Velocity=std::uintptr_t (__fastcall*)(void*,void*);
    using ArmPart=void (__fastcall*)(std::uint32_t*,char);
    inline static Bind bind_{};inline static Velocity velocity_{};inline static ArmPart arm_part_{};
    inline static void* bind_target_{};inline static void* velocity_target_{};inline static void* arm_target_{};
    inline static std::atomic<std::uintptr_t> keep_body_{};inline static std::atomic<unsigned> keep_mask_{};inline static std::atomic<std::uint64_t> keep_tick_{};
    inline static std::atomic<unsigned> depth_{};
    inline static SRWLOCK publication_lock_=SRWLOCK_INIT;
    inline static std::uint64_t publication_revision_{};inline static bool publication_exhausted_{};
    inline static Target data_{};
    inline static std::atomic<std::uintptr_t> rf_{},throw_entity_{};inline static std::atomic<std::uint64_t> throw_tick_{};
    inline static float throw_v_[3]{};

    template<std::size_t N> static bool code(std::uintptr_t at,const std::array<unsigned char,N>& expect) noexcept {
        std::array<unsigned char,N> got{};SIZE_T n{};
        return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(at),got.data(),N,&n) && n==N && got==expect;
    }
    static bool call_part(std::uintptr_t fn,std::uint32_t* id,char show) noexcept {
        __try {reinterpret_cast<void (__fastcall*)(std::uint32_t*,char)>(fn)(id,show);return true;}
        __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    static bool advance_publication_locked() noexcept {
        if(publication_exhausted_ || publication_revision_==(std::numeric_limits<std::uint64_t>::max)()) {
            publication_exhausted_=true;data_={};rf_.store(0,std::memory_order_release);return false;
        }
        ++publication_revision_;return true;
    }
    static bool snapshot(Target& out,std::uint64_t* revision=nullptr) noexcept {
        AcquireSRWLockShared(&publication_lock_);
        __try {
            if(publication_exhausted_ || !data_.rf || data_.rf!=rf_.load(std::memory_order_acquire))return false;
            out=data_;if(revision)*revision=publication_revision_;return true;
        } __finally {ReleaseSRWLockShared(&publication_lock_);}
    }
    static std::uintptr_t __fastcall bind_hook(void* rf,int mode,float* pos,float* axis,float* lin,float* ang,float dt) {
        ++depth_;
        const auto observe=bind_observer.load(std::memory_order_acquire);
        const auto binding=reinterpret_cast<std::uintptr_t>(rf);
        const auto caller=observe?reinterpret_cast<std::uintptr_t>(_ReturnAddress()):0;
        std::uintptr_t r{};
        if(observe)observe(0,binding,mode,caller,nullptr,nullptr,nullptr,nullptr,dt);
        __try {
            r=bind_(rf,mode,pos,axis,lin,ang,dt);
            if(observe)observe(1,binding,mode,caller,pos,axis,lin,ang,dt);
            if(reinterpret_cast<std::uintptr_t>(rf)==rf_.load(std::memory_order_acquire) && pos && axis) {
                if(observe)observe(4,binding,mode,caller,nullptr,nullptr,nullptr,nullptr,dt);
                place(rf,pos,axis,lin,ang,dt);
            }
            if(observe)observe(2,binding,mode,caller,pos,axis,lin,ang,dt);
        } __finally {
            if(observe)observe(3,binding,mode,caller,nullptr,nullptr,nullptr,nullptr,dt);
            --depth_;
        }
        return r;
    }
    static void place(void* rf,float* pos,float* axis,float* lin,float* ang,float dt) noexcept {
        Target t;std::uint64_t revision{};
        if(!snapshot(t,&revision) || t.rf!=reinterpret_cast<std::uintptr_t>(rf) || GetTickCount64()-t.tick>150)return;
        __try {
            if(!t.placement_serial || !NativeHeldPlacement::process_owner().current(t.placement_serial,t.source_side,
                t.entity,t.rf,t.pose_epoch,GetTickCount64())) return;
            const auto result=commit(t,revision,pos,axis,lin,ang,dt);
            if(result==2) (void)NativeHeldPlacement::process_owner().completed(t.placement_serial,
                t.source_side,t.entity,t.rf,t.pose_epoch,GetTickCount64());
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }
    static unsigned commit(const Target& t,std::uint64_t revision,float* pos,float* axis,float* lin,float* ang,float dt) {
        AcquireSRWLockExclusive(&publication_lock_);
        __try {
            if(publication_exhausted_ || revision!=publication_revision_ || !t.rf ||
               rf_.load(std::memory_order_acquire)!=t.rf || GetTickCount64()-t.tick>150)return 0;
            if(*reinterpret_cast<const std::uintptr_t*>(t.rf+0x10)!=t.entity)return 0;
            float cp[3],ca[9];std::memcpy(cp,reinterpret_cast<const void*>(t.entity+0x300),12);std::memcpy(ca,reinterpret_cast<const void*>(t.entity+0x30c),36);
            std::memcpy(pos,t.pos,12);std::memcpy(axis,t.axis,36);
            bool anchored_here{};
            if(const auto m=*reinterpret_cast<const std::uintptr_t*>(t.rf+0x3c8)) anchored_here=anchor(t,m);
            if(lin && ang && dt>1e-4f && dt<.2f) { // keyframed drive toward the hand (the post-physics call passes dt 1 with lin == ang)
                Vec3 v{(t.pos[0]-cp[0])/dt,(t.pos[1]-cp[1])/dt,(t.pos[2]-cp[2])/dt};v=grab_limit(v,25);
                WorldTrackedPose a{},b{};
                for(unsigned i=0;i<3;++i) {a.basis[i]={ca[i*3],ca[i*3+1],ca[i*3+2]};b.basis[i]={t.axis[i*3],t.axis[i*3+1],t.axis[i*3+2]};}
                const auto w=grab_limit(grab_rotation_error(a,b)*(1./dt),40);
                lin[0]=static_cast<float>(v.x);lin[1]=static_cast<float>(v.y);lin[2]=static_cast<float>(v.z);
                ang[0]=static_cast<float>(w.x);ang[1]=static_cast<float>(w.y);ang[2]=static_cast<float>(w.z);
            }
            ++placed;
            return anchored_here?2u:1u;
        } __finally {ReleaseSRWLockExclusive(&publication_lock_);}
    }
    // The drawn model follows the bind, not the physics body: HoldObject binds to the player's camera joint ("camera_tag"),
    // pinning the object by the eye. Rebind to the master itself (joint -1: origin = master+0x300 + offset x master axis rows,
    // axis = ToMat3(quat x,y,z,w) x master axis; 0x7506d0, 0x111410, 0xdce0) with the offset/rotation that land on the hand.
    static bool anchor(const Target& t,std::uintptr_t m) {
        float mo[3],ma[9],off[3],l[9];std::memcpy(mo,reinterpret_cast<const void*>(m+0x300),12);std::memcpy(ma,reinterpret_cast<const void*>(m+0x30c),36);
        for(unsigned i=0;i<3;++i) {
            off[i]=(t.pos[0]-mo[0])*ma[i*3]+(t.pos[1]-mo[1])*ma[i*3+1]+(t.pos[2]-mo[2])*ma[i*3+2];
            for(unsigned j=0;j<3;++j) l[i*3+j]=t.axis[i*3]*ma[j*3]+t.axis[i*3+1]*ma[j*3+1]+t.axis[i*3+2]*ma[j*3+2]; // L x M = T
        }
        if(!(std::abs(off[0])<4.f && std::abs(off[1])<4.f && std::abs(off[2])<4.f)) return false;
        const auto q=dvr::grab::quat_from_rot(l);const float xyzw[4]{q.x,q.y,q.z,q.w};
        auto* rf=reinterpret_cast<char*>(t.rf);
        *reinterpret_cast<std::int16_t*>(rf+0x3d0)=-1;std::memcpy(rf+0x3d4,off,12);std::memcpy(rf+0x3e0,xyzw,16);
        ++anchored;return true;
    }
    static void __fastcall arm_part_hook(std::uint32_t* part,char show) {
        ++depth_;
        const unsigned mask=keep_mask_.load(std::memory_order_acquire);const auto at=reinterpret_cast<std::uintptr_t>(part);
        const auto body=keep_body_.load(std::memory_order_relaxed);
        if(!show && mask && body && GetTickCount64()-keep_tick_.load(std::memory_order_relaxed)<=200 &&
           ((at==body+0x370 && (mask&1u)) || (at==body+0x680 && (mask&2u)))) {++hides_skipped;--depth_;return;}
        arm_part_(part,show);
        --depth_;
    }
    static std::uintptr_t __fastcall velocity_hook(void* rb,void* ev) {
        ++depth_;
        if(const auto e=throw_entity_.load(std::memory_order_acquire);e && rb && ev) toss(rb,ev,e);
        const auto r=velocity_(rb,ev);
        --depth_;return r;
    }
    static void toss(void* rb,void* ev,std::uintptr_t entity) noexcept {
        __try {
            if(*reinterpret_cast<const std::uintptr_t*>(static_cast<char*>(rb)+0x10)!=entity) return;
            if(GetTickCount64()-throw_tick_.load()>600) {throw_entity_.store(0);return;}
            auto* p=*reinterpret_cast<float**>(static_cast<char*>(ev)+8);
            if(!p || reinterpret_cast<const unsigned char*>(p)[0x19]) return; // additive impulse: not the throw
            throw_entity_.store(0);
            if(std::sqrt(p[0]*p[0]+p[1]*p[1]+p[2]*p[2])<=.15f) {++throw_kept;return;} // blocked: native keeps it by the eye
            for(unsigned i=0;i<3;++i) p[i]=throw_v_[i];
            ++thrown;
        } __except(EXCEPTION_EXECUTE_HANDLER) {throw_entity_.store(0);}
    }
};
} // namespace d2vr
