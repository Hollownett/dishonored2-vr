#include "../src/native_regular_contact.hpp"
#include <cassert>
#include <iostream>
#include <limits>
#include <map>

namespace {
using Collector=d2vr::NativeRegularContactCollector;
using Record=d2vr::NativeInitialContactRecord;
constexpr std::uintptr_t base=0x140000000;
std::map<std::uintptr_t,unsigned char> memory;
template<class Value>void put(std::uintptr_t address,const Value& value) {
    const auto* bytes=reinterpret_cast<const unsigned char*>(&value);
    for(std::size_t offset=0;offset<sizeof(value);++offset)memory[address+offset]=bytes[offset];
}
bool read(std::uintptr_t address,void* output,std::size_t size) {
    for(std::size_t offset=0;offset<size;++offset) {
        const auto found=memory.find(address+offset);if(found==memory.end())return false;
        static_cast<unsigned char*>(output)[offset]=found->second;
    }
    return true;
}
void profile() {
    for(unsigned index=0;index<5;++index)put(base+d2vr::NativeInitialContactCollector::native_vtable_rva+index*8ull,
        base+d2vr::NativeInitialContactCollector::native_slots[index]);
    put(base+d2vr::NativeInitialContactCollector::native_slots[0],d2vr::NativeInitialContactCollector::getter_code);
    put(base+d2vr::NativeInitialContactCollector::native_slots[4],d2vr::NativeInitialContactCollector::noop_code);
}
template<class Value>void field(Record& record,unsigned offset,const Value& value) {
    std::memcpy(record.bytes.data()+offset,&value,sizeof(value));
}
Record sample(float fraction=.375f,std::array<float,3> normal={0,1.2f,0}) {
    Record output{};field(output,0,std::array<float,3>{2,3,4});field(output,0x10,normal);field(output,0x20,fraction);
    field(output,0x28,std::uint32_t{0x12000001});field(output,0x48,std::uint32_t{0x23000002});
    field(output,0x4c,std::uint16_t{0x4567});field(output,0x50,std::uint32_t{42});return output;
}
using Add=void (__fastcall*)(Collector*,const void*);
}
int main() {
    profile();auto reader=&read;Collector collector{};d2vr::NativeInitialContactTable table{};
    assert(Collector::initialize(collector,table,base,reader));
    assert(collector.native()==reinterpret_cast<d2vr::NativeInitialContactCollector*>(&collector));
    assert(collector.storage.vtable==reinterpret_cast<std::uintptr_t>(table.entries.data()));
    assert(table.entries[3]==reinterpret_cast<std::uintptr_t>(&Collector::add_hit));
    assert(table.entries[0]==reinterpret_cast<std::uintptr_t>(&d2vr::NativeInitialContactCollector::type));
    assert(table.entries[1]==reinterpret_cast<std::uintptr_t>(&d2vr::NativeInitialContactCollector::destroy));
    assert(table.entries[2]==reinterpret_cast<std::uintptr_t>(&d2vr::NativeInitialContactCollector::reset_native));
    assert(table.entries[4]==reinterpret_cast<std::uintptr_t>(&d2vr::NativeInitialContactCollector::notify));
    const auto source=sample();const auto original_memory=memory;
    assert(collector.begin() && !collector.complete() && !collector.contact(0));
    reinterpret_cast<Add>(table.entries[3])(&collector,&source);
    assert(collector.finish() && collector.complete() && collector.count()==1 && memory==original_memory);
    const auto hit=collector.contact(0);assert(hit && hit->point.x==2 && hit->point.y==3 && hit->point.z==4);
    assert(hit->fraction==.375f && hit->normal_valid && hit->normal.y==1);
    assert(hit->source_body==0x12000001 && hit->body==0x23000002 && hit->shape_key==42 && hit->material==0x4567);
    assert((!collector.contact(1) && collector.storage.limit==std::array<float,4>{1,1,1,1}));
    assert(collector.storage.mode==0 && collector.storage.stop==0);
    assert(collector.begin() && collector.finish() && collector.count()==0 && collector.complete());

    const auto bad_normal=sample(.5f,{0,0,0});assert(collector.begin());Collector::add_hit(&collector,&bad_normal);
    assert(collector.finish() && collector.contact(0) && !collector.contact(0)->normal_valid);
    for(const float invalid_fraction:std::array<float,5>{-1.f,1.001f,std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity()}) {
        const auto invalid=sample(invalid_fraction);assert(collector.begin());Collector::add_hit(&collector,&invalid);
        assert(!collector.finish() && !collector.complete() && !collector.contact(0));
    }
    for(const auto point:std::array<std::array<float,3>,2>{{{NAN,0,0},{0,INFINITY,0}}}) {
        auto invalid=sample();field(invalid,0,point);assert(collector.begin());Collector::add_hit(&collector,&invalid);
        assert(!collector.finish() && !collector.contact(0));
    }
    auto nonfinite_normal=sample();field(nonfinite_normal,0x10,std::array<float,3>{0,NAN,0});
    assert(collector.begin());Collector::add_hit(&collector,&nonfinite_normal);assert(!collector.finish());
    assert(collector.begin());Collector::add_hit(&collector,reinterpret_cast<void*>(1));assert(!collector.finish() && !collector.count());
    assert(collector.begin());Collector::add_hit(&collector,nullptr);assert(!collector.finish());

    assert(collector.begin());for(unsigned index=0;index<32;++index)Collector::add_hit(&collector,&source);
    assert(collector.finish() && collector.count()==32 && collector.contact(31));
    assert(collector.begin());for(unsigned index=0;index<33;++index)Collector::add_hit(&collector,&source);
    assert(!collector.finish() && collector.storage.overflow && collector.storage.incomplete && collector.count()==32);
    assert((!collector.contact(0) && !collector.contact(31) && collector.storage.limit==std::array<float,4>{1,1,1,1}));

    using Reset=void (__fastcall*)(d2vr::NativeInitialContactCollector*);
    reinterpret_cast<Reset>(table.entries[2])(collector.native());
    assert(!collector.count() && !collector.storage.incomplete && !collector.storage.overflow && !collector.complete());
    assert(collector.begin());Collector::add_hit(&collector,&source);reinterpret_cast<Reset>(table.entries[2])(collector.native());
    assert(collector.count()==1 && collector.storage.misuse && !collector.finish() && !collector.contact(0));
    assert(collector.begin());using Destroy=d2vr::NativeInitialContactCollector* (__fastcall*)(d2vr::NativeInitialContactCollector*,unsigned);
    assert(reinterpret_cast<Destroy>(table.entries[1])(collector.native(),1)==collector.native());
    assert(collector.storage.misuse && !collector.finish() && !collector.contact(0));
    assert(Collector::initialize(collector,table,base,reader));
    Collector::add_hit(&collector,&source);assert(collector.storage.misuse && !collector.complete());
    assert(collector.begin());assert(!collector.begin() && !collector.finish());
    assert(Collector::initialize(collector,table,base,reader));
    assert(collector.begin());assert(!Collector::initialize(collector,table,base,reader));
    assert(collector.storage.active && collector.storage.incomplete && collector.storage.misuse && !collector.finish());
    assert(Collector::initialize(collector,table,base,reader));
    using Notify=void (__fastcall*)(d2vr::NativeInitialContactCollector*);
    reinterpret_cast<Notify>(table.entries[4])(collector.native());assert(collector.begin());
    reinterpret_cast<Notify>(table.entries[4])(collector.native());assert(collector.finish());
    assert((collector.storage.limit==std::array<float,4>{1,1,1,1} && collector.storage.mode==0 && collector.storage.stop==0));
    std::cout<<"PASS regular fraction collector shares native POD lifecycle, preserves non-pruning limits, validates typed regular records, retains body IDs/key/material, and handles bounds/overflow/misuse\n";
}
