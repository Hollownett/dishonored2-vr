#pragma once
// Diagnostic (build/frame-order-trace -> artifacts/frame-order.csv): one QPC-stamped row per pipeline stage, so the order of the
// Present, the runtime sample, the arm uploads, the view Setup, the eye begins, the stick turn and the weapon reads can be read from
// one clock. Lock-free circular ring, appended to the file every 64 Presents (a retained DLL never reaches its teardown). Kinds:
// P present, X sample, U arm upload, W skin written, G GPU skin copy, S setup (eye 0), I image joints, K weapon frames, B eye begin,
// T turn applied, R weapon joint read.
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <mutex>

namespace d2vr::frame_order {
struct Event {std::int64_t qpc{};std::uint64_t frame{};std::uint32_t tid{};char kind{};std::uint8_t a{};std::array<float,16> v{};};
inline std::array<Event,1u<<16> ring{};
inline std::atomic<std::uint64_t> next{0};
inline std::atomic<bool> on{false};
inline std::atomic<std::uint64_t> frame{0};   // the Present counter, mirrored by the Present hook
inline std::mutex file_mutex;inline std::ofstream file;inline std::uint64_t written{};
inline void note(char kind,std::uint8_t a,std::initializer_list<double> values={}) noexcept {
    if(!on.load(std::memory_order_relaxed)) return;
    auto& e=ring[next.fetch_add(1,std::memory_order_relaxed)&(ring.size()-1)];
    LARGE_INTEGER q{};QueryPerformanceCounter(&q);
    e.qpc=q.QuadPart;e.frame=frame.load(std::memory_order_relaxed);e.tid=GetCurrentThreadId();e.kind=kind;e.a=a;e.v={};
    unsigned k=0;for(const double x:values) if(k<e.v.size()) e.v[k++]=static_cast<float>(x);
}
inline void start(const std::filesystem::path& path) {
    std::lock_guard<std::mutex> lock(file_mutex);
    file.close();file.open(path,std::ios::trunc);if(!file) return;
    file.precision(9);file<<"qpc_us,frame,tid,kind,a";for(unsigned k=0;k<16;++k) file<<",v"<<k;file<<'\n';
    next=0;written=0;on=true;
}
// Writes the rows recorded since the last flush, but the newest 256 (a writer may still be filling them); final: all.
inline void flush(bool final=false) noexcept {
    try {
        std::lock_guard<std::mutex> lock(file_mutex);
        if(!file.is_open()) return;
        LARGE_INTEGER f{};QueryPerformanceFrequency(&f);
        const auto n=next.load();const auto end=final?n:(n>256?n-256:0);
        if(end>written+ring.size()) written=end-ring.size();   // overrun: the oldest rows are gone
        for(;written<end;++written) {
            const auto& e=ring[written&(ring.size()-1)];
            file<<static_cast<double>(e.qpc)*1e6/static_cast<double>(f.QuadPart)<<','<<e.frame<<','<<e.tid<<','<<e.kind<<','<<unsigned(e.a);
            for(const float x:e.v) file<<','<<x;
            file<<'\n';
        }
        file.flush();
    } catch(...) {}
}
inline void stop() {if(!on.exchange(false)) return;flush(true);std::lock_guard<std::mutex> lock(file_mutex);file.close();}
} // namespace d2vr::frame_order
