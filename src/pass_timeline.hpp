#pragma once
// v1433 pass timeline (knob build/pass-timeline, needs build/overlap-probe for the frame markers): every engine job item the executor
// exe 2c5d50 runs (render passes and the game side's jobs) as [start, end] on its thread, plus the main thread's render-graph submits
// (exe 2c8310), graph waits (exe 2c8440), render sides (exe 458c0) and game sides (exe 459a0), for `frames_to_record` gameplay frames after
// `skip_frames` gameplay frames. Written to pass-timeline.csv beside the status files when the recording ends. Measurement only: no
// behaviour change; off, the executor hook pays one relaxed load. Purpose: find which passes form each eye graph's serial chain
// (docs/research/ENGINE-PARALLEL-EYES.md section 10).
#include <windows.h>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <thread>

namespace d2vr::pass_timeline {
struct Rec {std::int64_t t0,t1;std::uintptr_t view;std::uint32_t tid;std::int16_t pass;std::uint16_t depth;};
// pass ids >= 0 are render_pass_table.hpp ids, -1 an item of no render pass; markers (main thread):
enum Marker : std::int16_t {graph_submit=-2,graph_wait=-3,render_side=-4,game_side=-5};
inline constexpr std::size_t capacity=std::size_t{1}<<19;  // 16 MB of records, ~2,000 frames
inline std::atomic<bool> armed{false},recording{false},written{false};
inline std::atomic<std::uint64_t> next{0};
inline std::unique_ptr<Rec[]> buffer;
inline std::filesystem::path out_path;
inline unsigned skip_frames=400,frames_to_record=1000,gameplay_frames=0;
inline DWORD main_tid=0;
inline thread_local std::uint16_t depth=0;
inline std::int64_t now() noexcept {return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}

inline void configure(bool on,std::filesystem::path path) {
    if(!on || armed.load()) return;
    try {buffer.reset(new Rec[capacity]);} catch(...) {return;}
    out_path=std::move(path);armed.store(true);
}
inline void add(int pass,const void* view,std::int64_t t0,std::int64_t t1,std::uint16_t d) noexcept {
    if(!recording.load(std::memory_order_relaxed)) return;
    const auto i=next.fetch_add(1,std::memory_order_relaxed);
    if(i>=capacity) {recording.store(false,std::memory_order_relaxed);return;}
    buffer[i]=Rec{t0,t1,reinterpret_cast<std::uintptr_t>(view),GetCurrentThreadId(),static_cast<std::int16_t>(pass),d};
}
inline void write_file() noexcept {
    // Writers that took an index just before the stop may still be filling their slot: wait them out.
    Sleep(200);
    const auto n=static_cast<std::size_t>(std::min<std::uint64_t>(next.load(),capacity));
    FILE* f=nullptr;
    if(_wfopen_s(&f,out_path.c_str(),L"w") || !f) return;
    std::fprintf(f,"# main_tid %lu records %zu frames %u\nt0,t1,tid,pass,depth,view\n",static_cast<unsigned long>(main_tid),n,frames_to_record);
    for(std::size_t i=0;i<n;++i) {
        const Rec& r=buffer[i];
        std::fprintf(f,"%lld,%lld,%u,%d,%u,%llx\n",static_cast<long long>(r.t0),static_cast<long long>(r.t1),r.tid,r.pass,r.depth,
            static_cast<unsigned long long>(r.view));
    }
    std::fclose(f);
}
// Main thread, at each render side's start: `gameplay` = a real game side ran before it (the overlap probe's G >= 1 ms rule).
inline void frame(bool gameplay) noexcept {
    if(!armed.load(std::memory_order_relaxed) || written.load(std::memory_order_relaxed) || !gameplay) return;
    ++gameplay_frames;
    if(gameplay_frames==skip_frames) {main_tid=GetCurrentThreadId();recording.store(true);}
    else if(gameplay_frames==skip_frames+frames_to_record || (gameplay_frames>skip_frames && !recording.load(std::memory_order_relaxed))) {
        recording.store(false);written.store(true);
        try {std::thread(write_file).detach();} catch(...) {}
    }
}
} // namespace d2vr::pass_timeline
