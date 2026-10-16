#pragma once
#include "full_hand_query_transaction.hpp"
#include "hand_palette_contact.hpp"
#include "anatomical_raw_pair.hpp"

namespace d2vr {
// One existing per-side mailbox. Its results are immutable numeric palettes;
// native queries run only inside the caller's admitted Stage0 callback.
class NativeFullHandContact {
public:
    using Clock=std::uint64_t(*)() noexcept;
    struct ProducerReceipt {AnatomicalContactOwner owner{};std::uint64_t epoch{},side_epoch{};unsigned side{};};
    struct Sample {
        AnatomicalContactOwner owner{};
        NativeWorldQuerySource source{};
        std::shared_ptr<const rig::NativeArmBindingSnapshot> binding;
        fullbody::Tracking tracking{};
        HandProxies proxies{};
        std::uint64_t frame{},fit_revision{};
        unsigned side{};
        std::optional<fullbody::PreparedContactArm> prepared;
        ProducerReceipt producer{};
    };
private:
    struct Response {Sample sample;FullHandQueryTransaction::Result result;std::uint64_t tick{},receipt{},epoch{};};
public:
    class Receipt {
        friend class NativeFullHandContact;
        const NativeFullHandContact* owner_{};
        std::shared_ptr<const Response> value_;
        Receipt(const NativeFullHandContact* owner,std::shared_ptr<const Response> value):owner_(owner),value_(std::move(value)) {}
    public:
        Receipt()=default;
        explicit operator bool() const noexcept {return owner_ && value_;}
        std::uint64_t id() const noexcept {return value_?value_->receipt:0;}
        const Sample& sample() const noexcept {return value_->sample;}
        const FullHandQueryTransaction::Result& result() const noexcept {return value_->result;}
        std::uint64_t query_tick() const noexcept {return value_->tick;}
    };
    struct Resolved {Vec3 shift{};HandProxies proxies{};bool contact{};std::uintptr_t entity{};std::uint64_t geometry_tick{};};
    enum class ContactReason : std::uint32_t {
        none,explicit_clear,scope_unqualified,owner_or_grab_unavailable,verification_failed,inactive,stage_exception,
        lifecycle_start,lifecycle_stop,owner_changed,explicit_revoke,publish_invalid,publish_incompatible,
        replaced_contact,replaced_clear,batch_rejected,service_exception,receipt_exhausted,
        stopping,sealed,invalid_receipt,side_invalid,epoch_changed,owner_mismatch,response_replaced,
        tracking_age,source_age,query_age,latest_missing,incompatible,latest_tracking_age,latest_source_age
    };
    enum CompatibilityBit : std::uint32_t {
        different_side=1u<<0,different_owner=1u<<1,different_source=1u<<2,different_binding=1u<<3,different_fit=1u<<4,
        different_tracking_epoch=1u<<5,different_pose_epoch=1u<<6,different_reference_epoch=1u<<7,different_mapping=1u<<8,
        different_finger_presence=1u<<9,different_axis_presence=1u<<10,different_grip=1u<<11,different_trigger=1u<<12,
        different_finger_turn=1u<<13,different_finger_axis=1u<<14
    };
    struct ContactEvent {
        std::uint64_t tick{},epoch{},side_epoch{},latest_frame{},latest_tracking_tick{},latest_source_tick{};
        ContactReason reason{};
        std::uint32_t mismatch_bits{},work_mask{};
        FullHandQueryTransaction::State transaction_state{};
        bool budget_hit{};
    };
    struct ContactLifecycle {
        std::uint64_t receipt{},epoch{},side_epoch{},frame{},tracking_revision{},tracking_tick{},source_tick{},query_tick{},publish_tick{},selected_tick{};
        unsigned side{};
        ContactEvent lookup{},destruction{};
    };
    struct ContactDiagnostics {
        std::array<ContactLifecycle,32> records{};
        std::uint64_t published{},overwritten{};
        unsigned count{},next{};
    };
private:
    mutable std::mutex mutex_;
    AnatomicalContactOwner owner_{};
    std::array<std::optional<Sample>,2> pending_{},latest_{};
    std::array<std::shared_ptr<const Response>,2> response_{};
    std::array<std::uint64_t,2> side_epoch_{},last_frame_{};
    std::uint64_t epoch_{1},next_receipt_{};
    unsigned inflight_{};
    bool stopping_{},sealed_{};
    std::atomic_flag servicing_=ATOMIC_FLAG_INIT;
    struct Active {std::uint64_t receipt{};unsigned count{};std::shared_ptr<const Response> value;};
    std::array<Active,64> active_{};
    struct Local {const NativeFullHandContact* owner;std::uint64_t receipt;unsigned count,committed;};
    inline static thread_local std::array<Local,16> local_{};
    static std::uint64_t real_tick() noexcept {return GetTickCount64();}
    Clock clock_;
    mutable ContactDiagnostics contact_diagnostics_{};
    std::array<unsigned,2> contact_slots_{32,32};
    std::uint64_t timestamp(std::uint64_t hint) const noexcept {return std::max(hint,clock_());}
    bool continuation(std::uint64_t receipt) const noexcept {
        for(const auto& slot:local_)if(slot.owner==this && slot.receipt==receipt && slot.committed)return true;
        return false;
    }
    bool enter_local(std::uint64_t receipt=0) noexcept {
        for(auto& slot:local_)if(slot.owner==this && slot.receipt==receipt) {++slot.count;return true;}
        for(auto& slot:local_)if(!slot.owner) {slot={this,receipt,1,0};return true;}
        return false;
    }
    void leave_local(std::uint64_t receipt=0,bool committed=false) noexcept {
        for(auto& slot:local_)if(slot.owner==this && slot.receipt==receipt) {if(committed)--slot.committed;if(!--slot.count)slot={};return;}
    }
    static bool fresh(std::uint64_t tick,std::uint64_t now) noexcept {return tick && now>=tick && now-tick<=32;}
    static bool fresh(const Sample& value,std::uint64_t now) noexcept {
        return fresh(value.tracking.tick,now) && fresh(value.tracking.source_tick,now);
    }
    static WorldTrackedPose raw(const Sample& sample) {
        const auto& pose=sample.tracking.raw_grip[sample.side];
        return {pose.p,{rotated(pose.q,{1,0,0}),rotated(pose.q,{0,1,0}),rotated(pose.q,{0,0,1})}};
    }
    static bool compatible(const Sample& a,const Sample& b) noexcept {
        const auto side=a.side;const auto& x=a.tracking;const auto& y=b.tracking;
        return side==b.side && a.owner==b.owner && a.source==b.source && a.binding==b.binding && a.fit_revision==b.fit_revision &&
            x.epoch==y.epoch && x.pose_epoch==y.pose_epoch && x.reference_epoch==y.reference_epoch && x.mapping_revision==y.mapping_revision &&
            x.has_finger[side]==y.has_finger[side] && x.has_finger_axis[side]==y.has_finger_axis[side] &&
            x.grip_value[side]==y.grip_value[side] && x.trigger[side]==y.trigger[side] &&
            std::memcmp(x.finger_turn[side],y.finger_turn[side],sizeof(x.finger_turn[side]))==0 &&
            std::memcmp(x.finger_axis[side],y.finger_axis[side],sizeof(x.finger_axis[side]))==0;
    }
    static std::uint32_t compatibility_bits(const Sample& previous,const Sample& next) noexcept {
        const auto side=previous.side;const auto& before=previous.tracking;const auto& after=next.tracking;
        if(side>1 || next.side>1)return different_side;
        std::uint32_t bits{};
        if(side!=next.side)bits|=different_side;
        if(previous.owner!=next.owner)bits|=different_owner;
        if(previous.source!=next.source)bits|=different_source;
        if(previous.binding!=next.binding)bits|=different_binding;
        if(previous.fit_revision!=next.fit_revision)bits|=different_fit;
        if(before.epoch!=after.epoch)bits|=different_tracking_epoch;
        if(before.pose_epoch!=after.pose_epoch)bits|=different_pose_epoch;
        if(before.reference_epoch!=after.reference_epoch)bits|=different_reference_epoch;
        if(before.mapping_revision!=after.mapping_revision)bits|=different_mapping;
        if(before.has_finger[side]!=after.has_finger[side])bits|=different_finger_presence;
        if(before.has_finger_axis[side]!=after.has_finger_axis[side])bits|=different_axis_presence;
        if(before.grip_value[side]!=after.grip_value[side])bits|=different_grip;
        if(before.trigger[side]!=after.trigger[side])bits|=different_trigger;
        if(std::memcmp(before.finger_turn[side],after.finger_turn[side],sizeof(before.finger_turn[side]))!=0)bits|=different_finger_turn;
        if(std::memcmp(before.finger_axis[side],after.finger_axis[side],sizeof(before.finger_axis[side]))!=0)bits|=different_finger_axis;
        return bits;
    }
    ContactLifecycle* contact_record_locked(unsigned side) const noexcept {
        if(side>1 || contact_slots_[side]>=contact_diagnostics_.records.size() || !response_[side] || !response_[side]->result.contact)return nullptr;
        auto& record=contact_diagnostics_.records[contact_slots_[side]];
        return record.receipt==response_[side]->receipt?&record:nullptr;
    }
    ContactEvent contact_event_locked(unsigned side,ContactReason reason,std::uint64_t now) const noexcept {
        ContactEvent event{};event.tick=now;event.epoch=epoch_;event.side_epoch=side_epoch_[side];event.reason=reason;
        if(latest_[side]) {
            event.latest_frame=latest_[side]->frame;event.latest_tracking_tick=latest_[side]->tracking.tick;
            event.latest_source_tick=latest_[side]->tracking.source_tick;
        }
        return event;
    }
    void contact_destroyed_locked(unsigned side,ContactReason reason,std::uint64_t now,std::uint32_t mismatch=0,
                                  FullHandQueryTransaction::State state=FullHandQueryTransaction::State::invalid,
                                  unsigned work_mask=0,bool budget_hit=false) noexcept {
        if(auto* record=contact_record_locked(side);record && record->destruction.reason==ContactReason::none) {
            record->destruction=contact_event_locked(side,reason,now);record->destruction.mismatch_bits=mismatch;
            record->destruction.transaction_state=state;record->destruction.work_mask=work_mask;record->destruction.budget_hit=budget_hit;
        }
    }
    void contact_published_locked(unsigned side,std::uint64_t now) noexcept {
        if(!response_[side]->result.contact) {contact_slots_[side]=32;return;}
        auto& diagnostic=contact_diagnostics_;
        if(diagnostic.published!=UINT64_MAX)++diagnostic.published;
        if(diagnostic.count==diagnostic.records.size()) {if(diagnostic.overwritten!=UINT64_MAX)++diagnostic.overwritten;}
        else ++diagnostic.count;
        const auto slot=diagnostic.next;diagnostic.next=(slot+1)%static_cast<unsigned>(diagnostic.records.size());contact_slots_[side]=slot;
        const auto& value=*response_[side];const auto& sample=value.sample;
        auto& record=diagnostic.records[slot];record={};
        record.receipt=value.receipt;record.epoch=value.epoch;record.side_epoch=side_epoch_[side];record.side=side;
        record.frame=sample.frame;record.tracking_revision=sample.tracking.tracking_revision;
        record.tracking_tick=sample.tracking.tick;record.source_tick=sample.tracking.source_tick;
        record.query_tick=value.tick;record.publish_tick=now;
    }
    ContactReason candidate_failure_locked(const Response& value,std::uint64_t now) const noexcept {
        const auto side=value.sample.side;
        if(stopping_)return ContactReason::stopping;
        if(sealed_)return ContactReason::sealed;
        if(side>1)return ContactReason::side_invalid;
        if(value.epoch!=epoch_)return ContactReason::epoch_changed;
        if(value.sample.owner!=owner_)return ContactReason::owner_mismatch;
        if(response_[side].get()!=&value)return ContactReason::response_replaced;
        if(!fresh(value.sample.tracking.tick,now))return ContactReason::tracking_age;
        if(!fresh(value.sample.tracking.source_tick,now))return ContactReason::source_age;
        if(!fresh(value.tick,now))return ContactReason::query_age;
        if(!latest_[side])return ContactReason::latest_missing;
        if(!compatible(value.sample,*latest_[side]))return ContactReason::incompatible;
        if(!fresh(latest_[side]->tracking.tick,now))return ContactReason::latest_tracking_age;
        if(!fresh(latest_[side]->tracking.source_tick,now))return ContactReason::latest_source_age;
        return ContactReason::none;
    }
    void revoke_locked(ContactReason reason,std::uint64_t now) noexcept {
        for(unsigned side=0;side<2;++side)contact_destroyed_locked(side,reason,now);
        if(epoch_==UINT64_MAX)sealed_=true;else ++epoch_;
        owner_={};pending_={};latest_={};response_={};last_frame_={};
    }
    bool current_locked(const Receipt& receipt,std::uint64_t now) const noexcept {
        if(receipt.owner_!=this || !receipt.value_ || stopping_ || sealed_)return false;
        const auto& value=*receipt.value_;const auto side=value.sample.side;
        return side<2 && value.epoch==epoch_ && value.sample.owner==owner_ && response_[side]==receipt.value_ &&
            fresh(value.sample,now) && fresh(value.tick,now) && latest_[side] && compatible(value.sample,*latest_[side]) && fresh(*latest_[side],now);
    }
    bool sample_current(const Sample& sample,std::uint64_t epoch,std::uint64_t side_epoch,std::uint64_t now) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return !stopping_ && !sealed_ && epoch_==epoch && side_epoch_[sample.side]==side_epoch && owner_==sample.owner &&
            latest_[sample.side] && compatible(sample,*latest_[sample.side]) && fresh(sample,now) && fresh(*latest_[sample.side],now);
    }
    bool producer_current_locked(const ProducerReceipt& receipt) const noexcept {
        return receipt.side<2 && receipt.epoch && receipt.epoch==epoch_ && receipt.side_epoch==side_epoch_[receipt.side] &&
            receipt.owner.valid() && receipt.owner==owner_ && !stopping_ && !sealed_;
    }
public:
    explicit NativeFullHandContact(Clock clock=nullptr):clock_(clock?clock:&real_tick) {}
    std::atomic<std::uint64_t> casts{},queries{},contacts{},rejected{},batches{},service_ns{},worst_service_ns{},consumed{},fallbacks{},budget_rejected{},deferred{};
    std::atomic<std::uint64_t> attachment_continued{},attachment_rejected{};
    std::array<std::atomic<std::uint64_t>,9> states{};
    std::atomic<std::uint64_t> budget_ns{3000000};
    void clear(ContactReason reason=ContactReason::explicit_clear) {const auto now=clock_();std::lock_guard<std::mutex> lock(mutex_);revoke_locked(reason,now);}
    void stop() {const auto now=clock_();std::lock_guard<std::mutex> lock(mutex_);stopping_=true;revoke_locked(ContactReason::lifecycle_stop,now);}
    bool start() {const auto now=clock_();std::lock_guard<std::mutex> lock(mutex_);if(inflight_ || sealed_)return false;stopping_=false;revoke_locked(ContactReason::lifecycle_start,now);return !sealed_;}
    ContactDiagnostics diagnostics() const {std::lock_guard<std::mutex> lock(mutex_);return contact_diagnostics_;}
    bool quiescent() const {std::lock_guard<std::mutex> lock(mutex_);return inflight_==0;}
    bool in_callback() const noexcept {for(const auto& slot:local_)if(slot.owner==this && slot.count)return true;return false;}
    void observe(const AnatomicalContactOwner& owner) {
        const auto now=clock_();
        std::lock_guard<std::mutex> lock(mutex_);if(stopping_ || sealed_)return;
        if(owner_!=owner) {revoke_locked(ContactReason::owner_changed,now);owner_=owner;}
    }
    AnatomicalContactOwner owner() const {std::lock_guard<std::mutex> lock(mutex_);return owner_;}
    std::optional<ProducerReceipt> producer(unsigned side) const {
        std::lock_guard<std::mutex> lock(mutex_);
        if(side>1 || stopping_ || sealed_ || !owner_.valid())return {};
        return ProducerReceipt{owner_,epoch_,side_epoch_[side],side};
    }
    bool producer_current(const ProducerReceipt& receipt) const {
        std::lock_guard<std::mutex> lock(mutex_);return producer_current_locked(receipt);
    }
    std::optional<Sample> latest(unsigned side) const {std::lock_guard<std::mutex> lock(mutex_);return side<2?latest_[side]:std::nullopt;}
    bool publish(Sample sample,std::uint64_t now,ProducerReceipt* admitted=nullptr) {
        if(admitted)*admitted={};
        now=timestamp(now);
        const auto side=sample.side;const auto& t=sample.tracking;
        {std::lock_guard<std::mutex> lock(mutex_);
            if(sample.producer.side!=side || sample.producer.owner!=sample.owner || !producer_current_locked(sample.producer))return false;
        }
        const auto reject=[&] {
            if(side<2) {
                std::lock_guard<std::mutex> lock(mutex_);
                if(producer_current_locked(sample.producer) && (!latest_[side] || sample.frame>=latest_[side]->frame)) {
                    contact_destroyed_locked(side,ContactReason::publish_invalid,now);
                    pending_[side].reset();latest_[side].reset();response_[side].reset();++side_epoch_[side];
                }
            }
            return false;
        };
        if(side>1 || !sample.binding || !sample.frame || !sample.prepared || sample.prepared->side()!=side || !sample.owner.valid() ||
           !sample.source.valid() || !fresh(sample,now) || !t.pose_epoch || !t.reference_epoch || !t.mapping_revision ||
           !(t.tracked_hands&t.raw_grip_hands&(1u<<side)) || !t.hand_empty[side] || !body::valid_pose(t.raw_grip[side]) ||
           sample.fit_revision!=sample.prepared->original().fit_revision || !anatomical_shape_valid(sample.proxies) ||
           sample.owner.game!=sample.source.game || sample.owner.player!=sample.source.player || sample.owner.camera!=sample.source.camera ||
           sample.owner.world!=sample.source.world || sample.owner.spawn!=sample.source.spawn || sample.owner.camera_identity!=sample.source.camera_identity ||
           sample.binding->arms[side].player!=sample.owner.player)return reject();
        const auto& original=sample.prepared->original();const auto& captured=original.tracking;
        auto expected=sample;expected.tracking=captured;
        const auto expected_raw=body::mul(original.world_from_solve,captured.raw_grip[side]);
        const auto& provided=t.raw_grip[side];
        if(!compatible(sample,expected) || t.tick!=captured.tick || t.source_tick!=captured.source_tick ||
           t.tracking_revision!=captured.tracking_revision || t.native_frame!=captured.native_frame ||
           !body::valid_pose(expected_raw) || length(expected_raw.p-provided.p)>1e-8 ||
           length(rotated(expected_raw.q,{1,0,0})-rotated(provided.q,{1,0,0}))>1e-8 ||
           length(rotated(expected_raw.q,{0,1,0})-rotated(provided.q,{0,1,0}))>1e-8)return reject();
        const auto geometry=HandPaletteContact::bind(sample.binding->bindings[side]);
        if(!geometry || !sample.prepared->original().raw_joints)return reject();
        const auto actual=geometry->proxies(*sample.prepared->original().raw_joints,sample.prepared->model_frame());
        if(!actual || !FullHandQueryTransaction::same_geometry(*actual,sample.proxies))return reject();
        std::lock_guard<std::mutex> lock(mutex_);
        if(!producer_current_locked(sample.producer) || owner_!=sample.owner || sample.frame<=last_frame_[side])return false;
        if(latest_[side] && !compatible(*latest_[side],sample)) {
            contact_destroyed_locked(side,ContactReason::publish_incompatible,now,compatibility_bits(*latest_[side],sample));
            response_[side].reset();++side_epoch_[side];
        }
        last_frame_[side]=sample.frame;latest_[side]=sample;pending_[side]=std::move(sample);
        if(admitted)*admitted=ProducerReceipt{owner_,epoch_,side_epoch_[side],side};
        return true;
    }
    void revoke(unsigned side) {
        if(side>1)return;const auto now=clock_();std::lock_guard<std::mutex> lock(mutex_);
        contact_destroyed_locked(side,ContactReason::explicit_revoke,now);
        pending_[side].reset();latest_[side].reset();response_[side].reset();++side_epoch_[side];
    }
    std::optional<Receipt> candidate(unsigned side,std::uint64_t now) const {
        if(side>1)return {};now=timestamp(now);std::lock_guard<std::mutex> lock(mutex_);
        Receipt value(this,response_[side]);const auto current=current_locked(value,now);
        if(auto* record=contact_record_locked(side)) {
            if(current) {if(!record->selected_tick)record->selected_tick=now;}
            else if(record->lookup.reason==ContactReason::none) {
                record->lookup=contact_event_locked(side,candidate_failure_locked(*value.value_,now),now);
                if(latest_[side])record->lookup.mismatch_bits=compatibility_bits(value.value_->sample,*latest_[side]);
            }
        }
        return current?std::optional<Receipt>{std::move(value)}:std::nullopt;
    }
    bool current(const Receipt& receipt,std::uint64_t now) const {now=timestamp(now);std::lock_guard<std::mutex> lock(mutex_);return current_locked(receipt,now);}
private:
    bool acquire_impl(std::uint64_t receipt,std::uint64_t now,bool continuation_only) noexcept {
        try {
            now=timestamp(now);std::lock_guard<std::mutex> lock(mutex_);std::shared_ptr<const Response> value;
            if(continuation_only && !continuation(receipt))return false;
            if(!continuation_only)for(const auto& response:response_)if(response && response->receipt==receipt && current_locked(Receipt(this,response),now))value=response;
            Active* entry{};for(auto& slot:active_)if(slot.receipt==receipt && slot.count) {entry=&slot;break;}
            if(continuation_only && entry)value=entry->value;
            if(!value)return false;
            if(!entry)for(auto& slot:active_)if(!slot.count) {entry=&slot;break;}
            if(!entry || !enter_local(receipt))return false;
            entry->receipt=receipt;entry->value=std::move(value);++entry->count;++inflight_;return true;
        }catch(...) {return false;}
    }
    void release_impl(std::uint64_t receipt,bool committed) noexcept {
        try {std::lock_guard<std::mutex> lock(mutex_);for(auto& slot:active_)if(slot.receipt==receipt && slot.count) {
            --slot.count;--inflight_;leave_local(receipt,committed);if(!slot.count)slot.value.reset();return;
        }}catch(...) {}
    }
    bool commit(const Receipt& receipt,std::uint64_t now) {
        now=timestamp(now);std::lock_guard<std::mutex> lock(mutex_);if(!current_locked(receipt,now))return false;
        for(auto& slot:local_)if(slot.owner==this && slot.receipt==receipt.id() && slot.count) {++slot.committed;return true;}
        return false;
    }
public:
    bool acquire(std::uint64_t receipt,std::uint64_t now) noexcept {return acquire_impl(receipt,now,false);}
    void release(std::uint64_t receipt) noexcept {release_impl(receipt,false);}
    static bool acquire_attachment(void* context,std::uint64_t receipt) noexcept {
        if(!context)return false;auto& self=*static_cast<NativeFullHandContact*>(context);
        // Independent attachment reads have no live source/target admission.
        const auto admitted=self.acquire_impl(receipt,0,true);
        if(admitted)++self.attachment_continued;else ++self.attachment_rejected;
        return admitted;
    }
    static void release_attachment(void* context,std::uint64_t receipt) noexcept {
        if(context)static_cast<NativeFullHandContact*>(context)->release(receipt);
    }
private:
    template<class Cast,class Validate,class Light,class Watch> struct ServiceFrame {
        NativeFullHandContact* self;AnatomicalContactOwner owner;std::uint64_t now,epoch{};
        Cast cast;Validate validate;Light light;Watch watch;
        std::vector<FullHandTargetWitness::Witness> raw_targets,final_targets;
        WorldTraceQueryResult query;
        std::array<std::optional<Sample>,2> work{};
        std::array<std::uint64_t,2> side_epoch{};
        FullHandQueryTransaction::Working transaction;
        std::array<std::shared_ptr<Response>,2> staged{};
        std::chrono::steady_clock::time_point began=std::chrono::steady_clock::now();
        bool finished{},budget_hit{};
        template<class C,class V,class L,class W>ServiceFrame(NativeFullHandContact* s,const AnatomicalContactOwner& o,std::uint64_t n,C&& c,V&& v,L&& l,W&& w):
            self(s),owner(o),now(n),cast(std::forward<C>(c)),validate(std::forward<V>(v)),light(std::forward<L>(l)),watch(std::forward<W>(w)) {}
    };
    template<class F>void service_impl(F& f) {
        bool valid=true;
        for(unsigned side=0;side<2 && valid;++side)if(f.work[side]) {
            const auto& sample=*f.work[side];
            const auto live=[&] {
                const auto budget=budget_ns.load(std::memory_order_relaxed);
                if(budget && static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-f.began).count())>budget) {
                    f.budget_hit=true;return false;
                }
                return sample_current(sample,f.epoch,f.side_epoch[side],timestamp(f.now)) && f.light(sample);
            };
            valid=sample_current(sample,f.epoch,f.side_epoch[side],timestamp(f.now)) && f.validate(sample) && live();
            const auto geometry=HandPaletteContact::bind(sample.binding->bindings[side]);
            if(!valid || !geometry || !sample.prepared || !sample.prepared->original().raw_joints) {valid=false;break;}
            f.raw_targets.clear();f.final_targets.clear();unsigned cast_index{};
            const auto targets_current=[&](const auto& targets) {
                for(const auto& target:targets) {
                    const auto current=f.watch(sample,target.handle,target.contact_key);
                    if(!current || !FullHandTargetWitness::equal(target,*current))return false;
                }
                return true;
            };
            auto cast=[&](Vec3 from,Vec3 to,double radius) {
                ++casts;f.query=f.cast(sample,from,to,radius);auto& query=f.query;
                auto& targets=cast_index++<hand_proxy_count?f.raw_targets:f.final_targets;
                const auto witness=[&](std::uint32_t body,std::uint32_t key) {
                    const auto value=f.watch(sample,body,key);
                    if(!value || value->source!=sample.source || value->handle!=body || value->contact_key!=key)return false;
                    for(const auto& previous:targets)if(previous.handle==body && previous.contact_key==key)
                        return FullHandTargetWitness::equal(previous,*value);
                    targets.push_back(*value);return true;
                };
                if(!query.complete || !query.initial || !query.regular || query.initial->count>query.initial->hits.size() ||
                   query.regular->count>query.regular->hits.size()) {query.complete=false;return std::move(query);}
                for(unsigned i=0;i<query.initial->count;++i) {
                    const auto& hit=query.initial->hits[i].contact;
                    if(!witness(hit.body,hit.shape_key)) {query.complete=false;return std::move(query);}
                }
                for(unsigned i=0;i<query.regular->count;++i) {
                    const auto& hit=query.regular->hits[i];
                    if(!witness(hit.body_handle,hit.shape_key)) {query.complete=false;return std::move(query);}
                }
                return std::move(query);
            };
            auto solve=[&](const WorldTrackedPose& target)->std::optional<FullHandQueryTransaction::Palette> {
                if(!targets_current(f.raw_targets))return {};
                const auto pose=sample.prepared->solve_world_grip(fullbody::from_basis(target.position,target.basis[0],target.basis[1],target.basis[2]),timestamp(f.now));
                return pose && pose->reached?std::optional<FullHandQueryTransaction::Palette>{pose->joints}:std::nullopt;
            };
            auto fit=[&](const FullHandQueryTransaction::Palette& palette) {return geometry->proxies(palette,sample.prepared->model_frame());};
            FullHandQueryTransaction::run(raw(sample),*sample.prepared->original().raw_joints,sample.proxies,sample.source,cast,solve,fit,live,f.transaction);
            ++queries;++states[static_cast<unsigned>(f.transaction.result.state)];
            valid=f.transaction.result.ready() && targets_current(f.raw_targets) && targets_current(f.final_targets) && live();
            if(valid) {
                f.transaction.result.raw_targets=std::move(f.raw_targets);f.transaction.result.final_targets=std::move(f.final_targets);
                f.staged[side]=std::make_shared<Response>(Response{sample,std::move(f.transaction.result),f.now,0,f.epoch});
            }
        }
        for(unsigned side=0;side<2 && valid;++side)if(f.work[side]) {
            valid=sample_current(*f.work[side],f.epoch,f.side_epoch[side],timestamp(f.now)) && f.light(*f.work[side]) && f.validate(*f.work[side]) &&
                sample_current(*f.work[side],f.epoch,f.side_epoch[side],timestamp(f.now));
            if(valid)for(const auto* targets:{&f.staged[side]->result.raw_targets,&f.staged[side]->result.final_targets})
                for(const auto& target:*targets) {
                    const auto current=f.watch(*f.work[side],target.handle,target.contact_key);
                    if(!current || !FullHandTargetWitness::equal(target,*current))valid=false;
                }
            valid=valid && sample_current(*f.work[side],f.epoch,f.side_epoch[side],timestamp(f.now));
        }
        const auto budget=budget_ns.load(std::memory_order_relaxed);
        if(budget && static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-f.began).count())>budget) {
            f.budget_hit=true;valid=false;
        }
        // Publish the batch only after all full pre/post checks. A later failed
        // side cannot leave an earlier side's partially admitted response.
        const auto diagnostic_now=timestamp(f.now);
        std::lock_guard<std::mutex> lock(mutex_);
        if(epoch_!=f.epoch || owner_!=f.owner || stopping_ || sealed_) {f.finished=true;return;}
        for(unsigned side=0;side<2;++side)if(f.work[side])
            valid=valid && side_epoch_[side]==f.side_epoch[side] && latest_[side] && compatible(*f.work[side],*latest_[side]) &&
                fresh(*f.work[side],timestamp(f.now)) && fresh(*latest_[side],timestamp(f.now));
        for(unsigned side=0;side<2;++side)if(f.work[side]) {
            if(valid && f.staged[side] && next_receipt_!=UINT64_MAX) {
                bool borrowed{};
                if(response_[side])for(const auto& slot:active_)
                    borrowed=borrowed || (slot.count && slot.receipt==response_[side]->receipt);
                // Keep skinning and nested attachment readers on the same
                // admitted palette. A fresh stage may be dropped without waiting.
                if(borrowed) {++deferred;continue;}
                contact_destroyed_locked(side,f.staged[side]->result.contact?ContactReason::replaced_contact:ContactReason::replaced_clear,diagnostic_now);
                f.staged[side]->receipt=++next_receipt_;if(f.staged[side]->result.contact)++contacts;
                response_[side]=std::move(f.staged[side]);
                contact_published_locked(side,diagnostic_now);
            }else {
                unsigned work_mask{};for(unsigned lane=0;lane<2;++lane)if(f.work[lane])work_mask|=1u<<lane;
                contact_destroyed_locked(side,next_receipt_==UINT64_MAX?ContactReason::receipt_exhausted:ContactReason::batch_rejected,
                    diagnostic_now,0,f.transaction.result.state,work_mask,f.budget_hit);
                ++rejected;response_[side].reset();if(next_receipt_==UINT64_MAX)sealed_=true;
            }
        }
        f.finished=true;
    }
    template<class F>static void service_cleanup(void* raw) noexcept {
        auto* f=static_cast<F*>(raw);auto* self=f->self;
        const auto diagnostic_now=self->timestamp(f->now);
        {
            std::lock_guard<std::mutex> lock(self->mutex_);
            if(!f->finished && self->epoch_==f->epoch)for(unsigned side=0;side<2;++side)if(f->work[side] && self->side_epoch_[side]==f->side_epoch[side]) {
                unsigned work_mask{};for(unsigned lane=0;lane<2;++lane)if(f->work[lane])work_mask|=1u<<lane;
                self->contact_destroyed_locked(side,ContactReason::service_exception,diagnostic_now,0,f->transaction.result.state,work_mask,f->budget_hit);
                self->response_[side].reset();++self->rejected;
            }
            --self->inflight_;self->leave_local();
        }
        const auto elapsed=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-f->began).count());
        ++self->batches;self->service_ns+=elapsed;auto peak=self->worst_service_ns.load();
        if(f->budget_hit)++self->budget_rejected;
        while(elapsed>peak && !self->worst_service_ns.compare_exchange_weak(peak,elapsed)) {}
        delete f;
    }
public:
    template<class Cast,class Validate,class Light,class Watch>void service(const AnatomicalContactOwner& owner,std::uint64_t now,Cast&& cast,Validate&& validate,Light&& light,Watch&& watch) {
        if(in_callback() || servicing_.test_and_set(std::memory_order_acquire))return;
        using F=ServiceFrame<std::decay_t<Cast>,std::decay_t<Validate>,std::decay_t<Light>,std::decay_t<Watch>>;
        F* f{};try {f=new F(this,owner,now,std::forward<Cast>(cast),std::forward<Validate>(validate),std::forward<Light>(light),std::forward<Watch>(watch));}
        catch(...) {servicing_.clear(std::memory_order_release);throw;}
        bool admitted{};
        {std::lock_guard<std::mutex> lock(mutex_);
            if(!stopping_ && !sealed_ && owner_==owner && (pending_[0] || pending_[1]) && enter_local()) {
                ++inflight_;f->work=pending_;pending_={};f->epoch=epoch_;f->side_epoch=side_epoch_;admitted=true;
            }
        }
        if(!admitted) {delete f;servicing_.clear(std::memory_order_release);return;}
        const auto invoke=[](void* context)->unsigned {auto& value=*static_cast<F*>(context);value.self->service_impl(value);return 1;};
        AnatomicalWorldContact::invoke_seh_cleanup(&servicing_,invoke,f,&service_cleanup<F>);
    }
    template<class Cast,class Validate,class Light>void service(const AnatomicalContactOwner& owner,std::uint64_t now,Cast&& cast,Validate&& validate,Light&& light) {
        service(owner,now,std::forward<Cast>(cast),std::forward<Validate>(validate),std::forward<Light>(light),
            [](const Sample&,std::uint32_t,std::uint32_t)->std::optional<FullHandTargetWitness::Witness> {return {};});
    }
    template<class Cast,class Validate>void service(const AnatomicalContactOwner& owner,std::uint64_t now,Cast&& cast,Validate&& validate) {
        service(owner,now,std::forward<Cast>(cast),std::forward<Validate>(validate),[](const Sample&) {return true;});
    }
    template<class Validate,class Invoke>bool consume(const Receipt& receipt,std::uint64_t now,Validate&& validate,Invoke&& callback) {
        if(in_callback())return false;
        using V=std::decay_t<Validate>;using I=std::decay_t<Invoke>;
        struct F {NativeFullHandContact* self;Receipt receipt;std::uint64_t now;V validate;I callback;bool admitted{},committed{};};
        auto* frame=new F{this,receipt,now,std::forward<Validate>(validate),std::forward<Invoke>(callback)};
        const auto invoke=[](void* raw)->unsigned {auto& f=*static_cast<F*>(raw);
            if(!f.receipt || !f.self->acquire_impl(f.receipt.id(),f.now,false))return 0;
            f.admitted=true;
            if(!f.validate(f.receipt.sample()) || !f.self->commit(f.receipt,f.now))return 0;
            f.committed=true;
            f.callback(f.receipt);++f.self->consumed;return 1;
        };
        const auto cleanup=[](void* raw) noexcept {auto* f=static_cast<F*>(raw);if(f->admitted)f->self->release_impl(f->receipt.id(),f->committed);delete f;};
        return AnatomicalWorldContact::invoke_seh_cleanup(nullptr,invoke,frame,cleanup)!=0;
    }
    // Pre-IK world-plane transport is deliberately unavailable. Hosts must use
    // candidate/consume and the tested immutable palette at the native consumer.
    template<class Validate>std::optional<Resolved> resolve_checked(unsigned,const WorldTrackedPose&,std::uint64_t,std::uint64_t,
        const std::shared_ptr<const rig::NativeArmBindingSnapshot>&,std::uint64_t,Validate) const {return {};}
    std::optional<Resolved> resolve(unsigned,const WorldTrackedPose&,std::uint64_t,std::uint64_t,
        const std::shared_ptr<const rig::NativeArmBindingSnapshot>&,std::uint64_t) const {return {};}
};
}
