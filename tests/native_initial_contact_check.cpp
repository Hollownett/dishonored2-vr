#include "../src/native_initial_contact.hpp"
#include <cassert>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>

namespace {
using Collector=d2vr::NativeInitialContactCollector;
constexpr std::uintptr_t base=0x140000000;
std::map<std::uintptr_t,unsigned char> memory;
bool failed{},thrown{},faulted{};
template<class Value>void put(std::uintptr_t address,const Value& value) {
    const auto* bytes=reinterpret_cast<const unsigned char*>(&value);
    for(std::size_t offset=0;offset<sizeof(value);++offset)memory[address+offset]=bytes[offset];
}
bool read(std::uintptr_t address,void* output,std::size_t size) {
    if(thrown)throw std::runtime_error("read");if(faulted)RaiseException(0xe0426666,0,0,nullptr);if(failed)return false;
    for(std::size_t offset=0;offset<size;++offset) {const auto found=memory.find(address+offset);if(found==memory.end())return false;static_cast<unsigned char*>(output)[offset]=found->second;}
    return true;
}
void profile() {
    for(unsigned index=0;index<5;++index)put(base+Collector::native_vtable_rva+index*8ull,base+Collector::native_slots[index]);
    put(base+Collector::native_slots[0],Collector::getter_code);put(base+Collector::native_slots[4],Collector::noop_code);
}
template<class Value>void field(d2vr::NativeInitialContactRecord& record,unsigned offset,const Value& value) {std::memcpy(record.bytes.data()+offset,&value,sizeof(value));}
d2vr::NativeInitialContactRecord record() {
    d2vr::NativeInitialContactRecord result{};
    field(result,0,std::array<float,3>{1,2,3});field(result,0x10,std::array<float,3>{-1,0,0});field(result,0x20,-.8f);
    field(result,0x28,std::uint32_t{0x12000001});field(result,0x48,std::uint32_t{0x13000002});field(result,0x50,std::uint32_t{42});return result;
}
}
int main() {
    profile();auto reader=&read;Collector collector{};d2vr::NativeInitialContactTable table{};
    assert(Collector::initialize(collector,table,base,reader) && table.type_locator==0);
    const auto source=record();const auto original_memory=memory;
    assert(collector.begin() && !collector.complete());
    using Add=void (__fastcall*)(Collector*,const void*);reinterpret_cast<Add>(table.entries[3])(&collector,&source);
    assert(!collector.contact(0));
    assert(collector.finish() && collector.count==1 && memory==original_memory);
    const auto hit=collector.contact(0);assert(hit && hit->point.x==1 && hit->normal.x==-1 && hit->signed_separation==-.8f);
    assert(hit->source_body==0x12000001 && hit->body==0x13000002 && hit->shape_key==42 && hit->normal_valid);
    assert(!collector.contact(1) && collector.limit[0]==1 && !collector.stop && collector.mode==0);
    auto nonunit=source;field(nonunit,0x10,std::array<float,3>{-1.1f,0,0});assert(collector.begin());Collector::add_hit(&collector,&nonunit);
    assert(collector.finish() && collector.contact(0)->normal.x==-1);
    assert(collector.begin() && collector.finish() && !collector.count);
    for(unsigned corruption=0;corruption<3;++corruption) {
        assert(collector.begin());if(corruption==0)collector.mode=1;if(corruption==1)collector.stop=1;if(corruption==2)collector.limit[0]=-.8f;
        assert(!collector.finish() && collector.incomplete);
    }
    using Type=const void* (__fastcall*)(const Collector*);assert(reinterpret_cast<Type>(table.entries[0])(&collector)==reinterpret_cast<void*>(base+Collector::metadata_rva));
    using Reset=void (__fastcall*)(Collector*);reinterpret_cast<Reset>(table.entries[2])(&collector);
    assert(!collector.complete() && !collector.count && !collector.incomplete);
    assert(collector.begin());for(unsigned index=0;index<32;++index)Collector::add_hit(&collector,&source);
    assert(collector.finish() && collector.count==32);
    assert(collector.begin());for(unsigned index=0;index<33;++index)Collector::add_hit(&collector,&source);
    assert(!collector.finish() && collector.overflow && collector.incomplete && collector.count==32 && collector.limit[0]==1 && !collector.stop);
    assert(!collector.contact(0) && !collector.contact(31));
    assert(collector.begin());Collector::add_hit(&collector,&source);Collector::reset_native(&collector);
    assert(collector.count==1 && collector.misuse && !collector.finish() && !collector.contact(0));
    assert(collector.begin());assert(!collector.begin() && !collector.finish());
    for(const auto offset:std::array<unsigned,7>{0,4,8,0x10,0x14,0x18,0x20}) {
        auto invalid=source;field(invalid,offset,std::numeric_limits<float>::quiet_NaN());assert(collector.begin());Collector::add_hit(&collector,&invalid);assert(!collector.finish() && !collector.contact(0));
    }
    for(const auto normal:std::array<std::array<float,3>,3>{{{0,0,0},{4,0,0},{0,0,std::numeric_limits<float>::infinity()}}}) {
        auto invalid=source;field(invalid,0x10,normal);assert(collector.begin());Collector::add_hit(&collector,&invalid);assert(!collector.finish());
    }
    auto invalid=source;field(invalid,0x20,.001f);assert(collector.begin());Collector::add_hit(&collector,&invalid);assert(!collector.finish());
    assert(collector.begin());Collector::add_hit(&collector,reinterpret_cast<void*>(1));assert(!collector.finish() && !collector.count);
    assert(collector.begin());Collector::add_hit(&collector,nullptr);assert(!collector.finish());
    assert(collector.begin());using Destroy=Collector* (__fastcall*)(Collector*,unsigned);
    assert(reinterpret_cast<Destroy>(table.entries[1])(&collector,1)==&collector && collector.misuse && !collector.finish());
    assert(Collector::initialize(collector,table,base,reader));Collector::add_hit(&collector,&source);assert(collector.misuse && !collector.complete());
    assert(collector.begin());using Notify=void (__fastcall*)(Collector*);reinterpret_cast<Notify>(table.entries[4])(&collector);
    Collector::reset_native(&collector);reinterpret_cast<Notify>(table.entries[4])(&collector);assert(!collector.finish() && collector.misuse);
    assert(collector.begin());assert(!Collector::initialize(collector,table,base,reader) && !collector.finish());
    for(auto* flag:std::array<bool*,3>{&failed,&thrown,&faulted}) {*flag=true;assert(!Collector::verify(base,reader));*flag=false;}
    for(unsigned index=0;index<5;++index) {profile();memory[base+Collector::native_vtable_rva+index*8ull]^=1;assert(!Collector::verify(base,reader));}
    profile();assert(Collector::verify(base,reader));
    std::cout<<"PASS fixed32 initial collector POD/typed separation, exact five-slot ABI, overflow/fault/invalid normal, active reset/delete/reinitialize misuse and native profile rejection\n";
}
