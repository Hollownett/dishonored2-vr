#pragma once
#include "native_wrist_hud.hpp"
#include <cmath>
#include <cstring>

namespace d2vr {
// IggyValueGetS32RS (1009BF0) proves both numeric representations. Read the
// boxed double immediately on the native UI thread; never retain its pointer.
template<class Read> std::optional<double> native_prompt_number(Read& read,std::uint64_t value) {
    if(value==7) return {};
    if(!(value&1)) return static_cast<double>(static_cast<std::int64_t>(value)>>1);
    if((value&3)!=1 || value<0x10001) return {};
    const auto type=rig::value<unsigned char>(read,value-1);
    const auto number=rig::value<double>(read,value+7);
    if(!type || *type!=7 || !number || !std::isfinite(*number)) return {};
    return number;
}
struct NativePromptCall {
    std::uintptr_t file{},iggy{},element{};
};
template<class Read> std::optional<NativePromptCall> native_prompt_call_owner(Read& read,std::uintptr_t base,
    std::uintptr_t caller,std::uintptr_t method,std::uintptr_t receiver,std::uintptr_t args) {
    // 38F7D0 Add6, 38F690 UpdateGauge2, 3903B0 Remove1; all use the exact
    // _interactionsManager receiver bound by 3901E0 at element+188.
    const auto offset=caller==0x38f963?0x98u:caller==0x38f743?0x128u:caller==0x3906a9?0xf8u:0u;
    const auto count=caller==0x38f963?6u:caller==0x38f743?2u:1u;
    if(!offset || method<offset) return {};
    const auto element=method-offset;
    // Pinned PE RTTI COL1F8DA90 identifies arkHUDElementInteractives vtable.
    if(!rig::typed(read,base,element,0x1b3a3c8) || receiver!=element+0x188 ||
       rig::value<std::uint32_t>(read,args+0x14)!=count) return {};
    const auto file=rig::value<std::uintptr_t>(read,element+0x90);
    const auto iggy=rig::value<std::uintptr_t>(read,method),handle=rig::value<std::uintptr_t>(read,method+8);
    if(!file || !iggy || !handle || !*handle || !wrist_hud_file_owned(read,base,*file) ||
       rig::value<std::uintptr_t>(read,*file+0x1608)!=iggy) return {};
    return NativePromptCall{*file,*iggy,element};
}
struct NativePromptObservation {
    std::uintptr_t file{},iggy{},element{};
    double x{},y{},width{},height{};
    bool visible{},valid{};
    std::uint32_t fields{},status{};
};
// Read-only runtime probe, called AFTER the existing shared 28A90 original.
// No hook is installed here and no x/y/visibility is written. The caller is a
// verified native UI update; do not call probe from Present or a helper thread.
class NativeInteractionPrompt {
public:
    using Read=NativeWristHud::Read;
    using Object=std::array<std::byte,0x2a8>;
    using Construct=void* (__fastcall*)(void*,void*,const char*);
    using Destroy=void (__fastcall*)(void*);
    using Get=std::uint64_t (__fastcall*)(const void*,std::uint64_t,const char*,void*,const char*,std::uint32_t*);
    struct Api {Construct construct{};Destroy destroy{};Get get{};};
private:
    std::uintptr_t base_{};
    Read read_{};
    Api api_{};
    std::uint64_t last_tick_{};
    template<std::size_t N> bool match(std::uintptr_t rva,const std::array<unsigned char,N>& expected) const {
        std::array<unsigned char,N> actual{};
        return read_(base_+rva,actual.data(),actual.size()) && actual==expected;
    }
public:
    const Api& native_api() const noexcept {return api_;}
    bool configure(std::uintptr_t base,Read read) {
        api_={};last_tick_=0;
        if(!base || !read) return false;base_=base;read_=read;
        // Prefixes extracted from the pinned Steam17576867 executable.
        if(!match(0x28e50,std::array<unsigned char,18>{0x48,0x89,0x4c,0x24,0x08,0x57,0x48,0x83,0xec,0x30,0x48,0xc7,0x44,0x24,0x20,0xfe,0xff,0xff}) ||
           !match(0x28fd0,std::array<unsigned char,10>{0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0x51,0x10}) ||
           !match(0x100ba70,std::array<unsigned char,18>{0x4c,0x89,0x4c,0x24,0x20,0x48,0x89,0x54,0x24,0x10,0x48,0x89,0x4c,0x24,0x08,0x53,0x56,0x57})) return false;
        api_={reinterpret_cast<Construct>(base+0x28e50),reinterpret_cast<Destroy>(base+0x28fd0),reinterpret_cast<Get>(base+0x100ba70)};
        last_tick_=0;return true;
    }
    // Injectable functions let the fixture verify binding/getter/destruction and
    // unchanged native content without invoking any game code.
    static NativePromptObservation probe(Read read,std::uintptr_t base,const NativePromptCall& owner,const Api& api,const char* path="_interactionWindow") {
        NativePromptObservation out{};out.file=owner.file;out.iggy=owner.iggy;out.element=owner.element;
        if(!read || !api.construct || !api.destroy || !api.get) return out;
        alignas(8) Object object{};
        api.construct(object.data(),reinterpret_cast<void*>(owner.file),path);
        struct Lifetime {void* object;Destroy destroy;~Lifetime(){destroy(object);}} lifetime{object.data(),api.destroy};
        const auto address=reinterpret_cast<std::uintptr_t>(object.data());
        const auto table=rig::value<std::uintptr_t>(read,address+0x18);
        const auto index=rig::value<std::uint32_t>(read,address+0x20);
        if(!table || !index || !rig::pointer(*table) || !(*index&0xffffff) || (*index&0xffffff)>16 ||
           rig::value<std::uintptr_t>(read,address+8)!=owner.iggy) return out;
        const auto record=*table+((*index&0xffffff)-1)*0x28;
        double* outputs[]{&out.x,&out.y,&out.width,&out.height};
        const char* names[]{"x","y","width","height","visible"};
        for(unsigned i=0;i<5;++i) {
            std::uint32_t status{};
            const auto value=api.get(reinterpret_cast<const void*>(record),0,names[i],nullptr,"D2VR native prompt probe",&status);
            out.status=status;if(status || value==7) return out;
            if(i<4) {
                const auto number=native_prompt_number(read,value);if(!number) return out;
                *outputs[i]=*number;
            } else {
                // Exact singleton tags compared by IggyValueGetBooleanRS.
                if(value==base+0x2424049) out.visible=true;
                else if(value==base+0x2424041) out.visible=false;
                else return out;
            }
            out.fields|=1u<<i;
        }
        out.valid=out.width>=0 && out.height>=0 && out.width<=10000 && out.height<=10000;
        return out;
    }
    std::optional<NativePromptObservation> observe(std::uintptr_t caller,std::uintptr_t method,std::uintptr_t receiver,
        std::uintptr_t args,std::uint64_t tick) {
        if(!api_.get || (last_tick_ && tick-last_tick_<500)) return {};
        auto read=read_;const auto owner=native_prompt_call_owner(read,base_,caller,method,receiver,args);
        if(!owner) return {};last_tick_=tick;
        return probe(read_,base_,*owner,api_);
    }
};
} // namespace d2vr
