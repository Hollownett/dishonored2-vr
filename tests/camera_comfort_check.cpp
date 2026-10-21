#include "../src/camera_comfort.hpp"
#include <cassert>
#include <iostream>
#include <map>

struct Memory {
    std::map<std::uintptr_t,std::byte> bytes;
    unsigned writes{};
    std::uintptr_t fail{};
    template<class T> void put(std::uintptr_t address,T value) {
        for(unsigned i=0;i<sizeof(T);++i) bytes[address+i]=reinterpret_cast<const std::byte*>(&value)[i];
    }
    bool read(std::uintptr_t address,void* target,std::size_t size) {
        for(std::size_t i=0;i<size;++i) {
            auto found=bytes.find(address+i);if(found==bytes.end()) return false;
            static_cast<std::byte*>(target)[i]=found->second;
        }
        return true;
    }
    bool exchange(std::uintptr_t address,std::uint64_t expected,std::uint64_t desired) {
        std::uint64_t actual{};
        if(address==fail || !read(address,&actual,8) || actual!=expected) return false;
        put(address,desired);++writes;return true;
    }
    int integer(std::uintptr_t address) {int result{};assert(read(address,&result,4));return result;}
};

int main() {
    constexpr std::uintptr_t base=0x140000000,bob=base+0x398e200,shakes=base+0x39a41a0,nobob=base+0x39a3490;
    Memory memory;
    for(auto address:{bob,shakes,nobob}) for(unsigned i=0;i<0x50;++i) memory.bytes[address+i]=std::byte{};
    memory.put(bob+0x30,base+0x1c5a9c8);memory.put(shakes+0x30,base+0x1c8dec8);
    memory.put(bob+0x48,4u);memory.put(shakes+0x48,1u);
    memory.put(nobob+0x30,base+0x1c8cf60);memory.put(nobob+0x48,9u);
    memory.put(bob+0x28,1);memory.put(bob+0x2c,1.f);
    auto read=[&](std::uintptr_t a,void* b,std::size_t n) {return memory.read(a,b,n);};
    auto exchange=[&](std::uintptr_t a,std::uint64_t b,std::uint64_t c) {return memory.exchange(a,b,c);};
    const auto baseline=memory.bytes;
    d2vr::CameraComfort comfort;
    comfort.apply(base,read,exchange);
    assert(comfort.active() && memory.integer(bob+0x28)==0 && memory.integer(shakes+0x28)==1);
    // applies_three_entries_atomically: pm_noBob (flags 1|8 as the native ctor stores) becomes 1/1.0.
    float one{};assert(memory.integer(nobob+0x28)==1 && memory.read(nobob+0x2c,&one,4) && one==1.f);
    assert(comfort.restore(read,exchange) && !comfort.active() && memory.bytes==baseline);

    // A later native/user value survives shutdown, including a fractional bob.
    comfort.apply(base,read,exchange);memory.put(bob+0x28,0);memory.put(bob+0x2c,.5f);
    assert(comfort.restore(read,exchange) && comfort.preserved_changes()==1);
    float half{};assert(memory.read(bob+0x2c,&half,4) && half==.5f);
    memory.bytes=baseline;

    // Activation failure on the second variable rolls back the first.
    memory.fail=shakes+0x28;bool rejected=false;
    try {comfort.apply(base,read,exchange);} catch(const std::runtime_error&) {rejected=true;}
    assert(rejected && !comfort.active() && memory.bytes==baseline);memory.fail=0;

    // An unreadable restore stays owned and can be retried without losing baseline.
    comfort.apply(base,read,exchange);const auto saved=memory.bytes[bob+0x28];memory.bytes.erase(bob+0x28);
    assert(!comfort.restore(read,exchange) && comfort.active());memory.bytes[bob+0x28]=saved;
    assert(comfort.restore(read,exchange) && memory.bytes==baseline);

    memory.put(shakes+0x30,base+0x1c8dec9);const auto writes=memory.writes;rejected=false;
    try {comfort.apply(base,read,exchange);} catch(const std::runtime_error&) {rejected=true;}
    assert(rejected && memory.writes==writes && !comfort.active());
    // third_entry_mismatch_changes_nothing: wrong type on pm_noBob rejects before any write.
    memory.put(nobob+0x48,4u);const auto before_third=memory.bytes;rejected=false;
    try {comfort.apply(base,read,exchange);} catch(const std::runtime_error&) {rejected=true;}
    assert(rejected && memory.writes==writes && !comfort.active() && memory.bytes==before_third);
    memory.put(nobob+0x48,9u);memory.put(nobob+0x30,base+0x1c8cf61);rejected=false;
    try {comfort.apply(base,read,exchange);} catch(const std::runtime_error&) {rejected=true;}
    assert(rejected && memory.writes==writes && !comfort.active());
    std::cout<<"PASS: comfort cache identity, atomic activation, exact restoration, preserved user changes, partial rollback and failed-read retry; synthetic memory only\n";
}
