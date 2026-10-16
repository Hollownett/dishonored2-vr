#pragma once
#include "anatomical_world_contact.hpp"
#include "native_rigid_body.hpp"
#include <vector>

namespace d2vr {
// Numeric witnesses only: their native owners must be revalidated by the caller.
struct FullHandContactPlane {
    Vec3 point{},normal{};
    double depth{};
    unsigned proxy{};
    std::uint32_t body{},shape_key{};
    bool initial{};
    std::uintptr_t entity{};
};
struct FullHandContactResult {
    bool valid{},blocked{true};
    Vec3 shift{};
    HandProxies proxies{};
    std::vector<FullHandContactPlane> planes;
};
inline FullHandContactResult full_hand_contact_solve(FullHandContactResult result,double max_shift) {
    result.shift={};result.blocked=true;
    if(!result.valid || !std::isfinite(max_shift) || max_shift<=0 || max_shift>.35) {result.valid=false;return result;}
    for(unsigned pass=0;pass<32;++pass) {
        bool satisfied=true;
        for(const auto& plane:result.planes) {
            const auto remaining=plane.depth-dot(result.shift,plane.normal);
            if(remaining>1e-6) {result.shift=result.shift+plane.normal*remaining;satisfied=false;}
            if(!finite(result.shift) || length(result.shift)>max_shift) {result.shift={};return result;}
        }
        if(satisfied) {result.blocked=false;return result;}
    }
    result.shift={};return result;
}
inline FullHandContactResult full_hand_contact_reduce(const HandProxies& from,const HandProxies& to,
        const std::array<WorldTraceQueryResult,hand_proxy_count>& queries,const NativeWorldQuerySource& source,
        std::uintptr_t held_entity=0,double max_shift=.35) {
    FullHandContactResult result{};result.proxies=to;
    if(!source.valid() || !anatomical_shape_valid(from) || !anatomical_shape_valid(to))return result;
    const auto body_valid=[&](std::uint32_t handle) {
        const auto index=handle&0xffffffu;
        return handle!=0xffffffffu && index!=0xffffffu && index<source.body_count && handle!=source.handle;
    };
    const auto entity_valid=[&](std::uintptr_t entity) {return entity!=source.player && (!held_entity || entity!=held_entity);};
    for(unsigned proxy=0;proxy<hand_proxy_count;++proxy) {
        const auto delta=to[proxy].centre-from[proxy].centre;
        const auto& query=queries[proxy];
        if(length(delta)>.35 || from[proxy].radius!=to[proxy].radius || !query.complete || !query.initial || !query.regular ||
           query.initial->count>query.initial->hits.size() || query.regular->count>query.regular->hits.size())return {};
        for(unsigned index=0;index<query.initial->count;++index) {
            const auto& witness=query.initial->hits[index];const auto& hit=witness.contact;
            if(!hit.normal_valid || !finite(hit.point) || !finite(hit.normal) || std::abs(length(hit.normal)-1.)>.01 ||
               !std::isfinite(hit.signed_separation) || hit.signed_separation>0 || hit.source_body!=source.handle ||
               !body_valid(hit.body) || !entity_valid(witness.entity))return {};
            result.planes.push_back({hit.point,hit.normal,-static_cast<double>(hit.signed_separation)-dot(delta,hit.normal),
                proxy,hit.body,hit.shape_key,true,witness.entity});
        }
        for(unsigned index=0;index<query.regular->count;++index) {
            const auto& hit=query.regular->hits[index];
            if(!finite(hit.point) || !finite(hit.normal) || !std::isfinite(hit.fraction) || hit.fraction<0 || hit.fraction>1 ||
               !body_valid(hit.body_handle) || hit.body!=(hit.body_handle&0xffffffu) || !entity_valid(hit.entity))return {};
            if(hit.initial_overlap) {
                bool matched=false;
                for(unsigned initial=0;initial<query.initial->count;++initial) {
                    const auto& contact=query.initial->hits[initial].contact;
                    matched=matched || (contact.body==hit.body_handle && contact.shape_key==hit.shape_key);
                }
                if(!matched)return {};
                continue; // Its signed initial witness already supplies the plane.
            }
            if(!hit.normal_valid || std::abs(length(hit.normal)-1.)>.01)return {};
            result.planes.push_back({hit.point,hit.normal,to[proxy].radius-dot(to[proxy].centre-hit.point,hit.normal),
                proxy,hit.body_handle,hit.shape_key,false,hit.entity});
        }
    }
    result.valid=true;
    return full_hand_contact_solve(std::move(result),max_shift);
}
// This reprojects only witnessed planes; an empty result is not a startup clearance certificate.
inline FullHandContactResult full_hand_contact_reproject(const FullHandContactResult& previous,const HandProxies& current,
        std::uint64_t now,std::uint64_t query_tick,std::uint64_t source_tick,double max_shift=.35) {
    if(!previous.valid || previous.blocked || !query_tick || !source_tick || now<query_tick || now<source_tick ||
       now-query_tick>32 || now-source_tick>32 || !anatomical_shape_valid(previous.proxies) || !anatomical_shape_valid(current))return {};
    for(unsigned proxy=0;proxy<hand_proxy_count;++proxy)
        if(previous.proxies[proxy].radius!=current[proxy].radius ||
           length(current[proxy].centre-previous.proxies[proxy].centre)>.06)return {};
    auto result=previous;
    for(auto& plane:result.planes) {
        if(plane.proxy>=hand_proxy_count || !finite(plane.normal) || std::abs(length(plane.normal)-1.)>.01 || !std::isfinite(plane.depth))return {};
        plane.depth-=dot(current[plane.proxy].centre-previous.proxies[plane.proxy].centre,plane.normal);
    }
    result.proxies=current;
    return full_hand_contact_solve(std::move(result),max_shift);
}
inline std::optional<Vec3> full_hand_contact_grab(const HandProxies& proxies,const NativeGrabBounds& bounds,double skin=.003) {
    if(!anatomical_shape_valid(proxies) || !finite(bounds.minimum) || !finite(bounds.maximum) ||
       bounds.minimum.x>bounds.maximum.x || bounds.minimum.y>bounds.maximum.y || bounds.minimum.z>bounds.maximum.z ||
       !std::isfinite(skin) || skin<0 || skin>.003)return {};
    std::optional<Vec3> contact;double nearest=std::numeric_limits<double>::infinity();
    for(const auto& proxy:proxies) {
        const auto point=bounds.closest(proxy.centre);const auto separation=length(point-proxy.centre)-proxy.radius;
        if(separation<=skin && separation<nearest) {contact=point;nearest=separation;}
    }
    return contact;
}

// Numeric change detector, not a native pin. Capture follows the admitted query;
// it cannot detect an in-place mesh payload edit or a change before that capture.
class FullHandTargetWitness {
public:
    struct Witness {
        NativeWorldQuerySource source{};
        std::uintptr_t pool{},shape{},shape_vtable{},userdata{},component{},component_vtable{},entity{};
        std::uint32_t count{},handle{},contact_key{},motion{},body_flags{},filter{},broadphase{},entity_id{};
        std::uint16_t root_key{};
        rig::Identity component_identity{};
        std::array<std::uint32_t,12> pose{};
        std::array<std::byte,0x28> shape_header{}; // Native fixed header +20..47 only.
    };
    static bool equal(const Witness& a,const Witness& b) noexcept {
        return a.source==b.source && a.pool==b.pool && a.shape==b.shape && a.shape_vtable==b.shape_vtable &&
            a.userdata==b.userdata && a.component==b.component && a.component_vtable==b.component_vtable &&
            a.entity==b.entity && a.count==b.count && a.handle==b.handle && a.contact_key==b.contact_key &&
            a.motion==b.motion && a.body_flags==b.body_flags && a.filter==b.filter && a.broadphase==b.broadphase &&
            a.entity_id==b.entity_id && a.root_key==b.root_key && a.component_identity==b.component_identity &&
            a.pose==b.pose && a.shape_header==b.shape_header;
    }
private:
    template<class Read> static bool inspect(Read& read,const NativeWorldQuerySource& source,
            std::uint32_t handle,std::uint32_t key,Witness& out) {
        if(!source.valid() || handle==source.handle)return false;
        const auto body=NativeWorldTrace::inspect_hit_body(read,source,handle);
        if(!body || body->pool!=source.pool || body->count!=source.body_count)return false;
        out.source=source;out.pool=body->pool;out.count=body->count;out.handle=handle;out.contact_key=key;
        out.broadphase=body->broadphase;out.userdata=body->userdata;out.component=body->component;
        out.entity=body->entity;out.entity_id=body->entity_id;
        rig::Transform pose{};
        for(unsigned row=0;row<4;++row) {
            if(!read(body->record+row*16,out.pose.data()+row*3,12))return false;
            std::memcpy(pose.f.data()+row*4,out.pose.data()+row*3,12);
        }
        if(!rig::valid(pose) || !read(body->record+0x40,&out.motion,4) ||
           !read(body->record+0x44,&out.body_flags,4) || !read(body->record+0x60,&out.shape,8) ||
           !rig::pointer(out.shape) || !read(body->record+0x6a,&out.root_key,2) ||
           !read(body->record+0x6c,&out.filter,4) || !read(out.shape,&out.shape_vtable,8) ||
           !rig::pointer(out.shape_vtable) || !read(out.shape+0x20,out.shape_header.data(),out.shape_header.size()))return false;
        if(out.userdata) {
            if(!out.component || !out.entity || !read(out.component,&out.component_vtable,8) ||
               (out.component_vtable!=source.base+0x1c745c8 && out.component_vtable!=source.base+0x1b64b68 &&
                out.component_vtable!=source.base+0x1c54008))return false;
            const auto identity=rig::started(read,out.component);if(!identity)return false;
            out.component_identity=*identity;
            const auto members=rig::list(read,out.entity+0x268,256);if(!members)return false;
            unsigned matches{};
            for(unsigned i=0;i<members->packed_count;++i) {
                const auto member=rig::value<std::uintptr_t>(read,members->data+i*8);if(!member)return false;
                if(*member==out.component)++matches;
            }
            if(matches!=1)return false;
        }
        return true;
    }
public:
    template<class Read> static std::optional<Witness> capture(Read& read,const NativeWorldQuerySource& source,
            std::uint32_t handle,std::uint32_t key) noexcept {
        Witness result{};
        auto copy=[&]() {
            try {
                Witness first{},second{};
                if(!native_world_query_source_current(read,source) || !inspect(read,source,handle,key,first) ||
                   !inspect(read,source,handle,key,second) || !equal(first,second) ||
                   !native_world_query_source_current(read,source))return false;
                result=second;return true;
            }catch(...) {return false;}
        };
        if(!native_world_query_source_detail::guarded(copy))return {};
        return result;
    }
    template<class Read> static bool current(Read& read,const NativeWorldQuerySource& source,const Witness& witness) noexcept {
        if(source!=witness.source)return false;
        // Re-derive both record and shape from the current world/full ID. Saved
        // pointer values participate only in equality, never in read addresses.
        const auto fresh=capture(read,source,witness.handle,witness.contact_key);
        return fresh && equal(witness,*fresh);
    }
};
}
