// v1444: job_wake idle points (the SteamVR spacewarp frame from the main thread's waits). Build and run: build\night\lab.cmd job_wake_idle_check
#include "job_wake.hpp"
#include <chrono>
#include <cstdio>
#include <thread>

namespace jw=d2vr::job_wake;
static std::int64_t due_ns=jw::idle_none;  // steady ns, or idle_none
static int runs=0;static std::int64_t ran_at=0;
static std::int64_t steady_ns() noexcept {return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
static std::int64_t left() noexcept {return due_ns==jw::idle_none?jw::idle_none:due_ns-steady_ns();}
static void work() noexcept {++runs;ran_at=steady_ns();due_ns=jw::idle_none;}  // as GameXr: submitting clears the pending frame
static int failures=0;
static void check(bool ok,const char* what) {if(!ok) {++failures;std::printf("FAIL %s\n",what);} else std::printf("ok   %s\n",what);}
static void signal_after(HANDLE h,int ms,bool semaphore=false) {
    std::thread([h,ms,semaphore] {std::this_thread::sleep_for(std::chrono::milliseconds(ms));if(semaphore) ReleaseSemaphore(h,1,nullptr);else SetEvent(h);}).detach();
}

int main() {
    LARGE_INTEGER f{};QueryPerformanceFrequency(&f);jw::ticks_per_second=f.QuadPart;
    jw::idle_left.store(&left);jw::idle_work.store(&work);
    const HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);  // auto-reset, as the engine's job events
    {   // nothing pending: the plain wait
        due_ns=jw::idle_none;runs=0;DWORD timeout=INFINITE,result=0xffff;
        check(!jw::idle_wait(event,timeout,result) && runs==0 && timeout==INFINITE,"nothing pending leaves the wait alone");
    }
    {   // due now: runs before the wait
        due_ns=steady_ns()-1'000'000;runs=0;DWORD timeout=INFINITE,result=0xffff;
        check(!jw::idle_wait(event,timeout,result) && runs==1,"work already due runs at once");
    }
    {   // the wait ends before the work is due: untouched
        due_ns=steady_ns()+50'000'000;runs=0;DWORD timeout=5,result=0xffff;
        check(!jw::idle_wait(event,timeout,result) && runs==0 && timeout==5,"a short wait is not stretched to the due time");
    }
    {   // the object comes first: taken exactly once, the work stays pending
        due_ns=steady_ns()+80'000'000;runs=0;DWORD timeout=INFINITE,result=0xffff;
        const auto t0=steady_ns();signal_after(event,10);
        const bool waited=jw::idle_wait(event,timeout,result);
        const auto ms=(steady_ns()-t0)/1'000'000;
        check(waited && result==WAIT_OBJECT_0 && runs==0,"the signal before the due time returns WAIT_OBJECT_0 without the work");
        check(ms>=5 && ms<40,"... after the signal, not the due time");
        check(WaitForSingleObject(event,0)==WAIT_TIMEOUT,"... and the auto-reset event was taken (reset), as WaitForSingleObject takes it");
    }
    {   // the due time comes first: the work at its time, then the caller waits on
        const auto t0=steady_ns();due_ns=t0+20'000'000;runs=0;DWORD timeout=INFINITE,result=0xffff;
        const bool waited=jw::idle_wait(event,timeout,result);
        const auto at_ms=(ran_at-t0)/1'000'000;
        check(!waited && runs==1 && timeout==INFINITE,"the due time first runs the work and hands the wait back");
        check(at_ms>=19 && at_ms<35,"... at the due time (20 ms)");
        std::printf("     work ran %.2f ms after the start (due 20 ms)\n",(ran_at-t0)/1e6);
    }
    {   // a finite timeout is reduced by the time spent before the work
        const auto t0=steady_ns();due_ns=t0+20'000'000;runs=0;DWORD timeout=100,result=0xffff;
        const bool waited=jw::idle_wait(event,timeout,result);
        check(!waited && runs==1 && timeout>=70 && timeout<=81,"the remaining timeout after the work is about 80 ms");
        (void)t0;
    }
    {   // a semaphore: one count taken, the work stays pending
        const HANDLE sem=CreateSemaphoreW(nullptr,1,4,nullptr);
        due_ns=steady_ns()+30'000'000;runs=0;DWORD timeout=INFINITE,result=0xffff;
        check(jw::idle_wait(sem,timeout,result) && result==WAIT_OBJECT_0 && runs==0,"a semaphore with a count is taken at once");
        LONG previous=-1;ReleaseSemaphore(sem,1,&previous);
        check(previous==0,"... exactly one count was taken");
        CloseHandle(sem);
    }
    {   // a whole timeout that passes before the due time cannot happen (short waits are left alone); a timeout after the work ends as WAIT_TIMEOUT in the caller
        due_ns=jw::idle_none;
    }
    {   // an invalid handle: the plain wait decides
        due_ns=steady_ns()+10'000'000;runs=0;DWORD timeout=INFINITE,result=0xffff;
        check(!jw::idle_wait(reinterpret_cast<HANDLE>(0x7ff0),timeout,result) && runs==0,"WAIT_FAILED falls back to the plain wait");
    }
    CloseHandle(event);
    std::printf(failures?"FAILED %d\n":"all passed\n",failures);
    return failures?1:0;
}
