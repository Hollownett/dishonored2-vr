// v1296: BodylessMotionGuard::doomed over fake engine memory (the 2026-11-13 crash shape and the cases that must pass through).
#include "../src/bodyless_motion_guard.hpp"
#include <cassert>
#include <cstdio>
#include <map>
static std::map<std::uintptr_t,unsigned char> mem;
static void put(std::uintptr_t a,const void* v,std::size_t n){for(std::size_t i=0;i<n;++i)mem[a+i]=static_cast<const unsigned char*>(v)[i];}
static bool rd(std::uintptr_t a,void* o,std::size_t n){for(std::size_t i=0;i<n;++i){auto f=mem.find(a+i);if(f==mem.end())return false;static_cast<unsigned char*>(o)[i]=f->second;}return true;}
static void system_at(std::uintptr_t s,std::uint32_t flags,std::int32_t count,std::initializer_list<std::uint32_t> ids){
    std::uintptr_t table=s+0x1000,list=s+0x2000;put(s+0x1c,&flags,4);put(s+0x138,&count,4);put(s+0x100,&table,8);put(table+0x28,&list,8);
    std::uint32_t i=0;for(auto id:ids){put(list+4*i,&id,4);++i;}}
int main(){
    using G=d2vr::BodylessMotionGuard;const std::uint16_t all=0xfffe;
    system_at(0x10000,0x8,2,{0xffffffu,0x3ffffffu});            // crash shape: every body slot dead (high byte is flags)
    assert(G::doomed(rd,0x10000,all,all));
    assert(!G::doomed(rd,0x10000,3,all));                        // a named bone: not this path
    system_at(0x20000,0x8,3,{0xffffff,0x1234,0xffffff});         // one live body: engine finds it
    assert(!G::doomed(rd,0x20000,all,all));
    system_at(0x30000,0x4,2,{0xffffff,0xffffff});                // flag test fails: engine skips the switch itself
    assert(!G::doomed(rd,0x30000,all,all));
    system_at(0x40000,0xc,0,{});                                 // no bodies at all
    assert(G::doomed(rd,0x40000,all,all));
    assert(!G::doomed(rd,0x50000,all,all));                      // unreadable: unchanged behaviour
    assert(G::forbidden_body(0) && G::forbidden_body(0x5000000) && G::forbidden_body(0xffffff) && G::forbidden_body(0xffffffff));   // world body / invalid id (high byte = flags)
    assert(!G::forbidden_body(1) && !G::forbidden_body(0x123));
    std::puts("bodyless_motion_guard_check ok");return 0;}
