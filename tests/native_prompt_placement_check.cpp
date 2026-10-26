#include "../src/native_prompt_placement.hpp"
#include <cassert>
#include <iostream>
#include <limits>
#include <map>

namespace {
std::map<std::uintptr_t,unsigned char> memory;
constexpr std::uintptr_t base=0x140000000,element=0x200000,entries=0x300000,game=0x600000,owner=0x700000,model=0x800000;
constexpr std::uint32_t spawn=0x1230007;
const d2vr::native_prompt_entry::Receipt receipt{element,entries,owner,42,spawn};
template<class T> void put(std::uintptr_t address,const T& input) {
    const auto* bytes=reinterpret_cast<const unsigned char*>(&input);
    for(std::size_t i=0;i<sizeof(input);++i)memory[address+i]=bytes[i];
}
bool read(std::uintptr_t address,void* output,std::size_t size) {
    auto* bytes=static_cast<unsigned char*>(output);
    for(std::size_t i=0;i<size;++i) {const auto it=memory.find(address+i);if(it==memory.end())return false;bytes[i]=it->second;}
    return true;
}
void fixture() {
    memory.clear();put(element+0x80,entries);put(element+0x88,std::uint32_t{1});
    put(entries,std::array<unsigned char,0x40>{});put(entries,std::uint32_t{42});put(entries+0x3a,std::uint8_t{4});put(entries+0x3c,spawn);
    put(base+0x39a5290,game);put(game+0x3130b0+(spawn>>16)*8ull,owner);put(owner+0xa8,spawn);
    put(owner+0xd0,std::uint8_t{0});put(owner+0xf0,model);put(model,base+0x1b0dd88);
    put(owner+0x300,std::array<float,3>{10,20,30});
    put(model+0x1840,std::array<float,8>{1,2,3,0,3,6,9,0});
}
auto anchor() {return d2vr::native_retained_prompt_anchor(read,base,receipt);}
void expect(double x,double y,double z) {const auto p=anchor();assert(p && std::abs(p->x-x)<1e-5 && std::abs(p->y-y)<1e-5 && std::abs(p->z-z)<1e-5);}
}
int main() {
    fixture();expect(2,4,6);
    // Retained NPC art must use the same body-relative height as the existing
    // non-retained path, even if a static-looking model cache is present.
    put(owner+0xd1,std::uint8_t{8});
    auto npc=d2vr::native_retained_prompt_anchor(read,base,receipt,true);
    assert(npc && npc->x==10 && npc->y==20 && std::abs(npc->z-30.35)<1e-5);
    put(owner+0xd1,std::uint8_t{0x20});
    npc=d2vr::native_retained_prompt_anchor(read,base,receipt,true);
    assert(npc && std::abs(npc->z-30.35)<1e-5);
    put(owner+0xd1,std::uint8_t{0});
    npc=d2vr::native_retained_prompt_anchor(read,base,receipt,true);
    assert(npc && std::abs(npc->z-31.95)<1e-5);
    fixture();assert(!d2vr::native_retained_prompt_anchor(read,base,receipt,true)); // unreadable NPC state
    fixture();put(owner+0xd1,std::uint8_t{8});bool state_changed=false;
    auto waking=[&](std::uintptr_t address,void* output,std::size_t size) {
        const bool ok=read(address,output,size);
        if(address==owner+0x300 && !state_changed) {state_changed=true;put(owner+0xd1,std::uint8_t{0});}
        return ok;
    };
    assert(!d2vr::native_retained_prompt_anchor(waking,base,receipt,true));
    fixture();
    // Small coins/books and broad lying bodies retain their actual world centre.
    put(model+0x1840,std::array<float,8>{10,20,30,0,10.02f,20.04f,30.01f,0});expect(10.01,20.02,30.005);
    put(model+0x1840,std::array<float,8>{-1,-.3f,.1f,0,1,.3f,.5f,0});expect(0,0,.3);
    put(owner+0xd0,std::uint8_t{2});expect(10,20,30); // transformed cache is not world space
    fixture();put(owner+0xf0,std::uintptr_t{0});expect(10,20,30);
    fixture();put(model,base+0x1234);expect(10,20,30); // unrelated model ABI
    fixture();put(model+0x1840,std::array<float,8>{3,2,3,0,1,6,9,0});expect(10,20,30);
    fixture();put(model+0x1840,std::array<float,8>{std::numeric_limits<float>::infinity(),2,3,0,3,6,9,0});expect(10,20,30);
    put(owner+0x300,std::array<float,3>{0,std::numeric_limits<float>::quiet_NaN(),0});assert(!anchor());
    fixture();put(entries+0x3a,std::uint8_t{12});assert(!anchor()); // removal pending
    fixture();put(entries+0x3a,std::uint8_t{1});assert(!anchor()); // Add not active
    fixture();put(element+0x88,std::uint32_t{0});assert(!anchor());
    fixture();put(owner+0xa8,spawn+1);assert(!anchor()); // entity address recycled
    fixture();put(game+0x3130b0+(spawn>>16)*8ull,owner+0x10000);assert(!anchor());
    fixture();std::array<unsigned char,0x40> record{};assert(read(entries,record.data(),record.size()));
    put(entries+0x40,record);put(entries,std::array<unsigned char,0x40>{});put(element+0x88,std::uint32_t{2});expect(2,4,6); // compaction
    fixture();bool changed=false;
    auto mutating=[&](std::uintptr_t address,void* output,std::size_t size) {
        const bool ok=read(address,output,size);
        if(address==model+0x1840 && !changed) {changed=true;put(owner+0xf0,model+0x10000);}
        return ok;
    };
    assert(!d2vr::native_retained_prompt_anchor(mutating,base,receipt));
    fixture();changed=false;
    auto removing=[&](std::uintptr_t address,void* output,std::size_t size) {
        const bool ok=read(address,output,size);
        if(address==model+0x1840 && !changed) {changed=true;put(entries+0x3a,std::uint8_t{12});}
        return ok;
    };
    assert(!d2vr::native_retained_prompt_anchor(removing,base,receipt));
    fixture();changed=false;
    auto recycling=[&](std::uintptr_t address,void* output,std::size_t size) {
        const bool ok=read(address,output,size);
        if(address==model+0x1840 && !changed) {changed=true;put(owner+0xa8,spawn+1);}
        return ok;
    };
    assert(!d2vr::native_retained_prompt_anchor(recycling,base,receipt));
    fixture();changed=false;
    auto compacting=[&](std::uintptr_t address,void* output,std::size_t size) {
        const bool ok=read(address,output,size);
        if(address==model+0x1840 && !changed) {
            changed=true;std::array<unsigned char,0x40> active{};assert(read(entries,active.data(),active.size()));
            put(entries+0x40,active);put(entries,std::array<unsigned char,0x40>{});put(element+0x88,std::uint32_t{2});
        }
        return ok;
    };
    const auto compacted=d2vr::native_retained_prompt_anchor(compacting,base,receipt);
    assert(compacted && compacted->x==2 && compacted->y==4 && compacted->z==6);
    std::cout<<"PASS: retained native prompt anchors use verified world bounds or entity origins and reject stale ownership\n";
}
