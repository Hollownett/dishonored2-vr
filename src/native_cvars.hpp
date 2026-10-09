#pragma once
#include <windows.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
namespace d2vr {
// Session-only VR overrides of native cvar caches. Each cvar object keeps its
// int cache at +0x28, float cache at +0x2c and name pointer at +0x30 (pinned
// build, see docs/image-quality-research.md). A cvar is touched only when its
// name matches; restore puts the original back unless the game changed it.
class NativeCvars {
public:
    struct Override {std::uintptr_t rva;const char* name;LONG value;};
    // Screen-space blur/DOF and dynamic resolution look wrong or shimmer per eye.
    // Temporal AA is forced on: NativeTaaHistory gives each eye its own history.
    // Anisotropic filtering keeps floor/wall textures sharp at headset viewing angles.
    static constexpr std::array<Override,8> vr_defaults{{
        {0x39a4220,"r_skipCameraMotionBlur",1},{0x32f3d50,"r_skipDOF",1},
        {0x32f4dd0,"r_DofUseFringing",0},{0x397bec0,"rs_enable",0},{0x32ea3f0,"image_anisotropy",16},
        {0x3982ef0,"r_AntiAliasingPreset",3},{0x397bb50,"r_AAMode",-1},{0x397bbd0,"r_skipTemporalAA",0}}};
    static constexpr std::size_t skip_temporal_aa=7;
private:
    struct Saved {bool active{};LONG value{};float scalar{};};
    static constexpr std::array<LONG,vr_defaults.size()> default_values() {
        std::array<LONG,vr_defaults.size()> values{};for(std::size_t i=0;i<values.size();++i) values[i]=vr_defaults[i].value;return values;
    }
    std::array<Saved,vr_defaults.size()> saved_{};
    // The live override set: set() moves an entry here, apply()/restore() compare against it.
    std::array<LONG,vr_defaults.size()> values_{default_values()};
    std::uintptr_t base_{};
    template<class Read> static bool named(Read& read,std::uintptr_t object,const char* name) {
        std::uintptr_t text{};std::array<char,48> buffer{};
        if(!read(object+0x30,&text,8) || !text || !read(text,buffer.data(),std::strlen(name)+1)) return false;
        return std::strcmp(buffer.data(),name)==0;
    }
public:
    // Entries from index 5 enable temporal AA; pass temporal=false without per-eye history.
    template<class Read> unsigned apply(std::uintptr_t base,Read& read,bool temporal) {
        base_=base;unsigned applied{};
        for(std::size_t i=0;i<vr_defaults.size();++i) {
            const auto& o=vr_defaults[i];const auto object=base+o.rva;
            auto& s=saved_[i];s={};
            if(i>=5 && !temporal) continue;
            if(!named(read,object,o.name) || !read(object+0x28,&s.value,4) || !read(object+0x2c,&s.scalar,4)) continue;
            // A value already equal to the override is still ours: remember it so a
            // live set() and the matching restore() can move it back.
            if(s.value==values_[i]) {s.active=true;continue;}
            const float scalar=static_cast<float>(values_[i]);LONG bits{};std::memcpy(&bits,&scalar,4);
            if(InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(object+0x28),values_[i],s.value)!=s.value) continue;
            LONG previous_bits{};std::memcpy(&previous_bits,&s.scalar,4);
            InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(object+0x2c),bits,previous_bits);
            s.active=true;++applied;
        }
        return applied;
    }
    // A live edit of one applied override; the int and float caches move together
    // and only when the int cache still holds our value.
    bool set(std::size_t index,LONG value) noexcept {
        if(index>=vr_defaults.size() || !saved_[index].active) return false;
        const auto object=base_+vr_defaults[index].rva;
        if(InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(object+0x28),value,values_[index])!=values_[index]) return false;
        const float scalar=static_cast<float>(value);LONG bits{},previous_bits{};
        std::memcpy(&bits,&scalar,4);
        const float previous=static_cast<float>(values_[index]);std::memcpy(&previous_bits,&previous,4);
        InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(object+0x2c),bits,previous_bits);
        values_[index]=value;return true;
    }
    void restore() noexcept {
        for(std::size_t i=0;i<vr_defaults.size();++i) {
            auto& s=saved_[i];if(!s.active) continue;
            const auto object=base_+vr_defaults[i].rva;
            InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(object+0x28),s.value,values_[i]);
            const float scalar=static_cast<float>(values_[i]);LONG bits{},previous_bits{};
            std::memcpy(&bits,&scalar,4);std::memcpy(&previous_bits,&s.scalar,4);
            InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(object+0x2c),previous_bits,bits);
            s.active=false;
        }
    }
};
} // namespace d2vr
