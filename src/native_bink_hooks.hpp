#pragma once
// Track T C1: the bink2w64.dll export hooks feeding BinkTracker (bink_video.hpp). Only installs when the module is already loaded.
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
#include "bink_video.hpp"
#include <atomic>
#include <mutex>

namespace d2vr {
class NativeBinkHooks {
public:
    inline static std::atomic<std::uint64_t> opens{},closes{},frames{};
    inline static std::atomic<bool> installed_flag{};
    static bool playing() noexcept {std::lock_guard<std::mutex> l(mutex_);return tracker_.playing(GetTickCount64());}
    static bool installed() noexcept {return installed_flag.load();}
    // Cheap: returns at once when installed or when the module is not loaded; retried by the caller about once a second.
    static bool try_install() noexcept {
        if(installed_flag.load() || tried_) return installed_flag.load();
        const HMODULE module=GetModuleHandleW(L"bink2w64.dll");
        if(!module) return false;
        tried_=true;   // one attempt per process once the module is there
        struct Entry {const char* name;void* detour;void** original;};
        const Entry entries[]{
            {"BinkOpen",reinterpret_cast<void*>(&open_hook),reinterpret_cast<void**>(&open_)},
            {"BinkOpenWithOptions",reinterpret_cast<void*>(&open_options_hook),reinterpret_cast<void**>(&open_options_)},
            {"BinkClose",reinterpret_cast<void*>(&close_hook),reinterpret_cast<void**>(&close_)},
            {"BinkDoFrame",reinterpret_cast<void*>(&frame_hook),reinterpret_cast<void**>(&do_frame_)},
            {"BinkNextFrame",reinterpret_cast<void*>(&next_hook),reinterpret_cast<void**>(&next_)}};
        void* targets[5]{};unsigned n=0;
        for(const auto& e:entries) {
            void* target=reinterpret_cast<void*>(GetProcAddress(module,e.name));
            if(!target || MH_CreateHook(target,e.detour,e.original)!=MH_OK) return false;   // fail soft: nothing enabled
            targets[n++]=target;
        }
        for(unsigned i=0;i<n;++i) {const auto s=MH_EnableHook(targets[i]);if(s!=MH_OK && s!=MH_ERROR_ENABLED) return false;}
        installed_flag=true;return true;
    }
private:
    inline static bool tried_{};
    inline static std::mutex mutex_;inline static BinkTracker tracker_;
    inline static void* (__fastcall *open_)(void*,void*,void*,void*){};
    inline static void* (__fastcall *open_options_)(void*,void*,void*,void*){};
    inline static void (__fastcall *close_)(void*){};
    inline static std::intptr_t (__fastcall *do_frame_)(void*){};
    inline static void (__fastcall *next_)(void*){};
    static void* __fastcall open_hook(void* a,void* b,void* c,void* d) {
        void* h=open_(a,b,c,d);{std::lock_guard<std::mutex> l(mutex_);tracker_.open(reinterpret_cast<std::uintptr_t>(h),GetTickCount64());}++opens;return h;
    }
    static void* __fastcall open_options_hook(void* a,void* b,void* c,void* d) {
        void* h=open_options_(a,b,c,d);{std::lock_guard<std::mutex> l(mutex_);tracker_.open(reinterpret_cast<std::uintptr_t>(h),GetTickCount64());}++opens;return h;
    }
    static void __fastcall close_hook(void* h) {
        {std::lock_guard<std::mutex> l(mutex_);tracker_.close(reinterpret_cast<std::uintptr_t>(h));}++closes;close_(h);
    }
    static std::intptr_t __fastcall frame_hook(void* h) {
        {std::lock_guard<std::mutex> l(mutex_);tracker_.frame(reinterpret_cast<std::uintptr_t>(h),GetTickCount64());}++frames;return do_frame_(h);
    }
    static void __fastcall next_hook(void* h) {
        {std::lock_guard<std::mutex> l(mutex_);tracker_.frame(reinterpret_cast<std::uintptr_t>(h),GetTickCount64());}next_(h);
    }
};
} // namespace d2vr
