#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "arm_ik.hpp"
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <type_traits>

namespace d2vr {
struct NativeInitialContact {
    Vec3 point{},normal{};
    float signed_separation{};
    std::uint32_t source_body{},body{},shape_key{};
    bool normal_valid{};
};
struct NativeInitialContactRecord {std::array<unsigned char,0x70> bytes{};};
struct NativeInitialContactTable {std::uintptr_t type_locator{};std::array<std::uintptr_t,5> entries{};};
struct alignas(16) NativeInitialContactCollector {
    std::uintptr_t vtable{};
    std::uint32_t mode{},count{};
    std::uint8_t stop{};
    std::array<unsigned char,15> padding{};
    std::array<float,4> limit{1,1,1,1};
    std::array<NativeInitialContactRecord,32> records{};
    std::uintptr_t metadata{};
    bool ready{},active{},finished{},incomplete{},overflow{},misuse{};
    std::array<unsigned char,2> tail_padding{};

    inline static constexpr std::uintptr_t native_vtable_rva=0x1ad5010,metadata_rva=0x23b7e90;
    inline static constexpr std::array<std::uintptr_t,5> native_slots{0xa95260,0x90e80,0x99d50,0xca53a0,0x998b0};
    inline static constexpr std::array<unsigned char,8> getter_code{0x48,0x8d,0x05,0x29,0x2c,0x92,0x01,0xc3};
    inline static constexpr std::array<unsigned char,3> noop_code{0xc2,0,0};

    template<class Read> static bool verify(std::uintptr_t base,Read& read) noexcept {
        if(!base)return false;
        std::array<std::uintptr_t,5> slots{};std::array<unsigned char,8> getter{};std::array<unsigned char,3> noop{};
        if(!guarded_read(read,base+native_vtable_rva,slots.data(),sizeof(slots)) ||
           !guarded_read(read,base+native_slots[0],getter.data(),getter.size()) || getter!=getter_code ||
           !guarded_read(read,base+native_slots[4],noop.data(),noop.size()) || noop!=noop_code)return false;
        for(unsigned index=0;index<slots.size();++index)if(slots[index]!=base+native_slots[index])return false;
        return true;
    }
    template<class Read> static bool initialize(NativeInitialContactCollector& output,NativeInitialContactTable& table,
                                               std::uintptr_t base,Read& read) noexcept {
        if(output.active) {output.incomplete=true;output.misuse=true;return false;}
        if(!verify(base,read)) {output.ready=false;output.incomplete=true;return false;}
        table.type_locator=0;
        table.entries={reinterpret_cast<std::uintptr_t>(&type),reinterpret_cast<std::uintptr_t>(&destroy),
            reinterpret_cast<std::uintptr_t>(&reset_native),reinterpret_cast<std::uintptr_t>(&add_hit),reinterpret_cast<std::uintptr_t>(&notify)};
        output={};output.vtable=reinterpret_cast<std::uintptr_t>(table.entries.data());output.metadata=base+metadata_rva;output.ready=true;
        return true;
    }
    bool begin() noexcept {
        if(!ready || active) {incomplete=true;misuse=true;return false;}
        reset_native(this);active=true;return true;
    }
    bool finish() noexcept {
        if(!active) {incomplete=true;misuse=true;return false;}
        if(mode || stop || count>records.size() || limit!=std::array<float,4>{1,1,1,1})incomplete=true;
        active=false;finished=true;return complete();
    }
    bool complete() const noexcept {return ready && finished && !active && !incomplete && !overflow && !misuse && count<=records.size();}
    std::optional<NativeInitialContact> contact(unsigned index) const noexcept {
        if(!complete() || index>=count || index>=records.size())return {};
        return decode(records[index]);
    }
    static const void* __fastcall type(const NativeInitialContactCollector* collector) noexcept {
        return collector?reinterpret_cast<const void*>(collector->metadata):nullptr;
    }
    static NativeInitialContactCollector* __fastcall destroy(NativeInitialContactCollector* collector,unsigned) noexcept {
        if(collector) {collector->incomplete=true;collector->misuse=true;collector->ready=false;}
        return collector;
    }
    static void __fastcall reset_native(NativeInitialContactCollector* collector) noexcept {
        if(!collector)return;
        if(collector->active) {collector->incomplete=true;collector->misuse=true;return;}
        collector->mode=collector->count=0;collector->stop=0;collector->padding={};collector->limit={1,1,1,1};collector->records={};
        collector->finished=collector->incomplete=collector->overflow=collector->misuse=false;
    }
    static void __fastcall add_hit(NativeInitialContactCollector* collector,const void* input) noexcept {
        if(!collector)return;
        if(!collector->ready || !collector->active) {collector->incomplete=true;collector->misuse=true;return;}
        if(collector->count>=collector->records.size()) {collector->incomplete=true;collector->overflow=true;return;}
        NativeInitialContactRecord record{};
        if(!copy_record(input,record)) {collector->incomplete=true;return;}
        const auto contact=decode(record);
        if(!contact || !contact->normal_valid)collector->incomplete=true;
        collector->records[collector->count++]=record;
    }
    static void __fastcall notify(NativeInitialContactCollector*) noexcept {}
private:
    template<class Read> static bool cpp_read(Read& read,std::uintptr_t address,void* output,std::size_t size) noexcept {
        try {return read(address,output,size);}catch(...) {return false;}
    }
    template<class Read> static bool guarded_read(Read& read,std::uintptr_t address,void* output,std::size_t size) noexcept {
        __try {return cpp_read(read,address,output,size);}__except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    static bool copy_record(const void* input,NativeInitialContactRecord& output) noexcept {
        if(!input)return false;
        __try {std::memcpy(output.bytes.data(),input,output.bytes.size());return true;}__except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    static std::optional<NativeInitialContact> decode(const NativeInitialContactRecord& record) noexcept {
        std::array<float,3> point{},normal{};NativeInitialContact result{};
        std::memcpy(point.data(),record.bytes.data(),12);std::memcpy(normal.data(),record.bytes.data()+0x10,12);
        std::memcpy(&result.signed_separation,record.bytes.data()+0x20,4);
        std::memcpy(&result.source_body,record.bytes.data()+0x28,4);std::memcpy(&result.body,record.bytes.data()+0x48,4);
        std::memcpy(&result.shape_key,record.bytes.data()+0x50,4);
        result.point={point[0],point[1],point[2]};result.normal={normal[0],normal[1],normal[2]};
        if(!finite(result.point) || !finite(result.normal) || !std::isfinite(result.signed_separation) || result.signed_separation>0)return {};
        const auto magnitude=length(result.normal);result.normal_valid=magnitude>.5 && magnitude<1.5;
        if(result.normal_valid)result.normal=result.normal*(1/magnitude);
        return result;
    }
};
static_assert(sizeof(NativeInitialContactRecord)==0x70);
static_assert(offsetof(NativeInitialContactTable,entries)==8);
static_assert(offsetof(NativeInitialContactCollector,mode)==8 && offsetof(NativeInitialContactCollector,count)==0xc);
static_assert(offsetof(NativeInitialContactCollector,stop)==0x10 && offsetof(NativeInitialContactCollector,limit)==0x20);
static_assert(offsetof(NativeInitialContactCollector,records)==0x30);
static_assert(alignof(NativeInitialContactCollector)==16);
static_assert(std::is_trivially_copyable_v<NativeInitialContactCollector> && std::is_standard_layout_v<NativeInitialContactCollector>);
static_assert(std::is_trivially_copyable_v<NativeInitialContactTable> && std::is_trivially_copyable_v<NativeInitialContact>);
}
