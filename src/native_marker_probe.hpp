#pragma once
#include "native_readable_range.hpp"
#include <windows.h>
#include <intrin.h>
// Relative so a bare `cl tests\..._check.cpp` with no /I still resolves; the
// repository build passes third_party\minhook\include as well.
#include "../third_party/minhook/include/MinHook.h"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>

namespace d2vr {
// Read-only MinHook observer of the two functions that place HUD markers, plus
// the eye phase the render path publishes. It writes nothing into the game, no
// argument is modified and the originals run unchanged.
//
//  0x677B10 arkEntityMarker::Update, void(marker*). The pinned executable calls
//           it from exactly one site, 0x53AAE9 inside arkEntityMarkerManager's
//           sortMarkersByDist update, once per live marker per update pass; that
//           caller loads xmm0 from memory right after the call and no caller
//           tests a result register, so the update is a void function.
//  0x6DDA50 idView::WorldToScreen, void(view, const float3* in, float* out).
//           Three call sites, 0x676073, 0x676459 and 0x677D7F, all in the marker
//           code, and none of them reads a return register: also void.
//
// What it answers: whether marker updates run once per frame or once per eye
// pass. The render path publishes the phase it is in through eye_phase() (1 at
// left-eye begin, 2 at right-eye begin, 3 after present), every call records the
// phase that was live when it ran, and summary() reports calls and phase
// publications per phase and calls per thread. A phase with more calls than
// publications is a pass that updates markers more than once; publications with
// no calls are passes the markers never saw.
//
// Every call only reads GetTickCount64/GetCurrentThreadId and copies a 64-byte
// row into an in-memory ring (1024 rows) under a bounded spinlock: no
// allocation, no I/O, no engine memory read, nothing that can fault. The ring,
// the per-phase counts and the per-thread counts are dumped at disable().
//
// The expected prologues were extracted from the pinned executable (sha256
// b671f8249422862bf557d00aab2f4bb671ed4515cc10ad8a8a9decad84516180,
// artifacts/re-toolchain/direct-ghidra/Dishonored2-steam17576867.exe) by mapping
// each RVA through its own PE section table (.text: RVA 0x1000 -> file 0x400,
// so 0x677B10 lies at file 0x676F10 and 0x6DDA50 at file 0x6DCE50), and
// install() proves the same bytes are live and executable before it hooks.
class NativeMarkerProbe {
public:
    using AfterUpdate=void (*)(std::uintptr_t caller_rva,std::uintptr_t marker) noexcept;
    inline static std::atomic<AfterUpdate> after_update{};
    inline static constexpr std::size_t ring_slots=1024,phase_count=4,thread_slots=16;
    // 0 = no phase published yet, 1 = left eye, 2 = right eye, 3 = after
    // present. The integrator's eye_phase() values are kept verbatim.
    struct Row {
        std::uint64_t call{},tick{};std::uint32_t thread{},phase{},site{};
        std::uintptr_t marker{},view{},point{},out{};
        std::uint32_t entry_phase{},read_flags{};
        std::array<float,3> world{},screen{};
        std::array<float,16> matrix{};
        std::int32_t cache_before{},cache_after{};
    };
    static const char* site_name(unsigned site) noexcept {return site?"world_to_screen":"update";}
    static std::string header() {
        std::string text="call,tick_ms,thread,phase,site,marker,view,point,out,entry_phase,read_flags,world_x,world_y,world_z,screen_x,screen_y,screen_z,cache_before,cache_after";
        for(unsigned i=0;i<16;++i) text+=",vp"+std::to_string(i);
        return text+"\n";
    }
private:
    using Update=void (__fastcall*)(void*);
    using Project=void (__fastcall*)(void*,const void*,void*);
    using Read=bool (*)(std::uintptr_t,void*,std::size_t);
    struct ThreadCount {DWORD thread{};std::uint64_t count{};};
    inline static NativeMarkerProbe* instance_{};
    inline static constexpr std::uintptr_t update_rva=0x677b10,project_rva=0x6dda50;
    inline static constexpr std::array<std::uintptr_t,2> rvas{update_rva,project_rva};
    // 0x677B10: push rbx; sub rsp,0x20; cmp byte [rcx+0x96],0; mov rbx,rcx; jne.
    inline static constexpr std::array<std::byte,32> update_expected{{
        std::byte{0x40},std::byte{0x53},std::byte{0x48},std::byte{0x83},std::byte{0xec},std::byte{0x20},std::byte{0x80},std::byte{0xb9},
        std::byte{0x96},std::byte{0x00},std::byte{0x00},std::byte{0x00},std::byte{0x00},std::byte{0x48},std::byte{0x8b},std::byte{0xd9},
        std::byte{0x0f},std::byte{0x85},std::byte{0xe7},std::byte{0x00},std::byte{0x00},std::byte{0x00},std::byte{0x8b},std::byte{0x49},
        std::byte{0x0c},std::byte{0xe8},std::byte{0x32},std::byte{0x4a},std::byte{0x06},std::byte{0x00},std::byte{0x48},std::byte{0x85}
    }};
    // 0x6DDA50: push rbx; sub rsp,0x30; mov rbx,r8; lea r8,[rsp+0x20]; call.
    inline static constexpr std::array<std::byte,32> project_expected{{
        std::byte{0x40},std::byte{0x53},std::byte{0x48},std::byte{0x83},std::byte{0xec},std::byte{0x30},std::byte{0x49},std::byte{0x8b},
        std::byte{0xd8},std::byte{0x4c},std::byte{0x8d},std::byte{0x44},std::byte{0x24},std::byte{0x20},std::byte{0xe8},std::byte{0x0d},
        std::byte{0xff},std::byte{0x1a},std::byte{0x00},std::byte{0x48},std::byte{0x8b},std::byte{0x0d},std::byte{0x26},std::byte{0x78},
        std::byte{0x2c},std::byte{0x03},std::byte{0xf3},std::byte{0x0f},std::byte{0x10},std::byte{0x44},std::byte{0x24},std::byte{0x20}
    }};
    Update original_update_{};Project original_project_{};Read read_{};
    std::uintptr_t base_{};std::filesystem::path file_;
    std::ofstream log_;
    std::array<bool,2> created_{},enabled_{};
    std::atomic<bool> active_{false};
    std::atomic<UINT> depth_{};
    std::array<std::atomic<std::uint64_t>,2> calls_{};
    std::array<std::atomic<std::uint64_t>,phase_count> phase_calls_{},phase_publishes_{};
    std::atomic<std::uint32_t> phase_{};
    std::atomic<std::uint64_t> published_{},pushed_{},busy_{},dropped_{},thread_overflow_{};
    std::array<Row,ring_slots> ring_{};
    std::array<ThreadCount,thread_slots> threads_{};
    std::size_t thread_count_{};
    mutable std::atomic_flag lock_=ATOMIC_FLAG_INIT;

    static bool executable(std::uintptr_t at) {
        return native_executable_cached(at);
    }

    // The ring and the thread table share one bounded spin: the critical section
    // is a 64-byte copy and a short linear scan, and a contended lock drops that
    // one row rather than blocking a marker update.
    bool enter() const noexcept {
        for(unsigned spin=0;spin<4096;++spin) {
            if(!lock_.test_and_set(std::memory_order_acquire)) return true;
            YieldProcessor();
        }
        return false;
    }
    void leave() const noexcept {lock_.clear(std::memory_order_release);}

    void store(const Row& row) noexcept {
        if(!enter()) {busy_.fetch_add(1,std::memory_order_relaxed);return;}
        bool known=false;
        for(std::size_t index=0;index<thread_count_;++index)
            if(threads_[index].thread==row.thread) {++threads_[index].count;known=true;break;}
        if(!known) {
            if(thread_count_<thread_slots) threads_[thread_count_++]={row.thread,1};
            else thread_overflow_.fetch_add(1,std::memory_order_relaxed);
        }
        ring_[static_cast<std::size_t>((row.call-1)%ring_slots)]=row;
        leave();
    }
    // One row per call, in call order: the phase is read once, at the call, so a
    // publication racing this call can only be one row ambiguous.
    void record(unsigned site,Row row) noexcept {
        row.site=site;row.phase=phase_.load(std::memory_order_relaxed);
        row.tick=GetTickCount64();row.thread=GetCurrentThreadId();
        row.call=pushed_.fetch_add(1,std::memory_order_relaxed)+1;
        calls_[site].fetch_add(1,std::memory_order_relaxed);
        phase_calls_[row.phase].fetch_add(1,std::memory_order_relaxed);
        store(row);
    }
    static void __fastcall update(void* marker) {
        auto& self=*instance_;
        self.depth_.fetch_add(1,std::memory_order_acq_rel);
        Row row;row.marker=reinterpret_cast<std::uintptr_t>(marker);
        const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
        __try {self.original_update_(marker);self.record(0,row);
            if(self.active_.load()) if(const auto observe=after_update.load()) observe(caller>=self.base_?caller-self.base_:0,row.marker);}
        __finally {self.depth_.fetch_sub(1,std::memory_order_acq_rel);}
    }
    static void __fastcall project(void* view,const void* point,void* out) {
        auto& self=*instance_;
        self.depth_.fetch_add(1,std::memory_order_acq_rel);
        Row row;row.view=reinterpret_cast<std::uintptr_t>(view);
        row.point=reinterpret_cast<std::uintptr_t>(point);row.out=reinterpret_cast<std::uintptr_t>(out);
        row.entry_phase=self.phase_.load(std::memory_order_relaxed);
        // Verified ABI: W2S(view,const float3*,float3*). 88D970 uses the matrix
        // at view+3EB0; 890DD0 compares/stores its frame cache token at +3EF0.
        // Read input before the call (input/output may alias); read resulting
        // matrix/token after the original's lazy cache update. No native calls
        // are added and neither the input, output nor cache is modified.
        __try {
            if(point && self.read_(row.point,row.world.data(),sizeof(row.world))) row.read_flags|=1;
            if(view && self.read_(row.view+0x3ef0,&row.cache_before,sizeof(row.cache_before))) row.read_flags|=8;
            self.original_project_(view,point,out);
            if(out && self.read_(row.out,row.screen.data(),sizeof(row.screen))) row.read_flags|=2;
            if(view && self.read_(row.view+0x3eb0,row.matrix.data(),sizeof(row.matrix))) row.read_flags|=4;
            if(view && self.read_(row.view+0x3ef0,&row.cache_after,sizeof(row.cache_after))) row.read_flags|=16;
            self.record(1,row);
        }
        __finally {self.depth_.fetch_sub(1,std::memory_order_acq_rel);}
    }
    static std::string hex_text(std::uintptr_t value) {
        std::ostringstream out;out<<"0x"<<std::hex<<value;return out.str();
    }
public:
    // Called from the render path: 1 at left-eye begin, 2 at right-eye begin, 3
    // after present. Monotonic in the sense the engine is: the phase advances
    // through a frame and wraps to 1 on the next. Anything else counts as 0.
    void eye_phase(std::uint32_t phase) noexcept {
        if(phase>=phase_count) phase=0;
        phase_.store(phase,std::memory_order_release);
        phase_publishes_[phase].fetch_add(1,std::memory_order_relaxed);
        published_.fetch_add(1,std::memory_order_relaxed);
    }
    // Verifies both prologues against the pinned executable and enables the
    // detours it can prove: a mismatch retires only that one function, so an exe
    // that has one of the two still yields data. Throws when neither matches.
    void install(std::uintptr_t base,Read read,const std::filesystem::path& file) {
        if(!base || !read || active_.load(std::memory_order_acquire) || (instance_ && instance_!=this))
            throw std::runtime_error("Invalid native marker probe ownership");
        base_=base;read_=read;file_=file;
        const std::array<const std::array<std::byte,32>*,2> expected{&update_expected,&project_expected};
        std::array<bool,2> matched{};
        unsigned matches=0;
        for(std::size_t index=0;index<rvas.size();++index) {
            std::array<std::byte,32> code{};
            if(read_(base_+rvas[index],code.data(),code.size()) && code==*expected[index] && executable(base_+rvas[index])) {
                matched[index]=true;++matches;
            }
        }
        if(!matches) throw std::runtime_error("Native marker probe code differs from the pinned executable");
        if(!file_.empty()) {log_.open(file_,std::ios::trunc);   // v1046: an empty path (diagnostics off) is not an error; the hooks still publish
            if(!log_) throw std::runtime_error("Cannot open native marker probe log");
            log_<<std::setprecision(9)<<header();}
        calls_[0]=0;calls_[1]=0;phase_.store(0,std::memory_order_relaxed);
        for(auto& counter:phase_calls_) counter=0;
        for(auto& counter:phase_publishes_) counter=0;
        published_=0;pushed_=0;busy_=0;dropped_=0;thread_overflow_=0;depth_=0;
        thread_count_=0;created_={};enabled_={};
        instance_=this;
        const std::array<void*,2> bodies{reinterpret_cast<void*>(&update),reinterpret_cast<void*>(&project)};
        const std::array<void**,2> originals{reinterpret_cast<void**>(&original_update_),reinterpret_cast<void**>(&original_project_)};
        for(std::size_t index=0;index<rvas.size();++index) {
            if(!matched[index]) continue;
            const auto target=reinterpret_cast<void*>(base_+rvas[index]);
            if(!*originals[index] && MH_CreateHook(target,bodies[index],originals[index])!=MH_OK) continue;
            created_[index]=true;
            if(MH_EnableHook(target)!=MH_OK) continue;
            enabled_[index]=true;
        }
        if(!hooks()) {
            if(created_[0]) MH_DisableHook(reinterpret_cast<void*>(base_+rvas[0]));
            if(created_[1]) MH_DisableHook(reinterpret_cast<void*>(base_+rvas[1]));
            log_.close();
            throw std::runtime_error("Native marker probe hooked nothing");
        }
        active_.store(true,std::memory_order_release);
    }
    // Disengages what was enabled, waits out any in-flight call, then writes the
    // ring and the counts into the install path. True means every enabled
    // prologue is the pinned one again.
    bool disable() noexcept {
        after_update=nullptr;
        active_.store(false,std::memory_order_release);
        if(!created_[0] && !created_[1]) return true;
        bool restored=true;
        for(std::size_t index=0;index<rvas.size();++index) if(enabled_[index]) {
            const auto status=MH_DisableHook(reinterpret_cast<void*>(base_+rvas[index]));
            if(status==MH_OK || status==MH_ERROR_DISABLED) enabled_[index]=false;else restored=false;
        }
        const auto deadline=GetTickCount64()+5000;
        while(depth_.load(std::memory_order_acquire) && GetTickCount64()<deadline) Sleep(1);
        if(depth_.load(std::memory_order_acquire)) return false;
        try {
            const std::array<const std::array<std::byte,32>*,2> expected{&update_expected,&project_expected};
            for(std::size_t index=0;index<rvas.size();++index) if(created_[index]) {
                std::array<std::byte,32> code{};
                restored=restored && read_(base_+rvas[index],code.data(),code.size()) && code==*expected[index];
            }
            if(log_.is_open()) log_<<rows();
            log_.close();
        } catch(...) {return false;}
        return restored;
    }
    // The ring, oldest first, capped at ring_slots calls: tick, thread and eye
    // phase per call, plus the pointers each call was handed.
    std::string rows() const {
        std::ostringstream out;out<<std::setprecision(9);
        const auto count=pushed_.load(std::memory_order_relaxed);
        const auto first=count>ring_slots?count-ring_slots:0;
        for(std::uint64_t index=first;index<count;++index) {
            const auto& row=ring_[static_cast<std::size_t>(index%ring_slots)];
            out<<row.call<<','<<row.tick<<','<<row.thread<<','<<row.phase<<','<<site_name(row.site)<<','
               <<hex_text(row.marker)<<','<<hex_text(row.view)<<','<<hex_text(row.point)<<','<<hex_text(row.out)
               <<','<<row.entry_phase<<','<<row.read_flags;
            for(auto value:row.world) out<<','<<value;
            for(auto value:row.screen) out<<','<<value;
            out<<','<<row.cache_before<<','<<row.cache_after;
            for(auto value:row.matrix) out<<','<<value;
            out<<'\n';
        }
        return out.str();
    }
    // Counters plus one line per phase (calls and publications) and one line per
    // observed thread, which is what says whether a pass updates markers once.
    std::string summary() const {
        std::ostringstream out;
        out<<"native marker probe update="<<calls_[0].load(std::memory_order_relaxed)
           <<" world_to_screen="<<calls_[1].load(std::memory_order_relaxed)
           <<" phase="<<phase_.load(std::memory_order_relaxed)
           <<" publishes="<<published_.load(std::memory_order_relaxed)
           <<" ring="<<pushed_.load(std::memory_order_relaxed)<<" busy="<<busy_.load(std::memory_order_relaxed)
           <<" dropped="<<dropped_.load(std::memory_order_relaxed)
           <<" thread_overflow="<<thread_overflow_.load(std::memory_order_relaxed)<<'\n';
        for(std::uint32_t index=0;index<phase_count;++index)
            out<<"phase="<<index<<" calls="<<phase_calls_[index].load(std::memory_order_relaxed)
               <<" publishes="<<phase_publishes_[index].load(std::memory_order_relaxed)<<'\n';
        try {
            if(enter()) {
                for(std::size_t index=0;index<thread_count_;++index)
                    out<<"thread="<<threads_[index].thread<<" calls="<<threads_[index].count<<'\n';
                leave();
            }
        } catch(...) {}
        return out.str();
    }
    std::uint64_t calls(unsigned site) const noexcept {return site<2?calls_[site].load(std::memory_order_relaxed):0;}
    std::uint64_t phase_calls(std::uint32_t phase) const noexcept {
        return phase<phase_count?phase_calls_[phase].load(std::memory_order_relaxed):0;
    }
    std::uint64_t phase_publishes(std::uint32_t phase) const noexcept {
        return phase<phase_count?phase_publishes_[phase].load(std::memory_order_relaxed):0;
    }
    std::uint32_t phase() const noexcept {return phase_.load(std::memory_order_relaxed);}
    std::uint64_t published() const noexcept {return published_.load(std::memory_order_relaxed);}
    std::uint64_t pushed() const noexcept {return pushed_.load(std::memory_order_relaxed);}
    std::uint64_t busy() const noexcept {return busy_.load(std::memory_order_relaxed);}
    std::uint64_t dropped() const noexcept {return dropped_.load(std::memory_order_relaxed);}
    std::uint64_t thread_overflow() const noexcept {return thread_overflow_.load(std::memory_order_relaxed);}
    unsigned hooks() const noexcept {return (enabled_[0]?1u:0u)+(enabled_[1]?1u:0u);}
    bool installed(std::uintptr_t rva) const noexcept {
        return (rva==update_rva && enabled_[0]) || (rva==project_rva && enabled_[1]);
    }
    bool created(std::uintptr_t rva) const noexcept {
        return (rva==update_rva && created_[0]) || (rva==project_rva && created_[1]);
    }
    bool active() const noexcept {return active_.load(std::memory_order_acquire);}
    const Row* ring() const noexcept {return ring_.data();}
};

// Diagnostic and strictly read-only, so it installs only when the module carries
// a build/marker-probe flag file (the same shape as build/hud-census):
// `type nul > build\marker-probe` turns it on for one session.
inline bool native_marker_probe_requested(const std::filesystem::path& root) {
    std::error_code error;
    return std::filesystem::exists(root/L"build"/L"marker-probe",error);
}
} // namespace d2vr
