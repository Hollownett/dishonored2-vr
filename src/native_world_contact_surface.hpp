#pragma once
#include "native_world_trace.hpp"

namespace d2vr {
class NativeWorldContactSurface {
public:
    inline static constexpr bool diagnostic_only=true;
    inline static constexpr std::uintptr_t sphere_vtable=0x1ec1e78,capsule_vtable=0x1ec1c98;
    struct Receipt {
        NativeWorldQuerySource source{};
        std::uintptr_t pool{},record{},shape{},shape_vtable{},userdata{},component{},entity{};
        std::uint32_t count{},handle{},broadphase{},entity_id{},motion{},filter{},body_flags{},contact_key{};
        std::uint16_t root_key{},shape_flags{},vertex_count{},vertex_offset{};
        std::uint8_t shape_type{},dimension{};
        rig::Identity component_identity{};
        std::array<std::uint32_t,12> pose{};
        std::uint32_t radius{};
        std::array<std::uint32_t,32> support{};
        std::array<std::uint32_t,8> capsule_endpoints{};
    };
    static bool equal(const Receipt& first,const Receipt& second) noexcept {
        return first.source==second.source && first.pool==second.pool && first.record==second.record &&
            first.shape==second.shape && first.shape_vtable==second.shape_vtable && first.userdata==second.userdata &&
            first.component==second.component && first.entity==second.entity && first.count==second.count &&
            first.handle==second.handle && first.broadphase==second.broadphase && first.entity_id==second.entity_id &&
            first.motion==second.motion && first.filter==second.filter && first.body_flags==second.body_flags &&
            first.contact_key==second.contact_key && first.root_key==second.root_key && first.shape_flags==second.shape_flags &&
            first.vertex_count==second.vertex_count && first.vertex_offset==second.vertex_offset &&
            first.shape_type==second.shape_type && first.dimension==second.dimension &&
            first.component_identity==second.component_identity && first.pose==second.pose && first.radius==second.radius &&
            first.support==second.support && first.capsule_endpoints==second.capsule_endpoints;
    }
    template<class Read> static std::optional<Receipt> capture(Read& read,const NativeWorldQuerySource& source,
            std::uint32_t handle,std::uint32_t contact_key) noexcept {
        Receipt receipt{};
        if(!guarded(read,source,handle,contact_key,receipt))return {};
        return receipt;
    }
    template<class Read> static bool current(Read& read,const Receipt& receipt) noexcept {
        const auto now=capture(read,receipt.source,receipt.handle,receipt.contact_key);
        return now && equal(receipt,*now);
    }
private:
    template<class Read> static bool inspect(Read& read,const NativeWorldQuerySource& source,
            std::uint32_t handle,std::uint32_t contact_key,Receipt& result) {
        if(!source.valid() || (contact_key!=0 && contact_key!=0xffffffffu) || handle==source.handle)return false;
        const auto body=NativeWorldTrace::inspect_hit_body(read,source,handle);if(!body)return false;
        result.source=source;result.pool=body->pool;result.record=body->record;result.count=body->count;
        result.handle=handle;result.broadphase=body->broadphase;result.userdata=body->userdata;
        result.component=body->component;result.entity=body->entity;result.entity_id=body->entity_id;result.contact_key=contact_key;
        if(!read(result.record+0x40,&result.motion,4) || !read(result.record+0x44,&result.body_flags,4) ||
           !read(result.record+0x60,&result.shape,8) || !rig::pointer(result.shape) ||
           !read(result.record+0x6a,&result.root_key,2) || !read(result.record+0x6c,&result.filter,4))return false;
        rig::Transform transform{};
        for(unsigned row=0;row<4;++row) {
            if(!read(result.record+row*16,result.pose.data()+row*3,12))return false;
            std::memcpy(transform.f.data()+row*4,result.pose.data()+row*3,12);
        }
        if(!rig::valid(transform) || !read(result.shape,&result.shape_vtable,8) ||
           !read(result.shape+0x20,&result.shape_flags,2) || !read(result.shape+0x22,&result.shape_type,1) ||
           !read(result.shape+0x24,&result.dimension,1) || result.dimension!=2 ||
           !read(result.shape+0x28,&result.radius,4) || !read(result.shape+0x40,&result.vertex_count,2) ||
           !read(result.shape+0x42,&result.vertex_offset,2))return false;
        const bool sphere=result.shape_vtable==source.base+sphere_vtable && result.shape_type==2 &&
            result.vertex_count==1 && result.vertex_offset==0x10;
        const bool capsule=result.shape_vtable==source.base+capsule_vtable && result.shape_type==3 &&
            result.vertex_count==8 && result.vertex_offset==0x50;
        if(!sphere && !capsule)return false;
        float radius{};std::memcpy(&radius,&result.radius,4);
        if(!std::isfinite(radius) || radius<=0 || radius>100)return false;
        const auto bytes=static_cast<std::size_t>(result.vertex_count)*16;
        if(!read(result.shape+0x40+result.vertex_offset,result.support.data(),bytes))return false;
        if(capsule && !read(result.shape+0x70,result.capsule_endpoints.data(),sizeof(result.capsule_endpoints)))return false;
        for(unsigned vertex=0;vertex<result.vertex_count;++vertex)for(unsigned axis=0;axis<3;++axis) {
            float coordinate{};std::memcpy(&coordinate,&result.support[vertex*4+axis],4);
            if(!std::isfinite(coordinate) || std::abs(coordinate)>1e7f)return false;
        }
        if(result.component) {
            if(!result.entity || !(rig::typed(read,source.base,result.component,0x1c745c8) ||
                rig::typed(read,source.base,result.component,0x1b64b68) || rig::typed(read,source.base,result.component,0x1c54008)))return false;
            const auto identity=rig::started(read,result.component);if(!identity)return false;
            result.component_identity=*identity;
            const auto membership=rig::list(read,result.entity+0x268,256);if(!membership)return false;
            unsigned matches{};
            for(unsigned index=0;index<membership->packed_count;++index) {
                const auto component=rig::value<std::uintptr_t>(read,membership->data+index*8);
                if(!component)return false;
                if(*component==result.component)++matches;
            }
            if(matches!=1)return false;
        } else if(result.userdata)return false;
        return true;
    }
    template<class Read> static bool cpp_capture(Read& read,const NativeWorldQuerySource& source,
            std::uint32_t handle,std::uint32_t contact_key,Receipt& output) noexcept {
        try {
            Receipt first{},second{};
            if(!native_world_query_source_current(read,source) || !inspect(read,source,handle,contact_key,first) ||
               !inspect(read,source,handle,contact_key,second) || !equal(first,second) ||
               !native_world_query_source_current(read,source))return false;
            output=second;return true;
        }catch(...) {return false;}
    }
    template<class Read> static bool guarded(Read& read,const NativeWorldQuerySource& source,
            std::uint32_t handle,std::uint32_t contact_key,Receipt& output) noexcept {
        __try {return cpp_capture(read,source,handle,contact_key,output);}
        __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
};
}
