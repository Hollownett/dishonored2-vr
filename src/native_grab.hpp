#pragma once
#include "native_guarded_call.hpp"
#include <windows.h>
#include <MinHook.h>
#include "native_rigid_body.hpp"
#include "native_held_object.hpp"
#include "release_depen.hpp"
#include "native_hand_contact.hpp"
#include "mass_probe.hpp"
#include "hand_body_probe.hpp"
#include "hand_body_spike.hpp"
#include "full_hand_contact.hpp"
#include "native_player_camera.hpp"
#include "crash_observer.hpp"
#include "physical_grab.hpp"
#include "corpse_tow.hpp"
#include "native_motion.hpp"
#include "grab_mass.hpp"
#include "ragdoll_safety.hpp"
#include "corpse_diagnostics.hpp"
#include "right_grip_owner.hpp"
#include "selection_policy.hpp"
#include "grab_intent_buffer.hpp"
#include <atomic>
#include <mutex>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <chrono>
#include <limits>
#include <cmath>
namespace d2vr {
// The hand-contact registry scan reads entities scattered over the heap. The shared region cache behind read_self (thread_local, 32
// entries, 100 ms TTL) thrashed on them: a VirtualQuery per read, 150-340 us peaks (v503 sim). A direct-mapped table of validated
// blocks (one per 64 KB of address space, TTL 0.8-1.2 s staggered per block) answers nearly every read; SEH covers an unmap inside it.
class ScanReader {
    struct Slot {std::uintptr_t begin{},end{};ULONGLONG until{};};
    std::array<Slot,1024> slots_{};
    ULONGLONG slowest_{}; // microseconds: the slowest VirtualQuery since take_slowest()
    static bool copy(std::uintptr_t address,void* out,std::size_t size,Slot& slot) {
        __try {std::memcpy(out,reinterpret_cast<const void*>(address),size);return true;}
        __except(EXCEPTION_EXECUTE_HANDLER) {slot=Slot{};return false;}
    }
public:
    ULONGLONG take_slowest() {const auto v=slowest_;slowest_=0;return v;}
    bool operator()(std::uintptr_t address,void* out,std::size_t size) {
        constexpr std::uintptr_t highest=0x00007fffffffffffULL;
        if(!out || address<0x10000 || !size || size>0x10000 || address>highest-size) return false;
        const ULONGLONG now=GetTickCount64();auto& slot=slots_[(address>>16)&1023];
        if(!(address>=slot.begin && address+size<=slot.end && now<slot.until)) {
            MEMORY_BASIC_INFORMATION region{};
            LARGE_INTEGER q0{},q1{},qf{};QueryPerformanceCounter(&q0);
            const bool queried=VirtualQuery(reinterpret_cast<const void*>(address),&region,sizeof region)==sizeof region;
            QueryPerformanceCounter(&q1);QueryPerformanceFrequency(&qf);
            if(qf.QuadPart) slowest_=std::max<ULONGLONG>(slowest_,static_cast<ULONGLONG>((q1.QuadPart-q0.QuadPart)*1000000/qf.QuadPart));
            if(!queried || region.State!=MEM_COMMIT || (region.Protect&PAGE_GUARD)) {slot=Slot{};return false;}
            const auto protection=region.Protect&0xff;
            if(protection!=PAGE_READONLY && protection!=PAGE_READWRITE && protection!=PAGE_WRITECOPY &&
               protection!=PAGE_EXECUTE_READ && protection!=PAGE_EXECUTE_READWRITE && protection!=PAGE_EXECUTE_WRITECOPY) {slot=Slot{};return false;}
            const auto begin=reinterpret_cast<std::uintptr_t>(region.BaseAddress);
            slot={begin,begin+region.RegionSize,now+800+((address>>16)*37)%400};
            if(!(address>=slot.begin && address+size<=slot.end)) {slot=Slot{};return false;} // spans two regions: refused
        }
        return copy(address,out,size,slot);
    }
};
// The calling thread's own CPU time in microseconds, from its cycle counter (QueryThreadCycleTime: cycles it ran, not wall time while the OS
// ran something else), calibrated once against the performance counter. A hand-contact service whose wall time is far over this was
// preempted; one that is not was slow itself. 0 when the counter is unavailable.
class ThreadClock {
    double per_microsecond_{};
public:
    void calibrate() { // ~3 ms, once, off the physics thread; the highest ratio of three 1 ms spins is the one nothing interrupted
        LARGE_INTEGER frequency{};QueryPerformanceFrequency(&frequency);
        if(!frequency.QuadPart) return;
        double best=0;
        for(int trial=0;trial<3;++trial) {
            LARGE_INTEGER a{},b{};ULONG64 first=0,last=0;
            QueryPerformanceCounter(&a);
            if(!QueryThreadCycleTime(GetCurrentThread(),&first)) return;
            do QueryPerformanceCounter(&b);while((b.QuadPart-a.QuadPart)*1000<frequency.QuadPart);
            if(!QueryThreadCycleTime(GetCurrentThread(),&last)) return;
            const double microseconds=static_cast<double>(b.QuadPart-a.QuadPart)*1e6/static_cast<double>(frequency.QuadPart);
            best=std::max(best,static_cast<double>(last-first)/microseconds);
        }
        per_microsecond_=best>200 && best<10000?best:0.; // 0.2-10 GHz: anything else is not a cycle counter we understand
    }
    double micros() const {ULONG64 cycles=0;return per_microsecond_>0 && QueryThreadCycleTime(GetCurrentThread(),&cycles)?static_cast<double>(cycles)/per_microsecond_:0.;}
};
inline std::optional<NativeRigidBody> grab_focus_candidate(const std::vector<NativeRigidBody>& candidates,Vec3 hand,
        std::uintptr_t other_entity,bool native_use=false,std::uintptr_t preferred_entity=0,std::uint32_t other_handle=0) {
    if(native_use) return {};
    std::optional<NativeRigidBody> best,preferred;double distance=grab_acquire_distance;
    for(const auto& body:candidates) {
        // The other hand's body; two limbs of one guard's body are two bodies.
        if(other_entity && body.entity==other_entity && (!other_handle || body.handle==other_handle)) continue;
        const auto gap=body.contact_distance(hand);
        if(body.entity==preferred_entity && gap<=grab_retain_distance) preferred=body;
        if(gap<distance || (!best && gap==distance)) {distance=gap;best=body;}
    }
    if(preferred && (!best || preferred->contact_distance(hand)<=distance+grab_switch_margin)) return preferred;
    return best;
}
// The palm picks the prop or bone (the G2 cost, the same one that orders loot): what the palm faces and is near wins, ties to the lower id.
// `gate` is the acquire distance (0.45 m); the current pick is kept out to 0.55 m at x 0.86 (PalmSelection, the only hysteresis). The other
// hand's body (its focus or held body; entity, and handle when given) is no candidate. legacy_nearest is build/legacy-palm-select: the v611
// nearest-AABB pick from the probe's palm. An invalid probe picks nothing new (PalmSelection counts scoring_fallback).
inline std::optional<NativeRigidBody> grab_focus_candidate(const std::vector<NativeRigidBody>& candidates,const PalmProbe& probe,
        PalmSelection& selection,const GrabTuning& tuning,std::uintptr_t other_entity,std::uint32_t other_handle,double gate,
        bool legacy_nearest=false,std::uint64_t now_ms=0,PalmPick* out=nullptr,const std::vector<PalmTarget>* extra=nullptr,std::optional<std::size_t>* extra_pick=nullptr) {
    // extra (G6, Task 14): more targets in the same ONE list (a living NPC's bones); a winner among them is returned through extra_pick.
    if(extra_pick) extra_pick->reset();
    std::vector<NativeRigidBody> eligible;std::vector<PalmTarget> targets;eligible.reserve(candidates.size());targets.reserve(candidates.size());
    for(const auto& body:candidates) {
        if(other_entity && body.entity==other_entity && (!other_handle || body.handle==other_handle)) continue;
        const auto t=palm_body_target(body,tuning);
        if(!palm_target_valid(t)) continue;
        eligible.push_back(body);targets.push_back(t);
    }
    if(legacy_nearest) {
        if(!finite(probe.palm)) return {};
        std::uintptr_t preferred=0;
        for(std::size_t i=0;i<targets.size();++i) if(selection.current() && targets[i].id==selection.current()) preferred=eligible[i].entity;
        return grab_focus_candidate(eligible,probe.palm,0,false,preferred,0);
    }
    const std::size_t bodies=targets.size();
    if(extra) for(const auto& t:*extra) targets.push_back(t);
    const auto pick=selection.update(probe,targets,tuning.hover_radius_m,now_ms,gate,std::max(gate,grab_retain_distance));
    if(out) *out=pick;
    if(!pick.index) return {};
    if(*pick.index>=bodies) {if(extra_pick) *extra_pick=*pick.index-bodies;return {};}
    return eligible[*pick.index];
}
// Bounded nearby cache. Registry discovery never grants a native Use action;
// each consumer revalidates the full dynamic-body lifetime before using it.
class NativeGrabDiscovery {
    unsigned cursor_{1};std::uint64_t tick_{},slices_{},lifetime_rejections_{};
    std::vector<NativeRigidBody> bodies_;
public:
    static constexpr unsigned registry_size=0x3fff,slice_size=1024,capacity=64; // full registry in ~16 pose ticks (~0.25 s): a book reached for right after walking up is already known
    struct Diagnostics {unsigned cursor{},cached{};std::uint64_t tick{},slices{},lifetime_rejections{};};
    Diagnostics diagnostics() const {return {cursor_,static_cast<unsigned>(bodies_.size()),tick_,slices_,lifetime_rejections_};}
    unsigned cursor() const {return cursor_;}
    void reset() {cursor_=1;tick_=0;bodies_.clear();}
    // hints (G3, plan Task 8): registry indices near this hand (its stage-0 Use hit owners, its probe hits), looked up every tick before the
    // slice, so the object just reached is cached the same tick (Bonelab: "the scan runs every physics step"). At most 8; 0 is skipped.
    template<class Lookup,class Validate> const std::vector<NativeRigidBody>& advance(
            Vec3 hand,std::uint64_t tick,Lookup lookup,Validate validate,const std::array<unsigned,8>* hints=nullptr) {
        if(!finite(hand) || !tick) {reset();return bodies_;}
        if(tick==tick_)return bodies_;
        tick_=tick;++slices_;
        for(auto it=bodies_.begin();it!=bodies_.end();) {
            const auto body=validate(*it);
            // A guard's body re-picks the limb nearest the hand, so its handle may change.
            if(!body || body->identity!=it->identity || body->entity_id!=it->entity_id ||
               body->component!=it->component || (it->bone<0 && body->handle!=it->handle) || body->world!=it->world ||
               body->model!=it->model || body->contact_distance(hand)>grab_retain_distance+3) {++lifetime_rejections_;it=bodies_.erase(it);}
            else {*it=*body;++it;}
        }
        const auto consider=[&](unsigned index) {
            const auto body=lookup(index);
            if(!body || body->contact_distance(hand)>grab_retain_distance+3)return;
            const auto found=std::find_if(bodies_.begin(),bodies_.end(),[&](const auto& b){return b.entity==body->entity;});
            if(found!=bodies_.end()) {*found=*body;return;}
            if(bodies_.size()<capacity)bodies_.push_back(*body);
            else {
                const auto farthest=std::max_element(bodies_.begin(),bodies_.end(),[&](const auto& a,const auto& b){return a.contact_distance(hand)<b.contact_distance(hand);});
                if(body->contact_distance(hand)<farthest->contact_distance(hand))*farthest=*body;
            }
        };
        if(hints) for(const auto index:*hints) if(index && index<registry_size) consider(index);
        for(unsigned scanned=0;scanned<slice_size;++scanned) {
            const auto index=cursor_;if(++cursor_==registry_size)cursor_=1;
            consider(index);
        }
        return bodies_;
    }
};
// Corpse hold options (Bonelab feel Task 10). enabled=false is build/no-corpse-grab: corpse bodies are no candidates. from_file says the
// tuning came from build/grab-tuning.cfg (status "tuning":"file"), so the shipped config is the tested config or visibly not.
struct CorpseOptions {bool enabled=true;GrabTuning tuning{};bool from_file=false;};
// The grasp point of the held body as of the last dispatcher tick (a prop or a corpse), for the drawn hand: a mutex-protected copy.
struct HoldAnchorView {bool active{};Vec3 anchor{};std::uint64_t tick{};bool capped{},corpse{}; // capped: a motor-held corpse or prop (a living guard's yank hold is active but not capped); corpse: a corpse limb or collar
    std::uintptr_t entity{};int bone{-1},layer{-1};std::uint64_t serial{};bool living{};};   // living (G6, Task 14): the hand seats on a living NPC's bone. B16: what is held (a corpse's limb bone, -1 for a prop), its collision layer (pool record +0x6c & 0x1f; -1 unreadable) and this hold's serial
class NativeGrab {
    friend struct NativeGrabCheck;
public:
    enum class Gate : unsigned {none,occupied,deferred,inactive,pose_unusable,invalid_grip,not_gameplay,unfocused,
        native_use,epoch_mismatch,source_missing,source_future,source_stale,owner_invalid,authored_view,
        awaiting_release,release_armed,native_no_candidate,native_locked,physical_reserved,physical_held,pending_release,obstruction_pending,obstruction_blocked,
        buffer_pending};   // buffer_pending (G3, Task 5): a squeeze with nothing in reach waits up to grab_buffer_ms for a candidate
    struct Decision {
        unsigned side{};UINT64 tick{},epoch{},serial{},publication_tick{},source_tick{},publication_age_ms{},source_age_ms{};
        float raw_grip{};Gate gate=Gate::none;RightGripOwner::Role role=RightGripOwner::Role::release;
        bool usable{},occupied{},deferred{},released_armed{},publication_age_valid{},source_age_valid{};
        unsigned candidates{};std::uintptr_t player{},camera{},focus{},held{};int camera_generation{};
        std::uint32_t focus_id{},held_id{};
    };
    struct ObstructionRequest {
        unsigned side{};UINT64 token{},epoch{},tick{},source_tick{};NativePlayerCamera owner{};
        WorldTrackedPose hand{};NativeRigidBody body{};
        bool hover{};   // G3 (Task 8): asked at hover (grip under 0.35) so the press can use the verdict; never decides or binds by itself
    };
private:
    struct Obstruction {ObstructionRequest request{};std::optional<bool> clear;UINT64 verdict_tick{};};
    using Read=bool (*)(std::uintptr_t,void*,std::size_t);
    using Logic=UINT (*)();
    using Dispatch=void (__fastcall*)(void*,void*);
    using Velocity=void (__fastcall*)(void*,UINT,const float*,const float*,int);
    // palm: the G2 palm probe (raw controller grip + palm fit, C9/C10), the same one that orders the hand's native Use hits (Task 3).
    struct Publication {NativePlayerCamera owner{};WorldTrackedPose hand{};float grip{};UINT64 epoch{},tick{},serial{};bool usable{};bool occupied{};UINT64 source_tick{};PalmProbe palm{};};
    // One independent lane per hand. Either hand may hold one dynamic body.
    // The caller marks native carrying/action occupancy. Equipped weapons do
    // not prevent either tracked hand from acquiring a loose physics object.
    struct Lane {
        Publication published{};std::optional<Obstruction> obstruction;PhysicalGrab grab{};std::optional<NativeRigidBody> held;RightGripOwner owner{};
        std::vector<NativeRigidBody> candidates;NativeGrabDiscovery discovery;
        std::optional<NativeRigidBody> focus;UINT64 focus_tick{};Vec3 focus_hand{};std::uintptr_t focus_excluded{};
        // G2/G3 (Task 5): the palm's pick (the one 0.86 hysteresis), its score, and the grab buffer of this press (buffer_used: started once per press;
        // catch_tick: the catch edge, for press-to-held, ruling H4).
        PalmSelection selection;std::optional<FocusScore> score;double palm_dot{};GrabIntentBuffer buffer;bool buffer_used{};UINT64 catch_tick{};
        std::uint32_t reject_id{};BodyReject reject{};
        std::array<unsigned,8> hints{};UINT64 hint_tick{},hover_request_tick{};   // G3 (Task 8): discovery hints; the last hover LOS request
        // G6 (Task 14): living NPCs seen near this hand (entity, id, last seen; at most 8 within 3 m), the bone the palm picked among theirs, the press's
        // candidate, the hold (no `held` body: nothing is ever written to him), the choke/takedown reservation and the events for the game thread.
        struct LivingSeen {std::uintptr_t entity{};std::uint32_t id{};UINT64 seen{};};std::vector<LivingSeen> living_npcs;
        std::optional<LivingBone> living_focus,living_candidate,living_started,hard_pull;std::optional<LivingHold> living;UINT64 living_tick{};
        std::optional<std::pair<std::uintptr_t,std::array<float,3>>> pull_request;std::uint32_t reserved_npc{};UINT64 reserved_tick{};   // G3 thin props (Task 7): the last prop near this hand whose body or bounds were refused, and why
        bool pending_release{},release_throw{},attempted{},acquired{},decision_deferred{};
        double tow{},tow_recovery{}; // a dragged body's pelvis follows its grabbed limb at this distance (set on the first motor write); tow_recovery: plan_tow's stale-frame recovery time
        void reset_tow() {tow=0;tow_recovery=0;last_tow=0;}
        // Capped hold (Task 10). corpse: a dead or unconscious guard's ragdoll body driven by PhysicalGrab::corpse_motor; capped: a corpse or a prop (the
        // force-capped pipeline); neither: a legacy-yanked living guard on the old motor. mass: kg the cap last used; last_capped: CorpseMotor.capped of the
        // last write (it feeds the tow's limb_saturated); bind_pelvis_z: where the pelvis was at the bind (the lift that hangs mass on the held limb).
        bool corpse{},capped{},last_capped{},near_noted{},mass_noted{};
        double mass{},bind_pelvis_z{std::numeric_limits<double>::quiet_NaN()};std::uint32_t bind_motion{};std::optional<std::uint32_t> bind_owner;UINT64 bound_at{};
        HoldAnchorView anchor_view{};
        // B16 (render thread -> dispatcher, copied from coupling_pub_ each pass): the hand body's lag behind the palm while this capped hold is
        // coupled to the body, and the coupling's release (a corpse: 0.5 m of lag for 0.25 s; a prop: 1.5 m). Used only while fresh and only for the
        // hold it names (serial: the render thread saw this very hold; a coupling meant for an earlier hold never reaches a new one).
        struct Coupling {Vec3 offset{};bool release{};UINT64 tick{};bool active{};std::uint64_t serial{};} coupling{};
        WorldTrackedPose sample_hand{};   // the hand the grab sampled this pass (the controller, or the coupled body pose)
        std::uint64_t hold_serial{};bool was_coupled{};Vec3 last_offset{};   // B16: this hold's serial; the coupling edge rebases the grab
        void end_hold() {
            held.reset();corpse=capped=last_capped=near_noted=mass_noted=false;mass=0;bind_pelvis_z=std::numeric_limits<double>::quiet_NaN();
            bind_motion=0;bind_owner.reset();anchor_view={};reset_tow();
        }
        Gate gate=Gate::none;float raw_grip{};UINT64 decision_tick{};bool decision_usable{};
        UINT64 consumed{},last_motor{},last_tow{},last_acquisition{},owner_epoch{},grip_epoch{};
        UINT64 focus_calls{},focus_us{},focus_max_us{};
        std::optional<HandProxies> contact_proxies;
        UINT64 contact_epoch{},contact_tick{};
        std::uintptr_t owner_player{},owner_camera{},grip_player{},grip_camera{};int owner_generation{},grip_generation{};
    };
    inline static NativeGrab* instance_{};
    inline static constexpr std::uintptr_t rva=0x89fb0,velocity_rva=0xc85dc0;
    inline static constexpr std::array<unsigned char,32> dispatch_code{0x48,0x83,0xec,0x68,0x48,0xc7,0x44,0x24,0x30,0xfe,0xff,0xff,0xff,0x4c,0x8b,0xd2,0x0f,0xb7,0x42,0x04,0x83,0xf8,0x3e,0x0f,0x87,0xf3,0x05,0x00,0x00,0x48,0x8d,0x15};
    inline static constexpr std::array<unsigned char,32> velocity_code{0x48,0x89,0x6c,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x89,0x54,0x24,0x10,0x57,0x48,0x83,0xec,0x40,0x44,0x8b,0xd2,0x49,0x8b,0xe9,0x49,0x8b,0xf0,0x41,0x81,0xe2,0xff};
    Dispatch original_{};Velocity velocity_{};Read read_{};Logic logic_{};std::uintptr_t base_{};
    bool enabled_{};std::atomic<bool> active_{};std::atomic<unsigned> depth_{};
    UINT64 obstruction_token_{};bool obstruction_required_{};
    UINT64 acquisition_samples_{};std::ofstream acquisitions_;
    // F1 mass probe (read-only): build/mass-probe, read once at install, appends to artifacts/mass-probe.csv at a bind, at most 200 rows per session.
    // Collar (chest/neck) holds stay off by default: the mass probe has not yet confirmed bones 2/3 are chest/neck. Enabled only while
    // build/collar-grab exists; remove this knob once the probe verifies the bone map. The yank path below passes allow_collar=false.
    bool collar_grab_{};
    static constexpr float collar_fist_squeeze=.35f; // one threshold for offering the collar (discovery) and binding it: a full fist reserves the lane here
    // Corpse hold (Task 10). All of this is touched under mutex_ (the dispatcher callback, the render-thread accessors) except the atomics.
    // corpse_enabled_: build/no-corpse-grab clears it; lockout_ is internally locked (any thread); the dispatcher cost counters are dispatcher-thread only; no_posts_ is
    // build/no-ragdoll-posts, read at install for the wake path (Task 11).
    bool corpse_enabled_=true,no_posts_{},prop_grasp_point_=true;
    bool legacy_palm_select_{};   // build/legacy-palm-select (read at install): the nearest-AABB pick and no grab buffer (plan Task 5)
    EntityLockout lockout_{};CorpseDiagnostics corpse_diag_{};CorpseTraceWriter* corpse_trace_{};
    UINT64 pass_cost_us_{};std::atomic<UINT64> corpse_cost_us_{},corpse_cost_max_us_{},velocity_unknown_{};
    // hold_anchor() must never wait for a dispatcher pass: the pass publishes a copy of each lane's anchor under this small mutex (held only for the copy).
    UINT64 last_pass_ms_{};
    std::mutex snapshot_mutex_;std::array<HoldAnchorView,2> snapshot_{};
    void publish_snapshot_locked() {std::lock_guard<std::mutex> lock(snapshot_mutex_);for(unsigned side=0;side<2;++side) snapshot_[side]=lanes_[side].anchor_view;}
    // Hand-contact pushes (apply_contact) respect the runaway lockout and the session latch: a guard's body is left alone while either applies.
    bool contact_write_blocked(std::uintptr_t entity,bool guard) {
        if(!guard) return false;
        if(FaultLatch::tripped()) return true;
        const auto id=rig::value<std::uint32_t>(read_,entity+0xa8);
        if(!id) return true;
        std::lock_guard<std::mutex> lock(mutex_);return lockout_.locked(*id,seconds_from_ms(last_pass_ms_)); // the clock of the last dispatcher pass, the one the lock was set on
    }
    void trip_fault_latch() noexcept {FaultLatch::trip();corpse_diag_.set_fault_latch(true);}
    bool mass_probe_{};unsigned mass_probe_rows_{};std::ofstream mass_probe_out_;
    static constexpr unsigned mass_probe_limit=200;
    void probe_masses(const NativeRigidBody& body,UINT64 now) {
        if(!mass_probe_ || !mass_probe_out_ || mass_probe_rows_>=mass_probe_limit) return;
        try {
            std::vector<MassProbeRow> rows;
            if(body.bone>=0) rows=probe_ragdoll_masses(read_,base_,body.entity,now);
            else if(const auto row=probe_prop_mass(read_,body,now)) rows.push_back(*row);
            for(const auto& row:rows) {if(mass_probe_rows_>=mass_probe_limit) break;mass_probe_out_<<mass_probe_csv_row(row)<<'\n';++mass_probe_rows_;}
            mass_probe_out_.flush();
        } catch(...) {mass_probe_=false;}
    }
    std::mutex mutex_;std::array<Lane,2> lanes_{};
    std::atomic<std::uintptr_t> last_published_camera_{};
public:
    std::uintptr_t last_published_camera() const {return last_published_camera_.load(std::memory_order_relaxed);}
private:
    UINT64 lifecycle_revision_{1};
    bool contact_required_{};
    std::optional<Vec3> acquisition_contact(const Lane& lane,const NativeRigidBody& body,UINT64 now) const {
        if(!contact_required_)return body.contact(lane.published.hand.position);
        if(!lane.contact_proxies || !body.bounds || lane.contact_epoch!=lane.published.epoch || !lane.contact_tick ||
           now<lane.contact_tick || now-lane.contact_tick>32)return {};
        return full_hand_contact_grab(*lane.contact_proxies,*body.bounds);
    }
    void advance_lifecycle_locked() noexcept {
        if(lifecycle_revision_!=std::numeric_limits<UINT64>::max())++lifecycle_revision_;
    }
    void note_role_change_locked(RightGripOwner::Role before,const Lane& lane) noexcept {
        if(before!=lane.owner.role())advance_lifecycle_locked();
    }
    void revoke_owner_locked(Lane& lane) noexcept {
        const auto before=lane.owner.role();lane.owner.revoke();note_role_change_locked(before,lane);
    }
    static bool same_publisher_owner(const NativePlayerCamera& first,const NativePlayerCamera& second) noexcept {
        return first.player==second.player && first.camera==second.camera && first.body==second.body &&
            first.controller==second.controller && first.physics==second.physics && first.heading_delegate==second.heading_delegate &&
            first.camera_identity==second.camera_identity && first.physics_identity==second.physics_identity;
    }
    std::array<Decision,256> decisions_{};unsigned decision_begin_{},decision_count_{};
    std::array<std::optional<Decision>,2> last_decision_{};std::atomic<UINT64> decision_dropped_{};
    void record_decision(unsigned side,UINT64 now) {
        const auto& lane=lanes_[side];const auto& p=lane.published;const auto source=p.source_tick?p.source_tick:p.tick;
        const Decision record{side,now,p.epoch,p.serial,p.tick,source,now>=p.tick?now-p.tick:0,now>=source?now-source:0,
            lane.raw_grip,lane.gate,lane.owner.role(),lane.decision_usable,p.occupied,lane.decision_deferred,
            lane.owner.role()==RightGripOwner::Role::ready,p.tick!=0 && now>=p.tick,source!=0 && now>=source,
            static_cast<unsigned>(lane.candidates.size()),p.owner.player,p.owner.camera,lane.focus?lane.focus->entity:0,lane.held?lane.held->entity:0,
            p.owner.camera_identity.generation,lane.focus?lane.focus->entity_id:0,lane.held?lane.held->entity_id:0};
        const auto band=[](float raw) {return !std::isfinite(raw) || raw<0 || raw>1?3u:raw<.35f?0u:raw<=.65f?1u:2u;};
        const auto& last=last_decision_[side];
        if(last && now>=last->tick && now-last->tick<100 && band(record.raw_grip)==band(last->raw_grip) &&
           record.gate==last->gate && record.role==last->role && record.usable==last->usable && record.occupied==last->occupied &&
           record.deferred==last->deferred && record.epoch==last->epoch && record.player==last->player && record.camera==last->camera &&
           record.camera_generation==last->camera_generation && record.focus==last->focus && record.held==last->held) return;
        if(decision_count_==decisions_.size()) {decision_begin_=(decision_begin_+1)%decisions_.size();--decision_count_;++decision_dropped_;}
        decisions_[(decision_begin_+decision_count_)%decisions_.size()]=record;++decision_count_;last_decision_[side]=record;
    }
    std::atomic<UINT64> native_presses_{},physical_presses_{};
    std::ofstream log_;std::filesystem::path loss_file_;std::atomic<UINT64> picked_{},moved_{},dropped_{},lost_{},queries_{},samples_{},callbacks_{};
    void note_reject(unsigned side,std::uint32_t id,BodyReject why) {auto& lane=lanes_[side];lane.reject_id=id;lane.reject=why;}
    // A press's distance to its candidate: from the palm point (the G2 cost's distance) or, with no palm score, from the hand.
    static std::optional<double> focus_gap(const Lane& lane,const NativeRigidBody& body) {
        if(lane.score && lane.score->id==palm_body_id(body)) return lane.score->distance;
        const auto d=body.contact_distance(lane.published.hand.position);
        return std::isfinite(d)?std::optional<double>{d}:std::nullopt;
    }
    std::optional<NativeRigidBody> focus_body(unsigned side,UINT64 now,bool refresh) {
        auto& lane=lanes_[side];const auto& p=lane.published;const auto& other_lane=lanes_[1-side];
        // The other hand's held body, else its fresh focus, is not offered to this hand (no two hands on one body's same handle, no ping-pong).
        const auto& other=other_lane.held?other_lane.held:
            (other_lane.focus && other_lane.focus_tick && now>=other_lane.focus_tick && now-other_lane.focus_tick<=250?other_lane.focus:std::optional<NativeRigidBody>{});
        struct Cost {
            Lane& lane;std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();
            ~Cost() {const auto us=static_cast<UINT64>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count());
                ++lane.focus_calls;lane.focus_us+=us;lane.focus_max_us=std::max(lane.focus_max_us,us);}
        } cost{lane};
        const auto excluded=other?other->entity:0;const auto excluded_handle=other?other->handle:0;
        // Collar (chest, neck) targets are offered only to a full fist (the published squeeze is zero for a grip alone, which stays the game's shoulder carry).
        const bool collar=collar_grab_ && lane.raw_grip>=collar_fist_squeeze;
        const auto game=rig::value<std::uintptr_t>(read_,base_+0x39a5290);
        if(!game || !rig::typed(read_,base_,*game,0x1c940a8)) {lane.discovery.reset();lane.focus.reset();return {};}
        // G3 (Task 7): a corpse offers every bone with a known radius, picked by the palm; without a palm probe (or build/legacy-palm-select) the v611 limbs.
        const bool palm_bones=!legacy_palm_select_ && p.palm.valid;
        const auto corpse=[&](std::uintptr_t entity)->std::optional<NativeRigidBody> {
            if(!corpse_enabled_) return {};
            return palm_bones?corpse_candidate(read_,base_,entity,p.palm,tuning_,collar):corpse_candidate(read_,base_,entity,p.hand.position,collar);
        };
        const bool hinted=lane.hint_tick && now>=lane.hint_tick && now-lane.hint_tick<=250;
        const auto& candidates=lane.discovery.advance(p.hand.position,p.tick,[&](unsigned index)->std::optional<NativeRigidBody> {
            const auto entity=rig::value<std::uintptr_t>(read_,*game+0x3130b0+index*8);
            const bool guard=entity && rig::typed(read_,base_,*entity,0x1c5e108); // a corpse or knocked-out guard: drag by a limb
            if(!entity || !(guard || rig::typed(read_,base_,*entity,0x1c886a8) || rig::typed(read_,base_,*entity,0x1c87d58) || rig::typed(read_,base_,*entity,0x1c5de18)))return {}; // + arkMovable (bottles, glasses: thrown by hand)
            const auto id=rig::value<std::uint32_t>(read_,*entity+0xa8);
            const auto pos=rig::value<std::array<float,3>>(read_,*entity+0x300);
            if(!id || (*id>>16)!=index || !pos)return {};
            const Vec3 point{(*pos)[0],(*pos)[1],(*pos)[2]};
            if(!finite(point) || length(point-p.hand.position)>grab_retain_distance+3)return {};
            if(guard) {
                if(living_enabled_ && palm_bones && !ragdoll_pushable_state(read_,*entity) && length(point-p.hand.position)<=living_seen_m) note_living(lane,*entity,*id,now);
                return corpse(*entity);
            }
            BodyReject why{};auto body=native_rigid_body(read_,base_,*entity,false,&why);
            if(why!=BodyReject::ok && why!=BodyReject::not_prop) note_reject(side,*id,why);   // the thin-prop line of the grab_focus block
            return body;
        },[&](const NativeRigidBody& body)->std::optional<NativeRigidBody> {
            if(body.bone<0) return native_rigid_body(read_,base_,body.entity);
            return corpse(body.entity);
        },hinted?&lane.hints:nullptr);
        if(!refresh && lane.focus_tick && now>=lane.focus_tick && now-lane.focus_tick<100 &&
           length(p.hand.position-lane.focus_hand)<.05 && lane.focus_excluded==excluded) {
            if(!lane.focus) return {};
            const auto current=native_grab_body(read_,base_,*lane.focus);
            if(current && current->identity==lane.focus->identity && current->component==lane.focus->component &&
               current->handle==lane.focus->handle && current->model==lane.focus->model && current->world==lane.focus->world && current->entity_id==lane.focus->entity_id &&
               current->contact_distance(p.hand.position)<=grab_retain_distance && acquisition_contact(lane,*current,now)) return current;
        }
        const auto preferred=lane.focus && (!refresh || lane.focus->contact_distance(p.hand.position)<=grab_acquire_distance)?lane.focus->entity:0;
        std::vector<NativeRigidBody> touching;
        if(contact_required_)for(const auto& candidate:candidates)if(acquisition_contact(lane,candidate,now))touching.push_back(candidate);
        const auto& list=contact_required_?touching:candidates;
        PalmPick pick{};lane.score.reset();lane.palm_dot=0;
        lane.living_focus.reset();
        if(!legacy_palm_select_ && p.palm.valid) {
            std::vector<PalmTarget> living_targets;std::vector<LivingBone> living_bones;std::optional<std::size_t> living_pick;
            if(living_enabled_) living_candidates(side,now,living_targets,living_bones);
            lane.focus=grab_focus_candidate(list,p.palm,lane.selection,tuning_,excluded,excluded_handle,grab_acquire_distance,false,now,&pick,
                living_targets.empty()?nullptr:&living_targets,&living_pick);
            if(living_pick) {
                const auto& target=living_targets[*living_pick];lane.living_focus=living_bones[*living_pick];
                if(const auto s=palm_cost(p.palm,target,tuning_.hover_radius_m,grab_retain_distance)) {
                    lane.score=FocusScore{target.id,target.kind,s->cost,std::isfinite(pick.runner_up)?pick.runner_up:0.,s->distance,lane.living_focus->entity};
                    const auto to=s->point-palm_point(p.palm);lane.palm_dot=length(to)>1e-9?dot(p.palm.direction,to*(1/length(to))):1.;
                }
            }
            if(lane.focus) {
                const auto target=palm_body_target(*lane.focus,tuning_);
                if(const auto s=palm_cost(p.palm,target,tuning_.hover_radius_m,grab_retain_distance)) {
                    lane.score=FocusScore{target.id,target.kind,s->cost,std::isfinite(pick.runner_up)?pick.runner_up:0.,s->distance,lane.focus->entity};
                    const auto to=s->point-palm_point(p.palm);lane.palm_dot=length(to)>1e-9?dot(p.palm.direction,to*(1/length(to))):1.;
                }
            }
        } else {   // build/legacy-palm-select, or no valid palm probe this frame (counted as scoring_fallback): the v611 nearest-AABB pick
            if(!legacy_palm_select_) (void)lane.selection.update(p.palm,{},tuning_.hover_radius_m,now);
            lane.focus=grab_focus_candidate(list,p.hand.position,excluded,false,preferred,excluded_handle);
            if(lane.focus) lane.score=FocusScore{palm_body_id(*lane.focus),lane.focus->bone>=0?PalmKind::bone:PalmKind::prop,0.,0.,lane.focus->contact_distance(p.hand.position),lane.focus->entity};
        }
        lane.focus_tick=now;lane.focus_hand=p.hand.position;lane.focus_excluded=excluded;
        return lane.focus;
    }
    // ---- G6 (plan Task 14): living NPCs ----------------------------------------------------------------------------------------------------
    static constexpr double living_seen_m=3.;static constexpr UINT64 living_seen_ms=500,reserve_ms=250;
    bool living_enabled_=true,living_pull_enabled_{};   // build/no-living-hold clears living_enabled_ (install); the pull follows NativeWeaponHands::living_pull_enabled
    std::atomic<UINT64> living_holds_{},living_hard_pulls_{},living_releases_{};
    void note_living(Lane& lane,std::uintptr_t entity,std::uint32_t id,UINT64 now) {
        for(auto& n:lane.living_npcs) if(n.entity==entity) {n.id=id;n.seen=now;return;}
        if(lane.living_npcs.size()<8) {lane.living_npcs.push_back({entity,id,now});return;}
        auto oldest=std::min_element(lane.living_npcs.begin(),lane.living_npcs.end(),[](const auto& a,const auto& b){return a.seen<b.seen;});
        *oldest={entity,id,now};
    }
    // A choke or takedown offered for this NPC (either hand, fresh within 250 ms) keeps priority: he is no hold candidate and a hold on him ends.
    bool npc_reserved(std::uint32_t id,UINT64 now) const {
        for(const auto& lane:lanes_) if(id && lane.reserved_npc==id && lane.reserved_tick && now>=lane.reserved_tick && now-lane.reserved_tick<=reserve_ms) return true;
        return false;
    }
    // One target per nearby living NPC: of his collar, upper arms and forearms, the bone the palm prefers (palm cost, 0.45 m gate; the grip-alone
    // 0.12 m gate is the decision's). Priority living (1.0). The other hand's held NPC and a reserved one are skipped.
    void living_candidates(unsigned side,UINT64 now,std::vector<PalmTarget>& targets,std::vector<LivingBone>& bones) {
        auto& lane=lanes_[side];const auto& p=lane.published;const auto& other=lanes_[1-side];
        lane.living_npcs.erase(std::remove_if(lane.living_npcs.begin(),lane.living_npcs.end(),[&](const auto& n){return now<n.seen || now-n.seen>living_seen_ms;}),lane.living_npcs.end());
        for(const auto& n:lane.living_npcs) {
            if(npc_reserved(n.id,now) || (other.living && other.living->grasp().entity==n.entity)) continue;
            std::optional<PalmTarget> best;std::optional<LivingBone> best_bone;double best_cost=std::numeric_limits<double>::infinity();
            for(int bone=0;bone<17;++bone) {
                if(!living_hold_bone(bone)) continue;
                const auto b=living_bone(read_,base_,n.entity,bone);
                if(!b || b->entity_id!=n.id) continue;
                PalmTarget t;t.id=b->entity_id;t.kind=PalmKind::living;t.priority=tuning_.priority(PalmKind::living);
                t.minimum=b->position-Vec3{b->radius,b->radius,b->radius};t.maximum=b->position+Vec3{b->radius,b->radius,b->radius};
                const auto c=palm_cost(p.palm,t,tuning_.hover_radius_m,grab_acquire_distance);
                if(c && c->cost<best_cost) {best_cost=c->cost;best=t;best_bone=b;}
            }
            if(best) {targets.push_back(*best);bones.push_back(*best_bone);}
        }
    }
    // One tick of a living hold (dispatcher, mutex_ held). No velocity, no `held` body: the hold only reads his bone and publishes the anchor.
    void living_tick(Lane& lane,UINT64 now,bool usable) {
        const auto grasp=lane.living->grasp();const auto& p=lane.published;
        const auto bone=living_bone(read_,base_,grasp.entity,grasp.bone);
        LivingTick t;t.controller=p.hand.position;t.bone=bone?bone->position:grasp.position;t.hand_velocity=p.palm.velocity;t.grip=p.grip;
        t.dt=lane.living_tick && now>lane.living_tick?static_cast<double>(now-lane.living_tick)/1000.:0.;t.epoch=p.epoch;
        t.alive=bone && bone->entity_id==grasp.entity_id && usable && !npc_reserved(grasp.entity_id,now);
        lane.living_tick=now;
        const auto step=lane.living->tick(t);
        if(step.hard_pull) {lane.hard_pull=grasp;++living_hard_pulls_;}
        if(step.pull_step && living_pull_enabled_ && !step.release)
            if(const auto at=rig::value<std::array<float,3>>(read_,grasp.entity+0x300))
                lane.pull_request=std::make_pair(grasp.entity,std::array<float,3>{static_cast<float>((*at)[0]+step.pull_step->x),static_cast<float>((*at)[1]+step.pull_step->y),(*at)[2]});
        if(step.release) {lane.living.reset();lane.anchor_view={};if(!step.hard_pull) ++living_releases_;return;}
        lane.anchor_view=HoldAnchorView{true,step.anchor,now,false,false,grasp.entity,grasp.bone,-1,0,true};
    }
    static bool same_body(const NativeRigidBody& a,const NativeRigidBody& b) {
        // Generation only: a held prop's component flag word can change once it moves (headset v447: the ashtray was lost 30 ms
        // after every grab, eligible, same handle and world); ragdoll bodies already compare the generation alone.
        return a.entity==b.entity && a.entity_id==b.entity_id && a.component==b.component && a.identity.generation==b.identity.generation &&
            a.handle==b.handle && a.model==b.model && a.world==b.world && a.record==b.record;
    }
    // The collar bones (chest 2, neck 3) are a physical hold only on a full fist: the published squeeze is nonzero only for one (dxgi_probe publishes zero
    // for a grip alone, which stays the game's own shoulder carry), and only while build/collar-grab is on. Limbs are unaffected.
    bool hold_bone_allowed(int bone,float squeeze) const {return (bone!=2 && bone!=3) || (collar_grab_ && squeeze>=collar_fist_squeeze);}
    static bool request_current(const ObstructionRequest& r,const Publication& p,UINT64 now) {
        return p.usable && !p.occupied && p.epoch==r.epoch && p.tick==r.tick && p.source_tick==r.source_tick &&
            length(p.hand.position-r.hand.position)==0 &&
            length(p.hand.basis[0]-r.hand.basis[0])==0 && length(p.hand.basis[1]-r.hand.basis[1])==0 && length(p.hand.basis[2]-r.hand.basis[2])==0 &&
            p.owner.player==r.owner.player && p.owner.camera==r.owner.camera && p.owner.camera_identity==r.owner.camera_identity &&
            now>=r.tick && now-r.tick<=grab_pose_freshness_ms &&
            (!r.source_tick || (now>=r.source_tick && now-r.source_tick<=grab_pose_freshness_ms));
    }
    // G3 (Task 8): a hover request stays askable across publications while the same hand owner and epoch hold it, the hand is within 2 cm of
    // where it asked and the request is fresh.
    static constexpr UINT64 hover_verdict_ms=100,hover_request_every_ms=100;static constexpr double hover_hand_m=.02,hover_contact_m=.01;
    static bool hover_request_live(const ObstructionRequest& r,const Publication& p,UINT64 now) {
        return r.hover && p.usable && !p.occupied && p.epoch==r.epoch && p.owner.player==r.owner.player && p.owner.camera==r.owner.camera &&
            p.owner.camera_identity==r.owner.camera_identity && now>=r.tick && now-r.tick<=grab_pose_freshness_ms && length(p.hand.position-r.hand.position)<=hover_hand_m;
    }
    // A hover verdict still answers the press: <= 100 ms old, the hand moved <= 2 cm since, the same body, its contact point moved <= 1 cm.
    static bool hover_verdict_current(const Obstruction& o,const Publication& p,const NativeRigidBody& body,UINT64 now) {
        const auto& r=o.request;
        return r.hover && o.verdict_tick && now>=o.verdict_tick && now-o.verdict_tick<=hover_verdict_ms && p.usable && !p.occupied && p.epoch==r.epoch &&
            p.owner.player==r.owner.player && p.owner.camera==r.owner.camera && p.owner.camera_identity==r.owner.camera_identity &&
            length(p.hand.position-r.hand.position)<=hover_hand_m && same_body(r.body,body) &&
            length(body.contact(r.hand.position)-r.body.contact(r.hand.position))<=hover_contact_m;
    }
    bool cleared(Lane& lane,const NativeRigidBody& body,UINT64 now) {
        if(!obstruction_required_) return true;
        if(!lane.obstruction || !lane.obstruction->clear.value_or(false)) return false;
        if(lane.obstruction->request.hover) return hover_verdict_current(*lane.obstruction,lane.published,body,now);
        return request_current(lane.obstruction->request,lane.published,now) && same_body(lane.obstruction->request.body,body) &&
            length(lane.obstruction->request.body.contact(lane.published.hand.position)-body.contact(lane.published.hand.position))<.001;
    }
    void request_obstruction(unsigned side,const NativeRigidBody& body,bool hover=false) {
        auto& lane=lanes_[side];const auto& p=lane.published;
        if(!hover && lane.obstruction && !lane.obstruction->request.hover && request_current(lane.obstruction->request,p,p.tick) && same_body(lane.obstruction->request.body,body)) return;
        lane.obstruction=Obstruction{{side,++obstruction_token_,p.epoch,p.tick,p.source_tick,p.owner,p.hand,body,hover},{},0};
    }
    // The hover cue's candidate gets its LOS asked now (at most once per hand per 100 ms, never over a press request), so the press binds at once.
    void request_hover_obstruction(unsigned side,const NativeRigidBody& body,UINT64 now) {
        auto& lane=lanes_[side];const auto& p=lane.published;
        if(!obstruction_required_ || p.grip>=.35f || lane.raw_grip>=.35f || lane.held || lane.decision_deferred) return;
        if(lane.obstruction && !lane.obstruction->request.hover) return;
        if(lane.hover_request_tick && now>=lane.hover_request_tick && now-lane.hover_request_tick<hover_request_every_ms) return;
        request_obstruction(side,body,true);lane.hover_request_tick=now;++hover_requests_;
    }
    std::atomic<UINT64> hover_requests_{},hover_used_{},hover_verdicts_{};
    enum class VelocityWrite {written,rejected,faulted};
    // The native velocity setter validates nothing (a stale world or handle can fault, a NaN reaches the simulation), and /EHsc
    // does not turn an access violation into a C++ exception, so the call runs under an SEH guard and non-finite input never
    // reaches it. rejected is a fault-free refusal; faulted means the engine objects are gone.
    VelocityWrite write_velocity(void* world,UINT handle,float* linear,float* angular) {
        for(unsigned i=0;i<3;++i) if(!std::isfinite(linear[i]) || !std::isfinite(angular[i])) return VelocityWrite::rejected;
        auto call=[&]{velocity_(world,handle,linear,angular,0);};
        return guarded_call(call)?VelocityWrite::written:VelocityWrite::faulted;
    }
    // A velocity fault drops every held body and owner, the same recovery as a fault anywhere in update(). Caller holds mutex_.
    void release_all_locked() {
        for(unsigned side=0;side<2;++side) {
            auto& lane=lanes_[side];
            if(lane.held && lane.corpse) corpse_diag_.note_release(side,ReleaseReason::fault);
            lane.grab.release(false,ReleaseReason::fault);lane.end_hold();lane.pending_release=false;revoke_owner_locked(lane);lane.candidates.clear();
        }
        ++lost_;
    }
    void release_all() {std::lock_guard<std::mutex> lock(mutex_);release_all_locked();}
    // Returns false when the setter faulted and every lane was released: the caller must not touch lane state further.
    bool write(Lane& lane,unsigned side,const NativeRigidBody& body,const GrabMotor& motor) {
        alignas(16) std::array<float,4> linear{static_cast<float>(motor.linear.x),static_cast<float>(motor.linear.y),static_cast<float>(motor.linear.z),0};
        alignas(16) std::array<float,4> angular{static_cast<float>(motor.angular.x),static_cast<float>(motor.angular.y),static_cast<float>(motor.angular.z),0};
        CrashOperation operation("native physical grab velocity");
        const auto result=write_velocity(reinterpret_cast<void*>(body.world),body.handle,linear.data(),angular.data());
        if(result==VelocityWrite::faulted) {if(body.bone>=0) trip_fault_latch();++contact_.faults;release_all_locked();return false;}
        if(result==VelocityWrite::rejected) return true;
        if(motor.release) ++dropped_;else ++moved_;
        if(samples_<4000) {
            log_<<GetTickCount64()<<','<<side<<','<<body.entity_id<<','<<body.component<<','<<body.identity.generation<<','<<body.handle
                <<','<<body.flags<<','<<motor.release<<','<<body.pose.position.x<<','<<body.pose.position.y<<','<<body.pose.position.z;
            const auto target=lane.grab.target();log_<<','<<target.position.x<<','<<target.position.y<<','<<target.position.z;
            for(unsigned i=0;i<3;++i) log_<<','<<linear[i];for(unsigned i=0;i<3;++i) log_<<','<<angular[i];log_<<'\n';++samples_;
            if(samples_%60==0 || motor.release) log_.flush();
        }
        return true;
    }
    // A body dragged by a limb follows at the limb's reach: when the hand pulls the grabbed limb farther from the pelvis than at
    // the grab, the pelvis slides after it along the floor (sim v402: the wrist alone moved 0.86 m and dragged the pelvis 0.19 m).
    // limb_saturated: the limb's own capped motor could not reach its velocity this tick (CorpseMotor.capped); then plan_tow stands down.
    // `pelvis` is the caller's fresh read of body 0 when it has one (the same tick), else it is read here.
    struct TowResult {bool written{},saturated{};};
    TowResult tow(Lane& lane,const NativeRigidBody& held,double dt,std::optional<NativeRigidBody> pelvis,bool limb_saturated) {
        TowResult result;
        if(!tuning_.tow) return result;
        if(held.bone>=0 && FaultLatch::tripped()) return result;
        if(!pelvis) pelvis=native_ragdoll_body(read_,base_,held.entity,0);
        if(!pelvis) return result;
        TowInput in;in.target=lane.grab.target().position;in.pelvis=pelvis->pose.position;in.bind_reach=lane.tow;in.recovery_s=lane.tow_recovery;
        in.pelvis_kg=ragdoll_class_kg[0];in.dt=dt;in.tuning=&tuning_;
        in.limb_saturated=limb_saturated;
        const auto motion=native_motion(read_,*pelvis); // the plan writes an absolute velocity, so it needs the current one for the cap
        if(!motion) {++tow_unreadable_;return result;}
        in.pelvis_velocity=motion->velocity;
        const auto plan=plan_tow(in);lane.tow=plan.bind_reach;lane.tow_recovery=plan.recovery_s;result.saturated=plan.disabled_saturated;if(!plan.write) return result;
        // the plan holds the pelvis on the floor plane; its vertical speed is left as read, so a body lifted or swung by one hand is not pinned by the tow
        alignas(16) std::array<float,4> linear{static_cast<float>(plan.linear.x),static_cast<float>(plan.linear.y),static_cast<float>(motion->velocity.z),0};
        alignas(16) std::array<float,4> angular{};
        CrashOperation operation("native ragdoll tow velocity");
        const auto outcome=write_velocity(reinterpret_cast<void*>(pelvis->world),pelvis->handle,linear.data(),angular.data());
        if(outcome==VelocityWrite::faulted) {trip_fault_latch();++contact_.faults;release_all_locked();return result;}
        if(outcome==VelocityWrite::written) {++tows_;result.written=true;}
        return result;
    }
    // ---- capped hold (Task 10): corpse and prop lanes -------------------------------------------------------------------------------------
    // hknpMotion read of a body (shared layout with native_motion): unreadable = the memory is not there (the velocity is then unknown, counted),
    // garbage = it read but is non-finite or above 100 m/s (the hold is dropped without a write). Angular velocity sits at +0x50; a bad spin reads as rest.
    enum class MotionStatus {unreadable,ok,garbage};
    struct MotionState {MotionStatus status{MotionStatus::unreadable};Vec3 velocity{},spin{};std::optional<std::uint16_t> inverse;std::uint32_t index{};std::optional<std::uint32_t> owner;};
    MotionState read_motion_state(const NativeRigidBody& body) const {
        using namespace rig;MotionState out;
        const auto index=value<std::uint32_t>(read_,body.record+0x40);
        const auto array=value<std::uintptr_t>(read_,body.world+0x120);
        if(!index || *index>=0x100000 || !array || !pointer(*array)) return out;
        out.index=*index;
        const auto at=*array+static_cast<std::uintptr_t>(*index)*0x80;
        const auto v=value<std::array<float,3>>(read_,at+0x40);
        if(!v) return out;
        out.inverse=value<std::uint16_t>(read_,at+0x26);
        out.owner=value<std::uint32_t>(read_,at+0x28); // the entry's first attached body id: format unverified live, so only a CHANGE from the bind is used
        const Vec3 velocity{(*v)[0],(*v)[1],(*v)[2]};
        if(!finite(velocity) || length(velocity)>100) {out.status=MotionStatus::garbage;return out;}
        out.status=MotionStatus::ok;out.velocity=velocity;
        if(const auto w=value<std::array<float,3>>(read_,at+0x50)) {
            const Vec3 spin{(*w)[0],(*w)[1],(*w)[2]};
            if(finite(spin) && length(spin)<=100) out.spin=spin;
        }
        return out;
    }
    // The velocity of all 17 bodies of the held guard, for RunawayDetector (a body that does not resolve is skipped; one that resolves but reads
    // as garbage counts as non-finite). The held bone reuses the read the caller already made.
    std::array<std::optional<Vec3>,17> ragdoll_velocities(const NativeRigidBody& held,const MotionState& held_motion) const {
        using namespace rig;std::array<std::optional<Vec3>,17> out{};
        const auto handles=value<std::uintptr_t>(read_,held.component+0x100);
        if(!handles || !pointer(*handles)) return out;
        const auto data=value<std::uintptr_t>(read_,*handles+0x28);
        if(!data || !pointer(*data) || value<std::uint32_t>(read_,*handles+0x30)!=17u) return out;
        const auto pool=value<std::uintptr_t>(read_,held.world+0x28);const auto count=value<std::uint32_t>(read_,held.world+0x30);
        if(!pool || !pointer(*pool) || !count || *count>0x100000) return out;
        for(int bone=0;bone<17;++bone) {
            MotionState state;
            if(bone==held.bone) state=held_motion;
            else {
                const auto handle=value<std::uint32_t>(read_,*data+static_cast<std::uintptr_t>(bone)*4);
                if(!handle || !*handle) continue;
                const auto index=*handle&0xffffffu;
                if(index==0xffffffu || index>=*count) continue;
                const auto record=*pool+static_cast<std::uintptr_t>(index)*0xb0;
                const auto flags=value<std::uint32_t>(read_,record+0x44);
                if(!flags || (*flags&7)!=2 || value<std::uint32_t>(read_,record+0x70)!=handle || value<std::uint32_t>(read_,record+0x74)!=handle) continue;
                NativeRigidBody probe;probe.world=held.world;probe.record=record;
                state=read_motion_state(probe);
            }
            out[static_cast<std::size_t>(bone)]=runaway_sample(state.status!=MotionStatus::unreadable,
                state.status==MotionStatus::ok?std::optional<NativeMotion>(NativeMotion{state.velocity,0}):std::nullopt);
        }
        return out;
    }
    // Horizontal distance from the player entity (registry[0], +0x300) under 0.4 m and |dz| under 1.0 m: a body dragged into the player's feet.
    bool body_near_player(const NativeRigidBody& body) const {
        const auto game=rig::value<std::uintptr_t>(read_,base_+0x39a5290);
        if(!game) return false;
        const auto player=rig::value<std::uintptr_t>(read_,*game+0x3130b0);
        if(!player || !rig::pointer(*player)) return false;
        const auto at=rig::value<std::array<float,3>>(read_,*player+0x300);
        if(!at) return false;
        const Vec3 feet{(*at)[0],(*at)[1],(*at)[2]};
        if(!finite(feet)) return false;
        return std::hypot(body.pose.position.x-feet.x,body.pose.position.y-feet.y)<.4 && std::abs(body.pose.position.z-feet.z)<1.;
    }
    // The anchor view the render thread reads (B16: plus what is held and its layer, for the hand body's filter word).
    // Review P3-2: knob off (no build/hand-body) adds no work here: the extra fields and the layer read run only with coupling_on_.
    HoldAnchorView anchor_view_of(const Lane& lane,const NativeRigidBody& body,UINT64 now) const {
        HoldAnchorView v{true,lane.grab.anchor(body.pose),now,lane.capped,lane.corpse};
        if(coupling_on_) {
            v.entity=body.entity;v.bone=body.bone;v.serial=lane.hold_serial;
            if(body.record) if(const auto word=rig::value<std::uint32_t>(read_,body.record+0x6c)) v.layer=static_cast<int>(*word&0x1fu);
        }
        return v;
    }
    static constexpr UINT64 coupling_fresh_ms=50;   // B16: a coupling older than this is ignored (the render thread publishes every frame)
    static constexpr double coupling_step_m=.25;    // B16: a coupled offset that jumps more than this in one pass (a body snap) rebases the grab
    // current: fresh, and published for this very hold (its serial: the render thread read this hold's anchor view; never the previous hold's)
    static bool coupling_current(const Lane& lane,UINT64 now) {
        return lane.coupling.serial && lane.coupling.serial==lane.hold_serial && lane.coupling.tick && now>=lane.coupling.tick && now-lane.coupling.tick<=coupling_fresh_ms;
    }
    // The hold begins (every bind site, mutex_ held): classify it, remember where the pelvis was and which motion slot the body uses.
    void begin_hold(Lane& lane,unsigned side,const NativeRigidBody& body,UINT64 now) {
        lane.corpse=body.bone>=0 && corpse_enabled_ && ragdoll_pushable_state(read_,body.entity);
        lane.capped=lane.corpse || body.bone<0;
        lane.last_capped=false;lane.near_noted=lane.mass_noted=false;lane.mass=0;lane.bind_pelvis_z=std::numeric_limits<double>::quiet_NaN();
        lane.bound_at=now;if(lane.capped) lane.last_motor=0; // no write yet this hold: the first one measures its dt from the bind
        if(coupling_on_) {lane.hold_serial=++hold_serials_;lane.was_coupled=false;lane.last_offset={};}   // B16: the render thread couples the new hold afresh
        {const auto motion=read_motion_state(body);lane.bind_motion=motion.index;lane.bind_owner=motion.owner;}
        if(lane.corpse && body.bone>0) if(const auto pelvis=native_ragdoll_body(read_,base_,body.entity,0)) lane.bind_pelvis_z=pelvis->pose.position.z;
        lane.anchor_view=anchor_view_of(lane,body,now);
        if(lane.corpse) corpse_diag_.note_bind(side,body.bone,ragdoll_body_mass(body.bone,std::nullopt,tuning_.use_measured_mass).source);
    }
    // A capped hold ends without a velocity write (a corpse never gets one; a prop only its release). The grab keeps the release reason, the
    // regrab cooldown and the anchor to blend out; the diagnostics count a corpse release exactly once, here.
    void end_capped_hold(Lane& lane,unsigned side,ReleaseReason reason,bool forced=false) {
        lane.grab.release(false,reason,forced);
        if(lane.corpse) corpse_diag_.note_release(side,reason);
        lane.end_hold();lane.pending_release=false;
    }
    // One motor tick of a corpse or prop lane; the lifecycle re-read of the held body has already passed this tick. Order for a corpse: fault latch or
    // lockout, then the state byte (+0xd1 lost 0x28), then an open fist, then (at most every 10 ms) the 17-body runaway check, the capped motor, the write
    // and the tow. A prop skips the ragdoll-only steps and keeps its throw. Returns false only when a fault released every lane: the caller stops.
    // A runaway: this hold ends as `runaway` (counted once), the entity is locked, and a second hand on the same body lets go as a lifecycle release.
    void runaway_release(Lane& lane,unsigned side,const NativeRigidBody& body,double now_s) {
        lockout_.lock(body.entity_id,now_s);end_capped_hold(lane,side,ReleaseReason::runaway);
        auto& sibling=lanes_[1-side];
        if(sibling.held && sibling.corpse && sibling.held->entity==body.entity) end_capped_hold(sibling,1-side,ReleaseReason::lifecycle);
    }
    bool capped_tick(Lane& lane,unsigned side,const NativeRigidBody& body,UINT64 now,bool motor_usable,bool release_usable) {
        struct Cost {UINT64& total;std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();
            ~Cost() {total+=static_cast<UINT64>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count());}} cost{pass_cost_us_};
        const auto& p=lane.published;const bool corpse=lane.corpse;const double now_s=seconds_from_ms(now);
        // A tripped session latch ends every capped hold before any write, props included (a fault means the engine objects are suspect).
        if(FaultLatch::tripped()) {corpse_diag_.set_fault_latch(true);end_capped_hold(lane,side,ReleaseReason::fault);return true;}
        if(corpse) {
            if(lockout_.locked(body.entity_id,now_s)) {end_capped_hold(lane,side,ReleaseReason::runaway);return true;}
            const auto state=rig::value<unsigned char>(read_,body.entity+0xd1);
            if(!state || !(*state&0x28)) {end_capped_hold(lane,side,ReleaseReason::state_changed);return true;} // wins over an open fist on the same tick
            if(lane.pending_release) { // an open fist or a lost owner: a runaway body still counts and locks the entity first
                const auto held_motion=read_motion_state(body);
                if(RunawayDetector::check(ragdoll_velocities(body,held_motion)).runaway) {runaway_release(lane,side,body,now_s);return true;}
                end_capped_hold(lane,side,lane.release_throw?ReleaseReason::open_fist:ReleaseReason::lifecycle);return true;
            }
        } else if(lane.pending_release) { // a prop: the release throws with the hand's velocity, as before
            const auto motor=lane.grab.release_sample(coupling_on_?lane.sample_hand:p.hand,p.grip,release_usable,p.epoch,p.tick,lane.release_throw);
            if(!write(lane,side,body,motor)) return false;
            lane.last_motor=now;
            if(length(motor.linear)>3) {std::lock_guard<std::mutex> g(thrown_mutex_);thrown_=Thrown{body.entity,body.component,body.entity_id,motor.linear,now};}
            lane.end_hold();lane.pending_release=false;return true;
        }
        if(lane.last_motor && now-lane.last_motor<10) return true;
        const UINT64 since=lane.last_motor?lane.last_motor:lane.bound_at; // elapsed since the last write, or since the bind
        const double elapsed=now>since?static_cast<double>(now-since)/1000.:0.;
        const double dt=elapsed>0?elapsed:1./60; // two ticks inside one clock quantum: assume one nominal tick
        const auto motion=read_motion_state(body);
        if(motion.status==MotionStatus::garbage && !corpse) {end_capped_hold(lane,side,ReleaseReason::fault);return true;}
        // The body's slot in the motion array and the body that entry belongs to are fixed for its life; a different slot or owner is a recycled body.
        // A corpse hold with no baseline (the motion read failed at the bind) is refused: nothing is written on a path that cannot be validated.
        if(corpse && (!lane.bind_motion || !lane.bind_owner)) {end_capped_hold(lane,side,ReleaseReason::lifecycle);return true;}
        if(motion.index && ((lane.bind_motion && motion.index!=lane.bind_motion) || (lane.bind_owner && motion.owner!=lane.bind_owner))) {
            end_capped_hold(lane,side,ReleaseReason::lifecycle);return true;
        }
        double peak=0;
        if(corpse) {
            if(motion.status==MotionStatus::garbage) {lockout_.lock(body.entity_id,now_s);end_capped_hold(lane,side,ReleaseReason::fault);return true;} // untrusted memory: a fault, not a runaway
            const auto verdict=RunawayDetector::check(ragdoll_velocities(body,motion));
            if(verdict.runaway) {runaway_release(lane,side,body,now_s);return true;} // never corrected with another write
            peak=verdict.max_speed;
        }
        const std::optional<std::uint16_t> half=motion.status==MotionStatus::ok?motion.inverse:std::nullopt;
        BodyMass mass;std::optional<NativeRigidBody> pelvis;double hanging=0,radius=.15;
        if(corpse) {
            mass=ragdoll_body_mass(body.bone,half,tuning_.use_measured_mass);
            if(mass.rejected && !lane.mass_noted) {lane.mass_noted=true;corpse_diag_.note_mass_rejected();}
            if(body.bone>0) {
                pelvis=native_ragdoll_body(read_,base_,body.entity,0);
                if(pelvis) hanging=hanging_mass(body.bone,pelvis->pose.position.z-lane.bind_pelvis_z,tuning_);
            }
            radius=ragdoll_bone_radius[static_cast<std::size_t>(body.bone)];
        } else {
            mass=body.bounds?prop_body_mass(half,*body.bounds,tuning_.use_measured_mass):BodyMass{prop_class_kg,MassSource::cls,false};
            if(body.bounds) {const auto extent=body.bounds->maximum-body.bounds->minimum;radius=std::max({extent.x,extent.y,extent.z})*.5;}
        }
        const double kg=mass.kg+hanging;lane.mass=kg;
        CorpseMotorInput in;in.body_velocity=motion.velocity;in.body_spin=motion.spin;in.velocity_known=motion.status==MotionStatus::ok;in.corpse=corpse;
        in.object_kg=kg;in.inertia_kgm2=bone_inertia(kg,radius);in.dt=dt;in.tuning=corpse?&tuning_:&prop_tuning_;
        const auto& other=lanes_[1-side].held;in.share=other && other->entity==body.entity?.5:1.; // two hands on one body: each takes half the mass
        if(!in.velocity_known) ++velocity_unknown_;
        const auto result=lane.grab.corpse_motor(body.pose,motor_usable,now,p.source_tick,in);
        if(!result) {end_capped_hold(lane,side,ReleaseReason::lifecycle);return true;}
        if(result->motor.release) {
            if(corpse) { // the grab already ended the hold; a corpse release writes no velocity
                corpse_diag_.note_release(side,result->reason);lane.end_hold();lane.pending_release=false;return true;
            }
            if(!write(lane,side,body,result->motor)) return false;
            lane.last_motor=now;
            if(length(result->motor.linear)>3) {std::lock_guard<std::mutex> g(thrown_mutex_);thrown_=Thrown{body.entity,body.component,body.entity_id,result->motor.linear,now};}
            lane.end_hold();lane.pending_release=false;return true;
        }
        if(!result->write) return true; // no usable dt this tick: the hold continues
        if(!write(lane,side,body,result->motor)) return false;
        lane.last_motor=now;lane.last_capped=result->capped;
        TowResult towed;
        if(corpse && body.bone>0) {
            const double tow_dt=lane.last_tow && now>lane.last_tow?static_cast<double>(now-lane.last_tow)/1000.:1./60; // first tow of a hold uses the fallback
            lane.last_tow=now;towed=tow(lane,body,tow_dt,pelvis,result->capped);
            if(!lane.held) return false; // the tow write faulted: every lane was released
        }
        if(corpse) {
            const CorpseMotorSample sample{now,side,body.bone,result->lag_m,length(motion.velocity),peak,kg,result->cap_fraction,result->implied_force_n,
                result->capped,towed.written,towed.saturated,mass.source};
            corpse_diag_.note_motor(sample);
            if(corpse_trace_) corpse_trace_->push(sample);
            if(!lane.near_noted && body_near_player(body)) {lane.near_noted=true;corpse_diag_.note_near_player(side);}
        }
        return true;
    }
    void update_lane(unsigned side,std::uintptr_t world,UINT64 now) {
        auto& lane=lanes_[side];last_pass_ms_=now;
        if(lane.consumed==lane.published.serial && (!lane.held || now-lane.last_motor<10)) return;
        const auto& p=lane.published;lane.consumed=p.serial;
        // A pending native query must not disarm the observed-release physics
        // latch before the final per-press owner can be chosen.
        if(lane.decision_deferred && !lane.held) {lane.grab.keep_fresh(p.hand,p.epoch,p.tick);return;}
        const auto owner=native_player_camera(read_,base_,p.owner.camera);
        const bool usable=active_ && p.usable && owner && owner->player==p.owner.player && owner->camera_identity==p.owner.camera_identity &&
            logic_()==2 && !owner->authored_view() && now>=p.tick && now-p.tick<=grab_pose_freshness_ms && p.epoch &&
            (!p.source_tick || (now>=p.source_tick && now-p.source_tick<=grab_pose_freshness_ms));
        if(yanked_entity_[side].load() && now<yanked_until_[side].load() && !lane.held && p.grip>=.35f && !lane.owner.physical()) {
            const auto role_before=lane.owner.role();lane.owner.claim_physical();note_role_change_locked(role_before,lane);lane.grab.arm(); // the yank/wake target takes the squeeze already held
        }
        const bool physical_usable=usable && !p.occupied && lane.owner.physical();
        if(lane.living) {living_tick(lane,now,physical_usable);return;}   // G6: a living hold writes nothing; NativeGrab::write is never reached from here
        if(lane.living_candidate && physical_usable && p.grip>=.35f && !lane.held) {   // G6: a grip-alone press on a living NPC's collar or arm
            const auto c=*lane.living_candidate;lane.living_candidate.reset();
            const auto bone=living_bone(read_,base_,c.entity,c.bone);
            if(bone && bone->entity_id==c.entity_id && !npc_reserved(c.entity_id,now) && length(bone->position-p.hand.position)<=grab_acquire_distance+bone->radius) {
                lane.living.emplace(*bone,p.hand.position,p.epoch,living_pull_enabled_);lane.living_tick=now;lane.living_started=*bone;lane.acquired=true;++living_holds_;
                lane.anchor_view=HoldAnchorView{true,bone->position,now,false,false,bone->entity,bone->bone,-1,0,true};
            }
            return;
        }
        if(lane.held && !physical_usable && !lane.pending_release) {lane.pending_release=true;lane.release_throw=false;revoke_owner_locked(lane);}
        const bool same_owner=lane.owner_player==p.owner.player && lane.owner_camera==p.owner.camera &&
            lane.owner_generation==p.owner.camera_identity.generation && lane.owner_epoch==p.epoch;
        // B16: a capped hold the render thread couples to the hand body follows the body (Bonelab §1.2: the held body hangs on the physical hand):
        // the grab samples the controller moved by the body's lag behind the palm (the controller-relative grip offset applied at the body).
        // Never while not holding: selection and the bind read the raw controller (C9). Not coupled or stale: the controller, as before.
        // Review P3-2: knob off, none of this runs (the grab samples p.hand exactly as before).
        bool coupled=false,coupled_release=false;
        if(coupling_on_) {
            const bool current=lane.held && lane.capped && coupling_current(lane,now);
            coupled=current && lane.coupling.active && finite(lane.coupling.offset);
            coupled_release=current && lane.coupling.release;   // the raw-gap lag release (fix round 2), whatever B15 draws
            lane.sample_hand=p.hand;if(coupled) lane.sample_hand.position=p.hand.position+lane.coupling.offset;
            // DeepSeek P2: a coupling edge (the body became ready or was lost, the render stalled) or a body snap moves the sampled hand by a known
            // step; the grab continues from it instead of reading it as motion (its 0.5 m discontinuity rule would drop the hold, the step thrown)
            if(lane.held && lane.capped && (coupled!=lane.was_coupled || (coupled && length(lane.coupling.offset-lane.last_offset)>coupling_step_m))) {lane.grab.rebase(lane.sample_hand);++coupling_rebases_;}
            lane.was_coupled=coupled;lane.last_offset=coupled?lane.coupling.offset:Vec3{};
        }
        const bool sample=lane.held && lane.pending_release?false:lane.grab.sample(coupling_on_?lane.sample_hand:p.hand,p.grip,
            usable && (physical_usable || (!lane.held && p.grip<.35f)) && (!lane.held || same_owner),p.epoch,p.tick);
        if(!lane.grab.pressed()) {lane.attempted=lane.acquired=false;if(!lane.owner.physical() && !lane.held) lane.candidates.clear();}
        if(lane.held) {
            const auto body=native_grab_body(read_,base_,*lane.held);
            const auto h=*lane.held;
            if(!body || !same_body(*body,h) || body->world!=world) {
                std::ofstream loss(loss_file_,std::ios::app);
                loss<<"tick="<<now<<" side="<<side<<" entity="<<h.entity<<" component="<<h.component<<" eligible="<<body.has_value()<<" world="<<world<<" original_world="<<h.world<<'\n';
                if(lane.corpse) corpse_diag_.note_release(side,ReleaseReason::lifecycle); // world mismatch, a different body, or the hand's owner lost: never a write
                lane.grab.release(false);lane.end_hold();lane.pending_release=false;revoke_owner_locked(lane);
                lane.obstruction.reset();lane.focus.reset();lane.focus_tick=0;lane.candidates.clear();lane.discovery.reset();
                if(h.bone<0) ++lost_; // a ragdoll limb goes when the guard gets up (or his body settles): a release, not a loss
                return;
            }
            lane.held=*body;
            lane.anchor_view=anchor_view_of(lane,*body,now);
            if(!lane.capped && body->bone>=0 && corpse_enabled_ && ragdoll_pushable_state(read_,body->entity)) { // a held living guard died: the corpse envelope takes over
                if(lockout_.locked(body->entity_id,seconds_from_ms(now))) { // ... unless he is locked out: no hold, no write
                    lane.grab.release(false,ReleaseReason::lifecycle);lane.end_hold();lane.pending_release=false;return;
                }
                begin_hold(lane,side,*body,now);
            }
            if(lane.capped && coupled_release && !lane.pending_release) {   // B16: the hand body lagged too far behind the controller (HoldCoupling)
                ++coupled_releases_;lane.coupling.release=false;   // consumed
                if(lane.corpse) {end_capped_hold(lane,side,ReleaseReason::lag,true);return;}   // plan A's corpse rule: no velocity, the regrab cooldown
                lane.pending_release=true;lane.release_throw=true;                             // a prop: thrown with the hand's velocity (the 1.5 m rule)
            }
            if(lane.capped) {
                capped_tick(lane,side,*body,now,physical_usable && sample && same_owner,usable && same_owner);
                return;
            }
            // A legacy-yanked living guard keeps the old uncapped motor; his ragdoll writes still obey the session fault latch.
            if(body->bone>=0 && FaultLatch::tripped()) {
                corpse_diag_.set_fault_latch(true);lane.grab.release(false,ReleaseReason::fault);lane.end_hold();lane.pending_release=false;return;
            }
            const auto motor=lane.pending_release?std::optional<GrabMotor>{lane.grab.release_sample(p.hand,p.grip,usable && same_owner,p.epoch,p.tick,lane.release_throw)}:
                lane.grab.motor(body->pose,physical_usable && sample && same_owner,now,p.source_tick);
            if(motor && (motor->release || now-lane.last_motor>=10)) {
                if(!write(lane,side,*body,*motor)) return;
                lane.last_motor=now;
                if(body->bone>0 && !motor->release) {
                    const double dt=lane.last_tow && now>lane.last_tow?(now-lane.last_tow)/1000.:1./60; // first tow of a hold uses the fallback
                    lane.last_tow=now;(void)tow(lane,*body,dt,std::nullopt,false);
                }
                if(motor->release) {
                    if(body->bone<0 && length(motor->linear)>3) {std::lock_guard<std::mutex> g(thrown_mutex_);thrown_=Thrown{body->entity,body->component,body->entity_id,motor->linear,now};}
                    lane.end_hold();lane.pending_release=false;}
            }
        } else if(const auto yanked=yanked_entity_[side].load();yanked && now<yanked_until_[side].load() && sample && physical_usable && lane.grab.pressed()) {
            // The guard this hand just yanked into ragdoll (NativeWeaponHands::yank): the hand had his collar or arm, so it takes
            // his nearest limb and pulls it into the fist (bound as if the limb's contact point were at the hand).
            if(const auto limb=!legacy_palm_select_ && p.palm.valid?nearest_ragdoll_body(read_,base_,yanked,p.palm,tuning_):nearest_ragdoll_body(read_,base_,yanked,p.hand.position);limb && limb->world==world &&
               !lockout_.locked(limb->entity_id,seconds_from_ms(now))) { // a runaway guard is not yanked back into a hold inside his lockout
                auto at_hand=limb->pose;at_hand.position=at_hand.position+(p.hand.position-limb->contact(p.hand.position));
                if(lane.grab.bind(at_hand,p.hand.position,grab_acquire_distance)) {
                    lane.held=limb;lane.acquired=true;lane.reset_tow();lane.owner_player=p.owner.player;lane.owner_camera=p.owner.camera;
                    lane.owner_generation=p.owner.camera_identity.generation;lane.owner_epoch=p.epoch;++picked_;++yank_holds_;yanked_entity_[side]=0;
                    begin_hold(lane,side,*limb,now);
                    probe_masses(*limb,now);
                }
            }
        } else if(sample && physical_usable && lane.grab.pressed() && !lane.acquired && (!lane.attempted || now-lane.last_acquisition>=100)) {
            lane.last_acquisition=now;
            const auto& other=lanes_[1-side].held;
            std::optional<NativeRigidBody> body;double distance=2.;
            std::vector<NativeRigidBody> valid;std::vector<PalmTarget> targets;
            for(const auto& cached:lane.candidates) {
                if(other && other->entity==cached.entity && other->handle==cached.handle) continue;
                const auto current=native_grab_body(read_,base_,cached);
                if(!current || current->identity!=cached.identity || current->component!=cached.component ||
                   current->handle!=cached.handle || current->model!=cached.model || current->world!=cached.world || current->entity_id!=cached.entity_id) continue;
                if(!acquisition_contact(lane,*current,now))continue;
                if(current->bone>=0 && lockout_.locked(current->entity_id,seconds_from_ms(now))) continue; // a runaway body: no hold for 1 s
                if(!hold_bone_allowed(current->bone,p.grip)) continue;
                const auto d=current->contact_distance(p.hand.position);
                if(d<distance) {distance=d;body=current;}
                valid.push_back(*current);targets.push_back(palm_body_target(*current,tuning_));
            }
            // G2 (Task 5): the same palm cost ranks the bind (the one hysteresis: the lane's current pick at x 0.86); no palm probe: the nearest, as before.
            if(valid.size()>1 && !legacy_palm_select_ && p.palm.valid) {
                const auto pick=pick_by_palm(p.palm,targets,lane.selection.current(),tuning_.hover_radius_m);
                if(pick.index) {body=valid[*pick.index];distance=body->contact_distance(p.hand.position);}
            }
            if(acquisition_samples_++<1000) {
                acquisitions_<<now<<','<<side<<','<<p.hand.position.x<<','<<p.hand.position.y<<','<<p.hand.position.z<<','
                    <<lane.candidates.size()<<','<<(body?body->entity_id:0)<<','<<distance<<'\n';acquisitions_.flush();
            }
            if(body && obstruction_required_ && !cleared(lane,*body,now)) {
                request_obstruction(side,*body);lane.decision_deferred=true;lane.gate=Gate::obstruction_pending;return;
            }
            const auto contact=body?acquisition_contact(lane,*body,now):std::nullopt;
            if(body && contact && body->world==world && cleared(lane,*body,now) && lane.grab.bind(body->pose,*contact,grab_acquire_distance)) {
                lane.held=body;lane.acquired=true;lane.reset_tow();lane.owner_player=p.owner.player;lane.owner_camera=p.owner.camera;
                lane.owner_generation=p.owner.camera_identity.generation;lane.owner_epoch=p.epoch;++picked_;
                begin_hold(lane,side,*body,now);
                probe_masses(*body,now);
            }
        }
    }
    void update(std::uintptr_t dispatcher) {
        ++callbacks_;
        std::unique_lock<std::mutex> lock(mutex_,std::try_to_lock);if(!lock.owns_lock()) return;
        const auto world=rig::value<std::uintptr_t>(read_,dispatcher+0x18);
        const auto game=rig::value<std::uintptr_t>(read_,base_+0x39a5290);
        if(!world || !game || !rig::typed(read_,base_,*game,0x1c940a8) || rig::value<std::uintptr_t>(read_,*game+0x60410)!=world) return;
        const auto now=GetTickCount64();
        pass_cost_us_=0;
        if(coupling_on_) {std::unique_lock<std::mutex> pub(coupling_mutex_,std::try_to_lock);   // B16: the render thread's couplings (never waited for)
         if(pub.owns_lock()) for(unsigned side=0;side<2;++side) if(coupling_pub_[side].serial!=lanes_[side].coupling.serial || coupling_pub_[side].tick!=lanes_[side].coupling.tick) lanes_[side].coupling=coupling_pub_[side];}
        for(unsigned side=0;side<2;++side) update_lane(side,*world,now);
        publish_snapshot_locked();
        if(pass_cost_us_) { // the capped hold's game-thread time for both hands in this callback (budget 0.2 ms)
            corpse_cost_us_+=pass_cost_us_;
            auto seen=corpse_cost_max_us_.load();while(pass_cost_us_>seen && !corpse_cost_max_us_.compare_exchange_weak(seen,pass_cost_us_)) {}
        }
    }
    // Blade poke: a dynamic prop the sword tip struck gets the tip's push velocity, written here like the grab motor
    // (0x89fb0 stays the only velocity caller). One request, consumed within 100 ms.
    struct Poke {std::uintptr_t entity{};Vec3 velocity{};UINT64 tick{};};Poke poke_{};
    NativeHandContact contact_;ScanReader scan_,discovery_scan_;ThreadClock clock_;
    // Safe release (held_clamp.hpp SafeRelease): a dropped object that is still inside the world gets a small outward velocity (<= 1.5 m/s, asked for by
    // the render thread every frame until it is clear), written here with the other velocity writers. One request (release_depen.hpp), consumed within
    // 100 ms; it names the entity, its entity id and its throwable component's handle, all re-read just before the write, and a new hold cancels it. A
    // native hold's item is not a native_rigid_body prop (that mapping refuses the native-carry component): its body is the single throwable component
    // the layer swap writes (NativeHeldObject::exclusion_body), the same mapping the exclusion sweeps and the release watch use.
    ReleaseDepen depen_{};
    void apply_release_depen() {
        ReleaseDepenRequest d{};std::uintptr_t world=0;
        {std::lock_guard<std::mutex> lock(mutex_);
            if(!depen_.pending()) return;
            const auto held=[&](std::uintptr_t entity) {for(const auto& lane:lanes_) if(lane.held && lane.held->entity==entity) return true;return false;};
            const auto live_id=[&](std::uintptr_t entity)->std::optional<std::uint32_t> {return rig::value<std::uint32_t>(read_,entity+0xa8);};
            const auto live_handle=[&](std::uintptr_t entity)->std::optional<std::uint32_t> {
                const auto body=NativeHeldObject::exclusion_body(base_,read_,entity);if(!body) return std::nullopt;world=body->world;return body->handle;};
            const auto verdict=depen_.take(GetTickCount64(),live_id,live_handle,held,d);
            if(verdict==ReleaseDepen::Verdict::none || verdict==ReleaseDepen::Verdict::held || verdict==ReleaseDepen::Verdict::stale) return;
            if(verdict!=ReleaseDepen::Verdict::ready) {++depen_rejected_;return;}
        }
        alignas(16) std::array<float,4> linear{static_cast<float>(d.velocity.x),static_cast<float>(d.velocity.y),static_cast<float>(d.velocity.z),0};
        alignas(16) std::array<float,4> angular{};
        CrashOperation operation("native release depenetration velocity");
        const auto result=write_velocity(reinterpret_cast<void*>(world),d.handle,linear.data(),angular.data());
        if(result==VelocityWrite::faulted) {++contact_.faults;release_all();return;}
        if(result==VelocityWrite::written) ++depen_writes_;
    }
    void apply_poke() {
        Poke p{};{std::lock_guard<std::mutex> lock(mutex_);p=poke_;poke_={};
            for(const auto& lane:lanes_) if(lane.held && lane.held->entity==p.entity) return;} // a held prop follows its hand
        if(!p.entity || GetTickCount64()-p.tick>100) return;
        const auto body=native_rigid_body(read_,base_,p.entity);if(!body) return;
        alignas(16) std::array<float,4> linear{static_cast<float>(p.velocity.x),static_cast<float>(p.velocity.y),static_cast<float>(p.velocity.z),0};
        alignas(16) std::array<float,4> angular{};
        CrashOperation operation("native blade poke velocity");
        const auto result=write_velocity(reinterpret_cast<void*>(body->world),body->handle,linear.data(),angular.data());
        if(result==VelocityWrite::faulted) {++contact_.faults;release_all();return;}
        if(result==VelocityWrite::written) ++pokes_;
    }
    // Solid hands: loose props an open empty hand touches take its velocity (native_hand_contact.hpp). Here with the other velocity
    // writers; a prop a lane holds is its motor's.
    void apply_contact(std::uintptr_t dispatcher) {
        if(!active_ || !contact_.wanted()) return;
        const auto world=rig::value<std::uintptr_t>(read_,dispatcher+0x18);
        if(!world || logic_()!=2) return;
        std::array<std::uintptr_t,2> held{};
        {std::unique_lock<std::mutex> lock(mutex_,std::try_to_lock);if(!lock.owns_lock()) return;
            for(unsigned side=0;side<2;++side) if(lanes_[side].held) held[side]=lanes_[side].held->entity;}
        contact_.service(scan_,base_,*world,GetTickCount64(),held,[&](std::uintptr_t w,std::uint32_t handle,const std::array<float,3>& v,const std::array<float,3>& spin) {
            alignas(16) std::array<float,4> linear{v[0],v[1],v[2],0};
            alignas(16) std::array<float,4> angular{spin[0],spin[1],spin[2],0};
            CrashOperation operation("native hand contact velocity");
            const auto result=write_velocity(reinterpret_cast<void*>(w),handle,linear.data(),angular.data());
            if(result==VelocityWrite::faulted) {if(contact_.last_write_ragdoll) trip_fault_latch();++contact_.faults;release_all();}
        },[this] {return clock_.micros();});
    }
    void invoke(void* manager,void* command) {
        // B7 probe (build/hand-body-probe, read-only): cadence of this hook, the record type, and the physics busy flag as the record is dispatched.
        if(HandBodyProbeCounters::enabled.load(std::memory_order_relaxed)) {
            try {HandBodyProbeCounters::note_dispatch(read_,base_,reinterpret_cast<std::uintptr_t>(command),[] {return static_cast<std::uint32_t>(GetCurrentThreadId());});} catch(...) {}
        }
        original_(manager,command);
        // B11 spike (build/hand-body-spike, sim only): set only when the knob installed it; otherwise one relaxed load.
        if(const auto spike=hand_body_spike_dispatch.load(std::memory_order_relaxed)) {try {spike(manager);} catch(...) {}}
        // B13 hand body (build/hand-body, default off): set only when the knob installed it; otherwise one relaxed load.
        if(const auto hand_body=hand_body_dispatch.load(std::memory_order_relaxed)) {try {hand_body(manager);} catch(...) {}}
        try {apply_poke();} catch(...) {}
        try {apply_release_depen();} catch(...) {}
        try {apply_contact(reinterpret_cast<std::uintptr_t>(manager));} catch(...) {++contact_.faults;}
        try {update(reinterpret_cast<std::uintptr_t>(manager));} catch(...) {
            release_all();
        }
    }
    static void __fastcall dispatch(void* manager,void* command) {
        auto& self=*instance_;++self.depth_;
        __try {self.invoke(manager,command);} __finally {--self.depth_;}
    }
public:
    struct DiagnosticLane {
        UINT64 epoch{},publication_tick{},serial{},acquisition_tick{},focus_tick{},focus_calls{},focus_us{},focus_max_us{};
        std::uintptr_t player{},camera{};std::optional<NativeRigidBody> focus,held;
        NativeGrabDiscovery::Diagnostics discovery{};bool usable{},occupied{},pending_release{},deferred{};
        Gate gate=Gate::none;float raw_grip{};UINT64 decision_tick{},source_age_ms{},source_tick{};
        RightGripOwner::Role role=RightGripOwner::Role::release;unsigned candidates{};
        bool released_armed{},source_age_valid{};
        NativePlayerCamera publication_owner{};
        UINT64 lifecycle_revision{};
    };
    std::optional<std::array<DiagnosticLane,2>> diagnostics(UINT64 now=GetTickCount64()) {
        std::unique_lock<std::mutex> lock(mutex_,std::try_to_lock);if(!lock.owns_lock())return {};
        std::array<DiagnosticLane,2> out{};
        for(unsigned side=0;side<2;++side) {
            const auto& lane=lanes_[side];const auto& p=lane.published;
            out[side]={p.epoch,p.tick,p.serial,lane.last_acquisition,lane.focus_tick,lane.focus_calls,lane.focus_us,lane.focus_max_us,
                p.owner.player,p.owner.camera,lane.focus,lane.held,lane.discovery.diagnostics(),p.usable,p.occupied,lane.pending_release,lane.decision_deferred,
                lane.gate,lane.raw_grip,lane.decision_tick,now>=(p.source_tick?p.source_tick:p.tick)?now-(p.source_tick?p.source_tick:p.tick):0,p.source_tick?p.source_tick:p.tick,lane.owner.role(),static_cast<unsigned>(lane.candidates.size()),
                lane.owner.role()==RightGripOwner::Role::ready,(p.source_tick?p.source_tick:p.tick)!=0 && now>=(p.source_tick?p.source_tick:p.tick),p.owner,lifecycle_revision_};
        }
        return out;
    }
    std::optional<Decision> pop_decision() {
        std::unique_lock<std::mutex> lock(mutex_,std::try_to_lock);if(!lock.owns_lock() || !decision_count_) return {};
        const auto record=decisions_[decision_begin_];decision_begin_=(decision_begin_+1)%decisions_.size();--decision_count_;return record;
    }
    UINT64 decision_dropped() const {return decision_dropped_.load();}
    void require_contact_proxies(bool required) {std::lock_guard<std::mutex> lock(mutex_);contact_required_=required;
        for(auto& lane:lanes_) {lane.contact_proxies.reset();lane.focus.reset();lane.focus_tick=0;}}
    void contact_proxies(unsigned side,std::optional<HandProxies> proxies,UINT64 epoch,UINT64 tick) {
        std::lock_guard<std::mutex> lock(mutex_);auto& lane=lanes_.at(side);
        lane.contact_proxies=std::move(proxies);lane.contact_epoch=epoch;lane.contact_tick=tick;lane.focus_tick=0;
    }
    void publish(unsigned side,const NativePlayerCamera& owner,const WorldTrackedPose& hand,float grip,UINT64 epoch,UINT64 tick,bool usable,bool occupied=false,UINT64 source_tick=0,
                 const std::optional<HandProxies>* proxies=nullptr,UINT64 geometry_tick=0,const PalmProbe& palm=PalmProbe{}) {
        last_published_camera_.store(owner.camera,std::memory_order_relaxed); // lock-free copy for the read-only B7 probe
        std::lock_guard<std::mutex> lock(mutex_);auto& lane=lanes_.at(side);auto& p=lane.published;
        if(proxies && contact_required_) {lane.contact_proxies=*proxies;lane.contact_epoch=epoch;lane.contact_tick=geometry_tick;lane.focus_tick=0;}
        if(p.epoch!=epoch || !same_publisher_owner(p.owner,owner) || p.usable!=usable || p.occupied!=occupied)advance_lifecycle_locked();
        if(p.epoch!=epoch || p.owner.player!=owner.player || p.owner.camera!=owner.camera ||
           p.owner.camera_identity!=owner.camera_identity || !usable || occupied) {lane.focus.reset();lane.focus_tick=0;lane.discovery.reset();}
        // A hover LOS request/verdict (Task 8) outlives a new frame of the same hand owner; a press request is per publication, as before.
        const bool keep_hover=lane.obstruction && lane.obstruction->request.hover && p.epoch==epoch && p.owner.camera_identity==owner.camera_identity && usable && !occupied;
        if((p.tick!=tick || p.source_tick!=source_tick || p.epoch!=epoch || p.owner.camera_identity!=owner.camera_identity || !usable || occupied) && !keep_hover) lane.obstruction.reset();
        if(p.epoch!=epoch || !usable || occupied) lane.selection.reset();   // a new pose epoch or a lost hand starts the palm's pick afresh
        p={owner,hand,grip,epoch,tick,p.serial+1,usable,occupied,source_tick,palm};
    }
    void clear() {{std::lock_guard<std::mutex> lock(mutex_);advance_lifecycle_locked();for(auto& lane:lanes_) {lane.published.usable=false;lane.contact_proxies.reset();lane.obstruction.reset();++lane.published.serial;lane.focus.reset();lane.focus_tick=0;}}contact_.clear();}
    bool holding(unsigned side) {std::lock_guard<std::mutex> lock(mutex_);return lanes_.at(side).held.has_value();}
    // B16, render thread: the capped hold `serial` (HoldAnchorView::serial) follows the hand body (offset: the body minus the palm, world metres)
    // or not (nullopt: the controller, as before); release: HoldCoupling's lag rule fired. Its own small mutex, held only for the copy (DeepSeek
    // P2: never mutex_, which a dispatcher pass holds for its whole run); the dispatcher copies it with try_lock at the start of each pass.
    void couple(unsigned side,std::uint64_t serial,const std::optional<Vec3>& offset,bool release,UINT64 tick) {
        if(side>=2) return;
        std::lock_guard<std::mutex> lock(coupling_mutex_);auto& c=coupling_pub_[side];
        c.active=offset.has_value();c.offset=offset?*offset:Vec3{};c.release=release;c.tick=tick;c.serial=serial;
    }
    std::atomic<UINT64> coupled_releases_{},coupling_rebases_{};   // B16: capped holds let go by the hand-lag rule; grabs rebased on a coupling edge
private:
    std::mutex coupling_mutex_;std::array<Lane::Coupling,2> coupling_pub_{};std::uint64_t hold_serials_{};
    bool coupling_on_{};   // B16 review P3-2: build/hand-body (and no build/no-hand-body), read once at install before the hook runs; off: no B16 work
                           // (review N-3: a hand-body install that fails later leaves it on: the same behaviour, nothing is ever published to it)
public:
    static NativeGrab* instance() noexcept {return instance_;}
    // The entity whose ragdoll limb this hand holds (0 if none or a prop).
    std::uintptr_t held_ragdoll(unsigned side) {
        std::lock_guard<std::mutex> lock(mutex_);const auto& h=lanes_.at(side).held;return h && h->bone>=0?h->entity:0;
    }
    // Size of what this hand holds (the diagonal of its drawn bounds; a limb's sphere box), for fingers that close around it.
    std::optional<NativeRigidBody> held(unsigned side) {std::lock_guard<std::mutex> lock(mutex_);return side<2?lanes_[side].held:std::nullopt;}
    std::optional<double> held_size(unsigned side) {
        std::lock_guard<std::mutex> lock(mutex_);const auto& h=lanes_.at(side).held;if(!h) return {};
        return h->bounds?length(h->bounds->maximum-h->bounds->minimum):.3;
    }
    NativeHandContact& hand_contact() {return contact_;}
    // Solid hands, discovery: the registry scan that finds the props near a palm, once a frame from the render thread after the hands are published
    // (native_hand_contact.hpp discover): the physics thread keeps only the bodies and the pushes. Its own reader: a ScanReader is single-threaded.
    void discover_contacts() {
        if(!active_ || !contact_.wanted() || logic_()!=2) return;
        try {contact_.discover(discovery_scan_,base_);} catch(...) {++contact_.faults;}
    }
    void poke(std::uintptr_t entity,Vec3 velocity,UINT64 now) {std::lock_guard<std::mutex> lock(mutex_);poke_={entity,velocity,now};}
    // Render thread: the outward velocity of a released object inside the world (the next dispatcher call applies it). Never above depen_max_speed.
    static constexpr double depen_max_speed=ReleaseDepen::max_speed;
    void release_depen(std::uintptr_t entity,std::uint32_t id,std::uint32_t handle,Vec3 velocity,UINT64 now) {std::lock_guard<std::mutex> lock(mutex_);depen_.request({entity,id,handle,velocity,now});}
    void cancel_release_depen() {std::lock_guard<std::mutex> lock(mutex_);depen_.cancel();}
    std::atomic<UINT64> depen_writes_{},depen_rejected_{};
    // A guard yanked into ragdoll by this hand (NativeWeaponHands::yank): its lane takes his nearest limb until `until`.
    static void yanked(unsigned side,std::uintptr_t entity,UINT64 until) {yanked_entity_[side&1]=entity;yanked_until_[side&1]=until;}
    inline static std::array<std::atomic<std::uintptr_t>,2> yanked_entity_{};inline static std::array<std::atomic<UINT64>,2> yanked_until_{};
    inline static std::atomic<UINT64> yank_holds_{};
    // A prop let go faster than 3 m/s, for the game-thread service (NativeWeaponHands): the game's own thrown state, or thrown hits.
    struct Thrown {std::uintptr_t entity{},component{};std::uint32_t entity_id{};Vec3 velocity{};UINT64 tick{};};
    static std::optional<Thrown> take_thrown() {std::lock_guard<std::mutex> g(thrown_mutex_);auto t=thrown_;thrown_.reset();return t;}
    inline static std::mutex thrown_mutex_;inline static std::optional<Thrown> thrown_{};
    std::atomic<UINT64> pokes_{},tows_{},tow_unreadable_{};
    GrabTuning tuning_{}; // the shipped defaults (tow ON); install() replaces them with build/grab-tuning.cfg when that file is valid
    GrabTuning prop_tuning_{tuning_.prop_view()}; // v609 G6: the capped prop lanes' tuning (prop_strength_scale), refreshed wherever tuning_ is set
    // Corpse hold (Task 10). configure_corpse runs before the first update (install() calls it with the build/ knobs); the options replace the
    // enable flag and the tuning at once. corpse_diagnostics() is the F4 block of the status line (dxgi_probe.cpp reads it).
    void configure_corpse(const CorpseOptions& options) {
        std::lock_guard<std::mutex> lock(mutex_);
        corpse_enabled_=options.enabled;tuning_=options.tuning;prop_tuning_=tuning_.prop_view();corpse_diag_.set_tuning(options.from_file,options.tuning.tow);
    }
    CorpseDiagnostics& corpse_diagnostics() noexcept {return corpse_diag_;}
    // The grasp point of what this hand holds as of the last dispatcher tick (render thread; a corpse or a prop). active=false when nothing is held.
    HoldAnchorView hold_anchor(unsigned side) {
        if(side>=2) return {};
        std::lock_guard<std::mutex> lock(snapshot_mutex_);return snapshot_[side]; // never mutex_: the render thread does not wait for a dispatcher pass
    }
    bool corpse_enabled() {std::lock_guard<std::mutex> lock(mutex_);return corpse_enabled_;}
    bool prop_grasp_point() const noexcept {return prop_grasp_point_;} // !build/no-prop-grasp-point, read at install; the drawn hand sits on a motor-held prop (Task 12)
    bool no_ragdoll_posts() const noexcept {return no_posts_;} // build/no-ragdoll-posts, read at install; the wake path (Task 11) honours it
    // The runaway lockout is written on the physics thread (dispatcher) and read from the game thread (Task 11 wake/retry), the status thread and the contact pass;
    // EntityLockout carries its own small mutex, so none of those readers touches mutex_.
    EntityLockout& lockout() noexcept {return lockout_;}
    std::uint64_t lock_refused() const noexcept {return lockout_.refused();}
    void attach_corpse_trace(CorpseTraceWriter* trace) {std::lock_guard<std::mutex> lock(mutex_);corpse_trace_=trace;}
    UINT64 velocity_unknown() const {return velocity_unknown_.load();}
    UINT64 corpse_cost_us() const {return corpse_cost_us_.load();}UINT64 corpse_cost_max_us() const {return corpse_cost_max_us_.load();}
    bool waiting_native_query(unsigned side) {std::lock_guard<std::mutex> lock(mutex_);return lanes_.at(side).decision_deferred;}
    // G2/G3 (Task 5): this hand's palm pick (fresh within 250 ms of its last scoring), for the grip-alone decision (Task 6) and the status block.
    std::optional<FocusScore> focus_score(unsigned side,UINT64 now,UINT64 max_age_ms=250) {
        if(side>=2) return {};
        std::lock_guard<std::mutex> lock(mutex_);const auto& lane=lanes_[side];
        if(!lane.focus || !lane.score || !lane.focus_tick || now<lane.focus_tick || now-lane.focus_tick>max_age_ms) return {};
        return lane.score;
    }
    // {buffered_grabs, buffer_expired} of this hand since install.
    std::pair<std::uint64_t,std::uint64_t> buffer_counts(unsigned side) {
        if(side>=2) return {};
        std::lock_guard<std::mutex> lock(mutex_);return {lanes_[side].buffer.buffered(),lanes_[side].buffer.expired()};
    }
    bool legacy_palm_select() const noexcept {return legacy_palm_select_;}
    // G6 (Task 14). living_held: this hand holds a living NPC. reserve_npc: a choke or takedown is offered for this NPC (entity id), fed every input
    // frame before filter_native_grip. The take_* events are consumed by the input thread (NativeWeaponHands requests): the hold's start (the shove,
    // the finger blend), a hard pull (the yank on him), a pull step (the teleport, only with the verdict's pull).
    bool living_held(unsigned side) {if(side>=2) return false;std::lock_guard<std::mutex> lock(mutex_);return lanes_[side].living.has_value();}
    std::optional<LivingBone> living_grasp(unsigned side) {if(side>=2) return {};std::lock_guard<std::mutex> lock(mutex_);const auto& l=lanes_[side].living;return l?std::optional<LivingBone>(l->grasp()):std::nullopt;}
    void reserve_npc(unsigned side,std::uint32_t entity_id,UINT64 now) {if(side>=2) return;std::lock_guard<std::mutex> lock(mutex_);lanes_[side].reserved_npc=entity_id;lanes_[side].reserved_tick=entity_id?now:0;}
    std::optional<LivingBone> take_living_start(unsigned side) {if(side>=2) return {};std::lock_guard<std::mutex> lock(mutex_);auto v=lanes_[side].living_started;lanes_[side].living_started.reset();return v;}
    std::optional<LivingBone> take_hard_pull(unsigned side) {if(side>=2) return {};std::lock_guard<std::mutex> lock(mutex_);auto v=lanes_[side].hard_pull;lanes_[side].hard_pull.reset();return v;}
    std::optional<std::pair<std::uintptr_t,std::array<float,3>>> take_living_pull(unsigned side) {if(side>=2) return {};std::lock_guard<std::mutex> lock(mutex_);auto v=lanes_[side].pull_request;lanes_[side].pull_request.reset();return v;}
    void living_pull(bool enabled) {std::lock_guard<std::mutex> lock(mutex_);living_pull_enabled_=enabled;}
    bool living_enabled() {std::lock_guard<std::mutex> lock(mutex_);return living_enabled_;}
    struct LivingCounters {UINT64 holds{},hard_pulls{},releases{};};
    LivingCounters living_counters() const {return {living_holds_.load(),living_hard_pulls_.load(),living_releases_.load()};}
    // G3 (Task 8): the stage-0 hit owners' registry indices for this hand's discovery (taken within 250 ms), from NativeInteractionAim::hint_indices.
    void hints(unsigned side,const std::array<unsigned,8>& indices,UINT64 tick) {if(side>=2) return;std::lock_guard<std::mutex> lock(mutex_);lanes_[side].hints=indices;lanes_[side].hint_tick=tick;}
    // Status: is the LOS (obstruction) query required (on whenever the physical grab is attached to the interaction aim), and the hover LOS counters.
    struct HoverLos {bool required{};UINT64 requests{},verdicts{},used{};};
    HoverLos hover_los() {bool required{};{std::lock_guard<std::mutex> lock(mutex_);required=obstruction_required_;}return {required,hover_requests_.load(),hover_verdicts_.load(),hover_used_.load()};}
    // The palm probe last published for this hand (the one its physical pick was scored with), for the grip-alone decision (Task 6).
    PalmProbe published_palm(unsigned side) {if(side>=2) return {};std::lock_guard<std::mutex> lock(mutex_);return lanes_[side].published.palm;}
    // Status block "grab_focus" (plan Task 5): per hand {id,kind,cost,runner_up,switches_10s,hysteresis_held,scoring_fallback,buffered_grabs,
    // buffer_expired,palm_dot}. try_lock: the status thread never waits for a dispatcher pass (null when busy).
    std::string grab_focus_json(UINT64 now) {
        std::unique_lock<std::mutex> lock(mutex_,std::try_to_lock);if(!lock.owns_lock()) return "null";
        static constexpr const char* kinds[]={"loot","usable","prop","bone","living"};
        std::ostringstream o;o<<std::setprecision(6)<<'[';
        for(unsigned side=0;side<2;++side) {
            const auto& lane=lanes_[side];
            const bool fresh=lane.focus && lane.score && lane.focus_tick && now>=lane.focus_tick && now-lane.focus_tick<=250;
            if(side) o<<',';
            o<<"{\"id\":"<<(fresh?lane.score->id:0)<<",\"kind\":";
            if(fresh) o<<'"'<<kinds[static_cast<unsigned>(lane.score->kind)%5]<<'"';else o<<"null";
            o<<",\"cost\":"<<(fresh?lane.score->cost:0.)<<",\"runner_up\":"<<(fresh?lane.score->runner_up:0.)
             <<",\"switches_10s\":"<<lane.selection.switches_10s(now)<<",\"hysteresis_held\":"<<lane.selection.hysteresis_held()
             <<",\"scoring_fallback\":"<<lane.selection.scoring_fallback()<<",\"buffered_grabs\":"<<lane.buffer.buffered()
             <<",\"buffer_expired\":"<<lane.buffer.expired()<<",\"palm_dot\":"<<(fresh?lane.palm_dot:0.)
             <<",\"thin_prop\":{\"id\":"<<lane.reject_id<<",\"result\":\""<<body_reject_name(lane.reject)<<"\"}}";
        }
        o<<']';return o.str();
    }
    // Entity of the physics prop this hand would grab, 0 if none.
    std::uintptr_t focus_entity(unsigned side,UINT64 now) {
        if(!focus_point(side,now)) return 0;
        std::lock_guard<std::mutex> lock(mutex_);const auto& f=lanes_[side].focus;return f?f->entity:0;
    }
    // The fingers' hover candidate (G4 pre-curl and hover shape: dxgi_probe.cpp hover_candidate_for_fingers): the lane's focus as the last
    // focus_point refresh left it (no discovery, no engine read here), when that refresh is at most 150 ms old.
    std::optional<NativeRigidBody> focus_cached(unsigned side,UINT64 now) {
        if(side>=2) return {};
        std::lock_guard<std::mutex> lock(mutex_);const auto& lane=lanes_[side];
        if(!lane.focus || !lane.focus_tick || now<lane.focus_tick || now-lane.focus_tick>150) return {};
        return lane.focus;
    }
    std::optional<Vec3> focus_point(unsigned side,UINT64 now,bool native_use=false) {
        if(side>=2) return {};
        std::lock_guard<std::mutex> lock(mutex_);auto& lane=lanes_[side];const auto& p=lane.published;
        if(native_use || !active_ || !p.usable || p.occupied || lane.held || lane.living || lane.pending_release || !p.epoch ||
           now<p.tick || now-p.tick>grab_pose_freshness_ms ||
           (p.source_tick && (now<p.source_tick || now-p.source_tick>grab_pose_freshness_ms)) || !logic_ || logic_()!=2) {lane.focus.reset();lane.focus_tick=0;return {};}
        try {
            const auto owner=native_player_camera(read_,base_,p.owner.camera);
            if(!owner || owner->player!=p.owner.player || owner->camera_identity!=p.owner.camera_identity || owner->authored_view()) {
                lane.focus.reset();lane.focus_tick=0;return {};
            }
            const auto body=focus_body(side,now,false);
            if(body) request_hover_obstruction(side,*body,now);
            return body?std::optional<Vec3>(body->contact(p.hand.position)):std::nullopt;
        } catch(...) {lane.focus.reset();lane.focus_tick=0;return {};}
    }
    // Preview reads only. Reserving at .35 prevents RB's approximately .551
    // threshold from preceding the native motor's .65 acquisition threshold.
    // Returns the squeeze left for the native button (0 while the grip grabs).
    // Caller-supplied native carrying/action occupancy returns the raw squeeze;
    // the physical role is revoked and never re-reserved during that press.
    // no_buffer (fix round 1, I2): the right hand holds or has drawn the sword: decide at the edge (the RB block must not wait 150 ms).
    // grip_alone (Task 6): a grip-alone grab press; its candidate must be within the grip-alone gate (0.12 m) at the decision and in the buffer.
    float filter_native_grip(unsigned side,float raw,UINT64 epoch,UINT64 now,bool gameplay,bool focused,bool use,bool defer_decision=false,bool grip_alone=false,bool no_buffer=false) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& lane=lanes_.at(side);auto& p=lane.published;lane.decision_deferred=false;
        lane.raw_grip=raw;lane.decision_tick=now;lane.gate=Gate::none;lane.decision_usable=false;
        struct Record {NativeGrab& self;unsigned side;UINT64 tick;~Record(){self.record_decision(side,tick);}} record{*this,side,now};
        if(epoch!=lane.grip_epoch || p.owner.player!=lane.grip_player || p.owner.camera!=lane.grip_camera ||
           p.owner.camera_identity.generation!=lane.grip_generation) {
            if(lane.owner.role()!=RightGripOwner::Role::release)advance_lifecycle_locked();
            lane.owner={};lane.buffer.cancel();lane.buffer_used=false;
            if(lane.held) {lane.pending_release=true;lane.release_throw=false;}
            lane.grip_epoch=epoch;lane.grip_player=p.owner.player;lane.grip_camera=p.owner.camera;lane.grip_generation=p.owner.camera_identity.generation;
        }
        if(p.occupied) {
            lane.gate=Gate::occupied;
            if(lane.held && !lane.pending_release) {lane.pending_release=true;lane.release_throw=true;} // item took the hand: throw with hand velocity, never a dead drop
            const auto role_before=lane.owner.role();const float native=lane.owner.filter(raw,focused,false,true);note_role_change_locked(role_before,lane);
            lane.buffer.cancel();p.grip=raw;++p.serial;return native;
        }
        if(defer_decision && !lane.held && raw>=.35f && !lane.owner.physical()) {
            lane.gate=Gate::deferred;lane.decision_deferred=true;p.grip=raw;++p.serial;return 0;
        }
        if(!active_) lane.gate=Gate::inactive;
        else if(!p.usable) lane.gate=Gate::pose_unusable;
        else if(!std::isfinite(raw) || raw<0 || raw>1) lane.gate=Gate::invalid_grip;
        else if(!gameplay) lane.gate=Gate::not_gameplay;
        else if(!focused) lane.gate=Gate::unfocused;
        else if(use && !lane.held) lane.gate=Gate::native_use; // trigger/Use while holding must not drop the prop
        else if(epoch!=p.epoch || !p.epoch) lane.gate=Gate::epoch_mismatch;
        else if(!p.tick) lane.gate=Gate::source_missing;
        else if(now<p.tick || (p.source_tick && now<p.source_tick)) lane.gate=Gate::source_future;
        else if(now-p.tick>grab_pose_freshness_ms || (p.source_tick && now-p.source_tick>grab_pose_freshness_ms)) lane.gate=Gate::source_stale;
        const auto owner=lane.gate==Gate::none?native_player_camera(read_,base_,p.owner.camera):std::optional<NativePlayerCamera>{};
        if(lane.gate==Gate::none) {
            if(!owner || owner->player!=p.owner.player || owner->camera_identity!=p.owner.camera_identity) lane.gate=Gate::owner_invalid;
            else if(owner->authored_view()) lane.gate=Gate::authored_view;
        }
        const bool usable=lane.gate==Gate::none;lane.decision_usable=usable;
        if(lane.held && !lane.pending_release && (raw<.35f || !usable)) {
            lane.pending_release=true;lane.release_throw=raw<.35f && usable;
        }
        if(!usable) {revoke_owner_locked(lane);lane.obstruction.reset();}
        bool eligible=false;
        const double gate=grip_alone?tuning_.grip_alone_gate_m:grab_acquire_distance;
        // G3 (Task 5): a buffered press looks again each frame. A candidate inside the gate is caught: the press is decided again below, on
        // this frame, as a normal press. The window's end resolves it native (the squeeze then passes as today), counted once.
        if(lane.owner.pending() && usable && !lane.pending_release && focused && std::isfinite(raw) && raw>=.35f) {
            const auto candidate=focus_body(side,now,true);
            const auto gap=candidate?focus_gap(lane,*candidate):std::nullopt;
            const auto step=lane.buffer.step(now,gap,candidate?candidate->entity_id:0);
            // caught: re-arm the press latch now (the dispatcher sampled this squeeze as not ours while it was pending), so the bind works whether the press
            // is decided below or after a LOS query (service_obstruction samples and binds; acceptance sim T124714 caught a corpse but never bound it)
            if(step==GrabIntentBuffer::Step::caught) {lane.owner.catch_pending();lane.catch_tick=now;lane.grab.arm();}
            else if(step!=GrabIntentBuffer::Step::waiting) {
                const auto role_before=lane.owner.role();lane.owner.resolve_pending(false);note_role_change_locked(role_before,lane);++native_presses_;
            }
        }
        const bool deciding=lane.owner.deciding(raw,focused);
        if(deciding && usable && !lane.pending_release) {
            // Use the same bounded surface target as the hover cue.
            ++queries_;auto candidate=focus_body(side,now,true);
            if(candidate) {const auto gap=focus_gap(lane,*candidate);if(!gap || *gap>gate) candidate.reset();}
            lane.candidates.clear();lane.living_candidate.reset();
            // G6 (Task 14): only a grip-alone press holds a living NPC (the fist keeps today's yank); his bone within the grip-alone gate
            if(!candidate && grip_alone && living_enabled_ && lane.living_focus && lane.score && lane.score->kind==PalmKind::living &&
               lane.score->distance<=gate && !npc_reserved(lane.living_focus->entity_id,now)) {lane.living_candidate=lane.living_focus;eligible=true;}
            if(candidate) {
                lane.candidates.push_back(*candidate);
                if(obstruction_required_ && lane.obstruction && lane.obstruction->request.hover && cleared(lane,*candidate,now)) ++hover_used_;
                if(obstruction_required_ && !cleared(lane,*candidate,now)) {
                    request_obstruction(side,*candidate);lane.gate=Gate::obstruction_pending;
                    lane.decision_deferred=true;p.grip=raw;++p.serial;return 0;
                }
                eligible=true;
            }
        }
        // G3: a press with nothing in reach starts the buffer (once per press) instead of latching native; build/legacy-palm-select or
        // grab_buffer_ms 0 keep today's single-frame decision.
        const bool buffer=deciding && usable && !lane.pending_release && !eligible && !lane.buffer_used && !legacy_palm_select_ && !no_buffer && tuning_.grab_buffer_ms>0;
        const auto role_before=lane.owner.role();float native=0;
        if(buffer) {lane.owner.begin_pending();lane.buffer.start(now,static_cast<std::uint64_t>(tuning_.grab_buffer_ms),gate);lane.buffer_used=true;}
        else native=lane.owner.filter(raw,focused,eligible);
        note_role_change_locked(role_before,lane);
        if(!lane.owner.pending()) lane.buffer.cancel();   // released, unfocused, occupied or resolved: the buffer ends with the pending role
        if(deciding && !lane.owner.pending()) {if(lane.owner.physical()) ++physical_presses_;else ++native_presses_;}
        // A press granted physical arms the latch: `deciding` already required an observed release (role ready), and a squeeze that reaches the lane at
        // once (a grip-alone grab publishes the grip only after its decision; a buffered catch) may have been sampled as not ours just before (sim T125301).
        if(deciding && lane.owner.physical()) {lane.attempted=true;lane.acquired=false;lane.pending_release=false;lane.grab.arm();}
        if(!std::isfinite(raw) || raw<.35f) lane.buffer_used=false;
        // Arm only after a real observed release. This is controller math;
        // the renderer does not bind bodies or call the velocity ABI.
        if(!lane.held && usable && raw<.35f) lane.grab.sample(p.hand,raw,true,p.epoch,p.tick);
        if(usable) {
            if(lane.pending_release) lane.gate=Gate::pending_release;
            else if(lane.held) lane.gate=Gate::physical_held;
            else if(raw<.35f) lane.gate=Gate::release_armed;
            else if(lane.owner.physical()) lane.gate=Gate::physical_reserved;
            else if(lane.owner.pending()) lane.gate=Gate::buffer_pending;
            else if(deciding) lane.gate=Gate::native_no_candidate;
            else if(lane.owner.role()==RightGripOwner::Role::native) lane.gate=Gate::native_locked;
            else lane.gate=Gate::awaiting_release;
        }
        p.grip=raw;++p.serial;
        return native;
    }
    // Called only by the owned stage-0 sensor scope. Query executes without
    // mailbox locks; the exact publication and body are checked again afterward.
    void require_obstruction(bool required=true) {std::lock_guard<std::mutex> lock(mutex_);obstruction_required_=required;for(auto& lane:lanes_)lane.obstruction.reset();}
    template<class Query> void service_obstruction(const NativePlayerCamera& camera,UINT64 now,Query query) {
        for(unsigned side=0;side<2;++side) {
            std::optional<ObstructionRequest> obstruction_request;
            {std::lock_guard<std::mutex> lock(mutex_);const auto& lane=lanes_[side];
                if(active_ && obstruction_required_ && lane.obstruction && !lane.obstruction->clear &&
                   (lane.obstruction->request.hover?hover_request_live(lane.obstruction->request,lane.published,now):
                    request_current(lane.obstruction->request,lane.published,now) && lane.published.grip>=.35f) &&
                   camera.player==lane.published.owner.player && camera.camera==lane.published.owner.camera &&
                   camera.camera_identity==lane.published.owner.camera_identity && !camera.authored_view()) obstruction_request=lane.obstruction->request;
            }
            if(!obstruction_request)continue;
            const auto owner=native_player_camera(read_,base_,obstruction_request->owner.camera);
            const auto body=native_grab_body(read_,base_,obstruction_request->body);
            if(!owner || owner->authored_view() || owner->camera_identity!=obstruction_request->owner.camera_identity ||
               !body || !same_body(*body,obstruction_request->body) || logic_()!=2 ||
               length(body->contact(obstruction_request->hand.position)-obstruction_request->body.contact(obstruction_request->hand.position))>=.001)continue;
            {std::lock_guard<std::mutex> lock(mutex_);const auto& lane=lanes_[side];
                if(!active_ || !lane.obstruction || lane.obstruction->request.token!=obstruction_request->token ||
                   !(obstruction_request->hover?hover_request_live(*obstruction_request,lane.published,GetTickCount64()):
                     request_current(*obstruction_request,lane.published,GetTickCount64())))continue;
            }
            const auto verdict=query(*obstruction_request);
            const auto after=native_grab_body(read_,base_,obstruction_request->body);
            const auto after_owner=native_player_camera(read_,base_,obstruction_request->owner.camera);
            std::lock_guard<std::mutex> lock(mutex_);auto& lane=lanes_[side];auto& p=lane.published;
            if(obstruction_request->hover) {   // a hover verdict is recorded for the press; it decides and binds nothing (Task 8)
                if(!active_ || !lane.obstruction || lane.obstruction->request.token!=obstruction_request->token ||
                   !hover_request_live(*obstruction_request,p,GetTickCount64()) || p.occupied || lane.held ||
                   !after || !same_body(*after,obstruction_request->body) || !after_owner || after_owner->authored_view() ||
                   after_owner->camera_identity!=obstruction_request->owner.camera_identity || logic_()!=2 ||
                   length(after->contact(obstruction_request->hand.position)-obstruction_request->body.contact(obstruction_request->hand.position))>=hover_contact_m || !verdict) continue;
                lane.obstruction->clear=*verdict;lane.obstruction->verdict_tick=GetTickCount64();++hover_verdicts_;continue;
            }
            if(!active_ || !lane.obstruction || lane.obstruction->request.token!=obstruction_request->token ||
               !request_current(*obstruction_request,p,GetTickCount64()) || p.grip<.35f || p.occupied || lane.held ||
               !after || !same_body(*after,obstruction_request->body) || !after_owner || after_owner->authored_view() ||
               after_owner->camera_identity!=obstruction_request->owner.camera_identity || logic_()!=2 ||
               length(after->contact(obstruction_request->hand.position)-obstruction_request->body.contact(obstruction_request->hand.position))>=.001 || !verdict)continue;
            lane.obstruction->clear=*verdict;lane.obstruction->verdict_tick=GetTickCount64();lane.decision_deferred=false;
            if(lane.owner.deciding(p.grip,true)) {
                const auto role_before=lane.owner.role();lane.owner.filter(p.grip,true,*verdict);note_role_change_locked(role_before,lane);lane.attempted=*verdict;lane.acquired=false;
                if(lane.owner.physical()) lane.grab.arm();   // the press is ours from here (see filter_native_grip)
                if(*verdict)++physical_presses_;else ++native_presses_;
            }
            if(!*verdict)revoke_owner_locked(lane);
            // Binding is controller math and internal ownership only. No native
            // write runs here: 0x89fb0 remains the sole velocity/motor caller.
            // Commit the queried pose before another eye publication replaces it.
            const auto& other=lanes_[1-side].held;
            const auto contact=acquisition_contact(lane,*after,GetTickCount64());
            if(*verdict && contact && lane.owner.physical() && hold_bone_allowed(after->bone,p.grip) && !(after->bone>=0 && lockout_.locked(after->entity_id,seconds_from_ms(GetTickCount64()))) && (!other || other->entity!=after->entity || other->handle!=after->handle) &&
               lane.grab.sample(p.hand,p.grip,true,p.epoch,p.tick) &&
               lane.grab.bind(after->pose,*contact,grab_acquire_distance)) {
                lane.held=after;lane.acquired=true;lane.pending_release=false;lane.reset_tow();
                lane.owner_player=p.owner.player;lane.owner_camera=p.owner.camera;
                lane.owner_generation=p.owner.camera_identity.generation;lane.owner_epoch=p.epoch;++picked_;
                begin_hold(lane,side,*after,GetTickCount64());
            }
            lane.gate=*verdict?(lane.held?Gate::physical_held:Gate::physical_reserved):Gate::obstruction_blocked;
            ++p.serial;record_decision(side,GetTickCount64());
        }
    }
    // The install-time corpse knobs of a mod root (<root>/build/no-corpse-grab, no-ragdoll-posts, grab-tuning.cfg): a valid cfg replaces the tuning, an
    // invalid one leaves the defaults and says why, no-corpse-grab clears `enabled`. Pure, so a test can run it on a temp directory.
    struct CorpseKnobs {CorpseOptions options;KillSwitches kill;TuningLoad loaded;};
    static CorpseKnobs corpse_knobs_from(const std::filesystem::path& root) {
        CorpseKnobs knobs;
        knobs.kill=KillSwitches::read(root/L"build");knobs.loaded=load_grab_tuning(root/L"build"/L"grab-tuning.cfg");
        if(knobs.kill.legacy_corpse_strength) knobs.loaded.value.strength_scale=kLegacyCorpseStrengthScale;   // plan H1: build/legacy-corpse-strength restores the pre-H1 (3x) caps, over a cfg value too
        knobs.options.enabled=!knobs.kill.no_corpse_grab;knobs.options.tuning=knobs.loaded.value;knobs.options.from_file=knobs.loaded.source==TuningSource::file;
        return knobs;
    }
    void install(std::uintptr_t base,Read read,Logic logic,const std::filesystem::path& file) {
        base_=base;read_=read;logic_=logic;
        std::array<unsigned char,32> code{};
        if(!read(base+rva,code.data(),code.size()) || code!=dispatch_code || !read(base+velocity_rva,code.data(),code.size()) || code!=velocity_code)
            throw std::runtime_error("Native physical-grab dispatch or velocity ABI differs");
        loss_file_=file.parent_path()/L"native-physical-grab-loss.txt";
        {std::ofstream reset(loss_file_,std::ios::trunc);}
        log_.open(file,std::ios::trunc);if(!log_) throw std::runtime_error("Cannot open native grab evidence");
        acquisitions_.open(file.parent_path()/L"native-grab-acquisition.csv",std::ios::trunc);
        if(!acquisitions_) throw std::runtime_error("Cannot open native acquisition evidence");
        mass_probe_=false;mass_probe_rows_=0;mass_probe_out_.close();
        {std::error_code ec;const auto root=file.parent_path().parent_path();
         collar_grab_=std::filesystem::exists(root/L"build"/L"collar-grab",ec);
         legacy_palm_select_=std::filesystem::exists(root/L"build"/L"legacy-palm-select",ec);
         living_enabled_=!std::filesystem::exists(root/L"build"/L"no-living-hold",ec);
         if(!living_enabled_) {std::ofstream note(loss_file_,std::ios::app);note<<"knob build/no-living-hold present: living NPCs are no hold candidates (the fist yank stays)\n";}
         if(legacy_palm_select_) {std::ofstream note(loss_file_,std::ios::app);note<<"knob build/legacy-palm-select present: nearest-AABB grab pick, no grab buffer\n";}
         coupling_on_=hand_body_knob_enabled(std::filesystem::exists(root/L"build"/L"hand-body",ec),std::filesystem::exists(root/L"build"/L"no-hand-body",ec));
         if(collar_grab_) {std::ofstream note(loss_file_,std::ios::app);note<<"knob build/collar-grab present: chest/neck hold targets enabled (full fist only)\n";}
         // Corpse hold knobs and the tuning file, read once here so the shipped configuration is the one that was tested (valid file or the defaults).
         const auto knobs=corpse_knobs_from(root);const auto& kill=knobs.kill;const auto& loaded=knobs.loaded;
         no_posts_=kill.no_posts;prop_grasp_point_=!kill.no_prop_grasp_point;for(auto& l:lanes_) l.grab.set_legacy_lag_release(kill.legacy_corpse_lag_release);
         configure_corpse(knobs.options);
         {std::ofstream note(loss_file_,std::ios::app);
          if(kill.no_corpse_grab) note<<"knob build/no-corpse-grab present: corpse bodies are not grab candidates\n";
          if(kill.legacy_corpse_lag_release) note<<"knob build/legacy-corpse-lag-release present: corpse holds drop at 0.5 m lag / 0.25 s, no grasp slide or slip\n";
          if(kill.no_posts) note<<"knob build/no-ragdoll-posts present: no ragdoll event posts\n";
          note<<(kill.no_prop_grasp_point?"knob build/no-prop-grasp-point present: the drawn hand stays at the controller on held props\n":"prop grasp point: the drawn hand sits on motor-held props (corpses always anchor)\n");
          if(!loaded.error.empty()) note<<"build/grab-tuning.cfg rejected, defaults in use: "<<loaded.error<<'\n';
          else if(loaded.source==TuningSource::file) note<<"build/grab-tuning.cfg loaded (tow "<<(loaded.value.tow?"on":"off")<<", strength_scale "<<loaded.value.strength_scale<<")\n";}
         if(std::filesystem::exists(root/L"build"/L"mass-probe",ec)) {
            mass_probe_out_.open(file.parent_path()/L"mass-probe.csv",std::ios::trunc);
            if(mass_probe_out_) {mass_probe_out_<<mass_probe_csv_header()<<'\n';mass_probe_out_.flush();mass_probe_=true;}
         }}
        acquisitions_<<std::setprecision(9)<<"tick_ms,side,hand_x,hand_y,hand_z,candidates,nearest_entity_id,contact_distance\n";
        log_<<std::setprecision(9)<<"tick_ms,side,entity_id,component,generation,handle,body_flags,release,x,y,z,target_x,target_y,target_z,vx,vy,vz,wx,wy,wz\n";
        {std::lock_guard<std::mutex> lock(mutex_);advance_lifecycle_locked();lanes_={};lockout_.clear();}
        decision_begin_=decision_count_=0;last_decision_={};decision_dropped_=0;acquisition_samples_=0;picked_=moved_=dropped_=lost_=queries_=samples_=callbacks_=0;native_presses_=physical_presses_=0;
        velocity_=reinterpret_cast<Velocity>(base+velocity_rva);instance_=this;
        contact_.write_blocked=[this](std::uintptr_t entity,bool guard) {return contact_write_blocked(entity,guard);};
        contact_.install(file.parent_path()/L"native-hand-contact.csv");contact_.inline_discovery=false;clock_.calibrate();
        const auto target=reinterpret_cast<void*>(base+rva);
        if(!original_ && MH_CreateHook(target,reinterpret_cast<void*>(&dispatch),reinterpret_cast<void**>(&original_))!=MH_OK) throw std::runtime_error("Cannot create native grab callback");
        active_=true;if(MH_EnableHook(target)!=MH_OK) {active_=false;throw std::runtime_error("Cannot enable native grab callback");}enabled_=true;
    }
    bool disable() noexcept {
        active_=false;clear();
        // Bodies stay dynamic: stopping the motor needs no physics-mode restore.
        if(enabled_) {if(MH_DisableHook(reinterpret_cast<void*>(base_+rva))!=MH_OK) return false;enabled_=false;}
        const auto deadline=GetTickCount64()+5000;while(depth_ && GetTickCount64()<deadline) Sleep(1);if(depth_) return false;
        std::array<unsigned char,32> code{};const bool restored=!original_ || (read_(base_+rva,code.data(),code.size()) && code==dispatch_code);
        try {std::lock_guard<std::mutex> lock(mutex_);for(unsigned side=0;side<2;++side) {auto& lane=lanes_[side];if(lane.held && lane.corpse) corpse_diag_.note_release(side,ReleaseReason::lifecycle);lane.grab.release(false);lane.end_hold();}publish_snapshot_locked();log_.close();acquisitions_.close();mass_probe_out_.close();mass_probe_=false;} catch(...) {return false;}
        return restored;
    }
    bool enabled() const noexcept {return active_.load(std::memory_order_acquire);}
    UINT64 picked() const {return picked_.load();}UINT64 moved() const {return moved_.load();}UINT64 dropped() const {return dropped_.load();}
    UINT64 lost() const {return lost_.load();}UINT64 queries() const {return queries_.load();}UINT64 callbacks() const {return callbacks_.load();}
    UINT64 native_presses() const {return native_presses_.load();}UINT64 physical_presses() const {return physical_presses_.load();}
};
} // namespace d2vr
