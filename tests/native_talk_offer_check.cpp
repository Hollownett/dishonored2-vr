#include "../src/native_talk_offer.hpp"
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>

namespace {
struct Memory {
    std::map<std::uintptr_t,unsigned char> bytes;
    std::uintptr_t fail{};
    std::function<void(std::uintptr_t)> before;
    template<class T> void put(std::uintptr_t address,const T& value) {
        const auto* source=reinterpret_cast<const unsigned char*>(&value);
        for(std::size_t index=0;index<sizeof(T);++index) bytes[address+index]=source[index];
    }
    bool operator()(std::uintptr_t address,void* output,std::size_t size) {
        if(before) before(address);
        if(address==fail) return false;
        auto* destination=static_cast<unsigned char*>(output);
        for(std::size_t index=0;index<size;++index) {
            const auto found=bytes.find(address+index);
            if(found==bytes.end()) return false;
            destination[index]=found->second;
        }
        return true;
    }
};
struct Fixture {
    static constexpr std::uintptr_t base=0x140000000,game=0x20000000,player=0x21000000,owner=0x22000000,
        sensor=0x23000000,machine=0x24000000,interaction=0x25000000,component=0x26000000,
        declaration=0x27000000,selected=0x28000000,link=0x29000000,vtable=0x2a000000;
    static constexpr std::uint32_t spawn=0x12000034,generation=5;
    Memory memory;
    Fixture() {
        using namespace d2vr;
        using namespace d2vr::rig;
        memory.put(base+0x39a5290,game);memory.put(game,base+0x1c940a8);
        memory.put(game+0x3130b0,player);memory.put(game+0x3130b0+(spawn>>16)*8,owner);
        memory.put(player,base+0x1c5e428);memory.put(owner+0xa8,spawn);
        memory.put(sensor,base+native_talk_offer_detail::sensor_vtable);memory.put(sensor+0x10,player);
        memory.put(sensor+0x18,Identity{7,0xa});memory.put(sensor+0x20,machine);
        memory.put(sensor+0x98,selected);memory.put(sensor+0xa0,std::uint32_t{9});
        memory.put(sensor+0xa8,declaration+native_talk_offer_detail::talk_offset);
        memory.put(sensor+0xb0,std::uintptr_t{0});
        memory.put(machine,base+native_talk_offer_detail::machine_vtable);memory.put(machine+0x318,interaction);
        memory.put(interaction,base+native_talk_offer_detail::interaction_vtable);memory.put(interaction+0x10,player);
        memory.put(interaction+0x30,static_cast<std::int32_t>(generation));memory.put(interaction+0x38,component);
        memory.put(component,vtable);memory.put(vtable+0xd0,base+native_talk_offer_detail::selector_rva);
        memory.put(component+8,declaration);memory.put(component+0x10,owner);
        memory.put(component+0x18,Identity{static_cast<std::int32_t>(generation),0xa});
        memory.put(selected+0x18,link);memory.put(selected+0x44,std::uint32_t{9});memory.put(link+0x10,owner);
        memory.put(declaration+native_talk_offer_detail::talk_offset,std::uintptr_t{0x2b000000});
        memory.put(declaration+native_talk_offer_detail::busy_talk_offset,std::uintptr_t{0x2c000000});
    }
    auto receipt() {return d2vr::native_talk_offer(memory,base,sensor,player,component,generation,owner,spawn);}
};
}

int main() {
    {
        Fixture fixture;const auto receipt=fixture.receipt();assert(receipt);
        assert(d2vr::native_talk_offer_current(fixture.memory,Fixture::base,*receipt));
        fixture.memory.put(Fixture::sensor+0xb0,Fixture::declaration+d2vr::native_talk_offer_detail::talk_offset);
        assert(!d2vr::native_talk_offer_current(fixture.memory,Fixture::base,*receipt));
    }
    {
        Fixture fixture;fixture.memory.put(Fixture::sensor+0xa8,Fixture::declaration+d2vr::native_talk_offer_detail::busy_talk_offset);
        assert(!fixture.receipt());
        fixture.memory.put(Fixture::sensor+0xa8,Fixture::declaration+d2vr::native_talk_offer_detail::talk_offset);
        fixture.memory.put(Fixture::sensor+0xb0,Fixture::declaration+d2vr::native_talk_offer_detail::busy_talk_offset);
        assert(!fixture.receipt());
    }
    {
        Fixture fixture;fixture.memory.put(Fixture::selected+0x18,Fixture::link+8);
        assert(!fixture.receipt());
        fixture.memory.put(Fixture::selected+0x18,Fixture::link);fixture.memory.put(Fixture::link+0x10,Fixture::player);
        assert(!fixture.receipt());
    }
    {
        Fixture fixture;auto receipt=fixture.receipt();assert(receipt);
        fixture.memory.put(Fixture::interaction+0x38,Fixture::machine);assert(!d2vr::native_talk_offer_current(fixture.memory,Fixture::base,*receipt));
        fixture.memory.put(Fixture::interaction+0x38,Fixture::component);
        fixture.memory.put(Fixture::component+0x18,d2vr::rig::Identity{6,0xa});assert(!d2vr::native_talk_offer_current(fixture.memory,Fixture::base,*receipt));
    }
    {
        Fixture fixture;auto receipt=fixture.receipt();assert(receipt);
        fixture.memory.put(Fixture::game+0x3130b0+(Fixture::spawn>>16)*8,Fixture::machine);
        assert(!d2vr::native_talk_offer_current(fixture.memory,Fixture::base,*receipt));
    }
    {
        const std::array<std::uintptr_t,10> changing_addresses{
            Fixture::sensor+0x98,Fixture::sensor+0xa0,Fixture::sensor+0xa8,Fixture::sensor+0xb0,
            Fixture::component+8,Fixture::interaction+0x38,Fixture::component,Fixture::vtable+0xd0,
            Fixture::interaction+0x30,Fixture::machine+0x318};
        for(const auto address:changing_addresses) {
            Fixture fixture;auto receipt=fixture.receipt();assert(receipt);unsigned reads{};
            fixture.memory.before=[&](std::uintptr_t current) {
                if(current==address && ++reads==2) {
                    if(address==Fixture::sensor+0x98) fixture.memory.put(address,Fixture::link);
                    else if(address==Fixture::sensor+0xa0) fixture.memory.put(address,std::uint32_t{10});
                    else if(address==Fixture::sensor+0xa8) fixture.memory.put(address,Fixture::declaration+Fixture::generation);
                    else if(address==Fixture::sensor+0xb0) fixture.memory.put(address,Fixture::declaration+0x110);
                    else if(address==Fixture::component+8) fixture.memory.put(address,Fixture::machine);
                    else if(address==Fixture::interaction+0x38) fixture.memory.put(address,Fixture::machine);
                    else if(address==Fixture::component) fixture.memory.put(address,Fixture::machine);
                    else if(address==Fixture::vtable+0xd0) fixture.memory.put(address,Fixture::base+0x123456);
                    else if(address==Fixture::interaction+0x30) fixture.memory.put(address,std::int32_t{6});
                    else fixture.memory.put(address,Fixture::sensor);
                }
            };
            assert(!d2vr::native_talk_offer_current(fixture.memory,Fixture::base,*receipt));
        }
    }
    {
        Fixture fixture;fixture.memory.fail=Fixture::sensor+0xb0;assert(!fixture.receipt());
        fixture.memory.fail=0;fixture.memory.put(Fixture::vtable+0xd0,Fixture::base+0x123456);
        assert(!fixture.receipt());
    }
}
