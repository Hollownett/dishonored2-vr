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
// them exactly as tests\native_fire_seam_trace_check.cpp does.
typedef enum MH_STATUS {
    MH_UNKNOWN=-1,MH_OK=0,MH_ERROR_ALREADY_INITIALIZED,MH_ERROR_NOT_INITIALIZED,MH_ERROR_ALREADY_CREATED,
    MH_ERROR_NOT_CREATED,MH_ERROR_ENABLED,MH_ERROR_DISABLED,MH_ERROR_NOT_EXECUTABLE,MH_ERROR_UNSUPPORTED_FUNCTION,
    MH_ERROR_MEMORY_ALLOC,MH_ERROR_MEMORY_PROTECT,MH_ERROR_MODULE_NOT_FOUND,MH_ERROR_FUNCTION_NOT_FOUND
} MH_STATUS;
extern "C" MH_STATUS WINAPI MH_CreateHook(LPVOID,LPVOID,LPVOID*);
extern "C" MH_STATUS WINAPI MH_EnableHook(LPVOID);
extern "C" MH_STATUS WINAPI MH_DisableHook(LPVOID);
#endif
#include <intrin.h>
#include "gameplay_aim.hpp"
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <optional>

namespace d2vr {
// Ranged shots (pistol, crossbow, projectile powers) leave along the frame the
// caller hands to 0x639970, so routing that launch frame to the VR hand ray
// makes the shot leave the hand in the hand's direction. Prologue and call
// sites were read from the pinned build
// artifacts/re-toolchain/direct-ghidra/Dishonored2-steam17576867.exe
// (.text vaddr 0x1000/raw 0x400): 0x639970 launch, 9 arguments (rcx weapon
// object, rdx target entity, r8 float3 point, r9 3x3 rows forward/left/up, then
// a float 5th and four more stack slots; the callers ignore its result), called
// by 0x632f70 (single target, call at 0x633419) and 0x62b560 (multi target, call
// at 0x62b8cc). Mutually exclusive with NativeFireSeamTrace's hook of 0x639970:
// whichever installs second sees a patched prologue.
inline constexpr std::uintptr_t shot_aim_rva=0x639970;
inline constexpr std::array<std::array<std::uintptr_t,2>,2> shot_aim_calls{{{0x633419,0x639970},{0x62b8cc,0x639970}}};
inline constexpr std::array<std::byte,32> shot_aim_prologue{{
    std::byte{0x48},std::byte{0x89},std::byte{0x5c},std::byte{0x24},std::byte{0x10},std::byte{0x48},std::byte{0x89},std::byte{0x6c},
    std::byte{0x24},std::byte{0x18},std::byte{0x56},std::byte{0x57},std::byte{0x41},std::byte{0x57},std::byte{0x48},std::byte{0x83},
    std::byte{0xec},std::byte{0x30},std::byte{0x48},std::byte{0x8b},std::byte{0x41},std::byte{0x10},std::byte{0x4c},std::byte{0x8b},
    std::byte{0xf9},std::byte{0x49},std::byte{0x8b},std::byte{0xe9},std::byte{0x8b},std::byte{0x88},std::byte{0xac},std::byte{0x00}
}};
// The engine frame is row-major forward / left / up, which is exactly the
// publication order; gameplay_aim's validator is the orthonormality and
// right-handedness test the native aim writer itself uses.
inline std::optional<GameplayAim> shot_aim_from_hand(const WorldTrackedPose& hand) noexcept {
    GameplayAim result{};
    result.origin={static_cast<float>(hand.position.x),static_cast<float>(hand.position.y),static_cast<float>(hand.position.z)};
    for(unsigned i=0;i<3;++i) {
        const auto& v=hand.basis[i];
        result.axis[3*i]=static_cast<float>(v.x);result.axis[3*i+1]=static_cast<float>(v.y);
        result.axis[3*i+2]=static_cast<float>(v.z);
    }
    if(!valid_aim_frame(result.axis)) return {};
    return result;
}
// Exactly the 12 origin bytes and 36 frame bytes the launch function owns. Plain
// memory in and out so the SEH detour can call these without unwinding objects.
struct ShotAimSave {std::array<float,3> origin{};std::array<float,9> axis{};};
inline void shot_aim_save(const void* origin,const void* axis,ShotAimSave& saved) noexcept {
    std::memcpy(saved.origin.data(),origin,sizeof(saved.origin));
    std::memcpy(saved.axis.data(),axis,sizeof(saved.axis));
}
inline void shot_aim_apply(void* origin,void* axis,const GameplayAim& planned) noexcept {
    std::memcpy(origin,planned.origin.data(),sizeof(planned.origin));
    std::memcpy(axis,planned.axis.data(),sizeof(planned.axis));
}
inline void shot_aim_restore(void* origin,void* axis,const ShotAimSave& saved) noexcept {
    std::memcpy(origin,saved.origin.data(),sizeof(saved.origin));
    std::memcpy(axis,saved.axis.data(),sizeof(saved.axis));
}
// Developer experiment, default observation only. The render thread publishes
// the pointing hand; the game thread swaps the launch frame for one call and
// restores the caller's own bytes before returning. Nothing else is written.
class NativeShotAim {
    // Twelve raw 64-bit slots: rcx, rdx, r8, r9 and eight stack slots, so the
    // float 5th argument and the callers' dword 8th/9th keep their exact bits.
    using Launch=std::uint64_t (__fastcall*)(void*,void*,void*,void*,std::uint64_t,std::uint64_t,
        std::uint64_t,std::uint64_t,std::uint64_t,std::uint64_t,std::uint64_t,std::uint64_t);
    using Read=bool (*)(std::uintptr_t,void*,std::size_t);
    using Logic=UINT (*)();
    struct Publication {std::uint64_t tick{},epoch{};WorldTrackedPose hand{};bool active{};};
    struct Plan {GameplayAim native{};GameplayAim planned{};};
    inline static NativeShotAim* instance_{};
    Launch original_{};
    std::array<bool,1> enabled_{};
    Read read_{};Logic logic_{};std::uintptr_t base_{};
    std::atomic<bool> active_{false},route_{false},failed_{false};
    std::atomic<UINT> depth_{};
    std::atomic<UINT64> calls_{},routed_{},rejected_{},overlap_{},rows_{};
    std::atomic<int> last_reason_{-1};
    std::mutex mutex_;Publication hand_;std::ofstream log_;
    // Whole range committed and writable, so a swap can never fault into the
    // __finally restore and fault a second time on the same page.
    static bool writable(const void* at,std::size_t size) noexcept {
        if(!at || !size) return false;
        MEMORY_BASIC_INFORMATION info{};
        if(VirtualQuery(at,&info,sizeof(info))!=sizeof(info) || info.State!=MEM_COMMIT || (info.Protect&PAGE_GUARD)) return false;
        const auto first=reinterpret_cast<std::uintptr_t>(at);
        const auto base=reinterpret_cast<std::uintptr_t>(info.BaseAddress);
        if(first<base || first+size-1>=base+info.RegionSize) return false;
        constexpr DWORD writable_pages=PAGE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY;
        return (info.Protect&writable_pages)!=0;
    }
    // Reads the native frame only; the read plus the page check is what proves
    // both pointers can be swapped safely.
    bool plan(void* origin,void* axis,Plan& out) noexcept {
        try {
            // last_reason_: 1 inactive/route/failed, 2 nested, 3 not gameplay, 4 stale hand, 5 hand basis, 6 memory.
            if(!active_.load(std::memory_order_acquire) || !route_.load(std::memory_order_acquire) || failed_.load()) {last_reason_=1;return false;}
            if(depth_.load()!=1) {last_reason_=2;return false;}
            if(!logic_ || logic_()!=2) {last_reason_=3;return false;}
            Publication hand;{std::lock_guard<std::mutex> lock(mutex_);hand=hand_;}
            const auto now=GetTickCount64();
            if(!hand.active || !hand.epoch || now<hand.tick || now-hand.tick>150) {last_reason_=4;return false;}
            const auto planned=shot_aim_from_hand(hand.hand);
            if(!planned) {last_reason_=5;return false;}
            if(!read_(reinterpret_cast<std::uintptr_t>(origin),out.native.origin.data(),sizeof(out.native.origin)) ||
               !read_(reinterpret_cast<std::uintptr_t>(axis),out.native.axis.data(),sizeof(out.native.axis)) ||
               !writable(origin,sizeof(out.native.origin)) || !writable(axis,sizeof(out.native.axis))) {last_reason_=6;return false;}
            last_reason_=0;
            out.planned=*planned;return true;
        } catch(...) {return false;}
    }
    void record(std::uintptr_t caller,bool routed,const Plan& plan) noexcept {
        try {
            if(rows_.load()>=2000) return;
            std::lock_guard<std::mutex> lock(mutex_);
            if(rows_.load()>=2000) return;
            log_<<GetTickCount64()<<','<<GetCurrentThreadId()<<','<<(caller>=base_?caller-base_:0)<<','<<routed;
            for(float v:plan.native.origin) log_<<','<<v;
            for(unsigned i=0;i<3;++i) log_<<','<<plan.native.axis[i];
            for(float v:plan.planned.origin) log_<<','<<v;
            for(unsigned i=0;i<3;++i) log_<<','<<plan.planned.axis[i];
            log_<<'\n';const auto rows=rows_.load()+1;rows_.store(rows);log_.flush(); // shots are rare; keep every row
        } catch(...) {}
    }
    static std::uint64_t __fastcall launch(void* weapon,void* target,void* origin,void* axis,
            std::uint64_t a5,std::uint64_t a6,std::uint64_t a7,std::uint64_t a8,
            std::uint64_t a9,std::uint64_t a10,std::uint64_t a11,std::uint64_t a12) {
        auto& self=*instance_;++self.calls_;
        const bool nested=self.depth_.fetch_add(1,std::memory_order_acq_rel)>0;
        if(nested) ++self.overlap_;
        const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
        Plan plan{};ShotAimSave saved{};bool saved_ok=false,routed=false;std::uint64_t result{};
        const bool candidate=!nested && self.plan(origin,axis,plan);
        if(!candidate && !nested && self.active_.load() && self.route_.load()) ++self.rejected_;
        __try {
            if(candidate) {
                shot_aim_save(origin,axis,saved);saved_ok=true;
                shot_aim_apply(origin,axis,plan.planned);routed=true;++self.routed_;
            }
            result=self.original_(weapon,target,origin,axis,a5,a6,a7,a8,a9,a10,a11,a12);
            self.record(caller,routed,plan);
        } __finally {
            // Nothing was written unless the save completed, so a fault while
            // saving leaves the caller's bytes alone.
            if(saved_ok) shot_aim_restore(origin,axis,saved);
            self.depth_.fetch_sub(1,std::memory_order_acq_rel);
        }
        return result;
    }
public:
    void publish(const WorldTrackedPose& hand,std::uint64_t epoch,std::uint64_t tick,bool active) {
        std::lock_guard<std::mutex> lock(mutex_);hand_={tick,epoch,hand,active};
    }
    // Clears intent only; the game thread restores the launch frame it owns.
    void clear() {std::lock_guard<std::mutex> lock(mutex_);hand_={};}
    void install(std::uintptr_t base,Read read,Logic logic,const std::filesystem::path& file,bool route=false) {
        if(!base || !read || !logic || active_ || enabled_[0] || depth_ ||
           (original_ && base!=base_) || (instance_ && instance_!=this))
            throw std::runtime_error("Invalid native shot aim ownership");
        base_=base;read_=read;logic_=logic;
        std::array<std::byte,32> code{};
        if(!read_(base_+shot_aim_rva,code.data(),code.size()) || code!=shot_aim_prologue)
            throw std::runtime_error("Native shot launch code differs");
        for(const auto& call:shot_aim_calls) {
            std::array<std::byte,5> bytes{};std::int32_t displacement{};
            if(!read_(base_+call[0],bytes.data(),bytes.size()) || bytes[0]!=std::byte{0xe8})
                throw std::runtime_error("Native shot launch caller differs");
            std::memcpy(&displacement,bytes.data()+1,4);
            if(static_cast<std::intptr_t>(base_+call[0]+5)+displacement!=static_cast<std::intptr_t>(base_+call[1]))
                throw std::runtime_error("Native shot launch caller target differs");
        }
        log_.open(file,std::ios::trunc);if(!log_) throw std::runtime_error("Cannot open native shot aim log");
        log_<<std::setprecision(9)<<"tick_ms,thread,caller_rva,routed,"
            "native_origin0,native_origin1,native_origin2,native_forward0,native_forward1,native_forward2,"
            "hand_origin0,hand_origin1,hand_origin2,hand_forward0,hand_forward1,hand_forward2\n";
        clear();calls_=routed_=rejected_=overlap_=rows_=0;failed_=false;route_=route;instance_=this;
        const auto target=reinterpret_cast<void*>(base_+shot_aim_rva);
        if(!original_ && MH_CreateHook(target,reinterpret_cast<void*>(&launch),reinterpret_cast<void**>(&original_))!=MH_OK)
            throw std::runtime_error("Cannot create native shot launch hook; retire the fire seam trace first");
        if(MH_EnableHook(target)!=MH_OK) throw std::runtime_error("Cannot enable native shot launch hook");
        enabled_[0]=true;active_=true;
    }
    bool disable() noexcept {
        active_.store(false,std::memory_order_release);route_.store(false,std::memory_order_release);
        if(!original_) return true;
        bool restored=true;
        if(enabled_[0]) {
            const auto status=MH_DisableHook(reinterpret_cast<void*>(base_+shot_aim_rva));
            if(status==MH_OK || status==MH_ERROR_DISABLED) enabled_[0]=false;else restored=false;
        }
        const auto deadline=GetTickCount64()+5000;
        while(depth_ && GetTickCount64()<deadline) Sleep(1);
        if(depth_) return false;
        std::array<std::byte,32> code{};
        if(!read_(base_+shot_aim_rva,code.data(),code.size()) || code!=shot_aim_prologue) {failed_.store(true);restored=false;}
        try {std::lock_guard<std::mutex> lock(mutex_);log_.close();hand_={};} catch(...) {return false;}
        return restored;
    }
    UINT64 calls() const {return calls_.load();}
    UINT64 routed() const {return routed_.load();}
    UINT64 rejected() const {return rejected_.load();}
    int last_reason() const {return last_reason_.load();}
    UINT64 overlap() const {return overlap_.load();}
    UINT64 rows() const {return rows_.load();}
    unsigned hooks() const {return enabled_[0]?1u:0u;}
    bool routing() const {return active_.load() && route_.load() && !failed_.load();}
    bool failed() const {return failed_.load();}
};
} // namespace d2vr
