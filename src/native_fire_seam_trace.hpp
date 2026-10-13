#pragma once
#include <windows.h>
#if __has_include(<MinHook.h>)
#include <MinHook.h>
#else
// The DLL build adds /Ithird_party\minhook\include. The standalone check command
// has no /I, so name the three entry points this header uses; the check stubs
// them exactly as tests\native_gameplay_aim_lifecycle_check.cpp does.
typedef enum MH_STATUS {
    MH_UNKNOWN=-1,MH_OK=0,MH_ERROR_ALREADY_INITIALIZED,MH_ERROR_NOT_INITIALIZED,MH_ERROR_ALREADY_CREATED,
    MH_ERROR_NOT_CREATED,MH_ERROR_ENABLED,MH_ERROR_DISABLED,MH_ERROR_NOT_EXECUTABLE,MH_ERROR_UNSUPPORTED_FUNCTION,
    MH_ERROR_MEMORY_ALLOC,MH_ERROR_MEMORY_PROTECT,MH_ERROR_MODULE_NOT_FOUND,MH_ERROR_FUNCTION_NOT_FOUND
} MH_STATUS;
extern "C" MH_STATUS WINAPI MH_CreateHook(LPVOID,LPVOID,LPVOID*);
extern "C" MH_STATUS WINAPI MH_EnableHook(LPVOID);
extern "C" MH_STATUS WINAPI MH_DisableHook(LPVOID);
extern "C" MH_STATUS WINAPI MH_RemoveHook(LPVOID);
#endif
#include <intrin.h>
#include "native_player_camera.hpp"
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>

namespace d2vr {
// Passive observation of the native aim/line seam the pistol and crossbow fire
// through, plus the wider fire-path probe (0x867330/0x62b560/0x639bc0/0x88e140)
// that finds the ranged-weapon path our 0x639970 argument swap never sees.
// Nothing here writes an argument, result, camera or weapon field. Every hook is
// optional: a prologue mismatch skips that one function and is noted.
// Prologues were read from the pinned build
// artifacts/re-toolchain/direct-ghidra/Dishonored2-steam17576867.exe
// (TimeDateStamp 0x67a50cde, SizeOfImage 0x41cb000, CheckSum 0x27436c9):
//  0x632f70 fire aim, one argument; 0x633341 picks the frame at *[rcx+0x48]
//           +0x25288 when byte [*[rcx+0x8]+0x2ac] is set, else *[rcx+0x10]+0x30c.
//  0x639970 aim output, 9 arguments (4 registers, float 5th, qword 6th/7th,
//           dword 8th/9th at [rsp+0x40]/[rsp+0x48]); r8 is the point, r9 the
//           frame and [rsp+0x28] a float. It returns void.
//  0x638570 engine level line check, 5 arguments (rcx is dead and reloaded with
//           the world); rdx/r8 are float3 points, r9 the filter word (0x800)
//           and [rsp+0x28] an out pointer. It returns a byte (0x638d64: movzx
//           ebx,al / test al,al).
//  0x867330 arkPlayerStateFireWeapon, one argument (param_1+0x10 world,
//           +0x78 state word, +0x8c the packed target ids, +0x88 the count).
//           Called very often, so a row appears only when the state word of
//           the same object changed, and CSV rows are capped.
//  0x62b560 multi-target launch sibling, one argument; it calls 0x639970 and
//           [rcx+0x10]+0x30c is the launch frame while [rcx+0xc8]&0xffffff is
//           the packed target count.
//  0x639bc0 fire validation, three arguments, returns a byte.
//  0x88e140 origin/axis accessor, three arguments: it copies [rcx+0xa8] float3
//           and [rcx+0xcc] 3x3 out, so the object itself carries both. Per-frame
//           hot, so its caller RVAs live in an in-memory histogram dumped at
//           disable and only a capped number of CSV rows is written.
inline constexpr std::array<std::uintptr_t,7> fire_seam_rvas{0x632f70,0x639970,0x638570,0x867330,0x62b560,0x639bc0,0x88e140};
inline constexpr std::array<std::array<unsigned char,32>,7> fire_seam_prologues{{
    {0x40,0x55,0x57,0x48,0x8d,0xac,0x24,0x68,0xff,0xff,0xff,0x48,0x81,0xec,0x98,0x01,0x00,0x00,0x48,0x8b,0x81,0x60,0x01,0x00,0x00,0x48,0x8b,0xf9,0x48,0x85,0xc0,0x74},
    {0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18,0x56,0x57,0x41,0x57,0x48,0x83,0xec,0x30,0x48,0x8b,0x41,0x10,0x4c,0x8b,0xf9,0x49,0x8b,0xe9,0x8b,0x88,0xac,0x00},
    {0x48,0x8b,0xc4,0x55,0x48,0x8d,0xa8,0x68,0xff,0xff,0xff,0x48,0x81,0xec,0x90,0x01,0x00,0x00,0x48,0xc7,0x45,0x00,0xfe,0xff,0xff,0xff,0x48,0x89,0x58,0x08,0x0f,0x29},
    {0x40,0x53,0x55,0x41,0x55,0x48,0x81,0xec,0x80,0x00,0x00,0x00,0x48,0x8b,0x59,0x10,0x48,0x8b,0xe9,0x41,0xbd,0x01,0x00,0x00,0x00,0x4c,0x8d,0x0d,0x80,0xec,0x15,0x03},
    {0x40,0x55,0x57,0x48,0x8d,0x6c,0x24,0x88,0x48,0x81,0xec,0x78,0x01,0x00,0x00,0x48,0x8b,0x01,0x48,0x8b,0xf9,0xff,0x90,0x10,0x01,0x00,0x00,0x84,0xc0,0x0f,0x84,0x01},
    {0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18,0x56,0x48,0x83,0xec,0x50,0x49,0x8b,0xf0,0x48,0x8b,0xda,0x48,0x8b,0xe9,0x48,0x85,0xd2,0x0f,0x84,0x85,0x01,0x00},
    {0x8b,0x81,0xa8,0x00,0x00,0x00,0x89,0x02,0x8b,0x81,0xac,0x00,0x00,0x00,0x89,0x42,0x04,0x8b,0x81,0xb0,0x00,0x00,0x00,0x89,0x42,0x08,0x8b,0x81,0xcc,0x00,0x00,0x00}
}};
inline constexpr UINT64 fire_seam_rows=4000,fire_seam_hot_rows=512;
// 0x867330 updates every tick and 0x88e140 is read per frame, so those two keep
// only a small CSV budget; the fire itself must keep room under the 4000-row cap.
inline bool fire_seam_hot(unsigned index) noexcept {return index==3 || index==6;}
// The rare functions flush every row so a crash still leaves the shot behind.
inline bool fire_seam_flush_each(unsigned index) noexcept {return index<2 || index==4 || index==5;}
// The engine stores frames row-major as forward / left / up (0x109CC0 writes the
// view's first row as the second viewaxis row), so the forward row is the first.
inline std::array<float,3> fire_seam_forward_row(const std::array<float,9>& axis) noexcept {
    return {axis[0],axis[1],axis[2]};
}
struct FireSeamRow {
    UINT64 call{},tick{};DWORD thread{};
    unsigned site{},flag{},result{};
    std::uintptr_t function{},caller{},a{},b{},c{},axis{};
    float value{};
    std::array<float,3> start{},end{};
    std::array<float,9> frame{};
    std::uintptr_t camera{};
    std::array<float,3> override_origin{},override_forward{};
};
inline const char* fire_seam_site_name(unsigned site) noexcept {
    constexpr const char* names[]{"aim","output","line","fire","launch","validate","axis"};
    return site<7?names[site]:"other";
}
inline const char* fire_seam_header() noexcept {
    return "call,tick_ms,thread,site,function_rva,caller_rva,a,b,c,axis_pointer,flag,result,value,"
           "start0,start1,start2,end0,end1,end2,"
           "frame0,frame1,frame2,frame3,frame4,frame5,frame6,frame7,frame8,"
           "camera_pointer,override_origin0,override_origin1,override_origin2,"
           "override_forward0,override_forward1,override_forward2\n";
}
inline void fire_seam_write_row(std::ostream& out,const FireSeamRow& row) {
    out<<row.call<<','<<row.tick<<','<<row.thread<<','<<fire_seam_site_name(row.site)<<','<<row.function<<','<<row.caller<<','
       <<row.a<<','<<row.b<<','<<row.c<<','<<row.axis<<','<<row.flag<<','<<row.result<<','<<row.value;
    for(float f:row.start) out<<','<<f;
    for(float f:row.end) out<<','<<f;
    for(float f:row.frame) out<<','<<f;
    out<<','<<row.camera;
    for(float f:row.override_origin) out<<','<<f;
    for(float f:row.override_forward) out<<','<<f;
    out<<'\n';
}
// Prints the 32 bytes the way fire_seam_prologues spells them.
inline std::string fire_seam_prologue_text(const std::array<unsigned char,32>& bytes) {
    std::ostringstream out;out<<std::hex<<std::setfill('0');
    for(unsigned char byte:bytes) out<<"0x"<<std::setw(2)<<static_cast<unsigned>(byte)<<',';
    return out.str();
}
class NativeFireSeamTrace {
    using Aim=void (__fastcall*)(void*);
    using Output=void (__fastcall*)(void*,void*,void*,void*,float,std::uintptr_t,std::uintptr_t,int,int);
    using Line=unsigned char (__fastcall*)(void*,void*,void*,std::uintptr_t,void*);
    using Validate=unsigned char (__fastcall*)(void*,void*,void*);
    using Axis=void (__fastcall*)(void*,void*,void*);
    using Read=bool (*)(std::uintptr_t,void*,std::size_t);
    struct CallerCount {std::uintptr_t caller{};UINT64 count{};};
    // Bounded in-memory caller histogram: 64 distinct RVAs, the rest overflow.
    struct CallerHistogram {
        mutable std::mutex mutex;std::array<CallerCount,64> slots{};std::size_t used{};UINT64 overflow{};
        void add(std::uintptr_t caller) noexcept {
            if(!caller) return;
            std::lock_guard<std::mutex> lock(mutex);
            for(std::size_t i=0;i<used;++i) if(slots[i].caller==caller) {++slots[i].count;return;}
            if(used<slots.size()) slots[used++]={caller,1};else ++overflow;
        }
        void top(std::array<CallerCount,64>& sorted,std::size_t& size,UINT64& overflowed,std::size_t limit) const noexcept {
            std::lock_guard<std::mutex> lock(mutex);
            sorted=slots;size=used;overflowed=overflow;
            std::sort(sorted.begin(),sorted.begin()+size,[](const CallerCount& a,const CallerCount& b) {
                return a.count!=b.count?a.count>b.count:a.caller<b.caller;
            });
            if(size>limit) size=limit;
        }
    };
    inline static NativeFireSeamTrace* instance_{};
    Aim original_aim_{};Output original_output_{};Line original_line_{};
    Aim original_fire_{};Aim original_launch_{};Validate original_validate_{};Axis original_axis_{};
    Read read_{};std::uintptr_t base_{};
    std::array<bool,7> enabled_{},created_{};
    std::atomic<bool> active_{false};std::atomic<UINT> depth_{};
    std::atomic<std::uintptr_t> camera_{};
    std::atomic<UINT64> calls_{},samples_{},rejected_{},overlap_{},dropped_{};
    std::array<std::atomic<UINT64>,7> function_calls_{},function_rows_{};
    std::atomic<std::uintptr_t> fire_object_{};std::atomic<unsigned> fire_state_{0xffffffffu};
    CallerHistogram axis_callers_{},line_callers_{};
    std::mutex mutex_;std::ofstream log_;
    // Prefer whichever of the direct argument and its +0x48 pointer verifies as
    // the camera the aim writer owns; the other is only a reader's view of it.
    void update_camera(std::uintptr_t owner,std::uintptr_t candidate) noexcept {
        if(rig::typed(read_,base_,owner,camera_vtable,camera_secondary_vtable)) camera_.store(owner,std::memory_order_relaxed);
        else if(rig::typed(read_,base_,candidate,camera_vtable,camera_secondary_vtable)) camera_.store(candidate,std::memory_order_relaxed);
    }
    // The override the aim writer owns, once per logged call so a line point can
    // be matched against the hand pose or the camera view.
    void camera_pose(FireSeamRow& row) noexcept {
        const auto camera=camera_.load(std::memory_order_relaxed);
        if(!camera) return;
        row.camera=camera;
        if(const auto origin=rig::value<std::array<float,3>>(read_,camera+0x2527c)) row.override_origin=*origin;
        if(const auto axis=rig::value<std::array<float,9>>(read_,camera+0x25288)) row.override_forward=fire_seam_forward_row(*axis);
    }
    // The lock-free budget test, so a spent hot function never touches the game
    // memory our readers walk.
    bool rows_open(unsigned index) const noexcept {
        return samples_.load(std::memory_order_relaxed)<fire_seam_rows &&
            (!fire_seam_hot(index) || function_rows_[index].load(std::memory_order_relaxed)<fire_seam_hot_rows);
    }
    void record(FireSeamRow row,unsigned index) noexcept {
        try {
            std::lock_guard<std::mutex> lock(mutex_);
            if(!active_.load(std::memory_order_acquire) || !log_) return;
            if(samples_>=fire_seam_rows || (fire_seam_hot(index) && function_rows_[index]>=fire_seam_hot_rows)) {++dropped_;return;}
            fire_seam_write_row(log_,row);++samples_;++function_rows_[index];
            if(fire_seam_flush_each(index) || samples_%120==0) log_.flush();
        } catch(...) {++rejected_;}
    }
    // a=rcx camera container, b=*[a+0x10] frame owner, c=*[a+0x8] byte owner,
    // flag=[c+0x2ac], axis=the selected frame, end=the rejected b+0x30c frame.
    void observe_aim(void* param,std::uintptr_t caller,UINT64 call) noexcept {
        try {
            FireSeamRow row{};row.call=call;row.tick=GetTickCount64();row.thread=GetCurrentThreadId();
            row.site=0;row.function=fire_seam_rvas[0];row.caller=caller-base_;
            const auto a=reinterpret_cast<std::uintptr_t>(param);row.a=a;
            const auto alternative=rig::value<std::uintptr_t>(read_,a+0x10);
            const auto owner=rig::value<std::uintptr_t>(read_,a+0x8);
            const auto preferred=rig::value<std::uintptr_t>(read_,a+0x48);
            row.b=alternative.value_or(0);row.c=owner.value_or(0);
            row.flag=rig::value<unsigned char>(read_,row.c+0x2ac).value_or(0);
            std::uintptr_t axis=0;
            if(row.flag && preferred && *preferred) axis=*preferred+0x25288;
            else if(alternative && *alternative) axis=*alternative+0x30c;
            row.axis=axis;
            if(const auto frame=rig::value<std::array<float,9>>(read_,axis)) {row.frame=*frame;row.start=fire_seam_forward_row(*frame);}
            if(alternative && *alternative) if(const auto entity=rig::value<std::array<float,9>>(read_,*alternative+0x30c))
                row.end=fire_seam_forward_row(*entity);
            update_camera(a,preferred.value_or(0));
            camera_pose(row);record(row,0);
        } catch(...) {++rejected_;}
    }
    // a=rcx, b=rdx object, c=r8 point, axis=r9 frame, value=the float 5th arg.
    void observe_output(void* owner,void* object,void* point,void* axis_ptr,float value,std::uintptr_t caller,UINT64 call) noexcept {
        try {
            FireSeamRow row{};row.call=call;row.tick=GetTickCount64();row.thread=GetCurrentThreadId();
            row.site=1;row.function=fire_seam_rvas[1];row.caller=caller-base_;
            row.a=reinterpret_cast<std::uintptr_t>(owner);row.b=reinterpret_cast<std::uintptr_t>(object);
            row.c=reinterpret_cast<std::uintptr_t>(point);row.axis=reinterpret_cast<std::uintptr_t>(axis_ptr);
            row.value=value;
            if(const auto start=rig::value<std::array<float,3>>(read_,row.c)) row.start=*start;
            if(const auto frame=rig::value<std::array<float,9>>(read_,row.axis)) {row.frame=*frame;row.end=fire_seam_forward_row(*frame);}
            update_camera(row.a,rig::value<std::uintptr_t>(read_,row.a+0x48).value_or(0));
            camera_pose(row);record(row,1);
        } catch(...) {++rejected_;}
    }
    // a=rcx (dead world argument), b=rdx start, c=r8 end, flag=r9 filter word.
    void observe_line(void* world,void* start,void* end,std::uintptr_t flags,unsigned char result,std::uintptr_t caller,UINT64 call) noexcept {
        try {
            FireSeamRow row{};row.call=call;row.tick=GetTickCount64();row.thread=GetCurrentThreadId();
            row.site=2;row.function=fire_seam_rvas[2];row.caller=caller-base_;
            row.a=reinterpret_cast<std::uintptr_t>(world);row.b=reinterpret_cast<std::uintptr_t>(start);
            row.c=reinterpret_cast<std::uintptr_t>(end);row.flag=static_cast<unsigned>(flags);row.result=result;
            if(const auto first=rig::value<std::array<float,3>>(read_,row.b)) row.start=*first;
            if(const auto second=rig::value<std::array<float,3>>(read_,row.c)) row.end=*second;
            // Whoever asks the engine for a line is a candidate fire path, so its
            // caller RVAs are counted even when the row budget is already spent.
            line_callers_.add(row.caller);
            camera_pose(row);record(row,2);
        } catch(...) {++rejected_;}
    }
    // a=rcx player state; a row only for a changed (object,state at +0x78) pair.
    void observe_fire(void* state,std::uintptr_t caller,UINT64 call) noexcept {
        try {
            const auto object=reinterpret_cast<std::uintptr_t>(state);
            if(!object) return;
            const auto current=static_cast<unsigned>(rig::value<int>(read_,object+0x78).value_or(0));
            if(object==fire_object_.load(std::memory_order_relaxed) && current==fire_state_.load(std::memory_order_relaxed)) return;
            fire_object_.store(object,std::memory_order_relaxed);fire_state_.store(current,std::memory_order_relaxed);
            FireSeamRow row{};row.call=call;row.tick=GetTickCount64();row.thread=GetCurrentThreadId();
            row.site=3;row.function=fire_seam_rvas[3];row.caller=caller-base_;
            row.a=object;row.b=rig::value<std::uintptr_t>(read_,object+0x10).value_or(0);
            row.c=rig::value<std::uintptr_t>(read_,object+0x18).value_or(0);
            row.flag=current;row.result=rig::value<unsigned>(read_,object+0x88).value_or(0);
            row.value=rig::value<float>(read_,object+0x7c).value_or(0);
            record(row,3);
        } catch(...) {++rejected_;}
    }
    // a=rcx, b=[a+0x10] world, axis=b+0x30c the launch frame, flag the packed
    // target count at [a+0xc8].
    void observe_launch(void* owner,std::uintptr_t caller,UINT64 call) noexcept {
        try {
            FireSeamRow row{};row.call=call;row.tick=GetTickCount64();row.thread=GetCurrentThreadId();
            row.site=4;row.function=fire_seam_rvas[4];row.caller=caller-base_;
            row.a=reinterpret_cast<std::uintptr_t>(owner);
            row.b=rig::value<std::uintptr_t>(read_,row.a+0x10).value_or(0);
            row.c=rig::value<std::uintptr_t>(read_,row.a+0x18).value_or(0);
            row.flag=rig::value<unsigned>(read_,row.a+0xc8).value_or(0)&0xffffffu;
            if(row.b) {
                row.axis=row.b+0x30c;
                if(const auto frame=rig::value<std::array<float,9>>(read_,row.axis)) {row.frame=*frame;row.end=fire_seam_forward_row(*frame);}
            }
            record(row,4);
        } catch(...) {++rejected_;}
    }
    // a=rcx, b=rdx, c=r8 candidate ids; result is the returned byte.
    void observe_validate(void* first,void* second,void* third,unsigned char result,std::uintptr_t caller,UINT64 call) noexcept {
        try {
            FireSeamRow row{};row.call=call;row.tick=GetTickCount64();row.thread=GetCurrentThreadId();
            row.site=5;row.function=fire_seam_rvas[5];row.caller=caller-base_;
            row.a=reinterpret_cast<std::uintptr_t>(first);row.b=reinterpret_cast<std::uintptr_t>(second);
            row.c=reinterpret_cast<std::uintptr_t>(third);row.result=result;
            record(row,5);
        } catch(...) {++rejected_;}
    }
    // a=rcx pose object, b=rdx out origin, c=r8 out 3x3; the copied fields stay
    // on the object after the call, so the row reads them from there.
    void observe_axis(void* object,void* origin,void* frame,std::uintptr_t caller,UINT64 call) noexcept {
        try {
            const auto owner=reinterpret_cast<std::uintptr_t>(object);
            const auto relative=caller-base_;
            axis_callers_.add(relative);
            if(!rows_open(6)) return;
            FireSeamRow row{};row.call=call;row.tick=GetTickCount64();row.thread=GetCurrentThreadId();
            row.site=6;row.function=fire_seam_rvas[6];row.caller=relative;
            row.a=owner;row.b=reinterpret_cast<std::uintptr_t>(origin);row.c=reinterpret_cast<std::uintptr_t>(frame);
            if(const auto source=rig::value<std::array<float,3>>(read_,owner+0xa8)) row.start=*source;
            if(const auto axis=rig::value<std::array<float,9>>(read_,owner+0xcc)) {row.frame=*axis;row.end=fire_seam_forward_row(*axis);}
            record(row,6);
        } catch(...) {++rejected_;}
    }
    static void __fastcall aim(void* param) {
        auto& self=*instance_;const auto call=++self.calls_;++self.function_calls_[0];
        const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
        if(self.depth_.fetch_add(1,std::memory_order_acq_rel)>0) ++self.overlap_;
        __try {self.observe_aim(param,caller,call);self.original_aim_(param);}
        __finally {self.depth_.fetch_sub(1,std::memory_order_acq_rel);}
    }
    static void __fastcall output(void* owner,void* object,void* point,void* axis,float value,
            std::uintptr_t d6,std::uintptr_t d7,int d8,int d9) {
        auto& self=*instance_;const auto call=++self.calls_;++self.function_calls_[1];
        const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
        if(self.depth_.fetch_add(1,std::memory_order_acq_rel)>0) ++self.overlap_;
        __try {
            self.observe_output(owner,object,point,axis,value,caller,call);
            self.original_output_(owner,object,point,axis,value,d6,d7,d8,d9);
        } __finally {self.depth_.fetch_sub(1,std::memory_order_acq_rel);}
    }
    static unsigned char __fastcall line(void* world,void* start,void* end,std::uintptr_t flags,void* out) {
        auto& self=*instance_;const auto call=++self.calls_;++self.function_calls_[2];
        const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
        if(self.depth_.fetch_add(1,std::memory_order_acq_rel)>0) ++self.overlap_;
        unsigned char result{};
        // The original loads both points on entry and never rewrites them, so the
        // row can carry its return value.
        __try {result=self.original_line_(world,start,end,flags,out);self.observe_line(world,start,end,flags,result,caller,call);}
        __finally {self.depth_.fetch_sub(1,std::memory_order_acq_rel);}
        return result;
    }
    static void __fastcall fire(void* state) {
        auto& self=*instance_;const auto call=++self.calls_;++self.function_calls_[3];
        const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
        if(self.depth_.fetch_add(1,std::memory_order_acq_rel)>0) ++self.overlap_;
        __try {
            if(self.active_.load(std::memory_order_acquire) && self.rows_open(3)) self.observe_fire(state,caller,call);
            self.original_fire_(state);
        } __finally {self.depth_.fetch_sub(1,std::memory_order_acq_rel);}
    }
    static void __fastcall launch(void* owner) {
        auto& self=*instance_;const auto call=++self.calls_;++self.function_calls_[4];
        const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
        if(self.depth_.fetch_add(1,std::memory_order_acq_rel)>0) ++self.overlap_;
        __try {
            if(self.active_.load(std::memory_order_acquire) && self.rows_open(4)) self.observe_launch(owner,caller,call);
            self.original_launch_(owner);
        } __finally {self.depth_.fetch_sub(1,std::memory_order_acq_rel);}
    }
    static unsigned char __fastcall validate(void* first,void* second,void* third) {
        auto& self=*instance_;const auto call=++self.calls_;++self.function_calls_[5];
        const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
        if(self.depth_.fetch_add(1,std::memory_order_acq_rel)>0) ++self.overlap_;
        unsigned char result{};
        __try {
            result=self.original_validate_(first,second,third);
            if(self.active_.load(std::memory_order_acquire) && self.rows_open(5)) self.observe_validate(first,second,third,result,caller,call);
        } __finally {self.depth_.fetch_sub(1,std::memory_order_acq_rel);}
        return result;
    }
    static void __fastcall axis(void* object,void* origin,void* frame) {
        auto& self=*instance_;const auto call=++self.calls_;++self.function_calls_[6];
        const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
        if(self.depth_.fetch_add(1,std::memory_order_acq_rel)>0) ++self.overlap_;
        __try {
            // observe_axis always counts the caller; only its rows are capped.
            if(self.active_.load(std::memory_order_acquire)) self.observe_axis(object,origin,frame,caller,call);
            self.original_axis_(object,origin,frame);
        } __finally {self.depth_.fetch_sub(1,std::memory_order_acq_rel);}
    }
    // A retired hook is written into the CSV as a '#' note and the debug log.
    void skip(unsigned index,const char* reason) {
        std::ostringstream line;
        line<<"#skip 0x"<<std::hex<<fire_seam_rvas[index]<<std::dec<<' '<<reason<<'\n';
        if(log_) log_<<line.str();
        OutputDebugStringA(line.str().c_str());
    }
    static void append_callers(std::ostream& out,std::uintptr_t function,const CallerHistogram& histogram,std::size_t limit) {
        std::array<CallerCount,64> sorted{};std::size_t size=0;UINT64 overflow=0;
        histogram.top(sorted,size,overflow,limit);
        out<<"\"0x"<<std::hex<<function<<std::dec<<"\":{\"overflow\":"<<overflow<<",\"top\":[";
        for(std::size_t i=0;i<size;++i) {
            if(i) out<<',';
            out<<"[\"0x"<<std::hex<<sorted[i].caller<<std::dec<<"\","<<sorted[i].count<<']';
        }
        out<<"]}";
    }
    void dump_callers(std::ostream& out,std::uintptr_t function,const CallerHistogram& histogram) const {
        std::array<CallerCount,64> sorted{};std::size_t size=0;UINT64 overflow=0;
        histogram.top(sorted,size,overflow,64);
        for(std::size_t i=0;i<size;++i) out<<"#caller 0x"<<std::hex<<function<<' '<<sorted[i].caller<<std::dec<<' '<<sorted[i].count<<'\n';
        if(overflow) out<<"#caller_overflow 0x"<<std::hex<<function<<std::dec<<' '<<overflow<<'\n';
    }
public:
    void install(std::uintptr_t base,Read read,const std::filesystem::path& file) {
        if(!base || !read) throw std::runtime_error("Missing native fire seam readers");
        if(instance_ && instance_!=this) throw std::runtime_error("Another native fire seam trace owns this DLL");
        for(bool enabled:enabled_) if(enabled) throw std::runtime_error("Native fire seam trace already installed");
        base_=base;read_=read;
        // Verify every prologue first: a mismatch only retires that one hook.
        std::array<bool,7> matched{};
        unsigned matches=0;
        for(unsigned i=0;i<fire_seam_rvas.size();++i) {
            std::array<unsigned char,32> code{};
            if(read_(base+fire_seam_rvas[i],code.data(),code.size()) && code==fire_seam_prologues[i]) {matched[i]=true;++matches;}
        }
        if(!matches) throw std::runtime_error("Native fire seam code differs");
        log_.open(file,std::ios::trunc);if(!log_) throw std::runtime_error("Cannot open native fire seam trace");
        log_<<std::setprecision(9)<<fire_seam_header();
        calls_=samples_=rejected_=overlap_=dropped_=0;camera_.store(0);
        for(auto& counter:function_calls_) counter=0;
        for(auto& rows:function_rows_) rows=0;
        fire_object_.store(0);fire_state_.store(0xffffffffu);
        instance_=this;
        const std::array<void*,7> bodies{reinterpret_cast<void*>(&aim),reinterpret_cast<void*>(&output),reinterpret_cast<void*>(&line),
            reinterpret_cast<void*>(&fire),reinterpret_cast<void*>(&launch),reinterpret_cast<void*>(&validate),reinterpret_cast<void*>(&axis)};
        const std::array<void**,7> originals{reinterpret_cast<void**>(&original_aim_),reinterpret_cast<void**>(&original_output_),
            reinterpret_cast<void**>(&original_line_),reinterpret_cast<void**>(&original_fire_),reinterpret_cast<void**>(&original_launch_),
            reinterpret_cast<void**>(&original_validate_),reinterpret_cast<void**>(&original_axis_)};
        for(unsigned i=0;i<fire_seam_rvas.size();++i) {
            if(!matched[i]) {skip(i,"prologue_mismatch");continue;}
            const auto target=reinterpret_cast<void*>(base_+fire_seam_rvas[i]);
            if(!*originals[i] && MH_CreateHook(target,bodies[i],originals[i])!=MH_OK) {skip(i,"create_hook_failed");continue;}
            created_[i]=true;
            if(MH_EnableHook(target)!=MH_OK) {skip(i,"enable_hook_failed");continue;}
            enabled_[i]=true;
        }
        if(!hooks()) {
            for(unsigned i=0;i<fire_seam_rvas.size();++i) if(created_[i]) MH_DisableHook(reinterpret_cast<void*>(base_+fire_seam_rvas[i]));
            try {log_.close();} catch(...) {}
            throw std::runtime_error("Native fire seam hooked nothing");
        }
        active_.store(true,std::memory_order_release);
    }
    bool disable() noexcept {
        active_.store(false,std::memory_order_release);
        bool any=false;for(bool created:created_) any=any||created;
        if(!any) return true;
        const std::array<bool,7> was_enabled=enabled_;
        bool restored=true;
        for(unsigned i=0;i<fire_seam_rvas.size();++i) if(enabled_[i]) {
            const auto status=MH_DisableHook(reinterpret_cast<void*>(base_+fire_seam_rvas[i]));
            if(status==MH_OK || status==MH_ERROR_DISABLED) enabled_[i]=false;else restored=false;
        }
        const auto deadline=GetTickCount64()+5000;
        while(depth_ && GetTickCount64()<deadline) Sleep(1);
        if(depth_) return false;
        for(unsigned i=0;i<fire_seam_rvas.size();++i) if(was_enabled[i]) {
            std::array<unsigned char,32> code{};
            restored=restored && read_(base_+fire_seam_rvas[i],code.data(),code.size()) && code==fire_seam_prologues[i];
        }
        try {
            std::lock_guard<std::mutex> lock(mutex_);
            // The histogram is the deliverable for the hot accessor, so it is
            // dumped before the file closes.
            dump_callers(log_,fire_seam_rvas[6],axis_callers_);
            dump_callers(log_,fire_seam_rvas[2],line_callers_);
            log_.close();camera_.store(0);
        } catch(...) {return false;}
        return restored;
    }
    // Transfer our retained, disabled launch site to the production router.
    // All observer callbacks drain before its trampoline is removed.
    bool release_shot_hook() noexcept {
        constexpr unsigned index=1;
        if(!created_[index]) return true;
        if(!disable() || depth_) return false;
        if(MH_RemoveHook(reinterpret_cast<void*>(base_+fire_seam_rvas[index]))!=MH_OK)return false;
        original_output_=nullptr;created_[index]=enabled_[index]=false;return true;
    }
    UINT64 calls() const {return calls_.load();}
    UINT64 samples() const {return samples_.load();}
    UINT64 rejected() const {return rejected_.load();}
    UINT64 overlap() const {return overlap_.load();}
    UINT64 dropped() const {return dropped_.load();}
    unsigned hooks() const {unsigned count=0;for(bool enabled:enabled_) if(enabled) ++count;return count;}
    bool installed(std::uintptr_t rva) const noexcept {
        for(unsigned i=0;i<fire_seam_rvas.size();++i) if(fire_seam_rvas[i]==rva) return enabled_[i];
        return false;
    }
    // Compact one-line JSON for xr-live-status.json: per-function call counts,
    // the retired hooks, and the top caller RVAs of 0x88e140 and 0x638570.
    std::string summary() const {
        std::ostringstream out;
        out<<"{\"hooks\":"<<hooks()<<",\"skipped\":[";
        bool first=true;
        for(unsigned i=0;i<fire_seam_rvas.size();++i) if(!enabled_[i]) {
            if(!first) out<<',';
            out<<"\"0x"<<std::hex<<fire_seam_rvas[i]<<std::dec<<'"';first=false;
        }
        out<<']';
        for(unsigned i=0;i<fire_seam_rvas.size();++i)
            out<<",\"0x"<<std::hex<<fire_seam_rvas[i]<<std::dec<<"\":"<<function_calls_[i].load();
        out<<",\"samples\":"<<samples_.load()<<",\"dropped\":"<<dropped_.load()<<",\"callers\":{";
        append_callers(out,fire_seam_rvas[6],axis_callers_,8);
        out<<',';
        append_callers(out,fire_seam_rvas[2],line_callers_,8);
        out<<"}}";
        return out.str();
    }
};
} // namespace d2vr
