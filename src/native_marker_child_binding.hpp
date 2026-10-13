#pragma once
#include "native_hud_child_draw.hpp"
#include "tracked_pose.hpp"
#include "grab_beam.hpp"
namespace d2vr {
struct NativeMarkerChildPacket {
    std::uintptr_t marker{},manager{},entity{},record{};unsigned slot{},spawn{};
    Vec3 world{};NativeHudArtGeometry geometry{};
    std::array<std::byte,40> receiver_record{};UINT64 qualified_tick{};
    bool offscreen{}; // marker+640==1: native clamped/edge indicator at receiver +398
};
// The exact level native camera anchor and roomscale compensation used for the
// rendered scene, transported together. An old but time-fresh heading is not
// interchangeable with a new frame after snap turning or camera takeover.
struct NativeMarkerWorldFrame {
    WorldTrackedPose anchor;TrackedPose origin;Vec3 controller_centre;
    UINT64 epoch{},render_frame{},tick{};
};
inline std::optional<Vec3> native_marker_world_point(Vec3 world,const NativeMarkerWorldFrame& frame,
        UINT64 epoch,UINT64 render_frame,UINT64 anchor_tick,UINT64 now,double units_per_metre=1.0) noexcept {
    if(!epoch || frame.epoch!=epoch || !render_frame || frame.render_frame!=render_frame || !frame.tick ||
       !anchor_tick || now<frame.tick || now-frame.tick>250 || now<anchor_tick || now-anchor_tick>250)return {};
    try {return world_to_xr_controller(world,frame.origin,frame.anchor,units_per_metre,frame.controller_centre,frame.origin.position);}
    catch(...){return {};}
}
// No hook and no Update call. Invoke only AFTER the shared byte-verified
// 677B10 original, from its53AAEE return site, on that native owner thread.
class NativeMarkerChildBinding {
public:
    using Read=NativeWristHud::Read;using Api=NativeInteractionPrompt::Api;
    static std::optional<NativeMarkerChildPacket> observe(Read read,std::uintptr_t base,std::uintptr_t caller,
            std::uintptr_t marker,std::uintptr_t current_hud_file,const Api& api) {
        if(!read || !api.get || caller!=0x53aaee || !rig::pointer(marker) || !wrist_hud_file_owned(read,base,current_hud_file)) return {};
        // Manager53AA20 walks exactly this native marker array;53A2F0 and
        // pure677030(marker,0) resolve the same slot and return marker+F0.
        const auto manager=rig::value<std::uintptr_t>(read,marker+0x78);
        const auto slot=rig::value<unsigned>(read,marker+0xd4);
        const auto spawn=rig::value<unsigned>(read,marker+0xc);
        const auto world=rig::value<std::array<float,3>>(read,marker+0x3c);
        if(!manager || !slot || !spawn || !world || !rig::pointer(*manager) || *slot>=256 ||
           rig::value<unsigned char>(read,marker+0x94)!=1 || rig::value<unsigned char>(read,marker+0x96)!=0) return {};
        // 677030(marker,1) selects +398 when marker+640 is set (clamped/offscreen); else +F0.
        const auto mode=rig::value<unsigned char>(read,marker+0x640);if(!mode || *mode>1) return {};
        const bool offscreen=*mode==1;
        const auto list=rig::value<std::uintptr_t>(read,*manager+0x18);const auto packed=rig::value<unsigned>(read,*manager+0x20);
        if(!list || !packed || (*packed&0xffffff)>256 || *slot>=(*packed&0xffffff) ||
           rig::value<std::uintptr_t>(read,*list+*slot*8)!=marker) return {};
        const auto game=rig::value<std::uintptr_t>(read,base+0x39a5290);
        if(!game || !rig::typed(read,base,*game,0x1c940a8) || *spawn==0x3fff0010 || (*spawn>>16)>=0x3fff) return {};
        const auto entity=rig::value<std::uintptr_t>(read,*game+0x3130b0+(*spawn>>16)*8);
        if(!entity || !rig::pointer(*entity) || rig::value<unsigned>(read,*entity+0xa8)!=spawn) return {};
        const Vec3 anchor{(*world)[0],(*world)[1],(*world)[2]};if(!finite(anchor)) return {};
        const auto receiver=marker+(offscreen?0x398:0xf0);
        const auto iggy=rig::value<std::uintptr_t>(read,receiver+8),table=rig::value<std::uintptr_t>(read,receiver+0x18);
        const auto index=rig::value<unsigned>(read,receiver+0x20);
        if(!iggy || !table || !index || !rig::pointer(*table) || !(*index&0xffffff) || (*index&0xffffff)>16 ||
           rig::value<std::uintptr_t>(read,current_hud_file+0x1608)!=iggy) return {};
        const auto record=*table+((*index&0xffffff)-1)*0x28;
        std::array<std::byte,40> receiver_record{};if(!read(record,receiver_record.data(),receiver_record.size())) return {};
        std::uint32_t status{};
        const auto value=api.get(reinterpret_cast<const void*>(record),0,nullptr,nullptr,"D2VR marker display identity",&status);
        if(status || value==7 || (value&3)!=1 || !rig::pointer(value-1) || rig::value<unsigned char>(read,value-1)!=0x24) return {};
        const auto node=rig::value<std::uintptr_t>(read,value+0x27);
        if(!node || !NativeHudArtProbe::in_tree(read,*iggy,*node)) return {};
        NativeHudArtGeometry geometry{};geometry.file=current_hud_file;geometry.iggy=*iggy;geometry.display_node=*node;
        double* fields[]{&geometry.x,&geometry.y,&geometry.width,&geometry.height};
        const char* names[]{"x","y","width","height","visible"};
        for(unsigned i=0;i<5;++i) {
            status=0;const auto field=api.get(reinterpret_cast<const void*>(record),0,names[i],nullptr,"D2VR marker native geometry",&status);
            if(status || field==7) return {};
            if(i<4) {const auto number=native_prompt_number(read,field);if(!number)return {};*fields[i]=*number;}
            else if(field==base+0x2424049) geometry.visible=true;
            else if(field==base+0x2424041) geometry.visible=false;else return {};
            geometry.fields|=1u<<i;
        }
        geometry.valid=geometry.width>0 && geometry.height>0 && geometry.width<=10000 && geometry.height<=10000;
        if(!geometry.valid || !geometry.visible || rig::value<std::uintptr_t>(read,marker+0x78)!=manager ||
           rig::value<unsigned>(read,marker+0xd4)!=slot || rig::value<unsigned>(read,marker+0xc)!=spawn ||
           rig::value<std::array<float,3>>(read,marker+0x3c)!=world || rig::value<std::uintptr_t>(read,*list+*slot*8)!=marker ||
           rig::value<std::uintptr_t>(read,receiver+0x18)!=table || rig::value<unsigned>(read,receiver+0x20)!=index ||
           rig::value<std::uintptr_t>(read,current_hud_file+0x1608)!=iggy || !wrist_hud_file_owned(read,base,current_hud_file) ||
           rig::value<std::uintptr_t>(read,*game+0x3130b0+(*spawn>>16)*8)!=entity || rig::value<unsigned>(read,*entity+0xa8)!=spawn ||
           !NativeHudArtProbe::in_tree(read,*iggy,*node)) return {};
        std::array<std::byte,40> again{};
        if(!read(record,again.data(),again.size()) || again!=receiver_record || rig::value<unsigned char>(read,marker+0x640)!=*mode) return {};
        return NativeMarkerChildPacket{marker,*manager,*entity,record,*slot,*spawn,anchor,geometry,receiver_record,GetTickCount64(),offscreen};
    }
    // No AS3 calls. Geometry/node identity expires250ms after a full observe;
    // refresh changes only the world anchor and never extends qualification.
    // Actual native child draw remains required for fresh artwork/visibility.
    static std::optional<NativeMarkerChildPacket> refresh(Read read,std::uintptr_t base,std::uintptr_t caller,
            std::uintptr_t marker,std::uintptr_t current_hud_file,const NativeMarkerChildPacket& cached) {
        const auto now=GetTickCount64();
        if(!read || caller!=0x53aaee || cached.marker!=marker || cached.geometry.file!=current_hud_file ||
           !cached.qualified_tick || now<cached.qualified_tick || now-cached.qualified_tick>250 ||
           !wrist_hud_file_owned(read,base,current_hud_file)) return {};
        const auto manager=rig::value<std::uintptr_t>(read,marker+0x78),list=rig::value<std::uintptr_t>(read,cached.manager+0x18);
        const auto packed=rig::value<unsigned>(read,cached.manager+0x20);
        const auto game=rig::value<std::uintptr_t>(read,base+0x39a5290);
        const auto receiver=marker+(cached.offscreen?0x398:0xf0);
        const auto table=rig::value<std::uintptr_t>(read,receiver+0x18);const auto index=rig::value<unsigned>(read,receiver+0x20);
        const auto anchor=rig::value<std::array<float,3>>(read,marker+0x3c);
        std::array<std::byte,40> record{};
        if(manager!=cached.manager || !list || !packed || (*packed&0xffffff)>256 || cached.slot>=(*packed&0xffffff) ||
           rig::value<unsigned>(read,marker+0xd4)!=cached.slot || rig::value<std::uintptr_t>(read,*list+cached.slot*8)!=marker ||
           rig::value<unsigned>(read,marker+0xc)!=cached.spawn || rig::value<unsigned char>(read,marker+0x94)!=1 ||
           rig::value<unsigned char>(read,marker+0x96)!=0 || rig::value<unsigned char>(read,marker+0x640)!=(cached.offscreen?1:0) ||
           !game || !rig::typed(read,base,*game,0x1c940a8) || (cached.spawn>>16)>=0x3fff ||
           rig::value<std::uintptr_t>(read,*game+0x3130b0+(cached.spawn>>16)*8)!=cached.entity ||
           rig::value<unsigned>(read,cached.entity+0xa8)!=cached.spawn ||
           rig::value<std::uintptr_t>(read,receiver+8)!=cached.geometry.iggy || !table || !index ||
           !(*index&0xffffff) || (*index&0xffffff)>16 || *table+((*index&0xffffff)-1)*40!=cached.record ||
           !read(cached.record,record.data(),record.size()) || record!=cached.receiver_record ||
           rig::value<std::uintptr_t>(read,current_hud_file+0x1608)!=cached.geometry.iggy ||
           !NativeHudArtProbe::in_tree(read,cached.geometry.iggy,cached.geometry.display_node) || !anchor) return {};
        const Vec3 world{(*anchor)[0],(*anchor)[1],(*anchor)[2]};if(!finite(world)) return {};
        std::array<std::byte,40> again{};
        if(rig::value<std::uintptr_t>(read,marker+0x78)!=manager || rig::value<unsigned>(read,marker+0xd4)!=cached.slot ||
           rig::value<unsigned>(read,marker+0xc)!=cached.spawn || rig::value<std::array<float,3>>(read,marker+0x3c)!=anchor ||
           rig::value<std::uintptr_t>(read,*list+cached.slot*8)!=marker || rig::value<unsigned char>(read,marker+0x640)!=(cached.offscreen?1:0) ||
           !read(cached.record,again.data(),again.size()) || again!=record ||
           rig::value<std::uintptr_t>(read,*game+0x3130b0+(cached.spawn>>16)*8)!=cached.entity ||
           rig::value<unsigned>(read,cached.entity+0xa8)!=cached.spawn || !wrist_hud_file_owned(read,base,current_hud_file) ||
           !NativeHudArtProbe::in_tree(read,cached.geometry.iggy,cached.geometry.display_node)) return {};
        auto out=cached;out.world=world;return out;
    }
    // Publisher mutates only the mod's capture bindings. Root retains the packet
    // world anchor keyed by marker/epoch for scene placement; no Iggy writes.
    static bool publish(const NativeMarkerChildPacket& packet,UINT64 epoch,UINT64 tick) {
        // A cheap anchor refresh does not renew native geometry qualification.
        // Using caller tick here previously extended stale bindings indefinitely.
        return epoch && tick && packet.qualified_tick && tick>=packet.qualified_tick && tick-packet.qualified_tick<=250 &&
            NativeHudChildDraw::publish(NativeHudChildKind::objective,packet.geometry,packet.marker,epoch,packet.qualified_tick);
    }
};
} // namespace d2vr
