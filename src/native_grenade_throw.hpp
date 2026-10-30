#pragma once
// Track T item A (docs/native-throw-hand-velocity.md): a grenade thrown with the real hand. MinHook on the entity velocity setter
// exe+0x87C2A0 (leaf: `mov eax,[rdx]; mov [rcx+0x33c],eax; ...`), taken ONLY when its caller is the call site exe+0x63333C inside
// arkCpntWeaponThrowable::Throw (return address exe+0x633341) and the thrown entity carries an arkCpntGrenade component
// (GetComponent exe+0x877720, flags 0, typeinfo exe+0x39E28E0: Throw itself asks for it at exe+0x633455). The velocity vector is Throw's
// local at [rsp+0x50] (= rdx) and the launch point is the local 0x20 bytes after it ([rsp+0x70], passed to the launch exe+0x639970).
// Both are rewritten in place from ThrowGesture. Any mismatch (bytes, hook, no gesture) leaves the native throw alone.
#include <windows.h>
#if __has_include(<MinHook.h>)
#include <MinHook.h>
#else
typedef enum MH_STATUS {MH_UNKNOWN=-1,MH_OK=0,MH_ERROR_ALREADY_INITIALIZED,MH_ERROR_NOT_INITIALIZED,MH_ERROR_ALREADY_CREATED,MH_ERROR_NOT_CREATED,
    MH_ERROR_ENABLED,MH_ERROR_DISABLED,MH_ERROR_NOT_EXECUTABLE,MH_ERROR_UNSUPPORTED_FUNCTION,MH_ERROR_MEMORY_ALLOC,MH_ERROR_MEMORY_PROTECT,
    MH_ERROR_MODULE_NOT_FOUND,MH_ERROR_FUNCTION_NOT_FOUND} MH_STATUS;
extern "C" MH_STATUS WINAPI MH_CreateHook(LPVOID,LPVOID,LPVOID*);
extern "C" MH_STATUS WINAPI MH_EnableHook(LPVOID);
extern "C" MH_STATUS WINAPI MH_DisableHook(LPVOID);
#endif
#include "throw_gesture.hpp"
#include <intrin.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>

namespace d2vr {
// v691 (user, headset v690: the grenade leaves ~0.3 s after the hand opens; it must leave at once, like any thrown object).
// The launch is the ARK_MSG_WEAPON_FIRE (200) event at 293 ms in the throw scene gadget_grenade_throw_high.midnightscene
// (302 ms low/slide), not a timer or data delay. Both throw states (arkPlayerStateFireWeapon 0x866DC0, quick-use
// arkPlayerStateThrowWeapon 0x84AA20) take the scene AND its play rate from arkCpntWeaponThrowable slot 0x120 = 0x633810
// (vtable 0x1B9D9D8 only; grenades only: mines/razors are cpntWeaponPlaceable) and pass that rate to the scene-rate setter
// 0x7D8FE0, which stores it unclamped (0x7D1740 -> scene+0x16C). Hooking 0x633810 to scale its rate plays the throw scene
// (event, sound, ammo, the switch to reload) K times faster: x8 puts the launch at ~37 ms. The reload scene (slot 0x128) is
// untouched. Signature verified at 0x866E8C..0x866E9A: rcx = the component, rdx = float* rate (preset 1.0), rax = scene.
// Knob build/grenade-throw-rate (1..16, 1 = native).
class NativeGrenadeThrow {
public:
    static constexpr std::uintptr_t setter_rva=0x87c2a0,call_return_rva=0x633341,call_site_rva=0x63333c,get_component_rva=0x877720,grenade_type_rva=0x39e28e0;
    static constexpr std::uintptr_t fire_scene_rva=0x633810;
    inline static std::atomic<float> scene_rate{8.f};inline static std::atomic<std::uint64_t> scenes_sped{};
    inline static std::atomic<std::uint64_t> calls{},rewritten{},not_grenade{},no_gesture{},faults{};
    inline static std::atomic<float> last_speed{};inline static std::atomic<std::uint64_t> last_delay_ms{};
    // Present thread: one hand sample per frame (relative velocity) and the trigger.
    static void hand(std::uint64_t now,const std::array<double,3>& pos,const std::array<double,3>& relative_velocity) noexcept {
        std::lock_guard<std::mutex> l(mutex_);gesture_.note_hand(now,pos,relative_velocity);
    }
    static void trigger(std::uint64_t now,float value) noexcept {std::lock_guard<std::mutex> l(mutex_);gesture_.note_trigger(now,value);}
    static void player_velocity(const std::array<double,3>& v) noexcept {std::lock_guard<std::mutex> l(mutex_);player_v_=v;}
    static bool installed() noexcept {return target_!=nullptr;}
    static bool install(std::uintptr_t base) noexcept {
        if(!base) return false;
        if(!target_) {
            static const unsigned char want[8]{0x8b,0x02,0x89,0x81,0x3c,0x03,0x00,0x00};   // mov eax,[rdx]; mov [rcx+0x33c],eax
            unsigned char got[8]{};
            __try {std::memcpy(got,reinterpret_cast<const void*>(base+setter_rva),8);} __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
            if(std::memcmp(got,want,8)) return false;
            void* t=reinterpret_cast<void*>(base+setter_rva);
            if(MH_CreateHook(t,reinterpret_cast<void*>(&hook),reinterpret_cast<void**>(&original_))!=MH_OK) return false;
            target_=t;base_=base;
        }
        const auto s=MH_EnableHook(target_);
        return s==MH_OK || s==MH_ERROR_ENABLED;
    }
    static void disable() noexcept {if(target_) MH_DisableHook(target_);if(scene_target_) MH_DisableHook(scene_target_);}
    // The throw scene's play rate (see the header). False: the bytes differ or the hook failed; the native rate stays.
    static bool install_scene_rate(std::uintptr_t base,float rate) noexcept {
        if(!base || !std::isfinite(rate) || rate<1.f || rate>16.f) return false;
        scene_rate=rate;
        if(!scene_target_) {
            static const unsigned char want[12]{0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18,0x56,0x57};
            unsigned char got[12]{};
            __try {std::memcpy(got,reinterpret_cast<const void*>(base+fire_scene_rva),12);} __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
            if(std::memcmp(got,want,12)) return false;
            void* t=reinterpret_cast<void*>(base+fire_scene_rva);
            if(MH_CreateHook(t,reinterpret_cast<void*>(&scene_hook),reinterpret_cast<void**>(&scene_original_))!=MH_OK) return false;
            scene_target_=t;
        }
        const auto s=MH_EnableHook(scene_target_);
        return s==MH_OK || s==MH_ERROR_ENABLED;
    }
    static void reset() noexcept {std::lock_guard<std::mutex> l(mutex_);gesture_.reset();}
private:
    inline static void* target_{};inline static std::uintptr_t base_{};
    inline static void* scene_target_{};inline static void* (__fastcall *scene_original_)(void*,float*){};
    static void* __fastcall scene_hook(void* component,float* rate) {
        void* scene=scene_original_(component,rate);
        if(scene && rate) __try {if(std::isfinite(*rate) && *rate>0.f) {*rate*=scene_rate.load();++scenes_sped;}} __except(EXCEPTION_EXECUTE_HANDLER) {}
        return scene;
    }
    inline static void (__fastcall *original_)(void*,const float*){};
    inline static std::mutex mutex_;inline static ThrowGesture gesture_;inline static std::array<double,3> player_v_{};
    static bool is_grenade(void* entity) noexcept {
        __try {
            using Get=void* (__fastcall*)(void*,const void*,unsigned);
            return reinterpret_cast<Get>(base_+get_component_rva)(entity,reinterpret_cast<const void*>(base_+grenade_type_rva),0)!=nullptr;
        } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    static bool write(float* velocity,const ThrowGestureResult& r) noexcept {
        __try {
            for(unsigned i=0;i<3;++i) {velocity[i]=static_cast<float>(r.velocity[i]);velocity[8+i]=static_cast<float>(r.spawn[i]);}   // +0x20 bytes = 8 floats
            return true;
        } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    static void __fastcall hook(void* entity,float* velocity) {
        // _ReturnAddress is read first: only Throw's own call (return exe+0x633341) is ever touched.
        const auto from=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
        if(from==base_+call_return_rva && entity && velocity) {
            ++calls;
            if(!is_grenade(entity)) ++not_grenade;
            else {
                ThrowGestureResult r{};
                {std::lock_guard<std::mutex> l(mutex_);r=gesture_.take(GetTickCount64(),player_v_);}
                if(!r.ok) ++no_gesture;
                else if(write(velocity,r)) {last_delay_ms=r.delay_ms;last_speed=static_cast<float>(std::sqrt(r.velocity[0]*r.velocity[0]+r.velocity[1]*r.velocity[1]+r.velocity[2]*r.velocity[2]));++rewritten;}
                else ++faults;
            }
        }
        original_(entity,velocity);
    }
};
} // namespace d2vr
