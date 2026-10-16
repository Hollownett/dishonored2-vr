#pragma once
#include "native_left_weapon_socket.hpp"
#include <mutex>
#include <limits>

namespace d2vr {
class NativeLeftWeaponSocketOwner {
public:
    using Read=bool(*)(void*,std::uintptr_t,void*,std::size_t);
    using Clock=std::uint64_t(*)(void*);
    struct Epochs {
        std::uint64_t pose{},reference{},mapping{};
        bool operator==(const Epochs& other)const noexcept {
            return pose==other.pose && reference==other.reference && mapping==other.mapping;
        }
        bool valid()const noexcept{return pose && reference && mapping;}
    };
    struct Diagnostic {
        std::shared_ptr<const NativeLeftWeaponSocket::Lease> lease;
        std::shared_ptr<const rig::NativeArmBindingSnapshot> binding;
        Epochs epochs{};
        std::uint64_t token{},revision{},tick{};
    };
private:
    struct Publication {
        std::shared_ptr<const NativeLeftWeaponSocket::Lease> lease;
        std::shared_ptr<const rig::NativeArmBindingSnapshot> binding;
        NativeLeftWeaponSocket::Availability availability{};
        Epochs epochs{};
        std::uint64_t token{},revision{};
    };
    std::mutex mutex_;
    Publication publication_{};
    Read read_{};Clock clock_{};void* read_context_{};void* clock_context_{};
    std::uint64_t next_{},revision_{};
    bool exhausted_{};
    static bool same_binding(const rig::NativeArmBindingSnapshot& first,const rig::NativeArmBindingSnapshot& second) {
        if(first.arms!=second.arms || first.sources!=second.sources)return false;
        for(unsigned side=0;side<2;++side) {
            const auto& left=first.bindings[side];const auto& right=second.bindings[side];
            if(left.parents!=right.parents || left.chain.upper!=right.chain.upper || left.chain.elbow!=right.chain.elbow ||
                left.chain.wrist!=right.chain.wrist || left.bind_wrist.f!=right.bind_wrist.f || left.bind_grip.f!=right.bind_grip.f ||
                left.helpers!=right.helpers || left.finger_chains!=right.finger_chains || left.inverse!=right.inverse ||
                left.palm.underwrist!=right.palm.underwrist || left.palm.upperwrist!=right.palm.upperwrist ||
                left.native_camera_to_shoulder_body.has_value()!=right.native_camera_to_shoulder_body.has_value())return false;
            if(left.native_camera_to_shoulder_body && (left.native_camera_to_shoulder_body->x!=right.native_camera_to_shoulder_body->x ||
                left.native_camera_to_shoulder_body->y!=right.native_camera_to_shoulder_body->y ||
                left.native_camera_to_shoulder_body->z!=right.native_camera_to_shoulder_body->z))return false;
            const auto& left_fingers=left.fingers;const auto& right_fingers=right.fingers;
            if(left_fingers.mask!=right_fingers.mask || left_fingers.parents!=right_fingers.parents ||
                left_fingers.native_reference!=right_fingers.native_reference || left_fingers.digit!=right_fingers.digit ||
                left_fingers.bone!=right_fingers.bone)return false;
            for(unsigned joint=0;joint<69;++joint)if(left_fingers.closed[joint].f!=right_fingers.closed[joint].f ||
                left_fingers.open[joint].f!=right_fingers.open[joint].f)return false;
        }
        return true;
    }
    static bool same_equipment(const NativeLeftWeaponSocket::Lease& first,const NativeLeftWeaponSocket::Lease& second) {
        if(first.base!=second.base || first.epoch!=second.epoch ||
            !same_attachment_candidate(first.equipment.attachment,second.equipment.attachment) ||
            first.equipment.guards.size()!=second.equipment.guards.size())return false;
        for(std::size_t index=0;index<first.equipment.guards.size();++index) {
            const auto& left=first.equipment.guards[index];const auto& right=second.equipment.guards[index];
            if(left.address!=right.address || left.bytes!=right.bytes)return false;
        }
        return true;
    }
    static std::uint64_t clock_cpp(Clock clock,void* context)noexcept {
        try{return clock?clock(context):0;}catch(...){return 0;}
    }
    static std::uint64_t clock_safe(Clock clock,void* context)noexcept {
        __try{return clock_cpp(clock,context);}__except(EXCEPTION_EXECUTE_HANDLER){return 0;}
    }
    void retire_locked()noexcept {
        publication_={};
        if(revision_==std::numeric_limits<std::uint64_t>::max())exhausted_=true;
        else ++revision_;
    }
    bool check(const Publication& value,Read read,void* context,Clock clock,void* clock_context) {
        if(!value.token || !value.lease || !value.binding || !value.epochs.valid() || !read || !clock ||
            !(value.lease->equipment.attachment.arm==value.binding->arms[0]))return false;
        const auto now=clock_safe(clock,clock_context);if(!now)return false;
        auto reader=[&](std::uintptr_t address,void* output,std::size_t size){return read(context,address,output,size);};
        return NativeLeftWeaponSocket::current(reader,*value.lease,value.availability,now);
    }
public:
    NativeLeftWeaponSocketOwner()=default;
    NativeLeftWeaponSocketOwner(const NativeLeftWeaponSocketOwner&)=delete;
    NativeLeftWeaponSocketOwner& operator=(const NativeLeftWeaponSocketOwner&)=delete;
    static NativeLeftWeaponSocketOwner& process_owner(){static auto* owner=new NativeLeftWeaponSocketOwner;return *owner;}
    Diagnostic diagnostic() noexcept {
        try {
            std::unique_lock<std::mutex> lock(mutex_,std::try_to_lock);if(!lock.owns_lock())return {};
            return {publication_.lease,publication_.binding,publication_.epochs,
                publication_.token,publication_.revision,publication_.availability.tick};
        }catch(...){return {};}
    }
    void configure(Read read,void* read_context,Clock clock,void* clock_context) {
        std::lock_guard<std::mutex> lock(mutex_);retire_locked();
        read_=read;read_context_=read_context;clock_=clock;clock_context_=clock_context;
    }
    void clear(){std::lock_guard<std::mutex> lock(mutex_);retire_locked();}
    std::uint64_t current_token()noexcept {
        try {
            std::uint64_t token{};
            {std::lock_guard<std::mutex> lock(mutex_);token=publication_.token;}
            return current(token)?token:0;
        }catch(...){return 0;}
    }
    bool current(std::uint64_t token)noexcept {
        try {
            Publication value;Read read;Clock clock;void* context;void* clock_context;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if(!token || publication_.token!=token)return false;
                value=publication_;read=read_;clock=clock_;context=read_context_;clock_context=clock_context_;
            }
            const bool valid=check(value,read,context,clock,clock_context);
            std::lock_guard<std::mutex> lock(mutex_);
            if(publication_.token!=token || publication_.revision!=value.revision)return false;
            if(!valid){retire_locked();return false;}
            return true;
        }catch(...){return false;}
    }
    bool current(std::uint64_t token,const std::shared_ptr<const rig::NativeArmBindingSnapshot>& binding,Epochs epochs)noexcept {
        try {
            std::uint64_t revision;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if(!token || publication_.token!=token || !binding || !(publication_.epochs==epochs) ||
                    (publication_.binding!=binding && !same_binding(*publication_.binding,*binding)))return false;
                revision=publication_.revision;
            }
            if(!current(token))return false;
            std::lock_guard<std::mutex> lock(mutex_);
            return publication_.token==token && publication_.revision==revision && publication_.epochs==epochs &&
                (publication_.binding==binding || (publication_.binding && same_binding(*publication_.binding,*binding)));
        }catch(...){return false;}
    }
    std::uint64_t publish(std::shared_ptr<const NativeLeftWeaponSocket::Lease> lease,
            std::shared_ptr<const rig::NativeArmBindingSnapshot> binding,Epochs epochs,
            NativeLeftWeaponSocket::Availability availability) {
        Publication next;Read read;Clock clock;void* context;void* clock_context;std::uint64_t previous_revision;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if(exhausted_)return 0;
            previous_revision=revision_;read=read_;clock=clock_;context=read_context_;clock_context=clock_context_;
            next={std::move(lease),std::move(binding),availability,epochs,1,0};
        }
        const bool valid=check(next,read,context,clock,clock_context);
        std::lock_guard<std::mutex> lock(mutex_);
        if(previous_revision!=revision_)return 0;
        if(!valid){retire_locked();return 0;}
        if(next_==std::numeric_limits<std::uint64_t>::max() || revision_==std::numeric_limits<std::uint64_t>::max()) {
            exhausted_=true;retire_locked();return 0;
        }
        const bool retain=publication_.token && publication_.lease && publication_.binding &&
            publication_.epochs==next.epochs && (publication_.binding==next.binding || same_binding(*publication_.binding,*next.binding)) &&
            same_equipment(*publication_.lease,*next.lease);
        next.token=retain?publication_.token:++next_;next.revision=++revision_;publication_=std::move(next);return publication_.token;
    }
    std::uint64_t refresh(const std::shared_ptr<const rig::NativeArmBindingSnapshot>& binding,Epochs epochs,
            NativeLeftWeaponSocket::Availability availability) {
        std::uint64_t token;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if(!publication_.token || !binding || (publication_.binding!=binding && !same_binding(*publication_.binding,*binding)) || !(publication_.epochs==epochs) ||
                revision_==std::numeric_limits<std::uint64_t>::max()){retire_locked();return 0;}
            publication_.availability=availability;publication_.revision=++revision_;token=publication_.token;
        }
        return current(token)?token:0;
    }
    static bool acquire(void* context,std::uint64_t token)noexcept {
        return context && static_cast<NativeLeftWeaponSocketOwner*>(context)->current(token);
    }
    static void release(void*,std::uint64_t)noexcept {}
};
}
