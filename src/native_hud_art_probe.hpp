#pragma once
#include "native_interaction_prompt.hpp"

namespace d2vr {
// Geometry evidence only. These bounds are not claiming rectangles: Flash
// width/height do not expose a display object's registration point or filters.
struct NativeHudArtGeometry {
    std::uintptr_t file{},iggy{},display_node{};
    double x{},y{},width{},height{},a{},b{},c{},d{},tx{},ty{};
    bool visible{},valid{};
    std::uint32_t fields{},status{};
};
class NativeHudArtProbe {
    using Api=NativeInteractionPrompt::Api;
    using Read=NativeInteractionPrompt::Read;
    static std::optional<double> number(Read read,std::uintptr_t base,const NativePromptCall& owner,
        const Api& api,const char* path,const char* field,std::uint32_t& status) {
        alignas(8) NativeInteractionPrompt::Object object{};
        api.construct(object.data(),reinterpret_cast<void*>(owner.file),path);
        struct Lifetime {void* object;NativeInteractionPrompt::Destroy destroy;~Lifetime(){destroy(object);}} life{object.data(),api.destroy};
        const auto address=reinterpret_cast<std::uintptr_t>(object.data());
        const auto table=rig::value<std::uintptr_t>(read,address+0x18);
        const auto index=rig::value<std::uint32_t>(read,address+0x20);
        if(!table || !index || !rig::pointer(*table) || !(*index&0xffffff) || (*index&0xffffff)>16 ||
            rig::value<std::uintptr_t>(read,address+8)!=owner.iggy) return {};
        const auto value=api.get(reinterpret_cast<const void*>(*table+((*index&0xffffff)-1)*0x28),0,field,nullptr,"D2VR native art geometry",&status);
        if(status || value==7) return {};
        (void)base;
        return native_prompt_number(read,value);
    }
public:
    // 100D280 proves tag24 and the native DisplayObject node at value+27.
    // The native renderer's child and ancestor walk (1037100) proves parent80
    // and the movie's root node at iggy+26B0. This is identity, not a crop guess.
    static bool in_tree(Read read,std::uintptr_t iggy,std::uintptr_t node) {
        for(unsigned depth=0;depth<64 && rig::pointer(node);++depth) {
            if(node==iggy+0x26b0)return true;
            const auto parent=rig::value<std::uintptr_t>(read,node+0x80);
            if(!parent || !*parent || *parent==node)return false;node=*parent;
        }
        return false;
    }
    static std::uintptr_t display_node(Read read,const NativePromptCall& owner,const Api& api,const char* path) {
        alignas(8) NativeInteractionPrompt::Object object{};
        api.construct(object.data(),reinterpret_cast<void*>(owner.file),path);
        struct Lifetime {void* object;NativeInteractionPrompt::Destroy destroy;~Lifetime(){destroy(object);}} life{object.data(),api.destroy};
        const auto address=reinterpret_cast<std::uintptr_t>(object.data());
        const auto table=rig::value<std::uintptr_t>(read,address+0x18),identity=rig::value<std::uintptr_t>(read,address+8);
        const auto index=rig::value<std::uint32_t>(read,address+0x20);
        if(!table || !index || identity!=owner.iggy || !rig::pointer(*table) || !(*index&0xffffff) || (*index&0xffffff)>16)return 0;
        std::uint32_t status{};const auto value=api.get(reinterpret_cast<const void*>(*table+((*index&0xffffff)-1)*0x28),0,nullptr,nullptr,"D2VR native display identity",&status);
        if(status || value==7 || (value&3)!=1 || !rig::pointer(value-1) || rig::value<unsigned char>(read,value-1)!=0x24)return 0;
        const auto node=rig::value<std::uintptr_t>(read,value+0x27);
        return node && in_tree(read,owner.iggy,*node)?*node:0;
    }
    // Display node of an engine-held AS3 object (e.g. the crosshair element's object at +0x3A8, which the
    // engine's own member setter takes as `this`): same layout as a constructed path object above.
    static std::uintptr_t object_node(Read read,std::uintptr_t object,std::uintptr_t iggy,const Api& api) {
        if(!rig::pointer(object) || !api.get)return 0;
        const auto table=rig::value<std::uintptr_t>(read,object+0x18),identity=rig::value<std::uintptr_t>(read,object+8);
        const auto index=rig::value<std::uint32_t>(read,object+0x20);
        if(!table || !index || !identity || (iggy && *identity!=iggy) || !rig::pointer(*table) || !(*index&0xffffff) || (*index&0xffffff)>16)return 0;
        // The getter uses the record's first field as its Iggy player and writes its call depth there: it must be this object's player.
        const auto record=*table+((*index&0xffffff)-1)*0x28;
        if(rig::value<std::uintptr_t>(read,record)!=*identity) return 0;
        std::uint32_t status{};const auto value=api.get(reinterpret_cast<const void*>(record),0,nullptr,nullptr,"D2VR native display identity",&status);
        if(status || value==7 || (value&3)!=1 || !rig::pointer(value-1) || rig::value<unsigned char>(read,value-1)!=0x24)return 0;
        const auto node=rig::value<std::uintptr_t>(read,value+0x27);
        return node && in_tree(read,*identity,*node)?*node:0;
    }
    // Call only on the verified shared native UI Call observer. The public
    // getters are proven in HUDScene m5907/m5910. Dotted paths are parsed by
    // 29A60 using literal separator2E. No member pointer survives this call.
    static NativeHudArtGeometry probe(Read read,std::uintptr_t base,const NativePromptCall& owner,const Api& api,bool master) {
        NativeHudArtGeometry out{};out.file=owner.file;out.iggy=owner.iggy;
        if(!read || !api.construct || !api.destroy || !api.get || !wrist_hud_file_owned(read,base,owner.file)) return out;
        const char* path=master?"_masterHUD":"_interactionWindow";
        out.display_node=display_node(read,owner,api,path);
        const auto local=NativeInteractionPrompt::probe(read,base,owner,api,path);
        out.x=local.x;out.y=local.y;out.width=local.width;out.height=local.height;out.visible=local.visible;
        out.fields=local.fields;out.status=local.status;if(!local.valid)return out;
        const char* matrix=master?"_masterHUD.transform.concatenatedMatrix":"_interactionWindow.transform.concatenatedMatrix";
        double* values[]{&out.a,&out.b,&out.c,&out.d,&out.tx,&out.ty};const char* names[]{"a","b","c","d","tx","ty"};
        for(unsigned i=0;i<6;++i){const auto v=number(read,base,owner,api,matrix,names[i],out.status);if(!v)return out;*values[i]=*v;out.fields|=1u<<(i+5);}
        out.valid=out.width>0 && out.height>0 && std::abs(out.a*out.d-out.b*out.c)>1e-12;
        return out;
    }
};
} // namespace d2vr
