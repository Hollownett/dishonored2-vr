#pragma once
// v1430 (night 2026-11-20, engine frame rate): the engine's job threads and its main thread sleep in KERNEL32 WaitForSingleObject (the
// exe's wrappers 158450 / 158570 / 158590) and are woken with SetEvent / ReleaseSemaphore (wrappers 158560 / 158440). The frame is a
// chain of dependent jobs (docs/ENGINE-DIG-NOTES.md section 2: 1-3 of 5 workers run while the main thread waits), so every wake-up on
// that chain adds the time a sleeping core needs to come back; on the Balanced power plan that core may be in a deep C-state.
// This patches the exe's own import table (only the exe's calls go through it; no code bytes change) to
//   - measure, per thread kind (the main/render thread, every other thread): waits, time blocked, and the wake-up latency (from the
//     SetEvent/ReleaseSemaphore that released a sleeping waiter to the waiter running again);
//   - optionally poll the object for a few microseconds before the real wait (knob build/job-spin "<main us> <other us>", dev command
//     `jobspin <main us> <other us>`). A poll is WaitForSingleObject(handle, 0), which takes the signal exactly like the real wait would
//     (an auto-reset event is reset, a semaphore count is taken), so the engine sees the same results, only sooner.
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <string>

namespace d2vr::job_wake {
using WaitFn=DWORD(WINAPI*)(HANDLE,DWORD);
using SetEventFn=BOOL(WINAPI*)(HANDLE);
using ReleaseFn=BOOL(WINAPI*)(HANDLE,LONG,LPLONG);
inline std::atomic<WaitFn> real_wait{};
inline std::atomic<SetEventFn> real_set_event{};
inline std::atomic<ReleaseFn> real_release{};
inline std::atomic<DWORD> main_tid{};
inline std::atomic<std::uint32_t> spin_main_us{0},spin_other_us{0};
inline std::atomic<bool> installed{false};
inline std::int64_t ticks_per_second=1;

struct Kind {
    std::atomic<std::uint64_t> waits{},slept{},blocked{},woken{},wake{},spin_hits{},spin{},hist[6]{};
};
inline Kind kinds[2];  // 0 the main/render thread, 1 every other thread

// Last signal time per handle (stats only: two handles that share a slot just lose a sample).
struct Slot {std::atomic<std::uintptr_t> handle{};std::atomic<std::int64_t> tick{};};
inline Slot slots[4096];
inline Slot& slot_of(HANDLE h) noexcept {return slots[(reinterpret_cast<std::uintptr_t>(h)>>2)&4095];}
inline std::int64_t now() noexcept {LARGE_INTEGER t;QueryPerformanceCounter(&t);return t.QuadPart;}
inline void note_signal(HANDLE h) noexcept {
    auto& s=slot_of(h);s.tick.store(now(),std::memory_order_relaxed);s.handle.store(reinterpret_cast<std::uintptr_t>(h),std::memory_order_release);
}
inline BOOL WINAPI set_event_hook(HANDLE h) {note_signal(h);return real_set_event.load(std::memory_order_relaxed)(h);}
inline BOOL WINAPI release_hook(HANDLE h,LONG n,LPLONG previous) {note_signal(h);return real_release.load(std::memory_order_relaxed)(h,n,previous);}

inline DWORD WINAPI wait_hook(HANDLE h,DWORD timeout) {
    const WaitFn wait=real_wait.load(std::memory_order_relaxed);
    if(timeout==0) return wait(h,0);
    const bool main=GetCurrentThreadId()==main_tid.load(std::memory_order_relaxed);
    Kind& k=kinds[main?0:1];
    k.waits.fetch_add(1,std::memory_order_relaxed);
    const std::uint32_t spin_us=(main?spin_main_us:spin_other_us).load(std::memory_order_relaxed);
    if(spin_us) {
        const std::int64_t t0=now(),deadline=t0+static_cast<std::int64_t>(spin_us)*ticks_per_second/1000000;
        for(;;) {
            const DWORD r=wait(h,0);
            if(r!=WAIT_TIMEOUT) {k.spin_hits.fetch_add(1,std::memory_order_relaxed);k.spin.fetch_add(static_cast<std::uint64_t>(now()-t0),std::memory_order_relaxed);return r;}
            if(now()>=deadline) break;
            for(int i=0;i<16;++i) YieldProcessor();
        }
        const std::int64_t t=now();k.spin.fetch_add(static_cast<std::uint64_t>(t-t0),std::memory_order_relaxed);
        if(timeout!=INFINITE) {const auto spent=static_cast<DWORD>(((t-t0)*1000+ticks_per_second-1)/ticks_per_second);timeout=timeout>spent?timeout-spent:0;}  // rounded up
    }
    const std::int64_t t1=now();
    const DWORD r=wait(h,timeout);
    const std::int64_t t2=now();
    k.blocked.fetch_add(static_cast<std::uint64_t>(t2-t1),std::memory_order_relaxed);
    if((t2-t1)*100000>ticks_per_second) k.slept.fetch_add(1,std::memory_order_relaxed);  // over 10 us: it went to sleep
    if(r==WAIT_OBJECT_0) {
        const Slot& s=slot_of(h);const std::int64_t signal=s.tick.load(std::memory_order_relaxed);
        if(s.handle.load(std::memory_order_acquire)==reinterpret_cast<std::uintptr_t>(h) && signal>=t1 && signal<=t2) {
            const std::int64_t latency=t2-signal;k.woken.fetch_add(1,std::memory_order_relaxed);k.wake.fetch_add(static_cast<std::uint64_t>(latency),std::memory_order_relaxed);
            const std::int64_t us=latency*1000000/ticks_per_second;
            k.hist[us<5?0:us<20?1:us<50?2:us<100?3:us<500?4:5].fetch_add(1,std::memory_order_relaxed);
        }
    }
    return r;
}

inline void** iat_slot(HMODULE module,const char* dll,const char* function) noexcept {
    auto* base=reinterpret_cast<std::uint8_t*>(module);
    const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if(!base || dos->e_magic!=IMAGE_DOS_SIGNATURE) return nullptr;
    const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
    if(nt->Signature!=IMAGE_NT_SIGNATURE) return nullptr;
    const auto& directory=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if(!directory.VirtualAddress) return nullptr;
    for(auto* d=reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base+directory.VirtualAddress);d->Name;++d) {
        // review 2026-11-20: without OriginalFirstThunk there is no name table left in a loaded image (FirstThunk holds the resolved
        // addresses), so such a descriptor cannot be searched by name
        if(_stricmp(reinterpret_cast<const char*>(base+d->Name),dll)!=0 || !d->FirstThunk || !d->OriginalFirstThunk) continue;
        const auto* names=reinterpret_cast<const IMAGE_THUNK_DATA64*>(base+d->OriginalFirstThunk);
        auto* functions=reinterpret_cast<IMAGE_THUNK_DATA64*>(base+d->FirstThunk);
        for(;names->u1.AddressOfData;++names,++functions) {
            if(IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal)) continue;
            const auto* by_name=reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base+names->u1.AddressOfData);
            if(std::strcmp(reinterpret_cast<const char*>(by_name->Name),function)==0) return reinterpret_cast<void**>(&functions->u1.Function);
        }
    }
    return nullptr;
}
inline bool patch(void** slot,void* hook,void** original) noexcept {
    DWORD old{};if(!slot || !VirtualProtect(slot,sizeof(void*),PAGE_READWRITE,&old)) return false;
    *original=InterlockedExchangePointer(slot,hook);
    DWORD unused{};VirtualProtect(slot,sizeof(void*),old,&unused);
    return true;
}
// Once per process. The three slots are found first; nothing is patched unless all three are there.
inline bool install(HMODULE exe) noexcept {
    static std::atomic<bool> tried{false};
    if(tried.exchange(true)) return installed.load();
    LARGE_INTEGER f{};QueryPerformanceFrequency(&f);ticks_per_second=f.QuadPart>0?f.QuadPart:1;
    void** wait=iat_slot(exe,"KERNEL32.dll","WaitForSingleObject");
    void** set=iat_slot(exe,"KERNEL32.dll","SetEvent");
    void** release=iat_slot(exe,"KERNEL32.dll","ReleaseSemaphore");
    if(!wait || !set || !release || !*wait || !*set || !*release) return false;
    // the originals are stored before the slots point at the hooks, so a hook never runs without its original
    real_wait.store(reinterpret_cast<WaitFn>(*wait));real_set_event.store(reinterpret_cast<SetEventFn>(*set));real_release.store(reinterpret_cast<ReleaseFn>(*release));
    void* previous{};
    if(!patch(set,reinterpret_cast<void*>(&set_event_hook),&previous)) return false;
    if(!patch(release,reinterpret_cast<void*>(&release_hook),&previous)) return false;
    if(!patch(wait,reinterpret_cast<void*>(&wait_hook),&previous)) return false;
    installed.store(true);return true;
}
inline std::string json(std::uint64_t frames) {
    std::ostringstream o;o.setf(std::ios::fixed);o.precision(4);
    o<<"{\"installed\":"<<(installed.load()?"true":"false")<<",\"frames\":"<<frames<<",\"spin_main_us\":"<<spin_main_us.load()<<",\"spin_other_us\":"<<spin_other_us.load();
    const double ms=1000./static_cast<double>(ticks_per_second);
    for(int i=0;i<2;++i) {
        const Kind& k=kinds[i];
        o<<",\""<<(i==0?"main":"other")<<"\":{\"waits\":"<<k.waits.load()<<",\"slept\":"<<k.slept.load()<<",\"blocked_ms\":"<<k.blocked.load()*ms
         <<",\"woken\":"<<k.woken.load()<<",\"wake_ms\":"<<k.wake.load()*ms<<",\"spin_hits\":"<<k.spin_hits.load()<<",\"spin_ms\":"<<k.spin.load()*ms
         <<",\"wake_us_hist\":{\"lt5\":"<<k.hist[0].load()<<",\"lt20\":"<<k.hist[1].load()<<",\"lt50\":"<<k.hist[2].load()<<",\"lt100\":"<<k.hist[3].load()
         <<",\"lt500\":"<<k.hist[4].load()<<",\"ge500\":"<<k.hist[5].load()<<"}}";
    }
    o<<"}";return o.str();
}
} // namespace d2vr::job_wake
