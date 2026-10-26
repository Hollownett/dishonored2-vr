#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../src/native_player_depth.hpp"
#include <cassert>
#include <iostream>
#include <limits>

namespace {
using Depth=d2vr::NativePlayerDepth;
struct Memory {
    static constexpr std::uintptr_t base=0x140000000,text=0x150000000;
    Depth::Value cache{5,5.f};
    std::array<unsigned char,20> code=Depth::setter_code;
    std::array<char,18> name{'r','_','p','l','a','y','e','r','D','e','p','t','h','H','a','c','k','\0'};
    unsigned writes{};bool fail{},partial{};float player=5,left=5,right=5;
    bool operator()(std::uintptr_t at,void* to,std::size_t count) const {
        if(at==base+Depth::setter_rva && count==code.size())std::memcpy(to,code.data(),count);
        else if(at==base+Depth::cvar_rva+0x30 && count==8) {const auto p=text;std::memcpy(to,&p,count);}
        else if(at==text && count==name.size())std::memcpy(to,name.data(),count);
        else if(at==base+Depth::cvar_rva+0x28 && count==sizeof(cache))std::memcpy(to,&cache,count);
        else return false;
        return true;
    }
    bool set(std::uintptr_t method,std::uintptr_t object,float scalar) {
        assert(method==base+Depth::setter_rva && object==base+Depth::cvar_rva);++writes;
        if(fail)return false;
        cache={static_cast<LONG>(scalar),scalar};
        if(partial)return false;
        player=left=right=scalar;return true;
    }
};
}
int main() {
    Memory m;Depth depth;
    auto set=[&](auto at,auto object,float scalar){return m.set(at,object,scalar);};
    assert(depth.start(m.base,m));assert(!depth.start(m.base,m));assert(m.writes==0);
    depth.service(m,set);assert(m.writes==1 && m.cache.scalar==0 && m.left==0 && m.right==0);
    depth.service(m,set);assert(m.writes==1); // no per-frame string writes
    // Native load/equip listener restored 5: re-enter through the setter, so
    // inherited arm model depth follows the global override on the same tick.
    m.cache={5,5.f};m.player=m.left=m.right=5;
    depth.service(m,set);assert(m.writes==2 && m.player==0 && m.left==0 && m.right==0);
    assert(!depth.stop_and_wait(0) && depth.pending() && m.writes==2);
    assert(!depth.start(m.base,m)); // no dispatcher => no pretend restoration/restart
    depth.service(m,set);assert(m.writes==3 && m.cache.scalar==5 && m.left==5 && m.right==5);
    assert(depth.stop_and_wait(0) && !depth.needs_service());
    assert(depth.start(m.base,m));m.fail=true;depth.service(m,set);
    assert(depth.pending());depth.request_stop();m.fail=false;depth.service(m,set);
    assert(!depth.pending() && m.cache.scalar==5); // call failed before writing; preserve foreign/native pair
    assert(depth.start(m.base,m));m.partial=true;depth.service(m,set);
    assert(depth.pending() && m.cache.scalar==0);depth.request_stop();
    assert(!depth.stop_and_wait(0));m.partial=false;depth.service(m,set);
    assert(!depth.pending() && m.player==5 && m.cache.scalar==5);
    assert(depth.start(m.base,m));depth.service(m,set);depth.request_stop();
    m.cache={7,7.f};m.player=m.left=m.right=7;const auto before=m.writes;
    depth.service(m,set);assert(!depth.pending() && m.writes==before && m.left==7);
    // Bad/unsupported names, ABI, and scalar caches never acquire ownership.
    m.name[0]='x';assert(!depth.start(m.base,m));m.name[0]='r';
    m.code[0]=0;assert(!depth.start(m.base,m));m.code=Depth::setter_code;
    m.cache={1,.5f};assert(!depth.start(m.base,m));
    m.cache={0,std::numeric_limits<float>::quiet_NaN()};assert(!depth.start(m.base,m));
    m.cache={0,0.f};assert(depth.start(m.base,m));depth.service(m,set);
    assert(!depth.pending());assert(depth.stop_and_wait(0));
    std::cout<<"PASS native player depth: load reset, inherited arms, native listener writes, stop/restore retry, foreign values, ABI/name/cache rejection\n";
}
