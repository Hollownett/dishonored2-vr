#pragma once
#include "native_carry_snapshot.hpp"
#include <limits>
#include <mutex>
#include <atomic>
#include <windows.h>

namespace d2vr {
class NativeHeldPlacement {
public:
    using Read=bool (*)(std::uintptr_t,void*,std::size_t);
    using Serial=std::uint64_t;
    struct Proof {
        rig::NativeCarrySnapshot carry;
        std::uintptr_t entity{},rf{},component_slot{};
        rig::List components{};rig::Identity rf_identity{};
        std::uint32_t spawn{};unsigned side{2};std::uint64_t epoch{},pose_tick{};
    };
    static NativeHeldPlacement& process_owner(){static auto* owner=new NativeHeldPlacement;return *owner;}
    bool install(std::uintptr_t base,Read read) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(!base || !read || failed_ || installed_ || (base_ && (base_!=base || read_!=read)))return false;
        base_=base;read_=read;installed_=true;focused_=false;epoch_=0;clear_locked();return true;
    }
    void disable()noexcept {
        try{std::lock_guard<std::mutex> lock(mutex_);installed_=focused_=false;clear_locked();}catch(...){failed_=true;}
    }
    void context(std::uint64_t epoch,bool focused)noexcept {try{context_cpp(epoch,focused);}catch(...){failed_=true;}}
    void clear()noexcept {try{std::lock_guard<std::mutex> lock(mutex_);clear_locked();}catch(...){failed_=true;}}
private:
    void context_cpp(std::uint64_t epoch,bool focused) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(epoch_!=epoch || !focused)clear_locked();epoch_=epoch;focused_=focused && epoch!=0;
    }
public:
    static std::optional<Proof> qualify(std::uintptr_t base,Read read,const rig::NativeCarrySnapshot& carry,
            unsigned side,std::uint64_t epoch,std::uint64_t pose_tick,std::uint64_t now)noexcept {
        if(side>1 || !epoch || !fresh(pose_tick,now))return {};
        Reader reader{read};const auto held=rig::native_carry_current(reader,base,carry);
        if(!held || !held->hold_state() || held->active[1])return {};
        const auto spawn=rig::value<std::uint32_t>(reader,held->hold_state()+0x68);
        if(!spawn || !*spawn || *spawn==0x3fff0010 || (*spawn>>16)>=0x3fff)return {};
        const auto entity=rig::value<std::uintptr_t>(reader,held->game+0x3130b0+(*spawn>>16)*8ull);
        const auto rf=entity?rig::value<std::uintptr_t>(reader,*entity+0x358):std::nullopt;
        const auto components=entity?rig::list(reader,*entity+0x268,4096):std::nullopt;
        const auto identity=rf?rig::started(reader,*rf):std::nullopt;
        if(!entity || !rf || !components || !identity)return {};
        std::uintptr_t slot{};
        for(unsigned index=0;index<components->packed_count;++index){
            const auto at=components->data+index*8ull;const auto component=rig::value<std::uintptr_t>(reader,at);
            if(!component)return {};if(*component==*rf){if(slot)return {};slot=at;}
        }
        Proof result{*held,*entity,*rf,slot,*components,*identity,*spawn,side,epoch,pose_tick};
        return valid(base,read,result,now)?std::optional<Proof>{result}:std::nullopt;
    }
    Serial publish(const Proof& proof,std::uint64_t now) {
        std::uintptr_t base{};Read read{};Serial revision{};
        {std::lock_guard<std::mutex> lock(mutex_);base=base_;read=read_;revision=next_;}
        if(!valid(base,read,proof,now)){
            std::lock_guard<std::mutex> lock(mutex_);if(next_==revision)clear_locked();return 0;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        if(!installed_ || !focused_ || failed_ || epoch_!=proof.epoch || exhausted_ || next_!=revision)return 0;
        if(serial_ && same_identity(proof_,proof)) {
            if(proof.pose_tick<proof_.pose_tick)return 0;
            proof_=proof;return serial_;
        }
        if(next_==(std::numeric_limits<Serial>::max)()){exhausted_=true;clear_locked();return 0;}
        serial_=++next_;proof_=proof;ack_=0;return serial_;
    }
    bool current(Serial serial,unsigned side,std::uintptr_t entity,std::uintptr_t rf,std::uint64_t epoch,std::uint64_t now)const noexcept {
        try{return current_cpp(serial,side,entity,rf,epoch,now);}catch(...){failed_=true;return false;}
    }
    bool completed(Serial serial,unsigned side,std::uintptr_t entity,std::uintptr_t rf,std::uint64_t epoch,std::uint64_t now)noexcept {
        try{return completed_cpp(serial,side,entity,rf,epoch,now);}catch(...){failed_=true;return false;}
    }
    std::optional<unsigned> current_driven_side(const rig::NativeCarrySnapshot& carry,std::uint64_t epoch,std::uint64_t now)const noexcept {
        try{return current_driven_side_cpp(carry,epoch,now);}catch(...){failed_=true;return {};}
    }
    bool current_driven_right(Serial serial,std::uint64_t epoch,const rig::NativeCarrySnapshot& carry,std::uint64_t now)const noexcept {
        try{return current_driven_right_cpp(serial,epoch,carry,now);}catch(...){failed_=true;return false;}
    }
private:
    bool current_cpp(Serial serial,unsigned side,std::uintptr_t entity,std::uintptr_t rf,std::uint64_t epoch,std::uint64_t now)const {
        const auto value=snapshot(serial,side,entity,rf,epoch);if(!value || !valid(value->base,value->read,value->proof,now))return false;
        std::lock_guard<std::mutex> lock(mutex_);return matches_locked(serial,side,entity,rf,epoch);
    }
    bool completed_cpp(Serial serial,unsigned side,std::uintptr_t entity,std::uintptr_t rf,std::uint64_t epoch,std::uint64_t now) {
        const auto value=snapshot(serial,side,entity,rf,epoch);if(!value || !valid(value->base,value->read,value->proof,now))return false;
        std::lock_guard<std::mutex> lock(mutex_);if(!matches_locked(serial,side,entity,rf,epoch))return false;ack_=now;return true;
    }
    std::optional<unsigned> current_driven_side_cpp(const rig::NativeCarrySnapshot& carry,std::uint64_t epoch,std::uint64_t now)const {
        Snapshot value{};std::uint64_t ack{};
        {std::lock_guard<std::mutex> lock(mutex_);
            if(!installed_ || !focused_ || failed_ || epoch_!=epoch || !serial_ || !fresh(ack_,now))return {};
            value={base_,read_,proof_,serial_};ack=ack_;
        }
        if(carry!=value.proof.carry || !valid(value.base,value.read,value.proof,now))return {};
        std::lock_guard<std::mutex> lock(mutex_);
        if(ack_!=ack || !matches_locked(value.serial,value.proof.side,value.proof.entity,value.proof.rf,epoch))return {};
        return value.proof.side;
    }
    bool current_driven_right_cpp(Serial serial,std::uint64_t epoch,const rig::NativeCarrySnapshot& carry,std::uint64_t now)const {
        if(!carry.active[0] || carry.active[1] || carry.active[2])return false;
        {std::lock_guard<std::mutex> lock(mutex_);if(!serial || serial_!=serial)return false;}
        const auto side=current_driven_side(carry,epoch,now);if(!side || *side!=1)return false;
        std::lock_guard<std::mutex> lock(mutex_);return serial_==serial;
    }
private:
    struct Reader {
        Read read{};
        static bool invoke(Read read,std::uintptr_t at,void* output,std::size_t size)noexcept {try{return read && read(at,output,size);}catch(...){return false;}}
        bool operator()(std::uintptr_t at,void* output,std::size_t size)const noexcept {
            __try{return invoke(read,at,output,size);}__except(EXCEPTION_EXECUTE_HANDLER){return false;}
        }
    };
    struct Snapshot {std::uintptr_t base{};Read read{};Proof proof;Serial serial{};};
    static bool fresh(std::uint64_t tick,std::uint64_t now)noexcept{return tick && now>=tick && now-tick<=150;}
    static bool valid(std::uintptr_t base,Read read,const Proof& proof,std::uint64_t now)noexcept {
        if(!base || !read || proof.side>1 || !proof.epoch || !fresh(proof.pose_tick,now) || !proof.carry.hold_state() ||
           proof.carry.active[1] || !proof.spawn || proof.spawn==0x3fff0010 || (proof.spawn>>16)>=0x3fff ||
           !proof.component_slot || proof.component_slot<proof.components.data ||
           proof.component_slot-proof.components.data>=proof.components.packed_count*8ull || (proof.component_slot-proof.components.data)%8)return false;
        Reader reader{read};
        for(unsigned pass=0;pass<2;++pass){
            const auto carry=rig::native_carry_current(reader,base,proof.carry);
            const auto components=rig::list(reader,proof.entity+0x268,4096);
            if(!carry || *carry!=proof.carry || !components || components->data!=proof.components.data ||
               components->packed_count!=proof.components.packed_count || components->packed_capacity!=proof.components.packed_capacity ||
               rig::value<std::uint32_t>(reader,proof.carry.hold_state()+0x68)!=proof.spawn ||
               rig::value<std::uintptr_t>(reader,proof.carry.game+0x3130b0+(proof.spawn>>16)*8ull)!=proof.entity ||
               rig::value<std::uint32_t>(reader,proof.entity+0xa8)!=proof.spawn || rig::value<std::uintptr_t>(reader,proof.entity+0x358)!=proof.rf ||
               !rig::typed(reader,base,proof.rf,0x1c573e8,0x1c574c0) || rig::started(reader,proof.rf)!=proof.rf_identity ||
               rig::value<std::uintptr_t>(reader,proof.rf+0x10)!=proof.entity || rig::value<std::uintptr_t>(reader,proof.component_slot)!=proof.rf)return false;
        }
        return true;
    }
    std::optional<Snapshot> snapshot(Serial serial,unsigned side,std::uintptr_t entity,std::uintptr_t rf,std::uint64_t epoch)const {
        std::lock_guard<std::mutex> lock(mutex_);if(!matches_locked(serial,side,entity,rf,epoch))return {};
        return Snapshot{base_,read_,proof_,serial_};
    }
    bool matches_locked(Serial serial,unsigned side,std::uintptr_t entity,std::uintptr_t rf,std::uint64_t epoch)const {
        return installed_ && focused_ && !failed_ && serial && serial_==serial && epoch_==epoch && proof_.epoch==epoch &&
            proof_.side==side && proof_.entity==entity && proof_.rf==rf;
    }
    static bool same_identity(const Proof& first,const Proof& second)noexcept {
        return first.carry==second.carry && first.entity==second.entity && first.rf==second.rf && first.component_slot==second.component_slot &&
            first.components.data==second.components.data && first.components.packed_count==second.components.packed_count &&
            first.components.packed_capacity==second.components.packed_capacity && first.rf_identity==second.rf_identity &&
            first.spawn==second.spawn && first.side==second.side && first.epoch==second.epoch;
    }
    void clear_locked(){
        serial_=0;ack_=0;proof_={};
        if(next_==(std::numeric_limits<Serial>::max)())exhausted_=true;else ++next_;
    }
    mutable std::mutex mutex_;std::uintptr_t base_{};Read read_{};Proof proof_{};
    Serial next_{},serial_{};std::uint64_t epoch_{},ack_{};bool installed_{},focused_{},exhausted_{};
    mutable std::atomic<bool> failed_{};
};
}
