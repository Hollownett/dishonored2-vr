#pragma once
#include "native_hud_child_draw.hpp"

namespace d2vr {
struct NativeWristArtPublication {
    NativeHudChildKind kind{};
    NativeHudArtGeometry geometry{};
    std::uintptr_t owner{};
    const char* path{};
    bool valid{};
};
struct NativeHudMasterLayoutIdentity {
    std::uintptr_t file{},iggy{},node{};
    explicit operator bool() const noexcept{return file && iggy && node;}
};
// Read-only binding of individual native artwork. Use exclusively AFTER the
// verified shared UI Call, on its owning thread. No hook or setters here.
class NativeWristArtPublisher {
public:
    using Read=NativeInteractionPrompt::Read;
    using Api=NativeInteractionPrompt::Api;
    static constexpr std::array<const char*,3> paths{
        "_masterHUD._healthMeter","_masterHUD._manaMeter","_masterHUD._equipmentIcon"};
    static constexpr std::array<NativeHudChildKind,3> kinds{
        NativeHudChildKind::health,NativeHudChildKind::mana,NativeHudChildKind::equipment};

    static std::optional<NativeHudMasterLayoutIdentity> master_layout(Read read,std::uintptr_t base,const Api& api,
        bool before,std::uintptr_t caller,std::uintptr_t method,std::uintptr_t receiver,
        std::uintptr_t block,std::uint32_t result) {
        if(before || result!=1 || !read || !api.construct || !api.destroy || !api.get)return {};
        auto reader=read;const auto owned=wrist_hud_arguments(reader,base,caller,method,receiver,block);
        if(!owned)return {};
        const auto field=wrist_hud_caller(caller);
        const auto offset=field==WristHudField::health?0x70u:field==WristHudField::mana?0x88u:
            field==WristHudField::power?0xb0u:0x80u;
        const NativePromptCall owner{owned->file,owned->iggy,method-offset};
        // Resolve only this public container. No numeric geometry getters,
        // whole-movie root qualification or guessed damage-mask rectangles.
        const auto node=NativeHudArtProbe::display_node(read,owner,api,"_masterHUD");
        const auto again=wrist_hud_arguments(reader,base,caller,method,receiver,block);
        if(!node || node==owner.iggy+0x26b0 || !again || again->file!=owner.file || again->iggy!=owner.iggy ||
           !wrist_hud_file_owned(reader,base,owner.file) || rig::value<std::uintptr_t>(reader,owner.file+0x1608)!=owner.iggy ||
           !NativeHudArtProbe::in_tree(read,owner.iggy,node))return {};
        return NativeHudMasterLayoutIdentity{owner.file,owner.iggy,node};
    }

    static std::array<NativeWristArtPublication,3> observe(Read read,std::uintptr_t base,const Api& api,
        bool before,std::uintptr_t caller,std::uintptr_t method,std::uintptr_t receiver,
        std::uintptr_t block,std::uint32_t result) {
        std::array<NativeWristArtPublication,3> out{};
        if(before || result!=1 || !read || !api.construct || !api.destroy || !api.get)return out;
        auto reader=read;
        const auto owned=wrist_hud_arguments(reader,base,caller,method,receiver,block);
        if(!owned)return out;
        const auto field=wrist_hud_caller(caller);
        const auto offset=field==WristHudField::health?0x70u:field==WristHudField::mana?0x88u:
            field==WristHudField::power?0xb0u:0x80u;
        return observe_owner(read,base,api,{owned->file,owned->iggy,method-offset});
    }
    // Owner of a stat call that arrived before the HUD movie was first drawn (load time). The game repeats these
    // calls only when a value changes, so without a deferred probe the vitals stay unbound for a whole level.
    template<class R> static std::optional<NativePromptCall> early_owner(R read,std::uintptr_t caller,std::uintptr_t method,std::uintptr_t receiver) {
        const auto field=wrist_hud_caller(caller);
        if(field==WristHudField::none || method<0x100)return {};
        const auto delta=field==WristHudField::health?0xf0u:field==WristHudField::mana?0x30u:field==WristHudField::gadget?0x90u:0x60u;
        if(receiver!=method+delta)return {};
        const auto offset=field==WristHudField::health?0x70u:field==WristHudField::mana?0x88u:field==WristHudField::power?0xb0u:0x80u;
        const auto file=rig::value<std::uintptr_t>(read,method-(field==WristHudField::power?0x38u:8u));
        const auto iggy=rig::value<std::uintptr_t>(read,method);
        if(!file || !*file || !iggy || !*iggy || rig::value<std::uintptr_t>(read,*file+0x1608)!=iggy)return {};
        return NativePromptCall{*file,*iggy,method-offset};
    }
    // Probe the three public slots of a known MasterHUD owner. UI thread only (Iggy getters).
    static std::array<NativeWristArtPublication,3> observe_owner(Read read,std::uintptr_t base,const Api& api,const NativePromptCall& owner) {
        std::array<NativeWristArtPublication,3> out{};
        if(!read || !api.construct || !api.destroy || !api.get || !owner.file || !owner.iggy || !owner.element)return out;
        auto reader=read;
        // All three are public slots on the SAME native MasterHUD. Resolving
        // them together avoids waiting for a separate stat change to discover
        // each node, without retaining an AS3 wrapper or assuming its lifetime.
        for(unsigned i=0;i<out.size();++i) {
            auto& publication=out[i];publication.kind=kinds[i];publication.owner=owner.element;publication.path=paths[i];
            const auto local=NativeInteractionPrompt::probe(read,base,owner,api,paths[i]);
            auto& geometry=publication.geometry;geometry.file=owner.file;geometry.iggy=owner.iggy;
            geometry.x=local.x;geometry.y=local.y;geometry.width=local.width;geometry.height=local.height;
            geometry.visible=local.visible;geometry.fields=local.fields;geometry.status=local.status;
            if(!local.valid || !local.visible || local.width<=0 || local.height<=0)continue;
            geometry.display_node=NativeHudArtProbe::display_node(read,owner,api,paths[i]);
            // Revalidate after getters: an unloaded/replaced movie cannot be
            // published merely because its owner was valid before resolution.
            geometry.valid=geometry.display_node && wrist_hud_file_owned(reader,base,owner.file) &&
                rig::value<std::uintptr_t>(reader,owner.file+0x1608)==owner.iggy;
            publication.valid=geometry.valid;
        }
        return out;
    }
};
} // namespace d2vr
