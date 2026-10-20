#pragma once
// tracked_pose.hpp reaches arm_ik.hpp, whose std::max/std::min need the macros
// gone before windows.h is pulled in by the standalone check.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#if __has_include(<MinHook.h>)
#include <MinHook.h>
#else
// The DLL build adds /Ithird_party\minhook\include. The standalone check command
// has no /I, so name the three entry points this header uses; the check stubs
// them exactly as tests\native_shot_aim_check.cpp does.
typedef enum MH_STATUS {
    MH_UNKNOWN=-1,MH_OK=0,MH_ERROR_ALREADY_INITIALIZED,MH_ERROR_NOT_INITIALIZED,MH_ERROR_ALREADY_CREATED,
    MH_ERROR_NOT_CREATED,MH_ERROR_ENABLED,MH_ERROR_DISABLED,MH_ERROR_NOT_EXECUTABLE,MH_ERROR_UNSUPPORTED_FUNCTION,
    MH_ERROR_MEMORY_ALLOC,MH_ERROR_MEMORY_PROTECT,MH_ERROR_MODULE_NOT_FOUND,MH_ERROR_FUNCTION_NOT_FOUND
} MH_STATUS;
extern "C" MH_STATUS WINAPI MH_CreateHook(LPVOID,LPVOID,LPVOID*);
extern "C" MH_STATUS WINAPI MH_EnableHook(LPVOID);
extern "C" MH_STATUS WINAPI MH_DisableHook(LPVOID);
#endif
#include "gameplay_aim.hpp"
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>

namespace d2vr {
// The Blink power (arkCpntPowerBlink) aims with its per-frame function 0x1406205e0
// (job thunk 0x954d00, call at 0x954d2e, component stage 1). It reads the ray
// from the player camera component: direction at cam+0x25288, origin x/y at
// cam+0x2527c/+0x25280. docs/native-blink-aim.md has the static RE. The
// camera aim producers write those bytes at stage 7, one frame earlier, so a
// direction written just before this call reaches the cast, the Blink sensors
// and the ledge test together. Nothing is restored afterwards: the sensors read
// the same bytes in the same stage and the next stage-7 build regenerates all 48.
// The origin stays native (the solver ignores camera Z, the sensors use it).
inline constexpr std::uintptr_t blink_aim_rva=0x6205e0,blink_aim_thunk_rva=0x954d00,blink_aim_call_rva=0x954d2e;
inline constexpr std::array<std::byte,32> blink_aim_prologue{{
    std::byte{0x48},std::byte{0x8b},std::byte{0xc4},std::byte{0x55},std::byte{0x57},std::byte{0x48},std::byte{0x8d},std::byte{0x68},
    std::byte{0xa1},std::byte{0x48},std::byte{0x81},std::byte{0xec},std::byte{0xf8},std::byte{0x00},std::byte{0x00},std::byte{0x00},
    std::byte{0x80},std::byte{0xb9},std::byte{0xa4},std::byte{0x00},std::byte{0x00},std::byte{0x00},std::byte{0x00},std::byte{0x48},
    std::byte{0x8b},std::byte{0xf9},std::byte{0x0f},std::byte{0x84},std::byte{0x16},std::byte{0x0b},std::byte{0x00},std::byte{0x00}
}};
inline constexpr std::array<std::byte,32> blink_aim_thunk_prologue{{
    std::byte{0x48},std::byte{0x89},std::byte{0x5c},std::byte{0x24},std::byte{0x08},std::byte{0x48},std::byte{0x89},std::byte{0x74},
    std::byte{0x24},std::byte{0x10},std::byte{0x57},std::byte{0x48},std::byte{0x83},std::byte{0xec},std::byte{0x20},std::byte{0x48},
    std::byte{0x8b},std::byte{0x1a},std::byte{0x48},std::byte{0x8b},std::byte{0xfa},std::byte{0x48},std::byte{0x8b},std::byte{0xf1},
    std::byte{0x48},std::byte{0x3b},std::byte{0x5a},std::byte{0x18},std::byte{0x73},std::byte{0x1f},std::byte{0x66},std::byte{0x90}
}};
// Pitch limit of the substituted forward: keeps the legacy reach ellipse away from its +-90 degree branch.
inline constexpr double blink_aim_max_pitch_degrees=85.;

// The direction basis (rows forward / left / up, as the left-aim path publishes
// it). A hand within the pitch limit passes through unchanged (roll kept); a
// steeper one is clamped on forward and the frame rebuilt roll-free.
inline std::optional<std::array<float,9>> blink_aim_axis_from_hand(const WorldTrackedPose& hand) noexcept {
    std::array<float,9> axis{};
    for(unsigned i=0;i<3;++i) {
        axis[3*i]=static_cast<float>(hand.basis[i].x);axis[3*i+1]=static_cast<float>(hand.basis[i].y);
        axis[3*i+2]=static_cast<float>(hand.basis[i].z);
    }
    if(!valid_aim_frame(axis)) return {};
    const double limit=std::sin(blink_aim_max_pitch_degrees*3.14159265358979323846/180.);
    const double fz=axis[2];
    if(std::abs(fz)<=limit) return axis;
    const double xy=std::hypot(static_cast<double>(axis[0]),static_cast<double>(axis[1]));
    if(xy<1e-6) return {};
    const double scale=std::sqrt(1-limit*limit)/xy;
    const Vec3 forward{axis[0]*scale,axis[1]*scale,fz<0?-limit:limit};
    Vec3 left=cross(Vec3{0,0,1},forward);const double ll=length(left);
    if(ll<1e-6) return {};
    left=left*(1/ll);const Vec3 up=cross(forward,left);
    const Vec3 rows[3]{forward,left,up};
    for(unsigned i=0;i<3;++i) {
        axis[3*i]=static_cast<float>(rows[i].x);axis[3*i+1]=static_cast<float>(rows[i].y);axis[3*i+2]=static_cast<float>(rows[i].z);
    }
    if(!valid_aim_frame(axis)) return {};
    return axis;
}

// Developer-visible, default on. The render thread publishes the left-hand aim
// only while the left-aim gates hold (left trigger within its 400 ms window,
// left grip released, not authored view, no sword swing, right trigger low); the
// game thread replaces the camera aim direction once, just before the original
// runs, and never restores it.
class NativeBlinkAim {
public:
    using Read=bool (*)(std::uintptr_t,void*,std::size_t);
    using Logic=UINT (*)();
    using Camera=bool (*)(std::uintptr_t);          // a verified, non-authored player camera
    using OwnerCamera=std::uintptr_t (*)();         // camera the existing left-aim hook owns
    using LastApplied=UINT64 (*)();                 // tick of that hook's last clean write
    // last_reason: 0 routed, 1 inactive/observe-only/failed, 2 nested, 3 aim not active, 4 not gameplay, 5 stale or no hand,
    // 6 hand basis, 7 camera not a verified player camera or authored view, 8 memory or write failure.
    struct Sample {
        std::uint64_t tick{};std::uintptr_t power{},camera{};bool aim_active{},cam_matches_left{},left_applied{},routed{};
        std::array<float,12> aim{};std::array<float,6> result{};int reason{-1};
    };
private:
    using Aim=void (__fastcall*)(void*);
    struct Publication {std::uint64_t tick{},epoch{};WorldTrackedPose hand{};bool active{};};
    inline static NativeBlinkAim* instance_{};
    Aim original_{};
    bool enabled_{};
    Read read_{};Logic logic_{};Camera camera_{};OwnerCamera owner_{};LastApplied applied_{};std::uintptr_t base_{};
    std::atomic<bool> active_{false},route_{false},failed_{false};
    std::atomic<UINT> depth_{};
    std::atomic<UINT64> calls_{},aim_calls_{},routed_{},rejected_{},cam_matches_{},left_applied_frames_{},overlap_{};
    std::atomic<int> last_reason_{-1};
    std::mutex mutex_;Publication hand_;Sample sample_;
    static constexpr std::uintptr_t camera_aim_offset=0x2527c,camera_axis_offset=0x25288,power_camera=0x160,power_active=0xa4,power_cast=0xa5,power_result=0x1e8;
    static bool writable(const void* at,std::size_t size) noexcept {
        if(!at || !size) return false;
        MEMORY_BASIC_INFORMATION info{};
        if(VirtualQuery(at,&info,sizeof(info))!=sizeof(info) || info.State!=MEM_COMMIT || (info.Protect&PAGE_GUARD)) return false;
        const auto first=reinterpret_cast<std::uintptr_t>(at);
        const auto region=reinterpret_cast<std::uintptr_t>(info.BaseAddress);
        if(first<region || first+size-1>=region+info.RegionSize) return false;
        constexpr DWORD pages=PAGE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY;
        return (info.Protect&pages)!=0;
    }
    static bool copy_guarded(void* destination,const void* source,std::size_t size) noexcept {
        __try {std::memcpy(destination,source,size);return true;}
        __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    bool fresh_hand(Publication& out,std::uint64_t now) {
        std::lock_guard<std::mutex> lock(mutex_);out=hand_;
        return out.active && out.epoch && now>=out.tick && now-out.tick<=250;
    }
public:
    // Before the original runs. Writes only the 36 direction bytes. Returns the reason code and the camera it saw.
    int before(void* power,std::uintptr_t& camera_out,bool& aim_active_out) noexcept {
        camera_out=0;aim_active_out=false;
        try {
            std::uint8_t active=0,cast=0;std::uintptr_t camera=0;
            if(!read_(reinterpret_cast<std::uintptr_t>(power)+power_active,&active,1) ||
               !read_(reinterpret_cast<std::uintptr_t>(power)+power_cast,&cast,1)) return 8;
            if(!(active!=0 && cast==0)) return 3;
            aim_active_out=true;
            if(!read_(reinterpret_cast<std::uintptr_t>(power)+power_camera,&camera,sizeof(camera))) return 8;
            camera_out=camera;
            if(!active_.load(std::memory_order_acquire) || !route_.load(std::memory_order_acquire) || failed_.load()) return 1;
            if(depth_.load()!=1) return 2;
            if(!logic_ || logic_()!=2) return 4;
            Publication hand;
            if(!fresh_hand(hand,GetTickCount64())) return 5;
            const auto axis=blink_aim_axis_from_hand(hand.hand);
            if(!axis) return 6;
            if(!camera || !camera_ || !camera_(camera)) return 7;
            if(logic_()!=2) return 4;
            const auto destination=camera+camera_axis_offset;
            std::array<float,9> native{},after{};
            if(!read_(destination,native.data(),sizeof(native)) || !writable(reinterpret_cast<void*>(destination),sizeof(native))) return 8;
            if(!copy_guarded(reinterpret_cast<void*>(destination),axis->data(),sizeof(*axis)) ||
               !read_(destination,after.data(),sizeof(after)) || std::memcmp(after.data(),axis->data(),sizeof(after))!=0) {
                copy_guarded(reinterpret_cast<void*>(destination),native.data(),sizeof(native)); // best effort: stage 7 regenerates anyway
                return 8;
            }
            return 0;
        } catch(...) {return 8;}
    }
    // After the original returns: observation only.
    void after(void* power,std::uintptr_t camera,bool aim_active,int reason) noexcept {
        try {
            Sample s{};s.tick=GetTickCount64();s.power=reinterpret_cast<std::uintptr_t>(power);s.camera=camera;s.aim_active=aim_active;s.reason=reason;
            s.routed=reason==0;
            if(aim_active) {
                if(camera) read_(camera+camera_aim_offset,s.aim.data(),sizeof(s.aim));
                read_(reinterpret_cast<std::uintptr_t>(power)+power_result,s.result.data(),sizeof(s.result));
                s.cam_matches_left=camera && owner_ && owner_()==camera;
                const auto applied=applied_?applied_():0;
                s.left_applied=applied && s.tick>=applied && s.tick-applied<=100;
                ++aim_calls_;if(s.cam_matches_left) ++cam_matches_;if(s.left_applied) ++left_applied_frames_;
                last_reason_=reason;
                if(reason==0) ++routed_;else if(reason!=3) ++rejected_;
            }
            std::lock_guard<std::mutex> lock(mutex_);sample_=s;
        } catch(...) {}
    }
    // The detour: counts, routes before and observes after. depth_ stays raised until the original has returned.
    static void __fastcall detour(void* power) {
        auto& self=*instance_;++self.calls_;
        const bool nested=self.depth_.fetch_add(1,std::memory_order_acq_rel)>0;
        if(nested) ++self.overlap_;
        std::uintptr_t camera=0;bool aim_active=false;int reason=2;
        __try {
            if(!nested) reason=self.before(power,camera,aim_active);
            self.original_(power);
            if(!nested) self.after(power,camera,aim_active,reason);
        } __finally {self.depth_.fetch_sub(1,std::memory_order_acq_rel);}
    }
    // Hand frame from the left-aim publication; call under the same gates (it clears on every other branch).
    void publish(const WorldTrackedPose& hand,std::uint64_t epoch,std::uint64_t tick,bool active) {
        std::lock_guard<std::mutex> lock(mutex_);hand_={tick,epoch,hand,active};
    }
    void clear() {std::lock_guard<std::mutex> lock(mutex_);hand_={};}
    void install(std::uintptr_t base,Read read,Logic logic,Camera camera,OwnerCamera owner,LastApplied applied,bool route=true) {
        if(!base || !read || !logic || !camera || !owner || !applied || active_ || enabled_ || depth_ ||
           (original_ && base!=base_) || (instance_ && instance_!=this))
            throw std::runtime_error("Invalid native blink aim ownership");
        base_=base;read_=read;logic_=logic;camera_=camera;owner_=owner;applied_=applied;
        std::array<std::byte,32> code{};
        if(!read_(base_+blink_aim_rva,code.data(),code.size()) || code!=blink_aim_prologue)
            throw std::runtime_error("Native blink aim function differs");
        if(!read_(base_+blink_aim_thunk_rva,code.data(),code.size()) || code!=blink_aim_thunk_prologue)
            throw std::runtime_error("Native blink aim job thunk differs");
        std::array<std::byte,5> bytes{};std::int32_t displacement{};
        if(!read_(base_+blink_aim_call_rva,bytes.data(),bytes.size()) || bytes[0]!=std::byte{0xe8})
            throw std::runtime_error("Native blink aim caller differs");
        std::memcpy(&displacement,bytes.data()+1,4);
        if(static_cast<std::intptr_t>(base_+blink_aim_call_rva+5)+displacement!=static_cast<std::intptr_t>(base_+blink_aim_rva))
            throw std::runtime_error("Native blink aim caller target differs");
        clear();calls_=aim_calls_=routed_=rejected_=cam_matches_=left_applied_frames_=overlap_=0;last_reason_=-1;sample_={};
        failed_=false;route_=route;instance_=this;
        const auto target=reinterpret_cast<void*>(base_+blink_aim_rva);
        if(!original_ && MH_CreateHook(target,reinterpret_cast<void*>(&detour),reinterpret_cast<void**>(&original_))!=MH_OK)
            throw std::runtime_error("Cannot create native blink aim hook");
        if(MH_EnableHook(target)!=MH_OK) throw std::runtime_error("Cannot enable native blink aim hook");
        enabled_=true;active_=true;
    }
    bool disable() noexcept {
        active_.store(false,std::memory_order_release);route_.store(false,std::memory_order_release);
        if(!original_) return true;
        bool restored=true;
        if(enabled_) {
            const auto status=MH_DisableHook(reinterpret_cast<void*>(base_+blink_aim_rva));
            if(status==MH_OK || status==MH_ERROR_DISABLED) enabled_=false;else restored=false;
        }
        const auto deadline=GetTickCount64()+5000;
        while(depth_ && GetTickCount64()<deadline) Sleep(1);
        if(depth_) return false;
        std::array<std::byte,32> code{};
        if(!read_(base_+blink_aim_rva,code.data(),code.size()) || code!=blink_aim_prologue) {failed_.store(true);restored=false;}
        try {clear();} catch(...) {return false;}
        return restored;
    }
    bool installed() const {return enabled_;}
    UINT64 calls() const {return calls_.load();}
    UINT64 aim_calls() const {return aim_calls_.load();}
    UINT64 routed() const {return routed_.load();}
    UINT64 rejected() const {return rejected_.load();}
    UINT64 overlap() const {return overlap_.load();}
    UINT64 cam_matches() const {return cam_matches_.load();}
    UINT64 left_applied_frames() const {return left_applied_frames_.load();}
    int last_reason() const {return last_reason_.load();}
    bool routing() const {return active_.load() && route_.load() && !failed_.load();}
    bool failed() const {return failed_.load();}
    Sample last_sample() {std::lock_guard<std::mutex> lock(mutex_);return sample_;}
    // The xr-live-status.json "blink_aim" object (no trailing comma; the caller adds the key).
    std::string status_json() {
        const auto s=last_sample();std::ostringstream out;
        out<<"{\"installed\":"<<(enabled_?"true":"false")<<",\"routing\":"<<(routing()?"true":"false")<<",\"calls\":"<<calls()
           <<",\"aim_calls\":"<<aim_calls()<<",\"routed\":"<<routed()<<",\"rejected\":"<<rejected()
           <<",\"last_reason\":"<<last_reason()<<",\"cam_matches_left_aim\":"<<(s.cam_matches_left?"true":"false")
           <<",\"cam_matches_left_aim_calls\":"<<cam_matches()<<",\"left_aim_applied_frames\":"<<left_applied_frames()
           <<",\"overlaps\":"<<overlap()<<",\"last_dest\":["<<(std::isfinite(s.result[3])?s.result[3]:0.f)<<','
           <<(std::isfinite(s.result[4])?s.result[4]:0.f)<<','<<(std::isfinite(s.result[5])?s.result[5]:0.f)<<"]}";
        return out.str();
    }
};
} // namespace d2vr
