#pragma once
#include "native_initial_contact.hpp"

namespace d2vr {
struct NativeRegularContact {
    Vec3 point{},normal{};
    float fraction{};
    std::uint32_t source_body{},body{},shape_key{};
    std::uint16_t material{};
    bool normal_valid{};
};

struct NativeRegularContactCollector {
    NativeInitialContactCollector storage{};

    template<class Read> static bool initialize(NativeRegularContactCollector& output,NativeInitialContactTable& table,
                                                  std::uintptr_t base,Read& read) noexcept {
        if(!NativeInitialContactCollector::initialize(output.storage,table,base,read))return false;
        table.entries[3]=reinterpret_cast<std::uintptr_t>(&add_hit);
        return true;
    }
    NativeInitialContactCollector* native() noexcept {return &storage;}
    const NativeInitialContactCollector* native() const noexcept {return &storage;}
    bool begin() noexcept {return storage.begin();}
    bool finish() noexcept {return storage.finish();}
    bool complete() const noexcept {return storage.complete();}
    unsigned count() const noexcept {return storage.count;}
    std::optional<NativeRegularContact> contact(unsigned index) const noexcept {
        if(!complete() || index>=storage.count || index>=storage.records.size())return {};
        return decode(storage.records[index]);
    }
    static void __fastcall add_hit(NativeRegularContactCollector* collector,const void* input) noexcept {
        if(!collector)return;
        auto& target=collector->storage;
        if(!target.ready || !target.active) {target.incomplete=true;target.misuse=true;return;}
        if(target.count>=target.records.size()) {target.incomplete=true;target.overflow=true;return;}
        NativeInitialContactRecord record{};
        if(!guarded_copy(input,record)) {target.incomplete=true;return;}
        const auto contact=decode(record);
        if(!contact)target.incomplete=true;
        target.records[target.count++]=record;
    }
private:
    static bool guarded_copy(const void* input,NativeInitialContactRecord& output) noexcept {
        if(!input)return false;
        __try {std::memcpy(output.bytes.data(),input,output.bytes.size());return true;}
        __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    static std::optional<NativeRegularContact> decode(const NativeInitialContactRecord& record) noexcept {
        std::array<float,3> point{},normal{};NativeRegularContact result{};
        std::memcpy(point.data(),record.bytes.data(),12);std::memcpy(normal.data(),record.bytes.data()+0x10,12);
        std::memcpy(&result.fraction,record.bytes.data()+0x20,4);
        std::memcpy(&result.source_body,record.bytes.data()+0x28,4);
        std::memcpy(&result.material,record.bytes.data()+0x4c,2);
        std::memcpy(&result.body,record.bytes.data()+0x48,4);
        std::memcpy(&result.shape_key,record.bytes.data()+0x50,4);
        result.point={point[0],point[1],point[2]};result.normal={normal[0],normal[1],normal[2]};
        if(!finite(result.point) || !finite(result.normal) || !std::isfinite(result.fraction) || result.fraction<0 || result.fraction>1)return {};
        const auto magnitude=length(result.normal);result.normal_valid=magnitude>.5 && magnitude<1.5;
        if(result.normal_valid)result.normal=result.normal*(1/magnitude);
        return result;
    }
};
static_assert(std::is_standard_layout_v<NativeRegularContactCollector>);
static_assert(offsetof(NativeRegularContactCollector,storage)==0);
static_assert(alignof(NativeRegularContactCollector)==alignof(NativeInitialContactCollector));
static_assert(std::is_trivially_copyable_v<NativeRegularContactCollector>);
static_assert(std::is_trivially_copyable_v<NativeRegularContact>);
}
