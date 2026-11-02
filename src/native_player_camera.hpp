#pragma once
// Exact-build, read-only gameplay state. Generated camera fields: 0x159baf0.
// Physics allocation/ownership: 0x7fe310, 0x1b2290 and 0x694c20.
#include "game_camera.hpp"
#include "game_rig.hpp"
#include "native_action_camera.hpp"

namespace d2vr {
template<class Read> bool native_shadow_walk_active(Read& read,std::uintptr_t base,
        std::uintptr_t player,std::uintptr_t controller) {
    using namespace rig;
    if(!typed(read,base,controller,0x1c72918) ||
       value<std::uint8_t>(read,controller+0x172)!=0) return false;
    const auto machine=value<std::uintptr_t>(read,controller+0x58);
    if(!machine || !typed(read,base,*machine,0x1c73d58) ||
       value<std::uintptr_t>(read,*machine+0x10)!=player) return false;
    const auto machine_id=started(read,*machine),controller_id=started(read,controller);
    const auto components=list(read,player+0x268,4096);
    if(!machine_id || !controller_id || !components) return false;
    std::optional<std::uint32_t> member;
    for(std::uint32_t i=0;i<components->packed_count;++i) {
        const auto component=value<std::uintptr_t>(read,components->data+i*8);
        if(!component) return false;
        if(*component==*machine) {if(member) return false;member=i;}
    }
    if(!member) return false;
    const auto pool=value<std::uintptr_t>(read,*machine+0x90);
    if(!pool || !pointer(*pool)) return false;
    const auto mode=value<std::uintptr_t>(read,*pool+8);
    if(!mode || !typed(read,base,*mode,0x1c83ed8) || value<unsigned>(read,*mode+0x60)!=2u) return false;
    std::array<std::uintptr_t,native_action_camera_states.size()> scripted{};
    for(std::size_t i=0;i<scripted.size();++i) {
        const auto& entry=native_action_camera_states[i];if(entry.combat) continue;
        const auto state=value<std::uintptr_t>(read,*pool+0x10+entry.index*8);
        if(!state) return false;
        scripted[i]=*state;
        if(*state && (!typed(read,base,*state,entry.vtable) ||
           value<unsigned>(read,*state+8)!=entry.index || value<std::uintptr_t>(read,*state+0x10)!=machine ||
           value<std::uint8_t>(read,*state+0x20)!=0)) return false;
    }
    for(unsigned index=0;index<2;++index) {
        const auto at=*pool+0x10+index*8;
        const auto state=value<std::uintptr_t>(read,at);
        if(!state || !typed(read,base,*state,0x1c7ed28) ||
           value<unsigned>(read,*state+8)!=index ||
           value<std::uintptr_t>(read,*state+0x10)!=machine ||
           value<std::uint8_t>(read,*state+0x20)!=1) continue;
        const auto decl=value<std::uintptr_t>(read,*state+0x18);
        if(!decl || !typed(read,base,*decl,0x1c7ebf8) || value<unsigned>(read,*decl+0x60)!=index) return false;
        for(std::size_t i=0;i<scripted.size();++i) {
            const auto& entry=native_action_camera_states[i];if(entry.combat) continue;
            if(value<std::uintptr_t>(read,*pool+0x10+entry.index*8)!=scripted[i] ||
               (scripted[i] && (!typed(read,base,scripted[i],entry.vtable) ||
                value<unsigned>(read,scripted[i]+8)!=entry.index ||
                value<std::uintptr_t>(read,scripted[i]+0x10)!=machine ||
                value<std::uint8_t>(read,scripted[i]+0x20)!=0))) return false;
        }
        const auto after=list(read,player+0x268,4096);
        return after && after->data==components->data && after->packed_count==components->packed_count &&
            after->packed_capacity==components->packed_capacity &&
            value<std::uintptr_t>(read,after->data+*member*8)==machine &&
            typed(read,base,controller,0x1c72918) &&
            value<std::uint8_t>(read,controller+0x172)==0 &&
            value<std::uintptr_t>(read,controller+0x10)==player &&
            value<std::uintptr_t>(read,controller+0x58)==machine &&
            typed(read,base,*machine,0x1c73d58) &&
            value<std::uintptr_t>(read,*machine+0x10)==player &&
            value<std::uintptr_t>(read,*machine+0x90)==pool &&
            value<std::uintptr_t>(read,*pool+8)==mode && typed(read,base,*mode,0x1c83ed8) && value<unsigned>(read,*mode+0x60)==2u &&
            value<std::uintptr_t>(read,at)==state && typed(read,base,*state,0x1c7ed28) &&
            value<unsigned>(read,*state+8)==index &&
            value<std::uintptr_t>(read,*state+0x10)==machine &&
            value<std::uintptr_t>(read,*state+0x18)==decl && typed(read,base,*decl,0x1c7ebf8) &&
            value<unsigned>(read,*decl+0x60)==index &&
            value<std::uint8_t>(read,*state+0x20)==1 &&
            started(read,*machine)==machine_id && started(read,controller)==controller_id;
    }
    return false;
}

struct NativePlayerCamera {
    std::uintptr_t player{},camera{},body{},controller{},physics{},heading_delegate{};
    rig::Identity camera_identity{},physics_identity{};
    int mode{},target_mode{};
    bool blend_body_yaw{},code_lock{},shadow_walk{};
    float pitch{},yaw{},roll{},heading{},pitch_velocity{},yaw_velocity{},physics_yaw_velocity{},physics_yaw_impulse{};
    std::array<float,3> aim_origin{};
    std::array<float,9> aim_axis{};
    std::array<float,9> entity_axis{};
    std::array<float,2> look{};
    std::optional<GameCameraPose> view;
    bool tracked_shadow_walk() const noexcept {
        return shadow_walk && mode==7 && target_mode==7 && !blend_body_yaw && !code_lock;
    }
    bool authored_view() const noexcept {
        return !tracked_shadow_walk() && (mode!=0 || target_mode!=0 || blend_body_yaw || code_lock);
    }
};

template<class Read> std::optional<NativePlayerCamera> native_player_camera(
        Read& read,std::uintptr_t base,std::uintptr_t camera) {
    using namespace rig;
    auto game=value<std::uintptr_t>(read,base+0x39a5290);
    if(!game || !typed(read,base,*game,0x1c940a8)) return {};
    auto player=value<std::uintptr_t>(read,*game+0x3130b0);
    if(!player || !typed(read,base,*player,0x1c5e428) ||
       !typed(read,base,camera,camera_vtable,camera_secondary_vtable) ||
       value<std::uintptr_t>(read,camera+0x10)!=player) return {};
    auto id=started(read,camera);
    if(!id) return {};
    std::array<std::byte,0x200> bytes{};
    if(!read(camera+0x25150,bytes.data(),bytes.size())) return {};
    auto field=[&](auto& out,std::size_t offset) {
        std::memcpy(&out,bytes.data()+offset-0x25150,sizeof(out));
    };
    NativePlayerCamera s{};s.player=*player;s.camera=camera;s.camera_identity=*id;
    field(s.controller,0x25158);field(s.physics,0x25160);field(s.body,0x25170);
    field(s.mode,0x25200);field(s.target_mode,0x25204);
    s.code_lock=bytes[0x69]!=std::byte{};s.blend_body_yaw=bytes[0x110]!=std::byte{};
    field(s.pitch,0x25264);field(s.yaw,0x25274);field(s.roll,0x25278);
    field(s.aim_origin,0x2527c);field(s.aim_axis,0x25288);
    field(s.pitch_velocity,0x252ac);field(s.yaw_velocity,0x252b0);
    if(!typed(read,base,s.body,0x1c5a8d8,0x1c5a9b0) ||
       !typed(read,base,s.controller,0x1c72918) ||
       !typed(read,base,s.physics,0x1c73268,0x1c73340) ||
       value<std::uintptr_t>(read,s.physics+0x30)!=base+0x1c73378 ||
       value<std::uintptr_t>(read,s.body+0x10)!=player ||
       value<std::uintptr_t>(read,s.controller+0x10)!=player ||
       value<std::uintptr_t>(read,s.physics+0x10)!=player) return {};
    auto physics_id=started(read,s.physics);auto delegate=value<std::uintptr_t>(read,s.physics+0x48);
    if(!physics_id || !delegate || !pointer(*delegate)) return {};
    s.physics_identity=*physics_id;s.heading_delegate=*delegate;
    // The allocated physics object is not polymorphic at offset zero.
    if(value<std::uintptr_t>(read,*delegate+0x160)!=base+0x1b02768 ||
       value<std::uintptr_t>(read,*delegate+0x168)!=s.physics ||
       value<std::uintptr_t>(read,*delegate+0x120)!=*player+0x300 ||
       value<std::uintptr_t>(read,*delegate+0x1e0)!=delegate ||
       !read(*delegate+0xd0,&s.heading,4) ||
       !read(*delegate+0xbc,&s.physics_yaw_velocity,4) || !read(*delegate+0x40,&s.physics_yaw_impulse,4) ||
       !read(*player+0x30c,s.entity_axis.data(),sizeof(s.entity_axis)) ||
       !read(s.controller+0xd0,s.look.data(),sizeof(s.look))) return {};
    const auto valid=[](float f) {return std::isfinite(f) && std::abs(f)<1e7f;};
    for(auto f:{s.pitch,s.yaw,s.roll,s.heading,s.pitch_velocity,s.yaw_velocity,s.physics_yaw_velocity,s.physics_yaw_impulse,s.look[0],s.look[1]})
        if(!valid(f)) return {};
    for(auto f:s.aim_origin) if(!valid(f)) return {};
    for(auto f:s.aim_axis) if(!valid(f)) return {};
    for(auto f:s.entity_axis) if(!valid(f)) return {};
    if(std::abs(s.heading)>3.141603f || s.mode<0 || s.mode>32 || s.target_mode<0 || s.target_mode>32) return {};
    std::array<std::byte,camera_read_size> view_bytes{};
    if(read(camera,view_bytes.data(),view_bytes.size())) s.view=decode_game_camera(view_bytes,base);
    if(value<Identity>(read,camera+0x18)!=id || value<Identity>(read,s.physics+0x18)!=physics_id ||
       value<std::uintptr_t>(read,camera+0x10)!=player ||
       value<std::uintptr_t>(read,camera+0x25160)!=s.physics ||
       value<std::uintptr_t>(read,s.physics+0x48)!=delegate ||
       value<std::uintptr_t>(read,*game+0x3130b0)!=player) return {};
    if(s.mode==7 && s.target_mode==7 && !s.blend_body_yaw && !s.code_lock) {
        s.shadow_walk=native_shadow_walk_active(read,base,*player,s.controller);
        if(s.shadow_walk && (value<std::array<int,2>>(read,camera+0x25200)!=std::array<int,2>{7,7} ||
           value<std::uint8_t>(read,camera+0x251b9)!=0 || value<std::uint8_t>(read,camera+0x25260)!=0 ||
           value<Identity>(read,camera+0x18)!=id || value<std::uintptr_t>(read,camera+0x10)!=player ||
           value<std::uintptr_t>(read,camera+0x25158)!=s.controller ||
           value<std::uintptr_t>(read,camera+0x25160)!=s.physics || value<Identity>(read,s.physics+0x18)!=physics_id ||
           value<std::uintptr_t>(read,camera+0x25170)!=s.body || value<std::uintptr_t>(read,s.physics+0x48)!=delegate ||
           value<std::uintptr_t>(read,base+0x39a5290)!=game || value<std::uintptr_t>(read,*game+0x3130b0)!=player)) return {};
    }
    return s;
}
// The game and its player entity exist (a level is loaded), without validating the camera: scripted scenes such as the boat ride
// leave the camera in a state find_native_player_camera rejects (v722).
template<class Read> bool native_player_present(Read& read,std::uintptr_t base) {
    using namespace rig;
    const auto game=value<std::uintptr_t>(read,base+0x39a5290);
    if(!game || !typed(read,base,*game,0x1c940a8)) return false;
    const auto player=value<std::uintptr_t>(read,*game+0x3130b0);
    return player && typed(read,base,*player,0x1c5e428);
}
template<class Read> std::optional<NativePlayerCamera> find_native_player_camera(Read& read,std::uintptr_t base) {
    using namespace rig;
    auto game=value<std::uintptr_t>(read,base+0x39a5290);
    if(!game || !typed(read,base,*game,0x1c940a8)) return {};
    auto player=value<std::uintptr_t>(read,*game+0x3130b0);
    if(!player || !typed(read,base,*player,0x1c5e428)) return {};
    auto components=list(read,*player+0x268,4096);
    if(!components) return {};
    std::uintptr_t camera{};
    for(std::uint32_t i=0;i<components->packed_count;++i) {
        auto at=value<std::uintptr_t>(read,components->data+i*8);
        if(at && typed(read,base,*at,camera_vtable,camera_secondary_vtable) && value<std::uintptr_t>(read,*at+0x10)==player) {
            if(camera) return {};
            camera=*at;
        }
    }
    auto after=list(read,*player+0x268,4096);
    if(!camera || !after || after->data!=components->data || after->packed_count!=components->packed_count ||
       value<std::uintptr_t>(read,*game+0x3130b0)!=player) return {};
    return native_player_camera(read,base,camera);
}
} // namespace d2vr
