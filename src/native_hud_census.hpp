#pragma once
#include "native_readable_range.hpp"
#include <windows.h>
#include <intrin.h>
// Relative so a bare `cl tests\..._check.cpp` with no /I still resolves; the
// repository build passes third_party\minhook\include as well.
#include "../third_party/minhook/include/MinHook.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
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
// both addresses inside the HUD element.
//
// The crosshair's x/y writes are not made by the element update itself. 0x672F70
// (artifact aim-reader-terminal/function-140672f70.txt) calls the two placement
// helpers 0x672DB0 at :102 (stage centre) and 0x672E20 at :108 (projected world
// point), and those call the setter with object = element+0x3A8 and the handles
// at element+0x660/+0x670, so crosshair x/y arrive as member deltas 0x2B8 and
// 0x2C8 and only from callers 0x672E20/0x672DB0. The update's own call, at
// 0x67318F in that same artifact (:87), writes element+0x100 with the handle at
// element+0x650 and the constant 1.0 - delta 0x550 - so it is not a crosshair
// x/y write; an earlier note here attributed the x/y rows to it and was wrong.
//
// Every other (caller, delta) pair this census reports is another HUD element
// whose AS3 member can be driven exactly the same way: delta 0x2B8/0x2C8 from a
// distinct caller means that caller's element has the crosshair's x/y field
// layout, and the caller RVA names the update function to hook or to trace for
// the element's lifetime.
//
// Caller attribution alone only sees engine callers: 27002 of 28639 setter calls
// in the review session came from JIT/foreign code outside the image. For every
// foreign call the census walks the stack with RtlCaptureStackBackTrace (bounded
// by stack_walk_limit and by stack_frames) and folds the first frame that lands
// inside the pinned image into a second table, which is what attributes a JIT
// write to an engine RVA. For every aggregate group it keeps the first four
// distinct receiver field addresses and, once per distinct AS3 object those point
// to, dumps the object's member-record table (artifacts .../demo-native-iggy-
// mouse-api/function-14002a8b0.txt: record table at object+0x18, 1-based 24-bit
// member index at object+0x20, 0x28-byte records) as raw hex plus every printable
// name pointer found in those records.
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
// bounded spinlock, one bounded object-field read with a linear scan of at most
// dumped_objects dumps, and one bounded detail slot while they last. Only foreign
// calls pay for the stack walk, which is itself capped. No allocation, no I/O, no
// blocking lock: a contended lock drops that sample and counts it.
inline constexpr std::int32_t hud_census_crosshair_x_delta=0x2b8,
    hud_census_crosshair_y_delta=0x2c8,hud_census_update_delta=0x550;
// The AS3 member-record table (artifact function-14002a8b0.txt).
inline constexpr std::uintptr_t hud_census_member_table_offset=0x18,
    hud_census_member_index_offset=0x20;
inline constexpr std::size_t member_record_stride=0x28,member_record_fields=5,member_record_limit=64,name_bytes=64,
    dumped_object_limit=32;

// One 8-byte-aligned field of a member record that points at printable ASCII.
struct MemberName {
    std::uint32_t field{};   // Byte offset of the pointer inside the record.
    std::string text;
};
// One raw 0x28-byte AS3 member record plus the printable names its pointer
// fields resolve to. The raw bytes are the evidence; the names are the reading
// aid, and a record with neither is still recorded as raw bytes alone.
struct MemberRecord {
    std::array<std::byte,member_record_stride> raw{};
    std::array<MemberName,member_record_fields> names{};
    std::uint32_t name_count{};
};

class HudCensusTable {
public:
    inline static constexpr std::size_t slots=4096,tracked_objects=4;
    static_assert((slots&(slots-1))==0,"The census table probe needs a power-of-two slot count");
    // One (caller RVA, member delta) group. minimum/maximum hold the extremes of
    // the readable values; samples counts those reads, so a group with only
    // unreadable values reports nan for last/min/max instead of a stale zero.
    // objects holds the first tracked_objects distinct receiver field addresses.
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

// Every group carries its first tracked_objects receiver field addresses, so a
// reader can tell two HUD elements with the same caller/delta apart and can point
// a debugger at the object each one is feeding.
inline std::string HudCensusTable::csv() const {
    lock_cold();
    try {
        auto groups=occupied();
        std::sort(groups.begin(),groups.end(),[](const Group* a,const Group* b){
            if(a->caller!=b->caller) return a->caller<b->caller;return a->delta<b->delta;});
        std::ostringstream out;
        out<<"caller_rva,delta,count,samples,distinct_objects,objects_saturated,last_value,min_value,max_value"
           <<",object0,object1,object2,object3\n";
        for(const auto* group:groups) {
            out<<hex_text(group->caller)<<','<<hex_text(static_cast<std::uint32_t>(group->delta))<<','
               <<group->count<<','<<group->samples<<','<<group->objects_seen<<','<<(group->saturated?1:0)<<','
               <<(group->samples?number(group->last):std::string("nan"))<<','
               <<(group->samples?number(group->minimum):std::string("nan"))<<','
               <<(group->samples?number(group->maximum):std::string("nan"));
            for(std::uint32_t index=0;index<tracked_objects;++index)
                out<<','<<hex_text(index<group->objects_seen?group->objects[index]:0);
            out<<'\n';
        }
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
               <<" max="<<(group->samples?number(group->maximum):std::string("nan"));
            for(std::uint32_t seen=0;seen<group->objects_seen;++seen) out<<" field"<<seen<<'='<<hex_text(group->objects[seen]);
            out<<'\n';
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
// owner must live in static storage: the table, the raw call detail, the stack
// attribution table and the AS3 object dumps are a fixed ~2 MB, deliberately
// never allocated after install.
class NativeHudCensus {
    using Setter=std::uint64_t (__fastcall*)(std::uintptr_t,const float*,void*);
    using Read=bool (*)(std::uintptr_t,void*,std::size_t);
    inline static NativeHudCensus* instance_{};
    inline static constexpr std::uintptr_t setter_rva=0x2a2c0;
    inline static constexpr std::size_t detail_limit=20000,max_sections=16;
    // Foreign calls are the majority (27002 of 28639), so the stack walk is the
    // one unbounded-looking cost in the hot path; past this many walks it is not
    // paid at all and those calls are counted as dropped.
    inline static constexpr std::uint64_t stack_walk_limit=200000;
    // Frames to capture for the foreign stack walk. The walk cannot assume a
    // fixed depth: the detour's own recording and helper bodies may be inlined
    // into one another, so the caller's caller is not always frame 2. It takes the
    // first captured frame that lands inside the pinned image instead, which is
    // the engine function above the foreign caller however deep the detour's own
    // frames happen to be.
    inline static constexpr unsigned stack_frames=8;
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
    // One AS3 object captured once, keyed by the pointer its receiver field held.
    struct ObjectDump {
        std::uintptr_t object{},object_field{};
        std::uint32_t members{},dumped{};   // The 24-bit count, then how many were read.
        std::array<MemberRecord,member_record_limit> records{};
        bool occupied{};
    };
    Setter original_{};Read read_{};std::uintptr_t base_{},image_size_{};
    std::filesystem::path file_;
    HudCensusTable table_,stack_table_;
    std::array<Detail,detail_limit> details_{};
    std::array<ObjectDump,dumped_object_limit> dumps_{};
    std::uint32_t dump_count_{};
    mutable std::atomic_flag dump_lock_=ATOMIC_FLAG_INIT;
    std::atomic<std::uint64_t> calls_{},recorded_{},foreign_{},odd_{},stacked_{},unattributed_{},
        stack_walked_{},stack_dropped_{},dump_busy_{},dump_saturated_{};
    std::atomic<UINT> depth_{};
    std::atomic<bool> active_{};
    bool enabled_{},installed_{};

    static bool executable(std::uintptr_t at) {
        return native_executable_cached(at);
    }

    // Bounded engine reads under their own SEH scope: only PODs live here, so a
    // torn AS3 pointer in the same call can never take the setter down, and in the
    // game the reader is already the process-safe ReadProcessMemory wrapper.
    static bool read_engine(Read read,std::uintptr_t at,void* out,std::size_t size) noexcept {
        __try {return read(at,out,size);}
        __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
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

    bool dump_enter() const noexcept {
        for(unsigned spin=0;spin<4096;++spin) {
            if(!dump_lock_.test_and_set(std::memory_order_acquire)) return true;
            YieldProcessor();
        }
        return false;
    }
    void dump_leave() const noexcept {dump_lock_.clear(std::memory_order_release);}
    void dump_lock_cold() const noexcept {while(dump_lock_.test_and_set(std::memory_order_acquire)) YieldProcessor();}

    // A record's pointer field, kept only when it reads back as a NUL-terminated
    // all-printable-ASCII string inside the window. A small integer or an
    // unreadable address simply yields no name.
    std::string name_at(std::uintptr_t pointer) const {
        if(!pointer) return {};
        std::array<char,name_bytes> text{};
        if(!read_engine(read_,pointer,text.data(),text.size())) return {};
        const auto end=std::find(text.begin(),text.end(),'\0');
        if(end==text.end() || end==text.begin()) return {};
        for(auto at=text.begin();at!=end;++at) if(*at<' ' || *at>'~') return {};
        return std::string(text.begin(),end);
    }

    // The AS3 object's member-record table (function-14002a8b0.txt). Runs once per
    // distinct object, off the hot lock: everything it writes is engine data into
    // a fixed slot the census already claimed.
    void capture(ObjectDump& dump) noexcept {
        try {
            std::uintptr_t table{};
            std::uint32_t word{};
            if(!read_engine(read_,dump.object+hud_census_member_table_offset,&table,sizeof table) ||
               !read_engine(read_,dump.object+hud_census_member_index_offset,&word,sizeof word) || !table) return;
            dump.members=word&0xffffffu;
            const auto count=dump.members<member_record_limit?dump.members:static_cast<std::uint32_t>(member_record_limit);
            for(std::uint32_t index=0;index<count;++index) {
                MemberRecord record;
                const auto address=table+static_cast<std::uintptr_t>(index)*member_record_stride;
                if(!read_engine(read_,address,record.raw.data(),record.raw.size())) break;
                for(std::uint32_t field=0;field<member_record_fields;++field) {
                    std::uintptr_t pointer{};
                    std::memcpy(&pointer,record.raw.data()+static_cast<std::size_t>(field)*8,sizeof pointer);
                    auto text=name_at(pointer);
                    if(text.empty()) continue;
                    record.names[record.name_count++]={field,text};
                }
                dump.records[dump.dumped++]=record;
            }
        } catch(...) {}
    }

    // Once per distinct AS3 object: the slot is claimed under the bounded spin and
    // the reads happen after it is released, so a second thread never repeats the
    // dump and never spins behind 64 engine reads.
    void dump_object(std::uintptr_t object_field) noexcept {
        std::uintptr_t object{};
        if(!read_engine(read_,object_field,&object,sizeof object) || !object) return;
        if(!dump_enter()) {dump_busy_.fetch_add(1,std::memory_order_relaxed);return;}
        ObjectDump* slot{};
        bool known=false;
        for(std::uint32_t index=0;index<dump_count_;++index)
            if(dumps_[index].occupied && dumps_[index].object==object) {known=true;break;}
        if(!known) {
            if(dump_count_<dumped_object_limit) {
                slot=&dumps_[dump_count_++];
                slot->object=object;slot->object_field=object_field;slot->occupied=true;
            } else dump_saturated_.fetch_add(1,std::memory_order_relaxed);
        }
        dump_leave();
        if(slot) capture(*slot);
    }

    // RtlCaptureStackBackTrace from inside the detour. JIT code lives outside the
    // image, and so do the census's own frames, so the first captured frame that
    // lands inside the pinned image is the engine caller above the foreign caller:
    // the manager or job that handed JIT the object.
    void attribute(std::uintptr_t object_field,std::int32_t delta,float observed) noexcept {
        if(stack_walked_.fetch_add(1,std::memory_order_relaxed)>=stack_walk_limit) {
            stack_dropped_.fetch_add(1,std::memory_order_relaxed);return;
        }
        void* frames[stack_frames]{};
        const auto captured=RtlCaptureStackBackTrace(0,stack_frames,frames,nullptr);
        for(unsigned index=0;index<captured;++index) {
            const auto frame=reinterpret_cast<std::uintptr_t>(frames[index]);
            if(frame<base_ || frame-base_>=image_size_) continue;
            stacked_.fetch_add(1,std::memory_order_relaxed);
            stack_table_.insert(static_cast<std::uint32_t>(frame-base_),delta,object_field,observed);
            return;
        }
        unattributed_.fetch_add(1,std::memory_order_relaxed);
    }

    void record(std::uintptr_t return_address,std::uintptr_t object_field,const float* value,void* member) noexcept {
        calls_.fetch_add(1,std::memory_order_relaxed);
        const auto delta=static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(member))-static_cast<std::int64_t>(object_field);
        if(delta<std::numeric_limits<std::int32_t>::min() || delta>std::numeric_limits<std::int32_t>::max()) {
            odd_.fetch_add(1,std::memory_order_relaxed);return;
        }
        float observed=std::numeric_limits<float>::quiet_NaN();
        if(!sample(value,observed)) observed=std::numeric_limits<float>::quiet_NaN();
        const auto key=static_cast<std::int32_t>(delta);
        // A call from outside the pinned image names no HUD element update, so it
        // is attributed one frame further up instead of into the aggregate.
        if(return_address<base_ || return_address-base_>=image_size_) {
            foreign_.fetch_add(1,std::memory_order_relaxed);
            attribute(object_field,key,observed);
            return;
        }
        const auto caller=static_cast<std::uint32_t>(return_address-base_);
        const auto index=recorded_.fetch_add(1,std::memory_order_relaxed);
        if(index<detail_limit)
            details_[index]=Detail{caller,key,object_field,
                reinterpret_cast<std::uintptr_t>(member),observed,GetCurrentThreadId()};
        table_.insert(caller,key,object_field,observed);
        dump_object(object_field);
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
        if(file.empty()) return;   // v1046: diagnostics off = a read-only census (its only output is these files) hooks nothing
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
        table_.clear();stack_table_.clear();calls_=0;recorded_=0;foreign_=0;odd_=0;depth_=0;
        stacked_=0;unattributed_=0;stack_walked_=0;stack_dropped_=0;dump_busy_=0;dump_saturated_=0;
        dump_count_=0;dumps_={};enabled_=false;installed_=false;instance_=this;
        const auto target=reinterpret_cast<void*>(base+setter_rva);
        if(!original_ && MH_CreateHook(target,reinterpret_cast<void*>(&setter),reinterpret_cast<void**>(&original_))!=MH_OK)
            throw std::runtime_error("Cannot create native HUD census hook");
        active_.store(true,std::memory_order_release);
        if(MH_EnableHook(target)!=MH_OK) {active_.store(false,std::memory_order_release);throw std::runtime_error("Cannot enable native HUD census hook");}
        enabled_=true;installed_=true;
    }
    // Disengages the detour, waits out any in-flight call, then dumps the
    // aggregate CSV to the install path, the bounded raw call detail next to it as
    // <stem>-calls.csv, the foreign stack attribution as <stem>-stack.csv and the
    // AS3 object dumps as <stem>-objects.csv. True means the prologue is the
    // pinned one again.
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
                const auto detail_file=file_.parent_path()/(file_.stem().string()+"-calls"+file_.extension().string());
                {std::ofstream detail(detail_file,std::ios::trunc);if(!detail) return false;detail<<details_csv();}
                const auto stack_file=file_.parent_path()/(file_.stem().string()+"-stack"+file_.extension().string());
                {std::ofstream stack(stack_file,std::ios::trunc);if(!stack) return false;stack<<stack_csv();}
                const auto object_file=file_.parent_path()/(file_.stem().string()+"-objects"+file_.extension().string());
                {std::ofstream objects(object_file,std::ios::trunc);if(!objects) return false;objects<<objects_csv();}
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
    // The foreign calls attributed to the engine frame above the JIT frame, keyed
    // by that frame's RVA and the same member delta, so a JIT write lands on the
    // engine function that handed it the object.
    std::string stack_csv() const {return stack_table_.csv();}
    // One row per member (and one bare row for an object whose table was
    // unreadable): the object, the receiver field it was reached through, the
    // 24-bit member count, one raw 0x28-byte record as hex, and the printable
    // names found in its five pointer fields.
    std::string objects_csv() const {
        std::ostringstream out;
        out<<"object,object_field,members,dumped,member,raw,names\n";
        dump_lock_cold();
        try {
            for(std::uint32_t index=0;index<dump_count_;++index) {
                const auto& dump=dumps_[index];
                for(std::uint32_t member=0;member<dump.dumped;++member) {
                    const auto& record=dump.records[member];
                    out<<HudCensusTable::hex_text(dump.object)<<','<<HudCensusTable::hex_text(dump.object_field)<<','
                       <<dump.members<<','<<dump.dumped<<','<<member<<',';
                    for(std::byte byte:record.raw) {
                        constexpr char digits[]="0123456789abcdef";
                        const auto value=std::to_integer<unsigned>(byte);
                        out<<digits[value>>4]<<digits[value&0xf];
                    }
                    out<<',';
                    for(std::uint32_t name=0;name<record.name_count;++name) {
                        if(name) out<<';';
                        out<<"+0x"<<std::hex<<record.names[name].field*8<<std::dec<<'='<<record.names[name].text;
                    }
                    out<<'\n';
                }
                if(!dump.dumped)
                    out<<HudCensusTable::hex_text(dump.object)<<','<<HudCensusTable::hex_text(dump.object_field)<<','
                       <<dump.members<<",0,,,\n";
            }
            dump_leave();
            return out.str();
        } catch(...) {dump_leave();return {};}
    }
    // Diagnostics plus the table's top 40 (caller, delta) groups: the candidates
    // with delta 0x2B8/0x2C8 carry the crosshair's x/y layout, and each caller RVA
    // is the element update function that feeds it.
    std::string summary() const {
        std::ostringstream out;
        out<<"native hud census calls="<<calls_.load(std::memory_order_relaxed)<<" recorded="<<recorded()
           <<" foreign="<<foreign_.load(std::memory_order_relaxed)
           <<" stacked="<<stacked_.load(std::memory_order_relaxed)
           <<" unattributed="<<unattributed_.load(std::memory_order_relaxed)
           <<" stack_dropped="<<stack_dropped_.load(std::memory_order_relaxed)
           <<" odd="<<odd_.load(std::memory_order_relaxed)
           <<" groups="<<table_.size()<<" overflow="<<table_.overflow()<<" busy="<<table_.busy()
           <<" stack_groups="<<stack_table_.size()<<" stack_overflow="<<stack_table_.overflow()
           <<" objects="<<dump_count_<<" object_saturated="<<dump_saturated_.load(std::memory_order_relaxed)
           <<" dump_busy="<<dump_busy_.load(std::memory_order_relaxed)<<'\n';
        out<<table_.summary();
        return out.str();
    }
    std::uint64_t calls() const noexcept {return calls_.load(std::memory_order_relaxed);}
    std::uint64_t recorded() const noexcept {
        const auto recorded=recorded_.load(std::memory_order_relaxed);
        return recorded<detail_limit?recorded:static_cast<std::uint64_t>(detail_limit);
    }
    std::uint64_t foreign() const noexcept {return foreign_.load(std::memory_order_relaxed);}
    // Foreign calls whose frame 2 landed inside the image, and those that did not
    // (or whose walk was cut short); the remainder of foreign() past both is what
    // the stack_walk_limit budget dropped.
    std::uint64_t stacked() const noexcept {return stacked_.load(std::memory_order_relaxed);}
    std::uint64_t unattributed() const noexcept {return unattributed_.load(std::memory_order_relaxed);}
    std::uint64_t stack_dropped() const noexcept {return stack_dropped_.load(std::memory_order_relaxed);}
    std::uint32_t dumped_objects() const noexcept {return dump_count_;}
    std::uint64_t dump_busy() const noexcept {return dump_busy_.load(std::memory_order_relaxed);}
    std::uint64_t dump_saturated() const noexcept {return dump_saturated_.load(std::memory_order_relaxed);}
    std::uint64_t odd() const noexcept {return odd_.load(std::memory_order_relaxed);}
    std::uint64_t overflow() const noexcept {return table_.overflow();}
    std::uint64_t busy() const noexcept {return table_.busy();}
    std::optional<HudCensusTable::Group> find(std::uint32_t caller,std::int32_t delta) const noexcept {return table_.find(caller,delta);}
    std::optional<HudCensusTable::Group> foreign_find(std::uint32_t caller,std::int32_t delta) const noexcept {return stack_table_.find(caller,delta);}
    std::size_t groups() const noexcept {return table_.size();}
    bool active() const noexcept {return active_.load(std::memory_order_acquire);}
};
} // namespace d2vr
