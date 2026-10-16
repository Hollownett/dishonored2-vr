#pragma once
#include "native_door_empty_gate.hpp"
#include "native_door_clip_selection.hpp"
#include "native_door_skeleton_content.hpp"
#include "native_door_request.hpp"
#include "physical_door_geometry.hpp"
#include "hand_contact.hpp"
#include <atomic>
#include <limits>
#include <mutex>

namespace d2vr {
class NativeDoorIngress {
public:
    using Read=NativeWorldTrace::Read;
    struct Publication {
        NativeWorldQuerySource source{};
        std::array<WorldTrackedPose,2> grips{};
        std::array<Vec3,2> velocities{};
        Vec3 player_origin{};
        std::array<Vec3,3> mapping_axes{};
        std::array<float,2> curls{};
        std::uint64_t tick{},source_tick{},pose_epoch{},reference_epoch{},revision{};
        unsigned hands{};
        bool velocity_ready{};
        std::array<std::optional<HandProxies>,2> proxies{};
        std::array<std::uint64_t,2> proxy_tick{};
        bool require_fitted_contact{};
    };
    struct Contact {
        NativeDoorOwner::Snapshot owner{};
        NativeDoorOwner::State state{};
        NativeDoorEmptyGate::Lease empty{};
        NativeDoorClipSelection::Lease clip{};
        NativeDoorSkeletonContent::Lease skeleton{};
        NativeDoorContact::Surface surface{};
        PhysicalDoorGeometry::Frame frame{};
        PhysicalDoorGeometry::Sample geometry{};
        Publication publication{};
        std::uint64_t observed_tick{},contact_token{};
        unsigned side{};
        bool opening_ccw{};
    };
    struct Metrics {std::uint64_t queries{},qualified{},service_ticks{},worst_service_ticks{};};
    Metrics metrics() const noexcept {return {query_count_.load(),qualified_count_.load(),service_ticks_.load(),worst_service_ticks_.load()};}
    inline static constexpr double intent_radius=.018;
    static bool same_source(NativeWorldQuerySource first,NativeWorldQuerySource second) noexcept {
        first.camera_identity.flags&=NativeDoorOwner::stable_identity_flags;
        first.physics_identity.flags&=NativeDoorOwner::stable_identity_flags;
        second.camera_identity.flags&=NativeDoorOwner::stable_identity_flags;
        second.physics_identity.flags&=NativeDoorOwner::stable_identity_flags;
        return first==second;
    }
    static bool fresh(const Publication& publication,std::uint64_t now) noexcept {
        if(!publication.source.valid() || publication.hands!=3 || !publication.pose_epoch || !publication.reference_epoch ||
           !publication.revision || !publication.tick || !publication.source_tick || now<publication.tick || now<publication.source_tick ||
           now-publication.tick>100 || now-publication.source_tick>grab_pose_freshness_ms || !finite(publication.player_origin))return false;
        for(unsigned side=0;side<2;++side) {
            if(!finite(publication.grips[side].position) || !finite(publication.velocities[side]) ||
               !std::isfinite(publication.curls[side]) || publication.curls[side]<0 || publication.curls[side]>1.f)return false;
            if(publication.proxies[side]) {
                if(now-publication.tick>32 || now-publication.source_tick>32 || !publication.proxy_tick[side] ||
                   now<publication.proxy_tick[side] || now-publication.proxy_tick[side]>32)return false;
                for(const auto& sphere:*publication.proxies[side])
                    if(!finite(sphere.centre) || !std::isfinite(sphere.radius) || sphere.radius<=.0005 || sphere.radius>.15)return false;
            }
        }
        for(const auto axis:publication.mapping_axes)if(!finite(axis) || std::abs(dot(axis,axis)-1.)>.02)return false;
        return std::abs(dot(publication.mapping_axes[0],publication.mapping_axes[1]))<.02 &&
            std::abs(dot(publication.mapping_axes[0],publication.mapping_axes[2]))<.02 &&
            std::abs(dot(publication.mapping_axes[1],publication.mapping_axes[2]))<.02 &&
            dot(cross(publication.mapping_axes[0],publication.mapping_axes[1]),publication.mapping_axes[2])>.99;
    }
    static std::optional<ContactSphere> contact_sphere(const Publication& publication,unsigned side,unsigned index,std::uint64_t now) noexcept {
        if(side>1 || !fresh(publication,now) || publication.curls[side]>.55f)return {};
        if(publication.proxies[side])return index<hand_proxy_count?std::optional<ContactSphere>{(*publication.proxies[side])[index]}:std::nullopt;
        if(publication.require_fitted_contact || index)return {};
        return ContactSphere{publication.grips[side].position,intent_radius};
    }
    static bool continuous(const Publication& first,const Publication& second) noexcept {
        if(!fresh(first,first.tick) || !fresh(second,second.tick) || !same_source(first.source,second.source) || first.pose_epoch!=second.pose_epoch || first.reference_epoch!=second.reference_epoch ||
           second.source_tick<=first.source_tick || second.tick<first.tick || second.source_tick-first.source_tick>100 ||
           length(second.player_origin-first.player_origin)>.2)return false;
        for(unsigned axis=0;axis<3;++axis)if(dot(first.mapping_axes[axis],second.mapping_axes[axis])<.95)return false;
        for(unsigned side=0;side<2;++side)if(length(second.grips[side].position-first.grips[side].position)>.35)return false;
        return true;
    }
    bool configure(std::uintptr_t base,Read read) noexcept {
        enabled_=false;
        clear();
        if(!base || !read)return false;
        bool verified{};
        auto verify=[&] {try {verified=NativeWorldTrace::verify(base,read);return verified;}catch(...) {return false;}};
        if(!native_world_query_source_detail::guarded(verify) || !verified)return false;
        std::lock_guard<std::mutex> guard(mutex_);base_=base;read_=read;enabled_=true;return true;
    }
    void stop() noexcept {enabled_=false;clear();}
    void clear() noexcept {
        std::lock_guard<std::mutex> guard(mutex_);
        publication_={};contacts_={};previous_={};last_surface_={};suppressed_until_=0;
        advance_revision();
    }
    bool publish(Publication publication) noexcept {
        if(!enabled_)return false;
        std::lock_guard<std::mutex> guard(mutex_);
        if(!enabled_)return false;
        publication.revision=revision_;
        publication.velocity_ready=false;
        publication.velocities={};
        if(!fresh(publication,publication.tick) || publication.source.base!=base_) {
            advance_revision();
            publication_={};previous_={};contacts_={};return false;
        }
        if(previous_.source_tick==publication.source_tick && previous_.pose_epoch==publication.pose_epoch &&
           previous_.reference_epoch==publication.reference_epoch) {
            if(same_sample(previous_,publication))return fresh(publication_,publication.tick);
            advance_revision();
            publication_={};contacts_={};return false;
        }
        if(!advance_revision())return false;
        publication.revision=revision_;
        if(previous_.revision && !continuous(previous_,publication)) {
            suppressed_until_=publication.tick>UINT64_MAX-200?UINT64_MAX:publication.tick+200;
            contacts_={};
        } else if(previous_.revision) {
            const double seconds=static_cast<double>(publication.source_tick-previous_.source_tick)*.001;
            if(seconds>=.001 && seconds<=.1) {
                for(unsigned side=0;side<2;++side)
                    publication.velocities[side]=(publication.grips[side].position-previous_.grips[side].position)*(1./seconds);
                publication.velocity_ready=true;
            }
        }
        previous_=publication;
        if(publication.tick<suppressed_until_ || !publication.velocity_ready) {publication_={};return false;}
        publication_=publication;return true;
    }
    std::array<std::optional<Contact>,2> contacts(std::uint64_t now) const {
        std::lock_guard<std::mutex> guard(mutex_);
        if(!enabled_ || !fresh(publication_,now))return {};
        auto result=contacts_;
        for(auto& contact:result)if(contact && (contact->publication.revision!=publication_.revision || now<contact->observed_tick || now-contact->observed_tick>100 ||
            contact->publication.pose_epoch!=publication_.pose_epoch || contact->publication.reference_epoch!=publication_.reference_epoch ||
            !same_source(contact->publication.source,publication_.source)))contact.reset();
        return result;
    }
    template<class ScopeValid,class Query>
    void service_with(const NativePlayerCamera& camera,NativeGrab& grab,std::uint64_t now,ScopeValid&& scope_valid,Query&& query,
                      std::uint64_t (*clock)()=nullptr) {
        if(servicing_.test_and_set())return;
        struct Exit {std::atomic_flag& flag;~Exit() {flag.clear();}} exit{servicing_};
        Publication publication{};std::uint64_t revision{};std::uintptr_t base{};Read read{};
        {std::lock_guard<std::mutex> guard(mutex_);publication=publication_;revision=revision_;base=base_;read=read_;contacts_={};}
        if(!enabled_ || !fresh(publication,now) || !publication.velocity_ready || !scope_valid())return;
        const auto source=native_world_query_source(read,base,camera.camera,publication.source.world);
        if(!source || !same_source(*source,publication.source) || source->player!=camera.player)return;
        const auto origin=rig::value<std::array<float,3>>(read,camera.player+0x300);
        if(!origin || !finite(Vec3{(*origin)[0],(*origin)[1],(*origin)[2]}) ||
           length(Vec3{(*origin)[0],(*origin)[1],(*origin)[2]}-publication.player_origin)>.2)return;
        std::array<std::optional<Contact>,2> result{};
        for(unsigned side=0;side<2;++side) {
          if(publication.curls[side]>.55f)continue;
          const auto empty=NativeDoorEmptyGate::capture(base,read,camera,grab,clock?clock():now,1u<<side);
          if(!empty || empty->epoch!=publication.pose_epoch)continue;
          for(unsigned proxy=0;proxy<hand_proxy_count;++proxy) {
            if(!current_revision(revision) || !fresh(publication,clock?clock():now) || !scope_valid())return;
            const auto sphere=contact_sphere(publication,side,proxy,clock?clock():now);
            if(!sphere)break;
            const auto point=sphere->centre;const auto radius=sphere->radius;
            ++query_count_;
            const auto hits=query(*source,point,point+Vec3{0,0,.002},radius,read);
            if(!hits.complete || !hits.initial || !hits.regular || hits.initial->count>32 || hits.regular->count>32)return;
            std::uintptr_t target{};
            for(unsigned index=0;index<hits.initial->count;++index) {
                const auto entity=hits.initial->hits[index].entity;
                if(!rig::typed(read,base,entity,0x1cb7c68))continue;
                if(target && target!=entity)return;
                target=entity;
            }
            if(!target)continue;
            const auto surface=NativeDoorContact::select(hits,source->handle,target);
            const auto owner=NativeDoorOwner::capture(base,read,target);
            if(!surface || !owner || owner->player!=camera.player || owner->world!=source->world)continue;
            const auto state=NativeDoorOwner::state(read,*owner);
            const auto transform=rig::value<std::array<float,12>>(read,owner->part+0x300);
            const auto frame=transform?PhysicalDoorGeometry::frame(*transform):std::nullopt;
            if(!state || !state->eligible() || !frame || state->current!=state->next || state->current>2)continue;
            bool ccw{};
            if(state->current==0)ccw=dot(Vec3{(*origin)[0],(*origin)[1],(*origin)[2]}-frame->hinge,frame->normal)<=0;
            else if(state->current==1) {if(state->params[2]>1)continue;ccw=state->params[2]!=0;}
            else {
                const auto component=owner->component;
                unsigned char sense{};
                if(!NativeDoorEventAdmission::opened_state(base,read,component,sense))continue;
                ccw=sense!=0;
            }
            const auto clip=NativeDoorClipSelection::capture(read,*owner,ccw);
            const auto skeleton=clip?NativeDoorSkeletonContent::capture(base,read,*owner,clip->context.descriptor):std::nullopt;
            const auto geometry=PhysicalDoorGeometry::sample(*frame,*surface,point,publication.velocities[side],radius,ccw);
            const auto current_tick=clock?clock():now;
            if(!clip || !skeleton || !geometry || !fresh(publication,current_tick) || !NativeDoorOwner::current(read,*owner) ||
               rig::value<std::array<float,12>>(read,owner->part+0x300)!=transform ||
               !NativeDoorEmptyGate::current(base,read,camera,grab,*empty,current_tick) || !scope_valid() || !current_revision(revision))continue;
            result[side]=Contact{*owner,*state,*empty,*clip,*skeleton,*surface,*frame,*geometry,publication,current_tick,0,side,ccw};
            break;
          }
        }
        const auto final_tick=clock?clock():now;
        if(!fresh(publication,final_tick) || !native_world_query_source_current(read,*source) || !scope_valid())return;
        for(const auto& contact:result)if(contact && (!NativeDoorEmptyGate::current(base,read,camera,grab,contact->empty,final_tick) ||
            !NativeDoorOwner::current(read,contact->owner) ||
            NativeWorldTrace::checked_entity_of(read,*source,contact->surface.full_body)!=contact->surface.entity))return;
        if(!fresh(publication,clock?clock():now) || !scope_valid())return;
        std::lock_guard<std::mutex> guard(mutex_);
        if(!enabled_ || revision_!=revision)return;
        for(unsigned side=0;side<2;++side)if(result[side]) {
            const auto& prior=last_surface_[side];
            if(!prior || prior->entity!=result[side]->surface.entity || prior->full_body!=result[side]->surface.full_body ||
               prior->shape_key!=result[side]->surface.shape_key) {
                if(contact_sequence_==UINT64_MAX) {enabled_=false;contacts_={};return;}
                contact_tokens_[side]=++contact_sequence_;
            }
            result[side]->contact_token=contact_tokens_[side];last_surface_[side]=result[side]->surface;
            ++qualified_count_;
        } else last_surface_[side].reset();
        contacts_=std::move(result);
    }
    template<class ScopeValid> void service(const NativePlayerCamera& camera,NativeGrab& grab,std::uint64_t now,ScopeValid&& scope_valid) {
        LARGE_INTEGER began{},ended{};QueryPerformanceCounter(&began);
        service_with(camera,grab,now,scope_valid,[](const NativeWorldQuerySource& source,Vec3 from,Vec3 to,double radius,Read read) {
            return checked_query(source.base,read,source,from,to,radius);
        },+[]()->std::uint64_t {return GetTickCount64();});
        QueryPerformanceCounter(&ended);
        if(ended.QuadPart>=began.QuadPart) {
            const auto ticks=static_cast<std::uint64_t>(ended.QuadPart-began.QuadPart);service_ticks_+=ticks;
            auto worst=worst_service_ticks_.load();while(worst<ticks && !worst_service_ticks_.compare_exchange_weak(worst,ticks)) {}
        }
    }
private:
    static bool same_point(Vec3 first,Vec3 second) noexcept {return first.x==second.x && first.y==second.y && first.z==second.z;}
    static bool same_sample(const Publication& first,const Publication& second) noexcept {
        if(!same_source(first.source,second.source) || !same_point(first.player_origin,second.player_origin) ||
           first.hands!=second.hands || first.curls!=second.curls || first.require_fitted_contact!=second.require_fitted_contact ||
           first.proxy_tick!=second.proxy_tick)return false;
        for(unsigned axis=0;axis<3;++axis)if(!same_point(first.mapping_axes[axis],second.mapping_axes[axis]))return false;
        for(unsigned side=0;side<2;++side)if(!same_point(first.grips[side].position,second.grips[side].position))return false;
        for(unsigned side=0;side<2;++side) {
            if(first.proxies[side].has_value()!=second.proxies[side].has_value())return false;
            if(first.proxies[side])for(unsigned proxy=0;proxy<hand_proxy_count;++proxy) {
                const auto& a=(*first.proxies[side])[proxy];const auto& b=(*second.proxies[side])[proxy];
                if(a.radius!=b.radius || !same_point(a.centre,b.centre))return false;
            }
        }
        return true;
    }
    static WorldTraceQueryResult checked_query(std::uintptr_t base,Read read,const NativeWorldQuerySource& source,Vec3 from,Vec3 to,double radius) {
        WorldTraceQueryResult result{};
        auto invoke=[&] {result=NativeWorldTrace::sphere_query_checked(base,read,reinterpret_cast<NativeWorldTrace::Cast>(base+NativeWorldTrace::cast_rva),
            reinterpret_cast<NativeWorldTrace::Construct>(base+NativeWorldTrace::sphere_ctor_rva),source.world,from,to,radius,
            kWorldOccluderLayers,kWorldOccluderRequired,&source,true);return result.complete;};
        if(!native_world_query_source_detail::guarded(invoke))return {};
        return result;
    }
    bool advance_revision() noexcept {
        if(revision_==UINT64_MAX) {enabled_=false;return false;}
        ++revision_;return true;
    }
    bool current_revision(std::uint64_t revision) const noexcept {
        std::lock_guard<std::mutex> guard(mutex_);return enabled_ && revision_==revision;
    }
    mutable std::mutex mutex_;
    std::atomic_flag servicing_=ATOMIC_FLAG_INIT;
    std::atomic<bool> enabled_{};
    std::atomic<std::uint64_t> query_count_{},qualified_count_{},service_ticks_{},worst_service_ticks_{};
    std::uintptr_t base_{};Read read_{};
    Publication publication_{},previous_{};
    std::array<std::optional<Contact>,2> contacts_{};
    std::array<std::optional<NativeDoorContact::Surface>,2> last_surface_{};
    std::array<std::uint64_t,2> contact_tokens_{};
    std::uint64_t revision_{},contact_sequence_{},suppressed_until_{};
};
}
