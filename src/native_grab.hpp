#pragma once
#include <windows.h>
#include <MinHook.h>
#include "native_rigid_body.hpp"
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
namespace d2vr {
inline std::optional<NativeRigidBody> grab_focus_candidate(const std::vector<NativeRigidBody>& candidates,Vec3 hand,
        std::uintptr_t other_entity,bool native_use=false,std::uintptr_t preferred_entity=0) {
    if(native_use) return {};
    std::optional<NativeRigidBody> best,preferred;double distance=grab_acquire_distance;
    for(const auto& body:candidates) {
        if(other_entity && body.entity==other_entity) continue;
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
            if(!body || body->identity!=it->identity || body->entity_id!=it->entity_id ||
               body->component!=it->component || body->handle!=it->handle || body->world!=it->world ||
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
        Gate gate=Gate::none;float raw_grip{};UINT64 decision_tick{};bool decision_usable{};
        UINT64 consumed{},last_motor{},last_acquisition{},owner_epoch{},grip_epoch{};
        UINT64 focus_calls{},focus_us{},focus_max_us{};
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
    std::mutex mutex_;std::array<Lane,2> lanes_{};
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
        const auto excluded=other?other->entity:0;
        const auto game=rig::value<std::uintptr_t>(read_,base_+0x39a5290);
        if(!game || !rig::typed(read_,base_,*game,0x1c940a8)) {lane.discovery.reset();lane.focus.reset();return {};}
        const auto& candidates=lane.discovery.advance(p.hand.position,p.tick,[&](unsigned index)->std::optional<NativeRigidBody> {
            const auto entity=rig::value<std::uintptr_t>(read_,*game+0x3130b0+index*8);
            if(!entity || !(rig::typed(read_,base_,*entity,0x1c886a8) || rig::typed(read_,base_,*entity,0x1c87d58) || rig::typed(read_,base_,*entity,0x1c5de18)))return {}; // + arkMovable (bottles, glasses: thrown by hand)
            const auto id=rig::value<std::uint32_t>(read_,*entity+0xa8);
            const auto pos=rig::value<std::array<float,3>>(read_,*entity+0x300);
            if(!id || (*id>>16)!=index || !pos)return {};
            const Vec3 point{(*pos)[0],(*pos)[1],(*pos)[2]};
            if(!finite(point) || length(point-p.hand.position)>grab_retain_distance+3)return {};
            return native_rigid_body(read_,base_,*entity);
        },[&](const NativeRigidBody& body){return native_rigid_body(read_,base_,body.entity);});
        if(!refresh && lane.focus_tick && now>=lane.focus_tick && now-lane.focus_tick<100 &&
           length(p.hand.position-lane.focus_hand)<.05 && lane.focus_excluded==excluded) {
            if(!lane.focus) return {};
            const auto current=native_rigid_body(read_,base_,lane.focus->entity);
            if(current && current->identity==lane.focus->identity && current->component==lane.focus->component &&
               current->handle==lane.focus->handle && current->model==lane.focus->model && current->world==lane.focus->world && current->entity_id==lane.focus->entity_id &&
               current->contact_distance(p.hand.position)<=grab_retain_distance) return current;
        }
        const auto preferred=lane.focus && (!refresh || lane.focus->contact_distance(p.hand.position)<=grab_acquire_distance)?lane.focus->entity:0;
        lane.focus=grab_focus_candidate(candidates,p.hand.position,excluded,false,preferred);lane.focus_tick=now;lane.focus_hand=p.hand.position;lane.focus_excluded=excluded;
        return lane.focus;
    }
    static bool same_body(const NativeRigidBody& a,const NativeRigidBody& b) {
        return a.entity==b.entity && a.entity_id==b.entity_id && a.component==b.component && a.identity==b.identity &&
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
    void write(Lane& lane,unsigned side,const NativeRigidBody& body,const GrabMotor& motor) {
        alignas(16) std::array<float,4> linear{static_cast<float>(motor.linear.x),static_cast<float>(motor.linear.y),static_cast<float>(motor.linear.z),0};
        alignas(16) std::array<float,4> angular{static_cast<float>(motor.angular.x),static_cast<float>(motor.angular.y),static_cast<float>(motor.angular.z),0};
        CrashOperation operation("native physical grab velocity");
        velocity_(reinterpret_cast<void*>(body.world),body.handle,linear.data(),angular.data(),0);
        if(motor.release) ++dropped_;else ++moved_;
        if(samples_<4000) {
            log_<<GetTickCount64()<<','<<side<<','<<body.entity_id<<','<<body.component<<','<<body.identity.generation<<','<<body.handle
                <<','<<body.flags<<','<<motor.release<<','<<body.pose.position.x<<','<<body.pose.position.y<<','<<body.pose.position.z;
            const auto target=lane.grab.target();log_<<','<<target.position.x<<','<<target.position.y<<','<<target.position.z;
            for(unsigned i=0;i<3;++i) log_<<','<<linear[i];for(unsigned i=0;i<3;++i) log_<<','<<angular[i];log_<<'\n';++samples_;
            if(samples_%60==0 || motor.release) log_.flush();
        }
    }
    void update_lane(unsigned side,std::uintptr_t world,UINT64 now) {
        auto& lane=lanes_[side];
        if(lane.consumed==lane.published.serial && (!lane.held || now-lane.last_motor<10)) return;
        const auto& p=lane.published;lane.consumed=p.serial;
        // A pending native query must not disarm the observed-release physics
        // latch before the final per-press owner can be chosen.
        if(lane.decision_deferred && !lane.held) return;
        const auto owner=native_player_camera(read_,base_,p.owner.camera);
        const bool usable=active_ && p.usable && owner && owner->player==p.owner.player && owner->camera_identity==p.owner.camera_identity &&
            logic_()==2 && !owner->authored_view() && now>=p.tick && now-p.tick<=grab_pose_freshness_ms && p.epoch &&
            (!p.source_tick || (now>=p.source_tick && now-p.source_tick<=grab_pose_freshness_ms));
        const bool physical_usable=usable && !p.occupied && lane.owner.physical();
        if(lane.held && !physical_usable && !lane.pending_release) {lane.pending_release=true;lane.release_throw=false;lane.owner.revoke();}
        const bool same_owner=lane.owner_player==p.owner.player && lane.owner_camera==p.owner.camera &&
            lane.owner_generation==p.owner.camera_identity.generation && lane.owner_epoch==p.epoch;
        const bool sample=lane.held && lane.pending_release?false:lane.grab.sample(p.hand,p.grip,
            usable && (physical_usable || (!lane.held && p.grip<.35f)) && (!lane.held || same_owner),p.epoch,p.tick);
        if(!lane.grab.pressed()) {lane.attempted=lane.acquired=false;if(!lane.owner.physical() && !lane.held) lane.candidates.clear();}
        if(lane.held) {
            const auto body=native_rigid_body(read_,base_,lane.held->entity);
            const auto& h=*lane.held;
            if(!body || !same_body(*body,h) || body->world!=world) {
                std::ofstream loss(loss_file_,std::ios::app);
                loss<<"tick="<<now<<" side="<<side<<" entity="<<h.entity<<" component="<<h.component<<" eligible="<<body.has_value()<<" world="<<world<<" original_world="<<h.world<<'\n';
                lane.grab.release(false);lane.held.reset();lane.pending_release=false;lane.owner.revoke();
                lane.obstruction.reset();lane.focus.reset();lane.focus_tick=0;lane.candidates.clear();lane.discovery.reset();++lost_;return;
            }
            const auto motor=lane.pending_release?std::optional<GrabMotor>{lane.grab.release_sample(p.hand,p.grip,usable && same_owner,p.epoch,p.tick,lane.release_throw)}:
                lane.grab.motor(body->pose,physical_usable && sample && same_owner);
            if(motor && (motor->release || now-lane.last_motor>=10)) {
                write(lane,side,*body,*motor);lane.last_motor=now;
                if(motor->release) {lane.held.reset();lane.pending_release=false;}
            }
        } else if(sample && physical_usable && lane.grab.pressed() && !lane.acquired && (!lane.attempted || now-lane.last_acquisition>=100)) {
            lane.last_acquisition=now;
            const auto& other=lanes_[1-side].held;
            std::optional<NativeRigidBody> body;double distance=2.;
            for(const auto& cached:lane.candidates) {
                if(other && other->entity==cached.entity) continue;
                const auto current=native_rigid_body(read_,base_,cached.entity);
                if(!current || current->identity!=cached.identity || current->component!=cached.component ||
                   current->handle!=cached.handle || current->model!=cached.model || current->world!=cached.world || current->entity_id!=cached.entity_id) continue;
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
            if(body && body->world==world && cleared(lane,*body,now) && lane.grab.bind(body->pose,body->contact(p.hand.position),grab_acquire_distance)) {
                lane.held=body;lane.acquired=true;lane.owner_player=p.owner.player;lane.owner_camera=p.owner.camera;
                lane.owner_generation=p.owner.camera_identity.generation;lane.owner_epoch=p.epoch;++picked_;
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
    void apply_poke() {
        Poke p{};{std::lock_guard<std::mutex> lock(mutex_);p=poke_;poke_={};
            for(const auto& lane:lanes_) if(lane.held && lane.held->entity==p.entity) return;} // a held prop follows its hand
        if(!p.entity || GetTickCount64()-p.tick>100) return;
        const auto body=native_rigid_body(read_,base_,p.entity);if(!body) return;
        alignas(16) std::array<float,4> linear{static_cast<float>(p.velocity.x),static_cast<float>(p.velocity.y),static_cast<float>(p.velocity.z),0};
        alignas(16) std::array<float,4> angular{};
        CrashOperation operation("native blade poke velocity");
        velocity_(reinterpret_cast<void*>(body->world),body->handle,linear.data(),angular.data(),0);++pokes_;
    }
    void invoke(void* manager,void* command) {
        original_(manager,command);
        try {apply_poke();} catch(...) {}
        try {update(reinterpret_cast<std::uintptr_t>(manager));} catch(...) {
            std::lock_guard<std::mutex> lock(mutex_);
            for(auto& lane:lanes_) {lane.grab.release(false);lane.held.reset();lane.pending_release=false;lane.owner.revoke();lane.candidates.clear();}
            ++lost_;
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
    };
    std::optional<std::array<DiagnosticLane,2>> diagnostics(UINT64 now=GetTickCount64()) {
        std::unique_lock<std::mutex> lock(mutex_,std::try_to_lock);if(!lock.owns_lock())return {};
        std::array<DiagnosticLane,2> out{};
        for(unsigned side=0;side<2;++side) {
            const auto& lane=lanes_[side];const auto& p=lane.published;
            out[side]={p.epoch,p.tick,p.serial,lane.last_acquisition,lane.focus_tick,lane.focus_calls,lane.focus_us,lane.focus_max_us,
                p.owner.player,p.owner.camera,lane.focus,lane.held,lane.discovery.diagnostics(),p.usable,p.occupied,lane.pending_release,lane.decision_deferred,
                lane.gate,lane.raw_grip,lane.decision_tick,now>=(p.source_tick?p.source_tick:p.tick)?now-(p.source_tick?p.source_tick:p.tick):0,p.source_tick?p.source_tick:p.tick,lane.owner.role(),static_cast<unsigned>(lane.candidates.size()),
                lane.owner.role()==RightGripOwner::Role::ready,(p.source_tick?p.source_tick:p.tick)!=0 && now>=(p.source_tick?p.source_tick:p.tick)};
        }
        return out;
    }
    std::optional<Decision> pop_decision() {
        std::unique_lock<std::mutex> lock(mutex_,std::try_to_lock);if(!lock.owns_lock() || !decision_count_) return {};
        const auto record=decisions_[decision_begin_];decision_begin_=(decision_begin_+1)%decisions_.size();--decision_count_;return record;
    }
    UINT64 decision_dropped() const {return decision_dropped_.load();}
    void publish(unsigned side,const NativePlayerCamera& owner,const WorldTrackedPose& hand,float grip,UINT64 epoch,UINT64 tick,bool usable,bool occupied=false,UINT64 source_tick=0) {
        std::lock_guard<std::mutex> lock(mutex_);auto& lane=lanes_.at(side);auto& p=lane.published;
        if(p.epoch!=epoch || p.owner.player!=owner.player || p.owner.camera!=owner.camera ||
           p.owner.camera_identity!=owner.camera_identity || !usable || occupied) {lane.focus.reset();lane.focus_tick=0;lane.discovery.reset();}
        if(p.tick!=tick || p.source_tick!=source_tick || p.epoch!=epoch || p.owner.camera_identity!=owner.camera_identity || !usable || occupied) lane.obstruction.reset();
        p={owner,hand,grip,epoch,tick,p.serial+1,usable,occupied,source_tick};
    }
    void clear() {std::lock_guard<std::mutex> lock(mutex_);for(auto& lane:lanes_) {lane.published.usable=false;lane.obstruction.reset();++lane.published.serial;lane.focus.reset();lane.focus_tick=0;}}
    bool holding(unsigned side) {std::lock_guard<std::mutex> lock(mutex_);return lanes_.at(side).held.has_value();}
    void poke(std::uintptr_t entity,Vec3 velocity,UINT64 now) {std::lock_guard<std::mutex> lock(mutex_);poke_={entity,velocity,now};}
    std::atomic<UINT64> pokes_{};
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
            lane.owner={};
            if(lane.held) {lane.pending_release=true;lane.release_throw=false;}
            lane.grip_epoch=epoch;lane.grip_player=p.owner.player;lane.grip_camera=p.owner.camera;lane.grip_generation=p.owner.camera_identity.generation;
        }
        if(p.occupied) {
            lane.gate=Gate::occupied;
            if(lane.held && !lane.pending_release) {lane.pending_release=true;lane.release_throw=true;} // item took the hand: throw with hand velocity, never a dead drop
            const float native=lane.owner.filter(raw,focused,false,true);
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
        if(!usable) {lane.owner.revoke();lane.obstruction.reset();}
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
        const float native=lane.owner.filter(raw,focused,eligible);
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
            const auto body=native_rigid_body(read_,base_,obstruction_request->body.entity);
            if(!owner || owner->authored_view() || owner->camera_identity!=obstruction_request->owner.camera_identity ||
               !body || !same_body(*body,obstruction_request->body) || logic_()!=2 ||
               length(body->contact(obstruction_request->hand.position)-obstruction_request->body.contact(obstruction_request->hand.position))>=.001)continue;
            {std::lock_guard<std::mutex> lock(mutex_);const auto& lane=lanes_[side];
                if(!active_ || !lane.obstruction || lane.obstruction->request.token!=obstruction_request->token ||
                   !request_current(*obstruction_request,lane.published,GetTickCount64()))continue;
            }
            const auto verdict=query(*obstruction_request);
            const auto after=native_rigid_body(read_,base_,obstruction_request->body.entity);
            const auto after_owner=native_player_camera(read_,base_,obstruction_request->owner.camera);
            std::lock_guard<std::mutex> lock(mutex_);auto& lane=lanes_[side];auto& p=lane.published;
            if(!active_ || !lane.obstruction || lane.obstruction->request.token!=obstruction_request->token ||
               !request_current(*obstruction_request,p,GetTickCount64()) || p.grip<.35f || p.occupied || lane.held ||
               !after || !same_body(*after,obstruction_request->body) || !after_owner || after_owner->authored_view() ||
               after_owner->camera_identity!=obstruction_request->owner.camera_identity || logic_()!=2 ||
               length(after->contact(obstruction_request->hand.position)-obstruction_request->body.contact(obstruction_request->hand.position))>=.001 || !verdict)continue;
            lane.obstruction->clear=*verdict;lane.decision_deferred=false;
            if(lane.owner.deciding(p.grip,true)) {
                lane.owner.filter(p.grip,true,*verdict);lane.attempted=*verdict;lane.acquired=false;
                if(*verdict)++physical_presses_;else ++native_presses_;
            }
            if(!*verdict)lane.owner.revoke();
            // Binding is controller math and internal ownership only. No native
            // write runs here: 0x89fb0 remains the sole velocity/motor caller.
            // Commit the queried pose before another eye publication replaces it.
            const auto& other=lanes_[1-side].held;
            if(*verdict && lane.owner.physical() && (!other || other->entity!=after->entity) &&
               lane.grab.sample(p.hand,p.grip,true,p.epoch,p.tick) &&
               lane.grab.bind(after->pose,after->contact(p.hand.position),grab_acquire_distance)) {
                lane.held=after;lane.acquired=true;lane.pending_release=false;
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
        acquisitions_<<std::setprecision(9)<<"tick_ms,side,hand_x,hand_y,hand_z,candidates,nearest_entity_id,contact_distance\n";
        log_<<std::setprecision(9)<<"tick_ms,side,entity_id,component,generation,handle,body_flags,release,x,y,z,target_x,target_y,target_z,vx,vy,vz,wx,wy,wz\n";
        lanes_={};decision_begin_=decision_count_=0;last_decision_={};decision_dropped_=0;acquisition_samples_=0;picked_=moved_=dropped_=lost_=queries_=samples_=callbacks_=0;native_presses_=physical_presses_=0;
        velocity_=reinterpret_cast<Velocity>(base+velocity_rva);instance_=this;
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
        try {std::lock_guard<std::mutex> lock(mutex_);for(auto& lane:lanes_) {lane.grab.release(false);lane.held.reset();}log_.close();acquisitions_.close();} catch(...) {return false;}
        return restored;
    }
    UINT64 picked() const {return picked_.load();}UINT64 moved() const {return moved_.load();}UINT64 dropped() const {return dropped_.load();}
    UINT64 lost() const {return lost_.load();}UINT64 queries() const {return queries_.load();}UINT64 callbacks() const {return callbacks_.load();}
    UINT64 native_presses() const {return native_presses_.load();}UINT64 physical_presses() const {return physical_presses_.load();}
};
} // namespace d2vr
