#pragma once
#include <windows.h>
#include <MinHook.h>
#include "native_rigid_body.hpp"
#include "native_player_camera.hpp"
#include "crash_observer.hpp"
#include "physical_grab.hpp"
#include "right_grip_owner.hpp"
#include <atomic>
#include <mutex>
#include <fstream>
#include <filesystem>
#include <iomanip>
namespace d2vr {
class NativeGrab {
    using Read=bool (*)(std::uintptr_t,void*,std::size_t);
    using Logic=UINT (*)();
    using Dispatch=void (__fastcall*)(void*,void*);
    using Velocity=void (__fastcall*)(void*,UINT,const float*,const float*,int);
    struct Publication {NativePlayerCamera owner{};WorldTrackedPose hand{};float grip{};UINT64 epoch{},tick{},serial{};bool usable{};bool occupied{};};
    // One independent lane per hand. Either hand may hold one dynamic body.
    // A hand the game already occupies (weapon/power/carried body) is left to
    // the game: no acquisition, and its raw native squeeze passes through.
    struct Lane {
        Publication published{};PhysicalGrab grab{};std::optional<NativeRigidBody> held;RightGripOwner owner{};
        std::vector<NativeRigidBody> candidates;
        bool pending_release{},release_throw{},attempted{},acquired{};
        UINT64 consumed{},last_motor{},last_acquisition{},owner_epoch{},grip_epoch{};
        std::uintptr_t owner_player{},owner_camera{},grip_player{},grip_camera{};int owner_generation{},grip_generation{};
    };
    inline static NativeGrab* instance_{};
    inline static constexpr std::uintptr_t rva=0x89fb0,velocity_rva=0xc85dc0;
    inline static constexpr std::array<unsigned char,32> dispatch_code{0x48,0x83,0xec,0x68,0x48,0xc7,0x44,0x24,0x30,0xfe,0xff,0xff,0xff,0x4c,0x8b,0xd2,0x0f,0xb7,0x42,0x04,0x83,0xf8,0x3e,0x0f,0x87,0xf3,0x05,0x00,0x00,0x48,0x8d,0x15};
    inline static constexpr std::array<unsigned char,32> velocity_code{0x48,0x89,0x6c,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x89,0x54,0x24,0x10,0x57,0x48,0x83,0xec,0x40,0x44,0x8b,0xd2,0x49,0x8b,0xe9,0x49,0x8b,0xf0,0x41,0x81,0xe2,0xff};
    Dispatch original_{};Velocity velocity_{};Read read_{};Logic logic_{};std::uintptr_t base_{};
    bool enabled_{};std::atomic<bool> active_{};std::atomic<unsigned> depth_{};
    UINT64 acquisition_samples_{};std::ofstream acquisitions_;
    std::mutex mutex_;std::array<Lane,2> lanes_{};
    std::atomic<UINT64> native_presses_{},physical_presses_{};
    std::ofstream log_;std::filesystem::path loss_file_;std::atomic<UINT64> picked_{},moved_{},dropped_{},lost_{},queries_{},samples_{},callbacks_{};
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
        const auto owner=native_player_camera(read_,base_,p.owner.camera);
        const bool usable=active_ && p.usable && owner && owner->player==p.owner.player && owner->camera_identity==p.owner.camera_identity &&
            logic_()==2 && !owner->authored_view() && now>=p.tick && now-p.tick<=100 && p.epoch;
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
            if(!body || body->component!=h.component || body->identity.generation!=h.identity.generation || body->handle!=h.handle || body->model!=h.model || body->world!=world) {
                std::ofstream loss(loss_file_,std::ios::app);
                loss<<"tick="<<now<<" side="<<side<<" entity="<<h.entity<<" component="<<h.component<<" eligible="<<body.has_value()<<" world="<<world<<" original_world="<<h.world<<'\n';
                lane.grab.release(false);lane.held.reset();lane.pending_release=false;++lost_;return;
            }
            const auto motor=lane.pending_release?std::optional<GrabMotor>{lane.grab.release(lane.release_throw && usable && same_owner)}:
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
                   current->handle!=cached.handle || current->model!=cached.model) continue;
                const auto d=current->contact_distance(p.hand.position);
                if(d<distance) {distance=d;body=current;}
            }
            if(acquisition_samples_++<1000) {
                acquisitions_<<now<<','<<side<<','<<p.hand.position.x<<','<<p.hand.position.y<<','<<p.hand.position.z<<','
                    <<lane.candidates.size()<<','<<(body?body->entity_id:0)<<','<<distance<<'\n';acquisitions_.flush();
            }
            if(body && body->world==world && lane.grab.bind(body->pose,body->contact(p.hand.position))) {
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
    void invoke(void* manager,void* command) {
        original_(manager,command);
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
    void publish(unsigned side,const NativePlayerCamera& owner,const WorldTrackedPose& hand,float grip,UINT64 epoch,UINT64 tick,bool usable,bool occupied=false) {
        std::lock_guard<std::mutex> lock(mutex_);auto& p=lanes_.at(side).published;p={owner,hand,grip,epoch,tick,p.serial+1,usable,occupied};
    }
    void clear() {std::lock_guard<std::mutex> lock(mutex_);for(auto& lane:lanes_) {lane.published.usable=false;++lane.published.serial;}}
    bool holding(unsigned side) {std::lock_guard<std::mutex> lock(mutex_);return lanes_.at(side).held.has_value();}
    // Preview reads only. Reserving at .35 prevents RB's approximately .551
    // threshold from preceding the native motor's .65 acquisition threshold.
    // Returns the squeeze left for the native button (0 while the grip grabs).
    // An occupied hand (native weapon/power/carried body) always gets its raw
    // squeeze back: the physical role is revoked and never re-reserved.
    float filter_native_grip(unsigned side,float raw,UINT64 epoch,UINT64 now,bool gameplay,bool focused,bool use) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& lane=lanes_.at(side);auto& p=lane.published;
        if(epoch!=lane.grip_epoch || p.owner.player!=lane.grip_player || p.owner.camera!=lane.grip_camera ||
           p.owner.camera_identity.generation!=lane.grip_generation) {
            lane.owner={};
            if(lane.held) {lane.pending_release=true;lane.release_throw=false;}
            lane.grip_epoch=epoch;lane.grip_player=p.owner.player;lane.grip_camera=p.owner.camera;lane.grip_generation=p.owner.camera_identity.generation;
        }
        if(p.occupied) {
            if(lane.held && !lane.pending_release) {lane.pending_release=true;lane.release_throw=false;}
            const float native=lane.owner.filter(raw,focused,false,true);
            p.grip=raw;++p.serial;return native;
        }
        const auto owner=native_player_camera(read_,base_,p.owner.camera);
        const bool usable=active_ && p.usable && owner && owner->player==p.owner.player &&
            owner->camera_identity==p.owner.camera_identity && epoch==p.epoch && now>=p.tick && now-p.tick<=100 &&
            gameplay && focused && !use && !owner->authored_view();
        if(lane.held && !lane.pending_release && (raw<.35f || !usable)) {
            lane.pending_release=true;lane.release_throw=raw<.35f && usable;
        }
        if(!usable) lane.owner.revoke();
        bool eligible=false;
        const bool deciding=lane.owner.deciding(raw,focused);
        if(deciding && usable && !lane.pending_release) {
            // Generous reach (0.9 m): a near miss must not fall through to a native grip/attack action.
            constexpr double reach=.9;
            ++queries_;const auto candidate=nearest_grab_body(read_,base_,p.hand.position,reach);
            lane.candidates.clear();
            if(candidate && candidate->contact_distance(p.hand.position)<=reach) {lane.candidates.push_back(*candidate);eligible=true;}
        }
        const float native=lane.owner.filter(raw,focused,eligible);
        if(deciding) {if(lane.owner.physical()) ++physical_presses_;else ++native_presses_;}
        if(deciding && lane.owner.physical()) {lane.attempted=true;lane.acquired=false;lane.pending_release=false;}
        // Arm only after a real observed release. This is controller math;
        // the renderer does not bind bodies or call the velocity ABI.
        if(!lane.held && usable && raw<.35f) lane.grab.sample(p.hand,raw,true,p.epoch,p.tick);
        p.grip=raw;++p.serial;
        return native;
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
        lanes_={};acquisition_samples_=0;picked_=moved_=dropped_=lost_=queries_=samples_=callbacks_=0;native_presses_=physical_presses_=0;
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
