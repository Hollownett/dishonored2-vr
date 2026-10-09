#pragma once
#include <windows.h>
#include <MinHook.h>
#include <intrin.h>
#include "native_arm_binding_snapshot.hpp"
#include "native_weapon_source_receipts.hpp"
#include "native_weapon_dry_lease.hpp"
#include "native_weapon_input.hpp"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>

namespace d2vr {
// Default passive. The opt-in experiment supplies private numeric inputs to
// pinned synchronous originals; wrappers never write native output/source bytes.
class NativeWeaponAttachmentTrace {
    using Compose=void (__fastcall*)(void*,int,float*,float*,float*,float*,float);
    using Unpack=void (__fastcall*)(const void*,float*,float*);
    using Outer=void (__fastcall*)(void*);
    using Query=unsigned char (__fastcall*)(void*,int,short,float*,float*);
    using Read=bool (*)(std::uintptr_t,void*,std::size_t);
    using Logic=UINT (*)();
    using Frozen=rig::ArmTrackingSnapshot (*)();using DryEligible=bool (*)();
    struct Publication {std::shared_ptr<const rig::NativeArmBindingSnapshot> arms;UINT64 epoch{},tick{};unsigned hands{};};
    struct Scope {NativeAttachmentCandidate candidate;Publication publication;std::uintptr_t caller{},source{};unsigned leaves{};rig::Transform source_transform{};NativeWeaponSourceMatch receipt{};};
    struct OuterScope {NativeAttachmentCandidate candidate;Publication publication;std::uintptr_t caller{};bool secondary{};};
    struct ModelObservation {UINT64 tick{};DWORD thread{};unsigned site{};std::uintptr_t binding{},caller{};
        NativeWeaponSourceMatch match{};std::uint32_t flags{};std::array<float,7> offset{};bool offset_valid{};rig::Arm arm{};
        UINT64 binding_generation{};std::int16_t joint{};
        std::array<float,15> input_base{};bool has_input_base{},input_base_matched{};};
    struct DryObservation {UINT64 tick{};DWORD thread{};std::uintptr_t caller{},binding{};
        NativeWeaponDryLease::Result result{};};
    inline static NativeWeaponAttachmentTrace* instance_{};
    inline static thread_local Scope* scope_{};
    inline static thread_local OuterScope* outer_scope_{};
    inline static constexpr std::array<std::uintptr_t,4> rvas{0x7506d0,0x9b900,0x753c10,0x53d6a0};
    inline static constexpr std::array<std::array<unsigned char,32>,4> expected{{
        {{0x48,0x8b,0xc4,0x48,0x81,0xec,0x68,0x01,0,0,0x66,0x83,0xb9,0xd0,0x03,0,0,0xff,0x0f,0x29,0x70,0xe8,0x0f,0x29,0x78,0xd8,0x48,0x89,0x68,0x10,0x48,0x89}},
        {{0x0f,0x28,0x09,0x0f,0x28,0x51,0x10,0x0f,0x28,0x59,0x20,0xf3,0x0f,0x11,0x0a,0x0f,0x28,0xc1,0x0f,0xc6,0xc1,0x55,0x0f,0xc6,0xc9,0xaa,0xf3,0x0f,0x11,0x42,0x04,0xf3}},
        {{0x40,0x55,0x53,0x56,0x57,0x48,0x8d,0x6c,0x24,0xc8,0x48,0x81,0xec,0x38,0x01,0,0,0x48,0x8b,0xf9,0x48,0x8b,0x0d,0x65,0x16,0x25,0x03,0x0f,0x29,0xb4,0x24,0x20}},
        {{0x48,0x83,0xec,0x28,0x4c,0x8b,0xd9,0x66,0x45,0x85,0xc0,0x78,0x5f,0x48,0x8b,0x41,0x38,0x4c,0x8b,0x90,0xe8,0x1f,0,0,0x49,0x8b,0x42,0x50,0x44,0x0f,0xb7,0x50}}}};
    Compose original_compose_{};Unpack original_unpack_{};
    Outer original_outer_{};Query original_query_{};
    Read read_{};Logic logic_{};std::uintptr_t base_{};
    std::array<bool,4> enabled_{};std::atomic<bool> active_{false};std::atomic<UINT> depth_{};
    std::atomic<UINT64> calls_{},samples_{},rejected_{},leaves_{},nested_{},errors_{};
    std::mutex mutex_;Publication publication_{};std::ofstream log_;
    NativeWeaponSourceReceipts source_receipts_;
    std::unique_ptr<std::array<ModelObservation,8192>> observations_;std::size_t observation_count_{};
    std::filesystem::path model_path_;std::atomic<UINT64> outer_calls_{},owned_outer_{},secondary_calls_{},model_matches_{},model_mismatches_{};
    std::atomic<UINT64> gpu_observations_{};
    NativeWeaponDryLease dry_;Frozen frozen_{};DryEligible dry_eligible_{};
    std::unique_ptr<std::array<DryObservation,8192>> dry_observations_;std::size_t dry_count_{};
    std::filesystem::path dry_path_;
    std::shared_ptr<const NativeEquipmentOwnership> equipment_;
    std::shared_ptr<const rig::NativeArmBindingSnapshot> equipment_rig_;UINT64 equipment_epoch_{};
    std::atomic<UINT64> equipment_absent_{},equipment_changed_{},equipment_mismatch_{},equipment_refreshes_{};
    struct EquipmentCheck {std::shared_ptr<const NativeEquipmentOwnership> lease;unsigned reason{NativeWeaponDryLease::equipment_absent};};
    struct InputObservation {UINT64 tick{},token{},receipt{};DWORD thread{};std::uint32_t frame{};unsigned site{},reason{};
        std::uintptr_t caller{};bool substituted{},value_changed{},normal_return{},partial_fallback{};};
    std::atomic<bool> input_requested_{false};
    std::shared_ptr<const NativeWeaponDryLease::Lease> input_primary_;
    std::unique_ptr<std::array<InputObservation,8192>> input_observations_;std::size_t input_count_{};std::filesystem::path input_path_;
    std::atomic<UINT64> input_primary_substitutions_{},input_gpu_substitutions_{},input_secondary_fallbacks_{},input_fallbacks_{},
        input_partial_fallbacks_{},input_unwinds_{},input_value_changes_{};
    struct Depth {NativeWeaponAttachmentTrace& self;Depth(NativeWeaponAttachmentTrace& s):self(s) {++self.depth_;}~Depth() {--self.depth_;}};
    friend struct NativeWeaponAttachmentTraceCheck;
    void finish_input(const NativeWeaponInput& input,std::uintptr_t caller,bool normal) noexcept {
        try {
            const bool substituted=static_cast<bool>(input);bool partial{};
            std::lock_guard<std::mutex> lock(mutex_);
            if(substituted && input.site==NativeWeaponDryLease::primary) {
                if(input_primary_ && input_primary_!=input.lease) {++input_partial_fallbacks_;partial=true;}
                input_primary_=input.lease;++input_primary_substitutions_;
            } else if(substituted && input.site==NativeWeaponDryLease::gpu) {
                ++input_gpu_substitutions_;if(input_primary_==input.lease) input_primary_.reset();
            } else {
                ++input_fallbacks_;if(input.site==NativeWeaponDryLease::secondary) ++input_secondary_fallbacks_;
                if(input.site==NativeWeaponDryLease::gpu && input_primary_) {++input_partial_fallbacks_;partial=true;input_primary_.reset();}
            }
            if(!normal) {++input_unwinds_;if(substituted) {++input_partial_fallbacks_;partial=true;input_primary_.reset();}}
            if(substituted && input.value_changed) ++input_value_changes_;
            if(input_observations_ && input_count_<input_observations_->size()) {
                const auto& lease=input.lease;
                (*input_observations_)[input_count_++]={GetTickCount64(),lease?lease->token:0,lease?lease->receipt:0,
                    GetCurrentThreadId(),lease?lease->source.frame:0,input.site,input.reason,caller?caller-base_:0,
                    substituted,input.value_changed,normal,partial};
            }
        } catch(...) {++errors_;}
    }
    struct InputReturn {NativeWeaponAttachmentTrace& trace;NativeWeaponInput input;std::uintptr_t caller;bool normal{};
        ~InputReturn(){trace.finish_input(input,caller,normal);}};
    EquipmentCheck check_equipment(const NativeAttachmentCandidate& candidate,const Publication& publication) {
        EquipmentCheck check{};
        {std::lock_guard<std::mutex> lock(mutex_);check.lease=equipment_;
            if(!check.lease) return check;
            if(equipment_rig_!=publication.arms || equipment_epoch_!=publication.epoch) {
                check.reason=NativeWeaponDryLease::equipment_changed;return check;
            }}
        if(!same_attachment_candidate(check.lease->attachment,candidate)) {check.reason=NativeWeaponDryLease::equipment_link_mismatch;return check;}
        if(!native_equipment_current(read_,*check.lease)) {check.reason=NativeWeaponDryLease::equipment_changed;return check;}
        std::lock_guard<std::mutex> lock(mutex_);
        check.reason=active_.load() && equipment_==check.lease && equipment_rig_==publication.arms &&
            equipment_epoch_==publication.epoch && publication_.arms==publication.arms && publication_.epoch==publication.epoch?
            NativeWeaponDryLease::okay:NativeWeaponDryLease::equipment_changed;
        return check;
    }
    void reject_equipment(const EquipmentCheck& check,const NativeAttachmentCandidate& candidate,std::uintptr_t caller,unsigned site) {
        if(check.reason==NativeWeaponDryLease::equipment_absent) ++equipment_absent_;
        else if(check.reason==NativeWeaponDryLease::equipment_link_mismatch) ++equipment_mismatch_;else ++equipment_changed_;
        auto result=dry_.clear_side(candidate.arm.side,check.reason);result.side=candidate.arm.side;result.site=site;result.reason=check.reason;
        if(check.lease) {result.equipment=check.lease->equipment;result.equipment_declaration=check.lease->equipment_declaration;
            result.equipment_item=check.lease->item;result.equipment_generation=check.lease->equipment_identity.generation;}
        record_dry(result,candidate.binding,caller);
        if(check.reason==NativeWeaponDryLease::equipment_changed) {
            std::lock_guard<std::mutex> lock(mutex_);
            if(equipment_==check.lease) {equipment_.reset();equipment_rig_.reset();equipment_epoch_=0;}
        }
    }
    void record_dry(NativeWeaponDryLease::Result result,std::uintptr_t binding,std::uintptr_t caller) noexcept {
        try {std::lock_guard<std::mutex> lock(mutex_);
            if(dry_observations_ && dry_count_<dry_observations_->size())
                (*dry_observations_)[dry_count_++]={GetTickCount64(),GetCurrentThreadId(),caller?caller-base_:0,binding,result};
        } catch(...) {++errors_;}
    }
    void revoke_dry(unsigned reason=NativeWeaponDryLease::ineligible,unsigned side=2) noexcept {
        try {if(side<2) {auto result=dry_.clear_side(side,reason);if(result.token) record_dry(result,0,0);}
            else for(auto result:dry_.clear(reason)) if(result.token) record_dry(result,0,0);
            if(side==1 || side==2) {std::lock_guard<std::mutex> lock(mutex_);
                if(input_primary_) {
                    ++input_partial_fallbacks_;
                    if(input_observations_ && input_count_<input_observations_->size()) {
                        const auto& lease=input_primary_;
                        (*input_observations_)[input_count_++]={GetTickCount64(),lease->token,lease->receipt,GetCurrentThreadId(),
                            lease->source.frame,0,NativeWeaponInput::unqualified,0,false,false,true,true};
                    }
                    input_primary_.reset();}}
        } catch(...) {++errors_;}
    }
    void prepare_dry(const OuterScope& outer) noexcept {
        if(!frozen_ || !dry_eligible_) return;
        try {
            const auto equipment=check_equipment(outer.candidate,outer.publication);
            if(equipment.reason) {reject_equipment(equipment,outer.candidate,outer.caller,0);return;}
            const auto capture=source_receipts_.capture_current(outer.candidate.arm);
            if(!capture || !outer_current(outer) || !dry_eligible_()) {revoke_dry(NativeWeaponDryLease::ineligible,outer.candidate.arm.side);return;}
            auto frozen=frozen_();const auto revision=frozen.revision;
            const auto result=dry_.prepare(outer.publication.arms,capture->content,capture->match.model,std::move(frozen),
                capture->match.receipt,outer.publication.epoch,outer.candidate,GetTickCount64(),true,&rig::ArmTracking::pose_frozen,equipment.lease);
            record_dry(result,outer.candidate.binding,outer.caller);
            if(!result.accepted) revoke_dry(result.reason,outer.candidate.arm.side);
            // A reservation that changed during computation fails BEFORE the hypothetical primary boundary.
            const auto again=source_receipts_.capture_current(outer.candidate.arm);
            const auto equipment_again=check_equipment(outer.candidate,outer.publication);
            if(result.accepted && (equipment_again.reason || equipment_again.lease!=equipment.lease)) {
                auto changed=equipment_again;if(!changed.reason) changed.reason=NativeWeaponDryLease::equipment_changed;
                reject_equipment(changed,outer.candidate,outer.caller,0);return;
            }
            if(result.accepted && (!again || again->match.receipt!=capture->match.receipt ||
               !same_weapon_source(again->content,capture->content) || !same_weapon_model_base(again->match.model,capture->match.model) ||
               source_receipts_.tracking_stamp().revision!=revision || !outer_current(outer) || !dry_eligible_())) revoke_dry();
        } catch(...) {revoke_dry();++errors_;}
    }
    NativeWeaponDryLease::Result observe_dry(unsigned site,const rig::Arm& arm,std::uintptr_t source,unsigned joint,
        const OuterScope* outer,std::uintptr_t caller,const std::array<float,15>* input_base=nullptr) noexcept {
        if(!frozen_ || !dry_eligible_) return {};
        try {
            const auto saved=dry_.candidate(arm.side);if(!saved) return {};
            const auto capture=source_receipts_.capture_current(arm);
            if(!capture) {revoke_dry(NativeWeaponDryLease::source_changed,arm.side);return {};}
            const auto expected_source=capture->content.source+(joint<69?static_cast<std::uintptr_t>(joint)*64:0);
            Publication publication{};{std::lock_guard<std::mutex> lock(mutex_);publication=publication_;}
            const auto equipment=check_equipment(*saved,publication);
            if(equipment.reason) {reject_equipment(equipment,*saved,caller,site);return {};}
            const bool eligible=active_.load() && publication.arms && publication.epoch &&
                (publication.hands&(1u<<arm.side)) && logic_()==2 && dry_eligible_() &&
                (!outer || outer_current(*outer)) && source==expected_source &&
                (!input_base || !std::memcmp(input_base->data(),capture->match.model.values.data(),sizeof(*input_base))) &&
                (site!=NativeWeaponDryLease::gpu || caller==base_+0x541310);
            const auto current=saved && publication.arms ?
                native_attachment_candidate(read_,base_,*publication.arms,saved->binding,3) : std::optional<NativeAttachmentCandidate>{};
            const auto equipment_again=check_equipment(current?*current:*saved,publication);
            if(equipment_again.reason || equipment_again.lease!=equipment.lease) {
                auto changed=equipment_again;if(!changed.reason) changed.reason=NativeWeaponDryLease::equipment_changed;
                reject_equipment(changed,*saved,caller,site);return {};
            }
            auto result=dry_.observe(arm.side,site,capture->content,capture->match.model,source_receipts_.tracking_stamp(),publication.arms,
                capture->match.receipt,publication.epoch,current,eligible,GetTickCount64(),equipment.lease);
            if(result.token) record_dry(result,outer?outer->candidate.binding:0,caller);
            return result;
        } catch(...) {revoke_dry();++errors_;return {};}
    }
    bool outer_current(const OuterScope& outer) {
        const auto now=GetTickCount64();
        if(!active_.load() || now<outer.publication.tick || now-outer.publication.tick>250 || logic_()!=2) return false;
        const auto candidate=native_attachment_candidate(read_,base_,*outer.publication.arms,outer.candidate.binding,3);
        if(!candidate || !same_attachment_candidate(*candidate,outer.candidate)) return false;
        std::lock_guard<std::mutex> lock(mutex_);
        return publication_.arms==outer.publication.arms && publication_.epoch==outer.publication.epoch &&
            (publication_.hands&(1u<<candidate->arm.side));
    }
    void observe_model(unsigned site,const NativeAttachmentCandidate& candidate,std::uintptr_t caller,
                       NativeWeaponSourceMatch match,const std::array<float,15>* input_base=nullptr) noexcept {
        try {
            ModelObservation row{GetTickCount64(),GetCurrentThreadId(),site,candidate.binding,caller?caller-base_:0,match};
            row.arm=candidate.arm;
            row.binding_generation=candidate.binding_identity.generation;row.joint=candidate.joint;
            if(input_base) {
                row.input_base=*input_base;row.has_input_base=true;
                row.input_base_matched=match.model.valid && !std::memcmp(input_base->data(),match.model.values.data(),sizeof(*input_base));
            }
            const auto flags=candidate.binding?rig::value<std::uint32_t>(read_,candidate.binding+0x408):std::nullopt;
            std::array<float,7> again{};
            row.offset_valid=flags && read_(candidate.binding+0x3d4,row.offset.data(),sizeof(row.offset)) &&
                read_(candidate.binding+0x3d4,again.data(),sizeof(again)) && !std::memcmp(again.data(),row.offset.data(),sizeof(again)) &&
                rig::value<std::uint32_t>(read_,candidate.binding+0x408)==flags;
            row.flags=flags.value_or(0);
            if(match.model_matched) ++model_matches_;else ++model_mismatches_;
            std::lock_guard<std::mutex> lock(mutex_);
            if(observations_ && observation_count_<observations_->size()) (*observations_)[observation_count_++]=row;
        } catch(...) {++errors_;}
    }
    void invoke_outer(void* binding,std::uintptr_t caller) {
        Depth depth(*this);++outer_calls_;OuterScope outer{};auto* previous=outer_scope_;outer_scope_=nullptr;
        try {
            if(active_.load() && (caller==base_+0x7550f1 || caller==base_+0x754587)) {
                {std::lock_guard<std::mutex> lock(mutex_);outer.publication=publication_;}
                const auto now=GetTickCount64();
                if(outer.publication.arms && outer.publication.epoch && now>=outer.publication.tick && now-outer.publication.tick<=250 && logic_()==2) {
                    const auto candidate=native_attachment_candidate(read_,base_,*outer.publication.arms,reinterpret_cast<std::uintptr_t>(binding),3);
                    if(candidate && (outer.publication.hands&(1u<<candidate->arm.side))) {
                        outer.candidate=*candidate;outer.caller=caller;outer_scope_=&outer;++owned_outer_;
                        observe_model(3,*candidate,caller,source_receipts_.match_current(candidate->arm));
                        prepare_dry(outer);
                    }
                }
            }
        } catch(...) {++errors_;}
        struct Restore {OuterScope* previous;~Restore(){outer_scope_=previous;}} restore{previous};
        original_outer_(binding);
        try {if(outer_scope_==&outer && outer_current(outer)) {
            observe_model(4,outer.candidate,caller,source_receipts_.match_current(outer.candidate.arm));
            const auto source=source_receipts_.capture_current(outer.candidate.arm);
            observe_dry(NativeWeaponDryLease::outer_exit,outer.candidate.arm,source?source->content.source:0,69,&outer,caller);
        } else if(outer_scope_==&outer) {
            revoke_dry(NativeWeaponDryLease::ineligible,outer.candidate.arm.side);
        }} catch(...) {++errors_;}
    }
    static void __fastcall outer(void* binding) {instance_->invoke_outer(binding,reinterpret_cast<std::uintptr_t>(_ReturnAddress()));}
    unsigned char invoke_query(void* animation,int stage,short joint,float* position,float* axis,std::uintptr_t caller) {
        Depth depth(*this);auto* outer=outer_scope_;const bool previous=outer?outer->secondary:false;
        bool owned=false;
        try {owned=outer && active_.load() && caller==base_+0x754234 && stage==3 &&
            reinterpret_cast<std::uintptr_t>(animation)==outer->candidate.arm.animation && joint==outer->candidate.joint && outer_current(*outer);
        } catch(...) {++errors_;}
        if(outer) outer->secondary=owned;
        struct Restore {OuterScope* outer;bool previous;~Restore(){if(outer) outer->secondary=previous;}} restore{outer,previous};
        return original_query_(animation,stage,joint,position,axis);
    }
    static unsigned char __fastcall query(void* animation,int stage,short joint,float* position,float* axis) {
        return instance_->invoke_query(animation,stage,joint,position,axis,reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
    }
    static void __fastcall compose(void* binding,int stage,float* position,float* axis,float* linear,float* angular,float step) {
        auto& self=*instance_;Depth depth(self);++self.calls_;
        Scope scope{};Scope* previous=scope_;bool eligible=false;
        if(self.active_.load(std::memory_order_acquire) && !previous && stage==3) {
            try {
                const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
                if(caller==self.base_+0x753d09 || caller==self.base_+0x753ee1) {
                    {std::lock_guard<std::mutex> lock(self.mutex_);scope.publication=self.publication_;}
                    const auto now=GetTickCount64();
                    if(scope.publication.arms && scope.publication.epoch && now>=scope.publication.tick &&
                       now-scope.publication.tick<=250 && self.logic_()==2) {
                        const auto candidate=native_attachment_candidate(self.read_,self.base_,*scope.publication.arms,reinterpret_cast<std::uintptr_t>(binding),stage);
                        if(candidate && (scope.publication.hands&(1u<<candidate->arm.side))) {
                            scope.candidate=*candidate;scope.caller=caller;eligible=true;scope_=&scope;
                        }
                    }
                }
            } catch(...) {++self.errors_;}
        }
        if(previous) ++self.nested_;
        if(!eligible) {++self.rejected_;scope_=nullptr;}
        struct Restore {Scope* previous;~Restore() {scope_=previous;}} restore{previous};
        self.original_compose_(binding,stage,position,axis,linear,angular,step); // Exactly once, no catch/retry.
        if(eligible) self.record(scope,position,axis,linear,angular,step);
    }
    static void __fastcall unpack(const void* source,float* axis,float* position) {
        auto& self=*instance_;Depth depth(self);
        const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
        NativeWeaponInput input{};input.site=NativeWeaponDryLease::primary;bool preobserved{};
        const bool requested=self.input_requested_.load();
        if(requested) {
            try {
                if(outer_scope_ && outer_scope_->secondary) input.site=NativeWeaponDryLease::secondary;
                if(scope_ && outer_scope_ && !outer_scope_->secondary && caller==self.base_+0x53d539 && self.outer_current(*outer_scope_)) {
                    const auto result=self.observe_dry(NativeWeaponDryLease::primary,scope_->candidate.arm,
                        reinterpret_cast<std::uintptr_t>(source),static_cast<unsigned>(scope_->candidate.joint),outer_scope_,caller);
                    preobserved=true;
                    input=native_weapon_input(self.dry_.qualified_input(1,NativeWeaponDryLease::primary),NativeWeaponDryLease::primary,
                        reinterpret_cast<std::uintptr_t>(source),1,caller-self.base_,true,result.accepted && result.strict_revision);
                } else input=native_weapon_input({},input.site,reinterpret_cast<std::uintptr_t>(source),1,caller-self.base_,true,false);
            } catch(...) {++self.errors_;input.reason=NativeWeaponInput::unqualified;}
        }
        // Original arguments for output are always preserved; no retry on unwind.
        if(requested) {InputReturn returned{self,input,caller};native_weapon_input_call(input,source,[&](const void* selected){self.original_unpack_(selected,axis,position);});returned.normal=true;}
        else self.original_unpack_(source,axis,position);
        try {if(outer_scope_ && outer_scope_->secondary && caller==self.base_+0x53d705 && self.outer_current(*outer_scope_)) {
            const auto& candidate=outer_scope_->candidate;
            auto match=self.source_receipts_.match(candidate.arm,reinterpret_cast<std::uintptr_t>(source),static_cast<unsigned>(candidate.joint));
            ++self.secondary_calls_;self.observe_model(2,candidate,caller,match);
            self.observe_dry(NativeWeaponDryLease::secondary,candidate.arm,reinterpret_cast<std::uintptr_t>(source),
                static_cast<unsigned>(candidate.joint),outer_scope_,caller);
        }} catch(...) {++self.errors_;}
        if(!scope_ || caller!=self.base_+0x53d539 || !self.active_.load(std::memory_order_acquire)) return;
        try {
            auto& scope=*scope_;const auto& arm=scope.candidate.arm;
            const auto transforms=rig::list(self.read_,arm.animation+0x2d8,4096);
            const auto states=rig::list(self.read_,arm.animation+0x2e8,4);
            if(!transforms || !states || states->packed_count!=4) return;
            const auto mapping=rig::list(self.read_,states->data+0x30,4096);
            if(!mapping || mapping->packed_count!=69) return;
            const auto index=rig::value<std::int16_t>(self.read_,mapping->data+scope.candidate.joint*2);
            if(!index || *index<0 || static_cast<unsigned>(*index)>=transforms->packed_count ||
               reinterpret_cast<std::uintptr_t>(source)!=transforms->data+static_cast<std::uintptr_t>(*index)*64 ||
               !self.read_(reinterpret_cast<std::uintptr_t>(source),&scope.source_transform,sizeof(scope.source_transform)) || !rig::valid(scope.source_transform)) return;
            scope.source=reinterpret_cast<std::uintptr_t>(source);
            scope.receipt=self.source_receipts_.match(arm,scope.source,static_cast<unsigned>(scope.candidate.joint));
            self.observe_model(1,scope.candidate,caller,scope.receipt);
            if(!preobserved) self.observe_dry(NativeWeaponDryLease::primary,arm,scope.source,static_cast<unsigned>(scope.candidate.joint),outer_scope_,caller);
            ++scope.leaves;++self.leaves_;
        } catch(...) {++self.errors_;}
    }
    void record(const Scope& scope,float* position,float* axis,float* linear,float* angular,float step) noexcept {
        try {
            std::array<float,3> p{},v{},w{};std::array<float,9> a{};
            if(!read_(reinterpret_cast<std::uintptr_t>(position),p.data(),sizeof(p)) || !read_(reinterpret_cast<std::uintptr_t>(axis),a.data(),sizeof(a)) ||
               !read_(reinterpret_cast<std::uintptr_t>(linear),v.data(),sizeof(v)) || !read_(reinterpret_cast<std::uintptr_t>(angular),w.data(),sizeof(w))) {++errors_;return;}
            for(float f:p) if(!std::isfinite(f)) return;for(float f:a) if(!std::isfinite(f)) return;
            for(float f:v) if(!std::isfinite(f)) return;for(float f:w) if(!std::isfinite(f)) return;
            const auto again=native_attachment_candidate(read_,base_,*scope.publication.arms,scope.candidate.binding,3);
            if(!again || !same_attachment_candidate(*again,scope.candidate)) {++rejected_;return;}
            auto receipt=source_receipts_.match(scope.candidate.arm,scope.source,static_cast<unsigned>(scope.candidate.joint));
            receipt.matched=receipt.matched && scope.receipt.matched && receipt.receipt==scope.receipt.receipt &&
                receipt.native_frame==scope.receipt.native_frame && receipt.epoch==scope.receipt.epoch;
            if(!receipt.matched && !receipt.reason) receipt.reason=5;
            std::lock_guard<std::mutex> lock(mutex_);const auto tick=GetTickCount64();
            if(!active_.load(std::memory_order_acquire) || publication_.arms!=scope.publication.arms || publication_.epoch!=scope.publication.epoch ||
               !publication_.tick || tick<publication_.tick || tick-publication_.tick>250 || logic_()!=2 || samples_>=8192) return;
            const auto& c=scope.candidate;const auto& arm=c.arm;
            log_<<tick<<','<<GetCurrentThreadId()<<','<<scope.publication.epoch<<','<<tick-scope.publication.tick<<','<<3<<','<<scope.caller-base_<<','
                <<c.binding<<','<<c.binding_identity.generation<<','<<c.binding_cached_frame<<','<<c.entity<<','<<c.entity_table-base_<<','<<c.parent<<','<<c.parent_table-base_<<','
                <<arm.side<<','<<arm.player<<','<<arm.body<<','<<arm.body_identity.generation<<','<<arm.animation<<','<<arm.animation_identity.generation<<','<<arm.model<<','
                <<c.joint<<','<<std::quoted(scope.publication.arms->sources[arm.side].labels[static_cast<unsigned>(c.joint)])<<','
                <<c.inventory<<','<<c.inventory_identity.generation;
            for(unsigned side=0;side<2;++side) log_<<','<<c.slots[side]<<','<<(c.slot_tables[side]?c.slot_tables[side]-base_:0)<<','<<c.slot_owner_fields[side]<<','<<c.slot_identity_fields[side];
            log_<<",false,"<<scope.source<<','<<scope.leaves<<','<<step;
            for(float f:p) log_<<','<<f;for(float f:a) log_<<','<<f;for(float f:v) log_<<','<<f;for(float f:w) log_<<','<<f;
            for(float f:scope.source_transform.f) log_<<','<<f;
            log_<<','<<receipt.receipt<<','<<receipt.native_frame<<','<<receipt.receipt_frame<<','<<receipt.producer_thread<<','
                <<receipt.epoch<<','<<(receipt.matched?"true":"false")<<','<<receipt.reason<<','<<receipt.game<<','
                <<receipt.receipt_source<<','<<receipt.receipt_remap<<','<<receipt.receipt_transform_count<<','
                <<receipt.producer_caller_rva<<','<<receipt.producer_stage<<'\n';
            if(!log_) {++errors_;active_=false;return;}++samples_;
        } catch(...) {++errors_;}
    }
public:
    // Configure before install. Enabling a running installation is forbidden.
    void input_experiment(bool requested) {
        if(active_.load()) throw std::logic_error("Configure numeric input experiment before installation");
        input_requested_=requested;
    }
    void tracking_reader(rig::ArmTrackingStamp (*reader)()) {source_receipts_.tracking_reader(reader);}
    void dry_reader(Frozen reader,DryEligible eligible) {frozen_=reader;dry_eligible_=eligible;}
    void clear_equipment(unsigned reason=NativeWeaponDryLease::equipment_changed) noexcept {
        try {{std::lock_guard<std::mutex> lock(mutex_);equipment_.reset();equipment_rig_.reset();equipment_epoch_=0;}
            revoke_dry(reason);
        } catch(...) {++errors_;}
    }
    // Main/present only, after publish(). All component enumeration and lease
    // allocation occurs here; native callbacks only validate saved ranges.
    bool refresh_equipment(std::shared_ptr<const rig::NativeArmBindingSnapshot> arms,UINT64 epoch,bool eligible) noexcept {
        try {
            if(!eligible || !active_.load() || !read_ || !arms || !epoch) {clear_equipment();return false;}
            std::shared_ptr<const NativeEquipmentOwnership> previous;
            {std::lock_guard<std::mutex> lock(mutex_);
                if(publication_.arms!=arms || publication_.epoch!=epoch || !(publication_.hands&2u)) previous={};
                else if(equipment_rig_==arms && equipment_epoch_==epoch) previous=equipment_;
            }
            if(previous && native_equipment_current(read_,*previous)) {
                std::lock_guard<std::mutex> lock(mutex_);
                if(equipment_==previous && publication_.arms==arms && publication_.epoch==epoch) return true;
            }
            clear_equipment();
            {std::lock_guard<std::mutex> lock(mutex_);
                if(publication_.arms!=arms || publication_.epoch!=epoch || !(publication_.hands&2u)) return false;}
            auto discovered=native_sword_equipment_ownership(read_,base_,*arms);if(!discovered) return false;
            auto lease=std::make_shared<const NativeEquipmentOwnership>(std::move(*discovered));
            if(!native_equipment_current(read_,*lease)) return false;
            std::lock_guard<std::mutex> lock(mutex_);
            if(!active_.load() || publication_.arms!=arms || publication_.epoch!=epoch || !(publication_.hands&2u)) return false;
            equipment_=std::move(lease);equipment_rig_=std::move(arms);equipment_epoch_=epoch;++equipment_refreshes_;return true;
        } catch(...) {clear_equipment();++errors_;return false;}
    }
    void publish(std::shared_ptr<const rig::NativeArmBindingSnapshot> arms,UINT64 epoch,UINT64 tick,unsigned hands,bool eligible) {
        source_receipts_.publish(arms,epoch,tick,hands,eligible);
        bool revoke{};
        {std::lock_guard<std::mutex> lock(mutex_);
            publication_=eligible && arms && rig::native_arm_pair_owned(arms->arms) && epoch?Publication{std::move(arms),epoch,tick,hands&3u}:Publication{};
            revoke=!publication_.arms || !(publication_.hands&2u) || equipment_rig_!=publication_.arms || equipment_epoch_!=publication_.epoch;
            if(revoke) {equipment_.reset();equipment_rig_.reset();equipment_epoch_=0;}}
        if(revoke) revoke_dry(NativeWeaponDryLease::equipment_changed);
    }
    void clear() noexcept {clear_equipment();source_receipts_.clear();try {std::lock_guard<std::mutex> lock(mutex_);publication_={};} catch(...) {active_=false;}}
    void install(std::uintptr_t base,Read read,Logic logic,const std::filesystem::path& path) {
        if(instance_ && instance_!=this) throw std::runtime_error("Another native attachment trace owns this DLL");
        if((original_compose_ || original_unpack_) && base_!=base) throw std::runtime_error("Native attachment trace image owner changed");
        if(!read || !logic) throw std::invalid_argument("Missing attachment trace readers");
        if(input_requested_ && (!frozen_ || !dry_eligible_)) throw std::invalid_argument("Numeric input experiment requires frozen tracking eligibility");
        base_=base;read_=read;logic_=logic;
        for(unsigned i=0;i<rvas.size();++i) {std::array<unsigned char,32> code{};
            if(!read_(base+rvas[i],code.data(),code.size()) || code!=expected[i]) throw std::runtime_error("Native attachment trace prefix differs");}
        for(const auto call:std::array<std::pair<std::uintptr_t,std::uintptr_t>,7>{{{0x753d04,0x7506d0},{0x753edc,0x7506d0},{0x53d534,0x9b900},
            {0x7550ec,0x753c10},{0x754582,0x753c10},{0x75422f,0x53d6a0},{0x53d700,0x9b900}}}) {
            std::array<unsigned char,5> bytes{};std::int32_t displacement{};
            if(!read_(base+call.first,bytes.data(),bytes.size()) || bytes[0]!=0xe8) throw std::runtime_error("Native attachment caller differs");
            std::memcpy(&displacement,bytes.data()+1,4);
            if(base+call.first+5+displacement!=base+call.second) throw std::runtime_error("Native attachment caller target differs");
        }
        log_.open(path,std::ios::trunc);if(!log_) throw std::runtime_error("Cannot open native attachment trace");
        model_path_=path.parent_path()/L"native-weapon-model-base.csv";
        if(!observations_) observations_=std::make_unique<std::array<ModelObservation,8192>>();
        if(frozen_) {
            dry_path_=path.parent_path()/L"native-weapon-dry-lease.csv";
            if(!dry_observations_) dry_observations_=std::make_unique<std::array<DryObservation,8192>>();
        } else dry_path_.clear();
        if(input_requested_) {
            std::array<unsigned char,5> call{};std::int32_t displacement{};
            if(!read_(base+0x54130b,call.data(),call.size()) || call[0]!=0xe8) throw std::runtime_error("Numeric upload caller differs");
            std::memcpy(&displacement,call.data()+1,4);
            if(base+0x541310+displacement!=base+0x25efe0) throw std::runtime_error("Numeric upload target differs");
            input_path_=path.parent_path()/L"native-weapon-input.csv";
            input_observations_=std::make_unique<std::array<InputObservation,8192>>();
        } else {input_path_.clear();input_observations_.reset();}
        log_<<"tick,thread,epoch,pose_age_ms,stage,caller_rva,binding,binding_generation,binding_cached_frame,entity,entity_vtable_rva,parent,parent_vtable_rva,side,player,body,body_generation,animation,animation_generation,model,joint,socket,inventory,inventory_generation,left_slot,left_slot_vtable_rva,left_slot_owner_field,left_slot_identity_field,right_slot,right_slot_vtable_rva,right_slot_owner_field,right_slot_identity_field,slot_equivalence_verified,source,leaf_count,step";
        for(const char* kind:{"position","axis","linear","angular","source_matrix"}) for(unsigned i=0;i<(std::strcmp(kind,"axis")==0?9u:std::strcmp(kind,"source_matrix")==0?16u:3u);++i) log_<<','<<kind<<i;
        log_<<",receipt_serial,native_frame,receipt_frame,receipt_producer_thread,receipt_epoch,receipt_matched,receipt_reason,receipt_game,receipt_source,receipt_remap,receipt_transform_count,receipt_producer_caller_rva,receipt_producer_stage\n";
        clear();calls_=samples_=rejected_=leaves_=nested_=errors_=0;instance_=this;
        observation_count_=0;outer_calls_=owned_outer_=secondary_calls_=model_matches_=model_mismatches_=gpu_observations_=0;
        dry_count_=0;dry_.reset();equipment_absent_=equipment_changed_=equipment_mismatch_=equipment_refreshes_=0;
        input_count_=0;input_primary_.reset();input_primary_substitutions_=input_gpu_substitutions_=input_secondary_fallbacks_=input_fallbacks_=0;
        input_partial_fallbacks_=input_unwinds_=input_value_changes_=0;
        source_receipts_.install(base,read,logic);
        const std::array<void*,4> hooks{reinterpret_cast<void*>(&compose),reinterpret_cast<void*>(&unpack),reinterpret_cast<void*>(&outer),reinterpret_cast<void*>(&query)};
        const std::array<void**,4> originals{reinterpret_cast<void**>(&original_compose_),reinterpret_cast<void**>(&original_unpack_),reinterpret_cast<void**>(&original_outer_),reinterpret_cast<void**>(&original_query_)};
        for(unsigned i=0;i<rvas.size();++i) {
            const auto target=reinterpret_cast<void*>(base+rvas[i]);
            if(!*originals[i] && MH_CreateHook(target,hooks[i],originals[i])!=MH_OK) throw std::runtime_error("Cannot create native attachment trace");
            if(MH_EnableHook(target)!=MH_OK) throw std::runtime_error("Cannot enable native attachment trace");enabled_[i]=true;
        }
        active_.store(true,std::memory_order_release);
    }
    bool disable() noexcept {
        active_.store(false,std::memory_order_release);clear();
        const bool receipts_disabled=source_receipts_.disable();
        if(!read_) return receipts_disabled;
        const std::array<bool,4> created{original_compose_!=nullptr,original_unpack_!=nullptr,original_outer_!=nullptr,original_query_!=nullptr};
        bool okay=receipts_disabled;for(unsigned i=0;i<rvas.size();++i) if(created[i]) {
            const auto status=MH_DisableHook(reinterpret_cast<void*>(base_+rvas[i]));
            if(status==MH_OK || status==MH_ERROR_DISABLED) enabled_[i]=false;else okay=false;
        }
        const auto deadline=GetTickCount64()+1000;while(depth_ && GetTickCount64()<deadline) Sleep(1);
        okay=okay && !depth_;
        for(unsigned i=0;i<rvas.size();++i) if(created[i]) {
            std::array<unsigned char,32> code{};if(!read_(base_+rvas[i],code.data(),code.size()) || code!=expected[i]) okay=false;
        }
        try {std::lock_guard<std::mutex> lock(mutex_);log_.close();
            if(!depth_ && !model_path_.empty()) {
                std::ofstream output(model_path_,std::ios::trunc);output<<std::setprecision(9);
                output<<"tick,thread,site,binding,binding_generation,joint,caller_rva,side,player,body,body_generation,animation,animation_generation,skeleton,receipt_serial,native_frame,receipt_frame,source_matched,source_reason,model,model_valid,receipt_model_valid,model_matched,tracking_revision,receipt_tracking_revision,tracking_epoch,tracking_reference_epoch,tracking_hands,tracking_calibrated,tracking_matched,flags,offset_valid,input_model_valid,input_model_matched";
                for(unsigned i=0;i<7;++i) output<<",offset"<<i;
                for(const char* prefix:{"model_base","receipt_model_base"}) for(unsigned i=0;i<15;++i) output<<','<<prefix<<i;
                for(unsigned i=0;i<15;++i) output<<",input_model_base"<<i;
                output<<'\n';
                for(std::size_t i=0;i<observation_count_;++i) {
                    const auto& row=(*observations_)[i];const auto& m=row.match;
                    output<<row.tick<<','<<row.thread<<','<<row.site<<','<<row.binding<<','<<row.binding_generation<<','<<row.joint<<','<<row.caller<<','<<row.arm.side<<','<<row.arm.player<<','
                        <<row.arm.body<<','<<row.arm.body_identity.generation<<','<<row.arm.animation<<','<<row.arm.animation_identity.generation<<','<<row.arm.skeleton<<','
                        <<m.receipt<<','<<m.native_frame<<','<<m.receipt_frame<<','
                        <<m.matched<<','<<m.reason<<','<<m.model.model<<','<<m.model.valid<<','<<m.receipt_model.valid<<','<<m.model_matched<<','
                        <<m.tracking.revision<<','<<m.receipt_tracking.revision<<','<<m.tracking.epoch<<','<<m.tracking.reference_epoch<<','
                        <<m.tracking.hands<<','<<m.tracking.calibrated<<','<<m.tracking_matched<<','<<row.flags<<','<<row.offset_valid<<','<<row.has_input_base<<','<<row.input_base_matched;
                    for(float f:row.offset) output<<','<<f;
                    for(float f:m.model.values) output<<','<<f;for(float f:m.receipt_model.values) output<<','<<f;
                    for(float f:row.input_base) output<<','<<f;output<<'\n';
                }
                if(!output) okay=false;
            }
            if(!depth_ && !dry_path_.empty()) {
                std::ofstream output(dry_path_,std::ios::trunc);output<<std::setprecision(9);
                output<<"tick,thread,caller_rva,binding,token,receipt_serial,native_frame,side,site,reason,accepted,strict_revision,after_hypothetical_primary,observed_consumers_complete,secondary_observed,frozen_revision,posed69_digest,outputs_changed,typed_equipment_verified,equipment,equipment_generation,equipment_declaration,equipment_item";
                for(unsigned i=0;i<16;++i) output<<",predicted_socket"<<i;output<<'\n';
                for(std::size_t i=0;i<dry_count_;++i) {
                    const auto& row=(*dry_observations_)[i];const auto& r=row.result;
                    output<<row.tick<<','<<row.thread<<','<<row.caller<<','<<row.binding<<','<<r.token<<','<<r.receipt<<','<<r.frame<<','
                        <<r.side<<','<<r.site<<','<<r.reason<<','<<r.accepted<<','<<r.strict_revision<<','<<r.after_primary<<','<<r.complete<<','<<r.secondary_observed<<','
                        <<r.revision<<','<<r.posed_digest<<",false,"<<r.typed_equipment_verified<<','<<r.equipment<<','<<r.equipment_generation<<','<<r.equipment_declaration<<','<<r.equipment_item;
                    for(float f:r.predicted_socket.f) output<<','<<f;output<<'\n';
                }
                if(!output) okay=false;
            }
            if(!depth_ && !input_path_.empty()) {
                std::ofstream output(input_path_,std::ios::trunc);
                output<<"tick,thread,token,receipt_serial,native_frame,site,caller_rva,reason,actually_substituted,input_value_changed,original_normal_return,native_effect_possible,partial_fallback,wrapper_source_written,wrapper_outputs_written\n";
                for(std::size_t i=0;i<input_count_;++i) {const auto& row=(*input_observations_)[i];
                    output<<row.tick<<','<<row.thread<<','<<row.token<<','<<row.receipt<<','<<row.frame<<','<<row.site<<','<<row.caller<<','<<row.reason<<','
                        <<row.substituted<<','<<row.value_changed<<','<<row.normal_return<<','<<(row.substituted && row.value_changed)<<','<<row.partial_fallback<<",false,false\n";}
                if(!output) okay=false;
            }
        } catch(...) {okay=false;}
        return okay;
    }
    UINT64 calls() const {return calls_.load();}UINT64 samples() const {return samples_.load();}
    UINT64 rejected() const {return rejected_.load();}UINT64 leaves() const {return leaves_.load();}
    UINT64 nested() const {return nested_.load();}UINT64 errors() const {return errors_.load();}
    UINT64 outer_calls() const {return outer_calls_.load();}UINT64 owned_outer() const {return owned_outer_.load();}
    UINT64 secondary_calls() const {return secondary_calls_.load();}UINT64 model_matches() const {return model_matches_.load();}
    UINT64 model_mismatches() const {return model_mismatches_.load();}
    UINT64 gpu_observations() const {return gpu_observations_.load();}
    std::size_t model_observations() {std::lock_guard<std::mutex> lock(mutex_);return observation_count_;}
    const NativeWeaponSourceReceipts& source_receipts() const noexcept {return source_receipts_;}
    NativeWeaponSourceMatch match_upload(const rig::Arm& arm,std::uintptr_t source,
        const std::array<float,15>& input_base,std::uintptr_t caller=0) noexcept {
        Depth depth(*this);auto match=source_receipts_.match(arm,source);
        if(active_.load() && arm.side<2) {
            NativeAttachmentCandidate candidate{};candidate.arm=arm;candidate.joint=69;
            ++gpu_observations_;observe_model(5,candidate,caller,match,&input_base);
            if(!input_requested_) observe_dry(NativeWeaponDryLease::gpu,arm,source,69,nullptr,caller,&input_base);
        }
        return match;
    }
    const NativeWeaponDryLease& dry() const {return dry_;}
    // baseline_source is the caller's existing submitted palette. Its lifetime
    // and every native output/model argument remain the caller's responsibility.
    template<class Original> void invoke_input_upload(const rig::Arm& arm,void* model,const void* native_source,
        const void* baseline_source,UINT count,const std::array<float,15>& input_base,std::uintptr_t caller,Original&& original) {
        if(!input_requested_) {original(model,baseline_source,count);return;}
        Depth depth(*this);NativeWeaponInput input{};input.site=NativeWeaponDryLease::gpu;input.reason=NativeWeaponInput::unqualified;
        try {
            Publication publication{};std::shared_ptr<const NativeWeaponDryLease::Lease> primary;
            {std::lock_guard<std::mutex> lock(mutex_);publication=publication_;primary=input_primary_;}
            const auto now=GetTickCount64();
            const bool fresh=active_.load() && publication.arms && publication.epoch && (publication.hands&2u) &&
                now>=publication.tick && now-publication.tick<=250 && primary && primary->rig==publication.arms &&
                primary->epoch==publication.epoch && arm.side==1 && arm.model==reinterpret_cast<std::uintptr_t>(model) &&
                primary->candidate.arm==arm;
            NativeWeaponDryLease::Result result{};
            if(fresh && caller==base_+0x541310 && count==69)
                result=observe_dry(NativeWeaponDryLease::gpu,arm,reinterpret_cast<std::uintptr_t>(native_source),69,nullptr,caller,&input_base);
            auto lease=dry_.qualified_input(1,NativeWeaponDryLease::gpu);
            input=native_weapon_input(lease,NativeWeaponDryLease::gpu,reinterpret_cast<std::uintptr_t>(native_source),count,
                caller-base_,true,fresh && result.accepted && result.strict_revision && lease==primary);
        } catch(...) {++errors_;input.reason=NativeWeaponInput::unqualified;}
        InputReturn returned{*this,input,caller};native_weapon_input_call(input,baseline_source,[&](const void* selected){original(model,selected,count);});returned.normal=true;
    }
    UINT64 input_primary_substitutions() const {return input_primary_substitutions_.load();}
    UINT64 input_gpu_substitutions() const {return input_gpu_substitutions_.load();}
    UINT64 input_secondary_fallbacks() const {return input_secondary_fallbacks_.load();}
    UINT64 input_fallbacks() const {return input_fallbacks_.load();}
    UINT64 input_partial_fallbacks() const {return input_partial_fallbacks_.load();}
    UINT64 input_unwinds() const {return input_unwinds_.load();}
    UINT64 input_value_changes() const {return input_value_changes_.load();}
    UINT64 equipment_absent() const {return equipment_absent_.load();}
    UINT64 equipment_changed() const {return equipment_changed_.load();}
    UINT64 equipment_mismatch() const {return equipment_mismatch_.load();}
    UINT64 equipment_refreshes() const {return equipment_refreshes_.load();}
};
} // namespace d2vr
