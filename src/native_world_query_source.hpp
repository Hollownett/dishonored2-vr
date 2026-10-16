#pragma once
#include "native_player_camera.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <type_traits>
#if defined(_MSC_VER)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace d2vr {
struct NativeWorldQuerySource {
    std::uintptr_t base{},game{},player{},camera{},camera_controller{},physics{},delegate{},controller{},world{},pool{},body{};
    std::uint32_t handle{},query_filter_info{},body_filter_info{},spawn{},body_count{},broadphase{};
    std::uint8_t body_flags{};
    rig::Identity camera_identity{},physics_identity{};
    std::uintptr_t component_list_data{};
    std::uint32_t component_list_count{},component_list_capacity{};
    bool valid() const {
        return rig::pointer(base) && rig::pointer(game) && rig::pointer(player) && rig::pointer(camera) &&
            rig::pointer(camera_controller) && rig::pointer(physics) && rig::pointer(delegate) && rig::pointer(controller) && rig::pointer(world) &&
            rig::pointer(pool) && rig::pointer(body) && handle!=0xffffffffu && (handle&0xffffffu)!=0xffffffu &&
            body_count && body_count<=0x1000000u && (handle&0xffffffu)<body_count && (body_flags&3u) && broadphase!=0xffffffffu && spawn &&
            camera_identity.generation!=-1 && physics_identity.generation!=-1 && rig::pointer(component_list_data) &&
            component_list_count>=4 && component_list_count<=component_list_capacity && component_list_capacity<=4096;
    }
};
inline bool operator==(const NativeWorldQuerySource& first,const NativeWorldQuerySource& second) {
    return first.base==second.base && first.game==second.game && first.player==second.player && first.camera==second.camera &&
        first.camera_controller==second.camera_controller && first.physics==second.physics && first.delegate==second.delegate &&
        first.controller==second.controller && first.world==second.world && first.pool==second.pool && first.body==second.body &&
        first.handle==second.handle && first.query_filter_info==second.query_filter_info && first.body_filter_info==second.body_filter_info && first.spawn==second.spawn &&
        first.body_count==second.body_count && first.broadphase==second.broadphase && first.body_flags==second.body_flags &&
        first.camera_identity==second.camera_identity && first.physics_identity==second.physics_identity &&
        first.component_list_data==second.component_list_data && first.component_list_count==second.component_list_count &&
        first.component_list_capacity==second.component_list_capacity;
}
inline bool operator!=(const NativeWorldQuerySource& first,const NativeWorldQuerySource& second) {return !(first==second);}

namespace native_world_query_source_detail {
template<class Read> std::optional<rig::List> player_component_membership(Read& read,std::uintptr_t player,
        std::uintptr_t camera,std::uintptr_t body,std::uintptr_t camera_controller,std::uintptr_t physics) {
    using namespace rig;
    const auto before=list(read,player+0x268,4096);
    if(!before || !before->packed_count || !pointer(before->data))return {};
    std::array<std::uint32_t,4> matches{};
    for(std::uint32_t index=0;index<before->packed_count;++index) {
        const auto component=value<std::uintptr_t>(read,before->data+static_cast<std::uintptr_t>(index)*sizeof(std::uintptr_t));
        if(!component)return {};
        if(*component==camera)++matches[0];
        if(*component==body)++matches[1];
        if(*component==camera_controller)++matches[2];
        if(*component==physics)++matches[3];
    }
    const auto after=list(read,player+0x268,4096);
    if(!after || before->data!=after->data || before->packed_count!=after->packed_count ||
       before->packed_capacity!=after->packed_capacity || matches[0]!=1 || matches[1]!=1 ||
       matches[2]!=1 || matches[3]!=1)return {};
    return before;
}
template<class Read> std::optional<NativeWorldQuerySource> inspect(Read& read,std::uintptr_t base,std::uintptr_t camera_address,std::uintptr_t expected_world) {
    using namespace rig;
    const auto camera=native_player_camera(read,base,camera_address);
    if(!camera)return {};
    const auto components=player_component_membership(read,camera->player,camera->camera,camera->body,camera->controller,camera->physics);
    if(!components)return {};
    const auto game=value<std::uintptr_t>(read,base+0x39a5290);
    if(!game || !typed(read,base,*game,0x1c940a8) || value<std::uintptr_t>(read,*game+0x3130b0)!=camera->player) return {};
    const auto spawn=value<std::uint32_t>(read,camera->player+0xa8);
    if(!spawn || !*spawn || *spawn==0x3fff0010u || *spawn==0xffffffffu || (*spawn>>16)>=0x3fff ||
       value<std::uintptr_t>(read,*game+0x3130b0+static_cast<std::uintptr_t>(*spawn>>16)*8)!=camera->player) return {};
    const auto world=value<std::uintptr_t>(read,*game+0x60410);
    if(!world || *world!=expected_world || !pointer(*world))return {};
    const auto controller=value<std::uintptr_t>(read,camera->heading_delegate+0x148);
    if(!controller || !typed(read,base,*controller,0x1ad26a0) ||
       value<std::uintptr_t>(read,*controller+0x140)!=*game+0x60280 ||
       value<std::uintptr_t>(read,*controller+0x40)!=*world ||
       value<std::uintptr_t>(read,camera->camera+0x25160)!=camera->physics) return {};
    const auto handle=value<std::uint32_t>(read,*controller+0x38);
    const auto query_filter_info=value<std::uint32_t>(read,*controller+0x3c);
    const auto pool=value<std::uintptr_t>(read,*world+0x28);
    const auto count=value<std::uint32_t>(read,*world+0x30);
    if(!handle || !query_filter_info || !pool || !pointer(*pool) || !count || *count>0x1000000u) return {};
    const std::uint32_t index=*handle&0xffffffu;
    if(index==0xffffffu || index>=*count)return {};
    const auto body=*pool+static_cast<std::uintptr_t>(index)*0xb0;
    if(!pointer(body))return {};
    const auto flags=value<std::uint8_t>(read,body+0x44);
    const auto body_handle=value<std::uint32_t>(read,body+0x70);
    const auto broadphase=value<std::uint32_t>(read,body+0x78);
    const auto body_filter_info=value<std::uint32_t>(read,body+0x6c);
    if(!flags || ((*flags&3u)==0) || body_handle!=handle || !broadphase || *broadphase==0xffffffffu || !body_filter_info) return {};
    NativeWorldQuerySource source{base,*game,camera->player,camera->camera,camera->controller,camera->physics,camera->heading_delegate,
        *controller,*world,*pool,body,*handle,*query_filter_info,*body_filter_info,*spawn,*count,*broadphase,*flags,
        camera->camera_identity,camera->physics_identity,components->data,components->packed_count,components->packed_capacity};
    if(!source.valid())return {};
    const auto confirmed_components=player_component_membership(read,camera->player,camera->camera,camera->body,camera->controller,camera->physics);
    if(!confirmed_components || confirmed_components->data!=components->data ||
       confirmed_components->packed_count!=components->packed_count || confirmed_components->packed_capacity!=components->packed_capacity)return {};
    return source;
}
static bool invoke_seh(bool(*invoke)(void*),void* context) {
#if defined(_MSC_VER)
    __try {return invoke(context);}
    __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
#else
    return invoke(context);
#endif
}
template<class Function> bool guarded(Function& function) {
    struct Context {Function* function;};
    Context context{&function};
    const auto invoke=[](void* raw)->bool {return (*static_cast<Context*>(raw)->function)();};
    return invoke_seh(invoke,&context);
}
}

template<class Read> std::optional<NativeWorldQuerySource> native_world_query_source(Read& read,std::uintptr_t base,
        std::uintptr_t camera_address,std::uintptr_t world) {
    std::optional<NativeWorldQuerySource> result;
    auto inspect=[&]() {
        try {
            const auto first=native_world_query_source_detail::inspect(read,base,camera_address,world);
            if(!first)return false;
            const auto confirmed=native_world_query_source_detail::inspect(read,base,camera_address,world);
            if(!confirmed || *first!=*confirmed)return false;
            result=first;return true;
        } catch(...) {result.reset();return false;}
    };
    if(!native_world_query_source_detail::guarded(inspect))return {};
    return result;
}

template<class Read> bool native_world_query_source_current(Read& read,const NativeWorldQuerySource& source) {
    bool current{};
    auto inspect=[&]() {
        try {
            if(!source.valid())return false;
            const auto value=native_world_query_source_detail::inspect(read,source.base,source.camera,source.world);
            current=value && *value==source;return current;
        } catch(...) {current=false;return false;}
    };
    return native_world_query_source_detail::guarded(inspect) && current;
}
}
