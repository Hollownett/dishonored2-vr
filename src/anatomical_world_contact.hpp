#pragma once
#include "hand_contact.hpp"
#include "native_world_trace.hpp"
#include <algorithm>
#include <atomic>
#include <mutex>
#include <type_traits>
#include <memory>

namespace d2vr {
struct AnatomicalContactOwner {
    std::uintptr_t game{},player{},world{},camera{},sensor{};
    std::uint32_t spawn{};
    rig::Identity camera_identity{},sensor_identity{};
    bool valid() const {return game && player && world && camera && sensor && spawn;}
};
inline bool operator==(const AnatomicalContactOwner& first,const AnatomicalContactOwner& second) {
    return first.game==second.game && first.player==second.player && first.world==second.world &&
        first.camera==second.camera && first.sensor==second.sensor && first.spawn==second.spawn &&
        first.camera_identity.generation==second.camera_identity.generation && first.camera_identity.flags==second.camera_identity.flags &&
        first.sensor_identity.generation==second.sensor_identity.generation && first.sensor_identity.flags==second.sensor_identity.flags;
}
inline bool operator!=(const AnatomicalContactOwner& first,const AnatomicalContactOwner& second) {return !(first==second);}
struct AnatomicalContactToken {
    AnatomicalContactOwner owner{};
    std::uint64_t pose_epoch{},geometry_epoch{},ownership_epoch{},request_revision{};
    unsigned side{};
    std::uint64_t admission_epoch{};
};
inline bool operator==(const AnatomicalContactToken& first,const AnatomicalContactToken& second) {
    return first.owner==second.owner && first.pose_epoch==second.pose_epoch && first.geometry_epoch==second.geometry_epoch &&
        first.ownership_epoch==second.ownership_epoch && first.request_revision==second.request_revision && first.side==second.side &&
        first.admission_epoch==second.admission_epoch;
}
enum class AnatomicalContactState {ready,retained,acquisition_required,invalid,occluded,limited};
struct AnatomicalContactRequest {
    AnatomicalContactToken token{};
    HandProxies from{},to{};
    std::uint64_t tick{};
    std::uintptr_t held_entity{};
    bool safe_from{};
    bool safe_anchor_acquisition{};
    std::array<bool,hand_proxy_count> acquire{};
    bool local_probe{};
    NativeWorldQuerySource source{};
};
struct AnatomicalContactWitness {
    ContactSphere from{},to{};
    Vec3 point{},normal{};
    double depth{},fraction{};
    float signed_separation{};
    std::uint32_t source_body{},body{},shape_key{};
    std::uintptr_t entity{};
    unsigned proxy{};
    bool initial{},initial_overlap{},normal_valid{},contributes{};
};
class AnatomicalContactWitnessBundle {
    friend class AnatomicalLocalContactReducer;
    std::array<AnatomicalContactWitness,hand_proxy_count*64> entries_{};
    unsigned count_{};
public:
    inline static constexpr unsigned capacity=hand_proxy_count*64;
    unsigned size() const noexcept {return count_;}
    std::optional<AnatomicalContactWitness> at(unsigned index) const noexcept {
        return index<count_?std::optional<AnatomicalContactWitness>{entries_[index]}:std::nullopt;
    }
};
struct AnatomicalContactResult {
    AnatomicalContactToken token{};
    AnatomicalContactState state{AnatomicalContactState::invalid};
    Vec3 shift{},point{},normal{};
    double depth{};
    std::uintptr_t entity{};
    std::uint64_t tick{},query_tick{};
    unsigned proxy{},casts{};
    bool contact{};
    bool local_probe{};
    std::array<std::optional<WorldTraceHit>,hand_proxy_count> hits{};
    std::uint64_t request_tick{};
    std::shared_ptr<const AnatomicalContactWitnessBundle> witnesses;
    NativeWorldQuerySource source{};
    bool actionable() const {return state==AnatomicalContactState::ready && contact;}
};
inline bool anatomical_shape_valid(const HandProxies& shape) {
    for(const auto& sphere:shape) if(!finite(sphere.centre) || !std::isfinite(sphere.radius) || sphere.radius<=.0005 || sphere.radius>.15)return false;
    return true;
}
inline AnatomicalContactResult anatomical_contact_reduce(const AnatomicalContactRequest& request,
        const std::array<std::optional<WorldTraceHit>,hand_proxy_count>& hits,double max_shift=.45) {
    AnatomicalContactResult result{};result.token=request.token;result.hits=hits;result.request_tick=request.tick;result.source=request.source;
    if(!anatomical_shape_valid(request.to) || !std::isfinite(max_shift) || max_shift<=0 || max_shift>.45)return result;
    std::array<double,hand_proxy_count> depths{};
    for(unsigned proxy=0;proxy<hand_proxy_count;++proxy) if(const auto& hit=hits[proxy]) {
        if(request.held_entity && hit->entity==request.held_entity) {result.state=AnatomicalContactState::occluded;return result;}
        const double normal_length=length(hit->normal);
        if(!hit->normal_valid || hit->initial_overlap || !finite(hit->point) || !finite(hit->normal) ||
           std::abs(normal_length-1.)>.01 || !std::isfinite(hit->fraction) || hit->fraction<0 || hit->fraction>1)return result;
        const double depth=request.to[proxy].radius-dot(request.to[proxy].centre-hit->point,hit->normal);
        if(!std::isfinite(depth))return result;
        depths[proxy]=std::max(0.,depth);
        if(depths[proxy]>result.depth) {
            result.contact=true;result.depth=depths[proxy];result.point=hit->point;result.normal=hit->normal;
            result.entity=hit->entity;result.proxy=proxy;
        }
    }
    for(unsigned pass=0;pass<8;++pass) {
        bool satisfied=true;
        for(unsigned proxy=0;proxy<hand_proxy_count;++proxy) if(const auto& hit=hits[proxy];hit && depths[proxy]>0) {
            const double remaining=depths[proxy]-dot(result.shift,hit->normal);
            if(remaining>1e-6) {result.shift=result.shift+hit->normal*remaining;satisfied=false;}
            if(length(result.shift)>max_shift) {result.shift=unit(result.shift)*max_shift;result.state=AnatomicalContactState::limited;return result;}
        }
        if(satisfied) {result.state=AnatomicalContactState::ready;return result;}
    }
    result.state=AnatomicalContactState::limited;return result;
}
class AnatomicalLocalContactReducer {
    const AnatomicalContactRequest& request_;
    AnatomicalContactResult result_{};
    std::shared_ptr<AnatomicalContactWitnessBundle> witnesses_;
    std::array<bool,hand_proxy_count> seen_{};
    unsigned seen_count_{},witness_limit_{};
    double max_shift_{};
    bool valid_{},finished_{};
    static bool body_valid(std::uint32_t body) {return (body&0xffffffu)!=0xffffffu;}
    bool reject(AnatomicalContactState state=AnatomicalContactState::invalid) {valid_=false;result_.state=state;return false;}
    bool add_witness(AnatomicalContactWitness witness) {
        if(!witnesses_ || !finite(witness.point) || !finite(witness.normal) || !std::isfinite(witness.depth) ||
           !std::isfinite(witness.fraction) || !std::isfinite(witness.signed_separation) ||
           !body_valid(witness.body) || !body_valid(witness.source_body) || witness.body==witness.source_body ||
           witness.source_body!=request_.source.handle || (witness.contributes && std::abs(length(witness.normal)-1.)>.01))return reject();
        if(request_.held_entity && witness.entity==request_.held_entity)return reject(AnatomicalContactState::occluded);
        if(witnesses_->count_>=witness_limit_)return reject();
        witnesses_->entries_[witnesses_->count_++]=witness;
        if(witness.contributes && witness.depth>result_.depth) {
            result_.contact=true;result_.depth=witness.depth;result_.point=witness.point;result_.normal=witness.normal;
            result_.entity=witness.entity;result_.proxy=witness.proxy;
        }
        return true;
    }
    AnatomicalContactResult seal(AnatomicalContactState state) {
        result_.state=state;result_.witnesses=std::move(witnesses_);finished_=true;return result_;
    }
public:
    AnatomicalLocalContactReducer(const AnatomicalLocalContactReducer&)=delete;
    AnatomicalLocalContactReducer& operator=(const AnatomicalLocalContactReducer&)=delete;
    explicit AnatomicalLocalContactReducer(const AnatomicalContactRequest& request,double max_shift=.08,
            unsigned witness_limit=AnatomicalContactWitnessBundle::capacity):request_(request),witness_limit_(witness_limit),max_shift_(max_shift) {
        result_.token=request.token;result_.local_probe=true;result_.request_tick=request.tick;result_.source=request.source;
        valid_=request.local_probe && anatomical_shape_valid(request.from) && anatomical_shape_valid(request.to) &&
            std::isfinite(max_shift) && max_shift>0 && max_shift<=.08 && request.source.valid() &&
            witness_limit && witness_limit<=AnatomicalContactWitnessBundle::capacity;
        for(unsigned proxy=0;proxy<hand_proxy_count && valid_;++proxy) {
            const auto distance=length(request.to[proxy].centre-request.from[proxy].centre);
            if(distance<.001 || distance>.0025 || request.to[proxy].radius!=request.from[proxy].radius)valid_=false;
        }
        if(valid_)try {witnesses_=std::make_shared<AnatomicalContactWitnessBundle>();}catch(...) {valid_=false;}
    }
    bool add(unsigned proxy,const WorldTraceQueryResult& query) {
        if(!valid_ || finished_)return false;
        if(proxy>=hand_proxy_count || seen_[proxy] || !query.complete || !query.initial || !query.regular ||
           query.initial->count>query.initial->hits.size() || query.regular->count>query.regular->hits.size())return reject();
        seen_[proxy]=true;++seen_count_;result_.hits[proxy]=query.hit;
        const auto delta=request_.to[proxy].centre-request_.from[proxy].centre;
        for(unsigned index=0;index<query.initial->count;++index) {
            const auto& hit=query.initial->hits[index];const auto& contact=hit.contact;
            if(!contact.normal_valid || !std::isfinite(contact.signed_separation) || contact.signed_separation>0 ||
               (contact.body&0xffffffu)==0xffffffu)return reject();
            if(!add_witness({request_.from[proxy],request_.to[proxy],contact.point,contact.normal,
                -static_cast<double>(contact.signed_separation)-dot(delta,contact.normal),0.,contact.signed_separation,
                contact.source_body,contact.body,contact.shape_key,hit.entity,proxy,true,true,contact.normal_valid,true}))return false;
        }
        for(unsigned index=0;index<query.regular->count;++index) {
            const auto& hit=query.regular->hits[index];
            if(!std::isfinite(hit.fraction) || hit.fraction<0 || hit.fraction>1 || hit.body!=(hit.body_handle&0xffffffu))return reject();
            if(request_.held_entity && hit.entity==request_.held_entity)return reject(AnatomicalContactState::occluded);
            if(hit.initial_overlap) {
                bool matched=false;
                for(unsigned initial_index=0;initial_index<query.initial->count;++initial_index) {
                    const auto& contact=query.initial->hits[initial_index].contact;
                    matched=matched || (contact.body==hit.body_handle && contact.shape_key==hit.shape_key);
                }
                if(!matched)return reject();
            }
            else if(!hit.normal_valid)return reject();
            const auto depth=hit.initial_overlap?0.:request_.to[proxy].radius-dot(request_.to[proxy].centre-hit.point,hit.normal);
            if(!add_witness({request_.from[proxy],request_.to[proxy],hit.point,hit.normal,depth,hit.fraction,0.f,
                request_.source.handle,hit.body_handle,hit.shape_key,hit.entity,proxy,false,hit.initial_overlap,hit.normal_valid,!hit.initial_overlap}))return false;
        }
        return true;
    }
    AnatomicalContactResult finish() {
        if(finished_)return result_;
        if(!valid_ || seen_count_!=hand_proxy_count) {finished_=true;witnesses_.reset();return result_;}
        for(unsigned pass=0;pass<8;++pass) {
            bool satisfied=true;
            for(unsigned index=0;index<witnesses_->count_;++index) {
                const auto& constraint=witnesses_->entries_[index];if(!constraint.contributes)continue;
                const auto remaining=constraint.depth-dot(result_.shift,constraint.normal);
                if(remaining>1e-6) {result_.shift=result_.shift+constraint.normal*remaining;satisfied=false;}
                if(length(result_.shift)>max_shift_) {
                    result_.shift=unit(result_.shift)*max_shift_;return seal(AnatomicalContactState::limited);
                }
            }
            if(satisfied)return seal(AnatomicalContactState::ready);
        }
        return seal(AnatomicalContactState::limited);
    }
};
class AnatomicalWorldContact {
    friend struct AnatomicalAdmissionFixture;
    struct Retained {AnatomicalContactRequest request;AnatomicalContactResult result;};
    struct Working {
        std::array<std::optional<AnatomicalContactRequest>,2> requests{};
        std::array<std::optional<Retained>,2> retained{};
        std::array<std::optional<AnatomicalContactResult>,2> staged{};
        AnatomicalContactResult value{};
        std::optional<Retained> previous;
        std::unique_ptr<AnatomicalLocalContactReducer> local;
    };
    struct Lane {
        std::optional<AnatomicalContactRequest> request;
        std::optional<AnatomicalContactResult> result;
        std::optional<Retained> retained;
        AnatomicalContactOwner owner{};
        std::uint64_t newest{},consumed{},geometry{},ownership{},reference{},revision{},admission{};
    };
    mutable std::mutex mutex_;
    std::atomic_flag servicing_=ATOMIC_FLAG_INIT;
    std::array<Lane,2> lanes_{};
    std::uint64_t revision_{};
    std::uint64_t service_turn_{};
    std::uint64_t admission_epoch_{1},inflight_{};
    bool revoking_{},stopping_{},sealed_{};
    struct CallbackFrame {AnatomicalWorldContact* self;CallbackFrame* previous;};
    inline static thread_local CallbackFrame* callback_frame_{};
    bool own_callback() const noexcept {
        for(auto* frame=callback_frame_;frame;frame=frame->previous)if(frame->self==this)return true;
        return false;
    }
    bool admission_open() const noexcept {return !stopping_ && !sealed_ && !revoking_;}
    void revoke_locked() noexcept {
        if(admission_epoch_==UINT64_MAX)sealed_=stopping_=true;else ++admission_epoch_;
        ++revision_;revoking_=inflight_!=0;
    }
    void leave_callback(CallbackFrame& frame) noexcept {
        callback_frame_=frame.previous;
        std::lock_guard<std::mutex> lock(mutex_);
        if(inflight_)--inflight_;
        if(!inflight_)revoking_=false;
    }
    bool token_admitted(const AnatomicalContactToken& token) const noexcept {
        return !token.admission_epoch || (admission_open() && token.admission_epoch==admission_epoch_);
    }
    static WorldTraceQueryResult normalize_query(WorldTraceQueryResult value) {return value;}
    static WorldTraceQueryResult normalize_query(std::optional<WorldTraceHit> value) {return {true,std::move(value)};}
    static bool valid_post(const Lane& lane,const AnatomicalContactRequest& request,unsigned side) {
        const bool new_epoch=request.token.geometry_epoch!=lane.geometry || request.token.ownership_epoch!=lane.ownership ||
            request.token.pose_epoch!=lane.reference || request.token.admission_epoch!=lane.admission;
        return request.token.side==side && request.token.owner.valid() && request.token.owner==lane.owner && request.token.pose_epoch &&
            request.token.request_revision && request.token.geometry_epoch && request.token.ownership_epoch && request.tick &&
            anatomical_shape_valid(request.from) && anatomical_shape_valid(request.to) && request.token.geometry_epoch>=lane.geometry &&
            request.token.ownership_epoch>=lane.ownership && request.token.pose_epoch>=lane.reference && (new_epoch || request.token.request_revision>lane.newest);
    }
    static void apply_post(Lane& lane,const AnatomicalContactRequest& request) {
        if(request.token.geometry_epoch!=lane.geometry || request.token.ownership_epoch!=lane.ownership || request.token.pose_epoch!=lane.reference ||
           request.token.admission_epoch!=lane.admission) {
            const auto lane_owner=lane.owner;const auto revision=lane.revision+1;lane={};lane.owner=lane_owner;lane.revision=revision;
            lane.geometry=request.token.geometry_epoch;lane.ownership=request.token.ownership_epoch;lane.reference=request.token.pose_epoch;lane.admission=request.token.admission_epoch;
        }
        lane.newest=request.token.request_revision;lane.request=request;lane.result.reset();
    }
    static void cancel(Lane& lane) {
        const auto lane_owner=lane.owner;const auto newest=lane.newest;const auto consumed=std::max(lane.consumed,newest);
        const auto geometry=lane.geometry,ownership=lane.ownership,reference=lane.reference,revision=lane.revision+1,admission=lane.admission;
        lane={};lane.owner=lane_owner;lane.newest=newest;lane.consumed=consumed;lane.geometry=geometry;
        lane.ownership=ownership;lane.reference=reference;lane.revision=revision;lane.admission=admission;
    }
    static bool same_batch(const AnatomicalContactRequest& first,const AnatomicalContactRequest& second) {
        return first.token.owner==second.token.owner && first.token.admission_epoch==second.token.admission_epoch && first.token.pose_epoch==second.token.pose_epoch &&
            first.token.request_revision==second.token.request_revision && first.tick==second.tick;
    }
public:
    using RequestPair=std::array<std::optional<AnatomicalContactRequest>,2>;
    using TokenPair=std::array<std::optional<AnatomicalContactToken>,2>;
    using ResultPair=std::array<std::optional<AnatomicalContactResult>,2>;
    struct AdmissionReceipt {
        std::array<AnatomicalContactOwner,2> owners{};
        std::uint64_t epoch{};
    };
    struct RejectRetention {bool operator()(const AnatomicalContactResult&) const {return false;}};
    static constexpr std::uint64_t max_age=100;
    void clear() {std::lock_guard<std::mutex> lock(mutex_);revoke_locked();lanes_={};}
    void stop() {std::lock_guard<std::mutex> lock(mutex_);stopping_=true;revoke_locked();lanes_={};}
    bool quiescent() const {std::lock_guard<std::mutex> lock(mutex_);return !inflight_;}
    bool stopped() const {std::lock_guard<std::mutex> lock(mutex_);return stopping_ && !inflight_;}
    bool start() {
        std::lock_guard<std::mutex> lock(mutex_);
        if(inflight_ || sealed_)return false;
        revoke_locked();if(sealed_)return false;lanes_={};stopping_=false;return true;
    }
    std::optional<AdmissionReceipt> admission() const {
        std::lock_guard<std::mutex> lock(mutex_);
        if(!admission_open() || !lanes_[0].owner.valid() || lanes_[0].owner!=lanes_[1].owner)return {};
        return AdmissionReceipt{{lanes_[0].owner,lanes_[1].owner},admission_epoch_};
    }
private:
    bool receipt_current(const AdmissionReceipt& receipt) const noexcept {
        return admission_open() && receipt.epoch && receipt.epoch==admission_epoch_ &&
            receipt.owners[0].valid() && receipt.owners[0]==receipt.owners[1] &&
            receipt.owners[0]==lanes_[0].owner && receipt.owners[1]==lanes_[1].owner;
    }
public:
    void observe(unsigned side,const AnatomicalContactOwner& owner) {
        if(side>1)return;
        std::lock_guard<std::mutex> lock(mutex_);auto& lane=lanes_[side];
        auto& other=lanes_[1-side];
        if(lane.owner!=owner)revoke_locked();
        if(owner.valid() && other.owner.valid() && (other.owner.game!=owner.game || other.owner.player!=owner.player ||
           other.owner.world!=owner.world || other.owner.spawn!=owner.spawn)) {
            const auto revision=other.revision+1;other={};other.revision=revision;
        }
        if(lane.owner!=owner) {const auto revision=lane.revision+1;lane={};lane.owner=owner;lane.revision=revision;}
    }
    AnatomicalContactOwner owner(unsigned side) const {
        std::lock_guard<std::mutex> lock(mutex_);return side<2?lanes_[side].owner:AnatomicalContactOwner{};
    }
    bool has_pending(unsigned side) const {
        std::lock_guard<std::mutex> lock(mutex_);return side<2 && lanes_[side].request && lanes_[side].request->token.request_revision>lanes_[side].consumed;
    }
    bool post(const AnatomicalContactRequest& request) {
        if(request.token.side>1)return false;
        std::lock_guard<std::mutex> lock(mutex_);
        auto& lane=lanes_[request.token.side];
        if(!token_admitted(request.token) || !valid_post(lane,request,request.token.side))return false;
        apply_post(lane,request);return true;
    }
    bool post_batch(const std::array<std::optional<AnatomicalContactRequest>,2>& requests) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(requests[0] && requests[1] && !same_batch(*requests[0],*requests[1]))return false;
        for(unsigned side=0;side<2;++side)if(requests[side] && (!token_admitted(requests[side]->token) || !valid_post(lanes_[side],*requests[side],side)))return false;
        for(unsigned side=0;side<2;++side) {
            if(requests[side])apply_post(lanes_[side],*requests[side]);
            else cancel(lanes_[side]);
        }
        return true;
    }
    bool post_batch_current(const AdmissionReceipt& receipt,const RequestPair& requests) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(!receipt_current(receipt) || (requests[0] && requests[1] && !same_batch(*requests[0],*requests[1])))return false;
        for(unsigned side=0;side<2;++side)if(requests[side] &&
            (requests[side]->token.admission_epoch!=receipt.epoch || !valid_post(lanes_[side],*requests[side],side)))return false;
        for(unsigned side=0;side<2;++side) {
            if(requests[side])apply_post(lanes_[side],*requests[side]);else cancel(lanes_[side]);
        }
        return true;
    }
    std::optional<AnatomicalContactResult> result(const AnatomicalContactToken& token,std::uint64_t now) const {
        std::lock_guard<std::mutex> lock(mutex_);
        if(token.side>1)return {};
        const auto& value=lanes_[token.side].result;
        return token_admitted(token) && value && value->token==token && value->request_tick && value->query_tick && now>=value->tick && now-value->tick<=max_age &&
            now>=value->request_tick && now-value->request_tick<=max_age && now>=value->query_tick && now-value->query_tick<=max_age?value:std::nullopt;
    }
    template<class Cast,class Validate,class Surface> unsigned service_batch_impl(const AnatomicalContactOwner& owner,std::uint64_t now,Cast& cast,Validate& validate,Surface& surface,unsigned side_mask,Working& working,const NativeWorldQuerySource* source=nullptr) {
        auto& requests=working.requests;auto& retained=working.retained;auto& staged=working.staged;
        std::array<std::uint64_t,2> lane_revisions{},lane_newest{},lane_geometry{},lane_ownership{},lane_reference{};
        std::array<AnatomicalContactOwner,2> lane_owners{};
        std::uint64_t revision{},epoch{};unsigned first_side{};bool any{};
        {
            std::lock_guard<std::mutex> lock(mutex_);revision=revision_;epoch=admission_epoch_;first_side=static_cast<unsigned>(service_turn_&1u);
            for(unsigned side=0;side<2;++side) if(side_mask&(1u<<side)) {
                auto& lane=lanes_[side];lane_revisions[side]=lane.revision;
                lane_newest[side]=lane.newest;lane_geometry[side]=lane.geometry;lane_ownership[side]=lane.ownership;lane_reference[side]=lane.reference;lane_owners[side]=lane.owner;
                if(lane.request && lane.request->token.request_revision>lane.consumed) {
                    requests[side]=lane.request;lane.consumed=lane.request->token.request_revision;lane.request.reset();retained[side]=lane.retained;any=true;
                }
            }
            if(any)++service_turn_;
        }
        if(!any)return 0;
        const auto snapshot_current=[&]() {
            std::lock_guard<std::mutex> lock(mutex_);
            if(revision!=revision_ || !admission_open())return false;
            for(unsigned side=0;side<2;++side) if(side_mask&(1u<<side)) {
                const auto& lane=lanes_[side];
                if(lane.revision!=lane_revisions[side] || lane.owner!=lane_owners[side] || lane.newest!=lane_newest[side] ||
                   lane.geometry!=lane_geometry[side] || lane.ownership!=lane_ownership[side] || lane.reference!=lane_reference[side] || lane.owner!=owner)return false;
            }
            return true;
        };
        unsigned total{};bool complete=true,owner_invalid=false;
        try {
            if(!owner.valid() || !validate()) {complete=false;owner_invalid=true;}
            if(source && (!source->valid() || source->game!=owner.game || source->player!=owner.player || source->world!=owner.world ||
                source->camera!=owner.camera || source->spawn!=owner.spawn || source->camera_identity!=owner.camera_identity))complete=false;
            for(const auto& pending:requests) if(pending && (pending->token.owner!=owner ||
                (pending->token.admission_epoch && pending->token.admission_epoch!=epoch) || now<pending->tick || now-pending->tick>max_age ||
                (pending->local_probe && !pending->source.valid()) || (source && (!pending->source.valid() || pending->source!=*source))))complete=false;
            if(requests[0] && requests[1] && !same_batch(*requests[0],*requests[1]))complete=false;
            for(unsigned offset=0;offset<2 && complete;++offset) {
                const unsigned side=(first_side+offset)&1u;
                if(!requests[side])continue;
                if(!snapshot_current()) {complete=false;break;}
                const auto& contact_request=*requests[side];auto& value=working.value;value={};value.token=contact_request.token;value.tick=now;value.query_tick=now;value.source=contact_request.source;
                if(contact_request.token.owner!=owner || now<contact_request.tick || now-contact_request.tick>max_age) {complete=false;break;}
                auto& previous=working.previous;previous=retained[side];
                bool unchanged=!contact_request.local_probe && previous && !previous->request.local_probe && previous->request.held_entity==contact_request.held_entity &&
                    previous->request.source==contact_request.source && previous->request.token.admission_epoch==contact_request.token.admission_epoch &&
                    previous->request.token.owner==contact_request.token.owner && previous->request.token.geometry_epoch==contact_request.token.geometry_epoch &&
                    previous->request.token.ownership_epoch==contact_request.token.ownership_epoch && previous->request.token.pose_epoch==contact_request.token.pose_epoch;
                if(unchanged)for(unsigned proxy=0;proxy<hand_proxy_count;++proxy) {
                    if(length(previous->request.to[proxy].centre-contact_request.to[proxy].centre)!=0 ||
                       previous->request.to[proxy].radius!=contact_request.to[proxy].radius || contact_request.acquire[proxy])unchanged=false;
                }
                if(unchanged && surface(previous->result)) {
                    value=previous->result;value.token=contact_request.token;value.tick=now;value.request_tick=contact_request.tick;value.casts=0;value.state=AnatomicalContactState::retained;
                    if(!validate()) {complete=false;owner_invalid=true;}
                    else if(!snapshot_current())complete=false;
                    else staged[side]=value;
                    continue;
                }
                bool acquired=true;
                for(unsigned proxy=0;proxy<hand_proxy_count;++proxy) {
                    const double distance=length(contact_request.to[proxy].centre-contact_request.from[proxy].centre);
                    if(distance<.001 || distance>(contact_request.local_probe?.0025:(contact_request.acquire[proxy]?.0025:(contact_request.safe_anchor_acquisition?1.5:.35))) ||
                       (!contact_request.local_probe && !contact_request.acquire[proxy] && !contact_request.safe_from) || contact_request.to[proxy].radius!=contact_request.from[proxy].radius)acquired=false;
                }
                if(!acquired)value.state=AnatomicalContactState::acquisition_required;
                else {
                    std::array<std::optional<WorldTraceHit>,hand_proxy_count> hits{};
                    working.local=std::make_unique<AnatomicalLocalContactReducer>(contact_request);
                    for(unsigned proxy=0;proxy<hand_proxy_count;++proxy) {
                        if(!validate()) {complete=false;owner_invalid=true;break;}
                        if(!snapshot_current()) {complete=false;break;}
                        ++total;++value.casts;
                        auto query=normalize_query(cast(contact_request.from[proxy].centre,contact_request.to[proxy].centre,contact_request.to[proxy].radius));
                        if(!query.complete) {complete=false;break;}
                        if(contact_request.local_probe && !working.local->add(proxy,query)) {complete=false;break;}
                        hits[proxy]=std::move(query.hit);
                    }
                    if(!complete)break;
                    const auto casts=value.casts;bool acquisition_requested=false,held_hit=false;
                    for(const auto acquire:contact_request.acquire)acquisition_requested=acquisition_requested || acquire;
                    for(unsigned proxy=0;proxy<hand_proxy_count;++proxy) if(hits[proxy])held_hit=held_hit || (contact_request.held_entity && hits[proxy]->entity==contact_request.held_entity);
                    if(contact_request.local_probe)value=working.local->finish();
                    else if(acquisition_requested) {value.hits=hits;value.state=held_hit?AnatomicalContactState::occluded:AnatomicalContactState::acquisition_required;}
                    else value=anatomical_contact_reduce(contact_request,hits);
                    value.casts=casts;value.tick=now;value.query_tick=now;
                }
                value.request_tick=contact_request.tick;
                if(!validate()) {complete=false;owner_invalid=true;}
                else if(!snapshot_current())complete=false;
                else staged[side]=value;
            }
            if(complete && !validate()) {complete=false;owner_invalid=true;}
        } catch(...) {complete=false;}
        if(owner_invalid)for(unsigned side=0;side<2;++side)if(side_mask&(1u<<side))observe(side,{});
        {
            std::lock_guard<std::mutex> lock(mutex_);
            bool current=complete && revision==revision_;
            for(unsigned side=0;side<2 && current;++side) if(side_mask&(1u<<side)) {
                const auto& lane=lanes_[side];
                current=lane.revision==lane_revisions[side] && lane.owner==lane_owners[side] && lane.newest==lane_newest[side] &&
                    lane.geometry==lane_geometry[side] && lane.ownership==lane_ownership[side] && lane.reference==lane_reference[side] && lane.owner==owner;
            }
            if(current) {
                for(unsigned result_side=0;result_side<2;++result_side) if(requests[result_side] && staged[result_side]) {
                    auto& lane=lanes_[result_side];lane.result=staged[result_side];lane.retained=staged[result_side]->actionable()?std::optional<Retained>{Retained{*requests[result_side],*staged[result_side]}}:std::nullopt;
                }
            } else {
                for(unsigned result_side=0;result_side<2;++result_side) if(requests[result_side]) {
                    auto& lane=lanes_[result_side];if(lane.result && lane.result->token==requests[result_side]->token)lane.result.reset();
                }
            }
        }
        return total;
    }
    static unsigned invoke_seh_cleanup(std::atomic_flag* flag,unsigned(*invoke)(void*),void* context,void(*cleanup)(void*) noexcept) {
        unsigned result{};
#if defined(_MSC_VER)
        __try {result=invoke(context);}
        __finally {if(flag)flag->clear(std::memory_order_release);cleanup(context);}
#else
        try {result=invoke(context);} catch(...) {if(flag)flag->clear(std::memory_order_release);cleanup(context);throw;}
        if(flag)flag->clear(std::memory_order_release);cleanup(context);
#endif
        return result;
    }
    template<class Cast,class Validate,class Surface> unsigned invoke_batch(const AnatomicalContactOwner& owner,std::uint64_t now,Cast& cast,Validate& validate,Surface& surface,unsigned side_mask,const NativeWorldQuerySource* source=nullptr) {
        if(own_callback())return 0;
        {std::lock_guard<std::mutex> lock(mutex_);if(!admission_open() || servicing_.test_and_set(std::memory_order_acquire))return 0;++inflight_;}
        struct Context {AnatomicalWorldContact* self;const AnatomicalContactOwner* owner;std::uint64_t now;Cast* cast;Validate* validate;Surface* surface;unsigned mask;const NativeWorldQuerySource* source;Working* working;CallbackFrame frame;};
        Context context{this,&owner,now,&cast,&validate,&surface,side_mask,source,nullptr,{this,callback_frame_}};
        callback_frame_=&context.frame;
        try {context.working=new Working;}catch(...) {servicing_.clear(std::memory_order_release);leave_callback(context.frame);return 0;}
        const auto invoke=[](void* raw)->unsigned {auto& value=*static_cast<Context*>(raw);return value.self->service_batch_impl(*value.owner,value.now,*value.cast,*value.validate,*value.surface,value.mask,*value.working,value.source);};
        const auto cleanup=[](void* raw) noexcept {auto& value=*static_cast<Context*>(raw);delete value.working;value.working=nullptr;value.self->leave_callback(value.frame);};
        return invoke_seh_cleanup(&servicing_,invoke,&context,cleanup);
    }
    template<class Invoke> bool consume_batch(const AdmissionReceipt& receipt,const TokenPair& tokens,std::uint64_t now,Invoke&& callback) {
        if(own_callback())return false;
        struct Context {AnatomicalWorldContact* self;std::remove_reference_t<Invoke>* callback;ResultPair* results;CallbackFrame frame;};
        ResultPair* results{};try {results=new ResultPair;}catch(...) {return false;}
        {
            std::lock_guard<std::mutex> lock(mutex_);
            bool valid=receipt_current(receipt) && (tokens[0] || tokens[1]);
            for(unsigned side=0;side<2 && valid;++side)if(tokens[side]) {
                const auto& value=lanes_[side].result;const auto& token=*tokens[side];
                valid=token.side==side && token.admission_epoch==receipt.epoch && token.owner==receipt.owners[side] && value && value->token==token &&
                    value->request_tick && value->query_tick && now>=value->tick && now-value->tick<=max_age &&
                    now>=value->request_tick && now-value->request_tick<=max_age && now>=value->query_tick && now-value->query_tick<=max_age;
            }
            if(valid && tokens[0] && tokens[1])valid=tokens[0]->owner==tokens[1]->owner && tokens[0]->pose_epoch==tokens[1]->pose_epoch &&
                tokens[0]->request_revision==tokens[1]->request_revision && lanes_[0].result->request_tick==lanes_[1].result->request_tick;
            if(!valid) {delete results;return false;}
            ++inflight_;
            for(unsigned side=0;side<2;++side)if(tokens[side])(*results)[side]=lanes_[side].result;
        }
        Context context{this,&callback,results,{this,callback_frame_}};callback_frame_=&context.frame;
        const auto invoke=[](void* raw)->unsigned {auto& value=*static_cast<Context*>(raw);(*value.callback)(static_cast<const ResultPair&>(*value.results));return 1;};
        const auto cleanup=[](void* raw) noexcept {auto& value=*static_cast<Context*>(raw);delete value.results;value.self->leave_callback(value.frame);};
        return invoke_seh_cleanup(nullptr,invoke,&context,cleanup)!=0;
    }
    template<class Cast,class Validate,class Surface=RejectRetention> unsigned service_batch(const AnatomicalContactOwner& owner,std::uint64_t now,Cast cast,Validate validate,Surface surface={}) {
        static_assert(std::is_same_v<std::decay_t<std::invoke_result_t<Cast,Vec3,Vec3,double>>,WorldTraceQueryResult>,"service_batch requires checked native query results");
        return invoke_batch(owner,now,cast,validate,surface,3u);
    }
    template<class Cast,class Validate,class Surface=RejectRetention> unsigned service_batch(const AnatomicalContactOwner& owner,const NativeWorldQuerySource& source,
            std::uint64_t now,Cast cast,Validate validate,Surface surface={}) {
        static_assert(std::is_same_v<std::decay_t<std::invoke_result_t<Cast,Vec3,Vec3,double>>,WorldTraceQueryResult>,"service_batch requires checked native query results");
        return invoke_batch(owner,now,cast,validate,surface,3u,&source);
    }
    template<class Cast,class Validate,class Surface=RejectRetention> unsigned service(unsigned side,const AnatomicalContactOwner& owner,std::uint64_t now,Cast cast,Validate validate,Surface surface={}) {
        if(side>1)return 0;
        return invoke_batch(owner,now,cast,validate,surface,1u<<side);
    }
};
}
