#pragma once
#include "anatomical_world_contact.hpp"
#include "full_body_driver.hpp"
#include "native_arm_binding_snapshot.hpp"

namespace d2vr {
class AnatomicalRawPair {
public:
    using Store=AnatomicalWorldContact;
    struct Publication {
        Store::AdmissionReceipt admission{};
        NativeWorldQuerySource source{};
        std::shared_ptr<const rig::NativeArmBindingSnapshot> binding;
        fullbody::Tracking tracking{};
        HandProxies proxies{};
        std::array<float,15> model_frame{};
        body::Xf world_from_solve{};
        std::uint64_t frame{},solved_revision{},fit_revision{};
        unsigned side{};
        bool root_mapping_valid{};
    };
    struct Observation {
        Store::AdmissionReceipt admission{};
        Store::TokenPair tokens{};
        NativeWorldQuerySource source{};
        std::uint64_t revision{},tick{},source_tick{},pose_epoch{},frame{},tracking_revision{},fit_revision{};
        std::array<rig::Arm,2> arms{};
        std::array<std::array<float,15>,2> model_frames{};
        body::Xf world_from_solve{};
    };
    struct Diagnostic {
        std::uint64_t admission_epoch{},revision{},tracking_revision{},frame{},request_tick{},source_tick{},query_tick{},lifecycle_revision{},picked{};
        std::uint32_t source_body{};
        std::array<unsigned,2> state{},casts{},witnesses{};
        std::array<Vec3,2> shift{};
        bool valid{},empty_verified_at_query{};
    };
private:
    mutable std::mutex mutex_;
    std::array<std::optional<Publication>,2> slots_{};
    std::optional<Observation> observation_;
    std::uint64_t revision_{},last_attempt_{};
    std::uint64_t frontier_epoch_{},last_frame_{},last_solved_revision_{};
    bool enabled_{},pending_{},exhausted_{};
    static bool same_pose(const body::Xf& first,const body::Xf& second) noexcept {
        return first.p.x==second.p.x && first.p.y==second.p.y && first.p.z==second.p.z &&
            first.q.x==second.q.x && first.q.y==second.q.y && first.q.z==second.q.z && first.q.w==second.q.w;
    }
    static bool same_admission(const Store::AdmissionReceipt& first,const Store::AdmissionReceipt& second) noexcept {
        return first.epoch==second.epoch && first.owners==second.owners;
    }
    static bool fresh(const Publication& value,std::uint64_t now) noexcept {
        const auto& tracking=value.tracking;
        if(value.side>1 || !value.frame || !value.solved_revision || !value.binding || !value.root_mapping_valid ||
           !body::valid_pose(value.world_from_solve) || !value.admission.epoch || !value.source.valid() ||
           !tracking.tick || !tracking.source_tick || !tracking.tracking_revision || !tracking.pose_epoch || !tracking.reference_epoch ||
           now<tracking.tick || now<tracking.source_tick || now-tracking.tick>Store::max_age || now-tracking.source_tick>Store::max_age ||
           (tracking.tracked_hands&tracking.raw_grip_hands&3u)!=3u || !tracking.grip_valid[value.side] ||
           !tracking.hand_empty[value.side] || !body::valid_pose(tracking.raw_grip[value.side]) || !anatomical_shape_valid(value.proxies))return false;
        const auto& owner=value.admission.owners[value.side];
        if(!owner.valid() || owner!=value.admission.owners[1-value.side] || owner.game!=value.source.game || owner.player!=value.source.player ||
           owner.world!=value.source.world || owner.camera!=value.source.camera || owner.spawn!=value.source.spawn || owner.camera_identity!=value.source.camera_identity ||
           value.binding->arms[value.side].side!=value.side || value.binding->arms[0].player!=owner.player || value.binding->arms[1].player!=owner.player)return false;
        try {(void)fullbody::frame_of(value.model_frame);return true;}catch(...) {return false;}
    }
    static bool same_sample(const Publication& first,const Publication& second) noexcept {
        const auto& a=first.tracking;const auto& b=second.tracking;
        return same_admission(first.admission,second.admission) && first.source==second.source && first.binding==second.binding &&
            first.frame==second.frame && first.solved_revision==second.solved_revision && first.fit_revision==second.fit_revision &&
            same_pose(first.world_from_solve,second.world_from_solve) && a.tick==b.tick && a.source_tick==b.source_tick &&
            a.epoch==b.epoch && a.pose_epoch==b.pose_epoch && a.reference_epoch==b.reference_epoch &&
            a.mapping_revision==b.mapping_revision && a.tracking_revision==b.tracking_revision && a.native_frame==b.native_frame;
    }
    static bool same_geometry(const Publication& first,const Publication& second) noexcept {
        if(first.model_frame!=second.model_frame)return false;
        for(unsigned proxy=0;proxy<hand_proxy_count;++proxy)
            if(first.proxies[proxy].centre.x!=second.proxies[proxy].centre.x || first.proxies[proxy].centre.y!=second.proxies[proxy].centre.y ||
               first.proxies[proxy].centre.z!=second.proxies[proxy].centre.z || first.proxies[proxy].radius!=second.proxies[proxy].radius)return false;
        return true;
    }
    void reset_locked() noexcept {slots_={};observation_.reset();pending_=false;}
    void cancel_locked(const Store::AdmissionReceipt& receipt,Store& store) {
        reset_locked();(void)store.post_batch_current(receipt,{});
    }
public:
    bool enabled() const {std::lock_guard<std::mutex> lock(mutex_);return enabled_ && !exhausted_;}
    bool wants_publication(const Store::AdmissionReceipt& receipt,std::uint64_t frame,std::uint64_t solved_revision,unsigned side,std::uint64_t now) const {
        std::lock_guard<std::mutex> lock(mutex_);
        if(!enabled_ || exhausted_ || side>1 || !receipt.epoch || !frame || !solved_revision)return false;
        if(frontier_epoch_!=receipt.epoch)return true;
        if(frame<last_frame_ || solved_revision<last_solved_revision_)return false;
        if(pending_ || (observation_ && slots_[side] && slots_[side]->frame==frame))return true;
        return !last_attempt_ || (now>=last_attempt_ && now-last_attempt_>=100);
    }
    void configure(bool enabled,Store& store) {
        std::lock_guard<std::mutex> lock(mutex_);enabled_=enabled && !exhausted_;reset_locked();last_attempt_=0;store.clear();
    }
    void clear(Store& store) {
        std::lock_guard<std::mutex> lock(mutex_);reset_locked();last_attempt_=0;store.clear();
    }
    void stop(Store& store) {
        std::lock_guard<std::mutex> lock(mutex_);enabled_=false;reset_locked();last_attempt_=0;store.stop();
    }
    bool publish(Publication value,Store& store,std::uint64_t now) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(!enabled_ || exhausted_)return false;
        const auto current=store.admission();
        if(!current || !same_admission(*current,value.admission))return false;
        if(frontier_epoch_==value.admission.epoch && (value.frame<last_frame_ || value.solved_revision<last_solved_revision_))return false;
        if(!fresh(value,now)) {cancel_locked(value.admission,store);return false;}
        if(frontier_epoch_!=value.admission.epoch) {
            reset_locked();last_attempt_=last_frame_=last_solved_revision_=0;frontier_epoch_=value.admission.epoch;
        }
        if(value.frame<last_frame_ || value.solved_revision<last_solved_revision_)return false;
        last_frame_=value.frame;last_solved_revision_=value.solved_revision;
        if(!pending_ && observation_ && slots_[value.side] && slots_[value.side]->frame==value.frame) {
            if(!same_sample(*slots_[value.side],value) || !same_geometry(*slots_[value.side],value))cancel_locked(value.admission,store);
            return false;
        }
        if(pending_) {
            const auto& first=slots_[0]?*slots_[0]:*slots_[1];
            if(!same_sample(first,value)) {cancel_locked(value.admission,store);return false;}
        } else {
            if(last_attempt_ && (now<last_attempt_ || now-last_attempt_<100))return false;
            cancel_locked(value.admission,store);last_attempt_=now;
            if(revision_==UINT64_MAX) {exhausted_=true;enabled_=false;store.clear();return false;}
            ++revision_;pending_=true;
        }
        if(slots_[value.side]) {
            if(!same_geometry(*slots_[value.side],value))cancel_locked(value.admission,store);
            return false;
        }
        slots_[value.side]=std::move(value);
        if(!slots_[0] || !slots_[1])return false;
        Store::RequestPair requests{};
        const auto& sample=*slots_[0];
        Observation next{};next.admission=sample.admission;next.source=sample.source;next.revision=revision_;
        next.tick=sample.tracking.tick;next.source_tick=sample.tracking.source_tick;next.pose_epoch=sample.tracking.pose_epoch;
        next.frame=sample.frame;next.tracking_revision=sample.tracking.tracking_revision;next.fit_revision=sample.fit_revision;
        next.arms=sample.binding->arms;next.world_from_solve=sample.world_from_solve;
        for(unsigned side=0;side<2;++side) {
            const auto& input=*slots_[side];auto& request=requests[side].emplace();
            next.model_frames[side]=input.model_frame;
            request.token={input.admission.owners[side],input.tracking.pose_epoch,revision_,input.admission.epoch,revision_,side,input.admission.epoch};
            request.source=input.source;request.tick=input.tracking.tick;request.to=input.proxies;request.local_probe=true;
            for(unsigned proxy=0;proxy<hand_proxy_count;++proxy)
                request.from[proxy]={request.to[proxy].centre+Vec3{0,0,.0015},request.to[proxy].radius};
            next.tokens[side]=request.token;
        }
        if(!store.post_batch_current(next.admission,requests)) {reset_locked();return false;}
        observation_=next;pending_=false;return true;
    }
    std::optional<Observation> observation(const Store& store,std::uint64_t now) const {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto receipt=store.admission();
        if(!enabled_ || !observation_ || now<observation_->tick || now<observation_->source_tick ||
           now-observation_->tick>Store::max_age || now-observation_->source_tick>Store::max_age ||
           !receipt || !same_admission(*receipt,observation_->admission))return {};
        return observation_;
    }
    bool current(const Observation& expected,const Store& store,std::uint64_t now) const {
        const auto value=observation(store,now);
        return value && value->revision==expected.revision && value->tokens==expected.tokens && same_admission(value->admission,expected.admission) &&
            value->source==expected.source && value->tick==expected.tick && value->source_tick==expected.source_tick && value->pose_epoch==expected.pose_epoch &&
            value->frame==expected.frame && value->tracking_revision==expected.tracking_revision && value->fit_revision==expected.fit_revision &&
            value->arms==expected.arms && value->model_frames==expected.model_frames && same_pose(value->world_from_solve,expected.world_from_solve);
    }
};
}
