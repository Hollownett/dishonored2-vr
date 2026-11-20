#pragma once
// v1430 (night 2026-11-20, engine frame rate): the game's main thread is the frame's critical path (docs/ENGINE-DIG-NOTES.md section 2).
// A read-only sim probe (thread ideal processors, 2026-11-20 03:36) showed the engine gives its five job workers (start exe:7a220) the
// ideal processors 0/5/7/9/11 and the main thread 1; on the 6-core/12-thread Ryzen logical processors 2k and 2k+1 are one physical core,
// so the main thread's preferred core is shared with a job worker. This module gives the main thread a physical core of its own:
//   mode 1 (soft): ideal processors only: the main thread on the first logical processor of its own core, every other thread of the
//                  process with an ideal processor on that core moved to the same logical index of the next core (the scheduler may
//                  still use any idle processor);
//   mode 2 (hard): mode 1 plus affinity: the main thread runs only on its core; every other thread keeps its own affinity minus that core
//                  (a thread pinned to that core alone moves to the other cores).
// Every change is recorded with the thread's creation time and undone exactly when the mode goes back to 0 (a thread that exited, or a new
// thread that reused its id, is never written to). Knob build/core-isolation (text "soft" or "hard"), dev command `cores off|soft|hard`.
// A background thread re-applies the mode once a second (threads created later) and retries restores that failed.
// Measured 2026-11-20 (ENGINE-DIG-NOTES section 8): no gain on the 5600X. Kept as an opt-in measurement tool.
#include <windows.h>
#include <tlhelp32.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace d2vr::core_isolation {
inline std::atomic<int> mode{0};
// Measurement: `cores abab soft|hard <ms>` alternates off and that mode every <ms> on the background thread and logs each switch
// (steady_clock ns, the clock of xr-frame-timing.csv start_cpu_ns, taken once the switch is in effect) in the status, so one run gives
// many interleaved A/B pairs.
inline std::atomic<int> abab_mode{0},abab_period_ms{1000};
inline std::atomic<DWORD> main_tid{};
struct State {
    std::mutex m;
    std::vector<KAFFINITY> cores;           // logical-processor mask of each physical core (empty until a valid topology is read)
    KAFFINITY all{};
    int main_core=-1;
    struct Saved {std::uint64_t created{};KAFFINITY affinity{};bool affinity_set{};PROCESSOR_NUMBER ideal{};bool ideal_set{};};
    std::map<DWORD,Saved> saved;            // the first value we replaced, per thread (and that thread's creation time)
    int applied=0;
    std::uint64_t passes{},changes{},failures{},threads{};
    std::vector<std::pair<std::int64_t,int>> switches;  // the last 512 measurement switches (time, mode in effect)
};
inline State& state() {static State s;return s;}

inline bool topology(State& s) {
    if(!s.cores.empty()) return true;
    DWORD bytes=0;GetLogicalProcessorInformation(nullptr,&bytes);
    if(!bytes) return false;
    std::vector<SYSTEM_LOGICAL_PROCESSOR_INFORMATION> info(bytes/sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION));
    if(!GetLogicalProcessorInformation(info.data(),&bytes)) return false;
    std::vector<KAFFINITY> cores;
    for(const auto& i:info) if(i.Relationship==RelationProcessorCore) cores.push_back(i.ProcessorMask);
    if(cores.size()<2) return false;        // nothing to isolate (checked before anything is cached)
    std::sort(cores.begin(),cores.end());
    DWORD_PTR process{},system{};
    if(!GetProcessAffinityMask(GetCurrentProcess(),&process,&system) || !process) return false;
    s.cores=std::move(cores);s.all=process;
    // the main thread's core: core 1 when there are at least four (core 0 takes most interrupts), else the last
    s.main_core=s.cores.size()>=4?1:static_cast<int>(s.cores.size())-1;
    return true;
}
inline int lowest(KAFFINITY mask) noexcept {for(int i=0;i<64;++i) if(mask&(KAFFINITY{1}<<i)) return i;return -1;}
inline int core_of(const State& s,int processor) noexcept {
    for(std::size_t c=0;c<s.cores.size();++c) if(s.cores[c]&(KAFFINITY{1}<<processor)) return static_cast<int>(c);
    return -1;
}
inline std::uint64_t created_of(HANDLE h) noexcept {
    FILETIME created{},exited{},kernel{},user{};
    if(!GetThreadTimes(h,&created,&exited,&kernel,&user)) return 0;
    return (static_cast<std::uint64_t>(created.dwHighDateTime)<<32)|created.dwLowDateTime;
}
// The thread's current affinity (there is no GetThreadAffinityMask): THREAD_BASIC_INFORMATION through NtQueryInformationThread.
inline KAFFINITY affinity_of(HANDLE h) noexcept {
    struct Basic {LONG exit_status;PVOID teb;PVOID process;PVOID thread;KAFFINITY affinity;LONG priority;LONG base_priority;};
    using QueryFn=LONG(NTAPI*)(HANDLE,ULONG,PVOID,ULONG,PULONG);
    static const auto query=reinterpret_cast<QueryFn>(reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"NtQueryInformationThread")));
    Basic b{};
    return query && query(h,0,&b,sizeof b,nullptr)>=0?b.affinity:0;
}
// Puts back what we changed on one thread. True when nothing of ours is left on it.
inline bool restore(HANDLE h,State::Saved& saved) noexcept {
    if(saved.affinity_set && SetThreadAffinityMask(h,saved.affinity)) saved.affinity_set=false;
    if(saved.ideal_set && SetThreadIdealProcessorEx(h,&saved.ideal,nullptr)) saved.ideal_set=false;
    return !saved.affinity_set && !saved.ideal_set;
}

// One pass over the process's threads. Called with the state's mutex held.
inline void pass(State& s,int m) {
    ++s.passes;
    const DWORD self=GetCurrentProcessId(),main=main_tid.load(std::memory_order_relaxed);
    if(m && (!main || !topology(s))) return;
    HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);
    if(snap==INVALID_HANDLE_VALUE) {++s.failures;return;}
    THREADENTRY32 e{};e.dwSize=sizeof e;std::set<DWORD> seen;
    const KAFFINITY main_mask=m?s.cores[static_cast<std::size_t>(s.main_core)]:0;
    for(BOOL ok=Thread32First(snap,&e);ok;ok=Thread32Next(snap,&e)) {
        if(e.th32OwnerProcessID!=self) continue;
        seen.insert(e.th32ThreadID);
        HANDLE h=OpenThread(THREAD_SET_INFORMATION|THREAD_QUERY_INFORMATION,FALSE,e.th32ThreadID);
        if(!h) continue;  // an entry of ours stays until the thread is gone or a later pass can open it
        const std::uint64_t created=created_of(h);
        auto it=s.saved.find(e.th32ThreadID);
        if(it!=s.saved.end() && it->second.created!=created) {s.saved.erase(it);it=s.saved.end();}  // a new thread reused a dead one's id
        if(m==0) {  // undo exactly what we changed
            if(it!=s.saved.end()) {if(restore(h,it->second)) s.saved.erase(it);else ++s.failures;++s.changes;}
            CloseHandle(h);continue;
        }
        if(it==s.saved.end()) it=s.saved.emplace(e.th32ThreadID,State::Saved{created}).first;
        State::Saved& saved=it->second;
        const bool is_main=e.th32ThreadID==main;
        // ideal processor
        PROCESSOR_NUMBER ideal{};
        if(GetThreadIdealProcessorEx(h,&ideal)) {
            PROCESSOR_NUMBER want=ideal;
            if(is_main) want.Number=static_cast<BYTE>(lowest(main_mask));
            else if(core_of(s,ideal.Number)==s.main_core) {  // same logical index on the next core
                const int index=ideal.Number-lowest(main_mask);
                const auto& other=s.cores[static_cast<std::size_t>((s.main_core+1)%static_cast<int>(s.cores.size()))];
                want.Number=static_cast<BYTE>(std::min(lowest(other)+std::max(index,0),63));
            }
            if(want.Number!=ideal.Number) {
                PROCESSOR_NUMBER previous{};
                if(SetThreadIdealProcessorEx(h,&want,&previous)) {if(!saved.ideal_set) {saved.ideal=previous;saved.ideal_set=true;}++s.changes;}
                else ++s.failures;
            }
        }
        // affinity (hard mode only; soft mode puts back any affinity a previous hard pass set)
        if(m==2) {
            const KAFFINITY current=affinity_of(h);  // 0: unreadable, the thread is left alone
            const KAFFINITY original=saved.affinity_set?saved.affinity:current;
            KAFFINITY want=is_main?main_mask:(original&~main_mask);
            if(!want) want=is_main?main_mask:(s.all&~main_mask);
            if(current && current!=want) {
                const KAFFINITY previous=SetThreadAffinityMask(h,want);
                if(previous) {if(!saved.affinity_set) {saved.affinity=previous;saved.affinity_set=true;}++s.changes;}
                else ++s.failures;
            }
        } else if(saved.affinity_set) {
            if(SetThreadAffinityMask(h,saved.affinity)) {saved.affinity_set=false;++s.changes;}
            else ++s.failures;
        }
        if(!saved.affinity_set && !saved.ideal_set) s.saved.erase(it);  // nothing of ours on this thread
        CloseHandle(h);
    }
    CloseHandle(snap);
    s.threads=seen.size();
    for(auto it=s.saved.begin();it!=s.saved.end();) it=seen.count(it->first)?std::next(it):s.saved.erase(it);  // threads that are gone
    s.applied=m;
}
inline void apply_now() {State& s=state();std::lock_guard<std::mutex> l(s.m);pass(s,mode.load());}
// Starts the once-a-second thread (once per process).
inline void start() {
    static std::once_flag once;
    std::call_once(once,[] {
        std::thread([] {
            for(;;) {
                const int planned=abab_mode.load();
                Sleep(planned?static_cast<DWORD>(std::clamp(abab_period_ms.load(),100,10000)):1000);
                State& s=state();
                std::lock_guard<std::mutex> l(s.m);
                const int ab=abab_mode.load();  // re-read under the lock: `cores off` may have run during the sleep
                if(ab) {
                    pass(s,s.applied==0?ab:0);
                    if(s.switches.size()>=512) s.switches.erase(s.switches.begin());
                    s.switches.emplace_back(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(),s.applied);
                } else {
                    const int m=mode.load();
                    if(m || s.applied || !s.saved.empty()) pass(s,m);
                }
            }
        }).detach();
    });
}
inline const char* name(int m) noexcept {return m==2?"hard":m==1?"soft":"off";}
inline std::string json() {
    State& s=state();std::lock_guard<std::mutex> l(s.m);std::ostringstream o;
    o<<"{\"mode\":\""<<name(mode.load())<<"\",\"applied\":\""<<name(s.applied)<<"\",\"main_tid\":"<<main_tid.load()<<",\"main_core\":"<<s.main_core
     <<",\"cores\":"<<s.cores.size()<<",\"threads\":"<<s.threads<<",\"saved\":"<<s.saved.size()<<",\"passes\":"<<s.passes<<",\"changes\":"<<s.changes
     <<",\"failures\":"<<s.failures<<",\"abab\":\""<<name(abab_mode.load())<<"\",\"abab_ms\":"<<abab_period_ms.load()<<",\"switches\":[";
    for(std::size_t i=0;i<s.switches.size();++i) o<<(i?",":"")<<"["<<s.switches[i].first<<","<<s.switches[i].second<<"]";
    o<<"]}";
    return o.str();
}
} // namespace d2vr::core_isolation
