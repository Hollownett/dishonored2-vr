#pragma once
#include "native_guarded_call.hpp"
#include <windows.h>
#include <MinHook.h>
#include "native_rigid_body.hpp"
#include "native_hand_contact.hpp"
#include "mass_probe.hpp"
#include "full_hand_contact.hpp"
#include "native_player_camera.hpp"
#include "crash_observer.hpp"
#include "physical_grab.hpp"
#include "right_grip_owner.hpp"
#include "selection_policy.hpp"
#include <atomic>
#include <mutex>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <chrono>
#include <limits>
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
    template<class Lookup,class Validate> const std::vector<NativeRigidBody>& advance(
            Vec3 hand,std::uint64_t tick,Lookup lookup,Validate validate) {
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
        for(unsigned scanned=0;scanned<slice_size;++scanned) {
            const auto index=cursor_;if(++cursor_==registry_size)cursor_=1;
            const auto body=lookup(index);
            if(!body || body->contact_distance(hand)>grab_retain_distance+3)continue;
            const auto found=std::find_if(bodies_.begin(),bodies_.end(),[&](const auto& b){return b.entity==body->entity;});
            if(found!=bodies_.end()) {*found=*body;continue;}
            if(bodies_.size()<capacity)bodies_.push_back(*body);
            else {
                const auto farthest=std::max_element(bodies_.begin(),bodies_.end(),[&](const auto& a,const auto& b){return a.contact_distance(hand)<b.contact_distance(hand);});
                if(body->contact_distance(hand)<farthest->contact_distance(hand))*farthest=*body;
            }
        }
        return bodies_;
    }
};
class NativeGrab {
    friend struct NativeGrabCheck;
public:
    enum class Gate : unsigned {none,occupied,deferred,inactive,pose_unusable,invalid_grip,not_gameplay,unfocused,
        native_use,epoch_mismatch,source_missing,source_future,source_stale,owner_invalid,authored_view,
        awaiting_release,release_armed,native_no_candidate,native_locked,physical_reserved,physical_held,pending_release,obstruction_pending,obstruction_blocked};
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
    };
private:
    struct Obstruction {ObstructionRequest request{};std::optional<bool> clear;};
    using Read=bool (*)(std::uintptr_t,void*,std::size_t);
    using Logic=UINT (*)();
    using Dispatch=void (__fastcall*)(void*,void*);
    using Velocity=void (__fastcall*)(void*,UINT,const float*,const float*,int);
    struct Publication {NativePlayerCamera owner{};WorldTrackedPose hand{};float grip{};UINT64 epoch{},tick{},serial{};bool usable{};bool occupied{};UINT64 source_tick{};};
    // One independent lane per hand. Either hand may hold one dynamic body.
    // The caller marks native carrying/action occupancy. Equipped weapons do
    // not prevent either tracked hand from acquiring a loose physics object.
    struct Lane {
        Publication published{};std::optional<Obstruction> obstruction;PhysicalGrab grab{};std::optional<NativeRigidBody> held;RightGripOwner owner{};
        std::vector<NativeRigidBody> candidates;NativeGrabDiscovery discovery;
        std::optional<NativeRigidBody> focus;UINT64 focus_tick{};Vec3 focus_hand{};std::uintptr_t focus_excluded{};
        bool pending_release{},release_throw{},attempted{},acquired{},decision_deferred{};
        double tow{}; // a dragged body's pelvis follows its grabbed limb at this distance (set on the first motor write)
        Gate gate=Gate::none;float raw_grip{};UINT64 decision_tick{};bool decision_usable{};
        UINT64 consumed{},last_motor{},last_acquisition{},owner_epoch{},grip_epoch{};
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
    std::optional<NativeRigidBody> focus_body(unsigned side,UINT64 now,bool refresh) {
        auto& lane=lanes_[side];const auto& p=lane.published;const auto& other=lanes_[1-side].held;
        struct Cost {
            Lane& lane;std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();
            ~Cost() {const auto us=static_cast<UINT64>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count());
                ++lane.focus_calls;lane.focus_us+=us;lane.focus_max_us=std::max(lane.focus_max_us,us);}
        } cost{lane};
        const auto excluded=other?other->entity:0;const auto excluded_handle=other?other->handle:0;
        const auto game=rig::value<std::uintptr_t>(read_,base_+0x39a5290);
        if(!game || !rig::typed(read_,base_,*game,0x1c940a8)) {lane.discovery.reset();lane.focus.reset();return {};}
        const auto& candidates=lane.discovery.advance(p.hand.position,p.tick,[&](unsigned index)->std::optional<NativeRigidBody> {
            const auto entity=rig::value<std::uintptr_t>(read_,*game+0x3130b0+index*8);
            const bool guard=entity && rig::typed(read_,base_,*entity,0x1c5e108); // a corpse or knocked-out guard: drag by a limb
            if(!entity || !(guard || rig::typed(read_,base_,*entity,0x1c886a8) || rig::typed(read_,base_,*entity,0x1c87d58) || rig::typed(read_,base_,*entity,0x1c5de18)))return {}; // + arkMovable (bottles, glasses: thrown by hand)
            const auto id=rig::value<std::uint32_t>(read_,*entity+0xa8);
            const auto pos=rig::value<std::array<float,3>>(read_,*entity+0x300);
            if(!id || (*id>>16)!=index || !pos)return {};
            const Vec3 point{(*pos)[0],(*pos)[1],(*pos)[2]};
            if(!finite(point) || length(point-p.hand.position)>grab_retain_distance+3)return {};
            return guard?corpse_candidate(read_,base_,*entity,p.hand.position,collar_grab_):native_rigid_body(read_,base_,*entity);
        },[&](const NativeRigidBody& body){return body.bone<0?native_rigid_body(read_,base_,body.entity):corpse_candidate(read_,base_,body.entity,p.hand.position,collar_grab_);});
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
        lane.focus=grab_focus_candidate(contact_required_?touching:candidates,p.hand.position,excluded,false,preferred,excluded_handle);lane.focus_tick=now;lane.focus_hand=p.hand.position;lane.focus_excluded=excluded;
        return lane.focus;
    }
    static bool same_body(const NativeRigidBody& a,const NativeRigidBody& b) {
        // Generation only: a held prop's component flag word can change once it moves (headset v447: the ashtray was lost 30 ms
        // after every grab, eligible, same handle and world); ragdoll bodies already compare the generation alone.
        return a.entity==b.entity && a.entity_id==b.entity_id && a.component==b.component && a.identity.generation==b.identity.generation &&
            a.handle==b.handle && a.model==b.model && a.world==b.world && a.record==b.record;
    }
    static bool request_current(const ObstructionRequest& r,const Publication& p,UINT64 now) {
        return p.usable && !p.occupied && p.epoch==r.epoch && p.tick==r.tick && p.source_tick==r.source_tick &&
            length(p.hand.position-r.hand.position)==0 &&
            length(p.hand.basis[0]-r.hand.basis[0])==0 && length(p.hand.basis[1]-r.hand.basis[1])==0 && length(p.hand.basis[2]-r.hand.basis[2])==0 &&
            p.owner.player==r.owner.player && p.owner.camera==r.owner.camera && p.owner.camera_identity==r.owner.camera_identity &&
            now>=r.tick && now-r.tick<=grab_pose_freshness_ms &&
            (!r.source_tick || (now>=r.source_tick && now-r.source_tick<=grab_pose_freshness_ms));
    }
    bool cleared(Lane& lane,const NativeRigidBody& body,UINT64 now) {
        return !obstruction_required_ || (lane.obstruction && lane.obstruction->clear.value_or(false) &&
            request_current(lane.obstruction->request,lane.published,now) && same_body(lane.obstruction->request.body,body) &&
            length(lane.obstruction->request.body.contact(lane.published.hand.position)-body.contact(lane.published.hand.position))<.001);
    }
    void request_obstruction(unsigned side,const NativeRigidBody& body) {
        auto& lane=lanes_[side];const auto& p=lane.published;
        if(lane.obstruction && request_current(lane.obstruction->request,p,p.tick) && same_body(lane.obstruction->request.body,body)) return;
        lane.obstruction=Obstruction{{side,++obstruction_token_,p.epoch,p.tick,p.source_tick,p.owner,p.hand,body},{}};
    }
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
        for(auto& lane:lanes_) {lane.grab.release(false);lane.held.reset();lane.pending_release=false;revoke_owner_locked(lane);lane.candidates.clear();}
        ++lost_;
    }
    void release_all() {std::lock_guard<std::mutex> lock(mutex_);release_all_locked();}
    // Returns false when the setter faulted and every lane was released: the caller must not touch lane state further.
    bool write(Lane& lane,unsigned side,const NativeRigidBody& body,const GrabMotor& motor) {
        alignas(16) std::array<float,4> linear{static_cast<float>(motor.linear.x),static_cast<float>(motor.linear.y),static_cast<float>(motor.linear.z),0};
        alignas(16) std::array<float,4> angular{static_cast<float>(motor.angular.x),static_cast<float>(motor.angular.y),static_cast<float>(motor.angular.z),0};
        CrashOperation operation("native physical grab velocity");
        const auto result=write_velocity(reinterpret_cast<void*>(body.world),body.handle,linear.data(),angular.data());
        if(result==VelocityWrite::faulted) {++contact_.faults;release_all_locked();return false;}
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
    void tow(Lane& lane,const NativeRigidBody& held) {
        const auto pelvis=native_ragdoll_body(read_,base_,held.entity,0);if(!pelvis) return;
        Vec3 pull=lane.grab.target().position-pelvis->pose.position;pull.z=0;const auto reach=length(pull);
        if(lane.tow<=0) {lane.tow=std::clamp(reach,.4,1.4);return;}
        if(reach<=lane.tow+.05) return;
        const auto speed=std::min((reach-lane.tow)*6,4.);
        alignas(16) std::array<float,4> linear{static_cast<float>(pull.x/reach*speed),static_cast<float>(pull.y/reach*speed),0,0};
        alignas(16) std::array<float,4> angular{};
        CrashOperation operation("native ragdoll tow velocity");
        const auto result=write_velocity(reinterpret_cast<void*>(pelvis->world),pelvis->handle,linear.data(),angular.data());
        if(result==VelocityWrite::faulted) {++contact_.faults;release_all_locked();return;}
        if(result==VelocityWrite::written) ++tows_;
    }
    void update_lane(unsigned side,std::uintptr_t world,UINT64 now) {
        auto& lane=lanes_[side];
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
        if(lane.held && !physical_usable && !lane.pending_release) {lane.pending_release=true;lane.release_throw=false;revoke_owner_locked(lane);}
        const bool same_owner=lane.owner_player==p.owner.player && lane.owner_camera==p.owner.camera &&
            lane.owner_generation==p.owner.camera_identity.generation && lane.owner_epoch==p.epoch;
        const bool sample=lane.held && lane.pending_release?false:lane.grab.sample(p.hand,p.grip,
            usable && (physical_usable || (!lane.held && p.grip<.35f)) && (!lane.held || same_owner),p.epoch,p.tick);
        if(!lane.grab.pressed()) {lane.attempted=lane.acquired=false;if(!lane.owner.physical() && !lane.held) lane.candidates.clear();}
        if(lane.held) {
            const auto body=native_grab_body(read_,base_,*lane.held);
            const auto h=*lane.held;
            if(!body || !same_body(*body,h) || body->world!=world) {
                std::ofstream loss(loss_file_,std::ios::app);
                loss<<"tick="<<now<<" side="<<side<<" entity="<<h.entity<<" component="<<h.component<<" eligible="<<body.has_value()<<" world="<<world<<" original_world="<<h.world<<'\n';
                lane.grab.release(false);lane.held.reset();lane.pending_release=false;revoke_owner_locked(lane);
                lane.obstruction.reset();lane.focus.reset();lane.focus_tick=0;lane.candidates.clear();lane.discovery.reset();
                if(h.bone<0) ++lost_; // a ragdoll limb goes when the guard gets up (or his body settles): a release, not a loss
                return;
            }
            lane.held=*body;
            const auto motor=lane.pending_release?std::optional<GrabMotor>{lane.grab.release_sample(p.hand,p.grip,usable && same_owner,p.epoch,p.tick,lane.release_throw)}:
                lane.grab.motor(body->pose,physical_usable && sample && same_owner,now,p.source_tick);
            if(motor && (motor->release || now-lane.last_motor>=10)) {
                if(!write(lane,side,*body,*motor)) return;
                lane.last_motor=now;
                if(body->bone>0 && !motor->release) tow(lane,*body);
                if(motor->release) {
                    if(body->bone<0 && length(motor->linear)>3) {std::lock_guard<std::mutex> g(thrown_mutex_);thrown_=Thrown{body->entity,body->component,body->entity_id,motor->linear,now};}
                    lane.held.reset();lane.pending_release=false;lane.tow=0;}
            }
        } else if(const auto yanked=yanked_entity_[side].load();yanked && now<yanked_until_[side].load() && sample && physical_usable && lane.grab.pressed()) {
            // The guard this hand just yanked into ragdoll (NativeWeaponHands::yank): the hand had his collar or arm, so it takes
            // his nearest limb and pulls it into the fist (bound as if the limb's contact point were at the hand).
            if(const auto limb=nearest_ragdoll_body(read_,base_,yanked,p.hand.position);limb && limb->world==world) {
                auto at_hand=limb->pose;at_hand.position=at_hand.position+(p.hand.position-limb->contact(p.hand.position));
                if(lane.grab.bind(at_hand,p.hand.position,grab_acquire_distance)) {
                    lane.held=limb;lane.acquired=true;lane.tow=0;lane.owner_player=p.owner.player;lane.owner_camera=p.owner.camera;
                    lane.owner_generation=p.owner.camera_identity.generation;lane.owner_epoch=p.epoch;++picked_;++yank_holds_;yanked_entity_[side]=0;
                    probe_masses(*limb,now);
                }
            }
        } else if(sample && physical_usable && lane.grab.pressed() && !lane.acquired && (!lane.attempted || now-lane.last_acquisition>=100)) {
            lane.last_acquisition=now;
            const auto& other=lanes_[1-side].held;
            std::optional<NativeRigidBody> body;double distance=2.;
            for(const auto& cached:lane.candidates) {
                if(other && other->entity==cached.entity && other->handle==cached.handle) continue;
                const auto current=native_grab_body(read_,base_,cached);
                if(!current || current->identity!=cached.identity || current->component!=cached.component ||
                   current->handle!=cached.handle || current->model!=cached.model || current->world!=cached.world || current->entity_id!=cached.entity_id) continue;
                if(!acquisition_contact(lane,*current,now))continue;
                const auto d=current->contact_distance(p.hand.position);
                if(d<distance) {distance=d;body=current;}
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
                lane.held=body;lane.acquired=true;lane.tow=0;lane.owner_player=p.owner.player;lane.owner_camera=p.owner.camera;
                lane.owner_generation=p.owner.camera_identity.generation;lane.owner_epoch=p.epoch;++picked_;
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
        for(unsigned side=0;side<2;++side) update_lane(side,*world,now);
    }
    // Blade poke: a dynamic prop the sword tip struck gets the tip's push velocity, written here like the grab motor
    // (0x89fb0 stays the only velocity caller). One request, consumed within 100 ms.
    struct Poke {std::uintptr_t entity{};Vec3 velocity{};UINT64 tick{};};Poke poke_{};
    NativeHandContact contact_;ScanReader scan_,discovery_scan_;ThreadClock clock_;
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
            if(result==VelocityWrite::faulted) {++contact_.faults;release_all();}
        },[this] {return clock_.micros();});
    }
    void invoke(void* manager,void* command) {
        original_(manager,command);
        try {apply_poke();} catch(...) {}
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
                 const std::optional<HandProxies>* proxies=nullptr,UINT64 geometry_tick=0) {
        std::lock_guard<std::mutex> lock(mutex_);auto& lane=lanes_.at(side);auto& p=lane.published;
        if(proxies && contact_required_) {lane.contact_proxies=*proxies;lane.contact_epoch=epoch;lane.contact_tick=geometry_tick;lane.focus_tick=0;}
        if(p.epoch!=epoch || !same_publisher_owner(p.owner,owner) || p.usable!=usable || p.occupied!=occupied)advance_lifecycle_locked();
        if(p.epoch!=epoch || p.owner.player!=owner.player || p.owner.camera!=owner.camera ||
           p.owner.camera_identity!=owner.camera_identity || !usable || occupied) {lane.focus.reset();lane.focus_tick=0;lane.discovery.reset();}
        if(p.tick!=tick || p.source_tick!=source_tick || p.epoch!=epoch || p.owner.camera_identity!=owner.camera_identity || !usable || occupied) lane.obstruction.reset();
        p={owner,hand,grip,epoch,tick,p.serial+1,usable,occupied,source_tick};
    }
    void clear() {{std::lock_guard<std::mutex> lock(mutex_);advance_lifecycle_locked();for(auto& lane:lanes_) {lane.published.usable=false;lane.contact_proxies.reset();lane.obstruction.reset();++lane.published.serial;lane.focus.reset();lane.focus_tick=0;}}contact_.clear();}
    bool holding(unsigned side) {std::lock_guard<std::mutex> lock(mutex_);return lanes_.at(side).held.has_value();}
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
    // A guard yanked into ragdoll by this hand (NativeWeaponHands::yank): its lane takes his nearest limb until `until`.
    static void yanked(unsigned side,std::uintptr_t entity,UINT64 until) {yanked_entity_[side&1]=entity;yanked_until_[side&1]=until;}
    inline static std::array<std::atomic<std::uintptr_t>,2> yanked_entity_{};inline static std::array<std::atomic<UINT64>,2> yanked_until_{};
    inline static std::atomic<UINT64> yank_holds_{};
    // A prop let go faster than 3 m/s, for the game-thread service (NativeWeaponHands): the game's own thrown state, or thrown hits.
    struct Thrown {std::uintptr_t entity{},component{};std::uint32_t entity_id{};Vec3 velocity{};UINT64 tick{};};
    static std::optional<Thrown> take_thrown() {std::lock_guard<std::mutex> g(thrown_mutex_);auto t=thrown_;thrown_.reset();return t;}
    inline static std::mutex thrown_mutex_;inline static std::optional<Thrown> thrown_{};
    std::atomic<UINT64> pokes_{},tows_{};
    bool waiting_native_query(unsigned side) {std::lock_guard<std::mutex> lock(mutex_);return lanes_.at(side).decision_deferred;}
    // Entity of the physics prop this hand would grab, 0 if none.
    std::uintptr_t focus_entity(unsigned side,UINT64 now) {
        if(!focus_point(side,now)) return 0;
        std::lock_guard<std::mutex> lock(mutex_);const auto& f=lanes_[side].focus;return f?f->entity:0;
    }
    std::optional<Vec3> focus_point(unsigned side,UINT64 now,bool native_use=false) {
        if(side>=2) return {};
        std::lock_guard<std::mutex> lock(mutex_);auto& lane=lanes_[side];const auto& p=lane.published;
        if(native_use || !active_ || !p.usable || p.occupied || lane.held || lane.pending_release || !p.epoch ||
           now<p.tick || now-p.tick>grab_pose_freshness_ms ||
           (p.source_tick && (now<p.source_tick || now-p.source_tick>grab_pose_freshness_ms)) || !logic_ || logic_()!=2) {lane.focus.reset();lane.focus_tick=0;return {};}
        try {
            const auto owner=native_player_camera(read_,base_,p.owner.camera);
            if(!owner || owner->player!=p.owner.player || owner->camera_identity!=p.owner.camera_identity || owner->authored_view()) {
                lane.focus.reset();lane.focus_tick=0;return {};
            }
            const auto body=focus_body(side,now,false);
            return body?std::optional<Vec3>(body->contact(p.hand.position)):std::nullopt;
        } catch(...) {lane.focus.reset();lane.focus_tick=0;return {};}
    }
    // Preview reads only. Reserving at .35 prevents RB's approximately .551
    // threshold from preceding the native motor's .65 acquisition threshold.
    // Returns the squeeze left for the native button (0 while the grip grabs).
    // Caller-supplied native carrying/action occupancy returns the raw squeeze;
    // the physical role is revoked and never re-reserved during that press.
    float filter_native_grip(unsigned side,float raw,UINT64 epoch,UINT64 now,bool gameplay,bool focused,bool use,bool defer_decision=false) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& lane=lanes_.at(side);auto& p=lane.published;lane.decision_deferred=false;
        lane.raw_grip=raw;lane.decision_tick=now;lane.gate=Gate::none;lane.decision_usable=false;
        struct Record {NativeGrab& self;unsigned side;UINT64 tick;~Record(){self.record_decision(side,tick);}} record{*this,side,now};
        if(epoch!=lane.grip_epoch || p.owner.player!=lane.grip_player || p.owner.camera!=lane.grip_camera ||
           p.owner.camera_identity.generation!=lane.grip_generation) {
            if(lane.owner.role()!=RightGripOwner::Role::release)advance_lifecycle_locked();
            lane.owner={};
            if(lane.held) {lane.pending_release=true;lane.release_throw=false;}
            lane.grip_epoch=epoch;lane.grip_player=p.owner.player;lane.grip_camera=p.owner.camera;lane.grip_generation=p.owner.camera_identity.generation;
        }
        if(p.occupied) {
            lane.gate=Gate::occupied;
            if(lane.held && !lane.pending_release) {lane.pending_release=true;lane.release_throw=true;} // item took the hand: throw with hand velocity, never a dead drop
            const auto role_before=lane.owner.role();const float native=lane.owner.filter(raw,focused,false,true);note_role_change_locked(role_before,lane);
            p.grip=raw;++p.serial;return native;
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
        const bool deciding=lane.owner.deciding(raw,focused);
        if(deciding && usable && !lane.pending_release) {
            // Use the same bounded surface target as the hover cue.
            ++queries_;const auto candidate=focus_body(side,now,true);
            lane.candidates.clear();
            if(candidate) {
                lane.candidates.push_back(*candidate);
                if(obstruction_required_ && !cleared(lane,*candidate,now)) {
                    request_obstruction(side,*candidate);lane.gate=Gate::obstruction_pending;
                    lane.decision_deferred=true;p.grip=raw;++p.serial;return 0;
                }
                eligible=true;
            }
        }
        const auto role_before=lane.owner.role();const float native=lane.owner.filter(raw,focused,eligible);note_role_change_locked(role_before,lane);
        if(deciding) {if(lane.owner.physical()) ++physical_presses_;else ++native_presses_;}
        if(deciding && lane.owner.physical()) {lane.attempted=true;lane.acquired=false;lane.pending_release=false;}
        // Arm only after a real observed release. This is controller math;
        // the renderer does not bind bodies or call the velocity ABI.
        if(!lane.held && usable && raw<.35f) lane.grab.sample(p.hand,raw,true,p.epoch,p.tick);
        if(usable) {
            if(lane.pending_release) lane.gate=Gate::pending_release;
            else if(lane.held) lane.gate=Gate::physical_held;
            else if(raw<.35f) lane.gate=Gate::release_armed;
            else if(lane.owner.physical()) lane.gate=Gate::physical_reserved;
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
                   request_current(lane.obstruction->request,lane.published,now) && lane.published.grip>=.35f &&
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
                   !request_current(*obstruction_request,lane.published,GetTickCount64()))continue;
            }
            const auto verdict=query(*obstruction_request);
            const auto after=native_grab_body(read_,base_,obstruction_request->body);
            const auto after_owner=native_player_camera(read_,base_,obstruction_request->owner.camera);
            std::lock_guard<std::mutex> lock(mutex_);auto& lane=lanes_[side];auto& p=lane.published;
            if(!active_ || !lane.obstruction || lane.obstruction->request.token!=obstruction_request->token ||
               !request_current(*obstruction_request,p,GetTickCount64()) || p.grip<.35f || p.occupied || lane.held ||
               !after || !same_body(*after,obstruction_request->body) || !after_owner || after_owner->authored_view() ||
               after_owner->camera_identity!=obstruction_request->owner.camera_identity || logic_()!=2 ||
               length(after->contact(obstruction_request->hand.position)-obstruction_request->body.contact(obstruction_request->hand.position))>=.001 || !verdict)continue;
            lane.obstruction->clear=*verdict;lane.decision_deferred=false;
            if(lane.owner.deciding(p.grip,true)) {
                const auto role_before=lane.owner.role();lane.owner.filter(p.grip,true,*verdict);note_role_change_locked(role_before,lane);lane.attempted=*verdict;lane.acquired=false;
                if(*verdict)++physical_presses_;else ++native_presses_;
            }
            if(!*verdict)revoke_owner_locked(lane);
            // Binding is controller math and internal ownership only. No native
            // write runs here: 0x89fb0 remains the sole velocity/motor caller.
            // Commit the queried pose before another eye publication replaces it.
            const auto& other=lanes_[1-side].held;
            const auto contact=acquisition_contact(lane,*after,GetTickCount64());
            if(*verdict && contact && lane.owner.physical() && (!other || other->entity!=after->entity || other->handle!=after->handle) &&
               lane.grab.sample(p.hand,p.grip,true,p.epoch,p.tick) &&
               lane.grab.bind(after->pose,*contact,grab_acquire_distance)) {
                lane.held=after;lane.acquired=true;lane.pending_release=false;lane.tow=0;
                lane.owner_player=p.owner.player;lane.owner_camera=p.owner.camera;
                lane.owner_generation=p.owner.camera_identity.generation;lane.owner_epoch=p.epoch;++picked_;
            }
            lane.gate=*verdict?(lane.held?Gate::physical_held:Gate::physical_reserved):Gate::obstruction_blocked;
            ++p.serial;record_decision(side,GetTickCount64());
        }
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
        mass_probe_=false;mass_probe_rows_=0;mass_probe_out_.close();collar_grab_=false;
        {std::error_code ec;const auto root=file.parent_path().parent_path();
         collar_grab_=std::filesystem::exists(root/L"build"/L"collar-grab",ec);
         if(std::filesystem::exists(root/L"build"/L"mass-probe",ec)) {
            mass_probe_out_.open(file.parent_path()/L"mass-probe.csv",std::ios::trunc);
            if(mass_probe_out_) {mass_probe_out_<<mass_probe_csv_header()<<'\n';mass_probe_out_.flush();mass_probe_=true;}
         }}
        acquisitions_<<std::setprecision(9)<<"tick_ms,side,hand_x,hand_y,hand_z,candidates,nearest_entity_id,contact_distance\n";
        log_<<std::setprecision(9)<<"tick_ms,side,entity_id,component,generation,handle,body_flags,release,x,y,z,target_x,target_y,target_z,vx,vy,vz,wx,wy,wz\n";
        {std::lock_guard<std::mutex> lock(mutex_);advance_lifecycle_locked();lanes_={};}
        decision_begin_=decision_count_=0;last_decision_={};decision_dropped_=0;acquisition_samples_=0;picked_=moved_=dropped_=lost_=queries_=samples_=callbacks_=0;native_presses_=physical_presses_=0;
        velocity_=reinterpret_cast<Velocity>(base+velocity_rva);instance_=this;
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
        try {std::lock_guard<std::mutex> lock(mutex_);for(auto& lane:lanes_) {lane.grab.release(false);lane.held.reset();}log_.close();acquisitions_.close();mass_probe_out_.close();mass_probe_=false;} catch(...) {return false;}
        return restored;
    }
    bool enabled() const noexcept {return active_.load(std::memory_order_acquire);}
    UINT64 picked() const {return picked_.load();}UINT64 moved() const {return moved_.load();}UINT64 dropped() const {return dropped_.load();}
    UINT64 lost() const {return lost_.load();}UINT64 queries() const {return queries_.load();}UINT64 callbacks() const {return callbacks_.load();}
    UINT64 native_presses() const {return native_presses_.load();}UINT64 physical_presses() const {return physical_presses_.load();}
};
} // namespace d2vr
