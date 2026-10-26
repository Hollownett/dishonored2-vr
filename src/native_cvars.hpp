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
    // r_playerDepthHack ("player's models depth hack value") compresses player-model depth
    // so flatscreen arms never clip walls; in VR it draws the full-body avatar over the world.
    static constexpr std::array<Override,11> vr_defaults{{
        {0x39a4220,"r_skipCameraMotionBlur",1},{0x32f3d50,"r_skipDOF",1},
        {0x32f4dd0,"r_DofUseFringing",0},{0x397bec0,"rs_enable",0},{0x32ea3f0,"image_anisotropy",16},
        {0x398e300,"r_playerDepthHack",0},
        // SSAO is the one per-eye pass worth cutting: -15% frame time at 2496x2688 (shadow/tessellation cvars: no change).
        {0x397b4d0,"r_skipSSAO",1},
        // Screen-space lens flares are wrong per eye (~0.8 ms native GPU; registration 0x1372252).
        {0x32f7fc0,"r_skipLensFlares",1},
        {0x3982ef0,"r_AntiAliasingPreset",3},{0x397bb50,"r_AAMode",-1},{0x397bbd0,"r_skipTemporalAA",0}}};
    static constexpr std::size_t player_depth_hack=5,skip_temporal_aa=10,first_temporal=8;
private:
    struct Saved {bool active{};LONG value{};float scalar{};};
    static constexpr std::array<LONG,vr_defaults.size()> default_values() {
        std::array<LONG,vr_defaults.size()> values{};for(std::size_t i=0;i<values.size();++i) values[i]=vr_defaults[i].value;return values;
    }
    std::array<Saved,vr_defaults.size()> saved_{};
    // The live override set: set() moves an entry here, apply()/restore() compare against it.
    std::array<LONG,vr_defaults.size()> values_{default_values()};
    std::uintptr_t base_{};
    static LONG64 pair(LONG integer,float scalar) noexcept {
        const std::array<LONG,2> cache{integer,0};LONG64 result{};
        std::memcpy(&result,cache.data(),8);std::memcpy(reinterpret_cast<unsigned char*>(&result)+4,&scalar,4);return result;
    }
    static LONG64 pair(LONG value) noexcept {return pair(value,static_cast<float>(value));}
    static bool exchange(std::uintptr_t object,LONG64 before,LONG64 after) noexcept {
        if((object+0x28)%8) return false;
        return InterlockedCompareExchange64(reinterpret_cast<volatile LONG64*>(object+0x28),after,before)==before;
    }
    template<class Read> static bool named(Read& read,std::uintptr_t object,const char* name) {
        std::uintptr_t text{};std::array<char,48> buffer{};
        if(!read(object+0x30,&text,8) || !text || !read(text,buffer.data(),std::strlen(name)+1)) return false;
        return std::strcmp(buffer.data(),name)==0;
    }
public:
    // Entries from first_temporal enable temporal AA; pass temporal=false without per-eye history.
    template<class Read> unsigned apply(std::uintptr_t base,Read& read,bool temporal,bool native_player_depth=false) {
        base_=base;unsigned applied{};
        for(std::size_t i=0;i<vr_defaults.size();++i) {
            const auto& o=vr_defaults[i];const auto object=base+o.rva;
            auto& s=saved_[i];s={};
            if(native_player_depth && i==player_depth_hack)continue; // NativePlayerDepth owns strings/listeners as well as caches.
            if(i>=first_temporal && !temporal) continue;
            if(!named(read,object,o.name) || !read(object+0x28,&s.value,4) || !read(object+0x2c,&s.scalar,4)) continue;
            // Both adjacent caches form one aligned native value. Even an equal
            // integer needs its scalar checked before claiming the override.
            const auto original=pair(s.value,s.scalar),desired=pair(values_[i]);
            if(!exchange(object,original,desired)) continue;
            s.active=true;if(original!=desired) ++applied;
        }
        return applied;
    }
    // A live edit accepts only our last pair or the saved original pair (native
    // reset). Atomic pair CAS rejects mixed/foreign values without partial writes.
    bool set(std::size_t index,LONG value) noexcept {
        if(index>=vr_defaults.size() || !saved_[index].active) return false;
        const auto object=base_+vr_defaults[index].rva;
        const auto previous=pair(values_[index]),original=pair(saved_[index].value,saved_[index].scalar),desired=pair(value);
        auto accepted=previous;
        if(!exchange(object,previous,desired)) {
            if(original!=previous && exchange(object,original,desired)) accepted=original;
            else {
                // The game rewrote the cache itself (live: rs_enable=3 after settings/scene changes).
                // Refusing forever left FSR Quality/Balanced rendering at 100%; take the live pair over.
                LONG64 current{};SIZE_T bytes{};
                if(!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(object+0x28),&current,8,&bytes) || bytes!=8 ||
                   !exchange(object,current,desired)) return false;
                accepted=current;
            }
        }
        if(!matches(index,value)) {
            // A native write may follow our successful CAS. Never replace it.
            // Roll back only our pair; otherwise retain its ownership record.
            if(!exchange(object,desired,accepted)) values_[index]=value;
            return false;
        }
        values_[index]=value;return true;
    }
    bool matches(std::size_t index,LONG value) const noexcept {
        if(index>=vr_defaults.size() || !saved_[index].active || !base_) return false;
        std::array<LONG,2> cache{},again{};SIZE_T bytes{};
        const auto at=reinterpret_cast<const void*>(base_+vr_defaults[index].rva+0x28);
        const float scalar=static_cast<float>(value);LONG bits{};std::memcpy(&bits,&scalar,4);
        return ReadProcessMemory(GetCurrentProcess(),at,cache.data(),8,&bytes) && bytes==8 &&
            ReadProcessMemory(GetCurrentProcess(),at,again.data(),8,&bytes) && bytes==8 &&
            cache==again && cache[0]==value && cache[1]==bits;
    }
    void restore() noexcept {
        for(std::size_t i=0;i<vr_defaults.size();++i) {
            auto& s=saved_[i];if(!s.active) continue;
            const auto object=base_+vr_defaults[i].rva;
            exchange(object,pair(values_[i]),pair(s.value,s.scalar));
            s.active=false;
        }
    }
};
} // namespace d2vr
