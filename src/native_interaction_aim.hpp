#pragma once
#include "interaction_ray.hpp"
#include "selection_policy.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
// The standalone pure-logic check defines D2VR_NATIVE_INTERACTION_PURE: it then
// sees only the routing policy and hit point, which need neither windows.h nor
// the hook headers (the check command adds no /Ithird_party\minhook\include).
#ifndef D2VR_NATIVE_INTERACTION_PURE
#include <windows.h>
#if __has_include(<MinHook.h>)
#include <MinHook.h>
#else
// The DLL build adds /Ithird_party\minhook\include. A check without /I names the
// three entry points and stubs them, exactly as tests\native_lean_query_trace_check.cpp does.
typedef enum MH_STATUS {
    MH_UNKNOWN=-1,MH_OK=0,MH_ERROR_ALREADY_INITIALIZED,MH_ERROR_NOT_INITIALIZED,MH_ERROR_ALREADY_CREATED,
    MH_ERROR_NOT_CREATED,MH_ERROR_ENABLED,MH_ERROR_DISABLED,MH_ERROR_NOT_EXECUTABLE,MH_ERROR_UNSUPPORTED_FUNCTION,
    MH_ERROR_MEMORY_ALLOC,MH_ERROR_MEMORY_PROTECT,MH_ERROR_MODULE_NOT_FOUND,MH_ERROR_FUNCTION_NOT_FOUND
} MH_STATUS;
extern "C" MH_STATUS WINAPI MH_CreateHook(LPVOID,LPVOID,LPVOID*);
extern "C" MH_STATUS WINAPI MH_EnableHook(LPVOID);
extern "C" MH_STATUS WINAPI MH_DisableHook(LPVOID);
#endif
#include <intrin.h>
#include "native_player_camera.hpp"
#include "native_grab.hpp"
#include "native_world_trace.hpp"
#include "native_cast_exclusion.hpp"
#include "anatomical_world_contact.hpp"
#include "anatomical_raw_pair.hpp"
#include "native_full_hand_contact.hpp"
#include "native_console.hpp"
#include "native_melee.hpp"
#include "native_sword_contact.hpp"
#include "native_held_object.hpp"
#include "native_weapon_hands.hpp"
#include "native_door_interaction.hpp"
#include "native_slash_scene_rate.hpp"
#include "native_takedown.hpp"
#include "native_choke_distance_scope.hpp"
#include "native_offered_choke.hpp"
#include "native_talk_offer.hpp"
#include "native_character_gaze.hpp"
#include "sword_contact_policy.hpp"
#include "takedown_range_policy.hpp"
#include <vector>
#include <string>
#include <vector>
#include "native_hand_volume.hpp"
#include "native_lean_query_trace.hpp"
#include "native_lean_lifecycle.hpp"
#include <atomic>
#include <fstream>
#include <filesystem>
#include <mutex>
#include <iomanip>
#include <sstream>
#include <limits>
#include "crash_observer.hpp"
#endif

namespace d2vr {
// Select once per publication dwell, never independently inside worker stages.
class InteractionHandRouting {
    int locked_{-1},selected_{-1},last_{-1};unsigned cycle_{};
    std::uint64_t until_{},revision_{},last_tick_{},probe_at_{};
    std::uint64_t ownership_revision_{1};
public:
    void invalidate_owner() noexcept {if(ownership_revision_)++ownership_revision_;}
    std::uint64_t ownership_revision() const noexcept {return ownership_revision_;}
    void lock(int side) noexcept {
        if(locked_<0 && (side==0||side==1)) {locked_=selected_=side;until_=0;++revision_;invalidate_owner();}
    }
    void unlock() noexcept {if(locked_>=0) {locked_=-1;until_=0;++revision_;invalidate_owner();}}
    int locked() const noexcept {return locked_;}
    int selected() const noexcept {return selected_;}
    std::uint64_t revision() const noexcept {return revision_;}
    // moving: the hand moved recently (aiming at something new). Unknown motion counts as moving.
    int next(const std::array<bool,2>& published,std::uint64_t now,bool qualified_selected=false,std::array<bool,2> moving={true,true}) noexcept {
        int next=-1;
        if(qualified_selected && selected_>=0) {last_=selected_;last_tick_=now;}
        const bool both=published[0] && published[1];
        if(locked_>=0) next=published[locked_]?locked_:-1;
        else if(qualified_selected && selected_>=0 && published[selected_]) {
            // Fairness: a lowered hand that keeps a target must not starve the other. With both hands
            // published, the other hand gets one dwell every 600 ms, but only while it moves: probing a
            // resting hand made the held target (a guard's choke prompt) blink every 600 ms.
            if(both && moving[1-selected_] && now>=probe_at_+600) next=1-selected_;
            else {until_=now+120;return selected_;}
        }
        // Searching: the hand that is moving is the one aiming, so it gets every query (no 120 ms ping-pong).
        else if(both && moving[0]!=moving[1]) next=moving[0]?0:1;
        else if(selected_>=0 && published[selected_] && now<until_) return selected_;
        // Sticky: after losing a target, search the hand that last had one before ping-ponging.
        else if(published[0]&&published[1]) next=last_>=0 && now>=last_tick_ && now-last_tick_<=400?last_:static_cast<int>(cycle_++&1u);
        else if(published[0]) next=0;
        else if(published[1]) next=1;
        if(next!=selected_) {selected_=next;probe_at_=now;++revision_;}
        until_=now+120;
        return selected_;
    }
    void reset() noexcept {locked_=selected_=last_=-1;cycle_=0;last_tick_=0;until_=0;probe_at_=0;++revision_;invalidate_owner();}
};
// 0x6c6760 returns a low-byte bool, an entity node, distance and normal.
// The entity node is never a float3; reconstruct the point from the routed ray.
inline std::optional<Vec3> interaction_hit_point(const InteractionRay& ray,std::uintptr_t result,
        std::optional<double> distance) {
    if(!(result&0xff) || !distance || !std::isfinite(*distance) || *distance<0 || *distance>10000) return {};
    const Vec3 origin{ray.f[0],ray.f[1],ray.f[2]},direction{ray.f[4],ray.f[5],ray.f[6]};
    const double extent=length(direction);
    if(!finite(origin) || !std::isfinite(extent) || extent<1e-6) return {};
    return origin+direction*(*distance/extent);
}
inline std::optional<std::array<float,4>> interaction_hand_range_origin(Vec3 hand,Vec3 native_eye) noexcept {
    // Same bounded hand reach as controller_interaction_ray. Native descriptor
    // range/height/facing checks consume this origin without changing the hit.
    if(!finite(hand) || !finite(native_eye) || length(hand-native_eye)>1.5)return {};
    std::array<float,4> out{static_cast<float>(hand.x),static_cast<float>(hand.y),static_cast<float>(hand.z),0};
    for(const auto value:out)if(!std::isfinite(value))return {};
    return out;
}
// Native sensor jobs and direct/queued rays, verified in the pinned PE/Ghidra.
// Retarget only local query arguments. Never write shared native camera aim.
#ifndef D2VR_NATIVE_INTERACTION_PURE
class NativeInteractionAim {
    friend struct NativeInteractionRangeFixture;
    friend struct NativeInteractionStopFixture;
    friend struct NativeCharacterGazeFixture;
    AnatomicalWorldContact anatomical_contact_;
    NativeFullHandContact full_hand_contact_;
    std::atomic<bool> full_hand_enabled_{};
    std::atomic<std::uint64_t> anatomical_cast_count_{},anatomical_cast_ticks_{};
    using Stage=void (__fastcall*)(void*);
    using Direct=std::uintptr_t (__fastcall*)(void*,const InteractionRay*,std::uintptr_t*,float*,void*);
    using Fetch=std::uintptr_t (__fastcall*)(void*,void*,void*);
    using Occlusion=std::uintptr_t (__fastcall*)(void*,void*,const void*,const void*,const void*,unsigned,unsigned char*);
    using Sort=void (__fastcall*)(void*,unsigned,int,unsigned char);
    using Queue=void* (__fastcall*)(void*,void*,void*,const InteractionRay*,UINT,UINT);
    using RangeOrigin=const float* (__fastcall*)(void*);
    using Read=bool (*)(std::uintptr_t,void*,std::size_t);
    using Logic=UINT (*)();
    // reach: the game's own Use distance along this ray (a gaze Use), not the grab-sized hand volume.
    struct Publication {std::uint64_t tick{},epoch{};std::uintptr_t player{},camera{};int generation{};WorldTrackedPose hand{};bool active{};Vec3 volume_centre{};bool reach{};std::optional<WorldTrackedPose> grip;std::optional<rig::NativeCarrySnapshot> carry;};
    struct Contact {
        std::uintptr_t owner{},sensor{},world{},component{};unsigned spawn{};
        rig::Identity sensor_identity{},camera_identity{},component_identity{};
        Vec3 point{};std::uint64_t tick{},epoch{},revision{},publication_tick{};
    };
    struct Context {NativePlayerCamera camera;Publication hand;int side;unsigned stage;std::uintptr_t sensor;std::uint64_t revision;
        rig::Identity sensor_identity{};
        mutable std::array<Contact,6> contacts{};mutable unsigned contact_count{};mutable bool contacts_overflow{};std::uint64_t ownership_revision{};};
    struct Retrieved {std::uintptr_t data{},world{};unsigned count{};std::uintptr_t preferred{};const Context* scope{};bool ranked{};};
    static thread_local Retrieved retrieved_; // defined after the class: clang-cl needs Retrieved's member initializers complete
    inline static NativeInteractionAim* instance_{};
    inline static thread_local const Context* context_{};
    inline static thread_local const NativeLeanQuerySnapshot* lean_context_{};
    inline static thread_local NativeLeanQueryTransaction* lean_transaction_{};
    inline static constexpr std::array<std::uintptr_t,6> rvas{0x3b8430,0x3b8d60,0x3b9650,0x6c6760,0x95280,0x95550};
    inline static constexpr std::array<std::array<std::byte,32>,6> expected{{
{std::byte{0x48},std::byte{0x8b},std::byte{0xc4},std::byte{0x55},std::byte{0x53},std::byte{0x56},std::byte{0x57},std::byte{0x41},std::byte{0x54},std::byte{0x41},std::byte{0x55},std::byte{0x41},std::byte{0x56},std::byte{0x41},std::byte{0x57},std::byte{0x48},std::byte{0x8d},std::byte{0xa8},std::byte{0x98},std::byte{0xfe},std::byte{0xff},std::byte{0xff},std::byte{0x48},std::byte{0x81},std::byte{0xec},std::byte{0x28},std::byte{0x02},std::byte{0x00},std::byte{0x00},std::byte{0x48},std::byte{0xc7},std::byte{0x45}},
{std::byte{0x48},std::byte{0x8b},std::byte{0xc4},std::byte{0x55},std::byte{0x48},std::byte{0x8d},std::byte{0xa8},std::byte{0x38},std::byte{0xff},std::byte{0xff},std::byte{0xff},std::byte{0x48},std::byte{0x81},std::byte{0xec},std::byte{0xc0},std::byte{0x01},std::byte{0x00},std::byte{0x00},std::byte{0x80},std::byte{0x79},std::byte{0x38},std::byte{0x00},std::byte{0x0f},std::byte{0x84},std::byte{0x76},std::byte{0x03},std::byte{0x00},std::byte{0x00},std::byte{0x48},std::byte{0x89},std::byte{0x58},std::byte{0x18}},
{std::byte{0x48},std::byte{0x8b},std::byte{0xc4},std::byte{0x55},std::byte{0x56},std::byte{0x48},std::byte{0x8d},std::byte{0xa8},std::byte{0x38},std::byte{0xff},std::byte{0xff},std::byte{0xff},std::byte{0x48},std::byte{0x81},std::byte{0xec},std::byte{0xb8},std::byte{0x01},std::byte{0x00},std::byte{0x00},std::byte{0x80},std::byte{0x79},std::byte{0x38},std::byte{0x00},std::byte{0x48},std::byte{0x8b},std::byte{0xf1},std::byte{0x0f},std::byte{0x84},std::byte{0x28},std::byte{0x04},std::byte{0x00},std::byte{0x00}},
{std::byte{0x48},std::byte{0x89},std::byte{0x5c},std::byte{0x24},std::byte{0x18},std::byte{0x55},std::byte{0x56},std::byte{0x57},std::byte{0x48},std::byte{0x81},std::byte{0xec},std::byte{0xb0},std::byte{0x00},std::byte{0x00},std::byte{0x00},std::byte{0x80},std::byte{0x39},std::byte{0x00},std::byte{0x49},std::byte{0x8b},std::byte{0xf9},std::byte{0x49},std::byte{0x8b},std::byte{0xf0},std::byte{0x48},std::byte{0x8b},std::byte{0xea},std::byte{0x48},std::byte{0x8b},std::byte{0xd9},std::byte{0x75},std::byte{0x07}},
{std::byte{0x48},std::byte{0x89},std::byte{0x5c},std::byte{0x24},std::byte{0x18},std::byte{0x55},std::byte{0x41},std::byte{0x56},std::byte{0x41},std::byte{0x57},std::byte{0x48},std::byte{0x83},std::byte{0xec},std::byte{0x40},std::byte{0x48},std::byte{0x83},std::byte{0xb9},std::byte{0x90},std::byte{0x01},std::byte{0x00},std::byte{0x00},std::byte{0x00},std::byte{0x4d},std::byte{0x8b},std::byte{0xf1},std::byte{0x4d},std::byte{0x8b},std::byte{0xf8},std::byte{0x48},std::byte{0x8b},std::byte{0xda},std::byte{0x48}},
{std::byte{0x48},std::byte{0x89},std::byte{0x5c},std::byte{0x24},std::byte{0x18},std::byte{0x55},std::byte{0x41},std::byte{0x56},std::byte{0x41},std::byte{0x57},std::byte{0x48},std::byte{0x83},std::byte{0xec},std::byte{0x40},std::byte{0x48},std::byte{0x83},std::byte{0xb9},std::byte{0x90},std::byte{0x01},std::byte{0x00},std::byte{0x00},std::byte{0x00},std::byte{0x4d},std::byte{0x8b},std::byte{0xf1},std::byte{0x4d},std::byte{0x8b},std::byte{0xf8},std::byte{0x48},std::byte{0x8b},std::byte{0xda},std::byte{0x48}}
    }};
    std::array<Stage,3> stages_{};
    Direct direct_{};std::array<Queue,2> queues_{};
    std::array<bool,6> enabled_{};
    NativeGrab* physical_grab_{};Occlusion physical_occlusion_{};
    NativeDoorIngress door_ingress_;
    AnatomicalRawPair anatomical_raw_;
    std::atomic<bool> anatomical_shadow_{};
    struct AnatomicalQueryProof {
        std::uint64_t revision{},admission_epoch{},lifecycle_revision{},picked{};
    };
    mutable std::mutex anatomical_proof_mutex_;
    AnatomicalQueryProof anatomical_proof_{};
    std::atomic<bool> door_installed_{};
    Fetch fetch_{};Sort sort_{};Occlusion occlusion_{};std::array<bool,3> ranking_enabled_{};
    RangeOrigin range_origin_{};bool range_origin_enabled_{};
    inline static constexpr std::array<unsigned char,8> range_origin_code{0x48,0x8d,0x81,0xd8,0,0,0,0xc3};
    inline static thread_local std::array<float,4> hand_range_origin_{};
    inline static constexpr std::array<std::uintptr_t,3> ranking_rvas{0x92630,0x3a37f0,0x3b7b90};
    inline static constexpr std::array<const char*,3> ranking_code{"488954241048894c240857415541564883ec508b024c8db180820000498bf844","44884c242044894424188954241048894c2408535556574881ec880000004863","488bc4554154415541564157488da8f8faffff4881ece005000048c745e0feff"};
    Read read_{};Logic logic_{};std::uintptr_t base_{};
    bool route_{};
    bool observe_lean_{};
    bool observe_lean_lifetime_{};
    NativeLeanQueryTrace lean_trace_;
    NativeLeanLifecycle lean_lifetime_;
    std::atomic<bool> active_{false};
    std::atomic<bool> takedown_ranges_enabled_{false};
    std::atomic<bool> stopping_{false};
    std::atomic<UINT> depth_{};std::atomic<DWORD> stage0_thread_id_{}; // thread of the last stage<0> hook (frame split telemetry)
    std::atomic<UINT64> stage_calls_{},rays_{},routed_{},rejected_{},samples_{},overlap_{};
    std::atomic<UINT> owned_depth_{};
    std::atomic<UINT64> volume_requests_{},volume_rejected_{},ranked_{},ranking_rejected_{},direct_shortcuts_filtered_{},occluded_scan_continued_{};
    std::atomic<UINT64> hand_range_origins_{};
    std::atomic<UINT64> character_gaze_admitted_{},character_gaze_declined_{};
    std::array<std::atomic<UINT64>,static_cast<unsigned>(NativeUseQueryOutcome::count)> query_outcomes_{};
    std::mutex mutex_;
    std::array<Publication,2> hands_{};
    std::array<bool,2> published_{};
    InteractionHandRouting routing_;
    struct Motion {Vec3 position{},direction{};std::uint64_t tick{},until{};};std::array<Motion,2> motion_{};
    // Latest native selected interactable per routed hand, plus unrouted native
    // cycles, and the world hit point of each hand's routed direct query.
    std::array<std::uintptr_t,2> target_object_{};std::array<std::uint64_t,2> target_tick_{};
    std::array<int,2> target_generation_{-1,-1};
    std::array<std::uintptr_t,2> target_owner_{};std::array<unsigned,2> target_spawn_{};
    std::array<std::optional<NativeTalkOfferReceipt>,2> talk_offers_{};
    std::uintptr_t native_object_{};std::uint64_t native_tick_{},epoch_hint_{};
    std::array<Contact,2> contact_anchor_{};
    std::array<Vec3,2> point_{};std::array<std::uint64_t,2> point_tick_{};
    std::array<std::uint64_t,2> queued_revision_{},ready_revision_{};
    struct VolumeTicket {std::uintptr_t sensor{};unsigned token{};int side{-1};std::uint64_t revision{},tick{};};
    std::array<VolumeTicket,3> volume_tickets_{};
    std::ofstream log_;
    std::ofstream targets_;
    std::ofstream ranking_log_;unsigned ranking_rows_{};std::array<std::uint32_t,2> ranking_first_{};
    UINT64 target_samples_{};

    template<class Invoke> static auto counted_callback(NativeInteractionAim* self,Invoke&& invoke)->decltype(invoke()) {
        const auto previous_operation=crash_operation;++self->depth_;
        __try {return invoke();}
        __finally {crash_operation=previous_operation;--self->depth_;}
    }

    const float* range_origin(void* node,std::uintptr_t caller) {
        const auto* native=range_origin_(node); // exact pure getter, once
        const auto* scope=context_;
        if(!active_.load(std::memory_order_acquire) || !scope || scope->stage!=0 || scope->side<0 ||
           caller!=base_+0x3b81d8 || reinterpret_cast<std::uintptr_t>(node)!=scope->camera.camera ||
           !fresh(*scope,GetTickCount64()))return native;
        try {
            // Revalidate the current native sensor/camera owner. Other uses of
            // the shared getter, authored states and replaced cameras fall back.
            if(!rig::typed(read_,base_,scope->sensor,0x1b403b8) ||
               rig::value<std::uintptr_t>(read_,scope->sensor+0x40)!=scope->camera.camera ||
               rig::value<std::uintptr_t>(read_,scope->sensor+0x10)!=scope->camera.player ||
               !rig::typed(read_,base_,scope->camera.camera,camera_vtable,camera_secondary_vtable) ||
               rig::started(read_,scope->camera.camera)!=scope->camera.camera_identity ||
               rig::value<std::uintptr_t>(read_,scope->camera.camera+0x10)!=scope->camera.player)return native;
            std::array<float,3> eye{};
            if(!native || !read_(reinterpret_cast<std::uintptr_t>(native),eye.data(),sizeof(eye)))return native;
            const auto hand=interaction_hand_range_origin(scope->hand.volume_centre,{eye[0],eye[1],eye[2]});
            if(!hand)return native;
            std::lock_guard<std::mutex> lock(mutex_);
            if(scope->revision!=routing_.revision() || routing_.selected()!=scope->side || queued_revision_[scope->side]!=scope->revision)return native;
            hand_range_origin_=*hand;++hand_range_origins_;return hand_range_origin_.data();
        }catch(...){return native;}
    }
    static const float* __fastcall range_origin_hook(void* node) {
        auto* self=instance_;const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
        return counted_callback(self,[&] {return self->range_origin(node,caller);});
    }

    void invalidate_selection() noexcept {
        contact_anchor_={};volume_tickets_={};target_object_={};target_owner_={};target_spawn_={};talk_offers_={};target_tick_={};target_generation_={-1,-1};point_={};point_tick_={};queued_revision_={};ready_revision_={};native_object_=0;native_tick_=0;
    }
    std::optional<AnatomicalContactOwner> anatomical_scope_owner(const Context& scope) {
        if(scope.stage!=0 || stopping_ || !active_ || logic_()!=2)return {};
        const auto camera=native_player_camera(read_,base_,scope.camera.camera);
        const auto identity=rig::started(read_,scope.sensor);
        const auto game=rig::value<std::uintptr_t>(read_,base_+0x39a5290);
        const auto world=game?rig::value<std::uintptr_t>(read_,*game+NativeWorldTrace::world_offset):std::nullopt;
        const auto spawn=camera?rig::value<std::uint32_t>(read_,camera->player+0xa8):std::nullopt;
        if(!camera || camera->authored_view() || camera->player!=scope.camera.player || camera->camera_identity!=scope.camera.camera_identity ||
           !identity || *identity!=scope.sensor_identity || !game || !world || !*world || !spawn || !*spawn ||
           !rig::typed(read_,base_,scope.sensor,0x1b403b8) || rig::value<std::uintptr_t>(read_,scope.sensor+0x10)!=camera->player ||
           rig::value<std::uintptr_t>(read_,scope.sensor+0x40)!=camera->camera || rig::value<unsigned char>(read_,scope.sensor+0x38).value_or(0)==0)return {};
        return AnatomicalContactOwner{*game,camera->player,*world,camera->camera,scope.sensor,*spawn,camera->camera_identity,*identity};
    }
    void service_full_hand_contact(const Context& scope) {
        if(!full_hand_enabled_)return;
        const auto owner=anatomical_scope_owner(scope);
        if(!owner || !physical_grab_ || !physical_grab_->enabled()) {full_hand_contact_.clear(NativeFullHandContact::ContactReason::owner_or_grab_unavailable);return;}
        full_hand_contact_.observe(*owner);
        static const bool verified=NativeWorldTrace::verify(base_,read_);
        if(!verified) {full_hand_contact_.clear(NativeFullHandContact::ContactReason::verification_failed);return;}
        std::array<std::optional<NativeDoorEmptyGate::Lease>,2> gates{};
        const auto valid=[&](const NativeFullHandContact::Sample& sample) {
            if(!full_hand_enabled_.load(std::memory_order_acquire) || stopping_ || !active_ || sample.side>1)return false;
            const auto now=GetTickCount64();const auto current=anatomical_scope_owner(scope);
            if(!current || *current!=sample.owner || !native_world_query_source_current(read_,sample.source))return false;
            const auto camera=native_player_camera(read_,base_,scope.camera.camera);
            if(!camera || camera->authored_view())return false;
            auto& gate=gates[sample.side];
            if(gate)return gate->hands.arms==sample.binding->arms && gate->epoch==sample.tracking.pose_epoch &&
                NativeDoorEmptyGate::current(base_,read_,*camera,*physical_grab_,*gate,now);
            gate=NativeDoorEmptyGate::capture(base_,read_,*camera,*physical_grab_,now,1u<<sample.side);
            return gate && gate->hands.arms==sample.binding->arms && gate->epoch==sample.tracking.pose_epoch;
        };
        const auto light=[&](const NativeFullHandContact::Sample& sample) {
            const auto now=GetTickCount64();
            return full_hand_enabled_ && !stopping_ && active_ && sample.side<2 && gates[sample.side] &&
                sample.tracking.tick && sample.tracking.source_tick && now>=sample.tracking.tick && now>=sample.tracking.source_tick &&
                now-sample.tracking.tick<=32 && now-sample.tracking.source_tick<=32 &&
                native_world_query_source_current(read_,sample.source) &&
                NativeDoorEmptyGate::publications_current(*physical_grab_,*gates[sample.side],now);
        };
        full_hand_contact_.service(*owner,GetTickCount64(),[&](const NativeFullHandContact::Sample& sample,Vec3 from,Vec3 to,double radius) {
            return NativeWorldTrace::sphere_query_checked(base_,read_,reinterpret_cast<NativeWorldTrace::Cast>(base_+NativeWorldTrace::cast_rva),
                reinterpret_cast<NativeWorldTrace::Construct>(base_+NativeWorldTrace::sphere_ctor_rva),owner->world,from,to,radius,
                kWorldOccluderLayers,kWorldOccluderRequired,&sample.source,true);
        },valid,light,[&](const NativeFullHandContact::Sample& sample,std::uint32_t body,std::uint32_t key) {
            return FullHandTargetWitness::capture(read_,sample.source,body,key);
        });
    }
    void service_anatomical_contact(const Context& scope) {
        const auto owner=anatomical_scope_owner(scope);
        if(!owner) {clear_anatomical_contact();return;}
        anatomical_contact_.observe(0,*owner);anatomical_contact_.observe(1,*owner);
        if(!anatomical_contact_.has_pending(0) && !anatomical_contact_.has_pending(1))return;
        const auto now=GetTickCount64();
        const auto sample=anatomical_raw_.observation(anatomical_contact_,now);
        if(!sample || !physical_grab_ || !physical_grab_->enabled()) {clear_anatomical_contact();return;}
        static const bool verified=NativeWorldTrace::verify(base_,read_);
        if(!verified) {clear_anatomical_contact();return;}
        const auto source=native_world_query_source(read_,base_,owner->camera,owner->world);
        if(!source || *source!=sample->source) {clear_anatomical_contact();return;}
        const auto empty=NativeDoorEmptyGate::capture(base_,read_,scope.camera,*physical_grab_,now);
        if(!empty || empty->epoch!=sample->pose_epoch || empty->hands.arms!=sample->arms) {clear_anatomical_contact();return;}
        const auto valid=[&] {
            const auto current=anatomical_scope_owner(scope);const auto tick=GetTickCount64();
            return !stopping_.load(std::memory_order_acquire) && active_.load(std::memory_order_acquire) && current && *current==*owner &&
                anatomical_raw_.current(*sample,anatomical_contact_,tick) && native_world_query_source_current(read_,*source) &&
                NativeDoorEmptyGate::publications_current(*physical_grab_,*empty,tick);
        };
        LARGE_INTEGER begin{},end{};QueryPerformanceCounter(&begin);
        const auto count=anatomical_contact_.service_batch(*owner,*source,GetTickCount64(),[&](Vec3 from,Vec3 to,double radius) {
            return NativeWorldTrace::sphere_query_checked(base_,read_,reinterpret_cast<NativeWorldTrace::Cast>(base_+NativeWorldTrace::cast_rva),
                reinterpret_cast<NativeWorldTrace::Construct>(base_+NativeWorldTrace::sphere_ctor_rva),owner->world,from,to,radius,
                kWorldOccluderLayers,kWorldOccluderRequired,&*source,true);
        },valid);
        QueryPerformanceCounter(&end);anatomical_cast_count_+=count;anatomical_cast_ticks_+=static_cast<std::uint64_t>(end.QuadPart-begin.QuadPart);
        if(count==hand_proxy_count*2 && valid() && NativeDoorEmptyGate::current(base_,read_,scope.camera,*physical_grab_,*empty,GetTickCount64())) {
            std::lock_guard<std::mutex> lock(anatomical_proof_mutex_);
            anatomical_proof_={sample->revision,sample->admission.epoch,empty->lifecycle_revision,empty->picked};
        } else clear_anatomical_contact();
    }
    void service_door_contact(const Context& scope) {
        const auto owner=anatomical_scope_owner(scope);
        if(!door_installed_)return;
        if(!physical_grab_ || !owner) {clear_door_contact();return;}
        door_ingress_.service(scope.camera,*physical_grab_,GetTickCount64(),[&] {
            const auto current=anatomical_scope_owner(scope);return current && *current==*owner;
        });
        service_door_controller(scope);
    }
    void service_door_controller(const Context& scope) {
        if(!door_installed_)return;
        const auto now=GetTickCount64();
        if(stopping_ || !active_ || !anatomical_scope_owner(scope)) {clear_door_contact();return;}
        NativeDoorInteraction::process_owner().service(scope.camera,scope.sensor,scope.sensor_identity,door_ingress_,now);
    }
    struct StageFrame {
        const Context* context;Retrieved retrieved;const NativeLeanQuerySnapshot* lean_context;
        NativeLeanQueryTransaction* lean_transaction;const char* operation;bool owned{};
    };
    template<unsigned Index> static void __fastcall stage(void* sensor) {
        auto* self=instance_;StageFrame frame{context_,retrieved_,lean_context_,lean_transaction_,crash_operation};
        if constexpr(Index==0) self->stage0_thread_id_.store(GetCurrentThreadId(),std::memory_order_relaxed); // the game thread (frame split telemetry)
        ++self->depth_;++self->stage_calls_;context_=nullptr;retrieved_={};lean_context_=nullptr;lean_transaction_=nullptr;
        __try {stage_body<Index>(sensor,frame);}
        __finally {
            if(frame.owned)--self->owned_depth_;
            lean_transaction_=frame.lean_transaction;lean_context_=frame.lean_context;
            context_=frame.context;retrieved_=frame.retrieved;crash_operation=frame.operation;--self->depth_;
        }
    }
    template<unsigned Index> static void stage_body(void* sensor,StageFrame& frame) {
        auto* self=instance_;std::optional<Context> scope;std::optional<rig::Identity> sensor_identity;
        std::optional<NativeLeanQuerySnapshot> lean_scope;
        std::optional<NativeLeanQueryTransaction> lean_transaction;
        try {
            const auto at=reinterpret_cast<std::uintptr_t>(sensor);
            if(self->active_.load(std::memory_order_acquire) && !self->stopping_.load(std::memory_order_acquire) && self->logic_()==2 &&
               rig::typed(self->read_,self->base_,at,0x1b403b8) && rig::started(self->read_,at) &&
               rig::value<unsigned char>(self->read_,at+0x38).value_or(0)!=0) {
                const auto camera=rig::value<std::uintptr_t>(self->read_,at+0x40);
                auto owned=camera?native_player_camera(self->read_,self->base_,*camera):std::nullopt;
                if(owned && rig::value<std::uintptr_t>(self->read_,at+0x10)==owned->player && !owned->authored_view()) {
                    int side=-1;Publication hand{};std::uint64_t revision{},ownership_revision{};
                    {std::lock_guard<std::mutex> lock(self->mutex_);side=self->routing_.selected();revision=self->routing_.revision();ownership_revision=self->routing_.ownership_revision();if(side>=0) hand=self->hands_[side];}
                    scope=Context{*owned,hand,side,Index,at,revision};sensor_identity=rig::started(self->read_,at);if(sensor_identity)scope->sensor_identity=*sensor_identity;context_=&*scope;
                    scope->ownership_revision=ownership_revision;
                }
            }
        } catch(...) {}
        if constexpr(Index==2) {
            try {
                const auto at=reinterpret_cast<std::uintptr_t>(sensor);
                if(self->observe_lean_ && self->active_.load(std::memory_order_acquire) && self->logic_()==2 &&
                   rig::typed(self->read_,self->base_,at,0x1b40648)) {
                    lean_scope=native_lean_query_snapshot(self->read_,self->base_,at);
                    if(lean_scope) {
                        lean_context_=&*lean_scope;lean_transaction.emplace();lean_transaction_=&*lean_transaction;
                        NativeLeanQueryEvent event{};event.state=*lean_scope;event.thread=GetCurrentThreadId();event.mask=lean_scope->mask;
                        lean_transaction_->push(event,GetTickCount64());
                    } else self->lean_trace_.reject();
                }
            } catch(...) {self->lean_trace_.reject();}
        }
        if(scope) {frame.owned=true;if(self->owned_depth_.fetch_add(1)>0)++self->overlap_;}
        if constexpr(Index==0) {std::lock_guard<std::mutex> guard(self->mutex_);self->contact_anchor_={};}
        self->stages_[Index](sensor);
        if(lean_scope) {
            const auto after=native_lean_query_snapshot(self->read_,self->base_,lean_scope->sensor);
            NativeLeanQueryEvent event{};event.state=after.value_or(*lean_scope);event.phase=1;event.thread=GetCurrentThreadId();event.mask=event.state.mask;
            if(!after || !same_lean_resources(*lean_scope,*after)) {event.rejection=1;self->lean_trace_.reject();}
            lean_transaction_->push(event,GetTickCount64());
        }
        if(lean_scope) self->lean_trace_.commit(*lean_transaction,[] {return GetTickCount64();});
        if(self->stopping_.load(std::memory_order_acquire) || !self->active_.load(std::memory_order_acquire)) {
            self->full_hand_contact_.clear(NativeFullHandContact::ContactReason::inactive);
            self->clear_anatomical_contact();
            self->clear_door_contact();
            return;
        }
        if constexpr(Index==0) if(!scope) {self->full_hand_contact_.clear(NativeFullHandContact::ContactReason::scope_unqualified);self->clear_anatomical_contact();self->clear_door_contact();}
        if constexpr(Index==0) {if(scope) {
            try {self->service_full_hand_contact(*scope);} catch(...) {self->full_hand_contact_.clear(NativeFullHandContact::ContactReason::stage_exception);}
            try {self->service_anatomical_contact(*scope);} catch(...) {self->clear_anatomical_contact();}
            try {self->service_door_contact(*scope);} catch(...) {self->clear_door_contact();}
            self->target(*scope);
            try {if(self->physical_grab_ && self->physical_occlusion_) self->physical_grab_->service_obstruction(scope->camera,GetTickCount64(),[&](const NativeGrab::ObstructionRequest& obstruction_request)->std::optional<bool> {
                const auto identity=sensor_identity;
                const auto filter=rig::value<std::uint64_t>(self->read_,scope->sensor+0x48);
                const auto valid=[&] {
                    const auto camera=native_player_camera(self->read_,self->base_,scope->camera.camera);
                    return self->active_ && self->logic_()==2 && identity && filter &&
                        rig::typed(self->read_,self->base_,scope->sensor,0x1b403b8) && rig::started(self->read_,scope->sensor)==identity &&
                        rig::value<unsigned char>(self->read_,scope->sensor+0x38).value_or(0)!=0 &&
                        rig::value<std::uintptr_t>(self->read_,scope->sensor+0x40)==scope->camera.camera &&
                        rig::value<std::uintptr_t>(self->read_,scope->sensor+0x10)==scope->camera.player &&
                        rig::value<std::uint64_t>(self->read_,scope->sensor+0x48)==filter && camera &&
                        camera->camera_identity==scope->camera.camera_identity && !camera->authored_view();
                };
                if(!valid() || !grab_pose_valid(obstruction_request.hand))return {};
                const auto point=obstruction_request.body.contact(obstruction_request.hand.position);
                std::array<float,3> origin{static_cast<float>(obstruction_request.hand.position.x),static_cast<float>(obstruction_request.hand.position.y),static_cast<float>(obstruction_request.hand.position.z)},
                    target{static_cast<float>(point.x),static_cast<float>(point.y),static_cast<float>(point.z)};
                std::array<float,9> axis{};
                for(unsigned i=0;i<3;++i) {axis[i*3]=static_cast<float>(obstruction_request.hand.basis[i].x);axis[i*3+1]=static_cast<float>(obstruction_request.hand.basis[i].y);axis[i*3+2]=static_cast<float>(obstruction_request.hand.basis[i].z);}
                unsigned char continuation{};
                // ORIGINAL wrapper owns the collision filter/collector. Range0
                // matches stage0; the outbyte is scan continuation, not clear.
                const auto result=self->physical_occlusion_(sensor,reinterpret_cast<void*>(obstruction_request.body.entity),origin.data(),axis.data(),target.data(),0,&continuation);
                if(!valid())return {};
                return (result&0xff)==0;
            });} catch(...) {}
            // World-trace probe ("trace" test command): one sphere cast along the native camera's view, on this
            // game-thread scope only (the same engine call 0x3b7b90 makes here). Verifies the weapon-collision source.
            // Weapon/hand collision sweeps posted by the render thread (one frame late by design).
            {auto segments=take_weapon_requests();
             bool any=false;for(const auto& side:segments) for(const auto& seg:side) any=any || seg.valid;
             if(any) try {
                static const bool verified=NativeWorldTrace::verify(self->base_,self->read_);
                const auto camera=native_player_camera(self->read_,self->base_,scope->camera.camera);
                const auto game=rig::value<std::uintptr_t>(self->read_,self->base_+0x39a5290);
                const auto world=game?rig::value<std::uintptr_t>(self->read_,*game+NativeWorldTrace::world_offset):std::nullopt;
                if(verified && self->active_ && self->logic_()==2 && camera && !camera->authored_view() && world && *world) {
                    std::array<std::array<std::optional<WorldTraceHit>,weapon_slots>,2> hits{};
                    LARGE_INTEGER cast_begin{},cast_end{};unsigned cast_n=0,clamp_n=0;std::uint64_t clamp_ticks=0;QueryPerformanceCounter(&cast_begin);
                    // The hand clamp's exclusion sweeps (physical hands I1): the excluded entity's body (a prop's, or a dead guard's limb nearest the hand), mapped once per scope. Unmapped: cast without exclusion.
                    NativeExclusionCache excluded;
                    // Keyed by (entity, side, the body the service met): each hand has its own met body of one corpse.
                    const auto exclusion_of=[&](std::uintptr_t entity,unsigned side,const WeaponSegment& seg)->std::optional<NativeCastExclusion> {
                        return excluded.get(entity,side,seg.exclude_bone,seg.exclude_handle,[&] {
                            auto mapped=native_cast_exclusion(self->read_,self->base_,*world,entity,seg.exclude_bone,seg.exclude_handle,clamp_exclude_info_zero_.load(std::memory_order_relaxed));
                            // A native hold's item is not found by the generic prop mapping: its own throwable body (the one the layer swap writes) is.
                            if(!mapped && seg.exclude_bone<0) if(const auto held=NativeHeldObject::exclusion_body(self->base_,self->read_,entity); held && held->world==*world)
                                mapped=NativeCastExclusion{held->record,held->handle,held->filter};
                            return mapped;});
                    };
                    for(unsigned side=0;side<2;++side) for(unsigned slot=0;slot<weapon_slots;++slot) if(const auto& seg=segments[side][slot];seg.valid && ++cast_n) {
                        LARGE_INTEGER slot_begin{},slot_end{};QueryPerformanceCounter(&slot_begin);
                        const auto cast=[&](const NativeCastExclusion* exclusion) {
                            return NativeWorldTrace::sphere_cast(self->base_,self->read_,reinterpret_cast<NativeWorldTrace::Cast>(self->base_+NativeWorldTrace::cast_rva),
                                reinterpret_cast<NativeWorldTrace::Construct>(self->base_+NativeWorldTrace::sphere_ctor_rva),*world,seg.from,seg.to,seg.radius,
                                kWorldOccluderLayers,kWorldOccluderRequired,exclusion,clamp_slot(slot));   // only the clamp's slots are ranked by body class
                        };
                        if(seg.exclude) {
                            ++clamp_exclude_casts_;
                            if(const auto exclusion=exclusion_of(seg.exclude,side,seg)) {
                                hits[side][slot]=cast(&*exclusion);
                                if(hits[side][slot]) {if(hits[side][slot]->body_handle==exclusion->handle) ++clamp_exclude_self_hits_;else ++clamp_exclude_hits_;}
                            } else {++clamp_exclude_unmapped_;hits[side][slot]=cast(nullptr);}   // never drop the sweep: without exclusion it still finds what it finds
                        } else hits[side][slot]=cast(nullptr);
                        QueryPerformanceCounter(&slot_end);
                        if(clamp_slot(slot)) {clamp_ticks+=static_cast<std::uint64_t>(slot_end.QuadPart-slot_begin.QuadPart);++clamp_n;}
                    }
                    QueryPerformanceCounter(&cast_end);weapon_cast_ticks_+=static_cast<std::uint64_t>(cast_end.QuadPart-cast_begin.QuadPart);weapon_cast_count_+=cast_n;
                    {LARGE_INTEGER frequency{};QueryPerformanceFrequency(&frequency);
                     clamp_cast_us_.store(frequency.QuadPart?static_cast<double>(clamp_ticks)*1e6/static_cast<double>(frequency.QuadPart):0.);
                     clamp_cast_n_.store(clamp_n);++clamp_cast_serial_;}
                    record_weapon_answers(segments,hits,GetTickCount64());
                }
             } catch(...) {}}
            // Contact sword hits posted by the render thread: the engine's TakeDamage on this game-thread scope.
            {std::vector<OwnedSwordContact> contacts;std::vector<ImpactFxRequest> fx;std::uint64_t melee_revision{};
             {std::lock_guard<std::mutex> guard(trace_mutex_);contacts.swap(sword_contacts_);fx.swap(impact_fx_);melee_revision=nonlethal_grip_revision_.load(std::memory_order_relaxed);}
             if(!fx.empty() && impact_enabled_) try { // blade on world: the engine's own contact FX, quieter
                static const bool fx_verified=NativeImpactFx::verify(self->base_,self->read_);
                const auto camera=native_player_camera(self->read_,self->base_,scope->camera.camera);
                const auto sword=fx_verified && self->active_ && self->logic_()==2 && camera && !camera->authored_view()?NativeMelee::sword(self->base_,self->read_):std::nullopt;
                if(sword) for(const auto& r:fx) {const int k=NativeImpactFx::play(self->base_,self->read_,*sword,r);
                    if(k==0) ++impact_played_;else if(k==1) ++impact_skipped_;else {++melee_faults_;impact_enabled_=false;break;}}
                else impact_skipped_+=fx.size();
             } catch(...) {}
             if((!contacts.empty() || stealth_hold_.pending()) && melee_enabled_) try {
                static const bool verified=NativeMelee::verify(self->base_,self->read_);
                for(const auto& contact:contacts) {
                    const auto now=GetTickCount64();const auto hand=sword_hand_.current(now);
                    const auto camera=native_player_camera(self->read_,self->base_,scope->camera.camera);
                    if(!verified || !self->active_ || self->logic_()!=2 || !camera || camera->authored_view() || camera->player!=contact.owner.player ||
                       !hand || hand->epoch!=sword_pose_epoch_.load(std::memory_order_acquire) ||
                       !melee_batch_current(melee_revision) || !sword_contact_current(self->read_,contact,hand->epoch,now)) {++sword_contact_rejected_;continue;}
                    const auto final_now=GetTickCount64();const auto final_hand=sword_hand_.current(final_now);
                    if(!final_hand || final_hand->epoch!=contact.pose_epoch || final_hand->epoch!=sword_pose_epoch_.load(std::memory_order_acquire) ||
                       final_now<contact.hit.tick || final_now-contact.hit.tick>100 || !melee_batch_current(melee_revision)) {++sword_contact_rejected_;continue;}
                    // Without legacy-sword-contact (plan G task 3) the mod never holds or forces anything: the physical attack presses the
                    // native attack and the engine decides. A guard still unaware gets no contact damage (it would alert him before the press
                    // resolves); an aware guard is hit as before. A calm touch never reaches here (the producer drops it).
                    if(!legacy_contact_.load(std::memory_order_relaxed)) {
                        if(stealth_enabled_.load(std::memory_order_relaxed) && NativeMelee::damageable(self->base_,self->read_,contact.hit.victim) && NativeMelee::unaware(self->base_,self->read_,contact.hit.victim)) {
                            // No mod-made damage; the contact (moving at least 0.3 m/s, the producer's rule) asks for the native attack, once per episode.
                            ++intent_unaware_deferred_;
                            if(intent_episode_.first(contact.victim_spawn,final_now)) intent_contact_tick_.store(final_now,std::memory_order_release);
                            continue;}
                    } else
                    // A guard the engine calls unaware is not hit: the native Slash may still make this the Assassination, or the
                    // contact becomes the stealth kill below (sword_contact_policy.hpp). Anything else is the ordinary hit.
                    if(contact.stealth_strike && stealth_enabled_.load(std::memory_order_relaxed)) {
                        const bool damageable=NativeMelee::damageable(self->base_,self->read_,contact.hit.victim);
                        const bool unaware=damageable && NativeMelee::unaware(self->base_,self->read_,contact.hit.victim);
                        if(!damageable)++stealth_reject_[3];
                        else if(!unaware)++stealth_reject_[2];
                        else if(stealth_hold_.hold(contact,contact.victim_spawn,final_now,contact.native_offer)) {
                            // A full-speed contact absorbed into a calm hold: the held payload keeps its ordinary-hit fallback.
                            if(!contact.stealth_only && stealth_hold_.victim()==contact.victim_spawn) if(auto* held_payload=stealth_hold_.payload_mut()) held_payload->stealth_only=false;
                            ++stealth_counts_[0];continue;}
                        else ++stealth_reject_[4];
                    }
                    // A calm contact (below the hit speed) is only ever a stealth kill: never the ordinary alerting hit.
                    if(contact.stealth_only) {++stealth_slow_unheld_drops_;continue;}
                    NativeMelee::DamageResultObservation observation{};
                    const auto outcome=NativeMelee::apply(self->base_,self->read_,contact.equipment.sword,contact.hit,&observation);
                    ++sword_contact_invoked_;if(observation.returned_buffer)++sword_damage_results_;
                    {std::lock_guard<std::mutex> guard(trace_mutex_);
                     sword_last_contact_={contact.hit.tick,final_now,contact.pose_epoch,contact.victim_spawn,contact.owner.spawn,contact.equipment.sword.sword_id,
                         contact.owner.player,contact.equipment.sword.entity,contact.equipment.sword.weapon,outcome,observation,true};}
                    if(outcome==0)++melee_applied_;
                    else if(outcome==1)++sword_contact_rejected_;
                    else {++melee_faults_;melee_enabled_=false;break;}
                }
                if(const auto* held=stealth_hold_.payload()) {
                    const auto now=GetTickCount64();const auto& c=*held;
                    const auto camera=native_player_camera(self->read_,self->base_,scope->camera.camera);
                    const bool owned=verified && self->active_ && self->logic_()==2 && camera && camera->player==c.owner.player && sword_contact_owned(self->read_,c);
                    const bool alive=owned && NativeMelee::damageable(self->base_,self->read_,c.hit.victim);
                    const bool native=owned && (camera->authored_view() || native_player_state_active(self->read_,c.owner,native_state_assassination,native_state_assassination_vtable));
                    const bool choking=nonlethal_grip_reserved_.load(std::memory_order_acquire) || (owned && native_player_state_active(self->read_,c.owner,native_state_choke,native_state_choke_vtable));
                    const bool unaware=alive && NativeMelee::unaware(self->base_,self->read_,c.hit.victim);
                    const auto sword=c.equipment.sword;const auto hit=c.hit;
                    const auto raw_act=stealth_hold_.service(now,unaware,alive,native,choking,owned);
                    const auto act=resolve_stealth_act(raw_act,c.stealth_only);
                    if(raw_act==StealthAct::hit && act==StealthAct::drop) ++stealth_slow_aware_drops_;
                    else if(act==StealthAct::drop) ++stealth_counts_[native?1:4];
                    else if(act==StealthAct::hit || act==StealthAct::kill) {
                        auto strike=sword;if(act==StealthAct::kill) strike.unaware_amount=1000.f;
                        NativeMelee::DamageResultObservation observation{};
                        const auto outcome=NativeMelee::apply(self->base_,self->read_,strike,hit,&observation);
                        ++stealth_counts_[act==StealthAct::kill?2:3];++sword_contact_invoked_;if(observation.returned_buffer)++sword_damage_results_;
                        if(outcome==0)++melee_applied_;else if(outcome==1)++sword_contact_rejected_;else {++melee_faults_;melee_enabled_=false;}
                    }
                }
             } catch(...) {++sword_contact_rejected_;}}
            // Engine console line ("console" test command): appended on this game-thread scope; the engine runs it
            // at its next command-buffer flush. The command system has no lock, so never append from another thread.
            {std::vector<std::string> lines;{std::lock_guard<std::mutex> guard(trace_mutex_);lines.swap(console_lines_);}
             if(!lines.empty()) {
                static const bool console_ok=NativeConsole::verify(self->base_,self->read_);
                std::string report;
                for(const auto& line:lines) { // in order: e.g. "god player" before a teleport
                    const bool queued=console_ok && self->active_ && NativeConsole::buffer_command(self->base_,self->read_,line.c_str());
                    report+=std::string(report.empty()?"":" | ")+"console: "+(queued?"queued ":console_ok?"refused ":"code bytes differ ")+line;
                }
                std::lock_guard<std::mutex> guard(trace_mutex_);trace_result_+=(trace_result_.empty()?"":" | ")+report;
             }}
            // Player arms the engine hid while the mod tracks them (render thread request): the engine's own Show.
            if(const auto arms=arm_show_.exchange(0)) {if(self->active_ && self->logic_()==2) try {NativeHeldObject::show_arms(self->base_,self->read_,arms);} catch(...) {}}
            // Full-grip sword: equip requests from the input thread and the dropped copy (native_weapon_hands.hpp).
            if(self->logic_()==2) try {NativeWeaponHands::service(self->base_,self->read_);} catch(...) {}
            // The held object's collision layer is serviced every frame (restore on clear, then DEBRIS while held), apart from its render scale.
            // Released object height watch (status "release_watch"): z at a real throw/drop's restore, minimum and last over the next 3 s.
            // Game Z is up. Read-only; it stops when the entity id and body handle no longer validate.
            if(self->active_ && self->logic_()==2) try {
                // The watched body is read the way the layer swap finds it (a HoldObject item is not a native_rigid_body prop: sim 2026-10-22, the watch never
                // started): entity id, the throwable component's handle, and the entity origin (+0x300, game Z up).
                const auto watched_z=[&](std::uintptr_t entity,std::uint32_t id,std::uint32_t handle)->std::optional<float> {
                    const auto live_id=rig::value<std::uint32_t>(self->read_,entity+0xa8);
                    const auto body=NativeHeldObject::exclusion_body(self->base_,self->read_,entity);
                    float at[3]{};
                    if(!live_id || *live_id!=id || !body || body->handle!=handle || !self->read_(entity+0x300,at,sizeof at) || !std::isfinite(at[2])) return std::nullopt;
                    return at[2];
                };
                if(NativeHeldObject::released_pending_.exchange(false)) {
                    const auto r=NativeHeldObject::released_;
                    if(const auto z=watched_z(r.entity,r.id,r.handle)) {
                        release_watch_entity_=r.entity;release_watch_id_=r.id;release_watch_handle_=r.handle;release_watch_until_=GetTickCount64()+3000;
                        release_watch_z0_=release_watch_min_=release_watch_last_=*z;++release_watches_;
                    }
                }
                if(release_watch_entity_ && GetTickCount64()<release_watch_until_) {
                    if(const auto z=watched_z(release_watch_entity_,release_watch_id_,release_watch_handle_)) {release_watch_last_=*z;if(*z<release_watch_min_) release_watch_min_=*z;}
                    else release_watch_entity_=0;
                } else release_watch_entity_=0;
            } catch(...) {}
            {const auto held=held_world_.exchange(0);if(self->active_ && self->logic_()==2) try {NativeHeldObject::service_hold_layers(self->base_,self->read_,held);if(held) NativeHeldObject::world_render(self->base_,self->read_,held);} catch(...) {}}
            double probe_distance{};{std::lock_guard<std::mutex> guard(trace_mutex_);probe_distance=trace_distance_;trace_distance_=0;}
            if(probe_distance!=0) { // negative: straight down (game Z up)
                std::string out="trace: ";
                try {
                    static const bool verified=NativeWorldTrace::verify(self->base_,self->read_);
                    const auto camera=native_player_camera(self->read_,self->base_,scope->camera.camera);
                    const auto origin=rig::value<std::array<float,3>>(self->read_,scope->camera.camera+0x2527c);
                    const auto axis=rig::value<std::array<float,9>>(self->read_,scope->camera.camera+0x25288);
                    const auto game=rig::value<std::uintptr_t>(self->read_,self->base_+0x39a5290);
                    const auto world=game?rig::value<std::uintptr_t>(self->read_,*game+NativeWorldTrace::world_offset):std::nullopt;
                    if(!verified) out+="code bytes differ (unverified build)";
                    else if(!self->active_ || self->logic_()!=2 || !camera || camera->authored_view() || !origin || !axis || !world || !*world) out+="scope invalid";
                    else {
                        const bool down=probe_distance<0;probe_distance=std::abs(probe_distance);
                        const Vec3 from{(*origin)[0],(*origin)[1],(*origin)[2]},dir=down?Vec3{0,0,-1}:Vec3{(*axis)[0],(*axis)[1],(*axis)[2]};
                        const auto hit=NativeWorldTrace::sphere_cast(self->base_,self->read_,reinterpret_cast<NativeWorldTrace::Cast>(self->base_+NativeWorldTrace::cast_rva),
                            reinterpret_cast<NativeWorldTrace::Construct>(self->base_+NativeWorldTrace::sphere_ctor_rva),*world,from,from+dir*probe_distance,.05);
                        char line[320];
                        if(!hit) std::snprintf(line,sizeof(line),"no hit within %.2f from (%.2f %.2f %.2f) dir (%.2f %.2f %.2f)",probe_distance,from.x,from.y,from.z,dir.x,dir.y,dir.z);
                        else std::snprintf(line,sizeof(line),"hit at %.3f of %.2f point (%.2f %.2f %.2f) normal (%.2f %.2f %.2f) entity 0x%llx overlap %d",
                            hit->fraction,probe_distance,hit->point.x,hit->point.y,hit->point.z,hit->normal.x,hit->normal.y,hit->normal.z,
                            static_cast<unsigned long long>(hit->entity),hit->initial_overlap?1:0);
                        out+=line;
                    }
                } catch(...) {out+="exception";}
                std::lock_guard<std::mutex> guard(trace_mutex_);trace_result_=out;
            }
        }}
    }
    inline static std::mutex trace_mutex_;inline static double trace_distance_{};inline static std::string trace_result_;inline static std::vector<std::string> console_lines_;inline static std::vector<OwnedSwordContact> sword_contacts_;inline static std::vector<ImpactFxRequest> impact_fx_;
    inline static std::atomic<bool> nonlethal_grip_reserved_{};
    inline static std::atomic<std::uint64_t> nonlethal_grip_revision_{1};
    static bool melee_batch_current(std::uint64_t revision) noexcept {
        return revision && revision!=std::numeric_limits<std::uint64_t>::max() &&
            !nonlethal_grip_reserved_.load(std::memory_order_acquire) &&
            nonlethal_grip_revision_.load(std::memory_order_acquire)==revision;
    }
    // exclude: an entity whose body the cast must not report (the hand clamp's exclusion sweeps; 0: none).
    struct WeaponSegment {Vec3 from{},to{};double radius{};bool valid{};std::uintptr_t exclude{};int exclude_bone=-1;std::uint32_t exclude_handle{};std::uint8_t producer{};};   // exclude_bone/handle: the guard body the contact service met (-1: a prop)
    // Slots per hand: 0 the hand clamp's palm sweep (the chest re-acquire while released), 1 weapon, 2 sword reach (hit detection only,
    // past the tip), 3 the palm sweep and 4-8 one closing-fingertip sweep per finger (finger_ik.hpp: what each finger could touch; one
    // posted per frame, the others' hits persist <=100 ms). 9 and 10: the profile casts (dxgi_probe.cpp profile_step, radius_profile.hpp):
    // across a prop the hand targets, from its two sides. 11-13: the hand clamp's leading fingertip, wall tip and exclusion sweeps
    // (hand_clamp.hpp; with slot 0 at most 4 a hand, timed together as clamp_cast_us_, C14).
    inline static constexpr unsigned weapon_slots=14;
    inline static std::mutex weapon_mutex_;static std::array<std::array<WeaponSegment,weapon_slots>,2> weapon_req_; // defined after the class: clang-cl needs WeaponSegment's member initializers complete
    inline static std::array<std::array<std::optional<WorldTraceHit>,weapon_slots>,2> weapon_hit_{};inline static std::array<std::array<std::uint64_t,weapon_slots>,2> weapon_tick_{},weapon_serial_{};inline static std::array<std::array<std::uint8_t,weapon_slots>,2> weapon_producer_{};
public:
    static constexpr unsigned slot_count=weapon_slots;   // checks
    static constexpr unsigned probe_slots=9;   // the finger probes: slots 3-8 (the palm's own and the weapon's come first)
    static constexpr unsigned profile_slot=9;  // and 10
    static constexpr unsigned tip_slot=11,wall_tip_slot=12,exclude_slot=13;   // the hand clamp's (slot 0 is its palm)
    static constexpr bool clamp_slot(unsigned slot) {return slot==0 || slot==tip_slot || slot==wall_tip_slot || slot==exclude_slot;}
    inline static std::atomic<std::uint64_t> weapon_casts_{},weapon_cast_ticks_{},weapon_cast_count_{}; // QPC ticks and number of sphere casts run on the game thread
    // The hand clamp's casts (slots 0, 11-13, both hands) in the last game-thread scope: cost (us) and count; serial: one more per scope.
    inline static std::atomic<double> clamp_cast_us_{};inline static std::atomic<unsigned> clamp_cast_n_{};inline static std::atomic<std::uint64_t> clamp_cast_serial_{};
    // Exclusion sweeps: cast; the entity had no checked dynamic body (no cast); a hit on another body (a wall reports through it);
    // a hit on the excluded body itself (the native exclusion did not apply). build/hand-clamp-exclude-info-zero writes 0 to +0x14.
    inline static std::atomic<std::uint64_t> clamp_exclude_casts_{},clamp_exclude_unmapped_{},clamp_exclude_hits_{},clamp_exclude_self_hits_{};
    inline static std::atomic<bool> clamp_exclude_info_zero_{};
    // Render thread: post a sweep for the next game-thread scope; read the latest result (<=100 ms old).
    // producer: who asked (producer_clamp 0: the hand clamp, the weapon and the finger probes; producer_held: the held object's clamp sweeps; producer_release:
    // the safe-release probes). The slots are shared, the answer carries the producer of the request it answers, and a reader asks for its own: another
    // producer's request that overwrote its own before the game thread copied it is never read as its answer.
    static constexpr std::uint8_t producer_clamp=0,producer_held=1,producer_release=2;
    static void post_weapon_segment(unsigned side,unsigned slot,Vec3 from,Vec3 to,double radius,std::uintptr_t exclude=0,int exclude_bone=-1,std::uint32_t exclude_handle=0,std::uint8_t producer=producer_clamp) {
        if(side>1 || slot>=weapon_slots) return;std::lock_guard<std::mutex> guard(weapon_mutex_);weapon_req_[side][slot]={from,to,radius,true,exclude,exclude_bone,exclude_handle,producer};
    }
    // Game thread: the requests posted since the last scope (each is answered once).
    static std::array<std::array<WeaponSegment,weapon_slots>,2> take_weapon_requests() {
        std::lock_guard<std::mutex> guard(weapon_mutex_);auto segments=weapon_req_;
        for(auto& side:weapon_req_) for(auto& seg:side) seg.valid=false;
        return segments;
    }
    // Game thread: record the answers of a scope (the segments it copied, their hits) with each request's producer.
    static void record_weapon_answers(const std::array<std::array<WeaponSegment,weapon_slots>,2>& segments,const std::array<std::array<std::optional<WorldTraceHit>,weapon_slots>,2>& hits,std::uint64_t now) {
        std::lock_guard<std::mutex> guard(weapon_mutex_);
        for(unsigned side=0;side<2;++side) for(unsigned slot=0;slot<weapon_slots;++slot) if(segments[side][slot].valid) {
            weapon_hit_[side][slot]=hits[side][slot];weapon_tick_[side][slot]=now;weapon_serial_[side][slot]=weapon_casts_.load()+1;weapon_producer_[side][slot]=segments[side][slot].producer;
        }
        ++weapon_casts_;
    }
    // The scope (weapon_casts_ count) that last answered this slot: unchanged since a post means the post is still unanswered.
    static std::uint64_t weapon_serial(unsigned side,unsigned slot,std::uint8_t producer=producer_clamp) {
        if(side>1 || slot>=weapon_slots) return 0;std::lock_guard<std::mutex> guard(weapon_mutex_);return weapon_producer_[side][slot]==producer?weapon_serial_[side][slot]:0;
    }
    static std::optional<WorldTraceHit> weapon_hit(unsigned side,unsigned slot,std::uint64_t now,std::uint8_t producer=producer_clamp) {
        if(side>1 || slot>=weapon_slots) return {};std::lock_guard<std::mutex> guard(weapon_mutex_);
        const auto t=weapon_tick_[side][slot];return t && now>=t && now-t<=100 && weapon_producer_[side][slot]==producer?weapon_hit_[side][slot]:std::nullopt;
    }
    // The held object's sweeps (held_clamp.hpp, Part 5A): up to 4 sphere casts a frame on the holding hand's clamp slots (0, tip, wall tip, exclude), the
    // hand clamp itself being yielded to the carry. exclude: the held entity (its body must not report itself). Hits come back by proxy index, <=100 ms old.
    static constexpr unsigned held_sweep_slots=4;
    static constexpr unsigned held_slot(std::size_t index) {return index==0?0u:index==1?tip_slot:index==2?wall_tip_slot:exclude_slot;}
    template<class Sweeps> static void post_held_sweeps(unsigned side,const Sweeps& sweeps,std::uintptr_t exclude,std::uint8_t producer=producer_held) {
        if(side>1 || !exclude) return;
        for(std::size_t i=0;i<sweeps.size() && i<held_sweep_slots;++i) {
            const auto& sweep=sweeps[i];
            if(!sweep.valid || !std::isfinite(sweep.radius) || !(sweep.radius>0) || !finite(sweep.from) || !finite(sweep.to)) continue;
            post_weapon_segment(side,held_slot(i),sweep.from,sweep.to,sweep.radius,exclude,-1,0,producer);
        }
    }
    static std::array<std::optional<WorldTraceHit>,held_sweep_slots> held_hits(unsigned side,std::uint64_t now,std::uint8_t producer=producer_held) {
        std::array<std::optional<WorldTraceHit>,held_sweep_slots> out{};
        for(std::size_t i=0;i<held_sweep_slots;++i) out[i]=weapon_hit(side,held_slot(i),now,producer);
        return out;
    }
    // Diagnostics: did the palm, tip or wall-tip sweep of this side hit the entity? One lock for all three slots.
    static bool weapon_hits_entity(unsigned side,std::uintptr_t entity,std::uint64_t now) {
        if(side>1 || !entity) return false;std::lock_guard<std::mutex> guard(weapon_mutex_);
        for(const unsigned slot:{0u,tip_slot,wall_tip_slot}) {
            const auto t=weapon_tick_[side][slot];
            if(t && now>=t && now-t<=100 && weapon_hit_[side][slot] && weapon_hit_[side][slot]->entity==entity) return true;
        }
        return false;
    }
    // Test hook: request one world trace (metres along the view) and collect its text result later.
    static void request_trace(double distance) {std::lock_guard<std::mutex> guard(trace_mutex_);trace_distance_=distance;}
    // Render thread: a blade contact with a live NPC (one request per NPC per swing; the caller rate-limits).
    static void reserve_nonlethal_grip(bool reserved) {
        std::lock_guard<std::mutex> guard(trace_mutex_);
        if(reserved && !nonlethal_grip_reserved_.load(std::memory_order_relaxed)) {
            const auto revision=nonlethal_grip_revision_.load(std::memory_order_relaxed);
            if(revision!=std::numeric_limits<std::uint64_t>::max())nonlethal_grip_revision_.store(revision+1,std::memory_order_release);
            sword_nonlethal_rejected_+=sword_contacts_.size();sword_contacts_.clear();
        }
        nonlethal_grip_reserved_.store(reserved,std::memory_order_release);
    }
    inline static SwordContactPublication sword_hand_;
    inline static std::atomic<std::uint64_t> sword_pose_epoch_{},sword_contact_rejected_{},sword_contact_enqueued_{};
    inline static std::atomic<std::uint64_t> sword_damage_results_{},sword_contact_invoked_{},sword_capture_rejected_{},sword_queue_full_{},sword_nonlethal_rejected_{},sword_epoch_rejected_{};
    struct SwordContactObservation {
        std::uint64_t contact_tick{},consumer_tick{},pose_epoch{};
        std::uint32_t victim_spawn{},player_spawn{},sword_spawn{};
        std::uintptr_t player{},sword{},weapon{};
        int outcome{};
        NativeMelee::DamageResultObservation damage{};
        bool valid{};
    };
    static SwordContactObservation sword_last_contact_; // defined after the class: clang-cl needs SwordContactObservation's member initializers complete
    static SwordContactObservation sword_contact_observation() {
        std::lock_guard<std::mutex> guard(trace_mutex_);return sword_last_contact_;
    }
    static std::string sword_contact_status_json() {
        const auto observation=sword_contact_observation();std::ostringstream output;
        const auto number=[](float amount) {return std::isfinite(amount)?std::to_string(amount):std::string("null");};
        output<<"{\"enqueued\":"<<sword_contact_enqueued_.load()<<",\"capture_rejected\":"<<sword_capture_rejected_.load()
            <<",\"queue_full\":"<<sword_queue_full_.load()<<",\"nonlethal_rejected\":"<<sword_nonlethal_rejected_.load()
            <<",\"epoch_rejected\":"<<sword_epoch_rejected_.load()<<",\"consumer_rejected\":"<<sword_contact_rejected_.load()
            <<",\"stealth\":["<<stealth_counts_[0].load()<<','<<stealth_counts_[1].load()<<','<<stealth_counts_[2].load()<<','<<stealth_counts_[3].load()<<','<<stealth_counts_[4].load()<<']'
            <<",\"stealth_reject\":["<<stealth_reject_[0].load()<<','<<stealth_reject_[1].load()<<','<<stealth_reject_[2].load()<<','<<stealth_reject_[3].load()<<','<<stealth_reject_[4].load()<<']'
            <<",\"stealth_speed\":"<<stealth_speed_.load()<<",\"slow_aware_drops\":"<<stealth_slow_aware_drops_.load()<<",\"slow_unheld_drops\":"<<stealth_slow_unheld_drops_.load()<<",\"intent_unaware_deferred\":"<<intent_unaware_deferred_.load()
            <<",\"hit_height\":["<<hit_height_[0].load()<<','<<hit_height_[1].load()<<','<<hit_height_[2].load()<<']'
            <<",\"invoked\":"<<sword_contact_invoked_.load()<<",\"native_returned\":"<<sword_damage_results_.load()<<",\"last\":";
        if(!observation.valid)output<<"null";
        else {
            output<<"{\"contact_tick\":"<<observation.contact_tick<<",\"consumer_tick\":"<<observation.consumer_tick
                <<",\"pose_epoch\":"<<observation.pose_epoch<<",\"victim_spawn\":"<<observation.victim_spawn
                <<",\"player_spawn\":"<<observation.player_spawn<<",\"sword_spawn\":"<<observation.sword_spawn
                <<",\"player\":"<<observation.player<<",\"sword\":"<<observation.sword<<",\"weapon\":"<<observation.weapon
                <<",\"outcome\":"<<observation.outcome<<",\"native_returned\":"<<(observation.damage.returned_buffer?"true":"false")
                <<",\"computed_damage\":"<<number(observation.damage.computed_damage)<<",\"resource_amount\":"<<number(observation.damage.resource_damage)
                <<",\"modifier_mask\":"<<observation.damage.modifier_mask<<",\"modifier_payload\":"<<observation.damage.modifier_payload
                <<",\"flags\":"<<static_cast<unsigned>(observation.damage.flags)
                <<",\"computed_positive\":"<<(std::isfinite(observation.damage.computed_damage) && observation.damage.computed_damage>0?"true":"false")<<",\"raw\":\"";
            for(const auto byte:observation.damage.raw)output<<std::hex<<std::setfill('0')<<std::setw(2)<<static_cast<unsigned>(byte);
            output<<"\"}";
        }
        output<<'}';return output.str();
    }
    static void publish_sword_hand(bool held,std::uint64_t epoch,std::uint64_t tick) noexcept {sword_hand_.publish(held,epoch,tick);}
    static void publish_sword_pose_epoch(std::uint64_t epoch) noexcept {sword_pose_epoch_.store(epoch,std::memory_order_release);}
    static bool request_sword_contact(const OwnedSwordContact& contact) {
        std::lock_guard<std::mutex> guard(trace_mutex_);
        if(!melee_batch_current(nonlethal_grip_revision_.load(std::memory_order_relaxed))) {++sword_nonlethal_rejected_;return false;}
        if(sword_contacts_.size()>=4) {++sword_queue_full_;return false;}
        if(contact.pose_epoch!=sword_pose_epoch_.load(std::memory_order_acquire)) {++sword_epoch_rejected_;return false;}
        sword_contacts_.push_back(contact);++sword_contact_enqueued_;return true;
    }
    inline static StealthHold<OwnedSwordContact> stealth_hold_; // game thread only
    inline static std::atomic<bool> stealth_enabled_{true};
    inline static std::atomic<bool> legacy_contact_{false}; // build/legacy-sword-contact: the old hold/drop/kill routing
    inline static std::atomic<std::uint64_t> intent_contact_tick_{}; // a contact on an unaware guard wants the native attack pressed (control loop consumes it)
    inline static SlowRejectLatch intent_episode_; // game thread only
    inline static std::atomic<std::uint64_t> intent_unaware_deferred_{}; // contacts on an unaware guard left to the native attack press
    inline static std::atomic<double> stealth_speed_{stealth_speed_default}; // m/s, knob build\stealth-speed; the near-pass assist reads it too
    inline static std::atomic<std::uint64_t> stealth_slow_unheld_drops_{}; // calm contacts never held (aware, not damageable, slot busy, stealth off): dropped
    inline static std::atomic<std::uint64_t> stealth_slow_aware_drops_{};   // held calm contacts whose guard turned aware (service hit->drop)
    inline static std::array<std::atomic<std::uint64_t>,5> stealth_reject_{}; // not behind/side, slow, not unaware, not damageable, other (hold slot taken by another victim)
    inline static std::array<std::atomic<std::uint64_t>,3> hit_height_{};     // committed contacts by hit height above the victim's feet: leg < 0.9 m, torso 0.9-1.5 m, head > 1.5 m
    inline static std::array<std::atomic<std::uint64_t>,5> stealth_counts_{}; // held, native took it, stealth kill, noticed (ordinary hit), dropped (dead/choke/owner lost)
    inline static std::atomic<std::uint64_t> melee_applied_{},melee_rejected_{},melee_faults_{};
    inline static std::array<std::atomic<std::uint64_t>,3> melee_why_{}; // rejected: code bytes differ, no sword/scope, victim not damageable (dead/unconscious/not NPC)
    inline static std::atomic<bool> melee_enabled_{true},impact_enabled_{true};inline static std::atomic<std::uint64_t> impact_played_{},impact_skipped_{};
    static void request_impact_fx(const ImpactFxRequest& r) {std::lock_guard<std::mutex> guard(trace_mutex_);if(impact_fx_.size()<4) impact_fx_.push_back(r);}
    inline static std::atomic<unsigned> arm_show_{};
    static void request_arm_show(unsigned mask) noexcept {arm_show_|=mask;}
    inline static std::atomic<std::uintptr_t> held_world_{};
    inline static std::uintptr_t release_watch_entity_{};inline static std::uint32_t release_watch_id_{},release_watch_handle_{};inline static std::uint64_t release_watch_until_{};
public:
    inline static std::atomic<float> release_watch_z0_{},release_watch_min_{},release_watch_last_{};inline static std::atomic<std::uint64_t> release_watches_{};
    static std::string release_watch_json() {
        char b[200];std::snprintf(b,sizeof(b),"{\"count\":%llu,\"z_at_release\":%.3f,\"z_min\":%.3f,\"z_last\":%.3f,\"floor_z\":null}",
            (unsigned long long)release_watches_.load(),release_watch_z0_.load(),release_watch_min_.load(),release_watch_last_.load());
        return b;
    }
public:
    static void request_held_world(std::uintptr_t entity) noexcept {held_world_=entity;}
    static void request_console(const std::string& line) {std::lock_guard<std::mutex> guard(trace_mutex_);if(console_lines_.size()<32) console_lines_.push_back(line);}
    static std::string take_trace_result() {std::lock_guard<std::mutex> guard(trace_mutex_);std::string r;r.swap(trace_result_);return r;}
private:
    bool qualified_selected(std::uint64_t now) const noexcept {
        const auto side=routing_.selected();
        return side>=0 && target_object_[side] && ready_revision_[side]==routing_.revision() &&
            target_tick_[side] && now>=target_tick_[side] && now-target_tick_[side]<=250;
    }
    bool live_target(unsigned side,std::uint64_t now) const noexcept {
        if(side>=2 || !published_[side] || routing_.selected()!=static_cast<int>(side) ||
           ready_revision_[side]!=routing_.revision() || !target_object_[side] || !target_tick_[side] ||
           now<target_tick_[side] || now-target_tick_[side]>250 || !hands_[side].epoch ||
           now<hands_[side].tick || now-hands_[side].tick>250)return false;
        try {
            const auto object=target_object_[side];const auto identity=rig::started(read_,object);
            const auto owner=rig::value<std::uintptr_t>(read_,object+0x10);
            const auto game=rig::value<std::uintptr_t>(read_,base_+0x39a5290);
            const auto spawn=owner?rig::value<unsigned>(read_,*owner+0xa8):std::nullopt;
            const auto camera=hands_[side].camera;const auto camera_id=rig::started(read_,camera);
            return identity && identity->generation==target_generation_[side] && owner && *owner==target_owner_[side] &&
                spawn && *spawn==target_spawn_[side] && game &&
                *spawn!=0x3fff0010 && (*spawn>>16)<0x3fff && camera_id && camera_id->generation==hands_[side].generation &&
                rig::typed(read_,base_,camera,camera_vtable,camera_secondary_vtable) &&
                rig::value<std::uintptr_t>(read_,camera+0x10)==hands_[side].player &&
                rig::value<std::uintptr_t>(read_,*game+0x3130b0)==hands_[side].player &&
                rig::value<int>(read_,camera+0x25200)==0 && rig::value<int>(read_,camera+0x25204)==0 &&
                rig::value<unsigned char>(read_,camera+0x251b9)==0 && rig::value<unsigned char>(read_,camera+0x25260)==0 &&
                rig::started(read_,camera)==camera_id &&
                rig::value<std::uintptr_t>(read_,*game+0x3130b0+(*spawn>>16)*8)==owner &&
                rig::value<unsigned>(read_,*owner+0xa8)==spawn && rig::started(read_,object)==identity &&
                rig::value<std::uintptr_t>(read_,object+0x10)==owner;
        } catch(...) {return false;}
    }
    // A published hand is routable only while fresh and bound to this player/camera.
    bool fresh(const Context& scope,std::uint64_t now) const noexcept {
        const auto& hand=scope.hand;
        return route_ && hand.active && hand.epoch && now>=hand.tick && now-hand.tick<=250 &&
            hand.player==scope.camera.player && hand.camera==scope.camera.camera &&
            hand.generation==scope.camera.camera_identity.generation;
    }
    void target(const Context& scope) noexcept {
        try {
            const auto machine=rig::value<std::uintptr_t>(read_,scope.sensor+0x20);
            const auto interaction=machine?rig::value<std::uintptr_t>(read_,*machine+0x318):std::nullopt;
            if(!machine || !interaction || !rig::typed(read_,base_,*machine,0x1c73d58) ||
               !rig::typed(read_,base_,*interaction,0x1c72e98) || rig::value<std::uintptr_t>(read_,*interaction+0x10)!=scope.camera.player) return;
            const auto generation=rig::value<int>(read_,*interaction+0x30);
            const auto selected=rig::value<std::uintptr_t>(read_,*interaction+0x38);
            std::uintptr_t object=0,table=0,owner=0;
            if(generation && selected && *generation!=-1 && rig::pointer(*selected) &&
               rig::value<int>(read_,*selected+0x18)==generation) {
                object=*selected;table=rig::value<std::uintptr_t>(read_,object).value_or(0);
                owner=rig::value<std::uintptr_t>(read_,object+0x10).value_or(0);
            }
            const auto interacting=rig::value<unsigned char>(read_,*interaction+0x40).value_or(0);
            std::array<UINT,3> candidates{};
            if(!read_(scope.sensor+0x84,candidates.data(),sizeof(candidates))) return;
            // Stage0 writes entity spawn ids from entity+0xa8. The shared
            // selected component can still belong to the previous hand.
            const auto spawn=owner?rig::value<UINT>(read_,owner+0xa8):std::nullopt;
            bool candidate=false;
            if(spawn) for(auto id:candidates) if(id!=0x3fff0010u && id!=0xffffffffu && id==*spawn) candidate=true;
            const auto node=rig::value<std::uintptr_t>(read_,scope.sensor+0x98);
            const auto link=node && rig::pointer(*node)?rig::value<std::uintptr_t>(read_,*node+0x18):std::nullopt;
            const auto direct_owner=link && rig::pointer(*link)?rig::value<std::uintptr_t>(read_,*link+0x10):std::nullopt;
            const auto selected_object=object; // before the candidate check (target log)
            if(!candidate && (!direct_owner || *direct_owner!=owner)) object=0;
            const auto talk_offer=object && spawn && generation?native_talk_offer(read_,base_,scope.sensor,scope.camera.player,
                object,static_cast<std::uint32_t>(*generation),owner,*spawn):std::nullopt;
            std::lock_guard<std::mutex> lock(mutex_);
            const auto now=GetTickCount64();
            if(scope.side>=0 && fresh(scope,now) && scope.revision==routing_.revision() &&
               queued_revision_[scope.side]==scope.revision) {
                target_object_[scope.side]=object;target_tick_[scope.side]=now;ready_revision_[scope.side]=scope.revision;
                talk_offers_[scope.side]=talk_offer;
                contact_anchor_[scope.side]={};
                if(object && spawn && scope.stage==0 && !scope.contacts_overflow &&
                   hands_[scope.side].tick==scope.hand.tick && hands_[scope.side].epoch==scope.hand.epoch) {
                    std::optional<Contact> chosen;bool ambiguous=false;
                    for(unsigned i=0;i<scope.contact_count;++i) {
                        const auto& observed=scope.contacts[i];
                        if(observed.owner!=owner || observed.spawn!=*spawn || !contact_valid(scope,observed,now))continue;
                        if(chosen && length(chosen->point-observed.point)>1e-5)ambiguous=true;
                        chosen=observed;
                    }
                    const auto selected_identity=rig::started(read_,object);
                    if(chosen && !ambiguous && selected_identity && selected_identity->generation==generation) {
                        chosen->component=object;chosen->component_identity=*selected_identity;contact_anchor_[scope.side]=*chosen;
                    }
                }
                target_generation_[scope.side]=generation.value_or(-1);target_owner_[scope.side]=object?owner:0;target_spawn_[scope.side]=object?spawn.value_or(0):0;
            }
            else if(scope.side<0) {native_object_=object;native_tick_=now;}
            // The first 2000 cycles, then (up to 12000) every cycle while a grip Use holds a hand: what that hand's query selected.
            if(target_samples_<2000 || (routing_.locked()>=0 && target_samples_<12000)) {
                targets_<<GetTickCount64()<<','<<GetCurrentThreadId()<<','<<scope.stage<<','<<*interaction<<','<<object<<','
                    <<generation.value_or(-1)<<','<<(table>=base_?table-base_:0)<<','<<owner<<','<<static_cast<unsigned>(interacting);
                for(auto id:candidates) targets_<<','<<id;
                targets_<<','<<scope.side<<','<<routing_.locked()<<','<<selected_object<<','<<(candidate?1:0)<<','<<(direct_owner?*direct_owner:0)<<','<<spawn.value_or(0)<<'\n';
                ++target_samples_;if(target_samples_%120==0) targets_.flush();
            }
        } catch(...) {}
    }
    const InteractionRay* ray(const InteractionRay* native,InteractionRay& copy,std::uintptr_t caller) noexcept {
        const auto* scope=context_;
        if(!active_.load(std::memory_order_acquire) || !scope) return native;
        const std::array<std::array<std::uintptr_t,2>,3> callers{{{0x3b867b,0},{0x3b9037,0},{0x3b9962,0x3b9a49}}};
        const auto rva=caller-base_;
        if(rva!=callers[scope->stage][0] && rva!=callers[scope->stage][1]) return native;
        ++rays_;
        InteractionRay original{};
        if(!read_(reinterpret_cast<std::uintptr_t>(native),&original,sizeof(original))) {++rejected_;return native;}
        bool replaced=false;
        const auto& hand=scope->hand;const auto now=GetTickCount64();
        if(scope->side>=0 && fresh(*scope,now)) {
            const auto next=controller_interaction_ray(original,scope->camera.aim_origin,scope->camera.aim_axis,hand.hand);
            if(next) {
                copy=*next;
                if(scope->stage==0 && !hand.reach) {const auto volume_ray=hand_volume_direct_ray(copy,hand.volume_centre,hand.hand.basis[0]);if(volume_ray) copy=*volume_ray;else {++rejected_;return native;}}
                replaced=true;++routed_;
            } else ++rejected_;
        }
        try {
            if(samples_<2000) {
                std::lock_guard<std::mutex> lock(mutex_);
                if(samples_<2000) {
                    log_<<GetTickCount64()<<','<<GetCurrentThreadId()<<','<<scope->stage<<','<<scope->sensor<<','<<scope->camera.camera<<','<<replaced;
                    for(auto f:original.f) log_<<','<<f;
                    const auto& effective=replaced?copy:original;for(auto f:effective.f) log_<<','<<f;
                    log_<<'\n';++samples_;if(samples_%120==0) log_.flush();
                }
            }
        } catch(...) {}
        return replaced?&copy:native;
    }
    // The direct query's own outputs are handed out untouched; only when the ray
    // was ours do we record this hand's world hit point under the routed side.
    void point(const InteractionRay& routed,std::uintptr_t result,const void* distance) noexcept {
        const auto* scope=context_;
        if(!scope || scope->side<0) return;
        try {
            std::optional<double> native_distance;float range{};
            if((result&0xff) && distance) {std::memcpy(&range,distance,sizeof(range));native_distance=range;}
            const auto found=interaction_hit_point(routed,result,native_distance);
            std::lock_guard<std::mutex> lock(mutex_);
            const auto now=GetTickCount64();
            if(scope->revision!=routing_.revision() || !fresh(*scope,now)) return;
            point_tick_[scope->side]=found?now:0;
            if(found) point_[scope->side]=*found;
        } catch(...) {}
    }
    static std::uintptr_t __fastcall direct(void* manager,const InteractionRay* ray,std::uintptr_t* entity,float* distance,void* normal) {
        auto* self=instance_;const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
        return counted_callback(self,[&] {return direct_body(manager,ray,entity,distance,normal,caller);});
    }
    static std::uintptr_t direct_body(void* manager,const InteractionRay* ray,std::uintptr_t* entity,float* distance,void* normal,std::uintptr_t caller) {
        CrashOperation operation("native interaction direct ray");
        auto* self=instance_;alignas(16) InteractionRay copy{};
        const auto* selected=self->ray(ray,copy,caller);
        const auto result=self->direct_(manager,selected,entity,distance,normal);
        if(selected!=ray) self->point(*selected,result,distance);
        auto filtered=result;
        if(self->active_.load(std::memory_order_acquire) && selected!=ray && context_ && context_->stage==0 && context_->side>=0) {
            std::lock_guard<std::mutex> lock(self->mutex_);
            // Native stage0 compares direct HAND distance with candidate HEAD
            // distance. For a verified volume cycle, let native volume selection
            // own priority instead of allowing that mixed-origin shortcut.
            if(self->queued_revision_[context_->side]==context_->revision && NativeHandVolume::shape(self->base_,self->read_)) {
                filtered&=~std::uintptr_t{0xff};++self->direct_shortcuts_filtered_;
            }
        }
        return filtered;
    }
    bool ranking_profile(unsigned index) const {
        std::array<unsigned char,32> actual{};
        if(!read_(base_+ranking_rvas[index],actual.data(),actual.size())) return false;
        for(unsigned i=0;i<32;++i) {
            const auto nibble=[](char c){return c<='9'?c-'0':c-'a'+10;};
            if(actual[i]!=static_cast<unsigned char>(nibble(ranking_code[index][2*i])*16+nibble(ranking_code[index][2*i+1]))) return false;
        }
        return true;
    }
public:
    // Something a hand takes: loot (arkPickup 0x1c5e258) or a native carryable (bottle, glass: arkCpntPickable_Simple 0x1c5a3b8).
    bool takeable(std::uintptr_t entity) const {
        if(!entity) return false;
        if(rig::typed(read_,base_,entity,0x1c5e258)) return true;
        if(const auto list=rig::list(read_,entity+0x268,256))
            for(unsigned i=0;i<list->packed_count;++i)
                if(const auto c=rig::value<std::uintptr_t>(read_,list->data+i*8);c && *c && rig::typed(read_,base_,*c,0x1c5a3b8)) return true;
        return false;
    }
private:
    std::uintptr_t hit_owner(const NativeUseHit& hit,std::uintptr_t world) const {
        std::uint32_t handle{};std::memcpy(&handle,hit.data()+0x48,4);
        const auto pool=rig::value<std::uintptr_t>(read_,world+0x28);const auto count=rig::value<unsigned>(read_,world+0x30);
        const auto index=handle&0xffffff;if(!pool || !count || *count>0x100000 || index>=*count) return 0;
        const auto record=*pool+index*0xb0;
        if(rig::value<std::uint32_t>(read_,record+0x70)!=handle) return 0;
        const auto node=rig::value<std::uintptr_t>(read_,record+0x90);
        const auto link=node?rig::value<std::uintptr_t>(read_,*node+8):std::nullopt;
        const auto owner=link?rig::value<std::uintptr_t>(read_,*link+0x10):std::nullopt;
        const auto game=rig::value<std::uintptr_t>(read_,base_+0x39a5290);
        const auto spawn=owner?rig::value<unsigned>(read_,*owner+0xa8):std::nullopt;
        if(!game || !owner || !spawn || (*spawn>>16)>=0x3fff ||
           rig::value<std::uintptr_t>(read_,*game+0x3130b0+(*spawn>>16)*8)!=owner ||
           rig::value<std::uint32_t>(read_,record+0x70)!=handle || rig::value<std::uintptr_t>(read_,record+0x90)!=node) return 0;
        return *owner;
    }
    static std::uintptr_t __fastcall fetch(void* manager,void* id,void* output) {
        return instance_->fetch_result(manager,id,output,reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
    }
    std::uintptr_t fetch_result(void* manager,void* id,void* output,std::uintptr_t caller) {
        return counted_callback(this,[&] {return fetch_result_body(manager,id,output,caller);});
    }
    std::uintptr_t fetch_result_body(void* manager,void* id,void* output,std::uintptr_t caller) {
        auto* self=this;retrieved_={};const auto* scope=context_;
        bool owned=false;std::uintptr_t world{},preferred{};
        try {
            if(self->active_.load(std::memory_order_acquire) && scope && scope->stage==0 && scope->side>=0 && caller==self->base_+0x3b885b && self->fresh(*scope,GetTickCount64())) {
                const auto at=reinterpret_cast<std::uintptr_t>(id);const auto manager_at=reinterpret_cast<std::uintptr_t>(manager);
                const auto game=rig::value<std::uintptr_t>(self->read_,self->base_+0x39a5290);
                const auto token=rig::value<unsigned>(self->read_,at);
                if(game && token && *token!=0xffffffff && manager_at==*game+0x60280 && at>=scope->sensor+0x70 && at<=scope->sensor+0x78 && (at-scope->sensor-0x70)%4==0) {
                    const auto slot=manager_at+0x8280+(*token&0x1fff)*0x140;
                    world=rig::value<std::uintptr_t>(self->read_,manager_at+0x190).value_or(0);
                    const auto shape=NativeHandVolume::shape(self->base_,self->read_);
                    owned=world && rig::value<std::uintptr_t>(self->read_,*game+0x60410)==world && shape &&
                        rig::value<unsigned>(self->read_,slot+0x88)==token && rig::value<unsigned char>(self->read_,slot+0x8c)==4 &&
                        (rig::value<unsigned char>(self->read_,slot+0x8e).value_or(0)&2) && rig::value<std::uintptr_t>(self->read_,slot+0xb8)==shape;
                    std::lock_guard<std::mutex> lock(self->mutex_);
                    const auto& ticket=self->volume_tickets_[static_cast<unsigned>((at-scope->sensor-0x70)/4)];
                    const auto now=GetTickCount64();
                    owned=owned && ticket.sensor==scope->sensor && ticket.token==*token && ticket.side==scope->side && ticket.revision==scope->revision &&
                        ticket.revision==self->routing_.revision() && now>=ticket.tick && now-ticket.tick<=250;
                    if(self->qualified_selected(GetTickCount64())) preferred=rig::value<std::uintptr_t>(self->read_,self->target_object_[scope->side]+0x10).value_or(0);
                }
            }
        } catch(...) {}
        const auto result=self->fetch_(manager,id,output);
        try {
            if(owned && scope && self->fresh(*scope,GetTickCount64())) {
                const auto at=reinterpret_cast<std::uintptr_t>(output);
                const auto data=rig::value<std::uintptr_t>(self->read_,at);const auto count=rig::value<int>(self->read_,at+8);
                const auto capacity=rig::value<unsigned>(self->read_,at+12);
                const auto outcome=(result&0xffu)==0?NativeUseQueryOutcome::incomplete:
                    data && count && capacity?native_use_query_outcome(result,*data,*count,*capacity):NativeUseQueryOutcome::rejected;
                ++self->query_outcomes_[static_cast<unsigned>(outcome)];
                if(outcome==NativeUseQueryOutcome::multiple) retrieved_={*data,world,static_cast<unsigned>(*count),preferred,scope};
                else if(outcome==NativeUseQueryOutcome::rejected)++self->ranking_rejected_;
            }
        } catch(...) {++self->ranking_rejected_;}
        return result;
    }
    static void __fastcall sort(void* data,unsigned first,int last,unsigned char mode) {
        auto* self=instance_;const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
        counted_callback(self,[&] {sort_body(data,first,last,mode,caller);});
    }
    static void sort_body(void* data,unsigned first,int last,unsigned char mode,std::uintptr_t caller) {
        auto* self=instance_;
        self->sort_(data,first,last,mode); // Native sort runs once, including recursion.
        try {
            const auto scope=retrieved_;const auto at=reinterpret_cast<std::uintptr_t>(data);
            if(self->active_.load(std::memory_order_acquire) && caller==self->base_+0x3b8878 && scope.scope==context_ && scope.scope && at==scope.data &&
               first==0 && last==static_cast<int>(scope.count)-1 && mode==0 && self->fresh(*scope.scope,GetTickCount64())) {
                std::array<NativeUseHit,64> hits{},before{};
                if(!self->read_(at,hits.data(),scope.count*sizeof(NativeUseHit))) throw std::runtime_error("Native Use results unreadable");
                before=hits;
                if(!order_native_use_hits(hits.data(),scope.count,scope.scope->hand.volume_centre,scope.preferred,
                    [&](const NativeUseHit& hit){return self->hit_owner(hit,scope.world);},scope.scope->hand.hand.basis[0],
                    [&](std::uintptr_t e){return self->takeable(e);})) throw std::runtime_error("Native Use result point invalid");
                std::array<NativeUseHit,64> current{};
                if(!self->read_(at,current.data(),scope.count*sizeof(NativeUseHit)) || current!=before) throw std::runtime_error("Native Use results changed");
                std::memcpy(data,hits.data(),scope.count*sizeof(NativeUseHit));retrieved_.ranked=true;++self->ranked_;
                std::uint32_t first_handle{};std::memcpy(&first_handle,hits[0].data()+0x48,4);
                std::lock_guard<std::mutex> lock(self->mutex_);
                if(self->ranking_rows_<512 && self->ranking_first_[scope.scope->side]!=first_handle) {
                    self->ranking_first_[scope.scope->side]=first_handle;
                    for(unsigned i=0;i<scope.count && self->ranking_rows_<512;++i) {
                        std::array<float,3> xyz{};float fraction{};std::uint32_t handle{};
                        std::memcpy(xyz.data(),hits[i].data(),12);std::memcpy(&fraction,hits[i].data()+0x20,4);std::memcpy(&handle,hits[i].data()+0x48,4);
                        self->ranking_log_<<GetTickCount64()<<','<<scope.scope->side<<','<<scope.scope->sensor<<','<<scope.count<<','<<i<<','<<handle<<','<<self->hit_owner(hits[i],scope.world)<<','
                            <<xyz[0]<<','<<xyz[1]<<','<<xyz[2]<<','<<length(Vec3{xyz[0],xyz[1],xyz[2]}-scope.scope->hand.volume_centre)<<','<<fraction<<','<<scope.preferred<<'\n';++self->ranking_rows_;
                    }
                    self->ranking_log_.flush();
                }
            }
        } catch(...) {++self->ranking_rejected_;}
    }
    std::optional<bool> character_type(std::uintptr_t entity) const {
        // Native stage0 calls entity vtable[0] and compares typeInfo+60 with
        // these exact character bounds. Decode only the proven pure LEA/RET
        // getter form instead of introducing another native engine call.
        const auto table=rig::value<std::uintptr_t>(read_,entity);
        const auto getter=table?rig::value<std::uintptr_t>(read_,*table):std::nullopt;
        if(!getter || *getter<base_+0x1000 || *getter>=base_+0x1a70000) return {};
        std::array<unsigned char,8> code{};
        if(!read_(*getter,code.data(),code.size()) || code[0]!=0x48 || code[1]!=0x8d || code[2]!=0x05 || code[7]!=0xc3) return {};
        std::int32_t displacement{};std::memcpy(&displacement,code.data()+3,4);
        const auto info=static_cast<std::uintptr_t>(static_cast<std::int64_t>(*getter+7)+displacement);
        if(info<base_+0x3900000 || info>=base_+0x3f00000) return {};
        const auto type=rig::value<int>(read_,info+0x60),lower=rig::value<int>(read_,base_+0x39ce2c0),upper=rig::value<int>(read_,base_+0x39ce2c4);
        if(!type || !lower || !upper || *type<0 || *lower<0 || *upper<*lower ||
           rig::value<std::uintptr_t>(read_,entity)!=table || rig::value<std::uintptr_t>(read_,*table)!=getter) return {};
        return *type>=*lower && *type<=*upper;
    }
    bool contact_valid(const Context& scope,const Contact& contact,std::uint64_t now) {
        if(scope.side<0 || scope.side>1 || scope.stage!=0 || !fresh(scope,now) || !active_ || logic_()!=2 ||
           contact.sensor!=scope.sensor || contact.epoch!=scope.hand.epoch || contact.revision!=scope.revision ||
           contact.publication_tick!=scope.hand.tick || !contact.tick || now<contact.tick || now-contact.tick>250 ||
           !finite(contact.point))return false;
        const auto game=rig::value<std::uintptr_t>(read_,base_+0x39a5290);
        return game && rig::value<std::uintptr_t>(read_,*game+0x60410)==contact.world &&
            rig::value<std::uintptr_t>(read_,*game+0x3130b0)==scope.camera.player &&
            contact.spawn!=0x3fff0010 && (contact.spawn>>16)<0x3fff &&
            rig::value<unsigned>(read_,contact.owner+0xa8)==contact.spawn &&
            rig::value<std::uintptr_t>(read_,*game+0x3130b0+(contact.spawn>>16)*8)==contact.owner &&
            rig::typed(read_,base_,scope.sensor,0x1b403b8) && rig::started(read_,scope.sensor)==contact.sensor_identity && contact.sensor_identity==scope.sensor_identity &&
            rig::value<unsigned char>(read_,scope.sensor+0x38).value_or(0)!=0 &&
            rig::value<std::uintptr_t>(read_,scope.sensor+0x10)==scope.camera.player &&
            rig::value<std::uintptr_t>(read_,scope.sensor+0x40)==scope.camera.camera &&
            rig::typed(read_,base_,scope.camera.camera,camera_vtable,camera_secondary_vtable) &&
            rig::started(read_,scope.camera.camera)==contact.camera_identity && contact.camera_identity==scope.camera.camera_identity &&
            rig::value<std::uintptr_t>(read_,scope.camera.camera+0x10)==scope.camera.player &&
            rig::value<int>(read_,scope.camera.camera+0x25200)==0 && rig::value<int>(read_,scope.camera.camera+0x25204)==0 &&
            rig::value<unsigned char>(read_,scope.camera.camera+0x251b9)==0 && rig::value<unsigned char>(read_,scope.camera.camera+0x25260)==0;
    }
    static std::uintptr_t __fastcall occlusion(void* sensor,void* entity,const void* origin,const void* axis,
            const void* point,unsigned range,unsigned char* output) {
        return instance_->occlusion_result(sensor,entity,origin,axis,point,range,output,reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
    }
    std::uintptr_t occlusion_result(void* sensor,void* entity,const void* origin,const void* axis,
            const void* point,unsigned range,unsigned char* output,std::uintptr_t caller) {
        return counted_callback(this,[&] {return occlusion_result_body(sensor,entity,origin,axis,point,range,output,caller);});
    }
    std::uintptr_t occlusion_result_body(void* sensor,void* entity,const void* origin,const void* axis,
            const void* point,unsigned range,unsigned char* output,std::uintptr_t caller) {
        auto* self=this;const auto* observed_scope=context_;std::optional<Contact> observed;
        std::array<float,3> frozen_point{};
        try {
            if(observed_scope && observed_scope->stage==0 && range==0 &&
               (caller==base_+0x3b87b5 || caller==base_+0x3b8b05) &&
               reinterpret_cast<std::uintptr_t>(sensor)==observed_scope->sensor &&
               read_(reinterpret_cast<std::uintptr_t>(point),frozen_point.data(),sizeof(frozen_point))) {
                const auto game=rig::value<std::uintptr_t>(read_,base_+0x39a5290);
                const auto world=game?rig::value<std::uintptr_t>(read_,*game+0x60410):std::nullopt;
                const auto spawn=rig::value<unsigned>(read_,reinterpret_cast<std::uintptr_t>(entity)+0xa8);
                const auto sensor_id=rig::started(read_,observed_scope->sensor);
                if(world && spawn && sensor_id) {
                    Contact candidate{};candidate.owner=reinterpret_cast<std::uintptr_t>(entity);candidate.sensor=observed_scope->sensor;
                    candidate.world=*world;candidate.spawn=*spawn;candidate.sensor_identity=*sensor_id;candidate.camera_identity=observed_scope->camera.camera_identity;
                    candidate.point={frozen_point[0],frozen_point[1],frozen_point[2]};candidate.tick=GetTickCount64();candidate.epoch=observed_scope->hand.epoch;
                    candidate.revision=observed_scope->revision;candidate.publication_tick=observed_scope->hand.tick;
                    if(contact_valid(*observed_scope,candidate,candidate.tick))observed=candidate;
                }
            }
        }catch(...) {}
        const auto result=occlusion_(sensor,entity,origin,axis,point,range,output); // original once, raw return preserved
        try {
            std::array<float,3> after{};
            if(observed && (result&0xffu)==0 && context_==observed_scope &&
               read_(reinterpret_cast<std::uintptr_t>(point),after.data(),sizeof(after)) && after==frozen_point &&
               contact_valid(*observed_scope,*observed,GetTickCount64())) {
                std::lock_guard<std::mutex> guard(mutex_);
                const auto side=observed_scope->side;
                if(routing_.selected()==side && routing_.revision()==observed_scope->revision &&
                   hands_[side].epoch==observed_scope->hand.epoch && hands_[side].tick==observed_scope->hand.tick) {
                    if(observed_scope->contact_count<observed_scope->contacts.size())observed_scope->contacts[observed_scope->contact_count++]=*observed;
                    else observed_scope->contacts_overflow=true;
                }
            }
        }catch(...) {}
        try {
            const auto& scope=retrieved_;
            if(self->active_.load(std::memory_order_acquire) && scope.ranked && scope.scope==context_ && context_ &&
               caller==self->base_+0x3b8b05 && reinterpret_cast<std::uintptr_t>(sensor)==context_->sensor && range==0 && output &&
               self->fresh(*context_,GetTickCount64())) {
                const auto flag=rig::value<unsigned char>(self->read_,reinterpret_cast<std::uintptr_t>(output));
                const auto character=self->character_type(reinterpret_cast<std::uintptr_t>(entity));
                if(flag && continue_native_use_scan(result,*flag,true,character)) {
                    // Keep native rejected result. Native stage0's outflag0
                    // early break assumes camera-fraction ordering. Our object
                    // ordering needs the next candidate's independent LOS test.
                    *output=1;++self->occluded_scan_continued_;
                }
            }
        } catch(...) {}
        return result;
    }
    bool idle_character_publication(const Context& scope) {
        std::lock_guard<std::mutex> guard(mutex_);
        if(scope.side<0 || scope.side>1 || !scope.ownership_revision || routing_.ownership_revision()!=scope.ownership_revision)return false;
        const auto& current=hands_[scope.side];
        const auto now=GetTickCount64();
        return routing_.locked()<0 && epoch_hint_==scope.hand.epoch && published_[scope.side] && current.active &&
            current.epoch==scope.hand.epoch && current.player==scope.camera.player && current.camera==scope.camera.camera &&
            current.generation==scope.camera.camera_identity.generation && current.tick>=scope.hand.tick &&
            current.tick<=now && now-current.tick<=250 &&
            !hands_[0].reach && !hands_[1].reach;
    }
    bool idle_character_gaze(const Context& scope,std::uint64_t now) {
        if(scope.stage!=1 || scope.side<0 || !scope.hand.carry || !physical_grab_ || !physical_grab_->enabled())return false;
        const bool reserved=nonlethal_grip_reserved_.load(std::memory_order_acquire);
        const auto reservation_revision=nonlethal_grip_revision_.load(std::memory_order_acquire);
        const auto offered=reserved?NativeOfferedChoke::process_owner().offered(now):std::nullopt;
        const auto& carry=*scope.hand.carry;
        if(reserved && !native_character_choke_owner(offered,scope.camera,carry))return false;
        const auto idle=[&] {
            if(stopping_ || !active_ || logic_()!=2 || nonlethal_grip_reserved_.load(std::memory_order_acquire)!=reserved ||
               nonlethal_grip_revision_.load(std::memory_order_acquire)!=reservation_revision ||
               (reserved && !NativeOfferedChoke::process_owner().current(*offered,GetTickCount64())) ||
               !fresh(scope,GetTickCount64()))return false;
            const auto camera=native_player_camera(read_,base_,scope.camera.camera);
            if(!camera || camera->authored_view() || camera->player!=scope.camera.player || camera->body!=scope.camera.body ||
               camera->controller!=scope.camera.controller || camera->physics!=scope.camera.physics ||
               camera->heading_delegate!=scope.camera.heading_delegate || camera->physics_identity!=scope.camera.physics_identity ||
               camera->camera_identity!=scope.camera.camera_identity)return false;
            return idle_character_publication(scope);
        };
        return native_character_gaze_idle(read_,base_,scope.camera,*scope.hand.carry,scope.hand.epoch,now,
            [&](std::uint64_t tick){return physical_grab_->diagnostics(tick);},idle,[] {return GetTickCount64();});
    }
    template<unsigned Index> static void* __fastcall queue(void* manager,void* id,void* filter,const InteractionRay* ray,UINT mask,UINT flags) {
        auto* self=instance_;const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
        return counted_callback(self,[&] {return queue_body<Index>(manager,id,filter,ray,mask,flags,caller);});
    }
    template<unsigned Index> static void* queue_body(void* manager,void* id,void* filter,const InteractionRay* ray,UINT mask,UINT flags,std::uintptr_t caller) {
        CrashOperation operation("native interaction queued ray");
        auto* self=instance_;alignas(16) InteractionRay copy{};
        if constexpr(Index==1) {
            if(self->observe_lean_ && lean_context_ && lean_transaction_ && (caller==self->base_+0x3b9962 || caller==self->base_+0x3b9a49)) {
                NativeLeanQueryEvent event{};event.state=*lean_context_;event.phase=2;event.thread=GetCurrentThreadId();
                event.caller=caller;event.id_pointer=reinterpret_cast<std::uintptr_t>(id);event.mask=mask;event.flags=flags;
                event.request_pointer=reinterpret_cast<std::uintptr_t>(filter);event.axes_pointer=reinterpret_cast<std::uintptr_t>(ray);
                bool accepted=false;
                const auto result=passive_lean_call([&] {
                    self->lean_trace_.queue();
                    const auto current=native_lean_query_snapshot(self->read_,self->base_,lean_context_->sensor);
                    accepted=self->active_.load(std::memory_order_acquire) && self->logic_()==2 && current &&
                        same_lean_resources(*lean_context_,*current) && native_lean_query_arguments(self->read_,*current,caller,self->base_,
                            reinterpret_cast<std::uintptr_t>(manager),reinterpret_cast<std::uintptr_t>(filter),
                            reinterpret_cast<std::uintptr_t>(ray),mask,flags,event);
                    const auto before_id=rig::value<int>(self->read_,event.id_pointer);
                    accepted=accepted && before_id.has_value();if(before_id) event.query_id=*before_id;
                    if(!accepted) {event.rejection=2;self->lean_trace_.reject();}
                    lean_transaction_->push(event,GetTickCount64());
                },[&] {
                    const auto original=[&] {return self->queues_[Index](manager,id,filter,ray,mask,flags);};
                    return self->observe_lean_lifetime_?self->lean_lifetime_.queue_call(accepted?&event:nullptr,original):original();
                },[&](void* returned) {
                    event.phase=3;event.result_pointer=reinterpret_cast<std::uintptr_t>(returned);
                    const auto after=native_lean_query_snapshot(self->read_,self->base_,lean_context_->sensor);
                    std::array<std::byte,112> request_after{};std::array<std::byte,48> axes_after{};
                    const auto after_id=rig::value<int>(self->read_,event.id_pointer);
                    if(!accepted || returned!=id || !after || !same_lean_resources(event.state,*after) || !after_id ||
                       !self->read_(reinterpret_cast<std::uintptr_t>(filter),request_after.data(),112) || request_after!=event.request ||
                       !self->read_(reinterpret_cast<std::uintptr_t>(ray),axes_after.data(),48) || axes_after!=event.axes) {
                        event.rejection=3;self->lean_trace_.reject();
                    }
                    if(after) event.state=*after;if(after_id) event.query_id=*after_id;
                    lean_transaction_->push(event,GetTickCount64());
                });
                return result;
            }
        }
        // The queued ABI embeds the ray at filter+0x30; its fourth argument is
        // a separate three-axis frame. 0x08ffb0 eagerly copies 112+48 bytes.
        alignas(16) std::array<std::byte,112> filter_copy{};
        alignas(16) InteractionRay frame_copy{};
        void* selected_filter=filter;const auto* selected_frame=ray;bool volume=false;int volume_slot=-1;
        if(filter && self->read_(reinterpret_cast<std::uintptr_t>(filter),filter_copy.data(),filter_copy.size()) &&
           self->read_(reinterpret_cast<std::uintptr_t>(ray),&frame_copy,sizeof(frame_copy))) {
            const auto* input=reinterpret_cast<const InteractionRay*>(static_cast<const std::byte*>(filter)+0x30);
            if constexpr(Index==0) {
                if(context_ && context_->stage==1 && caller==self->base_+0x3b9037 && mask==0x408 && flags==0) {
                    std::uintptr_t original_shape{};std::memcpy(&original_shape,filter_copy.data()+0x28,8);
                    bool character_shape=false;
                    for(unsigned slot=3;slot<5;++slot)
                        character_shape=character_shape || (original_shape && rig::value<std::uintptr_t>(self->read_,context_->sensor+0x48+slot*8)==original_shape);
                    if(character_shape) {
                        if(self->idle_character_gaze(*context_,GetTickCount64())) {
                            ++self->character_gaze_admitted_;
                            return self->queues_[Index](manager,id,filter,ray,mask,flags);
                        }
                        ++self->character_gaze_declined_;
                    }
                }
            }
            // Each request contains its own native geometry pointer at +0x28.
            // Keep all native shapes/ranges centred on the tracked hand; these
            // are not interchangeable cone samples. The queue retains the shape
            // pointer, so a stack copy or mutable enlargement would be unsafe.
            const auto* selected=self->ray(input,copy,caller);
            if(selected!=input && context_) {
                if constexpr(Index==0) {
                    // Only the three object jobs at the verified stage1 callsite.
                    // Preserve masks, permissions, exclusion/filter fields and IDs.
                    if(context_->stage==1 && mask==0x20cc38 && flags==0 && !context_->hand.reach) {
                        std::uintptr_t original_shape{};std::memcpy(&original_shape,filter_copy.data()+0x28,8);
                        bool owned_shape=false;
                        for(unsigned slot=0;slot<3;++slot) if(rig::value<std::uintptr_t>(self->read_,context_->sensor+0x48+slot*8)==original_shape && original_shape) {owned_shape=true;volume_slot=static_cast<int>(slot);}
                        const auto game=rig::value<std::uintptr_t>(self->read_,self->base_+0x39a5290);
                        const auto manager_at=reinterpret_cast<std::uintptr_t>(manager);
                        const bool owned_manager=game && manager_at==*game+0x60280 &&
                            rig::value<std::uintptr_t>(self->read_,manager_at+0x190)==rig::value<std::uintptr_t>(self->read_,*game+0x60410) &&
                            rig::value<std::uintptr_t>(self->read_,manager_at+0x190).value_or(0)!=0;
                        const auto volume_ray=hand_volume_ray(copy,context_->hand.volume_centre,context_->hand.hand.basis[0]);
                        const auto shape=owned_shape && owned_manager && volume_ray?NativeHandVolume::shape(self->base_,self->read_):0;
                        if(shape) {copy=*volume_ray;std::memcpy(filter_copy.data()+0x28,&shape,8);volume=true;++self->volume_requests_;}
                        else ++self->volume_rejected_;
                    }
                }
                std::memcpy(filter_copy.data()+0x30,&copy,sizeof(copy));
                for(unsigned axis=0;axis<3;++axis) {
                    const auto& v=context_->hand.hand.basis[axis];
                    frame_copy.f[axis*4]=static_cast<float>(v.x);frame_copy.f[axis*4+1]=static_cast<float>(v.y);
                    frame_copy.f[axis*4+2]=static_cast<float>(v.z);
                }
                selected_filter=filter_copy.data();selected_frame=&frame_copy;
            }
        }
        const auto selected_queue=volume?reinterpret_cast<Queue>(self->base_+0x95370):self->queues_[Index];
        const auto result=selected_queue(manager,id,selected_filter,selected_frame,mask,flags);
        if(selected_filter!=filter && context_ && context_->stage==1 && context_->side>=0) {
            std::lock_guard<std::mutex> lock(self->mutex_);
            const auto token=rig::value<unsigned>(self->read_,reinterpret_cast<std::uintptr_t>(id));
            if(context_->revision==self->routing_.revision() && result==id && token && *token!=0xffffffff) {
                self->queued_revision_[context_->side]=context_->revision;
                if(volume && volume_slot>=0) self->volume_tickets_[static_cast<unsigned>(volume_slot)]={context_->sensor,*token,context_->side,context_->revision,GetTickCount64()};
            }
        }
        return result;
    }
    std::optional<Vec3> static_target_point_locked(unsigned side,std::uint64_t now) {
        if(!live_target(side,now))return {};
        try {
            const auto object=target_object_[side];const auto identity=rig::started(read_,object);
            const auto owner=rig::value<std::uintptr_t>(read_,object+0x10);
            const auto model=owner?rig::value<std::uintptr_t>(read_,*owner+0xf0):std::nullopt;
            if(!identity || identity->generation!=target_generation_[side] || !model || !rig::typed(read_,base_,*model,0x1b0dd88)) return {};
            const auto bounds=rig::value<std::array<float,8>>(read_,*model+0x1840);
            if(!bounds) return {};
            const Vec3 minimum{(*bounds)[0],(*bounds)[1],(*bounds)[2]},maximum{(*bounds)[4],(*bounds)[5],(*bounds)[6]};
            if(!finite(minimum) || !finite(maximum) || minimum.x>maximum.x || minimum.y>maximum.y || minimum.z>maximum.z ||
               length(maximum-minimum)>3 || length(maximum-minimum)<.001 ||
               rig::started(read_,object)!=identity || rig::value<std::uintptr_t>(read_,object+0x10)!=owner ||
               rig::value<std::uintptr_t>(read_,*owner+0xf0)!=model) return {};
            const auto& hand=hands_[side].volume_centre;
            return Vec3{std::clamp(hand.x,minimum.x,maximum.x),std::clamp(hand.y,minimum.y,maximum.y),std::clamp(hand.z,minimum.z,maximum.z)};
        } catch(...) {return {};}
    }
    std::optional<Vec3> target_point_locked(unsigned side,std::uint64_t now) {
        if(const auto native_bounds=static_target_point_locked(side,now))return native_bounds;
        if(!live_target(side,now))return {};
        try {
            auto& anchor=contact_anchor_[side];
            Context scope{};scope.side=static_cast<int>(side);scope.stage=0;scope.sensor=anchor.sensor;scope.revision=routing_.revision();
            scope.hand=hands_[side];scope.hand.tick=anchor.publication_tick;
            scope.sensor_identity=anchor.sensor_identity;
            scope.camera.player=hands_[side].player;scope.camera.camera=hands_[side].camera;scope.camera.camera_identity=anchor.camera_identity;
            // Admission requires the exact stage publication. A retained world
            // contact may outlive that publication, but never move backwards in
            // its source clock or receive a fresh timestamp from a new hand.
            if(anchor.publication_tick<=hands_[side].tick && anchor.component==target_object_[side] && anchor.owner==target_owner_[side] && anchor.spawn==target_spawn_[side] &&
               rig::started(read_,anchor.component)==anchor.component_identity && contact_valid(scope,anchor,now))return anchor.point;
            anchor={};
        }catch(...) {contact_anchor_[side]={};}
        return {};
    }

public:
    struct QualifiedTarget {
        NativeInteractionTargetIdentity identity{};std::uintptr_t owner{};unsigned spawn{};
        std::uint64_t publication_tick{},selection_tick{};std::optional<Vec3> point;bool native_only{};
    };
private:
    struct Seen {QualifiedTarget target{};std::uint64_t tick{};};
    std::array<Seen,2> seen_{};
    std::uintptr_t held_owner(const NativeInteractionTargetIdentity& id) const noexcept {
        for(const auto& item:seen_)if(item.tick && item.target.identity==id)return item.target.owner;
        return 0;
    }
    // Revalidated against the live entity table; the hand must still be published for the same epoch.
    std::optional<QualifiedTarget> held_locked(unsigned side,std::uint64_t now) {
        if(side>=2)return {};
        const auto& item=seen_[side];
        if(!item.tick || now<item.tick || now-item.tick>held_grace_ms || !published_[side] || !hands_[side].epoch ||
           hands_[side].epoch!=item.target.identity.epoch || now<hands_[side].tick || now-hands_[side].tick>250)return {};
        try {
            const auto& t=item.target;const auto game=rig::value<std::uintptr_t>(read_,base_+0x39a5290);
            const auto identity=rig::started(read_,t.identity.object);
            const auto camera=hands_[side].camera;const auto camera_id=rig::started(read_,camera);
            if(!game || !identity || identity->generation!=t.identity.generation || !camera_id || camera_id->generation!=hands_[side].generation ||
               !rig::typed(read_,base_,camera,camera_vtable,camera_secondary_vtable) ||
               rig::value<std::uintptr_t>(read_,*game+0x3130b0)!=hands_[side].player || (t.spawn>>16)>=0x3fff ||
               rig::value<std::uintptr_t>(read_,*game+0x3130b0+(t.spawn>>16)*8)!=t.owner ||
               rig::value<unsigned>(read_,t.owner+0xa8)!=t.spawn)return {};
        }catch(...){return {};}
        return item.target;
    }
    std::optional<QualifiedTarget> native_selected_locked(std::uint64_t now) {
        if(!native_object_ || !epoch_hint_ || now<native_tick_ || now-native_tick_>250)return {};
        try {
            const auto identity=rig::started(read_,native_object_);
            const auto owner=rig::value<std::uintptr_t>(read_,native_object_+0x10);
            const auto game=rig::value<std::uintptr_t>(read_,base_+0x39a5290);
            const auto spawn=owner?rig::value<unsigned>(read_,*owner+0xa8):std::nullopt;
            if(!identity || !owner || !game || !spawn || *spawn==0x3fff0010u || *spawn==0xffffffffu || (*spawn>>16)>=0x3fff ||
               rig::value<std::uintptr_t>(read_,*game+0x3130b0+(*spawn>>16)*8)!=*owner)return {};
            const auto at=rig::value<std::array<float,3>>(read_,*owner+0x300);
            if(!at)return {};
            for(const auto v:*at)if(!std::isfinite(v))return {};
            if(rig::started(read_,native_object_)!=identity)return {};
            QualifiedTarget out{{native_object_,identity->generation,0,epoch_hint_,3},*owner,*spawn,native_tick_,native_tick_,Vec3{(*at)[0],(*at)[1],(*at)[2]}};
            out.native_only=true;return out;
        }catch(...){return {};}
    }
    std::optional<QualifiedTarget> newest_held_locked(std::uint64_t now) {
        std::optional<QualifiedTarget> best;std::uint64_t best_tick=0;
        for(unsigned side=0;side<2;++side)if(const auto held=held_locked(side,now))if(seen_[side].tick>=best_tick){best=held;best_tick=seen_[side].tick;}
        return best;
    }
public:
    struct DiagnosticSnapshot {
        int side{-1};std::uint64_t epoch{},revision{},publication_tick{},target_tick{},ready_revision{};
        std::uintptr_t player{},camera{},component{};int component_generation{-1};
        bool published{},qualified{};
        std::array<UINT64,static_cast<unsigned>(NativeUseQueryOutcome::count)> query_outcomes{};
    };
    std::optional<DiagnosticSnapshot> diagnostics(std::uint64_t now) {
        std::unique_lock<std::mutex> lock(mutex_,std::try_to_lock);if(!lock.owns_lock())return {};
        DiagnosticSnapshot out{};out.side=routing_.selected();out.revision=routing_.revision();
        for(unsigned i=0;i<out.query_outcomes.size();++i)out.query_outcomes[i]=query_outcomes_[i].load();
        if(out.side>=0) {
            const auto side=static_cast<unsigned>(out.side);const auto& p=hands_[side];
            out.epoch=p.epoch;out.publication_tick=p.tick;out.player=p.player;out.camera=p.camera;
            out.component=target_object_[side];out.component_generation=target_generation_[side];
            out.target_tick=target_tick_[side];out.ready_revision=ready_revision_[side];out.published=published_[side];
            out.qualified=live_target(side,now);
        }
        return out;
    }
    // Legacy single hand: routed every cycle and attributed to `side` (right by default).
    void publish(const NativePlayerCamera& camera,const WorldTrackedPose& hand,std::uint64_t epoch,std::uint64_t tick,bool active,int side=1) {
        std::lock_guard<std::mutex> lock(mutex_);epoch_hint_=epoch;
        const unsigned index=side==0?0u:1u;
        if(hands_[index].epoch!=epoch || hands_[index].player!=camera.player || hands_[index].camera!=camera.camera ||
           hands_[index].generation!=camera.camera_identity.generation || hands_[index].active!=active || hands_[index].reach || published_[1-index]) {invalidate_selection();routing_.invalidate_owner();}
        const auto revision=routing_.revision();
        hands_[index]={tick,epoch,camera.player,camera.camera,camera.camera_identity.generation,hand,active,hand.position};
        hands_[index].grip=hand;
        hands_[1-index]={};published_={false,false};published_[index]=active;
        routing_.next(published_,tick,qualified_selected(tick));if(revision!=routing_.revision()) invalidate_selection();
    }
    // Search both hands; keep a freshly qualified native target's hand until
    // native eligibility is lost. An opposite grip always overrides this dwell.
    void publish_hands(const NativePlayerCamera& camera,std::array<std::optional<WorldTrackedPose>,2> hands,
            std::uint64_t epoch,std::uint64_t tick,std::array<std::optional<Vec3>,2> grip_centres={},std::array<bool,2> reach={},
            std::array<std::optional<WorldTrackedPose>,2> grips={},std::optional<rig::NativeCarrySnapshot> carry={}) {
        std::lock_guard<std::mutex> lock(mutex_);epoch_hint_=epoch;
        for(unsigned side=0;side<2;++side) if(hands_[side].active!=hands[side].has_value() || (hands_[side].active && (hands_[side].epoch!=epoch ||
            hands_[side].player!=camera.player || hands_[side].camera!=camera.camera ||
            hands_[side].generation!=camera.camera_identity.generation))) {invalidate_selection();routing_.reset();break;}
        const auto revision=routing_.revision();
        std::array<bool,2> moving{};
        for(unsigned side=0;side<2;++side) if(hands[side]) { // moved >3 cm or >5 degrees within the last 300 ms
            auto& m=motion_[side];const auto dir=hands[side]->basis[0];
            if(!m.tick || tick<m.tick) m={hands[side]->position,dir,tick,0};
            else if(tick-m.tick>=100) {
                if(length(hands[side]->position-m.position)>.03 || dot(dir,m.direction)<.9962) m.until=tick+300;
                m.position=hands[side]->position;m.direction=dir;m.tick=tick;
            }
            moving[side]=tick<m.until;
        } else motion_[side]={};
        for(unsigned side=0;side<2;++side) {
            if(hands_[side].reach!=(hands[side].has_value() && reach[side]))routing_.invalidate_owner();
            hands_[side]=hands[side]?Publication{tick,epoch,camera.player,camera.camera,camera.camera_identity.generation,*hands[side],true,grip_centres[side] && finite(*grip_centres[side]) && length(*grip_centres[side]-hands[side]->position)<=.5?*grip_centres[side]:hands[side]->position,reach[side]}:Publication{};
            published_[side]=hands[side].has_value();
            if(published_[side] && grips[side] && grab_pose_valid(*grips[side]))hands_[side].grip=grips[side];
            if(published_[side])hands_[side].carry=carry;
        }
        routing_.next(published_,tick,qualified_selected(tick),moving);if(revision!=routing_.revision()) invalidate_selection();
    }
    // While locked only that hand's ray is routed, so the game's selection (and its
    // native Interact) belongs to the hand pressing the grip until it resolves.
    void lock(int side) {std::lock_guard<std::mutex> lock(mutex_);const auto revision=routing_.revision();routing_.lock(side);if(revision!=routing_.revision()) invalidate_selection();}
    void unlock() {std::lock_guard<std::mutex> lock(mutex_);const auto revision=routing_.revision();routing_.unlock();if(revision!=routing_.revision()) invalidate_selection();}
    void clear() {full_hand_contact_.clear();clear_anatomical_contact();clear_door_contact();std::lock_guard<std::mutex> lock(mutex_);seen_={};hands_={};published_={false,false};routing_.reset();invalidate_selection();}
    // True while the native game has an interactable selected, sampled within the last quarter second.
    bool has_target(std::uint64_t now) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(native_object_ && now>=native_tick_ && now-native_tick_<=250) return true;
        for(unsigned side=0;side<2;++side)
            if(live_target(side,now)) return true;
        return false;
    }
    bool query_ready(int side,std::uint64_t now) {
        if(side<0 || side>1) return false;
        std::lock_guard<std::mutex> lock(mutex_);
        return published_[side] && routing_.selected()==side && ready_revision_[side]==routing_.revision() &&
            target_tick_[side] && now>=target_tick_[side] && now-target_tick_[side]<=250;
    }
    // Per-hand selection: only cycles whose ray was this hand's update it.
    bool has_target(int side,std::uint64_t now) {
        if(side<0||side>1) return false;
        std::lock_guard<std::mutex> lock(mutex_);
        return live_target(static_cast<unsigned>(side),now);
    }
    std::optional<QualifiedTarget> talk_target(int side,std::uint64_t now) {
        if(side<0 || side>1)return {};
        std::lock_guard<std::mutex> lock(mutex_);
        const auto& offer=talk_offers_[side];
        if(!live_target(static_cast<unsigned>(side),now) || !offer || offer->component!=target_object_[side] ||
           offer->generation!=static_cast<std::uint32_t>(target_generation_[side]) || offer->owner!=target_owner_[side] ||
           offer->spawn!=target_spawn_[side] || !native_talk_offer_current(read_,base_,*offer))return {};
        const NativeInteractionTargetIdentity identity{offer->component,target_generation_[side],static_cast<unsigned>(side),hands_[side].epoch,1};
        return QualifiedTarget{identity,offer->owner,offer->spawn,hands_[side].tick,target_tick_[side],{}};
    }
    // Entity the game currently offers this hand for Use (its highlight), 0 if none.
    std::uintptr_t target_owner(int side,std::uint64_t now) {
        if(side<0||side>1) return 0;
        std::lock_guard<std::mutex> lock(mutex_);
        return live_target(static_cast<unsigned>(side),now)?target_owner_[static_cast<unsigned>(side)]:0;
    }
    bool grip_use_target(int side,std::uint64_t now) noexcept {
        if(side<0 || side>1)return false;
        mutex_.lock();
        __try {
            __try {return grip_use_target_body(side,now);}__finally {mutex_.unlock();}
        }__except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
private:
    bool grip_use_target_body(int side,std::uint64_t now) {
        if(side<0 || side>1)return false;
        try {
            const auto selected=static_cast<unsigned>(side);
            if(!live_target(selected,now))return false;
            const auto component=target_object_[selected],door=target_owner_[selected];
            if(takeable(door))return live_target(selected,now);
            const auto identity=rig::started(read_,component);
            const auto members=rig::list(read_,door+0x268,256);
            if(!identity || identity->generation!=target_generation_[selected] || !members || !members->packed_count ||
               !rig::typed(read_,base_,component,0x1c569c8) || !rig::typed(read_,base_,door,0x1cb7c68) ||
               rig::value<std::uintptr_t>(read_,door+0x370)!=component || rig::value<std::uintptr_t>(read_,component+0x10)!=door)return false;
            std::array<std::uintptr_t,256> entries{},again{};unsigned matches{};
            for(unsigned index=0;index<members->packed_count;++index) {
                const auto entry=rig::value<std::uintptr_t>(read_,members->data+index*8ull);
                if(!entry || !rig::pointer(*entry) || std::find(entries.begin(),entries.begin()+index,*entry)!=entries.begin()+index)return false;
                entries[index]=*entry;if(*entry==component)++matches;
            }
            if(matches!=1 || !read_(members->data,again.data(),members->packed_count*sizeof(std::uintptr_t)) || entries!=again)return false;
            const auto final_members=rig::list(read_,door+0x268,256);
            return final_members && final_members->data==members->data && final_members->packed_count==members->packed_count &&
                final_members->packed_capacity==members->packed_capacity && rig::started(read_,component)==identity &&
                rig::typed(read_,base_,component,0x1c569c8) && rig::typed(read_,base_,door,0x1cb7c68) &&
                rig::value<std::uintptr_t>(read_,door+0x370)==component && rig::value<std::uintptr_t>(read_,component+0x10)==door && live_target(selected,now);
        }catch(...) {return false;}
    }
public:
    // The native prompt belongs only to the currently selected/locked hand's
    // freshly consumed sensor result; never choose whichever side is first.
    std::optional<unsigned> target_side(std::uint64_t now) {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto side=routing_.selected();
        if(side<0 || !live_target(static_cast<unsigned>(side),now)) return {};
        try {
            const auto object=target_object_[side];const auto identity=rig::started(read_,object);
            if(!identity || identity->generation!=target_generation_[side] || rig::started(read_,object)!=identity) return {};
            return static_cast<unsigned>(side);
        } catch(...) {return {};}
    }
    std::optional<NativeInteractionTargetIdentity> target_identity(std::uint64_t now) {
        std::lock_guard<std::mutex> lock(mutex_);const auto side=routing_.selected();
        if(side<0 || !live_target(static_cast<unsigned>(side),now))return {};
        try {
            const auto object=target_object_[side];const auto identity=rig::started(read_,object);
            const auto owner=rig::value<std::uintptr_t>(read_,object+0x10);
            if(!identity || identity->generation!=target_generation_[side] || !owner || !rig::pointer(*owner) ||
               rig::started(read_,object)!=identity || rig::value<std::uintptr_t>(read_,object+0x10)!=owner)return {};
            // Routing revision is NOT part of the prompt identity: with both hands aiming it bumps
            // every 120 ms dwell, faster than the async prompt-art pipeline, so the prompt
            // never matched and always fell back flat. Staleness is enforced by live_target.
            const NativeInteractionTargetIdentity target{object,identity->generation,static_cast<unsigned>(side),hands_[side].epoch,1};
            return target?std::optional<NativeInteractionTargetIdentity>{target}:std::nullopt;
        }catch(...){return {};}
    }
    // Object cue belongs to the qualified selected component, never an unrelated
    // forward-ray hit. Bounds are the exact rendered static-model world cache.
    // Each hand's last qualified target is remembered for held_grace_ms so both hands can show an
    // indicator at once and a routing flip never drops the prompt to the flat HUD.
    static constexpr std::uint64_t held_grace_ms=500;
    std::optional<QualifiedTarget> qualified_target(std::uint64_t now) {
        std::lock_guard<std::mutex> lock(mutex_);const auto selected=routing_.selected();
        if(selected<0 || !live_target(static_cast<unsigned>(selected),now))return {};
        const auto side=static_cast<unsigned>(selected);const auto object=target_object_[side];
        const auto owner=rig::value<std::uintptr_t>(read_,object+0x10);
        const auto spawn=owner?rig::value<unsigned>(read_,*owner+0xa8):std::nullopt;
        if(!owner || !spawn)return {};
        QualifiedTarget out{{object,target_generation_[side],side,hands_[side].epoch,1},*owner,*spawn,
            hands_[side].tick,target_tick_[side],target_point_locked(side,now)};
        if(!live_target(side,now) || rig::value<std::uintptr_t>(read_,object+0x10)!=owner ||
           rig::value<unsigned>(read_,*owner+0xa8)!=spawn)return {};
        // Keep the last good world point if this frame's anchor is momentarily unavailable.
        if(!out.point && seen_[side].tick && seen_[side].target.identity==out.identity)out.point=seen_[side].target.point;
        seen_[side]={out,now};
        return out;
    }
    // Prompt-path variants: strict live result first, else the newest remembered target. A hand
    // routing flip wipes the live selection for a cycle; these keep the prompt bound to a world
    // anchor (so the flat HUD copy stays suppressed) instead of dropping it for that cycle.
    bool has_target_or_held(std::uint64_t now) {
        if(has_target(now))return true;
        std::lock_guard<std::mutex> lock(mutex_);return newest_held_locked(now).has_value();
    }
    // The game's own selected use object (view-centre use with no hand ray, e.g. a typewriter):
    // entity validated against the live table, origin from entity+0x300 (native units, feet/base).
    // Identity is side 0, revision 3 (hands use 1, takedowns 2); `native_only` lets callers lift the cue.
    // Needs the pose epoch from the hand publication (epoch_hint_).
    std::optional<QualifiedTarget> native_selected_target(std::uint64_t now) {
        std::lock_guard<std::mutex> lock(mutex_);return native_selected_locked(now);
    }
    std::optional<NativeInteractionTargetIdentity> prompt_identity(std::uint64_t now) {
        if(const auto live=target_identity(now))return live;
        std::lock_guard<std::mutex> lock(mutex_);
        if(const auto held=newest_held_locked(now))return held->identity;
        if(const auto native=native_selected_locked(now))return native->identity;
        return {};
    }
    std::optional<QualifiedTarget> prompt_target(std::uint64_t now) {
        if(const auto live=qualified_target(now))return live;
        std::lock_guard<std::mutex> lock(mutex_);
        if(const auto held=newest_held_locked(now))return held;
        return native_selected_locked(now);
    }
    // The other hand's remembered target (independent second indicator); never the primary's own.
    std::optional<QualifiedTarget> second_target(const NativeInteractionTargetIdentity& primary,std::uint64_t now) {
        std::lock_guard<std::mutex> lock(mutex_);
        for(unsigned side=0;side<2;++side) {
            if(primary.side==side)continue;
            if(const auto held=held_locked(side,now))if(held->identity!=primary && held->owner!=held_owner(primary))return held;
        }
        return {};
    }
    std::optional<Vec3> target_point(int side,std::uint64_t now) {
        if(side<0 || side>1)return {};
        std::lock_guard<std::mutex> lock(mutex_);return target_point_locked(static_cast<unsigned>(side),now);
    }

    void attach_physical_grab(NativeGrab* grab) {
        if(door_installed_)throw std::runtime_error("Native door owner must stop before changing physical grab");
        physical_grab_=grab;if(grab)grab->require_obstruction();
    }
    void install_door_interaction() {
        if(!active_ || stopping_ || !physical_grab_ || !physical_grab_->enabled())
            throw std::runtime_error("Native door interaction requires installed interaction and physical grab hooks");
        door_installed_=true;
        if(!NativeDoorInteraction::process_owner().install(base_,read_,logic_,physical_grab_))
            throw std::runtime_error("Cannot install native door interaction owner");
    }
    void install(std::uintptr_t base,Read read,Logic logic,bool route,const std::filesystem::path& file,bool observe_lean=false,bool observe_lean_lifetime=false) {
        if(active_.load(std::memory_order_acquire) || depth_.load(std::memory_order_acquire) ||
           std::any_of(enabled_.begin(),enabled_.end(),[](bool enabled){return enabled;}) ||
           std::any_of(ranking_enabled_.begin(),ranking_enabled_.end(),[](bool enabled){return enabled;}) || range_origin_enabled_)
            throw std::runtime_error("Native interaction hooks are still installed");
        if(stopping_.load(std::memory_order_acquire))
            throw std::runtime_error("Native interaction stop is still pending");
        if(!anatomical_contact_.quiescent() || !full_hand_contact_.quiescent())throw std::runtime_error("Anatomical contact stop is still pending");
        if(observe_lean_lifetime && !observe_lean) throw std::runtime_error("Native Lean lifetime requires typed Lean observation");
        if(!NativeBlockEligibility::process_owner().ready_for_install())
            throw std::runtime_error("Native Block eligibility stop is still pending");
        if(!NativeSlashSceneRate::process_owner().ready_for_install())
            throw std::runtime_error("Native Slash scene-rate stop is still pending");
        if(!NativeChokeDistanceScope::process_owner().ready_for_install())
            throw std::runtime_error("Native Choke scope stop is still pending");
        if(!NativeOfferedChoke::process_owner().ready_for_install())
            throw std::runtime_error("Native Choke offer observer stop is still pending");
        if(!anatomical_contact_.start())throw std::runtime_error("Cannot start anatomical contact admission");
        base_=base;read_=read;logic_=logic;route_=route;observe_lean_=observe_lean;observe_lean_lifetime_=observe_lean_lifetime;clear();lean_trace_.reset();
        for(unsigned i=0;i<6;++i) {
            std::array<std::byte,32> bytes{};
            if(!read(base+rvas[i],bytes.data(),bytes.size()) || bytes!=expected[i]) throw std::runtime_error("Native interaction query code differs");
        }
        if(physical_grab_ && !ranking_profile(2))throw std::runtime_error("Native physical-grab obstruction wrapper differs");
        if(!NativeBlockEligibility::verify(base,read))throw std::runtime_error("Native Block eligibility consumer differs");
        if(!NativeSlashSceneRate::verify(base,read))throw std::runtime_error("Native Slash scene-rate consumer differs");
        if(!NativeChokeDistanceScope::verify(base,read))throw std::runtime_error("Native Choke scope consumer differs");
        if(!NativeOfferedChoke::verify(base,read))throw std::runtime_error("Native Choke offer observer differs");
        if(route_) {
            std::array<unsigned char,8> getter{};
            constexpr std::array<unsigned char,32> eligibility_code{0x48,0x8b,0xc4,0x48,0x89,0x78,0x20,0x41,0x54,0x41,0x56,0x41,0x57,0x48,0x81,0xec,0xa0,0,0,0,0x4d,0x8b,0xe1,0x4d,0x8b,0xf0,0x48,0x8b,0xfa,0x4c,0x8b,0xf9};
            constexpr std::array<unsigned char,5> origin_call{0xe8,0x48,0xf6,0x05,0};
            std::array<unsigned char,32> eligibility{};std::array<unsigned char,5> call{};
            if(!read_(base_+0x417820,getter.data(),getter.size()) || getter!=range_origin_code ||
               !read_(base_+0x3b80a0,eligibility.data(),eligibility.size()) || eligibility!=eligibility_code ||
               !read_(base_+0x3b81d3,call.data(),call.size()) || call!=origin_call)
                throw std::runtime_error("Native interaction range-origin getter differs");
            for(unsigned index=0;index<ranking_rvas.size();++index)
                if(!ranking_profile(index))throw std::runtime_error("Native interaction ranking code differs");
        }
        if(physical_grab_ && !door_ingress_.configure(base,read))throw std::runtime_error("Cannot configure native door contact ingress");
        log_.open(file,std::ios::trunc);if(!log_) throw std::runtime_error("Cannot open native interaction trace");
        targets_.open(file.parent_path()/L"native-interaction-targets.csv",std::ios::trunc);
        if(!targets_) throw std::runtime_error("Cannot open native interaction targets");
        if(observe_lean_) lean_trace_.open(file.parent_path()/L"native-lean-query-trace.csv");
        if(observe_lean_lifetime_) lean_lifetime_.install(base,read,file.parent_path()/L"native-lean-lifecycle.csv");
        ranking_log_.open(file.parent_path()/L"native-interaction-ranking.csv",std::ios::trunc);
        if(!ranking_log_) throw std::runtime_error("Cannot open native interaction ranking evidence");
        ranking_rows_=0;ranking_first_={};ranking_log_<<std::setprecision(9)<<"tick_ms,side,sensor,count,rank,body_handle,owner,hit_x,hit_y,hit_z,hand_distance,sweep_fraction,preferred_owner\n";
        targets_<<"tick_ms,thread,stage,interaction,target,generation,target_vtable_rva,owner,interacting,candidate_primary,candidate_secondary,candidate_character,side,locked,selected,is_candidate,direct_owner,spawn\n";
        log_<<std::setprecision(9);target_samples_=0;
        log_<<"tick_ms,thread,stage,sensor,camera,routed";
        for(const auto* prefix:{"native","effective"}) for(unsigned i=0;i<12;++i) log_<<','<<prefix<<i;log_<<'\n';
        for(auto& count:query_outcomes_)count=0;
        stage_calls_=rays_=routed_=rejected_=samples_=overlap_=volume_requests_=volume_rejected_=ranked_=ranking_rejected_=direct_shortcuts_filtered_=occluded_scan_continued_=0;instance_=this;
        hand_range_origins_=0;
        const std::array<void*,6> hooks{reinterpret_cast<void*>(&stage<0>),reinterpret_cast<void*>(&stage<1>),reinterpret_cast<void*>(&stage<2>),
            reinterpret_cast<void*>(&direct),reinterpret_cast<void*>(&queue<0>),reinterpret_cast<void*>(&queue<1>)};
        const std::array<void**,6> originals{reinterpret_cast<void**>(&stages_[0]),reinterpret_cast<void**>(&stages_[1]),reinterpret_cast<void**>(&stages_[2]),
            reinterpret_cast<void**>(&direct_),reinterpret_cast<void**>(&queues_[0]),reinterpret_cast<void**>(&queues_[1])};
        try {
        for(unsigned i=0;i<6;++i) {
            const auto target=reinterpret_cast<void*>(base+rvas[i]);
            if(!*originals[i] && MH_CreateHook(target,hooks[i],originals[i])!=MH_OK) throw std::runtime_error("Cannot create interaction query hook");
            if(MH_EnableHook(target)!=MH_OK) throw std::runtime_error("Cannot enable interaction query hook");
            enabled_[i]=true;
        }
        if(route_) {
            const auto range_at=reinterpret_cast<void*>(base_+0x417820);
            if(!range_origin_ && MH_CreateHook(range_at,reinterpret_cast<void*>(&range_origin_hook),reinterpret_cast<void**>(&range_origin_))!=MH_OK)
                throw std::runtime_error("Cannot create native interaction range-origin hook");
            if(MH_EnableHook(range_at)!=MH_OK)throw std::runtime_error("Cannot enable native interaction range-origin hook");
            range_origin_enabled_=true;
            const std::array<void*,3> hooks_extra{reinterpret_cast<void*>(&fetch),reinterpret_cast<void*>(&sort),reinterpret_cast<void*>(&occlusion)};
            const std::array<void**,3> originals_extra{reinterpret_cast<void**>(&fetch_),reinterpret_cast<void**>(&sort_),reinterpret_cast<void**>(&occlusion_)};
            for(unsigned i=0;i<3;++i) {
                if(!ranking_profile(i)) throw std::runtime_error("Native interaction ranking code differs");
                const auto at=reinterpret_cast<void*>(base_+ranking_rvas[i]);
                if(!*originals_extra[i] && MH_CreateHook(at,hooks_extra[i],originals_extra[i])!=MH_OK) throw std::runtime_error("Cannot create native interaction ranking hook");
                if(MH_EnableHook(at)!=MH_OK) throw std::runtime_error("Cannot enable native interaction ranking hook");ranking_enabled_[i]=true;
            }
        }
        if(physical_grab_) {
            physical_occlusion_=occlusion_?occlusion_:reinterpret_cast<Occlusion>(base_+ranking_rvas[2]);
        }
        if(!NativeBlockEligibility::process_owner().install(base,read))throw std::runtime_error("Cannot install native Block eligibility consumer");
        if(!NativeSlashSceneRate::process_owner().install(base,read,logic))throw std::runtime_error("Cannot install native Slash scene-rate consumer");
        if(!NativeChokeDistanceScope::process_owner().install(base,read,logic))throw std::runtime_error("Cannot install native Choke scope consumer");
        if(!NativeOfferedChoke::process_owner().install(base,read,logic))throw std::runtime_error("Cannot install native Choke offer observer");
        active_=true;
        NativeBlockEligibility::process_owner().enabled(true);
        NativeSlashSceneRate::process_owner().enabled(true);
        NativeChokeDistanceScope::process_owner().enabled(takedown_ranges_enabled_.load(std::memory_order_acquire));
        } catch(...) {
            disable();
            throw;
        }
    }
    bool disable() noexcept {
        stopping_=true;
        NativeBlockEligibility::process_owner().enabled(false);
        NativeSlashSceneRate::process_owner().enabled(false);
        NativeChokeDistanceScope::process_owner().enabled(false);
        anatomical_shadow_=false;anatomical_raw_.stop(anatomical_contact_);
        full_hand_enabled_=false;full_hand_contact_.stop();
        clear_door_contact();
        door_ingress_.stop();
        active_=false;publish_sword_pose_epoch(0);publish_sword_hand(false,0,0);bool restored=NativeBlockEligibility::process_owner().disable();
        if(!NativeSlashSceneRate::process_owner().disable())restored=false;
        if(!NativeChokeDistanceScope::process_owner().disable())restored=false;
        if(!NativeOfferedChoke::process_owner().disable())restored=false;
        if(range_origin_enabled_) {
            if(MH_DisableHook(reinterpret_cast<void*>(base_+0x417820))==MH_OK)range_origin_enabled_=false;else restored=false;
        }
        for(unsigned i=1;i<6;++i) if(enabled_[i]) {
            if(MH_DisableHook(reinterpret_cast<void*>(base_+rvas[i]))==MH_OK) enabled_[i]=false;else restored=false;
        }
        for(unsigned i=0;i<3;++i) if(ranking_enabled_[i]) {
            if(MH_DisableHook(reinterpret_cast<void*>(base_+ranking_rvas[i]))==MH_OK) ranking_enabled_[i]=false;else restored=false;
        }
        const auto deadline=GetTickCount64()+5000;
        if(enabled_[0]) {
            if(MH_DisableHook(reinterpret_cast<void*>(base_+rvas[0]))==MH_OK) enabled_[0]=false;else restored=false;
        }
        while((depth_ || !anatomical_contact_.quiescent() || !full_hand_contact_.quiescent()) && GetTickCount64()<deadline) Sleep(1);
        if(depth_ || !anatomical_contact_.quiescent() || !full_hand_contact_.quiescent()) return false;
        if(door_installed_) {
            if(!NativeDoorInteraction::process_owner().disable())restored=false;
            else door_installed_=false;
        }
        if(!restore_takedown_ranges()) restored=false;
        if(observe_lean_lifetime_ && !lean_lifetime_.disable()) restored=false;
        for(unsigned i=0;i<6;++i) if(i<3?stages_[i]!=nullptr:i==3?direct_!=nullptr:queues_[i-4]!=nullptr) {
            std::array<std::byte,32> bytes{};
            restored=restored && read_(base_+rvas[i],bytes.data(),bytes.size()) && bytes==expected[i];
        }
        for(unsigned i=0;i<3;++i) if(i==0?fetch_!=nullptr:i==1?sort_!=nullptr:occlusion_!=nullptr) restored=restored && ranking_profile(i);
        if(range_origin_) {std::array<unsigned char,8> getter{};restored=restored && read_(base_+0x417820,getter.data(),getter.size()) && getter==range_origin_code;}
        try {std::lock_guard<std::mutex> lock(mutex_);log_.close();targets_.close();ranking_log_.close();hands_={};published_={false,false};if(observe_lean_ && !lean_trace_.close()) restored=false;} catch(...) {return false;}
        if(restored) {
            try {
                std::lock_guard<std::mutex> lock(trace_mutex_);
                console_lines_.clear();sword_contacts_.clear();impact_fx_.clear();trace_distance_=0;
                arm_show_=0;held_world_=0;
                nonlethal_grip_reserved_=false;
            } catch(...) {return false;}
            stopping_=false;
        }
        return restored;
    }
    // The choke private view's scale (build/takedown-range-scale): distance and height only ever shrink (takedown_range_policy.hpp).
    void takedown_range_scale(float scale) noexcept {NativeChokeDistanceScope::process_owner().factor(choke_view_factor(scale));}
    void takedown_ranges(bool enabled) noexcept {
        takedown_ranges_enabled_=enabled;
        NativeChokeDistanceScope::process_owner().enabled(enabled && active_.load(std::memory_order_acquire) && !stopping_.load(std::memory_order_acquire));
    }
    AnatomicalContactOwner anatomical_owner(unsigned side) const {return anatomical_contact_.owner(side);}
    std::optional<AnatomicalWorldContact::AdmissionReceipt> anatomical_admission() const {
        if(!active_.load(std::memory_order_acquire) || stopping_.load(std::memory_order_acquire) || !anatomical_shadow_enabled())return {};
        return anatomical_contact_.admission();
    }
    bool post_anatomical_contact(const AnatomicalContactRequest& request) {
        AnatomicalWorldContact::RequestPair requests{};
        if(request.token.side>1)return false;requests[request.token.side]=request;return post_anatomical_contacts(requests);
    }
    bool post_anatomical_contacts(const std::array<std::optional<AnatomicalContactRequest>,2>& requests) {
        const auto& first=requests[0]?requests[0]:requests[1];
        if(!first || !first->token.admission_epoch)return false;
        const AnatomicalWorldContact::AdmissionReceipt receipt{{first->token.owner,first->token.owner},first->token.admission_epoch};
        return anatomical_shadow_enabled() && active_.load(std::memory_order_acquire) && !stopping_.load(std::memory_order_acquire) &&
            anatomical_contact_.post_batch_current(receipt,requests);
    }
    std::optional<AnatomicalContactResult> anatomical_result(const AnatomicalContactToken& token,std::uint64_t now) const {return anatomical_contact_.result(token,now);}
    void clear_anatomical_contact() {anatomical_raw_.clear(anatomical_contact_);}
    void full_hand_collision(bool enabled) {
        full_hand_enabled_.store(false,std::memory_order_release);
        full_hand_contact_.stop();
        if(enabled && (!active_ || stopping_ || !physical_grab_ || !physical_grab_->enabled() || !full_hand_contact_.start()))
            throw std::runtime_error("Full-hand query response requires active interaction, physical grab and drained consumers");
        full_hand_enabled_.store(enabled,std::memory_order_release);
    }
    bool full_hand_collision() const {return full_hand_enabled_.load();}
    NativeFullHandContact& full_hand_contact() {return full_hand_contact_;}
    std::optional<NativeFullHandContact::Resolved> resolve_full_hand(unsigned side,const WorldTrackedPose& grip,std::uint64_t pose_epoch,
            std::uint64_t reference_epoch,const std::shared_ptr<const rig::NativeArmBindingSnapshot>& binding,std::uint64_t now) {
        if(!full_hand_enabled_ || !active_ || stopping_ || !physical_grab_)return {};
        return full_hand_contact_.resolve_checked(side,grip,pose_epoch,reference_epoch,binding,now,[&](const NativeFullHandContact::Sample& sample) {
            if(!native_world_query_source_current(read_,sample.source) || physical_grab_->holding(side))return false;
            const auto state=rig::native_hand_state(read_,base_);
            return state && state->arms==binding->arms && !state->carrying && (state->empty_candidates&(1u<<side));
        });
    }
    bool anatomical_shadow_enabled() const {return anatomical_shadow_.load(std::memory_order_acquire) && anatomical_raw_.enabled();}
    void anatomical_shadow(bool enabled) {
        if(enabled && (!active_.load(std::memory_order_acquire) || stopping_.load(std::memory_order_acquire) || !physical_grab_ || !physical_grab_->enabled()))
            throw std::runtime_error("Anatomical shadow requires installed interaction and physical-grab owners");
        anatomical_shadow_=false;anatomical_raw_.configure(enabled,anatomical_contact_);anatomical_shadow_=enabled;
    }
    bool publish_anatomical_raw(AnatomicalRawPair::Publication publication,std::uint64_t now) {
        if(!active_.load(std::memory_order_acquire) || stopping_.load(std::memory_order_acquire))return false;
        return anatomical_raw_.publish(std::move(publication),anatomical_contact_,now);
    }
    bool wants_anatomical_raw(const AnatomicalWorldContact::AdmissionReceipt& receipt,std::uint64_t frame,std::uint64_t solved_revision,unsigned side,std::uint64_t now) const {
        return anatomical_shadow_.load(std::memory_order_acquire) && active_.load(std::memory_order_acquire) &&
            !stopping_.load(std::memory_order_acquire) && anatomical_raw_.wants_publication(receipt,frame,solved_revision,side,now);
    }
    AnatomicalRawPair::Diagnostic anatomical_shadow_diagnostic(std::uint64_t now) {
        AnatomicalRawPair::Diagnostic result{};
        if(!anatomical_shadow_.load(std::memory_order_acquire) || !active_.load(std::memory_order_acquire) || stopping_.load(std::memory_order_acquire))return result;
        const auto sample=anatomical_raw_.observation(anatomical_contact_,now);if(!sample)return result;
        AnatomicalQueryProof proof{};{std::lock_guard<std::mutex> lock(anatomical_proof_mutex_);proof=anatomical_proof_;}
        const bool consumed=anatomical_contact_.consume_batch(sample->admission,sample->tokens,now,[&](const AnatomicalWorldContact::ResultPair& pair) {
            if(!anatomical_raw_.current(*sample,anatomical_contact_,now) || !pair[0] || !pair[1])return;
            result.admission_epoch=sample->admission.epoch;result.revision=sample->revision;result.tracking_revision=sample->tracking_revision;
            result.frame=sample->frame;result.request_tick=sample->tick;result.source_tick=sample->source_tick;result.source_body=sample->source.handle;
            result.query_tick=std::min(pair[0]->query_tick,pair[1]->query_tick);
            result.empty_verified_at_query=proof.revision==sample->revision && proof.admission_epoch==sample->admission.epoch;
            if(result.empty_verified_at_query) {result.lifecycle_revision=proof.lifecycle_revision;result.picked=proof.picked;}
            for(unsigned side=0;side<2;++side) {
                result.state[side]=static_cast<unsigned>(pair[side]->state);result.casts[side]=pair[side]->casts;
                result.witnesses[side]=pair[side]->witnesses?pair[side]->witnesses->size():0;result.shift[side]=pair[side]->shift;
            }
            result.valid=true;
        });
        if(!consumed)result={};return result;
    }
    bool publish_door_intent(const NativePlayerCamera& camera,NativeDoorIngress::Publication publication) {
        if(!active_ || stopping_ || !physical_grab_ || !logic_ || logic_()!=2 || camera.authored_view()) {clear_door_contact();return false;}
        const auto game=rig::value<std::uintptr_t>(read_,base_+0x39a5290);
        const auto world=game?rig::value<std::uintptr_t>(read_,*game+NativeWorldTrace::world_offset):std::nullopt;
        const auto source=world?native_world_query_source(read_,base_,camera.camera,*world):std::nullopt;
        const auto origin=rig::value<std::array<float,3>>(read_,camera.player+0x300);
        if(!source || !origin || source->player!=camera.player || source->physics!=camera.physics ||
           source->camera_identity.generation!=camera.camera_identity.generation ||
           source->physics_identity.generation!=camera.physics_identity.generation ||
           ((source->camera_identity.flags^camera.camera_identity.flags)&NativeDoorOwner::stable_identity_flags) ||
           ((source->physics_identity.flags^camera.physics_identity.flags)&NativeDoorOwner::stable_identity_flags)) {clear_door_contact();return false;}
        publication.source=*source;publication.player_origin={(*origin)[0],(*origin)[1],(*origin)[2]};
        return door_ingress_.publish(std::move(publication));
    }
    void clear_door_contact() {
        door_ingress_.clear();
        if(door_installed_)NativeDoorInteraction::process_owner().clear();
    }
    NativeDoorIngress::Metrics door_metrics() const noexcept {return door_ingress_.metrics();}
    std::uint64_t anatomical_cast_count() const {return anatomical_cast_count_.load();}
    std::uint64_t anatomical_cast_ticks() const {return anatomical_cast_ticks_.load();}
    void slash_speed(float factor) noexcept {NativeSlashSceneRate::process_owner().factor(factor);}
    bool restore_takedown_ranges() noexcept {
        return NativeChokeDistanceScope::process_owner().stopped();
    }
    bool takedown_ranges_applied() const noexcept {return NativeChokeDistanceScope::process_owner().enabled();}
    unsigned takedown_range_kill_rules() const noexcept {return 0;}
    UINT64 occluded_scan_continued() const {return occluded_scan_continued_.load();}
    UINT64 hand_range_origins() const {return hand_range_origins_.load();}
    std::array<UINT64,static_cast<unsigned>(NativeUseQueryOutcome::count)> query_outcomes() const {
        std::array<UINT64,static_cast<unsigned>(NativeUseQueryOutcome::count)> out{};
        for(unsigned i=0;i<out.size();++i)out[i]=query_outcomes_[i].load();return out;
    }
    UINT64 ranked_results() const {return ranked_.load();}
    UINT64 ranking_rejected() const {return ranking_rejected_.load();}
    UINT64 direct_shortcuts_filtered() const {return direct_shortcuts_filtered_.load();}
    UINT64 volume_requests() const {return volume_requests_.load();}
    UINT64 volume_rejected() const {return volume_rejected_.load();}
    DWORD stage0_thread_id() const {return stage0_thread_id_.load(std::memory_order_relaxed);}
    UINT64 character_gaze_admitted() const {return character_gaze_admitted_.load();}
    UINT64 character_gaze_declined() const {return character_gaze_declined_.load();}
    UINT64 calls() const {return stage_calls_.load();}
    UINT64 rays() const {return rays_.load();}
    UINT64 routed() const {return routed_.load();}
    UINT64 rejected() const {return rejected_.load();}
    UINT64 overlap() const {return overlap_.load();}
    UINT64 lean_rows() const {return lean_trace_.rows();}
    UINT64 lean_dropped() const {return lean_trace_.dropped();}
    UINT64 lean_rejected() const {return lean_trace_.rejected();}
    UINT64 lean_queues() const {return lean_trace_.queues();}
    UINT64 lean_transactions() const {return lean_trace_.transactions();}
    UINT64 lean_skipped() const {return lean_trace_.skipped();}
    bool lean_trace_failed() const {return lean_trace_.failed();}
    UINT64 lean_lifetime_rows() const {return lean_lifetime_.rows();}
    UINT64 lean_lifetime_dropped() const {return lean_lifetime_.dropped();}
    UINT64 lean_lifetime_rejected() const {return lean_lifetime_.rejected();}
    UINT64 lean_lifetime_overflow() const {return lean_lifetime_.overflow();}
    bool lean_lifetime_failed() const {return lean_lifetime_.failed();}
};
inline thread_local NativeInteractionAim::Retrieved NativeInteractionAim::retrieved_{};
inline std::array<std::array<NativeInteractionAim::WeaponSegment,NativeInteractionAim::weapon_slots>,2> NativeInteractionAim::weapon_req_{};
inline NativeInteractionAim::SwordContactObservation NativeInteractionAim::sword_last_contact_;
#endif
} // namespace d2vr

