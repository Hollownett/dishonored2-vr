#pragma once
// v1408 (docs/PERF-PLAN-90HZ-2026-11-19.md 2B.4 step 1; user 2026-11-19: "lets try Overlap the two halves"), opt-in build/async-present.
//
// The engine's main loop (exe:450d0) runs the game side (exe:459a0) and then the render side (exe:458c0) strictly in sequence on one
// thread. The render side ends in IDXGISwapChain::Present: the mod submits the XR frame there and then presents the desktop window
// through the Steam overlay's hook, DXGI and the driver (~0.7 ms of main-thread frame time per frame in the 2026-11-19 sim).
// With the knob, during headset gameplay that desktop Present runs on this worker thread and the main thread returns at once, so it
// overlaps the next frame's pre-game steps and game side; the desktop window still gets every frame.
//
// Why the D3D11 immediate context stays single-threaded: 2026-11-19 stack walks of the main thread (tools/stack_tree.py, 3,152 walks):
// 0 of 808 walks inside the game side and 0 of 29 in the main loop between the halves pass through d3d11/dxgi/the driver. The main
// thread waits for the worker when the game side returns (the hook on exe:459a0), at the next Present, in ResizeBuffers and at teardown;
// as a safety net the mod's own immediate-context hooks (draws, viewports, render targets) wait too and count it (guard_waits: expected
// 0). The wait pumps sent window messages, so a Present that sends to the game window cannot deadlock against it.
// Pure apart from the Win32 thread/event calls; the lab is tests/async_present_check.cpp.
#include <windows.h>
#include <dxgi.h>
#include <atomic>
#include <cstdint>
#include <sstream>
#include <string>
#include <thread>

namespace d2vr {
class AsyncPresent {
public:
    using PresentFn=HRESULT (WINAPI*)(IDXGISwapChain*,UINT,UINT);
    static AsyncPresent& instance() noexcept {static AsyncPresent a;return a;}
    // Once per session, from the Present hook's thread.
    bool start(PresentFn present) noexcept {
        if(running_.load(std::memory_order_acquire)) return true;
        if(!present) return false;
        present_=present;
        kick_=CreateEventW(nullptr,FALSE,FALSE,nullptr);done_=CreateEventW(nullptr,TRUE,TRUE,nullptr);
        if(!kick_ || !done_) {close();reason_="CreateEvent";return false;}
        stop_.store(false,std::memory_order_release);
        try {worker_=std::thread([this] {run();});} catch(...) {close();reason_="thread";return false;}
        running_.store(true,std::memory_order_release);reason_="none";
        return true;
    }
    bool running() const noexcept {return running_.load(std::memory_order_acquire);}
    bool in_flight() const noexcept {return in_flight_.load(std::memory_order_acquire);}
    // Main thread, inside the Present hook: hand this Present to the worker. Returns the previous asynchronous result (a Present
    // error therefore reaches the game one frame late; DXGI_ERROR_WAS_STILL_DRAWING of a paced mirror is success, as in the sync path).
    HRESULT kick(IDXGISwapChain* chain,UINT interval,UINT flags,bool paced) noexcept {
        if(!running() || !chain) return present_?present_(chain,interval,flags):E_FAIL;
        wait();
        chain->AddRef();
        chain_=chain;interval_=interval;flags_=flags;paced_=paced;
        ResetEvent(done_);in_flight_.store(true,std::memory_order_release);++kicks_;
        SetEvent(kick_);
        return last_.load(std::memory_order_acquire);
    }
    // Any thread that is about to use the immediate context, the swap chain or the window: returns once no Present is in flight.
    // `guard` marks the safety-net callers (the mod's immediate-context hooks).
    void wait(bool guard=false) noexcept {
        if(!in_flight_.load(std::memory_order_acquire)) return;
        if(GetCurrentThreadId()==worker_id_.load(std::memory_order_acquire)) return; // never from the worker itself
        const auto t0=now_ns();
        for(;;) {
            const DWORD r=MsgWaitForMultipleObjectsEx(1,&done_,INFINITE,QS_SENDMESSAGE,0);
            if(r==WAIT_OBJECT_0) break;
            if(r==WAIT_OBJECT_0+1) {MSG m;PeekMessageW(&m,nullptr,0,0,PM_NOREMOVE|PM_QS_SENDMESSAGE);continue;} // dispatch sent messages only
            WaitForSingleObject(done_,INFINITE);break; // failed wait: plain wait
        }
        const auto waited=now_ns()-t0;
        waited_ns_.fetch_add(waited,std::memory_order_relaxed);++waits_;
        if(waited>max_wait_ns_.load(std::memory_order_relaxed)) max_wait_ns_.store(waited,std::memory_order_relaxed);
        if(guard) guard_waits_.fetch_add(1,std::memory_order_relaxed);
    }
    // Teardown (session end): finish the Present in flight and stop the worker.
    void stop() noexcept {
        if(!running()) return;
        wait();stop_.store(true,std::memory_order_release);SetEvent(kick_);
        if(worker_.joinable()) worker_.join();
        running_.store(false,std::memory_order_release);close();
    }
    std::string json() const {
        std::ostringstream o;
        const auto w=waits_.load();
        o<<"{\"running\":"<<(running()?"true":"false")<<",\"reason\":\""<<reason_<<"\",\"kicks\":"<<kicks_.load()<<",\"waits\":"<<w
         <<",\"waited_ms_mean\":"<<(w?static_cast<double>(waited_ns_.load())/w/1e6:0.)<<",\"waited_ms_max\":"<<max_wait_ns_.load()/1e6
         <<",\"present_ms_mean\":"<<(kicks_.load()?static_cast<double>(present_ns_.load())/kicks_.load()/1e6:0.)
         <<",\"guard_waits\":"<<guard_waits_.load()<<",\"still_drawing\":"<<still_drawing_.load()<<",\"errors\":"<<errors_.load()<<'}';
        return o.str();
    }
private:
    static std::uint64_t now_ns() noexcept {
        static const double scale=[] {LARGE_INTEGER f;QueryPerformanceFrequency(&f);return 1e9/static_cast<double>(f.QuadPart);}();
        LARGE_INTEGER c;QueryPerformanceCounter(&c);return static_cast<std::uint64_t>(static_cast<double>(c.QuadPart)*scale);
    }
    void run() noexcept {
        worker_id_.store(GetCurrentThreadId(),std::memory_order_release);
        for(;;) {
            WaitForSingleObject(kick_,INFINITE);
            if(stop_.load(std::memory_order_acquire)) break;
            const auto t0=now_ns();
            HRESULT r=present_(chain_,interval_,flags_);
            present_ns_.fetch_add(now_ns()-t0,std::memory_order_relaxed);
            if(paced_ && r==DXGI_ERROR_WAS_STILL_DRAWING) {r=S_OK;++still_drawing_;}
            if(FAILED(r)) ++errors_;
            last_.store(r,std::memory_order_release);
            chain_->Release();chain_=nullptr;
            in_flight_.store(false,std::memory_order_release);
            SetEvent(done_);
        }
        worker_id_.store(0,std::memory_order_release);
    }
    void close() noexcept {
        if(kick_) CloseHandle(kick_);
        if(done_) CloseHandle(done_);
        kick_=done_=nullptr;
    }
    PresentFn present_{};HANDLE kick_{},done_{};std::thread worker_;
    IDXGISwapChain* chain_{};UINT interval_{},flags_{};bool paced_{};
    std::atomic<bool> running_{false},stop_{false},in_flight_{false};std::atomic<DWORD> worker_id_{0};
    std::atomic<HRESULT> last_{S_OK};const char* reason_="off";
    std::atomic<std::uint64_t> kicks_{},waits_{},waited_ns_{},max_wait_ns_{},present_ns_{},guard_waits_{},still_drawing_{},errors_{};
};
} // namespace d2vr
