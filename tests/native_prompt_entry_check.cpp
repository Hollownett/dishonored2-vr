#include "../src/native_prompt_entry.hpp"
#include <cassert>
#include <iostream>
#include <map>
#include <vector>
namespace {
std::map<std::uintptr_t,unsigned char> memory;
template<class T> void put(std::uintptr_t address,T input) {
    const auto* bytes=reinterpret_cast<const unsigned char*>(&input);
    for(std::size_t i=0;i<sizeof(input);++i)memory[address+i]=bytes[i];
}
bool read(std::uintptr_t address,void* output,std::size_t size) {
    auto* bytes=static_cast<unsigned char*>(output);
    for(std::size_t i=0;i<size;++i) {const auto it=memory.find(address+i);if(it==memory.end())return false;bytes[i]=it->second;}
    return true;
}
constexpr std::uintptr_t base=0x140000000,element=0x200000,entries=0x300000,block=0x400000,slots=0x500000,game=0x600000,owner=0x700000;
constexpr std::uint32_t spawn=0x1230007;
void fixture() {
    memory.clear();put(element+0x80,entries);put(element+0x88,std::uint32_t{1});
    put(block,slots);put(block+0x10,std::uint32_t{6});put(block+0x14,std::uint32_t{6});
    put(slots,std::uint32_t{4});put(slots+0x10,42.);
    put(entries,std::array<unsigned char,0x40>{});put(entries,std::uint32_t{42});put(entries+0x3a,std::uint8_t{1});put(entries+0x3c,spawn);
    put(base+0x39a5290,game);put(game+0x3130b0+(spawn>>16)*8ull,owner);put(owner+0xa8,spawn);
}
auto resolve(std::uintptr_t caller=0x38f963) {return d2vr::native_prompt_entry::resolve(read,base,element,caller,block);}
}
int main() {
    fixture();const auto good=resolve();assert(good && good->owner==owner && good->spawn==spawn && good->key==42);
    assert(!d2vr::native_prompt_entry::refresh(read,base,*good)); // Add has not returned to activate the entry yet
    assert(d2vr::native_prompt_entry::refresh(read,base,*good,false));
    put(entries+0x3a,std::uint8_t{4});assert(d2vr::native_prompt_entry::refresh(read,base,*good));
    // Removing an unrelated entry moves the last record into its slot.
    std::array<unsigned char,0x40> active{};assert(read(entries,active.data(),active.size()));
    put(entries+0x40,active);put(entries,std::array<unsigned char,0x40>{});put(entries,std::uint32_t{99});
    put(element+0x88,std::uint32_t{2});
    const auto moved=d2vr::native_prompt_entry::refresh(read,base,*good);
    assert(moved && moved->entry==entries+0x40 && d2vr::native_prompt_entry::same_source(*good,*moved));
    put(entries,active);put(element+0x88,std::uint32_t{1});
    assert(d2vr::native_prompt_entry::refresh(read,base,*moved)); // compacted again; old entry address is irrelevant
    put(entries+0x3a,std::uint8_t{12});assert(!d2vr::native_prompt_entry::refresh(read,base,*good,false));
    put(entries+0x3a,std::uint8_t{4});put(entries,std::uint32_t{43});assert(!d2vr::native_prompt_entry::refresh(read,base,*good));
    put(entries,std::uint32_t{42});put(entries+0x3c,spawn+1);put(owner+0xa8,spawn+1);
    assert(!d2vr::native_prompt_entry::refresh(read,base,*good)); // reused entity pointer with a different spawn generation
    fixture();put(element+0x88,std::uint32_t{0});assert(!d2vr::native_prompt_entry::refresh(read,base,*good));
    fixture();
    put(owner+0xa8,spawn+1);assert(!resolve());
    fixture();put(entries+0x3c,std::uint32_t{0x3fff0010});assert(!resolve());
    fixture();put(entries+0x3a,std::uint8_t{4});assert(!resolve());put(block+0x14,std::uint32_t{2});assert(resolve(0x38f743));
    fixture();put(slots+0x10,42.5);assert(!resolve());
    fixture();put(slots,std::uint32_t{3});assert(!resolve());
    fixture();put(element+0x88,std::uint32_t{2});put(entries+0x40,std::array<unsigned char,0x40>{});put(entries+0x40,std::uint32_t{42});assert(!resolve());
    fixture();put(element+0x88,std::uint32_t{257});assert(!resolve());
    fixture();assert(d2vr::native_prompt_entry::event_key(read,0x38f963,block)==42);
    put(block+0x14,std::uint32_t{2});assert(d2vr::native_prompt_entry::event_key(read,0x38f743,block)==42);
    put(block+0x14,std::uint32_t{1});assert(d2vr::native_prompt_entry::event_key(read,0x3906a9,block)==42);
    assert(!d2vr::native_prompt_entry::event_key(read,0x38f963,block));
    assert(!d2vr::native_prompt_entry::event_key(read,0x123456,block));
    put(slots+0x10,42.5);assert(!d2vr::native_prompt_entry::event_key(read,0x3906a9,block));
    put(slots+0x10,42.);put(slots,std::uint32_t{3});assert(!d2vr::native_prompt_entry::event_key(read,0x3906a9,block)); // batched native Remove array is not a numeric key
    fixture();assert(!resolve(0x3906a9));assert(!d2vr::native_prompt_entry::verified_abi(read,base));
    std::cout<<"PASS: native prompt entry uses exact argument key and spawn owner; rejects ambiguous/stale entries\n";
}
