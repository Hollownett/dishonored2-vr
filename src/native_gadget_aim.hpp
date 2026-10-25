#pragma once
// Track T item B (docs/native-gadget-hand-aim.md): mines / spring razors are placed where the LEFT hand aims. MinHook on the two
// player-sensor job bodies (each carries its own profile string, so the RVAs are identified by the exe itself):
//   exe+0x3C1E40 arkCpntPlayerSensorPlacement::UpdateQueryPhysics (string at exe+0x1B41768): one ray, start = *( [s+0x40]+0xd8 ) float3
//        (0x417820 returns rcx+0xd8), direction = float3 at [s+0x48]+0x2c, length [s+0x58] (call to the ray 0x95130: r8 start, r9 direction).
//   exe+0x3CEC30 arkCpntPlayerSensorThrowHit::UpdateQueryPhysics (string at exe+0x1B42C48): origin = entity+0x300, horizontal direction
//        from entity+0x30c (x,y normalised, z forced 0), entity = [s+0x10]; its ray start is the same *([s+0x40]+0xd8).
// While the hand pose is fresh the hook writes the hand's origin / forward into those fields, runs the native sensor and puts the
// fields back (game thread, same call). No fresh pose, a bad pointer or a prologue mismatch leaves the native sensor untouched.
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
#include "gadget_aim.hpp"
#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>

namespace d2vr {
class NativeGadgetAim {
public:
    static constexpr std::uintptr_t placement_rva=0x3c1e40,throw_hit_rva=0x3cec30;
    inline static std::atomic<std::uint64_t> placement_calls{},placement_swapped{},throw_calls{},throw_swapped{},stale{},bad_pointer{},restore_skipped{};
    static void publish(const GadgetAimPose& pose) noexcept {std::lock_guard<std::mutex> l(mutex_);pose_=pose;}
    static bool installed() noexcept {return targets_[0]!=nullptr;}
    static bool install(std::uintptr_t base) noexcept {
        if(!base) return false;
        if(!targets_[0]) {
            static const unsigned char want[2][16]{
                {0x40,0x57,0x48,0x83,0xec,0x40,0x80,0x79,0x38,0x00,0x48,0x8b,0xf9,0x74,0x65,0x48},
                {0x48,0x8b,0xc4,0x55,0x48,0x81,0xec,0x50,0x01,0x00,0x00,0x80,0x79,0x38,0x00,0x48}};
            const std::uintptr_t rvas[2]{placement_rva,throw_hit_rva};
            for(unsigned i=0;i<2;++i) {
                unsigned char got[16]{};
                __try {std::memcpy(got,reinterpret_cast<const void*>(base+rvas[i]),16);} __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
                if(std::memcmp(got,want[i],16)) return false;
            }
            void* t[2]{reinterpret_cast<void*>(base+placement_rva),reinterpret_cast<void*>(base+throw_hit_rva)};
            if(MH_CreateHook(t[0],reinterpret_cast<void*>(&placement_hook),reinterpret_cast<void**>(&original_[0]))!=MH_OK) return false;
            if(MH_CreateHook(t[1],reinterpret_cast<void*>(&throw_hook),reinterpret_cast<void**>(&original_[1]))!=MH_OK) return false;
            targets_[0]=t[0];targets_[1]=t[1];
        }
        bool ok=true;
        for(void* t:targets_) {const auto s=MH_EnableHook(t);ok=ok && (s==MH_OK || s==MH_ERROR_ENABLED);}
        return ok;
    }
    static void disable() noexcept {for(void* t:targets_) if(t) MH_DisableHook(t);}
private:
    using Fn=std::uintptr_t (__fastcall *)(void*);
    inline static void* targets_[2]{};inline static Fn original_[2]{};
    inline static std::mutex mutex_;inline static GadgetAimPose pose_{};
    template<class T> static bool rd(std::uintptr_t at,T& out) noexcept {
        __try {std::memcpy(&out,reinterpret_cast<const void*>(at),sizeof(T));return true;} __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    static float* f3(std::uintptr_t at) noexcept {float probe[3];return at>0x10000 && rd(at,probe)?reinterpret_cast<float*>(at):nullptr;}
    static bool pointer_field(std::uintptr_t object,std::uintptr_t offset,std::uintptr_t& out) noexcept {return rd(object+offset,out) && out>0x10000;}
    // Resolves the fields the sensor reads; null = not resolvable (the sensor then runs untouched).
    static void resolve(std::uintptr_t sensor,bool placement,float*& start,float*& dir,float*& origin) noexcept {
        std::uintptr_t s40{},p{};
        start=dir=origin=nullptr;
        if(pointer_field(sensor,0x40,s40)) start=f3(s40+0xd8);
        if(placement) {if(pointer_field(sensor,0x48,p)) dir=f3(p+0x2c);}
        else if(pointer_field(sensor,0x10,p)) {origin=f3(p+0x300);dir=f3(p+0x30c);}
    }
    static std::uintptr_t run(unsigned which,void* sensor) {
        const bool placement=which==0;
        ++(placement?placement_calls:throw_calls);
        unsigned char enabled{};   // the sensor's own enable byte (+0x38, tested first by both bodies): idle sensors run every frame and are left alone
        if(!rd(reinterpret_cast<std::uintptr_t>(sensor)+0x38,enabled) || !enabled) return original_[which](sensor);
        GadgetAimPose pose{};{std::lock_guard<std::mutex> l(mutex_);pose=pose_;}
        if(!gadget_aim_fresh(pose,GetTickCount64()) || !gadget_aim_wanted(pose,placement)) {++stale;return original_[which](sensor);}
        float *start{},*dir{},*origin{};
        resolve(reinterpret_cast<std::uintptr_t>(sensor),placement,start,dir,origin);
        if(!start || !dir || (!placement && !origin)) {++bad_pointer;return original_[which](sensor);}
        FieldSwap swap;
        swap.put(start,pose.origin);
        swap.put(dir,pose.forward,!placement);
        if(origin) swap.put(origin,pose.origin);
        ++(placement?placement_swapped:throw_swapped);
        const auto result=original_[which](sensor);
        swap.restore();   // a field the engine rewrote during the call keeps its value
        if(const auto skipped=swap.skipped()) restore_skipped+=skipped;
        return result;
    }
    static std::uintptr_t __fastcall placement_hook(void* sensor) {return run(0,sensor);}
    static std::uintptr_t __fastcall throw_hook(void* sensor) {return run(1,sensor);}
};
} // namespace d2vr
