#pragma once
#include <windows.h>
#include <intrin.h>
// Relative so a bare `cl tests\..._check.cpp` with no /I still resolves; the
// repository build passes third_party\minhook\include as well.
#include "../third_party/minhook/include/MinHook.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace d2vr {
// Which HUD elements we can move into the VR view, found through the one
// mechanism that already works: the engine's shared
// arkIggyAS3Object::SetAS3Value<float> at RVA 0x2A2C0 (native_crosshair.hpp),
// called as setter(object_field_address, &value, member_handle_address) with
// both addresses inside the HUD element. The crosshair's own element passes
// object = element+0x3A8 with its x/y handles at element+0x660/+0x670, so its
// two calls show up as member deltas 0x2B8 and 0x2C8 from the caller at
// 0x67318F. Every other (caller, delta) pair this census reports is another HUD
// element whose AS3 member can be driven exactly the same way: delta 0x2B8/0x2C8
// from a distinct caller means that caller's element has the crosshair's x/y
// field layout, and the caller RVA names the update function to hook or to trace
// for the element's lifetime.
//
// The census is strictly read-only. 0x2A2C0 is hooked with MinHook, the original
// runs first and its result is returned unchanged, and the setter is never used
// to write anything. Each call is reduced to its caller's return-address RVA and
// the member delta, aggregated in a fixed open-addressing table, and (for the
// first detail_limit calls) kept as one raw record.
//
// The expected prologue was extracted from the pinned executable by mapping RVA
// 0x2A2C0 through its own PE section table
// (artifacts/re-toolchain/direct-ghidra/Dishonored2-steam17576867.exe: .text,
// RVA 0x1000 -> file 0x400, VirtualSize 0x1A7CE2C, so 0x2A2C0 lies at file
// 0x296C0), and install() proves the same bytes are live, that the RVA falls in
// an executable section of the section table, and that callers lie inside
// SizeOfImage before any of them is recorded.
//
// Cost: the setter is hot. The hot path is one indirect call, one
// _ReturnAddress, one SEH-guarded float load, a fixed-table insert under a
// bounded spinlock, and one bounded detail slot while they last. No allocation,
// no I/O, no blocking lock: a contended table drops that sample and counts it.
class HudCensusTable {
public:
    inline static constexpr std::size_t slots=4096,tracked_objects=4;
    static_assert((slots&(slots-1))==0,"The census table probe needs a power-of-two slot count");
    // One (caller RVA, member delta) group. minimum/maximum hold the extremes of
    // the readable values; samples counts those reads, so a group with only
    // unreadable values reports nan for last/min/max instead of a stale zero.
    struct Group {
        std::uint32_t caller{};std::int32_t delta{};
        std::uint64_t count{},samples{};
        float last{},minimum{std::numeric_limits<float>::infinity()},maximum{-std::numeric_limits<float>::infinity()};
        std::uintptr_t objects[tracked_objects]{};
        std::uint32_t objects_seen{};
        bool occupied{},saturated{};
    };
    // Returns false when the sample was not recorded: the table is full
    // (overflow(), a lower bound on the groups that were dropped) or the lock
    // stayed contended (busy()). A true result means count and value moved.
    bool insert(std::uint32_t caller,std::int32_t delta,std::uintptr_t object,float value) noexcept;
    // Folds other's groups into this table under both locks. Only one thread may
    // ever merge, and never two tables into each other: the lock order is fixed.
    bool merge(const HudCensusTable& other) noexcept;
    std::optional<Group> find(std::uint32_t caller,std::int32_t delta) const noexcept;
    std::size_t size() const noexcept;
    std::uint64_t overflow() const noexcept {return overflow_.load(std::memory_order_relaxed);}
    std::uint64_t busy() const noexcept {return busy_.load(std::memory_order_relaxed);}
    std::string csv() const;
    // One line per group, highest count first, at most limit lines (40 by default).
    std::string summary(std::size_t limit=40) const;
    void clear() noexcept;
    // Presentation helpers, shared with the census's call-site detail dump.
    static std::string hex_text(std::uint64_t value);
    static std::string number(float value);
private:
    static std::size_t hash(std::uint32_t caller,std::int32_t delta) noexcept;
    Group* claim(std::uint32_t caller,std::int32_t delta) noexcept;
    bool enter() const noexcept;
    void leave() const noexcept;
    void lock_cold() const noexcept;
    std::vector<const Group*> occupied() const;
    std::array<Group,slots> table_{};
    mutable std::atomic_flag lock_=ATOMIC_FLAG_INIT;
    std::atomic<std::uint64_t> overflow_{},busy_{};
};

inline std::size_t HudCensusTable::hash(std::uint32_t caller,std::int32_t delta) noexcept {
    auto key=(static_cast<std::uint64_t>(caller)<<32)|static_cast<std::uint32_t>(delta);
    key^=key>>33;key*=0xff51afd7ed558ccdull;key^=key>>33;
    return static_cast<std::size_t>(key)&(slots-1);
}

inline bool HudCensusTable::enter() const noexcept {
    for(unsigned spin=0;spin<4096;++spin) {
        if(!lock_.test_and_set(std::memory_order_acquire)) return true;
        YieldProcessor();
    }
    return false;
}

inline void HudCensusTable::leave() const noexcept {lock_.clear(std::memory_order_release);}

// The hot-path lock must never be held across a throw and must never block the
// setter, so the recording path only ever takes it for a bounded spin. The cold
// readers below spin until they get it: their holder is a few counter updates.
inline void HudCensusTable::lock_cold() const noexcept {
    while(lock_.test_and_set(std::memory_order_acquire)) YieldProcessor();
}

inline HudCensusTable::Group* HudCensusTable::claim(std::uint32_t caller,std::int32_t delta) noexcept {
    auto index=hash(caller,delta);
    for(std::size_t probe=0;probe<slots;++probe) {
        auto& group=table_[index];
        if(!group.occupied) {group={};group.occupied=true;group.caller=caller;group.delta=delta;return &group;}
        if(group.caller==caller && group.delta==delta) return &group;
        index=(index+1)&(slots-1);
    }
    return nullptr;
}

inline bool HudCensusTable::insert(std::uint32_t caller,std::int32_t delta,std::uintptr_t object,float value) noexcept {
    if(!enter()) {busy_.fetch_add(1,std::memory_order_relaxed);return false;}
    auto* group=claim(caller,delta);
    if(!group) {overflow_.fetch_add(1,std::memory_order_relaxed);leave();return false;}
    ++group->count;
    if(!std::isnan(value)) {
        ++group->samples;group->last=value;
        if(value<group->minimum) group->minimum=value;
        if(value>group->maximum) group->maximum=value;
    }
    if(object) {
        bool known=false;
        for(std::uint32_t index=0;index<group->objects_seen;++index) if(group->objects[index]==object) {known=true;break;}
        if(!known) {
            if(group->objects_seen<tracked_objects) group->objects[group->objects_seen++]=object;
            else group->saturated=true;
        }
    }
    leave();
    return true;
}

inline bool HudCensusTable::merge(const HudCensusTable& other) noexcept {
    if(&other==this) return false;
    other.lock_cold();lock_cold();
    bool complete=true;
    overflow_.fetch_add(other.overflow_.load(std::memory_order_relaxed),std::memory_order_relaxed);
    for(const auto& source:other.table_) {
        if(!source.occupied) continue;
        auto* target=claim(source.caller,source.delta);
        if(!target) {overflow_.fetch_add(1,std::memory_order_relaxed);complete=false;continue;}
        target->count+=source.count;
        if(source.samples) {
            target->samples+=source.samples;target->last=source.last;
            if(source.minimum<target->minimum) target->minimum=source.minimum;
            if(source.maximum>target->maximum) target->maximum=source.maximum;
        }
        for(std::uint32_t index=0;index<source.objects_seen;++index) {
            const auto object=source.objects[index];
            bool known=false;
            for(std::uint32_t seen=0;seen<target->objects_seen;++seen) if(target->objects[seen]==object) {known=true;break;}
            if(known) continue;
            if(target->objects_seen<tracked_objects) target->objects[target->objects_seen++]=object;
            else target->saturated=true;
        }
        if(source.saturated) target->saturated=true;
    }
    leave();other.leave();
    return complete;
}

inline std::optional<HudCensusTable::Group> HudCensusTable::find(std::uint32_t caller,std::int32_t delta) const noexcept {
    lock_cold();
    std::optional<Group> found;
    auto index=hash(caller,delta);
    for(std::size_t probe=0;probe<slots;++probe) {
        const auto& group=table_[index];
        if(!group.occupied) break;
        if(group.caller==caller && group.delta==delta) {found=group;break;}
        index=(index+1)&(slots-1);
    }
    leave();
    return found;
}

inline std::size_t HudCensusTable::size() const noexcept {
    lock_cold();
    std::size_t count=0;
    for(const auto& group:table_) if(group.occupied) ++count;
    leave();
    return count;
}

inline void HudCensusTable::clear() noexcept {
    lock_cold();
    table_={};overflow_.store(0,std::memory_order_relaxed);busy_.store(0,std::memory_order_relaxed);
    leave();
}

inline std::string HudCensusTable::hex_text(std::uint64_t value) {
    std::ostringstream out;out<<"0x"<<std::hex<<value;return out.str();
}

inline std::string HudCensusTable::number(float value) {
    if(!std::isfinite(value)) return "nan";
    std::ostringstream out;out<<std::setprecision(9)<<value;return out.str();
}

inline std::vector<const HudCensusTable::Group*> HudCensusTable::occupied() const {
    std::vector<const Group*> groups;groups.reserve(table_.size());
    for(const auto& group:table_) if(group.occupied) groups.push_back(&group);
    return groups;
}

inline std::string HudCensusTable::csv() const {
    lock_cold();
    try {
        auto groups=occupied();
        std::sort(groups.begin(),groups.end(),[](const Group* a,const Group* b){
            if(a->caller!=b->caller) return a->caller<b->caller;return a->delta<b->delta;});
        std::ostringstream out;
        out<<"caller_rva,delta,count,samples,distinct_objects,objects_saturated,last_value,min_value,max_value\n";
        for(const auto* group:groups)
            out<<hex_text(group->caller)<<','<<hex_text(static_cast<std::uint32_t>(group->delta))<<','
               <<group->count<<','<<group->samples<<','<<group->objects_seen<<','<<(group->saturated?1:0)<<','
               <<(group->samples?number(group->last):std::string("nan"))<<','
               <<(group->samples?number(group->minimum):std::string("nan"))<<','
               <<(group->samples?number(group->maximum):std::string("nan"))<<'\n';
        leave();
        return out.str();
    } catch(...) {leave();return {};}
}

inline std::string HudCensusTable::summary(std::size_t limit) const {
    if(!limit) return {};
    lock_cold();
    try {
        auto groups=occupied();
        std::sort(groups.begin(),groups.end(),[](const Group* a,const Group* b){
            if(a->count!=b->count) return a->count>b->count;
            if(a->caller!=b->caller) return a->caller<b->caller;
            return a->delta<b->delta;});
        std::ostringstream out;
        for(std::size_t index=0;index<groups.size() && index<limit;++index) {
            const auto* group=groups[index];
            out<<"caller="<<hex_text(group->caller)<<" delta="<<hex_text(static_cast<std::uint32_t>(group->delta))
               <<" count="<<group->count<<" samples="<<group->samples<<" objects="<<group->objects_seen
               <<" saturated="<<(group->saturated?1:0)
               <<" last="<<(group->samples?number(group->last):std::string("nan"))
               <<" min="<<(group->samples?number(group->minimum):std::string("nan"))
               <<" max="<<(group->samples?number(group->maximum):std::string("nan"))<<'\n';
        }
        leave();
        return out.str();
    } catch(...) {leave();return {};}
}

// The census is diagnostic and hooks a shared engine path, so the probe installs
// it only when the module carries a build/hud-census flag file (the same shape as
// build/fire-probe): `type nul > build\hud-census` turns it on for one session.
inline bool native_hud_census_requested(const std::filesystem::path& root) {
    std::error_code error;
    return std::filesystem::exists(root/L"build"/L"hud-census",error);
}

// Read-only census of every arkIggyAS3Object::SetAS3Value<float> caller. The
// owner must live in static storage: the table and the raw call detail are a
// fixed ~1 MB, deliberately never allocated after install.
class NativeHudCensus {
    using Setter=std::uint64_t (__fastcall*)(std::uintptr_t,const float*,void*);
    using Read=bool (*)(std::uintptr_t,void*,std::size_t);
    inline static NativeHudCensus* instance_{};
    inline static constexpr std::uintptr_t setter_rva=0x2a2c0;
    inline static constexpr std::size_t detail_limit=20000,max_sections=16;
    // 0x2A2C0 from the pinned executable: push rdi; sub rsp,0x50; mov qword
    // [rsp+0x20],-2; then the non-volatile saves. MinHook relocates exactly this.
    inline static constexpr std::array<std::byte,32> expected{{
        std::byte{0x40},std::byte{0x57},std::byte{0x48},std::byte{0x83},std::byte{0xec},std::byte{0x50},std::byte{0x48},std::byte{0xc7},
        std::byte{0x44},std::byte{0x24},std::byte{0x20},std::byte{0xfe},std::byte{0xff},std::byte{0xff},std::byte{0xff},std::byte{0x48},
        std::byte{0x89},std::byte{0x5c},std::byte{0x24},std::byte{0x68},std::byte{0x48},std::byte{0x89},std::byte{0x6c},std::byte{0x24},
        std::byte{0x70},std::byte{0x48},std::byte{0x89},std::byte{0x74},std::byte{0x24},std::byte{0x78},std::byte{0x49},std::byte{0x8b}
    }};
    // callers, threads: the caller RVA, the raw member handle delta, the receiver
    // field address and the handle field address, all preserved verbatim.
    struct Detail {
        std::uint32_t caller{};std::int32_t delta{};
        std::uintptr_t object_field{},member{};
        float value{};std::uint32_t thread{};
    };
    Setter original_{};Read read_{};std::uintptr_t base_{},image_size_{};
    std::filesystem::path file_;
    HudCensusTable table_;
    std::array<Detail,detail_limit> details_{};
    std::atomic<std::uint64_t> calls_{},recorded_{},foreign_{},odd_{};
    std::atomic<UINT> depth_{};
    std::atomic<bool> active_{};
    bool enabled_{},installed_{};

    static bool executable(std::uintptr_t at) {
        MEMORY_BASIC_INFORMATION info{};
        if(!at || VirtualQuery(reinterpret_cast<const void*>(at),&info,sizeof info)!=sizeof info) return false;
        return info.State==MEM_COMMIT && (info.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY))!=0;
    }

    // The value pointer is the engine's own live argument, already dereferenced by
    // the original that just ran, so a plain load is one instruction; __except
    // keeps a stale pointer (teardown racing this call) from taking the process
    // down. ReadProcessMemory cannot be used: it is a kernel transition per call.
    static bool sample(const float* value,float& out) noexcept {
        if(!value) return false;
        __try {out=*value;return true;}
        __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }

    void record(std::uintptr_t return_address,std::uintptr_t object_field,const float* value,void* member) noexcept {
        calls_.fetch_add(1,std::memory_order_relaxed);
        // A call from outside the pinned image names no HUD element update.
        if(return_address<base_ || return_address-base_>=image_size_) {foreign_.fetch_add(1,std::memory_order_relaxed);return;}
        const auto delta=static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(member))-static_cast<std::int64_t>(object_field);
        if(delta<std::numeric_limits<std::int32_t>::min() || delta>std::numeric_limits<std::int32_t>::max()) {
            odd_.fetch_add(1,std::memory_order_relaxed);return;
        }
        const auto caller=static_cast<std::uint32_t>(return_address-base_);
        float observed=std::numeric_limits<float>::quiet_NaN();
        if(!sample(value,observed)) observed=std::numeric_limits<float>::quiet_NaN();
        const auto index=recorded_.fetch_add(1,std::memory_order_relaxed);
        if(index<detail_limit)
            details_[index]=Detail{caller,static_cast<std::int32_t>(delta),object_field,
                reinterpret_cast<std::uintptr_t>(member),observed,GetCurrentThreadId()};
        table_.insert(caller,static_cast<std::int32_t>(delta),object_field,observed);
    }
    static std::uint64_t __fastcall setter(std::uintptr_t object_field,const float* value,void* member) {
        auto& self=*instance_;
        self.depth_.fetch_add(1,std::memory_order_acq_rel);
        std::uint64_t result{};
        __try {
            result=self.original_(object_field,value,member);
            self.record(reinterpret_cast<std::uintptr_t>(_ReturnAddress()),object_field,value,member);
        } __finally {self.depth_.fetch_sub(1,std::memory_order_acq_rel);}
        return result;
    }
public:
    // Verifies the pinned image (PE headers, executable section covering 0x2A2C0,
    // the exact prologue), then enables the read-only detour. read is the
    // process-safe reader the other native hooks are installed with.
    void install(std::uintptr_t base,Read read,const std::filesystem::path& file) {
        if(!base || !read || active_.load(std::memory_order_acquire) || (instance_ && instance_!=this))
            throw std::runtime_error("Invalid native HUD census ownership");
        base_=base;read_=read;
        IMAGE_DOS_HEADER dos{};
        if(!read_(base,&dos,sizeof dos) || dos.e_magic!=IMAGE_DOS_SIGNATURE || dos.e_lfanew<64 || dos.e_lfanew>0x1000)
            throw std::runtime_error("Native HUD census module is not the pinned image");
        const auto nt_address=base+static_cast<std::uintptr_t>(dos.e_lfanew);
        IMAGE_NT_HEADERS64 nt{};
        if(!read_(nt_address,&nt,sizeof nt) || nt.Signature!=IMAGE_NT_SIGNATURE ||
           nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
           nt.FileHeader.NumberOfSections<1 || nt.FileHeader.NumberOfSections>max_sections)
            throw std::runtime_error("Native HUD census module is not the pinned image");
        bool covered=false;
        for(WORD index=0;index<nt.FileHeader.NumberOfSections;++index) {
            IMAGE_SECTION_HEADER section{};
            if(!read_(nt_address+sizeof nt+static_cast<std::uintptr_t>(index)*sizeof section,&section,sizeof section))
                throw std::runtime_error("Native HUD census section table is unreadable");
            const auto extent=static_cast<std::uintptr_t>(std::max(section.Misc.VirtualSize,section.SizeOfRawData));
            if(setter_rva>=section.VirtualAddress && setter_rva+expected.size()<=section.VirtualAddress+extent &&
               (section.Characteristics&IMAGE_SCN_MEM_EXECUTE)) covered=true;
        }
        if(!covered) throw std::runtime_error("Native HUD census member setter is not in an executable section");
        image_size_=nt.OptionalHeader.SizeOfImage;
        if(image_size_<setter_rva+expected.size())
            throw std::runtime_error("Native HUD census module is not the pinned image");
        std::array<std::byte,32> code{};
        if(!read_(base+setter_rva,code.data(),code.size()) || code!=expected)
            throw std::runtime_error("Native HUD census member setter differs from the pinned executable");
        if(!executable(base+setter_rva)) throw std::runtime_error("Native HUD census member setter is not executable");
        file_=file;
        table_.clear();calls_=0;recorded_=0;foreign_=0;odd_=0;depth_=0;enabled_=false;installed_=false;instance_=this;
        const auto target=reinterpret_cast<void*>(base+setter_rva);
        if(!original_ && MH_CreateHook(target,reinterpret_cast<void*>(&setter),reinterpret_cast<void**>(&original_))!=MH_OK)
            throw std::runtime_error("Cannot create native HUD census hook");
        active_.store(true,std::memory_order_release);
        if(MH_EnableHook(target)!=MH_OK) {active_.store(false,std::memory_order_release);throw std::runtime_error("Cannot enable native HUD census hook");}
        enabled_=true;installed_=true;
    }
    // Disengages the detour, waits out any in-flight call, then dumps the
    // aggregate CSV to the install path and the bounded raw call detail next to
    // it as <stem>-calls.csv. True means the prologue is the pinned one again.
    bool disable() noexcept {
        active_.store(false,std::memory_order_release);
        if(enabled_) {
            if(MH_DisableHook(reinterpret_cast<void*>(base_+setter_rva))!=MH_OK) return false;
            enabled_=false;
        }
        const auto deadline=GetTickCount64()+5000;
        while(depth_.load(std::memory_order_acquire) && GetTickCount64()<deadline) Sleep(1);
        if(depth_) return false;
        if(!installed_) return true;
        installed_=false;
        try {
            if(!file_.empty()) {
                std::ofstream out(file_,std::ios::trunc);
                if(!out) return false;
                out<<csv();
            }
            const auto detail_file=file_.parent_path()/(file_.stem().string()+"-calls"+file_.extension().string());
            if(!detail_file.empty()) {
                std::ofstream out(detail_file,std::ios::trunc);
                if(!out) return false;
                out<<details_csv();
            }
        } catch(...) {return false;}
        std::array<std::byte,32> code{};
        return !original_ || (read_(base_+setter_rva,code.data(),code.size()) && code==expected);
    }
    std::string csv() const {return table_.csv();}
    // callers, threads: the raw per-call evidence, capped at detail_limit.
    std::string details_csv() const {
        std::ostringstream out;
        out<<"caller_rva,delta,object_field,member,value,thread\n";
        const auto count=recorded();
        for(std::uint64_t index=0;index<count;++index) {
            const auto& detail=details_[static_cast<std::size_t>(index)];
            out<<HudCensusTable::hex_text(detail.caller)<<','<<HudCensusTable::hex_text(static_cast<std::uint32_t>(detail.delta))
               <<','<<HudCensusTable::hex_text(detail.object_field)<<','<<HudCensusTable::hex_text(detail.member)<<','
               <<HudCensusTable::number(detail.value)<<','<<detail.thread<<'\n';
        }
        return out.str();
    }
    // Diagnostics plus the table's top 40 (caller, delta) groups: the candidates
    // with delta 0x2B8/0x2C8 carry the crosshair's x/y layout, and each caller RVA
    // is the element update function that feeds it.
    std::string summary() const {
        std::ostringstream out;
        out<<"native hud census calls="<<calls_.load(std::memory_order_relaxed)<<" recorded="<<recorded()
           <<" foreign="<<foreign_.load(std::memory_order_relaxed)<<" odd="<<odd_.load(std::memory_order_relaxed)
           <<" groups="<<table_.size()<<" overflow="<<table_.overflow()<<" busy="<<table_.busy()<<'\n';
        out<<table_.summary();
        return out.str();
    }
    std::uint64_t calls() const noexcept {return calls_.load(std::memory_order_relaxed);}
    std::uint64_t recorded() const noexcept {
        const auto recorded=recorded_.load(std::memory_order_relaxed);
        return recorded<detail_limit?recorded:static_cast<std::uint64_t>(detail_limit);
    }
    std::uint64_t foreign() const noexcept {return foreign_.load(std::memory_order_relaxed);}
    std::uint64_t odd() const noexcept {return odd_.load(std::memory_order_relaxed);}
    std::uint64_t overflow() const noexcept {return table_.overflow();}
    std::uint64_t busy() const noexcept {return table_.busy();}
    std::optional<HudCensusTable::Group> find(std::uint32_t caller,std::int32_t delta) const noexcept {return table_.find(caller,delta);}
    std::size_t groups() const noexcept {return table_.size();}
    bool active() const noexcept {return active_.load(std::memory_order_acquire);}
};
} // namespace d2vr
