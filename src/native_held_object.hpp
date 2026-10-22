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
#include "held_release.hpp"
#include "layer_ledger.hpp"
#include "native_held_placement.hpp"
#include "physical_grab.hpp"
#include "../third_party/dishonored_vr/grab_pose.h"
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace d2vr {
struct NativeHeldReleased {std::uintptr_t entity{};std::uint32_t id{},handle{};};   // the object a real throw/drop handed back (release height watch)
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
        std::uint64_t placement_serial{},pose_epoch{};unsigned source_side{2};
        // The master's (player entity) origin/axis rows when the target was published: the bind tick re-bases the target on the
        // master's pose then, so locomotion and turns between the two (16-31 ms, headset v560/v561 receipts) do not trail it
        // behind the hand, which the arm upload already carries (full_body_driver.hpp anchored).
        float master_pos[3]{},master_axis[9]{};bool master_valid{};};
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

    // Counts targets refused by target_valid (publish, and commit after the master carry).
    inline static std::atomic<std::uint64_t> rejected_targets{};
    // The engine never normalizes the quaternion anchor() writes (ToMat3 0x111410 assumes unit length, NaN propagates into the
    // drawn transform), so only a finite, bounded, right-handed orthonormal pose may reach engine memory.
    static bool target_valid(const Target& t) noexcept {
        for(const float v:t.pos) if(!(std::isfinite(v) && std::abs(v)<1e5f)) return false;
        for(const float v:t.axis) if(!std::isfinite(v)) return false;
        const float* a=t.axis;
        for(unsigned i=0;i<3;++i) {
            const float* r=a+i*3;
            const float len=std::sqrt(r[0]*r[0]+r[1]*r[1]+r[2]*r[2]);
            if(!(std::abs(len-1.f)<1e-2f)) return false;
            const float* n=a+((i+1)%3)*3;
            if(!(std::abs(r[0]*n[0]+r[1]*n[1]+r[2]*n[2])<1e-2f)) return false;
        }
        const float det=a[0]*(a[4]*a[8]-a[5]*a[7])-a[1]*(a[3]*a[8]-a[5]*a[6])+a[2]*(a[3]*a[7]-a[4]*a[6]);
        return det>0.f;
    }
    // Render thread (single writer).
    // Returns whether the target was accepted (the carry diagnostics only count frames something was published for).
    static bool publish(const Target& t) noexcept {
        if(!target_valid(t)) {++rejected_targets;return false;}
        bool accepted{};
        AcquireSRWLockExclusive(&publication_lock_);
        __try {
            if(!advance_publication_locked())return false;
            data_=t;rf_.store(t.rf,std::memory_order_release);accepted=true;
        } __finally {ReleaseSRWLockExclusive(&publication_lock_);}
        return accepted;
    }
    // reason: 0 unattributed, 1 rf/entity gone, 2 placement refused, 3 no hold this frame, 4 tracking dropout (carry diagnostics; counted per reason).
    inline static std::array<std::atomic<std::uint64_t>,5> clear_reasons{};
    // Carry diagnostics C2: anchor() reads back what 0x753c10's rebind block would overwrite (joint, offset, rotation) before it writes again.
    inline static std::atomic<std::uint64_t> rebind_overwrites{};
    // Carry diagnostics C6: |entity+0x300 - the post-carry target| at the last bind tick (-1 = none since the last clear).
    inline static std::atomic<float> bind_entity_err{-2.f};
    static void clear(unsigned reason=0) noexcept {
        if(rf_.load(std::memory_order_acquire)) ++clear_reasons[reason<clear_reasons.size()?reason:0];   // only a publication being torn down counts; clear() also runs every no-hold frame
        layer_settled_.store(0,std::memory_order_release);
        if(!restore_hold_.load(std::memory_order_acquire)) layer_restore_.store(true,std::memory_order_release);   // a safe release in progress arms it itself (allow_restore)
        NativeHeldPlacement::process_owner().clear();
        AcquireSRWLockExclusive(&publication_lock_);
        __try {
            (void)advance_publication_locked();data_={};rf_.store(0,std::memory_order_release);last_write_rf_=0;bind_entity_err.store(-2.f,std::memory_order_relaxed);
        } __finally {ReleaseSRWLockExclusive(&publication_lock_);}
    }
    // Render thread: the next set-velocity event of this entity (within 600 ms) carries v (m/s, game world).
    // v: the hand's velocity (the caller no longer scales it); the hook keeps the engine's own speed and takes only v's direction
    // unless legacy_throw is set (build/legacy-throw), when the old x1.5 hand velocity replaces the engine's.
    inline static std::atomic<bool> legacy_throw{false};
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
    // The entity is still the one its handle names in the game's entity table.
    template<class Read> static bool live_entity(std::uintptr_t base,Read& read,std::uintptr_t entity) {
        const auto game=rig::value<std::uintptr_t>(read,base+0x39a5290);const auto id=entity?rig::value<std::uint32_t>(read,entity+0xa8):std::nullopt;
        return game && id && *id!=0x3fff0010 && (*id>>16)<0x3fff && rig::value<std::uintptr_t>(read,*game+0x3130b0+(*id>>16)*8ull)==entity;
    }
    // Game thread only: draw the held object in the world (render scale 0) so it shows where the hand holds it.
    template<class Read> static bool world_render(std::uintptr_t base,Read& read,std::uintptr_t entity) {
        static const bool verified=code(base+render_scale_rva,render_scale_code);
        if(!verified || !entity) return false;
        if(!live_entity(base,read,entity)) return false;
        const auto scale=rig::value<float>(read,entity+0xc8);if(!scale || *scale==0.f) return true;
        __try {reinterpret_cast<void (__fastcall*)(std::uintptr_t,float)>(base+render_scale_rva)(entity,0.f);} __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
        ++world_renders;return true;
    }
    // Game thread. Natively the hold keeps the object 1.05 m ahead of the eye; in the hand it sits by the player's capsule, and a throwable
    // on the RIGID_BODY layer (whale-oil tank: its decl inherits throwable_small's all-false block flags but overrides the layer; bottles
    // stay DEBRIS) collides with it there. For the hold it takes the DEBRIS layer, written the way 808310 (m_collideWithSword) writes
    // the body's filter word (pool record +0x6c, layer in the low 5 bits): 97600(game+0x60280, handle, filter, 1, 8a7820(game)).
    // Layers: the enum table 0x19f0c60 counts RIGID_BODY 3, DEBRIS 4, MOVER 5 (doors, live), PLAYER 6.
    inline static constexpr std::uintptr_t filter_rva=0x97600,physics_busy_rva=0x8a7820;
    inline static constexpr std::array<unsigned char,13> filter_code{0x48,0x83,0xec,0x48,0x80,0x7c,0x24,0x70,0x00,0x41,0x0f,0xb6,0xc1};
    inline static constexpr std::array<unsigned char,9> busy_code{0x80,0xb9,0x21,0x71,0x33,0x00,0x00,0x75,0x1c};
    inline static constexpr std::uint32_t layer_rigid_body=3,layer_debris=4;
    inline static bool (*filter_seam_)(std::uintptr_t,std::uint32_t,std::uint32_t){}; // checks only: stands in for the engine call
    inline static std::atomic<std::uint64_t> layers_swapped{},layers_restored{};
    inline static std::atomic<bool> layer_restore_{};
    // Safe release (held_clamp.hpp SafeRelease, Part 5A): while true the hold's end does not arm the layer restore, so the object keeps DEBRIS until the
    // render thread has seen it clear of the world and says allow_restore(); the restore itself still runs through the ledger (identity re-read before the
    // write, exactly once). A hold that starts again cancels it; a render thread that never answers is overtaken after restore_hold_max_ms.
    inline static std::atomic<bool> restore_hold_{};inline static std::atomic<std::uint64_t> restore_hold_since_{};
    static constexpr std::uint64_t restore_hold_max_ms=2000;
    static void begin_safe_release() noexcept {layer_restore_.store(false,std::memory_order_release);restore_hold_since_.store(GetTickCount64(),std::memory_order_release);restore_hold_.store(true,std::memory_order_release);}
    static void allow_restore() noexcept {if(restore_hold_.exchange(false,std::memory_order_acq_rel)) layer_restore_.store(true,std::memory_order_release);}
    static void cancel_safe_release() noexcept {restore_hold_.store(false,std::memory_order_release);}
    // Settled mirror (any thread): the entity whose layer needs nothing more this hold - swapped, or checked and not RIGID_BODY. clear() and
    // restore_layer() reset it, so a transient clear() makes the publisher ask for the service again even though the render scale is already 0.
    inline static std::atomic<std::uintptr_t> layer_settled_{};
    static bool layer_settled(std::uintptr_t entity) noexcept {return entity && layer_settled_.load(std::memory_order_acquire)==entity;}
    template<class Read> static bool debris_layer(std::uintptr_t base,Read& read,std::uintptr_t entity) {
        if(layer_entity_ && layer_entity_!=entity) {ledger_.released(layer_entity_);layer_entity_=0;}   // its restore stays pending; a new grab never overwrites it
        if(layer_entity_==entity) {layer_settled_.store(entity,std::memory_order_release);return true;}
        const auto body=throwable_body_full(base,read,entity);
        if(body && (body->filter&0x1f)!=layer_rigid_body) {layer_settled_.store(entity,std::memory_order_release);return false;}   // a bottle: nothing to write
        const auto id=rig::value<std::uint32_t>(read,entity+0xa8);
        if(!body || !body->record_ok || !body->identity_ok || !id || !set_filter(base,read,body->handle,(body->filter&~0x1fu)|layer_debris)) return false;
        LayerEntry e;e.entity=entity;e.entity_id=*id;e.handle=body->handle;e.filter=body->filter;e.generation=body->identity.generation;e.flags=body->identity.flags;
        layer_entity_=entity;swap_entry_=e;++layers_swapped;ledger_.swapped(e);layer_settled_.store(entity,std::memory_order_release);return true;
    }
    // The held entity's body as a world cast must be told to skip it (held_clamp.hpp sweeps): the same single throwable component the layer swap writes
    // (the generic prop mapping of native_cast_exclusion does not find a HoldObject item, and its sweeps then reported the object itself).
    struct ExclusionBody {std::uintptr_t record{},world{};std::uint32_t handle{},filter{};};
    template<class Read> static std::optional<ExclusionBody> exclusion_body(std::uintptr_t base,Read& read,std::uintptr_t entity) {
        const auto body=throwable_body_full(base,read,entity);
        if(!body || !body->record_ok || !body->identity_ok || !body->record || !body->filter) return {};
        return ExclusionBody{body->record,body->world,body->handle,body->filter};
    }
    // Any thread: the pose epoch (level load, session change). The game thread gives every pending restore up without writing.
    inline static std::atomic<std::uint64_t> epoch_seen_{};
    static void note_epoch(std::uint64_t epoch) noexcept {epoch_seen_.store(epoch,std::memory_order_release);}
    // Any thread: the hold ended by a real throw or drop of this entity (not a transient clear). The watch starts when its restore lands.
    inline static std::atomic<std::uintptr_t> real_release_{};
    static void note_real_release(std::uintptr_t entity) noexcept {real_release_.store(entity,std::memory_order_release);}
    // Game thread, every frame: pending restores run (retried until the budget is spent), then, while an entity is held, it takes the
    // DEBRIS layer again. The layer is serviced apart from the render scale, which is 0 after the first request.
    template<class Read> static void service_hold_layers(std::uintptr_t base,Read& read,std::uintptr_t entity) {
        const auto epoch=epoch_seen_.load(std::memory_order_acquire);
        if(epoch!=layer_epoch_) {
            layer_epoch_=epoch;
            // A recenter or an XR suspend/resume changes the epoch without reloading the level: an entry still on DEBRIS (a held object, or a released one
            // waiting for its safe release) is restored through the ledger first, its identity re-read before the write (a level load fails that and the
            // entry is given up); only what could not be restored is abandoned, so a live object is never left on DEBRIS.
            if(ledger_.pending()) {
                const auto before=ledger_.abandoned();
                if(layer_entity_) {ledger_.released(layer_entity_);layer_entity_=0;}
                restore_pending(base,read);
                if(ledger_.pending()) ledger_.clear_all();
                layers_abandoned+=ledger_.abandoned()-before;
            }
            layer_entity_=0;layer_settled_.store(0,std::memory_order_release);real_release_.store(0);released_ = {};restore_hold_.store(false,std::memory_order_release);
        }
        if(restore_hold_.load(std::memory_order_acquire) && GetTickCount64()-restore_hold_since_.load(std::memory_order_acquire)>restore_hold_max_ms) allow_restore();
        if(layer_restore_.exchange(false)) restore_layer(base,read);else restore_pending(base,read);
        if(entity && live_entity(base,read,entity)) debris_layer(base,read,entity);
        else if(restore_hold_.load(std::memory_order_acquire) && layer_entity_) reassert_debris(base,read);
    }
    // While a safe release is pending the object must stay DEBRIS: the engine's own drop writes RIGID_BODY back (sim 2026-10-22: layer_at_release 3), which
    // would put a body that still overlaps the floor back among the solid ones. Written only to the very object that was swapped (the ledger's identity
    // re-read first), and only while the release waits.
    inline static std::atomic<std::uint64_t> layers_reasserted{};
    template<class Read> static void reassert_debris(std::uintptr_t base,Read& read) {
        if(!layer_entity_ || swap_entry_.entity!=layer_entity_) return;
        const auto body=throwable_body_full(base,read,layer_entity_);
        if(!body || (body->filter&0x1f)!=layer_rigid_body || probe_entry(base,read,swap_entry_)!=LayerLedger::Probe::match) return;
        if(set_filter(base,read,body->handle,(body->filter&~0x1fu)|layer_debris)) ++layers_reasserted;
    }
    // The hold ended (clear() asks): the held entry becomes releasable and every pending restore runs. True when a restore landed in this call.
    template<class Read> static bool restore_layer(std::uintptr_t base,Read& read) {
        const auto before=layers_restored.load();
        if(layer_entity_) {ledger_.released(layer_entity_);layer_entity_=0;layer_settled_.store(0,std::memory_order_release);}
        restore_pending(base,read);
        return layers_restored.load()>before;
    }
    // The object a restore may write: same entity id and registry slot, same body component identity, same pool handle and record self-handle.
    template<class Read> static LayerLedger::Probe probe_entry(std::uintptr_t base,Read& read,const LayerEntry& e) {
        const auto id=rig::value<std::uint32_t>(read,e.entity+0xa8);
        const auto game=rig::value<std::uintptr_t>(read,base+0x39a5290);
        if(!id || !game || !rig::pointer(*game)) return LayerLedger::Probe::unreadable;
        if(*id!=e.entity_id || *id==0x3fff0010 || (*id>>16)>=0x3fff) return LayerLedger::Probe::mismatch;
        const auto slot=rig::value<std::uintptr_t>(read,*game+0x3130b0+(*id>>16)*8ull);
        if(!slot) return LayerLedger::Probe::unreadable;
        if(*slot!=e.entity) return LayerLedger::Probe::mismatch;
        const auto body=throwable_body_full(base,read,e.entity);
        if(!body) return LayerLedger::Probe::unreadable;
        if(!body->record_ok || !body->identity_ok || body->handle!=e.handle || body->identity.generation!=e.generation || body->identity.flags!=e.flags) return LayerLedger::Probe::mismatch;
        return LayerLedger::Probe::match;
    }
    template<class Read> static void restore_pending(std::uintptr_t base,Read& read) {
        if(!ledger_.pending()) return;
        const auto before=ledger_.abandoned();
        ledger_.service(
            [&](const LayerEntry& e){return probe_entry(base,read,e);},
            [&](const LayerEntry& e){
                const auto body=throwable_body_full(base,read,e.entity);   // the final identity re-read, immediately before the write
                if(!body || probe_entry(base,read,e)!=LayerLedger::Probe::match) return LayerLedger::Write::mismatch;
                const bool ok=set_filter(base,read,body->handle,(body->filter&~0x1fu)|(e.filter&0x1fu));
                if(!ok) ++layer_restore_misses;
                return ok?LayerLedger::Write::ok:LayerLedger::Write::failed;
            },
            [&](const LayerEntry& e,bool restored){
                if(restored) {
                    ++layers_restored;
                    // The release watch follows only a real throw or drop of this very entity.
                    if(real_release_.load(std::memory_order_acquire)==e.entity) {real_release_.store(0);released_={e.entity,e.entity_id,e.handle};released_pending_.store(true,std::memory_order_release);}
                }
            });
        layers_abandoned+=ledger_.abandoned()-before;
    }
    using Released=NativeHeldReleased;
    inline static Released released_{};inline static std::atomic<bool> released_pending_{};
    inline static std::uint64_t layer_epoch_{};
    inline static LayerLedger ledger_{};
    inline static std::atomic<std::uint64_t> layers_abandoned{},layer_restore_misses{};
    static std::string layer_json() {
        char b[256];std::snprintf(b,sizeof(b),"{\"swaps\":%llu,\"restores\":%llu,\"abandoned\":%llu,\"misses\":%llu,\"pending\":%u,\"restores_per_release\":%.3f}",
            (unsigned long long)layers_swapped.load(),(unsigned long long)layers_restored.load(),(unsigned long long)layers_abandoned.load(),
            (unsigned long long)layer_restore_misses.load(),ledger_.pending(),
            (layers_restored.load()+layers_abandoned.load())?double(layers_restored.load())/double(layers_restored.load()+layers_abandoned.load()):0.);
        return b;
    }
    // Diagnostics, read only: the collision layer (filter word & 0x1f) of the held entity's throwable body; 3 RIGID_BODY, 4 DEBRIS.
    // The cache keeps the body handle per entity: the component scan runs once per hold, then only the pool record is read.
    struct LayerCache {std::uintptr_t entity{},component{};std::uint32_t handle{};};   // keyed on entity + body handle (+ the component that owns it)
    template<class Read> static std::optional<std::uint32_t> held_layer(std::uintptr_t base,Read& read,std::uintptr_t entity) {
        const auto body=entity?throwable_body(base,read,entity):std::nullopt;
        if(!body) return std::nullopt;
        return body->second&0x1fu;
    }
    template<class Read> static std::optional<std::uint32_t> held_layer(std::uintptr_t base,Read& read,std::uintptr_t entity,LayerCache& cache) {
        using namespace rig;
        if(!entity) return std::nullopt;
        // Still the same body? The component must still belong to the entity and still hold the cached handle; otherwise rescan.
        if(cache.entity!=entity || !cache.handle || !cache.component ||
           value<std::uintptr_t>(read,cache.component+0x10)!=entity || value<std::uint32_t>(read,cache.component+0xf8)!=cache.handle) {
            cache={};std::uintptr_t component{};const auto body=throwable_body(base,read,entity,&component);
            if(!body) return std::nullopt;
            cache={entity,component,body->first};return body->second&0x1fu;
        }
        const auto game=value<std::uintptr_t>(read,base+0x39a5290);const auto world=game&&pointer(*game)?value<std::uintptr_t>(read,*game+0x60410):std::nullopt;
        const auto pool=world&&pointer(*world)?value<std::uintptr_t>(read,*world+0x28):std::nullopt;const auto count=world&&pointer(*world)?value<std::uint32_t>(read,*world+0x30):std::nullopt;
        const auto index=cache.handle&0xffffff;
        const auto filter=pool&&count&&pointer(*pool)&&index<*count?value<std::uint32_t>(read,*pool+index*0xb0+0x6c):std::nullopt;
        if(!filter) {cache={};return std::nullopt;}
        return *filter&0x1fu;
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
    static Target data_; // defined after the class: clang-cl needs Target's member initializers complete
    inline static std::atomic<std::uintptr_t> rf_{},throw_entity_{};inline static std::atomic<std::uint64_t> throw_tick_{};
    inline static float throw_v_[3]{};
    inline static std::uintptr_t layer_entity_{}; // game thread
    inline static LayerEntry swap_entry_{};       // game thread: the entry debris_layer last put in the ledger
    // The throwable rigid body of a held entity: its physics handle and the filter word of its pool record.
    struct Body {std::uint32_t handle{},filter{};std::uintptr_t component{};rig::Identity identity{};bool record_ok{},identity_ok{};std::uintptr_t record{},world{};};
    template<class Read> static std::optional<Body> throwable_body_full(std::uintptr_t base,Read& read,std::uintptr_t entity) {
        using namespace rig;
        const auto game=value<std::uintptr_t>(read,base+0x39a5290);const auto entries=entity?list(read,entity+0x268,256):std::nullopt;
        if(!game || !pointer(*game) || !entries) return {};
        std::uintptr_t component{};
        for(unsigned i=0;i<entries->packed_count;++i) {
            const auto at=value<std::uintptr_t>(read,entries->data+i*8);
            if(at && (typed(read,base,*at,0x1c745c8) || typed(read,base,*at,0x1b64b68))) {if(component) return {};component=*at;}
        }
        const auto handle=component?value<std::uint32_t>(read,component+0xf8):std::nullopt;const auto world=value<std::uintptr_t>(read,*game+0x60410);
        if(!handle || !*handle || !world || !pointer(*world) || value<std::uintptr_t>(read,component+0x10)!=entity) return {};
        const auto pool=value<std::uintptr_t>(read,*world+0x28);const auto count=value<std::uint32_t>(read,*world+0x30);const auto index=*handle&0xffffff;
        if(!pool || !pointer(*pool) || !count || *count>0x100000 || index==0xffffff || index>=*count) return {};
        const auto record=*pool+index*0xb0;
        const auto filter=value<std::uint32_t>(read,record+0x6c);if(!filter) return {};
        const auto first=value<std::uint32_t>(read,record+0x70),second=value<std::uint32_t>(read,record+0x74);
        const auto identity=started(read,component);
        return Body{*handle,*filter,component,identity?*identity:Identity{},first && second && *first==*handle && *second==*handle,identity.has_value(),record,*world};
    }
    template<class Read> static std::optional<std::pair<std::uint32_t,std::uint32_t>> throwable_body(std::uintptr_t base,Read& read,std::uintptr_t entity,std::uintptr_t* component_out=nullptr) {
        const auto b=throwable_body_full(base,read,entity);if(!b) return {};
        if(component_out) *component_out=b->component;
        return std::pair<std::uint32_t,std::uint32_t>{b->handle,b->filter};
    }
    template<class Read> static bool set_filter(std::uintptr_t base,Read& read,std::uint32_t handle,std::uint32_t filter) {
        const auto game=rig::value<std::uintptr_t>(read,base+0x39a5290);if(!game || !rig::pointer(*game)) return false;
        if(filter_seam_) return filter_seam_(*game,handle,filter);
        static const bool verified=code(base+filter_rva,filter_code) && code(base+physics_busy_rva,busy_code);
        if(!verified) return false;
        return call_filter(base,*game,handle,filter);
    }
    static bool call_filter(std::uintptr_t base,std::uintptr_t game,std::uint32_t handle,std::uint32_t filter) noexcept {
        __try {
            const char busy=reinterpret_cast<char (__fastcall*)(std::uintptr_t)>(base+physics_busy_rva)(game);
            reinterpret_cast<void (__fastcall*)(std::uintptr_t,std::uint32_t,std::uint32_t,char,char)>(base+filter_rva)(game+0x60280,handle,filter,1,busy);
            return true;
        } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }

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
    static unsigned commit(const Target& published,std::uint64_t revision,float* pos,float* axis,float* lin,float* ang,float dt) {
        AcquireSRWLockExclusive(&publication_lock_);
        __try {
            if(publication_exhausted_ || revision!=publication_revision_ || !published.rf ||
               rf_.load(std::memory_order_acquire)!=published.rf || GetTickCount64()-published.tick>150)return 0;
            if(*reinterpret_cast<const std::uintptr_t*>(published.rf+0x10)!=published.entity)return 0;
            Target t=published;
            if(t.master_valid) if(const auto m=*reinterpret_cast<const std::uintptr_t*>(t.rf+0x3c8)) carry(t,m);
            if(!target_valid(t)) {++rejected_targets;return 0;}
            float cp[3],ca[9];std::memcpy(cp,reinterpret_cast<const void*>(t.entity+0x300),12);std::memcpy(ca,reinterpret_cast<const void*>(t.entity+0x30c),36);
            std::memcpy(pos,t.pos,12);std::memcpy(axis,t.axis,36);
            bind_entity_err.store(std::sqrt((cp[0]-t.pos[0])*(cp[0]-t.pos[0])+(cp[1]-t.pos[1])*(cp[1]-t.pos[1])+(cp[2]-t.pos[2])*(cp[2]-t.pos[2])),std::memory_order_relaxed);
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
    // Move the target by the master's motion since publication: local = inverse(master then) x target, target = master now x local.
    static void carry(Target& t,std::uintptr_t m) {
        float mo[3],ma[9],off[3],l[9];std::memcpy(mo,reinterpret_cast<const void*>(m+0x300),12);std::memcpy(ma,reinterpret_cast<const void*>(m+0x30c),36);
        const auto& po=t.master_pos;const auto& pa=t.master_axis;
        for(unsigned i=0;i<3;++i) {
            off[i]=(t.pos[0]-po[0])*pa[i*3]+(t.pos[1]-po[1])*pa[i*3+1]+(t.pos[2]-po[2])*pa[i*3+2];
            for(unsigned j=0;j<3;++j) l[i*3+j]=t.axis[i*3]*pa[j*3]+t.axis[i*3+1]*pa[j*3+1]+t.axis[i*3+2]*pa[j*3+2];
        }
        for(unsigned i=0;i<3;++i) {
            t.pos[i]=mo[i]+off[0]*ma[i]+off[1]*ma[3+i]+off[2]*ma[6+i];
            for(unsigned j=0;j<3;++j) t.axis[i*3+j]=l[i*3]*ma[j]+l[i*3+1]*ma[3+j]+l[i*3+2]*ma[6+j];
        }
    }
    // The drawn model follows the bind, not the physics body: HoldObject binds to the player's camera joint ("camera_tag"),
    // pinning the object by the eye. Rebind to the master itself (joint -1: origin = master+0x300 + offset x master axis rows,
    // axis = ToMat3(quat x,y,z,w) x master axis; 0x7506d0, 0x111410, 0xdce0) with the offset/rotation that land on the hand.
    // Did the bind fields move off what anchor() wrote? Offset beyond 1 mm, or rotation beyond 0.5 degrees (|dot| makes -q the same rotation).
    static bool rebind_changed(const float o[3],const float q[4],const float last_o[3],const float last_q[4]) noexcept {
        for(unsigned i=0;i<3;++i) if(!(std::abs(o[i]-last_o[i])<=1e-3f)) return true;
        double dot=0,a=0,b=0;
        for(unsigned i=0;i<4;++i) {dot+=double(q[i])*last_q[i];a+=double(q[i])*q[i];b+=double(last_q[i])*last_q[i];}
        if(!(a>0) || !(b>0) || !std::isfinite(dot)) return true;
        const double c=std::abs(dot)/std::sqrt(a*b);
        return !(c>=0.99999048);   // cos(0.25 degrees): a 0.5 degree rotation
    }
    static bool anchor(const Target& t,std::uintptr_t m) {
        float mo[3],ma[9],off[3],l[9];std::memcpy(mo,reinterpret_cast<const void*>(m+0x300),12);std::memcpy(ma,reinterpret_cast<const void*>(m+0x30c),36);
        for(unsigned i=0;i<3;++i) {
            off[i]=(t.pos[0]-mo[0])*ma[i*3]+(t.pos[1]-mo[1])*ma[i*3+1]+(t.pos[2]-mo[2])*ma[i*3+2];
            for(unsigned j=0;j<3;++j) l[i*3+j]=t.axis[i*3]*ma[j*3]+t.axis[i*3+1]*ma[j*3+1]+t.axis[i*3+2]*ma[j*3+2]; // L x M = T
        }
        if(!(std::abs(off[0])<4.f && std::abs(off[1])<4.f && std::abs(off[2])<4.f)) return false;
        const auto q=dvr::grab::quat_from_rot(l);float xyzw[4]{q.x,q.y,q.z,q.w};
        const float norm=std::sqrt(xyzw[0]*xyzw[0]+xyzw[1]*xyzw[1]+xyzw[2]*xyzw[2]+xyzw[3]*xyzw[3]);
        for(const float c:xyzw) if(!std::isfinite(c)) return false;
        if(!(std::abs(norm-1.f)<1e-3f)) return false;
        for(float& c:xyzw) c/=norm;
        auto* rf=reinterpret_cast<char*>(t.rf);
        // Read-back (diagnostics only, serialized by publication_lock_ with the previous write): did anything change the fields since we wrote them?
        if(last_write_rf_==t.rf) {
            // 0x753c10 writes these without our lock: read twice and judge only a stable pair; a recompute that differs by rounding or the -q sign is the same bind.
            std::int16_t joint{},joint2{};float back_o[3],back_q[4],back_o2[3],back_q2[4];
            std::memcpy(&joint,rf+0x3d0,2);std::memcpy(back_o,rf+0x3d4,12);std::memcpy(back_q,rf+0x3e0,16);
            std::memcpy(&joint2,rf+0x3d0,2);std::memcpy(back_o2,rf+0x3d4,12);std::memcpy(back_q2,rf+0x3e0,16);
            if(joint==joint2 && std::memcmp(back_o,back_o2,12)==0 && std::memcmp(back_q,back_q2,16)==0 && (joint!=-1 || rebind_changed(back_o,back_q,last_off_,last_quat_))) ++rebind_overwrites;
        }
        *reinterpret_cast<std::int16_t*>(rf+0x3d0)=-1;std::memcpy(rf+0x3d4,off,12);std::memcpy(rf+0x3e0,xyzw,16);
        std::memcpy(last_off_,off,12);std::memcpy(last_quat_,xyzw,16);last_write_rf_=t.rf;
        ++anchored;return true;
    }
    inline static std::uintptr_t last_write_rf_{};inline static float last_off_[3]{},last_quat_[4]{};   // the last anchor write (under publication_lock_)
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
            const auto out=held_throw_velocity({throw_v_[0],throw_v_[1],throw_v_[2]},{p[0],p[1],p[2]},legacy_throw.load(std::memory_order_relaxed));
            if(!out.ok) {++throw_kept;return;}
            for(unsigned i=0;i<3;++i) p[i]=static_cast<float>(out.v[i]);
            ++thrown;
        } __except(EXCEPTION_EXECUTE_HANDLER) {throw_entity_.store(0);}
    }
};
inline NativeHeldObject::Target NativeHeldObject::data_{};
} // namespace d2vr
