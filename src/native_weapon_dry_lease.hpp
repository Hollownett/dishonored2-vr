#pragma once
#include "native_weapon_source_receipts.hpp"
#include "native_weapon_attachment_candidate.hpp"
#include "native_equipment_ownership.hpp"
#include <mutex>

namespace d2vr {
// A dry hypothetical boundary only. No native/GPU output or source is changed.
class NativeWeaponDryLease {
public:
    enum Site : unsigned {primary=1,secondary=2,outer_exit=4,gpu=5};
    enum Reason : unsigned {okay=0,absent=1,ineligible=2,source_changed=3,model_changed=4,
        binding_changed=5,tracking_changed=6,duplicate=7,missing_consumer=8,solve_failed=9,
        equipment_absent=10,equipment_changed=11,equipment_link_mismatch=12};
    // SIMD palette alignment deliberately pads this owned value type.
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4324)
#endif
    struct Lease {
        UINT64 token{},receipt{},epoch{};NativeAttachmentCandidate candidate{};
        NativeWeaponSourceContent source{};NativeWeaponModelBase model{};rig::ArmTrackingSnapshot tracking{};
        std::shared_ptr<const rig::NativeArmBindingSnapshot> rig;alignas(16) rig::PosedArm posed{};UINT64 posed_digest{};
        std::shared_ptr<const NativeEquipmentOwnership> equipment;
    };
#ifdef _MSC_VER
#pragma warning(pop)
#endif
    static_assert(sizeof(rig::Transform)==64 && sizeof(rig::PosedArm::joints)==69*64 && alignof(Lease)>=16);
    struct Result {UINT64 token{},receipt{},revision{},posed_digest{};std::uint32_t frame{};unsigned side{},site{},reason{absent};
        bool accepted{},strict_revision{},after_primary{},complete{},secondary_observed{};rig::Transform predicted_socket{};
        std::uintptr_t equipment{},equipment_declaration{},equipment_item{};std::int32_t equipment_generation{};bool typed_equipment_verified{};};
private:
    struct State {std::shared_ptr<const Lease> lease;bool primary_seen{},secondary_seen{},exit_seen{},gpu_seen{},failed{};};
    std::mutex mutex_;std::array<State,2> states_{};UINT64 serial_{};
    std::atomic<UINT64> prepared_{},completed_{},before_failures_{},after_failures_{},revoked_{};
    std::atomic<UINT64> secondary_matches_{};
    static void equipment_fields(Result& result,const std::shared_ptr<const NativeEquipmentOwnership>& equipment) {
        if(!equipment) return;
        result.equipment=equipment->equipment;result.equipment_declaration=equipment->equipment_declaration;
        result.equipment_item=equipment->item;result.equipment_generation=equipment->equipment_identity.generation;
        result.typed_equipment_verified=result.accepted;
    }
    Result fail(State& state,unsigned side,unsigned site,unsigned reason) {
        Result result{};result.side=side;result.site=site;result.reason=reason;result.after_primary=state.primary_seen;
        result.secondary_observed=state.secondary_seen;
        if(state.lease) {result.token=state.lease->token;result.receipt=state.lease->receipt;
            result.frame=state.lease->source.frame;result.revision=state.lease->tracking.revision;result.posed_digest=state.lease->posed_digest;
            equipment_fields(result,state.lease->equipment);}
        if(!state.failed) {if(state.primary_seen) ++after_failures_;else ++before_failures_;state.failed=true;}
        return result;
    }
public:
    template<class Compute> Result prepare(std::shared_ptr<const rig::NativeArmBindingSnapshot> rig,
        const NativeWeaponSourceContent& source,const NativeWeaponModelBase& model,
        rig::ArmTrackingSnapshot tracking,UINT64 receipt,UINT64 epoch,NativeAttachmentCandidate candidate,UINT64 tick,bool eligible,Compute&& compute,
        std::shared_ptr<const NativeEquipmentOwnership> equipment={}) {
        const auto side=source.arm.side;const auto joint=static_cast<unsigned>(candidate.joint);Result rejected{};rejected.side=side;rejected.reason=ineligible;
        if(!equipment || equipment->guards.empty()) {rejected.reason=equipment_absent;return rejected;}
        equipment_fields(rejected,equipment);
        if(!same_attachment_candidate(equipment->attachment,candidate)) {rejected.reason=equipment_link_mismatch;return rejected;}
        if(side>1 || !rig || !eligible || !receipt || !epoch || !model.valid || model.model!=source.arm.model ||
           model.game!=source.game || model.frame!=source.frame || !(rig->arms[side]==source.arm) || joint>=69 ||
           !(candidate.arm==source.arm) || !candidate.binding || !tracking.revision || !tracking.calibration || !(tracking.frame.hands&(1u<<side)) ||
           tracking.frame.epoch!=epoch || !tracking.frame.has_head_anchor ||
           tracking.frame.reference_epoch!=tracking.calibration->reference_epoch || tick<tracking.frame.tick || tick-tracking.frame.tick>250)
            return rejected;
        rig::Upload input{source.joints,model.values,source.remap[0]};
        std::optional<rig::PosedArm> posed;
        try {posed=compute(input,rig->bindings[side],side,tick,tracking);} catch(...) {}
        if(!posed) {rejected.reason=solve_failed;return rejected;}
        auto lease=std::make_shared<Lease>();lease->receipt=receipt;lease->epoch=epoch;lease->candidate=candidate;lease->source=source;lease->model=model;
        lease->tracking=std::move(tracking);lease->rig=std::move(rig);lease->posed=*posed;lease->equipment=std::move(equipment);
        lease->posed_digest=14695981039346656037ull; // Diagnostic only; qualification always uses full owned values.
        const auto* bytes=reinterpret_cast<const unsigned char*>(lease->posed.joints.data());
        for(std::size_t i=0;i<sizeof(lease->posed.joints);++i) {lease->posed_digest^=bytes[i];lease->posed_digest*=1099511628211ull;}
        std::lock_guard<std::mutex> lock(mutex_);auto& state=states_[side];
        if(state.lease && state.lease->receipt==receipt && state.lease->source.frame==source.frame)
            return fail(state,side,0,duplicate);
        if(state.lease && !state.failed && !state.gpu_seen) return fail(state,side,0,missing_consumer);
        lease->token=++serial_;state={lease};++prepared_;
        Result result{};result.token=lease->token;result.receipt=receipt;result.revision=lease->tracking.revision;
        result.frame=source.frame;result.side=side;result.reason=okay;result.accepted=true;result.strict_revision=true;
        result.posed_digest=lease->posed_digest;
        result.predicted_socket=lease->posed.joints[joint];equipment_fields(result,lease->equipment);return result;
    }
    Result observe(unsigned side,unsigned site,const NativeWeaponSourceContent& source,const NativeWeaponModelBase& model,
        const rig::ArmTrackingStamp& tracking,std::shared_ptr<const rig::NativeArmBindingSnapshot> rig,
        UINT64 receipt,UINT64 epoch,const std::optional<NativeAttachmentCandidate>& candidate,bool eligible,UINT64 tick,
        std::shared_ptr<const NativeEquipmentOwnership> equipment={}) {
        std::lock_guard<std::mutex> lock(mutex_);Result empty{};empty.side=side;empty.site=site;
        if(side>1 || !states_[side].lease) return empty;
        auto& state=states_[side];const auto& lease=*state.lease;
        if(state.gpu_seen && lease.receipt!=receipt) return empty;
        if(state.failed) return fail(state,side,site,absent);
        if(!equipment) return fail(state,side,site,equipment_absent);
        if(equipment!=lease.equipment) return fail(state,side,site,equipment_changed);
        if(!candidate || !same_attachment_candidate(equipment->attachment,*candidate)) return fail(state,side,site,equipment_link_mismatch);
        if(!eligible || tick<lease.tracking.frame.tick || tick-lease.tracking.frame.tick>250 ||
           tick<tracking.tick || tick-tracking.tick>250) return fail(state,side,site,ineligible);
        if(lease.rig!=rig || lease.epoch!=epoch || lease.receipt!=receipt || !same_weapon_source(lease.source,source))
            return fail(state,side,site,source_changed);
        if(!same_weapon_model_base(lease.model,model)) return fail(state,side,site,model_changed);
        if(!candidate || !same_attachment_candidate(lease.candidate,*candidate))
            return fail(state,side,site,binding_changed);
        if(tracking.epoch!=lease.tracking.frame.epoch || tracking.reference_epoch!=lease.tracking.frame.reference_epoch ||
           tracking.mapping_revision!=lease.tracking.mapping_revision || !(tracking.hands&(1u<<side)) || !tracking.calibrated)
            return fail(state,side,site,tracking_changed);
        if((site==primary && state.primary_seen) || (site==secondary && (!state.primary_seen || state.secondary_seen)) ||
           (site==outer_exit && (!state.primary_seen || state.exit_seen)) || (site==gpu && (!state.exit_seen || state.gpu_seen)))
            return fail(state,side,site,duplicate);
        if(site==primary) state.primary_seen=true;else if(site==secondary) {state.secondary_seen=true;++secondary_matches_;}
        else if(site==outer_exit) state.exit_seen=true;else if(site==gpu) {state.gpu_seen=true;++completed_;}
        else return fail(state,side,site,ineligible);
        Result result{};result.token=lease.token;result.receipt=receipt;result.revision=lease.tracking.revision;
        result.frame=source.frame;result.side=side;result.site=site;result.reason=okay;result.accepted=true;
        result.strict_revision=tracking.revision==lease.tracking.revision;result.after_primary=state.primary_seen;
        result.posed_digest=lease.posed_digest;
        result.complete=state.gpu_seen;result.secondary_observed=state.secondary_seen;result.predicted_socket=lease.posed.joints[static_cast<unsigned>(lease.candidate.joint)];
        equipment_fields(result,lease.equipment);return result;
    }
    std::optional<NativeAttachmentCandidate> candidate(unsigned side) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(side>1 || !states_[side].lease || states_[side].failed) return {};
        return states_[side].lease->candidate;
    }
    std::shared_ptr<const Lease> qualified_input(unsigned side,unsigned site) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(side!=1 || !states_[side].lease || states_[side].failed) return {};
        const auto& state=states_[side];
        if((site==primary && !state.primary_seen) || (site==gpu && (!state.primary_seen || !state.exit_seen || !state.gpu_seen)) ||
           (site!=primary && site!=gpu)) return {};
        return state.lease;
    }
    std::array<Result,2> clear(unsigned reason=ineligible) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::array<Result,2> results{};
        for(unsigned side=0;side<2;++side) {
            auto& state=states_[side];if(state.lease && !state.failed && !state.gpu_seen) {++revoked_;results[side]=fail(state,side,0,reason);}
            state={};
        }
        return results;
    }
    Result clear_side(unsigned side,unsigned reason=ineligible) {
        std::lock_guard<std::mutex> lock(mutex_);Result result{};
        if(side>1) return result;auto& state=states_[side];
        if(state.lease && !state.failed && !state.gpu_seen) {++revoked_;result=fail(state,side,0,reason);}
        state={};return result;
    }
    void reset() {clear();prepared_=completed_=before_failures_=after_failures_=revoked_=secondary_matches_=0;}
    UINT64 prepared() const {return prepared_.load();}UINT64 completed() const{return completed_.load();}
    UINT64 before_failures() const{return before_failures_.load();}UINT64 after_failures() const{return after_failures_.load();}
    UINT64 revoked() const{return revoked_.load();}
    UINT64 secondary_matches() const{return secondary_matches_.load();}
};
} // namespace d2vr
